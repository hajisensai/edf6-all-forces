"""The Primer centipede (src/primer.cpp: an enemy whose individuals fight alone and link head to tail into one long
creature, docs/primer-plan.md): made from primitives (pylib/procmesh.py) and written into the stock E508_CARRIER.MRAB
(the Primers' teleportation ship: their teal armour) in place of its e508_carrier.mdb, with the ship's materials.

    python pylib/centipede_model.py [OUT.MRAB]        build it (default build/EDF6VC_CENTIPEDE.MRAB)

Axes: +x right, +y up, +z the nose. One individual is 12 m from its fangs to its tail hooks. Skeleton (all bound
level, src/primer_pose.h moves them): mdl -> globalSRT -> body (the middle segment; the bone the V506 body drives)
-> front chain segF1 -> segF2 -> head, rear chain segB1 -> segB2 -> tail (each hinged about y: the body's S-wave;
linked into a longer one the plugin shrinks a follower's head and a leader's tail to nothing at their joints, so
the chain shows one head and one tail), and one bone per leg, leg_<segment>_l / _r, under its segment (swung about
y: the gait's wave). The skinned object's own bone is `centipede`.
"""
from __future__ import annotations

import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from procmesh import Part, build_archive, ellipsoid, tube  # noqa: E402

TEMPLATE = ('E508_CARRIER.MRAB', 'e508_carrier.mdb')
METAL, GLOW, GOLD, COPPER, WEAK = 0, 1, 2, 3, 4   # the template's materials
SIZE = 1.0
# Segments: (bone, centre z). The body bone sits in the middle one; each other segment's bone is at its joint
# toward the middle, so a turn of it bends the creature there.
SEGMENTS = [('segF2', 3.6), ('segF1', 1.8), ('body', 0.0), ('segB1', -1.8), ('segB2', -3.6)]
BONES = [
    ('mdl', -1, (0.0, 0.0, 0.0)),
    ('centipede', 0, (0.0, 0.0, 0.0)),
    ('globalSRT', 0, (0.0, 0.0, 0.0)),
    ('body', 2, (0.0, 0.0, 0.0)),
    ('segF1', 3, (0.0, 0.0, 0.9)),
    ('segF2', 4, (0.0, 0.0, 2.7)),
    ('head', 5, (0.0, 0.0, 4.4)),
    ('segB1', 3, (0.0, 0.0, -0.9)),
    ('segB2', 7, (0.0, 0.0, -2.7)),
    ('tail', 8, (0.0, 0.0, -4.4)),
]
for _seg, _z in SEGMENTS:
    _parent = [n for n, _, _ in BONES].index(_seg)
    for _side, _x in (('l', -1.0), ('r', 1.0)):
        BONES.append((f'leg_{_seg}_{_side}', _parent, (_x * 1.0, -0.15, _z)))
BONE = {n: i for i, (n, _, _) in enumerate(BONES)}
LEG_BONES = [n for n, _, _ in BONES if n.startswith('leg_')]


def segment(armour: Part, seam: Part, z: float, bone: str, width: float = 1.15) -> None:
    sk = [(BONE[bone], 1.0)]
    ellipsoid(armour, (0.0, 0.18, z), (width, 0.55, 0.95), sk, rings=8, segs=16)       # the dorsal plate
    ellipsoid(armour, (0.0, -0.12, z), (width * 0.85, 0.45, 0.85), sk, rings=8, segs=14)  # the belly
    tube(seam, [(0.0, 0.0, z - 0.95), (0.0, 0.0, z - 0.75)], [0.72, 0.76], [sk] * 2, segs=12)   # the glowing seam


def leg(part: Part, seg_z: float, side: float, name: str, reach: float = 1.0) -> None:
    """A jointed leg from the hip out, up to the knee and down to the foot, a little swept back."""
    sk = [(BONE[name], 1.0)]
    hip = (side * 0.95, -0.15, seg_z)
    knee = (side * (1.0 + 1.3 * reach), 0.55, seg_z - 0.2)
    foot = (side * (1.0 + 2.3 * reach), -1.35, seg_z - 0.55)
    tube(part, [hip, knee], [0.16, 0.12], [sk] * 2, segs=6)
    tube(part, [knee, foot], [0.11, 0.04], [sk] * 2, segs=6)


def parts() -> list[Part]:
    # Primer colours (their ship's): black armour plates, gold legs and fangs, teal light in the seams between
    # segments (the ship's glowing panels), red eyes (its weak point's glow).
    armour, legs, metal = Part(GOLD, (34, 34, 38)), Part(METAL, (200, 165, 80)), Part(COPPER, (200, 165, 80))
    under, glow = Part(GLOW), Part(WEAK)
    for bone, z in SEGMENTS:
        segment(armour, under, z, bone)
        for side, s in ((-1.0, 'l'), (1.0, 'r')):
            leg(legs, z, side, f'leg_{bone}_{s}')
    head = [(BONE['head'], 1.0)]
    ellipsoid(armour, (0.0, 0.1, 5.15), (1.0, 0.5, 0.85), head, rings=8, segs=16)          # head plate
    for side in (-1, 1):
        ellipsoid(glow, (side * 0.55, 0.35, 5.55), (0.22, 0.16, 0.2), head, rings=6, segs=8)   # eyes
        # fangs: forward and curving in
        tube(metal, [(side * 0.5, -0.2, 5.6), (side * 0.85, -0.3, 6.3), (side * 0.35, -0.35, 6.9)],
             [0.17, 0.11, 0.03], [head] * 3, segs=6)
        # antennae: up, forward and out, in beads
        pts = [(side * (0.35 + 0.35 * k), 0.35 + 0.45 * k - 0.04 * k * k, 5.7 + 0.55 * k) for k in range(8)]
        tube(metal, pts, [0.09 * (1 - 0.08 * k) for k in range(8)], [head] * 8, segs=5)
    tail = [(BONE['tail'], 1.0)]
    ellipsoid(armour, (0.0, 0.12, -5.15), (0.85, 0.45, 0.75), tail, rings=8, segs=14)     # last plate
    for side in (-1, 1):   # the tail hooks (ultimate legs): long, back and out
        tube(metal, [(side * 0.4, 0.0, -5.5), (side * 0.9, 0.25, -6.6), (side * 1.1, 0.1, -7.6)],
             [0.14, 0.1, 0.03], [tail] * 3, segs=6)
    return [armour, under, legs, glow, metal]


def build(game) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The archive: the teleportation ship's MRAB with its model replaced by the centipede (same member name)."""
    return build_archive(game, TEMPLATE[0], TEMPLATE[1], BONES, 'centipede', parts(), SIZE)


def main(argv: list[str]) -> int:
    import rootcpk
    out = argv[0] if argv else os.path.join(HERE, '..', 'build', 'EDF6VC_CENTIPEDE.MRAB')
    arc = build(rootcpk.default())
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, 'wb') as h:
        h.write(arc)
    print(f'wrote {out}: {len(arc)} bytes, {len(BONES)} bones')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
