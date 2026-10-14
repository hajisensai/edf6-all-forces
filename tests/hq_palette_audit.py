"""The room menu's palette (src/hq_palette.inc, src/room_menu.h kDefaultPalette) against the game, read as files (never
loaded; nothing written): the four CustomColor files are what CONFIG.SGO's SoldierInit names for each class, every class
offers 12 primary / secondary pairs with entries 4..11 shared, and the checked-in table is exactly what
tools/hq_palette.py makes from them now. With EDF.dll 0x678CCB46: the loader 0xDF1B0 names L"CustomColor" and reads the
class's path through 0xE07D0, and the colour-select screen fills 12 chips a channel. Exit 77 (skipped) without the game."""
from __future__ import annotations

import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import hq_palette as hp  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


# The header's side (no game needed).
inc = open(hp.INC, encoding='utf-8').read()
colours = [int(x, 16) for x in re.findall(r'^0x([0-9A-F]{6}),', inc, re.M)]
menu = open(os.path.join(ROOT, 'src', 'room_menu.h'), encoding='utf-8').read()
most = int(re.search(r'kPaletteMost=(\d+);', menu).group(1))
check('#include "hq_palette.inc"' in menu and 'kDefaultPalette=HqPalette()' in menu, 'room_menu.h takes its palette from the table')
check(0 < len(colours) <= most, f'{len(colours)} colours fit kPaletteMost {most}')
check(len(set(colours)) == len(colours), 'no colour twice')
check(len(inc.strip().splitlines()) == 3 + len(colours), 'the table is comments and colours only')

try:
    import rootcpk
    game = rootcpk.default()
    game.read('DEFAULTPACKAGE', 'CONFIG.SGO')
except Exception as e:   # no game on this machine (CI)
    print(f'SKIP: no game ({e})')
    raise SystemExit(77)

files = hp.config_files(game)
check(len(files) == 4 and [f.split('/')[-1].upper() for f in files] == list(hp.CLASS_FILES),
      f'CONFIG.SGO SoldierInit[class][5][0] names the four files in class order: {files}')
check(all(f.upper().startswith('APP:/DEFAULTPACKAGE/') for f in files), 'all in DefaultPackage')
tables = [hp.class_table(game, c) for c in range(4)]
check(all(len(t) == hp.ENTRIES for t in tables), '12 pairs a class')
check(all(t[4:] == tables[0][4:] for t in tables), 'entries 4..11 the same in every class')
check(len({tuple(t[:4]) for t in tables}) == 4, 'entries 0..3 each class\'s own')
want = hp.render(hp.palette(game))
check(inc == want, 'src/hq_palette.inc is what tools/hq_palette.py makes from the game now (run it with --write)')
expected = []
for i in hp.SHARED:
    expected += list(tables[0][i])
for c in hp.SUPPORT_CLASSES:
    for i in hp.OWN:
        expected += list(tables[c][i])
dedup = list(dict.fromkeys(expected))
check(colours == dedup, 'the order: shared pairs, then the Ranger\'s, Wing Diver\'s, Fencer\'s own, first sight kept')
check(set(colours) >= {c for t in (tables[0], tables[1], tables[3]) for pair in t for c in pair},
      'every colour the HQ offers a support soldier\'s class is in the menu')

# EDF.dll (only read): the loader and the screen the RE names.
path = os.path.join(game.root, 'EDF.dll')
try:
    import pefile
    pe = pefile.PE(path, fast_load=True)
    stamp = pe.FILE_HEADER.TimeDateStamp
except Exception as e:
    print(f'hq_palette_audit: EDF.dll not read ({e}); the data checks passed')
    stamp = None
if stamp == 0x678CCB46:
    img = pe.get_memory_mapped_image()
    # 0xDF3D5: lea rdx,[rip+X] -> L"CustomColor", then call 0x528A0 (the node by name).
    check(img[0xDF3D5:0xDF3D8] == bytes.fromhex('488d15'), 'the loader loads a name into rdx')
    name = 0xDF3D5 + 7 + struct.unpack_from('<i', img, 0xDF3D8)[0]
    check(img[name:name + 24].decode('utf-16le') == 'CustomColor\0', 'the loader asks for L"CustomColor"')
    # The loader reaches for the path through 0xE07D0 (SoldierInit[class][5][0]) when given none.
    calls = [a for a in range(0xDF1B0, 0xDF3D5) if img[a] == 0xE8 and a + 5 + struct.unpack_from('<i', img, a + 1)[0] == 0xE07D0]
    check(len(calls) >= 1, 'the loader calls 0xE07D0 for the class\'s colour file')
    # HUiHQColorSelect's draw (vtable 0x1800C40 slot 2 = 0x880F50) loops 12 chips a channel (cmp ..., 0Ch).
    base = pe.OPTIONAL_HEADER.ImageBase
    check(struct.unpack_from('<Q', img, 0x1800C40 + 2 * 8)[0] - base == 0x880F50, 'the colour-select screen\'s draw is 0x880F50')
    # 0x880FFE: call 0x880010 (a chip: ColorChipBox) inside the loop that ends cmp rbx,0Ch (0x88100A).
    check(img[0x880FFE] == 0xE8 and 0x880FFE + 5 + struct.unpack_from('<i', img, 0x880FFF)[0] == 0x880010 and
          img[0x88100A:0x88100E] == bytes.fromhex('4883fb0c'), 'the draw colours a chip 12 times a channel (cmp rbx, 0Ch)')
    print(f'hq_palette_audit: EDF.dll {stamp:#x} checked')
print(f'hq_palette_audit: {checks} checks passed ({len(colours)} colours)')
