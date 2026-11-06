// 1.0.91 : « Atelier partition », fenêtre du Studio qui affiche la partition en notes avec abcjs (bibliothèque JavaScript,
// licence MIT) dans le moteur WebView2 de Windows (fourni avec Windows 10/11). Les pages et les sons sont livrés dans
// ressources\partition et servis sous l'adresse locale https://atelier.studio/ : aucun accès à internet n'est nécessaire.
//
// Échanges : le Studio envoie des messages JSON à la page (atelier_post) ; la page répond par window.chrome.webview.postMessage,
// reçu par on_message (appelé sur le fil de l'interface). L'impression passe par la page ; le PDF par atelier_print_pdf.
#pragma once

#include <windows.h>

#include <functional>
#include <string>

struct AtelierCallbacks {
    std::function<void(const std::string & json)>                         on_message;  // message de la page (JSON)
    std::function<void(const std::string & json)>                         on_ready;    // page chargée (ignoré si vide)
    std::function<void(bool ok, const std::wstring & path)>               on_pdf;      // fin d'un export PDF
};

// Moteur WebView2 présent sur ce PC ? (version installée, vide si absent)
std::wstring atelier_runtime_version();
// Ouvre (ou ramène devant) la fenêtre et lui envoie first_message une fois la page prête. false si WebView2 manque.
bool atelier_open(HWND owner, const std::wstring & title, const std::wstring & pages_dir, const std::wstring & user_data_dir,
                  const std::string & first_message, AtelierCallbacks callbacks);
// 1.0.97 : dossier des sons rendus (WAV), servi à la page sous https://rendu.atelier.studio/ (à appeler avant atelier_open)
void atelier_set_audio_dir(const std::wstring & dir);
bool atelier_is_open();
void atelier_post(const std::string & json);  // message JSON à la page (mis en attente si elle charge encore)
bool atelier_print_pdf(const std::wstring & path);  // PDF vectoriel de la page (fin signalée par on_pdf)
void atelier_close();
