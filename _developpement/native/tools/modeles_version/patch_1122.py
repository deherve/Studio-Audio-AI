# 1.1.022 : trois modèles d'images (Iris-3B Turbo, Qwen-Image-2.1-Turbo, Qwen-Image 2.1). Usage : python -X utf8 patch_1122.py
import re
SRC = r'C:\Users\deher\Downloads\dev\music\_developpement\native\src' + '\\'


def edit(name, pairs):
    p = SRC + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, old[:60], s.count(old))
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)


NEW_MODELS = '''        // 1.1.022 : banc du 26/11/2026 (RX 6700 XT, sd-cli master-1086, 768 × 768, 5 prompts) : Iris 14 s, Qwen Turbo 54 s, Qwen 2.1 158 s ; poids en mémoire vidéo (pas de déchargement).
        // Mage-Flow (int8 convrot) essayé : image grise, format non lu par ce moteur : non ajouté.
        { "iris", "Iris-3B Turbo", "iris-3b",
          { { "speridlabs/iris-3b", "turbo/model.safetensors", "iris-3b-turbo.safetensors", 5975089200ull },
            { "Qwen/Qwen3-VL-4B-Instruct-GGUF", "Qwen3VL-4B-Instruct-Q4_K_M.gguf", "Qwen3VL-4B-Instruct-Q4_K_M.gguf", 2497281664ull } },
          4, 1.0f, 14, 8.1f },
        { "qwenturbo", "Qwen-Image 2.1 Turbo", "qwen-image-2.1-turbo",
          { { "unsloth/Qwen-Image-2.1-Turbo-GGUF", "qwen-image-2.1-turbo-Q4_K_M.gguf", "qwen-image-2.1-turbo-Q4_K_M.gguf", 4199565120ull },
            { "Qwen/Qwen3-VL-8B-Instruct-GGUF", "Qwen3VL-8B-Instruct-Q4_K_M.gguf", "Qwen3VL-8B-Instruct-Q4_K_M.gguf", 5027784800ull },
            { "Comfy-Org/Qwen-Image-2.1", "vae/qwen_image_2.1_vae_bf16.safetensors", "qwen_image_2.1_vae_bf16.safetensors", 675509688ull } },
          8, 1.0f, 54, 8.5f },
        { "qwen21", "Qwen-Image 2.1", "qwen-image-2.1",
          { { "leejet/Qwen-Image-2.1-GGUF", "qwen_image_2.1-Q4_K.gguf", "qwen_image_2.1-Q4_K.gguf", 4197494816ull },
            { "Qwen/Qwen3-VL-8B-Instruct-GGUF", "Qwen3VL-8B-Instruct-Q4_K_M.gguf", "Qwen3VL-8B-Instruct-Q4_K_M.gguf", 5027784800ull },
            { "Comfy-Org/Qwen-Image-2.1", "vae/qwen_image_2.1_vae_bf16.safetensors", "qwen_image_2.1_vae_bf16.safetensors", 675509688ull } },
          20, 4.0f, 158, 8.5f },
'''
ANCHOR = '        // 1.0.58 : un seul fichier (modèle, texte et VAE ensemble)'

edit('ai_writer.cpp', [
    (ANCHOR, NEW_MODELS + ANCHOR),
    # Iris : 2 fichiers (pas de VAE) ; Qwen : échantillonneur euler
    ('''    if (f.size() == 1) return L" -m \\"" + (dir / widen(f[0].file)).wstring() + L"\\" --vae-tiling";
''',
     '''    if (f.size() == 1) return L" -m \\"" + (dir / widen(f[0].file)).wstring() + L"\\" --vae-tiling";
    if (f.size() == 2)  // 1.1.022 : Iris-3B : modèle + encodeur de texte, pas de VAE séparé
        return L" --diffusion-model \\"" + (dir / widen(f[0].file)).wstring() + L"\\" --llm \\"" + (dir / widen(f[1].file)).wstring() + L"\\"";
    if (f[0].file.rfind("qwen", 0) == 0)  // 1.1.022 : Qwen-Image 2.1 : échantillonneur euler (celui du banc)
        return L" --diffusion-model \\"" + (dir / widen(f[0].file)).wstring() + L"\\" --llm \\"" + (dir / widen(f[1].file)).wstring() + L"\\" --vae \\"" +
               (dir / widen(f[2].file)).wstring() + L"\\" --sampling-method euler";
'''),
])

edit('app_aiset.cpp', [
    ('''                else if (std::string(m.id) == "sdxl") muted_wrapped(''',
     '''                else if (std::string(m.id) == "iris") muted_wrapped(tr("Le plus rapide des modèles qui écrivent bien le texte dans l'image (titre, nom du groupe) : environ 14 s par image. Licence Apache 2.0."));
                else if (std::string(m.id) == "qwenturbo") muted_wrapped(tr("Très bon rendu et texte bien écrit dans l'image (accents parfois approximatifs), environ 1 minute par image, 8,5 Go de mémoire vidéo. Licence Qwen : usage commercial à vérifier (tableau des licences dans Réglages › Système)."));
                else if (std::string(m.id) == "qwen21") muted_wrapped(tr("Version complète de Qwen-Image 2.1 : rendu sobre et soigné, mais environ 3 fois plus lent que la version Turbo (2 min 40 par image). Même licence Qwen que la version Turbo."));
                else if (std::string(m.id) == "sdxl") muted_wrapped('''),
    ('''muted_wrapped(tr("Modèles sous licence Apache 2.0 (Z-Image Turbo : Alibaba ; MOSS-Image : OpenMOSS). FLUX.3 klein (Black Forest Labs) : FLUX Kommunity License, voir le tableau des licences."));''',
     '''muted_wrapped(tr("Modèles sous licence Apache 2.0 (Z-Image Turbo : Alibaba ; MOSS-Image : OpenMOSS ; Iris-3B : Sperid Labs). FLUX.3 klein (Black Forest Labs) : FLUX Kommunity License ; Qwen-Image 2.1 : licence Qwen, voir le tableau des licences."));'''),
])

edit('app_models.cpp', [
    ('''"Z-Image Turbo, MOSS-Image", "Apache 2.0", false },''', '''"Z-Image Turbo, MOSS-Image, Iris-3B Turbo", "Apache 2.0", false },
        { N_("Pochettes et clips"), "Qwen-Image 2.1, Qwen-Image 2.1 Turbo", N_("licence Qwen (non commerciale d'après la fiche du modèle : à vérifier avant tout usage commercial)"), true },'''),
])
print('OK')
