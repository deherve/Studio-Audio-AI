#define _USE_MATH_DEFINES
#include "analysis.hpp"
#include "util.hpp"  // 1.1.001 : trim, lower, read_text, write_text, url_encode
#include "lyrics_web.hpp"  // is_whisper_hallucination

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <set>
#include <sstream>

#include <onnxruntime_cxx_api.h>

#include "i18n.hpp"
#include "studio.hpp"
#include "yyjson.h"

namespace {

const char * BASE = "https://essentia.upf.edu/models/";

struct HeadDef {
    const char *       name;
    unsigned long long onnx_bytes, json_bytes;
};
// Têtes de classification sur les embeddings Discogs-EffNet (tailles exactes des fichiers publiés).
const HeadDef HEADS[] = {
    { "mtg_jamendo_moodtheme", 2739322, 3646 }, { "mtg_jamendo_instrument", 2706492, 3382 }, { "voice_instrumental", 514114, 2013 },
    { "gender", 514089, 1965 },                 { "timbre", 513429, 1980 },                  { "tonal_atonal", 514101, 1981 },
    { "danceability", 514101, 1989 },           { "mood_happy", 514097, 2350 },              { "mood_sad", 514093, 2334 },
    { "mood_aggressive", 514107, 2386 },        { "mood_relaxed", 514101, 2364 },            { "mood_party", 514097, 2350 },
    { "mood_acoustic", 514103, 2380 },          { "mood_electronic", 514107, 2394 },
};

constexpr int SR = 16000, FRAME = 512, HOP = 256, BANDS = 96, PATCH = 128, PATCH_HOP = 62, EMB = 1280, STYLES = 400;

// Noms de classes d'un fichier « *-discogs-effnet-1.json »
std::vector<std::string> read_classes(const fs::path & json) {
    std::vector<std::string> out;
    std::string              text = read_text(json);
    yyjson_doc *             doc  = yyjson_read(text.data(), text.size(), 0);
    if (!doc) return out;
    yyjson_val * arr = yyjson_obj_get(yyjson_doc_get_root(doc), "classes");
    size_t       i, n;
    yyjson_val * v;
    yyjson_arr_foreach(arr, i, n, v) {
        if (yyjson_is_str(v)) out.push_back(yyjson_get_str(v));
    }
    yyjson_doc_free(doc);
    return out;
}

// ------------------------------------------------------------------ spectrogramme mel (réglages d'entraînement des modèles Essentia)

void fft512(std::complex<float> * a) {
    static std::vector<std::complex<float>> tw;
    static std::vector<int>                 rev;
    if (tw.empty()) {
        tw.resize(FRAME / 2);
        for (int i = 0; i < FRAME / 2; i++) tw[i] = std::polar(1.f, (float) (-2.0 * M_PI * i / FRAME));
        rev.resize(FRAME);
        for (int i = 0; i < FRAME; i++) {
            int r = 0;
            for (int b = 0; b < 9; b++) if (i & (1 << b)) r |= 1 << (8 - b);
            rev[i] = r;
        }
    }
    for (int i = 0; i < FRAME; i++) if (i < rev[i]) std::swap(a[i], a[rev[i]]);
    for (int len = 2; len <= FRAME; len <<= 1) {
        int half = len / 2, step = FRAME / len;
        for (int i = 0; i < FRAME; i += len) {
            for (int j = 0; j < half; j++) {
                std::complex<float> u = a[i + j], v = a[i + j + half] * tw[j * step];
                a[i + j]        = u + v;
                a[i + j + half] = u - v;
            }
        }
    }
}

double hz_to_mel(double f) {  // échelle de Slaney
    const double f_sp = 200.0 / 3, min_log_hz = 1000.0, min_log_mel = min_log_hz / f_sp, logstep = std::log(6.4) / 27.0;
    return f >= min_log_hz ? min_log_mel + std::log(f / min_log_hz) / logstep : f / f_sp;
}
double mel_to_hz(double m) {
    const double f_sp = 200.0 / 3, min_log_hz = 1000.0, min_log_mel = min_log_hz / f_sp, logstep = std::log(6.4) / 27.0;
    return m >= min_log_mel ? min_log_hz * std::exp(logstep * (m - min_log_mel)) : f_sp * m;
}

// Poids des 96 bandes (triangles de hauteur 1) sur les 257 bins.
std::vector<float> mel_weights() {
    std::vector<double> pts(BANDS + 2);
    double              m0 = hz_to_mel(0), m1 = hz_to_mel(SR / 2.0);
    for (int i = 0; i < BANDS + 2; i++) pts[i] = mel_to_hz(m0 + (m1 - m0) * i / (BANDS + 1));
    std::vector<float> w((size_t) BANDS * (FRAME / 2 + 1), 0.f);
    for (int b = 0; b < BANDS; b++) {
        // Normalisation « unit_tri » d'Essentia (1.0.23) : chaque triangle a une aire de 1 en Hz, d'où le facteur
        // 2 / largeur. Sans elle, les bandes aiguës (larges) pesaient trop et faussaient les styles détectés.
        double norm = 2.0 / (pts[b + 2] - pts[b]);
        for (int k = 0; k <= FRAME / 2; k++) {
            double f     = (double) k * SR / FRAME;
            double lower = (f - pts[b]) / (pts[b + 1] - pts[b]);
            double upper = (pts[b + 2] - f) / (pts[b + 2] - pts[b + 1]);
            w[(size_t) b * (FRAME / 2 + 1) + k] = (float) (norm * std::max(0.0, std::min(lower, upper)));
        }
    }
    return w;
}

// Renvoie nfr x 96 valeurs : log10(10000 x + 1) du spectre de puissance
std::vector<float> mel_spectrogram(const std::vector<float> & y, int & nfr) {
    std::vector<float> win(FRAME);
    for (int i = 0; i < FRAME; i++) win[i] = (float) (0.5 - 0.5 * std::cos(2.0 * M_PI * i / (FRAME - 1)));
    static const std::vector<float> weights = mel_weights();
    // Comme Essentia (1.0.23) : trames entières seulement, la première commence à l'échantillon 0 (pas de centrage).
    nfr = (int) y.size() >= FRAME ? 1 + ((int) y.size() - FRAME) / HOP : 0;
    std::vector<float>               mel((size_t) nfr * BANDS);
    std::vector<std::complex<float>> buf(FRAME);
    std::vector<float>               power(FRAME / 2 + 1);
    for (int f = 0; f < nfr; f++) {
        long start = (long) f * HOP;
        for (int i = 0; i < FRAME; i++) {
            long  idx = start + i;
            float s   = idx >= 0 && idx < (long) y.size() ? y[(size_t) idx] : 0.f;
            buf[i]    = std::complex<float>(s * win[i], 0.f);
        }
        fft512(buf.data());
        for (int k = 0; k <= FRAME / 2; k++) power[k] = std::norm(buf[k]);
        for (int b = 0; b < BANDS; b++) {
            double acc = 0;
            const float * wb = &weights[(size_t) b * (FRAME / 2 + 1)];
            for (int k = 0; k <= FRAME / 2; k++) acc += (double) wb[k] * power[k];
            mel[(size_t) f * BANDS + b] = (float) std::log10(10000.0 * acc + 1.0);
        }
    }
    return mel;
}

// Sortie « -oj » de whisper-cli : segments horodatés (millisecondes) et langue détectée.
void parse_whisper(const std::string & json, AudioAnalysis & a) {
    yyjson_doc * doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) return;
    yyjson_val * root = yyjson_doc_get_root(doc);
    yyjson_val * lang = yyjson_obj_get(yyjson_obj_get(root, "result"), "language");
    if (yyjson_is_str(lang)) a.lyrics_lang = yyjson_get_str(lang);
    size_t       i, n;
    yyjson_val * seg;
    std::string  prev;
    yyjson_arr_foreach(yyjson_obj_get(root, "transcription"), i, n, seg) {
        yyjson_val * t = yyjson_obj_get(seg, "text");
        yyjson_val * o = yyjson_obj_get(seg, "offsets");
        if (!yyjson_is_str(t)) continue;
        std::string text = yyjson_get_str(t);
        size_t      b = text.find_first_not_of(" \t\r\n"), e = text.find_last_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        text = text.substr(b, e - b + 1);
        if (text.front() == '[' || text.front() == '(' || text.find("\xE2\x99\xAA") != std::string::npos) continue;  // [Music], (applaudissements), ♪
        if (text == prev) continue;  // répétition de segment : hallucination fréquente sur les passages instrumentaux
        if (is_whisper_hallucination(text)) continue;  // « Thank you for watching… », crédits de sous-titres…
        prev = text;
        LyricLine l;
        l.start = yyjson_get_num(yyjson_obj_get(o, "from")) / 1000.0;
        l.end   = yyjson_get_num(yyjson_obj_get(o, "to")) / 1000.0;
        l.text  = text;
        a.lyric_lines.push_back(l);
    }
    yyjson_doc_free(doc);
}

// ------------------------------------------------------------------ partition ABC (SheetSage2)

struct AbcSection {
    std::string label;
    int         vocal_bars = 0, other_bars = 0;
};

int count_bars(const std::string & line) {
    int  bars = 0;
    bool has  = false;
    for (char c : line) {
        if (c == '|') {
            if (has) bars++;
            has = false;
        } else if (c != ' ' && c != ':' && c != ']' && c != '[') {
            has = true;
        }
    }
    if (has) bars++;
    return bars;
}

void parse_abc(const std::string & abc, AudioAnalysis & a) {
    std::istringstream       in(abc);
    std::string              line, voice = "Vocal";
    double                   beat_whole = 0.25, q_per_min = 0, bar_whole = 1.0;
    std::vector<AbcSection>  secs;
    std::map<std::string, int> chord_count;
    std::vector<int>         notes;
    bool                     header = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (header) {
            if (line.rfind("M:", 0) == 0) {
                int n = 4, d = 4;
                if (sscanf(line.c_str() + 2, "%d/%d", &n, &d) == 2 && d > 0) {
                    bar_whole = (double) n / d;
                    a.meter   = std::to_string(n) + "/" + std::to_string(d);
                }
            } else if (line.rfind("Q:", 0) == 0) {
                int n = 1, d = 4, bpm = 0;
                if (sscanf(line.c_str() + 2, "%d/%d=%d", &n, &d, &bpm) == 3 && d > 0 && bpm > 0) {
                    beat_whole = (double) n / d;
                    q_per_min  = bpm;
                    a.bpm      = bpm * (beat_whole / 0.25);  // en noires par minute
                }
            } else if (line.rfind("K:", 0) == 0) {
                std::string k = line.substr(2);
                while (!k.empty() && k[0] == ' ') k.erase(0, 1);
                if (!k.empty() && k[0] >= 'A' && k[0] <= 'G') {
                    std::string tonic(1, k[0]);
                    size_t      p = 1;
                    if (p < k.size() && (k[p] == '#' || k[p] == 'b')) tonic += k[p++];
                    std::string rest = k.substr(p), low;
                    for (char c : rest) low += (char) tolower((unsigned char) c);
                    bool minor = !low.empty() && (low[0] == 'm' && low.rfind("maj", 0) != 0);
                    if (low.rfind("min", 0) == 0 || low.rfind("aeo", 0) == 0) minor = true;
                    std::string mode = minor ? "minor" : "major";
                    if (low.rfind("dor", 0) == 0) mode = "dorian";
                    else if (low.rfind("mix", 0) == 0) mode = "mixolydian";
                    else if (low.rfind("lyd", 0) == 0) mode = "lydian";
                    else if (low.rfind("phr", 0) == 0) mode = "phrygian";
                    a.key = tonic + " " + mode;
                }
                header = false;
            }
            continue;
        }
        if (line.rfind("% ", 0) == 0) {
            AbcSection s;
            s.label = line.substr(2);
            secs.push_back(s);
            continue;
        }
        if (line.rfind("V:", 0) == 0) {
            size_t p = line.find_first_not_of(' ', 2);
            if (p != std::string::npos) voice = line.substr(p, line.find(' ', p) - p);
            continue;
        }
        if (line.empty() || line[0] == '%' || (line.size() > 1 && line[1] == ':')) continue;
        if (secs.empty()) secs.push_back({ "intro", 0, 0 });
        const bool vocal = voice == "Vocal";
        (vocal ? secs.back().vocal_bars : secs.back().other_bars) += count_bars(line);
        // accords et notes
        for (size_t i = 0; i < line.size();) {
            char c = line[i];
            if (c == '"') {
                size_t e = line.find('"', i + 1);
                if (e == std::string::npos) break;
                if (vocal) chord_count[line.substr(i + 1, e - i - 1)]++;
                i = e + 1;
                continue;
            }
            if (c == '!' || c == '+') {  // décorations
                size_t e = line.find(c, i + 1);
                i        = e == std::string::npos ? line.size() : e + 1;
                continue;
            }
            int acc = 0;
            while (i < line.size() && (line[i] == '^' || line[i] == '_' || line[i] == '=')) {
                acc += line[i] == '^' ? 1 : line[i] == '_' ? -1 : 0;
                i++;
            }
            if (i < line.size() && ((line[i] >= 'A' && line[i] <= 'G') || (line[i] >= 'a' && line[i] <= 'g'))) {
                static const int pc[7] = { 9, 11, 0, 2, 4, 5, 7 };  // A B C D E F G
                bool             low   = line[i] >= 'a';
                int              idx   = (low ? line[i] - 'a' : line[i] - 'A');
                int              midi  = pc[idx] + (low ? 72 : 60) + acc;
                if (idx < 2) midi += 0;  // A et B sont au-dessus du do de la même octave
                i++;
                while (i < line.size() && (line[i] == ',' || line[i] == '\'')) {
                    midi += line[i] == '\'' ? 12 : -12;
                    i++;
                }
                if (vocal) notes.push_back(midi);
                continue;
            }
            i++;
        }
    }
    // Durée des sections (mesures de la voix, à défaut de l'autre voix)
    double bar_seconds = (q_per_min > 0 && beat_whole > 0) ? (bar_whole / beat_whole) * 60.0 / q_per_min : 0;
    for (const AbcSection & s : secs) {
        SectionInfo si;
        si.label   = s.label;
        si.bars    = std::max(s.vocal_bars, s.other_bars);
        si.seconds = si.bars * bar_seconds;
        a.sections.push_back(si);
    }
    if (!notes.empty()) {
        std::sort(notes.begin(), notes.end());
        a.has_vocal_notes = notes.size() >= 12;
        a.vocal_low       = notes[notes.size() * 5 / 100];
        a.vocal_high      = notes[std::min(notes.size() - 1, notes.size() * 95 / 100)];
        a.vocal_median    = notes[notes.size() / 2];
    }
    std::vector<std::pair<std::string, int>> cc(chord_count.begin(), chord_count.end());
    std::sort(cc.begin(), cc.end(), [](auto & x, auto & y) { return x.second > y.second; });
    for (size_t i = 0; i < cc.size() && i < 6; i++) a.chords.push_back(cc[i].first);
}

}  // namespace

// ------------------------------------------------------------------ fichiers du modèle

const AnalysisFile & whisper_file() {
    static const AnalysisFile f = { "ggml-medium-q5_0.bin", "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium-q5_0.bin",
                                    539212467 };
    return f;
}

const std::vector<AnalysisFile> & analysis_files() {
    static const std::vector<AnalysisFile> files = [] {
        std::vector<AnalysisFile> v;
        v.push_back({ "discogs-effnet-bsdynamic-1.onnx", std::string(BASE) + "feature-extractors/discogs-effnet/discogs-effnet-bsdynamic-1.onnx", 18027718 });
        v.push_back({ "genre_discogs400-discogs-effnet-1.json", std::string(BASE) + "classification-heads/genre_discogs400/genre_discogs400-discogs-effnet-1.json", 14951 });
        for (const HeadDef & h : HEADS) {
            std::string base = std::string(BASE) + "classification-heads/" + h.name + "/" + h.name + "-discogs-effnet-1";
            v.push_back({ std::string(h.name) + "-discogs-effnet-1.onnx", base + ".onnx", h.onnx_bytes });
            v.push_back({ std::string(h.name) + "-discogs-effnet-1.json", base + ".json", h.json_bytes });
        }
        return v;
    }();
    return files;
}

bool analysis_installed(const fs::path & dir) {
    std::error_code ec;
    for (const AnalysisFile & f : analysis_files()) {
        auto n = fs::file_size(dir / widen(f.name), ec);
        if (ec || n == 0) return false;
    }
    return true;
}

// ------------------------------------------------------------------ analyse

bool analyze_audio(const fs::path & audio, const fs::path & models_dir, const fs::path & work_dir, const fs::path & transcribe_exe,
                   const fs::path & sheetsage_gguf, const fs::path & whisper_exe, const fs::path & whisper_model,
                   const std::function<void(const std::string &, float)> & progress,
                   std::atomic<bool> & cancel, AudioAnalysis & out) {
    out = AudioAnalysis();
    auto report = [&](const char * what, float f) { if (progress) progress(what, f); };
    try {
        std::error_code ec;
        fs::create_directories(work_dir, ec);

        // 1. décodage en mono 16 kHz
        report("decode", 0.02f);
        fs::path raw = work_dir / "analysis.f32";
        fs::remove(raw, ec);
        std::wstring ff = ffmpeg_exe() + L" -v error -y -i " + quote_arg(audio.wstring()) + L" -vn -ac 1 -ar 16000 -f f32le " + quote_arg(raw.wstring());
        if (run_command(ff, nullptr) != 0 || !fs::exists(raw, ec)) {
            out.error = "decode";
            return false;
        }
        std::vector<float> y;
        {
            std::ifstream in(raw, std::ios::binary);
            in.seekg(0, std::ios::end);
            size_t n = (size_t) in.tellg() / sizeof(float);
            in.seekg(0);
            y.resize(n);
            in.read((char *) y.data(), (std::streamsize) (n * sizeof(float)));
        }
        fs::remove(raw, ec);
        out.duration = (double) y.size() / SR;
        if (y.size() < (size_t) SR * 5) {
            out.error = "short";
            return false;
        }
        if (cancel) return false;

        // 2. spectrogramme et modèle de style
        report("mel", 0.08f);
        int                nfr = 0;
        std::vector<float> mel = mel_spectrogram(y, nfr);
        y.clear();
        y.shrink_to_fit();
        std::vector<int> starts;
        for (int s = 0; s + PATCH <= nfr; s += PATCH_HOP) starts.push_back(s);
        if (starts.empty()) {
            out.error = "short";
            return false;
        }
        Ort::Env            env(ORT_LOGGING_LEVEL_ERROR, "analysis");
        Ort::SessionOptions so;
        so.SetIntraOpNumThreads(4);
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        Ort::Session eff(env, (models_dir / L"discogs-effnet-bsdynamic-1.onnx").c_str(), so);
        Ort::MemoryInfo     mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::AllocatedStringPtr in_name = eff.GetInputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
        Ort::AllocatedStringPtr out0    = eff.GetOutputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
        Ort::AllocatedStringPtr out1    = eff.GetOutputNameAllocated(1, Ort::AllocatorWithDefaultOptions());
        const char *            in_names[]  = { in_name.get() };
        const char *            out_names[] = { out0.get(), out1.get() };
        std::vector<float>      acts(STYLES, 0.f), emb;  // activations moyennes, embeddings de tous les patchs
        const int               batch = 16;
        size_t                  done  = 0;
        for (size_t b0 = 0; b0 < starts.size(); b0 += batch) {
            if (cancel) return false;
            int                n = (int) std::min<size_t>(batch, starts.size() - b0);
            std::vector<float> in((size_t) n * PATCH * BANDS);
            for (int i = 0; i < n; i++) {
                std::copy(mel.begin() + (size_t) starts[b0 + i] * BANDS, mel.begin() + (size_t) (starts[b0 + i] + PATCH) * BANDS, in.begin() + (size_t) i * PATCH * BANDS);
            }
            int64_t       shape[3] = { n, PATCH, BANDS };
            Ort::Value    tensor   = Ort::Value::CreateTensor<float>(mem, in.data(), in.size(), shape, 3);
            auto          res      = eff.Run(Ort::RunOptions{ nullptr }, in_names, &tensor, 1, out_names, 2);
            const float * a_ptr    = res[0].GetTensorData<float>();
            const float * e_ptr    = res[1].GetTensorData<float>();
            for (int i = 0; i < n; i++) {
                for (int k = 0; k < STYLES; k++) acts[k] += a_ptr[(size_t) i * STYLES + k];
                emb.insert(emb.end(), e_ptr + (size_t) i * EMB, e_ptr + (size_t) (i + 1) * EMB);
            }
            done += n;
            report("style", 0.10f + 0.45f * (float) done / starts.size());
        }
        mel.clear();
        mel.shrink_to_fit();
        for (float & v : acts) v /= (float) starts.size();
        std::vector<std::string> style_names = read_classes(models_dir / L"genre_discogs400-discogs-effnet-1.json");
        for (int k = 0; k < STYLES && k < (int) style_names.size(); k++) out.styles.push_back({ style_names[k], acts[k] });
        std::sort(out.styles.begin(), out.styles.end(), [](auto & x, auto & y2) { return x.score > y2.score; });

        // 3. têtes de classification (embeddings -> activations moyennes)
        const size_t count = emb.size() / EMB;
        auto         head  = [&](const std::string & name, std::vector<std::string> & classes) -> std::vector<float> {
            Ort::Session s(env, (models_dir / widen(name + "-discogs-effnet-1.onnx")).c_str(), so);
            classes = read_classes(models_dir / widen(name + "-discogs-effnet-1.json"));
            Ort::AllocatedStringPtr hi = s.GetInputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
            Ort::AllocatedStringPtr ho = s.GetOutputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
            const char *            hin[]  = { hi.get() };
            const char *            hout[] = { ho.get() };
            int64_t                 shape[2] = { (int64_t) count, EMB };
            Ort::Value              t        = Ort::Value::CreateTensor<float>(mem, emb.data(), emb.size(), shape, 2);
            auto                    r        = s.Run(Ort::RunOptions{ nullptr }, hin, &t, 1, hout, 1);
            size_t                  k        = classes.size();
            std::vector<float>      mean(k, 0.f);
            const float *           p = r[0].GetTensorData<float>();
            for (size_t i = 0; i < count; i++) for (size_t j = 0; j < k; j++) mean[j] += p[i * k + j];
            for (float & v : mean) v /= (float) count;
            return mean;
        };
        std::vector<std::string> cls;
        report("tags", 0.60f);
        std::vector<float> v = head("mtg_jamendo_moodtheme", cls);
        for (size_t i = 0; i < cls.size(); i++) out.moods.push_back({ cls[i], v[i] });
        std::sort(out.moods.begin(), out.moods.end(), [](auto & x, auto & y2) { return x.score > y2.score; });
        v = head("mtg_jamendo_instrument", cls);
        for (size_t i = 0; i < cls.size(); i++) out.instruments.push_back({ cls[i], v[i] });
        std::sort(out.instruments.begin(), out.instruments.end(), [](auto & x, auto & y2) { return x.score > y2.score; });
        auto pick = [&](const std::string & name, const char * wanted) {
            std::vector<std::string> c;
            std::vector<float>       r = head(name, c);
            for (size_t i = 0; i < c.size(); i++) if (c[i] == wanted) return r[i];
            return 0.f;
        };
        out.voice     = pick("voice_instrumental", "voice");
        out.male      = pick("gender", "male");
        out.bright    = pick("timbre", "bright");
        out.tonal     = pick("tonal_atonal", "tonal");
        out.danceable = pick("danceability", "danceable");
        out.mood_flags["happy"]      = pick("mood_happy", "happy");
        out.mood_flags["sad"]        = pick("mood_sad", "sad");
        out.mood_flags["aggressive"] = pick("mood_aggressive", "aggressive");
        out.mood_flags["relaxed"]    = pick("mood_relaxed", "relaxed");
        out.mood_flags["party"]      = pick("mood_party", "party");
        out.mood_flags["acoustic"]   = pick("mood_acoustic", "acoustic");
        out.mood_flags["electronic"] = pick("mood_electronic", "electronic");
        report("tags", 0.80f);
        if (cancel) return false;

        // 4. partition : tempo, tonalité, voix, structure
        // (les variables GGML_VK_* propres à la carte sont posées par run_command avant chaque lancement)
        if (!transcribe_exe.empty() && fs::exists(transcribe_exe, ec) && fs::exists(sheetsage_gguf, ec)) {
            report("score", 0.82f);
            fs::path input = audio;
            std::string ext = narrow(audio.extension().wstring());
            for (char & c : ext) c = (char) tolower((unsigned char) c);
            if (ext != ".mp3" && ext != ".wav") {
                input = work_dir / "analysis_source.wav";
                std::wstring cv = ffmpeg_exe() + L" -v error -y -i " + quote_arg(audio.wstring()) + L" " + quote_arg(input.wstring());
                if (run_command(cv, nullptr) != 0) input.clear();
            }
            if (!input.empty()) {
                fs::path     abc_file = work_dir / "analysis.abc";
                fs::remove(abc_file, ec);
                std::wstring cmd = quote_arg(transcribe_exe.wstring()) + L" --no-fa --model " + quote_arg(sheetsage_gguf.wstring()) + L" --audio " +
                                   quote_arg(input.wstring()) + L" --out " + quote_arg(abc_file.wstring());
                std::string last;
                int         code = run_command(cmd, [&](const std::string & l) { if (!l.empty()) last = l; }, nullptr);
                std::string abc  = read_text(abc_file);
                if (!abc.empty()) parse_abc(abc, out);
                else out.score_error = "code " + std::to_string(code) + " : " + last;
                fs::remove(abc_file, ec);
                if (input != audio) fs::remove(input, ec);
            }
        }
        // 5. paroles (Whisper, carte graphique) : seulement si une voix est présente
        if (!whisper_exe.empty() && fs::exists(whisper_exe, ec) && fs::exists(whisper_model, ec) && out.voice >= 0.3f && !cancel) {
            report("lyrics", 0.90f);
            fs::path wav  = work_dir / "analysis_16k.wav";
            fs::path base = work_dir / "analysis_lyrics";
            fs::remove(wav, ec);
            fs::remove(base.wstring() + L".json", ec);
            std::wstring ff2 = ffmpeg_exe() + L" -v error -y -i " + quote_arg(audio.wstring()) + L" -vn -ac 1 -ar 16000 " + quote_arg(wav.wstring());
            std::string  last;
            if (run_command(ff2, nullptr) == 0) {
                std::wstring cmd = quote_arg(whisper_exe.wstring()) + L" -m " + quote_arg(whisper_model.wstring()) + L" -f " + quote_arg(wav.wstring()) +
                                   L" -l auto -sns -mc 0 -oj -np -of " + quote_arg(base.wstring());  // -mc 0 : sans contexte, Whisper ne boucle plus sur ses inventions
                int code = run_command(cmd, [&](const std::string & l) { if (!l.empty()) last = l; }, nullptr);
                std::string json = read_text(base.wstring() + L".json");
                if (json.empty()) out.lyrics_error = "code " + std::to_string(code) + " : " + last;
                else parse_whisper(json, out);
            } else {
                out.lyrics_error = "decode";
            }
            fs::remove(wav, ec);
            fs::remove(base.wstring() + L".json", ec);
        }
        report("done", 1.0f);
        out.ok = true;
        return true;
    } catch (const std::exception & e) {
        out.error = e.what();
        return false;
    }
}

std::string detect_audio_language(const fs::path & audio, const fs::path & whisper_exe, const fs::path & whisper_model, const fs::path & work_dir,
                                  double seconds, float & avg, int & votes) {
    avg   = 0;
    votes = 0;
    std::error_code ec;
    fs::create_directories(work_dir, ec);
    std::vector<double> starts;
    if (seconds < 40) starts = { 0 };
    else for (double f : { 0.25, 0.45, 0.65 }) starts.push_back(std::max(0.0, std::min(seconds * f, seconds - 30)));
    std::map<std::string, std::pair<float, int>> tally;  // langue -> somme des probabilités, votes
    fs::path wav = work_dir / "lang_check.wav";
    for (double st : starts) {
        fs::remove(wav, ec);
        char ss[32];
        snprintf(ss, sizeof(ss), "%.1f", st);
        std::wstring ff = ffmpeg_exe() + L" -v error -y -ss " + widen(ss) + L" -t 30 -i " + quote_arg(audio.wstring()) + L" -vn -ac 1 -ar 16000 " + quote_arg(wav.wstring());
        if (run_command(ff, nullptr) != 0 || !fs::exists(wav, ec)) continue;
        std::wstring cmd = quote_arg(whisper_exe.wstring()) + L" -m " + quote_arg(whisper_model.wstring()) + L" -f " + quote_arg(wav.wstring()) + L" -l auto -dl";
        run_command(cmd, [&](const std::string & l) {
            size_t a = l.find("auto-detected language: ");
            if (a == std::string::npos) return;
            char  code[16] = {};
            float p        = 0;
            if (sscanf(l.c_str() + a + 24, "%15s (p = %f)", code, &p) == 2) {
                tally[code].first += p;
                tally[code].second++;
            }
        }, nullptr);
    }
    fs::remove(wav, ec);
    std::string best;
    for (auto & [code, v] : tally) {
        if (best.empty() || v.first > tally[best].first) best = code;
    }
    if (best.empty()) return best;
    votes = tally[best].second;
    avg   = tally[best].first / (float) starts.size();
    return best;
}

void detect_audio_vocals(const fs::path & audio, const fs::path & whisper_exe, const fs::path & whisper_model, const fs::path & work_dir,
                         double seconds, std::string & lang, int & words) {
    lang  = "";
    words = 0;
    std::error_code ec;
    fs::create_directories(work_dir, ec);
    std::vector<double> starts;
    if (seconds < 40) starts = { 0 };
    else for (double f : { 0.25, 0.45, 0.65 }) starts.push_back(std::max(0.0, std::min(seconds * f, seconds - 30)));
    fs::path                    wav  = work_dir / "vocals_check.wav";
    fs::path                    base = work_dir / "vocals_check";
    std::map<std::string, int>  langs;
    std::set<std::string>       seen;
    for (double st : starts) {
        fs::remove(wav, ec);
        fs::remove(base.wstring() + L".json", ec);
        char ss[32];
        snprintf(ss, sizeof(ss), "%.1f", st);
        std::wstring ff = ffmpeg_exe() + L" -v error -y -ss " + widen(ss) + L" -t 30 -i " + quote_arg(audio.wstring()) + L" -vn -ac 1 -ar 16000 " + quote_arg(wav.wstring());
        if (run_command(ff, nullptr) != 0 || !fs::exists(wav, ec)) continue;
        std::wstring cmd = quote_arg(whisper_exe.wstring()) + L" -m " + quote_arg(whisper_model.wstring()) + L" -f " + quote_arg(wav.wstring()) +
                           L" -l auto -sns -oj -np -of " + quote_arg(base.wstring());
        run_command(cmd, [](const std::string &) {}, nullptr);
        std::string json = read_text(base.wstring() + L".json");
        if (json.empty()) continue;
        AudioAnalysis a;
        parse_whisper(json, a);  // écarte déjà [Music], (music), ♪ et les segments répétés
        int n = 0;
        for (const LyricLine & l : a.lyric_lines) {
            if (!seen.insert(l.text).second) continue;
            std::istringstream ws(l.text);
            std::string        w;
            while (ws >> w) n++;
        }
        if (n > 0 && !a.lyrics_lang.empty()) langs[a.lyrics_lang] += n;
        words += n;
    }
    fs::remove(wav, ec);
    fs::remove(base.wstring() + L".json", ec);
    int best = 0;
    for (auto & [code, n] : langs) if (n > best) { best = n; lang = code; }
}

// ------------------------------------------------------------------ suggestions de balises YuE2

namespace {

// « Rock---Goth Rock » -> « gothic rock » ; vide = style sans intérêt pour une balise
std::string style_tag(const std::string & cls) {
    size_t p = cls.find("---");
    if (p == std::string::npos) return lower(cls);
    std::string group = cls.substr(0, p), name = lower(cls.substr(p + 3));
    if (group == "Non-Music" || group == "Children's" || group == "Brass & Military") return "";
    static const std::map<std::string, std::string> alias = {
        { "goth rock", "gothic rock" }, { "brit pop", "britpop" }, { "drum n bass", "drum and bass" }, { "hi nrg", "hi-nrg" },
        { "darkwave", "dark wave" }, { "coldwave", "cold wave" }, { "rock & roll", "rock and roll" }, { "rhythm & blues", "R&B" },
        { "contemporary r&b", "R&B" }, { "prog rock", "progressive rock" }, { "dance-pop", "dance pop" }, { "acid rock", "psychedelic rock" },
        { "score", "cinematic" }, { "theme", "soundtrack" }, { "vocal", "" }, { "light music", "" }, { "parody", "" }, { "novelty", "" },
        { "pornogrind", "" }, { "oi", "punk rock" }, { "mod", "" }, { "twist", "" }, { "cut-up/dj", "" }, { "dj battle tool", "" },
        { "educational", "" }, { "story", "" }, { "dialogue", "" }, { "tribal", "" }, { "ghetto", "" }, { "donk", "" }, { "bleep", "" },
        { "gospel", "gospel" }, { "trip hop", "trip hop" }, { "hip hop", "hip hop" }, { "pop rap", "hip hop" }, { "boom bap", "hip hop" },
        { "hardcore hip-hop", "hip hop" }, { "gangsta", "hip hop" }, { "thug rap", "hip hop" }, { "rnb/swing", "R&B" },
        { "folk rock", "folk rock" }, { "country rock", "country rock" }, { "synthwave", "synthwave" }, { "vaporwave", "synthwave" },
        { "depressive black metal", "black metal" }, { "atmospheric black metal", "black metal" }, { "technical death metal", "death metal" },
        { "melodic death metal", "melodic death metal" }, { "funeral doom metal", "doom metal" }, { "sludge metal", "doom metal" },
        { "speed metal", "heavy metal" }, { "thrash", "thrash metal" }, { "nu metal", "nu metal" }, { "post-metal", "metal" },
    };
    auto it = alias.find(name);
    return it != alias.end() ? it->second : name;
}

}  // namespace

std::vector<Suggestion> build_suggestions(const AudioAnalysis & a) {
    std::vector<Suggestion> out;
    auto                    add = [&](const std::string & tag, const char * group, float score, bool sel, const std::string & why) {
        if (tag.empty()) return;
        for (const Suggestion & s : out) if (lower(s.tag) == lower(tag)) return;
        out.push_back({ tag, group, score, sel, why });
    };
    char buf[96];
    // --- genres (Discogs)
    {
        int   shown = 0;
        float top   = a.styles.empty() ? 0.f : a.styles[0].score;
        for (const TagScore & s : a.styles) {
            if (shown >= 6 || s.score < 0.03f) break;
            std::string tag = style_tag(s.name);
            if (tag.empty()) continue;
            snprintf(buf, sizeof(buf), "Discogs : %s (%.0f %%)", s.name.c_str(), s.score * 100);
            bool selected = shown < 3 && s.score >= std::max(0.05f, top * 0.30f);
            size_t before = out.size();
            add(tag, "genre", s.score, selected, buf);
            if (out.size() > before) shown++;
        }
    }
    // --- ambiances
    {
        static const std::map<std::string, std::string> mood_tag = {
            { "melodic", "melodic" }, { "energetic", "energetic" }, { "happy", "happy" }, { "love", "romantic" }, { "dark", "dark" },
            { "dramatic", "dramatic" }, { "epic", "epic" }, { "emotional", "emotional" }, { "melancholic", "melancholic" }, { "sad", "sad" },
            { "calm", "calm" }, { "relaxing", "calm" }, { "dream", "dreamy" }, { "uplifting", "uplifting" }, { "powerful", "powerful" },
            { "romantic", "romantic" }, { "groovy", "groovy" }, { "heavy", "intense" }, { "hopeful", "hopeful" }, { "inspiring", "inspiring" },
            { "positive", "uplifting" }, { "sexy", "sensual" }, { "meditative", "meditative" }, { "soft", "soft" }, { "deep", "" },
        };
        int shown = 0;
        for (const TagScore & m : a.moods) {
            if (shown >= 4 || m.score < 0.07f) break;
            auto it = mood_tag.find(m.name);
            if (it == mood_tag.end() || it->second.empty()) continue;
            snprintf(buf, sizeof(buf), "MTG-Jamendo : %s (%.0f %%)", m.name.c_str(), m.score * 100);
            size_t before = out.size();
            add(it->second, "mood", m.score, shown < 2 && m.score >= 0.10f, buf);
            if (out.size() > before) shown++;
        }
        auto flag = [&](const char * k) { auto it = a.mood_flags.find(k); return it == a.mood_flags.end() ? 0.f : it->second; };
        if (flag("sad") > 0.60f) add("sad", "mood", flag("sad"), false, tr("Humeur : triste"));
        if (flag("aggressive") > 0.55f) add("aggressive", "mood", flag("aggressive"), true, tr("Humeur : agressive"));
        if (flag("relaxed") > 0.65f) add("calm", "mood", flag("relaxed"), false, tr("Humeur : détendue"));
    }
    // --- instruments
    {
        static const std::map<std::string, std::string> inst_tag = {
            { "electricguitar", "electric guitar" }, { "acousticguitar", "acoustic guitar" }, { "bass", "bass guitar" },
            { "acousticbassguitar", "acoustic bass" }, { "drums", "drums" }, { "drummachine", "drum machine" }, { "synthesizer", "analog synth" },
            { "keyboard", "keyboards" }, { "piano", "piano" }, { "electricpiano", "electric piano" }, { "rhodes", "Rhodes" }, { "organ", "organ" },
            { "pipeorgan", "organ" }, { "strings", "strings" }, { "violin", "violin" }, { "cello", "cello" }, { "brass", "brass" },
            { "saxophone", "saxophone" }, { "trumpet", "trumpet" }, { "trombone", "trombone" }, { "flute", "flute" }, { "clarinet", "clarinet" },
            { "oboe", "oboe" }, { "harp", "harp" }, { "accordion", "accordion" }, { "harmonica", "harmonica" }, { "orchestra", "orchestral" },
            { "pad", "synth pads" }, { "percussion", "percussion" }, { "bongo", "bongos" }, { "doublebass", "double bass" },
            { "classicalguitar", "nylon string guitar" }, { "horn", "horns" }, { "guitar", "guitar" },
        };
        float guitar_specific = 0;
        for (const TagScore & s : a.instruments) if (s.name == "electricguitar" || s.name == "acousticguitar" || s.name == "classicalguitar") guitar_specific = std::max(guitar_specific, s.score);
        int shown = 0;
        for (const TagScore & s : a.instruments) {
            if (shown >= 6 || s.score < 0.20f) break;
            auto it = inst_tag.find(s.name);
            if (it == inst_tag.end()) continue;
            if (s.name == "guitar" && guitar_specific >= 0.20f) continue;  // le type précis suffit
            snprintf(buf, sizeof(buf), tr("Instruments : %s (%.0f %%)"), s.name.c_str(), s.score * 100);
            size_t before = out.size();
            add(it->second, "instrument", s.score, shown < 4 && s.score >= 0.25f, buf);
            if (out.size() > before) shown++;
        }
    }
    // --- voix : genre (modèle + hauteur de la mélodie) et registre
    if (a.voice >= 0.5f) {
        const float male = a.male;  // la hauteur transcrite n'est pas fiable pour le genre : le modèle seul décide
        const bool is_male = male >= 0.5f;
        snprintf(buf, sizeof(buf), tr("Voix : %s (%.0f %%)"), is_male ? tr("masculine") : tr("féminine"), (is_male ? male : 1 - male) * 100);
        add(is_male ? "male vocal" : "female vocal", "voice", is_male ? male : 1 - male, true, buf);
        if (a.has_vocal_notes) {
            int         m   = a.vocal_median;
            std::string reg = is_male ? (m < 52 ? "deep male vocal" : m < 64 ? "baritone vocal" : "tenor vocal")
                                      : (m < 62 ? "alto vocal" : m < 69 ? "mezzo-soprano vocal" : "soprano vocal");
            static const char * names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            snprintf(buf, sizeof(buf), tr("Mélodie chantée : %s%d à %s%d (médiane %s%d)"), names[a.vocal_low % 12], a.vocal_low / 12 - 1,
                     names[a.vocal_high % 12], a.vocal_high / 12 - 1, names[m % 12], m / 12 - 1);
            add(reg, "voice", 0.6f, true, buf);
            if (a.vocal_high - a.vocal_low <= 9) add("low vocal register", "voice", 0.4f, false, tr("Mélodie chantée : tessiture étroite"));
        }
    }
    // --- tempo et tonalité
    if (a.bpm > 0) {
        int bpm = (int) std::lround(a.bpm);
        bpm     = std::clamp(bpm, 60, 260);
        snprintf(buf, sizeof(buf), tr("Partition : %d BPM"), bpm);
        add(std::to_string(bpm) + " BPM", "tempo", 1.f, true, buf);
    }
    if (!a.key.empty()) {
        snprintf(buf, sizeof(buf), tr("Partition : %s"), a.key.c_str());
        add(a.key + " key", "key", 1.f, true, buf);
    }
    return out;
}

namespace {

struct Slot {
    std::string tag;
    double      start = 0, end = 0;
    bool        vocal = true;
};

// Sections retenues pour les paroles, avec leur place dans le temps (cumul des durées de la partition).
std::vector<Slot> structure_slots(const AudioAnalysis & a, bool instrumental) {
    std::vector<Slot> out;
    double            t = 0;
    for (size_t i = 0; i < a.sections.size(); i++) {
        const SectionInfo & s   = a.sections[i];
        std::string         lab = lower(s.label);
        double              from = t;
        t += s.seconds;
        std::string tag;
        if (lab == "intro") tag = "Intro";
        else if (lab == "verse") tag = "Verse";
        else if (lab == "chorus") tag = "Chorus";
        else if (lab == "pre-chorus" || lab == "prechorus") tag = "Pre-Chorus";
        else if (lab == "bridge") tag = "Bridge";
        else if (lab == "interlude" || lab == "solo" || lab == "instrumental") tag = "Interlude";
        else if (lab == "outro") tag = "Outro";
        else if (lab == "silence" || lab == "none") {
            if (i + 1 == a.sections.size() && !out.empty() && out.back().tag != "Outro" && s.seconds > 2) tag = "Outro";
            else continue;
        } else continue;
        if (s.bars > 0 && s.bars < 2) continue;  // section de moins de deux mesures : bruit
        Slot sl;
        sl.start = from;
        sl.end   = t;
        sl.vocal = !(tag == "Intro" || tag == "Interlude" || tag == "Outro");
        if (instrumental) {
            for (char & c : tag) c = (char) tolower((unsigned char) c);
            if (tag == "interlude") tag = "bridge";  // la LoRA instrumentale ne connaît pas [interlude]
        }
        sl.tag = tag;
        out.push_back(sl);
    }
    return out;
}

}  // namespace

std::vector<std::string> suggested_structure(const AudioAnalysis & a, bool instrumental) {
    std::vector<std::string> out;
    for (const Slot & s : structure_slots(a, instrumental)) out.push_back(s.tag);
    return out;
}

std::string suggested_lyrics(const AudioAnalysis & a) {
    std::vector<Slot> slots = structure_slots(a, false);
    std::string       out;
    if (a.lyric_lines.empty()) return out;
    if (slots.size() < 3) {  // pas de structure fiable : tout dans un couplet
        out = "[Verse]\n";
        for (const LyricLine & l : a.lyric_lines) out += l.text + "\n";
        return out;
    }
    std::vector<std::vector<const LyricLine *>> lines(slots.size());
    for (const LyricLine & l : a.lyric_lines) {
        double mid = (l.start + l.end) / 2;
        size_t k   = slots.size() - 1;
        for (size_t i = 0; i < slots.size(); i++) {
            if (mid < slots[i].end) {
                k = i;
                break;
            }
        }
        if (!slots[k].vocal) {  // de la voix dans une partie instrumentale : on la rattache au couplet ou refrain voisin
            size_t j = k;
            while (j + 1 < slots.size() && !slots[j].vocal) j++;
            if (!slots[j].vocal) {
                j = k;
                while (j > 0 && !slots[j].vocal) j--;
            }
            if (slots[j].vocal) k = j;
        }
        lines[k].push_back(&l);
    }
    for (size_t i = 0; i < slots.size(); i++) {
        out += "[" + slots[i].tag + "]\n";
        for (const LyricLine * l : lines[i]) out += l->text + "\n";
        if (slots[i].vocal && lines[i].empty()) out += "\n";
        out += "\n";
    }
    return out;
}

// ====================================================================== 1.1.007 : empreinte pour la recherche de doublons
namespace {
constexpr int PRINT_N = 4096, PRINT_HOP = 2000, PRINT_GROUP = 8;  // fenêtre 0,256 s, pas 0,125 s, 8 pas = 1 s par valeur de chroma

void fft_inplace(std::vector<std::complex<float>> & a) {  // radix 2, taille puissance de 2
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * M_PI / (double) len;
        const std::complex<float> wl((float) std::cos(ang), (float) std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (size_t k = 0; k < len / 2; k++) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

void normalize(std::vector<float> & v, size_t from, size_t count) {  // centré, norme 1 (zéros si silence)
    double mean = 0;
    for (size_t k = 0; k < count; k++) mean += v[from + k];
    mean /= (double) count;
    double norm = 0;
    for (size_t k = 0; k < count; k++) { v[from + k] -= (float) mean; norm += (double) v[from + k] * v[from + k]; }
    norm = std::sqrt(norm);
    for (size_t k = 0; k < count; k++) v[from + k] = norm > 1e-6 ? (float) (v[from + k] / norm) : 0.f;
}
}  // namespace

bool audio_print(const fs::path & audio, const fs::path & models_dir, const fs::path & work_dir, std::atomic<bool> & cancel, AudioPrint & out) {
    out = AudioPrint();
    try {
        std::error_code ec;
        fs::create_directories(work_dir, ec);
        const fs::path raw = work_dir / "empreinte.f32";
        fs::remove(raw, ec);
        const std::wstring ff = ffmpeg_exe() + L" -v error -y -i " + quote_arg(audio.wstring()) + L" -vn -ac 1 -ar 16000 -f f32le " + quote_arg(raw.wstring());
        if (run_command(ff, nullptr) != 0 || !fs::exists(raw, ec)) return false;
        std::vector<float> y;
        {
            std::ifstream in(raw, std::ios::binary);
            in.seekg(0, std::ios::end);
            const size_t n = (size_t) in.tellg() / sizeof(float);
            in.seekg(0);
            y.resize(n);
            in.read((char *) y.data(), (std::streamsize) (n * sizeof(float)));
        }
        fs::remove(raw, ec);
        if (y.size() < (size_t) SR * 10 || cancel) return false;

        // Chroma : énergie de chaque note (55 Hz à 2 kHz) ramenée aux 12 hauteurs, moyennée par seconde.
        {
            std::vector<int> pc(PRINT_N / 2 + 1, -1);
            for (int k = 1; k <= PRINT_N / 2; k++) {
                const double f = (double) k * SR / PRINT_N;
                if (f < 55 || f > 2000) continue;
                const int midi = (int) std::lround(12.0 * std::log2(f / 440.0) + 69.0);
                pc[k] = ((midi % 12) + 12) % 12;
            }
            std::vector<float> win(PRINT_N);
            for (int i = 0; i < PRINT_N; i++) win[i] = (float) (0.5 - 0.5 * std::cos(2.0 * M_PI * i / (PRINT_N - 1)));
            std::vector<std::complex<float>> buf(PRINT_N);
            std::vector<float> acc(12, 0.f);
            int in_group = 0;
            for (size_t start = 0; start + PRINT_N <= y.size(); start += PRINT_HOP) {
                for (int i = 0; i < PRINT_N; i++) buf[i] = std::complex<float>(y[start + i] * win[i], 0.f);
                fft_inplace(buf);
                for (int k = 1; k <= PRINT_N / 2; k++)
                    if (pc[k] >= 0) acc[pc[k]] += std::abs(buf[k]);
                if (++in_group == PRINT_GROUP) {
                    const size_t at = out.chroma.size();
                    for (int c = 0; c < 12; c++) out.chroma.push_back(std::log1p(acc[c]));
                    normalize(out.chroma, at, 12);
                    std::fill(acc.begin(), acc.end(), 0.f);
                    in_group = 0;
                }
                if (cancel) return false;
            }
        }

        // Son global : embeddings Discogs-EffNet (comme l'analyse), moyenne et écart de chaque dimension.
        if (fs::exists(models_dir / L"discogs-effnet-bsdynamic-1.onnx", ec)) {
            int nfr = 0;
            std::vector<float> mel = mel_spectrogram(y, nfr);
            y.clear();
            y.shrink_to_fit();
            std::vector<int> starts;
            for (int s = 0; s + PATCH <= nfr; s += PATCH_HOP) starts.push_back(s);
            if (!starts.empty()) {
                static Ort::Env     env(ORT_LOGGING_LEVEL_ERROR, "empreinte");
                Ort::SessionOptions so;
                so.SetIntraOpNumThreads(4);
                so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
                Ort::Session            eff(env, (models_dir / L"discogs-effnet-bsdynamic-1.onnx").c_str(), so);
                Ort::MemoryInfo         mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
                Ort::AllocatedStringPtr in_name = eff.GetInputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
                Ort::AllocatedStringPtr out0    = eff.GetOutputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
                Ort::AllocatedStringPtr out1    = eff.GetOutputNameAllocated(1, Ort::AllocatorWithDefaultOptions());
                const char *            in_names[]  = { in_name.get() };
                const char *            out_names[] = { out0.get(), out1.get() };
                std::vector<double>     sum(EMB, 0.0), sq(EMB, 0.0);
                const int               batch = 16;
                for (size_t b0 = 0; b0 < starts.size(); b0 += batch) {
                    if (cancel) return false;
                    const int          n = (int) std::min<size_t>(batch, starts.size() - b0);
                    std::vector<float> in((size_t) n * PATCH * BANDS);
                    for (int i = 0; i < n; i++)
                        std::copy(mel.begin() + (size_t) starts[b0 + i] * BANDS, mel.begin() + (size_t) (starts[b0 + i] + PATCH) * BANDS, in.begin() + (size_t) i * PATCH * BANDS);
                    int64_t       shape[3] = { n, PATCH, BANDS };
                    Ort::Value    tensor   = Ort::Value::CreateTensor<float>(mem, in.data(), in.size(), shape, 3);
                    auto          res      = eff.Run(Ort::RunOptions{ nullptr }, in_names, &tensor, 1, out_names, 2);
                    const float * e_ptr    = res[1].GetTensorData<float>();
                    for (int i = 0; i < n; i++)
                        for (int k = 0; k < EMB; k++) { const double v = e_ptr[(size_t) i * EMB + k]; sum[k] += v; sq[k] += v * v; }
                }
                const double cnt = (double) starts.size();
                out.sound.resize(2 * EMB);
                for (int k = 0; k < EMB; k++) {
                    const double m = sum[k] / cnt;
                    out.sound[k]       = (float) m;
                    out.sound[EMB + k] = (float) std::sqrt(std::max(0.0, sq[k] / cnt - m * m));
                }
            }
        }
        return !out.chroma.empty();
    } catch (...) {
        return false;
    }
}

bool save_print(const fs::path & file, const AudioPrint & p) {
    std::ofstream o(file, std::ios::binary | std::ios::trunc);
    if (!o) return false;
    const uint32_t head[3] = { 0x31505341u /* « ASP1 » */, (uint32_t) p.sound.size(), (uint32_t) p.chroma.size() };
    o.write((const char *) head, sizeof(head));
    o.write((const char *) p.sound.data(), (std::streamsize) (p.sound.size() * sizeof(float)));
    o.write((const char *) p.chroma.data(), (std::streamsize) (p.chroma.size() * sizeof(float)));
    return (bool) o;
}

bool load_print(const fs::path & file, AudioPrint & p) {
    std::ifstream in(file, std::ios::binary);
    uint32_t      head[3] = {};
    if (!in.read((char *) head, sizeof(head)) || head[0] != 0x31505341u || head[1] > 100000 || head[2] > 10000000) return false;
    p.sound.resize(head[1]);
    p.chroma.resize(head[2]);
    in.read((char *) p.sound.data(), (std::streamsize) (p.sound.size() * sizeof(float)));
    in.read((char *) p.chroma.data(), (std::streamsize) (p.chroma.size() * sizeof(float)));
    return (bool) in && !p.chroma.empty();
}

float print_sound_similarity(const AudioPrint & a, const AudioPrint & b) {
    if (a.sound.empty() || a.sound.size() != b.sound.size()) return -2.f;
    double ab = 0, aa = 0, bb = 0;
    for (size_t k = 0; k < a.sound.size(); k++) { ab += (double) a.sound[k] * b.sound[k]; aa += (double) a.sound[k] * a.sound[k]; bb += (double) b.sound[k] * b.sound[k]; }
    return aa > 0 && bb > 0 ? (float) (ab / std::sqrt(aa * bb)) : -2.f;
}

float print_melody_similarity(const AudioPrint & a, const AudioPrint & b) {
    const int na = (int) a.chroma.size() / 12, nb = (int) b.chroma.size() / 12, W = 20;  // passage commun d'au moins 20 s
    if (na < W || nb < W) return 0.f;
    // Tonalités essayées : les 2 transpositions qui accordent le mieux les profils moyens (un morceau transposé reste trouvé).
    std::vector<double> pa(12, 0.0), pb(12, 0.0);
    for (int i = 0; i < na; i++) for (int c = 0; c < 12; c++) pa[c] += a.chroma[(size_t) i * 12 + c];
    for (int j = 0; j < nb; j++) for (int c = 0; c < 12; c++) pb[c] += b.chroma[(size_t) j * 12 + c];
    std::vector<std::pair<double, int>> tr;
    for (int t = 0; t < 12; t++) {
        double s = 0;
        for (int c = 0; c < 12; c++) s += pa[c] * pb[(c + t) % 12];
        tr.push_back({ s, t });
    }
    std::sort(tr.rbegin(), tr.rend());
    float best = 0.f;
    std::vector<float> sim((size_t) na * nb);
    for (int r = 0; r < 2; r++) {
        const int t = tr[r].second;
        for (int i = 0; i < na; i++)
            for (int j = 0; j < nb; j++) {
                const float * x = &a.chroma[(size_t) i * 12];
                const float * y = &b.chroma[(size_t) j * 12];
                float d = 0;
                for (int c = 0; c < 12; c++) d += x[c] * y[(c + t) % 12];
                sim[(size_t) i * nb + j] = d;
            }
        // Chaque diagonale (décalage dans le temps) : meilleure moyenne sur W secondes consécutives.
        for (int lag = -(na - W); lag <= nb - W; lag++) {
            const int i0 = std::max(0, -lag), i1 = std::min(na, nb - lag);
            if (i1 - i0 < W) continue;
            double run = 0;
            for (int i = i0; i < i1; i++) {
                run += sim[(size_t) i * nb + i + lag];
                if (i - i0 >= W) run -= sim[(size_t) (i - W) * nb + (i - W) + lag];
                if (i - i0 >= W - 1) best = std::max(best, (float) (run / W));
            }
        }
    }
    return std::clamp(best, 0.f, 1.f);
}