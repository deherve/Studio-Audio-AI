# Traductions complementaires 1.0.97 (telechargement du modele de separation). Usage : python fill_197b.py <ressources\langues>
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "Télécharger le modèle de séparation (84 Mo)…": ["Download the separation model (84 MB)…", "Descargar el modelo de separación (84 MB)…", "Scarica il modello di separazione (84 MB)…", "Trennungsmodell herunterladen (84 MB)…", "Transferir o modelo de separação (84 MB)…", "分離モデルをダウンロード（84 MB）…", "분리 모델 다운로드(84MB)…", "下载分离模型（84 MB）…", "下載分離模型（84 MB）…"],
 "Modèle de séparation des pistes": ["Stem separation model", "Modelo de separación de pistas", "Modello di separazione delle tracce", "Modell zur Spurtrennung", "Modelo de separação de pistas", "トラック分離モデル", "트랙 분리 모델", "音轨分离模型", "音軌分離模型"],
 "Téléchargement du modèle de séparation lancé (84 Mo).": ["Separation model download started (84 MB).", "Descarga del modelo de separación iniciada (84 MB).", "Download del modello di separazione avviato (84 MB).", "Download des Trennungsmodells gestartet (84 MB).", "Transferência do modelo de separação iniciada (84 MB).", "分離モデルのダウンロードを開始しました（84 MB）。", "분리 모델 다운로드를 시작했습니다(84MB).", "已开始下载分离模型（84 MB）。", "已開始下載分離模型（84 MB）。"],
 "Modèle de séparation téléchargé.": ["Separation model downloaded.", "Modelo de separación descargado.", "Modello di separazione scaricato.", "Trennungsmodell heruntergeladen.", "Modelo de separação transferido.", "分離モデルをダウンロードしました。", "분리 모델을 다운로드했습니다.", "分离模型已下载。", "分離模型已下載。"],
 "Échec du téléchargement du modèle de séparation.": ["Separation model download failed.", "Falló la descarga del modelo de separación.", "Download del modello di separazione non riuscito.", "Download des Trennungsmodells fehlgeschlagen.", "Falha na transferência do modelo de separação.", "分離モデルのダウンロードに失敗しました。", "분리 모델 다운로드에 실패했습니다.", "分离模型下载失败。", "分離模型下載失敗。"],
}
d0 = Path(sys.argv[1])
for i, c in enumerate(L):
    p = d0 / f"{c}.json"; d = json.loads(p.read_text(encoding="utf-8"))
    for k, v in T.items(): d["strings"][k] = v[i]
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")
