#pragma once

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class Library;
struct Game;

// Télécharge les jaquettes (GameTDB, libretro-thumbnails, dépôt boxart d'Eden)
// et les met en cache dans le dossier covers.
class CoverManager : public QObject {
    Q_OBJECT
public:
    explicit CoverManager(Library* lib, QObject* parent = nullptr);

    void request(const Game& g, bool force = false);
    void requestMissing();
    bool setCustomCover(const QString& gamePath, const QString& imageFile, QString* error);
    int pending() const { return m_queue.size() + m_active; }

    // URL candidates pour un jeu, dans l'ordre d'essai (exposé pour les tests).
    static QStringList candidateUrls(const Game& g);

signals:
    void coverFound(const QString& gamePath);
    void pendingChanged(int remaining);

private:
    struct Job {
        QString gamePath;
        QString system;
        QStringList urls;
        bool fuzzyDone = false;
    };
    void pump();
    void tryNext(Job job);
    void finishJob(const Job& job, const QString& coverFile);
    // Dernier recours : la liste des jaquettes disponibles, et le titre qui ressemble le plus.
    void fuzzyStage(Job job);
    void loadIndex(const QString& source);
    QString fuzzyUrl(const QString& source, const Game& g) const;

    QHash<QString, QStringList> m_index;         // source -> noms disponibles
    QHash<QString, QVector<Job>> m_waitingIndex; // jobs en attente du chargement d'une liste

public:
    // Rapprochement de titres (exposé pour les tests) : meilleur nom de la liste, ou vide.
    static QString bestMatch(const QString& title, const QString& region, const QStringList& names);

private:
    Library* m_lib;
    QQueue<Job> m_queue;
    QSet<QString> m_inFlight;
    int m_active = 0;
};
