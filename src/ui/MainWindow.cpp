#include "MainWindow.h"

#include "../core/CoverManager.h"
#include "../core/AppInfo.h"
#include "../core/Emulators.h"
#include "../core/Lang.h"
#include "../core/Launcher.h"
#include "../core/Organizer.h"
#include "../core/Library.h"
#include "../core/Paths.h"
#include "../core/Registry.h"
#include "../core/Scanner.h"
#include "../core/Saves.h"
#include "../core/SelfUpdate.h"
#include "../core/RomDetector.h"
#include "../core/Settings.h"
#include "../core/Tools.h"
#include "DetailsPanel.h"
#include "GameDelegate.h"
#include "HeroBand.h"
#include "HomePage.h"
#include "OrganizeDialog.h"
#include "EmulatorsPage.h"
#include "SavesPage.h"
#include "SettingsPage.h"
#include "Sidebar.h"
#include "Theme.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRadialGradient>
#include <QScrollArea>
#include <QScrollBar>
#include <QVariantAnimation>
#include <QSet>
#include <QShortcut>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

// ===========================================================================
// Toast : message bref en bas de la grille (lancement, erreurs légères).
// ===========================================================================
class Toast : public QLabel {
public:
    explicit Toast(QWidget* parent) : QLabel(parent) {
        setAlignment(Qt::AlignCenter);
        setFont(Theme::font(16, QFont::DemiBold));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        m_fx = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(m_fx);
        m_anim = new QPropertyAnimation(m_fx, "opacity", this);
        m_hide = new QTimer(this);
        m_hide->setSingleShot(true);
        connect(m_hide, &QTimer::timeout, this, [this] {
            m_anim->stop();
            m_anim->setDuration(400);
            m_anim->setStartValue(1.0);
            m_anim->setEndValue(0.0);
            m_anim->start();
        });
        connect(m_anim, &QPropertyAnimation::finished, this, [this] {
            if (m_fx->opacity() < 0.01)
                hide();
        });
        hide();
    }
    void showMessage(const QString& text, const QColor& accent, int ms = 2600) {
        setText(text);
        setStyleSheet(QStringLiteral("background: %1; color: %2; border-left: 5px solid %3; border-radius: 8px; padding: 14px 24px;")
                          .arg(Theme::Color::raised.name(), Theme::Color::text.name(), accent.name()));
        adjustSize();
        reposition();
        m_anim->stop();
        m_fx->setOpacity(1.0);
        show();
        raise();
        m_hide->start(ms);
    }
    void reposition() {
        if (!parentWidget())
            return;
        const QSize s = sizeHint().boundedTo(parentWidget()->size() - QSize(40, 40));
        resize(s);
        move((parentWidget()->width() - s.width()) / 2, parentWidget()->height() - s.height() - 28);
    }

private:
    QGraphicsOpacityEffect* m_fx;
    QPropertyAnimation* m_anim;
    QTimer* m_hide;
};

namespace {
QString countText(int n) { return n <= 1 ? QObject::tr("%1 jeu").arg(n) : QObject::tr("%1 jeux").arg(n); }

int visibleCount(const Library* lib) {
    return lib->countForSystem(QStringLiteral("__all"), !Settings::instance().hideAddons());
}

QString formatClock(qint64 s) {
    return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s % 3600) / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QPixmap wordmark() {
    const int h = 36, w = 156;
    QPixmap pm(w * 2, h * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    Theme::paintLogo(p, QRectF(2, 2, h - 4, h - 4));
    p.setPen(Theme::Color::text);
    p.setFont(Theme::font(27, QFont::Bold, true));
    p.drawText(QRectF(h + 8, 0, w - h - 8, h), Qt::AlignLeft | Qt::AlignVCenter, QString::fromLatin1(AppInfo::kName));
    return pm;
}
} // namespace

// Fond de la bibliothèque : une lueur à la couleur de la catégorie ouverte (or pour l'accueil,
// violet pour la GameCube…). Elle glisse d'une couleur à l'autre quand on change de console.
class AmbientWidget : public QWidget {
public:
    explicit AmbientWidget(QWidget* parent) : QWidget(parent), m_color(Theme::Color::gold) {
        m_anim = new QVariantAnimation(this);
        m_anim->setDuration(380);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_color = v.value<QColor>();
            update();
        });
    }
    void setAccent(const QColor& c) {
        if (c == m_anim->endValue().value<QColor>() && m_anim->state() == QAbstractAnimation::Running)
            return;
        m_anim->stop();
        m_anim->setStartValue(m_color);
        m_anim->setEndValue(c);
        m_anim->start();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        QRadialGradient g(QPointF(width() * 0.16, -60), qMax(width(), 700) * 0.72);
        QColor c = m_color;
        c.setAlpha(58);
        g.setColorAt(0, c);
        c.setAlpha(18);
        g.setColorAt(0.55, c);
        c.setAlpha(0);
        g.setColorAt(1, c);
        p.fillRect(rect(), g);
    }

private:
    QColor m_color;
    QVariantAnimation* m_anim;
};

// ===========================================================================
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Playnest"));
    m_lib = new Library(this);
    m_model = new LibraryModel(m_lib, this);
    m_filter = new LibraryFilter(this);
    m_filter->setSourceModel(m_model);
    m_filter->setHideAddons(Settings::instance().hideAddons());
    m_filter->setSortKey(Settings::instance().sortMode());
    m_scanner = new Scanner(this);
    m_launcher = new Launcher(this);
    m_covers = new CoverManager(m_lib, this);
    m_pad = new Gamepad(this);
    m_org = new Organizer(m_lib, this);
    setAcceptDrops(true);
    SaveManager::instance().setLibrary(m_lib);

    auto* central = new QWidget(this);
    auto* h = new QHBoxLayout(central);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(buildSidebar());

    auto* mainCol = new QWidget(central);
    auto* mv = new QVBoxLayout(mainCol);
    mv->setContentsMargins(0, 0, 0, 0);
    mv->setSpacing(0);
    mv->addWidget(buildBanner());
    m_pages = new QStackedWidget(mainCol);
    m_pages->addWidget(buildLibraryPage());
    m_emuPage = new EmulatorsPage(m_pages);
    m_savesPage = new SavesPage(m_pages);
    m_settingsPage = new SettingsPage(m_pages);
    m_pages->addWidget(m_emuPage);
    m_pages->addWidget(m_savesPage);
    m_pages->addWidget(m_settingsPage);
    mv->addWidget(m_pages, 1);
    mv->addWidget(buildStatusBar());
    h->addWidget(mainCol, 1);
    setCentralWidget(central);

    m_toast = new Toast(m_pages);
    m_pages->installEventFilter(this);

    // --- Connexions -------------------------------------------------------
    connect(&ImageCache::instance(), &ImageCache::loaded, m_view->viewport(), qOverload<>(&QWidget::update));
    connect(m_sidebar, &Sidebar::categoryChanged, this, [this](const QString& id) {
        setCategory(id);
        showPage(LibraryPage);
    });
    connect(m_sidebar, &QListWidget::itemClicked, this, [this] { showPage(LibraryPage); });
    connect(m_view, &QListView::doubleClicked, this, [this](const QModelIndex& idx) {
        launchGame(idx.data(LibraryModel::PathRole).toString());
    });
    connect(m_view, &QListView::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex idx = m_view->indexAt(pos);
        if (idx.isValid())
            showGameMenu(idx.data(LibraryModel::PathRole).toString(), m_view->viewport()->mapToGlobal(pos));
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& cur) {
        m_details->showGame(cur.isValid() ? cur.data(LibraryModel::PathRole).toString() : QString());
    });
    connect(m_filter, &QAbstractItemModel::modelReset, this, [this] { updateEmptyState(); updateHeader(); });
    connect(m_filter, &QAbstractItemModel::layoutChanged, this, [this] { updateEmptyState(); updateHeader(); });
    connect(m_filter, &QAbstractItemModel::rowsInserted, this, [this] { updateEmptyState(); updateHeader(); });
    connect(m_filter, &QAbstractItemModel::rowsRemoved, this, [this] { updateEmptyState(); updateHeader(); });

    connect(m_lib, &Library::reset, this, [this] {
        m_sidebar->refreshCounts();
        if (m_home)
            m_home->refresh();
        m_details->refresh();
        updateHero();
    });
    connect(m_lib, &Library::gameChanged, this, [this](const QString& path) {
        if (path == m_details->currentPath())
            m_details->refresh();
        m_sidebar->refreshCounts();
        if (m_home)
            m_home->refresh();
        if (path == m_hero->path() || m_hero->isHidden())
            updateHero();
        else
            m_hero->update();
    });
    connect(m_hero, &HeroBand::playRequested, this, [this](const QString& p) { launchGame(p); });
    connect(m_hero, &HeroBand::selectRequested, this, [this](const QString& p) { selectPath(p); });
    connect(m_details, &DetailsPanel::showGameRequested, this, &MainWindow::goToGame);
    connect(m_org, &Organizer::progress, this, [this](const QString& name, qint64 done, qint64 total) {
        if (!m_importing)
            return;
        m_statusText->setText(tr("Ajout : %1").arg(name));
        m_statusProgress->setValue(int(qMin<qint64>(1000, done * 1000 / qMax<qint64>(1, total))));
    });
    connect(m_org, &Organizer::finished, this, [this](const Organizer::Result& r) {
        if (!m_importing)
            return;
        m_importing = false;
        m_statusProgress->hide();
        notify(r.message, !r.ok);
        m_selectAfterScan = r.newPaths.value(0);
        startScan();
    });

    connect(m_details, &DetailsPanel::playRequested, this, [this](const QString& p) { launchGame(p); });
    connect(m_details, &DetailsPanel::favoriteToggled, this, &MainWindow::toggleFavorite);
    connect(m_details, &DetailsPanel::openFolderRequested, this, [](const QString& p) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(p).absolutePath()));
    });
    connect(m_details, &DetailsPanel::coverMenuRequested, this, &MainWindow::showCoverMenu);
    connect(m_details, &DetailsPanel::emulatorChosen, this, [this](const QString& p, const QString& emu) {
        m_lib->update(p, [&](Game& g) { g.emulatorOverride = emu; });
    });
    connect(m_details, &DetailsPanel::installRequested, this, [this](const QString& emu) { installEmulator(emu); });

    connect(m_scanner, &Scanner::progress, this, [this](int seen, int found, const QString&) {
        m_statusText->setText(tr("Recherche des jeux… %1 fichiers examinés, %2 trouvés").arg(seen).arg(found));
    });
    connect(m_scanner, &Scanner::finished, this, [this](const QVector<DetectedRom>& games, const QStringList& folders, bool cancelled) {
        m_statusProgress->hide();
        m_cancelScan->hide();
        if (cancelled) {
            m_statusText->setText(tr("Recherche annulée."));
            return;
        }
        const QString keep = currentPath();
        QVector<DetectedRom> all = games;
        for (const QString& exe : Settings::instance().value(QStringLiteral("pc/games")).toStringList())
            if (auto r = RomDetector::detectPcGame(exe, false))
                all << *r;
        const auto res = m_lib->mergeScan(all, folders);
        qInfo("Recherche des jeux : %d trouvé(s) dans %d dossier(s), %d nouveau(x), %d retiré(s)", int(games.size()),
              int(folders.size()), res.added, res.removed);
        selectPath(keep);
        if (!m_selectAfterScan.isEmpty()) { // jeu tout juste ajouté : on le montre
            goToGame(m_selectAfterScan);
            m_selectAfterScan.clear();
        }
        QString msg = tr("%1 dans la bibliothèque").arg(countText(visibleCount(m_lib)));
        if (res.added)
            msg += tr(", %1 nouveau(x)").arg(res.added);
        if (res.removed)
            msg += tr(", %1 retiré(s)").arg(res.removed);
        m_statusText->setText(msg + QLatin1Char('.'));
        m_covers->requestMissing();
        updateEmptyState();
        if (!m_view->currentIndex().isValid() && m_filter->rowCount() > 0)
            m_view->setCurrentIndex(m_filter->index(0, 0));
    });
    connect(m_covers, &CoverManager::pendingChanged, this, [this](int n) {
        if (m_scanner->isRunning())
            return;
        if (n > 0)
            m_statusText->setText(tr("Téléchargement des jaquettes, %1 restante(s)…").arg(n));
        else
            m_statusText->setText(tr("%1 dans la bibliothèque.").arg(countText(visibleCount(m_lib))));
    });
    connect(m_covers, &CoverManager::coverFound, this, [this](const QString& path) {
        if (const Game* g = m_lib->find(path))
            ImageCache::instance().invalidate(g->coverPath);
        m_view->viewport()->update();
    });

    connect(m_launcher, &Launcher::started, this, [this](const QString& path) {
        m_delegate->setRunningPath(path);
        m_details->setRunning(path);
        m_hero->setRunning(true);
        m_emuPage->refresh();
        m_playTimer->start();
        updateGamepadState();
        m_view->viewport()->update();
    });
    connect(m_launcher, &Launcher::finished, this,
            [this](const QString& path, int code, qint64 secs, bool crashed, const QString& tail) {
                m_playTimer->stop();
                m_playing->clear();
                m_delegate->setRunningPath(QString());
                m_details->setRunning(QString());
                m_hero->setRunning(false);
                m_lib->update(path, [&](Game& g) { g.playSeconds += secs; });
                m_lib->save();
                if (isMinimized())
                    showNormal();
                raise();
                activateWindow();
                updateGamepadState();
                m_view->viewport()->update();
                if (crashed) {
                    QMessageBox box(this);
                    box.setIcon(QMessageBox::Warning);
                    box.setWindowTitle(tr("L'émulateur s'est arrêté"));
                    box.setText(tr("L'émulateur s'est fermé après %1 seconde(s) (code %2).").arg(secs).arg(code));
                    box.setInformativeText(tr("Causes fréquentes : clés ou firmware manquants (Switch, Switch 2, 3DS chiffré, "
                                              "Wii U chiffré), format de fichier non pris en charge, ou pilote graphique "
                                              "trop ancien. Lance l'émulateur seul pour voir son message d'erreur."));
                    if (!tail.isEmpty())
                        box.setDetailedText(tail);
                    box.exec();
                } else if (secs >= 60) {
                    m_toast->showMessage(tr("Session terminée : %1").arg(formatClock(secs)), Theme::Color::teal);
                }
            });

    auto& em = EmulatorManager::instance();
    connect(&em, &EmulatorManager::installProgress, this, [this](const QString&, const QString& stage, qint64 rec, qint64 total) {
        m_statusProgress->show();
        m_statusProgress->setRange(0, total > 0 ? 1000 : 0);
        if (total > 0)
            m_statusProgress->setValue(int(rec * 1000 / total));
        m_statusText->setText(stage);
    });
    connect(&em, &EmulatorManager::installFinished, this, [this](const QString& id, bool ok, const QString& msg) {
        m_statusProgress->hide();
        m_statusText->setText(msg);
        m_details->refresh();
        if (!ok) {
            m_pendingLaunch.clear();
            if (m_pages->currentIndex() == EmulatorsPageIdx)
                return; // la page Émulateurs affiche déjà le message
            const EmulatorDef* e = Registry::instance().emulator(id);
            QMessageBox box(QMessageBox::Warning, tr("Installation impossible"), msg, QMessageBox::Ok, this);
            QPushButton* site = nullptr;
            if (e && !e->website.isEmpty())
                site = box.addButton(tr("Ouvrir la page de téléchargement"), QMessageBox::ActionRole);
            box.exec();
            if (site && box.clickedButton() == site)
                QDesktopServices::openUrl(QUrl(e->website));
            return;
        }
        notify(msg);
        if (!m_pendingLaunch.isEmpty() && m_pendingLaunchEmu == id) {
            const QString p = m_pendingLaunch, emu = m_pendingLaunchEmu;
            m_pendingLaunch.clear();
            QTimer::singleShot(400, this, [this, p, emu] { launchGame(p, emu); });
        }
    });
    connect(&em, &EmulatorManager::changed, this, [this] {
        updateNavBadges();
        m_details->refresh();
    });
    connect(&em, &EmulatorManager::detectionFinished, this, [this](int) {
        updateNavBadges();
        if (!Settings::instance().firstRunDone())
            QTimer::singleShot(300, this, &MainWindow::showFirstRunSummary);
        // Vérification des mises à jour un peu plus tard : la bibliothèque s'affiche d'abord.
        if (Settings::instance().value(QStringLiteral("updates/autoCheck"), true).toBool())
            QTimer::singleShot(6000, this, [] { EmulatorManager::instance().checkUpdates(false); });
        SaveManager::instance().maybeWeeklyBackup();
    });
    connect(&em, &EmulatorManager::updatesChecked, this, [this](int n) {
        updateNavBadges();
        if (n > 0 && m_bannerKind != 1) {
            m_bannerKind = 2;
            m_bannerText->setText(n == 1 ? tr("Une mise à jour d'émulateur est disponible.") : tr("%1 mises à jour d'émulateurs sont disponibles.").arg(n));
            m_bannerAction->setText(tr("Voir les émulateurs"));
            m_banner->show();
        }
    });
    connect(&SelfUpdate::instance(), &SelfUpdate::checked, this, [this](bool available, const QString& version, const QString&) {
        if (!available)
            return;
        m_bannerKind = 1;
        m_bannerText->setText(tr("%1 %2 est disponible.").arg(QString::fromLatin1(AppInfo::kName), version));
        m_bannerAction->setText(tr("Mettre à jour et redémarrer"));
        m_banner->show();
    });
    connect(&SelfUpdate::instance(), &SelfUpdate::progress, this, [this](const QString& stage, qint64 r, qint64 t) {
        m_statusProgress->show();
        m_statusProgress->setRange(0, t > 0 ? 1000 : 0);
        if (t > 0)
            m_statusProgress->setValue(int(r * 1000 / t));
        m_statusText->setText(stage);
    });
    connect(&SelfUpdate::instance(), &SelfUpdate::failed, this, [this](const QString& msg) {
        m_statusProgress->hide();
        notify(msg, true);
    });
    connect(&Tools::instance(), &Tools::installFinished, this, [this](const QString&, bool ok, const QString& msg) {
        if (m_pages->currentIndex() != EmulatorsPageIdx)
            notify(msg, !ok);
    });
    connect(m_emuPage, &EmulatorsPage::message, this, [this](const QString& t, bool err) { notify(t, err); });
    connect(m_savesPage, &SavesPage::message, this, [this](const QString& t, bool err) { notify(t, err); });
    connect(m_settingsPage, &SettingsPage::message, this, [this](const QString& t, bool err) { notify(t, err); });
    connect(m_settingsPage, &SettingsPage::foldersChanged, this, [this] { startScan(); });
    connect(m_settingsPage, &SettingsPage::languageChosen, this, &MainWindow::changeLanguage);
    connect(m_settingsPage, &SettingsPage::appearanceChanged, this, [this] {
        m_filter->setHideAddons(Settings::instance().hideAddons());
        m_sidebar->refreshCounts();
        if (m_home)
            m_home->refresh();
        updateGamepadState();
    });

    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(1000);
    connect(m_playTimer, &QTimer::timeout, this, [this] {
        m_playing->setText(tr("En cours : %1 avec %2, %3")
                               .arg(m_launcher->runningTitle(), m_launcher->runningEmulator(), formatClock(m_launcher->elapsedSeconds())));
    });

    connect(m_pad, &Gamepad::action, this, &MainWindow::onGamepad);
    connect(m_pad, &Gamepad::connectionChanged, this, [this](bool on) {
        m_padIcon->setVisible(on);
        if (on)
            m_toast->showMessage(tr("Manette détectée"), Theme::Color::teal, 1600);
    });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this] { updateGamepadState(); });

    // --- Raccourcis --------------------------------------------------------
    new QShortcut(QKeySequence::Find, this, [this] { m_search->setFocus(); m_search->selectAll(); });
    new QShortcut(QKeySequence(Qt::Key_F5), this, [this] { startScan(); });
    new QShortcut(QKeySequence(Qt::Key_F11), this, [this] { toggleFullscreen(); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma), this, [this] { showPage(SettingsPageIdx); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this, [this] { toggleFavorite(currentPath()); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageDown), this, [this] { m_sidebar->step(+1); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageUp), this, [this] { m_sidebar->step(-1); });

    resize(1440, 900);
    const QByteArray geo = Settings::instance().windowGeometry();
    if (!geo.isEmpty())
        restoreGeometry(geo);
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
QWidget* MainWindow::buildSidebar() {
    auto* w = new QWidget(this);
    w->setObjectName(QStringLiteral("sidebar"));
    w->setAttribute(Qt::WA_StyledBackground, true);
    w->setStyleSheet(QStringLiteral("#sidebar { background: %1; border-right: 1px solid %2; }")
                         .arg(Theme::Color::panel.name(), Theme::Color::line.name()));
    w->setFixedWidth(248);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 18, 0, 14);
    v->setSpacing(6);
    auto* top = new QHBoxLayout;
    top->setContentsMargins(20, 0, 12, 8);
    auto* logo = new QLabel(w);
    logo->setPixmap(wordmark());
    top->addWidget(logo);
    top->addStretch();
    // Langue : toujours sous la main, le changement s'applique tout de suite.
    m_langBtn = new QToolButton(w);
    m_langBtn->setIcon(Theme::icon(QStringLiteral("globe"), Theme::Color::muted, 15));
    m_langBtn->setText(Lang::current().toUpper());
    m_langBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_langBtn->setPopupMode(QToolButton::InstantPopup);
    m_langBtn->setCursor(Qt::PointingHandCursor);
    m_langBtn->setToolTip(tr("Langue"));
    m_langBtn->setStyleSheet(QStringLiteral("QToolButton { padding: 4px 8px; color: %1; font-size: 13px; font-weight: 600; border: 1px solid %2;"
                                            " border-radius: 13px; background: transparent; }"
                                            "QToolButton:hover { color: %3; border-color: %1; }"
                                            "QToolButton::menu-indicator { image: none; width: 0; }")
                                 .arg(Theme::Color::muted.name(), Theme::Color::line.name(), Theme::Color::text.name()));
    auto* langMenu = new QMenu(m_langBtn);
    for (const auto& [code, label] : {std::pair{QStringLiteral("fr"), QStringLiteral("Français")}, std::pair{QStringLiteral("en"), QStringLiteral("English")}}) {
        QAction* a = langMenu->addAction(label);
        a->setCheckable(true);
        a->setChecked(Lang::current() == code);
        connect(a, &QAction::triggered, this, [this, code = code] { changeLanguage(code); });
    }
    m_langBtn->setMenu(langMenu);
    top->addWidget(m_langBtn);
    v->addLayout(top);
    m_sidebar = new Sidebar(m_lib, w);
    v->addWidget(m_sidebar, 1);

    auto mkNav = [w](const QString& icon, const QString& text) {
        auto* b = new QToolButton(w);
        b->setIcon(Theme::icon(icon, Theme::Color::muted, 18));
        b->setText(text);
        b->setCheckable(true);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setCursor(Qt::PointingHandCursor);
        b->setIconSize(QSize(17, 17));
        b->setStyleSheet(QStringLiteral("QToolButton { text-align: left; padding: 8px 6px; color: %1; font-size: 13px; font-weight: 500;"
                                        " border: 1px solid transparent; border-radius: 6px; }"
                                        "QToolButton:hover { color: %2; background: %3; }"
                                        "QToolButton:checked { color: %2; background: %3; border-color: %4; }")
                             .arg(Theme::Color::muted.name(), Theme::Color::text.name(), Theme::Color::raised.name(), Theme::Color::line.name()));
        return b;
    };
    m_navEmus = mkNav(QStringLiteral("gamepad"), tr("Émulateurs"));
    m_navSaves = mkNav(QStringLiteral("download"), tr("Sauvegardes"));
    m_navTidy = mkNav(QStringLiteral("tidy"), tr("Ranger"));
    m_navTidy->setToolTip(tr("Ranger mes fichiers"));
    m_navTidy->setCheckable(false);
    m_navSettings = mkNav(QStringLiteral("settings"), tr("Paramètres"));
    // Quatre raccourcis sur deux lignes : la place gagnée sert à montrer toutes les consoles.
    auto* line = new QFrame(w);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: %1;").arg(Theme::Color::line.name()));
    v->addWidget(line);
    auto* bl = new QGridLayout;
    bl->setContentsMargins(10, 6, 10, 0);
    bl->setSpacing(3);
    bl->addWidget(m_navEmus, 0, 0);
    bl->addWidget(m_navSaves, 0, 1);
    bl->addWidget(m_navTidy, 1, 0);
    bl->addWidget(m_navSettings, 1, 1);
    v->addLayout(bl);
    connect(m_navEmus, &QToolButton::clicked, this, [this] { showPage(m_pages->currentIndex() == EmulatorsPageIdx ? LibraryPage : EmulatorsPageIdx); });
    connect(m_navSaves, &QToolButton::clicked, this, [this] { showPage(m_pages->currentIndex() == SavesPageIdx ? LibraryPage : SavesPageIdx); });
    connect(m_navSettings, &QToolButton::clicked, this, [this] { showPage(m_pages->currentIndex() == SettingsPageIdx ? LibraryPage : SettingsPageIdx); });
    connect(m_navTidy, &QToolButton::clicked, this, &MainWindow::openOrganizer);
    return w;
}

QWidget* MainWindow::buildLibraryPage() {
    auto* page = new QWidget(this);
    auto* h = new QHBoxLayout(page);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    m_ambient = new AmbientWidget(page);
    QWidget* col = m_ambient;
    auto* mv = new QVBoxLayout(col);
    mv->setContentsMargins(0, 0, 0, 0);
    mv->setSpacing(0);
    m_hero = new HeroBand(m_lib, col);
    m_hero->hide();
    mv->addWidget(m_hero);
    mv->addWidget(buildHeader());

    m_stack = new QStackedWidget(col);
    m_view = new QListView(m_stack);
    m_delegate = new GameDelegate(m_view);
    m_delegate->setCardWidth(Settings::instance().cardSize());
    m_view->setItemDelegate(m_delegate);
    m_view->setModel(m_filter);
    m_view->setViewMode(QListView::IconMode);
    m_view->setMovement(QListView::Static);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setUniformItemSizes(true);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setGridSize(m_delegate->cardSize());
    m_view->setSpacing(0);
    m_view->setMouseTracking(true);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->verticalScrollBar()->setSingleStep(40);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setStyleSheet(QStringLiteral("QListView { background: transparent; padding: 8px 18px; }"));
    m_view->viewport()->setAttribute(Qt::WA_Hover);
    m_view->installEventFilter(this);
    m_view->viewport()->installEventFilter(this);
    m_stack->addWidget(m_view);
    m_stack->addWidget(buildEmptyState());
    m_noResults = new QLabel(m_stack);
    m_noResults->setAlignment(Qt::AlignCenter);
    m_noResults->setWordWrap(true);
    m_noResults->setProperty("role", "muted");
    m_noResults->setFont(Theme::font(17));
    m_stack->addWidget(m_noResults);
    // Accueil : le mur des consoles.
    m_home = new HomePage(m_lib, m_stack);
    auto* homeScroll = new QScrollArea(m_stack);
    homeScroll->setWidget(m_home);
    homeScroll->setWidgetResizable(true);
    homeScroll->setFrameShape(QFrame::NoFrame);
    homeScroll->setFocusPolicy(Qt::NoFocus);
    homeScroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; } QScrollArea > QWidget > QWidget { background: transparent; }"));
    m_stack->addWidget(homeScroll);
    connect(m_home, &HomePage::consoleChosen, this, [this](const QString& id) { m_sidebar->selectCategory(id); });
    connect(m_home, &HomePage::addGamesRequested, this, &MainWindow::chooseGamesToImport);
    mv->addWidget(m_stack, 1);
    h->addWidget(col, 1);

    m_details = new DetailsPanel(m_lib, page);
    m_details->setVisible(Settings::instance().detailsVisible());
    h->addWidget(m_details);
    return page;
}

QWidget* MainWindow::buildBanner() {
    m_banner = new QWidget(this);
    m_banner->setObjectName(QStringLiteral("banner"));
    m_banner->setAttribute(Qt::WA_StyledBackground, true);
    m_banner->setStyleSheet(QStringLiteral("#banner { background: %1; border-bottom: 1px solid %2; }")
                                .arg(QColor(0x3A, 0x32, 0x1C).name(), Theme::Color::gold.darker(160).name()));
    auto* h = new QHBoxLayout(m_banner);
    h->setContentsMargins(30, 8, 18, 8);
    m_bannerText = new QLabel(m_banner);
    m_bannerText->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(Theme::Color::gold.name()));
    h->addWidget(m_bannerText);
    h->addStretch();
    m_bannerAction = new QPushButton(m_banner);
    m_bannerAction->setStyleSheet(Theme::primaryButtonStyle(Theme::Color::gold, true));
    auto* later = new QPushButton(tr("Plus tard"), m_banner);
    h->addWidget(m_bannerAction);
    h->addWidget(later);
    connect(later, &QPushButton::clicked, m_banner, &QWidget::hide);
    connect(m_bannerAction, &QPushButton::clicked, this, [this] {
        if (m_bannerKind == 1) {
            if (m_launcher->isRunning()) {
                notify(tr("Ferme le jeu en cours avant de mettre à jour Playnest."), true);
                return;
            }
            SelfUpdate::instance().apply();
        } else {
            showPage(EmulatorsPageIdx);
        }
        m_banner->hide();
    });
    m_banner->hide();
    return m_banner;
}

QWidget* MainWindow::buildHeader() {
    auto* w = new QWidget(this);
    auto* outer = new QVBoxLayout(w);
    outer->setContentsMargins(30, 20, 22, 8);
    outer->setSpacing(4);
    auto* row1 = new QHBoxLayout;
    auto* row2 = new QHBoxLayout;
    row2->setSpacing(10);
    outer->addLayout(row1);
    outer->addLayout(row2);
    QHBoxLayout* h = row2;

    m_title = new QLabel(w);
    m_title->setFont(Theme::font(40, QFont::Bold, true));
    m_title->setTextFormat(Qt::RichText);
    row1->addWidget(m_title);
    row1->addStretch(1);
    m_subtitle = new QLabel(w);
    m_subtitle->setProperty("role", "muted");
    m_subtitle->setFont(Theme::font(15, QFont::Medium));
    row2->addWidget(m_subtitle);
    row2->addStretch(1);

    m_search = new QLineEdit(w);
    m_search->setPlaceholderText(tr("Rechercher un jeu"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(Theme::icon(QStringLiteral("search"), Theme::Color::faint, 18), QLineEdit::LeadingPosition);
    m_search->setFixedWidth(280);
    m_search->setMinimumHeight(36);
    m_search->installEventFilter(this);
    h->addWidget(m_search, 0, Qt::AlignVCenter);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& t) {
        if (m_onHome && !t.trimmed().isEmpty())
            m_sidebar->selectCategory(QStringLiteral("__all")); // une recherche depuis l'accueil porte sur tous les jeux
        m_filter->setSearch(t);
        updateEmptyState();
        updateHeader();
        updateHero();
        if (m_filter->rowCount() > 0 && !m_view->currentIndex().isValid())
            m_view->setCurrentIndex(m_filter->index(0, 0));
    });

    m_sort = new QComboBox(w);
    m_sort->addItem(tr("Titre"), QStringLiteral("title"));
    m_sort->addItem(tr("Joué récemment"), QStringLiteral("recent"));
    m_sort->addItem(tr("Temps de jeu"), QStringLiteral("playtime"));
    m_sort->addItem(tr("Ajouté récemment"), QStringLiteral("added"));
    m_sort->addItem(tr("Console"), QStringLiteral("system"));
    m_sort->setCurrentIndex(qMax(0, m_sort->findData(Settings::instance().sortMode())));
    m_sort->setToolTip(tr("Trier par"));
    m_sort->setMinimumHeight(36);
    h->addWidget(m_sort, 0, Qt::AlignVCenter);
    connect(m_sort, qOverload<int>(&QComboBox::activated), this, [this](int) {
        const QString k = m_sort->currentData().toString();
        Settings::instance().setSortMode(k);
        m_filter->setSortKey(k);
    });

    m_size = new QSlider(Qt::Horizontal, w);
    m_size->setRange(120, 260);
    m_size->setValue(Settings::instance().cardSize());
    m_size->setFixedWidth(100);
    m_size->setToolTip(tr("Taille des jaquettes"));
    h->addWidget(m_size, 0, Qt::AlignVCenter);
    connect(m_size, &QSlider::valueChanged, this, [this](int v) {
        m_delegate->setCardWidth(v);
        relayoutGrid();
        Settings::instance().setCardSize(v);
        m_view->scrollTo(m_view->currentIndex());
    });

    auto mkTool = [w](const QString& icon, const QString& tip) {
        auto* b = new QToolButton(w);
        b->setIcon(Theme::icon(icon, Theme::Color::muted, 20));
        b->setIconSize(QSize(20, 20));
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    auto* add = mkTool(QStringLiteral("plus"), tr("Ajouter des jeux (tu peux aussi les glisser sur la fenêtre)"));
    auto* refresh = mkTool(QStringLiteral("refresh"), tr("Rechercher les nouveaux jeux (F5)"));
    m_detailsBtn = mkTool(QStringLiteral("panel"), tr("Afficher le panneau de détails"));
    m_detailsBtn->setCheckable(true);
    m_detailsBtn->setChecked(Settings::instance().detailsVisible());
    auto* full = mkTool(QStringLiteral("fullscreen"), tr("Plein écran (F11)"));
    row1->addWidget(add, 0, Qt::AlignVCenter);
    row1->addWidget(refresh, 0, Qt::AlignVCenter);
    auto* addMenu = new QMenu(add);
    addMenu->addAction(Theme::icon(QStringLiteral("grid")), tr("Ajouter des jeux de console…"), this, &MainWindow::chooseGamesToImport);
    addMenu->addAction(Theme::icon(QStringLiteral("play")), tr("Ajouter un jeu PC ou un fan game…"), this, [this] {
        const QString exe = QFileDialog::getOpenFileName(this, tr("Exécutable du jeu (souvent Game.exe)"),
                                                         QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                                                         tr("Programmes (*.exe)"));
        if (!exe.isEmpty())
            addPcGames({exe});
    });
    add->setMenu(addMenu);
    add->setPopupMode(QToolButton::InstantPopup);
    row1->addWidget(m_detailsBtn, 0, Qt::AlignVCenter);
    row1->addWidget(full, 0, Qt::AlignVCenter);
    connect(refresh, &QToolButton::clicked, this, &MainWindow::startScan);
    connect(m_detailsBtn, &QToolButton::toggled, this, [this](bool on) {
        m_details->setVisible(on);
        Settings::instance().setDetailsVisible(on);
    });
    connect(full, &QToolButton::clicked, this, &MainWindow::toggleFullscreen);
    return w;
}

QWidget* MainWindow::buildEmptyState() {
    auto* w = new QWidget(this);
    auto* outer = new QVBoxLayout(w);
    outer->addStretch(2);
    auto* box = new QWidget(w);
    box->setMaximumWidth(620);
    auto* v = new QVBoxLayout(box);
    v->setSpacing(14);
    auto* logo = new QLabel(box);
    logo->setPixmap(Theme::logoPixmap(132, 2.0));
    v->addWidget(logo);
    v->addSpacing(8);
    auto* t = new QLabel(tr("Ta bibliothèque est vide"), box);
    t->setFont(Theme::font(34, QFont::Bold, true));
    v->addWidget(t);
    auto* d = new QLabel(tr("Ajoute le dossier où tu ranges tes jeux. Playnest reconnaît tous les jeux Nintendo, de la NES "
                            "à la Switch 2, puis les lance avec le bon émulateur."), box);
    d->setWordWrap(true);
    d->setProperty("role", "muted");
    d->setFont(Theme::font(17));
    v->addWidget(d);
    v->addSpacing(6);
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(Theme::icon(QStringLiteral("plus"), Theme::onColor(Theme::Color::gold), 18),
                                tr("Ajouter un dossier de jeux"), box);
    add->setStyleSheet(Theme::primaryButtonStyle(Theme::Color::gold));
    add->setCursor(Qt::PointingHandCursor);
    auto* emus = new QPushButton(tr("Voir mes émulateurs"), box);
    emus->setMinimumHeight(44);
    row->addWidget(add);
    row->addWidget(emus);
    row->addStretch();
    v->addLayout(row);
    auto* hl = new QHBoxLayout;
    hl->addStretch();
    hl->addWidget(box);
    hl->addStretch();
    outer->addLayout(hl);
    outer->addStretch(3);
    connect(add, &QPushButton::clicked, this, &MainWindow::addFolder);
    connect(emus, &QPushButton::clicked, this, [this] { showPage(EmulatorsPageIdx); });
    return w;
}

QWidget* MainWindow::buildStatusBar() {
    auto* w = new QWidget(this);
    w->setObjectName(QStringLiteral("statusbar"));
    w->setAttribute(Qt::WA_StyledBackground, true);
    w->setStyleSheet(QStringLiteral("#statusbar { border-top: 1px solid %1; }").arg(Theme::Color::line.name()));
    w->setFixedHeight(38);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(30, 0, 18, 0);
    h->setSpacing(10);
    m_statusText = new QLabel(w);
    m_statusText->setProperty("role", "muted");
    h->addWidget(m_statusText);
    m_statusProgress = new QProgressBar(w);
    m_statusProgress->setFixedSize(140, 6);
    m_statusProgress->setRange(0, 0);
    m_statusProgress->hide();
    h->addWidget(m_statusProgress);
    m_cancelScan = new QToolButton(w);
    m_cancelScan->setText(tr("Annuler"));
    m_cancelScan->hide();
    connect(m_cancelScan, &QToolButton::clicked, this, [this] { m_scanner->cancel(); });
    h->addWidget(m_cancelScan);
    h->addStretch();
    m_playing = new QLabel(w);
    m_playing->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(Theme::Color::teal.name()));
    h->addWidget(m_playing);
    m_padIcon = new QLabel(w);
    m_padIcon->setPixmap(Theme::icon(QStringLiteral("gamepad"), Theme::Color::teal, 22).pixmap(22, 22));
    m_padIcon->setToolTip(tr("Manette connectée"));
    m_padIcon->hide();
    h->addWidget(m_padIcon);
    return w;
}

// ---------------------------------------------------------------------------
void MainWindow::startup() {
    m_lib->load();
    m_sidebar->rebuild();
    setCategory(m_sidebar->currentCategory());
    updateEmptyState();
    if (m_filter->rowCount() > 0)
        m_view->setCurrentIndex(m_filter->index(0, 0));
    m_view->setFocus();
    updateGamepadState();
    m_statusText->setText(tr("%1 dans la bibliothèque.").arg(countText(visibleCount(m_lib))));

    SelfUpdate::instance().cleanupOldFiles();
    EmulatorManager::instance().init(); // statut immédiat depuis le cache, détection en arrière-plan
    Tools::instance().checkVulkanAsync();
    SaveManager::instance().reload();
    updateNavBadges();
    m_details->refresh();
    // Aucun dossier de jeux (nouvelle copie de Playnest, réglages perdus) mais le dossier rangé existe : on le reprend.
    if (Settings::instance().romFolders().isEmpty()) {
        const QString organized = QDir::fromNativeSeparators(Organizer::defaultRoot());
        if (Organizer::isOrganized(organized)) {
            Settings::instance().setRomFolders({QDir::toNativeSeparators(organized)});
            Settings::instance().sync();
            qInfo("Dossier de jeux repris : %s", qUtf8Printable(organized));
        }
    }
    qInfo("Bibliothèque : %d élément(s), %d dossier(s) de jeux", m_lib->count(), int(Settings::instance().romFolders().size()));
    if (Settings::instance().scanOnStartup() && !Settings::instance().romFolders().isEmpty())
        QTimer::singleShot(300, this, &MainWindow::startScan);
    else
        m_covers->requestMissing();
    if (Settings::instance().value(QStringLiteral("app/autoUpdateCheck"), true).toBool() && !SelfUpdate::instance().repo().isEmpty())
        QTimer::singleShot(4000, this, [] { SelfUpdate::instance().check(false); });
}

void MainWindow::showPage(int page) {
    const bool changed = m_pages->currentIndex() != page;
    m_pages->setCurrentIndex(page);
    if (changed) { // fondu court : on voit que la page a changé
        QWidget* w = m_pages->currentWidget();
        auto* fx = new QGraphicsOpacityEffect(w);
        w->setGraphicsEffect(fx);
        auto* anim = new QPropertyAnimation(fx, "opacity", w);
        anim->setDuration(170);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QPropertyAnimation::finished, w, [w] { w->setGraphicsEffect(nullptr); });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
    m_navEmus->setChecked(page == EmulatorsPageIdx);
    m_navSaves->setChecked(page == SavesPageIdx);
    m_navSettings->setChecked(page == SettingsPageIdx);
    if (page == SavesPageIdx)
        m_savesPage->activate();
    if (page == EmulatorsPageIdx)
        m_emuPage->refresh();
    if (page == LibraryPage)
        m_view->setFocus();
    m_toast->reposition();
}

void MainWindow::updateNavBadges() {
    const int updates = EmulatorManager::instance().updatesAvailable();
    m_navEmus->setIcon(Theme::icon(QStringLiteral("gamepad"), updates > 0 ? Theme::Color::gold : Theme::Color::muted, 18));
    m_navEmus->setToolTip(updates > 0 ? tr("%n mise(s) à jour disponible(s)", nullptr, updates) : QString());
}

void MainWindow::notify(const QString& text, bool error) {
    m_toast->showMessage(text, error ? Theme::Color::danger : Theme::Color::teal, error ? 4500 : 2800);
}

void MainWindow::showFirstRunSummary() {
    if (m_firstRunShown)
        return;
    m_firstRunShown = true;
    Settings::instance().setFirstRunDone(true);
    Settings::instance().sync();
    auto& em = EmulatorManager::instance();
    QString found;
    QStringList missing;
    for (const auto& e : Registry::instance().emulators()) {
        const EmulatorStatus st = em.status(e.id);
        if (!st.installed()) {
            missing << e.name;
            continue;
        }
        QStringList chips;
        for (const auto& f : em.systemFiles(e.id)) {
            const QColor c = f.present ? Theme::Color::teal : (f.def.required ? Theme::Color::danger : Theme::Color::faint);
            chips << QStringLiteral("<span style='color:%1'>%2 %3</span>")
                         .arg(c.name(), f.present ? QStringLiteral("&#10003;") : QStringLiteral("&#10007;"), f.def.label.toHtmlEscaped());
        }
        found += QStringLiteral("<p style='margin:10px 0 0 0'><span style='font-size:17px; font-weight:700'>%1</span>"
                                "&nbsp;&nbsp;<span style='color:%2'>%3</span><br>"
                                "<span style='color:%4'>%5</span>%6</p>")
                     .arg(e.name.toHtmlEscaped(), Theme::Color::teal.name(), st.version.toHtmlEscaped(), Theme::Color::muted.name(),
                          QDir::toNativeSeparators(st.exe).toHtmlEscaped(),
                          chips.isEmpty() ? QString() : QStringLiteral("<br>") + chips.join(QStringLiteral("&nbsp;&nbsp;&nbsp;")));
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Bienvenue dans Playnest"));
    dlg.setMinimumWidth(680);
    auto* v = new QVBoxLayout(&dlg);
    v->setContentsMargins(26, 22, 26, 18);
    v->setSpacing(10);
    auto* title = new QLabel(tr("Ce que Playnest a trouvé sur ton PC"), &dlg);
    title->setFont(Theme::font(26, QFont::Bold, true));
    v->addWidget(title);
    auto* body = new QLabel(&dlg);
    body->setTextFormat(Qt::RichText);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QString text = found.isEmpty() ? tr("<p>Aucun émulateur n'a été trouvé pour l'instant.</p>") : found;
    if (!missing.isEmpty())
        text += tr("<p style='margin-top:16px; color:%1'>Pas trouvés sur ce PC : %2. Tu peux les installer depuis la page "
                   "Émulateurs, en un clic pour la plupart.</p>").arg(Theme::Color::muted.name(), missing.join(QStringLiteral(", ")).toHtmlEscaped());
    body->setText(text);
    v->addWidget(body);
    v->addSpacing(8);
    auto* row = new QHBoxLayout;
    row->addStretch();
    QPushButton* add = nullptr;
    if (Settings::instance().romFolders().isEmpty())
        add = new QPushButton(tr("Ajouter un dossier de jeux"), &dlg);
    auto* emus = new QPushButton(tr("Voir les émulateurs"), &dlg);
    emus->setStyleSheet(Theme::primaryButtonStyle(Theme::Color::gold, true));
    auto* close = new QPushButton(tr("Fermer"), &dlg);
    if (add)
        row->addWidget(add);
    row->addWidget(emus);
    row->addWidget(close);
    v->addLayout(row);
    int choice = 0;
    connect(emus, &QPushButton::clicked, &dlg, [&] { choice = 1; dlg.accept(); });
    if (add)
        connect(add, &QPushButton::clicked, &dlg, [&] { choice = 2; dlg.accept(); });
    connect(close, &QPushButton::clicked, &dlg, &QDialog::reject);
    dlg.exec();
    if (choice == 1)
        showPage(EmulatorsPageIdx);
    else if (choice == 2)
        addFolder();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    Settings::instance().setWindowGeometry(saveGeometry());
    Settings::instance().setLastCategory(m_sidebar->currentCategory());
    Settings::instance().sync();
    m_scanner->cancel();
    m_lib->save();
    e->accept();
}

// Change la langue : le choix est enregistré, puis Playnest se relance aussitôt dans la nouvelle langue.
void MainWindow::changeLanguage(const QString& lang) {
    const QString effective = lang == QLatin1String("auto") ? Lang::systemLanguage() : lang;
    qInfo("Langue demandée : %s (actuelle : %s)", qUtf8Printable(lang), qUtf8Printable(Lang::current()));
    Settings::instance().setValue(QStringLiteral("ui/language"), lang);
    Settings::instance().sync();
    if (effective == Lang::current())
        return;
    if (m_launcher->isRunning()) {
        notify(tr("La langue changera au prochain lancement de Playnest : un jeu est en cours."), true);
        return;
    }
    Settings::instance().setWindowGeometry(saveGeometry());
    Settings::instance().sync();
    m_scanner->cancel();
    m_lib->save();
    QStringList args{QStringLiteral("--restarted")};
    QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
    QCoreApplication::quit();
}

void MainWindow::changeEvent(QEvent* e) {
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange || e->type() == QEvent::ActivationChange)
        updateGamepadState();
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e) {
    if (e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (obj == m_view) {
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                launchGame(currentPath());
                return true;
            }
            if (ke->key() == Qt::Key_Menu) {
                const QModelIndex idx = m_view->currentIndex();
                if (idx.isValid())
                    showGameMenu(currentPath(), m_view->viewport()->mapToGlobal(m_view->visualRect(idx).center()));
                return true;
            }
            // Taper du texte dans la grille lance la recherche.
            const QString t = ke->text();
            if (!t.isEmpty() && t.at(0).isPrint() && !t.at(0).isSpace() &&
                !(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
                m_search->setFocus();
                m_search->insert(t);
                return true;
            }
        } else if (obj == m_search) {
            if (ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                m_view->setFocus();
                if (!m_view->currentIndex().isValid() && m_filter->rowCount() > 0)
                    m_view->setCurrentIndex(m_filter->index(0, 0));
                return true;
            }
            if (ke->key() == Qt::Key_Escape) {
                m_search->clear();
                m_view->setFocus();
                return true;
            }
        }
    } else if (e->type() == QEvent::Resize && (obj == m_view || obj == m_view->viewport())) {
        relayoutGrid();
        m_toast->reposition();
    }
    return QMainWindow::eventFilter(obj, e);
}

// ---------------------------------------------------------------------------
void MainWindow::setCategory(const QString& id) {
    const QString keep = currentPath();
    m_onHome = id == QLatin1String("__home");
    m_filter->setCategory(m_onHome ? QStringLiteral("__all") : id);
    m_delegate->setShowSystem(id.startsWith(QLatin1String("__")));
    Settings::instance().setLastCategory(id);
    m_ambient->setAccent(Theme::categoryColor(id));
    for (QWidget* control : {static_cast<QWidget*>(m_sort), static_cast<QWidget*>(m_size), static_cast<QWidget*>(m_detailsBtn)})
        control->setVisible(!m_onHome); // tri, taille des jaquettes et panneau : sans objet sur l'accueil
    if (m_onHome)
        m_home->refresh();
    updateHeader();
    updateEmptyState();
    updateHero();
    selectPath(keep);
    if (!m_view->currentIndex().isValid() && m_filter->rowCount() > 0)
        m_view->setCurrentIndex(m_filter->index(0, 0));
    m_view->viewport()->update();
}

void MainWindow::updateHeader() {
    const QString id = m_filter->category();
    const QColor c = Theme::categoryColor(id);
    QString name;
    QString year;
    if (m_onHome) name = tr("Accueil");
    else if (id == QLatin1String("__all")) name = tr("Tous les jeux");
    else if (id == QLatin1String("__fav")) name = tr("Favoris");
    else if (id == QLatin1String("__recent")) name = tr("Joués récemment");
    else if (id == QLatin1String("__addons")) name = tr("DLC et mises à jour");
    else if (const SystemInfo* s = Registry::instance().system(id)) {
        name = s->name;
        if (s->year)
            year = QString::number(s->year);
    }
    QString html = QStringLiteral("<span style='color:%1'>%2</span>").arg(c.name(), name.toHtmlEscaped());
    if (!year.isEmpty())
        html += QStringLiteral("<span style='color:%1; font-size:20px; font-weight:600;'>&nbsp;&nbsp;%2</span>")
                    .arg(Theme::Color::faint.name(), year);
    m_title->setText(html);
    const int shown = m_filter->rowCount();
    QString sub = id == QLatin1String("__addons") ? tr("%n contenu(s) additionnel(s)", nullptr, shown) : countText(shown);
    if (!m_search->text().trimmed().isEmpty())
        sub = tr("%1 pour « %2 »").arg(countText(shown), m_search->text().trimmed());
    if (m_onHome && m_search->text().trimmed().isEmpty())
        sub = tr("Choisis une console pour voir ses jeux.");
    m_subtitle->setText(sub);
}

void MainWindow::updateEmptyState() {
    m_details->setVisible(m_lib->count() > 0 && Settings::instance().detailsVisible() && !m_onHome);
    if (m_lib->count() == 0) {
        m_stack->setCurrentIndex(1);
    } else if (m_onHome) {
        m_stack->setCurrentIndex(3);
    } else if (m_filter->rowCount() == 0) {
        const QString q = m_search->text().trimmed();
        if (!q.isEmpty())
            m_noResults->setText(tr("Aucun jeu ne correspond à « %1 ».").arg(q));
        else if (m_filter->category() == QLatin1String("__fav"))
            m_noResults->setText(tr("Aucun favori pour l'instant.\nAppuie sur Y ou Ctrl+D sur un jeu pour l'ajouter ici."));
        else if (m_filter->category() == QLatin1String("__recent"))
            m_noResults->setText(tr("Les jeux que tu lances apparaîtront ici."));
        else if (m_filter->category() == QLatin1String("__addons"))
            m_noResults->setText(tr("Aucune mise à jour ni DLC trouvé.\nAjoute le dossier où tu les ranges, puis appuie sur F5."));
        else
            m_noResults->setText(tr("Aucun jeu pour cette console.\nAjoute un dossier qui en contient, puis appuie sur F5."));
        m_stack->setCurrentIndex(2);
    } else {
        m_stack->setCurrentIndex(0);
    }
}

void MainWindow::updateGamepadState() {
    const bool want = Settings::instance().gamepadEnabled() && !m_launcher->isRunning() &&
                      QGuiApplication::applicationState() == Qt::ApplicationActive && !isMinimized();
    m_pad->setEnabled(want);
    if (!want)
        m_padIcon->hide();
}

QString MainWindow::currentPath() const {
    const QModelIndex idx = m_view->currentIndex();
    return idx.isValid() ? idx.data(LibraryModel::PathRole).toString() : QString();
}

void MainWindow::selectPath(const QString& path) {
    if (path.isEmpty())
        return;
    const int src = m_lib->indexOf(path);
    if (src < 0)
        return;
    const QModelIndex idx = m_filter->mapFromSource(m_model->index(src, 0));
    if (idx.isValid()) {
        m_view->setCurrentIndex(idx);
        m_view->scrollTo(idx);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::startScan() {
    const QStringList folders = Settings::instance().romFolders();
    if (folders.isEmpty()) {
        updateEmptyState();
        m_statusText->setText(tr("Aucun dossier de jeux. Ajoute un dossier pour commencer."));
        return;
    }
    if (m_scanner->isRunning())
        return;
    m_statusProgress->setRange(0, 0);
    m_statusProgress->show();
    m_cancelScan->show();
    m_statusText->setText(tr("Recherche des jeux…"));
    qInfo("Recherche des jeux dans : %s", qUtf8Printable(folders.join(QStringLiteral(" | "))));
    // Résultats précédents : les fichiers inchangés ne sont pas relus (scan incrémental).
    QHash<QString, DetectedRom> known;
    for (const Game& g : m_lib->games()) {
        if (!g.modified)
            continue;
        known.insert(Scanner::key(g.path), Library::toDetected(g));
    }
    // Quand la détection s'améliore (nouvelle version), tout est relu une fois.
    // Pareil après un changement de langue : les noms des mises à jour et DLC sont écrits dans la langue de l'interface.
    const QString scanLang = Settings::instance().value(QStringLiteral("library/lang"), QStringLiteral("fr")).toString();
    if (Settings::instance().value(QStringLiteral("library/detectorVersion")).toInt() < RomDetector::kVersion || scanLang != Lang::current()) {
        known.clear();
        Settings::instance().setValue(QStringLiteral("library/detectorVersion"), RomDetector::kVersion);
        Settings::instance().setValue(QStringLiteral("library/lang"), Lang::current());
    }
    m_scanner->start(folders, known);
}

void MainWindow::addFolder() {
    const QString d = QFileDialog::getExistingDirectory(this, tr("Choisir un dossier de jeux"));
    if (d.isEmpty())
        return;
    QStringList f = Settings::instance().romFolders();
    auto known = [&f](const QString& p) {
        for (const QString& x : f)
            if (QDir::cleanPath(QDir::fromNativeSeparators(x)).compare(QDir::cleanPath(QDir::fromNativeSeparators(p)), Qt::CaseInsensitive) == 0)
                return true;
        return false;
    };
    if (!known(d))
        f << QDir::toNativeSeparators(d);
    // Dossiers voisins qui contiennent aussi des jeux (ex. « dlc switch » à côté de « switch game ») : on propose de les ajouter.
    const QStringList exts = Registry::instance().allExtensions();
    const QSet<QString> extSet(exts.begin(), exts.end());
    QStringList siblings;
    QDir parent(d);
    if (parent.cdUp()) {
        const auto dirs = parent.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (int i = 0; i < dirs.size() && i < 80; ++i) {
            const QString sd = dirs[i].absoluteFilePath();
            if (known(sd))
                continue;
            const auto files = QDir(sd).entryInfoList(QDir::Files, QDir::NoSort);
            for (int j = 0; j < files.size() && j < 300; ++j)
                if (extSet.contains(files[j].suffix().toLower())) {
                    siblings << QDir::toNativeSeparators(sd);
                    break;
                }
        }
    }
    if (!siblings.isEmpty()) {
        QStringList names;
        for (const QString& sdir : siblings)
            names << QStringLiteral("• ") + QFileInfo(sdir).fileName();
        QMessageBox box(QMessageBox::Question, tr("D'autres dossiers de jeux"),
                        tr("Ces dossiers voisins contiennent aussi des jeux, des mises à jour ou des DLC :"), QMessageBox::NoButton, this);
        box.setInformativeText(names.mid(0, 12).join(QLatin1Char('\n')));
        QPushButton* all = box.addButton(tr("Les ajouter aussi"), QMessageBox::AcceptRole);
        box.addButton(tr("Seulement celui-ci"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == all)
            f << siblings;
    }
    Settings::instance().setRomFolders(f);
    Settings::instance().sync();
    startScan();
}

void MainWindow::installEmulator(const QString& emuId, const QString& thenLaunchPath) {
    const EmulatorDef* e = Registry::instance().emulator(emuId);
    if (!e)
        return;
    if (!e->canAutoInstall()) {
        showPage(EmulatorsPageIdx);
        return;
    }
    m_pendingLaunch = thenLaunchPath;
    m_pendingLaunchEmu = emuId;
    m_toast->showMessage(tr("Installation de %1…").arg(e->name), Theme::Color::gold);
    EmulatorManager::instance().install(emuId);
}

void MainWindow::launchGame(const QString& path, const QString& emulatorId) {
    const Game* g = m_lib->find(path);
    if (!g)
        return;
    if (g->isAddon) { // une mise à jour ou un DLC ne se lance pas seul : on va au jeu
        if (const Game* base = m_lib->baseFor(*g)) {
            goToGame(base->path);
            notify(tr("Les mises à jour et DLC s'utilisent avec leur jeu : « %1 ».").arg(base->displayTitle()));
        } else {
            notify(tr("Une mise à jour ou un DLC ne se lance pas seul, et son jeu n'est pas dans ta bibliothèque."), true);
        }
        return;
    }
    const bool pcGame = g->system() == QLatin1String("pc"); // fan game, jeu PC : lancé directement
    auto& em = EmulatorManager::instance();
    const QString emuId = pcGame ? QString() : (emulatorId.isEmpty() ? em.emulatorFor(*g) : emulatorId);
    const EmulatorDef* emu = Registry::instance().emulator(emuId);
    if (!pcGame && !emu) {
        QMessageBox::warning(this, tr("Lancement impossible"), tr("Aucun émulateur ne prend en charge cette console."));
        return;
    }
    if (!pcGame) {
    // 1. Émulateur absent : proposer de l'installer.
    if (!em.isInstalled(emuId)) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("%1 n'est pas installé").arg(emu->name));
        box.setText(tr("Pour jouer à « %1 », Playnest a besoin de %2.").arg(g->displayTitle(), emu->name));
        QPushButton* install = nullptr;
        if (emu->canAutoInstall()) {
            box.setInformativeText(tr("Playnest peut télécharger la dernière version officielle, puis lancer le jeu."));
            install = box.addButton(tr("Installer et jouer"), QMessageBox::AcceptRole);
        } else {
            box.setInformativeText(tr("Télécharge-le sur son site officiel, puis indique son exécutable dans la page Émulateurs."));
        }
        QPushButton* page = box.addButton(tr("Voir les émulateurs"), QMessageBox::ActionRole);
        box.addButton(tr("Annuler"), QMessageBox::RejectRole);
        box.exec();
        if (install && box.clickedButton() == install)
            installEmulator(emu->id, path);
        else if (box.clickedButton() == page)
            showPage(EmulatorsPageIdx);
        return;
    }
    // 2. Fichiers système obligatoires manquants (clés, firmware).
    const QStringList missing = em.missingRequired(emuId);
    if (!missing.isEmpty()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Fichiers manquants pour %1").arg(emu->name));
        box.setText(tr("Il manque à %1 : %2.").arg(emu->name, missing.join(QStringLiteral(", "))));
        box.setInformativeText(tr("Sans ces fichiers extraits de ta console, le jeu ne démarrera probablement pas. "
                                  "La page Émulateurs permet d'importer tes clés."));
        QPushButton* page = box.addButton(tr("Voir les émulateurs"), QMessageBox::AcceptRole);
        QPushButton* anyway = box.addButton(tr("Lancer quand même"), QMessageBox::ActionRole);
        box.addButton(tr("Annuler"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == page) {
            showPage(EmulatorsPageIdx);
            return;
        }
        if (box.clickedButton() != anyway)
            return;
    }
    // 3. Runtime Visual C++ (Windows).
    if (emu->needs.contains(QStringLiteral("vcredist")) && !Tools::instance().status(QStringLiteral("vcredist")).ok &&
        !Settings::instance().value(QStringLiteral("tools/ignoreVcredist"), false).toBool()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Runtime Visual C++"));
        box.setText(tr("%1 a besoin du runtime Microsoft Visual C++ 2015-2022 (x64), absent ou trop ancien sur ce PC.").arg(emu->name));
        box.setInformativeText(Tools::instance().status(QStringLiteral("vcredist")).detail);
        QPushButton* install = box.addButton(tr("L'installer"), QMessageBox::AcceptRole);
        QPushButton* anyway = box.addButton(tr("Lancer quand même"), QMessageBox::ActionRole);
        box.addButton(tr("Annuler"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == install) {
            Tools::instance().install(QStringLiteral("vcredist"));
            notify(tr("Téléchargement du runtime Visual C++…"));
            return;
        }
        if (box.clickedButton() != anyway)
            return;
        Settings::instance().setValue(QStringLiteral("tools/ignoreVcredist"), true);
    }
    } // !pcGame
    QString err;
    if (!m_launcher->launch(*g, emuId, &err)) {
        QMessageBox::warning(this, tr("Lancement impossible"), err);
        return;
    }
    m_lib->update(path, [](Game& x) {
        x.lastPlayed = QDateTime::currentDateTime();
        x.playCount += 1;
    });
    m_toast->showMessage(tr("Lancement de « %1 » avec %2").arg(g->displayTitle(), emu->name), Theme::categoryColor(g->system()));
    if (Settings::instance().minimizeWhileRunning())
        QTimer::singleShot(1500, this, [this] {
            if (m_launcher->isRunning())
                showMinimized();
        });
}

void MainWindow::toggleFavorite(const QString& path) {
    if (path.isEmpty())
        return;
    bool fav = false;
    m_lib->update(path, [&](Game& g) {
        g.favorite = !g.favorite;
        fav = g.favorite;
    });
    m_toast->showMessage(fav ? tr("Ajouté aux favoris") : tr("Retiré des favoris"), Theme::Color::pink, 1400);
}

void MainWindow::renameGame(const QString& path) {
    const Game* g = m_lib->find(path);
    if (!g)
        return;
    bool ok = false;
    const QString t = QInputDialog::getText(this, tr("Renommer"), tr("Titre affiché (laisse vide pour revenir au titre détecté) :"),
                                            QLineEdit::Normal, g->displayTitle(), &ok);
    if (!ok)
        return;
    m_lib->update(path, [&](Game& x) { x.customTitle = (t.trimmed() == x.title) ? QString() : t.trimmed(); });
}

void MainWindow::showCoverMenu(const QString& path, const QPoint& pos) {
    const Game* g = m_lib->find(path);
    if (!g)
        return;
    QMenu menu(this);
    menu.addAction(tr("Choisir une image…"), this, [this, path] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Choisir une jaquette"), QString(),
                                                       tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
        if (f.isEmpty())
            return;
        QString err;
        if (!m_covers->setCustomCover(path, f, &err))
            QMessageBox::warning(this, tr("Jaquette"), err);
    });
    menu.addAction(tr("Rechercher en ligne"), this, [this, path] {
        if (const Game* x = m_lib->find(path)) {
            m_covers->request(*x, true);
            m_toast->showMessage(tr("Recherche de la jaquette…"), Theme::Color::gold, 1500);
        }
    });
    QAction* clear = menu.addAction(tr("Retirer la jaquette"), this, [this, path] {
        m_lib->update(path, [](Game& x) { x.coverPath.clear(); });
    });
    clear->setEnabled(!g->coverPath.isEmpty());
    menu.exec(pos);
}

void MainWindow::showGameMenu(const QString& path, const QPoint& pos) {
    const Game* g = m_lib->find(path);
    if (!g)
        return;
    const Game game = *g; // copie : le menu peut modifier la bibliothèque
    QMenu menu(this);
    QAction* play = menu.addAction(Theme::icon(QStringLiteral("play")), tr("Jouer"), this, [this, path] { launchGame(path); });
    play->setEnabled(!m_launcher->isRunning());
    QMenu* with = menu.addMenu(tr("Jouer avec"));
    for (const EmulatorDef* e : Registry::instance().emulatorsFor(game.system())) {
        const bool found = EmulatorManager::instance().isInstalled(e->id);
        with->addAction(found ? e->name : tr("%1 (non installé)").arg(e->name), this, [this, path, id = e->id] { launchGame(path, id); });
    }
    with->setEnabled(!m_launcher->isRunning());
    menu.addSeparator();
    menu.addAction(Theme::icon(game.favorite ? QStringLiteral("star-filled") : QStringLiteral("star"), Theme::Color::pink),
                   game.favorite ? tr("Retirer des favoris") : tr("Ajouter aux favoris"), this, [this, path] { toggleFavorite(path); });
    menu.addAction(tr("Renommer…"), this, [this, path] { renameGame(path); });

    QMenu* sysMenu = menu.addMenu(tr("Changer de console"));
    const QString ext = QFileInfo(game.path).suffix().toLower();
    QStringList candidates = Registry::instance().systemsForExtension(ext);
    if (candidates.isEmpty())
        candidates << game.system();
    for (const QString& sid : candidates) {
        const SystemInfo* s = Registry::instance().system(sid);
        QAction* a = sysMenu->addAction(s ? s->name : sid, this, [this, path, sid] {
            m_lib->update(path, [&](Game& x) { x.systemOverride = (sid == x.systemId) ? QString() : sid; });
            m_sidebar->refreshCounts();
            if (m_home)
                m_home->refresh();
        });
        a->setCheckable(true);
        a->setChecked(sid == game.system());
    }
    sysMenu->setEnabled(candidates.size() > 1);

    QMenu* coverMenu = menu.addMenu(tr("Jaquette"));
    coverMenu->addAction(tr("Choisir une image…"), this, [this, path] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Choisir une jaquette"), QString(),
                                                       tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
        if (f.isEmpty())
            return;
        QString err;
        if (!m_covers->setCustomCover(path, f, &err))
            QMessageBox::warning(this, tr("Jaquette"), err);
    });
    coverMenu->addAction(tr("Rechercher en ligne"), this, [this, path] {
        if (const Game* x = m_lib->find(path))
            m_covers->request(*x, true);
    });
    menu.addSeparator();
    menu.addAction(Theme::icon(QStringLiteral("folder")), tr("Ouvrir le dossier du jeu"), this, [path] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
    menu.addAction(tr("Copier le chemin"), this, [path] { QApplication::clipboard()->setText(QDir::toNativeSeparators(path)); });
    menu.addSeparator();
    menu.addAction(tr("Retirer de la bibliothèque"), this, [this, path] {
        QStringList pc = Settings::instance().value(QStringLiteral("pc/games")).toStringList();
        pc.removeIf([&](const QString& p) { return QDir::cleanPath(QDir::fromNativeSeparators(p)).compare(QDir::cleanPath(QDir::fromNativeSeparators(path)), Qt::CaseInsensitive) == 0; });
        Settings::instance().setValue(QStringLiteral("pc/games"), pc);
        m_lib->remove(path);
    });
    menu.exec(pos);
}

void MainWindow::debugShow(const QString& what) {
    if (what == QLatin1String("organize")) {
        openOrganizer();
        return;
    }
    if (what.startsWith(QLatin1String("lang:"))) {
        changeLanguage(what.mid(5));
        return;
    }
    if (what.startsWith(QLatin1String("select:"))) {
        for (const Game& g : m_lib->games())
            if (g.displayTitle().startsWith(what.mid(7)))
                return goToGame(g.path);
        return;
    }
    m_sidebar->selectCategory(what);
}

void MainWindow::updateHero() {
    const bool want = m_onHome && m_search->text().trimmed().isEmpty() &&
                      m_lib->count() > 0 && height() >= 700;
    m_hero->setVisible(want && m_hero->refresh());
}

namespace {
bool isEmulatorFile(const QString& f) {
    for (const char* ext : {".zip", ".7z", ".exe", ".msi"})
        if (f.endsWith(QLatin1String(ext), Qt::CaseInsensitive))
            return true;
    return false;
}
QStringList droppedPaths(const QMimeData* md) {
    QStringList out;
    if (md && md->hasUrls())
        for (const QUrl& u : md->urls())
            if (u.isLocalFile())
                out << u.toLocalFile();
    return out;
}
} // namespace

// Glisser sur la fenêtre : des jeux (ou un dossier de jeux) → ajoutés et rangés ;
// l'archive ou l'installateur d'un émulateur → mise à jour de cet émulateur.
void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (!droppedPaths(e->mimeData()).isEmpty())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    const QStringList paths = droppedPaths(e->mimeData());
    if (paths.isEmpty())
        return;
    e->acceptProposedAction();
    QStringList games, pcGames;
    QString emuFile;
    for (const QString& p : paths) {
        if (QFileInfo(p).isDir() && RomDetector::detectPcGame(p, true)) { // dossier d'un fan game
            pcGames << RomDetector::detectPcGame(p, true)->path;
        } else if (p.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive) &&
                   EmulatorManager::instance().classifyFile(p).kind == EmulatorManager::FileKind::Unknown) {
            pcGames << p; // un .exe qui n'est pas un émulateur : un jeu PC
        } else if (isEmulatorFile(p)) {
            emuFile = p;
        } else {
            games << p;
        }
    }
    if (!pcGames.isEmpty())
        addPcGames(pcGames);
    if (!games.isEmpty()) {
        importGames(games);
        return;
    }
    if (!pcGames.isEmpty())
        return;
    if (!emuFile.isEmpty() && EmulatorsPage::installFromFile(this, emuFile))
        showPage(EmulatorsPageIdx);
}

// Jeux PC et fan games : ajoutés là où ils sont (ils gardent leurs sauvegardes et leurs raccourcis).
void MainWindow::addPcGames(const QStringList& exes) {
    QStringList list = Settings::instance().value(QStringLiteral("pc/games")).toStringList();
    QStringList titles, added;
    for (const QString& e : exes) {
        const auto r = RomDetector::detectPcGame(e, false);
        if (!r)
            continue;
        titles << r->title + (r->internalTitle.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(r->internalTitle));
        if (!list.contains(r->path, Qt::CaseInsensitive)) {
            list << r->path;
            added << r->path;
        }
    }
    if (titles.isEmpty()) {
        notify(tr("Aucun jeu trouvé dans ce que tu as déposé."), true);
        return;
    }
    if (QMessageBox::question(this, tr("Ajouter des jeux PC"),
                              tr("Ajouter à « Jeux PC et fan games » ?\n\n• %1\n\nIls se lanceront directement, sans émulateur, depuis leur dossier actuel.")
                                  .arg(titles.join(QStringLiteral("\n• ")))) != QMessageBox::Yes)
        return;
    Settings::instance().setValue(QStringLiteral("pc/games"), list);
    Settings::instance().sync();
    m_selectAfterScan = added.isEmpty() ? QString() : added.first();
    startScan();
}

void MainWindow::chooseGamesToImport() {
    const QStringList exts = Registry::instance().allExtensions();
    QStringList patterns;
    for (const QString& x : exts)
        patterns << QStringLiteral("*.") + x;
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Ajouter des jeux"),
                                                            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                                                            tr("Jeux, mises à jour et DLC (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (!files.isEmpty())
        importGames(files);
}

void MainWindow::importGames(const QStringList& paths) {
    if (m_org->isRunning()) {
        notify(tr("Un rangement est déjà en cours, réessaie dans un instant."), true);
        return;
    }
    Organizer::Plan plan = m_org->planImport(paths, false);
    if (plan.moves.isEmpty()) {
        if (plan.alreadyInPlace > 0) {
            m_selectAfterScan = plan.importedPaths.value(0);
            startScan();
            notify(tr("Ces jeux sont déjà dans « %1 » : ils apparaissent dans ta bibliothèque.").arg(QFileInfo(plan.options.root).fileName()));
        } else {
            notify(tr("Aucun jeu reconnu dans ce que tu as déposé."), true);
        }
        return;
    }
    // Aperçu : où va chaque fichier.
    QStringList lines;
    for (int i = 0; i < plan.moves.size() && i < 8; ++i) {
        const auto& m = plan.moves[i];
        lines << QStringLiteral("• %1  →  %2\\%3").arg(m.label, Organizer::kindFolder(m.kind), m.group);
    }
    if (plan.moves.size() > 8)
        lines << tr("… et %n autre(s)", nullptr, int(plan.moves.size() - 8));
    if (plan.unknown > 0)
        lines << QString() << tr("%n fichier(s) ignoré(s) : ce ne sont pas des jeux reconnus.", nullptr, plan.unknown);
    if (!plan.warnings.isEmpty())
        lines << QString() << plan.warnings;
    QMessageBox box(QMessageBox::Question, tr("Ajouter des jeux"),
                    tr("Ajouter %n élément(s) à ta bibliothèque ? Ils seront rangés dans « %1 ».", nullptr, int(plan.moves.size()))
                        .arg(QDir::toNativeSeparators(plan.options.root)),
                    QMessageBox::NoButton, this);
    box.setInformativeText(lines.join(QLatin1Char('\n')));
    QPushButton* move = box.addButton(tr("Déplacer"), QMessageBox::AcceptRole);
    QPushButton* copy = box.addButton(tr("Copier"), QMessageBox::ActionRole);
    box.addButton(tr("Annuler"), QMessageBox::RejectRole);
    move->setToolTip(tr("Sur le même disque, c'est instantané."));
    copy->setToolTip(tr("L'original reste où il est (utile depuis une clé USB)."));
    box.setDefaultButton(move);
    box.exec();
    if (box.clickedButton() != move && box.clickedButton() != copy)
        return;
    if (box.clickedButton() == copy)
        plan = m_org->planImport(paths, true);
    m_importing = true;
    m_statusText->setText(tr("Ajout des jeux…"));
    m_statusProgress->setRange(0, 1000);
    m_statusProgress->setValue(0);
    m_statusProgress->show();
    m_org->run(plan);
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    if (m_hero)
        updateHero();
}

void MainWindow::goToGame(const QString& path) {
    const Game* g = m_lib->find(path);
    if (!g)
        return;
    showPage(LibraryPage);
    const QModelIndex idx = m_filter->mapFromSource(m_model->index(m_lib->indexOf(path), 0));
    if (!idx.isValid()) {
        m_search->clear();
        const QString cat = g->isAddon && Settings::instance().hideAddons() ? QStringLiteral("__addons") : QStringLiteral("__all");
        m_sidebar->selectCategory(cat);
        setCategory(cat);
    }
    selectPath(path);
}

void MainWindow::openOrganizer() {
    if (m_launcher->isRunning()) {
        notify(tr("Ferme le jeu en cours avant de ranger tes fichiers."), true);
        return;
    }
    if (m_scanner->isRunning())
        m_scanner->cancel();
    const QString keep = currentPath();
    OrganizeDialog dlg(m_org, this);
    dlg.exec();
    SaveManager::instance().reload();
    m_sidebar->refreshCounts();
    if (m_home)
        m_home->refresh();
    m_emuPage->refresh();
    selectPath(keep);
}

void MainWindow::toggleFullscreen() {
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void MainWindow::relayoutGrid() {
    // Colonnes qui remplissent toute la largeur : l'espace restant est réparti entre les cartes.
    // On réserve toujours la place de la barre de défilement pour éviter les oscillations.
    const int sb = m_view->verticalScrollBar()->isVisible() ? 0 : m_view->verticalScrollBar()->sizeHint().width();
    const int avail = m_view->viewport()->width() - sb - 4;
    const QSize card = m_delegate->cardSize();
    const int cols = qMax(1, avail / card.width());
    const int cellW = qMax(card.width(), avail / cols);
    const QSize grid(cellW, card.height());
    if (m_view->gridSize() != grid)
        m_view->setGridSize(grid);
}

void MainWindow::sendKey(QWidget* target, int key, Qt::KeyboardModifiers mods) {
    if (!target)
        return;
    QKeyEvent press(QEvent::KeyPress, key, mods);
    QKeyEvent release(QEvent::KeyRelease, key, mods);
    QApplication::sendEvent(target, &press);
    QApplication::sendEvent(target, &release);
}

void MainWindow::onGamepad(Gamepad::Action a) {
    // Menu contextuel ouvert : la manette le pilote.
    if (QWidget* popup = QApplication::activePopupWidget()) {
        switch (a) {
        case Gamepad::Up: sendKey(popup, Qt::Key_Up); break;
        case Gamepad::Down: sendKey(popup, Qt::Key_Down); break;
        case Gamepad::Left: sendKey(popup, Qt::Key_Left); break;
        case Gamepad::Right: sendKey(popup, Qt::Key_Right); break;
        case Gamepad::Accept: sendKey(popup, Qt::Key_Return); break;
        case Gamepad::Back:
        case Gamepad::Menu: sendKey(popup, Qt::Key_Escape); break;
        default: break;
        }
        return;
    }
    if (QWidget* modal = QApplication::activeModalWidget()) {
        if (a == Gamepad::Back)
            sendKey(modal, Qt::Key_Escape);
        return;
    }
    // Pages Émulateurs / Sauvegardes / Paramètres : navigation entre les boutons.
    if (m_pages->currentIndex() != LibraryPage) {
        QWidget* focus = QApplication::focusWidget();
        switch (a) {
        case Gamepad::Up:
        case Gamepad::Left: focusNextPrevChild(false); break;
        case Gamepad::Down:
        case Gamepad::Right: focusNextPrevChild(true); break;
        case Gamepad::Accept:
            if (auto* b = qobject_cast<QAbstractButton*>(focus))
                b->click();
            break;
        case Gamepad::Back: showPage(LibraryPage); break;
        case Gamepad::PrevCategory:
        case Gamepad::NextCategory:
            showPage(LibraryPage);
            m_sidebar->step(a == Gamepad::NextCategory ? 1 : -1);
            break;
        case Gamepad::Options: toggleFullscreen(); break;
        default: break;
        }
        return;
    }
    if (m_onHome && m_stack->currentIndex() == 3) { // accueil : la manette parcourt le mur des consoles
        if (!m_home->hasFocus())
            m_home->setFocus();
        switch (a) {
        case Gamepad::Up: sendKey(m_home, Qt::Key_Up); break;
        case Gamepad::Down: sendKey(m_home, Qt::Key_Down); break;
        case Gamepad::Left: sendKey(m_home, Qt::Key_Left); break;
        case Gamepad::Right: sendKey(m_home, Qt::Key_Right); break;
        case Gamepad::Accept: sendKey(m_home, Qt::Key_Return); break;
        case Gamepad::PrevCategory: m_sidebar->step(-1); break;
        case Gamepad::NextCategory: m_sidebar->step(+1); break;
        case Gamepad::Options: toggleFullscreen(); break;
        default: break;
        }
        return;
    }
    if (!m_view->hasFocus())
        m_view->setFocus();
    switch (a) {
    case Gamepad::Up: sendKey(m_view, Qt::Key_Up); break;
    case Gamepad::Down: sendKey(m_view, Qt::Key_Down); break;
    case Gamepad::Left: sendKey(m_view, Qt::Key_Left); break;
    case Gamepad::Right: sendKey(m_view, Qt::Key_Right); break;
    case Gamepad::PageUp: sendKey(m_view, Qt::Key_PageUp); break;
    case Gamepad::PageDown: sendKey(m_view, Qt::Key_PageDown); break;
    case Gamepad::Accept: launchGame(currentPath()); break;
    case Gamepad::Back:
        if (!m_search->text().isEmpty())
            m_search->clear();
        else if (isFullScreen())
            showNormal();
        break;
    case Gamepad::PrevCategory: m_sidebar->step(-1); break;
    case Gamepad::NextCategory: m_sidebar->step(+1); break;
    case Gamepad::Favorite: toggleFavorite(currentPath()); break;
    case Gamepad::Menu: {
        const QModelIndex idx = m_view->currentIndex();
        if (idx.isValid())
            showGameMenu(currentPath(), m_view->viewport()->mapToGlobal(m_view->visualRect(idx).center()));
        break;
    }
    case Gamepad::Options: toggleFullscreen(); break;
    }
}
