#include <map>
#include <set>
#include "media.hpp"
#include "util.hpp"  // 1.1.001 : trim, lower, read_text, write_text, url_encode

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "clip.hpp"
#include "fiches.hpp"
#include "score.hpp"
#include "suno.hpp"
#include "i18n.hpp"

void Job::stop() {
    cancel = true;
    if (void * p = proc.load()) TerminateProcess((HANDLE) p, 1);
}

Job::~Job() {
    stop();
    if (thread.joinable()) thread.join();
}

std::string album_key(const std::string & artist, const std::string & album) {
    return fold(artist) + "|" + fold(album);
}

int track_number(const Song & s) {
    auto leading = [](const std::string & t) {
        size_t n = 0;
        while (n < t.size() && isdigit((unsigned char) t[n])) n++;
        return n > 0 && n <= 3 ? atoi(t.substr(0, n).c_str()) : 0;
    };
    int n = leading(s.track);
    return n > 0 ? n : leading(s.title);
}

// Retire les suffixes ajoutés par l'application aux re-rendus et recompositions.
static std::string strip_suffixes(std::string title) {
    for (bool again = true; again;) {
        again = false;
        for (const char * suffix : { " (re-rendu)", " (recomposé)" }) {
            size_t n = strlen(suffix);
            if (title.size() > n && title.compare(title.size() - n, n, suffix) == 0) {
                title.resize(title.size() - n);
                again = true;
            }
        }
    }
    return title;
}

std::string display_title(const std::string & title) {
    return strip_suffixes(title);
}

std::string clean_title(const std::string & title) {
    std::string t = strip_suffixes(title);
    size_t      n = 0;
    while (n < t.size() && isdigit((unsigned char) t[n])) n++;
    if (n == 0 || n > 3 || n >= t.size() || !strchr(".-) ", t[n])) return t;
    while (n < t.size() && strchr(".-) ", t[n])) n++;
    return n < t.size() ? t.substr(n) : t;
}

std::vector<Song> album_songs(const std::vector<Song> & all, const std::string & key, bool validated_only) {
    std::vector<Song> out;
    for (const Song & s : all) {
        if (s.status == "done" && !s.album.empty() && album_key(s.artist, s.album) == key && (!validated_only || s.validated)) {
            out.push_back(s);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Song & a, const Song & b) {
        int x = track_number(a), y = track_number(b);
        if (x != y) return (x ? x : 100000) < (y ? y : 100000);
        return a.created < b.created;
    });
    return out;
}

static void finish_job(Job & job, bool ok, const std::string & message, const fs::path & output) {
    std::lock_guard<std::mutex> lock(job.mutex);
    job.message = message;
    job.output  = output;
    job.state   = ok ? 1 : 2;
}

// ffmpeg avec suivi du processus (pour l'annulation). Renvoie le code de sortie.
static int ffmpeg(Job & job, const std::wstring & args, const std::function<void(const std::string &)> & on_line = nullptr,
                  bool read_stdout = false) {
    return run_command(ffmpeg_exe() + L" -v error -y " + args, on_line, [&job](void * h) { job.proc = h; }, read_stdout);
}

static std::string two(int n) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d", n);
    return buf;
}

void start_export(const std::shared_ptr<Job> & job, const Paths & paths, std::vector<Song> songs, std::string artist,
                  std::string album, fs::path cover, fs::path dest_parent, bool flac, bool mp3, bool wav) {
    job->thread = std::thread([=]() mutable {
        Job &    j    = *job;
        fs::path dest = dest_parent;  // 1.0.46 : <dossier de l'album>\Audios (choisi par l'appelant)
        std::error_code ec;
        fs::create_directories(dest, ec);
        if (ec) {
            finish_job(j, false, trf("Impossible de créer le dossier : %s", narrow(dest.wstring()).c_str()), dest);
            return;
        }
        // Pochette normalisée en JPEG (1400 px max) : fichier du dossier et image intégrée aux étiquettes.
        fs::path cover_jpg;
        if (!cover.empty() && fs::exists(cover)) {
            cover_jpg = dest / "cover.jpg";
            if (ffmpeg(j, L"-i " + quote_arg(cover.wstring()) + L" -vf \"scale='min(1400,iw)':-2\" -q:v 2 -frames:v 1 " +
                              quote_arg(cover_jpg.wstring())) != 0 || !fs::exists(cover_jpg)) {
                cover_jpg.clear();
            }
        }
        int formats = (int) flac + (int) mp3 + (int) wav;
        int total   = std::max(1, (int) songs.size() * formats), done = 0, count = (int) songs.size();
        for (const Song & s : songs) count = std::max(count, track_number(s));  // « 12/12 » et non « 12/11 »
        int         written = 0;
        std::string errors;
        for (size_t i = 0; i < songs.size() && !j.cancel; i++) {
            const Song & s       = songs[i];
            int          num     = track_number(s) > 0 ? track_number(s) : (int) i + 1;
            std::string  name    = two(num) + " - " + safe_filename(clean_title(s.title));
            fs::path     src_dir = song_folder(s.id);
            struct Fmt { bool on; const wchar_t * file; const char * ext; };
            for (const Fmt & f : { Fmt{ flac, L"audio.flac", ".flac" }, Fmt{ mp3, L"audio.mp3", ".mp3" },
                                   Fmt{ wav, L"audio.wav", ".wav" } }) {
                if (!f.on || j.cancel) continue;
                fs::path src = src_dir / f.file, dst = dest / widen(name + f.ext);
                bool     ok  = false;
                if (!fs::exists(src)) {
                    errors += trf("fichier manquant : %s", narrow(src.wstring()).c_str()) + "\n";
                } else if (std::string(f.ext) == ".wav") {
                    ok = CopyFileW(src.c_str(), dst.c_str(), FALSE) != 0;
                } else {
                    std::wstring tags = L" -metadata " + quote_arg(L"title=" + widen(clean_title(s.title))) + L" -metadata " +
                                        quote_arg(L"artist=" + widen(s.artist.empty() ? "YuE2" : s.artist)) + L" -metadata " +
                                        quote_arg(L"album=" + widen(album)) + L" -metadata " +
                                        quote_arg(L"album_artist=" + widen(artist)) + L" -metadata " +
                                        quote_arg(L"track=" + std::to_wstring(num) + L"/" + std::to_wstring(count));
                    std::wstring args = L"-i " + quote_arg(src.wstring());
                    if (!cover_jpg.empty()) {
                        args += L" -i " + quote_arg(cover_jpg.wstring()) + L" -map 0:a -map 1:v -c copy -disposition:v:0 attached_pic"
                                L" -metadata:s:v title=\"Album cover\" -metadata:s:v comment=\"Cover (front)\"";
                    } else {
                        args += L" -map 0:a -c copy";
                    }
                    if (std::string(f.ext) == ".mp3") args += L" -id3v2_version 3";
                    ok = ffmpeg(j, args + tags + L" " + quote_arg(dst.wstring())) == 0 && fs::exists(dst);
                }
                if (ok) written++;
                else errors += trf("échec : %s", narrow(dst.filename().wstring()).c_str()) + "\n";
                j.progress = (float) ++done / total;
            }
        }
        if (j.cancel) {
            finish_job(j, false, tr("Export annulé."), dest);
        } else if (written == 0) {
            finish_job(j, false, errors.empty() ? std::string(tr("Aucun fichier exporté.")) : errors, dest);
        } else {
            std::string msg = cover_jpg.empty() ? trf("%d fichier(s) exporté(s) (sans pochette).", written) : trf("%d fichier(s) exporté(s) avec pochette.", written);
            if (!errors.empty()) msg += "\n" + errors;
            finish_job(j, true, msg, dest);
        }
    });
}

// Contrôle de la vidéo terminée : lecture sans erreur du début et de la fin, et vraie image (pas un écran noir).
static bool video_is_sound(Job & j, const fs::path & file, std::string & why) {
    std::string errs;
    auto        collect = [&](const std::string & l) { errs += l + "\n"; };
    run_command(ffmpeg_exe() + L" -v error -i " + quote_arg(file.wstring()) + L" -t 15 -f null -", collect, nullptr);
    run_command(ffmpeg_exe() + L" -v error -sseof -10 -i " + quote_arg(file.wstring()) + L" -f null -", collect, nullptr);
    if (!errs.empty()) {
        why = trf("erreurs de lecture : %s", errs.substr(0, 200).c_str());
        return false;
    }
    // Image réellement présente : une trame du milieu ne doit pas être noire.
    std::string info;
    run_command(ffmpeg_exe() + L" -v info -ss 3 -i " + quote_arg(file.wstring()) +
                    L" -frames:v 1 -vf signalstats,metadata=print:key=lavfi.signalstats.YAVG -f null -",
                [&](const std::string & l) { info += l + "\n"; }, nullptr);
    size_t pos = info.find("YAVG=");
    if (pos == std::string::npos) {
        why = tr("aucune image décodée");
        return false;
    }
    if (atof(info.c_str() + pos + 5) < 1.0) {
        why = tr("image noire");
        return false;
    }
    (void) j;
    return true;
}

// Une vidéo par titre : image fixe + audio du morceau (qualité maximale), écrite dans un fichier temporaire
// puis renommée une fois contrôlée — jamais de fichier à moitié écrit sous le nom final.
void start_videos(const std::shared_ptr<Job> & job, const Paths & paths, std::vector<VideoTask> tasks) {
    job->thread = std::thread([=]() mutable {
        Job &       j = *job;
        int         made = 0;
        std::string errors;
        fs::path    folder = tasks.empty() ? fs::path() : tasks[0].output.parent_path();
        for (size_t i = 0; i < tasks.size() && !j.cancel; i++) {
            const VideoTask & t = tasks[i];
            j.total = (int) tasks.size();
            j.item  = (int) i + 1;
            fs::path          audio = song_folder(t.song.id) / "audio.flac";
            if (!fs::exists(audio)) audio = song_folder(t.song.id) / "audio.wav";
            double seconds = fs::exists(audio) ? media_duration(audio) : 0;
            if (seconds <= 0) {
                errors += trf("audio introuvable : %s", t.song.title.c_str()) + "\n";
                continue;
            }
            std::error_code ec;
            fs::create_directories(t.output.parent_path(), ec);
            fs::path     tmp  = t.output;
            tmp += L".part";
            std::wstring size = std::to_wstring(t.width) + L":" + std::to_wstring(t.height);
            std::wstring args = L"-progress pipe:1 -nostats -framerate 15 -loop 1 -i " + quote_arg(t.image.wstring()) + L" -i " +
                                quote_arg(audio.wstring()) + L" -vf \"scale=" + size + L":force_original_aspect_ratio=decrease,pad=" +
                                size + L":(ow-iw)/2:(oh-ih)/2:color=black,format=yuv420p\" -c:v libx264 -tune stillimage "
                                L"-preset veryfast -crf 20 -r 15 -c:a aac -b:a 320k -shortest -movflags +faststart -f mp4 " + quote_arg(tmp.wstring());
            int code = ffmpeg(j, args, [&](const std::string & line) {
                if (line.rfind("out_time_us=", 0) == 0) {
                    double us = atof(line.c_str() + 12);
                    if (us > 0) j.progress = (float) std::min(0.999, (i + std::min(0.98, us / 1e6 / seconds)) / tasks.size());
                }
            }, true);
            if (j.cancel) {
                fs::remove(tmp, ec);
                break;
            }
            std::string why;
            if (code != 0 || !fs::exists(tmp)) {
                errors += trf("échec ffmpeg (%d) : %s", (int) code, narrow(t.output.filename().wstring()).c_str()) + "\n";
            } else if (!video_is_sound(j, tmp, why)) {
                errors += trf("vidéo invalide (%s) : %s", why.c_str(), narrow(t.output.filename().wstring()).c_str()) + "\n";
            } else if (MoveFileExW(tmp.c_str(), t.output.c_str(), MOVEFILE_REPLACE_EXISTING) == 0) {
                errors += trf("renommage impossible : %s", narrow(t.output.filename().wstring()).c_str()) + "\n";
            } else {
                made++;
            }
            fs::remove(tmp, ec);
            j.progress = (float) (i + 1) / tasks.size();
        }
        if (j.cancel) {
            finish_job(j, false, trf("Création des vidéos annulée (%d terminée(s)).", (int) made), folder);
        } else if (made == 0) {
            finish_job(j, false, errors.empty() ? std::string(tr("Aucune vidéo créée.")) : errors, folder);
        } else {
            std::string msg = trf("%d vidéo(s) créée(s) et contrôlée(s).", (int) made);
            if (!errors.empty()) msg += "\n" + errors;
            finish_job(j, errors.empty(), msg, folder);
        }
    });
}

// 1.0.53 — Scénario d'après les paroles. Le texte envoyé à l'IA locale réunit les paroles et les fiches (morceau, album,
// groupe) ; la réponse (histoire + scènes + descriptions d'images) est gardée dans <morceau>\clip_scenes pour pouvoir
// être relue, corrigée et réutilisée par les trois formats. Les images viennent ensuite du modèle d'images local, l'une
// après l'autre (la carte graphique est libérée entre l'IA de texte et celle d'images).
static std::string read_head(const fs::path & file, size_t max = 100000) {  // début d'un fichier (fiches lues pour le clip)
    return read_text(file).substr(0, max);
}

// un dossier par type de rendu et par moteur d'images (FLUX klein, le moteur d'origine, garde le nom simple)
fs::path clip_scenes_dir(const SceneEngine & eng, const Song & s) {
    const bool other_engine = eng.image_model && std::string(eng.image_model->id) != "klein";
    return song_folder(s.id) / L"clip_scenes" / widen(eng.style_id + (other_engine ? std::string("_") + eng.image_model->id : std::string()));
}

bool make_clip_scenes(const SceneEngine & eng, Job & j, const Song & s, const std::vector<ClipLine> & lines, double seconds, ClipScenes & out,
                      std::string & note, bool scenario_only) {
    std::error_code ec;
    fs::path        dir = clip_scenes_dir(eng, s);
    fs::create_directories(dir, ec);
    // nombre d'images de ce morceau : celui préparé pour lui (recommandation, plus, moins) sinon le réglage général (0 = auto)
    const auto      per_song = eng.counts.find(s.id);
    const int       want     = per_song != eng.counts.end() ? per_song->second : eng.scene_count;
    auto say = [&](const std::string & m) {
        std::lock_guard<std::mutex> lock(j.mutex);
        j.message = m;
    };

    // 1. scénario (gardé : même scénario pour YouTube, TikTok et Instagram, et à la reprise)
    fs::path    answer_file = dir / L"scenario_ia.txt";
    std::string answer      = read_head(answer_file);
    // Le scénario gardé n'est valable que pour les mêmes paroles (nombre de lignes et texte) : sinon on le refait, et les
    // images de l'ancien scénario sont retirées (morceau long : les paroles ont pu passer de la partie 2 aux deux parties).
    std::string sig;
    {
        size_t h = 1469598103934665603ull;
        for (const ClipLine & l : lines)
            for (unsigned char c : l.text) h = (h ^ c) * 1099511628211ull;
        sig = std::to_string(lines.size()) + ":" + std::to_string(h) + (want > 0 ? ":n" + std::to_string(want) : std::string());
    }
    if (!answer.empty()) {
        size_t nl = answer.find('\n');
        if (nl != std::string::npos && answer.compare(0, nl, sig) == 0) {
            answer.erase(0, nl + 1);
        } else {
            answer.clear();
            for (const auto & e : fs::directory_iterator(dir, ec))
                if (e.path().filename().wstring().rfind(L"scene_", 0) == 0) fs::remove(e.path(), ec);
        }
    }
    Storyboard  sb;
    if (answer.empty() || !clip_story_parse(answer, lines.size(), sb, eng.style_prompt, want)) {
        if (!eng.llm.ready() || !fs::exists(eng.llm_model, ec)) {
            note = tr("IA locale de texte non prête (Réglages › IA) : clip fait avec la pochette.");
            return false;
        }
        eng.images->server.reset();  // l'IA de texte a besoin de la mémoire vidéo : le modèle d'images sera rechargé après
        say(trf("Scénario : « %s »…", s.title.c_str()));
        fs::path parent = song_folder(s.id).parent_path();
        std::string context = read_head(parent / L"fiche_groupe.txt", 700) + "\n" + read_head(parent / L"fiche_album.txt", 700) + "\n" +
                              read_head(parent.parent_path() / L"fiche_groupe.txt", 700) + "\n" + read_head(song_folder(s.id) / L"fiche.txt", 900);
        std::string user = clip_story_user(display_title(s.title), s.style, context, lines, eng.style_prompt, want);
        std::atomic<int> stage{ 0 };
        if (want <= 12) {  // 12 scènes ou moins : une seule demande
            for (int attempt = 0; attempt < 2 && !j.cancel; attempt++) {
                AiCallResult r = ai_local_chat(eng.llm, eng.llm_model, eng.llm_layers, clip_story_system(),
                                               attempt ? user + "\n\nYour previous answer could not be read. Answer again with ONE valid JSON object only." : user,
                                               &stage, &j.cancel);
                if (r.ok && clip_story_parse(r.text, lines.size(), sb, eng.style_prompt, want)) {
                    answer = r.text;
                    break;
                }
            }
        } else {
            // Beaucoup d'images : autant de vraies scènes que d'images (au plus une par ligne de paroles ; au-delà, les plans d'une
            // scène sont déclinés par clip_story_parse). L'histoire d'abord, puis les scènes par morceaux de ~10, dans une même session
            // du modèle (chargé une seule fois).
            const int total = lines.empty() ? want : std::min<int>(want, (int) lines.size());
            const int parts = (total + 9) / 10;
            std::string story, character, style;
            std::vector<SceneSpec> raw;
            bool failed = false;
            AiCallResult started = ai_local_session(eng.llm, eng.llm_model, eng.llm_layers, &stage, &j.cancel, [&](const AiAsk & ask) {
                auto ask_json = [&](const std::string & u, const std::function<bool(const std::string &)> & ok) {
                    for (int attempt = 0; attempt < 2 && !j.cancel; attempt++) {
                        AiCallResult r = ask(clip_story_system(), attempt ? u + "\n\nYour previous answer could not be read. Answer again with ONE valid JSON object only." : u);
                        if (r.ok && ok(r.text)) return true;
                    }
                    return false;
                };
                std::string plan_user = clip_plan_user(display_title(s.title), s.style, context, lines, eng.style_prompt);
                if (!ask_json(plan_user, [&](const std::string & t) { return clip_plan_parse(t, story, character, style); })) { failed = true; return; }
                if (!eng.style_prompt.empty()) style = eng.style_prompt;
                for (int p = 0; p < parts && !j.cancel; p++) {
                    say(trf("Scénario : « %s » (partie %d / %d)…", s.title.c_str(), p + 1, parts));
                    // lignes (ou parts du morceau instrumental) et nombre de scènes de cette demande, répartis également
                    const int m = total / parts + (p < total % parts ? 1 : 0);
                    int a = -1, b = -1;
                    if (!lines.empty()) {
                        const int n = (int) lines.size();
                        a = (int) ((long long) n * p / parts);
                        b = (int) ((long long) n * (p + 1) / parts) - 1;
                    }
                    std::vector<SceneSpec> got;
                    // 1.0.64 : une partie illisible n'annule pas le scénario : les autres scènes se partagent ses images
                    if (ask_json(clip_chunk_user(story, character, style, lines, a, b, m, p + 1, parts),
                                 [&](const std::string & t) { got.clear(); return clip_chunk_parse(t, a, b, m, got); }))
                        raw.insert(raw.end(), got.begin(), got.end());
                }
            });
            if (!started.ok || failed) {
                if (j.cancel) return false;
                note = tr("Scénario illisible (IA locale) : clip fait avec la pochette.");
                return false;
            }
            std::string assembled = clip_story_assemble(story, character, style, raw);
            if (clip_story_parse(assembled, lines.size(), sb, eng.style_prompt, want)) answer = assembled;
        }
        if (j.cancel) return false;
        if (sb.scenes.empty()) {
            note = tr("Scénario illisible (IA locale) : clip fait avec la pochette.");
            return false;
        }
        std::ofstream(answer_file, std::ios::binary) << sig << "\n" << answer;
    }

    // fiche lisible du scénario
    {
        std::ostringstream o;
        o << "SCÉNARIO : " << s.title << "\n\n" << sb.story << "\n\n";
        for (size_t k = 0; k < sb.scenes.size(); k++) {
            o << "Scène " << k + 1;
            if (sb.scenes[k].first >= 0) o << " (lignes " << sb.scenes[k].first + 1 << " à " << sb.scenes[k].last + 1 << ")";
            o << " : " << sb.scenes[k].prompt << "\n";
        }
        std::ofstream(dir / L"scenario.txt", std::ios::binary) << o.str();
    }

    if (scenario_only) return true;

    // 2. images : une par scène, 16:9 (les formats verticaux et carrés en gardent le centre)
    if (!eng.image_model || eng.sd_cli.empty() || !image_model_ready(eng.image_root, *eng.image_model)) {
        note = tr("Modèle d'images local non prêt (Réglages › IA › Pochettes) : clip fait avec la pochette.");
        return false;
    }
    out = ClipScenes{};
    // 1.0.98 (atelier clip, point 8) : même graine pour toutes les images du morceau. Les scènes gardent leur propre
    // description ; seul le tirage du bruit de départ est commun, ce qui rapproche visage, couleurs et cadrage d'une image
    // à l'autre (sans garantie : un modèle d'images ne « reconnaît » pas un personnage). Graine tirée de l'id du morceau
    // (FNV-1a) : la même à chaque reprise, différente d'un morceau à l'autre.
    long long seed = -1;
    if (eng.same_seed) {
        unsigned long long h = 1469598103934665603ull;
        for (unsigned char c : s.id) h = (h ^ c) * 1099511628211ull;
        seed = (long long) (h & 0x7fffffffull);
    }
    for (size_t k = 0; k < sb.scenes.size() && !j.cancel; k++) {
        char name[32];
        snprintf(name, sizeof(name), "scene_%02d.png", (int) k + 1);
        fs::path png = dir / widen(name);
        if (!fs::exists(png, ec) || fs::file_size(png, ec) < 10000) {
            say(trf("Images de « %s » : %d sur %d…", s.title.c_str(), (int) k + 1, (int) sb.scenes.size()));
            // 1.0.61 : modèle gardé chargé ; sans serveur, ou s'il échoue, une image à la fois comme avant
            ImageServerSlot & slot = *eng.images;
            if (!slot.server && !slot.failed) {
                auto server = std::make_shared<LocalImageServer>(eng.sd_cli, eng.image_root, *eng.image_model, eng.offload);
                if (server->start(&j.cancel)) slot.server = server;
                else slot.failed = true;
            }
            AiCallResult r;
            if (slot.server) r = slot.server->generate(sb.scenes[k].prompt, 1344, 768, png, &j.cancel, seed);
            if (!r.ok && !j.cancel) {
                slot.server.reset();  // libère la mémoire vidéo pour sd-cli
                slot.failed = true;
                r = ai_image_local(eng.sd_cli, eng.image_root, *eng.image_model, sb.scenes[k].prompt, 1344, 768, png, &j.cancel, eng.offload, seed);
            }
            if (!r.ok || !fs::exists(png, ec)) {
                if (j.cancel) return false;
                note = std::string(tr("Image de scène impossible : ")) + trf(tr(r.error.c_str()), r.error_arg.c_str()) + tr(" Clip fait avec la pochette.");
                return false;
            }
        }
        out.images.push_back(png);
    }
    if (j.cancel) return false;
    out.starts = clip_scene_starts(sb, lines, seconds);
    say({});
    return true;
}

// « 1:23 » (Short : début et fin de l'extrait dans le compte rendu)
static std::string fmt_seconds(double s) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", (int) s / 60, (int) s % 60);
    return buf;
}

// 1.0.52 — Atelier clip. Pour chaque titre : paroles calées sur l'audio (Whisper, mot par mot, rapprochées du texte connu),
// sous-titres ASS, puis montage ffmpeg (plans de caméra lents sur l'image, fondus, vignette). Même contrôle final et même
// écriture « .part » puis renommage que start_videos. Un titre instrumental n'a pas de paroles : seulement les plans.

// 1.0.98 : paroles synchronisées d'un fichier .lrc (« [mm:ss.xx]texte ») ; la fin de chaque ligne est le début de la suivante.
static std::vector<ClipLine> read_lrc(const fs::path & file, double seconds) {
    std::vector<ClipLine> out;
    std::ifstream f(file, std::ios::binary);
    std::string   line;
    while (std::getline(f, line)) {
        if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        int m = 0;
        double sec = 0;
        if (line.size() > 8 && line[0] == '[' && sscanf(line.c_str(), "[%d:%lf]", &m, &sec) == 2) {
            size_t close = line.find(']');
            std::string text = close == std::string::npos ? std::string() : line.substr(close + 1);
            while (!text.empty() && (text.back() == '\r' || text.back() == ' ')) text.pop_back();
            if (text.empty()) continue;
            ClipLine l;
            l.text = text;
            l.start = m * 60 + sec;
            out.push_back(l);
        }
    }
    for (size_t i = 0; i < out.size(); i++) out[i].end = i + 1 < out.size() ? out[i + 1].start : std::min(seconds, out[i].start + 4.0);
    return out;
}
static std::string stamp(double t, bool srt);
static void write_lyric_files(const Song & s, const std::vector<ClipLine> & lines) {  // paroles.lrc et paroles.srt
    const fs::path dir = song_folder(s.id);
    std::ostringstream lrc, srt;
    lrc << "[ti:" << display_title(s.title) << "]\n[ar:" << s.artist << "]\n[al:" << s.album << "]\n";
    int n = 0;
    for (size_t k = 0; k < lines.size(); k++) {
        const double end = lines[k].end > lines[k].start ? lines[k].end : (k + 1 < lines.size() ? lines[k + 1].start : lines[k].start + 3);
        lrc << "[" << stamp(lines[k].start, false) << "]" << lines[k].text << "\n";
        srt << ++n << "\n" << stamp(lines[k].start, true) << " --> " << stamp(end, true) << "\n" << lines[k].text << "\n\n";
    }
    std::ofstream(dir / L"paroles.lrc", std::ios::binary | std::ios::trunc) << "\xEF\xBB\xBF" << lrc.str();
    std::ofstream(dir / L"paroles.srt", std::ios::binary | std::ios::trunc) << "\xEF\xBB\xBF" << srt.str();
}

void start_clips(const std::shared_ptr<Job> & job, const Paths & paths, std::vector<ClipTask> tasks) {
    (void) paths;
    job->thread = std::thread([=]() mutable {
        Job &       j = *job;
        int         made = 0;
        std::string errors, notes;
        fs::path    folder = tasks.empty() ? fs::path() : tasks[0].output.parent_path();
        std::map<std::string, std::vector<ClipLine>> aligned;  // 1.0.58 : paroles calées une fois par morceau, pas une fois par format
        auto song_lines = [&](const ClipTask & t, const fs::path & audio, double seconds, const fs::path & work) {
            std::error_code ec;
            // 1. paroles : lignes connues, puis temps de chaque ligne d'après Whisper
            std::vector<ClipLine> lines;
            if (!t.song.instrumental) {
                // fiche « paroles.txt » du morceau (corrigée à la main si besoin) ; à défaut, les paroles de la composition
                write_fiches(t.song, song_folder(t.song.id));  // crée les fiches manquantes des anciens morceaux
                std::string   text = t.song.lyrics;
                std::ifstream pf(song_folder(t.song.id) / L"paroles.txt", std::ios::binary);
                if (pf) text.assign((std::istreambuf_iterator<char>(pf)), std::istreambuf_iterator<char>());
                lines = clip_lyrics_lines(text);
            }
            auto known = aligned.find(t.song.id);
            if (known == aligned.end() && t.karaoke && !lines.empty()) {  // 1.0.98 : karaoké : temps déjà calés dans paroles.lrc
                std::vector<ClipLine> lrc = read_lrc(song_folder(t.song.id) / L"paroles.lrc", seconds);
                if (lrc.size() >= std::max<size_t>(3, lines.size() / 2)) {
                    aligned[t.song.id] = lrc;
                    known = aligned.find(t.song.id);
                }
            }
            if (known != aligned.end()) {
                lines = known->second;
            } else if (!lines.empty()) {
                std::vector<ClipWord> heard;
                if (!t.whisper_exe.empty() && fs::exists(t.whisper_exe, ec) && fs::exists(t.whisper_model, ec)) {
                    fs::path wav = work / L"voix.wav", base = work / L"mots";
                    if (ffmpeg(j, L"-i " + quote_arg(audio.wstring()) + L" -vn -ac 1 -ar 16000 " + quote_arg(wav.wstring())) == 0 && !j.cancel) {
                        std::wstring lang = t.song.lyrics_lang.empty() ? L"auto" : widen(t.song.lyrics_lang);
                        std::wstring cmd  = quote_arg(t.whisper_exe.wstring()) + L" -m " + quote_arg(t.whisper_model.wstring()) + L" -f " +
                                           quote_arg(wav.wstring()) + L" -l " + lang + L" -ml 1 -sow -mc 0 -oj -np -of " + quote_arg(base.wstring());
                        run_command(cmd, nullptr, [&j](void * h) { j.proc = h; });
                        std::ifstream f(base.wstring() + L".json", std::ios::binary);
                        std::string   json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                        heard = clip_parse_words(json);
                    }
                }
                if (j.cancel) return lines;
                clip_align(lines, heard, seconds);  // sans Whisper (heard vide) : lignes réparties sur la durée
                aligned[t.song.id] = lines;
                if (t.karaoke && !heard.empty()) write_lyric_files(t.song, lines);  // 1.0.98 : paroles.lrc pour les prochains karaokés
            }
            return lines;
        };
        // 1.0.61 : tous les scénarios d'abord ; ensuite le modèle d'images reste chargé d'un morceau à l'autre
        std::set<std::string> planned;
        for (const ClipTask & t : tasks) {
            if (j.cancel || !t.make_scenario || !planned.insert(t.song.id).second) continue;
            fs::path audio = song_folder(t.song.id) / "audio.flac";
            if (!fs::exists(audio)) audio = song_folder(t.song.id) / "audio.wav";
            double seconds = fs::exists(audio) ? media_duration(audio) : 0;
            if (seconds <= 0) continue;
            std::error_code ec;
            fs::path work = fs::temp_directory_path() / (L"studio_clip_" + widen(t.song.id));
            fs::create_directories(work, ec);
            std::vector<ClipLine> lines = song_lines(t, audio, seconds, work);
            ClipScenes  unused;
            std::string note;  // un échec ici sera repris et signalé au passage du morceau
            if (!j.cancel) t.make_scenario(j, t.song, lines, seconds, unused, note);
        }
        for (size_t i = 0; i < tasks.size() && !j.cancel; i++) {
            const ClipTask & t = tasks[i];
            j.total = (int) tasks.size();
            j.item  = (int) i + 1;
            fs::path         audio = song_folder(t.song.id) / "audio.flac";
            if (!fs::exists(audio)) audio = song_folder(t.song.id) / "audio.wav";
            double seconds = fs::exists(audio) ? media_duration(audio) : 0;
            if (seconds <= 0) {
                errors += trf("audio introuvable : %s", t.song.title.c_str()) + "\n";
                continue;
            }
            std::error_code ec;
            fs::create_directories(t.output.parent_path(), ec);
            fs::path work = fs::temp_directory_path() / (L"studio_clip_" + widen(t.song.id));
            fs::create_directories(work, ec);

            std::vector<ClipLine> lines = song_lines(t, audio, seconds, work);
            if (j.cancel) break;

            // 1 bis. scénario d'après les paroles : une image par scène (IA locale), sinon l'image du clip (la pochette)
            ClipScenes scenes;
            if (t.make_scenes && !j.cancel) {
                std::string note;
                if (!t.make_scenes(j, t.song, lines, seconds, scenes, note) || scenes.images.empty()) {
                    scenes = ClipScenes{};
                    if (!note.empty()) notes += note + " (" + t.song.title + ")\n";
                }
            }
            if (j.cancel) break;
            if (scenes.images.empty()) {
                scenes.images = { t.image };
                scenes.starts = { 0 };
            }
            // 1.0.74 — Short : seulement l'extrait autour du refrain, avec les images (déjà faites) qui le couvrent
            double from = 0;
            if (t.short_seconds > 0 && seconds > t.short_seconds) {
                from = clip_short_start(lines, seconds, t.short_seconds);
                lines = clip_window_lines(lines, from, t.short_seconds);
                ClipScenes cut;
                for (size_t k = 0; k < scenes.images.size(); k++) {
                    double a = k < scenes.starts.size() ? scenes.starts[k] : 0, z = k + 1 < scenes.starts.size() ? scenes.starts[k + 1] : seconds;
                    if (z <= from || a >= from + t.short_seconds) continue;
                    cut.images.push_back(scenes.images[k]);
                    cut.starts.push_back(cut.starts.empty() ? 0 : a - from);
                }
                if (!cut.images.empty()) scenes = cut;
                else scenes = ClipScenes{ { scenes.images[0] }, { 0 } };
                notes += trf("Short « %s » : de %s à %s.", t.song.title.c_str(), fmt_seconds(from).c_str(), fmt_seconds(from + t.short_seconds).c_str()) + "\n";
                seconds = t.short_seconds;
            }

            // 2. sous-titres et script de filtres (chemin court 8.3 pour libass, qui n'ouvre pas les chemins accentués)
            fs::path ass = work / L"clip.ass", graph = work / L"clip.filter";
            {
                std::ofstream f(ass, std::ios::binary);
                ClipTexts texts;
                texts.title    = clean_title(display_title(t.song.title));
                texts.artist   = t.song.artist;
                texts.album    = t.song.album;
                texts.start    = t.text_start;
                texts.end      = t.text_end;
                texts.duration = seconds;
                texts.karaoke  = t.karaoke;
                texts.end_pos  = t.end_pos;  // 1.0.98
                texts.end_box  = t.end_box;
                f << clip_ass(lines, t.width, t.height, texts);
            }
            std::wstring short_ass = ass.wstring();
            wchar_t      sp[MAX_PATH] = {};
            if (GetShortPathNameW(ass.c_str(), sp, MAX_PATH) > 0) short_ass = sp;
            {
                std::ofstream f(graph, std::ios::binary);
                f << clip_filtergraph(lines, seconds, t.width, t.height, clip_filter_path(narrow(short_ass)), scenes.starts);
            }

            // 3. montage
            fs::path tmp = t.output;
            tmp += L".part";
            std::wstring inputs;
            for (const fs::path & img : scenes.images) inputs += L" -loop 1 -framerate 30 -i " + quote_arg(img.wstring());
            // Short : le son commence à « from », avec un fondu d'entrée et de sortie
            std::wstring cut_in = from > 0 ? L" -ss " + std::to_wstring(from) + L" -t " + std::to_wstring(seconds) : L"";
            std::wstring fades  = t.short_seconds > 0 ? L" -af afade=t=in:st=0:d=0.8,afade=t=out:st=" + std::to_wstring(std::max(0.0, seconds - 2)) + L":d=2" : L"";
            std::wstring args = L"-progress pipe:1 -nostats" + inputs + cut_in + L" -i " +
                                quote_arg(audio.wstring()) + L" -filter_complex_script " + quote_arg(graph.wstring()) +
                                L" -map [v] -map " + std::to_wstring(scenes.images.size()) + L":a -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p -c:a aac -b:a 320k" + fades + L" -t " +
                                std::to_wstring(seconds) + L" -movflags +faststart -f mp4 " + quote_arg(tmp.wstring());
            int code = ffmpeg(j, args, [&](const std::string & line) {
                if (line.rfind("out_time_us=", 0) == 0) {
                    double us = atof(line.c_str() + 12);
                    if (us > 0) j.progress = (float) std::min(0.999, (i + std::min(0.98, us / 1e6 / seconds)) / tasks.size());
                }
            }, true);
            if (j.cancel) {
                fs::remove(tmp, ec);
                fs::remove_all(work, ec);
                break;
            }
            std::string why;
            if (code != 0 || !fs::exists(tmp)) {
                errors += trf("échec ffmpeg (%d) : %s", (int) code, narrow(t.output.filename().wstring()).c_str()) + "\n";
            } else if (!video_is_sound(j, tmp, why)) {
                errors += trf("vidéo invalide (%s) : %s", why.c_str(), narrow(t.output.filename().wstring()).c_str()) + "\n";
            } else if (MoveFileExW(tmp.c_str(), t.output.c_str(), MOVEFILE_REPLACE_EXISTING) == 0) {
                errors += trf("renommage impossible : %s", narrow(t.output.filename().wstring()).c_str()) + "\n";
            } else {
                made++;
            }
            fs::remove(tmp, ec);
            fs::remove_all(work, ec);
            j.progress = (float) (i + 1) / tasks.size();
        }
        tasks.clear();  // 1.0.100 : arrête le serveur d'images (gardé d'un morceau à l'autre) et rend la carte avant d'annoncer la fin
        if (j.cancel) {
            finish_job(j, false, trf("Création des clips annulée (%d terminé(s)).", (int) made), folder);
        } else if (made == 0) {
            finish_job(j, false, (errors.empty() ? std::string(tr("Aucun clip créé.")) : errors) + (notes.empty() ? std::string() : "\n" + notes), folder);
        } else {
            std::string msg = trf("%d clip(s) créé(s) et contrôlé(s).", (int) made);
            if (!notes.empty()) msg += "\n" + notes;
            if (!errors.empty()) msg += "\n" + errors;
            finish_job(j, errors.empty(), msg, folder);
        }
    });
}

fs::path pick_folder(void * owner, const wchar_t * title) {
    BROWSEINFOW bi{};
    bi.hwndOwner = (HWND) owner;
    bi.lpszTitle = title;
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return {};
    wchar_t path[MAX_PATH] = {};
    bool    ok             = SHGetPathFromIDListW(pidl, path) != 0;
    CoTaskMemFree(pidl);
    return ok ? fs::path(path) : fs::path();
}

// ---- 1.0.57 : descriptions rédigées par l'IA locale

// Les styles citent de vrais artistes (« <guitariste> fuzz solo ») et des réglages (« 105 BPM ») : seuls les genres sont
// montrés à l'IA. Une balise est gardée si elle contient un mot de genre, sans chiffre ni autre mot à majuscule (un nom propre).
static bool is_genre_word(const std::string & w) {
    std::string low = w;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char) tolower(c); });
    for (const char * g : { "rock", "metal", "blues", "pop", "jazz", "folk", "grunge", "punk", "soul", "funk", "country", "electro", "electronic", "ambient",
                            "ballad", "reggae", "hip", "hop", "classical", "orchestral", "acoustic", "gothic", "goth", "indie", "alternative", "psychedelic",
                            "stoner", "sludge", "doom", "progressive", "synthwave", "new", "wave", "hard", "heavy", "groove", "desert" })
        if (low.find(g) != std::string::npos) return true;
    return false;
}

static std::vector<std::string> split_words(const std::string & tag) {
    std::vector<std::string> out;
    std::stringstream        in(tag);
    std::string              w;
    while (in >> w) out.push_back(w);
    return out;
}

static std::string safe_style(const std::string & style, std::vector<std::string> * names = nullptr) {
    std::string       out, tag;
    std::stringstream in(style);
    while (std::getline(in, tag, ',')) {
        bool genre = false, keep = true;
        std::vector<std::string> words = split_words(tag);
        for (const std::string & w : words) {
            bool g = is_genre_word(w);
            genre  = genre || g;
            if (!g && isupper((unsigned char) w[0])) {
                keep = false;
                if (names && w.size() > 2) names->push_back(w);  // nom propre probable : vérifié dans la réponse
            }
            if (std::any_of(w.begin(), w.end(), [](unsigned char c) { return isdigit(c); })) keep = false;
        }
        if (genre && keep) {
            for (const std::string & w : words) out += (out.empty() || out.back() == ' ' ? "" : (&w == &words[0] ? ", " : " ")) + w;
        }
    }
    return out;
}

static const char * language_name(const std::string & code) {
    static const std::pair<const char *, const char *> names[] = { { "fr", "French" }, { "en", "English" }, { "es", "Spanish" }, { "it", "Italian" }, { "de", "German" },
                                                                   { "pt", "Portuguese" }, { "ja", "Japanese" }, { "ko", "Korean" }, { "zh-Hans", "Simplified Chinese" }, { "yue", "Cantonese" } };
    for (const auto & [c, n] : names)
        if (code == c) return n;
    return "English";
}

static std::string description_system() {
    return "You write short presentation texts for music releases. "
           "Plain text only: no title, no markdown, no hashtags, no quotation marks around the text. "
           "Use only the facts given. Invent nothing: no dates, concerts, awards, sales, band members or biography. Do not mention tempo or BPM. Keep song, album and group names exactly as given, never translated. "
           "Never name any real artist, band or person other than the names given.";
}

// Réponse nettoyée : blancs de tête et de fin, guillemets autour du texte.
static std::string clean_answer(std::string s) {
    auto strip = [&] {
        size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
        s = b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    };
    strip();
    if (s.size() > 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    strip();
    return s;
}

void write_descriptions(const std::vector<Song> & songs, const std::string & lang, const AiAsk & ask,
                        const std::function<void(const std::string &)> & say, std::atomic<bool> & cancel, std::atomic<int> & written, std::string & error) {
    if (songs.empty()) return;
    const Song &    first  = songs[0];
    const fs::path  sdir   = song_folder(first.id);
    const bool      has_al = !first.album.empty();
    const fs::path  adir   = sdir.parent_path(), gdir = has_al ? adir.parent_path() : adir;
    const std::string sys = description_system(), genres = safe_style(first.style);
    const std::string tail = std::string(" Write only in ") + language_name(lang) + ", even if the lyrics or names are in another language.";
    std::vector<std::string> names;  // noms propres écartés des styles : ils ne doivent pas ressortir dans le texte
    for (const Song & s : songs) safe_style(s.style, &names);
    std::string     titles;
    for (const Song & s : songs) titles += (titles.empty() ? "" : ", ") + clean_title(display_title(s.title));

    auto one = [&](const fs::path & file, const std::string & label, const std::string & user) {
        say(label);
        std::error_code ec;
        if (cancel || !error.empty() || fs::exists(file, ec)) return;  // jamais remplacée : l'utilisateur peut l'avoir corrigée
        std::string text;
        for (int attempt = 0; attempt < 2 && !cancel && text.empty(); attempt++) {
            AiCallResult r = ask(sys, user + tail + (attempt ? " Do not name any real musician." : ""));
            if (!r.ok) {
                if (!cancel) error = r.error;
                return;
            }
            text = clean_answer(r.text);
            for (const std::string & n : names)
                if (text.find(n) != std::string::npos) text.clear();
        }
        if (text.empty()) return;  // texte vide ou nom d'artiste encore présent : rien d'écrit plutôt qu'un texte à ne pas publier
        std::ofstream(file, std::ios::binary | std::ios::trunc) << text << "\n";
        written++;
    };
    one(gdir / L"description.txt", first.artist, "Write 3 to 4 sentences presenting the music group \"" + first.artist + "\". Genres: " + genres + ". Album: " + first.album + ". Song titles: " + titles + ".");
    if (has_al) one(adir / L"description.txt", first.album, "Write 3 to 4 sentences presenting the album \"" + first.album + "\" by \"" + first.artist + "\". Genres: " + genres + ". Song titles: " + titles + ".");
    for (const Song & s : songs) {
        const std::string title = clean_title(display_title(s.title));
        std::string user = "Write 2 to 3 sentences presenting the song \"" + title + "\" by \"" + s.artist + "\"" + (has_al ? " from the album \"" + s.album + "\"" : "") + ". Genres: " + safe_style(s.style) + ". ";
        user += s.instrumental ? std::string("It is an instrumental.") : "What the song talks about, from its lyrics (do not quote them):\n" + s.lyrics.substr(0, 1500);
        one(song_folder(s.id) / L"description.txt", title, user);
    }
}


// ---- 1.0.83 : contrôle qualité
void start_quality(const std::shared_ptr<Job> & job, std::vector<Song> songs, fs::path whisper_exe, fs::path whisper_model, fs::path report,
                   std::shared_ptr<std::vector<QualityFinding>> found) {
    job->thread = std::thread([=]() mutable {
        Job &              j = *job;
        std::error_code    ec;
        const bool         with_whisper = !whisper_exe.empty() && fs::exists(whisper_exe, ec) && fs::exists(whisper_model, ec);
        const fs::path     work         = fs::temp_directory_path() / L"studio_controle_qualite";
        fs::create_directories(work, ec);
        std::ostringstream out;
        int                checked = 0;
        for (size_t i = 0; i < songs.size() && !j.cancel; i++) {
            const Song & s = songs[i];
            j.total        = (int) songs.size();
            j.item         = (int) i + 1;
            j.progress     = (float) i / songs.size();
            const std::string label = (s.artist.empty() ? std::string() : s.artist + " — ") + display_title(s.title);
            {
                std::lock_guard<std::mutex> lock(j.mutex);
                j.message = label;
            }
            QualityFinding f{ s.id, label, {} };
            const fs::path dir   = song_folder(s.id);
            fs::path       audio = dir / "audio.flac";
            if (!fs::exists(audio, ec)) audio = dir / "audio.wav";
            const double seconds = fs::exists(audio, ec) ? media_duration(audio) : 0;
            if (seconds <= 0) {
                f.problems.push_back(tr("audio introuvable ou illisible"));
            } else {
                // 1. dernière partie d'un morceau long : le moteur a conclu juste après la reprise
                if (s.long_join > 0) {
                    const double part  = fs::exists(dir / "partie2_seule.wav", ec) ? media_duration(dir / "partie2_seule.wav") : 0;
                    const double added = part - s.long_join;
                    if (part > 0 && added < 40)
                        f.problems.push_back(trf("dernière partie trop courte : %d s ajoutées après le raccord", (int) std::max(0.0, added)));
                }
                // 2. durée loin de la cible (morceau seul ; un morceau long est jugé sur sa dernière partie)
                if (s.long_join <= 0 && s.long_rest.empty()) {
                    const double target = s.auto_duration ? s.expected_seconds : s.duration;
                    const double ratio  = s.auto_duration ? 0.6 : 0.75;
                    if (target > 0 && seconds < target * ratio)
                        f.problems.push_back(trf("durée %s pour %s attendues", fmt_seconds(seconds).c_str(), fmt_seconds(target).c_str()));
                }
                // 3. silence de 5 s ou plus au milieu (ni les 2 premières secondes, ni les 10 dernières)
                if (s.long_rest.empty()) {
                    double                   start = -1;
                    std::vector<std::string> gaps;
                    ffmpeg(j, L"-v info -i " + quote_arg(audio.wstring()) + L" -af silencedetect=noise=-45dB:d=5 -f null -", [&](const std::string & line) {
                        size_t a = line.find("silence_start: "), b = line.find("silence_end: ");
                        if (a != std::string::npos) start = atof(line.c_str() + a + 15);
                        if (b != std::string::npos && start >= 0) {
                            const double end = atof(line.c_str() + b + 13);
                            if (end > 2 && start < seconds - 10 && gaps.size() < 3)
                                gaps.push_back(trf("silence de %d s à %s", (int) (end - start + 0.5), fmt_seconds(start).c_str()));
                            start = -1;
                        }
                    });
                    if (start >= 0 && start < seconds - 10)  // silence qui dure jusqu'à la fin
                        gaps.push_back(trf("silence de %d s à %s", (int) (seconds - start + 0.5), fmt_seconds(start).c_str()));
                    for (auto & g : gaps) f.problems.push_back(g);
                }
                // 4. paroles chantées : part des mots du texte retrouvés par Whisper, puis fin du texte entendue
                if (s.long_rest.empty() && !s.instrumental && with_whisper && !j.cancel) {
                    std::string   text = s.lyrics;
                    std::ifstream pf(dir / L"paroles.txt", std::ios::binary);
                    if (pf) text.assign((std::istreambuf_iterator<char>(pf)), std::istreambuf_iterator<char>());
                    std::vector<ClipLine> lines = clip_lyrics_lines(text);
                    if (lines.size() >= 4) {
                        fs::path wav = work / L"voix.wav", base = work / L"mots";
                        fs::remove(base.wstring() + L".json", ec);
                        if (ffmpeg(j, L"-i " + quote_arg(audio.wstring()) + L" -vn -ac 1 -ar 16000 " + quote_arg(wav.wstring())) == 0 && !j.cancel) {
                            std::wstring lang = s.lyrics_lang.empty() ? L"auto" : widen(s.lyrics_lang);
                            run_command(quote_arg(whisper_exe.wstring()) + L" -m " + quote_arg(whisper_model.wstring()) + L" -f " + quote_arg(wav.wstring()) +
                                            L" -l " + lang + L" -ml 1 -sow -mc 0 -oj -np -of " + quote_arg(base.wstring()),
                                        nullptr, [&j](void * h) { j.proc = h; });
                            std::ifstream wf(base.wstring() + L".json", std::ios::binary);
                            std::string   json((std::istreambuf_iterator<char>(wf)), std::istreambuf_iterator<char>());
                            const std::vector<ClipWord> heard = clip_parse_words(json);
                            if (!j.cancel && !json.empty()) {
                                // texte entier, puis son dernier quart seul : un morceau qui conclut avant la fin du texte
                                const double          all = clip_align(lines, heard, seconds);
                                std::vector<ClipLine> tail(lines.end() - lines.size() / 4, lines.end());
                                const double          end = clip_align(tail, heard, seconds);
                                if (all < 0.5) f.problems.push_back(trf("paroles peu reconnues : %d %% des mots entendus", (int) (all * 100 + 0.5)));
                                else if (end < 0.3)
                                    f.problems.push_back(trf("fin des paroles non chantée : %d %% des mots du dernier quart entendus", (int) (end * 100 + 0.5)));
                            }
                        }
                    }
                }
            }
            checked++;
            if (!f.problems.empty()) {
                out << label << "\n";
                for (const std::string & p : f.problems) out << "  - " << p << "\n";
                std::lock_guard<std::mutex> lock(j.mutex);
                found->push_back(f);
            }
        }
        fs::remove_all(work, ec);
        size_t bad = 0;
        {
            std::lock_guard<std::mutex> lock(j.mutex);
            bad = found->size();
        }
        std::string head = trf("%d morceau(x) contrôlé(s), %d à refaire.", checked, (int) bad);
        if (!with_whisper) head += " " + std::string(tr("Paroles non vérifiées : modèle Whisper absent."));
        fs::create_directories(report.parent_path(), ec);
        {
            std::ofstream r(report, std::ios::binary);
            r << head << "\n\n" << out.str();
        }
        j.progress = 1;
        if (j.cancel) finish_job(j, false, trf("Contrôle qualité arrêté (%d morceau(x) contrôlé(s)).", checked), report);
        else finish_job(j, true, head, report);
    });
}


// ================================================================== 1.0.97 : outils audio
static fs::path best_audio(const fs::path & dir) {
    std::error_code ec;
    for (const wchar_t * n : { L"audio.wav", L"audio.flac", L"audio.mp3" })
        if (fs::exists(dir / n, ec)) return dir / n;
    return {};
}
static std::string json_num(const std::string & text, const char * key) {  // valeur de "key" : "…" dans la sortie de loudnorm
    size_t p = text.find(std::string("\"") + key + "\"");
    if (p == std::string::npos) return {};
    p = text.find(':', p);
    size_t a = text.find('"', p), b = a == std::string::npos ? a : text.find('"', a + 1);
    return b == std::string::npos ? std::string() : text.substr(a + 1, b - a - 1);
}
static std::wstring wfmt(const char * f, double v) {
    char b[64];
    snprintf(b, sizeof b, f, v);
    return widen(b);
}

void start_master(const std::shared_ptr<Job> & job, std::vector<Song> songs, double lufs, double tp, bool mp3) {
    job->thread = std::thread([=]() mutable {
        Job & j = *job;
        std::error_code ec;
        int done = 0;
        std::string errors;
        fs::path last;
        for (size_t i = 0; i < songs.size() && !j.cancel; i++) {
            const Song & s = songs[i];
            j.total = (int) songs.size(); j.item = (int) i + 1; j.progress = (float) i / songs.size();
            { std::lock_guard<std::mutex> lock(j.mutex); j.message = display_title(s.title); }
            const fs::path dir = song_folder(s.id), src = best_audio(dir);
            if (src.empty()) { errors += display_title(s.title) + " : " + tr("audio introuvable") + "\n"; continue; }
            const fs::path out_dir = dir / L"master";
            fs::create_directories(out_dir, ec);
            const std::wstring base = widen(safe_filename(display_title(s.title))) + L"_master";
            const std::wstring target = L"I=" + wfmt("%.1f", lufs) + L":TP=" + wfmt("%.1f", tp) + L":LRA=11";
            std::string info;  // passe 1 : mesure
            run_command(ffmpeg_exe() + L" -hide_banner -nostats -i " + quote_arg(src.wstring()) + L" -af loudnorm=" + target + L":print_format=json -f null -",
                        [&](const std::string & l) { info += l + "\n"; }, [&j](void * h) { j.proc = h; });
            const std::string mi = json_num(info, "input_i"), mtp = json_num(info, "input_tp"), mlra = json_num(info, "input_lra"),
                              mth = json_num(info, "input_thresh"), off = json_num(info, "target_offset");
            if (mi.empty() || mth.empty() || off.empty()) { errors += display_title(s.title) + " : " + tr("mesure du volume impossible") + "\n"; continue; }
            const std::wstring pass2 = L"loudnorm=" + target + L":measured_I=" + widen(mi) + L":measured_TP=" + widen(mtp) + L":measured_LRA=" + widen(mlra) +
                                       L":measured_thresh=" + widen(mth) + L":offset=" + widen(off) + L":linear=true,aresample=44100,alimiter=limit=0.89:level=0";
            const fs::path wav = out_dir / (base + L".wav");
            if (ffmpeg(j, L"-i " + quote_arg(src.wstring()) + L" -af " + pass2 + L" -ar 44100 -c:a pcm_s24le " + quote_arg(wav.wstring())) != 0) {
                errors += display_title(s.title) + " : " + tr("échec ffmpeg") + "\n";
                continue;
            }
            if (mp3) ffmpeg(j, L"-i " + quote_arg(wav.wstring()) + L" -c:a libmp3lame -b:a 320k " + quote_arg((out_dir / (base + L".mp3")).wstring()));
            last = out_dir;
            done++;
        }
        j.progress = 1;
        std::string msg = trf("%d morceau(x) masterisé(s) à %.1f LUFS (dossier « master » de chaque morceau).", done, lufs);
        if (!errors.empty()) msg += "\n" + errors;
        if (j.cancel) finish_job(j, false, tr("Mastering arrêté."), last);
        else finish_job(j, done > 0, msg, last);
    });
}

static std::string stamp(double t, bool srt) {
    if (t < 0) t = 0;
    int ms = (int) (t * 1000 + 0.5), h = ms / 3600000, m = ms / 60000 % 60, s = ms / 1000 % 60, c = ms % 1000;
    char b[40];
    if (srt) snprintf(b, sizeof b, "%02d:%02d:%02d,%03d", h, m, s, c);
    else snprintf(b, sizeof b, "%02d:%02d.%02d", ms / 60000, s, c / 10);
    return b;
}

void start_lrc(const std::shared_ptr<Job> & job, std::vector<Song> songs, fs::path whisper_exe, fs::path whisper_model) {
    job->thread = std::thread([=]() mutable {
        Job & j = *job;
        std::error_code ec;
        std::string errors;
        int done = 0;
        if (whisper_exe.empty() || !fs::exists(whisper_exe, ec) || !fs::exists(whisper_model, ec)) {
            finish_job(j, false, tr("Whisper (transcription) absent : installez-le dans Réglages › Transcription des paroles."), {});
            return;
        }
        const fs::path work = fs::temp_directory_path() / L"studio_lrc";
        fs::create_directories(work, ec);
        for (size_t i = 0; i < songs.size() && !j.cancel; i++) {
            const Song & s = songs[i];
            j.total = (int) songs.size(); j.item = (int) i + 1; j.progress = (float) i / songs.size();
            { std::lock_guard<std::mutex> lock(j.mutex); j.message = display_title(s.title); }
            if (s.instrumental) continue;
            const fs::path dir = song_folder(s.id), audio = best_audio(dir);
            std::string text = s.lyrics;
            std::ifstream pf(dir / L"paroles.txt", std::ios::binary);
            if (pf) text.assign((std::istreambuf_iterator<char>(pf)), std::istreambuf_iterator<char>());
            std::vector<ClipLine> lines = clip_lyrics_lines(text);
            if (audio.empty() || lines.empty()) continue;
            const double seconds = media_duration(audio);
            const fs::path wav = work / L"voix.wav", base = work / L"mots";
            fs::remove(base.wstring() + L".json", ec);
            if (ffmpeg(j, L"-i " + quote_arg(audio.wstring()) + L" -vn -ac 1 -ar 16000 " + quote_arg(wav.wstring())) != 0 || j.cancel) continue;
            const std::wstring lang = s.lyrics_lang.empty() ? L"auto" : widen(s.lyrics_lang);
            run_command(quote_arg(whisper_exe.wstring()) + L" -m " + quote_arg(whisper_model.wstring()) + L" -f " + quote_arg(wav.wstring()) + L" -l " + lang +
                            L" -ml 1 -sow -mc 0 -oj -np -of " + quote_arg(base.wstring()), nullptr, [&j](void * h) { j.proc = h; });
            std::ifstream wf(base.wstring() + L".json", std::ios::binary);
            const std::string json((std::istreambuf_iterator<char>(wf)), std::istreambuf_iterator<char>());
            if (json.empty()) { errors += display_title(s.title) + " : " + tr("aucun mot reconnu") + "\n"; continue; }
            clip_align(lines, clip_parse_words(json), seconds);
            std::ostringstream lrc, srt;
            lrc << "[ti:" << display_title(s.title) << "]\n[ar:" << s.artist << "]\n[al:" << s.album << "]\n";
            int n = 0;
            for (size_t k = 0; k < lines.size(); k++) {
                const double end = lines[k].end > lines[k].start ? lines[k].end : (k + 1 < lines.size() ? lines[k + 1].start : lines[k].start + 3);
                lrc << "[" << stamp(lines[k].start, false) << "]" << lines[k].text << "\n";
                srt << ++n << "\n" << stamp(lines[k].start, true) << " --> " << stamp(end, true) << "\n" << lines[k].text << "\n\n";
            }
            std::ofstream(dir / L"paroles.lrc", std::ios::binary | std::ios::trunc) << "\xEF\xBB\xBF" << lrc.str();
            std::ofstream(dir / L"paroles.srt", std::ios::binary | std::ios::trunc) << "\xEF\xBB\xBF" << srt.str();
            done++;
        }
        fs::remove_all(work, ec);
        j.progress = 1;
        std::string msg = trf("%d morceau(x) : fichiers « paroles.lrc » et « paroles.srt » écrits dans le dossier du morceau.", done);
        if (!errors.empty()) msg += "\n" + errors;
        if (j.cancel) finish_job(j, false, tr("Paroles synchronisées arrêtées."), {});
        else finish_job(j, done > 0, msg, songs.empty() ? fs::path() : song_folder(songs[0].id));
    });
}

static std::string html_escape(const std::string & s) {
    std::string o;
    for (char c : s) o += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : std::string(1, c);
    return o;
}
static std::string file_url(const fs::path & p) {
    std::string u = "file:///";
    for (unsigned char c : narrow(p.wstring())) {
        if (c == '\\') u += '/';
        else if (isalnum(c) || strchr("-_.~/:", c)) u += (char) c;
        else { char b[8]; snprintf(b, sizeof b, "%%%02X", c); u += b; }
    }
    return u;
}

void start_booklet(const std::shared_ptr<Job> & job, std::vector<Song> songs, std::string artist, std::string album, fs::path cover, fs::path dest) {
    job->thread = std::thread([=]() mutable {
        Job & j = *job;
        std::error_code ec;
        fs::create_directories(dest, ec);
        std::ostringstream h;
        h << "<!doctype html><html><head><meta charset=\"utf-8\"><title>" << html_escape(album) << "</title><style>"
             "@page{size:A5;margin:14mm}body{font-family:'Segoe UI',sans-serif;color:#222}"
             ".cover{page-break-after:always;text-align:center}.cover img{width:100%;max-height:150mm;object-fit:cover}"
             "h1{font-size:26pt;margin:8mm 0 2mm}h2{font-size:15pt;margin:0 0 4mm}.band{font-size:13pt;color:#555}"
             "ol{font-size:12pt;line-height:1.7}.track{page-break-before:always}"
             "pre{font-family:inherit;font-size:11pt;line-height:1.5;white-space:pre-wrap}.tag{color:#7b5cd6;font-weight:600}"
             "</style></head><body><div class=\"cover\">";
        if (!cover.empty() && fs::exists(cover, ec)) h << "<img src=\"" << file_url(cover) << "\">";
        h << "<h1>" << html_escape(album) << "</h1><div class=\"band\">" << html_escape(artist) << "</div></div><div><h2>" << tr("Liste des morceaux") << "</h2><ol>";
        for (const Song & s : songs) h << "<li>" << html_escape(clean_title(display_title(s.title))) << "</li>";
        h << "</ol></div>";
        for (size_t i = 0; i < songs.size(); i++) {
            const Song & s = songs[i];
            h << "<div class=\"track\"><h2>" << (i + 1) << ". " << html_escape(clean_title(display_title(s.title))) << "</h2><pre>";
            if (s.instrumental) h << "<i>" << tr("Morceau instrumental") << "</i>";
            else {
                std::istringstream in(s.lyrics);
                std::string line;
                while (std::getline(in, line)) {
                    size_t b = line.find_first_not_of(" \t\r");
                    if (b != std::string::npos && line[b] == '[') h << "<span class=\"tag\">" << html_escape(line) << "</span>\n";
                    else h << html_escape(line) << "\n";
                }
            }
            h << "</pre></div>";
        }
        h << "</body></html>";
        const fs::path html = dest / L"livret.html", pdf = dest / L"livret.pdf";
        std::ofstream(html, std::ios::binary | std::ios::trunc) << h.str();
        std::wstring edge;
        for (const wchar_t * p : { L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe", L"C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe" })
            if (fs::exists(p, ec)) { edge = p; break; }
        if (edge.empty()) { finish_job(j, false, tr("Microsoft Edge introuvable : le livret HTML est écrit, ouvrez-le et imprimez-le en PDF."), html); return; }
        fs::remove(pdf, ec);
        const fs::path profile = fs::temp_directory_path() / L"studio_livret_edge";
        run_command(quote_arg(edge) + L" --headless=new --disable-gpu --no-pdf-header-footer --user-data-dir=" + quote_arg(profile.wstring()) +
                        L" --print-to-pdf=" + quote_arg(pdf.wstring()) + L" " + quote_arg(widen(file_url(html))),
                    nullptr, [&j](void * hh) { j.proc = hh; });
        fs::remove_all(profile, ec);
        j.progress = 1;
        if (fs::exists(pdf, ec) && fs::file_size(pdf, ec) > 1000) finish_job(j, true, trf("Livret de %d morceau(x) écrit : %s", (int) songs.size(), narrow(pdf.wstring()).c_str()), pdf);
        else finish_job(j, false, tr("Le PDF n'a pas pu être créé : le livret HTML est dans le dossier de l'album."), html);
    });
}

void start_daw_export(const std::shared_ptr<Job> & job, std::vector<Song> songs, fs::path dest) {
    job->thread = std::thread([=]() mutable {
        Job & j = *job;
        std::error_code ec;
        int n = 0;
        for (size_t i = 0; i < songs.size() && !j.cancel; i++) {
            const Song & s = songs[i];
            j.total = (int) songs.size(); j.item = (int) i + 1; j.progress = (float) i / songs.size();
            const fs::path dir = song_folder(s.id);
            std::string tag;
            std::ifstream sf(dir / L"score.abc", std::ios::binary);
            std::string abc((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>());
            if (!abc.empty()) {
                ScoreInfo sc = parse_score(abc);
                if (sc.tempo > 0) tag += " - " + std::to_string(sc.tempo) + " BPM";
                if (!sc.key.empty()) tag += " - " + sc.key;
            }
            const std::wstring name = widen(safe_filename(clean_title(display_title(s.title)) + tag));
            const fs::path out = dest / widen(safe_filename(display_title(s.title)));
            fs::create_directories(out, ec);
            fs::path audio = best_audio(dir);
            const fs::path mdir = dir / L"master";
            if (fs::exists(mdir, ec)) for (const auto & e : fs::directory_iterator(mdir, ec)) if (e.path().extension() == L".wav") audio = e.path();
            if (!audio.empty()) fs::copy_file(audio, out / (name + audio.extension().wstring()), fs::copy_options::overwrite_existing, ec);
            for (const wchar_t * f : { L"score.abc", L"paroles.txt", L"paroles.lrc", L"paroles.srt" })
                if (fs::exists(dir / f, ec)) fs::copy_file(dir / f, out / f, fs::copy_options::overwrite_existing, ec);
            if (fs::exists(dir / L"stems", ec)) fs::copy(dir / L"stems", out / L"stems", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            n++;
        }
        j.progress = 1;
        finish_job(j, n > 0, trf("%d morceau(x) exporté(s) : audio, partition ABC, paroles et stems.", n), dest);
    });
}

void write_publication(const std::vector<Song> & songs, const std::string & lang, const AiAsk & ask, const std::function<void(const std::string &)> & say,
                       std::atomic<bool> & cancel, std::atomic<int> & written, std::string & error) {
    const std::string sys = "You write publication texts for music videos (YouTube, TikTok, Instagram). Use only the facts given. Invent nothing: no dates, awards, sales or biography. "
                            "Never name any real artist, band or person other than the names given. Keep song, album and group names exactly as given.";
    const std::string tail = std::string(" Write only in ") + language_name(lang) + ".";
    for (const Song & s : songs) {
        if (cancel || !error.empty()) return;
        const std::string title = clean_title(display_title(s.title));
        say(title);
        const fs::path file = song_folder(s.id) / L"publication.txt";
        std::error_code ec;
        if (fs::exists(file, ec)) continue;
        std::vector<std::string> names;
        const std::string genres = safe_style(s.style, &names);
        std::string user = "Song \"" + title + "\" by \"" + s.artist + "\"" + (s.album.empty() ? "" : " from the album \"" + s.album + "\"") + ". Genres: " + genres + ". ";
        user += s.instrumental ? std::string("It is an instrumental. ") : "What it talks about (do not quote the lyrics):\n" + s.lyrics.substr(0, 1200) + "\n";
        user += "Answer with exactly three parts, each on its own line: 'TITLE:' a catchy video title (max 70 characters, include the song name), "
                "'DESCRIPTION:' 3 to 4 sentences for the video description, 'HASHTAGS:' 8 to 12 hashtags separated by spaces." + tail;
        std::string text;
        for (int attempt = 0; attempt < 2 && !cancel && text.empty(); attempt++) {
            AiCallResult r = ask(sys, user + (attempt ? " Do not name any real musician." : ""));
            if (!r.ok) { if (!cancel) error = r.error; return; }
            text = clean_answer(r.text);
            for (const std::string & n : names) if (text.find(n) != std::string::npos) text.clear();
            if (text.find("TITLE:") == std::string::npos) text.clear();
        }
        if (text.empty()) continue;
        std::ofstream(file, std::ios::binary | std::ios::trunc) << text << "\n";
        written++;
    }
}

static std::vector<uint64_t> token_grams(const fs::path & audio_json) {
    std::ifstream f(audio_json, std::ios::binary);
    std::string t((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t p = t.find("\"semantic_tokens\"");
    std::vector<uint64_t> out;
    if (p == std::string::npos) return out;
    p = t.find('"', t.find(':', p));
    if (p == std::string::npos) return out;
    std::vector<int> v;
    for (size_t i = p + 1; i < t.size() && t[i] != '"';) {
        if (isdigit((unsigned char) t[i])) { int x = 0; while (isdigit((unsigned char) t[i])) x = x * 10 + (t[i++] - '0'); v.push_back(x); }
        else i++;
    }
    for (size_t i = 0; i + 6 <= v.size(); i++) {
        uint64_t h = 1469598103934665603ull;
        for (int k = 0; k < 6; k++) h = (h ^ (uint64_t) v[i + k]) * 1099511628211ull;
        out.push_back(h);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
static double jaccard(const std::vector<uint64_t> & a, const std::vector<uint64_t> & b) {
    if (a.empty() || b.empty()) return 0;
    size_t i = 0, k = 0, inter = 0;
    while (i < a.size() && k < b.size()) { if (a[i] == b[k]) { inter++; i++; k++; } else if (a[i] < b[k]) i++; else k++; }
    return (double) inter / (double) (a.size() + b.size() - inter);
}
static std::vector<uint64_t> lyric_lines(const std::string & lyrics) {
    std::vector<uint64_t> out;
    std::istringstream in(lyrics);
    std::string line;
    while (std::getline(in, line)) {
        std::string f;
        for (unsigned char c : line) if (isalnum(c) || c >= 128) f += (char) tolower(c);
        if (f.size() < 8 || line.find('[') != std::string::npos) continue;
        out.push_back(std::hash<std::string>{}(f));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void start_duplicates(const std::shared_ptr<Job> & job, std::vector<Song> songs, std::shared_ptr<std::vector<DupPair>> found, fs::path report) {
    job->thread = std::thread([=]() mutable {
        Job & j = *job;
        std::vector<Song> done;
        for (const Song & s : songs) if (s.status == "done" && s.long_rest.empty()) done.push_back(s);
        std::vector<std::vector<uint64_t>> grams(done.size()), lines(done.size());
        for (size_t i = 0; i < done.size() && !j.cancel; i++) {
            j.progress = 0.5f * i / std::max<size_t>(1, done.size());
            grams[i] = token_grams(song_folder(done[i].id) / L"audio.json");
            lines[i] = done[i].instrumental ? std::vector<uint64_t>() : lyric_lines(done[i].lyrics);
        }
        std::vector<DupPair> out;
        for (size_t a = 0; a < done.size() && !j.cancel; a++) {
            j.progress = 0.5f + 0.5f * a / std::max<size_t>(1, done.size());
            for (size_t b = a + 1; b < done.size(); b++) {
                const double t = jaccard(grams[a], grams[b]);
                const double l = lines[a].size() >= 6 && lines[b].size() >= 6 ? jaccard(lines[a], lines[b]) : 0;
                const bool same_family = done[a].parent == done[b].id || done[b].parent == done[a].id || (!done[a].parent.empty() && done[a].parent == done[b].parent);
                if (t < 0.35 && l < 0.6) continue;
                DupPair d{ done[a].id, done[b].id, display_title(done[a].title), display_title(done[b].title), {}, t, l };
                d.why = same_family ? tr("même famille (suite, re-rendu ou nouvelle version)") : t >= 0.35 && l >= 0.6 ? tr("composition et paroles très proches")
                        : t >= 0.35 ? tr("composition très proche") : tr("paroles très proches");
                out.push_back(d);
            }
        }
        std::sort(out.begin(), out.end(), [](const DupPair & x, const DupPair & y) { return std::max(x.tokens, x.lyrics) > std::max(y.tokens, y.lyrics); });
        std::error_code ec;
        fs::create_directories(report.parent_path(), ec);
        {
            std::ofstream r(report, std::ios::binary | std::ios::trunc);
            r << "\xEF\xBB\xBF";
            for (const DupPair & d : out)
                r << d.label_a << "  <->  " << d.label_b << "  (" << (int) (d.tokens * 100) << " % composition, " << (int) (d.lyrics * 100) << " % paroles) : " << d.why << "\n";
        }
        { std::lock_guard<std::mutex> lock(j.mutex); *found = out; }
        j.progress = 1;
        finish_job(j, true, trf("%d couple(s) de morceaux proches sur %d morceau(x).", (int) out.size(), (int) done.size()), report);
    });
}

int best_trial(const std::vector<Song> & trials, std::vector<std::string> & problems) {
    problems.assign(trials.size(), std::string());
    int best = 0;
    double best_score = -1e9;
    for (size_t i = 0; i < trials.size(); i++) {
        const Song & s = trials[i];
        const fs::path audio = best_audio(song_folder(s.id));
        double score = 0;
        const double seconds = audio.empty() ? 0 : media_duration(audio);
        if (seconds <= 0) { problems[i] = tr("audio illisible"); score = -1000; }
        else {
            const double target = s.auto_duration ? s.expected_seconds : s.duration;
            if (target > 0) {
                const double r = seconds / target;
                score -= std::fabs(1 - r) * 100;  // écart à la durée visée
                if (r < (s.auto_duration ? 0.6 : 0.75)) problems[i] += trf("durée %s pour %s attendues", fmt_seconds(seconds).c_str(), fmt_seconds(target).c_str()) + "; ";
            }
            int gaps = 0;
            double start = -1;
            run_command(ffmpeg_exe() + L" -hide_banner -nostats -i " + quote_arg(audio.wstring()) + L" -af silencedetect=noise=-45dB:d=5 -f null -", [&](const std::string & line) {
                size_t a = line.find("silence_start: "), b = line.find("silence_end: ");
                if (a != std::string::npos) start = atof(line.c_str() + a + 15);
                if (b != std::string::npos && start >= 0) { const double end = atof(line.c_str() + b + 13); if (end > 2 && start < seconds - 10) gaps++; start = -1; }
            });
            if (gaps) { score -= 50.0 * gaps; problems[i] += trf("%d silence(s) anormal(aux)", gaps); }
        }
        if (score > best_score) { best_score = score; best = (int) i; }
    }
    return best;
}

void start_stems(const std::shared_ptr<Job> & job, std::vector<Song> songs, fs::path demucs_exe, fs::path demucs_model) {
    job->thread = std::thread([=]() mutable {
        Job & j = *job;
        std::error_code ec;
        int done = 0;
        std::string errors;
        for (size_t i = 0; i < songs.size() && !j.cancel; i++) {
            const Song & s = songs[i];
            j.total = (int) songs.size(); j.item = (int) i + 1; j.progress = (float) i / songs.size();
            { std::lock_guard<std::mutex> lock(j.mutex); j.message = display_title(s.title); }
            const fs::path dir = song_folder(s.id), src = best_audio(dir), out = dir / L"stems";
            if (src.empty()) continue;
            fs::create_directories(out, ec);
            const fs::path wav44 = fs::temp_directory_path() / L"studio_stems_in.wav";
            if (ffmpeg(j, L"-i " + quote_arg(src.wstring()) + L" -ar 44100 -ac 2 -c:a pcm_s16le " + quote_arg(wav44.wstring())) != 0) continue;
            // demucs_mt (demucs.cpp) : <exe> <modèle> <entrée.wav> <dossier de sortie> <fils> ; target_0_drums.wav, target_1_bass.wav, target_2_other.wav, target_3_vocals.wav
            run_command(quote_arg(demucs_exe.wstring()) + L" " + quote_arg(demucs_model.wstring()) + L" " + quote_arg(wav44.wstring()) + L" " + quote_arg(out.wstring()) + L" 8",
                        nullptr, [&j](void * h) { j.proc = h; });
            fs::remove(wav44, ec);
            int found = 0;
            const std::pair<const wchar_t *, const wchar_t *> names[] = { { L"target_0_drums.wav", L"batterie.wav" }, { L"target_1_bass.wav", L"basse.wav" },
                                                                         { L"target_2_other.wav", L"autres.wav" }, { L"target_3_vocals.wav", L"voix.wav" } };
            for (const auto & [from, to] : names)
                if (fs::exists(out / from, ec)) { fs::rename(out / from, out / to, ec); found++; }
            if (found == 4) done++;
            else errors += display_title(s.title) + " : " + tr("séparation incomplète") + "\n";
        }
        j.progress = 1;
        std::string msg = trf("%d morceau(x) séparé(s) en voix, batterie, basse et autres (dossier « stems »).", done);
        if (!errors.empty()) msg += "\n" + errors;
        if (j.cancel) finish_job(j, false, tr("Séparation arrêtée."), {});
        else finish_job(j, done > 0, msg, songs.empty() ? fs::path() : song_folder(songs[0].id) / L"stems");
    });
}
