"""Real cut-outs preserve neighbouring geometry and texture interpolation; refits contain live pivots."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'pylib'))
import numpy as np
import cockpit_retrofit as rt
from cockpit_shapes import panel,basis
from cockpits import Kit,KitMaterial,Screen

source=panel('source','original',(0,0,0),2,2,(0,0,-1))
axes=np.asarray(basis((0,0,-1)));half=np.array((.25,.25,.025))
removed=[];cut=rt.subtract(source,np.zeros(3),axes,half,removed)
area=lambda part:sum(np.linalg.norm(np.cross(np.array(part.verts[b].pos)-part.verts[a].pos,
                                            np.array(part.verts[c].pos)-part.verts[a].pos))/2 for a,b,c in part.tris)
assert abs(area(cut)-3.75)<1e-8
assert abs(sum(area(p) for p in removed)-.25)<1e-8
for v in cut.verts:
    assert np.allclose(v.uv,((1-v.pos[0])/2,(1-v.pos[1])/2)),'cut edges keep interpolated source UVs'
wheel=panel('wheel','original',(0,0,-.2),.5,.5,(0,0,-1))
assert rt.subtract(wheel,np.zeros(3),axes,half) is wheel,'nearer wheel remains an occluder'
back=panel('back','original',(0,0,.2),2,2,(0,0,-1))
assert rt.subtract(back,np.zeros(3),axes,half) is back,'unrelated surface behind the panel survives'
edge=panel('edge','original',(-.25,0,0),.04,.3,(1,0,0))
pieces=[];outside=rt.subtract(edge,np.zeros(3),axes,half,pieces)
assert abs(area(outside)+sum(area(p) for p in pieces)-area(edge))<1e-9,'coplanar boundary is not duplicated'

kit=Kit([source,wheel],{'original':KitMaterial((50,50,50))})
block=rt.Block('test',(0,0,0),(0,0,-1),1,1,2,(rt.ci.SPEED,rt.ci.FUEL))
rt.apply(kit,(block,),ground=True)
assert {c.control for c in kit.controls if c.control.startswith('r_')}=={'r_3_160','r_2_100'}
assert {f'l_{i}_0' for i in range(4)}<={c.control for c in kit.controls}
assert {f'u_{i}' for i in (rt.ci.SPEED,rt.ci.FUEL)}<={c.control for c in kit.controls}
assert any(p.name=='wheel' and p.verts==wheel.verts for p in kit.parts)
assert all(np.isfinite(v.pos).all() for p in kit.parts for v in p.verts)

# Already-scaled display coordinates must not be transformed again; the old raised lens must actually be cut out.
c=(0,-.3,.5);face=panel('old_face','original',c,.3,.2,(0,0,-1))
lens=panel('old_lens','original',(0,-.3,.49),.06,.025,(0,0,-1))
k=Kit([face,lens],{'original':KitMaterial((50,50,50))},screens=(Screen('status',c,(0,0,-1),.10,.05),))
rt.apply(k,(),lambda p:np.asarray(p)*.5)
assert not any(p.name.startswith('old_lens') and p.tris for p in k.parts),'old lens cannot occlude the new LCD'
backing=next(p for p in k.parts if p.name=='screen_status')
assert np.allclose(np.mean([v.pos for v in backing.verts],axis=0),(0,-.3,.5005)),'auto display remains in final cab coordinates'

# Oblique LCDs need a cut through the seated eye's view cone: a perpendicular box tears the surrounding hood.
rear=panel('rear','original',(0,-.3,.53),.5,.5,(0,0,-1));pieces=[]
rt.subtract(rear,np.array(c),axes,np.array((.05,.025,.045)),pieces,view=True)
assert abs(sum(area(p) for p in pieces)-.1*.05*(.53/.5)**2)<1e-8
for p in pieces:
    for v in p.verts:
        projected=np.array(v.pos)*(.5/v.pos[2]);relative=(projected-np.array(c))@axes.T
        assert abs(relative[0])<=.05+1e-8 and abs(relative[1])<=.025+1e-8,'cut stays behind the visible LCD opening'

small=Kit([source],{'original':KitMaterial((50,50,50))})
rt.apply(small,(rt.Block('small_map',(0,0,0),(0,0,-1),.3,.1,mode='readout_map'),))
contacts=[c for c in small.controls if c.control.startswith('T_')]
# A contact moves in the nav card's own radius (its range at .35): the same both ways (true bearings) and
# within the added display's own face, not the last main display's.
assert contacts and all(c.extent[0]==c.extent[1]==contacts[0].extent[0] for c in contacts)
assert .35*contacts[0].extent[0]<=.1*.92/2+1e-9,'added map widgets carry their own extents'
print('source cut-outs, UV preservation, foreground occlusion and live analogue controls passed')

# Surveyed apertures are not a regular grid. Their centres and case diameters
# must survive fitting, including the live needle pivots, without automatic shrink.
sites=(rt.DialAt((-.27,.16,0),.20),rt.DialAt((.19,-.13,0),.16))
block=rt.Block('surveyed',(0,0,0),(0,0,-1),1,1,2,(rt.ci.CLIMB,rt.ci.SPEED),dials=sites)
for scale in (1.,.43):
    k=Kit([panel('source','original',(0,0,0),2*scale,2*scale,(0,0,-1))],{'original':KitMaterial((50,50,50))})
    rt.apply(k,(block,),lambda p:np.asarray(p)*scale)
    for i,site in enumerate(sites):
        needle=next(c for c in k.controls if c.part==f'retrofit_surveyed_{i}_needle')
        assert np.allclose(needle.centre[:2],np.asarray(site.centre[:2])*scale)
        rim=next(p for p in k.parts if p.name==f'retrofit_surveyed_{i}_rim')
        xyz=np.array([v.pos for v in rim.verts]);assert np.allclose(np.ptp(xyz,axis=0)[:2],site.diameter*scale)
    bad=rt.Block('bad',(0,0,0),(0,0,-1),1,1,2,(rt.ci.SPEED,rt.ci.ALT),dials=sites[:1])
    try:rt.apply(Kit([source],{'original':KitMaterial((50,50,50))}),(bad,))
    except ValueError:pass
    else:raise AssertionError('a missing surveyed mount must not silently fall back to the grid')
print('surveyed dial mounts and fitted control pivots passed')

# Captions sit below the lowest numeric scale and remain inside the face, in
# every language. Enlarging cases to their bores must not enlarge a text overlap.
for field in rt.TITLES:
    if field in (rt.ci.HEADING,rt.ci.PITCH,rt.ci.ALT):continue   # cards and a whole-dial altimeter: checked below
    labels={i:[] for i in range(4)}
    parts,_=rt._dial('layout',(0,0,0),(0,0,-1),(0,1,0),.118,field,False,labels,{})
    numbers=[v.pos[1] for p in parts if p.name.endswith('_number') for v in p.verts]
    captions=[p for group in labels.values() for p in group]+[p for p in parts if p.name.endswith('_unit')]
    for p in captions:
        assert max(v.pos[1] for v in p.verts)<min(numbers),'caption overlaps the lower numeric scale'
        assert max(np.linalg.norm(v.pos[:2]) for v in p.verts)<=.118*.5*.91+1e-9,'caption exceeds the dial face'

# The cards and the altimeter: the compass card's numbers and the altimeter's lie inside the face; the attitude card's
# pivot is the dial's centre, its travel no more than its bezel hides.
for field in (rt.ci.HEADING,rt.ci.PITCH,rt.ci.ALT):
    parts,controls=rt._dial('card',(0,0,0),(0,0,-1),(0,1,0),.118,field,False,{i:[] for i in range(4)},{})
    for p in parts:
        if p.name.endswith(('_card','_number','_unit','_title')):
            assert max(np.linalg.norm(v.pos[:2]) for v in p.verts)<=.118*.5*.91+1e-9,(field,p.name,'off the face')
    if field==rt.ci.PITCH:
        moving=[c for c in controls if c.control.startswith('g_')]
        assert len(moving)==3 and all(np.allclose(c.centre[:2],0) for c in moving)
        _,_,degree,travel=moving[0].control.split('_')
        lip=next(p for p in parts if p.name.endswith('_bezel'))
        window=min(np.linalg.norm(v.pos[:2]) for v in lip.verts);outer=max(np.linalg.norm(v.pos[:2]) for v in lip.verts)
        reach=max(np.linalg.norm(v.pos[:2]) for p in parts if p.name.endswith(('_sky','_ground')) for v in p.verts)
        shift=int(travel)*1e-4*.059
        assert reach>=window+shift-1e-6 and reach+shift<=outer+1e-6,'the card covers the window and hides behind the bezel'
print('compass card, attitude card and altimeter faces passed')
