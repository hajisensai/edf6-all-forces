"""Source guard: every ported X-Morph: Defense unit (pylib/xm_units.py ported()) is in every registry a vehicle of its family
must be in, from the catalog alone, and nothing else stands in for it (no tools/make_<key>.py stub: tools/make_xmorph.py
builds every unit). A new row with status 'ported' shows up in each of these, or this fails naming the one it is missing
from:

  the request (tools/calls.py CALLS, KINDS, GROUND_NOTES_BY_KIND; src/calls.inc), the vehicle (pylib/vcobjects.py
  GROUND_VEHICLES, TRACKED; its weapons' twin cadence in src/stores.inc), its model folder (pylib/wt_tracked.py FOLDERS, an emplacement's pylib/emplacement_model.py KINDS),
  its builder (tools/make_tracked_combat.py KINDS, LABELS, GUNS, NAMES, WT_ONLY; pylib/buildcache.py generator / tool),
  its files' owner (pylib/ledger.py OWNERS), its build group (pylib/buildcache.py GROUPS, COCKPIT_GROUPS, MODEL_INPUTS),
  its cockpit (pylib/cockpit_fits.py FITS), the test range and the M map (testrange/gen.py GROUND_MISSION,
  TRACKED_MISSION / WALKER_MISSION, CATALOG_ONLY; testrange/catalog_archives.json; src/spawn_catalog.inc), the release's model pack
  (tools/build_release.py RELEASE_MODELS) and the frozen installer's hidden imports.

By family (pylib/xm_units.py): the request rows are the requested units' (V603 and the walkers); the V603's tables, the
cockpits the V603 units'; the test range both (a walker's WALKER_MISSION); a walker's vehicle its host's holders (pylib/xm_walker.py weapons, its rig
in pylib/xm_rig.py RIGS); the infantry a support soldier kind (its bodies in src/support_soldier.cpp kBodies), no request.

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
import xm_infantry  # noqa: E402
import xm_rig  # noqa: E402
import xm_units  # noqa: E402
import xm_walker  # noqa: E402

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
soldier_cpp = text('src/support_soldier.cpp')
archives = json.loads(text('testrange/catalog_archives.json'))['rows']
release = text('tools/build_release.py')
hidden = release.split("for mod in ('call_weapons'", 1)[1].split('):', 1)[0]
ported = xm_units.ported()
check(bool(ported), 'no ported unit: the guard checks nothing')
for u in xm_units.UNITS.values():
    check(not os.path.exists(os.path.join(ROOT, 'tools', f'make_{u.key}.py')),
          f'{u.key}: a tools/make_{u.key}.py stub (tools/make_xmorph.py builds every unit)')
requested, v603 = {u.key for u in xm_units.requested()}, {u.key for u in xm_units.v603()}
for u in ported:
    key, upper, mission = u.key, u.key.upper(), f'{gen.DERIVED_PREFIX}{u.key}_mission'
    sgo = f'EDF6VC_{upper}'
    # the request
    mine = [c for c in calls.CALLS if c.ground == key]
    check(len(mine) == (1 if key in requested else 0),
          f'{key}: {len(mine)} requests in tools/calls.py CALLS (want {1 if key in requested else 0})')
    if mine:
        c = mine[0]
        check(c.id == calls.ID_PREFIX + upper and c.vehicle == sgo and c.brings == 'vehicle' and c.kind == key,
              f'{key}: its request {c.id} is not a vehicle request of {sgo}')
        at = calls.IDS.index(c.id)
        check(at > 0 and calls.IDS[at - 1] == u.after, f'{key}: its row does not follow {u.after} (Unit.after)')
        check(f'{{L"{c.id}",L"{c.id}.SGO",L"{sgo}.SGO"}}' in calls_inc, f'{key}: src/calls.inc lacks {c.id} (gen_calls)')
    check(set(calls.KINDS.get(key, {})) == set(xm_units.LANGS), f'{key}: tools/calls.py KINDS lacks its texts')
    # its builder: one generator for every unit
    check(buildcache.generator(key) == 'make_xmorph' and buildcache.tool(key) is make_xmorph.builder(key)
          and buildcache.tool(key).OWNER == key, f'{key}: pylib/buildcache.py generator / tool')
    # its files and its build group
    check(key in ledger.OWNERS, f'{key}: pylib/ledger.py OWNERS')
    check(key in buildcache.GROUPS and key in buildcache.COCKPIT_GROUPS
          and buildcache.MODEL_INPUTS.get(key) == xm_units.folder(key), f'{key}: pylib/buildcache.py groups')
    # the release
    check(xm_units.folder(key) in build_release.RELEASE_MODELS, f'{key}: tools/build_release.py RELEASE_MODELS')
    if u.family == 'infantry':
        # a support soldier kind: its member and leader bodies the plugin's (pylib/xm_infantry.py writes them)
        member, leader = xm_infantry.body_files(key)
        check(f'{{L"app:/object/{member}",L"app:/object/{leader}"}}'.lower() in soldier_cpp.lower(),
              f'{key}: src/support_soldier.cpp kBodies lacks {member} / {leader}')
        check(key in xm_rig.RIGS and key not in vc.GROUND_VEHICLES, f'{key}: xm_rig.RIGS / not a ground vehicle')
        continue
    check(calls.GROUND_NOTES_BY_KIND.get(key) is calls.PLUGIN_NOTES, f'{key}: GROUND_NOTES_BY_KIND')
    # the vehicle
    v = vc.GROUND_VEHICLES.get(key)
    check(v is not None and v.sgo == sgo and v.tool == 'make_xmorph' and v.durability == u.durability,
          f'{key}: pylib/vcobjects.py GROUND_VEHICLES')
    if u.family == 'mech':
        # a walker: the host's class and request, the unit's weapons on the host's holders, its rig
        w = xm_walker.WALKERS.get(key)
        check(w is not None and v is not None and v.stock == w.stock and v.family == w.family
              and v.weapons == xm_walker.weapons(key) and v.weapons_at == w.weapons_at and key in xm_rig.RIGS,
              f'{key}: pylib/vcobjects.py GROUND_VEHICLES is not its walker (pylib/xm_walker.py WALKERS, xm_rig RIGS)')
        check(all(any(a.key == h.arm for a in u.arms) for h in w.holders if h.arm) if w else False,
              f'{key}: a holder names an arm the unit has not')
        check(f'EDF6VC_{upper}' in cockpit_fits.FITS, f'{key}: pylib/cockpit_fits_plugin.py FITS')
        # the test range and the M map (its host's SGO turned into a mission one)
        check(key in vc.WALKERS and gen.GROUND_MISSION.get(mission) == 'make_xmorph' and mission in gen.WALKER_MISSION
              and mission in gen.CATALOG_ONLY, f'{key}: testrange/gen.py GROUND_MISSION / WALKER_MISSION / CATALOG_ONLY')
        check(f'"{mission.upper()}"' in spawn_inc, f'{key}: src/spawn_catalog.inc lacks {mission} (gen_spawn_catalog)')
        check(f'OBJECT/{sgo}.MRAB' in archives.get(mission, ()), f'{key}: testrange/catalog_archives.json lacks {mission}')
        continue
    check(key in vc.TRACKED, f'{key}: pylib/vcobjects.py TRACKED')
    if v is not None and v.alternate:
        for w in v.weapons[:2]:
            check(f'{{L"{w.rsplit("/", 1)[1].upper()}",' in stores_inc, f'{key}: src/stores.inc lacks {w} (gen_stores)')
    # its other weapons (xm_units.Arm): each an intrinsic secondary store, its muzzle slot after the pair
    for a in u.arms:
        kind = vc.xm_store(u, a)
        check(kind in vc.STORES and vc.STORES[kind].independent_secondary
              and f'{{L"EDF6VC_{kind}_",' in stores_inc, f'{key}: arm {a.key}: STORES {kind} / src/stores.inc (gen_stores)')
    check(v is None or v.slots == ('left', 'right', *(f'xm_{a.key}' for a in u.arms)), f'{key}: its slots are not its arms')
    # its model: a tracked unit's a wt_tracked folder (WT_ONLY its camera), an emplacement's on the fortified base
    if u.family == 'tracked':
        check(wt_tracked.FOLDERS.get(key) == xm_units.folder(key) and key in mtc.WT_ONLY,
              f'{key}: pylib/wt_tracked.py FOLDERS / make_tracked_combat WT_ONLY')
    else:
        e = emplacement_model.KINDS.get(key)
        check(e is not None and e.folder == xm_units.folder(key) and e.eye == u.eye and key in mtc.EMPLACEMENTS
              and key in mtc.ONE_CRADLE, f'{key}: pylib/emplacement_model.py KINDS / make_tracked_combat EMPLACEMENTS')
    # its V603 builder (make_tracked_combat under make_xmorph)
    check(key in mtc.KINDS and key in mtc.LABELS and key in mtc.GUNS and mtc.NAMES.get(key) == u.gun,
          f'{key}: tools/make_tracked_combat.py tables (KINDS, LABELS, GUNS, NAMES)')
    check(f'EDF6VC_{upper}' in cockpit_fits.FITS, f'{key}: pylib/cockpit_fits_plugin.py FITS')
    # the test range and the M map
    check(gen.GROUND_MISSION.get(mission) == 'make_xmorph' and mission in gen.TRACKED_MISSION
          and mission in gen.CATALOG_ONLY, f'{key}: testrange/gen.py GROUND_MISSION / TRACKED_MISSION / CATALOG_ONLY')
    check(f'"{mission.upper()}"' in spawn_inc, f'{key}: src/spawn_catalog.inc lacks {mission} (gen_spawn_catalog)')
    check(f'OBJECT/{sgo}.MRAB' in archives.get(mission, ()), f'{key}: testrange/catalog_archives.json lacks {mission}')
for module in ('make_xmorph', 'xm_units', 'xm_walker', 'xm_infantry', 'xm_rig'):
    check(f"'{module}'" in hidden, f'tools/build_release.py: {module} is not a hidden import of the installer')

print(f'xmorph_registry_guard: {len(ported)} ported units, {checks} checks, {failed} failed')
sys.exit(1 if failed else 0)
