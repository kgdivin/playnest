#pragma once

#include <QByteArray>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <memory>

// Paramètres persistants (fichier INI dans le dossier de données).
class Settings {
public:
    static Settings& instance();
    void init();
    void sync();

    QStringList romFolders() const;
    void setRomFolders(const QStringList& folders);

    QStringList extraEmulatorFolders() const;
    void setExtraEmulatorFolders(const QStringList& folders);

    QString emulatorPath(const QString& emuId) const;
    void setEmulatorPath(const QString& emuId, const QString& path);

    // Arguments personnalisés (vide = arguments par défaut du registre).
    QString customArgs(const QString& emuId, bool fullscreen) const;
    void setCustomArgs(const QString& emuId, bool fullscreen, const QString& args);

    QString defaultEmulator(const QString& systemId) const; // résout vers le défaut intégré
    void setDefaultEmulator(const QString& systemId, const QString& emuId);

    bool launchFullscreen() const;
    void setLaunchFullscreen(bool v);
    bool minimizeWhileRunning() const;
    void setMinimizeWhileRunning(bool v);
    bool downloadCovers() const;
    void setDownloadCovers(bool v);
    bool gamepadEnabled() const;
    void setGamepadEnabled(bool v);
    bool scanOnStartup() const;
    void setScanOnStartup(bool v);
    bool hideAddons() const;
    void setHideAddons(bool v);
    QString coverLanguage() const; // FR, EN, US, JA, DE, ES, IT...
    void setCoverLanguage(const QString& v);

    int cardSize() const;
    void setCardSize(int v);
    QString sortMode() const;
    void setSortMode(const QString& v);
    QString lastCategory() const;
    void setLastCategory(const QString& v);
    bool detailsVisible() const;
    void setDetailsVisible(bool v);

    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& g);

    bool firstRunDone() const;
    void setFirstRunDone(bool v);

    // Accès générique (mises à jour, sauvegardes, détection…).
    QVariant value(const QString& key, const QVariant& def = {}) const;
    void setValue(const QString& key, const QVariant& v);
    void remove(const QString& key);

private:
    Settings() = default;
    std::unique_ptr<QSettings> m_s;
};
