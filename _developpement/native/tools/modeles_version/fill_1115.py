# Traductions de la 1.1.015 (prévision de la file). Usage : python fill_1115.py <ressources\langues>
# Ordre : en, es, it, de, pt, ja, ko, zh-Hans, yue. Écrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "Fin estimée : %s (vers %s)": [
  "Estimated end: %s (around %s)", "Fin estimado: %s (hacia las %s)", "Fine stimata: %s (verso le %s)", "Geschätztes Ende: %s (gegen %s)",
  "Fim estimado: %s (por volta das %s)", "終了予定：%s（%s ごろ）", "예상 종료: %s (%s경)", "预计结束：%s（约 %s）", "預計完結：%s（約 %s）"],
 "Estimation d'après la durée des morceaux déjà générés sur ce PC ; elle s'affine au fil des générations.": [
  "Estimate based on the time of the tracks already generated on this PC; it improves as you generate more.",
  "Estimación según el tiempo de las canciones ya generadas en este PC; mejora con cada generación.",
  "Stima basata sul tempo dei brani già generati su questo PC; migliora a ogni generazione.",
  "Schätzung nach der Dauer der bereits auf diesem PC erzeugten Titel; sie wird mit jeder Erzeugung genauer.",
  "Estimativa com base no tempo das faixas já geradas neste PC; melhora a cada geração.",
  "このPCで生成済みの曲の所要時間に基づく見積もりです。生成を重ねるほど精度が上がります。",
  "이 PC에서 이미 생성한 곡의 소요 시간을 바탕으로 한 추정치이며, 생성할수록 정확해집니다.",
  "根据此电脑上已生成歌曲的耗时估算，生成越多越准确。",
  "根據呢部電腦上已生成歌曲嘅耗時估算，生成得越多就越準。"],
 "Temps estimé : environ %s pour %d versions.": [
  "Estimated time: about %s for %d versions.", "Tiempo estimado: unos %s para %d versiones.", "Tempo stimato: circa %s per %d versioni.",
  "Geschätzte Zeit: etwa %s für %d Versionen.", "Tempo estimado: cerca de %s para %d versões.", "所要時間の目安：約 %s（%d バージョン）。",
  "예상 시간: 약 %s (%d개 버전).", "预计用时：约 %s（%d 个版本）。", "預計用時：大約 %s（%d 個版本）。"],
 "Temps estimé : environ %s.": [
  "Estimated time: about %s.", "Tiempo estimado: unos %s.", "Tempo stimato: circa %s.", "Geschätzte Zeit: etwa %s.", "Tempo estimado: cerca de %s.",
  "所要時間の目安：約 %s。", "예상 시간: 약 %s.", "预计用时：约 %s。", "預計用時：大約 %s。"],
 "Morceau long : sur une carte de 12 Go ou moins, la mémoire vidéo peut manquer vers la fin au-delà de 4 minutes environ ; le Studio refait alors un essai tout seul.": [
  "Long track: on a card with 12 GB or less, video memory can run out near the end beyond about 4 minutes; the Studio then retries by itself.",
  "Canción larga: en una tarjeta de 12 GB o menos, la memoria de vídeo puede agotarse hacia el final a partir de unos 4 minutos; el Studio vuelve a intentarlo solo.",
  "Brano lungo: su una scheda da 12 GB o meno, la memoria video può esaurirsi verso la fine oltre i 4 minuti circa; lo Studio riprova da solo.",
  "Langer Titel: Auf einer Karte mit 12 GB oder weniger kann der Grafikspeicher ab etwa 4 Minuten gegen Ende ausgehen; das Studio versucht es dann selbst noch einmal.",
  "Faixa longa: numa placa de 12 GB ou menos, a memória de vídeo pode acabar perto do fim a partir de cerca de 4 minutos; o Studio tenta de novo sozinho.",
  "長い曲：12 GB 以下のカードでは、約4分を超えると終盤でビデオメモリが足りなくなることがあります。その場合は Studio が自動で再試行します。",
  "긴 곡: 12GB 이하의 카드에서는 약 4분을 넘으면 끝부분에서 비디오 메모리가 부족할 수 있으며, 이 경우 Studio가 자동으로 다시 시도합니다.",
  "长歌曲：显存 12 GB 及以下的显卡，超过约 4 分钟后接近结尾时可能显存不足；此时 Studio 会自动重试一次。",
  "長歌曲：12 GB 或以下嘅顯示卡，超過大約 4 分鐘後接近尾段可能會顯存不足；到時 Studio 會自動再試一次。"],
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
