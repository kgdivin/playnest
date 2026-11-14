#!/usr/bin/env python3
"""Crée des fichiers de test SYNTHÉTIQUES (en-têtes uniquement, aucun contenu de jeu)
pour vérifier la détection de Playnest. Usage : make_samples.py <dossier>"""
import os
import struct
import sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)


def w(name, data):
    path = os.path.join(out, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def pad(b, n):
    return b + b"\0" * (n - len(b))


# --- N64 : z64 (big-endian), v64 (paires inversées), n64 (mots inversés)
hdr = bytearray(0x1000)
hdr[0:4] = bytes.fromhex("80371240")
hdr[0x20:0x34] = pad(b"OMNIA TEST 64", 20)
hdr[0x3B:0x3F] = b"NOTE"
z64 = bytes(hdr)
v64 = bytearray(z64)
for i in range(0, len(v64), 2):
    v64[i], v64[i + 1] = v64[i + 1], v64[i]
n64 = bytearray(z64)
for i in range(0, len(n64), 4):
    n64[i:i + 4] = n64[i:i + 4][::-1]
w("N64/Playnest Racer (USA).z64", z64)
w("N64/Playnest Racer (Europe).v64", bytes(v64))
w("N64/playnest_kart.n64", bytes(n64))
w("N64/not_a_rom.z64", b"\x00" * 0x100)  # doit être ignoré

# --- DS : en-tête + bannière avec titre français et icône
nds = bytearray(0x2000)
nds[0:12] = pad(b"OMNIATEST", 12)
nds[0x0C:0x10] = b"AOTF"
nds[0x12] = 0
struct.pack_into("<I", nds, 0x68, 0x1000)
struct.pack_into("<H", nds, 0x15C, 0xCF56)
banner = 0x1000
struct.pack_into("<H", nds, banner, 1)
for i in range(512):  # icône : dégradé de deux couleurs
    nds[banner + 0x20 + i] = 0x21 if (i // 32) % 2 else 0x12
pal = [0, 0x7C1F, 0x03FF] + [0x7FFF] * 13
for i, c in enumerate(pal):
    struct.pack_into("<H", nds, banner + 0x220 + i * 2, c)
for lang, title in enumerate(["テスト", "Playnest Adventure\nNintendo", "Aventure Playnest\nLe Retour\nNintendo"]):
    t = title.encode("utf-16-le")
    nds[banner + 0x240 + lang * 0x100: banner + 0x240 + lang * 0x100 + len(t)] = t
w("DS/1234 - Playnest Adventure (E)(Scene).nds", bytes(nds))

# --- 3DS : NCSD + NCCH
cci = bytearray(0x5000)
cci[0x100:0x104] = b"NCSD"
struct.pack_into("<I", cci, 0x120, 0x4000 // 0x200)
ncch = 0x4000
cci[ncch + 0x100:ncch + 0x104] = b"NCCH"
struct.pack_into("<Q", cci, ncch + 0x118, 0x0004000000123400)
cci[ncch + 0x150:ncch + 0x160] = pad(b"CTR-P-AOTP", 16)
w("3DS/Playnest 3D Land (Europe) (En,Fr,De).3ds", bytes(cci))
w("3DS/homebrew.3dsx", b"3DSX" + b"\0" * 60)

# --- GameCube ISO
gc = bytearray(0x1000)
gc[0:6] = b"GOTE01"
struct.pack_into(">I", gc, 0x1C, 0xC2339F3D)
gc[0x20:0x20 + 16] = b"Playnest Party Cube"
w("GameCube/game.iso", bytes(gc))

# --- Wii RVZ
rvz = bytearray(0x200)
rvz[0:4] = b"RVZ\x01"
struct.pack_into(">I", rvz, 0x48, 2)
d = 0x58
rvz[d:d + 6] = b"ROTP01"
struct.pack_into(">I", rvz, d + 0x18, 0x5D1C9EA3)
rvz[d + 0x20:d + 0x20 + 17] = b"Playnest Sports Club"
w("Wii/Playnest Sports Club.rvz", bytes(rvz))

# --- Wii WBFS
wbfs = bytearray(0x400)
wbfs[0:4] = b"WBFS"
wbfs[8] = 9
wbfs[512:518] = b"ROMP01"
struct.pack_into(">I", wbfs, 512 + 0x18, 0x5D1C9EA3)
wbfs[512 + 0x20:512 + 0x20 + 14] = b"Playnest Galaxies"
w("Wii/ROMP01.wbfs", bytes(wbfs))

# --- ISO d'une autre console : doit être ignorée
w("Autres/ps2_game.iso", b"\0" * 0x8000 + b"\x01CD001")

# --- Wii U : dossier décrypté
meta = """<?xml version="1.0" encoding="utf-8"?>
<menu type="complex" access="777">
  <title_id type="hexBinary" length="8">0005000010199900</title_id>
  <product_code type="string" length="32">WUP-P-AOTP</product_code>
  <company_code type="string" length="8">0001</company_code>
  <longname_en type="string" length="512">Playnest U
Deluxe</longname_en>
  <longname_fr type="string" length="512">Playnest U
Édition Deluxe</longname_fr>
</menu>"""
w("WiiU/Playnest U [0005000010199900]/meta/meta.xml", meta.encode("utf-8"))
w("WiiU/Playnest U [0005000010199900]/code/playnest.rpx", b"\x7fELF" + b"\0" * 60)
w("WiiU/Playnest U [0005000010199900]/content/data.bin", b"\0" * 16)
tga = bytearray(18)
tga[2] = 2
struct.pack_into("<HH", tga, 12, 128, 128)
tga[16] = 32
tga[17] = 8
px = bytearray()
for y in range(128):
    for x in range(128):
        px += bytes([x * 2, y * 2, 200, 255])
w("WiiU/Playnest U [0005000010199900]/meta/iconTex.tga", bytes(tga + px))
w("WiiU/Playnest Party (Europe).wua", b"\0" * 64)


# --- Switch : NSP (PFS0) avec cnmt.xml
def pfs0(files):
    names = b""
    offsets = []
    for n, _ in files:
        offsets.append(len(names))
        names += n.encode() + b"\0"
    while len(names) % 0x20:
        names += b"\0"
    head = b"PFS0" + struct.pack("<III", len(files), len(names), 0)
    entries = b""
    data = b""
    for (n, content), noff in zip(files, offsets):
        entries += struct.pack("<QQII", len(data), len(content), noff, 0)
        data += content
    return head + entries + names + data


cnmt = b"<?xml version=\"1.0\"?><ContentMeta><Type>Application</Type><Id>0x0100abcd00010000</Id></ContentMeta>"
w("Switch/Playnest Odyssey.nsp", pfs0([("0123456789abcdef0123456789abcdef.cnmt.xml", cnmt), ("aa.nca", b"\0" * 32)]))
w("Switch/Playnest Odyssey [0100ABCD00010800][v65536].nsp", pfs0([("bb.nca", b"\0" * 32)]))
tik = "0100abcd00011001" + "0000000000000000"
w("Switch/Playnest DLC.nsp", pfs0([(tik + ".tik", b"\0" * 32), ("cc.nca", b"\0" * 32)]))
xci = bytearray(0x200)
xci[0x100:0x104] = b"HEAD"
w("Switch/Playnest Cartridge [0100ABCD00020000].xci", bytes(xci))
w("Switch2/Playnest Kart World [0400C3F00006E000][v0].nsp", pfs0([("dd.nca", b"\0" * 32)]))

# --- ExeFS Switch 2 extrait (main + main.npdm)
npdm = bytearray(0x400)
npdm[0:4] = b"META"
struct.pack_into("<II", npdm, 0x70, 0x100, 0x40)
npdm[0x100:0x104] = b"ACI0"
struct.pack_into("<Q", npdm, 0x110, 0x04005B902517C000)
w("Switch2/Playnest Bananza/exefs/main.npdm", bytes(npdm))
w("Switch2/Playnest Bananza/exefs/main", b"NSO0" + b"\0" * 60)
w("Switch2/Playnest Bananza/romfs/x.bin", b"\0")

# --- Homebrew Switch (NRO + NACP)
nro = bytearray(0x200)
nro[0x10:0x14] = b"NRO0"
struct.pack_into("<I", nro, 0x18, 0x100)
aset = 0x100
nro[aset:aset + 4] = b"ASET"
nacp_off = 0x38
struct.pack_into("<QQQQ", nro, aset + 0x08, 0, 0, nacp_off, 0x4000)
nacp = bytearray(0x4000)
nacp[0:13] = b"Playnest Toolbox"
nro = bytes(nro[:aset + nacp_off]) + bytes(nacp)
w("Switch/homebrew/toolbox.nro", nro)

print("échantillons créés dans", out)


# --- Noms réels de la bibliothèque d'un utilisateur (contenus factices)
def nsp_tid(tid):
    return pfs0([(tid.lower() + "0" * 16 + ".tik", b"\0" * 32), ("ee.nca", b"\0" * 32)])


def nsp_plain():
    return pfs0([("ff.nca", b"\0" * 32)])


for name in [
    "Reel/switch game/Mario Kart 8 Deluxe [0100152000022000][USA][v0].xci",
    "Reel/switch game/[ Torrent911.com ] Pokemon Legends Arceus V1.0.2 Eur SuperXCi - CLC.xci",
    "Reel/switch game/Pokemon Sword [0100ABF008968000].xci",
    "Reel/switch game/Pokemon Violet [01008F6008C5E000][USA][v0].xci",
    "Reel/switch game/SAEKO Giantess Dating Sim [0100fa2024c10000][v0][Base].xci",
]:
    w(name, bytes(xci))
for name in [
    "Reel/switch game/Pokémon Legends Z-A [0100F43008C44000][v1.0.0][US][Base][eShop].nsp",
    "Reel/switch game/Pokemon Let’s Go Eevee [0100187003A36000][v0].nsp",
    "Reel/dlc switch/Mario Kart 8 Deluxe [Wave 1-6 DLC Unlocker][010015200002300x][USA][v1114112].nsp",
    "Reel/dlc switch/Pokémon Legends Z-A [0100F43008C44800][DLC][US].nsp",
    "Reel/dlc switch/Pokémon Sword [The Crown Tundra] [0100ABF008969002] [v0].nsp",
    "Reel/dlc switch/Pokémon Sword [The Isle of Armor] [0100ABF008969001] [v0].nsp",
    "Reel/dlc switch/Pokemon Violet [The Indigo Disk, The Teal Mask + New Uniform Set DLC][01008F6008C5F001][v0].nsp",
    "Reel/dlc switch/Pokmon Legends ZA [Mega Dimension Full DLC 2][0100F43008C45002][v0][US].nsp",
    "Reel/dlc switch/SAEKO Giantess Dating Sim Soundtrack [DLC].nsp",
    "Reel/updates switch games/Mario Kart 8 Deluxe [0100152000022800][v1507328][US][Up v4.0.1].nsp",
    "Reel/updates switch games/Pokemon Legends Arceus [01001F5010DFA800][v196608].nsp",
    "Reel/updates switch games/Pokémon Legends Z-A [0100F43008C44800][v393216][US][Update v2.0.2].nsp",
    "Reel/updates switch games/Pokemon Lets Go Eevee [0100187003A36800][v131072].nsp",
    "Reel/updates switch games/Pokémon Violet [01008F6008C5E800][US][v786432][Update v4.0.0].nsp",
    "Reel/updates switch games/SAEKO꞉ Giantess Dating Sim [0100FA2024C10800][v131072][UP v2.1.2].nsp",
]:
    w(name, nsp_plain())
# Le DLC « Unlocker » a un ID illisible dans son nom, mais son ticket contient le vrai.
w("Reel/dlc switch/Mario Kart 8 Deluxe [Wave 1-6 DLC Unlocker][010015200002300x][USA][v1114112].nsp",
  nsp_tid("0100152000023001"))


# --- Paquet NUP (Lichii) : jeu de base + mise à jour + 2 DLC, métadonnées TLV
def tlv(t, payload):
    return struct.pack("<HHI", t, 0, len(payload)) + payload


def localized(entries):
    out_b = b""
    for lang, text in entries:
        lb = lang.encode()
        tb = text.encode("utf-8")
        out_b += bytes([len(lb)]) + lb + struct.pack("<I", len(tb)) + tb
    return out_b


try:
    import io
    from PIL import Image
    buf = io.BytesIO()
    Image.new("RGB", (256, 256), (230, 120, 40)).save(buf, "JPEG")
    jpeg = buf.getvalue()
except Exception:
    jpeg = b""

meta = (tlv(5, "Playnest of Time".encode()) + tlv(6, b"Playnest Studio") + tlv(4, b"2.0.0")
        + (tlv(3, jpeg) if jpeg else b"")
        + tlv(9, localized([("AmericanEnglish", "Playnest of Time"), ("French", "Playnest du Temps")])))
packages = [(0x0400486024DEC000, 0, 0), (0x0400486024DEC800, 131072, 1),
            (0x0400486024DED001, 0, 2), (0x0400486024DED002, 0, 2)]
toc_off = 0x80
toc = struct.pack("<IIQQQQ", 0x01434F54, len(packages), 0, 0, 0, 0)
toc = toc + b"\0" * (0x30 - len(toc))
meta_off = toc_off + 0x30 + 0x40 * len(packages)
for i, (tid, ver, kind) in enumerate(packages):
    rec = bytearray(0x40)
    struct.pack_into("<Q", rec, 0, tid)
    struct.pack_into("<I", rec, 0x14, ver)
    rec[0x18] = kind
    if i == 0:
        struct.pack_into("<QI", rec, 0x20, meta_off, len(meta))
    toc += bytes(rec)
body = toc + meta
sb = bytearray(0x80)
sb[0:4] = b"NUP\x01"
struct.pack_into("<HHQQ", sb, 4, 0x80, 1, toc_off, len(body))
struct.pack_into("<QQII", sb, 0x38, toc_off + len(body), 0x80 + len(body), len(packages), 0)
w("Reel/switch 2/Playnest of Time [0400486024dec000][v131072].nup", bytes(sb) + body)


# --- « Nintendo Switch 2 Edition » en .nup : title ID 0100… du jeu Switch, mais programme Switch 2
def npdm(switch2, tid=0):
    acid_off = 0x100
    sig = 0x300 if switch2 else 0x200
    data = bytearray(acid_off + sig + 0x40)
    data[0:4] = b"META"
    struct.pack_into("<II", data, 0x78, acid_off, len(data) - acid_off)
    data[acid_off + sig:acid_off + sig + 4] = b"ACID"
    if tid:  # bloc ACI0 avec le title ID, comme dans un vrai dump
        aci = len(data)
        data += b"ACI0" + b"\0" * 12 + struct.pack("<Q", tid) + b"\0" * 0x28
        struct.pack_into("<II", data, 0x70, aci, 0x40)
    return bytes(data)


def make_nup(path, packages):
    # packages : (tid, version, kind, meta)
    toc_off = 0x80
    toc = struct.pack("<IIQQQQ", 0x01434F54, len(packages), 0, 0, 0, 0)
    toc += b"\0" * (0x30 - len(toc))
    metas = b""
    meta_base = toc_off + 0x30 + 0x40 * len(packages)
    for tid, ver, kind, meta in packages:
        rec = bytearray(0x40)
        struct.pack_into("<Q", rec, 0, tid)
        struct.pack_into("<I", rec, 0x14, ver)
        rec[0x18] = kind
        if meta:
            struct.pack_into("<QI", rec, 0x20, meta_base + len(metas), len(meta))
            metas += meta
        toc += bytes(rec)
    body = toc + metas
    sb = bytearray(0x80)
    sb[0:4] = b"NUP\x01"
    struct.pack_into("<HHQQ", sb, 4, 0x80, 1, toc_off, len(body))
    struct.pack_into("<QQII", sb, 0x38, toc_off + len(body), 0x80 + len(body), len(packages), 0)
    w(path, bytes(sb) + body)


make_nup("Reel/switch 2/Pokemon Legends Z-A Switch 2 Edition [0100F43008C44000].nup", [
    (0x0100F43008C44000, 0, 0, tlv(5, "Pokémon Legends: Z-A".encode()) + tlv(1, npdm(False))),
    (0x0100F43008C44800, 196608, 1, tlv(1, npdm(True)) + tlv(4, b"2.0.2")),
])


# --- Fan games PC (lancés sans émulateur)
def png(path, color):
    try:
        import io
        from PIL import Image
        buf = io.BytesIO()
        Image.new("RGB", (640, 480), color).save(buf, "PNG")
        w(path, buf.getvalue())
    except Exception:
        w(path, b"\x89PNG\r\n\x1a\n")


# RPG Maker XP / Pokémon Essentials : Game.ini en Windows-1252
w("PC/Pokemon Z (La Colere de Zygarde)/Game.exe", b"MZ" + b"\0" * 64)
w("PC/Pokemon Z (La Colere de Zygarde)/Game.ini", "[Game]\r\nLibrary=RGSS104E.dll\r\nScripts=Data\\Scripts.rxdata\r\nTitle=Pokémon Z : La Colère de Zygarde\r\n".encode("cp1252"))
w("PC/Pokemon Z (La Colere de Zygarde)/Game.rgssad", b"RGSSAD\0\1")
png("PC/Pokemon Z (La Colere de Zygarde)/Graphics/Titles/title.png", (20, 120, 60))
w("PC/Pokemon Z (La Colere de Zygarde)/Save1.rxdata", b"save")
# Pokémon SDK
w("PC/Pokemon Eclipse/Game.exe", b"MZ" + b"\0" * 64)
w("PC/Pokemon Eclipse/pokemonsdk/version.txt", b"6.0")
w("PC/Pokemon Eclipse/Data/project_identity.yml", b"title: Eclipse")
png("PC/Pokemon Eclipse/graphics/titles/title_bg.png", (80, 30, 120))
# RPG Maker MV (NW.js)
w("PC/Pokemon Verdant/Game.exe", b"MZ" + b"\0" * 64)
w("PC/Pokemon Verdant/package.json", '{"name": "rmmv-game", "main": "www/index.html", "window": {"title": "Pokémon Verdant", "width": 816}}'.encode())
w("PC/Pokemon Verdant/www/index.html", b"<html></html>")
# Jeu Unity : pas reconnu automatiquement (ajout à la main)
w("PC/Mon Jeu Unity/MonJeu.exe", b"MZ" + b"\0" * 64)
w("PC/Mon Jeu Unity/MonJeu_Data/globalgamemanagers", b"\0")


# --- Dump au format de Lichii (racine/base/exefs + racine/updates/<v>/exefs), noms comme chez l'utilisateur
mkw = "Downloads/Mario Kart World NS2 + Emulator/Mario Kart World + Emulator/MKW [ROM]"
w(mkw + "/base/exefs/main", b"\0" * 64)
w(mkw + "/base/exefs/main.npdm", npdm(True, 0x0400C3F00006E000))
w(mkw + "/base/romfs/data.bin", b"\0" * 16)
w(mkw + "/updates/131072/exefs/main", b"\0" * 64)
w(mkw + "/updates/131072/exefs/main.npdm", npdm(True, 0x0400C3F00006E800))

# --- Même dump sans nom ni mise à jour, tel que l'a laissé un ancien rangement : « Jeux/Switch 2/base »
old = "Jeux Nintendo/Jeux/Switch 2"
w(old + "/base/exefs/main", b"\0" * 64)
w(old + "/base/exefs/main.npdm", npdm(True, 0x040074A01BF12000))
w(old + "/base/romfs/data.bin", b"\0" * 16)
# Dump inconnu, seul dans son dossier de console : ni nom de dossier utile, ni title ID connu
w("Jeux Nintendo/Jeux/Switch/base/exefs/main", b"\0" * 64)
w("Jeux Nintendo/Jeux/Switch/base/exefs/main.npdm", npdm(False, 0x0100111111110000))


# --- .nup sans nom ni icône sur le jeu de base, mais avec un NACP et des icônes traduites sur la mise à jour
def nacp(names, version):
    d = bytearray(0x4000)
    for lang, n in names.items():
        b = n.encode()
        d[lang * 0x300:lang * 0x300 + len(b)] = b
    v = version.encode()
    d[0x3060:0x3060 + len(v)] = v
    return bytes(d)


icons = b""
try:
    import io
    from PIL import Image
    buf = io.BytesIO()
    Image.new("RGB", (256, 256), (200, 40, 40)).save(buf, "JPEG")
    icons = localized([("AmericanEnglish", buf.getvalue().decode("latin1")), ("French", buf.getvalue().decode("latin1"))]) if False else b""
    jp = buf.getvalue()
    for lang in ("AmericanEnglish", "French"):
        lb = lang.encode()
        icons += bytes([len(lb)]) + lb + struct.pack("<I", len(jp)) + jp
except Exception:
    pass
make_nup("Reel/switch 2/Playnest Legends S2E [0100F43008C44000].nup", [
    (0x0100F43008C44000, 0, 0, b""),
    (0x0100F43008C44800, 393216, 1, tlv(1, npdm(True)) + tlv(2, nacp({0: "Playnest Legends", 3: "Légendes Playnest"}, "2.0.2")) + (tlv(10, icons) if icons else b"")),
    (0x0100F43008C45001, 0, 2, b""),
])


# --- Consoles rétro : NES, Super Nintendo, Game Boy, Game Boy Color, Game Boy Advance
w("Retro/Playnest Bros (Europe).nes", b"NES\x1a" + b"\0" * 60)
w("Retro/Playnest World (USA).sfc", b"\0" * 0x8000)
gb = bytearray(0x150)
gb[0x134:0x13C] = b"PLAYNEST"
w("Retro/Playnest Land (World).gb", bytes(gb))
gbc = bytearray(0x150)
gbc[0x143] = 0xC0  # réservé à la Game Boy Color, bien que nommé .gb
w("Retro/Playnest Crystal (France).gb", bytes(gbc))
w("Retro/Playnest DX (USA, Europe).gbc", bytes(gb))
gba = bytearray(0xC0)
gba[0xA0:0xAC] = b"PLAYNESTADV\0"
gba[0xAC:0xB0] = b"APNF"
gba[0xB2] = 0x96
w("Retro/Playnest Emeraude - Hack FR (v1.2).gba", bytes(gba))
w("Retro/Playnest Emeraude - Hack FR (v1.2).sav", b"\0" * 32)
