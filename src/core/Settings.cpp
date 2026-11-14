#include "Settings.h"
#include <QSet>
#include <QDir>

#include "Paths.h"
#include "Registry.h"

Settings& Settings::instance() {
    static Settings s;
    return s;
}

void Settings::init() {
    m_s = std::make_unique<QSettings>(Paths::settingsFile(), QSettings::IniFormat);
    Paths::setBackupsDir(m_s->value(QStringLiteral("saves/backupDir")).toString());
}

void Settings::sync() {
    if (m_s)
        m_s->sync();
}

namespace {
QStringList uniqueFolders(const QStringList& in) {
    QStringList out;
    QSet<QString> seen;
    for (const QString& f : in) {
        if (f.trimmed().isEmpty())
            continue;
        const QString key = QDir::cleanPath(QDir::fromNativeSeparators(f.trimmed())).toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        out << QDir::toNativeSeparators(QDir::cleanPath(QDir::fromNativeSeparators(f.trimmed())));
    }
    return out;
}
} // namespace

QStringList Settings::romFolders() const { return uniqueFolders(m_s->value(QStringLiteral("library/folders")).toStringList()); }
void Settings::setRomFolders(const QStringList& f) { m_s->setValue(QStringLiteral("library/folders"), uniqueFolders(f)); }

QStringList Settings::extraEmulatorFolders() const {
    return m_s->value(QStringLiteral("emulators/extraFolders")).toStringList();
}
void Settings::setExtraEmulatorFolders(const QStringList& f) {
    m_s->setValue(QStringLiteral("emulators/extraFolders"), f);
}

QString Settings::emulatorPath(const QString& id) const {
    return m_s->value(QStringLiteral("emulators/path_") + id).toString();
}
void Settings::setEmulatorPath(const QString& id, const QString& path) {
    if (path.isEmpty())
        m_s->remove(QStringLiteral("emulators/path_") + id);
    else
        m_s->setValue(QStringLiteral("emulators/path_") + id, path);
}

QString Settings::customArgs(const QString& id, bool fullscreen) const {
    return m_s->value(QStringLiteral("emulators/args_%1_%2").arg(id, fullscreen ? "fs" : "win")).toString();
}
void Settings::setCustomArgs(const QString& id, bool fullscreen, const QString& args) {
    const QString key = QStringLiteral("emulators/args_%1_%2").arg(id, fullscreen ? "fs" : "win");
    if (args.trimmed().isEmpty())
        m_s->remove(key);
    else
        m_s->setValue(key, args);
}

QString Settings::defaultEmulator(const QString& systemId) const {
    const QString v = m_s->value(QStringLiteral("systems/default_") + systemId).toString();
    if (!v.isEmpty() && Registry::instance().emulator(v))
        return v;
    return Registry::instance().builtinDefaultEmulator(systemId);
}
void Settings::setDefaultEmulator(const QString& systemId, const QString& emuId) {
    m_s->setValue(QStringLiteral("systems/default_") + systemId, emuId);
}

bool Settings::launchFullscreen() const { return m_s->value(QStringLiteral("launch/fullscreen"), true).toBool(); }
void Settings::setLaunchFullscreen(bool v) { m_s->setValue(QStringLiteral("launch/fullscreen"), v); }
bool Settings::minimizeWhileRunning() const { return m_s->value(QStringLiteral("launch/minimize"), true).toBool(); }
void Settings::setMinimizeWhileRunning(bool v) { m_s->setValue(QStringLiteral("launch/minimize"), v); }
bool Settings::downloadCovers() const { return m_s->value(QStringLiteral("covers/download"), true).toBool(); }
void Settings::setDownloadCovers(bool v) { m_s->setValue(QStringLiteral("covers/download"), v); }
bool Settings::gamepadEnabled() const { return m_s->value(QStringLiteral("ui/gamepad"), true).toBool(); }
void Settings::setGamepadEnabled(bool v) { m_s->setValue(QStringLiteral("ui/gamepad"), v); }
bool Settings::scanOnStartup() const { return m_s->value(QStringLiteral("library/scanOnStartup"), true).toBool(); }
void Settings::setScanOnStartup(bool v) { m_s->setValue(QStringLiteral("library/scanOnStartup"), v); }
bool Settings::hideAddons() const { return m_s->value(QStringLiteral("library/hideAddons"), true).toBool(); }
void Settings::setHideAddons(bool v) { m_s->setValue(QStringLiteral("library/hideAddons"), v); }
QString Settings::coverLanguage() const { return m_s->value(QStringLiteral("covers/language"), QStringLiteral("FR")).toString(); }
void Settings::setCoverLanguage(const QString& v) { m_s->setValue(QStringLiteral("covers/language"), v); }

int Settings::cardSize() const { return m_s->value(QStringLiteral("ui/cardSize"), 170).toInt(); }
void Settings::setCardSize(int v) { m_s->setValue(QStringLiteral("ui/cardSize"), v); }
QString Settings::sortMode() const { return m_s->value(QStringLiteral("ui/sort"), QStringLiteral("title")).toString(); }
void Settings::setSortMode(const QString& v) { m_s->setValue(QStringLiteral("ui/sort"), v); }
QString Settings::lastCategory() const { return m_s->value(QStringLiteral("ui/category"), QStringLiteral("__all")).toString(); }
void Settings::setLastCategory(const QString& v) { m_s->setValue(QStringLiteral("ui/category"), v); }
bool Settings::detailsVisible() const { return m_s->value(QStringLiteral("ui/details"), true).toBool(); }
void Settings::setDetailsVisible(bool v) { m_s->setValue(QStringLiteral("ui/details"), v); }

QByteArray Settings::windowGeometry() const { return m_s->value(QStringLiteral("ui/geometry")).toByteArray(); }
void Settings::setWindowGeometry(const QByteArray& g) { m_s->setValue(QStringLiteral("ui/geometry"), g); }

bool Settings::firstRunDone() const { return m_s->value(QStringLiteral("app/firstRunDone"), false).toBool(); }
void Settings::setFirstRunDone(bool v) { m_s->setValue(QStringLiteral("app/firstRunDone"), v); }

QVariant Settings::value(const QString& key, const QVariant& def) const { return m_s->value(key, def); }
void Settings::setValue(const QString& key, const QVariant& v) { m_s->setValue(key, v); }
void Settings::remove(const QString& key) { m_s->remove(key); }
