# Traductions de la 1.0.97 (sons de l'Atelier partition : instruments par portee, banques SoundFont). Usage : python fill_1910.py <ressources\langues>
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
def r(*a): return list(a)
T = {
 "Instruments :": r("Instruments:", "Instrumentos:", "Strumenti:", "Instrumente:", "Instrumentos:", "楽器：", "악기:", "乐器：", "樂器："),
 "Écouter (banque SoundFont)": r("Listen (SoundFont bank)", "Escuchar (banco SoundFont)", "Ascolta (banco SoundFont)", "Anhören (SoundFont-Bank)", "Ouvir (banco SoundFont)", "再生（SoundFont バンク）", "듣기(SoundFont 뱅크)", "试听（SoundFont 音色库）", "試聽（SoundFont 音色庫）"),
 "Exporter en audio…": r("Export as audio…", "Exportar como audio…", "Esporta come audio…", "Als Audio exportieren…", "Exportar como áudio…", "音声としてエクスポート…", "오디오로 내보내기…", "导出为音频…", "匯出為音頻…"),
 "Gérer les sons…": r("Manage sounds…", "Gestionar sonidos…", "Gestisci i suoni…", "Klänge verwalten…", "Gerir sons…", "サウンドを管理…", "사운드 관리…", "管理音色…", "管理音色…"),
 "Lecture avec la banque SoundFont choisie dans Réglages › Système › Sons de l'Atelier partition : rendu plus réaliste que l'écoute rapide, avec l'instrument de chaque portée.": r(
  "Plays with the SoundFont bank chosen in Settings › System › Score workshop sounds: more realistic than the quick preview, with each staff's instrument.",
  "Reproduce con el banco SoundFont elegido en Ajustes › Sistema › Sonidos del Taller de partituras: más realista que la escucha rápida, con el instrumento de cada pentagrama.",
  "Riproduce con il banco SoundFont scelto in Impostazioni › Sistema › Suoni dell'Atelier partitura: più realistico dell'ascolto rapido, con lo strumento di ogni pentagramma.",
  "Spielt mit der in Einstellungen › System › Klänge der Partitur-Werkstatt gewählten SoundFont-Bank: realistischer als die Schnellvorschau, mit dem Instrument jedes Systems.",
  "Reproduz com o banco SoundFont escolhido em Definições › Sistema › Sons do Atelier de partituras: mais realista do que a audição rápida, com o instrumento de cada pauta.",
  "設定 › システム › 楽譜アトリエのサウンドで選んだ SoundFont バンクで再生します。簡易再生よりリアルで、各段の楽器が使われます。",
  "설정 › 시스템 › 악보 작업실 사운드에서 선택한 SoundFont 뱅크로 재생합니다. 빠른 미리듣기보다 사실적이며 각 단의 악기가 사용됩니다.",
  "使用在 设置 › 系统 › 乐谱工坊音色 中选择的 SoundFont 音色库播放：比快速试听更真实，并使用每个谱表的乐器。",
  "使用喺 設定 › 系統 › 樂譜工房音色 揀嘅 SoundFont 音色庫播放：比快速試聽更真實，並使用每個譜表嘅樂器。"),
 "Ouvre Réglages › Système pour télécharger des instruments (↓ = à télécharger) et des banques de sons.": r("Opens Settings › System to download instruments (↓ = to download) and sound banks.", "Abre Ajustes › Sistema para descargar instrumentos (↓ = por descargar) y bancos de sonidos.", "Apre Impostazioni › Sistema per scaricare strumenti (↓ = da scaricare) e banchi di suoni.", "Öffnet Einstellungen › System, um Instrumente (↓ = herunterladen) und Klangbanken zu laden.", "Abre Definições › Sistema para transferir instrumentos (↓ = por transferir) e bancos de sons.", "設定 › システムを開き、楽器（↓ = 未ダウンロード）とサウンドバンクをダウンロードします。", "설정 › 시스템을 열어 악기(↓ = 다운로드 필요)와 사운드 뱅크를 내려받습니다.", "打开 设置 › 系统，下载乐器（↓ = 待下载）和音色库。", "打開 設定 › 系統，下載樂器（↓ = 待下載）同音色庫。"),
 "Piano à queue": r("Grand piano", "Piano de cola", "Pianoforte a coda", "Flügel", "Piano de cauda", "グランドピアノ", "그랜드 피아노", "三角钢琴", "三角琴"),
 "Piano électrique": r("Electric piano", "Piano eléctrico", "Piano elettrico", "E-Piano", "Piano elétrico", "エレクトリックピアノ", "일렉트릭 피아노", "电钢琴", "電鋼琴"),
 "Guitare classique (nylon)": r("Classical guitar (nylon)", "Guitarra clásica (nailon)", "Chitarra classica (nylon)", "Klassische Gitarre (Nylon)", "Guitarra clássica (nylon)", "クラシックギター（ナイロン）", "클래식 기타(나일론)", "古典吉他（尼龙弦）", "古典結他（尼龍弦）"),
 "Guitare acoustique (acier)": r("Acoustic guitar (steel)", "Guitarra acústica (acero)", "Chitarra acustica (acciaio)", "Akustikgitarre (Stahl)", "Guitarra acústica (aço)", "アコースティックギター（スチール）", "어쿠스틱 기타(스틸)", "民谣吉他（钢弦）", "民謠結他（鋼弦）"),
 "Guitare électrique claire": r("Electric guitar (clean)", "Guitarra eléctrica (limpia)", "Chitarra elettrica (pulita)", "E-Gitarre (clean)", "Guitarra elétrica (limpa)", "エレキギター（クリーン）", "일렉트릭 기타(클린)", "电吉他（清音）", "電結他（清音）"),
 "Guitare électrique étouffée": r("Electric guitar (muted)", "Guitarra eléctrica (apagada)", "Chitarra elettrica (muted)", "E-Gitarre (gedämpft)", "Guitarra elétrica (abafada)", "エレキギター（ミュート）", "일렉트릭 기타(뮤트)", "电吉他（闷音）", "電結他（悶音）"),
 "Guitare saturée": r("Overdriven guitar", "Guitarra con overdrive", "Chitarra overdrive", "Overdrive-Gitarre", "Guitarra com overdrive", "オーバードライブギター", "오버드라이브 기타", "过载吉他", "過載結他"),
 "Guitare distordue": r("Distortion guitar", "Guitarra con distorsión", "Chitarra distorta", "Verzerrte Gitarre", "Guitarra com distorção", "ディストーションギター", "디스토션 기타", "失真吉他", "失真結他"),
 "Contrebasse": r("Double bass", "Contrabajo", "Contrabbasso", "Kontrabass", "Contrabaixo", "コントラバス", "콘트라베이스", "低音提琴", "低音大提琴"),
 "Basse électrique (doigts)": r("Electric bass (fingered)", "Bajo eléctrico (dedos)", "Basso elettrico (dita)", "E-Bass (gezupft)", "Baixo elétrico (dedos)", "エレキベース（フィンガー）", "일렉트릭 베이스(핑거)", "电贝斯（指弹）", "電低音（指彈）"),
 "Basse électrique (médiator)": r("Electric bass (picked)", "Bajo eléctrico (púa)", "Basso elettrico (plettro)", "E-Bass (Plektrum)", "Baixo elétrico (palheta)", "エレキベース（ピック）", "일렉트릭 베이스(피크)", "电贝斯（拨片）", "電低音（撥片）"),
 "Basse slap": r("Slap bass", "Bajo slap", "Basso slap", "Slap-Bass", "Baixo slap", "スラップベース", "슬랩 베이스", "击弦贝斯", "擊弦低音"),
 "Basse synthé": r("Synth bass", "Bajo sintetizado", "Basso synth", "Synthbass", "Baixo sintetizador", "シンセベース", "신스 베이스", "合成贝斯", "合成低音"),
 "Violon": r("Violin", "Violín", "Violino", "Violine", "Violino", "バイオリン", "바이올린", "小提琴", "小提琴"),
 "Violoncelle": r("Cello", "Violonchelo", "Violoncello", "Cello", "Violoncelo", "チェロ", "첼로", "大提琴", "大提琴"),
 "Ensemble de cordes": r("String ensemble", "Conjunto de cuerdas", "Insieme d'archi", "Streicherensemble", "Conjunto de cordas", "ストリングス", "현악 앙상블", "弦乐合奏", "弦樂合奏"),
 "Chœur (aah)": r("Choir (aah)", "Coro (aah)", "Coro (aah)", "Chor (aah)", "Coro (aah)", "合唱（アー）", "합창(아)", "合唱（啊）", "合唱（啊）"),
 "Voix (ooh)": r("Voice (ooh)", "Voz (ooh)", "Voce (ooh)", "Stimme (ooh)", "Voz (ooh)", "ボイス（ウー）", "보이스(우)", "人声（呜）", "人聲（嗚）"),
 "Chœur synthétique": r("Synth choir", "Coro sintetizado", "Coro synth", "Synthchor", "Coro sintetizador", "シンセコーラス", "신스 합창", "合成合唱", "合成合唱"),
 "Trompette": r("Trumpet", "Trompeta", "Tromba", "Trompete", "Trompete", "トランペット", "트럼펫", "小号", "小號"),
 "Saxophone alto": r("Alto saxophone", "Saxofón alto", "Sassofono contralto", "Altsaxofon", "Saxofone alto", "アルトサックス", "알토 색소폰", "中音萨克斯", "中音色士風"),
 "Flûte": r("Flute", "Flauta", "Flauto", "Flöte", "Flauta", "フルート", "플루트", "长笛", "長笛"),
 "Synthé lead (carré)": r("Synth lead (square)", "Sintetizador lead (cuadrada)", "Synth lead (quadra)", "Synth-Lead (Rechteck)", "Sintetizador lead (quadrada)", "シンセリード（矩形波）", "신스 리드(사각파)", "合成主音（方波）", "合成主音（方波）"),
 "Nappe chaude": r("Warm pad", "Pad cálido", "Pad caldo", "Warmer Pad", "Pad quente", "ウォームパッド", "웜 패드", "温暖音垫", "溫暖音墊"),
 "Instrument téléchargé : %s": r("Instrument downloaded: %s", "Instrumento descargado: %s", "Strumento scaricato: %s", "Instrument heruntergeladen: %s", "Instrumento transferido: %s", "楽器をダウンロードしました：%s", "악기를 다운로드했습니다: %s", "已下载乐器：%s", "已下載樂器：%s"),
 "Échec du téléchargement : %s": r("Download failed: %s", "Falló la descarga: %s", "Download non riuscito: %s", "Download fehlgeschlagen: %s", "Falha na transferência: %s", "ダウンロードに失敗しました：%s", "다운로드 실패: %s", "下载失败：%s", "下載失敗：%s"),
 "GeneralUser GS (31 Mo, bon compromis, usage libre y compris commercial)": r("GeneralUser GS (31 MB, good compromise, free use including commercial)", "GeneralUser GS (31 MB, buen compromiso, uso libre incluido el comercial)", "GeneralUser GS (31 MB, buon compromesso, uso libero anche commerciale)", "GeneralUser GS (31 MB, guter Kompromiss, freie Nutzung auch kommerziell)", "GeneralUser GS (31 MB, bom compromisso, uso livre incluindo comercial)", "GeneralUser GS（31 MB、バランス良好、商用を含め自由に使用可）", "GeneralUser GS(31MB, 균형이 좋음, 상업적 사용 포함 자유 이용)", "GeneralUser GS（31 MB，综合表现好，可自由使用，含商用）", "GeneralUser GS（31 MB，綜合表現好，可自由使用，包括商用）"),
 "FluidR3 GM (142 Mo, généraliste, licence permissive)": r("FluidR3 GM (142 MB, general purpose, permissive licence)", "FluidR3 GM (142 MB, generalista, licencia permisiva)", "FluidR3 GM (142 MB, generico, licenza permissiva)", "FluidR3 GM (142 MB, Allrounder, freizügige Lizenz)", "FluidR3 GM (142 MB, generalista, licença permissiva)", "FluidR3 GM（142 MB、汎用、寛容なライセンス）", "FluidR3 GM(142MB, 범용, 관대한 라이선스)", "FluidR3 GM（142 MB，通用，宽松许可）", "FluidR3 GM（142 MB，通用，寬鬆授權）"),
 "Audio enregistré : %s": r("Audio saved: %s", "Audio guardado: %s", "Audio salvato: %s", "Audio gespeichert: %s", "Áudio guardado: %s", "音声を保存しました：%s", "오디오를 저장했습니다: %s", "音频已保存：%s", "音頻已儲存：%s"),
 "Rendu audio impossible : %s": r("Audio rendering failed: %s", "No se pudo generar el audio: %s", "Impossibile creare l'audio: %s", "Audio-Rendering nicht möglich: %s", "Não foi possível gerar o áudio: %s", "音声を生成できません：%s", "오디오를 생성할 수 없습니다: %s", "无法生成音频：%s", "生成唔到音頻：%s"),
 "Un rendu audio est déjà en cours.": r("An audio rendering is already in progress.", "Ya hay un audio en proceso de generación.", "Una generazione audio è già in corso.", "Ein Audio-Rendering läuft bereits.", "Já há um áudio em geração.", "音声の生成はすでに実行中です。", "오디오 생성이 이미 진행 중입니다.", "音频生成已在进行。", "音頻生成已經喺度進行。"),
 "Aucune banque de sons installée : Réglages › Système › Sons de l'Atelier partition.": r("No sound bank installed: Settings › System › Score workshop sounds.", "No hay ningún banco de sonidos instalado: Ajustes › Sistema › Sonidos del Taller de partituras.", "Nessun banco di suoni installato: Impostazioni › Sistema › Suoni dell'Atelier partitura.", "Keine Klangbank installiert: Einstellungen › System › Klänge der Partitur-Werkstatt.", "Nenhum banco de sons instalado: Definições › Sistema › Sons do Atelier de partituras.", "サウンドバンクがインストールされていません：設定 › システム › 楽譜アトリエのサウンド。", "설치된 사운드 뱅크가 없습니다: 설정 › 시스템 › 악보 작업실 사운드.", "未安装音色库：设置 › 系统 › 乐谱工坊音色。", "未安裝音色庫：設定 › 系統 › 樂譜工房音色。"),
 "Sons de l'Atelier partition (instruments et banques)": r("Score workshop sounds (instruments and banks)", "Sonidos del Taller de partituras (instrumentos y bancos)", "Suoni dell'Atelier partitura (strumenti e banchi)", "Klänge der Partitur-Werkstatt (Instrumente und Banken)", "Sons do Atelier de partituras (instrumentos e bancos)", "楽譜アトリエのサウンド（楽器とバンク）", "악보 작업실 사운드(악기 및 뱅크)", "乐谱工坊音色（乐器与音色库）", "樂譜工房音色（樂器同音色庫）"),
 "Instrument de chaque portée et lecture plus réaliste dans l'Atelier partition. Les instruments (échantillons FluidR3 GM, ≈ 2,5 Mo chacun) servent à l'écoute rapide ; une banque SoundFont donne la lecture de qualité et l'export audio (WAV). Tout est téléchargé à votre clic.": r(
  "Instrument per staff and more realistic playback in the Score workshop. Instruments (FluidR3 GM samples, ≈ 2.5 MB each) are used for quick listening; a SoundFont bank gives high-quality playback and audio export (WAV). Everything is downloaded when you click.",
  "Instrumento de cada pentagrama y reproducción más realista en el Taller de partituras. Los instrumentos (muestras FluidR3 GM, ≈ 2,5 MB cada uno) sirven para la escucha rápida; un banco SoundFont da la reproducción de calidad y la exportación de audio (WAV). Todo se descarga con su clic.",
  "Strumento di ogni pentagramma e riproduzione più realistica nell'Atelier partitura. Gli strumenti (campioni FluidR3 GM, ≈ 2,5 MB ciascuno) servono all'ascolto rapido; un banco SoundFont offre la riproduzione di qualità e l'esportazione audio (WAV). Tutto viene scaricato al tuo clic.",
  "Instrument pro System und realistischere Wiedergabe in der Partitur-Werkstatt. Die Instrumente (FluidR3-GM-Samples, ≈ 2,5 MB je) dienen der Schnellvorschau; eine SoundFont-Bank liefert hochwertige Wiedergabe und Audio-Export (WAV). Alles wird auf Ihren Klick heruntergeladen.",
  "Instrumento de cada pauta e reprodução mais realista no Atelier de partituras. Os instrumentos (amostras FluidR3 GM, ≈ 2,5 MB cada) servem para a audição rápida; um banco SoundFont dá a reprodução de qualidade e a exportação de áudio (WAV). Tudo é transferido ao seu clique.",
  "楽譜アトリエで各段の楽器を選び、よりリアルに再生できます。楽器（FluidR3 GM のサンプル、各約 2.5 MB）は簡易再生用で、SoundFont バンクは高品質な再生と音声（WAV）のエクスポートに使います。すべてクリックでダウンロードします。",
  "악보 작업실에서 단마다 악기를 고르고 더 사실적으로 재생합니다. 악기(FluidR3 GM 샘플, 각 약 2.5MB)는 빠른 미리듣기용이고, SoundFont 뱅크는 고품질 재생과 오디오(WAV) 내보내기에 사용됩니다. 모두 클릭 시 다운로드됩니다.",
  "在乐谱工坊中为每个谱表选择乐器并获得更真实的播放。乐器（FluidR3 GM 采样，每个约 2.5 MB）用于快速试听；SoundFont 音色库提供高质量播放和音频（WAV）导出。一切都在您点击后下载。",
  "喺樂譜工房為每個譜表揀樂器，播放更真實。樂器（FluidR3 GM 取樣，每個約 2.5 MB）用嚟快速試聽；SoundFont 音色庫提供高質素播放同音頻（WAV）匯出。一切都喺你㩒落去之後先下載。"),
 "Banques SoundFont": r("SoundFont banks", "Bancos SoundFont", "Banchi SoundFont", "SoundFont-Banken", "Bancos SoundFont", "SoundFont バンク", "SoundFont 뱅크", "SoundFont 音色库", "SoundFont 音色庫"),
 "Téléchargement : %s — %s / %s": r("Downloading: %s — %s / %s", "Descargando: %s — %s / %s", "Download: %s — %s / %s", "Download: %s — %s / %s", "A transferir: %s — %s / %s", "ダウンロード中：%s — %s / %s", "다운로드 중: %s — %s / %s", "正在下载：%s — %s / %s", "下載緊：%s — %s / %s"),
 "Tout télécharger (≈ 60 Mo)": r("Download all (≈ 60 MB)", "Descargar todo (≈ 60 MB)", "Scarica tutto (≈ 60 MB)", "Alles herunterladen (≈ 60 MB)", "Transferir tudo (≈ 60 MB)", "すべてダウンロード（約 60 MB）", "모두 다운로드(약 60MB)", "全部下载（约 60 MB）", "全部下載（約 60 MB）"),
 "%s — téléchargement…": r("%s — downloading…", "%s — descargando…", "%s — download in corso…", "%s — wird heruntergeladen…", "%s — a transferir…", "%s — ダウンロード中…", "%s — 다운로드 중…", "%s — 正在下载…", "%s — 下載緊…"),
}
langues = Path(sys.argv[1])
for i, code in enumerate(L):
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    for k, v in T.items():
        assert len(v) == 9, (k, len(v))
        d["strings"][k] = v[i]
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print(len(T), "OK")
