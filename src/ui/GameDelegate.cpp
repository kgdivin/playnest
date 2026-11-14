#include "GameDelegate.h"

#include "../core/Library.h"
#include "../core/Registry.h"
#include "Theme.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QPointer>
#include <QTextLayout>
#include <QtConcurrent/QtConcurrentRun>

// ===========================================================================
// ImageCache
// ===========================================================================
ImageCache& ImageCache::instance() {
    static ImageCache c;
    return c;
}

ImageCache::ImageCache() { QPixmapCache::setCacheLimit(256 * 1024); }

QString ImageCache::key(const QString& file, const QSize& box, qreal dpr) const {
    return QStringLiteral("%1|%2x%3|%4|%5").arg(file).arg(box.width()).arg(box.height()).arg(dpr).arg(m_stamp.value(file));
}

void ImageCache::invalidate(const QString& file) {
    m_stamp[file] = QDateTime::currentMSecsSinceEpoch();
    m_failed.remove(file);
}

QPixmap ImageCache::get(const QString& file, const QSize& box, qreal dpr, bool pixelArt) {
    if (file.isEmpty() || m_failed.contains(file))
        return {};
    const QString k = key(file, box, dpr);
    QPixmap pm;
    if (QPixmapCache::find(k, &pm))
        return pm;
    if (m_pending.contains(k))
        return {};
    m_pending.insert(k);
    const QSize target = box * dpr;
    auto future = QtConcurrent::run([file, target, pixelArt]() -> QImage {
        QImageReader r(file);
        r.setAutoTransform(true);
        QImage img = r.read();
        if (img.isNull())
            return img;
        if (pixelArt) {
            // Agrandissement entier sans lissage (icônes DS 32×32).
            const int factor = qMax(1, qMin(target.width() / img.width(), target.height() / img.height()));
            return img.scaled(img.size() * factor, Qt::KeepAspectRatio, Qt::FastTransformation);
        }
        return img.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    });
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, k, file, dpr] {
        const QImage img = watcher->result();
        watcher->deleteLater();
        m_pending.remove(k);
        if (img.isNull()) {
            m_failed.insert(file);
            return;
        }
        QPixmap pm = QPixmap::fromImage(img);
        pm.setDevicePixelRatio(dpr);
        QPixmapCache::insert(k, pm);
        emit loaded();
    });
    watcher->setFuture(future);
    return {};
}

// ===========================================================================
// GameDelegate
// ===========================================================================
namespace {

QStringList wrapLines(const QString& text, const QFont& font, qreal width, int maxLines) {
    QStringList out;
    QTextLayout layout(text, font);
    layout.beginLayout();
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
        if (out.size() == maxLines - 1) {
            const QString rest = text.mid(line.textStart());
            out << QFontMetrics(font).elidedText(rest.simplified(), Qt::ElideRight, int(width));
            break;
        }
        out << text.mid(line.textStart(), line.textLength()).trimmed();
    }
    layout.endLayout();
    return out;
}

QString formatPlaytime(qint64 secs) {
    if (secs < 60)
        return {};
    if (secs < 3600)
        return QObject::tr("%1 min").arg(secs / 60);
    return QObject::tr("%1 h %2").arg(secs / 3600).arg((secs % 3600) / 60, 2, 10, QLatin1Char('0'));
}

} // namespace

GameDelegate::GameDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize GameDelegate::cardSize() const {
    const int coverH = int(m_cardWidth * 4.0 / 3.0);
    return {m_cardWidth + kSpacing, coverH + 64 + kSpacing};
}

QSize GameDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const { return cardSize(); }

void GameDelegate::paintLabel(QPainter* p, const QRectF& r, const QString& systemId, const QString& title,
                              const QPixmap& icon, bool large) {
    // Étiquette générée : dégradé de la console, fines rayures de cartouche, icône du jeu ou monogramme.
    const QColor base = Theme::categoryColor(systemId);
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0, base.darker(150));
    g.setColorAt(1, base.darker(360));
    p->fillRect(r, g);
    p->setPen(QColor(255, 255, 255, 16));
    for (qreal y = r.top() + r.height() * 0.08; y < r.top() + r.height() * 0.16; y += 4)
        p->drawLine(QPointF(r.left() + 12, y), QPointF(r.right() - 12, y));

    const SystemInfo* sys = Registry::instance().system(systemId);
    const qreal pad = large ? 22 : 12;
    p->setPen(QColor(255, 255, 255, 150));
    p->setFont(Theme::font(large ? 16 : 12, QFont::DemiBold, true));
    p->drawText(QRectF(r.left() + pad, r.top() + pad * 0.7, r.width() - 2 * pad, 20), Qt::AlignLeft | Qt::AlignTop,
                sys ? sys->name : systemId);

    if (!icon.isNull()) {
        // Icône sur une tuile arrondie, légèrement ombrée.
        const QSizeF isz = icon.deviceIndependentSize();
        const QRectF tile(r.center().x() - isz.width() / 2, r.top() + r.height() * 0.48 - isz.height() / 2, isz.width(), isz.height());
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0, 0, 0, 70));
        p->drawRoundedRect(tile.adjusted(-2, 4, 2, 8), 14, 14);
        QPainterPath clip;
        clip.addRoundedRect(tile, isz.width() * 0.12, isz.width() * 0.12);
        p->save();
        p->setClipPath(clip, Qt::IntersectClip);
        p->drawPixmap(tile.topLeft(), icon);
        p->restore();
    } else {
        // Monogramme : initiale du titre, grande et discrète.
        QString mono;
        for (const QChar c : title)
            if (c.isLetterOrNumber()) {
                mono = c.toUpper();
                break;
            }
        p->setPen(QColor(255, 255, 255, 46));
        p->setFont(Theme::font(int(r.height() * 0.44), QFont::Bold, true));
        p->drawText(r.adjusted(0, r.height() * 0.06, 0, 0), Qt::AlignCenter, mono);
    }
    if (large) { // grande étiquette (panneau de détails) : le titre y figure aussi
        const QFont tf = Theme::font(28, QFont::Bold, true);
        const QStringList lines = wrapLines(title, tf, r.width() - 2 * pad, 3);
        const qreal lh = QFontMetricsF(tf).height() * 0.98;
        qreal y = r.bottom() - pad - lh * lines.size();
        p->setFont(tf);
        p->setPen(Qt::white);
        for (const auto& l : lines) {
            p->drawText(QRectF(r.left() + pad, y, r.width() - 2 * pad, lh), Qt::AlignLeft | Qt::AlignVCenter, l);
            y += lh;
        }
    }
}

namespace {
// Petite pastille sur la jaquette.
qreal drawChip(QPainter* p, qreal x, qreal bottom, const QString& text, const QColor& accent) {
    const QFont f = Theme::font(12, QFont::DemiBold);
    const qreal w = QFontMetricsF(f).horizontalAdvance(text) + 24;
    const QRectF chip(x, bottom - 22, w, 20);
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(16, 18, 30, 215));
    p->drawRoundedRect(chip, 10, 10);
    p->setBrush(accent);
    p->drawEllipse(QPointF(chip.left() + 9, chip.center().y()), 3, 3);
    p->setFont(f);
    p->setPen(Theme::Color::text);
    p->drawText(chip.adjusted(16, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, text);
    return w;
}
} // namespace

void GameDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::SmoothPixmapTransform);

    const QString sysId = idx.data(LibraryModel::SystemRole).toString();
    const QColor sysColor = Theme::categoryColor(sysId);
    const QString title = idx.data(LibraryModel::TitleRole).toString();
    const QString image = idx.data(LibraryModel::ImageRole).toString();
    const QString path = idx.data(LibraryModel::PathRole).toString();
    const bool selected = opt.state & QStyle::State_Selected;
    const bool hover = opt.state & QStyle::State_MouseOver;
    const bool missing = idx.data(LibraryModel::MissingRole).toBool();
    const bool addon = idx.data(LibraryModel::AddonRole).toBool();
    const bool update = idx.data(LibraryModel::AddonKindRole).toString() == QLatin1String("update");

    // La cellule peut être plus large que la carte (grille justifiée) : on centre la carte.
    const QRectF r = opt.rect;
    const QRectF cell(r.center().x() - m_cardWidth / 2.0, r.top() + kSpacing / 2.0, m_cardWidth, r.height() - kSpacing);
    const qreal coverH = m_cardWidth * 4.0 / 3.0;
    const QRectF cover(cell.left(), cell.top(), cell.width(), coverH);
    const qreal dpr = p->device() ? p->device()->devicePixelRatioF() : 1.0;

    // Ombre portée, plus marquée pour la carte sélectionnée.
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0, 0, 0, selected ? 110 : 60));
    p->drawRoundedRect(cover.adjusted(1, selected ? 8 : 5, -1, selected ? 10 : 6), 8, 8);

    // Contour de sélection dans la couleur de la console.
    if (selected) {
        p->setPen(QPen(sysColor, 3));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(cover.adjusted(-5, -5, 5, 5), 9, 9);
    } else if (hover) {
        p->setPen(QPen(Theme::Color::line.lighter(140), 1.5));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(cover.adjusted(-4, -4, 4, 4), 8, 8);
    }

    QPainterPath clip;
    clip.addRoundedRect(cover, 6, 6);
    p->setClipPath(clip);

    const bool isCover = !image.isEmpty() && !image.contains(QLatin1String("/icons/"));
    QPixmap pm;
    if (isCover)
        pm = ImageCache::instance().get(image, cover.size().toSize(), dpr, false);

    if (!pm.isNull()) {
        p->fillRect(cover, sysColor.darker(420));
        const QSizeF s = pm.deviceIndependentSize();
        const QRectF target(cover.center().x() - s.width() / 2, cover.center().y() - s.height() / 2, s.width(), s.height());
        p->drawPixmap(target.topLeft(), pm);
    } else {
        QPixmap icon;
        if (!image.isEmpty() && !isCover) {
            const int box = int(cover.width() * 0.56);
            static QHash<QString, bool> pixelArt; // icônes ≤ 48 px : agrandissement sans lissage
            auto it = pixelArt.find(image);
            if (it == pixelArt.end())
                it = pixelArt.insert(image, QImageReader(image).size().width() <= 48);
            icon = ImageCache::instance().get(image, QSize(box, box), dpr, it.value());
        }
        paintLabel(p, cover, sysId, addon ? idx.data(LibraryModel::BaseTitleRole).toString() + title : title, icon, false);
    }

    // Contenu additionnel : image du jeu assombrie et bandeau « DLC » / « Mise à jour ».
    if (addon) {
        p->fillRect(cover, QColor(12, 14, 26, 120));
        const QColor accent = update ? Theme::Color::teal : Theme::Color::lavender;
        const QRectF band(cover.left(), cover.bottom() - 34, cover.width(), 34);
        p->fillRect(band, QColor(12, 14, 26, 225));
        p->fillRect(QRectF(band.left(), band.top(), band.width(), 2), accent);
        p->setPen(accent);
        p->setFont(Theme::font(15, QFont::Bold, true));
        p->drawText(band.adjusted(12, 0, -10, 0), Qt::AlignLeft | Qt::AlignVCenter, update ? QObject::tr("Mise à jour") : QObject::tr("DLC"));
    }

    // Tranche de la console.
    p->fillRect(QRectF(cover.left(), cover.top(), 4, cover.height()), sysColor);

    // Pastilles : mise à jour et DLC rattachés.
    if (!addon && !missing) {
        qreal x = cover.left() + 10;
        for (const QString& chip : idx.data(LibraryModel::ChipsRole).toStringList()) {
            const QString v = chip.mid(2);
            if (chip.startsWith(QLatin1String("u:")))
                x += drawChip(p, x, cover.bottom() - 8, v.isEmpty() ? QObject::tr("MàJ") : QObject::tr("MàJ %1").arg(v), Theme::Color::teal) + 5;
            else if (chip.startsWith(QLatin1String("d:")))
                x += drawChip(p, x, cover.bottom() - 8, QObject::tr("+%1 DLC").arg(v), Theme::Color::lavender) + 5;
        }
    }

    if (missing) {
        p->fillRect(cover, QColor(10, 12, 20, 170));
        p->setPen(Theme::Color::muted);
        p->setFont(Theme::font(13, QFont::Medium));
        p->drawText(cover, Qt::AlignCenter, QObject::tr("Fichier introuvable"));
    }
    if (!m_running.isEmpty() && path == m_running) {
        const QRectF badge(cover.left() + 10, cover.top() + 10, 86, 24);
        p->setPen(Qt::NoPen);
        p->setBrush(Theme::Color::teal);
        p->drawRoundedRect(badge, 12, 12);
        p->setPen(QColor(0x14, 0x2A, 0x24));
        p->setFont(Theme::font(13, QFont::DemiBold));
        p->drawText(badge, Qt::AlignCenter, QObject::tr("En cours"));
    }
    p->setClipping(false);

    if (idx.data(LibraryModel::FavoriteRole).toBool()) {
        const QRectF star(cover.right() - 30, cover.top() + 8, 22, 22);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(20, 22, 34, 200));
        p->drawEllipse(star.adjusted(-3, -3, 3, 3));
        Theme::paintIcon(*p, QStringLiteral("star-filled"), star, Theme::Color::pink);
    }

    // Titre (2 lignes max) et ligne secondaire.
    const QString shownTitle = addon ? idx.data(LibraryModel::AddonLabelRole).toString() : title;
    const QFont tf = Theme::font(15, selected ? QFont::DemiBold : QFont::Medium);
    const QStringList lines = wrapLines(shownTitle, tf, cell.width(), 2);
    p->setFont(tf);
    p->setPen(selected ? Theme::Color::text : Theme::Color::text.darker(108));
    qreal y = cover.bottom() + 10;
    const qreal lh = QFontMetricsF(tf).height();
    for (const auto& l : lines) {
        p->drawText(QRectF(cell.left(), y, cell.width(), lh), Qt::AlignLeft | Qt::AlignVCenter, l);
        y += lh;
    }
    QString sub;
    QColor subColor = Theme::Color::faint;
    if (addon) {
        sub = idx.data(LibraryModel::SubtitleRole).toString();
        subColor = Theme::Color::muted;
    } else if (m_showSystem) {
        const SystemInfo* s = Registry::instance().system(sysId);
        sub = s ? s->shortName : sysId;
        subColor = sysColor;
    } else {
        sub = formatPlaytime(idx.data(LibraryModel::PlaySecondsRole).toLongLong());
    }
    if (!sub.isEmpty()) {
        p->setFont(Theme::font(13, QFont::Medium));
        p->setPen(subColor);
        p->drawText(QRectF(cell.left(), y + 1, cell.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(p->font()).elidedText(sub, Qt::ElideRight, int(cell.width())));
    }
    p->restore();
}
