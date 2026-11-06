// 1.0.97 : rendu d'un fichier MIDI en WAV avec une banque SoundFont 2 (TinySoundFont).
#pragma once

#include <atomic>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// midi : octets d'un fichier MIDI standard ; sf2 : banque ; wav : fichier créé (16 bits, 44,1 kHz, stéréo). Bloquant (quelques secondes).
bool render_midi_wav(const std::string & midi, const fs::path & sf2, const fs::path & wav, std::string & error, std::atomic<bool> * cancel = nullptr);
