#include "../core/Lang.h"
#include "DetailsPanel.h"

#include "../core/Emulators.h"
#include "../core/Library.h"
#include "../core/Registry.h"
#include "../core/Settings.h"
#include "GameDelegate.h"
#include "Theme.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QToolButton>
#include <QVBoxLayout>

// ===========================================================================
// CoverView
// ===========================================================================
CoverView::CoverView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(220, 290);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(&ImageCache::instance(), &ImageCache::loaded, this, qOverload<>(&QWidget::update));
}

void CoverView::setGame(const QString& systemId, const QString& title, const QString& image) {
    m_system = systemId;
    m_title = title;
    m_image = image;
    update();
}

void CoverView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF r = rect();
    QPainterPath clip;
    clip.addRoundedRect(r, 6, 6);
    p.setClipPath(clip);
    const QColor c = Theme::categoryColor(m_system);
    const bool isCover = !m_image.isEmpty() && !m_image.contains(QLatin1String("/icons/"));
    const qreal dpr = devicePixelRatioF();
    QPixmap pm = isCover ? ImageCache::instance().get(m_image, r.size().toSize(), dpr, false) : QPixmap();
    if (!pm.isNull()) {
        p.fillRect(r, c.darker(420));
        const QSizeF s = pm.deviceIndependentSize();
        p.drawPixmap(QPointF(r.center().x() - s.width() / 2, r.center().y() - s.height() / 2), pm);
    } else {
        QPixmap icon;
        if (!m_image.isEmpty()) {
            const bool pixel = QImageReader(m_image).size().width() <= 48;
            icon = ImageCache::instance().get(m_image, QSize(int(r.width() * 0.5), int(r.width() * 0.5)), dpr, pixel);
        }
        GameDelegate::paintLabel(&p, r, m_system, m_title, icon, true);
    }
    p.fillRect(QRectF(r.left(), r.top(), 5, r.height()), c);
}

// ===========================================================================
// DetailsPanel
// ===========================================================================
namespace {
QString formatSize(qint64 b) {
    if (b <= 0)
        return QStringLiteral("—");
    return Lang::locale().formattedDataSize(b, 1, QLocale::DataSizeTraditionalFormat);
}
QString formatPlay(qint64 s) {
    if (s <= 0)
        return QObject::tr("Jamais joué");
    if (s < 60)
        return QObject::tr("Moins d'une minute");
    if (s < 3600)
        return QObject::tr("%1 minutes").arg(s / 60);
    return QObject::tr("%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60);
}
QString factRow(const QString& label, const QString& value) {
    return QStringLiteral("<tr><td style='color:%1; padding:3px 14px 3px 0;'>%2</td><td style='padding:3px 0;'>%3</td></tr>")
        .arg(Theme::Color::muted.name(), label.toHtmlEscaped(), value.toHtmlEscaped());
}
} // namespace

DetailsPanel::DetailsPanel(Library* lib, QWidget* parent) : QWidget(parent), m_lib(lib) {
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("details"));
    setStyleSheet(QStringLiteral("#details { background: %1; border-left: 1px solid %2; }")
                      .arg(Theme::Color::panel.name(), Theme::Color::line.name()));
    setFixedWidth(340);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(20, 22, 20, 20);

    m_empty = new QLabel(tr("Sélectionne un jeu pour voir ses informations."), this);
    m_empty->setWordWrap(true);
    m_empty->setProperty("role", "muted");
    m_empty->setAlignment(Qt::AlignCenter);
    outer->addWidget(m_empty, 1);

    // Contenu défilant : rien ne se chevauche, même sur un petit écran.
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; } QScrollArea > QWidget > QWidget { background: transparent; }"));
    m_content = new QWidget;
    m_scroll->setWidget(m_content);
    auto* v = new QVBoxLayout(m_content);
    v->setContentsMargins(0, 0, 4, 0);
    v->setSpacing(10);
    outer->addWidget(m_scroll, 1);

    m_cover = new CoverView(m_content);
    m_cover->setFixedHeight(380);
    v->addWidget(m_cover);
    v->addSpacing(14);

    m_title = new QLabel(m_content);
    m_title->setWordWrap(true);
    m_title->setFont(Theme::font(26, QFont::Bold, true));
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_title);

    m_system = new QLabel(m_content);
    m_system->setTextFormat(Qt::RichText);
    v->addWidget(m_system);

    m_play = new QPushButton(Theme::icon(QStringLiteral("play"), Qt::white, 18), tr("Jouer"), m_content);
    m_play->setCursor(Qt::PointingHandCursor);
    m_play->setMinimumHeight(48);
    v->addSpacing(4);
    v->addWidget(m_play);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(6);
    auto mkTool = [this](const QString& icon, const QString& tip) {
        auto* b = new QToolButton(m_content);
        b->setIcon(Theme::icon(icon, Theme::Color::muted, 20));
        b->setIconSize(QSize(20, 20));
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        return b;
    };
    m_fav = mkTool(QStringLiteral("star"), tr("Ajouter aux favoris (Y)"));
    m_fav->setText(tr("Favori"));
    m_folder = mkTool(QStringLiteral("folder"), tr("Ouvrir le dossier du jeu"));
    m_folder->setText(tr("Dossier"));
    m_coverBtn = mkTool(QStringLiteral("download"), tr("Changer ou rechercher la jaquette"));
    m_coverBtn->setText(tr("Jaquette"));
    actions->addWidget(m_fav);
    actions->addWidget(m_folder);
    actions->addWidget(m_coverBtn);
    actions->addStretch();
    v->addLayout(actions);

    m_addonInfo = new QLabel(m_content);
    m_addonInfo->setWordWrap(true);
    m_addonInfo->setTextFormat(Qt::RichText);
    v->addWidget(m_addonInfo);
    m_baseBtn = new QPushButton(m_content);
    m_baseBtn->setCursor(Qt::PointingHandCursor);
    m_baseBtn->setMinimumHeight(44);
    m_baseBtn->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    v->addWidget(m_baseBtn);

    m_addonsTitle = new QLabel(tr("Mises à jour et DLC"), m_content);
    m_addonsTitle->setProperty("role", "muted");
    v->addSpacing(6);
    v->addWidget(m_addonsTitle);
    m_addonsBox = new QWidget(m_content);
    auto* ab = new QVBoxLayout(m_addonsBox);
    ab->setContentsMargins(0, 0, 0, 0);
    ab->setSpacing(4);
    v->addWidget(m_addonsBox);

    m_emuLabel = new QLabel(tr("Émulateur"), m_content);
    m_emuLabel->setProperty("role", "muted");
    v->addSpacing(6);
    v->addWidget(m_emuLabel);
    m_emu = new QComboBox(m_content);
    v->addWidget(m_emu);
    m_emuHint = new QLabel(m_content);
    m_emuHint->setWordWrap(true);
    m_emuHint->setProperty("role", "muted");
    v->addWidget(m_emuHint);
    m_install = new QPushButton(Theme::icon(QStringLiteral("download")), tr("Installer"), m_content);
    v->addWidget(m_install);

    m_facts = new QLabel(m_content);
    m_facts->setTextFormat(Qt::RichText);
    m_facts->setWordWrap(true);
    m_facts->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addSpacing(6);
    v->addWidget(m_facts);
    v->addStretch();

    connect(m_play, &QPushButton::clicked, this, [this] { emit playRequested(m_path); });
    connect(m_fav, &QToolButton::clicked, this, [this] { emit favoriteToggled(m_path); });
    connect(m_folder, &QToolButton::clicked, this, [this] { emit openFolderRequested(m_path); });
    connect(m_coverBtn, &QToolButton::clicked, this, [this] {
        emit coverMenuRequested(m_path, m_coverBtn->mapToGlobal(QPoint(0, m_coverBtn->height())));
    });
    connect(m_emu, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        emit emulatorChosen(m_path, m_emu->itemData(i).toString());
    });
    connect(m_install, &QPushButton::clicked, this, [this] { emit installRequested(m_install->property("emu").toString()); });
    connect(m_baseBtn, &QPushButton::clicked, this, [this] { emit showGameRequested(m_baseBtn->property("path").toString()); });

    showGame(QString());
}

void DetailsPanel::setRunning(const QString& runningPath) {
    m_running = runningPath;
    showGame(m_path);
}

void DetailsPanel::showGame(const QString& path) {
    m_path = path;
    const Game* g = path.isEmpty() ? nullptr : m_lib->find(path);
    m_scroll->setVisible(g);
    m_empty->setVisible(!g);
    if (!g)
        return;

    const QString sysId = g->system();
    const SystemInfo* sys = Registry::instance().system(sysId);
    const QColor c = Theme::categoryColor(sysId);
    const Game* addonBase = g->isAddon ? m_lib->baseFor(*g) : nullptr;
    m_cover->setGame(sysId, addonBase ? addonBase->displayTitle() : g->displayTitle(),
                     g->bestImage().isEmpty() && addonBase ? addonBase->bestImage() : g->bestImage());
    m_title->setText(g->isAddon ? Library::addonLabel(*g, addonBase) : g->displayTitle());

    QString sysLine = QStringLiteral("<span style='color:%1; font-weight:600;'>%2</span>")
                          .arg(c.name(), (sys ? sys->name : sysId).toHtmlEscaped());
    if (!g->region.isEmpty())
        sysLine += QStringLiteral("<span style='color:%1;'>&nbsp;&nbsp;%2</span>").arg(Theme::Color::muted.name(), g->region);
    m_system->setText(sysLine);

    const bool running = !m_running.isEmpty();
    const bool thisRunning = running && m_running == path;
    m_play->setStyleSheet(Theme::primaryButtonStyle(Theme::Color::gold));
    m_play->setIcon(Theme::icon(QStringLiteral("play"), Theme::onColor(Theme::Color::gold), 18));
    m_play->setText(thisRunning ? tr("En cours…") : tr("Jouer"));
    m_play->setEnabled(!running && !g->missing);
    m_play->setVisible(!g->isAddon);
    m_fav->setVisible(!g->isAddon);
    m_coverBtn->setVisible(!g->isAddon);

    // Contenus additionnels.
    auto* ab = static_cast<QVBoxLayout*>(m_addonsBox->layout());
    while (QLayoutItem* it = ab->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    m_baseBtn->hide();
    m_addonInfo->hide();
    if (g->isAddon) {
        const bool update = g->addonKind == QLatin1String("update");
        QString info = QStringLiteral("<span style='color:%1; font-weight:600;'>%2</span><br>")
                           .arg(Theme::Color::lavender.name(), (update ? tr("Mise à jour") : tr("Contenu téléchargeable")).toHtmlEscaped());
        info += addonBase ? tr("Pour %1.").arg(addonBase->displayTitle().toHtmlEscaped())
                          : tr("Le jeu de base n'est pas dans ta bibliothèque.");
        const QString emu = EmulatorManager::instance().effectiveDefault(sysId);
        const EmulatorDef* e = Registry::instance().emulator(emu);
        if (e && (emu == QLatin1String("eden") || emu == QLatin1String("citron") || emu == QLatin1String("ryujinx") ||
                  emu == QLatin1String("lichii")))
            info += QStringLiteral("<br><span style='color:%1;'>%2</span>")
                        .arg(Theme::Color::muted.name(),
                             tr("Pour l'utiliser : dans %1, menu Fichier puis « Installer des fichiers dans la NAND », et choisis ce fichier.")
                                 .arg(e->name)
                                 .toHtmlEscaped());
        m_addonInfo->setText(info);
        m_addonInfo->show();
        if (addonBase) {
            m_baseBtn->setText(QFontMetrics(Theme::font(15)).elidedText(tr("Voir %1").arg(addonBase->displayTitle()), Qt::ElideRight, 270));
            m_baseBtn->setProperty("path", addonBase->path);
            m_baseBtn->show();
        }
    } else if (sysId == QLatin1String("pc")) {
        m_addonInfo->setText(QStringLiteral("<span style='color:%1;'>%2</span>")
                                 .arg(Theme::Color::muted.name(),
                                      (g->internalTitle.isEmpty() ? tr("Jeu PC : il se lance directement, sans émulateur.")
                                                                  : tr("Jeu PC (%1) : il se lance directement, sans émulateur.").arg(g->internalTitle))
                                          .toHtmlEscaped()));
        m_addonInfo->show();
    } else if (!g->contentInfo.isEmpty()) {
        m_addonInfo->setText(QStringLiteral("<span style='color:%1;'>%2</span>")
                                 .arg(Theme::Color::lavender.name(), tr("Dans ce fichier : %1.").arg(g->contentInfo).toHtmlEscaped()));
        m_addonInfo->show();
    }
    const auto addons = g->isAddon ? QVector<const Game*>() : m_lib->addonsFor(*g);
    m_addonsTitle->setVisible(!addons.isEmpty());
    m_addonsBox->setVisible(!addons.isEmpty());
    for (const Game* a : addons) {
        const bool update = a->addonKind == QLatin1String("update");
        auto* row = new QPushButton(m_addonsBox);
        row->setCursor(Qt::PointingHandCursor);
        const QString label = Library::addonLabel(*a, g);
        row->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        row->setMinimumWidth(1);
        row->setText(QFontMetrics(Theme::font(14)).elidedText(label, Qt::ElideRight, 260));
        row->setToolTip(label + QLatin1Char('\n') + QDir::toNativeSeparators(a->path));
        row->setStyleSheet(QStringLiteral("QPushButton { text-align: left; padding: 8px 12px; border: 1px solid %1; border-left: 3px solid %2;"
                                          " border-radius: 6px; background: %3; color: %4; font-size: 14px; }"
                                          "QPushButton:hover { background: %5; }")
                               .arg(Theme::Color::line.name(), (update ? Theme::Color::teal : Theme::Color::lavender).name(),
                                    Theme::Color::bg.name(), Theme::Color::text.name(), Theme::Color::raised.name()));
        const QString p = a->path;
        connect(row, &QPushButton::clicked, this, [this, p] { emit showGameRequested(p); });
        ab->addWidget(row);
    }

    m_fav->setIcon(Theme::icon(g->favorite ? QStringLiteral("star-filled") : QStringLiteral("star"),
                               g->favorite ? Theme::Color::pink : Theme::Color::muted, 20));
    m_fav->setText(g->favorite ? tr("Favori") : tr("Favori"));
    m_fav->setToolTip(g->favorite ? tr("Retirer des favoris (Y)") : tr("Ajouter aux favoris (Y)"));

    // Émulateurs compatibles.
    auto& em = EmulatorManager::instance();
    m_emu->clear();
    const QString effective = em.emulatorFor(*g);
    const EmulatorDef* defEmu = Registry::instance().emulator(em.effectiveDefault(sysId));
    const EmulatorDef* autoEmu = Registry::instance().emulator(g->emulatorOverride.isEmpty() ? effective : em.effectiveDefault(sysId));
    Q_UNUSED(defEmu)
    m_emu->addItem(tr("Automatique (%1)").arg(autoEmu ? autoEmu->name : tr("aucun")), QString());
    for (const EmulatorDef* e : Registry::instance().emulatorsFor(sysId)) {
        if (!e->canOpen(g->path)) { // ex. Eden ne lit pas les .nup
            m_emu->addItem(tr("%1 (ne lit pas les .%2)").arg(e->name, QFileInfo(g->path).suffix().toLower()), e->id);
            if (auto* model = qobject_cast<QStandardItemModel*>(m_emu->model()))
                model->item(m_emu->count() - 1)->setEnabled(false);
            continue;
        }
        m_emu->addItem(em.isInstalled(e->id) ? e->name : tr("%1 (non installé)").arg(e->name), e->id);
    }
    const int sel = g->emulatorOverride.isEmpty() ? 0 : m_emu->findData(g->emulatorOverride);
    m_emu->setCurrentIndex(qMax(0, sel));

    const bool noEmu = g->isAddon || sysId == QLatin1String("pc");
    m_emu->setVisible(!noEmu);
    m_emuLabel->setVisible(!noEmu);
    const EmulatorDef* eff = Registry::instance().emulator(effective);
    const EmulatorStatus st = em.status(effective);
    const QStringList missing = em.missingRequired(effective);
    m_install->hide();
    m_emuHint->hide();
    if (!eff) {
        m_emuHint->setText(tr("Aucun émulateur ne prend en charge ce système."));
        m_emuHint->show();
    } else if (!st.installed()) {
        m_emuHint->setText(eff->canAutoInstall()
                               ? tr("%1 n'est pas encore installé. Playnest peut le télécharger depuis sa source officielle.").arg(eff->name)
                               : tr("%1 n'est pas installé. Télécharge-le sur son site, puis indique-le dans la page Émulateurs.").arg(eff->name));
        m_emuHint->show();
        m_install->setVisible(eff->canAutoInstall());
        m_install->setText(tr("Installer %1").arg(eff->name));
        m_install->setProperty("emu", eff->id);
    } else if (!missing.isEmpty()) {
        m_emuHint->setText(tr("Il manque à %1 : %2. Ajoute-les depuis la page Émulateurs.").arg(eff->name, missing.join(QStringLiteral(", "))));
        m_emuHint->show();
    } else if (st.updateAvailable) {
        m_emuHint->setText(tr("Une mise à jour de %1 est disponible (%2).").arg(eff->name, st.latest));
        m_emuHint->show();
    }

    if (noEmu) {
        m_emuHint->hide();
        m_install->hide();
    }

    QString facts = QStringLiteral("<table>");
    facts += factRow(tr("Temps de jeu"), formatPlay(g->playSeconds));
    if (g->lastPlayed.isValid())
        facts += factRow(tr("Dernière partie"), Lang::locale().toString(g->lastPlayed, QObject::tr("d MMMM yyyy, HH:mm")));
    if (!g->gameId.isEmpty())
        facts += factRow(tr("Identifiant"), g->gameId);
    if (!g->titleId.isEmpty())
        facts += factRow(tr("Title ID"), g->titleId);
    if (g->fileSize > 4096)
        facts += factRow(tr("Taille"), formatSize(g->fileSize));
    const QFileInfo fi(g->path);
    // Jeux "dossier" (ExeFS extrait, Wii U décrypté) : on affiche le dossier du jeu.
    const bool folderTitle = fi.fileName() == QLatin1String("main") || fi.dir().dirName() == QLatin1String("code");
    facts += factRow(folderTitle ? tr("Dossier") : tr("Fichier"),
                     folderTitle ? QDir(fi.absolutePath() + QStringLiteral("/..")).absolutePath().section(QLatin1Char('/'), -1)
                                 : fi.fileName());
    facts += QStringLiteral("</table>");
    m_facts->setText(facts);
}
