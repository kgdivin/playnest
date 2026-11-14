#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;
class QVBoxLayout;

// Page "Sauvegardes" : où sont les sauvegardes, copies existantes, restauration et options.
class SavesPage : public QWidget {
    Q_OBJECT
public:
    explicit SavesPage(QWidget* parent = nullptr);
    void refresh();
    void activate(); // à l'ouverture : recalcule les tailles

signals:
    void message(const QString& text, bool error);

private:
    void restoreSelected();
    QLabel* m_status = nullptr;
    QPushButton* m_backup = nullptr;
    QVBoxLayout* m_sourcesBox = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_restore = nullptr;
    QPushButton* m_delete = nullptr;
};
