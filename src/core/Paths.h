#pragma once

#include <QString>

// Emplacements des données de Playnest.
// Mode portable : avec un fichier "portable.txt" à côté de l'exécutable, toutes les
// données vont dans le sous-dossier "data" (s'il est accessible en écriture).
namespace Paths {

void init();
bool isPortable();

QString dataDir();
QString coversDir();
QString iconsDir();
QString emulatorsDir();   // émulateurs installés par Playnest
QString downloadsDir();   // téléchargements temporaires
QString stagingDir();     // extraction avant installation
QString rollbackDir();    // fichiers remplacés lors d'une mise à jour (retour arrière)
QString backupsDir();     // copies des sauvegardes de jeux (data/backups, ou dossier choisi)
void setBackupsDir(const QString& dir); // vide = dossier par défaut
QString toolsDir();       // outils (7-Zip…)
QString appDir();

QString settingsFile();
QString libraryFile();
QString logFile();
QString lockFile();

QString ensureDir(const QString& path);

// Chemins stockés relativement au dossier de données ("data:covers/…").
QString toStored(const QString& absolute);
QString fromStored(const QString& stored);

// Remplace %APPDATA%, %LOCALAPPDATA%, %DOCUMENTS%, %HOME% et {emudir}.
QString expand(const QString& templ, const QString& emuDir = QString());

} // namespace Paths
