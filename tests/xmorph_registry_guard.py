"""Source guard: every ported X-Morph: Defense unit (pylib/xm_units.py ported()) is in every registry a vehicle of its family
must be in, from the catalog alone, and nothing else stands in for it (no tools/make_<key>.py stub: tools/make_xmorph.py
builds every unit). A new row with status 'ported' shows up in each of these, or this fails naming the one it is missing
from:

  the request (tools/calls.py CALLS, KINDS, GROUND_NOTES_BY_KIND; src/calls.inc), the vehicle (pylib/vcobjects.py
  GROUND_VEHICLES, TRACKED; its weapons' twin cadence in src/stores.inc), its model folder (pylib/wt_tracked.py FOLDERS, an emplacement's pylib/emplacement_model.py KINDS),
  its builder (tools/make_tracked_combat.py KINDS, LABELS, GUNS, NAMES, WT_ONLY; pylib/buildcache.py generator / tool),
  its files' owner (pylib/ledger.py OWNERS), its build group (pylib/buildcache.py GROUPS, COCKPIT_GROUPS, MODEL_INPUTS),
  its cockpit (pylib/cockpit_fits.py FITS), the test range and the M map (testrange/gen.py GROUND_MISSION,
  TRACKED_MISSION, CATALOG_ONLY; testrange/catalog_archives.json; src/spawn_catalog.inc), the release's model pack
  (tools/build_release.py RELEASE_MODELS) and the frozen installer's hidden imports.

Registration does not depend on the model folders a machine has (CI has none): nothing here reads one. Read only; no game.

    python tests/xmorph_registry_guard.py
"""
from __future__ import annotations

import json
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path[:0] = [os.path.join(ROOT, 'pylib'), os.path.join(ROOT, 'tools'), os.path.join(ROOT, 'testrange')]
import buildcache  # noqa: E402
import build_release  # noqa: E402
import calls  # noqa: E402
import cockpit_fits  # noqa: E402
import emplacement_model  # noqa: E402
import gen  # noqa: E402
import ledger  # noqa: E402
import make_tracked_combat as mtc  # noqa: E402
import make_xmorph  # noqa: E402
import vcobjects as vc  # noqa: E402
import wt_tracked  # noqa: E402
import xm_units  # noqa: E402

checks = failed = 0


def check(ok: bool, what: str) -> None:
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print('FAIL', what)


def text(rel: str) -> str:
    with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read()


calls_inc, stores_inc, spawn_inc = text('src/calls.inc'), text('src/stores.inc'), text('src/spawn_catalog.inc')
archives = json.loads(text('testrange/catalog_archives.json'))['rows']
release = text('tools/build_release.py')
hidden = release.split("for mod in ('call_weapons'", 1)[1].split('):', 1)[0]
ported = xm_units.ported()
check(bool(ported), 'no ported unit: the guard checks nothing')
for u in xm_units.UNITS.values():
    check(not os.path.exists(os.path.join(ROOT, 'tools', f'make_{u.key}.py')),
          f'{u.key}: a tools/make_{u.key}.py stub (tools/make_xmorph.py builds every unit)')
for u in [u for u in ported if u.family == 'heli']:
    # a helicopter: a vehicle request of the stock V506 in its own model (pylib/xm_heli.py), built by make_xmorph
    key, upper = u.key, u.key.upper()
    sgo = f'EDF6VC_{upper}'
    mine = [c for c in calls.CALLS if c.heli == key]
    check(len(mine) == 1 and not mine[0].ground and not mine[0].jet and mine[0].mark == 0,
          f'{key}: {len(mine)} helicopter requests in tools/calls.py CALLS (want 1)')
    if mine:
        c = mine[0]
        check(c.id == calls.ID_PREFIX + upper and c.vehicle == sgo and c.brings == 'vehicle' and c.kind == key,
              f'{key}: its request {c.id} is not a vehicle request of {sgo}')
        at = calls.IDS.index(c.id)
        check(at > 0 and calls.IDS[at - 1] == u.after, f'{key}: its row does not follow {u.after} (Unit.after)')
        check(f'{{L"{c.id}",L"{c.id}.SGO",L"{sgo}.SGO"}}' in calls_inc, f'{key}: src/calls.inc lacks {c.id} (gen_calls)')
    check(set(calls.KINDS.get(key, {})) == set(xm_units.LANGS), f'{key}: tools/calls.py KINDS lacks its texts')
    check(key not in vc.GROUND_VEHICLES and key not in mtc.KINDS, f'{key}: a helicopter among the ground vehicles')
    check(key in make_xmorph.KINDS and key in make_xmorph.HELIS and buildcache.generator(key) == 'make_xmorph'
          and buildcache.tool(key) is make_xmorph.builder(key), f'{key}: tools/make_xmorph.py / pylib/buildcache.py tool')
    check(key in ledger.OWNERS and key in buildcache.GROUPS and key not in buildcache.COCKPIT_GROUPS
          and buildcache.MODEL_INPUTS.get(key) == xm_units.folder(key), f'{key}: pylib/buildcache.py groups / ledger')
    check(xm_units.folder(key) in build_release.RELEASE_MODELS, f'{key}: tools/build_release.py RELEASE_MODELS')
    check({'guns', 'missile'} <= set(u.air) or set(u.air) == {'seats'}, f'{key}: Unit.air has neither its arms nor seats')
import re  # noqa: E402
bodies = {b: (sgo_file, float(mark), role) for b, sgo_file, mark, role in re.findall(
    r'\{Body::(\w+),L"[^"]*",L"([^"]*)",(\d+)\.0f,Role::(\w+),', re.sub(r'\s+', ' ', text('src/jet_internal.h')))}
boardable = {b: float(ram) for b, ram in re.findall(r'\{Body::(\w+),Airframe::wing,Arm::none,Wing\("\w+",MarkOf\(Body::\w+\),'
                                                     r'Npc\(jet::Role::\w+\),([\d.]+)f\)', text('src/playerjet_kinds.h'))}
for u in [u for u in ported if u.family == 'jet']:
    # a jet: one of the plugin's aircraft (pylib/vcobjects.py XM_JETS, src/jet_internal.h kBodies) in its own model
    # (pylib/xm_jet.py), the player's to board (src/playerjet_kinds.h kBoardable), requested empty to fly
    key, upper, air = u.key, u.key.upper(), u.air
    name, twin = xm_units.jet_name(key), xm_units.jet_request(key)
    mine = [c for c in calls.CALLS if c.jet == twin]
    check(len(mine) == 1 and not mine[0].ground and not mine[0].heli and mine[0].mark == air['mark'],
          f'{key}: {len(mine)} jet requests in tools/calls.py CALLS (want 1, its mark)')
    if mine:
        c = mine[0]
        check(c.id == calls.ID_PREFIX + upper and c.vehicle == f'EDF6VC_FLY_{upper}' and c.brings == 'vehicle'
              and c.kind == key, f'{key}: its request {c.id} is not a vehicle request of EDF6VC_FLY_{upper}')
        at = calls.IDS.index(c.id)
        check(at > 0 and calls.IDS[at - 1] == u.after, f'{key}: its row does not follow {u.after} (Unit.after)')
        check(f'{{L"{c.id}",L"{c.id}.SGO",L"EDF6VC_FLY_{upper}.SGO"}}' in calls_inc, f'{key}: src/calls.inc lacks {c.id}')
    check(set(calls.KINDS.get(key, {})) == set(xm_units.LANGS), f'{key}: tools/calls.py KINDS lacks its texts')
    check(vc.XM_JETS.get(name) == key and vc.JETS[name].mark == air['mark'] and vc.JETS[twin].requested
          and vc.JETS[twin].parked and air['mark'] in vc.BOARDABLE_MARKS, f'{key}: pylib/vcobjects.py XM_JETS / JETS')
    check(name not in vc.REQUEST_KINDS and not name.endswith('_mission'), f'{key}: a make_jets / test range kind')
    body = air['body']
    check(bodies.get(body) == (f'EDF6VC_{upper}.SGO', air['mark'], air['role']),
          f'{key}: src/jet_internal.h kBodies {body} is not {air["mark"]} {air["role"]}')
    check(boardable.get(body) == air['ram'], f'{key}: src/playerjet_kinds.h kBoardable {body} (ram {air["ram"]})')
    for a in air['stores']:
        check(f'{{L"EDF6VC_{vc.xm_store(u, a)}_"' in stores_inc, f'{key}: src/stores.inc lacks {vc.xm_store(u, a)} (gen_stores)')
    check(f'{{{air["mark"]:.1f}f,L"' in stores_inc, f'{key}: src/stores.inc kJetWeapons lacks its mark (gen_stores)')
    check(key in make_xmorph.KINDS and key in make_xmorph.JETS and buildcache.generator(key) == 'make_xmorph'
          and buildcache.tool(key) is make_xmorph.builder(key), f'{key}: tools/make_xmorph.py / pylib/buildcache.py tool')
    check(key in ledger.OWNERS and key in buildcache.GROUPS and key not in buildcache.COCKPIT_GROUPS
          and buildcache.MODEL_INPUTS.get(key) == xm_units.folder(key), f'{key}: pylib/buildcache.py groups / ledger')
    check(xm_units.folder(key) in build_release.RELEASE_MODELS, f'{key}: tools/build_release.py RELEASE_MODELS')
for module in ('xm_heli', 'xm_jet', 'xm_air_model'):
    check(f"'{module}'" in hidden, f'tools/build_release.py: {module} is not a hidden import of the installer')
for u in xm_units.v603():
    key, upper, mission = u.key, u.key.upper(), f'{gen.DERIVED_PREFIX}{u.key}_mission'
    sgo = f'EDF6VC_{upper}'
    # the request
    mine = [c for c in calls.CALLS if c.ground == key]
    check(len(mine) == 1, f'{key}: {len(mine)} requests in tools/calls.py CALLS (want 1)')
    if mine:
        c = mine[0]
        check(c.id == calls.ID_PREFIX + upper and c.vehicle == sgo and c.brings == 'vehicle' and c.kind == key,
              f'{key}: its request {c.id} is not a vehicle request of {sgo}')
        at = calls.IDS.index(c.id)
        check(at > 0 and calls.IDS[at - 1] == u.after, f'{key}: its row does not follow {u.after} (Unit.after)')
        check(f'{{L"{c.id}",L"{c.id}.SGO",L"{sgo}.SGO"}}' in calls_inc, f'{key}: src/calls.inc lacks {c.id} (gen_calls)')
    check(set(calls.KINDS.get(key, {})) == set(xm_units.LANGS), f'{key}: tools/calls.py KINDS lacks its texts')
    check(calls.GROUND_NOTES_BY_KIND.get(key) is calls.PLUGIN_NOTES, f'{key}: GROUND_NOTES_BY_KIND')
    # the vehicle
    v = vc.GROUND_VEHICLES.get(key)
    check(v is not None and v.sgo == sgo and v.tool == 'make_xmorph' and v.durability == u.durability,
          f'{key}: pylib/vcobjects.py GROUND_VEHICLES')
    check(key in vc.TRACKED, f'{key}: pylib/vcobjects.py TRACKED')
    if v is not None and v.alternate:
        for w in v.weapons[:2]:
            check(f'{{L"{w.rsplit("/", 1)[1].upper()}",' in stores_inc, f'{key}: src/stores.inc lacks {w} (gen_stores)')
    # its other weapons (xm_units.Arm): each a store on its own fire key (one weapon a key), its muzzle slot after the pair
    for a in u.arms:
        kind = vc.xm_store(u, a)
        check(kind in vc.STORES and vc.STORES[kind].key == a.fire and vc.STORES[kind].auto == a.auto
              and vc.STORES[kind].seat == a.seat
              and f'{{L"EDF6VC_{kind}_",' in stores_inc, f'{key}: arm {a.key}: STORES {kind} / src/stores.inc (gen_stores)')
    keyed = [a.fire for a in u.arms if a.fire]
    check(len(set(keyed)) == len(keyed), f'{key}: two arms on one fire key')
    # its gunner seats (Arm.seat): one arm a seat, numbered 1.. in arms order, each with its own turret and no key
    seated = [a.seat for a in u.arms if a.seat]
    check(seated == list(range(1, len(seated) + 1)), f'{key}: the gunner seats of its arms {seated} are not 1..n in order')
    check(all((a.turret and not a.fire) if a.seat else a.fire for a in u.arms), f'{key}: a seat arm has a key or no turret')
    check(v is None or v.slots == ('left', 'right', *(f'xm_{a.key}' for a in u.arms)), f'{key}: its slots are not its arms')
    # its model: a tracked or wheeled unit's a wt_tracked folder (WT_ONLY its camera), an emplacement's on the
    # fortified base
    if u.family in xm_units.HULLS:
        check(wt_tracked.FOLDERS.get(key) == xm_units.folder(key) and key in mtc.WT_ONLY,
              f'{key}: pylib/wt_tracked.py FOLDERS / make_tracked_combat WT_ONLY')
    else:
        e = emplacement_model.KINDS.get(key)
        check(e is not None and e.folder == xm_units.folder(key) and e.eye == xm_units.eye(u) and key in mtc.EMPLACEMENTS
              and key in mtc.ONE_CRADLE, f'{key}: pylib/emplacement_model.py KINDS / make_tracked_combat EMPLACEMENTS')
    # its builder: one generator for every unit
    check(key in mtc.KINDS and key in mtc.LABELS and key in mtc.GUNS and mtc.NAMES.get(key) == u.gun,
          f'{key}: tools/make_tracked_combat.py tables (KINDS, LABELS, GUNS, NAMES)')
    check(buildcache.generator(key) == 'make_xmorph' and buildcache.tool(key) is make_xmorph.builder(key)
          and buildcache.tool(key).OWNER == key, f'{key}: pylib/buildcache.py generator / tool')
    # its files and its build group
    check(key in ledger.OWNERS, f'{key}: pylib/ledger.py OWNERS')
    check(key in buildcache.GROUPS and key in buildcache.COCKPIT_GROUPS
          and buildcache.MODEL_INPUTS.get(key) == xm_units.folder(key), f'{key}: pylib/buildcache.py groups')
    check(f'EDF6VC_{upper}' in cockpit_fits.FITS, f'{key}: pylib/cockpit_fits_plugin.py FITS')
    # the test range and the M map
    check(gen.GROUND_MISSION.get(mission) == 'make_xmorph' and mission in gen.TRACKED_MISSION
          and mission in gen.CATALOG_ONLY, f'{key}: testrange/gen.py GROUND_MISSION / TRACKED_MISSION / CATALOG_ONLY')
    check(f'"{mission.upper()}"' in spawn_inc, f'{key}: src/spawn_catalog.inc lacks {mission} (gen_spawn_catalog)')
    check(f'OBJECT/{sgo}.MRAB' in archives.get(mission, ()), f'{key}: testrange/catalog_archives.json lacks {mission}')
    # the release
    check(xm_units.folder(key) in build_release.RELEASE_MODELS, f'{key}: tools/build_release.py RELEASE_MODELS')
for module in ('make_xmorph', 'xm_units'):
    check(f"'{module}'" in hidden, f'tools/build_release.py: {module} is not a hidden import of the installer')

print(f'xmorph_registry_guard: {len(ported)} ported units, {checks} checks, {failed} failed')
sys.exit(1 if failed else 0)
