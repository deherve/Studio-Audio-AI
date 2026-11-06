#include "clip.hpp"
#include "i18n.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "yyjson.h"

static std::string trim(const std::string & s) {
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

// Mot comparable : minuscules ASCII, lettres et chiffres seulement ; les octets ≥ 0x80 (accents) sont gardés.
static std::string norm_word(const std::string & w) {
    std::string out;
    for (unsigned char c : w) {
        if (c >= 0x80) out += (char) c;
        else if (isalnum(c)) out += (char) tolower(c);
    }
    return out;
}

double clip_short_start(const std::vector<ClipLine> & lines, double duration, double length) {
    if (duration <= length) return 0;
    auto chorus = [](const std::string & section) {
        std::string s;
        for (unsigned char c : section) s += (char) tolower(c);
        return (s.find("chorus") != std::string::npos || s.find("refrain") != std::string::npos || s.find("hook") != std::string::npos) &&
               s.find("pre") == std::string::npos;
    };
    double first = -1, pick = -1;
    for (size_t i = 0; i < lines.size(); i++) {
        if (!chorus(lines[i].section) || (i > 0 && lines[i - 1].section == lines[i].section)) continue;  // début d'un refrain
        if (first < 0) first = lines[i].start;
        if (pick < 0 && lines[i].start >= 20) pick = lines[i].start;
    }
    double at = pick >= 0 ? pick - 2 : first >= 0 ? first - 2 : duration * 0.35;
    return std::clamp(at, 0.0, duration - length);
}

std::vector<ClipLine> clip_window_lines(const std::vector<ClipLine> & lines, double start, double length) {
    std::vector<ClipLine> out;
    for (ClipLine l : lines) {
        if (l.end <= start || l.start >= start + length) continue;
        l.start = std::max(0.0, l.start - start);
        l.end   = std::min(length, l.end - start);
        out.push_back(l);
    }
    return out;
}

static std::vector<std::string> split_words(const std::string & text) {
    std::vector<std::string> out;
    size_t                   i = 0;
    while (i < text.size()) {
        while (i < text.size() && isspace((unsigned char) text[i])) i++;
        size_t b = i;
        while (i < text.size() && !isspace((unsigned char) text[i])) i++;
        std::string w = norm_word(text.substr(b, i - b));
        if (!w.empty()) out.push_back(w);
    }
    return out;
}

std::vector<ClipLine> clip_lyrics_lines(const std::string & lyrics) {
    std::vector<ClipLine> out;
    std::string           section = "Intro";
    size_t                i       = 0;
    while (i <= lyrics.size()) {
        size_t      e    = lyrics.find('\n', i);
        std::string line = trim(lyrics.substr(i, e == std::string::npos ? std::string::npos : e - i));
        i                = e == std::string::npos ? lyrics.size() + 1 : e + 1;
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        ClipLine l;
        l.text    = line;
        l.section = section;
        out.push_back(l);
    }
    return out;
}

std::vector<ClipWord> clip_parse_words(const std::string & json) {
    std::vector<ClipWord> out;
    yyjson_doc *          doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) return out;
    size_t       i, n;
    yyjson_val * seg;
    yyjson_arr_foreach(yyjson_obj_get(yyjson_doc_get_root(doc), "transcription"), i, n, seg) {
        yyjson_val * t = yyjson_obj_get(seg, "text");
        yyjson_val * o = yyjson_obj_get(seg, "offsets");
        if (!yyjson_is_str(t) || !o) continue;
        std::string text = trim(yyjson_get_str(t));
        if (norm_word(text).empty() || text.front() == '[' || text.front() == '(') continue;  // [Music], (…)
        ClipWord w;
        w.text  = text;
        w.start = yyjson_get_num(yyjson_obj_get(o, "from")) / 1000.0;
        w.end   = yyjson_get_num(yyjson_obj_get(o, "to")) / 1000.0;
        out.push_back(w);
    }
    yyjson_doc_free(doc);
    return out;
}

double clip_align(std::vector<ClipLine> & lines, const std::vector<ClipWord> & heard, double duration) {
    // mots attendus, avec la ligne d'où ils viennent
    std::vector<std::string> exp, hrd;
    std::vector<int>         owner;
    for (size_t k = 0; k < lines.size(); k++)
        for (const std::string & w : split_words(lines[k].text)) {
            exp.push_back(w);
            owner.push_back((int) k);
        }
    for (const ClipWord & w : heard) hrd.push_back(norm_word(w.text));

    // plus longue suite commune (programmation dynamique) ; shortcut: O(n·m) en mémoire, un morceau reste sous ~1000 mots
    size_t                n = exp.size(), m = hrd.size();
    std::vector<int>      dp((n + 1) * (m + 1), 0);
    auto                  at = [&](size_t a, size_t b) -> int & { return dp[a * (m + 1) + b]; };
    for (size_t a = n; a-- > 0;)
        for (size_t b = m; b-- > 0;) at(a, b) = exp[a] == hrd[b] ? at(a + 1, b + 1) + 1 : std::max(at(a + 1, b), at(a, b + 1));

    std::vector<double> first(lines.size(), -1), last(lines.size(), -1);
    size_t              matched = 0;
    for (size_t a = 0, b = 0; a < n && b < m;) {
        if (exp[a] == hrd[b]) {
            int k = owner[a];
            if (first[k] < 0) first[k] = heard[b].start;
            last[k] = heard[b].end;
            matched++;
            a++;
            b++;
        } else if (at(a + 1, b) >= at(a, b + 1)) {
            a++;
        } else {
            b++;
        }
    }

    // lignes sans mot reconnu : réparties entre la fin de la précédente connue et le début de la suivante connue
    for (size_t k = 0; k < lines.size(); k++) {
        if (first[k] >= 0) continue;
        int p = (int) k - 1, q = (int) k + 1;
        while (p >= 0 && first[p] < 0) p--;
        while (q < (int) lines.size() && first[q] < 0) q++;
        double t0 = p >= 0 ? last[p] : 0, t1 = q < (int) lines.size() ? first[q] : duration;
        int    count = q - p - 1, idx = (int) k - p - 1;
        double step = (t1 - t0) / std::max(1, count);
        first[k]    = t0 + step * idx;
        last[k]     = first[k] + step * 0.9;
        // les lignes voisines sans mot reconnu s'appuient sur celles-ci : on les marque comme connues pour la suite
    }
    for (size_t k = 0; k < lines.size(); k++) {
        lines[k].start = first[k];
        lines[k].end   = std::max(last[k], first[k] + 0.8);
    }
    return n ? (double) matched / (double) n : 0;
}

// Majuscules UTF-8 (accents compris) via Windows.
static std::string upper(const std::string & s) {
    if (s.empty()) return s;
    int          wn = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
    std::wstring w((size_t) wn, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), w.data(), wn);
    CharUpperBuffW(w.data(), (DWORD) w.size());
    int         un = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t) un, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(), out.data(), un, nullptr, nullptr);
    return out;
}

static std::string ass_time(double t) {
    if (t < 0) t = 0;
    int    h = (int) (t / 3600), mi = (int) (t / 60) % 60;
    double s = std::fmod(t, 60.0);
    char   buf[40];
    snprintf(buf, sizeof(buf), "%d:%02d:%05.2f", h, mi, s);
    return buf;
}

static bool is_chorus(const std::string & section) {
    std::string s;
    for (unsigned char c : section) s += (char) tolower(c);
    return s.find("chorus") != std::string::npos || s.find("refrain") != std::string::npos;
}

// Texte pour un événement ASS : majuscules, accolades retirées, retours à la ligne en « \N ».
static std::string ass_text(const std::string & s) {
    std::string t = upper(s), out;
    for (size_t i = 0; i < t.size(); i++) {
        char c = t[i];
        if (c == '{' || c == '}') c = ' ';
        if (c == '\r') continue;
        if (c == '\n') out += "\\N";
        else out += c;
    }
    return out;
}

std::string clip_ass(const std::vector<ClipLine> & lines, int width, int height, const ClipTexts & texts) {
    char head[1536];
    // 16:9 : tailles proportionnelles à la hauteur ; 9:16 et 1:1 : à la largeur (écran de téléphone), paroles relevées en vertical
    // pour rester au-dessus de l'interface de TikTok / Instagram.
    const bool   landscape = width > height, portrait = height > width;
    const double u         = landscape ? height / 1080.0 : width / 1080.0 * 0.9;
    auto         sx        = [&](int v) { return landscape ? v * width / 1920 : (int) (v * 0.8); };
    auto         sy        = [&](int v) { return (int) (v * u); };
    const int    lyric_mv  = portrait ? (int) (height * 0.2) : sy(150);
    const int    card_mv   = portrait ? (int) (height * 0.08) : sy(70);
    // 1.0.98 : colonne « BorderStyle » (1 = contour normal) ; le fond sombre du texte de fin est un dessin à part (voir carte de fin).
    snprintf(head, sizeof(head),
             "[Script Info]\nScriptType: v4.00+\nPlayResX: %d\nPlayResY: %d\n\n[V4+ Styles]\n"
             "Format: Name,Fontname,Fontsize,PrimaryColour,OutlineColour,BackColour,Bold,BorderStyle,Outline,Shadow,Alignment,MarginL,MarginR,MarginV\n"
             "Style: Verse,Impact,%d,&H00FFFFFF,&H00000000,&H80000000,0,1,3,2,2,%d,%d,%d\n"
             "Style: Chorus,Impact,%d,&H00A8D9FF,&H003655FF,&H80000000,0,1,4,3,2,%d,%d,%d\n"
             "Style: Card,Impact,%d,&H00FFFFFF,&H00000000,&H80000000,0,1,3,2,7,%d,%d,%d\n",
             width, height, sy(62), sx(60), sx(60), lyric_mv, sy(84), sx(60), sx(60), lyric_mv, sy(72), sx(80), sx(80), card_mv);
    std::string out = head;
    {  // style du texte de fin : centre (alignement 5) ou bas (alignement 2, à la hauteur des paroles) ; bandeau sombre ou contour
        const bool bottom = texts.end_pos == 1;
        char       end_style[256];
        // 1.0.98 (bandeau unique) : plus de BorderStyle 3 (un rectangle par ligne, chevauchements plus sombres) ;
        // avec fond sombre, contour normal sans ombre, le bandeau pleine largeur est dessiné à part (événement de la couche 0 plus bas).
        snprintf(end_style, sizeof(end_style), "Style: End,Impact,%d,&H00FFFFFF,&H00000000,&H80000000,0,1,4,%d,%d,%d,%d,%d\n",
                 sy(70), texts.end_box ? 0 : 3, bottom ? 2 : 5, sx(100), sx(100), bottom ? lyric_mv : 0);
        out += end_style;
    }
    out += "\n[Events]\nFormat: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text\n";
    // carte de début : titre du morceau, groupe — album, puis le texte de début de l'utilisateur
    if (!texts.title.empty() || !texts.artist.empty() || !texts.album.empty() || !texts.start.empty()) {
        std::string who = ass_text(texts.artist) + (!texts.artist.empty() && !texts.album.empty() ? " — " : "") + ass_text(texts.album);
        char        fs1[64], fs2[64];
        snprintf(fs1, sizeof(fs1), "{\\fs%d\\c&H0066B3FF&}", sy(38));
        snprintf(fs2, sizeof(fs2), "{\\fs%d\\c&H00FFFFFF&}", sy(34));
        std::string card = "{\\fad(400,700)}" + ass_text(texts.title);
        if (!who.empty()) card += "\\N" + std::string(fs1) + who;
        if (!texts.start.empty()) card += "\\N\\N" + std::string(fs2) + ass_text(texts.start);
        out += "Dialogue: 1," + ass_time(0.4) + "," + ass_time(6.5) + ",Card,,0,0,0,," + card + "\n";
    }
    // carte de fin : le texte de fin au centre, pendant les 8 dernières secondes
    if (!texts.end.empty() && texts.duration > 12) {
        if (texts.end_box) {  // 1.0.98 : un seul bandeau sombre sur toute la largeur, couche 0 (sous le texte), même fondu
            const int fs  = sy(70), pad = sy(24);
            int       nl  = 1;
            for (char c : texts.end) nl += c == '\n';
            const int th  = (int) (nl * fs * 1.2);
            const int y0  = texts.end_pos == 1 ? height - lyric_mv - th - pad : height / 2 - th / 2 - pad;
            const int y1  = texts.end_pos == 1 ? height - lyric_mv + pad : height / 2 + th / 2 + pad;
            char      box[256];
            snprintf(box, sizeof(box), "{\\fad(600,600)\\an7\\pos(0,0)\\bord0\\shad0\\1c&H000000&\\1a&H60&\\p1}m 0 %d l %d %d l %d %d l 0 %d{\\p0}", y0, width, y0, width, y1, y1);
            out += "Dialogue: 0," + ass_time(texts.duration - 8) + "," + ass_time(texts.duration - 0.3) + ",End,,0,0,0,," + box + "\n";
        }
        out += "Dialogue: 1," + ass_time(texts.duration - 8) + "," + ass_time(texts.duration - 0.3) + ",End,,0,0,0,,{\\fad(600,600)}" + ass_text(texts.end) + "\n";
    }
    if (texts.karaoke) {  // 1.0.98 : une fenêtre de 5 lignes qui défile ; la ligne chantée est grande et orangée
        for (size_t k = 0; k < lines.size(); k++) {
            double end = k + 1 < lines.size() ? lines[k + 1].start : lines[k].end + 1.0;
            std::string text;
            for (int d = -2; d <= 2; d++) {
                const long i = (long) k + d;
                if (i < 0 || i >= (long) lines.size()) { text += "\\N"; continue; }
                std::string t = upper(lines[i].text);
                for (char & c : t) if (c == '{' || c == '}') c = ' ';
                char tag[96];
                if (d == 0) snprintf(tag, sizeof tag, "{\\fs%d\\c&H0066B3FF&\\alpha&H00&}", sy(74));
                else snprintf(tag, sizeof tag, "{\\fs%d\\c&H00FFFFFF&\\alpha&H%s&}", sy(d == -1 || d == 1 ? 46 : 36), d == -1 || d == 1 ? "70" : "A0");
                text += std::string(tag) + t + (d < 2 ? "\\N" : "");
            }
            out += "Dialogue: 0," + ass_time(lines[k].start) + "," + ass_time(end) + ",Verse,,0,0,0,,{\\fad(120,0)}" + text + "\n";
        }
        return out;
    }
    for (size_t k = 0; k < lines.size(); k++) {
        const ClipLine & l = lines[k];
        std::string text = upper(l.text);
        for (char & c : text)
            if (c == '{' || c == '}') c = ' ';
        // la ligne s'efface avant que la suivante n'apparaisse (sinon les deux s'empilent à l'écran)
        double end = l.end + 0.3;
        if (k + 1 < lines.size()) end = std::min(end, std::max(lines[k + 1].start - 0.05, l.start + 0.5));
        out += "Dialogue: 0," + ass_time(l.start) + "," + ass_time(end) + "," + (is_chorus(l.section) ? "Chorus" : "Verse") +
               ",,0,0,0,,{\\fad(200,250)}" + text + "\n";
    }
    return out;
}

std::string clip_filter_path(const std::string & path) {
    std::string out;
    for (char c : path) {
        if (c == '\\') out += '/';
        else if (c == ':' || c == '\'' || c == ',' || c == '[' || c == ']') { out += '\\'; out += c; }
        else out += c;
    }
    return out;
}

// Plans de caméra sur l'image : centre x, centre y (0 à 1), zoom au début, zoom à la fin.
struct Shot { double cx, cy, z0, z1; };
// 1.0.60 : centre vertical ≤ 0,50 et zoom ≤ 1,35 : le haut de l'image reste visible (les anciens plans bas et serrés coupaient les têtes)
static const Shot SHOTS[] = { { .50, .50, 1.00, 1.12 }, { .62, .42, 1.15, 1.30 }, { .50, .40, 1.20, 1.35 }, { .36, .42, 1.30, 1.15 },
                              { .30, .44, 1.25, 1.12 }, { .40, .38, 1.15, 1.30 }, { .60, .44, 1.30, 1.18 } };

std::string clip_filtergraph(const std::vector<ClipLine> & lines, double duration, int width, int height, const std::string & ass_path,
                             const std::vector<double> & image_start) {
    const int    FPS  = 30;
    const double FADE = 0.6;
    auto chorus_at = [&](double t) {
        for (const ClipLine & l : lines)
            if (t >= l.start && t < l.end + 1) return is_chorus(l.section);
        return false;
    };
    // coupes : 6 s en couplet, 3 s en refrain, et toujours une coupe quand une nouvelle image apparaît
    int n_img = (int) std::max<size_t>(1, image_start.size());
    auto image_at = [&](double t) {
        int k = 0;
        for (int i = 0; i < n_img && i < (int) image_start.size(); i++)
            if (image_start[i] <= t + 0.01) k = i;
        return k;
    };
    std::vector<double> cuts{ 0 };
    std::vector<int>    shot_image;
    while (cuts.back() < duration) {
        double t    = cuts.back();
        double next = t + (chorus_at(t) ? 3.0 : 6.0);
        for (size_t i = 0; i < image_start.size(); i++)
            if (image_start[i] > t + 0.5 && image_start[i] < next) next = image_start[i];
        if (next > duration - 1.0) next = std::max(next, duration);  // pas de plan final minuscule
        shot_image.push_back(image_at(t));
        cuts.push_back(next);
    }
    int count = (int) shot_image.size();

    // grande image au format de sortie (16:9, 9:16 ou 1:1) : les plans de caméra gardent ainsi les bonnes proportions
    int bw = 3840, bh = 2160;
    if (height > width) { bw = 2160; bh = 3840; }
    else if (height == width) { bw = bh = 2160; }
    std::vector<int> uses(n_img, 0);
    for (int k : shot_image) uses[k]++;
    char        buf[1024];
    std::string g;
    for (int k = 0; k < n_img; k++) {
        if (!uses[k]) {  // image sans plan : on la consomme quand même pour que ffmpeg ne se bloque pas
            snprintf(buf, sizeof(buf), "[%d:v]nullsink;", k);
            g += buf;
            continue;
        }
        snprintf(buf, sizeof(buf), "[%d:v]scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d,format=yuv420p,split=%d", k, bw, bh, bw, bh, uses[k]);
        g += buf;
        for (int j = 0; j < uses[k]; j++) g += "[i" + std::to_string(k) + "_" + std::to_string(j) + "]";
        g += ";";
    }
    std::vector<int> seen(n_img, 0);
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
    // 1.0.59 : vignette calculée une seule fois (masque noir transparent au centre) puis posée sur chaque image : mesuré 2,3 fois
    // plus rapide que le filtre « vignette » image par image, pour un rendu identique à l'œil
    snprintf(buf, sizeof(buf),
             "color=black:s=%dx%d:r=%d:d=1,format=rgba,trim=end_frame=1[vn];color=white:s=%dx%d:r=%d:d=1,format=gray,trim=end_frame=1,vignette=PI/4,negate[va];"
             "[vn][va]alphamerge,loop=-1:1:0[vm];",
             width, height, FPS, width, height, FPS);
    g += buf;
    g += "[" + last + "][vm]overlay=shortest=1,format=yuv420p,ass='" + ass_path + "'[v]";
    return g;
}

// ---------------------------------------------------------------------------- Scénario d'après les paroles

std::string clip_story_system() {
    return "You are a music video director. You read song lyrics, understand the story the song tells (a person, a place, "
           "a journey, an emotion that changes), and plan the images of the clip. You answer with ONE JSON object only.";
}

const std::vector<ClipStyle> & clip_styles() {
    static const std::vector<ClipStyle> list = {
        { "auto", N_("Auto (selon la musique et les paroles)"), "" },
        { "realiste", N_("Réaliste (photo de cinéma)"), "photorealistic cinematic film still, natural lighting, 35mm photography, high detail" },
        { "bd", N_("BD franco-belge (ligne claire)"), "franco-belgian comic book illustration, ligne claire, clean ink outlines, flat colors" },
        { "comics", N_("Comics américain"), "american comic book art, bold ink, halftone dots, dynamic composition, vivid colors" },
        { "manga", N_("Manga"), "japanese manga illustration, expressive linework, screentone shading, dramatic framing" },
        { "anime", N_("Animation japonaise"), "anime film frame, soft cel shading, painterly background, warm light" },
        { "aquarelle", N_("Aquarelle"), "watercolor painting, soft washes, visible paper texture, delicate colors" },
        { "huile", N_("Peinture à l'huile"), "oil painting, visible brush strokes, rich colors, chiaroscuro" },
        { "neon", N_("Néon futuriste"), "cyberpunk neon night scene, wet streets, glowing signs, film grain" },
        { "nb", N_("Noir et blanc argentique"), "black and white film photography, high contrast, fine grain" },
        { "3d", N_("Animation 3D"), "3d animated film render, stylized, soft global illumination" },
        { "retro", N_("Affiche rétro des années 70"), "retro 1970s poster illustration, limited color palette, printed grain" },
    };
    return list;
}

std::string clip_story_user(const std::string & title, const std::string & style, const std::string & context, const std::vector<ClipLine> & lines,
                            const std::string & forced_style, int scenes) {
    std::string u = "Song title: " + title + "\nMusical style: " + style.substr(0, 200) + "\n";
    if (!context.empty()) u += "\nBackground (band, album and song sheets; keep the clip coherent with this universe):\n" + context + "\n";
    u += "\n";
    if (lines.empty()) {
        u += "This song is instrumental (no lyrics). Imagine a short visual journey that fits the title and the style.\n\n";
    } else {
        u += "Lyrics, one numbered line per row (the section is in brackets):\n";
        for (size_t i = 0; i < lines.size(); i++) u += std::to_string(i + 1) + " [" + lines[i].section + "] " + lines[i].text + "\n";
        u += "\nFirst understand the story told by the lyrics. Then split the song into consecutive scenes that follow that story "
             "(a chorus that comes back may return to the same place or symbol). The scenes must cover ALL lines, in order, with no gap.\n\n";
    }
    u += "Reply with this JSON and nothing else:\n"
         "{\"story\": \"2 to 4 sentences, what happens in the song\",\n"
         " \"character\": \"one short visual description of the main character or subject, reused in every scene (no name)\",\n"
         " \"style\": \"" + (forced_style.empty() ? std::string("one visual style shared by all scenes: medium, color palette, lighting")
                                                    : "copy exactly: " + forced_style) + "\",\n"
         " \"scenes\": [{\"first\": 1, \"last\": 4, \"prompt\": \"what the image shows\"}, ...]}\n\n"
         "Rules: " + (scenes > 0 ? "exactly " + std::to_string(scenes) + " scenes"  // au-delà de 12 : scénario en plusieurs demandes (clip_plan_user, clip_chunk_user)
                                : std::string("between 6 and 12 scenes (about one scene per 4 to 5 lyric lines; fewer for a short song)"));
    u += lines.empty() ? " (no line numbers: leave first and last at 0)" : "";
    u += ". Each prompt is ONE concrete image in English: subject and action, place, time of day, light, camera angle, mood. "
         "Show the story literally, with objects and places from the lyrics. No text, no letters, no logos, no real people or "
         "celebrities, no band or artist names, nothing violent or explicit. Cinematic, 16:9.";
    return u;
}

static std::string json_str(yyjson_val * o, const char * key) {
    yyjson_val * v = yyjson_obj_get(o, key);
    return yyjson_is_str(v) ? trim(yyjson_get_str(v)) : std::string();
}

// Scènes consécutives sur les lignes lo à hi (0-based) : on recolle les trous et les chevauchements ; les scènes en trop
// (plus de lignes à couvrir) sont retirées. Faux s'il reste moins de min_scenes scènes.
static bool fix_coverage(std::vector<SceneSpec> & v, int lo, int hi, size_t min_scenes) {
    std::stable_sort(v.begin(), v.end(), [](const SceneSpec & a, const SceneSpec & c) { return a.first < c.first; });
    int next = lo;
    for (size_t k = 0; k < v.size(); k++) {
        v[k].first = next;
        int last   = k + 1 == v.size() ? hi : std::max(v[k].last, next);
        v[k].last  = std::min(last, hi);
        next       = v[k].last + 1;
        if (next > hi && k + 1 < v.size()) {
            v.resize(k + 1);
            v[k].last = hi;
            break;
        }
    }
    return v.size() >= min_scenes;
}

// 1.0.64 : Mistral oublie souvent la dernière accolade (« ...}] ») ou écrit du texte après le JSON : on lit de la première
// accolade jusqu'à sa fermeture et on referme ce qui manque.
static yyjson_doc * read_ai_json(const std::string & answer) {
    size_t b = answer.find('{');
    if (b == std::string::npos) return nullptr;
    std::string json, open;
    bool        in_str = false, esc = false;
    for (size_t i = b; i < answer.size(); i++) {
        char c = answer[i];
        if (in_str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
            else if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        } else if (c == '"') {
            in_str = true;
        } else if (c == '{' || c == '[') {
            open += c == '{' ? '}' : ']';
        } else if ((c == '}' || c == ']') && !open.empty()) {
            open.pop_back();
        }
        json += c;
        if (!in_str && open.empty()) break;
    }
    if (in_str) json += '"';
    json.append(open.rbegin(), open.rend());
    return yyjson_read(json.data(), json.size(), YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS);
}

bool clip_story_parse(const std::string & answer, size_t line_count, Storyboard & out, const std::string & forced_style, int max_scenes) {
    yyjson_doc * doc = read_ai_json(answer);
    if (!doc) return false;
    yyjson_val * root = yyjson_doc_get_root(doc);
    out               = Storyboard{};
    out.story         = json_str(root, "story");
    std::string character = json_str(root, "character"), style = forced_style.empty() ? json_str(root, "style") : forced_style;
    size_t       i, n;
    yyjson_val * s;
    yyjson_arr_foreach(yyjson_obj_get(root, "scenes"), i, n, s) {
        SceneSpec   sc;
        std::string p = json_str(s, "prompt");
        if (p.empty()) continue;
        sc.prompt = (character.empty() ? "" : character + ", ") + p + (style.empty() ? "" : ", " + style) + ", cinematic lighting, no text";
        if (line_count > 0) {
            sc.first = (int) yyjson_get_num(yyjson_obj_get(s, "first")) - 1;
            sc.last  = (int) yyjson_get_num(yyjson_obj_get(s, "last")) - 1;
        }
        out.scenes.push_back(sc);
    }
    yyjson_doc_free(doc);
    const size_t cap = max_scenes > 0 ? (size_t) max_scenes : 12;  // nombre de scènes voulu (12 au plus en automatique)
    if (out.scenes.size() > cap) out.scenes.resize(cap);
    if (out.scenes.size() < std::min<size_t>(3, cap)) return false;
    if (line_count > 0 && !fix_coverage(out.scenes, 0, (int) line_count - 1, std::min<size_t>(3, cap))) return false;
    // Plus d'images que de scènes (jusqu'à 60) : chaque scène devient plusieurs plans, avec d'autres angles de caméra.
    static const char * ANGLES[] = { "wide establishing shot", "close-up on the face", "detail shot of hands and objects", "over-the-shoulder view",
                                     "low angle looking up", "high angle looking down", "silhouette against the light", "seen from behind" };
    const size_t         nb      = out.scenes.size();
    for (size_t k = 0; k < nb; k++) out.scenes[k].base = (int) k;
    if (max_scenes > (int) nb) {
        std::vector<SceneSpec> all;
        for (size_t k = 0; k < nb; k++) {
            const int count = max_scenes / (int) nb + ((int) k < max_scenes % (int) nb ? 1 : 0);
            for (int j = 0; j < count; j++) {
                SceneSpec s = out.scenes[k];
                s.part      = j;
                s.parts     = count;
                if (j > 0) s.prompt += std::string(", ") + ANGLES[(j - 1) % 8];
                all.push_back(s);
            }
        }
        out.scenes = all;
    }
    return true;
}

int clip_recommend_images(size_t line_count, double seconds) {
    if (line_count == 0) return std::clamp((int) (seconds / 20.0 + 0.5), 6, 30);
    const int by_lines = ((int) line_count + 1) / 2;
    const int by_time  = std::max(6, (int) (seconds / 6.0));  // pas plus d'une image toutes les 6 secondes
    return std::clamp(std::min(by_lines, by_time), 6, 60);
}

// ---- Scénario en plusieurs demandes (beaucoup d'images) : l'histoire d'abord, puis les scènes par morceaux de paroles ----

std::string clip_plan_user(const std::string & title, const std::string & style, const std::string & context, const std::vector<ClipLine> & lines,
                           const std::string & forced_style) {
    std::string u = "Song title: " + title + "\nMusical style: " + style.substr(0, 200) + "\n";
    if (!context.empty()) u += "\nBackground (band, album and song sheets; keep the clip coherent with this universe):\n" + context + "\n";
    u += "\n";
    if (lines.empty()) {
        u += "This song is instrumental (no lyrics). Imagine a visual journey that fits the title and the style.\n\n";
    } else {
        u += "Lyrics:\n";
        for (const ClipLine & l : lines) u += "[" + l.section + "] " + l.text + "\n";
        u += "\nUnderstand the story told by the lyrics.\n\n";
    }
    u += "Reply with this JSON and nothing else:\n"
         "{\"story\": \"4 to 8 sentences, what happens in the song from beginning to end\",\n"
         " \"character\": \"one short visual description of the main character or subject, reused in every scene (no name)\",\n"
         " \"style\": \"" + (forced_style.empty() ? std::string("one visual style shared by all scenes: medium, color palette, lighting") : "copy exactly: " + forced_style) + "\"}";
    return u;
}

bool clip_plan_parse(const std::string & answer, std::string & story, std::string & character, std::string & style) {
    yyjson_doc * doc = read_ai_json(answer);
    if (!doc) return false;
    yyjson_val * root = yyjson_doc_get_root(doc);
    story             = json_str(root, "story");
    character         = json_str(root, "character");
    style             = json_str(root, "style");
    yyjson_doc_free(doc);
    return !story.empty();
}

std::string clip_chunk_user(const std::string & story, const std::string & character, const std::string & style, const std::vector<ClipLine> & lines,
                            int a, int b, int m, int part, int parts) {
    std::string u = "A music video follows this story: " + story + "\nMain character: " + character + "\nVisual style: " + style + "\n\n";
    if (lines.empty()) {
        u += "The song is instrumental. You plan part " + std::to_string(part) + " of " + std::to_string(parts) + " of the video (parts follow the story in order).\n";
    } else {
        u += "Here are lyric lines " + std::to_string(a + 1) + " to " + std::to_string(b + 1) + " of the song (part " + std::to_string(part) + " of " +
             std::to_string(parts) + "); the section is in brackets:\n";
        for (int i = a; i <= b && i < (int) lines.size(); i++) u += std::to_string(i + 1) + " [" + lines[i].section + "] " + lines[i].text + "\n";
        u += "\nThe scenes must cover ALL these lines, in order, with no gap.\n";
    }
    u += "\nReply with this JSON and nothing else:\n{\"scenes\": [{\"first\": " + std::to_string(lines.empty() ? 0 : a + 1) + ", \"last\": " +
         std::to_string(lines.empty() ? 0 : std::min(b + 1, a + 2)) + ", \"prompt\": \"what the image shows\"}, ...]}\n\nRules: exactly " + std::to_string(m) +
         " scenes" + (lines.empty() ? " (no line numbers: leave first and last at 0)" : "") +
         ". Each prompt is ONE concrete image in English: subject and action, place, time of day, light, camera angle, mood; show this part of the story "
         "literally, with objects and places from the lyrics. Do not repeat the description of the character or the style. No text, no letters, no logos, "
         "no real people or celebrities, no band or artist names, nothing violent or explicit. Cinematic, 16:9.";
    return u;
}

bool clip_chunk_parse(const std::string & answer, int a, int b, int m, std::vector<SceneSpec> & raw) {
    yyjson_doc * doc = read_ai_json(answer);
    if (!doc) return false;
    size_t       i, n;
    yyjson_val * s;
    yyjson_arr_foreach(yyjson_obj_get(yyjson_doc_get_root(doc), "scenes"), i, n, s) {
        SceneSpec sc;
        sc.prompt = json_str(s, "prompt");
        if (sc.prompt.empty()) continue;
        if (a >= 0) {
            sc.first = (int) yyjson_get_num(yyjson_obj_get(s, "first")) - 1;
            sc.last  = (int) yyjson_get_num(yyjson_obj_get(s, "last")) - 1;
        }
        raw.push_back(sc);
    }
    yyjson_doc_free(doc);
    if ((int) raw.size() > m) raw.resize((size_t) m);
    if (raw.empty()) return false;
    return a < 0 || fix_coverage(raw, a, b, 1);
}

static std::string json_escape(const std::string & s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += (char) c; }
        else if (c == '\n') out += "\\n";
        else if (c < 0x20) out += ' ';
        else out += (char) c;
    }
    return out;
}

std::string clip_story_assemble(const std::string & story, const std::string & character, const std::string & style, const std::vector<SceneSpec> & raw) {
    std::string out = "{\"story\":\"" + json_escape(story) + "\",\"character\":\"" + json_escape(character) + "\",\"style\":\"" + json_escape(style) + "\",\"scenes\":[";
    for (size_t k = 0; k < raw.size(); k++)
        out += std::string(k ? "," : "") + "{\"first\":" + std::to_string(raw[k].first + 1) + ",\"last\":" + std::to_string(raw[k].last + 1) + ",\"prompt\":\"" +
               json_escape(raw[k].prompt) + "\"}";
    return out + "]}";
}

std::vector<double> clip_scene_starts(const Storyboard & sb, const std::vector<ClipLine> & lines, double duration) {
    int nb = 0;
    for (const SceneSpec & s : sb.scenes) nb = std::max(nb, s.base + 1);
    std::vector<double> base_start(nb, 0);  // début de chaque scène d'origine ; ses plans se répartissent jusqu'à la suivante
    for (const SceneSpec & s : sb.scenes)
        if (s.part == 0) base_start[s.base] = s.base == 0 ? 0 : (lines.empty() || s.first < 0 ? duration * (double) s.base / (double) nb : lines[s.first].start);
    std::vector<double> st;
    for (const SceneSpec & s : sb.scenes) {
        double end = s.base + 1 < nb ? base_start[s.base + 1] : duration;
        double t   = base_start[s.base] + (end - base_start[s.base]) * (double) s.part / (double) s.parts;
        if (!st.empty() && t <= st.back() + 1.0) t = st.back() + 1.0;  // scènes toujours croissantes
        st.push_back(t);
    }
    return st;
}
