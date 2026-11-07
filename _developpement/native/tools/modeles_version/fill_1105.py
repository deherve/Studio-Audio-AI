# Traductions de la 1.1.005 (disposition B : aide intégrée). Usage : python fill_1105.py <ressources\langues>
# Ordre des langues : en, es, it, de, pt, ja, ko, zh-Hans, yue. Écrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "Page précédente": ["Previous page", "Página anterior", "Pagina precedente", "Vorherige Seite", "Página anterior", "前のページ", "이전 페이지", "上一页", "上一頁"],
 "Page suivante": ["Next page", "Página siguiente", "Pagina successiva", "Nächste Seite", "Página seguinte", "次のページ", "다음 페이지", "下一页", "下一頁"],
 "Sommaire": ["Contents", "Índice", "Sommario", "Inhalt", "Índice", "目次", "목차", "目录", "目錄"],
 "Demander à l'IA": ["Ask the AI", "Preguntar a la IA", "Chiedi all'IA", "KI fragen", "Perguntar à IA", "AIに質問", "AI에게 묻기", "询问 AI", "問 AI"],
 "Rubriques :": ["Sections:", "Secciones:", "Sezioni:", "Abschnitte:", "Secções:", "項目：", "항목:", "章节：", "章節："],
 "L'aide intégrée a besoin du moteur Microsoft Edge WebView2 (fourni avec Windows 10 et 11).": [
  "The built-in help needs the Microsoft Edge WebView2 engine (included with Windows 10 and 11).",
  "La ayuda integrada necesita el motor Microsoft Edge WebView2 (incluido en Windows 10 y 11).",
  "La guida integrata richiede il motore Microsoft Edge WebView2 (incluso in Windows 10 e 11).",
  "Die integrierte Hilfe benötigt die Microsoft Edge WebView2-Engine (in Windows 10 und 11 enthalten).",
  "A ajuda integrada precisa do motor Microsoft Edge WebView2 (incluído no Windows 10 e 11).",
  "内蔵ヘルプには Microsoft Edge WebView2 エンジン（Windows 10・11 に付属）が必要です。",
  "내장 도움말에는 Microsoft Edge WebView2 엔진(Windows 10 및 11에 포함)이 필요합니다.",
  "内置帮助需要 Microsoft Edge WebView2 引擎（Windows 10 和 11 自带）。",
  "內置說明需要 Microsoft Edge WebView2 引擎（Windows 10 同 11 有附帶）。"],
 "Ouvrir l'aide dans le navigateur": ["Open the help in the browser", "Abrir la ayuda en el navegador", "Apri la guida nel browser", "Hilfe im Browser öffnen",
                                      "Abrir a ajuda no navegador", "ブラウザーでヘルプを開く", "브라우저에서 도움말 열기", "在浏览器中打开帮助", "喺瀏覽器打開說明"],
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
