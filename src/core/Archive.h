#pragma once

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

// Outils d'archives et de fichiers, utilisables depuis un thread de travail.
namespace Archive {

// Extrait une archive (.zip intégré, .7z via 7-Zip, autres via tar) dans destDir.
bool extract(const QString& archive, const QString& destDir, QString* error);
bool extractZip(const QString& zip, const QString& destDir, QString* error);

// Crée un zip. entries : (nom dans l'archive, fichier sur disque). extra : (nom, contenu).
bool createZip(const QString& zipPath, const QVector<QPair<QString, QString>>& entries,
               const QVector<QPair<QString, QByteArray>>& extra, QString* error,
               const std::function<void(int done, int total)>& progress = {});

// Lit un fichier d'un zip (vide si absent).
QByteArray readZipEntry(const QString& zipPath, const QString& entry);
// Extrait les entrées commençant par prefix vers destRoot (prefix retiré). Renvoie le nombre de fichiers.
int extractZipPrefix(const QString& zipPath, const QString& prefix, const QString& destRoot, QString* error);
// Extrait une entrée précise vers un fichier.
bool extractZipEntry(const QString& zipPath, const QString& entry, const QString& destFile);

// Fichiers d'un dossier (chemins relatifs, séparateur '/').
QStringList listFiles(const QString& root);
qint64 treeSize(const QString& path);
bool copyFileReplace(const QString& src, const QString& dst, QString* error);
bool removeTree(const QString& path);

// Comparaison de versions : >0 si a est plus récent que b, 0 si égales, <0 sinon.
int compareVersions(const QString& a, const QString& b);
// Extrait une version lisible d'un nom (dossier ou fichier).
QString versionFromName(const QString& name);
// Cherche « préfixe + numéro » dans un fichier binaire (ex. "Lichii build-" → "build-150").
QString probeVersion(const QString& file, const QString& prefix);
// Noms des fichiers contenus dans une archive .zip (ou .7z si 7-Zip est disponible).
QStringList archiveEntries(const QString& archive);
// Taille lisible en français : « 4,3 Go ».
QString formatSize(qint64 bytes);

} // namespace Archive
