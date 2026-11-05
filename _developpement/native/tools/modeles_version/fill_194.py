# Traductions de la 1.0.94 (édition dans l'Atelier partition). Usage : python fill_194.py <ressources\langues>
# Ordre des langues : en, es, it, de, pt, ja, ko, zh-Hans, yue. Les « %d » restent dans le même ordre.
import json, sys
from pathlib import Path

L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
    "Note choisie :": ["Selected note:", "Nota elegida:", "Nota scelta:", "Gewählte Note:", "Nota escolhida:", "選択した音符：", "선택한 음표:", "所选音符：", "揀咗嘅音符："],
    "Silence": ["Rest", "Silencio", "Pausa", "Pause", "Pausa", "休符", "쉼표", "休止符", "休止符"],
    "Dupliquer": ["Duplicate", "Duplicar", "Duplica", "Duplizieren", "Duplicar", "複製", "복제", "复制", "複製"],
    "Accord…": ["Chord…", "Acorde…", "Accordo…", "Akkord…", "Acorde…", "コード…", "코드…", "和弦…", "和弦…"],
    "Paroles…": ["Lyrics…", "Letra…", "Testo…", "Text…", "Letra…", "歌詞…", "가사…", "歌词…", "歌詞…"],
    "Texte ABC": ["ABC text", "Texto ABC", "Testo ABC", "ABC-Text", "Texto ABC", "ABC テキスト", "ABC 텍스트", "ABC 文本", "ABC 文字"],
    "Paroles placées sous la mélodie chantée, une syllabe par note (séparez les syllabes d'un mot par « - », ex. : « le ma-tin se lè-ve »). Les sections entre crochets sont ignorées.": [
        "Lyrics placed under the sung melody, one syllable per note (split a word's syllables with “-”, e.g. “the mor-ning ri-ses”). Sections in square brackets are ignored.",
        "Letra colocada bajo la melodía cantada, una sílaba por nota (separe las sílabas de una palabra con «-», p. ej.: «la ma-ña-na se le-van-ta»). Las secciones entre corchetes se ignoran.",
        "Testo posto sotto la melodia cantata, una sillaba per nota (separa le sillabe di una parola con «-», es.: «il mat-ti-no si al-za»). Le sezioni tra parentesi quadre sono ignorate.",
        "Text unter der gesungenen Melodie, eine Silbe pro Note (Silben eines Wortes mit „-“ trennen, z. B. „der Mor-gen bricht an“). Abschnitte in eckigen Klammern werden ignoriert.",
        "Letra colocada sob a melodia cantada, uma sílaba por nota (separe as sílabas de uma palavra com «-», ex.: «a ma-nhã se le-van-ta»). Secções entre parênteses retos são ignoradas.",
        "歌のメロディーの下に歌詞を 1 音符 1 音節で配置します（単語の音節は「-」で区切ります。例：「mor-ning」）。角かっこ内のセクションは無視されます。",
        "노래 멜로디 아래에 음표마다 한 음절씩 가사를 놓습니다(단어의 음절은 \"-\"로 나눕니다. 예: \"mor-ning\"). 대괄호 안의 섹션은 무시됩니다.",
        "歌词放在演唱旋律下方，每个音符一个音节（单词的音节用“-”分开，例如“mor-ning”）。方括号中的段落会被忽略。",
        "歌詞放喺演唱旋律下面，每個音符一個音節（單詞嘅音節用「-」分開，例如「mor-ning」）。方括號入面嘅段落會被忽略。"],
    "Placer sous les notes": ["Place under the notes", "Colocar bajo las notas", "Posiziona sotto le note", "Unter die Noten setzen", "Colocar sob as notas", "音符の下に配置", "음표 아래에 배치", "放到音符下方", "放喺音符下面"],
    "Retirer les paroles": ["Remove lyrics", "Quitar la letra", "Rimuovi il testo", "Text entfernen", "Remover a letra", "歌詞を削除", "가사 제거", "移除歌词", "移除歌詞"],
    "Texte de la partition (format ABC) : chaque modification est redessinée aussitôt.": [
        "Score text (ABC format): every change is redrawn at once.", "Texto de la partitura (formato ABC): cada cambio se redibuja al instante.",
        "Testo della partitura (formato ABC): ogni modifica viene ridisegnata subito.", "Text der Partitur (ABC-Format): jede Änderung wird sofort neu gezeichnet.",
        "Texto da partitura (formato ABC): cada alteração é redesenhada de imediato.", "楽譜のテキスト（ABC 形式）：変更はすぐに再描画されます。",
        "악보 텍스트(ABC 형식): 변경할 때마다 바로 다시 그려집니다.", "乐谱文本（ABC 格式）：每次修改都会立即重绘。", "樂譜文字（ABC 格式）：每次修改都會即刻重畫。"],
    "%d syllabe(s) placée(s) sur %d.": ["%d syllable(s) placed out of %d.", "%d sílaba(s) colocada(s) de %d.", "%d sillaba/e posizionata/e su %d.", "%d Silbe(n) von %d gesetzt.",
        "%d sílaba(s) colocada(s) de %d.", "%d / %d 音節を配置しました。", "%d / %d 음절을 배치했습니다.", "已放置 %d / %d 个音节。", "已經放咗 %d / %d 個音節。"],
    "Mesures de durée incorrecte :": ["Measures with a wrong length:", "Compases de duración incorrecta:", "Battute di durata errata:", "Takte mit falscher Länge:", "Compassos de duração incorreta:",
        "長さが正しくない小節：", "길이가 맞지 않는 마디:", "时值不正确的小节：", "時值唔啱嘅小節："],
    "Accord (ex. : Dm, G7, Cmaj7, F#m, Bb/D) ; vide pour l'enlever :": ["Chord (e.g. Dm, G7, Cmaj7, F#m, Bb/D); empty to remove it:", "Acorde (p. ej.: Dm, G7, Cmaj7, F#m, Bb/D); vacío para quitarlo:",
        "Accordo (es.: Dm, G7, Cmaj7, F#m, Bb/D); vuoto per toglierlo:", "Akkord (z. B. Dm, G7, Cmaj7, F#m, Bb/D); leer zum Entfernen:", "Acorde (ex.: Dm, G7, Cmaj7, F#m, Bb/D); vazio para o remover:",
        "コード（例：Dm、G7、Cmaj7、F#m、Bb/D）。空にすると削除：", "코드(예: Dm, G7, Cmaj7, F#m, Bb/D), 비우면 제거:", "和弦（例如 Dm、G7、Cmaj7、F#m、Bb/D）；留空则删除：", "和弦（例如 Dm、G7、Cmaj7、F#m、Bb/D）；留空就刪除："],
    "(non modifiable ici : utilisez le texte ABC)": ["(cannot be edited here: use the ABC text)", "(no editable aquí: use el texto ABC)", "(non modificabile qui: usa il testo ABC)",
        "(hier nicht bearbeitbar: ABC-Text verwenden)", "(não editável aqui: use o texto ABC)", "（ここでは編集できません：ABC テキストを使用）", "(여기서 편집 불가: ABC 텍스트 사용)", "（此处无法编辑：请使用 ABC 文本）", "（呢度改唔到：請用 ABC 文字）"],
    "Note plus basse": ["Lower note", "Nota más grave", "Nota più bassa", "Tiefere Note", "Nota mais grave", "音を下げる", "음 내리기", "降低音符", "降低音符"],
    "Note plus haute": ["Higher note", "Nota más aguda", "Nota più alta", "Höhere Note", "Nota mais aguda", "音を上げる", "음 올리기", "升高音符", "升高音符"],
    "Une octave plus bas": ["One octave lower", "Una octava más grave", "Un'ottava sotto", "Eine Oktave tiefer", "Uma oitava abaixo", "1 オクターブ下", "한 옥타브 아래", "降低一个八度", "降低一個八度"],
    "Une octave plus haut": ["One octave higher", "Una octava más aguda", "Un'ottava sopra", "Eine Oktave höher", "Uma oitava acima", "1 オクターブ上", "한 옥타브 위", "升高一个八度", "升高一個八度"],
    "Dièse": ["Sharp", "Sostenido", "Diesis", "Kreuz", "Sustenido", "シャープ", "샵", "升号", "升號"],
    "Bémol": ["Flat", "Bemol", "Bemolle", "Be", "Bemol", "フラット", "플랫", "降号", "降號"],
    "Bécarre": ["Natural", "Becuadro", "Bequadro", "Auflösungszeichen", "Bequadro", "ナチュラル", "내추럴", "还原号", "還原號"],
    "Note pointée": ["Dotted note", "Nota con puntillo", "Nota puntata", "Punktierte Note", "Nota pontuada", "付点音符", "점음표", "附点音符", "附點音符"],
    "Annuler la dernière modification": ["Undo the last change", "Deshacer el último cambio", "Annulla l'ultima modifica", "Letzte Änderung rückgängig machen", "Anular a última alteração",
        "最後の変更を元に戻す", "마지막 변경 취소", "撤销上一次修改", "復原上一次修改"],
    "Rétablir la modification annulée": ["Redo the undone change", "Rehacer el cambio deshecho", "Ripristina la modifica annullata", "Rückgängig gemachte Änderung wiederherstellen",
        "Refazer a alteração anulada", "元に戻した変更をやり直す", "취소한 변경 다시 실행", "重做已撤销的修改", "重做已復原嘅修改"],
    "Remplacer la note par un silence": ["Replace the note with a rest", "Sustituir la nota por un silencio", "Sostituisci la nota con una pausa", "Note durch eine Pause ersetzen",
        "Substituir a nota por uma pausa", "音符を休符に置き換える", "음표를 쉼표로 바꾸기", "将音符替换为休止符", "將音符換成休止符"],
    "Répéter la note juste après": ["Repeat the note right after", "Repetir la nota justo después", "Ripeti la nota subito dopo", "Note direkt danach wiederholen",
        "Repetir a nota logo a seguir", "直後に同じ音符を繰り返す", "바로 뒤에 음표 반복", "在后面重复该音符", "喺後面重複呢個音符"],
    "Retirer la note (la mesure devient plus courte)": ["Remove the note (the measure becomes shorter)", "Quitar la nota (el compás se acorta)", "Rimuovi la nota (la battuta si accorcia)",
        "Note entfernen (der Takt wird kürzer)", "Remover a nota (o compasso fica mais curto)", "音符を削除（小節が短くなります）", "음표 제거(마디가 짧아짐)", "删除音符（小节变短）", "刪除音符（小節會變短）"],
    "Accord affiché au-dessus de la note": ["Chord shown above the note", "Acorde mostrado sobre la nota", "Accordo mostrato sopra la nota", "Akkord über der Note",
        "Acorde mostrado acima da nota", "音符の上に表示するコード", "음표 위에 표시되는 코드", "显示在音符上方的和弦", "顯示喺音符上面嘅和弦"],
    "Ronde": ["Whole note", "Redonda", "Semibreve", "Ganze Note", "Semibreve", "全音符", "온음표", "全音符", "全音符"],
    "Blanche": ["Half note", "Blanca", "Minima", "Halbe Note", "Mínima", "2 分音符", "2분음표", "二分音符", "二分音符"],
    "Noire": ["Quarter note", "Negra", "Semiminima", "Viertelnote", "Semínima", "4 分音符", "4분음표", "四分音符", "四分音符"],
    "Croche": ["Eighth note", "Corchea", "Croma", "Achtelnote", "Colcheia", "8 分音符", "8분음표", "八分音符", "八分音符"],
    "Double croche": ["Sixteenth note", "Semicorchea", "Semicroma", "Sechzehntelnote", "Semicolcheia", "16 分音符", "16분음표", "十六分音符", "十六分音符"],
    "« Nouveau morceau » est en mode Reprise : repassez en mode Chanson ou Instrumental pour créer un morceau avec cette partition.": [
        "“New track” is in Cover mode: switch back to Song or Instrumental mode to create a track with this score.",
        "«Nuevo tema» está en modo Versión: vuelva al modo Canción o Instrumental para crear un tema con esta partitura.",
        "«Nuovo brano» è in modalità Cover: torna alla modalità Canzone o Strumentale per creare un brano con questa partitura.",
        "„Neuer Titel“ ist im Cover-Modus: wechseln Sie zurück zu Song oder Instrumental, um mit dieser Partitur einen Titel zu erstellen.",
        "«Nova faixa» está no modo Versão: volte ao modo Canção ou Instrumental para criar uma faixa com esta partitura.",
        "「新しい曲」はカバーモードです。この楽譜で曲を作るには、ソングまたはインストゥルメンタルに戻してください。",
        "\"새 곡\"이 커버 모드입니다. 이 악보로 곡을 만들려면 노래 또는 연주곡 모드로 돌아가세요.",
        "“新曲目”处于翻唱模式：请切回歌曲或纯音乐模式，以用此乐谱创作曲目。",
        "「新曲」而家係翻唱模式：請轉返歌曲或純音樂模式，用呢份樂譜創作曲目。"],
    "Pour générer, renseignez dans « Nouveau morceau » : %s.": ["To generate, fill in under “New track”: %s.", "Para generar, rellene en «Nuevo tema»: %s.",
        "Per generare, compila in «Nuovo brano»: %s.", "Zum Erzeugen in „Neuer Titel“ ausfüllen: %s.", "Para gerar, preencha em «Nova faixa»: %s.",
        "生成するには「新しい曲」で次を入力してください：%s。", "생성하려면 \"새 곡\"에서 입력하세요: %s.", "要生成，请在“新曲目”中填写：%s。", "要生成，請喺「新曲」填寫：%s。"],
    "le titre": ["the title", "el título", "il titolo", "den Titel", "o título", "タイトル", "제목", "标题", "標題"],
    "le style": ["the style", "el estilo", "lo stile", "den Stil", "o estilo", "スタイル", "스타일", "风格", "風格"],
    "les paroles": ["the lyrics", "la letra", "il testo", "den Text", "a letra", "歌詞", "가사", "歌词", "歌詞"],
}
langues = Path(sys.argv[1])
for i, code in enumerate(L):
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    s = d["strings"]
    for k, v in T.items():
        assert len(v) == 9, k
        assert v[i].count("%d") == k.count("%d"), (code, k)
        s[k] = v[i]
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print(code, len(T))
print("OK")
