"""Seat-local visibility of the native shell replaced by a complete cockpit.

Back-face culling is not an interior filter: window reveals, dashboard backs and armour
seams have front faces too. Keep their original triangles, materials and animation for
outside views, but skin the cabin region to identity children of its original bone.
firstperson.cpp collapses only children whose seat mask includes the local rider, for
that model's colour draw, and restores the pose afterwards.
"""
from __future__ import annotations

from dataclasses import replace

import numpy as np

from mdb import bind_world, ident, read_elem

# These kits replace the native cabin. Stock passenger bays, open seats, motorcycles,
# the sedan's retained interior and remote cameras intentionally retain native geometry.
REPLACEMENTS = frozenset((
    'tank_gunner', 'apc_cab', 'tracked_cab', 'pickup_cab', 'truck_cab', 'kei_cab', 'trailer_cab',
    'heli_gunship', 'heli_gunship_pilot', 'heli_utility', 'jet_f16', 'jet_f16_narrow',
    'plane_deck', 'plane_deck_gun', 'plane_hold', 'heli_cabin', 'passenger',
    'mech_begaruta', 'mech_nix', 'mech_oldrobot', 'mech_groundrobo', 'mech_large',
))
PREFIX = 'vc_shell_'
RETAINED = frozenset(('bike_rider', 'gun_station', 'heli_door_left', 'heli_door_right', 'open_rider',
                      'remote_display', 'sedan_cab', 'troop_bay', 'troop_bay_l', 'troop_bay_r'))


def regions(md, fits, hull):
    """The old cabin's local envelopes, measured BEFORE adding the replacement.

    Twice the axial clearance includes the tapered nose/window reveals beyond the
    first pane. Infinite/open directions get no envelope: never erase a whole model
    because a ray missed. Only triangles rigidly owned by this station's body/turret
    qualify, together with its native doors. Articulated guns, rotors, wheels and
    other stations stay intact. A measured fit override covers unusually long noses.
    """
    import cockpits
    out = []
    for fit in fits:
        if fit.kit not in REPLACEMENTS:
            continue
        if not 0 <= fit.seat < 31:
            raise ValueError('cockpit shell seat mask exceeds 31 seats')
        matrix = np.array(cockpits.frame(fit)).reshape(4, 4)
        room = cockpits.clearance(hull, fit.eye, list(matrix.reshape(-1)))
        extent = np.array([room[k] for k in ('right', 'down', 'back', 'left', 'up', 'ahead')])
        if not np.isfinite(extent).all():
            continue
        low, high = -2 * extent[:3], 2 * extent[3:]
        if fit.shell_ahead is not None:
            if not np.isfinite(fit.shell_ahead) or fit.shell_ahead <= 0:
                raise ValueError('native cabin nose extent must be finite and positive')
            high[2] = max(high[2], fit.shell_ahead)
        out.append((fit.seat, md.bone_index(fit.bone), matrix, low, high))
    return out


# The cabin armour rigid to a mech station's parents: Nix's chest plate is skinned to `spine`, the parent of its
# station's `body` (docs/nix-re.md), and from the pilot's eye it is a glowing sheet across the pod (feedback
# 2026-10-17). The arms and the leg equipment have their own animated bones and stay.
TORSO = frozenset(('spine', 'mune', 'neck'))


def _native_owners(md, bone):
    allowed = {bone}
    ancestor = md.bones[bone].parent
    while ancestor >= 0:
        if md.name_of(md.bones[ancestor].name) in TORSO:
            allowed.add(ancestor)
        ancestor = md.bones[ancestor].parent
    for candidate in md.bones:
        if not md.name_of(candidate.name).lower().startswith('door_'):
            continue
        ancestor = candidate.parent
        while ancestor >= 0 and ancestor != bone:
            ancestor = md.bones[ancestor].parent
        if ancestor == bone:
            allowed.add(candidate.index)
    return allowed


def triangle_masks(md, obj, mesh, envelopes, hidden=()):
    """Per triangle seat mask and original owner; zero means native geometry retained. `hidden`: (seat, bone) pairs
    whose geometry that seat never sees from its cockpit, wherever it is (self_hidden): a triangle all of whose
    weighted influences are such bones, a blended one too."""
    indices = np.frombuffer(mesh.indices, dtype='<u2').reshape(-1, 3)
    masks = np.zeros(len(indices), dtype=np.uint32)
    owner = np.full(mesh.nverts, obj.bone, dtype=np.int64)
    position = next(e for e in mesh.elems if e.name.lower() == 'position')
    pos = np.array(read_elem(mesh, position.name, position.channel), dtype=float)[:, :3]
    if mesh.flags[1]:
        bones = np.array(read_elem(mesh, 'BLENDINDICES', 0))
        weights = np.array(read_elem(mesh, 'BLENDWEIGHT', 0))
        owner = bones[np.arange(len(bones)), weights.argmax(1)]
        rigid = ((bones == owner[:, None]) | (weights == 0)).all(1)
    else:
        world = np.array(bind_world(md)[obj.bone]).reshape(4, 4)
        pos = (np.c_[pos, np.ones(len(pos))] @ world)[:, :3]
        rigid = np.ones(len(pos), dtype=bool)
    stations = {}
    for seat, bone, _matrix, _low, _high in envelopes:
        stations[bone] = stations.get(bone, 0) | (1 << seat)
    # A shared cabin has one replacement shell. A passenger looking toward the
    # flight deck must not see its old window reveals, or vice versa. Open riders
    # and remote gun seats never enter this mask, even on the same vehicle.
    for _seat, bone, matrix, low, high in envelopes:
        eligible = (rigid & np.isin(owner, list(_native_owners(md, bone))))[indices].all(1)
        # The three corners must have one animated owner; a mixed door/body seam
        # stays native rather than collapsing one side of a moving triangle.
        eligible &= (owner[indices] == owner[indices[:, :1]]).all(1)
        if not eligible.any():
            continue
        corners = ((pos - matrix[3, :3]) @ matrix[:3, :3].T)[indices]
        # A large wing/fuselage triangle can cross the cabin box without belonging
        # to its interior. Keep crossing faces intact; never hide an entire wing
        # merely because one corner overlaps a crew region.
        intersects = ((corners >= low) & (corners <= high)).all((1, 2))
        masks[eligible & intersects] |= stations[bone]
    per_seat = {}
    for seat, bone in hidden:
        per_seat.setdefault(seat, set()).add(bone)
    for seat, bones_of in per_seat.items():
        if mesh.flags[1]:
            eligible = ((weights == 0) | np.isin(bones, list(bones_of))).all(1)[indices].all(1)
        else:
            eligible = np.isin(owner, list(bones_of))[indices].all(1)
        masks[eligible] |= 1 << seat
    return masks, owner


def self_hidden(md, fits):
    """(seat, bone) of the native parts a seat's complete cockpit replaces whatever their distance: a tank_gunner
    cabin is the turret's inside, so the station bone's whole subtree (the turret's own guns, mantlet and breech that
    crossed the cabin's visor: the twin-gun artillery's cannon_l / _r and their slides, the Titan's; feedback
    2026-10-17). Only that seat's first-person colour draw; outside views and its optic keep the animated originals."""
    out = []
    for fit in fits:
        if fit.kit == 'tank_gunner':
            station = md.bone_index(fit.bone)
            if station < 0:
                raise ValueError(f'cockpit station bone missing: {fit.bone}')
            for candidate in md.bones:
                ancestor = candidate.index
                while ancestor >= 0 and ancestor != station:
                    ancestor = md.bones[ancestor].parent
                if ancestor == station:
                    out.append((fit.seat, candidate.index))
    return out


def native_bones(md):
    """Each bone's native owner: a visibility clone (PREFIX, separate) stands for its parent, any other bone for itself.
    A check of which bone carries which geometry reads skin indices through this, before or after the graft."""
    return [b.parent if md.name_of(b.name).startswith(PREFIX) else b.index for b in md.bones]


def _slice(mesh, triangles, clone_of):
    """An exact vertex subset, each weighted influence moved to its clone (`clone_of`: original bone -> clone): a
    blended vertex keeps every influence and weight, so the outside pose is the original's exactly."""
    used, inverse = np.unique(triangles.reshape(-1), return_inverse=True)
    data = bytearray(b''.join(mesh.vdata[int(i) * mesh.vsize:(int(i) + 1) * mesh.vsize] for i in used))
    elem = next(e for e in mesh.elems if e.name.upper() == 'BLENDINDICES')
    if elem.fmt != 21:
        raise ValueError('unsupported shell skin-index format')
    # Zero-weight entries keep their original (in-palette) bone.
    for i in range(len(used)):
        at = i * mesh.vsize + elem.offset
        for k in range(4):
            if data[at + k] in clone_of:
                data[at + k] = clone_of[data[at + k]]
    return replace(mesh, vdata=bytes(data), indices=inverse.astype('<u2').tobytes())


def separate(md, fits, hull):
    """Preserve the native model, adding per-seat shell visibility children.

    The installer already skins the aircraft it copies; other replacement cabins
    also use skinned source meshes. Refuse an eligible rigid source rather than
    silently assigning bone-local coordinates to a model-space skin.
    """
    import vehicle_optics
    envelopes = regions(md, fits, hull)
    hidden = self_hidden(md, fits)
    if not envelopes and not hidden:
        return md
    world = bind_world(md)
    clones, objects = {}, []
    for obj in md.objects:
        meshes = []
        for mesh in obj.meshes:
            if md.name_of(md.materials[mesh.material].name).startswith('edf6vc_cpit_'):
                meshes.append(mesh)
                continue
            masks, owner = triangle_masks(md, obj, mesh, envelopes, hidden)
            indices = np.frombuffer(mesh.indices, dtype='<u2').reshape(-1, 3)
            if (masks == 0).any():
                meshes.append(replace(mesh, indices=indices[masks == 0].astype('<u2').tobytes()))
            for mask in np.unique(masks[masks != 0]):
                selected = indices[masks == mask]
                if not mesh.flags[1]:
                    raise ValueError('replacement cabin has an unskinned native shell')
                used = np.unique(selected)
                influence = np.asarray(read_elem(mesh, 'BLENDINDICES', 0))[used]
                weight = np.asarray(read_elem(mesh, 'BLENDWEIGHT', 0))[used]
                clone_of = {}
                for parent in np.unique(influence[weight > 0]):
                    key = (int(mask), int(parent))
                    if key not in clones:
                        name = PREFIX + str(key[0]) + '_' + str(key[1])
                        if len(md.bones) >= 256:
                            raise ValueError('cockpit shell exceeds the MDB ubyte skin-index limit')
                        md = vehicle_optics.add_marker(md, md.name_of(md.bones[parent].name), name, world[parent])
                        bones = list(md.bones)
                        # The stored inverse bind is authoritative, including the
                        # native exporter's rounding. Do not recompute it from the
                        # accumulated parent transforms and move exterior vertices.
                        bones[-1] = replace(bones[-1], kind=3, bounded=1, local=ident(),
                                            inv_bind=list(md.bones[parent].inv_bind))
                        md = replace(md, bones=bones)
                        clones[key] = len(bones) - 1
                    clone_of[int(parent)] = clones[key]
                meshes.append(_slice(mesh, selected, clone_of))
        objects.append(replace(obj, meshes=[replace(me, mesh_index=i) for i, me in enumerate(meshes)]))
    return replace(md, objects=objects, buffer_order=None)
