#include "Sidebar.h"

#include "../core/Library.h"
#include "../core/Registry.h"
#include "../core/Settings.h"
#include "Theme.h"

#include <QPainter>
#include <QStyledItemDelegate>

namespace {

enum Roles { IdRole = Qt::UserRole + 1, CountRole, HeaderRole, IconRole };

class SidebarDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    // Hauteur des lignes : réglée par la barre pour que toutes les consoles tiennent sans défiler.
    int rowHeight = 38;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& idx) const override {
        return idx.data(HeaderRole).toBool() ? QSize(200, rowHeight + 2) : QSize(200, rowHeight);
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF r = opt.rect;
        if (idx.data(HeaderRole).toBool()) {
            p->setPen(Theme::Color::faint);
            p->setFont(Theme::font(13, QFont::Medium));
            p->drawText(r.adjusted(20, r.height() > 34 ? 12 : 6, -12, 0), Qt::AlignLeft | Qt::AlignVCenter, idx.data(Qt::DisplayRole).toString());
            p->restore();
            return;
        }
        const QString id = idx.data(IdRole).toString();
        const QColor c = Theme::categoryColor(id);
        const bool sel = opt.state & QStyle::State_Selected;
        const bool hov = opt.state & QStyle::State_MouseOver;
        const int count = idx.data(CountRole).toInt();
        const bool isSystem = !id.startsWith(QLatin1String("__"));
        const bool empty = isSystem && count == 0;

        const QRectF row = r.adjusted(10, r.height() > 32 ? 2 : 1, -10, r.height() > 32 ? -2 : -1);
        if (sel) {
            QColor bg = c;
            bg.setAlpha(38);
            p->setPen(Qt::NoPen);
            p->setBrush(bg);
            p->drawRoundedRect(row, 6, 6);
        } else if (hov) {
            p->setPen(Qt::NoPen);
            p->setBrush(Theme::Color::raised);
            p->drawRoundedRect(row, 6, 6);
        }
        // Tranche colorée : pleine hauteur si sélectionnée.
        const qreal barH = sel ? row.height() - 10 : qMin<qreal>(14, row.height() - 12);
        p->setPen(Qt::NoPen);
        p->setBrush(empty ? Theme::Color::line : c);
        p->drawRoundedRect(QRectF(row.left() + 8, row.center().y() - barH / 2, 4, barH), 2, 2);

        const QString iconName = idx.data(IconRole).toString();
        qreal textLeft = row.left() + 22;
        if (!iconName.isEmpty()) {
            Theme::paintIcon(*p, iconName, QRectF(textLeft, row.center().y() - 9, 18, 18), sel ? c : Theme::Color::muted);
            textLeft += 26;
        }
        p->setFont(Theme::font(15, sel ? QFont::DemiBold : QFont::Medium));
        p->setPen(empty ? Theme::Color::faint : (sel ? Theme::Color::text : Theme::Color::text.darker(115)));
        p->drawText(QRectF(textLeft, row.top(), row.width() - (textLeft - row.left()) - 40, row.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, idx.data(Qt::DisplayRole).toString());
        if (count > 0) {
            p->setFont(Theme::font(13, QFont::Medium));
            p->setPen(sel ? c : Theme::Color::faint);
            p->drawText(QRectF(row.right() - 46, row.top(), 38, row.height()), Qt::AlignRight | Qt::AlignVCenter,
                        QString::number(count));
        }
        p->restore();
    }
};

} // namespace

Sidebar::Sidebar(Library* lib, QWidget* parent) : QListWidget(parent), m_lib(lib) {
    setItemDelegate(new SidebarDelegate(this));
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; } QListWidget::item { border: none; }"));
    rebuild();
    connect(this, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* cur) {
        if (cur && !cur->data(HeaderRole).toBool())
            emit categoryChanged(cur->data(IdRole).toString());
    });
}

void Sidebar::rebuild() {
    const QString keep = currentCategory();
    blockSignals(true);
    clear();
    auto add = [this](const QString& id, const QString& label, const QString& icon) {
        auto* it = new QListWidgetItem(label, this);
        it->setData(IdRole, id);
        it->setData(IconRole, icon);
    };
    add(QStringLiteral("__home"), tr("Accueil"), QStringLiteral("home"));
    add(QStringLiteral("__all"), tr("Tous les jeux"), QStringLiteral("grid"));
    add(QStringLiteral("__fav"), tr("Favoris"), QStringLiteral("star"));
    add(QStringLiteral("__recent"), tr("Joués récemment"), QStringLiteral("clock"));
    add(QStringLiteral("__addons"), tr("DLC et mises à jour"), QStringLiteral("layers"));
    auto* header = new QListWidgetItem(tr("Consoles"), this);
    header->setData(HeaderRole, true);
    header->setFlags(Qt::NoItemFlags);
    for (const auto& s : Registry::instance().systems())
        add(s.id, s.name, QString());
    blockSignals(false);
    refreshCounts();
    fitRows();
    selectCategory(keep.isEmpty() ? QStringLiteral("__home") : keep); // on démarre toujours sur l'accueil
}

// Toutes les consoles restent visibles : la hauteur des lignes s'adapte à celle de la fenêtre.
void Sidebar::fitRows() {
    auto* d = static_cast<SidebarDelegate*>(itemDelegate());
    const int rows = qMax(1, count());
    const int h = qBound(22, (viewport()->height() - 6) / rows, 40);
    if (h == d->rowHeight)
        return;
    d->rowHeight = h;
    scheduleDelayedItemsLayout();
}

void Sidebar::resizeEvent(QResizeEvent* e) {
    QListWidget::resizeEvent(e);
    fitRows();
}

void Sidebar::refreshCounts() {
    const bool addons = !Settings::instance().hideAddons();
    for (int i = 0; i < count(); ++i) {
        QListWidgetItem* it = item(i);
        if (it->data(HeaderRole).toBool())
            continue;
        const QString id = it->data(IdRole).toString();
        const int n = id == QLatin1String("__home") ? 0 : id == QLatin1String("__addons") ? m_lib->addonCount() : m_lib->countForSystem(id, addons);
        it->setData(CountRole, n);
    }
    viewport()->update();
}

QString Sidebar::currentCategory() const {
    const QListWidgetItem* it = currentItem();
    return it ? it->data(IdRole).toString() : QString();
}

void Sidebar::selectCategory(const QString& id) {
    for (int i = 0; i < count(); ++i) {
        if (item(i)->data(IdRole).toString() == id) {
            setCurrentRow(i);
            return;
        }
    }
    setCurrentRow(0);
}

void Sidebar::step(int delta) {
    int row = currentRow();
    for (int n = 0; n < count(); ++n) {
        row = (row + delta + count()) % count();
        QListWidgetItem* it = item(row);
        if (it->data(HeaderRole).toBool())
            continue;
        // On saute les consoles sans jeu pour une navigation rapide à la manette.
        const QString id = it->data(IdRole).toString();
        if (!id.startsWith(QLatin1String("__")) && it->data(CountRole).toInt() == 0)
            continue;
        setCurrentRow(row);
        return;
    }
}
