"""Portable real collision consumers for the frozen installer's package gate.

The tiny tagfile is synthetic: three static bodies share one cube hull. No game
or test module is read. Keep its schema independent of shkt_check's constants so
that importing a CLI wrapper under that library name cannot satisfy the check.
"""
from __future__ import annotations

import struct

import numpy as np

import ragdoll_fit as rf
import sgo
import shkt_check as sc
import tracked_collision as tc
from mdb import Bone, Mdb


def _sample() -> tuple[bytes, int]:
    body_type = 'hknpPhysicsSystemData::bodyCinfoWithAttachment'
    specs = [
        ('hkRootLevelContainer', 4, None, {'namedVariants': (0, 'hkUint32')}),
        ('hkRootLevelContainer::NamedVariant', 12, None, {}),
        ('hknpPhysicsSceneData', 4, None, {'systemDatas': (0, 'hkUint32')}),
        ('hknpPhysicsSystemData', 16, None, {'materials': (0, 'hkUint32'),
         'motionProperties': (4, 'hkUint32'), 'bodyCinfos': (8, body_type), 'constraintCinfos': (12, 'hkUint32')}),
        (body_type, 64, None, {'shape': (0, 'hknpConvexShape'), 'collisionFilterInfo': (4, 'hkUint32'),
         'materialId': (8, 'hkUint32'), 'motionPropertiesId': (10, 'hkUint32'), 'motionType': (12, 'hkUint32'),
         'name': (16, 'char'), 'position': (20, 'hkVector3'), 'orientation': (32, 'hkVector4'),
         'mass': (48, 'hkUint32'), 'massDistribution': (52, 'hkUint32')}),
        ('hknpShape', 4, None, {'properties': (0, 'hknpShapeProperties')}),
        ('hknpShapeProperties', 4, None, {'properties': (0, 'hknpShapeProperties::Property')}),
        ('hknpConvexHull', 24, None, {'vertices': (0, 'hkVector3'), 'planes': (4, 'hkVector4'),
         'faces': (8, 'Face'), 'indices': (12, 'char')}),
        ('hknpConvexShape', 40, 'hknpShape', {'convexRadius': (4, 'hkUint32'),
         'maxAllowedPenetration': (8, 'hkUint32'), 'hull': (16, 'hknpConvexHull')}),
        ('hknpShapeProperties::Property', 8, None, {'object': (0, 'hknpShapeMassProperties'), 'key': (4, 'hkUint32')}),
        ('hknpShapeMassProperties', 32, None, {}), ('hkUint32', 4, None, {}), ('char', 1, None, {}),
        ('hkVector3', 12, None, {}), ('hkVector4', 16, None, {}), ('Face', 4, None, {}),
    ]
    types = {name: i + 1 for i, (name, _, _, _) in enumerate(specs)}
    fields = list(dict.fromkeys(name for _, _, _, members in specs for name in members))

    def vi(value):
        if value < 128:
            return bytes([value])
        if value < 16384:
            return bytes([0x80 | (value >> 8), value & 255])
        raise ValueError(value)

    def section(name, payload, leaf=True):
        return struct.pack('>I', len(payload) + 8 | (0x40000000 if leaf else 0)) + name.encode() + payload

    tst = b''.join(name.encode() + b'\0' for name, _, _, _ in specs)
    fst = b''.join(name.encode() + b'\0' for name in fields)
    tna = vi(len(specs) + 1) + b''.join(vi(i) + vi(0) for i in range(len(specs)))
    tbd = bytearray()
    for name, size, parent, members in specs:
        tbd += b''.join(vi(x) for x in (types[name], types.get(parent, 0), 0x28, size, 16, len(members)))
        for field, (offset, kind) in members.items():
            tbd += b''.join(vi(x) for x in (fields.index(field), 0, offset, types[kind]))
    data = bytearray()
    items = [(0, 0, 0)]

    def add(kind, blob, count=1):
        data.extend(bytes(-len(data) % 16))
        index = len(items)
        items.append((types[kind] | ((16 if count == 1 else 32) << 24), len(data), count))
        data.extend(blob)
        return index

    def put(index, offset, fmt, *values):
        struct.pack_into(fmt, data, items[index][1] + offset, *values)

    root = add('hkRootLevelContainer', bytes(4))
    variants = add('hkRootLevelContainer::NamedVariant', bytes(12))
    class_name = add('char', b'hknpPhysicsSceneData\0', 20)
    scene = add('hknpPhysicsSceneData', bytes(4))
    systems = add('hkUint32', bytes(4))
    system = add('hknpPhysicsSystemData', bytes(16))
    library = add('hkUint32', bytes(4))
    points = np.asarray([(x, y, z) for x in (-1., 1.) for y in (-1., 1.) for z in (-1., 1.)])
    planes = np.asarray([(1., 0., 0., -1.), (-1., 0., 0., -1.), (0., 1., 0., -1.),
                         (0., -1., 0., -1.), (0., 0., 1., -1.), (0., 0., -1., -1.)])
    polygons = [(4, 5, 7, 6), (0, 2, 3, 1), (2, 6, 7, 3), (0, 1, 5, 4), (1, 3, 7, 5), (0, 4, 6, 2)]
    vertices = add('hkVector3', points.astype('<f4').tobytes(), 8)
    plane_item = add('hkVector4', planes.astype('<f4').tobytes(), 6)
    faces = add('Face', b''.join(struct.pack('<HBB', 4 * i, 4, 127) for i in range(6)), 6)
    indices = add('char', bytes(v for polygon in polygons for v in polygon), 24)
    mass = add('hknpShapeMassProperties', tc._mass_cache(np.asarray(((-1.05,) * 3, (1.05,) * 3)), 2.))
    properties = add('hknpShapeProperties::Property', struct.pack('<IH2x', mass, 0xF100))
    convex = bytearray(40)
    struct.pack_into('<IfH', convex, 0, properties, .05, 0x3c23)
    struct.pack_into('<4I', convex, 16, vertices, plane_item, faces, indices)
    shape = add('hknpConvexShape', convex)
    strings = [add('char', ('ragdoll_' + name + '\0').encode(), len('ragdoll_' + name) + 1)
               for name in ('doppler_radar', 'tracking_radar', 'cannon_main')]
    bodies = bytearray(64 * 3)
    for i, name_item in enumerate(strings):
        offset = 64 * i
        struct.pack_into('<I', bodies, offset, shape)
        struct.pack_into('<I', bodies, offset + 16, name_item)
        struct.pack_into('<4f', bodies, offset + 32, 0., 0., 0., 1.)
        struct.pack_into('<f', bodies, offset + 48, 2.)
    body_item = add(body_type, bodies, 3)
    put(root, 0, '<I', variants)
    put(variants, 4, '<II', class_name, scene)
    put(scene, 0, '<I', systems)
    put(systems, 0, '<I', system)
    put(system, 0, '<4I', library, library, body_item, 0)
    type_section = (section('TST1', tst) + section('TNA1', tna) + section('FST1', fst) +
                    section('TBDY', tbd) + section('TPAD', b''))
    raw = section('TAG0', section('SDKV', b'20200000') + section('DATA', data) +
                  section('TYPE', type_section, False) + section('INDX', section('ITEM',
                  b''.join(struct.pack('<III', *item) for item in items)), False), False)
    return raw, shape


def check() -> None:
    raw, shape = _sample()
    assert sc.problems(raw) == []
    names = ['doppler_radar', 'tracking_radar', 'cannon_main']
    identity = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]
    bones = [Bone(i, -1, -1, -1, i, 0, 0, 0, 0, 0, 0, list(identity), list(identity),
                  [0., 0., 0., 1.], [0., 0., 0., 1.]) for i in range(3)]
    model = Mdb(0x20, names, bones, [], [], [])

    def row(source, destination):
        return [[source, destination], [0., 0., 0.], [0., 0., 0., 1.], [1., 1., 1.]]

    binding = sgo.write(0x102, {
        'animation_from_ragdoll': [row('ragdoll_' + name, name) for name in names],
        'ragdoll_from_animation': [row(name, 'ragdoll_' + name) for name in names]})
    targets = {'doppler_radar': np.asarray(((1., 2., 3.), (3., 4., 5.))),
               'tracking_radar': np.asarray(((-4., -3., -2.), (-2., -1., 0.)))}
    # This is the installation consumer that accesses sc.B, not a mirrored API check.
    framed, new_binding, moved = tc._reframe(model, raw, binding, targets, 'bmpt')
    assert set(moved) == {'ragdoll_doppler_radar', 'ragdoll_tracking_radar'}
    assert rf.problems(model, framed, new_binding) == []
    reader = sc.Shkt(framed)
    positions = [(2., 3., 4.), (-3., -2., -1.), (0., 0., 0.)]
    assert [body.position for body in reader.systems[0].bodies] == positions
    inner = sgo.plain(sgo.read(new_binding)[1])
    for key, reverse in (('animation_from_ragdoll', True), ('ragdoll_from_animation', False)):
        rows = inner[key]
        assert len(rows) == 3
        for name, position in zip(names, positions):
            pair = ['ragdoll_' + name, name] if reverse else [name, 'ragdoll_' + name]
            mapped = next(row for row in rows if row[0] == pair)
            assert np.allclose([rf._f(value) for value in mapped[1]], -np.asarray(position) if reverse else position, atol=1e-6)
    editor = tc._Editor(framed)
    new_shape, new_box = editor.convex(shape, np.diag((2., 3., 4.)), np.asarray((5., 6., 7.)))
    # Attach the edited shape so the full SHKT checker visits its geometry and mass.
    editor.put(rf.Shkt(framed).bodies[0].at + editor.tag.offset(sc.B, 'shape'), '<I', new_shape)
    output = editor.finish()
    output_reader = sc.Shkt(output)
    actual = tc._convex_check(output_reader, new_shape)
    assert np.allclose(actual, new_box, atol=1e-5)
    assert np.allclose(actual, [[2.9, 2.9, 2.9], [7.1, 9.1, 11.1]], atol=1e-5)
    assert sc.problems(output) == []
    bad = bytearray(output)
    _, offset, _ = output_reader.tag.item(new_shape)
    hull = offset + output_reader.tag.offset('hknpConvexShape', 'hull')
    _, plane_offset, _ = output_reader.tag.item(output_reader.tag.u32(hull + 4))
    struct.pack_into('<f', bad, plane_offset + 12, 0.)
    try:
        tc._convex_check(sc.Shkt(bytes(bad)), new_shape)
    except tc.CollisionError as error:
        assert 'planes miss vertices' in str(error)
    else:
        raise AssertionError('stale convex planes accepted')
