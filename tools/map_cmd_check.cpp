// The map's NPC commands (src/mapcmd_logic.h) checked offline: the screen centre's ground point for every map view
// (src/map_cam.h: the ray eye -> focus meets the level ground at the focus), the selection's cycle through the units
// and ALL, a unit gone from the list dropped, and what each key press comes to (a command to the selection, or the
// refusal: no unit, no point, online).
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
}  // namespace

int main() {
    using namespace mapcmd;
    // --- The point: for views over the whole range of the map, the centre's ray meets the level ground at the focus.
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
            }
    {
        const float eye[3]={0,100,0},up[3]={0,1,0},level[3]={1,0,0},down[3]={0,-1,0};
        float hit[3];
        Check(!RayLevel(eye,up,0.0f,hit),"a ray away from the ground meets nothing");
        Check(!RayLevel(eye,level,0.0f,hit),"a level ray meets nothing");
        Check(RayLevel(eye,down,20.0f,hit) && Near(hit[1],20.0f,1e-4f),"straight down onto a plane at 20 m",hit[1]);
    }

    // --- The selection.
    static const char a=0,b=0,c=0,gone=0;
    const void* ids[]={&a,&b,&c};
    Check(Cycle(ids,3,nullptr,1)==&a,"none, next: the first");
    Check(Cycle(ids,3,&a,1)==&b && Cycle(ids,3,&b,1)==&c,"next steps on");
    Check(Cycle(ids,3,&c,1)==All(),"after the last: ALL");
    Check(Cycle(ids,3,All(),1)==&a,"after ALL: the first again");
    Check(Cycle(ids,3,nullptr,-1)==All(),"none, previous: ALL");
    Check(Cycle(ids,3,All(),-1)==&c && Cycle(ids,3,&a,-1)==All(),"previous steps back through ALL");
    Check(Cycle(ids,0,nullptr,1)==nullptr && Cycle(ids,0,All(),1)==nullptr,"no unit: no selection");
    Check(Keep(ids,3,&gone)==nullptr,"a unit no longer listed is dropped");
    Check(Keep(ids,3,&b)==&b && Keep(ids,3,All())==All(),"a listed unit and ALL stand");
    Check(Keep(ids,0,All())==nullptr,"ALL with no unit left is dropped");
    Check(Cycle(ids,3,&gone,1)==&a,"next from a gone unit: the first");
    Check(Targets(&b,&b) && !Targets(&b,&a) && Targets(All(),&a) && Targets(All(),&c) && !Targets(nullptr,&a),"who a command reaches");

    // --- The presses.
    const float point[3]={10.0f,2.0f,-30.0f};
    const Press none{},next{true,false,false,false,false},guard{false,false,true,false,false},follow{false,false,false,true,false},
                release{false,false,false,false,true},nextGuard{true,false,true,false,false};
    Step s=Decide(ids,3,nullptr,guard,true,point,true);
    Check(!s.issue && s.why==Refusal::noUnit,"guard with nothing selected: refused (no unit)");
    s=Decide(ids,3,&a,guard,false,point,true);
    Check(!s.issue && s.why==Refusal::online,"online: refused");
    s=Decide(ids,3,&a,guard,true,point,false);
    Check(!s.issue && s.why==Refusal::noPoint,"guard with no ground at the crosshair: refused");
    s=Decide(ids,3,&a,follow,true,point,false);
    Check(s.issue && s.cmd.order==Order::follow && s.sel==&a,"follow needs no point");
    s=Decide(ids,3,&a,guard,true,point,true);
    Check(s.issue && s.cmd.order==Order::guard && s.cmd.at[0]==10.0f && s.cmd.at[1]==2.0f && s.cmd.at[2]==-30.0f,"guard: the crosshair's point");
    s=Decide(ids,3,All(),release,true,point,true);
    Check(s.issue && s.cmd.order==Order::none && s.sel==All(),"release to ALL");
    s=Decide(ids,3,nullptr,nextGuard,true,point,true);
    Check(s.issue && s.sel==&a && s.cmd.order==Order::guard,"select and guard in one frame: the new selection gets it");
    s=Decide(ids,3,&gone,guard,true,point,true);
    Check(!s.issue && s.sel==nullptr && s.why==Refusal::noUnit,"a gone unit takes no command");
    s=Decide(ids,3,&b,none,true,point,true);
    Check(!s.issue && s.why==Refusal::none && s.sel==&b,"no press: nothing");
    s=Decide(ids,3,&b,next,true,point,true);
    Check(!s.issue && s.sel==&c,"Tab alone selects");

    std::printf("map_cmd_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
