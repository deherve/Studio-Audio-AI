# Traductions de la 1.1.016 (image de référence du groupe). Usage : python fill_1116.py <ressources\langues>
# Ordre : en, es, it, de, pt, ja, ko, zh-Hans, yue. Écrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "Image de référence du groupe…": [
  "Band reference image…", "Imagen de referencia del grupo…", "Immagine di riferimento del gruppo…", "Referenzbild der Band…", "Imagem de referência do grupo…",
  "グループの参照画像…", "그룹 참조 이미지…", "乐队参考图像…", "樂團參考圖像…"],
 "Image de référence du groupe": [
  "Band reference image", "Imagen de referencia del grupo", "Immagine di riferimento del gruppo", "Referenzbild der Band", "Imagem de referência do grupo",
  "グループの参照画像", "그룹 참조 이미지", "乐队参考图像", "樂團參考圖像"],
 "Une image (pochette, portrait du chanteur…) copiée dans le dossier du groupe : FLUX.3 klein s'en inspire pour garder le même style et les mêmes sujets dans les prochaines pochettes et dans les images des clips (plus lent : ≈ 90 s par image au lieu de ≈ 40 s).": [
  "An image (cover, portrait of the singer…) copied into the band's folder: FLUX.3 klein draws on it to keep the same style and subjects in the next covers and in the clip images (slower: ≈ 90 s per image instead of ≈ 40 s).",
  "Una imagen (portada, retrato del cantante…) copiada en la carpeta del grupo: FLUX.3 klein se inspira en ella para mantener el mismo estilo y los mismos sujetos en las próximas portadas y en las imágenes de los clips (más lento: ≈ 90 s por imagen en vez de ≈ 40 s).",
  "Un'immagine (copertina, ritratto del cantante…) copiata nella cartella del gruppo: FLUX.3 klein la usa come ispirazione per mantenere lo stesso stile e gli stessi soggetti nelle prossime copertine e nelle immagini dei clip (più lento: ≈ 90 s per immagine invece di ≈ 40 s).",
  "Ein Bild (Cover, Porträt des Sängers …), das in den Ordner der Band kopiert wird: FLUX.3 klein orientiert sich daran, um in den nächsten Covern und in den Clip-Bildern denselben Stil und dieselben Motive beizubehalten (langsamer: ≈ 90 s pro Bild statt ≈ 40 s).",
  "Uma imagem (capa, retrato do cantor…) copiada para a pasta do grupo: o FLUX.3 klein inspira-se nela para manter o mesmo estilo e os mesmos temas nas próximas capas e nas imagens dos clipes (mais lento: ≈ 90 s por imagem em vez de ≈ 40 s).",
  "グループのフォルダにコピーされる画像（ジャケット、歌手のポートレートなど）。FLUX.3 klein はこれを参考に、今後のジャケットやクリップ画像で同じスタイルと被写体を保ちます（遅くなります：1枚あたり約90秒、通常は約40秒）。",
  "그룹 폴더에 복사되는 이미지(커버, 가수 초상 등)입니다. FLUX.3 klein이 이를 참고하여 이후 커버와 클립 이미지에서 같은 스타일과 대상을 유지합니다(더 느림: 이미지당 약 90초, 기본 약 40초).",
  "复制到乐队文件夹的一张图片（封面、歌手肖像等）：FLUX.3 klein 会参考它，使之后的封面和视频片段图像保持相同的风格和主体（更慢：每张约 90 秒，原为约 40 秒）。",
  "複製到樂團資料夾嘅一幅圖像（封面、歌手肖像等）：FLUX.3 klein 會參考佢，令之後嘅封面同短片圖像保持相同嘅風格同主體（較慢：每幅大約 90 秒，原本大約 40 秒）。"],
 "Retirer l'image de référence": [
  "Remove the reference image", "Quitar la imagen de referencia", "Rimuovi l'immagine di riferimento", "Referenzbild entfernen", "Remover a imagem de referência",
  "参照画像を削除", "참조 이미지 제거", "移除参考图像", "移除參考圖像"],
 "Image de référence enregistrée pour « %s ».": [
  "Reference image saved for “%s”.", "Imagen de referencia guardada para «%s».", "Immagine di riferimento salvata per «%s».", "Referenzbild für „%s“ gespeichert.",
  "Imagem de referência guardada para «%s».", "「%s」の参照画像を保存しました。", "'%s'의 참조 이미지를 저장했습니다.", "已为“%s”保存参考图像。", "已經為「%s」儲存參考圖像。"],
 "Impossible d'enregistrer l'image de référence.": [
  "Could not save the reference image.", "No se pudo guardar la imagen de referencia.", "Impossibile salvare l'immagine di riferimento.", "Das Referenzbild konnte nicht gespeichert werden.",
  "Não foi possível guardar a imagem de referência.", "参照画像を保存できませんでした。", "참조 이미지를 저장할 수 없습니다.", "无法保存参考图像。", "無法儲存參考圖像。"],
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
