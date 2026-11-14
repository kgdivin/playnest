#include "CoverManager.h"

#include "Library.h"
#include "Net.h"
#include "Paths.h"
#include "RomDetector.h"
#include "Settings.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QUrl>

namespace {

constexpr int kMaxConcurrent = 3;

// Code région GameTDB à partir du 4e caractère de l'ID.
QString tdbRegion(QChar c) {
    switch (c.toUpper().unicode()) {
    case 'E': return QStringLiteral("US");
    case 'J': return QStringLiteral("JA");
    case 'K': return QStringLiteral("KO");
    case 'D': return QStringLiteral("DE");
    case 'F': return QStringLiteral("FR");
    case 'S': return QStringLiteral("ES");
    case 'I': return QStringLiteral("IT");
    case 'H': return QStringLiteral("NL");
    case 'U': return QStringLiteral("AU");
    case 'W': return QStringLiteral("ZH");
    default: return QStringLiteral("EN"); // PAL et variantes
    }
}

QStringList regionOrder(const QString& id) {
    QStringList out;
    const QString pref = Settings::instance().coverLanguage().toUpper();
    const QChar rc = id.size() >= 4 ? id.at(3) : QChar('E');
    const QString native = tdbRegion(rc);
    // Pour un jeu PAL, la langue préférée passe avant l'anglais.
    if (native == QLatin1String("EN") && !pref.isEmpty())
        out << pref;
    out << native;
    for (const QString& r : {pref, QStringLiteral("EN"), QStringLiteral("US"), QStringLiteral("FR"), QStringLiteral("JA")})
        if (!r.isEmpty() && !out.contains(r))
            out << r;
    return out.mid(0, 4);
}

QString libretroRepo(const QString& sys) {
    if (sys == QLatin1String("n64")) return QStringLiteral("Nintendo_-_Nintendo_64");
    if (sys == QLatin1String("nds")) return QStringLiteral("Nintendo_-_Nintendo_DS");
    if (sys == QLatin1String("3ds")) return QStringLiteral("Nintendo_-_Nintendo_3DS");
    if (sys == QLatin1String("gc")) return QStringLiteral("Nintendo_-_GameCube");
    if (sys == QLatin1String("wii")) return QStringLiteral("Nintendo_-_Wii");
    if (sys == QLatin1String("nes")) return QStringLiteral("Nintendo_-_Nintendo_Entertainment_System");
    if (sys == QLatin1String("snes")) return QStringLiteral("Nintendo_-_Super_Nintendo_Entertainment_System");
    if (sys == QLatin1String("gb")) return QStringLiteral("Nintendo_-_Game_Boy");
    if (sys == QLatin1String("gbc")) return QStringLiteral("Nintendo_-_Game_Boy_Color");
    if (sys == QLatin1String("gba")) return QStringLiteral("Nintendo_-_Game_Boy_Advance");
    return {};
}

QString libretroUrl(const QString& repo, QString name) {
    static const QRegularExpression bad(QStringLiteral("[&*/:`<>?\\\\|\"]"));
    name.replace(bad, QStringLiteral("_"));
    return QStringLiteral("https://raw.githubusercontent.com/libretro-thumbnails/%1/master/Named_Boxarts/%2.png")
        .arg(repo, QString::fromUtf8(QUrl::toPercentEncoding(name)));
}

QString edenSlug(const QString& title) {
    QString s = title.toLower().normalized(QString::NormalizationForm_D);
    QString out;
    bool dash = false;
    for (const QChar c : s) {
        if (c.category() == QChar::Mark_NonSpacing)
            continue;
        if (c.isLetterOrNumber() && c.unicode() < 128) {
            out += c;
            dash = false;
        } else if (!dash && !out.isEmpty()) {
            out += QLatin1Char('-');
            dash = true;
        }
    }
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    return out;
}

// Sources dont on peut lister les jaquettes : dépôt, branche, dossier.
struct IndexSource {
    QString owner, repo, branch, folder;
};
IndexSource indexSourceFor(const QString& sys) {
    if (sys == QLatin1String("switch") || sys == QLatin1String("switch2"))
        return {QStringLiteral("eden-emulator"), QStringLiteral("boxart"), QStringLiteral("master"), QStringLiteral("img")};
    const QString lr = libretroRepo(sys);
    if (!lr.isEmpty())
        return {QStringLiteral("libretro-thumbnails"), lr, QStringLiteral("master"), QStringLiteral("Named_Boxarts")};
    return {};
}

QString indexFile(const QString& source) {
    return Paths::ensureDir(Paths::coversDir() + QStringLiteral("/index")) + QLatin1Char('/') + QString(source).replace(QLatin1Char('/'), QLatin1Char('_')) +
           QStringLiteral(".txt");
}

// Mots significatifs d'un titre : sans accents, apostrophes, ponctuation, balises ni petits mots.
QStringList titleWords(QString s) {
    static const QRegularExpression tags(QStringLiteral("\\s*[\\[(][^\\])]*[\\])]"));
    s.remove(tags);
    // « Legend of Zelda, The - Ocarina of Time » → « The Legend of Zelda - Ocarina of Time »
    static const QRegularExpression article(QStringLiteral("^(.*?),\\s*(The|A|An|Le|La|Les)\\b(.*)$"), QRegularExpression::CaseInsensitiveOption);
    if (const auto m = article.match(s); m.hasMatch())
        s = m.captured(2) + QLatin1Char(' ') + m.captured(1) + m.captured(3);
    s = s.toLower().normalized(QString::NormalizationForm_D);
    QString flat;
    for (const QChar c : s) {
        if (c.category() == QChar::Mark_NonSpacing || c == QLatin1Char('\'') || c == QChar(0x2019) || c == QLatin1Char('`'))
            continue; // « let's » → « lets »
        flat += c == QLatin1Char('&') ? QStringLiteral(" and ") : (c.isLetterOrNumber() ? QString(c) : QStringLiteral(" "));
    }
    static const QSet<QString> stop = {QStringLiteral("the"), QStringLiteral("a"), QStringLiteral("an"), QStringLiteral("of"),
                                       QStringLiteral("and"), QStringLiteral("le"), QStringLiteral("la"), QStringLiteral("les"),
                                       QStringLiteral("de"), QStringLiteral("du"), QStringLiteral("des"), QStringLiteral("version"),
                                       QStringLiteral("edition"), QStringLiteral("nintendo")};
    QStringList out;
    for (const QString& w : flat.split(QLatin1Char(' '), Qt::SkipEmptyParts))
        if (!stop.contains(w))
            out << w;
    return out;
}

// Adresse de test (serveur local) pour vérifier le chargement des listes.
QUrl testable(const QString& url) {
    static const QString base = qEnvironmentVariable("PLAYNEST_COVER_TEST_BASE");
    if (base.isEmpty())
        return QUrl(url);
    QString u = url;
    u.replace(QStringLiteral("https://api.github.com"), base + QStringLiteral("/api"));
    u.replace(QStringLiteral("https://raw.githubusercontent.com"), base + QStringLiteral("/raw"));
    return QUrl(u);
}
QString cacheName(const QString& gamePath) {
    return QString::fromLatin1(QCryptographicHash::hash(gamePath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
}

} // namespace

CoverManager::CoverManager(Library* lib, QObject* parent) : QObject(parent), m_lib(lib) {}

QString CoverManager::bestMatch(const QString& title, const QString& region, const QStringList& names) {
    // 1. tous les mots du titre présents ; 2. sinon la partie avant « - » (hacks : « Ocarina of Time - Gold Quest »).
    QList<QStringList> attempts{titleWords(title)};
    const int dash = title.indexOf(QStringLiteral(" - "));
    if (dash > 3)
        attempts << titleWords(title.left(dash));
    static const QRegularExpression unwanted(QStringLiteral("(?i)\\((beta|proto|demo|sample|kiosk|prototype|rev \\d)"));
    const QString reg = region.toUpper();
    const QString wantedRegion = reg == QLatin1String("US") ? QStringLiteral("(usa") : (reg == QLatin1String("JP") ? QStringLiteral("(japan") : QStringLiteral("(europe"));
    for (const QStringList& words : attempts) {
        if (words.isEmpty() || (words.size() == 1 && words.first().size() < 5))
            continue; // trop court pour être fiable
        const QSet<QString> want(words.begin(), words.end());
        QString best;
        int bestScore = -1000;
        for (const QString& n : names) {
            const QStringList cw = titleWords(QString(n).replace(QLatin1Char('-'), QLatin1Char(' ')));
            const QSet<QString> have(cw.begin(), cw.end());
            if (!have.contains(want) || have.size() - want.size() > qMax(1, int(want.size())))
                continue; // « Mario » ne doit pas donner « Mario Kart 64 »
            int score = -10 * int(have.size() - want.size()); // le moins de mots en trop
            if (n.contains(wantedRegion, Qt::CaseInsensitive))
                score += 4;
            else if (n.contains(QLatin1String("(world"), Qt::CaseInsensitive) || n.contains(QLatin1String("(usa"), Qt::CaseInsensitive) ||
                     n.contains(QLatin1String("(europe"), Qt::CaseInsensitive))
                score += 2;
            if (unwanted.match(n).hasMatch())
                score -= 6;
            if (score > bestScore) {
                bestScore = score;
                best = n;
            }
        }
        if (!best.isEmpty())
            return best;
    }
    return {};
}

void CoverManager::loadIndex(const QString& source) {
    const QStringList parts = source.split(QLatin1Char('|'));
    const IndexSource src{parts.value(0), parts.value(1), parts.value(2), parts.value(3)};
    // Liste gardée en cache 14 jours.
    QFile cached(indexFile(source));
    if (cached.exists() && QFileInfo(cached).lastModified().daysTo(QDateTime::currentDateTime()) < 14 && cached.open(QIODevice::ReadOnly)) {
        const QStringList names = QString::fromUtf8(cached.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (!names.isEmpty()) {
            m_index.insert(source, names);
            const auto jobs = m_waitingIndex.take(source);
            for (const Job& j : jobs)
                fuzzyStage(j);
            return;
        }
    }
    auto finish = [this, source](const QStringList& names) {
        m_index.insert(source, names);
        if (!names.isEmpty()) {
            QFile out(indexFile(source));
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
                out.write(names.join(QLatin1Char('\n')).toUtf8());
        }
        const auto jobs = m_waitingIndex.take(source);
        for (const Job& j : jobs)
            fuzzyStage(j);
    };
    // Racine du dépôt, puis le seul dossier des jaquettes (évite une liste énorme).
    const QUrl root(QStringLiteral("https://api.github.com/repos/%1/%2/git/trees/%3").arg(src.owner, src.repo, src.branch));
    Net::instance().get(testable(root.toString()), this, [this, src, finish](bool ok, const QByteArray& data, const QString&) {
        QString sha;
        if (ok)
            for (const auto& v : QJsonDocument::fromJson(data).object().value(QStringLiteral("tree")).toArray())
                if (v.toObject().value(QStringLiteral("path")).toString() == src.folder)
                    sha = v.toObject().value(QStringLiteral("sha")).toString();
        if (sha.isEmpty()) {
            finish({});
            return;
        }
        const QUrl folder(QStringLiteral("https://api.github.com/repos/%1/%2/git/trees/%3").arg(src.owner, src.repo, sha));
        Net::instance().get(testable(folder.toString()), this, [finish](bool ok2, const QByteArray& d2, const QString&) {
            QStringList names;
            if (ok2)
                for (const auto& v : QJsonDocument::fromJson(d2).object().value(QStringLiteral("tree")).toArray()) {
                    const QString p = v.toObject().value(QStringLiteral("path")).toString();
                    if (p.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
                        names << p.chopped(4);
                }
            finish(names);
        }, 32 * 1024 * 1024);
    });
}

QString CoverManager::fuzzyUrl(const QString& source, const Game& g) const {
    const QStringList names = m_index.value(source);
    if (names.isEmpty())
        return {};
    QString title = g.displayTitle();
    static const QRegularExpression s2(QStringLiteral("(?i)\\s*[-–:]?\\s*(nintendo\\s+)?switch\\s*2(\\s+edition)?\\s*"));
    title.remove(s2);
    QString match = bestMatch(title, g.region, names);
    if (match.isEmpty() && g.displayTitle() != g.title)
        match = bestMatch(g.title, g.region, names);
    if (match.isEmpty())
        return {};
    const QStringList parts = source.split(QLatin1Char('|'));
    if (parts.value(0) == QLatin1String("eden-emulator"))
        return QStringLiteral("https://raw.githubusercontent.com/eden-emulator/boxart/refs/heads/master/img/%1.png").arg(match);
    return QStringLiteral("https://raw.githubusercontent.com/libretro-thumbnails/%1/master/Named_Boxarts/%2.png")
        .arg(parts.value(1), QString::fromUtf8(QUrl::toPercentEncoding(match)));
}

void CoverManager::fuzzyStage(Job job) {
    job.fuzzyDone = true;
    const Game* g = m_lib->find(job.gamePath);
    const IndexSource src = g ? indexSourceFor(g->system()) : IndexSource{};
    if (!g || src.owner.isEmpty()) {
        finishJob(job, QString());
        return;
    }
    const QString source = src.owner + QLatin1Char('|') + src.repo + QLatin1Char('|') + src.branch + QLatin1Char('|') + src.folder;
    if (!m_index.contains(source)) {
        const bool loading = m_waitingIndex.contains(source);
        m_waitingIndex[source].append(job);
        if (!loading)
            loadIndex(source);
        return;
    }
    const QString url = fuzzyUrl(source, *g);
    if (url.isEmpty()) {
        finishJob(job, QString());
        return;
    }
    job.urls = {url};
    tryNext(job);
}

QStringList CoverManager::candidateUrls(const Game& g) {
    QStringList urls;
    const QString sys = g.system();
    const QString id = g.gameId;
    if (sys == QLatin1String("pc")) // jeux PC : l'écran titre du jeu sert d'image
        return urls;
    if ((sys == QLatin1String("gc") || sys == QLatin1String("wii")) && id.size() == 6) {
        for (const auto& r : regionOrder(id))
            urls << QStringLiteral("https://art.gametdb.com/wii/coverfullHQ/%1/%2.png#cropfront").arg(r, id);
        for (const auto& r : regionOrder(id).mid(0, 2))
            urls << QStringLiteral("https://art.gametdb.com/wii/cover/%1/%2.png").arg(r, id);
    } else if (sys == QLatin1String("nds") && id.size() == 4) {
        for (const auto& r : regionOrder(id))
            urls << QStringLiteral("https://art.gametdb.com/ds/coverM/%1/%2.jpg").arg(r, id);
    } else if (sys == QLatin1String("3ds") && id.size() == 4) {
        for (const auto& r : regionOrder(id))
            urls << QStringLiteral("https://art.gametdb.com/3ds/coverM/%1/%2.jpg").arg(r, id);
    } else if (sys == QLatin1String("wiiu") && id.size() == 6) {
        for (const auto& r : regionOrder(id))
            urls << QStringLiteral("https://art.gametdb.com/wiiu/coverM/%1/%2.jpg").arg(r, id);
    }

    if (sys == QLatin1String("switch") || sys == QLatin1String("switch2")) {
        QString name = g.displayTitle();
        static const QRegularExpression s2(QStringLiteral("(?i)\\s*[-–:]?\\s*(nintendo\\s+)?switch\\s*2(\\s+edition)?\\s*"));
        name.remove(s2);
        const QString slug = edenSlug(name.trimmed());
        if (!slug.isEmpty())
            urls << QStringLiteral("https://raw.githubusercontent.com/eden-emulator/boxart/refs/heads/master/img/%1.png").arg(slug);
    }

    const QString repo = libretroRepo(sys);
    if (!repo.isEmpty()) {
        const QString base = QFileInfo(g.path).completeBaseName();
        const QString clean = RomDetector::cleanTitle(QFileInfo(g.path).fileName());
        QStringList names{base};
        for (const QString& suffix : {QStringLiteral(" (USA)"), QStringLiteral(" (Europe)"), QStringLiteral(" (World)"), QStringLiteral(" (Japan)")})
            names << clean + suffix;
        if (g.displayTitle() != clean)
            names << g.displayTitle() + QStringLiteral(" (USA)") << g.displayTitle() + QStringLiteral(" (Europe)");
        names.removeDuplicates();
        for (const auto& n : names)
            if (!n.trimmed().isEmpty())
                urls << libretroUrl(repo, n);
    }
    return urls;
}

void CoverManager::request(const Game& g, bool force) {
    if (m_inFlight.contains(g.path))
        return;
    if (!force && (g.coverTried || !g.coverPath.isEmpty()))
        return;
    Job job{g.path, g.system(), candidateUrls(g)};
    if (job.urls.isEmpty() && indexSourceFor(g.system()).owner.isEmpty()) {
        m_lib->update(g.path, [](Game& x) { x.coverTried = true; });
        return;
    }
    m_inFlight.insert(g.path);
    m_queue.enqueue(job);
    emit pendingChanged(pending());
    pump();
}

void CoverManager::requestMissing() {
    if (!Settings::instance().downloadCovers())
        return;
    // Nouvelle méthode de recherche : on retente une fois les jeux restés sans jaquette.
    constexpr int kMatcherVersion = 2;
    const bool retry = Settings::instance().value(QStringLiteral("covers/matcherVersion")).toInt() < kMatcherVersion;
    if (retry)
        Settings::instance().setValue(QStringLiteral("covers/matcherVersion"), kMatcherVersion);
    const QVector<Game> games = m_lib->games(); // copie : la bibliothèque peut changer pendant les requêtes
    for (const auto& g : games)
        if (!g.isAddon && !g.missing && g.coverPath.isEmpty() && (retry || !g.coverTried))
            request(g, retry);
}

void CoverManager::pump() {
    while (m_active < kMaxConcurrent && !m_queue.isEmpty()) {
        ++m_active;
        tryNext(m_queue.dequeue());
    }
}

void CoverManager::tryNext(Job job) {
    if (job.urls.isEmpty()) {
        if (!job.fuzzyDone) // les noms exacts n'ont rien donné : on cherche le titre le plus proche
            fuzzyStage(job);
        else
            finishJob(job, QString());
        return;
    }
    QString url = job.urls.takeFirst();
    const bool cropFront = url.endsWith(QLatin1String("#cropfront"));
    if (cropFront)
        url.chop(QStringLiteral("#cropfront").size());

    Net::instance().get(testable(url), this, [this, job, cropFront](bool ok, const QByteArray& data, const QString&) {
        if (ok) {
            QImage img = QImage::fromData(data);
            if (!img.isNull() && img.width() >= 48 && img.height() >= 48) {
                if (cropFront && img.width() > img.height()) {
                    const int fw = qMin(img.width(), int(img.height() * 160.0 / 224.0));
                    img = img.copy(img.width() - fw, 0, fw, img.height());
                }
                const QString dir = Paths::ensureDir(Paths::coversDir() + QLatin1Char('/') + job.system);
                const QString file = dir + QLatin1Char('/') + cacheName(job.gamePath) + QStringLiteral(".png");
                if (img.save(file, "PNG")) {
                    finishJob(job, file);
                    return;
                }
            }
        }
        tryNext(job);
    });
}

void CoverManager::finishJob(const Job& job, const QString& coverFile) {
    --m_active;
    m_inFlight.remove(job.gamePath);
    m_lib->update(job.gamePath, [&](Game& g) {
        g.coverTried = true;
        if (!coverFile.isEmpty())
            g.coverPath = coverFile;
    });
    if (!coverFile.isEmpty())
        emit coverFound(job.gamePath);
    emit pendingChanged(pending());
    pump();
}

bool CoverManager::setCustomCover(const QString& gamePath, const QString& imageFile, QString* error) {
    QImageReader reader(imageFile);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        if (error)
            *error = tr("Image illisible : %1").arg(reader.errorString());
        return false;
    }
    if (img.height() > 1200)
        img = img.scaledToHeight(1200, Qt::SmoothTransformation);
    const QString dir = Paths::ensureDir(Paths::coversDir() + QStringLiteral("/custom"));
    const QString file = dir + QLatin1Char('/') + cacheName(gamePath) + QStringLiteral(".png");
    if (!img.save(file, "PNG")) {
        if (error)
            *error = tr("Impossible d'enregistrer la jaquette.");
        return false;
    }
    m_lib->update(gamePath, [&](Game& g) {
        g.coverPath = file;
        g.coverTried = true;
    });
    emit coverFound(gamePath);
    return true;
}
