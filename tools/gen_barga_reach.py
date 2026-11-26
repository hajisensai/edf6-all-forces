"""Writes src/barga_reach.inc, the Balam's (501_FortressRobo) attack tables the impact marker draws from
(src/barga_aim.h, docs/feature-barga-aim.md): per variant of its attack_list (V515_RETROBALAM, V605_BARGA_CANNON: every
stock 501 vehicle carries one of these two lists), each attack's input mask, flag and the attacks it follows (indexes,
as the game resolves them: 0x60F570 compares the current attack +0x23AC with them), the limb that strikes and the
hit sphere's farthest reach: its centre in the model's frame (+Z forward, +X the left hand's side, the ground at y 0)
at the frame of its clip where it is farthest forward, and its radius. Measured offline on the model's skeleton
(.MRAB), its animations (.CAS, pylib/cas_pose.py) and its hit spheres (the SGO's animation_model kind-3 list: bone,
offset, radius, as 0x6C8A50 reads them):
  - an upper-body attack is an additive clip (`*_add`) over frame 0 of `stand`; its rotation is composed base x key
    (ORDER below) and its translation added (M: the game's blend is not decoded; the marker's Debug=1 line logs the
    live hand against it). Base x key is the order under which both variants' jabs strike with their own side's fist
    (key x base swings V605's jab_r with the left hand);
  - the limb is the one whose sphere gains the most ground forward over the clip (its farthest less where it starts);
  - a whole-body attack (heavyChop, maxBeat, maxHook, the stomps) is an absolute clip;
  - an attack with no clip of either (ground_charge, lift_up, attack_cannon) has no reach (limb -1).

  python tools/gen_barga_reach.py            write it (needs Root.cpk)
  python tools/gen_barga_reach.py --check    exit 1 when the committed file is not what this would write (CI). Without
                                             Root.cpk (CI): the file's own record (its `data` line) is checked against
                                             today's inputs and re-rendered; the game's files are only re-measured where
                                             the game is.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))

OUT = os.path.normpath(os.path.join(HERE, '..', 'src', 'barga_reach.inc'))
VERSION = 1   # this generator's: bump when what it measures or how it writes changes
DATA = '// data: '
VARIANTS = ('V515_RETROBALAM', 'V605_BARGA_CANNON')
BASE_CLIP = 'stand'    # what an additive clip is laid over (frame 0)
ORDER = 0              # additive rotation: 0 base x key (qb * qa: the key in the bone's frame), 1 key x base
LIMBS = ('left_arm', 'right_arm', 'left_leg', 'right_leg')
FOOT_MASKS = (4, 8)    # the stomps' inputs (attack_list [1][0]): struck with a foot, every other with a fist
MOST_PREV = 2


def inputs() -> dict:
    return {'version': VERSION, 'variants': list(VARIANTS), 'base': BASE_CLIP, 'order': ORDER,
            'limbs': list(LIMBS), 'foot_masks': list(FOOT_MASKS)}


def _qmul(a, b):  # noqa: ANN001, ANN202 - xyzw quaternions, Hamilton
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def _quat_of(local: list[float]) -> tuple[float, float, float, float]:
    """The quaternion cas_pose.quat_rows turns into these rows (mdb's row-vector local, its 3x3 part)."""
    r = [[local[i * 4 + j] for j in range(3)] for i in range(3)]
    tr = r[0][0] + r[1][1] + r[2][2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        return ((r[1][2] - r[2][1]) / s, (r[2][0] - r[0][2]) / s, (r[0][1] - r[1][0]) / s, 0.25 * s)
    if r[0][0] > r[1][1] and r[0][0] > r[2][2]:
        s = math.sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2
        return (0.25 * s, (r[0][1] + r[1][0]) / s, (r[2][0] + r[0][2]) / s, (r[1][2] - r[2][1]) / s)
    if r[1][1] > r[2][2]:
        s = math.sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2
        return ((r[0][1] + r[1][0]) / s, 0.25 * s, (r[1][2] + r[2][1]) / s, (r[2][0] - r[0][2]) / s)
    s = math.sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2
    return ((r[2][0] + r[0][2]) / s, (r[1][2] + r[2][1]) / s, 0.25 * s, (r[0][1] - r[1][0]) / s)


def _world(pose, md, base, kb: int, add=None, ka: int = 0) -> list[list[float]]:  # noqa: ANN001
    """Every bone's model-space matrix (mdb's 16-float rows) at key `kb` of `base`, `add` (an additive clip) at key `ka`
    laid over it."""
    import cas_pose
    tb = {t.name: t for t in base.tracks}
    ta = {t.name: t for t in add.tracks} if add else {}
    out: list[list[float]] = []
    for b in md.bones:
        name = md.name_of(b.name)
        local = list(b.local)
        t = tb.get(name)
        q = cas_pose.rotation(pose, t.rotation, kb) if t and t.rotation >= 0 else _quat_of(local)
        tr = list(pose.translation(t.translation, kb)) if t and t.translation >= 0 else local[12:15]
        a = ta.get(name)
        if a and a.rotation >= 0:
            qa = cas_pose.rotation(pose, a.rotation, ka)
            q = _qmul(qa, q) if ORDER else _qmul(q, qa)
        if a and a.translation >= 0:
            d = pose.translation(a.translation, ka)
            tr = [tr[i] + d[i] for i in range(3)]
        rows = cas_pose.quat_rows(q)
        for r in range(3):
            local[r * 4:r * 4 + 3] = rows[r]
        local[12:15] = tr
        if b.parent < 0:
            out.append(local)
        else:
            p = out[b.parent]
            out.append([sum(local[r * 4 + k] * p[k * 4 + c] for k in range(4)) for r in range(4) for c in range(4)])
    return out


def _spheres(mab: bytes) -> list[dict]:
    """The animation_model's kind-3 list (the hit spheres): {group, bone, offset (x, y, z, w), radius}."""
    import struct
    size = len(mab)

    def text(at: int) -> str:
        end = at
        while mab[end:end + 2] != b'\0\0':
            end += 2
        return mab[at:end].decode('utf-16le')
    nlists, = struct.unpack_from('<H', mab, 12)
    heads, = struct.unpack_from('<I', mab, 20)
    out = []
    for k in range(nlists):
        kind, n, first = struct.unpack_from('<HHi', mab, heads + 8 * k)
        if kind != 3:
            continue
        base = heads + 8 * k + first
        for i in range(n):
            r = base + 0x20 * i
            name, bone, _flags, offset = struct.unpack_from('<4i', mab, r)
            if not 0 < r + offset <= size - 16:
                raise ValueError('a hit sphere offset outside the model block')
            out.append({'group': text(r + name), 'bone': text(r + bone),
                        'offset': list(struct.unpack_from('<4f', mab, r + offset)),
                        'radius': struct.unpack_from('<f', mab, r + 0x10)[0]})
    if not out:
        raise ValueError('no hit spheres (kind 3) in the animation_model')
    return out


def _limb(bones: set[str]) -> int:
    side = {b[-2:] for b in bones}
    if len(side) != 1 or side.pop() not in ('_l', '_r'):
        return -1
    left = next(iter(bones)).endswith('_l')
    leg = any(b.startswith('foot') for b in bones)
    return (2 if leg else 0) + (0 if left else 1)


def _f(x: float) -> float:
    return round(x, 3) + 0.0


def measure_variant(game, stem: str) -> dict:  # noqa: ANN001 - rootcpk.Game
    import cas_graft
    import cas_pose
    import dsgo
    import mdb
    sgo = game.read('OBJECT', stem + '.SGO')
    cas = game.read('OBJECT', stem + '.CAS')
    mrab = game.read('OBJECT', stem + '.MRAB')
    doc = dsgo.parse(sgo).root
    attacks = dsgo.to_py(doc)['attack_list']
    spheres = _spheres(doc.get('animation_model').items[2].data)
    rab = mdb.rab_read(mrab)
    md = mdb.mdb_read(next(f for f in rab.files if f.name.lower() == stem.lower() + '.mdb').data)
    names = [md.name_of(b.name) for b in md.bones]
    pose = cas_pose.CasPose(cas)
    clips = {c.name: c for c in pose.clips}
    states = cas_graft.state_clips(cas)
    groups: dict[str, list[dict]] = {}
    for s in spheres:
        groups.setdefault(s['group'], []).append(s)
    limb_of = {g: _limb({p['bone'] for p in pts}) for g, pts in groups.items()}
    index = {a[0]: i for i, a in enumerate(attacks)}
    out = []
    for a in attacks:
        name, (masks, _window), prev, flag, (_type, anim, _speed), _damage = a
        if len(masks) > 1 or len(prev) > MOST_PREV:
            raise ValueError(f'{stem} {name}: {len(masks)} masks, {len(prev)} predecessors (the table holds 1, {MOST_PREV})')
        mask = int(masks[0]) if masks else 0
        row = {'name': name, 'mask': mask, 'flag': int(flag), 'prev': [index[p] for p in prev],
               'limb': -1, 'reach': [0.0, 0.0, 0.0], 'radius': 0.0, 'frame': 0, 'frames': 0}
        anim = anim if isinstance(anim, str) else None
        clip = additive = None
        for player in ('upper', 'main'):
            got = states.get(player, {}).get(anim or '')
            if got:
                clip, additive = clips[got[0]], player == 'upper'
                break
        if clip is None and anim and 'attack_' + anim in clips:   # heavyChop_r, maxHook_l: their states name no clip
            clip, additive = clips['attack_' + anim], False
        if clip is not None and not anim.startswith('ground_charge') and anim != 'lift_up':
            frames = cas_pose.clip_info(pose, clip)[2]
            wanted = (2, 3) if mask in FOOT_MASKS else (0, 1)
            far: dict[int, tuple] = {}     # limb: (centre, radius, frame) farthest forward
            start: dict[int, float] = {}   # limb: its farthest forward at frame 0
            for f in range(frames):
                w = (_world(pose, md, clips[BASE_CLIP], 0, clip, f) if additive else _world(pose, md, clip, f))
                for g, pts in groups.items():
                    limb = limb_of[g]
                    if limb not in wanted:
                        continue
                    for p in pts:
                        m = w[names.index(p['bone'])]
                        o = p['offset']
                        c = [o[0] * m[k] + o[1] * m[4 + k] + o[2] * m[8 + k] + o[3] * m[12 + k] for k in range(3)]
                        if f == 0:
                            start[limb] = max(start.get(limb, c[2]), c[2])
                        if limb not in far or c[2] > far[limb][0][2]:
                            far[limb] = (c, p['radius'], f)
            if far:
                limb = max(far, key=lambda k: (far[k][0][2] - start[k], -k))
                c, radius, f = far[limb]
                row.update(limb=limb, reach=[_f(x) for x in c], radius=_f(radius), frame=f, frames=frames)
        out.append(row)
    hashes = {k: hashlib.sha256(v).hexdigest() for k, v in (('sgo', sgo), ('cas', cas), ('mrab', mrab))}
    return {'file': stem, 'attacks': out, 'hashes': hashes,
            'groups': [{'name': g, 'limb': limb_of[g]} for g in groups]}


def measure(game) -> dict:  # noqa: ANN001
    return {'inputs': inputs(), 'variants': [measure_variant(game, s) for s in VARIANTS]}


def _cf(x: float) -> str:
    s = f'{x:.6g}'
    return (s if '.' in s or 'e' in s else s + '.0') + 'f'


def _w(text: str) -> str:
    return 'L"' + ''.join(c if ord(c) < 0x80 else f'\\x{ord(c):04X}' for c in text) + '"'


def render(data: dict) -> str:
    lines = ['// Generated by tools/gen_barga_reach.py from the Balam\'s SGO / CAS / MRAB in Root.cpk. Do not edit:',
             '// python tools/gen_barga_reach.py (with the game) rewrites it; src/barga_aim.h includes it.',
             DATA + json.dumps(data, sort_keys=True, separators=(',', ':'), ensure_ascii=True)]
    names = []
    for v in data['variants']:
        ident = 'k' + ''.join(p.capitalize() for p in v['file'].split('_')) + 'Attacks'
        names.append((v, ident))
        lines.append(f'inline constexpr Attack {ident}[]={{')
        for a in v['attacks']:
            prev = a['prev'] + [-1] * (MOST_PREV - len(a['prev']))
            lines.append(f'    {{"{a["name"]}",{a["mask"]},{a["flag"]},{a["limb"]},{len(a["prev"])},{{{",".join(map(str, prev))}}},'
                         f'{{{",".join(_cf(x) for x in a["reach"])}}},{_cf(a["radius"])},{a["frame"]},{a["frames"]}}},')
        lines.append('};')
    groups = {}
    for v in data['variants']:
        for g in v['groups']:
            groups.setdefault(g['name'], g['limb'])
    lines.append('inline constexpr HitGroup kHitGroups[]={')
    lines += [f'    {{{_w(n)},{limb}}},' for n, limb in groups.items()]
    lines.append('};')
    lines.append('inline constexpr Variant kVariants[]={')
    for v, ident in names:
        att = [a['name'] for a in v['attacks']]
        cannon = att.index('attack_cannon') if 'attack_cannon' in att else -1
        charge = att.index('ground_charge') if 'ground_charge' in att else -1
        lines.append(f'    {{"{v["file"]}",{ident},{len(att)},{cannon},{charge}}},')
    lines.append('};')
    return '\n'.join(lines) + '\n'


def _committed() -> str:
    with open(OUT, encoding='utf-8', newline='') as f:
        return f.read().replace('\r\n', '\n')


def recorded() -> dict:
    line = next((x for x in _committed().split('\n') if x.startswith(DATA)), None)
    if line is None:
        raise ValueError(f'{OUT}: no {DATA.strip()} line')
    return json.loads(line[len(DATA):])


def check(game=None) -> str | None:  # noqa: ANN001
    try:
        text = _committed()
    except FileNotFoundError:
        return 'missing'
    if game is not None:
        return None if text == render(measure(game)) else 'the game\'s files measure otherwise'
    try:
        data = recorded()
    except ValueError as e:
        return str(e)
    if data.get('inputs') != json.loads(json.dumps(inputs())):
        return f'made from {data.get("inputs")}, the inputs are now {inputs()}'
    return None if render(data) == text else 'its tables are not what its data line renders'


def main(argv: list[str]) -> int:
    from rootcpk import DEFAULT_GAME, Game
    have = os.path.isfile(os.path.join(DEFAULT_GAME, 'Root.cpk'))
    if '--check' in argv:
        why = check(Game(DEFAULT_GAME) if have else None)
        print(f'{os.path.relpath(OUT)}: {"current" if why is None else "STALE: " + why} '
              f'({"re-measured on the game" if have else "no Root.cpk: checked against its own record"})')
        return 0 if why is None else 1
    if not have:
        print('needs the game (Root.cpk) to read the Balam\'s files', file=sys.stderr)
        return 2
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(render(measure(Game(DEFAULT_GAME))))
    print('wrote', OUT)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
