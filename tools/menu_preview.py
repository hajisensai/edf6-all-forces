"""The unified model files of the support soldiers: each AI template the battle spawns (OBJECT/N60x_*.SGO and its
_LEADER) written into Mods as the same file with the keys only the room / HQ 3D preview's menu renderer reads added
(pylib/menu_model.py, docs/unified-model-re.md). The plugin's preview (src/room_preview.cpp) shows a soldier from the very
file the battle loads (app:/object/<template>.sgo), and the plugin's hook makes the menu read menu_animation_model where
the battle reads animation_model. No menu-only model file exists.

  menu_animation_model   the template's own body (animation_model[0]: archive, model) with its class's stock menu CAS
                         and MAB (MENUOBJECT/P505_RANGER / P606_WINGDIVER / P607_FENCER.SGO: the menu poses, `main`)
  change_color           the template's own soldier_color as the menu's selectors (selectors(): a material name or 0 =
                         every colourable material; the Ranger leaders colour their head; a face's tint is no channel)
  random_animation       the class's stock menu idle poses
  soldier_weapon_slot / soldier_weapon_grab_adjust   the template's own (the menu reads the same shape)

And the menu CAS of the support jets: their CAS v506_heli.cas has `Roter` / `Main` but no `main`, the anmgroup the menu
renderer looks up case-sensitively (EDF.dll 0x93FB85) and indexes without a check (0x93EBAC: -1 crashed test site #50),
so MENUOBJECT/EDF6VC_MENU_V506_HELI.CAS is that CAS with the one anmgroup's name lower-cased in place (same length), the
animation resource the jets' menu_animation_model names (pylib/vcobjects.py jet_sgo); the jets' own CAS is untouched.
build() refuses (ValueError) any file menu_model.menu_problems finds lacking or whose battle keys moved.

Written into <game>/Mods as this tool's in the ledger (pylib/ledger.py, owner OWNER); Root.cpk only read.

  build(game) -> {Mods path: bytes}   install(root, files) / install_new(root, files) -> paths   remove(root)
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import dsgo  # noqa: E402
import ledger  # noqa: E402
import mdb  # noqa: E402
import menu_model  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402
from modfiles import sha256, sha256_file  # noqa: E402

OWNER = 'menu_preview'   # pylib/ledger.py OWNERS
CHANNELS = menu_model.CHANNELS
EVERY = menu_model.EVERY
MENU_ANMGROUP = menu_model.MENU_ANMGROUP
JET_CAS = menu_model.JET_CAS

Selector = int | str
Templates = dict[str, dsgo.Node]


def template_names() -> list[str]:
    """The support soldiers' AI templates (support_loadout.KINDS with looks) and their _LEADER."""
    import support_loadout   # the X-Morph infantry is no stock template (its body is the installer's own)
    return [t + suffix for n, _, t in support_loadout.KINDS if support_loadout.has_looks(n) for suffix in ('', '_LEADER')]


def jet_cas_rel() -> str:
    return f'{menu_model.MENU_FOLDER}/{menu_model.menu_cas_name(JET_CAS)}'


def rels() -> list[str]:
    """Every file this tool writes (always made; loadout-generate remakes them when missing)."""
    return [f'OBJECT/{n}.SGO' for n in template_names()] + [jet_cas_rel()]


def cas_ref(path: str) -> tuple[str, str]:
    """An SGO's CAS path ('app:/object/v506_heli.cas') as (folder, name) of the archive / Mods ('OBJECT', 'V506_HELI.CAS')."""
    head = 'app:/'
    if not path.lower().startswith(head) or path.count('/') != 2:
        raise ValueError(f'CAS path {path!r} is not app:/<folder>/<name>')
    folder, name = path[len(head):].split('/')
    return folder.upper(), name.upper()


def anmgroups(cas: bytes) -> list[tuple[int, str]]:
    """A CAS's anmgroups: (where its name string is, the name)."""
    import cas_legacy
    lay = cas_legacy.cas_layout(cas)
    out = []
    for at, _ in lay.tree.anmgroups:
        to = at + cas_legacy._u(cas, '<i', at)[0]
        out.append((to, cas_legacy._text(cas, to)))
    return out


def menu_cas(cas: bytes) -> bytes | None:
    """None when the CAS has an anmgroup named exactly MENU_ANMGROUP; else the CAS with the one anmgroup named so
    ignoring case renamed to it in place (ValueError: none / more than one, or its name string shared with another
    field: renaming would rename that too)."""
    import cas_legacy
    groups = anmgroups(cas)
    if any(n == MENU_ANMGROUP for _, n in groups):
        return None
    hits = [(to, n) for to, n in groups if n.lower() == MENU_ANMGROUP]
    if len(hits) != 1:
        raise ValueError(f'CAS anmgroups {[n for _, n in groups]}: not exactly one named {MENU_ANMGROUP!r} ignoring case')
    to, _ = hits[0]
    lay = cas_legacy.cas_layout(cas)
    users = sum(1 for f in lay.string_fields if f + cas_legacy._u(cas, '<i', f)[0] == to)
    canm = {c.name_at for c in lay.canm.clips} | set(lay.canm.name_ats)
    if users != 1 or to in canm:
        raise ValueError(f'CAS anmgroup name at {to:#x} is shared ({users} fields, CANM {to in canm})')
    out = bytearray(cas)
    out[to:to + 2 * len(MENU_ANMGROUP)] = MENU_ANMGROUP.encode('utf-16le')
    return bytes(out)


def templates(game: rootcpk.Game) -> Templates:
    """Every template by name: their DSGO roots (Root.cpk)."""
    return {n: dsgo.parse(game.read('OBJECT', n + '.SGO')).root for n in template_names()}


def body(root: dsgo.Node) -> tuple[str, str]:
    """A template's (archive, model) from animation_model[0]."""
    first = root.get('animation_model').items[0].items
    return first[0], first[1]


def colourable(game: rootcpk.Game, archive: str, model: str) -> set[str]:
    """The materials of `model` (in Object/`archive`) that take change_color0/1."""
    rab = mdb.rab_read(game.read('OBJECT', archive.rsplit('/', 1)[-1]))
    data = next(f.data for f in rab.files if f.name.lower() == model.lower())
    md = mdb.mdb_read(data)
    return {md.name_of(m.name) for m in md.materials if {p.name for p in m.params} >= set(CHANNELS)}


def selectors(root: dsgo.Node, every: set[str]) -> list[Selector]:
    """change_color0/1's selector the menu needs for a template's soldier_color: EVERY when it colours every colourable
    material (or names none), the one material it colours otherwise; ValueError when two entries cannot say it. A
    face's entry (the Wing Diver leader's tint: support_loadout.recolour keeps it too) is no uniform channel."""
    out: list[Selector] = []
    for channel in CHANNELS:
        named = set()
        for entry in root.get('soldier_color').items:
            sels = entry.items[0].items
            if any(isinstance(s.items[0], str) and 'face' in s.items[0].lower() for s in sels):
                continue
            for sel in sels:
                if sel.items[1] == channel:
                    named.add(EVERY if isinstance(sel.items[0], float) else sel.items[0])
        if EVERY in named or not named or named == every:
            out.append(EVERY)
        elif len(named) == 1:
            out.append(named.pop())
        else:
            raise ValueError(f'{channel} 上色 {sorted(map(str, named))}，菜单一项只能选一个材质或全部')
    return out


def unify_soldier(game: rootcpk.Game, name: str, data: bytes,
                  every: dict[tuple[str, str], set[str]] | None = None) -> bytes:
    """Template `name`'s battle file `data` (a DSGO: the stock one or a support_loadout variant of it) with the menu
    keys added; ValueError when the result is no unified file (menu_model.menu_problems) or a battle key moved.
    A file that already is unified comes back as it is."""
    if data[:4] != b'DSGO':
        raise ValueError(f'{name} 不是 DSGO 士兵模板')
    stock = menu_model.STOCK_MENU.get(name[:4])
    if stock is None:
        raise ValueError(f'{name} 没有对应的菜单原型')
    doc = dsgo.parse(data)
    archive, model = body(doc.root)
    every = {} if every is None else every
    if (archive, model) not in every:
        every[(archive, model)] = colourable(game, archive, model)
    _, menu = sgo.read(game.read(menu_model.MENU_FOLDER, stock))
    keys = menu_model.soldier_menu_keys(menu, archive, model, selectors(doc.root, every[(archive, model)]))
    menu_model.add_dsgo(doc.root, keys)
    out = dsgo.write(doc)
    check(game, name, data, out)
    return out


def check(game: rootcpk.Game, name: str, battle: bytes, unified: bytes, root: str | None = None) -> None:
    """ValueError when `unified` lacks what either loader needs or a battle key differs from `battle`."""
    def read_cas(path: str) -> bytes | None:
        import support_loadout
        if path.lower() == menu_model.menu_cas_path(JET_CAS):
            return jet_menu_cas(game)   # this tool's own file: made here, whether or not it is on disk yet
        try:
            return support_loadout._source(root, game, *cas_ref(path))
        except ValueError:
            return None
    problems = menu_model.menu_problems(unified, read_cas)
    moved = menu_model.battle_equal(battle, unified)
    if problems or moved:
        raise ValueError(f'{name} 不是统一模型文件：{problems + [f"战斗键 {k} 变了" for k in moved]}')


def unify_vehicle(game: rootcpk.Game, name: str, data: bytes, root: str | None = None) -> bytes:
    """A vehicle's battle file `data` (a classic SGO: a support_loadout loaded tank / mech) with the vehicle menu keys
    (menu_model.vehicle_menu_keys: its own model, MAB and CAS; the jets' CAS the renamed menu CAS) added; a file that
    has them (a jet from pylib/vcobjects.py jet_sgo) comes back as it is. ValueError as unify_soldier."""
    version, members = sgo.read(data)
    if menu_model.MENU_MODEL in members:
        check(game, name, data, data, root)
        return data
    cas = members[menu_model.BATTLE_MODEL][1]
    menu = menu_model.menu_cas_path(cas) if cas.lower() == JET_CAS else cas
    _, stock = sgo.read(game.read(menu_model.MENU_FOLDER, menu_model.VEHICLE_STOCK))
    out = sgo.write_depth_first(version, menu_model.add_sgo(
        members, menu_model.vehicle_menu_keys(stock, members[menu_model.BATTLE_MODEL], menu)))
    check(game, name, data, out, root)
    return out


def jet_menu_cas(game: rootcpk.Game) -> bytes:
    """MENUOBJECT/EDF6VC_MENU_V506_HELI.CAS: the jets' CAS with `Main` named `main`."""
    renamed = menu_cas(game.read(*cas_ref(JET_CAS)))
    if renamed is None:
        raise ValueError(f'{JET_CAS} 已有 {MENU_ANMGROUP!r}：菜单 CAS 不再需要，改 pylib/vcobjects.py jet_sgo')
    return renamed


def build(game: rootcpk.Game, root: str | None = None) -> dict[str, bytes]:
    """{Mods path: bytes}: every template's unified file and the jets' menu CAS, from Root.cpk (read only). `root` is
    unused (the files depend on Root.cpk alone)."""
    del root
    every: dict[tuple[str, str], set[str]] = {}
    files = {f'OBJECT/{n}.SGO': unify_soldier(game, n, game.read('OBJECT', n + '.SGO'), every) for n in template_names()}
    files[jet_cas_rel()] = jet_menu_cas(game)
    return files


def missing(root: str) -> list[str]:
    """The Mods paths of rels() not on disk."""
    led = ledger.Ledger(root)
    return [rel for rel in rels() if not os.path.isfile(led.disk(rel))]


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` as OWNER's, each only when it is not already exactly that; what OWNER wrote before and does not
    now (the old menu SGOs EDF6VC_MENU_*.SGO among them) is released. The game must not be running (install_new when it
    may be). Returns the paths written."""
    led = ledger.Ledger(root)
    paths = [led.put(OWNER, rel, data) for rel, data in files.items()
             if not (sha256_file(led.disk(rel)) == sha256(data) and OWNER in led.owners(rel))]
    led.release(OWNER, sorted(set(led.owned_by(OWNER)) - {ledger.key(rel) for rel in files}))
    return paths


def install_new(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes the `files` whose names are free (ledger.put_new: never a file overwritten), so the game may be running.
    Returns the paths written."""
    led = ledger.Ledger(root)
    return [p for p in (led.put_new(OWNER, rel, data) for rel, data in files.items()) if p]


def remove(root: str) -> tuple[list[str], list[str]]:
    """Releases this tool's files (whatever the ledger has as OWNER's): (deleted, kept changed)."""
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER)) | {ledger.key(r) for r in rels()}), writer=True)
