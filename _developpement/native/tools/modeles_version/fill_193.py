# Traductions de la 1.0.93 (import MusicXML / MIDI / ABC). Usage : python fill_193.py <ressources\langues>
import json, sys
from pathlib import Path

K = ["Partition « %s » placée dans l'onglet « Partition » : choisissez le style et les paroles dans « Nouveau morceau », puis « Générer avec cette partition ».",
     "Importer…",
     "Ouvrir une partition : MusicXML (.musicxml, .xml, .mxl), MIDI (.mid) ou ABC (.abc). Elle est mise au format du moteur YuE2 (voix chantée et instrument).",
     "Utiliser dans le Studio",
     "Envoie cette partition dans l'onglet « Partition » du Studio, pour générer un morceau qui la suit.",
     "Aucune partition : « Importer… » pour ouvrir un fichier MusicXML, MIDI ou ABC.",
     "Partition importée", "Tonalité", "Import impossible :",
     "Importer MusicXML ou MIDI (Atelier partition)…",
     "Ouvre l'Atelier partition vide : « Importer… » lit un fichier MusicXML, MIDI ou ABC, l'affiche en notes, puis « Utiliser dans le Studio » le place ici."]
T = {
    "en": ["Score “%s” placed in the “Score” tab: choose the style and lyrics in “New track”, then “Generate with this score”.", "Import…",
           "Open a score: MusicXML (.musicxml, .xml, .mxl), MIDI (.mid) or ABC (.abc). It is converted to the YuE2 engine's format (sung voice and instrument).",
           "Use in the Studio", "Sends this score to the Studio's “Score” tab, to generate a track that follows it.",
           "No score: “Import…” to open a MusicXML, MIDI or ABC file.", "Imported score", "Key", "Import failed:",
           "Import MusicXML or MIDI (Score workshop)…", "Opens an empty Score workshop: “Import…” reads a MusicXML, MIDI or ABC file, shows it as notes, then “Use in the Studio” places it here."],
    "es": ["Partitura «%s» colocada en la pestaña «Partitura»: elija estilo y letra en «Nuevo tema» y luego «Generar con esta partitura».", "Importar…",
           "Abrir una partitura: MusicXML (.musicxml, .xml, .mxl), MIDI (.mid) o ABC (.abc). Se adapta al formato del motor YuE2 (voz cantada e instrumento).",
           "Usar en el Studio", "Envía esta partitura a la pestaña «Partitura» del Studio para generar un tema que la siga.",
           "Sin partitura: «Importar…» para abrir un archivo MusicXML, MIDI o ABC.", "Partitura importada", "Tonalidad", "Importación imposible:",
           "Importar MusicXML o MIDI (Taller de partitura)…", "Abre el Taller de partitura vacío: «Importar…» lee un archivo MusicXML, MIDI o ABC, lo muestra en notas y «Usar en el Studio» lo coloca aquí."],
    "it": ["Partitura «%s» inserita nella scheda «Partitura»: scegli stile e testo in «Nuovo brano», poi «Genera con questa partitura».", "Importa…",
           "Apri una partitura: MusicXML (.musicxml, .xml, .mxl), MIDI (.mid) o ABC (.abc). Viene adattata al formato del motore YuE2 (voce cantata e strumento).",
           "Usa nello Studio", "Invia questa partitura alla scheda «Partitura» dello Studio, per generare un brano che la segue.",
           "Nessuna partitura: «Importa…» per aprire un file MusicXML, MIDI o ABC.", "Partitura importata", "Tonalità", "Importazione impossibile:",
           "Importa MusicXML o MIDI (Laboratorio partitura)…", "Apre il Laboratorio partitura vuoto: «Importa…» legge un file MusicXML, MIDI o ABC, lo mostra in note, poi «Usa nello Studio» lo mette qui."],
    "de": ["Partitur „%s“ in den Reiter „Partitur“ gelegt: Stil und Text in „Neuer Titel“ wählen, dann „Mit dieser Partitur erzeugen“.", "Importieren…",
           "Partitur öffnen: MusicXML (.musicxml, .xml, .mxl), MIDI (.mid) oder ABC (.abc). Sie wird in das Format der YuE2-Engine gebracht (Gesang und Instrument).",
           "Im Studio verwenden", "Sendet diese Partitur in den Reiter „Partitur“ des Studios, um einen Titel zu erzeugen, der ihr folgt.",
           "Keine Partitur: „Importieren…“ öffnet eine MusicXML-, MIDI- oder ABC-Datei.", "Importierte Partitur", "Tonart", "Import nicht möglich:",
           "MusicXML oder MIDI importieren (Partitur-Werkstatt)…", "Öffnet die leere Partitur-Werkstatt: „Importieren…“ liest eine MusicXML-, MIDI- oder ABC-Datei, zeigt sie in Noten, dann legt „Im Studio verwenden“ sie hier ab."],
    "pt": ["Partitura «%s» colocada no separador «Partitura»: escolha estilo e letra em «Nova faixa» e depois «Gerar com esta partitura».", "Importar…",
           "Abrir uma partitura: MusicXML (.musicxml, .xml, .mxl), MIDI (.mid) ou ABC (.abc). É adaptada ao formato do motor YuE2 (voz cantada e instrumento).",
           "Usar no Studio", "Envia esta partitura para o separador «Partitura» do Studio, para gerar uma faixa que a segue.",
           "Sem partitura: «Importar…» para abrir um ficheiro MusicXML, MIDI ou ABC.", "Partitura importada", "Tonalidade", "Importação impossível:",
           "Importar MusicXML ou MIDI (Oficina de partitura)…", "Abre a Oficina de partitura vazia: «Importar…» lê um ficheiro MusicXML, MIDI ou ABC, mostra-o em notas e «Usar no Studio» coloca-o aqui."],
    "ja": ["楽譜「%s」を「楽譜」タブに入れました。「新しい曲」でスタイルと歌詞を選び、「この楽譜で生成」してください。", "読み込む…",
           "楽譜を開く：MusicXML（.musicxml、.xml、.mxl）、MIDI（.mid）、ABC（.abc）。YuE2 エンジンの形式（歌と楽器）に変換されます。",
           "Studio で使う", "この楽譜を Studio の「楽譜」タブに送り、それに沿った曲を生成します。",
           "楽譜がありません：「読み込む…」で MusicXML、MIDI、ABC ファイルを開きます。", "読み込んだ楽譜", "キー", "読み込めません：",
           "MusicXML または MIDI を読み込む（楽譜工房）…", "空の楽譜工房を開きます：「読み込む…」で MusicXML、MIDI、ABC を読み、音符で表示し、「Studio で使う」でここに入れます。"],
    "ko": ["악보 \"%s\"를 \"악보\" 탭에 넣었습니다: \"새 곡\"에서 스타일과 가사를 고른 뒤 \"이 악보로 생성\"을 누르세요.", "가져오기…",
           "악보 열기: MusicXML(.musicxml, .xml, .mxl), MIDI(.mid), ABC(.abc). YuE2 엔진 형식(노래와 악기)으로 바뀝니다.",
           "Studio에서 사용", "이 악보를 Studio의 \"악보\" 탭으로 보내 이를 따르는 곡을 생성합니다.",
           "악보 없음: \"가져오기…\"로 MusicXML, MIDI, ABC 파일을 여세요.", "가져온 악보", "조성", "가져올 수 없음:",
           "MusicXML 또는 MIDI 가져오기(악보 작업실)…", "빈 악보 작업실을 엽니다: \"가져오기…\"로 MusicXML, MIDI, ABC 파일을 읽어 음표로 보여 주고 \"Studio에서 사용\"으로 여기에 넣습니다."],
    "zh-Hans": ["乐谱「%s」已放入「乐谱」标签：请在「新曲目」中选择风格和歌词，然后「用此乐谱生成」。", "导入…",
                "打开乐谱：MusicXML（.musicxml、.xml、.mxl）、MIDI（.mid）或 ABC（.abc）。会转换为 YuE2 引擎的格式（人声和乐器）。",
                "在 Studio 中使用", "将此乐谱发送到 Studio 的「乐谱」标签，生成遵循它的曲目。",
                "没有乐谱：用「导入…」打开 MusicXML、MIDI 或 ABC 文件。", "已导入的乐谱", "调性", "无法导入：",
                "导入 MusicXML 或 MIDI（乐谱工坊）…", "打开空的乐谱工坊：「导入…」读取 MusicXML、MIDI 或 ABC 文件并以音符显示，然后「在 Studio 中使用」将其放到这里。"],
    "yue": ["樂譜「%s」已經放入「樂譜」分頁：喺「新歌」揀風格同歌詞，然後「用呢份樂譜整」。", "匯入…",
            "打開樂譜：MusicXML（.musicxml、.xml、.mxl）、MIDI（.mid）或者 ABC（.abc）。會轉做 YuE2 引擎嘅格式（人聲同樂器）。",
            "喺 Studio 用", "將呢份樂譜送去 Studio 嘅「樂譜」分頁，整一首跟住佢嘅歌。",
            "冇樂譜：用「匯入…」打開 MusicXML、MIDI 或者 ABC 檔案。", "匯入咗嘅樂譜", "調性", "匯入唔到：",
            "匯入 MusicXML 或者 MIDI（樂譜工坊）…", "打開空嘅樂譜工坊：「匯入…」讀 MusicXML、MIDI 或者 ABC 檔案，用音符顯示，然後「喺 Studio 用」放到呢度。"],
}
langues = Path(sys.argv[1])
for code, vals in T.items():
    assert len(vals) == len(K), (code, len(vals))
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    s = d["strings"]
    for k, v in zip(K, vals):
        if not s.get(k):
            s[k] = v
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")
