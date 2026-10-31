// Requêtes HTTPS courtes (paroles sur Internet, recherche de mise à jour) : jamais de blocage hors ligne.
#pragma once

#include <string>
#include <utility>
#include <vector>

// GET HTTPS avec délais courts (résolution 4 s, connexion 4 s, envoi 6 s, réponse 8 s).
// Renvoie le code HTTP (200, 404…) ou 0 si le site est injoignable (pas de connexion).
int https_get(const std::wstring & host, const std::wstring & path, std::string & body);

// Même chose à partir d'une adresse complète « https://site/chemin?param » (UTF-8). 0 si l'adresse est invalide.
int https_get_url(const std::string & url, std::string & body);

// Requête HTTP ou HTTPS complète (1.0.26, onglet « Composer avec l'IA ») : GET ou POST, en-têtes au choix
// (clé d'API…), corps JSON, délai de réponse long (une IA peut mettre une minute à écrire).
// url : « http://127.0.0.1:11434/v1/models » ou « https://api.openai.com/v1/chat/completions ».
// timeout_ms : délai d'attente de la réponse. Renvoie le code HTTP, 0 si le serveur est injoignable.
int http_request(const std::string & method, const std::string & url, const std::vector<std::pair<std::string, std::string>> & headers,
                 const std::string & body, std::string & out, int timeout_ms);
