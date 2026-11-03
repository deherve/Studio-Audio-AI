#include <map>
#include <set>
#include "media.hpp"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "clip.hpp"
#include "fiches.hpp"
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
static std::string read_text(const fs::path & file, size_t max = 100000) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return {};
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s.size() > max ? s.substr(0, max) : s;
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
    std::string answer      = read_text(answer_file);
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
        std::string context = read_text(parent / L"fiche_groupe.txt", 700) + "\n" + read_text(parent / L"fiche_album.txt", 700) + "\n" +
                              read_text(parent.parent_path() / L"fiche_groupe.txt", 700) + "\n" + read_text(song_folder(s.id) / L"fiche.txt", 900);
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
            if (slot.server) r = slot.server->generate(sb.scenes[k].prompt, 1344, 768, png, &j.cancel);
            if (!r.ok && !j.cancel) {
                slot.server.reset();  // libère la mémoire vidéo pour sd-cli
                slot.failed = true;
                r = ai_image_local(eng.sd_cli, eng.image_root, *eng.image_model, sb.scenes[k].prompt, 1344, 768, png, &j.cancel, eng.offload);
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

// Les styles citent de vrais artistes (« Jerry Cantrell fuzz solo ») et des réglages (« 105 BPM ») : seuls les genres sont
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
