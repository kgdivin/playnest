// Test de détection : analyse un dossier et affiche ce que Playnest reconnaît.
// Usage : playnest_detect_test <dossier>
#include "core/CoverManager.h"
#include "core/Emulators.h"
#include "core/Launcher.h"
#include "core/Library.h"
#include "core/Organizer.h"
#include "core/Paths.h"
#include "core/Registry.h"
#include "core/RomDetector.h"
#include "core/Saves.h"
#include "core/Settings.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

static QString readAll(const QString& p) {
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QStringLiteral("<absent>");
}

// Test de mise à jour : détection, copie des sauvegardes, remplacement, retour arrière, restauration.
static int installTest(const QString& emuDir) {
    QTextStream out(stdout);
    auto& em = EmulatorManager::instance();
    QEventLoop loop;
    QObject::connect(&em, &EmulatorManager::detectionFinished, &loop, &QEventLoop::quit);
    em.init();
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    loop.exec();
    EmulatorStatus st = em.status(QStringLiteral("fakeemu"));
    out << "Détecté : " << QDir::toNativeSeparators(st.exe) << " version=" << st.version << " données=" << st.dataDir << "\n";
    if (!st.installed())
        return 1;
    bool ok = false;
    QString msg;
    QObject::connect(&em, &EmulatorManager::installProgress, [&](const QString&, const QString& stage, qint64, qint64) { out << "  … " << stage << "\n"; out.flush(); });
    QObject::connect(&em, &EmulatorManager::installFinished, &loop, [&](const QString&, bool o, const QString& m) {
        ok = o;
        msg = m;
        loop.quit();
    });
    em.install(QStringLiteral("fakeemu"));
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();
    st = em.status(QStringLiteral("fakeemu"));
    out << "Mise à jour : " << (ok ? "OK" : "ÉCHEC") << " — " << msg << "\n";
    out << "  lib.dat=" << readAll(emuDir + "/lib.dat") << " new.dat=" << readAll(emuDir + "/new.dat")
        << " réglage conservé=" << readAll(emuDir + "/user/config.ini") << " version=" << st.version
        << " retour arrière possible=" << (st.canRollback ? "oui" : "non") << "\n";
    SaveManager::instance().reload();
    out << "  copies de sauvegardes : " << SaveManager::instance().backups().size() << "\n";
    QString err;
    const bool rb = em.rollback(QStringLiteral("fakeemu"), &err);
    st = em.status(QStringLiteral("fakeemu"));
    out << "Retour arrière : " << (rb ? "OK" : err) << " — lib.dat=" << readAll(emuDir + "/lib.dat")
        << " new.dat=" << readAll(emuDir + "/new.dat") << " version=" << st.version << "\n";
    // Restauration : la sauvegarde modifiée doit revenir à son contenu d'origine.
    QFile save(emuDir + "/user/saves/slot1.sav");
    save.open(QIODevice::WriteOnly);
    save.write("SAVE-B");
    save.close();
    const auto backups = SaveManager::instance().backups();
    if (backups.isEmpty())
        return 1;
    QObject::connect(&SaveManager::instance(), &SaveManager::finished, &loop, [&](bool o, const QString& m) {
        ok = o;
        msg = m;
        loop.quit();
    });
    SaveManager::instance().restore(backups.first().file, backups.first().keys);
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    loop.exec();
    out << "Restauration : " << (ok ? "OK" : "ÉCHEC") << " — " << msg << " slot1.sav=" << readAll(emuDir + "/user/saves/slot1.sav")
        << " copies=" << SaveManager::instance().backups().size() << "\n";
    return 0;
}

// Mise à jour depuis une archive déjà téléchargée (ex. une nouvelle build de Lichii).
static int installFileTest(const QString& id, const QString& emuDir, const QString& archive) {
    QTextStream out(stdout);
    auto& em = EmulatorManager::instance();
    QEventLoop loop;
    QObject::connect(&em, &EmulatorManager::detectionFinished, &loop, &QEventLoop::quit);
    em.init();
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    loop.exec();
    const EmulatorDef* def = Registry::instance().emulator(id);
    em.setExePath(id, QDir::toNativeSeparators(emuDir + QLatin1Char('/') + def->exeNames.value(0)));
    EmulatorStatus st = em.status(id);
    out << "Avant : " << QDir::toNativeSeparators(st.exe) << " version=" << st.version << "\n";
    out << "Archive reconnue comme : " << em.identifyArchive(archive) << "\n";
    const QString cfgBefore = readAll(emuDir + "/user/config/qt-config.ini");
    bool ok = false;
    QString msg;
    QObject::connect(&em, &EmulatorManager::installFinished, &loop, [&](const QString&, bool o, const QString& m) {
        ok = o;
        msg = m;
        loop.quit();
    });
    em.installFromFile(id, archive);
    QTimer::singleShot(300000, &loop, &QEventLoop::quit);
    loop.exec();
    st = em.status(id);
    out << "Mise à jour : " << (ok ? "OK" : "ÉCHEC") << " — " << msg << "\n";
    out << "  version=" << st.version << " réglages conservés=" << (readAll(emuDir + "/user/config/qt-config.ini") == cfgBefore ? "oui" : "NON")
        << " clés conservées=" << (QFileInfo::exists(emuDir + "/user/keys/prod.keys") ? "oui" : "NON")
        << " archive gardée=" << (QFileInfo::exists(archive) ? "oui" : "NON")
        << " retour arrière possible=" << (st.canRollback ? "oui" : "non") << "\n";
    QString err;
    const bool rb = em.rollback(id, &err);
    st = em.status(id);
    out << "Retour arrière : " << (rb ? "OK" : err) << " — version=" << st.version << "\n";
    out.flush();
    return ok && rb ? 0 : 1;
}

// Ajout de nouveaux jeux : chacun va dans Jeux / Mises à jour / DLC de sa console ; copie puis annulation.
static int importTest(const QString& root, const QStringList& inputs, bool copy) {
    QTextStream out(stdout);
    Settings::instance().setValue(QStringLiteral("organize/root"), root);
    Library lib;
    Organizer org(&lib);
    const auto plan = org.planImport(inputs, copy);
    out << "Plan : " << plan.moves.size() << " à ajouter, " << plan.unknown << " ignoré(s), " << plan.alreadyInPlace << " déjà en place\n";
    for (const auto& m : plan.moves)
        out << "  " << m.label << "  ->  " << QDir(root).relativeFilePath(m.to) << (m.copy ? "  (copie)" : "") << "\n";
    QEventLoop loop;
    Organizer::Result res;
    QObject::connect(&org, &Organizer::finished, &loop, [&](const Organizer::Result& r) { res = r; loop.quit(); });
    org.run(plan);
    loop.exec();
    int present = 0, sources = 0;
    for (const QString& p : res.newPaths)
        present += QFileInfo::exists(p) ? 1 : 0;
    for (const auto& m : plan.moves)
        sources += QFileInfo::exists(m.from) ? 1 : 0;
    out << "Résultat : " << res.message << " ; " << present << " présent(s) à destination, " << sources << " original(aux) encore là\n";
    out << "Dossiers de jeux : " << Settings::instance().romFolders().join(QStringLiteral(", ")) << "\n";
    org.undo(res.journal);
    loop.exec();
    int back = 0;
    for (const auto& m : plan.moves)
        back += QFileInfo::exists(m.from) ? 1 : 0;
    out << "Annulation : " << res.message << " ; " << back << "/" << plan.moves.size() << " originaux en place\n";
    out.flush();
    return back == plan.moves.size() ? 0 : 1;
}

// Dumps laissés sous un nom vide de sens (« Jeux/Switch 2/base ») : renommés d'après leur titre, une seule fois.
static int renameTest(const QString& root) {
    QTextStream out(stdout);
    QVector<DetectedRom> found;
    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext())
        if (auto r = RomDetector::detectDirectory(it.next()))
            found << *r;
    Library lib;
    lib.mergeScan(found, {});
    for (const Game& g : lib.games())
        out << "  jeu : " << g.displayTitle() << " [" << g.system() << "] " << QDir(root).relativeFilePath(g.path) << "\n";
    Organizer org(&lib);
    Organizer::Options o;
    o.root = QDir::fromNativeSeparators(root);
    o.emulators = false;
    o.saves = false;
    auto plan = org.plan(o);
    out << "Plan 1 : " << plan.moves.size() << " déplacement(s), " << plan.alreadyInPlace << " déjà en place\n";
    for (const auto& m : plan.moves)
        out << "  " << QDir(root).relativeFilePath(m.from) << "  ->  " << QDir(root).relativeFilePath(m.to) << "\n";
    QEventLoop loop;
    Organizer::Result res;
    QObject::connect(&org, &Organizer::finished, &loop, [&](const Organizer::Result& r) { res = r; loop.quit(); });
    org.run(plan);
    loop.exec();
    out << "Rangement : " << res.message << "\n";
    int ok = 0;
    for (const Game& g : lib.games())
        ok += QFileInfo::exists(g.path) ? 1 : 0;
    const auto again = org.plan(o);
    out << "Plan 2 : " << again.moves.size() << " déplacement(s) ; " << ok << "/" << lib.games().size() << " jeux trouvés\n";
    const bool consoleKept = QFileInfo(root + QStringLiteral("/Jeux/Switch")).isDir();
    out << "Dossier Jeux/Switch : " << (consoleKept ? "en place" : "DÉPLACÉ") << "\n";
    out.flush();
    return (again.moves.isEmpty() && ok == lib.games().size() && consoleKept) ? 0 : 1;
}

// Fichiers déposés : archive, application seule ou installateur ?
static int classifyTest(const QStringList& files) {
    QTextStream out(stdout);
    static const char* kinds[] = {"inconnu", "archive", "application", "installateur"};
    for (const QString& f : files) {
        const auto k = EmulatorManager::instance().classifyFile(f);
        out << QFileInfo(f).fileName() << " -> " << kinds[k.kind] << " " << k.id << "\n";
    }
    return 0;
}

// Installateur officiel : lancé, attendu, puis la nouvelle version est suivie.
static int installerTest(const QString& id, const QString& installer) {
    QTextStream out(stdout);
    auto& em = EmulatorManager::instance();
    QEventLoop loop;
    QObject::connect(&em, &EmulatorManager::detectionFinished, &loop, &QEventLoop::quit);
    em.init();
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    loop.exec();
    out << "Avant : " << QDir::toNativeSeparators(em.status(id).exe) << "\n";
    bool ok = false;
    QString msg;
    QObject::connect(&em, &EmulatorManager::installProgress, [&](const QString&, const QString& stage, qint64, qint64) { out << "  … " << stage << "\n"; out.flush(); });
    QObject::connect(&em, &EmulatorManager::installFinished, &loop, [&](const QString&, bool o, const QString& m) {
        ok = o;
        msg = m;
        loop.quit();
    });
    em.runInstaller(id, installer);
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);
    loop.exec();
    out << "Installateur : " << (ok ? "OK" : "ÉCHEC") << " — " << msg << "\n";
    out << "Après : " << QDir::toNativeSeparators(em.status(id).exe) << "\n";
    out.flush();
    return ok ? 0 : 1;
}

// Rangement : déplace une copie des échantillons, vérifie la bibliothèque, puis annule.
int organizeTest(const QString& samples) {
    QTextStream out(stdout);
    QVector<DetectedRom> found;
    QStringList titleDirs;
    QDirIterator it(samples, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        bool inside = false;
        for (const QString& t : titleDirs)
            inside |= p.startsWith(t + QLatin1Char('/'));
        if (inside)
            continue;
        if (it.fileInfo().isDir()) {
            if (auto r = RomDetector::detectDirectory(p)) {
                titleDirs << p;
                found << *r;
            }
        } else if (auto r = RomDetector::detectFile(p)) {
            found << *r;
        }
    }
    QFile sav(samples + QStringLiteral("/DS/Aventure Playnest.sav")); // sauvegarde posée à côté d'une ROM DS
    Library lib;
    lib.mergeScan(found, {});
    for (const Game& g : lib.games())
        if (g.system() == QLatin1String("nds")) {
            QFile f(QFileInfo(g.path).absolutePath() + QLatin1Char('/') + QFileInfo(g.path).completeBaseName() + QStringLiteral(".sav"));
            f.open(QIODevice::WriteOnly);
            f.write("SAVE");
        }
    lib.update(lib.games().first().path, [](Game& g) { g.playSeconds = 4242; g.favorite = true; });
    const QString firstTitle = lib.games().first().displayTitle();
    Settings::instance().setRomFolders({samples});
    Organizer org(&lib);
    Organizer::Options o;
    o.root = QDir::fromNativeSeparators(samples) + QStringLiteral("/../Rangé");
    o.root = QDir::cleanPath(o.root);
    o.emulators = false;
    o.saves = false;
    const auto plan = org.plan(o);
    out << "Plan : " << plan.moves.size() << " déplacement(s), " << plan.alreadyInPlace << " déjà en place\n";
    QEventLoop loop;
    Organizer::Result res;
    QObject::connect(&org, &Organizer::finished, &loop, [&](const Organizer::Result& r) { res = r; loop.quit(); });
    org.run(plan);
    loop.exec();
    out << "Rangement : " << res.message << "\n";
    int ok = 0, bad = 0;
    for (const Game& g : lib.games()) {
        const bool inRoot = QDir::fromNativeSeparators(g.path).startsWith(QDir::fromNativeSeparators(o.root) + QLatin1Char('/'), Qt::CaseInsensitive);
        (QFileInfo::exists(g.path) && inRoot) ? ++ok : ++bad;
    }
    QStringList tree;
    QDirIterator t(o.root, QDir::Files, QDirIterator::Subdirectories);
    while (t.hasNext())
        tree << QDir(o.root).relativeFilePath(t.next());
    tree.sort();
    for (const QString& f : tree.mid(0, 400))
        if (f.contains(QLatin1String("Switch")) || f.endsWith(QLatin1String(".sav")) || f.startsWith(QLatin1String("Jeux/PC/")))
            out << "    " << f << "\n";
    const Game* first = nullptr;
    for (const Game& g : lib.games())
        if (g.displayTitle() == firstTitle && g.playSeconds == 4242)
            first = &g;
    out << "Bibliothèque : " << ok << " chemin(s) à jour, " << bad << " incorrect(s) ; historique conservé : " << (first ? "oui" : "NON") << "\n";
    out << "Dossiers de jeux après : " << Settings::instance().romFolders().join(QStringLiteral(", ")) << "\n";
    org.undo(res.journal);
    loop.exec();
    out << "Annulation : " << res.message << "\n";
    int back = 0, lost = 0;
    for (const Game& g : lib.games())
        (QFileInfo::exists(g.path) && QDir::fromNativeSeparators(g.path).startsWith(QDir::fromNativeSeparators(samples), Qt::CaseInsensitive)) ? ++back : ++lost;
    out << "Après annulation : " << back << " à leur place, " << lost << " ailleurs ; dossier de rangement "
        << (QFileInfo::exists(o.root) ? "encore présent" : "supprimé") << "\n";
    out.flush();
    return (bad == 0 && lost == 0 && first) ? 0 : 1;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationVersion(QStringLiteral("2.1.0"));
    QCoreApplication::setApplicationName(QStringLiteral("Playnest"));
    QCoreApplication::setOrganizationName(QStringLiteral("Playnest"));
    Paths::init();
    Settings::instance().init();
    Registry::instance().load();
    QTextStream out(stdout);
    if (argc >= 4 && (QByteArray(argv[1]) == "--import-test" || QByteArray(argv[1]) == "--import-copy-test")) {
        QStringList in;
        for (int i = 3; i < argc; ++i)
            in << QString::fromLocal8Bit(argv[i]);
        return importTest(QString::fromLocal8Bit(argv[2]), in, QByteArray(argv[1]) == "--import-copy-test");
    }
    if (argc >= 3 && QByteArray(argv[1]) == "--launch-pc") { // jeu PC lancé directement
        QTextStream out(stdout);
        const auto r = RomDetector::detectPcGame(QString::fromLocal8Bit(argv[2]), false);
        if (!r)
            return 1;
        Game g;
        g.path = r->path;
        Library::applyDetected(g, *r);
        Launcher l;
        QEventLoop loop;
        int code = -1;
        QObject::connect(&l, &Launcher::finished, &loop, [&](const QString&, int c, qint64, bool, const QString&) { code = c; loop.quit(); });
        QString err;
        if (!l.launch(g, QString(), &err)) {
            out << "ÉCHEC : " << err << "\n";
            return 1;
        }
        QTimer::singleShot(30000, &loop, &QEventLoop::quit);
        loop.exec();
        out << "Jeu PC « " << g.displayTitle() << " » lancé sans émulateur, terminé avec le code " << code << "\n";
        return code == 0 ? 0 : 1;
    }
    if (argc >= 3 && QByteArray(argv[1]) == "--cover-test") { // jaquette : nom exact, puis liste + titre le plus proche
        QTextStream out(stdout);
        QVector<DetectedRom> found;
        for (int i = 2; i < argc; ++i)
            if (auto r = RomDetector::detectFile(QString::fromLocal8Bit(argv[i])))
                found << *r;
        Library lib;
        lib.mergeScan(found, {});
        CoverManager covers(&lib);
        QEventLoop loop;
        QObject::connect(&covers, &CoverManager::pendingChanged, &loop, [&](int n) { if (n == 0) loop.quit(); });
        for (const Game& g : lib.games())
            covers.request(g, true);
        QTimer::singleShot(30000, &loop, &QEventLoop::quit);
        if (covers.pending() > 0)
            loop.exec();
        for (const Game& g : lib.games())
            out << "  " << g.displayTitle() << "  ->  " << (g.coverPath.isEmpty() ? QStringLiteral("(aucune)") : QFileInfo(g.coverPath).fileName()) << "\n";
        return 0;
    }
    if (argc >= 2 && QByteArray(argv[1]) == "--match-test") { // rapprochement des titres et des jaquettes
        QTextStream out(stdout);
        const QStringList eden{"mario-kart-8-deluxe", "pokemon-lets-go-eevee", "pokemon-lets-go-pikachu", "pokemon-legends-z-a",
                               "pokemon-legends-arceus", "the-legend-of-zelda-breath-of-the-wild", "the-legend-of-zelda-links-awakening",
                               "super-mario-odyssey", "pokemon-sword", "pokemon-shield", "pokemon-violet"};
        const QStringList n64{"Legend of Zelda, The - Ocarina of Time (USA)", "Legend of Zelda, The - Ocarina of Time (Europe) (En,Fr,De)",
                              "Legend of Zelda, The - Ocarina of Time - Master Quest (USA) (Debug Edition)",
                              "Legend of Zelda, The - Majora's Mask (USA)", "Super Mario 64 (USA)", "Super Mario 64 (Japan) (Beta)",
                              "Mario Kart 64 (Europe)"};
        const QList<QPair<QString, QStringList>> cases{
            {"Pokemon Let’s Go Eevee", eden}, {"Pokemon Legends Z-A Switch 2 Edition", eden}, {"Pokémon Legends Z-A", eden},
            {"SAEKO Giantess Dating Sim", eden}, {"The Legend of Zelda: Ocarina of Time", eden}, {"Mario Kart World", eden},
            {"Ocarina of Time - Gold Quest", n64}, {"The Legend of Zelda - Ocarina of Time", n64}, {"Super Mario 64", n64}, {"Mario", n64}};
        for (const auto& [t, names] : cases) {
            QString title = t;
            title.remove(QRegularExpression(QStringLiteral("(?i)\\s*[-–:]?\\s*(nintendo\\s+)?switch\\s*2(\\s+edition)?\\s*")));
            out << "  " << t << "  ->  " << (CoverManager::bestMatch(title, QStringLiteral("EU"), names).isEmpty() ? QStringLiteral("(rien)") : CoverManager::bestMatch(title, QStringLiteral("EU"), names)) << "\n";
        }
        return 0;
    }
    if (argc >= 3 && QByteArray(argv[1]) == "--classify") {
        QStringList files;
        for (int i = 2; i < argc; ++i)
            files << QString::fromLocal8Bit(argv[i]);
        return classifyTest(files);
    }
    if (argc >= 4 && QByteArray(argv[1]) == "--installer-test")
        return installerTest(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    if (argc >= 5 && QByteArray(argv[1]) == "--install-file")
        return installFileTest(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]), QString::fromLocal8Bit(argv[4]));
    if (argc >= 3 && QByteArray(argv[1]) == "--rename-test")
        return renameTest(QString::fromLocal8Bit(argv[2]));
    if (argc >= 3 && QByteArray(argv[1]) == "--organize-test")
        return organizeTest(QString::fromLocal8Bit(argv[2]));
    if (argc >= 3 && QByteArray(argv[1]) == "--install-test")
        return installTest(QString::fromLocal8Bit(argv[2]));
    if (argc < 2) {
        out << "usage: playnest_detect_test <dossier>\n";
        return 2;
    }
    const QString root = QString::fromLocal8Bit(argv[1]);
    int found = 0, rejected = 0;
    QStringList titleDirs;
    QVector<DetectedRom> detected;
    QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    QStringList paths;
    while (it.hasNext())
        paths << it.next();
    paths.sort();
    for (const QString& p : paths) {
        bool inside = false;
        for (const auto& d : titleDirs)
            if (p.startsWith(d + QLatin1Char('/')))
                inside = true;
        if (inside)
            continue;
        std::optional<DetectedRom> r;
        if (QFileInfo(p).isDir()) {
            r = RomDetector::detectDirectory(p);
            if (r)
                titleDirs << p;
            else
                continue;
        } else {
            r = RomDetector::detectFile(p);
        }
        const QString rel = QDir(root).relativeFilePath(p);
        if (!r) {
            ++rejected;
            out << "  ignoré   " << rel << "\n";
            continue;
        }
        ++found;
        out << QStringLiteral("  %1 %2\n           titre=\"%3\" id=%4 tid=%5 région=%6 icône=%7%8\n")
                   .arg(r->systemId.leftJustified(8), rel, r->title, r->gameId.isEmpty() ? "-" : r->gameId,
                        r->titleId.isEmpty() ? "-" : r->titleId, r->region.isEmpty() ? "-" : r->region,
                        r->icon.isNull() ? "non" : QStringLiteral("%1x%2").arg(r->icon.width()).arg(r->icon.height()),
                        r->isAddon ? QStringLiteral(" [%1 \"%2\" -> %3]").arg(r->addonKind, r->addonName, r->baseTitleId.isEmpty() ? "?" : r->baseTitleId) : QString());
        if (!r->contentInfo.isEmpty())
            out << "           contenu: " << r->contentInfo << "\n";
        detected << *r;
        Game g;
        g.path = r->path;
        g.systemId = r->systemId;
        g.title = r->title;
        g.gameId = r->gameId;
        const QStringList urls = CoverManager::candidateUrls(g);
        if (!urls.isEmpty())
            out << "           jaquette: " << urls.first() << (urls.size() > 1 ? QStringLiteral(" (+%1)").arg(urls.size() - 1) : QString()) << "\n";
    }
    out << "\n" << found << " jeux reconnus, " << rejected << " fichiers ignorés\n\n";

    // Rattachement des mises à jour et DLC à leur jeu.
    {
        Library lib;
        lib.mergeScan(detected, {});
        out << "Contenus rattachés :\n";
        int orphans = 0;
        for (const Game& g : lib.games()) {
            if (g.isAddon) {
                if (!lib.baseFor(g))
                    ++orphans, out << "  (orphelin) " << QFileInfo(g.path).fileName() << "\n";
                continue;
            }
            const auto addons = lib.addonsFor(g);
            if (addons.isEmpty())
                continue;
            out << "  " << g.displayTitle() << " [" << g.system() << "]\n";
            for (const Game* a : addons)
                out << "      + " << Library::addonLabel(*a, &g) << "\n";
        }
        out << "  " << lib.addonCount() << " contenus additionnels, " << orphans << " orphelin(s)\n";
        const Game* nup = nullptr;
        for (const Game& g : lib.games())
            if (g.path.endsWith(QLatin1String(".nup")))
                nup = &g;
        if (nup)
            out << "  Émulateur choisi pour le .nup : " << EmulatorManager::instance().emulatorFor(*nup) << "\n";
        out << "\n";
    }

    out << "Nettoyage des titres :\n";
    for (const char* n : {"Super Mario 64 (USA).z64", "1234 - Pokemon Diamond (U)(XenoPhobia).nds",
                          "Super.Mario.Odyssey.[0100000000010000].nsp", "The_Legend_of_Zelda_[v0].xci",
                          "Metroid Prime (Europe) (En,Fr,De,Es,It) (Rev 1).nkit.iso"}) {
        QString region;
        const QString t = RomDetector::cleanTitle(QString::fromUtf8(n), &region);
        out << "  " << n << "  ->  \"" << t << "\"  région=" << (region.isEmpty() ? "-" : region) << "\n";
    }

    // Test de lancement : un faux émulateur enregistre ses arguments.
#ifndef Q_OS_WIN
    QTemporaryDir tmp;
    const QString fake = tmp.filePath(QStringLiteral("melonDS"));
    const QString argsOut = tmp.filePath(QStringLiteral("args.txt"));
    {
        QFile f(fake);
        f.open(QIODevice::WriteOnly);
        f.write(QStringLiteral("#!/bin/sh\nfor a in \"$@\"; do echo \"[$a]\"; done > '%1'\nsleep 1\nexit 0\n").arg(argsOut).toUtf8());
        f.close();
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
    }
    EmulatorManager::instance().setExePath(QStringLiteral("melonds"), fake);
    Game g;
    QDirIterator nds(root, {QStringLiteral("*.nds")}, QDir::Files, QDirIterator::Subdirectories);
    g.path = nds.next();
    g.systemId = QStringLiteral("nds");
    g.title = QStringLiteral("Test");
    Launcher launcher;
    QEventLoop loop;
    qint64 secs = -1;
    QObject::connect(&launcher, &Launcher::finished, &loop, [&](const QString&, int code, qint64 s, bool crashed, const QString&) {
        secs = s;
        out << "\nLancement : code=" << code << " durée=" << s << "s crash=" << (crashed ? "oui" : "non") << "\n";
        loop.quit();
    });
    QString err;
    if (!launcher.launch(g, EmulatorManager::instance().emulatorFor(g), &err)) {
        out << "ÉCHEC du lancement : " << err << "\n";
        return 1;
    }
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    QFile a(argsOut);
    a.open(QIODevice::ReadOnly);
    out << "Arguments reçus par l'émulateur :\n" << QString::fromUtf8(a.readAll());
    EmulatorManager::instance().setExePath(QStringLiteral("melonds"), QString());
    if (secs < 0)
        return 1;
#endif
    return 0;
}
