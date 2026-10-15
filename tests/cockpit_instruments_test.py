"""Model instrument contract, independent of game files and third-party cockpit assets."""
from pathlib import Path
import sys
import math

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'pylib'))
import cockpit_instruments as ci
from cockpits import Screen
from cockpit_shapes import basis

for kind in ('status','weapons','flight','map'):
    s=Screen(kind,(.2,-.3,.7),(0,.18,-.984),.18,.12)
    parts, controls=ci.instrument(s,0)
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
        assert sum(c.control.startswith('d_') for c in controls)==35
        _,passenger=ci.instrument(s,0,numbers=False)
        assert len(passenger)==4,'passenger status bars keep the full vehicle under the ubyte skin-index budget'
    if kind=='map':
        assert sum(c.control.startswith('t_') for c in controls)==6
print('physical instrument geometry and controls passed')

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
