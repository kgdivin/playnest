#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSortFilterProxyModel>
#include <QString>
#include <QVector>

#include <functional>

struct DetectedRom;

// Un jeu de la bibliothèque.
struct Game {
    QString path;           // clé unique
    QString systemId;       // système détecté
    QString systemOverride; // système choisi par l'utilisateur (prioritaire)
    QString title;          // titre détecté
    QString customTitle;    // titre choisi par l'utilisateur (prioritaire)
    QString internalTitle;
    QString gameId;
    QString titleId;
    QString region;
    QString iconPath;       // icône extraite de la ROM
    QString coverPath;      // jaquette (téléchargée ou choisie)
    QString emulatorOverride;
    qint64 fileSize = 0;
    qint64 modified = 0;    // date de modification du fichier (scan incrémental)
    QDateTime added;
    QDateTime lastPlayed;
    qint64 playSeconds = 0;
    int playCount = 0;
    bool favorite = false;
    bool isAddon = false;
    QString addonKind;      // "update" ou "dlc"
    QString addonName;      // "Mise à jour 4.0.0", nom du DLC…
    QString baseTitleId;    // jeu de base (Switch) auquel se rattache ce contenu
    QString contentInfo;    // contenu intégré (paquets .nup)
    QString bundledUpdate;  // version de la mise à jour intégrée (.nup)
    int bundledDlc = 0;     // DLC intégrés (.nup)
    bool coverTried = false; // téléchargement déjà tenté
    bool missing = false;    // fichier introuvable au dernier scan

    QString system() const { return systemOverride.isEmpty() ? systemId : systemOverride; }
    QString displayTitle() const { return customTitle.isEmpty() ? title : customTitle; }
    QString bestImage() const { return coverPath.isEmpty() ? iconPath : coverPath; }
};

// Stockage de la bibliothèque (library.json).
class Library : public QObject {
    Q_OBJECT
public:
    explicit Library(QObject* parent = nullptr);

    void load();
    void save();
    void scheduleSave();

    const QVector<Game>& games() const { return m_games; }
    int count() const { return m_games.size(); }
    const Game* find(const QString& path) const;
    int indexOf(const QString& path) const;

    // Fusionne le résultat d'un scan en conservant les données utilisateur.
    // Les jeux absents des dossiers scannés sont marqués "missing" ou retirés.
    struct MergeResult { int added = 0; int updated = 0; int removed = 0; };
    MergeResult mergeScan(const QVector<DetectedRom>& found, const QStringList& scannedFolders);

    // Modifications ponctuelles (émettent gameChanged).
    void update(const QString& path, const std::function<void(Game&)>& fn);
    void remove(const QString& path);

    int countForSystem(const QString& systemId, bool includeAddons) const;
    int addonCount() const;

    // Contenus additionnels (mises à jour, DLC) rattachés à un jeu, et l'inverse.
    QVector<const Game*> addonsFor(const Game& base) const;
    const Game* baseFor(const Game& addon) const;
    static QString addonLabel(const Game& addon, const Game* base); // « Mise à jour 4.0.0 », « The Crown Tundra »
    struct AddonStats { int updates = 0; int dlcs = 0; QString latestUpdate; };
    AddonStats addonStats(const Game& base) const;

    // Conversion entre résultat de détection et entrée de bibliothèque.
    static void applyDetected(Game& g, const DetectedRom& r);
    static DetectedRom toDetected(const Game& g);

    // Des fichiers ont été déplacés (rangement) : met à jour leurs chemins en gardant leurs données.
    int relocateMany(const QVector<QPair<QString, QString>>& moves);

signals:
    void reset();                       // la liste entière a changé
    void gameChanged(const QString& path);

private:
    void rebuildIndex();
    void ensureLinks() const;
    QVector<Game> m_games;
    mutable bool m_linksValid = false;
    mutable QHash<int, int> m_baseOf;           // contenu → jeu de base (indices)
    mutable QHash<int, QVector<int>> m_addonsOf; // jeu de base → contenus
    QHash<QString, int> m_index;
    bool m_saveScheduled = false;
};

// Modèle Qt exposant la bibliothèque aux vues.
class LibraryModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        TitleRole,
        SystemRole,
        ImageRole,
        FavoriteRole,
        LastPlayedRole,
        PlaySecondsRole,
        AddedRole,
        AddonRole,
        AddonKindRole,
        SubtitleRole,
        AddonLabelRole,   // « The Crown Tundra », « Mise à jour 4.0.0 »
        BaseTitleRole,    // titre du jeu de base d'un contenu
        ChipsRole,        // pastilles d'une carte : « MàJ 4.0.1 », « +2 DLC »
        MissingRole,
        SearchTextRole,
    };
    explicit LibraryModel(Library* lib, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    const Game* gameAt(int row) const;
    Library* library() const { return m_lib; }

private:
    Library* m_lib;
};

// Filtre (catégorie + recherche) et tri.
class LibraryFilter : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit LibraryFilter(QObject* parent = nullptr);
    void setCategory(const QString& cat); // id système, "__all", "__fav", "__recent", "__addons"
    QString category() const { return m_category; }
    void setSearch(const QString& text);
    void setSortKey(const QString& key);  // title, recent, playtime, added, system
    void setHideAddons(bool hide);

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
    bool lessThan(const QModelIndex& a, const QModelIndex& b) const override;

private:
    QString m_category = QStringLiteral("__all");
    QStringList m_terms;
    QString m_sortKey = QStringLiteral("title");
    bool m_hideAddons = true;
};
