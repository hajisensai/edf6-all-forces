"""Real generated sedan geometry, contact tree, empty weapon setup and installer ownership.
Read-only CPK input; all installation writes target a temporary directory.
"""
from pathlib import Path
import math
import struct
import sys
import tempfile

sys.path[:0] = [str(Path(__file__).resolve().parents[1]/p) for p in ('tools','pylib')]
import calls
import call_weapons
import graft_pure as graft
import make_sedan as sedan
import obj_model
import rootcpk
import sgo
import dsgo
import sidecar_model
import vcobjects
from mdb import bind_world, mdb_read, rab_read


def main():
    root = rootcpk.DEFAULT_GAME
    if not Path(root, 'Root.cpk').is_file() or not Path(root, 'Chunk02.cpk').is_file():
        print('SKIP: optional EDF6 archives unavailable');return 77
    game = rootcpk.Game(root)
    files = sedan.build(root)
    arc = rab_read(files['OBJECT/EDF6VC_SEDAN.MRAB'])
    md = mdb_read(next(f.data for f in arc.files if f.name == sedan.MODEL))
    assert not obj_model.texture_problems(arc, md, sedan.MODEL)
    world = bind_world(md)
    eye = world[md.bone_index('vc_eye_0')][12:15]
    assert max(abs(x-y) for x,y in zip(eye,sedan.EYE)) < 1e-4
    counts = {w:0 for w in sedan.WHEELS}
    shell = 0
    for obj in md.objects:
        for mesh in obj.meshes:
            material = md.name_of(md.materials[mesh.material].name)
            if not material.startswith('Car_'):continue
            bi, bw = graft.skin_columns(mesh)
            for t in graft.triangles(mesh):
                names = {md.name_of(md.bones[int(bi[i][0])].name) for i in t}
                assert len(names) == 1, ('mixed sedan triangle', names)
                name = names.pop()
                assert all(bw[i][0] == 1 and sum(bw[i][1:]) == 0 for i in t)
                if name in counts:counts[name] += 1
                else:assert name == 'body';shell += 1
    assert set(counts.values()) == {92}, counts
    assert sum(counts.values())+shell == 2859+68, 'all source geometry retained, truck geometry absent'
    raw = files['OBJECT/EDF6VC_SEDAN.SHKT']
    for body in ('body','door_l','door_r',*sedan.WHEELS):
        shape = sidecar_model._Shkt(raw, 'RagDollProxys.'+body)
        leaves = shape.leaves()
        bounds = shape.aabb
        for i in range(shape.inst_n):
            lo,hi = shape.box(i)
            assert all(leaves[i][0][k] <= lo[k]+1e-4 and leaves[i][1][k] >= hi[k]-1e-4 for k in range(3)), (body,i,'tree')
            assert all(bounds[k] <= lo[k]+1e-4 and bounds[4+k] >= hi[k]-1e-4 for k in range(3)), (body,i,'compound')
            for plane in shape.planes(i):
                assert abs(sum(x*x for x in plane[:3])-1) < 1e-4
                assert max(sum(plane[k]*p[k] for k in range(3))+plane[3] for p in shape.vertices(i)) < 2e-4
        if body == 'body':
            assert max(shape.box(i)[1][1]+.849 for i in range(shape.inst_n)) <= 1.401
    version, m = sgo.read(files['OBJECT/EDF6VC_SEDAN.SGO'])
    assert m['xgs_scene_object_class'] == 'Vehicle_Car'
    assert len(m['vehicle_riding_position']) == 1 and m['vehicle_weapon_setting'] == [] and m['vehicle_setup'][-1] == []
    assert len(m['car_base_wheel']) == 4 and 'mission_setup' in m
    call = next(c for c in calls.CALLS if c.ground == 'sedan')
    template = game.read('WEAPON', call_weapons.template_of(call)+'.SGO')
    request = dsgo.parse(call_weapons.vehicle_sgo(template,call,(1.,1.))).root.get('Ammo_CustomParameter').items[4]
    assert request.items[2] == 'app:/object/edf6vc_sedan.sgo'
    assert request.items[3].items[1].items == list(vcobjects.GROUND_VEHICLES['sedan'].setup)
    assert request.items[3].items[-1].items == [], 'an unarmed car must not inherit template rocket weapons'
    with tempfile.TemporaryDirectory(prefix='edf-sedan-') as tmp:
        sedan.install(tmp, files)
        for name,data in files.items():assert Path(tmp,'Mods',name).read_bytes() == data
        deleted,kept = sedan.remove(tmp)
        assert len(deleted) == 3 and not kept
        assert all(not Path(tmp,'Mods',name).exists() for name in files)
    print('sedan: four native animated wheels, complete source geometry/textures, cabin eye, contact tree, unarmed request and uninstall OK')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
