#include "Theme.h"

#include "../core/Registry.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <QtMath>

namespace Theme {

namespace {
QString g_display = QStringLiteral("Barlow Semi Condensed");
QString g_text = QStringLiteral("Barlow");

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }
} // namespace

QString displayFamily() { return g_display; }
QString textFamily() { return g_text; }

QFont font(int px, int weight, bool display) {
    QFont f(display ? g_display : g_text);
    f.setPixelSize(px);
    f.setWeight(static_cast<QFont::Weight>(weight));
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

void apply(QApplication& app) {
    // Polices intégrées (SIL Open Font License).
    for (const char* f : {":/fonts/Barlow_400Regular.ttf", ":/fonts/Barlow_500Medium.ttf", ":/fonts/Barlow_600SemiBold.ttf",
                          ":/fonts/BarlowSemiCondensed_600SemiBold.ttf", ":/fonts/BarlowSemiCondensed_700Bold.ttf"}) {
        const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(f));
        if (id < 0)
            qWarning("Police non chargée : %s", f);
    }
    const QStringList fams = QFontDatabase::families();
    if (!fams.contains(g_text))
        g_text = app.font().family();
    if (!fams.contains(g_display))
        g_display = g_text;

    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette pal;
    pal.setColor(QPalette::Window, Color::bg);
    pal.setColor(QPalette::WindowText, Color::text);
    pal.setColor(QPalette::Base, Color::raised);
    pal.setColor(QPalette::AlternateBase, Color::panel);
    pal.setColor(QPalette::Text, Color::text);
    pal.setColor(QPalette::Button, Color::raised);
    pal.setColor(QPalette::ButtonText, Color::text);
    pal.setColor(QPalette::Highlight, Color::gold);
    pal.setColor(QPalette::HighlightedText, QColor(0x1A, 0x1D, 0x2B));
    pal.setColor(QPalette::ToolTipBase, Color::raised);
    pal.setColor(QPalette::ToolTipText, Color::text);
    pal.setColor(QPalette::PlaceholderText, Color::faint);
    pal.setColor(QPalette::Link, Color::gold);
    pal.setColor(QPalette::Disabled, QPalette::Text, Color::faint);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, Color::faint);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, Color::faint);
    app.setPalette(pal);
    app.setFont(font(14));

    const QString qss = QStringLiteral(R"(
QWidget { color: %1; }
QToolTip { background: %3; color: %1; border: 1px solid %4; padding: 6px 8px; }
QLineEdit, QComboBox, QSpinBox, QPlainTextEdit, QTextEdit {
    background: %3; border: 1px solid %4; border-radius: 6px; padding: 6px 10px; selection-background-color: %6;
    selection-color: %7;
}
QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus { border-color: %6; }
QComboBox::drop-down { border: none; width: 26px; }
QComboBox::down-arrow { image: url(:/icons/chevron.png); width: 12px; height: 7px; }
QComboBox QAbstractItemView { background: %3; border: 1px solid %4; selection-background-color: %4; outline: 0; padding: 4px; }
QPushButton {
    background: %3; border: 1px solid %4; border-radius: 6px; padding: 7px 14px; font-weight: 500;
}
QPushButton:hover { background: %4; }
QPushButton:pressed { background: %2; }
QPushButton:disabled { color: %5; background: %2; }
QPushButton:focus { border-color: %6; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 5px; }
QToolButton:hover { background: %3; border-color: %4; }
QToolButton:checked { background: %3; border-color: %6; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: %4; border-radius: 3px; min-height: 40px; }
QScrollBar::handle:vertical:hover { background: %5; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: %4; border-radius: 3px; min-width: 40px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QMenu { background: %3; border: 1px solid %4; padding: 6px; }
QMenu::item { padding: 7px 26px 7px 14px; border-radius: 4px; }
QMenu::item:selected { background: %4; }
QMenu::item:disabled { color: %5; }
QMenu::separator { height: 1px; background: %4; margin: 5px 8px; }
QCheckBox { spacing: 10px; }
QCheckBox::indicator { width: 18px; height: 18px; border-radius: 4px; border: 1px solid %5; background: %2; }
QCheckBox::indicator:checked { background: %6; border-color: %6; image: url(:/icons/check.png); }
QSlider::groove:horizontal { height: 4px; background: %4; border-radius: 2px; }
QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; background: %1; }
QTabWidget::pane { border: none; }
QTabBar::tab { background: transparent; color: %8; padding: 9px 16px; border: none; border-bottom: 2px solid transparent; font-weight: 500; }
QTabBar::tab:selected { color: %1; border-bottom-color: %6; }
QTabBar::tab:hover { color: %1; }
QTableWidget, QListWidget, QTreeWidget { background: %2; border: 1px solid %4; border-radius: 8px; outline: 0; }
QTableWidget::item, QListWidget::item { padding: 6px; }
QListWidget::item:selected, QTableWidget::item:selected { background: %4; color: %1; }
QHeaderView::section { background: %2; color: %8; border: none; border-bottom: 1px solid %4; padding: 8px; font-weight: 500; }
QProgressBar { background: %3; border: none; border-radius: 3px; height: 6px; text-align: center; color: transparent; }
QProgressBar::chunk { background: %6; border-radius: 3px; }
QDialog { background: %9; }
QLabel[role="muted"] { color: %8; }
QLabel[role="faint"] { color: %5; }
QFrame[role="separator"] { background: %4; max-height: 1px; min-height: 1px; border: none; }
)")
                            .arg(hex(Color::text), hex(Color::panel), hex(Color::raised), hex(Color::line), hex(Color::faint),
                                 hex(Color::gold), hex(Color::bg), hex(Color::muted), hex(Color::bg));
    app.setStyleSheet(qss);
}

QColor categoryColor(const QString& id) {
    if (id == QLatin1String("__home") || id == QLatin1String("__all")) return Color::gold;
    if (id == QLatin1String("__fav")) return Color::pink;
    if (id == QLatin1String("__recent")) return Color::teal;
    if (id == QLatin1String("__addons")) return Color::lavender;
    if (const SystemInfo* s = Registry::instance().system(id))
        return s->color;
    return Color::gold;
}

QColor onColor(const QColor& c) {
    const double l = 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
    return l > 0.55 ? QColor(0x1A, 0x1D, 0x2B) : QColor(Qt::white);
}

QString primaryButtonStyle(const QColor& accent, bool compact) {
    const QColor fg = onColor(accent);
    return QStringLiteral("QPushButton { background: %1; color: %2; border: none; border-radius: 8px; padding: %6;"
                          " font-family: '%5'; font-size: %7px; font-weight: 700; }"
                          "QPushButton:hover { background: %3; }"
                          "QPushButton:pressed { background: %4; }"
                          "QPushButton:disabled { background: #353B5A; color: #6B7294; }"
                          "QPushButton:focus { outline: none; border: 2px solid #ECEEF6; }")
        .arg(hex(accent), hex(fg), hex(accent.lighter(112)), hex(accent.darker(115)), g_display,
             compact ? QStringLiteral("7px 14px") : QStringLiteral("11px 18px"), compact ? QStringLiteral("15") : QStringLiteral("18"));
}

void paintIcon(QPainter& p, const QString& name, const QRectF& r, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = qMin(r.width(), r.height());
    const QPointF c = r.center();
    QPen pen(color, qMax<qreal>(1.6, s / 11.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto R = [&](qreal x, qreal y, qreal w, qreal h) { return QRectF(c.x() + x * s, c.y() + y * s, w * s, h * s); };
    auto P = [&](qreal x, qreal y) { return QPointF(c.x() + x * s, c.y() + y * s); };

    if (name == QLatin1String("search")) {
        p.drawEllipse(R(-0.36, -0.36, 0.56, 0.56));
        p.drawLine(P(0.12, 0.12), P(0.36, 0.36));
    } else if (name == QLatin1String("settings")) {
        QPainterPath gear;
        const int teeth = 8;
        for (int i = 0; i < teeth * 2; ++i) {
            const qreal a = i * M_PI / teeth;
            const qreal rad = (i % 2 == 0 ? 0.40 : 0.30) * s;
            const QPointF pt(c.x() + rad * qCos(a), c.y() + rad * qSin(a));
            i == 0 ? gear.moveTo(pt) : gear.lineTo(pt);
        }
        gear.closeSubpath();
        p.drawPath(gear);
        p.drawEllipse(R(-0.12, -0.12, 0.24, 0.24));
    } else if (name == QLatin1String("plus")) {
        p.drawLine(P(0, -0.32), P(0, 0.32));
        p.drawLine(P(-0.32, 0), P(0.32, 0));
    } else if (name == QLatin1String("refresh")) {
        p.drawArc(R(-0.32, -0.32, 0.64, 0.64), 40 * 16, 290 * 16);
        QPainterPath head;
        head.moveTo(P(0.30, -0.36));
        head.lineTo(P(0.30, -0.12));
        head.lineTo(P(0.06, -0.12));
        p.drawPath(head);
    } else if (name == QLatin1String("star") || name == QLatin1String("star-filled")) {
        QPainterPath star;
        for (int i = 0; i < 10; ++i) {
            const qreal a = -M_PI / 2 + i * M_PI / 5;
            const qreal rad = (i % 2 == 0 ? 0.42 : 0.18) * s;
            const QPointF pt(c.x() + rad * qCos(a), c.y() + 0.04 * s + rad * qSin(a));
            i == 0 ? star.moveTo(pt) : star.lineTo(pt);
        }
        star.closeSubpath();
        if (name == QLatin1String("star-filled"))
            p.setBrush(color);
        p.drawPath(star);
    } else if (name == QLatin1String("play")) {
        QPainterPath tri;
        tri.moveTo(P(-0.22, -0.32));
        tri.lineTo(P(0.34, 0));
        tri.lineTo(P(-0.22, 0.32));
        tri.closeSubpath();
        p.setBrush(color);
        p.drawPath(tri);
    } else if (name == QLatin1String("folder")) {
        QPainterPath f;
        f.moveTo(P(-0.40, -0.24));
        f.lineTo(P(-0.12, -0.24));
        f.lineTo(P(-0.04, -0.14));
        f.lineTo(P(0.40, -0.14));
        f.lineTo(P(0.40, 0.30));
        f.lineTo(P(-0.40, 0.30));
        f.closeSubpath();
        p.drawPath(f);
    } else if (name == QLatin1String("fullscreen")) {
        const qreal a = 0.34, b = 0.14;
        for (int sx : {-1, 1})
            for (int sy : {-1, 1}) {
                p.drawLine(P(sx * a, sy * a), P(sx * (a - b), sy * a));
                p.drawLine(P(sx * a, sy * a), P(sx * a, sy * (a - b)));
            }
    } else if (name == QLatin1String("grid")) {
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
                p.drawRoundedRect(R(-0.34 + i * 0.38, -0.34 + j * 0.38, 0.30, 0.30), 0.04 * s, 0.04 * s);
    } else if (name == QLatin1String("clock")) {
        p.drawEllipse(R(-0.36, -0.36, 0.72, 0.72));
        p.drawLine(P(0, 0), P(0, -0.20));
        p.drawLine(P(0, 0), P(0.14, 0.08));
    } else if (name == QLatin1String("panel")) {
        p.drawRoundedRect(R(-0.38, -0.30, 0.76, 0.60), 0.06 * s, 0.06 * s);
        p.drawLine(P(0.12, -0.30), P(0.12, 0.30));
    } else if (name == QLatin1String("download")) {
        p.drawLine(P(0, -0.34), P(0, 0.14));
        p.drawLine(P(-0.18, -0.04), P(0, 0.14));
        p.drawLine(P(0.18, -0.04), P(0, 0.14));
        p.drawLine(P(-0.32, 0.32), P(0.32, 0.32));
    } else if (name == QLatin1String("gamepad")) {
        p.drawRoundedRect(R(-0.42, -0.20, 0.84, 0.42), 0.18 * s, 0.18 * s);
        p.drawLine(P(-0.24, 0), P(-0.10, 0));
        p.drawLine(P(-0.17, -0.07), P(-0.17, 0.07));
        p.setBrush(color);
        p.drawEllipse(P(0.16, 0.0), 0.03 * s, 0.03 * s);
        p.drawEllipse(P(0.26, -0.02), 0.03 * s, 0.03 * s);
    } else if (name == QLatin1String("layers")) { // contenus additionnels : plaques empilées
        for (int i = 0; i < 3; ++i) {
            const qreal y = -0.22 + i * 0.18;
            QPainterPath d;
            d.moveTo(P(-0.38, y));
            d.lineTo(P(0, y - 0.14));
            d.lineTo(P(0.38, y));
            d.lineTo(P(0, y + 0.14));
            d.closeSubpath();
            if (i == 0)
                p.drawPath(d);
            else {
                p.drawLine(P(-0.38, y), P(0, y + 0.14));
                p.drawLine(P(0, y + 0.14), P(0.38, y));
            }
        }
    } else if (name == QLatin1String("tidy")) { // rangement : trois casiers
        p.drawRoundedRect(R(-0.38, -0.34, 0.76, 0.68), 0.05 * s, 0.05 * s);
        p.drawLine(P(-0.38, -0.11), P(0.38, -0.11));
        p.drawLine(P(-0.38, 0.12), P(0.38, 0.12));
        p.drawLine(P(-0.08, -0.22), P(0.08, -0.22));
        p.drawLine(P(-0.08, 0.01), P(0.08, 0.01));
        p.drawLine(P(-0.08, 0.24), P(0.08, 0.24));
    } else if (name == QLatin1String("save")) { // disquette
        QPainterPath d;
        d.moveTo(P(-0.34, -0.34));
        d.lineTo(P(0.20, -0.34));
        d.lineTo(P(0.34, -0.20));
        d.lineTo(P(0.34, 0.34));
        d.lineTo(P(-0.34, 0.34));
        d.closeSubpath();
        p.drawPath(d);
        p.drawRect(R(-0.18, -0.34, 0.30, 0.20));
        p.drawRect(R(-0.20, 0.06, 0.40, 0.28));
    } else if (name == QLatin1String("tv")) {
        p.drawRoundedRect(R(-0.40, -0.28, 0.80, 0.50), 0.06 * s, 0.06 * s);
        p.drawLine(P(-0.16, 0.36), P(0.16, 0.36));
    } else if (name == QLatin1String("home")) { // maison : toit et murs
        QPainterPath house;
        house.moveTo(P(-0.36, -0.02));
        house.lineTo(P(0, -0.36));
        house.lineTo(P(0.36, -0.02));
        p.drawPath(house);
        QPainterPath walls;
        walls.moveTo(P(-0.26, -0.08));
        walls.lineTo(P(-0.26, 0.34));
        walls.lineTo(P(0.26, 0.34));
        walls.lineTo(P(0.26, -0.08));
        p.drawPath(walls);
        p.drawLine(P(0, 0.34), P(0, 0.12));
    } else if (name == QLatin1String("globe")) { // langue
        p.drawEllipse(R(-0.36, -0.36, 0.72, 0.72));
        p.drawEllipse(R(-0.15, -0.36, 0.30, 0.72));
        p.drawLine(P(-0.36, 0), P(0.36, 0));
    } else if (name == QLatin1String("arrow-left")) {
        p.drawLine(P(0.30, 0), P(-0.30, 0));
        p.drawLine(P(-0.30, 0), P(-0.10, -0.20));
        p.drawLine(P(-0.30, 0), P(-0.10, 0.20));
    }
    p.restore();
}

void paintLogo(QPainter& p, const QRectF& r) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    // Un nid tressé aux couleurs des consoles, qui porte un bouton « lecture » doré, sur une tuile sombre.
    const qreal s = qMin(r.width(), r.height());
    const QRectF tile(r.center().x() - s / 2, r.center().y() - s / 2, s, s);
    const bool tiny = s < 28;
    QLinearGradient bg(tile.topLeft(), tile.bottomLeft());
    bg.setColorAt(0, QColor(0x2B, 0x30, 0x52));
    bg.setColorAt(1, QColor(0x14, 0x17, 0x2A));
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(tile, s * 0.23, s * 0.23);
    if (!tiny) { // liseré clair : la tuile reste lisible sur une barre des tâches sombre
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 38), qMax(1.0, s * 0.012)));
        p.drawRoundedRect(tile.adjusted(s * 0.006, s * 0.006, -s * 0.006, -s * 0.006), s * 0.225, s * 0.225);
    }

    // Le nid : un arc ouvert vers le haut, découpé en brins aux couleurs des consoles.
    const auto& systems = Registry::instance().systems();
    const int n = qMax(1, int(systems.size()));
    const qreal thick = s * (tiny ? 0.17 : 0.135);
    const qreal ringD = s * 0.74;
    const QPointF c(tile.center().x(), tile.top() + s * 0.47);
    const QRectF ring(c.x() - ringD / 2 + thick / 2, c.y() - ringD / 2 + thick / 2, ringD - thick, ringD - thick);
    const qreal startDeg = 188, spanDeg = 164; // de la gauche à la droite en passant par le bas
    const qreal gap = tiny ? 0 : 2.2;
    for (int i = 0; i < n; ++i) {
        const bool end = i == 0 || i == n - 1;
        QPen pen(systems.isEmpty() ? Color::gold : systems[i].color, thick, Qt::SolidLine, end && !tiny ? Qt::RoundCap : Qt::FlatCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const qreal a0 = startDeg + spanDeg * i / n + gap / 2;
        p.drawArc(ring, int(a0 * 16), int((spanDeg / n - gap) * 16));
    }

    // Le bouton « lecture », posé dans le nid.
    const qreal t = s * (tiny ? 0.36 : 0.33);
    const QPointF m(c.x() + t * 0.08, c.y() - s * 0.035);
    QPainterPath play;
    play.moveTo(m.x() - t * 0.42, m.y() - t * 0.52);
    play.lineTo(m.x() + t * 0.52, m.y());
    play.lineTo(m.x() - t * 0.42, m.y() + t * 0.52);
    play.closeSubpath();
    QLinearGradient gold(QPointF(m.x(), m.y() - t / 2), QPointF(m.x(), m.y() + t / 2));
    gold.setColorAt(0, QColor(0xFF, 0xDC, 0x7A));
    gold.setColorAt(1, QColor(0xF0, 0xA8, 0x30));
    p.setBrush(gold);
    p.setPen(QPen(QBrush(gold), t * 0.16, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); // coins arrondis
    p.drawPath(play);
    p.restore();
}

QPixmap logoPixmap(int size, qreal dpr) {
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(dpr);
    QPainter p(&pm);
    paintLogo(p, QRectF(0, 0, size, size));
    p.end();
    return pm;
}

QIcon icon(const QString& name, const QColor& color, int size) {
    QIcon ic;
    for (int scale : {1, 2}) {
        QPixmap pm(size * scale, size * scale);
        pm.fill(Qt::transparent);
        pm.setDevicePixelRatio(scale);
        QPainter p(&pm);
        paintIcon(p, name, QRectF(0, 0, size, size), color);
        p.end();
        ic.addPixmap(pm);
    }
    return ic;
}

} // namespace Theme
