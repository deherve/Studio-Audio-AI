# Traductions de la 1.0.95 (fenetre « Paroles et structure »). Usage : python fill_195.py <ressources\langues>
# Ordre : en, es, it, de, pt, ja, ko, zh-Hans, yue.
import json, sys
from pathlib import Path

L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
    "Paroles et structure": ["Lyrics and structure", "Letra y estructura", "Testo e struttura", "Text und Struktur", "Letra e estrutura", "歌詞と構成", "가사와 구조", "歌词和结构", "歌詞同結構"],
    "Copier la sélection": ["Copy selection", "Copiar la selección", "Copia la selezione", "Auswahl kopieren", "Copiar a seleção", "選択範囲をコピー", "선택 항목 복사", "复制所选内容", "複製所選內容"],
    "Copier les paroles": ["Copy lyrics", "Copiar la letra", "Copia il testo", "Text kopieren", "Copiar a letra", "歌詞をコピー", "가사 복사", "复制歌词", "複製歌詞"],
    "Copier la structure": ["Copy structure", "Copiar la estructura", "Copia la struttura", "Struktur kopieren", "Copiar a estrutura", "構成をコピー", "구조 복사", "复制结构", "複製結構"],
    "Tout copier": ["Copy all", "Copiar todo", "Copia tutto", "Alles kopieren", "Copiar tudo", "すべてコピー", "모두 복사", "全部复制", "全部複製"],
    "Aucune structure enregistrée pour ce morceau.": ["No structure saved for this track.", "No hay estructura guardada para este tema.", "Nessuna struttura salvata per questo brano.", "Für diesen Titel ist keine Struktur gespeichert.", "Nenhuma estrutura guardada para esta faixa.", "この曲には保存された構成がありません。", "이 곡에는 저장된 구조가 없습니다.", "此曲目没有保存的结构。", "呢首歌冇儲存結構。"],
    "Morceau instrumental : pas de paroles, seulement sa structure.": ["Instrumental track: no lyrics, only its structure.", "Tema instrumental: sin letra, solo su estructura.", "Brano strumentale: nessun testo, solo la struttura.", "Instrumentaltitel: kein Text, nur die Struktur.", "Faixa instrumental: sem letra, apenas a estrutura.", "インストゥルメンタル曲：歌詞はなく、構成のみです。", "연주곡: 가사 없이 구조만 있습니다.", "纯音乐曲目：没有歌词，只有结构。", "純音樂歌曲：冇歌詞，只有結構。"],
    "Aucune parole enregistrée pour ce morceau.": ["No lyrics saved for this track.", "No hay letra guardada para este tema.", "Nessun testo salvato per questo brano.", "Für diesen Titel ist kein Text gespeichert.", "Nenhuma letra guardada para esta faixa.", "この曲には保存された歌詞がありません。", "이 곡에는 저장된 가사가 없습니다.", "此曲目没有保存的歌词。", "呢首歌冇儲存歌詞。"],
    "Affiche les paroles et la structure du morceau, à sélectionner et copier.": ["Shows the track's lyrics and structure, to select and copy.", "Muestra la letra y la estructura del tema, para seleccionar y copiar.", "Mostra il testo e la struttura del brano, da selezionare e copiare.", "Zeigt Text und Struktur des Titels zum Markieren und Kopieren.", "Mostra a letra e a estrutura da faixa, para selecionar e copiar.", "曲の歌詞と構成を表示します。選択してコピーできます。", "곡의 가사와 구조를 표시합니다. 선택하여 복사할 수 있습니다.", "显示曲目的歌词和结构，可选择并复制。", "顯示歌曲嘅歌詞同結構，可以選取同複製。"],
}
langues = Path(sys.argv[1])
for i, code in enumerate(L):
    p = langues / f"{code}.json"
    d = json.loads(p.read_text(encoding="utf-8"))
    s = d["strings"]
    for k, v in T.items():
        assert len(v) == 9, k
        s[k] = v[i]
    p.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print("OK")
