"""Proteus shield mesh, made from the player's stock Fencer shield material.

The stock skeleton, geometry and animation tracks retain their indices. Thirty-six
root children are bound in CANM and receive zero-scale tracks in every clip, so
the shield stays hidden without the plugin. The post-animation pose hook owns
their complete local matrices; it must replace, rather than multiply, zero scale.
"""
from __future__ import annotations

import copy
import math
import struct
from dataclasses import replace

import cas_pose
import graft_pure as graft
import mdb
from mdb_jet import pack_vertex, vertex_table

SHIELD_BONES = tuple(f'vc_ps_{i:02d}' for i in range(36))
SHIELD_RADIUS = 11.0
SHIELD_HALF_ANGLE = math.pi / 36
SHIELD_BOTTOM, SHIELD_TOP = 0.3, 16.0
HOSTS = ('V614_PROTEUS_MK2', 'VEHICLE407_BIGBEGARUTA')
DONOR = 'H_SHIELD_ENERGY01.RAB'


def animation(data: bytes) -> bytes:
    """Append runtime bones with hidden defaults, preserving every stock track.

    CANM offsets are relative signed offsets. Relocating its channel/name tables
    leaves key payloads in place; channel pointers are rebased individually.
    Existing clips and the CAS state graph remain at their original addresses.
    """
    pose = cas_pose.CasPose(data)
    if set(SHIELD_BONES) & set(pose.names):
        raise ValueError('Proteus shield is already bound')
    out = bytearray(data)
    # Channel rows contain SIMD vectors, not just scalar offsets. EDF.dll's
    # 0x1160500 evaluator reads constant quaternions with MOVAPS at 0x1160620
    # (and quantized vectors with aligned arithmetic in 0x1160200). The CAS
    # allocation is aligned; preserve that alignment for every 48-byte row.
    out.extend(bytes((-len(out)) % 16))
    points = len(out)
    for i in range(pose.channel_count):
        old_at = pose.points + i * 48
        new_at = len(out)
        row = bytearray(data[old_at:old_at + 48])
        pointer = struct.unpack_from('<i', row, 32)[0]
        if pointer:
            struct.pack_into('<i', row, 32, old_at + pointer - new_at)
        out.extend(row)
    # Single scale key. Kind 0 is an unquantized vec3, count 1 lives in base xyz.
    out.extend(struct.pack('<8f4i', 0., 0., 0., 0., 0., 0., 0., 0., 0, 0, 1, 0))
    struct.pack_into('<II', out, pose.canm + 16, pose.channel_count + 1, points - pose.canm)
    names = pose.names + SHIELD_BONES
    names_at = len(out)
    out.extend(bytes(4 * len(names)))
    for i, name in enumerate(names):
        at = names_at + i * 4
        struct.pack_into('<i', out, at, len(out) - at)
        out.extend(name.encode('utf-16-le') + b'\0\0')
    struct.pack_into('<II', out, pose.canm + 24, len(names), names_at - pose.canm)
    for clip in pose.clips:
        out.extend(bytes((-len(out)) % 4))
        at = len(out)
        for track in clip.tracks:
            out.extend(data[track.at:track.at + 8])
        for i in range(len(SHIELD_BONES)):
            out.extend(struct.pack('<Hhhh', len(pose.names) + i, -1, -1, pose.channel_count))
        struct.pack_into('<II', out, clip.at + 20, len(clip.tracks) + len(SHIELD_BONES), at - clip.at)
    return bytes(out)


def shield_mesh(template: mdb.Mesh, bone: int) -> mdb.Mesh:
    """Double-sided 10-degree panel, with the stock energy shield vertex layout."""
    keys, original = vertex_table(template)
    rows, triangles = [], []
    steps = 2
    for side in (1., -1.):
        start = len(rows)
        for i in range(steps + 1):
            angle = -SHIELD_HALF_ANGLE + 2 * SHIELD_HALF_ANGLE * i / steps
            sn, cs = math.sin(angle), math.cos(angle)
            for y, v in ((SHIELD_BOTTOM, 0.), (SHIELD_TOP, 1.)):
                values = {
                    'position': (SHIELD_RADIUS * sn, y, SHIELD_RADIUS * cs, 1.),
                    'normal': (side * sn, 0., side * cs, 1.),
                    'binormal': (0., 1., 0., 1.),
                    'tangent': (side * cs, 0., -side * sn, 1.),
                    'texcoord': (i / steps, v),
                    'BLENDWEIGHT': (1., 0., 0., 0.),
                    'BLENDINDICES': (bone, 0, 0, 0),
                }
                row = copy.deepcopy(original[0])
                for k, key in enumerate(keys):
                    name = key.split(':')[0]
                    if name not in values:
                        raise ValueError(f'unhandled shield vertex element {key}')
                    row[k] = values[name]
                rows.append(row)
        for i in range(steps):
            a, b = start + 2 * i, start + 2 * i + 2
            pair = [(a, a + 1, b), (a + 1, b + 1, b)]
            triangles.extend(pair if side > 0 else [(a, c, b) for a, b, c in pair])
    return replace(template, vdata=b''.join(pack_vertex(template.elems, template.vsize, row) for row in rows),
                   indices=struct.pack('<' + 'H' * (3 * len(triangles)), *(v for t in triangles for v in t)))


def build_model(game, host: str) -> tuple[bytes, bytes]:
    """Return modified MRAB and CAS; only reads Root.cpk."""
    archive = mdb.rab_read(game.read('OBJECT', host + '.MRAB'))
    member = next(f for f in archive.files if f.name.lower() == host.lower() + '.mdb')
    model = mdb.mdb_read(member.data)
    donor_arc = mdb.rab_read(game.read('WEAPON', DONOR))
    donor = mdb.mdb_read(next(f.data for f in donor_arc.files if f.name.endswith('.mdb')))
    material = next(m.index for m in donor.materials if m.shader == 'snd_Chara_FencerEnergyShield')
    template = next(me for ob in donor.objects for me in ob.meshes if me.material == material)
    indices = []
    for name in SHIELD_BONES:
        index = len(model.bones)
        indices.append(index)
        model.names.append(name)
        model.bones.append(mdb.Bone(index, 0, -1, -1, len(model.names) - 1, 0, 3, 0, 1, 0, 0,
                                    mdb.ident(), mdb.ident(), [0., 0., 0., 1.], [0., 0., 0., 1.]))
    model = graft.relink(model)
    model, materials = graft.merge_materials(model, donor, [material])
    # A stock skinned object keeps the renderer's existing vertex skinning path.
    target = next(i for i, ob in enumerate(model.objects) if any(me.flags[1] for me in ob.meshes))
    model = graft.append_meshes(model, [(material, shield_mesh(template, i)) for i in indices], materials, obj=target)
    model = graft.recompute_bounds(model)
    raw = mdb.mdb_write(model)
    # Serialize and parse before publishing an asset; stock model parts stay present.
    assert mdb.mdb_write(mdb.mdb_read(raw)) == raw
    member.stored = mdb.cmpl_compress(raw)
    graft.copy_texture_members(archive, donor_arc, [t.filename for t in donor.textures
                                                  if any(x.texture == t.index for x in donor.materials[material].textures)])
    return mdb.rab_write(archive), animation(game.read('OBJECT', host + '.CAS'))
