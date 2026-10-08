"""Read-only Root.cpk integration check for the Katyusha's actual mesh contacts.

python tests/katyusha_mount_check.py [GAME_DIR] [--render OUTPUT_DIR]
Optional game-data test: exit 77 if Root.cpk is unavailable. No game is started.
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import graft_pure as g
import katyusha_model as km
import make_katyusha
from mdb import bind_world, mdb_read, mdb_write, rab_read
import rootcpk


def run(game: str, render: str = '') -> None:
    files = make_katyusha.build(game)
    arc = files[f'OBJECT/{make_katyusha.MODEL_FILE}']
    md = mdb_read(km.member(rab_read(arc), km.HOST_MDB).data)
    contacts = km.mount_contacts(md)
    # Independent real-donor landmark: these V607 deck triangles are a single
    # horizontal face at 1.65625 after ground alignment and half-float storage.
    # The previous model's base was 1.9169921875, 26 cm above that face.
    assert len(contacts) == 16
    assert all(abs(y - 1.65625) < 0.001 for _p, y in contacts)
    assert all(abs(p[1] - 1.65625) < 0.001 for p, _y in contacts)
    km.check_mount(md)
    rack = g.subtree(md, md.bone_index('Rocketcannon_base'))
    for delta in [(0.0, 0.2607421875, 0.0), (0.0, -0.1, 0.0), (5.0, 0.0, 0.0)]:
        bad = mdb_read(mdb_write(g.move_bones(md, rack, delta)))
        try:
            km.check_mount(bad)
        except km.KatyushaCheckError:
            pass
        else:
            raise AssertionError(f'mount validator accepted {delta}')
    # Packed model, all firing elevations and loading travel; not only the bind.
    clearance = km.launcher_clearance(md, step=1)
    assert min(gap for _angle, gap in clearance) >= km.BED_CLEARANCE
    km.check_ram(md)
    print(f'PASS: {len(contacts)} real mesh contacts; 3 negative controls rejected; '
          f'81 elevations clear the deck by at least {min(gap for _, gap in clearance):.3f} m')
    if render:
        import model_view as mv
        import numpy as np
        from PIL import Image
        out = Path(render)
        out.mkdir(parents=True, exist_ok=True)
        model_file = out / make_katyusha.MODEL_FILE
        model_file.write_bytes(arc)
        md = mv.load(str(model_file), km.HOST_MDB)
        w = bind_world(md)
        P = tuple(w[md.bone_index('Rocketcannon_prop')][12:15])
        E = tuple(w[md.bone_index(km.RAM_ROD)][12:15])
        M = tuple(w[md.bone_index('Rocketcannon_main')][12:15])
        frames = []
        for deg in (0, 40, 80):
            turn, eye, _length = km.ram_pose(P, E, M, math.radians(deg))
            local = {}
            for name, angle in [('Rocketcannon_main', -deg),
                                ('Rocketcannon_prop', math.degrees(turn)),
                                (km.RAM_ROD, math.degrees(turn))]:
                original = md.bones[md.bone_index(name)].local
                # Runtime RamTurn multiplies rows in parent space; the pivot
                # translation is restored after multiplication.
                transformed = mv.mmul(original, mv.rot('x', angle))
                transformed[12:16] = original[12:16]
                if name == km.RAM_ROD:
                    transformed[12:15] = [original[12 + c] + eye[c] - E[c] for c in range(3)]
                local[name] = transformed
            v, t, c = mv.geometry(md, {}, ['@tex'], local)
            rows = [mv.render(v - np.array([0, 2.7, 0]), t, c, view, 1000, 5.5, f'{deg} degrees')
                    for view in ('side', 'iso')]
            row = Image.new('RGB', (2000, 1000))
            for k, im in enumerate(rows):
                row.paste(im, (k * 1000, 0))
            frames.append(row)
        sheet = Image.new('RGB', (2000, 3000))
        for k, im in enumerate(frames):
            sheet.paste(im, (0, k * 1000))
        sheet.save(out / 'mount-poses.png')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('game', nargs='?', default=rootcpk.DEFAULT_GAME)
    ap.add_argument('--render', default='')
    args = ap.parse_args()
    if not (Path(args.game) / 'Root.cpk').is_file():
        print('SKIP: Root.cpk is unavailable')
        return 77
    run(args.game, args.render)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
