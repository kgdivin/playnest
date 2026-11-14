#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

class Library;

// Un emplacement de sauvegardes (dossier de données d'un émulateur, ou sauvegardes à côté des jeux).
struct SaveSource {
    QString key;          // "emu-eden", "roms"
    QString name;         // "Eden et Lichii"
    QString root;         // dossier de données (vide pour "roms")
    QStringList emuIds;
    QStringList paths;    // dossiers ou fichiers de sauvegarde existants
};

// Copies de sécurité des sauvegardes de jeux : création, liste, restauration, rétention.
class SaveManager : public QObject {
    Q_OBJECT
public:
    static SaveManager& instance();
    void setLibrary(Library* lib) { m_lib = lib; }

    QVector<SaveSource> sources() const;
    QVector<SaveSource> sourcesForEmulators(const QStringList& ids) const;

    struct BackupInfo {
        QString file;
        QDateTime created;
        QString reason;
        bool automatic = false;
        qint64 size = 0;
        QStringList keys;
        QStringList names;
    };
    const QVector<BackupInfo>& backups() const { return m_backups; }
    const QHash<QString, qint64>& sizes() const { return m_sizes; }
    bool busy() const { return m_busy; }

    void reload();                         // relit la liste des copies
    void computeSizes();                   // taille des sauvegardes actuelles (arrière-plan)
    void backupNow(const QStringList& keys, const QString& reason, bool automatic);
    void restore(const QString& file, const QStringList& keys);
    bool remove(const QString& file);
    void maybeWeeklyBackup();

    static QString newBackupPath(const QString& tag);
    // Exécutable dans un thread de travail.
    static bool doBackup(const QVector<SaveSource>& sources, const QString& file, const QString& reason, bool automatic,
                         QString* error, qint64* bytes);

signals:
    void changed();
    void progress(const QString& message);
    void finished(bool ok, const QString& message);

private:
    SaveManager() = default;
    void prune();
    Library* m_lib = nullptr;
    QVector<BackupInfo> m_backups;
    QHash<QString, qint64> m_sizes;
    bool m_busy = false;
};
