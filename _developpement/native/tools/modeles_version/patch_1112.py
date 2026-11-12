# 1.1.012 : FLUX.3 klein remplace FLUX.2 klein dans la liste des modèles d'images (ai_writer.cpp, main.cpp)
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
            print('ERREUR', name, s.count(a), a[:80])
            sys.exit(1)
        s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='').write(s)


patch('ai_writer.cpp', [
    ('''        { "klein", "FLUX.2 klein 4B", "flux2-klein",
          { { "leejet/FLUX.2-klein-4B-GGUF", "flux-2-klein-4b-Q4_0.gguf", "flux-2-klein-4b-Q4_0.gguf", 2460378560ull },
            { "unsloth/Qwen3-4B-GGUF", "Qwen3-4B-Q4_K_M.gguf", "Qwen3-4B-Q4_K_M.gguf", 2497281312ull },
            { "unsloth/MOSS-Image-SFT-FP8", "vae/diffusion_pytorch_model.safetensors", "flux2_vae.safetensors", 249521372ull } },
          4, 1.0f, 14, 5.9f },''',
     '''        // 1.1.012 : FLUX.3 klein (5,6 Md de paramètres, Black Forest Labs, 15/10/2026) remplace FLUX.2 klein 4B : même identifiant « klein »
        // (réglages et clips inchangés), nouveau dossier ; encodeur de texte Qwen3-VL 4B, même VAE FLUX.2 ; 16 étapes, cfg 1,0. Licence
        // FLUX Kommunity (pas Apache). Banc du 12/11/2026 (RX 6700 XT, sd-cli master-1086) : 43 s en 768 × 768, graphe déchargé en RAM.
        { "klein", "FLUX.3 klein", "flux3-klein",
          { { "leejet/flux-3-image-klein-GGUF", "flux3-image-klein-Q4_0.gguf", "flux3-image-klein-Q4_0.gguf", 3514023104ull },
            { "Qwen/Qwen3-VL-4B-Instruct-GGUF", "Qwen3VL-4B-Instruct-Q4_K_M.gguf", "Qwen3VL-4B-Instruct-Q4_K_M.gguf", 2497281664ull },
            { "unsloth/MOSS-Image-SFT-FP8", "vae/diffusion_pytorch_model.safetensors", "flux2_vae.safetensors", 249521372ull } },
          16, 1.0f, 43, 6.0f },'''),
])

patch('main.cpp', [
    ('''            { N_("Pochettes et clips"), "FLUX.2 klein 4B, Z-Image Turbo, MOSS-Image", "Apache 2.0", false },''',
     '''            { N_("Pochettes et clips"), "Z-Image Turbo, MOSS-Image", "Apache 2.0", false },
            { N_("Pochettes et clips"), "FLUX.3 klein", N_("FLUX Kommunity : commercial seulement sous 5 M$ de revenus"), true },'''),
    ('''muted_wrapped(tr("Le plus rapide, très bon rendu, titre bien écrit dans l'image ; tient sur une carte de 8 Go."));''',
     '''muted_wrapped(tr("Très beau rendu, texte bien écrit dans l'image, mais environ trois fois plus lent que l'ancien FLUX.2 klein. Licence FLUX Kommunity (voir le tableau des licences dans Réglages › Système)."));'''),
    ('''tr("Modèles sous licence Apache 2.0 (FLUX.2 klein : Black Forest Labs ; Z-Image Turbo : Alibaba ; MOSS-Image : OpenMOSS)."));''',
     '''tr("Modèles sous licence Apache 2.0 (Z-Image Turbo : Alibaba ; MOSS-Image : OpenMOSS). FLUX.3 klein (Black Forest Labs) : FLUX Kommunity License, voir le tableau des licences."));'''),
])
print('OK')
