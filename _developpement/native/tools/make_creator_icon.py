# Fabrique l'icône du Créateur de styles à partir de celle du Studio.
#
# Reprend native\src\app.ico (6 tailles : 16, 24, 32, 48, 64, 256) et ajoute en bas à droite une pastille ronde
# vert d'eau portant une croche violet foncé : on reconnaît le Studio, et la pastille distingue l'outil sans cacher
# la double croche blanche d'origine. Résultat : native\createur\createur.ico, utilisé par native\createur\createur.rc
# (cible CreateurStyles du CMakeLists).
#
# Utilisation (Pillow requis) :   python native\tools\make_creator_icon.py
# À relancer seulement si l'icône du Studio change ; createur.ico est livré tout fait.
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

NATIVE = Path(__file__).resolve().parent.parent
SOURCE = NATIVE / "src" / "app.ico"
TARGET = NATIVE / "createur" / "createur.ico"

BIG = 1024  # la pastille est dessinée en grand puis réduite : bords lisses à toutes les tailles

INK = (34, 22, 92, 255)          # violet foncé (contour et note)
MINT_TOP = (153, 246, 228)       # vert d'eau clair (haut de la pastille)
MINT_BOTTOM = (20, 184, 166)     # vert d'eau soutenu (bas)


def note_mask(size):
    """Silhouette d'une croche (blanc sur noir), centrée dans un carré de côté size."""
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    u = size / 100.0
    # Tête de la note : ellipse penchée
    head = Image.new("L", (size, size), 0)
    ImageDraw.Draw(head).ellipse([24 * u, 60 * u, 54 * u, 82 * u], fill=255)
    head = head.rotate(20, center=(39 * u, 71 * u), resample=Image.BICUBIC)
    m.paste(255, (0, 0), head)
    # Hampe
    d.rectangle([48 * u, 20 * u, 55 * u, 72 * u], fill=255)
    # Crochet (drapeau) : deux arcs pleins
    d.polygon([(55 * u, 20 * u), (72 * u, 32 * u), (78 * u, 46 * u), (74 * u, 58 * u),
               (70 * u, 46 * u), (63 * u, 38 * u), (55 * u, 35 * u)], fill=255)
    return m


def badge_layer(size):
    """Pastille ronde vert d'eau en dégradé, contour violet foncé, croche violet foncé, petite ombre."""
    disc = Image.new("L", (size, size), 0)
    pad = size * 0.06
    ImageDraw.Draw(disc).ellipse([pad, pad, size - pad, size - pad], fill=255)
    outline = disc.filter(ImageFilter.MaxFilter(size // 30 * 2 + 1))  # contour épais
    shadow = outline.filter(ImageFilter.GaussianBlur(size / 50))

    layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    dark = Image.new("RGBA", (size, size), (20, 10, 60, 150))
    layer.paste(dark, (size // 80, size // 50), shadow)       # ombre légère vers le bas
    layer.paste(Image.new("RGBA", (size, size), INK), (0, 0), outline)  # contour
    mint = Image.new("RGBA", (size, size))
    md = ImageDraw.Draw(mint)
    for y in range(size):
        t = y / (size - 1)
        md.line([(0, y), (size, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(MINT_TOP, MINT_BOTTOM)) + (255,))
    layer.paste(mint, (0, 0), disc)
    # Croche, un peu réduite pour laisser de l'air dans la pastille
    note = note_mask(size).resize((int(size * 0.80), int(size * 0.80)), Image.LANCZOS)
    full = Image.new("L", (size, size), 0)
    off = (size - note.size[0]) // 2
    full.paste(note, (off - size // 40, off))
    layer.paste(Image.new("RGBA", (size, size), INK), (0, 0), full)
    return layer


def compose(base, layer, scale):
    """Pose la pastille (réduite à scale × la taille de l'icône) dans le coin bas droit."""
    s = base.size[0]
    k = max(8, round(s * scale))
    small = layer.resize((k, k), Image.LANCZOS)
    out = base.copy()
    out.alpha_composite(small, (s - k, s - k))
    return out


def main():
    if not SOURCE.exists():
        sys.exit(f"Icône du Studio introuvable : {SOURCE}")
    ico = Image.open(SOURCE)
    sizes = sorted(ico.info.get("sizes", {(256, 256)}))
    layer = badge_layer(BIG)
    images = []
    for w, h in sizes:
        base = ico.ico.getimage((w, h)).convert("RGBA")
        # Petites tailles : pastille plus grande, sinon elle ne se voit plus ; grandes : plus petite, la double
        # croche du Studio reste bien visible.
        scale = 0.66 if w <= 24 else 0.56 if w <= 48 else 0.50
        images.append(compose(base, layer, scale))
    images.sort(key=lambda im: im.size[0], reverse=True)
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    images[0].save(TARGET, format="ICO", sizes=[im.size for im in images], append_images=images[1:])
    print(f"{TARGET} : {', '.join(str(im.size[0]) for im in reversed(images))} px")


if __name__ == "__main__":
    main()
