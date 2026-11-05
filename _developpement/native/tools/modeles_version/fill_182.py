# Traductions de la 1.0.82 (Shorts en série). Usage : python fill_182.py <ressources\langues>
import json, sys
from pathlib import Path

K = ["Shorts en série",
     "%d morceau(x) sans Short : un Short de %s s par morceau, centré sur le refrain, rangé dans le dossier « videos\\Shorts » de son album.",
     "Les images du clip d'un morceau sont reprises. Si un morceau n'a pas encore de clip, ses images sont créées d'abord (quelques minutes par morceau). Les Shorts déjà faits ne sont pas refaits. Type de rendu, moteur d'images et durée : ceux de la fenêtre « Créer des vidéos ».",
     "… et %d autre(s) morceau(x).",
     "Créer les Shorts",
     "Tous les morceaux ont déjà leur Short.",
     "Shorts manquants (bibliothèque)…",
     "Toute la bibliothèque",
     "Les images des clips se font au repos : attendez la fin de la génération en cours.",
     "Crée un Short pour chaque morceau terminé qui n'en a pas encore, dans tous les albums.",
     "Shorts manquants de l'album…"]
T = {
    "en": ["Shorts in series",
           "%d track(s) without a Short: one %s s Short per track, centred on the chorus, saved in the “videos\\Shorts” folder of its album.",
           "A track's clip images are reused. If a track has no clip yet, its images are made first (a few minutes per track). Shorts already made are not redone. Render type, image engine and length: those of the “Create videos” window.",
           "… and %d other track(s).", "Create the Shorts", "Every track already has its Short.", "Missing Shorts (library)…", "Whole library",
           "Clip images are made when idle: wait for the current generation to finish.",
           "Creates a Short for every finished track that has none yet, in all albums.", "Missing Shorts of the album…"],
    "es": ["Shorts en serie",
           "%d tema(s) sin Short: un Short de %s s por tema, centrado en el estribillo, guardado en la carpeta «videos\\Shorts» de su álbum.",
           "Se reutilizan las imágenes del clip de un tema. Si un tema aún no tiene clip, sus imágenes se crean primero (unos minutos por tema). Los Shorts ya hechos no se rehacen. Tipo de render, motor de imágenes y duración: los de la ventana «Crear vídeos».",
           "… y %d tema(s) más.", "Crear los Shorts", "Todos los temas ya tienen su Short.", "Shorts que faltan (biblioteca)…", "Toda la biblioteca",
           "Las imágenes de los clips se hacen en reposo: espere a que termine la generación en curso.",
           "Crea un Short para cada tema terminado que aún no tenga, en todos los álbumes.", "Shorts que faltan del álbum…"],
    "it": ["Shorts in serie",
           "%d brano/i senza Short: uno Short di %s s per brano, centrato sul ritornello, salvato nella cartella «videos\\Shorts» del suo album.",
           "Le immagini del clip di un brano vengono riutilizzate. Se un brano non ha ancora un clip, le sue immagini vengono create prima (qualche minuto per brano). Gli Shorts già fatti non vengono rifatti. Tipo di resa, motore di immagini e durata: quelli della finestra «Crea video».",
           "… e altri %d brano/i.", "Crea gli Shorts", "Tutti i brani hanno già il loro Short.", "Shorts mancanti (libreria)…", "Tutta la libreria",
           "Le immagini dei clip si fanno a riposo: attendi la fine della generazione in corso.",
           "Crea uno Short per ogni brano terminato che non ne ha ancora, in tutti gli album.", "Shorts mancanti dell'album…"],
    "de": ["Shorts in Serie",
           "%d Titel ohne Short: ein Short von %s s pro Titel, auf den Refrain zentriert, im Ordner „videos\\Shorts“ seines Albums gespeichert.",
           "Die Clipbilder eines Titels werden wiederverwendet. Hat ein Titel noch keinen Clip, werden seine Bilder zuerst erstellt (einige Minuten pro Titel). Fertige Shorts werden nicht neu erstellt. Darstellungsart, Bild-Engine und Dauer: die des Fensters „Videos erstellen“.",
           "… und %d weitere(r) Titel.", "Shorts erstellen", "Alle Titel haben bereits ihren Short.", "Fehlende Shorts (Bibliothek)…", "Ganze Bibliothek",
           "Clipbilder werden im Leerlauf erstellt: warten Sie, bis die laufende Generierung fertig ist.",
           "Erstellt einen Short für jeden fertigen Titel, der noch keinen hat, in allen Alben.", "Fehlende Shorts des Albums…"],
    "pt": ["Shorts em série",
           "%d faixa(s) sem Short: um Short de %s s por faixa, centrado no refrão, guardado na pasta «videos\\Shorts» do seu álbum.",
           "As imagens do clipe de uma faixa são reutilizadas. Se uma faixa ainda não tem clipe, as suas imagens são criadas primeiro (alguns minutos por faixa). Os Shorts já feitos não são refeitos. Tipo de render, motor de imagens e duração: os da janela «Criar vídeos».",
           "… e mais %d faixa(s).", "Criar os Shorts", "Todas as faixas já têm o seu Short.", "Shorts em falta (biblioteca)…", "Toda a biblioteca",
           "As imagens dos clipes fazem-se em repouso: aguarde o fim da geração em curso.",
           "Cria um Short para cada faixa terminada que ainda não tenha, em todos os álbuns.", "Shorts em falta do álbum…"],
    "ja": ["ショートの一括作成",
           "ショートのない曲 %d 曲：各曲に %s 秒のショートを 1 本、サビ中心で作成し、アルバムの「videos\\Shorts」フォルダに保存します。",
           "曲のクリップ画像は再利用されます。まだクリップのない曲は、先に画像を作成します（1 曲数分）。作成済みのショートは作り直しません。描画タイプ、画像エンジン、長さは「動画を作成」ウィンドウの設定を使います。",
           "… ほか %d 曲。", "ショートを作成", "すべての曲にショートがあります。", "足りないショート（ライブラリ）…", "ライブラリ全体",
           "クリップ画像はアイドル時に作成されます。現在の生成が終わるまでお待ちください。",
           "すべてのアルバムで、ショートのない完成曲ごとにショートを作成します。", "アルバムの足りないショート…"],
    "ko": ["쇼츠 일괄 생성",
           "쇼츠가 없는 곡 %d개: 곡마다 %s초 쇼츠 1개를 후렴 중심으로 만들어 앨범의 \"videos\\Shorts\" 폴더에 저장합니다.",
           "곡의 클립 이미지는 다시 사용됩니다. 아직 클립이 없는 곡은 먼저 이미지를 만듭니다(곡당 몇 분). 이미 만든 쇼츠는 다시 만들지 않습니다. 렌더 유형, 이미지 엔진, 길이는 \"동영상 만들기\" 창의 설정을 따릅니다.",
           "… 외 %d곡.", "쇼츠 만들기", "모든 곡에 이미 쇼츠가 있습니다.", "빠진 쇼츠(라이브러리)…", "라이브러리 전체",
           "클립 이미지는 유휴 상태에서 만들어집니다. 현재 생성이 끝날 때까지 기다리세요.",
           "모든 앨범에서 쇼츠가 없는 완료된 곡마다 쇼츠를 만듭니다.", "앨범의 빠진 쇼츠…"],
    "zh-Hans": ["批量制作短视频",
                "%d 首曲目没有短视频：每首制作一个 %s 秒的短视频，以副歌为中心，保存在所属专辑的「videos\\Shorts」文件夹中。",
                "会重用曲目片段的图像。若曲目还没有片段，会先生成其图像（每首几分钟）。已做好的短视频不会重做。渲染类型、图像引擎和时长：使用「制作视频」窗口中的设置。",
                "… 以及其他 %d 首。", "制作短视频", "所有曲目都已有短视频。", "缺少的短视频（曲库）…", "整个曲库",
                "片段图像在空闲时生成：请等待当前生成结束。",
                "为所有专辑中尚无短视频的已完成曲目各制作一个短视频。", "专辑缺少的短视频…"],
    "yue": ["批量整短片",
            "%d 首歌未有短片：每首整一條 %s 秒嘅短片，以副歌為中心，放喺所屬專輯嘅「videos\\Shorts」資料夾。",
            "會重用歌曲片段嘅圖像。如果首歌未有片段，會先整佢嘅圖像（每首幾分鐘）。已經整好嘅短片唔會重整。渲染類型、圖像引擎同長度：用「製作影片」視窗嘅設定。",
            "… 仲有 %d 首。", "整短片", "所有歌都已經有短片。", "欠缺嘅短片（歌曲庫）…", "成個歌曲庫",
            "片段圖像會喺閒置時整：請等目前嘅生成完成。",
            "喺所有專輯入面，為每首未有短片嘅已完成歌曲整一條短片。", "專輯欠缺嘅短片…"],
}
langues = Path(sys.argv[1])
for code, vals in T.items():
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    s = d["strings"]
    for k, v in zip(K, vals):
        s[k] = v
        s[k.replace("\\", "\\\\")] = v  # i18n_sync peut garder la barre doublée du code source
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")
