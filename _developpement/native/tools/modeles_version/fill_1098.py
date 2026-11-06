# Traductions de la 1.0.98 (atelier clip : fiches texte, graine commune, texte de fin). Usage : python fill_1098.py <ressources\langues>
# Ordre des langues : en, es, it, de, pt, ja, ko, zh-Hans, yue. Ecrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "Écrire les fiches texte (paroles, morceau, album, groupe)": [
  "Write the text sheets (lyrics, track, album, band)",
  "Escribir las fichas de texto (letra, tema, álbum, grupo)",
  "Scrivi le schede di testo (testi, brano, album, gruppo)",
  "Textblätter schreiben (Songtext, Titel, Album, Band)",
  "Escrever as fichas de texto (letra, faixa, álbum, banda)",
  "テキストシートを書き出す（歌詞、曲、アルバム、バンド）",
  "텍스트 시트 작성(가사, 곡, 앨범, 밴드)",
  "写入文本资料（歌词、曲目、专辑、乐队）",
  "寫入文字資料（歌詞、歌曲、專輯、樂隊）"],
 "Fiches texte écrites pour %d morceau(x).": [
  "Text sheets written for %d track(s).",
  "Fichas de texto escritas para %d tema(s).",
  "Schede di testo scritte per %d brano/i.",
  "Textblätter für %d Titel geschrieben.",
  "Fichas de texto escritas para %d faixa(s).",
  "%d 曲のテキストシートを書き出しました。",
  "%d곡의 텍스트 시트를 작성했습니다.",
  "已为 %d 首曲目写入文本资料。",
  "已經為 %d 首歌寫入文字資料。"],
 "« paroles.txt » et « fiche.txt » dans le dossier de chaque morceau, « fiche_album.txt » et « fiche_groupe.txt » au-dessus. Le clip s'en sert pour son scénario. Un « paroles.txt » déjà présent n'est jamais remplacé.": [
  "“paroles.txt” and “fiche.txt” in each track's folder, “fiche_album.txt” and “fiche_groupe.txt” above it. The music video uses them for its storyline. An existing “paroles.txt” is never replaced.",
  "«paroles.txt» y «fiche.txt» en la carpeta de cada tema, «fiche_album.txt» y «fiche_groupe.txt» encima. El videoclip los usa para su guion. Un «paroles.txt» ya existente nunca se reemplaza.",
  "«paroles.txt» e «fiche.txt» nella cartella di ogni brano, «fiche_album.txt» e «fiche_groupe.txt» sopra. Il videoclip li usa per la sceneggiatura. Un «paroles.txt» già presente non viene mai sostituito.",
  "„paroles.txt“ und „fiche.txt“ im Ordner jedes Titels, „fiche_album.txt“ und „fiche_groupe.txt“ darüber. Der Clip nutzt sie für sein Drehbuch. Eine vorhandene „paroles.txt“ wird nie ersetzt.",
  "«paroles.txt» e «fiche.txt» na pasta de cada faixa, «fiche_album.txt» e «fiche_groupe.txt» acima. O videoclipe usa-os para o guião. Um «paroles.txt» já existente nunca é substituído.",
  "各曲のフォルダーに「paroles.txt」と「fiche.txt」、その上に「fiche_album.txt」と「fiche_groupe.txt」。クリップはこれらをシナリオに使います。既存の「paroles.txt」は置き換えません。",
  "각 곡 폴더에 \"paroles.txt\"와 \"fiche.txt\", 그 위에 \"fiche_album.txt\"와 \"fiche_groupe.txt\". 클립은 이를 시나리오에 사용합니다. 이미 있는 \"paroles.txt\"는 절대 바꾸지 않습니다.",
  "每首曲目的文件夹中有“paroles.txt”和“fiche.txt”，上一级有“fiche_album.txt”和“fiche_groupe.txt”。视频片段用它们编写剧本。已有的“paroles.txt”绝不会被替换。",
  "每首歌嘅資料夾有「paroles.txt」同「fiche.txt」，上一層有「fiche_album.txt」同「fiche_groupe.txt」。短片會用佢哋寫劇本。已經有嘅「paroles.txt」永遠唔會被取代。"],
 "Même graine pour toutes les images d'un clip (personnage et décor plus réguliers)": [
  "Same seed for all images of a video (more consistent character and setting)",
  "Misma semilla para todas las imágenes de un videoclip (personaje y decorado más regulares)",
  "Stesso seme per tutte le immagini di un videoclip (personaggio e ambientazione più coerenti)",
  "Gleicher Seed für alle Bilder eines Clips (Figur und Kulisse einheitlicher)",
  "Mesma semente para todas as imagens de um videoclipe (personagem e cenário mais regulares)",
  "クリップの全画像に同じシード（人物と背景がより揃う）",
  "클립의 모든 이미지에 같은 시드(인물과 배경이 더 일정함)",
  "同一视频片段的所有图像使用相同种子（人物和场景更一致）",
  "同一段短片嘅所有圖像用同一個種子（人物同場景更一致）"],
 "Chaque scène garde sa description, mais le tirage de départ est le même : visage, couleurs et cadrage se ressemblent davantage d'une image à l'autre. Ce n'est pas une garantie. Ne change que les nouvelles images : cochez « Refaire » pour un clip déjà fait.": [
  "Each scene keeps its own description, but the starting draw is the same: face, colours and framing look more alike from one image to the next. This is not a guarantee. Only affects new images: tick “Redo” for a video already made.",
  "Cada escena conserva su descripción, pero el sorteo inicial es el mismo: rostro, colores y encuadre se parecen más de una imagen a otra. No es una garantía. Solo cambia las imágenes nuevas: marque «Rehacer» para un videoclip ya hecho.",
  "Ogni scena mantiene la sua descrizione, ma l'estrazione iniziale è la stessa: volto, colori e inquadratura si somigliano di più da un'immagine all'altra. Non è una garanzia. Cambia solo le nuove immagini: spunta «Rifai» per un videoclip già fatto.",
  "Jede Szene behält ihre Beschreibung, aber die Startziehung ist dieselbe: Gesicht, Farben und Bildausschnitt ähneln sich von Bild zu Bild stärker. Das ist keine Garantie. Gilt nur für neue Bilder: Für einen fertigen Clip „Neu erstellen“ ankreuzen.",
  "Cada cena mantém a sua descrição, mas o sorteio inicial é o mesmo: rosto, cores e enquadramento parecem-se mais de uma imagem para outra. Não é uma garantia. Só altera as novas imagens: marque «Refazer» para um videoclipe já feito.",
  "各シーンは独自の説明を保ちますが、最初の乱数は同じです。顔、色、構図が画像間でより似てきます。保証ではありません。新しい画像にのみ影響します。作成済みのクリップには「やり直す」にチェックしてください。",
  "각 장면은 고유한 설명을 유지하지만 시작 난수는 같습니다. 얼굴, 색감, 구도가 이미지마다 더 비슷해집니다. 보장되지는 않습니다. 새 이미지에만 적용됩니다. 이미 만든 클립은 \"다시 만들기\"를 체크하세요.",
  "每个场景保留自己的描述，但初始随机数相同：面孔、色彩和构图在各图像之间更相似。这并非保证。只影响新图像：已完成的视频片段请勾选“重做”。",
  "每個場景保留自己嘅描述，但起始隨機數一樣：樣貌、色彩同構圖喺唔同圖像之間會更似。呢個唔係保證。只影響新圖像：已經整好嘅短片請剔「重做」。"],
 "Texte de fin :": ["End text:", "Texto final:", "Testo finale:", "Schlusstext:", "Texto final:", "終わりのテキスト：", "끝 텍스트:", "结尾文字：", "結尾文字："],
 "au centre": ["centred", "en el centro", "al centro", "in der Mitte", "ao centro", "中央", "가운데", "居中", "置中"],
 "en bas": ["at the bottom", "abajo", "in basso", "unten", "em baixo", "下", "아래", "底部", "底部"],
 "En bas, à la place des paroles : le centre de l'image (souvent le personnage) reste visible.": [
  "At the bottom, where the lyrics go: the centre of the image (often the character) stays visible.",
  "Abajo, en el lugar de la letra: el centro de la imagen (a menudo el personaje) sigue visible.",
  "In basso, al posto dei testi: il centro dell'immagine (spesso il personaggio) resta visibile.",
  "Unten, an der Stelle des Songtexts: Die Bildmitte (oft die Figur) bleibt sichtbar.",
  "Em baixo, no lugar da letra: o centro da imagem (muitas vezes a personagem) continua visível.",
  "下、歌詞の位置に表示：画像の中央（多くは人物）が見えたままになります。",
  "아래, 가사 자리에 표시: 이미지 가운데(주로 인물)가 그대로 보입니다.",
  "显示在底部歌词的位置：图像中央（通常是人物）保持可见。",
  "顯示喺底部歌詞嘅位置：圖像中間（通常係人物）保持睇得到。"],
 "fond sombre": ["dark background", "fondo oscuro", "sfondo scuro", "dunkler Hintergrund", "fundo escuro", "暗い背景", "어두운 배경", "深色背景", "深色背景"],
 "Bandeau noir semi-transparent derrière le texte de fin : lisible même sur une image claire.": [
  "Semi-transparent black band behind the end text: readable even on a bright image.",
  "Banda negra semitransparente detrás del texto final: legible incluso sobre una imagen clara.",
  "Fascia nera semitrasparente dietro il testo finale: leggibile anche su un'immagine chiara.",
  "Halbtransparentes schwarzes Band hinter dem Schlusstext: auch auf hellem Bild lesbar.",
  "Faixa preta semitransparente atrás do texto final: legível mesmo numa imagem clara.",
  "終わりのテキストの後ろに半透明の黒い帯：明るい画像でも読みやすくなります。",
  "끝 텍스트 뒤에 반투명 검은 띠: 밝은 이미지에서도 읽기 쉽습니다.",
  "结尾文字后面加半透明黑色横条：即使在明亮的图像上也清晰可读。",
  "結尾文字後面加半透明黑色橫條：就算喺光亮嘅圖像上都睇得清楚。"],
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
