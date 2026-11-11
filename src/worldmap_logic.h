// The world map mission select (docs/plan-2026-11-10-worldmap.md; test site #46): pure logic, no game memory, so it is
// checked offline (tests/worldmap_test.cpp). The data (src/worldmap.inc, tools/gen_worldmap.py): every mission key ->
// one real place (pylib/worldmap.py: the map the mission loads -> a real location of that kind).
//  - The base map is equirectangular (NASA Blue Marble, tools/worldmap_basemap.py): a place's (u, v) is linear in its
//    longitude / latitude, u east from 180 W, v south from 90 N.
//  - The view: a centre (u, v) and a zoom (map widths per screen width); panned and zoomed about the cursor, clamped so
//    the map always covers the screen vertically and wraps east-west.
//  - The route to the next mission: the shorter great circle between the two places, as a polyline split where it
//    crosses the antimeridian.
//  - Markers on one place (several missions on one map) fan out on a golden-angle spiral in screen pixels, so they
//    stay apart at every zoom.
//  - The base map is a JPEG tile pyramid of the whole Earth (pylib/worldmap.py TILE*, kTile / kTileLevels): the view
//    draws the coarsest level that is at least as sharp as the screen, never zooming past the deepest level's pixels.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <utility>

namespace worldmap {

enum class Region : int { hokkaido, tohoku, kanto, chubu, kansai, chugoku, kyushu, europe, africa };
// Names in src/hudtext.h Lang order: en, zhCN, zhTW, ja.
struct Place { float lat; float lon; Region region; const wchar_t* id; const wchar_t* name[4]; };
struct Mission { const wchar_t* key; int place; };

#include "worldmap.inc"

inline constexpr int kPlaceCount=static_cast<int>(sizeof(kPlaces)/sizeof(kPlaces[0]));
inline constexpr int kMissionCount=static_cast<int>(sizeof(kMissions)/sizeof(kMissions[0]));

inline bool SameKey(const wchar_t* a,const wchar_t* b) noexcept {
    if(!a || !b)return false;
    for(;;++a,++b) {
        wchar_t x=*a,y=*b;
        if(x>=L'a' && x<=L'z')x=static_cast<wchar_t>(x-L'a'+L'A');
        if(y>=L'a' && y<=L'z')y=static_cast<wchar_t>(y-L'a'+L'A');
        if(x==L'\\')x=L'/';
        if(y==L'\\')y=L'/';
        if(x!=y)return false;
        if(!x)return true;
    }
}
inline bool StartsWith(const wchar_t* s,const wchar_t* prefix) noexcept {
    if(!s)return false;
    for(;*prefix;++s,++prefix) {
        wchar_t x=*s,y=*prefix;
        if(x>=L'a' && x<=L'z')x=static_cast<wchar_t>(x-L'a'+L'A');
        if(x!=y)return false;
    }
    return true;
}
// The place of a list row's key (MISSIONLIST field 2, e.g. L"EDF6/M000B"); -1: a key the table does not know (a list
// the plugin did not make: that row gets no marker and the log says so once).
inline int PlaceOfKey(const wchar_t* key) noexcept {
    if(StartsWith(key,kRangeKeyPrefix))return kRangePlace;
    for(const Mission& m:kMissions)if(SameKey(m.key,key))return m.place;
    return -1;
}

// --- Projection ---
struct MapPoint { float u; float v; };
inline constexpr float kPi=3.14159265f;
inline MapPoint Project(float lat,float lon) noexcept {
    float u=(lon+180.f)/360.f;
    u-=std::floor(u);
    return MapPoint{u,(90.f-lat)/180.f};
}

// --- The tile pyramid (pylib/worldmap.py tile_grid / tile_table) ---
// Level z: 2^z columns, max(1, 2^(z-1)) rows; the whole map is kTile * 2^z px wide.
inline int TileColumns(int level) noexcept { return 1<<level; }
inline int TileRows(int level) noexcept { return level>0 ? 1<<(level-1) : 1; }
// The tile's entry in the file's table (levels in order, rows top down, columns west to east); -1 outside the level.
inline int TileIndex(int level,int x,int y) noexcept {
    if(level<0 || level>=kTileLevels || x<0 || y<0 || x>=TileColumns(level) || y>=TileRows(level))return -1;
    int base=0;
    for(int z=0;z<level;++z)base+=TileColumns(z)*TileRows(z);
    return base+y*TileColumns(level)+x;
}
// The level of a table entry (-1: outside the table).
inline int LevelOfIndex(int index) noexcept {
    if(index<0)return -1;
    int base=0;
    for(int z=0;z<kTileLevels;++z) {
        base+=TileColumns(z)*TileRows(z);
        if(index<base)return z;
    }
    return -1;
}
inline constexpr int kTileCount=[] {
    int n=0;
    for(int z=0;z<kTileLevels;++z)n+=(1<<z)*(z>0 ? 1<<(z-1) : 1);
    return n;
}();
inline constexpr float kDeepestWidth=static_cast<float>(kTile)*static_cast<float>(1<<(kTileLevels-1));   // px

// --- The view ---
struct View {
    float cu=0.5f,cv=0.5f;   // the map point at the screen's centre
    float zoom=1.f;          // map widths across the screen: 1 the whole world, more nearer
};
// The layout the map is drawn on (1920 x 1080 design units, src/hudscale.h); `px`: the width it is drawn at in real
// pixels (the viewport, docs/worldmap-select-re.md §6), 0 when not known yet (then taken as `w`). Only the sharpness
// (MostZoom, LevelFor) depends on it: positions are in design units.
struct Screen { float w; float h; float px=0.f; };
inline float PixelWidth(const Screen& s) noexcept { return s.px>0.f ? s.px : s.w; }
// The deepest level's pixels one to one on the screen: zooming further would only show them bigger (blurred).
inline float MostZoom(const Screen& s) noexcept { return kDeepestWidth/PixelWidth(s); }
// The map is twice as wide as tall: zoom 1 shows the world's width; the least zoom keeps it filling the screen's height.
inline float LeastZoom(const Screen& s) noexcept { return s.h*2.f/s.w>1.f ? s.h*2.f/s.w : 1.f; }
inline void Clamp(View& v,const Screen& s) noexcept {
    const float least=LeastZoom(s);
    if(!(v.zoom>=least))v.zoom=least;
    if(v.zoom>MostZoom(s))v.zoom=MostZoom(s);
    v.cu-=std::floor(v.cu);
    const float halfV=s.h/(s.w*v.zoom);   // half the screen's height in v (the map's v spans half its u)
    if(v.cv<halfV)v.cv=halfV;
    if(v.cv>1.f-halfV)v.cv=1.f-halfV;
}
// Screen pixels of a map point (u wrapped to the copy nearest the centre).
struct Pixel { float x; float y; };
inline Pixel ToScreen(const View& v,const Screen& s,MapPoint p) noexcept {
    float du=p.u-v.cu;
    du-=std::floor(du+0.5f);
    const float scale=s.w*v.zoom;   // pixels per map width
    return Pixel{s.w*0.5f+du*scale,s.h*0.5f+(p.v-v.cv)*scale*0.5f};
}
inline MapPoint FromScreen(const View& v,const Screen& s,Pixel px) noexcept {
    const float scale=s.w*v.zoom;
    float u=v.cu+(px.x-s.w*0.5f)/scale;
    u-=std::floor(u);
    return MapPoint{u,v.cv+(px.y-s.h*0.5f)/(scale*0.5f)};
}
// Zooms by `factor` keeping the map point under `at` where it is.
inline void ZoomAt(View& v,const Screen& s,Pixel at,float factor) noexcept {
    const MapPoint before=FromScreen(v,s,at);
    v.zoom*=factor;
    Clamp(v,s);
    const MapPoint after=FromScreen(v,s,at);
    float du=before.u-after.u;
    du-=std::floor(du+0.5f);
    v.cu+=du;v.cv+=before.v-after.v;
    Clamp(v,s);
}
inline void Pan(View& v,const Screen& s,float dx,float dy) noexcept {
    const float scale=s.w*v.zoom;
    v.cu-=dx/scale;v.cv-=dy/(scale*0.5f);
    Clamp(v,s);
}
// The level to draw: the coarsest whose pixels are no larger than the screen's (the map's on-screen width in real
// pixels, PixelWidth * zoom).
inline int LevelFor(const View& v,const Screen& s) noexcept {
    const float want=PixelWidth(s)*v.zoom;
    int z=0;
    while(z<kTileLevels-1 && static_cast<float>(kTile)*static_cast<float>(1<<z)<want)++z;
    return z;
}
// The tiles the view shows at `level`: columns x0..x1 (unwrapped: may run below 0 or past the last column across the
// antimeridian; the tile is column x mod TileColumns), rows y0..y1 (clamped).
struct TileSpan { int level,x0,x1,y0,y1; };
inline TileSpan Visible(const View& v,const Screen& s,int level) noexcept {
    const float scale=s.w*v.zoom;
    const float cols=static_cast<float>(TileColumns(level)),rows=static_cast<float>(TileRows(level));
    const float u0=v.cu-s.w*0.5f/scale,u1=v.cu+s.w*0.5f/scale;
    const float v0=v.cv-s.h*0.5f/(scale*0.5f),v1=v.cv+s.h*0.5f/(scale*0.5f);
    auto row=[&](float vv) {
        const int y=static_cast<int>(std::floor(vv*rows));
        return y<0 ? 0 : (y>=TileRows(level) ? TileRows(level)-1 : y);
    };
    return TileSpan{level,static_cast<int>(std::floor(u0*cols)),static_cast<int>(std::floor(u1*cols)),row(v0),row(v1)};
}
// The view framing every point in `p` (n >= 1) with `margin` (a share of the screen) on each side; u spread taken the
// short way round.
inline View Frame(const MapPoint* p,int n,const Screen& s,float margin=0.15f) noexcept {
    View v;
    if(n<=0 || !p){Clamp(v,s);return v;}
    float lo=0.f,hi=0.f,top=p[0].v,bottom=p[0].v;
    for(int i=1;i<n;++i) {
        float du=p[i].u-p[0].u;
        du-=std::floor(du+0.5f);
        if(du<lo)lo=du;
        if(du>hi)hi=du;
        if(p[i].v<top)top=p[i].v;
        if(p[i].v>bottom)bottom=p[i].v;
    }
    v.cu=p[0].u+(lo+hi)*0.5f;v.cv=(top+bottom)*0.5f;
    const float spanU=hi-lo,spanV=bottom-top;
    const float fit=1.f-2.f*margin;
    float zoom=MostZoom(s);
    if(spanU>0.f && fit/spanU<zoom)zoom=fit/spanU;
    if(spanV>0.f && fit*s.h*2.f/(s.w*spanV)<zoom)zoom=fit*s.h*2.f/(s.w*spanV);
    v.zoom=zoom;
    Clamp(v,s);
    return v;
}

// --- The route ---
struct LatLon { float lat; float lon; };
// `n` >= 2 points along the shorter great circle a -> b, both ends included; the count written (0 when n < 2).
inline int GreatCircle(LatLon a,LatLon b,LatLon* out,int n) noexcept {
    if(!out || n<2)return 0;
    const float d2r=kPi/180.f;
    auto vec=[&](LatLon p,float* x) {
        const float la=p.lat*d2r,lo=p.lon*d2r;
        x[0]=std::cos(la)*std::cos(lo);x[1]=std::cos(la)*std::sin(lo);x[2]=std::sin(la);
    };
    float va[3],vb[3];
    vec(a,va);vec(b,vb);
    float dot=va[0]*vb[0]+va[1]*vb[1]+va[2]*vb[2];
    dot=dot>1.f ? 1.f : dot<-1.f ? -1.f : dot;
    const float w=std::acos(dot);
    for(int i=0;i<n;++i) {
        const float t=static_cast<float>(i)/static_cast<float>(n-1);
        float x[3];
        if(w<1e-6f){x[0]=va[0];x[1]=va[1];x[2]=va[2];}
        else {
            const float s0=std::sin((1.f-t)*w)/std::sin(w),s1=std::sin(t*w)/std::sin(w);
            for(int c=0;c<3;++c)x[c]=s0*va[c]+s1*vb[c];
        }
        out[i]=LatLon{std::atan2(x[2],std::sqrt(x[0]*x[0]+x[1]*x[1]))/d2r,std::atan2(x[1],x[0])/d2r};
    }
    out[0]=a;out[n-1]=b;
    return n;
}
// Splits a projected polyline where it jumps across the map's east-west seam: `breakBefore[i]` is true when segment
// i-1 -> i crosses it (draw no line there). Returns how many breaks.
inline int SeamBreaks(const MapPoint* p,int n,bool* breakBefore) noexcept {
    int k=0;
    for(int i=0;i<n;++i) {
        const bool b=i>0 && std::fabs(p[i].u-p[i-1].u)>0.5f;
        if(breakBefore)breakBefore[i]=b;
        k+=b ? 1 : 0;
    }
    return k;
}

// --- Markers sharing a place ---
// The k-th marker (k >= 0) of a place: offset in pixels on a golden-angle spiral, `step` pixels apart (k = 0 centred).
inline Pixel Fan(int k,float step) noexcept {
    if(k<=0)return Pixel{0.f,0.f};
    const float golden=2.39996323f;   // pi * (3 - sqrt 5)
    const float r=step*std::sqrt(static_cast<float>(k));
    return Pixel{r*std::cos(golden*static_cast<float>(k)),r*std::sin(golden*static_cast<float>(k))};
}
// The nearest of `n` screen points to `at` within `radius` pixels, else -1 (ties: the lower index).
inline int Pick(const Pixel* p,int n,Pixel at,float radius) noexcept {
    int best=-1;
    float bestD=radius*radius;
    for(int i=0;i<n;++i) {
        const float dx=p[i].x-at.x,dy=p[i].y-at.y,d=dx*dx+dy*dy;
        if(d<=bestD && (best<0 || d<bestD)){best=i;bestD=d;}
    }
    return best;
}

// --- Which rows show, which routes ---
// The stock select builds one entry per row with a state (docs/mission-list-re.md, 0x8A1530: 0 cleared, 1 open, 2 open
// branch, 3 shown locked, 4 hidden). The map shows 0..3 and routes from `from` (the current mission) to each of its
// successors (MISSIONLIST field 3) that is open (1 / 2), or with `openOnly` false that is shown at all (0..3: the
// select's draw; in a linear run the current mission is the newest open one, so its successors are still locked and
// an open-only route would almost never show). Returns how many written to `out` (at most `capacity`).
inline bool Shown(int state) noexcept { return state>=0 && state<=3; }
inline bool Open(int state) noexcept { return state==1 || state==2; }
inline int RouteTargets(int from,const int* successors,int count,const int* state,int rows,int* out,int capacity,
                        bool openOnly=true) noexcept {
    if(from<0 || from>=rows || !successors || !state || !out)return 0;
    int n=0;
    for(int i=0;i<count && n<capacity;++i) {
        const int to=successors[i];
        if(to<0 || to>=rows || to==from || !(openOnly ? Open(state[to]) : Shown(state[to])))continue;
        bool dup=false;
        for(int j=0;j<n;++j)dup=dup || out[j]==to;
        if(!dup)out[n++]=to;
    }
    return n;
}

// --- Drawing the pyramid (src/worldmap.cpp, the select's draw) ---
// A tile's part of the map: u0..u1 east (x may be unwrapped: past the grid across the antimeridian), v0..v1 south.
struct MapRect { float u0,v0,u1,v1; };
inline MapRect TileRect(int level,int x,int y) noexcept {
    const float cols=static_cast<float>(TileColumns(level)),rows=static_cast<float>(TileRows(level));
    return MapRect{static_cast<float>(x)/cols,static_cast<float>(y)/rows,
                   static_cast<float>(x+1)/cols,static_cast<float>(y+1)/rows};
}
inline int WrapColumn(int level,int x) noexcept {
    const int cols=TileColumns(level);
    return ((x%cols)+cols)%cols;
}
// The tile (level, x, y) (x wrapped) drawn from the deepest of it and its ancestors whose table entry `has(index)`:
// that level (-1: none, not even level 0), its tile in *ax / *ay, and the part of that tile's texture (0..1) that is
// the wanted tile's area in *uv. A level's tile holds exactly 2 x 2 of the next one's (level 0's one tile: level 1's
// two), so the ancestor d levels up is (x >> d, y >> d). This is the pyramid's normal coarse-to-fine: a deep tile
// not decoded yet shows its area from the finest of what is there.
template<class Has> int Cover(int level,int x,int y,Has has,int* ax,int* ay,MapRect* uv) noexcept {
    for(int z=level;z>=0;--z) {
        const int d=level-z,px=x>>d,py=y>>d;
        if(!has(TileIndex(z,px,py)))continue;
        if(ax)*ax=px;
        if(ay)*ay=py;
        if(uv) {
            const MapRect t=TileRect(level,x,y),a=TileRect(z,px,py);
            const float w=a.u1-a.u0,h=a.v1-a.v0;
            *uv=MapRect{(t.u0-a.u0)/w,(t.v0-a.v0)/h,(t.u1-a.u0)/w,(t.v1-a.v0)/h};
        }
        return z;
    }
    return -1;
}
// A map u (unwrapped: the copy near the view's centre) and v to design pixels. The tiles' edges all come from this
// one formula, so neighbours share their edge bit for bit (no gap, no overlap).
inline Pixel ToScreenUnwrapped(const View& v,const Screen& s,float u,float vv) noexcept {
    const float scale=s.w*v.zoom;
    return Pixel{s.w*0.5f+(u-v.cu)*scale,s.h*0.5f+(vv-v.cv)*scale*0.5f};
}
// The tiles of `span` in the order to fetch them: nearest the screen's centre first. Writes at most `capacity`
// {x (unwrapped), y} pairs into `xy`; returns how many.
inline int CentreFirst(const View& v,const Screen& s,const TileSpan& span,int* xy,int capacity) noexcept {
    int n=0;
    for(int y=span.y0;y<=span.y1;++y)for(int x=span.x0;x<=span.x1 && n<capacity;++x){xy[2*n]=x;xy[2*n+1]=y;++n;}
    auto dist=[&](int i) {
        const MapRect r=TileRect(span.level,xy[2*i],xy[2*i+1]);
        const Pixel c=ToScreenUnwrapped(v,s,(r.u0+r.u1)*0.5f,(r.v0+r.v1)*0.5f);
        const float dx=c.x-s.w*0.5f,dy=c.y-s.h*0.5f;
        return dx*dx+dy*dy;
    };
    for(int i=1;i<n;++i)   // insertion sort: a dozen tiles
        for(int j=i;j>0 && dist(j)<dist(j-1);--j){std::swap(xy[2*j],xy[2*j-2]);std::swap(xy[2*j+1],xy[2*j-1]);}
    return n;
}

// --- The texture cache's eviction ---
// The cached tile to drop for a new one: never a resident one (levels below `resident`: 0 and 1, the three tiles
// under everything) nor one drawn in frame `now`; of the rest the least recently drawn, a tie the deeper level (the
// cheapest to be without: its parent shows its area). -1: none may go (the new tile waits).
inline int Victim(const int* level,const unsigned* used,int n,unsigned now,int resident=2) noexcept {
    int best=-1;
    for(int i=0;i<n;++i) {
        if(level[i]<resident || used[i]==now)continue;
        if(best<0 || used[i]<used[best] || (used[i]==used[best] && level[i]>level[best]))best=i;
    }
    return best;
}

// --- The opening animation ---
// Where the map shows what it is about (a place framed, LB / LT's zoom): the middle of its open area, left of the
// select's kept windows (OverPanels: x from 842).
inline constexpr Pixel kMapFocus{421.f,540.f};
// A view of one place: `spanDeg` degrees of longitude across the screen (the region round it), the place at `at`
// (as far as the clamp lets it: not past a pole), not past MostZoom.
inline View FramePlace(MapPoint p,const Screen& s,float spanDeg=24.f,Pixel at=kMapFocus) noexcept {
    View v;
    v.zoom=360.f/spanDeg;
    Clamp(v,s);
    const float scale=s.w*v.zoom;
    v.cu=p.u-(at.x-s.w*0.5f)/scale;
    v.cv=p.v-(at.y-s.h*0.5f)/(scale*0.5f);
    Clamp(v,s);
    return v;
}
// The whole Earth as far as it fits (LeastZoom: the map's full height), centred on longitude `u`.
inline View Whole(float u,const Screen& s) noexcept {
    View v;
    v.cu=u;v.cv=0.5f;v.zoom=LeastZoom(s);
    Clamp(v,s);
    return v;
}
inline float Ease(float t) noexcept {
    t=t<0.f ? 0.f : t>1.f ? 1.f : t;
    return t*t*(3.f-2.f*t);
}
// `a` to `b` at time t (0..1, eased): the centre the short way round, the zoom by its logarithm (a steady approach).
inline View Blend(const View& a,const View& b,float t,const Screen& s) noexcept {
    const float e=Ease(t);
    float du=b.cu-a.cu;
    du-=std::floor(du+0.5f);
    View v;
    v.cu=a.cu+du*e;
    v.cv=a.cv+(b.cv-a.cv)*e;
    v.zoom=a.zoom*std::pow(b.zoom/a.zoom,e);
    Clamp(v,s);
    return v;
}

// --- Input ---
// What the map takes (src/worldmap.cpp reads the devices): a mouse drag pans, the wheel zooms about the pointer, the
// right stick pans, LB / LT (keyboard Q / E) zoom out / in about the focus (RB / RT are the briefing's scroll).
inline constexpr float kStickDead=0.24f;
inline constexpr float kStickPanPerSec=900.f;    // design pixels a second at full deflection
inline constexpr float kHoldZoomPerSec=1.5f;     // octaves (x2) a second with LB / LT held
inline constexpr float kWheelNotchZoom=0.5f;     // octaves a wheel notch (120)
inline float Stick(short raw) noexcept {
    const float f=static_cast<float>(raw)/32767.f;
    const float a=f<0.f ? -f : f;
    if(a<kStickDead)return 0.f;
    float r=(a-kStickDead)/(1.f-kStickDead);
    if(r>1.f)r=1.f;
    return f<0.f ? -r : r;
}
// The drag to Pan for a stick (x right, y up as XInput gives it): the view moves the way it is pushed, so the map
// goes the other way.
inline Pixel StickPan(float x,float y,float dt) noexcept {
    return Pixel{-x*kStickPanPerSec*dt,y*kStickPanPerSec*dt};
}
// The zoom factor for LB (out) / LT (in) held `dt` s; 1 with both or neither.
inline float HoldZoom(bool out,bool in,float dt) noexcept {
    const float dir=(in ? 1.f : 0.f)-(out ? 1.f : 0.f);
    return std::exp2(dir*kHoldZoomPerSec*dt);
}
// The zoom factor for a wheel delta (WM_MOUSEWHEEL units: 120 a notch, forward positive: nearer).
inline float WheelZoom(int delta) noexcept {
    return std::exp2(static_cast<float>(delta)/120.f*kWheelNotchZoom);
}
// The select's windows the stock draw keeps over the map (lyt_HUiHQMissionSelect.sgo: Window02_SoldierSel1 at
// (842, 223) 540 x 274, Window02_SoldierSel2 at (842, 497) 540 x 354; docs/worldmap-select-re.md §2): the mouse
// there is theirs (the briefing's wheel / drag scroll), not the map's.
inline bool OverPanels(Pixel p) noexcept { return p.x>=842.f && p.x<842.f+540.f && p.y>=223.f && p.y<497.f+354.f; }
inline bool OnScreen(Pixel p,const Screen& s) noexcept { return p.x>=0.f && p.y>=0.f && p.x<s.w && p.y<s.h; }
// A client pixel (cx, cy) of a cw x ch client area to design units, through the render target (rw x rh: the client
// scaled to it) and the viewport the select is drawn in (vp: x, y, w, h in target pixels). Off screen when unknown.
inline Pixel ClientToDesign(float cx,float cy,float cw,float ch,float rw,float rh,const float* vp,const Screen& s) noexcept {
    if(!(cw>0.f) || !(ch>0.f) || !(vp[2]>0.f) || !(vp[3]>0.f))return Pixel{-1.f,-1.f};
    const float rx=cx*rw/cw,ry=cy*rh/ch;
    return Pixel{(rx-vp[0])*s.w/vp[2],(ry-vp[1])*s.h/vp[3]};
}


// --- Stage 2: the pins, the panel, the routes, choosing (src/worldmap.cpp) ---
// The panel of the chosen place: right of the kept windows, where their window skins draw a large empty frame (the
// .B / .c meshes of Window02_SoldierSel1 / 2 reach x 1920; docs/worldmap-select-re.md §8.1): the map is drawn again
// there over the stock draw and the panel on it. A heading (the place), then a line a mission.
struct Area { float x0,y0,x1,y1; };
inline constexpr Area kPanel{1396.f,196.f,1860.f,884.f};   // clear of the main frame's corner pieces
inline constexpr float kPanelHeading=60.f;   // the place's name
inline constexpr float kPanelRow=40.f;       // a mission's line
inline constexpr int kPanelRows=static_cast<int>((kPanel.y1-kPanel.y0-kPanelHeading)/kPanelRow);
// Where the map is seen whole: left of the kept windows, under the title bar, over the key guide.
inline constexpr Area kOpenArea{36.f,140.f,806.f,930.f};
inline bool In(Area a,Pixel p) noexcept { return p.x>=a.x0 && p.x<a.x1 && p.y>=a.y0 && p.y<a.y1; }
// The mouse over the panel is the panel's (a line clicked), not the map's.
inline bool OverPanel(Pixel p) noexcept { return In(kPanel,p); }

// A state's worth for a place's pin (the one it shows): open 1 > branch 2 > cleared 0 > locked 3 (lower is better).
inline int StateRank(int state) noexcept {
    switch(state) { case 1: return 0; case 2: return 1; case 0: return 2; case 3: return 3; default: return 4; }
}
inline std::uint32_t StateColour(int state) noexcept {
    switch(state) {
    case 1: return 0xFFFFD23Cu;    // open: yellow
    case 2: return 0xFFFF8C1Eu;    // open branch: orange
    case 0: return 0xFFA8A8A8u;    // cleared: grey
    default: return 0xFF9A2020u;   // locked: dark red
    }
}
// May be chosen (the stock confirm takes it; a cleared one is played again); a locked one only says it is not open.
inline bool Enterable(int state) noexcept { return state>=0 && state<=2; }

// One pin a place: its missions on the display list counted, the best state, its first entry.
struct Pin { int place; int count; int state; int first; };
// The pins of a display list (entry i: place[i], -1 none; state[i]) in the order their places first appear; at most
// `capacity`.
inline int GroupPins(const int* place,const int* state,int n,Pin* out,int capacity) noexcept {
    int pins=0;
    for(int i=0;i<n;++i) {
        if(place[i]<0 || !Shown(state[i]))continue;
        int k=0;
        while(k<pins && out[k].place!=place[i])++k;
        if(k==pins) {
            if(pins>=capacity)continue;
            out[pins++]=Pin{place[i],0,state[i],i};
        }
        ++out[k].count;
        if(StateRank(state[i])<StateRank(out[k].state))out[k].state=state[i];
    }
    return pins;
}
// The entry to put the cursor on at place `p`: its first open one (1 / 2), else its first cleared one; -1 when it has
// only locked ones.
inline int FirstEnterable(const int* place,const int* state,int n,int p) noexcept {
    int cleared=-1;
    for(int i=0;i<n;++i) {
        if(place[i]!=p)continue;
        if(Open(state[i]))return i;
        if(state[i]==0 && cleared<0)cleared=i;
    }
    return cleared;
}
// The entries at place `p`, in list order, into out (at most capacity); how many.
inline int EntriesAt(const int* place,int n,int p,int* out,int capacity) noexcept {
    int k=0;
    for(int i=0;i<n && k<capacity;++i)if(place[i]==p)out[k++]=i;
    return k;
}

// Left / right: of the pins marked `ok`, the nearest to pin `from` on screen in direction `dir` (-1 left, +1 right):
// ahead by more than 2 px, a vertical offset counting 2.5 times a horizontal one (a neighbour across wins over one far
// up or down). -1: none that way.
inline int Neighbour(const Pixel* at,const bool* ok,int n,int from,int dir) noexcept {
    if(from<0 || from>=n)return -1;
    int best=-1;
    float bestScore=0.f;
    for(int i=0;i<n;++i) {
        if(i==from || !ok[i])continue;
        const float dx=(at[i].x-at[from].x)*static_cast<float>(dir),dy=at[i].y-at[from].y;
        if(dx<=2.f)continue;
        const float score=dx+2.5f*(dy<0.f ? -dy : dy);
        if(best<0 || score<bestScore){best=i;bestScore=score;}
    }
    return best;
}

// The panel's list: the first line shown so line `cursor` (of `count`; -1 none) is among `visible`, kept near the
// middle while the list scrolls.
inline int ScrollFirst(int count,int cursor,int visible) noexcept {
    if(count<=visible || visible<=0 || cursor<0)return 0;
    int first=cursor-visible/2;
    if(first<0)first=0;
    if(first>count-visible)first=count-visible;
    return first;
}
// The panel line under `p` (0 = the first shown), -1 none.
inline int PanelLineAt(Pixel p,int shown) noexcept {
    if(!OverPanel(p))return -1;
    const float y=p.y-(kPanel.y0+kPanelHeading);
    if(y<0.f)return -1;
    const int line=static_cast<int>(y/kPanelRow);
    return line<shown ? line : -1;
}
// A row's stock number (MissionSelect_Format's %NUM%, docs/worldmap-select-re.md §4): row + 1, "???" for a branch
// row (field 5 = 1). Written to out; its length (0: out too small).
inline int MissionNumber(int row,int field5,wchar_t* out,std::size_t capacity) noexcept {
    if(capacity<4)return 0;
    if(field5==1){out[0]=out[1]=out[2]=L'?';out[3]=0;return 3;}
    wchar_t tmp[12];
    int k=0;
    unsigned v=static_cast<unsigned>(row<0 ? 0 : row)+1u;
    do{tmp[k++]=static_cast<wchar_t>(L'0'+v%10u);v/=10u;}while(v && k<12);
    if(static_cast<std::size_t>(k)>=capacity)return 0;
    for(int i=0;i<k;++i)out[i]=tmp[k-1-i];
    out[k]=0;
    return k;
}

// A view framing the points `p` (n >= 1) inside `area` (design units) `margin` px in from its edges, no nearer than
// `spanDeg` degrees of longitude across the screen (one place alone shows its region), not past MostZoom.
inline View FrameIn(const MapPoint* p,int n,const Screen& s,Area area=kOpenArea,float margin=60.f,float spanDeg=24.f) noexcept {
    if(n<=0 || !p)return Whole(0.5f,s);
    float lo=0.f,hi=0.f,top=p[0].v,bottom=p[0].v;
    for(int i=1;i<n;++i) {
        float du=p[i].u-p[0].u;
        du-=std::floor(du+0.5f);
        if(du<lo)lo=du;
        if(du>hi)hi=du;
        if(p[i].v<top)top=p[i].v;
        if(p[i].v>bottom)bottom=p[i].v;
    }
    const float w=area.x1-area.x0-2.f*margin,h=area.y1-area.y0-2.f*margin;
    float zoom=360.f/spanDeg;
    // On screen a map width is s.w * zoom px, the map's height (v 0..1) half that.
    if(hi-lo>0.f && w/(s.w*(hi-lo))<zoom)zoom=w/(s.w*(hi-lo));
    if(bottom-top>0.f && h/(s.w*0.5f*(bottom-top))<zoom)zoom=h/(s.w*0.5f*(bottom-top));
    View v;
    v.zoom=zoom;
    Clamp(v,s);
    const float scale=s.w*v.zoom;
    const Pixel at{(area.x0+area.x1)*0.5f,(area.y0+area.y1)*0.5f};
    v.cu=p[0].u+(lo+hi)*0.5f-(at.x-s.w*0.5f)/scale;
    v.cv=(top+bottom)*0.5f-(at.y-s.h*0.5f)/(scale*0.5f);
    Clamp(v,s);
    return v;
}
// `v` panned (its zoom kept) so `p` sits at `at`.
inline View CentreOn(View v,MapPoint p,const Screen& s,Pixel at=kMapFocus) noexcept {
    Clamp(v,s);
    const float scale=s.w*v.zoom;
    v.cu=p.u-(at.x-s.w*0.5f)/scale;
    v.cv=p.v-(at.y-s.h*0.5f)/(scale*0.5f);
    Clamp(v,s);
    return v;
}

// A route's projected polyline made continuous across the antimeridian: where SeamBreaks finds a jump the points after
// it shift a map width. The map wraps, so the line goes on over the seam (drawn at its copies u - 1, u, u + 1).
inline void Unwrap(MapPoint* p,int n) noexcept {
    bool breaks[128];
    if(n>128)n=128;
    if(n<2 || SeamBreaks(p,n,breaks)==0)return;
    float shift=0.f;
    float last=p[0].u;
    for(int i=1;i<n;++i) {
        const float raw=p[i].u;
        if(breaks[i])shift+=raw<last ? 1.f : -1.f;
        last=raw;
        p[i].u=raw+shift;
    }
}
// The current mission's ring: 0..1 over `period` seconds, then again.
inline float Pulse(float seconds,float period=1.6f) noexcept {
    const float t=seconds/period;
    return t-std::floor(t);
}

}  // namespace worldmap
