# 1.1.021 (suite) : réglage ai_polish dans app.hpp (décoché par défaut) et numéro de version 1.1.021.
SRC = r'C:\Users\deher\Downloads\dev\music\_developpement\native\src'


def patch(name, pairs, replace_all=False):
    p = SRC + '\\' + name
    raw = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in raw
    s = raw.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) >= 1 and (replace_all or s.count(old) == 1), (name, s.count(old), old[:70])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    assert len(s) > 1000
    open(p, 'wb').write(s.encode('utf-8'))


patch('app.hpp', [
    ('    bool                ai_natural = true;  // 1.1.018 : consignes d\'écriture naturelle pour les paroles écrites par l\'IA\n',
     '    bool                ai_natural = true;  // 1.1.018 : consignes d\'écriture naturelle pour les paroles écrites par l\'IA\n'
     '    bool                ai_polish  = false; // 1.1.021 : passe de relecture des paroles (réécrit les lignes qui sonnent « IA ») ; décochée : +1 min par chanson\n'),
    ('    v = yyjson_obj_get(r, "ai_natural");\n    if (yyjson_is_bool(v)) s.ai_natural = yyjson_get_bool(v);\n',
     '    v = yyjson_obj_get(r, "ai_natural");\n    if (yyjson_is_bool(v)) s.ai_natural = yyjson_get_bool(v);\n'
     '    v = yyjson_obj_get(r, "ai_polish");\n    if (yyjson_is_bool(v)) s.ai_polish = yyjson_get_bool(v);\n'),
    ('    yyjson_mut_obj_add_bool(doc, o, "ai_natural", s.ai_natural);\n',
     '    yyjson_mut_obj_add_bool(doc, o, "ai_natural", s.ai_natural);\n    yyjson_mut_obj_add_bool(doc, o, "ai_polish", s.ai_polish);\n')])
patch('version.hpp', [('1.1.018', '1.1.021')], replace_all=True)
print('OK')
