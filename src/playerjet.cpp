// Player-flown jets (docs/player-jet-re.md). A player jet is, like the NPC jets (jet.cpp), a Vehicle506_Helicopter
// body from a derived SGO (testrange/gen.py 'edf6tr_pjet_*', tools/make_jets.py EDF6VC_PJET_*.SGO) told apart by
// its speed gain k (veh+0x162C) = its kind's mark (kKinds, 7201-7202: no stock heli, no NPC jet, not the
// submarine). The plugin never crews it (crew.cpp Crew): it stands empty until the player boards it with the
// stock board button; then, while the player holds seat 0, the plugin flies it as a fixed-wing arcade plane,
// in two stages a frame like jet.cpp:
//  - input (slot 55, after the stock step, from crew.cpp InputHook): the stock heli inputs are read off the
//    seat (the left and right sticks and the ascend trigger, as the stock heli reads them), the heli's own
//    input block is zeroed (no rotor lift, no heli steering), and the flight step runs: on the ground it
//    taxis and rolls along its nose, accelerates with the throttle and lifts off at its rotate speed; in the
//    air it always flies forward, at least minAir (no stall), the stick bending its path at most maxG (the
//    lift tilts the body: it banks into a turn), climbing and diving at most kMaxClimb; touching down gently
//    it lands and rolls out, hitting the ground (or a building) hard it is damaged, destroyed below 0 HP;
//  - physics (506 slot 57, chained after jet.cpp's and subcarrier.cpp's hooks): its linear and angular
//    velocity, as jet.cpp writes them. Parked (stopped on the ground, throttle closed) nothing is written:
//    the stock heli code holds it, rotor still.
// The fire bytes stay the stock 506's: the primary trigger fires the two guns (holders 0 and 1), the
// secondary button the missile (holder 2), along the body's nose (vehicle_weapon_setting on the fuselage).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
constexpr unsigned kHeli506=0x17DB238,kPhysics506=0x61B710;
constexpr std::size_t kSlotPhysics=57;
constexpr std::size_t kSpeedGain=0x162C,kBody=0x1650;
constexpr unsigned kSetLinearVelocity=0x11B18F0,kSetAngularVelocity=0x11B1760;
constexpr std::size_t kInLateral=0x1540,kInThrottle=0x1544,kInForward=0x1548,kInW=0x154C,kInYaw=0x1550;
constexpr std::size_t kHpMax=0x2F4,kHp=0x2F8;
// The seat's stick block (docs/heli-input-re.md §4): left stick, right stick, the ascend trigger (the heli's
// collective: analog 0..1 on a pad, 0 or 1 on the keyboard).
constexpr std::size_t kSeatLX=0x2C0,kSeatLY=0x2C4,kSeatRX=0x2D0,kSeatRY=0x2D4,kSeatAscend=0x2E0;
constexpr std::size_t kAreaInset=0xE00;   // jet.cpp kAreaInset: the move-area clamp's inset
constexpr float kNoInset=-1.0e6f;
constexpr std::size_t kCeiling=0x20B2998,kCeilingY=0x3C;
// The heli's "body" part (jet.cpp FixBodyPart): the crash step reads its index unchecked.
constexpr unsigned kFindPart=0x6EA4B0;
constexpr std::size_t kParts=0x1320,kBodyPart=0x1530;
const wchar_t* const kFuselageBones[]={L"bomber501",L"bomber401",L"body"};
// The vehicle's death (docs/player-jet-re.md §4): 0x6329B0(vehicle), what the vehicle message handler
// 0x62ECB0 runs for message 0x1000000F: every seat kicked, HP 0, the dead byte set, then the dead effects.
constexpr unsigned kVehicleDie=0x6329B0;
constexpr float kG=9.8f;

struct Kind {
    const char* name;
    float mark;          // the speed gain k its SGO sets (testrange/gen.py JETS)
    float minAir;        // m/s: the least it flies at in the air (no stall)
    float rotate;        // m/s: it can lift off from here (on its own kAutoRotate faster)
    float top;           // m/s at full throttle (Havok caps a body near 200 m/s: jet.cpp kBodyTop)
    float thrust,brake;  // m/s^2 toward the throttle's speed
    float maxG;          // the most lift, in g
    float roll;          // rad/s: how fast the body turns onto its attitude
    float landMax;       // m/s: the fastest it can touch down without damage
};
constexpr Kind kKinds[]={
    {"fighter",7201.0f, 65.0f,75.0f,195.0f, 16.0f,20.0f, 6.0f,2.6f, 130.0f},
    {"strike", 7202.0f, 60.0f,70.0f,180.0f, 11.0f,15.0f, 5.0f,1.6f, 120.0f},
};
constexpr float kAutoRotate=20.0f;     // m/s over rotate: it lifts off without the stick...
constexpr float kAutoThrottle=0.6f;    // ...with the throttle at least this open (not rolling out a landing)
constexpr float kLiftOffClimb=5.0f;    // m/s up the moment it lifts off
constexpr float kThrottleRate=0.6f;    // the throttle lever's travel a second
constexpr float kDeadZone=0.08f;
constexpr float kTaxiTurn=0.8f;        // rad/s: the slowest taxi turn rate (the nose wheel), less fast
constexpr float kTaxiFull=25.0f;       // ...from this ground speed on (rate times kTaxiFull / speed)
constexpr float kGroundBrake=12.0f;    // m/s^2 rolling with the throttle closed
constexpr float kParkSpeed=0.5f;       // below this, throttle closed: parked (the stock code holds it)
constexpr float kTurnShare=0.9f;       // of maxG a full turn stick pulls (the rest holds it up)
constexpr float kMinUpLift=0.35f;      // g: the body's up never tips below this much lift (pushing it stays upright)
constexpr float kMaxClimb=0.94f;       // sine of the steepest climb or dive (~70 deg): no loops, no gimbal flip
constexpr float kTurnBleed=3.0f;       // m/s^2 lost per g pulled over 1 (jet.cpp kTurnBleed)
// Angle of attack (jet.cpp kAoaPerG): the nose rides this far above the path per g pulled at the middle of
// the speed range, more as it slows (lift ~ aoa * speed^2), kAoaMin to kAoaMax, eased over kAoaTau s. Only
// pitch, along the body's up: the nose never slips sideways off the path.
constexpr float kAoaPerG=0.026f,kAoaMin=-0.05f,kAoaMax=0.2f,kAoaTau=0.3f;
constexpr float kAttGain=6.0f;         // 1/s: the body closes on its attitude this fast (jet.cpp kAttGain)
constexpr float kBodyTop=195.0f;
constexpr float kCeilingGap=12.0f;
constexpr float kWorldWall=2400.0f;    // jet.cpp kWorldWall: the Havok broadphase ends at 3000 m a side
// The ground (Clearance): the body's origin rests about 1.3 m over the ground (heli_rigid_body: the box from
// 0.34 - 1.6 m), so under kTouch it is on it; under kOnGround it rolls; over kOffGround it is in the air.
constexpr float kTouch=3.0f,kOffGround=6.0f;
constexpr float kNoGround=-1e9f,kUnderProbe=600.0f,kGroundProbe=3000.0f;
constexpr float kFloorGap=1.0f,kFloorSweep=3.0f,kUnderClimb=40.0f;
// Touching down: at most kLandSink m/s down, the wings within kLandBank (cosine of the up row's y), the nose
// no lower than kLandNose (sine): a landing; else a crash: kCrashBase of its max HP plus kCrashPerSink per m/s
// over kLandSink and kCrashPerSpeed per m/s over landMax (at most kCrashMax), and one more at most every
// kCrashMs (scraping along the ground is one crash).
constexpr float kLandSink=10.0f,kLandBank=0.77f,kLandNose=-0.26f;
constexpr float kCrashBase=0.2f,kCrashPerSink=0.04f,kCrashPerSpeed=0.01f,kCrashMax=1.5f,kBankCrash=0.3f;
constexpr ULONGLONG kCrashMs=1000;
// Blocked (a building, the map's own walls): for kBlockedMs it made less than kBlockedPart of the way it was
// sent; a crash at the speed it lost, and it bounces back off at minAir.
constexpr float kBlockedPart=0.5f,kBlockedMin=40.0f;
constexpr ULONGLONG kBlockedMs=150;
constexpr ULONGLONG kLogMs=2000,kStaleMs=1500;
// Elevons (jet.cpp Elevons): bones elevon_L/R of the jet model, hinged along their local X.
constexpr std::size_t kModelInst=0xEE0,kInstBones=0x10,kInstBoneCount=0x20,kBoneStride=0x110,kBoneLocal=0x70;
constexpr float kElevonMax=0.35f,kElevonRate=2.0f;
const wchar_t* const kElevonNames[2]={L"elevon_L",L"elevon_R"};

enum class Phase { parked, rolling, air };
const char* const kPhaseNames[]={"parked","rolling","air"};

struct PJet {
    unsigned char* vehicle;
    const void* ctrl;            // its weak-this control block: a new object at the address is not this one
    const Kind* kind;
    bool driven;                 // the player holds seat 0
    bool active;                 // slot 57 writes vel / omega this frame
    bool bodyFixed;
    Phase phase;
    float throttle;              // 0..1, the lever the stick moves
    float vel[3],omega[3];
    float prev[3];               // its position last frame
    bool havePrev;
    float sent[3];               // the velocity it was sent with last frame
    float aoa;                   // the nose above the path (rad, see kAoaPerG)
    float savedInset;
    bool insetSaved;
    LARGE_INTEGER last;
    ULONGLONG seen,logAt,crashAt,blockedSince;
    const unsigned char* model;  // the bone array the elevons were found in
    unsigned char* elevon[2];
    float elevonBind[2][16],elevonSet[2][16],elevonAt[2];
};
constexpr int kMaxJets=16;
PJet jets[kMaxJets]{};

using PhysicsFn=void(__fastcall*)(void*);
using SetVecFn=void(*)(void*,const float*);
using FindPartFn=std::int32_t(__fastcall*)(void*,const wchar_t*);
using DieFn=void(__fastcall*)(void*);
PhysicsFn nextPhysics=nullptr;
bool physicsOk=false,bodyPartOk=false,dieOk=false;

float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
float Len(const float* a) noexcept { return std::sqrt(Dot(a,a)); }
float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
void Cross(const float* a,const float* b,float* out) noexcept {
    const float c[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
    std::memcpy(out,c,12);
}
bool Normalize(float* a) noexcept {
    const float l=Len(a);
    if(!std::isfinite(l) || l<1e-4f)return false;
    a[0]/=l;a[1]/=l;a[2]/=l;
    return true;
}
float Axis(const unsigned char* seat,std::size_t at) noexcept {
    const float x=At<float>(seat,at);
    if(!std::isfinite(x) || std::fabs(x)<kDeadZone)return 0.0f;
    return Clamp(x,-1.0f,1.0f);
}

const Kind* KindOf(const unsigned char* v) noexcept {
    if(!Readable(v,kSpeedGain+4) || At<const unsigned char*>(v,0)!=image+kHeli506)return nullptr;
    const float k=At<float>(v,kSpeedGain);
    for(const auto& kind:kKinds)if(k==kind.mark)return &kind;
    return nullptr;
}

PJet* Find(const unsigned char* v,ULONGLONG ms) noexcept {
    const void* ctrl=At<const void*>(v,kSelfCtrl);
    for(auto& j:jets)if(j.vehicle==v && j.ctrl==ctrl && ms-j.seen<kStaleMs)return &j;
    return nullptr;
}
PJet* Make(unsigned char* v,const Kind* kind,ULONGLONG ms) noexcept {
    for(auto& j:jets) {
        if(j.vehicle && ms-j.seen<kStaleMs)continue;
        j=PJet{};j.vehicle=v;j.ctrl=At<const void*>(v,kSelfCtrl);j.kind=kind;j.seen=ms;
        QueryPerformanceCounter(&j.last);
        return &j;
    }
    return nullptr;
}

// Metres of ground under `p` (jet.cpp Clearance): negative under it, kNoGround with none seen.
float Clearance(const float* p) noexcept {
    const float down[3]={p[0],p[1]-kGroundProbe,p[2]};
    float hit[3];
    if(MapRay(p,down,hit)>=0.0f)return p[1]-hit[1];
    const float top[3]={p[0],p[1]+kUnderProbe,p[2]};
    return MapRay(top,p,hit)>=0.0f ? p[1]-hit[1] : kNoGround;
}
float Ceiling() noexcept {
    const auto p=At<const unsigned char*>(image,kCeiling);
    if(!p || !Readable(p+kCeilingY,4))return 1e9f;
    const float y=At<float>(p,kCeilingY);
    return std::isfinite(y) ? y : 1e9f;
}

void FixBodyPart(unsigned char* v) noexcept {
    if(!bodyPartOk || At<std::int32_t>(v,kBodyPart)!=-1)return;
    for(const auto name:kFuselageBones) {
        const auto i=reinterpret_cast<FindPartFn>(image+kFindPart)(v+kParts,name);
        if(i<0)continue;
        Put<std::int32_t>(v,kBodyPart,i);
        if(cfg.debug)Log("PJET v=%p body part: %ls (%d)",v,name,i);
        return;
    }
    Log("PJET v=%p has no body part (going down it would crash)",v);
}

// The angular velocity that turns the body's rows onto `nose` and `up` (jet.cpp Attitude).
void Attitude(PJet& j,const unsigned char* v,const float* nose,const float* up) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float* r=m;const float* u=m+4;const float* f=m+8;
    float rx[3];Cross(u,f,rx);
    const float hand=Dot(rx,r)>=0.0f ? 1.0f : -1.0f;
    float right[3];Cross(up,nose,right);
    for(int i=0;i<3;++i)right[i]*=hand;
    float w[3]={0,0,0},c[3];
    Cross(r,right,c);for(int i=0;i<3;++i)w[i]+=c[i];
    Cross(u,up,c);for(int i=0;i<3;++i)w[i]+=c[i];
    Cross(f,nose,c);for(int i=0;i<3;++i)w[i]+=c[i];
    for(int i=0;i<3;++i)w[i]*=0.5f*kAttGain;
    const float l=Len(w);
    if(l>j.kind->roll)for(int i=0;i<3;++i)w[i]*=j.kind->roll/l;
    std::memcpy(j.omega,w,12);
}

unsigned char* BoneRecord(const unsigned char* inst,const wchar_t* name) noexcept {
    if(!Readable(inst,kInstBoneCount+4))return nullptr;
    const auto count=At<std::int32_t>(inst,kInstBoneCount);
    const auto bones=At<unsigned char*>(inst,kInstBones);
    if(count<=0 || count>256 || !Readable(bones,static_cast<std::size_t>(count)*kBoneStride))return nullptr;
    for(std::int32_t i=0;i<count;++i) {
        unsigned char* rec=bones+static_cast<std::size_t>(i)*kBoneStride;
        const auto n=At<const wchar_t*>(rec,0);
        if(n && Readable(n,32) && std::wcsncmp(n,name,16)==0)return rec;
    }
    return nullptr;
}

// The elevons after the body's turn (jet.cpp Elevons): both up pitch the nose up, opposite they roll.
void Elevons(PJet& j,unsigned char* v,float dt) noexcept {
    const unsigned char* inst=v+kModelInst;
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    if(!bones)return;
    if(bones!=j.model) {
        j.model=bones;
        for(int i=0;i<2;++i) {
            j.elevon[i]=BoneRecord(inst,kElevonNames[i]);
            if(j.elevon[i])std::memcpy(j.elevonBind[i],j.elevon[i]+kBoneLocal,64);
            j.elevonAt[i]=0.0f;
        }
        if(cfg.debug)Log("PJET v=%p elevons: %s",v,j.elevon[0] && j.elevon[1] ? "found" : "none in this model");
    }
    if(!j.elevon[0] || !j.elevon[1])return;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float* r=m;const float* u=m+4;const float* f=m+8;
    float c[3];
    Cross(j.omega,f,c);const float pitch=Dot(c,u);
    Cross(j.omega,r,c);const float roll=-Dot(c,u);
    const float s=Len(j.vel),pitchMax=j.kind->maxG*kG/(s>j.kind->minAir ? s : j.kind->minAir);
    const float p=Clamp(pitch/pitchMax,-1.0f,1.0f),q=Clamp(roll/j.kind->roll,-1.0f,1.0f);
    const float want[2]={Clamp((p-q)*kElevonMax,-kElevonMax,kElevonMax),Clamp((p+q)*kElevonMax,-kElevonMax,kElevonMax)};
    for(int i=0;i<2;++i) {
        j.elevonAt[i]+=Clamp(want[i]-j.elevonAt[i],-kElevonRate*dt,kElevonRate*dt);
        const float co=std::cos(j.elevonAt[i]),si=std::sin(j.elevonAt[i]);
        const float* b=j.elevonBind[i];
        float* o=j.elevonSet[i];
        for(int x=0;x<4;++x) {
            o[x]=b[x];
            o[4+x]=co*b[4+x]+si*b[8+x];
            o[8+x]=-si*b[4+x]+co*b[8+x];
            o[12+x]=b[12+x];
        }
        std::memcpy(j.elevon[i]+kBoneLocal,o,64);
    }
}

// The pilot's stick, read as the stock heli reads it (docs/player-jet-re.md §2). turn > 0: right (the stock
// yaw is -RX and turns the heading right for RX > 0; the stock lateral -LX moves it right for LX > 0); pitch
// > 0: nose up (stick back / mouse up: the right stick's Y is negative pushed up, as LY is for forward);
// throttle: +1 forward stick or ascend, -1 back stick.
struct Stick { float turn,pitch,throttle; float lx,ly,rx,ry,ascend; };
Stick ReadStick(const unsigned char* seat) noexcept {
    Stick s{};
    s.lx=Axis(seat,kSeatLX);s.ly=Axis(seat,kSeatLY);s.rx=Axis(seat,kSeatRX);s.ry=Axis(seat,kSeatRY);
    const float a=At<float>(seat,kSeatAscend);
    s.ascend=std::isfinite(a) ? Clamp(a,0.0f,1.0f) : 0.0f;
    s.turn=Clamp(s.rx+s.lx,-1.0f,1.0f);
    s.pitch=cfg.playerJetInvertPitch ? s.ry : -s.ry;
    s.throttle=s.ascend>0.5f || s.ly<-0.3f ? 1.0f : s.ly>0.3f ? -1.0f : 0.0f;
    return s;
}

// The right of a path along `dir`, level: the way a right turn bends it (a heading angle a has its nose at
// (sin a, 0, cos a) and a right turn lowers a; docs/player-jet-re.md §2).
void RightOf(const float* dir,float* right) noexcept {
    right[0]=-dir[2];right[1]=0.0f;right[2]=dir[0];
    if(!Normalize(right)){right[0]=-1.0f;right[1]=0.0f;right[2]=0.0f;}
}

void Kill(PJet& j,unsigned char* v,const char* why) noexcept {
    Log("PJET v=%p destroyed: %s",v,why);
    j.active=false;
    if(dieOk)reinterpret_cast<DieFn>(image+kVehicleDie)(v);
    else Put<float>(v,kHp,0.0f);   // no death call on this EDF.dll: the next hit finishes it
}

// A hard hit: `sink` m/s into the ground, `speed` over it, `banked` wings too steep. Damage (see kCrashBase).
void Crash(PJet& j,unsigned char* v,float sink,float speed,bool banked,ULONGLONG ms) noexcept {
    if(ms-j.crashAt<kCrashMs)return;
    j.crashAt=ms;
    const float hpMax=At<float>(v,kHpMax),hp=At<float>(v,kHp);
    const float share=Clamp(kCrashBase+kCrashPerSink*(sink>kLandSink ? sink-kLandSink : 0.0f)+
                            kCrashPerSpeed*(speed>j.kind->landMax ? speed-j.kind->landMax : 0.0f)+(banked ? kBankCrash : 0.0f),
                            kCrashBase,kCrashMax);
    const float left=hp-share*(hpMax>0.0f ? hpMax : 1000.0f);
    Log("PJET v=%p crash: sink %.1f m/s, speed %.0f m/s%s: %.0f%% of max HP, hp %.0f -> %.0f",v,sink,speed,banked ? ", banked" : "",
        share*100.0f,hp,left>0.0f ? left : 0.0f);
    if(left<=0.0f){Kill(j,v,"crashed");return;}
    Put<float>(v,kHp,left);
}

// The throttle lever, moved by the stick.
void Lever(PJet& j,const Stick& s,float dt) noexcept {
    j.throttle=Clamp(j.throttle+s.throttle*kThrottleRate*dt,0.0f,1.0f);
}

// On the ground: it rolls along its nose (level), turns at the nose wheel's rate, speeds up with the throttle
// and brakes with it closed; it lifts off at its rotate speed with the stick back, or kAutoRotate faster.
void Ground(PJet& j,const unsigned char* v,const Stick& s,const float* measured,float clear,float dt) noexcept {
    const Kind& k=*j.kind;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float nose[3]={m[8],0.0f,m[10]};
    if(!Normalize(nose)){nose[0]=0.0f;nose[2]=1.0f;}
    float speed=Dot(j.vel,nose);
    if(speed<0.0f)speed=0.0f;
    const float want=j.throttle*k.top;
    if(j.throttle<0.02f)speed-=kGroundBrake*dt;
    else speed+=Clamp(want-speed,-k.brake*dt,k.thrust*dt);
    if(speed<0.0f)speed=0.0f;
    // The nose wheel: kTaxiTurn at taxi speeds, less from kTaxiFull on.
    const float rate=kTaxiTurn*(speed>kTaxiFull ? kTaxiFull/speed : 1.0f)*(speed>0.5f || s.throttle>0.0f ? 1.0f : 0.0f);
    const float a=-s.turn*rate*dt,co=std::cos(a),si=std::sin(a);
    const float turned[3]={nose[0]*co+nose[2]*si,0.0f,nose[2]*co-nose[0]*si};
    const float vy=measured[1]<0.0f ? (measured[1]>-30.0f ? measured[1] : -30.0f) : 0.0f;
    for(int i=0;i<3;i+=2)j.vel[i]=turned[i]*speed;
    j.vel[1]=vy;
    const float up[3]={0.0f,1.0f,0.0f};
    Attitude(j,v,turned,up);
    if(speed>=k.rotate && (s.pitch>0.2f || (speed>=k.rotate+kAutoRotate && j.throttle>=kAutoThrottle))) {
        j.phase=Phase::air;j.vel[1]=kLiftOffClimb;
        Log("PJET v=%p takeoff at %.0f m/s (throttle %.2f, stick %.2f)",v,speed,j.throttle,s.pitch);
        return;
    }
    if(clear!=kNoGround && clear>kOffGround) {   // rolled off an edge: flying (or falling to minAir)
        j.phase=Phase::air;
        Log("PJET v=%p off the ground at %.0f m/s (%.0f m over it)",v,speed,clear);
        return;
    }
    j.phase=speed<kParkSpeed && j.throttle<0.02f ? Phase::parked : Phase::rolling;
}

// Touching the ground in the air: a landing (it rolls on) or a crash.
void Touch(PJet& j,unsigned char* v,float speed,ULONGLONG ms) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float sink=j.vel[1]<0.0f ? -j.vel[1] : 0.0f;
    const float dirY=speed>1.0f ? j.vel[1]/speed : 0.0f;
    const bool banked=m[5]<kLandBank;
    if(sink<=kLandSink && !banked && dirY>=kLandNose && speed<=j.kind->landMax) {
        j.phase=Phase::rolling;j.vel[1]=0.0f;
        Log("PJET v=%p landed at %.0f m/s, sink %.1f m/s",v,speed,sink);
        return;
    }
    Crash(j,v,sink,speed,banked,ms);
}

// In the air: always forward, at least minAir; the stick bends the path (at most maxG of lift, kTurnShare of it
// for a full turn), the lift tilts the body (it banks into turns); climb and dive at most kMaxClimb.
void Air(PJet& j,unsigned char* v,const Stick& s,const float* pos,float clear,float dt,ULONGLONG ms) noexcept {
    const Kind& k=*j.kind;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float dir[3]={j.vel[0],j.vel[1],j.vel[2]};
    float speed=Len(dir);
    if(!Normalize(dir)){dir[0]=m[8];dir[1]=m[9];dir[2]=m[10];if(!Normalize(dir)){dir[0]=0;dir[1]=0;dir[2]=1;}}
    if(speed<k.minAir)speed=k.minAir;
    float right[3];RightOf(dir,right);
    float up[3]={-dir[0]*dir[1],1.0f-dir[1]*dir[1],-dir[2]*dir[1]};   // world up off the path
    if(!Normalize(up)){up[0]=0;up[1]=1;up[2]=0;}
    const float most=k.maxG*kG;
    float pitch=s.pitch*most;
    if((dir[1]>kMaxClimb && pitch>0.0f) || (dir[1]<-kMaxClimb && pitch<0.0f))pitch=0.0f;
    const float gPerp[3]={dir[0]*kG*dir[1],-kG+dir[1]*kG*dir[1],dir[2]*kG*dir[1]};   // gravity across the path
    float lift[3];
    for(int i=0;i<3;++i)lift[i]=right[i]*s.turn*most*kTurnShare+up[i]*pitch-gPerp[i];
    const float pull=Len(lift);
    if(pull>most)for(int i=0;i<3;++i)lift[i]*=most/pull;
    float next[3];
    for(int i=0;i<3;++i)next[i]=dir[i]+(lift[i]+gPerp[i])*dt/speed;
    if(!Normalize(next))std::memcpy(next,dir,12);
    // The body's up: along the lift, never tipped past kMinUpLift of it (pushing over it stays upright).
    const float side=Dot(lift,right),lv=Dot(lift,up);
    float bodyUp[3];
    for(int i=0;i<3;++i)bodyUp[i]=right[i]*side+up[i]*(lv>kMinUpLift*kG ? lv : kMinUpLift*kG);
    if(!Normalize(bodyUp))std::memcpy(bodyUp,up,12);
    const float g=Len(lift)/kG,bleed=g>1.0f ? (g-1.0f)*kTurnBleed : 0.0f;
    const float want=k.minAir+j.throttle*(k.top-k.minAir);
    speed+=Clamp(want-speed,-k.brake*dt,k.thrust*dt)-(kG*next[1]+bleed)*dt;
    speed=Clamp(speed,k.minAir,k.top<kBodyTop ? k.top : kBodyTop);
    for(int i=0;i<3;++i)j.vel[i]=next[i]*speed;
    // The ceiling the stock input holds every body under, and the world's walls: it slides along them.
    if(pos[1]>Ceiling()-kCeilingGap && j.vel[1]>0.0f)j.vel[1]=0.0f;
    for(int i=0;i<3;i+=2)if(std::fabs(pos[i])>kWorldWall && j.vel[i]*pos[i]>0.0f)j.vel[i]=0.0f;
    // The nose above the path by what the wing needs, pitched about the body's right only (no sideslip).
    const float mid=0.5f*(k.minAir+k.top),slow=mid/speed;
    const float aoaWant=Clamp(kAoaPerG*g*slow*slow,kAoaMin,kAoaMax);
    j.aoa+=(aoaWant-j.aoa)*(dt<kAoaTau ? dt/kAoaTau : 1.0f);
    float nose[3]={next[0],next[1],next[2]};
    const float along=Dot(bodyUp,nose);
    for(int i=0;i<3;++i)bodyUp[i]-=nose[i]*along;
    if(Normalize(bodyUp)){const float c=std::cos(j.aoa),sn=std::sin(j.aoa);
        for(int i=0;i<3;++i){const float n=nose[i],u=bodyUp[i];nose[i]=n*c+u*sn;bodyUp[i]=u*c-n*sn;}}
    else std::memcpy(bodyUp,up,12);
    Attitude(j,v,nose,bodyUp);
    // The ground: under it, out (it went through); touching it or about to within kFloorSweep frames, a
    // landing or a crash, its descent cut to stop kFloorGap over the floor (jet.cpp HoldOffGround).
    if(clear==kNoGround)return;
    if(clear<0.0f){j.vel[1]=j.vel[1]>kUnderClimb ? j.vel[1] : kUnderClimb;return;}
    float floorY=pos[1]-clear;
    if(j.vel[1]<0.0f) {
        const float end[3]={pos[0]+j.vel[0]*dt*kFloorSweep,pos[1]+j.vel[1]*dt*kFloorSweep-kFloorGap,pos[2]+j.vel[2]*dt*kFloorSweep};
        float hit[3];
        if(MapRay(pos,end,hit)>=0.0f && hit[1]>floorY && hit[1]<pos[1])floorY=hit[1];
    }
    const float need=(floorY+kTouch-pos[1])/dt;
    if(j.vel[1]>=0.0f || j.vel[1]>=need)return;
    Touch(j,v,speed,ms);
    if(j.phase==Phase::air && j.vel[1]<need)j.vel[1]=need<0.0f ? need : 0.0f;
}

// Held back by what it flew into (see kBlockedPart): a crash, and it bounces off.
void Blocked(PJet& j,unsigned char* v,const float* measured,ULONGLONG ms) noexcept {
    const float sent=Len(j.sent);
    if(j.phase!=Phase::air || sent<kBlockedMin || Dot(measured,j.sent)>=kBlockedPart*sent*sent){j.blockedSince=0;return;}
    if(!j.blockedSince){j.blockedSince=ms;return;}
    if(ms-j.blockedSince<kBlockedMs)return;
    j.blockedSince=0;
    const float made=Dot(measured,j.sent)/sent;
    Log("PJET v=%p blocked: sent %.0f m/s, made %.0f",v,sent,made);
    Crash(j,v,0.0f,sent-made+j.kind->landMax,false,ms);
    if(!j.active)return;
    for(int i=0;i<3;i+=2)j.vel[i]=-j.vel[i];
    float dir[3]={j.vel[0],0.2f*Len(j.vel),j.vel[2]};
    if(!Normalize(dir))return;
    for(int i=0;i<3;++i)j.vel[i]=dir[i]*j.kind->minAir;
}

void Board(PJet& j,unsigned char* v,const float* pos,const float* measured,float clear) noexcept {
    j.driven=true;j.blockedSince=0;
    if(!j.insetSaved){j.savedInset=At<float>(v,kAreaInset);j.insetSaved=true;}
    const float speed=Len(measured);
    const bool air=clear==kNoGround || clear>kOffGround;
    j.phase=air ? Phase::air : speed>kParkSpeed ? Phase::rolling : Phase::parked;
    std::memcpy(j.vel,measured,12);
    j.throttle=air ? 0.5f : 0.0f;
    Log("PJET v=%p boarded: %s, hp %.0f/%.0f, %s at (%.0f,%.0f,%.0f), %.0f m over the ground, %.0f m/s",v,j.kind->name,
        At<float>(v,kHp),At<float>(v,kHpMax),kPhaseNames[static_cast<int>(j.phase)],pos[0],pos[1],pos[2],clear,speed);
}

void Leave(PJet& j,unsigned char* v) noexcept {
    j.driven=false;j.active=false;
    if(j.insetSaved){Put<float>(v,kAreaInset,j.savedInset);j.insetSaved=false;}
    Log("PJET v=%p left (%s, %.0f m/s)",v,kPhaseNames[static_cast<int>(j.phase)],Len(j.vel));
}

void Report(PJet& j,const unsigned char* v,const Stick& s,const float* pos,float clear,ULONGLONG ms) noexcept {
    if(!cfg.debug || ms-j.logAt<kLogMs)return;
    j.logAt=ms;
    const float speed=Len(j.vel);
    Log("PJET v=%p %s %.0f m/s climb %.1f thr %.2f pos=(%.0f,%.0f,%.0f) clear %.0f hp %.0f in(turn %.2f pitch %.2f) "
        "seat(LX %.2f LY %.2f RX %.2f RY %.2f asc %.2f)",v,kPhaseNames[static_cast<int>(j.phase)],speed,j.vel[1],j.throttle,
        pos[0],pos[1],pos[2],clear,At<float>(v,kHp),s.turn,s.pitch,s.lx,s.ly,s.rx,s.ry,s.ascend);
}

void Fly(PJet& j,unsigned char* v,ULONGLONG ms) noexcept {
    LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
    const float dt=Clamp(static_cast<float>(now.QuadPart-j.last.QuadPart)/static_cast<float>(freq.QuadPart),0.004f,0.1f);
    j.last=now;
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    float measured[3]={0,0,0};
    if(j.havePrev)for(int i=0;i<3;++i)measured[i]=(pos[i]-j.prev[i])/dt;
    std::memcpy(j.prev,pos,12);j.havePrev=true;
    const bool driven=SeatCount(v)>0 && SeatRider(SeatAt(v,0))==Rider::player;
    if(!driven){if(j.driven)Leave(j,v);return;}
    const float clear=Clearance(pos);
    if(!j.driven)Board(j,v,pos,measured,clear);
    const Stick s=ReadStick(SeatAt(v,0));
    // The heli stays out of it: no rotor lift, no heli stick (docs/heli-input-re.md §2a).
    Put<float>(v,kInLateral,0.0f);Put<float>(v,kInForward,0.0f);Put<float>(v,kInYaw,0.0f);
    Put<float>(v,kInThrottle,0.0f);Put<float>(v,kInW,1.0f);
    Put<float>(v,kAreaInset,kNoInset);
    Blocked(j,v,measured,ms);
    if(v[kDead]){j.active=false;return;}
    Lever(j,s,dt);
    if(j.phase==Phase::air)Air(j,v,s,pos,clear,dt,ms);
    else Ground(j,v,s,measured,clear,dt);
    j.active=j.phase!=Phase::parked && !v[kDead];
    std::memcpy(j.sent,j.vel,12);
    Elevons(j,v,dt);
    Report(j,v,s,pos,clear,ms);
}

// Slot 57 of the 506, after the hooks before it: a flown player jet's velocity and spin replace the heli's.
void __fastcall PhysicsHook(void* vehicle) {
    nextPhysics(vehicle);
    __try {
        auto v=static_cast<unsigned char*>(vehicle);
        const ULONGLONG ms=GameMs();
        PJet* j=Find(v,ms);
        if(!j || !j->active || !j->driven || v[kDead] || ms-j->seen>200)return;
        const auto body=At<void*>(v,kBody);
        if(!body)return;
        alignas(16) float lin[4]={j->vel[0],j->vel[1],j->vel[2],0.0f},ang[4]={j->omega[0],j->omega[1],j->omega[2],0.0f};
        reinterpret_cast<SetVecFn>(image+kSetLinearVelocity)(body,lin);
        reinterpret_cast<SetVecFn>(image+kSetAngularVelocity)(body,ang);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

const unsigned char kPhysicsSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8};
const unsigned char kSetLinSig[]={0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00,0x45,0x33,0xC9,0x4C,0x8B,0x50,0x58,0x49,0x8B,0x42,0x18,0x49,0x8D,0x4A,0x18,0x48,0xFF,0xA0,0xA8,0x00,0x00};
const unsigned char kSetAngSig[]={0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00,0x45,0x33,0xC9,0x4C,0x8B,0x50,0x58,0x49,0x8B,0x42,0x18,0x49,0x8D,0x4A,0x18,0x48,0xFF,0xA0,0xB0,0x00,0x00};
const unsigned char kFindPartSig[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x56,0x48,0x83,0xEC,0x50};
// 0x6329B0: mov [rsp+8],rbx; push rdi; sub rsp,20h; imul rax,[rcx+618h],340h (the seat loop)
const unsigned char kDieSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x69,0x81,0x18,0x06,0x00,0x00,0x40,0x03,0x00,0x00};
}  // namespace

bool IsPlayerJet(const void* vehicle) noexcept {
    __try { return KindOf(static_cast<const unsigned char*>(vehicle))!=nullptr; }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void PlayerJetFrame(unsigned char* v) noexcept {
    if(!physicsOk || !cfg.playerJet)return;
    const Kind* kind=KindOf(v);
    if(!kind || v[kDead])return;
    const ULONGLONG ms=GameMs();
    PJet* j=Find(v,ms);
    if(!j)j=Make(v,kind,ms);
    if(!j)return;
    j->seen=ms;
    if(!j->bodyFixed){FixBodyPart(v);j->bodyFixed=true;}
    Fly(*j,v,ms);
}

bool InstallPlayerJets() noexcept {
    __try {
        const bool sig=Matches(kPhysics506,kPhysicsSig,sizeof(kPhysicsSig)) && Matches(kSetLinearVelocity,kSetLinSig,sizeof(kSetLinSig)) &&
                       Matches(kSetAngularVelocity,kSetAngSig,sizeof(kSetAngSig));
        if(!sig){Log("PJET profile mismatch: player jets off");return false;}
        const auto slot=reinterpret_cast<void**>(image+kHeli506)+kSlotPhysics;
        void* const current=*slot;
        nextPhysics=reinterpret_cast<PhysicsFn>(current);
        physicsOk=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&PhysicsHook));
        bodyPartOk=physicsOk && Matches(kFindPart,kFindPartSig,sizeof(kFindPartSig));
        dieOk=physicsOk && Matches(kVehicleDie,kDieSig,sizeof(kDieSig));
        Log("HOOK player jets physics=%d bodyPart=%d die=%d",physicsOk,bodyPartOk,dieOk);
        return physicsOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
