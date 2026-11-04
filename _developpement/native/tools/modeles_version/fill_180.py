# Traductions de la 1.0.80. Usage : python fill_180.py <ressources\langues>
import json, sys
from pathlib import Path

K = ["Refaire les pochettes",
     "Recrée la pochette 1:1 et les images YouTube 16:9 et TikTok 9:16 de l'album (après confirmation), avec le modèle d'images des Réglages et la description de l'album."]
T = {
    "en": ["Redo covers", "Recreates the album's 1:1 cover and its YouTube 16:9 and TikTok 9:16 images (after confirmation), with the image model from Settings and the album description."],
    "es": ["Rehacer portadas", "Vuelve a crear la portada 1:1 y las imágenes YouTube 16:9 y TikTok 9:16 del álbum (tras confirmar), con el modelo de imágenes de Ajustes y la descripción del álbum."],
    "it": ["Rifai le copertine", "Ricrea la copertina 1:1 e le immagini YouTube 16:9 e TikTok 9:16 dell'album (dopo conferma), con il modello di immagini delle Impostazioni e la descrizione dell'album."],
    "de": ["Cover neu erstellen", "Erstellt das 1:1-Cover und die Bilder YouTube 16:9 und TikTok 9:16 des Albums neu (nach Bestätigung), mit dem Bildmodell aus den Einstellungen und der Albumbeschreibung."],
    "pt": ["Refazer as capas", "Recria a capa 1:1 e as imagens YouTube 16:9 e TikTok 9:16 do álbum (após confirmação), com o modelo de imagens das Definições e a descrição do álbum."],
    "ja": ["ジャケットを作り直す", "アルバムの 1:1 ジャケットと YouTube 16:9・TikTok 9:16 の画像を作り直します（確認後）。設定の画像モデルとアルバムの説明を使います。"],
    "ko": ["커버 다시 만들기", "앨범의 1:1 커버와 YouTube 16:9, TikTok 9:16 이미지를 다시 만듭니다(확인 후). 설정의 이미지 모델과 앨범 설명을 사용합니다."],
    "zh-Hans": ["重新制作封面", "重新创建专辑的 1:1 封面以及 YouTube 16:9 和 TikTok 9:16 图像（确认后），使用设置中的图像模型和专辑描述。"],
    "yue": ["重新整封面", "重新整專輯嘅 1:1 封面同 YouTube 16:9、TikTok 9:16 圖像（確認之後），用設定入面嘅圖像模型同專輯描述。"],
}
langues = Path(sys.argv[1])
for code, vals in T.items():
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    for k, v in zip(K, vals):
        d["strings"][k] = v
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")
