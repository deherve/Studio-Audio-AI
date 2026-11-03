# Traductions de la 1.0.77 (pastille de mise à jour). Usage : python fill_177.py <ressources\langues>
import json, sys
from pathlib import Path

BTN = "Mettre à jour (%s)"
TIP = "Version installée : %s · nouvelle version : %s\nOuvre la page de la dernière version sur GitHub (fichiers à télécharger)."
T = {
    "en": ("Update (%s)", "Installed version: %s · new version: %s\nOpens the latest version's page on GitHub (files to download)."),
    "es": ("Actualizar (%s)", "Versión instalada: %s · nueva versión: %s\nAbre la página de la última versión en GitHub (archivos para descargar)."),
    "it": ("Aggiorna (%s)", "Versione installata: %s · nuova versione: %s\nApre la pagina dell'ultima versione su GitHub (file da scaricare)."),
    "de": ("Aktualisieren (%s)", "Installierte Version: %s · neue Version: %s\nÖffnet die Seite der neuesten Version auf GitHub (Dateien zum Herunterladen)."),
    "pt": ("Atualizar (%s)", "Versão instalada: %s · nova versão: %s\nAbre a página da versão mais recente no GitHub (ficheiros para transferir)."),
    "ja": ("更新 (%s)", "インストール済み：%s · 新しいバージョン：%s\nGitHub の最新バージョンのページを開きます（ダウンロード用ファイル）。"),
    "ko": ("업데이트 (%s)", "설치된 버전: %s · 새 버전: %s\nGitHub의 최신 버전 페이지를 엽니다 (다운로드할 파일)."),
    "zh-Hans": ("更新 (%s)", "已安装版本：%s · 新版本：%s\n打开 GitHub 上最新版本的页面（可下载的文件）。"),
    "yue": ("更新 (%s)", "已安裝版本：%s · 新版本：%s\n打開 GitHub 上最新版本嘅頁面（可下載嘅檔案）。"),
}
langues = Path(sys.argv[1])
for code, (btn, tip) in T.items():
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    d["strings"][BTN], d["strings"][TIP] = btn, tip
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")
