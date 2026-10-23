"""Surveyed instrument-face replacements, cut from source geometry before installing live hardware.

Coordinates are in each prepared asset's seated-eye frame. Structural cabs, yokes and wheels outside the
shallow replacement prisms retain their source geometry, UVs and materials. Missing engine sensors stay unknown.
"""
from __future__ import annotations

from dataclasses import dataclass
import math
import numpy as np

import cockpit_instruments as ci
from cockpit_shapes import basis, cylinder, frame, merge, panel
from cockpits import InstrumentControl, Kit, KitMaterial, Screen
import obj_model as om


@dataclass(frozen=True)
class DialAt:
    centre: tuple[float,float,float]
    diameter: float


@dataclass(frozen=True)
class Block:
    name: str
    centre: tuple[float,float,float]
    facing: tuple[float,float,float]
    width: float
    height: float
    columns: int = 1
    fields: tuple[int | None,...] = ()
    mode: str = 'dials'
    dials: tuple[DialAt,...] = ()  # surveyed mounting centres/sizes, before the asset's fit transform


MATERIALS = {
    'retrofit_plate': KitMaterial((43,48,46)),
    'retrofit_rim': KitMaterial((83,91,85)),
    'retrofit_face': KitMaterial((12,18,17)),
    'retrofit_scale': KitMaterial((148,171,150),emissive=True),
    'retrofit_needle': KitMaterial((210,161,82),emissive=True),
    'retrofit_knob': KitMaterial((28,33,31)),
    'retrofit_sky': KitMaterial((40,100,165),emissive=True),
    'retrofit_ground': KitMaterial((115,76,40),emissive=True),
}
RANGES = {ci.SPEED:2000,ci.ALT:10000,ci.HEADING:360,ci.FUEL:100,ci.HP:100,
          ci.CLIMB:50,ci.PITCH:90,ci.POWER:100}
TITLES = {ci.SPEED:'KMH',ci.ALT:'ALT M',ci.HEADING:'HDG',ci.FUEL:'FUEL',ci.HP:'HP',
          ci.CLIMB:'VERT',ci.PITCH:'PITCH',ci.POWER:'POWER'}


def _clip(poly, axis, bound, sign):
    return _clip_distance(poly,lambda q:sign*(q[axis]-bound))


def _clip_distance(poly,distance):
    out=[]
    for i,cur in enumerate(poly):
        prev=poly[i-1];dc=distance(cur[0]);dp=distance(prev[0])
        if (dc>=0)!=(dp>=0):
            t=dp/(dp-dc)
            out.append(tuple(prev[k]+t*(cur[k]-prev[k]) for k in range(3)))
        if dc>=0:out.append(cur)
    return out


def subtract(part: om.Part, centre, axes, half, removed=None, view=False) -> om.Part:
    """Subtract an oriented prism, splitting crossing triangles and interpolating normals/UVs."""
    if not part.tris:return part
    pos=np.asarray([v.pos for v in part.verts]);centre=np.asarray(centre)
    planes=[]
    for axis in range(3):
        n=axes[axis];origin=centre@n
        if view and axis<2:
            f=axes[2];depth=centre@f
            for bound,sign in ((origin-half[axis],1),(origin+half[axis],-1)):
                normal=sign*(bound*f-depth*n);normal/=np.linalg.norm(normal)
                planes.append((normal,0.))
        else:
            planes.extend(((n,origin-half[axis]),(-n,-origin-half[axis])))
    distances=np.stack([pos@n-bound for n,bound in planes],axis=1)
    tris=np.asarray(part.tris)
    candidate=~(distances[tris].max(1)<-1e-10).any(1)
    if not candidate.any():return part
    verts=list(part.verts);kept=[tuple(map(int,t)) for t in tris[~candidate]];face_verts=[];face_tris=[]
    for indices in tris[candidate]:
        remaining=[(pos[i],np.asarray(verts[i].normal),np.asarray(verts[i].uv)) for i in indices]
        for normal,bound in planes:
            if not remaining:break
            distance=lambda q:float(q@normal-bound)
            coplanar=all(abs(distance(v[0]))<1e-9 for v in remaining)
            outside=[] if coplanar else _clip_distance(remaining,lambda q:-distance(q))
            if len(outside)>=3:
                start=len(verts)
                for q,n,uv in outside:
                    n=n/max(np.linalg.norm(n),1e-12)
                    verts.append(om.Vertex(tuple(q),tuple(n),tuple(uv)))
                kept.extend((start,start+i,start+i+1) for i in range(1,len(outside)-1))
            remaining=_clip_distance(remaining,distance)
        if removed is not None and len(remaining)>=3:
            start=len(face_verts)
            for q,n,uv in remaining:
                n=n/max(np.linalg.norm(n),1e-12)
                face_verts.append(om.Vertex(tuple(q),tuple(n),tuple(uv)))
            face_tris.extend((start,start+i,start+i+1) for i in range(1,len(remaining)-1))
    if removed is not None and face_tris:
        removed.append(om.Part(part.name+'_refinished','retrofit_plate',face_verts,face_tris))
    if not kept:return om.Part(part.name,part.material,[],[])
    used,inv=np.unique(np.asarray(kept).reshape(-1),return_inverse=True)
    return om.Part(part.name,part.material,[verts[i] for i in used],[tuple(map(int,t)) for t in inv.reshape(-1,3)])


def _surface(corners,point,normal,depth=.055):
    """Orthogonal placement on the surveyed source face, excluding nearer wheels and distant walls."""
    origin=np.asarray(point)+normal*depth;direction=-normal
    v0=corners[:,0];e1=corners[:,1]-v0;e2=corners[:,2]-v0
    p=np.cross(direction,e2);det=np.einsum('ij,ij->i',e1,p)
    valid=np.abs(det)>1e-10;inv=np.where(valid,1/np.where(valid,det,1),0)
    rel=origin-v0;u=np.einsum('ij,ij->i',rel,p)*inv;q=np.cross(rel,e1)
    v=q@direction*inv;t=np.einsum('ij,ij->i',e2,q)*inv
    n=np.cross(e1,e2);length=np.linalg.norm(n,axis=1)
    alignment=(n@normal)/np.maximum(length,1e-12)
    hits=np.flatnonzero(valid&(u>=-1e-6)&(v>=-1e-6)&(u+v<=1+1e-6)&(t>0)&(t<2*depth)&(alignment>.75))
    if not len(hits):return None
    i=hits[np.argmin(abs(t[hits]-depth))]
    return origin+direction*t[i]


def _front(corners,centre,axes,radius):
    """Clear raised source bezels throughout the dial footprint, not just its centre ray."""
    local=(corners-centre)@axes.T
    normal=np.cross(local[:,1]-local[:,0],local[:,2]-local[:,0])
    candidate=((local[:,:,:2].max(1)>=-radius)&(local[:,:,:2].min(1)<=radius)).all(1)
    candidate &= (local[:,:,2].max(1)<.055)&(normal[:,2]>.7*np.linalg.norm(normal,axis=1))
    front=-.055
    for triangle in local[candidate]:
        poly=[(v,np.zeros(3),np.zeros(2)) for v in triangle]
        for axis,bound,sign in ((0,-radius,1),(0,radius,-1),(1,-radius,1),(1,radius,-1)):
            poly=_clip(poly,axis,bound,sign)
            if not poly:break
        if poly:front=max(front,max(v[0][2] for v in poly))
    return front


def _text(name,text,at,width,height,material,facing,up):
    """A crisp model glyph run; SI units and numeric scales are language independent."""
    glyphs=[]
    for ch in text:
        if ch in ci.GLYPHS:
            w,rows=ci.GLYPHS[ch];glyphs.append((w,[[bool(row>>x&1) for x in range(w)] for row in rows]))
        else:
            lines=ci.FONT.get(ch.upper(),('000',)*5)
            glyphs.append((3,[[b=='1' for b in row] for row in lines]))
    total=sum(w+2 for w,_ in glyphs)-2;high=max(len(rows) for _,rows in glyphs)
    unit=min(width/max(total,1),height/high);cursor=-total/2;bits=[]
    r,u,_=basis(facing,up)
    for w,rows in glyphs:
        for y,row in enumerate(rows):
            x=0
            while x<w:
                if not row[x]:x+=1;continue
                end=x+1
                while end<w and row[end]:end+=1
                c=tuple(at[k]+r[k]*(cursor+(x+end)/2)*unit+u[k]*((high-1)/2-y)*unit for k in range(3))
                bits.append(panel(name,material,c,(end-x)*unit,unit,facing,up));x=end
        cursor+=w+2
    return merge(bits,name) if bits else om.Part(name,material,[],[])


def _disc(name,material,at,r,u,facing,radius,a0=0.,a1=360.,n=40):
    """A disc (or the sector a0..a1 degrees clockwise from up) in the face's plane at `at` (a function of x, y)."""
    pts=[(radius*math.sin(math.radians(a0+(a1-a0)*i/n)),radius*math.cos(math.radians(a0+(a1-a0)*i/n))) for i in range(n+1)]
    pts=[(0.,0.)]+pts if a1-a0<360 else pts[:-1]
    verts=[om.Vertex(at(x,y),tuple(facing),(0.,0.)) for x,y in pts]
    # Counter-clockwise seen from the front: a fan turning anticlockwise in (right, up).
    tris=[(0,i+1,i) for i in range(1,len(pts)-1)]
    return om.Part(name,material,verts,tris)


def _bar(name,material,at,facing,x0,y0,x1,y1,t):
    dx,dy=x1-x0,y1-y0;ln=math.hypot(dx,dy) or 1.;px,py=-dy/ln*t/2,dx/ln*t/2
    pts=[(x0-px,y0-py),(x1-px,y1-py),(x1+px,y1+py),(x0+px,y0+py)]
    if sum(pts[i-1][0]*pts[i][1]-pts[i][0]*pts[i-1][1] for i in range(4))<0:pts=pts[::-1]
    return om.Part(name,material,[om.Vertex(at(x,y),tuple(facing),(0.,0.)) for x,y in pts],[(0,1,2),(0,2,3)])


def _annulus(name,material,at,facing,inner,outer,n=48):
    verts,tris=[],[]
    for i in range(n):
        a,b=2*math.pi*i/n,2*math.pi*(i+1)/n
        quad=[(inner*math.sin(a),inner*math.cos(a)),(inner*math.sin(b),inner*math.cos(b)),
              (outer*math.sin(b),outer*math.cos(b)),(outer*math.sin(a),outer*math.cos(a))]
        base=len(verts);verts+=[om.Vertex(at(x,y),tuple(facing),(0.,0.)) for x,y in quad]
        tris+=[(base,base+1,base+2),(base,base+2,base+3)]
    return om.Part(name,material,verts,tris)


def _compass(name,centre,facing,up,diameter):
    """A standby compass: a card a tick each 5 degrees (longer each 30), N / E / S / W and the tens of degrees between
    (3 for 030) turning with the heading (h_) under a fixed lubber line and aircraft mark. Not a needle on a 270 degree
    scale: the user, 2026-10-19: 「哪有方位盘用那种表盘的」."""
    r,u,f=basis(facing,up);radius=diameter/2
    def at(x=0.,y=0.,z=0.):
        return tuple(centre[k]+r[k]*x+u[k]*y+f[k]*z for k in range(3))
    parts=[cylinder(name+'_rim','retrofit_rim',at(z=-.002),at(z=.003),radius,32),
           cylinder(name+'_face','retrofit_face',at(z=.003),at(z=.004),radius*.91,32)]
    card=[]
    for deg in range(0,360,5):
        a=math.radians(deg);long=deg%30==0;mid=deg%10==0
        r0=radius*(.66 if long else .72 if mid else .76)
        card.append(_bar(name+'_card','retrofit_scale',lambda x,y:at(x,y,.0052),f,r0*math.sin(a),r0*math.cos(a),
                         radius*.82*math.sin(a),radius*.82*math.cos(a),radius*(.035 if long else .02)))
    for deg in range(0,360,30):
        a=math.radians(deg)
        text={0:'N',90:'E',180:'S',270:'W'}.get(deg,str(deg//10))
        card.append(_text(name+'_card',text,at(radius*.5*math.sin(a),radius*.5*math.cos(a),.0054),diameter*.2,
                          diameter*(.1 if deg%90==0 else .075),'retrofit_scale',f,_turn(u,r,deg)))
    card=merge(card,name+'_card')
    parts.append(card)
    lubber=_bar(name+'_lubber','retrofit_needle',lambda x,y:at(x,y,.007),f,0,radius*.86,0,radius*.68,radius*.06)
    plane=merge([_bar(name+'_plane','retrofit_needle',lambda x,y:at(x,y,.007),f,0,radius*.22,0,-radius*.2,radius*.05),
                 _bar(name+'_plane','retrofit_needle',lambda x,y:at(x,y,.007),f,-radius*.2,radius*.04,radius*.2,radius*.04,radius*.05),
                 _bar(name+'_plane','retrofit_needle',lambda x,y:at(x,y,.007),f,-radius*.08,-radius*.17,radius*.08,-radius*.17,
                      radius*.04)],name+'_plane')
    parts+=[lubber,plane]
    dash=_text(name+'_unknown','-',at(0,0,.008),diameter*.18,diameter*.10,'retrofit_scale',f,u)
    parts.append(dash)
    return parts,[InstrumentControl(card.name,f'h_{ci.HEADING}_0',at(z=.0052),f,u),
                  InstrumentControl(dash.name,f'u_{ci.HEADING}',at(z=.008),f,u)]


def _turn(u,r,degrees):
    """`u` turned `degrees` clockwise as the face is seen (toward its right `r`)."""
    a=math.radians(degrees)
    return tuple(u[k]*math.cos(a)+r[k]*math.sin(a) for k in range(3))


ATTITUDE_DEGREE=.014   # a standby attitude card's pitch scale: dial radii a degree


def _attitude(name,centre,facing,up,diameter):
    """A standby attitude indicator: sky, ground, horizon and a pitch ladder on one card dropped by the nose's pitch and
    turned against the bank (g_), seen through a round window in a bezel ring in front of it (what moves beyond the
    window is behind the ring), the fixed aircraft over it and the bank scale's ticks on the ring. Not a needle."""
    r,u,f=basis(facing,up);radius=diameter/2
    def at(x=0.,y=0.,z=0.):
        return tuple(centre[k]+r[k]*x+u[k]*y+f[k]*z for k in range(3))
    window,lip=radius*.62,radius*1.06
    travel=(lip-window)/2;reach=window+travel
    parts=[cylinder(name+'_rim','retrofit_rim',at(z=-.002),at(z=.003),radius,32),
           cylinder(name+'_face','retrofit_face',at(z=.003),at(z=.004),radius*.91,32)]
    def z(depth):
        return lambda x,y:at(x,y,depth)
    sky=_disc(name+'_sky','retrofit_sky',z(.0045),r,u,f,reach,-90,90)
    ground=_disc(name+'_ground','retrofit_ground',z(.0045),r,u,f,reach,90,270)
    lines=[_bar(name+'_lines','retrofit_scale',z(.005),f,-reach,0,reach,0,radius*.035)]
    for deg in (-20,-10,10,20):
        y=deg*ATTITUDE_DEGREE*radius;half=radius*(.22 if deg%20 else .14)
        lines.append(_bar(name+'_lines','retrofit_scale',z(.005),f,-half,y,half,y,radius*.025))
    lines=merge(lines,name+'_lines')
    parts+=[sky,ground,lines,_annulus(name+'_bezel','retrofit_rim',z(.0062),f,window,lip)]
    for deg in (-60,-30,-20,-10,0,10,20,30,60):
        a=math.radians(deg)
        parts.append(_bar(name+'_bank','retrofit_scale',z(.0064),f,window*1.02*math.sin(a),window*1.02*math.cos(a),
                          window*(1.2 if deg%30==0 else 1.12)*math.sin(a),window*(1.2 if deg%30==0 else 1.12)*math.cos(a),
                          radius*.03))
    wings=merge([_bar(name+'_wings','retrofit_needle',z(.007),f,-window*.7,0,-window*.25,0,radius*.05),
                 _bar(name+'_wings','retrofit_needle',z(.007),f,window*.25,0,window*.7,0,radius*.05),
                 _disc(name+'_wings','retrofit_needle',z(.007),r,u,f,radius*.04,n=12)],name+'_wings')
    parts.append(wings)
    control=f'g_{ci.PITCH}_{round(ATTITUDE_DEGREE*1e4)}_{round(travel/radius*1e4)}'
    extent=(radius,radius)   # the card moves in dial radii
    return parts,[InstrumentControl(p.name,control,at(z=.0045),f,u,extent) for p in (sky,ground,lines)]


def _altimeter(name,centre,facing,up,diameter):
    """An altimeter: 0..9 round the whole dial, the long hand once round each 1000 m, the short one each 10000 m (w_)."""
    r,u,f=basis(facing,up);radius=diameter/2
    def at(x=0.,y=0.,z=0.):
        return tuple(centre[k]+r[k]*x+u[k]*y+f[k]*z for k in range(3))
    parts=[cylinder(name+'_rim','retrofit_rim',at(z=-.002),at(z=.003),radius,32),
           cylinder(name+'_face','retrofit_face',at(z=.003),at(z=.004),radius*.91,32)]
    for tick in range(50):
        a=math.radians(tick*7.2);direction=tuple(r[k]*math.sin(a)+u[k]*math.cos(a) for k in range(3))
        parts.append(panel(name+'_tick','retrofit_scale',at(math.sin(a)*radius*.79,math.cos(a)*radius*.79,.005),
                           radius*.025,radius*(.15 if tick%5==0 else .07),f,direction))
    for n in range(10):
        a=math.radians(n*36)
        parts.append(_text(name+'_number',str(n),at(math.sin(a)*radius*.58,math.cos(a)*radius*.58,.0055),diameter*.14,
                           diameter*.1,'retrofit_scale',f,u))
    parts.append(_text(name+'_unit','x100m',at(0,-radius*.3,.006),diameter*.4,diameter*.07,'retrofit_scale',f,u))
    parts.append(_text(name+'_title','ALT',at(0,radius*.28,.006),diameter*.3,diameter*.07,'retrofit_scale',f,u))
    controls=[]
    for per,length,width,depth in ((10000,.5,.08,.008),(1000,.78,.045,.0085)):
        p=panel(name+f'_hand_{per}','retrofit_needle',at(0,radius*length/2,depth),radius*width,radius*length,f,u)
        parts.append(p)
        controls.append(InstrumentControl(p.name,f'w_{ci.ALT}_{per}',at(z=depth),f,u))
    dash=_text(name+'_unknown','-',at(0,-radius*.1,.0088),diameter*.18,diameter*.10,'retrofit_scale',f,u)
    parts.append(dash)
    controls.append(InstrumentControl(dash.name,f'u_{ci.ALT}',at(z=.0088),f,u))
    return parts,controls


def _dial(name,centre,facing,up,diameter,field,ground,labels,ranges):
    # A heading is a compass card, an attitude a horizon, an altitude a whole-dial altimeter: real instruments' faces.
    if field==ci.HEADING:return _compass(name,centre,facing,up,diameter)
    if field==ci.PITCH:return _attitude(name,centre,facing,up,diameter)
    if field==ci.ALT and not ground:return _altimeter(name,centre,facing,up,diameter)
    r,u,f=basis(facing,up);radius=diameter/2
    def at(x=0.,y=0.,z=0.):
        return tuple(centre[k]+r[k]*x+u[k]*y+f[k]*z for k in range(3))
    parts=[cylinder(name+'_rim','retrofit_rim',at(z=-.002),at(z=.003),radius,32),
           cylinder(name+'_face','retrofit_face',at(z=.003),at(z=.004),radius*.91,32)]
    maximum=160 if ground and field==ci.SPEED else RANGES.get(field,100)
    maximum=ranges.get(field,maximum)
    for tick in range(21):
        angle=math.radians(-135+270*tick/20);dx,dy=math.sin(angle),math.cos(angle)
        direction=tuple(r[k]*dx+u[k]*dy for k in range(3))
        parts.append(panel(name+'_tick','retrofit_scale',at(dx*radius*.79,dy*radius*.79,.005),
                           radius*.025,radius*(.15 if tick%5==0 else .07),f,direction))
    signed=field in (ci.CLIMB,ci.PITCH)
    for tick in (0,5,10,15,20):
        angle=math.radians(-135+270*tick/20)
        value=round(maximum*(tick/10-1 if signed else tick/20))
        parts.append(_text(name+'_number',str(value),at(math.sin(angle)*radius*.59,
                         math.cos(angle)*radius*.59,.0055),diameter*.28,diameter*.085,'retrofit_scale',f,u))
    title=TITLES.get(field,'-')
    if title in ci.LABELS:
        for language,text in enumerate(ci.LABELS[title]):
            labels[language].append(_text(name+'_title',text,at(0,-radius*.62,.006),diameter*.56,
                                          diameter*.09,'retrofit_scale',f,u))
    else:
        parts.append(_text(name+'_unit',title,at(0,-radius*.62,.006),diameter*.5,diameter*.09,'retrofit_scale',f,u))
    dash=_text(name+'_unknown','-',at(0,0,.008),diameter*.18,diameter*.10,'retrofit_scale',f,u)
    controls=[]
    if field is not None:
        # A single pivot drives the rigid needle. The case and scale never animate.
        p=panel(name+'_needle','retrofit_needle',at(0,radius*.31,.008),radius*.045,radius*.72,f,u)
        parts.append(p)
        mode='a' if signed else 'q' if field==ci.HP else 'r'
        controls.append(InstrumentControl(p.name,f'{mode}_{field}_{maximum}',at(z=.008),f,u))
        controls.append(InstrumentControl(dash.name,f'u_{field}',at(z=.008),f,u))
    parts.append(dash)
    return parts,controls


def off_sight(p) -> float:
    """Degrees between the eye's line of sight (+z of the kit's eye frame, the eye at the origin) and the point p."""
    p=np.asarray(p,dtype=float);n=float(np.linalg.norm(p))
    return math.degrees(math.acos(max(-1.,min(1.,float(p[2])/n)))) if n>1e-9 else 180.


def placed_fields(fields, cells) -> list:
    """A block's fields (most needed first) on the cells the panel can carry a dial in (`cells`: (cell, centre,
    diameter) in the grid's order): [(cell, field, centre, diameter)]. A cell the panel cannot carry gives its place to
    the next one, so the fields the block runs out of cells for are its least needed (the tail), never one in the
    middle. The attitude indicator then takes the carried cell nearest the line of sight, swapping with its field: it is
    the instrument a pilot reads most (a basic T's centre), and a real cockpit's panel lies low under the game's narrow
    view, so the dial nearest the line of sight is the one still in view. Until 2026-10-21 each field kept its own cell
    and was dropped with it: the F-16's right_flight block lost the attitude indicator (its cell off the panel's edge),
    the one dial in view, and kept a climb dial; its other attitude indicators lie 34-40 degrees under the line of
    sight, below a 45-degree view (docs/feedback-2026-10-21-attitude-indicator.md)."""
    out=[[i,field,centre,diameter] for (i,centre,diameter),field in zip(cells,fields)]
    attitude=next((k for k,c in enumerate(out) if c[1]==ci.PITCH),None)
    if attitude is not None:
        nearest=min(range(len(out)),key=lambda k:off_sight(out[k][2]))
        out[attitude][1],out[nearest][1]=out[nearest][1],out[attitude][1]
    return [tuple(c) for c in out]


def apply(kit: Kit, blocks: tuple[Block,...], transform=lambda p:np.asarray(p), ground=False,
          limits=None, ranges=None) -> Kit:
    """Replace surveyed faces on the final, scaled/deformed cab, including their live control pivots."""
    labels={i:[] for i in range(4)};controls=list(kit.controls)
    ranges=ranges or {};kit.ranges=tuple(ranges.items())

    def limited(parts):
        if limits is None:return parts
        from cockpit_kits_plugin import _clip as clip_box
        clipped=[]
        for p in parts:
            if not p.tris:continue
            pos,nrm,uv,tris,_=clip_box(np.asarray([v.pos for v in p.verts]),np.asarray([v.normal for v in p.verts]),
                                      np.asarray([v.uv for v in p.verts]),np.asarray(p.tris),*limits)
            used,inv=np.unique(tris.reshape(-1),return_inverse=True)
            if len(used):clipped.append(om.Part(p.name,p.material,[om.Vertex(tuple(pos[i]),tuple(nrm[i]),tuple(uv[i])) for i in used],
                                               [tuple(map(int,t)) for t in inv.reshape(-1,3)]))
        return clipped
    placements=[(b,transform) for b in blocks]
    # Screen metadata is already in the final cab's frame: never scale/squeeze it a second time.
    placements += [(Block(f'mfd_{i}',s.centre,s.facing,s.width,s.height,mode='aperture_'+s.kind),np.asarray)
                   for i,s in enumerate(kit.screens) if s.kind not in ('hud','sensor')]
    for index,(b,mapped) in enumerate(placements):
        r,u,f=map(np.asarray,basis(b.facing));c=np.asarray(mapped(b.centre))
        rx=np.asarray(mapped(np.asarray(b.centre)+r*b.width/2))-c
        uy=np.asarray(mapped(np.asarray(b.centre)+u*b.height/2))-c
        width,height=2*np.linalg.norm(rx),2*np.linalg.norm(uy)
        r=rx/max(np.linalg.norm(rx),1e-9);f=np.cross(r,uy);f/=max(np.linalg.norm(f),1e-9);u=np.cross(f,r)
        if f@np.asarray(b.facing)<0:f=-f;u=-u
        axes=np.stack((r,u,f));rim=min(width,height)*.055
        aperture=b.mode.startswith('aperture_')
        half=np.array((width/2+(rim if aperture else 0),height/2+(rim if aperture else 0),.045))
        if limits is not None and ((c<limits[0]).any() or (c>limits[1]).any()):continue
        source=[p for p in kit.parts if p.tris and p.material not in ('screen','metal_dark','tub','cap')
                and (not p.material.startswith(('retrofit_','instrument_')) or p.material=='retrofit_plate')]
        corners=np.concatenate([np.asarray([v.pos for v in p.verts])[p.tris] for p in source])
        # Replace the material of the exact cut-out surface instead of laying a rectangular board over its outline.
        # This retains the model's curved perimeter and any non-flat structure in the instrument panel.
        refinished=[]
        source_ids={id(p) for p in source if p.material!='retrofit_plate'}
        kit.parts=[subtract(p,c,axes,half,refinished) if id(p) in source_ids else p for p in kit.parts]
        kit.parts.extend(refinished)
        name='retrofit_'+b.name
        def at(x=0.,y=0.,z=.001):return tuple(c+r*x+u*y+f*z)
        parts=[]
        if aperture:
            # Remove raised lenses/painted source faces inside the actual glass opening. Retaining them and
            # painting them black still lets those old shapes occlude the replacement digits.
            inner=np.array((width/2,height/2,.045))
            kit.parts=[subtract(p,c,axes,inner,view=True) if p.tris and p.material not in ('screen','metal_dark','tub','cap')
                       and (not p.material.startswith(('retrofit_','instrument_')) or p.material=='retrofit_plate')
                       else p for p in kit.parts]
            parts += [frame(name+'_case','retrofit_rim',at(z=-.001),width+2*rim,height+2*rim,rim,tuple(f),.006,tuple(u)),
                      panel('screen_'+b.mode[9:],'screen',at(z=-.0005),width,height,tuple(f),tuple(u))]
            kit.materials.setdefault('screen',KitMaterial((12,18,17)))
        elif b.mode.startswith('readout_'):
            s=Screen(b.mode[8:],at(z=.005),tuple(f),width*.94,height*.92,tuple(u))
            pp,cc=ci.instrument(s,1000+index,ranges=ranges)
            parts+=pp;controls+=cc;kit.materials.update(ci.MATERIALS)
        elif b.fields:
            if b.dials and len(b.dials)!=len(b.fields):
                raise ValueError(f'{b.name}: every field needs its own surveyed dial mount')
            rows=math.ceil(len(b.fields)/b.columns);cw,ch=width/b.columns,height/rows
            cells=[]   # (cell, its dial's centre, diameter): the cells the panel's surface can carry a dial in
            for i in range(len(b.fields)):
                x=(i%b.columns-(b.columns-1)/2)*cw;y=((rows-1)/2-i//b.columns)*ch
                diameter=min(cw,ch)*(.64 if ground else .83)
                mount=np.asarray(at(x,y,0))
                if b.dials:
                    site=b.dials[i];original=np.asarray(site.centre)
                    mount=np.asarray(mapped(original))
                    diameter=np.linalg.norm(np.asarray(mapped(original+np.asarray(basis(b.facing)[0])*site.diameter))-mount)
                anchor=_surface(corners,mount,f)
                if anchor is None:continue
                # Surveyed mounts already fit their source apertures. A support
                # probe through a hollow gauge must not shrink/move its new case.
                fitted=bool(b.dials)
                if not fitted:
                    for _ in range(5):
                        points=[mount+r*math.cos(a)*diameter*.51+u*math.sin(a)*diameter*.51
                                for a in np.linspace(0,2*math.pi,12,endpoint=False)]
                        if sum(_surface(corners,p,f) is not None for p in points)>=9:
                            fitted=True;break
                        diameter*=.85
                if not fitted:continue
                front=_front(corners,mount,axes,diameter*.51)
                cells.append((i,mount+f*(front+.002),diameter))
            for i,field,anchor,diameter in placed_fields(b.fields,cells):
                pp,cc=_dial(name+f'_{i}',tuple(anchor+f*.001),tuple(f),tuple(u),diameter,field,ground,labels,ranges)
                parts+=pp;controls+=cc
        elif b.mode=='switches':
            rows=max(1,round(height/width*b.columns))
            for row in range(rows):
                for col in range(b.columns):
                    x=(col-(b.columns-1)/2)*width/b.columns*.85;y=(row-(rows-1)/2)*height/rows*.85
                    radius=min(width/b.columns,height/rows)*.16
                    anchor=_surface(corners,at(x,y,0),f)
                    if anchor is None:continue
                    parts.append(cylinder(name+'_knob','retrofit_knob',tuple(anchor+f*.002),tuple(anchor+f*.010),radius,12))
                    parts.append(panel(name+'_index','retrofit_scale',tuple(anchor+f*.0105),radius*.18,radius,tuple(f),tuple(u)))
        kit.parts.extend(limited(parts))
    for lang,bits in labels.items():
        bits=limited([p for p in bits if p.tris])
        if bits:
            p=merge(bits,f'retrofit_captions_{lang}');kit.parts.append(p)
            controls.append(InstrumentControl(p.name,f'l_{lang}_0',(0,0,0),(0,0,-1),(0,1,0)))
    names={p.name for p in kit.parts if p.tris}
    kit.parts=[p for p in kit.parts if p.tris]
    kit.controls=tuple(c for c in controls if c.part in names)
    kit.materials.update(MATERIALS)
    return kit


AIR_FIELDS=(ci.SPEED,ci.ALT,ci.PITCH,ci.CLIMB,ci.HEADING,ci.FUEL,ci.POWER,ci.HP,None)
F16=(
    Block('right_flight',(-.205,-.38,.875),(0,.18,-.984),.25,.24,3,AIR_FIELDS),
    Block('standby',(0,-.50,.85),(0,.18,-.984),.20,.18,2,(ci.SPEED,ci.ALT,ci.PITCH,ci.HEADING)),
    Block('right_aux',(-.44,-.68,.645),(.10,.737,-.668),.12,.32,1,(ci.POWER,ci.FUEL,ci.HP)),
    Block('right_upper_aux',(-.3694,-.5777,.7626),(.1024,.7261,-.6799),.07,.07,1,(ci.POWER,)),
    Block('left_switches',(.40,-.67,.63),(-.10,.64,-.76),.26,.40,4,mode='switches'),
    Block('right_switches',(-.46,-.79,.39),(0,1,0),.20,.32,3,mode='switches'),
    Block('centre_controls',(0,-.335,.80),(0,.18,-.984),.16,.20,mode='blank'),
)
C130=(
    # Original octagonal bezels: centres from their source vertices, outer width
    # .1236 m. The lower pair mounts in the blank panel beneath, not between the
    # original two holes. Preserve all four readings without an overlapping grid.
    Block('standby',(-.23145,-.5132,.856),(0,0,-1),.265,.30,2,(ci.PITCH,ci.SPEED,ci.ALT,ci.CLIMB),
          dials=(DialAt((-.16737,-.43739,.856),.118),DialAt((-.29443,-.43739,.856),.118),
                 DialAt((-.16737,-.59299,.856),.118),DialAt((-.29443,-.59299,.856),.118))),
    Block('lower_standby',(-.51,-.80,.856),(0,0,-1),.32,.16,3,(ci.HEADING,ci.FUEL,ci.POWER)),
    Block('fmc_left',(-.19,-.255,.8203),(0,0,-1),.17,.13,mode='readout_status'),
    Block('fmc_middle',(-.37,-.255,.8203),(0,0,-1),.17,.13,mode='readout_weapons'),
    Block('fmc_right',(-.55,-.255,.8203),(0,0,-1),.17,.13,mode='readout_map'),
    Block('fmc_copilot_left',(-.75,-.255,.8203),(0,0,-1),.17,.13,mode='readout_flight'),
    Block('fmc_copilot_right',(-.975,-.255,.8203),(0,0,-1),.17,.13,mode='readout_weapons'),
    Block('lower_fmc',(-.79,-.974,.407),(0,.984,-.1783),.18,.16,mode='readout_weapons'),
    Block('pedestal',(-.49,-.93,.63),(0,.984,-.179),.48,.47,6,mode='switches'),
    Block('lower_controls',(-.96,-.745,.85),(0,0,-1),.55,.18,6,mode='switches'),
    Block('pilot_radio',(.17,-.28,.85),(0,0,-1),.35,.10,mode='blank'),
    Block('copilot_radio',(-1.43,-.28,.85),(0,0,-1),.35,.10,mode='blank'),
)
HELI={
    'heli_gunship':(
        Block('right_flight',(-.30,-.55,.746),(0,-.07,-.998),.36,.28,3,AIR_FIELDS),
        Block('left_console',(.45,-.725,.60),(0,.998,-.064),.28,.75,3,mode='switches'),
        Block('right_console',(-.45,-.725,.60),(0,.998,-.064),.28,.75,3,mode='switches'),
    ),
    'heli_gunship_pilot':(
        Block('left_standby',(.12,-.445,.50),(0,.1,-.995),.095,.22,1,(ci.PITCH,ci.CLIMB)),
        Block('right_flight',(-.21,-.50,.50),(0,.1,-.995),.32,.38,3,AIR_FIELDS),
        Block('compass',(0,-.56,.489),(0,.1,-.995),.14,.14,1,(ci.HEADING,)),
        Block('lower_aux',(-.025,-.68,.486),(0,.1,-.995),.14,.07,2,(ci.FUEL,ci.POWER)),
        Block('left_panel',(.25,-.55,.495),(0,.1,-.995),.16,.19,3,mode='switches'),
        Block('left_console',(.47,-.84,.35),(0,1,0),.30,.70,3,mode='switches'),
        Block('right_console',(-.47,-.84,.35),(0,1,0),.30,.70,3,mode='switches'),
    ),
    'heli_utility':(
        Block('central_standby',(.45,-.39,.703),(0,0,-1),.27,.235,3,AIR_FIELDS),
        Block('pedestal',(.24,-.51,.545),(0,.99,-.14),.46,.62,6,mode='switches'),
        Block('pedestal_left',(.58,-.51,.545),(0,.99,-.14),.20,.67,2,mode='switches'),
    ),
}
GROUND={
    'sf-oshkosh-matv':(Block('cluster',(-.105,-.20,.50),(0,.34,-.94),.36,.20,3,(ci.SPEED,ci.FUEL,ci.HP)),),
    'sf-gaz-tigr':(Block('cluster',(.055,-.27,.606),(0,0,-1),.43,.20,4,(ci.SPEED,ci.FUEL,ci.HP,None)),
                   Block('aux',(-.43,-.278,.576),(0,0,-1),.08,.08,1,(ci.HEADING,))),
    'sf-arhan-p880':(Block('cluster',(-.15,-.2742,.6398),(.027,.479,-.878),.25,.13,2,(ci.SPEED,ci.FUEL)),),
    'sf-lightbody-85':(Block('cluster',(.015,-.275,.87),(0,.354,-.935),.34,.145,2,(ci.SPEED,ci.FUEL)),),
}
