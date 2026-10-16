"""Instruments made of model geometry, including their live digits, needles and lamps.

No HUD quads cover a dashboard. Every moving part is rigidly skinned to a named bone; fp_vehicle.cpp changes its
draw pose only. The ordinary scene depth test, perspective and cockpit occlusion apply to the whole instrument.
The small block font and scales are geometry, so their clarity does not depend on an imported texture atlas.
"""
from __future__ import annotations

import math
from dataclasses import dataclass

from cockpit_shapes import basis, panel, merge
from cockpits import KitMaterial, Screen
from cockpit_label_glyphs import LABELS, GLYPHS
import obj_model as om

# Keep in step with src/cockpit_instruments.h. Negative readings mean unavailable, never zero.
HP, HULL, FUEL, SPEED, ALT, HEADING, SLOT, AMMO, WARN = range(9)
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


@dataclass(frozen=True)
class Moving:
    part: str
    control: str  # d_field_place_segment, b_field, n_field (digit, bar, needle)
    centre: tuple[float, float, float]
    facing: tuple[float, float, float]
    up: tuple[float, float, float]


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
    image=Image.new('1',(48,72));draw=ImageDraw.Draw(image)
    for tri in tris:
        points=[((hi[0]-p[0])/span[0]*47,(hi[vertical]-p[vertical])/span[vertical]*71) for p in tri]
        draw.polygon(points,fill=1)
    return tuple(''.join('1' if image.getpixel((x,y)) else '0' for x in range(48)) for y in range(72)),float(span[0]/span[vertical])


def instrument(s: Screen, index: int, numbers: bool = True,
               thumbnail: tuple[tuple[str, ...], float] = ((),1.)) -> tuple[list[om.Part], list[Moving]]:
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

    def label(text: str, x: float, y: float, size: float = 0.017, gate: int = 0) -> None:
        if text in LABELS:
            for language, translated in enumerate(LABELS[text]):
                glyphs=[GLYPHS[ch] for ch in translated]
                width=sum(w+2 for w,_rows in glyphs)-2
                height=max(len(rows) for _w,rows in glyphs)
                unit=min(max(5*size,.085)*s.height/height,(.46-x)*s.width/max(1,width))
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

    def live(p: om.Part, control: str, c: tuple[float, float, float], up=u) -> None:
        parts.append(p)
        moving.append(Moving(p.name, control, c, f, up))

    def bar(field: int, y: float, title: str) -> None:
        label(title, -.43, y+.12)
        parts.append(rect('track','instrument_dim',0,y,.86,.055))
        p=rect(f'bar_{field}', 'instrument_ink', 0,y,.86,.045,.002)
        live(p,f'b_{field}',point(-.43,y,.002))

    def digits(field: int, y: float, count: int, title: str, x: float = .01,
               width: float = .7, mode: str = 'd') -> None:
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
        h=min(.56*s.height,.58*s.width/max(aspect,.001));w=h*aspect
        height,width=len(rows),len(rows[0])
        fill=[];edge=[]
        for iy,row in enumerate(rows):
            ix=0
            while ix<width:
                if row[ix]!='1':ix+=1;continue
                end=ix+1
                while end<width and row[end]=='1':end+=1
                fill.append(rect('vehicle','instrument_ink',-.14+((ix+end)/2/width-.5)*w/s.width,
                                 .07+(.5-(iy+.5)/height)*h/s.height,(end-ix)/width*w/s.width,h/height/s.height,.002))
                for x in range(ix,end):
                    if any(not (0<=xx<width and 0<=yy<height) or rows[yy][xx]!='1'
                           for xx,yy in ((x-1,iy),(x+1,iy),(x,iy-1),(x,iy+1))):
                        edge.append(rect('outline','instrument_dim',-.14+((x+.5)/width-.5)*w/s.width,
                                         .07+(.5-(iy+.5)/height)*h/s.height,w/width/s.width,h/height/s.height,.003))
                ix=end
        parts.append(merge(edge,name=prefix+'_vehicle_outline'))
        shape=merge(fill,name=prefix+'_vehicle')
        for level,material in enumerate(('instrument_green','instrument_amber','instrument_red')):
            p=om.Part(shape.name+f'_{level}',material,list(shape.verts),list(shape.tris))
            live(p,f'c_{HP}_{level}',point(0,0,.002))

    def dial(field: int, x: float, y: float, title: str, maximum: int) -> None:
        radius=min(s.width*.2,s.height*.25)
        # A gauge face, ticks and numbers are actual triangles, not a blurred patch of the source cockpit atlas.
        for tick in range(21):
            a=math.radians(-135+270*tick/20)
            cx=x+math.sin(a)*radius/s.width
            cy=y+math.cos(a)*radius/s.height
            parts.append(rect('tick','instrument_ink',cx,cy,.009,.018 if tick%5 else .035))
        label(title,x-radius/s.width,y-radius/s.height-.055,.013)
        for tick in (0,10,20):
            a=math.radians(-135+270*tick/20)
            txt=str(round(maximum*tick/20))
            label(txt,x+math.sin(a)*radius*.65/s.width-.025*len(txt),
                  y+math.cos(a)*radius*.65/s.height,.011)
        p=panel(prefix+f'_needle_{field}','instrument_needle',point(x,y,.003),radius*.055,radius*.85,f,u)
        # Centre of the rectangular needle lies halfway along it; the bone stays at the hub.
        p=om.Part(p.name,p.material,[om.Vertex(tuple(v.pos[k]+u[k]*radius*.425 for k in range(3)),v.normal,v.uv)
                                   for v in p.verts],p.tris)
        live(p,f'n_{field}',point(x,y,.003))

    parts.append(rect('face','instrument_black',0,0,1,1,0))
    if s.kind == 'status':
        label('STATUS',-.43,.43)
        vehicle()
        if numbers:
            digits(HP,.03,3,'',.27,.30,'p')
            label('%',.34,-.13)
            label('FUEL',-.43,-.24,gate=FUEL+1)
            live(rect('fuel_track','instrument_dim',0,-.37,.86,.055),f'v_{FUEL}',point(0,-.37))
            live(rect('fuel','instrument_ink',0,-.37,.86,.045,.002),f'b_{FUEL}',point(-.43,-.37,.002))
        else:
            live(rect('health','instrument_ink',0,-.33,.86,.045,.002),f'b_{HP}',point(-.43,-.33,.002))
        live(rect('warn','instrument_needle',.35,-.425,.08,.045,.003),f'b_{WARN}',point(.31,-.425,.003))
    elif s.kind == 'weapons':
        digits(AMMO,.22,5,'AMMO')
        digits(SLOT,-.2,1,'SLOT')
    elif s.kind == 'flight':
        dial(SPEED,-.25,.16,'KMH',2000)
        dial(ALT,.25,.16,'ALT M',3000)
        digits(HEADING,-.27,3,'HDG')
    else:
        # Contacts move in the physical monitor plane; a steering wheel or bezel can occlude them.
        dial(HEADING,0,.08,'HDG',360)
        for contact in range(6):
            live(rect(f'contact_{contact}','instrument_needle',0,.08,.025,.025,.004),
                 f't_{contact}',point(0,.08,.004))
    for (language,gate),bits in captions.items():
        live(merge(bits,name=prefix+f'_caption_{language}_{gate}'),f'l_{language}_{gate}',point(0,0))
    return parts,moving


MATERIALS = {
    'instrument_black': KitMaterial((10,14,18)),
    'instrument_ink': KitMaterial((170,235,197),emissive=True),
    'instrument_dim': KitMaterial((30,55,44)),
    'instrument_needle': KitMaterial((245,162,55),emissive=True),
    'instrument_green': KitMaterial((38,115,67),emissive=True),
    'instrument_amber': KitMaterial((155,96,22),emissive=True),
    'instrument_red': KitMaterial((155,32,24),emissive=True),
}
