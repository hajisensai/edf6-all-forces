"""The Sazabi's standing stance, worked out from its built model (tools/sazabi_pose_view.py draws the result).

The source stands as it hovers (the Sketchfab pose: legs spread wide, shins raked back, feet pointing down at their
toes, the two legs not alike): its soles meet the floor at a toe tip only, and it looks as if it flew on the ground
(the user, 2026-10-07: 「这个脚没站起来，站立的时候还是飞的状态」). Per leg this finds the turns that stand it, in
src/sazabi_pose.h's terms (row vectors; a child's model rotation = its local x its parent's):
  thigh  Rx(tx) Rz(tz)   and the knee Rx(knee)
  foot   Rx(fx) Rz(fz)   the most floor the sole covers (its points within CONTACT of its lowest: spread along + across/2)
the ankle under the hip (STANCE_OUT m out, level with it front to back), the left knee bent LEFT_KNEE, the right leg
(shorter in this pose) bent until its sole is as high as the left's; and the drop (the pelvis comes down by it) that
puts both soles on the floor. src/sazabi_pose.h kStance holds what it prints.

    python tools/sazabi_stance.py MRAB          (needs numpy; the model pylib/sazabi_model.py builds)
"""
from __future__ import annotations

import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import sazabi_pose_view as view  # noqa: E402

STANCE_OUT = 1.6     # m: each ankle this far out from its hip
LEFT_KNEE = 5.0      # degrees: the left knee's bend standing
CONTACT = 0.3        # m: a sole point this near the foot's lowest touches the floor
# the directions the leg's underside is sampled along (kSole): SUPPORT_COUNT spread evenly (a Fibonacci sphere) over all
# that point down or no more than ~17 deg up (a foot the stride tilts far still has its lowest point among them)
SUPPORT_COUNT = 64
SUPPORT_DIRS = [(math.cos(2.399963 * i) * math.sqrt(1 - y * y), y, math.sin(2.399963 * i) * math.sqrt(1 - y * y))
                for i, y in ((i, -1.0 + 1.3 * (i + 0.5) / SUPPORT_COUNT) for i in range(SUPPORT_COUNT))]


def rot_x(a: float) -> np.ndarray:
    c, s = math.cos(a), math.sin(a)
    return np.array([[1.0, 0.0, 0.0], [0.0, c, s], [0.0, -s, c]])


def rot_z(a: float) -> np.ndarray:
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, s, 0.0], [-s, c, 0.0], [0.0, 0.0, 1.0]])


class Leg:
    def __init__(self, tris: np.ndarray, bones: np.ndarray, names: list[str], joint: dict, side: str) -> None:
        self.side, self.sign = side, 1.0 if side == 'l' else -1.0
        self.hip, self.knee, self.ankle = (joint[f'sz_{b}_{side}'] for b in ('thigh', 'shin', 'foot'))
        self.foot = tris[bones == names.index(f'sz_foot_{side}')].reshape(-1, 3) - self.ankle

    def chain(self, tx: float, tz: float, knee: float) -> tuple[np.ndarray, np.ndarray]:
        """(the ankle, the shin's model rotation) for the thigh's and knee's turns (radians)."""
        r_thigh = rot_x(tx) @ rot_z(tz)
        r_shin = rot_x(knee) @ r_thigh
        k = self.hip + (self.knee - self.hip) @ r_thigh
        return k + (self.ankle - self.knee) @ r_shin, r_shin

    def place(self, knee: float) -> tuple[float, float]:
        """The thigh's turns putting the ankle under the hip with the knee at `knee`."""
        best = None
        for tz in np.radians(np.arange(-45.0, 45.01, 0.25)):
            for tx in np.radians(np.arange(-30.0, 30.01, 0.25)):
                a, _ = self.chain(tx, tz, knee)
                cost = ((a[0] - self.hip[0]) * self.sign - STANCE_OUT) ** 2 + (a[2] - self.hip[2]) ** 2
                if best is None or cost < best[0]:
                    best = (cost, tx, tz)
        return best[1], best[2]

    def flatten(self, r_shin: np.ndarray) -> tuple[float, float, float]:
        """(fx, fz, contact): the foot's turns covering the most floor."""
        best = None
        for fz in np.radians(np.arange(-40.0, 40.01, 1.0)):
            for fx in np.radians(np.arange(-80.0, 80.01, 1.0)):
                p = self.foot @ (rot_x(fx) @ rot_z(fz) @ r_shin)
                touch = p[p[:, 1] < p[:, 1].min() + CONTACT]
                score = np.ptp(touch[:, 2]) + 0.5 * np.ptp(touch[:, 0]) if len(touch) > 2 else 0.0
                if best is None or score > best[0]:
                    best = (score, fx, fz)
        return best[1], best[2], best[0]

    def stand(self, knee: float) -> dict:
        tx, tz = self.place(knee)
        a, r_shin = self.chain(tx, tz, knee)
        fx, fz, contact = self.flatten(r_shin)
        sole = self.foot @ (rot_x(fx) @ rot_z(fz) @ r_shin) + a
        return {'tx': tx, 'tz': tz, 'knee': knee, 'fx': fx, 'fz': fz, 'contact': contact, 'sole': float(sole[:, 1].min()),
                'ankle': a}


def main(argv: list[str]) -> int:
    tris, bones, cols, names, joint = view.load_model(argv[0])
    root = joint['sz_root']
    tris = tris - root
    joint = {n: v - root for n, v in joint.items()}
    left = Leg(tris, bones, names, joint, 'l').stand(math.radians(LEFT_KNEE))
    right_leg = Leg(tris, bones, names, joint, 'r')
    best = None
    for knee in np.radians(np.arange(0.0, 60.01, 1.0)):   # the right knee bent until its sole meets the left's height
        st = right_leg.stand(knee) if best is None or abs(best['sole'] - left['sole']) > 0.05 else best
        if best is None or abs(st['sole'] - left['sole']) < abs(best['sole'] - left['sole']):
            best = st
        if abs(best['sole'] - left['sole']) <= 0.05:
            break
    for side, st in (('l', left), ('r', best)):
        print(side, ' '.join(f'{k} {math.degrees(v):.2f}' for k, v in st.items() if k in ('tx', 'tz', 'knee', 'fx', 'fz')),
              f'contact {st["contact"]:.2f} sole {st["sole"]:.2f} ankle {np.round(st["ankle"], 2)}')
    print(f'drop {(left["sole"] + best["sole"]) / 2:.3f}')
    # the left leg's underside: its shin's and foot's points furthest along each of SUPPORT_DIRS (all but level), as the
    # stance holds them, in each bone's own frame from its joint (the bind's axes): src/sazabi_pose.h kSole, the lowest of
    # which the feet are planted on whatever the stride does; the right leg's are these mirrored
    r_thigh = rot_x(left['tx']) @ rot_z(left['tz'])
    r_shin = rot_x(left['knee']) @ r_thigh
    r_foot = rot_x(left['fx']) @ rot_z(left['fz']) @ r_shin
    rows = []
    for bone, r in (('shin', r_shin), ('foot', r_foot)):
        sel = tris[bones == names.index(f'sz_{bone}_l')].reshape(-1, 3)
        local = sel - joint[f'sz_{bone}_l']
        posed = local @ r
        for d in SUPPORT_DIRS:
            k = int(np.argmax(posed @ (np.array(d) / np.linalg.norm(d))))
            rows.append((bone, tuple(float(x) for x in local[k])))
    rows = list(dict.fromkeys(rows))
    print(f'sole {len(rows)}', ' '.join(f'{{k{b.capitalize()}L,{{{x:.3f}f,{y:.3f}f,{z:.3f}f}}}},' for b, (x, y, z) in rows))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
