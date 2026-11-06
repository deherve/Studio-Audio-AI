// 1.0.97 : lecture d'un fichier MIDI avec une banque de sons SoundFont (.sf2), rendu en WAV 16 bits stéréo (TinySoundFont, MIT).
#include "soundfont.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#define TSF_IMPLEMENTATION
#include "../third_party/tsf/tsf.h"
#define TML_IMPLEMENTATION
#include "../third_party/tsf/tml.h"

namespace {
void put32(std::ofstream & f, uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); }
void put16(std::ofstream & f, uint16_t v) { f.write(reinterpret_cast<const char *>(&v), 2); }
}  // namespace

bool render_midi_wav(const std::string & midi, const fs::path & sf2, const fs::path & wav, std::string & error, std::atomic<bool> * cancel) {
    std::ifstream in(sf2, std::ios::binary);
    if (!in) { error = "banque introuvable"; return false; }
    std::vector<char> bank((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    tsf * font = tsf_load_memory(bank.data(), (int) bank.size());
    bank.clear();
    bank.shrink_to_fit();
    if (!font) { error = "banque illisible (format SoundFont 2 attendu)"; return false; }
    tml_message * first = tml_load_memory(midi.data(), (int) midi.size());
    if (!first) { tsf_close(font); error = "fichier MIDI illisible"; return false; }
    const int rate = 44100;
    tsf_set_output(font, TSF_STEREO_INTERLEAVED, rate, -3.0f);
    for (int ch = 0; ch < 16; ch++) tsf_channel_set_presetnumber(font, ch, 0, ch == 9);
    unsigned total_ms = 0;
    tml_get_info(first, nullptr, nullptr, nullptr, nullptr, &total_ms);
    std::vector<int16_t> pcm;
    pcm.reserve((size_t) (total_ms / 1000.0 * rate * 2) + rate * 8);
    std::vector<float> block(512 * 2);
    double       now_ms = 0;
    tml_message * msg   = first;
    const double end_ms = total_ms + 2500.0;  // queue de réverbération des dernières notes
    while (now_ms < end_ms) {
        if (cancel && *cancel) { tml_free(first); tsf_close(font); error = "annulé"; return false; }
        for (; msg && msg->time <= now_ms; msg = msg->next) {
            switch (msg->type) {
                case TML_PROGRAM_CHANGE: tsf_channel_set_presetnumber(font, msg->channel, msg->program, msg->channel == 9); break;
                case TML_NOTE_ON: tsf_channel_note_on(font, msg->channel, msg->key, msg->velocity / 127.0f); break;
                case TML_NOTE_OFF: tsf_channel_note_off(font, msg->channel, msg->key); break;
                case TML_PITCH_BEND: tsf_channel_set_pitchwheel(font, msg->channel, msg->pitch_bend); break;
                case TML_CONTROL_CHANGE: tsf_channel_midi_control(font, msg->channel, msg->control, msg->control_value); break;
                default: break;
            }
        }
        tsf_render_float(font, block.data(), 512, 0);
        for (float v : block) pcm.push_back((int16_t) std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
        now_ms += 512 * 1000.0 / rate;
    }
    tml_free(first);
    tsf_close(font);
    std::ofstream out(wav, std::ios::binary | std::ios::trunc);
    if (!out) { error = "écriture impossible"; return false; }
    const uint32_t bytes = (uint32_t) (pcm.size() * 2);
    out.write("RIFF", 4);
    put32(out, 36 + bytes);
    out.write("WAVEfmt ", 8);
    put32(out, 16);
    put16(out, 1);
    put16(out, 2);
    put32(out, rate);
    put32(out, rate * 4);
    put16(out, 4);
    put16(out, 16);
    out.write("data", 4);
    put32(out, bytes);
    out.write(reinterpret_cast<const char *>(pcm.data()), bytes);
    return (bool) out;
}
