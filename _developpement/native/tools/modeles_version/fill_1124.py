# Traductions de la 1.1.024 (images de clip en 1024 × 576). Usage : python fill_1124.py <ressources\langues>
# Ordre : en, es, it, de, pt, ja, ko, zh-Hans, yue. Écrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "Images du clip en 1024 × 576 (rapide)": [
  "Clip images at 1024 × 576 (fast)",
  "Imágenes del clip en 1024 × 576 (rápido)",
  "Immagini del clip in 1024 × 576 (veloce)",
  "Clip-Bilder in 1024 × 576 (schnell)",
  "Imagens do clipe em 1024 × 576 (rápido)",
  "クリップの画像を1024 × 576にする（高速）",
  "클립 이미지를 1024 × 576으로(빠름)",
  "剪辑图像使用 1024 × 576（更快）",
  "剪輯圖像用 1024 × 576（較快）"],
 "Les images de scène sont créées en 1024 × 576 puis agrandies par le montage : environ 2 fois plus rapide qu'en 1344 × 768, un peu moins de détail (surtout dans les vidéos verticales). Ne change que les nouvelles images.": [
  "Scene images are created at 1024 × 576 and enlarged by the editing: about 2 times faster than 1344 × 768, with slightly less detail (especially in vertical videos). Only affects new images.",
  "Las imágenes de escena se crean en 1024 × 576 y el montaje las amplía: unas 2 veces más rápido que en 1344 × 768, con algo menos de detalle (sobre todo en los vídeos verticales). Solo afecta a las imágenes nuevas.",
  "Le immagini delle scene vengono create in 1024 × 576 e ingrandite dal montaggio: circa 2 volte più veloce rispetto a 1344 × 768, con un po' meno dettaglio (soprattutto nei video verticali). Cambia solo le nuove immagini.",
  "Die Szenenbilder werden in 1024 × 576 erzeugt und beim Schnitt vergrößert: etwa doppelt so schnell wie 1344 × 768, mit etwas weniger Details (vor allem in Hochformat-Videos). Betrifft nur neue Bilder.",
  "As imagens de cena são criadas em 1024 × 576 e ampliadas pela montagem: cerca de 2 vezes mais rápido do que em 1344 × 768, com um pouco menos de detalhe (sobretudo nos vídeos verticais). Só altera as imagens novas.",
  "シーン画像は1024 × 576で作成され、編集時に拡大されます。1344 × 768の約2倍の速さですが、ディテールはやや減ります（特に縦動画）。新しい画像にのみ反映されます。",
  "장면 이미지는 1024 × 576으로 만든 뒤 편집 과정에서 확대됩니다. 1344 × 768보다 약 2배 빠르지만 디테일이 조금 줄어듭니다(특히 세로 영상). 새 이미지에만 적용됩니다.",
  "场景图像以 1024 × 576 生成，再由剪辑放大：比 1344 × 768 快约 2 倍，细节略少（尤其是竖屏视频）。只影响新生成的图像。",
  "場景圖像以 1024 × 576 生成，再由剪輯放大：比 1344 × 768 快約 2 倍，細節略少（尤其係直屏影片）。只影響新生成嘅圖像。"],
}
langues = Path(sys.argv[1])
for i, code in enumerate(L):
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8-sig"))
    for k, v in T.items():
        assert len(v) == 9, k
        d["strings"][k] = v[i]
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print(len(T), "OK")
