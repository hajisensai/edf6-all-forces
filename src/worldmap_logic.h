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
#pragma once
#include <cmath>
#include <cstddef>
#include <cwchar>

namespace worldmap {

enum class Region : int { hokkaido, tohoku, kanto, chubu, kansai, chugoku, kyushu };
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

// --- The view ---
struct View {
    float cu=0.5f,cv=0.5f;   // the map point at the screen's centre
    float zoom=1.f;          // map widths across the screen: 1 the whole world, more nearer
};
struct Screen { float w; float h; };   // the layout the map is drawn on (1920 x 1080, src/hudscale.h)
inline constexpr float kMostZoom=64.f;
// The map is twice as wide as tall: zoom 1 shows the world's width; the least zoom keeps it filling the screen's height.
inline float LeastZoom(const Screen& s) noexcept { return s.h*2.f/s.w>1.f ? s.h*2.f/s.w : 1.f; }
inline void Clamp(View& v,const Screen& s) noexcept {
    const float least=LeastZoom(s);
    if(!(v.zoom>=least))v.zoom=least;
    if(v.zoom>kMostZoom)v.zoom=kMostZoom;
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
    float zoom=kMostZoom;
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
// successors (MISSIONLIST field 3) that is open (1 / 2). Returns how many written to `out` (at most `capacity`).
inline bool Shown(int state) noexcept { return state>=0 && state<=3; }
inline bool Open(int state) noexcept { return state==1 || state==2; }
inline int RouteTargets(int from,const int* successors,int count,const int* state,int rows,int* out,int capacity) noexcept {
    if(from<0 || from>=rows || !successors || !state || !out)return 0;
    int n=0;
    for(int i=0;i<count && n<capacity;++i) {
        const int to=successors[i];
        if(to<0 || to>=rows || to==from || !Open(state[to]))continue;
        bool dup=false;
        for(int j=0;j<n;++j)dup=dup || out[j]==to;
        if(!dup)out[n++]=to;
    }
    return n;
}

}  // namespace worldmap
