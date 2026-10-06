// The sidecar motorcycle (边三轮摩托, docs/sidecar-re.md): EDF6VC_SIDECAR.SGO (tools/make_sidecar.py), the Freed bike's
// class (Vehicle503_Bike) on the bike's model with a sidecar (pylib/sidecar_model.py), told from a stock bike by the
// marker bone kMarkerBone. One rider drives (the stock seat: the bike, its two machine guns); the other stands in the
// sidecar and fires their OWN weapons.
//
// Why the gunner is no seat (docs/sidecar-re.md §1): a human in any vehicle seat runs the ride state (SoldierBase
// slot 79, 0x5A1660 -> HumanBase 0x57ADF0), whose entry hides and disables every hand weapon (weapon+0xE6C = 0,
// 0x696150(weapon, 0)) and sets the human's state word +0x5D0 = 0xCD; the weapon loop of the soldier's update
// (0x59ADD6) pulls a weapon's trigger (weapon+0x139) only with (+0x5D0 & 0x84) == 0, so a seated human never fires
// their own weapon, the reload / switch buttons are gated the same way, and an NPC's AI does not shoot from a seat
// either. No seat type (vehicle_riding_position's last field: 0 bike, 1 tank, 2 mech, 5 heli, 6 gunner / passenger)
// changes that: it is the human's state, not the seat's. So the gunner stays ON FOOT, in the human's normal state
// (aim, fire, reload, switch weapons, an NPC's own targeting all stock), and the plugin holds them standing in the
// sidecar's tub, feet on its floor:
//   - their walking is taken away at its one source: the human's pre-update (slot 4, 0x572DF0) turns the move stick
//     +0xD50 (a pad's, or an NPC AI's) into the walk's velocity in one call, 0x56D350 at 0x573B72; that call is
//     redirected here and the stick zeroed first for a held gunner (MoveIntent). The player's stick is kept as an
//     order to the bike (below);
//   - they ride along through the walk controller's own one-step velocity (docs/sidecar-re.md §3a): in the same
//     pre-update the game adds a step's velocity to the controller (0x11B8D90: ctrl+0x60 += v, the stock call at
//     0x573E3A moves a human to a target as (target - position) x 60), the controller's step (0x11B9890, in the
//     human's update) adds it to the character's velocity for this physics step and clears it (0x11B9A92,
//     0x11B9CB7). The held gunner gets the bike's velocity plus the stock (target - position) x 60 toward the
//     gunner's point (Follow), so they move WITH the bike in each physics step. The carried velocity +0x6B0 is no
//     use for this: on the ground the step zeroes it every frame (0x11B9A48) - writing it (the first version) left
//     them standing still in the world while the bike drove off, put back by a warp every 0.25 m: the stutter;
//   - the game's own character warp (0x11B9870, the call the ride state's exit makes, 0x57B187) is left only for
//     a gunner who has still drifted kHold off (a snag, a bike moved by other means);
//   - the tub's floor is solid (the ragdoll's body hull tools/make_sidecar.py moves under it), so they stand on it;
//     its walls are drawn only (a hollow is no one convex hull; walls round a soldier's capsule would squeeze it),
//     the hold keeps them inside (docs/sidecar-re.md §2).
// Getting in: the player's board button by the bike (crew.cpp FindSeatHook asks SidecarBoard first) takes the
// sidecar when they stand nearer its tub than the saddle's door, or the saddle is not theirs to take. Getting
// out: the board button again (taken off the button then, so it does not board the saddle) steps them off beside the
// tub; a jump takes them off too. An NPC: while the player drives, the nearest friendly soldier within
// SidecarNpcRange (the team manager's walk of the player's friends, the same walk the board prompt makes: 0x5E11D0)
// is put in the sidecar and fights from it with their own weapons; the player getting off the saddle lets them go.
// The bike with the player in the sidecar and nobody on the saddle is driven by the plugin: the player's left stick
// pushed sends it the way the camera looks (its drive block, the stock input's: throttle and steering; the
// steering's sign learned from how the heading turns). Its level: a bike with a sidecar must not lean. The chassis'
// angular velocity the car step sets (setAngVel at 0x6746C6, physics.cpp ChassisSetAngVel) loses its roll part,
// replaced by a rate that brings the roll back to level (SidecarLevel): no lean in a turn, no falling over.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "body506.h"
#include "memory.h"
#include <cmath>

namespace crew {
namespace {
// The Freed bike's class (Vehicle503_Bike; crew.cpp kClasses "503_Bike").
constexpr unsigned kVt503=0x17DA508;
// pylib/sidecar_model.py MARKER_BONE / GUNNER_POINT / TUB_OUT (tools/selftest.py holds them equal): the marker bone,
// the gunner's standing point on the tub's floor in the bike's frame (= its model's: +x the bike's left, +y up,
// +z forward: in the cockpit between the seat and the deck, the sides up to their hips), and the tub's outer side.
const wchar_t kMarkerBone[]=L"edf6vc_sidecar";
constexpr float kGunnerX=-1.0f,kGunnerY=0.30f,kGunnerZ=0.35f;
constexpr float kTubOut=-1.46f;
constexpr float kStepOff=0.9f;          // m past the tub's outer side the player steps off to (past its wheel)
// The vehicle: its chassis body (CarBase, veh+0x1698: the setAngVel 0x6746C6 and the velocity copy 0x6746D3 take it),
// its velocity after the step (m/s, 0x6746E6 copies getLinVel there), its drive block (the input's r8: the CarBase
// pre-update zeroes it, 0x673AAC, then hands it to slot 55: [0] float pad+0xE4, [4] float pad+0xCC, [8] float -LX
// steering, +0xC flags; BikeBase's 0x658D58 fills it from a pad).
constexpr std::size_t kChassisBody=0x1698,kChassisVel=0x1BC0,kDriveBlock=0x1BE0;
const unsigned char kBlockSig[]={0x4C,0x8D,0xAF,0xE0,0x1B,0x00,0x00};                       // 0x673AAC lea r13,[rdi+0x1BE0]
const unsigned char kVelSig[]={0x48,0x8B,0x8F,0x98,0x16,0x00,0x00,0xE8};                     // 0x6746D3 mov rcx,[rdi+0x1698]; call
const unsigned char kBikePadSig[]={0x8B,0x80,0xE4,0x00,0x00,0x00,0x33,0xD2,0x41,0x89,0x00};  // 0x658D6D [r8] = pad+0xE4
// The human (on foot): the move stick +0xD50 (vec4: -LX, 0, -LY, 1: 0x5706D7), the board button +0xD78 (pad
// +0x479: 0x57082B; the soldier's update boards on it at 0x59B417, after the pre-update), the character controller
// +0x680 (warped by 0x11B9870 with an hkTransform, as the ride exit does: 0x57B17C..0x57B187), its carried velocity
// +0x6B0 (m/s; docs/player-jet-re.md §7), attached / ragdolled +0x39C, a remote copy +0x128 bit 0.
constexpr std::size_t kHumanMove=0xD50,kHumanBoard=0xD78,kHumanCtrl=0x680,kHumanCarry=0x6B0,kHumanAttach=0x39C,kNetFlags=0x128;
constexpr std::size_t kHumanState=0x5D0;
constexpr unsigned kControllerPosition=0x11B8DD0;
const unsigned char kPositionSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x33,0xC0,0xC7,0x42,0x0C,0x00,0x00,0x80,0x3F};
constexpr unsigned kMoveIntent=0x56D350,kMoveIntentCall=0x573B72,kWarp=0x11B9870;
// The walk controller's one-step velocity (docs/sidecar-re.md §3a): 0x11B8D90(ctrl, v) adds v (xyz, m/s) to
// ctrl+0x60; the step 0x11B9890 adds ctrl+0x60 to this step's velocity (0x11B9A92) and clears it (0x11B9CB7). The
// stock pre-update moves a human to a target by it as (target - position) x kStepRate (0x573E10, the 60.0s at
// 0x1768E20); the step's gravity is per 1/60 s too (0x11B98EA).
constexpr unsigned kAddStep=0x11B8D90;
constexpr float kStepRate=60.0f;
const unsigned char kAddStepSig[]={0x48,0x83,0xEC,0x18,0x0F,0x10,0x49,0x60,0x0F,0x58,0x0A};   // 0x11B8D90
const unsigned char kStepUseSig[]={0x0F,0x58,0x73,0x60};     // 0x11B9A92 addps xmm6,[rbx+0x60]
const unsigned char kStepClearSig[]={0x4C,0x89,0x73,0x60};   // 0x11B9CB7 mov [rbx+0x60],r14
const unsigned char kMoveCallSig[]={0x48,0x8B,0xCE,0xE8,0xD9,0x97,0xFF,0xFF,0x48,0x8D,0x8E,0x60,0x0D,0x00,0x00};   // 0x573B6F
const unsigned char kWarpSig[]={0x48,0x8B,0x09,0xE9,0xF8,0x6B,0x02,0x00};                                       // 0x11B9870
const unsigned char kExitWarpSig[]={0x48,0x8D,0x55,0xE7,0x48,0x8D,0x8F,0x80,0x06,0x00,0x00,0xE8};             // 0x57B17C
// The team walk the board prompt makes (0x573624): 0x5E11D0(manager, team, functor) calls functor slot 1 (functor,
// object) for every object of every team friendly to `team` (relation 1, the team itself among them).
constexpr unsigned kTeamWalk=0x5E11D0,kTeamManager=0x20B2978;
const unsigned char kTeamWalkSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
// The soldier classes a squadmate is (the player's four: AssultSoldier, PaleWing, HeavyArmor, Engineer).
constexpr unsigned kSoldierVts[]={0x17CDF28,0x17D0FF8,0x17CF5B8,0x17CF100};
constexpr float kHold=0.25f;            // m a gunner may drift off the point before they are put back
constexpr float kLeave=3.0f;            // m off the point: they are gone (thrown off): let go
constexpr float kJumpOut=0.25f;         // m over the point, rising kJumpRise a frame or more: the player jumped off
constexpr float kJumpRise=0.035f;
constexpr ULONGLONG kNpcScanMs=500,kStaleMs=2000,kLogMs=3000;
constexpr ULONGLONG kGoneMs=500;        // a bike whose input has not run this long holds no one
constexpr float kPi=3.14159265f;
// The level (SidecarLevel): the roll rate set is -kLevelGain x the roll (1/s), at most kLevelRate rad/s.
constexpr float kLevelGain=6.0f,kLevelRate=3.0f;
// The plugin's driving (the player in the sidecar, nobody on the saddle): the stick pushed past kStickOn sends the
// bike the way the camera looks; steering kSteerGain per rad of heading off, full throttle under kSlowTurn off.
constexpr float kStickOn=0.3f,kSteerGain=1.5f,kSlowTurn=1.75f,kSlowThrottle=0.35f;
constexpr int kSteerVotes=12;           // turns seen against the steering before its sign is flipped
constexpr ULONGLONG kStallMs=2500;      // full throttle this long and under kStallSpeed: the other pedal is tried
constexpr float kStallSpeed=0.5f;
constexpr int kMaxSidecars=16;

struct Sidecar {
    ObjRef ref;
    ULONGLONG seen,npcScanAt,loggedAt,throttleAt,frame;
    bool marked;                  // has the marker bone (else a stock Freed bike: left alone)
    ObjRef gunner;                // who stands in the sidecar (none: empty)
    bool gunnerPlayer;
    float lastHeight;             // height above the moving tub, not world height
    bool npcReleased;             // ejection ends recruitment until the driver starts a new ride
    const void* body;             // its chassis body this frame (SidecarLevel)
    // The plugin's driving: the order (the player's stick, its frame), the steering's sign, the throttle's field.
    float order;ULONGLONG orderFrame;
    int steerSign,votes,pedal;    // pedal: the drive block's float the throttle goes in (0 or 1: [0] or [4])
    float prevHeading,lastSteer;bool prevValid,driving,stalled;
};
Sidecar sidecars[kMaxSidecars]{};
// The board button that took the player into the sidecar or out of it, until they let go of it: while held it is taken
// off them (a button held over the next frames would take them straight off again, or onto the saddle).
ObjRef boardHeld[kMaxSidecars]{};  // each local player consumes their own press (split screen)
bool ok=false,moveOk=false,driveOk=false;
using MoveIntentFn=std::uintptr_t(__fastcall*)(void*);
using WarpFn=void(__fastcall*)(void*,const float*);
using AddStepFn=void(__fastcall*)(void*,const float*);
using WalkFn=void(__fastcall*)(void*,std::int32_t,void*);
using PositionFn=float*(__fastcall*)(void*,float*);

// Published on the game thread; bullet batches may run elsewhere. Never read sidecars from a bullet hook.
struct PassengerPair { ObjRef vehicle,gunner,driver; ULONGLONG at; };
PassengerPair passengers[kMaxSidecars]{};
SRWLOCK passengerLock=SRWLOCK_INIT;

bool BoardHeld(const void* human) noexcept {
    for(const auto& held:boardHeld)if(held.Is(human))return true;
    return false;
}
void HoldBoard(const void* human) noexcept {
    for(auto& held:boardHeld)if(!held || held.obj==human || !held.Is(held.obj)){held=ObjRef::Of(human);return;}
}
void ReleaseBoard(const void* human) noexcept {
    for(auto& held:boardHeld)if(held.obj==human)held=ObjRef{};
}

// +0x90 is refreshed at 0x573D93, AFTER MoveIntent's 0x573B72. Reading it there feeds last frame's
// rendered pose back into this frame's correction. This is the native foot/body transform, also used by
// ride exit (0x57B12F..0x57B187); the game's capsule already contains each class's size/shape offset.
void FootPosition(unsigned char* human,float* out) noexcept {
    alignas(16) float p[4];
    reinterpret_cast<PositionFn>(image+kControllerPosition)(human+kHumanCtrl,p);
    std::memcpy(out,p,12);
}

float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
float Wrap(float a) noexcept { while(a>kPi)a-=2*kPi; while(a<-kPi)a+=2*kPi; return a; }
const float* Row(const unsigned char* o,int r) noexcept { return reinterpret_cast<const float*>(o+kMatrix+r*16); }
const float* Pos(const unsigned char* o) noexcept { return reinterpret_cast<const float*>(o+kPosition); }

// A point of the bike's frame (x the bike's left, y up, z forward) in the world.
void FramePoint(const unsigned char* v,float x,float y,float z,float* out) noexcept {
    const float* p=Pos(v);
    for(int c=0;c<3;++c)out[c]=p[c]+x*Row(v,0)[c]+y*Row(v,1)[c]+z*Row(v,2)[c];
}

bool InVehicle(const unsigned char* human) noexcept {
    const auto ctrl=At<const unsigned char*>(human,kHumanVehicleCtrl);
    return ctrl && At<std::int32_t>(ctrl,8)!=0;
}

// A human the sidecar can hold: alive, on foot, not attached or ragdolled, run on this machine.
bool Holdable(const unsigned char* h) noexcept {
    return h && !h[kDead] && At<std::int32_t>(h,kHumanAttach)==0 && !(At<unsigned>(h,kHumanState)&4) &&
        !InVehicle(h) && !(At<std::uint8_t>(h,kNetFlags)&1);
}

void PublishPassenger(const Sidecar& s) noexcept {
    PassengerPair pair{};
    if(s.gunner) {
        pair.vehicle=s.ref;pair.gunner=s.gunner;pair.at=GetTickCount64();
        auto v=const_cast<unsigned char*>(static_cast<const unsigned char*>(s.ref.obj));
        if(SeatCount(v) && SeatRider(SeatAt(v,0))!=Rider::none)
            pair.driver=ObjRef::Of(At<const void*>(SeatAt(v,0),kSeatRider));
    }
    AcquireSRWLockExclusive(&passengerLock);
    passengers[&s-sidecars]=pair;
    ReleaseSRWLockExclusive(&passengerLock);
}

Sidecar* Find(const void* v) noexcept {
    for(auto& s:sidecars)if(s.ref.Is(v))return &s;
    return nullptr;
}

// v's entry (a new one for a 503 not seen yet, its marker looked up once); nullptr: no slot free (logged).
Sidecar* EntryFor(unsigned char* v,ULONGLONG ms) noexcept {
    if(Sidecar* s=Find(v))return s;
    // Its model's bones not built yet (a bike just made): asked again next frame, not taken for a stock bike.
    if(!At<const void*>(v+kModelInst506,kInstBones506) || At<std::int32_t>(v+kModelInst506,kInstBoneCount)<=0)return nullptr;
    Sidecar* slot=nullptr;
    for(auto& s:sidecars)if(!s.ref || s.ref.obj==v || ms-s.seen>kStaleMs){slot=&s;break;}
    if(!slot){static ULONGLONG at=0;if(ms-at>10000){at=ms;Log("SIDECAR table full: v=%p left alone",v);}return nullptr;}
    *slot=Sidecar{};
    PublishPassenger(*slot);
    slot->ref=ObjRef::Of(v);slot->seen=ms;slot->steerSign=1;
    slot->marked=BoneRecord506(v+kModelInst506,kMarkerBone)!=nullptr;
    if(slot->marked)Log("SIDECAR v=%p a sidecar motorcycle",v);
    return slot;
}

// The game's warp of a human's character controller to `pos` (an hkTransform: identity turn, `pos`), as the ride
// state's exit puts a rider down (0x57B16F..0x57B187: 0x97D9D0 copies the 4x4 it builds the same way).
void Warp(unsigned char* human,const float* pos) noexcept {
    alignas(16) float t[16]={1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, pos[0],pos[1],pos[2],1.0f};
    reinterpret_cast<WarpFn>(image+kWarp)(human+kHumanCtrl,t);
}

const char* Who(const Sidecar& s) noexcept { return s.gunnerPlayer ? "the player" : "an NPC"; }

void Let(Sidecar& s,const unsigned char* v,const char* why) noexcept {
    Log("SIDECAR v=%p %s off the sidecar: %s",v,Who(s),why);
    if(!s.gunnerPlayer)s.npcReleased=true;
    s.gunner=ObjRef{};s.gunnerPlayer=false;s.driving=false;
    s.order=0.0f;s.orderFrame=0;
    PublishPassenger(s);
}

void Take(Sidecar& s,unsigned char* v,unsigned char* human,bool byPlayer) noexcept {
    float at[3];FramePoint(v,kGunnerX,kGunnerY,kGunnerZ,at);
    Warp(human,at);
    s.gunner=ObjRef::Of(human);s.gunnerPlayer=byPlayer;s.lastHeight=0.0f;
    if(byPlayer)HoldBoard(human);   // in by the board button: it gets them out only once let go
    for(auto& o:sidecars)if(&o!=&s && o.gunner.Is(human))Let(o,static_cast<const unsigned char*>(o.ref.obj),"transferred");
    PublishPassenger(s);
    Log("SIDECAR v=%p %s %p into the sidecar",v,byPlayer ? "the player" : "an NPC",human);
}

// Out of the tub, beside it on the ground: kStepOff past its outer side, level with the gunner's point.
void StepOff(Sidecar& s,unsigned char* v,unsigned char* human,const char* why) noexcept {
    float at[3];FramePoint(v,kTubOut-kStepOff,0.2f,kGunnerZ,at);
    Warp(human,at);
    HoldBoard(human);
    Let(s,v,why);
}

bool IsSoldier(const unsigned char* o) noexcept {
    const auto vt=At<const unsigned char*>(o,0);
    for(unsigned x:kSoldierVts)if(vt==image+x)return true;
    return false;
}

// The team walk's functor (0x5E11D0 calls slot 1 with each object): the nearest holdable NPC soldier to `at`.
struct Pick { void** vtable; const float* at; float best; unsigned char* found; };
void __fastcall PickVisit(void* self,void* object) noexcept {
    __try {
        auto& p=*static_cast<Pick*>(self);
        auto o=static_cast<unsigned char*>(object);
        if(!o || !IsSoldier(o) || IsPlayer(o) || !Holdable(o))return;
        for(const auto& s:sidecars)if(s.gunner.Is(o))return;
        const float* q=Pos(o);
        const float d[3]={q[0]-p.at[0],q[1]-p.at[1],q[2]-p.at[2]};
        const float dd=Dot(d,d);
        if(dd<p.best){p.best=dd;p.found=o;}
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
void __fastcall PickDtor(void*,unsigned) noexcept {}
void* kPickVtable[]={reinterpret_cast<void*>(&PickDtor),reinterpret_cast<void*>(&PickVisit)};

unsigned char* NearestSquadmate(const unsigned char* v,std::int32_t team) noexcept {
    const auto manager=At<void*>(image,kTeamManager);
    if(!manager)return nullptr;
    Pick p{kPickVtable,Pos(v),Cfg().sidecarNpcRange*Cfg().sidecarNpcRange,nullptr};
    reinterpret_cast<WalkFn>(image+kTeamWalk)(manager,team,&p);
    return p.found;
}

// The held gunner this frame: let go of one that is gone, has jumped off or been thrown; put the others back.
void Hold(Sidecar& s,unsigned char* v) noexcept {
    auto h=const_cast<unsigned char*>(static_cast<const unsigned char*>(s.gunner.obj));
    if(!s.gunner.Is(h)){Let(s,v,"gone");return;}
    if(!Holdable(h)){Let(s,v,"no longer able to ride on foot");return;}
    if(s.gunnerPlayer && !IsPlayer(h)){Let(s,v,"no longer the player");return;}
    float at[3];FramePoint(v,kGunnerX,kGunnerY,kGunnerZ,at);
    float p[3];FootPosition(h,p);
    const float d[3]={p[0]-at[0],p[1]-at[1],p[2]-at[2]};
    const float off=std::sqrt(Dot(d,d));
    const float height=Dot(d,Row(v,1));
    const bool rising=height-s.lastHeight>kJumpRise;
    s.lastHeight=height;
    if(s.gunnerPlayer && height>kJumpOut && rising) {
        // In the air the carried velocity keeps its x and z (docs/player-jet-re.md §7): off with the bike's way on.
        float* carry=reinterpret_cast<float*>(h+kHumanCarry);
        const float* vel=reinterpret_cast<const float*>(v+kChassisVel);
        carry[0]=vel[0];carry[2]=vel[2];
        Let(s,v,"jumped off");return;
    }
    if(off>kLeave){Let(s,v,"thrown off");return;}
    // Riding along is Follow's (the walk's step velocity); the warp only catches a gunner that still drifted off.
    if(off>kHold)Warp(h,at);
}

// The step velocity that keeps a held gunner on the tub floor's point through this physics step: the bike's velocity,
// and toward the point at the stock rate (the pre-update's own (target - position) x 60, 0x573E10) across the
// ground only. Height is the bike's velocity alone: the tub's floor holds them up, gravity brings them down to
// it, and a pull down would eat a jump (the player's way off).
void FollowVelocity(const float* target,const float* pos,const float* bikeVel,float* out) noexcept {
    out[0]=bikeVel[0]+(target[0]-pos[0])*kStepRate;
    out[1]=bikeVel[1];
    out[2]=bikeVel[2]+(target[2]-pos[2])*kStepRate;
    out[3]=0.0f;
}

// From MoveIntent (the human's pre-update, before its update's controller step consumes the step velocity).
void Follow(const Sidecar& s,unsigned char* h) noexcept {
    const auto v=static_cast<const unsigned char*>(s.ref.obj);
    if(!s.ref.Is(v))return;
    float at[3];FramePoint(v,kGunnerX,kGunnerY,kGunnerZ,at);
    float p[3];FootPosition(h,p);
    alignas(16) float step[4];
    FollowVelocity(at,p,reinterpret_cast<const float*>(v+kChassisVel),step);
    reinterpret_cast<AddStepFn>(image+kAddStep)(h+kHumanCtrl,step);
}

// The player in the sidecar, nobody on the saddle: the bike driven the way the camera looks while the stick is
// pushed (`order`, from MoveIntent this frame), else let roll. The drive block's steering is [8]; the throttle the
// float `pedal` picks ([0] first, the other after a stall). The steering's sign is learned from the heading's turn.
void Drive(Sidecar& s,unsigned char* v) noexcept {
    float* block=reinterpret_cast<float*>(v+kDriveBlock);
    const float* f=Row(v,2);
    const float heading=std::atan2(f[0],f[2]);
    const float* vel=reinterpret_cast<const float*>(v+kChassisVel);
    const float speed=Dot(vel,f);
    if(s.prevValid && std::fabs(s.lastSteer)>0.3f && std::fabs(speed)>2.0f) {
        const float turned=Wrap(heading-s.prevHeading);
        if(std::fabs(turned)>0.002f) {
            s.votes+=(turned*s.lastSteer>0.0f) ? -1 : 1;   // the way it was steered: a vote for the sign as it is
            if(s.votes>=kSteerVotes){s.steerSign=-s.steerSign;s.votes=0;Log("SIDECAR v=%p steering sign flipped to %d",v,s.steerSign);}
            if(s.votes<-kSteerVotes)s.votes=-kSteerVotes;
        }
    }
    s.prevHeading=heading;s.prevValid=true;
    float eye[3],look[3];
    const bool ordered=s.orderFrame+1>=GameFrame() && s.order>kStickOn && CameraRay(eye,look) && look[0]*look[0]+look[2]*look[2]>1e-4f;
    if(!ordered){s.lastSteer=0.0f;s.throttleAt=0;if(s.driving){s.driving=false;Log("SIDECAR v=%p the plugin's driver stops",v);}return;}
    if(!s.driving){s.driving=true;Log("SIDECAR v=%p the plugin drives for the player in the sidecar",v);}
    const float off=Wrap(std::atan2(look[0],look[2])-heading);
    const float steer=Clamp(kSteerGain*off,-1.0f,1.0f);
    const float throttle=(std::fabs(off)>kSlowTurn ? kSlowThrottle : 1.0f)*Clamp(s.order,0.0f,1.0f);
    block[s.pedal]=throttle;
    block[2]=steer*static_cast<float>(s.steerSign);
    s.lastSteer=steer;
    const ULONGLONG ms=GameMs();
    if(throttle<0.9f || std::fabs(speed)>kStallSpeed){s.throttleAt=0;s.stalled=false;return;}
    if(!s.throttleAt)s.throttleAt=ms;
    if(ms-s.throttleAt>kStallMs && !s.stalled) {
        s.stalled=true;s.throttleAt=0;s.pedal=1-s.pedal;
        Log("SIDECAR v=%p no way on at full throttle: the throttle tried in the drive block's float %d",v,s.pedal);
    }
}

void LogState(Sidecar& s,const unsigned char* v,ULONGLONG ms) noexcept {
    if(!Cfg().debug || ms-s.loggedAt<kLogMs)return;
    s.loggedAt=ms;
    const float* f=Row(v,2);const float* u=Row(v,1);
    const float* vel=reinterpret_cast<const float*>(v+kChassisVel);
    float at[3];FramePoint(v,kGunnerX,kGunnerY,kGunnerZ,at);
    float gap=-1.0f;
    if(s.gunner)__try {
        const float* p=Pos(static_cast<const unsigned char*>(s.gunner.obj));
        const float d[3]={p[0]-at[0],p[1]-at[1],p[2]-at[2]};gap=std::sqrt(Dot(d,d));
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    Log("SIDECAR v=%p gunner %s (%.2f m off the point) speed %.1f m/s up (%.2f %.2f %.2f) fwd.y %.2f%s",v,
        s.gunner ? Who(s) : "none",gap,Dot(vel,f),u[0],u[1],u[2],f[1],s.driving ? " plugin-driven" : "");
}

// Both explosion variants preserve DamageInfo's attacker weak reference (+0x10/+0x18), copied by
// 0x114210 from the bullet's core+0x730. Filter the two per-target damage calls, never the blast itself:
// enemies, other allies, the shooter's own damage, radius, knockback and visual effects remain native.
constexpr unsigned kDamage=0x541FF0,kBlastDamageCall=0x542FD4,kBlastListDamageCall=0x54360E;
const unsigned char kBlastDamageSig[]={0x4C,0x8D,0x45,0x50,0x48,0x8D,0x54,0x24,0x58,0x48,0x8D,0x4D,0x90,0xE8,0x17,0xF0,0xFF,0xFF};
const unsigned char kBlastListDamageSig[]={0x4C,0x8D,0x45,0x00,0x48,0x8D,0x54,0x24,0x30,0x48,0x8D,0x4C,0x24,0x70,0xE8,0xDD,0xE9,0xFF,0xFF};
const unsigned char kAttackerCopySig[]={0x48,0x8B,0x42,0x10,0x48,0x89,0x41,0x10,0x48,0x8B,0x42,0x18,0x48,0x89,0x41,0x18};
using DamageFn=void(__fastcall*)(void*,const void*,void*);
void __fastcall PassengerBlastDamage(void* damage,const void* target,void* info) noexcept {
    bool pass=false;
    __try {
        const auto targetCtrl=At<const unsigned char*>(target,8);
        const void* object=At<const void*>(target,0);
        if(targetCtrl && At<int>(targetCtrl,8)>0 && object && At<const void*>(object,kSelfCtrl)==targetCtrl)
            pass=SidecarBulletPass(At<const void*>(info,0x10),object,At<const void*>(info,0x18));
    } __except(EXCEPTION_EXECUTE_HANDLER) { pass=false; }
    if(!pass)reinterpret_cast<DamageFn>(image+kDamage)(damage,target,info);
}
}  // namespace

bool SidecarBulletPass(const void* owner,const void* target,const void* ownerCtrl) noexcept {
    if(!owner || !target || !ownerCtrl || !ok || !Cfg().sidecar)return false;
    PassengerPair match{};
    AcquireSRWLockShared(&passengerLock);
    for(const auto& p:passengers)if(p.gunner.obj==owner && p.gunner.ctrl==ownerCtrl &&
        (p.vehicle.obj==target || p.driver.obj==target)) {match=p;break;}
    ReleaseSRWLockShared(&passengerLock);
    if(!match.gunner || GetTickCount64()-match.at>kGoneMs)return false;
    __try {
        auto v=const_cast<unsigned char*>(static_cast<const unsigned char*>(match.vehicle.obj));
        const auto h=static_cast<const unsigned char*>(owner);
        if(At<int>(ownerCtrl,8)<=0 || !match.vehicle.ctrl || At<int>(match.vehicle.ctrl,8)<=0 ||
           !match.gunner.Is(owner) || !match.vehicle.Is(v) || v[kDead] || !Holdable(h))return false;
        if(target==v)return true;
        // A saved driver who has since stepped off is no longer protected.
        return match.driver.Is(target) && SeatCount(v) && SeatRider(SeatAt(v,0))!=Rider::none &&
            At<const void*>(SeatAt(v,0),kSeatRider)==target;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool IsSidecar(const void* vehicle) noexcept {
    const Sidecar* s=Find(vehicle);
    return s && s->marked;
}

bool SidecarHoldsPlayer(const void* vehicle) noexcept {
    const Sidecar* s=Find(vehicle);
    return s && s->marked && s->gunner && s->gunnerPlayer;
}

void SidecarFrame(unsigned char* v) noexcept {
    if(!ok || At<const unsigned char*>(v,0)!=image+kVt503)return;
    const ULONGLONG ms=GameMs();
    // Off (the ini) or wrecked: whoever it held goes free (else MoveIntent would keep their walk away).
    if(!Cfg().sidecar || v[kDead]) {
        if(Sidecar* s=Find(v); s && s->gunner)Let(*s,v,v[kDead] ? "the bike was destroyed" : "Sidecar=0");
        return;
    }
    Sidecar* sp=EntryFor(v,ms);
    if(!sp || !sp->marked)return;
    Sidecar& s=*sp;
    s.seen=ms;s.body=At<const void*>(v,kChassisBody);
    if(s.frame==GameFrame())return;
    s.frame=GameFrame();
    if(s.gunner)Hold(s,v);
    const Rider driver=SeatCount(v) ? SeatRider(SeatAt(v,0)) : Rider::none;
    // An NPC rides along while the player drives; the player off the saddle lets them go.
    if(s.gunner && !s.gunnerPlayer && driver!=Rider::player)Let(s,v,"the player left the saddle");
    if(driver!=Rider::player)s.npcReleased=false;
    if(!s.gunner && !s.npcReleased && driver==Rider::player && Cfg().sidecarNpcGunner && ms-s.npcScanAt>kNpcScanMs) {
        s.npcScanAt=ms;
        // The player's friends (their team's relation row), the team the player drives on (crew.cpp SeePlayer).
        const std::int32_t team=player.at && ms-player.at<2000 ? player.team : At<std::int32_t>(v,kTeam);
        if(auto npc=NearestSquadmate(v,team))Take(s,v,npc,false);
    }
    // The player in the sidecar: the plugin is their driver. A stock NPC driver left on the saddle (crewed while the
    // bike stood empty) would drive after the player, who sits on the bike itself: it gets off (the stock kick, as
    // crew.cpp's bump does; never from inside the team walk FindSeat runs in: here, from the vehicle's own input).
    if(s.gunner && s.gunnerPlayer && driver==Rider::dummy && driveOk) {
        reinterpret_cast<void(__fastcall*)(void*,void*)>(image+kSeatKick)(v,SeatAt(v,0));
        Log("SIDECAR v=%p its NPC driver got off: the plugin drives for the player in the sidecar",v);
    } else if(s.gunner && s.gunnerPlayer && driver==Rider::none && driveOk)Drive(s,v);
    else if(s.driving){s.driving=false;s.lastSteer=0.0f;}
    PublishPassenger(s);
    LogState(s,v,ms);
}

bool SidecarBoard(unsigned char* v,unsigned char* human) noexcept {
    if(!ok || !Cfg().sidecar || !IsPlayer(human))return false;
    // The boarding gun temporarily warps to a native door and confirms success through the native seated state.
    // A virtual sidecar take cannot satisfy that contract: PressOn would restore the human's remote position.
    if(BoardingOnly())return false;
    // FindSeat's caller walks every friendly vehicle, even after we return nullptr (there is no real seat).
    // Consume the whole press, including the rest of that walk and a step-off's press, before asking any vehicle.
    if(BoardHeld(human))return true;
    for(const auto& held:sidecars)if(held.gunnerPlayer && held.gunner.Is(human))return true;
    Sidecar* s=Find(v);
    if(!s || !s->marked || s->gunner || v[kDead] || !Holdable(human))return false;
    float gun[3];FramePoint(v,kGunnerX,0.0f,kGunnerZ,gun);
    const float* p=Pos(human);
    const float dg[3]={p[0]-gun[0],p[1]-gun[1],p[2]-gun[2]};
    float door[3],reach=0.0f;
    // The sidecar has its own door at the tub's foot, with the bike's stock boarding radius (already including
    // CanRideSeat's 0.5 m slack). The team walk does no distance check for us. Vertical separation counts too.
    if(!SeatPoint(v,0,door,&reach) || !(Dot(dg,dg)<=reach*reach))return false;
    const Rider driver=SeatCount(v) ? SeatRider(SeatAt(v,0)) : Rider::other;
    bool saddleNearer=false;
    if(driver==Rider::none || driver==Rider::dummy) {
        const float dd[3]={p[0]-door[0],p[1]-door[1],p[2]-door[2]};
        saddleNearer=Dot(dd,dd)<Dot(dg,dg);
    }
    if(saddleNearer)return false;   // the stock board (or crew.cpp's bump of an NPC driver) takes the saddle
    Take(*s,v,human,true);
    return true;
}

std::uintptr_t __fastcall MoveIntent(void* human) noexcept {
    __try {
        auto h=static_cast<unsigned char*>(human);
        if(BoardHeld(h)) {
            if(h[kHumanBoard])h[kHumanBoard]=0;   // still the press that took them in or out
            else ReleaseBoard(h);
        }
        for(auto& s:sidecars) {
            if(!s.gunner.Is(h))continue;
            // The bike no longer runs its input (gone: deleted, a new mission): the gunner is free to walk again.
            if(GameMs()-s.seen>kGoneMs){Let(s,static_cast<const unsigned char*>(s.ref.obj),"the bike is gone");break;}
            auto v=const_cast<unsigned char*>(static_cast<const unsigned char*>(s.ref.obj));
            // A knockdown may start between the vehicle's input and this pre-update. End the binding BEFORE
            // suppressing movement or adding any velocity; recovery must require a fresh boarding press.
            if(!Cfg().sidecar || !s.ref.Is(v) || v[kDead] || !Holdable(h)) {
                Let(s,v,"passenger or vehicle became unavailable");break;
            }
            Hold(s,v);
            if(!s.gunner)break;
            float* move=reinterpret_cast<float*>(h+kHumanMove);
            if(s.gunnerPlayer) {
                s.order=std::sqrt(move[0]*move[0]+move[2]*move[2]);s.orderFrame=GameFrame();
                if(h[kHumanBoard]) {
                    h[kHumanBoard]=0;   // the button gets them off the sidecar, not onto the saddle (0x59B417)
                    if(s.ref.Is(v))StepOff(s,v,h,"the board button");
                    else Let(s,v,"the bike is gone");
                }
            }
            if(s.gunner.Is(h)) {
                move[0]=move[1]=move[2]=0.0f;
                Follow(s,h);
            }
            break;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return reinterpret_cast<MoveIntentFn>(image+kMoveIntent)(human);
}

void SidecarLevel(const void* body,float* w) noexcept {
    if(!ok || !Cfg().sidecar || !body || !w)return;
    for(const auto& s:sidecars) {
        if(s.body!=body || !s.marked)continue;
        __try {
            const auto v=static_cast<const unsigned char*>(s.ref.obj);
            if(!s.ref.Is(v))return;
            const float* f=Row(v,2);const float* u=Row(v,1);
            // Level: the world's up with the forward part taken off; the roll is the turn about forward from it to up.
            float lv[3]={-f[1]*f[0],1.0f-f[1]*f[1],-f[1]*f[2]};
            const float n=std::sqrt(Dot(lv,lv));
            if(n<0.2f)return;   // nose straight up or down: no roll to speak of
            for(float& c:lv)c/=n;
            const float x[3]={lv[1]*u[2]-lv[2]*u[1],lv[2]*u[0]-lv[0]*u[2],lv[0]*u[1]-lv[1]*u[0]};
            const float roll=std::atan2(Dot(x,f),Dot(lv,u));
            const float want=Clamp(-kLevelGain*roll,-kLevelRate,kLevelRate);
            const float now=Dot(w,f);
            for(int c=0;c<3;++c)w[c]+=(want-now)*f[c];
        } __except(EXCEPTION_EXECUTE_HANDLER){}
        return;
    }
}

bool InstallSidecar() noexcept {
    __try {
        ok=Matches(kTeamWalk,kTeamWalkSig,sizeof(kTeamWalkSig)) && Matches(kWarp,kWarpSig,sizeof(kWarpSig)) &&
           Matches(kControllerPosition,kPositionSig,sizeof(kPositionSig)) &&
           Matches(0x57B17C,kExitWarpSig,sizeof(kExitWarpSig)) && Matches(0x6746D3,kVelSig,sizeof(kVelSig)) &&
           Matches(kAddStep,kAddStepSig,sizeof(kAddStepSig)) && Matches(0x11B9A92,kStepUseSig,sizeof(kStepUseSig)) &&
           Matches(0x11B9CB7,kStepClearSig,sizeof(kStepClearSig));
        driveOk=Matches(0x673AAC,kBlockSig,sizeof(kBlockSig)) && Matches(0x658D6D,kBikePadSig,sizeof(kBikePadSig));
        moveOk=false;
        if(ok && Matches(kMoveIntentCall-3,kMoveCallSig,sizeof(kMoveCallSig))) {
            bool changed=false;
            moveOk=edf::RedirectCall(image+kMoveIntentCall,image+kMoveIntent,reinterpret_cast<void*>(&MoveIntent),changed);
        }
        bool blastOk=false;
        if(ok && Matches(0x542FC7,kBlastDamageSig,sizeof(kBlastDamageSig)) &&
           Matches(0x543600,kBlastListDamageSig,sizeof(kBlastListDamageSig)) &&
           Matches(0x114251,kAttackerCopySig,sizeof(kAttackerCopySig))) {
            bool changed=false;
            blastOk=edf::RedirectCall(image+kBlastDamageCall,image+kDamage,reinterpret_cast<void*>(&PassengerBlastDamage),changed);
            if(blastOk)blastOk=edf::RedirectCall(image+kBlastListDamageCall,image+kDamage,reinterpret_cast<void*>(&PassengerBlastDamage),changed);
        }
        // Without the walk taken away a held gunner walks out of the tub every frame: no sidecar at all. A passenger's
        // rounds (the bullets' hook, jet_hooks.cpp InstallBulletPass) and blasts (above) left out of their own bike are
        // each a channel of its own: one missing, that one hits the bike and its driver as any friendly fire does.
        ok=ok && moveOk;
        const bool shotsOk=ok && SidecarBulletHooked();
        if(ok && !shotsOk)Log("SIDECAR no bullet pass-through: a passenger's rounds hit their own bike and driver (stock)");
        if(ok && !blastOk)Log("SIDECAR no blast filter: a passenger's explosions hurt their own bike and driver (stock)");
        Log("HOOK sidecar own-vehicle pass: rounds=%d blasts=%d",shotsOk,blastOk);
    } __except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
    Log("HOOK sidecar=%d (move=%d drive=%d level=%d) config %d",ok,moveOk,driveOk,ok && SidecarLevelHooked(),Cfg().sidecar);
    return ok;
}

void ResetSidecars() noexcept {
    for(auto& s:sidecars)s=Sidecar{};
    for(auto& held:boardHeld)held=ObjRef{};
    AcquireSRWLockExclusive(&passengerLock);
    for(auto& p:passengers)p=PassengerPair{};
    ReleaseSRWLockExclusive(&passengerLock);
}
}  // namespace crew
