# 1.1.013 : media.cpp (fils des travaux protégés) et update_check.cpp (coupure UTF-8)
SRC = r'C:\Users\deher\Downloads\dev\music\_developpement\native\src'


def load(name):
    s = open(SRC + '\\' + name, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')


def save(name, s):
    open(SRC + '\\' + name, 'w', encoding='utf-8', newline='').write(s)


s, nl = load('media.cpp')
old = 'job->thread = std::thread([=]() mutable {'
assert s.count(old) == 10, s.count(old)
s = s.replace(old, 'job->thread = guarded(job, [=]() mutable {')
anchor = "// ffmpeg avec suivi du processus (pour l'annulation). Renvoie le code de sortie."
assert s.count(anchor) == 1
s = s.replace(anchor, '''// 1.1.013 : fil d'un travail de fond protégé. Une erreur imprévue (fichier illisible, disque plein, mémoire…) termine le
// travail « en échec » avec son message, notée dans le journal ; avant, elle arrêtait tout le Studio (std::terminate).
template <class F>
static std::thread guarded(const std::shared_ptr<Job> & job, F && body) {
    return std::thread([job, body = std::forward<F>(body)]() mutable {
        try {
            body();
        } catch (const std::exception & e) {
            journal::error("travail", job->name + " : erreur imprévue : " + e.what());
            finish_job(*job, false, e.what(), {});
        }
    });
}

'''.replace('\n', nl) + anchor)
inc = '#include "i18n.hpp"' + nl
assert s.count(inc) == 1
s = s.replace(inc, inc + '#include "journal.hpp"' + nl)
save('media.cpp', s)

s, nl = load('update_check.cpp')
old = '    if (notes.size() > 600) notes = notes.substr(0, 600) + "…";'
assert s.count(old) == 1
s = s.replace(old, '''    if (notes.size() > 600) {
        size_t cut = 600;
        while (cut > 0 && ((unsigned char) notes[cut] & 0xC0) == 0x80) cut--;  // 1.1.013 : jamais au milieu d'un caractère accentué
        notes = notes.substr(0, cut) + "…";
    }'''.replace('\n', nl))
save('update_check.cpp', s)
print('OK')
