// The Depth Crawler's driving on any surface (src/crawl_logic.h) checked offline: on a level floor it moves and turns
// as the level-only driver did; on a wall a map command's point below or out from the wall gives a stick (down the
// wall first when the point lies straight out from it) and a turn (the user, 2026-10-06: "蜘蛛车爬墙上指挥不动了");
// on a ceiling it moves too; it stops within its stop distance; the turn's angle keeps the atan2(x, z) sign on a floor.
//   cmake --build build --target crawl_check && build\crawl_check.exe      (exit code 1 on a failure)
#include "../src/crawl_logic.h"
#include <cmath>
#include <cstdio>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}
// A frame from right / up / forward rows (a 4x4 row-major matrix as the game keeps it).
crawl::Frame FrameOf(const float* right,const float* up,const float* fwd) {
    float m[12]={right[0],right[1],right[2],0.0f,up[0],up[1],up[2],0.0f,fwd[0],fwd[1],fwd[2],0.0f};
    crawl::Frame f{};
    crawl::MakeFrame(m,&f);
    return f;
}
// Where the stick takes it in the world (x along right, z along forward).
void WorldMove(const crawl::Frame& f,const float* stick,float* w) {
    for(int i=0;i<3;++i)w[i]=f.right[i]*stick[0]+f.fwd[i]*stick[1];
}
}  // namespace

int main() {
    using namespace crawl;
    const float stop=20.0f,hyst=5.0f,ramp=10.0f;
    float stick[2],w[3];
    // --- A level floor: right is -x (the game's level right of +z), up +y, forward +z.
    {
        const float r[3]={-1.0f,0.0f,0.0f},u[3]={0.0f,1.0f,0.0f},fw[3]={0.0f,0.0f,1.0f};
        const Frame f=FrameOf(r,u,fw);
        const float pos[3]={0.0f,0.0f,0.0f},ahead[3]={0.0f,0.0f,100.0f};
        Check(Move(f,pos,ahead,stop,false,hyst,ramp,stick) && stick[1]>0.99f && std::fabs(stick[0])<1e-3f,"floor: full ahead",stick[0],stick[1]);
        const float left[3]={100.0f,0.0f,0.0f};
        Check(Move(f,pos,left,stop,false,hyst,ramp,stick) && stick[0]<-0.99f,"floor: +x is to its left (stick x negative)",stick[0]);
        const float close22[3]={0.0f,0.0f,22.0f};
        Check(!Move(f,pos,close22,stop,false,hyst,ramp,stick),"floor: stopped, not past the stop + hysteresis");
        Check(Move(f,pos,close22,stop,true,hyst,ramp,stick) && stick[1]>0.0f && stick[1]<0.5f,"floor: moving, slows into the stop",stick[1]);
        const float overhead[3]={3.0f,120.0f,3.0f};   // the player in a heli over it
        Check(!Move(f,pos,overhead,stop,false,hyst,ramp,stick),"floor: a goal high overhead is reached (no circling under it)");
        const float east[3]={1.0f,0.0f,0.0f},northEast[3]={1.0f,0.0f,1.0f};
        Check(std::fabs(AngleAbout(fw,east,u)-1.5707963f)<1e-3f,"floor: turning +z to +x is +90 deg (atan2 sign)",AngleAbout(fw,east,u));
        Check(std::fabs(AngleAbout(fw,northEast,u)-std::atan2(1.0f,1.0f))<1e-3f,"floor: the angle is the heading difference");
        const float upHill[3]={1.0f,3.0f,0.0f};
        Check(std::fabs(AngleAbout(fw,upHill,u)-1.5707963f)<1e-3f,"floor: a goal up a hill turns as on the level");
    }
    // --- A wall facing -x (the crawler on the wall at x = 0, its up -x), climbed head up: forward +y, right +z.
    {
        const float r[3]={0.0f,0.0f,1.0f},u[3]={-1.0f,0.0f,0.0f},fw[3]={0.0f,1.0f,0.0f};
        const Frame f=FrameOf(r,u,fw);
        Check(!OnFloor(f),"wall: not a floor");
        const float pos[3]={0.0f,15.0f,0.0f};
        // Straight out from the wall on the floor 15 m below (the old driver's stick was 0 here).
        const float out[3]={-60.0f,0.0f,0.0f};
        Check(Move(f,pos,out,stop,false,hyst,ramp,stick),"wall: a point out from the wall moves it");
        WorldMove(f,stick,w);
        Check(w[1]<-0.9f,"wall: it goes down the wall first",w[1]);
        // Straight out at its own height (across a corridor, a ledge opposite): nothing of it lies along the wall; it
        // still goes, down the wall onto the floor.
        const float across[3]={-60.0f,15.0f,0.0f};
        Check(Move(f,pos,across,stop,false,hyst,ramp,stick),"wall: a point straight across moves it");
        WorldMove(f,stick,w);
        Check(w[1]<-0.9f,"wall: across: down the wall",w[1]);
        // Behind the wall (a point past the building whose wall it is on): up and over, never down (down drives it back
        // into the same wall: it went up and down it for ever).
        const float behind[3]={60.0f,0.0f,0.0f},behindHigh[3]={60.0f,40.0f,0.0f};
        Check(Move(f,pos,behind,stop,false,hyst,ramp,stick),"wall: a point behind it moves it");
        WorldMove(f,stick,w);
        Check(w[1]>0.9f,"wall: behind: up the wall (over the top)",w[1]);
        Check(Move(f,pos,behindHigh,stop,false,hyst,ramp,stick),"wall: a point behind and above moves it");
        WorldMove(f,stick,w);
        Check(w[1]>0.9f,"wall: behind and above: up",w[1]);
        // Along the floor at the wall's foot, 80 m to +z: down and along.
        const float along[3]={-2.0f,0.0f,80.0f};
        Check(Move(f,pos,along,stop,false,hyst,ramp,stick),"wall: a point along the wall's foot moves it");
        WorldMove(f,stick,w);
        Check(w[2]>0.9f && w[1]<0.0f,"wall: along the wall and down",w[2],w[1]);
        // Within the stop in 3D: stays.
        const float close[3]={-5.0f,5.0f,0.0f};
        Check(!Move(f,pos,close,stop,false,hyst,ramp,stick),"wall: a point within the stop leaves it standing");
        // The turn on the wall: towards +z from its forward (+y) is a quarter turn, the sign as on a floor after the
        // same rotation (right is +z: a turn to its right).
        const float toZ[3]={0.0f,0.0f,50.0f};
        const float a=AngleAbout(fw,toZ,u);
        Check(std::fabs(std::fabs(a)-1.5707963f)<1e-3f,"wall: a quarter turn to face along the wall",a);
        Check(a<0.0f,"wall: turning to its right is negative (as -x, its right, on a floor)",a);
        // A floor's right is -x and a turn to it is -90: the same sign on the wall for a turn to its right.
        const float fr[3]={-1.0f,0.0f,0.0f},fu[3]={0.0f,1.0f,0.0f},ff[3]={0.0f,0.0f,1.0f};
        Check(AngleAbout(ff,fr,fu)<0.0f,"floor: turning to its right is negative too");
        Check(AngleAbout(fw,out,u)==0.0f,"wall: a goal straight out from it gives no heading");
    }
    // --- A ceiling (up -y), head along +z: right is +x.
    {
        const float r[3]={1.0f,0.0f,0.0f},u[3]={0.0f,-1.0f,0.0f},fw[3]={0.0f,0.0f,1.0f};
        const Frame f=FrameOf(r,u,fw);
        const float pos[3]={0.0f,12.0f,0.0f},goal[3]={60.0f,0.0f,0.0f};
        Check(Move(f,pos,goal,stop,false,hyst,ramp,stick),"ceiling: a point off to the side moves it");
        WorldMove(f,stick,w);
        Check(w[0]>0.9f,"ceiling: towards the point",w[0]);
        // Right over the point (a tall cave's floor 40 m below, past the stop): it stays, no darting about over it.
        const float high[3]={0.0f,40.0f,0.0f},below[3]={0.4f,0.0f,-0.3f};
        Check(!Move(f,high,below,stop,false,hyst,ramp,stick) && stick[0]==0.0f && stick[1]==0.0f,"ceiling: right over the point it stays");
        Check(!Move(f,high,below,stop,true,hyst,ramp,stick),"ceiling: right over the point, moving, it stops");
    }
    std::printf("crawl_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
