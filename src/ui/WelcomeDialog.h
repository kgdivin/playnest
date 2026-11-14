#pragma once

#include <QDialog>

class QLabel;
class QPushButton;
class QRadioButton;
class QStackedWidget;

// Premier lancement : langue, dossier de jeux, puis trois lignes sur le fonctionnement.
// Pensé pour quelqu'un qui reçoit Playnest d'un ami et n'a encore rien configuré.
class WelcomeDialog : public QDialog {
    Q_OBJECT
public:
    explicit WelcomeDialog(QWidget* parent = nullptr);

    // Faut-il montrer l'accueil ? (aucun réglage, aucune bibliothèque)
    static bool needed();
    // Dossier de jeux choisi (vide = « plus tard »).
    QString gamesFolder() const { return m_folder; }

private:
    void retranslate();
    void go(int page);
    void finish();

    QStackedWidget* m_stack;
    QPushButton* m_back;
    QPushButton* m_next;
    QLabel* m_step;
    // Page 1
    QLabel* m_hello;
    QLabel* m_tagline;
    QLabel* m_langLabel;
    QPushButton* m_fr;
    QPushButton* m_en;
    // Page 2
    QLabel* m_whereTitle;
    QLabel* m_whereText;
    QRadioButton* m_create;
    QLabel* m_createHint;
    QRadioButton* m_existing;
    QLabel* m_existingHint;
    QPushButton* m_browse;
    QRadioButton* m_later;
    QString m_picked;
    // Page 3
    QLabel* m_howTitle;
    QLabel* m_how[3][2];
    QLabel* m_legal;
    QString m_folder;
};
