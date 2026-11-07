// 1.1.005 : aide intégrée de la disposition B. La page d'aide (ressources\aide) s'affiche dans la page de droite du Studio,
// par le moteur WebView2 (comme l'Atelier partition), servie sous https://aide.studio/ : aucun accès à Internet.
// Le navigateur est créé à la première ouverture de la page Aide et libéré dès qu'on la quitte (mémoire rendue).
#pragma once
#include <windows.h>

#include <string>

// Place (ou crée) la vue d'aide dans la fenêtre parent, au rectangle donné (coordonnées client, pixels).
// url : adresse à charger la première fois (ou après helpview_navigate). false si WebView2 manque.
bool helpview_show(HWND parent, const RECT & rect, const std::wstring & pages_dir, const std::wstring & user_data_dir, const std::wstring & url);
void helpview_navigate(const std::wstring & url);  // nouvelle adresse (ex. rubrique #id)
void helpview_set_visible(bool visible);           // cachée quand une fenêtre du Studio s'ouvre par-dessus
void helpview_back();
void helpview_forward();
void helpview_release();                           // libère le navigateur (page quittée)
