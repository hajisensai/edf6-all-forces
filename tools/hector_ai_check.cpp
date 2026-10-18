// EDF4.1 Hector's arm gate and back-off cycle (src/hector_ai.h) checked offline: which arms start an attack by range
// (none / one / both, and the 4.1 random choice when the two arms differ), the cycle's counter (counts only inside
// `near`, twice as fast inside the stop radius), the back-off's length and what follows it, how the walk is steered and
// at what speed (4.1's, not the class's), and the arms' aim (angles, limits, easing, and the bones below an arm following it).
//   cmake --build build --target hector_ai_check && build\hector_ai_check.exe      (exit code 1 on a failure)
#include "../src/hector_ai.h"
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
hector::Config Cannon301Launcher() {
    hector::Config c;
    c.range[0]=2000.0f; c.range[1]=150.0f;      // 301 CL: cannon on arms_l, launcher on arms_r
    return c;
}
}  // namespace

int main() {
    using namespace hector;
    Rng rng;
    rng.Seed(12345);
    // --- lifecycle: a respawn at the same object address must not inherit the previous arm gate/cycle.
    {
        int first=0,second=0;
        Check(CacheMatches(&first,&first,true),"same live marked instance retains its cycle");
        Check(!CacheMatches(&first,&second,true),"new Hector control block at reused address resets cycle");
        Check(!CacheMatches(&first,&second,false),"stock Berserker at reused address is not a Hector");
        Check(!CacheMatches(&first,&first,false),"removed marker invalidates even matching identity");
        Check(!CacheMatches(nullptr,nullptr,true),"missing native identity cannot retain state");
    }
    // --- the pick
    {
        Config c=Cannon301Launcher();
        Check(PickArms(c,rng,2500.0f)==0u,"beyond every range: nothing fires");
        Check(PickArms(c,rng,2000.0f)==0u,"range is exclusive (4.1: range > distance)");
        for(int i=0;i<200;++i)Check(PickArms(c,rng,1000.0f)==kArmLeft,"only the cannon reaches 1000");
        int both=0,left=0,right=0;
        for(int i=0;i<4000;++i) {
            const unsigned m=PickArms(c,rng,100.0f);
            both+=m==3u; left+=m==kArmLeft; right+=m==kArmRight;
        }
        Check(both+left+right==4000 && both>1700 && both<2300,"different arms in range: about half both",both);
        Check(left>800 && left<1200 && right>800 && right<1200,"...a quarter each alone",left,right);
        c.sameWeapon=true;
        for(int i=0;i<200;++i)Check(PickArms(c,rng,100.0f)==3u,"the same weapon on both arms: both always");
        Config s;
        s.range[0]=0.0f; s.range[1]=2000.0f;    // a shield (range 0) never fires
        for(int i=0;i<200;++i)Check(PickArms(s,rng,500.0f)==kArmRight,"a shield arm is never picked");
    }
    // --- the cycle
    {
        Config c;
        Cycle y=Begin(c,rng);
        Check(y.counter>=720.0f && y.counter<2160.0f,"first counter 720..2160",y.counter);
        Check(y.stopRadius>=20.0f && y.stopRadius<=80.0f,"stop radius 20..80",y.stopRadius);
        const float before=y.counter;
        Check(Step(y,c,rng,400.0f)==Event::None && y.counter==before,"outside near: the counter stands still");
        const float r=y.stopRadius;
        Step(y,c,rng,r+10.0f > c.closeIn-1.0f ? c.closeIn-1.0f : r+10.0f);
        Check(y.counter==before-1.0f,"inside near, outside the stop radius: 1 a frame",y.counter,before);
        Step(y,c,rng,r-1.0f);
        Check(y.counter==before-3.0f,"inside the stop radius: 2 a frame",y.counter,before);
        // run it down
        y.counter=3.0f;
        Check(Step(y,c,rng,r-1.0f)==Event::None && y.counter==1.0f,"3 -> 1");
        Check(Step(y,c,rng,r-1.0f)==Event::BackoffStart && y.phase==Phase::Retreat,"1 -> back-off");
        Check(y.retreatLeft>=540 && y.retreatLeft<1260,"back-off 540..1260 frames",y.retreatLeft);
        const int len=y.retreatLeft;
        int frames=1;
        Event ev=Event::None;
        while(true) {
            ev=Step(y,c,rng,500.0f);       // distance does not matter while backing off
            if(ev!=Event::None)break;
            ++frames;
        }
        Check(ev==Event::BackoffEnd && frames==len,"the back-off lasts its rolled length",frames,len);
        Check(y.phase==Phase::Approach && y.counter>=720.0f,"then a new cycle",y.counter);
    }
    // --- the walk
    {
        Config c;
        Cycle y;
        y.stopRadius=40.0f;
        Walk w=Steer(y,c,300.0f);
        Check(w.takeover && !w.retreat && w.go,"far: 4.1 heads for the target too (no route), forward along its facing");
        w=Steer(y,c,100.0f);
        Check(w.takeover && !w.retreat && w.go,"inside near, outside the stop radius: straight at the target");
        w=Steer(y,c,39.0f);
        Check(w.takeover && !w.retreat && !w.go,"inside the stop radius: stand");
        y.phase=Phase::Retreat;
        w=Steer(y,c,10.0f);
        Check(w.takeover && w.retreat && w.go,"back-off: away from the target");
        w=Steer(y,c,900.0f);
        Check(w.takeover && w.retreat && w.go,"back-off at any distance");
        // 4.1's speeds (EDF41.exe 0x223832 / 0x2238BB: -0.85 backing off; +0x278 = 1 otherwise), not the class's 0.125
        Check(w.speed==kRetreatSpeed && kRetreatSpeed==0.85f,"back-off at 0.85 of the walk",w.speed);
        y.phase=Phase::Approach;
        w=Steer(y,c,300.0f);
        Check(w.takeover && w.speed==kWalkSpeed && kWalkSpeed==1.0f,"far: at 4.1's full speed",w.speed);
        w=Steer(y,c,100.0f);
        Check(w.speed==1.0f,"closing in at full speed",w.speed);
        w=Steer(y,c,39.0f);
        Check(w.speed==0.0f,"inside the stop radius: speed 0",w.speed);
        w=Steer(y,c,10.0f,false);
        Check(!w.takeover && w.speed==1.0f,"no target: the game's goal at full speed");
    }
    // --- the legs' animation (4.1 0x2220E0 / 0x2224E0)
    {
        const float walkK=10.0f,turnK=112.0f;   // HECTOR.CAS v_walk_animation_speed / v_turn_animation_speed
        Check(PickGait(0.0f,0.0f,0.0f,0.0f)==Gait::Stand,"still: stand");
        Check(GaitRate(Gait::Stand,0.0f,0.0f,0.0f,walkK,turnK)==1.0f,"stand at rate 1");
        // turning on the spot (the case EDF6's stand state plays as `stand`)
        Check(PickGait(0.03f,0.0f,0.0f,0.0f)==Gait::TurnL,"a growing yaw with nothing commanded: turn_l");
        Check(PickGait(-0.03f,0.0f,0.0f,0.0f)==Gait::TurnR,"a shrinking yaw: turn_r");
        Check(std::fabs(GaitRate(Gait::TurnL,0.03f,0.0f,0.0f,walkK,turnK)-3.36f)<1e-4f,"turn rate |yaw step| x 112",
              GaitRate(Gait::TurnL,0.03f,0.0f,0.0f,walkK,turnK));
        Check(std::fabs(GaitRate(Gait::TurnR,-0.003f,0.0f,0.0f,walkK,turnK)-0.336f)<1e-4f,"a slow turn plays slowly");
        Check(PickGait(0.00009f,0.0f,0.0f,0.0f)==Gait::Stand,"a yaw step under 1e-4: still stand");
        Check(PickGait(0.03f,0.0f,0.0f,0.0f)!=Gait::Stand && PickGait(0.0001f,0.0f,0.0f,0.0f)==Gait::WalkB,
              "exactly 1e-4: not stand, not a turn (4.1's comparisons), the walk branch");
        // walking
        Check(PickGait(0.0f,0.0f,0.1f,0.01f)==Gait::WalkF,"forward: walk_f");
        Check(std::fabs(GaitRate(Gait::WalkF,0.0f,0.0f,0.1f,walkK,turnK)-1.0f)<1e-5f,"4.1's full walk (0.1 a frame) at rate 1");
        Check(PickGait(0.0f,0.0f,-0.085f,0.0072f)==Gait::WalkB,"backing off: walk_b");
        Check(std::fabs(GaitRate(Gait::WalkB,0.0f,0.0f,-0.085f,walkK,turnK)-0.85f)<1e-5f,"...at 0.85");
        Check(PickGait(0.01f,0.0f,0.1f,0.01f)==Gait::WalkF,"a turn under 0.4 |v| while walking: still walk_f");
        Check(PickGait(0.03f,0.0f,0.05f,0.01f)==Gait::TurnL,"a turn over 0.4 |v|: turn_l though it walks");
        Check(PickGait(0.0f,0.0866f,0.05f,0.01f)==Gait::WalkL,"60 degrees to +x: walk_l (the larger axis)");
        Check(PickGait(0.0f,-0.0866f,0.05f,0.01f)==Gait::WalkR,"60 degrees to -x: walk_r");
        Check(PickGait(0.0f,0.05f,0.0866f,0.01f)==Gait::WalkF,"30 degrees off: walk_f");
        Check(PickGait(0.0f,0.0f,0.0f,0.01f)==Gait::WalkB,"commanded, not yet moving: the walk branch (4.1: z <= 0 is walk_b)");
        Check(PickGait(0.0f,0.0f,0.02f,0.0f)==Gait::WalkF,"coasting after the command stops: still walking");
        Check(PickGait(0.0f,0.0f,0.009f,0.0f)==Gait::Stand,"slow enough: stand");
    }
    // --- the arms' aim (4.1 Hector_WeaponBase 0x22ADF0)
    {
        const float pi=kPi;
        float p=0.0f,yw=0.0f;
        const float ahead[3]={0.0f,0.0f,50.0f};
        Check(AimAngles(ahead,0,p,yw) && p==0.0f && yw==0.0f,"straight ahead: no turn",p,yw);
        const float up[3]={0.0f,50.0f,50.0f};
        Check(AimAngles(up,0,p,yw) && std::fabs(p+pi/4.0f)<1e-5f,"above: pitch -atan2(y, |xz|)",p);
        const float zenith[3]={0.0f,100.0f,0.001f};
        const bool upOk=AimAngles(zenith,1,p,yw);
        Check(upOk && p>=kPitchMin && std::fabs(p-kPitchMin)<1e-4f,"straight up: pitch -pi/2 at most",p);
        const float below[3]={0.0f,-100.0f,10.0f};
        Check(AimAngles(below,1,p,yw) && std::fabs(p-kPitchMax)<1e-5f,"steep below: pitch held at 0.4 pi",p);
        const float side[3]={50.0f,0.0f,50.0f};
        Check(AimAngles(side,1,p,yw) && std::fabs(yw-pi/4.0f)<1e-5f,"to +x: yaw atan2(x, z)",yw);
        const float backR[3]={std::sin(0.8f*pi),0.0f,std::cos(0.8f*pi)};
        Check(AimAngles(backR,1,p,yw) && std::fabs(yw-kYawStop)<1e-5f,"side 1 stops at 0.7 pi",yw);
        const float backL[3]={-std::sin(0.65f*pi),0.0f,std::cos(0.65f*pi)};
        Check(!AimAngles(backL,1,p,yw) && p==0.0f && yw==0.0f,"side 1 gives up past -0.6 pi");
        Check(AimAngles(backL,0,p,yw) && std::fabs(yw+0.65f*pi)<1e-4f,"side 0: the mirror (-0.65 pi kept)",yw);
        Check(!AimAngles(backR,0,p,yw),"side 0 gives up past +0.6 pi");
        const float none[3]={0.0f,0.0f,0.0f};
        Check(!AimAngles(none,0,p,yw),"no direction: no aim");
        // the rotation points the arm's +Z where AimAngles says
        const float dirs[][3]={{10.0f,5.0f,30.0f},{-20.0f,-8.0f,15.0f},{3.0f,40.0f,2.0f}};
        for(const auto& d:dirs) {
            if(!AimAngles(d,1,p,yw))continue;
            float r[16];
            AimRotation(p,yw,r);
            const float n=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
            const float dot=(r[8]*d[0]+r[9]*d[1]+r[10]*d[2])/n;
            Check(dot>0.9999f || std::fabs(p-kPitchMin)<1e-5f || std::fabs(p-kPitchMax)<1e-5f,"Rx(pitch) Ry(yaw) turns +Z onto the target",dot);
        }
        ArmAim a;
        for(int i=0;i<200;++i)StepAim(a,true,0.5f,-1.0f);
        Check(std::fabs(a.pitch-0.5f)<1e-4f && std::fabs(a.yaw+1.0f)<1e-4f,"attacking: eased onto the target",a.pitch,a.yaw);
        StepAim(a,true,0.0f,0.0f);
        Check(std::fabs(a.pitch-0.45f)<1e-4f,"10 % of the way a frame",a.pitch);
        a.pitch=1.0f; a.yaw=-1.0f;
        StepAim(a,false,0.7f,0.7f);
        Check(std::fabs(a.pitch-0.95f)<1e-6f && std::fabs(a.yaw+0.95f)<1e-6f,"out of an attack: x 0.95 back to 0",a.pitch,a.yaw);
        // the bones below the arm follow it: world' = world x inv(arm as built) x arm as aimed
        float parentW[16],base[16],built[16],childL[16],childW[16],rot[16],local[16],aimed[16],inv[16],move[16],moved[16],want[16];
        const float deg[3]={20.0f,-35.0f,10.0f},pos[3]={5.0f,30.0f,-2.0f};
        const float deg2[3]={90.0f,0.0f,0.0f},pos2[3]={0.0f,-7.0f,0.0f};
        const float deg3[3]={0.0f,0.0f,0.0f},pos3[3]={0.0f,2.7f,5.1f};
        auto euler=[](const float* dg,const float* ps,float* out) {
            const float k=0.017453292f;
            const float cx=std::cos(dg[0]*k),sx=std::sin(dg[0]*k),cy=std::cos(dg[1]*k),sy=std::sin(dg[1]*k),cz=std::cos(dg[2]*k),sz=std::sin(dg[2]*k);
            const float rx[16]={1,0,0,0, 0,cx,sx,0, 0,-sx,cx,0, 0,0,0,1};
            const float ry[16]={cy,0,-sy,0, 0,1,0,0, sy,0,cy,0, 0,0,0,1};
            const float rz[16]={cz,sz,0,0, -sz,cz,0,0, 0,0,1,0, 0,0,0,1};
            float m[16];
            Mul4(rx,ry,m); Mul4(m,rz,out);
            out[12]=ps[0]; out[13]=ps[1]; out[14]=ps[2];
        };
        euler(deg,pos,parentW); euler(deg2,pos2,base); euler(deg3,pos3,childL);
        Mul4(base,parentW,built);
        Mul4(childL,built,childW);
        AimRotation(0.3f,-0.6f,rot);
        Mul4(rot,base,local);
        Mul4(local,parentW,aimed);
        Check(InverseAffine(built,inv),"the arm's world inverts");
        Mul4(inv,aimed,move);
        Mul4(childW,move,moved);
        Mul4(childL,aimed,want);
        float err=0.0f;
        for(int i=0;i<16;++i)err=std::fmax(err,std::fabs(moved[i]-want[i]));
        Check(err<1e-4f,"a bone below the arm ends where the aimed arm puts it",err);
        float l[3];
        const float tgt[3]={aimed[12]+aimed[8]*40.0f,aimed[13]+aimed[9]*40.0f,aimed[14]+aimed[10]*40.0f};
        InFrame(aimed,tgt,l);
        Check(std::fabs(l[0])<1e-3f && std::fabs(l[1])<1e-3f && std::fabs(l[2]-40.0f)<1e-3f,"InFrame: a point on +Z is (0, 0, d)",l[0],l[2]);
    }
    // --- the weapons' own animations and the head
    {
        // a clip of two bones, 3 keys 10 frames apart: bone 0 slides along y, bone 1 turns 180 degrees about z
        const float s=std::sqrt(0.5f);
        const float keys[]={0,0,0, 0,0,0,1,   0,0,0, 0,0,0,1,
                            0,1,0, 0,0,0,1,   0,0,0, 0,0,s,s,
                            0,2,0, 0,0,0,1,   0,0,0, 0,0,1,0};
        Clip c; c.frames=3; c.step=10.0f; c.keys=keys;
        Check(c.Length()==20.0f,"length = (frames - 1) x step",c.Length());
        float p[kKeyFloats];
        SamplePose(c,2,0,5.0f,false,p);
        Check(std::fabs(p[1]-0.5f)<1e-6f,"translation lerped between keys",p[1]);
        SamplePose(c,2,0,99.0f,false,p);
        Check(p[1]==2.0f,"past the end: the last key",p[1]);
        SamplePose(c,2,0,25.0f,true,p);
        Check(std::fabs(p[1]-0.5f)<1e-5f,"looping wraps",p[1]);
        SamplePose(c,2,1,5.0f,false,p);
        const float n=std::sqrt(p[3]*p[3]+p[4]*p[4]+p[5]*p[5]+p[6]*p[6]);
        Check(std::fabs(n-1.0f)<1e-5f && p[5]>0.0f && p[5]<s,"rotation nlerped, unit length",p[5],n);
        // the launcher's gun_bottom_l: the open clip's (0 0 -0.866 0.5) is the model's bind rows
        const float q[kKeyFloats]={1.798f,-1.038f,5.049f, 0.0f,0.0f,-0.8660254f,0.5f};
        float m[16];
        PoseMatrix(q,m);
        const float want[16]={-0.5f,-0.8660254f,0,0, 0.8660254f,-0.5f,0,0, 0,0,1,0, 1.798f,-1.038f,5.049f,1};
        float err=0.0f;
        for(int i=0;i<16;++i)err=std::fmax(err,std::fabs(m[i]-want[i]));
        Check(err<1e-5f,"PoseMatrix: rows are the rotated axes (the model's convention)",err);
        // open / close
        OpenClose oc;
        float t=-1.0f;
        Check(StepOpenClose(oc,false,60.0f,60.0f,t)==1 && t==60.0f,"idle: the close clip's end (closed)",t);
        int clip=StepOpenClose(oc,true,60.0f,60.0f,t);
        Check(clip==0 && t==1.0f && oc.phase==OpenClose::Phase::Opening,"an attack opens from the start",t);
        for(int i=0;i<100;++i)clip=StepOpenClose(oc,true,60.0f,60.0f,t);
        Check(clip==0 && t==60.0f && oc.phase==OpenClose::Phase::Open,"held open while attacking");
        clip=StepOpenClose(oc,false,60.0f,60.0f,t);
        Check(clip==1 && t==1.0f && oc.phase==OpenClose::Phase::Closing,"the attack's end closes");
        for(int i=0;i<19;++i)StepOpenClose(oc,false,60.0f,60.0f,t);
        clip=StepOpenClose(oc,true,60.0f,60.0f,t);
        Check(clip==0 && std::fabs(t-41.0f)<1e-4f,"a close a third done opens again from two thirds",t);
        // the gatling
        Barrels r;
        Check(StepRoll(r,false,60.0f)==kRollIdle,"turning at 1 before any attack");
        StepRoll(r,true,60.0f);
        const float before=r.t;
        Check(StepRoll(r,false,60.0f)==std::fmod(before+kRollFiring,60.0f) && r.fired,"at 5 from the first attack on, for good");
        // the head
        Head h;
        Rng hr; hr.Seed(7);
        int frames=0;
        const wchar_t* st=nullptr;
        while(!(st=StepHead(h,hr,false)) && frames<5000)++frames;
        Check(st && st[0]==L'o' && st[1]==L'p' && frames>=kHeadWaitBase && frames<kHeadWaitBase+kHeadWaitRand,"closed 300..1200 frames, then open",frames);
        Check(!StepHead(h,hr,false) && h.phase==Head::Phase::Opening,"opening until the clip ends");
        Check(!StepHead(h,hr,true) && h.phase==Head::Phase::OpenWait,"then waits open");
        frames=0;
        while(!(st=StepHead(h,hr,false)) && frames<5000)++frames;
        Check(st && st[0]==L'c' && frames>=kHeadWaitBase,"then close",frames);
        StepHead(h,hr,true);
        Check(h.phase==Head::Phase::ClosedWait,"and round again");
    }
    std::printf("hector_ai_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
