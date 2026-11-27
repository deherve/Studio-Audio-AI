# 1.1.019 : graphe de filtres du clip plus rapide (jonctions sans chaîne de xfade, masque de vignette en yuva420p).
# Usage : python -X utf8 patch_1119.py   (clip.cpp et version.hpp sont en CRLF : on garde les fins de ligne)
import re
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


OLD = '''    std::vector<int> seen(n_img, 0);
    for (int i = 0; i < count; i++) {
        int          k = shot_image[i], j = seen[k]++;
        const Shot & s = SHOTS[(k * 3 + j) % 7];  // variantes de caméra différentes d'une image à l'autre
        int          d = (int) std::lround((cuts[i + 1] - cuts[i] + FADE) * FPS);
        snprintf(buf, sizeof(buf),
                 "[i%d_%d]trim=end_frame=1,zoompan=z='%.3f+(%.3f-%.3f)*on/%d':x='iw*%.3f-iw/zoom/2':y='ih*%.3f-ih/zoom/2':d=%d:s=%dx%d:fps=%d,setsar=1[s%d];",
                 k, j, s.z0, s.z1, s.z0, d, s.cx, s.cy, d, width, height, FPS, i);
        g += buf;
    }
    std::string last = "s0";
    for (int i = 1; i < count; i++) {
        snprintf(buf, sizeof(buf), "[%s][s%d]xfade=transition=fade:duration=%.2f:offset=%.3f[x%d];", last.c_str(), i, FADE, cuts[i] - FADE, i);
        g += buf;
        last = "x" + std::to_string(i);
    }
'''

NEW = '''    std::vector<int> seen(n_img, 0);
    // 1.1.019 : jonctions sans chaîne de xfade. Avant, 35 xfade à la suite recopiaient chaque image 35 fois (mesuré : 15,8 s pour
    // 60 s de clip, 11,7 s avec ce montage, image identique à 45 dB près). Chaque plan est coupé en trois : corps, queue (fondu vers
    // le plan suivant) et tête (fondu depuis le plan précédent) ; seules les queues et les têtes (0,6 s) passent par xfade, puis tout est
    // mis bout à bout par concat. Les plans plus courts que 2 fondus gardent l'ancienne chaîne.
    const int FR     = (int) std::lround(FADE * FPS);
    bool      simple = true;
    for (int i = 0; i < count; i++)
        if (cuts[i + 1] - cuts[i] < 2 * FADE) simple = false;
    std::vector<int> frames(count);
    for (int i = 0; i < count; i++) frames[i] = (int) std::lround((cuts[i + 1] - cuts[i] + FADE) * FPS);
    for (int i = 0; i < count; i++) {
        int          k = shot_image[i], j = seen[k]++;
        const Shot & s = SHOTS[(k * 3 + j) % 7];  // variantes de caméra différentes d'une image à l'autre
        int          d = frames[i];
        snprintf(buf, sizeof(buf),
                 "[i%d_%d]trim=end_frame=1,zoompan=z='%.3f+(%.3f-%.3f)*on/%d':x='iw*%.3f-iw/zoom/2':y='ih*%.3f-ih/zoom/2':d=%d:s=%dx%d:fps=%d,setsar=1",
                 k, j, s.z0, s.z1, s.z0, d, s.cx, s.cy, d, width, height, FPS);
        g += buf;
        if (!simple) {
            g += "[s" + std::to_string(i) + "];";
            continue;
        }
        bool has_tail = i < count - 1, has_head = i > 0;
        int  n_out    = 1 + (has_tail ? 1 : 0) + (has_head ? 1 : 0);
        std::string id = std::to_string(i);
        if (n_out == 1) {
            g += "[b" + id + "];";
        } else {
            g += ",split=" + std::to_string(n_out) + "[b" + id + "]";
            if (has_tail) g += "[t" + id + "]";
            if (has_head) g += "[h" + id + "]";
            g += ";";
        }
        // Le premier plan commence à 0 s, les autres 0,6 s plus tôt (début du fondu) : leur corps saute donc la tête.
        int body_from = has_head ? FR : 0;
        int body_to   = has_tail ? d - FR - (i == 0 ? FR : 0) : d;
        snprintf(buf, sizeof(buf), "[b%d]trim=start_frame=%d:end_frame=%d,setpts=PTS-STARTPTS[body%d];", i, body_from, body_to, i);
        g += buf;
        if (has_tail) {
            int from = d - FR - (i == 0 ? FR : 0);
            snprintf(buf, sizeof(buf), "[t%d]trim=start_frame=%d:end_frame=%d,setpts=PTS-STARTPTS[tail%d];", i, from, from + FR, i);
            g += buf;
        }
        if (has_head) {
            snprintf(buf, sizeof(buf), "[h%d]trim=start_frame=0:end_frame=%d,setpts=PTS-STARTPTS[head%d];", i, FR, i);
            g += buf;
        }
    }
    std::string last;
    if (simple) {
        std::string parts;
        int         n_parts = 0;
        for (int i = 0; i < count; i++) {
            parts += "[body" + std::to_string(i) + "]";
            n_parts++;
            if (i < count - 1) {
                snprintf(buf, sizeof(buf), "[tail%d][head%d]xfade=transition=fade:duration=%.2f:offset=0[xf%d];", i, i + 1, FADE, i);
                g += buf;
                parts += "[xf" + std::to_string(i) + "]";
                n_parts++;
            }
        }
        if (n_parts == 1) {
            last = "body0";
        } else {
            g += parts + "concat=n=" + std::to_string(n_parts) + ":v=1:a=0[last];";
            last = "last";
        }
    } else {
        last = "s0";
        for (int i = 1; i < count; i++) {
            snprintf(buf, sizeof(buf), "[%s][s%d]xfade=transition=fade:duration=%.2f:offset=%.3f[x%d];", last.c_str(), i, FADE, cuts[i] - FADE, i);
            g += buf;
            last = "x" + std::to_string(i);
        }
    }
'''

MASK_OLD = '"[vn][va]alphamerge,loop=-1:1:0[vm];",'
MASK_NEW = '"[vn][va]alphamerge,format=yuva420p,loop=-1:1:0[vm];",  // 1.1.019 : masque déjà en yuva420p (sans conversion à chaque image)'

patch('clip.cpp', [(OLD, NEW), (MASK_OLD, MASK_NEW)])
print('OK')
