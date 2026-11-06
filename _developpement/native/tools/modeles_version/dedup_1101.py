# 1.1.001 : retire les copies de trim / lower / read_text / write_text / url_encode et inclut util.hpp.
# Usage : python dedup_1101.py <native\src>. Garde les fins de ligne (CRLF ou LF) de chaque fichier.
# Chaque bloc doit être trouvé exactement une fois, sinon le script s'arrête sans rien écrire.
import re, sys
from pathlib import Path

src = Path(sys.argv[1])

TRIM_A = """static std::string trim(const std::string & s) {
    size_t a = s.find_first_not_of(" \\t\\r\\n"), b = s.find_last_not_of(" \\t\\r\\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
"""
TRIM_B = """static std::string trim(const std::string & s) {
    size_t b = s.find_first_not_of(" \\t\\r\\n"), e = s.find_last_not_of(" \\t\\r\\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}
"""
EDITS = {
    "main.cpp": [
        ("""static std::string read_text(const fs::path & path) {
    std::FILE * file = _wfopen(path.c_str(), L"rb");
    std::string text;
    if (file) {
        char buf[4096];
        for (size_t n; (n = fread(buf, 1, sizeof(buf), file)) > 0;) text.append(buf, n);
        fclose(file);
    }
    return text;
}

static void write_text(const fs::path & path, const std::string & text) {
    std::FILE * file = _wfopen(path.c_str(), L"wb");
    if (file) {
        fwrite(text.data(), 1, text.size(), file);
        fclose(file);
    }
}

""", ""),
        ("static std::string trim(const std::string & s);  // définie plus bas (aides UI)\n", "", 2),
        (TRIM_A + "\n" + """static std::string lower(std::string s) {
    for (auto & c : s) c = (char) tolower((unsigned char) c);
    return s;
}

""", ""),
    ],
    "analysis.cpp": [
        ("""std::string read_text(const fs::path & p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

""", ""),
        ("""std::string lower(const std::string & s) {
    std::string o = s;
    for (char & c : o) c = (char) tolower((unsigned char) c);
    return o;
}

""", ""),
    ],
    "ai_writer.cpp": [
        (TRIM_A + "\n" + """static std::string lower_ascii(std::string s) {
    for (char & c : s) c = (char) tolower((unsigned char) c);
    return s;
}

""", ""),
        ("lower_ascii(", "lower(", None),
    ],
    "clip.cpp": [(TRIM_B + "\n", "")],
    "score.cpp": [(TRIM_A + "\n", "")],
    "suno.cpp": [(TRIM_A + "\n", "")],
    "lyrics_web.cpp": [
        (TRIM_B.replace("static ", "") + "\n", ""),
        ("""std::string url_encode(const std::string & s) {
    std::string out;
    char        buf[4];
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char) c;
        else {
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

""", ""),
    ],
    "translate.cpp": [
        ("""static std::string url_encode(const std::string & s) {
    static const char * hex = "0123456789ABCDEF";
    std::string         out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char) c;
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

""", ""),
    ],
    "fiches.cpp": [
        ("""static void write_text(const fs::path & file, const std::string & text) {
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f << text;
}

""", ""),
    ],
    "i18n.cpp": [
        ("""std::string read_file(const fs::path & p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

""", ""),
        ("read_file(", "read_text(", None),
    ],
    "studio.cpp": [
        ("""static std::string read_file(const fs::path & path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

""", ""),
        ("read_file(", "read_text(", None),
    ],
    "media.cpp": [
        ("""static std::string read_text(const fs::path & file, size_t max = 100000) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return {};
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s.size() > max ? s.substr(0, max) : s;
}""", """static std::string read_head(const fs::path & file, size_t max = 100000) {  // début d'un fichier (fiches lues pour le clip)
    return read_text(file).substr(0, max);
}"""),
        ("read_text(answer_file)", "read_head(answer_file)"),
        ("read_text(parent", "read_head(parent", 3),
        ("read_text(song_folder(s.id) / L\"fiche.txt\", 900)", "read_head(song_folder(s.id) / L\"fiche.txt\", 900)"),
    ],
}

out = {}
for name, edits in EDITS.items():
    p = src / name
    raw = p.read_bytes().decode("utf-8")
    crlf = "\r\n" in raw
    text = raw.replace("\r\n", "\n")
    for e in edits:
        old, new = e[0], e[1]
        want = e[2] if len(e) > 2 else 1
        n = text.count(old)
        if want is not None and n != want:
            sys.exit(f"{name} : bloc trouvé {n} fois au lieu de {want} : {old[:60]!r}")
        if want is None and n == 0:
            sys.exit(f"{name} : rien à remplacer pour {old!r}")
        text = text.replace(old, new)
    # include "util.hpp" après le premier #include "…hpp" du fichier
    if '#include "util.hpp"' not in text:
        m = re.search(r'^#include "[^"]+\.hpp"[^\n]*\n', text, re.M)
        text = text[: m.end()] + '#include "util.hpp"  // 1.1.001 : trim, lower, read_text, write_text, url_encode\n' + text[m.end():]
    out[p] = text.replace("\n", "\r\n") if crlf else text
for p, t in out.items():
    p.write_bytes(t.encode("utf-8"))
    print("modifié :", p.name)
