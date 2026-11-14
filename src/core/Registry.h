#pragma once

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

// Un système (console) pris en charge.
struct SystemInfo {
    QString id;
    QString name;
    QString shortName;
    int year = 0;
    QColor color;
    QStringList extensions;
    QString requirements;
};

// Candidat de dossier de données : retenu si `when` (ou `path` à défaut) existe.
struct DataDirRule {
    QString path;
    QString when;
};

// Fichier système attendu par un émulateur (clés, firmware…), fourni par l'utilisateur.
struct SystemFileDef {
    QString label;
    QString path;        // relatif au dossier de données
    bool isDir = false;  // dossier qui doit être non vide (firmware)
    bool required = false;
    bool importable = false;
    QString help;
};

// D'où télécharger l'émulateur.
struct SourceDef {
    QString type;        // github, gitlab, page
    QString repo;        // owner/repo (github, gitlab)
    QString host;        // hôte gitlab (ex. git.ryujinx.app)
    QString url;         // page web (type page)
    QStringList assets;  // expressions régulières, par ordre de préférence
    QString versionRegex;// type page : extrait la version depuis l'URL
    bool isValid() const { return !type.isEmpty() && !assets.isEmpty() && (!repo.isEmpty() || !url.isEmpty()); }
};

// Un émulateur externe piloté par Playnest.
struct EmulatorDef {
    QString id;
    QString name;
    QStringList systems;
    QStringList exeNames;      // pour la plateforme courante
    QStringList args;          // plein écran
    QStringList argsWindowed;  // fenêtré
    SourceDef source;
    QString website;
    QString notes;
    QVector<DataDirRule> dataDirs;
    QStringList saves;         // motifs relatifs au dossier de données
    QVector<SystemFileDef> systemFiles;
    QStringList needs;          // outils : vcredist, 7zip
    QStringList unsupported;    // extensions que cet émulateur ne sait pas ouvrir (ex. "nup")
    QHash<QString, QString> systemNames; // jeton {system} : nom de chaque console pour cet émulateur (ares : « Famicom »)
    QString versionProbe;       // texte suivi du numéro de version dans l'exécutable (ex. "Lichii build-")

    bool canAutoInstall() const { return source.isValid(); }
    bool canOpen(const QString& path) const;
};

// Registre des systèmes et émulateurs : ressources intégrées, puis fichiers
// systems.json / emulators.json du dossier de données (pour modder sans recompiler).
class Registry {
public:
    static Registry& instance();
    void load();

    const QVector<SystemInfo>& systems() const { return m_systems; }
    const SystemInfo* system(const QString& id) const;

    const QVector<EmulatorDef>& emulators() const { return m_emulators; }
    const EmulatorDef* emulator(const QString& id) const;
    QVector<const EmulatorDef*> emulatorsFor(const QString& systemId) const;
    QString builtinDefaultEmulator(const QString& systemId) const;

    QStringList systemsForExtension(const QString& ext) const;
    QStringList allExtensions() const;
    QStringList allExeNames() const; // en minuscules

private:
    Registry() = default;
    void loadSystems(const QByteArray& json, bool override);
    void loadEmulators(const QByteArray& json, bool override);

    QVector<SystemInfo> m_systems;
    QVector<EmulatorDef> m_emulators;
    QHash<QString, QString> m_defaults;
};
