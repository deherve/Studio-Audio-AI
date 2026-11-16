# Analyse de struct App (main.cpp) : liste les membres de premier niveau (fonctions définies dans la classe, données, types imbriqués).
import re, sys

SRC = r'C:\Users\deher\Downloads\dev\music\_developpement\native\src\main.cpp'


def scan(text):
    """Renvoie pour chaque position : 'code', 'comment' ou 'string' (on ignore le contenu des deux derniers)."""
    n = len(text)
    kind = bytearray(n)  # 0 code, 1 commentaire, 2 chaîne ou caractère
    i = 0
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
            for k in range(i, j):
                kind[k] = 1
            i = j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                kind[k] = 1
            i = j
        elif c == 'R' and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            m = re.match(r'R"([^(\s]*)\(', text[i:])
            if m:
                end = ')' + m.group(1) + '"'
                j = text.find(end, i)
                j = n if j < 0 else j + len(end)
                for k in range(i, j):
                    kind[k] = 2
                i = j
            else:
                i += 1
        elif c == '"' or c == "'":
            # apostrophe de séparation de chiffres (1'000) : précédée d'un chiffre ou lettre hexadécimale et suivie d'un chiffre
            if c == "'" and i > 0 and text[i - 1].isalnum() and i + 1 < n and text[i + 1].isalnum() and not text[i - 1].isalpha():
                i += 1
                continue
            j = i + 1
            while j < n and text[j] != c:
                if text[j] == '\\':
                    j += 1
                j += 1
            j += 1
            for k in range(i, min(j, n)):
                kind[k] = 2
            i = j
        else:
            i += 1
    return kind


def members(text, kind, start, end):
    """Membres au niveau 1 entre start et end (indices du texte, après « { » de la classe et avant « } »)."""
    out = []
    i = start
    depth = 0
    stmt_start = start
    paren = 0
    while i < end:
        if kind[i] != 0:
            i += 1
            continue
        c = text[i]
        if c in '([':
            paren += 1
        elif c in ')]':
            paren -= 1
        elif c == '{':
            if depth == 0 and paren == 0:
                # début d'un bloc au niveau 1 : fonction, type imbriqué, ou initialiseur « = { »
                head = strip_comments(text, kind, stmt_start, i)
                j = match_brace(text, kind, i)
                out.append(('block', stmt_start, i, j, head))
                i = j + 1
                # après un bloc : « ; » éventuel (type imbriqué, initialiseur)
                k = i
                while k < end and (text[k] in ' \t\r\n' or kind[k] == 1):
                    k += 1
                if k < end and text[k] == ';':
                    out[-1] = out[-1] + ('semi', k)
                    i = k + 1
                stmt_start = i
                continue
        elif c == ';' and paren == 0:
            head = strip_comments(text, kind, stmt_start, i)
            out.append(('decl', stmt_start, i, i, head))
            stmt_start = i + 1
        i += 1
    return out


def strip_comments(text, kind, a, b):
    return ''.join(text[k] if kind[k] == 0 else (' ' if kind[k] == 1 else text[k]) for k in range(a, b))


def match_brace(text, kind, i):
    depth = 0
    n = len(text)
    while i < n:
        if kind[i] == 0:
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0:
                    return i
        i += 1
    raise RuntimeError('accolade non fermée')


def classify(head):
    h = ' '.join(head.split())
    if re.match(r'^(struct|class|enum|union)\b', h) or re.match(r'^(typedef|using)\b', h):
        return 'type'
    if re.match(r'^(public|private|protected)\s*:', h):
        return 'access'
    if h.endswith('=') or re.search(r'=\s*$', h):
        return 'init'
    if '(' in h:
        return 'func'
    return 'other'


if __name__ == '__main__':
    text = open(SRC, encoding='utf-8', newline='').read().replace('\r\n', '\n')
    kind = scan(text)
    m = re.search(r'^struct App \{', text, re.M)
    a = m.end()
    b = match_brace(text, kind, m.end() - 1)
    mem = members(text, kind, a, b)
    stats = {}
    for item in mem:
        k = classify(item[4]) if item[0] == 'block' else 'data'
        stats[k] = stats.get(k, 0) + 1
    print('membres', len(mem), stats)
    line_of = lambda pos: text.count('\n', 0, pos) + 1
    if len(sys.argv) > 1:
        for item in mem:
            k = classify(item[4]) if item[0] == 'block' else 'data'
            if k == sys.argv[1]:
                print(line_of(item[1]), '|', ' '.join(item[4].split())[:140])
