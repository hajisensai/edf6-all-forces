"""The menu SGOs the room / HQ 3D preview of a support soldier shows (docs/room-3d-preview-re.md section 3; the plugin's
src/room_preview.cpp loads them by MENU_PATHS). The game's menu renderer only takes a menu SGO (animation_model with the
menu CAS, change_color, random_animation, soldier_weapon_slot), never an AI template (a DSGO with soldier_color), so:

  Ranger      the AI soldiers' body is n601_common_ranger (every N601_COMMON_RANGER_* template and its _LEADER), not the
              player's: MENUOBJECT/EDF6VC_MENU_RANGER.SGO is the stock MENUOBJECT/P505_RANGER.SGO with only
              animation_model[0] (archive, model) pointed at the templates' (same EDF6ArmySoldier skeleton).
  Ranger leader  MENUOBJECT/EDF6VC_MENU_RANGER_L.SGO: the same with change_color naming the material the _LEADER
              templates colour. The renderer (EDF.dll Draw 0x93C370) reads change_color[0] and [1] only, entry i with
              colour i; an entry's selector is a material name (a string: that material) or 0 (every material). The
              n601 model's colourable materials are p609_proto_ranger_common_body and _head (hair has no colour
              parameters): the FL / RL / SG / SN leaders colour the head alone (-> _L); the AF leader colours head and
              body, i.e. every colourable material (-> the plain file). leader_files() derives this from the data.
  Wing Diver / Fencer  the AI soldiers wear the player's p606 / p607: the stock MENUOBJECT/P606_WINGDIVER.SGO /
              P607_FENCER.SGO as they are, no file here (build checks the bodies still agree).
  Vehicles    MENUOBJECT/EDF6VC_MENU_V_<BODY>.SGO, one a support_loadout.BODIES body (the tank, the jets): the stock
              P505_RANGER.SGO with animation_model replaced whole by the body's own ([model], CAS, MAB of its base SGO as
              the game loads it: the Mods copy other tools made, else Root.cpk), so the menu renderer draws the vehicle
              (src/room_preview_logic.h VehicleMenuOf; it gets no weapons, its colour channels match no material). A
              body whose base SGO is nowhere (its model not installed) gets no file: it is not deployed either.

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
import rootcpk  # noqa: E402
import sgo  # noqa: E402
from modfiles import sha256, sha256_file  # noqa: E402
from i18n import tr  # noqa: E402

OWNER = 'menu_preview'   # pylib/ledger.py OWNERS
FOLDER = 'MENUOBJECT'
RANGER = 'EDF6VC_MENU_RANGER.SGO'
RANGER_LEADER = 'EDF6VC_MENU_RANGER_L.SGO'
FILES = (RANGER, RANGER_LEADER)
VEHICLE_PREFIX = 'EDF6VC_MENU_V_'   # src/room_preview_logic.h kMenuVehiclePrefix
# What the plugin hands the view (the path form CONFIG.SGO SoldierInit uses, e.g. APP:/MENUOBJECT/P505_RANGER.SGO).
MENU_PATHS = {name: f'app:/{FOLDER}/{name}' for name in FILES}
STOCK_RANGER = 'P505_RANGER.SGO'
STOCK_BODIES = {'N606': 'P606_WINGDIVER.SGO', 'N607': 'P607_FENCER.SGO'}   # template prefix -> stock menu SGO it shares
CHANNELS = ('change_color0', 'change_color1')
EVERY = 0   # change_color selector: every material

Selector = int | str
Templates = dict[str, dsgo.Node]


def rels() -> list[str]:
    """The soldiers' files (always made; loadout-generate remakes them when missing)."""
    return [f'{FOLDER}/{name}' for name in FILES]


def vehicle_file(body: str) -> str:
    """A support_loadout.BODIES body's menu SGO (src/room_preview_logic.h VehicleMenuOf)."""
    return f'{VEHICLE_PREFIX}{body}.SGO'


def vehicle_menu_sgo(stock: bytes, vehicle: bytes) -> bytes:
    """The stock menu SGO with animation_model = the vehicle SGO's whole animation_model (model, CAS, MAB)."""
    version, members = sgo.read(stock)
    _, v = sgo.read(vehicle)
    members['animation_model'] = list(v['animation_model'])
    return sgo.write_depth_first(version, members)


def build_vehicles(game: rootcpk.Game, root: str | None) -> dict[str, bytes]:
    """{'MENUOBJECT/<vehicle_file>': bytes} for every body whose base SGO the game would load (support_loadout._source:
    `root`'s Mods copy, else Root.cpk). A body without one is said and left out."""
    import support_loadout
    stock = game.read(FOLDER, STOCK_RANGER)
    out: dict[str, bytes] = {}
    for body, (base, _most, _tank) in support_loadout.BODIES.items():
        data = support_loadout._source(root, game, 'OBJECT', base)
        if data is None:
            print(tr(f'！ {base} 不在（该机体没有安装，也不会出动）：它的 3D 预览不生成',
                     f'! {base} is not there (that body is not installed and never goes out): no 3D preview for it'))
            continue
        out[f'{FOLDER}/{vehicle_file(body)}'] = vehicle_menu_sgo(stock, data)
    return out


def _kinds() -> list[str]:
    import support_loadout   # the 15 kinds; the stock AI templates of Root.cpk are the 14 with looks (not the X-Morph infantry)
    return [t for n, _, t in support_loadout.KINDS if support_loadout.has_looks(n)]


def templates(game: rootcpk.Game) -> Templates:
    """Every kind's AI template and its _LEADER, by name: their DSGO roots."""
    names = [t + suffix for t in _kinds() for suffix in ('', '_LEADER')]
    return {n: dsgo.parse(game.read('OBJECT', n + '.SGO')).root for n in names}


def body(root: dsgo.Node) -> tuple[str, str]:
    """A template's (archive, model) from animation_model[0]."""
    first = root.get('animation_model').items[0].items
    return first[0], first[1]


def ranger_body(tpl: Templates) -> tuple[str, str]:
    """The one body every Ranger template (leader or not) wears; ValueError when they differ."""
    bodies = {body(r) for n, r in tpl.items() if n.startswith('N601_')}
    if len(bodies) != 1:
        raise ValueError(f'游骑兵模板的身体不一致：{sorted(bodies)}')
    return bodies.pop()


def check_stock_bodies(game: rootcpk.Game, tpl: Templates) -> None:
    """Wing Diver / Fencer soldiers must wear the stock menu SGO's model (case-insensitive): else the stock preview
    would show another body. ValueError naming the template when not."""
    for prefix, menu in STOCK_BODIES.items():
        _, stock = sgo.read(game.read(FOLDER, menu))
        want = tuple(s.lower() for s in stock['animation_model'][0][:2])
        for name, root in tpl.items():
            if name.startswith(prefix) and tuple(s.lower() for s in body(root)) != want:
                raise ValueError(f'{name} 的身体 {body(root)} 不是 {menu} 的 {want}')


def colourable(game: rootcpk.Game, archive: str, model: str) -> set[str]:
    """The materials of `model` (in Object/`archive`) that take change_color0/1."""
    rab = mdb.rab_read(game.read('OBJECT', archive.rsplit('/', 1)[-1]))
    data = next(f.data for f in rab.files if f.name.lower() == model.lower())
    md = mdb.mdb_read(data)
    return {md.name_of(m.name) for m in md.materials if {p.name for p in m.params} >= set(CHANNELS)}


def selectors(root: dsgo.Node, every: set[str]) -> list[Selector]:
    """change_color0/1's selector the menu needs for a template's soldier_color: EVERY when it colours every colourable
    material (or names none), the one material it colours otherwise; ValueError when two entries cannot say it."""
    out: list[Selector] = []
    for channel in CHANNELS:
        named = set()
        for entry in root.get('soldier_color').items:
            for sel in entry.items[0].items:
                if sel.items[1] == channel:
                    named.add(EVERY if isinstance(sel.items[0], float) else sel.items[0])
        if EVERY in named or named == every:
            out.append(EVERY)
        elif len(named) == 1:
            out.append(named.pop())
        else:
            raise ValueError(f'{channel} 上色 {sorted(map(str, named))}，菜单一项只能选一个材质或全部')
    return out


def leader_files(game: rootcpk.Game, tpl: Templates | None = None) -> dict[str, str]:
    """Each Ranger template (and _LEADER) -> the file of FILES the preview shows it with. ValueError when the data need
    a third form (then this module is out of date)."""
    tpl = tpl or templates(game)
    every = colourable(game, *ranger_body(tpl))
    forms = {name: selectors(root, every) for name, root in tpl.items() if name.startswith('N601_')}
    leader = {tuple(s) for n, s in forms.items() if n.endswith('_LEADER') and s != [EVERY, EVERY]}
    if len(leader) > 1 or any(s != [EVERY, EVERY] for n, s in forms.items() if not n.endswith('_LEADER')):
        raise ValueError(f'游骑兵上色方式超出两种：{forms}')
    return {n: RANGER if s == [EVERY, EVERY] else RANGER_LEADER for n, s in forms.items()}


def menu_sgo(stock: bytes, archive: str, model: str, sel: list[Selector]) -> bytes:
    """The stock menu SGO with animation_model[0] = [archive, model] and change_color's selectors `sel`."""
    version, members = sgo.read(stock)
    members['animation_model'][0] = [archive, model]
    members['change_color'] = [[s, channel] for s, channel in zip(sel, CHANNELS)]
    return sgo.write_depth_first(version, members)


def build(game: rootcpk.Game, root: str | None = None) -> dict[str, bytes]:
    """{'MENUOBJECT/<file>': bytes} for FILES, from Root.cpk (read only); with the game directory `root` the vehicles'
    too (build_vehicles: their bases may be other tools' Mods files)."""
    tpl = templates(game)
    check_stock_bodies(game, tpl)
    archive, model = ranger_body(tpl)
    every = colourable(game, archive, model)
    picked = leader_files(game, tpl)
    head = next((selectors(tpl[n], every) for n, f in picked.items() if f == RANGER_LEADER), [EVERY, EVERY])
    stock = game.read(FOLDER, STOCK_RANGER)
    files = {f'{FOLDER}/{RANGER}': menu_sgo(stock, archive, model, [EVERY, EVERY]),
             f'{FOLDER}/{RANGER_LEADER}': menu_sgo(stock, archive, model, head)}
    if root is not None:
        files.update(build_vehicles(game, root))
    return files


def missing(root: str) -> list[str]:
    """The Mods paths of FILES not on disk."""
    led = ledger.Ledger(root)
    return [rel for rel in rels() if not os.path.isfile(led.disk(rel))]


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` as OWNER's, each only when it is not already exactly that; what OWNER wrote before and does not
    now is released. The game must not be running (install_new when it may be). Returns the paths written."""
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
    """Releases this tool's files (the vehicles' too: whatever the ledger has as OWNER's): (deleted, kept changed)."""
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER)) | {ledger.key(r) for r in rels()}), writer=True)
