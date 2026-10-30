"""The fixed gun emplacements (the user, 2026-10-24: 「再做一类在地图上固定的炮台。这个炮台使用无畏舰上面那几个」): the
Dreadnought's three secondary turrets (its model folder, tools/prep_dreadnought.py: SecShort, SecMid, SecLong, the
same guns the ship carries), each alone on a fortified base, on the native V603 skeleton the Kepler's class drives
(pylib/wt_tracked.py builds them as it does the War Thunder vehicles: spec() is the model folder those read).

The turret turns on cannon_main and its guns lift on the cradle (cannon_slide_l: both holders fire from it, the
quad Vulcan's one cradle, tools/make_tracked_combat.py ONE_CRADLE), as the Kepler's turret and guns do: the gunner
lays it with the native turret controls and the NPC gunners' native AI. It does not drive: the base covers the native
wheels (they stay where the V603 has them, inside it, with the tracks' pieces the native track animation needs) and
the plugin passes its driver no drive input (src/emplacement.cpp: the marker bone MARKER says it is one).

    spec(kind)  the wt_tracked.Spec the kind's model is built from (KINDS)
"""
from __future__ import annotations

import os
from dataclasses import dataclass, field

import numpy as np

import obj_model as om
import tracked_combat_model as t
import wt_tracked as wt
import xm_units
from tracked_combat_model import HOST_ARC, HOST_MDB, HOST_CAS, TrackedCombatCheckError  # noqa: F401 - the modules' API

MODEL_SUBDIR = 'dreadnought'          # pylib/dreadnought_model.py MODEL_SUBDIR: the Dreadnought's model folder
SOURCE = 'dreadnought.obj'
HULL_MATERIAL = 'VH_DreadH_ID01_Inst'   # the base's plating: the dreadnought's hull's
MARKER = 'edf6vc_emplacement'         # src/emplacement.cpp kMarker: a level leaf under body, no geometry
ROTOR = 'edf6vc_m61_rotor'            # a gun's spinning barrels (src/vulcan_rotor.cpp spins it as the M163's)
# The base (m): a regular octagon BASE_RADIUS across its corners, BASE_TOP tall, over the native wheels (the V603's
# reach x +-1.25 m, z -2.4 .. 2.5 m, tops 0.9 m), its foot BASE_FOOT over the ground (it stands on those wheels).
BASE_RADIUS, BASE_TOP, BASE_FOOT = 3.4, 1.3, 0.12
assert BASE_TOP == xm_units.EMPLACEMENT_BASE
TEXTURE_METRES = 6.0                  # m of the base's plating one hull texture repeat
# The tracks' pieces (the native track materials, v603_kyata_L / _R, which the SGO's track animation scrolls): a box
# each side, inside the base.
TRACK_BOX = ((0.8, 0.25, -1.5), (1.1, 0.55, 1.5))
# The native wheels' pieces (tracked_collision sizes each physics wheel by its piece: wheel_<letter>_<side>): a box
# WHEEL_RADIUS round each wheel's axle where the V603 has it, under the base (its edge 2 m out past them).
WHEEL_RADIUS, WHEEL_HALF_WIDTH = 0.35, 0.1


@dataclass(frozen=True)
class Emplacement:
    title: str
    turret: str           # the Dreadnought model's turret object (its gun the object turret + '_gun'); '' for `folder`
    material: str         # its source material (the folder's <material>_df / _nm maps)
    scale: float          # the source's size to the emplacement's
    durability: float
    folder: str = ''      # a model folder tools/prep_glb_model.py made, in place of a Dreadnought turret
    eye: tuple[float, float, float] | None = None   # the gunner's first-person eye (on cannon_main, or cannon_l where the raised guns would sweep it: pylib/cockpit_fits_plugin.py), else over the guns


# prep_dreadnought's turrets: turret_0/1 the SecMid (on the bow's domes), 2..5 the SecShort, 6/7 the SecLong.
KINDS = {
    'emp_mid': Emplacement('Dreadnought SecMid emplacement', 'turret_0', 'WP_DreadnoughtSecMid01_INST', 0.45, 3000.0),
    'emp_short': Emplacement('Dreadnought SecShort emplacement', 'turret_2', 'WP_DreadSecShort01_INST', 0.30, 3600.0),
    'emp_long': Emplacement('Dreadnought SecLong emplacement', 'turret_6', 'WP_DreadSecLong01_INST', 0.35, 3300.0),
    # The real guns (tools/prep_glb_model.py's folders from Sketchfab models; the user, 2026-10-24: 「还有各种现实的炮台、
    # 近防炮」), at their own size.
    # The Phalanx's eye beside its radome (the radar over the gun fills the view from above it), at the gun's height.
    'ciws_phalanx': Emplacement('Phalanx CIWS Mk 15', '', '', 1.0, 1800.0, 'emp_phalanx', (0.0, 6.35, 0.3)),
    'flak88': Emplacement('8.8 cm Flak 18/36', '', '', 1.0, 1500.0, 'emp_flak88'),
    'gdf005': Emplacement('Oerlikon GDF-005 35 mm', '', '', 1.0, 1400.0, 'emp_gdf005'),
    'laser': Emplacement('Laser air-defence turret', '', '', 1.0, 1600.0, 'emp_laser'),
    'railgun': Emplacement('Railgun turret', '', '', 1.0, 2400.0, 'emp_railgun'),
    'patriot': Emplacement('MIM-104 Patriot launcher', '', '', 1.0, 2000.0, 'emp_patriot', (0.0, 5.9, -4.0)),
}
# The X-Morph: Defense forts' guns (pylib/xm_units.py, family 'emplacement'): their folders tools/prep_xmorph.py
# emplacement writes in tools/prep_glb_model.py's format, at xm_units.SCALE (the folder's own size), the sight the row's.
KINDS.update({u.key: Emplacement(f'{u.title_en} (X-Morph: Defense)', '', '', 1.0, u.durability, xm_units.folder(u.key),
                                 xm_units.eye(u)) for u in xm_units.ported('emplacement')})
EYE_UP, EYE_BACK = 0.8, 0.6           # m: a gunner's sight over the guns' top, behind their trunnions
# A box's 12 triangles, outward, over its corners numbered by bits (x 1, y 2, z 4: set = the high side).
BOX_TRIS = ((0, 2, 1), (1, 2, 3), (4, 5, 6), (5, 7, 6), (0, 1, 4), (1, 5, 4),
            (2, 6, 3), (3, 6, 7), (0, 4, 2), (2, 4, 6), (1, 3, 5), (3, 7, 5))
# Every V603 physics proxy (its ragdoll's): what the emplacement's collision fits each to (collision_pieces).
WHEELS = tuple(f'tire_move{c}_{s}' for c in 'ABCDEFGH' for s in 'lr')
GUN_SPLIT = 0.3      # of the guns' length from their back: cannon_r's proxy (the breech), the rest cannon_l's

_SPECS: dict[tuple, wt.Spec] = {}


class EmplacementError(TrackedCombatCheckError):
    pass


def _req(ok: bool, message: str) -> None:
    if not ok:
        raise EmplacementError(message)


def folder() -> str | None:
    """The Dreadnought's model folder, None when it is not there."""
    d = om.model_dir(MODEL_SUBDIR)
    return d if d is not None and os.path.isfile(os.path.join(d, SOURCE)) else None


def source_folder(kind: str) -> str | None:
    """The folder `kind` is built from (its own prepared one, else the Dreadnought's), None when it is not there."""
    e = KINDS[kind]
    if not e.folder:
        return folder()
    d = om.model_dir(e.folder)
    return d if d is not None and all(os.path.isfile(os.path.join(d, f)) for f in ('model.json', 'model.npz')) else None


def _read(path: str, objects: set[str]) -> dict[str, tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]]:
    """object -> (positions, normals, uvs, triangles) of the OBJ's `objects` (one material each in the Dreadnought's
    folder: prep_dreadnought writes one object a bone), corners unwelded per (v, vt, vn); uv (u, -v) as the War Thunder
    folders store them (wt_tracked: the game's top-left origin)."""
    V: list[list[str]] = []
    T: list[list[str]] = []
    N: list[list[str]] = []
    faces: dict[str, list[list[str]]] = {}
    cur = None
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            head = line[:3]
            if head == 'v  ' or head[:2] == 'v ':
                V.append(line.split()[1:4])
            elif head == 'vt ':
                T.append(line.split()[1:3])
            elif head == 'vn ':
                N.append(line.split()[1:4])
            elif head[:2] == 'o ':
                cur = line[2:].strip()
            elif head[:2] == 'f ' and cur in objects:
                faces.setdefault(cur, []).append(line.split()[1:])
    pv, tv, nv = (np.asarray(x, dtype=np.float64) for x in (V, T, N))
    out = {}
    for name, rows in faces.items():
        corners: dict[str, int] = {}
        tris = []
        for row in rows:
            idx = [corners.setdefault(c, len(corners)) for c in row]
            tris += [(idx[0], idx[k], idx[k + 1]) for k in range(1, len(idx) - 1)]
        keys = np.asarray([[int(x) - 1 for x in c.split('/')] for c in corners], dtype=np.int64)
        uv = tv[keys[:, 1]] * (1.0, -1.0)
        out[name] = (pv[keys[:, 0]], nv[keys[:, 2]], uv, np.asarray(tris, dtype=np.int64))
    _req(set(out) == objects, f'{path}: objects missing: {sorted(objects - set(out))}')
    return out


def _octagon() -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """The base: its sides and top (its underside is never seen), flat-shaded, the plating mapped by the metre."""
    ring = [(BASE_RADIUS * np.cos(a), BASE_RADIUS * np.sin(a)) for a in np.arange(8) * np.pi / 4 + np.pi / 8]
    pos, nrm, uv, tris = [], [], [], []
    for k in range(8):
        (x0, z0), (x1, z1) = ring[k], ring[(k + 1) % 8]
        n = np.array((z1 - z0, 0.0, -(x1 - x0)))
        n /= np.linalg.norm(n)
        if np.dot(n, (x0 + x1, 0.0, z0 + z1)) < 0:
            n = -n
        b = len(pos)
        quad = [(x0, BASE_FOOT, z0), (x1, BASE_FOOT, z1), (x1, BASE_TOP, z1), (x0, BASE_TOP, z0)]
        run = np.hypot(x1 - x0, z1 - z0)
        pos += quad
        nrm += [n] * 4
        uv += [(0.0, 0.0), (run / TEXTURE_METRES, 0.0), (run / TEXTURE_METRES, -BASE_TOP / TEXTURE_METRES),
               (0.0, -BASE_TOP / TEXTURE_METRES)]
        tris += _outward([(b, b + 1, b + 2), (b, b + 2, b + 3)], pos, n)
    b = len(pos)
    pos += [(x, BASE_TOP, z) for x, z in ring] + [(0.0, BASE_TOP, 0.0)]
    nrm += [(0.0, 1.0, 0.0)] * 9
    uv += [(x / TEXTURE_METRES, -z / TEXTURE_METRES) for x, z in ring] + [(0.0, 0.0)]
    tris += _outward([(b + 8, b + k, b + (k + 1) % 8) for k in range(8)], pos, np.array((0.0, 1.0, 0.0)))
    return (np.asarray(pos, dtype=np.float64), np.asarray(nrm, dtype=np.float64), np.asarray(uv, dtype=np.float64),
            np.asarray(tris, dtype=np.int64))


def _outward(tris: list[tuple[int, int, int]], pos: list, n: np.ndarray) -> list[tuple[int, int, int]]:
    """Each triangle wound counter-clockwise seen from `n`'s side (the War Thunder parts' winding)."""
    out = []
    for a, b, c in tris:
        pa, pb, pc = (np.asarray(pos[i], dtype=float) for i in (a, b, c))
        out.append((a, b, c) if np.dot(np.cross(pb - pa, pc - pa), n) > 0 else (a, c, b))
    return out


def _box(lo, hi) -> tuple[np.ndarray, np.ndarray]:
    lo, hi = np.asarray(lo, dtype=np.float64), np.asarray(hi, dtype=np.float64)
    return np.array([[(lo, hi)[i >> k & 1][k] for k in range(3)] for i in range(8)]), np.asarray(BOX_TRIS, dtype=np.int64)


def _box_part(lo, hi) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """A closed box as a drawn part (flat normals, any UVs): the hidden track pieces."""
    P, T = _box(lo, hi)
    pos, nrm, uv, tris = [], [], [], []
    centre = P.mean(0)
    for a, b, c in T:
        n = np.cross(P[b] - P[a], P[c] - P[a])
        n /= np.linalg.norm(n)
        k = len(pos)
        pos += [P[a], P[b], P[c]]
        nrm += [n] * 3
        uv += [(0.0, 0.0), (0.1, 0.0), (0.0, -0.1)]
        tris.append((k, k + 1, k + 2) if np.dot(n, (P[a] + P[b] + P[c]) / 3 - centre) > 0 else (k, k + 2, k + 1))
    return np.asarray(pos), np.asarray(nrm), np.asarray(uv), np.asarray(tris, dtype=np.int64)


def _muzzles(gun: np.ndarray, muzzle: np.ndarray) -> dict[str, np.ndarray]:
    """The guns' mouths: the source's muzzle marker's height and reach, at the barrels' left and right tips (their
    frontmost points' two sides when there are two barrels, else both the one)."""
    front = gun[gun[:, 2] > gun[:, 2].max() - 0.04 * (gun[:, 2].max() - gun[:, 2].min())]
    xs = front[:, 0]
    mid = (xs.min() + xs.max()) / 2
    left, right = front[xs > mid], front[xs <= mid]
    twin = xs.max() - xs.min() > 0.25 * (gun[:, 0].max() - gun[:, 0].min()) and len(left) and len(right)
    at = lambda side: np.array((side[:, 0].mean(), muzzle[1], muzzle[2]))   # noqa: E731
    return {'left': at(left), 'right': at(right)} if twin else {'left': muzzle.copy(), 'right': muzzle.copy()}


@dataclass
class Source:
    """What an emplacement is made of, in game space and metres on the base's top: its pieces (name, bone, its
    material's index into `materials`, (positions, normals, uvs, triangles)), the turret's turning axis (`ring`), the
    guns' trunnion (`cradle`), their mouths, a spinning barrel cluster's axis (`rotor`: its point at the back), the
    base's plating (`plating`: its texture files) and a launcher's tube mouths (`tubes`: its missile holder's)."""
    folder: str
    title: str
    source: str
    materials: list[dict]
    pieces: list[tuple[str, str, int, tuple]]
    ring: np.ndarray
    cradle: np.ndarray
    mouths: dict[str, np.ndarray]
    plating: dict
    rotor: np.ndarray | None = None
    tubes: list[np.ndarray] = field(default_factory=list)


def _dreadnought(kind: str, e: Emplacement) -> Source:
    """A Dreadnought turret (the model folder's OBJ object e.turret and its gun), scaled, its lowest point on the base."""
    import json
    d = folder()
    _req(d is not None, f'{kind}: the Dreadnought model folder ({MODEL_SUBDIR}/{SOURCE}) is not there')
    with open(os.path.join(d, 'dreadnought.json'), encoding='utf-8') as f:  # type: ignore[arg-type]
        bones = json.load(f)['models']['dreadnought']['bones']
    gun_name = e.turret + '_gun'
    got = _read(os.path.join(d, SOURCE), {e.turret, gun_name})  # type: ignore[arg-type]
    pivot = np.asarray(bones[e.turret]['pivot'], dtype=np.float64)
    trunnion = np.asarray(bones[gun_name]['pivot'], dtype=np.float64)
    mouth = np.asarray(bones[e.turret + '_muzzle']['pivot'], dtype=np.float64)
    # The turret's lowest point onto the base's top, its turning axis on the vehicle's: source -> game, each a point.
    lowest = got[e.turret][0][:, 1].min()
    place = lambda p: (np.asarray(p) - (pivot[0], lowest, pivot[2])) * e.scale + (0.0, BASE_TOP, 0.0)   # noqa: E731
    placed = lambda name: (place(got[name][0]),) + got[name][1:]   # noqa: E731
    tex = lambda m, s: os.path.join(d, f'{m}_{s}.png')   # noqa: E731 - absolute: wt_tracked joins it as it is
    gun = {'name': f'edf6vc_{kind}_gun', 'template': 'v603_body', 'albedo': tex(e.material, 'df'),
           'normal': tex(e.material, 'nm')}
    plating = {'albedo': tex(HULL_MATERIAL, 'df'), 'normal': tex(HULL_MATERIAL, 'nm')}
    return Source(d, e.title, f'Dreadnought (Yager / Six Foot) secondary turret {e.turret} of {MODEL_SUBDIR}/{SOURCE}',  # type: ignore[arg-type]
                  [gun], [('cannon_main.turret', 'cannon_main', 0, placed(e.turret)),
                          ('cannon_slide_l.guns', 'cannon_slide_l', 0, placed(gun_name))],
                  place(pivot), place(trunnion), _muzzles(place(got[gun_name][0]), place(mouth)), plating)


ROLE_BONES = {'base': 'body', 'turret': 'cannon_main', 'gun': 'cannon_slide_l', 'rotor': ROTOR}


def _prepared(kind: str, e: Emplacement) -> Source:
    """A model folder tools/prep_glb_model.py made (its parts by role: base, turret, gun, rotor), on the base's top."""
    import json
    d = om.model_dir(e.folder)
    _req(d is not None, f'{kind}: the model folder {e.folder} is not there')
    with open(os.path.join(d, 'model.json'), encoding='utf-8') as f:  # type: ignore[arg-type]
        info = json.load(f)
    _req(info.get('format') == 1 and info.get('kind') == kind, f'{d}: not a format-1 {kind} model')
    with np.load(os.path.join(d, 'model.npz'), allow_pickle=False) as z:  # type: ignore[arg-type]
        arrays = {k: z[k] for k in z.files}
    up = np.array((0.0, BASE_TOP, 0.0))
    pieces = []
    for k, p in enumerate(info['parts']):
        geo = (arrays[f'p{k}_pos'].astype(np.float64) * e.scale + up, arrays[f'p{k}_nrm'].astype(np.float64),
               arrays[f'p{k}_uv'].astype(np.float64), arrays[f'p{k}_tri'].astype(np.int64))
        pieces.append((f'{ROLE_BONES[p["role"]]}.{p["name"]}', ROLE_BONES[p['role']], p['material'], geo))
    v = info['pivots']
    turret = np.vstack([g[0] for _n, b, _m, g in pieces if b == 'cannon_main'])
    ring = np.array((v['ring'][0] * e.scale, turret[:, 1].min(), v['ring'][1] * e.scale))
    cradle = np.array((ring[0], v['trunnion'][0] * e.scale + BASE_TOP, v['trunnion'][1] * e.scale))
    mouths = {s: np.asarray(v['muzzles'][s]) * e.scale + up for s in ('left', 'right')}
    rotor = np.asarray(v['rotor']) * e.scale + up if 'rotor' in v else None
    tubes = [np.asarray(p) * e.scale + up for p in v.get('tubes', [])]
    materials = [{**m, **{k: os.path.join(d, m[k]) for k in ('albedo', 'normal', 'specular', 'emissive') if k in m}}  # type: ignore[arg-type]
                 for m in info['materials']]
    plating = {'albedo': os.path.join(d, 'textures', 'plating.png')}  # type: ignore[arg-type]
    return Source(d, e.title, info.get('source', e.folder), materials, pieces, ring, cradle, mouths, plating, rotor, tubes)  # type: ignore[arg-type]


def spec(kind: str) -> wt.Spec:
    """The kind's model spec (wt_tracked.Spec: its parts in game space and metres, on the V603's bones, the bones
    moved under them: the turret ring to the base's top, the guns' trunnions to the source's), its source's on the
    fortified base with the native wheels and tracks hidden in it."""
    e = KINDS[kind]
    key = (kind, _stamp(e))
    if key in _SPECS:
        return _SPECS[key]
    src = _prepared(kind, e) if e.folder else _dreadnought(kind, e)
    parts, arrays = [], {}

    def add(name: str, bone: str, material: int, geo) -> None:  # noqa: ANN001
        k = len(parts)
        pos, nrm, uv, tri = geo
        parts.append({'name': name, 'bone': bone, 'material': material, 'triangles': len(tri), 'vertices': len(pos)})
        arrays[f'p{k}_pos'], arrays[f'p{k}_nrm'] = pos.astype(np.float32), nrm.astype(np.float32)
        arrays[f'p{k}_uv'], arrays[f'p{k}_tri'] = uv.astype(np.float32), tri.astype(np.uint32)
    first = len(src.materials)
    materials = list(src.materials) + [
        {'name': 'edf6vc_emp_base', 'template': 'v603_body', **src.plating},
        {'name': 'v603_kyata_L', 'template': 'v603_kyata_L', 'albedo': src.plating['albedo']},
        {'name': 'v603_kyata_R', 'template': 'v603_kyata_R', 'albedo': src.plating['albedo']},
    ]
    add('body.plinth', 'body', first, _octagon())
    for name, bone, material, geo in src.pieces:
        add(name, bone, material, geo)
    (x0, y0, z0), (x1, y1, z1) = TRACK_BOX
    add('catapi_body.v603_kyata_L', 'catapi_body', first + 1, _box_part((x0, y0, z0), (x1, y1, z1)))
    add('catapi_body.v603_kyata_R', 'catapi_body', first + 2, _box_part((-x1, y0, z0), (-x0, y1, z1)))
    for w in WHEELS:
        c = np.asarray(_wheel_origin(w))
        half = np.array((WHEEL_HALF_WIDTH, WHEEL_RADIUS, WHEEL_RADIUS))
        add(f'wheel_{w[9]}_{w[-1]}', w, first, _box_part(c - half, c + half))
    ring, cradle = src.ring, src.cradle

    def on(*bones: str) -> np.ndarray:
        return np.vstack([arrays[f'p{k}_pos'] for k, p in enumerate(parts) if p['bone'] in bones]).astype(np.float64)
    turret, guns = on('cannon_main'), on('cannon_slide_l', ROTOR)
    top = float(max(turret[:, 1].max(), guns[:, 1].max()))
    origins = {'cannon_main': ring.tolist(),
               # both native gun hinges and slides on the guns' trunnions: the guns lift and recoil about them
               **{b: cradle.tolist() for b in ('cannon_l', 'cannon_r', 'cannon_slide_l', 'cannon_slide_r')},
               # the radar bones (the Kepler's dishes, no geometry here) inside the turret
               'doppler_radar': [ring[0], top - 0.5, ring[2]], 'tracking_radar': [ring[0], top - 0.5, ring[2]]}
    visual = [{'name': MARKER, 'parent': 'body', 'origin': [0.0, 0.0, 0.0]}]
    if src.rotor is not None:
        visual.append({'name': ROTOR, 'parent': 'cannon_slide_l', 'origin': src.rotor.tolist()})
    # Collision: every proxy a box (the base, the turret, the guns' breech and barrels), the radars and wheels small
    # boxes inside what holds them (cannon_main, the base).
    collision, carr = {}, {}

    def hit(proxy: str, lo, hi) -> None:  # noqa: ANN001
        P, T = _box(lo, hi)
        carr[f'c_{proxy}_pos'], carr[f'c_{proxy}_tri'] = P.astype(np.float32), T.astype(np.uint32)
        collision[proxy] = {'target': [np.asarray(lo, float).tolist(), np.asarray(hi, float).tolist()],
                            'source_triangles': 12, 'hull_triangles': 12}
    r = BASE_RADIUS * np.cos(np.pi / 8)
    hit('body', (-r, BASE_FOOT, -r), (r, BASE_TOP, r))
    hit('cannon_main', turret.min(0), turret.max(0))
    glo, ghi = guns.min(0), guns.max(0)
    split = glo[2] + GUN_SPLIT * (ghi[2] - glo[2])
    hit('cannon_r', glo, (ghi[0], ghi[1], split))
    hit('cannon_l', (glo[0], glo[1], split + 0.05), ghi)
    for proxy in ('doppler_radar', 'tracking_radar'):
        c = np.asarray(origins[proxy])
        hit(proxy, c - 0.2, c + 0.2)
    for w in WHEELS:
        c = np.asarray(_wheel_origin(w))
        hit(w, c - 0.15, c + 0.15)
    arrays.update(carr)
    info = {'format': wt.FORMAT, 'kind': kind, 'title': src.title, 'source': src.source,
            'materials': materials, 'parts': parts, 'origins': origins, 'visual_bones': visual, 'wheels': {},
            'muzzles': {side: [{'bone': 'cannon_slide_l', 'point': np.asarray(p).tolist()}]
                        for side, p in src.mouths.items()} | ({'missiles': [{'bone': 'cannon_main', 'point': np.asarray(p).tolist()}
                                                                             for p in src.tubes]} if src.tubes else {}),
            'ejections': {side: {'bone': 'cannon_slide_l', 'point': cradle.tolist()} for side in ('left', 'right')},
            'collision': collision, 'triangles': int(sum(p['triangles'] for p in parts)),
            'eye': list(e.eye) if e.eye else [round(float(cradle[0]), 3), round(top + EYE_UP, 3),
                                              round(float(min(guns[:, 2].min(), cradle[2]) - EYE_BACK), 3)]}
    s = wt.Spec(src.folder, info, arrays)
    _SPECS.clear()
    _SPECS[key] = s
    return s


def _stamp(e: Emplacement) -> tuple:
    """What the kind's spec is made from, as it is now (the cache's key)."""
    d = om.model_dir(e.folder) if e.folder else folder()
    _req(d is not None, f'the model folder {e.folder or MODEL_SUBDIR} is not there')
    files = ('model.json', 'model.npz') if e.folder else (SOURCE, 'dreadnought.json')
    return (d,) + tuple((f, os.stat(os.path.join(d, f)).st_mtime_ns) for f in files)  # type: ignore[arg-type]


# The V603's wheel bones where its model has them (the emplacement leaves them there, inside its base): tracked_collision
# needs a proxy box on each. Measured from the stock model at first use (rootcpk), cached.
_WHEELS: dict[str, tuple[float, float, float]] = {}


def _wheel_origin(bone: str) -> tuple[float, float, float]:
    if not _WHEELS:
        import rootcpk
        from mdb import bind_world
        host, _rab = t.stock(rootcpk.default())
        w = bind_world(host)
        for name in WHEELS:
            _WHEELS[name] = tuple(float(x) for x in w[host.bone_index(name)][12:15])
    return _WHEELS[bone]


# Its camera over the base, the whole turret in view behind it (the SGO's game_object_camera_setting).
CAMERA = {'model_eye': (0.0, BASE_TOP + 3.0, 0.0), 'model_look_at': (0.0, BASE_TOP + 2.0, 8.0),
          'third_person_distance': 13.0, 'third_person_height': BASE_TOP + 6.5}
MODELS = {kind: wt.Model(kind, CAMERA) for kind in KINDS}
