# Traductions de la 1.0.100 (jeton carte graphique). Usage : python fill_1100.py <ressources\langues>
# Ordre des langues : en, es, it, de, pt, ja, ko, zh-Hans, yue. Ecrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "En attente de la carte graphique (%s)": [
  "Waiting for the graphics card (%s)",
  "Esperando la tarjeta gráfica (%s)",
  "In attesa della scheda grafica (%s)",
  "Warten auf die Grafikkarte (%s)",
  "À espera da placa gráfica (%s)",
  "グラフィックカードの空き待ち（%s）",
  "그래픽 카드 대기 중(%s)",
  "正在等待显卡（%s）",
  "等緊顯示卡（%s）"],
 "génération de musique": ["music generation", "generación de música", "generazione musicale", "Musikgenerierung", "geração de música",
                           "音楽の生成", "음악 생성", "音乐生成", "音樂生成"],
 "IA de texte locale": ["local text AI", "IA de texto local", "IA di testo locale", "lokale Text-KI", "IA de texto local",
                        "ローカルのテキストAI", "로컬 텍스트 AI", "本地文本 AI", "本機文字 AI"],
 "images locales": ["local images", "imágenes locales", "immagini locali", "lokale Bilder", "imagens locais",
                    "ローカル画像", "로컬 이미지", "本地图像", "本機圖像"],
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
