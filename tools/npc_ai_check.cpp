// The NPC AI's decisions (src/npc_logic.h) checked offline: the player's fire lane and the way out of it, the friendly
// fire test of a shot (its line and its blast), the weapon choice by range and target kind (and its hysteresis), the
// engage range from the true reach, the crowd response (back off, side-step, roll), the combat spot beside the player,
// the next leader and the squad join, the dismiss cooldown, the mark's reach, a vehicle's way back to its post.
//   cmake --build build --target npc_ai_check && build\npc_ai_check.exe      (exit code 1 on a failure)
#include "../src/npc_logic.h"
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

// The intent block: the move's local stick turns back into the world way it was made from (0x56D350's transform), the
// look as 0x4E100 makes it (up is a negative pitch), the roll's stick past the stock 0.35, the script classes.
void Intent() {
    using namespace npc;
    for(float yaw=-3.1f;yaw<3.2f;yaw+=0.37f)
        for(float a=0.0f;a<6.28f;a+=0.41f) {
            const float dir[3]={std::sin(a),0.0f,std::cos(a)};
            float x,z,back[3];
            LocalMove(yaw,dir,0.8f,&x,&z);
            WorldMove(yaw,x,z,back);
            Check(Near(back[0],dir[0]*0.8f,1e-4f) && Near(back[2],dir[2]*0.8f,1e-4f),"local move round trip",yaw,a);
            // The stock's own forward (sin yaw, 0, cos yaw) is local +z.
            const float fwd[3]={std::sin(yaw),0.0f,std::cos(yaw)};
            LocalMove(yaw,fwd,1.0f,&x,&z);
            Check(Near(x,0.0f,1e-4f) && Near(z,1.0f,1e-4f),"forward is +z",yaw);
        }
    float d[2];
    const float up[3]={0.0f,10.0f,10.0f};
    Check(AimDelta(0.0f,0.0f,up,1.0f,10.0f,d) && Near(d[0],-0.785398f,1e-4f) && Near(d[1],0.0f,1e-5f),"up 45 deg: pitch -pi/4",d[0],d[1]);
    const float right[3]={10.0f,0.0f,0.0f};
    Check(AimDelta(0.0f,0.0f,right,0.5f,10.0f,d) && Near(d[1],0.785398f,1e-4f),"yaw +pi/2 at gain 0.5",d[1]);
    const float behind[3]={-0.01f,0.0f,-10.0f};
    Check(AimDelta(0.0f,3.1f,behind,1.0f,10.0f,d) && std::fabs(d[1])<0.1f,"the short way round (wrap)",d[1]);
    Check(AimDelta(0.0f,0.0f,right,1.0f,0.2f,d) && Near(d[1],0.2f,1e-6f),"clamped to most a frame",d[1]);
    Check(AimError(0.0f,0.0f,right)>1.5f && AimError(0.0f,1.5707963f,right)<1e-4f,"aim error");
    // Converging: applying the delta every frame brings the look onto the direction.
    float p=0.3f,y=-2.0f;
    const float tgt[3]={3.0f,-2.0f,-7.0f};
    for(int f=0;f<200;++f){AimDelta(p,y,tgt,0.25f,0.3f,d);p+=d[0];y=Wrap(y+d[1]);}
    Check(AimError(p,y,tgt)<0.01f,"the look converges",AimError(p,y,tgt));
    for(float x=-1.0f;x<=1.0f;x+=0.25f)
        for(float z=-1.0f;z<=1.0f;z+=0.5f) {
            float ox,oz;RollStick(x,z,0.6f,&ox,&oz);
            Check(std::fabs(ox)>0.35f && Near(ox*ox+oz*oz,z==0.0f ? ox*ox : 1.0f,1e-3f),"roll stick past 0.35, unit",x,z);
            if(x<-0.01f)Check(ox<0.0f,"roll keeps its side",x);
        }
    ScriptFacts f{};
    Check(Classify(f)==Control::free,"nothing: free");
    f.npcLeader=true;Check(Classify(f)==Control::squad,"an NPC leader: squad");
    f.rootPlayer=true;Check(Classify(f)==Control::recruited,"root the player: recruited");
    f.directionFrames=5;Check(Classify(f)==Control::hold && Scripted(Classify(f)),"a direction order: hold (scripted)");
    f.directionFrames=0;f.rootRouted=true;f.rootPlayer=false;
    Check(Classify(f)==Control::script,"its leader on a route: script");
    f.escort=true;Check(Classify(f)==Control::escort && Scripted(Classify(f)),"a navigation route: escort");
    f=ScriptFacts{};f.route=true;f.rootPlayer=true;
    Check(Classify(f)==Control::script,"a route wins over recruited");
    f=ScriptFacts{};f.fixed=true;Check(Classify(f)==Control::hold,"fixed: hold");
}

void Lanes() {
    using namespace npc;
    const Lane lane{{0.0f,1.5f,0.0f},{0.0f,1.5f,100.0f},2.0f};
    const float on[3]={0.5f,1.0f,40.0f},beside[3]={5.0f,1.0f,40.0f},behind[3]={0.0f,1.0f,-5.0f},past[3]={0.0f,1.0f,120.0f};
    Check(InLane(lane,on),"in the lane");
    Check(!InLane(lane,beside),"beside the lane");
    Check(!InLane(lane,behind),"behind the player is not the lane");
    Check(!InLane(lane,past),"past the lane's end");
    float out[3];
    Check(LaneEscape(lane,on,out) && out[0]>0.99f && Near(out[2],0.0f,1e-4f),"escape to the side it stands on",out[0],out[2]);
    const float left[3]={-0.5f,1.0f,40.0f};
    Check(LaneEscape(lane,left,out) && out[0]<-0.99f,"escape left from the left",out[0]);
    Check(!LaneEscape(lane,beside,out),"no escape outside the lane");
    // Whatever side and depth, a step of the lane's width along the way out leaves the lane.
    for(float z=5.0f;z<100.0f;z+=7.0f)
        for(float x=-1.9f;x<=1.9f;x+=0.3f) {
            const float p[3]={x,1.0f,z};
            if(!LaneEscape(lane,p,out)){Check(false,"inside but no escape",x,z);continue;}
            const float q[3]={p[0]+out[0]*4.0f,p[1],p[2]+out[2]*4.0f};
            Check(!InLane(lane,q),"the escape leaves the lane",x,z);
        }
}

void Shots() {
    using namespace npc;
    const float from[3]={0.0f,1.0f,0.0f},to[3]={0.0f,1.0f,50.0f};
    const Friend inLine[]={{{0.3f,1.0f,20.0f},0.6f}};
    const Friend wide[]={{{3.0f,1.0f,20.0f},0.6f}};
    const Friend nearTarget[]={{{2.0f,1.0f,52.0f},0.6f}};
    const Friend behindShooter[]={{{0.0f,1.0f,-3.0f},0.6f}};
    Check(!ShotClear(from,to,0.5f,0.0f,inLine,1),"a friend on the line blocks");
    Check(ShotClear(from,to,0.5f,0.0f,wide,1),"a friend 3 m off is clear");
    Check(ShotClear(from,to,0.5f,0.0f,nearTarget,1),"no blast: a friend by the target is not hit");
    Check(!ShotClear(from,to,0.5f,6.0f,nearTarget,1),"blast: a friend by the target blocks");
    Check(ShotClear(from,to,0.5f,0.0f,behindShooter,1),"a friend behind the shooter is clear");
    Check(ShotClear(from,to,0.5f,6.0f,nullptr,0),"no friends: clear");
}

void Arms() {
    using namespace npc;
    // 0 an assault rifle (300 m), 1 a rocket launcher (blast 8 m, 400 m), 2 a homing launcher (anti-air), 3 a shotgun (60 m)
    Arm a[4]={{300.0f,0.0f,100.0f,false,true,false},{400.0f,8.0f,150.0f,false,true,false},
              {500.0f,4.0f,80.0f,true,true,true},{60.0f,0.0f,400.0f,false,true,false}};
    Check(PickArm(a,4,-1,30.0f,TargetKind::light,false)==3,"close: the shotgun",PickArm(a,4,-1,30.0f,TargetKind::light,false));
    Check(PickArm(a,4,-1,250.0f,TargetKind::large,false)==1,"far, large: the rocket",PickArm(a,4,-1,250.0f,TargetKind::large,false));
    Check(PickArm(a,4,-1,250.0f,TargetKind::large,true)==0,"far, large, a friend by it: no blast",PickArm(a,4,-1,250.0f,TargetKind::large,true));
    Check(PickArm(a,4,-1,200.0f,TargetKind::air,false)==2,"a flyer: the homing launcher",PickArm(a,4,-1,200.0f,TargetKind::air,false));
    Check(PickArm(a,4,-1,450.0f,TargetKind::light,false)==2,"past the rifle and rocket: only the 500 m one");
    Check(PickArm(a,4,-1,600.0f,TargetKind::light,false)==-1,"out of every reach: none");
    Check(PickArm(a,4,-1,10.0f,TargetKind::large,false)==3,"inside the blast's minimum: never the rocket");
    a[3].ready=false;
    Check(PickArm(a,4,-1,30.0f,TargetKind::light,false)==1,"the shotgun reloading: the rocket (30 m is past its blast)");
    Check(PickArm(a,4,-1,12.0f,TargetKind::light,false)==0,"the shotgun reloading, 12 m: the rifle (inside the blast)");
    a[3].ready=true;
    // Hysteresis: the current arm is kept while it is within a quarter of the best.
    Arm b[2]={{300.0f,0.0f,100.0f,false,true,false},{300.0f,0.0f,110.0f,false,true,false}};
    Check(PickArm(b,2,0,100.0f,TargetKind::light,false)==0,"a 10% better arm does not take over");
    b[1].dps=200.0f;
    Check(PickArm(b,2,0,100.0f,TargetKind::light,false)==1,"a twice better arm takes over");
    // Every pick is usable: in reach, out of its blast, ready.
    for(float d=1.0f;d<700.0f;d+=13.0f)
        for(int k=0;k<3;++k) {
            const int i=PickArm(a,4,-1,d,static_cast<TargetKind>(k),false);
            if(i<0)continue;
            Check(a[i].ready && d<=a[i].reach && d>=MinRange(a[i]),"pick usable",d,i);
        }
    // The engage range comes from the true reach.
    Check(Near(EngageRange(a,4,0.8f),400.0f,0.01f),"engage range 80% of the longest reach",EngageRange(a,4,0.8f));
    const Arm rocketOnly[1]={{30.0f,8.0f,100.0f,false,true,false}};
    Check(EngageRange(rocketOnly,1,0.5f)>=MinRange(rocketOnly[0]),"never inside its own blast",EngageRange(rocketOnly,1,0.5f));
    Check(EngageRange(nullptr,0,0.8f)==0.0f,"no arms: 0");
}

void Crowds() {
    using namespace npc;
    const float me[3]={0.0f,0.0f,0.0f};
    const Threat none[1]={{{100.0f,0.0f,0.0f},1.0f}};
    Check(CrowdResponse(me,none,1,20.0f,1.5f,4.0f,true).move==Move::hold,"far: hold");
    const Threat one[1]={{{0.0f,0.0f,15.0f},1.0f}};
    const Evade s=CrowdResponse(me,one,1,20.0f,1.5f,4.0f,true);
    Check(s.move==Move::sidestep && Near(std::fabs(s.dir[0]),1.0f,1e-3f),"one coming: side-step across",s.dir[0],s.dir[2]);
    const Threat close[1]={{{0.0f,0.0f,3.0f},1.0f}};
    const Evade r=CrowdResponse(me,close,1,20.0f,1.5f,4.0f,true);
    Check(r.move==Move::roll && r.dir[2]<-0.99f,"one at grabbing range, roll ready: roll away",r.dir[2]);
    Check(CrowdResponse(me,close,1,20.0f,1.5f,4.0f,false).move==Move::back,"...roll not ready: back off");
    const Threat pack[3]={{{5.0f,0.0f,5.0f},1.0f},{{-5.0f,0.0f,5.0f},1.0f},{{0.0f,0.0f,7.0f},1.0f}};
    const Evade p=CrowdResponse(me,pack,3,20.0f,1.5f,2.0f,true);
    Check(p.move==Move::back && p.dir[2]<-0.9f,"a pack in front: back off away from it",p.dir[2],p.pressure);
    // Surrounded evenly: still a way out (straight away from the nearest), never NaN.
    const Threat ring[4]={{{6.0f,0.0f,0.0f},1.0f},{{-6.0f,0.0f,0.0f},1.0f},{{0.0f,0.0f,6.0f},1.0f},{{0.0f,0.0f,-6.0f},1.0f}};
    const Evade q=CrowdResponse(me,ring,4,20.0f,1.5f,2.0f,true);
    Check(q.move==Move::back && Near(Len(q.dir),1.0f,1e-3f),"surrounded: back off along a unit way",Len(q.dir));
    // The way out always points away from the weighted centre (or at worst across it).
    for(float a=0.0f;a<6.28f;a+=0.4f) {
        const Threat t[2]={{{std::sin(a)*8.0f,0.0f,std::cos(a)*8.0f},1.0f},{{std::sin(a+0.5f)*9.0f,0.0f,std::cos(a+0.5f)*9.0f},1.0f}};
        const Evade e=CrowdResponse(me,t,2,20.0f,1.0f,2.0f,true);
        const float c[3]={std::sin(a)+std::sin(a+0.5f),0.0f,std::cos(a)+std::cos(a+0.5f)};
        Check(e.move==Move::back && Dot(e.dir,c)<0.0f,"back off away from them",a);
    }
}

void Spots() {
    using namespace npc;
    const float target[3]={0.0f,0.0f,100.0f},player[3]={0.0f,0.0f,0.0f};
    for(float x=-60.0f;x<=60.0f;x+=10.0f) {
        const float me[3]={x,0.0f,20.0f};
        float spot[3];
        CombatSpot(me,target,player,80.0f,0.8f,spot);
        Check(Near(Horiz(spot,target),80.0f,0.01f),"on the ring",x);
        // Never between the player and the target: off their lane by the flank angle.
        const Lane lane{{player[0],1.5f,player[2]},{target[0],1.5f,target[2]},8.0f};
        Check(!InLane(lane,spot),"not in the player's lane",x,spot[0]);
        if(x>5.0f)Check(spot[0]>0.0f,"on my side (right)",x,spot[0]);
        if(x<-5.0f)Check(spot[0]<0.0f,"on my side (left)",x,spot[0]);
    }
    float spot[3];const float me[3]={0.0f,0.0f,40.0f};
    CombatSpot(me,target,nullptr,50.0f,0.8f,spot);
    Check(Near(spot[2],50.0f,0.01f) && Near(spot[0],0.0f,0.01f),"no player: straight back towards me",spot[0],spot[2]);
}

void Squads() {
    using namespace npc;
    Member m[4]={{1,false,false,100.0f,3,{}},{2,true,false,80.0f,1,{}},{3,true,false,90.0f,1,{}},{4,true,true,500.0f,9,{}}};
    Check(PickLeader(m,4)==2,"leader dead: the healthier of equal rank, never the player",PickLeader(m,4));
    m[1].rank=2;
    Check(PickLeader(m,4)==1,"rank first",PickLeader(m,4));
    m[1].alive=m[2].alive=false;
    Check(PickLeader(m,4)==-1,"only the player left: none");
    Member eq[2]={{9,true,false,50.0f,1,{}},{5,true,false,50.0f,1,{}}};
    Check(PickLeader(eq,2)==1,"a tie: the lower id (stable)");
    const float at[3]={0.0f,0.0f,0.0f};
    const float others[3][3]={{300.0f,0.0f,0.0f},{80.0f,0.0f,0.0f},{60.0f,0.0f,0.0f}};
    const int sizes[3]={2,3,8};   // the nearest (60 m) is full
    Check(JoinSquad(1,2,at,others,sizes,3,8,200.0f)==1,"alone: join the nearest with room",JoinSquad(1,2,at,others,sizes,3,8,200.0f));
    Check(JoinSquad(3,2,at,others,sizes,3,8,200.0f)==-1,"enough left: keep going");
    Check(JoinSquad(1,2,at,others,sizes,1,8,200.0f)==-1,"none in range: alone");
    Check(JoinSquad(0,2,at,others,sizes,3,8,200.0f)==-1,"none left: nothing to join");
    Cooldowns<4> c;
    c.Start(7,1000,30000);
    Check(!c.Ready(7,1000) && !c.Ready(7,30999) && c.Ready(7,31000),"cooldown 30 s");
    Check(c.Ready(8,1000),"another squad unaffected");
    Check(c.Left(7,11000)==20000,"time left",static_cast<double>(c.Left(7,11000)));
    for(std::uint32_t s=10;s<20;++s)c.Start(s,2000,1000);   // more than the table holds: the soonest done give way
    Check(!c.Ready(19,2000),"a new cooldown always holds");
}

void Marks() {
    using namespace npc;
    const float me[3]={0.0f,0.0f,0.0f},mark[3]={0.0f,0.0f,250.0f};
    Check(MarkInReach(me,mark,200.0f,60.0f),"reach + move covers it");
    Check(!MarkInReach(me,mark,150.0f,60.0f),"too far: not taken");
}

void Posts() {
    using namespace npc;
    // A level tank: forward f = (sin h, 0, cos h), right r = (cos h, 0, -sin h) (row 0 to the right of row 2 seen from
    // above with y up, as the stock rows make the bearing atan2(d.r, d.f) positive for a post to the right).
    const float post[3]={0.0f,0.0f,0.0f};
    const float north[3]={0.0f,0.0f,1.0f},east[3]={1.0f,0.0f,0.0f};
    const float inside[3]={0.0f,0.0f,-1.0f};
    Check(!ReturnToPost(inside,east,north,post,3.0f,25.0f,2.0f,0.31f).active,"within the hold: nothing");
    const float back10[3]={0.0f,0.0f,-10.0f};
    Steer s=ReturnToPost(back10,east,north,post,3.0f,25.0f,2.0f,0.31f);
    Check(s.active && !s.reverse && s.throttle>0.0f && Near(s.bearing,0.0f,1e-4f) && ThrottleStick(s)<0.0f,
          "post ahead: forward (the stick negative, as a pad's forward)",s.throttle,s.bearing);
    const float fwd10[3]={0.0f,0.0f,10.0f};
    s=ReturnToPost(fwd10,east,north,post,3.0f,25.0f,2.0f,0.31f);
    Check(s.active && s.reverse && s.throttle<0.0f && Near(s.bearing,0.0f,1e-4f) && ThrottleStick(s)>0.0f,"post close behind: reverse",
          s.throttle,s.bearing);
    const float fwd80[3]={0.0f,0.0f,80.0f};
    s=ReturnToPost(fwd80,east,north,post,3.0f,25.0f,2.0f,0.31f);
    Check(s.active && !s.reverse && s.throttle==0.0f && std::fabs(SteerStick(s))>0.99f,"far behind: turn on the spot",s.throttle,s.bearing);
    const float leftOf[3]={10.0f,0.0f,0.0f};   // the post 10 m to its left (-x of the east row)
    s=ReturnToPost(leftOf,east,north,post,3.0f,25.0f,2.0f,0.31f);
    Check(s.bearing<0.0f && SteerStick(s)>0.0f,"post on the left: negative bearing, positive stick (the stock sign)",s.bearing);
    // Steering converges: a tank model turned by the stock's own sign (a negative stick turns it towards a positive
    // bearing, i.e. right) ends at the post from every start.
    for(float a=0.0f;a<6.28f;a+=0.5f)
        for(float d=6.0f;d<=60.0f;d+=18.0f)
            for(float h=0.0f;h<6.28f;h+=1.1f) {
                float p[3]={std::sin(a)*d,0.0f,std::cos(a)*d},heading=h;
                bool done=false;
                for(int f=0;f<60*60 && !done;++f) {
                    const float fw[3]={std::sin(heading),0.0f,std::cos(heading)},rt[3]={std::cos(heading),0.0f,-std::sin(heading)};
                    const Steer t=ReturnToPost(p,rt,fw,post,3.0f,25.0f,2.0f,0.31f);
                    if(!t.active){done=true;break;}
                    // A positive bearing is to the right; turning right grows the heading atan2(f.x, f.z). Tracks turn
                    // the hull the same way whichever way it drives, and the stock steers its reverse by the same formula
                    // (the bearing + pi: the tail's), so the stick's sign holds in reverse too.
                    heading+=-SteerStick(t)*1.2f/60.0f;
                    const float speed=-ThrottleStick(t)*8.0f/60.0f;
                    for(int k=0;k<3;k+=2)p[k]+=fw[k]*speed;
                }
                Check(done,"back at the post within a minute",a,d);
            }
}
}  // namespace

int main() {
    Intent();
    Lanes();
    Shots();
    Arms();
    Crowds();
    Spots();
    Squads();
    Marks();
    Posts();
    std::printf("npc_ai_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
