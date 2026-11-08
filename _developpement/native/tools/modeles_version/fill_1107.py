# Traductions de la 1.1.007 (doublons par empreinte audio). Usage : python fill_1107.py <ressources\langues>
# Ordre des langues : en, es, it, de, pt, ja, ko, zh-Hans, yue. Écrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "Aucun couple de morceaux trop proches : compositions, paroles, sons et mélodies sont tous différents.": [
  "No pair of tracks is too close: compositions, lyrics, sounds and melodies are all different.",
  "Ningún par de pistas es demasiado parecido: composiciones, letras, sonidos y melodías son todos distintos.",
  "Nessuna coppia di brani troppo simile: composizioni, testi, suoni e melodie sono tutti diversi.",
  "Kein Titelpaar ist zu ähnlich: Kompositionen, Texte, Klänge und Melodien sind alle verschieden.",
  "Nenhum par de faixas é demasiado parecido: composições, letras, sons e melodias são todos diferentes.",
  "似すぎている曲の組はありません：作曲・歌詞・音・メロディはすべて異なります。",
  "너무 비슷한 곡 쌍이 없습니다: 작곡, 가사, 소리, 멜로디가 모두 다릅니다.",
  "没有过于相似的曲目对：作曲、歌词、声音和旋律都不同。",
  "冇太相似嘅歌：作曲、歌詞、聲音同旋律全部唔同。"],
 "Couples dont la composition (jetons du modèle), les paroles, le son ou la mélodie (empreinte audio) se ressemblent beaucoup. Un clic sur un titre le cherche dans la bibliothèque ; rapport complet : « doublons.txt ».": [
  "Pairs whose composition (model tokens), lyrics, sound or melody (audio fingerprint) are very alike. Click a title to find it in the library; full report: “doublons.txt”.",
  "Pares cuya composición (tokens del modelo), letra, sonido o melodía (huella de audio) se parecen mucho. Un clic en un título lo busca en la biblioteca; informe completo: «doublons.txt».",
  "Coppie la cui composizione (token del modello), testo, suono o melodia (impronta audio) si somigliano molto. Un clic su un titolo lo cerca nella libreria; rapporto completo: «doublons.txt».",
  "Paare, deren Komposition (Modell-Token), Text, Klang oder Melodie (Audio-Fingerabdruck) sich stark ähneln. Ein Klick auf einen Titel sucht ihn in der Bibliothek; vollständiger Bericht: „doublons.txt“.",
  "Pares cuja composição (tokens do modelo), letra, som ou melodia (impressão áudio) se parecem muito. Um clique num título procura-o na biblioteca; relatório completo: «doublons.txt».",
  "作曲（モデルのトークン）・歌詞・音・メロディ（音声フィンガープリント）がよく似ている組です。タイトルをクリックするとライブラリで検索します。詳細レポート：「doublons.txt」。",
  "작곡(모델 토큰), 가사, 소리 또는 멜로디(오디오 지문)가 매우 비슷한 쌍입니다. 제목을 클릭하면 라이브러리에서 찾습니다. 전체 보고서: “doublons.txt”.",
  "作曲（模型令牌）、歌词、声音或旋律（音频指纹）非常相似的曲目对。点击标题可在曲库中查找；完整报告：“doublons.txt”。",
  "作曲（模型記號）、歌詞、聲音或者旋律（音訊指紋）好相似嘅歌。撳標題就會喺歌曲庫度搵；完整報告：「doublons.txt」。"],
 "son": ["sound", "sonido", "suono", "Klang", "som", "音", "소리", "声音", "聲音"],
 "mélodie": ["melody", "melodía", "melodia", "Melodie", "melodia", "メロディ", "멜로디", "旋律", "旋律"],
 "même mélodie (à l'écoute)": ["same melody (by ear)", "misma melodía (al oído)", "stessa melodia (all'ascolto)", "gleiche Melodie (nach Gehör)", "mesma melodia (ao ouvido)",
                               "同じメロディ（聴き取り）", "같은 멜로디(청취 기준)", "旋律相同（听感）", "旋律一樣（聽落）"],
 "son très proche (à l'écoute)": ["very similar sound (by ear)", "sonido muy parecido (al oído)", "suono molto simile (all'ascolto)", "sehr ähnlicher Klang (nach Gehör)", "som muito parecido (ao ouvido)",
                                  "音がとても近い（聴き取り）", "소리가 매우 비슷함(청취 기준)", "声音非常接近（听感）", "聲音好接近（聽落）"],
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
