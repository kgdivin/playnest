#include "Paths.h"

#include "AppInfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace {
QString g_dataDir;
bool g_portable = false;

bool writable(const QString& dir) {
    QDir().mkpath(dir);
    QFile probe(dir + QStringLiteral("/.write-test"));
    if (!probe.open(QIODevice::WriteOnly))
        return false;
    probe.close();
    probe.remove();
    return true;
}

void copyTree(const QString& from, const QString& to) {
    QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString src = it.next();
        const QString dst = to + src.mid(from.size());
        QDir().mkpath(QFileInfo(dst).absolutePath());
        QFile::copy(src, dst);
    }
}

// Les réglages et la bibliothèque gardent des chemins vers le dossier de données (émulateurs installés,
// copies de sauvegardes) : après un changement de nom du dossier, on les fait suivre.
void rewritePaths(const QString& oldDir, const QString& newDir) {
    QStringList files;
    for (const QString& sub : {QString(), QStringLiteral("/organize")})
        for (const QFileInfo& fi : QDir(newDir + sub).entryInfoList({QStringLiteral("*.ini"), QStringLiteral("*.json")}, QDir::Files))
            files << fi.absoluteFilePath();
    const QString oldNative = QDir::toNativeSeparators(oldDir), newNative = QDir::toNativeSeparators(newDir);
    QString oldEsc = oldNative, newEsc = newNative; // « \\ » dans les fichiers INI et JSON
    oldEsc.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    newEsc.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    for (const QString& path : files) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QString before = QString::fromUtf8(f.readAll());
        f.close();
        QString after = before;
        after.replace(oldEsc, newEsc, Qt::CaseInsensitive);
        after.replace(oldNative, newNative, Qt::CaseInsensitive);
        after.replace(QDir::fromNativeSeparators(oldDir), QDir::fromNativeSeparators(newDir), Qt::CaseInsensitive);
        if (after != before && f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(after.toUtf8());
            f.close();
        }
    }
}

// Reprend les données des anciens noms de l'application (Cartouche, Omnia).
void migrateOldData() {
    const QString name = QString::fromLatin1(AppInfo::kName);
    for (const char* old : AppInfo::kOldNames) {
        const QString oldName = QString::fromLatin1(old);
        if (!g_portable && !QFileInfo::exists(g_dataDir + QStringLiteral("/library.json"))) {
            QString oldDir = g_dataDir;
            oldDir.replace(name, oldName);
            if (oldDir != g_dataDir && QFileInfo(oldDir).isDir()) {
                QDir().mkpath(QFileInfo(g_dataDir).absolutePath());
                QDir().rmdir(g_dataDir);
                if (!QDir().rename(oldDir, g_dataDir))
                    copyTree(oldDir, g_dataDir);
                QDir().rmdir(QFileInfo(oldDir).absolutePath()); // dossier parent « Cartouche », s'il est vide
                QFile::remove(g_dataDir + QStringLiteral("/.lock"));
                const QString oldIni = g_dataDir + QLatin1Char('/') + oldName.toLower() + QStringLiteral(".ini");
                const QString newIni = g_dataDir + QLatin1Char('/') + name.toLower() + QStringLiteral(".ini");
                if (QFileInfo::exists(oldIni) && !QFileInfo::exists(newIni))
                    QFile::rename(oldIni, newIni);
                rewritePaths(oldDir, g_dataDir);
            }
        }
        // Mode portable (ou dossier déjà repris) : seul le fichier de réglages change de nom.
        const QString oldIni = g_dataDir + QLatin1Char('/') + oldName.toLower() + QStringLiteral(".ini");
        const QString newIni = g_dataDir + QLatin1Char('/') + name.toLower() + QStringLiteral(".ini");
        if (QFileInfo::exists(oldIni) && !QFileInfo::exists(newIni))
            QFile::rename(oldIni, newIni);
    }
}
} // namespace

namespace Paths {

void init() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    g_portable = QFileInfo::exists(exeDir + QStringLiteral("/portable.txt")) ||
                 QFileInfo::exists(exeDir + QStringLiteral("/portable"));
    if (g_portable) {
        g_dataDir = exeDir + QStringLiteral("/data");
        if (!writable(g_dataDir))
            g_portable = false; // ex. Program Files : repli sur le dossier utilisateur
    }
    if (!g_portable) {
        g_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (g_dataDir.isEmpty())
            g_dataDir = QDir::homePath() + QStringLiteral("/.playnest");
    }
    migrateOldData();
    for (const QString& d : {g_dataDir, coversDir(), iconsDir(), emulatorsDir(), downloadsDir(), stagingDir(),
                             rollbackDir(), backupsDir(), toolsDir()})
        ensureDir(d);
}

bool isPortable() { return g_portable; }

QString dataDir() { return g_dataDir; }
QString coversDir() { return g_dataDir + QStringLiteral("/covers"); }
QString iconsDir() { return g_dataDir + QStringLiteral("/covers/icons"); }
QString emulatorsDir() { return g_dataDir + QStringLiteral("/emulators"); }
QString downloadsDir() { return g_dataDir + QStringLiteral("/downloads"); }
QString stagingDir() { return g_dataDir + QStringLiteral("/staging"); }
QString rollbackDir() { return g_dataDir + QStringLiteral("/rollback"); }
QString g_backupsOverride;
QString backupsDir() {
    return g_backupsOverride.isEmpty() ? g_dataDir + QStringLiteral("/backups") : QDir::fromNativeSeparators(g_backupsOverride);
}
void setBackupsDir(const QString& dir) {
    g_backupsOverride = dir;
    if (!dir.isEmpty())
        QDir().mkpath(dir);
}
QString toolsDir() { return g_dataDir + QStringLiteral("/tools"); }
QString appDir() { return QCoreApplication::applicationDirPath(); }

QString settingsFile() { return g_dataDir + QLatin1Char('/') + QString::fromLatin1(AppInfo::kName).toLower() + QStringLiteral(".ini"); }
QString libraryFile() { return g_dataDir + QStringLiteral("/library.json"); }
QString logFile() { return g_dataDir + QLatin1Char('/') + QString::fromLatin1(AppInfo::kName).toLower() + QStringLiteral(".log"); }
QString lockFile() { return g_dataDir + QStringLiteral("/.lock"); }

QString ensureDir(const QString& path) {
    QDir().mkpath(path);
    return path;
}

QString toStored(const QString& absolute) {
    if (absolute.isEmpty())
        return {};
    const QString a = QDir::fromNativeSeparators(absolute);
    const QString base = QDir::fromNativeSeparators(g_dataDir) + QLatin1Char('/');
    if (a.startsWith(base, Qt::CaseInsensitive))
        return QStringLiteral("data:") + a.mid(base.size());
    return absolute;
}

QString fromStored(const QString& stored) {
    if (stored.startsWith(QLatin1String("data:")))
        return g_dataDir + QLatin1Char('/') + stored.mid(5);
    // Ancien chemin absolu vers une jaquette : on le rattache au dossier actuel si besoin.
    if (!stored.isEmpty() && !QFileInfo::exists(stored)) {
        const QString s = QDir::fromNativeSeparators(stored);
        const int i = s.lastIndexOf(QLatin1String("/covers/"));
        if (i >= 0) {
            const QString remapped = g_dataDir + s.mid(i);
            if (QFileInfo::exists(remapped))
                return remapped;
        }
    }
    return stored;
}

QString expand(const QString& templ, const QString& emuDir) {
    QString s = templ;
    const auto env = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_WIN
    const QString appData = env.value(QStringLiteral("APPDATA"), QDir::homePath() + QStringLiteral("/AppData/Roaming"));
    const QString localAppData = env.value(QStringLiteral("LOCALAPPDATA"), QDir::homePath() + QStringLiteral("/AppData/Local"));
#else
    const QString appData = env.value(QStringLiteral("XDG_DATA_HOME"), QDir::homePath() + QStringLiteral("/.local/share"));
    const QString localAppData = appData;
#endif
    s.replace(QStringLiteral("%APPDATA%"), QDir::fromNativeSeparators(appData), Qt::CaseInsensitive);
    s.replace(QStringLiteral("%LOCALAPPDATA%"), QDir::fromNativeSeparators(localAppData), Qt::CaseInsensitive);
    s.replace(QStringLiteral("%DOCUMENTS%"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), Qt::CaseInsensitive);
    s.replace(QStringLiteral("%HOME%"), QDir::homePath(), Qt::CaseInsensitive);
    s.replace(QStringLiteral("{emudir}"), QDir::fromNativeSeparators(emuDir));
    return QDir::cleanPath(s);
}

} // namespace Paths
