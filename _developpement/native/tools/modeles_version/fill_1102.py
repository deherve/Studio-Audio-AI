# Traductions de la 1.1.002 (ambiances Nuit / Console / Clair, disposition A). Usage : python fill_1102.py <ressources\langues>
# Ordre des langues : en, es, it, de, pt, ja, ko, zh-Hans, yue. Écrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "Nuit": ["Night", "Noche", "Notte", "Nacht", "Noite", "ナイト", "나이트", "夜间", "夜間"],
 "Console": ["Console", "Consola", "Console", "Konsole", "Consola", "コンソール", "콘솔", "调音台", "調音台"],
 "Clair": ["Light", "Claro", "Chiaro", "Hell", "Claro", "ライト", "라이트", "浅色", "淺色"],
 "sombre sobre, une seule couleur d'action (menthe)": [
  "sober dark, a single action colour (mint)", "oscuro sobrio, un solo color de acción (menta)", "scuro sobrio, un solo colore d'azione (menta)",
  "schlichtes Dunkel, eine einzige Aktionsfarbe (Minze)", "escuro sóbrio, uma só cor de ação (menta)", "落ち着いたダーク、操作色は1色（ミント）",
  "차분한 어두운 테마, 동작 색은 하나(민트)", "素雅深色，仅一种操作色（薄荷绿）", "素雅深色，得一種操作色（薄荷綠）"],
 "graphite chaud façon table de mixage, ambre": [
  "warm graphite like a mixing desk, amber", "grafito cálido como una mesa de mezclas, ámbar", "grafite caldo come un mixer, ambra",
  "warmes Graphit wie ein Mischpult, Bernstein", "grafite quente como uma mesa de mistura, âmbar", "ミキシング卓のような温かいグラファイト、アンバー",
  "믹싱 콘솔 같은 따뜻한 흑연색, 호박색", "调音台般的暖石墨色，琥珀色", "好似調音台嘅暖石墨色，琥珀色"],
 "clair sobre pour le plein jour, bleu": [
  "sober light for daytime, blue", "claro sobrio para el día, azul", "chiaro sobrio per il giorno, blu", "schlichtes Hell für den Tag, Blau",
  "claro sóbrio para o dia, azul", "昼間向けの落ち着いたライト、ブルー", "낮 시간용 차분한 밝은 테마, 파랑", "适合白天的素雅浅色，蓝色", "啱日頭用嘅素雅淺色，藍色"],
 "Télécharger ▾": ["Download ▾", "Descargar ▾", "Scarica ▾", "Herunterladen ▾", "Transferir ▾", "ダウンロード ▾", "다운로드 ▾", "下载 ▾", "下載 ▾"],
 "Refaire ▾": ["Redo ▾", "Rehacer ▾", "Rifai ▾", "Neu machen ▾", "Refazer ▾", "作り直す ▾", "다시 만들기 ▾", "重做 ▾", "重做 ▾"],
 "Vidéos ▾": ["Videos ▾", "Vídeos ▾", "Video ▾", "Videos ▾", "Vídeos ▾", "動画 ▾", "동영상 ▾", "视频 ▾", "影片 ▾"],
 "Plus ▾": ["More ▾", "Más ▾", "Altro ▾", "Mehr ▾", "Mais ▾", "その他 ▾", "더 보기 ▾", "更多 ▾", "更多 ▾"],
 "Outils audio, partition, infos du morceau, suppression.": [
  "Audio tools, score, track info, deletion.", "Herramientas de audio, partitura, información de la pista, eliminación.",
  "Strumenti audio, spartito, informazioni sul brano, eliminazione.", "Audio-Werkzeuge, Partitur, Titelinfos, Löschen.",
  "Ferramentas de áudio, partitura, informações da faixa, eliminação.", "オーディオツール、楽譜、曲の情報、削除。",
  "오디오 도구, 악보, 곡 정보, 삭제.", "音频工具、乐谱、曲目信息、删除。", "音訊工具、樂譜、歌曲資料、刪除。"],
 "File de génération vide": ["Generation queue empty", "Cola de generación vacía", "Coda di generazione vuota", "Generierungswarteschlange leer",
                             "Fila de geração vazia", "生成キューは空です", "생성 대기열 비어 있음", "生成队列为空", "生成隊列冇嘢"],
 "Options ▾": ["Options ▾", "Opciones ▾", "Opzioni ▾", "Optionen ▾", "Opções ▾", "オプション ▾", "옵션 ▾", "选项 ▾", "選項 ▾"],
 "Ce qu'il faut faire quand la file est terminée, contrôle de la tonalité et du tempo.": [
  "What to do when the queue is finished, key and tempo check.", "Qué hacer cuando termine la cola, control de tonalidad y tempo.",
  "Cosa fare quando la coda è finita, controllo di tonalità e tempo.", "Was nach Ende der Warteschlange passiert, Tonart- und Tempoprüfung.",
  "O que fazer quando a fila terminar, controlo da tonalidade e do andamento.", "キュー終了時の動作、キーとテンポの確認。",
  "대기열이 끝났을 때 할 일, 조성과 템포 확인.", "队列完成后的操作，调性与速度检查。", "隊列完咗之後做乜，調性同速度檢查。"],
 "Replier ▴": ["Collapse ▴", "Plegar ▴", "Comprimi ▴", "Einklappen ▴", "Recolher ▴", "折りたたむ ▴", "접기 ▴", "收起 ▴", "收埋 ▴"],
 "Disposition": ["Layout", "Disposición", "Disposizione", "Anordnung", "Disposição", "レイアウト", "배치", "布局", "版面"],
 "Actuelle": ["Current", "Actual", "Attuale", "Aktuell", "Atual", "現在", "현재", "当前", "而家嗰個"],
 "A : rangée": ["A: tidy", "A: ordenada", "A: ordinata", "A: aufgeräumt", "A: arrumada", "A：整理済み", "A: 정돈형", "A：整理版", "A：整理版"],
 "A : bouton Générer toujours visible en bas du formulaire, file de génération repliée quand elle est vide, lecteur fixe en bas de la fenêtre, cartes avec 4 boutons et 3 menus (Télécharger, Refaire, Vidéos) au lieu de 14 boutons. La disposition B (menu à gauche) viendra dans une prochaine version.": [
  "A: Generate button always visible at the bottom of the form, generation queue collapsed when empty, fixed player at the bottom of the window, cards with 4 buttons and 3 menus (Download, Redo, Videos) instead of 14 buttons. Layout B (menu on the left) will come in a future version.",
  "A: botón Generar siempre visible al pie del formulario, cola de generación plegada cuando está vacía, reproductor fijo al pie de la ventana, tarjetas con 4 botones y 3 menús (Descargar, Rehacer, Vídeos) en lugar de 14 botones. La disposición B (menú a la izquierda) llegará en una próxima versión.",
  "A: pulsante Genera sempre visibile in fondo al modulo, coda di generazione compressa quando è vuota, lettore fisso in fondo alla finestra, schede con 4 pulsanti e 3 menu (Scarica, Rifai, Video) invece di 14 pulsanti. La disposizione B (menu a sinistra) arriverà in una prossima versione.",
  "A: Schaltfläche Generieren immer unten im Formular sichtbar, Generierungswarteschlange eingeklappt, wenn sie leer ist, fester Player unten im Fenster, Karten mit 4 Schaltflächen und 3 Menüs (Herunterladen, Neu machen, Videos) statt 14 Schaltflächen. Die Anordnung B (Menü links) folgt in einer kommenden Version.",
  "A: botão Gerar sempre visível no fundo do formulário, fila de geração recolhida quando vazia, leitor fixo no fundo da janela, cartões com 4 botões e 3 menus (Transferir, Refazer, Vídeos) em vez de 14 botões. A disposição B (menu à esquerda) chegará numa próxima versão.",
  "A：生成ボタンをフォーム下部に常に表示、空の生成キューは折りたたみ、ウィンドウ下部に固定プレーヤー、カードは14個のボタンの代わりに4つのボタンと3つのメニュー（ダウンロード、作り直す、動画）。レイアウトB（左側メニュー）は今後のバージョンで追加予定です。",
  "A: 생성 버튼이 양식 아래에 항상 표시, 비어 있는 생성 대기열은 접힘, 창 아래에 고정 플레이어, 카드는 14개 버튼 대신 버튼 4개와 메뉴 3개(다운로드, 다시 만들기, 동영상). 배치 B(왼쪽 메뉴)는 다음 버전에서 제공됩니다.",
  "A：“生成”按钮始终显示在表单底部，生成队列为空时收起，窗口底部固定播放器，卡片用 4 个按钮和 3 个菜单（下载、重做、视频）代替 14 个按钮。布局 B（左侧菜单）将在以后的版本中提供。",
  "A：「生成」掣一直喺表格底部，生成隊列冇嘢時收埋，視窗底部有固定播放器，卡片用 4 個掣同 3 個選單（下載、重做、影片）代替 14 個掣。版面 B（左邊選單）會喺之後嘅版本推出。"],
 "Aucun morceau en lecture : « ▶ Lecture » sur une carte pour écouter.": [
  "Nothing playing: “▶ Play” on a card to listen.", "Nada en reproducción: «▶ Reproducir» en una tarjeta para escuchar.",
  "Nessun brano in riproduzione: «▶ Riproduci» su una scheda per ascoltare.", "Keine Wiedergabe: „▶ Wiedergabe“ auf einer Karte zum Anhören.",
  "Nada a tocar: «▶ Reproduzir» num cartão para ouvir.", "再生中の曲はありません：カードの「▶ 再生」で聴けます。",
  "재생 중인 곡 없음: 카드의 “▶ 재생”으로 들어 보세요.", "没有正在播放的曲目：点击卡片上的“▶ 播放”收听。", "冇歌喺度播：撳卡片上面嘅「▶ 播放」嚟聽。"],
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
