#pragma once

#include "../core/Organizer.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTreeWidget;

// « Ranger mes fichiers » : aperçu de ce qui va bouger, puis rangement avec progression.
class OrganizeDialog : public QDialog {
    Q_OBJECT
public:
    OrganizeDialog(Organizer* organizer, QWidget* parent = nullptr);

private:
    void rebuildPlan();
    void start();
    void onFinished(const Organizer::Result& r);

    Organizer* m_org;
    Organizer::Plan m_plan;
    QStackedWidget* m_stack;
    QLineEdit* m_root;
    QCheckBox* m_games;
    QCheckBox* m_emus;
    QCheckBox* m_saves;
    QTreeWidget* m_tree;
    QLabel* m_summary;
    QLabel* m_warnings;
    QPushButton* m_go;
    QPushButton* m_cancel;
    QLabel* m_runTitle;
    QLabel* m_runFile;
    QProgressBar* m_bar;
    QPushButton* m_stop;
    QLabel* m_doneText;
    QPushButton* m_undo;
    QString m_journal;
};
