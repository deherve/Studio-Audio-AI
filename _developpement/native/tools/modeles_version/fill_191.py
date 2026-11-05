# Traductions de la 1.0.91 (Atelier partition). Usage : python fill_191.py <ressources\langues>
import json, sys
from pathlib import Path

K = ["L'Atelier partition a besoin du moteur Microsoft Edge WebView2 (fourni avec Windows 10 et 11). Installez « WebView2 Runtime » depuis le site de Microsoft.",
     "Imprimer…", "Exporter en PDF…", "Agrandir", "Réduire", "Remarques de lecture de la partition :", "Export PDF impossible.",
     "Partition enregistrée en PDF : %s", "Atelier partition", "Impossible d'ouvrir l'Atelier partition.", "Exporter la partition en PDF",
     "Voir en notes…", "Ouvre l'Atelier partition : la partition en notes, à écouter, imprimer ou enregistrer en PDF."]
T = {
    "en": ["The Score workshop needs the Microsoft Edge WebView2 engine (included with Windows 10 and 11). Install “WebView2 Runtime” from Microsoft's website.",
           "Print…", "Export to PDF…", "Zoom in", "Zoom out", "Notes while reading the score:", "PDF export failed.",
           "Score saved as PDF: %s", "Score workshop", "Could not open the Score workshop.", "Export the score to PDF",
           "View as notes…", "Opens the Score workshop: the score as notes, to listen to, print or save as PDF."],
    "es": ["El Taller de partitura necesita el motor Microsoft Edge WebView2 (incluido en Windows 10 y 11). Instale «WebView2 Runtime» desde el sitio de Microsoft.",
           "Imprimir…", "Exportar a PDF…", "Ampliar", "Reducir", "Observaciones al leer la partitura:", "No se pudo exportar a PDF.",
           "Partitura guardada en PDF: %s", "Taller de partitura", "No se pudo abrir el Taller de partitura.", "Exportar la partitura a PDF",
           "Ver en notas…", "Abre el Taller de partitura: la partitura en notas, para escuchar, imprimir o guardar en PDF."],
    "it": ["Il Laboratorio partitura richiede il motore Microsoft Edge WebView2 (incluso in Windows 10 e 11). Installa «WebView2 Runtime» dal sito di Microsoft.",
           "Stampa…", "Esporta in PDF…", "Ingrandisci", "Riduci", "Osservazioni nella lettura della partitura:", "Esportazione PDF non riuscita.",
           "Partitura salvata in PDF: %s", "Laboratorio partitura", "Impossibile aprire il Laboratorio partitura.", "Esporta la partitura in PDF",
           "Vedi in note…", "Apre il Laboratorio partitura: la partitura in note, da ascoltare, stampare o salvare in PDF."],
    "de": ["Die Partitur-Werkstatt benötigt die Microsoft-Edge-WebView2-Engine (in Windows 10 und 11 enthalten). Installieren Sie „WebView2 Runtime“ von der Microsoft-Website.",
           "Drucken…", "Als PDF exportieren…", "Vergrößern", "Verkleinern", "Hinweise beim Lesen der Partitur:", "PDF-Export fehlgeschlagen.",
           "Partitur als PDF gespeichert: %s", "Partitur-Werkstatt", "Die Partitur-Werkstatt konnte nicht geöffnet werden.", "Partitur als PDF exportieren",
           "In Noten ansehen…", "Öffnet die Partitur-Werkstatt: die Partitur in Noten, zum Anhören, Drucken oder Speichern als PDF."],
    "pt": ["A Oficina de partitura precisa do motor Microsoft Edge WebView2 (incluído no Windows 10 e 11). Instale o «WebView2 Runtime» a partir do site da Microsoft.",
           "Imprimir…", "Exportar para PDF…", "Ampliar", "Reduzir", "Observações na leitura da partitura:", "Não foi possível exportar para PDF.",
           "Partitura guardada em PDF: %s", "Oficina de partitura", "Não foi possível abrir a Oficina de partitura.", "Exportar a partitura para PDF",
           "Ver em notas…", "Abre a Oficina de partitura: a partitura em notas, para ouvir, imprimir ou guardar em PDF."],
    "ja": ["楽譜工房には Microsoft Edge WebView2 エンジン（Windows 10 と 11 に付属）が必要です。Microsoft のサイトから「WebView2 Runtime」をインストールしてください。",
           "印刷…", "PDF に書き出し…", "拡大", "縮小", "楽譜の読み込みに関する注意：", "PDF の書き出しに失敗しました。",
           "楽譜を PDF に保存しました：%s", "楽譜工房", "楽譜工房を開けませんでした。", "楽譜を PDF に書き出す",
           "音符で見る…", "楽譜工房を開きます：楽譜を音符で表示し、再生、印刷、PDF 保存ができます。"],
    "ko": ["악보 작업실에는 Microsoft Edge WebView2 엔진(Windows 10과 11에 포함)이 필요합니다. Microsoft 사이트에서 \"WebView2 Runtime\"을 설치하세요.",
           "인쇄…", "PDF로 내보내기…", "확대", "축소", "악보 읽기 참고 사항:", "PDF 내보내기에 실패했습니다.",
           "악보를 PDF로 저장했습니다: %s", "악보 작업실", "악보 작업실을 열 수 없습니다.", "악보를 PDF로 내보내기",
           "음표로 보기…", "악보 작업실을 엽니다: 악보를 음표로 보고 듣고 인쇄하거나 PDF로 저장합니다."],
    "zh-Hans": ["乐谱工坊需要 Microsoft Edge WebView2 引擎（Windows 10 和 11 自带）。请从 Microsoft 网站安装「WebView2 Runtime」。",
                "打印…", "导出为 PDF…", "放大", "缩小", "读取乐谱时的提示：", "PDF 导出失败。",
                "乐谱已保存为 PDF：%s", "乐谱工坊", "无法打开乐谱工坊。", "将乐谱导出为 PDF",
                "以音符查看…", "打开乐谱工坊：以音符显示乐谱，可播放、打印或保存为 PDF。"],
    "yue": ["樂譜工坊需要 Microsoft Edge WebView2 引擎（Windows 10 同 11 已附帶）。請喺 Microsoft 網站安裝「WebView2 Runtime」。",
            "列印…", "匯出做 PDF…", "放大", "縮細", "讀樂譜時嘅提示：", "匯出 PDF 失敗。",
            "樂譜已經存做 PDF：%s", "樂譜工坊", "開唔到樂譜工坊。", "將樂譜匯出做 PDF",
            "用音符睇…", "打開樂譜工坊：用音符顯示樂譜，可以播放、列印或者存做 PDF。"],
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
