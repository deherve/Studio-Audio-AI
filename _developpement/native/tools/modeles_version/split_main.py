# 1.1.014 : découpe main.cpp en app.hpp (types, fonctions libres en « inline », déclaration de App), app_*.cpp (fonctions de App)
# et main.cpp (point d'entrée, essais). Transformation mécanique : aucun changement de comportement.
# Usage : python -X utf8 split_main.py [--dry]   (relit main.cpp tel qu'au 1.1.013 depuis _sauvegardes\avant_1.1.014 ou depuis git)
import re, sys, os, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from split_analyze import scan, members, match_brace, strip_comments, classify

ROOT = r'C:\Users\deher\Downloads\dev\music\_developpement\native\src'
SRC = r'C:\Users\deher\Downloads\dev\music\_sauvegardes\avant_1.1.014\main.cpp'

# Fichiers d'implémentation de App : (nom, description, bornes de lignes d'origine [début, fin[ du début du membre)
PARTS = [
    ('app_models',  'Modèles, LoRA, structures et packs, analyse audio, langue chantée, recommandations, sauvegarde', 2426, 4632),
    ('app_form',    'Formulaire « Nouveau morceau » et traduction des paroles', 4632, 6076),
    ('app_ai',      'Composer avec l\'IA : morceau, anti-répétition, styles, Réglages › Styles', 6076, 7204),
    ('app_cover',   'Pochettes générées, album écrit par l\'IA', 7204, 8516),
    ('app_aiset',   'Réglages › IA, bibliothèque, cartes', 8516, 9755),
    ('app_score',   'Partition, Atelier partition, aide interrogeable, disque, tonalité, mise à jour, stems, sons', 9755, 12065),
    ('app_media',   'Jeux, lecture continue, pochettes, exports, vidéos', 12065, 13066),
    ('app_albums',  'Albums, aperçus, tri', 13066, 14698),
    ('app_ui',      'Fenêtres modales, Réglages, menu de gauche, image', 14698, 10 ** 9),
]


def line_of(text, pos):
    return text.count('\n', 0, pos) + 1


def mask_preprocessor(text, kind):
    """Lignes « #… » (avec continuations) : traitées comme des commentaires."""
    pos = 0
    n = len(text)
    while pos < n:
        e = text.find('\n', pos)
        e = n if e < 0 else e
        j = pos
        while j < e and text[j] in ' \t':
            j += 1
        if j < e and text[j] == '#' and kind[j] == 0:
            end = e
            while end > pos and text[end - 1] == '\\' and end < n:
                nl = text.find('\n', end + 1)
                end = n if nl < 0 else nl
            for k in range(pos, end):
                kind[k] = 1
            pos = end + 1
        else:
            pos = e + 1


def code_start(text, kind, a, b):
    k = a
    while k < b and (kind[k] != 0 or text[k] in ' \t\r\n'):
        k += 1
    return k


def region_edits(text, kind, a, b, edits, keep_static=()):
    """Fonctions et variables libres d'une région : « static » -> « inline » (une seule définition pour tous les .cpp)."""
    for item in members(text, kind, a, b):
        what, s0, bo, bc, head = item[:5]
        cs = code_start(text, kind, s0, bo)
        h = ' '.join(head.split())
        if what == 'block':
            k = classify(head)
            if re.match(r'^namespace\b', h):
                region_edits(text, kind, bo + 1, bc, edits, keep_static)
                continue
            if re.match(r'^extern\s+"C"', h):
                continue
            if k == 'type':
                continue
            if h.startswith('template'):
                continue
            if k == 'func':
                m = re.match(r'(static|inline|constexpr|extern)\b', h)
                if m and m.group(1) == 'static':
                    edits.append((cs, 6, 'inline'))
                elif not m:
                    edits.append((cs, 0, 'inline '))
            else:  # variable initialisée par accolades ou « = { »
                if h.startswith('static '):
                    edits.append((cs, 6, 'inline'))
        elif what == 'decl':
            if h.startswith('static '):
                name = re.search(r'(\w+)\s*\(', h)
                if name and name.group(1) in keep_static:
                    edits.append((cs, 7, ''))  # déclaration anticipée d'une fonction définie dans main.cpp : sans static
                else:
                    edits.append((cs, 6, 'inline'))


def apply_edits(text, edits):
    for pos, old, new in sorted(edits, key=lambda e: -e[0]):
        text = text[:pos] + new + text[pos + old:]
    return text


def last_paren_group(h):
    """(début, fin) de la dernière paire de parenthèses de premier niveau de h (la liste de paramètres)."""
    depth = 0
    start = None
    last = None
    in_str = None
    i = 0
    while i < len(h):
        c = h[i]
        if in_str:
            if c == '\\':
                i += 1
            elif c == in_str:
                in_str = None
        elif c in '"\'':
            in_str = c
        elif c == '(':
            if depth == 0:
                start = i
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                last = (start, i)
        i += 1
    return last


def strip_defaults(params):
    """Retire les valeurs par défaut (« = … ») d'une liste de paramètres (sans les parenthèses)."""
    out = []
    depth = 0
    skipping = False
    in_str = None
    i = 0
    while i < len(params):
        c = params[i]
        if in_str:
            if not skipping:
                out.append(c)
            if c == '\\':
                i += 1
                if not skipping and i < len(params):
                    out.append(params[i])
            elif c == in_str:
                in_str = None
        elif c in '"\'':
            in_str = c
            if not skipping:
                out.append(c)
        elif c in '([{<' and not (c == '<' and params[i - 1:i + 1] in ('<<',)):
            depth += 1
            if not skipping:
                out.append(c)
        elif c in ')]}>':
            depth -= 1
            if not skipping:
                out.append(c)
        elif c == '=' and depth == 0 and not skipping:
            # on retire aussi l'espace qui précède
            while out and out[-1] == ' ':
                out.pop()
            skipping = True
        elif c == ',' and depth == 0 and skipping:
            skipping = False
            out.append(c)
        elif not skipping:
            out.append(c)
        i += 1
    return ''.join(out)


def main():
    dry = '--dry' in sys.argv
    text = open(SRC, encoding='utf-8', newline='').read().replace('\r\n', '\n')
    kind = scan(text)
    mask_preprocessor(text, kind)
    m = re.search(r'^struct App \{', text, re.M)
    cls_open = m.end() - 1
    cls_close = match_brace(text, kind, cls_open)
    # « }; » de fin de classe
    after_cls = cls_close + 1
    assert text[after_cls] == ';'
    after_cls += 1
    pre = text[:m.start()]
    post = text[after_cls:]

    # ---- 1. région avant la classe : fonctions et variables libres en inline
    edits = []
    region_edits(text, kind, 0, m.start(), edits, keep_static=('build_fonts',))
    pre_new = apply_edits(text[:m.start()], [e for e in edits])

    # ---- 2. classe : déclarations, définitions déplacées
    mem = members(text, kind, cls_open + 1, cls_close)
    nested = set()
    for item in mem:
        if item[0] == 'block':
            hh = ' '.join(item[4].split())
            mm = re.match(r'^(?:struct|class|enum(?:\s+class)?|union)\s+(\w+)', hh)
            if mm:
                nested.add(mm.group(1))
        elif item[0] == 'decl':
            hh = ' '.join(item[4].split())
            mm = re.match(r'^using\s+(\w+)\s*=', hh)
            if mm:
                nested.add(mm.group(1))
    parts = {p[0]: [] for p in PARTS}
    cls_edits = []
    moved = 0
    for item in mem:
        if item[0] != 'block':
            continue
        what, s0, bo, bc, head = item[:5]
        if classify(head) != 'func':
            continue
        h = ' '.join(head.split())
        if h.startswith('template'):
            continue
        cs = code_start(text, kind, s0, bo)
        head_clean = re.sub(r'\s*\n\s*', ' ', strip_comments(text, kind, cs, bo)).strip()
        lead = text[s0:cs]
        grp = last_paren_group(head_clean)
        assert grp, head_clean
        p0, p1 = grp
        before, params, after = head_clean[:p0], head_clean[p0 + 1:p1], head_clean[p1 + 1:]
        nm = re.search(r'(~?\w+)\s*$', before)
        assert nm, head_clean
        name = nm.group(1)
        ret = before[:nm.start()]
        # définition hors classe : sans static, types imbriqués qualifiés dans le type de retour, sans valeurs par défaut
        ret_def = re.sub(r'\bstatic\s+', '', ret)
        for t in nested:
            ret_def = re.sub(r'(?<![\w:])' + t + r'\b', 'App::' + t, ret_def)
        defn = ret_def + 'App::' + name + '(' + strip_defaults(params) + ')' + after + ' ' + text[bo:bc + 1]
        line = line_of(text, cs)
        target = None
        for pname, _d, a, b2 in PARTS:
            if a <= line < b2:
                target = pname
        assert target, line
        # texte de tête (commentaires, bandeaux) : gardé avec la définition, retiré de la classe
        lead_txt = '\n'.join(l[4:] if l.startswith('    ') else l for l in lead.split('\n'))
        # corps : décalage de 4 espaces en moins (la classe indentait de 4)
        body_lines = defn.split('\n')
        defn = '\n'.join(body_lines[:1] + [l[4:] if l.startswith('    ') else l for l in body_lines[1:]])
        parts[target].append((lead_txt.strip('\n') + '\n' if lead_txt.strip() else '') + defn + '\n')
        decl = '    ' + head_clean + ';'
        cls_edits.append((s0, bc + 1 - s0, '\n' + decl if lead.strip() == '' else '\n' + decl))
        moved += 1
        # point-virgule éventuel après le corps
        if len(item) > 5 and item[5] == 'semi':
            cls_edits[-1] = (s0, item[6] + 1 - s0, cls_edits[-1][2])
    cls_text = apply_edits(text[cls_open + 1:cls_close], [(p - (cls_open + 1), o, n) for p, o, n in cls_edits])
    class_decl = 'struct App {' + cls_text + '};\n'

    # ---- 3. fichiers
    header = pre_new.rstrip('\n') + '\n\n' + class_decl
    header = '#pragma once\n// 1.1.014 : types, fonctions libres (inline) et déclaration de App (fonctions dans app_*.cpp et main.cpp). Voir tools\\modeles_version\\split_main.py.\n' + header
    outputs = {'app.hpp': header}
    for pname, desc, a, b2 in PARTS:
        body = '\n'.join(parts[pname])
        outputs[pname + '.cpp'] = ('// ' + desc + ' (fonctions de App ; déclarées dans app.hpp).\n#include "app.hpp"\n\n' + body)
    outputs['main.cpp'] = '#include "app.hpp"\n\n// ---------------------------------------------------------------------------- Point d\'entrée (essais, fenêtre, boucle)\n' + post.lstrip('\n')
    print('fonctions déplacées :', moved, '; types imbriqués :', len(nested))
    for k, v in outputs.items():
        print(k, v.count('\n'), 'lignes')
    if dry:
        return
    for k, v in outputs.items():
        open(os.path.join(ROOT, k), 'w', encoding='utf-8', newline='').write(v.replace('\n', '\r\n'))
    print('OK')


main()
