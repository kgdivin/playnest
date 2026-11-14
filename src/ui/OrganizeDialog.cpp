#include "OrganizeDialog.h"

#include "../core/Archive.h"
#include "Theme.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QColor kindColor(Organizer::Kind k) {
    switch (k) {
    case Organizer::Game: return Theme::Color::gold;
    case Organizer::Update: return Theme::Color::teal;
    case Organizer::Dlc: return Theme::Color::lavender;
    case Organizer::Emulator: return Theme::Color::pink;
    case Organizer::Save: return Theme::Color::muted;
    }
    return Theme::Color::text;
}
QLabel* heading(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setFont(Theme::font(30, QFont::Bold, true));
    return l;
}
} // namespace

OrganizeDialog::OrganizeDialog(Organizer* organizer, QWidget* parent) : QDialog(parent), m_org(organizer) {
    setWindowTitle(tr("Ranger mes fichiers"));
    resize(860, 680);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 24, 28, 22);
    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack);

    // --- Page 1 : aperçu ----------------------------------------------------
    auto* prep = new QWidget(m_stack);
    auto* v = new QVBoxLayout(prep);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(12);
    v->addWidget(heading(tr("Ranger mes fichiers"), prep));
    auto* intro = new QLabel(tr("Playnest sépare tes jeux, leurs mises à jour, leurs DLC, tes émulateurs et tes copies de "
                                "sauvegardes dans des dossiers distincts. Tes jeux restent dans la bibliothèque avec leur "
                                "temps de jeu et leurs favoris, et tu peux tout remettre comme avant."), prep);
    intro->setWordWrap(true);
    intro->setProperty("role", "muted");
    v->addWidget(intro);

    // Dernier rangement : on peut encore l'annuler.
    const QString last = Organizer::lastJournal();
    if (!last.isEmpty()) {
        auto* lastRow = new QHBoxLayout;
        auto* lastText = new QLabel(Organizer::describeJournal(last), prep);
        lastText->setWordWrap(true);
        lastText->setProperty("role", "muted");
        auto* undoLast = new QPushButton(tr("Tout remettre comme avant"), prep);
        lastRow->addWidget(lastText, 1);
        lastRow->addWidget(undoLast);
        v->addLayout(lastRow);
        connect(undoLast, &QPushButton::clicked, this, [this, last] {
            if (QMessageBox::question(this, tr("Annuler le rangement"),
                                      tr("Remettre chaque fichier à l'endroit où il était avant ce rangement ?")) != QMessageBox::Yes)
                return;
            m_runTitle->setText(tr("Retour à l'état d'origine"));
            m_stop->hide();
            m_stack->setCurrentIndex(1);
            m_org->undo(last);
        });
    }

    auto* rootRow = new QHBoxLayout;
    auto* rootLabel = new QLabel(tr("Ranger dans"), prep);
    m_root = new QLineEdit(Organizer::defaultRoot(), prep);
    m_root->setMinimumHeight(36);
    auto* browse = new QPushButton(tr("Changer…"), prep);
    rootRow->addWidget(rootLabel);
    rootRow->addWidget(m_root, 1);
    rootRow->addWidget(browse);
    v->addLayout(rootRow);

    auto* opts = new QHBoxLayout;
    m_games = new QCheckBox(tr("Jeux, mises à jour et DLC"), prep);
    m_emus = new QCheckBox(tr("Émulateurs"), prep);
    m_saves = new QCheckBox(tr("Copies de sauvegardes"), prep);
    for (QCheckBox* c : {m_games, m_emus, m_saves}) {
        c->setChecked(true);
        opts->addWidget(c);
    }
    opts->addStretch();
    v->addLayout(opts);

    m_tree = new QTreeWidget(prep);
    m_tree->setHeaderLabels({tr("Nouvel emplacement"), tr("Vient de"), tr("Taille")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->setColumnWidth(1, 230);
    v->addWidget(m_tree, 1);

    m_warnings = new QLabel(prep);
    m_warnings->setWordWrap(true);
    m_warnings->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::Color::gold.name()));
    v->addWidget(m_warnings);

    auto* foot = new QHBoxLayout;
    m_summary = new QLabel(prep);
    m_summary->setWordWrap(true);
    foot->addWidget(m_summary, 1);
    m_cancel = new QPushButton(tr("Fermer"), prep);
    m_go = new QPushButton(Theme::icon(QStringLiteral("tidy"), Theme::onColor(Theme::Color::gold), 18), tr("Ranger"), prep);
    m_go->setStyleSheet(Theme::primaryButtonStyle(Theme::Color::gold));
    m_go->setCursor(Qt::PointingHandCursor);
    m_go->setMinimumWidth(150);
    foot->addWidget(m_cancel);
    foot->addWidget(m_go);
    v->addLayout(foot);
    m_stack->addWidget(prep);

    // --- Page 2 : rangement en cours ----------------------------------------
    auto* run = new QWidget(m_stack);
    auto* rv = new QVBoxLayout(run);
    rv->addStretch(2);
    m_runTitle = heading(tr("Rangement en cours"), run);
    rv->addWidget(m_runTitle);
    m_runFile = new QLabel(run);
    m_runFile->setProperty("role", "muted");
    rv->addWidget(m_runFile);
    m_bar = new QProgressBar(run);
    m_bar->setRange(0, 1000);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(8);
    rv->addSpacing(8);
    rv->addWidget(m_bar);
    auto* stopRow = new QHBoxLayout;
    stopRow->addStretch();
    m_stop = new QPushButton(tr("Arrêter après ce fichier"), run);
    stopRow->addWidget(m_stop);
    rv->addSpacing(10);
    rv->addLayout(stopRow);
    rv->addStretch(3);
    m_stack->addWidget(run);

    // --- Page 3 : terminé ---------------------------------------------------
    auto* done = new QWidget(m_stack);
    auto* dv = new QVBoxLayout(done);
    dv->addStretch(2);
    dv->addWidget(heading(tr("Terminé"), done));
    m_doneText = new QLabel(done);
    m_doneText->setWordWrap(true);
    m_doneText->setFont(Theme::font(16));
    dv->addWidget(m_doneText);
    auto* doneRow = new QHBoxLayout;
    auto* openRoot = new QPushButton(Theme::icon(QStringLiteral("folder")), tr("Ouvrir le dossier"), done);
    m_undo = new QPushButton(tr("Tout remettre comme avant"), done);
    auto* close = new QPushButton(tr("Fermer"), done);
    close->setStyleSheet(Theme::primaryButtonStyle(Theme::Color::gold, true));
    doneRow->addWidget(openRoot);
    doneRow->addWidget(m_undo);
    doneRow->addStretch();
    doneRow->addWidget(close);
    dv->addSpacing(12);
    dv->addLayout(doneRow);
    dv->addStretch(3);
    m_stack->addWidget(done);

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Où ranger tes fichiers ?"), m_root->text());
        if (!d.isEmpty()) {
            m_root->setText(QDir::toNativeSeparators(d));
            rebuildPlan();
        }
    });
    connect(m_root, &QLineEdit::editingFinished, this, &OrganizeDialog::rebuildPlan);
    for (QCheckBox* c : {m_games, m_emus, m_saves})
        connect(c, &QCheckBox::toggled, this, &OrganizeDialog::rebuildPlan);
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_go, &QPushButton::clicked, this, &OrganizeDialog::start);
    connect(m_stop, &QPushButton::clicked, this, [this] {
        m_org->cancel();
        m_stop->setEnabled(false);
        m_stop->setText(tr("Arrêt…"));
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(openRoot, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_root->text()));
    });
    connect(m_undo, &QPushButton::clicked, this, [this] {
        if (m_journal.isEmpty())
            return;
        m_runTitle->setText(tr("Retour à l'état d'origine"));
        m_stop->hide();
        m_stack->setCurrentIndex(1);
        m_org->undo(m_journal);
        m_journal.clear();
    });
    connect(m_org, &Organizer::progress, this, [this](const QString& name, qint64 done, qint64 total) {
        m_runFile->setText(name);
        m_bar->setValue(int(qMin<qint64>(1000, done * 1000 / qMax<qint64>(1, total))));
    });
    connect(m_org, &Organizer::finished, this, &OrganizeDialog::onFinished);

    rebuildPlan();
}

void OrganizeDialog::rebuildPlan() {
    Organizer::Options o;
    o.root = m_root->text().trimmed();
    o.games = m_games->isChecked();
    o.emulators = m_emus->isChecked();
    o.saves = m_saves->isChecked();
    m_tree->clear();
    if (o.root.isEmpty() || !QDir::isAbsolutePath(o.root)) {
        m_summary->setText(tr("Choisis un dossier."));
        m_go->setEnabled(false);
        return;
    }
    m_plan = m_org->plan(o);

    // Arbre : Jeux > Switch > fichiers, etc.
    QMap<int, QTreeWidgetItem*> kinds;
    QMap<QString, QTreeWidgetItem*> groups;
    QMap<int, qint64> kindBytes;
    for (const auto& m : m_plan.moves) {
        QTreeWidgetItem*& k = kinds[int(m.kind)];
        if (!k) {
            k = new QTreeWidgetItem(m_tree);
            k->setForeground(0, kindColor(m.kind));
            k->setFont(0, Theme::font(15, QFont::DemiBold));
            k->setIcon(0, Theme::icon(m.kind == Organizer::Emulator ? QStringLiteral("gamepad")
                                      : m.kind == Organizer::Save   ? QStringLiteral("save")
                                      : m.kind == Organizer::Game   ? QStringLiteral("grid")
                                                                    : QStringLiteral("layers"),
                                      kindColor(m.kind), 18));
        }
        kindBytes[int(m.kind)] += m.size;
        QTreeWidgetItem* parent = k;
        if (m.kind != Organizer::Emulator && m.kind != Organizer::Save) {
            QTreeWidgetItem*& g = groups[QString::number(int(m.kind)) + m.group];
            if (!g) {
                g = new QTreeWidgetItem(k);
                g->setText(0, m.group);
            }
            parent = g;
        }
        auto* it = new QTreeWidgetItem(parent);
        it->setText(0, m.label);
        it->setToolTip(0, QDir::toNativeSeparators(m.to));
        it->setText(1, QFileInfo(m.from).dir().dirName());
        it->setToolTip(1, QDir::toNativeSeparators(m.from));
        it->setText(2, Archive::formatSize(m.size));
        it->setForeground(1, Theme::Color::muted);
        it->setForeground(2, Theme::Color::muted);
    }
    for (auto it = kinds.begin(); it != kinds.end(); ++it) {
        const auto kind = Organizer::Kind(it.key());
        it.value()->setText(0, QStringLiteral("%1   (%2)").arg(Organizer::kindFolder(kind)).arg(it.value()->childCount() == 0 ? 0 : [&] {
            int n = 0;
            for (const auto& m : m_plan.moves)
                n += m.kind == kind ? 1 : 0;
            return n;
        }()));
        it.value()->setText(2, Archive::formatSize(kindBytes.value(it.key())));
        it.value()->setExpanded(kind != Organizer::Save);
        for (int i = 0; i < it.value()->childCount(); ++i)
            it.value()->child(i)->setExpanded(true);
    }

    const int n = m_plan.moves.size();
    QString sum;
    if (n == 0)
        sum = m_plan.alreadyInPlace > 0 ? tr("Tout est déjà rangé.") : tr("Rien à ranger.");
    else if (m_plan.copyBytes == 0)
        sum = tr("%n élément(s), %1. Même disque : le déplacement est instantané.", nullptr, n).arg(Archive::formatSize(m_plan.totalBytes));
    else
        sum = tr("%n élément(s), %1, dont %2 à copier vers un autre disque.", nullptr, n)
                  .arg(Archive::formatSize(m_plan.totalBytes), Archive::formatSize(m_plan.copyBytes));
    if (m_plan.alreadyInPlace > 0 && n > 0)
        sum += tr(" %n déjà en place.", nullptr, m_plan.alreadyInPlace);
    m_summary->setText(sum);
    m_warnings->setText(m_plan.warnings.join(QLatin1Char('\n')));
    m_warnings->setVisible(!m_plan.warnings.isEmpty());
    bool noSpace = false;
    for (const QString& w : m_plan.warnings)
        noSpace |= w.contains(tr("Pas assez de place"));
    m_go->setEnabled(n > 0 && !noSpace && !m_org->isRunning());
}

void OrganizeDialog::start() {
    if (m_plan.moves.isEmpty())
        return;
    m_runTitle->setText(tr("Rangement en cours"));
    m_stop->show();
    m_stop->setEnabled(true);
    m_stop->setText(tr("Arrêter après ce fichier"));
    m_bar->setValue(0);
    m_stack->setCurrentIndex(1);
    m_org->run(m_plan);
}

void OrganizeDialog::onFinished(const Organizer::Result& r) {
    if (!r.journal.isEmpty())
        m_journal = r.journal;
    m_doneText->setText(r.message);
    m_undo->setVisible(!m_journal.isEmpty());
    m_stack->setCurrentIndex(2);
}
