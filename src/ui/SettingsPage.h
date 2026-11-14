#pragma once

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QTabWidget;

// Page "Paramètres" : bibliothèque, lancement, mises à jour, prérequis, à propos.
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = nullptr);
    void showTab(int index);
    enum Tab { LibraryTab = 0, LaunchTab = 1, UpdatesTab = 2, RequirementsTab = 3, AboutTab = 4 };

signals:
    void foldersChanged();
    void appearanceChanged();
    void languageChosen(const QString& lang); // "auto", "fr" ou "en"
    void message(const QString& text, bool error);

private:
    QWidget* buildLibraryTab();
    QWidget* buildLaunchTab();
    QWidget* buildUpdatesTab();
    QWidget* buildRequirementsTab();
    QWidget* buildAboutTab();
    void saveFolders();

    QTabWidget* m_tabs = nullptr;
    QListWidget* m_folders = nullptr;
    QListWidget* m_emuFolders = nullptr;
    QLabel* m_appUpdateStatus = nullptr;
    QPushButton* m_appUpdateApply = nullptr;
};
