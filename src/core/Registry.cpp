#include "Registry.h"

#include "Lang.h"

#include <QFileInfo>

#include "Paths.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtGlobal>

namespace {

QByteArray readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

QStringList toStringList(const QJsonValue& v) {
    QStringList out;
    for (const auto& e : v.toArray())
        out << e.toString();
    return out;
}

QString platformKey() {
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("mac");
#else
    return QStringLiteral("linux");
#endif
}

} // namespace

bool EmulatorDef::canOpen(const QString& path) const {
    if (unsupported.isEmpty())
        return true;
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext.isEmpty() || !unsupported.contains(ext, Qt::CaseInsensitive);
}

Registry& Registry::instance() {
    static Registry r;
    return r;
}

void Registry::load() {
    m_systems.clear();
    m_emulators.clear();
    m_defaults.clear();
    loadSystems(readFile(QStringLiteral(":/data/systems.json")), false);
    loadEmulators(readFile(QStringLiteral(":/data/emulators.json")), false);

    const QByteArray userSystems = readFile(Paths::dataDir() + QStringLiteral("/systems.json"));
    if (!userSystems.isEmpty())
        loadSystems(userSystems, true);
    const QByteArray userEmus = readFile(Paths::dataDir() + QStringLiteral("/emulators.json"));
    if (!userEmus.isEmpty())
        loadEmulators(userEmus, true);
}

void Registry::loadSystems(const QByteArray& json, bool override) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning("systems.json invalide : %s", qUtf8Printable(err.errorString()));
        return;
    }
    for (const auto& v : doc.object().value(QStringLiteral("systems")).toArray()) {
        const QJsonObject o = v.toObject();
        SystemInfo s;
        s.id = o.value(QStringLiteral("id")).toString();
        if (s.id.isEmpty())
            continue;
        s.name = Lang::t(o.value(QStringLiteral("name")).toString(s.id));
        s.shortName = o.value(QStringLiteral("short")).toString(s.name);
        s.year = o.value(QStringLiteral("year")).toInt();
        s.color = QColor(o.value(QStringLiteral("color")).toString(QStringLiteral("#888888")));
        for (const QString& e : toStringList(o.value(QStringLiteral("extensions"))))
            s.extensions << e.toLower();
        s.requirements = Lang::t(o.value(QStringLiteral("requirements")).toString());

        bool replaced = false;
        if (override) {
            for (auto& existing : m_systems) {
                if (existing.id == s.id) {
                    existing = s;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced)
            m_systems.push_back(s);
    }
}

void Registry::loadEmulators(const QByteArray& json, bool override) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning("emulators.json invalide : %s", qUtf8Printable(err.errorString()));
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonObject defaults = root.value(QStringLiteral("defaults")).toObject();
    for (auto it = defaults.begin(); it != defaults.end(); ++it)
        m_defaults.insert(it.key(), it.value().toString());

    const QString plat = platformKey();
    for (const auto& v : root.value(QStringLiteral("emulators")).toArray()) {
        const QJsonObject o = v.toObject();
        EmulatorDef e;
        e.id = o.value(QStringLiteral("id")).toString();
        if (e.id.isEmpty())
            continue;
        e.name = o.value(QStringLiteral("name")).toString(e.id);
        e.systems = toStringList(o.value(QStringLiteral("systems")));
        e.exeNames = toStringList(o.value(QStringLiteral("exe")).toObject().value(plat));
        e.args = toStringList(o.value(QStringLiteral("args")));
        e.argsWindowed = toStringList(o.value(QStringLiteral("argsWindowed")));
        if (e.argsWindowed.isEmpty())
            e.argsWindowed = e.args;
        e.website = o.value(QStringLiteral("website")).toString();
        e.notes = Lang::t(o.value(QStringLiteral("notes")).toString());
        const QJsonObject sysNames = o.value(QStringLiteral("systemNames")).toObject();
        for (auto sn = sysNames.begin(); sn != sysNames.end(); ++sn)
            e.systemNames.insert(sn.key(), sn.value().toString());

        e.flatpak = o.value(QStringLiteral("flatpak")).toString();
        if (plat == QLatin1String("linux")) {
            // Sous Linux, un émulateur est souvent une AppImage : « mgba-qt » accepte aussi « mgba-qt.AppImage » et « mGBA.AppImage ».
            QStringList more;
            for (const QString& n : e.exeNames)
                more << n + QStringLiteral(".AppImage");
            more << QString(e.name).remove(QLatin1Char(' ')) + QStringLiteral(".AppImage");
            e.exeNames += more;
            e.exeNames.removeDuplicates();
        }

        const QJsonObject src = o.value(QStringLiteral("source")).toObject();
        if (!src.isEmpty()) {
            e.source.type = src.value(QStringLiteral("type")).toString(QStringLiteral("github"));
            e.source.repo = src.value(QStringLiteral("repo")).toString();
            e.source.host = src.value(QStringLiteral("host")).toString();
            e.source.url = src.value(QStringLiteral("url")).toString();
            e.source.versionRegex = src.value(QStringLiteral("version")).toString();
            // « assets » : une liste (Windows seulement) ou un objet par plateforme { "windows": […], "linux": […], "mac": […] }.
            const QJsonValue assets = src.value(QStringLiteral("assets"));
            if (assets.isObject())
                e.source.assets = toStringList(assets.toObject().value(plat));
            else if (plat == QLatin1String("windows"))
                e.source.assets = toStringList(assets);
            const bool releases = e.source.type == QLatin1String("github") || e.source.type == QLatin1String("gitlab");
            // Linux : à défaut de motif précis, on prend l'AppImage x86_64 des versions publiées.
            if (plat == QLatin1String("linux") && e.source.assets.isEmpty() && releases && src.value(QStringLiteral("appimage")).toBool(true))
                e.source.assets = {QStringLiteral("(?i)^(?!.*(arm|aarch|riscv)).*(x86_64|amd64|x64|linux64).*\\.appimage$"),
                                   QStringLiteral("(?i)^(?!.*(arm|aarch|riscv)).*\\.appimage$")};
            if (e.source.assets.isEmpty() && (releases || plat != QLatin1String("windows")))
                e.source = {}; // rien à installer automatiquement sur cette plateforme : site officiel
        } else if (o.contains(QStringLiteral("github")) && plat == QLatin1String("windows")) { // ancien format
            e.source.type = QStringLiteral("github");
            e.source.repo = o.value(QStringLiteral("github")).toString();
            e.source.assets = toStringList(o.value(QStringLiteral("assets")));
        }

        for (const auto& dv : o.value(QStringLiteral("dataDirs")).toObject().value(plat).toArray()) {
            const QJsonObject d = dv.toObject();
            DataDirRule rule{d.value(QStringLiteral("path")).toString(), d.value(QStringLiteral("when")).toString()};
            if (!rule.path.isEmpty())
                e.dataDirs.push_back(rule);
        }
        if (plat == QLatin1String("linux") && !e.flatpak.isEmpty()) {
            // Version Flatpak : ses données sont dans ~/.var/app/<id>/config ou data, plutôt que ~/.config ou ~/.local/share.
            const QVector<DataDirRule> native = e.dataDirs;
            for (const DataDirRule& r : native) {
                for (const auto& [from, to] : {std::pair{QStringLiteral("%HOME%/.config/"), QStringLiteral("/config/")},
                                               std::pair{QStringLiteral("%HOME%/.local/share/"), QStringLiteral("/data/")}}) {
                    if (r.path.startsWith(from))
                        e.dataDirs.push_back({QStringLiteral("%HOME%/.var/app/") + e.flatpak + to + r.path.mid(from.size()), QString()});
                }
            }
        }
        e.saves = toStringList(o.value(QStringLiteral("saves")));
        for (const auto& fv : o.value(QStringLiteral("systemFiles")).toArray()) {
            const QJsonObject f = fv.toObject();
            SystemFileDef sf;
            sf.label = Lang::t(f.value(QStringLiteral("label")).toString());
            sf.path = f.value(QStringLiteral("path")).toString();
            sf.isDir = f.value(QStringLiteral("kind")).toString() == QLatin1String("dir");
            sf.required = f.value(QStringLiteral("required")).toBool();
            sf.importable = f.value(QStringLiteral("import")).toBool();
            sf.help = Lang::t(f.value(QStringLiteral("help")).toString());
            if (!sf.path.isEmpty())
                e.systemFiles.push_back(sf);
        }
        e.needs = toStringList(o.value(QStringLiteral("requires")));
        e.unsupported = toStringList(o.value(QStringLiteral("unsupported")));
        e.versionProbe = o.value(QStringLiteral("versionProbe")).toString();

        bool replaced = false;
        if (override) {
            for (auto& existing : m_emulators) {
                if (existing.id == e.id) {
                    existing = e;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced)
            m_emulators.push_back(e);
    }
}

const SystemInfo* Registry::system(const QString& id) const {
    for (const auto& s : m_systems)
        if (s.id == id)
            return &s;
    return nullptr;
}

const EmulatorDef* Registry::emulator(const QString& id) const {
    for (const auto& e : m_emulators)
        if (e.id == id)
            return &e;
    return nullptr;
}

QVector<const EmulatorDef*> Registry::emulatorsFor(const QString& systemId) const {
    QVector<const EmulatorDef*> out;
    for (const auto& e : m_emulators)
        if (e.systems.contains(systemId))
            out.push_back(&e);
    return out;
}

QString Registry::builtinDefaultEmulator(const QString& systemId) const {
    const QString d = m_defaults.value(systemId);
    if (!d.isEmpty() && emulator(d))
        return d;
    const auto list = emulatorsFor(systemId);
    return list.isEmpty() ? QString() : list.first()->id;
}

QStringList Registry::systemsForExtension(const QString& ext) const {
    QStringList out;
    const QString e = ext.toLower();
    for (const auto& s : m_systems)
        if (s.extensions.contains(e))
            out << s.id;
    return out;
}

QStringList Registry::allExeNames() const {
    QSet<QString> set;
    for (const auto& e : m_emulators)
        for (const auto& n : e.exeNames)
            set.insert(n.toLower());
    return QStringList(set.begin(), set.end());
}

QStringList Registry::allExtensions() const {
    QSet<QString> set;
    for (const auto& s : m_systems)
        for (const auto& e : s.extensions)
            set.insert(e);
    QStringList out(set.begin(), set.end());
    out.sort();
    return out;
}
