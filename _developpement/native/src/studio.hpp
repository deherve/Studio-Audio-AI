// Studio Audio AI natif : bibliothèque, file de générations et pilotage de yue-synth.exe.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// Solveur de la phase audio (NAR) : un réglage = méthode d'intégration + nombre de pas.
// Liste classée du plus rapide au plus lent (évaluations du réseau : 17, 21, 25, 32, 33, 33, 64).
struct SolverSpec {
    const char * key;     // valeur mémorisée (réglages, fiche du morceau)
    const char * label;   // nom affiché (« AB3 24 »)
    const char * engine;  // nom du solveur pour yue-synth (--solver)
    int          steps;   // pas de l'intégration (--steps)
    int          evals;   // évaluations du réseau pour ces pas
    const char * note;    // description courte
};
const std::vector<SolverSpec> & solver_specs();
// Réglage d'une clé ; accepte les anciennes valeurs (« midpoint » ou vide = MP 32, « ab2 » = AB2 32) ; clé inconnue = AB2 32.
const SolverSpec & solver_spec(const std::string & key);

// Même schéma que meta.json de la version web (app/server.py) : les deux partagent bibliotheque/.
// Les champs propres à l'application native sont ignorés par la version web.
struct Song {
    std::string id, title, style, lyrics, cot = "full";
    bool        instrumental = false, instrumental_lora = true;
    int64_t     lm_seed = -1, seed = -1;
    double      duration = 180;
    std::string status = "queued", stage = "En attente", message;
    double      progress = 0, created = 0, started = 0, finished = 0, audio_seconds = 0;  // 0 = non défini
    bool        has_used_seeds = false;
    int64_t     used_lm_seed = 0, used_seed = 0;
    bool        local = false;  // mis en file par cette instance

    // Origine : "new" (création), "rerender" (même composition, nouvelle synthèse),
    // "extend" (suite d'un morceau), "cover" (reprise d'un fichier audio dans un autre style).
    std::string kind = "new", parent, parent_title, source_file;
    std::vector<std::pair<std::string, double>> adapters;  // LoRA supplémentaires : fichier de models/adapters et force
    double      fade_seconds = 0;  // fondu de fin d'un morceau coupé au plafond : 0 = réglage général, sinon durée propre au morceau (fin sur la structure)
    std::string lyrics_lang;          // langue des paroles (« fr », « en »…) ; vide = morceau instrumental ou langue inconnue
    std::string heard_vocals_lang;      // morceau marqué instrumental où Whisper entend des paroles : langue entendue (vide : non)
    int         lyrics_lang_check = 0;  // vérification à l'écoute (Whisper) : 0 jamais, 1 non concluante, 2 langue confirmée ou corrigée
    bool        lyrics_lang_declared = false;  // vrai si elle vient de la balise de langue du style, faux si détectée d'après le texte
    // Balises négatives (expérimental) : style à éviter (« vocoder, autotune ») et force du guidage (1,2 à 3).
    // Vide = aucun guidage, composition à vitesse normale ; rempli = phase de composition environ 2 fois plus lente.
    std::string negative_style;
    double      negative_scale = 1.5;
    // 1.0.88 : partition fournie (texte ABC) : le modèle la suit au lieu d'écrire la sienne (onglet « Partition »)
    std::string abc;
    std::string solver;  // solveur de la phase NAR : « midpoint » (référence) ou « ab2 » (2x plus rapide) ; vide = midpoint
    std::string model;  // fichier GGUF du modèle utilisé (« YuE2-3B-Q6_K.gguf ») ; vide = Q8_0 d'origine
    std::string artist, album, track;  // groupe, album et numéro de piste (métadonnées des fichiers)
    double      eta = 0, eta_at = 0;  // temps restant estimé, et instant de l'estimation
    // Durée automatique : le plafond laisse le modèle conclure ; expected_seconds sert à l'estimation.
    bool        auto_duration = false, truncated = false;
    double      expected_seconds = 0;
    // 1.0.48 (prototype « morceau long ») : long_rest = paroles de la partie 2, sur la partie 1 (lancée seule à sa fin) ;
    // long_join = secondes de la fin de la partie 1 reprises au début de la partie 2 (> 0 : assemblage après génération).
    std::string long_rest;
    double      long_join = 0;
    int         long_retry = 0;  // 1.0.70 : partie refaite car le modèle a conclu trop tôt (non enregistré)

    // Mesures de la génération, pour apprendre l'estimation du temps restant.
    int    score_tokens = 0, sem_tokens = 0, frames = 0;
    double score_s = 0, sem_s = 0, nar_step_s = 0;

    bool held = false;  // en file mais en pause : le moteur passe au suivant
    bool heard = true;       // morceau déjà écouté ; faux pour un morceau neuf, en attente d'écoute et de vérification
    bool validated = false;  // morceau validé par l'utilisateur (pouce vert)
    bool flagged = false;    // morceau à problème, à analyser (pouce rouge) ; exclusif avec validated

    bool active() const { return status == "queued" || status == "running"; }
    bool waiting() const { return status == "queued" && !held; }
};

// Résultat du test de santé de la carte graphique.
struct HealthResult {
    int         state = 0;  // 0 jamais lancé, 1 en cours, 2 stable, 3 instable, 4 erreur
    std::string message;
    double      error = 0;  // écart relatif entre deux rendus identiques (0,01 = 1 %)
    double      when  = 0;
};

struct Paths {
    fs::path root, synth, transcribe, whisper, model, vae, transcriber, adapters, library, models_dir, gguf_dir;
    // Données de l'utilisateur (1.0.30) : <root>\donnees (réglages, brouillon, studio_modeles.txt, pochettes, sante, temp).
    fs::path data;
    // 1.0.41 : <root>\config (fichiers de l'utilisateur à sauvegarder : reglages.json, styles.json, mots_a_eviter.json,
    // cles_api.json, dernier_album.json) et <root>\cache (temp, sante, brouillon : jetables).
    fs::path config, cache;
    // Moteur CUDA facultatif (cartes Nvidia) : dossier « moteur_cuda » à côté de l'application, avec son propre
    // yue-synth.exe, yue-transcribe.exe et ses DLL (ggml-cuda, cudart, cublas). Vide s'il n'est pas installé.
    fs::path cuda_dir;
    // Bascule synth et transcribe sur le moteur CUDA (appelé au démarrage si les réglages le demandent).
    bool use_cuda_engine();
    // Fichiers YuE2-3B-*.gguf disponibles (Q8_0, Q6_K, Q5_K_M, BF16…), triés du plus lourd au plus léger.
    std::vector<std::string> list_models() const;
    // Chemin du modèle demandé ; le modèle par défaut si le nom est vide ou introuvable.
    fs::path model_for(const std::string & name) const;
    // Moteur yue2.cpp (dans un dossier parent de l'exécutable, ou à côté) ; renseigne root et library.
    // shared_root : outil de développement (Créateur de styles) qui prend le dossier où il trouve le moteur comme
    // dossier de l'application ; le Studio, lui, garde toujours SON dossier (models, donnees, bibliotheque : 1.0.32).
    static bool locate(const fs::path & exe_dir, Paths & out, bool shared_root = false);
    // Modèles : dossier contenant les .gguf (directement ou dans un sous-dossier « gguf »), adapters/ à côté.
    // Renvoie false si YuE2-3B et le VAE n'y sont pas.
    bool use_models(const fs::path & dir);
};

// Modèle de temps appris sur l'historique (valeurs par défaut mesurées sur RX 6700 XT).
struct EtaModel {
    double step_a = 0.10, step_b = 0.34;  // secondes par étape de synthèse : a·x + b·x², x = trames / 1000
    double score_rate = 110, sem_rate = 115;  // tokens par seconde
    double fill_song = 0.85, fill_inst = 1.0;  // part de la durée maximale réellement composée

    double nar_step(double frames) const {
        double x = frames / 1000.0;
        return step_a * x + step_b * x * x;
    }
};

class Studio {
  public:
    Studio(Paths paths, std::function<void()> notify);
    ~Studio();

    std::vector<Song> snapshot();
    std::string       submit(Song song, bool held = false);
    void              cancel(const std::string & id);
    void              set_held(const std::string & id, bool held);  // pause / reprise ; en cours : arrêt puis pause
    void              set_heard(const std::string & id, bool heard);  // marque un morceau écouté (ou neuf)
    void              set_validated(const std::string & id, bool validated);
    void              set_flagged(const std::string & id, bool flagged);
    void              hold_all();                                    // tout en pause, GPU libéré
    void              resume_all();                                  // tout relancer
    void              relaunch(const std::string & id);              // morceau interrompu, en erreur ou annulé
    void              edit_metadata(const std::string & id, const std::string & title, const std::string & artist,
                                    const std::string & album, const std::string & track);
    void              shutdown();
    // Porte de la file : fermée (jeu en cours), plus aucune génération ne démarre et celle en cours est
    // remise en attente ; rouverte, la file reprend toute seule.
    void              set_gate(bool closed);
    bool              gate_closed();
    // Test de santé : rejoue deux fois le même court extrait et compare les résultats.
    void              start_health_check();
    HealthResult      health();  // fermeture : la génération en cours devient « interrompue »
    // Envoie le morceau à la corbeille de Windows (récupérable).
    bool              remove(const std::string & id, std::string & error);
    // Envoie tous les morceaux terminés à la corbeille ; renvoie leur nombre, ou -1 avec un message (génération en cours).
    int               remove_all(std::string & error);
    bool              remove_folder(const fs::path & dir, std::string & error);
    void              launch_long_part(const std::string & id);  // 1.0.48 : partie 2 d'un morceau long  // 1.0.46 : album entier à la corbeille
    void              rescan();  // recharge les morceaux gérés ailleurs (version web)
    bool              local_active();  // une génération tourne ici
    void              cancel_all_local();
    const Paths &     paths() const { return paths_; }
    bool              can_transcribe() const;
    // 1.0.88 : écrit seulement la partition d'un morceau (yue-plan, ≈ 20 s, carte graphique). Vide + error si échec.
    std::string       plan_score(const Song & song, std::string & error);
    // Langue des paroles établie à l'écoute (code vide = inchangée) ; check : voir Song::lyrics_lang_check.
    void              set_heard_vocals(const std::string & id, const std::string & lang);  // instrumental : voix entendue (lang) ou non (vide)
    void              set_language_check(const std::string & id, const std::string & code, int check);
    // Modèle appliqué aux prochains morceaux soumis (nom de fichier GGUF) ; pris à chaque lancement de yue-synth.
    void              set_active_model(const std::string & name);
    std::string       active_model();
    // Solveur appliqué aux prochains morceaux soumis (« midpoint » ou « ab2 »).
    void              set_active_solver(const std::string & name);
    std::string       active_solver();
    // Durée (s) du fondu de fin des morceaux coupés au plafond de durée, quand le morceau n'impose pas la sienne.
    void              set_fade_seconds(double seconds);

  private:
    void worker();
    void generate(const std::string & id);
    void update(const std::string & id, const std::function<void(Song &)> & fn);
    void save(const Song & song);
    void refit_model();
    bool pick_next(std::string & id);  // plus ancien morceau en file et pas en pause
    void finish_stopped(const std::string & id, const fs::path & wav);
    void run_health_check();

    Paths                                      paths_;
    std::function<void()>                      notify_;
    std::mutex                                 mutex_;
    std::condition_variable                    cv_;
    std::map<std::string, Song>                songs_;
    std::map<std::string, fs::file_time_type>  mtimes_;
    std::thread                                thread_;
    std::atomic<bool>                          stop_{ false };
    void *                                     process_ = nullptr;  // HANDLE du processus en cours
    std::string                                running_id_;
    bool                                       cancel_running_ = false;
    std::string                                stop_kind_;  // "cancelled", "paused", "gate" ou "interrupted"
    bool                                       gate_ = false, maintenance_ = false;
    HealthResult                               health_;
    std::string                                active_model_, active_solver_;
    double                                     fade_default_ = 5;
    std::thread                                health_thread_;
    EtaModel                                   model_;
};

// Utilitaires partagés avec l'interface.
// Lance une commande (stderr, ou stdout si read_stdout) ligne par ligne ; on_start reçoit le handle du
// processus (nullptr à la fin). Renvoie le code de sortie.
int          run_command(std::wstring command, const std::function<void(const std::string &)> & on_line,
                         const std::function<void(void *)> & on_start = nullptr, bool read_stdout = false);
std::wstring quote_arg(const std::wstring & arg);
std::wstring widen(const std::string & s);
std::string  narrow(const std::wstring & s);
std::string  safe_filename(const std::string & title);
double       now_seconds();
bool         other_synth_running();
double       media_duration(const fs::path & file);  // secondes, 0 si illisible (ffprobe)
// Durée probable d'un morceau d'après ses sections et ses paroles (secondes).
double       estimate_seconds(const std::string & lyrics, bool instrumental);
// 1.0.70 : dans long_rest, sépare les paroles de la partie suivante de celles de la dernière (morceau en 3 parties)
inline const char LONG_PART_MARK[] = "\n[[partie suivante]]\n";

// Dossiers de l'application (1.0.30) : tout reste dans le dossier du programme, où qu'il soit copié.
// Livrable : <app>\moteur, <app>\outils\{ffmpeg, whisper, llama, llama-vulkan}, <app>\ressources\{aide, langues},
// <app>\models, <app>\bibliotheque, <app>\donnees. Les anciens emplacements (racine, traduction\, ia\llama, whisper\,
// aide\, langues\, moteur_cuda\) restent reconnus : le dossier de développement et les installations précédentes marchent.
fs::path            app_data_dir();                         // <app>\donnees (vide avant Paths::locate)
fs::path            app_temp_dir();                         // <app>\cache\temp (créé, 1.0.41), sinon le dossier temporaire de Windows
fs::path            app_config_dir();                       // <app>\config (1.0.41 ; vide avant Paths::locate)
// 1.0.43 : morceaux rangés dans Audios\<Groupe>\<Album>\<NN. Titre>\ (sans album : Audios\<Groupe>\<Titre>\).
fs::path            song_folder(const std::string & id);    // dossier actuel d'un morceau
fs::path            album_folder(const std::string & artist, const std::string & album);  // album vide : dossier du groupe
void                merge_folder(const fs::path & from, const fs::path & to);             // sans écraser, avec journal
fs::path            app_resource_dir(const wchar_t * name);  // <app>\ressources\<name> s'il existe, sinon <app>\<name>
bool                inside_app_dir(const fs::path & p);     // vrai si p est dans le dossier du programme (1.0.32 : seules
                                                            // les suppressions de fichiers situés dedans sont permises)
const std::wstring & ffmpeg_exe();                          // chemin complet entre guillemets, ou « ffmpeg » (recherche de Windows)
const std::wstring & ffprobe_exe();
