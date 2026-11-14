#include "../core/Lang.h"
#include "HeroBand.h"

#include "../core/Library.h"
#include "../core/Registry.h"
#include "GameDelegate.h"
#include "Theme.h"

#include <QDateTime>
#include <QImageReader>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>

namespace {
QString whenText(const QDateTime& t) {
    const qint64 days = t.date().daysTo(QDate::currentDate());
    if (days <= 0)
        return QObject::tr("Joué aujourd'hui");
    if (days == 1)
        return QObject::tr("Joué hier");
    if (days < 30)
        return QObject::tr("Joué il y a %1 jours").arg(days);
    return QObject::tr("Joué le %1").arg(Lang::locale().toString(t.date(), QObject::tr("d MMMM yyyy")));
}
QString totalText(qint64 s) {
    if (s < 60)
        return {};
    if (s < 3600)
        return QObject::tr("%1 min au total").arg(s / 60);
    return QObject::tr("%1 h %2 au total").arg(s / 3600).arg((s % 3600) / 60, 2, 10, QLatin1Char('0'));
}
constexpr int kHeight = 200;
} // namespace

HeroBand::HeroBand(Library* lib, QWidget* parent) : QWidget(parent), m_lib(lib) {
    setFixedHeight(kHeight);
    setCursor(Qt::PointingHandCursor);
    m_play = new QPushButton(Theme::icon(QStringLiteral("play"), Theme::onColor(Theme::Color::gold), 18), tr("Reprendre"), this);
    m_play->setStyleSheet(Theme::primaryButtonStyle(Theme::Color::gold));
    m_play->setCursor(Qt::PointingHandCursor);
    m_play->setMinimumWidth(170);
    connect(m_play, &QPushButton::clicked, this, [this] { emit playRequested(m_path); });
}

bool HeroBand::refresh() {
    const Game* best = nullptr;
    for (const Game& g : m_lib->games())
        if (!g.isAddon && !g.missing && g.lastPlayed.isValid() && (!best || g.lastPlayed > best->lastPlayed))
            best = &g;
    if (!best) {
        m_path.clear();
        return false;
    }
    const QString image = best->bestImage();
    if (best->path != m_path || image != m_image) {
        m_path = best->path;
        m_image = image;
        m_blur = QPixmap();
        m_cover = QPixmap();
        QImageReader r(image);
        r.setAutoTransform(true);
        const QImage img = r.read();
        if (!img.isNull()) {
            // Flou bon marché : réduction extrême puis agrandissement lissé.
            const QImage tiny = img.scaled(28, 28, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            m_blur = QPixmap::fromImage(tiny.scaled(tiny.size() * 24, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            const bool pixel = img.width() <= 48;
            const int h = kHeight - 44;
            m_cover = QPixmap::fromImage(img.scaledToHeight(h * 2, pixel ? Qt::FastTransformation : Qt::SmoothTransformation));
            m_cover.setDevicePixelRatio(2);
        }
    }
    update();
    return true;
}

void HeroBand::setRunning(bool running) {
    m_play->setEnabled(!running);
    m_play->setText(running ? tr("En cours…") : tr("Reprendre"));
}

void HeroBand::resizeEvent(QResizeEvent*) { layoutButton(); }

void HeroBand::layoutButton() {
    m_play->adjustSize();
    m_play->move(32, height() - m_play->height() - 26);
}

void HeroBand::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && !m_path.isEmpty())
        emit selectRequested(m_path);
}

void HeroBand::paintEvent(QPaintEvent*) {
    const Game* g = m_lib->find(m_path);
    if (!g)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF r = rect().adjusted(18, 14, -18, -4);
    QPainterPath clip;
    clip.addRoundedRect(r, 14, 14);
    p.setClipPath(clip);

    const QColor c = Theme::categoryColor(g->system());
    // Fond : jaquette floutée, sinon dégradé de la console.
    if (!m_blur.isNull()) {
        const QSizeF s = m_blur.size();
        const qreal k = qMax(r.width() / s.width(), r.height() / s.height());
        const QSizeF t = s * k;
        p.drawPixmap(QRectF(r.center().x() - t.width() / 2, r.center().y() - t.height() / 2, t.width(), t.height()), m_blur, QRectF(QPointF(), s));
    } else {
        QLinearGradient bg(r.topLeft(), r.bottomRight());
        bg.setColorAt(0, c.darker(170));
        bg.setColorAt(1, c.darker(400));
        p.fillRect(r, bg);
    }
    QLinearGradient shade(r.topLeft(), r.topRight());
    QColor base = Theme::Color::bg;
    base.setAlpha(238);
    shade.setColorAt(0, base);
    base.setAlpha(190);
    shade.setColorAt(0.55, base);
    base.setAlpha(90);
    shade.setColorAt(1, base);
    p.fillRect(r, shade);
    p.fillRect(QRectF(r.left(), r.top(), 5, r.height()), c);

    // Jaquette nette à droite.
    qreal textRight = r.right() - 24;
    if (!m_cover.isNull()) {
        const QSizeF cs = m_cover.deviceIndependentSize();
        const QRectF cr(r.right() - 28 - cs.width(), r.center().y() - cs.height() / 2, cs.width(), cs.height());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 120));
        p.drawRoundedRect(cr.adjusted(-1, 6, 1, 9), 8, 8);
        QPainterPath cp;
        cp.addRoundedRect(cr, 7, 7);
        p.save();
        p.setClipPath(cp, Qt::IntersectClip);
        p.drawPixmap(cr.topLeft(), m_cover);
        p.restore();
        textRight = cr.left() - 24;
    }

    // Titre, console, temps de jeu.
    const qreal left = r.left() + 14 + 14;
    QFont tf = Theme::font(42, QFont::Bold, true);
    const QString title = QFontMetrics(tf).elidedText(g->displayTitle(), Qt::ElideRight, int(textRight - left));
    p.setFont(tf);
    p.setPen(Theme::Color::text);
    p.drawText(QRectF(left, r.top() + 22, textRight - left, 52), Qt::AlignLeft | Qt::AlignVCenter, title);

    const SystemInfo* sys = Registry::instance().system(g->system());
    QFont mf = Theme::font(15, QFont::DemiBold);
    p.setFont(mf);
    const QString sysName = sys ? sys->name : g->system();
    p.setPen(c.lighter(115));
    const qreal y = r.top() + 80;
    p.drawText(QPointF(left, y + 15), sysName);
    qreal x = left + QFontMetricsF(mf).horizontalAdvance(sysName) + 14;
    QStringList meta{whenText(g->lastPlayed)};
    if (const QString t = totalText(g->playSeconds); !t.isEmpty())
        meta << t;
    p.setFont(Theme::font(15, QFont::Medium));
    p.setPen(Theme::Color::muted);
    const QString metaText = meta.join(QStringLiteral(", "));
    p.drawText(QPointF(x, y + 15), metaText);
    x += QFontMetricsF(p.font()).horizontalAdvance(metaText) + 14;

    // Contenus rattachés.
    const Library::AddonStats st = m_lib->addonStats(*g);
    QStringList chips;
    if (st.updates > 0)
        chips << (st.latestUpdate.startsWith(QStringLiteral("Mise à jour")) || st.latestUpdate.startsWith(tr("Mise à jour")) ? st.latestUpdate
                  : st.latestUpdate.isEmpty() ? tr("Mise à jour") : tr("Mise à jour %1").arg(st.latestUpdate));
    if (st.dlcs > 0)
        chips << tr("%n DLC", nullptr, st.dlcs);
    QFont cf = Theme::font(13, QFont::DemiBold);
    p.setFont(cf);
    for (int i = 0; i < chips.size(); ++i) {
        const qreal w = QFontMetricsF(cf).horizontalAdvance(chips[i]) + 20;
        if (x + w > textRight)
            break;
        const QRectF chip(x, y, w, 22);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 26));
        p.drawRoundedRect(chip, 11, 11);
        p.setPen(i == 0 && st.updates > 0 ? Theme::Color::teal : Theme::Color::lavender);
        p.drawText(chip, Qt::AlignCenter, chips[i]);
        x += w + 6;
    }
}
