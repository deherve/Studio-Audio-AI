// Téléchargements HTTPS (WinHTTP) avec reprise des fichiers .part ; sert au premier lancement et aux LoRA.
#pragma once

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// Télécharge url vers dest (via dest + ".part", renommé à la fin). expected : taille attendue en octets (0 = inconnue).
// done reçoit les octets reçus ; cancel interrompt. Renvoie false en cas d'échec (error décrit la cause).
bool http_fetch(const std::wstring & url, const fs::path & dest, unsigned long long expected,
                std::atomic<unsigned long long> & done, std::atomic<bool> & cancel, std::wstring & error);

// Lit une petite réponse HTTPS en texte (délais courts : 8 s). Renvoie faux si le réseau ou le serveur échoue.
bool http_get_text(const std::wstring & url, std::string & body);

// Taille (octets) du fichier distant, sans le télécharger (requête « Range: bytes=0-0 », redirections suivies). 0 = inconnue.
unsigned long long http_remote_size(const std::wstring & url);

struct DlFile {
    std::string        url, file;  // file : nom du fichier créé dans le dossier de destination
    unsigned long long bytes = 0;
};

// Un téléchargement d'un ou plusieurs fichiers (une LoRA à deux fichiers par exemple), suivi par l'interface.
class DownloadJob {
  public:
    std::string                     key, label;
    std::atomic<unsigned long long> done{ 0 }, total{ 0 };
    std::atomic<int>                state{ 0 };  // 0 en cours, 1 terminé, 2 erreur, 3 annulé
    std::atomic<bool>               cancel{ false };
    std::string                     error;  // lu seulement quand state != 0
    std::thread                     thread;
    ~DownloadJob();
};

class Downloader {
  public:
    // Lance le téléchargement (un seul par clé à la fois). Les fichiers déjà complets sont ignorés.
    std::shared_ptr<DownloadJob> start(const std::string & key, const std::string & label, const fs::path & dir,
                                       const std::vector<DlFile> & files);
    std::shared_ptr<DownloadJob> find(const std::string & key);
    void                         cancel_all();

  private:
    std::mutex                                mutex_;
    std::vector<std::shared_ptr<DownloadJob>> jobs_;
};
