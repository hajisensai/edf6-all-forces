"""Old misaligned private animations must not survive a successful update."""
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'tools'), str(ROOT / 'tests')]
import buildcache
import cas_pose
import ledger
import make_proteus as mp
import proteus_model as pm
import rootcpk
from test_proteus_assets import fixture
from proteus_install_test import stock, MODEL, CAS, VEHICLE


def unaligned(data):
    p = cas_pose.CasPose(data)
    out = bytearray(data); out.extend(bytes((-len(out)) % 16 + 4)); points = len(out)
    for i in range(p.channel_count):
        old, new = p.points + i*48, len(out)
        row = bytearray(data[old:old+48]); offset = struct.unpack_from('<i', row, 32)[0]
        if offset: struct.pack_into('<i', row, 32, old+offset-new)
        out.extend(row)
    struct.pack_into('<i', out, p.canm+20, points-p.canm)
    return bytes(out)


class Upgrade(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = self.tmp.name
        self.source = fixture()
        self.fixed = pm.animation(self.source); self.old = unaligned(self.fixed)
        self.game = type('Game', (), {'read': lambda _, folder, name: self.source})()
        self.files = {VEHICLE: mp.redirect(stock())[0], MODEL: b'model', CAS: self.old}

    def test_standalone_range_repairs_unchanged_owned_animation(self):
        led = ledger.Ledger(self.root)
        led.put('testrange', MODEL, b'model'); led.put('testrange', CAS, self.old)
        made, needs = mp.range_vehicle(led, self.game, stock())
        self.assertTrue(made)
        self.assertEqual(Path(led.disk(CAS)).read_bytes(), self.fixed)
        self.assertEqual(set(ledger.Ledger(self.root).owners(CAS)), {'testrange'})
        self.assertEqual(needs, (MODEL, CAS))

    def test_shared_or_modified_old_animation_is_refused_without_writes(self):
        for changed in (False, True):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as root:
                led = ledger.Ledger(root)
                led.put('testrange', MODEL, b'model'); led.put('testrange', CAS, self.old)
                if changed: Path(led.disk(CAS)).write_bytes(self.old+b'other mod')
                else: led.need('proteus', CAS)
                before = Path(led.disk(CAS)).read_bytes()
                with self.assertRaisesRegex(RuntimeError, '全军出击安装器'):
                    mp.range_vehicle(led, self.game, stock())
                self.assertEqual(Path(led.disk(CAS)).read_bytes(), before)

    def test_recipe_change_forces_transactional_full_upgrade(self):
        # Use real dependency hashing, changing only the previous bad alignment
        # expression in a temporary source tree; never mutate checkout sources.
        with tempfile.TemporaryDirectory() as tree:
            for folder in ('pylib', 'tools', 'testrange'):
                dest = Path(tree, folder); dest.mkdir()
                for file in Path(ROOT, folder).glob('*.py'): shutil.copyfile(file, dest/file.name)
            current = buildcache.source_recipes(tree)
            source = Path(tree, 'pylib', 'proteus_model.py')
            source.write_text(source.read_text(encoding='utf-8').replace('(-len(out)) % 16', '(-len(out)) % 4'), encoding='utf-8')
            previous = buildcache.source_recipes(tree)
        self.assertNotEqual(previous['proteus'], current['proteus'])
        Path(self.root, 'Root.cpk').write_bytes(b'archive identity fixture')
        mp.install(self.root, self.files)
        with patch.object(buildcache, 'recipes', return_value=previous):
            cache = buildcache.Cache(self.root); self.assertFalse(cache.current('proteus'))
            cache.record('proteus', self.files); cache.save()
            self.assertTrue(buildcache.Cache(self.root).current('proteus'))
        with patch.object(buildcache, 'recipes', return_value=current):
            cache = buildcache.Cache(self.root); self.assertFalse(cache.current('proteus'))
            updated = {**self.files, CAS: self.fixed}
            mp.install(self.root, updated); cache.record('proteus', updated); cache.save()
            self.assertTrue(buildcache.Cache(self.root).current('proteus'))
        self.assertEqual(Path(self.root, 'Mods', CAS).read_bytes(), self.fixed)
        mp.remove(self.root)
        self.assertFalse(Path(self.root, 'Mods', CAS).exists())

    def test_full_update_does_not_silently_leave_modified_crashing_animation(self):
        mp.install(self.root, self.files)
        path = Path(self.root, 'Mods', CAS)
        modified = self.old + b'changed'; path.write_bytes(modified)
        with self.assertRaisesRegex(RuntimeError, '无法安全自动升级'):
            mp.install(self.root, {**self.files, CAS: self.fixed})
        self.assertEqual(path.read_bytes(), modified)

    @unittest.skipUnless(Path(rootcpk.DEFAULT_GAME, 'Root.cpk').is_file(), 'read-only Root.cpk unavailable')
    def test_actual_previous_cas_files_upgrade_and_uninstall_through_manifest(self):
        game = rootcpk.Game(rootcpk.DEFAULT_GAME)
        for host in pm.HOSTS:
            with self.subTest(host=host), tempfile.TemporaryDirectory() as root:
                fixed = pm.animation(game.read('OBJECT', host + '.CAS'))
                rel = f'OBJECT/EDF6VC_{host}.CAS'
                installed = Path(rootcpk.DEFAULT_GAME, 'Mods', rel)
                previous = installed.read_bytes() if installed.is_file() else unaligned(fixed)
                # After a local update the real installation may already be fixed;
                # retain a valid-but-misaligned previous-format control then.
                if cas_pose.CasPose(previous).points % 16 == 0:
                    previous = unaligned(fixed)
                mp.install(root, {rel: previous})
                self.assertNotEqual(cas_pose.CasPose(Path(root, 'Mods', rel).read_bytes()).points % 16, 0)
                mp.install(root, {rel: fixed})
                self.assertEqual(Path(root, 'Mods', rel).read_bytes(), fixed)
                mp.remove(root)
                self.assertFalse(Path(root, 'Mods', rel).exists())


if __name__ == '__main__': unittest.main()
