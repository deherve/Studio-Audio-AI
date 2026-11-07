# Traductions de la 1.1.003 (disposition B). Usage : python fill_1103.py <ressources\langues>
# Ordre des langues : en, es, it, de, pt, ja, ko, zh-Hans, yue. Écrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "B : menu à gauche": ["B: menu on the left", "B: menú a la izquierda", "B: menu a sinistra", "B: Menü links", "B: menu à esquerda",
                       "B：左側メニュー", "B: 왼쪽 메뉴", "B：左侧菜单", "B：左邊選單"],
 "A : bouton Générer toujours visible en bas du formulaire, file de génération repliée quand elle est vide, lecteur fixe en bas de la fenêtre, cartes avec 4 boutons et 3 menus (Télécharger, Refaire, Vidéos) au lieu de 14 boutons.": [
  "A: Generate button always visible at the bottom of the form, generation queue collapsed when empty, fixed player at the bottom of the window, cards with 4 buttons and 3 menus (Download, Redo, Videos) instead of 14 buttons.",
  "A: botón Generar siempre visible al pie del formulario, cola de generación plegada cuando está vacía, reproductor fijo al pie de la ventana, tarjetas con 4 botones y 3 menús (Descargar, Rehacer, Vídeos) en lugar de 14 botones.",
  "A: pulsante Genera sempre visibile in fondo al modulo, coda di generazione compressa quando è vuota, lettore fisso in fondo alla finestra, schede con 4 pulsanti e 3 menu (Scarica, Rifai, Video) invece di 14 pulsanti.",
  "A: Schaltfläche Generieren immer unten im Formular sichtbar, Generierungswarteschlange eingeklappt, wenn sie leer ist, fester Player unten im Fenster, Karten mit 4 Schaltflächen und 3 Menüs (Herunterladen, Neu machen, Videos) statt 14 Schaltflächen.",
  "A: botão Gerar sempre visível no fundo do formulário, fila de geração recolhida quando vazia, leitor fixo no fundo da janela, cartões com 4 botões e 3 menus (Transferir, Refazer, Vídeos) em vez de 14 botões.",
  "A：生成ボタンをフォーム下部に常に表示、空の生成キューは折りたたみ、ウィンドウ下部に固定プレーヤー、カードは14個のボタンの代わりに4つのボタンと3つのメニュー（ダウンロード、作り直す、動画）。",
  "A: 생성 버튼이 양식 아래에 항상 표시, 비어 있는 생성 대기열은 접힘, 창 아래에 고정 플레이어, 카드는 14개 버튼 대신 버튼 4개와 메뉴 3개(다운로드, 다시 만들기, 동영상).",
  "A：“生成”按钮始终显示在表单底部，生成队列为空时收起，窗口底部固定播放器，卡片用 4 个按钮和 3 个菜单（下载、重做、视频）代替 14 个按钮。",
  "A：「生成」掣一直喺表格底部，生成隊列冇嘢時收埋，視窗底部有固定播放器，卡片用 4 個掣同 3 個選單（下載、重做、影片）代替 14 個掣。"],
 "B : comme A, avec un menu à gauche ; la création et la bibliothèque occupent chacune toute la largeur.": [
  "B: like A, with a menu on the left; creation and the library each take the full width.",
  "B: como A, con un menú a la izquierda; la creación y la biblioteca ocupan cada una todo el ancho.",
  "B: come A, con un menu a sinistra; la creazione e la libreria occupano ciascuna tutta la larghezza.",
  "B: wie A, mit einem Menü links; Erstellung und Bibliothek nehmen jeweils die ganze Breite ein.",
  "B: como A, com um menu à esquerda; a criação e a biblioteca ocupam cada uma toda a largura.",
  "B：Aと同じで左側にメニュー。作成とライブラリはそれぞれ全幅で表示されます。",
  "B: A와 같고 왼쪽에 메뉴가 있습니다. 만들기와 라이브러리가 각각 전체 너비를 차지합니다.",
  "B：与 A 相同，左侧有菜单；创作和曲库各自占满整个宽度。",
  "B：同 A 一樣，左邊有選單；創作同歌曲庫各自佔晒成個闊度。"],
 "Morceaux (%d)": ["Tracks (%d)", "Pistas (%d)", "Brani (%d)", "Titel (%d)", "Faixas (%d)", "曲 (%d)", "곡 (%d)", "曲目 (%d)", "歌曲 (%d)"],
 "File : %d en cours, %d en attente": ["Queue: %d running, %d waiting", "Cola: %d en curso, %d en espera", "Coda: %d in corso, %d in attesa",
                                       "Warteschlange: %d laufend, %d wartend", "Fila: %d em curso, %d em espera", "キュー：実行中 %d、待機中 %d",
                                       "대기열: 진행 중 %d, 대기 %d", "队列：%d 个进行中，%d 个等待", "隊列：%d 個進行緊，%d 個等緊"],
 "Aide et réglages": ["Help and settings", "Ayuda y ajustes", "Aiuto e impostazioni", "Hilfe und Einstellungen", "Ajuda e definições",
                      "ヘルプと設定", "도움말 및 설정", "帮助与设置", "說明同設定"],
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
