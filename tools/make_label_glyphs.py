"""Add labels to the physical cockpit's prebaked glyph masks (pylib/cockpit_label_glyphs.py) without a font at runtime.

The masks the file already has stay as they are; a glyph a new label needs is rendered here once from a Windows font
(Microsoft YaHei Bold 20 px, threshold 128: the face and size the existing masks match) as (advance width, row
bitmasks, bit 0 the left column): a CJK glyph cropped to the full-height box of a reference ideograph, a Latin one to
its cap height, so a label's glyphs keep one top line (cockpit_instruments.label lays rows down from the top). The
masks are written into the marked block at the end of the file; the font itself is not redistributed.

    python tools/make_label_glyphs.py [--check]      (--check: exit 1 when the file's block is not what this makes)
"""
from __future__ import annotations

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TARGET = os.path.join(HERE, '..', 'pylib', 'cockpit_label_glyphs.py')
FONT = os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts', 'msyhbd.ttc')
SIZE, THRESHOLD = 20, 128
BEGIN = '# --- weapons screen labels (tools/make_label_glyphs.py) ---'
END = '# --- end of weapons screen labels ---'

# English, zh-CN, zh-TW, ja (hudtext::Lang). Shell tags and row numbers need no translation: their glyphs only.
LABELS = {
    'RELOAD': ('LOADING', '装填中', '裝填中', '装填中'),
    'EMPTY': ('EMPTY', '弹尽', '彈盡', '弾切れ'),
}
NEUTRAL = 'APHEBMTCN1234'


def masks(chars: str) -> dict[str, tuple[int, tuple[int, ...]]]:
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype(FONT, SIZE)

    def ink(ch: str):  # noqa: ANN202
        width = int(round(font.getlength(ch)))
        img = Image.new('L', (width + SIZE, SIZE * 3))
        ImageDraw.Draw(img).text((0, SIZE // 2), ch, font=font, fill=255)
        px = img.load()
        rows = [sum(1 << x for x in range(img.width) if px[x, y] >= THRESHOLD) for y in range(img.height)]
        lit = [y for y, r in enumerate(rows) if r]
        return width, rows, (lit[0], lit[-1]) if lit else (0, -1)

    _w, _r, cjk = ink('龍')
    _w, _r, cap = ink('H')
    out = {}
    for ch in chars:
        width, rows, _box = ink(ch)
        top, bottom = cjk if ord(ch) > 0x2FFF else cap
        out[ch] = (width, tuple(rows[top:bottom + 1]))
    return out


def block(existing: dict) -> str:
    wanted = ''.join(dict.fromkeys(''.join(''.join(v) for v in LABELS.values()) + NEUTRAL))
    missing = ''.join(ch for ch in wanted if ch not in existing and ch != ' ')
    lines = [BEGIN, 'LABELS.update({' + ', '.join(f'{k!r}: {v!r}' for k, v in LABELS.items()) + '})', 'GLYPHS.update({']
    lines += [f'    {ch!r}: {m!r},' for ch, m in masks(missing).items()]
    lines += ['})', END]
    return '\n'.join(lines) + '\n'


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    with open(TARGET, encoding='utf-8') as h:
        text = h.read()
    head = text.split(BEGIN)[0]
    scope: dict = {}
    exec(compile(head, TARGET, 'exec'), scope)   # the glyphs the file has without this block
    new = head + block(scope['GLYPHS'])
    if args.check:
        if new != text:
            print(f'{TARGET}: its weapons screen labels are not what tools/make_label_glyphs.py makes')
            return 1
        return 0
    with open(TARGET, 'w', encoding='utf-8', newline='\n') as h:
        h.write(new)
    print(TARGET)
    return 0


if __name__ == '__main__':
    sys.exit(main())
