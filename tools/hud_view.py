"""Draws what tools/hud_view.cpp recorded (DIR/<scene>.txt: 'T' triangles, 'S' text lines; DIR/<language>/<scene>.txt for
the HUD's other languages) into a PNG beside each, over a grey sky and ground, to look at the HUD's layout without the
game. The text is drawn at the recorded size in the game's own fonts when the game is here (its four fonts, read from
Root.cpk without writing anything, each character from the first of them that has it, in the order the game loads them
for the scene's language: docs/hud-re.md §11), else in Windows' own (Consolas, Microsoft YaHei, Yu Gothic).

    python tools/hud_view.py DIR
"""
import io
import os
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))

# The game's fonts (UI/*.TTF in Root.cpk) and the order its font loader (EDF.dll 0x963830) tries them per language.
GAME_FONTS = {'jp': 'TT_NEWCEZANNE-DB.TTF', 'kr': 'ARUDJINGXIHEIKR_BD.TTF', 'tc': 'ARUDJINGXIHEIB5_BD.TTF',
              'sc': 'ARUDJINGXIHEIG30_BD.TTF'}
ORDER = {'en': ['jp', 'kr', 'tc', 'sc'], 'ja': ['jp', 'kr', 'tc', 'sc'], 'zh-TW': ['tc', 'sc', 'jp', 'kr'],
         'zh-CN': ['sc', 'tc', 'jp', 'kr']}
SYSTEM_FONTS = {'en': ['consola.ttf', 'msyh.ttc'], 'ja': ['YuGothM.ttc', 'msgothic.ttc', 'msyh.ttc'],
                'zh-CN': ['msyh.ttc', 'consola.ttf'], 'zh-TW': ['msjh.ttc', 'msyh.ttc', 'consola.ttf']}


def game_fonts() -> dict[str, bytes]:
    """The game's four fonts' bytes ({} when the game or a reader is not here)."""
    try:
        import cpk
        import crilayla
        game = Path(os.environ.get('EDF6_DIR', r'D:\steam\steamapps\common\EARTH DEFENSE FORCE 6'))
        archive = cpk.Cpk(str(game / 'Root.cpk'))
        out = {}
        for key, name in GAME_FONTS.items():
            entry = archive.index[('UI', name)]
            with open(archive.path, 'rb') as handle:
                handle.seek(archive.base + int(entry['FileOffset']))
                raw = handle.read(int(entry['FileSize']))
            out[key] = crilayla.decompress(raw) if int(entry['ExtractSize']) != int(entry['FileSize']) else raw
        return out
    except Exception as e:  # noqa: BLE001 - no game, no reader: the system's fonts
        print('the game\'s fonts not read (%s): Windows\' fonts' % e)
        return {}


class Chain:
    """A language's fonts in order, each character drawn in the first that has it."""

    def __init__(self, lang: str, fonts: dict[str, bytes]):
        from fontTools.ttLib import TTFont
        self.sources = []   # (bytes or path, its cmap)
        if fonts:
            for key in ORDER.get(lang, ORDER['en']):
                data = fonts[key]
                self.sources.append((data, set(TTFont(io.BytesIO(data), lazy=True).getBestCmap())))
        else:
            for name in SYSTEM_FONTS.get(lang, SYSTEM_FONTS['en']):
                path = Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Fonts' / name
                if path.exists():
                    try:
                        cmap = set(TTFont(str(path), fontNumber=0, lazy=True).getBestCmap())
                    except Exception:  # noqa: BLE001
                        continue
                    self.sources.append((str(path), cmap))
        self.cache: dict[tuple[int, int], ImageFont.FreeTypeFont] = {}

    def font(self, index: int, size: int) -> ImageFont.FreeTypeFont:
        key = (index, size)
        if key not in self.cache:
            src = self.sources[index][0]
            self.cache[key] = ImageFont.truetype(io.BytesIO(src) if isinstance(src, bytes) else src, size)
        return self.cache[key]

    def runs(self, text: str) -> list[tuple[int, str]]:
        out: list[tuple[int, str]] = []
        for ch in text:
            index = next((i for i, (_, cmap) in enumerate(self.sources) if ord(ch) in cmap), 0)
            if out and out[-1][0] == index:
                out[-1] = (index, out[-1][1] + ch)
            else:
                out.append((index, ch))
        return out


def rgba(parts: list[str]) -> tuple[int, int, int, int]:
    return tuple(max(0, min(255, round(float(p) * 255))) for p in parts)  # type: ignore[return-value]


def text(d: ImageDraw.ImageDraw, chain: Chain, x: float, y: float, size: int, s: str, fill: tuple[int, int, int, int]) -> None:
    if not chain.sources:
        d.text((x, y), s, font=ImageFont.load_default(), fill=fill)
        return
    for index, run in chain.runs(s):
        font = chain.font(index, size)
        for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            d.text((x + dx, y + dy), run, font=font, fill=(0, 0, 0, 255))
        d.text((x, y), run, font=font, fill=fill)
        x += font.getlength(run)


def draw(src: Path, chain: Chain) -> Path:
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
            # The recorded size is a line's (kGlyphH a unit of font scale); the glyphs are some 0.8 of it.
            size = max(8, round(float(f[3]) * 0.8))
            text(d, chain, float(f[1]), float(f[2]), size, ' '.join(f[8:]), rgba(f[4:8]))
    img = Image.alpha_composite(img, over)
    out = src.with_suffix('.png')
    img.convert('RGB').save(out)
    return out


def main() -> None:
    top = Path(sys.argv[1])
    fonts = game_fonts()
    for folder in [top] + sorted(p for p in top.iterdir() if p.is_dir()):
        lang = 'en' if folder == top else folder.name
        chain = Chain(lang, fonts)
        for src in sorted(folder.glob('*.txt')):
            print(draw(src, chain))


if __name__ == '__main__':
    main()
