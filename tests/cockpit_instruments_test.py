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

for kind in ('status','weapons','flight','map'):
    s=Screen(kind,(.2,-.3,.7),(0,.18,-.984),.18,.12)
    parts, controls=ci.instrument(s,0,thumbnail=thumb)
    assert controls and len({c.part for c in controls})==len(controls)
    # Visibility-only controls may share a bone (cockpits.graft's `reusable`); every other moves its own.
    moved=[c.control for c in controls if c.control[0] not in 'dDplcuve']
    assert len(set(moved))==len(moved)
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
    if kind=='weapons':
        names={c.control for c in controls}
        assert {f'l_{i}_{g+1}' for i in range(4) for g in (ci.LOAD,ci.EMPTY)} <= names,'LOADING / EMPTY in each language'
        assert sum(c.startswith(f'D_{ci.AMMO}_') for c in names)==28,'four rounds digits'
        assert not any(c.startswith(f'D_{ci.SLOT}_') for c in names),'no lone slot digit'
        assert {f'e_{ci.ARM}_{g}' for g in range(ci.CHARGE+1)} <= names
        assert {f'e_{ci.SHELL}_{k}' for k in range(1,len(ci.SHELL_TAGS)+1)} <= names
        assert {f'{m}_{f}' for m,f in (('v',ci.MAG),('B',ci.MAG),('B',ci.LOAD))} <= names
        for i in range(ci.ROWS):
            assert {f'v_{ci.ROW+i}',f'B_{ci.ROW+i}',f'B_{ci.ROWLOAD+i}',f'e_{ci.PICK}_{i}'} <= names
    else:
        assert {f'l_{i}_0' for i in range(4)} <= {c.control for c in controls}
    if kind=='map':
        assert sum(c.control.startswith('T_') for c in controls)==6
        assert all(c.extent==(s.width,s.height) for c in controls if c.control.startswith('T_'))
print('physical instrument geometry and controls passed')


def extent(parts, screen, test):
    """(left, right, bottom, top) in screen fractions of the parts whose name passes `test`."""
    right,up,_=basis(screen.facing,screen.up)
    xs,ys=[],[]
    for p in parts:
        if test(p.name):
            for v in p.verts:
                rel=[v.pos[k]-screen.centre[k] for k in range(3)]
                xs.append(sum(rel[k]*right[k] for k in range(3))/screen.width)
                ys.append(sum(rel[k]*up[k] for k in range(3))/screen.height)
    assert xs,'nothing matched'
    return min(xs),max(xs),min(ys),max(ys)


def apart(a, b):
    return a[1]<=b[0] or b[1]<=a[0] or a[3]<=b[2] or b[3]<=a[2]


# The weapons screen on the displays the kits have (a tank's, a heli's, a truck's, the C-130's tall one): a digit keeps a
# digit's proportions whatever the screen's shape (its cells were the field's width over the count: one digit across a
# wide screen came out a flat smear, five squat), and the top line, the picture, the rounds, the fill and the foot's
# cells never overlap, in every language.
for w,h in ((.11,.075),(.14,.07),(.16,.08),(.13,.10),(.1,.08),(.19,.26),(.17,.13)):
    s=Screen('weapons',(.1,-.2,.6),(0,.2,-.98),w,h)
    parts,_=ci.instrument(s,0)
    for place in range(4):
        x0,x1,y0,y1=extent(parts,s,lambda n,place=place: f'_digit_{ci.AMMO}_{place}_' in n)
        aspect=(x1-x0)*w/((y1-y0)*h)
        assert abs(aspect-ci.DIGIT_ASPECT)<.02,(w,h,place,aspect)
    digits=extent(parts,s,lambda n: f'_digit_{ci.AMMO}_' in n)
    picture=extent(parts,s,lambda n: '_arm_' in n)
    tags=extent(parts,s,lambda n: '_shell_' in n)
    fill=extent(parts,s,lambda n: n.endswith(('_magazine_track',)))
    foot=extent(parts,s,lambda n: '_row_' in n)
    for language in range(4):
        for gate in (ci.LOAD+1,ci.EMPTY+1):
            caption=extent(parts,s,lambda n,l=language,g=gate: n.endswith(f'_caption_{l}_{g}'))
            assert caption[0]>=-.5 and caption[1]<=.5,(w,h,language,gate,'caption off the screen')
            for other,what in ((digits,'rounds'),(picture,'picture'),(tags,'tag'),(fill,'fill')):
                assert apart(caption,other),(w,h,language,gate,what)
    for a,b,what in ((digits,picture,'rounds / picture'),(digits,fill,'rounds / fill'),(picture,fill,'picture / fill'),
                     (fill,foot,'fill / foot'),(tags,picture,'tag / picture'),(digits,tags,'rounds / tag')):
        assert apart(a,b),(w,h,what)
print('weapons screen: digits keep their shape, its blocks never overlap on any display')

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
