"""Optic resource/transaction regressions; --game also checks actual Root.cpk models."""
from pathlib import Path
import copy
import math
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path[:0] = [str(Path(__file__).resolve().parents[1]/'pylib')]
import ledger
import dsgo
import make_optics
import rootcpk
import sgo
import vehicle_optics as optics
from mdb import bind_world, mdb_read, mdb_write, mmul, rab_read, rab_write, read_elem


def model_points(md):
    world = bind_world(md)
    skin = [mmul(b.inv_bind, world[b.index]) for b in md.bones]
    points, tris = [], []
    for obj in md.objects:
        for mesh in obj.meshes:
            raw, bi, bw = [optics.elem(mesh, n) for n in ('position', 'BLENDINDICES', 'BLENDWEIGHT')]
            base = len(points)
            for i, p in enumerate(raw):
                if mesh.flags[1] and bi and bw:
                    at = [sum(bw[i][j] * (sum(p[a]*skin[bi[i][j]][4*a+k] for a in range(3)) + skin[bi[i][j]][12+k])
                              for j in range(4)) for k in range(3)]
                else:
                    at = [sum(p[a]*world[obj.bone][4*a+k] for a in range(3))+world[obj.bone][12+k] for k in range(3)]
                points.append(tuple(at))
            tris.extend(tuple(base+j for j in row) for row in struct.iter_unpack('<3H', mesh.indices))
    return points, tris


def obstruction(points, triangles, eye, direction):
    """First forward ray/triangle hit beyond the lens surface itself, or None."""
    best = None
    def sub(a, b): return tuple(a[k]-b[k] for k in range(3))
    def dot(a, b): return sum(a[k]*b[k] for k in range(3))
    for tri in triangles:
        a, b, c = [points[j] for j in tri]
        e1, e2 = sub(b, a), sub(c, a)
        h = optics.cross(direction, e2)
        det = dot(e1, h)
        if abs(det) < 1e-9:
            continue
        off = sub(eye, a)
        u = dot(off, h)/det
        q = optics.cross(off, e1)
        v = dot(direction, q)/det
        distance = dot(e2, q)/det
        if u >= -1e-6 and v >= -1e-6 and u+v <= 1.000001 and distance > 1e-4:
            best = distance if best is None else min(best, distance)
    return best


def source_sgo(extra=123):
    return sgo.write(1, {'animation_model': [['app:/object/v505_tank.mrab', 'v505_tank.mdb'], 'retained.cas', b'MAB'],
                         'game_object_durability': 456.0, 'custom_setting': extra})


class Transactions(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='edf-optics-test-')
        self.root = self.temp.name
        self.obj = Path(self.root, 'Mods', 'OBJECT')
        self.obj.mkdir(parents=True)
        self.rel = 'OBJECT/V505_TANK.SGO'
        self.sgo = self.obj/'V505_TANK.SGO'
        self.model = 'OBJECT/EDF6VC_OPTIC_V505_TANK.MRAB'
        self.files = {self.rel: optics.redirect(source_sgo())[0], self.model: b'fixture model'}

    def tearDown(self):
        self.temp.cleanup()

    def test_redirect_preserves_fields_and_rejects_custom_models(self):
        old = source_sgo()
        made, needs = optics.redirect(old)
        a, b = sgo.read(old)[1], sgo.read(made)[1]
        b['animation_model'][0][0] = a['animation_model'][0][0]
        self.assertEqual(sgo.write(1, a), sgo.write(1, b))
        self.assertEqual(needs, (self.model,))
        a['animation_model'][0][0] = 'app:/object/edf6vc_drill.mrab'
        custom = sgo.write(1, a)
        self.assertEqual(optics.redirect(custom), (custom, ()))

    def test_install_upgrade_uninstall_restores_existing_bytes_and_ledger(self):
        original = source_sgo(987)
        self.sgo.write_bytes(original)
        led = ledger.Ledger(self.root)
        before = copy.deepcopy(led.files)
        make_optics.install(self.root, self.files)
        installed = sgo.read(self.sgo.read_bytes())[1]
        self.assertEqual(installed['custom_setting'], 987)
        make_optics.install(self.root, {**self.files, self.model: b'upgraded model'})
        make_optics.remove(self.root)
        self.assertEqual(self.sgo.read_bytes(), original)
        self.assertEqual(ledger.Ledger(self.root).files, before)

    def test_other_owner_keeps_restored_consumer_and_its_claim(self):
        self.sgo.write_bytes(source_sgo())
        ledger.Ledger(self.root).need('testrange', self.rel)
        make_optics.install(self.root, self.files)
        make_optics.remove(self.root)
        self.assertTrue(self.sgo.exists())
        self.assertEqual(optics.model_path(self.sgo.read_bytes()).lower(), 'app:/object/v505_tank.mrab')
        self.assertIn('testrange', ledger.Ledger(self.root).owners(self.rel))
        self.assertNotIn('optics', ledger.Ledger(self.root).owners(self.rel))

    def test_interrupted_put_resumes_from_pending_and_keeps_backup(self):
        original = source_sgo(789)
        self.sgo.write_bytes(original)
        real_put = ledger.Ledger.put
        def after_write(led, owner, rel, data):
            result = real_put(led, owner, rel, data)
            if rel == self.rel:
                raise OSError('simulated interruption after target write')
            return result
        with patch.object(ledger.Ledger, 'put', after_write), self.assertRaises(OSError):
            make_optics.install(self.root, self.files)
        make_optics.install(self.root, self.files)
        make_optics.remove(self.root)
        self.assertEqual(self.sgo.read_bytes(), original)

    def test_interrupted_restore_resumes_without_losing_original(self):
        import modfiles
        original = source_sgo(333)
        self.sgo.write_bytes(original)
        make_optics.install(self.root, self.files)
        write = modfiles.atomic_write
        def interrupted(path, data):
            write(path, data)
            if Path(path) == self.sgo:
                raise OSError('simulated interruption after restoring consumer')
        with patch.object(modfiles, 'atomic_write', interrupted), self.assertRaises(OSError):
            make_optics.remove(self.root)
        make_optics.remove(self.root)
        self.assertEqual(self.sgo.read_bytes(), original)
        self.assertNotIn(self.rel, ledger.Ledger(self.root).files)

    def test_changed_consumer_fields_survive_restoring_its_model_path(self):
        make_optics.install(self.root, self.files)
        v, m = sgo.read(self.sgo.read_bytes())
        m['user_changed'] = 1
        changed = sgo.write(v, m)
        self.sgo.write_bytes(changed)
        make_optics.remove(self.root)
        self.assertEqual(sgo.read(self.sgo.read_bytes())[1]['user_changed'], 1)
        self.assertEqual(optics.model_path(self.sgo.read_bytes()).lower(), 'app:/object/v505_tank.mrab')
        self.assertFalse((Path(self.root)/'Mods'/self.model).exists())

    def test_split_install_does_not_remove_the_other_scope(self):
        make_optics.install_models(self.root, {self.model: self.files[self.model]})
        make_optics.install_stock_redirects(self.root, {self.rel: self.files[self.rel]})
        make_optics.install_models(self.root, {self.model: self.files[self.model]})
        self.assertTrue(self.sgo.exists())
        self.assertTrue((Path(self.root)/'Mods'/self.model).exists())
        self.assertEqual(set(make_optics._load(self.root)['files']), set(self.files))

    def test_foreign_marker_model_is_not_overwritten(self):
        target = Path(self.root)/'Mods'/self.model
        target.write_bytes(b'foreign geometry')
        with self.assertRaises(RuntimeError):
            make_optics.install(self.root, self.files)
        self.assertEqual(target.read_bytes(), b'foreign geometry')
        self.assertFalse(self.sgo.exists())


class RealModels(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.game = rootcpk.default()
        cls.models = []
        for spec in optics.MODELS:
            raw = cls.game.read('OBJECT', spec.stem+'.MRAB')
            old_arc = rab_read(raw)
            old = mdb_read(next(f for f in old_arc.files if f.name.lower() == spec.stem.lower()+'.mdb').data)
            made = optics.build_model(cls.game, spec)
            arc = rab_read(made)
            md = mdb_read(next(f for f in arc.files if f.name.lower() == spec.stem.lower()+'.mdb').data)
            cls.models.append((spec, old_arc, arc, old, md))

    def test_original_geometry_and_other_archive_members_are_unchanged(self):
        for spec, old_arc, arc, old, md in self.models:
            self.assertEqual([f.name for f in arc.files], [f.name for f in old_arc.files])
            for a, b in zip(old_arc.files, arc.files):
                if a.name.lower() != spec.stem.lower()+'.mdb':
                    self.assertEqual(a.stored, b.stored)
            before, bt = model_points(old)
            after, at = model_points(md)
            self.assertEqual(bt, at)
            self.assertEqual(len(before), len(after))
            self.assertLess(max(abs(a-b) for p, q in zip(before, after) for a, b in zip(p, q)), 1e-5)
            self.assertEqual(mdb_write(mdb_read(mdb_write(md))), mdb_write(md))
            for b in old.bones:
                new = md.bones[md.bone_index(old.name_of(b.name))]
                self.assertEqual(new.local, b.local)
                self.assertEqual(new.inv_bind, b.inv_bind)

    def test_lenses_face_forward_and_have_unobstructed_bore_rays(self):
        for spec, _a, _b, _old, md in self.models:
            world = bind_world(md)
            points, tris = model_points(md)
            for lens in spec.lenses:
                eye = world[md.bone_index(lens.marker)]
                self.assertGreater(eye[10], .95)
                blocked = obstruction(points, tris, eye[12:15], eye[8:11])
                self.assertIsNone(blocked, (spec.stem, lens.marker, blocked))
                # A triangle put one metre in front must be found by the same
                # test, so a changed hull cannot silently mask the aperture.
                p = [eye[12+k]+eye[8+k] for k in range(3)]
                barrier = [(p[0]-1,p[1]-1,p[2]),(p[0]+1,p[1]-1,p[2]),(p[0],p[1]+1,p[2])]
                self.assertIsNotNone(obstruction(barrier, [(0,1,2)], eye[12:15], eye[8:11]))

    def test_markers_are_unique_and_inside_the_parent_dfs_subtree(self):
        for spec, _a, _b, _old, md in self.models:
            parents = []
            for lens in spec.lenses:
                i = md.bone_index(lens.marker)
                p = md.bones[i].parent
                parents.append(p)
                self.assertEqual(md.name_of(md.bones[p].name), lens.bone)
                subtree = set()
                for b in md.bones[p+1:]:
                    if b.parent == p or b.parent in subtree:
                        subtree.add(b.index)
                    else:
                        break
                self.assertIn(i, subtree)
                self.assertEqual(md.bones[i].kind, 0)
            self.assertEqual(len(parents), len(set(parents)))

    def test_real_holder_ancestry_selects_only_its_own_optic(self):
        expected = {'V505_TANK': ['vc_optic_00'], 'V601_TANK': ['vc_optic_00'],
                    'VEHICLE403_TANK': [None, 'vc_optic_01', 'vc_optic_02'],
                    'VEHICLE404_BIGTANK': ['vc_optic_00', 'vc_optic_01', 'vc_optic_02', None, None, None]}
        for spec, _a, _b, _old, md in self.models:
            data = self.game.read('OBJECT', spec.stem+'.SGO')
            values = optics.plain(dsgo.parse(data).root) if data[:4] == b'DSGO' else sgo.read(data)[1]
            by_parent = {md.bones[md.bone_index(l.marker)].parent: l.marker for l in spec.lenses}
            world = bind_world(md)
            points, tris = model_points(md)
            got = []
            for name, _seat in values['vehicle_weapon_setting']:
                bone = md.bone_index(name)
                at = bone
                while at >= 0 and at not in by_parent:
                    at = md.bones[at].parent
                marker = by_parent.get(at)
                got.append(marker)
                if marker:
                    eye = world[md.bone_index(marker)]
                    forward = optics.unit(world[bone][8:11])
                    self.assertGreater(sum(a*b for a,b in zip(eye[8:11], forward)), .95)
                    self.assertIsNone(obstruction(points, tris, eye[12:15], forward), (spec.stem, name))
            self.assertEqual(got, expected[spec.stem])

    def test_current_stockstore_writer_updates_survive_two_upgrades_and_uninstall(self):
        import make_stock_stores
        import modfiles
        names = ('V505_TANK', 'V601_TANK')  # actual SGO and DSGO encodings
        real_game = self.game
        class Source:
            def __init__(self, root): self.root = root
            def names(self, folder): return [n+'.SGO' for n in names]
            def read(self, folder, name): return real_game.read(folder, name)
        with tempfile.TemporaryDirectory(prefix='edf-optic-writers-') as tmp:
            directory = Path(tmp, 'Mods', 'OBJECT')
            directory.mkdir(parents=True)
            for n in names:
                (directory/(n+'.SGO')).write_bytes(self.game.read('OBJECT', n+'.SGO'))
            models = {'OBJECT/'+optics.output(spec): rab_write(arc) for spec,_a,arc,_b,_c in self.models}
            make_optics.install_models(tmp, models)
            with patch.object(make_optics.rootcpk, 'Game', Source), patch.object(optics, 'build_model', side_effect=AssertionError('model cache bypass')):
                make_optics.install_stock_redirects(tmp, make_optics.build_stock_redirects(tmp))
                for revision in (1234., 5678.):
                    expected = {}
                    for n in names:
                        payload = make_stock_stores.derived_vehicle(self.game, n)
                        if payload[:4] == b'DSGO':
                            doc = dsgo.parse(payload)
                            doc.root.set('game_object_durability', revision)
                            payload = dsgo.write(doc)
                        else:
                            version, m = sgo.read(payload)
                            m['game_object_durability'] = revision
                            payload = sgo.write(version, m)
                        expected[n] = make_stock_stores._read_any(payload)
                        ledger.Ledger(tmp).put('stockstores', 'OBJECT/'+n+'.SGO', payload)
                    make_optics.install_stock_redirects(tmp, make_optics.build_stock_redirects(tmp))
                    # Cached model installation cannot prune mutable consumers.
                    make_optics.install_models(tmp, models)
                    for n in names:
                        raw = (directory/(n+'.SGO')).read_bytes()
                        values = make_stock_stores._read_any(raw)
                        self.assertIn('edf6vc_optic_', values['animation_model'][0][0])
                        values['animation_model'][0][0] = expected[n]['animation_model'][0][0]
                        self.assertEqual(values, expected[n])
                        self.assertEqual(set(ledger.Ledger(tmp).owners('OBJECT/'+n+'.SGO')), {'stockstores','optics'})
                make_optics.remove(tmp)
                for n in names:
                    rel = 'OBJECT/'+n+'.SGO'
                    raw = (directory/(n+'.SGO')).read_bytes()
                    self.assertEqual(make_stock_stores._read_any(raw), expected[n])
                    entry = ledger.Ledger(tmp).files[rel]
                    self.assertEqual(entry['owners'], ['stockstores'])
                    self.assertEqual(entry['sha'], modfiles.sha256(raw))


if __name__ == '__main__':
    suite = unittest.TestLoader().loadTestsFromTestCase(Transactions)
    if '--game' in sys.argv:
        suite.addTests(unittest.TestLoader().loadTestsFromTestCase(RealModels))
    sys.exit(not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful())
