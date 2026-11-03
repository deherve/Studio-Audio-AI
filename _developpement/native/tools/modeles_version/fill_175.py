# Traductions de la 1.0.75 (durée du Short au choix), à lancer AVANT i18n_sync. Usage : python fill_175.py <ressources\langues>
import json, sys
from pathlib import Path

BOX = "Short (YouTube Shorts / TikTok, 9:16) centré sur le refrain, avec les images du clip"
REC = " (conseillé)"
TIP = "30 s : durée conseillée pour faire découvrir un titre (TikTok : 21 à 34 s ; YouTube Shorts accepte jusqu'à 3 min)."
T = {
    "en": ("Short (YouTube Shorts / TikTok, 9:16) centred on the chorus, with the clip's images", " (recommended)",
           "30 s: recommended length to introduce a track (TikTok: 21 to 34 s; YouTube Shorts accepts up to 3 min)."),
    "es": ("Short (YouTube Shorts / TikTok, 9:16) centrado en el estribillo, con las imágenes del clip", " (recomendado)",
           "30 s: duración recomendada para dar a conocer un tema (TikTok: 21 a 34 s; YouTube Shorts acepta hasta 3 min)."),
    "it": ("Short (YouTube Shorts / TikTok, 9:16) centrato sul ritornello, con le immagini del clip", " (consigliato)",
           "30 s: durata consigliata per far scoprire un brano (TikTok: da 21 a 34 s; YouTube Shorts accetta fino a 3 min)."),
    "de": ("Short (YouTube Shorts / TikTok, 9:16) um den Refrain, mit den Bildern des Clips", " (empfohlen)",
           "30 s: empfohlene Länge, um einen Titel bekannt zu machen (TikTok: 21 bis 34 s; YouTube Shorts erlaubt bis zu 3 Min.)."),
    "pt": ("Short (YouTube Shorts / TikTok, 9:16) centrado no refrão, com as imagens do clipe", " (recomendado)",
           "30 s: duração recomendada para dar a conhecer uma faixa (TikTok: 21 a 34 s; YouTube Shorts aceita até 3 min)."),
    "ja": ("サビを中心にしたショート（YouTube Shorts / TikTok、9:16）、クリップの画像を使用", "（推奨）",
           "30 秒：曲を知ってもらうための推奨の長さ（TikTok：21〜34 秒、YouTube Shorts は最大 3 分）。"),
    "ko": ("후렴 중심의 쇼츠(YouTube Shorts / TikTok, 9:16), 클립 이미지 사용", " (권장)",
           "30초: 곡을 알리기 위한 권장 길이 (TikTok: 21~34초, YouTube Shorts는 최대 3분)."),
    "zh-Hans": ("以副歌为中心的短视频（YouTube Shorts / TikTok，9:16），使用片段的图像", "（推荐）",
                "30 秒：推荐用于让人发现曲目的时长（TikTok：21 至 34 秒；YouTube Shorts 最长 3 分钟）。"),
    "yue": ("以副歌為中心嘅短片（YouTube Shorts / TikTok，9:16），用片段嘅圖像", "（建議）",
            "30 秒：建議用嚟介紹一首歌嘅長度（TikTok：21 至 34 秒；YouTube Shorts 最長 3 分鐘）。"),
}
langues = Path(sys.argv[1])
for code, (box, rec, tip) in T.items():
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    d["strings"][BOX], d["strings"][REC], d["strings"][TIP] = box, rec, tip
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")
