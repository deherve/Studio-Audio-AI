# Traductions de la 1.0.70, à lancer AVANT i18n_sync (les anciennes clés servent de base). Usage : python fill_170.py <ressources\langues>
import json, re, sys
from pathlib import Path

OLD_LABEL = "Morceau long : 2 parties enchaînées"
NEW_LABEL = "Morceau long : 2 ou 3 parties enchaînées"
NEW_LONG = "Très longue (≈ 9-11 min, 3 parties)"
NEW_ZERO = "0 = aucun, %d au plus"
LABEL = {"en": "Long track: 2 or 3 chained parts", "es": "Pista larga: 2 o 3 partes encadenadas", "it": "Brano lungo: 2 o 3 parti concatenate", "de": "Langer Titel: 2 oder 3 verkettete Teile",
         "pt": "Faixa longa: 2 ou 3 partes encadeadas", "ja": "長い曲：2〜3パートを連結", "ko": "긴 곡: 2~3개 파트 연결", "zh-Hans": "长曲目：2 或 3 个部分连接", "yue": "長曲：2 或 3 個部分連接"}
ZERO = {"en": "0 = none, at most %d", "es": "0 = ninguno, %d como máximo", "it": "0 = nessuno, al massimo %d", "de": "0 = keine, höchstens %d", "pt": "0 = nenhum, no máximo %d",
        "ja": "0 = なし、最大 %d", "ko": "0 = 없음, 최대 %d", "zh-Hans": "0 = 无，最多 %d", "yue": "0 = 冇，最多 %d"}
langues = Path(sys.argv[1])
for code in LABEL:
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    s = d["strings"]
    old_long = next(k for k in s if k.startswith("Très longue (≈ 9-11 min"))
    s[NEW_LONG] = re.sub(r"2(?=[^0-9]*$)", "3", s[old_long])  # le dernier chiffre : nombre de parties
    s[NEW_LABEL] = LABEL[code]
    s[NEW_ZERO] = ZERO[code]
    s[" (partie 2)"] = s[" (partie 1)"].replace("1", "2")
    tip = next((k for k in s if OLD_LABEL in k and k != OLD_LABEL), None)
    if tip:
        s[tip.replace(OLD_LABEL, NEW_LABEL)] = s[tip].replace(s[OLD_LABEL], LABEL[code])
    print(code, s[NEW_LONG], "|", s[" (partie 2)"])
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")
