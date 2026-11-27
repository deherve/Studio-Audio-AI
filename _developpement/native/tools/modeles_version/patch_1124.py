# 1.1.024 : images de clip en 1024 x 576 (case « clip_fast_images », cochée par défaut). Usage : python -X utf8 patch_1124.py
SRC = r'C:\Users\deher\Downloads\dev\music\_developpement\native\src' + '\\'


def edit(name, pairs):
    p = SRC + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new, n in pairs:
        assert s.count(old) == n, (name, old[:70], s.count(old))
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)


edit('media.hpp', [('    bool                    same_seed = false;',
                    '    bool                    fast_images = false;  // 1.1.024 : images de scène en 1024 × 576 (au lieu de 1344 × 768)\n    bool                    same_seed = false;', 1)])
edit('media.cpp', [
    ('r = slot.server->generate(sb.scenes[k].prompt, 1344, 768, png, &j.cancel, seed);',
     'r = slot.server->generate(sb.scenes[k].prompt, eng.fast_images ? 1024 : 1344, eng.fast_images ? 576 : 768, png, &j.cancel, seed);', 1),
    ('sb.scenes[k].prompt, 1344, 768, png, &j.cancel, eng.offload, seed, ref);',
     'sb.scenes[k].prompt, eng.fast_images ? 1024 : 1344, eng.fast_images ? 576 : 768, png, &j.cancel, eng.offload, seed, ref);', 1)])
edit('app.hpp', [
    ('clip_end_box = "0", clip_same_seed = "0";', 'clip_end_box = "0", clip_same_seed = "0", clip_fast_images = "1";  // 1.1.024 : images 1024 × 576', 1),
    ('{ "clip_same_seed", &s.clip_same_seed },  // 1.0.98', '{ "clip_same_seed", &s.clip_same_seed }, { "clip_fast_images", &s.clip_fast_images },  // 1.0.98, 1.1.024', 2),
])
NEWBOX = '''        {  // 1.1.024 : taille des images de scène (mesure du 26/11/2026 : klein 38 s en 1024 × 576, 74 s en 1344 × 768)
            bool fast = settings.clip_fast_images != "0";
            if (ImGui::Checkbox(tr("Images du clip en 1024 × 576 (rapide)"), &fast)) {
                settings.clip_fast_images = fast ? "1" : "0";
                save_settings(settings, settings_path);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr("Les images de scène sont créées en 1024 × 576 puis agrandies par le montage : environ 2 fois plus rapide qu'en 1344 × 768, un peu moins de détail (surtout dans les vidéos verticales). Ne change que les nouvelles images."));
        }
'''
edit('app_media.cpp', [
    ('            scene_engine.same_seed   = settings.clip_same_seed == "1";  // 1.0.98',
     '            scene_engine.same_seed   = settings.clip_same_seed == "1";  // 1.0.98\n            scene_engine.fast_images = settings.clip_fast_images != "0";  // 1.1.024', 1),
    ("        {  // 1.0.98 (atelier clip, point 8) : graine commune aux images d'un même clip",
     NEWBOX + "        {  // 1.0.98 (atelier clip, point 8) : graine commune aux images d'un même clip", 1),
])
print('ok')
