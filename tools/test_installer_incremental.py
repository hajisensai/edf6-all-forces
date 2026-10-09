"""Fast regression cases for incremental installs; never writes to the real game."""
from __future__ import annotations

from contextlib import ExitStack, redirect_stdout
import importlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT / p) for p in ('tools', 'pylib', 'testrange')]
import buildcache
import crilayla
import installer
import ledger


def cri_stream(tokens: list[int | tuple[int, int]]) -> tuple[bytes, bytes]:
    """Tiny CRILAYLA fixture encoder with literals, overlapping runs and extended lengths."""
    import struct
    bits = ''
    decoded = bytearray()
    for token in tokens:
        if isinstance(token, int):
            bits += '0' + format(token, '08b')
            decoded.append(token)
            continue
        distance, length = token
        bits += '1' + format(distance - 3, '013b')
        remaining = length - 3
        for width in (2, 3, 5, 8):
            part = min(remaining, (1 << width) - 1)
            bits += format(part, f'0{width}b')
            remaining -= part
            if part != (1 << width) - 1:
                break
        else:
            while remaining >= 255:
                bits += '1' * 8
                remaining -= 255
            bits += format(remaining, '08b')
        for _ in range(length):
            decoded.append(decoded[-distance])
    bits += '0' * (-len(bits) % 8)
    stream = bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))[::-1]
    prefix = bytes(range(256))
    return b'CRILAYLA' + struct.pack('<II', len(decoded), len(stream)) + stream + prefix, prefix + decoded[::-1]


class IncrementalTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.game = Path(self.temp.name)
        (self.game / 'Root.cpk').write_bytes(b'root')
        (self.game / 'Chunk02.cpk').write_bytes(b'map')
        self.recipe = {g: 'recipe-1' for g in buildcache.GROUPS}
        self.mock = patch.object(buildcache, 'recipes', lambda: self.recipe.copy())
        self.mock.start()
        self.addCleanup(self.mock.stop)

    def seed(self, group: str = 'chute') -> None:
        cache = buildcache.Cache(str(self.game))
        self.assertFalse(cache.current(group))
        files = {f'OBJECT/{group}.SGO': group.encode()}
        led = ledger.Ledger(str(self.game))
        for rel, blob in files.items():
            led.put(group, rel, blob)
        cache.record(group, files)
        cache.save()

    def check_default_migration_lifecycle(self, existing: bool) -> None:
        shipped = (ROOT / 'EDF6VehicleCrew.ini').read_bytes()
        path = self.game / 'Mods/Plugins/EDF6VehicleCrew.ini'
        if existing:
            path.parent.mkdir(parents=True)
            path.write_bytes(shipped)
        with redirect_stdout(io.StringIO()):
            installer.install_plugin(str(self.game), b'plugin', shipped)
        installed = path.read_text(encoding='utf-8-sig')
        self.assertIn(installer.DEFAULTS_MARK, installed)
        for key in installer.NEW_DEFAULTS[installer.SECTION]:
            with self.subTest(existing=existing, key=key):
                lines = installed.splitlines()
                line = installer._keys(lines)[key.lower()]
                lines[line] = f'{key}=0'
                disabled = '\n'.join(lines) + '\n'
                path.write_bytes(disabled.encode('utf-8'))
                with redirect_stdout(io.StringIO()):
                    installer.install_plugin(str(self.game), b'updated plugin', shipped)
                self.assertEqual(path.read_bytes(), disabled.encode('utf-8'))

    def test_fresh_install_keeps_later_disabled_features(self) -> None:
        self.check_default_migration_lifecycle(existing=False)

    def test_current_defaults_keep_later_disabled_features(self) -> None:
        self.check_default_migration_lifecycle(existing=True)

    def test_current_and_changed_outputs(self) -> None:
        self.seed()
        self.assertTrue(buildcache.Cache(str(self.game)).current('chute'))
        path = self.game / 'Mods/OBJECT/CHUTE.SGO'
        path.write_bytes(b'other')  # same length as original
        self.assertFalse(buildcache.Cache(str(self.game)).current('chute'))
        path.unlink()
        self.assertFalse(buildcache.Cache(str(self.game)).current('chute'))

    def test_recipe_and_archive_invalidation_are_grouped(self) -> None:
        self.seed('chute')
        self.seed('bigmap')
        self.recipe['chute'] = 'recipe-2'
        self.assertFalse(buildcache.Cache(str(self.game)).current('chute'))
        self.assertTrue(buildcache.Cache(str(self.game)).current('bigmap'))
        (self.game / 'Chunk02.cpk').write_bytes(b'changed archive')
        self.assertFalse(buildcache.Cache(str(self.game)).current('bigmap'))

    def test_missing_ownership_and_corrupt_manifest(self) -> None:
        self.seed()
        (self.game / 'Mods' / ledger.MANIFEST).unlink()
        self.assertFalse(buildcache.Cache(str(self.game)).current('chute'))
        path = self.game / 'Mods' / buildcache.MANIFEST
        for raw in ('{', '[]', '{"version":1,"groups":{"chute":null}}'):
            path.write_text(raw)
            self.assertFalse(buildcache.Cache(str(self.game)).current('chute'))

    def test_external_model_added_modified_removed(self) -> None:
        import obj_model
        with patch.object(obj_model, 'model_roots', lambda: [str(self.game / 'models')]):
            self.seed('drill')
            self.assertTrue(buildcache.Cache(str(self.game)).current('drill'))
            folder = self.game / 'models/drill_tank'
            folder.mkdir(parents=True)
            model = folder / 'drill_tank.obj'
            model.write_bytes(b'first')
            self.assertFalse(buildcache.Cache(str(self.game)).current('drill'))
            cache = buildcache.Cache(str(self.game))
            cache.current('drill')
            cache.record('drill', {'OBJECT/DRILL.SGO': b'drill'})
            cache.save()
            self.assertTrue(buildcache.Cache(str(self.game)).current('drill'))
            model.write_bytes(b'other')
            self.assertFalse(buildcache.Cache(str(self.game)).current('drill'))
            model.unlink()
            self.assertFalse(buildcache.Cache(str(self.game)).current('drill'))

    def test_second_install_never_builds_or_rewrites_cached_assets(self) -> None:
        import call_weapons
        import gen
        import make_bigmap
        import make_stock_stores
        with ExitStack() as stack, redirect_stdout(io.StringIO()):
            plugins = {name: (b'dll', f'[{section}]\nBigWorld=0\n'.encode()) for name, section in installer.PLUGINS}
            for name, replacement in {'check_loader': lambda g: None,
                                      'plugin_files': lambda: plugins,
                                      'stack_weapons': lambda g: {},
                                      'build_autoturret': lambda g: ({}, False),
                                      'install_autoturret': lambda g, files, force: None}.items():
                stack.enter_context(patch.object(installer, name, replacement))
            stack.enter_context(patch.object(call_weapons, 'recover', lambda g: False))
            import rootcpk   # the installer points it at its game: not this stand-in, for the tests after this one
            stack.enter_context(patch.object(rootcpk, 'use', lambda root: None))
            shared = stack.enter_context(patch.object(call_weapons, 'install'))
            mission = stack.enter_context(patch.object(gen, 'install', return_value=[]))
            import support_loadout   # no Root.cpk here: its files stand in; its install (skip unchanged) runs for real
            loadout = stack.enter_context(patch.object(support_loadout, 'build',
                                                       return_value={'OBJECT/EDF6VC_SUPPORT_TANK_AP.SGO': b'ap tank'}))
            stack.enter_context(patch.object(make_stock_stores, 'remove', lambda g: ([], [])))
            import make_edf5_campaign   # no Root.cpk here to append to: its files stand in, its install runs for real
            stack.enter_context(patch.object(make_edf5_campaign, 'build', lambda g, campaign=True, test_range=True: (
                {rel: b'stub' for rel in make_edf5_campaign.FILES}, make_edf5_campaign.Contents(3, 6),
                {'rows': [], 'skipped': []})))
            stack.enter_context(patch.object(make_edf5_campaign.modfiles, 'refuse_while_running', lambda *a, **k: None))
            builders = {}
            for group in buildcache.GROUPS:
                module = importlib.import_module('make_' + group)
                files = {f'OBJECT/{group}.MRAB' if group == 'optics' else f'OBJECT/{group}.SGO': group.encode()}
                if group == 'optics':
                    stack.enter_context(patch.object(module, 'build_stock_redirects', return_value={}))
                    stack.enter_context(patch.object(module, 'install_stock_redirects', return_value=[]))
                value = (b'mac', {'part': b'map'}) if group == 'bigmap' else files
                builders[group] = stack.enter_context(patch.object(module, 'build_models' if group == 'optics' else 'build', return_value=value))
                if group != 'bigmap':
                    def write(game: str, data: dict[str, bytes], owner: str = group) -> list[str]:
                        led = ledger.Ledger(game)
                        return [led.put(owner, p, b) for p, b in data.items()]
                    stack.enter_context(patch.object(module, 'install_models' if group == 'optics' else 'install', write))
            installer.install(str(self.game))
            paths = list((self.game / 'Mods/OBJECT').glob('*')) + list((self.game / 'Mods/MAP').glob('*'))
            timestamps = {p: p.stat().st_mtime_ns for p in paths}
            installer.install(str(self.game))
            self.assertTrue(all(b.call_count == 1 for b in builders.values()))
            self.assertEqual(timestamps, {p: p.stat().st_mtime_ns for p in paths})
            self.assertEqual(loadout.call_count, 2)   # rebuilt from the ini each time, written only the first
            self.assertEqual(shared.call_count, 2)  # mutable shared weapon table always checked/installed
            (self.game / 'Mods/OBJECT/CHUTE.SGO').unlink()
            installer.install(str(self.game))
            self.assertEqual(builders['chute'].call_count, 2)
            self.assertTrue(all(b.call_count == 1 for g, b in builders.items() if g != 'chute'))
            manifest = self.game / 'Mods' / buildcache.MANIFEST
            before_failure = manifest.read_bytes()
            self.recipe['chute'] = 'changed recipe'
            mission.side_effect = RuntimeError('interrupted mission install')
            with self.assertRaisesRegex(RuntimeError, 'interrupted'):
                installer.install(str(self.game))
            self.assertNotEqual(manifest.read_bytes(), before_failure)
            self.assertTrue(buildcache.Cache(str(self.game)).current('chute'))
            calls = builders['chute'].call_count
            with self.assertRaisesRegex(RuntimeError, 'interrupted'):
                installer.install(str(self.game))
            self.assertEqual(builders['chute'].call_count, calls)
            before_asset_failure = manifest.read_bytes()
            self.recipe['chute'] = 'another changed recipe'
            with patch.object(importlib.import_module('make_chute'), 'install', side_effect=OSError('asset write failed')):
                with self.assertRaisesRegex(OSError, 'asset write failed'):
                    installer.install(str(self.game))
            self.assertEqual(manifest.read_bytes(), before_asset_failure)
            self.assertFalse(buildcache.Cache(str(self.game)).current('chute'))

    def test_crilayla_literals_and_overlapping_runs(self) -> None:
        for length in (3, 4, 5, 6, 12, 13, 43, 44, 299, 300, 555, 1024):
            for distance in (3, 5, 17):
                packed, expected = cri_stream(list(range(distance)) + [(distance, length), 211, (3, 33)])
                self.assertEqual(crilayla.decompress(packed), expected)

    def test_recipe_tracks_transitive_and_dynamic_imports(self) -> None:
        root = self.game / 'source'
        (root / 'tools').mkdir(parents=True)
        (root / 'pylib').mkdir()
        (root / 'testrange').mkdir()
        (root / 'tools/make_one.py').write_text('import shared\n')
        (root / 'tools/make_two.py').write_text('VALUE = 2\n')
        (root / 'pylib/shared.py').write_text("import importlib\nGENERATED = {'shape': 'geometry'}\ndef build(name):\n    return importlib.import_module(GENERATED[name])\n")
        geometry = root / 'pylib/geometry.py'
        geometry.write_text('VALUE = 1\n')
        with patch.object(buildcache, 'GROUPS', ('one', 'two')):
            before = buildcache.source_recipes(str(root))
            geometry.write_text('VALUE = 2\n')
            after = buildcache.source_recipes(str(root))
        self.assertNotEqual(before['one'], after['one'])
        self.assertEqual(before['two'], after['two'])

    def test_frozen_uses_bundled_recipes_without_source(self) -> None:
        self.mock.stop()
        plugin = self.game / 'plugin'
        plugin.mkdir()
        (plugin / buildcache.RECIPES).write_text(json.dumps(self.recipe))
        with patch.object(sys, 'frozen', True, create=True), patch.object(sys, '_MEIPASS', str(self.game), create=True):
            self.assertEqual(buildcache.recipes(), self.recipe)

    def test_vehicle_source_change_does_not_invalidate_map(self) -> None:
        before = buildcache.source_recipes(str(ROOT))
        original = Path.read_bytes

        def changed(path: Path) -> bytes:
            return original(path) + (b'\n# changed model\n' if path.name == 'centipede_model.py' else b'')

        with patch.object(Path, 'read_bytes', changed):
            after = buildcache.source_recipes(str(ROOT))
        self.assertNotEqual(before['jets'], after['jets'])
        self.assertEqual(before['bigmap'], after['bigmap'])

    def test_external_material_and_texture_changes(self) -> None:
        import obj_model
        root = self.game / 'models'
        model = root / 'twin_tank'
        model.mkdir(parents=True)
        library = self.game / 'external.mtl'
        texture = self.game / 'external.dds'
        (model / 'twin_tank.obj').write_text('mtllib ../../external.mtl\n')
        library.write_text(f'newmtl skin\nmap_Kd {texture}\n')
        with patch.object(obj_model, 'model_roots', lambda: [str(root)]):
            a = buildcache.inputs(str(self.game), 'artillery')
            texture.write_bytes(b'texture')
            b = buildcache.inputs(str(self.game), 'artillery')
            self.assertNotEqual(a, b)
            texture.write_bytes(b'changed')
            self.assertNotEqual(b, buildcache.inputs(str(self.game), 'artillery'))
            library.write_text('newmtl other\n')
            self.assertNotEqual(b, buildcache.inputs(str(self.game), 'artillery'))


def run_checks() -> None:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(IncrementalTests)
    result = unittest.TextTestRunner(stream=io.StringIO()).run(suite)
    if not result.wasSuccessful():
        raise AssertionError('\n'.join(text for _, text in result.errors + result.failures))


if __name__ == '__main__':
    unittest.main()
