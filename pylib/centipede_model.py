"""The Primer centipede (src/primer.cpp: an enemy, one segment a creature, that fights alone and links head to tail
with others into one long centipede, docs/primer-plan.md): made from primitives (pylib/procmesh.py) and written
into the stock E514_DANGO.MRAB (the giant pill bug: its chitin) in place of its e514_dango.mdb, in that bug's one
material, each part the patch of its texture nearest its tint.

    python pylib/centipede_model.py [OUT.MRAB]        build it (default build/EDF6VC_CENTIPEDE.MRAB)

Axes: +x right, +y up, +z the nose. One creature is one segment, 3 m (src/primer.cpp kLinkSpacing: a chain's
segments are that far apart), with a pair of long jointed legs; its head (fangs, eyes, antennae) and its tail (the
last plate, the long hooked hind legs, the stinger) are bones of their own, which the plugin shrinks to nothing at
their joints while it is linked, so a chain shows one head at its front and one tail at its end. Skeleton (all bound
level, src/primer_pose.h moves them): mdl -> globalSRT -> body (the bone the V506 body drives) -> leg_l, leg_r (at
the hips: swung about y, folded about z when dead), gun (the barbs on its back: an aimed mount, the barbs' muzzles
on it), head, tail -> sting (the stinger: an aimed mount). The skinned object's own bone is `centipede`.
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from procmesh import Part, build_archive, ellipsoid, tube  # noqa: E402

TEMPLATE = ('E514_DANGO.MRAB', 'e514_dango.mdb')
CHITIN = 0   # the template's one material (dango_mat): every part is a patch of its texture (Part.tint)
# The tints (the texture's colours they take): dark chitin plates, the lighter brown of legs, fangs and spines,
# the soft between the plates, the eyes the darkest.
PLATE, LIMB, SOFT, EYE = (30, 28, 24), (95, 85, 65), (60, 52, 40), (8, 8, 8)
SIZE = 1.0
HALF = 1.45   # m: half a segment's plate along it (the chain's 3 m spacing leaves a little soft between plates)

# bone -> (parent, position): depth-first preorder (every stock model's order, which the engine's depth_delta walk
# relies on: docs/mdb-format.md §4.2)
JOINTS = [('mdl', -1, (0.0, 0.0, 0.0)), ('centipede', 0, (0.0, 0.0, 0.0)), ('globalSRT', 0, (0.0, 0.0, 0.0)),
          ('body', 2, (0.0, 0.0, 0.0)), ('leg_l', 3, (-1.3, -0.1, 0.0)), ('leg_r', 3, (1.3, -0.1, 0.0)),
          ('gun', 3, (0.0, 0.95, 0.0)), ('head', 3, (0.0, 0.1, HALF)), ('tail', 3, (0.0, 0.1, -HALF)),
          ('sting', 8, (0.0, 0.55, -2.3))]
BONE = {n: i for i, (n, _, _) in enumerate(JOINTS)}


def _check_preorder() -> None:
    for i, (_, parent, _) in enumerate(JOINTS):
        assert parent < i, 'a parent before its children'


_check_preorder()


def leg(part: Part, side: float) -> None:
    """A long jointed leg from the hip out, up to the knee and down to the foot, a little swept back."""
    sk = [(BONE['leg_r' if side > 0 else 'leg_l'], 1.0)]
    hip, knee = (side * 1.25, -0.1, 0.0), (side * 2.7, 1.0, -0.25)
    ankle, foot = (side * 3.7, -0.4, -0.5), (side * 4.1, -1.5, -0.7)
    tube(part, [hip, knee], [0.3, 0.22], [sk] * 2, segs=7)
    tube(part, [knee, ankle], [0.2, 0.13], [sk] * 2, segs=7)
    tube(part, [ankle, foot], [0.12, 0.04], [sk] * 2, segs=6)


def parts() -> list[Part]:
    plate, limb, soft, eye = Part(CHITIN, PLATE), Part(CHITIN, LIMB), Part(CHITIN, SOFT), Part(CHITIN, EYE)
    body = [(BONE['body'], 1.0)]
    ellipsoid(plate, (0.0, 0.25, 0.0), (1.5, 0.7, HALF), body, rings=10, segs=20)          # the dorsal plate
    ellipsoid(plate, (0.0, 0.7, -0.1), (0.85, 0.25, 1.15), body, rings=6, segs=12)        # its ridge
    ellipsoid(soft, (0.0, -0.15, 0.0), (1.25, 0.55, 1.3), body, rings=8, segs=16)         # the belly
    tube(soft, [(0.0, 0.05, -HALF - 0.05), (0.0, 0.05, -HALF + 0.25)], [0.95, 1.05], [body] * 2, segs=14)   # between plates
    for side in (-1.0, 1.0):
        leg(limb, side)
    gun = [(BONE['gun'], 1.0)]
    for x, lean in ((-0.35, -0.15), (0.0, 0.0), (0.35, 0.15)):   # the barbs: spines along the mount's +z
        tube(limb, [(x, 0.0, -0.3), (x + lean, 0.12, 0.45), (x + lean * 1.4, 0.1, 1.1)], [0.14, 0.09, 0.02], [gun] * 3, segs=6)
    head = [(BONE['head'], 1.0)]
    ellipsoid(plate, (0.0, 0.15, HALF + 0.75), (1.15, 0.55, 0.85), head, rings=8, segs=16)   # the head capsule
    for side in (-1.0, 1.0):
        ellipsoid(eye, (side * 0.62, 0.4, HALF + 1.3), (0.2, 0.16, 0.18), head, rings=6, segs=8)
        # the fangs (forcipules): out, forward and curving in
        tube(limb, [(side * 0.6, -0.25, HALF + 1.1), (side * 1.15, -0.35, HALF + 1.8), (side * 0.4, -0.4, HALF + 2.4)],
             [0.2, 0.13, 0.03], [head] * 3, segs=7)
        # the antennae: up, forward and out, in beads
        pts = [(side * (0.35 + 0.42 * k), 0.4 + 0.5 * k - 0.05 * k * k, HALF + 1.4 + 0.6 * k) for k in range(8)]
        tube(limb, pts, [0.1 * (1 - 0.08 * k) for k in range(8)], [head] * 8, segs=5)
    tail = [(BONE['tail'], 1.0)]
    ellipsoid(plate, (0.0, 0.15, -HALF - 0.6), (1.1, 0.5, 0.75), tail, rings=8, segs=14)      # the last plate
    for side in (-1.0, 1.0):   # the hind legs: long, hooked, back and out
        tube(limb, [(side * 0.6, 0.0, -HALF - 1.0), (side * 1.5, 0.3, -HALF - 2.2), (side * 1.8, 0.1, -HALF - 3.4)],
             [0.18, 0.12, 0.03], [tail] * 3, segs=6)
    sting = [(BONE['sting'], 1.0)]
    tube(limb, [(0.0, -0.1, -0.3), (0.0, 0.25, 0.4), (0.0, 0.2, 1.3)], [0.28, 0.15, 0.02], [sting] * 3, segs=8)   # along +z
    return [plate, soft, limb, eye]


def build(game) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The archive: the pill bug's MRAB with its model replaced by the centipede segment (same member name)."""
    return build_archive(game, TEMPLATE[0], TEMPLATE[1], JOINTS, 'centipede', parts(), SIZE)


def main(argv: list[str]) -> int:
    import rootcpk
    out = argv[0] if argv else os.path.join(HERE, '..', 'build', 'EDF6VC_CENTIPEDE.MRAB')
    arc = build(rootcpk.default())
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, 'wb') as h:
        h.write(arc)
    print(f'wrote {out}: {len(arc)} bytes, {len(JOINTS)} bones')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
