// Paroles d'un morceau de reprise : recherche sur Internet (base ouverte LRCLIB) pour corriger la transcription
// de Whisper, et filtre des phrases que Whisper invente sur de la musique.
//
// Principe :
//  1. on devine le groupe et le titre du fichier (étiquettes du fichier, sinon son nom et ses dossiers :
//     « …\The Sisters of Mercy\1990.Vision Thing\08 I Was Wrong.mp3 ») ;
//  2. on demande les paroles à lrclib.net (gratuit, sans compte) ; hors ligne, on abandonne en quelques secondes ;
//  3. on vérifie que ce sont bien les paroles du morceau (mots communs avec ce que Whisper a entendu) ;
//  4. on reconstruit la structure : strophes répétées = [Chorus], autres = [Verse], strophe unique après
//     le deuxième refrain = [Bridge], [Intro] et [Outro] repris de l'analyse.
#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Identité d'un morceau devinée à partir du fichier.
struct SongIdentity {
    std::string artist, title, album;
    double      seconds = 0;  // durée du fichier (aide LRCLIB à choisir la bonne version)
    std::string origin;       // « tags » (étiquettes du fichier) ou « name » (nom du fichier et dossiers)
    bool        ok() const { return !artist.empty() && !title.empty(); }
};

// Étiquettes artist / title / album lues par ffprobe ; à défaut, nom du fichier et dossiers parents.
SongIdentity guess_song_identity(const fs::path & audio, double seconds);

enum class WebLyricsStatus {
    NotTried = 0,  // recherche désactivée, pas de voix, ou pas encore lancée
    NoIdentity,    // groupe ou titre introuvables
    Offline,       // pas de connexion (ou site injoignable) : on garde Whisper
    NotFound,      // morceau inconnu de LRCLIB (ou instrumental)
    Mismatch,      // paroles trouvées mais trop différentes de ce que Whisper a entendu : on garde Whisper
    Found,         // paroles trouvées et retenues
};

struct WebLyrics {
    WebLyricsStatus status = WebLyricsStatus::NotTried;
    std::string     plain;   // paroles brutes (strophes séparées par une ligne vide)
    std::string     source;  // « LRCLIB · groupe – titre (album) »
    double          match = -1;  // part des mots de Whisper retrouvés dans les paroles (0 à 1) ; -1 = non mesuré
};

// Recherche sur lrclib.net (délais courts : jamais de blocage hors ligne). whisper_text : ce que Whisper a entendu
// (déjà filtré), pour vérifier qu'on a trouvé le bon morceau ; vide = pas de vérification possible.
WebLyrics fetch_web_lyrics(const SongIdentity & id, const std::string & whisper_text, const std::atomic<bool> & cancel);

// Paroles au format YuE2, structure reconstruite à partir des strophes. intro / outro : l'analyse a trouvé
// une introduction / une fin instrumentales.
std::string web_lyrics_to_yue2(const std::string & plain, bool intro, bool outro);

// Vrai pour une ligne typique des inventions de Whisper sur de la musique (« Thank you for watching »,
// « Sous-titres réalisés par… », « Abonnez-vous »…).
bool is_whisper_hallucination(const std::string & line);
