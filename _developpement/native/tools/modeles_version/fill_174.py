# Traductions de la 1.0.74 (Short 30 s). Usage : python fill_174.py <ressources\langues>
import json, sys
from pathlib import Path

BOX = "Short 30 s (YouTube Shorts / TikTok, 9:16) centré sur le refrain, avec les images du clip"
NOTE = "Short « %s » : de %s à %s."
T = {
    "en": ("30 s Short (YouTube Shorts / TikTok, 9:16) centred on the chorus, with the clip's images", "Short \"%s\": from %s to %s."),
    "es": ("Short de 30 s (YouTube Shorts / TikTok, 9:16) centrado en el estribillo, con las imágenes del clip", "Short «%s»: de %s a %s."),
    "it": ("Short di 30 s (YouTube Shorts / TikTok, 9:16) centrato sul ritornello, con le immagini del clip", "Short «%s»: da %s a %s."),
    "de": ("30-s-Short (YouTube Shorts / TikTok, 9:16) um den Refrain, mit den Bildern des Clips", "Short „%s“: von %s bis %s."),
    "pt": ("Short de 30 s (YouTube Shorts / TikTok, 9:16) centrado no refrão, com as imagens do clipe", "Short «%s»: de %s a %s."),
    "ja": ("サビを中心にした30秒のショート（YouTube Shorts / TikTok、9:16）、クリップの画像を使用", "ショート「%s」：%s から %s まで。"),
    "ko": ("후렴 중심의 30초 쇼츠(YouTube Shorts / TikTok, 9:16), 클립 이미지 사용", "쇼츠 \"%s\": %s부터 %s까지."),
    "zh-Hans": ("以副歌为中心的 30 秒短视频（YouTube Shorts / TikTok，9:16），使用片段的图像", "短视频「%s」：从 %s 到 %s。"),
    "yue": ("以副歌為中心嘅 30 秒短片（YouTube Shorts / TikTok，9:16），用片段嘅圖像", "短片「%s」：由 %s 到 %s。"),
}
langues = Path(sys.argv[1])
for code, (box, note) in T.items():
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    d["strings"][BOX] = box
    d["strings"][NOTE] = note
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")
