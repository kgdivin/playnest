#pragma once

#include <QImage>
#include <QString>

#include <optional>

// Résultat de l'analyse d'un fichier ou dossier de jeu.
struct DetectedRom {
    QString systemId;
    QString path;          // chemin passé à l'émulateur
    QString title;         // titre affiché
    QString internalTitle; // titre lu dans l'en-tête
    QString gameId;        // ID produit (GALE01, AMCE, AXCE, NSME, AMKE01…)
    QString titleId;       // ID 16 hex (Switch, 3DS, Wii U)
    QString region;        // US, EU, JP, FR…
    QImage icon;           // icône intégrée (DS, Wii U, homebrew Switch)
    QString iconFile;      // icône enregistrée sur disque par le scanner
    bool isAddon = false;  // mise à jour ou DLC (non lançable seul)
    QString addonKind;     // "update" ou "dlc"
    QString addonName;     // "Mise à jour 4.0.0", "The Crown Tundra"…
    QString baseTitleId;   // title ID du jeu de base (pour relier DLC et mises à jour)
    QString contentInfo;   // contenu inclus (paquets .nup : mises à jour et DLC intégrés)
    QString bundledUpdate; // .nup : version de la mise à jour intégrée
    int bundledDlc = 0;    // .nup : nombre de DLC intégrés
    qint64 fileSize = 0;
    qint64 modified = 0;
};

namespace RomDetector {

// Incrémenté quand la détection change : la bibliothèque est alors relue entièrement une fois.
inline constexpr int kVersion = 9;

// Analyse un fichier. Retourne std::nullopt si ce n'est pas un jeu reconnu.
std::optional<DetectedRom> detectFile(const QString& path);

// Analyse un dossier : jeu Wii U décrypté (code/content/meta) ou
// ExeFS Switch/Switch 2 extrait (exefs/main + main.npdm).
std::optional<DetectedRom> detectDirectory(const QString& dir);

// Jeu PC qui se lance sans émulateur (fan game RPG Maker, Pokémon Essentials, Pokémon SDK…).
// strict = reconnaissance automatique pendant le scan : seulement les moteurs reconnus avec certitude.
// Sinon (ajout à la main), n'importe quel .exe convient.
std::optional<DetectedRom> detectPcGame(const QString& exeOrDir, bool strict);

// Nettoie un nom de fichier : retire extension, balises [..] et (..),
// numérotation scène, soulignés. Renvoie la région détectée si demandé.
QString cleanTitle(const QString& fileName, QString* regionOut = nullptr);

// Région lisible à partir du dernier caractère d'un ID produit Nintendo.
QString regionFromIdChar(QChar c);

// Système Switch déduit d'un title ID (0100… = Switch, 0400… = Switch 2).
QString switchSystemForTitleId(const QString& titleId);

// Nom d'un jeu Switch 2 connu d'après son title ID (les dumps ExeFS n'ont pas de nom intégré).
QString knownTitle(const QString& titleId);

} // namespace RomDetector
