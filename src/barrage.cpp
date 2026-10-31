// The long-range barrages (docs/plan-2026-10-25-strikes.md §1.1; the user, 2026-10-25: 「攻击都做，实现方案可以随你」): a
// barrage entry of the map's support bar (tools/calls.py brings 'barrage', airstrike.cpp kStrikes; since 2026-10-26 no
// weapon) starts one at the point clicked (airstrike.cpp SupportStrikeAt); BarrageTick fires its rounds as
// src/barrage_plan.h planned them, and every round is flown along its curve by the plugin. Its area is warned of on the
// ground while it lasts (support_warning.h).
//  - a round: its own DemoIndirectFire (jet_bay.cpp BarrageFire: EDF6VC_BARRAGE_ROUND.SGO, one RocketBullet01 with its
//    smoke trail, no gravity, no damage of its own), owned by the caller (its kills, its side's friends spared), fired from
//    its launcher toward its curve's first stretch; its size, blast and mark written into the IFC before it fires (the
//    round takes its InitParam as it leaves);
//  - its mark: AmmoHitSizeAdjust (IFC +0x104, config 0x2B6AC7) as the float whose bits are kMarkBase | its slot, about
//    1.0005 (its hit radius as good as unchanged), copied into the round's core (+0xA34, read at its birth 0x231EEF);
//  - its flight: RocketBullet01's update (vtable 0x17A38E8 slot 5, 0x287E40, wrapped: UpdateHook) steps its core (0x235D50
//    at 0x287F50: velocity +0xB90 in m/s, gravity +0xBA0); before it, a marked round's velocity is set to reach where its
//    curve is next frame from where it is (PathAt), its gravity cleared. It bursts on what it meets as the stock round
//    does (its blast, the caller's damage); a cluster's carrier is put out at its split (its age to its life: the stock
//    round expires without a blast) and its bomblets leave there as rounds of their own, a flash (BarrageBurst) with them.
// Online the caller's machine tells every peer its kind, point and heading (barrage_net_protocol.h BRGS); every machine
// plans the same rounds from them (barrage_plan.h Seed) and fires them on its copy of the caller; the damage counts on the
// caller's machine alone (ShellMake: OnlineShotCounts). The guided rockets' aims are this machine's nearest enemies to the
// mark (each machine's own view of them).
#include "crew.h"
#include "barrage_net.h"
#include "barrage_net_protocol.h"
#include "barrage_plan.h"
#include "command_identity.h"   // SenderIsPlayer
#include "edf41classes.h"   // WeakRef
#include "heli.h"           // MapRay, VisitEnemiesOf
#include "memory.h"
#include "plugin_net.h"
#include "support_protocol.h"   // kExtBarrage
#include "support_soldier.h"    // ReadNativeObjectId
#include "support_warning.h"    // WarnBarrage
#include <cmath>
#include <cstring>

namespace crew {
int FaultLog(const char* where,const EXCEPTION_POINTERS* e) noexcept;   // jet.cpp
namespace {
using barrage::Kind;
using barrage::Round;
using barrage::kMaxRounds;

// RocketBullet01 (RTTI .?AVRocketBullet01@@): its update and what it does with its core (docs/plan-2026-10-25-strikes.md §1.1).
constexpr std::size_t kVtable=0x17A38E8,kSlotUpdate=5;
constexpr unsigned kUpdate=0x287E40,kCoreStepCall=0x287F4A,kBirthHit=0x231EEF;
const unsigned char kUpdateSig[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x81,0xEC,0xE0,0x00,0x00,0x00};
// 0x287F4A: mov rdx,rsi; mov rcx,rdi (the core, +0x140); call 0x235D50 (the core's step).
const unsigned char kCoreStepSig[]={0x48,0x8B,0xD6,0x48,0x8B,0xCF,0xE8,0xFB,0xDD,0xFA,0xFF};
const unsigned char kBirthHitSig[]={0xF3,0x0F,0x10,0x83,0x34,0x0A,0x00,0x00,0xF3,0x0F,0x59,0x83,0x30,0x0A,0x00,0x00};
constexpr std::size_t kCore=0x140;
constexpr std::size_t kMark=kCore+0xA34,kPos=kCore+0xB80,kVel=kCore+0xB90,kGravity=kCore+0xBA0;
constexpr std::size_t kLife=kCore+0xA08,kAge=kCore+0xAF8;
// The marks: kMarkBase | slot (slot < kSlots). Every stock AmmoHitSizeAdjust near 1.0 is exactly 1.0 (tools/calls.py
// THROW_MARK_BASE's scan); the thrown drones' and the gravity gun's weapon marks are under 0x3F801000.
constexpr std::uint32_t kMarkBase=0x3F801000u,kMarkMask=0xFFFFF000u;
constexpr int kSlots=0x1000;
constexpr int kMaxShows=6;
constexpr float kFrame=1.0f/60.0f;

using UpdateFn=void(__fastcall*)(void*,void*,void*,void*);
UpdateFn nextUpdate=nullptr;
bool hooked=false;

struct Show {
    bool on=false;
    Kind kind=Kind::katyusha;
    edf41c::WeakRef caller{};
    const unsigned char* by=nullptr;   // the caller while it lives (the rounds' owner)
    float damage=0.0f;
    ULONGLONG start=0;                 // the game frame of the call
    int n=0,next=0,end=0;
    Round rounds[kMaxRounds];
    int slot[kMaxRounds];
    bool refused=false;                // a round could not be fired: said once
};
Show shows[kMaxShows];
// What each slot's mark stands for: the show and its round, and the game frame the round left.
struct Slot { std::int16_t show=-1,round=-1; ULONGLONG launched=0; };
Slot slots[kSlots];
int nextSlot=0;

int ClaimSlot(int show,int round) noexcept {
    for(int k=0;k<kSlots;++k) {
        const int i=(nextSlot+k)%kSlots;
        if(slots[i].show>=0)continue;
        slots[i]=Slot{static_cast<std::int16_t>(show),static_cast<std::int16_t>(round),GameFrame()};
        nextSlot=(i+1)%kSlots;
        return i;
    }
    return -1;
}

void Finish(int i,const char* why) noexcept {
    Show& s=shows[i];
    if(!s.on)return;
    for(auto& sl:slots)if(sl.show==i)sl=Slot{};
    Log("BARRAGE %s done (%s): %d of %d rounds fired",barrage::kKindNames[static_cast<int>(s.kind)],why,s.next,s.n);
    edf41c::WeakReset(s.caller);
    s.on=false;s.by=nullptr;
}

// The height of what stands at (x, z) (terrain or a roof), from well over the marked point's height down; `y` with none.
float Top(float x,float z,float y) noexcept {
    const float from[3]={x,y+2500.0f,z},to[3]={x,y-1500.0f,z};
    float hit[3];
    return MapRay(from,to,hit)>=0.0f ? hit[1] : y;
}

// The guided rockets' aims: the enemies of the caller's side within kGuidedReach of the mark, nearest it first.
struct Aims {
    float at[3];
    int n=0;
    float pts[barrage::kGuidedRounds][3];
    float d2[barrage::kGuidedRounds];
};
void SeeAim(void* ctx,const void*,const float* aim) {
    Aims& a=*static_cast<Aims*>(ctx);
    const float dx=aim[0]-a.at[0],dz=aim[2]-a.at[2],d2=dx*dx+dz*dz;
    if(d2>barrage::kGuidedReach*barrage::kGuidedReach)return;
    int i=a.n<barrage::kGuidedRounds ? a.n++ : barrage::kGuidedRounds;
    if(i==barrage::kGuidedRounds) {   // full: in place of the farthest, when nearer
        int farthest=0;
        for(int k=1;k<barrage::kGuidedRounds;++k)if(a.d2[k]>a.d2[farthest])farthest=k;
        if(d2>=a.d2[farthest])return;
        i=farthest;
    }
    std::memcpy(a.pts[i],aim,12);a.d2[i]=d2;
    for(int k=i;k>0 && a.d2[k]<a.d2[k-1];--k) {   // kept nearest first
        float t[3];std::memcpy(t,a.pts[k],12);std::memcpy(a.pts[k],a.pts[k-1],12);std::memcpy(a.pts[k-1],t,12);
        const float d=a.d2[k];a.d2[k]=a.d2[k-1];a.d2[k-1]=d;
    }
}

// Round `r` of show `i`: fired now from its launcher, marked with a slot of its own.
void Fire(int i,int r) noexcept {
    Show& s=shows[i];
    const Round& round=s.rounds[r];
    s.slot[r]=-1;
    if(!s.by)return;
    const int slot=ClaimSlot(i,r);
    if(slot<0) {
        if(!s.refused)Log("BARRAGE %s: every round mark in use: rounds dropped",barrage::kKindNames[static_cast<int>(s.kind)]);
        s.refused=true;
        return;
    }
    // Its first stretch: from its launcher toward where it is a few frames on (its first frame before the plugin flies it).
    float ahead[3];barrage::PathAt(round,4.0f,ahead);
    const float dir[3]={ahead[0]-round.from[0],ahead[1]-round.from[1],ahead[2]-round.from[2]};
    const float len=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
    const float aim[3]={round.from[0]+(len>1e-3f ? dir[0]/len : 0.0f)*500.0f,round.from[1]+(len>1e-3f ? dir[1]/len : 1.0f)*500.0f,
                        round.from[2]+(len>1e-3f ? dir[2]/len : 0.0f)*500.0f};
    std::uint32_t bits=kMarkBase|static_cast<std::uint32_t>(slot);
    float mark;std::memcpy(&mark,&bits,4);
    if(!BarrageFire(s.by,round.from,aim,s.damage*round.share,round.size,round.blast,mark)) {
        slots[slot]=Slot{};
        if(!s.refused)Log("BARRAGE %s: a round was not fired (EDF6VC_BARRAGE_ROUND.SGO not preloaded this mission, or the "
                          "IFC's fields not where the plugin writes them)",barrage::kKindNames[static_cast<int>(s.kind)]);
        s.refused=true;
        return;
    }
    s.slot[r]=slot;
    if(round.burst)BarrageBurst(s.by,round.from);
}

void CheckCaller(Show& s) noexcept {
    bool live=false;
    __try { live=edf41c::WeakAlive(s.caller); } __except(EXCEPTION_EXECUTE_HANDLER){live=false;}
    if(!live && s.by) {
        Log("BARRAGE %s: its caller is gone: no more rounds",barrage::kKindNames[static_cast<int>(s.kind)]);
        s.by=nullptr;
    }
}

// A marked round's flight (before the stock update steps it): its velocity onto its curve's next point, no gravity; a
// cluster's carrier put out at its split.
void Steer(unsigned char* b) noexcept {
    std::uint32_t bits;std::memcpy(&bits,b+kMark,4);
    if((bits&kMarkMask)!=kMarkBase)return;
    const Slot& sl=slots[bits&~kMarkMask];
    if(sl.show<0)return;   // its barrage is over: it flies on as it is
    const Show& s=shows[sl.show];
    const Round& r=s.rounds[sl.round];
    const float frame=static_cast<float>(GameFrame()-sl.launched)+1.0f;
    if(r.splitAt>=0 && frame>=static_cast<float>(r.splitAt)) {
        Put<std::int32_t>(b,kAge,At<std::int32_t>(b,kLife));   // the stock round expires without a blast
        return;
    }
    float want[3];barrage::PathAt(r,frame,want);
    const float* pos=reinterpret_cast<const float*>(b+kPos);
    float* vel=reinterpret_cast<float*>(b+kVel);
    float* gravity=reinterpret_cast<float*>(b+kGravity);
    for(int k=0;k<3;++k){vel[k]=(want[k]-pos[k])/kFrame;gravity[k]=0.0f;}
}

void __fastcall UpdateHook(void* b,void* a2,void* a3,void* a4) noexcept {
    __try { Steer(static_cast<unsigned char*>(b)); } __except(EXCEPTION_EXECUTE_HANDLER){}
    nextUpdate(b,a2,a3,a4);
}

// ---- online (barrage_net_protocol.h) ----
std::uint32_t origin=0,serial=0;   // this machine's barrages: its run, the last one's number in it
barrage_net::Heard heard;          // the peers' barrages played here
// This machine's player's barrage told to every peer that has the channel, its caller by its mission slot
// (net_object_ref.h, a fallen caller's too; a caller that is no player of this mission's: nobody else can find it, nothing
// sent).
void Tell(int kind,const unsigned char* caller,const float* at,const float* heading,float damage) noexcept {
    if(!plugin_net::Ready())return;
    if(!origin)origin=plugin_net::NewOrigin();
    barrage_net::Msg m;
    m.kind=kind;m.origin=origin;m.serial=++serial;m.damage=damage;
    std::memcpy(m.at,at,12);std::memcpy(m.heading,heading,12);
    bool id=false;
    __try { id=EncodeNetRef(caller,m.caller) && netref::IsPlayer(m.caller); } __except(EXCEPTION_EXECUTE_HANDLER){id=false;}
    unsigned char bytes[barrage_net::kWire];
    if(!id || !barrage_net::Encode(m,bytes,sizeof(bytes))) {
        Log("BARRAGE NET %s: not told to the peers (%s)",barrage::kKindNames[kind],id ? "it does not encode" : "the caller is no player of this mission's");
        return;
    }
    const std::uint32_t sent=plugin_net::Broadcast(support_net::kExtBarrage,bytes,sizeof(bytes));
    Log("BARRAGE NET %s %08X:%u told to %u peer(s)",barrage::kKindNames[kind],m.origin,m.serial,sent);
}
}  // namespace

BarrageResult BarrageStart(int kind,const unsigned char* caller,const float* at,const float* heading,float damage) noexcept {
    if(kind<0 || kind>=barrage::kKinds || !caller || !at || !heading || !std::isfinite(at[0]+at[1]+at[2]+heading[0]+heading[2])) {
        Log("BARRAGE not started: no kind / caller / target");
        return BarrageResult::noTarget;
    }
    if(!hooked) {
        Log("BARRAGE not started: the rockets' flight hook is not in (unexpected EDF.dll code)");
        return BarrageResult::noHook;
    }
    int i=-1;
    for(int k=0;k<kMaxShows;++k)if(!shows[k].on){i=k;break;}
    if(i<0) {
        Log("BARRAGE not started: %d barrages fly already (the most at once)",kMaxShows);
        return BarrageResult::full;
    }
    Show& s=shows[i];
    s=Show{};
    s.kind=static_cast<Kind>(kind);
    edf41c::WeakSet(s.caller,caller);
    if(!edf41c::WeakAlive(s.caller)) {
        edf41c::WeakReset(s.caller);
        Log("BARRAGE not started: its caller is gone");
        return BarrageResult::noCaller;
    }
    s.by=caller;
    s.damage=std::isfinite(damage) && damage>0.0f ? damage : 0.0f;
    barrage::Setup setup;
    std::memcpy(setup.target,at,12);
    setup.target[1]=Top(at[0],at[2],at[1]);
    std::memcpy(setup.heading,heading,12);
    setup.seed=barrage::Seed(at,heading,kind);
    setup.worldHalf=HavokHalf()<WorldHalf() ? HavokHalf() : WorldHalf();
    // What the plan is made from (online every machine plans its own copy from the call it heard: these must agree from
    // machine to machine for the rounds to). The guided rockets' aims are each machine's own view of the enemies: each is
    // logged below, and the round count in the line after the plan.
    Log("BARRAGE %s plan from: seed %016llX at (%.3f, %.3f, %.3f) ground %.3f heading (%.4f, %.4f, %.4f) world half %.1f "
        "damage %.1f",barrage::kKindNames[kind],static_cast<unsigned long long>(setup.seed),at[0],at[1],at[2],setup.target[1],
        heading[0],heading[1],heading[2],setup.worldHalf,s.damage);
    Aims aims;
    if(s.kind==Kind::guided) {
        std::memcpy(aims.at,setup.target,12);
        __try { VisitEnemiesOf(At<std::int32_t>(caller,kTeam),&SeeAim,&aims); } __except(EXCEPTION_EXECUTE_HANDLER){aims.n=0;}
        setup.aims=aims.n;setup.aim=aims.pts;
        for(int a=0;a<aims.n;++a)Log("BARRAGE guided aim %d: (%.1f, %.1f, %.1f)",a,aims.pts[a][0],aims.pts[a][1],aims.pts[a][2]);
    }
    s.n=barrage::Plan(s.kind,setup,[](float x,float z,float y){return Top(x,z,y);},s.rounds,kMaxRounds);
    // In launch order (the creeping barrage's and the MRSI's are planned by where they land).
    for(int a=1;a<s.n;++a)for(int b=a;b>0 && s.rounds[b].launch<s.rounds[b-1].launch;--b) {
        const Round t=s.rounds[b];s.rounds[b]=s.rounds[b-1];s.rounds[b-1]=t;
    }
    s.end=barrage::EndFrame(s.rounds,s.n);
    s.start=GameFrame();
    s.on=s.n>0;
    if(!s.on){edf41c::WeakReset(s.caller);s.by=nullptr;}
    Log("BARRAGE %s at (%.0f, %.0f, %.0f): %d rounds over %.1f s, damage %.0f a round%s",barrage::kKindNames[kind],setup.target[0],
        setup.target[1],setup.target[2],s.n,static_cast<float>(s.end)/60.0f,s.damage,
        s.kind==Kind::guided ? (aims.n ? ", aimed at enemies" : ", no enemy near: round the mark") : "");
    if(!s.on)return BarrageResult::noRounds;
    WarnBarrage(kind,setup.target,heading,s.end);
    if(IsPlayer(caller))Tell(kind,caller,at,heading,s.damage);   // this machine's player's: every peer plays it too
    return BarrageResult::started;
}

void BarrageTick() noexcept {
    for(int i=0;i<kMaxShows;++i) {
        Show& s=shows[i];
        if(!s.on)continue;
        CheckCaller(s);
        const int frame=static_cast<int>(GameFrame()-s.start);
        while(s.next<s.n && s.rounds[s.next].launch<=frame)Fire(i,s.next++);   // in launch order (BarrageStart)
        if(frame>=s.end)Finish(i,"its time");
    }
}

void ResetBarrages() noexcept {
    for(auto& s:shows){if(s.on)edf41c::WeakReset(s.caller);s=Show{};}
    for(auto& sl:slots)sl=Slot{};
    nextSlot=0;
    origin=plugin_net::NewOrigin();serial=0;heard.Clear();
}

// ---- the channel (plugin_net.h: BRGS, support_protocol.h kExtBarrage) ----
namespace {
int netLogs=0;
bool ReceiveBarrageNetwork(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    barrage_net::Msg m;
    if(!barrage_net::Decode(bytes,size,m)) {
        if(netLogs++<16)Log("BARRAGE NET peer %u: a message that does not read (%zu bytes): dropped",peer,size);
        return true;
    }
    // The caller must be the sender's own player (a peer never starts a barrage for another machine's): found here, fallen
    // too, by its PUID; not here, by its slot's member (SenderOwns).
    NetResolved who{};
    bool sender=false;
    __try { sender=ResolveNetRef(m.caller,NetLife::present,&who) && SenderOwns(who,puid); } __except(EXCEPTION_EXECUTE_HANDLER){sender=false;}
    if(!sender) {
        if(netLogs++<16)Log("BARRAGE NET peer %u: barrage %08X:%u names a caller that is not its sender's player: dropped",peer,
                            m.origin,m.serial);
        return true;
    }
    if(!heard.First(m.origin,m.serial))return true;
    // Its rounds are fired by its caller's copy here (their damage zeroed: jet_bay.cpp ShellMake, its team the guided aims'):
    // a barrage is its rounds alone, so without that copy (left, deleted) there is nothing of it to play.
    if(!who.found) {
        Log("BARRAGE NET peer %u's barrage %08X:%u (%s): its caller is not here: not played",peer,m.origin,m.serial,
            barrage::kKindNames[m.kind]);
        return true;
    }
    const BarrageResult got=BarrageStart(m.kind,static_cast<const unsigned char*>(who.obj.obj),m.at,m.heading,m.damage);
    Log("BARRAGE NET peer %u's barrage %08X:%u (%s): %s here",peer,m.origin,m.serial,barrage::kKindNames[m.kind],
        got==BarrageResult::started ? "played" : "not played");
    return true;
}
void ResetBarrageNetwork() noexcept {}
}  // namespace

plugin_net::Channel BarrageNetChannel() noexcept {
    plugin_net::Channel c;
    c.name="barrage";c.magic=barrage_net::kMagic;c.version=barrage_net::kVersion;c.featureBit=support_net::kExtBarrage;
    c.minSize=c.maxSize=barrage_net::kWire;
    c.receive=&ReceiveBarrageNetwork;c.reset=&ResetBarrageNetwork;
    return c;
}

bool InstallBarrages() noexcept {
    __try {
        if(!Matches(kUpdate,kUpdateSig,sizeof(kUpdateSig)) || !Matches(kCoreStepCall,kCoreStepSig,sizeof(kCoreStepSig)) ||
           !Matches(kBirthHit,kBirthHitSig,sizeof(kBirthHitSig))) {
            Log("HOOK barrages=0 (unexpected EDF.dll code: the barrage calls stay stock bombers)");
            return false;
        }
        const auto slot=reinterpret_cast<void**>(image+kVtable)+kSlotUpdate;
        void* const current=*slot;
        if(current!=image+kUpdate)Log("BARRAGE rocket update: chaining onto %p (another plugin)",current);
        nextUpdate=reinterpret_cast<UpdateFn>(current);
        hooked=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&UpdateHook));
        Log("HOOK barrages=%d (the long-range barrages' rounds flown by the plugin)",hooked);
        return hooked;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
