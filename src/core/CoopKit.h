#pragma once

#include <QByteArray>
#include <QString>

struct Game;

// « OoT Co-op » (github.com/daurosk/oot-remake-multiplayer) : un projet de fans indépendant qui ajoute le jeu
// en ligne à plusieurs au remake d'Ocarina of Time. Playnest ne contient rien de ce projet : il télécharge son
// kit depuis sa page officielle, le décompresse dans le dossier choisi et lance son fichier de démarrage.
// Le kit se met ensuite à jour tout seul. Windows uniquement.
namespace CoopKit {

inline constexpr const char* kName = "OoT Co-op";
inline constexpr const char* kRepo = "daurosk/oot-remake-multiplayer";
inline constexpr const char* kWebsite = "https://github.com/daurosk/oot-remake-multiplayer";
inline constexpr const char* kTitleId = "0400486024DEC000";
inline constexpr const char* kStartFile = "Start OoT Co-op.bat";
inline constexpr int kNeededGb = 60;

struct Release {
    QString version; // « v0.10.1 »
    QString asset;   // « oot-coop-v0.10.1.zip »
    QString url;
};

bool available();                // ce système peut-il lancer le kit ?
bool appliesTo(const Game& g);   // le jeu de base auquel le kit s'applique
QString installDir();            // dossier du kit, vide s'il n'est pas installé
bool installed();
QString suggestedDir();          // dossier court sur le disque qui a le plus de place
// Avertissement à montrer avant d'installer dans ce dossier (vide = rien à signaler).
QString dirWarning(const QString& dir);

// Choisit le kit le plus récent dans la liste des versions de GitHub (les pré-versions comptent).
bool pickRelease(const QByteArray& releasesJson, Release* out);
// Décompresse le kit dans « dir ». Le dossier d'un kit déjà présent garde ses réglages et son jeu construit.
bool installZip(const QString& zip, const QString& dir, QString* error);
// Reprend un kit déjà décompressé ailleurs.
bool adopt(const QString& dir);
// Fichier ou dossier à donner au kit quand il demande le jeu.
QString gameFileFor(const Game& g);
bool launch(QString* error);

} // namespace CoopKit
