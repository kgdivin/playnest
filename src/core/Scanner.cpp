#include "Scanner.h"

#include "Paths.h"
#include "Registry.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>

Scanner::Scanner(QObject* parent) : QObject(parent) {}

Scanner::~Scanner() {
    if (m_cancel)
        m_cancel->store(true);
    if (m_watcher)
        m_watcher->waitForFinished();
}

QString Scanner::key(const QString& path) { return QDir::fromNativeSeparators(path).toLower(); }

void Scanner::start(const QStringList& folders, const QHash<QString, DetectedRom>& known) {
    if (m_running)
        return;
    m_running = true;
    m_folders = folders;
    m_cancel = std::make_shared<std::atomic_bool>(false);

    delete m_watcher;
    m_watcher = new QFutureWatcher<Result>(this);
    connect(m_watcher, &QFutureWatcher<Result>::finished, this, [this] {
        const Result r = m_watcher->result();
        m_running = false;
        emit finished(r.games, m_folders, r.cancelled);
    });
    m_watcher->setFuture(QtConcurrent::run(&Scanner::run, folders, known, m_cancel, this));
}

void Scanner::cancel() {
    if (m_cancel)
        m_cancel->store(true);
}

Scanner::Result Scanner::run(QStringList folders, QHash<QString, DetectedRom> known, std::shared_ptr<std::atomic_bool> cancel,
                             Scanner* self) {
    Result res;
    const QStringList exts = Registry::instance().allExtensions();
    const QSet<QString> extSet(exts.begin(), exts.end());
    QSet<QString> seenPaths;
    QStringList titleDirs; // dossiers déjà reconnus comme un jeu (on ignore leur contenu)
    int seen = 0;
    QElapsedTimer tick;
    tick.start();

    auto report = [&](const QString& current) {
        if (tick.elapsed() < 120)
            return;
        tick.restart();
        QMetaObject::invokeMethod(self, "progress", Qt::QueuedConnection, Q_ARG(int, seen),
                                  Q_ARG(int, res.games.size()), Q_ARG(QString, current));
    };
    auto insideTitleDir = [&](const QString& p) {
        for (const auto& d : titleDirs)
            if (p.startsWith(d + QLatin1Char('/')))
                return true;
        return false;
    };
    auto saveIcon = [](DetectedRom& r) {
        if (r.icon.isNull())
            return;
        const QByteArray h = QCryptographicHash::hash(r.path.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
        const QString file = Paths::iconsDir() + QLatin1Char('/') + QString::fromLatin1(h) + QStringLiteral(".png");
        if (QFileInfo::exists(file) || r.icon.save(file, "PNG"))
            r.iconFile = file;
        r.icon = QImage();
    };
    auto add = [&](DetectedRom r) {
        const QString k = key(r.path);
        if (seenPaths.contains(k))
            return;
        seenPaths.insert(k);
        if (r.fileSize == 0) {
            const QFileInfo fi(r.path);
            r.fileSize = fi.size();
            r.modified = fi.lastModified().toSecsSinceEpoch();
        }
        saveIcon(r);
        res.games.push_back(std::move(r));
    };
    // Fichier inchangé depuis le dernier scan : on réutilise le résultat (scan incrémental rapide).
    auto cached = [&](const QFileInfo& fi) -> std::optional<DetectedRom> {
        auto it = known.constFind(key(fi.absoluteFilePath()));
        if (it == known.constEnd())
            return std::nullopt;
        if (it->fileSize != fi.size() || it->modified != fi.lastModified().toSecsSinceEpoch())
            return std::nullopt;
        if (!it->iconFile.isEmpty() && !QFileInfo::exists(it->iconFile))
            return std::nullopt;
        return *it;
    };

    for (const QString& root : folders) {
        if (cancel->load())
            break;
        if (!QFileInfo(root).isDir())
            continue;
        if (auto r = RomDetector::detectDirectory(root)) {
            add(*r);
            continue;
        }
        QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (cancel->load()) {
                res.cancelled = true;
                break;
            }
            const QString path = it.next();
            const QFileInfo fi = it.fileInfo();
            if (insideTitleDir(path))
                continue;
            if (fi.isDir()) {
                const QString name = fi.fileName();
                if (name.startsWith(QLatin1Char('.')))
                    continue;
                if (auto r = RomDetector::detectDirectory(path)) {
                    titleDirs << path;
                    add(*r);
                }
                continue;
            }
            ++seen;
            if (!extSet.contains(fi.suffix().toLower())) {
                report(path);
                continue;
            }
            if (auto c = cached(fi)) {
                add(*c);
            } else if (auto r = RomDetector::detectFile(path)) {
                r->fileSize = fi.size();
                r->modified = fi.lastModified().toSecsSinceEpoch();
                add(*r);
            }
            report(path);
        }
    }
    if (cancel->load())
        res.cancelled = true;
    return res;
}
