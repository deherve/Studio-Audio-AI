# Traductions de la 1.0.78. Usage : python fill_178.py <ressources\langues>
import json, sys
from pathlib import Path

K = ["Pochettes",
     "Crée les images manquantes de l'album (%d sur 3) avec le modèle d'images des Réglages.",
     "Générer",
     "Crée cette image avec le modèle d'images des Réglages, d'après la description de l'album.",
     "Vider les champs",
     "Efface influence, thème, ambiance, titre, souhaits, groupe, titre de l'album et styles à éviter.",
     "Short : créé",
     "Short : pas encore créé"]
T = {
    "en": ["Covers", "Creates the album's missing images (%d of 3) with the image model chosen in Settings.", "Generate",
           "Creates this image with the image model chosen in Settings, from the album description.", "Clear fields",
           "Clears influence, theme, mood, title, wishes, band, album title and styles to avoid.", "Short: created", "Short: not created yet"],
    "es": ["Portadas", "Crea las imágenes que faltan del álbum (%d de 3) con el modelo de imágenes de Ajustes.", "Generar",
           "Crea esta imagen con el modelo de imágenes de Ajustes, a partir de la descripción del álbum.", "Vaciar los campos",
           "Borra influencia, tema, ambiente, título, deseos, grupo, título del álbum y estilos a evitar.", "Short: creado", "Short: aún no creado"],
    "it": ["Copertine", "Crea le immagini mancanti dell'album (%d su 3) con il modello di immagini delle Impostazioni.", "Genera",
           "Crea questa immagine con il modello di immagini delle Impostazioni, dalla descrizione dell'album.", "Svuota i campi",
           "Cancella influenza, tema, atmosfera, titolo, desideri, gruppo, titolo dell'album e stili da evitare.", "Short: creato", "Short: non ancora creato"],
    "de": ["Cover", "Erstellt die fehlenden Bilder des Albums (%d von 3) mit dem Bildmodell aus den Einstellungen.", "Erzeugen",
           "Erstellt dieses Bild mit dem Bildmodell aus den Einstellungen, nach der Albumbeschreibung.", "Felder leeren",
           "Löscht Einfluss, Thema, Stimmung, Titel, Wünsche, Band, Albumtitel und zu vermeidende Stile.", "Short: erstellt", "Short: noch nicht erstellt"],
    "pt": ["Capas", "Cria as imagens em falta do álbum (%d de 3) com o modelo de imagens das Definições.", "Gerar",
           "Cria esta imagem com o modelo de imagens das Definições, a partir da descrição do álbum.", "Limpar os campos",
           "Apaga influência, tema, ambiente, título, desejos, banda, título do álbum e estilos a evitar.", "Short: criado", "Short: ainda não criado"],
    "ja": ["ジャケット", "アルバムの足りない画像（3 枚中 %d 枚）を設定の画像モデルで作成します。", "生成",
           "アルバムの説明から、設定の画像モデルでこの画像を作成します。", "入力欄をクリア",
           "影響、テーマ、雰囲気、タイトル、要望、バンド、アルバム名、避けるスタイルを消去します。", "ショート：作成済み", "ショート：未作成"],
    "ko": ["커버", "앨범에 없는 이미지(3개 중 %d개)를 설정의 이미지 모델로 만듭니다.", "생성",
           "앨범 설명을 바탕으로 설정의 이미지 모델로 이 이미지를 만듭니다.", "입력란 비우기",
           "영향, 테마, 분위기, 제목, 요청, 밴드, 앨범 제목, 피할 스타일을 지웁니다.", "쇼츠: 만들어짐", "쇼츠: 아직 없음"],
    "zh-Hans": ["封面", "用设置中的图像模型创建专辑缺少的图像（3 张中的 %d 张）。", "生成",
                "根据专辑描述，用设置中的图像模型创建此图像。", "清空字段",
                "清除影响、主题、氛围、标题、愿望、乐队、专辑名和要避免的风格。", "短视频：已创建", "短视频：尚未创建"],
    "yue": ["封面", "用設定入面嘅圖像模型整專輯缺少嘅圖像（3 張中 %d 張）。", "生成",
            "根據專輯描述，用設定入面嘅圖像模型整呢張圖。", "清空欄位",
            "清除影響、主題、氣氛、標題、願望、樂隊、專輯名同要避免嘅風格。", "短片：已整好", "短片：未整"],
}
langues = Path(sys.argv[1])
for code, vals in T.items():
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    for k, v in zip(K, vals):
        if k == "Générer" and k in d["strings"]:
            continue  # texte déjà traduit ailleurs dans le Studio
        d["strings"][k] = v
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")
