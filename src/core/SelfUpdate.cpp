#include "SelfUpdate.h"

#include "AppInfo.h"
#include "Archive.h"
#include "Net.h"
#include "Paths.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

SelfUpdate& SelfUpdate::instance() {
    static SelfUpdate s;
    return s;
}

QString SelfUpdate::repo() const {
    return Settings::instance().value(QStringLiteral("app/updateRepo"), QString::fromLatin1(AppInfo::kDefaultUpdateRepo)).toString().trimmed();
}

void SelfUpdate::setRepo(const QString& repo) {
    QString r = repo.trimmed();
    r.remove(QRegularExpression(QStringLiteral("^https?://github\\.com/")));
    r.remove(QRegularExpression(QStringLiteral("(\\.git)?/?$")));
    Settings::instance().setValue(QStringLiteral("app/updateRepo"), r);
    Settings::instance().sync();
}

void SelfUpdate::cleanupOldFiles() {
    if (const QString appImage = qEnvironmentVariable("APPIMAGE"); !appImage.isEmpty())
        QFile::remove(appImage + QStringLiteral(".old"));
    QDirIterator it(Paths::appDir(), {QStringLiteral("*.old")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        if (!f.contains(QLatin1String("/data/")))
            QFile::remove(f);
    }
}

void SelfUpdate::check(bool force) {
    const QString r = repo();
    if (r.isEmpty()) {
        emit checked(false, {}, tr("Aucun dépôt de mises à jour n'est configuré."));
        return;
    }
    auto& s = Settings::instance();
    const QDateTime last = s.value(QStringLiteral("app/lastUpdateCheck")).toDateTime();
    if (!force && last.isValid() && last.secsTo(QDateTime::currentDateTime()) < 20 * 3600 && m_url.isEmpty()) {
        emit checked(false, {}, {});
        return;
    }
    const QUrl api(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(r));
    Net::instance().get(api, this, [this](bool ok, const QByteArray& data, const QString& err) {
        Settings::instance().setValue(QStringLiteral("app/lastUpdateCheck"), QDateTime::currentDateTime());
        if (!ok) {
            emit checked(false, {}, err);
            return;
        }
        const QJsonObject rel = QJsonDocument::fromJson(data).object();
        const QString tag = rel.value(QStringLiteral("tag_name")).toString();
        const QRegularExpression re(QString::fromLatin1(AppInfo::kUpdateAssetPattern));
        QString url, asset;
        for (const auto& av : rel.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject a = av.toObject();
            if (re.match(a.value(QStringLiteral("name")).toString()).hasMatch()) {
                url = a.value(QStringLiteral("browser_download_url")).toString();
                asset = a.value(QStringLiteral("name")).toString();
                break;
            }
        }
        if (tag.isEmpty() || url.isEmpty() || Archive::compareVersions(tag, QCoreApplication::applicationVersion()) <= 0) {
            m_url.clear();
            emit checked(false, tag, {});
            return;
        }
        m_version = tag;
        m_url = url;
        m_asset = asset;
        m_notes = rel.value(QStringLiteral("body")).toString().left(1500);
        m_page = rel.value(QStringLiteral("html_url")).toString();
        emit checked(true, tag, {});
    });
}

void SelfUpdate::apply() {
    if (m_busy || m_url.isEmpty())
        return;
#ifndef Q_OS_WIN
    // Linux : une AppImage se remplace elle-même. Dans les autres cas (macOS, version compilée à la main),
    // on ouvre la page de la version : l'installation reste un glisser-déposer.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const bool selfReplace = m_asset.endsWith(QLatin1String(".appimage"), Qt::CaseInsensitive) && !appImage.isEmpty() &&
                             QFileInfo(appImage).isFile() && QFileInfo(QFileInfo(appImage).absolutePath()).isWritable();
    if (!selfReplace) {
        QDesktopServices::openUrl(QUrl(m_page.isEmpty() ? QStringLiteral("https://github.com/%1/releases/latest").arg(repo()) : m_page));
        return;
    }
    m_busy = true;
    emit progress(tr("Téléchargement de %1 %2…").arg(QString::fromLatin1(AppInfo::kName), m_version), 0, 0);
    const QString fresh = appImage + QStringLiteral(".new");
    QFile::remove(fresh);
    Net::instance().download(
        QUrl(m_url), fresh, this, [this](qint64 r, qint64 t) { emit progress(tr("Téléchargement…"), r, t); },
        [this, fresh, appImage](bool ok, const QString& err) {
            m_busy = false;
            if (!ok) {
                QFile::remove(fresh);
                emit failed(tr("Échec du téléchargement : %1").arg(err));
                return;
            }
            QFile::setPermissions(fresh, QFile::permissions(fresh) | QFile::ExeOwner | QFile::ExeUser | QFile::ExeGroup | QFile::ExeOther);
            // Sous Linux, un programme en cours peut être remplacé : l'ancienne version est gardée en .old jusqu'au prochain démarrage.
            QFile::remove(appImage + QStringLiteral(".old"));
            if (!QFile::rename(appImage, appImage + QStringLiteral(".old")) || !QFile::rename(fresh, appImage)) {
                QFile::rename(appImage + QStringLiteral(".old"), appImage);
                emit failed(tr("Impossible de remplacer %1 (dossier protégé ?).").arg(appImage));
                return;
            }
            QProcess::startDetached(appImage, {QStringLiteral("--restarted")}, QFileInfo(appImage).absolutePath());
            QCoreApplication::quit();
        });
    return;
#endif
    m_busy = true;
    const QString archive = Paths::downloadsDir() + QLatin1Char('/') + m_asset;
    emit progress(tr("Téléchargement de %1 %2…").arg(QString::fromLatin1(AppInfo::kName), m_version), 0, 0);
    Net::instance().download(
        QUrl(m_url), archive, this, [this](qint64 r, qint64 t) { emit progress(tr("Téléchargement…"), r, t); },
        [this, archive](bool ok, const QString& err) {
            if (!ok) {
                m_busy = false;
                emit failed(tr("Échec du téléchargement : %1").arg(err));
                return;
            }
            emit progress(tr("Installation de la mise à jour…"), 0, 0);
            struct R { bool ok; QString msg; QString exe; };
            auto* w = new QFutureWatcher<R>(this);
            connect(w, &QFutureWatcher<R>::finished, this, [this, w] {
                const R r = w->result();
                w->deleteLater();
                m_busy = false;
                if (!r.ok) {
                    emit failed(r.msg);
                    return;
                }
                QProcess::startDetached(r.exe, {}, QFileInfo(r.exe).absolutePath());
                QCoreApplication::quit();
            });
            w->setFuture(QtConcurrent::run([archive]() -> R {
                const QString staging = Paths::stagingDir() + QStringLiteral("/app-update");
                Archive::removeTree(staging);
                QString err;
                if (!Archive::extract(archive, staging, &err))
                    return {false, QObject::tr("Extraction impossible : %1").arg(err), {}};
                const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
                QString newExe;
                QDirIterator it(staging, {exeName}, QDir::Files, QDirIterator::Subdirectories);
                if (it.hasNext())
                    newExe = it.next();
                if (newExe.isEmpty())
                    return {false, QObject::tr("La mise à jour ne contient pas %1.").arg(exeName), {}};
                const QString newRoot = QFileInfo(newExe).absolutePath();
                const QString appDir = Paths::appDir();
                QStringList renamed, added;
                auto undo = [&] { // remet l'application dans son état d'origine
                    for (const QString& d : added)
                        QFile::remove(d);
                    for (const QString& d : renamed) {
                        QFile::remove(d);
                        QFile::rename(d + QStringLiteral(".old"), d);
                    }
                };
                for (const QString& rel : Archive::listFiles(newRoot)) {
                    if (rel.startsWith(QLatin1String("data/")) || rel == QLatin1String("portable.txt"))
                        continue; // les données et le mode de l'utilisateur ne sont jamais remplacés
                    const QString dst = appDir + QLatin1Char('/') + rel;
                    if (QFileInfo::exists(dst)) {
                        const QString old = dst + QStringLiteral(".old");
                        QFile::remove(old);
                        if (!QFile::rename(dst, old)) {
                            undo();
                            return {false, QObject::tr("Impossible de remplacer %1 (dossier protégé ?).").arg(QDir::toNativeSeparators(dst)), {}};
                        }
                        renamed << dst;
                    } else {
                        added << dst;
                    }
                    QDir().mkpath(QFileInfo(dst).absolutePath());
                    if (!QFile::copy(newRoot + QLatin1Char('/') + rel, dst)) {
                        undo();
                        return {false, QObject::tr("Copie impossible : %1. Rien n'a été modifié.").arg(QDir::toNativeSeparators(dst)), {}};
                    }
                }
                Archive::removeTree(staging);
                QFile::remove(archive);
                return {true, {}, appDir + QLatin1Char('/') + exeName};
            }));
        });
}
