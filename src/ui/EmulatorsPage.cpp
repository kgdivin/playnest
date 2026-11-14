#include "../core/Lang.h"
#include "EmulatorsPage.h"

#include "../core/Emulators.h"
#include "../core/Registry.h"
#include "../core/Settings.h"
#include "../core/Tools.h"
#include "Theme.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QStandardPaths>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QLabel* heading(const QString& text, QWidget* parent, int px = 22) {
    auto* l = new QLabel(text, parent);
    l->setFont(Theme::font(px, QFont::Bold, true));
    return l;
}

QLabel* muted(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setProperty("role", "muted");
    return l;
}

QString pill(const QString&, const QColor& c) {
    QColor bg = c;
    bg.setAlpha(40);
    return QStringLiteral("QLabel { background: rgba(%1,%2,%3,%4); color: %5; border-radius: 10px; padding: 3px 10px; font-weight: 600; }")
               .arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(bg.alpha()).arg(c.name());
}

QString cardStyle(const QString& name) {
    return QStringLiteral("#%1 { background: %2; border: 1px solid %3; border-radius: 10px; }")
        .arg(name, Theme::Color::panel.name(), Theme::Color::line.name());
}

QString nativePath(const QString& p) { return QDir::toNativeSeparators(p); }

} // namespace

// ===========================================================================
// ToolCard : un outil système (runtime VC++, 7-Zip, Vulkan).
// ===========================================================================
class ToolCard : public QFrame {
public:
    ToolCard(const QString& id, QWidget* parent) : QFrame(parent), m_id(id) {
        setObjectName(QStringLiteral("toolCard"));
        setStyleSheet(cardStyle(QStringLiteral("toolCard")));
        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(14, 12, 14, 12);
        v->setSpacing(6);
        auto* top = new QHBoxLayout;
        m_dot = new QLabel(this);
        m_dot->setFixedSize(10, 10);
        top->addWidget(m_dot);
        auto* name = new QLabel(Tools::instance().label(id), this);
        name->setFont(Theme::font(16, QFont::DemiBold, true));
        top->addWidget(name);
        top->addStretch();
        v->addLayout(top);
        m_detail = muted(QString(), this);
        v->addWidget(m_detail);
        m_progress = new QProgressBar(this);
        m_progress->setFixedHeight(6);
        m_progress->hide();
        v->addWidget(m_progress);
        m_button = new QPushButton(this);
        m_button->setCursor(Qt::PointingHandCursor);
        v->addWidget(m_button, 0, Qt::AlignLeft);
        v->addStretch();
        QObject::connect(m_button, &QPushButton::clicked, this, [this] {
            if (m_id == QLatin1String("vulkan"))
                Tools::instance().checkVulkanAsync();
            else
                Tools::instance().install(m_id);
        });
        QObject::connect(&Tools::instance(), &Tools::installProgress, this, [this](const QString& id, const QString& stage, qint64 r, qint64 t) {
            if (id != m_id)
                return;
            m_detail->setText(stage);
            m_progress->show();
            m_progress->setRange(0, t > 0 ? 1000 : 0);
            if (t > 0)
                m_progress->setValue(int(r * 1000 / t));
        });
        refresh();
    }
    void refresh() {
        const Tools::Status st = Tools::instance().status(m_id);
        const bool busy = Tools::instance().isBusy(m_id);
        const QColor c = st.ok ? Theme::Color::teal : (m_id == QLatin1String("7zip") ? Theme::Color::faint : Theme::Color::gold);
        m_dot->setStyleSheet(QStringLiteral("background: %1; border-radius: 5px;").arg(c.name()));
        if (!busy) {
            m_detail->setText(st.detail);
            m_progress->hide();
        }
        if (m_id == QLatin1String("vulkan")) {
            m_button->setText(QObject::tr("Vérifier à nouveau"));
            m_button->setVisible(true);
        } else {
            m_button->setText(st.ok ? QObject::tr("Réinstaller") : QObject::tr("Installer"));
            m_button->setVisible(st.installable && (!st.ok || m_id == QLatin1String("vcredist")));
        }
        m_button->setEnabled(!busy);
    }

private:
    QString m_id;
    QLabel* m_dot;
    QLabel* m_detail;
    QProgressBar* m_progress;
    QPushButton* m_button;
};

// ===========================================================================
// EmulatorCard : état et actions d'un émulateur.
// ===========================================================================
class EmulatorCard : public QFrame {
public:
    EmulatorCard(const EmulatorDef& e, EmulatorsPage* page) : QFrame(page), m_id(e.id), m_page(page) {
        setObjectName(QStringLiteral("emuCard"));
        setStyleSheet(cardStyle(QStringLiteral("emuCard")));
        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(18, 14, 18, 14);
        v->setSpacing(7);

        auto* top = new QHBoxLayout;
        auto* name = new QLabel(e.name, this);
        name->setFont(Theme::font(20, QFont::Bold, true));
        top->addWidget(name);
        QStringList sys;
        for (const QString& sid : e.systems) {
            const SystemInfo* s = Registry::instance().system(sid);
            sys << QStringLiteral("<span style='color:%1'>%2</span>").arg(Theme::categoryColor(sid).name(), s ? s->name : sid);
        }
        auto* systems = new QLabel(sys.join(QStringLiteral(", ")), this);
        systems->setTextFormat(Qt::RichText);
        top->addSpacing(10);
        top->addWidget(systems);
        top->addStretch();
        m_pill = new QLabel(this);
        top->addWidget(m_pill);
        v->addLayout(top);

        m_path = new QLabel(this);
        m_path->setWordWrap(true);
        m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_path->setTextFormat(Qt::RichText);
        v->addWidget(m_path);

        m_data = new QLabel(this);
        m_data->setWordWrap(true);
        m_data->setTextFormat(Qt::RichText);
        m_data->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->addWidget(m_data);

        m_filesRow = new QHBoxLayout;
        m_filesRow->setSpacing(8);
        v->addLayout(m_filesRow);

        if (!e.notes.isEmpty())
            v->addWidget(muted(e.notes, this));

        m_stage = muted(QString(), this);
        m_stage->hide();
        v->addWidget(m_stage);
        m_progress = new QProgressBar(this);
        m_progress->setFixedHeight(6);
        m_progress->hide();
        v->addWidget(m_progress);

        auto* btns = new QHBoxLayout;
        btns->setSpacing(6);
        m_install = new QPushButton(this);
        m_install->setCursor(Qt::PointingHandCursor);
        m_rollback = new QPushButton(QObject::tr("Revenir à la version précédente"), this);
        auto* fromFile = new QPushButton(Theme::icon(QStringLiteral("download")), QObject::tr("Depuis un fichier…"), this);
        fromFile->setToolTip(QObject::tr("Tu as déjà téléchargé la nouvelle version ? Choisis le fichier : archive (.zip, .7z), "
                                         "application (.exe) ou installateur officiel (.exe, .msi)."));
        fromFile->setVisible(!e.exeNames.isEmpty());
        auto* browse = new QPushButton(QObject::tr("Choisir l'exécutable…"), this);
        auto* args = new QPushButton(QObject::tr("Arguments…"), this);
        m_open = new QPushButton(Theme::icon(QStringLiteral("folder")), QObject::tr("Dossier"), this);
        auto* site = new QPushButton(QObject::tr("Site officiel"), this);
        for (QPushButton* b : {m_install, fromFile, m_rollback, browse, args, m_open, site})
            btns->addWidget(b);
        btns->addStretch();
        v->addLayout(btns);

        auto& em = EmulatorManager::instance();
        const QString id = m_id;
        QObject::connect(m_install, &QPushButton::clicked, this, [id] { EmulatorManager::instance().install(id); });
        QObject::connect(fromFile, &QPushButton::clicked, this, [this, id] {
            const QString f = QFileDialog::getOpenFileName(this, QObject::tr("Archive de la nouvelle version"),
                                                           QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                                                           QObject::tr("Nouvelle version (*.zip *.7z *.exe *.msi)"));
            if (!f.isEmpty())
                EmulatorsPage::installFromFile(this, f, id);
        });
        QObject::connect(m_rollback, &QPushButton::clicked, this, [this, id] {
            const EmulatorDef* d = Registry::instance().emulator(id);
            if (QMessageBox::question(this, QObject::tr("Version précédente"),
                                      QObject::tr("Remettre la version de %1 utilisée avant la dernière mise à jour ? "
                                                  "Tes réglages et sauvegardes ne sont pas touchés.").arg(d ? d->name : id)) != QMessageBox::Yes)
                return;
            QString err;
            if (!EmulatorManager::instance().rollback(id, &err))
                QMessageBox::warning(this, QObject::tr("Version précédente"), err);
            else
                emit m_page->message(QObject::tr("%1 est revenu à sa version précédente.").arg(d ? d->name : id), false);
        });
        QObject::connect(browse, &QPushButton::clicked, this, [this, id] {
            const EmulatorDef* d = Registry::instance().emulator(id);
            if (!d)
                return;
#ifdef Q_OS_WIN
            const QString filter = QObject::tr("Exécutable (%1);;Tous les fichiers (*)").arg(d->exeNames.join(QLatin1Char(' ')));
#else
            const QString filter = QObject::tr("Tous les fichiers (*)");
#endif
            const QString start = EmulatorManager::instance().status(id).dir;
            const QString f = QFileDialog::getOpenFileName(this, QObject::tr("Exécutable de %1").arg(d->name), start, filter);
            if (!f.isEmpty())
                EmulatorManager::instance().setExePath(id, QDir::toNativeSeparators(f));
        });
        QObject::connect(args, &QPushButton::clicked, this, [this, id] { m_pageEditArgs(id); });
        QObject::connect(m_open, &QPushButton::clicked, this, [id] {
            const QString dir = EmulatorManager::instance().status(id).dir;
            if (!dir.isEmpty())
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        });
        QObject::connect(site, &QPushButton::clicked, this, [id] {
            const EmulatorDef* d = Registry::instance().emulator(id);
            if (!d)
                return;
            QString url = d->website;
            if (url.isEmpty() && d->source.type == QLatin1String("github"))
                url = QStringLiteral("https://github.com/%1/releases").arg(d->source.repo);
            if (!url.isEmpty())
                QDesktopServices::openUrl(QUrl(url));
        });
        QObject::connect(&em, &EmulatorManager::installProgress, this, [this](const QString& eid, const QString& stage, qint64 r, qint64 t) {
            if (eid != m_id)
                return;
            m_stage->setText(stage);
            m_stage->show();
            m_progress->show();
            m_progress->setRange(0, t > 0 ? 1000 : 0);
            if (t > 0)
                m_progress->setValue(int(r * 1000 / t));
        });
        refresh();
    }

    std::function<void(const QString&)> m_pageEditArgs;

    void refresh() {
        const EmulatorDef* e = Registry::instance().emulator(m_id);
        if (!e)
            return;
        const EmulatorStatus st = EmulatorManager::instance().status(m_id);
        // Pastille d'état.
        QString text;
        QColor c;
        if (st.busy) {
            text = QObject::tr("Installation…");
            c = Theme::Color::gold;
        } else if (!st.installed()) {
            text = QObject::tr("Non installé");
            c = Theme::Color::faint;
        } else if (st.updateAvailable) {
            text = QObject::tr("Mise à jour : %1").arg(st.latest);
            c = Theme::Color::gold;
        } else {
            text = st.version.isEmpty() ? QObject::tr("Prêt") : QObject::tr("Prêt, %1").arg(st.version);
            c = Theme::Color::teal;
        }
        m_pill->setText(text);
        m_pill->setStyleSheet(pill(text, c));

        if (st.installed()) {
            m_path->setText(QStringLiteral("<span style='color:%1'>%2</span> %3")
                                .arg(Theme::Color::muted.name(), QObject::tr("Programme :"), nativePath(st.exe).toHtmlEscaped()) +
                            (st.managed ? QStringLiteral(" <span style='color:%1'>%2</span>").arg(Theme::Color::faint.name(), QObject::tr("(géré par Playnest)")) : QString()));
        } else if (EmulatorManager::instance().detecting()) {
            m_path->setText(QStringLiteral("<span style='color:%1'>%2</span>").arg(Theme::Color::faint.name(), QObject::tr("Recherche sur ce PC…")));
        } else {
            m_path->setText(QStringLiteral("<span style='color:%1'>%2</span>")
                                .arg(Theme::Color::faint.name(),
                                     e->canAutoInstall() ? QObject::tr("Introuvable sur ce PC. « Installer » télécharge la version officielle.")
                                                         : QObject::tr("Introuvable sur ce PC. Télécharge-le sur son site officiel, puis « Choisir l'exécutable ».")));
        }
        const bool hasData = !st.dataDir.isEmpty() && QFileInfo(st.dataDir).isDir();
        m_data->setVisible(!e->dataDirs.isEmpty() && (st.installed() || hasData));
        m_data->setText(QStringLiteral("<span style='color:%1'>%2</span> %3%4")
                            .arg(Theme::Color::muted.name(), QObject::tr("Données :"), nativePath(st.dataDir).toHtmlEscaped(),
                                 hasData ? QString() : QStringLiteral(" <span style='color:%1'>%2</span>").arg(Theme::Color::faint.name(), QObject::tr("(créé au premier lancement)"))));

        // Fichiers système.
        while (QLayoutItem* it = m_filesRow->takeAt(0)) {
            delete it->widget();
            delete it;
        }
        const auto files = EmulatorManager::instance().systemFiles(m_id);
        for (int i = 0; i < files.size(); ++i) {
            const auto& f = files[i];
            const QColor fc = f.present ? Theme::Color::teal : (f.def.required ? Theme::Color::danger : Theme::Color::faint);
            auto* chip = new QLabel(QStringLiteral("%1 %2").arg(f.present ? QStringLiteral("✓") : QStringLiteral("✗"), f.def.label), this);
            chip->setStyleSheet(pill(f.def.label, fc));
            QString tip = nativePath(f.path);
            if (!f.def.help.isEmpty())
                tip += QStringLiteral("\n") + f.def.help;
            chip->setToolTip(tip);
            m_filesRow->addWidget(chip);
            if (f.def.importable && (st.installed() || hasData)) {
                auto* imp = new QToolButton(this);
                imp->setText(f.present ? QObject::tr("Remplacer…") : QObject::tr("Importer…"));
                imp->setCursor(Qt::PointingHandCursor);
                imp->setToolTip(QObject::tr("Copier ton fichier %1 au bon endroit (l'ancien est conservé en .bak)").arg(f.def.label));
                const QString id = m_id;
                const QString label = f.def.label;
                QObject::connect(imp, &QToolButton::clicked, this, [this, id, i, label] {
                    const QString src = QFileDialog::getOpenFileName(this, QObject::tr("Choisir %1").arg(label));
                    if (src.isEmpty())
                        return;
                    QString err;
                    if (EmulatorManager::instance().importSystemFile(id, i, src, &err))
                        emit m_page->message(QObject::tr("%1 importé.").arg(label), false);
                    else
                        QMessageBox::warning(this, QObject::tr("Import"), err);
                });
                m_filesRow->addWidget(imp);
            } else if (f.def.isDir && !f.present && !f.def.help.isEmpty()) {
                auto* help = new QLabel(f.def.help, this);
                help->setProperty("role", "faint");
                m_filesRow->addWidget(help);
            }
        }
        m_filesRow->addStretch();

        // Boutons.
        m_install->setVisible(e->canAutoInstall());
        m_install->setEnabled(!st.busy);
        if (!st.installed()) {
            m_install->setText(QObject::tr("Installer"));
            m_install->setIcon(Theme::icon(QStringLiteral("download"), Theme::onColor(Theme::Color::gold), 16));
            m_install->setStyleSheet(Theme::primaryButtonStyle(Theme::Color::gold, true));
        } else if (st.updateAvailable) {
            m_install->setText(QObject::tr("Mettre à jour vers %1").arg(st.latest));
            m_install->setIcon(Theme::icon(QStringLiteral("download"), Theme::onColor(Theme::Color::gold), 16));
            m_install->setStyleSheet(Theme::primaryButtonStyle(Theme::Color::gold, true));
        } else {
            m_install->setText(st.latest.isEmpty() ? QObject::tr("Réinstaller la dernière version") : QObject::tr("Réinstaller"));
            m_install->setIcon(Theme::icon(QStringLiteral("refresh"), Theme::Color::muted, 16));
            m_install->setStyleSheet(QString());
        }
        m_rollback->setVisible(st.canRollback && !st.busy);
        m_open->setEnabled(st.installed());
        if (!st.busy) {
            m_progress->hide();
            m_stage->hide();
        }
    }

private:
    QString m_id;
    EmulatorsPage* m_page;
    QLabel* m_pill;
    QLabel* m_path;
    QLabel* m_data;
    QHBoxLayout* m_filesRow;
    QLabel* m_stage;
    QProgressBar* m_progress;
    QPushButton* m_install;
    QPushButton* m_rollback;
    QPushButton* m_open;
};

// ===========================================================================
bool EmulatorsPage::installFromFile(QWidget* parent, const QString& file, const QString& expectedId) {
    auto& em = EmulatorManager::instance();
    const EmulatorManager::FileKind fk = em.classifyFile(file);
    const QString name = QFileInfo(file).fileName();
    QString found = fk.id;
    if (found.isEmpty() && !expectedId.isEmpty() && name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        found = expectedId; // exécutable au nom inhabituel choisi depuis la carte d'un émulateur
    if (found.isEmpty()) {
        QMessageBox::warning(parent, QObject::tr("Fichier non reconnu"),
                             QObject::tr("Playnest ne sait pas à quel émulateur correspond « %1 ». Utilise le bouton « Depuis un fichier… » "
                                         "sur la carte de l'émulateur concerné.").arg(name));
        return false;
    }
    if (!expectedId.isEmpty() && found != expectedId) {
        const EmulatorDef* a = Registry::instance().emulator(expectedId);
        const EmulatorDef* b = Registry::instance().emulator(found);
        if (QMessageBox::question(parent, QObject::tr("Autre émulateur"),
                                  QObject::tr("Ce fichier correspond à %1, pas à %2. L'utiliser pour %1 ?")
                                      .arg(b ? b->name : found, a ? a->name : expectedId)) != QMessageBox::Yes)
            return false;
    }
    const EmulatorDef* e = Registry::instance().emulator(found);
    const EmulatorStatus st = em.status(found);
    const QString current = st.version.isEmpty() ? QObject::tr("inconnue") : st.version;

    // Installateur officiel (ex. azahar-…-installer.exe) : on le lance, puis on suit la nouvelle version.
    if (fk.kind == EmulatorManager::FileKind::Installer) {
        if (em.isRunning(found)) {
            QMessageBox::warning(parent, e->name, QObject::tr("Ferme %1 avant de le mettre à jour.").arg(e->name));
            return false;
        }
        const QString text =
            QObject::tr("« %1 » est un installateur officiel pour %2.\n\n%3Playnest va ouvrir l'installateur : suis ses étapes "
                        "(Windows peut demander une autorisation). Quand il aura fini, Playnest utilisera automatiquement la nouvelle version.\n\n"
                        "Tes réglages et sauvegardes ne sont pas touchés. Avec un installateur, « Revenir à la version précédente » n'est pas disponible.")
                .arg(name, e->name, st.installed() ? QObject::tr("Version actuelle : %1. Une copie de tes sauvegardes est faite avant.\n\n").arg(current) : QString());
        if (QMessageBox::question(parent, QObject::tr("Installer %1").arg(e->name), text) != QMessageBox::Yes)
            return false;
        em.runInstaller(found, file);
        return true;
    }

    QString why;
    if (!em.canModify(found, &why)) {
        QMessageBox::warning(parent, e->name, why);
        return false;
    }
    const bool app = fk.kind == EmulatorManager::FileKind::App || name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive);
    if (app && !st.installed()) {
        QMessageBox::information(parent, e->name,
                                 QObject::tr("« %1 » est l'application seule. Pour une première installation, utilise plutôt « Installer » "
                                             "ou l'archive complète (.zip) : l'exécutable a besoin des fichiers qui l'accompagnent.").arg(name));
        return false;
    }
    const QString text = st.installed()
        ? QObject::tr("Mettre à jour %1 avec « %2 » ?\n\nVersion actuelle : %3.\n%4Tes réglages, clés, firmware et sauvegardes sont conservés, "
                      "une copie de tes sauvegardes est faite avant, et « Revenir à la version précédente » restera possible.")
              .arg(e->name, name, current,
                   app ? QObject::tr("Seul l'exécutable sera remplacé, les autres fichiers de %1 restent tels quels.\n").arg(e->name) : QString())
        : QObject::tr("Installer %1 depuis « %2 » ?").arg(e->name, name);
    if (QMessageBox::question(parent, st.installed() ? QObject::tr("Mettre à jour %1").arg(e->name) : QObject::tr("Installer %1").arg(e->name),
                              text) != QMessageBox::Yes)
        return false;
    em.installFromFile(found, file);
    return true;
}

EmulatorsPage::EmulatorsPage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; } QScrollArea > QWidget > QWidget { background: transparent; }"));
    outer->addWidget(scroll);
    auto* inner = new QWidget;
    scroll->setWidget(inner);
    auto* v = new QVBoxLayout(inner);
    v->setContentsMargins(30, 22, 30, 30);
    v->setSpacing(12);

    auto* head = new QHBoxLayout;
    auto* title = heading(tr("Émulateurs"), inner, 40);
    head->addWidget(title);
    head->addStretch();
    m_detect = new QPushButton(Theme::icon(QStringLiteral("search")), tr("Rechercher sur ce PC"), inner);
    m_check = new QPushButton(Theme::icon(QStringLiteral("refresh")), tr("Vérifier les mises à jour"), inner);
    m_updateAll = new QPushButton(Theme::icon(QStringLiteral("download"), Theme::onColor(Theme::Color::gold), 16), tr("Tout mettre à jour"), inner);
    m_updateAll->setStyleSheet(Theme::primaryButtonStyle(Theme::Color::gold, true));
    head->addWidget(m_detect);
    head->addWidget(m_check);
    head->addWidget(m_updateAll);
    v->addLayout(head);
    m_summary = muted(QString(), inner);
    m_summary->setFont(Theme::font(15, QFont::Medium));
    v->addWidget(m_summary);

    v->addSpacing(10);
    v->addWidget(heading(tr("Outils nécessaires"), inner));
    auto* toolsRow = new QHBoxLayout;
    toolsRow->setSpacing(12);
    for (const QString& id : {QStringLiteral("vcredist"), QStringLiteral("vulkan"), QStringLiteral("7zip")}) {
        auto* card = new ToolCard(id, inner);
        m_tools.insert(id, card);
        toolsRow->addWidget(card, 1);
    }
    v->addLayout(toolsRow);

    v->addSpacing(10);
    v->addWidget(heading(tr("Émulateur utilisé pour chaque console"), inner));
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(8);
    int r = 0;
    for (const auto& s : Registry::instance().systems()) {
        if (Registry::instance().emulatorsFor(s.id).isEmpty()) // jeux PC : pas d'émulateur
            continue;
        auto* bar = new QLabel(inner);
        bar->setFixedSize(4, 20);
        bar->setStyleSheet(QStringLiteral("background: %1; border-radius: 2px;").arg(s.color.name()));
        auto* name = new QLabel(s.name, inner);
        auto* combo = new QComboBox(inner);
        combo->setMinimumWidth(220);
        m_defaults.insert(s.id, combo);
        connect(combo, qOverload<int>(&QComboBox::activated), this, [combo, id = s.id](int) {
            const QString v = combo->currentData().toString();
            if (v.isEmpty())
                Settings::instance().remove(QStringLiteral("systems/default_") + id);
            else
                Settings::instance().setDefaultEmulator(id, v);
            Settings::instance().sync();
            emit EmulatorManager::instance().changed();
        });
        grid->addWidget(bar, r / 2, (r % 2) * 3);
        grid->addWidget(name, r / 2, (r % 2) * 3 + 1);
        grid->addWidget(combo, r / 2, (r % 2) * 3 + 2);
        ++r;
    }
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(5, 1);
    v->addLayout(grid);

    v->addSpacing(10);
    v->addWidget(heading(tr("Tes émulateurs"), inner));
    v->addWidget(muted(tr("Les mises à jour remplacent seulement les fichiers du programme : réglages, clés et sauvegardes restent "
                          "en place. Avant chaque mise à jour, tes sauvegardes sont copiées et les anciens fichiers sont gardés "
                          "pour pouvoir revenir en arrière."), inner));
    m_cardsBox = new QVBoxLayout;
    m_cardsBox->setSpacing(12);
    v->addLayout(m_cardsBox);
    for (const auto& e : Registry::instance().emulators()) {
        auto* card = new EmulatorCard(e, this);
        card->setParent(inner);
        card->m_pageEditArgs = [this](const QString& id) { editArguments(id); };
        m_cards.insert(e.id, card);
        m_cardsBox->addWidget(card);
    }
    v->addStretch();

    auto& em = EmulatorManager::instance();
    connect(m_detect, &QPushButton::clicked, this, [] { EmulatorManager::instance().startDetection(); });
    connect(m_check, &QPushButton::clicked, this, [] { EmulatorManager::instance().checkUpdates(true); });
    connect(m_updateAll, &QPushButton::clicked, this, [] {
        for (const auto& e : Registry::instance().emulators())
            if (EmulatorManager::instance().status(e.id).updateAvailable)
                EmulatorManager::instance().install(e.id);
    });
    connect(&em, &EmulatorManager::changed, this, &EmulatorsPage::refresh);
    connect(&em, &EmulatorManager::installFinished, this, [this](const QString&, bool ok, const QString& msg) { emit message(msg, !ok); });
    connect(&Tools::instance(), &Tools::changed, this, &EmulatorsPage::refresh);
    connect(&Tools::instance(), &Tools::installFinished, this, [this](const QString&, bool ok, const QString& msg) { emit message(msg, !ok); });
    refresh();
}

void EmulatorsPage::refresh() {
    auto& em = EmulatorManager::instance();
    for (auto* c : m_cards)
        c->refresh();
    // Émulateurs installés en premier, puis les autres, dans l'ordre du registre.
    int pos = 0;
    for (bool installed : {true, false})
        for (const auto& e : Registry::instance().emulators())
            if (em.isInstalled(e.id) == installed)
                if (EmulatorCard* c = m_cards.value(e.id)) {
                    if (m_cardsBox->indexOf(c) != pos) {
                        m_cardsBox->removeWidget(c);
                        m_cardsBox->insertWidget(pos, c);
                    }
                    ++pos;
                }
    for (auto* t : m_tools)
        t->refresh();
    for (auto it = m_defaults.begin(); it != m_defaults.end(); ++it) {
        QComboBox* combo = it.value();
        const QString sys = it.key();
        const QString chosen = Settings::instance().value(QStringLiteral("systems/default_") + sys).toString();
        const QString eff = em.effectiveDefault(sys);
        const EmulatorDef* effDef = Registry::instance().emulator(eff);
        combo->blockSignals(true);
        combo->clear();
        combo->addItem(tr("Automatique (%1)").arg(effDef ? effDef->name : tr("aucun")), QString());
        for (const EmulatorDef* e : Registry::instance().emulatorsFor(sys))
            combo->addItem(em.isInstalled(e->id) ? e->name : tr("%1 (non installé)").arg(e->name), e->id);
        combo->setCurrentIndex(chosen.isEmpty() ? 0 : qMax(0, combo->findData(chosen)));
        combo->blockSignals(false);
    }
    const int total = int(Registry::instance().emulators().size());
    const int ready = em.installedCount();
    const int updates = em.updatesAvailable();
    QString s = tr("%1 sur %2 émulateurs prêts").arg(ready).arg(total);
    if (em.detecting())
        s += tr(", recherche sur ce PC en cours…");
    if (updates > 0)
        s += tr(", %n mise(s) à jour disponible(s)", nullptr, updates);
    const QDateTime last = em.lastUpdateCheck();
    if (em.checkingUpdates())
        s += tr(". Vérification des mises à jour…");
    else if (last.isValid())
        s += tr(". Dernière vérification : %1.").arg(Lang::locale().toString(last, QObject::tr("d MMMM, HH:mm")));
    else
        s += QLatin1Char('.');
    m_summary->setText(s);
    m_updateAll->setVisible(updates > 1);
    m_detect->setEnabled(!em.detecting());
    m_check->setEnabled(!em.checkingUpdates());
}

void EmulatorsPage::editArguments(const QString& emuId) {
    const EmulatorDef* d = Registry::instance().emulator(emuId);
    if (!d)
        return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Arguments de %1").arg(d->name));
    dlg.resize(640, 280);
    auto* v = new QVBoxLayout(&dlg);
    v->addWidget(muted(tr("Jetons : {rom} chemin du jeu, {romdir} dossier du jeu, {romname} nom sans extension, {emudir} dossier "
                          "de l'émulateur. Laisse vide pour les arguments par défaut."), &dlg));
    auto* form = new QFormLayout;
    auto quoteJoin = [](const QStringList& l) {
        QStringList out;
        for (const auto& a : l)
            out << (a.contains(QLatin1Char(' ')) ? QStringLiteral("\"%1\"").arg(a) : a);
        return out.join(QLatin1Char(' '));
    };
    auto* fs = new QLineEdit(Settings::instance().customArgs(emuId, true), &dlg);
    fs->setPlaceholderText(quoteJoin(d->args));
    auto* win = new QLineEdit(Settings::instance().customArgs(emuId, false), &dlg);
    win->setPlaceholderText(quoteJoin(d->argsWindowed));
    form->addRow(tr("Plein écran"), fs);
    form->addRow(tr("Fenêtré"), win);
    v->addLayout(form);
    v->addStretch();
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel | QDialogButtonBox::Reset, &dlg);
    bb->button(QDialogButtonBox::Save)->setText(tr("Enregistrer"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    bb->button(QDialogButtonBox::Reset)->setText(tr("Valeurs par défaut"));
    connect(bb->button(QDialogButtonBox::Reset), &QPushButton::clicked, &dlg, [fs, win] {
        fs->clear();
        win->clear();
    });
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);
    if (dlg.exec() == QDialog::Accepted) {
        Settings::instance().setCustomArgs(emuId, true, fs->text());
        Settings::instance().setCustomArgs(emuId, false, win->text());
        Settings::instance().sync();
    }
}
