"""Portable real collision consumers for the frozen installer's package gate.

The tiny tagfile is synthetic (pylib/hkfile.py writes it from a minimal type table): three
static bodies share one cube hull. No game or test module is read. Keep its schema
independent of shkt_check's constants so that importing a CLI wrapper under that library
name cannot satisfy the check.
"""
from __future__ import annotations

import struct

import numpy as np

import hkfile
import ragdoll_fit as rf
import sgo
import shkt_check as sc
import tracked_collision as tc
from mdb import Bone, Mdb


def _schema() -> hkfile.TypeTable:
    """A minimal type table of the hknp classes the consumers read, with Havok's kinds (subTypeFlags) and the
    stock compact layout's member names; sizes and offsets are its own."""
    table = hkfile.TypeTable()

    def ty(name, kind, size, align=4, members=(), pointee=None, tuple_n=0, parent=None):  # noqa: ANN001, ANN202
        t = hkfile.Type(name)
        t.has_body = True
        t.body_flags = 0x01 | 0x08 | (0x02 if pointee is not None else 0) | (0x20 if members else 0)
        t.subtype_flags = kind | ((0x20 | tuple_n << 8) if tuple_n else 0)
        t.pointee, t.size, t.align_raw, t.parent = pointee, size, align, parent
        t.members = [hkfile.Member(n, 0, o, m) for n, o, m in members]
        table.add(t)
        return t
    char = ty('char', hkfile.INT, 1, 1)
    u8 = ty('hkUint8', hkfile.INT, 1, 1)
    u16 = ty('hkUint16', hkfile.INT, 2, 2)
    u32 = ty('hkUint32', hkfile.INT, 4)
    real = ty('hkReal', hkfile.FLOAT, 4)
    half = ty('hkHalf16', hkfile.FLOAT, 2, 2)
    vec4 = ty('hkVector4', hkfile.ARRAY, 16, 16, pointee=real, tuple_n=4)
    vec3 = ty('hkFloat3', hkfile.ARRAY, 12, 4, pointee=real, tuple_n=3)
    string = ty('hkStringPtr', hkfile.STRING, 4, pointee=char)

    def array(of):  # noqa: ANN001, ANN202
        return table.find(f'hkArray<{of.name}>') or ty(f'hkArray<{of.name}>', hkfile.ARRAY, 4, pointee=of)

    def ptr(of):  # noqa: ANN001, ANN202
        return table.find(f'hkRefPtr<{of.name}>') or ty(f'hkRefPtr<{of.name}>', hkfile.POINTER, 4, pointee=of)
    mass = ty('hknpShapeMassProperties', hkfile.RECORD, 32, 8, [('compressedMassProperties', 0, u32)])
    prop = ty('hknpShapeProperties::Property', hkfile.RECORD, 8, 4, [('object', 0, ptr(mass)), ('key', 4, u16)])
    props = ty('hknpShapeProperties', hkfile.RECORD, 4, 4, [('properties', 0, array(prop))])
    face = ty('hknpConvexHull::Face', hkfile.RECORD, 4, 2, [('firstIndex', 0, u16), ('numIndices', 2, u8),
                                                           ('minHalfAngle', 3, u8)])
    edge = ty('hknpConvexHull::Edge', hkfile.RECORD, 2, 1, [('faceIndex', 0, u8), ('edgeIndex', 1, u8)])
    hull = ty('hknpConvexHull', hkfile.RECORD, 24, 4, [
        ('vertices', 0, array(vec3)), ('planes', 4, array(vec4)), ('faces', 8, array(face)),
        ('indices', 12, array(u8)), ('faceLinks', 16, array(edge)), ('vertexEdges', 20, array(edge))])
    u64 = ty('hkUint64', hkfile.INT, 8, 4)
    i32 = ty('int', hkfile.INT | 0x200, 4)
    shape = ty('hknpShape', hkfile.RECORD, 24, 4, [
        ('type', 0, u8), ('dispatchType', 1, u8), ('flags', 2, u16), ('numShapeKeyBits', 4, u8),
        ('convexRadius', 8, real), ('userData', 12, u64), ('properties', 20, props)])
    convex = ty('hknpConvexShape', hkfile.RECORD, 52, 4, [('hull', 24, hull), ('maxAllowedPenetration', 48, half)],
                parent=shape)
    aabb = ty('hkAabb', hkfile.RECORD, 32, 16, [('min', 0, vec4), ('max', 16, vec4)])
    lanes = ty('T[N]<hkUint32,4>', hkfile.ARRAY, 16, 4, pointee=u32, tuple_n=4)
    node = ty('hkcdSimdTree::Node', hkfile.RECORD, 128, 16, [
        ('lx', 0, vec4), ('hx', 16, vec4), ('ly', 32, vec4), ('hy', 48, vec4), ('lz', 64, vec4), ('hz', 80, vec4),
        ('data', 96, lanes), ('isLeaf', 112, u8)])
    simd = ty('hkcdSimdTree', hkfile.RECORD, 8, 4, [('nodes', 0, array(node)), ('isCompact', 4, u8)])
    inst_members = [('rotation', 0, vec4), ('translation', 16, vec3), ('leafIndex', 28, u16), ('flags', 30, u8),
                    ('scale', 32, vec3), ('shapeTag', 44, u16), ('destructionTag', 46, u16), ('shape', 48, ptr(shape))]
    inst = ty('hknpShapeInstance', hkfile.RECORD, 64, 16, inst_members)
    element = ty('hkFreeListArrayElement<hknpShapeInstance>', hkfile.RECORD, 64, 16, [], parent=inst)
    free_list = ty('hkFreeListRelArray<hknpShapeInstance,8>', hkfile.RECORD, 12, 4, [
        ('elements', 0, array(element)), ('firstFree', 4, i32), ('numAllocated', 8, u32)])
    bvd = ty('hknpCompoundShapeData', hkfile.RECORD, 32, 4, [('simdTree', 20, simd), ('type', 28, u8)])
    ty('hknpCompoundShape', hkfile.RECORD, 128, 16, [
        ('shapeTagCodecInfo', 24, u32), ('instances', 28, free_list), ('aabb', 48, aabb), ('boundingRadius', 80, real),
        ('isMutable', 84, u8), ('estimatedNumShapeKeys', 88, i32), ('boundingVolumeData', 92, bvd)], parent=shape)
    a4 = ty('hkcdCompressedAabbCodecs::Aabb4BytesCodec', hkfile.RECORD, 4, 1, [('xyz', 0, u8)])
    a5 = ty('hkcdCompressedAabbCodecs::Aabb5BytesCodec', hkfile.RECORD, 5, 1, [('xyz', 0, u8)])
    reals6 = ty('T[N]<hkReal,6>', hkfile.ARRAY, 24, 4, pointee=real, tuple_n=6)
    section = ty('hkcdStaticMeshTree::Section', hkfile.RECORD, 96, 16, [
        ('nodes', 0, array(a4)), ('domain', 16, aabb), ('codecParms', 48, reals6), ('firstPackedVertexIndex', 72, u32),
        ('firstSharedVertexIndex', 76, u32), ('firstPrimitiveIndex', 80, u32), ('firstDataRunIndex', 84, u32),
        ('numPackedVertices', 88, u8), ('numPrimitives', 89, u8), ('numDataRuns', 90, u8), ('page', 91, u8),
        ('leafIndex', 92, u16), ('layerData', 94, u8), ('flags', 95, u8)])
    prim = ty('hkcdStaticMeshTree::Primitive', hkfile.RECORD, 4, 1, [('indices', 0, u8)])
    run = ty('hkcdDefaultStaticMeshTree::PrimitiveDataRun', hkfile.RECORD, 4, 2, [
        ('value', 0, u16), ('index', 2, u8), ('count', 3, u8)])
    tree = ty('hknpCompressedMeshShapeTree', hkfile.RECORD, 96, 16, [
        ('nodes', 0, array(a5)), ('domain', 16, aabb), ('numPrimitiveKeys', 48, i32), ('bitsPerKey', 52, i32),
        ('maxKeyValue', 56, u32), ('primitiveStoresIsFlatConvex', 60, u8), ('sections', 64, array(section)),
        ('primitives', 68, array(prim)), ('sharedVerticesIndex', 72, array(u16)), ('packedVertices', 76, array(u32)),
        ('sharedVertices', 80, array(u64)), ('primitiveDataRuns', 84, array(run))])
    data = ty('hknpCompressedMeshShapeData', hkfile.RECORD, 120, 16, [
        ('meshTree', 0, tree), ('simdTree', 96, simd), ('hasSimdTree', 116, u8)])
    bits = ty('hkRelBitField', hkfile.RECORD, 8, 4, [('words', 0, array(u32)), ('numBits', 4, i32)])
    ty('hknpCompressedMeshShape', hkfile.RECORD, 52, 4, [
        ('shapeTagCodecInfo', 24, u32), ('data', 28, ptr(data)), ('triangleIsInterior', 32, bits),
        ('numTriangles', 40, i32), ('numConvexShapes', 44, i32), ('externShapes', 48, array(inst))], parent=shape)
    body = ty('hknpPhysicsSystemData::bodyCinfoWithAttachment', hkfile.RECORD, 128, 16, [
        ('shape', 0, ptr(shape)), ('collisionFilterInfo', 12, u32), ('materialId', 16, u16), ('name', 20, string),
        ('motionType', 32, u8), ('position', 48, vec4), ('orientation', 64, vec4), ('mass', 80, real),
        ('massDistribution', 84, ptr(u32)), ('motionPropertiesId', 88, u16)])
    system = ty('hknpPhysicsSystemData', hkfile.RECORD, 16, 4, [
        ('materials', 0, array(u32)), ('motionProperties', 4, array(u32)), ('bodyCinfos', 8, array(body)),
        ('constraintCinfos', 12, array(u32))])
    scene = ty('hknpPhysicsSceneData', hkfile.RECORD, 4, 4, [('systemDatas', 0, array(ptr(system)))])
    variant = ty('hkRootLevelContainer::NamedVariant', hkfile.RECORD, 12, 4, [
        ('name', 0, string), ('className', 4, string), ('variant', 8, ptr(scene))])
    ty('hkRootLevelContainer', hkfile.RECORD, 4, 4, [('namedVariants', 0, array(variant))])
    return table


def _sample() -> tuple[bytes, hkfile.Block]:
    """(the tagfile, its cube hull shape block in a graph of that file): three static bodies share one cube hull."""
    table = _schema()
    tf = hkfile.Tagfile(table, hkfile.Block(table.get('hkRootLevelContainer'), 1))

    def array(type_name: str, count: int, data: bytes | None = None) -> hkfile.Block:
        return tf.new(table.get(type_name), count, data)
    points = np.asarray([(x, y, z) for x in (-1., 1.) for y in (-1., 1.) for z in (-1., 1.)])
    planes = np.asarray([(1., 0., 0., -1.), (-1., 0., 0., -1.), (0., 1., 0., -1.),
                         (0., -1., 0., -1.), (0., 0., 1., -1.), (0., 0., -1., -1.)])
    polygons = [(4, 5, 7, 6), (0, 2, 3, 1), (2, 6, 7, 3), (0, 1, 5, 4), (1, 3, 7, 5), (0, 4, 6, 2)]
    shape = tf.new('hknpConvexShape')
    h = shape.value['hull']
    h['vertices'].set_target(array('hkFloat3', 8, points.astype('<f4').tobytes()))
    h['planes'].set_target(array('hkVector4', 6, planes.astype('<f4').tobytes()))
    h['faces'].set_target(array('hknpConvexHull::Face', 6, b''.join(struct.pack('<HBB', 4 * i, 4, 127) for i in range(6))))
    h['indices'].set_target(array('hkUint8', 24, bytes(v for polygon in polygons for v in polygon)))
    shape.value['convexRadius'].set_f32(.05)
    shape.value['maxAllowedPenetration'].set_f32(struct.unpack('<f', struct.pack('<I', 0x3c23 << 16))[0])
    mass = tf.new('hknpShapeMassProperties', 1, tc._mass_cache(np.asarray(((-1.05,) * 3, (1.05,) * 3)), 2.))
    prop = array('hknpShapeProperties::Property', 1)
    prop[0]['object'].set_target(mass)
    prop[0]['key'].set_int(0xF100)
    shape.value['properties']['properties'].set_target(prop)
    bodies = array('hknpPhysicsSystemData::bodyCinfoWithAttachment', 3)
    for i, name in enumerate(('doppler_radar', 'tracking_radar', 'cannon_main')):
        bodies[i]['shape'].set_target(shape)
        bodies[i]['name'].set_target(tf.string('ragdoll_' + name))
        bodies[i]['orientation'].set_vec((0., 0., 0., 1.))
        bodies[i]['mass'].set_f32(2.)
    library = array('hkUint32', 1)
    system = tf.new('hknpPhysicsSystemData')
    for member in ('materials', 'motionProperties'):
        system.value[member].set_target(library)
    system.value['bodyCinfos'].set_target(bodies)
    systems = array('hkRefPtr<hknpPhysicsSystemData>', 1)
    systems[0].set_target(system)
    scene = tf.new('hknpPhysicsSceneData')
    scene.value['systemDatas'].set_target(systems)
    variants = array('hkRootLevelContainer::NamedVariant', 1)
    variants[0]['className'].set_target(tf.string('hknpPhysicsSceneData'))
    variants[0]['variant'].set_target(scene)
    tf.root.value['namedVariants'].set_target(variants)
    raw = tf.write()
    reread = sc.Shkt(raw)
    return raw, reread.systems[0].bodies[0].shape


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
    framed_reader = sc.Shkt(framed)
    editor = tc._Editor(framed_reader.tf)
    source = framed_reader.systems[0].bodies[0]
    new_shape, new_box = editor.convex(source.shape, np.diag((2., 3., 4.)), np.asarray((5., 6., 7.)))
    # Attach the edited shape so the full SHKT checker visits its geometry and mass.
    source.body.set_shape(new_shape)
    output = framed_reader.tf.write()
    output_reader = sc.Shkt(output)
    written = output_reader.systems[0].bodies[0].shape
    actual = tc._convex_check(written)
    assert np.allclose(actual, new_box, atol=1e-5)
    assert np.allclose(actual, [[2.9, 2.9, 2.9], [7.1, 9.1, 11.1]], atol=1e-5)
    assert sc.problems(output) == []
    planes = written.value['hull']['planes'].target
    struct.pack_into('<f', planes.data, 12, 0.)
    try:
        tc._convex_check(written)
    except tc.CollisionError as error:
        assert 'planes miss vertices' in str(error)
    else:
        raise AssertionError('stale convex planes accepted')
