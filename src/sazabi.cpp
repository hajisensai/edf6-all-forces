// The Sazabi (docs/gundam-plan.md, docs/sazabi-re.md): a 25.6 m mobile suit the player pilots, on the V506 heli body like
// the player jets (playerjet.cpp), told apart by its mark (body506.cpp kMarks: 7401-7499; tools/make_sazabi.py writes
// EDF6VC_SAZABI.SGO). The plugin never crews it (crew.cpp Crew) and never flies it as a heli (heli.cpp); with the player
// in seat 0 it walks and flies it, in two stages a frame like the player jets:
//  - input (slot 55, after the stock step, from crew.cpp InputHook: SazabiFrame): the seat's sticks are read, the stock
//    heli input zeroed, and the walk / flight step runs. It hovers KFloat m over what is under its soles (a map ray from
//    its sz_root bone, the soles' level), tracking the ground's height (slopes, kerbs and rubble are no step for it),
//    walks and runs with the left stick (relative to its facing), turns with the right stick or the mouse, which also
//    pitches its aim; the ascend trigger lifts it on its thrusters (a jump from the ground, a climb or a hover in the
//    air) and the dash key throws it along the stick; the thrusters' charge (SazabiThrusterSec at full thrust) refills on
//    the ground. Let go in the air it falls at SazabiGravity, and lands with its knees taking the blow;
//  - physics (506 slot 57, body506.cpp -> SazabiBodyStep): that velocity, and the spin that keeps it upright facing its
//    heading (body506.h BodyAttitude). Empty, nothing is written: the stock body holds it.
// Every frame (empty too) its bones are posed (sazabi_pose.h: gait, flight, landing, the rifle aimed, the tomahawk
// stowed): the local matrices written; the engine composes the worlds (its slot 45, SetWorld 0x1100B90, from the
// model's origin) and draws them (slot 3, 0x1100F10), a frame late at worst (docs/sazabi-re.md §2). The plugin
// composes none itself: SetWorld called with the vehicle matrix (veh+0x60, the box's centre, 12.7 m over the soles,
// not the model's origin) drew the mech that much too high every other frame, two of it flickering (2026-10-07).
// Its arms (the beam rifle, the shield missiles, the tomahawk and the shield's guard, the funnels, the chest's mega
// particle cannon), their effects and sounds, the thrusters' flames and the HUD's cue: sazabi_arms.inc.
// Water is no hazard: the 506's ditching message is taken whole (map rays see the seabed under the sea: it wades).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "body506.h"
#include "exhaust_pose.h"
#include "layout.h"
#include "heli.h"
#include "lockon.h"
#include "map.h"
#include "memory.h"
#include "sazabi_arms.h"
#include "sazabi_assist.h"
#include "sazabi_flames.h"
#include "sazabi_flight.h"
#include "sazabi_pose.h"
#include "sazabi_sound.h"
#include "sazabi_net.h"
#include "online_authority.h"
#include "vecmath.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
using vec::Clamp;using vec::Len;
using namespace szarms;
constexpr float kSazabiMark=7401.0f;   // pylib/vcobjects.py SAZABI_MARK (tools/selftest.py sazabi_bones_agree)
constexpr std::size_t kBody=0x1650;
constexpr std::size_t kInLateral=0x1540,kInThrottle=0x1544,kInForward=0x1548,kInW=0x154C,kInYaw=0x1550;
constexpr std::size_t kAreaInset=0xE00;   // playerjet.cpp: the move-area clamp's inset
constexpr float kNoInset=-1.0e6f;
constexpr std::size_t kSeatLX=0x2C0,kSeatLY=0x2C4,kSeatRX=0x2D0,kSeatRY=0x2D4,kSeatAscend=0x2E0,kSeatPad=0x2B0,
                      kSeatButtons=0x2E8;
constexpr std::uint16_t kButtonA=0x01;
constexpr std::size_t kBoneInvBind=0x30;   // a bone record's inverse bind (rec+0x30, 4x4): -its row 3 is a level bone's joint
constexpr float kDeadZone=0.08f;
// The walk and the flight: sazabi_flight.h (its hover over the ground, the stick's walk share, the boost, the gauge).
namespace fl=sazabi::flight;
using fl::kWalkShare;
constexpr float kProbeUp=2.0f;            // m over its soles the ground ray starts (under kerbs and rubble it stands on)
constexpr float kLandHard=25.0f;          // m/s down: the deepest crouch
constexpr float kPitchRate=1.4f;          // rad/s at full right stick (a pad's aim)
constexpr float kAimMost=55.0f*sazabi::kDeg;
constexpr float kAttitudeGain=6.0f,kAttitudeMost=3.0f;   // BodyAttitude: 1/s, rad/s
constexpr float kAirBlendRate=4.0f,kCrouchDecay=2.2f,kAimRate=3.0f;
constexpr ULONGLONG kLogMs=1000;
constexpr ULONGLONG kTestBoardMs=6000;   // SazabiTestBoard: this long after it is first seen (the player has landed)

struct NetState {
    sazabi_net::Gate gate;
    sazabi_net::State state;
    ULONGLONG sentAt=0,receivedAt=0,shotAt[6]{},soundAt[4]{};
    std::uint32_t soundSerial=0,shotSeen[6]{},soundSeen[4]{};
    std::uint64_t source=0;
    bool networked=false,remote=false,dirty=false;
};

struct Mech {
    ObjRef ref;
    unsigned char* vehicle=nullptr;
    bool driven=false,npc=false,active=false,insetSaved=false,havePrev=false,rigOk=false,rigSaid=false;
    float savedInset=0.0f;
    ULONGLONG frame=0,lastMs=0,logAt=0;
    float prev[3]{},measured[3]{},omega[3]{};
    float heading=0.0f,aimPitch=0.0f,yawRate=0.0f,feetClear=0.0f;
    fl::State fl{};                     // its walk and flight: velocity, in the air, the thrusters' gauge (sazabi_flight.h)
    float sinceBurst=1e3f;              // s since its last burst (a dash or a jump begun): the flames' (sazabi_flames.h)
    const void* bones=nullptr;          // the instance's bone array the records below were found in
    unsigned char* rec[sazabi::kBoneCount]{};
    sazabi::Rig rig{};
    sazabi::PoseInput pose{};
    sazabi::Pose posed{};             // this mech's last pose, also used before its next pose is composed
    exhaust::BodyTrack track{};         // the body's matrix last frame and now (sz_root's world carried: RootFrame)
    float root[16]{},rootInv[16]{};     // sz_root's world this frame, and its inverse
    bool rootOk=false;
    Arms arms{};
    NetState net{};
};
constexpr int kMaxMechs=8;
Mech mechs[kMaxMechs]{};
bool installed=false;
bool testBoarded=false;   // SazabiTestBoard: once a mission
ULONGLONG firstSeenMs=0;

bool Live(const Mech& m) noexcept {
    const auto ctrl=static_cast<const unsigned char*>(m.ref.ctrl);
    return m.vehicle && Readable(ctrl,12) && At<std::int32_t>(ctrl,8)>0 && m.ref.Is(m.vehicle) && !(m.vehicle[0x18]&4) &&
           !m.vehicle[kDead];
}
Mech* Find(const unsigned char* v) noexcept {
    for(auto& m:mechs)if(m.vehicle==v && m.ref.Is(v))return &m;
    return nullptr;
}
void DropLocalAim(Arms& a) noexcept;
Mech* Make(unsigned char* v) noexcept {
    for(auto& m:mechs) {
        if(m.vehicle && Live(m))continue;
        DropLocalAim(m.arms);m=Mech{};m.ref=ObjRef::Of(v);m.vehicle=v;
        return &m;
    }
    static const void* refused=nullptr;
    if(refused!=v)Log("SAZABI v=%p: %d already known: not driven",v,kMaxMechs);
    refused=v;
    return nullptr;
}

bool KeyDown(int vk) noexcept {   // playerjet.cpp KeyDown
    if(vk<=0 || MapHoldsKeys())return false;
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}
float Axis(const unsigned char* seat,std::size_t at) noexcept {
    const float x=At<float>(seat,at);
    if(!std::isfinite(x) || std::fabs(x)<kDeadZone)return 0.0f;
    return Clamp(x,-1.0f,1.0f);
}
float Raw(const unsigned char* seat,std::size_t at) noexcept {
    const float x=At<float>(seat,at);
    return std::isfinite(x) ? Clamp(x,-1.0f,1.0f) : 0.0f;
}

// The seat's controls this frame: move (forward, right, -1..1), turn (rad/s), aim pitch change (rad), ascend 0..1,
// descend, dash (held).
struct Controls { float forward,right,turn,pitch,ascend; bool descend,dash; };
Controls Read(const unsigned char* seat,float dt) noexcept {
    Controls c{};
    c.forward=-Axis(seat,kSeatLY);c.right=Axis(seat,kSeatLX);
    const float a=At<float>(seat,kSeatAscend);
    c.ascend=std::isfinite(a) ? Clamp(a,0.0f,1.0f) : 0.0f;
    const bool keys=At<unsigned char>(seat,kSeatPad)==0;
    if(keys) {   // the mouse's frame movement turns and pitches; the ini's keys dash and descend
        c.turn=-Raw(seat,kSeatRX)*Cfg().sazabiMouseTurn*sazabi::kDeg/(dt>0.0f ? dt : 1.0f/60.0f);
        c.pitch=-Raw(seat,kSeatRY)*Cfg().sazabiMouseTurn*sazabi::kDeg*(Cfg().sazabiInvertAim ? -1.0f : 1.0f);
        c.dash=KeyDown(Cfg().sazabiDashKey);
        c.descend=KeyDown(Cfg().sazabiDescendKey);
        return c;
    }
    c.turn=-Axis(seat,kSeatRX)*Cfg().sazabiTurn*sazabi::kDeg;
    c.pitch=-Axis(seat,kSeatRY)*kPitchRate*dt*(Cfg().sazabiInvertAim ? -1.0f : 1.0f);
    c.dash=(At<std::uint16_t>(seat,kSeatButtons)&kButtonA)!=0;
    return c;
}

float Measure(Mech& m,const float* pos,ULONGLONG ms) noexcept {
    const ULONGLONG since=m.lastMs ? ms-m.lastMs : 0;
    m.lastMs=ms;
    const float dt=GameStep(since);
    if(m.havePrev && since>0)for(int i=0;i<3;++i)m.measured[i]=(pos[i]-m.prev[i])/dt;
    std::memcpy(m.prev,pos,12);m.havePrev=true;
    return dt;
}

// ------------------------------------------------------------------------------------------ the skeleton
// Its bone records (found again whenever the instance's bone array moves) and their joints off the inverse binds.
bool Rig(Mech& m,unsigned char* v) noexcept {
    const unsigned char* inst=v+kModelInst506;
    const void* bones=At<const void*>(inst,kInstBones506);
    if(bones==m.bones && m.rigOk)return true;
    m.bones=bones;m.rigOk=false;
    for(int i=0;i<sazabi::kBoneCount;++i) {
        unsigned char* rec=BoneRecord506(inst,sazabi::kBones[i].name);
        if(!rec) {
            if(!m.rigSaid)Log("SAZABI v=%p: its model lacks bone %ls: not posed (not EDF6VC_SAZABI.MRAB?)",v,sazabi::kBones[i].name);
            m.rigSaid=true;
            return false;
        }
        m.rec[i]=rec;
        const float* ib=reinterpret_cast<const float*>(rec+kBoneInvBind);
        for(int k=0;k<3;++k)m.rig.joint[i][k]=-ib[12+k];
    }
    m.rigOk=true;
    Log("SAZABI v=%p rig: %d bones, pelvis at %.2f m, rifle muzzle (%.2f,%.2f,%.2f)",v,sazabi::kBoneCount,
        m.rig.joint[sazabi::kPelvis][1],m.rig.joint[sazabi::kMuzzle][0],m.rig.joint[sazabi::kMuzzle][1],m.rig.joint[sazabi::kMuzzle][2]);
    // Where the frames are in the world (docs/sazabi-re.md §3: the MAB's seat, door and camera hang on `mdl`).
    const unsigned char* mdl=BoneRecord506(inst,L"mdl");
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    const float* rw=reinterpret_cast<const float*>(m.rec[sazabi::kRoot]+kBoneWorld506);
    const float* mw=mdl ? reinterpret_cast<const float*>(mdl+kBoneWorld506) : p;
    float door[3]{},reach=0.0f;
    const bool seat=SeatPoint(v,0,door,&reach);
    Log("SAZABI v=%p frames: vehicle (%.2f,%.2f,%.2f) mdl (%.2f,%.2f,%.2f) sz_root (%.2f,%.2f,%.2f) door %d (%.2f,%.2f,%.2f) reach %.2f "
        "ground under the vehicle %.2f m",v,p[0],p[1],p[2],mw[12],mw[13],mw[14],rw[12],rw[13],rw[14],seat,door[0],door[1],door[2],reach,
        GroundClearance(p));
    return true;
}

void Pose(Mech& m,unsigned char* v) noexcept {
    if(!Rig(m,v))return;
    sazabi::Animate(m.pose,m.rig,&m.posed);
    for(int i=1;i<sazabi::kBoneCount;++i) {   // sz_root itself is the frame: its local (under body) stays the bind's
        float local[16];
        sazabi::LocalMatrix(m.posed,i,local);
        std::memcpy(m.rec[i]+kBoneLocal506,local,sizeof local);
    }
}

// The soles' height over what is under them: the sz_root bone is at the soles (its world, as last composed).
float FeetClear(const Mech& m) noexcept {
    if(!m.rigOk)return kNoGround;
    const float* w=reinterpret_cast<const float*>(m.rec[sazabi::kRoot]+kBoneWorld506);
    const float probe[3]={w[12],w[13]+kProbeUp,w[14]};
    const float clear=GroundClearance(probe);
    return clear==kNoGround ? kNoGround : clear-kProbeUp;
}

#include "sazabi_arms.inc"
#include "sazabi_pilot.inc"
#include "sazabi_camera.inc"
#include "sazabi_net.inc"

// ------------------------------------------------------------------------------------------ driving
void Board(Mech& m,unsigned char* v,bool npc) noexcept {
    m.driven=true;m.npc=npc;
    if(!m.insetSaved){m.savedInset=At<float>(v,kAreaInset);m.insetSaved=true;}
    const float* f=reinterpret_cast<const float*>(v+kMatrix)+8;
    m.heading=std::atan2(f[0],f[2]);
    m.aimPitch=0.0f;
    std::memcpy(m.fl.vel,m.measured,12);
    m.fl.air=m.feetClear==kNoGround || m.feetClear>fl::kOffGround;
    m.fl.boostHeld=true;m.fl.ascendHeld=true;   // a button held while boarding is no press
    Log("SAZABI v=%p boarded by %s: hp %.0f/%.0f, %s, %.1f m over the ground, thrusters %.0f%%",v,npc ? "an NPC" : "the player",
        At<float>(v,kHp),At<float>(v,kHpMax),m.fl.air ? "in the air" : "on its feet",m.feetClear,m.fl.gauge*100.0f);
}

void Leave(Mech& m,unsigned char* v,bool alive,const char* how) noexcept {
    DropArms(m);
    if(view.vehicle==v)DropView();   // the stock camera again
    m.driven=false;m.npc=false;m.active=false;
    if(m.insetSaved && alive)Put<float>(v,kAreaInset,m.savedInset);
    m.insetSaved=false;
    Log("SAZABI v=%p left: %s",v,how);
}

// The walk and the flight (sazabi_flight.h: the stick, the ascend trigger, the boost, the gauge), and what they set off:
// the dash's roar on a jump or a burst, the landing's crouch and thud.
void Fly(Mech& m,unsigned char* v,const Controls& c,float dt) noexcept {
    fl::Params p;
    p.walk=Cfg().sazabiWalk;p.run=Cfg().sazabiRun;p.fly=Cfg().sazabiFly;p.boost=Cfg().sazabiDash;p.climb=Cfg().sazabiClimb;
    p.gravity=Cfg().sazabiGravity;p.thrusterSec=Cfg().sazabiThrusterSec;p.regen=Cfg().sazabiThrusterRegen;
    fl::Input in;
    in.forward=c.forward;in.right=c.right;in.ascend=c.ascend;in.boost=c.dash;in.descend=c.descend;
    const float feet=m.feetClear==kNoGround ? fl::kNoGround : m.feetClear;
    const fl::Events ev=fl::Step(m.fl,in,m.heading,feet,dt,p);
    m.sinceBurst=ev.jumped || ev.burst ? 0.0f : m.sinceBurst+dt;
    const float* at=m.rootOk ? m.root+12 : nullptr;
    if(at && (ev.jumped || ev.burst))Sfx(m,SzSfx::dash,at);
    if(ev.landed) {
        m.pose.crouch=std::fmax(m.pose.crouch,Clamp(ev.landSpeed/kLandHard,0.3f,1.0f));
        if(at)Sfx(m,SzSfx::land,at);
    }
    if(ev.overheated)Log("SAZABI v=%p overheated: no boost until it lands",v);
}

// The pose's inputs from the motion (sazabi_pose.h PoseInput).
void Animate(Mech& m,float dt,bool driven) noexcept {
    sazabi::PoseInput& p=m.pose;
    const float ground=std::sqrt(m.fl.vel[0]*m.fl.vel[0]+m.fl.vel[2]*m.fl.vel[2]);
    p.t+=dt;
    const float strideWant=m.fl.air || !driven ? 0.0f : Clamp(ground/Cfg().sazabiRun,0.0f,1.0f);
    p.stride+=(strideWant-p.stride)*std::fmin(1.0f,4.0f*dt);
    if(!m.fl.air)p.gait=sazabi::GaitStep(p.gait,ground,p.stride,dt);
    p.air+=((m.fl.air ? 1.0f : 0.0f)-p.air)*std::fmin(1.0f,kAirBlendRate*dt);
    const float leanWant=m.fl.air ? Clamp(ground/Cfg().sazabiFly,0.0f,1.0f)*28.0f*sazabi::kDeg : 0.0f;
    p.lean+=(leanWant-p.lean)*std::fmin(1.0f,5.0f*dt);   // a boost's own lean: the pose's (PoseInput::boost)
    p.bank+=(Clamp(m.yawRate*0.12f,-0.3f,0.3f)-p.bank)*std::fmin(1.0f,4.0f*dt);
    p.crouch=std::fmax(0.0f,p.crouch-kCrouchDecay*dt);
    p.aimPitch=m.aimPitch;
    p.aimYaw=0.0f;
    p.aim+=((driven ? 1.0f : 0.0f)-p.aim)*std::fmin(1.0f,kAimRate*dt);
}

void Report(Mech& m,const unsigned char* v,const Controls& c,ULONGLONG ms) noexcept {
    if(!Cfg().debug || ms-m.logAt<kLogMs)return;
    m.logAt=ms;
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    Log("SAZABI v=%p %s vel=(%.1f,%.1f,%.1f) feet %.2f m heading %.0f aim %.0f thr %.0f%% boost %d heat %d pos=(%.0f,%.0f,%.0f) hp %.0f "
        "in(f %.2f r %.2f turn %.2f asc %.2f)",v,m.fl.air ? "AIR" : "GROUND",m.fl.vel[0],m.fl.vel[1],m.fl.vel[2],m.feetClear,
        m.heading/sazabi::kDeg,m.aimPitch/sazabi::kDeg,m.fl.gauge*100.0f,m.fl.boosting,m.fl.overheat,p[0],p[1],p[2],At<float>(v,kHp),c.forward,
        c.right,c.turn,c.ascend);
    // the frames' axes in the world (docs/sazabi-re.md: what the bones' worlds are against the body), the funnels
    const float* vm=reinterpret_cast<const float*>(v+kMatrix);
    const float* mz=reinterpret_cast<const float*>(m.rec[sazabi::kMuzzle]+kBoneWorld506);
    Log("SAZABI v=%p axes body x(%.2f,%.2f,%.2f) z(%.2f,%.2f,%.2f) root x(%.2f,%.2f,%.2f) z(%.2f,%.2f,%.2f) at (%.1f,%.1f,%.1f) "
        "muzzle z(%.2f,%.2f,%.2f) at (%.1f,%.1f,%.1f) aim %d (%.0f,%.0f,%.0f)",v,vm[0],vm[1],vm[2],vm[8],vm[9],vm[10],m.root[0],m.root[1],
        m.root[2],m.root[8],m.root[9],m.root[10],m.root[12],m.root[13],m.root[14],mz[8],mz[9],mz[10],mz[12],mz[13],mz[14],m.arms.hasAim,
        m.arms.aim[0],m.arms.aim[1],m.arms.aim[2]);
    for(int k=0;k<kFunnelCount;++k) {
        const Funnel& f=m.arms.funnels[k];
        if(f.phase==FunnelPhase::docked)continue;
        const float* fw=reinterpret_cast<const float*>(m.rec[sazabi::kFunnels[k]]+kBoneWorld506);
        Log("SAZABI v=%p funnel %d phase %d at (%.1f,%.1f,%.1f) drawn (%.1f,%.1f,%.1f) scale %.2f",v,k,static_cast<int>(f.phase),f.at[0],
            f.at[1],f.at[2],fw[12],fw[13],fw[14],std::sqrt(fw[0]*fw[0]+fw[1]*fw[1]+fw[2]*fw[2]));
    }
}

void Drive(Mech& m,unsigned char* v,ULONGLONG ms) noexcept {
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float dt=Measure(m,pos,ms);
    if(NetworkFrame(m,v,ms))return; // before player keys, NPC targeting, flight integration and every damage producer
    m.feetClear=FeetClear(m);
    // who drives it: the player in seat 0, or an NPC crew.cpp seated (sazabi_pilot.inc); a change of driver boards anew
    const Rider rider=SeatCount(v)>0 ? SeatRider(SeatAt(v,0)) : Rider::none;
    const bool driven=rider==Rider::player || rider==Rider::dummy,npc=rider==Rider::dummy;
    if(m.driven && (!driven || m.npc!=npc))Leave(m,v,true,m.npc ? "the NPC got out" : "got out");
    if(!driven) {
        RootFrame(m,v);
        Animate(m,dt,false);
        ArmsPose(m,dt);
        Pose(m,v);
        SendNetwork(m,v,ms);
        return;
    }
    if(!m.driven)Board(m,v,npc);
    RootFrame(m,v);
    Controls c{};
    ArmsInput arms{};
    if(npc){Pilot(m,v,dt,&c,&arms);v[kFireGun]=0;v[kFireMissile]=0;}   // the 506 fires nothing of its own
    else{c=Read(SeatAt(v,0),dt);arms=TakeButtons(v,SeatAt(v,0));LockInput(m,v,c,dt);}   // locked, the stick is the lock's
    m.yawRate=c.turn;
    m.heading+=c.turn*dt;
    if(m.heading>sazabi::kPi)m.heading-=2.0f*sazabi::kPi;
    if(m.heading<-sazabi::kPi)m.heading+=2.0f*sazabi::kPi;
    m.aimPitch=Clamp(m.aimPitch+c.pitch,-kAimMost,kAimMost);
    if(!npc)Assist(m,v,dt);     // the lock-on's enemy or the aim assist's, the camera pulled onto it (sazabi_camera.inc)
    Fly(m,v,c,dt);
    const float nose[3]={std::sin(m.heading),0.0f,std::cos(m.heading)},up[3]={0.0f,1.0f,0.0f};
    BodyAttitude(v,nose,up,kAttitudeGain,kAttitudeMost,m.omega);
    // The heli stays out of it (docs/heli-input-re.md §2a), and the move area's clamp too (playerjet.cpp).
    Put<float>(v,kInLateral,0.0f);Put<float>(v,kInForward,0.0f);Put<float>(v,kInYaw,0.0f);
    Put<float>(v,kInThrottle,0.0f);Put<float>(v,kInW,1.0f);
    Put<float>(v,kAreaInset,kNoInset);
    m.active=true;
    if(!npc)PublishView(m,v);   // the camera the look-at hook places (sazabi_camera.inc)
    Animate(m,dt,true);
    Aim(m);                     // the aim point, the arm onto it
    ArmsStep(m,v,arms,dt);
    Pose(m,v);
    // the thrusters push while it climbs, dashes or flies on
    const float thrust=m.fl.boosting || m.fl.climbing ? 1.0f : m.fl.air ? Clamp(Len(m.fl.vel)/Cfg().sazabiFly,0.0f,1.0f)*0.4f : 0.0f;
    ArmsFire(m,v,arms,thrust,dt);
    Footsteps(m);
    if(!npc)PublishCue(m);
    Report(m,v,c,ms);
    SendNetwork(m,v,ms);
}
// SazabiTestBoard (tests only): kTestBoardMs after the first Sazabi is seen, the player on foot is put into it once.
void TestBoard(const Mech& m,unsigned char* v) noexcept {
    if(!Cfg().sazabiTestBoard || testBoarded || m.driven)return;
    const ULONGLONG ms=GameMs();
    if(!firstSeenMs)firstSeenMs=ms;
    unsigned char* const human=PlayerHuman();
    if(ms-firstSeenMs<kTestBoardMs || !human || !HumanOnFoot(human))return;
    testBoarded=true;
    Log("SAZABI v=%p: SazabiTestBoard puts the player into it",v);
    BoardingRequest(v);
}
}  // namespace

bool IsSazabi(const void* vehicle) noexcept { return BodyOf(vehicle)==PluginBody::sazabi; }

// The riding camera of seat `seat` when it is the player's Sazabi's (turretcam.cpp LookHook, whatever thread fetches the
// camera): its eye and the point it looks at, on the centre ray the game thread aims along (sazabi_camera.inc).
bool SazabiCamera(const unsigned char* seat,const float* wasEye,const float* wasLook,float* eye,float* look) noexcept {
    if(!seat || !Cfg().enabled || !Cfg().sazabi)return false;
    AcquireSRWLockShared(&viewLock);
    const View w=view;
    ReleaseSRWLockShared(&viewLock);
    const ULONGLONG now=GameMs();
    if(w.seat!=seat || !w.vehicle || now-w.ms>kViewFresh || !Readable(w.vehicle+kPosition,12) || !Readable(w.vehicle+kMatrix,64))return false;
    float dir[3];
    ViewRay(w,Where(w.vehicle),eye,dir);
    for(int k=0;k<3;++k){look[k]=eye[k]+dir[k]*100.0f;if(!std::isfinite(eye[k]) || !std::isfinite(look[k]))return false;}
    // Debug: what the camera held when fetched (the game's work on the last placement) against this placement
    static ULONGLONG logAt=0;
    if(Cfg().debug && now-logAt>=1000 && wasEye && wasLook) {
        logAt=now;
        const float* p=Where(w.vehicle);
        const float* f=reinterpret_cast<const float*>(w.vehicle+kPosition);
        Log("SAZABI camera: field y %.1f, vehicle (%.1f,%.1f,%.1f) yaw %.0f pitch %.0f placed eye (%.1f,%.1f,%.1f) look (%.1f,%.1f,%.1f) held eye (%.1f,%.1f,%.1f) look (%.1f,%.1f,%.1f)",
            f[1],p[0],p[1],p[2],w.yaw/sazabi::kDeg,w.pitch/sazabi::kDeg,eye[0],eye[1],eye[2],look[0],look[1],look[2],wasEye[0],wasEye[1],wasEye[2],
            wasLook[0],wasLook[1],wasLook[2]);
    }
    placedMs=now;
    return true;
}

// The HUD's view of the player's Sazabi (crew.h SazabiCue): published each frame it is driven (PublishCue).
bool PlayerSazabiCue(SazabiCue* out) noexcept {
    if(!out || !cueMs || GameMs()-cueMs>kSazabiCueMs)return false;
    *out=cue;
    return true;
}

void SazabiFrame(unsigned char* v) noexcept {
    if(!installed || !Cfg().sazabi || !IsSazabi(v))return;
    Mech* m=Find(v);
    if(v[kDead]) {
        if(m && m->driven)Leave(*m,v,false,"destroyed");
        return;
    }
    if(!m && (m=Make(v))==nullptr)return;
    m->frame=GameFrame();
    Drive(*m,v,GameMs());
    TestBoard(*m,v);
}

// The 506 physics step (body506.cpp), after the stock one: the walk's or the flight's velocity, the spin upright.
bool SazabiBodyStep(unsigned char* v,float* lin,float* ang) noexcept {
    if(!Cfg().enabled || !Cfg().sazabi)return false;
    if(drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)) && !IsOnlineAuthority(v))return false;
    const Mech* m=Find(v);
    if(!m || m->net.remote || !m->active || !m->driven || v[kDead] || m->frame+1<GameFrame() || !At<void*>(v,kBody))return false;
    for(int i=0;i<3;++i){lin[i]=m->fl.vel[i];ang[i]=m->omega[i];}
    return true;
}

// The 506's messages to it: the water is no ditching for a mech wading (taken whole); with the shield up, a hit from
// ahead (its point of impact or blast centre, GameDamageInfo +0x30, in front of the mech) does SazabiGuardShare of its
// damage (+0x50; put back after the stock handler, the copy is the queue's own: primer.cpp PrimerMessage).
bool SazabiMessage(unsigned char* v,std::uint32_t msg,void* data,MessageRestore* restore) noexcept {
    if(!Cfg().enabled || !Cfg().sazabi)return false;
    if(msg==kMsgWater)return true;
    constexpr std::size_t kHitPoint=0x30,kDamage=0x50;
    if(msg!=kMsgDamage || !data || v[kDead])return false;
    const Mech* m=Find(v);
    if(!m || !m->driven || m->arms.guard<0.5f || (m->net.remote && !RemoteFresh(*m,v,GameMs())))return false;
    if(!m->net.remote && drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)) && !IsOnlineAuthority(v))return false;
    const float* hit=reinterpret_cast<const float*>(static_cast<unsigned char*>(data)+kHitPoint);
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    if((hit[0]-p[0])*std::sin(m->heading)+(hit[2]-p[2])*std::cos(m->heading)<=0.0f)return false;   // from behind
    float* const damage=reinterpret_cast<float*>(static_cast<unsigned char*>(data)+kDamage);
    if(!(*damage>0.0f))return false;
    restore->at=damage;restore->was=*damage;
    *damage*=m->net.remote ? m->net.state.arms.guardShare : Cfg().sazabiGuardShare;
    return false;
}

bool InstallSazabi() noexcept {
    installed=Body506Ok();
    Log("HOOK sazabi body=%d (mark %.0f)",installed,kSazabiMark);
    if(installed)InstallSazabiNet();
    return installed;
}

void ResetSazabi() noexcept {
    for(auto& m:mechs){DropLocalAim(m.arms);m=Mech{};}
    DropView();
    cueMs=0;
    testBoarded=false;
    firstSeenMs=0;
}

void SazabiNetReceived(unsigned char* v,const sazabi_net::State& state) noexcept {
    if(!installed || !Cfg().enabled || !Cfg().sazabi || !IsSazabi(v) || v[kDead] ||
       !drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)))return;
    Mech* m=Find(v);
    if(!m && (m=Make(v))==nullptr)return;
    if(!m->net.gate.Admit(state,true,IsOnlineAuthority(v),SazabiNetController(v)))return;
    AcceptNetwork(*m,v,state,GameMs());
}
}  // namespace crew
