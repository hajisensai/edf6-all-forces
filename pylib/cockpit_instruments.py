"""Instruments made of model geometry, including their live digits, needles and lamps.

No HUD quads cover a dashboard. Every moving part is rigidly skinned to a named bone; fp_vehicle.cpp changes its
draw pose only. The ordinary scene depth test, perspective and cockpit occlusion apply to the whole instrument.
The small block font and scales are geometry, so their clarity does not depend on an imported texture atlas.
"""
from __future__ import annotations

import math

from cockpit_shapes import basis, panel, merge
from cockpits import KitMaterial, Screen, InstrumentControl as Moving
from cockpit_label_glyphs import LABELS, GLYPHS
import obj_model as om

# Keep in step with src/cockpit_instruments.h. Negative readings mean unavailable, never zero.
HP, HULL, FUEL, SPEED, ALT, HEADING, SLOT, AMMO, WARN, CLIMB, PITCH, POWER = range(12)
FONT = {
    '%': ('101','001','010','100','101'),
    'H': ('101','101','111','101','101'), 'P': ('110','101','110','100','100'),
    'F': ('111','100','110','100','100'), 'U': ('101','101','101','101','111'),
    'E': ('111','100','110','100','111'), 'L': ('100','100','100','100','111'),
    'S': ('111','100','111','001','111'), 'D': ('110','101','101','101','110'),
    'A': ('010','101','111','101','101'), 'T': ('111','010','010','010','010'),
    'M': ('101','111','111','101','101'), 'O': ('111','101','101','101','111'),
    'W': ('101','101','111','111','101'), 'R': ('110','101','110','101','101'),
    'N': ('101','111','111','111','101'), 'K': ('101','101','110','101','101'),
    'I': ('111','010','010','010','111'), 'C': ('111','100','100','100','111'),
    'G': ('111','100','101','101','111'), 'B': ('110','101','110','101','110'),
    '0': ('111','101','101','101','111'), '1': ('010','110','010','010','111'),
    '2': ('111','001','111','100','111'), '3': ('111','001','111','001','111'),
    '4': ('101','101','111','001','001'), '5': ('111','100','111','001','111'),
    '6': ('111','100','111','101','111'), '7': ('111','001','010','010','010'),
    '8': ('111','101','111','101','111'), '9': ('111','101','111','001','111'),
}


def silhouette(triangles, view: str = 'top') -> tuple[tuple[str, ...], float]:
    """Model-derived silhouette: nose up in top view, head up for a mech's front view."""
    import numpy as np
    from PIL import Image, ImageDraw
    if view not in ('top','front'):raise ValueError('unknown vehicle thumbnail view')
    vertical=1 if view=='front' else 2
    tris = np.asarray(triangles, dtype=float)
    if not tris.size or not np.isfinite(tris).all():
        return (), 1.
    lo, hi = tris.reshape(-1,3).min(0), tris.reshape(-1,3).max(0)
    span = hi-lo
    if span[0]<1e-5 or span[vertical]<1e-5:return (), 1.
    width,height=96,144
    image=Image.new('1',(width,height));draw=ImageDraw.Draw(image)
    for tri in tris:
        points=[((hi[0]-p[0])/span[0]*(width-1),(hi[vertical]-p[vertical])/span[vertical]*(height-1)) for p in tri]
        draw.polygon(points,fill=1)
    return tuple(''.join('1' if image.getpixel((x,y)) else '0' for x in range(width)) for y in range(height)),float(span[0]/span[vertical])


def instrument(s: Screen, index: int, numbers: bool = True,
               thumbnail: tuple[tuple[str, ...], float] = ((),1.), ranges: dict[int,int] | None = None) -> tuple[list[om.Part], list[Moving]]:
    """A physical instrument at s, including an opaque backing in front of any old display face."""
    r, u, f = basis(s.facing, s.up)
    parts: list[om.Part] = []
    moving: list[Moving] = []
    prefix = f'instrument_{index}'
    captions: dict[tuple[int,int], list[om.Part]] = {}

    def point(x: float, y: float, depth: float = 0.001) -> tuple[float, float, float]:
        return tuple(s.centre[k] + r[k]*x*s.width + u[k]*y*s.height + f[k]*depth for k in range(3))

    def rect(name: str, material: str, x: float, y: float, w: float, h: float,
             depth: float = 0.001) -> om.Part:
        return panel(prefix+'_'+name, material, point(x,y,depth), w*s.width, h*s.height, f, u)

    def label(text: str, x: float, y: float, size: float = 0.017, gate: int = 0,
              max_width: float | None = None) -> None:
        if text in LABELS:
            for language, translated in enumerate(LABELS[text]):
                glyphs=[GLYPHS[ch] for ch in translated]
                width=sum(w+2 for w,_rows in glyphs)-2
                height=max(len(rows) for _w,rows in glyphs)
                available=.46-x if max_width is None else max_width
                unit=min(max(5*size,.085)*s.height/height,available*s.width/max(1,width))
                bits=[];cursor=0
                for w,rows in glyphs:
                    for row,mask in enumerate(rows):
                        col=0
                        while col<w:
                            if not (mask>>col&1):col+=1;continue
                            end=col+1
                            while end<w and mask>>end&1:end+=1
                            bits.append(rect('caption','instrument_ink',x+(cursor+(col+end-1)/2)*unit/s.width,
                                             y-row*unit/s.height,(end-col)*unit/s.width,unit/s.height))
                            col=end
                    cursor+=w+2
                captions.setdefault((language,gate),[]).extend(bits)
            return
        bits = []
        for i, ch in enumerate(text):
            for row, line in enumerate(FONT.get(ch, ('000',)*5)):
                for col, bit in enumerate(line):
                    if bit == '1':
                        bits.append(rect('label', 'instrument_ink', x+(i*4+col)*size*s.height/s.width,
                                         y-row*size, size*s.height/s.width*.82, size*.82))
        if bits:
            parts.append(merge(bits, name=prefix+'_label_'+text))

    def live(p: om.Part, control: str, c: tuple[float, float, float], up=u, extent=(1.0,1.0)) -> None:
        parts.append(p)
        moving.append(Moving(p.name, control, c, f, up, extent))

    def bar(field: int, y: float, title: str) -> None:
        label(title, -.43, y+.12)
        parts.append(rect('track','instrument_dim',0,y,.86,.055))
        p=rect(f'bar_{field}', 'instrument_ink', 0,y,.86,.045,.002)
        live(p,f'B_{field}',point(-.43,y,.002))

    def digits(field: int, y: float, count: int, title: str, x: float = .01,
               width: float = .7, mode: str = 'D') -> None:
        if title:label(title,-.43,y+.17)
        # Segment order: top, upper right, lower right, bottom, lower left, upper left, middle.
        segments=((0,.5,.65,.1),(.4,.25,.1,.4),(.4,-.25,.1,.4),(0,-.5,.65,.1),
                  (-.4,-.25,.1,.4),(-.4,.25,.1,.4),(0,0,.65,.1))
        dw, dh = width/count, .16
        for place in range(count):
            cx=x+width/2-(place+.5)*dw
            for seg,(dx,dy,w,h) in enumerate(segments):
                p=rect(f'digit_{field}_{place}_{seg}','instrument_ink',cx+dx*dw,y+dy*dh,w*dw,h*dh,.002)
                live(p,f'{mode}_{field}_{place}_{seg}',point(cx,y,.002))

    def vehicle() -> None:
        rows, aspect=thumbnail
        # A neutral outline is still meaningful before a live reading is available.
        # Tests without a model deliberately get no invented vehicle shape.
        if not rows:return
        h=min(.44*s.height,.52*s.width/max(aspect,.001));w=h*aspect
        height,width=len(rows),len(rows[0])
        fill=[];edge=[]
        for iy,row in enumerate(rows):
            # Merge runs in both masks. Only the contour lights up with the reading;
            # the quiet interior remains the same phosphor tint as the instrument face.
            outline=''.join('1' if bit=='1' and any(
                not (0<=xx<width and 0<=yy<height) or rows[yy][xx]!='1'
                for xx,yy in ((x-1,iy),(x+1,iy),(x,iy-1),(x,iy+1))) else '0'
                for x,bit in enumerate(row))
            for mask,bits,material,depth in ((row,fill,'instrument_dim',.0015),
                                              (outline,edge,'instrument_dim',.002)):
                ix=0
                while ix<width:
                    if mask[ix]!='1':ix+=1;continue
                    end=ix+1
                    while end<width and mask[end]=='1':end+=1
                    bits.append(rect('vehicle',material,-.17+((ix+end)/2/width-.5)*w/s.width,
                                     .04+(.5-(iy+.5)/height)*h/s.height,
                                     (end-ix)/width*w/s.width,h/height/s.height,depth))
                    ix=end
        parts.append(merge(fill,name=prefix+'_vehicle_fill'))
        shape=merge(edge,name=prefix+'_vehicle_outline')
        parts.append(shape)
        for level,material in enumerate(('instrument_green','instrument_amber','instrument_red')):
            verts=[om.Vertex(tuple(v.pos[k]+f[k]*.001 for k in range(3)),v.normal,v.uv) for v in shape.verts]
            p=om.Part(shape.name+f'_{level}',material,verts,list(shape.tris))
            live(p,f'c_{HP}_{level}',point(0,0,.003))

    def dial(field: int, x: float, y: float, title: str, maximum: int) -> None:
        maximum=(ranges or {}).get(field,maximum)
        radius=min(s.width*.2,s.height*.25)
        # A gauge face, ticks and numbers are actual triangles, not a blurred patch of the source cockpit atlas.
        for tick in range(21):
            a=math.radians(-135+270*tick/20)
            cx=x+math.sin(a)*radius/s.width
            cy=y+math.cos(a)*radius/s.height
            parts.append(rect('tick','instrument_ink',cx,cy,.009,.018 if tick%5 else .035))
        label(title,x-radius/s.width,y-radius/s.height-.055,.013,max_width=radius*1.8/s.width)
        for tick in (0,10,20):
            a=math.radians(-135+270*tick/20)
            txt=str(round(maximum*tick/20))
            size=min(.011,.13*s.width/((len(txt)*4-1)*s.height))
            text_width=(len(txt)*4-1)*size*s.height/s.width
            label(txt,x+math.sin(a)*radius*.65/s.width-text_width/2,
                  y+math.cos(a)*radius*.65/s.height+2*size,size)
        p=panel(prefix+f'_needle_{field}','instrument_needle',point(x,y,.003),radius*.055,radius*.85,f,u)
        # Centre of the rectangular needle lies halfway along it; the bone stays at the hub.
        p=om.Part(p.name,p.material,[om.Vertex(tuple(v.pos[k]+u[k]*radius*.425 for k in range(3)),v.normal,v.uv)
                                   for v in p.verts],p.tris)
        live(p,f'n_{field}_{maximum}' if ranges else f'n_{field}',point(x,y,.003))

    parts.append(rect('face','instrument_black',0,0,1,1,0))
    if s.kind == 'status':
        label('STATUS',-.43,.43)
        parts.append(rect('header_rule','instrument_dim',0,.30,.86,.004))
        vehicle()
        if numbers:
            digits(HP,.06,3,'',.23,.28,'p')
            label('%',.395,.05)
            label('FUEL',-.43,-.24,gate=FUEL+1)
            live(rect('fuel_track','instrument_dim',0,-.37,.86,.055),f'v_{FUEL}',point(0,-.37))
            live(rect('fuel','instrument_ink',0,-.37,.86,.045,.002),f'B_{FUEL}',point(-.43,-.37,.002))
        else:
            live(rect('health','instrument_ink',0,-.33,.86,.045,.002),f'B_{HP}',point(-.43,-.33,.002))
        live(rect('warn','instrument_needle',.35,-.425,.08,.045,.003),f'B_{WARN}',point(.31,-.425,.003))
    elif s.kind == 'weapons':
        digits(AMMO,.22,5,'AMMO')
        digits(SLOT,-.2,1,'SLOT')
    elif s.kind == 'flight':
        dial(SPEED,-.25,.16,'KMH',2000)
        dial(ALT,.25,.16,'ALT M',3000)
        label('HDG',-.43,-.28,.013,max_width=.34)
        digits(HEADING,-.32,3,'',.21,.46)
    else:
        # Contacts move in the physical monitor plane; a steering wheel or bezel can occlude them.
        dial(HEADING,0,.08,'HDG',360)
        for contact in range(6):
            live(rect(f'contact_{contact}','instrument_needle',0,.08,.025,.025,.004),
                 f'T_{contact}',point(0,.08,.004),extent=(s.width,s.height))
    for (language,gate),bits in captions.items():
        live(merge(bits,name=prefix+f'_caption_{language}_{gate}'),f'l_{language}_{gate}',point(0,0))
    return parts,moving


MATERIALS = {
    'instrument_black': KitMaterial((10,14,18)),
    'instrument_ink': KitMaterial((110,185,136),emissive=True),
    'instrument_dim': KitMaterial((27,49,35),emissive=True),
    'instrument_needle': KitMaterial((245,162,55),emissive=True),
    'instrument_green': KitMaterial((110,185,136),emissive=True),
    'instrument_amber': KitMaterial((205,151,62),emissive=True),
    'instrument_red': KitMaterial((205,77,61),emissive=True),
}
