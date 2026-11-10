// src/worldmap_logic.h: the world map's table, projection, view, routes, marker fan and picking (offline).
#include "../src/worldmap_logic.h"

#include <cmath>
#include <cstdio>
#include <cwchar>

using namespace worldmap;

namespace {
int checks=0,failures=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL: %s\n",what);}
}
bool Near(float a,float b,float eps=1e-3f) { return std::fabs(a-b)<=eps; }

void Table() {
    Expect(kPlaceCount>=40 && kMissionCount==483,"45 places, 483 missions (EDF6 206, EDF5 139, EDF4.1 138)");
    bool ranges=true,names=true;
    for(const Place& p:kPlaces) {
        ranges=ranges && p.lat>=24.f && p.lat<=46.f && p.lon>=122.f && p.lon<=146.f;   // Japan
        for(const wchar_t* n:p.name)names=names && n && *n;
    }
    Expect(ranges,"every place in Japan");
    Expect(names,"every place named in the four HUD languages");
    bool inRange=true;
    for(const Mission& m:kMissions)inRange=inRange && m.place>=0 && m.place<kPlaceCount;
    Expect(inRange,"every mission's place an index");
    Expect(PlaceOfKey(L"EDF6/M000B")>=0 && !std::wcscmp(kPlaces[PlaceOfKey(L"EDF6/M000B")].id,L"gotemba"),
           "M000B (the EDF basement) at the HQ");
    Expect(PlaceOfKey(L"edf6/m000b")==PlaceOfKey(L"EDF6/M000B") && PlaceOfKey(L"EDF6\\M000B")==PlaceOfKey(L"EDF6/M000B"),
           "keys case and slash blind");
    Expect(PlaceOfKey(L"EDF5_OLD_SCRIPT/M001")>=0 && PlaceOfKey(L"EDF41/M190")>=0 && PlaceOfKey(L"EDF6_DLC/M203")>=0,
           "EDF5, EDF4.1 and mission pack keys");
    Expect(PlaceOfKey(L"EDF6/EDF6TR_RANGE")==kRangePlace && PlaceOfKey(L"EDF6/EDF6TR_PVP_X")==kRangePlace,
           "the test range rows at the training ground");
    Expect(PlaceOfKey(L"EDF6/NOPE")==-1 && PlaceOfKey(nullptr)==-1 && PlaceOfKey(L"")==-1,"unknown: -1");
}

void Projection() {
    const MapPoint a=Project(0.f,0.f),b=Project(90.f,-180.f),c=Project(-90.f,180.f);
    Expect(Near(a.u,0.5f) && Near(a.v,0.5f),"0,0 at the centre");
    Expect(Near(b.u,0.f) && Near(b.v,0.f),"90N 180W at the top left");
    Expect(Near(c.u,0.f) && Near(c.v,1.f),"180E wraps to u 0");
    const MapPoint t=Project(35.69f,139.75f);
    Expect(Near(t.u,(139.75f+180.f)/360.f) && Near(t.v,(90.f-35.69f)/180.f),"Tokyo");
}

void Views() {
    const Screen s{1920.f,1080.f};
    View v;v.zoom=0.1f;
    Clamp(v,s);
    Expect(Near(v.zoom,LeastZoom(s)) && v.zoom>=1.f,"zoom never below filling the height");
    v.zoom=1000.f;Clamp(v,s);
    Expect(Near(v.zoom,kMostZoom),"zoom capped");
    v=View{};v.zoom=4.f;v.cv=0.f;Clamp(v,s);
    const float halfV=s.h/(s.w*4.f);
    Expect(Near(v.cv,halfV),"no sky above the north pole");
    // Round trip.
    v=View{};v.zoom=8.f;v.cu=0.3f;v.cv=0.4f;Clamp(v,s);
    const MapPoint p{0.31f,0.41f};
    const Pixel px=ToScreen(v,s,p);
    const MapPoint q=FromScreen(v,s,px);
    Expect(Near(q.u,p.u) && Near(q.v,p.v),"screen round trip");
    // u wraps to the nearest copy.
    v.cu=0.99f;Clamp(v,s);
    const Pixel w=ToScreen(v,s,MapPoint{0.01f,v.cv});
    Expect(Near(w.x,s.w*0.5f+0.02f*s.w*v.zoom,0.5f),"across the seam: the near copy");
    // Zoom about the cursor keeps the point under it.
    v=View{};v.zoom=2.f;v.cu=0.8f;v.cv=0.3f;Clamp(v,s);
    const Pixel at{1500.f,300.f};
    const MapPoint under=FromScreen(v,s,at);
    ZoomAt(v,s,at,2.f);
    const MapPoint still=FromScreen(v,s,at);
    Expect(Near(still.u,under.u,1e-4f) && Near(still.v,under.v,1e-4f) && Near(v.zoom,4.f),"zoom about the cursor");
    // Pan moves the map with the drag.
    const Pixel before=ToScreen(v,s,under);
    Pan(v,s,100.f,0.f);
    const Pixel after=ToScreen(v,s,under);
    Expect(Near(after.x-before.x,100.f,0.05f),"pan by the drag");
    // Framing every place: all on screen.
    MapPoint pts[kPlaceCount];
    for(int i=0;i<kPlaceCount;++i)pts[i]=Project(kPlaces[i].lat,kPlaces[i].lon);
    const View f=Frame(pts,kPlaceCount,s);
    bool inside=true;
    for(int i=0;i<kPlaceCount;++i){const Pixel x=ToScreen(f,s,pts[i]);inside=inside && x.x>=0 && x.x<=s.w && x.y>=0 && x.y<=s.h;}
    Expect(inside && f.zoom>8.f,"the framed view shows every place, zoomed in on Japan");
}

void Routes() {
    LatLon out[16];
    const LatLon tokyo{35.69f,139.75f},sapporo{43.06f,141.35f};
    Expect(GreatCircle(tokyo,sapporo,out,16)==16,"16 points");
    Expect(Near(out[0].lat,tokyo.lat) && Near(out[15].lon,sapporo.lon),"ends exact");
    bool monotone=true;
    for(int i=1;i<16;++i)monotone=monotone && out[i].lat>out[i-1].lat-1e-4f;
    Expect(monotone,"northwards all the way");
    Expect(GreatCircle(tokyo,tokyo,out,4)==4 && Near(out[2].lat,tokyo.lat),"same place: a point");
    Expect(GreatCircle(tokyo,sapporo,out,1)==0,"fewer than 2: none");
    // Across the antimeridian: Tokyo -> Honolulu goes east over 180, not west round the world.
    const LatLon honolulu{21.31f,-157.86f};
    GreatCircle(tokyo,honolulu,out,16);
    MapPoint mp[16];
    for(int i=0;i<16;++i)mp[i]=Project(out[i].lat,out[i].lon);
    bool breaks[16];
    Expect(SeamBreaks(mp,16,breaks)==1,"one break at the seam");
    bool east=true;
    for(int i=1;i<16;++i)east=east && (breaks[i] || mp[i].u>mp[i-1].u);
    Expect(east,"eastwards the short way");
}

void Markers() {
    Expect(Near(Fan(0,10.f).x,0.f) && Near(Fan(0,10.f).y,0.f),"the first centred");
    float least=1e9f;
    Pixel f[30];
    for(int i=0;i<30;++i)f[i]=Fan(i,12.f);
    for(int i=0;i<30;++i)for(int j=i+1;j<30;++j){const float dx=f[i].x-f[j].x,dy=f[i].y-f[j].y;const float d=std::sqrt(dx*dx+dy*dy);if(d<least)least=d;}
    Expect(least>=6.f,"30 markers on one place stay apart");
    const Pixel p[3]={{0,0},{10,0},{100,100}};
    Expect(Pick(p,3,Pixel{8,1},20.f)==1,"the nearest");
    Expect(Pick(p,3,Pixel{50,50},20.f)==-1,"none within the radius");
    Expect(Pick(p,3,Pixel{5,0},20.f)==0,"a tie: the lower index");
}

void Rows() {
    const int state[6]={0,1,2,3,4,1};
    const int succ[5]={1,2,3,9,1};
    int out[4];
    Expect(RouteTargets(0,succ,5,state,6,out,4)==2 && out[0]==1 && out[1]==2,"open successors only, once each");
    Expect(RouteTargets(7,succ,5,state,6,out,4)==0,"from outside the list: none");
    Expect(RouteTargets(0,succ,5,state,6,out,1)==1,"capacity kept");
    Expect(Shown(0) && Shown(3) && !Shown(4) && Open(2) && !Open(0),"states");
}
}  // namespace

int main() {
    Table();
    Projection();
    Views();
    Routes();
    Markers();
    Rows();
    std::printf("%d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
