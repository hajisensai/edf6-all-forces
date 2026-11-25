"""The online room's and the offline HQ's stock layouts with the plugin's native 整备 button added (src/room_ui.cpp),
written into <game>/Mods:

  Mods/UI/LYT_HUIONLINEROOM.SGO   HUiRoom's layout (Root.cpk UI/LYT_HUIONLINEROOM.SGO) + vcRoot + the vcLoad definition
  Mods/UI/LYT_HUIHQMAIN.SGO       HUiHQMain's layout (Root.cpk UI/LYT_HUIHQMAIN.SGO) + the same

A vcLoad Button definition inherits the screen's original menu button. It is deliberately absent from layout_tree;
src/room_ui.cpp creates it under the real native button group with HUiLayout::AddByName only after validation, and only
on a screen whose layout has vcRoot (src/room_ui_logic.h kRootName: the layout is this tool's, not another mod's).
Without the plugin there is no instance and no extra focus stop. Mouse, keyboard and pad activation are the native
Button Click event, with a native weak screen callback whose lifetime ends with the screen. The button opens 整备
(src/armory.h), which the plugin draws itself: nothing else of these layouts is the plugin's.

2026-11-24 (the 整备 rewrite): the old dialog menu's overlay (vcBack / vcHead / vcU.. / vcR.. / vcS.. / vcHint /
vcHelp2 / vcHelp3, whose texture skins the plugin tinted with Mods/UI/EDF6VC_WHITE.DDS) and its wide copy of the game's
description dialog (Mods/UI/EDF6VC_LOADOUTBOX.SGO) went with that menu. Those two files are RETIRED: an install releases
them from the ledger (deleted when they are still what an older install wrote, kept when someone changed them since).

Every stock member is kept as it is (the values read back unchanged, the file written in the stock files' own layout:
pylib/sgo.py write_depth_first reproduces both stock files byte for byte); the added members' names start with `vc`
(none in the stock files) and vcRoot is appended as BaseFrame's last child: an empty Transparent_skin layout that draws
nothing. LYT_MAINFRAME is never touched (EDF6Coop's).

The installer writes them at install (`install`, owner `roomui` in the ledger, released at uninstall); `loadout-generate`
writes a missing one as a new file only (`install_missing`: never replaces a file, so a running game sees none or all).
A layout already there that the ledger does not record as ours is another mod's: left alone, and said.

  python tools/room_layout.py [game dir]            write / refresh
  python tools/room_layout.py [game dir] --remove   release them
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import ledger  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402

OWNER = 'roomui'   # pylib/ledger.py OWNERS
LAYOUTS = ('LYT_HUIONLINEROOM.SGO', 'LYT_HUIHQMAIN.SGO')   # Root.cpk UI/<name> -> Mods/UI/<name>
NOT_OURS = ('LYT_MAINFRAME.SGO',)                          # EDF6Coop's: never written here
# What older versions wrote under Mods and nothing reads any more (the dialog menu's white texture and loadout box).
RETIRED = ('UI/EDF6VC_WHITE.DDS', 'UI/EDF6VC_LOADOUTBOX.SGO')

PREFIX = 'vc'        # every member this tool adds
TRANSPARENT = 'app:/UI/Transparent_skin.sgo'
SILENT_SKINS = (TRANSPARENT,)   # every skin this tool uses: none draws anything
ROOT = PREFIX + 'Root'          # src/room_ui_logic.h kRootName: the marker the plugin looks for
NATIVE_ENTRY = PREFIX + 'Load'  # a definition only: instantiated by the native AddByName after the plugin validates


def elements() -> dict[str, list]:
    """The members this tool adds to layout_tree, {name: element} (docs/room-ui-re.md §5: [type, skin, pos, area, flags,
    hcoord, vcoord, props])."""
    return {ROOT: ['Layout', TRANSPARENT, [0.0, 0.0, 0.0], [0.0, 0.0, 1920.0, 1080.0], [], 0, 0, []]}


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
    """[['vcRoot'], 0]: the pair appended to BaseFrame's children (layout_tree: a name, then 0 or its children)."""
    return [[ROOT], 0]


def _base_frame(tree: list) -> list:
    """BaseFrame's children list in a layout_tree (its first entry in both screens)."""
    if len(tree) < 2 or tree[0] != ['BaseFrame'] or not isinstance(tree[1], list):
        raise ValueError('layout_tree 的第一项不是带子元素的 BaseFrame')
    return tree[1]


def patch(stock: bytes) -> bytes:
    """The stock layout `stock` with vcRoot and vcLoad added (checked: nothing of the stock file changed, see strip)."""
    version, members = sgo.read(stock)
    clash = sorted(n for n in members if n.lower().startswith(PREFIX.lower()))
    if clash:
        raise ValueError(f'原版布局里已有同前缀的元素 {clash}：不是预期的原版文件')
    if 'layout_tree' not in members or members.get('xgs_ui_object_class') not in ('HUiRoom', 'HUiHQMain'):
        raise ValueError('不是房间 / HQ 主画面的布局')
    tree = [list(x) if isinstance(x, list) else x for x in members['layout_tree']]
    tree[1] = list(_base_frame(tree)) + subtree()
    out = dict(members)
    out['layout_tree'] = tree
    out.update(elements())
    # The game's own HQ creates its normal menu buttons this way too (0x8456D0). A member outside layout_tree is
    # inert without the plugin: no drawing, no empty focus stop, and no callback can point into an absent DLL.
    out[NATIVE_ENTRY] = native_entry(members['xgs_ui_object_class'] == 'HUiRoom')
    data = sgo.write_depth_first(version, out)
    if strip(data) != stock:
        raise AssertionError('补丁后去掉新增部分不等于原版文件')
    return data


def strip(patched: bytes) -> bytes:
    """`patched` without our members (and BaseFrame's last child pair), written as the stock files are."""
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


def install(root: str, files: dict[str, bytes]) -> tuple[list[str], list[str], list[str], list[str]]:
    """Writes `files` (build) as this tool's and releases RETIRED: (paths written, paths skipped: another mod's file
    already there, retired paths deleted, retired paths kept because someone changed them since)."""
    led = ledger.Ledger(root)
    written: list[str] = []
    skipped: list[str] = []
    for rel, data in files.items():
        assert os.path.basename(rel).upper() not in NOT_OURS, rel
        if os.path.exists(led.disk(rel)) and not _ours(led, rel):
            skipped.append(led.disk(rel))
            continue
        written.append(led.put(OWNER, rel, data))
    # Only what the ledger records as ours: a file of that name nobody recorded, or another owner's, stays.
    deleted, kept = led.release(OWNER, list(RETIRED), writer=False)
    return written, skipped, deleted, kept


def install_missing(root: str) -> list[str]:
    """For `loadout-generate` (the game may be running): the layouts not there at all, as new files only."""
    led = ledger.Ledger(root)
    out: list[str] = []
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
    written, skipped, deleted, kept = install(root, build(root))
    for path in written:
        print('写入', path)
    for path in skipped:
        print('跳过（别的 mod 的布局，保持原样；这个画面里没有「整备」按钮）', path)
    for path in deleted:
        print('删除（旧版战前配置菜单的文件，已不再使用）', path)
    for path in kept:
        print('保留（旧版战前配置菜单的文件，已被别人改过）', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
