# Traductions de la 1.0.92 (exports MIDI, MusicXML, ABC). Usage : python fill_192.py <ressources\langues>
import json, sys
from pathlib import Path

K = ["Export impossible :", "Fichier MIDI : les notes et le tempo, pour un logiciel de musique ou un clavier.",
     "Fichier MusicXML : la partition complète (notes, mesures, accords) pour MuseScore, Finale, Sibelius, Dorico…",
     "Fichier ABC : la partition au format texte du moteur YuE2.", "Partition enregistrée : %s", "Enregistrement impossible.",
     "Export impossible : fichier vide.", "Exporter la partition"]
T = {
    "en": ["Export failed:", "MIDI file: the notes and tempo, for music software or a keyboard.",
           "MusicXML file: the full score (notes, measures, chords) for MuseScore, Finale, Sibelius, Dorico…",
           "ABC file: the score in the YuE2 engine's text format.", "Score saved: %s", "Could not save the file.", "Export failed: empty file.", "Export the score"],
    "es": ["Exportación imposible:", "Archivo MIDI: las notas y el tempo, para un programa de música o un teclado.",
           "Archivo MusicXML: la partitura completa (notas, compases, acordes) para MuseScore, Finale, Sibelius, Dorico…",
           "Archivo ABC: la partitura en el formato de texto del motor YuE2.", "Partitura guardada: %s", "No se pudo guardar.", "Exportación imposible: archivo vacío.", "Exportar la partitura"],
    "it": ["Esportazione impossibile:", "File MIDI: le note e il tempo, per un software musicale o una tastiera.",
           "File MusicXML: la partitura completa (note, battute, accordi) per MuseScore, Finale, Sibelius, Dorico…",
           "File ABC: la partitura nel formato testo del motore YuE2.", "Partitura salvata: %s", "Salvataggio impossibile.", "Esportazione impossibile: file vuoto.", "Esporta la partitura"],
    "de": ["Export nicht möglich:", "MIDI-Datei: Noten und Tempo, für Musiksoftware oder ein Keyboard.",
           "MusicXML-Datei: die vollständige Partitur (Noten, Takte, Akkorde) für MuseScore, Finale, Sibelius, Dorico…",
           "ABC-Datei: die Partitur im Textformat der YuE2-Engine.", "Partitur gespeichert: %s", "Speichern nicht möglich.", "Export nicht möglich: leere Datei.", "Partitur exportieren"],
    "pt": ["Exportação impossível:", "Ficheiro MIDI: as notas e o andamento, para um programa de música ou um teclado.",
           "Ficheiro MusicXML: a partitura completa (notas, compassos, acordes) para MuseScore, Finale, Sibelius, Dorico…",
           "Ficheiro ABC: a partitura no formato de texto do motor YuE2.", "Partitura guardada: %s", "Não foi possível guardar.", "Exportação impossível: ficheiro vazio.", "Exportar a partitura"],
    "ja": ["書き出せません：", "MIDI ファイル：音符とテンポ。音楽ソフトやキーボード用。",
           "MusicXML ファイル：楽譜全体（音符、小節、コード）。MuseScore、Finale、Sibelius、Dorico など用。",
           "ABC ファイル：YuE2 エンジンのテキスト形式の楽譜。", "楽譜を保存しました：%s", "保存できませんでした。", "書き出せません：ファイルが空です。", "楽譜を書き出す"],
    "ko": ["내보낼 수 없음:", "MIDI 파일: 음표와 템포, 음악 소프트웨어나 키보드용.",
           "MusicXML 파일: 전체 악보(음표, 마디, 코드), MuseScore, Finale, Sibelius, Dorico 등용.",
           "ABC 파일: YuE2 엔진의 텍스트 형식 악보.", "악보 저장됨: %s", "저장할 수 없습니다.", "내보낼 수 없음: 빈 파일.", "악보 내보내기"],
    "zh-Hans": ["无法导出：", "MIDI 文件：音符和速度，适用于音乐软件或键盘。",
                "MusicXML 文件：完整乐谱（音符、小节、和弦），适用于 MuseScore、Finale、Sibelius、Dorico 等。",
                "ABC 文件：YuE2 引擎文本格式的乐谱。", "乐谱已保存：%s", "无法保存。", "无法导出：文件为空。", "导出乐谱"],
    "yue": ["匯出唔到：", "MIDI 檔案：音符同速度，俾音樂軟件或者鍵盤用。",
            "MusicXML 檔案：完整樂譜（音符、小節、和弦），俾 MuseScore、Finale、Sibelius、Dorico 等用。",
            "ABC 檔案：YuE2 引擎文字格式嘅樂譜。", "樂譜已經儲存：%s", "儲存唔到。", "匯出唔到：檔案係空嘅。", "匯出樂譜"],
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
