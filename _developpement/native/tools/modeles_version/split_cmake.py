# 1.1.014 : CMakeLists.txt, i18n_sync.py et main.cpp (build_fonts non static) pour la découpe de main.cpp
files = ['app_models', 'app_form', 'app_ai', 'app_cover', 'app_aiset', 'app_score', 'app_media', 'app_albums', 'app_ui']
base = r'C:\Users\deher\Downloads\dev\music\_developpement\native'

p = base + r'\CMakeLists.txt'
c = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in c else '\n'
a = '    src/main.cpp' + nl + '    src/studio.cpp'
if 'src/app_ui.cpp' not in c:
    assert c.count(a) == 1
    c = c.replace(a, '    src/main.cpp' + nl + ''.join('    src/%s.cpp%s' % (f, nl) for f in files) + '    src/studio.cpp')
    open(p, 'w', encoding='utf-8', newline='').write(c)

p = base + r'\tools\i18n_sync.py'
i = open(p, encoding='utf-8', newline='').read()
a = 'FILES = ["main.cpp",'
if '"app.hpp"' not in i:
    assert i.count(a) == 1
    i = i.replace(a, 'FILES = ["main.cpp", "app.hpp", ' + ', '.join('"%s.cpp"' % f for f in files) + ',')
    open(p, 'w', encoding='utf-8', newline='').write(i)

p = base + r'\src\main.cpp'
m = open(p, encoding='utf-8', newline='').read()
a = 'static void build_fonts(bool runtime) {'
if a in m:
    m = m.replace(a, 'void build_fonts(bool runtime) {')
    open(p, 'w', encoding='utf-8', newline='').write(m)
print('OK')
