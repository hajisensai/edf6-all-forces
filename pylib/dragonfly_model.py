"""The Primer dragonfly (src/primer.cpp: the Primers' air superiority fighter, docs/primer-plan.md): a bio-mechanical
dragonfly made from primitives (pylib/procmesh.py) and written into the stock E507_GOLDUFO.MRAB in place of its
e507_goldufo.mdb, so it uses that drone's materials and textures (gold, copper, metal, matte black, and the blue
light-scrolling translucent one for the wings and eyes).

    python pylib/dragonfly_model.py [OUT.MRAB]        build it (default build/EDF6VC_DRAGONFLY.MRAB)

Axes as every model here: +x right, +y up, +z the nose. The skeleton (all bound level, so the plugin's hinges are
plain axes, src/primer_pose.h): mdl (root, the V506 locators' parent) -> globalSRT -> body (the bone the V506 body
drives) -> head, wing_fl / wing_fr / wing_bl / wing_br (hinged about z at their roots: flap), abd1 -> abd2 -> abd3
-> abd4 (hinged about x: the abdomen curls), and the skinned object's own bone `dragonfly` under mdl.
"""
from __future__ import annotations

import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from procmesh import Part, build_archive, ellipsoid, grid, tube  # noqa: E402

TEMPLATE = ('E507_GOLDUFO.MRAB', 'e507_goldufo.mdb')
COPPER, METAL, GLOW, BLACK, GOLD = 0, 1, 2, 3, 4   # the template's materials
BONES = [
    ('mdl', -1, (0.0, 0.0, 0.0)),
    ('dragonfly', 0, (0.0, 0.0, 0.0)),     # the skinned object's bone
    ('globalSRT', 0, (0.0, 0.0, 0.0)),
    ('body', 2, (0.0, 0.0, 0.0)),
    ('head', 3, (0.0, 0.2, 3.9)),
    ('wing_fl', 3, (-0.55, 0.85, 2.7)),
    ('wing_fr', 3, (0.55, 0.85, 2.7)),
    ('wing_bl', 3, (-0.55, 0.8, 1.55)),
    ('wing_br', 3, (0.55, 0.8, 1.55)),
    ('abd1', 3, (0.0, 0.1, 0.6)),
    ('abd2', 9, (0.0, 0.1, -1.4)),
    ('abd3', 10, (0.0, 0.1, -3.4)),
    ('abd4', 11, (0.0, 0.1, -5.2)),
]
BONE = {n: i for i, (n, _, _) in enumerate(BONES)}
SIZE = 1.1   # the whole model, times: 14 m nose to tail tip, 13.5 m across the wings


def wing(membrane: Part, spar: Part, root, side: float, length: float, chord: float, sweep: float, bone: str) -> None:
    """A wing from `root` out along +x (side +1) or -x (-1): a thin membrane (both faces) round an ellipse-ish
    outline, swept back `sweep` m at the tip, with a spar along its leading edge."""
    steps, half = 14, 0.04
    top, bot = [], []
    sk = [(BONE[bone], 1.0)]
    for k in range(steps + 1):
        u = k / steps
        x = root[0] + side * length * u
        w = chord * math.sqrt(max(0.0, 1.0 - (2 * u - 1) ** 2 * 0.85)) * (0.55 + 0.45 * (1 - u))
        lead = root[2] - sweep * u * u
        for face, rows in ((half, top), (-half, bot)):
            rows.append([membrane.add((x, root[1] + face, z), sk, (u, j / 2)) for j, z in enumerate((lead, lead - w / 2, lead - w))])
    grid(membrane, top, False)
    grid(membrane, [r[::-1] for r in bot], False)   # the underside faces down
    path = [(root[0] + side * length * u, root[1] + 0.05, root[2] - sweep * u * u + 0.05) for u in np.linspace(0, 0.98, 10)]
    tube(spar, path, [0.12 * (1 - 0.7 * k / 9) for k in range(10)], [sk] * 10, segs=6, cap=False)


def abdomen_skin(z: float):
    """A ring at z along the abdomen: blended between the segment bones round each joint (smooth bending)."""
    joints = [(BONE['abd1'], 0.6), (BONE['abd2'], -1.4), (BONE['abd3'], -3.4), (BONE['abd4'], -5.2)]
    blend = 0.45
    for i in range(len(joints) - 1):
        b0, _ = joints[i]
        b1, z1 = joints[i + 1]
        if z > z1 + blend:
            return [(b0, 1.0)]
        if z > z1 - blend:
            t = (z1 + blend - z) / (2 * blend)
            return [(b0, 1.0 - t), (b1, t)]
    return [(joints[-1][0], 1.0)]


def parts() -> list[Part]:
    gold, copper, glow, black, metal = Part(GOLD), Part(COPPER), Part(GLOW), Part(BLACK), Part(METAL)
    body, head = [(BONE['body'], 1.0)], [(BONE['head'], 1.0)]
    ellipsoid(gold, (0.0, 0.2, 2.15), (1.05, 1.0, 1.55), body)                       # thorax
    ellipsoid(gold, (0.0, 0.15, 4.05), (0.75, 0.7, 0.65), head, rings=10, segs=14)   # head
    for side in (-1, 1):                                                              # compound eyes (glowing)
        ellipsoid(glow, (side * 0.62, 0.3, 4.3), (0.62, 0.66, 0.6), head, rings=10, segs=14)
    tube(metal, [(0.0, -0.45, 4.2), (0.0, -0.5, 5.2)], [0.22, 0.16], [head] * 2, segs=8)   # mandible cannon
    zs = list(np.linspace(0.8, -6.9, 22))                                             # abdomen, segmented
    radii = [0.62 * (1 - 0.55 * (k / 21)) * (1.0 if k % 3 else 1.12) for k in range(22)]
    tube(copper, [(0.0, 0.1 + 0.02 * k, z) for k, z in enumerate(zs)], radii, [abdomen_skin(z) for z in zs], segs=12, squash=0.9)
    tube(gold, [(0.0, 0.55, -6.6), (0.0, 0.2, -7.6)], [0.18, 0.02], [abdomen_skin(-6.6)] * 2, segs=6)   # tip
    for side, name in ((-1, 'wing_fl'), (1, 'wing_fr')):                              # fore wings: longer
        wing(glow, black, BONES[BONE[name]][2], side, 5.6, 1.15, 0.7, name)
    for side, name in ((-1, 'wing_bl'), (1, 'wing_br')):                              # hind wings: broader
        wing(glow, black, BONES[BONE[name]][2], side, 5.0, 1.45, 0.4, name)
    for side in (-1, 1):                                                              # legs, folded under
        for z in (2.9, 2.2, 1.5):
            tube(black, [(side * 0.5, -0.5, z), (side * 1.3, -0.6, z + 0.2), (side * 1.1, -1.4, z - 0.3)],
                 [0.11, 0.09, 0.05], [body] * 3, segs=6)
    return [gold, copper, glow, black, metal]


def build(game) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The archive: the stock drone's MRAB with its model replaced by the dragonfly (same member name)."""
    return build_archive(game, TEMPLATE[0], TEMPLATE[1], BONES, 'dragonfly', parts(), SIZE)


def main(argv: list[str]) -> int:
    import rootcpk
    out = argv[0] if argv else os.path.join(HERE, '..', 'build', 'EDF6VC_DRAGONFLY.MRAB')
    arc = build(rootcpk.default())
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, 'wb') as h:
        h.write(arc)
    print(f'wrote {out}: {len(arc)} bytes')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
