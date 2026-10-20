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

# The fields' and round classes' numbers: pylib/cockpit_fields.py (the C++ side generated from it: src/cockpit_fields.inc).
# Negative readings mean unavailable, never zero.
from cockpit_fields import (HP, HULL, FUEL, SPEED, ALT, HEADING, SLOT, AMMO, WARN, CLIMB, PITCH, POWER,  # noqa: F401
                            ROWS, ARM, MAG, LOAD, EMPTY, PICK, ROW, ROWROUND, ROLL,
                            ROUNDS, MG, GUN, AP, HE, HEAT, CAN, BEAM, RKT, MSL, AAM, BOMB, SPC)
# The colours (one a round class, in ROUNDS' order) after a war-game's shell icons: armour piercing blue, high explosive
# yellow, shaped charge orange, missiles red / teal.
ROUND_COLOURS = ((215,225,222),(150,210,250),(80,160,255),(255,214,64),(255,135,50),(150,230,90),(190,130,255),
                 (255,110,170),(255,72,72),(70,225,210),(205,165,110),(165,170,205))
ADI_DEGREE = .0045                           # an attitude card's pitch scale: screen heights a degree
# A seven-segment digit's cell: width over height, stroke over height, pitch over width.
DIGIT_ASPECT, DIGIT_STROKE, DIGIT_PITCH = .56, .15, 1.3
READOUT_PAD = .4   # a boxed readout's margin round its digits: digit heights
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
    captions: dict[tuple[int,int,str], list[om.Part]] = {}

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
                captions.setdefault((language,gate,material),[]).extend(glyph_bits(translated,left,y,unit,material,'caption'))
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

    def digits(field: int, right: float, y: float, count: int, height: float, mode: str = 'D',
               material: str = 'instrument_ink') -> float:
        """`count` seven-segment digits of field's reading right-aligned at `right`, `height` screen heights tall round
        y. A digit keeps a digit's proportions (DIGIT_ASPECT, DIGIT_STROKE) on a screen of any shape and whatever the
        count: the cells were the field's width over the count, so one digit across a wide screen was a flat smear
        and five were squat. Each segment is a display's bevelled bar, a small even gap at every joint (a 4's and a 7's
        strokes read apart from their neighbours), and every segment also lies unlit under the lit ones (a display's
        dark segments): a lone "1" reads as a digit in its cell, not as two bars. Returns the left end."""
        dh=height;wh=DIGIT_ASPECT*dh;t=DIGIT_STROKE*dh;gap=.12*t   # screen heights
        k=s.height/s.width

        def bar(cx: float, cy: float, horizontal: bool, length: float) -> list[tuple[float, float]]:
            h,l=t/2,length/2
            pts=[(-l,0.),(-l+h,h),(l-h,h),(l,0.),(l-h,-h),(-l+h,-h)]
            return [(cx+(px if horizontal else py)*k,cy+(py if horizontal else px)) for px,py in pts]
        across=wh-t-2*gap;down=dh/2-2*gap
        xr,yt=(wh-t)/2,(dh-t)/2   # the stroke centres' right and top
        # Segment order: top, upper right, lower right, bottom, lower left, upper left, middle (src Segment's).
        segments=((0,yt,True,across),(xr,yt/2,False,yt-2*gap),(xr,-yt/2,False,yt-2*gap),(0,-yt,True,across),
                  (-xr,-yt/2,False,yt-2*gap),(-xr,yt/2,False,yt-2*gap),(0,0,True,across))
        del down
        unlit=[]
        for place in range(count):
            cx=right-wh*k/2-place*wh*k*DIGIT_PITCH
            for seg,(dx,dy,horizontal,length) in enumerate(segments):
                pts=bar(cx+dx*k,y+dy,horizontal,length)
                live(poly(f'digit_{field}_{place}_{seg}',material,pts,.002),f'{mode}_{field}_{place}_{seg}',point(cx,y,.002))
                unlit.append(poly(f'digit_{field}_unlit','mfd_unlit',pts,.0015))
        parts.append(merge(unlit,name=prefix+f'_digit_{field}_unlit'))
        return right-wh*k-(count-1)*wh*k*DIGIT_PITCH

    def readout(field: int, centre: float, y: float, count: int, height: float, title: str, mode: str = 'D') -> None:
        """A boxed digital readout centred at (centre, y), its title left of the box: the digits' box keeps a stroke's
        clear margin all round (the digits never touch its lines)."""
        k=s.height/s.width;wide=DIGIT_ASPECT*height*k*(1+(count-1)*DIGIT_PITCH)
        digits(field,centre+wide/2,y,count,height,mode)
        pad_x,pad_y=READOUT_PAD*height*k,READOUT_PAD*height
        box=(centre-wide/2-pad_x,y-height/2-pad_y,centre+wide/2+pad_x,y+height/2+pad_y)
        parts.extend(frame_lines(*box,'mfd_frame',.006))
        parts.extend(htext(f'title_{field}',title,'mfd_accent',box[0]-.025,y,0,0,height*.45,.0015,align='right'))

    # --- Geometry in screen heights about a centre (cx, cy in screen fractions): circles stay round on any screen ---
    def hpoint(cx: float, cy: float, xh: float, yh: float, depth: float) -> tuple[float, float, float]:
        return point(cx+xh*s.height/s.width,cy+yh,depth)

    def hpoly(name: str, material: str, cx: float, cy: float, pts: list[tuple[float, float]], depth: float) -> om.Part:
        return poly(name,material,[(cx+x*s.height/s.width,cy+y) for x,y in pts],depth)

    def hdisc(name: str, material: str, cx: float, cy: float, radius: float, depth: float, a0: float = 0.,
              a1: float = 360., n: int = 48) -> om.Part:
        """A disc (or the sector a0..a1 degrees clockwise from up) `radius` screen heights."""
        ring=[(radius*math.sin(math.radians(a0+(a1-a0)*i/n)),radius*math.cos(math.radians(a0+(a1-a0)*i/n)))
              for i in range(n+1)]
        if a1-a0<360:ring=[(0.,0.)]+ring
        else:ring=ring[:-1]
        return hpoly(name,material,cx,cy,ring,depth)

    def hring(name: str, material: str, cx: float, cy: float, inner: float, outer: float, depth: float,
              a0: float = 0., a1: float = 360., n: int = 48) -> om.Part:
        quads=[]
        for i in range(n):
            a,b=math.radians(a0+(a1-a0)*i/n),math.radians(a0+(a1-a0)*(i+1)/n)
            quads.append(hpoly(name,material,cx,cy,[(inner*math.sin(a),inner*math.cos(a)),(outer*math.sin(a),outer*math.cos(a)),
                                                    (outer*math.sin(b),outer*math.cos(b)),(inner*math.sin(b),inner*math.cos(b))],depth))
        return merge(quads,name=prefix+'_'+name)

    def hbar(name: str, material: str, cx: float, cy: float, x0: float, y0: float, x1: float, y1: float, t: float,
             depth: float) -> om.Part:
        """A line t screen heights thick from (x0, y0) to (x1, y1), screen heights about (cx, cy)."""
        dx,dy=x1-x0,y1-y0;ln=math.hypot(dx,dy) or 1.;px,py=-dy/ln*t/2,dx/ln*t/2
        return hpoly(name,material,cx,cy,[(x0+px,y0+py),(x1+px,y1+py),(x1-px,y1-py),(x0-px,y0-py)],depth)

    def mask(name: str, cx: float, cy: float, hole: float, half: float, depth: float) -> om.Part:
        """The face round a round window `hole` screen heights across out to a square `half` each way: what moves behind
        it (an attitude card, a compass card) is seen through the window only."""
        quads=[];n=64
        for i in range(n):
            a,b=2*math.pi*i/n,2*math.pi*(i+1)/n
            def edge(t: float) -> tuple[float, float]:
                dx,dy=math.sin(t),math.cos(t);k=half/max(abs(dx),abs(dy));return dx*k,dy*k
            quads.append(hpoly(name,'instrument_black',cx,cy,[(hole*math.sin(a),hole*math.cos(a)),edge(a),edge(b),
                                                              (hole*math.sin(b),hole*math.cos(b))],depth))
        return merge(quads,name=prefix+'_'+name)

    def htext(name: str, text: str, material: str, cx: float, cy: float, xh: float, yh: float, height: float,
              depth: float = .003, angle: float = 0., align: str = 'centre') -> list[om.Part]:
        """Language-free `text` in the prebaked glyphs, `height` screen heights tall, centred (or its left / right end)
        at (xh, yh) screen heights about (cx, cy), turned `angle` degrees clockwise (a compass card's numbers)."""
        width,high=glyph_span(text);unit=height/high
        ox={'centre':-(width-1)/2,'left':0.,'right':-(width-1)}[align]*unit
        ca,sa=math.cos(math.radians(angle)),math.sin(math.radians(angle))
        def at(px: float, py: float) -> tuple[float, float]:   # text frame (x right, y up) -> turned, placed
            return xh+px*ca+py*sa,yh-px*sa+py*ca
        bits=[];cursor=0
        for ch in text:
            w,rows=GLYPHS[ch]
            for row,mask_bits in enumerate(rows):
                col=0
                while col<w:
                    if not (mask_bits>>col&1):col+=1;continue
                    end=col+1
                    while end<w and mask_bits>>end&1:end+=1
                    x0,x1=ox+(cursor+col-.5)*unit,ox+(cursor+end-.5)*unit
                    y0,y1=(high-1)/2*unit-(row+.5)*unit,(high-1)/2*unit-(row-.5)*unit
                    bits.append(hpoly(name,material,cx,cy,[at(x0,y0),at(x1,y0),at(x1,y1),at(x0,y1)],depth))
                    col=end
            cursor+=w+2
        return bits

    def frame_lines(x0: float, y0: float, x1: float, y1: float, material: str = 'mfd_frame', t: float = .008,
                    depth: float = .0012) -> list[om.Part]:
        """A thin box from (x0, y0) to (x1, y1), screen fractions."""
        tw=t*s.height/s.width
        return [rect('frame',material,(x0+x1)/2,y1-t/2,x1-x0,t,depth),rect('frame',material,(x0+x1)/2,y0+t/2,x1-x0,t,depth),
                rect('frame',material,x0+tw/2,(y0+y1)/2,tw,y1-y0,depth),rect('frame',material,x1-tw/2,(y0+y1)/2,tw,y1-y0,depth)]

    def bezel() -> None:
        """A display's thin frame just inside its edge: one style for every glass screen."""
        parts.append(merge(frame_lines(-.48,-.47,.48,.47),name=prefix+'_bezel'))

    def icon(round_class: int, cx: float, cy: float, box_w: float, box_h: float, material: str, name: str) -> list[om.Part]:
        """The picture of round class `round_class` (ROUNDS: a war-game's shell icons, each round's own profile; some 34 x
        18 units, the nose to the right) centred at (cx, cy), fitted into box_w x box_h screen fractions."""
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
        if round_class==MG:     # three rounds
            for v in rows3:
                v*=1.125;box(-10,v-1.4,2,v+1.4);tri(2,v,7,v,1.4);box(-12,v-1.6,-10.5,v+1.6)
        elif round_class==GUN:   # a whole round: its case, driving band, ogive and fuze
            box(-14,-3.6,-2,3.6);box(-15.5,-4.2,-14,4.2);box(-1,-3.4,4,3.4);box(-2,-3.9,-1,3.9)
            tri(4,0,12.5,0,3.4);box(12,-.7,14,.7)
        elif round_class==AP:   # a fin-stabilised dart: the long rod, its sabot ring, its fins
            box(-15,-1.1,9,1.1);tri(9,0,16,0,1.1);box(-4,-2.4,0,2.4)
            tri(-11,-1.1,-15,-5.5,2.);tri(-11,1.1,-15,5.5,2.)
        elif round_class==HE:   # a blunt shell: its body, a broad driving band, the ogive and the fuze
            box(-13,-3.8,4,3.8);box(-10,-4.6,-8,4.6);tri(4,0,11.5,0,3.8);box(11,-1.4,14,1.4)
        elif round_class==HEAT:   # a shaped charge: the cone, the stand-off probe, the tail fins
            box(-12,-3.8,-1,3.8);tri(-1,0,6,0,3.8);seg(5,0,14,0,1.4);box(13.5,-1.6,15.5,1.6)
            tri(-8,-3.8,-14,-7.5,1.6);tri(-8,3.8,-14,7.5,1.6)
        elif round_class==CAN:   # a canister: its can, the balls in it
            seg(-13,-4.6,11,-4.6,1.4);seg(-13,4.6,11,4.6,1.4);seg(-13,-5.3,-13,5.3,1.4);seg(11,-5.3,11,5.3,1.4)
            for uu in (-8.,-2.5,3.):
                for vv in (-2.,2.):
                    box(uu-1.2,vv-1.2,uu+1.2,vv+1.2)
        elif round_class==BEAM:
            box(-15,-5,-8,5)
            for v in rows3:
                seg(-7,v*.75,16,v*.75,2. if v==0 else 1.)
        elif round_class==RKT:   # an unguided rocket: its motor, nose, fins and flame
            box(-11,-2,8,2);tri(8,0,15,0,2);tri(-7,-2,-11,-6,1.6);tri(-7,2,-11,6,1.6);tri(-11,0,-17,0,1.6)
        elif round_class==MSL:
            missile(26,2.6,3,False,True)
        elif round_class==AAM:
            missile(30,1.6,4,True,False)
        elif round_class==BOMB:
            box(-7,-3.8,8,3.8);tri(8,0,15,0,3.8);tri(-7,0,-12,0,3.8)
            seg(-15,-5,-15,5,1.6);seg(-15,-5,-10,-3,1.4);seg(-15,5,-10,3,1.4)
        else:   # SPC: a special store (a drone, a charge)
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

    def compass_card(cx: float, cy: float, radius: float, control: str, depth: float) -> None:
        """A compass card `radius` screen heights round (cx, cy) on one bone: a tick each 10 degrees, longer each 30, the
        cardinal points lettered and the rest numbered in tens of degrees (3 for 030), all turning with the heading under a
        fixed lubber line (h_: Pose turns the card)."""
        bits=[]
        for deg in range(0,360,10):
            a=math.radians(deg);long=deg%30==0
            r0=radius*(.84 if long else .9)
            bits.append(hbar('card',  'instrument_ink',cx,cy,r0*math.sin(a),r0*math.cos(a),radius*math.sin(a),
                             radius*math.cos(a),radius*(.035 if long else .022),depth))
        for deg in range(0,360,30):
            a=math.radians(deg)
            text={0:'N',90:'E',180:'S',270:'W'}.get(deg,str(deg//10))
            cardinal=deg%90==0
            rr=radius*.66
            bits+=htext('card',text,'mfd_accent' if cardinal else 'instrument_ink',cx,cy,rr*math.sin(a),rr*math.cos(a),
                        radius*(.2 if cardinal else .15),depth,angle=deg)
        by_material: dict[str, list[om.Part]] = {}
        for b in bits:
            by_material.setdefault(b.material,[]).append(b)
        for material,group in by_material.items():
            live(merge(group,name=prefix+f'_card_{material}_{control}'),control,point(cx,cy,depth))

    def lubber(cx: float, cy: float, radius: float, depth: float) -> None:
        """The fixed heading mark over a compass card's top."""
        parts.append(hpoly('lubber','instrument_needle',cx,cy,[(-radius*.07,radius*1.06),(radius*.07,radius*1.06),
                                                                 (0.,radius*.9)],depth))

    def round_dial(cx: float, cy: float, radius: float, field: int, maximum: int, title: str, unit: str) -> None:
        """A standard round gauge `radius` screen heights at (cx, cy): 270 degrees of scale, a tick each twentieth,
        numbers each quarter, its title over the hub and its unit under it, one needle (r_)."""
        parts.append(hring('dial_rim','mfd_frame',cx,cy,radius*.97,radius,.0012))
        for tick in range(21):
            a=math.radians(-135+270*tick/20);long=tick%5==0
            parts.append(hbar('dial_tick','instrument_ink',cx,cy,radius*(.74 if long else .82)*math.sin(a),
                              radius*(.74 if long else .82)*math.cos(a),radius*.92*math.sin(a),radius*.92*math.cos(a),
                              radius*(.05 if long else .03),.0015))
        for tick in (0,5,10,15,20):
            a=math.radians(-135+270*tick/20)
            parts[len(parts):]=htext('dial_number',str(round(maximum*tick/20)),'instrument_ink',cx,cy,radius*.56*math.sin(a),
                         radius*.56*math.cos(a),radius*.12,.0015)
        parts[len(parts):]=htext('dial_title',title,'mfd_accent',cx,cy,0,-radius*.2,radius*.15,.0015)
        parts[len(parts):]=htext('dial_unit',unit,'mfd_accent',cx,cy,0,-radius*.66,radius*.12,.0015)
        needle(cx,cy,radius*.86,radius*.07,f'r_{field}_{maximum}')
        live(merge(htext('dial_unknown','--','instrument_ink',cx,cy,0,-radius*.25,radius*.16,.0016),
                   name=prefix+f'_unknown_{field}'),f'u_{field}',point(cx,cy,.0016))

    def needle(cx: float, cy: float, length: float, width: float, control: str, material: str = 'instrument_needle',
               depth: float = .003) -> None:
        """A needle pointing up from the hub at (cx, cy) (its pose turns it), a cap over the hub."""
        p=hpoly('needle',material,cx,cy,[(-width/2,-length*.12),(width/2,-length*.12),(width*.18,length),(-width*.18,length)],
                depth)
        live(om.Part(p.name+f'_{control}',p.material,p.verts,p.tris),control,point(cx,cy,depth))
        parts.append(hdisc('hub','mfd_frame',cx,cy,width*.9,depth+.0003,n=16))

    def altimeter(cx: float, cy: float, radius: float) -> None:
        """An altimeter `radius` screen heights at (cx, cy): 0..9 round the whole dial, the long hand once round each
        1000 m, the short hand each 10000 m (w_)."""
        parts.append(hring('alt_rim','mfd_frame',cx,cy,radius*.97,radius,.0012))
        for tick in range(50):
            a=math.radians(tick*7.2);long=tick%5==0
            parts.append(hbar('alt_tick','instrument_ink',cx,cy,radius*(.76 if long else .84)*math.sin(a),
                              radius*(.76 if long else .84)*math.cos(a),radius*.92*math.sin(a),radius*.92*math.cos(a),
                              radius*(.05 if long else .025),.0015))
        for n in range(10):
            a=math.radians(n*36)
            parts[len(parts):]=htext('alt_number',str(n),'instrument_ink',cx,cy,radius*.58*math.sin(a),radius*.58*math.cos(a),
                         radius*.17,.0015)
        parts[len(parts):]=htext('alt_title','ALT','mfd_accent',cx,cy,0,radius*.24,radius*.15,.0015)
        parts[len(parts):]=htext('alt_unit','x100m','mfd_accent',cx,cy,0,-radius*.3,radius*.13,.0015)
        needle(cx,cy,radius*.55,radius*.11,f'w_{ALT}_10000')
        needle(cx,cy,radius*.86,radius*.06,f'w_{ALT}_1000',depth=.0033)

    def attitude(cx: float, cy: float, hole: float, half: float) -> None:
        """An attitude indicator seen through a round window `hole` screen heights at (cx, cy): sky and ground with the
        horizon and a pitch ladder (a rung each 10 degrees, a short one each 5) on one bone, dropped by the nose's pitch
        and turned against the bank (g_), behind the face round the window (mask, out to `half`); the aircraft's fixed
        wings and dot over it. On the ground the window shows a compass card instead (h_..._1)."""
        travel=(half-hole)/2;reach=hole+travel;per=ADI_DEGREE
        parts.append(hdisc('adi_back','instrument_black',cx,cy,hole,.0005))
        control=f'g_{PITCH}_{round(per*1e4)}_{round(travel*1e4)}'
        card=[hdisc('adi_sky','adi_sky',cx,cy,reach,.001,-90,90),hdisc('adi_ground','adi_ground',cx,cy,reach,.001,90,270)]
        lines=[hbar('adi_horizon','adi_line',cx,cy,-reach,0,reach,0,.012,.0013)]
        for deg in range(-30,31,5):
            if deg==0:continue
            half_rung=hole*(.32 if deg%10==0 else .14)
            lines.append(hbar('adi_rung','adi_line',cx,cy,-half_rung,deg*per,half_rung,deg*per,.007,.0013))
        for p in card+[merge(lines,name=prefix+'_adi_lines')]:
            live(om.Part(p.name if p.name.startswith(prefix) else prefix+'_'+p.name,p.material,p.verts,p.tris),control,
                 point(cx,cy,.001),extent=(s.height,s.height))
        compass_card(cx,cy,hole*.92,f'h_{HEADING}_1',.0016)
        parts.append(mask('adi_mask',cx,cy,hole,half,.0022))
        parts.append(hring('adi_rim','mfd_frame',cx,cy,hole,hole+.01,.0024))
        for deg in (-60,-30,-20,-10,10,20,30,60):   # the bank scale's ticks over the window
            a=math.radians(deg)
            parts.append(hbar('adi_bank','instrument_ink',cx,cy,(hole+.012)*math.sin(a),(hole+.012)*math.cos(a),
                              (hole+(.04 if deg%30==0 else .03))*math.sin(a),(hole+(.04 if deg%30==0 else .03))*math.cos(a),
                              .007,.0024))
        lubber(cx,cy,hole,.0026)
        wing=[hbar('adi_wing','instrument_needle',cx,cy,-hole*.62,0,-hole*.22,0,.016,.0026),
              hbar('adi_wing','instrument_needle',cx,cy,-hole*.22,0,-hole*.22,-.035,.016,.0026),
              hbar('adi_wing','instrument_needle',cx,cy,hole*.22,0,hole*.62,0,.016,.0026),
              hbar('adi_wing','instrument_needle',cx,cy,hole*.22,0,hole*.22,-.035,.016,.0026),
              hdisc('adi_wing','instrument_needle',cx,cy,.012,.0026,n=12)]
        live(merge(wing,name=prefix+'_adi_wings'),f'v_{PITCH}',point(cx,cy,.0026))   # flying only

    def flight() -> None:
        """A primary flight display: the attitude indicator in the middle (a compass card in its window on the ground),
        the airspeed dial left of it, the altimeter right, the heading's digits under it."""
        aspect=s.width/s.height;wide=aspect>=1.1;edge=aspect/2-.05
        bezel()
        if 1.1<=aspect<1.75:   # the attitude indicator large left, the airspeed over the altimeter right of it
            half=.38;ax=-(edge-half);hole=half*.5
            attitude(ax*s.height/s.width,.06,hole,half)
            column=(half+ax+edge)/2;dial_r=min(.19,(edge-half-ax-.03)/2)
            round_dial(column*s.height/s.width,.25,dial_r,SPEED,(ranges or {}).get(SPEED,2000),'IAS','km/h')
            altimeter(column*s.height/s.width,-.2,dial_r)
            readout(HEADING,ax*s.height/s.width+.05,-.39,3,.065,'HDG','Z')   # zero-padded: 074, not 74
            return
        if wide:
            cy=.07;half=min(.36,edge-.3);hole=half*.5
            dial_r=min(.3,(edge-half)/2-.05)
            dx=(half+edge)/2*s.height/s.width
            round_dial(-dx,cy,dial_r,SPEED,(ranges or {}).get(SPEED,2000),'IAS','km/h')
            altimeter(dx,cy,dial_r)
            attitude(0.,cy,hole,half)
            digit_y,digit_h=-.375,.08
        else:
            half=min(.24,edge);hole=half*.5
            attitude(0.,.22,hole,half)
            dial_r=min(.13,edge/2-.02)
            dx=(edge/2)*s.height/s.width
            round_dial(-dx,-.18,dial_r,SPEED,(ranges or {}).get(SPEED,2000),'IAS','km/h')
            altimeter(dx,-.18,dial_r)
            digit_y,digit_h=-.4,.06
        readout(HEADING,0.,digit_y,3,digit_h,'HDG','Z')   # zero-padded: 074, not 74

    def nav() -> None:
        """A horizontal situation display: a compass card turning with the heading under the lubber line, range rings,
        the vehicle in the middle and the threats round it (T_: their bearing off the heading and range, as the card
        shows them)."""
        bezel()
        # The card and its range ring keep a clear margin inside the bezel on any screen (and under the title).
        cy=-.035;radius=min(.37,(s.width/s.height/2-.08)/1.03)
        parts.append(hring('nav_rim','mfd_frame',0,cy,radius*1.0,radius*1.02,.0012))
        for k in (1/3,2/3):
            parts.append(hring('nav_range','mfd_frame',0,cy,radius*.84*k-.003,radius*.84*k+.003,.0012))
        compass_card(0.,cy,radius,f'h_{HEADING}_0',.0016)
        lubber(0.,cy,radius,.0026)
        ship=[hpoly('nav_ship','instrument_needle',0.,cy,[(0.,radius*.11),(radius*.06,-radius*.07),(-radius*.06,-radius*.07)],
                    .0026)]
        parts.append(merge(ship,name=prefix+'_nav_ship'))
        parts.extend(htext('nav_title','NAV','mfd_accent',-.44+.02,.42,0,0,.07,.0015,align='left'))
        reach=radius*.84*s.height/.35   # a contact's offset is a 600 m range at .35 (cockpit_screens.inc)
        for contact in range(6):
            diamond=hpoly(f'contact_{contact}','instrument_red',0.,cy,[(0.,.025),(.025,0.),(0.,-.025),(-.025,0.)],.0028)
            live(diamond,f'T_{contact}',point(0,cy,.0028),extent=(reach,reach))

    def weapons() -> None:
        """A weapons MFD. Top: the picked weapon's round tag in its class colour (AP blue, HE yellow, HEAT orange, ...)
        left, LOADING (amber) or EMPTY (red) right. Middle: the round's own picture in its colour, its rounds large
        (unlit segments under them). Under them the magazine's fill (with quarter marks), the loading taking its place.
        The foot, under a STORES rule: one cell a weapon of the seat (a window of ROWS round the picked one), each with
        its class's picture and tag in its colour and its own fill / loading, the picked one framed bright."""
        tall=s.width<1.1*s.height
        em=min(1.,s.width/(1.45*s.height))
        bezel()
        top,fill,rule,foot=.39,-.15,-.235,-.36
        tag=.11*em
        picture=(0.,.17,.8,.15) if tall else (-.24,.07,.38,.26)
        rounds=(.44,-.03,.88,.15) if tall else (.44,.07,.42,.28)
        x,y,w,h=picture
        for k,name in enumerate(ROUNDS):
            material=f'round_{name.lower()}'
            live(merge(glyph_bits(name,-.43,top,tag/glyph_span(name)[1],material,f'tag_{k}',.002),name=prefix+f'_tag_{k}'),
                 f'e_{ARM}_{k}',point(x,y,.002))
            live(merge(icon(k,x,y,w,h,material,f'arm_{k}'),name=prefix+f'_arm_{k}'),f'e_{ARM}_{k}',point(x,y,.002))
        label('RELOAD',.43,top,tag/5,gate=LOAD+1,material='instrument_amber',right=True,max_width=.5)
        label('EMPTY',.43,top,tag/5,gate=EMPTY+1,material='instrument_red',right=True,max_width=.5)
        parts.append(rect('header_rule','mfd_frame',0,top-tag-.04,.9,.006,.0012))
        right,yy,most,hh=rounds
        digits(AMMO,right,yy,4,min(hh,most*s.width/s.height/(DIGIT_ASPECT*(1+3*DIGIT_PITCH))))
        live(rect('magazine_track','mfd_unlit',0,fill,.88,.05*em),f'v_{MAG}',point(0,fill))
        live(rect('magazine','instrument_ink',0,fill,.88,.036*em,.002),f'B_{MAG}',point(-.44,fill,.002))
        live(rect('loading','instrument_amber',0,fill,.88,.036*em,.003),f'B_{LOAD}',point(-.44,fill,.003))
        marks=[rect('magazine_mark','mfd_frame',-.44+.88*q,fill,.004,.07*em,.0035) for q in (.25,.5,.75)]
        live(merge(marks,name=prefix+'_magazine_marks'),f'v_{MAG}',point(0,fill))   # with the magazine's track
        # The STORES rule: a small title and a line to the right of it.
        title=htext('stores_title','STORES','mfd_accent',0.,rule,-.44*s.width/s.height,0.,.05*em,.0015,align='left')
        width,_high=glyph_span('STORES')
        after=-.44+(width+4)*(.05*em/_high)*s.height/s.width
        # Shown with the cells (a seat of more than one weapon), as the cells' own rows are.
        live(merge(title,name=prefix+'_stores_title'),f'v_{ROW}',point(0,rule))
        live(rect('stores_rule','mfd_frame',(after+.44)/2,rule,.44-after,.006,.0012),f'v_{ROW}',point(0,rule))
        gap=.02;cell=(.88-gap*(ROWS-1))/ROWS;half=.07*em
        line=.012*em;line_w=line*s.height/s.width
        for i in range(ROWS):
            x0=-.44+i*(cell+gap);x1=x0+cell;xc=(x0+x1)/2
            outline=frame_lines(x0,foot-half,x1,foot+half,'mfd_frame',.006)
            live(merge(outline,name=prefix+f'_row_{i}_outline'),f'v_{ROW+i}',point(xc,foot))
            label_y=foot+half*.38;icon_w=min(.32*cell,.09)
            for k,name in enumerate(ROUNDS):
                material=f'round_{name.lower()}'
                span,high=glyph_span(name)   # as tall as fits the cell's width beside the picture
                unit=min(.06*em/high,(cell-.04-icon_w)*s.width/s.height/span);th=unit*high
                live(merge(icon(k,x0+.012+icon_w/2,label_y,icon_w,.075*em,material,f'row_{i}_{k}'),
                           name=prefix+f'_row_{i}_icon_{k}'),f'e_{ROWROUND+i}_{k}',point(xc,foot))
                live(merge(glyph_bits(name,x0+.024+icon_w,label_y+th/2-unit/2,unit,material,f'row_{i}_tag_{k}',.002),
                           name=prefix+f'_row_{i}_tag_{k}'),f'e_{ROWROUND+i}_{k}',point(xc,foot))
            bar_y=foot-half*.5;left=x0+.012;length=cell-.024
            live(rect(f'row_{i}_track','mfd_unlit',left+length/2,bar_y,length,.03*em),f'v_{ROW+i}',point(xc,foot))
            live(rect(f'row_{i}_fill','instrument_ink',left+length/2,bar_y,length,.022*em,.002),f'B_{ROW+i}',
                 point(left,bar_y,.002))
            frame=[rect(f'row_{i}_frame','instrument_ink',xc,foot+half,cell,line,.003),
                   rect(f'row_{i}_frame','instrument_ink',xc,foot-half,cell,line,.003),
                   rect(f'row_{i}_frame','instrument_ink',x0+line_w/2,foot,line_w,2*half,.003),
                   rect(f'row_{i}_frame','instrument_ink',x1-line_w/2,foot,line_w,2*half,.003)]
            live(merge(frame,name=prefix+f'_row_{i}_frame'),f'e_{PICK}_{i}',point(xc,foot))

    parts.append(rect('face','instrument_black',0,0,1,1,0))
    if s.kind == 'status':
        bezel()
        label('STATUS',-.43,.43,material='mfd_accent')
        parts.append(rect('header_rule','mfd_frame',0,.30,.86,.006,.0012))
        vehicle()
        if numbers:
            digits(HP,.37,.06,3,.18,'p')
            label('%',.395,.05)
            label('FUEL',-.43,-.24,gate=FUEL+1,material='mfd_accent')
            live(rect('fuel_track','mfd_unlit',0,-.37,.86,.055),f'v_{FUEL}',point(0,-.37))
            live(rect('fuel','instrument_ink',0,-.37,.86,.045,.002),f'B_{FUEL}',point(-.43,-.37,.002))
        else:
            live(rect('health','instrument_ink',0,-.33,.86,.045,.002),f'B_{HP}',point(-.43,-.33,.002))
        live(rect('warn','instrument_needle',.35,-.425,.08,.045,.003),f'B_{WARN}',point(.31,-.425,.003))
    elif s.kind == 'weapons':
        weapons()
    elif s.kind == 'flight':
        flight()
    else:
        nav()
    for (language,gate,material),bits in captions.items():   # one bone a language and gate, whatever the colour
        name=prefix+f'_caption_{language}_{gate}'+('' if material=='instrument_ink' else '_'+material)
        live(merge(bits,name=name),f'l_{language}_{gate}',point(0,0))
    return parts,moving


MATERIALS = {
    'instrument_black': KitMaterial((8,11,14)),
    'instrument_ink': KitMaterial((205,238,228),emissive=True),
    'instrument_dim': KitMaterial((30,52,50),emissive=True),
    'instrument_needle': KitMaterial((245,170,60),emissive=True),
    'instrument_green': KitMaterial((90,220,130),emissive=True),
    'instrument_amber': KitMaterial((240,165,55),emissive=True),
    'instrument_red': KitMaterial((235,80,70),emissive=True),
    'mfd_frame': KitMaterial((50,105,120),emissive=True),
    'mfd_accent': KitMaterial((90,205,235),emissive=True),
    'mfd_unlit': KitMaterial((16,26,28),emissive=True),
    'adi_sky': KitMaterial((38,100,170),emissive=True),
    'adi_ground': KitMaterial((120,78,40),emissive=True),
    'adi_line': KitMaterial((235,242,240),emissive=True),
    **{f'round_{name.lower()}': KitMaterial(colour,emissive=True) for name,colour in zip(ROUNDS,ROUND_COLOURS)},
}
