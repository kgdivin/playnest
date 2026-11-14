#include "Lang.h"
#include "Saves.h"

#include "AppInfo.h"
#include "Archive.h"
#include "Emulators.h"
#include "Library.h"
#include "Paths.h"
#include "Registry.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>

namespace {

// Développe un motif relatif ("sdmc/Nintendo 3DS/*/*/title") en chemins existants.
QStringList expandPattern(const QString& root, const QString& pattern) {
    QStringList current{root};
    const QStringList segs = pattern.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = 0; i < segs.size(); ++i) {
        QStringList next;
        const bool last = i == segs.size() - 1;
        for (const QString& base : current) {
            if (segs[i] == QLatin1String("*")) {
                for (const QString& sub : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                    next << base + QLatin1Char('/') + sub;
            } else {
                const QString p = base + QLatin1Char('/') + segs[i];
                if (QFileInfo::exists(p) && (last || QFileInfo(p).isDir()))
                    next << p;
            }
        }
        current = next;
        if (current.isEmpty())
            break;
    }
    return current;
}

const QStringList kRomSaveExt = {QStringLiteral("sav"), QStringLiteral("dsv"), QStringLiteral("srm"), QStringLiteral("eep"),
                                 QStringLiteral("fla"), QStringLiteral("sra"), QStringLiteral("mpk")};

struct RestoreJob {
    QString file;
    QStringList keys;
    QVector<SaveSource> current;
    QString safetyFile;
};

} // namespace

SaveManager& SaveManager::instance() {
    static SaveManager m;
    return m;
}

QVector<SaveSource> SaveManager::sources() const {
    QVector<SaveSource> out;
    QHash<QString, int> byRoot;
    auto& em = EmulatorManager::instance();
    for (const auto& e : Registry::instance().emulators()) {
        if (e.saves.isEmpty() || !em.isInstalled(e.id))
            continue;
        const QString root = em.dataDir(e.id);
        if (root.isEmpty() || !QFileInfo(root).isDir())
            continue;
        const QString rk = QDir::cleanPath(root).toLower();
        if (byRoot.contains(rk)) { // Eden et Lichii partagent le même dossier
            SaveSource& s = out[byRoot.value(rk)];
            s.emuIds << e.id;
            s.name += tr(" et ") + e.name;
            continue;
        }
        SaveSource s;
        s.key = QStringLiteral("emu-") + e.id;
        s.name = e.name;
        s.root = QDir::cleanPath(root);
        s.emuIds << e.id;
        for (const QString& pat : e.saves)
            s.paths << expandPattern(s.root, pat);
        byRoot.insert(rk, int(out.size()));
        out.push_back(s);
    }
    // Sauvegardes enregistrées à côté des jeux (melonDS, ares…).
    if (m_lib) {
        SaveSource roms;
        roms.key = QStringLiteral("roms");
        roms.name = tr("À côté des jeux (DS, N64, Game Boy…)");
        QSet<QString> seen;
        for (const Game& g : m_lib->games()) {
            if (g.system() != QLatin1String("nds") && g.system() != QLatin1String("n64"))
                continue;
            const QFileInfo fi(g.path);
            for (const QString& ext : kRomSaveExt) {
                const QString p = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QLatin1Char('.') + ext;
                if (!seen.contains(p.toLower()) && QFileInfo::exists(p)) {
                    seen.insert(p.toLower());
                    roms.paths << p;
                }
            }
        }
        if (!roms.paths.isEmpty())
            out.push_back(roms);
    }
    return out;
}

QVector<SaveSource> SaveManager::sourcesForEmulators(const QStringList& ids) const {
    QVector<SaveSource> out;
    for (const SaveSource& s : sources())
        for (const QString& id : ids)
            if (s.emuIds.contains(id)) {
                out.push_back(s);
                break;
            }
    return out;
}

QString SaveManager::newBackupPath(const QString& tag) {
    QString t = tag;
    t.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("-"));
    return Paths::backupsDir() + QLatin1Char('/') + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")) +
           QLatin1Char('_') + t + QStringLiteral(".zip");
}

bool SaveManager::doBackup(const QVector<SaveSource>& sources, const QString& file, const QString& reason, bool automatic,
                           QString* error, qint64* bytes) {
    QVector<QPair<QString, QString>> entries;
    QJsonArray srcArr;
    QJsonObject romMap;
    qint64 total = 0;
    for (const SaveSource& s : sources) {
        QJsonObject o;
        o["key"] = s.key;
        o["name"] = s.name;
        o["root"] = s.root;
        o["emulators"] = QJsonArray::fromStringList(s.emuIds);
        srcArr.append(o);
        if (s.key == QLatin1String("roms")) {
            int n = 0;
            for (const QString& p : s.paths) {
                const QString entry = QStringLiteral("roms/%1_%2").arg(n++).arg(QFileInfo(p).fileName());
                entries.push_back({entry, p});
                romMap[entry] = QDir::fromNativeSeparators(p);
                total += QFileInfo(p).size();
            }
            continue;
        }
        for (const QString& p : s.paths) {
            const QFileInfo fi(p);
            if (fi.isFile()) {
                entries.push_back({s.key + QLatin1Char('/') + p.mid(s.root.size() + 1), p});
                total += fi.size();
            } else {
                for (const QString& rel : Archive::listFiles(p)) {
                    const QString abs = p + QLatin1Char('/') + rel;
                    entries.push_back({s.key + QLatin1Char('/') + abs.mid(s.root.size() + 1), abs});
                    total += QFileInfo(abs).size();
                }
            }
        }
    }
    if (entries.isEmpty()) {
        if (error)
            *error = QObject::tr("aucune sauvegarde à copier");
        return false;
    }
    QJsonObject manifest;
    manifest["app"] = QString::fromLatin1(AppInfo::kName);
    manifest["appVersion"] = QCoreApplication::applicationVersion();
    manifest["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    manifest["reason"] = reason;
    manifest["automatic"] = automatic;
    manifest["sources"] = srcArr;
    manifest["romFiles"] = romMap;
    manifest["files"] = int(entries.size());
    manifest["bytes"] = double(total);
    if (bytes)
        *bytes = total;
    return Archive::createZip(file, entries, {{QStringLiteral("manifest.json"), QJsonDocument(manifest).toJson()}}, error);
}

void SaveManager::reload() {
    m_backups.clear();
    QDir d(Paths::backupsDir());
    for (const QFileInfo& fi : d.entryInfoList({QStringLiteral("*.zip")}, QDir::Files, QDir::Name | QDir::Reversed)) {
        BackupInfo b;
        b.file = fi.absoluteFilePath();
        b.size = fi.size();
        const QJsonObject m = QJsonDocument::fromJson(Archive::readZipEntry(b.file, QStringLiteral("manifest.json"))).object();
        b.created = QDateTime::fromString(m["created"].toString(), Qt::ISODate);
        if (!b.created.isValid())
            b.created = fi.lastModified();
        b.reason = m["reason"].toString();
        b.automatic = m["automatic"].toBool();
        for (const auto& v : m["sources"].toArray()) {
            b.keys << v.toObject()["key"].toString();
            b.names << v.toObject()["name"].toString();
        }
        m_backups.push_back(b);
    }
    emit changed();
}

void SaveManager::computeSizes() {
    const QVector<SaveSource> srcs = sources();
    auto* w = new QFutureWatcher<QHash<QString, qint64>>(this);
    connect(w, &QFutureWatcher<QHash<QString, qint64>>::finished, this, [this, w] {
        m_sizes = w->result();
        w->deleteLater();
        emit changed();
    });
    w->setFuture(QtConcurrent::run([srcs] {
        QHash<QString, qint64> out;
        for (const auto& s : srcs) {
            qint64 t = 0;
            for (const auto& p : s.paths)
                t += Archive::treeSize(p);
            out.insert(s.key, t);
        }
        return out;
    }));
}

void SaveManager::prune() {
    const int keep = Settings::instance().value(QStringLiteral("saves/keep"), 10).toInt();
    int autoCount = 0;
    for (const BackupInfo& b : m_backups) { // du plus récent au plus ancien
        if (!b.automatic)
            continue;
        if (++autoCount > keep)
            QFile::remove(b.file);
    }
}

void SaveManager::backupNow(const QStringList& keys, const QString& reason, bool automatic) {
    if (m_busy)
        return;
    QVector<SaveSource> srcs;
    for (const SaveSource& s : sources())
        if (keys.isEmpty() || keys.contains(s.key))
            srcs.push_back(s);
    if (srcs.isEmpty()) {
        emit finished(false, tr("Aucune sauvegarde trouvée. Lance au moins une fois tes émulateurs."));
        return;
    }
    m_busy = true;
    emit progress(tr("Copie des sauvegardes…"));
    emit changed();
    const QString file = newBackupPath(automatic ? QStringLiteral("auto") : QStringLiteral("manuel"));
    struct R { bool ok; QString err; qint64 bytes; };
    auto* w = new QFutureWatcher<R>(this);
    connect(w, &QFutureWatcher<R>::finished, this, [this, w, automatic] {
        const R r = w->result();
        w->deleteLater();
        m_busy = false;
        reload();
        if (automatic)
            prune();
        reload();
        emit finished(r.ok, r.ok ? tr("Sauvegardes copiées (%1).").arg(Lang::locale().formattedDataSize(r.bytes))
                                 : tr("La copie a échoué : %1").arg(r.err));
    });
    w->setFuture(QtConcurrent::run([srcs, file, reason, automatic] {
        R r{false, {}, 0};
        r.ok = doBackup(srcs, file, reason, automatic, &r.err, &r.bytes);
        return r;
    }));
}

void SaveManager::restore(const QString& file, const QStringList& keys) {
    if (m_busy)
        return;
    m_busy = true;
    emit progress(tr("Restauration…"));
    emit changed();
    RestoreJob job;
    job.file = file;
    job.keys = keys;
    job.current = sources();
    job.safetyFile = newBackupPath(QStringLiteral("avant-restauration"));
    struct R { bool ok; QString msg; };
    auto* w = new QFutureWatcher<R>(this);
    connect(w, &QFutureWatcher<R>::finished, this, [this, w] {
        const R r = w->result();
        w->deleteLater();
        m_busy = false;
        reload();
        computeSizes();
        emit finished(r.ok, r.msg);
    });
    w->setFuture(QtConcurrent::run([job]() -> R {
        const QJsonObject m = QJsonDocument::fromJson(Archive::readZipEntry(job.file, QStringLiteral("manifest.json"))).object();
        if (m.isEmpty())
            return {false, QObject::tr("Cette copie est illisible.")};
        // 1. Copie de sécurité de l'état actuel.
        QVector<SaveSource> safety;
        for (const SaveSource& s : job.current)
            if (job.keys.contains(s.key))
                safety.push_back(s);
        if (!safety.isEmpty()) {
            QString err;
            qint64 b = 0;
            if (!SaveManager::doBackup(safety, job.safetyFile, QObject::tr("Avant restauration"), false, &err, &b) &&
                err != QObject::tr("aucune sauvegarde à copier"))
                return {false, QObject::tr("Copie de sécurité impossible (%1) : restauration annulée.").arg(err)};
        }
        // 2. Restauration (les fichiers absents de la copie ne sont pas supprimés).
        int restored = 0;
        for (const auto& v : m["sources"].toArray()) {
            const QJsonObject s = v.toObject();
            const QString key = s["key"].toString();
            if (!job.keys.contains(key))
                continue;
            if (key == QLatin1String("roms")) {
                const QJsonObject map = m["romFiles"].toObject();
                for (auto it = map.begin(); it != map.end(); ++it)
                    if (Archive::extractZipEntry(job.file, it.key(), it.value().toString()))
                        ++restored;
                continue;
            }
            QString root = s["root"].toString();
            for (const SaveSource& cur : job.current)
                if (cur.key == key)
                    root = cur.root; // dossier actuel de l'émulateur
            QString err;
            const int n = Archive::extractZipPrefix(job.file, key + QLatin1Char('/'), root, &err);
            if (n < 0)
                return {false, err};
            restored += n;
        }
        return {true, QObject::tr("%1 fichier(s) restauré(s). L'état précédent a été copié par sécurité.").arg(restored)};
    }));
}

bool SaveManager::remove(const QString& file) {
    const bool ok = QFile::remove(file);
    reload();
    return ok;
}

void SaveManager::maybeWeeklyBackup() {
    auto& s = Settings::instance();
    if (!s.value(QStringLiteral("saves/weekly"), true).toBool())
        return;
    const QDateTime last = s.value(QStringLiteral("saves/lastWeekly")).toDateTime();
    if (last.isValid() && last.daysTo(QDateTime::currentDateTime()) < 7)
        return;
    if (sources().isEmpty())
        return;
    s.setValue(QStringLiteral("saves/lastWeekly"), QDateTime::currentDateTime());
    backupNow({}, tr("Copie hebdomadaire automatique"), true);
}
