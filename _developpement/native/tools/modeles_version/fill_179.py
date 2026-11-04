# Traductions de la 1.0.79 (« Vidéos n/4 » : les Shorts comptent comme 4e format), à lancer AVANT i18n_sync.
# Usage : python fill_179.py <ressources\langues>
import json, sys
from pathlib import Path

PAIRS = [("Pochettes %d/3 · Vidéos %d/3", "Pochettes %d/3 · Vidéos %d/4"), ("Vidéos %d/3", "Vidéos %d/4")]
langues = Path(sys.argv[1])
for p in langues.glob("*.json"):
    d = json.loads(p.read_text(encoding="utf-8"))
    s = d.get("strings", {})
    for old, new in PAIRS:
        if old in s and s[old]:
            v = s[old]
            i = v.rfind("/3")  # le dernier « /3 » est celui des vidéos
            s[new] = v[:i] + "/4" + v[i + 2:] if i >= 0 else v
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")

print("OK")
