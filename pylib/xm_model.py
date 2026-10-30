"""An X-Morph: Defense unit's model as the EDF-side preparers read it (tools/prep_xmorph.py, docs/xmorph-port.md): its
entity blueprint, its Ogre mesh's triangles in EDF's frame, each triangle's (rigid) bone, the skeleton's bind points,
the attachments (.att), the turrets and weapons the blueprint declares, and its materials' textures as images. Nothing
here is family-specific: the tracked preparer builds a V603 model folder from it, a wheeled / heli one can do the same.

    load(packs, entity) -> UnitModel        entity: 'ground/medium_tank_regular' (entities/units/<entity>.ent)
    assemble(packs, entity) -> UnitModel    ... with its parts (ChildDescComp: a boss's turrets, tracks, legs)
    blueprint(packs, path) -> Node           an entity's blueprint with its BlueprintComp base's components
    named(packs, name) -> Node               the blueprint called name (a weapon's ammo_blueprint)
    to_edf(points)                           X-Morph (x fwd, y left, z up) -> EDF (x left, y up, z fwd): (y, z, x)
    texture_images(packs, material)          {'diffuse': RGB, 'normal': RGB tangent space, 'specular' / 'emissive':
                                             grey} images (what it has)

Frames (measured 2026-10-27): X-Morph's units face +X with +Y their left and +Z up, metres (barrels point +X, the
left wing / track at +Y, civilian cars 4.4 x 1.8 x 1.45 m: real scale), the ground at z = 0. EDF's vehicles face +Z
with +X their left and +Y up. (x, y, z) -> (y, z, x) is a cyclic permutation: a proper rotation, the winding kept.
The vertices are already in model space (the bind pose), so a vertex is placed by its own position alone; an
attachment's offset is in its parent bone's bind frame (most bones are unrotated, the be_* effect bones are not).
Texture coordinates are Ogre's: the top-left origin, like EDF's.
"""
from __future__ import annotations

import io
from dataclasses import dataclass, field

import numpy as np

import ogre_mesh as om
import xm_pack as xp


class XmModelError(ValueError):
    pass


def _req(ok: bool, message: str) -> None:
    if not ok:
        raise XmModelError(message)


def to_edf(points) -> np.ndarray:  # noqa: ANN001 - array-like (..., 3)
    p = np.asarray(points, dtype=np.float64)
    return p[..., [1, 2, 0]]


@dataclass
class Weapon:
    name: str
    turret: str | None                     # the TurretComp turret that aims it
    muzzles: list[tuple[str, str | None]]  # (attachment or bone, recoil bone)


@dataclass
class UnitModel:
    entity: str
    blueprint: xp.Node
    mesh_path: str
    bones: list[str]                       # skeleton order
    parent: dict[str, str | None]
    bind: dict[str, np.ndarray]            # bone -> its bind point, EDF frame
    frames: dict[str, np.ndarray]          # bone -> its 4x4 bind transform, X-Morph frame
    attachments: dict[str, tuple[str, np.ndarray]]   # name -> (parent bone or 'scene_node', offset in X-Morph frame)
    positions: np.ndarray                  # (n, 3) EDF frame, metres
    uvs: np.ndarray                        # (n, 2) top-left origin
    triangles: np.ndarray                  # (t, 3) into positions
    tri_bone: np.ndarray                   # (t,) bone name (object array)
    tri_material: np.ndarray               # (t,) index into materials
    materials: list[str]                   # Ogre material names, one per distinct submesh material
    # An assembly's (assemble()): its parts' turrets and weapons, after the root blueprint's own
    part_turrets: dict = field(default_factory=dict)
    part_weapons: list = field(default_factory=list)
    parts: list = field(default_factory=list)   # (part blueprint, the bone or attachment it hangs on or None, bones)
    normals: np.ndarray | None = None      # (n, 3) EDF frame, unit: the mesh's own (its normal maps were baked on them)

    def children(self, bone: str) -> list[str]:
        return [b for b in self.bones if self.parent[b] == bone]

    def subtree(self, bone: str) -> list[str]:
        out = [bone]
        for c in self.children(bone):
            out += self.subtree(c)
        return out

    def point(self, name: str) -> np.ndarray:
        """An attachment's or a bone's bind point (EDF frame). An attachment's offset is in its parent bone's bind
        frame ('scene_node': the model's)."""
        if name in self.attachments:
            parent, offset = self.attachments[name]
            if parent == 'scene_node':
                return to_edf(offset)
            _req(parent in self.frames, f'{name}: parent {parent} is not a bone')
            w = self.frames[parent]
            return to_edf(w[:3, :3] @ offset + w[:3, 3])
        _req(name in self.bind, f'{self.entity}: no attachment or bone {name}')
        return self.bind[name].copy()

    def turrets(self) -> dict[str, tuple[str | None, str | None]]:
        """TurretComp: turret name -> (yaw bone, pitch bone) as written (either may name an attachment)."""
        return {**_turrets(self.blueprint), **self.part_turrets}

    def weapons(self) -> list[Weapon]:
        return _weapons(self.blueprint) + self.part_weapons

    def bone_points(self, bones) -> np.ndarray:  # noqa: ANN001 - iterable of bone names
        sel = np.isin(self.tri_bone, list(bones))
        return self.positions[np.unique(self.triangles[sel])] if sel.any() else np.zeros((0, 3))


def _turrets(bp: xp.Node) -> dict[str, tuple[str | None, str | None]]:
    tc = bp.child('TurretComp')
    return {t.get('name'): (t.get('yaw'), t.get('pitch')) for t in (tc.find('Turret') if tc else [])}


def _weapons(bp: xp.Node) -> list[Weapon]:
    wc = bp.child('WeaponComp')
    out = []
    for w in (wc.find('Weapon') if wc else []):
        ctl = w.child('weapon_controller')
        out.append(Weapon(w.get('name'), ctl.get('turret') if ctl else None,
                          [(m.get('attachment'), m.get('recoil_bone')) for m in w.find('Muzzle')]))
    return out


def _attachments(packs: xp.Packs, mesh_path: str) -> dict[str, tuple[str, np.ndarray]]:
    path = mesh_path[:-len('.mesh')] + '.att'
    if not packs.exists(path):
        return {}
    out = {}
    for a in xp.parse(packs.text(path)).find('Attachment'):
        offset = a.get('offset')
        out[a.get('name')] = (a.get('parent'), np.asarray([float(v) for v in offset.split()] if offset else (0, 0, 0)))
    return out


_INDEX: dict[int, dict[str, xp.Node]] = {}


def blueprints(packs: xp.Packs) -> dict[str, xp.Node]:
    """Every EntityBlueprint in the packs by its name (entities/**/*.ent: a unit's file holds one, a projectile family's
    several: entities/projectiles/enemy_flak.ent defines projectiles/enemy_flak_medium and its kin), as written."""
    if id(packs) not in _INDEX:
        out: dict[str, xp.Node] = {}
        for path in packs.paths('entities/'):
            if path.endswith('.ent'):
                for bp in xp.parse(packs.text(path)).all('EntityBlueprint'):
                    if bp.get('name'):
                        out.setdefault(bp.get('name'), bp)
        _INDEX[id(packs)] = out
    return _INDEX[id(packs)]


def compose(packs: xp.Packs, bp: xp.Node) -> xp.Node:
    """`bp` as the game composes it: one whose BlueprintComp names another (`blueprint "units/air/large_helicopter_core"`)
    has that one's components, its own replacing the base's of the same name (the warcar's escort helicopter is the
    core helicopter with its own turrets, weapons and health)."""
    base = bp.child('BlueprintComp')
    if base is None or not base.get('blueprint'):
        return bp
    parent = blueprints(packs).get(base.get('blueprint'))
    _req(parent is not None, f'{bp.get("name")}: its base {base.get("blueprint")} is no blueprint in the packs')
    parent = compose(packs, parent)
    own = {c.name for c in bp.children}
    return xp.Node(bp.name, bp.values, [c for c in parent.children if c.name not in own] + bp.children)


def blueprint(packs: xp.Packs, path: str) -> xp.Node:
    """The (composed) EntityBlueprint of the file `path` (entities/units/<entity>.ent: a unit's one)."""
    bp = xp.parse(packs.text(path)).child('EntityBlueprint')
    _req(bp is not None, f'{path}: no EntityBlueprint')
    return compose(packs, bp)


def named(packs: xp.Packs, name: str) -> xp.Node:
    """The (composed) blueprint called `name` (a weapon's ammo_blueprint: 'projectiles/enemy_flak_medium')."""
    bp = blueprints(packs).get(name)
    _req(bp is not None, f'no blueprint {name} in the packs')
    return compose(packs, bp)


STATIC_BONE = 'body'   # the one bone of a unit whose mesh is not skinned (the bunkers, the highway tank's drone)


def load(packs: xp.Packs, entity: str) -> UnitModel:
    return load_blueprint(packs, blueprint(packs, f'entities/units/{entity}.ent'), entity)


def load_blueprint(packs: xp.Packs, bp: xp.Node, entity: str) -> UnitModel:
    """The model of the (composed) blueprint `bp` (load(): a unit file's; assemble(): its parts', which live in the
    unit's own file under their names, units/bosses/highway_tank/front_turret)."""
    rend, skel = bp.child('RenderableDescComp'), bp.child('SkeletonDescComp')
    _req(rend is not None and bool(rend.get('mesh')), f'{entity}: no mesh')
    mesh_path = rend.get('mesh')
    mesh = om.read_mesh(packs.read(mesh_path))
    rigid = [mesh.rigid_bones(sub) for sub in mesh.submeshes]
    if all((rb < 0).all() for rb in rigid):
        # Not skinned: the game draws it as it is (a SkeletonDescComp naming a skeleton the packs do not have, as
        # bunker_barracks_02's, changes nothing), so one bone at the origin carries it.
        names, parents = [STATIC_BONE], {STATIC_BONE: None}
        frames = {STATIC_BONE: np.eye(4)}
        rigid = [np.zeros_like(rb) for rb in rigid]
    else:
        _req(skel is not None, f'{entity}: a skinned mesh and no SkeletonDescComp')
        sk = om.read_skeleton(packs.read(skel.get('name') + '.skeleton'))
        by_handle = {b.handle: b.name for b in sk.bones}
        names = [b.name for b in sk.bones]
        parents = {b.name: (by_handle[b.parent] if b.parent >= 0 else None) for b in sk.bones}
        frames = {b.name: w for b, w in zip(sk.bones, sk.world())}
        index = {b.handle: i for i, b in enumerate(sk.bones)}
        root = next(i for i, b in enumerate(sk.bones) if b.parent < 0)
        # A vertex assigned to a handle the skeleton has not (large_gunship_aa's mesh: 16 vertices on a fifth bone of
        # a four-bone skeleton, in the game's own files) rides the root.
        rigid = [np.asarray([index.get(int(h), root) for h in rb], dtype=np.int32) for rb in rigid]
    pos, uv, nrm, tris, tbone, tmat = [], [], [], [], [], []
    materials: list[str] = []
    base = 0
    # A blueprint can put its own material on a submesh (RenderableDescComp SubMeshes: the TARANTULA Mk VI / Mk VII are
    # the four-legged mech's mesh in bosses/four_leg_mech_skin1 / _skin2)
    skins = {int(s.get('id')): s.get('material') for s in rend.find('SubMesh') if s.get('id') and s.get('material')}
    for k, (sub, rb) in enumerate(zip(mesh.submeshes, rigid)):
        material = skins.get(k, sub.material)
        p = mesh.positions(sub)
        t = mesh.uvs(sub)
        _req(t is not None, f'{entity}: submesh {sub.name} has no texture coordinates')
        n = mesh.normals(sub)
        _req(n is not None, f'{entity}: submesh {sub.name} has no normals')
        _req(bool((rb >= 0).all()), f'{entity}: submesh {sub.name} has unskinned vertices')
        corner = rb[sub.indices]
        # one bone a triangle: X-Morph's units are rigid pieces (a triangle whose corners disagree takes its first's)
        if material not in materials:
            materials.append(material)
        pos.append(to_edf(p))
        uv.append(t)
        n = to_edf(n)
        nrm.append(n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12))
        tris.append(sub.indices + base)
        tbone.append(np.asarray([names[i] for i in corner[:, 0]], dtype=object))
        tmat.append(np.full(len(sub.indices), materials.index(material)))
        base += len(p)
    return UnitModel(entity, bp, mesh_path, names, parents,
                     {n: to_edf(w[:3, 3]) for n, w in frames.items()}, frames,
                     _attachments(packs, mesh_path),
                     np.vstack(pos), np.vstack(uv), np.vstack(tris), np.concatenate(tbone), np.concatenate(tmat),
                     materials, normals=np.vstack(nrm))


def _from_edf(points) -> np.ndarray:  # noqa: ANN001 - array-like (..., 3)
    p = np.asarray(points, dtype=np.float64)
    return p[..., [2, 0, 1]]


def _rename(name: str, taken, part: str) -> str:  # noqa: ANN001 - container of names
    return name if name not in taken else f'{part}/{name}'


def assemble(packs: xp.Packs, entity: str) -> UnitModel:
    """The unit with its parts (ChildDescComp: the bosses are built of them, every turret, leg, wing, track a part
    blueprint with its own mesh and skeleton, hit and destroyed one by one in the game) as one model: what the game
    draws when nothing is destroyed. A part names the bone (or attachment) it hangs on, its mesh in that bone's bind
    frame (main_turret_barrel on be_main_turret_turret_vert), or none, its mesh then in the unit's own frame (measured
    2026-10-30: the highway tank's tracks, side turrets, rocket pods sit where they belong with no offset); its root
    bones become the children of that bone (of the unit's root bone). Parts nest (a part's own ChildDescComp).
    A part's bone or attachment named as one before it is is renamed '<part>/<name>' (a static part's one bone, its
    att_smoke_1), its weapons' muzzles with it; its turrets and weapons join the unit's (UnitModel.part_*)."""
    um = load(packs, entity)
    _assemble_into(packs, um, um.blueprint, None)
    return um


def _assemble_into(packs: xp.Packs, um: UnitModel, bp: xp.Node, _at: str | None) -> None:
    cdc = bp.child('ChildDescComp')
    for cd in (cdc.find('ChildDesc') if cdc else []):
        # the name alone: a line can carry the designers' note after it (the warcar's
        # 'units/bosses/warcar/middle_grenade_launcher -- obecny laser projectile')
        name = cd.get('blueprint').split()[0]
        raw = blueprints(packs).get(name)
        _req(raw is not None, f'{um.entity}: its part {name} is no blueprint in the packs')
        part_bp = compose(packs, raw)
        part = name.split('/')[-1]
        at = cd.get('bone')
        if at is None:
            host, frame = um.bones[0], np.eye(4)
        elif at in um.frames:
            host, frame = at, um.frames[at]
        else:
            _req(at in um.attachments, f'{um.entity}: part {part} hangs on {at}, no bone or attachment')
            parent, offset = um.attachments[at]
            host = um.bones[0] if parent == 'scene_node' else parent
            frame = (np.eye(4) if parent == 'scene_node' else um.frames[parent]).copy()
            frame[:3, 3] = frame[:3, :3] @ offset + frame[:3, 3]
        if part_bp.child('RenderableDescComp') is None or not part_bp.child('RenderableDescComp').get('mesh'):
            _assemble_into(packs, um, part_bp, at)   # a part without a mesh of its own (a destructor, a marker)
            continue
        pm = load_blueprint(packs, part_bp, name)
        bone = {b: _rename(b, um.frames, part) for b in pm.bones}
        att = {a: _rename(a, um.attachments, part) for a in pm.attachments}
        R, t = frame[:3, :3], frame[:3, 3]
        place = lambda p: to_edf(_from_edf(p) @ R.T + t)   # noqa: E731 - the part's frame -> the unit's
        for b in pm.bones:
            nb = bone[b]
            um.bones.append(nb)
            um.parent[nb] = bone[pm.parent[b]] if pm.parent[b] is not None else host
            um.frames[nb] = frame @ pm.frames[b]
            um.bind[nb] = place(pm.bind[b])
        for a, (parent, offset) in pm.attachments.items():
            if parent == 'scene_node' and at is None:
                um.attachments[att[a]] = ('scene_node', offset)
            elif parent == 'scene_node':      # the part's origin is the host's frame (an attachment host: its point)
                h = um.frames[host]
                um.attachments[att[a]] = (host, h[:3, :3].T @ (R @ offset + t - h[:3, 3]))
            else:
                um.attachments[att[a]] = (bone.get(parent, parent), offset)
        mat = []
        for m in pm.materials:
            if m not in um.materials:
                um.materials.append(m)
            mat.append(um.materials.index(m))
        base = len(um.positions)
        um.positions = np.vstack((um.positions, place(pm.positions)))
        um.uvs = np.vstack((um.uvs, pm.uvs))
        um.normals = np.vstack((um.normals, to_edf(_from_edf(pm.normals) @ R.T)))   # directions: turned only
        um.triangles = np.vstack((um.triangles, pm.triangles + base))
        um.tri_bone = np.concatenate((um.tri_bone, np.asarray([bone[b] for b in pm.tri_bone], dtype=object)))
        um.tri_material = np.concatenate((um.tri_material, np.asarray(mat, dtype=um.tri_material.dtype)[pm.tri_material]))
        for tn, (yaw, pitch) in _turrets(part_bp).items():
            um.part_turrets[tn] = (bone.get(yaw, att.get(yaw, yaw)), bone.get(pitch, att.get(pitch, pitch)))
        for w in _weapons(part_bp):
            um.part_weapons.append(Weapon(w.name, w.turret, [(bone.get(m, att.get(m, m)), bone.get(r, r) if r else r)
                                                             for m, r in w.muzzles]))
        um.parts.append((name, at, [bone[b] for b in pm.bones]))
        _assemble_into(packs, um, part_bp, at)


# ------------------------------------------------------------------------------------------ textures

def material_node(packs: xp.Packs, material: str) -> xp.Node:
    """The material script's block of `material` ('units_ground/medium_tank_regular': materials/scripts/
    units_ground.material, the block whose 'type' starts with the name; the bosses' 'bosses/highway_tank':
    units_bosses.material)."""
    group = material.split('/')[0]
    scripts = [f'materials/scripts/{g}.material' for g in (group, f'units_{group}')]
    for path in scripts:
        if packs.exists(path):
            for c in xp.parse(packs.text(path)).children:
                if (c.get('type') or '').split(' ')[0] == material:
                    return c
    raise XmModelError(f'material {material}: not in {" or ".join(scripts)}')


def texture_files(packs: xp.Packs, material: str) -> dict[str, str]:
    """The material's texture aliases: {'diffuse': path, 'normal': ..., 'specular': ..., 'emissive': ...}."""
    out = {}
    for v in material_node(packs, material).values.get('set_texture_alias', []):
        kind, path = v.split()[:2]
        out[kind] = path
    return out


def _image(data: bytes):  # noqa: ANN202 - PIL image
    from PIL import Image
    im = Image.open(io.BytesIO(data))
    im.load()
    return im


def normal_rgb(im):  # noqa: ANN001, ANN201 - PIL image -> PIL image
    """X-Morph's normal maps are DXT5 with X in alpha and Y in the colour channels (R = G = B; measured: equal means
    and spreads in R, G, B, a different alpha): rebuilt as an RGB tangent-space map (Z from the unit length), the
    layout the EDF body shader reads (its normal maps are RGB, RG used). Their green is OpenGL's (+Y the texture's up),
    the stock vehicles' and the binormal's (+v, down) the other way, so Y is negated (measured on the packs' tank maps:
    docs/texture-quality.md)."""
    from PIL import Image
    a = np.asarray(im.convert('RGBA'), dtype=np.float64) / 127.5 - 1.0
    x, y = a[..., 3], -a[..., 1]
    z = np.sqrt(np.clip(1.0 - x * x - y * y, 0.0, 1.0))
    rgb = np.stack((x, y, z), axis=-1)
    return Image.fromarray(np.clip((rgb + 1.0) * 127.5 + 0.5, 0, 255).astype(np.uint8), 'RGB')


def texture_images(packs: xp.Packs, material: str) -> dict:
    """{'diffuse': RGB image, 'normal': RGB image, 'specular': grey image, 'emissive': grey image} of what the material
    declares. The specular and emissive maps are grey (R = G = B, measured on every unit's): their red. The EDF side
    reads the specular map as roughness / metalness and the emissive one as a light mask (pylib/wt_tracked.py)."""
    files = texture_files(packs, material)
    _req('diffuse' in files, f'material {material}: no diffuse texture')
    out = {'diffuse': _image(packs.read(files['diffuse'])).convert('RGB')}
    if 'normal' in files:
        out['normal'] = normal_rgb(_image(packs.read(files['normal'])))
    for kind in ('specular', 'emissive'):
        if kind in files:
            out[kind] = _image(packs.read(files[kind])).convert('RGB').getchannel('R')
    return out
