# 1.1.016 : image de référence d'un groupe (FLUX.3 klein : option -r de sd-cli) pour les pochettes et les images des clips
import sys
SRC = r'C:\Users\deher\Downloads\dev\music\_developpement\native\src'


def patch(name, pairs):
    p = SRC + '\\' + name
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl)
        b = b.replace('\n', nl)
        if s.count(a) != 1:
            print('ERREUR', name, s.count(a), a[:90])
            sys.exit(1)
        s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='').write(s)


# --- studio : image de référence d'un groupe
patch('studio.hpp', [
    ('''fs::path            album_folder(const std::string & artist, const std::string & album);  // album vide : dossier du groupe''',
     '''fs::path            album_folder(const std::string & artist, const std::string & album);  // album vide : dossier du groupe
// 1.1.016 : image de référence du groupe (<groupe>\\reference_visuelle.png / .jpg / .webp), vide s'il n'y en a pas.
fs::path            group_reference(const std::string & artist);
// Copie « image » dans le dossier du groupe comme image de référence (remplace l'ancienne) ; faux en cas d'échec.
bool                set_group_reference(const std::string & artist, const fs::path & image);'''),
])
patch('studio.cpp', [
    ('''fs::path song_folder(const std::string & id) {''', '''fs::path group_reference(const std::string & artist) {
    const fs::path dir = album_folder(artist, std::string());
    std::error_code ec;
    for (const wchar_t * ext : { L".png", L".jpg", L".jpeg", L".webp" }) {
        const fs::path f = dir / (std::wstring(L"reference_visuelle") + ext);
        if (fs::exists(f, ec)) return f;
    }
    return {};
}

bool set_group_reference(const std::string & artist, const fs::path & image) {
    const fs::path dir = album_folder(artist, std::string());
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::wstring ext = image.extension().wstring();
    for (auto & c : ext) c = (wchar_t) towlower(c);
    for (const wchar_t * old : { L".png", L".jpg", L".jpeg", L".webp" }) fs::remove(dir / (std::wstring(L"reference_visuelle") + old), ec);
    fs::copy_file(image, dir / (std::wstring(L"reference_visuelle") + ext), fs::copy_options::overwrite_existing, ec);
    return !ec;
}

fs::path song_folder(const std::string & id) {'''),
])

# --- sd-cli : option -r
patch('ai_writer.hpp', [
    ('''                            int width, int height, const fs::path & out_png, std::atomic<bool> * cancel, bool offload = false,
                            long long seed = -1);  // 1.0.98 : graine (-1 = au hasard)''',
     '''                            int width, int height, const fs::path & out_png, std::atomic<bool> * cancel, bool offload = false,
                            long long seed = -1,  // 1.0.98 : graine (-1 = au hasard)
                            const fs::path & reference = fs::path());  // 1.1.016 : image de référence (FLUX.3 klein seulement ; ignorée sinon)'''),
])
patch('ai_writer.cpp', [
    ('''                            int width, int height, const fs::path & out_png, std::atomic<bool> * cancel, bool offload, long long seed) {''',
     '''                            int width, int height, const fs::path & out_png, std::atomic<bool> * cancel, bool offload, long long seed,
                            const fs::path & reference) {'''),
    ('''    std::string    p   = prompt;
    for (char & c : p) if (c == '"' || c == '\\r' || c == '\\n') c = c == '"' ? '\\'' : ' ';''',
     '''    std::string    p   = prompt;
    for (char & c : p) if (c == '"' || c == '\\r' || c == '\\n') c = c == '"' ? '\\'' : ' ';
    // 1.1.016 : image de référence (mode « édition » de FLUX.3 : -r) : même style et mêmes sujets que l'image donnée
    std::wstring ref_arg;
    {
        std::error_code rec;
        if (std::string(m.id) == "klein" && !reference.empty() && fs::exists(reference, rec)) {
            ref_arg = L" -r \\"" + reference.wstring() + L"\\"";
            p       = "Keep the same visual identity, style and main subject as the reference image. " + p;
        }
    }'''),
    ('''    std::wstring cmd = L"\\"" + sd_cli.wstring() + L"\\"" + weights +
                       (offload ? L" --offload-to-cpu" : L"") + L" -p \\"" + widen(p) + L"\\" --cfg-scale " + cfg + L" --steps " +''',
     '''    std::wstring cmd = L"\\"" + sd_cli.wstring() + L"\\"" + weights + ref_arg +
                       (offload ? L" --offload-to-cpu" : L"") + L" -p \\"" + widen(p) + L"\\" --cfg-scale " + cfg + L" --steps " +'''),
])

# --- clips : une image à la fois par sd-cli quand le groupe a une image de référence
patch('media.cpp', [
    ('''            ImageServerSlot & slot = *eng.images;
            if (!slot.server && !slot.failed) {''', '''            ImageServerSlot & slot = *eng.images;
            // 1.1.016 : image de référence du groupe (FLUX.3 klein) : le serveur d'images ne la gère pas, sd-cli image par image
            const fs::path ref = std::string(eng.image_model->id) == "klein" ? group_reference(s.artist) : fs::path();
            if (!ref.empty()) {
                slot.server.reset();
                journal::event("clip", "Image de scène avec référence du groupe : " + narrow(ref.wstring()));
            }
            if (ref.empty() && !slot.server && !slot.failed) {'''),
    ('''            AiCallResult r;
            if (slot.server) r = slot.server->generate(sb.scenes[k].prompt, 1344, 768, png, &j.cancel, seed);
            if (!r.ok && !j.cancel) {
                slot.server.reset();  // libère la mémoire vidéo pour sd-cli
                slot.failed = true;
                r = ai_image_local(eng.sd_cli, eng.image_root, *eng.image_model, sb.scenes[k].prompt, 1344, 768, png, &j.cancel, eng.offload, seed);
            }''', '''            AiCallResult r;
            if (slot.server) r = slot.server->generate(sb.scenes[k].prompt, 1344, 768, png, &j.cancel, seed);
            if (!r.ok && !j.cancel) {
                slot.server.reset();  // libère la mémoire vidéo pour sd-cli
                if (ref.empty()) slot.failed = true;
                r = ai_image_local(eng.sd_cli, eng.image_root, *eng.image_model, sb.scenes[k].prompt, 1344, 768, png, &j.cancel, eng.offload, seed, ref);
            }'''),
])

# --- pochettes : image de référence du groupe de chaque album
patch('app.hpp', [
    ('''        std::string model;  // 1.0.51 : modèle local choisi pour cette fois (« klein », « zimage », « moss ») ; vide = Réglages
    };
    struct CoverJob {''', '''        std::string model;  // 1.0.51 : modèle local choisi pour cette fois (« klein », « zimage », « moss ») ; vide = Réglages
        fs::path    reference;  // 1.1.016 : image de référence du groupe (vide = aucune)
    };
    struct CoverJob {'''),
])
patch('app_cover.cpp', [
    ('''            AiCallResult r = job->local ? ai_image_local(sd, dir, model, t.prompt, t.w, t.h, tmp / (widen(name) + L".png"), &job->cancel, offload)''',
     '''            AiCallResult r = job->local ? ai_image_local(sd, dir, model, t.prompt, t.w, t.h, tmp / (widen(name) + L".png"), &job->cancel, offload, -1, t.reference)'''),
])
print('OK')
