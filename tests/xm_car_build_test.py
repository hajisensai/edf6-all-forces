"""Every ported X-Morph car (pylib/xm_units.py car: pylib/car_model.py on the V512) built as the installer builds it
(tools/make_xmorph.py), then held to the car contract: each wheel's triangles on its own bone, that bone at the wheel's
centre; the front and rear pairs the V512's physical wheels at their tyres' radii, a middle axle drawn only at its own;
the body's hulls inside the model; unarmed, the driver alone, the V512's mission_setup kept (the range places it as
it is); the driver's eye marker where the row puts it; installed and removed through the ledger only.
Read-only game input; skipped (77) without Root.cpk or without a car's model folder (the model pack).

    python tests/xm_car_build_test.py
"""
from pathlib import Path
import sys
import tempfile

sys.path[:0] = [str(Path(__file__).resolve().parents[1] / p) for p in ('tools', 'pylib')]
import numpy as np  # noqa: E402

import car_model  # noqa: E402
import graft_pure as g  # noqa: E402
import make_xmorph  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402
import sidecar_model  # noqa: E402
import xm_units  # noqa: E402
from mdb import bind_world, mdb_read, rab_read  # noqa: E402


def main() -> int:
    root = rootcpk.DEFAULT_GAME
    if not Path(root, 'Root.cpk').is_file():
        print('SKIP: Root.cpk unavailable')
        return 77
    cars = [u for u in xm_units.v512() if make_xmorph.deployable(u.key)]
    if not cars:
        print('SKIP: no car model folder (the model pack)')
        return 77
    stock = sgo.read(rootcpk.Game(root).read('OBJECT', car_model.HOST + '.SGO'))[1]
    for u in cars:
        files = make_xmorph.build(root, u.key)
        stem = car_model.out_names(u.key)[0]
        assert set(files) == {f'OBJECT/{stem}.{e}' for e in ('SGO', 'MRAB', 'SHKT')}, sorted(files)
        spec = car_model.load(u.key)
        wheels = spec.info['wheels']
        md = mdb_read(next(f.data for f in rab_read(files[f'OBJECT/{stem}.MRAB']).files
                           if f.name.lower() == car_model.HOST_MDB))
        world = bind_world(md)
        at = {md.name_of(b.name): np.asarray(world[b.index][12:15]) for b in md.bones}
        eye = at['vc_eye_0']
        assert np.allclose(eye, u.eye, atol=1e-4), (u.key, eye, u.eye)
        # each wheel's triangles on its bone, round it within its radius; the bone at the wheel's centre
        count = dict.fromkeys(wheels, 0)
        for o in md.objects:
            for mesh in o.meshes:
                pos = g.mesh_positions(mesh)
                bi, bw = g.skin_columns(mesh)
                for t in g.triangles(mesh):
                    names = {md.name_of(md.bones[int(bi[i][0])].name) for i in t}
                    assert len(names) == 1 and all(bw[i][0] == 1 for i in t), (u.key, names)
                    name = names.pop()
                    if name in wheels:
                        count[name] += 1
                        d = np.linalg.norm((np.asarray([pos[i] for i in t]) - at[name])[:, 1:], axis=1)
                        assert d.max() <= 1.05 * wheels[name]['radius'], (u.key, name, d.max())   # a polygon's corners
        assert all(count.values()), (u.key, count)
        for name, w in wheels.items():
            assert np.allclose(at[name], w['centre'], atol=1e-4), (u.key, name)
        _v, m = sgo.read(files[f'OBJECT/{stem}.SGO'])
        assert m['xgs_scene_object_class'] == 'Vehicle_Car' and m['mission_setup'] == stock['mission_setup']
        assert m['vehicle_weapon_setting'] == [] and m['vehicle_setup'][-1] == [] and len(m['vehicle_riding_position']) == 1
        assert getattr(m['game_object_durability'], 'value', m['game_object_durability']) == u.durability
        physical = {row[0]: row[8].value for row in m['car_base_wheel']}
        assert set(physical) == {f'{a}_{s}' for a in (car_model.FRONT, car_model.REAR) for s in 'lr'}, physical
        assert all(abs(r - wheels[n]['radius']) < 1e-5 for n, r in physical.items()), physical
        drawn = {row[0][1]: row[1][0].value for row in m['car_base_draw_wheel_info'] if row[0][0] == -1}
        assert drawn.keys() == wheels.keys() - physical.keys(), drawn
        assert all(abs(r - wheels[n]['radius']) < 1e-5 for n, r in drawn.items()), drawn
        # the body's hulls within the model's box (its convex radius round them), above the axles
        lo, hi = (np.asarray(v) for v in spec.info['bounds'])
        body = sidecar_model._Shkt(files[f'OBJECT/{stem}.SHKT'])
        y0 = at['body'][1]
        for k in range(body.inst_n):
            blo, bhi = body.box(k)
            blo, bhi = np.asarray(blo) + (0, y0, 0), np.asarray(bhi) + (0, y0, 0)
            assert (blo >= lo - 1e-3).all() and (bhi <= hi + 1e-3).all(), (u.key, k, blo, bhi)
            assert blo[1] >= min(w['centre'][1] for w in wheels.values()) - 1e-3, (u.key, k, blo)
        with tempfile.TemporaryDirectory(prefix='edf-xmcar-') as tmp:
            make_xmorph.install(tmp, files, u.key)
            for name, data in files.items():
                assert Path(tmp, 'Mods', name).read_bytes() == data
            deleted, kept = make_xmorph.remove(tmp, u.key)
            assert len(deleted) == len(files) and not kept
        print(f'{u.key}: {len(wheels) // 2} axles ({len(drawn) // 2} drawn only), {sum(count.values())} wheel '
              f'triangles on their bones, {body.inst_n} body hulls, unarmed, install / remove OK')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
