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
        const bool japan=p.region!=Region::europe && p.region!=Region::africa;
        ranges=ranges && (!japan || (p.lat>=24.f && p.lat<=46.f && p.lon>=122.f && p.lon<=146.f));
        for(const wchar_t* n:p.name)names=names && n && *n;
    }
    Expect(ranges,"every Japanese region's place in Japan");
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

void Tiles() {
    Expect(kTile==1350 && kTileLevels==7,"the pyramid of pylib/worldmap.py");
    Expect(Near(kDeepestWidth,86400.f),"the deepest level: Blue Marble's 240 px / degree");
    Expect(TileColumns(0)==1 && TileRows(0)==1 && TileColumns(6)==64 && TileRows(6)==32,"the grid");
    Expect(kTileCount==2731,"every level's tiles");
    Expect(TileIndex(0,0,0)==0 && TileIndex(1,0,0)==1 && TileIndex(1,1,0)==2 && TileIndex(2,0,0)==3,"table order");
    Expect(TileIndex(6,63,31)==kTileCount-1,"the last tile");
    Expect(LevelOfIndex(0)==0 && LevelOfIndex(1)==1 && LevelOfIndex(2)==1 && LevelOfIndex(3)==2 &&
           LevelOfIndex(kTileCount-1)==6 && LevelOfIndex(kTileCount)==-1 && LevelOfIndex(-1)==-1,"an entry's level");
    Expect(TileIndex(6,64,0)==-1 && TileIndex(7,0,0)==-1 && TileIndex(2,0,2)==-1,"outside");
    const Screen s{1920.f,1080.f};
    View v;Clamp(v,s);
    Expect(LevelFor(v,s)==1,"the whole world on 1920 px: level 1 (2700 px wide)");
    v.zoom=MostZoom(s);
    Expect(LevelFor(v,s)==kTileLevels-1,"fully zoomed in: the deepest level");
    v.zoom=1350.f*8.f/1920.f;
    Expect(LevelFor(v,s)==3,"exactly one level's width: that level");
    v.zoom=1350.f*8.f/1920.f+0.01f;
    Expect(LevelFor(v,s)==4,"a little more: the next");
    // Tokyo at the deepest level: one screen spans 1920 / 1350 tiles, rows within the grid.
    v.zoom=MostZoom(s);
    const MapPoint t=Project(35.69f,139.75f);
    v.cu=t.u;v.cv=t.v;Clamp(v,s);
    const TileSpan a=Visible(v,s,kTileLevels-1);
    const int tx=static_cast<int>(t.u*64.f),ty=static_cast<int>(t.v*32.f);
    Expect(a.x0<=tx && tx<=a.x1 && a.y0<=ty && ty<=a.y1,"the centre's tile shown");
    Expect(a.x1-a.x0<=2 && a.y1-a.y0<=1,"one screen: at most 3 x 2 tiles");
    // Across the antimeridian columns run past the grid (the caller wraps them).
    v.zoom=4.f;v.cu=0.999f;v.cv=0.5f;Clamp(v,s);
    const TileSpan b=Visible(v,s,LevelFor(v,s));
    Expect(b.x1>=TileColumns(b.level),"east of 180: past the last column");
    v.cu=0.001f;Clamp(v,s);
    Expect(Visible(v,s,LevelFor(v,s)).x0<0,"west of 180 W: below column 0");
}

void Views() {
    const Screen s{1920.f,1080.f};
    View v;v.zoom=0.1f;
    Clamp(v,s);
    Expect(Near(v.zoom,LeastZoom(s)) && v.zoom>=1.f,"zoom never below filling the height");
    v.zoom=1000.f;Clamp(v,s);
    Expect(Near(v.zoom,MostZoom(s)) && Near(v.zoom,45.f),"zoom capped at the deepest level's pixels");
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
    Expect(inside && f.zoom>1.f,"the framed view shows every place, Prague to Sapporo");
    // Framing only the Japanese places zooms in on Japan.
    int n=0;
    for(int i=0;i<kPlaceCount;++i)if(kPlaces[i].region!=Region::europe && kPlaces[i].region!=Region::africa)pts[n++]=Project(kPlaces[i].lat,kPlaces[i].lon);
    Expect(n>=40 && Frame(pts,n,s).zoom>8.f,"framing Japan: zoomed in");
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

// The select's draw: which texture covers a tile, the edges, the fetch order, the cache's eviction.
void Drawing() {
    const MapRect r=TileRect(2,3,1);
    Expect(Near(r.u0,0.75f) && Near(r.u1,1.f) && Near(r.v0,0.5f) && Near(r.v1,1.f),"a tile's area");
    Expect(Near(TileRect(0,0,0).v1,1.f) && Near(TileRect(1,1,0).u0,0.5f) && Near(TileRect(1,1,0).v1,1.f),
           "level 0 the whole map, level 1 its halves");
    Expect(WrapColumn(3,-1)==7 && WrapColumn(3,8)==0 && WrapColumn(3,5)==5,"columns wrap across the antimeridian");
    // Only level 0 there: a level 3 tile is its part of level 0's one tile.
    auto only0=[](int i) { return i==0; };
    int ax=-1,ay=-1;
    MapRect uv{};
    Expect(Cover(3,5,2,only0,&ax,&ay,&uv)==0 && ax==0 && ay==0,"nothing finer: level 0");
    Expect(Near(uv.u0,5.f/8.f) && Near(uv.u1,6.f/8.f) && Near(uv.v0,0.5f) && Near(uv.v1,0.75f),"...its part of level 0");
    // Its level 2 parent there: the parent's quarter.
    const int parent=TileIndex(2,2,1);
    auto upTo2=[parent](int i) { return i==0 || i==parent; };
    Expect(Cover(3,5,2,upTo2,&ax,&ay,&uv)==2 && ax==2 && ay==1,"the finest ancestor there");
    Expect(Near(uv.u0,0.5f) && Near(uv.u1,1.f) && Near(uv.v0,0.f) && Near(uv.v1,0.5f),"...the right quarter");
    auto all=[](int i) { return i>=0; };
    Expect(Cover(3,5,2,all,&ax,&ay,&uv)==3 && Near(uv.u0,0.f) && Near(uv.v1,1.f),"the tile itself: all of it");
    auto none=[](int) { return false; };
    Expect(Cover(3,5,2,none,&ax,&ay,&uv)==-1,"nothing at all: -1");
    // Neighbours share their edge exactly, also across the antimeridian (unwrapped columns).
    const Screen s{1920.f,1080.f};
    View v;v.zoom=6.f;v.cu=0.999f;v.cv=0.5f;Clamp(v,s);
    const int level=LevelFor(v,s);
    const TileSpan span=Visible(v,s,level);
    bool shared=true;
    for(int x=span.x0;x<span.x1;++x) {
        const MapRect a=TileRect(level,x,span.y0),b=TileRect(level,x+1,span.y0);
        shared=shared && ToScreenUnwrapped(v,s,a.u1,a.v0).x==ToScreenUnwrapped(v,s,b.u0,b.v0).x;
    }
    Expect(shared && span.x1>=TileColumns(level),"edges shared bit for bit, across 180");
    // The covered tiles fill the screen.
    const MapRect first=TileRect(level,span.x0,span.y0),last=TileRect(level,span.x1,span.y1);
    const Pixel tl=ToScreenUnwrapped(v,s,first.u0,first.v0),br=ToScreenUnwrapped(v,s,last.u1,last.v1);
    Expect(tl.x<=0.f && tl.y<=0.f && br.x>=s.w && br.y>=s.h,"the visible span covers the screen");
    // Fetch order: the centre's tile first.
    int xy[64];
    const int n=CentreFirst(v,s,span,xy,32);
    Expect(n==(span.x1-span.x0+1)*(span.y1-span.y0+1),"every visible tile listed");
    const MapRect c=TileRect(level,xy[0],xy[1]);
    const Pixel c0=ToScreenUnwrapped(v,s,c.u0,c.v0),c1=ToScreenUnwrapped(v,s,c.u1,c.v1);
    Expect(c0.x<=s.w*0.5f && c1.x>=s.w*0.5f && c0.y<=s.h*0.5f && c1.y>=s.h*0.5f,"the first holds the centre");
    Expect(CentreFirst(v,s,span,xy,1)==1,"capacity kept");
    // At most a dozen tiles a screen at any zoom (the cache of 48 holds a screen's worth and their ancestors).
    bool few=true;
    for(float z=LeastZoom(s);z<=MostZoom(s);z*=1.07f) {
        View w;w.zoom=z;w.cu=0.3f;w.cv=0.4f;Clamp(w,s);
        const TileSpan t=Visible(w,s,LevelFor(w,s));
        few=few && (t.x1-t.x0+1)*(t.y1-t.y0+1)<=12;
    }
    Expect(few,"at most 12 tiles on a 1920 x 1080 screen");
    // A 2560 px wide viewport: sharpness by real pixels (one level finer sooner, the deepest zoom less far).
    const Screen wide{1920.f,1080.f,2560.f};
    Expect(Near(MostZoom(wide),86400.f/2560.f) && Near(MostZoom(s),45.f),"the most zoom by real pixels");
    View z;z.zoom=1350.f*8.f/2560.f+0.01f;
    Expect(LevelFor(z,wide)==4 && LevelFor(z,s)==3,"the level by real pixels");
    // Eviction: resident levels and this frame's tiles stay; else the least recently drawn, a tie the deeper.
    const int lv[5]={0,1,4,5,3};
    const unsigned used[5]={1,1,7,7,9};
    Expect(Victim(lv,used,5,9)==3,"oldest, then deeper");
    const unsigned now[5]={1,1,9,9,9};
    Expect(Victim(lv,now,5,9)==-1,"all drawn now or resident: none");
    const unsigned old[5]={0,0,5,5,5};
    Expect(Victim(lv,old,2,9)==-1,"levels 0 and 1 never go");
}

void Opening() {
    const Screen s{1920.f,1080.f};
    const MapPoint tokyo=Project(35.69f,139.75f);
    const View to=FramePlace(tokyo,s);
    const Pixel at=ToScreen(to,s,tokyo);
    Expect(Near(at.x,kMapFocus.x,0.05f) && Near(at.y,kMapFocus.y,0.05f) && Near(to.zoom,15.f),
           "a place's region: 24 degrees across, the place at the open area's middle");
    Expect(!OverPanels(kMapFocus),"the focus is not under the kept windows");
    const View pole=FramePlace(Project(89.f,0.f),s);
    Expect(Near(pole.cv,s.h/(s.w*pole.zoom)),"near a pole: clamped, no sky");
    const View from=Whole(tokyo.u,s);
    Expect(Near(from.zoom,LeastZoom(s)) && Near(from.cv,0.5f),"the whole Earth first");
    const View a=Blend(from,to,0.f,s),b=Blend(from,to,1.f,s),m=Blend(from,to,0.5f,s);
    Expect(Near(a.zoom,from.zoom) && Near(b.zoom,to.zoom) && Near(b.cu,to.cu) && Near(b.cv,to.cv),"ends exact");
    Expect(Near(m.zoom,std::sqrt(from.zoom*to.zoom),1e-2f),"zoom half way by its logarithm");
    Expect(Near(Blend(from,to,2.f,s).zoom,to.zoom) && Near(Blend(from,to,-1.f,s).zoom,from.zoom),"time held to 0..1");
    // Across the seam the short way: 170 E to 170 W passes 180.
    const Pixel mid{s.w*0.5f,s.h*0.5f};
    View e=FramePlace(Project(0.f,170.f),s,24.f,mid),w=FramePlace(Project(0.f,-170.f),s,24.f,mid);
    const View h=Blend(e,w,0.5f,s);
    Expect(Near(h.cu,0.f,1e-3f) || Near(h.cu,1.f,1e-3f),"the short way across 180");
    Expect(Near(Ease(0.5f),0.5f) && Ease(0.25f)<0.25f && Ease(0.75f)>0.75f,"eased");
}

void Input() {
    Expect(Stick(0)==0.f && Stick(7000)==0.f && Near(Stick(32767),1.f) && Near(Stick(-32768),-1.f),"stick dead zone, ends");
    Expect(Stick(20000)>0.f && Stick(20000)<1.f && Near(Stick(-20000),-Stick(20000)),"stick between");
    const Pixel p=StickPan(1.f,0.f,0.5f);
    Expect(Near(p.x,-450.f) && Near(p.y,0.f),"stick right: the map moves left (the view right)");
    Expect(StickPan(0.f,1.f,1.f).y>0.f,"stick up: the map moves down (the view north)");
    const Screen s{1920.f,1080.f};
    View v;v.zoom=4.f;v.cu=0.5f;v.cv=0.5f;Clamp(v,s);
    const Pixel d=StickPan(1.f,1.f,0.1f);
    Pan(v,s,d.x,d.y);
    Expect(v.cu>0.5f && v.cv<0.5f,"stick up-right: the view goes north-east");
    Expect(Near(HoldZoom(false,true,1.f),std::exp2(kHoldZoomPerSec)) && Near(HoldZoom(true,false,1.f),std::exp2(-kHoldZoomPerSec)),
           "RB in, LB out");
    Expect(HoldZoom(true,true,1.f)==1.f && HoldZoom(false,false,1.f)==1.f,"both or neither: no zoom");
    Expect(Near(WheelZoom(120),std::exp2(kWheelNotchZoom)) && Near(WheelZoom(-240),std::exp2(-2.f*kWheelNotchZoom)) &&
           WheelZoom(0)==1.f,"the wheel's notches");
    Expect(OverPanels(Pixel{1000.f,300.f}) && OverPanels(Pixel{1000.f,800.f}) && !OverPanels(Pixel{500.f,500.f}) &&
           !OverPanels(Pixel{1000.f,900.f}),"the kept windows' area");
    const float vp[4]={0.f,0.f,2560.f,1440.f};
    const Pixel c=ClientToDesign(1280.f,720.f,2560.f,1440.f,2560.f,1440.f,vp,s);
    Expect(Near(c.x,960.f) && Near(c.y,540.f),"client centre: design centre");
    const Pixel half=ClientToDesign(640.f,360.f,1280.f,720.f,2560.f,1440.f,vp,s);
    Expect(Near(half.x,960.f) && Near(half.y,540.f),"a client smaller than its target");
    const float box[4]={0.f,180.f,2560.f,1080.f};
    Expect(Near(ClientToDesign(0.f,180.f,2560.f,1440.f,2560.f,1440.f,box,s).y,0.f),"a letterboxed viewport");
    const float none[4]={0.f,0.f,0.f,0.f};
    Expect(!OnScreen(ClientToDesign(1.f,1.f,2560.f,1440.f,2560.f,1440.f,none,s),s),"no viewport yet: off screen");
}
}  // namespace

int main() {
    Table();
    Projection();
    Tiles();
    Views();
    Routes();
    Markers();
    Rows();
    Drawing();
    Opening();
    Input();
    std::printf("%d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
