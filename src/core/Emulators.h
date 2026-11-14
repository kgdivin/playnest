#pragma once

#include "Registry.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

struct Game;

// État d'un émulateur sur ce PC.
struct EmulatorStatus {
    QString exe;              // vide = introuvable
    QString dir;              // dossier de l'exécutable
    QString version;          // version installée (marqueur, version du fichier ou nom du dossier)
    bool managed = false;     // installé dans le dossier de Playnest
    QString dataDir;          // dossier de données (clés, sauvegardes…)
    QString latest;           // dernière version connue en ligne
    bool updateAvailable = false;
    bool canRollback = false;
    bool busy = false;

    bool installed() const { return !exe.isEmpty(); }
};

// Une release en ligne.
struct InstallJob;

struct ReleaseInfo {
    QString version;
    QString assetName;
    QString url;
    bool prerelease = false;
};

// Détection, état, installation, mise à jour et retour arrière des émulateurs.
class EmulatorManager : public QObject {
    Q_OBJECT
public:
    static EmulatorManager& instance();

    void init();                 // statut immédiat depuis le cache, puis détection en arrière-plan
    void startDetection();
    bool detecting() const { return m_detecting; }

    EmulatorStatus status(const QString& id) const { return m_status.value(id); }
    QString exePath(const QString& id) const { return m_status.value(id).exe; }
    bool isInstalled(const QString& id) const { return !exePath(id).isEmpty(); }
    QString dataDir(const QString& id) const { return m_status.value(id).dataDir; }
    int installedCount() const;
    int updatesAvailable() const;

    // Émulateur par défaut d'une console : choix de l'utilisateur, sinon celui prévu
    // s'il est installé, sinon le premier émulateur compatible installé.
    QString effectiveDefault(const QString& systemId) const;
    QString emulatorFor(const Game& g) const;

    // Fichiers système (clés, firmware).
    struct FileState {
        SystemFileDef def;
        QString path;
        bool present = false;
    };
    QVector<FileState> systemFiles(const QString& id) const;
    QStringList missingRequired(const QString& id) const;
    bool importSystemFile(const QString& id, int index, const QString& source, QString* error);

    void setExePath(const QString& id, const QString& path); // vide = détection automatique
    // Le dossier de l'émulateur a été déplacé (rangement) : met à jour son chemin et son retour arrière.
    void relocate(const QString& id, const QString& oldDir, const QString& newDir);
    bool isRunning(const QString& id) const;
    void setRunningExe(const QString& exe) { m_runningExe = exe; }

    // Mises à jour.
    void checkUpdates(bool force);
    bool checkingUpdates() const { return m_pendingChecks > 0; }
    QDateTime lastUpdateCheck() const;
    void install(const QString& id);
    // Mise à jour (ou installation) depuis une archive que l'utilisateur a déjà téléchargée.
    // Même protection qu'une mise à jour en ligne ; l'archive n'est jamais supprimée.
    void installFromFile(const QString& id, const QString& archive);
    // Fichier déposé par l'utilisateur : archive, application seule (.exe) ou installateur (.exe/.msi).
    struct FileKind {
        enum Kind { Unknown, Archive, App, Installer } kind = Unknown;
        QString id;
    };
    FileKind classifyFile(const QString& file) const;
    // Lance l'installateur officiel (après une copie des sauvegardes), attend sa fin, puis suit la nouvelle version.
    void runInstaller(const QString& id, const QString& file);
    // Émulateur contenu dans une archive (d'après ses exécutables), ou vide.
    QString identifyArchive(const QString& archive) const;
    bool canModify(const QString& id, QString* why = nullptr) const;       // installe ou met à jour
    bool rollback(const QString& id, QString* error);

signals:
    void changed();
    void detectionFinished(int installed);
    void updatesChecked(int available);
    void installProgress(const QString& id, const QString& stage, qint64 received, qint64 total);
    void installFinished(const QString& id, bool ok, const QString& message);

private:
    EmulatorManager() = default;
    void refreshStatus(const QString& id);
    void refreshAll();
    void fetchRelease(const EmulatorDef& e, std::function<void(bool, ReleaseInfo, QString)> done);
    void continueInstall(const QString& id, const ReleaseInfo& rel);
    void fail(const QString& id, const QString& msg);
    QString installedVersion(const QString& exe) const;
    QString probedVersion(const QString& id, const QString& exe) const;
    void startJob(const QString& id, const struct InstallJob& job);

    QHash<QString, EmulatorStatus> m_status;
    QHash<QString, QString> m_detected;    // id -> exe trouvé par la détection
    QHash<QString, ReleaseInfo> m_latest;  // id -> dernière release
    QSet<QString> m_busy;
    QString m_runningExe;
    bool m_detecting = false;
    int m_pendingChecks = 0;
    int m_updateFound = 0;
};
