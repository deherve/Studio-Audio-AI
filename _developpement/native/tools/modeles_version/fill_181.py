# Traductions de la 1.0.81 (sauvegarde automatique, vérification au démarrage). Usage : python fill_181.py <ressources\langues>
import json, sys
from pathlib import Path

K = ["Sauvegarde automatique : aucune pour l'instant.",
     "Sauvegarde automatique : %s (%d gardées)",
     "Ouvrir les sauvegardes",
     "● Vérification au démarrage : aucun fichier manquant.",
     "Disparu depuis la sauvegarde du %s : modeles\\%s",
     "Le style « %s » utilise une LoRA absente : %s",
     "Vérification au démarrage",
     "Des fichiers attendus sont introuvables. Vos réglages, vos styles, la liste des LoRA et la fiche de chaque morceau sont sauvegardés chaque jour dans le dossier « sauvegardes » du programme.",
     "Ouvrir le dossier des modèles"]
T = {
    "en": ["Automatic backup: none yet.", "Automatic backup: %s (%d kept)", "Open backups", "● Startup check: no missing file.",
           "Missing since the %s backup: modeles\\%s", "Style “%s” uses a missing LoRA: %s", "Startup check",
           "Some expected files cannot be found. Your settings, styles, LoRA list and each track's sheet are backed up every day in the program's “sauvegardes” folder.",
           "Open the models folder"],
    "es": ["Copia automática: ninguna por ahora.", "Copia automática: %s (%d guardadas)", "Abrir las copias", "● Comprobación al iniciar: ningún archivo falta.",
           "Desaparecido desde la copia del %s: modeles\\%s", "El estilo «%s» usa un LoRA ausente: %s", "Comprobación al iniciar",
           "No se encuentran algunos archivos esperados. Sus ajustes, estilos, la lista de LoRA y la ficha de cada tema se guardan cada día en la carpeta «sauvegardes» del programa.",
           "Abrir la carpeta de modelos"],
    "it": ["Backup automatico: nessuno per ora.", "Backup automatico: %s (%d conservati)", "Apri i backup", "● Verifica all'avvio: nessun file mancante.",
           "Scomparso dal backup del %s: modeles\\%s", "Lo stile «%s» usa un LoRA assente: %s", "Verifica all'avvio",
           "Alcuni file attesi non si trovano. Impostazioni, stili, elenco dei LoRA e scheda di ogni brano sono salvati ogni giorno nella cartella «sauvegardes» del programma.",
           "Apri la cartella dei modelli"],
    "de": ["Automatische Sicherung: noch keine.", "Automatische Sicherung: %s (%d behalten)", "Sicherungen öffnen", "● Prüfung beim Start: keine Datei fehlt.",
           "Seit der Sicherung vom %s verschwunden: modeles\\%s", "Der Stil „%s“ nutzt ein fehlendes LoRA: %s", "Prüfung beim Start",
           "Einige erwartete Dateien fehlen. Einstellungen, Stile, LoRA-Liste und das Blatt jedes Titels werden täglich im Ordner „sauvegardes“ des Programms gesichert.",
           "Modellordner öffnen"],
    "pt": ["Cópia automática: nenhuma por agora.", "Cópia automática: %s (%d guardadas)", "Abrir as cópias", "● Verificação no arranque: nenhum ficheiro em falta.",
           "Desaparecido desde a cópia de %s: modeles\\%s", "O estilo «%s» usa um LoRA ausente: %s", "Verificação no arranque",
           "Alguns ficheiros esperados não foram encontrados. Definições, estilos, lista de LoRA e a ficha de cada faixa são guardados todos os dias na pasta «sauvegardes» do programa.",
           "Abrir a pasta dos modelos"],
    "ja": ["自動バックアップ：まだありません。", "自動バックアップ：%s（%d 件保持）", "バックアップを開く", "● 起動時チェック：欠けているファイルはありません。",
           "%s のバックアップ以降に消えたファイル：modeles\\%s", "スタイル「%s」が見つからない LoRA を使っています：%s", "起動時チェック",
           "必要なファイルが見つかりません。設定、スタイル、LoRA の一覧、各曲の情報は、プログラムの「sauvegardes」フォルダに毎日バックアップされています。",
           "モデルフォルダを開く"],
    "ko": ["자동 백업: 아직 없음.", "자동 백업: %s (%d개 보관)", "백업 열기", "● 시작 시 점검: 빠진 파일 없음.",
           "%s 백업 이후 사라짐: modeles\\%s", "스타일 \"%s\"가 없는 LoRA를 사용합니다: %s", "시작 시 점검",
           "필요한 파일을 찾을 수 없습니다. 설정, 스타일, LoRA 목록과 각 곡의 정보는 매일 프로그램의 \"sauvegardes\" 폴더에 백업됩니다.",
           "모델 폴더 열기"],
    "zh-Hans": ["自动备份：暂无。", "自动备份：%s（保留 %d 份）", "打开备份", "● 启动检查：没有缺失的文件。",
                "自 %s 的备份以来消失：modeles\\%s", "风格「%s」使用了缺失的 LoRA：%s", "启动检查",
                "找不到某些应有的文件。您的设置、风格、LoRA 列表和每首曲目的信息每天都会备份到程序的「sauvegardes」文件夹中。",
                "打开模型文件夹"],
    "yue": ["自動備份：暫時冇。", "自動備份：%s（保留 %d 份）", "打開備份", "● 啟動檢查：冇缺少檔案。",
            "自 %s 嘅備份之後消失：modeles\\%s", "風格「%s」用咗唔見咗嘅 LoRA：%s", "啟動檢查",
            "搵唔到某啲應該有嘅檔案。你嘅設定、風格、LoRA 清單同每首歌嘅資料每日都會備份喺程式嘅「sauvegardes」資料夾。",
            "打開模型資料夾"],
}
langues = Path(sys.argv[1])
for code, vals in T.items():
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    s = d["strings"]
    for k, v in zip(K, vals):
        if k in s and s[k] and k == "Ouvrir le dossier des modèles":
            continue  # déjà traduit ailleurs
        s[k] = v
        s[k.replace("\\", "\\\\")] = v  # i18n_sync peut garder la barre doublée du code source
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")
