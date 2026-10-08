"""Proteus text isolation plus real Root.cpk menu/restore regression.

python tests/proteus_text_test.py --game "D:/steam/steamapps/common/EARTH DEFENSE FORCE 6"
Only temporary Mods folders are written; Root.cpk is read-only.
"""
from __future__ import annotations

import argparse
import copy
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib'), str(ROOT / 'autoturret/tools')]
import build
import describe
import dsgo
import proteus_describe as pd
import rootcpk


class ProteusText(unittest.TestCase):
    def test_names_stats_and_other_mod_text_preserved(self) -> None:
        row = dsgo.Node(['custom name', 'other MOD equipment text', dsgo.Node(['custom stats'])])
        updated = pd.rewrite(row, 'SC')
        self.assertEqual(updated.items[0], row.items[0])
        self.assertEqual(dsgo.dump(updated.items[2]), dsgo.dump(row.items[2]))
        self.assertTrue(updated.items[1].endswith(row.items[1]))
        self.assertEqual(dsgo.dump(pd.rewrite(updated, 'SC')), dsgo.dump(updated))
        updated.items[1] += '\nLater mod note'
        self.assertTrue(pd.rewrite(updated, 'SC').items[1].endswith('Later mod note'))

    def test_id_lookup_not_position_and_unrelated_rows_unchanged(self) -> None:
        ids = ['FOREIGN', *reversed(pd.IDS), 'EWEAPON391']
        rows = [dsgo.Node([x, 'old desc', dsgo.Node([])]) for x in ids]
        before = copy.deepcopy(rows)
        changed = pd.apply(rows, ids, 'EN')
        self.assertEqual(set(changed), set(pd.IDS))
        for at, row_id in enumerate(ids):
            if row_id in pd.IDS:
                self.assertIn('2 seats', rows[at].items[1])
            else:
                self.assertEqual(dsgo.dump(rows[at]), dsgo.dump(before[at]))

    def test_ambiguous_or_incomplete_tables_refused(self) -> None:
        row = dsgo.Node(['name', 'desc', dsgo.Node([])])
        for ids in (list(pd.IDS[:-1]), [*pd.IDS, pd.IDS[0]]):
            with self.assertRaises(ValueError):
                pd.apply([copy.deepcopy(row) for _ in ids], ids, 'EN')
        with self.assertRaises(ValueError):
            pd.apply([], list(pd.IDS), 'EN')


class RealRoot(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not os.path.isfile(os.path.join(rootcpk.DEFAULT_GAME, 'Root.cpk')):
            raise unittest.SkipTest('Root.cpk unavailable; pure row regressions still run')

    def test_all_five_languages_exactly_four_proteus_rows(self) -> None:
        ids = describe.stock_ids()
        game = rootcpk.default()
        en = dsgo.parse(game.read('WEAPON', 'WEAPONTEXT.EN.SGO')).root.get('text_table').items
        actual = {ids[i] for i, row in enumerate(en) if 'Proteus' in row.items[0]}
        self.assertEqual(actual, set(pd.IDS))
        self.assertIn('Barga', en[ids.index('EWEAPON391')].items[0])
        for lang in describe.LANGS:
            doc = dsgo.parse(game.read('WEAPON', f'WEAPONTEXT.{lang}.SGO'))
            before = [dsgo.dump(x) for x in doc.root.get('text_table').items]
            rows = doc.root.get('text_table').items
            pd.apply(rows, ids, lang)
            self.assertEqual({ids[i] for i, x in enumerate(rows) if dsgo.dump(x) != before[i]}, set(pd.IDS))
            first = dsgo.compact(doc)
            pd.apply(rows, ids, lang)
            self.assertEqual(dsgo.compact(doc), first)

    def test_opt_in_install_reinstall_uninstall_preserves_foreign_edit(self) -> None:
        # Exercise the shipped row manifest/restore path, without generating or
        # installing the unrelated AutoTurret models and gun definitions.
        def texts(files: dict, mods: str, *, proteus: bool = False) -> describe.Texts:
            return describe.build_texts([], files, mods, proteus=proteus)

        with tempfile.TemporaryDirectory(prefix='proteus-text-') as mods:
            original = texts({}, mods)
            self.assertTrue(all(not x for x in original.rows.values()))
            rel = 'WEAPON/WEAPONTEXT.EN.SGO'
            foreign = 'Foreign weapon row must survive reinstall and uninstall'
            before_doc = dsgo.parse(original.files[rel])
            before_doc.root.get('text_table').items[0].items[1] = foreign
            path = Path(mods, rel)
            path.parent.mkdir(parents=True)
            path.write_bytes(dsgo.compact(before_doc))
            with patch.object(build, 'build_texts', side_effect=texts):
                build.install(mods, text=True, force=False, files={}, proteus=True)
                first = path.read_bytes()
                build.install(mods, text=True, force=False, files={}, proteus=True)
                self.assertEqual(path.read_bytes(), first)
            manifest = build._load_manifest(mods)
            self.assertEqual(set(manifest['texts'][rel]['rows']), set(pd.IDS))
            build.uninstall(mods, force=False)
            restored = dsgo.parse(path.read_bytes()).root.get('text_table').items
            self.assertEqual(restored[0].items[1], foreign)
            stock = dsgo.parse(original.files[rel]).root.get('text_table').items
            ids = describe.stock_ids()
            for row_id in pd.IDS:
                at = ids.index(row_id)
                self.assertEqual(dsgo.dump(restored[at]), dsgo.dump(stock[at]))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', default=rootcpk.DEFAULT_GAME)
    args, rest = parser.parse_known_args()
    rootcpk.use(args.game)
    unittest.main(argv=[sys.argv[0], *rest])
