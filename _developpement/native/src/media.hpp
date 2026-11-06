// Export d'albums (pochette intÃ©grÃ©e, noms numÃ©rotÃ©s) et vidÃ©os YouTube (pochette fixe + audio), via ffmpeg.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <functional>

#include "ai_writer.hpp"
#include "clip.hpp"
#include "studio.hpp"

// TÃ¢che de fond affichÃ©e dans l'interface (export, vidÃ©o).
struct Job {
    std::string        name;
    std::atomic<int>   state{ 0 };  // 0 en cours, 1 terminÃ©e, 2 en erreur
    std::atomic<float> progress{ 0 };
    std::atomic<int>   item{ 0 }, total{ 0 };  // 1.0.54 : vidÃ©o en cours / nombre de vidÃ©os (affichÃ©s sur la barre de progression)
    std::atomic<bool>  cancel{ false };
    std::atomic<void *> proc{ nullptr };  // processus ffmpeg courant
    std::mutex         mutex;
    std::string        message, chapters;
    fs::path           output;
    std::thread        thread;
    bool               announced = false;  // rÃ©sultat dÃ©jÃ  signalÃ© Ã  l'utilisateur

    void stop();
    ~Job();
};

// ClÃ© d'un album : groupe + album, sans casse ni accents.
std::string album_key(const std::string & artist, const std::string & album);
// NumÃ©ro de piste d'un morceau (champ Â« piste Â» ou chiffres en tÃªte du titre) ; 0 si inconnu.
int track_number(const Song & s);
// Titre sans numÃ©ro de piste en tÃªte (Â« 01. Titre Â» -> Â« Titre Â»).
std::string clean_title(const std::string & title);
// Morceaux terminÃ©s d'un album, dans l'ordre des pistes.
std::vector<Song> album_songs(const std::vector<Song> & all, const std::string & key, bool validated_only);

// Copie les morceaux dans <dest>/<Groupe - Album>/ : Â« NN - Titre.ext Â», pochette intÃ©grÃ©e aux FLAC et MP3.
void start_export(const std::shared_ptr<Job> & job, const Paths & paths, std::vector<Song> songs, std::string artist,
                  std::string album, fs::path cover, fs::path dest_parent, bool flac, bool mp3, bool wav);
// Une vidÃ©o MP4 par titre et par rÃ©seau : image fixe + audio du morceau. width x height : 1920x1080 pour
// YouTube, 1080x1920 pour TikTok ; une image d'un autre format est centrÃ©e sur fond noir. Les vidÃ©os sont
// crÃ©Ã©es l'une aprÃ¨s l'autre, contrÃ´lÃ©es (lecture, vraie image) puis renommÃ©es.
struct VideoTask {
    Song     song;
    fs::path image, output;
    int      width = 1920, height = 1080;
};
void start_videos(const std::shared_ptr<Job> & job, const Paths & paths, std::vector<VideoTask> tasks);
// 1.0.52 : clip animÃ© (plans de camÃ©ra lents, fondus, paroles synchronisÃ©es) au lieu de l'image fixe. MÃªmes dossiers et
// mÃªmes formats que les vidÃ©os ; les paroles sont calÃ©es avec Whisper (whisper_exe/whisper_model ; sans lui, rÃ©parties
// sur la durÃ©e) puis le montage est fait par le seul ffmpeg du Studio (voir clip.hpp).
// 1.0.53 : images du clip (une par scÃ¨ne) et seconde oÃ¹ chacune apparaÃ®t.
struct ClipScenes {
    std::vector<fs::path> images;
    std::vector<double>   starts;
};
// 1.0.61 : serveur d'images commun Ã  tous les morceaux d'une mÃªme crÃ©ation de clips (partagÃ© par les copies de SceneEngine).
struct ImageServerSlot {
    std::shared_ptr<LocalImageServer> server;
    bool                              failed = false;  // pas de serveur : une image Ã  la fois avec sd-cli
};
// Moteurs du scÃ©nario (IA locale de texte, puis modÃ¨le d'images local) : prÃ©parÃ©s par l'application.
struct SceneEngine {
    std::shared_ptr<ImageServerSlot> images = std::make_shared<ImageServerSlot>();
    LocalLlm                llm;
    fs::path                llm_model;
    int                     llm_layers = 0;
    fs::path                sd_cli, image_root;
    const LocalImageModel * image_model = nullptr;
    bool                    offload = false;
    int                     scene_count = 0;  // nombre d'images demandÃ© ; 0 = automatique (6 Ã  12 selon les paroles)
    std::map<std::string, int> counts;        // nombre d'images propre Ã  chaque morceau (id) : prime sur scene_count
    bool                    same_seed = false;  // 1.0.98 : même graine pour toutes les images d'un morceau (tirée de son id)
    std::string             style_id = "auto", style_prompt;  // type de rendu choisi (voir clip_styles) ; prompt vide = auto
};
// Dossier du scÃ©nario et des images d'un morceau pour le type de rendu et le moteur d'images choisis (<morceau>\clip_scenes\â€¦).
fs::path clip_scenes_dir(const SceneEngine & eng, const Song & song);
// ScÃ©nario d'aprÃ¨s les paroles et les fiches (morceau, album, groupe), puis une image par scÃ¨ne dans <morceau>\clip_scenes
// (reprise possible : scÃ©nario et images dÃ©jÃ  faits sont gardÃ©s). Faux si le scÃ©nario ou les images Ã©chouent (note = motif).
bool make_clip_scenes(const SceneEngine & eng, Job & job, const Song & song, const std::vector<ClipLine> & lines, double seconds,
                      ClipScenes & out, std::string & note, bool scenario_only = false);
struct ClipTask : VideoTask {
    fs::path    whisper_exe, whisper_model;
    std::string text_start, text_end;  // 1.0.54 : textes personnalisÃ©s de la carte de dÃ©but et de la carte de fin (voir clip.hpp)
    bool        karaoke = false;        // 1.0.98 : paroles défilantes, lues dans paroles.lrc si elles existent
    int         end_pos = 0;            // 1.0.98 : texte de fin au centre (0) ou en bas (1)
    bool        end_box = false;        // 1.0.98 : bandeau sombre derrière le texte de fin
    double      short_seconds = 0;      // 1.0.74 : > 0 = Short : extrait de cette durÃ©e centrÃ© sur le refrain (clip_short_start)
    // ScÃ©nario d'aprÃ¨s les paroles : rempli par l'application (IA locale), appelÃ© une fois par titre aprÃ¨s le calage des
    // paroles. Faux ou aucune image : le clip utilise l'image du clip (la pochette) ; note = motif, affichÃ© Ã  la fin.
    std::function<bool(Job &, const Song &, const std::vector<ClipLine> &, double seconds, ClipScenes &, std::string & note)> make_scenes;
    // 1.0.61 : scÃ©nario seul, fait pour tous les morceaux avant la premiÃ¨re image (l'IA de texte et le modÃ¨le d'images ne tiennent
    // pas ensemble en mÃ©moire vidÃ©o) ; le modÃ¨le d'images reste ensuite chargÃ© jusqu'Ã  la derniÃ¨re image.
    std::function<bool(Job &, const Song &, const std::vector<ClipLine> &, double seconds, ClipScenes &, std::string & note)> make_scenario;
};
void start_clips(const std::shared_ptr<Job> & job, const Paths & paths, std::vector<ClipTask> tasks);
// Titre sans les suffixes Â« (re-rendu) Â» / Â« (recomposÃ©) Â», numÃ©ro de piste conservÃ© (noms des vidÃ©os).
std::string display_title(const std::string & title);

fs::path pick_folder(void * owner, const wchar_t * title);

// 1.0.83 : contrÃ´le qualitÃ© sans rÃ©Ã©coute. Pour chaque morceau : durÃ©e loin de la cible, paroles peu entendues par Whisper,
// silence anormal au milieu, derniÃ¨re partie d'un morceau long trop courte. Rapport Ã©crit dans report ; found = identifiant
// et problÃ¨mes de chaque morceau Ã  refaire (lu par l'application quand la tÃ¢che est finie).
struct QualityFinding {
    std::string              id, label;
    std::vector<std::string> problems;
};
void start_quality(const std::shared_ptr<Job> & job, std::vector<Song> songs, fs::path whisper_exe, fs::path whisper_model, fs::path report,
                   std::shared_ptr<std::vector<QualityFinding>> found);

// 1.0.57 : Ã©crit Â« description.txt Â» pour le groupe, l'album et chaque morceau de songs (mÃªme album) avec l'IA locale (ask), en
// langue lang. Un fichier dÃ©jÃ  prÃ©sent n'est jamais remplacÃ©. say : appelÃ© Ã  chaque Ã©lÃ©ment. Interrompu par cancel.
void write_descriptions(const std::vector<Song> & songs, const std::string & lang, const AiAsk & ask,
                        const std::function<void(const std::string &)> & say, std::atomic<bool> & cancel, std::atomic<int> & written, std::string & error);

// ---- 1.0.97 : outils audio (mastering, paroles synchronisÃ©es, livret, export, publication, doublons, essais, stems)
// Mastering : loudnorm en deux passes (volume cible en LUFS, crÃªte vraie max tp) ; Ã©crit <morceau>\master\<titre>_master.wav (+ .mp3).
void start_master(const std::shared_ptr<Job> & job, std::vector<Song> songs, double lufs, double tp, bool mp3);
// Paroles synchronisÃ©es : Whisper mot par mot, calage sur le texte, <morceau>\paroles.lrc et paroles.srt.
void start_lrc(const std::shared_ptr<Job> & job, std::vector<Song> songs, fs::path whisper_exe, fs::path whisper_model);
// Livret d'album : livret.html puis livret.pdf (Edge sans fenÃªtre) dans dest ; songs = morceaux dans l'ordre des pistes.
void start_booklet(const std::shared_ptr<Job> & job, std::vector<Song> songs, std::string artist, std::string album, fs::path cover, fs::path dest);
// Export pour un logiciel de musique : audio (master s'il existe) nommÃ© Â« Titre - 120 BPM - Em Â», partition .abc, paroles, stems.
void start_daw_export(const std::shared_ptr<Job> & job, std::vector<Song> songs, fs::path dest);
// Fiche de publication Â« publication.txt Â» (titre, description, hashtags) par morceau, avec l'IA (ask). Jamais remplacÃ©e.
void write_publication(const std::vector<Song> & songs, const std::string & lang, const AiAsk & ask, const std::function<void(const std::string &)> & say,
                       std::atomic<bool> & cancel, std::atomic<int> & written, std::string & error);
// Doublons : compositions (jetons sÃ©mantiques) ou paroles trÃ¨s proches entre deux morceaux terminÃ©s.
struct DupPair {
    std::string a, b, label_a, label_b, why;
    double      tokens = 0, lyrics = 0;
};
void start_duplicates(const std::shared_ptr<Job> & job, std::vector<Song> songs, std::shared_ptr<std::vector<DupPair>> found, fs::path report);
// Meilleur essai : contrÃ´le rapide (durÃ©e, silences) de plusieurs essais d'un mÃªme morceau ; renvoie l'indice du meilleur et les dÃ©fauts de chacun.
int best_trial(const std::vector<Song> & trials, std::vector<std::string> & problems);
// SÃ©paration des pistes (voix, batterie, basse, autres) : <morceau>\stems\*.wav, avec demucs.cpp (exe + modÃ¨le).
void start_stems(const std::shared_ptr<Job> & job, std::vector<Song> songs, fs::path demucs_exe, fs::path demucs_model);
