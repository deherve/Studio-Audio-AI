# Traductions de la 1.0.63. DonnÃ©es : fill_163_data.py.
import json, re, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from fill_163_data import KEYS, T

langues = Path(sys.argv[1])

for code, values in T.items():
    if len(values) != len(KEYS):
        sys.exit(f"{code} : {len(values)} traductions pour {len(KEYS)} textes")
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    s = d["strings"]
    for k, v in zip(KEYS, values):
        if k not in s:
            sys.exit(f"{code} : clÃ© absente : {k[:50]}")
        if re.findall(r"%[-0-9.]*[sdfu%]", k) != re.findall(r"%[-0-9.]*[sdfu%]", v):
            sys.exit(f"{code} : formats %s/%d diffÃ©rents ou dans un autre ordre : {k[:40]}")
        s[k] = v
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")






