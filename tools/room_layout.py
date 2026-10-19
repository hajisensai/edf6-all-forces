"""The in-game support loadout menu's screen (src/room_ui.cpp, docs/feature-2026-10-12-room-loadout-menu.md stage 4): the
online room's and the offline HQ's stock layouts with the menu's elements added, written into <game>/Mods:

  Mods/UI/LYT_HUIONLINEROOM.SGO   HUiRoom's layout (Root.cpk UI/LYT_HUIONLINEROOM.SGO) + the menu's subtree
  Mods/UI/LYT_HUIHQMAIN.SGO       HUiHQMain's layout (Root.cpk UI/LYT_HUIHQMAIN.SGO) + the same subtree
  Mods/UI/EDF6VC_WHITE.DDS        a plain white 8 x 8 texture the plugin binds to the panel and the chips and tints
                                  (Root.cpk has no plain one; the format is UI/CHATSHORTCUT_CURSOR.DDS's: 32-bit BGRA)

A vcLoad Button definition inherits the screen's original menu button. It is deliberately absent from layout_tree;
src/room_ui.cpp creates it under the real native button group with HUiLayout::AddByName only after validation.
Without the plugin there is no instance and no extra focus stop. Mouse, keyboard and pad activation are the native
Button Click event, with a native weak screen callback whose lifetime ends with the screen.

Every stock member is kept as it is (the values read back unchanged, the file written in the stock files' own layout:
pylib/sgo.py write_depth_first reproduces both stock files byte for byte); the menu's members are added (names starting
`vc`, none in the stock files) and one subtree is appended as BaseFrame's last child (drawn last: over the screen).
LYT_MAINFRAME is never touched (EDF6Coop's). The left side only: the right of x = MENU_RIGHT stays the game's 3D model
view (the preview, docs/room-3d-preview-re.md §6).

Nothing of it draws by itself: every element's skin is Transparent_skin or HUDIconTexture_skin (a Texture skin whose
default texture is empty: its draw 0x7F7040 skips an element with no texture bound) and every text is empty. Without the
plugin (not loaded, or refused after a game update) the screens look stock; the plugin binds the textures, tints them
and writes the texts while its menu is shown, and takes them back when it is not (src/room_ui.cpp).

The elements (docs/room-ui-re.md §5: [type, skin, pos, area, flags, hcoord, vcoord, props]; pos relative to the parent):
  vcRoot                 the whole menu, a transparent layout over the screen: the plugin shows / hides it (+0x1F8)
    vcBack               the panel (HUDIconTexture_skin: GLOBAL; the plugin binds WHITE and tints it)
    vcHead               the title line
    vcU00..vcU11         the unit column (TextField)
    vcR00..vcR11         the row column, each a transparent layout holding
      vcR00T             its text (TextField)
      vcR00A / vcR00B    the primary / secondary colour chips (HUDIconTexture_skin: WHITE tinted the colour)
      vcR00I             the weapon / store icon (HUDIconTexture_skin: the plugin sets the icon's texture)
      vcR00N             the field / rounds note (TextField)
    vcS0..vcS3           the status lines (generation, room sync, the client's note)
    vcHint / vcHelp2 / vcHelp3  three contextual help lines (separate from status)
    vcEntry              the visible closed-page entry (actual toggle key / pad combination)
src/room_ui_logic.h names them the same way (tests/room_layout_test.py checks the two agree).

The installer writes both at install (`install`, owner `roomui` in the ledger, released at uninstall); `loadout-generate`
writes a missing one as a new file only (`install_missing`: never replaces a file, so a running game sees none or all).
A layout already there that the ledger does not record as ours is another mod's: left alone, and said.

  python tools/room_layout.py [game dir]            write / refresh
  python tools/room_layout.py [game dir] --remove   release them
"""
from __future__ import annotations

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import ledger  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402

OWNER = 'roomui'   # pylib/ledger.py OWNERS
LAYOUTS = ('LYT_HUIONLINEROOM.SGO', 'LYT_HUIHQMAIN.SGO')   # Root.cpk UI/<name> -> Mods/UI/<name>
NOT_OURS = ('LYT_MAINFRAME.SGO',)                          # EDF6Coop's: never written here

ROWS = 12            # rows a column shows (src/room_ui_logic.h kRows)
STATUS_LINES = 4     # src/room_ui_logic.h kStatusLines
PREFIX = 'vc'        # every member this tool adds
MENU_RIGHT = 1128    # the panel's right edge (1920 x 1080 layout units): the 3D view's side stays clear

TRANSPARENT = 'app:/UI/Transparent_skin.sgo'
ICON = 'app:/UI/HUDIconTexture_skin.sgo'   # a Texture skin, default texture '' (nothing drawn until the plugin binds one)
SILENT_SKINS = (TRANSPARENT, ICON)          # every skin this tool uses: none draws anything by default
WHITE = 'UI/EDF6VC_WHITE.DDS'               # under Mods; the plugin's kWhiteTexture (app:/UI/EDF6VC_WHITE.dds)
WHITE_SIZE = 8

PANEL_X, PANEL_Y, PANEL_W, PANEL_H = 48.0, 96.0, MENU_RIGHT - 48.0, 912.0
ROW_Y, ROW_STEP, ROW_H = 168.0, 46.0, 40.0
UNIT_X, UNIT_W = 80.0, 380.0
ROWS_X = 480.0
STATUS_Y, STATUS_STEP = 736.0, 36.0
HINT_Y, HELP_STEP = 896.0, 36.0
ENTRY_Y = 974.0


def _layout(skin: str, x: float, y: float, w: float, h: float) -> list:
    return ['Layout', skin, [x, y, 0.0], [0.0, 0.0, w, h], [], 0, 0, []]


def _text(x: float, y: float, w: float, h: float, size: int) -> list:
    # The stock rooms' info lines (RoomInfo_Name): direct text, left / vertically centred, a 2 px border.
    # Empty by default (60 stock text fields are): nothing shows until the plugin writes a line.
    return ['TextField', TRANSPARENT, [x, y, 0.0], [0.0, 0.0, w, h], [0, 0], 0, 0,
            [['text_dr', ''], ['font_flag', [4, 10]], ['font_border_width', 2.0], ['font_size', [size, size]]]]


def unit_name(i: int) -> str:
    return f'{PREFIX}U{i:02d}'


def row_name(i: int, part: str = '') -> str:
    return f'{PREFIX}R{i:02d}{part}'


def status_name(i: int) -> str:
    return f'{PREFIX}S{i}'


ROOT, BACK, HEAD, HINT = PREFIX + 'Root', PREFIX + 'Back', PREFIX + 'Head', PREFIX + 'Hint'
HELP2, HELP3, ENTRY = PREFIX + 'Help2', PREFIX + 'Help3', PREFIX + 'Entry'
NATIVE_ENTRY = PREFIX + 'Load'  # a definition only: instantiated by the native AddByName after the plugin validates
ROW_PARTS = ('T', 'A', 'B', 'I', 'N')   # text, primary chip, secondary chip, icon, note


def elements() -> dict[str, list]:
    """The members this tool adds, {name: element}."""
    e: dict[str, list] = {
        ROOT: _layout(TRANSPARENT, 0.0, 0.0, 1920.0, 1080.0),
        BACK: _layout(ICON, PANEL_X, PANEL_Y, PANEL_W, PANEL_H),
        HEAD: _text(UNIT_X, 112.0, 1000.0, 40.0, 28),
        HINT: _text(UNIT_X, HINT_Y, 1000.0, 34.0, 18),
        HELP2: _text(UNIT_X, HINT_Y + HELP_STEP, 1000.0, 34.0, 18),
        HELP3: _text(UNIT_X, HINT_Y + 2 * HELP_STEP, 1000.0, 34.0, 18),
        ENTRY: _text(UNIT_X, ENTRY_Y, 1000.0, 34.0, 22),
    }
    for i in range(ROWS):
        y = ROW_Y + i * ROW_STEP
        e[unit_name(i)] = _text(UNIT_X, y, UNIT_W, ROW_H, 22)
        e[row_name(i)] = _layout(TRANSPARENT, ROWS_X, y, 620.0, ROW_H)
        e[row_name(i, 'T')] = _text(0.0, 0.0, 340.0, ROW_H, 22)
        e[row_name(i, 'A')] = _layout(ICON, 348.0, 4.0, 32.0, 32.0)
        e[row_name(i, 'B')] = _layout(ICON, 388.0, 4.0, 32.0, 32.0)
        e[row_name(i, 'I')] = _layout(ICON, 428.0, 0.0, 80.0, ROW_H)
        e[row_name(i, 'N')] = _text(512.0, 0.0, 108.0, ROW_H, 20)
    for i in range(STATUS_LINES):
        e[status_name(i)] = _text(UNIT_X, STATUS_Y + i * STATUS_STEP, 1000.0, 34.0, 20)
    return e


def native_entry(room: bool) -> list:
    """A native Button definition, deliberately not in the initial tree.

    The online room has eight 48px menu buttons at 58px intervals and chat starts at y=747. The new final row uses
    its stock 440px width/skin with a 2px leading gap and 34px height, ending at 744 instead of entering the chat area.
    HQ has sufficient height for the unchanged 42px/28px original button template, including split-screen menus.
    """
    props: list = [['text_dr', '']]
    if room:
        props += [['pos', [0.0, 2.0, 0.0]], ['area', [0.0, 0.0, 440.0, 34.0]], ['font_size', [22, 22]]]
    return [[0, 'Button03Base' if room else 'Button02Base'], props]


def white_dds() -> bytes:
    """A plain white opaque WHITE_SIZE x WHITE_SIZE texture: the header of Root.cpk's UI/CHATSHORTCUT_CURSOR.DDS (32-bit,
    masks R 0xFF0000 G 0xFF00 B 0xFF A 0xFF000000, no mipmaps, linear size), every pixel FF FF FF FF."""
    n = WHITE_SIZE
    header = struct.pack('<4s7I', b'DDS ', 124, 0x81007, n, n, n * n * 4, 0, 0) + bytes(44)
    header += struct.pack('<8I', 32, 0x41, 0, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000)
    header += struct.pack('<5I', 0x1000, 0, 0, 0, 0)
    assert len(header) == 128
    return header + bytes([0xFF]) * (n * n * 4)


def draws_by_default(element: list) -> list[str]:
    """Why `element` would show anything with no plugin driving it ([] for nothing): a skin that draws by itself, a text."""
    why = []
    if element[1] not in SILENT_SKINS:
        why.append(f'skin {element[1]}')
    props = {p[0]: p[1] for p in element[7] if isinstance(p, list) and p}
    if element[0] == 'TextField' and (props.get('text_dr', '') != '' or 'text' in props):
        why.append(f'text {props.get("text_dr", props.get("text"))!r}')
    return why


def subtree() -> list:
    """[['vcRoot'], [children]]: the pair appended to BaseFrame's children (layout_tree: a name, then 0 or its children)."""
    kids: list = [[BACK], 0, [HEAD], 0]
    for i in range(ROWS):
        kids += [[unit_name(i)], 0]
    for i in range(ROWS):
        kids += [[row_name(i)], [x for part in ROW_PARTS for x in ([row_name(i, part)], 0)]]
    for i in range(STATUS_LINES):
        kids += [[status_name(i)], 0]
    kids += [[HINT], 0, [HELP2], 0, [HELP3], 0, [ENTRY], 0]
    return [[ROOT], kids]


def _base_frame(tree: list) -> list:
    """BaseFrame's children list in a layout_tree (its first entry in both screens)."""
    if len(tree) < 2 or tree[0] != ['BaseFrame'] or not isinstance(tree[1], list):
        raise ValueError('layout_tree 的第一项不是带子元素的 BaseFrame')
    return tree[1]


def patch(stock: bytes) -> bytes:
    """The stock layout `stock` with the menu added (checked: nothing of the stock file changed, see strip)."""
    version, members = sgo.read(stock)
    ours = elements()
    clash = sorted(n for n in members if n.lower().startswith(PREFIX.lower()))
    if clash:
        raise ValueError(f'原版布局里已有同前缀的元素 {clash}：不是预期的原版文件')
    if 'layout_tree' not in members or members.get('xgs_ui_object_class') not in ('HUiRoom', 'HUiHQMain'):
        raise ValueError('不是房间 / HQ 主画面的布局')
    tree = [list(x) if isinstance(x, list) else x for x in members['layout_tree']]
    tree[1] = list(_base_frame(tree)) + subtree()
    out = dict(members)
    out['layout_tree'] = tree
    out.update(ours)
    # The game's own HQ creates its normal menu buttons this way too (0x8456D0). A member outside layout_tree is
    # inert without the plugin: no drawing, no empty focus stop, and no callback can point into an absent DLL.
    out[NATIVE_ENTRY] = native_entry(members['xgs_ui_object_class'] == 'HUiRoom')
    data = sgo.write_depth_first(version, out)
    if strip(data) != stock:
        raise AssertionError('补丁后去掉新增部分不等于原版文件')
    return data


def strip(patched: bytes) -> bytes:
    """`patched` without the menu (its members and BaseFrame's last child pair), written as the stock files are."""
    version, members = sgo.read(patched)
    out = {n: v for n, v in members.items() if not n.startswith(PREFIX)}
    tree = list(out['layout_tree'])
    kids = list(_base_frame(tree))
    if kids[-2:] == subtree():
        kids = kids[:-2]
    tree[1] = kids
    out['layout_tree'] = tree
    return sgo.write_depth_first(version, out)


def build(root: str) -> dict[str, bytes]:
    """{path under Mods: bytes} for both screens, from the game's Root.cpk (only read)."""
    game = rootcpk.Game(root)
    return {f'UI/{name}': patch(game.read('UI', name)) for name in LAYOUTS}


def _ours(led: ledger.Ledger, rel: str) -> bool:
    return OWNER in led.owners(rel)


def install(root: str, files: dict[str, bytes]) -> tuple[list[str], list[str]]:
    """Writes `files` (build) and the white texture as this tool's: (paths written, paths skipped: another mod's file
    already there)."""
    led = ledger.Ledger(root)
    written: list[str] = []
    skipped: list[str] = []
    for rel, data in {**files, WHITE: white_dds()}.items():
        assert os.path.basename(rel).upper() not in NOT_OURS, rel
        if os.path.exists(led.disk(rel)) and not _ours(led, rel):
            skipped.append(led.disk(rel))
            continue
        written.append(led.put(OWNER, rel, data))
    return written, skipped


def install_missing(root: str) -> list[str]:
    """For `loadout-generate` (the game may be running): the layouts and the white texture not there at all, as new files
    only."""
    led = ledger.Ledger(root)
    out = []
    if not os.path.exists(led.disk(WHITE)):
        path = led.put_new(OWNER, WHITE, white_dds())
        if path:
            out.append(path)
    if all(os.path.exists(led.disk(f'UI/{name}')) for name in LAYOUTS):
        return out
    for rel, data in build(root).items():
        path = led.put_new(OWNER, rel, data)
        if path:
            out.append(path)
    return out


def remove(root: str) -> tuple[list[str], list[str]]:
    """Releases this tool's files: (deleted, kept changed)."""
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=False)


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith('--')]
    root = args[0] if args else rootcpk.DEFAULT_GAME
    if '--remove' in argv:
        deleted, kept = remove(root)
        for path in deleted:
            print('删除', path)
        for path in kept:
            print('保留（已被别人改过）', path)
        return 0
    written, skipped = install(root, build(root))
    for path in written:
        print('写入', path)
    for path in skipped:
        print('跳过（别的 mod 的布局，保持原样；游戏内战前配置菜单在这个画面不显示）', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
