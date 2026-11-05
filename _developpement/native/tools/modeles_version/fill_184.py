# Traductions de la 1.0.84 (licences des modèles). Usage : python fill_184.py <ressources\langues>
# Un texte déjà traduit ailleurs (Musique, LoRA, Licence…) garde sa traduction.
import json, sys
from pathlib import Path

K = ["Licences des modèles",
     "La commercialisation des morceaux, pochettes et vidéos créés avec Studio Audio AI est soumise aux licences des modèles utilisés. Référez-vous à leurs licences :",
     "Musique", "Reprises et partitions", "Analyse audio", "LoRA", "licence propre à chaque LoRA (voir sa page)", "Transcription des paroles",
     "Pochettes et clips", "IA de texte", "Usage", "Modèles", "Licence", "non commercial",
     "Les textes complets sont sur la page de chaque modèle (Hugging Face) ; ceux des programmes livrés sont dans le dossier « LICENSES ».",
     "Ouvrir le dossier LICENSES"]
T = {
    "en": ["Model licences", "Selling the tracks, covers and videos made with Studio Audio AI is subject to the licences of the models used. Refer to their licences:",
           "Music", "Covers and scores", "Audio analysis", "LoRA", "each LoRA has its own licence (see its page)", "Lyrics transcription",
           "Covers and clips", "Text AI", "Use", "Models", "Licence", "non-commercial",
           "The full texts are on each model's page (Hugging Face); those of the bundled programs are in the “LICENSES” folder.", "Open the LICENSES folder"],
    "es": ["Licencias de los modelos", "La comercialización de los temas, portadas y vídeos creados con Studio Audio AI está sujeta a las licencias de los modelos utilizados. Consulte sus licencias:",
           "Música", "Versiones y partituras", "Análisis de audio", "LoRA", "cada LoRA tiene su propia licencia (ver su página)", "Transcripción de la letra",
           "Portadas y clips", "IA de texto", "Uso", "Modelos", "Licencia", "no comercial",
           "Los textos completos están en la página de cada modelo (Hugging Face); los de los programas incluidos están en la carpeta «LICENSES».", "Abrir la carpeta LICENSES"],
    "it": ["Licenze dei modelli", "La commercializzazione di brani, copertine e video creati con Studio Audio AI è soggetta alle licenze dei modelli utilizzati. Fate riferimento alle loro licenze:",
           "Musica", "Cover e partiture", "Analisi audio", "LoRA", "ogni LoRA ha la propria licenza (vedi la sua pagina)", "Trascrizione del testo",
           "Copertine e clip", "IA di testo", "Uso", "Modelli", "Licenza", "non commerciale",
           "I testi completi sono sulla pagina di ogni modello (Hugging Face); quelli dei programmi inclusi sono nella cartella «LICENSES».", "Apri la cartella LICENSES"],
    "de": ["Lizenzen der Modelle", "Die Vermarktung der mit Studio Audio AI erstellten Titel, Cover und Videos unterliegt den Lizenzen der verwendeten Modelle. Beachten Sie deren Lizenzen:",
           "Musik", "Coverversionen und Noten", "Audioanalyse", "LoRA", "jedes LoRA hat seine eigene Lizenz (siehe seine Seite)", "Transkription der Texte",
           "Cover und Clips", "Text-KI", "Verwendung", "Modelle", "Lizenz", "nicht kommerziell",
           "Die vollständigen Texte stehen auf der Seite jedes Modells (Hugging Face); die der mitgelieferten Programme im Ordner „LICENSES“.", "Ordner LICENSES öffnen"],
    "pt": ["Licenças dos modelos", "A comercialização das faixas, capas e vídeos criados com o Studio Audio AI está sujeita às licenças dos modelos utilizados. Consulte as suas licenças:",
           "Música", "Versões e partituras", "Análise de áudio", "LoRA", "cada LoRA tem a sua própria licença (ver a sua página)", "Transcrição da letra",
           "Capas e clipes", "IA de texto", "Uso", "Modelos", "Licença", "não comercial",
           "Os textos completos estão na página de cada modelo (Hugging Face); os dos programas incluídos estão na pasta «LICENSES».", "Abrir a pasta LICENSES"],
    "ja": ["モデルのライセンス", "Studio Audio AI で作成した曲、ジャケット、動画の商用利用は、使用したモデルのライセンスに従います。各ライセンスを確認してください：",
           "音楽", "カバーと楽譜", "音声解析", "LoRA", "LoRA ごとに独自のライセンス（各ページを参照）", "歌詞の文字起こし",
           "ジャケットとクリップ", "テキスト AI", "用途", "モデル", "ライセンス", "非商用",
           "全文は各モデルのページ（Hugging Face）にあります。同梱プログラムのライセンスは「LICENSES」フォルダにあります。", "LICENSES フォルダを開く"],
    "ko": ["모델 라이선스", "Studio Audio AI로 만든 곡, 커버, 동영상의 상업적 이용은 사용한 모델의 라이선스를 따릅니다. 각 라이선스를 확인하세요:",
           "음악", "커버와 악보", "오디오 분석", "LoRA", "LoRA마다 고유한 라이선스(해당 페이지 참조)", "가사 받아쓰기",
           "커버와 클립", "텍스트 AI", "용도", "모델", "라이선스", "비상업용",
           "전문은 각 모델 페이지(Hugging Face)에 있으며, 함께 제공되는 프로그램의 라이선스는 \"LICENSES\" 폴더에 있습니다.", "LICENSES 폴더 열기"],
    "zh-Hans": ["模型许可", "使用 Studio Audio AI 创作的曲目、封面和视频的商业化受所用模型许可的约束。请参阅其许可：",
                "音乐", "翻唱与乐谱", "音频分析", "LoRA", "每个 LoRA 有各自的许可（见其页面）", "歌词转写",
                "封面与短片", "文本 AI", "用途", "模型", "许可", "非商业",
                "完整文本见各模型页面（Hugging Face）；随附程序的许可在「LICENSES」文件夹中。", "打开 LICENSES 文件夹"],
    "yue": ["模型授權", "用 Studio Audio AI 整嘅歌、封面同影片嘅商業化，受所用模型嘅授權約束。請參閱佢哋嘅授權：",
            "音樂", "翻唱同樂譜", "音訊分析", "LoRA", "每個 LoRA 有自己嘅授權（睇佢嘅頁面）", "歌詞轉寫",
            "封面同短片", "文字 AI", "用途", "模型", "授權", "非商業",
            "完整文本喺每個模型嘅頁面（Hugging Face）；附帶程式嘅授權喺「LICENSES」資料夾。", "打開 LICENSES 資料夾"],
}
langues = Path(sys.argv[1])
for code, vals in T.items():
    assert len(vals) == len(K), code
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    s = d["strings"]
    for k, v in zip(K, vals):
        if not s.get(k):
            s[k] = v
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")
