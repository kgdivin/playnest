#include "SettingsPage.h"

#include "../core/AppInfo.h"
#include "../core/Emulators.h"
#include "../core/Lang.h"
#include "../core/Paths.h"
#include "../core/Registry.h"
#include "../core/SelfUpdate.h"
#include "../core/Settings.h"
#include "Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QLabel* heading(const QString& text, QWidget* parent, int px = 20) {
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
QWidget* scrollWrap(QWidget* inner) {
    auto* sa = new QScrollArea;
    sa->setWidget(inner);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; } QScrollArea > QWidget > QWidget { background: transparent; }"));
    return sa;
}
QVBoxLayout* page(QWidget* w) {
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(6, 18, 18, 12);
    v->setSpacing(10);
    return v;
}
} // namespace

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(30, 22, 30, 16);
    v->setSpacing(8);
    v->addWidget(heading(tr("Paramètres"), this, 40));
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(scrollWrap(buildLibraryTab()), tr("Bibliothèque"));
    m_tabs->addTab(scrollWrap(buildLaunchTab()), tr("Lancement et affichage"));
    m_tabs->addTab(scrollWrap(buildUpdatesTab()), tr("Mises à jour"));
    m_tabs->addTab(scrollWrap(buildRequirementsTab()), tr("Prérequis par console"));
    m_tabs->addTab(scrollWrap(buildAboutTab()), tr("À propos"));
    v->addWidget(m_tabs, 1);
}

void SettingsPage::showTab(int index) { m_tabs->setCurrentIndex(index); }

// ---------------------------------------------------------------------------
QWidget* SettingsPage::buildLibraryTab() {
    auto* w = new QWidget;
    auto* v = page(w);
    v->addWidget(heading(tr("Dossiers de jeux"), w));
    v->addWidget(muted(tr("Playnest parcourt ces dossiers et leurs sous-dossiers, puis reconnaît chaque jeu grâce à son en-tête. "
                          "Les jeux Wii U décryptés (dossiers code/content/meta) et les ExeFS Switch extraits sont aussi reconnus. "
                          "Après le premier passage, seuls les fichiers nouveaux ou modifiés sont relus."), w));
    m_folders = new QListWidget(w);
    for (const auto& f : Settings::instance().romFolders())
        m_folders->addItem(QDir::toNativeSeparators(f));
    m_folders->setMinimumHeight(150);
    v->addWidget(m_folders);
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(Theme::icon(QStringLiteral("plus")), tr("Ajouter un dossier…"), w);
    auto* rem = new QPushButton(tr("Retirer"), w);
    row->addWidget(add);
    row->addWidget(rem);
    row->addStretch();
    v->addLayout(row);
    connect(add, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Choisir un dossier de jeux"));
        if (d.isEmpty())
            return;
        m_folders->addItem(QDir::toNativeSeparators(d));
        saveFolders();
    });
    connect(rem, &QPushButton::clicked, this, [this] {
        delete m_folders->currentItem();
        saveFolders();
    });

    auto* scan = new QCheckBox(tr("Rechercher les nouveaux jeux au démarrage"), w);
    scan->setChecked(Settings::instance().scanOnStartup());
    connect(scan, &QCheckBox::toggled, this, [](bool on) { Settings::instance().setScanOnStartup(on); });
    v->addWidget(scan);
    auto* addons = new QCheckBox(tr("Masquer les mises à jour et DLC (Switch, 3DS, Wii U)"), w);
    addons->setChecked(Settings::instance().hideAddons());
    connect(addons, &QCheckBox::toggled, this, [this](bool on) {
        Settings::instance().setHideAddons(on);
        emit appearanceChanged();
    });
    v->addWidget(addons);

    v->addSpacing(14);
    v->addWidget(heading(tr("Où chercher tes émulateurs"), w));
    v->addWidget(muted(tr("Playnest cherche déjà dans son propre dossier, le Bureau, les Téléchargements, les Documents, Program Files "
                          "et la racine de chaque disque. Ajoute ici un dossier précis si tes émulateurs sont ailleurs."), w));
    m_emuFolders = new QListWidget(w);
    m_emuFolders->setMaximumHeight(110);
    for (const auto& f : Settings::instance().extraEmulatorFolders())
        m_emuFolders->addItem(QDir::toNativeSeparators(f));
    v->addWidget(m_emuFolders);
    auto* er = new QHBoxLayout;
    auto* addE = new QPushButton(Theme::icon(QStringLiteral("plus")), tr("Ajouter un dossier…"), w);
    auto* remE = new QPushButton(tr("Retirer"), w);
    er->addWidget(addE);
    er->addWidget(remE);
    er->addStretch();
    v->addLayout(er);
    auto saveExtra = [this] {
        QStringList l;
        for (int i = 0; i < m_emuFolders->count(); ++i)
            l << QDir::fromNativeSeparators(m_emuFolders->item(i)->text());
        Settings::instance().setExtraEmulatorFolders(l);
        Settings::instance().sync();
        EmulatorManager::instance().startDetection();
    };
    connect(addE, &QPushButton::clicked, this, [this, saveExtra] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Dossier contenant des émulateurs"));
        if (!d.isEmpty()) {
            m_emuFolders->addItem(QDir::toNativeSeparators(d));
            saveExtra();
        }
    });
    connect(remE, &QPushButton::clicked, this, [this, saveExtra] {
        delete m_emuFolders->currentItem();
        saveExtra();
    });
    v->addStretch();
    return w;
}

void SettingsPage::saveFolders() {
    QStringList l;
    for (int i = 0; i < m_folders->count(); ++i)
        l << QDir::fromNativeSeparators(m_folders->item(i)->text());
    l.removeDuplicates();
    Settings::instance().setRomFolders(l);
    Settings::instance().sync();
    emit foldersChanged();
}

// ---------------------------------------------------------------------------
QWidget* SettingsPage::buildLaunchTab() {
    auto* w = new QWidget;
    auto* v = page(w);
    auto& s = Settings::instance();
    v->addWidget(heading(tr("Langue"), w));
    auto* uiRow = new QHBoxLayout;
    auto* uiLang = new QComboBox(w);
    uiLang->addItem(tr("Automatique (langue de Windows)"), QStringLiteral("auto"));
    uiLang->addItem(QStringLiteral("Français"), QStringLiteral("fr"));
    uiLang->addItem(QStringLiteral("English"), QStringLiteral("en"));
    uiLang->setCurrentIndex(qMax(0, uiLang->findData(Lang::setting())));
    uiRow->addWidget(uiLang);
    uiRow->addStretch();
    v->addLayout(uiRow);
    v->addWidget(muted(tr("Playnest se relance tout de suite dans la langue choisie. Le bouton à côté du logo fait la même chose."), w));
    connect(uiLang, &QComboBox::currentIndexChanged, this, [this, uiLang] { emit languageChosen(uiLang->currentData().toString()); });

    v->addSpacing(10);
    v->addWidget(heading(tr("Lancement des jeux"), w));
    auto* fs = new QCheckBox(tr("Lancer les jeux en plein écran"), w);
    fs->setChecked(s.launchFullscreen());
    connect(fs, &QCheckBox::toggled, this, [](bool on) { Settings::instance().setLaunchFullscreen(on); });
    v->addWidget(fs);
    auto* min = new QCheckBox(tr("Réduire Playnest pendant que tu joues, puis le rouvrir à la fermeture du jeu"), w);
    min->setChecked(s.minimizeWhileRunning());
    connect(min, &QCheckBox::toggled, this, [](bool on) { Settings::instance().setMinimizeWhileRunning(on); });
    v->addWidget(min);

    v->addSpacing(10);
    v->addWidget(heading(tr("Manette"), w));
    auto* pad = new QCheckBox(tr("Naviguer dans Playnest avec une manette"), w);
    pad->setChecked(s.gamepadEnabled());
    connect(pad, &QCheckBox::toggled, this, [this](bool on) {
        Settings::instance().setGamepadEnabled(on);
        emit appearanceChanged();
    });
    v->addWidget(pad);
    v->addWidget(muted(tr("Croix ou stick : se déplacer. A : jouer ou valider. B : retour. LB et RB : console précédente ou suivante. "
                          "Y : favori. X : menu du jeu. Start : plein écran. Gâchettes : page précédente ou suivante. Les manettes "
                          "Xbox et compatibles XInput sont reconnues ; une manette PlayStation fonctionne avec DS4Windows ou Steam Input."), w));

    v->addSpacing(10);
    v->addWidget(heading(tr("Jaquettes"), w));
    auto* covers = new QCheckBox(tr("Télécharger automatiquement les jaquettes manquantes"), w);
    covers->setChecked(s.downloadCovers());
    connect(covers, &QCheckBox::toggled, this, [](bool on) { Settings::instance().setDownloadCovers(on); });
    v->addWidget(covers);
    auto* langRow = new QHBoxLayout;
    langRow->addWidget(new QLabel(tr("Langue préférée des jaquettes PAL"), w));
    auto* lang = new QComboBox(w);
    const QList<QPair<QString, QString>> langs = {{"FR", tr("Français")}, {"EN", tr("Anglais")}, {"DE", tr("Allemand")},
                                                   {"ES", tr("Espagnol")}, {"IT", tr("Italien")}, {"NL", tr("Néerlandais")}};
    for (const auto& l : langs)
        lang->addItem(l.second, l.first);
    lang->setCurrentIndex(qMax(0, lang->findData(s.coverLanguage())));
    connect(lang, qOverload<int>(&QComboBox::activated), this, [lang](int) { Settings::instance().setCoverLanguage(lang->currentData().toString()); });
    langRow->addWidget(lang);
    langRow->addStretch();
    v->addLayout(langRow);
    v->addWidget(muted(tr("Sources : GameTDB (GameCube, Wii, Wii U, DS, 3DS), libretro-thumbnails (NES, Super Nintendo, Game Boy, N64 et autres, d'après le nom "
                          "No-Intro du fichier) et le dépôt boxart d'Eden (Switch). Clic droit sur un jeu pour choisir ta propre image."), w));
    v->addStretch();
    return w;
}

// ---------------------------------------------------------------------------
QWidget* SettingsPage::buildUpdatesTab() {
    auto* w = new QWidget;
    auto* v = page(w);
    auto& s = Settings::instance();
    v->addWidget(heading(tr("Émulateurs"), w));
    auto* daily = new QCheckBox(tr("Vérifier chaque jour si une nouvelle version de mes émulateurs est sortie"), w);
    daily->setChecked(s.value(QStringLiteral("updates/autoCheck"), true).toBool());
    connect(daily, &QCheckBox::toggled, this, [](bool on) { Settings::instance().setValue(QStringLiteral("updates/autoCheck"), on); });
    v->addWidget(daily);
    auto* pre = new QCheckBox(tr("Proposer aussi les versions de test (rc, nightly), plus récentes mais moins stables"), w);
    pre->setChecked(s.value(QStringLiteral("updates/prereleases"), false).toBool());
    connect(pre, &QCheckBox::toggled, this, [](bool on) {
        Settings::instance().setValue(QStringLiteral("updates/prereleases"), on);
        EmulatorManager::instance().checkUpdates(true);
    });
    v->addWidget(pre);
    v->addWidget(muted(tr("Les mises à jour ne sont jamais installées sans ton accord. Elles remplacent seulement les fichiers du "
                          "programme ; une copie de tes sauvegardes est faite avant, et l'ancienne version reste disponible "
                          "avec « Revenir à la version précédente »."), w));

    v->addSpacing(14);
    v->addWidget(heading(QString::fromLatin1(AppInfo::kName), w));
    v->addWidget(muted(tr("Version installée : %1. Playnest cherche ses mises à jour dans les « Releases » de ce dépôt GitHub. "
                          "Change-le seulement si tu publies ta propre version (« ton-pseudo/playnest »).")
                           .arg(QCoreApplication::applicationVersion()), w));
    auto* repoRow = new QHBoxLayout;
    auto* repo = new QLineEdit(SelfUpdate::instance().repo(), w);
    repo->setPlaceholderText(tr("propriétaire/dépôt"));
    repo->setMinimumWidth(320);
    auto* check = new QPushButton(Theme::icon(QStringLiteral("refresh")), tr("Vérifier maintenant"), w);
    repoRow->addWidget(repo);
    repoRow->addWidget(check);
    repoRow->addStretch();
    v->addLayout(repoRow);
    auto* startup = new QCheckBox(tr("Vérifier au démarrage"), w);
    startup->setChecked(s.value(QStringLiteral("app/autoUpdateCheck"), true).toBool());
    connect(startup, &QCheckBox::toggled, this, [](bool on) { Settings::instance().setValue(QStringLiteral("app/autoUpdateCheck"), on); });
    v->addWidget(startup);
    m_appUpdateStatus = muted(QString(), w);
    v->addWidget(m_appUpdateStatus);
    m_appUpdateApply = new QPushButton(Theme::icon(QStringLiteral("download"), Theme::onColor(Theme::Color::gold), 16), QString(), w);
    m_appUpdateApply->setStyleSheet(Theme::primaryButtonStyle(Theme::Color::gold, true));
    m_appUpdateApply->hide();
    v->addWidget(m_appUpdateApply, 0, Qt::AlignLeft);
    connect(repo, &QLineEdit::editingFinished, this, [repo] { SelfUpdate::instance().setRepo(repo->text()); });
    connect(check, &QPushButton::clicked, this, [this, repo] {
        SelfUpdate::instance().setRepo(repo->text());
        m_appUpdateStatus->setText(tr("Vérification…"));
        SelfUpdate::instance().check(true);
    });
    connect(&SelfUpdate::instance(), &SelfUpdate::checked, this, [this](bool available, const QString& version, const QString& err) {
        if (!err.isEmpty())
            m_appUpdateStatus->setText(err);
        else if (available)
            m_appUpdateStatus->setText(tr("La version %1 est disponible.").arg(version));
        else
            m_appUpdateStatus->setText(version.isEmpty() ? QString() : tr("Playnest est à jour (dernière version : %1).").arg(version));
        m_appUpdateApply->setVisible(available);
        m_appUpdateApply->setText(tr("Installer la version %1 et redémarrer").arg(version));
    });
    connect(&SelfUpdate::instance(), &SelfUpdate::progress, this, [this](const QString& stage, qint64, qint64) { m_appUpdateStatus->setText(stage); });
    connect(&SelfUpdate::instance(), &SelfUpdate::failed, this, [this](const QString& msg) {
        m_appUpdateStatus->setText(msg);
        emit message(msg, true);
    });
    connect(m_appUpdateApply, &QPushButton::clicked, this, [] { SelfUpdate::instance().apply(); });
    v->addStretch();
    return w;
}

// ---------------------------------------------------------------------------
QWidget* SettingsPage::buildRequirementsTab() {
    auto* w = new QWidget;
    auto* v = page(w);
    v->addWidget(muted(tr("Playnest ne fournit ni jeux, ni BIOS, ni clés. Utilise des copies de tes propres jeux et les fichiers système "
                          "extraits de tes propres consoles. La page Émulateurs indique pour chacun si ses fichiers sont présents."), w));
    for (const auto& s : Registry::instance().systems()) {
        v->addSpacing(8);
        auto* row = new QHBoxLayout;
        auto* bar = new QLabel(w);
        bar->setFixedSize(4, 22);
        bar->setStyleSheet(QStringLiteral("background: %1; border-radius: 2px;").arg(s.color.name()));
        row->addWidget(bar);
        auto* h = new QLabel(s.name, w);
        h->setFont(Theme::font(18, QFont::DemiBold, true));
        row->addWidget(h);
        QStringList exts;
        for (const auto& e : s.extensions)
            exts << QStringLiteral(".") + e;
        row->addSpacing(8);
        row->addWidget(muted(exts.join(QLatin1Char(' ')), w), 1);
        v->addLayout(row);
        auto* req = new QLabel(s.requirements, w);
        req->setWordWrap(true);
        req->setContentsMargins(14, 0, 0, 0);
        v->addWidget(req);
    }
    v->addStretch();
    return w;
}

// ---------------------------------------------------------------------------
QWidget* SettingsPage::buildAboutTab() {
    auto* w = new QWidget;
    auto* v = page(w);
    auto* title = new QLabel(QString::fromLatin1(AppInfo::kName), w);
    title->setFont(Theme::font(44, QFont::Bold, true));
    v->addWidget(title);
    v->addWidget(muted(tr("Version %1. Tous tes jeux Nintendo dans une seule application, de la NES à la Switch 2. "
                          "Playnest ne contient aucun code d'émulation : il pilote des émulateurs open source indépendants, chacun "
                          "sous sa propre licence.").arg(QCoreApplication::applicationVersion()), w));
    v->addSpacing(8);
    v->addWidget(heading(tr("Partager Playnest"), w));
    v->addWidget(muted(tr("Pour l'envoyer à un ami, donne-lui le fichier zip de Playnest, ou une copie du dossier de l'application. "
                          "Tes jeux, tes réglages et tes sauvegardes ne sont pas dedans : ils restent sur ton PC, et ton ami démarre "
                          "avec l'écran de bienvenue."), w));
    auto* shareRow = new QHBoxLayout;
    auto* appFolder = new QPushButton(Theme::icon(QStringLiteral("folder")), tr("Ouvrir le dossier de l'application"), w);
    connect(appFolder, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::appDir())); });
    shareRow->addWidget(appFolder);
    shareRow->addStretch();
    v->addLayout(shareRow);
    v->addSpacing(8);
    auto* data = new QLabel(tr("Dossier de données%1 : %2")
                                .arg(Paths::isPortable() ? tr(" (mode portable)") : QString(), QDir::toNativeSeparators(Paths::dataDir())), w);
    data->setWordWrap(true);
    data->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(data);
    auto* row = new QHBoxLayout;
    auto* open = new QPushButton(Theme::icon(QStringLiteral("folder")), tr("Ouvrir le dossier de données"), w);
    connect(open, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::dataDir())); });
    auto* log = new QPushButton(tr("Ouvrir le journal"), w);
    connect(log, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::logFile())); });
    row->addWidget(open);
    row->addWidget(log);
    row->addStretch();
    v->addLayout(row);
    v->addWidget(muted(tr("Pour modifier les consoles ou les émulateurs sans recompiler, copie systems.json ou emulators.json (dossier "
                          "resources du code source) dans le dossier de données et modifie-les, puis redémarre Playnest."), w));
    v->addSpacing(8);
    v->addWidget(muted(tr("Polices Barlow et Barlow Semi Condensed : SIL Open Font License 1.1. Interface : Qt 6 (LGPLv3). Jaquettes : "
                          "GameTDB, libretro-thumbnails, Eden boxart. 7-Zip (7zr.exe, LGPL) et le runtime Visual C++ sont téléchargés "
                          "depuis leurs sites officiels, uniquement à ta demande."), w));
    v->addStretch();
    return w;
}
