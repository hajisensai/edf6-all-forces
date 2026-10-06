// The EMC's charged beam (EMC 蓄力光束, docs/emc-re.md; the user 2026-10-06: "改成'蓄力 → 一道粗光束持续 2–3 秒'的节奏，
// 总伤害集中在这一发里 ... 光束要贯穿，沿途建筑和地形一起摧毁 ... 几百米范围的爆炸").
// The EMC (V510_MASER.SGO, Vehicle510_Maser; the Air Raider's EMC / EMCS / EMCX requests) fires its one weapon
// (V_510_MASER_THUNDER01.SGO, Weapon_VehicleMaser) as a 1000-round burst, a round a frame (16.7 s) at 5 damage each.
// With EmcBeam on and the player in its seat, the trigger is taken off the seat before the stock input reads it (as
// the drill tank's, drill.cpp), so the stock burst never starts. Held, it charges (EmcChargeSec; a thin glow along the
// barrel thickening and the game's own charge loop rising in pitch); full, it fires one thick beam for EmcBeamSec that
// spends a burst's rounds and carries their whole damage (emc_plan.h Plan: AmmoDamage x the weapon's damage factor x
// FireBurstCount), every round of it passing through every enemy on its line (the satellite laser's penetrating round),
// so each enemy the beam crosses takes the burst's damage; meanwhile every building on its line gets a break charge
// (EmcBreak HP a second, the stock break-building path: a blast of 3 m or more, docs/drill-re.md §3); when it ends, a
// blast of EmcBlastRadius on what it ends on (EmcBlastShare of the beam's damage). The terrain cannot be broken: EDF6's
// ground is static baked collision and render meshes (docs/emc-re.md §4), so the beam ends on the ground it meets.
// Everything is fired by the EMC: its side's enemies only (the IFC's team is its owner's), its kills.
// An NPC driver's EMC is stock (its AI pulls the weapon itself); so is the player's without the files (tools/make_emc.py).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "body506.h"
#include "emc_plan.h"
#include "layout.h"
#include "memory.h"
#include "edf/weapon.h"
#include <cmath>
#include <cstdio>

namespace crew {
namespace {
// Vehicle510_Maser: its vtable and its input (slot 55, 0x61DDF0), which reads seat 0's trigger (seat+0x2E4) through
// 0x62DE50 (>= 0.8) and pulls weapon holder 0 with it (0x61DE34..0x61DE53): the same bytes as the Blacker's (drill.cpp).
constexpr unsigned kVt510=0x17DB9D8;
constexpr std::size_t kSeatTrigger=0x2E4;
constexpr float kTriggerOn=0.8f;
const unsigned char kTriggerSig[]={0xF3,0x0F,0x10,0x8B,0xE4,0x02,0x00,0x00,0x48,0x8D,0x8B,0xC0,0x02,0x00,0x00,0xE8};
constexpr unsigned kTriggerRead=0x61DE34;
// Its weapon (holder 0): Weapon_VehicleMaser (vtable 0x17E5E40, its ctor 0x6B08A0 writes it at 0x6B08D7), and the fields
// the budget reads (docs/emc-re.md §2): AmmoDamage +0x89C (the SGO's, 0x68D6EC), FireBurstCount +0x370 (0x68CE85), the
// damage factor +0x788 (each frame the vehicle's update writes veh+0x398 x holder+0x40, 0x630449; holder+0x40 is
// veh+0x678 = the request's vehicle_setup[0][1] x the difficulty's, 0x62F9AA / 0x633936; the weapon hands it to every
// round it fires, 0x697489), rounds left +0xBE8 (0x696819) and the magazine +0x248.
constexpr unsigned kMaserVtable=0x17E5E40;
constexpr std::size_t kWeaponDamage=0x89C,kWeaponFactor=0x788,kWeaponBurst=0x370,kWeaponMagazine=0x248;
struct Sig { unsigned rva; unsigned char bytes[8]; std::size_t size; };
const Sig kWeaponSigs[]={
    {0x6B08D7,{0x48,0x8D,0x05,0x62,0x55,0x13,0x01},7},   // lea rax,[rip -> 0x17E5E40]
    {0x68D6EC,{0xF3,0x0F,0x11,0x86,0x9C,0x08,0x00,0x00},8},   // movss [rsi+0x89C],xmm0
    {0x68CE85,{0x89,0x86,0x70,0x03,0x00,0x00},6},             // mov [rsi+0x370],eax
    {0x630449,{0xF3,0x0F,0x59,0x73,0x30,0xF3,0x0F,0x11},8},   // mulss xmm6,[rbx+0x30]; movss [rax+0x788],xmm6
    {0x696819,{0x89,0x87,0xE8,0x0B,0x00,0x00},6},             // mov [rdi+0xBE8],eax
};
// The game's sounds (docs/sound-re.md §2): SeManager *(image+0x20B2950); play a SEPRESET entry by name at a point into a
// handle (0x7B2A80: mgr, pos, name, handle), its pitch (0x7A8BF0: handle, p; p 1 = as recorded, 0..2 = an octave down /
// up) and place (0x7A8C20), stop (0x7A8CD0: handle, fade frames) and release (0x7A8730). The charge: the Wing Diver's
// weapon charge loop (SEPRESET ＷＤ武器チャージループ, weapon_WD_chargeIn_Bloop, in the always-loaded TIKYUUX_SE.ACB),
// pitched from kPitchLow to kPitchHigh as it fills: a loop, so it is stopped by the plugin (fire, let go, the EMC left).
constexpr std::size_t kSeManager=0x20B2950;
constexpr unsigned kSePlay=0x7B2A80,kSePitch=0x7A8BF0,kSePlace=0x7A8C20,kSeStop=0x7A8CD0,kSeRelease=0x7A8730;
struct SeSig { unsigned rva; unsigned char bytes[12]; };
const SeSig kSeSigs[]={
    {kSePlay,{0x40,0x53,0x55,0x56,0x57,0x48,0x81,0xEC,0xB8,0x00,0x00,0x00}},
    {kSePitch,{0x48,0x8B,0x41,0x08,0x48,0x85,0xC0,0x74,0x08,0xF3,0x0F,0x11}},
    {kSePlace,{0x4C,0x8B,0x41,0x08,0x4D,0x85,0xC0,0x74,0x20,0x8B,0x02,0x41}},
    {kSeStop,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x41,0x08,0x48,0x8B}},
    {kSeRelease,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8,0xC2,0x03}},
};
const wchar_t kChargeSe[]=L"ＷＤ武器チャージループ";
constexpr float kPitchLow=0.6f,kPitchHigh=1.4f;
constexpr int kSeFade=6;   // frames the charge loop fades out over
struct SoundHandle { void* ctrl; void* inst; };   // 16 bytes, zeroed before a play (docs/sound-re.md §2)

// The beam's parts (m): it starts kAhead past the muzzle (clear of the barrel); a break charge flies from kLead short
// of the building's face to kInto past it (vcobjects.py EMC_BREAK_SPEED x EMC_BREAK_LIFE = 10 m of flight: it meets the
// face); the blast's from kBlastLead short of the end to kInto past. A break charge every kBreakSec.
constexpr float kAhead=3.0f,kLead=3.0f,kInto=2.0f,kBlastLead=4.0f;
constexpr float kBreakSec=0.1f;
// m: the blast SGO's own radius (vcobjects.py EMC_BLAST_RADIUS), what it bursts with when RoundBlast cannot write EmcBlastRadius.
constexpr float kSgoBlastRadius=300.0f;
// The glow while it charges: from kSightThin to kSightThick as the charge fills (vcobjects.py EMC_SIGHT_SIZE is the first).
constexpr float kSightThin=0.6f,kSightThick=4.0f;
constexpr ULONGLONG kStaleMs=2000;    // an EMC not seen this long is gone: its slot is free
constexpr ULONGLONG kGoneMs=250;      // ...and its charge loop and rounds stopped after this (EmcTick)
constexpr ULONGLONG kLogMs=1000;      // Debug: the beam's line at most this often
constexpr int kMaxEmcs=4;

struct Emc {
    ObjRef ref;
    ULONGLONG seen,lastMs,frame,logAt;
    emc::State st;
    bool held,player;
    RoundObj sight,beam;
    SoundHandle se;
    bool sounding;
    emc::Budget budget;
    float breakLeft,end[3];
    bool endHit;
    int breaks,breaksFired,breaksMissed;
};
Emc emcs[kMaxEmcs]{};
bool triggerOk=false,weaponOk=false,seOk=false;

// The local player's EMC now (game thread writes, the HUD's draw thread reads).
SRWLOCK cueLock=SRWLOCK_INIT;
EmcCue cue{};
ULONGLONG cueAt=0;
constexpr ULONGLONG kCueFreshMs=300;

bool Is510(const void* v) noexcept { return At<const unsigned char*>(v,0)==image+kVt510; }

// Holder 0's weapon while it is the EMC's maser and its holder is live; nullptr otherwise.
unsigned char* Maser(const unsigned char* v) noexcept {
    const auto holders=At<unsigned char*>(v,kHolders);
    const auto count=At<std::uint64_t>(v,kHolderCount);
    if(count==0 || count>16 || !Readable(holders,kHolderStride))return nullptr;
    const auto ctrl=At<const unsigned char*>(holders,kHolderCtrl);
    if(!ctrl || !Readable(ctrl,0x10) || At<std::int32_t>(ctrl,8)==0)return nullptr;
    const auto weapon=At<unsigned char*>(holders,kHolderWeapon);
    if(!Readable(weapon,kWeaponAmmo+4) || At<const unsigned char*>(weapon,0)!=image+kMaserVtable)return nullptr;
    return weapon;
}

// Where the beam leaves: the maser's muzzle and its way (MeanMuzzle; else the weapon's own rows, +0x150, which the
// holder update copies from its aim bone each frame), kAhead along it.
bool Muzzle(const unsigned char* weapon,float* pos,float* dir) noexcept {
    if(!edf::MeanMuzzle(weapon,4,pos,dir)) {
        const float* rows=reinterpret_cast<const float*>(weapon+edf::kWeaponMatrix);
        std::memcpy(dir,rows+8,12);std::memcpy(pos,rows+12,12);
        const float l=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
        if(!(l>0.5f) || !std::isfinite(pos[0]+pos[1]+pos[2]))return false;
        for(int i=0;i<3;++i)dir[i]/=l;
    }
    for(int i=0;i<3;++i)pos[i]+=dir[i]*kAhead;
    return true;
}

// The charge loop (see kChargeSe): started, stopped, kept on the muzzle and pitched by the charge.
void SoundStart(Emc& e,const float* at) noexcept {
    if(!seOk || e.sounding)return;
    void* const mgr=At<void*>(image,kSeManager);
    if(!mgr)return;
    alignas(16) const float p[4]={at[0],at[1],at[2],1.0f};
    e.se=SoundHandle{};
    reinterpret_cast<void(*)(void*,const float*,const wchar_t*,SoundHandle*)>(image+kSePlay)(mgr,p,kChargeSe,&e.se);
    e.sounding=e.se.inst!=nullptr;
}
void SoundStep(Emc& e,const float* at,float charge) noexcept {
    if(!e.sounding)return;
    reinterpret_cast<void(*)(SoundHandle*,const float*)>(image+kSePlace)(&e.se,at);
    reinterpret_cast<void(*)(SoundHandle*,float)>(image+kSePitch)(&e.se,kPitchLow+(kPitchHigh-kPitchLow)*charge);
}
void SoundStop(Emc& e) noexcept {
    if(!e.sounding)return;
    e.sounding=false;
    reinterpret_cast<void(*)(SoundHandle*,int)>(image+kSeStop)(&e.se,kSeFade);
    reinterpret_cast<void(*)(SoundHandle*)>(image+kSeRelease)(&e.se);
    e.se=SoundHandle{};
}

// An EMC's state dropped: its charge loop stopped, its glow and beam deleted (a beam cut short fires no blast).
void Abort(Emc& e) noexcept {
    SoundStop(e);
    RoundDrop(e.sight);RoundDrop(e.beam);
    e=Emc{};
}

// v's state; none yet: a slot of its own when `make` (the player is in its seat: an NPC's EMC needs none), a free one
// or a gone EMC's (its leftovers dropped).
Emc* EmcOf(const void* v,ULONGLONG ms,bool make) noexcept {
    Emc* slot=nullptr;
    for(auto& e:emcs) {
        if(e.ref.Is(v))return &e;
        if(!slot && (!e.ref || e.ref.obj==v || ms-e.seen>kStaleMs))slot=&e;
    }
    if(!make || !slot)return nullptr;
    Abort(*slot);
    slot->ref=ObjRef::Of(v);slot->seen=ms;
    return slot;
}

// The line from `from` along `dir` this frame (emc_plan.h ScanLine with the map's and the buildings' rays).
emc::Scan Line(const float* from,const float* dir) noexcept {
    float hit[3];
    return emc::ScanLine(from,dir,emc::kBeamRange,[&](const float* a,const float* b){return MapRay(a,b,hit);},
                         [&](const float* a,const float* b){return BuildingRay(a,b,hit);});
}

// A break charge on each building on the line (see kLead), EmcBreak x kBreakSec each.
void Break(unsigned char* v,Emc& e,const float* from,const float* dir,const emc::Scan& s) noexcept {
    const float damage=Cfg().emcBreak*kBreakSec;
    if(!(damage>0.0f))return;
    for(int k=0;k<s.breaks;++k) {
        const float a=s.at[k]-kLead>0.0f ? s.at[k]-kLead : 0.0f,b=s.at[k]+kInto;
        const float p[3]={from[0]+dir[0]*a,from[1]+dir[1]*a,from[2]+dir[2]*a},q[3]={from[0]+dir[0]*b,from[1]+dir[1]*b,from[2]+dir[2]*b};
        if(EmcFire(EmcRound::breakCharge,v,p,q,damage).obj)++e.breaksFired;
        else ++e.breaksMissed;
    }
}

// The beam's frame: its line scanned, the beam steered onto its end, the buildings on it charged every kBreakSec.
void Beam(unsigned char* v,Emc& e,const float* from,const float* dir,float dt,ULONGLONG ms) noexcept {
    const emc::Scan s=Line(from,dir);
    for(int i=0;i<3;++i)e.end[i]=from[i]+dir[i]*s.end;
    e.endHit=s.end<emc::kBeamRange-1.0f;
    RoundSteer(e.beam,from,e.end);
    e.breakLeft-=dt;
    if(e.breakLeft<=0.0f) {
        e.breakLeft+=kBreakSec;
        Break(v,e,from,dir,s);
        e.breaks+=s.breaks;
    }
    if(Cfg().debug && ms-e.logAt>=kLogMs) {
        e.logAt=ms;
        Log("EMC v=%p beam: %.0f m to its end (%s), %d building(s) on its line (the nearest %.0f m), break charges %d fired, %d not",v,s.end,
            s.ground ? "the ground" : e.endHit ? "deep in buildings" : "nothing: its reach",s.breaks,s.breaks ? s.at[0] : -1.0f,e.breaksFired,
            e.breaksMissed);
    }
}

// Fired: a burst's rounds spent, the beam made with its round's share of their damage (Plan).
void Fire(unsigned char* v,Emc& e,unsigned char* weapon,const float* from,const float* dir) noexcept {
    const auto& c=Cfg();
    const float perHit=At<float>(weapon,kWeaponDamage)*At<float>(weapon,kWeaponFactor);
    const std::int32_t burst=At<std::int32_t>(weapon,kWeaponBurst),ammo=At<std::int32_t>(weapon,kWeaponAmmo);
    e.budget=emc::Plan(std::isfinite(perHit) && perHit>0.0f ? perHit : 0.0f,burst,ammo,c.emcBeamSec,c.emcBlastShare);
    Put<std::int32_t>(weapon,kWeaponAmmo,ammo-e.budget.used);
    const emc::Scan s=Line(from,dir);
    for(int i=0;i<3;++i)e.end[i]=from[i]+dir[i]*s.end;
    e.beam=EmcFire(EmcRound::beam,v,from,e.end,e.budget.perRound);
    e.breakLeft=0.0f;e.breaks=e.breaksFired=e.breaksMissed=0;e.logAt=0;
    Log("EMC v=%p fired: %d of %d rounds (burst %d, %.2f damage each) = %.0f damage through every enemy on its line, %d rounds of %.2f over %.1f s; "
        "blast %.0f m, %.0f damage; beam %s, its end %.0f m (%s)",v,e.budget.used,ammo,burst,perHit,e.budget.line,e.budget.rounds,
        e.budget.perRound,c.emcBeamSec,c.emcBlastRadius,e.budget.blast,e.beam.obj ? "made" : "NOT made",s.end,s.ground ? "the ground" : "no ground");
}

// The beam over: deleted, the blast on what it ended on (nothing there, its reach in the air: none).
void End(unsigned char* v,Emc& e,const float* from,const float* dir) noexcept {
    RoundDrop(e.beam);
    const float* f=from;
    float d=0.0f;
    for(int i=0;i<3;++i)d+=(e.end[i]-f[i])*dir[i];
    if(!e.endHit || !(e.budget.blast>0.0f)) {
        Log("EMC v=%p beam over: %d building charge(s) fired (%d not); %s",v,e.breaksFired,e.breaksMissed,
            e.endHit ? "no blast (EmcBlastShare 0)" : "nothing at its end: no blast");
        return;
    }
    const float a=d-kBlastLead>0.0f ? d-kBlastLead : 0.0f,b=d+kInto;
    const float p[3]={f[0]+dir[0]*a,f[1]+dir[1]*a,f[2]+dir[2]*a},q[3]={f[0]+dir[0]*b,f[1]+dir[1]*b,f[2]+dir[2]*b};
    RoundObj blast=EmcFire(EmcRound::blast,v,p,q,e.budget.blast);
    const bool sized=blast.obj && RoundBlast(blast,Cfg().emcBlastRadius);
    Log("EMC v=%p beam over: %d building charge(s) fired (%d not); blast %s at (%.0f,%.0f,%.0f), %.0f damage, %.0f m%s",v,e.breaksFired,
        e.breaksMissed,blast.obj ? "fired" : "NOT fired",q[0],q[1],q[2],e.budget.blast,sized ? Cfg().emcBlastRadius : kSgoBlastRadius,
        sized || !blast.obj ? "" : " (the SGO's: its radius could not be written)");
}

void Publish(const Emc& e,const unsigned char* v,const unsigned char* weapon) noexcept {
    const std::int32_t ammo=At<std::int32_t>(weapon,kWeaponAmmo),burst=At<std::int32_t>(weapon,kWeaponBurst);
    EmcCue c{};
    c.charge=e.st.charge;c.beamLeft=e.st.beamLeft;c.rearm=e.st.rearm;
    c.beams=burst>0 ? (ammo+burst-1)/burst : 0;
    c.charging=e.st.phase==emc::Phase::charging;c.firing=e.st.phase==emc::Phase::firing;c.empty=ammo<=0;
    std::memcpy(c.pos,v+kPosition,12);
    AcquireSRWLockExclusive(&cueLock);
    cue=c;cueAt=GetTickCount64();
    ReleaseSRWLockExclusive(&cueLock);
}

// The EMC `v`'s state and its maser: the player in its seat, or a charge / beam of its own still going. Dead or without
// its maser: none, and what it had going is dropped.
Emc* EmcOfVehicle(unsigned char* v,unsigned char** weapon) noexcept {
    if(!Is510(v) || SeatCount(v)==0)return nullptr;
    const bool driven=SeatRider(SeatAt(v,0))==Rider::player;
    Emc* const e=EmcOf(v,GameMs(),driven);
    *weapon=v[kDead] ? nullptr : Maser(v);
    if(e && !*weapon){Abort(*e);return nullptr;}
    return e;
}

// The charged beam is on: its code checked, the plugin and EmcBeam on, its rounds preloaded.
bool Ready() noexcept {
    return triggerOk && weaponOk && Cfg().enabled && Cfg().emcBeam && EmcRoundReady(EmcRound::beam) && EmcRoundReady(EmcRound::breakCharge) &&
           EmcRoundReady(EmcRound::blast);
}
}  // namespace

bool InstallEmc() noexcept {
    triggerOk=Matches(kTriggerRead,kTriggerSig,sizeof(kTriggerSig));
    weaponOk=Readable(image+kMaserVtable,8);
    for(const auto& s:kWeaponSigs)weaponOk=weaponOk && Matches(s.rva,s.bytes,s.size);
    seOk=true;
    for(const auto& s:kSeSigs)seOk=seOk && Matches(s.rva,s.bytes,sizeof(s.bytes));
    Log("HOOK emc trigger=%d weapon=%d sound=%d ifc=%d%s",triggerOk,weaponOk,seOk,EmcIfcOk(),
        triggerOk && weaponOk ? "" : " (unexpected EDF.dll code: the EMC keeps its stock burst)");
    return triggerOk && weaponOk;
}

bool IsEmc(const void* v) noexcept { return v && Is510(v); }

// Before the stock input (crew.cpp InputHook): the player's trigger is the charge's, taken off the seat so the stock
// input never pulls the maser.
void EmcInput(unsigned char* v) noexcept {
    if(!Ready() || !Is510(v))return;
    unsigned char* weapon=nullptr;
    Emc* const e=EmcOfVehicle(v,&weapon);
    if(!e)return;
    unsigned char* const seat=SeatAt(v,0);
    e->player=SeatRider(seat)==Rider::player;
    e->held=false;
    if(!e->player)return;
    float* const trigger=reinterpret_cast<float*>(seat+kSeatTrigger);
    e->held=*trigger>=kTriggerOn;
    *trigger=0.0f;
}

// After the stock input: the charge / beam / rearm step (emc_plan.h Step) and what it starts and ends, once a frame.
// The plugin off too (crew.cpp InputHook runs it before the Enabled test): Ready() is false then, and a charge going is
// let go (its loop stopped, its glow deleted) as for EmcBeam=0; a beam already out finishes (it was fired).
void EmcFrame(unsigned char* v) noexcept {
    if(!Is510(v))return;
    unsigned char* weapon=nullptr;
    Emc* const e=EmcOfVehicle(v,&weapon);
    if(!e)return;
    const ULONGLONG ms=GameMs(),frame=GameFrame();
    if(e->frame==frame)return;
    e->frame=frame;
    const float dt=GameStep(e->lastMs ? ms-e->lastMs : 0);
    e->lastMs=e->seen=ms;
    // Switched off (or its files gone): a charge is let go; a beam already out finishes (it was fired).
    const bool on=Ready();
    if(!on && e->st.phase!=emc::Phase::firing) {
        if(e->st.phase==emc::Phase::charging){RoundDrop(e->sight);SoundStop(*e);}
        e->st=emc::State{};
        return;
    }
    float from[3],dir[3];
    if(!Muzzle(weapon,from,dir)) {   // never seen unreadable; the vehicle's nose then, so the cycle still runs to its end
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        for(int i=0;i<3;++i){dir[i]=m[8+i];from[i]=m[12+i]+m[4+i]*kAhead+m[8+i]*kAhead;}
    }
    const emc::Tuning t{Cfg().emcChargeSec,Cfg().emcBeamSec};
    const bool held=on && e->player && e->held;
    switch(emc::Step(e->st,t,held,At<std::int32_t>(weapon,kWeaponAmmo)>0,dt)) {
        case emc::Event::start:
            if(!e->sight.obj)e->sight=EmcFire(EmcRound::sight,v,from,from,0.0f);
            SoundStart(*e,from);
            if(Cfg().debug)Log("EMC v=%p charging (%.1f s to full)",v,t.chargeSec);
            break;
        case emc::Event::cancel:
            RoundDrop(e->sight);SoundStop(*e);
            if(Cfg().debug)Log("EMC v=%p let go before full: no shot",v);
            break;
        case emc::Event::fire:
            RoundDrop(e->sight);SoundStop(*e);
            Fire(v,*e,weapon,from,dir);
            break;
        case emc::Event::end:
            End(v,*e,from,dir);
            break;
        case emc::Event::none: break;
    }
    if(e->st.phase==emc::Phase::charging) {
        const emc::Scan s=Line(from,dir);
        const float end[3]={from[0]+dir[0]*s.end,from[1]+dir[1]*s.end,from[2]+dir[2]*s.end};
        RoundSteer(e->sight,from,end);
        RoundSize(e->sight,kSightThin+(kSightThick-kSightThin)*e->st.charge);
        SoundStep(*e,from,e->st.charge);
    } else if(e->st.phase==emc::Phase::firing) {
        Beam(v,*e,from,dir,dt,ms);
    }
    if(e->player)Publish(*e,v,weapon);
}

// Once a frame (crew.cpp InputHook, the plugin off too): an EMC no longer seen (deleted mid-charge or mid-beam: its input never comes round
// again) has its charge loop stopped and its rounds deleted (a looped sound would play on to the mission's end).
void EmcTick() noexcept {
    static ULONGLONG tickFrame=0;   // run from every vehicle's input, the plugin off too: once a frame
    if(tickFrame==GameFrame())return;
    tickFrame=GameFrame();
    const ULONGLONG ms=GameMs();
    for(auto& e:emcs)
        if(e.ref && ms-e.seen>kGoneMs && (e.sounding || e.sight.obj || e.beam.obj)) {
            Log("EMC v=%p not seen for %llu ms: its charge / beam dropped",e.ref.obj,static_cast<unsigned long long>(ms-e.seen));
            Abort(e);
        }
}

bool PlayerEmcCue(EmcCue* out) noexcept {
    AcquireSRWLockShared(&cueLock);
    const bool fresh=cueAt && GetTickCount64()-cueAt<=kCueFreshMs;
    if(fresh)*out=cue;
    ReleaseSRWLockShared(&cueLock);
    return fresh;
}

// A new mission: the last one's objects are gone with it (their memory the game's to reuse), so they are forgotten, not
// deleted; the charge loop is stopped (its handle holds the sound system's reference).
void ResetEmc() noexcept {
    for(auto& e:emcs) {
        __try { SoundStop(e); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        e=Emc{};
    }
}
}  // namespace crew
