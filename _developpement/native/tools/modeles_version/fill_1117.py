# Traductions de la 1.1.017 (reprise et pause des vidéos). Usage : python fill_1117.py <ressources\langues>
# Ordre : en, es, it, de, pt, ja, ko, zh-Hans, yue. Écrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "Reprendre : ne pas refaire les vidéos déjà créées": [
  "Resume: do not redo the videos already created", "Reanudar: no rehacer los vídeos ya creados", "Riprendi: non rifare i video già creati",
  "Fortsetzen: bereits erstellte Videos nicht neu erzeugen", "Retomar: não refazer os vídeos já criados",
  "再開：作成済みの動画は作り直さない", "이어서 만들기: 이미 만든 동영상은 다시 만들지 않기", "继续：不重新制作已创建的视频", "繼續：唔重新製作已建立嘅影片"],
 "Après une interruption, seules les vidéos qui manquent sont créées. Sans effet si « Refaire le scénario et les images » est coché.": [
  "After an interruption, only the missing videos are created. No effect if “Redo the scenario and the images” is ticked.",
  "Tras una interrupción, solo se crean los vídeos que faltan. Sin efecto si «Rehacer el guion y las imágenes» está marcado.",
  "Dopo un'interruzione vengono creati solo i video mancanti. Nessun effetto se «Rifai sceneggiatura e immagini» è selezionato.",
  "Nach einer Unterbrechung werden nur die fehlenden Videos erstellt. Ohne Wirkung, wenn „Drehbuch und Bilder neu erstellen“ angehakt ist.",
  "Após uma interrupção, só são criados os vídeos que faltam. Sem efeito se «Refazer o roteiro e as imagens» estiver marcado.",
  "中断後は、不足している動画だけを作成します。「シナリオと画像を作り直す」にチェックがある場合は無効です。",
  "중단 후에는 없는 동영상만 만듭니다. '시나리오와 이미지 다시 만들기'를 선택하면 적용되지 않습니다.",
  "中断后只会创建缺少的视频。若勾选“重做剧本和图像”则不起作用。",
  "中斷之後，只會建立欠缺嘅影片。如果剔選咗「重做劇本同圖像」就冇作用。"],
 "%d vidéo(s) déjà faite(s) : ignorée(s).": [
  "%d video(s) already made: skipped.", "%d vídeo(s) ya hecho(s): omitido(s).", "%d video già fatti: saltati.",
  "%d Video(s) bereits vorhanden: übersprungen.", "%d vídeo(s) já feito(s): ignorado(s).",
  "作成済みの動画 %d 件：スキップしました。", "이미 만든 동영상 %d개: 건너뜀.", "已有 %d 个视频：已跳过。", "已有 %d 條影片：已略過。"],
 "Toutes les vidéos demandées existent déjà (décochez « Reprendre » pour les refaire).": [
  "All the requested videos already exist (untick “Resume” to redo them).",
  "Todos los vídeos pedidos ya existen (desmarque «Reanudar» para rehacerlos).",
  "Tutti i video richiesti esistono già (deseleziona «Riprendi» per rifarli).",
  "Alle gewünschten Videos existieren bereits (Häkchen bei „Fortsetzen“ entfernen, um sie neu zu erstellen).",
  "Todos os vídeos pedidos já existem (desmarque «Retomar» para os refazer).",
  "指定された動画はすべて存在します（作り直すには「再開」のチェックを外してください）。",
  "요청한 동영상이 모두 이미 있습니다(다시 만들려면 '이어서 만들기'를 해제하세요).",
  "所需视频都已存在（取消勾选“继续”即可重新制作）。",
  "所有要求嘅影片都已經存在（取消剔選「繼續」就可以重新製作）。"],
 "En pause : carte graphique libérée.": [
  "Paused: graphics card released.", "En pausa: tarjeta gráfica liberada.", "In pausa: scheda grafica liberata.",
  "Pausiert: Grafikkarte freigegeben.", "Em pausa: placa de vídeo libertada.",
  "一時停止中：グラフィックカードを解放しました。", "일시 중지: 그래픽 카드 해제됨.", "已暂停：显卡已释放。", "已暫停：顯示卡已釋放。"],
 "En pause : le travail reprendra au clic sur « Reprendre ».": [
  "Paused: the work will resume when you click “Resume”.", "En pausa: el trabajo continuará al hacer clic en «Reanudar».",
  "In pausa: il lavoro riprenderà al clic su «Riprendi».", "Pausiert: Die Arbeit wird nach Klick auf „Fortsetzen“ weitergeführt.",
  "Em pausa: o trabalho continuará ao clicar em «Retomar».", "一時停止中：「再開」をクリックすると続きから始まります。",
  "일시 중지됨: '이어서 만들기'를 누르면 계속됩니다.", "已暂停：点击“继续”后将接着进行。", "已暫停：㩒「繼續」之後會接住做。"],
 "Reprendre": ["Resume", "Reanudar", "Riprendi", "Fortsetzen", "Retomar", "再開", "재개", "继续", "繼續"],
 "Pause": ["Pause", "Pausa", "Pausa", "Pause", "Pausa", "一時停止", "일시 중지", "暂停", "暫停"],
 "Suspend la création et rend la carte graphique (à la fin de l'image en cours). La création reprend là où elle s'est arrêtée.": [
  "Suspends the creation and releases the graphics card (at the end of the current image). The creation resumes where it stopped.",
  "Suspende la creación y libera la tarjeta gráfica (al terminar la imagen en curso). La creación continúa donde se detuvo.",
  "Sospende la creazione e libera la scheda grafica (al termine dell'immagine in corso). La creazione riprende da dove si era fermata.",
  "Hält die Erstellung an und gibt die Grafikkarte frei (nach dem aktuellen Bild). Die Erstellung wird dort fortgesetzt, wo sie angehalten wurde.",
  "Suspende a criação e liberta a placa de vídeo (no fim da imagem em curso). A criação continua onde parou.",
  "作成を一時停止し、グラフィックカードを解放します（現在の画像の終了時）。作成は中断した場所から再開されます。",
  "생성을 일시 중지하고 그래픽 카드를 해제합니다(현재 이미지가 끝날 때). 생성은 멈춘 곳에서 이어집니다.",
  "暂停制作并释放显卡（在当前图像完成时）。制作将从中断处继续。",
  "暫停製作並釋放顯示卡（喺目前嘅圖像完成之後）。製作會由中斷嘅地方繼續。"],
}

lang_dir = Path(sys.argv[1])
for i, code in enumerate(L):
    f = lang_dir / (code + ".json")
    data = json.loads(f.read_text(encoding="utf-8"))
    s = data["strings"]
    for fr, tr in T.items():
        s[fr] = tr[i]
    f.write_text(json.dumps(data, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("ok", len(T))
