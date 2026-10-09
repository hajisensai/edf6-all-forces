// The map's NPC commands (src/mapcmd_logic.h) checked offline: the screen and the ground (the centre's ray meets the
// ground at the focus for every map view; a unit projected onto the screen and the ray under that screen point find it
// again), the floor in a cave (src/map_floor.h: the roof seen from above passed through, a lower level kept), the selection (Ctrl + drag box, Shift adding; a click picking one, Shift toggling, empty ground clearing; the
// Tab / pad X cycle through one unit at a time and ALL; units gone dropped), what each key press comes to (a command to
// the selection, or the refusal: no unit, no point, online), and the guard formation (slots apart, the first on the point).
//   cmake --build build --target map_cmd_check && build\map_cmd_check.exe      (exit code 1 on a failure)
#include "../src/map_cam.h"
#include "../src/mapcmd_logic.h"
#include "../src/map_floor.h"
#include <cstdio>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}
bool Near(float a,float b,float tol) { return std::fabs(a-b)<=tol; }

// A made-up cave for map_floor.h: horizontal faces, each a height and its own normal's y (+1 a floor, -1 a roof seen
// from inside); the ray reports the hit's normal as the triangles' own, or turned to face the ray.
struct Face { float y,ny; };
struct Cave {
    const Face* face; int n; bool facing;
    float operator()(const float* a,const float* b,float* hit,float* normal) const {
        float best=2.0f;int k=-1;
        for(int i=0;i<n;++i) {
            const float dy=b[1]-a[1];
            if(std::fabs(dy)<1e-6f)continue;
            const float t=(face[i].y-a[1])/dy;
            if(t>=0.0f && t<=1.0f && t<best){best=t;k=i;}
        }
        if(k<0)return -1.0f;
        float d[3],len=0.0f;
        for(int i=0;i<3;++i){d[i]=b[i]-a[i];hit[i]=a[i]+d[i]*best;len+=d[i]*d[i];}
        float ny=face[k].ny;
        if(facing && ny*d[1]>0.0f)ny=-ny;
        normal[0]=0.0f;normal[1]=ny;normal[2]=0.0f;
        return best*std::sqrt(len);
    }
};

// The map's camera as the HUD draws with it (tools/hud_view.cpp MapCamera: mapcam::Place, field of view 55 deg, the
// game's level right (-f.z, 0, f.x)): a row-vector view-projection.
void MapViewProj(const mapcam::View& v,float w,float h,float* vp) {
    float eye[3],look[3];
    mapcam::Place(v,eye,look);
    float f[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]};
    float l=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    for(float& c:f)c/=l;
    float r[3]={-f[2],0.0f,f[0]};
    l=std::sqrt(r[0]*r[0]+r[2]*r[2]);r[0]/=l;r[2]/=l;
    const float u[3]={r[1]*f[2]-r[2]*f[1],r[2]*f[0]-r[0]*f[2],r[0]*f[1]-r[1]*f[0]};
    const float fy=1.0f/std::tan(0.5f*55.0f*mapcam::kPi/180.0f),fx=fy/(w/h),n=1.0f,fa=20000.0f,A=fa/(fa-n),B=-n*fa/(fa-n);
    auto dot=[](const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    for(int i=0;i<3;++i){vp[i*4]=r[i]*fx;vp[i*4+1]=u[i]*fy;vp[i*4+2]=f[i]*A;vp[i*4+3]=f[i];}
    vp[12]=-dot(eye,r)*fx;vp[13]=-dot(eye,u)*fy;vp[14]=-dot(eye,f)*A+B;vp[15]=-dot(eye,f);
}
}  // namespace

int main() {
    using namespace mapcmd;
    // --- The screen and the ground: for views over the whole range of the map, the centre's ray meets the level ground at
    // the focus, and ground points on the screen are found again by the ray under them.
    int found=0;
    for(float h=mapcam::kMinHeight;h<=mapcam::kMaxHeight;h*=1.7f)
        for(float pitch=mapcam::kMinPitch;pitch<=mapcam::kMaxPitch;pitch+=0.2f)
            for(float yaw=-3.0f;yaw<=3.0f;yaw+=1.1f) {
                const mapcam::View v{{123.0f,-37.0f,-910.0f},yaw,pitch,h};
                float eye[3],look[3];
                mapcam::Place(v,eye,look);
                float dir[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]};
                const float len=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
                for(float& d:dir)d/=len;
                float hit[3];
                const bool ok=RayLevel(eye,dir,v.focus[1],hit);
                Check(ok && Near(hit[0],v.focus[0],0.05f) && Near(hit[2],v.focus[2],0.05f) && Near(hit[1],v.focus[1],0.01f),
                      "centre ray meets the ground at the focus",h,pitch);
                float vp[16];
                MapViewProj(v,1920.0f,1080.0f,vp);
                for(float ox=-0.4f;ox<=0.41f;ox+=0.2f)
                    for(float oz=-0.4f;oz<=0.41f;oz+=0.2f) {
                        const float p[3]={v.focus[0]+ox*h,v.focus[1],v.focus[2]+oz*h};
                        float x,y;
                        if(!Project(vp,p,1920.0f,1080.0f,&x,&y) || x<0.0f || x>1920.0f || y<0.0f || y>1080.0f)continue;
                        float e[3],d[3],back[3];
                        const bool rayOk=ScreenRay(vp,1920.0f,1080.0f,x,y,e,d) && RayLevel(e,d,v.focus[1],back);
                        const float tol=0.002f*h;
                        Check(rayOk && Near(back[0],p[0],tol) && Near(back[2],p[2],tol),"the ray under a projected ground point finds it",
                              rayOk ? back[0]-p[0] : -1.0,rayOk ? back[2]-p[2] : -1.0);
                        ++found;
                    }
            }
    Check(found>500,"enough ground points on screen to check",found);
    {
        const float eye[3]={0,100,0},up[3]={0,1,0},level[3]={1,0,0},down[3]={0,-1,0};
        float hit[3];
        Check(!RayLevel(eye,up,0.0f,hit),"a ray away from the ground meets nothing");
        Check(!RayLevel(eye,level,0.0f,hit),"a level ray meets nothing");
        Check(RayLevel(eye,down,20.0f,hit) && Near(hit[1],20.0f,1e-4f),"straight down onto a plane at 20 m",hit[1]);
        const float singular[16]{};
        float inv[16];
        Check(!Invert4(singular,inv),"a singular matrix has no inverse");
    }

    // --- The selection: the cycle.
    static const char a=0,b=0,c=0,gone=0;
    const void* ids[]={&a,&b,&c};
    auto one=[](const Selection& s,const void* id){ return s.n==1 && s.id[0]==id; };
    Selection s;
    Cycle(s,ids,3,1);Check(one(s,&a),"none, next: the first");
    Cycle(s,ids,3,1);Check(one(s,&b),"next steps on");
    Cycle(s,ids,3,1);Cycle(s,ids,3,1);Check(IsAll(s,3),"after the last: ALL (every unit)",s.n);
    Cycle(s,ids,3,1);Check(one(s,&a),"after ALL: the first again");
    s.Clear();Cycle(s,ids,3,-1);Check(IsAll(s,3),"none, previous: ALL");
    Cycle(s,ids,3,-1);Check(one(s,&c),"previous from ALL: the last");
    s.Clear();s.Add(&a);Cycle(s,ids,3,-1);Check(IsAll(s,3),"previous from the first: ALL");
    s.Clear();s.Add(&a);s.Add(&c);Cycle(s,ids,3,1);Check(one(s,&a),"a boxed selection, next: the first");
    s.Clear();Cycle(s,ids,1,1);Check(one(s,&a),"one unit: it");
    Cycle(s,ids,1,1);Check(one(s,&a),"one unit: no ALL, it stays");
    s.Clear();Cycle(s,ids,0,1);Check(s.n==0,"no unit: no selection");
    s.Clear();s.Add(&a);s.Add(&gone);s.Add(&c);Keep(s,ids,3);
    Check(s.n==2 && s.Has(&a) && s.Has(&c) && !s.Has(&gone),"a unit no longer listed is dropped",s.n);
    s.Clear();s.Add(&gone);Cycle(s,ids,3,1);Check(one(s,&a),"next from a gone unit: the first");

    // --- The box and the click (screen px).
    const Mark marks[]={{&a,100.0f,100.0f,true},{&b,300.0f,120.0f,true},{&c,310.0f,400.0f,true},{&gone,150.0f,110.0f,false}};
    s.Clear();Box(s,marks,4,50.0f,50.0f,350.0f,200.0f,false);
    Check(s.n==2 && s.Has(&a) && s.Has(&b),"a box takes the units inside it",s.n);
    Check(!s.Has(&gone),"a unit behind the eye is never boxed");
    Box(s,marks,4,350.0f,450.0f,250.0f,350.0f,false);
    Check(one(s,&c),"a new box (corners either way round) replaces the selection",s.n);
    Box(s,marks,4,50.0f,50.0f,150.0f,150.0f,true);
    Check(s.n==2 && s.Has(&a) && s.Has(&c),"Shift + box adds",s.n);
    Box(s,marks,4,500.0f,500.0f,600.0f,600.0f,true);
    Check(s.n==2,"Shift + an empty box changes nothing");
    Box(s,marks,4,500.0f,500.0f,600.0f,600.0f,false);
    Check(s.n==0,"an empty box clears");
    s.Clear();s.Add(&a);s.Add(&b);
    Check(Click(s,marks,4,305.0f,395.0f,22.0f,false)==&c && one(s,&c),"a click on a unit selects it alone");
    Check(Click(s,marks,4,104.0f,96.0f,22.0f,true)==&a && s.n==2 && s.Has(&a) && s.Has(&c),"Shift + click adds");
    Check(Click(s,marks,4,104.0f,96.0f,22.0f,true)==&a && one(s,&c),"Shift + click on a selected one takes it out");
    Check(Click(s,marks,4,700.0f,700.0f,22.0f,true)==nullptr && one(s,&c),"Shift + click on empty ground keeps the selection");
    Check(Click(s,marks,4,700.0f,700.0f,22.0f,false)==nullptr && s.n==0,"a click on empty ground clears");
    Check(Click(s,marks,4,150.0f,110.0f,22.0f,false)==nullptr,"a unit behind the eye is never clicked");
    const Mark close[]={{&a,100.0f,100.0f,true},{&b,112.0f,100.0f,true}};
    Check(Click(s,close,2,109.0f,100.0f,22.0f,false)==&b,"two in reach: the nearer");
    // Shown but not picked (Mark::pick; the user, 2026-10-09: a box of 30 units none of which took the order; a tank's
    // crew squad selected with the tank): the box passes over them, a click on one leaves the selection as it was.
    {
        int tank=0,crew=0,script=0;
        const Mark field[]={{&tank,200.0f,200.0f,true,true},{&crew,201.0f,199.0f,true,false},{&script,260.0f,220.0f,true,false}};
        s.Clear();Box(s,field,3,150.0f,150.0f,300.0f,300.0f,false);
        Check(one(s,&tank),"a box over a tank and its riding crew and a script's squad: the tank alone",s.n);
        Check(Click(s,field,3,260.0f,220.0f,22.0f,false)==&script && one(s,&tank),"a click on a script's squad: named, the selection kept");
        s.Clear();
        Check(Click(s,field,3,203.0f,198.0f,22.0f,false)==&tank && one(s,&tank),"a click on the crew over its tank: the tank");
        const Mark alone[]={{&crew,201.0f,199.0f,true,false}};
        Check(Click(s,alone,1,203.0f,198.0f,22.0f,false)==&crew && one(s,&tank),"a click on a riding crew alone: named, not picked");
    }
    // The enemy under the pointer (mapcmd.cpp Hover): an enemy has a mark at its lock point and one up its pin; either
    // under the pointer finds it, behind the eye never, out of reach none.
    const Mark foes[]={{&a,400.0f,300.0f,true},{&a,400.0f,240.0f,true},{&b,600.0f,300.0f,true},{&b,600.0f,240.0f,false}};
    Check(Nearest(foes,4,402.0f,244.0f,22.0f)==1,"hover: an enemy found up its pin");
    Check(Nearest(foes,4,398.0f,305.0f,22.0f)==0,"hover: an enemy found at its lock point");
    Check(Nearest(foes,4,600.0f,242.0f,22.0f)<0,"hover: a mark behind the eye is never under the pointer");
    Check(Nearest(foes,4,500.0f,300.0f,22.0f)<0,"hover: nothing within reach");
    Check(Nearest(foes,0,400.0f,300.0f,22.0f)<0,"hover: no enemies");
    {   // A box on a real map view: the left half of the screen takes exactly the units drawn there.
        const mapcam::View v{{0.0f,0.0f,0.0f},0.6f,mapcam::kStartPitch,mapcam::kStartHeight};
        float vp[16];
        MapViewProj(v,1920.0f,1080.0f,vp);
        static char unit[25];
        Mark m[25];
        int left=0;
        for(int i=0;i<25;++i) {
            const float p[3]={static_cast<float>(i%5-2)*120.0f,0.0f,static_cast<float>(i/5-2)*120.0f};
            m[i]=Mark{&unit[i],0.0f,0.0f,false};
            m[i].on=Project(vp,p,1920.0f,1080.0f,&m[i].x,&m[i].y);
            left+=m[i].on && m[i].x>=0.0f && m[i].x<960.0f && m[i].y>=0.0f && m[i].y<=1080.0f ? 1 : 0;
        }
        Selection boxed;
        Box(boxed,m,25,0.0f,0.0f,959.99f,1080.0f,false);
        Check(boxed.n==left && left>0 && left<25,"a box over the left half takes the units drawn there",boxed.n,left);
    }

    // --- The presses.
    const float point[3]={10.0f,2.0f,-30.0f};
    const Press none{},guard{false,false,true,false,false},follow{false,false,false,true,false},release{false,false,false,false,true};
    Step st=Decide(0,guard,true,point,true);
    Check(!st.issue && st.why==Refusal::noUnit,"guard with nothing selected: refused (no unit)");
    st=Decide(2,guard,false,point,true);
    Check(!st.issue && st.why==Refusal::online,"online: refused");
    st=Decide(2,guard,true,point,false);
    Check(!st.issue && st.why==Refusal::noPoint,"guard with no ground at the pointer: refused");
    st=Decide(1,follow,true,point,false);
    Check(st.issue && st.cmd.order==Order::follow,"follow needs no point");
    st=Decide(3,guard,true,point,true);
    Check(st.issue && st.cmd.order==Order::guard && st.cmd.at[0]==10.0f && st.cmd.at[1]==2.0f && st.cmd.at[2]==-30.0f,"guard: the pointer's point");
    st=Decide(3,release,true,point,true);
    Check(st.issue && st.cmd.order==Order::none,"release");
    st=Decide(3,none,true,point,true);
    Check(!st.issue && st.why==Refusal::none,"no press: nothing");
    // The squads' orders (docs/npc-ai-design.md §6.2): no point needed; focus needs a mark; vehicles take only three.
    Press engage{};engage.engage=true;
    st=Decide(2,engage,true,point,false);
    Check(st.issue && st.cmd.order==Order::engage,"engage needs no point");
    Press focus{};focus.focus=true;
    st=Decide(2,focus,true,point,true,false);
    Check(!st.issue && st.why==Refusal::noMark,"focus with no mark: refused");
    st=Decide(2,focus,true,point,true,true);
    Check(st.issue && st.cmd.order==Order::focus,"focus on the mark");
    Press dismiss{};dismiss.dismiss=true;dismiss.recruit=true;
    st=Decide(1,dismiss,true,point,true);
    Check(st.issue && st.cmd.order==Order::dismiss,"dismiss before recruit (one order a frame)");
    Press recruit{};recruit.recruit=true;
    st=Decide(1,recruit,false,point,true);
    Check(!st.issue && st.why==Refusal::online,"recruit online: refused");
    Press board{};board.board=true;
    Check(Decide(1,board,true,point,true).cmd.order==Order::board,"board");
    Press off{};off.dismount=true;
    Check(Decide(1,off,true,point,true).cmd.order==Order::dismount,"dismount");
    Check(VehicleOrder(Order::guard) && VehicleOrder(Order::follow) && VehicleOrder(Order::none),"vehicles: guard, follow, release");
    Check(!VehicleOrder(Order::engage) && !VehicleOrder(Order::dismiss) && !VehicleOrder(Order::board),"vehicles: no squad orders");

    // --- The formation.
    float out[3];
    Formation(0,1,point,30.0f,out);
    Check(out[0]==point[0] && out[2]==point[2],"one unit: on the point");
    const int counts[]={2,7,8,19,40};
    for(int k:counts) {
        float slot[40][3];
        for(int i=0;i<k;++i)Formation(i,k,point,30.0f,slot[i]);
        Check(slot[0][0]==point[0] && slot[0][2]==point[2],"the first slot: the point itself",k);
        float least=1e9f,most=0.0f;
        for(int i=0;i<k;++i) {
            most=std::fmax(most,std::sqrt((slot[i][0]-point[0])*(slot[i][0]-point[0])+(slot[i][2]-point[2])*(slot[i][2]-point[2])));
            for(int j=i+1;j<k;++j)
                least=std::fmin(least,std::sqrt((slot[i][0]-slot[j][0])*(slot[i][0]-slot[j][0])+(slot[i][2]-slot[j][2])*(slot[i][2]-slot[j][2])));
        }
        Check(least>=29.0f,"slots at least the spacing apart (no two on one spot)",k,least);
        int need=0;
        while(1+3*need*(need+1)<k)++need;   // rings of 6, 12, 18... round the point hold 1 + 3 r (r + 1)
        const float rings=static_cast<float>(need);
        Check(most<=30.0f*rings+0.01f,"slots within their rings",k,most);
    }

    // --- The floor in a cave (map_floor.h).
    {
        using mapfloor::Normals;
        const Face cave[]={{0.0f,1.0f},{12.0f,-1.0f}};                            // a floor and its roof
        const Face two[]={{0.0f,1.0f},{12.0f,-1.0f},{20.0f,1.0f},{32.0f,-1.0f}};     // two levels
        const Cave own{cave,2,false},facing{cave,2,true},levels{two,4,false};
        // Learn: a floor seen from above and from just under it, as heli.cpp LearnMapNormals casts.
        auto learn=[](const Cave& c){
            float h1[3],n1[3],h2[3],n2[3];
            const float top[3]={0.0f,1.5f,0.0f},bottom[3]={0.0f,-20.0f,0.0f};
            c(top,bottom,h1,n1);
            const float u[3]={0.0f,h1[1]-0.08f,0.0f},o[3]={0.0f,h1[1]+0.08f,0.0f};
            c(u,o,h2,n2);
            return mapfloor::Learn(n1,n2);
        };
        Check(learn(own)==Normals::own,"learn: the triangles' own normals");
        Check(learn(facing)==Normals::facing,"learn: normals facing the ray");
        const float wall[3]={1.0f,0.0f,0.0f},down[3]={0.0f,-1.0f,0.0f};
        Check(mapfloor::Learn(wall,down)==Normals::unknown,"learn: no floor, nothing learned");
        // The pointer's ray from the map's eye over the cave: through the roof's back onto the floor.
        float hit[3];
        const float eye[3]={0.0f,300.0f,-200.0f},end[3]={0.0f,-300.0f,200.0f};
        float m=mapfloor::Floor(eye,end,Normals::own,own,hit);
        Check(m>0.0f && Near(hit[1],0.0f,0.01f) && Near(hit[2],0.0f,0.5f),"pick: the cave's floor, not its roof",hit[1],hit[2]);
        Check(Near(m,std::sqrt(300.0f*300.0f+200.0f*200.0f),0.2f),"pick: metres from the eye to the floor",m);
        m=mapfloor::Floor(eye,end,Normals::own,levels,hit);
        Check(m>0.0f && Near(hit[1],20.0f,0.01f),"pick: the upper level's floor first",hit[1]);
        m=mapfloor::Floor(eye,end,Normals::facing,own,hit);
        Check(m>0.0f && Near(hit[1],12.0f,0.01f),"pick, normals facing the ray: the first hit as before",hit[1]);
        m=mapfloor::Floor(eye,end,Normals::unknown,own,hit);
        Check(m>0.0f && Near(hit[1],12.0f,0.01f),"pick, normals not known: the first hit as before",hit[1]);
        // The ground under a point: of the floors there, the one nearest the height asked about.
        const float top[3]={5.0f,4000.0f,5.0f},bottom[3]={5.0f,-4000.0f,5.0f};
        float h=0.0f;
        Check(mapfloor::Near(top,bottom,1.0f,Normals::own,own,&h) && h==0.0f,"ground: the cave's floor under a unit in it",h);
        Check(mapfloor::Near(top,bottom,21.0f,Normals::own,levels,&h) && h==20.0f,"ground: the upper level for a unit up there",h);
        Check(mapfloor::Near(top,bottom,3.0f,Normals::own,levels,&h) && h==0.0f,"ground: the lower level for a unit down there",h);
        Check(mapfloor::Near(top,bottom,1.0f,Normals::facing,own,&h) && h==0.0f,"ground, normals facing: still the nearest (the floor)",h);
        Check(mapfloor::Near(top,bottom,5000.0f,Normals::own,levels,&h) && h==20.0f,"ground: high over a cave, its top floor",h);
        const Face flat[]={{3.0f,1.0f}};
        const Cave open{flat,1,false};
        Check(mapfloor::Near(top,bottom,50.0f,Normals::own,open,&h) && h==3.0f,"ground: open ground as before",h);
        // A building on open ground: its roof, its underside half a metre up, the ground under it. A slot asked for at
        // ground height but inside its footprint stands on the roof (the ground there has no room over it).
        const Face building[]={{30.0f,1.0f},{0.5f,-1.0f},{0.0f,1.0f}};
        const Cave block{building,3,false};
        auto room=[&](const float* p){
            float hh[3],nn[3];
            const float lo[3]={p[0],p[1]+0.3f,p[2]},hi[3]={p[0],p[1]+2.5f,p[2]};
            return block(lo,hi,hh,nn)<0.0f;
        };
        Check(mapfloor::Near(top,bottom,0.0f,Normals::own,block,room,&h) && h==30.0f,"slot: inside a building's footprint, on its roof",h);
        Check(mapfloor::Near(top,bottom,0.0f,Normals::own,block,&h) && h==0.0f,"ground (any): the ground nearest",h);
        // An obstructed floor just below the requested height must not stop the search: a lower cave floor can be
        // nearer than the already accepted roof. The y=0 floor has only 1 m clearance; y=-5 has 4 m.
        const Face obstructed[]={{30.0f,1.0f},{1.0f,-1.0f},{0.0f,1.0f},{-1.0f,-1.0f},{-5.0f,1.0f}};
        const Cave lower{obstructed,5,false};
        auto lowerRoom=[&](const float* p){
            float hh[3],nn[3];
            const float lo[3]={p[0],p[1]+0.3f,p[2]},hi[3]={p[0],p[1]+2.5f,p[2]};
            return lower(lo,hi,hh,nn)<0.0f;
        };
        const bool lowerFound=mapfloor::Near(top,bottom,0.5f,Normals::own,lower,lowerRoom,&h);
        Check(lowerFound && Near(h,-5.0f,0.01f),
              "slot: skip an obstructed floor below the reference to find a nearer standable lower level",h);
        // Five cave levels over the one asked about (two hits each from the sky): still found.
        const Face deep[]={{0.0f,1.0f},{8.0f,-1.0f},{20.0f,1.0f},{28.0f,-1.0f},{40.0f,1.0f},{48.0f,-1.0f},{60.0f,1.0f},
                           {68.0f,-1.0f},{80.0f,1.0f},{88.0f,-1.0f},{100.0f,1.0f},{108.0f,-1.0f}};
        const Cave stack{deep,12,false};
        Check(mapfloor::Near(top,bottom,1.0f,Normals::own,stack,&h) && Near(h,0.0f,0.01f),"ground: the bottom level under five more",h);
        const Cave empty{flat,0,false};
        Check(!mapfloor::Near(top,bottom,0.0f,Normals::own,empty,&h),"ground: none (off the world)");
    }

    // --- The RTS orders (the user, 2026-10-09): move and attack-move take the pointer's point as guard does; the right
    // button's order; what a move does over the soldiers' own fight (Pursuit) and that it ends as a guard of its point.
    {
        Press move{};move.move=true;
        st=Decide(2,move,true,point,true);
        Check(st.issue && st.cmd.order==Order::move && st.cmd.at[0]==10.0f && st.cmd.at[2]==-30.0f,"move: the pointer's point");
        Check(!Decide(2,move,true,point,false).issue && Decide(2,move,true,point,false).why==Refusal::noPoint,"move with no ground: refused");
        Press am{};am.attackMove=true;am.guard=true;
        Check(Decide(1,am,true,point,true).cmd.order==Order::attackMove,"attack-move before guard (one order a frame)");
        Check(PointOrder(Order::guard) && PointOrder(Order::move) && PointOrder(Order::attackMove) && !PointOrder(Order::focus) &&
              !PointOrder(Order::follow),"the point orders");
        Check(VehicleOrder(Order::move) && VehicleOrder(Order::attackMove) && VehicleCommandOf(Order::move)==Order::guard &&
              VehicleCommandOf(Order::attackMove)==Order::guard && VehicleCommandOf(Order::follow)==Order::follow,
              "a vehicle takes a move / attack-move as its post's guard");
        Check(static_cast<int>(kLastOrder)==static_cast<int>(Order::attackMove) && static_cast<int>(Order::recruit)==8,
              "the wire's order values: recruit stays 8, the new ones after it");
        // The command's priority over the soldier's own fight.
        Check(PursuitOf(Order::move).forced && !PursuitOf(Order::move).fightFirst,"move: forced, before its dodge / combat spot");
        Check(!PursuitOf(Order::attackMove).forced && PursuitOf(Order::attackMove).fightFirst,"attack-move: fights first, then walks on");
        Check(!PursuitOf(Order::guard).forced && !PursuitOf(Order::guard).fightFirst && !PursuitOf(Order::engage).forced,
              "guard and engage: the soldier's own fight as before");
        {   // Pursues: a move always walks; an attack-move walks with no target or one farther than `close`
            const auto mv=PursuitOf(Order::move),atk=PursuitOf(Order::attackMove),gd=PursuitOf(Order::guard);
            Check(Pursues(mv,true,1.0f,15.0f) && Pursues(mv,false,0.0f,15.0f),"move: walks whatever it fights");
            Check(Pursues(atk,false,0.0f,15.0f) && Pursues(atk,true,30.0f,15.0f),"attack-move: walks on firing past a far target");
            Check(!Pursues(atk,true,15.0f,15.0f) && !Pursues(atk,true,4.0f,15.0f),"attack-move: stops for an enemy pressing on it");
            Check(!Pursues(gd,false,0.0f,15.0f) && !Pursues(gd,true,40.0f,15.0f),"guard: never pursues");        }
        Check(Arrive(Order::move,5.0f,6.0f)==Order::guard && Arrive(Order::attackMove,6.0f,6.0f)==Order::guard,"at the point: a guard of it");
        Check(Arrive(Order::move,6.5f,6.0f)==Order::move && Arrive(Order::guard,0.0f,6.0f)==Order::guard &&
              Arrive(Order::follow,0.0f,6.0f)==Order::follow,"not there yet / other orders: unchanged");
        // The right button let go on the map.
        Check(!RightClickOrder(0,true,true).issue && !RightClickOrder(0,false,true).issue,"nothing selected: nothing");
        Check(RightClickOrder(3,false,true).issue && RightClickOrder(3,false,true).order==Order::move,"on the ground: move there");
        Check(RightClickOrder(1,true,false).issue && RightClickOrder(1,true,false).order==Order::focus,"on an enemy: attack it");
        Check(!RightClickOrder(2,false,false).issue,"no ground under it: nothing");
        // Recruitment offered: nobody's, on foot, not cooling down.
        Check(OffersRecruit(false,false,false,false),"a free squad on foot: offered");
        Check(!OffersRecruit(false,false,false,true),"riding a vehicle: not offered");
        Check(!OffersRecruit(true,false,false,false) && !OffersRecruit(false,true,false,false) && !OffersRecruit(false,false,true,false),
              "the player's own, a script's, a dismissed one: not offered");
        // The mark on the body: a ground unit's kBodyLift m up from its feet, a flying one's on it; never the pin's top.
        const float feet[3]={5.0f,20.0f,-7.0f};float mark[3];
        BodyPoint(feet,false,mark);
        Check(mark[0]==5.0f && Near(mark[1],20.0f+kBodyLift,1e-4f) && mark[2]==-7.0f,"a soldier's mark at its middle");
        BodyPoint(feet,true,mark);
        Check(mark[1]==20.0f,"an aircraft's on it");
        const float pinTop=mapcam::PinHeight(800.0f,mapcam::kStartPitch);
        Check(kBodyLift<pinTop*0.1f,"the body point far below where the pin's icon stood",pinTop);
    }

    std::printf("map_cmd_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
