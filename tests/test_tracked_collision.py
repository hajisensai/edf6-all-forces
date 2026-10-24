"""Collision regression: authored bounds, real native mode ownership and sibling contact splits.

The complete asset checks run only when an installed EDF6 Root.cpk is available;
packing and sweep mathematics also run on CI without game assets.
"""
from __future__ import annotations

import copy
import math
import os
from pathlib import Path
import struct
import sys
import unittest

sys.path[:0]=[str(Path(__file__).resolve().parents[1]/'pylib')]

import numpy as np

import hkshape
import tracked_collision as tc


class PackedMassTests(unittest.TestCase):
    """The packer and the mass rule are pylib/hkshape.py's (tests/test_hkshape.py checks them on the stock files)."""
    def test_native_packed_vector_reference(self):
        for raw in ('0000ec534ef78030','5a419f4fec0f8038'):
            words=struct.unpack('<4H',bytes.fromhex(raw))
            self.assertEqual(hkshape.pack_vec3(hkshape.unpack_vec3(words)),words)
        self.assertEqual(struct.pack('<4H',*hkshape.pack_vec3((0,0,0))).hex(),'0000000000008019')
        with self.assertRaises(ValueError):hkshape.pack_vec3((0,float('nan'),0))

    def test_mass_box_has_positive_inertia_and_correct_volume(self):
        box=((-1.,.4,-2.),(1.,1.6,2.))
        packed=hkshape.mass_bytes(hkshape.box_mass(*box,1000.))
        self.assertTrue(all(x>0 for x in hkshape.unpack_vec3(struct.unpack_from('<4H',packed,8))))
        np.testing.assert_allclose(hkshape.unpack_vec3(struct.unpack_from('<4H',packed,0)),np.asarray(box).mean(0),atol=1e-4)
        self.assertAlmostEqual(struct.unpack_from('<f',packed,28)[0],9.6,places=5)
        self.assertEqual(packed[16:24],bytes.fromhex('008000800080'+'2ff5'))


class NativeAssetTests(unittest.TestCase):
    """The procedural models' contracts (tests/test_wt_tracked.py: the War Thunder models')."""
    @classmethod
    def setUpClass(cls):
        import wt_tracked
        cls.wt_enabled=wt_tracked.ENABLED
        wt_tracked.ENABLED=False
        cls.addClassCleanup(setattr,wt_tracked,'ENABLED',cls.wt_enabled)
        import rootcpk
        import tracked_combat_model as t
        import vulcan_model
        import bmpt_model
        path=os.environ.get('EDF6_GAME_DIR',rootcpk.DEFAULT_GAME)
        if not Path(path,'Root.cpk').is_file():raise unittest.SkipTest('installed EDF6 Root.cpk is unavailable')
        cls.game=rootcpk.Game(path)
        cls.assets={}
        for kind,module in (('vulcan',vulcan_model),('bmpt',bmpt_model)):
            _,md,_=module.build_with_info(cls.game)
            a=t.Assembly(md);module.parts(a)
            result=tc.build(cls.game,kind,md,a.parts)
            cls.assets[kind]=(md,a.parts,result)

    def test_complete_shapes_bindings_and_determinism(self):
        for kind,(md,parts,result) in self.assets.items():
            self.assertEqual(tc.build(self.game,kind,md,parts)[0],result[0])
            shkt,binding,overrides,report=result
            checked=tc.check(shkt,binding,md,overrides,report)
            self.assertEqual(checked['road_wheels_per_side'],5 if kind=='vulcan' else 6)
            self.assertEqual(len(overrides['car_base_wheel']),10 if kind=='vulcan' else 12)

    def test_explicit_mode_restore_and_unique_accessory_owners(self):
        import sgo
        for kind,(md,parts,result) in self.assets.items():
            _,binding,overrides,report=result
            inner=sgo.read(binding)[1]
            writers={row[0][1]:row[0][0] for row in inner['animation_from_ragdoll']}
            self.assertEqual(writers['cannon_main'],'ragdoll_cannon_main')
            self.assertEqual(writers['doppler_radar'],'ragdoll_doppler_radar')
            self.assertEqual(writers['tracking_radar'],'ragdoll_tracking_radar')
            for name in ('doppler_radar','tracking_radar','tire_moveH_l','tire_moveH_r'):
                self.assertIn([name,0.],overrides['car_base_simulation_node'])
            links={row[0][1]:row[0][0] for row in inner['ragdoll_from_animation']}
            self.assertEqual(links['ragdoll_doppler_radar'],'doppler_radar')
            self.assertEqual(links['ragdoll_tracking_radar'],'tracking_radar')
            if kind=='vulcan':self.assertEqual(links['ragdoll_tire_moveH_l'],'tire_moveG_l')
            broken=copy.deepcopy(overrides)
            broken['car_base_simulation_node']=[row for row in broken['car_base_simulation_node'] if row[0]!='doppler_radar']
            with self.assertRaises(tc.CollisionError):tc.check(result[0],binding,md,broken,report)

    def test_donor_hidden_shapes_are_replaced(self):
        import shkt_check as sc
        for kind,(md,parts,result) in self.assets.items():
            shkt,_,_,report=result
            reader=sc.Shkt(shkt)
            by_name={row['proxy']:np.asarray(row['target_world']) for row in report['proxies']}
            self.assertLess(by_name['ragdoll_doppler_radar'][1,1],3.2)
            self.assertLess(by_name['ragdoll_tracking_radar'][1,1],3.)
            self.assertLess(by_name['ragdoll_cannon_l'][1,2],2.2 if kind=='vulcan' else 3.7)
            if kind=='vulcan':
                self.assertLessEqual(by_name['ragdoll_cannon_main'][1,1],1.8001)
                self.assertGreater(by_name['ragdoll_tire_moveH_l'][0,2],-2.2)
            for b in reader.systems[0].bodies:
                cache,mass,volume=tc._mass_record(b.shape)
                self.assertGreater(mass,0)
                self.assertGreater(volume,0)
                self.assertTrue(np.isfinite(hkshape.unpack_vec3(struct.unpack_from('<4H',cache,8))).all())

    def test_siblings_remain_disjoint_through_native_rotation_and_recoil(self):
        md,parts,(_,_,_,report)=self.assets['vulcan']
        boxes={row['proxy']:np.asarray(row['target_world']) for row in report['proxies']}
        def corners(box):return np.asarray([(x,y,z) for x in box[:,0] for y in box[:,1] for z in box[:,2]])
        left,right=corners(boxes['ragdoll_cannon_l']),corners(boxes['ragdoll_cannon_r'])
        pivots=(np.asarray((.747215,2.218,-.437872)),np.asarray((-.747215,2.218,-.437872)))
        for yaw in range(0,360,30):
            a=math.radians(yaw);Y=np.asarray(((math.cos(a),0,math.sin(a)),(0,1,0),(-math.sin(a),0,math.cos(a))))
            for pitch in (-5,0,15,30,60,85):
                a=math.radians(pitch);X=np.asarray(((1,0,0),(0,math.cos(a),-math.sin(a)),(0,math.sin(a),math.cos(a))))
                normal=Y@X@np.asarray((0,0,1))
                for recoil in (0,.03,.08,.15):
                    L=((left-pivots[0]-np.asarray((0,0,recoil)))@X.T+pivots[0])@Y.T
                    R=((right-pivots[1])@X.T+pivots[1])@Y.T
                    # Extra 1 cm skin on both proxies, in addition to their
                    # already-serialized convex radii, must still leave a gap.
                    self.assertGreater(float((L@normal).min()-(R@normal).max())-.02,.025)
            for side in 'lr':
                G=corners(boxes[f'ragdoll_tire_moveG_{side}']);H=corners(boxes[f'ragdoll_tire_moveH_{side}'])
                normal=Y@np.asarray((1,0,0))
                for roll in (0,30,90,180,270,359):
                    a=math.radians(roll);X=np.asarray(((1,0,0),(0,math.cos(a),-math.sin(a)),(0,math.sin(a),math.cos(a))))
                    g=(G@X.T)@Y.T;h=(H@X.T)@Y.T
                    gap=max(float((g@normal).min()-(h@normal).max()),float((h@normal).min()-(g@normal).max()))
                    self.assertGreater(gap-.02,.019)

    def test_split_wheel_live_world_copy_and_dead_AFR_skin(self):
        import graft_pure as g
        import mdb
        import ragdoll_fit as rf
        import sgo
        import shkt_check as sc
        md,parts,(shkt,binding,overrides,report)=self.assets['vulcan']
        inner=sgo.read(binding)[1]
        afr={x.src:x for x in rf.links(inner,'animation_from_ragdoll')}
        proxies={x.name:x for x in sc.Shkt(shkt).systems[0].bodies}
        def matrix(frame):
            rows=rf.axes(frame.q)
            return [*rows[0],0.,*rows[1],0.,*rows[2],0.,*frame.t,1.]
        for side in 'lr':
            G=md.bones[md.bone_index(f'tire_moveG_{side}')]
            H=md.bones[md.bone_index(f'tire_moveH_{side}')]
            self.assertEqual(G.parent,H.parent)
            names=(f'wheel_G_outer_{side}',f'hub_G_{side}',f'sprocket_G_outer_{side}')
            P=np.vstack([parts[n].pos for n in names])
            self.assertTrue(all(parts[n]._rigid_bone==H.index for n in names))
            # Native mode 1 skips AFR. 66F430 writes the final G world after
            # pose45/world-build and leaves G.local stale. The live post-wheel
            # helper must multiply the immutable local-bind delta by G.world.
            for degrees in (0,30,90,180,270,359):
                angle=math.radians(degrees)
                posed=matrix(rf.Frame(tuple(G.local[12:15]),(math.sin(angle/2),0.,0.,math.cos(angle/2))))
                posed[13]+=.07
                Hlocal=mdb.mmul(mdb.mmul(H.local,g.inverse_affine_general(G.local)),posed)
                parent=matrix(rf.Frame((50.,8.,-20.),(0.,math.sin(.3),0.,math.cos(.3))))
                Gworld=mdb.mmul(posed,parent)
                delta=mdb.mmul(H.local,g.inverse_affine_general(G.local))
                Hworld=mdb.mmul(delta,Gworld)
                np.testing.assert_allclose(Hworld,mdb.mmul(Hlocal,parent),atol=1e-8,rtol=0)
                actual=np.asarray([g.xform(p,mdb.mmul(H.inv_bind,Hworld)) for p in P])
                expected=np.asarray([g.xform(p,mdb.mmul(G.inv_bind,Gworld)) for p in P])
                np.testing.assert_allclose(actual,expected,atol=2e-5,rtol=0)
            # Native mode 4 does run AFR. A separate moving H death body must
            # carry the real outer mesh, not an invisible collision half.
            proxy=proxies[f'ragdoll_tire_moveH_{side}']
            bind=rf.Frame(proxy.position,proxy.orientation)
            physical=rf.Frame((proxy.position[0]+2.,proxy.position[1]+.8,proxy.position[2]-.6),
                              (.30,0.,0.,math.sqrt(1.-.30**2)))
            Hworld=matrix(physical*afr[proxy.name].off)
            actual=np.asarray([g.xform(p,mdb.mmul(H.inv_bind,Hworld)) for p in P])
            to_proxy=g.inverse_affine_general(matrix(bind))
            expected=np.asarray([g.xform(p,mdb.mmul(to_proxy,matrix(physical))) for p in P])
            np.testing.assert_allclose(actual,expected,atol=3e-5,rtol=0)

    def test_private_CAS_changes_only_eight_radar_reference_bytes(self):
        import cas_pose
        import vulcan_model
        import bmpt_model
        source=self.game.read('OBJECT','V603_FLAK.CAS')
        old=cas_pose.CasPose(source)
        wanted=set()
        for clip in old.clips:
            for track in clip.tracks:
                if clip.name=='doppler_radar_loop' and track.name in ('doppler_radar','tracking_radar'):
                    wanted.update(range(track.at+2,track.at+6))
        self.assertEqual(len(wanted),8)
        for module in (vulcan_model,bmpt_model):
            fixed=module.animation(self.game)
            self.assertEqual(len(fixed),len(source))
            self.assertEqual({i for i,(a,b) in enumerate(zip(source,fixed)) if a!=b},wanted)
            pose=cas_pose.CasPose(fixed)
            for before,after in zip(old.clips,pose.clips):
                self.assertEqual(cas_pose.clip_info(old,before),cas_pose.clip_info(pose,after))
                self.assertEqual([x.name for x in before.tracks],[x.name for x in after.tracks])
                for a,b in zip(before.tracks,after.tracks):
                    if a.name in ('doppler_radar','tracking_radar'):
                        self.assertEqual((b.translation,b.rotation,b.scale),(-1,-1,a.scale))
                    else:self.assertEqual(source[a.at:a.at+8],fixed[b.at:b.at+8])
            fire=next(x for x in old.clips if x.name=='fire_loop')
            self.assertEqual(source[fire.at:fire.at+28],fixed[fire.at:fire.at+28])

    def test_real_optics_follow_main_alive_and_own_physics_in_death(self):
        import graft_pure as g
        import mdb
        import ragdoll_fit as rf
        import sgo
        import shkt_check as sc
        def matrix(frame):
            rows=rf.axes(frame.q)
            return [*rows[0],0.,*rows[1],0.,*rows[2],0.,*frame.t,1.]
        for kind,(md,parts,(shkt,binding,overrides,report)) in self.assets.items():
            inner=sgo.read(binding)[1]
            afr={x.src:x for x in rf.links(inner,'animation_from_ragdoll')}
            rfa={x.dst:x for x in rf.links(inner,'ragdoll_from_animation')}
            proxies={x.name:x for x in sc.Shkt(shkt).systems[0].bodies}
            main=md.bones[md.bone_index('cannon_main')]
            main_world_bind=mdb.bind_world(md)[main.index]
            for name in ('doppler_radar','tracking_radar'):
                bone=md.bones[md.bone_index(name)]
                self.assertEqual(bone.parent,main.index)
                P=np.vstack([part.pos for part in parts.values() if part._rigid_bone==bone.index])
                self.assertGreater(len(P),0,'real optics must be drawn by their own physical bone')
                for yaw in (0,.4,1.8,3.4):
                    main_pose=matrix(rf.Frame((100.,20.,50.),(0.,math.sin(yaw/2),0.,math.cos(yaw/2))))
                    live=mdb.mmul(bone.local,main_pose)
                    actual=np.asarray([g.xform(p,mdb.mmul(bone.inv_bind,live)) for p in P])
                    expected=np.asarray([g.xform(p,mdb.mmul(g.inverse_affine_general(main_world_bind),main_pose)) for p in P])
                    np.testing.assert_allclose(actual,expected,atol=2e-5,rtol=0)
                proxy=proxies['ragdoll_'+name]
                self.assertEqual(rfa[proxy.name].src,name)
                bind=matrix(rf.Frame(proxy.position,proxy.orientation))
                physical=rf.Frame((proxy.position[0]+1.3,proxy.position[1]+.9,proxy.position[2]-.4),
                                  (0.,math.sin(.45),0.,math.cos(.45)))
                dead=matrix(physical*afr[proxy.name].off)
                actual=np.asarray([g.xform(p,mdb.mmul(bone.inv_bind,dead)) for p in P])
                expected=np.asarray([g.xform(p,mdb.mmul(g.inverse_affine_general(bind),matrix(physical))) for p in P])
                np.testing.assert_allclose(actual,expected,atol=3e-5,rtol=0)

    def test_wrong_outer_half_skin_is_rejected(self):
        import mdb
        import tracked_combat_model as t
        import vulcan_model
        md,parts,result=self.assets['vulcan']
        broken=copy.deepcopy(md)
        mesh=broken.objects[0].meshes[list(parts).index('wheel_G_outer_l')]
        offset=next(e.offset for e in mesh.elems if e.name.lower()=='blendindices')
        data=bytearray(mesh.vdata)
        for vertex in range(mesh.nverts):data[vertex*mesh.vsize+offset]=md.bone_index('tire_moveG_l')
        mesh.vdata=bytes(data)
        archive=mdb.rab_read(self.game.read('OBJECT',t.HOST_ARC))
        t.member(archive,t.HOST_MDB).stored=mdb.cmpl_compress(mdb.mdb_write(broken))
        with self.assertRaises(t.TrackedCombatCheckError):vulcan_model.check(mdb.rab_write(archive))

    def test_corrupt_vertices_trees_mass_and_wheel_indices_are_rejected(self):
        import shkt_check as sc
        md,parts,(shkt,binding,overrides,report)=self.assets['vulcan']
        def corrupt(edit):
            reader=sc.Shkt(shkt);body=reader.systems[0].bodies[0]
            edit(body.shape,tc._instances(body.shape)[1][0][3])
            return reader.tf.write()
        def vertex(cms,convex):
            struct.pack_into('<f',convex.value['hull']['vertices'].target.data,0,999.)
        def domain(cms,convex):
            cms.value['data'].target.value['meshTree']['sections'].target[0]['domain']['min'][0].set_f32(999.)
        def mass(cms,convex):
            struct.pack_into('<f',hkshape.mass_properties(convex).data,24,-1.)
        def axes(cms,convex):
            hkshape.mass_properties(convex).data[16:24]=bytes.fromhex('0080008000800080')
        for edit in (vertex,domain,mass,axes):
            with self.assertRaises(tc.CollisionError,msg=edit.__name__):tc.check(corrupt(edit),binding,md,overrides,report)
        broken=copy.deepcopy(overrides);broken['car_base_draw_wheel_info'][0][0][0]=99
        with self.assertRaises(tc.CollisionError):tc.check(shkt,binding,md,broken,report)


if __name__=='__main__':unittest.main()
