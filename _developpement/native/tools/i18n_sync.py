#!/usr/bin/env python3
"""Synchronise les fichiers de langue (dossier « langues ») avec les textes du code.

Les textes à traduire sont ceux qui sont écrits en français dans tr("..."), trf("..."), wtr("...") et N_("...")
des sources de native/src. Pour chaque fichier langues/<code>.json :
  - les clés nouvelles sont ajoutées (valeur vide = retombe sur l'anglais, puis sur le français) ;
  - les clés qui n'existent plus dans le code sont retirées ;
  - l'ordre est celui des sources.
Crée aussi langues/_modele.json (gabarit vide pour une nouvelle langue).
Vérifie que chaque traduction garde les mêmes marques de format (%d, %s, %.1f…) que le texte français.

Usage : python native/tools/i18n_sync.py [--check]
"""
import json, os, re, sys

sys.stdout.reconfigure(encoding="utf-8")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(ROOT, "native", "src")
LANG_DIR = os.path.join(ROOT, "..", "ressources", "langues")  # 1.0.45 : ROOT = music/_developpement, langues dans music/ressources
FILES = ["main.cpp", "studio.cpp", "setup.cpp", "media.cpp", "download.cpp", "suno.cpp", "player.cpp", "gpumem.cpp", "lora_catalog.hpp", "analysis.cpp", "ai_writer.cpp", "clip.cpp", "fiches.cpp"]

LIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
CALL = re.compile(r'(?<![A-Za-z0-9_])(?:tr|trf|wtr|N_)\(\s*(?=")')
ESC = {"n": "\n", "t": "\t", "\\": "\\", '"': '"', "'": "'"}


def unescape(s):
    out, i = [], 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s) and s[i + 1] in ESC:
            out.append(ESC[s[i + 1]])
            i += 2
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def extract_keys():
    keys = []
    seen = set()
    for f in FILES:
        text = open(os.path.join(SRC, f), encoding="utf-8").read()
        for m in CALL.finditer(text):
            pos = m.end()
            parts = []
            while True:
                lm = LIT.match(text, pos)
                if not lm:
                    break
                parts.append(lm.group(1))
                pos = lm.end()
                ws = re.compile(r"\s*").match(text, pos)
                nxt = ws.end()
                if nxt < len(text) and text[nxt] == '"':
                    pos = nxt
                else:
                    break
            key = unescape("".join(parts))
            if key and key not in seen:
                seen.add(key)
                keys.append(key)
    return keys


SPEC = re.compile(r"%(?:(\d+)\$)?[-+#0]*\d*(?:\.\d+)?(?:ll|l|z|h)?([diuxXfgGeEsc])")


def specs(s):
    """Types des arguments attendus, dans l'ordre des arguments (gère les marques positionnelles %1$d)."""
    s = s.replace("%%", "")
    found = [(m.group(1), m.group(2)) for m in SPEC.finditer(s)]
    if any(p for p, _ in found):
        return [c for _, c in sorted(found, key=lambda x: int(x[0] or 0))]
    return [c for _, c in found]


def main():
    check_only = "--check" in sys.argv
    keys = extract_keys()
    print(len(keys), "textes dans le code")
    os.makedirs(LANG_DIR, exist_ok=True)
    problems = 0
    meta_default = {"name": "", "script": "latin"}
    for fn in sorted(os.listdir(LANG_DIR)):
        if not fn.endswith(".json"):
            continue
        path = os.path.join(LANG_DIR, fn)
        data = json.load(open(path, encoding="utf-8"))
        old = data.get("strings", {})
        new = {}
        for k in keys:
            new[k] = old.get(k, "")
        removed = [k for k in old if k not in new]
        missing = [k for k, v in new.items() if not v]
        code = fn[:-5]
        for k, v in new.items():
            if v and specs(k) != specs(v):
                print(f"  ! {code}: marques de format différentes : {k!r} -> {v!r}")
                problems += 1
        if code not in ("fr", "_modele"):
            print(f"{code}: {len(keys) - len(missing)}/{len(keys)} traduits, {len(removed)} retirés")
        if not check_only:
            data["meta"] = data.get("meta", dict(meta_default))
            data["strings"] = new
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                json.dump(data, f, ensure_ascii=False, indent=1)
                f.write("\n")
    if not check_only:
        tpl = os.path.join(LANG_DIR, "_modele.json")
        data = {
            "meta": {"name": "Nom de la langue dans cette langue", "script": "latin  (ou ja, ko, zh-Hans, zh-Hant)"},
            "strings": {k: "" for k in keys},
        }
        with open(tpl, "w", encoding="utf-8", newline="\n") as f:
            json.dump(data, f, ensure_ascii=False, indent=1)
            f.write("\n")
    print("terminé" if not problems else f"{problems} problème(s) de format")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
