# Traductions de la 1.1.020 / 1.1.021 (morceau long coupé selon la durée, relecture des paroles). Usage : python fill_1121.py <ressources\langues>
# Ordre : en, es, it, de, pt, ja, ko, zh-Hans, yue. Écrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "Morceau long : coupé en 3 parties d'environ %s chacune pour tenir dans la mémoire de la carte graphique.": [
  "Long track: split into 3 parts of about %s each so that it fits in the graphics card memory.",
  "Tema largo: dividido en 3 partes de unos %s cada una para que quepa en la memoria de la tarjeta gráfica.",
  "Brano lungo: diviso in 3 parti di circa %s ciascuna per stare nella memoria della scheda grafica.",
  "Langer Titel: in 3 Teile von je etwa %s geteilt, damit er in den Grafikspeicher passt.",
  "Faixa longa: dividida em 3 partes de cerca de %s cada para caber na memória da placa de vídeo.",
  "長い曲：グラフィックカードのメモリに収まるよう、約%sずつの3つのパートに分割します。",
  "긴 곡: 그래픽 카드 메모리에 맞도록 각각 약 %s 길이의 3개 파트로 나눕니다.",
  "长曲：为适应显卡显存，已拆分为 3 个部分，每部分约 %s。",
  "長曲：為適應顯卡記憶體，已拆成 3 個部分，每部分約 %s。"],
 "Relire les paroles : l'IA corrige les lignes qui sonnent « IA »": [
  "Proofread the lyrics: the AI fixes lines that sound “AI”",
  "Revisar la letra: la IA corrige las líneas que suenan a «IA»",
  "Rileggere i testi: l'IA corregge le righe che suonano «IA»",
  "Texte prüfen: Die KI korrigiert Zeilen, die nach „KI“ klingen",
  "Revisar a letra: a IA corrige as linhas que soam a «IA»",
  "歌詞を見直す：AIが「AIっぽい」行を直します",
  "가사 다시 읽기: AI가 'AI 같은' 줄을 고칩니다",
  "校对歌词：AI 修改听起来像“AI 腔”的句子",
  "校對歌詞：AI 修改聽落似「AI 腔」嘅句子"],
 "Après avoir écrit les paroles, l'IA les relit et ne réécrit que les lignes toutes faites ou traduites de l'anglais. Les sections, le nombre de lignes et les refrains restent identiques. Compter environ une minute de plus par chanson avec l'IA locale (quelques secondes en ligne). Décochée par défaut : avec une petite IA locale, l'effet reste modeste.": [
  "After writing the lyrics, the AI reads them again and rewrites only the stock lines or those translated from English. Sections, number of lines and choruses stay the same. Allow about one more minute per song with the local AI (a few seconds online). Off by default: with a small local AI the effect remains modest.",
  "Tras escribir la letra, la IA la relee y reescribe solo las líneas hechas o traducidas del inglés. Las secciones, el número de líneas y los estribillos no cambian. Cuenta con un minuto más por canción con la IA local (unos segundos en línea). Desactivado por defecto: con una IA local pequeña el efecto es modesto.",
  "Dopo aver scritto il testo, l'IA lo rilegge e riscrive solo le righe fatte o tradotte dall'inglese. Sezioni, numero di righe e ritornelli restano identici. Calcola circa un minuto in più per brano con l'IA locale (pochi secondi online). Disattivato di default: con una piccola IA locale l'effetto resta modesto.",
  "Nach dem Schreiben liest die KI den Text noch einmal und schreibt nur Floskeln oder aus dem Englischen übersetzte Zeilen um. Abschnitte, Zeilenzahl und Refrains bleiben gleich. Mit der lokalen KI etwa eine Minute mehr pro Titel (online einige Sekunden). Standardmäßig aus: bei einer kleinen lokalen KI bleibt die Wirkung gering.",
  "Depois de escrever a letra, a IA relê-a e reescreve apenas as linhas feitas ou traduzidas do inglês. As secções, o número de linhas e os refrões ficam iguais. Conte cerca de um minuto a mais por canção com a IA local (alguns segundos online). Desativado por predefinição: com uma IA local pequena o efeito é modesto.",
  "歌詞を書いた後、AIが読み直し、ありきたりな行や英語からの直訳の行だけを書き直します。セクション、行数、サビは変わりません。ローカルAIでは1曲あたり約1分長くなります（オンラインなら数秒）。初期設定はオフ：小さなローカルAIでは効果は控えめです。",
  "가사를 쓴 뒤 AI가 다시 읽고 상투적이거나 영어에서 직역한 줄만 고쳐 씁니다. 구간, 줄 수, 후렴은 그대로입니다. 로컬 AI에서는 곡당 약 1분이 더 걸립니다(온라인은 몇 초). 기본값은 꺼짐: 작은 로컬 AI에서는 효과가 크지 않습니다.",
  "写完歌词后，AI 会重读一遍，只改写套话或从英语直译的句子。段落、行数和副歌保持不变。使用本地 AI 时每首歌多花约一分钟（在线只需几秒）。默认关闭：小型本地 AI 的效果有限。",
  "寫完歌詞後，AI 會重讀一次，只改寫套語或由英文直譯嘅句子。段落、行數同副歌保持不變。用本機 AI 每首歌要多一分鐘左右（網上只需幾秒）。預設關閉：細型本機 AI 嘅效果有限。"],
}
langues = Path(sys.argv[1])
for i, code in enumerate(L):
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8-sig"))
    for k, v in T.items():
        assert len(v) == 9, k
        d["strings"][k] = v[i]
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print(len(T), "OK")
