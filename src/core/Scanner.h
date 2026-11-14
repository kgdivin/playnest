#pragma once

#include "RomDetector.h"

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>

// Parcourt les dossiers de jeux en arrière-plan et détecte les ROM.
class Scanner : public QObject {
    Q_OBJECT
public:
    explicit Scanner(QObject* parent = nullptr);
    ~Scanner() override;

    bool isRunning() const { return m_running; }
    // known : résultats précédents (clé = chemin en minuscules) réutilisés si le fichier n'a pas changé.
    void start(const QStringList& folders, const QHash<QString, DetectedRom>& known = {});
    static QString key(const QString& path);
    void cancel();

signals:
    void progress(int filesSeen, int gamesFound, const QString& currentPath);
    void finished(const QVector<DetectedRom>& games, const QStringList& folders, bool cancelled);

private:
    struct Result {
        QVector<DetectedRom> games;
        bool cancelled = false;
    };
    static Result run(QStringList folders, QHash<QString, DetectedRom> known, std::shared_ptr<std::atomic_bool> cancel,
                      Scanner* self);

    bool m_running = false;
    QStringList m_folders;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QFutureWatcher<Result>* m_watcher = nullptr;
};
