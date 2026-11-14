#pragma once

#include <QVector>
#include <QWidget>

class Library;
class QVariantAnimation;

// Accueil : le mur des consoles, de la NES (1983) à la Switch 2 (2025). Chaque console est une
// tuile à sa couleur, avec son nombre de jeux ; un clic ouvre sa bibliothèque.
class HomePage : public QWidget {
    Q_OBJECT
public:
    explicit HomePage(Library* lib, QWidget* parent = nullptr);

    void refresh(); // compteurs et temps de jeu

signals:
    void consoleChosen(const QString& systemId);
    void addGamesRequested();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void keyPressEvent(QKeyEvent* e) override;
    void focusInEvent(QFocusEvent*) override { update(); }
    void focusOutEvent(QFocusEvent*) override { update(); }

private:
    struct Tile {
        QString id; // vide = tuile « Ajouter des jeux »
        QRectF rect;
        int games = 0;
        qreal lift = 0; // 0 → 1 au survol
    };
    void layoutTiles();
    int tileAt(const QPointF& pos) const;
    void activate(int index);
    void setHover(int index);
    void paintTile(class QPainter& p, const Tile& t, bool focused) const;

    Library* m_lib;
    QVector<Tile> m_tiles;
    int m_columns = 1;
    int m_hover = -1;
    int m_pressed = -1;
    int m_focus = 0;
    qint64 m_playSeconds = 0;
    int m_totalGames = 0;
    QVariantAnimation* m_anim;
};
