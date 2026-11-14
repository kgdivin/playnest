#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPixmap>
#include <QString>

class QApplication;

// Identité visuelle de Playnest : fond indigo "écran éteint", texte clair,
// et une couleur d'accent qui suit la console sélectionnée.
namespace Theme {

namespace Color {
inline const QColor bg{0x1A, 0x1D, 0x2B};
inline const QColor panel{0x20, 0x24, 0x3A};
inline const QColor raised{0x2A, 0x2F, 0x49};
inline const QColor line{0x35, 0x3B, 0x5A};
inline const QColor text{0xEC, 0xEE, 0xF6};
inline const QColor muted{0x9B, 0xA2, 0xC0};
inline const QColor faint{0x6B, 0x72, 0x94};
inline const QColor gold{0xF2, 0xC1, 0x4E};   // "Tous les jeux"
inline const QColor pink{0xFF, 0x6F, 0xA8};   // Favoris
inline const QColor teal{0x5E, 0xD3, 0xB0};   // Récents
inline const QColor lavender{0xB7, 0x9C, 0xFF}; // DLC et mises à jour
inline const QColor danger{0xFF, 0x5D, 0x5D};
} // namespace Color

void apply(QApplication& app);

QString displayFamily(); // Barlow Semi Condensed (titres)
QString textFamily();    // Barlow (texte)
QFont font(int pixelSize, int weight = QFont::Normal, bool display = false);

// Couleur d'une catégorie (système ou catégorie spéciale).
QColor categoryColor(const QString& categoryId);
// Texte lisible (noir ou blanc) sur une couleur de fond.
QColor onColor(const QColor& c);

// Icônes vectorielles dessinées à la volée.
QIcon icon(const QString& name, const QColor& color = Color::text, int size = 20);
void paintIcon(class QPainter& p, const QString& name, const QRectF& r, const QColor& color);

// Logo : un nid aux couleurs des consoles qui porte un bouton « lecture » doré.
void paintLogo(class QPainter& p, const QRectF& r);
QPixmap logoPixmap(int size, qreal dpr = 1.0);

// Style d'un bouton principal teinté par la couleur d'accent.
QString primaryButtonStyle(const QColor& accent, bool compact = false);

} // namespace Theme
