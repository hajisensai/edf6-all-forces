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
# The weapons screen's (appended: a model built before them keeps its fields' numbers).
ARM, SHELL, MAG, LOAD, EMPTY, PICK = range(12, 18)
ROWS = 4                                     # the seat's weapons listed along the screen's foot
ROW, ROWLOAD = 18, 18 + ROWS                 # ROW + i: weapon i's rounds; ROWLOAD + i: its loading
# ARM's value: the picked weapon's picture (src/hud_cue.h CockpitArmGroup), SHELL's: its round's tag (reticle.h Shell).
GUN, CANNON, ROCKET, AAM, AGM, BOMB, DRONE, ENERGY, CHARGE = range(9)
SHELL_TAGS = ('AP', 'HE', 'BEAM', 'HEAT', 'CAN')   # SHELL 1..5
# A seven-segment digit's cell: width over height, stroke over height, pitch over width.
DIGIT_ASPECT, DIGIT_STROKE, DIGIT_PITCH = .56, .13, 1.25
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

    def poly(name: str, material: str, pts: list[tuple[float, float]], depth: float = .002) -> om.Part:
        """A convex face through `pts` (screen fractions, x right and y up as seen), its front toward the eye."""
        if sum(pts[i-1][0]*pts[i][1]-pts[i][0]*pts[i-1][1] for i in range(len(pts)))<0:pts=pts[::-1]
        at=[point(x,y,depth) for x,y in pts]
        verts=[om.Vertex(p,f,(0.,0.)) for p in at]
        return om.Part(prefix+'_'+name,material,verts,[(0,i,i+1) for i in range(1,len(at)-1)])

    def glyph_bits(text: str, x: float, y: float, unit: float, material: str, name: str,
                   depth: float = .001) -> list[om.Part]:
        """`text` in the prebaked glyphs from x, its top row's centre at y, `unit` screen heights a glyph pixel."""
        bits=[];cursor=0
        for ch in text:
            w,rows=GLYPHS[ch]
            for row,mask in enumerate(rows):
                col=0
                while col<w:
                    if not (mask>>col&1):col+=1;continue
                    end=col+1
                    while end<w and mask>>end&1:end+=1
                    bits.append(rect(name,material,x+(cursor+(col+end-1)/2)*unit*s.height/s.width,
                                     y-row*unit,(end-col)*unit*s.height/s.width,unit,depth))
                    col=end
            cursor+=w+2
        return bits

    def glyph_span(text: str) -> tuple[int, int]:
        """(width, height) of `text` in glyph pixels."""
        glyphs=[GLYPHS[ch] for ch in text]
        return sum(w+2 for w,_rows in glyphs)-2,max(len(rows) for _w,rows in glyphs)

    def label(text: str, x: float, y: float, size: float = 0.017, gate: int = 0,
              max_width: float | None = None, material: str = 'instrument_ink', right: bool = False) -> None:
        """A caption from x (right: x is its right end), its top at y, in each language (the HUD's language picks the
        one shown); gate > 0: shown only while field gate-1 has a reading. One material a gate."""
        if text in LABELS:
            for language, translated in enumerate(LABELS[text]):
                width,height=glyph_span(translated)
                available=(.46+x if right else .46-x) if max_width is None else max_width
                unit=min(max(5*size,.085)/height,available*s.width/s.height/max(1,width))
                left=x-(width-1)*unit*s.height/s.width if right else x
                captions.setdefault((language,gate),[]).extend(glyph_bits(translated,left,y,unit,material,'caption'))
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

    def digits(field: int, right: float, y: float, count: int, height: float, mode: str = 'D') -> float:
        """`count` seven-segment digits of field's reading right-aligned at `right`, `height` screen heights tall round
        y. A digit keeps a digit's proportions (DIGIT_ASPECT, DIGIT_STROKE) on a screen of any shape and whatever the
        count: the cells were the field's width over the count, so one digit across a wide screen was a flat smear
        and five were squat. Returns the left end."""
        dh=height;dw=DIGIT_ASPECT*dh*s.height/s.width;t=DIGIT_STROKE*dh;tw=t*s.height/s.width
        # Segment order: top, upper right, lower right, bottom, lower left, upper left, middle (src Segment's).
        across,down=(dw-2.2*tw,t),(tw,dh/2-1.1*t)
        segments=((0,dh/2-t/2,*across),(dw/2-tw/2,dh/4,*down),(dw/2-tw/2,-dh/4,*down),(0,-dh/2+t/2,*across),
                  (-dw/2+tw/2,-dh/4,*down),(-dw/2+tw/2,dh/4,*down),(0,0,*across))
        for place in range(count):
            cx=right-dw/2-place*dw*DIGIT_PITCH
            for seg,(dx,dy,w,h) in enumerate(segments):
                p=rect(f'digit_{field}_{place}_{seg}','instrument_ink',cx+dx,y+dy,w,h,.002)
                live(p,f'{mode}_{field}_{place}_{seg}',point(cx,y,.002))
        return right-dw-(count-1)*dw*DIGIT_PITCH

    def icon(group: int, cx: float, cy: float, box_w: float, box_h: float, material: str, name: str) -> list[om.Part]:
        """The picture of weapon group `group` (the HUD loadout strip's, src/hud.cpp StoreGlyph: some 34 x 18 units,
        the nose to the right) centred at (cx, cy), fitted into box_w x box_h screen fractions."""
        k=min(box_w*s.width/34.,box_h*s.height/18.)   # metres a unit

        def at(uu: float, vv: float) -> tuple[float, float]:
            return cx+uu*k/s.width,cy-vv*k/s.height
        out: list[om.Part] = []

        def box(u0: float, v0: float, u1: float, v1: float) -> None:
            out.append(poly(name,material,[at(u0,v0),at(u1,v0),at(u1,v1),at(u0,v1)]))

        def tri(bu: float, bv: float, tu: float, tv: float, half: float) -> None:
            du,dv=tu-bu,tv-bv;ln=math.hypot(du,dv) or 1.;pu,pv=-dv/ln*half,du/ln*half
            out.append(poly(name,material,[at(bu+pu,bv+pv),at(tu,tv),at(bu-pu,bv-pv)]))

        def seg(u0: float, v0: float, u1: float, v1: float, t: float) -> None:
            du,dv=u1-u0,v1-v0;ln=math.hypot(du,dv) or 1.;pu,pv=-dv/ln*t/2,du/ln*t/2
            out.append(poly(name,material,[at(u0+pu,v0+pv),at(u1+pu,v1+pv),at(u1-pu,v1-pv),at(u0-pu,v0-pv)]))

        def missile(length: float, radius: float, fin: float, canards: bool, mid: bool) -> None:
            tail,cone=-length/2,length/2-radius*2
            box(tail,-radius,cone,radius);tri(cone,0,length/2,0,radius)
            tri(tail+3,-radius,tail,-radius-fin,1.6);tri(tail+3,radius,tail,radius+fin,1.6)
            if canards:
                tri(cone-3,-radius,cone-4.5,-radius-fin*.5,1.);tri(cone-3,radius,cone-4.5,radius+fin*.5,1.)
            if mid:
                tri(1,-radius,-4,-radius-fin*1.2,3.5);tri(1,radius,-4,radius+fin*1.2,3.5)
        rows3=(-4.,0.,4.)
        if group==GUN:     # three rounds
            for v in rows3:
                v*=1.125;box(-10,v-1.4,2,v+1.4);tri(2,v,7,v,1.4);box(-12,v-1.6,-10.5,v+1.6)
        elif group==CANNON:   # one big shell: its case, driving band, ogive and fuze
            box(-14,-3.6,-2,3.6);box(-15.5,-4.2,-14,4.2);box(-1,-3.4,4,3.4);box(-2,-3.9,-1,3.9)
            tri(4,0,12.5,0,3.4);box(12,-.7,14,.7)
        elif group==ROCKET:   # the pod, its tubes, the rockets' tips
            seg(-14,-6,12,-6,1.6);seg(-14,6,12,6,1.6);seg(-14,-6.8,-14,6.8,1.6);seg(12,-6.8,12,6.8,1.6)
            seg(-14,-2,12,-2,.8);seg(-14,2,12,2,.8)
            for v in rows3:
                tri(12,v,16,v,1.4)
        elif group==AAM:
            missile(30,1.6,4,False,True)
        elif group==AGM:
            missile(26,2.6,3,False,True)
        elif group==BOMB:
            box(-7,-3.8,8,3.8);tri(8,0,15,0,3.8);tri(-7,0,-12,0,3.8)
            seg(-15,-5,-15,5,1.6);seg(-15,-5,-10,-3,1.4);seg(-15,5,-10,3,1.4)
        elif group==DRONE:
            seg(-14,0,14,0,2.5);tri(6,0,17,0,2.5);tri(2,0,-8,-8,3);tri(2,0,-8,8,3);seg(-12,-4,-12,4,2)
        elif group==ENERGY:
            box(-15,-5,-8,5)
            for v in rows3:
                seg(-7,v*.75,16,v*.75,2. if v==0 else 1.)
        elif group==CHARGE:
            seg(-12,-6,12,6,1.6);seg(-12,6,12,-6,1.6);seg(0,-9,0,9,1.6);seg(-16,0,16,0,1.6)
        return out

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

    def weapons() -> None:
        # Top line: the round's tag (AP / HE / ...) left, LOADING (amber) or EMPTY (red) right. Middle: the picked
        # weapon's picture left, its rounds large right. Under them the magazine's fill, the loading's amber over it.
        # The foot: one cell a weapon of the seat (when it has more than one), its rounds' fill and loading, the
        # picked one framed.
        tall=s.width<1.1*s.height   # a display taller than wide: the picture over the rounds, not beside them
        em=min(1.,s.width/(1.45*s.height))   # text and cells sized on a wide screen's height, a tall one's width
        top,fill,foot,tag=.42,-.2,-.37,.12*em
        for k,text in enumerate(SHELL_TAGS,1):
            width,height=glyph_span(text)
            unit=tag/height
            live(merge(glyph_bits(text,-.44,top,unit,'instrument_ink',f'shell_{k}'),name=prefix+f'_shell_{k}'),
                 f'e_{SHELL}_{k}',point(0,0))
        label('RELOAD',.44,top,tag/5,gate=LOAD+1,material='instrument_amber',right=True,max_width=.5)
        label('EMPTY',.44,top,tag/5,gate=EMPTY+1,material='instrument_red',right=True,max_width=.5)
        picture=(0.,.17,.8,.17) if tall else (-.24,.06,.4,.3)   # centre x, y; box width, height
        rounds=(.44,-.04,.88,.17) if tall else (.44,.06,.42,.3)   # right end, centre y; most width, height
        for group in range(CHARGE+1):
            x,y,w,h=picture
            live(merge(icon(group,x,y,w,h,'instrument_ink',f'arm_{group}'),name=prefix+f'_arm_{group}'),
                 f'e_{ARM}_{group}',point(x,y,.002))
        right,y,most,h=rounds
        digits(AMMO,right,y,4,min(h,most*s.width/s.height/(DIGIT_ASPECT*(1+3*DIGIT_PITCH))))
        live(rect('magazine_track','instrument_dim',0,fill,.88,.06*em),f'v_{MAG}',point(0,fill))
        live(rect('magazine','instrument_ink',0,fill,.88,.045*em,.002),f'B_{MAG}',point(-.44,fill,.002))
        live(rect('loading','instrument_amber',0,fill,.88,.045*em,.003),f'B_{LOAD}',point(-.44,fill,.003))
        gap=.02;cell=(.88-gap*(ROWS-1))/ROWS;half=.075*em
        line=.014*em;line_w=line*s.height/s.width
        for i in range(ROWS):
            x0=-.44+i*(cell+gap);x1=x0+cell;xc=(x0+x1)/2
            width,height=glyph_span(str(i+1))
            unit=.1*em/height
            number=merge(glyph_bits(str(i+1),x0+.015,foot+.05*em-unit/2,unit,'instrument_ink',f'row_{i}'),
                         name=prefix+f'_row_{i}_number')
            live(number,f'v_{ROW+i}',point(xc,foot))
            left=x0+.03+(width+1)*unit*s.height/s.width;length=x1-.015-left
            live(rect(f'row_{i}_track','instrument_dim',left+length/2,foot,length,.05*em),f'v_{ROW+i}',point(xc,foot))
            live(rect(f'row_{i}_fill','instrument_ink',left+length/2,foot,length,.035*em,.002),f'B_{ROW+i}',
                 point(left,foot,.002))
            live(rect(f'row_{i}_loading','instrument_amber',left+length/2,foot,length,.035*em,.003),
                 f'B_{ROWLOAD+i}',point(left,foot,.003))
            frame=[rect(f'row_{i}_frame','instrument_ink',xc,foot+half,cell,line),
                   rect(f'row_{i}_frame','instrument_ink',xc,foot-half,cell,line),
                   rect(f'row_{i}_frame','instrument_ink',x0+line_w/2,foot,line_w,2*half),
                   rect(f'row_{i}_frame','instrument_ink',x1-line_w/2,foot,line_w,2*half)]
            live(merge(frame,name=prefix+f'_row_{i}_frame'),f'e_{PICK}_{i}',point(xc,foot))

    parts.append(rect('face','instrument_black',0,0,1,1,0))
    if s.kind == 'status':
        label('STATUS',-.43,.43)
        parts.append(rect('header_rule','instrument_dim',0,.30,.86,.004))
        vehicle()
        if numbers:
            digits(HP,.37,.06,3,.18,'p')
            label('%',.395,.05)
            label('FUEL',-.43,-.24,gate=FUEL+1)
            live(rect('fuel_track','instrument_dim',0,-.37,.86,.055),f'v_{FUEL}',point(0,-.37))
            live(rect('fuel','instrument_ink',0,-.37,.86,.045,.002),f'B_{FUEL}',point(-.43,-.37,.002))
        else:
            live(rect('health','instrument_ink',0,-.33,.86,.045,.002),f'B_{HP}',point(-.43,-.33,.002))
        live(rect('warn','instrument_needle',.35,-.425,.08,.045,.003),f'B_{WARN}',point(.31,-.425,.003))
    elif s.kind == 'weapons':
        weapons()
    elif s.kind == 'flight':
        dial(SPEED,-.25,.16,'KMH',2000)
        dial(ALT,.25,.16,'ALT M',3000)
        label('HDG',-.43,-.28,.013,max_width=.34)
        digits(HEADING,.44,-.32,3,.16)
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
