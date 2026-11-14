#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QStyledItemDelegate>

// Chargement asynchrone et cache des images (jaquettes, icônes).
class ImageCache : public QObject {
    Q_OBJECT
public:
    static ImageCache& instance();
    // Renvoie l'image redimensionnée si elle est en cache, sinon lance son chargement.
    QPixmap get(const QString& file, const QSize& box, qreal dpr, bool pixelArt);
    void invalidate(const QString& file);

signals:
    void loaded();

private:
    ImageCache();
    QString key(const QString& file, const QSize& box, qreal dpr) const;
    QSet<QString> m_pending;
    QSet<QString> m_failed;
    QHash<QString, qint64> m_stamp;
};

// Dessine une "boîte de jeu" : jaquette (ou étiquette générée), tranche colorée
// de la console, titre et information secondaire.
class GameDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit GameDelegate(QObject* parent = nullptr);

    void setCardWidth(int w) { m_cardWidth = w; }
    int cardWidth() const { return m_cardWidth; }
    QSize cardSize() const;
    static constexpr int kSpacing = 22;
    void setShowSystem(bool on) { m_showSystem = on; }
    void setRunningPath(const QString& p) { m_running = p; }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;

    // Étiquette générée quand aucune jaquette n'est disponible (réutilisée par le panneau de détails).
    static void paintLabel(QPainter* p, const QRectF& r, const QString& systemId, const QString& title,
                           const QPixmap& icon, bool large);

private:
    int m_cardWidth = 170;
    bool m_showSystem = true;
    QString m_running;
};
