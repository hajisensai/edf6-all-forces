"""Tiny archive/skeleton regression for the V301 native initialization leaf."""
import copy
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
import edf41tank
import dsgo
import mdb
import sgo


def fixture():
    # root -> cannon -> cannon_slide -> slide_child; root -> mesh.
    names = ['root', 'cannon', 'cannon_slide', 'slide_child', 'mesh']
    bones = []
    for index, parent, sibling, child, children in ((0, -1, -1, 1, 2), (1, 0, 4, 2, 1),
                                                   (2, 1, -1, 3, 1), (3, 2, -1, -1, 0), (4, 0, -1, -1, 0)):
        local = mdb.ident()
        local[12:15] = [float(index), 2.0, 3.0]
        if index == 2:
            local[:12] = [0.0, 1.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]
        bones.append(mdb.Bone(index, parent, sibling, child, index, children, 3, 0, 0, 0, 0,
                              local, mdb.ident(), [0., 0., 0., 1.], [0., 0., 0., 1.]))
    vertex = bytes([4, 3, 2, 0]) + struct.pack('<4f', .25, .25, .25, .25)
    mesh = mdb.Mesh(bytes([0, 1, 4, 0]), 0, 0, 20,
                    [mdb.VElem(21, 0, 0, 'BLENDINDICES'), mdb.VElem(1, 4, 0, 'BLENDWEIGHT')], 0, vertex, b'')
    model = mdb.Mdb(0x20, names, bones, [mdb.Object(4, 4, [mesh])], [], [])
    world, depth = mdb.bind_world(model), mdb.depth_of(model)
    for index, bone in enumerate(bones):
        bone.inv_bind = mdb.inverse_affine(world[index])
        bone.depth_delta = depth[index+1]-depth[index] if index+1 < len(bones) else depth[index]
    archive = mdb.Rab(0x100, ['TEXTURE', 'MODEL'], [mdb.RabFile('paint.dds', 0, 0, b'unchanged texture'),
                                                 mdb.RabFile('tank.mdb', 1, 0, mdb.mdb_write(model))])
    return model, archive


class RecoilBindingModelTests(unittest.TestCase):
    def test_hidden_leaf_keeps_nonidentity_bind_and_every_original_render_owner(self):
        before, archive = fixture()
        made = edf41tank.recoil_model_archive(mdb.rab_write(archive), 'tank.mdb')
        after = edf41tank.model(made, 'tank.mdb')
        alias = after.bones[mdb.bone_named(after, edf41tank.RECOIL_BIND_BONE)]
        slide = after.bones[mdb.bone_named(after, 'cannon_slide')]
        self.assertEqual(alias.index, 4)  # inserted after the cannon subtree, not at EOF
        self.assertEqual([(bone.parent, bone.sibling, bone.child, bone.child_count) for bone in after.bones],
                         [(-1, -1, 1, 2), (0, 5, 2, 2), (1, 4, 3, 1), (2, -1, -1, 0),
                          (1, -1, -1, 0), (0, -1, -1, 0)])
        self.assertEqual([bone.depth_delta for bone in after.bones], [1, 1, 1, -1, -1, 1])
        self.assertEqual((alias.parent, alias.kind, alias.bounded), (slide.parent, 0, 0))
        self.assertNotEqual(alias.local, mdb.ident())
        self.assertEqual((alias.local, alias.inv_bind), (slide.local, slide.inv_bind))
        old_world, new_world = mdb.bind_world(before), mdb.bind_world(after)
        for bone in before.bones:
            index = mdb.bone_named(after, before.name_of(bone.name))
            self.assertEqual(new_world[index], old_world[bone.index])
            self.assertEqual(after.bones[index].inv_bind, bone.inv_bind)
        self.assertEqual(after.objects[0].bone, 5)
        old_mesh, new_mesh = before.objects[0].meshes[0], after.objects[0].meshes[0]
        self.assertEqual(new_mesh.vdata[4:], old_mesh.vdata[4:])
        self.assertEqual([after.name_of(after.bones[index].name) for index in new_mesh.vdata[:4]],
                         [before.name_of(before.bones[index].name) for index in old_mesh.vdata[:4]])
        self.assertNotIn(alias.index, new_mesh.vdata[:4])
        self.assertEqual((after.materials, after.textures), (before.materials, before.textures))
        self.assertEqual(mdb.rab_read(made).files[0], archive.files[0])
        self.assertEqual(mdb.rab_write(mdb.rab_read(made)), made)
        self.assertEqual(mdb.mdb_read(mdb.mdb_write(after)), after)

    def test_reserved_alias_or_duplicate_archive_model_is_refused(self):
        _model, archive = fixture()
        made = edf41tank.recoil_model_archive(mdb.rab_write(archive), 'tank.mdb')
        with self.assertRaisesRegex(ValueError, 'already exists'):
            edf41tank.recoil_model_archive(made, 'tank.mdb')
        archive.files.append(copy.deepcopy(archive.files[-1]))
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            edf41tank.recoil_model_archive(mdb.rab_write(archive), 'tank.mdb')


class TemplateContractTests(unittest.TestCase):
    def test_actual_dsgo_value_kinds_keep_their_raw_blobs_and_arrays(self):
        node = dsgo.Node(['Vehicle601_Tank', dsgo.Node([2.5, dsgo.Blob(b'raw model/MAB'),
                                                      dsgo.Node([0.0, 1.0])])],
                         {0: 'xgs_scene_object_class', 1: 'animation_model'})
        version, members = edf41tank._template(dsgo.write(dsgo.Document(node, [])))
        self.assertEqual(version, 0x102)
        self.assertEqual(members['animation_model'], [2.5, b'raw model/MAB', [0.0, 1.0]])
        reread = sgo.read(sgo.write_depth_first(version, members))[1]
        self.assertEqual(reread['xgs_scene_object_class'], 'Vehicle601_Tank')
        self.assertEqual(reread['animation_model'][1], b'raw model/MAB')

    def test_unrepresentable_dsgo_template_fields_are_refused(self):
        cases = ((dsgo.Node([1.0]), 'unnamed fields'),
                 (dsgo.Node([dsgo.Node([1.0], {0: 'inner'})], {0: 'field'}), 'named nested'),
                 (dsgo.Node([dsgo.Blob(b'expr', 4)], {0: 'field'}), 'typed expression'))
        for node, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                edf41tank._template(dsgo.write(dsgo.Document(node, [])))


if __name__ == '__main__':
    unittest.main()
