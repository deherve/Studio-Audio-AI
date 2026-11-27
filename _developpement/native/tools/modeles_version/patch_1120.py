# 1.1.020 : morceau long coupé en 3 parties d'après la durée estimée (et non seulement d'après le nombre de lignes / sections),
# avec un seuil plus bas sur une carte de 12 Go ou moins. Usage : python -X utf8 patch_1120.py
SRC = r'C:\Users\deher\Downloads\dev\music\_developpement\native\src'


def patch(name, pairs):
    p = SRC + '\\' + name
    raw = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in raw
    s = raw.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:60])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


patch('studio.hpp', [(
    'double       estimate_seconds(const std::string & lyrics, bool instrumental);',
    'double       estimate_seconds(const std::string & lyrics, bool instrumental, double cap = 360);  // 1.1.020 : plafond réglable (360 s par défaut)')])

patch('studio.cpp', [
    ('double estimate_seconds(const std::string & lyrics, bool instrumental) {',
     'double estimate_seconds(const std::string & lyrics, bool instrumental, double cap) {'),
    ('    return std::clamp(total, 30.0, 360.0);\n}\n\nint run_command',
     '    return std::clamp(total, 30.0, cap);\n}\n\nint run_command')])

patch('app_form.cpp', [(
    '''        const bool three = ai_sung_lines(words) >= 60 || (int) std::count(words.begin(), words.end(), '[') >= 18;
''',
    '''        bool three = ai_sung_lines(words) >= 60 || (int) std::count(words.begin(), words.end(), '[') >= 18;
        // 1.1.020 : aussi 3 parties quand la durée estimée de chaque moitié dépasse ce que la carte supporte (sur 12 Go : « Epic Journey »,
        // 2 parties de 251 s et 290 s, a manqué de mémoire vidéo ; 3 parties de ≈ 180 s restent sous 220 s)
        {
            const GpuMem   gm         = gpu_monitor.get();
            const bool     small_card = gm.total > 0 && gm.total <= (unsigned long long) (12.5 * 1073741824.0);
            const double   target     = small_card ? 220.0 : 330.0;
            const double   total_est  = estimate_seconds(words, f.instrumental, 3600);
            if (!three && total_est > 2 * target) {
                three = true;
                notify(trf("Morceau long : coupé en 3 parties d'environ %s chacune pour tenir dans la mémoire de la carte graphique.", fmt_time(total_est / 3).c_str()));
            }
        }
''')])
print('OK')
