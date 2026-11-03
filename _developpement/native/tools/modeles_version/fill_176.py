# Traductions de la 1.0.76 (fenêtre « Créer des vidéos » élargie). Usage : python fill_176.py <ressources\langues>
import json, sys
from pathlib import Path

DUR = "Durée du Short"
HINT = "Cochez « Vrai clip » pour un clip animé : scénario, images par scène, paroles en sous-titres et Shorts."
T = {
    "en": ("Short length", "Tick “Real clip” for an animated clip: storyline, one image per scene, lyrics as subtitles and Shorts."),
    "es": ("Duración del Short", "Marque «Clip real» para un clip animado: guion, imágenes por escena, letra en subtítulos y Shorts."),
    "it": ("Durata dello Short", "Spunta «Vero clip» per un clip animato: sceneggiatura, immagini per scena, testi nei sottotitoli e Shorts."),
    "de": ("Länge des Shorts", "„Echter Clip“ ankreuzen für einen animierten Clip: Drehbuch, Bilder pro Szene, Songtext als Untertitel und Shorts."),
    "pt": ("Duração do Short", "Marque «Clipe real» para um clipe animado: roteiro, imagens por cena, letra em legendas e Shorts."),
    "ja": ("ショートの長さ", "アニメーションクリップには「本格クリップ」にチェック：シナリオ、シーンごとの画像、歌詞の字幕、ショート。"),
    "ko": ("쇼츠 길이", "애니메이션 클립을 만들려면 \"실제 클립\"을 체크하세요: 시나리오, 장면별 이미지, 가사 자막, 쇼츠."),
    "zh-Hans": ("短视频时长", "勾选「真正的片段」以制作动画片段：剧本、每个场景的图像、歌词字幕和短视频。"),
    "yue": ("短片長度", "剔選「真正片段」做動畫片段：劇本、每個場景嘅圖像、歌詞字幕同短片。"),
}
langues = Path(sys.argv[1])
for code, (dur, hint) in T.items():
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    d["strings"][DUR], d["strings"][HINT] = dur, hint
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")
