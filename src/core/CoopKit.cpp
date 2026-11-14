#include "CoopKit.h"

#include "Archive.h"
#include "Library.h"
#include "Paths.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QUrl>

namespace {
const QString kDirKey = QStringLiteral("extras/oot-coop/dir");

QString startIn(const QString& dir) { return dir + QLatin1Char('/') + QString::fromLatin1(CoopKit::kStartFile); }
} // namespace

namespace CoopKit {

bool available() {
#ifdef Q_OS_WIN
    return true;
#else
    return qEnvironmentVariableIsSet("PLAYNEST_COOP_ANYWHERE"); // essais hors Windows
#endif
}

bool appliesTo(const Game& g) {
    return !g.isAddon && g.titleId.compare(QLatin1String(kTitleId), Qt::CaseInsensitive) == 0;
}

QString installDir() {
    const QString dir = QDir::fromNativeSeparators(Settings::instance().value(kDirKey).toString());
    return !dir.isEmpty() && QFileInfo(startIn(dir)).isFile() ? dir : QString();
}

bool installed() { return !installDir().isEmpty(); }

QString suggestedDir() {
    // Le kit construit le jeu sur place (environ 60 Go) : on propose le disque qui a le plus de place.
    QString best = QDir::rootPath();
    qint64 bestFree = -1;
    for (const QStorageInfo& v : QStorageInfo::mountedVolumes()) {
        if (!v.isValid() || !v.isReady() || v.isReadOnly())
            continue;
#ifdef Q_OS_WIN
        if (v.rootPath().size() > 3)
            continue; // lecteurs réseau, points de montage
#else
        if (v.rootPath() != QLatin1String("/") && !v.rootPath().startsWith(QDir::homePath()))
            continue;
#endif
        if (v.bytesAvailable() > bestFree) {
            bestFree = v.bytesAvailable();
            best = v.rootPath();
        }
    }
    if (!best.endsWith(QLatin1Char('/')))
        best += QLatin1Char('/');
#ifndef Q_OS_WIN
    best = QDir::homePath() + QLatin1Char('/');
#endif
    return QDir::toNativeSeparators(best + QStringLiteral("OoT-Coop"));
}

QString dirWarning(const QString& chosen) {
    const QString dir = QDir::fromNativeSeparators(chosen);
    const QString lower = dir.toLower();
    if (lower.contains(QLatin1String("onedrive")))
        return QCoreApplication::translate("CoopKit", "Ce dossier est dans OneDrive : le kit n'y fonctionne pas. Choisis un dossier hors de OneDrive.");
    if (lower.contains(QLatin1String("/program files")))
        return QCoreApplication::translate("CoopKit", "Ce dossier est dans Program Files, protégé par Windows. Choisis un dossier personnel.");
    if (dir.size() > 60)
        return QCoreApplication::translate("CoopKit", "Ce chemin est long : le kit demande un dossier court, par exemple C:\\OoT-Coop.");
    QString probe = dir;
    while (!probe.isEmpty() && !QFileInfo::exists(probe))
        probe = QFileInfo(probe).absolutePath() == probe ? QString() : QFileInfo(probe).absolutePath();
    const QStorageInfo disk(probe.isEmpty() ? QDir::rootPath() : probe);
    if (disk.isValid() && disk.bytesAvailable() < qint64(kNeededGb) * 1024 * 1024 * 1024)
        return QCoreApplication::translate("CoopKit", "Il reste %1 sur ce disque : la première construction du jeu demande environ %2 Go.")
            .arg(Archive::formatSize(disk.bytesAvailable()))
            .arg(kNeededGb);
    return {};
}

bool pickRelease(const QByteArray& releasesJson, Release* out) {
    static const QRegularExpression kit(QStringLiteral("(?i)^oot-coop-v?[\\d.]+.*\\.zip$"));
    for (const auto& rv : QJsonDocument::fromJson(releasesJson).array()) { // la plus récente d'abord
        const QJsonObject rel = rv.toObject();
        if (rel.value(QStringLiteral("draft")).toBool())
            continue;
        for (const auto& av : rel.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject a = av.toObject();
            const QString name = a.value(QStringLiteral("name")).toString();
            if (!kit.match(name).hasMatch())
                continue;
            if (out)
                *out = {rel.value(QStringLiteral("tag_name")).toString(), name, a.value(QStringLiteral("browser_download_url")).toString()};
            return true;
        }
    }
    return false;
}

bool installZip(const QString& zip, const QString& chosen, QString* error) {
    const QString dir = QDir::fromNativeSeparators(chosen);
    const QString staging = dir + QStringLiteral(".playnest-tmp");
    Archive::removeTree(staging);
    QString err;
    if (!QDir().mkpath(staging) || !Archive::extract(zip, staging, &err)) {
        Archive::removeTree(staging);
        if (error)
            *error = err.isEmpty() ? QCoreApplication::translate("CoopKit", "Impossible de créer %1").arg(QDir::toNativeSeparators(dir)) : err;
        return false;
    }
    // Le zip range tout dans un dossier « oot-coop-v… » : c'est son contenu qui devient le dossier du kit.
    QString root;
    QDirIterator it(staging, {QString::fromLatin1(kStartFile)}, QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext())
        root = QFileInfo(it.next()).absolutePath();
    if (root.isEmpty()) {
        Archive::removeTree(staging);
        if (error)
            *error = QCoreApplication::translate("CoopKit", "Ce fichier n'est pas le kit attendu : « %1 » est introuvable dedans.").arg(QString::fromLatin1(kStartFile));
        return false;
    }
    QDir().mkpath(dir);
    for (const QString& rel : Archive::listFiles(root)) {
        const QString dst = dir + QLatin1Char('/') + rel;
        // Les réglages d'un kit déjà présent sont gardés.
        if (rel == QLatin1String("coop-options.ini") && QFileInfo::exists(dst))
            continue;
        QDir().mkpath(QFileInfo(dst).absolutePath());
        QFile::remove(dst);
        if (!QFile::rename(root + QLatin1Char('/') + rel, dst) && !QFile::copy(root + QLatin1Char('/') + rel, dst)) {
            Archive::removeTree(staging);
            if (error)
                *error = QCoreApplication::translate("CoopKit", "Copie impossible : %1").arg(QDir::toNativeSeparators(dst));
            return false;
        }
    }
    Archive::removeTree(staging);
    return adopt(dir);
}

bool adopt(const QString& chosen) {
    const QString dir = QDir::fromNativeSeparators(chosen);
    if (!QFileInfo(startIn(dir)).isFile())
        return false;
    Settings::instance().setValue(kDirKey, QDir::toNativeSeparators(dir));
    Settings::instance().sync();
    return true;
}

QString gameFileFor(const Game& g) {
    QString path = QDir::fromNativeSeparators(g.path);
    if (path.endsWith(QLatin1String("/exefs/main"))) // jeu décompressé : le kit demande les dossiers romfs puis exefs
        path = path.left(path.size() - 11) + QStringLiteral("/romfs");
    return QDir::toNativeSeparators(path);
}

bool launch(QString* error) {
    const QString dir = installDir();
    if (dir.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("CoopKit", "Le kit %1 est introuvable. Réinstalle-le.").arg(QString::fromLatin1(kName));
        return false;
    }
    const QString bat = QDir::toNativeSeparators(startIn(dir));
#ifdef Q_OS_WIN
    // « start » ouvre le fichier de démarrage dans sa propre fenêtre de commandes, indépendante de Playnest.
    if (QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), QStringLiteral("start"), QString(), bat}, QDir::toNativeSeparators(dir)))
        return true;
#endif
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(startIn(dir))))
        return true;
    if (error)
        *error = QCoreApplication::translate("CoopKit", "Impossible de lancer %1.").arg(bat);
    return false;
}

} // namespace CoopKit
