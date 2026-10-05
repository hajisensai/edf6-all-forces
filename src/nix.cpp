// The Nix's torso twist (README 尼克斯; docs/nix-re.md; ini NixTorsoTwist): MechWarrior controls for the Air Raider's
// Nix combat frame (Vehicle612_nix, V612_NIX*.SGO). The stock Nix already drives its legs like a tank (W/S walk,
// A/D turn the legs: BodyInputController 0x6472C0 -> turn rate veh+0x1974 -> heading veh+0x19D4) and turns its torso
// with the mouse / right stick (the seat's aim, VehicleWeaponAim at seat+0xE0: a yaw axis and a pitch axis), but the
// torso's yaw is an angle off the legs: turning the legs swings the torso, the arms' aim and the camera with them.
// This keeps the torso's yaw in the world instead: each frame the yaw axis gives back what the legs turned since the
// last one, so only the mouse turns the torso; at the axis' stops (the stock twist limit) the torso goes round with
// the legs (the legs are never pushed by the mouse).
//  - When. Slot 4 (0x644350, the Begaruta family's update) builds the vehicle's matrix from the heading (0x645790 ->
//    0x645530 -> 0x4CD10), then steps every seat's aim by its rider's input (0x6459C7: aim slot 2, 0x5FBDA0, clamps
//    the angle at the stops) and only then steps the heading by this frame's turn (0x645190). The frame is drawn with
//    the matrix of the heading the update began with. So before the update the yaw axis is moved back by the turn
//    since the update before (the heading now less the heading then): the stock step adds the mouse and clamps, and
//    the torso is drawn at (that heading + twist), the same world yaw as last frame plus the mouse.
//  - Who. Only a Nix the local player drives (a pad on this machine in seat 0): an NPC's aims through the same axis
//    with its own closed loop (0x63AC40), and another machine's is that machine's. Not while the aim chases a
//    target angle (+0xC0 set with mode +0xC8 1: the online path, which overwrites the angle).
//  - Off (NixTorsoTwist=0, the plugin off): nothing is written; the torso stays where it is relative to the legs.
// PlayerNixTorso publishes the frame's legs and torso (nix.h) for the vehicle camera and the HUD's twist indicator.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include "nix_twist.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr unsigned kVtNix=0x17DD440,kUpdate=0x644350;
constexpr std::size_t kSlotUpdate=4;
// The legs' heading: the character controller at veh+0x1720, its +0x2B4 (0x645559 reads it for the matrix; 0x645190
// steps it by the turn rate at +0x254 = veh+0x1974, rad/s, times 1/60).
constexpr std::size_t kHeading=0x1720+0x2B4;
// The seat's aim (common/edf/layout.h kSeatAim): VehicleWeaponAim or its sound-playing kind (both step the axes with
// 0x5FBDA0); +0xC0 set: the target mode +0xC8 may apply; mode 1 chases target angles (0x5FBDFC).
constexpr unsigned kAimVt=0x17D8A68,kAimSeVt=0x17D8A90;
constexpr std::size_t kAimTargetOn=0xC0,kAimMode=0xC8;
constexpr std::int32_t kModeTarget=1;
constexpr ULONGLONG kResyncMs=250;    // game ms: an update this long ago is not the last frame's (a new ride, a pause)
constexpr ULONGLONG kLogMs=1000,kCueMs=200;
constexpr float kRad=57.2957795f;

struct Sig { unsigned rva; unsigned char bytes[16]; std::size_t size; };
const Sig kSigs[]={
    {kUpdate,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x30,0x48},16},
    {0x6443A2,{0xE8,0xE9,0x13,0x00,0x00},5},                                   // call 0x645790: matrix, input, aims
    {0x6443F6,{0xE8,0x95,0x0D,0x00,0x00},5},                                   // call 0x645190: then the heading's step
    {0x6457C9,{0x48,0x81,0xC1,0x20,0x17,0x00,0x00},7},                         // add rcx,0x1720: the controller
    {0x645559,{0xF3,0x0F,0x10,0x8F,0xB4,0x02,0x00,0x00},8},                    // movss xmm1,[rdi+0x2B4]: the matrix's heading
    {0x645314,{0xF3,0x0F,0x11,0x83,0xB4,0x02,0x00,0x00},8},                    // movss [rbx+0x2B4],xmm0: its step
    {0x6459D0,{0x48,0x8B,0x8B,0x08,0x06,0x00,0x00,0x48,0x81,0xC1,0xE0,0x00,0x00,0x00},14},   // the seats' aims (seat+0xE0)
    {0x5FBCF6,{0xF3,0x0F,0x10,0x71,0x04,0xF3,0x0F,0x58,0xE3,0xF3,0x0F,0x10,0x09},13},        // axis: max +4, min +0
    {0x5FBD12,{0xF3,0x0F,0x11,0x61,0x08},5},                                   // ...angle +8
};

using UpdateFn=edf::VehicleInputFn;   // (this, rdx = the frame's time step, ...): all four registers forwarded
UpdateFn nextUpdate=nullptr;

// One per Nix a local player drives (two in split screen), keyed by its ObjRef; a slot not updated for kResyncMs is free.
struct Hold {
    ObjRef ref;
    float heading;      // the heading the last update began with (the matrix of that frame was built from it)
    ULONGLONG ms;
    bool held;
};
constexpr int kHolds=4;
Hold holds[kHolds]{};
ULONGLONG logAt=0;

// `v`'s hold, or (make) a free or stale slot for it: then `*fresh` false (nothing to give back yet).
Hold* HoldOf(const void* v,ULONGLONG ms,bool make,bool* fresh) noexcept {
    Hold* slot=nullptr;
    for(auto& h:holds) {
        if(h.ref.Is(v)){*fresh=ms-h.ms<=kResyncMs;return &h;}
        if(!slot && (!h.ref || ms-h.ms>kResyncMs))slot=&h;
    }
    *fresh=false;
    if(!make || !slot)return nullptr;
    *slot=Hold{};slot->ref=ObjRef::Of(v);
    return slot;
}

struct Cue { NixTorso t; ULONGLONG at; };
Cue cue{};
SRWLOCK cueLock=SRWLOCK_INIT;

// Seat 0's aim of a Nix the local player drives, or nullptr.
unsigned char* PlayerAim(unsigned char* v) noexcept {
    if(v[kDead] || SeatCount(v)==0)return nullptr;
    unsigned char* const seat=SeatAt(v,0);
    if(SeatRider(seat)!=Rider::player)return nullptr;
    const auto aim=At<unsigned char*>(seat,kSeatAim);
    if(!Readable(aim,kAimMode+4))return nullptr;
    const auto vt=At<const unsigned char*>(aim,0);
    return vt==image+kAimVt || vt==image+kAimSeVt ? aim : nullptr;
}

float* YawAxis(unsigned char* aim) noexcept { return reinterpret_cast<float*>(aim+kAimAxes); }
float* PitchAxis(unsigned char* aim) noexcept { return reinterpret_cast<float*>(aim+kAimAxes+kAxisStride); }

// Before the stock update: the yaw axis given back what the legs turned since the last update.
void Before(unsigned char* v) noexcept {
    const ULONGLONG ms=GameMs();
    bool fresh=false;
    unsigned char* const aim=PlayerAim(v);
    if(!aim) {   // nobody of ours at the stick (any more): its slot is free
        if(Hold* h=HoldOf(v,ms,false,&fresh))*h=Hold{};
        return;
    }
    const float heading=At<float>(v,kHeading);
    if(!std::isfinite(heading))return;
    Hold* const hold=HoldOf(v,ms,true,&fresh);
    if(!hold)return;   // more than kHolds local Nixes: this one stays stock
    const bool steered=!aim[kAimTargetOn] || At<std::int32_t>(aim,kAimMode)!=kModeTarget;
    const Config& c=Cfg();
    float* const yaw=YawAxis(aim);
    if(!fresh) {
        const nixtwist::Stops s=nixtwist::TwistStops(yaw[kAxisMin/4],yaw[kAxisMax/4]);
        Log("NIX v=%p: the player drives it; torso %s, twist %.0f deg, stops %.0f..%.0f deg (axis %.0f..%.0f), pitch axis %.0f..%.0f deg",
            v,c.nixTorsoTwist ? "held in the world" : "stock (turns with the legs)",yaw[kAxisAngle/4]*kRad,s.lo*kRad,s.hi*kRad,
            yaw[kAxisMin/4]*kRad,yaw[kAxisMax/4]*kRad,PitchAxis(aim)[kAxisMin/4]*kRad,PitchAxis(aim)[kAxisMax/4]*kRad);
    }
    hold->held=c.nixTorsoTwist && steered;
    if(fresh && hold->held) {
        const nixtwist::Stops s=nixtwist::TwistStops(yaw[kAxisMin/4],yaw[kAxisMax/4]);
        yaw[kAxisAngle/4]=nixtwist::Hold(yaw[kAxisAngle/4],nixtwist::Wrap(heading-hold->heading),s);
    }
    hold->heading=heading;hold->ms=ms;
}

// Once a second with Debug: the heading the matrix was built from against the matrix's yaw (equal if the RE holds),
// the torso, and the drawn camera's yaw against the torso's (near 0 if the camera follows the torso's aim).
void DebugLog(const unsigned char* v,const Hold& h,const NixTorso& t) noexcept {
    const ULONGLONG now=GetTickCount64();
    if(!Cfg().debug || now-logAt<kLogMs)return;
    logAt=now;
    float eye[3],dir[3];
    const bool cam=CameraRay(eye,dir);
    const float camYaw=cam ? std::atan2(dir[0],dir[2]) : 0.0f;
    Log("NIX v=%p: heading %.1f (matrix %.1f) torso %.1f twist %.1f [%.0f..%.0f] pitch %.1f%s camera-torso %s%.1f deg",v,
        nixtwist::Wrap(h.heading)*kRad,t.legsYaw*kRad,t.torsoYaw*kRad,t.twist*kRad,t.twistMin*kRad,t.twistMax*kRad,t.pitch*kRad,
        t.held ? " held" : "",cam ? "" : "(no camera) ",cam ? nixtwist::Wrap(camYaw-t.torsoYaw)*kRad : 0.0f);
}

// After the stock update: the frame's legs and torso, published.
void After(unsigned char* v) noexcept {
    bool fresh=false;
    const Hold* const hold=HoldOf(v,GameMs(),false,&fresh);
    unsigned char* const aim=hold ? PlayerAim(v) : nullptr;
    if(!aim)return;
    const float* yaw=YawAxis(aim);
    const float* pitch=PitchAxis(aim);
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    NixTorso t{};
    t.vehicle=v;
    std::memcpy(t.at,v+kPosition,12);
    t.legsYaw=std::atan2(m[8],m[10]);
    t.twist=yaw[kAxisAngle/4];
    const nixtwist::Stops s=nixtwist::TwistStops(yaw[kAxisMin/4],yaw[kAxisMax/4]);
    t.twistMin=s.lo;t.twistMax=s.hi;
    t.torsoYaw=nixtwist::Wrap(t.legsYaw+t.twist);
    t.pitch=-pitch[kAxisAngle/4];
    float local[3];
    nixtwist::AimLocal(t.twist,pitch[kAxisAngle/4],local);
    for(int c=0;c<3;++c)t.dir[c]=local[0]*m[c]+local[1]*m[4+c]+local[2]*m[8+c];
    const float n=std::sqrt(t.dir[0]*t.dir[0]+t.dir[1]*t.dir[1]+t.dir[2]*t.dir[2]);
    if(!(n>0.5f && n<2.0f))return;
    for(float& d:t.dir)d/=n;
    t.held=hold->held;
    AcquireSRWLockExclusive(&cueLock);
    cue=Cue{t,GetTickCount64()};
    ReleaseSRWLockExclusive(&cueLock);
    DebugLog(v,*hold,t);
}

int Fault(const char* where,const EXCEPTION_POINTERS* e) noexcept {
    static ULONGLONG at=0;
    const ULONGLONG now=GetTickCount64();
    if(now-at>10000) {
        at=now;
        Log("FAULT nix %s: %08lX at %p (skipped this frame)",where,e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void __fastcall UpdateHook(void* v,std::uintptr_t a2,void* a3,void* a4) {
    const bool on=Cfg().enabled;
    if(on) {
        __try { Before(static_cast<unsigned char*>(v)); } __except(Fault("before",GetExceptionInformation())) {}
    }
    nextUpdate(v,a2,a3,a4);
    if(on) {
        __try { After(static_cast<unsigned char*>(v)); } __except(Fault("after",GetExceptionInformation())) {}
    }
}
}  // namespace

bool InstallNix() noexcept {
    __try {
        for(const auto& s:kSigs)
            if(!Matches(s.rva,s.bytes,s.size)){Log("NIX code at %#x not as expected: the Nix's torso stays stock",s.rva);return false;}
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    void** const slot=reinterpret_cast<void**>(image+kVtNix)+kSlotUpdate;
    if(*slot!=image+kUpdate)Log("NIX update slot holds %p (another plugin): chaining onto it",*slot);
    void* next=nullptr;
    if(!edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&UpdateHook),&next)){Log("NIX update slot patch failed: the torso stays stock");return false;}
    nextUpdate=reinterpret_cast<UpdateFn>(next);
    Log("HOOK nix torso twist (Vehicle612_nix slot 4)");
    return true;
}

void ResetNix() noexcept {
    for(auto& h:holds)h=Hold{};
    logAt=0;
    AcquireSRWLockExclusive(&cueLock);
    cue=Cue{};
    ReleaseSRWLockExclusive(&cueLock);
}

bool PlayerNixTorso(NixTorso* out) noexcept {
    AcquireSRWLockShared(&cueLock);
    const Cue c=cue;
    ReleaseSRWLockShared(&cueLock);
    if(!c.at || GetTickCount64()-c.at>kCueMs)return false;
    *out=c.t;
    return true;
}
}  // namespace crew
