"""tools/cabin_view.py: near-plane clipping, the z-buffer, culling from inside a closed hull, and the measured winding of
the stock models (when Root.cpk is at hand). Plain asserts: python tests/cabin_view_test.py"""
from pathlib import Path
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cabin_view as cv  # noqa: E402

NEAR = 0.02

# clip_near: none / one / two corners behind the plane
tri = np.array([[[0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [0.0, 1.0, 1.0]],     # all in front
                [[0.0, 0.0, 1.0], [1.0, 0.0, -1.0], [0.0, 1.0, -1.0]],   # one in front -> one triangle
                [[0.0, 0.0, -1.0], [1.0, 0.0, 1.0], [0.0, 1.0, 1.0]],    # one behind -> two triangles
                [[0.0, 0.0, -1.0], [1.0, 0.0, -1.0], [0.0, 1.0, -2.0]]])  # all behind -> gone
v, b, s = cv.clip_near(tri, NEAR)
assert sorted(s.tolist()) == [0, 1, 2, 2], s
assert (v[:, :, 2] >= NEAR - 1e-12).all(), "every clipped corner lies on or in front of the near plane"
assert np.allclose(b.sum(2), 1.0), "barycentric weights of clipped corners sum to one"
for k in range(len(v)):   # each corner is its weights applied to its source triangle's corners
    assert np.allclose(np.einsum('ij,jk->ik', b[k], tri[s[k]]), v[k])
    n_src = np.cross(tri[s[k], 1] - tri[s[k], 0], tri[s[k], 2] - tri[s[k], 0])
    n_new = np.cross(v[k, 1] - v[k, 0], v[k, 2] - v[k, 0])
    assert n_src @ n_new > 0, "clipping keeps the winding"

# rasterise: the nearer of two overlapping triangles wins every pixel they share
W, H = 64, 48
q = np.array([[[0.0, 0.0], [200.0, 0.0], [0.0, 200.0]], [[0.0, 0.0], [200.0, 0.0], [0.0, 200.0]]])
iz = np.array([[1 / 5.0] * 3, [1 / 2.0] * 3])
z, k, l = cv.rasterise(q, iz, W, H)
assert (k == 1).all() and np.allclose(z, 0.5), "the nearer triangle (z = 2) covers the whole screen"
assert np.allclose(l.sum(1), 1.0, atol=1e-5)


def scene_of(pos: np.ndarray, tris: np.ndarray) -> cv.Scene:
    sc = cv.Scene()
    sc.add(pos, tris, np.full((len(pos), 3), 200.0), np.full((len(pos), 2), np.nan),
           np.full(len(tris), cv.NO_TEX), [])
    return sc


def drawn(sc: cv.Scene, eye, look, cull: bool) -> float:
    _, st = cv.render(sc, np.array(eye, float), np.array(look, float), np.array([0.0, 1.0, 0.0]), 70.0, W, H, NEAR,
                        cull)
    return st['covered']


# a floor reaching from behind the eye to far ahead is clipped, not dropped: it fills the lower half
floor = scene_of(np.array([[-50.0, 0.0, -50.0], [50.0, 0.0, -50.0], [50.0, 0.0, 50.0], [-50.0, 0.0, 50.0]]),
                 np.array([[0, 2, 1], [0, 3, 2]]))
f = drawn(floor, (0, 1, 0), (0, 1, 10), False)
assert 0.45 < f < 0.55, f"floor straddling the eye covers half the frame, got {f:.2f}"

# a closed cube wound outward (positive signed volume): from inside, culled it vanishes, unculled it fills the frame;
# from outside, culled, its near faces still show (so WINDING agrees with the stock models' outward winding)
c = np.array([[x, y, z] for x in (-1.0, 1.0) for y in (-1.0, 1.0) for z in (-1.0, 1.0)])
faces = []
for axis in range(3):
    for side in (-1.0, 1.0):
        quad = [i for i in range(8) if c[i, axis] == side]
        a, bb, cc, d = sorted(quad, key=lambda i: np.arctan2(*np.delete(c[i], axis)[::-1]))
        for t in ([a, bb, cc], [a, cc, d]):   # turned so the normal points out (along side x axis)
            n = np.cross(c[t[1]] - c[t[0]], c[t[2]] - c[t[0]])
            faces.append(t if n[axis] * side > 0 else t[::-1])
cube = scene_of(c, np.array(faces))
p = c[np.array(faces)]
assert np.einsum('ij,ij->i', p[:, 0], np.cross(p[:, 1], p[:, 2])).sum() > 0, "test cube is wound outward"
assert drawn(cube, (0, 0, 0), (0, 0, 1), True) == 0.0, "culled: a hull is invisible from inside"
assert drawn(cube, (0, 0, 0), (0, 0, 1), False) == 1.0, "unculled: the inside of the hull fills the frame"
assert drawn(cube, (0, 0, -4), (0, 0, 0), True) > 0.1, "culled: the hull is visible from outside"

# the stock models really are wound outward (WINDING), when the game's Root.cpk is here
try:
    import rootcpk  # noqa: F401  (pylib is on the path through cabin_view)
    md = cv.load("V505_TANK.MRAB", "v505_tank.mdb")
except Exception as e:   # no game install: the synthetic checks above still ran
    print(f"cabin_view_test: synthetic checks passed; stock winding skipped ({type(e).__name__})")
    raise SystemExit(0)
v, t, _ = cv.mv.geometry(md, {}, [])
p = v[t]
vol = np.einsum('ij,ij->i', p[:, 0], np.cross(p[:, 1], p[:, 2])).sum() / 6 * cv.WINDING
assert vol > 1.0, f"V505 tank signed volume x WINDING should be positive, got {vol:.2f}"
print("cabin_view_test: all checks passed")
