#include "WelcomeDialog.h"

#include "../core/AppInfo.h"
#include "../core/Lang.h"
#include "../core/Organizer.h"
#include "../core/Paths.h"
#include "../core/Settings.h"
#include "Theme.h"

#include <QButtonGroup>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
QLabel* title(QWidget* parent) {
    auto* l = new QLabel(parent);
    l->setFont(Theme::font(34, QFont::Bold, true));
    l->setWordWrap(true);
    return l;
}
QLabel* text(QWidget* parent, int px = 16, bool muted = true) {
    auto* l = new QLabel(parent);
    l->setFont(Theme::font(px));
    l->setWordWrap(true);
    if (muted)
        l->setProperty("role", "muted");
    return l;
}
QString langButtonStyle() {
    return QStringLiteral(
               "QPushButton { background: %1; border: 2px solid %2; border-radius: 12px; padding: 14px 26px; font-size: 18px; font-weight: 600; }"
               "QPushButton:hover { border-color: %3; }"
               "QPushButton:checked { border-color: %4; background: %5; color: %4; }")
        .arg(Theme::Color::panel.name(), Theme::Color::line.name(), Theme::Color::muted.name(), Theme::Color::gold.name(),
             Theme::Color::raised.name());
}
} // namespace

bool WelcomeDialog::needed() {
    return !Settings::instance().value(QStringLiteral("app/welcomed"), false).toBool() && !Settings::instance().firstRunDone() &&
           Settings::instance().romFolders().isEmpty() && !QFileInfo::exists(Paths::libraryFile());
}

WelcomeDialog::WelcomeDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QString::fromLatin1(AppInfo::kName));
    setModal(true);
    setFixedSize(760, 540);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(44, 36, 44, 28);
    root->setSpacing(18);
    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    // --- 1. Bienvenue et langue
    auto* p1 = new QWidget(this);
    auto* v1 = new QVBoxLayout(p1);
    v1->setContentsMargins(0, 0, 0, 0);
    v1->setSpacing(12);
    auto* logo = new QLabel(p1);
    logo->setPixmap(Theme::logoPixmap(112, devicePixelRatioF()));
    v1->addWidget(logo, 0, Qt::AlignHCenter);
    v1->addSpacing(6);
    m_hello = title(p1);
    m_hello->setAlignment(Qt::AlignHCenter);
    v1->addWidget(m_hello);
    m_tagline = text(p1, 17);
    m_tagline->setAlignment(Qt::AlignHCenter);
    v1->addWidget(m_tagline);
    v1->addStretch(1);
    m_langLabel = text(p1, 15);
    m_langLabel->setAlignment(Qt::AlignHCenter);
    v1->addWidget(m_langLabel);
    auto* langs = new QHBoxLayout;
    langs->setSpacing(14);
    m_fr = new QPushButton(QStringLiteral("Français"), p1);
    m_en = new QPushButton(QStringLiteral("English"), p1);
    auto* group = new QButtonGroup(this);
    for (QPushButton* b : {m_fr, m_en}) {
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(langButtonStyle());
        b->setMinimumWidth(190);
        group->addButton(b);
    }
    langs->addStretch();
    langs->addWidget(m_fr);
    langs->addWidget(m_en);
    langs->addStretch();
    v1->addLayout(langs);
    v1->addStretch(1);
    (Lang::current() == QLatin1String("en") ? m_en : m_fr)->setChecked(true);
    connect(m_fr, &QPushButton::clicked, this, [this] { Lang::setLanguage(QStringLiteral("fr")); retranslate(); });
    connect(m_en, &QPushButton::clicked, this, [this] { Lang::setLanguage(QStringLiteral("en")); retranslate(); });
    m_stack->addWidget(p1);

    // --- 2. Dossier de jeux
    auto* p2 = new QWidget(this);
    auto* v2 = new QVBoxLayout(p2);
    v2->setContentsMargins(0, 0, 0, 0);
    v2->setSpacing(8);
    m_whereTitle = title(p2);
    v2->addWidget(m_whereTitle);
    m_whereText = text(p2);
    v2->addWidget(m_whereText);
    v2->addSpacing(12);
    m_create = new QRadioButton(p2);
    m_existing = new QRadioButton(p2);
    m_later = new QRadioButton(p2);
    for (QRadioButton* r : {m_create, m_existing, m_later})
        r->setFont(Theme::font(18, QFont::DemiBold));
    m_createHint = text(p2, 15);
    m_existingHint = text(p2, 15);
    m_createHint->setContentsMargins(30, 0, 0, 0);
    m_existingHint->setContentsMargins(30, 0, 0, 0);
    v2->addWidget(m_create);
    v2->addWidget(m_createHint);
    v2->addSpacing(22);
    auto* row = new QHBoxLayout;
    row->addWidget(m_existing, 1);
    m_browse = new QPushButton(Theme::icon(QStringLiteral("folder")), QString(), p2);
    row->addWidget(m_browse);
    v2->addLayout(row);
    v2->addWidget(m_existingHint);
    v2->addSpacing(22);
    v2->addWidget(m_later);
    v2->addStretch(1);
    m_create->setChecked(true);
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Choisir un dossier de jeux"), m_picked.isEmpty() ? QDir::homePath() : m_picked);
        if (d.isEmpty())
            return;
        m_picked = QDir::toNativeSeparators(d);
        m_existing->setChecked(true);
        retranslate();
    });
    m_stack->addWidget(p2);

    // --- 3. Comment ça marche
    auto* p3 = new QWidget(this);
    auto* v3 = new QVBoxLayout(p3);
    v3->setContentsMargins(0, 0, 0, 0);
    v3->setSpacing(10);
    m_howTitle = title(p3);
    v3->addWidget(m_howTitle);
    v3->addSpacing(20);
    const char* icons[3] = {"plus", "play", "save"};
    const QColor colors[3] = {Theme::Color::gold, Theme::Color::teal, Theme::Color::lavender};
    for (int i = 0; i < 3; ++i) {
        auto* line = new QHBoxLayout;
        line->setSpacing(18);
        auto* ic = new QLabel(p3);
        ic->setPixmap(Theme::icon(QString::fromLatin1(icons[i]), colors[i], 30).pixmap(30, 30));
        ic->setFixedSize(52, 52);
        ic->setAlignment(Qt::AlignCenter);
        ic->setStyleSheet(QStringLiteral("background: %1; border-radius: 26px;").arg(Theme::Color::raised.name()));
        line->addWidget(ic, 0, Qt::AlignTop);
        auto* col = new QVBoxLayout;
        col->setSpacing(2);
        m_how[i][0] = text(p3, 19, false);
        m_how[i][0]->setFont(Theme::font(19, QFont::DemiBold));
        m_how[i][1] = text(p3, 15);
        col->addWidget(m_how[i][0]);
        col->addWidget(m_how[i][1]);
        line->addLayout(col, 1);
        v3->addLayout(line);
        v3->addSpacing(20);
    }
    v3->addStretch(1);
    m_legal = text(p3, 13);
    v3->addWidget(m_legal);
    m_stack->addWidget(p3);

    // --- Boutons
    auto* bar = new QHBoxLayout;
    m_step = text(this, 14);
    m_back = new QPushButton(this);
    m_next = new QPushButton(this);
    m_next->setObjectName(QStringLiteral("welcomeNext"));
    m_next->setStyleSheet(Theme::primaryButtonStyle(Theme::Color::gold));
    m_next->setMinimumWidth(170);
    m_next->setDefault(true);
    m_next->setCursor(Qt::PointingHandCursor);
    bar->addWidget(m_step);
    bar->addStretch();
    bar->addWidget(m_back);
    bar->addWidget(m_next);
    root->addLayout(bar);
    connect(m_back, &QPushButton::clicked, this, [this] { go(m_stack->currentIndex() - 1); });
    connect(m_next, &QPushButton::clicked, this, [this] {
        if (m_stack->currentIndex() == m_stack->count() - 1)
            finish();
        else
            go(m_stack->currentIndex() + 1);
    });
    go(0);
}

void WelcomeDialog::go(int page) {
    m_stack->setCurrentIndex(qBound(0, page, m_stack->count() - 1));
    retranslate();
}

// Tous les textes sont posés ici : le choix de la langue s'applique tout de suite.
void WelcomeDialog::retranslate() {
    const int page = m_stack->currentIndex();
    m_hello->setText(tr("Bienvenue dans Playnest"));
    m_tagline->setText(tr("Tous tes jeux Nintendo dans une seule application, de la NES à la Switch 2.\n"
                          "Playnest installe les émulateurs, range tes jeux et protège tes sauvegardes."));
    m_langLabel->setText(tr("Choisis ta langue"));

    const QString root = Organizer::defaultRoot();
    m_whereTitle->setText(tr("Où sont tes jeux ?"));
    m_whereText->setText(tr("Playnest parcourt ce dossier et reconnaît chaque jeu tout seul. Tu pourras en ajouter d'autres plus tard."));
    m_create->setText(tr("Créer mon dossier de jeux (conseillé)"));
    m_createHint->setText(tr("Playnest crée « %1 ». Tu glisses ensuite tes jeux sur la fenêtre et ils sont rangés par console.").arg(root));
    m_existing->setText(tr("J'ai déjà un dossier de jeux"));
    m_existingHint->setText(m_picked.isEmpty() ? tr("Tes jeux restent où ils sont. Les sous-dossiers sont parcourus aussi.") : m_picked);
    m_browse->setText(tr("Choisir…"));
    m_later->setText(tr("Plus tard"));

    m_howTitle->setText(tr("Trois choses à savoir"));
    m_how[0][0]->setText(tr("Glisse tes jeux sur la fenêtre"));
    m_how[0][1]->setText(tr("Fichiers ou dossiers entiers : chaque jeu est reconnu, rangé par console et reçoit sa jaquette."));
    m_how[1][0]->setText(tr("Double-clique pour jouer"));
    m_how[1][1]->setText(tr("S'il manque l'émulateur, Playnest le télécharge depuis son site officiel et le tient à jour."));
    m_how[2][0]->setText(tr("Tes sauvegardes sont à l'abri"));
    m_how[2][1]->setText(tr("Elles sont copiées chaque semaine et avant chaque mise à jour d'émulateur."));
    m_legal->setText(tr("Playnest ne fournit ni jeux, ni clés, ni BIOS : utilise ceux de tes propres consoles et cartouches."));

    m_step->setText(tr("Étape %1 sur %2").arg(page + 1).arg(m_stack->count()));
    m_back->setText(tr("Retour"));
    m_back->setVisible(page > 0);
    m_next->setText(page == m_stack->count() - 1 ? tr("C'est parti") : tr("Continuer"));
}

void WelcomeDialog::finish() {
    auto& s = Settings::instance();
    if (m_create->isChecked()) {
        // Le dossier naît déjà rangé (« Jeux », « Mises à jour », « DLC ») : les jeux glissés y vont directement.
        const QString root = QDir::fromNativeSeparators(Organizer::defaultRoot());
        s.setValue(QStringLiteral("organize/lang"), Lang::current());
        for (auto k : {Organizer::Game, Organizer::Update, Organizer::Dlc})
            QDir().mkpath(root + QLatin1Char('/') + Organizer::kindFolder(k));
        s.setValue(QStringLiteral("organize/root"), QDir::toNativeSeparators(root));
        m_folder = QDir::toNativeSeparators(root);
    } else if (m_existing->isChecked() && !m_picked.isEmpty()) {
        m_folder = m_picked;
    }
    if (!m_folder.isEmpty())
        s.setRomFolders({m_folder});
    s.setValue(QStringLiteral("app/welcomed"), true);
    s.sync();
    accept();
}
