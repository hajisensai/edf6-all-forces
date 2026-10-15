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
import obj_model as om

# Keep in step with src/cockpit_instruments.h. Negative readings mean unavailable, never zero.
HP, HULL, FUEL, SPEED, ALT, HEADING, SLOT, AMMO, WARN = range(9)
FONT = {
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


def instrument(s: Screen, index: int, numbers: bool = True) -> tuple[list[om.Part], list[Moving]]:
    """A physical instrument at s, including an opaque backing in front of any old display face."""
    r, u, f = basis(s.facing, s.up)
    parts: list[om.Part] = []
    moving: list[Moving] = []
    prefix = f'instrument_{index}'

    def point(x: float, y: float, depth: float = 0.001) -> tuple[float, float, float]:
        return tuple(s.centre[k] + r[k]*x*s.width + u[k]*y*s.height + f[k]*depth for k in range(3))

    def rect(name: str, material: str, x: float, y: float, w: float, h: float,
             depth: float = 0.001) -> om.Part:
        return panel(prefix+'_'+name, material, point(x,y,depth), w*s.width, h*s.height, f, u)

    def label(text: str, x: float, y: float, size: float = 0.017) -> None:
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
        label(title, -.43, y+.1)
        parts.append(rect('track','instrument_dim',0,y,.86,.055))
        p=rect(f'bar_{field}', 'instrument_ink', 0,y,.86,.045,.002)
        live(p,f'b_{field}',point(-.43,y,.002))

    def digits(field: int, y: float, count: int, title: str) -> None:
        label(title,-.43,y+.13)
        # Segment order: top, upper right, lower right, bottom, lower left, upper left, middle.
        segments=((0,.5,.65,.1),(.4,.25,.1,.4),(.4,-.25,.1,.4),(0,-.5,.65,.1),
                  (-.4,-.25,.1,.4),(-.4,.25,.1,.4),(0,0,.65,.1))
        dw, dh = .7/count, .16
        for place in range(count):
            x=.36-(place+.5)*dw
            for seg,(dx,dy,w,h) in enumerate(segments):
                p=rect(f'digit_{field}_{place}_{seg}','instrument_ink',x+dx*dw,y+dy*dh,w*dw,h*dh,.002)
                live(p,f'd_{field}_{place}_{seg}',point(x,y,.002))

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
        if numbers: digits(HP,.25,5,'HP')
        else: bar(HP,.25,'HP')
        bar(HULL,-.05,'HULL')
        bar(FUEL,-.32,'FUEL')
        label('WARN',.22,-.31,.012)
        live(rect('warn','instrument_needle',.35,-.4,.08,.045,.003),f'b_{WARN}',point(.31,-.4,.003))
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
    return parts,moving


MATERIALS = {
    'instrument_black': KitMaterial((10,14,18)),
    'instrument_ink': KitMaterial((170,235,197),emissive=True),
    'instrument_dim': KitMaterial((30,55,44)),
    'instrument_needle': KitMaterial((245,162,55),emissive=True),
}
