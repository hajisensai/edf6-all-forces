"""Four illuminated launch pods on the salvo interceptor's native store muzzles."""
from dataclasses import replace

import dreadnought_model as materials
import obj_model
import procmesh
import texfile
import texfile_accel
import vcobjects
from jet_hardpoints import HP_STORE
from mdb import bind_world, cmpl_compress, mdb_read, mdb_write, rab_read, rab_write

BASE_ARCHIVE = 'EDF6VC_INTERCEPTOR.MRAB'
MODEL_MEMBER = 'bomber501_2.mdb'


def _solid(rgba):
    return texfile.Image(16, 16, bytearray(rgba * (16 * 16)))


def build(game, base: bytes) -> bytes:
    rab = rab_read(base)
    member = next(entry for entry in rab.files if entry.name.lower() == MODEL_MEMBER)
    md = mdb_read(member.data)
    mount = md.bone_index(HP_STORE)
    if mount < 0:
        raise ValueError('Salvo interceptor has no store hardpoint')
    x, y, z = bind_world(md)[mount][12:15]
    offsets = vcobjects._stock_muzzles(game, vcobjects.JET_MISSILE_STOCK)
    if len(offsets) != 4:
        raise ValueError(f'Salvo launcher needs four nozzles, found {len(offsets)}')

    host, template = materials._host(game, materials.LIGHT_HOST)
    normal = 'edf6vc_salvo_nm.dds'
    obj_model.add_texture(rab, normal, texfile_accel.bc5_normal_dds(_solid((128, 128, 255, 255))))
    meshes = list(md.objects[0].meshes)
    skin = [(mount, 1.0)]
    for name, albedo, mask in (
        ('housing', (47, 56, 64, 255), 0),
        ('emitter', (245, 176, 72, 255), 255),
    ):
        stem = f'edf6vc_salvo_{name}'
        diffuse, params = f'{stem}_df.dds', f'{stem}_rm.dds'
        obj_model.add_texture(rab, diffuse, texfile_accel.bc7_dds(_solid(albedo)))
        obj_model.add_texture(rab, params, texfile_accel.dxt1_dds(_solid((138, 108, mask, 255))))
        md, material = obj_model.add_material(md, host, materials.LIGHT_HOST[2], stem,
                                              dict(zip(materials.LIGHT_SLOTS, (diffuse, normal, params))))
        md = materials._with_light(md, material, 3.0)
        part = procmesh.Part(material)
        for dx, dy, dz in offsets:
            mouth = (x + dx, y + dy, z + dz)
            if name == 'housing':
                start = (mouth[0], mouth[1], mouth[2] - 2.0)
                end = (mouth[0], mouth[1], mouth[2] - 0.06)
                procmesh.hard_cylinder(part, start, end, 0.23, skin, segments=12, end_radius=0.18)
                procmesh.hard_box(part, (mouth[0], mouth[1] + 0.28, mouth[2] - 1.03), (0.11, 0.23, 0.55), skin)
            else:
                start = (mouth[0], mouth[1], mouth[2] - 0.10)
                procmesh.hard_cylinder(part, start, mouth, 0.175, skin, segments=12, cap=False)
        uv = {(material, elem.channel): (0.0, 0.0, 1.0, 1.0)
              for elem in template.elems if elem.name.lower() == 'texcoord'}
        vertices, indices = procmesh.pack(part, template, uv, 1.0)
        meshes.append(replace(template, material=material, mesh_index=len(meshes), vdata=vertices, indices=indices))
    md = replace(md, objects=[replace(md.objects[0], meshes=meshes), *md.objects[1:]], buffer_order=None)
    rab.files[rab.files.index(member)] = replace(member, stored=cmpl_compress(mdb_write(md)))
    problems = obj_model.texture_problems(rab, md, MODEL_MEMBER)
    if problems:
        raise ValueError('; '.join(problems))
    return rab_write(rab)
