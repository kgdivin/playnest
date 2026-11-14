#!/usr/bin/env python3
"""Essai de lancement, utilisé par le workflow GitHub sur Linux, Windows et macOS.
Lance Playnest sur une bibliothèque d'exemple, lui fait capturer sa fenêtre, puis vérifie la capture.
Usage : ci_smoke.py <programme Playnest> <capture.png>"""
import os
import pathlib
import subprocess
import sys
import tempfile

exe = os.path.abspath(sys.argv[1])
shot = os.path.abspath(sys.argv[2])
root = pathlib.Path(__file__).resolve().parent.parent
work = pathlib.Path(tempfile.mkdtemp(prefix="playnest-smoke-"))

# Bibliothèque d'exemple : de faux jeux pour chaque console.
samples = work / "samples"
subprocess.run([sys.executable, str(root / "tests" / "make_samples.py"), str(samples)], check=True, stdout=subprocess.DEVNULL)

# Dossier de données de Playnest sur ce système, avec des réglages qui passent l'écran de bienvenue.
if sys.platform == "win32":
    data = pathlib.Path(os.environ["APPDATA"]) / "Playnest" / "Playnest"
elif sys.platform == "darwin":
    data = pathlib.Path.home() / "Library" / "Application Support" / "Playnest" / "Playnest"
else:
    data = pathlib.Path(os.environ.get("XDG_DATA_HOME", pathlib.Path.home() / ".local" / "share")) / "Playnest" / "Playnest"
data.mkdir(parents=True, exist_ok=True)
(data / "playnest.ini").write_text(
    "[library]\nfolders=%s\n[app]\nfirstRunDone=true\nwelcomed=true\nautoUpdateCheck=false\n" % samples.as_posix(), encoding="utf-8")

env = dict(os.environ, PLAYNEST_SCREENSHOT=shot, PLAYNEST_SCREENSHOT_PAGE="0", PLAYNEST_SCREENSHOT_SIZE="1380x860",
           PLAYNEST_SCREENSHOT_DELAY="10000", PLAYNEST_LANG="en")
try:
    code = subprocess.run([exe], env=env, timeout=180).returncode
except subprocess.TimeoutExpired:
    code = "délai dépassé"

log = data / "playnest.log"
if log.exists():
    print("--- playnest.log")
    print("\n".join(log.read_text(encoding="utf-8", errors="replace").splitlines()[:25]))
size = os.path.getsize(shot) if os.path.exists(shot) else 0
print(f"--- code de sortie : {code}, capture : {size} octets")
if code != 0 or size < 30000:
    sys.exit("Playnest ne s'est pas lancé correctement.")
print("Playnest s'est lancé et a affiché sa fenêtre.")
