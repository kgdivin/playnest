#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QToolButton;
class Library;
struct Game;

class CoverView : public QWidget {
    Q_OBJECT
public:
    explicit CoverView(QWidget* parent = nullptr);
    void setGame(const QString& systemId, const QString& title, const QString& image);
    QSize sizeHint() const override { return {300, 400}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString m_system, m_title, m_image;
};

// Panneau de droite : informations du jeu sélectionné et bouton Jouer.
class DetailsPanel : public QWidget {
    Q_OBJECT
public:
    explicit DetailsPanel(Library* lib, QWidget* parent = nullptr);
    void showGame(const QString& path);
    QString currentPath() const { return m_path; }
    void refresh() { showGame(m_path); }
    void setRunning(const QString& runningPath);

signals:
    void playRequested(const QString& path);
    void coopRequested(const QString& path); // jeu en ligne à plusieurs (kit de co-op)
    void favoriteToggled(const QString& path);
    void openFolderRequested(const QString& path);
    void coverMenuRequested(const QString& path, const QPoint& globalPos);
    void emulatorChosen(const QString& path, const QString& emuId);
    void installRequested(const QString& emuId);
    void showGameRequested(const QString& path); // aller au jeu de base, ou à un contenu

private:
    Library* m_lib;
    QString m_path;
    QString m_running;
    CoverView* m_cover;
    QLabel* m_title;
    QLabel* m_system;
    QLabel* m_facts;
    QLabel* m_addonInfo;     // « Contenu pour … » ou contenu intégré (.nup)
    QPushButton* m_baseBtn;  // aller au jeu de base
    QLabel* m_addonsTitle;
    QWidget* m_addonsBox;    // liste des mises à jour et DLC du jeu
    QComboBox* m_emu;
    QLabel* m_emuLabel;
    QLabel* m_emuHint;
    QPushButton* m_install;
    QPushButton* m_play;
    QPushButton* m_coop;
    QToolButton* m_fav;
    QToolButton* m_folder;
    QToolButton* m_coverBtn;
    QWidget* m_content;
    QScrollArea* m_scroll;
    QLabel* m_empty;
};
