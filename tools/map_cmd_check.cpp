// The map's NPC commands (src/mapcmd_logic.h) checked offline: the screen and the ground (the centre's ray meets the
// ground at the focus for every map view; a unit projected onto the screen and the ray under that screen point find it
// again), the selection (Ctrl + drag box, Shift adding; a click picking one, Shift toggling, empty ground clearing; the
// Tab / pad X cycle through one unit at a time and ALL; units gone dropped), what each key press comes to (a command to
// the selection, or the refusal: no unit, no point, online), and the guard formation (slots apart, the first on the point).
//   cmake --build build --target map_cmd_check && build\map_cmd_check.exe      (exit code 1 on a failure)
#include "../src/map_cam.h"
#include "../src/mapcmd_logic.h"
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

    std::printf("map_cmd_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
