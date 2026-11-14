#include "RomDetector.h"

#include "Registry.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <QtEndian>

#include <algorithm>

namespace {

// ---------------------------------------------------------------------------
// Lecture binaire
// ---------------------------------------------------------------------------
class Reader {
public:
    explicit Reader(const QString& path) : m_file(path) { m_ok = m_file.open(QIODevice::ReadOnly); }
    bool ok() const { return m_ok; }
    qint64 size() const { return m_ok ? m_file.size() : 0; }
    QByteArray read(qint64 offset, qint64 len) {
        if (!m_ok || offset < 0 || len <= 0 || offset >= m_file.size())
            return {};
        if (!m_file.seek(offset))
            return {};
        return m_file.read(len);
    }

private:
    QFile m_file;
    bool m_ok = false;
};

quint32 be32(const QByteArray& b, int off) {
    if (off + 4 > b.size()) return 0;
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(b.constData() + off));
}
quint32 le32(const QByteArray& b, int off) {
    if (off + 4 > b.size()) return 0;
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(b.constData() + off));
}
quint16 le16(const QByteArray& b, int off) {
    if (off + 2 > b.size()) return 0;
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(b.constData() + off));
}
quint64 le64(const QByteArray& b, int off) {
    if (off + 8 > b.size()) return 0;
    return qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(b.constData() + off));
}
quint64 be64(const QByteArray& b, int off) {
    if (off + 8 > b.size()) return 0;
    return qFromBigEndian<quint64>(reinterpret_cast<const uchar*>(b.constData() + off));
}

// Chaîne ASCII imprimable (s'arrête au premier octet nul). Vide si caractères non ASCII.
QString asciiField(const QByteArray& b, int off, int len, bool allowEmpty = true) {
    if (off + len > b.size())
        return {};
    QString out;
    for (int i = 0; i < len; ++i) {
        const uchar c = static_cast<uchar>(b[off + i]);
        if (c == 0)
            break;
        if (c < 0x20 || c > 0x7E)
            return {};
        out += QChar(c);
    }
    out = out.trimmed();
    if (!allowEmpty && out.isEmpty())
        return {};
    return out;
}

bool isAlnumId(const QString& s) {
    static const QRegularExpression re(QStringLiteral("^[A-Z0-9]+$"));
    return re.match(s).hasMatch();
}

QString hex16(quint64 v) { return QStringLiteral("%1").arg(v, 16, 16, QLatin1Char('0')).toUpper(); }

DetectedRom baseRom(const QString& path, const QString& systemId) {
    DetectedRom r;
    r.path = QDir::toNativeSeparators(path);
    r.systemId = systemId;
    r.title = RomDetector::cleanTitle(QFileInfo(path).fileName(), &r.region);
    return r;
}

// ---------------------------------------------------------------------------
// NES, Super Nintendo, Game Boy, Game Boy Color, Game Boy Advance
// ---------------------------------------------------------------------------
QString regionFromLetter(char c) {
    switch (c) {
    case 'E': return QStringLiteral("US");
    case 'P': return QStringLiteral("EU");
    case 'J': return QStringLiteral("JP");
    case 'F': return QStringLiteral("FR");
    case 'D': return QStringLiteral("DE");
    case 'S': return QStringLiteral("ES");
    case 'I': return QStringLiteral("IT");
    default: return {};
    }
}

// Le titre vient du nom du fichier (les en-têtes de ces consoles sont abrégés et en majuscules).
// L'en-tête sert à distinguer Game Boy et Game Boy Color, et à lire le code d'un jeu GBA.
std::optional<DetectedRom> detectRetro(const QString& path, const QString& systemId) {
    DetectedRom r = baseRom(path, systemId);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray d = f.read(0x150);
    if ((systemId == QLatin1String("gb") || systemId == QLatin1String("gbc")) && d.size() >= 0x150) {
        const uchar cgb = uchar(d[0x143]);
        if (cgb == 0xC0)
            r.systemId = QStringLiteral("gbc"); // jeu réservé à la Game Boy Color, même nommé .gb
    } else if (systemId == QLatin1String("gba") && d.size() >= 0xC0 && uchar(d[0xB2]) == 0x96) {
        const QByteArray code = d.mid(0xAC, 4);
        bool ok = true;
        for (const char c : code)
            ok &= (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (ok) {
            r.gameId = QString::fromLatin1(code);
            if (r.region.isEmpty())
                r.region = regionFromLetter(code[3]);
        }
        r.internalTitle = QString::fromLatin1(d.mid(0xA0, 12)).trimmed();
    }
    return r;
}

// ---------------------------------------------------------------------------
// Nintendo 64
// ---------------------------------------------------------------------------
std::optional<DetectedRom> detectN64(const QString& path, const QString& ext) {
    DetectedRom r = baseRom(path, QStringLiteral("n64"));
    if (ext == QLatin1String("ndd"))
        return r; // disque 64DD : pas d'en-tête fiable
    Reader rd(path);
    QByteArray h = rd.read(0, 0x40);
    if (h.size() < 0x40)
        return std::nullopt;
    const quint32 magic = be32(h, 0);
    if (magic == 0x37804012u) { // .v64 : octets permutés par paires
        for (int i = 0; i + 1 < h.size(); i += 2)
            std::swap(h[i], h[i + 1]);
    } else if (magic == 0x40123780u) { // .n64 : petit-boutiste par mots de 32 bits
        for (int i = 0; i + 3 < h.size(); i += 4) {
            std::swap(h[i], h[i + 3]);
            std::swap(h[i + 1], h[i + 2]);
        }
    } else if ((magic & 0xFFFF0000u) != 0x80370000u) {
        return std::nullopt;
    }
    r.internalTitle = asciiField(h, 0x20, 20);
    const QString code = asciiField(h, 0x3B, 4);
    if (code.size() == 4 && isAlnumId(code)) {
        r.gameId = code;
        if (r.region.isEmpty())
            r.region = RomDetector::regionFromIdChar(code.at(3));
    }
    if (r.title.isEmpty())
        r.title = r.internalTitle;
    return r;
}

// ---------------------------------------------------------------------------
// Nintendo DS
// ---------------------------------------------------------------------------
QImage decodeNdsIcon(const QByteArray& banner) {
    if (banner.size() < 0x240)
        return {};
    QRgb pal[16];
    for (int i = 0; i < 16; ++i) {
        const quint16 c = le16(banner, 0x220 + i * 2);
        const int r = (c & 0x1F) << 3, g = ((c >> 5) & 0x1F) << 3, b = ((c >> 10) & 0x1F) << 3;
        pal[i] = qRgba(r | (r >> 5), g | (g >> 5), b | (b >> 5), i == 0 ? 0 : 255);
    }
    QImage img(32, 32, QImage::Format_ARGB32);
    for (int tile = 0; tile < 16; ++tile) {
        const int tx = (tile % 4) * 8, ty = (tile / 4) * 8;
        for (int i = 0; i < 32; ++i) {
            const uchar byte = static_cast<uchar>(banner[0x20 + tile * 32 + i]);
            const int px = (i * 2) % 8, py = (i * 2) / 8;
            img.setPixel(tx + px, ty + py, pal[byte & 0x0F]);
            img.setPixel(tx + px + 1, ty + py, pal[byte >> 4]);
        }
    }
    return img;
}

QString ndsBannerTitle(const QByteArray& banner, int lang) {
    const int off = 0x240 + lang * 0x100;
    if (banner.size() < off + 0x100)
        return {};
    QString s = QString::fromUtf16(reinterpret_cast<const char16_t*>(banner.constData() + off), 128);
    const int nul = s.indexOf(QChar(0));
    if (nul >= 0)
        s.truncate(nul);
    QStringList lines = s.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (auto& l : lines)
        l = l.trimmed();
    if (lines.isEmpty())
        return {};
    // La dernière ligne est l'éditeur quand il y a 2 lignes ou plus.
    if (lines.size() >= 3)
        return lines.mid(0, lines.size() - 1).join(QLatin1Char(' '));
    return lines.first();
}

std::optional<DetectedRom> detectNds(const QString& path) {
    Reader rd(path);
    const QByteArray h = rd.read(0, 0x200);
    if (h.size() < 0x160)
        return std::nullopt;
    if (le16(h, 0x15C) != 0xCF56) // CRC du logo Nintendo : présent sur toute ROM DS valide
        return std::nullopt;
    DetectedRom r = baseRom(path, QStringLiteral("nds"));
    r.internalTitle = asciiField(h, 0x00, 12);
    const QString code = asciiField(h, 0x0C, 4);
    if (code.size() == 4 && isAlnumId(code)) {
        r.gameId = code;
        if (r.region.isEmpty())
            r.region = RomDetector::regionFromIdChar(code.at(3));
    }
    const quint32 bannerOff = le32(h, 0x68);
    if (bannerOff > 0 && bannerOff < rd.size()) {
        const QByteArray banner = rd.read(bannerOff, 0x840);
        r.icon = decodeNdsIcon(banner);
        QString t = ndsBannerTitle(banner, 2); // français
        if (t.isEmpty())
            t = ndsBannerTitle(banner, 1);     // anglais
        if (!t.isEmpty())
            r.title = t;
    }
    if (r.title.isEmpty())
        r.title = r.internalTitle;
    return r;
}

// ---------------------------------------------------------------------------
// Nintendo 3DS
// ---------------------------------------------------------------------------
void readNcch(Reader& rd, qint64 off, DetectedRom& r) {
    const QByteArray n = rd.read(off, 0x200);
    if (n.size() < 0x200 || n.mid(0x100, 4) != "NCCH")
        return;
    const quint64 programId = le64(n, 0x118);
    if (programId)
        r.titleId = hex16(programId);
    const QString product = asciiField(n, 0x150, 16);
    if (product.size() >= 10 && (product.startsWith(QLatin1String("CTR-")) || product.startsWith(QLatin1String("KTR-")))) {
        const QString id4 = product.mid(6, 4);
        if (isAlnumId(id4)) {
            r.gameId = id4;
            if (r.region.isEmpty())
                r.region = RomDetector::regionFromIdChar(id4.at(3));
        }
    }
}

void mark3dsAddon(DetectedRom& r) {
    if (r.titleId.size() == 16) {
        const QString high = r.titleId.left(8);
        if (high == QLatin1String("0004000E") || high == QLatin1String("0004008C")) {
            r.isAddon = true;
            r.addonKind = high == QLatin1String("0004000E") ? QStringLiteral("update") : QStringLiteral("dlc");
            r.baseTitleId = QStringLiteral("00040000") + r.titleId.mid(8); // même jeu, catégorie « application »
        }
    }
}

std::optional<DetectedRom> detect3ds(const QString& path, const QString& ext) {
    DetectedRom r = baseRom(path, QStringLiteral("3ds"));
    Reader rd(path);
    if (!rd.ok())
        return std::nullopt;
    if (ext == QLatin1String("3ds") || ext == QLatin1String("cci")) {
        const QByteArray h = rd.read(0, 0x200);
        if (h.mid(0x100, 4) == "NCSD") {
            const quint32 part0 = le32(h, 0x120);
            readNcch(rd, qint64(part0) * 0x200, r);
        } else if (h.mid(0x100, 4) == "NCCH") {
            readNcch(rd, 0, r);
        } else {
            return std::nullopt;
        }
    } else if (ext == QLatin1String("cxi")) {
        readNcch(rd, 0, r);
    } else if (ext == QLatin1String("cia")) {
        const QByteArray h = rd.read(0, 0x20);
        if (h.size() < 0x20)
            return std::nullopt;
        auto align64 = [](quint64 v) { return (v + 63) & ~quint64(63); };
        const quint64 hdr = le32(h, 0x00), cert = le32(h, 0x08), tik = le32(h, 0x0C), tmd = le32(h, 0x10);
        if (hdr != 0x2020)
            return std::nullopt;
        const quint64 tmdOff = align64(hdr) + align64(cert) + align64(tik);
        const quint64 contentOff = tmdOff + align64(tmd);
        readNcch(rd, qint64(contentOff), r);
        if (r.titleId.isEmpty()) {
            const QByteArray t = rd.read(qint64(tmdOff), 0x400);
            const quint32 sigType = be32(t, 0);
            int hdrOff = -1;
            if (sigType == 0x10004 || sigType == 0x10001) hdrOff = 4 + 0x100 + 0x3C;
            else if (sigType == 0x10003 || sigType == 0x10000) hdrOff = 4 + 0x200 + 0x3C;
            else if (sigType == 0x10005 || sigType == 0x10002) hdrOff = 4 + 0x3C + 0x40;
            if (hdrOff > 0) {
                const quint64 tid = be64(t, hdrOff + 0x4C);
                if (tid)
                    r.titleId = hex16(tid);
            }
        }
    }
    // .3dsx et formats compressés Azahar (z*) : nom de fichier uniquement.
    mark3dsAddon(r);
    return r;
}

// ---------------------------------------------------------------------------
// GameCube / Wii
// ---------------------------------------------------------------------------
// Analyse un en-tête de disque (0x80 octets). Retourne "gc", "wii" ou vide.
QString parseDiscHeader(const QByteArray& h, DetectedRom& r) {
    if (h.size() < 0x60)
        return {};
    QString sys;
    if (be32(h, 0x18) == 0x5D1C9EA3u)
        sys = QStringLiteral("wii");
    else if (be32(h, 0x1C) == 0xC2339F3Du)
        sys = QStringLiteral("gc");
    if (sys.isEmpty())
        return {};
    const QString id6 = asciiField(h, 0, 6);
    if (id6.size() == 6 && isAlnumId(id6)) {
        r.gameId = id6;
        r.region = RomDetector::regionFromIdChar(id6.at(3));
    }
    const QString t = asciiField(h, 0x20, 0x40);
    if (!t.isEmpty()) {
        r.internalTitle = t;
        r.title = t; // titre du disque : fiable et lisible
    }
    return sys;
}

std::optional<DetectedRom> detectDisc(const QString& path, const QString& ext) {
    DetectedRom r = baseRom(path, QString());
    Reader rd(path);
    if (!rd.ok())
        return std::nullopt;
    const QByteArray head = rd.read(0, 0x100);
    QString sys;

    if (head.startsWith("RVZ\x01") || head.startsWith("WIA\x01")) {
        const quint32 discType = be32(head, 0x48);
        sys = parseDiscHeader(head.mid(0x58, 0x80), r);
        if (sys.isEmpty())
            sys = discType == 2 ? QStringLiteral("wii") : discType == 1 ? QStringLiteral("gc") : QString();
    } else if (head.startsWith("WBFS")) {
        const int shift = static_cast<uchar>(head[8]);
        if (shift >= 9 && shift <= 16)
            parseDiscHeader(rd.read(qint64(1) << shift, 0x100), r);
        sys = QStringLiteral("wii");
    } else if (head.startsWith("CISO")) {
        sys = parseDiscHeader(rd.read(0x8000, 0x100), r);
    } else if (le32(head, 0) == 0xB10BC001u) { // GCZ
        sys = le32(head, 4) == 1 ? QStringLiteral("wii") : QStringLiteral("gc");
    } else if (be32(head, 0) == 0xAE0F38A2u) { // TGC
        sys = QStringLiteral("gc");
    } else {
        sys = parseDiscHeader(head, r);
    }

    if (ext == QLatin1String("wad")) {
        if (be32(head, 0) == 0x20)
            sys = QStringLiteral("wii");
    } else if (ext == QLatin1String("dol")) {
        sys = QStringLiteral("wii");
    }
    if (sys.isEmpty())
        return std::nullopt; // ISO d'une autre console, ou fichier inconnu
    r.systemId = sys;
    if (r.title.isEmpty())
        r.title = RomDetector::cleanTitle(QFileInfo(path).fileName());
    return r;
}

// ---------------------------------------------------------------------------
// Wii U
// ---------------------------------------------------------------------------
QImage decodeTga(const QByteArray& d) {
    if (d.size() < 18)
        return {};
    const int idLen = static_cast<uchar>(d[0]);
    const int type = static_cast<uchar>(d[2]);
    const int w = le16(d, 12), h = le16(d, 14);
    const int bpp = static_cast<uchar>(d[16]);
    const int desc = static_cast<uchar>(d[17]);
    if ((type != 2 && type != 10) || (bpp != 32 && bpp != 24) || w <= 0 || h <= 0 || w > 1024 || h > 1024)
        return {};
    const int bytes = bpp / 8;
    QImage img(w, h, QImage::Format_ARGB32);
    int pos = 18 + idLen;
    const bool topDown = desc & 0x20;
    int pixel = 0;
    const int total = w * h;
    auto put = [&](int idx, const char* p) {
        const int x = idx % w, y = idx / w;
        const int yy = topDown ? y : (h - 1 - y);
        const int a = bytes == 4 ? static_cast<uchar>(p[3]) : 255;
        img.setPixel(x, yy, qRgba(static_cast<uchar>(p[2]), static_cast<uchar>(p[1]), static_cast<uchar>(p[0]), a));
    };
    if (type == 2) {
        if (d.size() < pos + total * bytes)
            return {};
        for (; pixel < total; ++pixel, pos += bytes)
            put(pixel, d.constData() + pos);
    } else { // RLE
        while (pixel < total && pos < d.size()) {
            const int hdr = static_cast<uchar>(d[pos++]);
            const int count = (hdr & 0x7F) + 1;
            if (hdr & 0x80) {
                if (pos + bytes > d.size()) break;
                for (int i = 0; i < count && pixel < total; ++i)
                    put(pixel++, d.constData() + pos);
                pos += bytes;
            } else {
                for (int i = 0; i < count && pixel < total; ++i) {
                    if (pos + bytes > d.size()) break;
                    put(pixel++, d.constData() + pos);
                    pos += bytes;
                }
            }
        }
    }
    return img;
}

std::optional<DetectedRom> detectWiiUFolder(const QString& dir) {
    const QString metaPath = dir + QStringLiteral("/meta/meta.xml");
    if (!QFileInfo::exists(metaPath))
        return std::nullopt;
    QDir codeDir(dir + QStringLiteral("/code"));
    const QStringList rpx = codeDir.entryList({QStringLiteral("*.rpx")}, QDir::Files);
    if (rpx.isEmpty())
        return std::nullopt;

    DetectedRom r;
    r.systemId = QStringLiteral("wiiu");
    r.path = QDir::toNativeSeparators(codeDir.filePath(rpx.first()));

    QFile f(metaPath);
    QString longFr, longEn, product, company;
    if (f.open(QIODevice::ReadOnly)) {
        QXmlStreamReader xml(&f);
        while (!xml.atEnd()) {
            if (xml.readNext() != QXmlStreamReader::StartElement)
                continue;
            const auto name = xml.name();
            if (name == QLatin1String("longname_fr")) longFr = xml.readElementText();
            else if (name == QLatin1String("longname_en")) longEn = xml.readElementText();
            else if (name == QLatin1String("product_code")) product = xml.readElementText();
            else if (name == QLatin1String("company_code")) company = xml.readElementText();
            else if (name == QLatin1String("title_id")) r.titleId = xml.readElementText().toUpper();
        }
    }
    auto clean = [](QString s) { return s.replace(QLatin1Char('\n'), QLatin1Char(' ')).simplified(); };
    r.title = clean(!longFr.trimmed().isEmpty() ? longFr : longEn);
    r.internalTitle = clean(longEn);
    if (r.title.isEmpty())
        r.title = RomDetector::cleanTitle(QFileInfo(dir).fileName(), &r.region);
    if (product.size() >= 10 && company.size() >= 2) {
        const QString id6 = product.mid(6, 4) + company.right(2);
        if (isAlnumId(id6)) {
            r.gameId = id6;
            r.region = RomDetector::regionFromIdChar(id6.at(3));
        }
    }
    const QString high = r.titleId.left(8);
    if (high == QLatin1String("0005000E") || high == QLatin1String("0005000C")) {
        r.isAddon = true;
        r.addonKind = high == QLatin1String("0005000E") ? QStringLiteral("update") : QStringLiteral("dlc");
        r.baseTitleId = QStringLiteral("00050000") + r.titleId.mid(8);
    }

    QFile icon(dir + QStringLiteral("/meta/iconTex.tga"));
    if (icon.open(QIODevice::ReadOnly) && icon.size() < 1024 * 1024)
        r.icon = decodeTga(icon.readAll());
    return r;
}

// ---------------------------------------------------------------------------
// Switch / Switch 2
// ---------------------------------------------------------------------------
void applySwitchTitleId(DetectedRom& r, const QString& tid) {
    if (tid.size() != 16)
        return;
    r.titleId = tid.toUpper();
    r.systemId = RomDetector::switchSystemForTitleId(r.titleId);
    bool ok = false;
    const quint64 v = r.titleId.toULongLong(&ok, 16);
    if (!ok)
        return;
    const quint64 low = v & 0xFFF;
    if (low == 0x800) { // mise à jour : jeu de base = ID - 0x800
        r.isAddon = true;
        r.addonKind = QStringLiteral("update");
        r.baseTitleId = hex16(v - 0x800);
    } else if (low != 0) { // DLC : jeu de base = (ID & ~0xFFF) - 0x1000
        r.isAddon = true;
        r.addonKind = QStringLiteral("dlc");
        r.baseTitleId = hex16((v & ~quint64(0xFFF)) - 0x1000);
    } else {
        r.baseTitleId = r.titleId;
    }
}

void scanPfs0(Reader& rd, DetectedRom& r) {
    const QByteArray h = rd.read(0, 0x10);
    if (h.size() < 0x10 || !h.startsWith("PFS0"))
        return;
    const quint32 count = le32(h, 4), strSize = le32(h, 8);
    if (count == 0 || count > 4096 || strSize > 1024 * 1024)
        return;
    const QByteArray entries = rd.read(0x10, qint64(count) * 0x18);
    const QByteArray strings = rd.read(0x10 + qint64(count) * 0x18, strSize);
    if (entries.size() < int(count * 0x18))
        return;
    const qint64 dataStart = 0x10 + qint64(count) * 0x18 + strSize;
    static const QRegularExpression idRe(QStringLiteral("<Id>0x([0-9a-fA-F]{16})</Id>"));
    static const QRegularExpression typeRe(QStringLiteral("<Type>(\\w+)</Type>"));
    QString tikTid;
    for (quint32 i = 0; i < count; ++i) {
        const quint64 off = le64(entries, i * 0x18);
        const quint64 size = le64(entries, i * 0x18 + 8);
        const quint32 nameOff = le32(entries, i * 0x18 + 16);
        if (nameOff >= quint32(strings.size()))
            continue;
        const QString name = QString::fromLatin1(strings.constData() + nameOff);
        if (name.endsWith(QLatin1String(".cnmt.xml")) && size < 256 * 1024) {
            const QString xml = QString::fromUtf8(rd.read(dataStart + qint64(off), qint64(size)));
            const auto m = idRe.match(xml);
            if (m.hasMatch()) {
                applySwitchTitleId(r, m.captured(1));
                const auto t = typeRe.match(xml);
                if (t.hasMatch()) {
                    r.isAddon = t.captured(1) != QLatin1String("Application");
                    if (t.captured(1) == QLatin1String("Patch"))
                        r.addonKind = QStringLiteral("update");
                    else if (t.captured(1) == QLatin1String("AddOnContent"))
                        r.addonKind = QStringLiteral("dlc");
                }
                return;
            }
        } else if (name.endsWith(QLatin1String(".tik")) && name.size() >= 36 && tikTid.isEmpty()) {
            tikTid = name.left(16);
        }
    }
    if (!tikTid.isEmpty() && r.titleId.isEmpty())
        applySwitchTitleId(r, tikTid);
}

std::optional<DetectedRom> detectNro(const QString& path) {
    Reader rd(path);
    const QByteArray h = rd.read(0, 0x80);
    if (h.mid(0x10, 4) != "NRO0")
        return std::nullopt;
    DetectedRom r = baseRom(path, QStringLiteral("switch"));
    const quint32 nroSize = le32(h, 0x18);
    const QByteArray aset = rd.read(nroSize, 0x38);
    if (aset.startsWith("ASET")) {
        const quint64 iconOff = le64(aset, 0x08), iconSize = le64(aset, 0x10);
        const quint64 nacpOff = le64(aset, 0x18), nacpSize = le64(aset, 0x20);
        if (iconSize > 0 && iconSize < 512 * 1024)
            r.icon = QImage::fromData(rd.read(qint64(nroSize + iconOff), qint64(iconSize)));
        if (nacpSize >= 0x3000) {
            const QByteArray nacp = rd.read(qint64(nroSize + nacpOff), 0x3000);
            for (int lang : {3, 0, 1}) { // français, anglais US, anglais UK
                const QString name = QString::fromUtf8(nacp.mid(lang * 0x300, 0x200)).section(QChar(0), 0, 0).trimmed();
                if (!name.isEmpty()) {
                    r.title = name;
                    r.internalTitle = name;
                    break;
                }
            }
        }
    }
    return r;
}

// Programme Switch 2 ? Son NPDM « META » a une zone de signature ACID de 0x300 octets au lieu de 0x200 :
// l'en-tête « ACID » commence donc 0x100 octets plus loin (règle utilisée par Lichii).
bool npdmIsSwitch2(const QByteArray& npdm) {
    if (npdm.size() < 0x80 || !npdm.startsWith("META"))
        return false;
    const qint64 acid = le32(npdm, 0x78);
    if (npdm.mid(int(acid + 0x200), 4) == "ACID")
        return false;
    return npdm.mid(int(acid + 0x300), 4) == "ACID";
}

// Paquet NUP (format de Lichii) : jeu de base + mises à jour + DLC dans un seul fichier.
// Superbloc "NUP\x01" (0x80 octets), table des matières "TOC\x01", enregistrements de paquets
// de 0x40 octets, métadonnées TLV (nom, éditeur, icône, version affichée, noms traduits).
std::optional<DetectedRom> detectNup(const QString& path) {
    Reader rd(path);
    const QByteArray sb = rd.read(0, 0x80);
    if (sb.size() < 0x80 || !sb.startsWith(QByteArray("NUP\x01", 4)))
        return std::nullopt;
    DetectedRom r = baseRom(path, QStringLiteral("switch2"));
    static const QRegularExpression tidRe(QStringLiteral("[\\[(]([0-9A-Fa-f]{16})[\\])]"));
    if (const auto m = tidRe.match(QFileInfo(path).fileName()); m.hasMatch()) // repli si la table est illisible
        applySwitchTitleId(r, m.captured(1));
    const quint64 tocOff = le64(sb, 0x08);
    const quint32 pkgCount = le32(sb, 0x48);
    if (pkgCount == 0 || pkgCount > 4096 || tocOff < 0x80)
        return r;
    const QByteArray toc = rd.read(qint64(tocOff), 0x30 + qint64(pkgCount) * 0x40);
    if (toc.size() < int(0x30 + pkgCount * 0x40) || toc.left(4) != QByteArray("TOC\x01", 4))
        return r;
    struct Pkg { quint64 tid; quint32 version; int kind; quint64 metaOff; quint32 metaSize; };
    QVector<Pkg> pkgs;
    for (quint32 i = 0; i < pkgCount; ++i) {
        const int o = 0x30 + int(i) * 0x40;
        pkgs.push_back({le64(toc, o), le32(toc, o + 0x14), static_cast<uchar>(toc[o + 0x18]), le64(toc, o + 0x20), le32(toc, o + 0x28)});
    }
    int base = -1, updates = 0, dlcs = 0;
    quint32 bestUpdate = 0;
    for (int i = 0; i < pkgs.size(); ++i) {
        if (pkgs[i].kind == 0 && base < 0)
            base = i;
        else if (pkgs[i].kind == 1) {
            ++updates;
            bestUpdate = qMax(bestUpdate, pkgs[i].version);
        } else if (pkgs[i].kind == 2)
            ++dlcs;
    }
    const Pkg& main = pkgs[base >= 0 ? base : 0];
    applySwitchTitleId(r, hex16(base >= 0 ? main.tid : (main.tid & ~quint64(0xFFF))));
    if (base < 0) { // mise à jour seule
        r.isAddon = true;
        r.addonKind = QStringLiteral("update");
    }
    // Métadonnées TLV : nom, nom traduit, icône, version affichée, contrôle (NACP).
    // Elles sont normalement sur le jeu de base, mais on regarde aussi la mise à jour et les autres paquets.
    QString name, nameFr, displayVersion;
    QByteArray iconData;
    auto parseMeta = [&](const Pkg& p, bool isUpdate) {
        if (p.metaSize == 0 || p.metaSize >= 32 * 1024 * 1024)
            return;
        const QByteArray meta = rd.read(qint64(p.metaOff), p.metaSize);
        for (int pos = 0; pos + 8 <= meta.size();) {
            const quint16 type = le16(meta, pos);
            const quint32 size = le32(meta, pos + 4);
            pos += 8;
            if (pos + int(size) > meta.size())
                break;
            const QByteArray payload = meta.mid(pos, int(size));
            pos += int(size);
            if (type == 0x0003 && iconData.isEmpty()) {
                iconData = payload;
            } else if (type == 0x0004 && (isUpdate || displayVersion.isEmpty())) {
                displayVersion = QString::fromUtf8(payload).trimmed();
            } else if (type == 0x0005 && name.isEmpty()) {
                name = QString::fromUtf8(payload).trimmed();
            } else if (type == 0x0002 && payload.size() >= 0x3070) { // NACP brut
                auto entry = [&](int lang) { return QString::fromUtf8(payload.mid(lang * 0x300, 0x200).split('\0').value(0)).trimmed(); };
                if (nameFr.isEmpty())
                    nameFr = !entry(3).isEmpty() ? entry(3) : entry(9); // français, puis français canadien
                if (name.isEmpty())
                    name = !entry(0).isEmpty() ? entry(0) : entry(1);
                const QString dv = QString::fromUtf8(payload.mid(0x3060, 0x10).split('\0').value(0)).trimmed();
                if (!dv.isEmpty() && (isUpdate || displayVersion.isEmpty()))
                    displayVersion = dv;
            } else if (type == 0x0009 || type == 0x000A) { // [len][langue][u32 taille][données]…
                QByteArray firstIcon, frIcon, enIcon;
                int rp = 0;
                while (rp < payload.size()) {
                    const int ll = static_cast<uchar>(payload[rp++]);
                    if (rp + ll + 4 > payload.size())
                        break;
                    const QString lang = QString::fromUtf8(payload.mid(rp, ll)).toLower();
                    rp += ll;
                    const quint32 dl = le32(payload, rp);
                    rp += 4;
                    if (rp + int(dl) > payload.size())
                        break;
                    const QByteArray data = payload.mid(rp, int(dl));
                    rp += int(dl);
                    const bool fr = lang.startsWith(QLatin1String("fr")) || lang.contains(QLatin1String("french"));
                    const bool en = lang.startsWith(QLatin1String("en")) || lang.contains(QLatin1String("english"));
                    if (type == 0x0009) {
                        if (nameFr.isEmpty() && fr)
                            nameFr = QString::fromUtf8(data).trimmed();
                        if (name.isEmpty() && en)
                            name = QString::fromUtf8(data).trimmed();
                    } else {
                        if (firstIcon.isEmpty())
                            firstIcon = data;
                        if (frIcon.isEmpty() && fr)
                            frIcon = data;
                        if (enIcon.isEmpty() && en)
                            enIcon = data;
                    }
                }
                if (type == 0x000A && iconData.isEmpty())
                    iconData = !frIcon.isEmpty() ? frIcon : !enIcon.isEmpty() ? enIcon : firstIcon;
            }
        }
    };
    int newestUpdate = -1;
    for (int i = 0; i < pkgs.size(); ++i)
        if (pkgs[i].kind == 1 && (newestUpdate < 0 || pkgs[i].version > pkgs[newestUpdate].version))
            newestUpdate = i;
    parseMeta(main, false);
    if (newestUpdate >= 0)
        parseMeta(pkgs[newestUpdate], true);
    for (int i = 0; i < pkgs.size() && (name.isEmpty() || iconData.isEmpty()); ++i)
        if (&pkgs[i] != &main && i != newestUpdate)
            parseMeta(pkgs[i], false);
    if (!iconData.isEmpty())
        r.icon = QImage::fromData(iconData);
    if (!nameFr.isEmpty())
        r.title = nameFr;
    else if (!name.isEmpty())
        r.title = name;
    r.internalTitle = name;
    // Console : une « Nintendo Switch 2 Edition » garde le title ID 0100… du jeu Switch, mais son programme
    // (dans la mise à jour la plus récente, sinon dans le jeu de base) est un programme Switch 2.
    auto npdmOf = [&](const Pkg& p) -> QByteArray {
        if (p.metaSize == 0 || p.metaSize >= 32 * 1024 * 1024)
            return {};
        const QByteArray meta = rd.read(qint64(p.metaOff), p.metaSize);
        for (int pos = 0; pos + 8 <= meta.size();) {
            const quint16 type = le16(meta, pos);
            const quint32 size = le32(meta, pos + 4);
            pos += 8;
            if (pos + int(size) > meta.size())
                break;
            if (type == 0x0001)
                return meta.mid(pos, int(size));
            pos += int(size);
        }
        return {};
    };
    int bestUpd = -1;
    for (int i = 0; i < pkgs.size(); ++i)
        if (pkgs[i].kind == 1 && (bestUpd < 0 || pkgs[i].version > pkgs[bestUpd].version))
            bestUpd = i;
    QByteArray npdm = bestUpd >= 0 ? npdmOf(pkgs[bestUpd]) : QByteArray();
    if (npdm.isEmpty())
        npdm = npdmOf(main);
    static const QRegularExpression s2Name(QStringLiteral("(?i)switch\\s*2|\\bns2\\b"));
    if (npdmIsSwitch2(npdm) || s2Name.match(QFileInfo(path).fileName()).hasMatch())
        r.systemId = QStringLiteral("switch2");
    QStringList content;
    if (updates > 0)
        content << (displayVersion.isEmpty() ? QObject::tr("mise à jour n°%1 incluse").arg(bestUpdate >> 16)
                                             : QObject::tr("mise à jour %1 incluse").arg(displayVersion));
    else if (!displayVersion.isEmpty())
        content << QObject::tr("version %1").arg(displayVersion);
    if (dlcs > 0)
        content << QObject::tr("%n DLC inclus", nullptr, dlcs);
    if (updates > 0)
        r.bundledUpdate = displayVersion.isEmpty() ? QObject::tr("n°%1").arg(bestUpdate >> 16) : displayVersion;
    r.bundledDlc = dlcs;
    r.contentInfo = content.join(QStringLiteral(", "));
    return r;
}

// Étiquettes du nom de fichier : [DLC], [Update v4.0.0], [UP v2.1.2], [The Crown Tundra]…
void applyAddonTags(DetectedRom& r, const QString& fileName) {
    static const QRegularExpression bracket(QStringLiteral("[\\[(]([^\\])]+)[\\])]"));
    static const QRegularExpression updRe(QStringLiteral("(?i)^(update|upd|up)\\b\\s*v?\\s*(\\d+(?:\\.\\d+)*)?"));
    static const QRegularExpression dlcRe(QStringLiteral("(?i)\\bdlc\\b"));
    static const QRegularExpression noise(QStringLiteral("(?i)^(v?\\d+|[0-9a-f]{16}|[0-9a-fx]{16}|usa?|eur?|jpn?|us|eu|jp|fr|world|base|eshop|dlc|update|nsp|xci|nsz|\\s*)$"));
    QString dlcName, updVersion;
    bool isUpd = false, isDlc = false;
    for (auto it = bracket.globalMatch(fileName); it.hasNext();) {
        const QString tag = it.next().captured(1).trimmed();
        if (const auto m = updRe.match(tag); m.hasMatch()) {
            isUpd = true;
            if (!m.captured(2).isEmpty())
                updVersion = m.captured(2);
            continue;
        }
        if (dlcRe.match(tag).hasMatch())
            isDlc = true;
        if (dlcName.isEmpty() && !noise.match(tag).hasMatch() && !tag.contains(QLatin1String(".com")))
            dlcName = tag;
    }
    if (isUpd) {
        r.isAddon = true;
        r.addonKind = QStringLiteral("update");
    } else if (isDlc) {
        r.isAddon = true;
        r.addonKind = QStringLiteral("dlc");
    }
    if (!r.isAddon)
        return;
    if (r.addonKind == QLatin1String("update")) {
        if (updVersion.isEmpty()) { // [v786432] → 786432 / 65536 = 12
            static const QRegularExpression vNum(QStringLiteral("\\[v(\\d{5,})\\]"));
            if (const auto m = vNum.match(fileName); m.hasMatch())
                updVersion = QObject::tr("n°%1").arg(m.captured(1).toLongLong() / 65536);
        }
        r.addonName = updVersion.isEmpty() ? QObject::tr("Mise à jour") : QObject::tr("Mise à jour %1").arg(updVersion);
    } else {
        QString n = dlcName.simplified();
        static const QRegularExpression edgeDlc(QStringLiteral("(?i)^dlc\\s+|\\s+dlc$"));
        n.remove(edgeDlc);
        r.addonName = n.trimmed().isEmpty() ? QObject::tr("DLC") : n.trimmed();
    }
}

std::optional<DetectedRom> detectSwitch(const QString& path, const QString& ext) {
    if (ext == QLatin1String("nro"))
        return detectNro(path);
    if (ext == QLatin1String("nup"))
        return detectNup(path);
    DetectedRom r = baseRom(path, QStringLiteral("switch"));
    const QString fileName = QFileInfo(path).fileName();

    static const QRegularExpression tidRe(QStringLiteral("[\\[(]([0-9A-Fa-f]{16})[\\])]"));
    const auto m = tidRe.match(fileName);
    if (m.hasMatch())
        applySwitchTitleId(r, m.captured(1));

    Reader rd(path);
    if (!rd.ok())
        return std::nullopt;
    if (ext == QLatin1String("nsp") || ext == QLatin1String("nsz")) {
        if (rd.read(0, 4) != "PFS0")
            return std::nullopt;
        scanPfs0(rd, r);
    } else { // xci / xcz : en-tête "HEAD" à 0x100 (ou 0x1100 avec zone de clés)
        if (rd.read(0x100, 4) != "HEAD" && rd.read(0x1100, 4) != "HEAD")
            return std::nullopt;
    }
    applyAddonTags(r, fileName);
    if (r.isAddon && r.addonName.isEmpty())
        r.addonName = r.addonKind == QLatin1String("update") ? QObject::tr("Mise à jour") : QObject::tr("DLC");
    // Le programme d'un NSP/XCI est chiffré : pour une « Nintendo Switch 2 Edition », le nom sert d'indice.
    static const QRegularExpression s2(QStringLiteral("(?i)switch\\s*2|\\bns2\\b|\\bnsw2\\b"));
    if (!r.isAddon && s2.match(fileName).hasMatch())
        r.systemId = QStringLiteral("switch2");
    return r;
}

// Titre d'un jeu décompressé : le nom de son dossier, en remontant si ce nom n'apprend rien
// (« base », « MKW [ROM] », « Mario Kart World + Emulator » → « Mario Kart World »).
// Dossiers qui ne disent rien du jeu : ceux de « Ranger mes fichiers » (Jeux, Switch 2…) et ceux de Windows.
// Au-dessus d'eux, on ne trouvera plus le nom du jeu.
bool containerFolder(const QString& name) {
    static const QSet<QString> fixed = [] {
        QSet<QString> s{QStringLiteral("jeux"), QStringLiteral("jeux nintendo"), QStringLiteral("mises à jour"), QStringLiteral("dlc"),
                        QStringLiteral("émulateurs"), QStringLiteral("sauvegardes"), QStringLiteral("autres"), QStringLiteral("games"),
                        QStringLiteral("nintendo games"), QStringLiteral("emulators"), QStringLiteral("saves"), QStringLiteral("other"),
                        QStringLiteral("downloads"), QStringLiteral("téléchargements"), QStringLiteral("desktop"), QStringLiteral("bureau"),
                        QStringLiteral("documents"), QStringLiteral("users"), QStringLiteral("switch"), QStringLiteral("switch 2"),
                        QStringLiteral("nintendo switch"), QStringLiteral("nintendo switch 2"), QStringLiteral("wii u"), QStringLiteral("pc")};
        for (const SystemInfo& sys : Registry::instance().systems()) {
            s.insert(sys.name.toLower());
            s.insert(sys.shortName.toLower());
            if (sys.name.startsWith(QLatin1String("Nintendo ")))
                s.insert(sys.name.mid(9).toLower());
        }
        return s;
    }();
    return fixed.contains(name.toLower()) || name.size() <= 3 && name.endsWith(QLatin1Char(':'));
}

// Nom du jeu d'après les dossiers qui contiennent le dump ; vide si aucun ne le dit.
QString dumpTitle(QString dir, QString* region) {
    static const QRegularExpression noise(QStringLiteral("(?i)\\s*[+&]?\\s*\\b(emulator|emulateur|émulateur|rom|roms|dump|ns2|nsw2|switch\\s*2|base)\\b\\s*"));
    static const QRegularExpression shortCode(QStringLiteral("^[A-Z0-9]{2,5}$"));
    QString fallback;
    for (int i = 0; i < 4 && !dir.isEmpty(); ++i) {
        const QString name = QFileInfo(dir).fileName();
        if (containerFolder(name))
            break;
        QString t = RomDetector::cleanTitle(name, region);
        t.remove(noise);
        t = t.simplified();
        if (fallback.isEmpty() && !t.isEmpty())
            fallback = t;
        if (t.size() >= 4 && !shortCode.match(t).hasMatch())
            return t;
        const QString up = QFileInfo(dir).absolutePath();
        if (up == dir)
            break;
        dir = up;
    }
    return fallback;
}

std::optional<DetectedRom> detectExefsFolder(const QString& dir) {
    const QString npdmPath = dir + QStringLiteral("/exefs/main.npdm");
    const QString mainPath = dir + QStringLiteral("/exefs/main");
    if (!QFileInfo::exists(npdmPath) || !QFileInfo::exists(mainPath))
        return std::nullopt;
    // Format « racine/base/exefs » + « racine/updates/<version>/exefs » : les mises à jour ne sont pas des jeux.
    const QString parentName = QFileInfo(QFileInfo(dir).absolutePath()).fileName();
    if (parentName.compare(QLatin1String("updates"), Qt::CaseInsensitive) == 0 ||
        parentName.compare(QLatin1String("update"), Qt::CaseInsensitive) == 0)
        return std::nullopt;
    DetectedRom r;
    r.systemId = QStringLiteral("switch");
    r.path = QDir::toNativeSeparators(mainPath);
    const QString folderTitle = dumpTitle(dir, &r.region);
    const QString root = QFileInfo(dir).fileName().compare(QLatin1String("base"), Qt::CaseInsensitive) == 0 ? QFileInfo(dir).absolutePath() : dir;
    if (QFileInfo(root + QStringLiteral("/updates")).isDir()) {
        r.contentInfo = QObject::tr("mise à jour incluse");
        r.bundledUpdate = QObject::tr("incluse");
    }
    QFile f(npdmPath);
    if (f.open(QIODevice::ReadOnly) && f.size() < 1024 * 1024) {
        const QByteArray d = f.readAll();
        if (d.startsWith("META")) {
            const quint32 aciOff = le32(d, 0x70);
            if (d.mid(int(aciOff), 4) == "ACI0") {
                const quint64 pid = le64(d, int(aciOff) + 0x10);
                if (pid)
                    applySwitchTitleId(r, hex16(pid));
                r.isAddon = false;
            }
            if (npdmIsSwitch2(d))
                r.systemId = QStringLiteral("switch2");
        }
    }
    // Le nom vient du title ID quand le jeu est connu, sinon des dossiers (« Mario Kart World + Emulator »…).
    r.title = RomDetector::knownTitle(r.titleId);
    if (r.title.isEmpty())
        r.title = folderTitle;
    if (r.title.isEmpty()) {
        const SystemInfo* sys = Registry::instance().system(r.systemId);
        r.title = QObject::tr("Jeu %1").arg(sys ? sys->shortName : QStringLiteral("Switch")) +
                  (r.titleId.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(r.titleId));
    }
    return r;
}

} // namespace

namespace RomDetector {

QString regionFromIdChar(QChar c) {
    switch (c.toUpper().unicode()) {
    case 'E': return QStringLiteral("US");
    case 'J': return QStringLiteral("JP");
    case 'P': case 'X': case 'Y': case 'Z': case 'L': case 'M': case 'V': return QStringLiteral("EU");
    case 'F': return QStringLiteral("FR");
    case 'D': return QStringLiteral("DE");
    case 'S': return QStringLiteral("ES");
    case 'I': return QStringLiteral("IT");
    case 'H': return QStringLiteral("NL");
    case 'K': case 'Q': case 'T': return QStringLiteral("KO");
    case 'U': return QStringLiteral("AU");
    case 'W': return QStringLiteral("TW");
    case 'C': return QStringLiteral("CN");
    case 'A': return QStringLiteral("World");
    default: return {};
    }
}

QString switchSystemForTitleId(const QString& titleId) {
    if (titleId.startsWith(QLatin1String("04"), Qt::CaseInsensitive))
        return QStringLiteral("switch2");
    return QStringLiteral("switch");
}

QString knownTitle(const QString& titleId) {
    static const QHash<QString, QString> titles = [] {
        QHash<QString, QString> h;
        QFile f(QStringLiteral(":/data/switch2_titles.json"));
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            for (auto it = o.begin(); it != o.end(); ++it)
                if (it.key().size() == 16)
                    h.insert(it.key().toUpper(), it.value().toString());
        }
        return h;
    }();
    return titleId.size() == 16 ? titles.value(titleId.toUpper()) : QString();
}

QString cleanTitle(const QString& fileName, QString* regionOut) {
    QString s = fileName;
    // Retire les extensions connues (y compris doubles : .nkit.iso).
    static const QRegularExpression extRe(QStringLiteral("(\\.nkit)?\\.[A-Za-z0-9]{1,5}$"));
    s.remove(extRe);

    if (regionOut) {
        static const QRegularExpression regionRe(
            QStringLiteral("\\((USA|Europe|Japan|France|Germany|Spain|Italy|Korea|World|Australia|Canada|China|Taiwan|Netherlands|"
                           "U|E|J|F|G|S|I|K)(,[^)]*)?\\)"),
            QRegularExpression::CaseInsensitiveOption);
        const auto m = regionRe.match(s);
        if (m.hasMatch()) {
            static const QHash<QString, QString> map = {
                {"usa", "US"}, {"u", "US"}, {"europe", "EU"}, {"e", "EU"}, {"japan", "JP"}, {"j", "JP"},
                {"france", "FR"}, {"f", "FR"}, {"germany", "DE"}, {"g", "DE"}, {"spain", "ES"}, {"s", "ES"},
                {"italy", "IT"}, {"i", "IT"}, {"korea", "KO"}, {"k", "KO"}, {"world", "World"},
                {"australia", "AU"}, {"canada", "US"}, {"china", "CN"}, {"taiwan", "TW"}, {"netherlands", "NL"}};
            *regionOut = map.value(m.captured(1).toLower());
        } else { // noms Switch : [USA], [US], [EUR], [EU], [JPN], ou « Eur » dans le nom
            static const QRegularExpression tagRegion(QStringLiteral("(?i)(?:\\[|\\b)(USA?|EUR?|JPN?|JP|FR)(?:\\]|\\b)"));
            if (const auto t = tagRegion.match(s); t.hasMatch()) {
                const QString c = t.captured(1).toUpper();
                *regionOut = c.startsWith(QLatin1Char('U')) ? QStringLiteral("US")
                             : c.startsWith(QLatin1Char('E')) ? QStringLiteral("EU")
                             : c.startsWith(QLatin1Char('J')) ? QStringLiteral("JP")
                                                              : QStringLiteral("FR");
            }
        }
    }

    static const QRegularExpression tagsRe(QStringLiteral("\\s*[\\[(][^\\])]*[\\])]"));
    s.remove(tagsRe);
    static const QRegularExpression sceneNum(QStringLiteral("^\\d{3,5}\\s*-\\s*"));
    s.remove(sceneNum);
    // Restes de noms de releases : sites, versions, régions, formats, groupe en fin de nom.
    static const QRegularExpression site(QStringLiteral("(?i)\\b(www\\.)?[a-z0-9-]+\\.(com|net|org|to|cc|io|me|ws)\\b"));
    static const QRegularExpression ver(QStringLiteral("(?i)\\bv\\d+(\\.\\d+)+\\b"));
    static const QRegularExpression junk(QStringLiteral("(?i)\\b(eur|usa|jpn|pal|ntsc|multi\\d*|superxci|xci|nsp|nsz|eshop|repack)\\b"));
    static const QRegularExpression group(QStringLiteral("\\s+-\\s+[A-Z0-9]{2,6}$"));
    const bool release = site.match(s).hasMatch() || ver.match(s).hasMatch() || junk.match(s).hasMatch();
    s.remove(site);
    s.remove(ver);
    s.remove(junk);
    s = s.simplified();
    if (release) // « - CLC » en fin de nom : seulement s'il s'agit visiblement d'un nom de release
        s.remove(group);
    s.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!s.contains(QLatin1Char(' ')) && s.count(QLatin1Char('.')) >= 2)
        s.replace(QLatin1Char('.'), QLatin1Char(' '));
    s = s.simplified();
    while (s.endsWith(QLatin1Char('-')) || s.endsWith(QLatin1Char(',')))
        s = s.left(s.size() - 1).trimmed();
    return s;
}

std::optional<DetectedRom> detectFile(const QString& path) {
    const QFileInfo fi(path);
    const QString ext = fi.suffix().toLower();
    const QStringList systems = Registry::instance().systemsForExtension(ext);
    if (systems.isEmpty() || fi.size() == 0)
        return std::nullopt;

    std::optional<DetectedRom> r;
    if (systems.contains(QLatin1String("n64")))
        r = detectN64(path, ext);
    else if (systems.contains(QLatin1String("nds")))
        r = detectNds(path);
    else if (systems.contains(QLatin1String("3ds")))
        r = detect3ds(path, ext);
    else if (systems.contains(QLatin1String("gc")) || systems.contains(QLatin1String("wii")))
        r = detectDisc(path, ext);
    else if (systems.contains(QLatin1String("wiiu")))
        r = baseRom(path, QStringLiteral("wiiu"));
    else if (systems.contains(QLatin1String("switch")) || systems.contains(QLatin1String("switch2")))
        r = detectSwitch(path, ext);
    else if (systems.size() == 1 && QStringList{QStringLiteral("nes"), QStringLiteral("snes"), QStringLiteral("gb"), QStringLiteral("gbc"),
                                                QStringLiteral("gba")}.contains(systems.first()))
        r = detectRetro(path, systems.first());
    else // système ajouté par l'utilisateur via systems.json : détection par extension seule
        r = baseRom(path, systems.first());

    if (r && r->title.isEmpty())
        r->title = fi.completeBaseName();
    // Autres consoles : mises à jour et DLC reconnus aussi d'après le nom (« Update v1.2 », « [DLC] »).
    // (3DS et Wii U seulement : sur DS ou N64, « Patch » désigne plutôt une traduction ou un hack, qui reste un jeu.)
    if (r && (r->systemId == QLatin1String("3ds") || r->systemId == QLatin1String("wiiu"))) {
        const QString name = fi.completeBaseName();
        if (!r->isAddon || r->addonKind.isEmpty()) {
            static const QRegularExpression upd(QStringLiteral("(?i)\\b(?:update|mise à jour)\\b\\s*v?\\s*(\\d+(?:\\.\\d+)*)?"));
            static const QRegularExpression dlc(QStringLiteral("(?i)\\b(?:dlc|add-?on|addon)\\b"));
            if (const auto m = upd.match(name); m.hasMatch()) {
                r->isAddon = true;
                r->addonKind = QStringLiteral("update");
                if (r->addonName.isEmpty())
                    r->addonName = m.captured(1).isEmpty() ? QObject::tr("Mise à jour") : QObject::tr("Mise à jour %1").arg(m.captured(1));
            } else if (dlc.match(name).hasMatch() || r->isAddon) {
                r->isAddon = true;
                r->addonKind = QStringLiteral("dlc");
            }
        }
        if (r->isAddon && r->addonName.isEmpty()) {
            static const QRegularExpression ver(QStringLiteral("(?i)\\bv\\s*(\\d+(?:\\.\\d+)+)"));
            const auto m = ver.match(name);
            r->addonName = r->addonKind == QLatin1String("update")
                               ? (m.hasMatch() ? QObject::tr("Mise à jour %1").arg(m.captured(1)) : QObject::tr("Mise à jour"))
                               : QObject::tr("DLC");
        }
        if (r->isAddon) { // le titre ne garde que le nom du jeu : « Pokemon Ultra Sun »
            static const QRegularExpression cut(QStringLiteral("(?i)\\s*[-–:]?\\s*\\b(?:update|mise à jour|dlc)\\b.*$"));
            QString t = r->title;
            t.remove(cut);
            if (t.trimmed().size() >= 3)
                r->title = t.trimmed();
        }
    }
    return r;
}

std::optional<DetectedRom> detectDirectory(const QString& dir) {
    if (auto r = detectWiiUFolder(dir))
        return r;
    if (auto r = detectExefsFolder(dir))
        return r;
    if (auto r = detectPcGame(dir, true))
        return r;
    return std::nullopt;
}

namespace {
// Fichier ou dossier d'un nom donné, sans tenir compte de la casse (« Game.exe », « game.exe »…).
QString findCI(const QString& dir, const QString& name, bool wantDir = false) {
    const QString direct = dir + QLatin1Char('/') + name;
    const QFileInfo fi(direct);
    if (fi.exists() && fi.isDir() == wantDir)
        return direct;
    for (const QFileInfo& e : QDir(dir).entryInfoList((wantDir ? QDir::Dirs : QDir::Files) | QDir::NoDotAndDotDot))
        if (e.fileName().compare(name, Qt::CaseInsensitive) == 0)
            return e.absoluteFilePath();
    return {};
}

QString decodeText(const QByteArray& raw) {
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString s = utf8(raw);
    return utf8.hasError() ? QString::fromLatin1(raw) : s; // vieux Game.ini en Windows-1252
}

bool genericTitle(const QString& t) {
    static const QRegularExpression re(QStringLiteral("(?i)^(project\\d*|game|rpg ?maker.*|pok[eé]mon essentials.*|pokemon sdk|psdk|untitled)$"));
    return t.trimmed().isEmpty() || re.match(t.trimmed()).hasMatch();
}
} // namespace

std::optional<DetectedRom> detectPcGame(const QString& exeOrDir, bool strict) {
    const QFileInfo in(exeOrDir);
    QString dir, exe;
    if (in.isDir()) {
        dir = in.absoluteFilePath();
        exe = findCI(dir, QStringLiteral("Game.exe"));
        if (exe.isEmpty() && !strict) { // un seul .exe dans le dossier : c'est le jeu
            const auto exes = QDir(dir).entryInfoList({QStringLiteral("*.exe"), QStringLiteral("*.EXE")}, QDir::Files);
            if (exes.size() == 1)
                exe = exes.first().absoluteFilePath();
        }
    } else if (in.isFile() && in.suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) == 0) {
        exe = in.absoluteFilePath();
        dir = in.absolutePath();
    }
    if (exe.isEmpty())
        return std::nullopt;

    // Moteur.
    QString engine;
    const QString ini = findCI(dir, QStringLiteral("Game.ini"));
    const QString data = findCI(dir, QStringLiteral("Data"), true);
    const bool rgssArchive = !findCI(dir, QStringLiteral("Game.rgssad")).isEmpty() || !findCI(dir, QStringLiteral("Game.rgss2a")).isEmpty() ||
                             !findCI(dir, QStringLiteral("Game.rgss3a")).isEmpty();
    const bool rgssScripts = !data.isEmpty() && (!findCI(data, QStringLiteral("Scripts.rxdata")).isEmpty() ||
                                                 !findCI(data, QStringLiteral("Scripts.rvdata")).isEmpty() ||
                                                 !findCI(data, QStringLiteral("Scripts.rvdata2")).isEmpty());
    const QString pkg = findCI(dir, QStringLiteral("package.json"));
    const bool nwjs = !pkg.isEmpty() && (!findCI(dir, QStringLiteral("www"), true).isEmpty() ||
                                         !findCI(dir + QStringLiteral("/js"), QStringLiteral("rmmz_core.js")).isEmpty() ||
                                         !findCI(dir + QStringLiteral("/js"), QStringLiteral("rpg_core.js")).isEmpty());
    if (!findCI(dir, QStringLiteral("pokemonsdk"), true).isEmpty() ||
        (!data.isEmpty() && !findCI(dir, QStringLiteral("graphics"), true).isEmpty() && !findCI(dir, QStringLiteral("lib"), true).isEmpty()))
        engine = QStringLiteral("Pokémon SDK");
    else if (!ini.isEmpty() && (rgssArchive || rgssScripts))
        engine = QStringLiteral("RPG Maker");
    else if (nwjs)
        engine = QStringLiteral("RPG Maker MV/MZ");
    if (strict && engine.isEmpty())
        return std::nullopt;

    DetectedRom r;
    r.systemId = QStringLiteral("pc");
    r.path = QDir::toNativeSeparators(exe);
    r.internalTitle = engine;
    const QFileInfo efi(exe);
    r.fileSize = efi.size();
    r.modified = efi.lastModified().toSecsSinceEpoch();

    // Titre : Game.ini, package.json, sinon le nom du dossier (ou de l'exécutable).
    QString title;
    if (!ini.isEmpty()) {
        QFile f(ini);
        if (f.open(QIODevice::ReadOnly) && f.size() < 64 * 1024)
            for (const QString& line : decodeText(f.readAll()).split(QLatin1Char('\n')))
                if (line.trimmed().startsWith(QLatin1String("Title="), Qt::CaseInsensitive)) {
                    title = line.trimmed().mid(6).trimmed();
                    break;
                }
    }
    if (title.isEmpty() && !pkg.isEmpty()) {
        QFile f(pkg);
        if (f.open(QIODevice::ReadOnly) && f.size() < 256 * 1024) {
            static const QRegularExpression wt(QStringLiteral("\"title\"\\s*:\\s*\"([^\"]+)\""));
            if (const auto m = wt.match(QString::fromUtf8(f.readAll())); m.hasMatch())
                title = m.captured(1).trimmed();
        }
    }
    const QString folderName = QFileInfo(dir).fileName();
    if (genericTitle(title))
        title = RomDetector::cleanTitle(folderName, &r.region);
    if (genericTitle(title) || title.compare(QLatin1String("Game"), Qt::CaseInsensitive) == 0)
        title = efi.completeBaseName();
    r.title = title;

    // Image : l'écran titre du jeu (Graphics/Titles, img/titles1…).
    for (const QString& sub : {QStringLiteral("Graphics/Titles"), QStringLiteral("graphics/titles"), QStringLiteral("www/img/titles1"),
                               QStringLiteral("img/titles1"), QStringLiteral("graphics/interface/title")}) {
        const QString d = dir + QLatin1Char('/') + sub;
        if (!QFileInfo(d).isDir())
            continue;
        QStringList imgs = QDir(d).entryList({QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.PNG"), QStringLiteral("*.JPG")},
                                             QDir::Files, QDir::Name);
        std::stable_sort(imgs.begin(), imgs.end(), [](const QString& a, const QString& b) { // « title… » d'abord
            return a.startsWith(QLatin1String("title"), Qt::CaseInsensitive) && !b.startsWith(QLatin1String("title"), Qt::CaseInsensitive);
        });
        if (!imgs.isEmpty()) {
            r.iconFile = QDir::toNativeSeparators(d + QLatin1Char('/') + imgs.first());
            break;
        }
    }
    return r;
}

} // namespace RomDetector
