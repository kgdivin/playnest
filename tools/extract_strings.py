#!/usr/bin/env python3
"""Liste les textes de l'interface (appels tr(...) et textes des fichiers JSON) pour la traduction.
Usage : extract_strings.py <racine du projet> [fichier de traduction.json]
Affiche les textes absents du fichier de traduction, un JSON par ligne."""
import json
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
ESC = {"n": "\n", "t": "\t", '"': '"', "\\": "\\", "'": "'", "0": "\0"}


def literal(src, i):
    """Lit une suite de littéraux C++ adjacents à partir de src[i] == '"'. Retourne (texte, fin)."""
    out = []
    while i < len(src) and src[i] == '"':
        i += 1
        while src[i] != '"':
            if src[i] == "\\":
                i += 1
                out.append(ESC.get(src[i], src[i]))
            else:
                out.append(src[i])
            i += 1
        i += 1
        j = i
        while j < len(src) and src[j] in " \t\r\n":
            j += 1
        if j < len(src) and src[j] == '"':
            i = j
        else:
            break
    return "".join(out), i


def strings():
    found = {}
    pat = re.compile(r'\b(?:tr|translate)\(\s*(?:"[A-Za-z]*"\s*,\s*)?(?=")')
    for f in sorted(root.glob("src/**/*.cpp")) + sorted(root.glob("src/**/*.h")):
        src = f.read_text(encoding="utf-8")
        for m in pat.finditer(src):
            text, end = literal(src, m.end())
            plural = bool(re.match(r"\s*,\s*(nullptr|\"\")\s*,", src[end:end + 30]))
            found.setdefault(text, {"file": f.name, "plural": plural})
            if plural:
                found[text]["plural"] = True
    for name, keys in (("systems.json", ("requirements", "name")), ("emulators.json", ("notes", "label", "help"))):
        def walk(o):
            if isinstance(o, dict):
                for k, v in o.items():
                    if k in keys and isinstance(v, str) and v:
                        found.setdefault(v, {"file": name, "plural": False})
                    walk(v)
            elif isinstance(o, list):
                for v in o:
                    walk(v)
        walk(json.loads((root / "resources" / name).read_text(encoding="utf-8")))
    return found


if __name__ == "__main__":
    found = strings()
    done = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8")) if len(sys.argv) > 2 and Path(sys.argv[2]).exists() else {}
    missing = [(t, i) for t, i in found.items() if t not in done]
    for t, i in missing:
        print(json.dumps({"s": t, **({"n": 1} if i["plural"] else {})}, ensure_ascii=False))
    print(f"{len(found)} textes, {len(missing)} à traduire, {len([k for k in done if k not in found])} obsolètes", file=sys.stderr)
