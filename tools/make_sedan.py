"""Build a drivable civilian sedan from EDF6's own city prop and native four-wheel car.

Read-only sources: Chunk02.cpk MAP/IG_2000MCITY.RAB sk_car_sedan01.mdb and
Root.cpk V512_KEITRUCK's skeleton, handling and physics. The four disconnected
tyres are skinned to the native wheel bones; body contact hulls fit the lower
sedan silhouette. The original interior supplies the wheel, seats and dashboard.
`--out DIR` only writes reviewable assets outside the game; installation uses the ledger.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import math
from pathlib import Path
import struct
import sys
from dataclasses import replace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
import cockpits
import cockpit_kits
import graft_pure as graft
import ledger
import model_lod
import rootcpk
import sgo
import texfile
import sidecar_model
import vcobjects as vc
import vehicle_optics
from mdb import Rab, RabFile, bind_world, cmpl_compress, mdb_read, mdb_write, rab_read, rab_write
from mdb_jet import pack_vertex, vertex_table

OWNER = 'sedan'
STEM = 'EDF6VC_SEDAN'
MODEL = 'edf6vc_sedan.mdb'
SOURCE = 'sk_car_sedan01.mdb'
DIGEST = '4ead8979524c6664cfc2a686f57ffab6ccb019ded833938298f48a3f95259fdb'
EYE = (-0.35, 1.19, 0.12)
FIT = (cockpits.Fit(0, 'body', EYE, 'sedan_cab'),)
HOST = 'V512_KEITRUCK'
WHEELS = ('tireF_l', 'tireF_r', 'tireB_l', 'tireB_r')


def wheel_groups(mesh):
    """Four whole welded tyre components, guarded by the exact source model digest."""
    positions = graft.mesh_positions(mesh)
    parents = list(range(len(positions)))
    welded = {}
    def root(i):
        while parents[i] != i:
            parents[i] = parents[parents[i]]
            i = parents[i]
        return i
    for i, p in enumerate(positions):
        key = tuple(round(x, 4) for x in p)
        if key in welded:
            parents[root(i)] = root(welded[key])
        else:
            welded[key] = i
    for tri in graft.triangles(mesh):
        for j in tri[1:]:
            parents[root(j)] = root(tri[0])
    components = {}
    for i in range(len(positions)):
        components.setdefault(root(i), []).append(i)
    groups = {}
    for ids in components.values():
        lo = [min(positions[i][k] for i in ids) for k in range(3)]
        hi = [max(positions[i][k] for i in ids) for k in range(3)]
        if hi[1] >= .7 or hi[2]-lo[2] < .6 or hi[0]-lo[0] > .3:
            continue
        name = 'tire' + ('F' if lo[2] > 0 else 'B') + ('_l' if lo[0] > 0 else '_r')
        if name in groups:
            raise ValueError('sedan tyre topology is ambiguous: '+name)
        groups[name] = (ids, [(a+b)/2 for a,b in zip(lo, hi)])
    if set(groups) != set(WHEELS):
        raise ValueError('sedan must have four separate native wheels')
    return groups


def model(game):
    city = rab_read(rootcpk.Game(game.root, 'Chunk02.cpk').read('MAP', 'IG_2000MCITY.RAB'))
    source = next(f.data for f in city.files if f.name.lower() == SOURCE)
    if hashlib.sha256(source).hexdigest() != DIGEST:
        raise ValueError('sedan source geometry differs from the measured model')
    donor = mdb_read(source)
    arc = rab_read(game.read('OBJECT', HOST+'.MRAB'))
    host = mdb_read(next(f.data for f in arc.files if f.name.lower() == HOST.lower()+'.mdb'))
    groups = wheel_groups(donor.objects[0].meshes[0])
    world = bind_world(host)
    front, back = groups['tireF_l'][1], groups['tireB_l'][1]
    hf, hb = (world[host.bone_index(w)][12:15] for w in ('tireF_l', 'tireB_l'))
    scale = (hf[0]/front[0], hf[1]/front[1], (hf[2]-hb[2])/(front[2]-back[2]))
    shift = (0., 0., hf[2]-front[2]*scale[2])
    host = replace(host, objects=[], buffer_order=None)
    host, materials = graft.merge_materials(host, donor, range(len(donor.materials)))
    # Map-object shaders require map-instance colour data absent on a drivable
    # AnimationModel. Use the native car's material layouts with the prop textures.
    for index, source_mat in enumerate(donor.materials):
        target = host.materials[materials[index]]
        template = host.materials[1 if 'Alpha' in source_mat.shader else 0]
        slots = [replace(slot, kind='param_r_m_occ_hr' if slot.kind.startswith('param_') else slot.kind)
                 for slot in target.textures]
        params = [replace(p, value=([.12,.12,.12,0.] if p.name == 'specular_color' else
                                    [.25,1.,1.,0.] if p.name == 'metallic' else
                                    [.8,1.,1.,0.] if p.name == 'roughness' else p.value)) for p in template.params]
        host.materials[materials[index]] = replace(template, index=target.index, name=target.name, textures=slots, params=params)

    owner = {i: host.bone_index(name) for name,(ids,_p) in groups.items() for i in ids}
    meshes = []
    for mi, mesh in enumerate(donor.objects[0].meshes):
        keys, rows = vertex_table(mesh)
        for i, row in enumerate(rows):
            for j, key in enumerate(keys):
                name = key.split(':')[0]
                values = list(row[j])
                if name.lower() == 'position':
                    values[:3] = [values[k]*scale[k]+shift[k] for k in range(3)]
                elif name.lower() in ('normal', 'tangent', 'binormal'):
                    values[:3] = [values[k]/scale[k] if name.lower() == 'normal' else values[k]*scale[k] for k in range(3)]
                    length = math.sqrt(sum(x*x for x in values[:3])) or 1.0
                    values[:3] = [x/length for x in values[:3]]
                elif name.upper() == 'BLENDINDICES':
                    values = [owner.get(i, host.bone_index('body')) if mi == 0 else host.bone_index('body'), 0, 0, 0]
                elif name.upper() == 'BLENDWEIGHT':
                    values = [1., 0., 0., 0.]
                row[j] = tuple(values)
        mesh = replace(mesh, material=materials[mesh.material], vdata=b''.join(pack_vertex(mesh.elems, mesh.vsize, r) for r in rows))
        meshes.append((mesh.material, mesh))
    host = graft.append_meshes(host, meshes, {m:m for m,_me in meshes}, new_object=('sedan', host.bone_index('v512_KeiTruck')))
    host = graft.recompute_bounds(host)
    host = vehicle_optics.add_marker(host, 'body', 'vc_sedan', cockpits.frame(FIT[0]))
    # Glass is clear from inside; retain its authored transparency while removing map-prop noise.
    host = replace(host, materials=[replace(mat, params=[
        replace(p, value=([0., *p.value[1:]] if p.name == 'normal_amount' else
                          [1.,1.,1.,.08] if p.name == 'diffuse' else
                          [.08,.08,.08,p.value[3]] if p.name == 'specular_color' else p.value))
        for p in mat.params]) if 'Alpha' in mat.shader else mat for mat in host.materials])
    # The original interior is sufficient for first person even without optional cockpit assets.
    if cockpits.assets_present():
        host, textures = cockpits.graft(host, FIT, cockpit_kits.KITS, game, vehicle_optics.preorder)
    else:
        host = vehicle_optics.preorder(vehicle_optics.add_marker(host, 'body', 'vc_eye_0', cockpits.frame(FIT[0])))
        textures = {}
    # Retain texture dependencies of unused native material slots (headlight controllers still name them).
    # No old truck shell, wheel or instrument geometry remains.
    output = Rab(arc.version, ['TEXTURE', 'MODEL', 'HD-TEXTURE'], [RabFile(MODEL, 1, 0, cmpl_compress(mdb_write(host)))])
    graft.copy_texture_members(output, arc, [t.filename for t in host.textures if t.filename.lower() in {f.name.lower() for f in arc.files}])
    graft.copy_texture_members(output, city, [t.filename for t in host.textures if t.filename.lower() in {f.name.lower() for f in city.files}])
    # Map assets use an extra final .lod mip and one BC7/DX10 glass texture. Normalize
    # those to the vehicle loader's HD/.lod contract; RGBA preserves the glass alpha.
    for texture in host.textures:
        member = next((f for f in output.files if f.name.lower() == texture.filename.lower()), None)
        if member is None:continue
        raw = member.data
        try:
            texfile.dds_info(raw)
            convert = 'fpr' in texture.filename.lower() and texture.filename.lower().startswith('sk_car_')
        except texfile.TextureError:
            convert = True
        if convert:
            from PIL import Image
            rgba = Image.open(io.BytesIO(raw)).convert('RGBA')
            if 'fpr' in texture.filename.lower():
                r,g,_mask,a = rgba.split()
                rgba = Image.merge('RGBA', (r,g,Image.new('L',rgba.size,255),a))
            levels = texfile.mip_chain(texfile.Image(rgba.width, rgba.height, bytearray(rgba.tobytes())))
            raw = texfile._header(rgba.width, rgba.height, len(levels), b'', 32,
                                  (0xff,0xff00,0xff0000,0xff000000), rgba.width*4) + b''.join(bytes(i.rgba) for i in levels)
        textures[texture.filename] = raw
    cockpits.add_textures(output, textures)
    return rab_write(output)


def collision(game, members):
    """Retain native suspension/tyres; refit the five body hulls to chassis, cabin, hood and boot."""
    name = members['ragdoll'][0].rsplit('/', 1)[-1]
    raw = game.read('OBJECT', name)
    # pylib/hkshape.py rebuilds each refitted hull (planes, faces, mass properties) and the compound's tree and box.
    shape = sidecar_model._Shkt(raw)
    if shape.inst_n != 5 or not all(shape.identity(k) for k in range(5)):
        raise ValueError('native car body collision changed')
    body_y = .849
    boxes = [((-.73, .58, -1.05), (.73, 1.40, .88)),
             ((-.84, .35, .88), (.84, .78, 2.12)),
             ((-.84, .24, -1.91), (.84, .50, 2.12)),
             ((-.84, .45, -1.91), (.84, .88, -1.05)),
             ((-.84, .30, -1.94), (.84, .52, -1.87))]
    for k,(lo,hi) in enumerate(boxes):
        radius = shape.radius(k)
        old = shape.vertices(k)
        lower, upper = [min(v[c] for v in old) for c in range(3)], [max(v[c] for v in old) for c in range(3)]
        target_lo = [lo[c]-(body_y if c == 1 else 0)+radius for c in range(3)]
        target_hi = [hi[c]-(body_y if c == 1 else 0)-radius for c in range(3)]
        scale = [(target_hi[c]-target_lo[c])/(upper[c]-lower[c]) for c in range(3)]
        shift = [target_lo[c]-lower[c]*scale[c] for c in range(3)]
        shape.replace(k, [[scale[0], 0, 0], [0, scale[1], 0], [0, 0, scale[2]]], shift, radius=radius)
    shape.refresh()
    # Original doors are not moving parts of the sedan. Their contacts stay inside the lower chassis: each door hull
    # shrunk to a millimetre-scale copy (its vertices and radius x 0.001) at a point there. No instance scale: no
    # stock file scales an instance, and the game keeps a scaled hull's radius.
    for door in ('door_l', 'door_r'):
        s = sidecar_model._Shkt(shape.bytes(), 'RagDollProxys.'+door)
        point = (-.15 if door.endswith('_l') else .15, -.83, -1.75)
        for k in range(s.inst_n):
            s.replace(k, [[.001, 0, 0], [0, .001, 0], [0, 0, .001]], point)
        s.refresh()
        shape = s
    out = shape.bytes()
    # The chassis keeps the truck's tuned massDistribution (centre of mass, inertia: its handling), and so do the
    # inert doors: the bodies pylib/shkt_check.py may flag as kept, no other.
    import shkt_check
    kept = sorted(p.where for p in shkt_check.stale_mass_distributions(out, raw))
    if kept != ["body 'RagDollProxys.body'", "body 'RagDollProxys.door_l'", "body 'RagDollProxys.door_r'"]:
        raise ValueError(f'sedan mass distributions kept from the truck: {kept}')
    return out


def build(root):
    game = rootcpk.Game(root)
    version, m = sgo.read(game.read('OBJECT', HOST+'.SGO'))
    m['animation_model'][0] = ['app:/object/edf6vc_sedan.mrab', MODEL]
    mab = bytearray(m['animation_model'][2])
    # Preserve native locator names and their SGO payloads, only move the driver/camera locators in the body frame.
    for name, point in ((m['vehicle_riding_position'][0][1], (EYE[0], EYE[1]-.849-.70, EYE[2])),
                        (m['vehicle_riding_position'][0][2][0], (0., 1.7-.849, -5.5)),
                        (m['vehicle_riding_position'][0][2][0]+'注目', (0., 1.0-.849, 1.0)),
                        (m['vehicle_riding_position'][0][0], (-1.1, -.55, EYE[2]))):
        at,_ = vc.mab_locator(bytes(mab), name)
        struct.pack_into('<3f', mab, at, *point)
    m['animation_model'][2] = bytes(mab)
    m['vehicle_riding_position'] = m['vehicle_riding_position'][:1]
    m['vehicle_weapon_setting'] = []
    m['vehicle_setup'][-1] = []
    m['game_object_durability'] = 300.
    shkt = collision(game, m)
    m['ragdoll'][0] = 'app:/object/edf6vc_sedan.shkt'
    # its model merged by material and given its LODs (pylib/model_lod.py)
    return model_lod.finish({'OBJECT/'+STEM+'.MRAB': model(game), 'OBJECT/'+STEM+'.SHKT': shkt,
                             'OBJECT/'+STEM+'.SGO': sgo.write(version, m)})


def install(root, files):
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = [led.put(OWNER, rel, data) for rel,data in files.items()]
    led.release(OWNER, sorted(before-{ledger.key(r) for r in files}))
    return paths


def remove(root):
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game', default=rootcpk.DEFAULT_GAME)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    out = Path(a.out).resolve()
    if out.is_relative_to(Path(a.game).resolve()):
        ap.error('--out must be outside the game directory')
    for name,data in build(a.game).items():
        p = out/name;p.parent.mkdir(parents=True, exist_ok=True);p.write_bytes(data)
        print(p)


if __name__ == '__main__':
    main()
