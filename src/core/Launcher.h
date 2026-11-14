#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

struct EmulatorDef;
struct Game;

// Lance un jeu avec un émulateur externe et suit la session de jeu.
class Launcher : public QObject {
    Q_OBJECT
public:
    explicit Launcher(QObject* parent = nullptr);
    ~Launcher() override;

    // Arguments finaux (jetons remplacés) pour un jeu.
    static QStringList buildArguments(const EmulatorDef& emu, const Game& g, const QString& exePath);

    bool launch(const Game& g, const QString& emulatorId, QString* error);
    bool launchDirect(const Game& g, QString* error); // jeu PC : lancé sans émulateur
    bool isRunning() const;
    QString runningPath() const { return m_gamePath; }
    QString runningTitle() const { return m_title; }
    QString runningEmulator() const { return m_emuName; }
    qint64 elapsedSeconds() const;

signals:
    void started(const QString& gamePath);
    // crashed : sortie anormale très rapide (clés manquantes, mauvais format…)
    void finished(const QString& gamePath, int exitCode, qint64 seconds, bool crashed, const QString& outputTail);

private:
    void onFinished(int exitCode, QProcess::ExitStatus status);

    QProcess* m_proc = nullptr;
    QString m_gamePath;
    QString m_title;
    QString m_emuName;
    QElapsedTimer m_timer;
    QByteArray m_output;
};
