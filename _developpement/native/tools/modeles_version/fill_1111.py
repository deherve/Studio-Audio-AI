# Traductions de la 1.1.011 (style du groupe, Réglages › Styles, journal). Usage : python fill_1111.py <ressources\langues>
# Ordre des langues : en, es, it, de, pt, ja, ko, zh-Hans, yue. Écrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "Sauvegarder le style de ce groupe": [
  "Save this band's style", "Guardar el estilo de este grupo", "Salva lo stile di questo gruppo", "Stil dieser Band speichern",
  "Guardar o estilo deste grupo", "このグループのスタイルを保存", "이 그룹의 스타일 저장", "保存此乐队的风格", "儲存呢個樂團嘅風格"],
 "À chaque morceau lancé, le style est enregistré dans vos styles sous le nom du groupe (s'il n'existe pas déjà), pour reproduire le même groupe plus tard.": [
  "Each time a track is started, its style is saved in your styles under the band's name (if it does not exist yet), so you can reproduce the same band later.",
  "Cada vez que se lanza una canción, su estilo se guarda en sus estilos con el nombre del grupo (si no existe ya), para reproducir el mismo grupo más adelante.",
  "A ogni brano avviato, lo stile viene salvato tra i tuoi stili con il nome del gruppo (se non esiste già), per riprodurre lo stesso gruppo in seguito.",
  "Bei jedem gestarteten Titel wird der Stil unter dem Namen der Band in Ihren Stilen gespeichert (falls es ihn noch nicht gibt), um dieselbe Band später nachzubilden.",
  "A cada faixa iniciada, o estilo é guardado nos seus estilos com o nome do grupo (se ainda não existir), para reproduzir o mesmo grupo mais tarde.",
  "曲を開始するたびに、そのスタイルがグループ名で自分のスタイルに保存されます（同名がまだない場合）。後で同じグループを再現できます。",
  "곡을 시작할 때마다 스타일이 그룹 이름으로 내 스타일에 저장됩니다(같은 이름이 없을 때). 나중에 같은 그룹을 재현할 수 있습니다.",
  "每次开始生成歌曲时，其风格会以乐队名称保存到您的风格中（若尚不存在），以便日后重现同一乐队。",
  "每次開始生成歌曲，佢嘅風格會用樂團名稱儲存喺你嘅風格入面（如果未有），方便之後重現同一個樂團。"],
 "Balises de style": [
  "Style tags", "Etiquetas de estilo", "Tag di stile", "Stil-Tags", "Etiquetas de estilo", "スタイルタグ", "스타일 태그", "风格标签", "風格標籤"],
 "Balises proposées dans le bloc Style de « Nouveau morceau ». La croix retire une balise de la liste (un style déjà écrit la garde) ; le champ en ajoute de nouvelles, en anglais comme le modèle (plusieurs séparées par des virgules). Les balises ajoutées sont précédées d'un point.": [
  "Tags offered in the Style block of \"New track\". The cross removes a tag from the list (a style already written keeps it); the field adds new ones, in English like the model (several separated by commas). Added tags are preceded by a dot.",
  "Etiquetas que se ofrecen en el bloque Estilo de «Nueva canción». La cruz quita una etiqueta de la lista (un estilo ya escrito la conserva); el campo añade otras nuevas, en inglés como el modelo (varias separadas por comas). Las etiquetas añadidas van precedidas de un punto.",
  "Tag proposti nel blocco Stile di «Nuovo brano». La croce toglie un tag dall'elenco (uno stile già scritto lo conserva); il campo ne aggiunge di nuovi, in inglese come il modello (più tag separati da virgole). I tag aggiunti sono preceduti da un punto.",
  "Tags, die im Block Stil von „Neuer Titel“ angeboten werden. Das Kreuz entfernt einen Tag aus der Liste (ein bereits geschriebener Stil behält ihn); das Feld fügt neue hinzu, auf Englisch wie das Modell (mehrere durch Kommas getrennt). Hinzugefügte Tags beginnen mit einem Punkt.",
  "Etiquetas oferecidas no bloco Estilo de «Nova faixa». A cruz remove uma etiqueta da lista (um estilo já escrito mantém-na); o campo adiciona novas, em inglês como o modelo (várias separadas por vírgulas). As etiquetas adicionadas são precedidas de um ponto.",
  "「新しい曲」のスタイル欄で提案されるタグです。×でリストからタグを外します（すでに書かれたスタイルには残ります）。入力欄では、モデルと同じ英語で新しいタグを追加できます（カンマで複数）。追加したタグの前には点が付きます。",
  "'새 곡'의 스타일 블록에 제안되는 태그입니다. ✕는 목록에서 태그를 제거합니다(이미 작성된 스타일에는 남습니다). 입력란에서는 모델과 같은 영어로 새 태그를 추가합니다(쉼표로 여러 개). 추가한 태그 앞에는 점이 붙습니다.",
  "“新歌曲”风格栏中提供的标签。点叉号可从列表中移除标签（已写好的风格仍保留）；输入框可添加新标签，使用与模型相同的英文（多个用逗号分隔）。添加的标签前有一个圆点。",
  "「新歌曲」風格欄提供嘅標籤。㩒叉叉可以由清單移除標籤（已經寫好嘅風格會保留）；輸入欄可以加新標籤，同模型一樣用英文（多個用逗號分隔）。新加嘅標籤前面有個點。"],
 "%d balises": [
  "%d tags", "%d etiquetas", "%d tag", "%d Tags", "%d etiquetas", "%d個のタグ", "태그 %d개", "%d 个标签", "%d 個標籤"],
 "Nouvelle balise (en anglais, ex. : dream pop)": [
  "New tag (in English, e.g. dream pop)", "Nueva etiqueta (en inglés, p. ej. dream pop)", "Nuovo tag (in inglese, es. dream pop)",
  "Neuer Tag (auf Englisch, z. B. dream pop)", "Nova etiqueta (em inglês, ex.: dream pop)", "新しいタグ（英語で。例：dream pop）",
  "새 태그(영어로, 예: dream pop)", "新标签（英文，例如 dream pop）", "新標籤（用英文，例如 dream pop）"],
 "Rétablir ce groupe": [
  "Reset this group", "Restablecer este grupo", "Ripristina questo gruppo", "Diese Gruppe zurücksetzen", "Repor este grupo",
  "このグループを元に戻す", "이 그룹 복원", "恢复此组", "還原呢組"],
 "Balises retirées (cliquez pour les remettre) :": [
  "Removed tags (click to put them back):", "Etiquetas quitadas (haga clic para volver a ponerlas):", "Tag rimossi (clicca per rimetterli):",
  "Entfernte Tags (zum Zurückholen anklicken):", "Etiquetas removidas (clique para as repor):", "外したタグ（クリックで元に戻す）：",
  "제거한 태그(클릭하면 되돌립니다):", "已移除的标签（点击可恢复）：", "已移除嘅標籤（㩒一下可以還原）："],
 "Tout réinitialiser": [
  "Reset everything", "Restablecer todo", "Ripristina tutto", "Alles zurücksetzen", "Repor tudo", "すべて初期化", "모두 초기화", "全部重置", "全部重設"],
 "Remettre toutes les balises par défaut ? Vos balises ajoutées sont supprimées et les balises retirées reviennent.": [
  "Restore all tags to their defaults? Your added tags are deleted and removed tags come back.",
  "¿Restablecer todas las etiquetas por defecto? Se borran las etiquetas añadidas y vuelven las quitadas.",
  "Ripristinare tutti i tag predefiniti? I tag aggiunti vengono eliminati e quelli rimossi tornano.",
  "Alle Tags auf die Standardwerte zurücksetzen? Hinzugefügte Tags werden gelöscht, entfernte kommen zurück.",
  "Repor todas as etiquetas por defeito? As etiquetas adicionadas são apagadas e as removidas voltam.",
  "すべてのタグを初期状態に戻しますか？追加したタグは削除され、外したタグは戻ります。",
  "모든 태그를 기본값으로 되돌릴까요? 추가한 태그는 삭제되고 제거한 태그는 돌아옵니다.",
  "将所有标签恢复为默认值？已添加的标签将被删除，已移除的标签会恢复。",
  "要將所有標籤還原做預設值嗎？新加嘅標籤會被刪除，已移除嘅標籤會返嚟。"],
 "Réinitialiser": [
  "Reset", "Restablecer", "Ripristina", "Zurücksetzen", "Repor", "初期化", "초기화", "重置", "重設"],
 "Crée un style personnalisé au nom du groupe, avec les balises communes à ses morceaux.": [
  "Creates a custom style named after the band, with the tags common to its tracks.",
  "Crea un estilo personalizado con el nombre del grupo, con las etiquetas comunes a sus canciones.",
  "Crea uno stile personalizzato con il nome del gruppo, con i tag comuni ai suoi brani.",
  "Erstellt einen eigenen Stil mit dem Namen der Band und den gemeinsamen Tags ihrer Titel.",
  "Cria um estilo personalizado com o nome do grupo, com as etiquetas comuns às suas faixas.",
  "グループ名のカスタムスタイルを作成します（曲に共通するタグを使用）。",
  "그룹 이름으로 사용자 지정 스타일을 만듭니다(곡들의 공통 태그 사용).",
  "以乐队名称创建自定义风格，使用其歌曲共有的标签。",
  "用樂團名稱建立自訂風格，使用佢啲歌共有嘅標籤。"],
 "Journal de débogage": [
  "Debug log", "Registro de depuración", "Registro di debug", "Debug-Protokoll", "Registo de depuração", "デバッグログ", "디버그 로그", "调试日志", "除錯日誌"],
 "Le Studio note ce qui se passe (démarrage, clics, travaux lancés, erreurs et ce qui les a précédées) : à joindre à un message en cas de problème.": [
  "The Studio records what happens (startup, clicks, jobs started, errors and what came before them): attach it to a message if there is a problem.",
  "El Studio anota lo que ocurre (inicio, clics, trabajos lanzados, errores y lo que los precedió): adjúntelo a un mensaje en caso de problema.",
  "Lo Studio annota ciò che accade (avvio, clic, lavori avviati, errori e ciò che li ha preceduti): allegalo a un messaggio in caso di problema.",
  "Das Studio protokolliert, was passiert (Start, Klicks, gestartete Aufträge, Fehler und was ihnen voranging): bei Problemen einer Nachricht beifügen.",
  "O Studio regista o que acontece (arranque, cliques, trabalhos iniciados, erros e o que os precedeu): junte-o a uma mensagem em caso de problema.",
  "Studio は動作（起動、クリック、開始した処理、エラーとその直前の操作）を記録します。問題が起きたときはメッセージに添付してください。",
  "Studio가 동작(시작, 클릭, 시작한 작업, 오류와 그 직전 동작)을 기록합니다. 문제가 있을 때 메시지에 첨부하세요.",
  "Studio 会记录发生的事情（启动、点击、已开始的任务、错误及其之前的操作），出现问题时请附在消息中。",
  "Studio 會記錄發生過嘅事（啟動、㩒掣、已開始嘅工作、錯誤同之前嘅操作），出問題時請附喺訊息度。"],
 "Ouvrir le journal": [
  "Open the log", "Abrir el registro", "Apri il registro", "Protokoll öffnen", "Abrir o registo", "ログを開く", "로그 열기", "打开日志", "打開日誌"],
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
