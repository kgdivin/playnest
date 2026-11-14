#pragma once

#include "../core/Gamepad.h"

#include <QMainWindow>
#include <QModelIndex>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QProgressBar;
class QPushButton;
class QSlider;
class QStackedWidget;
class QTimer;
class QToolButton;
class CoverManager;
class DetailsPanel;
class EmulatorsPage;
class GameDelegate;
class AmbientWidget;
class HeroBand;
class HomePage;
class Organizer;
class Launcher;
class Library;
class LibraryFilter;
class LibraryModel;
class SavesPage;
class Scanner;
class SettingsPage;
class Sidebar;
class Toast;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void startup();
    void showPage(int page);
    void debugShow(const QString& what); // captures d'écran de développement : "organize", catégorie…
    enum Page { LibraryPage = 0, EmulatorsPageIdx = 1, SavesPageIdx = 2, SettingsPageIdx = 3 };

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;
    void changeEvent(QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    QWidget* buildSidebar();
    QWidget* buildLibraryPage();
    QWidget* buildHeader();
    QWidget* buildEmptyState();
    QWidget* buildStatusBar();
    QWidget* buildBanner();

    void setCategory(const QString& id);
    void updateHeader();
    void updateEmptyState();
    void updateGamepadState();
    void updateNavBadges();
    QString currentPath() const;
    void selectPath(const QString& path);
    void relayoutGrid();
    void updateHero();
    void goToGame(const QString& path);  // affiche un jeu ou un contenu, en changeant de catégorie si besoin
    void openOrganizer();
    void changeLanguage(const QString& lang); // "fr", "en" ou "auto" : relance Playnest dans cette langue
    void importGames(const QStringList& paths); // nouveaux jeux glissés ou choisis : rangés au bon endroit
    void chooseGamesToImport();
    void addPcGames(const QStringList& exes);

    void startScan();
    void launchGame(const QString& path, const QString& emulatorId = QString());
    void showGameMenu(const QString& path, const QPoint& globalPos);
    void showCoverMenu(const QString& path, const QPoint& globalPos);
    void toggleFavorite(const QString& path);
    void renameGame(const QString& path);
    void addFolder();
    void installEmulator(const QString& emuId, const QString& thenLaunchPath = QString());
    void showFirstRunSummary();
    void onGamepad(Gamepad::Action a);
    void toggleFullscreen();
    void sendKey(QWidget* target, int key, Qt::KeyboardModifiers mods = Qt::NoModifier);
    void notify(const QString& text, bool error = false);

    Library* m_lib;
    LibraryModel* m_model;
    LibraryFilter* m_filter;
    Scanner* m_scanner;
    Launcher* m_launcher;
    CoverManager* m_covers;
    Gamepad* m_pad;

    Sidebar* m_sidebar = nullptr;
    QToolButton* m_navEmus = nullptr;
    QToolButton* m_navSaves = nullptr;
    QToolButton* m_navSettings = nullptr;
    QToolButton* m_navTidy = nullptr;
    HeroBand* m_hero = nullptr;
    HomePage* m_home = nullptr;
    AmbientWidget* m_ambient = nullptr;
    QToolButton* m_langBtn = nullptr;
    bool m_onHome = false;
    Organizer* m_org = nullptr;
    bool m_importing = false;
    QString m_selectAfterScan;
    QStackedWidget* m_pages = nullptr;
    EmulatorsPage* m_emuPage = nullptr;
    SavesPage* m_savesPage = nullptr;
    SettingsPage* m_settingsPage = nullptr;

    QWidget* m_banner = nullptr;
    QLabel* m_bannerText = nullptr;
    QPushButton* m_bannerAction = nullptr;
    int m_bannerKind = 0; // 1 = mise à jour de l'application, 2 = mises à jour d'émulateurs

    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_sort = nullptr;
    QSlider* m_size = nullptr;
    QToolButton* m_detailsBtn = nullptr;
    QStackedWidget* m_stack = nullptr;
    QListView* m_view = nullptr;
    GameDelegate* m_delegate = nullptr;
    QLabel* m_noResults = nullptr;
    DetailsPanel* m_details = nullptr;
    QLabel* m_statusText = nullptr;
    QProgressBar* m_statusProgress = nullptr;
    QToolButton* m_cancelScan = nullptr;
    QLabel* m_playing = nullptr;
    QLabel* m_padIcon = nullptr;
    Toast* m_toast = nullptr;
    QTimer* m_playTimer = nullptr;
    QString m_pendingLaunch;
    QString m_pendingLaunchEmu;
    bool m_firstRunShown = false;
};
