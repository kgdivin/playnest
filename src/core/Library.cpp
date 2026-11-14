#include "Library.h"

#include "Archive.h"
#include "Paths.h"
#include "Registry.h"
#include "RomDetector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

#include <algorithm>

namespace {

Qt::CaseSensitivity pathCase() {
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString pathKey(const QString& p) {
#ifdef Q_OS_WIN
    return QDir::toNativeSeparators(p).toLower();
#else
    return QDir::toNativeSeparators(p);
#endif
}

bool isUnder(const QString& path, const QString& folder) {
    const QString f = QDir::toNativeSeparators(QDir::cleanPath(folder));
    const QString p = QDir::toNativeSeparators(path);
    return p.startsWith(f + QDir::separator(), pathCase()) || p.compare(f, pathCase()) == 0;
}

// Minuscules sans accents, pour une recherche tolérante.
QString foldText(const QString& s) {
    const QString d = s.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(d.size());
    for (const QChar c : d)
        if (c.category() != QChar::Mark_NonSpacing)
            out += c.toLower();
    return out;
}

QJsonObject toJson(const Game& g) {
    QJsonObject o;
    o["path"] = g.path;
    o["system"] = g.systemId;
    if (!g.systemOverride.isEmpty()) o["systemOverride"] = g.systemOverride;
    o["title"] = g.title;
    if (!g.customTitle.isEmpty()) o["customTitle"] = g.customTitle;
    if (!g.internalTitle.isEmpty()) o["internalTitle"] = g.internalTitle;
    if (!g.gameId.isEmpty()) o["gameId"] = g.gameId;
    if (!g.titleId.isEmpty()) o["titleId"] = g.titleId;
    if (!g.region.isEmpty()) o["region"] = g.region;
    if (!g.iconPath.isEmpty()) o["icon"] = Paths::toStored(g.iconPath);
    if (!g.coverPath.isEmpty()) o["cover"] = Paths::toStored(g.coverPath);
    if (g.modified) o["modified"] = double(g.modified);
    if (!g.emulatorOverride.isEmpty()) o["emulator"] = g.emulatorOverride;
    o["size"] = double(g.fileSize);
    if (g.added.isValid()) o["added"] = g.added.toString(Qt::ISODate);
    if (g.lastPlayed.isValid()) o["lastPlayed"] = g.lastPlayed.toString(Qt::ISODate);
    if (g.playSeconds) o["playSeconds"] = double(g.playSeconds);
    if (g.playCount) o["playCount"] = g.playCount;
    if (g.favorite) o["favorite"] = true;
    if (g.isAddon) o["addon"] = true;
    if (!g.addonKind.isEmpty()) o["addonKind"] = g.addonKind;
    if (!g.addonName.isEmpty()) o["addonName"] = g.addonName;
    if (!g.baseTitleId.isEmpty()) o["baseTitleId"] = g.baseTitleId;
    if (!g.contentInfo.isEmpty()) o["content"] = g.contentInfo;
    if (!g.bundledUpdate.isEmpty()) o["bundledUpdate"] = g.bundledUpdate;
    if (g.bundledDlc) o["bundledDlc"] = g.bundledDlc;
    if (g.coverTried) o["coverTried"] = true;
    if (g.missing) o["missing"] = true;
    return o;
}

Game fromJson(const QJsonObject& o) {
    Game g;
    g.path = o["path"].toString();
    g.systemId = o["system"].toString();
    g.systemOverride = o["systemOverride"].toString();
    g.title = o["title"].toString();
    g.customTitle = o["customTitle"].toString();
    g.internalTitle = o["internalTitle"].toString();
    g.gameId = o["gameId"].toString();
    g.titleId = o["titleId"].toString();
    g.region = o["region"].toString();
    g.iconPath = Paths::fromStored(o["icon"].toString());
    g.coverPath = Paths::fromStored(o["cover"].toString());
    g.modified = qint64(o["modified"].toDouble());
    g.emulatorOverride = o["emulator"].toString();
    g.fileSize = qint64(o["size"].toDouble());
    g.added = QDateTime::fromString(o["added"].toString(), Qt::ISODate);
    g.lastPlayed = QDateTime::fromString(o["lastPlayed"].toString(), Qt::ISODate);
    g.playSeconds = qint64(o["playSeconds"].toDouble());
    g.playCount = o["playCount"].toInt();
    g.favorite = o["favorite"].toBool();
    g.isAddon = o["addon"].toBool();
    g.addonKind = o["addonKind"].toString();
    g.addonName = o["addonName"].toString();
    g.baseTitleId = o["baseTitleId"].toString();
    g.contentInfo = o["content"].toString();
    g.bundledUpdate = o["bundledUpdate"].toString();
    g.bundledDlc = o["bundledDlc"].toInt();
    g.coverTried = o["coverTried"].toBool();
    g.missing = o["missing"].toBool();
    return g;
}

bool hasUserData(const Game& g) {
    return g.favorite || g.playSeconds > 0 || !g.customTitle.isEmpty() || !g.systemOverride.isEmpty() ||
           !g.emulatorOverride.isEmpty();
}

} // namespace

// ===========================================================================
// Library
// ===========================================================================
Library::Library(QObject* parent) : QObject(parent) {}

void Library::load() {
    m_games.clear();
    QFile f(Paths::libraryFile());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        for (const auto& v : doc.object().value(QStringLiteral("games")).toArray()) {
            Game g = fromJson(v.toObject());
            if (!g.path.isEmpty())
                m_games.push_back(g);
        }
    }
    rebuildIndex();
    emit reset();
}

void Library::save() {
    m_saveScheduled = false;
    QJsonArray arr;
    for (const auto& g : m_games)
        arr.append(toJson(g));
    QJsonObject root;
    root["version"] = 1;
    root["games"] = arr;
    QSaveFile f(Paths::libraryFile());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

void Library::scheduleSave() {
    if (m_saveScheduled)
        return;
    m_saveScheduled = true;
    QTimer::singleShot(1500, this, &Library::save);
}

const Game* Library::find(const QString& path) const {
    const int i = indexOf(path);
    return i >= 0 ? &m_games[i] : nullptr;
}

int Library::indexOf(const QString& path) const { return m_index.value(pathKey(path), -1); }

void Library::rebuildIndex() {
    m_linksValid = false;
    m_index.clear();
    for (int i = 0; i < m_games.size(); ++i)
        m_index.insert(pathKey(m_games[i].path), i);
}

Library::MergeResult Library::mergeScan(const QVector<DetectedRom>& found, const QStringList& scannedFolders) {
    MergeResult res;
    QHash<QString, const DetectedRom*> foundByKey;
    for (const auto& r : found)
        foundByKey.insert(pathKey(r.path), &r);

    QVector<Game> next;
    next.reserve(found.size() + m_games.size());
    QSet<QString> kept;

    for (auto& g : m_games) {
        const QString key = pathKey(g.path);
        auto it = foundByKey.constFind(key);
        if (it != foundByKey.constEnd()) {
            applyDetected(g, **it);
            g.missing = false;
            next.push_back(g);
            kept.insert(key);
            ++res.updated;
            continue;
        }
        bool inScanned = false;
        for (const auto& folder : scannedFolders)
            if (isUnder(g.path, folder))
                inScanned = true;
        if (inScanned && hasUserData(g)) {
            g.missing = true; // on garde les statistiques si le fichier revient
            next.push_back(g);
            kept.insert(key);
        } else {
            ++res.removed;
        }
    }

    const QDateTime now = QDateTime::currentDateTime();
    for (const auto& r : found) {
        const QString key = pathKey(r.path);
        if (kept.contains(key))
            continue;
        Game g;
        g.path = r.path;
        applyDetected(g, r);
        g.added = now;
        next.push_back(g);
        kept.insert(key);
        if (!r.isAddon)
            ++res.added;
    }

    m_games = std::move(next);
    rebuildIndex();
    emit reset();
    save();
    return res;
}

void Library::update(const QString& path, const std::function<void(Game&)>& fn) {
    const int i = indexOf(path);
    if (i < 0)
        return;
    fn(m_games[i]);
    m_linksValid = false;
    emit gameChanged(m_games[i].path);
    scheduleSave();
}

void Library::remove(const QString& path) {
    const int i = indexOf(path);
    if (i < 0)
        return;
    m_games.removeAt(i);
    rebuildIndex();
    emit reset();
    scheduleSave();
}

void Library::applyDetected(Game& g, const DetectedRom& r) {
    g.systemId = r.systemId;
    g.title = r.title;
    g.internalTitle = r.internalTitle;
    g.gameId = r.gameId;
    g.titleId = r.titleId;
    g.region = r.region;
    g.isAddon = r.isAddon;
    g.addonKind = r.addonKind;
    g.addonName = r.addonName;
    g.baseTitleId = r.baseTitleId;
    g.contentInfo = r.contentInfo;
    g.bundledUpdate = r.bundledUpdate;
    g.bundledDlc = r.bundledDlc;
    g.fileSize = r.fileSize;
    g.modified = r.modified;
    if (!r.iconFile.isEmpty())
        g.iconPath = r.iconFile;
}

DetectedRom Library::toDetected(const Game& g) {
    DetectedRom r;
    r.path = g.path;
    r.systemId = g.systemId;
    r.title = g.title;
    r.internalTitle = g.internalTitle;
    r.gameId = g.gameId;
    r.titleId = g.titleId;
    r.region = g.region;
    r.iconFile = g.iconPath;
    r.isAddon = g.isAddon;
    r.addonKind = g.addonKind;
    r.addonName = g.addonName;
    r.baseTitleId = g.baseTitleId;
    r.contentInfo = g.contentInfo;
    r.bundledUpdate = g.bundledUpdate;
    r.bundledDlc = g.bundledDlc;
    r.fileSize = g.fileSize;
    r.modified = g.modified;
    return r;
}

int Library::relocateMany(const QVector<QPair<QString, QString>>& moves) {
    int n = 0;
    for (const auto& [from, to] : moves) {
        const int i = indexOf(from);
        if (i < 0)
            continue;
        const int existing = indexOf(to);
        if (existing >= 0 && existing != i) { // entrée fantôme à la destination : on garde celle qui a l'historique
            m_games.removeAt(existing);
            rebuildIndex();
        }
        const int j = indexOf(from);
        m_games[j].path = QDir::toNativeSeparators(to);
        m_games[j].missing = false;
        rebuildIndex();
        ++n;
    }
    if (n) {
        emit reset();
        save();
    }
    return n;
}

namespace {
bool sameFamily(const QString& a, const QString& b) {
    auto fam = [](const QString& s) { return s.startsWith(QLatin1String("switch")) ? QStringLiteral("switch") : s; };
    return fam(a) == fam(b);
}
// Titre réduit à ses lettres et chiffres, pour rapprocher "Pokémon Legends Z-A" et "Pokemon Legends Z A".
QString matchKey(const QString& t) {
    QString k = foldText(t);
    k.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    return k;
}
} // namespace

// Rattachement des contenus additionnels, calculé une fois puis gardé jusqu'au prochain changement.
// Par title ID quand il est connu, sinon par le titre (préfixe le plus long, même famille de consoles).
void Library::ensureLinks() const {
    if (m_linksValid)
        return;
    m_linksValid = true;
    m_baseOf.clear();
    m_addonsOf.clear();
    QHash<QString, QVector<int>> byTid; // plusieurs jeux peuvent partager un title ID (version Switch et Switch 2 Edition)
    QVector<QPair<QString, int>> byKey;
    for (int i = 0; i < m_games.size(); ++i) {
        const Game& g = m_games[i];
        if (g.isAddon)
            continue;
        if (!g.titleId.isEmpty())
            byTid[g.titleId.toUpper()].append(i);
        const QString k = matchKey(g.displayTitle());
        if (k.size() >= 4)
            byKey.append({k, i});
    }
    std::sort(byKey.begin(), byKey.end(), [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
    for (int i = 0; i < m_games.size(); ++i) {
        const Game& a = m_games[i];
        if (!a.isAddon)
            continue;
        int base = -1;
        if (!a.baseTitleId.isEmpty()) {
            // Même console d'abord, puis un fichier qui n'embarque pas déjà ses contenus (.nup).
            int bestScore = -1;
            for (int t : byTid.value(a.baseTitleId.toUpper())) {
                const Game& b = m_games[t];
                if (!sameFamily(a.system(), b.system()))
                    continue;
                const int score = (b.system() == a.system() ? 2 : 0) + (b.path.endsWith(QLatin1String(".nup"), Qt::CaseInsensitive) ? 0 : 1);
                if (score > bestScore) {
                    bestScore = score;
                    base = t;
                }
            }
        }
        if (base < 0) {
            const QString ka = matchKey(a.title);
            for (const auto& [k, idx] : byKey) {
                const Game& b = m_games[idx];
                // Un jeu dont le title ID est connu et différent n'est pas le bon, même si le titre colle.
                if (!a.baseTitleId.isEmpty() && !b.titleId.isEmpty())
                    continue;
                if (ka.startsWith(k) && sameFamily(a.system(), b.system())) {
                    base = idx;
                    break;
                }
            }
        }
        if (base >= 0) {
            m_baseOf.insert(i, base);
            m_addonsOf[base].append(i);
        }
    }
    for (auto it = m_addonsOf.begin(); it != m_addonsOf.end(); ++it) {
        std::sort(it->begin(), it->end(), [this](int x, int y) {
            const Game& a = m_games[x];
            const Game& b = m_games[y];
            if (a.addonKind != b.addonKind)
                return a.addonKind == QLatin1String("update"); // mises à jour d'abord
            if (a.addonKind == QLatin1String("update"))
                return Archive::compareVersions(a.addonName, b.addonName) > 0; // la plus récente en tête
            return QString::localeAwareCompare(a.addonName, b.addonName) < 0;
        });
    }
}

QVector<const Game*> Library::addonsFor(const Game& base) const {
    QVector<const Game*> out;
    const int i = indexOf(base.path);
    if (i < 0 || base.isAddon)
        return out;
    ensureLinks();
    for (int idx : m_addonsOf.value(i))
        out << &m_games[idx];
    return out;
}

const Game* Library::baseFor(const Game& addon) const {
    const int i = indexOf(addon.path);
    if (i < 0 || !addon.isAddon)
        return nullptr;
    ensureLinks();
    const int b = m_baseOf.value(i, -1);
    return b >= 0 ? &m_games[b] : nullptr;
}

Library::AddonStats Library::addonStats(const Game& base) const {
    AddonStats st;
    for (const Game* a : addonsFor(base)) {
        if (a->addonKind == QLatin1String("update")) {
            if (st.updates++ == 0)
                st.latestUpdate = a->addonName;
        } else {
            ++st.dlcs;
        }
    }
    // Paquet .nup : mise à jour et DLC intégrés au fichier.
    if (!base.bundledUpdate.isEmpty() && st.updates == 0) {
        st.updates = 1;
        st.latestUpdate = base.bundledUpdate;
    }
    st.dlcs += base.bundledDlc;
    return st;
}

QString Library::addonLabel(const Game& addon, const Game* base) {
    const bool update = addon.addonKind == QLatin1String("update");
    QString label = addon.addonName;
    if ((label.isEmpty() || label == QLatin1String("DLC")) && !update && base) {
        // « SAEKO Giantess Dating Sim Soundtrack » → « Soundtrack »
        const QStringList bw = base->displayTitle().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const QStringList aw = addon.title.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (aw.size() > bw.size() && matchKey(aw.mid(0, bw.size()).join(QString())) == matchKey(bw.join(QString())))
            label = aw.mid(bw.size()).join(QLatin1Char(' '));
    }
    if (label.isEmpty())
        label = update ? QObject::tr("Mise à jour") : QObject::tr("DLC");
    return label;
}

int Library::addonCount() const {
    int n = 0;
    for (const auto& g : m_games)
        n += g.isAddon ? 1 : 0;
    return n;
}

int Library::countForSystem(const QString& systemId, bool includeAddons) const {
    int n = 0;
    for (const auto& g : m_games) {
        if (!includeAddons && g.isAddon)
            continue;
        if (systemId == QLatin1String("__all") || g.system() == systemId ||
            (systemId == QLatin1String("__fav") && g.favorite) ||
            (systemId == QLatin1String("__recent") && g.lastPlayed.isValid()))
            ++n;
    }
    return n;
}

// ===========================================================================
// LibraryModel
// ===========================================================================
LibraryModel::LibraryModel(Library* lib, QObject* parent) : QAbstractListModel(parent), m_lib(lib) {
    connect(lib, &Library::reset, this, [this] {
        beginResetModel();
        endResetModel();
    });
    connect(lib, &Library::gameChanged, this, [this](const QString& path) {
        const int row = m_lib->indexOf(path);
        if (row >= 0)
            emit dataChanged(index(row), index(row));
    });
}

int LibraryModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_lib->count(); }

const Game* LibraryModel::gameAt(int row) const {
    if (row < 0 || row >= m_lib->count())
        return nullptr;
    return &m_lib->games()[row];
}

QVariant LibraryModel::data(const QModelIndex& index, int role) const {
    const Game* g = gameAt(index.row());
    if (!g)
        return {};
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole: return g->displayTitle();
    case Qt::ToolTipRole: return QDir::toNativeSeparators(g->path);
    case PathRole: return g->path;
    case SystemRole: return g->system();
    case ImageRole: {
        if (g->isAddon && g->bestImage().isEmpty()) // un DLC prend l'image de son jeu
            if (const Game* base = m_lib->baseFor(*g))
                return base->bestImage();
        return g->bestImage();
    }
    case FavoriteRole: return g->favorite;
    case LastPlayedRole: return g->lastPlayed;
    case PlaySecondsRole: return g->playSeconds;
    case AddedRole: return g->added;
    case AddonRole: return g->isAddon;
    case AddonKindRole: return g->addonKind;
    case SubtitleRole: {
        if (g->isAddon) {
            const Game* base = m_lib->baseFor(*g);
            return base ? base->displayTitle() : QObject::tr("Jeu de base absent");
        }
        return g->contentInfo;
    }
    case AddonLabelRole: return g->isAddon ? Library::addonLabel(*g, m_lib->baseFor(*g)) : QString();
    case BaseTitleRole: {
        const Game* base = g->isAddon ? m_lib->baseFor(*g) : nullptr;
        return base ? base->displayTitle() : QString();
    }
    case ChipsRole: {
        if (g->isAddon)
            return QStringList();
        const Library::AddonStats st = m_lib->addonStats(*g);
        QStringList chips;
        if (st.updates > 0) {
            static const QRegularExpression ver(QStringLiteral("(\\d+(?:\\.\\d+)+|n°\\d+|v\\d+)"));
            const auto m = ver.match(st.latestUpdate);
            chips << QStringLiteral("u:") + (m.hasMatch() ? m.captured(1) : QString());
        }
        if (st.dlcs > 0)
            chips << QStringLiteral("d:%1").arg(st.dlcs);
        return chips;
    }
    case MissingRole: return g->missing;
    case SearchTextRole: {
        const SystemInfo* s = Registry::instance().system(g->system());
        return foldText(g->displayTitle() + QLatin1Char(' ') + g->internalTitle + QLatin1Char(' ') + g->gameId +
                        QLatin1Char(' ') + g->titleId + QLatin1Char(' ') + (s ? s->name + QLatin1Char(' ') + s->shortName : QString()) +
                        QLatin1Char(' ') + QFileInfo(g->path).fileName());
    }
    default: return {};
    }
}

// ===========================================================================
// LibraryFilter
// ===========================================================================
LibraryFilter::LibraryFilter(QObject* parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    sort(0);
}

void LibraryFilter::setCategory(const QString& cat) {
    m_category = cat;
    invalidate();
}

void LibraryFilter::setSearch(const QString& text) {
    m_terms = foldText(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    invalidateFilter();
}

void LibraryFilter::setSortKey(const QString& key) {
    m_sortKey = key;
    invalidate();
}

void LibraryFilter::setHideAddons(bool hide) {
    m_hideAddons = hide;
    invalidateFilter();
}

bool LibraryFilter::filterAcceptsRow(int row, const QModelIndex& parent) const {
    const QModelIndex idx = sourceModel()->index(row, 0, parent);
    const bool addon = idx.data(LibraryModel::AddonRole).toBool();
    if (m_category == QLatin1String("__addons")) {
        if (!addon)
            return false;
    } else if (m_hideAddons && addon) {
        return false;
    }
    if (m_category == QLatin1String("__fav")) {
        if (!idx.data(LibraryModel::FavoriteRole).toBool())
            return false;
    } else if (m_category == QLatin1String("__recent")) {
        if (!idx.data(LibraryModel::LastPlayedRole).toDateTime().isValid())
            return false;
    } else if (m_category != QLatin1String("__all") && m_category != QLatin1String("__addons")) {
        if (idx.data(LibraryModel::SystemRole).toString() != m_category)
            return false;
    }
    if (!m_terms.isEmpty()) {
        const QString hay = idx.data(LibraryModel::SearchTextRole).toString();
        for (const auto& t : m_terms)
            if (!hay.contains(t))
                return false;
    }
    return true;
}

bool LibraryFilter::lessThan(const QModelIndex& a, const QModelIndex& b) const {
    const QString key = m_category == QLatin1String("__recent") ? QStringLiteral("recent") : m_sortKey;
    auto titleLess = [&] {
        return QString::localeAwareCompare(a.data(LibraryModel::TitleRole).toString(),
                                           b.data(LibraryModel::TitleRole).toString()) < 0;
    };
    if (key == QLatin1String("recent")) {
        const QDateTime da = a.data(LibraryModel::LastPlayedRole).toDateTime();
        const QDateTime db = b.data(LibraryModel::LastPlayedRole).toDateTime();
        if (da != db)
            return da > db; // le plus récent d'abord
    } else if (key == QLatin1String("playtime")) {
        const qint64 pa = a.data(LibraryModel::PlaySecondsRole).toLongLong();
        const qint64 pb = b.data(LibraryModel::PlaySecondsRole).toLongLong();
        if (pa != pb)
            return pa > pb;
    } else if (key == QLatin1String("added")) {
        const QDateTime da = a.data(LibraryModel::AddedRole).toDateTime();
        const QDateTime db = b.data(LibraryModel::AddedRole).toDateTime();
        if (da != db)
            return da > db;
    } else if (key == QLatin1String("system")) {
        const QString sa = a.data(LibraryModel::SystemRole).toString();
        const QString sb = b.data(LibraryModel::SystemRole).toString();
        if (sa != sb) {
            const auto& systems = Registry::instance().systems();
            int ia = 0, ib = 0;
            for (int i = 0; i < systems.size(); ++i) {
                if (systems[i].id == sa) ia = i;
                if (systems[i].id == sb) ib = i;
            }
            return ia < ib;
        }
    }
    return titleLess();
}
