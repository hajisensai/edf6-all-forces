"""Real installer writers and ownership/cache transactions, exclusively in temporary Mods folders."""
from contextlib import ExitStack, redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/x) for x in ('pylib','tools','testrange','autoturret/tools')]
import buildcache, ledger, modfiles, rootcpk, sgo, vehicle_optics, make_optics, make_stock_stores, gen, installer
import build as at_build

MODEL='OBJECT/EDF6VC_OPTIC_V505_TANK.MRAB'
STOCK='OBJECT/V505_TANK_MISSION.SGO'

def vehicle(hp=123, weapons=2):
    return sgo.write(0x102, {'xgs_scene_object_class':'Vehicle505_Tank', 'game_object_durability':hp,
        'animation_model':[['app:/object/v505_tank.mrab','v505_tank.mdb'],'app:/object/v505_tank.cas',[]],
        'mission_setup':[1,2,3], 'vehicle_weapon_setting':[[float(i),42] for i in range(weapons)],
        'third_party':[99,'retain this field']})

class Integration(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=self.temp.name
        Path(self.root,'Root.cpk').write_bytes(b'archive identity fixture')
        self.models={MODEL:b'generated optic model fixture'}
        make_optics.install_models(self.root,self.models)
    def path(self,rel):return Path(self.root,'Mods',*rel.split('/'))
    def write(self,rel,data):modfiles.atomic_write(str(self.path(rel)),data)
    def values(self,rel):return sgo.load(data=self.path(rel).read_bytes())
    def test_mutable_sgos_are_not_part_of_model_cache(self):
        recipes={name:'recipe' for name in buildcache.GROUPS}
        with patch.object(buildcache,'recipes',return_value=recipes):
            cache=buildcache.Cache(self.root);self.assertFalse(cache.current('optics'));cache.record('optics',self.models);cache.save()
            for hp,weapons in ((123,2),(987,5)):
                self.write(STOCK,vehicle(hp,weapons))
                make_optics.install_stock_redirects(self.root,{STOCK:vehicle()})
                value=self.values(STOCK)
                self.assertEqual(value['game_object_durability'],hp);self.assertEqual(len(value['vehicle_weapon_setting']),weapons)
                self.assertEqual(value['third_party'],[99,'retain this field'])
                self.assertTrue(buildcache.Cache(self.root).current('optics'))
            self.write('OBJECT/V505_TANK.MRAB',b'new texture or mesh')
            self.assertFalse(buildcache.Cache(self.root).current('optics'))
        make_optics.remove(self.root)
        self.assertEqual(self.values(STOCK)['game_object_durability'],987)
        self.assertEqual(len(self.values(STOCK)['vehicle_weapon_setting']),5)
        self.assertEqual(self.values(STOCK)['animation_model'][0][0],'app:/object/v505_tank.mrab')
    def test_stockstores_redirect_before_hash_and_keep_needed_model(self):
        rel='OBJECT/EDF6VC_V505_TANK_STORES.SGO'
        with patch.object(make_stock_stores,'store_files',return_value=[]),patch.object(make_stock_stores.vc,'Game',return_value=object()):
            make_stock_stores.install(self.root,{rel:vehicle(456,4)})
        current=self.path(rel).read_bytes();led=ledger.Ledger(self.root)
        self.assertEqual(led.files[rel]['sha'],modfiles.sha256(current));self.assertIn('stockstores',led.owners(MODEL))
        self.assertEqual(len(self.values(rel)['vehicle_weapon_setting']),4)
        self.assertIn('edf6vc_optic_',self.values(rel)['animation_model'][0][0])
        make_optics.remove(self.root);self.assertTrue(self.path(MODEL).exists())
        # A changed consumer must retain its dependency and fingerprint across independent removal.
        edited=sgo.load(data=current);edited['third_party']=[1234];self.write(rel,sgo.write(0x102,edited))
        edited_bytes=self.path(rel).read_bytes()
        with patch.object(make_stock_stores,'store_files',return_value=[]),patch.object(make_stock_stores.vc,'Game',return_value=object()):
            make_stock_stores.install(self.root,{rel:vehicle(1,1)})
            self.assertEqual(self.path(rel).read_bytes(),edited_bytes)
            make_stock_stores.install(self.root,{})
            self.assertTrue(self.path(rel).exists() and self.path(MODEL).exists())
        _,kept=make_stock_stores.remove(self.root);self.assertTrue(kept);self.assertTrue(self.path(MODEL).exists())
        self.assertEqual(ledger.Ledger(self.root).files[rel]['sha'],modfiles.sha256(current))
    def test_autoturret_records_final_optic_bytes_and_reinstalls_without_force(self):
        rel='OBJECT/V505_TANK_AI.SGO'
        original_install=at_build.install
        def without_text(mods,text,force,files,**kw):
            return original_install(mods,text=False,force=force,files=files)
        with ExitStack() as st,redirect_stdout(io.StringIO()):
            st.enter_context(patch.object(at_build,'_refuse_while_running',return_value=None))
            st.enter_context(patch.object(rootcpk,'Game',return_value=object()))
            st.enter_context(patch.object(at_build,'install',side_effect=without_text))
            for hp in (100,888):
                st.enter_context(patch.object(at_build,'build_files',return_value={rel:vehicle(hp,3)}))
                built=installer.build_autoturret(self.root);self.assertIsNotNone(built);self.assertFalse(built[1])
                installer.install_autoturret(self.root,*built)
                self.assertFalse(at_build.foreign(str(Path(self.root,'Mods')),built[0]))
                manifest=json.loads(Path(self.root,'Mods',at_build.MANIFEST).read_text())
                self.assertEqual(manifest['files'][rel]['sha'],modfiles.sha256(self.path(rel).read_bytes()))
                self.assertEqual(self.values(rel)['game_object_durability'],hp)
        make_optics.remove(self.root);self.assertTrue(self.path(MODEL).exists())
    def test_private_range_preserves_public_plan_ids_and_changed_consumers(self):
        source='v505_tank_mission';private=gen.mission_vehicle(source)
        self.assertNotEqual(source,private);self.assertEqual(gen.DERIVED[private],source.upper())
        class Game:
            def read(self,folder,name):return vehicle(765,4)
        game=Game()
        with patch.object(gen,'vehicle_sgo',return_value=vehicle(765,4)):
            gen._write_derived(self.root,game,{private})
        rel='OBJECT/'+private.upper()+'.SGO';first=self.path(rel).read_bytes()
        self.assertFalse(self.path('OBJECT/'+source.upper()+'.SGO').exists())
        self.assertEqual(len(self.values(rel)['vehicle_weapon_setting']),4)
        self.assertIn('testrange',ledger.Ledger(self.root).owners(MODEL))
        value=self.values(rel);value['third_party']=[456];self.write(rel,sgo.write(0x102,value))
        gen._write_derived(self.root,game,set());make_optics.remove(self.root)
        self.assertTrue(self.path(rel).exists() and self.path(MODEL).exists())
        self.assertEqual(ledger.Ledger(self.root).files[rel]['sha'],modfiles.sha256(first))
        self.path(rel).unlink();gen._write_derived(self.root,game,set());make_optics.remove(self.root)
        self.assertFalse(self.path(MODEL).exists())

@unittest.skipUnless(Path(rootcpk.DEFAULT_GAME,'Root.cpk').is_file(),'read-only Root.cpk unavailable')
class RealStock(unittest.TestCase):
    def test_generated_stockstores_and_range_change_only_the_model_path(self):
        game=rootcpk.Game(rootcpk.DEFAULT_GAME)
        for source,private in gen.OPTIC_MISSION.items():
            original=game.read('OBJECT',source.upper()+'.SGO')
            made=gen.vehicle_sgo(game,private)
            self.assertEqual(sgo.load(data=made),sgo.load(data=original))
            marked,needs=make_optics.redirect(made);self.assertTrue(needs)
            before,after=sgo.load(data=made),sgo.load(data=marked)
            before['animation_model'][0][0]=after['animation_model'][0][0]
            self.assertEqual(before,after)
        for stem in ('V505_TANK','V601_TANK','VEHICLE403_TANK','VEHICLE404_BIGTANK'):
            data=make_stock_stores.derived_vehicle(game,stem)
            self.assertIn('edf6vc_optic_',sgo.load(data=data)['animation_model'][0][0])
            self.assertGreater(len(sgo.load(data=data)['vehicle_weapon_setting']),make_stock_stores.stock_rows(game,stem))

if __name__=='__main__':unittest.main()
