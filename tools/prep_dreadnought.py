"""Developer-only: the Dreadnought's ships (Yager / Six Foot's "Dreadnought", UE 4.14, offline since 2023) out of its
game files into the model folder the installer builds them from (pylib/dreadnought_model.py):

    <models>/dreadnought/<model>.obj / .mtl   one model in game space and metres: +Y up, +Z the bow, +X its left;
                                              one OBJ object per bone (MODELS' bones), every triangle rigid on it
    <models>/dreadnought/<material>_df.png    its colour: its paint (colour mask x colours) x the source's grey
                                              diffuse x its ambient occlusion (the ship's: its default vanity, PAINT)
    <models>/dreadnought/<material>_nm.png    its normal map (the source's, DirectX green as the game's)
    <models>/dreadnought/<material>_rml.png   R roughness, G metal, B light mask (the source's lights: AO's alpha)
    <models>/dreadnought/dreadnought.json     every model's bones (pivot, parent), size and materials
    <models>/dreadnought/SOURCE.txt           where it came from

The source is the folder CUE4Parse wrote the game's meshes and textures into (glb + png, every path as in the game's
paks; a developer's own CUE4Parse exporter, EGame UE4_13 parses its 4.14 packages): each ship a skeletal mesh of one
real bone (root), +Y up, +X the bow, metres / 100 of the game's world.

Size: the Dreadnought's world is scaled down (a dreadnought 19.4 of its metres, its fighters 0.68): the user wants
the original size (2026-10-21「原作多大他就多大」), and the original gives none in metres; so its proportions are
kept with the fighters a fighter's size (17.6 m) and the dreadnought SHIP_LENGTH long.

The hangar modules (Abilities/Dreadnought/Hangar, the dreadnoughts' "Fighters" primary, AB_DN_Pri_Hangar_SK): the game
mounts a module by its ability's m_meshSocketTag (the fighters' AB_DN_Pri_Jet_Fighter_Ability_BP: AB_primary) on the
hull skeleton's sockets of that tag (VH_DreadH_SKEL: WP_broadside_left01 / 03, right01 / 03), so one on each
(HANGAR_SOCKETS), in its bind pose: its doors open, its lifts up (the only animation, AB_Pri_Hangar_Preview_AS, plays
in the customisation preview alone, from shut and lowered to that pose). Its side opening faces outboard (the module's
-Y); each of its two bays (its DroneSocket0 / 1: X -100 / +100, Z -50 cm, yaw -90, the fighter's nose out of it) has
its lift and its upper and lower door. Bay i (left01 0, 1, left03 2, 3, right01 4, 5, right03 6, 7) holds a copy of
the fighter of kind i % 4 (BAY_KINDS) docked on its raised lift, nose outboard: bone bay_<i> (its origin the fighter's
box centre, as the plugin's jet's; its frame rows left, up, ahead = outboard), with bay_<i>_out the same pose straight
outboard where that fighter's box (BAY_MARGIN m larger) is clear of the ship's collision (the plugin slides the copy
out, hides it there and makes the real jet in its place; bay_markers). Every module is in the folder; which are fitted
is the installer's (the user, 2026-10-24: each socket's module fitted or not: pylib/dreadnought_model.py HANGAR_KEYS).
The foredeck's ready-fighter markers (`ready`, `ready_side`) are no longer made: a player boards from a hangar (2026-10-24).
Its turrets (the user, 2026-10-21: 「船的炮塔是可以上人的」): the dreadnoughts' secondary guns (Weapons/Dreadnought/Sec*)
on the hull's turret mounts (turret_mounts: its round seats, found as pieces of their own), each with a yaw bone, its
guns' pitch bone and its muzzle (turret_<n>, turret_<n>_gun, turret_<n>_muzzle), so the plugin turns them and a
gunner seat fires from each (src/dreadnought.cpp).
Its marker bones (no geometry, level: the plugin reads their world matrices by name, rows left, up, ahead, the point;
ship_markers): `bridge`, where the pilot stands on leaving the ship (src/dreadnought.cpp: the roof of its bridge over
the source's PreviewTarget_Bridge socket), and the seats' first-person eyes `vc_eye_<seat>` (src/fp_vehicle.cpp
VehicleEye): seat 0 the bridge's, at its front face looking ahead over the bow, seat 1 + n turret n's, on its guns'
bone (it turns and pitches with them) over and behind its guns, looking along them. tools/make_dreadnought.py checks
them on the built model (the bridge on its collision, the bridge eye's view ahead clear).

    python tools/prep_dreadnought.py [EXPORT_DIR] [--out <models>/dreadnought] [--paint VanityItems/.../VAN_PN_..._DA]
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from dataclasses import dataclass

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import obj_model  # noqa: E402

GENERIC = 'DreadGame/Content/Generic'
SHIP_LENGTH = 500.0        # metres, the dreadnought's bow to stern: its fighters a fighter's size (see Size above)
TEX = 1024                 # every texture's size
# The source's colour is its paint x its diffuse as a shading map, in linear colour (the shader's; the diffuse is sRGB):
# the diffuse's median neutral, so the paint shows as authored on a plain plate, its edges lighter, its grime darker
# (the master's math is cooked away; its default diffuse CH_DN_Empty_D is white with alpha 0: alpha weighs it).
# The paint: the colour mask MC's R / G / B pick CustomColor01 / 02 / 03, BaseColor where none (linear, lerped).
# Its parameters are the material's own over its parents' (PARENTS: each instance's Parent, read with dnexport -info,
# as the export's JSON names none). Metal and roughness likewise per mask channel.
PARENTS = {
    'VH_DreadH_ID01_Inst': 'VH_YPawn_JupA_MASTERINST', 'VH_DreadH_ID02_INST': 'VH_YPawn_JupA_MASTERINST',
    'AB_DN_Pri_Hangar_INST': 'AB_DN_Pri_Hatch_Inst', 'AB_DN_Pri_Hatch_Inst': 'VH_DreadH_ID01_Inst',
    'WP_DreadnoughtSecMid01_INST': 'VH_YPawn_JupA_MASTERINST', 'WP_DreadSecShort01_INST': 'WP_DreadSecLong01_INST',
    'WP_DreadSecLong01_INST': 'VH_DreadH_ID01_Inst',
    'VH_Fighter_Inst': 'VH_DreadH_ID01_Inst', 'DN_Fighter_SK_Inst': 'VH_DreadH_ID01_Inst',
    'VH_Bomber_SK_INST': 'VH_DreadM_ID01_Inst', 'VH_DreadM_ID01_Inst': 'VH_YPawn_Akula_MASTERINST',
    'VH_Interceptor_Inst': 'VH_DreadL_ID01_Inst', 'VH_DreadL_ID01_Inst': 'VH_YPawn_Oberon_MASTERINST',
}
# The original's look is its vanity: the ship's paint (a colour set: BaseColor, CustomColor01-03, roughness, metal) over
# every material of the ship (hull, modules, guns), and its pattern (colour masks for the hull's material IDs in place
# of the hull's own). The Jupiter Arms heavy dreadnought's default (Loadouts/Precast/T4/
# VH_DreadnoughtHeavy_T4_PrecastLoadout_BP "Jutland", likewise T5 "Monarch"): paint VAN_PN_Manufacturer_Jupiter_DA,
# pattern VAN_PTN_DreadH_Default_DA. Read from the export (dnexport <pakdir> -json <package> > <export>/<package>.json).
# The fighters (Ships/RespawnJet) have no pattern (patterns are per dreadnought class) and keep their own colours.
VANITY = 'VanityItems'
PAINT = f'{VANITY}/_Shared/Paints/VAN_PN_Manufacturer_Jupiter_DA'
PATTERN = f'{VANITY}/Patterns/Dread/Heavy/VAN_PTN_DreadH_Default_DA'
PATTERN_IDS = ('VH_DreadH_ID01_Inst', 'VH_DreadH_ID02_INST')   # its masks' order: the hull's material IDs
PAINTED = 'dreadnought'    # the model the paint goes on
COLOUR_SET = {'m_baseColor': 'BaseColor', 'm_customColor01': 'CustomColor01', 'm_customColor02': 'CustomColor02',
              'm_customColor03': 'CustomColor03'}
COLOUR_SET_SCALARS = {'m_baseMetalic': 'BaseMetalic', 'm_baseRoughness01': 'BaseRoughness01',
                      'm_redRoughness01': 'RedRoughness01', 'm_greenRoughness01': 'GreenRoughness01',
                      'm_blueRoughness01': 'BlueRoughness01'}
DEFAULT_BASE = (0.505, 0.477, 0.444)   # linear, no BaseColor anywhere: Jupiter Arms' DetailSurfaceColor01
AO_DEFAULT = 0.8           # AoInfluence when the material has none
SHIP_METAL = 0.5           # the ship's metal (its painted materials) scaled for the game's sky (see bake)
DETAIL_MAX = 1.6


@dataclass(frozen=True)
class Part:
    """One source mesh in a model: its glb, scaled by `scale` about its origin and set at `at` in the source's space
    (its units) before the common scale. `bones`: source bone -> our bone ('': the model's body bone), a bone's pivot
    the first of its sources'; `parents`: our bone -> its parent (default: the part's first bone under the model, the
    others under that one); `muzzle`: the bone whose geometry's tip gets a muzzle bone (<bone less _gun>_muzzle).
    `face`: where the part's +X goes (the source's frame, level: a turn about the up axis), before `at`; `paint`: the
    ship's paint goes on its materials (not on a fighter's copy: its materials are the fighter model's)."""
    glb: str
    at: tuple[float, float, float] = (0.0, 0.0, 0.0)
    bones: tuple[tuple[str, str], ...] = (('root', ''),)
    scale: float = 1.0
    parents: tuple[tuple[str, str], ...] = ()
    muzzle: str = ''
    face: tuple[float, float, float] = (1.0, 0.0, 0.0)
    paint: bool = True


def turn(p: np.ndarray, face: tuple[float, float, float]) -> np.ndarray:
    """Points (n, 3) of the source's frame turned about +Y so that +X goes to `face` (+Z to (-face z, 0, face x))."""
    cx, cz = face[0], face[2]
    return np.stack([p[:, 0] * cx - p[:, 2] * cz, p[:, 1], p[:, 0] * cz + p[:, 2] * cx], axis=1)


HANGAR_GLB = 'Abilities/Dreadnought/Hangar/AB_DN_Pri_Hangar_SK.glb'
# The AB_primary sockets (VH_DreadH_SKEL, on b_offset at the origin; UE cm -> the glb: X, Z, Y over 100) and where the
# module's +X goes: the starboard ones turned (their yaw 180), so every opening (the module's -Y, glb -Z) faces outboard.
HANGAR_SOCKETS = (('left01', (-1.765, 1.205, -2.70), (1.0, 0.0, 0.0)), ('left03', (-5.5, 1.205, -2.70), (1.0, 0.0, 0.0)),
                  ('right01', (-1.765, 1.205, 2.70), (-1.0, 0.0, 0.0)), ('right03', (-5.5, 1.205, 2.70), (-1.0, 0.0, 0.0)))
# A module's bays: its DroneSocket0 / 1 (the module's frame, glb), each one's lift and doors (the elevator and doors on
# its side of the module: 02 at -X, 01 at +X); a bay's fighter faces the module's -Y (glb -Z), standing on its lift.
DRONE_SOCKETS = ((-1.0, -0.5, 0.0), (1.0, -0.5, 0.0))
BAY_SOURCE_BONES = (('b_elevator_02', 'b_upper_door02', 'b_bottom_door02'),
                    ('b_elevator_01', 'b_upper_door01', 'b_bottom_door01'))
BAY_FACE = (0.0, 0.0, -1.0)
FIGHTER_GLBS = {'fighter': 'Ships/RespawnJet/Fighter/VH_Fighter_SK.glb',
                'defender': 'Ships/RespawnJet/Defender/DN_Fighter_SK.glb',
                'bomber': 'Ships/RespawnJet/Bomber/VH_Bomber_SK.glb',
                'interceptor': 'Ships/RespawnJet/Booster/VH_Interceptor_SK.glb'}
BAY_KINDS = tuple(FIGHTER_GLBS)   # bay i holds BAY_KINDS[i % 4]
BAYS = 2 * len(HANGAR_SOCKETS)
BAY_MARGIN = 1.0      # m round a fighter's box: clear of the collision at bay_<i>_out


def hangar_bones(m: int) -> tuple[tuple[str, str], ...]:
    """Module m's bones: hangar_<m>, and for each of its bays i: lift_<i>, door_u_<i>, door_l_<i>."""
    out = [('root', f'hangar_{m}')]
    for k, (lift, up, low) in enumerate(BAY_SOURCE_BONES):
        i = 2 * m + k
        out += [(lift, f'lift_{i}'), (up, f'door_u_{i}'), (low, f'door_l_{i}')]
    return tuple(out)


MODELS: dict[str, tuple[Part, ...]] = {
    'dreadnought': (Part('Ships/Dreadnought/Heavy/VH_DreadH_SK.glb'),) + tuple(
        Part(HANGAR_GLB, at, hangar_bones(m), face=face) for m, (_n, at, face) in enumerate(HANGAR_SOCKETS)),
    **{k: (Part(g),) for k, g in FIGHTER_GLBS.items()},
}
SCALE_FROM = 'dreadnought'     # SHIP_LENGTH is this model's length
MUZZLE_DEPTH = 0.02   # source units: a gun's tip, what of it is this near its farthest forward

# The turrets (turret_mounts): a mount is a piece of the hull of MOUNT_TRIS triangles (the hull's round seats; the deck's
# own pieces are larger, its fittings smaller), its top over MOUNT_TOP (the upper deck: those lower down sit on the hull's
# flanks and belly), not on the foredeck's spine (FOREDECK: source x and half length, where the first guess had set a
# hangar module and a ready fighter now stands: the eight it has always taken, vcobjects.DREADNOUGHT_TURRETS). Its kind by its shape (source units): a dome MOUNT_DOME wide or more, a flat
# pad (under MOUNT_FLAT tall), else a small seat. Each kind's gun and its scale (to the seat's size).
MOUNT_TRIS, MOUNT_TOP, MOUNT_DOME, MOUNT_FLAT = 88, 0.9, 0.9, 0.25
FOREDECK = (3.05, 1.85)
WEAPONS = 'Weapons/Dreadnought'
TURRET_KINDS = {
    'dome': (f'{WEAPONS}/SecShort/Mesh/WP_DreadSecShort01_SK.glb', 1.9),
    'small': (f'{WEAPONS}/SecMid/MAT/WP_DreadnoughtSecMid01_SK.glb', 1.4),
    'pad': (f'{WEAPONS}/SecLong/Mesh/WP_DreadSecLong01_SK.glb', 1.6),
}
GUN_BONES = ('b_barrel', 'b_barrel_01', 'b_barrel_02', 'b_recoil01', 'b_recoil02')

# The markers (ship_markers). The bridge: the hull skeleton VH_DreadH_SKEL's socket PreviewTarget_Bridge (UE -338.7, 0,
# 173.2 cm: in the bridge, the customisation camera's target; dnexport -json Ships/Dreadnought/Heavy/VH_DreadH_SKEL), as
# the glb has it (+X the bow, +Y up, source units). Its roof is flat (2.005 units up from x -4.1 to -3.2, then falling
# 2 cm a metre to its front at -2.95; about 11 x 3 m at 200 m), two rails along its back half (2.06, x -4.1 .. -3.5).
# `bridge` STAND_UP m over the roof on its centre line at BRIDGE_STAND (source x), ahead of the rails: the collision
# (pylib/aircraft_collision.py, a height field of the highest point in each cell of the whole model's box, the same
# cells in source units at any SHIP_LENGTH) puts the cell with the rails 0.6 m over the roof; at 24 x 32 the cell from
# x -3.26 to -2.65 was the roof's own height, and at DN_GRID (48 x 64, nested in it) the stand is on the roof's height
# still (tools/make_dreadnought.py check_markers holds it).
# The bridge's eye: EYE_DOWN m under the roof over the target, EYE_OUT m ahead of the bridge's front face there (the
# face's farthest forward EYE_BAND m under the eye to the eye). A turret's: TURRET_EYE_UP m over its and its guns'
# tops, TURRET_EYE_BACK m behind its guns' rear, on their centre line.
BRIDGE_TARGET = (-3.387, 1.732, 0.0)
BRIDGE_STAND = -3.1
STAND_UP = 0.2
EYE_DOWN, EYE_OUT, EYE_BAND = 1.6, 0.5, 1.0
TURRET_EYE_UP, TURRET_EYE_BACK = 0.5, 0.5


def turret_mounts(hull: 'Mesh') -> list[tuple[str, np.ndarray]]:
    """(kind, the seat's top centre, source units) of every turret mount, bow first, port before starboard."""
    key = np.round(hull.pos * 200.0).astype(np.int64)
    _, weld = np.unique(key, axis=0, return_inverse=True)
    weld = weld.ravel()
    parent = np.arange(weld.max() + 1)

    def find(x: int) -> int:
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for a, b, c in weld[hull.tris]:
        for u, v in ((a, b), (b, c)):
            ru, rv = find(u), find(v)
            if ru != rv:
                parent[ru] = rv
    piece = np.array([find(x) for x in weld[hull.tris[:, 0]]])
    out = []
    for pid, count in zip(*np.unique(piece, return_counts=True)):
        if count != MOUNT_TRIS:
            continue
        pts = hull.pos[np.unique(hull.tris[piece == pid])]
        lo, hi = pts.min(0), pts.max(0)
        top = np.array([(lo[0] + hi[0]) / 2, hi[1], (lo[2] + hi[2]) / 2])
        if top[1] < MOUNT_TOP or abs(top[0] - FOREDECK[0]) < FOREDECK[1] + (hi[0] - lo[0]) / 2:
            continue
        size = hi - lo
        kind = 'pad' if size[1] < MOUNT_FLAT else 'dome' if size[0] >= MOUNT_DOME else 'small'
        out.append((kind, top))
    return sorted(out, key=lambda m: (-round(float(m[1][0]), 2), -float(m[1][2])))


def turret_part(n: int, kind: str, top: np.ndarray) -> Part:
    glb, scale = TURRET_KINDS[kind]
    t, g = f'turret_{n}', f'turret_{n}_gun'
    return Part(glb, tuple(float(x) for x in top), (('b_base', t), ('root', t)) + tuple((b, g) for b in GUN_BONES),
                scale, ((t, ''), (g, t)), g)


def ray(tris: np.ndarray, origin, direction) -> float | None:
    """The nearest hit's distance along `direction` (a unit) from `origin` against triangles (n, 3, 3), None: none."""
    o, d = np.asarray(origin, float), np.asarray(direction, float)
    v0 = tris[:, 0]
    e1, e2 = tris[:, 1] - v0, tris[:, 2] - v0
    h = np.cross(d, e2)
    a = (e1 * h).sum(1)
    ok = np.abs(a) > 1e-12
    f = np.where(ok, 1.0 / np.where(ok, a, 1.0), 0.0)
    s = o - v0
    u = f * (s * h).sum(1)
    q = np.cross(s, e1)
    v = f * (q * d).sum(1)
    t = f * (e2 * q).sum(1)
    hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1e-6)
    return float(t[hit].min()) if hit.any() else None


def ship_markers(hull: 'Mesh', bones: dict, extent: dict, scale: float) -> dict[str, dict]:
    """The dreadnought's marker bones (see the module doc), {bone: {'parent', 'pivot'}} in game space (unshifted)."""
    tris = hull.pos[hull.tris]
    tx, _ty, tz = BRIDGE_TARGET
    t = ray(tris, (tx, 10.0, tz), (0.0, -1.0, 0.0))
    assert t is not None, 'no bridge roof over PreviewTarget_Bridge'
    roof = 10.0 - t
    eye_y = roof - EYE_DOWN / scale
    faces = [ray(tris, (tx + 20.0, y, tz), (-1.0, 0.0, 0.0)) for y in np.linspace(eye_y - EYE_BAND / scale, eye_y, 11)]
    assert all(f is not None for f in faces), 'no bridge face'
    face = tx + 20.0 - min(faces)
    assert tx < face < tx + 2.0, f'the bridge face at {face}, not ahead of the bridge ({tx})'
    t = ray(tris, (BRIDGE_STAND, 10.0, tz), (0.0, -1.0, 0.0))
    assert t is not None and abs(10.0 - t - roof) < 0.02, f'the bridge roof at {BRIDGE_STAND}: {t}, not {roof}'
    out = {'bridge': {'parent': SCALE_FROM, 'pivot': to_game(np.array([[BRIDGE_STAND, 10.0 - t, tz]]), scale)[0]
                      + np.array([0.0, STAND_UP, 0.0])},
           'vc_eye_0': {'parent': SCALE_FROM,
                        'pivot': to_game(np.array([[face, eye_y, tz]]), scale)[0] + np.array([0.0, 0.0, EYE_OUT])}}
    for n in range(len([b for b in bones if b.startswith('turret_') and b.endswith('_gun')])):
        gun, base = extent[f'turret_{n}_gun'], extent[f'turret_{n}']
        pivot = bones[f'turret_{n}_gun']['pivot']
        top = max(gun[1][1], base[1][1])
        out[f'vc_eye_{1 + n}'] = {'parent': f'turret_{n}_gun',
                                  'pivot': np.array([pivot[0], top + TURRET_EYE_UP, gun[0][2] - TURRET_EYE_BACK])}
    return {b: {'parent': e['parent'], 'pivot': [round(float(x), 4) for x in e['pivot']]} for b, e in out.items()}


def _rounded(markers: dict[str, dict]) -> dict[str, dict]:
    return {b: {**e, 'pivot': [round(float(x), 4) for x in e['pivot']]} for b, e in markers.items()}


def bay_parts(export: str) -> tuple[Part, ...]:
    """Every bay's fighter (bay_<i>): its kind's model docked on its bay's raised lift (its lowest point on the lift's
    top, its origin over the bay's DroneSocket), its nose out of the bay."""
    def glb(rel: str) -> Mesh:
        return read_glb(os.path.join(export, *GENERIC.split('/'), *rel.split('/')))
    hangar = glb(HANGAR_GLB)
    out = []
    for m, (_name, at, face) in enumerate(HANGAR_SOCKETS):
        for k, (lift, _up, _low) in enumerate(BAY_SOURCE_BONES):
            i = 2 * m + k
            top = float(hangar.pos[hangar.bone == hangar.names.index(lift), 1].max())   # the lift's top (glb y)
            jet = glb(FIGHTER_GLBS[BAY_KINDS[i % len(BAY_KINDS)]])
            spot = turn(np.array([DRONE_SOCKETS[k]]), face)[0] + np.array(at)
            spot[1] = at[1] + top - float(jet.pos[:, 1].min())
            out.append(Part(FIGHTER_GLBS[BAY_KINDS[i % len(BAY_KINDS)]], tuple(float(x) for x in spot),
                            (('root', f'bay_{i}'),), face=tuple(float(x) for x in turn(np.array([BAY_FACE]), face)[0]),
                            paint=False))
    return tuple(out)


def ship_parts(hull: 'Mesh', export: str) -> tuple[Part, ...]:
    """The dreadnought: its hull, its hangars, its turrets, its bays' fighters."""
    return (MODELS['dreadnought'] + tuple(turret_part(n, k, top) for n, (k, top) in enumerate(turret_mounts(hull)))
            + bay_parts(export))


def frame_of(ahead) -> list[list[float]]:   # noqa: ANN001
    """Rows left, up, ahead (game space) of a level frame looking along `ahead`."""
    a = np.asarray(ahead, float)
    up = np.array([0.0, 1.0, 0.0])
    return [[round(float(x), 6) for x in r] for r in (np.cross(up, a), up, a)]


def _cells(tris: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """The ship's collision (pylib/aircraft_collision.py mesh_cells: what the game's player and jets meet): its cells'
    low and high corners, (n, 3) each, in the same frame as `tris` (the cells are the same in any frame shifted)."""
    import aircraft_collision
    cells = aircraft_collision.mesh_cells([tuple(map(tuple, t)) for t in tris], *aircraft_collision.DN_GRID)
    return np.array([c[0] for c in cells]), np.array([c[1] for c in cells])


def _hits(cells: tuple[np.ndarray, np.ndarray], lo: np.ndarray, hi: np.ndarray) -> np.ndarray:
    """The cells a box (lo, hi) overlaps (more than touches)."""
    c0, c1 = cells
    return np.all((c0 < hi - 1e-6) & (c1 > lo + 1e-6), axis=1)


BAY_SLIDE_STEP = 0.25   # m


def bay_markers(bones: dict, extent: dict, cells) -> dict[str, dict]:  # noqa: ANN001
    """bay_<i>: its fighter's box centre and frame (nose out); bay_<i>_out: straight outboard of it where the box,
    BAY_MARGIN m larger, overlaps no collision cell."""
    out = {}
    for i in range(BAYS):
        lo, hi = extent[f'bay_{i}']
        centre, half = (lo + hi) / 2.0, (hi - lo) / 2.0 + BAY_MARGIN
        ahead = np.array([1.0 if centre[0] > 0 else -1.0, 0.0, 0.0])   # outboard: port (+x) or starboard
        slide = 0.0
        while _hits(cells, centre + slide * ahead - half, centre + slide * ahead + half).any():
            slide += BAY_SLIDE_STEP
            assert slide < 200.0, f'bay {i}: no clear point outboard'
        rows = frame_of(ahead)
        out[f'bay_{i}'] = {'parent': SCALE_FROM, 'pivot': centre, 'frame': rows}
        out[f'bay_{i}_out'] = {'parent': SCALE_FROM, 'pivot': centre + slide * ahead, 'frame': rows}
    return out


# ------------------------------------------------------------------------------------------ the source's glb

CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


@dataclass
class Mesh:
    pos: np.ndarray      # (n, 3) source space
    nrm: np.ndarray      # (n, 3)
    uv: np.ndarray       # (n, 2) top-left origin
    bone: np.ndarray     # (n,) each vertex's heaviest joint, as a source bone name index into `names`
    tris: np.ndarray     # (m, 3)
    mat: np.ndarray      # (m,) into `materials`
    materials: list[str]
    names: list[str]     # source bone names
    pivots: dict[str, np.ndarray]


def read_glb(path: str) -> Mesh:
    """CUE4Parse's skinned glb: every primitive with its heaviest joint per vertex (the ships are rigid)."""
    with open(path, 'rb') as h:
        b = h.read()
    jl = struct.unpack_from('<I', b, 12)[0]
    g = json.loads(b[20:20 + jl])
    blob = b[20 + jl + 8:]

    def acc(i: int) -> np.ndarray:
        a = g['accessors'][i]
        v = g['bufferViews'][a['bufferView']]
        off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
        n = NC[a['type']]
        dt = np.dtype(CT[a['componentType']])
        stride = v.get('byteStride', dt.itemsize * n)
        raw = np.frombuffer(blob, np.uint8, stride * (a['count'] - 1) + dt.itemsize * n, off)
        out = np.lib.stride_tricks.as_strided(raw, (a['count'], dt.itemsize * n), (stride, 1)).copy().view(dt)
        return out.reshape(a['count'], n)

    skin = g['skins'][0] if g.get('skins') else None
    joints = skin['joints'] if skin else []
    names = [g['nodes'][j].get('name', f'joint{j}') for j in joints] or ['root']
    up = {c: i for i, nd in enumerate(g['nodes']) for c in nd.get('children', [])}

    def world(i: int) -> np.ndarray:   # a joint's place: its translations up its chain (the joints are unrotated)
        t = np.array(g['nodes'][i].get('translation', (0.0, 0.0, 0.0)), float)
        return t + world(up[i]) if i in up and up[i] in joints else t
    pivots = {g['nodes'][j].get('name', ''): world(j) for j in joints}
    P, N, U, B, T, M = [], [], [], [], [], []
    base = 0
    for mesh in g['meshes']:
        for pr in mesh['primitives']:
            at = pr['attributes']
            p = acc(at['POSITION']).astype(np.float64)
            P.append(p)
            N.append(acc(at['NORMAL']).astype(np.float64))
            U.append(acc(at['TEXCOORD_0']).astype(np.float64))
            if 'JOINTS_0' in at:
                j, w = acc(at['JOINTS_0']).astype(np.int64), acc(at['WEIGHTS_0']).astype(np.float64)
                B.append(j[np.arange(len(j)), w.argmax(1)])
            else:
                B.append(np.zeros(len(p), np.int64))
            f = acc(pr['indices']).astype(np.int64).reshape(-1, 3)
            T.append(f + base)
            M.append(np.full(len(f), pr.get('material', 0)))
            base += len(p)
    root = g['nodes'][joints[0]] if joints else {}
    if root.get('rotation') is not None:   # the root's rotation: identity up to its sign (w = +-1) in every ship
        x, y, z, w = root['rotation']
        assert abs(abs(w) - 1.0) < 1e-5, f'{path}: root rotation {root["rotation"]}'
    return Mesh(np.vstack(P), np.vstack(N), np.vstack(U), np.concatenate(B), np.vstack(T), np.concatenate(M),
                [m.get('name', f'm{i}') for i, m in enumerate(g.get('materials', []))], names, pivots)


def to_game(p: np.ndarray, scale: float) -> np.ndarray:
    """Source (+X bow, +Y up, +Z port) -> game (+Z bow, +Y up, +X port): a turn about Y, no mirror."""
    return np.stack([-p[:, 2], p[:, 1], p[:, 0]], axis=1) * scale


# ------------------------------------------------------------------------------------------ textures

def material_json(export: str, material: str) -> dict:
    hits = [os.path.join(r, f) for r, _, fs in os.walk(export) for f in fs if f.lower() == material.lower() + '.json']
    if not hits:
        return {}
    with open(hits[0], encoding='utf-8') as h:
        return json.load(h)


def chain(material: str) -> list[str]:
    """The material and its parents, the material first."""
    out = [material]
    while out[-1] in PARENTS:
        out.append(PARENTS[out[-1]])
    return out


def _linear(c: dict) -> tuple[float, float, float]:
    return (float(c['R']), float(c['G']), float(c['B']))


def asset_json(export: str, rel: str) -> dict:
    """The properties of an asset dnexport -json wrote (rel: under Generic, or a package's ObjectPath)."""
    rel = rel.split('.', 1)[0].removeprefix(GENERIC + '/')
    with open(os.path.join(export, *GENERIC.split('/'), *rel.split('/')) + '.json', encoding='utf-8') as h:
        return json.load(h)['Properties']


@dataclass
class Vanity:
    colours: dict[str, tuple[float, float, float]]
    scalars: dict[str, float]
    masks: dict[str, str]    # material -> its colour mask png (the pattern's)


def vanity(export: str, paint: str = PAINT) -> Vanity:
    """The paint's (PAINT) colour set and PATTERN's colour masks."""
    cs = asset_json(export, asset_json(export, paint)['m_colorSet']['ObjectPath'])
    masks = {}
    for mat, tex in zip(PATTERN_IDS, asset_json(export, PATTERN)['m_colorMaskTextures']):
        rel = tex['ObjectPath'].rsplit('.', 1)[0]
        path = os.path.join(export, *rel.split('/')) + '.png'
        assert os.path.isfile(path), f'{path}: export it (dnexport <pakdir> <export> {rel})'
        masks[mat] = path
    return Vanity({ours: _linear(cs[k]) for k, ours in COLOUR_SET.items() if k in cs},
                  {ours: float(cs[k]) for k, ours in COLOUR_SET_SCALARS.items() if k in cs}, masks)


def params(export: str, material: str, paint: Vanity | None = None
           ) -> tuple[dict[str, tuple[float, float, float]], dict[str, float]]:
    """The material's colours (linear) and scalars: its parents', its own over them, the paint's over all."""
    colours: dict[str, tuple[float, float, float]] = {}
    scalars: dict[str, float] = {}
    for m in reversed(chain(material)):
        d = material_json(export, m)
        for k, v in d.get('Colors', {}).items():
            colours[k] = _linear(v)
        scalars.update(d.get('Scalars', {}))
    if paint is not None:
        colours.update(paint.colours)
        scalars.update(paint.scalars)
    return colours, scalars


def find_texture(export: str, material: str, slot: tuple[str, ...]) -> str | None:
    """The texture file the material (else its nearest parent) names for the first of `slot`'s parameters it has."""
    for m in chain(material):
        tex = material_json(export, m).get('Textures', {})
        for key in slot:
            if key in tex:
                rel = tex[key]['ObjectPath'].rsplit('.', 1)[0] + '.png'
                path = os.path.join(export, *rel.split('/'))
                if os.path.isfile(path):
                    return path
    return None


def _img(path: str | None, fill: tuple[int, ...], mode: str = 'RGBA') -> np.ndarray:
    if path is None:
        return np.broadcast_to(np.array(fill, np.float64) / 255.0, (TEX, TEX, len(fill))).copy()
    im = Image.open(path).convert(mode)
    # each channel alone: PIL resizes RGBA premultiplied, which zeroes the colour where alpha (here a mask) is 0
    chans = [c.resize((TEX, TEX), Image.LANCZOS) for c in im.split()]
    return np.stack([np.asarray(c, np.float64) for c in chans], axis=-1) / 255.0


def to_linear(c: np.ndarray) -> np.ndarray:
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def to_srgb(c: np.ndarray) -> np.ndarray:
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def bake(export: str, material: str, paint: Vanity | None = None) -> dict[str, Image.Image]:
    """The material's three textures (see the module doc); `paint`: the ship's vanity, on the ship's materials."""
    d = _img(find_texture(export, material, ('Albedo', 'PM_Diffuse', 'Diffuse')), (188, 188, 188, 255))
    mask = paint.masks.get(material) if paint is not None else None
    mc = _img(mask or find_texture(export, material, ('colorMask', 'PM_SpecularMasks')), (0, 0, 0, 255))
    ao = _img(find_texture(export, material, ('AO',)), (255, 255, 255, 0))
    nm = _img(find_texture(export, material, ('Normal', 'PM_Normals')), (128, 128, 255, 255))
    colours, scalars = params(export, material, paint)
    base = colours.get('BaseColor', colours.get('DetailSurfaceColor01', DEFAULT_BASE))
    lin = to_linear(d[..., :3])
    detail = np.clip(lin / max(float(np.median(lin.mean(-1))), 1e-3), 0.0, DETAIL_MAX)
    detail = 1.0 + d[..., 3:4] * (detail - 1.0)   # its alpha: how much (the "empty" CH_DN_Empty_D: white, alpha 0)
    rgb = np.broadcast_to(np.array(base), d[..., :3].shape)
    metal = np.full(d.shape[:2], scalars.get('BaseMetalic', 0.8))
    rough = np.full(d.shape[:2], scalars.get('BaseRoughness01', 0.5))
    for k, ch in enumerate(('Red', 'Green', 'Blue')):
        w = mc[..., k]
        if f'CustomColor0{k + 1}' in colours:
            rgb = rgb + w[..., None] * (np.array(colours[f'CustomColor0{k + 1}']) - rgb)
        # a colour set gives metal for its base alone (m_baseMetalic): its paints over the masks are coats, not metal
        # (at 0.5 the hull read as chrome under the game's sky: range 2026-10-21)
        metal = metal + w * (scalars.get(f'{ch}Metalic', 0.0) - metal)
        rough = rough + w * (scalars.get(f'{ch}Roughness01', 0.5) - rough)
    ao_w = scalars.get('AoInfluence', AO_DEFAULT)
    rgb = to_srgb(rgb * detail * (1.0 - ao_w * (1.0 - ao[..., 1:2])))
    light = ao[..., 3] if ao[..., 3].mean() < 0.5 else np.zeros(d.shape[:2])   # the fighters' AO alpha is all 1: no lights
    if paint is not None:
        # the ship's bare metal (its colour set's m_baseMetalic 1) under the game's bright sky: a mirror of it, the hull
        # white and the paint lost (range 2026-10-21; the original flies in space, its sky dark): half of it
        metal = metal * SHIP_METAL
    rml = np.stack([np.clip(rough, 0, 1), np.clip(metal, 0, 1), light], axis=-1)

    def png(a: np.ndarray) -> Image.Image:
        return Image.fromarray(np.clip(np.round(a * 255.0), 0, 255).astype(np.uint8), 'RGB')
    return {'df': png(rgb), 'nm': png(nm[..., :3]), 'rml': png(rml)}


# ------------------------------------------------------------------------------------------ write

def write_model(name: str, parts: tuple[Part, ...], export: str, scale: float, out: str, done: set[str],
                paint: Vanity | None = None) -> dict:
    """<name>.obj / .mtl and its materials' textures; its manifest entry."""
    obj = [f'# {name}: tools/prep_dreadnought.py\n', f'mtllib {name}.mtl\n']
    mtl: list[str] = []
    bones: dict[str, dict] = {name: {'parent': '', 'pivot': [0.0, 0.0, 0.0]}}
    vi = 1
    lo, hi = np.full(3, 1e9), np.full(3, -1e9)
    extent: dict[str, tuple[np.ndarray, np.ndarray]] = {}   # each bone's geometry's box
    materials: list[str] = []
    painted: set[str] = set()
    tris_out: list[np.ndarray] = []   # every triangle (game space)
    for part in parts:
        m = read_glb(os.path.join(export, *GENERIC.split('/'), *part.glb.split('/')))
        at = np.array(part.at)
        rename = dict(part.bones)
        first = next((o for _, o in part.bones), '') or name
        parents = {o or name: (p or name) for o, p in part.parents}
        tips: dict[str, list[np.ndarray]] = {}
        for _src, ours in part.bones:   # every bone the part names, with its pivot (the first of its sources')
            bone = ours or name
            if bone in bones:
                continue
            src = next(s for s, o in part.bones if (o or name) == bone)
            pivot = to_game(turn((m.pivots.get(src, np.zeros(3)) * part.scale)[None], part.face) + at, scale)[0]
            bones[bone] = {'parent': parents.get(bone, name if bone == first else first),
                           'pivot': [round(float(x), 4) for x in pivot]}
        for src in sorted(set(m.bone.tolist())):
            sname = m.names[src] if src < len(m.names) else 'root'
            bone = rename.get(sname, rename.get('root', '')) or name
            assert bone in bones, f'{part.glb}: bone {sname} -> {bone} not named'
            for mi, mat in enumerate(m.materials):
                sel = (m.bone[m.tris[:, 0]] == src) & (m.mat == mi)
                if not sel.any():
                    continue
                tris = m.tris[sel]
                used = np.unique(tris)
                remap = np.zeros(len(m.pos), np.int64)
                remap[used] = np.arange(len(used)) + vi
                p = to_game(turn(m.pos[used] * part.scale, part.face) + at, scale)
                if bone == part.muzzle:
                    tips.setdefault(bone, []).append(p)
                n = to_game(turn(m.nrm[used], part.face), 1.0)
                tris_out.append(p[np.searchsorted(used, tris)])
                lo, hi = np.minimum(lo, p.min(0)), np.maximum(hi, p.max(0))
                was = extent.get(bone, (p.min(0), p.max(0)))
                extent[bone] = (np.minimum(was[0], p.min(0)), np.maximum(was[1], p.max(0)))
                obj.append(f'o {bone}\nusemtl {mat}\n')
                obj += [f'v {a:.4f} {b:.4f} {c:.4f}\n' for a, b, c in p]
                obj += [f'vn {a:.4f} {b:.4f} {c:.4f}\n' for a, b, c in n]
                obj += [f'vt {u:.6f} {1.0 - v:.6f}\n' for u, v in m.uv[used]]   # OBJ: bottom-left origin
                obj += [f'f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n' for a, b, c in remap[tris]]
                vi += len(used)
                if mat not in materials:
                    materials.append(mat)
                if part.paint:
                    painted.add(mat)
        for gun, pts in tips.items():   # its muzzle: the middle of its guns' tips (their farthest forward, +z)
            q = np.vstack(pts)
            tip = q[q[:, 2] > q[:, 2].max() - MUZZLE_DEPTH * scale]
            at_tip = [(tip[:, 0].min() + tip[:, 0].max()) / 2, (tip[:, 1].min() + tip[:, 1].max()) / 2, float(q[:, 2].max())]
            bones[gun.removesuffix('_gun') + '_muzzle'] = {'parent': gun, 'pivot': [round(float(x), 4) for x in at_tip]}
    for mat in materials:
        mtl.append(f'newmtl {mat}\nKd 1 1 1\nmap_Kd {mat}_df.png\nnorm {mat}_nm.png\nmap_Pr {mat}_rml.png\n')
        if mat not in done:
            for k, im in bake(export, mat, paint if mat in painted else None).items():
                im.save(os.path.join(out, f'{mat}_{k}.png'))
            done.add(mat)
    with open(os.path.join(out, f'{name}.obj'), 'w', encoding='utf-8', newline='\n') as h:
        h.writelines(obj)
    with open(os.path.join(out, f'{name}.mtl'), 'w', encoding='utf-8', newline='\n') as h:
        h.writelines(mtl)
    return {'bones': bones, 'min': [round(float(x), 3) for x in lo], 'max': [round(float(x), 3) for x in hi],
            'materials': materials, 'extent': extent, 'tris': np.concatenate(tris_out)}


# The model folder's record of where its assets came from (as models/sazabi/LICENSE.txt and models/cockpits/CREDITS_*.txt
# record theirs): it goes with the folder into the release (tools/build_release.py RELEASE_MODELS).
SOURCE = """Dreadnought model used by EDF6VehicleCrew (free, non-commercial mod).

Source: the game "Dreadnought" (Yager Development / Six Foot, 2017-2023), extracted from its game files with CUE4Parse:
  DreadGame/Content/Generic/Ships/Dreadnought/Heavy (the Jupiter Arms heavy dreadnought, its default paint),
  DreadGame/Content/Generic/Abilities/Dreadnought/Hangar (the "Fighters" hangar module, four of them),
  DreadGame/Content/Generic/Weapons/Dreadnought/SecShort, SecMid, SecLong (the secondary turrets),
  DreadGame/Content/Generic/Ships/RespawnJet (fighter, defender, bomber, interceptor).
Changes: scaled to {length:.0f} m bow to stern, split into rigid parts per bone, its paint baked into the colour textures,
converted to the game's model format; marker bones and collision added by the mod.
The assets remain the property of their owners and carry no open licence; they ship with the mod by the decision of its
repository owner. This is an unofficial fan work, not affiliated with or endorsed by the game's developer or publisher.
Made by tools/prep_dreadnought.py.
"""


def write_source(out: str) -> None:
    """The folder's SOURCE.txt (SOURCE)."""
    with open(os.path.join(out, 'SOURCE.txt'), 'w', encoding='utf-8', newline='\n') as h:
        h.write(SOURCE.format(length=SHIP_LENGTH))


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('export', nargs='?', help="the folder CUE4Parse exported the game's packages into")
    ap.add_argument('--out', default=os.path.join(obj_model.DEV_MODELS, 'dreadnought'))
    ap.add_argument('--paint', default=PAINT, help='another paint (VanityItems/..._DA, exported) to compare')
    ap.add_argument('--source-only', action='store_true', help="only (re)write the folder's SOURCE.txt")
    a = ap.parse_args(argv)
    os.makedirs(a.out, exist_ok=True)
    if a.source_only:
        write_source(a.out)
        return 0
    if not a.export:
        ap.error('the export folder is required')
    ship = read_glb(os.path.join(a.export, *GENERIC.split('/'), *MODELS[SCALE_FROM][0].glb.split('/')))
    scale = SHIP_LENGTH / float(np.ptp(ship.pos[:, 0]))
    manifest = {'scale': scale, 'models': {}}
    done: set[str] = set()
    paint = vanity(a.export, a.paint)
    for name, parts in MODELS.items():
        if name == SCALE_FROM:
            parts = ship_parts(ship, a.export)
        manifest['models'][name] = e = write_model(name, parts, a.export, scale, a.out, done,
                                                   paint if name == PAINTED else None)
        extent, tris = e.pop('extent'), e.pop('tris')
        if name == SCALE_FROM:
            e['bones'].update(ship_markers(ship, e['bones'], extent, scale))
            cells = _cells(tris)
            e['bones'].update(_rounded(bay_markers(e['bones'], extent, cells)))
        print(name, 'size', [round(h - l, 1) for l, h in zip(e['min'], e['max'])], 'bones', list(e['bones']))
    with open(os.path.join(a.out, 'dreadnought.json'), 'w', encoding='utf-8', newline='\n') as h:
        json.dump(manifest, h, indent=1)
    write_source(a.out)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
