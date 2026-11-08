"""The Katyusha's firing floor over its cab (test site #36: 「让喀秋莎的向前射角限制一下，他不应该穿过车头设计」) against the
model it is for, built read-only from Root.cpk (no game is started):

  - the committed table (common/edf/katyusha_cab_gen.h) was measured on this very model (its record's sha256);
  - the bug: over the cab at 0 elevation (the stock pose's lowest) the rockets' rays meet the truck;
  - every half degree round the turntable, at the floor the plugins pose the launcher at there (pylib/katyusha_model.py
    cab_floor_at, as common/edf/katyusha_cab.h CabFloor interpolates) and every 5 deg over it up to the stop, no ray of
    any rocket (its axis and its fins' reach) meets the truck;
  - the floor is no higher than it needs to be: for every entry over 0, just under its floor less the margin some
    azimuth it covers meets the truck.

python tests/katyusha_cab_check.py [GAME_DIR]     exit 77 when Root.cpk is unavailable.
"""
from __future__ import annotations

import argparse
import hashlib
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools')]
import gen_katyusha_cab  # noqa: E402
import katyusha_model as km  # noqa: E402
from mdb import mdb_write  # noqa: E402
import rootcpk  # noqa: E402


def run(game_dir: str) -> None:
    md = gen_katyusha_cab.model(rootcpk.Game(game_dir))
    data = gen_katyusha_cab.recorded()
    assert data['model'] == hashlib.sha256(mdb_write(md)).hexdigest(), \
        'common/edf/katyusha_cab_gen.h was measured on another model: python tools/gen_katyusha_cab.py'
    assert gen_katyusha_cab.check() is None, gen_katyusha_cab.check()
    table = data['floor']
    frame = km.cab_frame(md)
    tris = km.cab_reachable(md, km.truck_triangles(md))
    assert km.cab_hits(md, [0.0], 0.0, km.cab_lane(md, 0.0, tris, frame), frame) == [True], \
        'over the cab at 0 elevation the rockets do not meet the truck: the model changed, see #36'
    poses = 0
    for k in range(720):
        az = k * 0.5
        floor = km.cab_floor_at(table, az)
        elevs = [floor] + [float(e) for e in range(int(math.ceil(floor / 5.0)) * 5, int(km.PITCH_STOP_DEG) + 1, 5) if e > floor]
        hits = km.cab_hits(md, elevs, az, km.cab_lane(md, az, tris, frame), frame)
        poses += len(elevs)
        assert not any(hits), f'azimuth {az}: the rockets meet the truck at {[e for e, h in zip(elevs, hits) if h]} deg ' \
                              f'(floor {floor:.2f})'
    tight = 0
    for k, floor in enumerate(table):
        if floor <= 0.0:
            continue
        under = floor - km.CAB_MARGIN_DEG - km.CAB_ELEV_STEP
        a0 = k * km.CAB_AZIMUTH_STEP
        near = [(a0 + d) % 360 for d in range(-km.CAB_AZIMUTH_STEP, km.CAB_AZIMUTH_STEP + 1, km.CAB_FINE_STEP)]
        assert any(km.cab_hits(md, [under], float(a), km.cab_lane(md, float(a), tris, frame), frame)[0] for a in near), \
            f'entry {a0} deg: nothing meets the truck at {under} deg, its floor {floor} is higher than it needs'
        tight += 1
    print(f'PASS: the table is this model\'s; 0 deg over the cab meets the truck; {poses} poses over 720 azimuths clear; '
          f'{tight} entries tight (front floor {table[0]} deg)')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('game', nargs='?', default=rootcpk.DEFAULT_GAME)
    args = ap.parse_args()
    if not (Path(args.game) / 'Root.cpk').is_file():
        print('SKIP: Root.cpk is unavailable')
        return 77
    run(args.game)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
