"""Developer-only: the War Thunder M163 and BMPT (Sketchfab uploads by KojfDiscord, published as CC BY 4.0: their
LICENSE.txt) into the model folders the installer builds the two tracked vehicles from (pylib/wt_tracked.py):

    <models>/wt_m163/, <models>/wt_bmpt/
        model.npz          per part (one bone, one material): positions (game space, metres), smoothed normals, uvs
                           (top-left origin), triangles; and per collision proxy the triangles it is fitted to
        model.json         the parts (bone, material), the materials (stock template, texture files), the bones' new
                           origins, the visual rotor, the muzzles and case ports (model space), the collision groups
        textures/          the source's albedo and normal JPEGs as they are, and an occlusion PNG per material
        LICENSE.txt        the attribution the licence asks for

The source is the Sketchfab "source" download's OBJ (its own zip inside the zip: M163.obj / BMPT.obj with their MTL and
textures/), read from wherever it was unpacked:

    python tools/prep_wt_tracked.py m163 <unpacked>/M163.obj [--out <models>/wt_m163]
    python tools/prep_wt_tracked.py bmpt <unpacked>/BMPT.obj [--out <models>/wt_bmpt]

It needs numpy, Pillow and scipy (the installer only reads the result). The source is treated as data: nothing in it runs.

What the source is (measured 2026-10-21, docs/wt-tracked-models.md): War Thunder's own node names survive as OBJ
objects (`vehicle#bone_turret_59_0`: node, then the exporter's object and material numbers), one material each; +Y up,
+Z forward, +X the vehicle's left (`wheel_l_*` are at +x): the game's axes, metres, the track's ground at y = 0. Its
texture coordinates are the engine's with v negated (v' = -v: top-left origin). It has no normals: they are smoothed
here within AUTO_SMOOTH. Its winding is counter-clockwise seen from outside like the game's (checked by the hull's
signed volume; a source that is not gets every triangle turned).

Bones (the V603 skeleton the native Vehicle603_Flak drives; pylib/wt_tracked.py moves their origins here):
    body / cannon_main / cannon_l, cannon_r (elevation) / cannon_slide_l, _r (recoil) / doppler_radar, tracking_radar
    (two parts riding on the turret: the private CAS stops their radar spin) / tire_moveA..H_l, _r (the road wheels
    and the end wheels) / catapi_body (the tracks; the SGO scrolls their material's texture)
Which source node goes on which bone is RULES (by node name), the pivots that cannot be read off a node's box (the
turret ring, the gun's trunnion) are PIVOTS, measured off side / top renders and a circle fit of the turret's base.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import shutil
import sys
from dataclasses import dataclass, field

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))

AUTO_SMOOTH = 50.0              # degrees: faces meeting sharper than this keep a hard edge (armour plates)
OCC_FLOOR = 110                 # the occlusion map's black is this (stock body maps: occlusion mean ~198 of 255)
MIN_HULL = 0.06                 # m: a collision piece smaller than this across is left out (bolts, handles)
MAX_HULLS = 48                  # collision pieces a proxy keeps (the largest; a compressed mesh holds at most 127)
IDLER_GAP = 0.04                # m: between the split idler's halves' collision (siblings collide)
FORMAT = 1                      # model.json 'format': pylib/wt_tracked.py refuses another


@dataclass
class Vehicle:
    kind: str                   # make_tracked_combat kind
    folder: str                 # model folder name (obj_model.model_dir)
    title: str
    source_url: str
    rules: list[tuple[str, str]]          # (node regex, bone), first match wins; no match: body
    turret: tuple[float, float, float]    # cannon_main's origin: the ring's centre (x, z) at the hull roof (y)
    trunnion: dict[str, tuple[float, float]]   # elevation bone -> (y, z) of its axis (x: the barrel's)
    barrels: dict[str, str]               # weapon side ('left' / 'right') -> the node of its barrel
    rotor: str | None = None              # a visual rotor bone under cannon_slide_l, spinning the barrel node
    missiles: str | None = None           # node whose instances are the missile tubes (on missile_bone)
    # The bone the tubes ride and the missiles' holder (make_tracked_combat: the muzzle contract's bone): the round
    # leaves along that bone's forward row (muzzle mode 0, 0x633DD0). cannon_main only turns; launchers on the gun's
    # elevating bone leave along the gun's line.
    missile_bone: str = 'cannon_main'
    split_idler: str | None = None        # the wheel bone whose outer half goes on tire_moveH (M113: no H station)
    collision: dict[str, list[str]] = field(default_factory=dict)   # proxy -> node regexes (default: by bone)
    # proxy -> (axis, 'min'/'max', value), or a list of them (a proxy cut on two axes: X-Morph's turret-top slices)
    clip: dict[str, tuple[int, str, float] | list[tuple[int, str, float]]] = field(default_factory=dict)
    hull: str = 'x_root'                  # the hull's node (winding_flipped measures its signed volume)
    missiles_keep: int = 0                # missile tubes the launcher's muzzles take (the outermost, half a side); 0: all
    # The turret's room (min, max corners): a node no rule names whose box lies inside it rides the turret (its armour,
    # hatches and fittings, too many to name), not the hull.
    turret_box: tuple[tuple[float, float, float], tuple[float, float, float]] | None = None
    # Each side's muzzle (model space) where it is not the front of its barrel node's box on the box's axis: a source
    # whose guns are not nodes of their own (tools/prep_xmorph.py: X-Morph's muzzle attachments). The barrel node still
    # gives the breech (its box's rear: the case port).
    mouths: dict[str, tuple[float, float, float]] | None = None
    # A store slot's muzzles (make_tracked_combat slot: a weapon besides the gun pair, firing from them) where no node
    # of its own gives them: slot -> (bone, [model-space points]). The PzH 2000's roof MG3: its barrel's front face.
    stores: dict[str, tuple[str, list[tuple[float, float, float]]]] = field(default_factory=dict)


M163 = Vehicle(
    kind='vulcan', folder='wt_m163', title='M163 VADS (War Thunder)',
    source_url='https://sketchfab.com/3d-models/m163-war-thunder-4d6f3359f84e47caa49d717b91d4270a',
    rules=[
        (r'^wheel_([lr])_drive$', 'tire_moveA_{0}'),
        (r'^wheel_([lr])_0([1-5])$', 'tire_move{B}_{0}'),
        (r'^wheel_([lr])_back$', 'tire_moveG_{0}'),
        (r'^track_([lr])$', 'catapi_body'),
        (r'^gun_barrel$', 'edf6vc_m61_rotor'),
        # The M61's cradle, receiver and feed chutes elevate with it (cannon_r: the native right hinge, so the
        # barrels' and the cradle's physics bodies are two siblings, apart along the bore: tracked_collision's sweep)
        (r'^(bone_gun|bone_roll_.*|gun_mask_03_anim)$', 'cannon_r'),
        (r'^antenna_target_tagging$', 'doppler_radar'),          # the range-only radar's dish
        (r'^antenna_target_tagging_mount$', 'tracking_radar'),   # its arm
        (r'^(bone_turret|ex_decor_turret_.*|gun_mask_04|ammo|antenna_0[12])$', 'cannon_main'),
    ],
    turret=(0.0, 1.87, -0.27),       # ring circle fit at y 1.85..2.35: centre z -0.23..-0.32, x ~0 (symmetric)
    trunnion={'cannon_l': (2.31, 0.05), 'cannon_r': (2.31, 0.05)},   # the barrels' axis where it leaves the turret
    barrels={'left': 'gun_barrel', 'right': 'gun_barrel'},
    rotor='edf6vc_m61_rotor', split_idler='tire_moveG',
    collision={'cannon_main': [r'^(bone_turret|ex_decor_turret_.*|gun_mask_04|ammo)$']},   # not the whip antennas
    # Siblings collide (D93250): the barrels' body from 0.75 m, the cradle's to 0.55 m (tracked_collision._sweep: a
    # 0.20 m gap through 0.15 m of recoil and a 1 cm skin on each).
    clip={'cannon_l': (2, 'min', 0.75), 'cannon_r': (2, 'max', 0.55)},
)

# A rule's bone DROP leaves the node out (a variant's parts exported over the base model's: the 2S6M1's radar and sights).
DROP = 'drop'

BMPT = Vehicle(
    kind='bmpt', folder='wt_bmpt', title='BMPT "Terminator" (War Thunder)',
    source_url='https://sketchfab.com/3d-models/bmpt-war-thunder-line-of-contact-3f2e54b873a14b45b99007b7cefdf2c8',
    rules=[
        (r'^wheel_([lr])_front$', 'tire_moveA_{0}'),             # T-72: the idler is in front
        (r'^wheel_([lr])_0([1-6])$', 'tire_move{B}_{0}'),
        (r'^wheel_([lr])_drive$', 'tire_moveH_{0}'),             # and the sprocket at the back
        (r'^track_([lr])$', 'catapi_body'),
        (r'^gun_barrel_02$', 'cannon_slide_l'),                  # +x: the left 2A42
        (r'^gun_barrel_01$', 'cannon_slide_r'),
        # The weapon module (both 2A42s' cradle and the coaxial PKTM) elevates about its trunnion on cannon_l, and with
        # it the Ataka launchers (the four tubes and their brackets gun_mask_03 / 04, War Thunder's elevating mantlet
        # nodes), as on the real Terminator: on the turret the missiles left level with the hull whatever the gun's
        # elevation, into the ground on a downslope (the user, 2026-10-25: 「bmpt的飞弹怎么会往地里射？」).
        (r'^(bone_gun|ex_decor_01|bone_mg_gun_twin|gun_mask_0[34]|9m120_rocket)$', 'cannon_l'),
        (r'^bone_commander_sight_h$', 'doppler_radar'),          # the commander's panoramic sight
        (r'^hatch_04$', 'tracking_radar'),                       # the gunner's hatch and sight block
        # bone_turret_01..04 are the hull's grenade launchers (outside the ring): body
        (r'^(bone_turret|ex_armor_turret_.*|ex_era_turret_.*|hatch_03|antenna)$', 'cannon_main'),
    ],
    turret=(0.0, 1.70, -0.25),       # ring circle fit at y 1.50..1.75: centre z -0.20..-0.30, r 1.17..1.22
    trunnion={'cannon_l': (2.44, -1.47), 'cannon_r': (2.44, -1.47)},  # the module's side bearing (side render)
    barrels={'left': 'gun_barrel_02', 'right': 'gun_barrel_01'},
    missiles='9m120_rocket', missile_bone='cannon_l',
    # The module's collision is the turret's (cannon_main): on cannon_l it would wrap the right barrel's body, its
    # sibling (D93250 lets siblings collide). Each barrel's body is its barrel.
    collision={'cannon_l': [r'^gun_barrel_02$'], 'cannon_r': [r'^gun_barrel_01$'],
               'cannon_main': [r'^(bone_turret|ex_armor_turret_.*|ex_era_turret_.*|hatch_03|gun_mask_0[34]|'
                               r'9m120_rocket|bone_gun|ex_decor_01|bone_mg_gun_twin)$']},
)

# The 2S6 Tunguska (2026-10-24: the user's 「各种现实的炮台、近防炮……出名、独特的载具」: guns and missiles in one mount, what
# the M163 has not): two twin 30 mm 2A38M and eight 9M311 missiles beside them, the T-72-like running gear (the idler
# in front, six road wheels, the sprocket at the back: the BMPT's stations). The export carries the 2S6M1's search
# radar, sight and turret fittings over the 2S6's own (the m1_turret texture): those are dropped.
TUNGUSKA = Vehicle(
    kind='tunguska', folder='wt_tunguska', title='2S6 Tunguska (War Thunder)',
    source_url='https://sketchfab.com/3d-models/2s6-tunguska-war-thunder-dd5c6c0b5497401daf222c57a22f6c23',
    rules=[
        (r'^(antenna_target_location_01|antenna_target_location_mount_01|optic_turret_06|turret_add_01)$', DROP),
        (r'^wheel_([lr])_front$', 'tire_moveA_{0}'),
        (r'^wheel_([lr])_0([1-6])$', 'tire_move{B}_{0}'),
        (r'^wheel_([lr])_drive$', 'tire_moveH_{0}'),
        (r'^track_([lr])$', 'catapi_body'),
        (r'^gun_barrel_0[25]$', 'cannon_slide_l'),               # +x: the left 2A38M's two barrels
        (r'^gun_barrel_0[16]$', 'cannon_slide_r'),
        (r'^gun_02$', 'cannon_l'),                               # each gun's housing elevates on its own side
        (r'^gun_01$', 'cannon_r'),
        (r'^antenna_target_location$', 'doppler_radar'),         # the search radar on the turret's back
        (r'^antenna_target_tagging$', 'tracking_radar'),         # the tracking radar in front
        (r'^(bone_turret|ex_armor_[lr]_.*|hatch_(0[789]|10)|ex_lantern_01|ex_mount_lantern|optic_turret_05|roll_.*|'
         r'turret_add|antenna_0[12]|antenna_target_.*_mount|bone_commander_sight_h|9m311_rocket)$', 'cannon_main'),
    ],
    turret=(0.0, 1.88, 0.90),        # ring circle fit at y 1.85..2.0: centre z ~0.94, r ~1.19; the hull roof 1.88
    trunnion={'cannon_l': (2.59, -0.15), 'cannon_r': (2.59, -0.15)},   # the barrels' axis through the housings' middle
    barrels={'left': 'gun_barrel_02', 'right': 'gun_barrel_01'},
    missiles='9m311_rocket', missiles_keep=4, hull='root',
    collision={'cannon_l': [r'^(gun_02|gun_barrel_0[25])$'], 'cannon_r': [r'^(gun_01|gun_barrel_0[16])$']},
)

# The PzH 2000 (2026-10-24: an upgrade of the self-propelled howitzer, 「升级版（定位相近但是数值高）」: its turret turns,
# the plugin's twin-gun one's does not): one 155 mm L52 (both native holders on its barrel, in turn), seven road wheels
# a side, the sprocket in front and the idler behind: A the sprocket, B..G road wheels 1..6, H the idler; the seventh
# road wheel stays on the hull (the V603 has eight stations). The turret (far back) takes every fitting over the roof.
PZH2000 = Vehicle(
    kind='pzh2000', folder='wt_pzh2000', title='PzH 2000 (War Thunder)',
    source_url='https://sketchfab.com/3d-models/pzh-2000-war-thunder-remodel-3391b53c325c45f6a9aa769ba8c511ab',
    rules=[
        (r'^wheel_([lr])_drive$', 'tire_moveA_{0}'),
        (r'^wheel_([lr])_0([1-6])$', 'tire_move{B}_{0}'),
        (r'^wheel_([lr])_back$', 'tire_moveH_{0}'),
        (r'^track_([lr])$', 'catapi_body'),
        (r'^gun_barrel$', 'cannon_slide_l'),
        # The mantlet's fittings elevate with it (2026-10-25, docs/feedback-2026-10-25-model-rigs.md: 「炮管的防尘布转炮管的
        # 时候留在原地了」): the dust cover round the barrel ahead of the mantlet (ex_decor_04, outside turret_box: it
        # was left on the hull), the shroud on the mantlet's face (ex_decor_05) and the plate on its top (ex_armor_05).
        (r'^(bone_gun|ex_decor_04|ex_decor_05|ex_armor_05)$', 'cannon_l'),
        (r'^bone_commander_sight_h$', 'doppler_radar'),          # the commander's sight head
        (r'^mg_aa_mount_h$', 'tracking_radar'),                  # the roof machine gun's ring
        (r'^(bone_turret|bone_turret_01|antenna_0[12]|bone_mg_aa_v|mg_aa_mount_v|ammo)$', 'cannon_main'),
    ],
    turret=(0.0, 1.84, -2.30),       # ring circle fit at y 0.82: centre z -2.15, r 0.9 (the basket); outer at y 1.9: -2.45
    trunnion={'cannon_l': (2.45, -0.90), 'cannon_r': (2.45, -0.90)},   # the barrel's axis behind the mantlet
    barrels={'left': 'gun_barrel', 'right': 'gun_barrel'},
    turret_box=((-1.7, 1.95, -4.7), (1.7, 5.0, 0.6)),
    collision={'cannon_l': [r'^(gun_barrel|bone_gun)$'], 'cannon_r': [r'^bone_gun$']},
    # The roof MG3 on the loader's ring (the tester, 2026-10-28, #29: 「Pzh2000自行榴弹炮可以加个机枪或者机枪位」): the
    # front face of its barrel (wt_mg_mg_42_c's foremost vertices, x 1.05..1.09, y 3.36..3.40 at z -1.452), on the
    # turret (its ring's mount rides cannon_main): it turns with the turret and fires level along it.
    stores={'mg': ('cannon_main', [(1.0697, 3.3773, -1.4517)])},
)

# The Sturmtiger (WT: 38 cm Sturmmorser): no turret, a 380 mm rocket mortar in the casemate's ball mount, which the
# V603's turret hinge turns a little (make_tracked_combat TURRET_LIMITS: +-10 deg). The Tiger's interleaved road wheels:
# A the sprocket in front, B..G road wheels 1..6, H the idler behind; 7 and 8 stay on the hull.
STURMTIGER = Vehicle(
    kind='sturmtiger', folder='wt_sturmtiger', title='Sturmtiger (War Thunder: 38 cm Sturmmorser)',
    source_url='https://sketchfab.com/3d-models/38-cm-sturmmorser-war-thunder-31033223348b4d0982ed4ee27b4f5afd',
    rules=[
        (r'^wheel_([lr])_drive$', 'tire_moveA_{0}'),
        (r'^wheel_([lr])_0([1-6])$', 'tire_move{B}_{0}'),
        (r'^wheel_([lr])_front$', 'tire_moveH_{0}'),             # WT calls the rear idler "front"
        (r'^track_([lr])$', 'catapi_body'),
        (r'^(gun_barrel|380mm_spgr_4581)$', 'cannon_slide_l'),   # the mortar and the round in it
        (r'^bone_gun$', 'cannon_l'),
        # The roof's hatches (the commander's hatch_01, the loading hatch_02) stay on the hull: doppler_radar /
        # tracking_radar ride cannon_main, here the mortar's traverse, so they swung with the mortar (2026-10-25,
        # docs/feedback-2026-10-25-model-rigs.md: 「顶部的天花板怎么和炮管联动了」). The casemate has no fitting that turns:
        # the two radar bones carry no mesh, their origins and kinematic proxies still fitted to the hatches (collision).
    ],
    turret=(-0.285, 2.10, 1.0),      # the ball mount's centre: the mortar's axis (its box) behind the casemate's face
    trunnion={'cannon_l': (2.10, 1.10), 'cannon_r': (2.10, 1.10)},
    barrels={'left': 'gun_barrel', 'right': 'gun_barrel'},
    collision={'cannon_main': [r'^bone_gun$'], 'cannon_l': [r'^(gun_barrel|380mm_spgr_4581)$'], 'cannon_r': [r'^bone_gun$'],
               'doppler_radar': [r'^hatch_01$'], 'tracking_radar': [r'^hatch_02$']},
)

# The Apocalypse (Command & Conquer: Red Alert 2's, ska-ara's Sketchfab model; the user, 2026-10-24: 「再做个冷战那种
# 天启」): tools/glb_to_wt_obj.py names its pieces in War Thunder's way (its source is a .glb: apocalypse.obj there). Twin
# guns, a missile pod either side of the turret (its outermost tubes launch), two track units a side: A / H the front
# and rear end wheels, B..G six of its eight road wheels (glb_to_wt_obj.stations).
APOCALYPSE = Vehicle(
    kind='apocalypse', folder='wt_apocalypse', title='Apocalypse tank (Red Alert 2)',
    source_url='https://sketchfab.com/3d-models/red-alert-2-soviet-apocalypse-tank-v2-bfc38d9673b348c4aa258442d049b620',
    rules=[
        (r'^wheel_([lr])_([A-H])$', 'tire_move{1}_{0}'),
        (r'^track_([lr])$', 'catapi_body'),
        (r'^gun_barrel_l$', 'cannon_slide_l'),
        (r'^gun_barrel_r$', 'cannon_slide_r'),
        (r'^antenna_l$', 'doppler_radar'),
        (r'^antenna_r$', 'tracking_radar'),
        (r'^(turret|rocket|missile_pod)$', 'cannon_main'),
    ],
    turret=(0.0, 2.20, 1.10),        # ring circle fit at y 2.0..2.5: centre z 1.0..1.16, r 1.85..2.1; the hull roof 2.2
    trunnion={'cannon_l': (2.75, 2.50), 'cannon_r': (2.75, 2.50)},     # the barrels' axis inside the turret's face
    barrels={'left': 'gun_barrel_l', 'right': 'gun_barrel_r'},
    missiles='rocket', missiles_keep=4,
)

# The TOS-1 (lm9241221's low-poly Sketchfab model through tools/glb_to_wt_obj.py; the user, 2026-10-24: 「升级版」 of
# the Katyusha): a T-72 hull, its 30-tube thermobaric launcher pack on a turntable (both native holders on the pack, in
# turn); the idler in front (A), six road wheels, the sprocket behind (H).
TOS1 = Vehicle(
    kind='tos1', folder='wt_tos1', title='TOS-1 Buratino',
    source_url='https://sketchfab.com/3d-models/tos-1-buratino-low-poly-255fba6ba6c44b97aaea91d0225d0c75',
    rules=[
        (r'^wheel_([lr])_([A-H])$', 'tire_move{1}_{0}'),
        (r'^track_([lr])$', 'catapi_body'),
        (r'^launcher$', 'cannon_slide_l'),
        (r'^antenna_l$', 'doppler_radar'),
        (r'^antenna_r$', 'tracking_radar'),
        (r'^turret$', 'cannon_main'),
    ],
    turret=(0.0, 1.45, -0.98),       # the turntable's middle (its box: x -1.12..1.22, z -2.8..0.85), on the hull roof
    trunnion={'cannon_l': (1.60, -2.70), 'cannon_r': (1.60, -2.70)},   # the pack's back end (z -2.83), at its foot
    barrels={'left': 'launcher', 'right': 'launcher'},
    collision={'cannon_l': [r'^launcher$'], 'cannon_r': [r'^launcher$']},
)

VEHICLES = {'m163': M163, 'bmpt': BMPT, 'tunguska': TUNGUSKA, 'pzh2000': PZH2000, 'sturmtiger': STURMTIGER,
            'apocalypse': APOCALYPSE, 'tos1': TOS1}
LETTERS = 'ABCDEFGH'


# ------------------------------------------------------------------------------------------ the source

@dataclass
class Source:
    path: str
    positions: np.ndarray        # (n, 3)
    uvs: np.ndarray              # (m, 2)
    tri_v: np.ndarray            # (t, 3) position indices
    tri_t: np.ndarray            # (t, 3) uv indices
    tri_obj: np.ndarray          # (t,) index into objects
    tri_mat: np.ndarray          # (t,) index into materials
    objects: list[str]           # OBJ object names less the exporter's material number ('vehicle#wheel_l_01_72')
    materials: list[str]
    mtl: dict[str, dict[str, str]]   # material -> {'map_Kd': file, 'map_normal': file, 'map_ao': file, ...}
    normals: np.ndarray | None = None   # (n, 3) per position, the source's own (None: smoothed here, AUTO_SMOOTH)


def read_source(path: str) -> Source:
    """The OBJ (triangles only: the WT export is), its MTL's texture maps."""
    pos: list[list[str]] = []
    uv: list[list[str]] = []
    tv: list[tuple[int, int, int]] = []
    tt: list[tuple[int, int, int]] = []
    to: list[int] = []
    tm: list[int] = []
    objects: list[str] = []
    materials: list[str] = []
    obj_at: dict[str, int] = {}
    mat_at: dict[str, int] = {}
    cur_o, cur_m, mtllib = -1, -1, None
    with open(path, encoding='utf-8', errors='replace') as h:
        for line in h:
            if line.startswith('v '):
                pos.append(line.split()[1:4])
            elif line.startswith('vt '):
                uv.append(line.split()[1:3])
            elif line.startswith('f '):
                c = [x.split('/') for x in line.split()[1:]]
                if len(c) != 3 or any(len(x) < 2 or not x[1] for x in c):
                    raise ValueError(f'{path}: a face that is not a textured triangle: {line.strip()}')
                tv.append(tuple(int(x[0]) - 1 for x in c))      # type: ignore[arg-type]
                tt.append(tuple(int(x[1]) - 1 for x in c))      # type: ignore[arg-type]
                to.append(cur_o)
                tm.append(cur_m)
            elif line.startswith(('o ', 'g ')):
                name = re.sub(r'_\d+$', '', line[2:].strip())
                cur_o = obj_at.setdefault(name, len(obj_at))
                if cur_o == len(objects):
                    objects.append(name)
            elif line.startswith('usemtl '):
                name = line.split(None, 1)[1].strip()
                cur_m = mat_at.setdefault(name, len(mat_at))
                if cur_m == len(materials):
                    materials.append(name)
            elif line.startswith('mtllib '):
                mtllib = line.split(None, 1)[1].strip()
    if min(to) < 0 or min(tm) < 0:
        raise ValueError(f'{path}: faces before an object or a material')
    mtl: dict[str, dict[str, str]] = {}
    if mtllib:
        cur: dict[str, str] | None = None
        with open(os.path.join(os.path.dirname(path), mtllib), encoding='utf-8', errors='replace') as h:
            for line in h:
                t = line.strip().split(None, 1)
                if not t:
                    continue
                if t[0] == 'newmtl':
                    cur = mtl.setdefault(t[1].strip(), {})
                elif cur is not None and t[0].startswith('map_') and len(t) > 1:
                    cur[t[0]] = t[1].strip()
    return Source(path, np.asarray(pos, dtype=np.float64), np.asarray(uv, dtype=np.float64),
                  np.asarray(tv, dtype=np.int64), np.asarray(tt, dtype=np.int64), np.asarray(to, dtype=np.int64),
                  np.asarray(tm, dtype=np.int64), objects, materials, mtl)


def node(obj: str) -> str:
    """The War Thunder node of an OBJ object name: 'vehicle#wheel_l_01_72' -> 'wheel_l_01'."""
    return re.sub(r'_\d+$', '', obj.split('#', 1)[-1])


# ------------------------------------------------------------------------------------------ bones

def bone_of(v: Vehicle, name: str) -> str:
    for pattern, bone in v.rules:
        m = re.match(pattern, name)
        if m:
            g = m.groups()
            if '{B}' in bone:
                return bone.replace('{B}', LETTERS[int(g[1])]).format(*g)
            return bone.format(*g)
    return 'body'


def triangle_bones(v: Vehicle, src: Source) -> np.ndarray:
    """Each triangle's bone; the split idler's outer half (x beyond its middle) on tire_moveH."""
    by_obj = [bone_of(v, node(o)) for o in src.objects]
    if v.turret_box:
        lo, hi = (np.asarray(c) for c in v.turret_box)
        for k, o in enumerate(by_obj):
            if o != 'body' or any(re.match(p, node(src.objects[k])) for p, _b in v.rules):
                continue
            sel = src.tri_obj == k
            if sel.any():
                P = src.positions[np.unique(src.tri_v[sel])]
                if (P.min(0) >= lo).all() and (P.max(0) <= hi).all():
                    by_obj[k] = 'cannon_main'
    bones = np.asarray(by_obj, dtype=object)[src.tri_obj]
    if v.split_idler:
        for side, sign in (('l', 1.0), ('r', -1.0)):
            sel = bones == f'{v.split_idler}_{side}'
            if not sel.any():
                raise ValueError(f'{v.kind}: no {v.split_idler}_{side} wheel')
            P = src.positions[src.tri_v[sel]]
            mid = (P[..., 0].min() + P[..., 0].max()) / 2
            outer = np.where(sel)[0][P[..., 0].mean(axis=1) * sign > mid * sign]
            bones[outer] = f'tire_moveH_{side}'
    return bones


def drop(v: Vehicle, src: Source) -> None:
    """Leave out the triangles of every node a rule sends to DROP."""
    gone = np.asarray([bone_of(v, node(o)) == DROP for o in src.objects])[src.tri_obj]
    if gone.any():
        keep = ~gone
        src.tri_v, src.tri_t, src.tri_obj, src.tri_mat = (a[keep] for a in (src.tri_v, src.tri_t, src.tri_obj, src.tri_mat))


def box(points: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    return points.min(axis=0), points.max(axis=0)


def node_points(src: Source, pattern: str) -> np.ndarray:
    sel = np.asarray([bool(re.match(pattern, node(o))) for o in src.objects])[src.tri_obj]
    if not sel.any():
        raise ValueError(f'no node matches {pattern}')
    return src.positions[np.unique(src.tri_v[sel])]


def instances(src: Source, name: str) -> list[np.ndarray]:
    """Each OBJ object of node `name` (a node exported several times: the four Ataka tubes)."""
    out = []
    for k, o in enumerate(src.objects):
        if node(o) == name and (src.tri_obj == k).any():
            out.append(src.positions[np.unique(src.tri_v[src.tri_obj == k])])
    return out


# ------------------------------------------------------------------------------------------ geometry

def corner_normals(P: np.ndarray, tv: np.ndarray) -> np.ndarray:
    """(normals, (t, 3) corner -> normal row): around each position the area-weighted face normals, grouped greedily
    (a face joins the first group whose first face is within AUTO_SMOOTH of it). A zero-area face (the sources have
    some) seeds no group: it joins the position's first, or stands up (+y) where the position has no other face."""
    fn = np.cross(P[tv[:, 1]] - P[tv[:, 0]], P[tv[:, 2]] - P[tv[:, 0]])
    area = np.linalg.norm(fn, axis=1)
    unit = fn / np.maximum(area, 1e-20)[:, None]
    flat = area <= 1e-20
    cos_lim = math.cos(math.radians(AUTO_SMOOTH))
    corners = tv.reshape(-1)
    order = np.argsort(corners, kind='stable')
    starts = np.flatnonzero(np.r_[True, corners[order][1:] != corners[order][:-1]])
    ends = np.r_[starts[1:], len(order)]
    normal_of = np.empty(len(corners), dtype=np.int64)
    normals: list[np.ndarray] = []
    for s, e in zip(starts, ends):
        members = order[s:e]
        members = members[np.argsort(flat[members // 3], kind='stable')]   # the zero-area faces last
        faces = members // 3
        groups: list[tuple[np.ndarray, np.ndarray, list[int]]] = []    # (accumulated normal, seed unit, members)
        for c, f in zip(members, faces):
            if flat[f]:
                if not groups:
                    groups.append((np.array((0.0, 1.0, 0.0)), np.array((0.0, 1.0, 0.0)), []))
                groups[0][2].append(c)
                continue
            for acc, seed, mem in groups:
                if float(seed @ unit[f]) >= cos_lim:
                    acc += fn[f]
                    mem.append(c)
                    break
            else:
                groups.append((fn[f].copy(), unit[f], [c]))
        for acc, seed, mem in groups:
            n = np.linalg.norm(acc)
            normal_of[mem] = len(normals)
            normals.append(acc / n if n > 1e-20 else seed)
    return np.asarray(normals), normal_of.reshape(-1, 3)


def winding_flipped(src: Source, hull: str = 'x_root') -> bool:
    """Whether the triangles are clockwise seen from outside: the hull's (x_root) signed volume is negative."""
    sel = np.asarray([node(o) == hull for o in src.objects])[src.tri_obj]
    T = src.positions[src.tri_v[sel]]
    c = T.reshape(-1, 3).mean(axis=0)
    vol = np.einsum('ij,ij->i', T[:, 0] - c, np.cross(T[:, 1] - c, T[:, 2] - c)).sum() / 6
    return bool(vol < 0)


def collision_hulls(P: np.ndarray, T: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """What a proxy's collision is fitted to: the convex hull of each connected piece of its triangles (welded at
    1 mm), the MAX_HULLS largest, pieces under MIN_HULL across left out. The source meshes are game meshes (a hull of
    100,000 triangles, bolts and straps on it): model_pieces.fit on them takes many minutes in pure Python and gives
    no better contact; on hulls it only cuts them to the proxy's box."""
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
    from scipy.spatial import ConvexHull, QhullError
    used, inv = np.unique(T, return_inverse=True)
    inv = inv.reshape(-1, 3)
    key = np.round(P[used] / 1e-3).astype(np.int64)
    _k, weld = np.unique(key, axis=0, return_inverse=True)
    weld = weld.reshape(-1)
    a = np.concatenate((weld[inv[:, 0]], weld[inv[:, 1]]))
    b = np.concatenate((weld[inv[:, 1]], weld[inv[:, 2]]))
    n = int(weld.max()) + 1
    _count, label = connected_components(coo_matrix((np.ones(len(a)), (a, b)), shape=(n, n)), directed=False)
    piece = label[weld]                     # per used point
    hulls = []
    for lab in np.unique(piece):
        pts = P[used[piece == lab]]
        ext = pts.max(axis=0) - pts.min(axis=0)
        if len(pts) < 4 or ext.max() < MIN_HULL:
            continue
        try:
            h = ConvexHull(pts, qhull_options='QJ')
        except QhullError:
            continue
        hulls.append((float(np.prod(np.maximum(ext, 0.01))), pts[h.vertices], h))
    hulls.sort(key=lambda x: -x[0])
    out_p, out_t = [], []
    base = 0
    for _vol, _v, h in hulls[:MAX_HULLS]:
        idx, local = np.unique(h.simplices, return_inverse=True)
        out_p.append(h.points[idx])
        tri = local.reshape(-1, 3)
        c = h.points[idx].mean(axis=0)
        p = h.points[idx][tri]
        outward = np.einsum('ij,ij->i', np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0]), p.mean(axis=1) - c) > 0
        tri[~outward] = tri[~outward][:, ::-1]
        out_t.append(tri + base)
        base += len(idx)
    if not out_p:
        raise ValueError('no collision piece')
    return np.vstack(out_p), np.vstack(out_t)


# ------------------------------------------------------------------------------------------ materials

def texture_set(src: Source, material: str) -> tuple[str, str | None, str | None]:
    """(albedo, normal, occlusion) files of a source material, relative to the OBJ's folder. The BMPT's MTL leaves
    out its occlusion maps: `<stem>_ao_ao.jpg` beside the albedo `<stem>_c*.jpg` is taken when it exists."""
    m = src.mtl.get(material, {})
    albedo = m.get('map_Kd')
    if not albedo:
        raise ValueError(f'{material}: no map_Kd')
    folder = os.path.dirname(src.path)
    normal = m.get('map_normal')
    ao = m.get('map_ao')
    if ao is None:
        stem = re.sub(r'_c(_[a-z_0-9]+)?\.jpg$', '', albedo)
        guess = stem + '_ao_ao.jpg'
        ao = guess if os.path.isfile(os.path.join(folder, guess)) else None
    for f in (albedo, normal, ao):
        if f and not os.path.isfile(os.path.join(folder, f)):
            raise ValueError(f'{material}: {f} not found')
    return albedo, normal, ao


def occlusion_png(src_file: str, out_file: str) -> None:
    """The occlusion map rescaled so that its black is OCC_FLOOR (it multiplies the stock shader's lighting)."""
    from PIL import Image
    with Image.open(src_file) as im:
        g = np.asarray(im.convert('L'), dtype=np.float64)
    out = (OCC_FLOOR + g * (255 - OCC_FLOOR) / 255).round().astype(np.uint8)
    Image.fromarray(out, 'L').save(out_file, optimize=True)


# ------------------------------------------------------------------------------------------ build

def prepare(v: Vehicle, src: Source, out: str) -> dict:
    drop(v, src)
    if winding_flipped(src, v.hull):
        src.tri_v = src.tri_v[:, ::-1].copy()
        src.tri_t = src.tri_t[:, ::-1].copy()
        flipped = True
    else:
        flipped = False
    bones = triangle_bones(v, src)
    if src.normals is not None:     # the source's own: its normal maps were baked on them
        normals, normal_of = src.normals, src.tri_v
        # a corner whose own normal is degenerate (the X-Morph JAGUAR's mesh has zero ones) takes the faces' around it
        bad = np.linalg.norm(normals, axis=1)[normal_of] < 0.5
        if bad.any():
            computed, computed_of = corner_normals(src.positions, src.tri_v)
            normal_of = np.where(bad, computed_of.reshape(normal_of.shape) + len(normals), normal_of)
            normals = np.vstack((normals, computed))
    else:
        normals, normal_of = corner_normals(src.positions, src.tri_v)

    # materials: one per distinct texture set; the tracks under the native names the SGO scrolls
    tex_dir = os.path.join(out, 'textures')
    os.makedirs(tex_dir, exist_ok=True)
    folder = os.path.dirname(src.path)
    mat_specs: list[dict] = []
    mat_key: dict[tuple, int] = {}
    tri_material = np.empty(len(src.tri_v), dtype=np.int64)
    tri_node = np.asarray([node(o) for o in src.objects], dtype=object)[src.tri_obj]
    for k, m in enumerate(src.materials):
        sel = src.tri_mat == k
        if not sel.any():
            continue
        albedo, normal, ao = texture_set(src, m)
        for side in ('L', 'R', None):
            tsel = sel & (tri_node == f'track_{side.lower()}') if side else sel & ~np.isin(tri_node, ['track_l', 'track_r'])
            if not tsel.any():
                continue
            key = (albedo, normal, ao, side)
            if key not in mat_key:
                files = {}
                for kind, f in (('albedo', albedo), ('normal', normal)):
                    if f:
                        files[kind] = 'textures/' + os.path.basename(f)
                        shutil.copyfile(os.path.join(folder, f), os.path.join(out, files[kind]))
                if ao:
                    files['occlusion'] = 'textures/' + os.path.basename(ao).rsplit('.', 1)[0] + '_occ.png'
                    occlusion_png(os.path.join(folder, ao), os.path.join(out, files['occlusion']))
                stem = re.sub(r'\.[a-z]+$', '', os.path.basename(albedo))
                name = f'v603_kyata_{side}' if side else 'wt_' + stem
                if any(s['name'] == name for s in mat_specs):
                    name += f'_{len(mat_specs)}'
                mat_key[key] = len(mat_specs)
                mat_specs.append({'name': name, 'template': f'v603_kyata_{side}' if side else 'v603_body', **files})
            tri_material[tsel] = mat_key[key]

    # parts: one per (bone, material), wheels named as tracked_collision reads them
    arrays: dict[str, np.ndarray] = {}
    parts: list[dict] = []
    keys = sorted({(b, m) for b, m in zip(bones, tri_material)}, key=lambda x: (x[0], x[1]))
    for bone, mat in keys:
        sel = np.flatnonzero((bones == bone) & (tri_material == mat))
        name = part_name(v, bone, mat, mat_specs, [p['name'] for p in parts])
        tv, tt, tn = src.tri_v[sel], src.tri_t[sel], normal_of[sel]
        corner = np.stack((tv, tt, tn), axis=-1).reshape(-1, 3)
        uniq, inverse = np.unique(corner, axis=0, return_inverse=True)
        k = len(parts)
        arrays[f'p{k}_pos'] = src.positions[uniq[:, 0]].astype(np.float32)
        arrays[f'p{k}_nrm'] = normals[uniq[:, 2]].astype(np.float32)
        uv = src.uvs[uniq[:, 1]]
        arrays[f'p{k}_uv'] = np.column_stack((uv[:, 0], -uv[:, 1])).astype(np.float32)
        arrays[f'p{k}_tri'] = inverse.reshape(-1, 3).astype(np.uint32)
        parts.append({'name': name, 'bone': bone, 'material': int(mat), 'triangles': int(len(sel)),
                      'vertices': int(len(uniq))})

    # bone origins
    origins: dict[str, list[float]] = {'cannon_main': list(v.turret)}
    axes = {}
    for side, bone in (('left', 'l'), ('right', 'r')):
        P = node_points(src, '^' + re.escape(v.barrels[side]) + '$')
        lo, hi = box(P)
        axes[side] = ((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2], hi[2])
        if v.mouths:
            mx, my, mz = v.mouths[side]
            axes[side] = (mx, my, lo[2], mz)
    for side, b in (('left', 'l'), ('right', 'r')):
        x, y, z0, z1 = axes[side]
        ty, tz = v.trunnion[f'cannon_{b}']
        origins[f'cannon_{b}'] = [x, ty, tz]
        origins[f'cannon_slide_{b}'] = [x, y, z1]          # the mouth plane (the shells come out there)
    wheels = {}
    for bone in sorted(set(bones)):
        if bone.startswith('tire_move'):
            sel = bones == bone
            if v.split_idler and bone.startswith(('tire_moveG', 'tire_moveH')):
                sel = np.isin(bones, [f'{v.split_idler}_{bone[-1]}', f'tire_moveH_{bone[-1]}'])
            lo, hi = box(src.positions[np.unique(src.tri_v[sel])])
            centre = (lo + hi) / 2
            origins[bone] = centre.tolist()
            wheels[bone] = {'centre': centre.tolist(), 'radius': float(min(hi[1] - lo[1], hi[2] - lo[2]) / 2)}
            # the track station bone at its wheel (car_base_draw_wheel_info pairs them; nothing is skinned to it)
            if bone[9] in 'BCDEFG':
                origins[f'catapi{bone[9]}_{bone[-1]}'] = centre.tolist()
    for bone in ('doppler_radar', 'tracking_radar'):
        sel = bones == bone
        if not sel.any():       # a bone with no mesh (the Sturmtiger's): at the nodes its proxy is fitted to
            sel = np.asarray([any(re.match(p, n) for p in v.collision.get(bone, ())) for n in tri_node])
        if not sel.any():
            raise ValueError(f'{v.kind}: {bone} has no mesh and no collision nodes')
        lo, hi = box(src.positions[np.unique(src.tri_v[sel])])
        origins[bone] = ((lo + hi) / 2).tolist()
    visual = []
    if v.rotor:
        x, y, z0, _z1 = axes['left']
        visual.append({'name': v.rotor, 'parent': 'cannon_slide_l', 'origin': [x, y, z0]})

    # muzzles and case ports (model space)
    muzzles = {side: [{'bone': f'cannon_slide_{side[0]}', 'point': [axes[side][0], axes[side][1], axes[side][3]]}]
               for side in ('left', 'right')}
    if v.kind == 'vulcan':        # one M61: both belts leave the same six barrels (make_tracked_combat: slide_l)
        muzzles['right'] = [dict(muzzles['left'][0])]
    tubes = []
    if v.missiles:
        # The holder is the tubes' bone: a tube elsewhere would leave its round off the tube as the gun elevates.
        on = {str(b) for b in bones[np.isin(src.tri_obj, [k for k, o in enumerate(src.objects) if node(o) == v.missiles])]}
        if on != {v.missile_bone}:
            raise ValueError(f'{v.kind}: the {v.missiles} tubes ride {sorted(on)}, not missile_bone {v.missile_bone}')
        for P in instances(src, v.missiles):
            lo, hi = box(P)
            tubes.append([(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, hi[2]])
        if v.missiles_keep:     # the outermost, half on each side, the higher first where they stand level
            tubes = [p for sign in (1, -1) for p in sorted((p for p in tubes if p[0] * sign > 0),
                                                           key=lambda p: (-round(abs(p[0]), 1), -p[1]))[:v.missiles_keep // 2]]
        tubes.sort(key=lambda p: (-round(p[0], 1), p[1]))     # left low, left high, right low, right high
    muzzles['missiles'] = [{'bone': v.missile_bone, 'point': p} for p in tubes]
    for slot, (bone, points) in v.stores.items():
        muzzles[slot] = [{'bone': bone, 'point': list(p)} for p in points]
    ejections = {}
    for side, sign in (('left', 1.0), ('right', -1.0)):
        x, y, z0, _z1 = axes[side]
        bone = muzzles[side][0]['bone']
        ejections[side] = {'bone': bone, 'point': [x + sign * 0.2, y, z0]}

    # collision: per proxy the triangles of its nodes (default: every triangle of its bone and the bones under it
    # that have no proxy of their own), cut to `clip`
    proxies = ['body', 'cannon_main', 'cannon_l', 'cannon_r', 'doppler_radar', 'tracking_radar'] + \
              [f'tire_move{k}_{s}' for k in LETTERS for s in 'lr']
    owner = {'cannon_slide_l': 'cannon_l', 'cannon_slide_r': 'cannon_r', 'edf6vc_m61_rotor': 'cannon_l'}
    collision = {}
    for proxy in proxies:
        if proxy in v.collision:
            sel = np.zeros(len(src.tri_v), dtype=bool)
            for pattern in v.collision[proxy]:
                sel |= np.asarray([bool(re.match(pattern, n)) for n in tri_node])
        else:
            sel = np.asarray([owner.get(b, b) == proxy for b in bones])
        if proxy.startswith('tire_move') and not sel.any():
            continue            # an unused station (the BMPT has eight a side: none unused; the M163 seven + split H)
        if not sel.any():
            raise ValueError(f'{v.kind}: no triangle for the {proxy} proxy')
        T = src.tri_v[sel]
        used = np.unique(T)
        hull_pos, hull_tri = collision_hulls(src.positions, T)
        arrays[f'c_{proxy}_pos'] = hull_pos.astype(np.float32)
        arrays[f'c_{proxy}_tri'] = hull_tri.astype(np.uint32)
        lo, hi = box(src.positions[used])
        target = [lo.tolist(), hi.tolist()]
        if proxy in v.clip:
            cuts = v.clip[proxy]
            for axis, end, value in (cuts if isinstance(cuts, list) else [cuts]):
                target[0 if end == 'min' else 1][axis] = value
        if v.split_idler and proxy[:-2] in (v.split_idler, 'tire_moveH'):
            # the idler's halves are siblings: 4 cm apart across the middle (tracked_collision._sweep)
            whole = box(src.positions[np.unique(src.tri_v[np.isin(bones, [f'{v.split_idler}_{proxy[-1]}',
                                                                           f'tire_moveH_{proxy[-1]}'])])])
            mid = float((whole[0][0] + whole[1][0]) / 2)
            plus_x = (proxy[:-2] == 'tire_moveH') == (proxy[-1] == 'l')     # H is the outer half: +x on the left
            if plus_x:
                target[0][0] = mid + IDLER_GAP / 2
            else:
                target[1][0] = mid - IDLER_GAP / 2
        collision[proxy] = {'target': target, 'source_triangles': int(len(T)), 'hull_triangles': int(len(hull_tri))}

    os.makedirs(out, exist_ok=True)
    np.savez_compressed(os.path.join(out, 'model.npz'), **arrays)
    info = {'format': FORMAT, 'kind': v.kind, 'title': v.title, 'source': v.source_url,
            'prepared_from': os.path.basename(src.path), 'winding_flipped': flipped, 'auto_smooth_deg': AUTO_SMOOTH,
            'materials': mat_specs, 'parts': parts, 'origins': origins, 'visual_bones': visual,
            'wheels': wheels, 'muzzles': muzzles, 'ejections': ejections, 'collision': collision,
            'triangles': int(len(src.tri_v))}
    with open(os.path.join(out, 'model.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(info, f, indent=1, sort_keys=False)
        f.write('\n')
    return info


def part_name(v: Vehicle, bone: str, mat: int, mats: list[dict], taken: list[str]) -> str:
    """tracked_collision reads the wheels by name: wheel_<letter>_<side>; the split idler's outer half
    wheel_G_outer_<side>. Every other part: <bone>.<material>."""
    if bone.startswith('tire_move'):
        letter, side = bone[9], bone[-1]
        name = f'wheel_G_outer_{side}' if v.split_idler and letter == 'H' else f'wheel_{letter}_{side}'
    else:
        name = f'{bone}.{mats[mat]["name"]}'
    if name in taken:
        raise ValueError(f'{v.kind}: two parts named {name} (a wheel in two materials?)')
    return name


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('vehicle', choices=sorted(VEHICLES))
    ap.add_argument('source', help='the unpacked Sketchfab source OBJ (M163.obj / BMPT.obj)')
    ap.add_argument('--out', help='model folder to write (default: models/<folder> in the repository)')
    ap.add_argument('--license', help="the Sketchfab download's license.txt (copied as LICENSE.txt)")
    a = ap.parse_args(argv)
    v = VEHICLES[a.vehicle]
    out = a.out or os.path.join(ROOT, 'models', v.folder)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    info = prepare(v, read_source(a.source), out)
    if a.license:
        shutil.copyfile(a.license, os.path.join(out, 'LICENSE.txt'))
    print(json.dumps({'kind': info['kind'], 'triangles': info['triangles'], 'parts': len(info['parts']),
                      'materials': [m['name'] for m in info['materials']], 'origins': info['origins'],
                      'muzzles': info['muzzles']}, indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
