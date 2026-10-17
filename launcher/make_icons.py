"""Draws the OpenMware app icon (an "M" on a violet gradient) and writes the Apple asset catalog sizes.
Usage: .venv/Scripts/python.exe -I launcher/make_icons.py     (needs Pillow; the PNGs are checked in)"""
import json
from pathlib import Path
from PIL import Image, ImageDraw

SIZE = 1024
here = Path(__file__).resolve().parent


def draw() -> Image.Image:
    img = Image.new("RGB", (SIZE, SIZE))
    px = img.load()
    top, bottom = (112, 92, 168), (60, 46, 112)
    for y in range(SIZE):
        t = y / (SIZE - 1)
        c = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(SIZE):
            px[x, y] = c
    d = ImageDraw.Draw(img)
    pts = [(300, 690), (300, 340), (512, 570), (724, 340), (724, 690)]
    d.line(pts, fill=(255, 255, 255), width=84, joint="curve")
    for p in (pts[0], pts[-1]):
        d.ellipse([p[0] - 42, p[1] - 42, p[0] + 42, p[1] + 42], fill=(255, 255, 255))
    r = 46
    d.ellipse([512 - r, 570 - r, 512 + r, 570 + r], fill=(255, 201, 222))
    return img


def main() -> None:
    icon = draw()
    cat = here / "apple" / "Resources" / "Assets.xcassets"
    app = cat / "AppIcon.appiconset"
    app.mkdir(parents=True, exist_ok=True)
    (cat / "Contents.json").write_text(json.dumps({"info": {"author": "xcode", "version": 1}}, indent=2))
    images = []
    icon.save(app / "icon_1024.png")
    images.append({"filename": "icon_1024.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"})
    for base in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            px = base * scale
            name = f"mac_{base}@{scale}x.png"
            icon.resize((px, px), Image.LANCZOS).save(app / name)
            images.append({"filename": name, "idiom": "mac", "scale": f"{scale}x", "size": f"{base}x{base}"})
    (app / "Contents.json").write_text(json.dumps({"images": images, "info": {"author": "xcode", "version": 1}}, indent=2))
    (cat / "AccentColor.colorset").mkdir(exist_ok=True)
    (cat / "AccentColor.colorset" / "Contents.json").write_text(json.dumps({
        "colors": [{"idiom": "universal", "color": {"color-space": "srgb", "components": {"red": "0.357", "green": "0.294", "blue": "0.541", "alpha": "1.000"}}}],
        "info": {"author": "xcode", "version": 1}}, indent=2))
    # a 512 px preview for the README / store pages
    icon.resize((512, 512), Image.LANCZOS).save(here / "icon-512.png")


main()
