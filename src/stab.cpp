// The gun stabilizer (README 炮管稳定器; docs/camera-re.md §7; the user, 2026-10-06: "vehicles that should have a gun
// stabilizer get one"): while the hull pitches, rolls and turns under it, a stabilized gun stays on the line it was
// laid on in the WORLD, as a modern tank's two-plane stabilizer holds it (elevation and traverse), within its turret's
// own drive and with a small error that grows with the hull's turn rate.
//  - Where (H, docs/camera-re.md §4, docs/nix-re.md §3): every seat's aim (VehicleWeaponAim at seat+0xE0) steps its two
//    axes once a frame from the vehicle's update, slot 2: 0x5FBDA0 (VehicleWeaponAim, vtable 0x17D8A68) or 0x5FCD80
//    (VehicleWeaponAimAddSe, 0x17D8A90: 0x5FBDA0 then a sound). Each axis' step 0x5FBC00 eases its rate toward the
//    input, adds it to the angle, stops it at the ends and maps the angle onto the axis' bones (0x5FC280, `dl` 1, at
//    its end: the bone entries the pose reads). The stabilizer runs right after the stock step of a seat it holds
//    (StabStep: the AddSe step through turretcam.cpp's hook, the plain one through this file's): the stock step's own
//    turn is the gun's command (the rider's stick, the turret camera's, EDF6AutoTurret's, the stock AI's), and the
//    stabilizer adds the turn that keeps the gun on its world line (stab.h Step), then maps the angles onto the bones
//    again (0x5FC280 with the corrected angle). The axis' rate is left as the stock step made it: the stock easing
//    goes on from the command alone.
//  - CarBase velocity-joint feedback is a separate writer: 669A52 -> 5FC140 replaces the previous axis target with
//    the actual physical angle before the next input step. ReadbackHook reconciles that local tracking displacement
//    in the previous pose basis, so the stabilizer does not become a second position servo fighting the native motor.
//    It does not replace either stored hull basis: the next step still compensates real hull motion and player input.
//  - Which gun, how well (the class table kClasses; the reasons in docs/camera-re.md §7): the main guns of the tanks
//    (Blacker 403, Titan 404, the single-seat tank 601, the E551 505) and of the Kepler (603), the Grape's turret (Car),
//    the gunner seats of the tanks and the mechs; not the artillery (402, a 603 with an indirect-fire weapon), the drill,
//    the bikes, the mechs' pilots, the Depth Crawler, the Maser, the aircraft.
//  - Which seats: a rider aboard (the player, an NPC, the dummy rider of an NPC-crewed vehicle) or an empty one
//    EDF6AutoTurret steers this frame (common/edf/aimlink.h V2 Steers); not one another machine runs (the aim's +0xC0:
//    the network's input, docs/nix-re.md §3; a remote rider). An EDF6AutoTurret older than the V3 link (its feed-forward
//    would count the hull's turn twice: it predates the stabilizer) keeps the seats it may steer stock.
//  - Which hull the drawn gun is seen in (stab.h Probe), measured per seat from the muzzle bones: the mount (a gunner's
//    gun may sit on the main turret) and whether the pose takes a hull that moved on after the aim step (then the
//    stabilizer aims for the hull a step ahead). A seat whose gun does not move as its axes say is left stock (logged).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "stab.h"
#include "turretaim.h"
#include "edf/weapon.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
using stab::Frame;
// The plain seat aim (VehicleWeaponAim) and its slot 2; the axis step's end (mov dl,1; ...; jmp 0x5FC280) and the bone
// map it jumps to; the angle (+8) and rate (+0xC) the step writes.
constexpr unsigned kPlainAimVtable=0x17D8A68,kPlainAimStep=0x5FBDA0,kAxisStepEnd=0x5FBD78,kAxisMap=0x5FC280;
constexpr unsigned kAxisAngleWrite=0x5FBD12,kAxisRateWrite=0x5FBCE8;
// CarBase's velocity motors read back their actual joint angle before the aim step. This is the only
// direct caller of 5FC140; position motors do not take it (669A1F requires motor type 1).
constexpr unsigned kReadbackCall=0x669A52,kReadback=0x5FC140;
const unsigned char kReadbackCode[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x40,0x0F,0x29,0x74,0x24,0x30,0x0F};
const unsigned char kReadbackCallCode[]={0xE8,0xE9,0x26,0xF9,0xFF,0x4C,0x8D,0x9C,0x24,0x90,0x01,0,0};
const unsigned char kReadbackWriteCode[]={0xF3,0x0F,0x11,0x5F,0x18,0xF3,0x0F,0x5C,0xDA,0xF3,0x0F,0x11,0x5F,0x20};
const unsigned char kVelocityMotorGateCode[]={0x83,0xFB,0x01,0x75,0x33};
constexpr std::size_t kAimStepSlot=2,kAimParams=0x90,kAimNetwork=0xC0,kAxisRate=0xC;
const unsigned char kPlainAimStepCode[]={0x40,0x53,0x55,0x56,0x57,0x48,0x83,0xEC,0x78,0x80,0xB9,0xC0,0x00,0x00,0x00,0x00,0x48,0x8B,0xF1,0x75,0x47};
const unsigned char kAxisStepEndCode[]={0xB2,0x01,0x0F,0x28,0x74,0x24,0x40,0x0F,0x28,0x7C,0x24,0x30,0x44,0x0F,0x28,0x44,0x24,0x20,0x48,0x83,0xC4,0x58,0xE9,0xED,0x04,0x00,0x00};
const unsigned char kAxisMapCode[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x57,0x48,0x81,0xEC,0x90,0x00,0x00,0x00};
const unsigned char kAxisAngleWriteCode[]={0xF3,0x0F,0x11,0x61,0x08};
const unsigned char kAxisRateWriteCode[]={0xF3,0x0F,0x11,0x51,0x0C};
constexpr std::uint64_t kMostMuzzles=16;
constexpr ULONGLONG kLogMs=1000;

using AxisMapFn=void(__fastcall*)(void*,bool);
using ReadbackFn=void(__fastcall*)(void*,int,int,float);

// The classes (vtables as crew.cpp kClasses has them) whose guns have a stabilizer, the main gun's (seat 0) and the
// gunner seats' (stab::Perf: lag s, slip rad; lag 0 = none). Why each: docs/camera-re.md §7.
struct Class { unsigned vtable; const char* name; stab::Perf main,gunner; };
constexpr float kSlip=0.35f;   // 20 deg: past it the hull outruns the drive and drags the reference
const Class kClasses[]={
    {0x17D8FA0,"403 Blacker",{0.0015f,kSlip},{0.004f,kSlip}},   // a modern MBT; its side guns remote weapon stations
    {0x17D9458,"404 Titan",{0.003f,kSlip},{0.004f,kSlip}},      // a land battleship: heavy drives, a looser hold
    {0x17DC250,"601 tank",{0.0015f,kSlip},{0.004f,kSlip}},
    {0x17DADB0,"505 E551",{0.0015f,kSlip},{0.004f,kSlip}},      // the drill tank (same class) is excluded by IsDrillTank
    {0x17DC620,"603 Kepler",{0.0015f,kSlip},{0.004f,kSlip}},    // an AA gun tracking aircraft on the move; the howitzer
                                                                  // (an indirect-fire weapon on this class) is excluded
    {0x17E01B0,"Car Grape",{0.003f,kSlip},{0.004f,kSlip}},      // an IFV turret
    {0x17DA960,"504 Begaruta",{0.0f,0.0f},{0.005f,kSlip}},      // mechs: the pilot's arms no, the gunner turrets yes
    {0x17DE0A8,"Begaruta",{0.0f,0.0f},{0.005f,kSlip}},
    {0x17DD440,"612 Nix",{0.0f,0.0f},{0.005f,kSlip}},
};

const Class* ClassOfVehicle(const void* v) noexcept {
    const auto vt=At<const unsigned char*>(v,0);
    for(const Class& c:kClasses)if(vt==image+c.vtable)return &c;
    return nullptr;
}

// A seat's first weapon with muzzles (the gun the axes turn), or nullptr.
const unsigned char* SeatGun(const unsigned char* seat) noexcept {
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!count || count>8 || !Readable(holders,count*8))return nullptr;
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(Readable(w,edf::kWeaponAccuracyScale+4) && At<std::uint64_t>(w,edf::kMuzzleCount)>0)return w;
    }
    return nullptr;
}

// The stabilizer a vehicle's seat has (its class's, and none of the exclusions), or nullptr.
const stab::Perf* PerfOf(const unsigned char* v,unsigned seat,const Class* c) noexcept {
    if(!c || BodyOf(v)!=PluginBody::none || IsPlayerJet(v) || IsSub(v))return nullptr;
    const stab::Perf& p=seat==0 ? c->main : c->gunner;
    if(!(p.lag>0.0f))return nullptr;
    if(seat==0 && IsDrillTank(v))return nullptr;   // a drill held against its work, no gun on a target
    return &p;
}

// One seat's aim the stabilizer knows (registered by its vehicle's frame, StabFrame), and its state.
struct Entry {
    const unsigned char* aim;
    ObjRef ref;
    unsigned char* v;
    unsigned seat;
    const Class* cls;
    ULONGLONG seen;            // the game frame StabFrame last registered it
    // The step's state.
    ULONGLONG stepFrame;       // the game frame of its last step
    float before[2];           // its angles before that step (as the last pose drew them)
    bool active;               // the stabilizer held the gun in that step
    stab::Hold hold;
    stab::Probe probe;
    stab::Choice choice;
    bool hasHull;
    Frame hull;                // the hull's frame at the last step
    float posedWas[2],seat0Was;
    bool hadChoice,hadFit;     // the last logged choice
    int lastMount; bool lastNext;
    ULONGLONG logAt;
    float worst;               // rad, the largest error since the last log line
};
constexpr int kEntries=512;    // open addressing on the aim's address: 128 vehicles x a few seats, half full at most
Entry table[kEntries]{};
constexpr ULONGLONG kStaleFrames=4;   // registered no later than this many frames ago: still its vehicle's

bool hooked=false,plainHooked=false;
AxisMapFn axisMap=nullptr;
AimStepFn nextPlain=nullptr;
ReadbackFn nextReadback=nullptr;

std::size_t Slot(const void* aim) noexcept { return (reinterpret_cast<std::uintptr_t>(aim)>>4)%kEntries; }

Entry* Find(const void* aim) noexcept {
    for(std::size_t i=Slot(aim),n=0;n<kEntries;++n,i=(i+1)%kEntries) {
        if(table[i].aim==aim)return &table[i];
        if(!table[i].aim)return nullptr;
    }
    return nullptr;
}

// The entry for `aim` (a new one in a free or stale slot; another vehicle at the same address: started anew).
Entry* Claim(const unsigned char* aim,unsigned char* v,unsigned seat,const Class* c,ULONGLONG frame) noexcept {
    Entry* stale=nullptr;
    for(std::size_t i=Slot(aim),n=0;n<kEntries;++n,i=(i+1)%kEntries) {
        Entry& e=table[i];
        if(e.aim==aim) {
            if(!e.ref.Is(v) || e.seat!=seat){e=Entry{};e.aim=aim;e.ref=ObjRef::Of(v);}
            e.v=v;e.seat=seat;e.cls=c;e.seen=frame;
            return &e;
        }
        if(!stale && e.aim && frame-e.seen>kStaleFrames*30)stale=&e;
        if(!e.aim) {
            Entry& slot=stale ? *stale : e;
            slot=Entry{};slot.aim=aim;slot.ref=ObjRef::Of(v);slot.v=v;slot.seat=seat;slot.cls=c;slot.seen=frame;
            return &slot;
        }
    }
    if(stale){*stale=Entry{};stale->aim=aim;stale->ref=ObjRef::Of(v);stale->v=v;stale->seat=seat;stale->cls=c;stale->seen=frame;}
    return stale;
}

float* AxisOf(const unsigned char* aim,int i) noexcept {
    return reinterpret_cast<float*>(const_cast<unsigned char*>(aim)+kAimAxes+static_cast<std::size_t>(i)*kAxisStride);
}

// EDF6AutoTurret may turn this seat but predates the V3 link (its feed-forward would count the hull's turn twice).
bool LeftToOldAutoTurret(const unsigned char* v,unsigned seat) noexcept {
    return AutoTurretStabAware()==0 && AutoTurretSteers(v,seat)!=0;
}

// Whether the stabilizer holds this seat's gun this frame (see the top).
bool Wanted(const Entry& e,const unsigned char* seat) noexcept {
    const Config& c=Cfg();
    if(!c.enabled || !c.gunStabilizer || !e.ref.Is(e.v) || e.v[kDead] || GameFrame()-e.seen>kStaleFrames)return false;
    if(!PerfOf(e.v,e.seat,e.cls) || !SeatGun(seat) || At<unsigned char>(e.aim,kAimNetwork)!=0)return false;
    if(e.seat==0 && IndirectFireSeat(seat))return false;   // the artillery: fired from a halt
    const Rider r=SeatRider(seat);
    if(r==Rider::none && AutoTurretSteers(e.v,e.seat)!=1)return false;
    if(r!=Rider::none && edf::RemoteRider(At<const unsigned char*>(seat,kSeatRider)))return false;
    return !LeftToOldAutoTurret(e.v,e.seat);
}

void Off(Entry& e) noexcept { e.active=false;e.hold.live=false;e.hold.shift[0]=e.hold.shift[1]=0.0f;e.hold.error=0.0f;e.hold.slipping=false; }

void LogChoice(Entry& e,bool turret) noexcept {
    const stab::Choice& c=e.choice;
    if(c.known==e.hadChoice && c.fits==e.hadFit && c.mount==e.lastMount && c.next==e.lastNext)return;
    e.hadChoice=c.known;e.hadFit=c.fits;e.lastMount=c.mount;e.lastNext=c.next;
    if(!Cfg().debug)return;
    const stab::Probe& p=e.probe;
    Log("STAB v=%p (%s) seat %u: %s; mount %s, pose %s (errors same/next hull %.4f/%.4f%s turret %.4f/%.4f, motion hull %.3f mount %.3f all %.3f)",
        e.v,e.cls ? e.cls->name : "?",e.seat,!c.fits ? "the gun does not move as its axes say: left stock" : c.known ? "held" : "learning its mount",
        c.mount ? "the main turret" : "the hull",c.next ? "a step ahead" : "the step's own",p.err[0],p.err[1],turret ? "," : " (no",
        p.err[2],p.err[3],p.hull,p.mount,p.motion);
}

void Debug(Entry& e) noexcept {
    if(e.hold.error>e.worst)e.worst=e.hold.error;
    const ULONGLONG now=GetTickCount64();
    if(!Cfg().debug || now-e.logAt<kLogMs || SeatRider(SeatAt(e.v,e.seat))!=Rider::player)return;
    e.logAt=now;
    Log("STAB v=%p seat %u: %s, error now %.2f / worst %.2f deg, shift (%.3f,%.3f) deg, hull turned %.1f deg/s%s",e.v,e.seat,
        e.active ? "holding" : "off",e.hold.error*57.29578f,e.worst*57.29578f,e.hold.shift[0]*57.29578f,e.hold.shift[1]*57.29578f,
        e.hasHull ? e.probe.hull*(1.0f-stab::kDecay)*60.0f*57.29578f : 0.0f,e.hold.slipping ? ", dragged (the drive is outrun)" : "");
    e.worst=0.0f;
}

// The mount's frame now (`hull` turned by seat 0's yaw for a gun on the main turret) and the one the drawn gun will be
// seen in (that, or a step ahead at the rate it turned from `prev`: Probe's timing).
void Frames(const stab::Choice& c,const Frame& hull,const Frame* prev,float seat0,Frame* mount,Frame* seen) noexcept {
    *mount=c.mount ? stab::Turned(hull,seat0) : hull;
    if(!c.next || !prev){*seen=*mount;return;}
    const Frame ahead=stab::Ahead(hull,*prev);
    *seen=c.mount ? stab::Turned(ahead,seat0) : ahead;
}

stab::Stops StopsAt(const unsigned char* aim,int i) noexcept { const float* x=AxisOf(aim,i);return stab::StopsOf(x[0],x[1]); }

void __fastcall ReadbackHook(void* aim,int axis,int bone,float measuredJoint) noexcept {
    auto* a=static_cast<unsigned char*>(aim);
    const bool knownAxis=axis>=0 && axis<2;
    const float commanded=knownAxis ? AxisOf(a,axis)[2] : 0.0f;
    nextReadback(aim,axis,bone,measuredJoint);
    __try {
        Entry* e=hooked && knownAxis ? Find(aim) : nullptr;
        if(e && e->active && e->hold.live && GameFrame()-e->stepFrame<=1 && Wanted(*e,a-kSeatAim))
            stab::Readback(e->hold,StopsAt(a,axis),axis,commanded,AxisOf(a,axis)[2]);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// After the stock step of a seat's aim: the probe fed, the gun held (see the top).
void Hold(Entry& e,unsigned char* aim,const float* before) noexcept {
    const unsigned char* seat=aim-kSeatAim;
    const ULONGLONG frame=GameFrame();
    const bool consecutive=e.stepFrame+1==frame;
    float after[2]={AxisOf(aim,0)[2],AxisOf(aim,1)[2]};
    const bool want=Wanted(e,seat);
    Frame hull{};
    if(!stab::FromMatrix(reinterpret_cast<const float*>(e.v+kMatrix),&hull) || !std::isfinite(after[0]) || !std::isfinite(after[1])) {
        Off(e);e.hasHull=false;e.probe.has=false;return;
    }
    if(!consecutive){e.probe.has=false;e.hasHull=false;Off(e);}   // a gap: the history is no longer last frame's
    // Seat 0's yaw as the last pose drew it (its angle before its step this frame, else as it stands) and now.
    const bool turret=e.seat>0;
    float seat0=0.0f,seat0Posed=0.0f;
    if(turret) {
        const unsigned char* aim0=SeatAt(e.v,0)+kSeatAim;
        seat0=AxisOf(aim0,0)[2];
        const Entry* z=Find(aim0);
        seat0Posed=z && z->stepFrame==frame ? z->before[0] : seat0;
        if(!std::isfinite(seat0) || !std::isfinite(seat0Posed))seat0=seat0Posed=0.0f;
    }
    // The probe: the drawn gun's line against the axes as drawn.
    const unsigned char* gun=SeatGun(seat);
    float pos[3],dir[3];
    if(want && gun && e.hasHull && edf::MeanMuzzle(gun,kMostMuzzles,pos,dir))
        stab::Feed(e.probe,hull,e.hull,before,seat0Posed,e.seat0Was,e.posedWas,dir,turret);
    else if(!want)e.probe.has=false;
    e.posedWas[0]=before[0];e.posedWas[1]=before[1];e.seat0Was=seat0Posed;
    e.choice=stab::Decide(e.probe,turret,seat0);
    if(want)LogChoice(e,turret);
    const Frame prev=e.hull;
    const bool hadHull=e.hasHull;
    e.hull=hull;e.hasHull=true;
    if(!want || !e.choice.known || !e.choice.fits){Off(e);Debug(e);return;}
    Frame mount{},seen{};
    Frames(e.choice,hull,hadHull ? &prev : nullptr,seat0,&mount,&seen);
    const stab::Stops stops[2]={StopsAt(aim,0),StopsAt(aim,1)};
    const float top=At<float>(aim,kAimParams+8);
    float out[2];
    stab::Step(e.hold,stops,before,after,std::isfinite(top) ? std::fabs(top) : 0.0f,mount,seen,*PerfOf(e.v,e.seat,e.cls),out);
    e.active=true;
    for(int i=0;i<2;++i) {
        if(!std::isfinite(out[i]) || out[i]==after[i])continue;
        float* axis=AxisOf(aim,i);
        stab::Remap(axis,before[i],out[i],[](float* mapped) noexcept { axisMap(mapped,true); });
    }
    Debug(e);
}

void StepHook(void* aim,const float* in,AimStepFn next) noexcept {
    Entry* e=hooked ? Find(aim) : nullptr;
    if(!e){next(aim,in);return;}
    auto* a=static_cast<unsigned char*>(aim);
    const float before[2]={AxisOf(a,0)[2],AxisOf(a,1)[2]};
    next(aim,in);
    __try {
        Hold(*e,a,before);
        e->before[0]=before[0];e->before[1]=before[1];e->stepFrame=GameFrame();
    } __except(EXCEPTION_EXECUTE_HANDLER){Off(*e);}
}

void __fastcall PlainHook(void* aim,const float* in) { StepHook(aim,in,nextPlain); }
}  // namespace

bool InstallStabilizer() noexcept {
    __try {
        if(!Matches(kAxisStepEnd,kAxisStepEndCode,sizeof(kAxisStepEndCode)) || !Matches(kAxisMap,kAxisMapCode,sizeof(kAxisMapCode)) ||
           !Matches(kAxisAngleWrite,kAxisAngleWriteCode,sizeof(kAxisAngleWriteCode)) ||
           !Matches(kAxisRateWrite,kAxisRateWriteCode,sizeof(kAxisRateWriteCode)) ||
           !Matches(kReadback,kReadbackCode,sizeof(kReadbackCode)) ||
           !Matches(0x5FC230,kReadbackWriteCode,sizeof(kReadbackWriteCode)) ||
           !Matches(0x669A1F,kVelocityMotorGateCode,sizeof(kVelocityMotorGateCode)) ||
           !Matches(kReadbackCall,kReadbackCallCode,sizeof(kReadbackCallCode))) {
            hooked=false;
            Log("STAB the aim axis' step changed: no gun stabilizer");
            return false;
        }
        nextReadback=reinterpret_cast<ReadbackFn>(image+kReadback);
        bool changed=false;
        if(!RedirectCall(image+kReadbackCall,image+kReadback,reinterpret_cast<void*>(&ReadbackHook),changed) || !changed) {
            hooked=false;Log("STAB native joint readback unavailable: stabilizer disabled");return false;
        }
        axisMap=reinterpret_cast<AxisMapFn>(image+kAxisMap);
        hooked=true;   // the AddSe step comes through turretcam.cpp's hook (StabStep)
        auto slot=reinterpret_cast<void**>(image+kPlainAimVtable)+kAimStepSlot;
        void* next=nullptr;
        if(*slot!=image+kPlainAimStep || !Matches(kPlainAimStep,kPlainAimStepCode,sizeof(kPlainAimStepCode)))
            Log("STAB the plain seat aim's step is patched already or changed (%p): its seats left stock",*slot);
        else if(edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&PlainHook),&next)){nextPlain=reinterpret_cast<AimStepFn>(next);plainHooked=true;}
        Log("STAB hooks: plain aim=%d (the AddSe aim through the turret camera's hook)",plainHooked);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void StabFrame(unsigned char* v) noexcept {
    const Config& c=Cfg();
    if(!hooked || !c.enabled || !c.gunStabilizer)return;
    const Class* cls=ClassOfVehicle(v);
    if(!cls)return;
    const unsigned count=SeatCount(v);
    const ULONGLONG frame=GameFrame();
    // Every seat of the class, held or not: a gunner's probe reads seat 0's angles as its last pose drew them.
    for(unsigned i=0;i<count;++i)Claim(SeatAt(v,i)+kSeatAim,v,i,cls,frame);
}

void StabStep(void* aim,const float* in,AimStepFn next) noexcept { StepHook(aim,in,next); }

bool StabHeld(const void* aim,float* held,float* hull,float* frame) noexcept {
    const auto* a=static_cast<const unsigned char*>(aim);
    const float axes[2]={AxisOf(a,0)[2],AxisOf(a,1)[2]};
    held[0]=axes[0];held[1]=axes[1];hull[0]=hull[1]=0.0f;
    const Entry* e=hooked ? Find(aim) : nullptr;
    if(!e || !e->active || !e->hasHull || GameFrame()-e->stepFrame>1 || !e->ref.Is(e->v))return false;
    Frame now{};
    if(!stab::FromMatrix(reinterpret_cast<const float*>(e->v+kMatrix),&now))return false;
    // Seat 0's yaw as this frame's pose will draw it: a gunner asks before its step, seat 0 may have stepped or not.
    const float seat0=e->seat>0 ? AxisOf(SeatAt(e->v,0)+kSeatAim,0)[2] : 0.0f;
    Frame mount{},seen{};
    Frames(e->choice,now,&e->hull,std::isfinite(seat0) ? seat0 : 0.0f,&mount,&seen);
    const stab::Stops stops[2]={StopsAt(a,0),StopsAt(a,1)};
    Frame in{};
    const bool isHeld=stab::HeldIn(e->hold,stops,seen,now,axes,held,hull,&in);
    if(isHeld && frame)std::memcpy(frame,in.r,sizeof(in.r));
    return isHeld;
}

int StabState(unsigned char* vehicle,unsigned seat) noexcept {
    if(!hooked || seat>=SeatCount(vehicle))return 0;
    const Entry* e=Find(SeatAt(vehicle,seat)+kSeatAim);
    if(!e || !e->ref.Is(vehicle) || !e->active || GameFrame()-e->stepFrame>2)return 0;
    return e->hold.slipping ? 2 : 1;
}

void ResetStabilizer() noexcept {
    for(Entry& e:table)e=Entry{};
}
}  // namespace crew

// EDF6AutoTurret asks where the stabilizer holds `vehicle`'s seat `seat`'s gun this frame and how much of that the
// hull's turn is (common/edf/aimlink.h V3): it steers from the held axes and takes the hull's part out of what it learns.
extern "C" __declspec(dllexport) bool __cdecl EDF6VehicleCrew_StabilizerV3(const void* vehicle,unsigned seat,float* held,float* hull) {
    if(!vehicle || !held || !hull)return false;
    held[0]=held[1]=hull[0]=hull[1]=0.0f;
    __try {
        auto* v=static_cast<unsigned char*>(const_cast<void*>(vehicle));
        if(seat>=crew::SeatCount(v))return false;
        return crew::StabHeld(crew::SeatAt(v,seat)+crew::kSeatAim,held,hull,nullptr);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
