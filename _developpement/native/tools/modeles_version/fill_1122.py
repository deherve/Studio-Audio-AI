# Traductions de la 1.1.022 (Iris-3B, Qwen-Image 2.1). Usage : python -X utf8 fill_1122.py <ressources\langues>
# Ordre : en, es, it, de, pt, ja, ko, zh-Hans, yue. Écrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "Le plus rapide des modèles qui écrivent bien le texte dans l'image (titre, nom du groupe) : environ 14 s par image. Licence Apache 2.0.": [
  "The fastest of the models that write text well inside the image (title, band name): about 14 s per image. Apache 2.0 licence.",
  "El más rápido de los modelos que escriben bien el texto en la imagen (título, nombre del grupo): unos 14 s por imagen. Licencia Apache 2.0.",
  "Il più veloce tra i modelli che scrivono bene il testo nell'immagine (titolo, nome del gruppo): circa 14 s per immagine. Licenza Apache 2.0.",
  "Das schnellste der Modelle, die Text im Bild gut schreiben (Titel, Bandname): etwa 14 s pro Bild. Apache-2.0-Lizenz.",
  "O mais rápido dos modelos que escrevem bem o texto na imagem (título, nome da banda): cerca de 14 s por imagem. Licença Apache 2.0.",
  "画像内の文字（タイトル、バンド名）をきれいに書けるモデルの中で最速：1枚あたり約14秒。Apache 2.0ライセンス。",
  "이미지 안의 글자(제목, 밴드 이름)를 잘 쓰는 모델 중 가장 빠름: 이미지당 약 14초. Apache 2.0 라이선스.",
  "在图像中写字（标题、乐队名）效果好的模型里最快的：每张约 14 秒。Apache 2.0 许可。",
  "喺圖入面寫字（標題、樂隊名）寫得好嘅模型之中最快：每張約 14 秒。Apache 2.0 授權。"],
 "Très bon rendu et texte bien écrit dans l'image (accents parfois approximatifs), environ 1 minute par image, 8,5 Go de mémoire vidéo. Licence Qwen : usage commercial à vérifier (tableau des licences dans Réglages › Système).": [
  "Very good rendering and well-written text in the image (accents sometimes approximate), about 1 minute per image, 8.5 GB of video memory. Qwen licence: check commercial use (licence table in Settings › System).",
  "Muy buen resultado y texto bien escrito en la imagen (los acentos a veces son aproximados), aproximadamente 1 minuto por imagen, 8,5 GB de memoria de vídeo. Licencia Qwen: comprueba el uso comercial (tabla de licencias en Ajustes › Sistema).",
  "Ottima resa e testo ben scritto nell'immagine (gli accenti a volte sono approssimativi), circa 1 minuto per immagine, 8,5 GB di memoria video. Licenza Qwen: verifica l'uso commerciale (tabella delle licenze in Impostazioni › Sistema).",
  "Sehr gutes Ergebnis und gut geschriebener Text im Bild (Akzente manchmal ungenau), etwa 1 Minute pro Bild, 8,5 GB Grafikspeicher. Qwen-Lizenz: kommerzielle Nutzung prüfen (Lizenztabelle unter Einstellungen › System).",
  "Muito bom resultado e texto bem escrito na imagem (os acentos às vezes são aproximados), cerca de 1 minuto por imagem, 8,5 GB de memória de vídeo. Licença Qwen: verifique o uso comercial (tabela de licenças em Definições › Sistema).",
  "とても良い仕上がりで、画像内の文字もきれいです（アクセントが不正確なことがあります）。1枚あたり約1分、ビデオメモリ8.5GB。Qwenライセンス：商用利用は要確認（設定 › システムのライセンス表）。",
  "결과물이 매우 좋고 이미지 속 글자도 잘 써집니다(악센트가 부정확할 때가 있음). 이미지당 약 1분, 비디오 메모리 8.5GB. Qwen 라이선스: 상업적 사용은 확인 필요(설정 › 시스템의 라이선스 표).",
  "效果很好，图中文字写得准确（重音符号有时不准），每张约 1 分钟，占用 8.5 GB 显存。Qwen 许可：商业使用需自行核实（设置 › 系统中的许可表）。",
  "效果好好，圖入面嘅字寫得準（重音符號有時唔準），每張約 1 分鐘，用 8.5 GB 顯示記憶體。Qwen 授權：商業用途要自己核實（設定 › 系統入面嘅授權表）。"],
 "Version complète de Qwen-Image 2.1 : rendu sobre et soigné, mais environ 3 fois plus lent que la version Turbo (2 min 40 par image). Même licence Qwen que la version Turbo.": [
  "Full version of Qwen-Image 2.1: restrained, polished rendering, but about 3 times slower than the Turbo version (2 min 40 per image). Same Qwen licence as the Turbo version.",
  "Versión completa de Qwen-Image 2.1: resultado sobrio y cuidado, pero unas 3 veces más lento que la versión Turbo (2 min 40 por imagen). Misma licencia Qwen que la versión Turbo.",
  "Versione completa di Qwen-Image 2.1: resa sobria e curata, ma circa 3 volte più lenta della versione Turbo (2 min 40 per immagine). Stessa licenza Qwen della versione Turbo.",
  "Vollversion von Qwen-Image 2.1: schlichtes, sauberes Ergebnis, aber etwa 3-mal langsamer als die Turbo-Version (2 Min. 40 pro Bild). Dieselbe Qwen-Lizenz wie die Turbo-Version.",
  "Versão completa do Qwen-Image 2.1: resultado sóbrio e cuidado, mas cerca de 3 vezes mais lento que a versão Turbo (2 min 40 por imagem). Mesma licença Qwen da versão Turbo.",
  "Qwen-Image 2.1の完全版：控えめで丁寧な仕上がりですが、Turbo版の約3倍遅く、1枚あたり2分40秒です。ライセンスはTurbo版と同じQwenライセンスです。",
  "Qwen-Image 2.1 전체 버전: 절제되고 정돈된 결과물이지만 Turbo 버전보다 약 3배 느립니다(이미지당 2분 40초). 라이선스는 Turbo 버전과 같은 Qwen 라이선스입니다.",
  "Qwen-Image 2.1 完整版：画面朴素细致，但比 Turbo 版慢约 3 倍（每张 2 分 40 秒）。许可与 Turbo 版相同，均为 Qwen 许可。",
  "Qwen-Image 2.1 完整版：畫面樸實細緻，但比 Turbo 版慢約 3 倍（每張 2 分 40 秒）。授權同 Turbo 版一樣，都係 Qwen 授權。"],
 "Modèles sous licence Apache 2.0 (Z-Image Turbo : Alibaba ; MOSS-Image : OpenMOSS ; Iris-3B : Sperid Labs). FLUX.3 klein (Black Forest Labs) : FLUX Kommunity License ; Qwen-Image 2.1 : licence Qwen, voir le tableau des licences.": [
  "Models under Apache 2.0 licence (Z-Image Turbo: Alibaba; MOSS-Image: OpenMOSS; Iris-3B: Sperid Labs). FLUX.3 klein (Black Forest Labs): FLUX Kommunity License; Qwen-Image 2.1: Qwen licence, see the licence table.",
  "Modelos con licencia Apache 2.0 (Z-Image Turbo: Alibaba; MOSS-Image: OpenMOSS; Iris-3B: Sperid Labs). FLUX.3 klein (Black Forest Labs): FLUX Kommunity License; Qwen-Image 2.1: licencia Qwen, ver la tabla de licencias.",
  "Modelli con licenza Apache 2.0 (Z-Image Turbo: Alibaba; MOSS-Image: OpenMOSS; Iris-3B: Sperid Labs). FLUX.3 klein (Black Forest Labs): FLUX Kommunity License; Qwen-Image 2.1: licenza Qwen, vedi la tabella delle licenze.",
  "Modelle unter Apache-2.0-Lizenz (Z-Image Turbo: Alibaba; MOSS-Image: OpenMOSS; Iris-3B: Sperid Labs). FLUX.3 klein (Black Forest Labs): FLUX Kommunity License; Qwen-Image 2.1: Qwen-Lizenz, siehe Lizenztabelle.",
  "Modelos com licença Apache 2.0 (Z-Image Turbo: Alibaba; MOSS-Image: OpenMOSS; Iris-3B: Sperid Labs). FLUX.3 klein (Black Forest Labs): FLUX Kommunity License; Qwen-Image 2.1: licença Qwen, ver a tabela de licenças.",
  "Apache 2.0ライセンスのモデル（Z-Image Turbo：Alibaba、MOSS-Image：OpenMOSS、Iris-3B：Sperid Labs）。FLUX.3 klein（Black Forest Labs）：FLUX Kommunity License。Qwen-Image 2.1：Qwenライセンス（ライセンス表を参照）。",
  "Apache 2.0 라이선스 모델(Z-Image Turbo: Alibaba, MOSS-Image: OpenMOSS, Iris-3B: Sperid Labs). FLUX.3 klein(Black Forest Labs): FLUX Kommunity License. Qwen-Image 2.1: Qwen 라이선스, 라이선스 표 참조.",
  "采用 Apache 2.0 许可的模型（Z-Image Turbo：Alibaba；MOSS-Image：OpenMOSS；Iris-3B：Sperid Labs）。FLUX.3 klein（Black Forest Labs）：FLUX Kommunity License；Qwen-Image 2.1：Qwen 许可，见许可表。",
  "採用 Apache 2.0 授權嘅模型（Z-Image Turbo：Alibaba；MOSS-Image：OpenMOSS；Iris-3B：Sperid Labs）。FLUX.3 klein（Black Forest Labs）：FLUX Kommunity License；Qwen-Image 2.1：Qwen 授權，睇授權表。"],
 "licence Qwen (non commerciale d'après la fiche du modèle : à vérifier avant tout usage commercial)": [
  "Qwen licence (non-commercial according to the model card: check before any commercial use)",
  "licencia Qwen (no comercial según la ficha del modelo: comprobar antes de cualquier uso comercial)",
  "licenza Qwen (non commerciale secondo la scheda del modello: verificare prima di ogni uso commerciale)",
  "Qwen-Lizenz (laut Modellkarte nicht kommerziell: vor jeder kommerziellen Nutzung prüfen)",
  "licença Qwen (não comercial segundo a ficha do modelo: verificar antes de qualquer uso comercial)",
  "Qwenライセンス（モデルカードによれば非商用：商用利用の前に要確認）",
  "Qwen 라이선스(모델 카드상 비상업용: 상업적 사용 전에 확인 필요)",
  "Qwen 许可（据模型卡为非商业用途：商业使用前请核实）",
  "Qwen 授權（按模型卡係非商業用途：商業使用前要核實）"],
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
