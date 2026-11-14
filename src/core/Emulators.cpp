#include "Emulators.h"

#include "Archive.h"
#include "Library.h"
#include "Net.h"
#include "Paths.h"
#include "Saves.h"
#include "Settings.h"
#include "Tools.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQueue>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <functional>
#include <memory>
#include <optional>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#endif

struct InstallJob {
    QString id;
    QString name;
    QString archive;
    QString version;
    QString assetName;
    QStringList exeNames;
    QString currentDir;            // vide = nouvelle installation
    QString dataDir;               // dossier de données (jamais écrasé s'il est dans le dossier de l'émulateur)
    QVector<SaveSource> backupSources;
    QString backupFile;
    bool keepArchive = false;      // fichier fourni par l'utilisateur : on ne le supprime jamais
    bool singleExe = false;        // le fichier est l'exécutable lui-même (pas une archive)
    QString versionProbe;          // pour lire la version dans le nouvel exécutable
};
struct InstallResult {
    bool ok = false;
    QString message;
    QString exe;
    QString version;
};


namespace {

const char* kMarker = ".playnest-version";

bool isFile(const QString& p) {
    const QFileInfo fi(p);
    return !p.isEmpty() && fi.exists() && fi.isFile();
}

// ---------------------------------------------------------------------------
// Détection : parcours en largeur, profondeur bornée, des emplacements probables.
// ---------------------------------------------------------------------------
struct Root {
    QString path;
    int depth;
};

QVector<Root> detectionRoots() {
    QVector<Root> roots;
    auto add = [&](const QString& p, int d) {
        if (!p.isEmpty() && QFileInfo(p).isDir())
            roots.push_back({QDir::cleanPath(p), d});
    };
    add(Paths::emulatorsDir(), 4);
    // Émulateurs rangés par « Ranger mes fichiers » (même si les réglages ont été perdus).
    QStringList organizedRoots{QDir::fromNativeSeparators(Settings::instance().value(QStringLiteral("organize/root")).toString()),
                               QDir::homePath() + QStringLiteral("/Jeux Nintendo"), QDir::homePath() + QStringLiteral("/Nintendo Games")};
    organizedRoots.removeAll(QString());
    organizedRoots.removeDuplicates();
    for (const QString& root : organizedRoots)
        for (const char* name : {"Émulateurs", "Emulators"})
            add(root + QLatin1Char('/') + QString::fromUtf8(name), 3);
    add(Paths::appDir() + QStringLiteral("/emulators"), 4);
    for (const auto& f : Settings::instance().extraEmulatorFolders())
        add(f, 5);
    add(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation), 4);
    add(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), 4);
    add(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), 3);
#ifdef Q_OS_WIN
    const auto env = QProcessEnvironment::systemEnvironment();
    add(env.value(QStringLiteral("LOCALAPPDATA")) + QStringLiteral("/Programs"), 3);
    add(env.value(QStringLiteral("ProgramFiles")), 3);
    add(env.value(QStringLiteral("ProgramFiles(x86)")), 2);
    for (const QFileInfo& drive : QDir::drives()) {
        const QStorageInfo si(drive.absoluteFilePath());
        if (si.isValid() && si.isReady())
            add(drive.absoluteFilePath(), 2);
    }
#else
    add(QDir::homePath() + QStringLiteral("/Applications"), 3);
#ifdef Q_OS_MACOS
    add(QStringLiteral("/Applications"), 2);
#endif
    add(QDir::homePath() + QStringLiteral("/AppImages"), 2);
    add(QDir::homePath() + QStringLiteral("/.local/bin"), 1);
    add(QDir::homePath() + QStringLiteral("/Emulators"), 4);
    add(QStringLiteral("/opt"), 2);
    add(QStringLiteral("/usr/bin"), 0);
    add(QStringLiteral("/usr/local/bin"), 0);
#endif
    return roots;
}

// Fichiers dont le nom varie d'une version à l'autre (Linux : « mGBA-0.10.5-appimage-x64.appimage » ;
// macOS : « Dolphin.app ») : reconnus par un mot du nom, et rangés sous le nom d'exécutable habituel.
struct LooseName {
    QString key;         // nom d'exécutable sous lequel l'émulateur est cherché ensuite
    QStringList needles; // « mgba », « dolphin »…
};

QString looseKey(const QString& fileName, const QVector<LooseName>& loose) {
    const QString n = fileName.toLower();
    for (const LooseName& l : loose)
        for (const QString& needle : l.needles) {
            const int at = n.indexOf(needle);
            if (at >= 0 && (at == 0 || !n[at - 1].isLetter()) && (at + needle.size() >= n.size() || !n[at + needle.size()].isLetter()))
                return l.key;
        }
    return {};
}

// Programme réel d'une application macOS : Dolphin.app/Contents/MacOS/Dolphin.
QString bundleExecutable(const QString& app) {
    const QDir mac(app + QStringLiteral("/Contents/MacOS"));
    const QString expected = mac.filePath(QFileInfo(app).completeBaseName());
    if (QFileInfo(expected).isFile())
        return expected;
    const QFileInfoList files = mac.entryInfoList(QDir::Files | QDir::Executable, QDir::Name);
    return files.isEmpty() ? QString() : files.first().absoluteFilePath();
}

QHash<QString, QString> scanForExecutables(const QVector<Root>& roots, const QSet<QString>& names, const QVector<LooseName>& loose) {
    static const QSet<QString> skip = {QStringLiteral("windows"), QStringLiteral("$recycle.bin"),
                                       QStringLiteral("system volume information"), QStringLiteral("winsxs"),
                                       QStringLiteral("node_modules"), QStringLiteral(".git"), QStringLiteral("appdata"),
                                       QStringLiteral("programdata"), QStringLiteral("staging"), QStringLiteral("rollback"),
                                       QStringLiteral("downloads.tmp")};
    const QString ownStaging = QDir::cleanPath(Paths::stagingDir()).toLower();
    const QString ownRollback = QDir::cleanPath(Paths::rollbackDir()).toLower();
    QHash<QString, QString> found;
    QHash<QString, QPair<int, QDateTime>> foundAt; // racine où il a été trouvé, date du fichier
    QSet<QString> visitedDirs;
    int budget = 120000; // garde-fou : nombre maximal de dossiers parcourus
    for (int ri = 0; ri < roots.size(); ++ri) {
        const Root& root = roots[ri];
        QQueue<QPair<QString, int>> queue;
        queue.enqueue({root.path, 0});
        while (!queue.isEmpty() && budget > 0) {
            const auto [dir, depth] = queue.dequeue();
            const QString key = dir.toLower();
            if (visitedDirs.contains(key) || key.startsWith(ownStaging) || key.startsWith(ownRollback))
                continue;
            visitedDirs.insert(key);
            --budget;
            const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
            for (const QFileInfo& fi : entries) {
                if (fi.isDir() && fi.suffix().compare(QLatin1String("app"), Qt::CaseInsensitive) == 0) {
                    // Application macOS : on ne descend pas dedans, on retient son programme.
                    QString n = fi.fileName().toLower();
                    if (!names.contains(n))
                        n = looseKey(fi.completeBaseName(), loose);
                    const QString exe = n.isEmpty() ? QString() : bundleExecutable(fi.absoluteFilePath());
                    if (!exe.isEmpty() && !found.contains(n)) {
                        found.insert(n, exe);
                        foundAt.insert(n, {ri, fi.lastModified()});
                    }
                } else if (fi.isDir()) {
                    if (depth < root.depth && !skip.contains(fi.fileName().toLower()))
                        queue.enqueue({fi.absoluteFilePath(), depth + 1});
                } else {
                    QString n = fi.fileName().toLower();
                    if (!names.contains(n)) {
                        if (!n.endsWith(QLatin1String(".appimage")))
                            continue;
                        n = looseKey(fi.completeBaseName(), loose); // AppImage téléchargée telle quelle
                        if (n.isEmpty())
                            continue;
                    }
                    // Deux copies dans le même dossier racine (ex. deux builds de Lichii) : la plus récente gagne.
                    QDateTime t = fi.lastModified();
                    if (fi.birthTime().isValid() && fi.birthTime() > t)
                        t = fi.birthTime();
                    const auto prev = foundAt.value(n, {-1, QDateTime()});
                    if (!found.contains(n) || (prev.first == ri && t > prev.second)) {
                        found.insert(n, QDir::toNativeSeparators(fi.absoluteFilePath()));
                        foundAt.insert(n, {ri, t});
                    }
                }
            }
        }
    }
    return found;
}

#ifdef Q_OS_WIN
QString peProductVersion(const QString& exe) {
    const std::wstring w = QDir::toNativeSeparators(exe).toStdWString();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(w.c_str(), &handle);
    if (!size)
        return {};
    QByteArray buf(int(size), 0);
    if (!GetFileVersionInfoW(w.c_str(), 0, size, buf.data()))
        return {};
    struct LangCp { WORD lang; WORD cp; }* trans = nullptr;
    UINT len = 0;
    if (VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&trans), &len) && len >= sizeof(LangCp)) {
        const QString key = QStringLiteral("\\StringFileInfo\\%1%2\\ProductVersion")
                                .arg(trans[0].lang, 4, 16, QLatin1Char('0'))
                                .arg(trans[0].cp, 4, 16, QLatin1Char('0'));
        wchar_t* val = nullptr;
        UINT vlen = 0;
        const std::wstring wk = key.toStdWString();
        if (VerQueryValueW(buf.data(), wk.c_str(), reinterpret_cast<LPVOID*>(&val), &vlen) && vlen > 1) {
            const QString v = QString::fromWCharArray(val).trimmed();
            if (!v.isEmpty() && v != QLatin1String("0.0.0") && v != QLatin1String("0.0.0.0"))
                return v;
        }
    }
    return {};
}
#endif

// Jetons d'un nom d'archive, sans numéros de version (pour retrouver la même "saveur" : msvc, gcc, pgo…).
QSet<QString> flavorTokens(const QString& name) {
    QSet<QString> out;
    static const QRegularExpression split(QStringLiteral("[-_. ]+"));
    static const QRegularExpression ver(QStringLiteral("^(v?\\d+|rc\\d*|zip|7z|exe)$"), QRegularExpression::CaseInsensitiveOption);
    for (const QString& t : name.toLower().split(split, Qt::SkipEmptyParts))
        if (!ver.match(t).hasMatch())
            out.insert(t);
    return out;
}

int chooseAsset(const QStringList& patterns, const QStringList& names, const QString& currentFlavor) {
    static const QRegularExpression junk(QStringLiteral("(?i)(pdb|debug|symbols|source|dbgsym|arm64)"));
    const QSet<QString> cur = flavorTokens(currentFlavor);
    int best = -1, bestScore = -1, bestPattern = 1 << 20;
    for (int i = 0; i < names.size(); ++i) {
        if (junk.match(names[i]).hasMatch())
            continue;
        int pat = -1;
        for (int p = 0; p < patterns.size(); ++p)
            if (QRegularExpression(patterns[p]).match(names[i]).hasMatch()) {
                pat = p;
                break;
            }
        if (pat < 0)
            continue;
        const int score = cur.isEmpty() ? 0 : int((flavorTokens(names[i]) & cur).size());
        if (score > bestScore || (score == bestScore && pat < bestPattern)) {
            best = i;
            bestScore = score;
            bestPattern = pat;
        }
    }
    return best;
}

QString readText(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

bool writeText(const QString& path, const QString& text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(text.toUtf8());
    return true;
}

// Marqueur de version écrit dans le dossier de l'émulateur ; ceux des anciens noms de l'application restent lus.
QString readMarker(const QString& dir) {
    for (const char* marker : {kMarker, ".cartouche-version", ".omnia-version"}) {
        const QString v = readText(dir + QLatin1Char('/') + QLatin1String(marker));
        if (!v.isEmpty())
            return v;
    }
    return {};
}

QString findExeIn(const QString& root, const QStringList& exeNames, int maxDepth) {
    QQueue<QPair<QString, int>> q;
    q.enqueue({root, 0});
    while (!q.isEmpty()) {
        const auto [dir, depth] = q.dequeue();
        for (const QString& exe : exeNames) {
            const QString p = dir + QLatin1Char('/') + exe;
            if (isFile(p))
                return p;
        }
        // Recherche insensible à la casse.
        const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& fi : entries) {
            if (fi.isFile()) {
                for (const QString& exe : exeNames)
                    if (fi.fileName().compare(exe, Qt::CaseInsensitive) == 0)
                        return fi.absoluteFilePath();
            } else if (depth < maxDepth) {
                q.enqueue({fi.absoluteFilePath(), depth + 1});
            }
        }
    }
    return {};
}

// Remplace les fichiers de l'émulateur en conservant une copie des fichiers écrasés.
InstallResult runInstall(InstallJob job) {
    InstallResult res;
    if (!job.backupSources.isEmpty()) {
        QString err;
        qint64 bytes = 0;
        // Aucune sauvegarde encore (émulateur jamais utilisé) : rien à protéger, la mise à jour continue.
        if (!SaveManager::doBackup(job.backupSources, job.backupFile,
                                   QObject::tr("Avant la mise à jour de %1").arg(job.name), true, &err, &bytes) &&
            err != QObject::tr("aucune sauvegarde à copier")) {
            res.message = QObject::tr("La copie de sécurité de tes sauvegardes a échoué (%1). Mise à jour annulée, rien n'a été modifié.").arg(err);
            return res;
        }
    }
    const QString staging = Paths::stagingDir() + QLatin1Char('/') + job.id;
    Archive::removeTree(staging);
    QString err;
    // Nom sous lequel une AppImage est installée : « mgba-qt.AppImage ».
    QString appImageName;
    for (const QString& n : job.exeNames)
        if (appImageName.isEmpty() && n.endsWith(QLatin1String(".AppImage"), Qt::CaseInsensitive))
            appImageName = n;
    const bool appImage = job.archive.endsWith(QLatin1String(".appimage"), Qt::CaseInsensitive) && !appImageName.isEmpty();
    if (job.singleExe || appImage) { // fichier seul : il prend la place de l'exécutable actuel
        QDir().mkpath(staging);
        const QString single = staging + QLatin1Char('/') + (appImage ? appImageName : job.exeNames.value(0));
        if (QFile::copy(job.archive, single))
            QFile::setPermissions(single, QFile::permissions(single) | QFile::ExeOwner | QFile::ExeUser | QFile::ExeGroup | QFile::ExeOther);
        if (!QFileInfo::exists(single)) {
            res.message = QObject::tr("Copie impossible de %1.").arg(QFileInfo(job.archive).fileName());
            return res;
        }
    } else if (!Archive::extract(job.archive, staging, &err)) {
        Archive::removeTree(staging);
        res.message = QObject::tr("Extraction impossible : %1").arg(err);
        return res;
    }
    QString exeInStaging = findExeIn(staging, job.exeNames, 4);
    if (exeInStaging.isEmpty() && !appImageName.isEmpty()) {
        // Archive qui contient une AppImage au nom variable (melonDS) : elle prend le nom habituel.
        QDirIterator it(staging, {QStringLiteral("*.AppImage"), QStringLiteral("*.appimage")}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) {
            const QString inner = it.next();
            const QString renamed = QFileInfo(inner).absolutePath() + QLatin1Char('/') + appImageName;
            if (QFile::rename(inner, renamed)) {
                QFile::setPermissions(renamed, QFile::permissions(renamed) | QFile::ExeOwner | QFile::ExeUser | QFile::ExeGroup | QFile::ExeOther);
                exeInStaging = renamed;
            }
        }
    }
    if (exeInStaging.isEmpty()) {
        Archive::removeTree(staging);
        res.message = QObject::tr("L'archive ne contient pas %1.").arg(job.exeNames.join(QStringLiteral(", ")));
        return res;
    }
    const QString newRoot = QFileInfo(exeInStaging).absolutePath();
    if (job.version.isEmpty()) // fichier local : version lue dans l'exécutable, sinon dans le nom de l'archive
        job.version = Archive::probeVersion(exeInStaging, job.versionProbe);
    if (job.version.isEmpty())
        job.version = Archive::versionFromName(job.assetName);
#ifdef Q_OS_WIN
    if (job.version.isEmpty()) // version inscrite dans l'exécutable (Azahar, Dolphin…)
        job.version = peProductVersion(exeInStaging);
#endif
    // Version inconnue : rien n'est inscrit, la détection habituelle s'en charge ensuite.
    res.version = job.version.isEmpty() ? QObject::tr("version inconnue") : job.version;
    const QString target = job.currentDir.isEmpty() ? Paths::emulatorsDir() + QLatin1Char('/') + job.id : job.currentDir;
    QDir().mkpath(target);

    const QString rb = Paths::rollbackDir() + QLatin1Char('/') + job.id;
    Archive::removeTree(rb);
    QDir().mkpath(rb + QStringLiteral("/files"));
    writeText(rb + QStringLiteral("/target.txt"), target);
    writeText(rb + QStringLiteral("/marker.txt"), readMarker(target));

    const QStringList files = Archive::listFiles(newRoot);
    QStringList added, replaced, kept;
    // Réglages et données de l'utilisateur : une archive ne les écrase jamais.
    const QString dataPrefix = QDir::fromNativeSeparators(QDir::cleanPath(job.dataDir)).toLower() + QLatin1Char('/');
    static const QStringList userDirs = {QStringLiteral("user"), QStringLiteral("portable"), QStringLiteral("config"),
                                         QStringLiteral("data"), QStringLiteral("save"), QStringLiteral("saves"),
                                         QStringLiteral("nand"), QStringLiteral("sdmc"), QStringLiteral("keys")};
    static const QStringList configExt = {QStringLiteral("ini"), QStringLiteral("cfg"), QStringLiteral("toml"),
                                          QStringLiteral("xml"), QStringLiteral("json"), QStringLiteral("keys")};
    auto isUserFile = [&](const QString& rel, const QString& dst) {
        if (!job.dataDir.isEmpty() && QDir::fromNativeSeparators(dst).toLower().startsWith(dataPrefix))
            return true;
        const QString first = rel.section(QLatin1Char('/'), 0, 0).toLower();
        if (rel.contains(QLatin1Char('/')) && userDirs.contains(first))
            return true;
        return !rel.contains(QLatin1Char('/')) && configExt.contains(QFileInfo(rel).suffix().toLower());
    };
    auto revert = [&]() {
        for (const QString& rel : replaced)
            Archive::copyFileReplace(rb + QStringLiteral("/files/") + rel, target + QLatin1Char('/') + rel, nullptr);
        for (const QString& rel : added)
            QFile::remove(target + QLatin1Char('/') + rel);
    };
    for (const QString& rel : files) {
        const QString src = newRoot + QLatin1Char('/') + rel;
        const QString dst = target + QLatin1Char('/') + rel;
        if (QFileInfo::exists(dst) && isUserFile(rel, dst)) {
            kept << rel;
            continue;
        }
        if (QFileInfo::exists(dst)) {
            const QString keep = rb + QStringLiteral("/files/") + rel;
            QDir().mkpath(QFileInfo(keep).absolutePath());
            QFile::copy(dst, keep);
        }
        QString cerr;
        const bool existed = QFileInfo::exists(dst);
        if (!Archive::copyFileReplace(src, dst, &cerr)) {
            revert();
            Archive::removeTree(staging);
            Archive::removeTree(rb);
            res.message = QObject::tr("%1 Mise à jour annulée, l'ancienne version est intacte.").arg(cerr);
            return res;
        }
        (existed ? replaced : added) << rel;
    }
    writeText(rb + QStringLiteral("/added.txt"), added.join(QLatin1Char('\n')));
    if (!kept.isEmpty())
        qInfo("Mise à jour de %s : %d fichier(s) de réglages conservé(s)", qUtf8Printable(job.name), int(kept.size()));
    writeText(target + QLatin1Char('/') + QLatin1String(kMarker), job.version + QLatin1Char('\n') + job.assetName);
    Archive::removeTree(staging);
    if (!job.keepArchive)
        QFile::remove(job.archive);
    res.ok = true;
    res.exe = QDir::toNativeSeparators(target + QLatin1Char('/') + exeInStaging.mid(newRoot.size() + 1));
    return res;
}

// Installateur Windows (NSIS, Inno Setup, MSI…) plutôt que l'émulateur lui-même ?
bool looksLikeInstaller(const QString& file) {
    if (file.endsWith(QLatin1String(".msi"), Qt::CaseInsensitive))
        return true;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = f.read(4 * 1024 * 1024);
    for (const char* sig : {"Nullsoft", "NullsoftInst", "Inno Setup", "InstallShield", "WixBundle", "Squirrel"})
        if (head.contains(sig))
            return true;
    return false;
}

// Lance un fichier comme un double-clic (l'installateur demande lui-même l'autorisation administrateur) et attend sa fin.
int openAndWait(const QString& file) {
#ifdef Q_OS_WIN
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"open";
    const std::wstring path = QDir::toNativeSeparators(file).toStdWString();
    sei.lpFile = path.c_str();
    sei.nShow = SW_SHOWNORMAL;
    int code = -1;
    if (ShellExecuteExW(&sei) && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD c = 1;
        GetExitCodeProcess(sei.hProcess, &c);
        CloseHandle(sei.hProcess);
        code = int(c);
    }
    CoUninitialize();
    return code;
#else
    return QProcess::execute(file, {});
#endif
}

// Après un installateur : l'exécutable tout juste créé ou modifié dans les dossiers d'installation habituels.
QString findFreshExe(const QStringList& exeNames, const QStringList& extraDirs, const QDateTime& since) {
    QStringList roots = extraDirs;
#ifdef Q_OS_WIN
    const auto env = QProcessEnvironment::systemEnvironment();
    roots << env.value(QStringLiteral("LOCALAPPDATA")) + QStringLiteral("/Programs") << env.value(QStringLiteral("ProgramFiles"))
          << env.value(QStringLiteral("ProgramFiles(x86)")) << env.value(QStringLiteral("LOCALAPPDATA"));
#endif
    QString best;
    QDateTime bestTime;
    std::function<void(const QString&, int)> walk = [&](const QString& dir, int depth) {
        const QDir d(dir);
        for (const QFileInfo& fi : d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (fi.isDir()) {
                if (depth > 0)
                    walk(fi.absoluteFilePath(), depth - 1);
                continue;
            }
            if (!exeNames.contains(fi.fileName(), Qt::CaseInsensitive))
                continue;
            // Les installateurs gardent souvent la date d'origine des fichiers : la date de création compte aussi.
            QDateTime t = fi.lastModified();
            if (fi.birthTime().isValid() && fi.birthTime() > t)
                t = fi.birthTime();
            if (t >= since.addSecs(-120) && (!bestTime.isValid() || t > bestTime)) {
                best = fi.absoluteFilePath();
                bestTime = t;
            }
        }
    };
    for (const QString& r : roots)
        if (!r.isEmpty() && QFileInfo(r).isDir())
            walk(r, 2);
    return best;
}

} // namespace

// ===========================================================================
EmulatorManager& EmulatorManager::instance() {
    static EmulatorManager m;
    return m;
}

void EmulatorManager::init() {
    auto& s = Settings::instance();
    for (const auto& e : Registry::instance().emulators()) {
        const QString cached = s.value(QStringLiteral("detect/path_") + e.id).toString();
        if (isFile(cached))
            m_detected.insert(e.id, cached);
        const QString latest = s.value(QStringLiteral("updates/latest_") + e.id).toString();
        if (!latest.isEmpty()) {
            ReleaseInfo r;
            r.version = latest;
            r.assetName = s.value(QStringLiteral("updates/asset_") + e.id).toString();
            r.url = s.value(QStringLiteral("updates/url_") + e.id).toString();
            m_latest.insert(e.id, r);
        }
    }
    refreshAll();
    startDetection();
}

void EmulatorManager::startDetection() {
    if (m_detecting)
        return;
    m_detecting = true;
    emit changed();
    const QVector<Root> roots = detectionRoots();
    const QStringList names = Registry::instance().allExeNames();
    const QSet<QString> nameSet(names.begin(), names.end());
    QVector<LooseName> loose;
#ifndef Q_OS_WIN
    for (const auto& e : Registry::instance().emulators()) {
        if (e.exeNames.isEmpty())
            continue;
        LooseName l{e.exeNames.first().toLower(), {e.id.toLower(), QString(e.name).remove(QLatin1Char(' ')).toLower()}};
        QString first = e.exeNames.first().toLower();
        first.remove(QStringLiteral(".app"));
        l.needles << first;
        l.needles.removeDuplicates();
        loose << l;
    }
#endif
    auto* w = new QFutureWatcher<QHash<QString, QString>>(this);
    connect(w, &QFutureWatcher<QHash<QString, QString>>::finished, this, [this, w] {
        const QHash<QString, QString> found = w->result();
        w->deleteLater();
        m_detected.clear();
        for (const auto& e : Registry::instance().emulators()) {
            QString path;
            for (const QString& exe : e.exeNames) {
                path = found.value(exe.toLower());
                if (!path.isEmpty())
                    break;
            }
            if (path.isEmpty()) {
                for (const QString& exe : e.exeNames) { // PATH système
                    path = QStandardPaths::findExecutable(exe);
                    if (!path.isEmpty())
                        break;
                }
            }
#ifdef Q_OS_LINUX
            if (path.isEmpty() && !e.flatpak.isEmpty()) { // Flatpak : le lanceur exporté se lance comme un programme
                for (const QString& dir : {QStringLiteral("/var/lib/flatpak/exports/bin"), QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/bin")}) {
                    if (QFileInfo(dir + QLatin1Char('/') + e.flatpak).isFile()) {
                        path = dir + QLatin1Char('/') + e.flatpak;
                        break;
                    }
                }
            }
#endif
            if (!path.isEmpty())
                m_detected.insert(e.id, QDir::toNativeSeparators(path));
            Settings::instance().setValue(QStringLiteral("detect/path_") + e.id, path);
        }
        m_detecting = false;
        refreshAll();
        qInfo("Détection des émulateurs : %d trouvé(s)", installedCount());
        emit detectionFinished(installedCount());
        emit changed();
    });
    w->setFuture(QtConcurrent::run(scanForExecutables, roots, nameSet, loose));
}

QString EmulatorManager::installedVersion(const QString& exe) const {
    if (exe.isEmpty())
        return {};
    const QFileInfo fi(exe);
    const QDir dir = fi.absoluteDir();
    if (const QString v = readMarker(dir.absolutePath()).section(QLatin1Char('\n'), 0, 0).trimmed(); !v.isEmpty())
        return v;
    QString v = Archive::versionFromName(dir.dirName());
    if (v.isEmpty()) {
        QDir parent = dir;
        if (parent.cdUp())
            v = Archive::versionFromName(parent.dirName());
    }
#ifdef Q_OS_WIN
    if (v.isEmpty())
        v = peProductVersion(exe);
#endif
    return v;
}

// Version lue dans l'exécutable (ex. Lichii), gardée en cache tant que le fichier ne change pas.
QString EmulatorManager::probedVersion(const QString& id, const QString& exe) const {
    const EmulatorDef* e = Registry::instance().emulator(id);
    if (!e || e->versionProbe.isEmpty() || exe.isEmpty())
        return {};
    const QFileInfo fi(exe);
    const QString stamp = QStringLiteral("%1|%2|%3").arg(QDir::fromNativeSeparators(exe)).arg(fi.size()).arg(fi.lastModified().toSecsSinceEpoch());
    auto& s = Settings::instance();
    const QString key = QStringLiteral("detect/probe_") + id;
    const QString cached = s.value(key).toString();
    if (cached.section(QLatin1Char('#'), 0, 0) == stamp)
        return cached.section(QLatin1Char('#'), 1);
    const QString v = Archive::probeVersion(exe, e->versionProbe);
    s.setValue(key, stamp + QLatin1Char('#') + v);
    return v;
}

void EmulatorManager::refreshStatus(const QString& id) {
    const EmulatorDef* e = Registry::instance().emulator(id);
    if (!e)
        return;
    EmulatorStatus st;
    const QString configured = Settings::instance().emulatorPath(id);
    if (isFile(configured))
        st.exe = QDir::toNativeSeparators(configured);
    else if (isFile(m_detected.value(id)))
        st.exe = m_detected.value(id);
    if (!st.exe.isEmpty()) {
        st.dir = QFileInfo(st.exe).absolutePath();
        st.managed = QDir::fromNativeSeparators(st.dir).startsWith(QDir::fromNativeSeparators(Paths::emulatorsDir()), Qt::CaseInsensitive);
        const QString marker = readMarker(st.dir).section(QLatin1Char('\n'), 0, 0).trimmed();
        st.version = marker.isEmpty() ? probedVersion(id, st.exe) : QString();
        if (st.version.isEmpty())
            st.version = installedVersion(st.exe);
    }
    // Dossier de données : premier candidat dont la condition est remplie.
    QString fallback;
    for (const DataDirRule& rule : e->dataDirs) {
        if (rule.path.contains(QLatin1String("{emudir}")) && st.dir.isEmpty())
            continue;
        const QString path = Paths::expand(rule.path, st.dir);
        const QString when = rule.when.isEmpty() ? path : Paths::expand(rule.when, st.dir);
        fallback = path;
        if (QFileInfo::exists(when)) {
            st.dataDir = path;
            break;
        }
    }
    if (st.dataDir.isEmpty())
        st.dataDir = fallback;
    if (m_latest.contains(id))
        st.latest = m_latest.value(id).version;
    if (st.installed() && !st.latest.isEmpty() && !st.version.isEmpty())
        st.updateAvailable = st.version != st.latest && Archive::compareVersions(st.latest, st.version) > 0;
    st.canRollback = QFileInfo::exists(Paths::rollbackDir() + QLatin1Char('/') + id + QStringLiteral("/target.txt"));
    st.busy = m_busy.contains(id);
    m_status.insert(id, st);
}

void EmulatorManager::refreshAll() {
    for (const auto& e : Registry::instance().emulators())
        refreshStatus(e.id);
}

int EmulatorManager::installedCount() const {
    int n = 0;
    for (const auto& st : m_status)
        if (st.installed())
            ++n;
    return n;
}

int EmulatorManager::updatesAvailable() const {
    int n = 0;
    for (const auto& st : m_status)
        if (st.updateAvailable)
            ++n;
    return n;
}

QString EmulatorManager::effectiveDefault(const QString& systemId) const {
    const QString chosen = Settings::instance().value(QStringLiteral("systems/default_") + systemId).toString();
    if (!chosen.isEmpty() && Registry::instance().emulator(chosen))
        return chosen;
    const QString builtin = Registry::instance().builtinDefaultEmulator(systemId);
    if (isInstalled(builtin))
        return builtin;
    for (const EmulatorDef* e : Registry::instance().emulatorsFor(systemId))
        if (isInstalled(e->id))
            return e->id;
    return builtin;
}

QString EmulatorManager::emulatorFor(const Game& g) const {
    if (!g.emulatorOverride.isEmpty() && Registry::instance().emulator(g.emulatorOverride))
        return g.emulatorOverride;
    const QString def = effectiveDefault(g.system());
    const EmulatorDef* d = Registry::instance().emulator(def);
    if (!d || d->canOpen(g.path))
        return def;
    // L'émulateur par défaut ne sait pas ouvrir ce format (ex. .nup avec Eden) :
    // on prend un émulateur installé qui le peut, sinon le premier compatible.
    QString fallback;
    for (const EmulatorDef* e : Registry::instance().emulatorsFor(g.system())) {
        if (!e->canOpen(g.path))
            continue;
        if (isInstalled(e->id))
            return e->id;
        if (fallback.isEmpty())
            fallback = e->id;
    }
    return fallback.isEmpty() ? def : fallback;
}

QVector<EmulatorManager::FileState> EmulatorManager::systemFiles(const QString& id) const {
    QVector<FileState> out;
    const EmulatorDef* e = Registry::instance().emulator(id);
    const QString data = dataDir(id);
    if (!e || data.isEmpty())
        return out;
    for (const SystemFileDef& def : e->systemFiles) {
        FileState fs;
        fs.def = def;
        fs.path = QDir::cleanPath(data + QLatin1Char('/') + def.path);
        if (def.isDir) {
            const QDir d(fs.path);
            fs.present = d.exists() && !d.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
        } else {
            fs.present = QFileInfo(fs.path).size() > 0;
        }
        out.push_back(fs);
    }
    return out;
}

QStringList EmulatorManager::missingRequired(const QString& id) const {
    QStringList out;
    for (const auto& f : systemFiles(id))
        if (f.def.required && !f.present)
            out << f.def.label;
    return out;
}

bool EmulatorManager::importSystemFile(const QString& id, int index, const QString& source, QString* error) {
    const auto files = systemFiles(id);
    if (index < 0 || index >= files.size() || files[index].def.isDir) {
        if (error)
            *error = tr("Ce fichier ne peut pas être importé ici.");
        return false;
    }
    const QString dest = files[index].path;
    if (QFileInfo::exists(dest)) {
        const QString bak = dest + QStringLiteral(".bak-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        QFile::copy(dest, bak);
    }
    QString err;
    if (!Archive::copyFileReplace(source, dest, &err)) {
        if (error)
            *error = err;
        return false;
    }
    emit changed();
    return true;
}

void EmulatorManager::setExePath(const QString& id, const QString& chosen) {
    QString path = chosen;
    // macOS : on choisit « Dolphin.app », Playnest retient le programme qu'elle contient.
    if (path.endsWith(QLatin1String(".app"), Qt::CaseInsensitive) && QFileInfo(path).isDir())
        if (const QString inner = bundleExecutable(path); !inner.isEmpty())
            path = inner;
    Settings::instance().setEmulatorPath(id, path);
    Settings::instance().sync();
    refreshStatus(id);
    emit changed();
}

void EmulatorManager::relocate(const QString& id, const QString& oldDir, const QString& newDir) {
    const QString exe = exePath(id);
    const QString from = QDir::fromNativeSeparators(QDir::cleanPath(oldDir));
    QString rel = QDir::fromNativeSeparators(exe);
    if (!rel.startsWith(from + QLatin1Char('/'), Qt::CaseInsensitive))
        return;
    const QString moved = QDir::toNativeSeparators(QDir::cleanPath(newDir) + rel.mid(from.size()));
    auto& s = Settings::instance();
    if (!s.emulatorPath(id).isEmpty())
        s.setEmulatorPath(id, moved);
    s.setValue(QStringLiteral("detect/path_") + id, moved);
    m_detected.insert(id, moved);
    // Retour arrière d'une mise à jour : il vise désormais le nouveau dossier.
    const QString target = Paths::rollbackDir() + QLatin1Char('/') + id + QStringLiteral("/target.txt");
    if (QFileInfo::exists(target)) {
        QFile f(target);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QDir::toNativeSeparators(newDir).toUtf8());
    }
    s.sync();
    refreshStatus(id);
    emit changed();
}

bool EmulatorManager::isRunning(const QString& id) const {
    const QString exe = exePath(id);
    if (exe.isEmpty())
        return false;
    if (!m_runningExe.isEmpty() && QDir::fromNativeSeparators(m_runningExe).compare(QDir::fromNativeSeparators(exe), Qt::CaseInsensitive) == 0)
        return true;
#ifdef Q_OS_WIN
    // Sous Windows, un exécutable en cours d'exécution ne peut pas être renommé.
    const QString probe = exe + QStringLiteral(".playnest-check");
    if (!QFile::rename(exe, probe))
        return true;
    QFile::rename(probe, exe);
#endif
    return false;
}

QDateTime EmulatorManager::lastUpdateCheck() const {
    return Settings::instance().value(QStringLiteral("updates/lastCheck")).toDateTime();
}

void EmulatorManager::fetchRelease(const EmulatorDef& e, std::function<void(bool, ReleaseInfo, QString)> done) {
    const SourceDef src = e.source;
    const bool includePre = Settings::instance().value(QStringLiteral("updates/prereleases"), false).toBool();
    QString flavor = readMarker(status(e.id).dir).section(QLatin1Char('\n'), 1, 1);
    if (flavor.isEmpty() && status(e.id).installed())
        flavor = QFileInfo(status(e.id).dir).fileName();

    if (src.type == QLatin1String("page")) {
        Net::instance().get(QUrl(src.url), this, [src, done](bool ok, const QByteArray& data, const QString& err) {
            if (!ok) {
                done(false, {}, err);
                return;
            }
            const QString html = QString::fromUtf8(data);
            for (const QString& pat : src.assets) {
                const auto m = QRegularExpression(pat).match(html);
                if (!m.hasMatch())
                    continue;
                ReleaseInfo r;
                r.url = m.captured(0);
                r.assetName = QUrl(r.url).fileName();
                if (!src.versionRegex.isEmpty()) {
                    const auto vm = QRegularExpression(src.versionRegex).match(r.url);
                    if (vm.hasMatch())
                        r.version = vm.captured(1);
                }
                if (r.version.isEmpty())
                    r.version = Archive::versionFromName(r.assetName);
                done(true, r, {});
                return;
            }
            done(false, {}, tr("aucun lien de téléchargement trouvé sur la page"));
        });
        return;
    }

    QUrl api;
    if (src.type == QLatin1String("gitlab"))
        api = QUrl(QStringLiteral("https://%1/api/v4/projects/%2/releases?per_page=12")
                       .arg(src.host, QString::fromUtf8(QUrl::toPercentEncoding(src.repo))));
    else
        api = QUrl(QStringLiteral("https://api.github.com/repos/%1/releases?per_page=12").arg(src.repo));

    Net::instance().get(api, this, [src, includePre, flavor, done](bool ok, const QByteArray& data, const QString& err) {
        if (!ok) {
            done(false, {}, err);
            return;
        }
        const QJsonArray releases = QJsonDocument::fromJson(data).array();
        std::optional<ReleaseInfo> stable, pre;
        for (const auto& rv : releases) {
            const QJsonObject rel = rv.toObject();
            if (rel.value(QStringLiteral("draft")).toBool() || rel.value(QStringLiteral("upcoming_release")).toBool())
                continue;
            QStringList names, urls;
            if (src.type == QLatin1String("gitlab")) {
                for (const auto& lv : rel.value(QStringLiteral("assets")).toObject().value(QStringLiteral("links")).toArray()) {
                    const QJsonObject l = lv.toObject();
                    names << l.value(QStringLiteral("name")).toString();
                    const QString direct = l.value(QStringLiteral("direct_asset_url")).toString();
                    urls << (direct.isEmpty() ? l.value(QStringLiteral("url")).toString() : direct);
                }
            } else {
                for (const auto& av : rel.value(QStringLiteral("assets")).toArray()) {
                    const QJsonObject a = av.toObject();
                    names << a.value(QStringLiteral("name")).toString();
                    urls << a.value(QStringLiteral("browser_download_url")).toString();
                }
            }
            const int idx = chooseAsset(src.assets, names, flavor);
            if (idx < 0)
                continue;
            ReleaseInfo r;
            r.version = rel.value(QStringLiteral("tag_name")).toString();
            r.assetName = names[idx];
            r.url = urls[idx];
            const QString tagLow = r.version.toLower();
            r.prerelease = rel.value(QStringLiteral("prerelease")).toBool() || tagLow.contains(QLatin1String("rc")) ||
                           tagLow.contains(QLatin1String("beta")) || tagLow.contains(QLatin1String("nightly"));
            if (!r.prerelease && !stable)
                stable = r;
            if (r.prerelease && !pre)
                pre = r;
            if (stable && pre)
                break;
        }
        // Version stable en priorité ; versions de test si demandé ou si le projet n'en publie pas d'autres.
        std::optional<ReleaseInfo> pick = stable;
        if (pre && (!stable || (includePre && Archive::compareVersions(pre->version, stable->version) > 0)))
            pick = pre;
        if (!pick) {
            done(false, {}, tr("aucun fichier compatible avec ton système dans les dernières versions"));
            return;
        }
        done(true, *pick, {});
    });
}

void EmulatorManager::checkUpdates(bool force) {
    if (m_pendingChecks > 0)
        return;
    const QDateTime last = lastUpdateCheck();
    if (!force && last.isValid() && last.secsTo(QDateTime::currentDateTime()) < 20 * 3600) {
        emit updatesChecked(updatesAvailable());
        return;
    }
    m_updateFound = 0;
    for (const auto& e : Registry::instance().emulators()) {
        if (!e.canAutoInstall())
            continue;
        ++m_pendingChecks;
        const QString id = e.id;
        fetchRelease(e, [this, id](bool ok, ReleaseInfo r, QString err) {
            if (ok) {
                m_latest.insert(id, r);
                auto& s = Settings::instance();
                s.setValue(QStringLiteral("updates/latest_") + id, r.version);
                s.setValue(QStringLiteral("updates/asset_") + id, r.assetName);
                s.setValue(QStringLiteral("updates/url_") + id, r.url);
            } else {
                qWarning("Vérification des mises à jour de %s : %s", qUtf8Printable(id), qUtf8Printable(err));
            }
            refreshStatus(id);
            if (--m_pendingChecks == 0) {
                Settings::instance().setValue(QStringLiteral("updates/lastCheck"), QDateTime::currentDateTime());
                Settings::instance().sync();
                emit updatesChecked(updatesAvailable());
                emit changed();
            }
        });
    }
    if (m_pendingChecks == 0)
        emit updatesChecked(0);
    emit changed();
}

void EmulatorManager::fail(const QString& id, const QString& msg) {
    m_busy.remove(id);
    refreshStatus(id);
    emit installFinished(id, false, msg);
    emit changed();
}

void EmulatorManager::install(const QString& id) {
    const EmulatorDef* e = Registry::instance().emulator(id);
    if (!e || m_busy.contains(id))
        return;
    if (!e->canAutoInstall()) {
        emit installFinished(id, false, tr("%1 ne peut pas être installé automatiquement. Télécharge-le depuis son site officiel.").arg(e->name));
        return;
    }
    QString why;
    if (!canModify(id, &why)) {
        emit installFinished(id, false, why);
        return;
    }
    m_busy.insert(id);
    refreshStatus(id);
    emit changed();
    emit installProgress(id, tr("Recherche de la dernière version de %1…").arg(e->name), 0, 0);
    fetchRelease(*e, [this, id](bool ok, ReleaseInfo r, QString err) {
        if (!ok) {
            fail(id, tr("Impossible de trouver la dernière version : %1").arg(err));
            return;
        }
        m_latest.insert(id, r);
        continueInstall(id, r);
    });
}

void EmulatorManager::continueInstall(const QString& id, const ReleaseInfo& rel) {
    const EmulatorDef* e = Registry::instance().emulator(id);
    if (!e) {
        fail(id, tr("Émulateur inconnu."));
        return;
    }
    // Archive .7z : 7-Zip est installé d'abord si nécessaire.
    if (rel.assetName.toLower().endsWith(QLatin1String(".7z")) && Tools::instance().sevenZipPath().isEmpty()) {
        emit installProgress(id, tr("Installation de 7-Zip, nécessaire pour %1…").arg(e->name), 0, 0);
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(&Tools::instance(), &Tools::installFinished, this, [this, id, rel, conn](const QString& tool, bool ok, const QString& msg) {
            if (tool != QLatin1String("7zip"))
                return;
            disconnect(*conn);
            if (!ok)
                fail(id, msg);
            else
                continueInstall(id, rel);
        });
        Tools::instance().install(QStringLiteral("7zip"));
        return;
    }

    const EmulatorStatus st = status(id);
    InstallJob job;
    job.id = id;
    job.name = e->name;
    job.version = rel.version;
    job.assetName = rel.assetName;
    job.exeNames = e->exeNames;
    job.currentDir = st.installed() ? st.dir : QString();
    job.dataDir = st.dataDir;
    if (st.installed() && Settings::instance().value(QStringLiteral("saves/autoBeforeUpdate"), true).toBool()) {
        job.backupSources = SaveManager::instance().sourcesForEmulators({id});
        job.backupFile = SaveManager::newBackupPath(QStringLiteral("maj-") + id);
    }
    job.archive = Paths::downloadsDir() + QLatin1Char('/') + rel.assetName;
    emit installProgress(id, tr("Téléchargement de %1 %2…").arg(e->name, rel.version), 0, 0);
    Net::instance().download(
        QUrl(rel.url), job.archive, this,
        [this, id, name = e->name, v = rel.version](qint64 r, qint64 t) {
            emit installProgress(id, tr("Téléchargement de %1 %2…").arg(name, v), r, t);
        },
        [this, id, job](bool ok, const QString& err) {
            if (!ok) {
                QFile::remove(job.archive);
                fail(id, tr("Échec du téléchargement : %1").arg(err));
                return;
            }
            startJob(id, job);
        });
}

void EmulatorManager::startJob(const QString& id, const InstallJob& job) {
    emit installProgress(id, job.backupSources.isEmpty() ? tr("Installation…") : tr("Copie de sécurité des sauvegardes, puis installation…"), 0, 0);
    auto* w = new QFutureWatcher<InstallResult>(this);
    connect(w, &QFutureWatcher<InstallResult>::finished, this, [this, w, id, job] {
        const InstallResult r = w->result();
        w->deleteLater();
        if (!r.ok) {
            fail(id, r.message);
            return;
        }
        if (job.currentDir.isEmpty())
            Settings::instance().setEmulatorPath(id, r.exe);
        m_detected.insert(id, r.exe);
        Settings::instance().setValue(QStringLiteral("detect/path_") + id, r.exe);
        Settings::instance().sync();
        m_busy.remove(id);
        refreshStatus(id);
        if (!job.backupSources.isEmpty())
            SaveManager::instance().reload();
        const bool known = r.version != tr("version inconnue");
        emit installFinished(id, true, job.currentDir.isEmpty() ? (known ? tr("%1 %2 est installé.").arg(job.name, r.version) : tr("%1 est installé.").arg(job.name))
                                                                 : (known ? tr("%1 est à jour (%2).").arg(job.name, r.version) : tr("%1 est à jour.").arg(job.name)));
        emit changed();
    });
    w->setFuture(QtConcurrent::run(runInstall, job));
}

QString EmulatorManager::identifyArchive(const QString& archive) const {
    // L'émulateur dont l'exécutable principal est dans l'archive l'emporte (lichii.exe plutôt qu'eden-cli.exe).
    QSet<QString> names;
    for (const QString& entry : Archive::archiveEntries(archive))
        names.insert(QFileInfo(entry).fileName().toLower());
    QString best;
    int bestScore = 0;
    for (const EmulatorDef& e : Registry::instance().emulators()) {
        const QStringList& exes = e.exeNames;
        for (int i = 0; i < exes.size(); ++i) {
            QString n = exes[i].toLower();
            if (!names.contains(n) && !names.contains(n + QStringLiteral(".exe")) &&
                !(n.endsWith(QLatin1String(".exe")) && names.contains(n.chopped(4))))
                continue;
            const int score = i == 0 ? 2 : 1;
            if (score > bestScore) {
                bestScore = score;
                best = e.id;
            }
        }
    }
    return best;
}

bool EmulatorManager::canModify(const QString& id, QString* why) const {
    const EmulatorDef* e = Registry::instance().emulator(id);
    if (!e) {
        if (why) *why = tr("Émulateur inconnu.");
        return false;
    }
    if (m_busy.contains(id)) {
        if (why) *why = tr("%1 est déjà en cours d'installation.").arg(e->name);
        return false;
    }
    const EmulatorStatus cur = status(id);
    if (cur.installed()) {
        QFile probe(cur.dir + QStringLiteral("/.playnest-write-test"));
        if (!probe.open(QIODevice::WriteOnly)) {
            if (why)
                *why = tr("Le dossier de %1 est protégé en écriture (%2). Déplace %1 dans un dossier personnel, "
                          "puis indique son nouvel emplacement.").arg(e->name, QDir::toNativeSeparators(cur.dir));
            return false;
        }
        probe.close();
        probe.remove();
    }
    if (isRunning(id)) {
        if (why) *why = tr("Ferme %1 avant de le mettre à jour.").arg(e->name);
        return false;
    }
    return true;
}

EmulatorManager::FileKind EmulatorManager::classifyFile(const QString& file) const {
    FileKind k;
    const QString low = QFileInfo(file).fileName().toLower();
    if (low.endsWith(QLatin1String(".zip")) || low.endsWith(QLatin1String(".7z"))) {
        k.id = identifyArchive(file);
        k.kind = k.id.isEmpty() ? FileKind::Unknown : FileKind::Archive;
        return k;
    }
    if (!low.endsWith(QLatin1String(".exe")) && !low.endsWith(QLatin1String(".msi")))
        return k;
    // Exécutable nommé exactement comme celui d'un émulateur : c'est l'application elle-même.
    for (const EmulatorDef& e : Registry::instance().emulators())
        for (const QString& n : e.exeNames)
            if (n.compare(low, Qt::CaseInsensitive) == 0 || (n + QStringLiteral(".exe")).compare(low, Qt::CaseInsensitive) == 0) {
                k.id = e.id;
                k.kind = FileKind::App;
                return k;
            }
    // Sinon : le nom contient celui d'un émulateur (« azahar-windows-msys2-2126.2-installer.exe »).
    const QStringList tokens = low.split(QRegularExpression(QStringLiteral("[^a-z0-9]+")), Qt::SkipEmptyParts);
    for (const EmulatorDef& e : Registry::instance().emulators()) {
        QString compact = e.name.toLower();
        compact.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
        if (tokens.contains(e.id) || tokens.contains(compact)) {
            k.id = e.id;
            break;
        }
    }
    if (k.id.isEmpty())
        return k;
    const bool namedInstaller = tokens.contains(QStringLiteral("setup")) || tokens.contains(QStringLiteral("installer")) ||
                                tokens.contains(QStringLiteral("install"));
    k.kind = namedInstaller || looksLikeInstaller(file) ? FileKind::Installer : FileKind::App;
    return k;
}

void EmulatorManager::runInstaller(const QString& id, const QString& file) {
    const EmulatorDef* e = Registry::instance().emulator(id);
    if (!e || m_busy.contains(id))
        return;
    if (isRunning(id)) {
        emit installFinished(id, false, tr("Ferme %1 avant de le mettre à jour.").arg(e->name));
        return;
    }
    const EmulatorStatus st = status(id);
    QVector<SaveSource> sources;
    QString backupFile;
    if (st.installed() && Settings::instance().value(QStringLiteral("saves/autoBeforeUpdate"), true).toBool()) {
        sources = SaveManager::instance().sourcesForEmulators({id});
        backupFile = SaveManager::newBackupPath(QStringLiteral("maj-") + id);
    }
    m_busy.insert(id);
    refreshStatus(id);
    emit changed();
    const QDateTime started = QDateTime::currentDateTime();
    struct R { bool backupOk = true; QString err; int code = -1; };
    auto* w = new QFutureWatcher<R>(this);
    connect(w, &QFutureWatcher<R>::finished, this, [this, w, id, started, oldDir = st.dir, installerName = file] {
        const R r = w->result();
        w->deleteLater();
        const EmulatorDef* d = Registry::instance().emulator(id);
        if (!r.backupOk) {
            fail(id, tr("La copie de sécurité de tes sauvegardes a échoué (%1). L'installateur n'a pas été lancé.").arg(r.err));
            return;
        }
        if (r.code != 0) {
            fail(id, r.code < 0 ? tr("Impossible de lancer l'installateur.")
                                : tr("L'installateur s'est arrêté sans terminer (code %1). Si tu l'as annulé, rien n'a changé.").arg(r.code));
            return;
        }
        QStringList extra;
        if (!oldDir.isEmpty())
            extra << QFileInfo(oldDir).absolutePath();
        const QString fresh = findFreshExe(d->exeNames, extra, started);
        m_busy.remove(id);
        if (!fresh.isEmpty()) {
            // Version donnée par le nom de l'installateur (azahar-windows-msys2-2126.2-installer.exe → 2126.2).
            const QString v = Archive::versionFromName(QFileInfo(installerName).fileName());
            if (!v.isEmpty())
                writeText(QFileInfo(fresh).absolutePath() + QLatin1Char('/') + QLatin1String(kMarker), v + QLatin1Char('\n') + QFileInfo(installerName).fileName());
            if (!Settings::instance().emulatorPath(id).isEmpty())
                Settings::instance().setEmulatorPath(id, QDir::toNativeSeparators(fresh));
            m_detected.insert(id, QDir::toNativeSeparators(fresh));
            Settings::instance().setValue(QStringLiteral("detect/path_") + id, QDir::toNativeSeparators(fresh));
            Settings::instance().sync();
            refreshStatus(id);
            const EmulatorStatus now = status(id);
            SaveManager::instance().reload();
            emit installFinished(id, true, now.version.isEmpty() ? tr("%1 est installé (%2).").arg(d->name, QDir::toNativeSeparators(now.dir))
                                                                 : tr("%1 est à jour (%2).").arg(d->name, now.version));
        } else {
            refreshStatus(id);
            startDetection();
            emit installFinished(id, true, tr("Installation de %1 terminée. Si Playnest utilise encore l'ancienne version, "
                                              "indique la nouvelle avec « Choisir l'exécutable… ».").arg(d->name));
        }
        emit changed();
    });
    emit installProgress(id, sources.isEmpty() ? tr("Installateur ouvert pour %1 : suis ses étapes…").arg(e->name)
                                               : tr("Copie de sécurité des sauvegardes…"), 0, 0);
    w->setFuture(QtConcurrent::run([this, id, file, sources, backupFile, name = e->name]() -> R {
        R r;
        if (!sources.isEmpty()) {
            qint64 bytes = 0;
            if (!SaveManager::doBackup(sources, backupFile, QObject::tr("Avant la mise à jour de %1").arg(name), true, &r.err, &bytes) &&
                r.err != QObject::tr("aucune sauvegarde à copier")) {
                r.backupOk = false;
                return r;
            }
            emit installProgress(id, QObject::tr("Installateur ouvert pour %1 : suis ses étapes…").arg(name), 0, 0);
        }
        r.code = openAndWait(file);
        return r;
    }));
}

void EmulatorManager::installFromFile(const QString& id, const QString& archive) {
    const EmulatorDef* e = Registry::instance().emulator(id);
    QString why;
    if (!canModify(id, &why)) {
        emit installFinished(id, false, why);
        return;
    }
    if (archive.endsWith(QLatin1String(".7z"), Qt::CaseInsensitive) && Tools::instance().sevenZipPath().isEmpty()) {
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(&Tools::instance(), &Tools::installFinished, this, [this, id, archive, conn](const QString& tool, bool ok, const QString& msg) {
            if (tool != QLatin1String("7zip"))
                return;
            disconnect(*conn);
            if (ok)
                installFromFile(id, archive);
            else
                emit installFinished(id, false, msg);
        });
        emit installProgress(id, tr("Installation de 7-Zip, nécessaire pour cette archive…"), 0, 0);
        Tools::instance().install(QStringLiteral("7zip"));
        return;
    }
    const EmulatorStatus st = status(id);
    InstallJob job;
    job.id = id;
    job.name = e->name;
    job.archive = archive;
    job.keepArchive = true;
    job.singleExe = archive.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive);
    job.versionProbe = e->versionProbe;
    // Nom de l'archive sans « (1) » : il sert à garder la même variante (pgo, msvc…) aux mises à jour suivantes.
    job.assetName = QFileInfo(archive).fileName().remove(QRegularExpression(QStringLiteral("\\s*[\\(_]+\\d+[\\)_]*(?=\\.)")));
    job.exeNames = e->exeNames;
    job.currentDir = st.installed() ? st.dir : QString();
    job.dataDir = st.dataDir;
    if (st.installed() && Settings::instance().value(QStringLiteral("saves/autoBeforeUpdate"), true).toBool()) {
        job.backupSources = SaveManager::instance().sourcesForEmulators({id});
        job.backupFile = SaveManager::newBackupPath(QStringLiteral("maj-") + id);
    }
    m_busy.insert(id);
    refreshStatus(id);
    emit changed();
    startJob(id, job);
}

bool EmulatorManager::rollback(const QString& id, QString* error) {
    const QString rb = Paths::rollbackDir() + QLatin1Char('/') + id;
    const QString target = readText(rb + QStringLiteral("/target.txt"));
    if (target.isEmpty()) {
        if (error)
            *error = tr("Aucune version précédente n'est disponible.");
        return false;
    }
    if (isRunning(id)) {
        if (error)
            *error = tr("Ferme l'émulateur avant de revenir à la version précédente.");
        return false;
    }
    const QString filesDir = rb + QStringLiteral("/files");
    for (const QString& rel : Archive::listFiles(filesDir)) {
        QString err;
        if (!Archive::copyFileReplace(filesDir + QLatin1Char('/') + rel, target + QLatin1Char('/') + rel, &err)) {
            if (error)
                *error = err;
            return false;
        }
    }
    for (const QString& rel : readText(rb + QStringLiteral("/added.txt")).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        QFile::remove(target + QLatin1Char('/') + rel);
    const QString oldMarker = readText(rb + QStringLiteral("/marker.txt"));
    if (oldMarker.isEmpty())
        QFile::remove(target + QLatin1Char('/') + QLatin1String(kMarker));
    else
        writeText(target + QLatin1Char('/') + QLatin1String(kMarker), oldMarker);
    Archive::removeTree(rb);
    refreshStatus(id);
    emit changed();
    return true;
}
