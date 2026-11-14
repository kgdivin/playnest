#include "HomePage.h"

#include "../core/Library.h"
#include "../core/Registry.h"
#include "../core/Settings.h"
#include "Theme.h"

#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

namespace {
constexpr int kMargin = 30;
constexpr int kTop = 62;     // titre « Tes consoles »
constexpr int kGap = 14;
constexpr int kMinTile = 188;
constexpr int kTileH = 128;    // hauteur minimale ; les tuiles grandissent s'il reste de la place
constexpr int kTileMaxH = 184;
constexpr qreal kRadius = 14;
} // namespace

HomePage::HomePage(Library* lib, QWidget* parent) : QWidget(parent), m_lib(lib) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    // Une seule animation pour toutes les tuiles : celle survolée monte, les autres redescendent.
    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(1000000);
    m_anim->setStartValue(0);
    m_anim->setEndValue(1);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this] {
        bool moving = false;
        for (int i = 0; i < m_tiles.size(); ++i) {
            const qreal target = i == m_hover ? 1 : 0;
            if (!qFuzzyCompare(m_tiles[i].lift + 1, target + 1)) {
                m_tiles[i].lift += (target - m_tiles[i].lift) * 0.28;
                if (qAbs(target - m_tiles[i].lift) < 0.02)
                    m_tiles[i].lift = target;
                moving = true;
            }
        }
        update();
        if (!moving)
            m_anim->stop();
    });
    refresh();
}

void HomePage::refresh() {
    const bool addons = !Settings::instance().hideAddons();
    QVector<Tile> tiles;
    m_totalGames = 0;
    for (const SystemInfo& s : Registry::instance().systems()) {
        Tile t;
        t.id = s.id;
        t.games = m_lib->countForSystem(s.id, addons);
        m_totalGames += t.games;
        tiles << t;
    }
    tiles << Tile{}; // « Ajouter des jeux »
    for (int i = 0; i < tiles.size() && i < m_tiles.size(); ++i)
        tiles[i].lift = m_tiles[i].lift;
    m_tiles = tiles;
    m_playSeconds = 0;
    for (const Game& g : m_lib->games())
        m_playSeconds += g.playSeconds;
    m_focus = qBound(0, m_focus, int(m_tiles.size()) - 1);
    layoutTiles();
    update();
}

void HomePage::layoutTiles() {
    const int avail = qMax(kMinTile, width() - 2 * kMargin);
    m_columns = qMax(1, (avail + kGap) / (kMinTile + kGap));
    const qreal tw = (avail - (m_columns - 1) * kGap) / qreal(m_columns);
    const int rows = qMax(1, (int(m_tiles.size()) + m_columns - 1) / m_columns);
    const int th = qBound(kTileH, (height() - kTop - 24 - (rows - 1) * kGap) / rows, kTileMaxH);
    for (int i = 0; i < m_tiles.size(); ++i) {
        const int row = i / m_columns, col = i % m_columns;
        m_tiles[i].rect = QRectF(kMargin + col * (tw + kGap), kTop + row * (th + kGap), tw, th);
    }
    setMinimumHeight(kTop + rows * (kTileH + kGap) + 20);
}

void HomePage::resizeEvent(QResizeEvent*) { layoutTiles(); }

int HomePage::tileAt(const QPointF& pos) const {
    for (int i = 0; i < m_tiles.size(); ++i)
        if (m_tiles[i].rect.contains(pos))
            return i;
    return -1;
}

void HomePage::setHover(int index) {
    if (index == m_hover)
        return;
    m_hover = index;
    setCursor(index >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (m_anim->state() != QAbstractAnimation::Running)
        m_anim->start();
}

void HomePage::mouseMoveEvent(QMouseEvent* e) { setHover(tileAt(e->position())); }
void HomePage::leaveEvent(QEvent*) { setHover(-1); }
void HomePage::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton)
        m_pressed = tileAt(e->position());
}
void HomePage::mouseReleaseEvent(QMouseEvent* e) {
    const int at = tileAt(e->position());
    if (e->button() == Qt::LeftButton && at >= 0 && at == m_pressed) {
        m_focus = at;
        activate(at);
    }
    m_pressed = -1;
}

void HomePage::activate(int index) {
    if (index < 0 || index >= m_tiles.size())
        return;
    if (m_tiles[index].id.isEmpty())
        emit addGamesRequested();
    else
        emit consoleChosen(m_tiles[index].id);
}

void HomePage::keyPressEvent(QKeyEvent* e) {
    const int n = m_tiles.size();
    int f = m_focus;
    switch (e->key()) {
    case Qt::Key_Left: f = qMax(0, f - 1); break;
    case Qt::Key_Right: f = qMin(n - 1, f + 1); break;
    case Qt::Key_Up: f = f - m_columns >= 0 ? f - m_columns : f; break;
    case Qt::Key_Down: f = f + m_columns < n ? f + m_columns : n - 1; break;
    case Qt::Key_Home: f = 0; break;
    case Qt::Key_End: f = n - 1; break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space: activate(m_focus); return;
    default: QWidget::keyPressEvent(e); return;
    }
    m_focus = f;
    update();
}

void HomePage::paintTile(QPainter& p, const Tile& t, bool focused) const {
    const SystemInfo* sys = t.id.isEmpty() ? nullptr : Registry::instance().system(t.id);
    const QRectF r = t.rect.translated(0, -4 * t.lift);
    QPainterPath shape;
    shape.addRoundedRect(r, kRadius, kRadius);

    // Ombre portée, plus profonde quand la tuile se soulève.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, int(50 + 40 * t.lift)));
    p.drawRoundedRect(r.translated(0, 5 + 4 * t.lift).adjusted(4, 0, -4, 0), kRadius, kRadius);

    if (!sys) { // « Ajouter des jeux » : contour en pointillés
        p.setBrush(QColor(255, 255, 255, int(6 + 14 * t.lift)));
        p.setPen(QPen(t.lift > 0.5 ? Theme::Color::gold : Theme::Color::line, 2, Qt::DashLine));
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), kRadius, kRadius);
        Theme::paintIcon(p, QStringLiteral("plus"), QRectF(r.center().x() - 15, r.center().y() - 34, 30, 30), Theme::Color::gold);
        p.setPen(Theme::Color::text);
        p.setFont(Theme::font(17, QFont::DemiBold));
        p.drawText(QRectF(r.left(), r.center().y() + 6, r.width(), 26), Qt::AlignHCenter | Qt::AlignVCenter, tr("Ajouter des jeux"));
    } else if (t.games > 0) {
        // Console avec des jeux : un aplat à sa couleur, comme la tranche d'une boîte de jeu.
        const QColor c = sys->color;
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0, c.lighter(108));
        g.setColorAt(1, c.darker(165));
        p.setBrush(g);
        p.drawPath(shape);
        p.save();
        p.setClipPath(shape);
        // Rayures fines en haut à droite, reprises des étiquettes de jeux.
        p.setPen(QPen(QColor(255, 255, 255, 26), 1));
        for (int i = 0; i < 6; ++i)
            p.drawLine(QPointF(r.right() - 74, r.top() + 44 + i * 5), QPointF(r.right() - 14, r.top() + 44 + i * 5));
        p.restore();
        const QColor ink = Theme::onColor(c);
        QColor soft = ink;
        soft.setAlpha(190);
        p.setPen(soft);
        p.setFont(Theme::font(14, QFont::DemiBold));
        if (sys->year)
            p.drawText(QRectF(r.left() + 16, r.top() + 12, 80, 20), Qt::AlignLeft | Qt::AlignVCenter, QString::number(sys->year));
        // Nombre de jeux, en haut à droite.
        const QString count = t.games == 1 ? tr("%1 jeu").arg(t.games) : tr("%1 jeux").arg(t.games);
        const qreal cw = QFontMetricsF(p.font()).horizontalAdvance(count) + 20;
        const QRectF pill(r.right() - 12 - cw, r.top() + 11, cw, 24);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 80));
        p.drawRoundedRect(pill, 12, 12);
        p.setPen(Qt::white);
        p.drawText(pill, Qt::AlignCenter, count);
        // Nom court en grand, nom complet au-dessus s'il diffère.
        p.setPen(ink);
        p.setFont(Theme::font(40, QFont::Bold, true));
        p.drawText(QRectF(r.left() + 16, r.bottom() - 58, r.width() - 28, 48), Qt::AlignLeft | Qt::AlignVCenter, sys->shortName);
        if (sys->name != sys->shortName) {
            p.setPen(soft);
            p.setFont(Theme::font(14, QFont::Medium));
            p.drawText(QRectF(r.left() + 16, r.bottom() - 78, r.width() - 28, 20), Qt::AlignLeft | Qt::AlignVCenter,
                       QFontMetrics(p.font()).elidedText(sys->name, Qt::ElideRight, int(r.width() - 28)));
        }
    } else {
        // Console sans jeu : tuile éteinte, la couleur ne reste que sur la tranche et le nom.
        const QColor c = sys->color;
        p.setBrush(QColor(Theme::Color::panel).lighter(int(100 + 14 * t.lift)));
        p.drawPath(shape);
        p.save();
        p.setClipPath(shape);
        p.fillRect(QRectF(r.left(), r.top(), 5, r.height()), c);
        p.restore();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Theme::Color::line, 1));
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
        p.setPen(Theme::Color::faint);
        p.setFont(Theme::font(14, QFont::DemiBold));
        if (sys->year)
            p.drawText(QRectF(r.left() + 18, r.top() + 12, 80, 20), Qt::AlignLeft | Qt::AlignVCenter, QString::number(sys->year));
        p.drawText(QRectF(r.right() - 132, r.top() + 12, 120, 20), Qt::AlignRight | Qt::AlignVCenter, tr("Aucun jeu"));
        QColor name = c;
        name.setAlpha(int(170 + 85 * t.lift));
        p.setPen(name);
        p.setFont(Theme::font(40, QFont::Bold, true));
        p.drawText(QRectF(r.left() + 18, r.bottom() - 58, r.width() - 30, 48), Qt::AlignLeft | Qt::AlignVCenter, sys->shortName);
        if (sys->name != sys->shortName) {
            p.setPen(Theme::Color::faint);
            p.setFont(Theme::font(14, QFont::Medium));
            p.drawText(QRectF(r.left() + 18, r.bottom() - 78, r.width() - 30, 20), Qt::AlignLeft | Qt::AlignVCenter,
                       QFontMetrics(p.font()).elidedText(sys->name, Qt::ElideRight, int(r.width() - 30)));
        }
    }
    if (t.lift > 0.01 && sys) { // liseré clair au survol
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, int(90 * t.lift)), 1.5));
        p.drawRoundedRect(r.adjusted(0.75, 0.75, -0.75, -0.75), kRadius, kRadius);
    }
    if (focused) { // navigation au clavier ou à la manette
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Theme::Color::text, 3));
        p.drawRoundedRect(r.adjusted(-4, -4, 4, 4), kRadius + 4, kRadius + 4);
    }
}

void HomePage::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Theme::Color::text);
    p.setFont(Theme::font(26, QFont::Bold, true));
    p.drawText(QRectF(kMargin, 12, width() - 2 * kMargin, 36), Qt::AlignLeft | Qt::AlignVCenter, tr("Tes consoles"));
    // À droite : ce que contient la bibliothèque.
    QString summary = m_totalGames == 1 ? tr("%1 jeu").arg(m_totalGames) : tr("%1 jeux").arg(m_totalGames);
    if (m_playSeconds >= 3600)
        summary = tr("%1, %2 h de jeu").arg(summary).arg(m_playSeconds / 3600);
    else if (m_playSeconds >= 60)
        summary = tr("%1, %2 min de jeu").arg(summary).arg(m_playSeconds / 60);
    p.setPen(Theme::Color::muted);
    p.setFont(Theme::font(15, QFont::Medium));
    p.drawText(QRectF(kMargin, 14, width() - 2 * kMargin, 34), Qt::AlignRight | Qt::AlignVCenter, summary);
    for (int i = 0; i < m_tiles.size(); ++i)
        if (i != m_hover)
            paintTile(p, m_tiles[i], hasFocus() && i == m_focus);
    if (m_hover >= 0 && m_hover < m_tiles.size()) // la tuile survolée passe au-dessus
        paintTile(p, m_tiles[m_hover], hasFocus() && m_hover == m_focus);
}
