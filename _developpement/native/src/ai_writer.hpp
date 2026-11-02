// Composer avec l'IA (1.0.26) : une IA de langage écrit un morceau complet (titre, style, langue, paroles avec
// leur structure) à partir d'un petit formulaire, et le Studio le range dans « Nouveau morceau ».
//
// Trois sortes de moteurs :
//  - le moteur intégré : llama.cpp (llama-server.exe) lancé par le Studio avec un modèle GGUF téléchargé depuis
//    Hugging Face (Réglages › IA). Il n'est utilisé qu'au repos (aucune génération de morceau en cours) ;
//  - un serveur local déjà installé (Ollama, LM Studio, llama-server, KoboldCpp, Jan…), détecté sur ses ports
//    habituels : tous parlent le même langage (« compatible OpenAI ») ;
//  - un service en ligne avec une clé d'API (OpenAI, Claude, Gemini, Mistral, OpenRouter, DeepSeek, Groq…).
//
// La réponse de l'IA est demandée en JSON, puis nettoyée : balises de section ramenées à celles de YuE2, sans
// description ni numéro, rien sous les balises instrumentales (Intro, Interlude, Outro), pas d'indications de jeu.
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------- Moteurs

enum AiKind : int {
    AI_OPENAI    = 0,  // service ou serveur « compatible OpenAI » (/v1/chat/completions)
    AI_ANTHROPIC = 1,  // Claude (format Anthropic : /v1/messages)
};

// Service en ligne proposé dans Réglages › IA.
struct AiService {
    const char * id;        // identifiant enregistré dans les réglages (« openai », « anthropic »…)
    const char * name;      // nom affiché
    int          kind;      // AiKind
    const char * base_url;  // adresse de base de l'API (sans « /chat/completions ») ; "" = à saisir (service personnalisé)
    const char * model;     // modèle proposé par défaut (modifiable, et « Lister les modèles » interroge le service)
    const char * key_page;  // page où créer une clé d'API
};
const std::vector<AiService> & ai_services();
const AiService *              find_ai_service(const std::string & id);

// Serveur local connu : nom et port habituel.
struct AiServerKind {
    const char * name;
    int          port;
};
const std::vector<AiServerKind> & ai_server_kinds();

// Serveur local trouvé en fonctionnement.
struct AiServer {
    std::string              name;  // « Ollama », « LM Studio »…
    std::string              url;   // « http://127.0.0.1:11434/v1 »
    std::vector<std::string> models;
};
// Interroge les ports habituels (quelques dixièmes de seconde par port absent). Bloquant : à lancer dans un fil.
std::vector<AiServer> ai_detect_servers();

// Point d'accès complet d'une requête.
struct AiEndpoint {
    int         kind = AI_OPENAI;
    std::string base_url;  // « https://api.openai.com/v1 », « http://127.0.0.1:11434/v1 »…
    std::string key;       // clé d'API en clair (vide pour un serveur local)
    std::string model;
};

struct AiCallResult {
    bool        ok = false;
    std::string text;   // réponse de l'IA (texte brut)
    std::string error;  // message d'erreur en français (passé à tr() par l'interface), %s éventuel dans error_arg
    std::string error_arg;
};

// Envoie une conversation (consigne système + demande) et renvoie le texte de la réponse. Bloquant.
AiCallResult ai_chat(const AiEndpoint & ep, const std::string & system, const std::string & user, int timeout_ms);

// Liste des modèles proposés par un service ou un serveur. Bloquant.
std::vector<std::string> ai_list_models(const AiEndpoint & ep, std::string & error);

// ---------------------------------------------------------------------------- Moteur intégré (llama.cpp)

// Modèle GGUF du catalogue (Réglages › IA), téléchargé dans <models>\ia.
struct AiModelFile {
    const char *       id;
    const char *       name;     // nom affiché
    const char *       repo;     // dépôt Hugging Face
    const char *       file;     // nom du fichier GGUF
    unsigned long long bytes;    // taille exacte (vérifiée le 23/10/2026)
    const char *       note;     // points forts (texte français, traduit par l'interface)
    int                quality;  // 1 à 5 : qualité d'écriture attendue (paroles, langues autres que l'anglais)
};
const std::vector<AiModelFile> & ai_model_catalog();
std::string                      ai_model_url(const AiModelFile & m);

// Adaptation d'un modèle au PC : entièrement sur la carte graphique (rapide), partagé entre la carte et le
// processeur (1.0.28 : modèle plus gros que la mémoire vidéo), sur le processeur seul (PC sans carte) ou trop lourd.
enum AiFit : int { FIT_GPU = 0, FIT_CPU = 1, FIT_TOO_BIG = 2, FIT_MIXED = 3 };
AiFit ai_model_fit(unsigned long long bytes, unsigned long long vram, unsigned long long ram, bool gpu_engine);
// Nombre de couches du modèle confiées à la carte graphique (option -ngl de llama.cpp) : 99 = tout le modèle,
// moins si le modèle dépasse la mémoire vidéo (le reste tourne sur le processeur), 0 sans carte.
int ai_gpu_layers(unsigned long long bytes, unsigned long long vram);

// llama.cpp prêt à servir : llama-server.exe trouvé, avec ou sans prise en charge de la carte graphique (Vulkan).
struct LocalLlm {
    fs::path server;          // llama-server.exe ; vide = absent
    bool     vulkan = false;  // ggml-vulkan.dll à côté : la carte graphique peut servir
    bool     ready() const { return !server.empty(); }
};
// Cherche d'abord la version Vulkan téléchargée par le Studio (<app>\outils\llama-vulkan, ou l'ancien <app>\ia\llama), puis la
// version processeur (<app>\outils\llama, ou l'ancien <app>\traduction).
LocalLlm find_local_llm(const fs::path & app_dir);

// Version Vulkan de llama.cpp proposée au téléchargement (figée : version testée).
struct LlamaPackage {
    const char *       tag;   // « b11829 »
    const char *       url;   // archive .zip
    const char *       file;  // nom de l'archive
    unsigned long long bytes;
};
const LlamaPackage & llama_vulkan_package();
// Décompresse l'archive téléchargée (tar.exe de Windows) dans son dossier. Bloquant. Renvoie vrai si llama-server.exe est là.
bool llama_extract(const fs::path & zip, const fs::path & dir, std::string & error);

// Lance llama-server avec le modèle, attend qu'il soit prêt, envoie la conversation, puis arrête le serveur
// (la carte graphique redevient libre pour les morceaux). stage : 0 chargement du modèle, 1 écriture.
// cancel : arrêt demandé (le serveur est arrêté, la requête échoue). Bloquant.
// gpu_layers : couches sur la carte graphique (voir ai_gpu_layers ; 0 = processeur seul).
AiCallResult ai_local_chat(const LocalLlm & llm, const fs::path & model, int gpu_layers, const std::string & system,
                           const std::string & user, std::atomic<int> * stage, std::atomic<bool> * cancel);

// Session du moteur intégré (1.0.33, album) : le modèle n'est chargé qu'une fois, puis work() envoie autant de
// demandes qu'il veut avec ask(consigne, demande) ; le serveur est arrêté à la fin (ou dès cancel).
// Renvoie un résultat en échec si le moteur n'a pas pu démarrer ; sinon ok = vrai (les erreurs des demandes sont
// celles renvoyées par ask).
using AiAsk = std::function<AiCallResult(const std::string & system, const std::string & user)>;
AiCallResult ai_local_session(const LocalLlm & llm, const fs::path & model, int gpu_layers, std::atomic<int> * stage,
                              std::atomic<bool> * cancel, const std::function<void(const AiAsk &)> & work);

// ---------------------------------------------------------------------------- Demande et réponse

struct AiRequest {
    std::string language;      // langue des paroles, en anglais (« French ») ; vide = anglais
    std::string language_tag;  // balise de langue du style YuE2 (« French »…) ; vide = aucune
    std::string influence;     // groupe ou chanteur
    std::string theme;         // thème (facultatif)
    std::string mood;          // ambiance (facultatif)
    std::string title;         // titre imposé (facultatif)
    std::string extra;         // souhaits libres (facultatif)
    bool        instrumental = false;
    int         length       = 1;  // 0 court, 1 normal, 2 long, 3 très long (1.0.70 : 3 parties)
    // Anti-répétition (1.0.33) : mots à ne pas employer, titres déjà pris, et pour un album la place du morceau.
    std::vector<std::string> avoid;
    std::vector<std::string> used_titles;
    std::string              album_context;  // phrase en anglais (album, groupe, son commun, tempo, ambiance)
    std::string              negative;       // 1.0.34 : styles à éviter (balises séparées par des virgules)
};

// Phrase résumée affichée dans l'onglet (« Créer un morceau en français inspiré par … sur le thème … »),
// construite par l'interface ; ici la consigne complète envoyée à l'IA (en anglais : mieux suivie par tous les modèles).
std::string ai_system_prompt(bool instrumental);
std::string ai_user_prompt(const AiRequest & r);
// Rappel ajouté lors du second essai quand la première réponse était illisible.
std::string ai_retry_prompt();

struct AiSong {
    bool        ok = false;
    std::string title, language, style, lyrics;
    int         removed = 0;  // descriptions, indications ou lignes parasites retirées par le nettoyage
};
// Lit la réponse (JSON, même entouré de texte ou de ```), puis nettoie le style et les paroles.
AiSong ai_parse_song(const std::string & text, bool instrumental);
// Nettoyage seul (sert aussi au bouton « Nettoyer » sur une réponse collée à la main).
std::string ai_clean_lyrics(const std::string & lyrics, bool instrumental, int & removed);
std::string ai_clean_style(const std::string & style, int & removed);

// ---------------------------------------------------------------------------- Anti-répétition (1.0.33)

// Liste de départ des mots à éviter (clichés fréquents des paroles écrites par IA, en français et en anglais).
std::vector<std::string> ai_default_avoid_words();
// Mots de la liste présents dans le texte : mots entiers, sans tenir compte des majuscules ni des accents ;
// « rêve » trouve aussi « rêves » ou « Rêve ».
std::vector<std::string> ai_forbidden_found(const std::string & text, const std::vector<std::string> & avoid);
// Mots qui reviennent dans au moins min_songs des textes donnés (derniers morceaux écrits par l'IA), les plus
// fréquents d'abord, au plus max_words ; mots courts et mots-outils ignorés.
std::vector<std::string> ai_frequent_words(const std::vector<std::string> & texts, int min_songs, size_t max_words);
// Demande de réécriture quand le morceau contient des mots interdits.
std::string ai_avoid_retry_prompt(const std::vector<std::string> & found);
int         ai_sung_lines(const std::string & lyrics);
void        ai_lengthen_song(AiSong & song, int target, const std::vector<std::string> & avoid, const AiAsk & ask, const std::atomic<bool> * cancel);
constexpr int AI_LONG_LINES = 110;  // « Très longue » : mesuré 94 lignes = 8 min 01 en 3 parties, soit ≈ 5,1 s par ligne
// 1.0.37 : titre encore fautif après la relance → demande d'un titre seul, et sa lecture (JSON {"title"} ou texte).
std::string ai_title_retry_prompt(const std::string & old_title, const std::vector<std::string> & found, const std::vector<std::string> & avoid);
std::string ai_parse_title(const std::string & text);
// Retire les doublons d'une liste de mots (même comparaison que ai_forbidden_found : accents, majuscules et
// pluriels ignorés) ; garde la première écriture. removed : nombre de mots retirés.
std::vector<std::string> ai_dedupe_words(const std::vector<std::string> & words, int & removed);

// ---------------------------------------------------------------------------- Album (1.0.33)

struct AiAlbumRequest {
    std::string              language = "English";  // langue des chansons, en anglais
    std::string              influence, theme, mood, extra;
    std::string              band, album;           // imposés ; vides = l'IA choisit
    int                      count         = 10;    // 1 à 50 morceaux
    int                      kind          = 0;     // 0 chansons, 1 instrumentaux, 2 mélangé
    int                      instrumentals = 0;     // mélangé : nombre d'instrumentaux (0 = l'IA répartit)
    int                      max_instrumentals = 2; // mélangé : maximum par album (1.0.35, Réglages › IA)
    std::vector<std::string> avoid;
    std::string              negative;              // 1.0.34 : styles à éviter
    // 1.0.47 : compléter un album ou continuer un groupe existant
    std::vector<std::string> used_titles;           // titres déjà faits (album ou groupe) : jamais repris
    std::vector<std::string> used_albums;           // albums déjà sortis par le groupe (nouvel album : autre titre)
    std::string              sound;                 // son commun imposé (balises), vide = l'IA le choisit
    bool                     complete = false;      // vrai : ajout de morceaux à l'album « album » existant
};
struct AiTrackPlan {
    std::string title, theme, mood;
    int         bpm          = 0;
    bool        instrumental = false;
};
struct AiAlbumPlan {
    bool                     ok = false;
    std::string              band, album, sound;  // sound : balises du son commun de l'album
    std::string              cover_prompt;        // 1.0.36 : description de la pochette (anglais, sans texte)
    std::vector<AiTrackPlan> tracks;              // toujours r.count pistes quand ok
};
std::string ai_album_system_prompt();
std::string ai_album_user_prompt(const AiAlbumRequest & r);
// Lit le plan (JSON tolérant) puis le complète : nom de groupe et titre imposés, nombre de pistes exact, répartition
// des instrumentaux, titres en double ou contenant un mot interdit remplacés par « Piste N » (l'IA en choisira un).
AiAlbumPlan ai_parse_album_plan(const std::string & text, const AiAlbumRequest & r);
// Phrase « album_context » d'une piste, pour ai_user_prompt.
std::string ai_track_context(const AiAlbumPlan & plan, int index);
// 1.0.34 : mots interdits dans le nom du groupe et le titre de l'album choisis par l'IA (jamais dans ceux que
// l'utilisateur a tapés), et demande d'un nouveau plan avec d'autres noms.
std::vector<std::string> ai_plan_forbidden(const AiAlbumPlan & plan, const AiAlbumRequest & r);
std::string              ai_plan_retry_prompt(const std::vector<std::string> & found);
// Balises du style qui figurent parmi les styles à éviter, retirées (comparaison sans majuscules).
std::string ai_strip_negative(const std::string & style, const std::string & negative);
// 1.0.37, instrumentaux sans voix, quoi que réponde l'IA : style sans aucune balise de voix (vocal, singer, choir,
// rap…) avec « instrumental, no vocals » ; styles à éviter complétés de vocals, singing, voice, choir, humming,
// spoken word, rap, lyrics.
std::string ai_force_instrumental_style(const std::string & style);
std::string ai_instrumental_negative(const std::string & negative);
bool        ai_is_voice_tag(const std::string & tag);  // « no vocals », « without vocals » : pas une voix

// ---------------------------------------------------------------------------- Pochettes (1.0.36)
// Images d'album générées en local (stable-diffusion.cpp Vulkan, sd-cli.exe dans <app>\outils\sd-vulkan, modèle
// Z-Image-Turbo Q4_K dans <models>\images, téléchargé au clic) ou par un service en ligne compatible OpenAI
// (POST {base}/images/generations, mêmes clés d'API que l'écriture des paroles).

struct ImageModelFile {
    const char *       repo;   // dépôt Hugging Face
    const char *       path;   // chemin dans le dépôt
    const char *       file;   // nom du fichier dans le dossier du modèle
    unsigned long long bytes;  // taille exacte
};
// 1.0.50 : trois modèles locaux au choix (Réglages › IA › Pochettes), chacun dans <models>\images\<subdir> (Z-Image Turbo
// reste directement dans <models>\images, comme avant). files : [0] modèle d'image, [1] encodeur de texte, [2] VAE,
// [3] tokenizer facultatif. Mesures : banc d'essai du 28/10/2026 sur Radeon RX 6700 XT 12 Go, 768 × 768.
struct LocalImageModel {
    const char *                id;      // « klein », « zimage », « moss », « sdxl »
    const char *                name;    // nom affiché
    const char *                subdir;  // sous-dossier de <models>\images ("" = le dossier lui-même)
    std::vector<ImageModelFile> files;
    int                         steps;
    float                       cfg;
    int                         seconds;  // temps mesuré par image 768 × 768
    float                       vram_gb;  // mémoire vidéo mesurée
};
const std::vector<LocalImageModel> & local_image_models();
const LocalImageModel &              local_image_model(const std::string & id);  // inconnu : FLUX.2 klein
fs::path                             local_image_dir(const fs::path & images_root, const LocalImageModel & m);
unsigned long long                   local_image_bytes(const LocalImageModel & m);
std::string                          image_model_url(const ImageModelFile & f);
bool                                 image_model_ready(const fs::path & images_root, const LocalImageModel & m);  // fichiers complets
fs::path                             find_sd_cli(const fs::path & app_dir);   // vide si absent

struct ImageService {
    const char * id;        // « local », « openai », « xai », « together », « custom »
    const char * name;      // nom affiché
    const char * base_url;  // "" pour local et custom (adresse perso de Réglages › IA)
    const char * model;     // modèle proposé
    const char * key_id;    // service dont la clé d'API est reprise (ai_keys)
};
const std::vector<ImageService> & image_services();
const ImageService *              find_image_service(const std::string & id);

// Image locale (bloquant, plusieurs dizaines de secondes) : PNG écrit dans out_png. r.text = chemin écrit.
// offload : poids gardés en mémoire vive (--offload-to-cpu), pour MOSS-Image sur les cartes de 12 Go ou moins.
// 1.0.61 : modèle d'images gardé chargé par sd-server (à côté de sd-cli) pendant une série d'images : 9,3 s par image SDXL en
// 1344 × 768 au lieu de 13,4 s, le modèle n'étant ni rechargé ni ses calculs refaits à chaque image. Arrêté à la destruction.
class LocalImageServer {
public:
    LocalImageServer(const fs::path & sd_cli, const fs::path & images_root, const LocalImageModel & model, bool offload);
    ~LocalImageServer();
    bool         start(std::atomic<bool> * cancel);  // faux : pas de serveur (l'appelant se rabat sur ai_image_local)
    AiCallResult generate(const std::string & prompt, int width, int height, const fs::path & out_png, std::atomic<bool> * cancel);

private:
    fs::path                sd_cli_, root_;
    const LocalImageModel & model_;
    bool                    offload_;
    void *                  process_ = nullptr;
};
AiCallResult ai_image_local(const fs::path & sd_cli, const fs::path & images_root, const LocalImageModel & m, const std::string & prompt,
                            int width, int height, const fs::path & out_png, std::atomic<bool> * cancel, bool offload = false);
// Image en ligne (bloquant) : out_base sans extension ; l'extension suit le format reçu (.png, .jpg, .webp). r.text = chemin.
AiCallResult ai_image_online(const std::string & service, const AiEndpoint & ep, const std::string & prompt, int width, int height,
                             const fs::path & out_base);

// ---------------------------------------------------------------------------- Clés d'API

// Chiffre une clé pour l'utilisateur Windows courant (DPAPI) et la rend lisible en texte (base64) ; inverse.
// La clé n'est jamais écrite en clair dans les réglages, et un autre compte Windows ne peut pas la relire.
std::string ai_protect_key(const std::string & key);
std::string ai_unprotect_key(const std::string & stored);
