# Traductions de la 1.1.009 (nouvel essai automatique). Usage : python fill_1109.py <ressources\langues>
# Ordre des langues : en, es, it, de, pt, ja, ko, zh-Hans, yue. Écrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "Mémoire de la carte graphique pleine : nouvel essai dans 20 s": [
  "Graphics card memory full: retrying in 20 s", "Memoria de la tarjeta gráfica llena: nuevo intento en 20 s",
  "Memoria della scheda grafica piena: nuovo tentativo tra 20 s", "Grafikspeicher voll: neuer Versuch in 20 s",
  "Memória da placa gráfica cheia: nova tentativa em 20 s", "グラフィックカードのメモリ不足：20 秒後に再試行します",
  "그래픽 카드 메모리 부족: 20초 후 다시 시도", "显卡内存已满：20 秒后重试", "顯示卡記憶體滿咗：20 秒後再試"],
}
langues = Path(sys.argv[1])
for i, code in enumerate(L):
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    for k, v in T.items():
        assert len(v) == 9, k
        d["strings"][k] = v[i]
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print(len(T), "OK")
