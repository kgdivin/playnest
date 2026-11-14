#include "../core/Lang.h"
#include "SavesPage.h"

#include "../core/Paths.h"
#include "../core/Registry.h"
#include "../core/Saves.h"
#include "../core/Settings.h"
#include "Theme.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
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
QString fmtSize(qint64 b) { return b < 0 ? QStringLiteral("…") : Lang::locale().formattedDataSize(b, 1, QLocale::DataSizeTraditionalFormat); }
} // namespace

SavesPage::SavesPage(QWidget* parent) : QWidget(parent) {
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
    head->addWidget(heading(tr("Sauvegardes"), inner, 40));
    head->addStretch();
    auto* open = new QPushButton(Theme::icon(QStringLiteral("folder")), tr("Ouvrir le dossier des copies"), inner);
    m_backup = new QPushButton(Theme::icon(QStringLiteral("download"), Theme::onColor(Theme::Color::teal), 16), tr("Copier mes sauvegardes maintenant"), inner);
    m_backup->setStyleSheet(Theme::primaryButtonStyle(Theme::Color::teal, true));
    head->addWidget(open);
    head->addWidget(m_backup);
    v->addLayout(head);
    v->addWidget(muted(tr("Playnest copie les sauvegardes de tes jeux dans une archive datée (%1). Tes émulateurs ne sont jamais "
                          "modifiés par une copie. Une restauration remet les fichiers de la copie en place, après avoir copié "
                          "l'état actuel par sécurité.").arg(QDir::toNativeSeparators(Paths::backupsDir())), inner));
    m_status = muted(QString(), inner);
    m_status->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(Theme::Color::teal.name()));
    v->addWidget(m_status);

    v->addSpacing(8);
    v->addWidget(heading(tr("Où sont tes sauvegardes"), inner));
    auto* sourcesFrame = new QFrame(inner);
    sourcesFrame->setObjectName(QStringLiteral("srcFrame"));
    sourcesFrame->setStyleSheet(QStringLiteral("#srcFrame { background: %1; border: 1px solid %2; border-radius: 10px; }")
                                    .arg(Theme::Color::panel.name(), Theme::Color::line.name()));
    m_sourcesBox = new QVBoxLayout(sourcesFrame);
    m_sourcesBox->setContentsMargins(16, 12, 16, 12);
    m_sourcesBox->setSpacing(10);
    v->addWidget(sourcesFrame);

    v->addSpacing(8);
    v->addWidget(heading(tr("Copies de sécurité"), inner));
    m_table = new QTableWidget(0, 4, inner);
    m_table->setHorizontalHeaderLabels({tr("Date"), tr("Contenu"), tr("Taille"), tr("Origine")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setMinimumHeight(240);
    v->addWidget(m_table);
    auto* tb = new QHBoxLayout;
    m_restore = new QPushButton(tr("Restaurer cette copie…"), inner);
    m_delete = new QPushButton(tr("Supprimer"), inner);
    tb->addWidget(m_restore);
    tb->addWidget(m_delete);
    tb->addStretch();
    v->addLayout(tb);

    v->addSpacing(8);
    v->addWidget(heading(tr("Copies automatiques"), inner));
    auto& s = Settings::instance();
    auto* before = new QCheckBox(tr("Copier mes sauvegardes avant chaque mise à jour d'émulateur"), inner);
    before->setChecked(s.value(QStringLiteral("saves/autoBeforeUpdate"), true).toBool());
    connect(before, &QCheckBox::toggled, this, [](bool on) { Settings::instance().setValue(QStringLiteral("saves/autoBeforeUpdate"), on); });
    v->addWidget(before);
    auto* weekly = new QCheckBox(tr("Faire une copie automatique chaque semaine"), inner);
    weekly->setChecked(s.value(QStringLiteral("saves/weekly"), true).toBool());
    connect(weekly, &QCheckBox::toggled, this, [](bool on) { Settings::instance().setValue(QStringLiteral("saves/weekly"), on); });
    v->addWidget(weekly);
    auto* keepRow = new QHBoxLayout;
    keepRow->addWidget(new QLabel(tr("Garder les"), inner));
    auto* keep = new QSpinBox(inner);
    keep->setRange(2, 100);
    keep->setButtonSymbols(QAbstractSpinBox::NoButtons);
    keep->setFixedWidth(64);
    keep->setAlignment(Qt::AlignCenter);
    keep->setValue(s.value(QStringLiteral("saves/keep"), 10).toInt());
    connect(keep, qOverload<int>(&QSpinBox::valueChanged), this, [](int n) { Settings::instance().setValue(QStringLiteral("saves/keep"), n); });
    keepRow->addWidget(keep);
    keepRow->addWidget(new QLabel(tr("dernières copies automatiques (les copies manuelles ne sont jamais supprimées)"), inner));
    keepRow->addStretch();
    v->addLayout(keepRow);
    v->addStretch();

    auto& sm = SaveManager::instance();
    connect(open, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::backupsDir())); });
    connect(m_backup, &QPushButton::clicked, this, [] { SaveManager::instance().backupNow({}, tr("Copie manuelle"), false); });
    connect(m_restore, &QPushButton::clicked, this, &SavesPage::restoreSelected);
    connect(m_delete, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        const auto& list = SaveManager::instance().backups();
        if (row < 0 || row >= list.size())
            return;
        if (QMessageBox::question(this, tr("Supprimer la copie"), tr("Supprimer définitivement cette copie de sécurité ?")) == QMessageBox::Yes)
            SaveManager::instance().remove(list[row].file);
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        m_restore->setEnabled(m_table->currentRow() >= 0);
        m_delete->setEnabled(m_table->currentRow() >= 0);
    });
    connect(&sm, &SaveManager::changed, this, &SavesPage::refresh);
    connect(&sm, &SaveManager::progress, this, [this](const QString& msg) { m_status->setText(msg); });
    connect(&sm, &SaveManager::finished, this, [this](bool ok, const QString& msg) {
        m_status->setText(msg);
        m_status->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg((ok ? Theme::Color::teal : Theme::Color::danger).name()));
        emit message(msg, !ok);
    });
    refresh();
}

void SavesPage::activate() {
    SaveManager::instance().reload();
    SaveManager::instance().computeSizes();
}

void SavesPage::refresh() {
    auto& sm = SaveManager::instance();
    m_backup->setEnabled(!sm.busy());
    m_restore->setEnabled(!sm.busy() && m_table->currentRow() >= 0);

    while (QLayoutItem* it = m_sourcesBox->takeAt(0)) {
        if (it->layout()) {
            while (QLayoutItem* sub = it->layout()->takeAt(0)) {
                delete sub->widget();
                delete sub;
            }
        }
        delete it->widget();
        delete it;
    }
    const auto sources = sm.sources();
    if (sources.isEmpty()) {
        m_sourcesBox->addWidget(muted(tr("Aucune sauvegarde trouvée pour l'instant. Elles apparaîtront ici dès que tu auras joué "
                                         "avec un émulateur installé."), this));
    }
    for (const SaveSource& s : sources) {
        auto* row = new QHBoxLayout;
        const QString firstEmu = s.emuIds.value(0);
        auto* bar = new QLabel(this);
        bar->setFixedSize(4, 34);
        QColor c = Theme::Color::muted;
        if (!firstEmu.isEmpty()) {
            if (const EmulatorDef* e = Registry::instance().emulator(firstEmu))
                c = Theme::categoryColor(e->systems.value(0));
        }
        bar->setStyleSheet(QStringLiteral("background: %1; border-radius: 2px;").arg(c.name()));
        row->addWidget(bar);
        auto* text = new QLabel(this);
        text->setTextFormat(Qt::RichText);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        const QString where = s.root.isEmpty() ? tr("%n fichier(s) à côté de tes jeux", nullptr, int(s.paths.size()))
                                               : QDir::toNativeSeparators(s.root);
        text->setText(QStringLiteral("<b>%1</b><br><span style='color:%2'>%3</span>")
                          .arg(s.name.toHtmlEscaped(), Theme::Color::muted.name(), where.toHtmlEscaped()));
        row->addWidget(text, 1);
        auto* sz = new QLabel(fmtSize(sm.sizes().value(s.key, -1)), this);
        sz->setProperty("role", "muted");
        row->addWidget(sz);
        auto* btn = new QPushButton(tr("Copier"), this);
        const QString key = s.key;
        connect(btn, &QPushButton::clicked, this, [key, name = s.name] {
            SaveManager::instance().backupNow({key}, tr("Copie manuelle : %1").arg(name), false);
        });
        btn->setEnabled(!sm.busy());
        row->addWidget(btn);
        m_sourcesBox->addLayout(row);
    }

    const auto& list = sm.backups();
    m_table->setRowCount(int(list.size()));
    for (int i = 0; i < list.size(); ++i) {
        const auto& b = list[i];
        m_table->setItem(i, 0, new QTableWidgetItem(Lang::locale().toString(b.created, QObject::tr("d MMM yyyy, HH:mm"))));
        m_table->setItem(i, 1, new QTableWidgetItem(b.names.join(QStringLiteral(", "))));
        m_table->setItem(i, 2, new QTableWidgetItem(fmtSize(b.size)));
        m_table->setItem(i, 3, new QTableWidgetItem(b.reason.isEmpty() ? (b.automatic ? tr("Automatique") : tr("Manuelle")) : b.reason));
    }
    m_table->setVisible(!list.isEmpty());
    m_restore->setVisible(!list.isEmpty());
    m_delete->setVisible(!list.isEmpty());
}

void SavesPage::restoreSelected() {
    const int row = m_table->currentRow();
    const auto& list = SaveManager::instance().backups();
    if (row < 0 || row >= list.size())
        return;
    const auto b = list[row];
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Restaurer une copie"));
    dlg.resize(560, 320);
    auto* v = new QVBoxLayout(&dlg);
    v->addWidget(muted(tr("Copie du %1. Choisis ce qu'il faut remettre en place. L'état actuel sera d'abord copié par sécurité, "
                          "et les fichiers qui ne sont pas dans la copie ne seront pas supprimés.")
                           .arg(Lang::locale().toString(b.created, QObject::tr("d MMMM yyyy à HH:mm"))), &dlg));
    QList<QCheckBox*> boxes;
    for (int i = 0; i < b.keys.size(); ++i) {
        auto* cb = new QCheckBox(b.names.value(i), &dlg);
        cb->setChecked(true);
        cb->setProperty("key", b.keys[i]);
        boxes << cb;
        v->addWidget(cb);
    }
    v->addWidget(muted(tr("Ferme les émulateurs concernés avant de restaurer."), &dlg));
    v->addStretch();
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Restaurer"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QStringList keys;
    for (auto* cb : boxes)
        if (cb->isChecked())
            keys << cb->property("key").toString();
    if (!keys.isEmpty())
        SaveManager::instance().restore(b.file, keys);
}
