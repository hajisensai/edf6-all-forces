"""Model instrument contract, independent of game files and third-party cockpit assets."""
from pathlib import Path
import sys
import math

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'pylib'))
import cockpit_instruments as ci
from cockpits import Screen
from cockpit_shapes import basis

# Longitudinal and wide hulls produce distinct, proportionate model-derived outlines.
model=[[(0,0,-2),(1,0,2),(-1,0,2)],[(0,0,-2),(-.3,0,-1),(.3,0,-1)]]
thumb=ci.silhouette(model)
assert thumb[0] and abs(thumb[1]-.5)<1e-6
assert ci.silhouette([])==((),1.)
assert ci.silhouette(model,'front')==((),1.),'a flat top-view fixture is not an invented mech front'
assert ci.silhouette([[(-1,0,0),(1,0,0),(0,4,0)]],'front')[1]==.5
for language in range(4):
    for variants in ci.LABELS.values():
        assert all(ch in ci.GLYPHS and (ch==' ' or any(ci.GLYPHS[ch][1])) for ch in variants[language])
assert len({ci.GLYPHS[ch] for ch in '耐久车体車體燃料弹药彈藥弾薬武器兵装警告速度高度航向方位'})>20

for kind in ('status','weapons','flight','map','leftarms','rightarms','toparms'):
    s=Screen(kind,(.2,-.3,.7),(0,.18,-.984),.18,.12)
    parts, controls=ci.instrument(s,0,thumbnail=thumb)
    assert controls and len({c.part for c in controls})==len(controls)
    assert len({c.control for c in controls})==len(controls)
    by_name={p.name:p for p in parts}
    right,up,normal=basis(s.facing,s.up)
    for p in parts:
        assert p.material in ci.MATERIALS
        for v in p.verts:
            rel=tuple(v.pos[k]-s.centre[k] for k in range(3))
            dot=lambda a,b: sum(x*y for x,y in zip(a,b))
            assert all(math.isfinite(x) for x in v.pos)
            assert abs(dot(rel,right))<=s.width*.501,(kind,p.name,'outside display width')
            assert abs(dot(rel,up))<=s.height*.501,(kind,p.name,'outside display height')
            assert 0<=dot(rel,normal)+1e-9<=.005,(kind,p.name,'not on display surface')
    for c in controls:
        assert c.part in by_name and by_name[c.part].tris
    if kind=='flight':
        assert {f'n_{ci.SPEED}',f'n_{ci.ALT}'} <= {c.control for c in controls}
    if kind=='status':
        assert sum(c.control.startswith('p_') for c in controls)==21
        assert not any(c.control.startswith('d_0_') for c in controls),'large raw HP digits replaced by percent'
        assert {f'c_{ci.HP}_{i}' for i in range(3)} <= {c.control for c in controls}
        assert any(p.name.endswith('vehicle_outline') for p in parts)
        _,passenger=ci.instrument(s,0,numbers=False,thumbnail=thumb)
        assert len(passenger)==9,'shared passenger state fits the ubyte skin-index budget'
        assert not any(c.control.startswith(f'b_{ci.HULL}') for c in controls),'no redundant unknown hull bar'
    if kind not in ('leftarms','rightarms','toparms'):
        assert {f'l_{i}_0' for i in range(4)} <= {c.control for c in controls}
    if kind=='map':
        assert sum(c.control.startswith('T_') for c in controls)==6
        assert all(c.extent==(s.width,s.height) for c in controls if c.control.startswith('T_'))
print('physical instrument geometry and controls passed')

# Tall transport displays and five-digit altitude scales must remain inside their measured aperture.
tall=Screen('flight',(0,0,0),(0,0,-1),.19,.26)
parts,_=ci.instrument(tall,0,ranges={ci.SPEED:700,ci.ALT:10000})
right,up,_=basis(tall.facing)
for p in parts:
    for v in p.verts:
        assert abs(sum(v.pos[k]*right[k] for k in range(3)))<=tall.width*.501,(p.name,'tall width')
        assert abs(sum(v.pos[k]*up[k] for k in range(3)))<=tall.height*.501,(p.name,'tall height')

# The display/text gap is sub-centimetre, even when the vehicle's model-space coordinates are large. Test the
# actual vertex packer with a widened hull layout, including subsequent attribute offsets.
import struct
import cockpits
from mdb import Mesh,VElem
layout=Mesh(bytes((0,1,1,0)),0,0,16,[VElem(7,0,0,'position'),VElem(12,8,0,'texcoord')],0,b'',b'')
wide=cockpits.precise_template(layout)
assert wide.vsize==24 and wide.elems[0].fmt==1 and wide.elems[1].offset==16
import obj_model as om
vertex=om.Vertex((3.1234,2.0123,50.001),(0,0,-1),(.25,.75))
row=om._row(wide.elems,vertex,((1,0,0),(0,1,0)),[(0,1.)])
packed=om.pack_vertex(wide.elems,wide.vsize,row)
pos=struct.unpack_from('<4f',packed,0)
assert max(abs(a-b) for a,b in zip(pos[:3],vertex.pos))<2e-6
assert struct.unpack_from('<2f',packed,16)==(.25,.75)

# A raised cab rim in the middle of a display is missed by centre/corner rays. Complete triangle clipping must
# clear it, and must exclude a separate wheel well in front of the dashboard.
import numpy as np
import cockpit_kits_ground as ground
centre=(0.,0.,1.)
ridge=np.array([[[.02,-.03,.994],[.04,-.03,.994],[.03,.03,.994]]])
assert abs(ground._panel_clearance(ridge,centre,.2,.1)-(.006+ground.PROUD))<1e-8
behind=ridge.copy();behind[:,:,2]=1.02
assert ground._panel_clearance(behind,centre,.2,.1)==0
wheel=ridge.copy();wheel[:,:,2]=.7
assert ground._panel_clearance(wheel,centre,.2,.1)==0,'the steering wheel must remain an occluder'

try:
    cab=ground.real_cab(dict(left=1.97,right=.77,up=1.13,down=.68,ahead=.94,back=1.21),ground.CABS['truck_cab'])
except (FileNotFoundError, cockpits.CockpitAssetsMissing):
    print('cab assets unavailable; synthetic panel-clearance checks passed')
else:
    corners=np.concatenate([np.array([v.pos for v in p.verts])[p.tris] for p in cab.parts if p.name.startswith('cab')])
    for screen in cab.screens:
        r,u,_=map(np.array,basis(screen.facing));c=np.array(screen.centre)
        for y in np.linspace(-.45,.45,21):
            for x in np.linspace(-.45,.45,31):
                point=c+r*x*screen.width+u*y*screen.height;distance=np.linalg.norm(point)
                assert ground._first_hit(corners,point/distance,.001)>=distance-1e-4,(screen.kind,x,y)
    print('real truck screens: 0/651 obscured sample points each')

# A single weapon slot and each of the five ammo digits have physical digit
# proportions, independent of display aspect ratio; no wide bars or cutoff digits.
import numpy as np
for width,height in ((.13,.10),(.18,.10),(.10,.18)):
    display=Screen('weapons',(0,0,0),(0,0,-1),width,height)
    parts,_=ci.instrument(display,0)
    for field,count in ((ci.AMMO0,5),(ci.AMMO1,5),(ci.AMMO2,5),(ci.AMMO3,5)):
        for place in range(count):
            pts=np.array([v.pos for p in parts if p.name.startswith(f'instrument_0_digit_{field}_{place}_') for v in p.verts])
            extent=np.ptp(pts,axis=0)
            assert .35<extent[0]/extent[1]<.65,'digital glyph must not stretch into horizontal bars'
            assert abs(pts[:,0]).max()<width*.48,'every ammo digit stays inside the physical aperture'
print('ammo and slot glyph proportions passed')

for kind,fields in [('leftarms',(ci.AMMO0,)),('rightarms',(ci.AMMO1,)),('toparms',(ci.AMMO2,ci.AMMO3))]:
    _,controls=ci.instrument(Screen(kind,(0,0,0),(0,0,-1),.21,.155),0)
    actual={int(c.control.split('_')[1]) for c in controls if c.control.startswith('k_')}
    assert actual==set(fields),(kind,actual)
print('all hand and roof channels have simultaneous, disjoint physical counters')

# The centre console's retained grips must not hide either roof magazine. Test
# actual eye rays through the counter region, not merely the widget's own bounds.
import cockpit_kits_mech as mech
try: seat_asset=Path(cockpits.asset_root())/mech.SEAT_ASSET
except cockpits.CockpitAssetsMissing:seat_asset=Path('_missing_mech_asset')
if seat_asset.is_file():
    for name in mech.PODS:
        kit=mech.KITS[name]({side:10. for side in ('left','right','up','down','ahead','back')})
        triangles=np.concatenate([np.asarray([v.pos for v in p.verts])[p.tris] for p in kit.parts if p.tris])
        a=triangles[:,0];e1=triangles[:,1]-a;e2=triangles[:,2]-a;q=np.cross(-a,e1)
        for screen in kit.screens:
            if screen.kind not in ('leftarms','rightarms','toparms'):continue
            right,up,_=map(np.asarray,basis(screen.facing,screen.up))
            for x in np.linspace(-.24,.32,8):
                for y in np.linspace(-.28,.29,9):
                    point=np.asarray(screen.centre)+right*x*screen.width+up*y*screen.height
                    length=np.linalg.norm(point);direction=point/length
                    p=np.cross(direction,e2);det=np.einsum('ij,ij->i',e1,p);inv=1/np.where(abs(det)>1e-10,det,1)
                    u=np.einsum('ij,ij->i',-a,p)*inv;v=q@direction*inv;t=np.einsum('ij,ij->i',e2,q)*inv
                    blocked=(det>1e-10)&(u>=0)&(v>=0)&(u+v<=1)&(t>1e-4)&(t<length-.004)
                    assert not blocked.any(),(name,screen.kind,'control hardware hides a magazine',x,y)
    print('all mech hand and roof counter sightlines clear of control hardware')
