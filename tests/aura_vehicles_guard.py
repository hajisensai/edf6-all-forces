"""The aura trucks' registries agree (docs/feature-aura-vehicles.md): tools/make_aura_vehicles.py TRUCKS, in aura::Kind
order, carry src/aura_logic.h kMarkers; each is a ground vehicle (pylib/vcobjects.py), a request with its texts and notes
(tools/calls.py, src/calls.inc), a cockpit fit, a placeable range row; its group is the ledger's, the build cache's and
the installer's (install and uninstall). Where the game is here (Root.cpk), the trucks are built in memory too: each
model carries its marker under `body` and the driver's eye, its SGO draws it and is unarmed.

    python tests/aura_vehicles_guard.py
"""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path[:0] = [os.path.join(ROOT, p) for p in ('pylib', 'tools', 'testrange')]
import buildcache  # noqa: E402
import calls  # noqa: E402
import catalog  # noqa: E402
import cockpit_fits  # noqa: E402
import ledger  # noqa: E402
import make_aura_vehicles as aura  # noqa: E402
import vcobjects as vc  # noqa: E402

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


def registries() -> None:
    logic = text('src/aura_logic.h')
    markers = re.search(r'kMarkers\[kKinds\]=\{([^}]*)\}', logic)
    check(markers is not None, 'src/aura_logic.h: kMarkers not found')
    names = re.findall(r'L"([^"]+)"', markers.group(1)) if markers else []
    check(names == [t.marker for t in aura.TRUCKS], f'TRUCKS markers {[t.marker for t in aura.TRUCKS]} != kMarkers {names}')
    check(re.search(r'enum class Kind[^{]*\{\s*command=0,\s*repair=1,\s*ammo=2\s*\}', logic) is not None,
          'aura::Kind is not command, repair, ammo (TRUCKS order)')

    inc = text('src/calls.inc')
    installer = text('tools/installer.py')
    rows = {r[0]: r for r in catalog.INSTALLER_VEHICLES}
    for t in aura.TRUCKS:
        g = vc.GROUND_VEHICLES.get(t.key)
        check(g is not None and g.sgo == t.stem and g.stock == aura.HOST and g.tool == 'make_aura_vehicles' and not g.weapons,
              f'{t.key}: pylib/vcobjects.py GROUND_VEHICLES')
        call = [c for c in calls.CALLS if c.ground == t.key]
        check(len(call) == 1 and call[0].vehicle == t.stem and call[0].brings == 'vehicle' and call[0].kind == t.key,
              f'{t.key}: tools/calls.py has no request bringing {t.stem}')
        if call:
            check(f'{{L"{call[0].id}",L"{call[0].id}.SGO",L"{t.stem}.SGO"}}' in inc, f'{t.key}: src/calls.inc stale')
        check(set(calls.KINDS.get(t.key, {})) >= {'SC', 'CN', 'JA', 'EN'}, f'{t.key}: tools/calls.py KINDS texts')
        check(t.key in calls.GROUND_NOTES_BY_KIND, f'{t.key}: GROUND_NOTES_BY_KIND (it needs no EDF6AutoTurret)')
        check(0 in {f.seat for f in cockpit_fits.FITS.get(t.stem, ())}, f'{t.stem}: no driver cockpit fit')
        check(t.stem.lower() in rows and set(rows[t.stem.lower()][2]) == {f'OBJECT/{t.stem}.SGO', f'OBJECT/{t.stem}.MRAB'},
              f'{t.stem}: testrange/catalog.py INSTALLER_VEHICLES')
    check(aura.OWNER in ledger.OWNERS, 'ledger OWNERS lacks aura_vehicles')
    check(aura.OWNER in buildcache.GROUPS and aura.OWNER in buildcache.COCKPIT_GROUPS, 'buildcache GROUPS / COCKPIT_GROUPS')
    for needle in ("('aura_vehicles', tr(", 'make_aura_vehicles.install(game, aura)', "('aura_vehicles', aura)",
                   'make_aura_vehicles.remove,'):
        check(needle in installer, f'tools/installer.py lacks {needle}')
    check("'make_aura_vehicles'" in text('tools/build_release.py'), 'tools/build_release.py does not freeze make_aura_vehicles')


def built() -> None:
    import rootcpk
    if os.path.isfile(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
        import cockpits
        import sgo
        from mdb import bind_world, mdb_read, rab_read
        files = aura.build(rootcpk.DEFAULT_GAME)
        for t in aura.TRUCKS:
            arc = rab_read(files[f'OBJECT/{t.stem}.MRAB'])
            md = mdb_read(next(f.data for f in arc.files if f.name == t.model))
            at = md.bone_index(t.marker)
            check(at >= 0, f'{t.stem}: no marker {t.marker}')
            check(at >= 0 and md.bones[at].parent == md.bone_index('body'), f'{t.stem}: the marker is not under body')
            check(not cockpits.assets_present() or md.bone_index('vc_eye_0') >= 0, f'{t.stem}: no driver eye (cockpit not grafted)')
            others = [o.marker for o in aura.TRUCKS if o is not t]
            check(all(md.bone_index(o) < 0 for o in others), f'{t.stem}: carries another truck\'s marker')
            world = bind_world(md)
            check(at >= 0 and max(abs(a - b) for a, b in zip(world[at][12:15], aura.MARKER_AT)) < 1e-4,
                  f'{t.stem}: marker frame')
            m = sgo.read(files[f'OBJECT/{t.stem}.SGO'])[1]
            check(m['animation_model'][0][:2] == [f'app:/object/{t.stem.lower()}.mrab', t.model], f'{t.stem}: animation_model')
            check(m['vehicle_weapon_setting'] == [], f'{t.stem}: armed')
    else:
        print('aura_vehicles_guard: no Root.cpk here, the build is not checked')


if __name__ == '__main__':   # the build grafts its cockpits in worker processes (spawn)
    registries()
    built()
    print(f'aura_vehicles_guard: {checks} checks, {failed} failed')
    sys.exit(1 if failed else 0)
