"""An X-Morph: Defense unit's model as the EDF-side preparers read it (tools/prep_xmorph.py, docs/xmorph-port.md): its
entity blueprint, its Ogre mesh's triangles in EDF's frame, each triangle's (rigid) bone, the skeleton's bind points,
the attachments (.att), the turrets and weapons the blueprint declares, and its materials' textures as images. Nothing
here is family-specific: the tracked preparer builds a V603 model folder from it, a wheeled / heli one can do the same.

    load(packs, entity) -> UnitModel        entity: 'ground/medium_tank_regular' (entities/units/<entity>.ent)
    to_edf(points)                           X-Morph (x fwd, y left, z up) -> EDF (x left, y up, z fwd): (y, z, x)
    texture_images(packs, material)          {'diffuse': RGB image, 'normal': RGB tangent-space image} (what it has)

Frames (measured 2026-10-27): X-Morph's units face +X with +Y their left and +Z up, metres (barrels point +X, the
left wing / track at +Y, civilian cars 4.4 x 1.8 x 1.45 m: real scale), the ground at z = 0. EDF's vehicles face +Z
with +X their left and +Y up. (x, y, z) -> (y, z, x) is a cyclic permutation: a proper rotation, the winding kept.
The vertices are already in model space (the bind pose), so a vertex is placed by its own position alone; an
attachment's offset is in its parent bone's bind frame (most bones are unrotated, the be_* effect bones are not).
Texture coordinates are Ogre's: the top-left origin, like EDF's.
"""
from __future__ import annotations

import io
from dataclasses import dataclass

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
        tc = self.blueprint.child('TurretComp')
        return {t.get('name'): (t.get('yaw'), t.get('pitch')) for t in (tc.find('Turret') if tc else [])}

    def weapons(self) -> list[Weapon]:
        wc = self.blueprint.child('WeaponComp')
        out = []
        for w in (wc.find('Weapon') if wc else []):
            ctl = w.child('weapon_controller')
            out.append(Weapon(w.get('name'), ctl.get('turret') if ctl else None,
                              [(m.get('attachment'), m.get('recoil_bone')) for m in w.find('Muzzle')]))
        return out

    def bone_points(self, bones) -> np.ndarray:  # noqa: ANN001 - iterable of bone names
        sel = np.isin(self.tri_bone, list(bones))
        return self.positions[np.unique(self.triangles[sel])] if sel.any() else np.zeros((0, 3))


def _attachments(packs: xp.Packs, mesh_path: str) -> dict[str, tuple[str, np.ndarray]]:
    path = mesh_path[:-len('.mesh')] + '.att'
    if not packs.exists(path):
        return {}
    out = {}
    for a in xp.parse(packs.text(path)).find('Attachment'):
        offset = a.get('offset')
        out[a.get('name')] = (a.get('parent'), np.asarray([float(v) for v in offset.split()] if offset else (0, 0, 0)))
    return out


def load(packs: xp.Packs, entity: str) -> UnitModel:
    bp = xp.parse(packs.text(f'entities/units/{entity}.ent')).child('EntityBlueprint')
    _req(bp is not None, f'{entity}: no EntityBlueprint')
    rend, skel = bp.child('RenderableDescComp'), bp.child('SkeletonDescComp')
    _req(rend is not None and skel is not None, f'{entity}: no mesh or no skeleton')
    mesh_path = rend.get('mesh')
    mesh = om.read_mesh(packs.read(mesh_path))
    sk = om.read_skeleton(packs.read(skel.get('name') + '.skeleton'))
    world = sk.world()
    by_handle = {b.handle: b.name for b in sk.bones}
    pos, uv, tris, tbone, tmat = [], [], [], [], []
    materials: list[str] = []
    base = 0
    for sub in mesh.submeshes:
        p = mesh.positions(sub)
        t = mesh.uvs(sub)
        _req(t is not None, f'{entity}: submesh {sub.name} has no texture coordinates')
        rb = mesh.rigid_bones(sub)
        _req(bool((rb >= 0).all()), f'{entity}: submesh {sub.name} has unskinned vertices')
        corner = rb[sub.indices]
        # one bone a triangle: X-Morph's units are rigid pieces (a triangle whose corners disagree takes its first's)
        if sub.material not in materials:
            materials.append(sub.material)
        pos.append(to_edf(p))
        uv.append(t)
        tris.append(sub.indices + base)
        tbone.append(np.asarray([sk.bones[i].name for i in corner[:, 0]], dtype=object))
        tmat.append(np.full(len(sub.indices), materials.index(sub.material)))
        base += len(p)
    names = [b.name for b in sk.bones]
    return UnitModel(entity, bp, mesh_path, names,
                     {b.name: (by_handle[b.parent] if b.parent >= 0 else None) for b in sk.bones},
                     {b.name: to_edf(w[:3, 3]) for b, w in zip(sk.bones, world)},
                     {b.name: w for b, w in zip(sk.bones, world)},
                     _attachments(packs, mesh_path),
                     np.vstack(pos), np.vstack(uv), np.vstack(tris), np.concatenate(tbone), np.concatenate(tmat),
                     materials)


# ------------------------------------------------------------------------------------------ textures

def material_node(packs: xp.Packs, material: str) -> xp.Node:
    """The material script's block of `material` ('units_ground/medium_tank_regular': materials/scripts/
    units_ground.material, the block whose 'type' starts with the name)."""
    group = material.split('/')[0]
    root = xp.parse(packs.text(f'materials/scripts/{group}.material'))
    for c in root.children:
        if (c.get('type') or '').split(' ')[0] == material:
            return c
    raise XmModelError(f'material {material}: not in materials/scripts/{group}.material')


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
    """{'diffuse': RGB image, 'normal': RGB image} of what the material declares (no specular or emissive: the EDF
    body material has no slot for them)."""
    files = texture_files(packs, material)
    _req('diffuse' in files, f'material {material}: no diffuse texture')
    out = {'diffuse': _image(packs.read(files['diffuse'])).convert('RGB')}
    if 'normal' in files:
        out['normal'] = normal_rgb(_image(packs.read(files['normal'])))
    return out
