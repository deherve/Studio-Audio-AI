# Traductions de la 1.0.96. Usage : python fill_196.py <ressources\langues>
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {"Rechercher un titre, un groupe, un album, un style ou des paroles…": ["Search a title, a band, an album, a style or lyrics…", "Buscar un título, un grupo, un álbum, un estilo o una letra…", "Cerca un titolo, un gruppo, un album, uno stile o un testo…", "Titel, Band, Album, Stil oder Text suchen…", "Pesquisar um título, uma banda, um álbum, um estilo ou uma letra…", "タイトル、バンド、アルバム、スタイル、歌詞を検索…", "제목, 그룹, 앨범, 스타일 또는 가사 검색…", "搜索标题、乐队、专辑、风格或歌词…", "搜尋標題、樂隊、專輯、風格或歌詞…"]}
d0 = Path(sys.argv[1])
for i, c in enumerate(L):
    p = d0 / f"{c}.json"; d = json.loads(p.read_text(encoding="utf-8"))
    for k, v in T.items(): d["strings"][k] = v[i]
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")
