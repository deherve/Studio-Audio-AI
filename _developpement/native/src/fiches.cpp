#include "fiches.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

static std::string mmss(double s) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d:%02d", (int) s / 60, (int) s % 60);
    return buf;
}

static void write_text(const fs::path & file, const std::string & text) {
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f << text;
}

// Valeur d'une ligne « Clé : valeur » d'une fiche de morceau.
static std::string field(const fs::path & fiche, const std::string & key) {
    std::ifstream f(fiche, std::ios::binary);
    std::string   line, prefix = key + " : ";
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind(prefix, 0) == 0) return line.substr(prefix.size());
    }
    return {};
}

// Les dossiers « NN. Titre » ayant une fiche.txt, dans l'ordre.
static std::vector<fs::path> song_dirs(const fs::path & parent) {
    std::vector<fs::path> out;
    std::error_code       ec;
    for (const auto & e : fs::directory_iterator(parent, ec))
        // la partie 1 d'un morceau long est un intermédiaire : seule la version assemblée figure dans les fiches d'album et de groupe
        if (e.is_directory(ec) && fs::exists(e.path() / L"fiche.txt", ec) && e.path().filename().wstring().find(L"(partie 1)") == std::wstring::npos &&
            field(e.path() / L"fiche.txt", "Intermédiaire") != "oui")
            out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

static void write_album_fiche(const Song & s, const fs::path & album_dir) {
    std::ostringstream o;
    o << "ALBUM : " << s.album << "\nGroupe : " << s.artist << "\n\n";
    double total = 0;
    int    n     = 0;
    std::string styles;
    for (const fs::path & d : song_dirs(album_dir)) {
        fs::path    f   = d / L"fiche.txt";
        std::string dur = field(f, "Durée");
        o << ++n << ". " << field(f, "Titre") << "  (" << dur << ")  " << field(f, "Langue") << "\n";
        size_t p = dur.find(':');
        if (p != std::string::npos) total += atoi(dur.c_str()) * 60 + atoi(dur.c_str() + p + 1);
        if (styles.empty()) styles = field(f, "Style");
    }
    o << "\n" << n << " titre(s), durée totale " << mmss(total) << "\n";
    if (!styles.empty()) o << "Style (premier titre) : " << styles << "\n";
    write_text(album_dir / L"fiche_album.txt", o.str());
}

static void write_group_fiche(const Song & s, const fs::path & group_dir) {
    std::ostringstream o;
    o << "GROUPE : " << s.artist << "\n\n";
    std::error_code ec;
    int             albums = 0, songs = 0;
    std::string     styles;
    auto            count = [&](const fs::path & dir) {
        for (const fs::path & d : song_dirs(dir)) {
            songs++;
            if (styles.empty()) styles = field(d / L"fiche.txt", "Style");
        }
    };
    count(group_dir);  // titres sans album, directement dans le dossier du groupe
    for (const auto & e : fs::directory_iterator(group_dir, ec)) {
        if (!e.is_directory(ec) || !fs::exists(e.path() / L"fiche_album.txt", ec)) continue;
        albums++;
        o << "Album : " << narrow(e.path().filename().wstring()) << "\n";
        count(e.path());
    }
    o << "\n" << albums << " album(s), " << songs << " titre(s)\n";
    if (!styles.empty()) o << "Style : " << styles << "\n";
    write_text(group_dir / L"fiche_groupe.txt", o.str());
}

void write_fiches(const Song & s, const fs::path & dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    // paroles.txt : jamais écrasé (l'utilisateur peut l'avoir corrigé)
    if (!fs::exists(dir / L"paroles.txt", ec) && !s.instrumental && !s.lyrics.empty()) write_text(dir / L"paroles.txt", s.lyrics);

    std::ostringstream o;
    o << "Titre : " << s.title << "\nGroupe : " << s.artist << "\nAlbum : " << s.album << "\nPiste : " << s.track
      << "\nDurée : " << mmss(s.audio_seconds) << "\nLangue : " << (s.instrumental ? "instrumental" : s.lyrics_lang.empty() ? "inconnue" : s.lyrics_lang)
      << "\nStyle : " << s.style << "\n";
    if (!s.long_rest.empty()) o << "Intermédiaire : oui\n";  // partie 1 d'un morceau long
    write_text(dir / L"fiche.txt", o.str());

    fs::path parent = dir.parent_path();
    if (!s.album.empty()) {
        write_album_fiche(s, parent);
        write_group_fiche(s, parent.parent_path());
    } else {
        write_group_fiche(s, parent);
    }
}

Song with_full_lyrics(const Song & s, const std::vector<Song> & all) {
    if (s.kind != "extend" || s.long_join <= 0 || s.parent.empty() || s.instrumental) return s;
    Song         full = s;
    const Song * cur  = &s;
    while (cur && cur->kind == "extend" && cur->long_join > 0) {  // 1.0.70 : jusqu'à 3 parties
        const Song * up = nullptr;
        for (const Song & p : all)
            if (p.id == cur->parent && !p.lyrics.empty()) up = &p;
        if (up) full.lyrics = up->lyrics + "\n\n" + full.lyrics;
        cur = up;
    }
    return full;
}

void drop_stale_lyrics_file(const Song & original, const Song & full, const fs::path & dir) {
    if (full.lyrics == original.lyrics) return;
    fs::path      file = dir / L"paroles.txt";
    std::ifstream f(file, std::ios::binary);
    if (!f) return;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    auto trimmed = [](const std::string & s) {
        size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
        return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    };
    if (trimmed(text) == trimmed(original.lyrics)) {
        std::error_code ec;
        fs::remove(file, ec);
    }
}
