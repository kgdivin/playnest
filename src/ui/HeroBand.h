#pragma once

#include <QPixmap>
#include <QWidget>

class QPushButton;
class Library;
struct Game;

// Bandeau « Reprendre » en tête de bibliothèque : le dernier jeu lancé, en grand,
// sur un fond tiré de sa jaquette.
class HeroBand : public QWidget {
    Q_OBJECT
public:
    explicit HeroBand(Library* lib, QWidget* parent = nullptr);
    // Choisit le jeu le plus récemment joué ; renvoie false s'il n'y en a aucun.
    bool refresh();
    QString path() const { return m_path; }
    void setRunning(bool running);

signals:
    void playRequested(const QString& path);
    void selectRequested(const QString& path);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void layoutButton();
    Library* m_lib;
    QString m_path;
    QString m_image;
    QPixmap m_blur;
    QPixmap m_cover;
    QPushButton* m_play;
};
