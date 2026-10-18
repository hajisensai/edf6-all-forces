"""The in-game loadout menu's layouts (tools/room_layout.py): parsed back, every element the plugin looks for is there
under BaseFrame, every stock member reads back unchanged and the file without the menu is the stock file byte for byte
(LYT_HUIONLINEROOM and LYT_HUIHQMAIN from the game's Root.cpk, only read); LYT_MAINFRAME is never produced; the panel
stays left of the 3D view; the names, row and status counts agree with src/room_ui_logic.h, and every icon it names is in
Root.cpk. Nothing in the added subtree draws by itself (no plugin, or a plugin that refused after a game update, must
leave the screens stock): every skin is Transparent or the empty-texture HUDIconTexture skin (both read back from
Root.cpk), every text is empty; the white texture the plugin tints is a plain opaque DDS in the stock 32-bit format. The
ledger side (install over our own file, another mod's left alone, install_missing new files only, remove)
on a temporary directory. Nothing is written to the game."""
from __future__ import annotations

import os
import re
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import ledger  # noqa: E402
import room_layout as rl  # noqa: E402
import sgo  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def read(path: str) -> bytes:
    with open(path, 'rb') as f:
        return f.read()


# --- The header's side ---
header = open(os.path.join(ROOT, 'src', 'room_ui_logic.h'), encoding='utf-8').read()
check(int(re.search(r'kRows=(\d+);', header).group(1)) == rl.ROWS, 'kRows == ROWS')
check(int(re.search(r'kStatusLines=(\d+);', header).group(1)) == rl.STATUS_LINES, 'kStatusLines == STATUS_LINES')
for const, name in (('kRootName', rl.ROOT), ('kBackName', rl.BACK), ('kHeadName', rl.HEAD), ('kHintName', rl.HINT),
                    ('kHelp2Name', rl.HELP2), ('kHelp3Name', rl.HELP3), ('kEntryName', rl.ENTRY)):
    check(re.search(const + r'=L"(\w+)"', header).group(1) == name, f'{const} == {name}')
parts = dict(re.findall(r"(text|primary|secondary|icon|note)=L'(\w)'", header))
check(parts == {'text': 'T', 'primary': 'A', 'secondary': 'B', 'icon': 'I', 'note': 'N'} and
      tuple(parts[p] for p in ('text', 'primary', 'secondary', 'icon', 'note')) == rl.ROW_PARTS, 'row parts agree')
check("Name(out,L'U',i,2,0)" in header and "Name(out,L'R',i,2," in header and "Name(out,L'S',i,1,0)" in header,
      'the header spells vcU00 / vcR00x / vcS0 as room_layout.py does')
check(rl.unit_name(3) == 'vcU03' and rl.row_name(12, 'B') == 'vcR12B' and rl.status_name(2) == 'vcS2', 'the spellings')
names = set(rl.elements())
check(all(len(n) <= 7 and n.startswith(rl.PREFIX) for n in names), 'every name at most 7 characters (the in-place wstring)')
check(len(names) == 7 + rl.ROWS * 7 + rl.STATUS_LINES, 'element count')
icons = sorted(set(re.findall(r'L"app:/weapon/icon/([\w.]+)"', header)))
check(len(icons) >= 15, f'the header names the icons: {icons}')

# --- The subtree ---
tree = rl.subtree()
flat: list[str] = []


def walk(pairs: list) -> None:
    for i in range(0, len(pairs), 2):
        flat.append(pairs[i][0])
        if isinstance(pairs[i + 1], list):
            walk(pairs[i + 1])


walk(tree)
check(sorted(flat) == sorted(names) and len(flat) == len(set(flat)), 'every element once in the subtree')
check(tree[0] == [rl.ROOT], 'vcRoot is the subtree root')
els = rl.elements()
check(els[rl.ROOT][0] == 'Layout' and els[rl.ROOT][1] == rl.TRANSPARENT and els[rl.BACK][1] == rl.ICON, 'root and panel')
# Nothing draws by default: every element's skin silent, every text empty.
for n, e in els.items():
    check(rl.draws_by_default(e) == [], f'{n} draws nothing by itself: {rl.draws_by_default(e)}')
    check(e[1] in rl.SILENT_SKINS, f'{n}: a silent skin')
    if e[0] == 'TextField':
        props = {p[0]: p[1] for p in e[7]}
        check(props.get('text_dr') == '' and 'text' not in props, f'{n}: empty text')
check(sum(1 for e in els.values() if e[0] == 'TextField') == 5 + rl.ROWS * 3 + rl.STATUS_LINES, 'every text field checked')
# The predicate itself catches what would show (a window skin, a model chip, a text).
check(rl.draws_by_default(['Layout', 'app:/UI/Window_Test01_skin.sgo', [0, 0, 0], [0, 0, 1, 1], [], 0, 0, []]) != [] and
      rl.draws_by_default(['Layout', 'app:/UI/ColorChipBox_skin.sgo', [0, 0, 0], [0, 0, 1, 1], [], 0, 0, []]) != [] and
      rl.draws_by_default(['TextField', rl.TRANSPARENT, [0, 0, 0], [0, 0, 1, 1], [0, 0], 0, 0, [['text_dr', ' ']]]) != [] and
      rl.draws_by_default(['TextField', rl.TRANSPARENT, [0, 0, 0], [0, 0, 1, 1], [0, 0], 0, 0, [['text', 'Key']]]) != [],
      'draws_by_default names a drawing skin and a text')
# The white texture: the header's path, a plain opaque DDS.
white = re.search(r'kWhiteTexture=L"app:/([\w./]+)"', header).group(1)
check(white.upper() == rl.WHITE.upper(), f'the plugin binds the file the installer writes: {white} / {rl.WHITE}')
dds = rl.white_dds()
check(dds[:4] == b'DDS ' and len(dds) == 128 + rl.WHITE_SIZE ** 2 * 4 and set(dds[128:]) == {0xFF}, 'white: every pixel FF FF FF FF')
for i in range(rl.ROWS):
    check(els[rl.row_name(i, 'A')][1] == rl.ICON == els[rl.row_name(i, 'B')][1], 'chips use the texture skin')
    check(els[rl.row_name(i, 'I')][1] == rl.ICON, 'icons use HUDIconTexture_skin')
    check(els[rl.row_name(i, 'T')][0] == 'TextField' == els[rl.row_name(i, 'N')][0] == els[rl.unit_name(i)][0], 'text fields')
    check(all(els[rl.row_name(i, p)][0] != 'Button' for p in rl.ROW_PARTS), 'no Button: nothing takes the screen\'s focus')
    # The panel's right edge: the row box at ROWS_X plus its parts.
    box = els[rl.row_name(i)]
    for p in rl.ROW_PARTS:
        e = els[rl.row_name(i, p)]
        check(box[2][0] + e[2][0] + e[3][2] <= rl.MENU_RIGHT, f'{rl.row_name(i, p)} left of the 3D view')
    check(els[rl.unit_name(i)][2][0] + els[rl.unit_name(i)][3][2] <= box[2][0], 'units left of the rows')
check(els[rl.BACK][2][0] + els[rl.BACK][3][2] <= rl.MENU_RIGHT, 'the panel left of the 3D view')
# The last selectable row, status and help are independent vertical bands. Validate in layout units and after uniform
# scaling to the supported common resolutions: a shorter page leaves the full three-line help visible at 720p too.
for width, height in ((1280, 720), (1600, 900), (1920, 1080), (2560, 1440), (3440, 1440)):
    scale = min(width / 1920, height / 1080)
    bands = [(rl.ROW_Y + (rl.ROWS - 1) * rl.ROW_STEP, rl.ROW_H),
             (rl.STATUS_Y, (rl.STATUS_LINES - 1) * rl.STATUS_STEP + 34),
             (rl.HINT_Y, 34), (rl.HINT_Y + rl.HELP_STEP, 34), (rl.HINT_Y + 2 * rl.HELP_STEP, 34)]
    check(all((y + h) * scale <= bands[i + 1][0] * scale for i, (y, h) in enumerate(bands[:-1])),
          f'{width}x{height}: rows, status and all help lines have no vertical overlap')
    check((bands[-1][0] + bands[-1][1]) * scale <= height, f'{width}x{height}: help stays on screen')
check(els[rl.ENTRY][2][1] > 953 and els[rl.ENTRY][2][1] + els[rl.ENTRY][3][3] <= 1008,
      'closed-page entry is below the room chat and inside the page margin')

check(all(e[2][1] + e[3][3] <= 1080 for n, e in els.items() if not (n.startswith(rl.PREFIX + 'R') and len(n) == 6)),
      'every direct child of vcRoot inside the screen')

# --- The stock files (Root.cpk only read) ---
try:
    import rootcpk
    game = rootcpk.default()
    game.read('UI', 'LYT_HUIONLINEROOM.SGO')
except Exception as e:   # no game on this machine (CI)
    print(f'room_layout_test: no game ({e}); the stock layouts not patched here')
    game = None

if game is not None:
    built = rl.build(game.root)
    check(sorted(built) == ['UI/LYT_HUIHQMAIN.SGO', 'UI/LYT_HUIONLINEROOM.SGO'], 'both screens, nothing else')
    check(not any('MAINFRAME' in k.upper() for k in built), 'LYT_MAINFRAME never produced (EDF6Coop owns it)')
    for rel, data in built.items():
        stock = game.read('UI', os.path.basename(rel))
        v0, before = sgo.read(stock)
        v1, after = sgo.read(data)
        if os.path.basename(rel) == 'LYT_HUIONLINEROOM.SGO':
            chat = sgo.plain(before['Chat_Window'])
            check(els[rl.ENTRY][2][1] >= chat[2][1] + chat[3][3], 'entry stays below the native room chat window')
        check(v0 == v1, 'version kept')
        check(set(after) == set(before) | names and not (set(before) & names), f'{rel}: the menu\'s members added, none replaced')
        for n, v in before.items():
            if n != 'layout_tree':
                check(after[n] == v, f'{rel}: {n} unchanged')
        check(after['layout_tree'][0] == ['BaseFrame'] and after['layout_tree'][1][:-2] == before['layout_tree'][1] and
              after['layout_tree'][1][-2:] == tree and after['layout_tree'][2:] == before['layout_tree'][2:],
              f'{rel}: the subtree appended as BaseFrame\'s last child, the stock tree otherwise the same')
        check(rl.strip(data) == stock, f'{rel}: without the menu, the stock file byte for byte')
        check(all(after[n] == els[n] or sgo.plain(after[n]) == sgo.plain(els[n]) for n in names), f'{rel}: the elements read back')
        try:
            rl.patch(data)
            check(False, f'{rel}: patching twice is refused')
        except ValueError:
            pass
    _, main_frame = sgo.read(game.read('UI', 'LYT_MAINFRAME.SGO'))
    guide = sgo.plain(main_frame['ControlGuideText'])
    check(els[rl.ENTRY][2][0] + els[rl.ENTRY][3][2] <= guide[2][0],
          'entry stays left of the native MainFrame control guide (MainFrame itself is not changed)')
    # The skins as the game has them: Transparent draws nothing, HUDIconTexture's default texture is empty.
    _, skin = sgo.read(game.read('UI', 'TRANSPARENT_SKIN.SGO'))
    check(skin == {'object_class': 'Transparent'}, f'Transparent_skin: {skin}')
    _, skin = sgo.read(game.read('UI', 'HUDICONTEXTURE_SKIN.SGO'))
    check(skin.get('object_class') == 'Texture' and skin.get('base') == '' and
          all(state[0][0] == '' for k, state in skin.items() if isinstance(state, list)), f'HUDIconTexture_skin: no texture in any state: {skin}')
    check(all(s.split('/')[-1].upper() in {n.upper() for n in game.names('UI')} for s in rl.SILENT_SKINS), 'both skins in Root.cpk')
    # The white texture in the format the game loads (UI/CHATSHORTCUT_CURSOR.DDS: 32-bit uncompressed, no mipmaps).
    import struct
    stock = game.read('UI', 'CHATSHORTCUT_CURSOR.DDS')
    check(struct.unpack_from('<2I', stock, 4) == struct.unpack_from('<2I', dds, 4) and stock[28:32] == dds[28:32] and
          stock[76:128] == dds[76:128], 'white: the stock 32-bit DDS header (flags, mipmaps, pixel format, caps)')
    check(struct.unpack_from('<3I', dds, 12) == (rl.WHITE_SIZE, rl.WHITE_SIZE, rl.WHITE_SIZE ** 2 * 4), 'white: size and linear size')
    check(not any(n.upper() == os.path.basename(rl.WHITE).upper() for n in game.names('UI')), 'white: a new name, no stock file shadowed')
    wanted = {i.upper() for i in icons}
    have = {n.upper() for n in game.names('WEAPON/ICON')}
    check(wanted <= have, f'every icon the header names is in Root.cpk: missing {sorted(wanted - have)}')

# --- The ledger side ---
with tempfile.TemporaryDirectory(prefix='edf6vc-roomui-') as tmp:
    os.makedirs(os.path.join(tmp, 'Mods'))
    files = {'UI/LYT_HUIONLINEROOM.SGO': b'room v1', 'UI/LYT_HUIHQMAIN.SGO': b'hq v1'}
    written, skipped = rl.install(tmp, files)
    led = ledger.Ledger(tmp)
    check(len(written) == 3 and not skipped and all(rl.OWNER in led.owners(r) for r in [*files, rl.WHITE]) and
          read(led.disk(rl.WHITE)) == rl.white_dds(), 'install: both layouts and the white texture, ours')
    written, skipped = rl.install(tmp, {r: b.replace(b'v1', b'v2') for r, b in files.items()})
    check(len(written) == 3 and read(led.disk('UI/LYT_HUIHQMAIN.SGO')) == b'hq v2', 'reinstall replaces our own files')
    other = os.path.join(tmp, 'other')
    os.makedirs(os.path.join(other, 'Mods', 'UI'))
    with open(os.path.join(other, 'Mods', 'UI', 'LYT_HUIONLINEROOM.SGO'), 'wb') as f:
        f.write(b'another mod')
    written, skipped = rl.install(other, files)
    check(len(written) == 2 and len(skipped) == 1 and read(skipped[0]) == b'another mod', "another mod's layout left alone")
    saved = rl.build
    rl.build = lambda root: {r: b'new ' + b for r, b in files.items()}
    try:
        check(rl.install_missing(tmp) == [], 'install_missing: all there, nothing built')
        os.remove(led.disk(rl.WHITE))
        got = rl.install_missing(tmp)
        check(len(got) == 1 and read(got[0]) == rl.white_dds(), 'install_missing: the white texture alone, without Root.cpk')
        os.remove(led.disk('UI/LYT_HUIHQMAIN.SGO'))
        got = rl.install_missing(tmp)
        check(len(got) == 1 and read(got[0]) == b'new hq v1' and read(led.disk('UI/LYT_HUIONLINEROOM.SGO')) == b'room v2',
              'install_missing: the missing one only, the other never replaced')
    finally:
        rl.build = saved
    deleted, kept = rl.remove(tmp)
    check(len(deleted) == 3 and not os.listdir(os.path.join(tmp, 'Mods', 'UI')), 'remove: our files gone')
    deleted, kept = rl.remove(other)
    check(read(os.path.join(other, 'Mods', 'UI', 'LYT_HUIONLINEROOM.SGO')) == b'another mod', "remove leaves another mod's file")
    try:
        rl.install(tmp, {'UI/LYT_MAINFRAME.SGO': b'x'})
        check(False, 'LYT_MAINFRAME refused')
    except AssertionError:
        pass

check('roomui' in ledger.OWNERS, 'the ledger knows the owner')
src = open(os.path.join(ROOT, 'tools', 'installer.py'), encoding='utf-8').read()
gen = open(os.path.join(ROOT, 'tools', 'loadout_generator.py'), encoding='utf-8').read()   # loadout-generate's body
check('room_layout.install(game, room)' in src and 'room_layout.remove(game)' in src.split('\ndef uninstall(', 1)[1] and
      'room_layout.install_missing(game)' in gen, 'the installer installs, uninstalls and loadout-generate fills in')
print(f'room_layout_test: {checks} checks passed')
