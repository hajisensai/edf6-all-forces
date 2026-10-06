"""Build and replay the two vehicle animations against actual generated MRABs.

Root.cpk and user models are read only. --out is required and must be outside the
game directory. Saves generated assets, before/after pose previews and a JSON report.
Run: python tools/vehicle_cas_check.py --out build/vehicle-cas-review
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct
import sys

sys.path[:0] = [str(Path(__file__).resolve().parent.parent / 'pylib')]
import artillery_model
from cas_pose import CasPose
import drill_model
import make_artillery
import make_drill
import model_view
import rootcpk


def locals_from(md, pose: CasPose, clip_name: str, frame: int = 0):
    """Replay stored absolute translations on the MDB; omitted tracks retain its bind.

    Rotation is intentionally unchanged here: the reported defect is in translation,
    while the byte comparison below verifies that rotation channels are untouched.
    """
    clip = next(c for c in pose.clips if c.name == clip_name)
    out = {}
    for track in clip.tracks:
        i = md.bone_index(track.name)
        if i < 0 or track.translation < 0:
            continue
        local = list(md.bones[i].local)
        local[12:15] = pose.translation(track.translation, frame)
        out[track.name] = local
    return out


def verify_motion(old: CasPose, new: CasPose, free: set[str]):
    """Every retained channel has identical sample displacement and non-base bytes."""
    assert old.names == new.names
    for a, b in zip(old.clips, new.clips):
        assert a.name == b.name
        assert [t.name for t in b.tracks] == [t.name for t in a.tracks if t.name not in free]
        for ta, tb in zip([t for t in a.tracks if t.name not in free], b.tracks):
            assert (ta.translation, ta.rotation, ta.scale) == (tb.translation, tb.rotation, tb.scale)
            for i in (ta.rotation, ta.scale):
                if i >= 0:
                    at = old.points + i*48
                    assert old.data[at:at+48] == new.data[at:at+48]
            if ta.translation < 0:
                continue
            i = ta.translation; at = old.points+i*48
            assert old.data[at+12:at+48] == new.data[at+12:at+48]
            count = struct.unpack_from('<i', old.data, at+40)[0]
            d0 = tuple(y-x for x,y in zip(old.translation(i,0), new.translation(i,0)))
            for f in range(count):
                delta = tuple(y-x for x,y in zip(old.translation(i,f), new.translation(i,f)))
                assert max(abs(x-y) for x,y in zip(delta,d0)) < 1e-5
    # Key streams, names and CAS state graph also remain byte-identical, apart from
    # compacted track rows/counts and xyz channel bases (the only authorized edits).
    allowed = set()
    for c in old.clips:
        allowed.update(range(c.at+20,c.at+24))
        allowed.update(range(c.tracks_at,c.tracks_at+len(c.tracks)*8))
    for i in range(old.channel_count):
        allowed.update(range(old.points+i*48,old.points+i*48+12))
    assert len(old.data)==len(new.data)
    assert all(a==b for i,(a,b) in enumerate(zip(old.data,new.data)) if i not in allowed)


def preview(md, before, after, out):
    """Keep the full repaired gun/drill in frame, even when the broken pose is smaller."""
    rows=[(label,model_view.geometry(md,{},['@tex'],local)) for label,local in
          [('stock CAS: broken',before),('retargeted CAS',after)]]
    extent=max(float(abs(v).max()) for _label,(v,_t,_c) in rows)
    size=600
    image=model_view.Image.new('RGB',(size*4,size*len(rows)))
    for row,(label,(v,t,c)) in enumerate(rows):
        for col,view in enumerate(['top','front','side','iso']):
            image.paste(model_view.render(v,t,c,view,size,extent,label),(col*size,row*size))
    image.save(out)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',required=True)
    ap.add_argument('--game',default=rootcpk.DEFAULT_GAME)
    args=ap.parse_args()
    out, game_path=Path(args.out).resolve(),Path(args.game).resolve()
    if out==game_path or game_path in out.parents:
        ap.error('--out must be outside the game directory')
    if not drill_model.obj_path() or not artillery_model.model_dir():
        ap.error('both drill_tank and twin_tank user model folders are required')
    out.mkdir(parents=True,exist_ok=True)
    game=rootcpk.Game(str(game_path)); report={}
    for tag,mod,builder,clip,bones in [
        ('drill',drill_model,make_drill,'default',['catapi_body']),
        ('artillery',artillery_model,make_artillery,'doppler_radar_loop',['cannon_slide_l','cannon_slide_r'])]:
        files=builder.build(str(game_path))
        for rel,data in files.items():
            path=out/rel; path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        archive=out/'OBJECT'/builder.MODEL_FILE
        md=model_view.load(str(archive),builder.MODEL_MDB)
        old=CasPose(game.read('OBJECT',mod.HOST_CAS)); new=CasPose(files['OBJECT/'+mod.OUT_CAS])
        verify_motion(old,new,{drill_model.SPIN_BONE} if tag=='drill' else set())
        before=locals_from(md,old,clip);after=locals_from(md,new,clip)
        baseline=model_view.posed_world(md,{})
        wrong=model_view.posed_world(md,{},before);right=model_view.posed_world(md,{},after)
        offsets={}
        for name in bones:
            i=md.bone_index(name)
            a=math.dist(wrong[i][12:15],baseline[i][12:15]);b=math.dist(right[i][12:15],baseline[i][12:15])
            assert a>2.0,(tag,name,'old animation no longer reproduces the displacement',a)
            assert b<1e-4,(tag,name,'retargeted animation still displaces geometry',b)
            offsets[name]={'old_displacement_m':a,'new_displacement_m':b}
        if tag=='drill':
            # Every spin angle retains its centre and length because neither stock clip
            # can write the plugin-owned local matrix back to the track rig's position.
            assert all(not any(t.name==mod.SPIN_BONE for t in c.tracks) for c in new.clips)
            for angle in (0,45,90,180,270):
                spun=model_view.posed_world(md,{mod.SPIN_BONE:[('z',angle)]},after)
                i=md.bone_index(mod.SPIN_BONE)
                assert math.dist(spun[i][12:15],baseline[i][12:15])<1e-5
        else:
            for t in next(c for c in old.clips if c.name=='fire_loop').tracks:
                at=old.points+t.translation*48
                assert old.data[at:at+48]==new.data[at:at+48], 'additive recoil was rebased'
        preview(md,before,after,str(out/(tag+'-cas-poses.png')))
        report[tag]={'offsets':offsets,'generated_files':list(files),'animation_bytes':len(new.data)}
        print(tag,json.dumps(offsets),flush=True)
    (out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('Real MRAB + CANM replay passed; game runtime rendering remains unverified.')


if __name__=='__main__':
    main()
