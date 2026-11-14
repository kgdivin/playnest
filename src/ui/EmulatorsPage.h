#pragma once

#include <QHash>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
class EmulatorCard;
class ToolCard;

// Page "Émulateurs" : outils nécessaires, émulateur par console, état et mises à jour de chaque émulateur.
class EmulatorsPage : public QWidget {
    Q_OBJECT
public:
    // Installe une archive déjà téléchargée, après confirmation. expectedId vide = deviner l'émulateur.
    static bool installFromFile(QWidget* parent, const QString& file, const QString& expectedId = {});
    explicit EmulatorsPage(QWidget* parent = nullptr);
    void refresh();

signals:
    void message(const QString& text, bool error);

private:
    void editArguments(const QString& emuId);
    QLabel* m_summary = nullptr;
    QPushButton* m_detect = nullptr;
    QPushButton* m_check = nullptr;
    QPushButton* m_updateAll = nullptr;
    QHash<QString, EmulatorCard*> m_cards;
    QVBoxLayout* m_cardsBox = nullptr;
    QHash<QString, ToolCard*> m_tools;
    QHash<QString, QComboBox*> m_defaults;
};
