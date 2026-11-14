#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>

class Library;

// Range les fichiers dans des dossiers séparés :
//   <racine>/Jeux/<Console>, <racine>/Mises à jour/<Console>, <racine>/DLC/<Console>,
//   <racine>/Émulateurs/<Nom>, <racine>/Sauvegardes (copies faites par Playnest).
// Sur le même disque, chaque déplacement est un simple renommage (instantané) ;
// sinon le fichier est copié puis vérifié avant que l'original ne soit supprimé.
// Chaque rangement est consigné dans un journal et peut être annulé.
class Organizer : public QObject {
    Q_OBJECT
public:
    enum Kind { Game, Update, Dlc, Emulator, Save };
    struct Move {
        Kind kind = Game;
        QString from;       // chemin d'origine (fichier ou dossier)
        QString to;         // destination
        QString label;      // ce qui est affiché (titre du jeu, nom de l'émulateur)
        QString group;      // console ou nom d'émulateur
        QString emulatorId; // pour Kind::Emulator
        qint64 size = 0;
        bool isDir = false;
        bool crossDevice = false;
        QStringList companions; // fichiers qui suivent le jeu (.sav, .dsv, .srm à côté de la ROM)
        bool copy = false;      // l'original reste en place
        QString gameRel;        // chemin du jeu dans le dossier déplacé (exefs/main, code/x.rpx, Game.exe…)
        bool renameInPlace = false; // dossier de jeu renommé d'après son titre, même s'il est déjà dans la bonne console
    };
    struct Options {
        QString root;
        bool games = true;
        bool emulators = true;
        bool saves = true;
        bool import = false;     // ajout de nouveaux jeux (glissés sur la fenêtre)
        bool keepSource = false; // copier au lieu de déplacer
    };
    struct Plan {
        Options options;
        QVector<Move> moves;
        int alreadyInPlace = 0;
        int skippedMissing = 0;
        int unknown = 0;        // fichiers qui ne sont pas des jeux reconnus (ajout)
        QStringList importedPaths;
        qint64 totalBytes = 0;
        qint64 copyBytes = 0; // à copier (autre disque)
        QStringList warnings;
    };
    struct Result {
        bool ok = false;
        bool cancelled = false;
        int moved = 0;
        int skipped = 0;
        QString message;
        QString journal;
        QStringList newPaths; // emplacements finaux (ajout de jeux)
    };

    explicit Organizer(Library* lib, QObject* parent = nullptr);

    static QString defaultRoot();
    static QString kindFolder(Kind k);
    static QStringList kindFolderNames(Kind k); // en français et en anglais
    // Langue des noms de dossiers : fixée au premier rangement, pour qu'un changement de langue
    // de l'interface ne déplace pas tout de « Jeux » vers « Games ».
    static QString folderLang();
    static bool isOrganized(const QString& root);
    static QString consoleFolder(const QString& systemId);

    Plan plan(const Options& options) const;
    // Nouveaux jeux à ajouter : chacun va directement dans Jeux, Mises à jour ou DLC de sa console.
    Plan planImport(const QStringList& filesOrFolders, bool keepSource) const;
    void run(const Plan& plan);     // asynchrone : progress(), puis finished()
    void cancel() { m_cancel = true; }
    bool isRunning() const { return m_running; }

    // Dernier rangement, s'il peut être annulé.
    static QString lastJournal();
    static QString describeJournal(const QString& journal);
    void undo(const QString& journal); // asynchrone, comme run()

signals:
    void progress(const QString& current, qint64 done, qint64 total);
    void finished(const Organizer::Result& result);

private:
    void applyToApp(const QVector<QPair<QString, QString>>& moved, const QVector<Move>& moves,
                    const QJsonObject& settingsAfter, bool undoing);
    Library* m_lib;
    std::atomic_bool m_cancel{false};
    bool m_running = false;
};

Q_DECLARE_METATYPE(Organizer::Result)
