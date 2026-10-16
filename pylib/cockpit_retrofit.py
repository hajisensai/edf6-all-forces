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
class Block:
    name: str
    centre: tuple[float,float,float]
    facing: tuple[float,float,float]
    width: float
    height: float
    columns: int = 1
    fields: tuple[int | None,...] = ()
    mode: str = 'dials'


MATERIALS = {
    'retrofit_plate': KitMaterial((43,48,46)),
    'retrofit_rim': KitMaterial((83,91,85)),
    'retrofit_face': KitMaterial((12,18,17)),
    'retrofit_scale': KitMaterial((148,171,150),emissive=True),
    'retrofit_needle': KitMaterial((210,161,82),emissive=True),
    'retrofit_knob': KitMaterial((28,33,31)),
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


def _dial(name,centre,facing,up,diameter,field,ground,labels,ranges):
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
            labels[language].append(_text(name+'_title',text,at(0,-radius*.46,.006),diameter*.64,
                                          diameter*.105,'retrofit_scale',f,u))
    else:
        parts.append(_text(name+'_unit',title,at(0,-radius*.46,.006),diameter*.5,diameter*.105,'retrofit_scale',f,u))
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
            rows=math.ceil(len(b.fields)/b.columns);cw,ch=width/b.columns,height/rows
            for i,field in enumerate(b.fields):
                x=(i%b.columns-(b.columns-1)/2)*cw;y=((rows-1)/2-i//b.columns)*ch
                diameter=min(cw,ch)*(.64 if ground else .83)
                anchor=_surface(corners,at(x,y,0),f)
                if anchor is None:continue
                fitted=False
                for _ in range(5):
                    points=[at(x+math.cos(a)*diameter*.51,y+math.sin(a)*diameter*.51,0) for a in np.linspace(0,2*math.pi,12,endpoint=False)]
                    if sum(_surface(corners,p,f) is not None for p in points)>=9:
                        fitted=True;break
                    diameter*=.85
                if not fitted:continue
                front=_front(corners,np.asarray(at(x,y,0)),axes,diameter*.51)
                anchor=np.asarray(at(x,y,0))+f*(front+.002)
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
    Block('standby',(-.24,-.46,.856),(0,0,-1),.26,.22,2,(ci.PITCH,ci.SPEED,ci.ALT,ci.CLIMB)),
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
