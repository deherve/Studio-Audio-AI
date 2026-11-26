# Traductions de la 1.1.018 (paroles plus naturelles). Usage : python fill_1118.py <ressources\langues>
# Ordre : en, es, it, de, pt, ja, ko, zh-Hans, yue. Écrites sans relecture par un locuteur natif.
import json, sys
from pathlib import Path
L = ["en", "es", "it", "de", "pt", "ja", "ko", "zh-Hans", "yue"]
T = {
 "Paroles plus naturelles : consignes anti-style « IA » (humanizer)": [
  "More natural lyrics: anti “AI style” rules (humanizer)", "Letras más naturales: reglas contra el estilo «IA» (humanizer)",
  "Testi più naturali: regole contro lo stile «IA» (humanizer)", "Natürlichere Texte: Regeln gegen den „KI-Stil“ (humanizer)",
  "Letras mais naturais: regras contra o estilo «IA» (humanizer)", "より自然な歌詞：「AIっぽさ」を避けるルール（humanizer）",
  "더 자연스러운 가사: 'AI 문체' 방지 규칙(humanizer)", "更自然的歌词：避免“AI 腔”的规则（humanizer）", "更自然嘅歌詞：避免「AI 腔」嘅規則（humanizer）"],
 "Ajoute aux demandes de paroles des règles d'écriture naturelle dans la langue choisie : images concrètes, pas de « ce n'est pas X, c'est Y », pas de formules toutes faites ni de calques de l'anglais (règles supplémentaires pour le français). Inspiré des skills humanizer et humanizer-fr (MIT).": [
  "Adds natural-writing rules to the lyrics requests, in the chosen language: concrete images, no “it's not X, it's Y”, no stock phrases or calques from English (extra rules for French). Inspired by the humanizer and humanizer-fr skills (MIT).",
  "Añade a las peticiones de letras reglas de escritura natural en el idioma elegido: imágenes concretas, nada de «no es X, es Y», ni frases hechas ni calcos del inglés (reglas adicionales para el francés). Inspirado en los skills humanizer y humanizer-fr (MIT).",
  "Aggiunge alle richieste di testi regole di scrittura naturale nella lingua scelta: immagini concrete, niente «non è X, è Y», niente frasi fatte né calchi dall'inglese (regole aggiuntive per il francese). Ispirato agli skill humanizer e humanizer-fr (MIT).",
  "Fügt den Textanfragen Regeln für natürliches Schreiben in der gewählten Sprache hinzu: konkrete Bilder, kein „Es ist nicht X, es ist Y“, keine Floskeln und keine Lehnübersetzungen aus dem Englischen (zusätzliche Regeln für Französisch). Inspiriert von den Skills humanizer und humanizer-fr (MIT).",
  "Acrescenta aos pedidos de letras regras de escrita natural na língua escolhida: imagens concretas, nada de «não é X, é Y», nem frases feitas nem decalques do inglês (regras adicionais para o francês). Inspirado nos skills humanizer e humanizer-fr (MIT).",
  "歌詞の依頼に、選んだ言語での自然な書き方のルールを追加します：具体的なイメージ、「XではなくY」の言い回しや決まり文句、英語直訳の表現を避けます（フランス語には追加ルールあり）。humanizer と humanizer-fr スキル（MIT）に着想を得ています。",
  "가사 요청에 선택한 언어의 자연스러운 글쓰기 규칙을 추가합니다. 구체적인 이미지, 'X가 아니라 Y' 표현 금지, 상투적인 문구와 영어 직역 금지(프랑스어는 추가 규칙). humanizer 및 humanizer-fr 스킬(MIT)에서 영감을 받았습니다.",
  "在歌词请求中加入所选语言的自然写作规则：使用具体意象，避免“不是X而是Y”、套话和英语直译（法语另有补充规则）。灵感来自 humanizer 和 humanizer-fr 技能（MIT）。",
  "喺歌詞請求入面加入所選語言嘅自然寫作規則：用具體意象，避免「唔係X而係Y」、套語同英文直譯（法文另有補充規則）。靈感來自 humanizer 同 humanizer-fr 技能（MIT）。"],
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
