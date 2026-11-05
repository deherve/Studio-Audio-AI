// Export d'albums (pochette intégrée, noms numérotés) et vidéos YouTube (pochette fixe + audio), via ffmpeg.
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

// Tâche de fond affichée dans l'interface (export, vidéo).
struct Job {
    std::string        name;
    std::atomic<int>   state{ 0 };  // 0 en cours, 1 terminée, 2 en erreur
    std::atomic<float> progress{ 0 };
    std::atomic<int>   item{ 0 }, total{ 0 };  // 1.0.54 : vidéo en cours / nombre de vidéos (affichés sur la barre de progression)
    std::atomic<bool>  cancel{ false };
    std::atomic<void *> proc{ nullptr };  // processus ffmpeg courant
    std::mutex         mutex;
    std::string        message, chapters;
    fs::path           output;
    std::thread        thread;
    bool               announced = false;  // résultat déjà signalé à l'utilisateur

    void stop();
    ~Job();
};

// Clé d'un album : groupe + album, sans casse ni accents.
std::string album_key(const std::string & artist, const std::string & album);
// Numéro de piste d'un morceau (champ « piste » ou chiffres en tête du titre) ; 0 si inconnu.
int track_number(const Song & s);
// Titre sans numéro de piste en tête (« 01. Titre » -> « Titre »).
std::string clean_title(const std::string & title);
// Morceaux terminés d'un album, dans l'ordre des pistes.
std::vector<Song> album_songs(const std::vector<Song> & all, const std::string & key, bool validated_only);

// Copie les morceaux dans <dest>/<Groupe - Album>/ : « NN - Titre.ext », pochette intégrée aux FLAC et MP3.
void start_export(const std::shared_ptr<Job> & job, const Paths & paths, std::vector<Song> songs, std::string artist,
                  std::string album, fs::path cover, fs::path dest_parent, bool flac, bool mp3, bool wav);
// Une vidéo MP4 par titre et par réseau : image fixe + audio du morceau. width x height : 1920x1080 pour
// YouTube, 1080x1920 pour TikTok ; une image d'un autre format est centrée sur fond noir. Les vidéos sont
// créées l'une après l'autre, contrôlées (lecture, vraie image) puis renommées.
struct VideoTask {
    Song     song;
    fs::path image, output;
    int      width = 1920, height = 1080;
};
void start_videos(const std::shared_ptr<Job> & job, const Paths & paths, std::vector<VideoTask> tasks);
// 1.0.52 : clip animé (plans de caméra lents, fondus, paroles synchronisées) au lieu de l'image fixe. Mêmes dossiers et
// mêmes formats que les vidéos ; les paroles sont calées avec Whisper (whisper_exe/whisper_model ; sans lui, réparties
// sur la durée) puis le montage est fait par le seul ffmpeg du Studio (voir clip.hpp).
// 1.0.53 : images du clip (une par scène) et seconde où chacune apparaît.
struct ClipScenes {
    std::vector<fs::path> images;
    std::vector<double>   starts;
};
// 1.0.61 : serveur d'images commun à tous les morceaux d'une même création de clips (partagé par les copies de SceneEngine).
struct ImageServerSlot {
    std::shared_ptr<LocalImageServer> server;
    bool                              failed = false;  // pas de serveur : une image à la fois avec sd-cli
};
// Moteurs du scénario (IA locale de texte, puis modèle d'images local) : préparés par l'application.
struct SceneEngine {
    std::shared_ptr<ImageServerSlot> images = std::make_shared<ImageServerSlot>();
    LocalLlm                llm;
    fs::path                llm_model;
    int                     llm_layers = 0;
    fs::path                sd_cli, image_root;
    const LocalImageModel * image_model = nullptr;
    bool                    offload = false;
    int                     scene_count = 0;  // nombre d'images demandé ; 0 = automatique (6 à 12 selon les paroles)
    std::map<std::string, int> counts;        // nombre d'images propre à chaque morceau (id) : prime sur scene_count
    std::string             style_id = "auto", style_prompt;  // type de rendu choisi (voir clip_styles) ; prompt vide = auto
};
// Dossier du scénario et des images d'un morceau pour le type de rendu et le moteur d'images choisis (<morceau>\clip_scenes\…).
fs::path clip_scenes_dir(const SceneEngine & eng, const Song & song);
// Scénario d'après les paroles et les fiches (morceau, album, groupe), puis une image par scène dans <morceau>\clip_scenes
// (reprise possible : scénario et images déjà faits sont gardés). Faux si le scénario ou les images échouent (note = motif).
bool make_clip_scenes(const SceneEngine & eng, Job & job, const Song & song, const std::vector<ClipLine> & lines, double seconds,
                      ClipScenes & out, std::string & note, bool scenario_only = false);
struct ClipTask : VideoTask {
    fs::path    whisper_exe, whisper_model;
    std::string text_start, text_end;  // 1.0.54 : textes personnalisés de la carte de début et de la carte de fin (voir clip.hpp)
    double      short_seconds = 0;      // 1.0.74 : > 0 = Short : extrait de cette durée centré sur le refrain (clip_short_start)
    // Scénario d'après les paroles : rempli par l'application (IA locale), appelé une fois par titre après le calage des
    // paroles. Faux ou aucune image : le clip utilise l'image du clip (la pochette) ; note = motif, affiché à la fin.
    std::function<bool(Job &, const Song &, const std::vector<ClipLine> &, double seconds, ClipScenes &, std::string & note)> make_scenes;
    // 1.0.61 : scénario seul, fait pour tous les morceaux avant la première image (l'IA de texte et le modèle d'images ne tiennent
    // pas ensemble en mémoire vidéo) ; le modèle d'images reste ensuite chargé jusqu'à la dernière image.
    std::function<bool(Job &, const Song &, const std::vector<ClipLine> &, double seconds, ClipScenes &, std::string & note)> make_scenario;
};
void start_clips(const std::shared_ptr<Job> & job, const Paths & paths, std::vector<ClipTask> tasks);
// Titre sans les suffixes « (re-rendu) » / « (recomposé) », numéro de piste conservé (noms des vidéos).
std::string display_title(const std::string & title);

fs::path pick_folder(void * owner, const wchar_t * title);

// 1.0.83 : contrôle qualité sans réécoute. Pour chaque morceau : durée loin de la cible, paroles peu entendues par Whisper,
// silence anormal au milieu, dernière partie d'un morceau long trop courte. Rapport écrit dans report ; found = identifiant
// et problèmes de chaque morceau à refaire (lu par l'application quand la tâche est finie).
struct QualityFinding {
    std::string              id, label;
    std::vector<std::string> problems;
};
void start_quality(const std::shared_ptr<Job> & job, std::vector<Song> songs, fs::path whisper_exe, fs::path whisper_model, fs::path report,
                   std::shared_ptr<std::vector<QualityFinding>> found);

// 1.0.57 : écrit « description.txt » pour le groupe, l'album et chaque morceau de songs (même album) avec l'IA locale (ask), en
// langue lang. Un fichier déjà présent n'est jamais remplacé. say : appelé à chaque élément. Interrompu par cancel.
void write_descriptions(const std::vector<Song> & songs, const std::string & lang, const AiAsk & ask,
                        const std::function<void(const std::string &)> & say, std::atomic<bool> & cancel, std::atomic<int> & written, std::string & error);
