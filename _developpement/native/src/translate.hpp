// Traduction des paroles dans une autre langue, sans toucher à la structure du morceau :
// les balises de section ([Verse], [Chorus]…) et les lignes vides restent à leur place, seules les lignes
// chantées sont traduites, une ligne traduite par ligne d'origine.
//
// Deux moteurs :
//  - Internet (gratuit, sans clé) : Google Traduction (point d'accès public « gtx »), plus de 100 langues ;
//  - local, hors ligne : petit modèle de langage (Qwen2.5-3B-Instruct, format GGUF) exécuté sur le processeur
//    par llama.cpp (dossier « outils\llama » de l'application) : une vingtaine de langues.
// Sans connexion, le moteur local prend le relais ; la liste des langues s'adapte au moteur disponible.
#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct TranslateLang {
    const char * code;   // code de langue (Google) : « fr », « en », « zh-CN »…
    const char * name;   // nom dans la langue elle-même (« Français », « English »), sans traduction nécessaire
    const char * llm;    // nom anglais donné au modèle local ; nullptr = langue non prise en charge hors ligne
    bool         yue2;   // langue que YuE2 sait chanter (anglais, français, espagnol… cantonais)
};

// Toutes les langues proposées, celles chantées par YuE2 en premier.
const std::vector<TranslateLang> & translate_languages();
const TranslateLang *              find_translate_lang(const std::string & code);

// Fichiers du moteur local.
struct LocalTranslator {
    fs::path exe;    // llama-completion.exe (ou llama-cli.exe) ; vide = absent
    fs::path model;  // modèle GGUF ; vide = absent
    bool     ready() const { return !exe.empty() && !model.empty(); }
};
// Cherche llama.cpp dans <dossier de l'application>\outils\llama (ou l'ancien dossier traduction) et le modèle dans <models>\traduction.
LocalTranslator find_local_translator(const fs::path & app_dir, const fs::path & models_dir);

// Modèle local proposé au téléchargement (Réglages › Analyse et paroles).
struct TranslateModelFile {
    const char *       name;
    const char *       url;
    unsigned long long bytes;  // taille approximative (la vraie taille vient du serveur)
};
const TranslateModelFile & translate_model_file();

// Vrai si le service de traduction Internet répond (requête courte, quelques secondes au plus hors ligne).
bool translate_web_available();

struct TranslateResult {
    bool        ok = false;
    std::string text;    // paroles traduites (même structure)
    std::string engine;  // « web » ou « local »
    std::string error;   // message d'erreur (en français, passé à tr() par l'interface)
    int         kept = 0;  // lignes laissées dans la langue d'origine (réponse incomplète du moteur)
};

// Traduit les paroles vers la langue « target ». use_web : essayer Internet d'abord.
// progress : 0 à 1 pendant le travail ; cancel : demande d'arrêt. Bloquant : à lancer dans un fil séparé.
TranslateResult translate_lyrics(const std::string & lyrics, const std::string & target, bool use_web,
                                 const LocalTranslator & local, std::atomic<float> * progress, std::atomic<bool> * cancel);
