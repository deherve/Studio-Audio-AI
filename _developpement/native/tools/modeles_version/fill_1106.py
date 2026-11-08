# Traductions de la 1.1.006 (refaire à partir d'ici). Usage : python fill_1106.py <ressources\langues>
# Ordre des langues : en, es, it, de, pt, ja, ko, zh-Hans, yue. Écrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "Partie refaite": ["Part redone", "Parte rehecha", "Parte rifatta", "Teil neu gemacht", "Parte refeita", "一部作り直し", "일부 다시 만듦", "部分重做", "部分重做"],
 "Refait à partir de %d:%02d de « %s »": ["Redone from %d:%02d of “%s”", "Rehecha desde %d:%02d de «%s»", "Rifatta da %d:%02d di «%s»", "Ab %d:%02d neu gemacht aus „%s“",
                                           "Refeita a partir de %d:%02d de «%s»", "%d:%02d から作り直し（「%s」）", "%d:%02d부터 다시 만듦(“%s”)", "从 %d:%02d 起重做（“%s”）", "由 %d:%02d 開始重做（「%s」）"],
 "Refaire à partir d'ici…": ["Redo from here…", "Rehacer desde aquí…", "Rifai da qui…", "Ab hier neu machen…", "Refazer a partir daqui…", "ここから作り直す…", "여기부터 다시 만들기…", "从此处重做…", "由呢度重做…"],
 "Garde le début du morceau et recompose tout ce qui suit la section choisie (nouveau morceau).": [
  "Keeps the start of the track and recomposes everything after the chosen section (new track).",
  "Conserva el principio de la pista y recompone todo lo que sigue a la sección elegida (nueva pista).",
  "Mantiene l'inizio del brano e ricompone tutto ciò che segue la sezione scelta (nuovo brano).",
  "Behält den Anfang des Titels und komponiert alles nach dem gewählten Abschnitt neu (neuer Titel).",
  "Mantém o início da faixa e recompõe tudo o que vem depois da secção escolhida (nova faixa).",
  "曲の冒頭はそのままにして、選んだセクション以降をすべて作曲し直します（新しい曲）。",
  "곡의 앞부분은 그대로 두고 선택한 섹션 이후를 모두 다시 작곡합니다(새 곡).",
  "保留曲目开头，重新创作所选段落之后的全部内容（新曲目）。",
  "保留首歌開頭，重新創作揀咗嗰段之後嘅全部內容（新歌）。"],
 "Refaire à partir d'ici": ["Redo from here", "Rehacer desde aquí", "Rifai da qui", "Ab hier neu machen", "Refazer a partir daqui", "ここから作り直す", "여기부터 다시 만들기", "从此处重做", "由呢度重做"],
 "Le début du morceau est gardé tel quel ; le moteur recompose tout ce qui suit le point choisi (même style, mêmes réglages). Le résultat est un nouveau morceau : l'original ne change pas.": [
  "The start of the track is kept as it is; the engine recomposes everything after the chosen point (same style, same settings). The result is a new track: the original does not change.",
  "El principio de la pista se conserva tal cual; el motor recompone todo lo que sigue al punto elegido (mismo estilo, mismos ajustes). El resultado es una pista nueva: la original no cambia.",
  "L'inizio del brano resta com'è; il motore ricompone tutto ciò che segue il punto scelto (stesso stile, stesse impostazioni). Il risultato è un nuovo brano: l'originale non cambia.",
  "Der Anfang des Titels bleibt unverändert; die Engine komponiert alles nach dem gewählten Punkt neu (gleicher Stil, gleiche Einstellungen). Das Ergebnis ist ein neuer Titel: das Original bleibt unverändert.",
  "O início da faixa é mantido tal como está; o motor recompõe tudo o que vem depois do ponto escolhido (mesmo estilo, mesmas definições). O resultado é uma nova faixa: a original não muda.",
  "曲の冒頭はそのまま残り、選んだ位置以降をエンジンが作曲し直します（同じスタイル・同じ設定）。結果は新しい曲になり、元の曲は変わりません。",
  "곡의 앞부분은 그대로 유지되고, 선택한 지점 이후를 엔진이 다시 작곡합니다(같은 스타일, 같은 설정). 결과는 새 곡이며 원곡은 바뀌지 않습니다.",
  "曲目开头保持不变；引擎会重新创作所选位置之后的全部内容（相同风格、相同设置）。结果是一首新曲目，原曲不变。",
  "首歌開頭保持唔變；引擎會重新創作揀咗嘅位置之後嘅全部內容（同一風格、同一設定）。結果係一首新歌，原曲唔會變。"],
 "Refaire à partir de :": ["Redo from:", "Rehacer desde:", "Rifai da:", "Neu machen ab:", "Refazer a partir de:", "作り直す位置：", "다시 만들 위치:", "重做起点：", "重做起點："],
 "soit %d:%02d sur %s": ["that is %d:%02d of %s", "es decir %d:%02d de %s", "cioè %d:%02d su %s", "also %d:%02d von %s", "ou seja %d:%02d de %s", "%d:%02d（全体 %s）", "%d:%02d (전체 %s)", "即 %d:%02d（共 %s）", "即係 %d:%02d（共 %s）"],
 "Calage des paroles sur le chant (Whisper)…": ["Aligning the lyrics with the singing (Whisper)…", "Ajustando la letra al canto (Whisper)…", "Allineamento del testo al canto (Whisper)…",
                                                "Text wird an den Gesang angeglichen (Whisper)…", "A alinhar a letra com o canto (Whisper)…", "歌詞を歌声に合わせています（Whisper）…",
                                                "가사를 노래에 맞추는 중(Whisper)…", "正在将歌词与演唱对齐（Whisper）…", "正喺度將歌詞對準歌聲（Whisper）…"],
 "Pour choisir une section, les paroles doivent être calées sur le chant (fichier paroles.lrc, environ 30 s).": [
  "To choose a section, the lyrics must be aligned with the singing (paroles.lrc file, about 30 s).",
  "Para elegir una sección, la letra debe estar ajustada al canto (archivo paroles.lrc, unos 30 s).",
  "Per scegliere una sezione, il testo deve essere allineato al canto (file paroles.lrc, circa 30 s).",
  "Um einen Abschnitt zu wählen, muss der Text an den Gesang angeglichen sein (Datei paroles.lrc, etwa 30 s).",
  "Para escolher uma secção, a letra tem de estar alinhada com o canto (ficheiro paroles.lrc, cerca de 30 s).",
  "セクションを選ぶには、歌詞を歌声に合わせる必要があります（paroles.lrc ファイル、約 30 秒）。",
  "섹션을 고르려면 가사를 노래에 맞춰야 합니다(paroles.lrc 파일, 약 30초).",
  "要选择段落，歌词需先与演唱对齐（paroles.lrc 文件，约 30 秒）。",
  "要揀段落，歌詞要先對準歌聲（paroles.lrc 檔案，大約 30 秒）。"],
 "Caler les paroles": ["Align the lyrics", "Ajustar la letra", "Allinea il testo", "Text angleichen", "Alinhar a letra", "歌詞を合わせる", "가사 맞추기", "对齐歌词", "對準歌詞"],
 "Refaire à partir de la section :": ["Redo from the section:", "Rehacer desde la sección:", "Rifai dalla sezione:", "Neu machen ab Abschnitt:", "Refazer a partir da secção:",
                                     "作り直すセクション：", "다시 만들 섹션:", "从以下段落重做：", "由以下段落重做："],
 "%s  —  %d:%02d": ["%s  —  %d:%02d"] * 9,
 "Paroles chantées à partir de ce point :": ["Lyrics sung from this point:", "Letra cantada a partir de este punto:", "Testo cantato da questo punto:", "Ab hier gesungener Text:",
                                             "Letra cantada a partir deste ponto:", "この位置から歌われる歌詞：", "이 지점부터 부를 가사:", "从此处开始演唱的歌词：", "由呢度開始唱嘅歌詞："],
 "Refaire": ["Redo", "Rehacer", "Rifai", "Neu machen", "Refazer", "作り直す", "다시 만들기", "重做", "重做"],
 "« %s » : nouvelle version à partir de %d:%02d ajoutée à la file.": [
  "“%s”: new version from %d:%02d added to the queue.", "«%s»: nueva versión desde %d:%02d añadida a la cola.", "«%s»: nuova versione da %d:%02d aggiunta alla coda.",
  "„%s“: neue Version ab %d:%02d zur Warteschlange hinzugefügt.", "«%s»: nova versão a partir de %d:%02d adicionada à fila.", "「%s」：%d:%02d からの新しいバージョンをキューに追加しました。",
  "“%s”: %d:%02d부터의 새 버전을 대기열에 추가했습니다.", "“%s”：从 %d:%02d 起的新版本已加入队列。", "「%s」：由 %d:%02d 開始嘅新版本已經加入隊列。"],
 " (refait depuis %d:%02d)": [" (redone from %d:%02d)", " (rehecha desde %d:%02d)", " (rifatta da %d:%02d)", " (ab %d:%02d neu)", " (refeita desde %d:%02d)",
                              "（%d:%02d から作り直し）", " (%d:%02d부터 다시 만듦)", "（从 %d:%02d 起重做）", "（由 %d:%02d 開始重做）"],
}
langues = Path(sys.argv[1])
for i, code in enumerate(L):
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    for k, v in T.items():
        assert len(v) == 9, k
        d["strings"][k] = v[i]
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print(len(T), "OK")
