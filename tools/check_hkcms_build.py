"""Runs pylib/hkcms_build.py's checks against the player's own EDF6 (the donor: IG_HEIGEN601.MAD's collision.hkt) and,
with an EDF4.1 beside it, builds the real SK_HEIGEN02 terrain collision (501 x 501 lattice, 2500 tiles) and checks it.

  python tools/check_hkcms_build.py [game dir] [--real]

--real is the slow part (about 15 s to build, 10 s for hkcms.bound_problems over 2500 tiles). Exit 0 = every check held,
1 = one failed, 2 = no EDF6 found (nothing was checked).
"""
from __future__ import annotations

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import bigmap  # noqa: E402
import edf41_maps  # noqa: E402
import gamedir  # noqa: E402
import hkcms  # noqa: E402
import hkcms_build  # noqa: E402
import hktag  # noqa: E402
import map_legacy  # noqa: E402
import numpy as np  # noqa: E402
import sk_terrain  # noqa: E402


def donor(root: str) -> bytes:
    """IG_HEIGEN601's collision.hkt from the player's EDF6."""
    return bigmap.Marc.parse(edf41_maps.Archives(root).read('IG_HEIGEN601.MAD')).get('collision.hkt')


def real_check(root: str, root41: str, d: bytes) -> list[str]:
    """SK_HEIGEN02's lattice built and decoded back (the terrain every other SK map shares the layout of)."""
    mac = map_legacy.mac_from_legacy(edf41_maps.Archives(root41).read('SK_HEIGEN02.MAC'))
    shfb = dict(bigmap.Marc.parse(mac).files)['sk_heigen02_fld2mgrid.shfb']
    lat = sk_terrain.lattice(sk_terrain.parse(shfb))
    t0 = time.time()
    out = hkcms_build.build_collision(d, 'sk_heigen02_fld2mgrid', lat, -1000.0, -1000.0, 4.0)
    took = time.time() - t0
    tag = hktag.Tag(out)
    name = 'sk_heigen02_fld2mgrid.sk_heigen02_fld2mgrid'
    item = hkcms.bodies(tag)[name]
    comp = hkcms.Compound(tag, bytearray(out), item)
    t0 = time.time()
    problems = hkcms.bound_problems(tag, out, item)
    check = time.time() - t0
    assert not problems, problems[:5]
    tris = comp.triangles().reshape(-1, 3)
    ix = np.rint((tris[:, 0] + 1000) / 4).astype(int)
    iz = np.rint((tris[:, 2] + 1000) / 4).astype(int)
    err = max(np.abs(tris[:, 1] - lat[iz, ix]).max(), np.abs(tris[:, 0] - (-1000 + ix * 4.0)).max(),
              np.abs(tris[:, 2] - (-1000 + iz * 4.0)).max())
    assert err < 2e-4, err
    return [f'SK_HEIGEN02: {len(comp.instances)} tiles, {len(tris) // 3} triangles, {len(out) / 1e6:.1f} MB, built in '
            f'{took:.0f} s; decoded vertices within {err:.1e} m of the lattice; bound_problems == [] ({check:.0f} s)']


def main(argv: list[str]) -> int:
    root = next((a for a in argv if not a.startswith('--')), None) or gamedir.find()
    if not root:
        print('no EDF6 found')
        return 2
    d = donor(root)
    try:
        for line in hkcms_build.selftest(d):
            print(line)
        if '--real' in argv:
            root41 = gamedir.find_other(gamedir.EDF41, near=root)
            if not root41:
                print('no EDF4.1 found: the real SK_HEIGEN02 check was skipped')
            else:
                for line in real_check(root, root41, d):
                    print(line)
    except AssertionError as e:
        print('FAILED:', e)
        return 1
    print('OK')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
