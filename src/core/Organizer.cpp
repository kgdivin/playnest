#include "Organizer.h"

#include "Archive.h"
#include "Emulators.h"
#include "Lang.h"
#include "Library.h"
#include "Paths.h"
#include "Registry.h"
#include "RomDetector.h"
#include "Settings.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <functional>

namespace {

Qt::CaseSensitivity pathCs() {
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString norm(const QString& p) { return QDir::cleanPath(QDir::fromNativeSeparators(p)); }

bool samePath(const QString& a, const QString& b) { return norm(a).compare(norm(b), pathCs()) == 0; }

bool isInside(const QString& path, const QString& dir) {
    const QString p = norm(path), d = norm(dir);
    return p.startsWith(d + QLatin1Char('/'), pathCs());
}

QString journalDir() { return Paths::dataDir() + QStringLiteral("/organize"); }

QString kindKey(Organizer::Kind k) {
    switch (k) {
    case Organizer::Game: return QStringLiteral("game");
    case Organizer::Update: return QStringLiteral("update");
    case Organizer::Dlc: return QStringLiteral("dlc");
    case Organizer::Emulator: return QStringLiteral("emulator");
    case Organizer::Save: return QStringLiteral("save");
    }
    return {};
}

// Dossier existant le plus proche (la destination n'existe pas encore pendant la préparation).
QString existingAncestor(const QString& path) {
    QString p = norm(path);
    while (!p.isEmpty() && !QFileInfo::exists(p)) {
        const QString up = QFileInfo(p).path();
        if (up == p)
            break;
        p = up;
    }
    return p;
}

bool sameDevice(const QString& a, const QString& b) {
    const QStorageInfo sa(existingAncestor(a)), sb(existingAncestor(b));
    if (!sa.isValid() || !sb.isValid())
        return false;
    return samePath(sa.rootPath(), sb.rootPath());
}

// Dossiers génériques qu'on ne déplace jamais (un émulateur posé directement dedans reste en place).
bool isGenericFolder(const QString& dir, bool byName = true) {
    const QString d = norm(dir);
    if (QFileInfo(d).isRoot() || samePath(d, QDir::homePath()))
        return true;
    for (auto loc : {QStandardPaths::DownloadLocation, QStandardPaths::DesktopLocation, QStandardPaths::DocumentsLocation,
                     QStandardPaths::HomeLocation, QStandardPaths::AppDataLocation, QStandardPaths::GenericDataLocation})
        for (const QString& s : QStandardPaths::standardLocations(loc))
            if (samePath(d, s))
                return true;
    if (!byName)
        return false;
    static const QStringList names = {"downloads", "téléchargements", "desktop", "bureau", "documents", "program files",
                                      "program files (x86)", "programs", "appdata", "local", "roaming", "users", "games", "jeux"};
    return names.contains(QFileInfo(d).fileName().toLower());
}

bool underProgramFiles(const QString& dir) {
    const QString d = norm(dir).toLower();
    return d.contains(QLatin1String("/program files/")) || d.contains(QLatin1String("/program files (x86)/")) ||
           d.endsWith(QLatin1String("/program files")) || d.contains(QLatin1String("/windowsapps/"));
}

QString uniqueTarget(const QString& target, QSet<QString>& taken) {
    QString t = norm(target);
    const QFileInfo fi(t);
    const QString base = fi.isDir() || fi.suffix().isEmpty() ? fi.fileName() : fi.completeBaseName();
    const QString ext = fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix();
    for (int n = 2; QFileInfo::exists(t) || taken.contains(t.toLower()); ++n)
        t = fi.path() + QLatin1Char('/') + QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(ext);
    taken.insert(t.toLower());
    return t;
}

QString safeName(QString s) {
    static const QString bad = QStringLiteral("<>:\"/\\|?*");
    for (QChar& c : s)
        if (bad.contains(c) || c.unicode() < 32)
            c = QLatin1Char(' ');
    return s.simplified();
}

// Nom de dossier pour un jeu en dossier (dump Switch, jeu Wii U) : son titre, pas « base » ou « MKW [ROM] ».
QString titleFolder(const QString& title) {
    QString n = safeName(title);
    n.remove(QChar(0x2122)).remove(QChar(0x00AE)); // ™ ®
    while (n.endsWith(QLatin1Char('.')))
        n.chop(1);
    return n.left(120).trimmed();
}

// « Mario Kart World (2) » est déjà au bon nom : uniqueTarget l'a numéroté parce qu'un autre jeu porte le même titre.
bool sameFolderName(const QString& current, const QString& wanted) {
    static const QRegularExpression num(QStringLiteral("\\s\\(\\d+\\)$"));
    QString c = current;
    c.remove(num);
    return c.compare(wanted, Qt::CaseInsensitive) == 0;
}

// Dossier d'un jeu à déplacer en entier, à partir du dossier qui contient exefs/ ou code/.
// Dossier créé par le rangement (« Jeux », « Switch 2 »…) : il contient plusieurs jeux, on ne le déplace jamais.
bool organizerFolder(const QString& dir) {
    const QString name = QFileInfo(dir).fileName();
    for (auto k : {Organizer::Game, Organizer::Update, Organizer::Dlc, Organizer::Emulator, Organizer::Save})
        if (Organizer::kindFolderNames(k).contains(name, Qt::CaseInsensitive))
            return true;
    for (const char* fixed : {"PC", "Autres", "Other", "Jeux Nintendo", "Nintendo Games"})
        if (name.compare(QLatin1String(fixed), Qt::CaseInsensitive) == 0)
            return true;
    for (const SystemInfo& sys : Registry::instance().systems())
        if (name.compare(Organizer::consoleFolder(sys.id), Qt::CaseInsensitive) == 0 || name.compare(sys.name, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

// « racine/base » remonte à la racine seulement si elle appartient au jeu : elle contient « updates »,
// ou rien d'autre que « base » (« MKW [ROM] »). Jamais un dossier partagé comme « Jeux/Switch 2 ».
QString exefsUnit(const QString& dir) {
    if (QFileInfo(dir).fileName().compare(QLatin1String("base"), Qt::CaseInsensitive) != 0)
        return norm(dir);
    const QString up = norm(QFileInfo(dir).absolutePath());
    if (isGenericFolder(up) || organizerFolder(up))
        return norm(dir);
    if (QFileInfo(up + QStringLiteral("/updates")).isDir() || QFileInfo(up + QStringLiteral("/update")).isDir())
        return up;
    for (const QFileInfo& e : QDir(up).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        if (e.fileName().compare(QLatin1String("base"), Qt::CaseInsensitive) != 0 && !e.fileName().startsWith(QLatin1Char('.')))
            return norm(dir); // le dossier contient d'autres jeux ou fichiers : on ne déplace que « base »
    return up;
}

// --- Exécution (thread de travail) -----------------------------------------
struct Runner {
    std::atomic_bool* cancel;
    std::function<void(const QString&, qint64)> report; // nom courant, octets faits
    qint64 done = 0;
    QString error;

    bool copyFile(const QString& src, const QString& dst) {
        QFile in(src);
        if (!in.open(QIODevice::ReadOnly)) {
            error = QObject::tr("Lecture impossible : %1").arg(QDir::toNativeSeparators(src));
            return false;
        }
        QDir().mkpath(QFileInfo(dst).absolutePath());
        const QString part = dst + QStringLiteral(".playnest-part");
        QFile out(part);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            error = QObject::tr("Écriture impossible : %1").arg(QDir::toNativeSeparators(dst));
            return false;
        }
        QByteArray buf;
        buf.resize(8 * 1024 * 1024);
        const QString name = QFileInfo(src).fileName();
        while (true) {
            if (*cancel) {
                out.close();
                QFile::remove(part);
                return false;
            }
            const qint64 n = in.read(buf.data(), buf.size());
            if (n < 0) {
                error = QObject::tr("Lecture interrompue : %1").arg(QDir::toNativeSeparators(src));
                out.close();
                QFile::remove(part);
                return false;
            }
            if (n == 0)
                break;
            if (out.write(buf.constData(), n) != n) {
                error = QObject::tr("Écriture interrompue (disque plein ?) : %1").arg(QDir::toNativeSeparators(dst));
                out.close();
                QFile::remove(part);
                return false;
            }
            done += n;
            report(name, done);
        }
        out.close();
        if (QFileInfo(part).size() != QFileInfo(src).size() || !QFile::rename(part, dst)) {
            error = QObject::tr("Copie incomplète : %1").arg(QDir::toNativeSeparators(dst));
            QFile::remove(part);
            return false;
        }
        return true;
    }

    // Copie sans toucher à l'original.
    bool copy(const QString& from, const QString& to, bool isDir) {
        if (!isDir)
            return copyFile(from, to);
        for (const QString& rel : Archive::listFiles(from))
            if (!copyFile(from + QLatin1Char('/') + rel, to + QLatin1Char('/') + rel)) {
                Archive::removeTree(to);
                return false;
            }
        return true;
    }

    // Déplace un fichier ou un dossier. Même disque : renommage. Sinon : copie, vérification, suppression.
    bool move(const QString& from, const QString& to, bool isDir, qint64 size) {
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (sameDevice(from, QFileInfo(to).absolutePath())) {
            const bool ok = isDir ? QDir().rename(from, to) : QFile::rename(from, to);
            if (!ok) {
                error = QObject::tr("« %1 » est utilisé par un autre programme ou protégé.").arg(QFileInfo(from).fileName());
                return false;
            }
            done += size;
            report(QFileInfo(from).fileName(), done);
            return true;
        }
        if (!isDir) {
            if (!copyFile(from, to))
                return false;
            if (!QFile::remove(from)) { // la copie est complète : on garde les deux plutôt que de perdre l'original
                error = QObject::tr("« %1 » a été copié mais l'original n'a pas pu être supprimé.").arg(QFileInfo(from).fileName());
                return true;
            }
            return true;
        }
        const QStringList files = Archive::listFiles(from);
        QStringList copied;
        for (const QString& rel : files) {
            if (!copyFile(from + QLatin1Char('/') + rel, to + QLatin1Char('/') + rel)) {
                Archive::removeTree(to); // rien n'est supprimé à la source
                return false;
            }
            copied << rel;
        }
        QDir().mkpath(to);
        Archive::removeTree(from);
        return true;
    }
};

// Supprime les dossiers devenus vides (du plus profond au moins profond), sauf les dossiers génériques.
QStringList removeEmptyDirs(const QStringList& candidates, const QString& keepRoot) {
    QStringList dirs;
    for (const QString& c : candidates) {
        QString d = norm(c);
        while (!d.isEmpty() && !isGenericFolder(d, false) &&
               (keepRoot.isEmpty() || (!samePath(d, keepRoot) && !isInside(keepRoot, d) && !isInside(d, keepRoot)))) {
            if (!dirs.contains(d, pathCs()))
                dirs << d;
            const QString up = QFileInfo(d).path();
            if (up == d)
                break;
            d = up;
        }
    }
    std::sort(dirs.begin(), dirs.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
    QStringList removed;
    for (const QString& d : dirs) {
        QDir dir(d);
        if (dir.exists() && dir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System) && QDir().rmdir(d))
            removed << d;
    }
    return removed;
}

} // namespace

// ===========================================================================
Organizer::Organizer(Library* lib, QObject* parent) : QObject(parent), m_lib(lib) {
    qRegisterMetaType<Organizer::Result>("Organizer::Result");
}

namespace {
// Noms des dossiers de rangement, en français puis en anglais (même ordre que Organizer::Kind).
const char* const kKindNames[2][5] = {{"Jeux", "Mises à jour", "DLC", "Émulateurs", "Sauvegardes"},
                                      {"Games", "Updates", "DLC", "Emulators", "Saves"}};
const char* const kRootNames[2] = {"Jeux Nintendo", "Nintendo Games"};

// Langue d'un dossier déjà rangé, d'après les sous-dossiers qu'il contient ; vide s'il n'est pas rangé.
QString treeLang(const QString& root) {
    for (int l = 0; l < 2; ++l)
        for (int k : {0, 1, 3})
            if (QFileInfo(root + QLatin1Char('/') + QString::fromUtf8(kKindNames[l][k])).isDir())
                return l == 0 ? QStringLiteral("fr") : QStringLiteral("en");
    return {};
}
} // namespace

QString Organizer::defaultRoot() {
    const QString saved = Settings::instance().value(QStringLiteral("organize/root")).toString();
    if (!saved.isEmpty())
        return QDir::toNativeSeparators(saved);
    // Un dossier déjà rangé (dans l'une ou l'autre langue) est repris ; sinon on suit la langue de l'interface.
    for (const char* name : kRootNames)
        if (const QString dir = QDir::homePath() + QLatin1Char('/') + QString::fromUtf8(name); QFileInfo(dir).isDir())
            return QDir::toNativeSeparators(dir);
    return QDir::toNativeSeparators(QDir::homePath() + QLatin1Char('/') + QString::fromUtf8(kRootNames[Lang::current() == QLatin1String("en") ? 1 : 0]));
}

QString Organizer::folderLang() {
    const QString saved = Settings::instance().value(QStringLiteral("organize/lang")).toString();
    if (saved == QLatin1String("fr") || saved == QLatin1String("en"))
        return saved;
    const QString found = treeLang(QDir::fromNativeSeparators(defaultRoot()));
    return found.isEmpty() ? Lang::current() : found;
}

QString Organizer::kindFolder(Kind k) { return QString::fromUtf8(kKindNames[folderLang() == QLatin1String("en") ? 1 : 0][int(k)]); }

QStringList Organizer::kindFolderNames(Kind k) { return {QString::fromUtf8(kKindNames[0][int(k)]), QString::fromUtf8(kKindNames[1][int(k)])}; }

bool Organizer::isOrganized(const QString& root) { return !treeLang(QDir::fromNativeSeparators(root)).isEmpty(); }

QString Organizer::consoleFolder(const QString& systemId) {
    if (systemId == QLatin1String("pc"))
        return QStringLiteral("PC");
    const SystemInfo* s = Registry::instance().system(systemId);
    if (!s)
        return folderLang() == QLatin1String("en") ? QStringLiteral("Other") : QStringLiteral("Autres");
    QString n = s->name;
    if (n.startsWith(QLatin1String("Nintendo ")) && n != QLatin1String("Nintendo 64"))
        n = n.mid(9); // « Nintendo Switch 2 » → « Switch 2 », mais « Nintendo 64 » reste lisible
    return safeName(n);
}

Organizer::Plan Organizer::plan(const Options& options) const {
    Plan plan;
    plan.options = options;
    const QString root = norm(options.root);
    QSet<QString> taken;
    auto finish = [&](Move m) {
        if (!QFileInfo::exists(m.from)) {
            ++plan.skippedMissing;
            return;
        }
        const bool sameParent = samePath(QFileInfo(m.from).absolutePath(), QFileInfo(m.to).absolutePath());
        // Déjà dans le bon dossier. Exception : un jeu en dossier mal nommé (« base ») est renommé d'après son titre.
        if (samePath(m.from, m.to) || (sameParent && (!m.renameInPlace || sameFolderName(QFileInfo(m.from).fileName(), QFileInfo(m.to).fileName())))) {
            ++plan.alreadyInPlace;
            return;
        }
        m.to = uniqueTarget(m.to, taken);
        m.crossDevice = !sameDevice(m.from, root);
        for (const QString& c : m.companions)
            m.size += QFileInfo(c).size();
        plan.totalBytes += m.size;
        if (m.crossDevice)
            plan.copyBytes += m.size;
        plan.moves << m;
    };

    if (options.games && m_lib) {
        for (const ::Game& g : m_lib->games()) {
            if (g.missing)
                continue;
            Move m;
            m.kind = !g.isAddon ? Game : (g.addonKind == QLatin1String("update") ? Update : Dlc);
            m.group = consoleFolder(g.system());
            // Jeux en dossier : on déplace le dossier entier, jamais le seul fichier lancé.
            const QString gp = norm(g.path);
            QString unit = gp;
            if (g.system() == QLatin1String("pc")) { // jeu PC : tout son dossier, s'il lui est propre
                const QString dir = QFileInfo(gp).absolutePath();
                if (isGenericFolder(dir) || underProgramFiles(dir) || isInside(root, dir) || samePath(root, dir))
                    continue; // installé ailleurs (Program Files, Téléchargements…) : il reste où il est
                unit = dir;
            } else if (gp.endsWith(QLatin1String("/exefs/main"))) {
                unit = exefsUnit(QFileInfo(QFileInfo(gp).absolutePath()).absolutePath());
            } else if (QFileInfo(gp).absoluteDir().dirName().compare(QLatin1String("code"), Qt::CaseInsensitive) == 0 &&
                       gp.endsWith(QLatin1String(".rpx"), Qt::CaseInsensitive)) {
                unit = QFileInfo(QFileInfo(gp).absolutePath()).absolutePath(); // Wii U : dossier du titre
            }
            if (unit != gp)
                m.gameRel = gp.mid(unit.size() + 1);
            const QFileInfo fi(unit);
            m.isDir = fi.isDir();
            m.from = unit;
            m.size = m.isDir ? Archive::treeSize(unit) : fi.size();
            const ::Game* base = g.isAddon ? m_lib->baseFor(g) : nullptr;
            m.label = g.isAddon ? Library::addonLabel(g, base) + QStringLiteral(" (") + (base ? base->displayTitle() : g.title) + QLatin1Char(')')
                                : g.displayTitle();
            QString name = fi.fileName();
            if (m.isDir && !m.gameRel.isEmpty() && g.system() != QLatin1String("pc") && !g.isAddon) {
                if (const QString t = titleFolder(g.displayTitle()); !t.isEmpty()) {
                    name = t;
                    m.renameInPlace = true;
                }
            }
            m.to = root + QLatin1Char('/') + kindFolder(m.kind) + QLatin1Char('/') + m.group + QLatin1Char('/') + name;
            if (isInside(root, m.from) || samePath(root, m.from)) { // la racine choisie est dans ce jeu : impossible
                plan.warnings << tr("« %1 » contient le dossier de rangement, il reste en place.").arg(m.label);
                continue;
            }
            if (!m.isDir && m.gameRel.isEmpty()) { // sauvegardes posées à côté de la ROM (melonDS, mupen64…) : elles suivent le jeu
                const QString stem = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName();
                for (const char* ext : {".sav", ".dsv", ".srm", ".eep", ".sra", ".fla", ".mpk"})
                    if (QFileInfo::exists(stem + QLatin1String(ext)))
                        m.companions << norm(stem + QLatin1String(ext));
            }
            finish(m);
        }
    }

    if (options.emulators) {
        auto& em = EmulatorManager::instance();
        QHash<QString, Move> byDir;
        QStringList order;
        for (const EmulatorDef& e : Registry::instance().emulators()) {
            const EmulatorStatus st = em.status(e.id);
            if (!st.installed() || st.managed)
                continue;
            const QString dir = norm(st.dir);
            if (isGenericFolder(dir)) {
                plan.warnings << tr("%1 est rangé directement dans « %2 » : il reste en place.").arg(e.name, QFileInfo(dir).fileName());
                continue;
            }
            if (underProgramFiles(dir)) {
                plan.warnings << tr("%1 est installé dans Program Files : il reste en place.").arg(e.name);
                continue;
            }
            if (isInside(root, dir) || samePath(root, dir)) {
                plan.warnings << tr("Le dossier de rangement est dans celui de %1 : choisis un autre emplacement.").arg(e.name);
                continue;
            }
            if (em.isRunning(e.id)) {
                plan.warnings << tr("%1 est ouvert : ferme-le pour le ranger.").arg(e.name);
                continue;
            }
            const QString key = dir.toLower();
            if (byDir.contains(key)) { // Eden et Lichii dans le même dossier : un seul déplacement
                byDir[key].label += QStringLiteral(", ") + e.name;
                byDir[key].emulatorId += QLatin1Char(',') + e.id;
                continue;
            }
            Move m;
            m.kind = Emulator;
            m.isDir = true;
            m.from = dir;
            m.label = e.name;
            m.group = e.name;
            m.emulatorId = e.id;
            m.to = root + QLatin1Char('/') + kindFolder(Emulator) + QLatin1Char('/') + safeName(QFileInfo(dir).fileName());
            byDir.insert(key, m);
            order << key;
        }
        for (const QString& k : order) {
            Move m = byDir.value(k);
            m.size = Archive::treeSize(m.from);
            finish(m);
        }
    }

    if (options.saves) {
        const QString target = root + QLatin1Char('/') + kindFolder(Save);
        if (!samePath(Paths::backupsDir(), target)) {
            QDirIterator it(Paths::backupsDir(), {QStringLiteral("*.zip")}, QDir::Files);
            while (it.hasNext()) {
                const QString f = it.next();
                Move m;
                m.kind = Save;
                m.from = norm(f);
                m.label = QFileInfo(f).completeBaseName();
                m.group = kindFolder(Save);
                m.size = QFileInfo(f).size();
                m.to = target + QLatin1Char('/') + QFileInfo(f).fileName();
                finish(m);
            }
        }
    }

    // Place disponible pour ce qui doit être copié vers un autre disque.
    if (plan.copyBytes > 0) {
        const QStorageInfo dst(existingAncestor(root));
        if (dst.isValid() && dst.bytesAvailable() < plan.copyBytes + 512LL * 1024 * 1024)
            plan.warnings << tr("Pas assez de place sur le disque de destination pour copier %1.").arg(Archive::formatSize(plan.copyBytes));
    }
    return plan;
}

Organizer::Plan Organizer::planImport(const QStringList& inputs, bool keepSource) const {
    Plan plan;
    plan.options.root = defaultRoot();
    plan.options.emulators = false;
    plan.options.saves = false;
    plan.options.import = true;
    plan.options.keepSource = keepSource;
    const QString root = norm(plan.options.root);
    const QStringList exts = Registry::instance().allExtensions();
    QStringList files;
    QSet<QString> taken;
    // Jeux en dossier (dump Switch exefs/romfs, jeu Wii U code/content/meta) : déplacés en entier, nommés d'après le jeu.
    QStringList units;
    auto addUnit = [&](const QString& unit, const DetectedRom& r) {
        const QString u = norm(unit);
        for (const QString& known : units)
            if (samePath(known, u) || isInside(u, known))
                return;
        units << u;
        const QString gp = norm(r.path);
        if (isInside(gp, root) || samePath(u, root) || isInside(u, root)) {
            ++plan.alreadyInPlace;
            plan.importedPaths << QDir::toNativeSeparators(gp);
            return;
        }
        Move m;
        m.kind = !r.isAddon ? Game : (r.addonKind == QLatin1String("update") ? Update : Dlc);
        m.group = consoleFolder(r.systemId);
        m.from = u;
        m.isDir = true;
        m.copy = keepSource;
        m.gameRel = gp.mid(u.size() + 1);
        m.label = r.title;
        m.size = Archive::treeSize(u);
        const QString name = titleFolder(r.title);
        m.to = uniqueTarget(root + QLatin1Char('/') + kindFolder(m.kind) + QLatin1Char('/') + m.group + QLatin1Char('/') +
                                (name.isEmpty() ? QFileInfo(u).fileName() : name), taken);
        m.crossDevice = keepSource || !sameDevice(m.from, root);
        plan.totalBytes += m.size;
        if (m.crossDevice)
            plan.copyBytes += m.size;
        plan.moves << m;
    };
    auto findUnits = [&](const QString& top) {
        QStringList queue{norm(top)};
        for (int depth = 0; depth < 5 && !queue.isEmpty(); ++depth) {
            QStringList next;
            for (const QString& d : queue) {
                const auto r = RomDetector::detectDirectory(d);
                if (r && r->systemId != QLatin1String("pc")) {
                    const QString gp = norm(r->path);
                    addUnit(gp.endsWith(QLatin1String("/exefs/main")) ? exefsUnit(d) : d, *r);
                    continue;
                }
                for (const QFileInfo& sub : QDir(d).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                    next << norm(sub.absoluteFilePath());
            }
            queue = next.mid(0, 2000);
        }
    };
    for (const QString& in : inputs) {
        if (QFileInfo(in).isDir()) { // dossier glissé : on prend les jeux qu'il contient
            findUnits(in);
            QDirIterator it(in, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext() && files.size() < 5000) {
                const QString f = norm(it.next());
                bool inUnit = false;
                for (const QString& u : units)
                    inUnit |= isInside(f, u);
                if (!inUnit && exts.contains(QFileInfo(f).suffix().toLower()))
                    files << f;
            }
        } else {
            files << in;
        }
    }
    for (const QString& f : files) {
        const auto r = RomDetector::detectFile(f);
        if (!r) {
            ++plan.unknown;
            continue;
        }
        if (isInside(f, root)) { // déjà dans « Jeux Nintendo »
            ++plan.alreadyInPlace;
            plan.importedPaths << QDir::toNativeSeparators(f);
            continue;
        }
        Move m;
        m.kind = !r->isAddon ? Game : (r->addonKind == QLatin1String("update") ? Update : Dlc);
        m.group = consoleFolder(r->systemId);
        m.from = norm(f);
        m.copy = keepSource;
        m.label = r->isAddon && !r->addonName.isEmpty() ? r->addonName + QStringLiteral(" (") + r->title + QLatin1Char(')') : r->title;
        m.size = QFileInfo(f).size();
        m.to = uniqueTarget(root + QLatin1Char('/') + kindFolder(m.kind) + QLatin1Char('/') + m.group + QLatin1Char('/') + QFileInfo(f).fileName(), taken);
        const QString stem = QFileInfo(f).absolutePath() + QLatin1Char('/') + QFileInfo(f).completeBaseName();
        for (const char* ext : {".sav", ".dsv", ".srm", ".eep", ".sra", ".fla", ".mpk"})
            if (QFileInfo::exists(stem + QLatin1String(ext))) {
                m.companions << norm(stem + QLatin1String(ext));
                m.size += QFileInfo(stem + QLatin1String(ext)).size();
            }
        m.crossDevice = keepSource || !sameDevice(m.from, root);
        plan.totalBytes += m.size;
        if (m.crossDevice)
            plan.copyBytes += m.size;
        plan.moves << m;
    }
    if (plan.copyBytes > 0) {
        const QStorageInfo dst(existingAncestor(root));
        if (dst.isValid() && dst.bytesAvailable() < plan.copyBytes + 512LL * 1024 * 1024)
            plan.warnings << tr("Pas assez de place sur le disque de destination pour copier %1.").arg(Archive::formatSize(plan.copyBytes));
    }
    return plan;
}

void Organizer::run(const Plan& plan) {
    if (m_running)
        return;
    m_running = true;
    m_cancel = false;
    const QString root = norm(plan.options.root);
    Settings::instance().setValue(QStringLiteral("organize/lang"), folderLang()); // les noms de dossiers ne changent plus
    Settings::instance().setValue(QStringLiteral("organize/root"), QDir::toNativeSeparators(root));

    // Réglages avant / après, pour pouvoir tout remettre comme avant.
    auto& s = Settings::instance();
    QJsonObject before;
    before[QStringLiteral("library/folders")] = QJsonArray::fromStringList(s.romFolders());
    before[QStringLiteral("emulators/extraFolders")] = QJsonArray::fromStringList(s.extraEmulatorFolders());
    before[QStringLiteral("saves/backupDir")] = s.value(QStringLiteral("saves/backupDir")).toString();

    struct Out {
        QVector<QPair<QString, QString>> moved;
        QVector<Move> movedDefs;
        QStringList removedDirs;
        QStringList errors;
        bool cancelled = false;
    };
    const QVector<Move> moves = plan.moves;
    const qint64 total = qMax<qint64>(1, plan.totalBytes);
    auto* w = new QFutureWatcher<Out>(this);
    connect(w, &QFutureWatcher<Out>::finished, this, [this, w, root, before, plan] {
        const Out out = w->result();
        w->deleteLater();
        // Réglages après rangement.
        auto& s = Settings::instance();
        QStringList folders;
        for (const QString& f : s.romFolders())
            if (QFileInfo(f).isDir() && !isInside(f, root))
                folders << f;
        bool gamesMoved = false;
        for (const Move& m : out.movedDefs)
            gamesMoved |= m.kind == Game || m.kind == Update || m.kind == Dlc;
        if (gamesMoved && !folders.contains(QDir::toNativeSeparators(root), pathCs()))
            folders << QDir::toNativeSeparators(root);
        QJsonObject after;
        after[QStringLiteral("library/folders")] = QJsonArray::fromStringList(folders);
        QStringList extra = s.extraEmulatorFolders();
        const QString emuRoot = QDir::toNativeSeparators(root + QLatin1Char('/') + kindFolder(Emulator));
        bool emuMoved = false;
        for (const Move& m : out.movedDefs)
            emuMoved |= m.kind == Emulator;
        if (emuMoved && !extra.contains(emuRoot, pathCs()))
            extra << emuRoot;
        after[QStringLiteral("emulators/extraFolders")] = QJsonArray::fromStringList(extra);
        bool savesMoved = plan.options.saves;
        after[QStringLiteral("saves/backupDir")] = savesMoved ? QDir::toNativeSeparators(root + QLatin1Char('/') + kindFolder(Save))
                                                             : s.value(QStringLiteral("saves/backupDir")).toString();
        applyToApp(out.moved, out.movedDefs, after, false);

        // Journal (permet d'annuler).
        Result r;
        if (!out.moved.isEmpty()) {
            QJsonArray arr;
            for (int i = 0; i < out.moved.size(); ++i) {
                QJsonObject o;
                o[QStringLiteral("from")] = QDir::toNativeSeparators(out.moved[i].first);
                o[QStringLiteral("to")] = QDir::toNativeSeparators(out.moved[i].second);
                const Move& def = out.movedDefs[i];
                o[QStringLiteral("kind")] = kindKey(def.kind);
                o[QStringLiteral("dir")] = def.isDir;
                if (def.copy)
                    o[QStringLiteral("copy")] = true;
                if (!def.gameRel.isEmpty())
                    o[QStringLiteral("gameRel")] = def.gameRel;
                if (!def.emulatorId.isEmpty())
                    o[QStringLiteral("emulator")] = def.emulatorId;
                arr.append(o);
            }
            QJsonObject j;
            j[QStringLiteral("version")] = 1;
            j[QStringLiteral("date")] = QDateTime::currentDateTime().toString(Qt::ISODate);
            j[QStringLiteral("root")] = QDir::toNativeSeparators(root);
            j[QStringLiteral("moves")] = arr;
            j[QStringLiteral("removedDirs")] = QJsonArray::fromStringList(out.removedDirs);
            j[QStringLiteral("settingsBefore")] = before;
            j[QStringLiteral("settingsAfter")] = after;
            QDir().mkpath(journalDir());
            r.journal = journalDir() + QStringLiteral("/rangement-%1.json").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
            QSaveFile f(r.journal);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(QJsonDocument(j).toJson());
                f.commit();
            }
        }
        r.cancelled = out.cancelled;
        r.ok = out.errors.isEmpty() && !out.cancelled;
        int files = 0;
        for (const Move& m : out.movedDefs)
            files += m.kind == Save ? 0 : 1;
        r.moved = files;
        r.skipped = out.errors.size();
        if (out.cancelled)
            r.message = tr("Rangement arrêté : %n élément(s) déjà déplacé(s), le reste n'a pas bougé.", nullptr, files);
        else if (!out.errors.isEmpty())
            r.message = tr("%n élément(s) rangé(s). Certains fichiers sont restés en place :", nullptr, files) +
                        QStringLiteral("\n• ") + out.errors.mid(0, 6).join(QStringLiteral("\n• "));
        else if (plan.options.import)
            r.message = tr("%n jeu(x) ajouté(s) à ta bibliothèque.", nullptr, files);
        else
            r.message = tr("C'est rangé : %n élément(s) déplacé(s).", nullptr, files);
        for (int i = 0; i < out.moved.size(); ++i) {
            const Move& def = out.movedDefs[i];
            if (def.kind != Save && def.kind != Emulator)
                r.newPaths << QDir::toNativeSeparators(out.moved[i].second + (def.gameRel.isEmpty() ? QString() : QLatin1Char('/') + def.gameRel));
        }
        if (!out.removedDirs.isEmpty()) {
            QStringList names;
            for (const QString& d : out.removedDirs)
                names << QStringLiteral("« ") + QFileInfo(d).fileName() + QStringLiteral(" »");
            r.message += QStringLiteral("\n\n") +
                         tr("Les anciens dossiers, désormais vides, ont été supprimés : %1. Si tu les avais épinglés dans l'Explorateur "
                            "Windows, retire-les et épingle « %2 » à la place.")
                             .arg(names.mid(0, 8).join(QStringLiteral(", ")), QFileInfo(root).fileName());
        }
        qInfo("Rangement : %d déplacé(s), %d erreur(s), journal %s", int(out.moved.size()), int(out.errors.size()), qUtf8Printable(r.journal));
        m_running = false;
        emit finished(r);
    });
    const bool importing = plan.options.import;
    w->setFuture(QtConcurrent::run([this, moves, total, root, importing]() -> Out {
        Out out;
        Runner run;
        run.cancel = &m_cancel;
        run.report = [this, total](const QString& name, qint64 done) { emit progress(name, done, total); };
        QStringList parents;
        for (const Move& m : moves) {
            if (m_cancel) {
                out.cancelled = true;
                break;
            }
            run.error.clear();
            const bool done = m.copy ? run.copy(m.from, m.to, m.isDir)
                                     : run.move(m.from, m.to, m.isDir, m.isDir ? m.size : QFileInfo(m.from).size());
            if (!done) {
                if (m_cancel) {
                    out.cancelled = true;
                    break;
                }
                out.errors << (run.error.isEmpty() ? m.label : run.error);
                continue;
            }
            if (!run.error.isEmpty())
                out.errors << run.error;
            out.moved.append({m.from, m.to});
            out.movedDefs << m;
            parents << QFileInfo(m.from).absolutePath();
            for (const QString& c : m.companions) { // .sav à côté de la ROM
                const QString dst = QFileInfo(m.to).absolutePath() + QLatin1Char('/') + QFileInfo(c).fileName();
                if (QFileInfo::exists(dst) || !(m.copy ? run.copy(c, dst, false) : run.move(c, dst, false, QFileInfo(c).size())))
                    continue;
                Move cm = m;
                cm.kind = Save;
                cm.from = c;
                cm.to = dst;
                cm.isDir = false;
                cm.emulatorId.clear();
                cm.companions.clear();
                out.moved.append({c, dst});
                out.movedDefs << cm;
            }
        }
        if (!importing) // un ajout ne supprime pas les dossiers d'où viennent les fichiers
            out.removedDirs = removeEmptyDirs(parents, root);
        return out;
    }));
}

void Organizer::applyToApp(const QVector<QPair<QString, QString>>& moved, const QVector<Move>& moves,
                           const QJsonObject& settings, bool undoing) {
    auto& s = Settings::instance();
    s.setRomFolders(settings.value(QStringLiteral("library/folders")).toVariant().toStringList());
    s.setExtraEmulatorFolders(settings.value(QStringLiteral("emulators/extraFolders")).toVariant().toStringList());
    const QString backupDir = settings.value(QStringLiteral("saves/backupDir")).toString();
    if (backupDir.isEmpty())
        s.remove(QStringLiteral("saves/backupDir"));
    else
        s.setValue(QStringLiteral("saves/backupDir"), backupDir);
    Paths::setBackupsDir(backupDir);
    s.sync();

    QVector<QPair<QString, QString>> games;
    for (int i = 0; i < moved.size(); ++i) {
        const Move& m = moves[i];
        if (m.kind == Game || m.kind == Update || m.kind == Dlc) {
            const QString rel = m.gameRel.isEmpty() ? QString() : QLatin1Char('/') + m.gameRel;
            games.append({QDir::toNativeSeparators(moved[i].first + rel), QDir::toNativeSeparators(moved[i].second + rel)});
        } else if (m.kind == Emulator) {
            for (const QString& id : m.emulatorId.split(QLatin1Char(','), Qt::SkipEmptyParts))
                EmulatorManager::instance().relocate(id, moved[i].first, moved[i].second);
        }
    }
    if (m_lib && !games.isEmpty())
        m_lib->relocateMany(games);
    // Jeux PC ajoutés à la main : leur chemin suit aussi.
    QStringList pcGames = s.value(QStringLiteral("pc/games")).toStringList();
    bool pcChanged = false;
    for (const auto& [from, to] : games)
        for (QString& p : pcGames)
            if (samePath(p, from)) {
                p = to;
                pcChanged = true;
            }
    if (pcChanged)
        s.setValue(QStringLiteral("pc/games"), pcGames);
    Q_UNUSED(undoing)
}

QString Organizer::lastJournal() {
    QDir d(journalDir());
    const QStringList files = d.entryList({QStringLiteral("rangement-*.json")}, QDir::Files, QDir::Name);
    for (int i = files.size() - 1; i >= 0; --i)
        if (!files[i].contains(QLatin1String(".annule")))
            return d.filePath(files[i]);
    return {};
}

QString Organizer::describeJournal(const QString& journal) {
    QFile f(journal);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject j = QJsonDocument::fromJson(f.readAll()).object();
    const QDateTime when = QDateTime::fromString(j.value(QStringLiteral("date")).toString(), Qt::ISODate);
    int n = 0;
    for (const auto& v : j.value(QStringLiteral("moves")).toArray())
        n += v.toObject().value(QStringLiteral("kind")).toString() == QLatin1String("save") ? 0 : 1;
    return tr("Rangement du %1 : %n élément(s) vers %2", nullptr, n)
        .arg(QLocale().toString(when, QLocale::ShortFormat), j.value(QStringLiteral("root")).toString());
}

void Organizer::undo(const QString& journal) {
    if (m_running)
        return;
    QFile f(journal);
    if (!f.open(QIODevice::ReadOnly)) {
        emit finished({false, false, 0, 0, tr("Journal de rangement introuvable."), {}});
        return;
    }
    const QJsonObject j = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    m_running = true;
    m_cancel = false;
    QVector<Move> moves; // dans l'ordre inverse, de la destination vers l'origine
    const QJsonArray arr = j.value(QStringLiteral("moves")).toArray();
    qint64 total = 0;
    for (int i = arr.size() - 1; i >= 0; --i) {
        const QJsonObject o = arr[i].toObject();
        Move m;
        const QString k = o.value(QStringLiteral("kind")).toString();
        m.kind = k == QLatin1String("update") ? Update : k == QLatin1String("dlc") ? Dlc : k == QLatin1String("emulator") ? Emulator
                 : k == QLatin1String("save") ? Save : Game;
        m.from = norm(o.value(QStringLiteral("to")).toString());
        m.to = norm(o.value(QStringLiteral("from")).toString());
        m.isDir = o.value(QStringLiteral("dir")).toBool();
        m.emulatorId = o.value(QStringLiteral("emulator")).toString();
        m.copy = o.value(QStringLiteral("copy")).toBool();
        m.gameRel = o.value(QStringLiteral("gameRel")).toString();
        m.label = QFileInfo(m.to).fileName();
        m.size = m.isDir ? Archive::treeSize(m.from) : QFileInfo(m.from).size();
        total += m.size;
        moves << m;
    }
    const QJsonObject before = j.value(QStringLiteral("settingsBefore")).toObject();
    const QString root = norm(j.value(QStringLiteral("root")).toString());
    struct Out {
        QVector<QPair<QString, QString>> moved;
        QVector<Move> defs;
        QStringList errors;
    };
    auto* w = new QFutureWatcher<Out>(this);
    connect(w, &QFutureWatcher<Out>::finished, this, [this, w, before, journal] {
        const Out out = w->result();
        w->deleteLater();
        applyToApp(out.moved, out.defs, before, true);
        QString done = journal;
        done.replace(QStringLiteral(".json"), QStringLiteral(".annule.json"));
        QFile::rename(journal, done);
        Result r;
        r.ok = out.errors.isEmpty();
        int n = 0;
        for (const Move& m : out.defs)
            n += m.kind == Save ? 0 : 1;
        r.moved = n;
        r.skipped = out.errors.size();
        r.message = out.errors.isEmpty() ? tr("Rangement annulé : %n élément(s) remis à leur place.", nullptr, n)
                                         : tr("Rangement annulé en partie. Restés en place :") + QStringLiteral("\n• ") +
                                               out.errors.mid(0, 6).join(QStringLiteral("\n• "));
        m_running = false;
        emit finished(r);
    });
    w->setFuture(QtConcurrent::run([this, moves, total, root]() -> Out {
        Out out;
        Runner run;
        run.cancel = &m_cancel;
        const qint64 t = qMax<qint64>(1, total);
        run.report = [this, t](const QString& name, qint64 done) { emit progress(name, done, t); };
        QStringList parents;
        for (const Move& m : moves) {
            if (!QFileInfo::exists(m.from)) {
                out.errors << tr("%1 (introuvable)").arg(QFileInfo(m.from).fileName());
                continue;
            }
            if (m.copy) { // c'était une copie : l'original est toujours là, on retire seulement la copie
                if (m.isDir ? Archive::removeTree(m.from) : QFile::remove(m.from)) {
                    out.moved.append({m.from, m.to});
                    out.defs << m;
                    parents << QFileInfo(m.from).absolutePath();
                } else {
                    out.errors << tr("%1 (utilisé par un autre programme)").arg(QFileInfo(m.from).fileName());
                }
                continue;
            }
            if (QFileInfo::exists(m.to)) {
                out.errors << tr("%1 (un fichier du même nom existe déjà à l'origine)").arg(QFileInfo(m.to).fileName());
                continue;
            }
            run.error.clear();
            if (!run.move(m.from, m.to, m.isDir, m.size)) {
                out.errors << (run.error.isEmpty() ? m.label : run.error);
                continue;
            }
            out.moved.append({m.from, m.to});
            out.defs << m;
            parents << QFileInfo(m.from).absolutePath();
        }
        // Les dossiers de rangement vidés disparaissent ; la racine aussi si elle est vide.
        QStringList dirs = parents;
        removeEmptyDirs(dirs, QString());
        QDir().rmdir(root + QLatin1Char('/') + Organizer::kindFolder(Save));
        QDir().rmdir(root);
        return out;
    }));
}
