# 1.1.018 : paroles ecrites par l'IA : consignes d'ecriture naturelle (inspirees des skills humanizer et humanizer-fr, MIT).
import io, sys
SRC = r"C:\Users\deher\Downloads\dev\music\_developpement\native\src" + "\\"

def load(n):
    return io.open(SRC + n, encoding="utf-8", newline="").read().replace("\r\n", "\n")

def save(n, t):
    io.open(SRC + n, "w", encoding="utf-8", newline="").write(t.replace("\n", "\r\n"))

def rep(t, old, new, count=1):
    n = t.count(old)
    if n != count:
        sys.exit("ERREUR : %d occurrence(s) au lieu de %d pour : %r" % (n, count, old[:90]))
    return t.replace(old, new)

# ---------------------------------------------------------------- ai_writer.hpp
h = load("ai_writer.hpp")
h = rep(h, "std::string ai_user_prompt(const AiRequest & r);\n",
"""std::string ai_user_prompt(const AiRequest & r);
// 1.1.018 : consignes d'ecriture naturelle ajoutees aux demandes de paroles (reglage « ai_natural », Reglages > IA).
void        ai_set_natural(bool on);
""")
save("ai_writer.hpp", h)

# ---------------------------------------------------------------- ai_writer.cpp
c = load("ai_writer.cpp")
c = rep(c, "// trim est défini plus bas (lecture des réponses) : petite copie pour les consignes.\n",
r'''// 1.1.018 : écriture naturelle. Règles inspirées des skills « humanizer » (blader, MIT) et « humanizer-fr » (ferr079, MIT),
// réduites à ce qui sert à des paroles (consignes courtes : une IA locale en suit peu à la fois). Désactivable (Réglages › IA).
static std::atomic<bool> g_natural{ true };
void ai_set_natural(bool on) { g_natural = on; }

static std::string natural_rules(const std::string & lang) {
    std::string p =
        "\nNatural writing (important): write like a native songwriter of " + lang + " speaks and writes, not like a chatbot. "
        "Each line must say something concrete (a place, an object, a gesture, a person) instead of announcing a feeling. "
        "Do not write lines built as \"it's not X, it's Y\" or \"not just X but Y\", nor lines that sound like a deep saying "
        "(\"at the core\", \"the real question\"), nor lists of three abstract nouns used as filler. "
        "Avoid stock AI words and images (tapestry, symphony, echoes, whispers, journey, beacon, embrace, a dance of light and shadow, "
        "testament), inflated or promotional words, and word-for-word translations of English expressions. "
        "Vary the length of the lines and keep a spoken rhythm; only the chorus repeats identically. "
        "Never use dashes or markdown inside a line.";
    if (lang == "French")
        p += "\nFrench lyrics: use everyday French as it is sung, with the elisions and contractions a French songwriter would write. "
             "Avoid calques from English (\"adresser un problème\", \"supporter\" for \"to support\", \"délivrer\", \"au final\", \"faire du sens\", "
             "\"une opportunité\" for a chance), report-style connectors (ainsi, en effet, par ailleurs, de plus, notamment), "
             "empty verbs and phrases (mettre en lumière, s'inscrire dans, venir + infinitive, se veut), filler words (véritable, essentiel, "
             "crucial, fluide, robuste, pierre angulaire), and lines like \"pas X mais Y\" or \"ce n'est pas X, c'est Y\". "
             "Prefer the plain verbs être and avoir.";
    return p;
}

// trim est défini plus bas (lecture des réponses) : petite copie pour les consignes.
''')
c = rep(c, '        p += "\\nUse fresh, concrete and surprising images; avoid the usual clichés of AI-written songs.";\n',
           '        p += "\\nUse fresh, concrete and surprising images; avoid the usual clichés of AI-written songs.";\n        if (g_natural) p += natural_rules(lang);  // 1.1.018\n')
c = rep(c, '        u += "\\n\\nReply with the new sections only, as plain text with their [Section] tags: no JSON, no comment.";',
           '        if (g_natural) u += natural_rules(song.language.empty() ? std::string("English") : song.language);  // 1.1.018\n        u += "\\n\\nReply with the new sections only, as plain text with their [Section] tags: no JSON, no comment.";')
save("ai_writer.cpp", c)

# ---------------------------------------------------------------- app.hpp (reglage)
a = load("app.hpp")
a = rep(a, "    bool                save_group_style = false;",
           "    bool                ai_natural = true;  // 1.1.018 : consignes d'écriture naturelle pour les paroles écrites par l'IA\n    bool                save_group_style = false;")
a = rep(a, '    v = yyjson_obj_get(r, "save_group_style");\n',
           '    v = yyjson_obj_get(r, "ai_natural");\n    if (yyjson_is_bool(v)) s.ai_natural = yyjson_get_bool(v);\n    v = yyjson_obj_get(r, "save_group_style");\n')
a = rep(a, '    yyjson_mut_obj_add_bool(doc, o, "save_group_style", s.save_group_style);\n',
           '    yyjson_mut_obj_add_bool(doc, o, "ai_natural", s.ai_natural);\n    yyjson_mut_obj_add_bool(doc, o, "save_group_style", s.save_group_style);\n')
save("app.hpp", a)

# ---------------------------------------------------------------- app_ai.cpp : applique le reglage avant chaque demande
p = load("app_ai.cpp")
p = rep(p, "    r.avoid        = ai_avoid_words();\n    r.negative     = join_style(split_style(settings.ai_negative));  // 1.0.34\n",
           "    r.avoid        = ai_avoid_words();\n    ai_set_natural(settings.ai_natural);  // 1.1.018\n    r.negative     = join_style(split_style(settings.ai_negative));  // 1.0.34\n")
save("app_ai.cpp", p)

# ---------------------------------------------------------------- app_aiset.cpp : case dans Reglages > IA
s = load("app_aiset.cpp")
s = rep(s, '    if (ImGui::Checkbox(tr("Éviter aussi les mots qui reviennent dans les derniers morceaux écrits par l\'IA"), &settings.ai_auto_avoid)) save_settings(settings, settings_path);\n',
'''    if (ImGui::Checkbox(tr("Paroles plus naturelles : consignes anti-style « IA » (humanizer)"), &settings.ai_natural)) save_settings(settings, settings_path);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("Ajoute aux demandes de paroles des règles d'écriture naturelle dans la langue choisie : images concrètes, pas de « ce n'est pas X, c'est Y », pas de formules toutes faites ni de calques de l'anglais (règles supplémentaires pour le français). Inspiré des skills humanizer et humanizer-fr (MIT)."));
''' + '    if (ImGui::Checkbox(tr("Éviter aussi les mots qui reviennent dans les derniers morceaux écrits par l\'IA"), &settings.ai_auto_avoid)) save_settings(settings, settings_path);\n')
save("app_aiset.cpp", s)

v = load("version.hpp")
v = rep(v, "1.1.017", "1.1.018", 2)
save("version.hpp", v)
print("OK")
