# Traductions de la 1.1.012 (FLUX.3 klein). Usage : python fill_1112.py <ressources\langues>
# Ordre des langues : en, es, it, de, pt, ja, ko, zh-Hans, yue. Écrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "FLUX Kommunity : commercial seulement sous 5 M$ de revenus": [
  "FLUX Kommunity: commercial use only under US$5M revenue", "FLUX Kommunity: comercial solo por debajo de 5 M$ de ingresos",
  "FLUX Kommunity: commerciale solo sotto 5 M$ di ricavi", "FLUX Kommunity: kommerziell nur unter 5 Mio. US$ Umsatz",
  "FLUX Kommunity: comercial apenas abaixo de 5 M$ de receitas", "FLUX Kommunity：商用は収益500万ドル未満の場合のみ",
  "FLUX Kommunity: 매출 500만 달러 미만일 때만 상업적 사용 가능", "FLUX Kommunity：仅限收入低于 500 万美元时商用",
  "FLUX Kommunity：收入低於 500 萬美元先可以商用"],
 "Très beau rendu, texte bien écrit dans l'image, mais environ trois fois plus lent que l'ancien FLUX.2 klein. Licence FLUX Kommunity (voir le tableau des licences dans Réglages › Système).": [
  "Very good rendering, text well written in the image, but about three times slower than the former FLUX.2 klein. FLUX Kommunity license (see the licence table in Settings › System).",
  "Muy buen resultado, texto bien escrito en la imagen, pero unas tres veces más lento que el antiguo FLUX.2 klein. Licencia FLUX Kommunity (véase la tabla de licencias en Ajustes › Sistema).",
  "Ottima resa, testo scritto bene nell'immagine, ma circa tre volte più lento del vecchio FLUX.2 klein. Licenza FLUX Kommunity (vedi la tabella delle licenze in Impostazioni › Sistema).",
  "Sehr schönes Ergebnis, Text im Bild gut geschrieben, aber etwa dreimal langsamer als das frühere FLUX.2 klein. FLUX-Kommunity-Lizenz (siehe Lizenztabelle unter Einstellungen › System).",
  "Muito bom resultado, texto bem escrito na imagem, mas cerca de três vezes mais lento do que o antigo FLUX.2 klein. Licença FLUX Kommunity (veja a tabela de licenças em Definições › Sistema).",
  "非常に美しい仕上がりで、画像内の文字もきれいに描かれますが、以前の FLUX.2 klein より約3倍遅くなります。FLUX Kommunity ライセンス（設定›システムのライセンス表を参照）。",
  "렌더링이 매우 훌륭하고 이미지 속 글자도 잘 써지지만, 이전 FLUX.2 klein보다 약 3배 느립니다. FLUX Kommunity 라이선스(설정 › 시스템의 라이선스 표 참조).",
  "画面效果非常出色，图像中的文字也写得很好，但比之前的 FLUX.2 klein 慢约三倍。FLUX Kommunity 许可（见“设置 › 系统”中的许可表）。",
  "畫面效果非常好，圖入面嘅文字都寫得好，但係比之前嘅 FLUX.2 klein 慢大約三倍。FLUX Kommunity 授權（見「設定 › 系統」嘅授權表）。"],
 "Modèles sous licence Apache 2.0 (Z-Image Turbo : Alibaba ; MOSS-Image : OpenMOSS). FLUX.3 klein (Black Forest Labs) : FLUX Kommunity License, voir le tableau des licences.": [
  "Models under the Apache 2.0 license (Z-Image Turbo: Alibaba; MOSS-Image: OpenMOSS). FLUX.3 klein (Black Forest Labs): FLUX Kommunity License, see the licence table.",
  "Modelos con licencia Apache 2.0 (Z-Image Turbo: Alibaba; MOSS-Image: OpenMOSS). FLUX.3 klein (Black Forest Labs): licencia FLUX Kommunity, véase la tabla de licencias.",
  "Modelli con licenza Apache 2.0 (Z-Image Turbo: Alibaba; MOSS-Image: OpenMOSS). FLUX.3 klein (Black Forest Labs): licenza FLUX Kommunity, vedi la tabella delle licenze.",
  "Modelle unter Apache-2.0-Lizenz (Z-Image Turbo: Alibaba; MOSS-Image: OpenMOSS). FLUX.3 klein (Black Forest Labs): FLUX-Kommunity-Lizenz, siehe Lizenztabelle.",
  "Modelos com licença Apache 2.0 (Z-Image Turbo: Alibaba; MOSS-Image: OpenMOSS). FLUX.3 klein (Black Forest Labs): licença FLUX Kommunity, veja a tabela de licenças.",
  "Apache 2.0 ライセンスのモデル（Z-Image Turbo：Alibaba、MOSS-Image：OpenMOSS）。FLUX.3 klein（Black Forest Labs）：FLUX Kommunity ライセンス。ライセンス表を参照。",
  "Apache 2.0 라이선스 모델(Z-Image Turbo: Alibaba, MOSS-Image: OpenMOSS). FLUX.3 klein(Black Forest Labs): FLUX Kommunity 라이선스, 라이선스 표 참조.",
  "采用 Apache 2.0 许可的模型（Z-Image Turbo：阿里巴巴；MOSS-Image：OpenMOSS）。FLUX.3 klein（Black Forest Labs）：FLUX Kommunity 许可，见许可表。",
  "用 Apache 2.0 授權嘅模型（Z-Image Turbo：阿里巴巴；MOSS-Image：OpenMOSS）。FLUX.3 klein（Black Forest Labs）：FLUX Kommunity 授權，見授權表。"],
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
