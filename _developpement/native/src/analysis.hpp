// analysis : analyse d'un fichier audio (style, instruments, ambiance, voix, tempo, tonalité, structure)
// pour proposer des balises de style YuE2.
//
//  - modèles d'étiquetage Essentia (Discogs-EffNet + têtes de classification MTG-Jamendo / ambiance), au format ONNX,
//    exécutés avec ONNX Runtime (CPU) sur les spectrogrammes mel du fichier ;
//  - partition SheetSage2 (yue-transcribe) pour le tempo, la tonalité, la hauteur de la voix et la structure.
// Modèles : licence CC BY-NC-SA 4.0 (Music Technology Group, Universitat Pompeu Fabra), usage non commercial.
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct AnalysisFile {
    std::string        name;   // nom du fichier dans models/analysis
    std::string        url;
    unsigned long long bytes;
};
// Fichiers du modèle d'analyse (téléchargés dans models/analysis).
const std::vector<AnalysisFile> & analysis_files();
// Vrai si tous les fichiers sont présents (taille non nulle).
bool analysis_installed(const fs::path & dir);

// Modèle Whisper (whisper.cpp, GGML) pour la transcription des paroles ; fichier dans models/whisper.
const AnalysisFile & whisper_file();

struct LyricLine {
    double      start = 0, end = 0;  // secondes
    std::string text;
};

struct TagScore {
    std::string name;
    float       score = 0;
};
struct SectionInfo {
    std::string label;    // intro, verse, chorus, interlude, bridge, outro, silence…
    double      seconds = 0;
    int         bars    = 0;
};

struct AudioAnalysis {
    bool        ok = false;
    std::string error;
    double      duration = 0;                      // secondes
    std::vector<TagScore> styles, moods, instruments;  // triés par score décroissant (styles Discogs « Rock---Goth Rock »)
    float       voice = 0;                         // probabilité de voix chantée (contre instrumental)
    float       male = 0;                          // probabilité d'une voix masculine (si voix)
    float       bright = 0, tonal = 0, danceable = 0;
    std::map<std::string, float> mood_flags;       // happy, sad, aggressive, relaxed, party, acoustic, electronic
    double      bpm = 0;                           // tempo en noires par minute (partition)
    std::string key;                               // « A minor », « C major »…
    std::string meter;                             // « 4/4 »
    std::vector<SectionInfo> sections;             // structure d'après la partition
    bool        has_vocal_notes = false;
    int         vocal_low = 0, vocal_high = 0, vocal_median = 0;  // notes MIDI de la mélodie chantée (5e, 95e centile, médiane)
    std::vector<LyricLine> lyric_lines;            // paroles transcrites (vide : instrumental, modèle absent ou rien d'audible)
    std::string lyrics_lang;                       // langue détectée par Whisper (« en », « fr »…)
    std::string lyrics_error;                      // transcription impossible : dernière ligne du moteur
    std::string score_error;                       // partition indisponible : dernière ligne du moteur de transcription
    std::vector<std::string> chords;               // accords les plus fréquents
};

// Un réglage proposé au formulaire.
struct Suggestion {
    std::string tag;       // balise de style en anglais (« gothic rock », « 132 BPM »…)
    std::string group;     // « genre », « mood », « voice », « instrument », « tempo », « key »
    float       score = 0;
    bool        selected = false;
    std::string why;       // origine (texte court, non traduit : « Discogs : Rock---Goth Rock 12 % »)
};
std::vector<Suggestion> build_suggestions(const AudioAnalysis & a);
// Structure proposée pour les paroles : balises de section YuE2 dans l'ordre (« [Intro] », « [Verse] »…).
std::vector<std::string> suggested_structure(const AudioAnalysis & a, bool instrumental);

// Paroles prêtes à coller : sections de la structure détectée, chacune avec les lignes transcrites qui y tombent.
std::string suggested_lyrics(const AudioAnalysis & a);

// Langue chantée d'un enregistrement, à l'écoute (Whisper, détection seule sur trois extraits de 30 s).
// Renvoie le code de la langue majoritaire (« en », « fr »…) ou vide ; avg = probabilité moyenne, votes = extraits d'accord (sur 3).
std::string detect_audio_language(const fs::path & audio, const fs::path & whisper_exe, const fs::path & whisper_model, const fs::path & work_dir,
                                  double seconds, float & avg, int & votes);

// Voix dans un enregistrement marqué instrumental : Whisper transcrit trois extraits de 30 s ; words = mots distincts entendus
// (hors « (music) » et autres indications), lang = langue majoritaire. Quelques mots seulement sont des hallucinations fréquentes.
void detect_audio_vocals(const fs::path & audio, const fs::path & whisper_exe, const fs::path & whisper_model, const fs::path & work_dir,
                         double seconds, std::string & lang, int & words);

// Analyse un fichier. progress(étape, 0..1) est appelée régulièrement ; cancel interrompt.
// ffmpeg_dir : dossier de travail pour les fichiers temporaires ; transcribe/sheetsage : moteur et modèle de partition
// (facultatifs : sans eux, pas de tempo, de tonalité ni de structure).
bool analyze_audio(const fs::path & audio, const fs::path & models_dir, const fs::path & work_dir, const fs::path & transcribe_exe,
                   const fs::path & sheetsage_gguf, const fs::path & whisper_exe, const fs::path & whisper_model,
                   const std::function<void(const std::string &, float)> & progress,
                   std::atomic<bool> & cancel, AudioAnalysis & out);

// 1.1.007 : empreinte d'un morceau pour la recherche de doublons.
//  - sound : « son » global (moyenne et écart des embeddings Discogs-EffNet, 2 x 1280 valeurs, normé) ; vide si le modèle
//    d'analyse n'est pas installé ;
//  - chroma : mélodie et accords, 12 valeurs (hauteurs de Do à Si) par demi-seconde, centrées et normées.
struct AudioPrint {
    std::vector<float> sound;
    std::vector<float> chroma;
};
bool audio_print(const fs::path & audio, const fs::path & models_dir, const fs::path & work_dir, std::atomic<bool> & cancel, AudioPrint & out);
bool save_print(const fs::path & file, const AudioPrint & p);
bool load_print(const fs::path & file, AudioPrint & p);
// Ressemblance du son (cosinus, -1 à 1) ; -2 si une empreinte manque.
float print_sound_similarity(const AudioPrint & a, const AudioPrint & b);
// Ressemblance de la mélodie (0 à 1) : meilleur passage commun d'au moins 20 s, toutes tonalités et tous décalages essayés.
float print_melody_similarity(const AudioPrint & a, const AudioPrint & b);
