#include "Lang.h"
#include "Archive.h"

#include "Tools.h"

#include <QDir>

#include <cctype>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
// QZipReader et QZipWriter : dans QtGui jusqu'à Qt 6.6, dans QtCore ensuite.
#if __has_include(<QtCore/private/qzipreader_p.h>)
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>
#else
#include <QtGui/private/qzipreader_p.h>
#include <QtGui/private/qzipwriter_p.h>
#endif

namespace Archive {

namespace {
// Empêche une entrée d'archive de sortir du dossier de destination ("zip slip").
QString safeJoin(const QString& root, const QString& rel) {
    const QString clean = QDir::cleanPath(QString(rel).replace(QLatin1Char('\\'), QLatin1Char('/')));
    if (clean.isEmpty() || clean.startsWith(QLatin1String("../")) || clean == QLatin1String("..") ||
        QDir::isAbsolutePath(clean) || clean.contains(QLatin1Char(':')))
        return {};
    return QDir::cleanPath(root + QLatin1Char('/') + clean);
}

// QZipReader::fileData() relit les noms en « local 8 bits » alors que la liste les donne en UTF-8 :
// sous Windows, un fichier au nom accentué (ex. « Pokémon ») ou spécial n'était pas trouvé et sortait vide.
// De même, la liste remplace « \ » par « / » mais fileData() compare le nom brut (archives d'Azahar).
QByteArray zipData(const QZipReader& reader, const QString& name) {
    QByteArray data = reader.fileData(name);
    if (!data.isEmpty())
        return data;
    QStringList variants{QString::fromLocal8Bit(name.toUtf8())};
    if (name.contains(QLatin1Char('/'))) {
        const QString back = QString(name).replace(QLatin1Char('/'), QLatin1Char('\\'));
        variants << back << QString::fromLocal8Bit(back.toUtf8());
    }
    for (const QString& v : variants) {
        if (v == name)
            continue;
        data = reader.fileData(v);
        if (!data.isEmpty())
            return data;
    }
    return data;
}

bool runTool(const QString& program, const QStringList& args, QString* error, int timeoutMs = 30 * 60 * 1000) {
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForStarted(15000)) {
        if (error)
            *error = QObject::tr("Impossible de lancer %1.").arg(QFileInfo(program).fileName());
        return false;
    }
    p.waitForFinished(timeoutMs);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (error)
            *error = QString::fromLocal8Bit(p.readAll()).trimmed().right(600);
        return false;
    }
    return true;
}
} // namespace

bool extractZip(const QString& zip, const QString& destDir, QString* error) {
    QZipReader reader(zip);
    if (!reader.isReadable() || reader.status() != QZipReader::NoError) {
        if (error)
            *error = QObject::tr("Archive zip illisible ou corrompue.");
        return false;
    }
    QDir().mkpath(destDir);
    const auto entries = reader.fileInfoList();
    if (entries.isEmpty()) {
        if (error)
            *error = QObject::tr("Archive vide.");
        return false;
    }
    // Dossier noté « nom\ » (archives faites sous Windows, ex. Azahar) : Qt le présente comme un fichier vide « nom ».
    // Tout chemin qui est le parent d'une autre entrée est donc un dossier.
    QSet<QString> parents;
    for (const auto& e : entries)
        for (int i = e.filePath.indexOf(QLatin1Char('/')); i > 0; i = e.filePath.indexOf(QLatin1Char('/'), i + 1))
            parents.insert(e.filePath.left(i));
    for (const auto& e : entries) {
        const QString target = safeJoin(destDir, e.filePath);
        if (target.isEmpty())
            continue;
        const bool dirEntry = e.isDir || parents.contains(e.filePath) || e.filePath.endsWith(QLatin1Char('/'));
        if (dirEntry) {
            QDir().mkpath(target);
            continue;
        }
        if (!e.isFile)
            continue;
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly)) {
            if (error)
                *error = QObject::tr("Écriture impossible : %1").arg(QDir::toNativeSeparators(target));
            return false;
        }
        const QByteArray data = zipData(reader, e.filePath);
        if (data.size() != e.size) {
            if (error)
                *error = QObject::tr("Fichier corrompu dans l'archive : %1").arg(e.filePath);
            return false;
        }
        out.write(data);
        out.close();
#ifndef Q_OS_WIN
        if (e.permissions & (QFile::ExeOwner | QFile::ExeUser))
            out.setPermissions(out.permissions() | QFile::ExeOwner | QFile::ExeUser);
#endif
    }
    return true;
}

bool extract(const QString& archive, const QString& destDir, QString* error) {
    const QString low = archive.toLower();
    if (low.endsWith(QLatin1String(".zip")))
        return extractZip(archive, destDir, error);
    QDir().mkpath(destDir);
    if (low.endsWith(QLatin1String(".7z"))) {
        const QString sz = Tools::instance().sevenZipPath();
        if (sz.isEmpty()) {
            if (error)
                *error = QObject::tr("7-Zip est nécessaire pour extraire cette archive.");
            return false;
        }
        return runTool(sz, {QStringLiteral("x"), QStringLiteral("-y"), QStringLiteral("-o") + QDir::toNativeSeparators(destDir),
                            QDir::toNativeSeparators(archive)},
                       error);
    }
    // .tar.gz, .tar.xz… : tar (fourni avec Windows 10+ et Linux)
    return runTool(QStringLiteral("tar"), {QStringLiteral("-xf"), QDir::toNativeSeparators(archive), QStringLiteral("-C"),
                                           QDir::toNativeSeparators(destDir)},
                   error);
}

bool createZip(const QString& zipPath, const QVector<QPair<QString, QString>>& entries,
               const QVector<QPair<QString, QByteArray>>& extra, QString* error,
               const std::function<void(int, int)>& progress) {
    QDir().mkpath(QFileInfo(zipPath).absolutePath());
    const QString tmp = zipPath + QStringLiteral(".part");
    {
        QZipWriter writer(tmp);
        if (writer.status() != QZipWriter::NoError) {
            if (error)
                *error = QObject::tr("Impossible de créer %1").arg(QDir::toNativeSeparators(zipPath));
            return false;
        }
        writer.setCompressionPolicy(QZipWriter::AutoCompress);
        int done = 0;
        qint64 total = 0;
        for (const auto& [name, file] : entries) {
            const qint64 sz = QFileInfo(file).size();
            if (sz > 1024LL * 1024 * 1024) { // > 1 Go : hors limites du format, ignoré
                qWarning("Copie : fichier ignoré car trop volumineux : %s", qUtf8Printable(file));
                ++done;
                continue;
            }
            total += sz;
            if (total > 3500LL * 1024 * 1024) {
                if (error)
                    *error = QObject::tr("les sauvegardes dépassent 3,5 Go, trop pour une seule copie");
                writer.close();
                QFile::remove(tmp);
                return false;
            }
            QFile f(file);
            if (!f.open(QIODevice::ReadOnly)) {
                ++done;
                continue; // fichier verrouillé ou disparu : on continue
            }
            writer.addFile(name, &f);
            ++done;
            if (progress && (done % 20 == 0 || done == entries.size()))
                progress(done, int(entries.size()));
        }
        for (const auto& [name, data] : extra)
            writer.addFile(name, data);
        writer.close();
        if (writer.status() != QZipWriter::NoError) {
            if (error)
                *error = QObject::tr("Erreur d'écriture de l'archive (disque plein ?).");
            QFile::remove(tmp);
            return false;
        }
    }
    QFile::remove(zipPath);
    if (!QFile::rename(tmp, zipPath)) {
        if (error)
            *error = QObject::tr("Impossible de finaliser l'archive.");
        return false;
    }
    return true;
}

QByteArray readZipEntry(const QString& zipPath, const QString& entry) {
    QZipReader reader(zipPath);
    if (!reader.isReadable())
        return {};
    return zipData(reader, entry);
}

int extractZipPrefix(const QString& zipPath, const QString& prefix, const QString& destRoot, QString* error) {
    QZipReader reader(zipPath);
    if (!reader.isReadable()) {
        if (error)
            *error = QObject::tr("Archive illisible.");
        return -1;
    }
    int n = 0;
    for (const auto& e : reader.fileInfoList()) {
        if (!e.isFile || !e.filePath.startsWith(prefix) || e.filePath.endsWith(QLatin1Char('/')))
            continue;
        const QString target = safeJoin(destRoot, e.filePath.mid(prefix.size()));
        if (target.isEmpty())
            continue;
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly)) {
            if (error)
                *error = QObject::tr("Écriture impossible : %1").arg(QDir::toNativeSeparators(target));
            return -1;
        }
        out.write(zipData(reader, e.filePath));
        ++n;
    }
    return n;
}

bool extractZipEntry(const QString& zipPath, const QString& entry, const QString& destFile) {
    QZipReader reader(zipPath);
    if (!reader.isReadable())
        return false;
    const QByteArray data = zipData(reader, entry);
    QDir().mkpath(QFileInfo(destFile).absolutePath());
    QFile out(destFile);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(data);
    return true;
}

QStringList listFiles(const QString& root) {
    QStringList out;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << it.next().mid(root.size() + 1);
    return out;
}

qint64 treeSize(const QString& path) {
    const QFileInfo fi(path);
    if (fi.isFile())
        return fi.size();
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

bool copyFileReplace(const QString& src, const QString& dst, QString* error) {
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (QFileInfo::exists(dst) && !QFile::remove(dst)) {
        // Fichier verrouillé (DLL en cours d'utilisation) : on tente de l'écarter.
        const QString aside = dst + QStringLiteral(".old");
        QFile::remove(aside);
        if (!QFile::rename(dst, aside)) {
            if (error)
                *error = QObject::tr("Fichier verrouillé : %1").arg(QDir::toNativeSeparators(dst));
            return false;
        }
    }
    if (!QFile::copy(src, dst)) {
        if (error)
            *error = QObject::tr("Copie impossible vers %1").arg(QDir::toNativeSeparators(dst));
        return false;
    }
    return true;
}

bool removeTree(const QString& path) {
    if (path.isEmpty() || !QFileInfo::exists(path))
        return true;
    return QDir(path).removeRecursively();
}

int compareVersions(const QString& a, const QString& b) {
    static const QRegularExpression num(QStringLiteral("\\d+"));
    QList<qint64> na, nb;
    for (auto it = num.globalMatch(a); it.hasNext();)
        na << it.next().captured().toLongLong();
    for (auto it = num.globalMatch(b); it.hasNext();)
        nb << it.next().captured().toLongLong();
    // Les suffixes de pré-version (rc, beta) ne comptent pas comme une version supérieure.
    static const QRegularExpression pre(QStringLiteral("(?i)(rc|beta|alpha|pre|nightly)\\s*\\d*"));
    const auto ma = pre.match(a), mb = pre.match(b);
    auto core = [&](const QString& s, const QRegularExpressionMatch& m) {
        QList<qint64> out;
        const QString c = m.hasMatch() ? s.left(m.capturedStart()) : s;
        for (auto it = num.globalMatch(c); it.hasNext();)
            out << it.next().captured().toLongLong();
        return out;
    };
    const QList<qint64> ca = core(a, ma), cb = core(b, mb);
    for (int i = 0; i < qMax(ca.size(), cb.size()); ++i) {
        const qint64 x = i < ca.size() ? ca[i] : 0, y = i < cb.size() ? cb[i] : 0;
        if (x != y)
            return x > y ? 1 : -1;
    }
    if (ma.hasMatch() != mb.hasMatch())
        return ma.hasMatch() ? -1 : 1; // 1.0 > 1.0-rc1
    for (int i = 0; i < qMax(na.size(), nb.size()); ++i) {
        const qint64 x = i < na.size() ? na[i] : 0, y = i < nb.size() ? nb[i] : 0;
        if (x != y)
            return x > y ? 1 : -1;
    }
    return 0;
}

QString probeVersion(const QString& file, const QString& prefix) {
    if (prefix.isEmpty())
        return {};
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    if (prefix.startsWith(QLatin1String("re:"))) { // expression régulière sur le binaire, version = groupe 1
        const QRegularExpression re(prefix.mid(3));
        const qint64 chunk = 8 * 1024 * 1024;
        QByteArray tail;
        while (!f.atEnd()) {
            const QByteArray buf = tail + f.read(chunk);
            const auto m = re.match(QString::fromLatin1(buf));
            if (m.hasMatch())
                return m.captured(1);
            tail = buf.right(256);
        }
        return {};
    }
    const QByteArray needle = prefix.toUtf8();
    const QString label = prefix.section(QLatin1Char(' '), -1); // « build- »
    const qint64 chunk = 8 * 1024 * 1024;
    QByteArray tail;
    while (!f.atEnd()) {
        const QByteArray buf = tail + f.read(chunk);
        for (int i = buf.indexOf(needle); i >= 0; i = buf.indexOf(needle, i + 1)) {
            int j = i + needle.size();
            QByteArray num;
            while (j < buf.size() && (std::isdigit(static_cast<unsigned char>(buf[j])) || (buf[j] == '.' && !num.isEmpty())))
                num += buf[j++];
            if (j >= buf.size() && !f.atEnd())
                break; // numéro coupé en fin de bloc : on le relira avec le suivant
            if (!num.isEmpty())
                return label + QString::fromLatin1(num);
        }
        tail = buf.right(needle.size() + 16);
    }
    return {};
}

QStringList archiveEntries(const QString& archive) {
    QStringList out;
    if (archive.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) {
        QZipReader reader(archive);
        if (!reader.isReadable())
            return out;
        for (const auto& e : reader.fileInfoList())
            if (e.isFile) // certaines archives (Azahar) utilisent « \ » comme séparateur
                out << QString(e.filePath).replace(QLatin1Char('\\'), QLatin1Char('/'));
        return out;
    }
    const QString sz = Tools::instance().sevenZipPath();
    if (sz.isEmpty())
        return out;
    QProcess p;
    p.start(sz, {QStringLiteral("l"), QStringLiteral("-ba"), QStringLiteral("-slt"), QDir::toNativeSeparators(archive)});
    if (!p.waitForFinished(60000))
        return out;
    for (const QString& line : QString::fromLocal8Bit(p.readAllStandardOutput()).split(QLatin1Char('\n')))
        if (line.startsWith(QLatin1String("Path = ")))
            out << QDir::fromNativeSeparators(line.mid(7).trimmed());
    return out;
}

QString formatSize(qint64 bytes) {
    return Lang::locale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString versionFromName(const QString& name) {
    static const QRegularExpression semver(QStringLiteral("(?i)v?(\\d+\\.\\d+(?:\\.\\d+)*(?:-?(?:rc|beta|alpha)\\d*)?)"));
    static const QRegularExpression date(QStringLiteral("(20\\d{6})"));
    static const QRegularExpression build(QStringLiteral("(?<![\\d.])(2\\d{3})(?![\\d])"));
    if (const auto m = semver.match(name); m.hasMatch())
        return m.captured(1);
    if (const auto m = date.match(name); m.hasMatch())
        return m.captured(1);
    if (const auto m = build.match(name); m.hasMatch())
        return m.captured(1);
    return {};
}

} // namespace Archive
