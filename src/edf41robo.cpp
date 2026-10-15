// EDF4.1's UfoRobo (OBJECT/UFOROBO401.SGO, the story's M007 "巨神船" / M411 "アルゴ", DLC M415 / M416 / M010) as the plugin's own
// class: a flying ship that turns into a walking robot, one object for both (the scripts keep one handle across the change:
// M007 registers its durability events on it after ObjectAction 0). Spec: docs/edf41-uforobo.md (EDF41.exe RE: jobs/14618f05/
// tmp/rerobo/SPEC.md). RVAs: EDF41.exe for 4.1 (the port's specification), EDF.dll TimeDateStamp 0x678CCB46 for EDF6.
//
// The object. Built by EventAnimationObject's constructor (EDF.dll 0x5B5FB0, 0xC90 bytes: a model, the CAS player "main"
// started in "default", a ragdoll, GameObjectBase's durability and damage) under a cloned vtable (src/edf41classes.h; slots 0
// dtor, 5 Update, 9 OnMessage ours). Nothing of EventAnimationObject moves or fights: every behaviour below is 4.1's, frame for
// frame, kept in the plugin's own record of the object (Robo):
//  - body state machine (4.1 +0x15B0, 12 states, EDF41.exe 0x278230 ..): S0 ship idle, S1/S2/S3 the ship's three volleys,
//    S4 transform (no damage taken: 4.1 slot 10), S5 fall, S6 landing, S7 robot idle (step / turn / attack chooser), S8 a step
//    or turn clip, S9 an attack clip, S10 dying, S11 wreck (deleted 10 s later);
//  - AI state machine (4.1 +0x1450): A0 waits, A1 the ship's flight (circle 300-500 m from the target, 250 m above it, facing
//    it; then a straight leg; sideways drifts; the three gun timers), A2/A3 the robot's (approach / hold / walk legs, turns
//    by +-36 / 72 / 126 degrees, the attack timer), A4/A5 the attack choice (robo_attack_table: distance and angle windows);
//  - motion (4.1 slot 1, 0x2766A0): the ship's velocity eased 0.25 % a frame to the command (<= 90 m/s), its yaw rate, the
//    roll; the robot's root motion from the clip (edf41_robo_motion, read off the CAS by tools/make_edf41_objects.py, the
//    bone's own tracks dropped so the model does not move twice), turned by the yaw it had when the step began, kept on the
//    ground by a map ray (from 100 m above to the feet, else falling at 0.008 m/frame^2);
//  - four gun groups (4.1 +0x3F0 / +0x800 / +0xC10 / +0x1020, each G0 idle - G1 charge - G2 fire): knees (BeamBullet01),
//    shoulders (RocketBullet01), hands (RocketBullet01, the buster and the robot's beam attacks), the genocide shot; numbers
//    of 4.1's 0x2744B0 with s = level / 10; muzzles by MAB locator, aimed at the target within the group's limit, a random
//    cone;
//  - the clips' MAB events, raised here as the main CAS player passes their times (edf41_robo_events): se (4.1's preset
//    names, played by cue: edf41_se), 攻撃判定 (a blast of damage x s at the locator: every footstep, the chops and stomps,
//    the landing), 砲撃開始 / 砲撃終了 (the hand guns' beam attacks), ジェノサイド砲発射;
//  - ObjectAction (message 0x1000000D): 0 transform (alive only), 1 robot at once + approach mode, 2 approach mode.
// What EDF6 does for it unchanged: durability and damage (GameObjectBase), the script's durability events (9200 / 9201), the
// kill count, the model's drawing and its animation clock.
// Every game pointer goes through Readable; every EDF.dll function is fingerprinted (edf41classes.cpp); a failure is logged
// once and the object is left as EventAnimationObject made it.
#include "crew.h"
#include "edf41classes.h"
#include "edf41net.h"
#include "memory.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace crew {
namespace {
using namespace edf41c;

constexpr int kSlots=45;
Clone roboClass{"EventAnimationObject",kEaoVtable,kSlots,kEaoSlots,{},{},{}};

constexpr float kPi=3.14159265f;
constexpr int kMostEvents=24;          // per clip (tools/make_edf41_objects.py ROBO_MOST_EVENTS)
constexpr int kMostMotionKeys=512;     // ROBO_MOST_MOTION_KEYS

// ---- once-per-kind log ------------------------------------------------------------------------------------------
void Fail(const char* what) noexcept {
    static std::mutex lock;
    static const char* said[48];
    static int count=0;
    std::lock_guard<std::mutex> hold(lock);
    for(int i=0;i<count;++i)
        if(said[i]==what)return;
    if(count<48)said[count++]=what;
    Log("EDF41ROBO check failed: %s",what);
}

void* Model(void* object) noexcept { return static_cast<char*>(object)+kEaoModel; }

// ---- small vector helpers ---------------------------------------------------------------------------------------
float Len3(const float* v) noexcept { return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
float WrapPi(float a) noexcept {
    while(a>kPi)a-=2.0f*kPi;
    while(a<-kPi)a+=2.0f*kPi;
    return a;
}
// World -> the object's yaw frame (4.1 0x2795C0: rotate by -yaw about +Y; +X is where a positive yaw turns +Z to).
void ToLocal(float yaw,const float* w,float* l) noexcept {
    const float c=std::cos(yaw),s=std::sin(yaw);
    l[0]=w[0]*c-w[2]*s;
    l[1]=w[1];
    l[2]=w[0]*s+w[2]*c;
}
void ToWorld(float yaw,const float* l,float* w) noexcept {
    const float c=std::cos(yaw),s=std::sin(yaw);
    w[0]=l[0]*c+l[2]*s;
    w[1]=l[1];
    w[2]=-l[0]*s+l[2]*c;
}

// ---- the object's data (its SGO, read once) ---------------------------------------------------------------------
enum EventKind : int { kEvSe=0, kEvSeStop=1, kEvHit=2, kEvBeamStart=3, kEvBeamEnd=4, kEvGenocide=5 };
struct Event { float t; int kind; std::wstring text; float a,b,c; };
struct ClipEvents { std::wstring state; int stateIndex=-1; int clip=0; std::vector<Event> events; };
struct Motion { std::wstring state; int stateIndex=-1; float length=0.0f; std::vector<float> keys; };   // x z yaw per key
struct AttackEntry { std::wstring state; int stateIndex=-1; float dmin,dmax,amin,amax; };

// ---- the four gun groups (4.1 0x27AE00 / 0x27B0A0) --------------------------------------------------------------
enum GunState : int { kG0=0, kG1=1, kG2=2 };
enum GunGroup : int { kSmall=0, kMiddle=1, kLarge=2, kGenocide=3 };
const char* const kGunNames[4]={"small","middle","large","genocide"};
struct Muzzle { std::wstring locator,aimBone; LocatorHandle loc{}; bool found=false; };
struct Gun {
    Muzzle muzzle[2];
    int muzzles=0;
    int state=kG0;
    int frames=0;                // frames in the state
    bool autoFire=false;         // +0xB4
    int autoMin=0,autoMax=0;     // +0xB8 / +0xBC
    int autoCount=0;             // +0xC0
    int charge=0;                // +0x9C, frames
    int interval=0;              // +0xA0: a round every interval + 1 frames
    int counter=0;               // +0xA4
    std::int64_t duration=0;     // +0xA8, frames; -1 never ends
    float aimLimit=0.0f;         // +0x98, rad
    float spread=0.0f;           // +0xB0, rad
    Bullet bullet;
    wchar_t factory[32]={};
    const wchar_t* chargeSe=nullptr;
    const wchar_t* shotSe=nullptr;
    std::uint32_t shots=0;
};

// ---- the body / AI states -----------------------------------------------------------------------------------------
enum Body : int { kS0=0, kS1, kS2, kS3, kS4, kS5, kS6, kS7, kS8, kS9, kS10, kS11 };
const char* const kBodyNames[12]={"S0 ship","S1 small volley","S2 middle volley","S3 buster","S4 transform","S5 fall",
                                  "S6 landing","S7 robot","S8 step","S9 attack","S10 dying","S11 wreck"};
enum Ai : int { kA0=0, kA1, kA2, kA3, kA4, kA5 };
const char* const kAiNames[6]={"A0 wait","A1 ship","A2 robot reroll","A3 robot","A4 wait idle","A5 attack choice"};

struct Timer { int count=0,min=0,max=0; float dmin=0.0f,dmax=0.0f; };   // 4.1 0x271AB0

struct Robo {
    void* obj=nullptr;
    bool ready=false,broken=false;
    std::uint32_t ticks=0;
    std::uint64_t rng=0;
    float level=10.0f,s=1.0f;
    float scale=1.0f;
    // the data
    std::vector<ClipEvents> clipEvents;
    std::vector<Motion> motions;
    std::vector<AttackEntry> attacks;
    int stShip=-1,stTransform=-1,stStand=-1,stFall=-1,stFallLand=-1,stDead=-1;
    int stWalkF[5]={-1,-1,-1,-1,-1},stWalkB[5]={-1,-1,-1,-1,-1};   // nl lr rl ln rn
    int stSideL=-1,stSideR=-1,stTurnL[3]={-1,-1,-1},stTurnR[3]={-1,-1,-1};
    LocatorHandle footL{},footR{},lockOn{};
    void* lockPoint=nullptr;     // the lock point the plugin keeps on ロックオン (EventAnimationObject has none)
    SoundHandle drive;           // se_table[0], the drive hum, from birth (4.1 +0x1560)
    bool driveOn=false;
    Gun gun[4];
    // state machines
    int body=kS0,bodyFrames=0;
    int ai=kA0,aiFrames=0;
    int form=0;                  // +0x15F0: 0 ship, 1 robot
    unsigned flags=0;            // +0x15F4: bit0 no commands, guns hold fire; bit3 small / middle guns do not aim
    bool approach=false;         // +0x1538
    // the frame's command (+0x1600..+0x161C)
    float cmdX=0.0f,cmdZ=0.0f,cmdY=0.0f,yawRate=0.0f;
    int turn=0;
    bool cmdTransform=false,cmdSmall=false,cmdMiddle=false,cmdBuster=false;
    int cmdAttack=-1;
    // AI parameters (+0x1478..)
    float aiSpeed=90.0f;
    int phase1=0;                // +0x147C
    float radius=0.0f;           // +0x1480
    int sideFrames=0,sideInterval=0;   // +0x1484 / +0x1488
    float sideAmount=0.0f;       // +0x148C
    int phase2=0;                // +0x1490
    float cruise[3]={0.0f,0.0f,90.0f};   // +0x14A0
    int cruiseTurnFrames=0;      // +0x14B0
    float cruiseTurnRate=0.0f;   // +0x14B4
    Timer tSmall,tMiddle,tBuster,tAttack;
    int attackPick=-1;
    // pose
    float pos[3]={},yaw=0.0f,roll=0.0f,vel[3]={},fall=0.0f;
    bool landed=false;
    // a step (S8)
    int stepState=-1;
    const Motion* stepMotion=nullptr;
    float stepYaw=0.0f,stepPos[3]={};
    int chain=0;                 // +0x328: 1 after a backward step
    int pendingState=-1;         // the clip S8 / S9 is to play
    // the clip's events
    int evState=-2,evClip=0;
    float evTime=-1.0f;
    // dying
    int dieFrames=0;
    bool dead=false,deleted=false;
    // target
    bool haveTarget=false;
    float target[3]={};
    // dev
    bool placed=false,forcedTransform=false;
    int logged=0;
    std::uint32_t events=0,hits=0,blasts=0;   // MAB events raised, 攻撃判定 among them, blasts let off
};

std::mutex lock;
std::unordered_map<const void*,Robo> robos;
std::atomic<int> created{0};

// ---- online (edf41net): declared here, defined in the block "online" at the end of this namespace ----
bool NetFollows(const Robo& r) noexcept;     // a guest copy (online: the host's object; or the development mirror): it shows, it does not decide
void NetBody(Robo& r) noexcept;              // the deciding copy: a body state change, to the guests
void NetGun(Robo& r,int group) noexcept;     // the deciding copy: a robot-form gun group started by its own timer
void NetIn(Robo& r) noexcept;                // every copy, before its frame: the role, the events that came (guests)
void NetPose(Robo& r) noexcept;              // a guest, after Move: its pose pulled to the host's
void NetOut(Robo& r) noexcept;               // the deciding copy, after its frame: the periodic state
void NetMirrorMake(void* self) noexcept;     // development (ini Edf41NetMirror): the mirror of `self`, made outside the lock
void NetForget(const void* self) noexcept;   // its destructor: the record goes

int RandInt(Robo& r,int n) noexcept { return n<=0 ? 0 : static_cast<int>(Random01(r.rng)*static_cast<float>(n))%n; }
float Rand(Robo& r) noexcept { return Random01(r.rng); }

// ---- sounds ---------------------------------------------------------------------------------------------------------
// 4.1's sound by its SEPRESET name, from the object's edf41_se (edf41classes.h PlaySe).
void PlaySe(Robo& r,const wchar_t* name,const float* at) noexcept {
    if(name && *name)edf41c::PlaySe(r.obj,name,at,nullptr);
}

// ---- reading the SGO ------------------------------------------------------------------------------------------------
std::wstring TextOf(const SgoNode* n) noexcept {
    wchar_t buf[128];
    return Text(n,buf,128) ? std::wstring(buf) : std::wstring();
}

bool ReadData(Robo& r) noexcept {
    void* o=r.obj;
    // edf41_robo_events: [[state, clip, [[t, kind, text, a, b, c], ...]], ...]
    const SgoNode* ev=Key(o,L"edf41_robo_events");
    for(int i=0;i<Count(ev);++i) {
        const SgoNode* e=Child(ev,i);
        ClipEvents ce;
        ce.state=TextOf(Child(e,0));
        std::int32_t clip=0;
        if(ce.state.empty() || !Int(Child(e,1),&clip))return false;
        ce.clip=clip;
        ce.stateIndex=StateIndex(o,ce.state.c_str());
        const SgoNode* keys=Child(e,2);
        if(Count(keys)<0 || Count(keys)>kMostEvents)return false;
        for(int k=0;k<Count(keys);++k) {
            const SgoNode* key=Child(keys,k);
            Event x{};
            std::int32_t kind=0;
            if(!Float(Child(key,0),&x.t) || !Int(Child(key,1),&kind) || !Float(Child(key,3),&x.a) || !Float(Child(key,4),&x.b)
               || !Float(Child(key,5),&x.c))return false;
            x.kind=kind;
            x.text=TextOf(Child(key,2));
            ce.events.push_back(std::move(x));
        }
        r.clipEvents.push_back(std::move(ce));
    }
    // edf41_robo_motion: [[state, length, [x, z, yaw, ...]], ...]
    const SgoNode* mo=Key(o,L"edf41_robo_motion");
    for(int i=0;i<Count(mo);++i) {
        const SgoNode* e=Child(mo,i);
        Motion m;
        m.state=TextOf(Child(e,0));
        const SgoNode* keys=Child(e,2);
        const int n=Count(keys);
        if(m.state.empty() || !Float(Child(e,1),&m.length) || n<3 || n%3 || n>kMostMotionKeys*3)return false;
        m.keys.resize(static_cast<std::size_t>(n));
        for(int k=0;k<n;++k)
            if(!Float(Child(keys,k),&m.keys[static_cast<std::size_t>(k)]))return false;
        m.stateIndex=StateIndex(o,m.state.c_str());
        r.motions.push_back(std::move(m));
    }
    // robo_attack_table: [[[state], [dmin, dmax], [amin, amax]], ...]
    const SgoNode* at=Key(o,L"robo_attack_table");
    for(int i=0;i<Count(at);++i) {
        const SgoNode* e=Child(at,i);
        AttackEntry a{};
        a.state=TextOf(Child(Child(e,0),0));
        float d[2],an[2];
        if(a.state.empty() || !Vec(Child(e,1),d,2) || !Vec(Child(e,2),an,2))return false;
        a.dmin=d[0]; a.dmax=d[1]; a.amin=an[0]; a.amax=an[1];
        a.stateIndex=StateIndex(o,a.state.c_str());
        r.attacks.push_back(a);
    }
    if(r.clipEvents.empty() || r.motions.empty() || r.attacks.empty() || !Key(o,L"edf41_se"))return false;
    // CAS states (4.1 0x2732B0)
    r.stShip=StateIndex(o,L"ship"); r.stTransform=StateIndex(o,L"transform"); r.stStand=StateIndex(o,L"stand");
    r.stFall=StateIndex(o,L"fall"); r.stFallLand=StateIndex(o,L"fall_land"); r.stDead=StateIndex(o,L"dead");
    const wchar_t* const f[5]={L"walk_f_nl",L"walk_f_lr",L"walk_f_rl",L"walk_f_ln",L"walk_f_rn"};
    const wchar_t* const b[5]={L"walk_b_nl",L"walk_b_lr",L"walk_b_rl",L"walk_b_ln",L"walk_b_rn"};
    for(int i=0;i<5;++i) { r.stWalkF[i]=StateIndex(o,f[i]); r.stWalkB[i]=StateIndex(o,b[i]); }
    r.stSideL=StateIndex(o,L"side_step_l"); r.stSideR=StateIndex(o,L"side_step_r");
    const wchar_t* const tl[3]={L"turn_l45",L"turn_l90",L"turn_l180"};
    const wchar_t* const tr[3]={L"turn_r45",L"turn_r90",L"turn_r180"};
    for(int i=0;i<3;++i) { r.stTurnL[i]=StateIndex(o,tl[i]); r.stTurnR[i]=StateIndex(o,tr[i]); }
    return r.stShip>=0 && r.stTransform>=0 && r.stStand>=0 && r.stFall>=0 && r.stFallLand>=0;
}

// A gun group's muzzles: [[locator, aim bone or 0], ...] (4.1 0x27B0A0; the aim bone binds only when it is a string).
bool ReadMuzzles(Robo& r,Gun& g,const wchar_t* key) noexcept {
    const SgoNode* m=Key(r.obj,key);
    g.muzzles=0;
    for(int i=0;i<Count(m) && i<2;++i) {
        Muzzle& z=g.muzzle[g.muzzles];
        z.locator=TextOf(Child(Child(m,i),0));
        z.aimBone=TextOf(Child(Child(m,i),1));
        if(z.locator.empty())return false;
        z.found=FindLocator(Model(r.obj),z.locator.c_str(),&z.loc);
        if(!z.found)Log("EDF41ROBO %p muzzle %ls of %ls: no such locator",r.obj,z.locator.c_str(),key);
        ++g.muzzles;
    }
    return g.muzzles>0;
}

// The bullet class and custom parameter of a group (keys <prefix>_bullet_class / _custom_param).
bool ReadBullet(Robo& r,Gun& g,const wchar_t* cls,const wchar_t* custom) noexcept {
    if(!Text(Key(r.obj,cls),g.factory,32))return false;
    g.bullet.factory=g.factory;
    g.bullet.custom=Persist(Key(r.obj,custom));
    return g.bullet.custom!=nullptr;
}

// ---- level and the bullets' numbers (4.1 0x2744B0 / 0x2790F0) ------------------------------------------------------
void SetLevel(Robo& r) noexcept {
    r.level=LevelS(r.obj)*10.0f;   // GameObjectBase +0x49C (SetLevel's difficulty-scaled level; 10 = level 1)
    if(Cfg().edf41RoboLevel>0.0f)r.level=Cfg().edf41RoboLevel;
    r.s=r.level/10.0f;
    const float s=r.s;
    // T_atk (4.1's difficulty table x players) is EDF6's own level here, as for the dragon and Hector: 1.
    Gun& sm=r.gun[kSmall];
    sm.bullet.speed=2.0f; sm.bullet.life=180; sm.bullet.damage=(std::max)(1.0f,0.5f*s); sm.bullet.size=3.2f;
    const float k=0.015f*r.level;
    const float smc[4]={1.0f*k,0.25f*k,0.5f*k,1.0f*k};
    std::memcpy(sm.bullet.color,smc,sizeof(smc));
    sm.aimLimit=0.5f; sm.spread=(std::max)(0.05f,0.25f/s); sm.interval=5; sm.duration=300; sm.charge=180;
    sm.chargeSe=L"ロボ小ビームチャージ"; sm.shotSe=L"ロボ小ビーム発射";
    Gun& md=r.gun[kMiddle];
    md.bullet.speed=2.0f; md.bullet.life=600; md.bullet.damage=(std::max)(1.0f,30.0f*s); md.bullet.explosion=9.0f+s; md.bullet.size=40.0f;
    const float mdc[4]={0.25f,0.5f,1.5f,1.0f};
    std::memcpy(md.bullet.color,mdc,sizeof(mdc));
    md.aimLimit=0.2f; md.spread=0.1f; md.interval=2; md.duration=1;
    md.charge=(std::max)(20,static_cast<int>(180.0f*(1.0f+0.1f*(s-1.0f))));
    md.chargeSe=L"ロボ飛行形態中型キャノンチャージ"; md.shotSe=L"ロボ肩ビーム発射";
    Gun& lg=r.gun[kLarge];
    lg.bullet.speed=10.0f; lg.bullet.life=600; lg.bullet.damage=(std::max)(1.0f,40.0f*s); lg.bullet.explosion=(std::min)(40.0f,36.0f+4.0f*s);
    lg.bullet.size=40.0f;
    const float lgc[4]={0.5f,0.5f,2.0f,1.0f};
    std::memcpy(lg.bullet.color,lgc,sizeof(lgc));
    lg.aimLimit=0.0f; lg.spread=0.1f; lg.interval=31; lg.duration=600; lg.charge=300;
    lg.chargeSe=nullptr; lg.shotSe=L"ロボバスター砲発射";
    Gun& gn=r.gun[kGenocide];
    gn.bullet.speed=0.1f; gn.bullet.life=6000; gn.bullet.damage=(std::max)(1.0f,80.0f*s); gn.bullet.explosion=100.0f; gn.bullet.size=120.0f;
    const float gnc[4]={5.0f,50.0f,20.0f,1.0f};
    std::memcpy(gn.bullet.color,gnc,sizeof(gnc));
    gn.aimLimit=0.0f; gn.spread=0.0f; gn.interval=30; gn.duration=1; gn.charge=346;
    gn.chargeSe=nullptr; gn.shotSe=L"ロボバスター砲発射";
    // the timers (4.1 0x2790F0: x / s truncated)
    auto per=[s](float x) { return static_cast<int>(x/s); };
    r.tSmall={RandInt(r,720),180,720,0.0f,500.0f};
    r.tMiddle={RandInt(r,per(480.0f)),per(300.0f),per(480.0f),150.0f,800.0f};
    r.tBuster={RandInt(r,per(3600.0f)),per(1800.0f),per(3600.0f),0.0f,1000.0f};
    r.tAttack={RandInt(r,per(1800.0f)),per(600.0f),per(1800.0f),-1.0f,-1.0f};
}

// 4.1 0x271AB0: counts down only while the distance is inside [dmin, dmax] (dmin < 0: always); true when it runs out (and
// starts again at min + rand(max - min)).
bool TickTimer(Robo& r,Timer& t,float distance) noexcept {
    if(t.dmin>=0.0f && (distance<t.dmin || distance>t.dmax))return false;
    if(--t.count>0)return false;
    t.count=t.min+RandInt(r,t.max-t.min);
    return true;
}

// ---- AI parameters (4.1 0x2797F0 / 0x279AC0) ---------------------------------------------------------------------
void RerollSide(Robo& r) noexcept {
    r.sideFrames=Rand(r)<0.5f ? 600+RandInt(r,600) : 0;
    r.sideAmount=(Rand(r)<0.5f ? 0.25f : -0.25f)*r.aiSpeed;
    r.sideInterval=r.form==1 && r.approach ? 0 : RandInt(r,600);
}
void Reroll(Robo& r) noexcept {
    r.phase1=1200+RandInt(r,600);
    r.radius=r.form==1 && r.approach ? 50.0f+150.0f*Rand(r) : 300.0f+200.0f*Rand(r);
    r.phase2=600+RandInt(r,300);
    r.cruise[0]=0.0f; r.cruise[1]=0.0f; r.cruise[2]=r.aiSpeed;
    if(r.form==1 && Rand(r)<(r.approach ? 0.5f : 1.0f/3.0f))r.cruise[2]=-r.aiSpeed;
    r.cruiseTurnFrames=RandInt(r,300);
    r.cruiseTurnRate=Rand(r)*0.015f-0.0075f;
}

// ---- guns (4.1 0x27BD70 / 0x27BE60 / 0x27C0F0 / 0x27B610) ------------------------------------------------------------
void GunEnter(Robo& r,Gun& g,int state) noexcept {
    g.state=state;
    g.frames=0;
    if(state==kG1 && g.chargeSe) {   // 4.1: the charge sound at the muzzles' centre
        float c[3]={r.pos[0],r.pos[1],r.pos[2]},m[16];
        int n=0;
        float sum[3]={};
        for(int i=0;i<g.muzzles;++i)
            if(g.muzzle[i].found && LocatorMatrix(g.muzzle[i].loc,m)) { sum[0]+=m[12]; sum[1]+=m[13]; sum[2]+=m[14]; ++n; }
        if(n) { c[0]=sum[0]/n; c[1]=sum[1]/n; c[2]=sum[2]/n; }
        PlaySe(r,g.chargeSe,c);
    }
    if(state==kG2)g.counter=0;
}

// A unit vector within `cone` rad of `axis` (4.1 0x51B7C0: angle uniform in [0, cone], azimuth uniform).
void Scatter(Robo& r,const float* axis,float cone,float* out) noexcept {
    float a[3]={axis[0],axis[1],axis[2]};
    const float l=Len3(a);
    if(l<1e-6f) { out[0]=0.0f; out[1]=0.0f; out[2]=1.0f; return; }
    a[0]/=l; a[1]/=l; a[2]/=l;
    if(cone<=0.0f) { std::memcpy(out,a,12); return; }
    float u[3]={0.0f,1.0f,0.0f};
    if(std::fabs(a[1])>0.9f) { u[0]=1.0f; u[1]=0.0f; }
    float x[3]={u[1]*a[2]-u[2]*a[1],u[2]*a[0]-u[0]*a[2],u[0]*a[1]-u[1]*a[0]};
    const float xl=Len3(x);
    x[0]/=xl; x[1]/=xl; x[2]/=xl;
    const float y[3]={a[1]*x[2]-a[2]*x[1],a[2]*x[0]-a[0]*x[2],a[0]*x[1]-a[1]*x[0]};
    const float t=cone*Rand(r),p=2.0f*kPi*Rand(r);
    const float st=std::sin(t),ct=std::cos(t),sp=std::sin(p),cp=std::cos(p);
    for(int k=0;k<3;++k)out[k]=a[k]*ct+(x[k]*cp+y[k]*sp)*st;
}

// One round from muzzle i: towards the target (no target: 1000 m ahead of the muzzle), turned from the muzzle's axis by at
// most aimLimit, then scattered within the group's cone.
void FireOne(Robo& r,Gun& g,int i,int group) noexcept {
    Muzzle& z=g.muzzle[i];
    float m[16];
    if(!z.found || !LocatorMatrix(z.loc,m)) { Fail("a muzzle locator gives no matrix"); return; }
    const float* p=m+12;
    float axis[3]={m[8],m[9],m[10]};
    const float al=Len3(axis);
    if(al<1e-6f)return;
    axis[0]/=al; axis[1]/=al; axis[2]/=al;
    float to[3]={axis[0],axis[1],axis[2]};
    if(r.haveTarget) {
        to[0]=r.target[0]-p[0]; to[1]=r.target[1]-p[1]; to[2]=r.target[2]-p[2];
        const float tl=Len3(to);
        if(tl>1e-3f) { to[0]/=tl; to[1]/=tl; to[2]/=tl; }
    }
    // turn the axis towards `to` by at most aimLimit
    float dir[3];
    const float cosA=(std::max)(-1.0f,(std::min)(1.0f,axis[0]*to[0]+axis[1]*to[1]+axis[2]*to[2]));
    const float angle=std::acos(cosA);
    if(angle<=g.aimLimit || angle<1e-5f)std::memcpy(dir,to,12);
    else {
        const float t=g.aimLimit/angle,so=std::sin(angle);
        const float a=std::sin((1.0f-t)*angle)/so,b=std::sin(t*angle)/so;
        for(int k=0;k<3;++k)dir[k]=axis[k]*a+to[k]*b;
    }
    float shot[3];
    Scatter(r,dir,g.spread,shot);
    float mm[16];
    AimMatrix(p,shot,mm);
    if(Fire(r.obj,mm,g.bullet)) {
        ++g.shots;
        if(g.shots<=2 || g.shots%200==0)
            Log("EDF41ROBO %p %s gun round #%u from %ls at %.0f %.0f %.0f: %ls speed %.2f life %d damage %.1f blast %.1f size %.1f",r.obj,
                kGunNames[group],g.shots,z.locator.c_str(),p[0],p[1],p[2],g.factory,g.bullet.speed,g.bullet.life,g.bullet.damage,
                g.bullet.explosion,g.bullet.size);
    }
    PlaySe(r,g.shotSe,p);
}

void GunTick(Robo& r,Gun& g,int group) noexcept {
    ++g.frames;
    switch(g.state) {
    case kG0:
        if(g.autoFire && !(r.flags&1) && !NetFollows(r) && --g.autoCount<=0) {   // online: a guest copy's start comes from the host (NetGun)
            g.autoCount=g.autoMin+RandInt(r,g.autoMax-g.autoMin);
            GunEnter(r,g,kG1);
            NetGun(r,group);
        }
        break;
    case kG1:
        if(g.frames>=g.charge)GunEnter(r,g,kG2);
        break;
    case kG2:
        if(g.counter--==0) {
            g.counter=g.interval;
            for(int i=0;i<g.muzzles;++i)FireOne(r,g,i,group);
        }
        if(g.duration>=0 && g.frames>g.duration)GunEnter(r,g,kG0);
        break;
    }
}

// ---- CAS ------------------------------------------------------------------------------------------------------------
bool Play(Robo& r,int state,const wchar_t* why) noexcept {
    if(state<0)return false;
    int before=-1;
    float t=0.0f,len=0.0f;
    ClipNow(r.obj,&before,&t,&len);
    const bool ok=PlayStateIndex(r.obj,state);
    if(ok && before==state)ClipRewind(r.obj);   // the same state again (a second walk_f_lr): from its start
    r.evState=-2;     // the events start over with the new clip
    if(r.logged<400) { ++r.logged; Log("EDF41ROBO %p plays %ls (state %d) %s",r.obj,why,state,ok ? "" : "REFUSED"); }
    return ok;
}
bool ClipEnded(Robo& r) noexcept {
    int st=0;
    float t=0.0f,len=0.0f;
    if(!ClipNow(r.obj,&st,&t,&len))return true;
    return t>=len-1e-3f;
}

// ---- the body state machine -------------------------------------------------------------------------------------------
void ToRobot(Robo& r) noexcept;
void BodyEnter(Robo& r,int s) noexcept;

void BodySet(Robo& r,int s) noexcept {
    // exits
    if(r.body==kS9)GunEnter(r,r.gun[kLarge],kG0);
    if(r.logged<400) { ++r.logged; Log("EDF41ROBO %p body %s -> %s (form %d, ai %s)",r.obj,kBodyNames[r.body],kBodyNames[s],r.form,kAiNames[r.ai]); }
    r.body=s;
    r.bodyFrames=0;
    BodyEnter(r,s);
    NetBody(r);   // online: the deciding copy tells the guests (edf41net)
}

const Motion* MotionOf(const Robo& r,int state) noexcept {
    for(const Motion& m:r.motions)
        if(m.stateIndex==state)return &m;
    return nullptr;
}

// S8: a step or turn clip, the pose it starts from kept (4.1 +0x324 / +0x328).
void Step(Robo& r,int state,int chain) noexcept {
    r.pendingState=state;
    r.chain=chain;
    BodySet(r,kS8);
}

void BodyEnter(Robo& r,int s) noexcept {
    switch(s) {
    case kS0:
        r.flags=0;
        Play(r,r.stShip,L"ship");
        for(int g=kSmall;g<=kLarge;++g)GunEnter(r,r.gun[g],kG0);
        break;
    case kS1:
        r.flags=0;
        if(r.gun[kSmall].state==kG0)GunEnter(r,r.gun[kSmall],kG1);
        break;
    case kS2:
        r.flags=0;
        if(r.gun[kMiddle].state==kG0)GunEnter(r,r.gun[kMiddle],kG1);
        break;
    case kS3: {
        r.flags=8;
        PlaySe(r,L"ロボ飛行形態バスター砲チャージ",r.pos);
        Gun& g=r.gun[kLarge];
        g.charge=300; g.interval=31; g.duration=600; g.aimLimit=0.0f; g.spread=0.1f;
        GunEnter(r,g,kG1);
        break;
    }
    case kS4:
        r.flags=9;
        Play(r,r.stTransform,L"transform");
        break;
    case kS5:
        r.flags=9;
        ToRobot(r);
        r.landed=false;
        Play(r,r.stFall,L"fall");
        break;
    case kS6:
        r.flags=9;
        Play(r,r.stFallLand,L"fall_land");
        break;
    case kS7:
        r.flags=0;
        break;
    case kS8:
        r.flags=0;
        r.stepYaw=r.yaw;
        std::memcpy(r.stepPos,r.pos,12);
        r.stepMotion=MotionOf(r,r.pendingState);
        r.stepState=r.pendingState;
        Play(r,r.pendingState,L"a step");
        break;
    case kS9:
        r.flags=0;
        r.stepMotion=nullptr;
        Play(r,r.pendingState,L"an attack");
        break;
    case kS10: {
        r.flags=1;
        for(Gun& g:r.gun)GunEnter(r,g,kG0);
        r.dieFrames=static_cast<int>((8.0f+7.0f*Rand(r))*60.0f);
        r.stepMotion=nullptr;
        if(r.form==1)Play(r,r.stDead,L"dead");
        break;
    }
    case kS11: {
        r.flags=1;
        const float up[3]={0.0f,1.0f,0.0f};
        BigExplosion(r.pos,up,2.0f,1.0f);   // 4.1 0x1B2AB0 at the ragdoll's first body, size 2
        PlaySe(r,L"ロボ死亡",r.pos);
        break;
    }
    default:
        break;
    }
}

// Feet: 0 together, 1 the left ahead by more than 10 along the body's forward, 2 the right (4.1 0x277F20).
int FootPhase(Robo& r) noexcept {
    float l[16],rr[16];
    if(!LocatorMatrix(r.footL,l) || !LocatorMatrix(r.footR,rr))return 0;
    const float f[3]={std::sin(r.yaw),0.0f,std::cos(r.yaw)};
    const float d=(l[12]-rr[12])*f[0]+(l[14]-rr[14])*f[2];
    return d>10.0f ? 1 : d<-10.0f ? 2 : 0;
}

void BodyTick(Robo& r) noexcept {
    ++r.bodyFrames;
    switch(r.body) {
    case kS0:
        if(r.cmdTransform)BodySet(r,kS4);
        else if(r.cmdSmall)BodySet(r,kS1);
        else if(r.cmdMiddle)BodySet(r,kS2);
        else if(r.cmdBuster)BodySet(r,kS3);
        break;
    case kS1: if(r.gun[kSmall].state==kG0)BodySet(r,kS0); break;
    case kS2: if(r.gun[kMiddle].state==kG0)BodySet(r,kS0); break;
    case kS3: if(r.gun[kLarge].state==kG0)BodySet(r,kS0); break;
    case kS4: if(r.bodyFrames>2 && ClipEnded(r))BodySet(r,kS5); break;
    case kS5: if(r.landed)BodySet(r,kS6); break;
    case kS6: if(r.bodyFrames>2 && ClipEnded(r))BodySet(r,kS7); break;
    case kS7: {   // 4.1 0x278810
        // the move: 0 none (|x| and |z| under 0.5), 1 / 2 sideways (x >= 0 / x < 0), 3 / 4 forward / back (z >= 0 / z < 0)
        const float ax=std::fabs(r.cmdX),az=std::fabs(r.cmdZ);
        const int mv=(ax<0.5f && az<0.5f) ? 0 : ax>az ? (r.cmdX<0.0f ? 2 : 1) : (r.cmdZ<0.0f ? 4 : 3);
        const int phase=FootPhase(r);
        if(phase==1 || phase==2) {   // a foot ahead: carry on (lr / rl) or bring it back (ln / rn); +0x328 the backward chain
            const int on=phase==1 ? 1 : 2,home=phase==1 ? 3 : 4;
            if(mv==3 && r.chain==0)Step(r,r.stWalkF[on],0);
            else if(mv==4 && r.chain==1)Step(r,r.stWalkB[on],1);
            else if(r.chain==0)Step(r,r.stWalkF[home],0);
            else Step(r,r.stWalkB[home],1);
            break;
        }
        // feet together: the move's first step, then a turn replaces it, else an attack does (the same frame, in this order)
        if(mv==3)Step(r,r.stWalkF[0],0);
        else if(mv==4)Step(r,r.stWalkB[0],1);
        else if(mv==1)Step(r,r.stSideL,0);
        else if(mv==2)Step(r,r.stSideR,0);
        if(r.turn) {
            const int k=(std::min)(3,std::abs(r.turn))-1;
            if(r.body==kS8)r.body=kS7;   // the step just chosen is replaced
            Step(r,r.turn>0 ? r.stTurnL[k] : r.stTurnR[k],0);
        } else if(r.cmdAttack>=0 && r.cmdAttack<static_cast<int>(r.attacks.size())) {
            r.pendingState=r.attacks[static_cast<std::size_t>(r.cmdAttack)].stateIndex;
            BodySet(r,kS9);
        }
        break;
    }
    case kS8:
    case kS9:
        if(r.bodyFrames>2 && ClipEnded(r))BodySet(r,kS7);
        break;
    case kS10:
        if(r.bodyFrames>=r.dieFrames)BodySet(r,kS11);
        break;
    case kS11:
        if(r.bodyFrames>=600 && !r.deleted) {   // 4.1 0x56BBC0 after 10 s
            r.deleted=true;
            Log("EDF41ROBO %p wreck removed",r.obj);
            DeleteObject(r.obj);
        }
        break;
    default:
        break;
    }
}

// 4.1 0x2780C0: robot form, the knee and shoulder guns firing on their own.
void ToRobot(Robo& r) noexcept {
    if(r.form==1)return;
    r.form=1;
    const float k=Cfg().edf41RoboGunEvery>0 ? 0.0f : 1.0f;   // dev: the guns' own short timers below
    Gun& sm=r.gun[kSmall];
    Gun& md=r.gun[kMiddle];
    sm.autoFire=true; sm.autoCount=0; sm.autoMin=static_cast<int>(900*k); sm.autoMax=static_cast<int>(1200*k);
    md.autoFire=true; md.autoCount=0; md.autoMin=static_cast<int>(300*k); md.autoMax=static_cast<int>(480*k);
    if(Cfg().edf41RoboGunEvery>0) { sm.autoMin=sm.autoMax=md.autoMin=md.autoMax=Cfg().edf41RoboGunEvery; }
    r.vel[0]=r.vel[1]=r.vel[2]=0.0f;
    r.roll=0.0f;
    Log("EDF41ROBO %p robot form",r.obj);
}

// ---- the AI ---------------------------------------------------------------------------------------------------------
// 4.1 0x2795C0: the ship turns at most 0.01 rad a frame towards the target; the robot asks for a turn clip by the angle.
void TurnTo(Robo& r,const float* point,float most) noexcept {
    const float d[3]={point[0]-r.pos[0],0.0f,point[2]-r.pos[2]};
    float l[3];
    ToLocal(r.yaw,d,l);
    const float a=(l[0]==0.0f && l[2]==0.0f) ? 0.0f : std::atan2(l[0],l[2]);
    if(r.form==0) { r.yawRate=(std::max)(-most,(std::min)(most,a)); return; }
    const float w=WrapPi(a);
    r.turn=w>2.19911f ? 3 : w<-2.19911f ? -3 : w>1.25664f ? 2 : w<-1.25664f ? -2 : w>0.628319f ? 1 : w<-0.628319f ? -1 : 0;
}
// 4.1 0x279420: the move command towards `point` + up, in the object's frame, at most `speed` long.
void MoveTo(Robo& r,const float* point,float up,float speed) noexcept {
    const float d[3]={point[0]-r.pos[0],point[1]+up-r.pos[1],point[2]-r.pos[2]};
    float l[3];
    ToLocal(r.yaw,d,l);
    const float n=Len3(l);
    const float k=n>speed ? speed/n : 1.0f;
    r.cmdX=l[0]*k; r.cmdY=l[1]*k; r.cmdZ=l[2]*k;
}

void Side(Robo& r) noexcept {   // 4.1 0x279FA1 (literally: a reroll every frame while the interval counter is above 0)
    if(r.sideFrames-->0)r.cmdX=r.sideAmount;
    else if(r.sideInterval-->0)RerollSide(r);
}

float TargetDistance(const Robo& r) noexcept {
    const float d[3]={r.target[0]-r.pos[0],r.target[1]-r.pos[1],r.target[2]-r.pos[2]};
    return Len3(d);
}

void AiSet(Robo& r,int a) noexcept {
    if(r.logged<400) { ++r.logged; Log("EDF41ROBO %p ai %s -> %s",r.obj,kAiNames[r.ai],kAiNames[a]); }
    r.ai=a;
    r.aiFrames=0;
    if(a==kA2) { Reroll(r); RerollSide(r); }
}

void ShipAi(Robo& r) noexcept {
    if(r.phase1-->0) {
        float p[3]={r.target[0],r.target[1],r.target[2]};
        float d[2]={r.pos[0]-r.target[0],r.pos[2]-r.target[2]};
        const float dl=std::sqrt(d[0]*d[0]+d[1]*d[1]);
        const float rad=r.radius>0.0f ? r.radius : 0.0f;
        if(dl>1e-4f) { p[0]+=d[0]/dl*rad; p[2]+=d[1]/dl*rad; }
        TurnTo(r,r.target,0.01f);
        MoveTo(r,p,250.0f,r.aiSpeed);
    } else if(r.phase2-->0) {
        r.cmdX=r.cruise[0]; r.cmdY=r.cruise[1]; r.cmdZ=r.cruise[2];
        if(r.cruiseTurnFrames-->0)r.yawRate=r.cruiseTurnRate;
    } else Reroll(r);
    Side(r);
    if(r.body!=kS0)return;
    const float dist=TargetDistance(r);
    const int every=Cfg().edf41RoboGunEvery;
    if(every>0) {   // dev: the three volleys in turn every `every` frames, whatever the distance
        if(r.aiFrames%every==0) {
            const int k=(r.aiFrames/every)%3;
            (k==0 ? r.cmdSmall : k==1 ? r.cmdMiddle : r.cmdBuster)=true;
        }
        return;
    }
    if(TickTimer(r,r.tSmall,dist))r.cmdSmall=true;
    if(TickTimer(r,r.tMiddle,dist))r.cmdMiddle=true;
    if(TickTimer(r,r.tBuster,dist))r.cmdBuster=true;
}

void RobotAi(Robo& r) noexcept {
    const float h[2]={r.target[0]-r.pos[0],r.target[2]-r.pos[2]};
    const float hd=std::sqrt(h[0]*h[0]+h[1]*h[1]);
    if(r.phase1-->0) {
        TurnTo(r,r.target,0.0f);
        if(r.approach)r.cmdZ=hd<r.radius ? -1.0f : 1.0f;
    } else if(r.phase2-->0) {
        r.cmdZ=r.cruise[2]>0.0f ? 1.0f : -1.0f;
    } else Reroll(r);
    Side(r);
    if(r.cmdX!=0.0f)r.cmdX=r.cmdX>0.0f ? 1.0f : -1.0f;
    const int forced=Cfg().edf41RoboAttack;
    if(forced>=0 && r.tAttack.count>600)r.tAttack.count=600;   // dev: an attack every 10 s
    if(TickTimer(r,r.tAttack,0.0f))AiSet(r,kA4);
}

// 4.1 0x27A770: the entries whose distance and angle windows hold the target, one of them at random.
void ChooseAttack(Robo& r) noexcept {
    const float d[3]={r.target[0]-r.pos[0],r.target[1]-r.pos[1],r.target[2]-r.pos[2]};
    float l[3];
    ToLocal(r.yaw,d,l);
    const float angle=WrapPi(std::atan2(l[0],l[2]));
    const float dist=Len3(d);
    int pick[16],n=0;
    for(std::size_t i=0;i<r.attacks.size() && n<16;++i) {
        const AttackEntry& a=r.attacks[i];
        if(dist>=a.dmin && dist<=a.dmax && angle>=a.amin && angle<=a.amax && a.stateIndex>=0)pick[n++]=static_cast<int>(i);
    }
    const int forced=Cfg().edf41RoboAttack;
    if(forced>=0 && forced<static_cast<int>(r.attacks.size()))pick[0]=forced, n=1;
    r.attackPick=n ? pick[RandInt(r,n)] : -1;
    Log("EDF41ROBO %p attack choice: target %.0f m at %.2f rad, %d entries fit -> %d %ls",r.obj,dist,angle,n,r.attackPick,
        r.attackPick>=0 ? r.attacks[static_cast<std::size_t>(r.attackPick)].state.c_str() : L"(none)");
}

void AiTick(Robo& r) noexcept {
    ++r.aiFrames;
    // the frame's command starts empty (4.1 0x279390), and stays so while the body holds it (0x276B42)
    r.cmdX=r.cmdY=r.cmdZ=r.yawRate=0.0f;
    r.turn=0;
    r.cmdTransform=r.cmdSmall=r.cmdMiddle=r.cmdBuster=false;
    r.cmdAttack=-1;
    float p[3];
    // GameObjectBase's target (the nearest hostile registered object, renewed every 60 frames: 4.1's base does the same at
    // 0x28D7F4), aimed at its first lock point; the player when the object has none yet.
    r.haveTarget=AimPoint(Target(r.obj),p) || NearestPlayer(r.pos,p);
    if(r.haveTarget)std::memcpy(r.target,p,12);
    switch(r.ai) {
    case kA0:
        AiSet(r,kA1);   // 4.1 waits for +0x260 (encountered); the script's CreateEnemy makes these active
        break;
    case kA1:
        if(r.form==1) { AiSet(r,kA2); break; }
        if(r.haveTarget)ShipAi(r);
        break;
    case kA2:
        AiSet(r,kA3);
        break;
    case kA3:
        if(r.haveTarget)RobotAi(r);
        break;
    case kA4:
        if(r.body==kS7) { ChooseAttack(r); AiSet(r,kA5); }
        break;
    case kA5:
        r.cmdAttack=r.attackPick;
        if(r.body!=kS7 || r.attackPick<0)AiSet(r,kA3);
        break;
    }
    if(r.flags&1) { r.cmdX=r.cmdY=r.cmdZ=r.yawRate=0.0f; r.turn=0; r.cmdAttack=-1; }
}

// ---- motion (4.1 slot 1, 0x2766A0) ------------------------------------------------------------------------------------
void Sample(const Motion& m,float t,float* out) noexcept {
    const std::size_t keys=m.keys.size()/3;
    const float f=m.length>0.0f ? (std::max)(0.0f,(std::min)(1.0f,t/m.length))*static_cast<float>(keys-1) : 0.0f;
    const std::size_t a=(std::min)(keys-1,static_cast<std::size_t>(f)),b=(std::min)(keys-1,a+1);
    const float w=f-static_cast<float>(a);
    for(int k=0;k<3;++k)out[k]=m.keys[a*3+k]*(1.0f-w)+m.keys[b*3+k]*w;
}

void Move(Robo& r) noexcept {
    if(r.form==0) {
        if(r.body==kS10) {   // a dead ship drops (4.1: its ragdoll falls; the plugin has no ragdoll to hand it to)
            r.fall-=0.008f;
            r.pos[1]+=r.fall;
            float hit[3];
            const float from[3]={r.pos[0],r.pos[1]+2.0f,r.pos[2]},to[3]={r.pos[0],r.pos[1],r.pos[2]};
            if(GroundHit(from,to,hit,static_cast<char*>(r.obj)+kEaoRagdoll)) { r.pos[1]=hit[1]; r.fall=0.0f; }
            return;
        }
        r.yaw=WrapPi(r.yaw+r.yawRate);
        const float l[3]={r.cmdX,r.cmdY,r.cmdZ};
        float w[3];
        ToWorld(r.yaw,l,w);
        for(int k=0;k<3;++k) { r.vel[k]+=(w[k]-r.vel[k])*0.0025f; r.pos[k]+=r.vel[k]/60.0f; }
        const float want=(std::max)(-0.9425f,(std::min)(0.9425f,-30.0f*r.yawRate));
        r.roll+=(want-r.roll)*0.1f;
        return;
    }
    // robot: root motion of the clip S8 plays, then the ground
    if(r.body==kS8 && r.stepMotion) {
        int st=0;
        float t=0.0f,len=0.0f;
        if(ClipNow(r.obj,&st,&t,&len) && st==r.stepState) {
            float k[3];
            Sample(*r.stepMotion,t,k);
            const float l[3]={k[0],0.0f,k[1]};
            float w[3];
            ToWorld(r.stepYaw,l,w);
            r.pos[0]=r.stepPos[0]+w[0];
            r.pos[2]=r.stepPos[2]+w[2];
            r.yaw=WrapPi(r.stepYaw+k[2]);
        }
    }
    float hit[3];
    // 4.1 casts from 100 m above the feet down to them (1 m below once landed). Its filter misses its own bodies; EDF6's map
    // ray does not, and a body the ray meets above the feet lifted the robot every frame (M411 real run 2026-10-15): the feet's
    // part (2 m above them down) first, then the rest of 4.1's segment (sunk into a slope: back up), own ragdoll bodies passed.
    void* ragdoll=static_cast<char*>(r.obj)+kEaoRagdoll;
    const float knee[3]={r.pos[0],r.pos[1]+2.0f,r.pos[2]},to[3]={r.pos[0],r.pos[1]-(r.landed ? 1.0f : 0.0f),r.pos[2]};
    const float top[3]={r.pos[0],r.pos[1]+100.0f,r.pos[2]};
    if(GroundHit(knee,to,hit,ragdoll) || GroundHit(top,knee,hit,ragdoll)) {
        r.pos[1]=hit[1];
        r.fall=0.0f;
        r.landed=true;
    } else {
        r.landed=false;
        r.fall-=0.008f;
        r.pos[1]+=r.fall;
    }
}

void WriteMatrix(Robo& r) noexcept {
    char* o=static_cast<char*>(r.obj);
    if(!Readable(o+kMatrixAt,64))return;
    const float cy=std::cos(r.yaw),sy=std::sin(r.yaw),cr=std::cos(r.roll),sr=std::sin(r.roll);
    // Rz(roll) * Ry(yaw), rows x y z, times the creation scale (CreateEnemy's size argument)
    const float x[3]={cy,0.0f,-sy},y[3]={0.0f,1.0f,0.0f},z[3]={sy,0.0f,cy};
    float m[16]={};
    for(int k=0;k<3;++k) {
        m[k]=(x[k]*cr+y[k]*sr)*r.scale;
        m[4+k]=(-x[k]*sr+y[k]*cr)*r.scale;
        m[8+k]=z[k]*r.scale;
    }
    m[12]=r.pos[0]; m[13]=r.pos[1]; m[14]=r.pos[2]; m[15]=1.0f;
    std::memcpy(o+kMatrixAt,m,sizeof(m));
}

// ---- the clips' MAB events -------------------------------------------------------------------------------------------
void RaiseEvent(Robo& r,const Event& e) noexcept {
    ++r.events;
    switch(e.kind) {
    case kEvSe:
        PlaySe(r,e.text.c_str(),r.pos);
        break;
    case kEvSeStop:
        break;   // 4.1 stops the channel's loop (ロボ３６０発射); the cues the plugin plays are one-shots
    case kEvHit: {   // 4.1 0x277790: damage x s at the locator, radius scale x the locator's size
        LocatorHandle l{};
        float m[16];
        if(!FindLocator(Model(r.obj),e.text.c_str(),&l) || !LocatorMatrix(l,m)) { Fail("an attack range locator is missing"); break; }
        const float size=(std::max)(Len3(m),1.0f);
        const float radius=e.b*size,damage=e.a*r.s;
        Quake(m+12,e.c,radius*10.0f);            // 4.1 0x5689F0 (message 0x80000018), quake default 0.2
        Smoke(m+12,e.b*size*0.7f);               // 4.1 0x1B7140, the dust: scale x size x 0.7
        // The damage: a still grenade blast (Blast). Its explosion is drawn too, so it goes off only when the target is within
        // reach of the circle; 4.1's area damage (0x28B930) draws nothing.
        const float d[3]={r.target[0]-m[12],r.target[1]-m[13],r.target[2]-m[14]};
        const bool inReach=r.haveTarget && Len3(d)<=radius+10.0f;
        if(inReach && Blast(r.obj,m+12,radius,damage))++r.blasts;
        if(++r.hits<=24) { Log("EDF41ROBO %p hit %ls at %.0f %.0f %.0f radius %.1f damage %.1f%s",r.obj,e.text.c_str(),m[12],m[13],m[14],radius,damage,inReach ? " (blast: the target is within reach)" : ""); }
        break;
    }
    case kEvBeamStart: {   // 4.1 0x277C10: the hand guns fire until 砲撃終了
        Gun& g=r.gun[kLarge];
        g.aimLimit=0.0f; g.duration=-1; g.interval=30; g.spread=0.1f; g.charge=static_cast<int>(e.a);
        GunEnter(r,g,kG1);
        break;
    }
    case kEvBeamEnd:
        GunEnter(r,r.gun[kLarge],kG0);
        break;
    case kEvGenocide: {   // 4.1 0x277DB0
        Gun& g=r.gun[kGenocide];
        g.aimLimit=0.0f; g.spread=0.0f; g.duration=1; g.interval=30; g.charge=static_cast<int>(e.a);
        GunEnter(r,g,kG1);
        break;
    }
    default:
        break;
    }
}

void Events(Robo& r) noexcept {
    int st=0;
    float t=0.0f,len=0.0f;
    if(!ClipNow(r.obj,&st,&t,&len))return;
    if(st!=r.evState) { r.evState=st; r.evClip=0; r.evTime=-1.0f; }
    else if(t+1e-3f<r.evTime)++r.evClip;   // the state's next clip (attack_fire_genocide: charge, then the shot)
    for(const ClipEvents& c:r.clipEvents) {
        if(c.stateIndex!=st || c.clip!=r.evClip)continue;
        for(const Event& e:c.events)
            if(e.t>r.evTime && e.t<=t)RaiseEvent(r,e);
    }
    r.evTime=t;
}

// ---- creation, update, messages ------------------------------------------------------------------------------------
bool Setup(Robo& r) noexcept {
    char* o=static_cast<char*>(r.obj);
    if(!Readable(o+kMatrixAt,64))return false;
    float m[16];
    std::memcpy(m,o+kMatrixAt,sizeof(m));
    std::memcpy(r.pos,m+12,12);
    r.scale=Len3(m+8);
    if(!(r.scale>0.01f && r.scale<100.0f))r.scale=1.0f;
    r.yaw=std::atan2(m[8],m[10]);
    r.rng=reinterpret_cast<std::uintptr_t>(r.obj)*0x9E3779B97F4A7C15ull^GetTickCount64();
    if(!ReadData(r)) { Log("EDF41ROBO %p: its SGO lacks the plugin's keys (edf41_robo_events / _motion / edf41_se / robo_attack_table) or CAS states: left as it is",r.obj); return false; }
    Gun& sm=r.gun[kSmall];
    Gun& md=r.gun[kMiddle];
    Gun& lg=r.gun[kLarge];
    Gun& gn=r.gun[kGenocide];
    const bool guns=ReadMuzzles(r,sm,L"robo_small_muzzle_setting") && ReadMuzzles(r,md,L"robo_middle_muzzle_setting")
        && ReadMuzzles(r,lg,L"robo_large_muzzle_setting") && ReadMuzzles(r,gn,L"robo_large_muzzle_setting")
        && ReadBullet(r,sm,L"small_bullet_class",L"small_bullet_custom_param") && ReadBullet(r,md,L"cannon_bullet_class",L"cannon_bullet_custom_param")
        && ReadBullet(r,lg,L"cannon_bullet_class",L"cannon_bullet_custom_param") && ReadBullet(r,gn,L"genocide_bullet_class",L"genocide_bullet_custom_param");
    if(!guns) { Log("EDF41ROBO %p: the gun keys are not what 4.1's SGO has: left as it is",r.obj); return false; }
    FindLocator(Model(r.obj),L"左足歩行衝撃",&r.footL);
    FindLocator(Model(r.obj),L"右足歩行衝撃",&r.footR);
    FindLocator(Model(r.obj),L"ロックオン",&r.lockOn);
    SetLevel(r);
    // The team. EventAnimationObject's constructor registers the object on team 6 (hostile to nobody, never a target, not
    // counted: PRIMITIVES 7); the real M415 run (2026-10-15) found the robots still on 6 at their first update, firing bullets
    // of team 6. 4.1's robot is an enemy whatever made it: team 6 becomes the enemy team 1 (registered); a team a script set
    // otherwise is kept.
    std::int32_t team0=0;
    if(Readable(o+kTeamAt,4))std::memcpy(&team0,o+kTeamAt,4);
    if(team0==6) {
        const bool ok=SetTeam(r.obj,1,true);
        Log("EDF41ROBO %p team 6 (EventAnimationObject's) -> enemy team 1 %s",r.obj,ok ? "set" : "NOT set");
    }
    r.lockPoint=AddLockPoint(r.obj);
    r.driveOn=edf41c::PlaySe(r.obj,L"ロボ駆動音",r.pos,&r.drive);   // se_table[0], looped (its cue loops)
    Reroll(r);
    RerollSide(r);
    BodyEnter(r,kS0);
    float durability=0.0f;
    if(Readable(o+kDurabilityAt,4))std::memcpy(&durability,o+kDurabilityAt,4);
    std::int32_t team=0;
    if(Readable(o+kTeamAt,4))std::memcpy(&team,o+kTeamAt,4);
    Log("EDF41ROBO %p set up: pos %.0f %.0f %.0f yaw %.2f scale %.2f level %.2f (s %.2f) durability %.0f team %d lock point %p drive %d; %zu event clips, %zu motions, %zu sounds, %zu attacks; muzzles %d/%d/%d/%d feet %d/%d",
        r.obj,r.pos[0],r.pos[1],r.pos[2],r.yaw,r.scale,r.level,r.s,durability,team,r.lockPoint,r.driveOn ? 1 : 0,r.clipEvents.size(),r.motions.size(),static_cast<std::size_t>(Count(Key(r.obj,L"edf41_se"))),
        r.attacks.size(),sm.muzzles,md.muzzles,lg.muzzles,gn.muzzles,r.footL.entry ? 1 : 0,r.footR.entry ? 1 : 0);
    return true;
}

// The dev placement (ini Edf41RoboPlace): the object put that far in front of the camera, once, high when a ship.
void Place(Robo& r) noexcept {
    const float d=Cfg().edf41RoboPlace;
    if(r.placed || d<=0.0f)return;
    float eye[3],fwd[3];
    if(!CameraNow(eye,fwd))return;
    const float h=std::sqrt(fwd[0]*fwd[0]+fwd[2]*fwd[2]);
    if(h<1e-3f)return;
    r.placed=true;
    r.pos[0]=eye[0]+fwd[0]/h*d;
    r.pos[2]=eye[2]+fwd[2]/h*d;
    r.pos[1]=eye[1]+(r.form==0 ? 150.0f : 50.0f);
    std::memcpy(r.stepPos,r.pos,12);   // a step under way goes on from here
    r.yaw=std::atan2(eye[0]-r.pos[0],eye[2]-r.pos[2]);
    Log("EDF41ROBO %p placed %.0f m in front of the camera (ini Edf41RoboPlace): %.0f %.0f %.0f",r.obj,d,r.pos[0],r.pos[1],r.pos[2]);
}

void Tick(Robo& r) noexcept {
    ++r.ticks;
    if(!r.placed && r.ticks<=600 && !NetFollows(r))Place(r);   // the camera may not be a view yet on the first frames (a guest copy: the host's place)
    // dev: transform this long after the creation, as the script's ObjectAction 0 would
    const float after=Cfg().edf41RoboTransformAfter;
    if(after>0.0f && !r.forcedTransform && r.form==0 && r.ticks>=static_cast<std::uint32_t>(after*60.0f) && !NetFollows(r)) {
        r.forcedTransform=true;
        Log("EDF41ROBO %p transform forced (ini Edf41RoboTransformAfter=%.0f)",r.obj,after);
        if(Alive(r.obj))BodySet(r,kS4);
    }
    if(r.body<kS10) {
        float durability=1.0f;
        const char* o=static_cast<const char*>(r.obj);
        if(Readable(o+kDurabilityAt,4))std::memcpy(&durability,o+kDurabilityAt,4);
        if(!(durability>0.0f)) {
            Log("EDF41ROBO %p destroyed (%s form) at its frame %u",r.obj,r.form ? "robot" : "ship",r.ticks);
            PlaySe(r,r.form ? L"ロボ死亡" : L"ロボ飛行形態死亡",r.pos);
            const float back[3]={-std::sin(r.yaw),0.0f,-std::cos(r.yaw)},up[3]={0.0f,1.0f,0.0f};
            if(r.form)Explosion(r.pos,up,2.0f*r.s);      // 4.1 0x1B2070 at a hit point
            else BigExplosion(r.pos,back,2.0f,0.75f);          // 4.1: towards -forward, size 2
            StopSound(r.drive,60);
            BodySet(r,kS10);
        }
    }
    if(r.body<kS10 && !NetFollows(r))AiTick(r);   // online: a guest copy's decisions are the host's (NetIn)
    else { r.cmdX=r.cmdY=r.cmdZ=r.yawRate=0.0f; r.turn=0; }
    BodyTick(r);
    Move(r);
    NetPose(r);   // online: a guest copy pulled to the host's pose
    for(int g=0;g<4;++g)GunTick(r,r.gun[g],g);
    WriteMatrix(r);
    MoveSound(r.drive,r.pos);
    NetOut(r);    // online: the deciding copy's periodic state
    if(r.ticks%600==0) {
        float durability=0.0f;
        const char* o=static_cast<const char*>(r.obj);
        if(Readable(o+kDurabilityAt,4))std::memcpy(&durability,o+kDurabilityAt,4);
        Log("EDF41ROBO %p frame %u: %s / %s, form %d, pos %.0f %.0f %.0f yaw %.2f, target %s %.0f m, durability %.0f, guns %d%d%d%d rounds %u/%u/%u/%u, MAB events %u (hits %u, blasts %u)",
            r.obj,r.ticks,kBodyNames[r.body],kAiNames[r.ai],r.form,r.pos[0],r.pos[1],r.pos[2],r.yaw,r.haveTarget ? "seen" : "none",
            r.haveTarget ? TargetDistance(r) : 0.0f,durability,r.gun[0].state,r.gun[1].state,r.gun[2].state,r.gun[3].state,
            r.gun[0].shots,r.gun[1].shots,r.gun[2].shots,r.gun[3].shots,r.events,r.hits,r.blasts);
    }
}

std::uint64_t __fastcall DtorWrap(void* self,unsigned flags) {
    {
        std::lock_guard<std::mutex> hold(lock);
        auto it=robos.find(self);
        if(it!=robos.end()) { StopSound(it->second.drive,1); robos.erase(it); }
        NetForget(self);   // online (edf41net)
    }
    Log("EDF41ROBO destructor %p",self);
    return reinterpret_cast<DtorFn>(roboClass.orig[kSlotDtor])(self,flags);
}

std::uint64_t __fastcall UpdateWrap(void* self,void* dt) {
    {
        std::lock_guard<std::mutex> hold(lock);
        auto it=robos.find(self);
        if(it!=robos.end() && !it->second.broken) {
            Robo& r=it->second;
            if(!r.ready) {
                r.ready=true;
                r.broken=!Setup(r);
            }
            if(!r.broken) { NetIn(r); Tick(r); }   // NetIn: online (edf41net)
        }
    }
    NetMirrorMake(self);   // development (ini Edf41NetMirror): made outside the lock (the factory takes it)
    const std::uint64_t result=reinterpret_cast<UpdateFn>(roboClass.orig[kSlotUpdate])(self,dt);
    {
        std::lock_guard<std::mutex> hold(lock);
        auto it=robos.find(self);
        if(it!=robos.end() && it->second.ready && !it->second.broken) {
            Robo& r=it->second;
            Events(r);
            WriteMatrix(r);
            float m[16];
            if(r.lockPoint && r.lockOn.entry && LocatorMatrix(r.lockOn,m))SetLockPoint(r.lockPoint,m+12);   // the class's update leaves the matrix alone; keep ours for the next frame's draw
            float aim[3];
            if(Alive(self) && AimPoint(self,aim))DevShoot(self,aim);   // development (ini Edf41DevShoot)
        }
    }
    return result;
}

// ObjectAction (4.1 0x2758B0): 0 transform when alive, 1 robot at once (stand) + approach, 2 approach; damage while
// transforming is refused (4.1 slot 10 answers false in S4).
std::uint64_t __fastcall MessageWrap(void* self,std::uint32_t msg,void* data) {
    if(msg==kMsgAction && data && Readable(data,4)) {
        const std::int32_t action=*static_cast<const std::int32_t*>(data);
        std::lock_guard<std::mutex> hold(lock);
        auto it=robos.find(self);
        if(it!=robos.end()) {
            Robo& r=it->second;
            Log("EDF41ROBO %p ObjectAction %d (form %d, %s)",self,action,r.form,kBodyNames[r.body]);
            if(!r.ready) { r.ready=true; r.broken=!Setup(r); }
            if(!r.broken) {
                if(action==0) {
                    if(Alive(self))BodySet(r,kS4);
                    return 1;
                }
                if(action==1) {
                    ToRobot(r);
                    BodySet(r,kS7);
                    Play(r,r.stStand,L"stand");
                    r.approach=true;
                } else if(action==2)r.approach=true;
            }
            // Never handed on: EventAnimationObject's OnMessage (0x5B6540) answers 0x1000000D from its SGO's `action` table,
            // which this object has not, and throws (EDF+0x5B6A01, M411's ObjectAction 1 on the real game 2026-10-15). 4.1's
            // base did nothing with 0 / 1 / 2 either.
            return 1;
        }
    }
    if(msg==kMsgDamage) {
        std::lock_guard<std::mutex> hold(lock);
        auto it=robos.find(self);
        if(it!=robos.end() && it->second.body==kS4)return 0;
    }
    return reinterpret_cast<MessageFn>(roboClass.orig[kSlotMessage])(self,msg,data);
}

// Slot 6 (OnLevel, called by SetLevel 0x54E740 the first time: the script's CreateEnemy sets the level right after the creation,
// before a durability percentage, native 3019): EventAnimationObject's is `return true` (0x125270) and never scales the
// durability; 4.1's 0x2744B0 makes it 12000 x s x the difficulty: UfoCarrier508's formula (0x4F6D00) does the same in EDF6.
constexpr int kSlotOnLevel=6;
using OnLevelFn=bool(__fastcall*)(void*);
bool __fastcall OnLevelWrap(void* self) {
    const bool r=reinterpret_cast<OnLevelFn>(roboClass.orig[kSlotOnLevel])(self);
    float level=0.0f,max=0.0f;
    SizeDurability(self);
    level=LevelS(self)*10.0f;
    if(Readable(static_cast<char*>(self)+kMaxDurabilityAt,4))std::memcpy(&max,static_cast<char*>(self)+kMaxDurabilityAt,4);
    Log("EDF41ROBO %p level %.2f: durability sized to %.0f (UfoCarrier508's formula)",self,level,max);
    return r;
}

void CreateRobo(void* info) noexcept {
    void* obj=CloneObject(roboClass,kEaoCtor,kEaoSize,info);
    if(!obj)return;
    // EventAnimationObject's constructor sets +0x380 bit 0 (0x5B6406: hits take no HP); 4.1's robot loses its durability.
    SetFlag(obj,kNoHpLoss,false);
    {
        std::lock_guard<std::mutex> hold(lock);
        Robo& r=robos[obj];
        r=Robo{};
        r.obj=obj;
    }
    edf41net::Attach(obj);   // online: its NetworkObject takes the host's events (edf41net)
    Log("EDF41ROBO create #%d: UfoRobo %p built by EventAnimationObject's constructor, vtable -> clone",++created,obj);
}

// ---- online (src/edf41net.h; docs/edf41-uforobo.md 2.4, docs/edf41-classes.md 联机) -------------------------------------------
// The robot is the script's own object (CreateEnemy: it has the game's network identity, the host's). Its copy on the host
// decides as before and tells the guests:
//  - kNetBody on each body state change (the state, the clip of a step / an attack, the backward chain, the dying frames, the
//    form, approach, where a step starts): the ship's volleys, the transform, every step, turn and attack choice;
//  - kNetGun when a robot-form gun group starts on its own timer (the one random start outside the body machine);
//  - kNetState every kNetEvery frames: the pose, the velocity, the target point.
// A guest copy runs no AI and no gun timer of its own: it enters the host's states (their clips, the clips' MAB events, the
// sounds, the blasts and the guns' G1 -> G2 -> G0 run here, aimed at the host's target point) and its pose is pulled to the
// host's. Its bullets and blasts are its own, as each machine's copy of a stock enemy fires its own: the hit on a player is
// judged on that player's machine (SoldierBase slot 34 0x5A3600), so each lands once. Durability is the game's (damage judged
// by the shooter's machine, the HP change broadcast): a guest sees the death from its own HP (Tick) or the host's kNetBody.
enum NetKind : std::uint8_t { kNetBody=1, kNetGun=2, kNetState=3 };
struct NetBodyMsg {
    std::int32_t body,pending,chain,dieFrames,form,approach;
    float stepPos[3],stepYaw,pos[3],yaw;
};
static_assert(sizeof(NetBodyMsg)==56,"fixed wire layout");
struct NetGunMsg { std::int32_t group,state,autoCount,reserved; };
static_assert(sizeof(NetGunMsg)==16,"fixed wire layout");
struct NetStateMsg {
    float pos[3],yaw,roll,vel[3],target[3];
    std::int32_t haveTarget,body,form,flags;
    std::uint32_t ticks;
};
static_assert(sizeof(NetStateMsg)==64,"fixed wire layout");
constexpr std::uint32_t kNetEvery=15;   // frames between two state messages (4 a second, 88 bytes each with the header)

struct RoboNet {
    bool follows=false;          // this frame: a guest copy
    bool mirror=false;           // the development mirror (ini Edf41NetMirror)
    const void* of=nullptr;      // the mirror: the robot it follows
    bool want=false,made=false;  // the original: its mirror asked for, made
    bool dressed=false;
    float offset[3]={};          // the mirror stands this far from what it follows
    bool snap=false;
    NetStateMsg last{};
    std::uint32_t age=0;
    std::uint32_t sent=0,applied=0,logged=0;
};
std::unordered_map<const void*,RoboNet> nets;   // under `lock`, as the robots

bool NetFollows(const Robo& r) noexcept {
    const auto it=nets.find(r.obj);
    return it!=nets.end() && it->second.follows;
}

void NetBody(Robo& r) noexcept {
    if(NetFollows(r))return;
    NetBodyMsg m{};
    m.body=r.body; m.pending=r.pendingState; m.chain=r.chain; m.dieFrames=r.dieFrames; m.form=r.form; m.approach=r.approach ? 1 : 0;
    std::memcpy(m.stepPos,r.stepPos,12); m.stepYaw=r.stepYaw;
    std::memcpy(m.pos,r.pos,12); m.yaw=r.yaw;
    if(edf41net::Send(r.obj,kNetBody,&m,sizeof(m)))++nets[r.obj].sent;
}

void NetGun(Robo& r,int group) noexcept {
    if(NetFollows(r) || group<0 || group>3)return;
    const NetGunMsg m{group,r.gun[group].state,r.gun[group].autoCount,0};
    if(edf41net::Send(r.obj,kNetGun,&m,sizeof(m)))++nets[r.obj].sent;
}

void NetOut(Robo& r) noexcept {
    if(NetFollows(r) || r.ticks%kNetEvery!=0)return;
    NetStateMsg m{};
    std::memcpy(m.pos,r.pos,12); m.yaw=r.yaw; m.roll=r.roll; std::memcpy(m.vel,r.vel,12); std::memcpy(m.target,r.target,12);
    m.haveTarget=r.haveTarget ? 1 : 0; m.body=r.body; m.form=r.form; m.flags=static_cast<std::int32_t>(r.flags); m.ticks=r.ticks;
    if(edf41net::Send(r.obj,kNetState,&m,sizeof(m))) {
        RoboNet& n=nets[r.obj];
        if(++n.sent<=3 || n.sent%2000==0) {
            const edf41net::Stats s=edf41net::Counters();
            Log("EDF41ROBO %p online: %u events sent (layer: sent %u, looped %u, received %u, refused %u)",r.obj,n.sent,s.sent,s.looped,s.received,s.refused);
        }
    }
}

void ApplyBody(Robo& r,RoboNet& n,const NetBodyMsg& m) noexcept {
    if(m.body<kS0 || m.body>kS11 || m.form<0 || m.form>1 || m.pending< -1 || m.pending>4096 || m.chain<0 || m.chain>1 || m.dieFrames<0
       || m.dieFrames>100000 || !edf41net::Sane(m.stepPos,3) || !edf41net::Sane(&m.stepYaw,1) || !edf41net::Sane(m.pos,3) || !edf41net::Sane(&m.yaw,1)) {
        Fail("a body event out of range (refused)");
        return;
    }
    const bool turned=m.form==1 && r.form==0 && m.body!=kS4 && m.body!=kS5;   // S5's entry turns it itself
    if(turned)ToRobot(r);
    if(m.body>=kS10 && r.body<kS10) {   // the host's copy died first: the death Tick plays when the HP runs out
        PlaySe(r,r.form ? L"ロボ死亡" : L"ロボ飛行形態死亡",r.pos);
        const float back[3]={-std::sin(r.yaw),0.0f,-std::cos(r.yaw)},up[3]={0.0f,1.0f,0.0f};
        if(r.form)Explosion(r.pos,up,2.0f*r.s);
        else BigExplosion(r.pos,back,2.0f,0.75f);
        StopSound(r.drive,60);
    }
    const bool same=r.body==m.body && ((m.body!=kS8 && m.body!=kS9) || r.pendingState==m.pending);
    r.approach=m.approach!=0;
    if(!same) {
        r.pendingState=m.pending;
        r.chain=m.chain;
        BodySet(r,m.body);
    }
    if(turned && m.body==kS7)Play(r,r.stStand,L"stand");   // ObjectAction 1 on the host: robot at once, standing
    if(m.body==kS8) {   // the step starts where the host's did (its root motion runs here from there)
        for(int k=0;k<3;k+=2)r.stepPos[k]=r.pos[k]=m.stepPos[k]+n.offset[k];
        r.stepYaw=r.yaw=m.stepYaw;
    }
    if(m.body>=kS10)r.dieFrames=m.dieFrames;
    ++n.applied;
    if(n.logged<60) {
        ++n.logged;
        Log("EDF41ROBO %p %s copy follows: body %s%s (clip state %d, form %d)",r.obj,n.mirror ? "mirror" : "guest",kBodyNames[m.body],
            same ? " (already)" : "",m.pending,m.form);
    }
}

void ApplyGun(Robo& r,const NetGunMsg& m) noexcept {
    if(m.group<0 || m.group>3 || m.state!=kG1 || m.autoCount<0 || m.autoCount>100000) { Fail("a gun event out of range (refused)"); return; }
    Gun& g=r.gun[m.group];
    if(g.state==kG0)GunEnter(r,g,kG1);
    g.autoCount=m.autoCount;
}

void ApplyState(Robo& r,RoboNet& n,const NetStateMsg& m) noexcept {
    if(!edf41net::Sane(m.pos,3) || !edf41net::Sane(&m.yaw,1) || !edf41net::Sane(&m.roll,1) || !edf41net::Sane(m.vel,3,1.0e5f)
       || !edf41net::Sane(m.target,3)) { Fail("a state event out of range (refused)"); return; }
    n.last=m;
    n.snap=true;
    n.age=0;
    r.haveTarget=m.haveTarget!=0;
    for(int k=0;k<3;++k)r.target[k]=m.target[k]+n.offset[k];
}

void NetApply(void* context,std::uint8_t kind,const unsigned char* data,std::size_t size) noexcept {
    Robo& r=*static_cast<Robo*>(context);
    RoboNet& n=nets[r.obj];
    if(kind==kNetBody && size==sizeof(NetBodyMsg)) { NetBodyMsg m; std::memcpy(&m,data,sizeof(m)); ApplyBody(r,n,m); }
    else if(kind==kNetGun && size==sizeof(NetGunMsg)) { NetGunMsg m; std::memcpy(&m,data,sizeof(m)); ApplyGun(r,m); }
    else if(kind==kNetState && size==sizeof(NetStateMsg)) { NetStateMsg m; std::memcpy(&m,data,sizeof(m)); ApplyState(r,n,m); }
    else Fail("an event of a kind or size this version does not send (refused)");
}

// The mirror's line every 300 frames: what it shows against what it follows.
void MirrorReport(Robo& r,RoboNet& n) noexcept {
    const auto it=robos.find(n.of);
    if(it==robos.end()) { Log("EDF41ROBO mirror %p frame %u: the robot it follows is gone; body %s",r.obj,r.ticks,kBodyNames[r.body]); return; }
    const Robo& o=it->second;
    float d[3];
    for(int k=0;k<3;++k)d[k]=r.pos[k]-(o.pos[k]+n.offset[k]);
    int sm=-1,so=-1;
    float tm=0.0f,to=0.0f,lm=0.0f,lo=0.0f;
    ClipNow(r.obj,&sm,&tm,&lm);
    ClipNow(o.obj,&so,&to,&lo);
    Log("EDF41ROBO mirror %p frame %u: body %s / followed %s, form %d / %d, clip state %d at %.0f / %d at %.0f, pose off by %.1f m and %.3f rad, guns %d%d%d%d / %d%d%d%d, %u events applied",
        r.obj,r.ticks,kBodyNames[r.body],kBodyNames[o.body],r.form,o.form,sm,tm,so,to,Len3(d),WrapPi(r.yaw-o.yaw),r.gun[0].state,r.gun[1].state,
        r.gun[2].state,r.gun[3].state,o.gun[0].state,o.gun[1].state,o.gun[2].state,o.gun[3].state,n.applied);
}

void NetIn(Robo& r) noexcept {
    RoboNet& n=nets[r.obj];
    n.follows=edf41net::Guest(r.obj);
    if(n.mirror && !n.dressed) {   // the development mirror: on team 6 (its bullets and blasts hurt nobody), no target, no HP loss, not counted
        n.dressed=true;
        SetTeam(r.obj,6,true);
        SetFlag(r.obj,kNotTarget|kNotCounted|kNoHpLoss,true);
        Log("EDF41ROBO mirror %p follows %p: team 6, never a target, takes no damage, not counted (ini Edf41NetMirror)",r.obj,n.of);
    }
    if(!n.follows) {
        if(!n.mirror && !n.made && r.ticks>=30 && edf41net::Mirror())n.want=true;
        return;
    }
    r.cmdX=r.cmdY=r.cmdZ=r.yawRate=0.0f;
    r.turn=0;
    r.cmdTransform=r.cmdSmall=r.cmdMiddle=r.cmdBuster=false;
    r.cmdAttack=-1;
    ++n.age;
    edf41net::Drain(r.obj,&NetApply,&r);
    if(n.mirror && r.ticks%300==0)MirrorReport(r,n);
}

void NetPose(Robo& r) noexcept {
    const auto it=nets.find(r.obj);
    if(it==nets.end() || !it->second.follows || !it->second.snap || r.body>=kS10)return;   // a dying copy falls on its own
    const RoboNet& n=it->second;
    const NetStateMsg& s=n.last;
    float want[3],d[3];
    for(int k=0;k<3;++k)want[k]=s.pos[k]+n.offset[k]+(r.form==0 ? s.vel[k]*static_cast<float>(n.age)/60.0f : 0.0f);
    for(int k=0;k<3;++k)d[k]=want[k]-r.pos[k];
    const float dist=Len3(d);
    if(r.form==0) {   // the ship: velocity the host's, position pulled 8 % a frame (at once from 300 m off)
        const float k=dist>300.0f ? 1.0f : 0.08f;
        for(int i=0;i<3;++i) { r.pos[i]+=d[i]*k; r.vel[i]=s.vel[i]; }
        r.yaw=WrapPi(r.yaw+WrapPi(s.yaw-r.yaw)*0.08f);
        r.roll+=(s.roll-r.roll)*0.08f;
    } else if(r.body!=kS8 && r.body!=kS5 && r.landed) {   // the robot between steps: its feet slide slowly to the host's place
        const float k=dist>100.0f ? 1.0f : 0.03f;
        r.pos[0]+=d[0]*k;
        r.pos[2]+=d[2]*k;
        if(r.body!=kS9)r.yaw=WrapPi(r.yaw+WrapPi(s.yaw-r.yaw)*0.05f);
    }
}

void NetMirrorMake(void* self) noexcept {
    alignas(16) float m[16];
    float offset[3];
    {
        std::lock_guard<std::mutex> hold(lock);
        const auto it=nets.find(self);
        const auto rt=robos.find(self);
        if(it==nets.end() || !it->second.want || it->second.made || rt==robos.end())return;
        it->second.want=false;
        it->second.made=true;
        const Robo& r=rt->second;
        const float side=120.0f;   // to its right (the yaw frame's +X row: cos yaw, 0, -sin yaw)
        offset[0]=std::cos(r.yaw)*side; offset[1]=0.0f; offset[2]=-std::sin(r.yaw)*side;
        if(!Readable(static_cast<char*>(self)+kMatrixAt,64))return;
        std::memcpy(m,static_cast<char*>(self)+kMatrixAt,sizeof(m));
        for(int k=0;k<3;++k)m[12+k]+=offset[k];
    }
    void* o=CreateFromPath(m,L"app:/object/UfoRobo401.sgo");   // the factory runs here and takes `lock`
    std::lock_guard<std::mutex> hold(lock);
    if(!o || robos.find(o)==robos.end()) { Log("EDF41ROBO %p: the development mirror was not made (ini Edf41NetMirror)",self); return; }
    RoboNet& mn=nets[o];
    mn.mirror=true;
    mn.of=self;
    std::memcpy(mn.offset,offset,sizeof(offset));
    Log("EDF41ROBO %p: development mirror %p made 120 m to its right; it follows through edf41net's loopback (ini Edf41NetMirror)",self,o);
    edf41net::Loopback(self,o);
}

void NetForget(const void* self) noexcept { nets.erase(self); }
}  // namespace

bool InstallEdf41Robo() noexcept {
    if(!Cfg().edf41Robo) {
        Log("EDF41ROBO off (ini Edf41Robo=0)");
        return false;
    }
    if(!BuildClone(roboClass,&DtorWrap,&UpdateWrap,&MessageWrap)) {
        Log("EDF41ROBO the vtable clone could not be built: off");
        return false;
    }
    SetSlot(roboClass,kSlotOnLevel,reinterpret_cast<void*>(&OnLevelWrap));
    // Slot 22 (CanChangeTeam, asked by SetTeam 0x54EE70): EventAnimationObject's 0x5B6BA0 allows team 6 only, so CreateEnemy's
    // SetTeam(1) left the robots on team 6 (M415 / M411 real runs 2026-10-15): GameObjectBase's (every team).
    AllowAnyTeam(roboClass);
    const bool ok=AddFactory(L"UfoRobo",&CreateRobo);
    Log("EDF41ROBO on: UfoRobo %s",ok ? "queued for the game's registration" : "NOT registered");
    return ok;
}
}  // namespace crew
