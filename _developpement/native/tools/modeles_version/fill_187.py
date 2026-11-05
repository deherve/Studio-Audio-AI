# Traductions de la 1.0.87 (partition). Usage : python fill_187.py <ressources\langues>
# Un texte déjà traduit ailleurs garde sa traduction.
import json, sys
from pathlib import Path

K = ["Do", "Ré", "Mi", "Fa", "Sol", "La", "Si", "dièse", "bémol", "mineur", "majeur",
     "Partition", "Pas de partition pour ce morceau : il a été créé en mode « Sans partition » ou avant la version 1.0.87.",
     "Tonalité : %s (%s)", "Tempo : %d BPM", "Mesure : %s", "Section", "Accords", "Partition complète (texte ABC)",
     "La partition est le plan que le modèle a écrit avant de composer le son ; le morceau la suit (tonalité, tempo, accords).",
     "Ouvrir le fichier", "Voir la partition…", "Morceau créé sans partition (mode « Sans partition » ou avant la version 1.0.87)."]
LETTERS = ["C", "D", "E", "F", "G", "A", "B"]
T = {
    "en": LETTERS + ["sharp", "flat", "minor", "major", "Score", "No score for this track: it was made in “No score” mode or before version 1.0.87.",
                     "Key: %s (%s)", "Tempo: %d BPM", "Time: %s", "Section", "Chords", "Full score (ABC text)",
                     "The score is the plan the model wrote before composing the sound; the track follows it (key, tempo, chords).",
                     "Open the file", "View the score…", "Track made without a score (“No score” mode or before version 1.0.87)."],
    "es": ["Do", "Re", "Mi", "Fa", "Sol", "La", "Si", "sostenido", "bemol", "menor", "mayor", "Partitura",
           "No hay partitura para este tema: se creó en modo «Sin partitura» o antes de la versión 1.0.87.",
           "Tonalidad: %s (%s)", "Tempo: %d BPM", "Compás: %s", "Sección", "Acordes", "Partitura completa (texto ABC)",
           "La partitura es el plan que el modelo escribió antes de componer el sonido; el tema la sigue (tonalidad, tempo, acordes).",
           "Abrir el archivo", "Ver la partitura…", "Tema creado sin partitura (modo «Sin partitura» o antes de la versión 1.0.87)."],
    "it": ["Do", "Re", "Mi", "Fa", "Sol", "La", "Si", "diesis", "bemolle", "minore", "maggiore", "Partitura",
           "Nessuna partitura per questo brano: è stato creato in modalità «Senza partitura» o prima della versione 1.0.87.",
           "Tonalità: %s (%s)", "Tempo: %d BPM", "Metro: %s", "Sezione", "Accordi", "Partitura completa (testo ABC)",
           "La partitura è il piano che il modello ha scritto prima di comporre il suono; il brano la segue (tonalità, tempo, accordi).",
           "Apri il file", "Vedi la partitura…", "Brano creato senza partitura (modalità «Senza partitura» o prima della versione 1.0.87)."],
    "de": ["C", "D", "E", "F", "G", "A", "H", "Kreuz", "b", "Moll", "Dur", "Partitur",
           "Keine Partitur für diesen Titel: Er wurde im Modus „Ohne Partitur“ oder vor Version 1.0.87 erstellt.",
           "Tonart: %s (%s)", "Tempo: %d BPM", "Takt: %s", "Abschnitt", "Akkorde", "Vollständige Partitur (ABC-Text)",
           "Die Partitur ist der Plan, den das Modell vor dem Klang geschrieben hat; der Titel folgt ihr (Tonart, Tempo, Akkorde).",
           "Datei öffnen", "Partitur ansehen…", "Titel ohne Partitur erstellt (Modus „Ohne Partitur“ oder vor Version 1.0.87)."],
    "pt": ["Dó", "Ré", "Mi", "Fá", "Sol", "Lá", "Si", "sustenido", "bemol", "menor", "maior", "Partitura",
           "Sem partitura para esta faixa: foi criada no modo «Sem partitura» ou antes da versão 1.0.87.",
           "Tonalidade: %s (%s)", "Andamento: %d BPM", "Compasso: %s", "Secção", "Acordes", "Partitura completa (texto ABC)",
           "A partitura é o plano que o modelo escreveu antes de compor o som; a faixa segue-a (tonalidade, andamento, acordes).",
           "Abrir o ficheiro", "Ver a partitura…", "Faixa criada sem partitura (modo «Sem partitura» ou antes da versão 1.0.87)."],
    "ja": LETTERS + ["シャープ", "フラット", "マイナー", "メジャー", "楽譜",
                     "この曲には楽譜がありません：「楽譜なし」モード、または 1.0.87 より前に作成されました。",
                     "キー：%s（%s）", "テンポ：%d BPM", "拍子：%s", "セクション", "コード", "楽譜全体（ABC テキスト）",
                     "楽譜はモデルが音を作る前に書いた設計図で、曲はそれに従います（キー、テンポ、コード）。",
                     "ファイルを開く", "楽譜を見る…", "楽譜なしで作成された曲（「楽譜なし」モード、または 1.0.87 より前）。"],
    "ko": LETTERS + ["샵", "플랫", "단조", "장조", "악보",
                     "이 곡에는 악보가 없습니다: \"악보 없음\" 모드 또는 1.0.87 이전에 만들어졌습니다.",
                     "조성: %s (%s)", "템포: %d BPM", "박자: %s", "섹션", "코드", "전체 악보(ABC 텍스트)",
                     "악보는 모델이 소리를 만들기 전에 쓴 설계도이며, 곡은 이를 따릅니다(조성, 템포, 코드).",
                     "파일 열기", "악보 보기…", "악보 없이 만든 곡(\"악보 없음\" 모드 또는 1.0.87 이전)."],
    "zh-Hans": LETTERS + ["升", "降", "小调", "大调", "乐谱",
                          "此曲目没有乐谱：它是以「无乐谱」模式或在 1.0.87 版之前创作的。",
                          "调性：%s（%s）", "速度：%d BPM", "拍号：%s", "段落", "和弦", "完整乐谱（ABC 文本）",
                          "乐谱是模型在生成声音之前写下的计划；曲目会遵循它（调性、速度、和弦）。",
                          "打开文件", "查看乐谱…", "无乐谱创作的曲目（「无乐谱」模式或 1.0.87 版之前）。"],
    "yue": LETTERS + ["升", "降", "小調", "大調", "樂譜",
                      "呢首歌冇樂譜：佢係用「無樂譜」模式或者喺 1.0.87 版之前整嘅。",
                      "調性：%s（%s）", "速度：%d BPM", "拍號：%s", "段落", "和弦", "完整樂譜（ABC 文字）",
                      "樂譜係模型整聲音之前寫低嘅計劃；首歌會跟住佢（調性、速度、和弦）。",
                      "打開檔案", "睇樂譜…", "冇樂譜整嘅歌（「無樂譜」模式或者 1.0.87 版之前）。"],
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
