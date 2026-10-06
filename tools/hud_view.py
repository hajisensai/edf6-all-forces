"""Draws what tools/hud_view.cpp recorded (DIR/<scene>.txt: 'T' triangles, 'S' text lines) into DIR/<scene>.png over a
grey sky and ground, to look at the HUD's layout without the game. The text is a stand-in font at the recorded size.

    python tools/hud_view.py DIR
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def rgba(parts: list[str]) -> tuple[int, int, int, int]:
    return tuple(max(0, min(255, round(float(p) * 255))) for p in parts)  # type: ignore[return-value]


def draw(src: Path) -> Path:
    lines = src.read_text(encoding='utf-8').splitlines()
    w, h = (int(v) for v in lines[0].split()[1:3])
    img = Image.new('RGBA', (w, h), (92, 110, 130, 255))
    ImageDraw.Draw(img).rectangle([0, h * 0.62, w, h], fill=(88, 84, 70, 255))
    over = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    for line in lines[1:]:
        f = line.split(' ')
        if f[0] == 'T':
            xy = [(float(f[1]), float(f[2])), (float(f[3]), float(f[4])), (float(f[5]), float(f[6]))]
            d.polygon(xy, fill=rgba(f[7:11]))
        elif f[0] == 'S':
            size = max(8, round(float(f[3]) * 0.8))
            try:
                font = ImageFont.truetype('consola.ttf', size)
            except OSError:
                font = ImageFont.load_default()
            x, y, text = float(f[1]), float(f[2]), ' '.join(f[8:])
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                d.text((x + dx, y + dy), text, font=font, fill=(0, 0, 0, 255))
            d.text((x, y), text, font=font, fill=rgba(f[4:8]))
    img = Image.alpha_composite(img, over)
    out = src.with_suffix('.png')
    img.convert('RGB').save(out)
    return out


def main() -> None:
    for src in sorted(Path(sys.argv[1]).glob('*.txt')):
        print(draw(src))


if __name__ == '__main__':
    main()
