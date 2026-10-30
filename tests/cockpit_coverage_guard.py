"""Source guard (the user, 2026-10-21: "bmpt和火神炮缺少第一人称"): every ground vehicle the plugin generates
(pylib/vcobjects.py GROUND_VEHICLES) has first person on its seats the one way every vehicle gets it. A seat has a first
person only where its model carries the `vc_eye_<seat>` marker (src/fp_vehicle.cpp VehicleEye: else "no cockpit in this
seat"), and a generated model carries it only when

  - pylib/cockpit_fits*.py FITS fits a cockpit to the stem of the model its generator makes (OBJECT/<STEM>.MRAB),
  - the generator's build() ends in cockpits.graft_files (itself, or the shared builder it delegates to:
    tools/make_tracked_combat.py for the M163 and the BMPT), and
  - pylib/buildcache.py COCKPIT_GROUPS lists its group, so that a fit or kit change rebuilds an installed model.

The M163 and the BMPT had none of the three: their models never had an eye, every other generated vehicle did.
Read only; no game needed.

    python tests/cockpit_coverage_guard.py
"""
from __future__ import annotations

import ast
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path[:0] = [os.path.join(ROOT, 'pylib'), os.path.join(ROOT, 'tools')]
import buildcache  # noqa: E402
import cockpit_fits  # noqa: E402
import vcobjects as vc  # noqa: E402
import xm_walker  # noqa: E402

checks = failed = 0


def check(ok: bool, what: str) -> None:
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print('FAIL', what)


def tree(tool: str) -> ast.Module:
    with open(os.path.join(ROOT, 'tools', tool + '.py'), encoding='utf-8') as f:
        return ast.parse(f.read())


def build_grafts(tool: str, seen: tuple[str, ...] = ()) -> bool:
    """`tool` grafts its model's cockpits (a call of cockpits.graft_files, or of cockpits.graft on its model as
    make_sedan does), or its build() returns the build() of a tools/ module it delegates to that does."""
    module = tree(tool)
    for node in ast.walk(module):
        if (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and isinstance(node.func.value, ast.Name)
                and node.func.value.id == 'cockpits' and node.func.attr in ('graft_files', 'graft')):
            return True
    build = next((n for n in module.body if isinstance(n, ast.FunctionDef) and n.name == 'build'), None)
    imports = {a.asname or a.name: a.name for n in module.body if isinstance(n, ast.Import) for a in n.names}
    for node in ast.walk(build) if build else ():
        if not isinstance(node, ast.Return) or not isinstance(node.value, ast.Call):
            continue
        f = node.value.func
        if isinstance(f, ast.Attribute) and isinstance(f.value, ast.Name) and f.attr == 'build':
            target = imports.get(f.value.id)
            if target and target.startswith('make_') and target not in seen:
                return build_grafts(target, seen + (tool,))
    return False


def model_stem(kind: str, vehicle: vc.GroundVehicle) -> str:
    """The stem of the model `kind`'s generator makes: the tracked builder's model module names it (OUT_ARC); the
    others (an X-Morph car's and walker's tools/make_xmorph.py among them) make OBJECT/<their SGO's stem>.MRAB."""
    import make_tracked_combat
    if kind in make_tracked_combat.KINDS:
        return make_tracked_combat.model_module(kind).OUT_ARC.rsplit('.', 1)[0].upper()
    return vehicle.sgo.upper()


for kind, vehicle in vc.GROUND_VEHICLES.items():
    stem = model_stem(kind, vehicle)
    fits = cockpit_fits.FITS.get(stem, ())
    check(bool(fits), f'{kind}: no cockpit fitted to {stem} (pylib/cockpit_fits_plugin.py FITS): its seats have no first person')
    check(0 in {f.seat for f in fits}, f'{kind}: the driver\'s seat 0 has no eye')
    check(build_grafts(vehicle.tool), f'{kind}: tools/{vehicle.tool}.py grafts no cockpit (cockpits.graft_files)')
    # a V603 kind and an X-Morph unit are their own groups
    group = kind if kind in vc.TRACKED or kind in buildcache.XMORPH else vehicle.tool.removeprefix('make_')
    check(group in buildcache.GROUPS and group in buildcache.COCKPIT_GROUPS,
          f'{kind}: buildcache COCKPIT_GROUPS lacks {group!r} (a fit change would not rebuild its installed model)')

print(f'cockpit_coverage_guard: {checks} checks, {failed} failed')
sys.exit(1 if failed else 0)
