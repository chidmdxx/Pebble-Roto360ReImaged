"""Builds the appstore banners and icons.

Usage: python tools/make_store_assets.py <screenshot dir> <output dir>

The screenshot dir must hold <platform>-1.png for each platform. Banners
(720x320) reuse that emulator screenshot; the 80x80 and
144x144 icons are drawn from scratch. Text uses Bebas Neue (OFL) from
app/resources/fonts.
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "app", "resources", "fonts", "BebasNeue-Regular.ttf")
PLATFORMS = ("gabbro", "chalk", "emery", "basalt")
SIZE = (720, 320)
BACKGROUND = (0, 30, 65)
ACCENT = (85, 170, 255)


def round_mask(size):
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, size[0] - 1, size[1] - 1), fill=255)
    return mask


def banner(platform, shots_dir):
    canvas = Image.new("RGB", SIZE, BACKGROUND)
    draw = ImageDraw.Draw(canvas)
    round_screen = platform in ("gabbro", "chalk")

    # Native size keeps the pixels crisp; scaling small screens up blurs them.
    shot = Image.open(os.path.join(shots_dir, f"{platform}-1.png")).convert("RGB")
    x = SIZE[0] - 40 - shot.width
    y = (SIZE[1] - shot.height) // 2
    canvas.paste(shot, (x, y), round_mask(shot.size) if round_screen else None)

    title = ImageFont.truetype(FONT, 80)
    subtitle = ImageFont.truetype(FONT, 30)
    draw.text((40, 62), "ROTO360", font=title, fill=(255, 255, 255))
    draw.text((40, 140), "REIMAGED", font=title, fill=ACCENT)
    draw.text((42, 236), "A ROTATING DIAL WATCHFACE", font=subtitle, fill=(200, 200, 200))
    return canvas


def icon(size):
    """Draws a miniature dial at 8x and downsamples it for clean edges."""
    s = size * 8
    c = s / 2
    image = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    def disc(radius, fill):
        draw.ellipse((c - radius, c - radius, c + radius, c + radius), fill=fill)

    def sector(outer, half_deg, fill):
        box = (c - outer, c - outer, c + outer, c + outer)
        draw.pieslice(box, -90 - half_deg, -90 + half_deg, fill=fill)

    r = s / 2
    disc(r, (85, 85, 85))
    disc(r * 0.92, (0, 0, 0))
    disc(r * 0.58, (85, 85, 85))
    disc(r * 0.36, (0, 0, 0))

    # Shield: stack the wedges, then restore the centre disc over their tips.
    sector(r, 22, (0, 85, 170))
    sector(r * 0.92, 22, (0, 30, 65))
    sector(r * 0.58, 16, (0, 85, 170))
    disc(r * 0.36, (0, 0, 0))

    font = ImageFont.truetype(FONT, int(r * 0.42))
    draw.text((c, c - r * 0.75), "08", font=font, fill="white", anchor="mm")
    small = ImageFont.truetype(FONT, int(r * 0.22))
    draw.text((c, c - r * 0.47), "20", font=small, fill="white", anchor="mm")

    # A few rotated hour numerals around the ring hint at the rotation.
    for hour, deg in ((9, 60), (7, -60), (10, 120), (6, -120), (11, 180)):
        label = Image.new("RGBA", (int(r * 0.6), int(r * 0.4)), (0, 0, 0, 0))
        ImageDraw.Draw(label).text((label.width / 2, label.height / 2), f"{hour:02d}",
                                   font=font, fill="white", anchor="mm")
        label = label.rotate(-deg, resample=Image.BICUBIC, expand=True)
        rad = math.radians(deg)
        px = c + math.sin(rad) * r * 0.75 - label.width / 2
        py = c - math.cos(rad) * r * 0.75 - label.height / 2
        image.alpha_composite(label, (int(px), int(py)))

    # Store icons are opaque, so the dial sits on the banner background.
    small_icon = image.resize((size, size), Image.LANCZOS)
    opaque = Image.new("RGB", (size, size), BACKGROUND)
    opaque.paste(small_icon, (0, 0), small_icon)
    return opaque


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    shots_dir, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    for platform in PLATFORMS:
        banner(platform, shots_dir).save(os.path.join(out_dir, f"banner-{platform}.png"))
    for size in (80, 144):
        icon(size).save(os.path.join(out_dir, f"icon-{size}.png"))


if __name__ == "__main__":
    main()
