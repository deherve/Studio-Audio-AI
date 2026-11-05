# Traductions de la 1.0.89 (partition reconnue). Usage : python fill_189.py <ressources\langues>
import json, sys
from pathlib import Path

K = ["Modèles de transcription absents (SheetSage2 et MERT dans modeles\\musique).",
     "La reconnaissance de la partition a échoué (code %d, journal : %s).",
     "Partition reconnue : choisissez vos paroles et votre style dans « Nouveau morceau », retouchez-la si besoin, puis générez.",
     "Morceau dont reconnaître la partition", "Partitions ABC", "Ouvrir une partition",
     "Ce fichier ne ressemble pas à une partition ABC (pas de ligne « K: »).",
     "Reconnaissance de la partition… %d s", "Ou partir d'un morceau existant :", "Reconnaître la partition d'un fichier audio…",
     "SheetSage2 écoute le morceau et écrit sa partition (mélodie, et accords si « Complète » est choisi), ≈ 1 min pour 3 min de musique. Vous la retouchez, puis vous générez avec vos paroles et votre style : comme une reprise, mais la partition est visible et modifiable.",
     "Ouvrir une partition (.abc)…", "(morceau entier)", "Reconnaître sa partition…",
     "SheetSage2 écoute le morceau et écrit sa partition dans l'onglet « Partition » (≈ 1 min pour 3 min) : pour en faire un nouveau morceau avec d'autres paroles ou un autre style."]
T = {
    "en": ["Transcription models missing (SheetSage2 and MERT in modeles\\musique).", "Score recognition failed (code %d, log: %s).",
           "Score recognised: choose your lyrics and style in “New track”, edit the score if needed, then generate.",
           "Track whose score to recognise", "ABC scores", "Open a score", "This file does not look like an ABC score (no “K:” line).",
           "Recognising the score… %d s", "Or start from an existing track:", "Recognise the score of an audio file…",
           "SheetSage2 listens to the track and writes its score (melody, plus chords if “Full” is chosen), ≈ 1 min for 3 min of music. You edit it, then generate with your own lyrics and style: like a cover, but the score is visible and editable.",
           "Open a score (.abc)…", "(whole track)", "Recognise its score…",
           "SheetSage2 listens to the track and writes its score in the “Score” tab (≈ 1 min for 3 min): to make a new track from it with other lyrics or another style."],
    "es": ["Faltan los modelos de transcripción (SheetSage2 y MERT en modeles\\musique).", "El reconocimiento de la partitura falló (código %d, registro: %s).",
           "Partitura reconocida: elija letra y estilo en «Nuevo tema», retóquela si hace falta y genere.",
           "Tema cuya partitura reconocer", "Partituras ABC", "Abrir una partitura", "Este archivo no parece una partitura ABC (sin línea «K:»).",
           "Reconociendo la partitura… %d s", "O partir de un tema existente:", "Reconocer la partitura de un archivo de audio…",
           "SheetSage2 escucha el tema y escribe su partitura (melodía, y acordes si se elige «Completa»), ≈ 1 min por 3 min de música. La retoca y genera con su letra y su estilo: como una versión, pero con la partitura visible y editable.",
           "Abrir una partitura (.abc)…", "(tema completo)", "Reconocer su partitura…",
           "SheetSage2 escucha el tema y escribe su partitura en la pestaña «Partitura» (≈ 1 min por 3 min): para crear un tema nuevo con otra letra u otro estilo."],
    "it": ["Modelli di trascrizione assenti (SheetSage2 e MERT in modeles\\musique).", "Riconoscimento della partitura non riuscito (codice %d, registro: %s).",
           "Partitura riconosciuta: scegli testo e stile in «Nuovo brano», ritoccala se serve, poi genera.",
           "Brano di cui riconoscere la partitura", "Partiture ABC", "Apri una partitura", "Questo file non sembra una partitura ABC (nessuna riga «K:»).",
           "Riconoscimento della partitura… %d s", "Oppure parti da un brano esistente:", "Riconosci la partitura di un file audio…",
           "SheetSage2 ascolta il brano e ne scrive la partitura (melodia, e accordi se è scelto «Completa»), ≈ 1 min per 3 min di musica. La ritocchi, poi generi con il tuo testo e il tuo stile: come una cover, ma la partitura è visibile e modificabile.",
           "Apri una partitura (.abc)…", "(brano intero)", "Riconosci la sua partitura…",
           "SheetSage2 ascolta il brano e ne scrive la partitura nella scheda «Partitura» (≈ 1 min per 3 min): per farne un nuovo brano con un altro testo o un altro stile."],
    "de": ["Transkriptionsmodelle fehlen (SheetSage2 und MERT in modeles\\musique).", "Erkennung der Partitur fehlgeschlagen (Code %d, Protokoll: %s).",
           "Partitur erkannt: Text und Stil in „Neuer Titel“ wählen, Partitur bei Bedarf anpassen, dann erzeugen.",
           "Titel, dessen Partitur erkannt werden soll", "ABC-Partituren", "Partitur öffnen", "Diese Datei sieht nicht wie eine ABC-Partitur aus (keine Zeile „K:“).",
           "Partitur wird erkannt… %d s", "Oder von einem vorhandenen Titel ausgehen:", "Partitur einer Audiodatei erkennen…",
           "SheetSage2 hört den Titel und schreibt seine Partitur (Melodie, dazu Akkorde bei „Vollständig“), ≈ 1 Min. für 3 Min. Musik. Sie passen sie an und erzeugen mit eigenem Text und Stil: wie eine Coverversion, aber die Partitur ist sichtbar und änderbar.",
           "Partitur öffnen (.abc)…", "(ganzer Titel)", "Seine Partitur erkennen…",
           "SheetSage2 hört den Titel und schreibt seine Partitur in den Reiter „Partitur“ (≈ 1 Min. für 3 Min.): um daraus einen neuen Titel mit anderem Text oder Stil zu machen."],
    "pt": ["Modelos de transcrição em falta (SheetSage2 e MERT em modeles\\musique).", "O reconhecimento da partitura falhou (código %d, registo: %s).",
           "Partitura reconhecida: escolha letra e estilo em «Nova faixa», retoque-a se necessário e gere.",
           "Faixa cuja partitura reconhecer", "Partituras ABC", "Abrir uma partitura", "Este ficheiro não parece uma partitura ABC (sem linha «K:»).",
           "A reconhecer a partitura… %d s", "Ou partir de uma faixa existente:", "Reconhecer a partitura de um ficheiro de áudio…",
           "O SheetSage2 ouve a faixa e escreve a sua partitura (melodia, e acordes se «Completa» for escolhida), ≈ 1 min por 3 min de música. Retoca-a e gera com a sua letra e o seu estilo: como uma versão, mas a partitura é visível e editável.",
           "Abrir uma partitura (.abc)…", "(faixa inteira)", "Reconhecer a sua partitura…",
           "O SheetSage2 ouve a faixa e escreve a sua partitura no separador «Partitura» (≈ 1 min por 3 min): para fazer uma faixa nova com outra letra ou outro estilo."],
    "ja": ["採譜モデルがありません（modeles\\musique の SheetSage2 と MERT）。", "楽譜の認識に失敗しました（コード %d、ログ：%s）。",
           "楽譜を認識しました。「新しい曲」で歌詞とスタイルを選び、必要なら楽譜を修正してから生成してください。",
           "楽譜を認識する曲", "ABC 楽譜", "楽譜を開く", "このファイルは ABC 楽譜ではないようです（「K:」の行がありません）。",
           "楽譜を認識しています… %d 秒", "または既存の曲から始める：", "音声ファイルの楽譜を認識…",
           "SheetSage2 が曲を聴いて楽譜を書きます（メロディ、「完全」ならコードも）。3 分の曲で約 1 分。楽譜を修正し、自分の歌詞とスタイルで生成します。カバーと同じですが、楽譜が見えて編集できます。",
           "楽譜を開く（.abc）…", "（曲全体）", "楽譜を認識…",
           "SheetSage2 が曲を聴いて「楽譜」タブに楽譜を書きます（3 分で約 1 分）。別の歌詞やスタイルで新しい曲を作れます。"],
    "ko": ["채보 모델이 없습니다(modeles\\musique의 SheetSage2와 MERT).", "악보 인식에 실패했습니다(코드 %d, 기록: %s).",
           "악보 인식 완료: \"새 곡\"에서 가사와 스타일을 고르고, 필요하면 악보를 수정한 뒤 생성하세요.",
           "악보를 인식할 곡", "ABC 악보", "악보 열기", "이 파일은 ABC 악보가 아닌 것 같습니다(\"K:\" 줄 없음).",
           "악보 인식 중… %d초", "또는 기존 곡에서 시작:", "오디오 파일의 악보 인식…",
           "SheetSage2가 곡을 듣고 악보를 씁니다(멜로디, \"전체\"를 고르면 코드도). 3분 곡에 약 1분. 악보를 수정하고 자신의 가사와 스타일로 생성합니다: 커버와 같지만 악보를 보고 고칠 수 있습니다.",
           "악보 열기(.abc)…", "(곡 전체)", "악보 인식…",
           "SheetSage2가 곡을 듣고 \"악보\" 탭에 악보를 씁니다(3분에 약 1분): 다른 가사나 스타일로 새 곡을 만들 수 있습니다."],
    "zh-Hans": ["缺少转录模型（modeles\\musique 中的 SheetSage2 和 MERT）。", "乐谱识别失败（代码 %d，日志：%s）。",
                "乐谱已识别：请在「新曲目」中选择歌词和风格，必要时修改乐谱，然后生成。",
                "要识别乐谱的曲目", "ABC 乐谱", "打开乐谱", "此文件不像 ABC 乐谱（没有「K:」行）。",
                "正在识别乐谱… %d 秒", "或从现有曲目开始：", "识别音频文件的乐谱…",
                "SheetSage2 聆听曲目并写出其乐谱（旋律；选择「完整」时还有和弦），3 分钟音乐约需 1 分钟。修改后用您自己的歌词和风格生成：与翻唱相同，但乐谱可见且可编辑。",
                "打开乐谱（.abc）…", "（整首曲目）", "识别其乐谱…",
                "SheetSage2 聆听曲目并在「乐谱」标签中写出乐谱（3 分钟约 1 分钟）：可用其他歌词或风格制作新曲目。"],
    "yue": ["冇轉錄模型（modeles\\musique 入面嘅 SheetSage2 同 MERT）。", "識別樂譜失敗（代碼 %d，記錄：%s）。",
            "樂譜識別咗：喺「新歌」揀歌詞同風格，有需要就改樂譜，然後生成。",
            "要識別樂譜嘅歌", "ABC 樂譜", "打開樂譜", "呢個檔案唔似 ABC 樂譜（冇「K:」行）。",
            "識別緊樂譜… %d 秒", "或者由現有嘅歌開始：", "識別音訊檔案嘅樂譜…",
            "SheetSage2 聽首歌並寫出佢嘅樂譜（旋律；揀「完整」就連和弦），3 分鐘音樂大約 1 分鐘。改完之後用你自己嘅歌詞同風格生成：同翻唱一樣，但樂譜睇得到又改得到。",
            "打開樂譜（.abc）…", "（成首歌）", "識別佢嘅樂譜…",
            "SheetSage2 聽首歌並喺「樂譜」分頁寫出樂譜（3 分鐘大約 1 分鐘）：可以用其他歌詞或者風格整新歌。"],
}
langues = Path(sys.argv[1])
for code, vals in T.items():
    assert len(vals) == len(K), (code, len(vals))
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    s = d["strings"]
    for k, v in zip(K, vals):
        for key in (k, k.replace("\\", "\\\\")):
            if not s.get(key):
                s[key] = v
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")
