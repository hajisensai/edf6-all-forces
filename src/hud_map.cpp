// The map view's marks (split from hud.cpp; the HUD's overview is hud.cpp's head). Owned by the map command
// subsystem (map.cpp / mapcmd.cpp; README 地图): the grid, compass and scale, the pins, the squad panel, the command
// card's buttons (MapButtons), MapCommands, the legend, the support bar and its panels, the damage statistics' page
// (hud_stats.inc), the marks of the command hint, the question marks and the support warnings, the order banners.
#include "hud_internal.h"

namespace crew {
namespace hudi {

// The first-time hint over a squad the map can command (test site #45: 「玩家第一次靠近可指挥的 NPC，或看到原版招募图标的位置
//时，在图标位置显示「按 M 键进行指挥」」; npcai.cpp NpcCommandHintFrame): a small chevron over its head, the line above it.
void CommandHintMark(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const CommandHint& h,
                     Line* lines,int* at) noexcept {
    float x,y,depth;
    if(!Project(vp,h.at,width,height,&x,&y,&depth) || x<0.0f || x>width || y<0.0f || y>height)return;
    Seg(drawer,ctx,x-9.0f*s,y-6.0f*s,x,y+3.0f*s,2.0f*s,kCyan);
    Seg(drawer,ctx,x,y+3.0f*s,x+9.0f*s,y-6.0f*s,2.0f*s,kCyan);
    wchar_t key[32];KeyName(h.key,key,32);
    const float tx=vec::Clamp(x,160.0f*s,width-160.0f*s),ty=vec::Clamp(y-22.0f*s,30.0f*s,height-30.0f*s);
    float on[3];ScreenShifted(vp,width,height,h.at,tx-x,ty-y,on);   // kept on the screen as the 2D line was (clamped in)
    WorldLabel(text,lines,at,vp,width,height,on,WorldKey(kWorldCommandHint,h.of),0.0f,0.0f,1,kLineScale,kCyan,Tr(Tx::cmdHintPressKey),key);
}

// --- The map view's marks (map.cpp; README 地图, docs/camera-re.md §8): drawn over the real world the map's camera
// shows, in place of every other HUD element while it is open: a ground grid (its lines a round step apart, through the
// player, labelled with their distance from the player and the side: north is the world's +z, east its -x, the game's
// level right of +z), the north arrow, a scale bar, the player, the squad, friendly vehicles, aircraft and carriers, the
// enemies the lock-on registry knows, the objective markers, a legend and the keys. ---
alignas(16) const float kMapGrid[4]={0.85f,0.95f,1.0f,0.28f};
alignas(16) const float kMapAxis[4]={0.85f,0.95f,1.0f,0.6f};
alignas(16) const float kMapSquad[4]={0.35f,1.0f,0.45f,1.0f};
alignas(16) const float kMapAlly[4]={0.45f,0.85f,0.5f,0.8f};
alignas(16) const float kMapEnemy[4]={1.0f,0.22f,0.18f,1.0f};
alignas(16) const float kMapMarker[4]={1.0f,0.85f,0.2f,1.0f};
constexpr int kMapLabels=12;             // at most this many labels an axis (every other line past that)
constexpr float kMapLabelGap=34.0f;      // px (at 1080 lines) between two labels of an axis
constexpr float kMapFarFade=0.45f;       // a pin three times the focus's distance away (and on) drawn this opaque
constexpr float kMapLockNear=40.0f;      // m: a lock point this near an enemy's pin brackets that pin
constexpr float kMapEmptyFade=0.55f;     // nobody in it (kMapEmpty): its pin this much of a crewed one's
constexpr int kMapLegendRows=15;
constexpr float kMapGuard=1.25f;         // a grid line is cut to this much of the screen's half size round it
constexpr float kMapNearW=1.0f;          // ...and to this clip w (m in front of the eye)

// The world segment a..b, cut in clip space to the part in front of the eye and on the screen (with a guard band:
// Liang-Barsky on the planes w >= kMapNearW, |x|, |y| <= kMapGuard w), drawn as one line: a straight line stays
// straight in a perspective view, and a line whose ends are both off the screen may still cross it.
void MapLine(void* drawer,void* ctx,const float* vp,float width,float height,const float* a,const float* b,float t,const float* rgba) noexcept {
    float ca[4],cb[4];
    for(int k=0;k<4;++k) {
        ca[k]=a[0]*vp[k]+a[1]*vp[4+k]+a[2]*vp[8+k]+vp[12+k];
        cb[k]=b[0]*vp[k]+b[1]*vp[4+k]+b[2]*vp[8+k]+vp[12+k];
    }
    // Each plane as d(c) >= 0, linear in the clip coordinates.
    const float da[5]={ca[3]-kMapNearW,kMapGuard*ca[3]+ca[0],kMapGuard*ca[3]-ca[0],kMapGuard*ca[3]+ca[1],kMapGuard*ca[3]-ca[1]};
    const float db[5]={cb[3]-kMapNearW,kMapGuard*cb[3]+cb[0],kMapGuard*cb[3]-cb[0],kMapGuard*cb[3]+cb[1],kMapGuard*cb[3]-cb[1]};
    float t0=0.0f,t1=1.0f;
    for(int i=0;i<5;++i) {
        if(da[i]<0.0f && db[i]<0.0f)return;
        if(da[i]<0.0f)t0=std::fmax(t0,da[i]/(da[i]-db[i]));
        else if(db[i]<0.0f)t1=std::fmin(t1,da[i]/(da[i]-db[i]));
    }
    if(!(t0<t1))return;
    float p[2][2];
    for(int e=0;e<2;++e) {
        const float f=e ? t1 : t0;
        float c[4];
        for(int k=0;k<4;++k)c[k]=ca[k]+(cb[k]-ca[k])*f;
        p[e][0]=width*0.5f*(c[0]/c[3])+width*0.5f;p[e][1]=height*0.5f-height*0.5f*(c[1]/c[3]);
    }
    Seg(drawer,ctx,p[0][0],p[0][1],p[1][0],p[1][1],t,rgba);
}

// A distance on the map: "850 m" / "1.5 km".
void MapDistance(wchar_t* out,std::size_t size,float m) noexcept {
    if(m<1000.0f)_snwprintf_s(out,size,_TRUNCATE,L"%.0f m",m);
    else _snwprintf_s(out,size,_TRUNCATE,L"%.1f km",m*0.001f);
}

// Whether a grid label at (x, y) keeps clear of the bands (title, keys), the legend and the scale bar.
bool MapLabelFree(float x,float y,float width,float height,float s) noexcept {
    if(y<60.0f*s || y>height-90.0f*s || x>width-140.0f*s)return false;   // the bands (and MapCommands' over the keys)
    return !(x<420.0f*s && y>height-140.0f*s);                                             // the scale bar (MapScale)
}

// The grid, its axes through the player, its labels (where each line meets the other axis through the focus; one at
// least kMapLabelGap px from the last one drawn of its axis: they crowd toward the horizon).
void MapGrid(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const MapReadout& m,Line* lines,int* at) noexcept {
    const float step=mapcam::GridStep(m.height);
    const int n=mapcam::GridLines(m.height,m.pitch,step);
    const float y=m.focus[1],reach=static_cast<float>(n)*step;
    for(int axis=0;axis<2;++axis) {   // 0: the lines of constant x (east / west of the player), 1: of constant z
        const int c=axis*2,o=2-c;
        const int k0=static_cast<int>(std::floor((m.focus[c]-reach-m.me[c])/step)),k1=static_cast<int>(std::ceil((m.focus[c]+reach-m.me[c])/step));
        const int every=(k1-k0+1)>kMapLabels*2 ? 2 : 1;
        float lastX=-1e9f,lastY=-1e9f;
        for(int k=k0;k<=k1;++k) {
            float a[3],b[3];
            a[1]=b[1]=y;a[c]=b[c]=m.me[c]+static_cast<float>(k)*step;
            a[o]=m.focus[o]-reach;b[o]=m.focus[o]+reach;
            MapLine(drawer,ctx,vp,width,height,a,b,(k==0 ? 2.0f : 1.0f)*s,k==0 ? kMapAxis : kMapGrid);
            if(k==0 || k%every)continue;
            float p[3];p[1]=y;p[c]=a[c];p[o]=m.focus[o];
            float x,yy,depth;
            if(!Project(vp,p,width,height,&x,&yy,&depth) || !MapLabelFree(x,yy,width,height,s))continue;
            if(std::fabs(x-lastX)<kMapLabelGap*s && std::fabs(yy-lastY)<kMapLabelGap*s)continue;
            lastX=x;lastY=yy;
            // +x is west of +z (north), -x east; +z north, -z south.
            const wchar_t* side=Tr(axis==0 ? (k>0 ? Tx::compassW : Tx::compassE) : (k>0 ? Tx::compassN : Tx::compassS));
            wchar_t d[24];MapDistance(d,_countof(d),std::fabs(static_cast<float>(k))*step);
            WorldLabel(text,lines,at,vp,width,height,p,kGroundKey,4.0f*s,-10.0f*s,0,kLineScale*0.7f,kMapAxis,L"%ls %ls",side,d);
        }
    }
}

// The north arrow (top right): north's way on the screen from the focus.
void MapCompass(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const MapReadout& m,Line* lines,int* at) noexcept {
    const float north[3]={m.focus[0],m.focus[1],m.focus[2]+mapcam::GridStep(m.height)};
    float x0,y0,x1,y1,depth;
    if(!Project(vp,m.focus,width,height,&x0,&y0,&depth) || !Project(vp,north,width,height,&x1,&y1,&depth))return;
    float dx=x1-x0,dy=y1-y0;
    const float len=std::sqrt(dx*dx+dy*dy);
    if(!(len>0.5f))return;
    dx/=len;dy/=len;
    const float r=34.0f*s,cx=width-70.0f*s,cy=110.0f*s;
    Arc(drawer,ctx,cx,cy,r,0.0f,kTurn,2.0f*s,32,kMapAxis);
    Tri(drawer,ctx,cx,cy,cx+dx*r,cy+dy*r,8.0f*s,kMapEnemy);     // north red
    Tri(drawer,ctx,cx,cy,cx-dx*r,cy-dy*r,8.0f*s,kWhite);        // south white
    Label(text,lines,at,cx+dx*(r+14.0f*s),cy+dy*(r+14.0f*s),1,kLineScale*0.8f,kWhite,L"%ls",Tr(Tx::compassN));
}

// The scale bar (bottom left): one grid step across the screen at the focus.
void MapScale(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const MapReadout& m,Line* lines,int* at) noexcept {
    const float step=mapcam::GridStep(m.height);
    float r[3];mapcam::Right(m.yaw,r);
    const float b[3]={m.focus[0]+r[0]*step,m.focus[1],m.focus[2]+r[2]*step};
    float x0,y0,x1,y1,depth;
    if(!Project(vp,m.focus,width,height,&x0,&y0,&depth) || !Project(vp,b,width,height,&x1,&y1,&depth))return;
    const float len=std::sqrt((x1-x0)*(x1-x0)+(y1-y0)*(y1-y0));
    if(!(len>4.0f) || len>width*0.5f)return;
    const float drawn=std::fmin(len,width*0.40f-20.0f*s);
    const float x=width*0.55f,y=184.0f*s;
    Rect(drawer,ctx,x,y-2.0f*s,x+drawn,y+2.0f*s,kWhite);
    Rect(drawer,ctx,x-1.0f*s,y-8.0f*s,x+1.0f*s,y+8.0f*s,kWhite);
    Rect(drawer,ctx,x+drawn-1.0f*s,y-8.0f*s,x+drawn+1.0f*s,y+8.0f*s,kWhite);
    wchar_t d[24];MapDistance(d,_countof(d),step*drawn/len);
    Label(text,lines,at,x+drawn*0.5f,y-18.0f*s,1,kLineScale*0.75f,kWhite,L"%ls",d);
}

// A hollow square / diamond of half size `h` round (x, y).
void MapBox(void* drawer,void* ctx,float x,float y,float h,float t,const float* rgba) noexcept {
    Seg(drawer,ctx,x-h,y-h,x+h,y-h,t,rgba);Seg(drawer,ctx,x+h,y-h,x+h,y+h,t,rgba);
    Seg(drawer,ctx,x+h,y+h,x-h,y+h,t,rgba);Seg(drawer,ctx,x-h,y+h,x-h,y-h,t,rgba);
}
void MapDiamond(void* drawer,void* ctx,float x,float y,float h,float t,const float* rgba) noexcept {
    Seg(drawer,ctx,x,y-h,x+h,y,t,rgba);Seg(drawer,ctx,x+h,y,x,y+h,t,rgba);
    Seg(drawer,ctx,x,y+h,x-h,y,t,rgba);Seg(drawer,ctx,x-h,y,x,y-h,t,rgba);
}
// Corner brackets round (x, y), half size `h` (a lock, the nearest enemy).
void MapBrackets(void* drawer,void* ctx,float x,float y,float h,float t,const float* rgba) noexcept {
    const float k=h*0.4f;
    for(int sx=-1;sx<=1;sx+=2)for(int sy=-1;sy<=1;sy+=2) {
        const float cx=x+static_cast<float>(sx)*h,cy=y+static_cast<float>(sy)*h;
        Seg(drawer,ctx,cx,cy,cx-static_cast<float>(sx)*k,cy,t,rgba);Seg(drawer,ctx,cx,cy,cx,cy-static_cast<float>(sy)*k,t,rgba);
    }
}

// The pins' colours by side, and the icon's shape by kind (the user, 2026-10-06: "3D 显示各个友方微缩模型或者标记，
// 敌方也显示"): a constant size on the screen, at the top of the pin's stem (or, an aircraft, at the unit itself).
const float* MapColour(MapKind kind) noexcept {
    switch(kind) {
    case MapKind::squad: case MapKind::vehicle: return kMapSquad;
    case MapKind::ally: return kMapAlly;
    case MapKind::air: case MapKind::carrier: return kCyan;
    case MapKind::marker: return kMapMarker;
    default: return kMapEnemy;
    }
}
// `rgba` with its alpha times `fade` (into `out`, 16-aligned as the quad reads it).
const float* MapFade(const float* rgba,float fade,float* out) noexcept {
    out[0]=rgba[0];out[1]=rgba[1];out[2]=rgba[2];out[3]=rgba[3]*fade;
    return out;
}

// A friendly aircraft (kind air; the user, 2026-10-06: aircraft as plainly on the map as the vehicles, an icon of
// their own): a fixed-wing one a plane seen from above, its nose along its heading on the screen (`dx`, `dy`; none: up);
// a helicopter (kMapRotor) its rotor's ring round a short fuselage and tail boom.
void MapAircraft(void* drawer,void* ctx,float x,float y,float s,bool rotor,float dx,float dy,const float* rgba) noexcept {
    if(dx==0.0f && dy==0.0f)dy=-1.0f;
    const float rx=-dy,ry=dx,t=2.0f*s;
    auto seg=[&](float f0,float r0,float f1,float r1,float w){
        Seg(drawer,ctx,x+(dx*f0+rx*r0)*s,y+(dy*f0+ry*r0)*s,x+(dx*f1+rx*r1)*s,y+(dy*f1+ry*r1)*s,w,rgba);
    };
    if(rotor) {
        Arc(drawer,ctx,x,y,9.0f*s,0.0f,kTurn,1.5f*s,18,rgba);
        seg(5.0f,0.0f,-13.0f,0.0f,3.0f*s);seg(-12.0f,-3.0f,-12.0f,3.0f,t);
        return;
    }
    seg(12.0f,0.0f,-10.0f,0.0f,3.0f*s);                        // the fuselage
    seg(3.0f,0.0f,-4.0f,11.0f,t);seg(3.0f,0.0f,-4.0f,-11.0f,t);   // the swept wings
    seg(-4.0f,11.0f,-1.0f,0.0f,t);seg(-4.0f,-11.0f,-1.0f,0.0f,t);
    seg(-7.0f,0.0f,-11.0f,5.0f,t);seg(-7.0f,0.0f,-11.0f,-5.0f,t);   // the tail
}

// One icon at (x, y): `dx, dy` its heading on the screen (0, 0: none), `hp` its HP bar (<0: none). Nobody in it
// (kMapEmpty: a vehicle or aircraft of team 5) at kMapEmptyFade of it.
void MapIcon(void* drawer,void* ctx,float x,float y,float s,MapKind kind,std::uint8_t flags,float dx,float dy,float hp,float fade) noexcept {
    alignas(16) float c[4],b[4];
    if(flags&kMapEmpty)fade*=kMapEmptyFade;
    const float* rgba=MapFade(MapColour(kind),fade,c);
    const float t=2.0f*s;
    float tick=0.0f;   // the heading tick's start, from the centre
    switch(kind) {
    case MapKind::squad: Rect(drawer,ctx,x-4.5f*s,y-4.5f*s,x+4.5f*s,y+4.5f*s,rgba);break;
    case MapKind::ally: Rect(drawer,ctx,x-3.5f*s,y-3.5f*s,x+3.5f*s,y+3.5f*s,rgba);break;
    case MapKind::vehicle: MapBox(drawer,ctx,x,y,8.0f*s,t,rgba);Rect(drawer,ctx,x-3.0f*s,y-3.0f*s,x+3.0f*s,y+3.0f*s,rgba);tick=8.0f*s;break;
    case MapKind::air: MapAircraft(drawer,ctx,x,y,s,(flags&kMapRotor)!=0,dx,dy,rgba);tick=flags&kMapRotor ? 9.0f*s : 0.0f;break;
    case MapKind::carrier: MapBox(drawer,ctx,x,y,13.0f*s,t,rgba);MapBox(drawer,ctx,x,y,7.0f*s,t,rgba);tick=13.0f*s;break;
    case MapKind::enemy:
        if(flags&kMapLarge){MapBox(drawer,ctx,x,y,11.0f*s,t,rgba);Rect(drawer,ctx,x-6.0f*s,y-6.0f*s,x+6.0f*s,y+6.0f*s,rgba);tick=11.0f*s;}
        else Tri(drawer,ctx,x,y-5.0f*s,x,y+6.0f*s,6.0f*s,rgba);   // a small one: a red triangle, its point down onto it
        break;
    case MapKind::enemyAir:
        MapDiamond(drawer,ctx,x,y,(flags&kMapLarge ? 12.0f : 8.0f)*s,t,rgba);Rect(drawer,ctx,x-2.5f*s,y-2.5f*s,x+2.5f*s,y+2.5f*s,rgba);
        tick=(flags&kMapLarge ? 12.0f : 8.0f)*s;
        break;
    case MapKind::marker: Arc(drawer,ctx,x,y,13.0f*s,0.0f,kTurn,t,20,rgba);Rect(drawer,ctx,x-2.5f*s,y-2.5f*s,x+2.5f*s,y+2.5f*s,rgba);break;
    case MapKind::lock: break;
    }
    if(tick>0.0f && (dx!=0.0f || dy!=0.0f))Seg(drawer,ctx,x+dx*tick,y+dy*tick,x+dx*(tick+8.0f*s),y+dy*(tick+8.0f*s),t,rgba);
    if(hp>=0.0f)Bar(drawer,ctx,x-17.0f*s,y+15.0f*s,34.0f*s,4.0f*s,hp,hp,MapFade(HpColour(hp),fade,b),s);
}

// A small enemy's dot at (x, y): filled on the ground, hollow flying (kMapFlying).
void MapDot1(void* drawer,void* ctx,float x,float y,float s,std::uint8_t flags,const float* rgba) noexcept {
    if(flags&kMapFlying)MapBox(drawer,ctx,x,y,3.5f*s,1.5f*s,rgba);
    else Rect(drawer,ctx,x-3.0f*s,y-3.0f*s,x+3.0f*s,y+3.0f*s,rgba);
}

// Where a unit's pin is on the screen: its icon (`ix`, `iy`), its stem's other end (`bx`, `by`; false: none shown), its
// heading on the screen, its depth. A ground unit's stem stands `pin` m up from it to the icon; an aircraft's icon is
// on the aircraft, its stem down to the ground under it (its height read off the stem).
struct Pin { float ix,iy,bx,by,dx,dy,depth; bool stem; };
bool MapPin(const float* vp,float width,float height,const MapUnit& u,float pin,Pin* p) noexcept {
    // An aircraft standing on the ground (parked, landed: mapmarks::Landed) gets a ground unit's pin: its icon up on a
    // stem as a vehicle's, not lost in the ground's clutter at its wheels.
    const bool air=u.kind==MapKind::lock ||
                   ((u.kind==MapKind::air || u.kind==MapKind::carrier || u.kind==MapKind::enemyAir) && !mapmarks::Landed(u.pos[1],u.ground));
    // A soldier's mark is on its body, no stem (the user, 2026-10-09: "这个图标为什么要在npc的竖直顶上。直接在npc身上不好吗").
    const bool soldier=u.kind==MapKind::squad || u.kind==MapKind::ally;
    float top[3]={u.pos[0],u.pos[1],u.pos[2]},base[3]={u.pos[0],u.pos[1],u.pos[2]};
    if(air)base[1]=u.ground;
    else if(soldier)mapcmd::BodyPoint(u.pos,false,top);
    else top[1]+=pin;
    float depth;
    if(!Project(vp,top,width,height,&p->ix,&p->iy,&p->depth))return false;
    p->stem=(!air || u.pos[1]-u.ground>1.0f) && !soldier && u.kind!=MapKind::lock && Project(vp,base,width,height,&p->bx,&p->by,&depth);
    p->dx=p->dy=0.0f;
    if(u.dir[0]!=0.0f || u.dir[1]!=0.0f) {
        const float ahead[3]={top[0]+u.dir[0]*pin*0.5f,top[1],top[2]+u.dir[1]*pin*0.5f};
        float x,y;
        if(Project(vp,ahead,width,height,&x,&y,&depth)) {
            const float dx=x-p->ix,dy=y-p->iy,len=std::sqrt(dx*dx+dy*dy);
            if(len>0.5f){p->dx=dx/len;p->dy=dy/len;}
        }
    }
    return true;
}

void MapUnits(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const MapReadout& m,Line* lines,int* at) noexcept {
    const mapcam::View view{{m.focus[0],m.focus[1],m.focus[2]},m.yaw,m.pitch,m.height};
    const float dist=mapcam::Distance(view),pin=mapcam::PinHeight(dist,m.pitch);
    // Depth fade: full at the focus's distance and nearer, kMapFarFade past three times it.
    auto fade=[&](float depth){ return 1.0f-(1.0f-kMapFarFade)*Unit((depth-dist)/(2.0f*dist)); };
    alignas(16) float c[4];
    // The small enemies first: flat dots (no pin), filled on the ground, hollow flying.
    for(int i=0;i<m.dots && i<kMapDots;++i) {
        const MapDot& d=m.dot[i];
        float x,y,depth;
        if(!Project(vp,d.pos,width,height,&x,&y,&depth))continue;
        MapDot1(drawer,ctx,x,y,s,d.flags,MapFade(kMapEnemy,fade(depth),c));
        if(d.flags&kMapNearest)MapBrackets(drawer,ctx,x,y,10.0f*s,2.0f*s,MapFade(kAmber,fade(depth),c));
    }
    // The large enemies' pins, the friendly side over them, the markers and the locks on top.
    static const MapKind kOrder[]={MapKind::enemy,MapKind::enemyAir,MapKind::ally,MapKind::squad,MapKind::vehicle,MapKind::air,
                                   MapKind::carrier,MapKind::marker};
    for(MapKind kind:kOrder) {
        for(int i=0;i<m.count && i<kMapUnits;++i) {
            const MapUnit& u=m.unit[i];
            Pin p;
            if(u.kind!=kind || !MapPin(vp,width,height,u,pin,&p))continue;
            const float f=fade(p.depth);
            if(p.stem) {
                Seg(drawer,ctx,p.bx,p.by,p.ix,p.iy,1.5f*s,MapFade(MapColour(kind),f*0.7f,c));
                if((kind==MapKind::air || kind==MapKind::carrier || kind==MapKind::enemyAir) && !mapmarks::Landed(u.pos[1],u.ground))
                    Arc(drawer,ctx,p.bx,p.by,4.0f*s,0.0f,kTurn,1.5f*s,10,c);   // a flying one's ground point
            }
            MapIcon(drawer,ctx,p.ix,p.iy,s,kind,u.flags,p.dx,p.dy,u.flags&kMapLarge ? u.hp : -1.0f,f);
            if(u.flags&kMapNearest)MapBrackets(drawer,ctx,p.ix,p.iy,16.0f*s,2.0f*s,MapFade(kAmber,f,c));
            if(kind==MapKind::marker && MapLabelFree(p.ix,p.iy-24.0f*s,width,height,s)) {
                wchar_t d[24];MapDistance(d,_countof(d),vec::Flat(u.pos,m.me));
                const float top[3]={u.pos[0],u.pos[1]+pin,u.pos[2]};
                WorldLabel(text,lines,at,vp,width,height,top,WorldKey(kWorldMapMarker,u.of),0.0f,-24.0f*s,1,kLineScale*0.7f,kMapMarker,L"%ls",d);
            }
        }
    }
    // The locks: brackets on the enemy (a large one's pin, a small one's dot) nearest the lock point within kMapLockNear m,
    // else on the point itself.
    auto d2=[](const float* a,const float* b){ const float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};return d[0]*d[0]+d[1]*d[1]+d[2]*d[2]; };
    for(int i=0;i<m.count && i<kMapUnits;++i) {
        const MapUnit& l=m.unit[i];
        if(l.kind!=MapKind::lock)continue;
        const float* rgba=l.flags&kMapAcquiring ? kAmber : kMapEnemy;
        const MapUnit* on=&l;
        const MapDot* dot=nullptr;
        float best=kMapLockNear*kMapLockNear;
        for(int k=0;k<m.count && k<kMapUnits;++k) {
            const MapUnit& u=m.unit[k];
            if((u.kind==MapKind::enemy || u.kind==MapKind::enemyAir) && d2(u.pos,l.pos)<best){best=d2(u.pos,l.pos);on=&u;}
        }
        for(int k=0;k<m.dots && k<kMapDots;++k)if(d2(m.dot[k].pos,l.pos)<best){best=d2(m.dot[k].pos,l.pos);dot=&m.dot[k];}
        float x,y,depth;
        Pin p;
        if(dot){if(Project(vp,dot->pos,width,height,&x,&y,&depth))MapBrackets(drawer,ctx,x,y,13.0f*s,2.5f*s,rgba);}
        else if(MapPin(vp,width,height,*on,pin,&p))MapBrackets(drawer,ctx,p.ix,p.iy,20.0f*s,2.5f*s,rgba);
    }
    // The player: a white pin, a ring and an arrow along their heading.
    MapUnit me{};
    std::memcpy(me.pos,m.me,12);me.ground=m.me[1];me.dir[0]=m.meDir[0];me.dir[1]=m.meDir[2];
    Pin p;
    if(!MapPin(vp,width,height,me,pin,&p))return;
    if(p.stem)Seg(drawer,ctx,p.bx,p.by,p.ix,p.iy,2.0f*s,kWhite);
    Arc(drawer,ctx,p.ix,p.iy,11.0f*s,0.0f,kTurn,2.5f*s,24,kWhite);
    if(p.dx!=0.0f || p.dy!=0.0f)Tri(drawer,ctx,p.ix-p.dx*5.0f*s,p.iy-p.dy*5.0f*s,p.ix+p.dx*20.0f*s,p.iy+p.dy*20.0f*s,7.0f*s,kWhite);
}

// Text is drawn in a final batch, over every panel: an opaque panel first empties the earlier lines (world labels) under
// it, else they would show through on top of it.
void HideLinesUnder(Line* lines,int prior,float x0,float y0,float x1,float y1) noexcept {
    for(int i=0;i<prior;++i) {
        auto& line=lines[i];
        if(line.x<x1 && line.x+line.w>x0 && line.y<y1 && line.y+line.h>y0)line.text[0]=0;
    }
}

// Map panels share a viewport-fitted scale and publish the exact opaque UI regions.
float mapUiPanels[kMapUiPanels*4]{};int mapUiPanelCount=0;
void MapUiBox(void* drawer,void* ctx,float x0,float y0,float x1,float y1,Line* lines,int prior) noexcept {
    HideLinesUnder(lines,prior,x0,y0,x1,y1);
    Rect(drawer,ctx,x0,y0,x1,y1,kMapBand);
    if(mapUiPanelCount<kMapUiPanels) {
        auto* r=mapUiPanels+4*mapUiPanelCount++;r[0]=x0;r[1]=y0;r[2]=x1;r[3]=y1;
    }
}
void MapFitLabel(Text* text,Line& row,float left,float right) noexcept {
    if(!text || !(row.w>right-left))return;
    const float mid=row.y+row.h*0.5f;
    // Native fonts quantize pixel sizes; remeasure instead of assuming one proportional shrink fits.
    for(int pass=0;pass<4 && row.w>right-left;++pass) {
        row.scale*=std::fmax(0.01f,(right-left)/row.w)*0.96f;MeasureAll(*text,&row,1);
    }
    row.x=std::fmax(left,std::fmin(row.x,right-row.w));row.y=mid-row.h*0.5f;
}
alignas(16) const float kMapBoxFill[4]={0.3f,0.9f,1.0f,0.08f};
alignas(16) const float kMapLocked[4]={0.6f,0.6f,0.6f,0.6f};   // a squad a mission script drives: shown, takes no order
alignas(16) const float kMapPathReverse[4]={0.85f,0.5f,1.0f,1.0f};   // a reverse move's path (violet)
// A point order's path and ring by its kind (the legend's rows; Broken Arrow's paths coloured by order): a move white,
// an attack-move amber, a fast move green, a reverse violet, an unload yellow, a guard the map's own cyan.
const float* MapPathColour(Order o) noexcept;
constexpr float kMapGuardRing=12.0f;      // m: the ring at a guard order's point (formation slots are 30 m apart)
// The squad panel (docs/npc-ai-design.md §6.1), the map's right edge under the title band: a row a squad (the number key
// that picks it, its class, members alive, what it is doing, its order), selected rows white, a script's grey.
const wchar_t* MapOrderWord(Order o) noexcept {
    switch(o) {
    case Order::guard: return Tr(Tx::orderGuard);
    case Order::follow: return Tr(Tx::orderFollow);
    case Order::engage: return Tr(Tx::orderEngage);
    case Order::focus: return Tr(Tx::orderFocus);
    case Order::board: return Tr(Tx::orderBoard);
    case Order::dismount: return Tr(Tx::orderDismount);
    case Order::dismiss: return Tr(Tx::orderDismiss);
    case Order::recruit: return Tr(Tx::orderRecruit);
    case Order::withdraw: return Tr(Tx::orderWithdraw);
    case Order::move: return Tr(Tx::orderMove);
    case Order::attackMove: return Tr(Tx::orderAttackMove);
    case Order::dismountAll: return Tr(Tx::orderDismountAll);
    case Order::hold: return Tr(Tx::orderHold);
    case Order::formation: return Tr(Tx::btnFormationShort);
    case Order::fastMove: return Tr(Tx::orderFastMove);
    case Order::reverseMove: return Tr(Tx::orderReverseMove);
    case Order::unloadAt: return Tr(Tx::orderUnloadAt);
    case Order::holdFire: return Tr(Tx::orderHoldFire);
    case Order::altitude: return Tr(Tx::orderAltitude);
    case Order::sprint: return Tr(Tx::orderSprint);
    case Order::smoke: return Tr(Tx::orderSmoke);
    case Order::decoy: return Tr(Tx::orderDecoy);
    case Order::none: break;
    }
    return L"-";
}
const float* MapPathColour(Order o) noexcept {
    switch(o) {
    case Order::move: return kWhite;
    case Order::attackMove: return kAmber;
    case Order::fastMove: return kGreen;
    case Order::reverseMove: return kMapPathReverse;
    case Order::unloadAt: return kYellow;
    default: return kMapOrder;
    }
}
// SquadCommandUnits supplies "RANGER x4"; translate the identifier, preserving the live member count.
void MapUnitName(const char* name,wchar_t* out,std::size_t size) noexcept {
    const char* count=name ? std::strstr(name," x") : nullptr;
    if(!count){hudtext::WordTo(name,out,size);return;}
    char kind[24]{};const std::size_t n=static_cast<std::size_t>(count-name);
    if(n>=sizeof(kind)){hudtext::WordTo(name,out,size);return;}
    std::memcpy(kind,name,n);wchar_t translated[32];hudtext::WordTo(kind,translated,_countof(translated));
    _snwprintf_s(out,size,_TRUNCATE,L"%ls%hs",translated,count);
}
void MapSquadStatus(const SquadRow& r,wchar_t* out,std::size_t size) noexcept {
    if(std::strncmp(r.status,"WAIT ",5)==0)_snwprintf_s(out,size,_TRUNCATE,Tr(Tx::squadWait),r.cooldown);
    else hudtext::WordTo(r.status,out,size);
}
// A unit's RTS groups as their digits in brackets ("[13]": groups 1 and 3), nothing (three spaces) in none.
void MapGroupDigits(std::uint16_t groups,wchar_t* out,std::size_t size) noexcept {
    if(!groups){_snwprintf_s(out,size,_TRUNCATE,L"   ");return;}
    wchar_t digits[mapcmd::kGroups+1]{};int n=0;
    for(int g=0;g<mapcmd::kGroups;++g)if(groups&(1u<<g))digits[n++]=static_cast<wchar_t>(L'1'+g);
    _snwprintf_s(out,size,_TRUNCATE,L"[%ls]",digits);
}
// The panel's rows (the user, 2026-10-09: "小队太多了吧，怎么处理合适，感觉看不过来"): sorted by SquadRank (the player's,
// the free ones, riding, a script's), folded to the ones on foot that take orders (at most 9), the rest
// summed in a row that opens it (mapcmd_logic.h SquadRowsShown). Its bottom (px), for what goes under it; `top` none.
float MapSquadPanel(void* drawer,void* ctx,Text* text,float width,float s,bool pad,const MapCommandReadout& c,Line* lines,int* at) noexcept {
    const float x0=16.0f*s,rowH=22.0f*s,top=56.0f*s;
    if(c.squads<=0){MapCommandSquadButtons(nullptr,nullptr,0);MapCommandSquadFold(nullptr);return top;}
    // Top left, under the title band and over the legend; the compass has the top right.
    int ranks[16];
    const int listed=c.squads<16 ? c.squads : 16;
    for(int i=0;i<listed;++i)ranks[i]=c.squad[i].rank;
    const int rows=mapcmd::SquadRowsShown(ranks,listed,c.squadOpen,16);
    const int foldedRows=mapcmd::SquadRowsShown(ranks,listed,false,16);
    const int total=c.squadTally.total>listed ? c.squadTally.total : listed;
    const bool fold=c.squadOpen ? foldedRows<total : rows<total;
    if(*at+rows+2>kMaxLines){MapCommandSquadButtons(nullptr,nullptr,0);MapCommandSquadFold(nullptr);return top;}
    const int first=*at;
    Line& title=lines[(*at)++];Format(title,L"%ls",Tr(pad ? Tx::squadTitle : Tx::squadTitleKeys));
    title.rgba=kMapOrder;
    for(int i=0;i<rows;++i) {
        const SquadRow& r=c.squad[i];
        const float* tint=r.locked ? kMapLocked : c.squadSelected[i] ? kWhite : kMapOrder;
        // Its RTS groups (Ctrl+1-9; the number keys select a group now, not a row).
        wchar_t key[16]=L"   ";
        MapGroupDigits(c.squadGroups[i],key,_countof(key));
        wchar_t name[32],status[32];hudtext::WordTo(r.name,name,_countof(name));MapSquadStatus(r,status,_countof(status));
        Line& row=lines[(*at)++];Format(row,L"%ls  %ls x%d   %ls   %ls",key,name,r.alive,status,MapOrderWord(r.now.order));row.rgba=tint;
    }
    if(fold) {
        Line& more=lines[(*at)++];
        if(c.squadOpen)Format(more,L"%ls",Tr(Tx::squadFold));
        else Format(more,Tr(Tx::squadMore),total-rows,c.squadTally.riding,c.squadTally.scripted);
        more.rgba=kMapOrderDim;
    }
    float panelW=0.0f;
    for(int i=first;i<*at;++i) {
        Line& row=lines[i];row.scale=kLineScale*0.7f;row.w=row.h=0;
        if(text)MeasureAll(*text,&row,1);
        const float most=std::fmin(520.0f*s,width*0.46f-32.0f*s);
        if(row.w>most && text){row.scale*=most/row.w;MeasureAll(*text,&row,1);}
        row.x=x0;row.y=top+rowH*static_cast<float>(i-first);
        panelW=std::fmax(panelW,row.w);
    }
    const int drawn=*at-first;   // the title, the rows, the summary
    const float left=x0-8.0f*s,right=x0+panelW+8.0f*s,bottom=top+rowH*static_cast<float>(drawn)+4.0f*s;
    MapUiBox(drawer,ctx,left,top-6.0f*s,right,bottom,lines,first);
    float rects[16*4]{};ObjRef identities[16]{};
    for(int i=0;i<rows;++i) {
        const auto& row=lines[first+i+1];auto* hit=rects+i*4;
        hit[0]=left;hit[1]=row.y;hit[2]=right;hit[3]=row.y+rowH;
        identities[i]=c.squad[i].identity;
        if(c.pointer && c.px>=left && c.px<right && c.py>=hit[1] && c.py<hit[3])
            Rect(drawer,ctx,left,hit[1],right,hit[3],kMapBoxFill);
    }
    MapCommandSquadButtons(pad ? nullptr : rects,pad ? nullptr : identities,pad ? 0 : rows);
    if(fold && !pad) {
        const auto& row=lines[first+rows+1];
        const float hit[4]={left,row.y,right,row.y+rowH};
        if(c.pointer && c.px>=left && c.px<right && c.py>=hit[1] && c.py<hit[3])Rect(drawer,ctx,left,hit[1],right,hit[3],kMapBoxFill);
        MapCommandSquadFold(hit);
    } else MapCommandSquadFold(nullptr);
    return bottom;
}

// The map's right-hand dock (the user, 2026-10-19: 「m地图里面右边的操作太难受了」「操作区域和图例冲突了」): one column at
// the right edge under the compass and the scale, from top to bottom the payload panel, the enemy control, PVP and the
// test range's 「摆放」, with the legend folded at its foot. Each panel has its own band of it: nothing opens over
// another, nothing grows or shrinks under the pointer (every fold is a click), so a click lands where it was aimed.
struct MapDock { float x0,x1; };
MapDock MapDockAt(float width,float s) noexcept {
    const float x1=width-12.0f*s;
    return {x1-std::fmin(420.0f*s,width*0.32f),x1};
}
// A map-only interactive loadout keeps left-click firing and camera aim untouched outside M. Its bottom (px), 0 none.
float MapPayloadPanel(void* drawer,void* ctx,Text* text,float width,float s,bool pad,
                      const MapCommandReadout& commands,Line* lines,int* at) noexcept {
    PayloadReadout r{};
    if(!PlayerSelectablePayload(&r) || r.count<=0){MapCommandPayloadButtons(nullptr,0,0,nullptr,0);return 0.0f;}
    const int count=r.count<kMostPayload ? r.count : kMostPayload;
    const MapDock dock=MapDockAt(width,s);   // the dock's width, as every panel under it
    const float right=dock.x1-8.0f*s,left=dock.x0+8.0f*s,top=216.0f*s,rowH=30.0f*s;
    MapUiBox(drawer,ctx,left-8.0f*s,top-14.0f*s,right+8.0f*s,top+rowH*static_cast<float>(count+1),lines,*at);
    Label(text,lines,at,left,top,0,kLineScale*0.75f,kWhite,L"%ls",Tr(Tx::mapPayloadTitle));
    if(*at)MapFitLabel(text,lines[*at-1],left,right);
    float rects[kMostPayload*4]{};int entries[kMostPayload]{};int hits=0;
    for(int i=0;i<count;++i) {
        const auto& e=r.entry[i];const float y=top+rowH*static_cast<float>(i+1);
        const bool hover=!pad && commands.pointer && commands.px>=left && commands.px<right && commands.py>=y-12*s && commands.py<y+12*s;
        if(e.picked || (e.selectable && hover))Rect(drawer,ctx,left-3*s,y-12*s,right+3*s,y+12*s,kMapBoxFill);
        Label(text,lines,at,left+10*s,y,0,kLineScale*0.70f,e.selectable ? kWhite : kMapLocked,L"%ls  %d/%d",e.name,e.rounds,e.capacity);
        if(*at)MapFitLabel(text,lines[*at-1],left+10*s,right);
        if(e.picked)Rect(drawer,ctx,left,y-7*s,left+3*s,y+7*s,kCyan);
        if(!pad && e.selectable && r.selectionToken) {
            auto* hit=rects+4*hits;hit[0]=left;hit[1]=y-12*s;hit[2]=right;hit[3]=y+12*s;entries[hits++]=i;
        }
    }
    MapCommandPayloadButtons(rects,r.selectionToken,r.seat,entries,hits);
    return top+rowH*static_cast<float>(count+1);
}

// The map's tooltip (the user, 2026-10-09: "m里面显示太复杂了"): a button's full word and its key are not on the
// button but in a box by the pointer while it is over it, drawn over everything else (MapScreen, last).
struct MapTip { bool on; float x,y; wchar_t text[160]; };
MapTip mapTip{};
void MapTipSet(const MapCommandReadout& c,float x0,float y0,float x1,float y1,const wchar_t* format,...) noexcept {
    if(!c.pointer || c.px<x0 || c.px>=x1 || c.py<y0 || c.py>=y1)return;
    va_list args;va_start(args,format);
    _vsnwprintf_s(mapTip.text,_countof(mapTip.text),_TRUNCATE,format,args);
    va_end(args);
    mapTip.on=true;mapTip.x=c.px;mapTip.y=c.py;
}
void MapTipDraw(void* drawer,void* ctx,Text* text,float width,float height,float s,Line* lines,int* at) noexcept {
    if(!mapTip.on || *at>=kMaxLines)return;
    Line& l=lines[*at];
    Format(l,L"%ls",mapTip.text);l.scale=kLineScale*0.7f;l.rgba=kWhite;l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
    if(!(l.w>0.0f)){l.w=static_cast<float>(wcslen(l.text))*9.0f*s;l.h=18.0f*s;}
    const float pad=6.0f*s;
    float x=mapTip.x+18.0f*s,y=mapTip.y-l.h-22.0f*s;   // over the pointer and to its right
    if(x+l.w+2.0f*pad>width-4.0f*s)x=width-4.0f*s-l.w-2.0f*pad;
    if(x<4.0f*s)x=4.0f*s;
    if(y<50.0f*s)y=mapTip.y+24.0f*s;
    if(y+l.h+2.0f*pad>height)y=height-l.h-2.0f*pad;
    MapUiBox(drawer,ctx,x,y,x+l.w+2.0f*pad,y+l.h+2.0f*pad,lines,*at);
    l.x=x+pad;l.y=y+pad;
    ++*at;
}

// The command card's and the support bar's icons: small line drawings in a box `h` px across round (x, y), from the
// HUD's own primitives (Seg, Tri, Arc, Rect).
constexpr float kIconChevrons[]={-0.75f,-0.2f},kIconWheels3[]={-0.55f,0.0f,0.55f},kIconWheels2[]={-0.55f,0.55f};
void MapOrderIcon(void* d,void* c,mapbtn::Id id,float x,float y,float h,float s,const float* rgba) noexcept {
    using mapbtn::Id;
    const float t=1.8f*s,r=h*0.5f;
    switch(id) {
    case Id::move:          // an arrow up and right
        Seg(d,c,x-r*0.8f,y+r*0.8f,x+r*0.3f,y-r*0.3f,t,rgba);Tri(d,c,x+r*0.1f,y-r*0.1f,x+r*0.85f,y-r*0.85f,r*0.45f,rgba);break;
    case Id::attackMove:    // the arrow over a cross
        Seg(d,c,x-r*0.8f,y+r*0.8f,x+r*0.3f,y-r*0.3f,t,rgba);Tri(d,c,x+r*0.1f,y-r*0.1f,x+r*0.85f,y-r*0.85f,r*0.45f,rgba);
        Seg(d,c,x-r*0.9f,y-r*0.1f,x-r*0.1f,y+r*0.9f,t,kAmber);Seg(d,c,x-r*0.9f,y+r*0.9f,x-r*0.1f,y+r*0.1f,t,kAmber);break;
    case Id::guard:         // a shield
        Seg(d,c,x-r*0.75f,y-r*0.8f,x+r*0.75f,y-r*0.8f,t,rgba);Seg(d,c,x-r*0.75f,y-r*0.8f,x-r*0.75f,y,t,rgba);
        Seg(d,c,x+r*0.75f,y-r*0.8f,x+r*0.75f,y,t,rgba);Seg(d,c,x-r*0.75f,y,x,y+r*0.9f,t,rgba);Seg(d,c,x+r*0.75f,y,x,y+r*0.9f,t,rgba);break;
    case Id::hold:          // a shield with a dot in it (each where it stands)
        Seg(d,c,x-r*0.75f,y-r*0.8f,x+r*0.75f,y-r*0.8f,t,rgba);Seg(d,c,x-r*0.75f,y-r*0.8f,x-r*0.75f,y,t,rgba);
        Seg(d,c,x+r*0.75f,y-r*0.8f,x+r*0.75f,y,t,rgba);Seg(d,c,x-r*0.75f,y,x,y+r*0.9f,t,rgba);Seg(d,c,x+r*0.75f,y,x,y+r*0.9f,t,rgba);
        Rect(d,c,x-r*0.22f,y-r*0.3f,x+r*0.22f,y+r*0.14f,rgba);break;
    case Id::follow:        // two chevrons towards a dot (the player)
        for(const float o:kIconChevrons){Seg(d,c,x+o*r,y-r*0.6f,x+(o+0.4f)*r,y,t,rgba);Seg(d,c,x+(o+0.4f)*r,y,x+o*r,y+r*0.6f,t,rgba);}
        Rect(d,c,x+r*0.45f,y-r*0.25f,x+r*0.95f,y+r*0.25f,rgba);break;
    case Id::release:       // an open ring
        Arc(d,c,x,y,r*0.75f,-0.9f,kTurn-1.0f,t,14,rgba);break;
    case Id::engage:        // a crosshair
        Arc(d,c,x,y,r*0.6f,0.0f,kTurn,t,14,rgba);
        Seg(d,c,x-r,y,x-r*0.3f,y,t,rgba);Seg(d,c,x+r*0.3f,y,x+r,y,t,rgba);Seg(d,c,x,y-r,x,y-r*0.3f,t,rgba);Seg(d,c,x,y+r*0.3f,x,y+r,t,rgba);break;
    case Id::focus:         // a bullseye
        Arc(d,c,x,y,r*0.85f,0.0f,kTurn,t,16,kAmber);Arc(d,c,x,y,r*0.45f,0.0f,kTurn,t,12,kAmber);
        Rect(d,c,x-r*0.15f,y-r*0.15f,x+r*0.15f,y+r*0.15f,kAmber);break;
    case Id::board:         // into a box
    case Id::dismount:      // out of it
    case Id::dismountAll:   // out of it, two of them
        Seg(d,c,x-r*0.2f,y-r*0.8f,x+r*0.9f,y-r*0.8f,t,rgba);Seg(d,c,x+r*0.9f,y-r*0.8f,x+r*0.9f,y+r*0.8f,t,rgba);
        Seg(d,c,x+r*0.9f,y+r*0.8f,x-r*0.2f,y+r*0.8f,t,rgba);
        if(id==Id::board){Seg(d,c,x-r,y,x+r*0.1f,y,t,rgba);Tri(d,c,x-r*0.1f,y,x+r*0.5f,y,r*0.4f,rgba);}
        else{Seg(d,c,x+r*0.5f,y,x-r*0.4f,y,t,rgba);Tri(d,c,x-r*0.3f,y,x-r,y,r*0.4f,rgba);}
        if(id==Id::dismountAll){Seg(d,c,x+r*0.5f,y-r*0.45f,x-r*0.4f,y-r*0.45f,t,kAmber);Tri(d,c,x-r*0.3f,y-r*0.45f,x-r,y-r*0.45f,r*0.3f,kAmber);}
        break;
    case Id::withdraw:      // out of the box and away (the transport leaves the field)
        Seg(d,c,x-r*0.9f,y-r*0.8f,x+r*0.1f,y-r*0.8f,t,rgba);Seg(d,c,x-r*0.9f,y-r*0.8f,x-r*0.9f,y+r*0.8f,t,rgba);
        Seg(d,c,x-r*0.9f,y+r*0.8f,x+r*0.1f,y+r*0.8f,t,rgba);
        Seg(d,c,x-r*0.4f,y,x+r*0.5f,y,t,kAmber);Tri(d,c,x+r*0.3f,y,x+r,y,r*0.4f,kAmber);
        break;
    case Id::dismiss:       // a head and a minus
    case Id::recruit:       // a head and a plus
        Arc(d,c,x-r*0.35f,y-r*0.35f,r*0.35f,0.0f,kTurn,t,10,rgba);Seg(d,c,x-r*0.95f,y+r*0.8f,x+r*0.25f,y+r*0.8f,t,rgba);
        Seg(d,c,x-r*0.95f,y+r*0.8f,x-r*0.35f,y+r*0.15f,t,rgba);Seg(d,c,x+r*0.25f,y+r*0.8f,x-r*0.35f,y+r*0.15f,t,rgba);
        Seg(d,c,x+r*0.35f,y-r*0.4f,x+r,y-r*0.4f,t,id==Id::recruit ? kGreen : kAmber);
        if(id==Id::recruit)Seg(d,c,x+r*0.675f,y-r*0.75f,x+r*0.675f,y-r*0.05f,t,kGreen);
        break;
    case Id::formation:     // a wedge of dots
    {
        static const float kDots[5][2]={{0.0f,-0.6f},{-0.6f,0.1f},{0.6f,0.1f},{-1.0f,0.75f},{1.0f,0.75f}};
        for(const auto& p:kDots) {
            const float px=x+p[0]*r*0.85f,py=y+p[1]*r;Rect(d,c,px-r*0.18f,py-r*0.18f,px+r*0.18f,py+r*0.18f,rgba);
        }
        break;
    }
    case Id::sweep:         // a crate
        Seg(d,c,x-r*0.8f,y-r*0.6f,x+r*0.8f,y-r*0.6f,t,rgba);Seg(d,c,x+r*0.8f,y-r*0.6f,x+r*0.8f,y+r*0.7f,t,rgba);
        Seg(d,c,x+r*0.8f,y+r*0.7f,x-r*0.8f,y+r*0.7f,t,rgba);Seg(d,c,x-r*0.8f,y+r*0.7f,x-r*0.8f,y-r*0.6f,t,rgba);
        Seg(d,c,x-r*0.8f,y-r*0.6f,x+r*0.8f,y+r*0.7f,t*0.7f,rgba);Seg(d,c,x+r*0.8f,y-r*0.6f,x-r*0.8f,y+r*0.7f,t*0.7f,rgba);break;
    case Id::health:        // a cross
        Rect(d,c,x-r*0.25f,y-r*0.8f,x+r*0.25f,y+r*0.8f,kGreen);Rect(d,c,x-r*0.8f,y-r*0.25f,x+r*0.8f,y+r*0.25f,kGreen);break;
    case Id::fastMove:      // two arrow heads (the arrow twice as fast)
        Seg(d,c,x-r*0.8f,y+r*0.8f,x+r*0.3f,y-r*0.3f,t,rgba);Tri(d,c,x-r*0.3f,y+r*0.3f,x+r*0.3f,y-r*0.3f,r*0.4f,kGreen);
        Tri(d,c,x+r*0.1f,y-r*0.1f,x+r*0.85f,y-r*0.85f,r*0.45f,kGreen);break;
    case Id::reverseMove:   // the arrow backwards (down and left)
        Seg(d,c,x+r*0.8f,y-r*0.8f,x-r*0.3f,y+r*0.3f,t,rgba);Tri(d,c,x-r*0.1f,y+r*0.1f,x-r*0.85f,y+r*0.85f,r*0.45f,kMapPathReverse);break;
    case Id::unloadAt:      // out of the box onto a point
        Seg(d,c,x-r*0.2f,y-r*0.8f,x+r*0.9f,y-r*0.8f,t,rgba);Seg(d,c,x+r*0.9f,y-r*0.8f,x+r*0.9f,y+r*0.2f,t,rgba);
        Seg(d,c,x+r*0.9f,y+r*0.2f,x-r*0.2f,y+r*0.2f,t,rgba);Seg(d,c,x+r*0.3f,y-r*0.3f,x-r*0.5f,y-r*0.3f,t,rgba);
        Arc(d,c,x-r*0.55f,y+r*0.65f,r*0.3f,0.0f,kTurn,t,10,kYellow);break;
    case Id::holdFire:      // a crosshair struck through
        Arc(d,c,x,y,r*0.6f,0.0f,kTurn,t,14,rgba);
        Seg(d,c,x-r,y,x-r*0.3f,y,t,rgba);Seg(d,c,x+r*0.3f,y,x+r,y,t,rgba);Seg(d,c,x,y-r,x,y-r*0.3f,t,rgba);Seg(d,c,x,y+r*0.3f,x,y+r,t,rgba);
        Seg(d,c,x-r*0.85f,y-r*0.85f,x+r*0.85f,y+r*0.85f,t,kRed);break;
    case Id::altitude:      // up and down
        Seg(d,c,x,y-r*0.5f,x,y+r*0.5f,t,rgba);Tri(d,c,x,y-r*0.3f,x,y-r*0.95f,r*0.45f,rgba);Tri(d,c,x,y+r*0.3f,x,y+r*0.95f,r*0.45f,rgba);break;
    case Id::sprint:        // a bolt
        Seg(d,c,x+r*0.35f,y-r*0.9f,x-r*0.35f,y+r*0.05f,t,kAmber);Seg(d,c,x-r*0.35f,y+r*0.05f,x+r*0.35f,y-r*0.05f,t,kAmber);
        Seg(d,c,x+r*0.35f,y-r*0.05f,x-r*0.35f,y+r*0.9f,t,kAmber);break;
    case Id::smoke:         // a cloud
        Arc(d,c,x-r*0.45f,y+r*0.2f,r*0.38f,0.0f,kTurn,t,10,rgba);Arc(d,c,x+r*0.45f,y+r*0.2f,r*0.38f,0.0f,kTurn,t,10,rgba);
        Arc(d,c,x,y-r*0.25f,r*0.45f,0.0f,kTurn,t,12,rgba);break;
    case Id::decoy:         // a burst of flares
        for(int k=0;k<6;++k) {
            const float a=kTurn*static_cast<float>(k)/6.0f;
            Seg(d,c,x+std::sin(a)*r*0.3f,y+std::cos(a)*r*0.3f,x+std::sin(a)*r*0.9f,y+std::cos(a)*r*0.9f,t,kAmber);
        }
        break;
    case Id::pace:          // three in step
        Seg(d,c,x-r*0.9f,y+r*0.55f,x+r*0.9f,y+r*0.55f,t,rgba);
        for(const float o:{-0.6f,0.0f,0.6f})Rect(d,c,x+o*r-r*0.18f,y-r*0.25f,x+o*r+r*0.18f,y+r*0.11f,rgba);
        break;
    case Id::count: break;
    }
}
// MapAircraft's drawing reaches kAircraftReach of its own units from its centre (the heli's tail boom, the plane's nose):
// an icon `h` px across draws it at h / 2 / kAircraftReach. `h` is already in screen px (the caller's kSupIcon * s): the
// HUD scale must not go in a second time (the user, 2026-10-09, a 4K screenshot: "左边的重叠了" -- at s 2 the aircraft
// were drawn at s * s, twice their row, over the rows round them).
constexpr float kAircraftReach=13.0f;
void MapSupportIcon(void* d,void* c,SupportIcon icon,float x,float y,float h,float s,const float* rgba) noexcept {
    const float t=1.8f*s,r=h*0.5f,plane=r/kAircraftReach;
    auto heads=[&](int n){
        for(int i=0;i<n;++i) {
            const float px=x+(static_cast<float>(i%3)-1.0f)*r*0.65f,py=y+(n>3 ? (i<3 ? -0.4f : 0.45f) : 0.0f)*r;
            Arc(d,c,px,py,r*0.22f,0.0f,kTurn,t*0.8f,8,rgba);
        }
    };
    switch(icon) {
    case SupportIcon::jet: MapAircraft(d,c,x,y,plane,false,0.0f,-1.0f,rgba);break;
    case SupportIcon::heli: MapAircraft(d,c,x,y,plane,true,0.0f,-1.0f,rgba);break;
    case SupportIcon::carrier: MapBox(d,c,x,y,r*0.9f,t,rgba);MapBox(d,c,x,y,r*0.45f,t,rgba);break;
    case SupportIcon::gunship:
        MapAircraft(d,c,x,y,plane,false,0.0f,-1.0f,rgba);Arc(d,c,x,y,r*0.95f,0.0f,kTurn,t*0.7f,16,kAmber);break;
    case SupportIcon::sub:
        Arc(d,c,x-r*0.45f,y+r*0.2f,r*0.4f,kTurn*0.25f,kTurn*0.5f,t,8,rgba);Arc(d,c,x+r*0.45f,y+r*0.2f,r*0.4f,-kTurn*0.25f,kTurn*0.5f,t,8,rgba);
        Seg(d,c,x-r*0.45f,y-r*0.2f,x+r*0.45f,y-r*0.2f,t,rgba);Seg(d,c,x-r*0.45f,y+r*0.6f,x+r*0.45f,y+r*0.6f,t,rgba);
        Rect(d,c,x-r*0.15f,y-r*0.65f,x+r*0.2f,y-r*0.2f,rgba);break;
    case SupportIcon::squad: heads(3);break;
    case SupportIcon::platoon: heads(6);break;
    case SupportIcon::tank:
        Seg(d,c,x-r*0.9f,y+r*0.5f,x+r*0.9f,y+r*0.5f,t,rgba);Seg(d,c,x-r*0.9f,y+r*0.5f,x-r*0.9f,y,t,rgba);
        Seg(d,c,x+r*0.9f,y+r*0.5f,x+r*0.9f,y,t,rgba);Seg(d,c,x-r*0.9f,y,x+r*0.9f,y,t,rgba);
        Rect(d,c,x-r*0.4f,y-r*0.4f,x+r*0.3f,y,rgba);Seg(d,c,x+r*0.2f,y-r*0.25f,x+r,y-r*0.45f,t,rgba);break;
    case SupportIcon::apc:
        Seg(d,c,x-r*0.9f,y+r*0.3f,x+r*0.9f,y+r*0.3f,t,rgba);Seg(d,c,x-r*0.9f,y+r*0.3f,x-r*0.9f,y-r*0.5f,t,rgba);
        Seg(d,c,x+r*0.9f,y+r*0.3f,x+r*0.9f,y-r*0.2f,t,rgba);Seg(d,c,x-r*0.9f,y-r*0.5f,x+r*0.5f,y-r*0.5f,t,rgba);
        Seg(d,c,x+r*0.5f,y-r*0.5f,x+r*0.9f,y-r*0.2f,t,rgba);
        for(const float o:kIconWheels3)Arc(d,c,x+o*r,y+r*0.55f,r*0.2f,0.0f,kTurn,t*0.8f,8,rgba);
        break;
    case SupportIcon::truck:
        Rect(d,c,x+r*0.25f,y-r*0.45f,x+r*0.9f,y+r*0.3f,rgba);
        Seg(d,c,x-r*0.9f,y+r*0.3f,x+r*0.2f,y+r*0.3f,t,rgba);Seg(d,c,x-r*0.9f,y+r*0.3f,x-r*0.9f,y-r*0.15f,t,rgba);
        for(const float o:kIconWheels2)Arc(d,c,x+o*r,y+r*0.55f,r*0.22f,0.0f,kTurn,t*0.8f,8,rgba);
        break;
    case SupportIcon::barrage:   // three shells falling onto a burst
        for(int i=-1;i<=1;++i)Seg(d,c,x+i*r*0.5f-r*0.3f,y-r*0.9f,x+i*r*0.5f,y-r*0.1f,t,rgba);
        Arc(d,c,x,y+r*0.45f,r*0.45f,0.0f,kTurn,t,12,kRed);
        break;
    case SupportIcon::satellite:   // a beam down onto a ring
        Seg(d,c,x,y-r*0.95f,x,y+r*0.35f,t*1.4f,rgba);Rect(d,c,x-r*0.45f,y-r*0.95f,x+r*0.45f,y-r*0.7f,rgba);
        Arc(d,c,x,y+r*0.45f,r*0.8f,0.0f,kTurn,t,16,kRed);
        break;
    }
}
// A variant chip's icon (support_call.h SupportCallVariant: the catalog's own data, never its words): guarding a point,
// following the player, a crew aboard, an empty vehicle delivered, the vehicle a plane drops.
void MapVariantIcon(void* d,void* c,SupportVariant variant,float x,float y,float h,float s,const float* rgba) noexcept {
    switch(variant) {
    case SupportVariant::guard: MapOrderIcon(d,c,mapbtn::Id::guard,x,y,h,s,rgba);break;
    case SupportVariant::follow: MapOrderIcon(d,c,mapbtn::Id::follow,x,y,h,s,rgba);break;
    case SupportVariant::empty: MapBox(d,c,x,y,h*0.4f,1.6f*s,rgba);break;
    case SupportVariant::crewed:
        Arc(d,c,x,y-h*0.15f,h*0.18f,0.0f,kTurn,1.6f*s,8,rgba);Seg(d,c,x-h*0.3f,y+h*0.35f,x+h*0.3f,y+h*0.35f,1.6f*s,rgba);break;
    case SupportVariant::squad: MapSupportIcon(d,c,SupportIcon::squad,x,y,h,s,rgba);break;
    case SupportVariant::platoon: MapSupportIcon(d,c,SupportIcon::platoon,x,y,h,s,rgba);break;
    case SupportVariant::tank: MapSupportIcon(d,c,SupportIcon::tank,x,y,h,s,rgba);break;
    case SupportVariant::apc: MapSupportIcon(d,c,SupportIcon::apc,x,y,h,s,rgba);break;
    case SupportVariant::truck: MapSupportIcon(d,c,SupportIcon::truck,x,y,h,s,rgba);break;
    case SupportVariant::none: Rect(d,c,x-h*0.12f,y-h*0.12f,x+h*0.12f,y+h*0.12f,rgba);break;
    }
}
constexpr float kBtnRowH=30.0f,kBtnGap=6.0f,kBtnPad=8.0f,kBtnIcon=18.0f,kBtnMargin=16.0f,kBtnBottom=142.0f;   // px at 1080 lines
// The ALL OUT button's menu (map_buttons.h kDismountMenu; test site #45 「如何让某个角色单独下车」): over the button, a row a
// choice -- everyone, everyone but the driver, one class's riders; a click on a row is that DISMOUNT ALL (mapcmd.cpp
// PickDismount). Its rows go to mapcmd.cpp as the formation menu's do (one menu at a time).
void MapDismountMenu(void* drawer,void* ctx,Text* text,float width,float height,float s,const MapCommandReadout& c,
                     const float* buttons,const int* ids,int n,Line* lines,int* at) noexcept {
    int button=-1;
    for(int i=0;i<n;++i)if(ids[i]==static_cast<int>(mapbtn::Id::dismountAll))button=i;
    if(button<0){MapCommandFormationButtons(nullptr,nullptr,0);return;}
    static_assert(mapbtn::kDismountMenuRows<=kMapFormationEntries,"the menu's rows fit the formation menu's");
    const int count=mapbtn::kDismountMenuRows;
    int entries[mapbtn::kDismountMenuRows];
    const float scale=kLineScale*0.7f,pad=kBtnPad*s,rowH=26.0f*s,gap=2.0f*s;
    wchar_t word[mapbtn::kDismountMenuRows][64];float rowW=120.0f*s;
    for(int i=0;i<count;++i) {
        const std::uint32_t p=mapbtn::kDismountMenu[i];
        entries[i]=mapbtn::DismountEntry(p);
        const std::uint32_t cls=p&mapcmd::kDismountClasses;
        if(!p)wcsncpy_s(word[i],Tr(Tx::dismountEveryone),_TRUNCATE);
        else if(!cls)wcsncpy_s(word[i],Tr(Tx::dismountKeepDriver),_TRUNCATE);
        else {
            const Tx name=cls==mapcmd::kDismountRanger ? Tx::npcRanger : cls==mapcmd::kDismountWingDiver ? Tx::npcWingDiver :
                          cls==mapcmd::kDismountFencer ? Tx::npcFencer : Tx::npcAirRaider;
            _snwprintf_s(word[i],_countof(word[i]),_TRUNCATE,Tr(Tx::dismountClass),Tr(name));
        }
        Line probe{};Format(probe,L"%ls",word[i]);probe.scale=scale;
        if(text)MeasureAll(*text,&probe,1);
        const float w=(text ? probe.w : static_cast<float>(wcslen(probe.text))*9.0f*s)+2.0f*pad;
        rowW=std::fmax(rowW,w);
    }
    float top=buttons[button*4+1];
    for(int i=0;i<n;++i)top=std::fmin(top,buttons[i*4+1]);
    const mapbtn::Rect at0{buttons[button*4],top,buttons[button*4+2],top+(buttons[button*4+3]-buttons[button*4+1])};
    mapbtn::Rect row[mapbtn::kDismountMenuRows]{};
    const int placed=mapbtn::MenuColumn(at0,count,rowW,rowH,gap,width,height,row);
    if(placed<=0){MapCommandFormationButtons(nullptr,nullptr,0);return;}
    MapUiBox(drawer,ctx,row[0].x0-4.0f*s,row[0].y0-4.0f*s,row[0].x1+4.0f*s,row[placed-1].y1+4.0f*s,lines,*at);
    float rects[mapbtn::kDismountMenuRows*4];
    for(int i=0;i<placed;++i) {
        const mapbtn::Rect& q=row[i];
        const bool hover=c.pointer && c.px>=q.x0 && c.px<q.x1 && c.py>=q.y0 && c.py<q.y1;
        Rect(drawer,ctx,q.x0,q.y0,q.x1,q.y1,hover ? kMapBoxFill : kBtnFill);
        Label(text,lines,at,q.x0+pad,(q.y0+q.y1)*0.5f,0,scale,kMapOrder,L"%ls",word[i]);
        if(*at>0)MapFitLabel(text,lines[*at-1],q.x0+pad,q.x1-pad);
        rects[i*4]=q.x0;rects[i*4+1]=q.y0;rects[i*4+2]=q.x1;rects[i*4+3]=q.y1;
    }
    MapCommandFormationButtons(rects,entries,placed);
}
// The formation button's menu (map_buttons.h MenuColumn; the user, 2026-10-09: "这个编队应该点击以后展开选择里面的东西"):
// over the button, a row a shape -- a selected guarding squad's defences, the march of the squads following the player --
// the shape in use lit; a click on a row is that shape (mapcmd.cpp PickFormation).
void MapFormationMenu(void* drawer,void* ctx,Text* text,float width,float height,float s,const MapCommandReadout& c,
                      const float* buttons,const int* ids,int n,Line* lines,int* at) noexcept {
    if(!c.formationMenu && c.dismountMenu){MapDismountMenu(drawer,ctx,text,width,height,s,c,buttons,ids,n,lines,at);return;}
    int button=-1;
    for(int i=0;i<n;++i)if(ids[i]==static_cast<int>(mapbtn::Id::formation))button=i;
    if(!c.formationMenu || button<0){MapCommandFormationButtons(nullptr,nullptr,0);return;}
    int entries[kMapFormationEntries];int count=0;
    if(c.formationGuard)for(const auto shape:npc::formation::kGuard)if(count<kMapFormationEntries)entries[count++]=mapbtn::MenuEntry(true,static_cast<int>(shape));
    if(c.formationMarch)for(const auto shape:npc::formation::kMarch)if(count<kMapFormationEntries)entries[count++]=mapbtn::MenuEntry(false,static_cast<int>(shape));
    const float scale=kLineScale*0.7f,pad=kBtnPad*s,rowH=26.0f*s,gap=2.0f*s;
    wchar_t word[kMapFormationEntries][64];float rowW=120.0f*s;
    for(int i=0;i<count;++i) {
        _snwprintf_s(word[i],_countof(word[i]),_TRUNCATE,L"%ls  %ls",Tr(mapbtn::MenuGuard(entries[i]) ? Tx::menuDefence : Tx::menuMarch),
                     FormationText(mapbtn::MenuShape(entries[i]),mapbtn::MenuGuard(entries[i])));
        Line probe{};Format(probe,L"%ls",word[i]);probe.scale=scale;
        if(text)MeasureAll(*text,&probe,1);
        const float w=(text ? probe.w : static_cast<float>(wcslen(probe.text))*9.0f*s)+2.0f*pad;
        rowW=std::fmax(rowW,w);
    }
    // Over the whole card (its top row's top), at the formation button's left: a card of more rows than one (a narrow
    // screen, the transport's WITHDRAW making one more) has buttons over the formation button the menu would cover.
    float top=buttons[button*4+1];
    for(int i=0;i<n;++i)top=std::fmin(top,buttons[i*4+1]);
    const mapbtn::Rect at0{buttons[button*4],top,buttons[button*4+2],top+(buttons[button*4+3]-buttons[button*4+1])};
    mapbtn::Rect row[kMapFormationEntries]{};
    const int placed=mapbtn::MenuColumn(at0,count,rowW,rowH,gap,width,height,row);
    if(placed<=0){MapCommandFormationButtons(nullptr,nullptr,0);return;}
    MapUiBox(drawer,ctx,row[0].x0-4.0f*s,row[0].y0-4.0f*s,row[0].x1+4.0f*s,row[placed-1].y1+4.0f*s,lines,*at);
    float rects[kMapFormationEntries*4];
    for(int i=0;i<placed;++i) {
        const mapbtn::Rect& q=row[i];
        const int shape=mapbtn::MenuShape(entries[i]);
        const bool lit=mapbtn::MenuGuard(entries[i]) ? shape==c.formationGuardShape : shape==c.march;
        const bool hover=c.pointer && c.px>=q.x0 && c.px<q.x1 && c.py>=q.y0 && c.py<q.y1;
        Rect(drawer,ctx,q.x0,q.y0,q.x1,q.y1,lit ? kBtnLit : hover ? kMapBoxFill : kBtnFill);
        Label(text,lines,at,q.x0+pad,(q.y0+q.y1)*0.5f,0,scale,lit ? kWhite : kMapOrder,L"%ls",word[i]);
        if(*at>0)MapFitLabel(text,lines[*at-1],q.x0+pad,q.x1-pad);
        rects[i*4]=q.x0;rects[i*4+1]=q.y0;rects[i*4+2]=q.x1;rects[i*4+3]=q.y1;
    }
    MapCommandFormationButtons(rects,entries,placed);
}

// The command card as drawn this frame (MapButtons), for its formation menu (MapCommands draws it over every panel).
float mapCardRects[mapbtn::kCount*4]{};int mapCardIds[mapbtn::kCount]{};int mapCardCount=0;
int MapButtons(void* drawer,void* ctx,Text* text,float width,float height,float s,bool pad,const MapCommandReadout& c,Line* lines,
               int* at) noexcept {
    using mapbtn::Id;
    mapCardCount=0;
    if(pad){MapCommandButtons(nullptr,nullptr,0);MapCommandFormationButtons(nullptr,nullptr,0);return 0;}
    constexpr int n=mapbtn::kCount;
    static const Tx kWord[n]={Tx::orderMove,Tx::orderAttackMove,Tx::orderGuard,Tx::orderFollow,Tx::orderRelease,Tx::orderEngage,
                              Tx::orderFocus,Tx::orderBoard,Tx::orderDismount,Tx::orderDismiss,Tx::orderRecruit,Tx::orderWithdraw,
                              Tx::orderDismountAll,Tx::orderHold,Tx::orderFastMove,Tx::orderReverseMove,Tx::orderUnloadAt,
                              Tx::orderHoldFire,Tx::orderAltitude,Tx::orderSprint,Tx::orderSmoke,Tx::orderDecoy,
                              Tx::btnFormationShort,Tx::btnSweep,Tx::btnHealth,Tx::btnPace};
    // Each button's key in its tooltip: the configured one (map_keys.h, the ini's Map*Key), "-" for a button alone.
    wchar_t kKey[n][32];
    for(auto& k:kKey)wcscpy_s(k,L"-");
    const auto keyOf=[&](Id b,mapkeys::Key key){ControlKeyName(mapkeys::Of(Cfg()).vk[key],kKey[static_cast<int>(b)],32);};
    keyOf(Id::attackMove,mapkeys::attackMove);keyOf(Id::release,mapkeys::stop);keyOf(Id::withdraw,mapkeys::back);
    keyOf(Id::fastMove,mapkeys::fastMove);keyOf(Id::reverseMove,mapkeys::reverse);keyOf(Id::unloadAt,mapkeys::unload);
    keyOf(Id::holdFire,mapkeys::holdFire);keyOf(Id::altitude,mapkeys::altitude);keyOf(Id::sprint,mapkeys::sprint);
    keyOf(Id::smoke,mapkeys::smoke);keyOf(Id::decoy,mapkeys::decoy);
    {   // ALL OUT: the unload key twice
        wchar_t u[24];ControlKeyName(mapkeys::Of(Cfg()).vk[mapkeys::unload],u,24);
        _snwprintf_s(kKey[static_cast<int>(Id::dismountAll)],32,_TRUNCATE,L"%ls %ls",u,u);
    }
    const std::uint32_t orders=c.allowed && c.selected>0 ? c.allowedOrders : 0u;
    const bool tools=c.allowed && c.selectedSquads>0;
    const float scale=kLineScale*0.7f;
    int shownIds[n];float w[n];int shown=0;
    const wchar_t* word[n]{};
    for(const Id b:mapbtn::kCardOrder) {
        const int i=static_cast<int>(b);
        if(!mapbtn::Shown(b,orders,tools))continue;
        word[shown]=b==Id::sweep && c.sweepOn ? Tr(Tx::btnSweepStop) : Tr(kWord[i]);
        Line probe{};Format(probe,L"%ls",word[shown]);probe.scale=scale;
        if(text)MeasureAll(*text,&probe,1);
        const float textW=text ? probe.w : static_cast<float>(wcslen(probe.text))*9.0f*s;
        w[shown]=(2.0f*kBtnPad+kBtnIcon+6.0f)*s+textW;shownIds[shown++]=i;
    }
    mapbtn::Rect r[n]{};
    const int rows=mapbtn::Flow(w,shown,width,height-kBtnBottom*s,kBtnRowH*s,kBtnGap*s,kBtnMargin*s,r);
    if(rows>0)MapUiBox(drawer,ctx,0,height-(kBtnBottom+static_cast<float>(rows)*(kBtnRowH+kBtnGap)+8.0f)*s,width,height-104.0f*s,lines,*at);
    float rects[n*4];int placed=0,ids[n];
    for(int k=0;k<shown;++k) {
        const mapbtn::Rect& q=r[k];
        if(!(q.x1>q.x0))continue;   // no room for its row
        const Id b=static_cast<Id>(shownIds[k]);
        const bool lit=(c.armedOrder && mapbtn::IsOrder(b) && mapbtn::Arms(b) && c.armed==mapbtn::OrderOf(b)) ||
                       (b==Id::sweep && c.sweepOn) || (b==Id::health && c.healthOn) || (b==Id::holdFire && c.holdFireOn) ||
                       (b==Id::altitude && c.highOn) || (b==Id::pace && c.paced);
        const bool available=mapbtn::Available(b,orders);   // SMOKE / DECOY with none of it: greyed
        const bool hover=c.pointer && c.px>=q.x0 && c.px<q.x1 && c.py>=q.y0 && c.py<q.y1;
        Rect(drawer,ctx,q.x0,q.y0,q.x1,q.y1,lit ? kBtnLit : hover ? kMapBoxFill : kBtnFill);
        const float t=1.5f*s;
        Seg(drawer,ctx,q.x0,q.y0,q.x1,q.y0,t,kMapOrder);Seg(drawer,ctx,q.x1,q.y0,q.x1,q.y1,t,kMapOrder);
        Seg(drawer,ctx,q.x1,q.y1,q.x0,q.y1,t,kMapOrder);Seg(drawer,ctx,q.x0,q.y1,q.x0,q.y0,t,kMapOrder);
        const float iconX=q.x0+(kBtnPad+kBtnIcon*0.5f)*s,mid=(q.y0+q.y1)*0.5f;
        MapOrderIcon(drawer,ctx,b,iconX,mid,kBtnIcon*s,s,available ? kWhite : kMapLocked);
        Label(text,lines,at,q.x0+(kBtnPad+kBtnIcon+6.0f)*s,mid,0,scale,available ? kWhite : kMapLocked,L"%ls",word[k]);
        if(*at>0)MapFitLabel(text,lines[*at-1],q.x0+(kBtnPad+kBtnIcon+6.0f)*s,q.x1-kBtnPad*s);
        // The tooltip: the full word (the formation's name, the health boxes' state) and how to give it.
        wchar_t full[96];
        if(b==Id::formation)_snwprintf_s(full,_countof(full),_TRUNCATE,Tr(Tx::btnFormation),FormationText(c.march,false));
        else if(b==Id::health)_snwprintf_s(full,_countof(full),_TRUNCATE,L"%ls",Tr(c.healthOn ? Tx::btnHealthOn : Tx::btnHealthOff));
        else _snwprintf_s(full,_countof(full),_TRUNCATE,L"%ls",word[k]);
        if(b==Id::move)MapTipSet(c,q.x0,q.y0,q.x1,q.y1,Tr(Tx::tipRightClick),full);
        else if(mapbtn::Arms(b))MapTipSet(c,q.x0,q.y0,q.x1,q.y1,Tr(Tx::tipArms),full,kKey[shownIds[k]]);
        else MapTipSet(c,q.x0,q.y0,q.x1,q.y1,Tr(Tx::tipKey),full,kKey[shownIds[k]]);
        rects[placed*4]=q.x0;rects[placed*4+1]=q.y0;rects[placed*4+2]=q.x1;rects[placed*4+3]=q.y1;ids[placed++]=shownIds[k];
    }
    MapCommandButtons(rects,ids,placed);
    std::memcpy(mapCardRects,rects,sizeof(float)*4*static_cast<std::size_t>(placed));
    std::memcpy(mapCardIds,ids,sizeof(int)*static_cast<std::size_t>(placed));
    mapCardCount=placed;
    return rows;
}

// The support bar (map_buttons.h GroupSupport / Column; the user, 2026-10-09: "m里面的支援招募弄成一列带图标的hud并且可以
// 点击操作吧"): the map's left edge under the squad panel, a row a kind of support (its icon and name), its variants as
// chips at the row's right; over them its title and whether the dispatcher takes a call now (dim when it does not). A
// click on a row or chip arms that call (lit), the next left click on the map is its point; its tooltip the full name.
constexpr float kSupRowH=26.0f,kSupGap=3.0f,kSupW=250.0f,kSupIcon=18.0f;   // px at 1080 lines
constexpr float kSupPriceW=44.0f;   // px at 1080 lines: a row's price (up to five digits) at the right of its name
constexpr float kSupTabH=22.0f;     // px at 1080 lines: the tabs' row (support_call.h SupportTab)
// The composition panel's words (map_buttons.h ComposePanel): a soldier kind's short name (SupportWeapon order), a class's.
constexpr Tx kKindText[kSupportWeaponCount]={Tx::kindRifle,Tx::kindFlame,Tx::kindRocket,Tx::kindShotgun,Tx::kindSniper,
    Tx::kindLance,Tx::kindLaser,Tx::kindMonster,Tx::kindIzuna,Tx::kindThunderBow,
    Tx::kindCannon,Tx::kindMiddleCannon,Tx::kindPileBanker,Tx::kindFencerShotgun,Tx::kindXmInfantry};
constexpr Tx kClassText[mapbtn::kComposeClasses]={Tx::classRanger,Tx::classWingDiver,Tx::classFencer};
const wchar_t* KindText(SupportWeapon w) noexcept {
    const int i=static_cast<int>(w);
    return Tr(i>=0 && i<kSupportWeaponCount ? kKindText[i] : Tx::kindRifle);
}
// The armed support's composition beside the bar (the user, 2026-10-09: "支援栏是断剑那种，先点载具，然后选里面的人并且可以
// 点多次，直到座位满"; a click a whole squad): the seats used over the seats in its title, its squads in their places (each
// its kind; a click takes it out), the squad kinds a class a row (a click puts one more in; dim once no squad fits).
void MapComposePanel(void* drawer,void* ctx,Text* text,float s,const MapCommandReadout& c,const wchar_t* name,float x0,float top,
                     float ceiling,float bottom,Line* lines,int* at) noexcept {
    if(c.composeEntry<0 || c.composeSeats<=0){MapCommandComposeButtons(nullptr,nullptr,0);return;}
    const auto L=mapbtn::ComposePanel(x0,top,ceiling,bottom,c.composeSeats,s);
    MapUiBox(drawer,ctx,L.box.x0,L.box.y0,L.box.x1,L.box.y1,lines,*at);
    const auto& load=c.compose;
    const bool room=mapbtn::ComposeRoom(load,c.composeSeats);
    Label(text,lines,at,L.title.x0,(L.title.y0+L.title.y1)*0.5f,0,kLineScale*0.7f,kWhite,Tr(Tx::composeSeats),name,load.count,
          c.composeSeats<kSupportLoadoutMost ? c.composeSeats : kSupportLoadoutMost);
    if(*at>0)MapFitLabel(text,lines[*at-1],L.title.x0,L.title.x1);
    float rects[mapbtn::kComposeItems*4];int codes[mapbtn::kComposeItems];int hits=0;
    auto hit=[&](const mapbtn::Rect& q,int code){
        if(hits>=mapbtn::kComposeItems)return;
        rects[hits*4]=q.x0;rects[hits*4+1]=q.y0;rects[hits*4+2]=q.x1;rects[hits*4+3]=q.y1;codes[hits++]=code;
    };
    const float t=1.2f*s;
    auto frame=[&](const mapbtn::Rect& q,const float* edge){
        Seg(drawer,ctx,q.x0,q.y0,q.x1,q.y0,t,edge);Seg(drawer,ctx,q.x1,q.y0,q.x1,q.y1,t,edge);
        Seg(drawer,ctx,q.x1,q.y1,q.x0,q.y1,t,edge);Seg(drawer,ctx,q.x0,q.y1,q.x0,q.y0,t,edge);
    };
    const int squads=mapbtn::ComposeSquads(load);
    for(int j=0;j<L.slots;++j) {
        const mapbtn::Rect& q=L.slot[j];
        const bool taken=j<squads;
        const bool hover=taken && c.pointer && c.px>=q.x0 && c.px<q.x1 && c.py>=q.y0 && c.py<q.y1;
        Rect(drawer,ctx,q.x0,q.y0,q.x1,q.y1,hover ? kBtnLit : taken ? kBtnFill : kMapBoxFill);
        frame(q,taken ? kMapOrder : kMapOrderDim);
        if(!taken)continue;
        const int members=load.count-j*mapbtn::kSquad<mapbtn::kSquad ? load.count-j*mapbtn::kSquad : mapbtn::kSquad;
        const SupportWeapon kind=mapbtn::ComposeSquadKind(load,j);
        Label(text,lines,at,(q.x0+q.x1)*0.5f,(q.y0+q.y1)*0.5f,1,kLineScale*0.6f,kWhite,Tr(Tx::composeSquad),KindText(kind),members);
        if(*at>0)MapFitLabel(text,lines[*at-1],q.x0+2.0f*s,q.x1-2.0f*s);
        MapTipSet(c,q.x0,q.y0,q.x1,q.y1,Tr(Tx::composeSquadTip),j+1,KindText(load.soldier[j*mapbtn::kSquad]),KindText(kind),members);
        hit(q,mapbtn::ComposeSquadCode(j));
    }
    for(int k=0;k<mapbtn::kComposeClasses;++k) {
        Label(text,lines,at,L.classLabel[k].x0,(L.classLabel[k].y0+L.classLabel[k].y1)*0.5f,0,kLineScale*0.6f,kMapOrder,L"%ls",Tr(kClassText[k]));
        if(*at>0)MapFitLabel(text,lines[*at-1],L.classLabel[k].x0,L.classLabel[k].x1-2.0f*s);
    }
    for(int k=0;k<L.kinds;++k) {
        const mapbtn::Rect& q=L.kind[k];
        const auto kind=static_cast<SupportWeapon>(L.kindCode[k]);
        const bool hover=room && c.pointer && c.px>=q.x0 && c.px<q.x1 && c.py>=q.y0 && c.py<q.y1;
        Rect(drawer,ctx,q.x0,q.y0,q.x1,q.y1,hover ? kBtnLit : kBtnFill);
        frame(q,room ? kMapOrder : kMapOrderDim);
        Label(text,lines,at,(q.x0+q.x1)*0.5f,(q.y0+q.y1)*0.5f,1,kLineScale*0.6f,room ? kWhite : kMapOrderDim,L"%ls",KindText(kind));
        if(*at>0)MapFitLabel(text,lines[*at-1],q.x0+2.0f*s,q.x1-2.0f*s);
        if(room)MapTipSet(c,q.x0,q.y0,q.x1,q.y1,Tr(Tx::composeAddTip),KindText(kind),mapbtn::kSquad,c.composeSeats-load.count);
        else MapTipSet(c,q.x0,q.y0,q.x1,q.y1,Tr(Tx::composeFullTip),KindText(kind),mapbtn::kSquad);
        hit(q,L.kindCode[k]);
    }
    Label(text,lines,at,L.hint.x0,(L.hint.y0+L.hint.y1)*0.5f,0,kLineScale*0.55f,kMapOrderDim,L"%ls",Tr(Tx::composeHint));
    if(*at>0)MapFitLabel(text,lines[*at-1],L.hint.x0,L.hint.x1);
    MapCommandComposeButtons(rects,codes,hits);
}
void MapSupportBar(void* drawer,void* ctx,Text* text,float width,float s,float top,float bottom,bool pad,const MapCommandReadout& c,
                   Line* lines,int* at) noexcept {
    if(pad || c.supports<=0){MapCommandSupportButtons(nullptr,nullptr,0);return;}
    // The tab shown (support_call.h SupportTab): its entries, in catalog order (`idx`: an entry's catalog index).
    const int tab=c.supportTab>=0 && c.supportTab<kSupportTabs ? c.supportTab : 0;
    const wchar_t* names[kMapSupports];int idx[kMapSupports];int n=0;
    for(int i=0;i<c.supports;++i)if(static_cast<int>(c.support[i].tab)==tab){names[n]=c.support[i].name;idx[n++]=i;}
    mapbtn::Group groups[kMapSupports];
    const int rows=mapbtn::GroupSupport(names,n,groups,kMapSupports);
    const float x0=8.0f*s,x1=std::fmin(kSupW*s,width*0.3f),titleH=24.0f*s;
    const float poolH=18.0f*s,tabH=kSupTabH*s;   // the pool's line, then the tabs, under the title
    const float tabsTop=top+titleH+poolH,rowsTop=tabsTop+tabH+kSupGap*s;
    mapbtn::Rect row[kMapSupports]{},chip[kMapSupports]{};
    const int placed=rows>0 ? mapbtn::Column(groups,rows,x0+4.0f*s,x1-4.0f*s,rowsTop,bottom,kSupRowH*s,kSupGap*s,4.0f*s,row,chip) : 0;
    if(rowsTop>bottom){MapCommandSupportButtons(nullptr,nullptr,0);return;}
    const float last=placed>0 ? row[placed-1].y1 : rowsTop;
    MapUiBox(drawer,ctx,x0,top,x1,last+4.0f*s,lines,*at);
    const bool ready=c.supportReady.state==SupportReady::ready;
    const float* tint=ready ? kWhite : kMapOrderDim;
    Label(text,lines,at,x0+8.0f*s,top+titleH*0.5f,0,kLineScale*0.75f,kMapOrder,L"%ls",Tr(Tx::supportTitle));
    // The pool (support_points.h): what the calls are paid from, on its own line under the title.
    Label(text,lines,at,x0+8.0f*s,top+titleH+poolH*0.4f,0,kLineScale*0.6f,kAmber,Tr(Tx::supportPt),c.points,c.pointsMost);
    if(!ready) {
        const SupportReadiness& r=c.supportReady;
        if(r.state==SupportReady::cooldown)Label(text,lines,at,x1-8.0f*s,top+titleH*0.5f,2,kLineScale*0.65f,kAmber,Tr(Tx::supportCooldown),r.seconds);
        else Label(text,lines,at,x1-8.0f*s,top+titleH*0.5f,2,kLineScale*0.65f,kAmber,L"%ls",
                   Tr(r.state==SupportReady::planning ? Tx::supportPlanning : Tx::supportOff));
    }
    float rects[kMapSupportHits*4];int entries[kMapSupportHits];int hits=0;
    auto hit=[&](const mapbtn::Rect& q,int entry){
        if(hits>=kMapSupportHits)return;
        rects[hits*4]=q.x0;rects[hits*4+1]=q.y0;rects[hits*4+2]=q.x1;rects[hits*4+3]=q.y1;entries[hits++]=entry;
    };
    // The tabs, side by side across the bar: the one shown lit; a click shows another (map_buttons.h SupportTabHit).
    static constexpr Tx kTabText[kSupportTabs]={Tx::supportTabAir,Tx::supportTabSortie,Tx::supportTabStrike,Tx::supportTabGround};
    const float tabW=(x1-x0-8.0f*s-static_cast<float>(kSupportTabs-1)*kSupGap*s)/static_cast<float>(kSupportTabs);
    for(int t=0;t<kSupportTabs;++t) {
        const float tx0=x0+4.0f*s+static_cast<float>(t)*(tabW+kSupGap*s);
        const mapbtn::Rect q{tx0,tabsTop,tx0+tabW,tabsTop+tabH};
        const bool lit=t==tab,hover=c.pointer && c.px>=q.x0 && c.px<q.x1 && c.py>=q.y0 && c.py<q.y1;
        Rect(drawer,ctx,q.x0,q.y0,q.x1,q.y1,lit ? kBtnLit : hover ? kMapBoxFill : kBtnFill);
        {const float w=1.2f*s;const float* edge=lit ? kWhite : kMapOrderDim;
         Seg(drawer,ctx,q.x0,q.y0,q.x1,q.y0,w,edge);Seg(drawer,ctx,q.x1,q.y0,q.x1,q.y1,w,edge);
         Seg(drawer,ctx,q.x1,q.y1,q.x0,q.y1,w,edge);Seg(drawer,ctx,q.x0,q.y1,q.x0,q.y0,w,edge);}
        Label(text,lines,at,(q.x0+q.x1)*0.5f,(q.y0+q.y1)*0.5f,1,kLineScale*0.6f,lit ? kWhite : kMapOrder,L"%ls",Tr(kTabText[t]));
        if(*at>0)MapFitLabel(text,lines[*at-1],q.x0+2.0f*s,q.x1-2.0f*s);
        hit(q,mapbtn::SupportTabHit(t));
    }
    // An entry the mission forbids, or one the pool cannot pay for now: dimmed (a click on it still says why).
    auto entryTint=[&](int e){ return c.support[e].allowed && c.support[e].afford ? tint : kMapOrderDim; };
    // The rows under their chips (drawn first), the chips over them; the hit list the chips first: a click on a chip is
    // that variant, not the row's. `g.first` and the chips are the tab's own positions; idx[] their catalog entries.
    mapbtn::Rect body[kMapSupports]{};int pickOf[kMapSupports]{};
    for(int k=0;k<placed;++k) {
        const mapbtn::Group& g=groups[k];
        const mapbtn::Rect& q=row[k];
        bool lit=false;
        for(int e=g.first;e<g.first+g.count;++e)lit=lit || c.supportArmed==idx[e];
        const bool hover=c.pointer && c.px>=q.x0 && c.px<q.x1 && c.py>=q.y0 && c.py<q.y1;
        if(lit || hover)Rect(drawer,ctx,q.x0,q.y0,q.x1,q.y1,lit ? kBtnLit : kMapBoxFill);
        const float mid=(q.y0+q.y1)*0.5f;
        // The row itself (left of its chips): its first variant, or the one [ ] picked last within it.
        pickOf[k]=idx[g.first];
        for(int e=g.first;e<g.first+g.count;++e)if(c.supportPick==idx[e])pickOf[k]=idx[e];
        const int pick=pickOf[k];
        const float* rowTint=entryTint(pick);
        MapSupportIcon(drawer,ctx,c.support[idx[g.first]].icon,q.x0+(4.0f+kSupIcon*0.5f)*s,mid,kSupIcon*s,s,rowTint);
        wchar_t base[40]{};   // the name before its "·" (map_buttons.h BaseLength)
        const int most=static_cast<int>(_countof(base))-1,len=mapbtn::BaseLength(names[g.first])<most ? mapbtn::BaseLength(names[g.first]) : most;
        std::wmemcpy(base,names[g.first],static_cast<std::size_t>(len));
        const float chipsLeft=g.count>1 ? chip[g.first].x0-4.0f*s : q.x1-4.0f*s;
        // Its price (the picked variant's) at the right of its words: amber when the pool holds it, red when not.
        Label(text,lines,at,chipsLeft,mid,2,kLineScale*0.6f,c.support[pick].afford ? kAmber : kRed,L"%d",c.support[pick].price);
        const float textRight=chipsLeft-kSupPriceW*s;
        Label(text,lines,at,q.x0+(8.0f+kSupIcon)*s,mid,0,kLineScale*0.7f,rowTint,L"%ls",base);
        if(*at>0)MapFitLabel(text,lines[*at-1],q.x0+(8.0f+kSupIcon)*s,textRight);
        body[k]=mapbtn::Rect{q.x0,q.y0,chipsLeft,q.y1};
        MapTipSet(c,body[k].x0,body[k].y0,body[k].x1,body[k].y1,Tr(Tx::supportTip),c.support[pick].name,c.support[pick].price);
    }
    for(int k=0;k<placed;++k) {
        const mapbtn::Group& g=groups[k];
        if(g.count<=1)continue;
        for(int e=g.first;e<g.first+g.count;++e) {
            const mapbtn::Rect& q=chip[e];
            const int entry=idx[e];
            const bool lit=c.supportArmed==entry;
            Rect(drawer,ctx,q.x0,q.y0,q.x1,q.y1,lit ? kBtnLit : kBtnFill);
            const float t=1.2f*s;const float* edge=lit ? kWhite : kMapOrderDim;
            Seg(drawer,ctx,q.x0,q.y0,q.x1,q.y0,t,edge);Seg(drawer,ctx,q.x1,q.y0,q.x1,q.y1,t,edge);
            Seg(drawer,ctx,q.x1,q.y1,q.x0,q.y1,t,edge);Seg(drawer,ctx,q.x0,q.y1,q.x0,q.y0,t,edge);
            MapVariantIcon(drawer,ctx,c.support[entry].variant,(q.x0+q.x1)*0.5f,(q.y0+q.y1)*0.5f,(q.y1-q.y0)*0.8f,s,entryTint(entry));
            MapTipSet(c,q.x0,q.y0,q.x1,q.y1,Tr(Tx::supportTip),c.support[entry].name,c.support[entry].price);
            hit(q,entry);
        }
    }
    for(int k=0;k<placed;++k)hit(body[k],pickOf[k]);
    MapCommandSupportButtons(rects,entries,hits);
    // The armed support's composition beside its row (its name before the "·").
    int composeRow=-1;
    for(int k=0;k<placed && composeRow<0;++k)
        for(int e=groups[k].first;e<groups[k].first+groups[k].count;++e)if(c.composeEntry==idx[e])composeRow=k;
    if(composeRow<0){MapCommandComposeButtons(nullptr,nullptr,0);return;}
    wchar_t base[40]{};
    const int most=static_cast<int>(_countof(base))-1,blen=mapbtn::BaseLength(c.support[c.composeEntry].name);
    std::wmemcpy(base,c.support[c.composeEntry].name,static_cast<std::size_t>(blen<most ? blen : most));
    MapComposePanel(drawer,ctx,text,s,c,base,x1+6.0f*s,row[composeRow].y0,top,bottom,lines,at);
}

const wchar_t* EnemyAbilityState(const enemy_control::Ability& ability,wchar_t* out,std::size_t size) noexcept {
    using State=enemy_control::AbilityState;
    if(ability.state==State::cooldown && ability.cooldownKnown) {
        _snwprintf_s(out,size,_TRUNCATE,Tr(Tx::enemyControlCooldown),static_cast<double>(ability.cooldownSeconds));return out;
    }
    return Tr(ability.state==State::active ? Tx::enemyControlRunning : ability.state==State::unavailable ?
        Tx::enemyControlUnavailable : Tx::enemyControlNativeReady);
}
const wchar_t* EnemyHealthText(const enemy_control::Snapshot& snapshot,wchar_t* out,std::size_t size) noexcept {
    if(!enemyui::HealthAvailable(snapshot.healthKnown,snapshot.health,snapshot.maxHealth))return Tr(Tx::enemyControlHealthUnavailable);
    _snwprintf_s(out,size,_TRUNCATE,Tr(Tx::enemyControlHealth),static_cast<double>(snapshot.health),static_cast<double>(snapshot.maxHealth));
    return out;
}
// The key of the controlled enemy's attack `index` (enemy_control_bindings.h): the pad's
// button with a pad, else the keyboard/mouse key; past the last key the M card's row only.
const wchar_t* EnemyKeyLabel(unsigned index,bool pad,wchar_t* out,std::size_t size) noexcept {
    const auto* binding=enemy_control::BindingOf(index);
    if(!binding || (pad && !binding->pad))return Tr(Tx::enemyKeyCardOnly);
    if(pad) {
        struct Name { std::uint32_t bit;const wchar_t* text; };
        static constexpr Name names[]={{enemy_control::kPadRightTrigger,L"RT"},{enemy_control::kPadLeftTrigger,L"LT"},{0x0200,L"RB"},
            {0x0100,L"LB"},{0x1000,L"A"},{0x2000,L"B"},{0x4000,L"X"},{0x8000,L"Y"}};
        for(const auto& n:names)if(n.bit==binding->pad)return n.text;
        if(binding->pad==0x0001)return Tr(Tx::enemyKeyDpadUp);
        if(binding->pad==0x0008)return Tr(Tx::enemyKeyDpadRight);
        if(binding->pad==0x0002)return Tr(Tx::enemyKeyDpadDown);
        if(binding->pad==0x0004)return Tr(Tx::enemyKeyDpadLeft);
        return Tr(Tx::enemyKeyCardOnly);
    }
    switch(binding->vk) {
        case 0x01:return Tr(Tx::enemyKeyMouseLeft);
        case 0x02:return Tr(Tx::enemyKeyMouseRight);
        case 0x05:return Tr(Tx::enemyKeyMouse4);
        case 0x06:return Tr(Tx::enemyKeyMouse5);
        default:_snwprintf_s(out,size,_TRUNCATE,L"%lc",static_cast<wchar_t>(binding->vk));return out;
    }
}
float MapEnemyPanel(void* drawer,void* ctx,Text* text,float width,float s,float top,float bottom,bool pad,
                    const MapCommandReadout& c,Line* lines,int* at) noexcept {
    MapEnemyControlReadout read{};
    if(!ReadMapEnemyControl(&read) || !read.visible || *at+18>kMaxLines) {
        MapCommandEnemyButtons(nullptr,nullptr,0,nullptr);return top;
    }
    const auto& snapshot=read.snapshot;
    using Phase=enemy_control::Phase;
    const bool active=snapshot.phase==Phase::active,pending=snapshot.phase==Phase::pending || snapshot.phase==Phase::restoring;
    const bool candidate=snapshot.name[0]!=L'\0';
    const float x1=width-12.0f*s,x0=x1-std::fmin(420.0f*s,width*0.32f);
    // Folded or unfolded only by the user (its title, a pick, L3): a hover never changes the
    // card under the pointer on its way to a button.
    const bool engaged=active || pending;
    const bool open=engaged || read.focused || read.expanded;
    const int rows=active ? read.count : 0;
    const float natural=active ? (snapshot.sourceProtected ? 176.0f : 158.0f)+24.0f*static_cast<float>(rows) : open ? 108.0f : 28.0f;
    if(bottom-(engaged ? 54.0f*s : top)<(open ? 90.0f : 28.0f)*s) {
        MapCommandEnemyButtons(nullptr,nullptr,0,nullptr);return top;
    }
    // Active control takes precedence over lower-priority map panels. Move the
    // complete card up when necessary, then fit measured and final text together.
    if(engaged)top=std::fmax(54.0f*s,std::fmin(top,bottom-natural*s));
    const float z=std::fmin(s,(bottom-top)/natural);
    HudRegionText region(text,s,z,lines,at);text=region.text;
    const float titleH=26.0f*z,rowH=24.0f*z,left=x0+8.0f*z,right=x1-8.0f*z;
    const float panelBottom=top+natural*z;
    MapUiBox(drawer,ctx,x0,top,x1,panelBottom,lines,*at);
    float rects[kMapEnemyItems*4]{};int codes[kMapEnemyItems]{};int hits=0;
    const auto hitbox=[&](float a,float b,float c0,float d,int code) {
        if(hits<kMapEnemyItems){float* q=rects+4*hits;q[0]=a;q[1]=b;q[2]=c0;q[3]=d;codes[hits++]=code;}
    };
    // The title folds/unfolds the card while nothing is controlled (engaged cards stay open).
    if(!engaged) {
        const bool overTitle=!pad && c.pointer && c.px>=x0 && c.px<x1 && c.py>=top && c.py<top+titleH;
        if(overTitle)Rect(drawer,ctx,x0,top,x1,top+titleH,kMapBoxFill);
        hitbox(x0,top,x1,top+titleH,enemyui::kFold);
    }
    Label(text,lines,at,left,top+titleH*0.5f,0,kLineScale*0.68f,kCyan,L"%ls",Tr(Tx::enemyControlTitle));
    if(*at>0)MapFitLabel(text,lines[*at-1],left,right-110.0f*z);
    Label(text,lines,at,right,top+titleH*0.5f,2,kLineScale*0.5f,kMapOrderDim,L"%ls",
        Tr(engaged ? Tx::enemyControlToggle : open ? Tx::enemyControlFoldOpen : Tx::enemyControlFoldClosed));
    if(!open) {
        MapTipSet(c,x0,top,x1,panelBottom,L"%ls",candidate ? snapshot.name : Tr(Tx::enemyControlEmpty));
        MapCommandEnemyButtons(rects,codes,hits,&snapshot);return panelBottom;
    }
    float y=top+titleH;
    if(!engaged) {
        // The target row: < name > steps through the nearest controllable enemies.
        const float arrow=30.0f*z;
        for(int direction:{-1,1}) {
            const float a=direction<0 ? left : right-arrow,b=a+arrow;
            const bool over=!pad && c.pointer && c.px>=a && c.px<b && c.py>=y && c.py<y+20.0f*z;
            Rect(drawer,ctx,a,y,b,y+20.0f*z,over ? kBtnLit : kBtnFill);
            Label(text,lines,at,(a+b)*0.5f,y+10.0f*z,1,kLineScale*0.55f,kWhite,L"%ls",direction<0 ? L"<" : L">");
            hitbox(a,y,b,y+20.0f*z,direction<0 ? enemyui::kTargetPrev : enemyui::kTargetNext);
            MapTipSet(c,a,y,b,y+20.0f*z,L"%ls",Tr(Tx::enemyControlCycleTip));
        }
        Label(text,lines,at,(left+right)*0.5f,y+10.0f*z,1,kLineScale*0.6f,candidate ? kWhite : kAmber,L"%ls",candidate ? snapshot.name : Tr(Tx::enemyControlEmpty));
        if(*at>0) {
            Line& row=lines[*at-1];MapFitLabel(text,row,left+arrow+4.0f*z,right-arrow-4.0f*z);
            row.x=(left+right-row.w)*0.5f;
        }
    } else {
        Label(text,lines,at,left,y+10.0f*z,0,kLineScale*0.6f,active ? kWhite : kAmber,L"%ls",candidate ? snapshot.name : Tr(Tx::enemyControlEmpty));
        if(*at>0)MapFitLabel(text,lines[*at-1],left,right);
    }
    y+=22.0f*z;
    if(active) {
        wchar_t health[128]{};
        Label(text,lines,at,left,y+10.0f*z,0,kLineScale*0.6f,kWhite,L"%ls",EnemyHealthText(snapshot,health,_countof(health)));
        if(*at>0)MapFitLabel(text,lines[*at-1],left,right);
        y+=22.0f*z;
    }
    const auto button=[&](int code,int row,const wchar_t* label,bool allowed,bool selected) {
        const bool hover=!pad && c.pointer && c.px>=left && c.px<right && c.py>=y && c.py<y+rowH-2.0f*z;
        const bool focus=pad && read.focused && read.padRow==row;
        Rect(drawer,ctx,left,y,right,y+rowH-2.0f*z,focus || (hover && allowed) ? kBtnLit : kBtnFill);
        if(selected)Rect(drawer,ctx,left,y+4.0f*z,left+3.0f*z,y+rowH-6.0f*z,kMapOrder);
        Label(text,lines,at,left+8.0f*z,y+rowH*0.46f,0,kLineScale*0.6f,allowed ? kWhite : kMapOrderDim,L"%ls",label);
        if(*at>0)MapFitLabel(text,lines[*at-1],left+8.0f*z,right-4.0f*z);
        if(allowed)hitbox(left,y,right,y+rowH-2.0f*z,code);
        y+=rowH;
    };
    if(active) {
        // Key -> attack: each attack's own key (enemy_control_bindings.h); holding a row uses it too.
        for(int i=0;i<rows;++i) {
            const auto& ability=read.ability[i];wchar_t state[96]{},label[224]{},key[32]{};
            _snwprintf_s(label,_countof(label),_TRUNCATE,L"[%ls]  %ls  /  %ls",EnemyKeyLabel(static_cast<unsigned>(read.first+i),pad,key,_countof(key)),
                ability.name,EnemyAbilityState(ability,state,_countof(state)));
            button(read.first+i,i,label,true,false);
            MapTipSet(c,left,y-rowH,right,y,L"%ls\n%ls",ability.name,Tr(Tx::enemyControlRowTip));
        }
        const float half=(left+right)*0.5f;
        Label(text,lines,at,half,y+10.0f*z,1,kLineScale*0.5f,kTeal,Tr(Tx::enemyControlPage),
            static_cast<unsigned>(read.first+1),static_cast<unsigned>(read.first+rows),snapshot.abilityCount);
        const float pageWidth=36.0f*z;
        for(int direction:{-1,1}) {
            const float a=direction<0 ? left : right-pageWidth,b=a+pageWidth;
            Rect(drawer,ctx,a,y,b,y+20.0f*z,kBtnFill);
            Label(text,lines,at,(a+b)*0.5f,y+10.0f*z,1,kLineScale*0.55f,kWhite,L"%ls",direction<0 ? L"<" : L">");
            if(snapshot.abilityCount>enemyui::kPageRows)hitbox(a,y,b,y+20.0f*z,direction<0 ? enemyui::kPrev : enemyui::kNext);
        }
        y+=22.0f*z;
        button(enemyui::kExit,rows,Tr(Tx::enemyControlReturn),true,false);
    } else {
        const wchar_t* reason=pending ? Tr(snapshot.phase==Phase::restoring ? Tx::enemyControlRestoring : Tx::enemyControlPending) :
            candidate ? (snapshot.reason==enemy_control::Reason::networkCapacity ? Tr(Tx::enemyControlCapacity) : enemy_control::ReasonText(snapshot.reason)) : Tr(Tx::enemyControlEmpty);
        Label(text,lines,at,left,y+9.0f*z,0,kLineScale*0.52f,kAmber,L"%ls",reason);
        if(*at>0)MapFitLabel(text,lines[*at-1],left,right);
        MapTipSet(c,left,y,right,y+20.0f*z,L"%ls",reason);y+=20.0f*z;
        button(pending ? enemyui::kExit : enemyui::kBegin,0,Tr(pending ? Tx::enemyControlReturn : Tx::enemyControlBegin),
            pending || (candidate && snapshot.reason==enemy_control::Reason::none),false);
    }
    if(active) {
        Label(text,lines,at,left,panelBottom-(snapshot.sourceProtected ? 46.0f : 28.0f)*z,0,kLineScale*0.5f,kAmber,
            Tr(snapshot.followsOwnerTeam ? Tx::enemyControlTeamOwner : Tx::enemyControlTeamOriginal),snapshot.controlledTeam);
        if(*at>0)MapFitLabel(text,lines[*at-1],left,right);
    }
    if(active && snapshot.sourceProtected) {
        Label(text,lines,at,left,panelBottom-28.0f*z,0,kLineScale*0.5f,kCyan,L"%ls",Tr(Tx::enemyControlSourceSafe));
        if(*at>0)MapFitLabel(text,lines[*at-1],left,right);
    }
    Label(text,lines,at,left,panelBottom-10.0f*z,0,kLineScale*0.48f,kTeal,L"%ls",
        Tr(active ? (pad ? Tx::enemyControlPadHint : Tx::enemyControlMouseHint) : pad ? Tx::enemyControlPadPickHint : Tx::enemyControlPickHint));
    if(*at>0)MapFitLabel(text,lines[*at-1],left,right);
    MapCommandEnemyButtons(rects,codes,hits,&snapshot);
    return panelBottom;
}

// PVP shares the right-hand stack with loadout and placement. Hover or R3 focus expands its tab.
// Hitboxes carry the same status snapshot, so a click cannot target a replacement world/revision.
Tx MapPvpResult(pvp_protocol::Result result) noexcept {
    using R=pvp_protocol::Result;
    switch(result) {
    case R::Idle: return Tx::pvpReady;
    case R::Pending: return Tx::pvpPending;
    case R::Complete: return Tx::pvpComplete;
    case R::NotReady: return Tx::pvpNotReady;
    case R::Busy: return Tx::pvpBusy;
    case R::InvalidArgument: return Tx::pvpInvalid;
    case R::UnsupportedRoster: return Tx::pvpRoster;
    case R::ProtocolError: return Tx::pvpChanged;
    case R::PrepareFailed: return Tx::pvpPrepareFailed;
    case R::ApplyFailed: return Tx::pvpApplyFailed;
    case R::SendFailed: return Tx::pvpSendFailed;
    case R::TimedOut: return Tx::pvpTimedOut;
    case R::Aborted: return Tx::pvpAborted;
    case R::SessionLost: return Tx::pvpSessionLost;
    case R::Faulted: return Tx::pvpFaulted;
    }
    return Tx::pvpNotReady;
}
// A faction's and a preset's words, a faction's chip colour (pvp_match.h kFactions' ARGB).
Tx PvpFactionName(int faction) noexcept {
    constexpr Tx kNames[]={Tx::pvpFactionEdf,Tx::pvpFactionEdf2,Tx::pvpFactionEdf3,Tx::pvpFactionEdf4,
                           Tx::pvpFactionMonster,Tx::pvpFactionPrimer,Tx::pvpFactionForeigner};
    static_assert(sizeof(kNames)/sizeof(kNames[0])==pvp_match::kFactionCount,"a name a faction");
    return pvp_match::FactionValid(faction) ? kNames[faction] : Tx::pvpFactionEdf;
}
Tx PvpPresetName(int preset) noexcept {
    constexpr Tx kNames[]={Tx::pvpPresetFfa,Tx::pvpPresetHva,Tx::pvpPresetCivil,Tx::pvpPresetAliens,Tx::pvpPresetCustom};
    static_assert(sizeof(kNames)/sizeof(kNames[0])==pvp_match::kPresetCount,"a name a preset");
    return preset>=0 && preset<pvp_match::kPresetCount ? kNames[preset] : Tx::pvpPresetFfa;
}
void PvpFactionColor(int faction,float* rgba) noexcept {
    const std::uint32_t argb=pvp_match::FactionValid(faction) ? pvp_match::kFactions[faction].color : 0xFFFFFFFFu;
    rgba[0]=static_cast<float>((argb>>16)&0xFF)/255.0f;rgba[1]=static_cast<float>((argb>>8)&0xFF)/255.0f;
    rgba[2]=static_cast<float>(argb&0xFF)/255.0f;rgba[3]=static_cast<float>(argb>>24)/255.0f;
}
float MapPvpPanel(void* drawer,void* ctx,Text* text,float width,float s,float top,float bottom,bool pad,
                  const MapCommandReadout& c,Line* lines,int* at) noexcept {
    PvpStatus status{};ReadPvpStatus(&status);
    // The host's two rows full width, then the factions two to a row (the dock's panels below keep their room).
    constexpr int kButtons=pvpmenu::kItems,kFactionRows=(pvp_match::kFactionCount+1)/2,kRows=pvpmenu::kFaction0+kFactionRows;
    if(*at+kButtons+4>kMaxLines || top+28.0f*s>bottom){MapCommandPvpButtons(nullptr,nullptr,0,nullptr);return top;}
    const float x1=width-12.0f*s,x0=x1-std::fmin(420.0f*s,width*0.32f),titleH=28.0f*s,rowH=24.0f*s,footH=19.0f*s;
    const float expandedBottom=top+titleH+static_cast<float>(kRows)*rowH+2.0f*footH+8.0f*s;
    const int padPick=pad ? MapPvpPadSelection() : -1;
    const bool pointer=!pad && c.pointer;
    const bool overTitle=pointer && c.px>=x0 && c.px<x1 && c.py>=top && c.py<top+titleH;
    // Opened and folded by a click on its title (kPvpFold) or R3's focus, never by the pointer passing over it: the
    // panels under it in the dock stay where they are while the pointer moves (MapDock).
    const bool open=(MapPvpOpen() || padPick>=0) && expandedBottom<=bottom;
    const float panelBottom=open ? expandedBottom : top+titleH;
    MapUiBox(drawer,ctx,x0,top,x1,panelBottom,lines,*at);
    Rect(drawer,ctx,x0,top,x1,top+titleH,overTitle ? kBtnLit : kBtnFill);
    const float* stateTint=!status.ready || status.pending ? kAmber : status.enabled ? kWhite : kMapOrderDim;
    Label(text,lines,at,x0+8.0f*s,top+titleH*0.5f,0,kLineScale*0.7f,stateTint,
          Tr(Tx::pvpTitleState),Tr(status.enabled ? Tx::pvpOn : Tx::pvpOff),Tr(PvpFactionName(status.local.faction)));
    const float hintLeft=x1-108.0f*s;
    if(*at>0)MapFitLabel(text,lines[*at-1],x0+8.0f*s,hintLeft-8.0f*s);
    Label(text,lines,at,x1-22.0f*s,top+titleH*0.5f,2,kLineScale*0.55f,kMapOrderDim,L"%ls",
          Tr(pad ? Tx::pvpFocusPad : Tx::pvpExpand));
    if(*at>0)MapFitLabel(text,lines[*at-1],hintLeft,x1-22.0f*s);
    Tri(drawer,ctx,x1-11.0f*s,top+titleH*0.5f+(open ? 3.0f : -3.0f)*s,
        x1-11.0f*s,top+titleH*0.5f+(open ? -4.0f : 4.0f)*s,4.0f*s,kMapOrder);
    MapTipSet(c,x0,top,x1,top+titleH,L"%ls",Tr(Tx::pvpMouseHint));
    float rects[kMapPvpItems*4]{};int codes[kMapPvpItems]{};int hits=0;
    if(pointer){float* q=rects;q[0]=x0;q[1]=top;q[2]=x1;q[3]=top+titleH;codes[hits++]=kPvpFold;}
    if(!open){MapCommandPvpButtons(rects,codes,hits,&status);return panelBottom;}
    const float left=x0+8.0f*s,right=x1-8.0f*s,half=(right-left-4.0f*s)*0.5f;
    const float rowsTop=top+titleH;
    for(int code=0;code<kButtons;++code) {
        const int faction=pvpmenu::CodeFaction(code);
        // Its cell: a host row across the panel, a faction in its row's left or right half.
        const int slot=faction<0 ? code : pvpmenu::kFaction0+faction/2;
        const float y=rowsTop+static_cast<float>(slot)*rowH;
        const float cellLeft=faction<0 || faction%2==0 ? left : left+half+4.0f*s;
        const float cellRight=faction<0 ? right : faction%2==0 ? left+half : right;
        const bool allowed=pvpmenu::Allowed(status,code);
        const bool selected=code==kPvpModeToggle ? status.enabled : faction>=0 && status.local.faction==faction;
        const bool hover=pointer && c.px>=cellLeft && c.px<cellRight && c.py>=y && c.py<y+rowH-3.0f*s;
        const bool focus=padPick==code;
        Rect(drawer,ctx,cellLeft,y,cellRight,y+rowH-3.0f*s,(allowed && hover) || focus ? kBtnLit : kBtnFill);
        if(selected)Rect(drawer,ctx,cellLeft,y+4.0f*s,cellLeft+3.0f*s,y+rowH-7.0f*s,kMapOrder);
        const float* tint=allowed ? kWhite : kMapOrderDim;
        if(code==kPvpModeToggle)
            Label(text,lines,at,left+10.0f*s,y+(rowH-3.0f*s)*0.5f,0,kLineScale*0.64f,tint,L"%ls",
                  Tr(status.enabled ? Tx::pvpDisable : Tx::pvpEnable));
        else if(code==kPvpPreset)
            Label(text,lines,at,left+10.0f*s,y+(rowH-3.0f*s)*0.5f,0,kLineScale*0.64f,tint,Tr(Tx::pvpPresetRow),
                  Tr(PvpPresetName(status.preset)));
        else {
            float chip[4];PvpFactionColor(faction,chip);
            Rect(drawer,ctx,cellRight-14.0f*s,y+5.0f*s,cellRight-4.0f*s,y+rowH-8.0f*s,chip);
            Label(text,lines,at,cellLeft+10.0f*s,y+(rowH-3.0f*s)*0.5f,0,kLineScale*0.64f,tint,L"%ls",Tr(PvpFactionName(faction)));
            MapTipSet(c,cellLeft,y,cellRight,y+rowH,Tr(Tx::pvpJoinFaction),Tr(PvpFactionName(faction)));
        }
        if(*at>0)MapFitLabel(text,lines[*at-1],cellLeft+10.0f*s,cellRight-18.0f*s);
        if(code==kPvpModeToggle || code==kPvpPreset)MapTipSet(c,left,y,right,y+rowH,L"%ls",Tr(Tx::pvpHostOnly));
        if(allowed){float* q=rects+4*hits;q[0]=cellLeft;q[1]=y;q[2]=cellRight;q[3]=y+rowH-3.0f*s;codes[hits++]=code;}
    }
    float y=rowsTop+static_cast<float>(kRows)*rowH;
    const Tx result=status.pending ? Tx::pvpPending : !status.ready ? Tx::pvpNotReady : MapPvpResult(status.result);
    Label(text,lines,at,left,y+footH*0.5f,0,kLineScale*0.56f,stateTint,L"%ls",Tr(result));
    if(*at>0)MapFitLabel(text,lines[*at-1],left,right);
    const wchar_t* details=hudtext::InUse()==hudtext::Lang::zhCN && status.note[0] ? status.note : Tr(result);
    MapTipSet(c,left,y,right,y+footH,L"%ls",details);
    y+=footH;
    Label(text,lines,at,left,y+footH*0.5f,0,kLineScale*0.55f,kTeal,L"%ls",Tr(pad ? Tx::pvpPadHint : Tx::pvpMouseHint));
    if(*at>0)MapFitLabel(text,lines[*at-1],left,right);
    MapCommandPvpButtons(rects,codes,hits,&status);
    return panelBottom;
}

// A framed button's edge (the card's look).
void MapFrame(void* drawer,void* ctx,float x0,float y0,float x1,float y1,float s,const float* edge) noexcept {
    const float t=1.2f*s;
    Seg(drawer,ctx,x0,y0,x1,y0,t,edge);Seg(drawer,ctx,x1,y0,x1,y1,t,edge);
    Seg(drawer,ctx,x1,y1,x0,y1,t,edge);Seg(drawer,ctx,x0,y1,x0,y0,t,edge);
}

// The legend at the dock's foot: its title row (a click opens / folds it; never the pointer passing over it), the whole
// legend in three columns while open (the order paths' rows with it, no taller than the two columns it had before:
// its top stays under the map scale's box, drawn after it), beside the dock over the map (its bottom level with the title's), so opening it
// neither covers nor squeezes the dock's controls. The title's top: the dock's other panels end above it.
float MapLegend(void* drawer,void* ctx,Text* text,float width,float s,float bottom,bool pad,const MapCommandReadout& c,
                Line* lines,int* at) noexcept {
    struct Entry { MapKind kind; std::uint8_t flags; Tx name; };
    static const Entry kLegend[]={{MapKind::squad,0,Tx::legendSquad},{MapKind::ally,0,Tx::legendFriendly},{MapKind::vehicle,0,Tx::legendVehicle},
                                  {MapKind::air,0,Tx::legendAircraft},{MapKind::air,kMapRotor,Tx::legendHelicopter},
                                  {MapKind::vehicle,kMapEmpty,Tx::legendEmpty},{MapKind::carrier,0,Tx::legendCarrier},
                                  {MapKind::enemy,kMapLarge,Tx::legendLargeEnemy},{MapKind::enemyAir,kMapLarge,Tx::legendLargeEnemyAir},
                                  {MapKind::marker,0,Tx::legendObjective}};
    // The order paths' colours (MapPathColour) after them, a row each.
    static const Order kPaths[]={Order::move,Order::attackMove,Order::fastMove,Order::reverseMove,Order::unloadAt,Order::guard};
    constexpr int kPathRows=static_cast<int>(sizeof(kPaths)/sizeof(kPaths[0]));
    constexpr int kItems=static_cast<int>(sizeof(kLegend)/sizeof(kLegend[0]))+5+kPathRows;   // the player, the dots, the brackets, the paths
    constexpr int kCols=3,kRows=(kItems+kCols-1)/kCols;
    const MapDock dock=MapDockAt(width,s);
    const float titleH=28.0f*s,rowH=22.0f*s,y1=bottom,y0=y1-titleH;
    if(y0<60.0f*s || *at+kItems+2>kMaxLines){MapCommandLegendButton(nullptr);return bottom;}
    const bool open=MapLegendOpen();
    const float listW=std::fmin(540.0f*s,dock.x0-24.0f*s),lx1=dock.x0-8.0f*s,lx0=lx1-listW;
    const float listH=static_cast<float>(kRows)*rowH+10.0f*s;
    // Level with the title, or as low over it as clears every panel drawn so far (the support composition, the payload
    // panel): it covers the map only.
    float listBottom=y1;
    for(int tries=0;tries<kMapUiPanels;++tries) {
        int blocker=-1;
        for(int i=0;i<mapUiPanelCount && blocker<0;++i) {
            const float* r=mapUiPanels+4*i;
            if(r[0]<lx1 && lx0<r[2] && r[1]<listBottom && listBottom-listH<r[3])blocker=i;
        }
        if(blocker<0)break;
        listBottom=mapUiPanels[4*blocker+1]-6.0f*s;
        if(listBottom-listH<60.0f*s){listBottom=y1;break;}   // nowhere clear: over the title after all
    }
    const float listTop=listBottom-listH;
    const bool shown=open && listTop>60.0f*s && listW>200.0f*s;
    MapUiBox(drawer,ctx,dock.x0,y0,dock.x1,y1,lines,*at);
    const bool hover=!pad && c.pointer && c.px>=dock.x0 && c.px<dock.x1 && c.py>=y0 && c.py<y1;
    Rect(drawer,ctx,dock.x0,y0,dock.x1,y1,hover ? kBtnLit : kBtnFill);
    MapFrame(drawer,ctx,dock.x0,y0,dock.x1,y1,s,kMapOrderDim);
    const float mid=(y0+y1)*0.5f;
    Label(text,lines,at,dock.x0+10.0f*s,mid,0,kLineScale*0.7f,open ? kWhite : kMapOrder,L"%ls",Tr(Tx::legendTitle));
    Tri(drawer,ctx,dock.x1-15.0f*s,mid+(open ? 3.0f : -3.0f)*s,dock.x1-15.0f*s,mid+(open ? -4.0f : 4.0f)*s,4.5f*s,open ? kWhite : kMapOrder);
    if(!pad){const float hit[4]={dock.x0,y0,dock.x1,y1};MapCommandLegendButton(hit);}
    else MapCommandLegendButton(nullptr);
    if(!shown)return y0;
    MapUiBox(drawer,ctx,lx0,listTop,lx1,listBottom,lines,*at);
    MapFrame(drawer,ctx,lx0,listTop,lx1,listBottom,s,kMapOrderDim);
    const int first=*at;
    const float colW=(lx1-lx0)/static_cast<float>(kCols);
    int item=0;
    float ix=0.0f,tx=0.0f,y=0.0f;
    auto slot=[&]{
        const int col=item/kRows,row=item%kRows;++item;
        ix=lx0+colW*static_cast<float>(col)+16.0f*s;tx=ix+20.0f*s;y=listTop+5.0f*s+rowH*(static_cast<float>(row)+0.5f);
    };
    slot();Arc(drawer,ctx,ix,y,7.0f*s,0.0f,kTurn,2.0f*s,16,kWhite);
    Label(text,lines,at,tx,y,0,kLineScale*0.65f,kWhite,L"%ls",Tr(Tx::legendYou));
    for(const Entry& e:kLegend) {
        slot();MapIcon(drawer,ctx,ix,y,s*0.9f,e.kind,e.flags,0.0f,0.0f,-1.0f,1.0f);
        Label(text,lines,at,tx,y,0,kLineScale*0.65f,kWhite,L"%ls",Tr(e.name));
    }
    slot();MapDot1(drawer,ctx,ix,y,s,0,kMapEnemy);
    Label(text,lines,at,tx,y,0,kLineScale*0.65f,kWhite,L"%ls",Tr(Tx::legendEnemy));
    slot();MapDot1(drawer,ctx,ix,y,s,kMapFlying,kMapEnemy);
    Label(text,lines,at,tx,y,0,kLineScale*0.65f,kWhite,L"%ls",Tr(Tx::legendEnemyFlying));
    slot();MapBrackets(drawer,ctx,ix,y,8.0f*s,2.0f*s,kMapEnemy);
    Label(text,lines,at,tx,y,0,kLineScale*0.65f,kWhite,L"%ls",Tr(Tx::legendLock));
    slot();MapBrackets(drawer,ctx,ix,y,8.0f*s,2.0f*s,kAmber);
    Label(text,lines,at,tx,y,0,kLineScale*0.65f,kWhite,L"%ls",Tr(Tx::legendNearestEnemy));
    for(const Order o:kPaths) {
        slot();Seg(drawer,ctx,ix-8.0f*s,y,ix+8.0f*s,y,2.5f*s,MapPathColour(o));
        Label(text,lines,at,tx,y,0,kLineScale*0.65f,kWhite,L"%ls %ls",o==kPaths[0] ? Tr(Tx::legendPaths) : L"",MapOrderWord(o));
    }
    for(int i=first;i<*at;++i) {
        Line& row=lines[i];
        const float col=std::floor((row.x-lx0)/colW);
        MapFitLabel(text,row,row.x,col<static_cast<float>(kCols-1) ? lx0+colW*(col+1.0f)-4.0f*s : lx1-6.0f*s);
    }
    return y0;
}

// The test range's 「摆放」 panel (range_place.h, mapcmd.cpp; ini RangePlace) in the dock, from `top` down to `bottom`.
// Shut: its title row alone (a click, , . / or the D-pad opens it). Open: the title (a click shuts it), the categories
// as tabs with their counts (two rows of three), the list (as many rows as fit, its own scroll: the wheel over it, the
// arrows a page; the armed row lit, the picked one marked, one that cannot be made now dimmed with why at its right),
// the page line between the arrows and one line of what to press. A pad uses its D-pad, A and B instead of the pointer.
void MapPlacePanel(void* drawer,void* ctx,Text* text,float width,float s,float top,float bottom,bool pad,const MapCommandReadout& c,
                   Line* lines,int* at) noexcept {
    if(!c.placeOn || *at+2*rangeplace::kWindow+16>kMaxLines){MapCommandPlaceButtons(nullptr,nullptr,0,0);return;}
    const MapDock dock=MapDockAt(width,s);
    const float x0=dock.x0,x1=dock.x1,rowH=30.0f*s,titleH=32.0f*s,tabH=32.0f*s,arrowH=30.0f*s,footH=22.0f*s,pad6=6.0f*s;
    float rects[kMapPlaceItems*4];int codes[kMapPlaceItems];int hits=0;
    auto hit=[&](float a,float b,float r,float d,int code){
        if(pad || hits>=kMapPlaceItems)return;
        float* q=rects+4*hits;q[0]=a;q[1]=b;q[2]=r;q[3]=d;codes[hits++]=code;
    };
    auto over=[&](float a,float b,float r,float d){ return !pad && c.pointer && c.px>=a && c.px<r && c.py>=b && c.py<d; };
    const Tx* const words=DebugSpawnCategoryWords();
    using debugspawn::RowState;
    const int cat=c.placeCategory>=0 && c.placeCategory<debugspawn::kCategoryCount ? c.placeCategory : 0;
    const debugspawn::Category category=static_cast<debugspawn::Category>(cat);
    const int count=debugspawn::CountIn(category);
    const int pick=c.placePick>=0 && c.placePick<count ? c.placePick : 0;
    // The title row: 「摆放」, the category and the pick (shut: how to open it), the fold arrow. The whole row is the click.
    auto title=[&](bool open) {
        Rect(drawer,ctx,x0,top,x1,top+titleH,over(x0,top,x1,top+titleH) ? kBtnLit : kBtnFill);
        MapFrame(drawer,ctx,x0,top,x1,top+titleH,s,kMapOrder);
        const float mid=top+titleH*0.5f;
        Label(text,lines,at,x0+10.0f*s,mid,0,kLineScale*0.78f,kWhite,L"%ls",Tr(Tx::placeTitle));
        const float titleRight=*at>0 ? lines[*at-1].x+lines[*at-1].w+12.0f*s : x0+90.0f*s;
        if(open)Label(text,lines,at,x1-30.0f*s,mid,2,kLineScale*0.62f,kMapOrderDim,L"%ls  %d/%d",Tr(words[cat]),count ? pick+1 : 0,count);
        else Label(text,lines,at,x1-30.0f*s,mid,2,kLineScale*0.58f,kMapOrderDim,L"%ls",Tr(pad ? Tx::placeOpenPad : Tx::placeOpenMouse));
        if(*at>0)MapFitLabel(text,lines[*at-1],titleRight,x1-30.0f*s);
        Tri(drawer,ctx,x1-15.0f*s,mid+(open ? 4.0f : -4.0f)*s,x1-15.0f*s,mid+(open ? -4.0f : 4.0f)*s,5.0f*s,kMapOrder);
        MapTipSet(c,x0,top,x1,top+titleH,L"%ls",Tr(Tx::placeTabTip));
        hit(x0,top,x1,top+titleH,kPlaceToggle);
    };
    if(top+titleH>bottom){MapCommandPlaceButtons(nullptr,nullptr,0,0);return;}
    if(!c.placeOpen) {
        MapUiBox(drawer,ctx,x0,top,x1,top+titleH,lines,*at);
        title(false);
        MapCommandPlaceButtons(pad ? nullptr : rects,codes,hits,0);
        return;
    }
    const int tabRows=(debugspawn::kCategoryCount+2)/3;
    const float fixed=titleH+(c.placeOnline ? footH : 0.0f)+static_cast<float>(tabRows)*tabH+pad6+arrowH+footH+pad6;
    int window=static_cast<int>((bottom-top-fixed)/rowH);
    if(window>rangeplace::kWindow)window=rangeplace::kWindow;
    if(window<1) {   // no room for a row: the title alone (it still shuts the panel)
        MapUiBox(drawer,ctx,x0,top,x1,top+titleH,lines,*at);
        title(true);
        MapCommandPlaceButtons(pad ? nullptr : rects,codes,hits,0);
        return;
    }
    const int first=rangeplace::ViewFirst(c.placeTop,pick,count,window),shown=count-first<window ? count-first : window;
    const float y1=top+fixed+static_cast<float>(window)*rowH;   // the panel keeps its height while paging
    MapUiBox(drawer,ctx,x0,top,x1,y1,lines,*at);
    title(true);
    float y=top+titleH;
    if(c.placeOnline) {
        Label(text,lines,at,x0+8.0f*s,y+footH*0.5f,0,kLineScale*0.58f,kAmber,L"%ls",Tr(Tx::placeOnline));
        if(*at>0)MapFitLabel(text,lines[*at-1],x0+8.0f*s,x1-6.0f*s);
        y+=footH;
    }
    // The tabs, three a row, each with its count.
    const float tabW=(x1-x0-8.0f*s)/3.0f;
    for(int k=0;k<debugspawn::kCategoryCount;++k) {
        const float a=x0+4.0f*s+tabW*static_cast<float>(k%3),b=y+3.0f*s+tabH*static_cast<float>(k/3),r=a+tabW-4.0f*s,d=b+tabH-4.0f*s;
        const bool lit=k==cat;
        Rect(drawer,ctx,a,b,r,d,lit ? kBtnLit : over(a,b,r,d) ? kMapBoxFill : kBtnFill);
        MapFrame(drawer,ctx,a,b,r,d,s,lit ? kWhite : kMapOrderDim);
        Label(text,lines,at,(a+r)*0.5f,(b+d)*0.5f,1,kLineScale*0.6f,lit ? kWhite : kMapOrder,L"%ls %d",Tr(words[k]),
              debugspawn::CountIn(static_cast<debugspawn::Category>(k)));
        if(*at>0)MapFitLabel(text,lines[*at-1],a+3.0f*s,r-3.0f*s);
        hit(a,b,r,d,kPlaceTab+k);
    }
    y+=tabH*static_cast<float>(tabRows)+pad6;
    // The rows.
    for(int i=0;i<shown;++i) {
        const int row=debugspawn::RowOf(category,first+i);
        if(row<0)break;
        const float b=y+rowH*static_cast<float>(i),d=b+rowH-2.0f*s;
        const RowState st=c.placeState[row];
        const bool armed=c.placeArmed==row,picked=first+i==pick,ready=st==RowState::ready;
        const bool usable=ready || st==RowState::notPreloaded || st==RowState::loading;   // the first placing loads it
        if(armed)Rect(drawer,ctx,x0+4.0f*s,b,x1-4.0f*s,d,kBtnLit);
        else if(over(x0,b,x1,d) || (picked && pad))Rect(drawer,ctx,x0+4.0f*s,b,x1-4.0f*s,d,kMapBoxFill);
        else if(i%2)Rect(drawer,ctx,x0+4.0f*s,b,x1-4.0f*s,d,kBtnFill);
        const float mid=(b+d)*0.5f;
        float right=x1-10.0f*s;
        if(!ready) {   // why at the row's right, the name fitted left of it
            const wchar_t* why=st==RowState::online ? Tr(Tx::placeOnlineRow) : st==RowState::missing ? Tr(Tx::placeMissing) :
                st==RowState::notPreloaded ? Tr(Tx::placeNotPreloaded) : st==RowState::loading ? Tr(Tx::placeLoading) :
                st==RowState::broken ? Tr(Tx::placeBroken) : Tr(Tx::debugSpawnUnavailable);
            Label(text,lines,at,right,mid,2,kLineScale*0.56f,st==RowState::broken ? kAmber : st==RowState::loading ? kGreen : kMapOrderDim,
                  L"%ls",why);
            if(*at>0){Line& w=lines[*at-1];MapFitLabel(text,w,x1-160.0f*s,right);w.x=right-w.w;right=w.x-6.0f*s;}
        }
        Label(text,lines,at,x0+20.0f*s,mid,0,kLineScale*0.68f,armed || usable ? kWhite : kMapOrderDim,L"%ls",debugspawn::kEntries[row].name);
        if(*at>0)MapFitLabel(text,lines[*at-1],x0+20.0f*s,right);
        if(picked)Tri(drawer,ctx,x0+6.0f*s,mid,x0+14.0f*s,mid,4.5f*s,armed ? kWhite : kMapOrder);
        hit(x0,b,x1,d,row);
    }
    y+=rowH*static_cast<float>(window);
    // The page line: ▲ (a page up), the page we are on, ▼ (a page down); an arrow dim when there is nothing that way.
    const int pages=rangeplace::Pages(count,window),page=rangeplace::PageOf(first,count,window);
    const float aw=std::fmin(110.0f*s,(x1-x0)*0.3f);
    const float ua=x0+4.0f*s,ur=ua+aw,da=x1-4.0f*s-aw,dr=x1-4.0f*s,ab=y+2.0f*s,ad=y+arrowH-2.0f*s,amid=(ab+ad)*0.5f;
    const bool up=first>0,down=first+shown<count;
    Rect(drawer,ctx,ua,ab,ur,ad,up && over(ua,ab,ur,ad) ? kBtnLit : kBtnFill);MapFrame(drawer,ctx,ua,ab,ur,ad,s,up ? kMapOrder : kMapOrderDim);
    Tri(drawer,ctx,(ua+ur)*0.5f,amid+5.0f*s,(ua+ur)*0.5f,amid-5.0f*s,7.0f*s,up ? kWhite : kMapOrderDim);
    Rect(drawer,ctx,da,ab,dr,ad,down && over(da,ab,dr,ad) ? kBtnLit : kBtnFill);MapFrame(drawer,ctx,da,ab,dr,ad,s,down ? kMapOrder : kMapOrderDim);
    Tri(drawer,ctx,(da+dr)*0.5f,amid-5.0f*s,(da+dr)*0.5f,amid+5.0f*s,7.0f*s,down ? kWhite : kMapOrderDim);
    Label(text,lines,at,(x0+x1)*0.5f,amid,1,kLineScale*0.6f,kMapOrder,Tr(Tx::placePage),page,pages);
    if(*at>0)MapFitLabel(text,lines[*at-1],ur+4.0f*s,da-4.0f*s);
    if(up){hit(ua,ab,ur,ad,kPlacePageUp);MapTipSet(c,ua,ab,ur,ad,L"%ls",Tr(Tx::placePageUpTip));}
    if(down){hit(da,ab,dr,ad,kPlacePageDown);MapTipSet(c,da,ab,dr,ad,L"%ls",Tr(Tx::placePageDownTip));}
    y+=arrowH;
    // One line of what to press (the keyboard's keys in the title's tooltip).
    Label(text,lines,at,x0+8.0f*s,y+footH*0.5f,0,kLineScale*0.56f,kTeal,L"%ls",Tr(pad ? Tx::placeHintPad : Tx::placeHintMouse));
    if(*at>0)MapFitLabel(text,lines[*at-1],x0+8.0f*s,x1-6.0f*s);
    MapCommandPlaceButtons(pad ? nullptr : rects,codes,hits,window);
}

void MapCommands(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const MapReadout& m,Line* lines,int* at) noexcept {
    static MapCommandReadout c;   // the draw thread's (too big for its stack)
    if(!PlayerMapCommands(&c)){
        MapCommandButtons(nullptr,nullptr,0);MapCommandSquadButtons(nullptr,nullptr,0);MapCommandSquadFold(nullptr);
        MapCommandFormationButtons(nullptr,nullptr,0);
        MapCommandPayloadButtons(nullptr,0,0,nullptr,0);MapCommandSupportButtons(nullptr,nullptr,0);MapCommandPlaceButtons(nullptr,nullptr,0,0);
        MapCommandPvpButtons(nullptr,nullptr,0,nullptr);MapCommandEnemyButtons(nullptr,nullptr,0,nullptr);
        MapLegend(drawer,ctx,text,width,s,height-(kBtnBottom+40.0f)*s,m.pad,c,lines,at);return;
    }
    const float* tint=c.allowed && c.pickable ? kMapOrder : kMapOrderDim;
    // Where a point order goes: the pointer (a ring and a cross), or with a pad the crosshair at the centre; an armed
    // point order or support call in its colour.
    const float* aim=c.armedOrder ? MapPathColour(c.armed) : c.supportArmed>=0 || c.placeArmed>=0 ? kGreen : tint;
    const float cx=c.pointer ? c.px : width*0.5f,cy=c.pointer ? c.py : height*0.5f;
    Seg(drawer,ctx,cx-16.0f*s,cy,cx-5.0f*s,cy,2.0f*s,aim);Seg(drawer,ctx,cx+5.0f*s,cy,cx+16.0f*s,cy,2.0f*s,aim);
    Seg(drawer,ctx,cx,cy-16.0f*s,cx,cy-5.0f*s,2.0f*s,aim);Seg(drawer,ctx,cx,cy+5.0f*s,cx,cy+16.0f*s,2.0f*s,aim);
    if(c.pointer)Arc(drawer,ctx,cx,cy,9.0f*s,0.0f,kTurn,1.5f*s,16,c.armedOrder || c.supportArmed>=0 || c.placeArmed>=0 ? aim : kWhite);
    // The box being dragged.
    if(c.boxing) {
        const float x0=c.bx<c.px ? c.bx : c.px,x1=c.bx<c.px ? c.px : c.bx,y0=c.by<c.py ? c.by : c.py,y1=c.by<c.py ? c.py : c.by;
        Rect(drawer,ctx,x0,y0,x1,y1,kMapBoxFill);
        Seg(drawer,ctx,x0,y0,x1,y0,1.5f*s,kMapOrder);Seg(drawer,ctx,x1,y0,x1,y1,1.5f*s,kMapOrder);
        Seg(drawer,ctx,x1,y1,x0,y1,1.5f*s,kMapOrder);Seg(drawer,ctx,x0,y1,x0,y0,1.5f*s,kMapOrder);
    }
    // A right drag laying a line (mapcmd_logic.h LineOf): the line on the ground, ticks along it where the units will
    // stand, and an arrow from its middle the way they will face (square to the line, away from the camera).
    if(c.dragLine) {
        const float a[3]={c.lineFrom[0],c.lineFrom[1]+1.0f,c.lineFrom[2]},b[3]={c.lineTo[0],c.lineTo[1]+1.0f,c.lineTo[2]};
        MapLine(drawer,ctx,vp,width,height,a,b,2.5f*s,kWhite);
        const float dx=b[0]-a[0],dz=b[2]-a[2],length=std::hypot(dx,dz);
        if(length>1.0f) {
            const float face[2]={dz/length,-dx/length};   // a side of the line; the units' facing (mapcmd.cpp: away from the camera)
            const float mid[3]={(a[0]+b[0])*0.5f,a[1],(a[2]+b[2])*0.5f};
            const float tip[3]={mid[0]+face[0]*kMapGuardRing,mid[1],mid[2]+face[1]*kMapGuardRing};
            const float rear[3]={mid[0]-face[0]*kMapGuardRing,mid[1],mid[2]-face[1]*kMapGuardRing};
            float mx,my,md,tx,ty,td,bx,by,bd;
            // The camera's side of the line is the back: the arrow points to the far side (the greater depth).
            if(Project(vp,mid,width,height,&mx,&my,&md) && Project(vp,tip,width,height,&tx,&ty,&td) &&
               Project(vp,rear,width,height,&bx,&by,&bd)) {
                if(td>bd)Tri(drawer,ctx,mx,my,tx,ty,6.0f*s,kWhite);
                else Tri(drawer,ctx,mx,my,bx,by,6.0f*s,kWhite);
            }
        }
    }
    // The pings (F1-F4, this machine's and the room's): rings opening out on the point while they last, its number.
    {
        static const float* const kPingColour[4]={kAmber,kWhite,kGreen,kRed};
        const std::uint64_t now=GetTickCount64();
        for(int i=0;i<c.pings && i<MapCommandReadout::kPings;++i) {
            const auto& p=c.ping[i];
            float px,py,pd;
            if(!Project(vp,p.at,width,height,&px,&py,&pd))continue;
            const float* colour=kPingColour[p.kind&3u];
            const float phase=static_cast<float>((now-p.at_ms)%1000u)/1000.0f;
            Arc(drawer,ctx,px,py,(8.0f+22.0f*phase)*s,0.0f,kTurn,2.5f*s,20,colour);
            Arc(drawer,ctx,px,py,6.0f*s,0.0f,kTurn,2.0f*s,14,colour);
            WorldLabel(text,lines,at,vp,width,height,p.at,WorldKey(kWorldMapHover,static_cast<std::uint64_t>(i)+0x50494E47u),0.0f,-34.0f*s,1,
                       kLineScale*0.6f,colour,Tr(Tx::cmdPing),static_cast<int>(p.kind)+1);
        }
    }
    // The enemy-control target picked (a click on its mark, L3, the card's arrows): cyan brackets on it.
    {
        MapEnemyControlReadout picked{};float tx,ty,td;
        if(ReadMapEnemyControl(&picked) && picked.visible && picked.marked && Project(vp,picked.markAt,width,height,&tx,&ty,&td)) {
            MapBrackets(drawer,ctx,tx,ty,26.0f*s,2.5f*s,kCyan);
            WorldLabel(text,lines,at,vp,width,height,picked.markAt,WorldKey(kWorldMapTarget,picked.markOf),0.0f,34.0f*s,1,kLineScale*0.6f,kCyan,L"%ls",Tr(Tx::enemyControlTargetMark));
        }
    }
    // The enemy under the pointer: amber brackets on it, what the right button (or H) and Q do with it.
    float hx,hy,hd;
    if(c.hover && Project(vp,c.hoverAt,width,height,&hx,&hy,&hd)) {
        MapBrackets(drawer,ctx,hx,hy,20.0f*s,2.0f*s,kAmber);
        WorldLabel(text,lines,at,vp,width,height,c.hoverAt,WorldKey(kWorldMapHover,c.hoverOf),0.0f,-30.0f*s,1,kLineScale*0.6f,kAmber,L"%ls",Tr(Tx::cmdHoverEnemy));
    }
    // The transports' pairs (transport.cpp): a thin line from a squad on foot to its vehicle (bright on a trip).
    for(int i=0;i<c.links && i<kMapTransportLinks;++i) {
        float sx,sy,sd,vx,vy,vd;
        float sp[3],vp3[3];mapcmd::BodyPoint(c.link[i].squad,false,sp);mapcmd::BodyPoint(c.link[i].vehicle,false,vp3);
        if(Project(vp,sp,width,height,&sx,&sy,&sd) && Project(vp,vp3,width,height,&vx,&vy,&vd))
            Seg(drawer,ctx,sx,sy,vx,vy,1.0f*s,c.link[i].trip ? kMapOrder : kMapOrderDim);
    }
    wchar_t one[64]{};
    for(int i=0;i<c.count && i<kCmdUnits;++i) {
        const CmdMark& u=c.unit[i];
        if(u.riding && !u.selected)continue;   // its vehicle's mark stands for it (picked only from its panel row)
        // On the unit's body (mapcmd_logic.h BodyPoint; the user, 2026-10-09: "直接在npc身上不好吗"), where a click takes it.
        float body[3],ux=0.0f,uy=0.0f,ud=0.0f;
        mapcmd::BodyPoint(u.pos,u.air,body);
        const bool shown=Project(vp,body,width,height,&ux,&uy,&ud);
        if(mapcmd::PointOrder(u.now.order)) {
            // The ring round its point (MapPathColour: the order's own colour, the legend's), its line from the unit.
            const float* order=MapPathColour(u.now.order);
            float last[3];
            for(int k=0;k<=16;++k) {
                const float a=kTurn*static_cast<float>(k)/16.0f;
                const float q[3]={u.now.at[0]+std::sin(a)*kMapGuardRing,u.now.at[1]+1.0f,u.now.at[2]+std::cos(a)*kMapGuardRing};
                if(k)MapLine(drawer,ctx,vp,width,height,last,q,2.0f*s,order);
                std::memcpy(last,q,12);
            }
            float gx,gy,depth;
            if(shown && Project(vp,u.now.at,width,height,&gx,&gy,&depth))Seg(drawer,ctx,ux,uy,gx,gy,1.5f*s,u.now.order==Order::guard ? kMapOrderDim : order);
            // Its Shift-queued points after that one (mapcmd_logic.h Waypoints): a line on through them, a small ring each.
            const float* from=u.now.at;
            for(int w=0;w<u.ways && w<mapcmd::kWaypointsMost;++w) {
                const float a[3]={from[0],from[1]+1.0f,from[2]},b[3]={u.way[w][0],u.way[w][1]+1.0f,u.way[w][2]};
                MapLine(drawer,ctx,vp,width,height,a,b,1.5f*s,kMapOrderDim);
                float wx,wy,wd;
                if(Project(vp,b,width,height,&wx,&wy,&wd))Arc(drawer,ctx,wx,wy,5.0f*s,0.0f,kTurn,1.5f*s,12,kMapOrder);
                from=u.way[w];
            }
        }
        if(!shown)continue;
        Arc(drawer,ctx,ux,uy,13.0f*s,0.0f,kTurn,1.5f*s,20,u.selected ? kWhite : u.locked ? kMapLocked : kMapOrderDim);
        if(u.selected)MapBrackets(drawer,ctx,ux,uy,20.0f*s,2.5f*s,kWhite);
        if(u.groups) {   // its RTS groups by its mark (Ctrl+1-9)
            wchar_t digits[16];MapGroupDigits(u.groups,digits,_countof(digits));
            WorldLabel(text,lines,at,vp,width,height,body,WorldKey(kWorldMapUnit,u.of),15.0f*s,-15.0f*s,0,kLineScale*0.55f,u.selected ? kWhite : kMapOrder,L"%ls",digits);
        }
        if(u.now.order==Order::follow)WorldLabel(text,lines,at,vp,width,height,body,WorldKey(kWorldMapUnit,u.of),0.0f,22.0f*s,1,kLineScale*0.6f,kMapOrder,L"%ls",Tr(Tx::orderFollow));
        if(u.selected && c.selected==1) {
            wchar_t kind[48];
            MapUnitName(u.name,kind,_countof(kind));
            if(u.owner==kCmdOwnerHeli || u.owner==kCmdOwnerJet)
                _snwprintf_s(one,_countof(one),_TRUNCATE,Tr(u.owner==kCmdOwnerHeli ? Tx::unitHeli : Tx::unitJet),kind);
            else _snwprintf_s(one,_countof(one),_TRUNCATE,L"%ls",kind);
            WorldLabel(text,lines,at,vp,width,height,body,WorldKey(kWorldMapUnit,u.of),0.0f,-28.0f*s,1,kLineScale*0.7f,kWhite,L"%ls",one);
        }
    }
    const float panelBottom=MapSquadPanel(drawer,ctx,text,width,s,m.pad,c,lines,at);
    MapEnemyControlReadout enemyState{};
    const bool controlling=ReadMapEnemyControl(&enemyState) &&
        (enemyState.snapshot.phase==enemy_control::Phase::active || enemyState.snapshot.phase==enemy_control::Phase::pending ||
         enemyState.snapshot.phase==enemy_control::Phase::restoring);
    float payloadBottom=0.0f;
    if(controlling)MapCommandPayloadButtons(nullptr,0,0,nullptr,0);
    else payloadBottom=MapPayloadPanel(drawer,ctx,text,width,s,m.pad,c,lines,at);
    const int buttonRows=MapButtons(drawer,ctx,text,width,height,s,m.pad,c,lines,at);
    // The support bar under the squad panel (its rows and the gap under them), down to the command card.
    const float barTop=c.squads>0 ? panelBottom+10.0f*s : panelBottom;
    const float barBottom=height-(kBtnBottom+static_cast<float>(buttonRows)*(kBtnRowH+kBtnGap)+40.0f)*s;
    MapSupportBar(drawer,ctx,text,width,s,barTop,barBottom,m.pad,c,lines,at);
    // The right-hand dock (MapDock): under the scale / compass and the payload panel, the enemy control, PVP and the
    // range's 「摆放」 down to the legend folded at its foot, each in its own band. A band changes only by the user's own
    // click on its panel's title (or L3 / R3 into it): a pick, a hover or a selection never folds or unfolds one
    // (mapcmd.cpp SetEnemyCandidate), so the panels under it never move on their own (the user, 2026-10-19: 「这个展开和
    // 收起会自动变，我不好点」). A folded panel keeps only its title's height: the 「摆放」 list keeps its rows.
    const float dockTop=std::fmax(206.0f*s,payloadBottom+10.0f*s);
    const float dockBottom=MapLegend(drawer,ctx,text,width,s,barBottom,m.pad,c,lines,at)-8.0f*s;
    const float enemyBottom=MapEnemyPanel(drawer,ctx,text,width,s,dockTop,dockBottom,m.pad,c,lines,at);
    const float pvpBottom=MapPvpPanel(drawer,ctx,text,width,s,enemyBottom+8.0f*s,dockBottom,m.pad,c,lines,at);
    MapPlacePanel(drawer,ctx,text,width,s,pvpBottom+8.0f*s,dockBottom,m.pad,c,lines,at);
    // The formation button's menu over all of them (its box empties the text under it; its rows take the clicks first).
    if(m.pad)MapCommandFormationButtons(nullptr,nullptr,0);
    else MapFormationMenu(drawer,ctx,text,width,height,s,c,mapCardRects,mapCardIds,mapCardCount,lines,at);
    // The band over the map's keys: what is selected and what to do with it (one line; the keys are in the tooltips).
    Rect(drawer,ctx,0.0f,height-104.0f*s,width,height-46.0f*s,kMapBand);
    const float y=height-75.0f*s;
    const int footerFirst=*at;
    wchar_t sel[64];
    if(c.all)_snwprintf_s(sel,_countof(sel),_TRUNCATE,Tr(Tx::selectedAll),c.selected);
    else if(c.selected==1 && one[0])_snwprintf_s(sel,_countof(sel),_TRUNCATE,Tr(Tx::selectedOne),c.pickable,one);
    else _snwprintf_s(sel,_countof(sel),_TRUNCATE,Tr(Tx::selectedSome),c.selected,c.pickable);
    if(!c.allowed)Label(text,lines,at,width*0.5f,y,1,kLineScale*0.75f,kAmber,L"%ls",Tr(Tx::npcOfflineOnly));
    else if(!c.pickable)Label(text,lines,at,width*0.5f,y,1,kLineScale*0.75f,kWhite,L"%ls",Tr(Tx::npcNone));
    else if(!c.selected && !m.pad)Label(text,lines,at,width*0.5f,y,1,kLineScale*0.75f,kWhite,L"%ls",Tr(Tx::cmdHintSelect));
    else if(m.pad)Label(text,lines,at,width*0.5f,y,1,kLineScale*0.75f,kWhite,Tr(Tx::npcPadKeys),sel);
    else {
        wchar_t key[6][24];
        const mapkeys::Key shown[6]={mapkeys::attackMove,mapkeys::fastMove,mapkeys::reverse,mapkeys::stop,mapkeys::holdFire,mapkeys::subgroup};
        for(int i=0;i<6;++i)ControlKeyName(mapkeys::Of(Cfg()).vk[shown[i]],key[i],24);
        Label(text,lines,at,width*0.5f,y,1,kLineScale*0.75f,kWhite,Tr(Tx::npcMouseKeys),sel,key[0],key[1],key[2],key[3],key[4],key[5]);
    }
    if(c.noteFresh)Label(text,lines,at,width*0.5f,height-124.0f*s,1,kLineScale*0.8f,kAmber,L"%ls",c.note);
    for(int i=footerFirst;i<*at;++i) {
        Line& row=lines[i];MapFitLabel(text,row,16.0f*s,width-16.0f*s);row.x=(width-row.w)*0.5f;
    }
    // Tab's sub-selection: the kind the orders go to, over the band's left.
    if(c.subKind[0] && c.selected>0) {
        wchar_t kind[32];hudtext::WordTo(c.subKind,kind,_countof(kind));
        Label(text,lines,at,16.0f*s,height-124.0f*s,0,kLineScale*0.75f,kWhite,Tr(Tx::cmdSubShown),kind);
    }
}

// The title and the keys (top and bottom bands).
void MapText(void* drawer,void* ctx,Text* text,float width,float height,float s,const MapReadout& m,Line* lines,int* at) noexcept {
    MapUiBox(drawer,ctx,0,0,width,46.0f*s,lines,*at);
    MapUiBox(drawer,ctx,0,height-46.0f*s,width,height,lines,*at);
    const int bandsFirst=*at;
    if(m.watchCommander)Label(text,lines,at,width*0.5f,23.0f*s,1,kTitleScale,kWhite,L"%ls",Tr(Tx::mapCommanderView));
    else {
        wchar_t h[24],g[24];
        MapDistance(h,_countof(h),m.height);MapDistance(g,_countof(g),mapcam::GridStep(m.height));
        wchar_t follow[32]=L"";
        if(m.follow)std::swprintf(follow,32,L"   %ls",Tr(Tx::orderFollow));
        Label(text,lines,at,width*0.5f,23.0f*s,1,kTitleScale,kWhite,Tr(Tx::mapTitle),h,g,follow);
    }
    if(m.commander) {
        wchar_t key[32];
        if(!m.pad)KeyName(m.mapKey,key,32);
        else if(m.mapButton==0x20)wcscpy_s(key,Tr(Tx::padBack));   // XInput's default map button
        else std::swprintf(key,32,Tr(Tx::padButton),m.mapButton);
        Label(text,lines,at,width*0.5f,height-23.0f*s,1,kLineScale*0.8f,kWhite,Tr(Tx::mapCommanderSwitch),key);
    } else if(m.pad)Label(text,lines,at,width*0.5f,height-23.0f*s,1,kLineScale*0.8f,kWhite,L"%ls",Tr(Tx::mapPadKeys));
    else {
        wchar_t key[32];KeyName(m.mapKey,key,32);
        Label(text,lines,at,width*0.5f,height-23.0f*s,1,kLineScale*0.8f,kWhite,Tr(Tx::mapMouseKeys),key);
    }
    for(int i=bandsFirst;i<*at;++i)MapFitLabel(text,lines[i],16.0f*s,width-16.0f*s);
}

#include "hud_stats.inc"

// The map's click targets dropped (the map shut, or the stats page over it: nothing under the page takes a click).
void MapTargetsNone() noexcept {
    MapCommandButtons(nullptr,nullptr,0);MapCommandSquadButtons(nullptr,nullptr,0);MapCommandSquadFold(nullptr);
    MapCommandFormationButtons(nullptr,nullptr,0);MapCommandComposeButtons(nullptr,nullptr,0);
    MapCommandPayloadButtons(nullptr,0,0,nullptr,0);MapCommandSupportButtons(nullptr,nullptr,0);MapCommandUiPanels(nullptr,0);
    MapCommandPlaceButtons(nullptr,nullptr,0,0);MapCommandPvpButtons(nullptr,nullptr,0,nullptr);MapCommandEnemyButtons(nullptr,nullptr,0,nullptr);
    MapCommandLegendButton(nullptr);
}

// The map view open: its marks drawn (true), nothing else of the HUD; the damage statistics' page in their place while it
// is shown.
bool MapScreen(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int* at) noexcept {
    static MapReadout m;   // the draw thread's (too big for its stack)
    static dmgstat::Book book;   // the last copy of the statistics (copied again only when they moved)
    if(!PlayerMap(&m)){MapTargetsNone();DamageStatsUi(nullptr,nullptr,0,0,0);return false;}
    if(m.ownerHumanKey!=passPlayerRefKey && m.ownerHumanKey!=passPlayerSoldierKey)return false;
    s=hudscale::FitMap(s,width,height);
    if(text)text->s=s;
    // Its world labels their 2D size at the focus (the camera's distance), larger nearer, smaller past it.
    worldRef=mapcam::Distance(mapcam::View{{m.focus[0],m.focus[1],m.focus[2]},m.yaw,m.pitch,m.height});
    worldMapView=true;   // its labels' facings its own (made square on to the map's camera)
    mapUiPanelCount=0;mapTip.on=false;
    bool fresh=false;
    dmgstat::View statsView{};
    std::uint32_t missionMs=0;
    const bool stats=DamageStatsRead(&book,&fresh,&statsView,&missionMs);
    if(stats && statsView.open) {
        MapTargetsNone();
        MapCommandView(vp,width,height);   // the pointer moves on this view
        MapCommandReadout c{};
        const bool pointer=PlayerMapCommands(&c) && c.pointer && !m.pad;
        StatsPage(drawer,ctx,text,width,height,s,book,statsView,missionMs,m.pad,StatsPointer{pointer,c.px,c.py},lines,at);
        MapCommandUiPanels(mapUiPanels,mapUiPanelCount);
        return true;
    }
    if(!m.watchCommander)MapGrid(drawer,ctx,text,vp,width,height,s,m,lines,at);
    MapUnits(drawer,ctx,text,vp,width,height,s,m,lines,at);
    MapCommandView(vp,width,height);   // the commands' box, clicks and pointer are found on this view
    QMarkHud(drawer,ctx,text,vp,width,height,s,lines,at);   // the Q marks, ours and the teammates', on the map too
    SupportWarnHud(drawer,ctx,text,vp,width,height,s,lines,at);   // the supports' areas and routes, the armed one's preview
    MapCommands(drawer,ctx,text,vp,width,height,s,m,lines,at);
    if(!m.watchCommander) {
        MapUiBox(drawer,ctx,width*0.55f-8*s,146*s,width-12*s,198*s,lines,*at);
        MapScale(drawer,ctx,text,vp,width,height,s,m,lines,at);
        MapUiBox(drawer,ctx,width-134*s,54*s,width-6*s,174*s,lines,*at);
        MapCompass(drawer,ctx,text,vp,width,height,s,m,lines,at);
    }
    MapText(drawer,ctx,text,width,height,s,m,lines,at);
    if(stats)StatsMapTab(drawer,ctx,text,width,height,s,lines,at);
    else DamageStatsUi(nullptr,nullptr,0,0,0);
    if(!m.pad)MapTipDraw(drawer,ctx,text,width,height,s,lines,at);   // over everything: drawn last
    MapCommandUiPanels(mapUiPanels,mapUiPanelCount);
    MapCommandReadout pointer{};
    if(PlayerMapCommands(&pointer) && pointer.pointer && !m.pad) {
        for(int i=0;i<mapUiPanelCount;++i) {
            const auto* r=mapUiPanels+4*i;
            if(pointer.px<r[0] || pointer.px>=r[2] || pointer.py<r[1] || pointer.py>=r[3])continue;
            // Keep the pointer above opaque controls, rather than hiding the map crosshair behind them.
            Tri(drawer,ctx,pointer.px+9*s,pointer.py+13*s,pointer.px,pointer.py,5*s,kWhite);break;
        }
    }
    return true;
}
// The Q marks (qmark.cpp; the user's Q, docs/npc-ai-design.md §6.3): an enemy marked has a diamond round it, a point on
// the ground a ring with a cross; this machine's player's amber with MARK and the distance, a teammate's in its player
// slot's colour with its player's name (the game's name tag's; P<n> when it could not be read, ALLY when its slot is not
// known here) and the distance. The teammates' are drawn first: ours
// on top where two fall together.
alignas(16) const float kSlotTint[4][4]={{0.3f,0.85f,1.0f,1.0f},{1.0f,0.45f,0.9f,1.0f},{0.55f,1.0f,0.35f,1.0f},{0.72f,0.6f,1.0f,1.0f}};
void QMarkHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int* at) noexcept {
    QMarkView v[kQMarkViews];
    const int n=QMarkViews(v,kQMarkViews);
    float eye[3],dir[3];
    const bool cam=CameraRay(eye,dir);
    for(int pass=0;pass<2;++pass)for(int i=0;i<n;++i) {
        const QMarkView& m=v[i];
        float x,y,depth;
        if(m.own!=(pass==1) || !Project(vp,m.at,width,height,&x,&y,&depth))continue;
        const float* tint=m.own ? kAmber : m.slot>=0 ? kSlotTint[m.slot%4] : kWhite;
        const float r=(m.own ? 16.0f : 13.0f)*s,t=2.0f*s;
        if(m.enemy) {
            Seg(drawer,ctx,x,y-r,x+r,y,t,tint);Seg(drawer,ctx,x+r,y,x,y+r,t,tint);
            Seg(drawer,ctx,x,y+r,x-r,y,t,tint);Seg(drawer,ctx,x-r,y,x,y-r,t,tint);
        } else {
            Arc(drawer,ctx,x,y,r*0.9f,0.0f,kTurn,t,20,tint);
            Seg(drawer,ctx,x,y-r*0.4f,x,y+r*0.4f,t,tint);Seg(drawer,ctx,x-r*0.4f,y,x+r*0.4f,y,t,tint);
        }
        // An enemy's words under it; a point's over it (the selected units' word, NpcPingHud, goes under).
        const float ly=m.enemy ? r+12.0f*s : -r-12.0f*s;   // from the mark
        const float d=cam ? vec::Dist(eye,m.at) : -1.0f;
        // Its plate kept by whose mark it is and what it is of (the enemy marked, the point's making).
        const std::uint64_t whose=(m.own ? 1u : 0u)|(m.enemy ? 2u : 0u)|(static_cast<std::uint64_t>(m.slot+1)&0xFFu)<<2;
        const std::uint64_t key=WorldKey(kWorldQMark,m.of*0x100000001B3ull^whose);
        if(m.own && d>=0.0f)WorldLabel(text,lines,at,vp,width,height,m.at,key,0.0f,ly,1,kLineScale*0.7f,tint,Tr(Tx::npcMarkRange),d);
        else if(m.own)WorldLabel(text,lines,at,vp,width,height,m.at,key,0.0f,ly,1,kLineScale*0.7f,tint,L"%ls",Tr(Tx::npcMark));
        // Its player's name as the game's name tag shows it (any script: the game's font draws it; FitName cut it to
        // kQMarkName - 1 characters, so a long one never runs past the mark); not read: P<n>.
        else if(m.name[0])WorldLabel(text,lines,at,vp,width,height,m.at,key,0.0f,ly,1,kLineScale*0.7f,tint,L"%ls %.0f m",m.name,d>=0.0f ? d : 0.0f);
        else if(m.slot>=0)WorldLabel(text,lines,at,vp,width,height,m.at,key,0.0f,ly,1,kLineScale*0.7f,tint,Tr(Tx::qmarkPlayer),m.slot+1,d>=0.0f ? d : 0.0f);
        else WorldLabel(text,lines,at,vp,width,height,m.at,key,0.0f,ly,1,kLineScale*0.7f,tint,Tr(Tx::qmarkAlly),d>=0.0f ? d : 0.0f);
    }
}

// The supports' warnings (support_warning.h; the user, 2026-10-26: 「支援要有攻击警告红圈，直升机等没有红圈的可以给出路线预览
// 图之类的」): a strike's area red, pulsing; a flight's route cyan with an arrow at its end, its guard ring cyan; the map's
// preview of the armed support fainter and dashed. Their words (the name, the seconds left) over the area / at the route's
// start. On the map (`map`) and on the view alike: each outline is laid on the ground where it was made.
void SupportWarnHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int* at) noexcept {
    static WarningView views[kWarningViews];
    const int n=SupportWarningViews(views,kWarningViews);
    if(n<=0)return;
    const float pulse=0.55f+0.45f*std::fabs(std::sin(static_cast<float>(GetTickCount64()%2000ull)*kTurn/2000.0f));
    for(int w=0;w<n;++w) {
        const WarningView& v=views[w];
        const float* base=v.look==warning::Look::strike ? kRed : kCyan;
        const float a=v.preview ? 0.6f : v.look==warning::Look::strike ? pulse : 0.9f;
        // Its colour kept past this call: a Line keeps a pointer to it until the text is drawn (DrawAll).
        static float colours[kWarningViews][4];
        float* const rgba=colours[w];
        rgba[0]=base[0];rgba[1]=base[1];rgba[2]=base[2];rgba[3]=base[3]*a;
        const float t=(v.look==warning::Look::strike ? 2.6f : 2.0f)*s;
        const int segs=v.closed ? v.points : v.points-1;
        for(int i=0;i<segs;++i) {
            const float* p=v.pt[i];const float* q=v.pt[(i+1)%v.points];
            if(!std::isfinite(p[0]) || !std::isfinite(q[0]))continue;   // a gap between two outlines of one warning
            if(v.preview && (i&1))continue;                              // dashed
            MapLine(drawer,ctx,vp,width,height,p,q,t,rgba);
        }
        if(!v.closed && v.points>=2) {   // the route's arrow at its end (on the screen, along its last stretch)
            float ex,ey,ed,fx,fy,fd;
            const float* end=v.pt[v.points-1];const float* before=v.pt[v.points-2];
            if(std::isfinite(before[0]) && Project(vp,end,width,height,&ex,&ey,&ed) && Project(vp,before,width,height,&fx,&fy,&fd)) {
                const float dx=ex-fx,dy=ey-fy,l=std::sqrt(dx*dx+dy*dy);
                if(l>1.0f) {
                    const float ux=dx/l,uy=dy/l,h=12.0f*s;
                    Seg(drawer,ctx,ex,ey,ex-ux*h-uy*h*0.6f,ey-uy*h+ux*h*0.6f,t,rgba);
                    Seg(drawer,ctx,ex,ey,ex-ux*h+uy*h*0.6f,ey-uy*h-ux*h*0.6f,t,rgba);
                }
            }
        }
        if(v.text[0])WorldLabel(text,lines,at,vp,width,height,v.label,WorldKey(kWorldWarning,v.id),0.0f,-14.0f*s,1,kLineScale*0.7f,rgba,L"%ls",v.text);
    }
}
}  // namespace hudi

// The march formation just cycled (npcai.cpp; the player's key on foot or the map's T): its name and the key, a moment
// under the screen's centre.
// The formation's name (formation.h Shape) as the HUD says it (mapcmd.cpp's notes too).

const wchar_t* FormationText(int shape,bool guard) noexcept {
    using hudtext::Tx;
    static const Tx kNames[]={Tx::formStock,Tx::formColumn,Tx::formStaggered,Tx::formWedge,Tx::formVee,Tx::formLine,
                              Tx::formEchelonLeft,Tx::formEchelonRight,Tx::formDiamond,Tx::formBounding,Tx::formPerimeter,
                              Tx::formFollow};
    static_assert(sizeof(kNames)/sizeof(kNames[0])==static_cast<std::size_t>(npc::formation::kShapes),"a name a shape");
    if(shape==static_cast<int>(npc::formation::Shape::stock) && !guard)return hudtext::Tr(Tx::formStockFollow);
    return hudtext::Tr(shape>=0 && shape<npc::formation::kShapes ? kNames[shape] : Tx::formStock);
}
namespace hudi {
void FormationBanner(Text* text,float width,float height,float s,Line* lines,int* at) noexcept {
    FormationCue c{};
    if(!PlayerFormationCue(&c))return;
    wchar_t key[24];
    KeyName(c.key,key,_countof(key));
    Label(text,lines,at,width*0.5f,height*0.5f+190.0f*s,1,kLineScale,kCyan,Tr(Tx::formationCue),FormationText(c.shape,false),key);
}

// The box sweep (npcai.cpp, the player's NpcPickupKey): the boxes still to fetch and the ones in while it goes, the
// count a moment after it is over; under the formation's banner.
void SweepBanner(Text* text,float width,float height,float s,Line* lines,int* at) noexcept {
    SweepCue c{};
    if(!PlayerSweepCue(&c))return;
    wchar_t key[24];
    KeyName(c.key,key,_countof(key));
    if(c.on)Label(text,lines,at,width*0.5f,height*0.5f+222.0f*s,1,kLineScale,kAmber,Tr(Tx::sweepOn),c.left,c.taken,key);
    else Label(text,lines,at,width*0.5f,height*0.5f+222.0f*s,1,kLineScale,kGreen,Tr(Tx::sweepDone),c.taken);
}
// The caller's strike calls (airstrike.cpp PlayerStrikeCue): a barrage that could not start, a sortie's second wave not
// called; a moment, under the ultimate's banner.
void StrikeBanner(Text* text,float width,float height,float s,Line* lines,int* at) noexcept {
    wchar_t line[200];
    if(!PlayerStrikeCue(line,_countof(line)))return;
    Label(text,lines,at,width*0.5f,height*0.5f+370.0f*s,1,kLineScale*0.85f,kAmber,L"%ls",line);
}
}  // namespace hudi
}  // namespace crew
