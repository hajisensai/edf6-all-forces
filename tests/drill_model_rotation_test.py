"""The whole connected rear cone must be on the spinning bone, including its triangles behind the old cut.

Always run the synthetic crossing-component regression. With the private OBJ present also check all six source
pieces. Pass --archive <generated EDF6VC_DRILL.MRAB> to verify the actual emitted skin and rotate its real palette.
"""
from dataclasses import replace
from pathlib import Path
import argparse
import math
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
import drill_model as drill
import obj_model as obj
import graft_pure as graft
from mdb import bind_world, mdb_read, mmul, rab_read


def half(point):
    return tuple(struct.unpack('<e', struct.pack('<e', x))[0] for x in point)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    # Both faces belong to one connected fluted piece. One crosses the cut; the other lies wholly in front.
    points = [(0, 4, 3.4), (1, 4, 5.2), (0, 5, 5.2), (0, 4, 6.0)]
    part = obj.Part('rear-cone', 'MI_Tank_C', [obj.Vertex(p, (0, 1, 0), (0, 0)) for p in points], [(0, 1, 2), (1, 3, 2)])
    hull, rotating = drill.split([part])
    assert not hull and sum(len(p.tris) for p in rotating) == 2, 'rear cone was cut between static and spinning bones'
    behind = obj.Part('hull', 'MI_Tank_C', [obj.Vertex((x, y, z - 4), (0, 1, 0), (0, 0)) for x, y, z in points], part.tris)
    hull, rotating = drill.split([part, behind])
    assert sum(len(p.tris) for p in hull) == 2 and sum(len(p.tris) for p in rotating) == 2
    # Every OBJ material is drawn with a real source texture (the lower hull was a plain steel, 2026-10-09).
    assert set(drill.MATERIALS) == {'MI_Tank_C', 'MI_Tank_B_CS'}
    assert all(src in drill.ALBEDO_TEX for _, src in drill.MATERIALS.values()), 'an OBJ material has no albedo texture'
    private = drill.obj_path()
    if not private:
        assert not args.archive, 'private OBJ required for archive correspondence'
        print('drill_model_rotation_test: crossing component passed; private resource checks skipped')
        return
    source = obj.read_obj(private)
    cover = drill.texture_coverage(source)
    # MI_Tank_B_CS lies on the same atlas: its UVs fill the islands MI_Tank_C leaves (wheels, tracks, lower plates).
    assert cover['MI_Tank_B_CS'] > 0.3 and cover['all'] > 1.6 * cover['MI_Tank_C'], cover
    hull, rotating = drill.split(obj.obj_parts(source, drill.CONVERSION))
    assert len(rotating) == 6, f'expected rear cone + four forward cones + tip, got {len(rotating)}'
    rear = min(rotating, key=lambda p: p.box()[0][2])
    assert len(rear.tris) == 384 and rear.box()[0][2] < 3.35 and rear.box()[1][2] > 5.26
    base, length, radius = drill.drill_axis(rotating)
    assert abs(base[2] - drill.DRILL_BASE[2]) < 0.01 and abs(length - drill.DRILL_LENGTH) < 0.01
    assert abs(radius - drill.DRILL_RADIUS) < 0.003
    if not args.archive:
        print('drill_model_rotation_test: synthetic and six private OBJ components passed; emitted archive not requested')
        return
    blob = args.archive.read_bytes()
    drill.check(blob)
    md = mdb_read(drill.member(rab_read(blob), drill.HOST_MDB).data)
    assert not drill.drawn_albedo_problems(md)
    # Negative control: the lower hull back on the steel the old generator gave it is caught.
    steel = next(t.index for t in md.textures if t.filename.lower() == drill.STEEL_TEX)
    lower = next(m.index for m in md.materials if md.name_of(m.name) == drill.MATERIALS['MI_Tank_B_CS'][0])
    old_mats = [replace(m, textures=[replace(x, texture=steel) if x.kind.lower() == 'albedo' else x for x in m.textures])
                if m.index == lower else m for m in md.materials]
    assert drill.drawn_albedo_problems(replace(md, materials=old_mats)), 'untextured lower hull not detected'
    spin = md.bone_index(drill.SPIN_BONE)
    skins = {}
    for model_object in md.objects:
        for mesh in model_object.meshes:
            indices, weights = graft.skin_columns(mesh)
            for point, ix, wt in zip(graft.mesh_positions(mesh), indices, weights):
                skins.setdefault(point, set()).update(int(b) for b, w in zip(ix, wt) if w > 0.001)
    for i, piece in enumerate(rotating):
        for vertex in piece.verts:
            assert spin in skins.get(half(vertex.pos), set()), f'piece {i}: vertex {vertex.pos} not on spin bone'
    world = list(bind_world(md)[spin])
    a = 0.11  # not the mesh's 1/16-turn repeat, so visible flutes must move
    c, s = math.cos(a), math.sin(a)
    world[:3], world[4:7] = [c, s, 0], [-s, c, 0]
    palette = mmul(md.bones[spin].inv_bind, world)
    for i, piece in enumerate(rotating):
        point = max((half(v.pos) for v in piece.verts), key=lambda p: math.hypot(p[0] - base[0], p[1] - base[1]))
        moved = graft.xform(point, palette)
        assert math.dist(point, moved) > 0.02 and abs(point[2] - moved[2]) < 1e-4, f'piece {i} does not rotate axially'
    print('drill_model_rotation_test: source six pieces, emitted skin, full archive checks and all six real palette rotations passed')


if __name__ == '__main__':
    main()
