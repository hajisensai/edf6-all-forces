// The Andryusha's incendiary cluster rockets (test site #37; README 安德柳沙; docs/andryusha-re.md; the pure part
// src/andryusha.h, checked offline by tools/andryusha_check.cpp). The truck is the Katyusha's (tools/make_katyusha.py:
// its model, its pose, its HUD and EDF6AutoTurret's aim all ride kMarkKatyusha); its weapon fires carriers that split:
//  - Which rounds. Its rockets are the Katyusha's GrenadeBullet01 with AmmoHitSizeAdjust set to one of the two marks
//    (andryusha.h kMarkImpactBits / kMarkAirburstBits). The core copies it at the round's birth (core +0xA34, read at
//    0x231EEF, barrage.cpp's mark; H), so every carrier names the fuse it was fired with. The weapon's template InitParam
//    is at weapon +0x830 (fire 0x696FD0 copies it to +0xA10 for each shot, docs/bullet-pass-re.md §3.3) with
//    AmmoHitSizeAdjust at its +0x94 (the IFC's InitParam at IFC +0x70 has it at IFC +0x104, docs/carrier-laser-re.md
//    §3; AmmoExplosion, AmmoSpeed and AmmoAlive sit at the same distances in both: weapon +0x8B0 / +0x894 / +0x898, IFC
//    +0xF0 / +0xD4 / +0xD8), so the rider's fuse switch writes weapon +0x8C4 (M: the layout holds for four fields, the
//    write's effect on the next round is not seen in the game yet). The switch only ever writes over one of the two marks.
//  - Their flight. GrenadeBullet01's update (vtable 0x17A17E0 slot 5, 0x264AB0; EDF6AutoTurret hooks it too: both chain,
//    edf::ChainVtableSlot) is wrapped: a marked round's position, velocity and age are noted each update (Before), and
//    when an airburst carrier is due the stock round is told to burst this update where it is (flags |= 0x20 burst on
//    expiry, age = its life: autoturret/src/plugin.cpp Fuze's way, H). After the stock update, a carrier that died
//    bursting (hit something, or burst by its fuse) is queued with where it was.
//  - The game thread (AndryushaTick, SharedFrameTick): the airburst fuse's ground probe (a map ray under each falling
//    airburst carrier: the bullet updates may run off the game thread, the ray is not called there), and the queued
//    bursts split: each a set of bomblets (andryusha.h ImpactAims / AirburstAims) fired by the carrier's owner (core +0x9A8,
//    its weak count at +0x9B0: the vehicle) as the plugin's own rounds (jet_bay.cpp AndryushaBomblet). A burst whose owner
//    is gone (the truck destroyed while its rockets flew) has no shooter to fire them as: logged, not split.
//  - Online: every machine runs its own copy of a carrier and splits it alike (the bomblets' pattern seeded by where it
//    burst); the damage counts on one machine (jet_bay.cpp ShellMake: OnlineShotCounts). The fuse switch is this
//    machine's write to its copy of the weapon: a remote player's switch is not sent (docs/andryusha-re.md 待办).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "andryusha.h"
#include "heli.h"            // MapRay
#include "map.h"             // MapHoldsKeys
#include "memory.h"
#include "seat_switch_logic.h"
#include "edf/patch.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
using andryusha::Fuse;

// GrenadeBullet01: its vtable and update (autoturret/docs/re-notes.md "GrenadeBullet01"); the update's age step and
// expiry test, as EDF6AutoTurret checks them before it hooks (0x236888 / 0x236899).
constexpr unsigned kGrenadeVtable=0x17A17E0,kGrenadeUpdate=0x264AB0;
constexpr std::size_t kUpdateSlot=5;
const unsigned char kAgeStep[]={0x8B,0x8D,0xF8,0x0A,0x00,0x00,0xFF,0xC1};   // mov ecx,[rbp+AF8] / inc ecx
const unsigned char kExpiry[]={0x3B,0x8D,0x08,0x0A,0x00,0x00};             // cmp ecx,[rbp+A08]
// The round's core (+0x140) fields.
constexpr std::size_t kCore=0x140,kWeakCtrl=0x30;
constexpr std::size_t kMark=kCore+0xA34,kFlags=kCore+0xAF4,kAge=kCore+0xAF8,kLife=kCore+0xA08,kPos=kCore+0xB80,kVel=kCore+0xB90;
constexpr std::size_t kDamage=kCore+0xA0C,kOwner=kCore+0x9A8,kOwnerCtrl=kCore+0x9B0;
constexpr std::uint32_t kDead=0x1,kBurstOnExpiry=0x20;
// The weapon's template AmmoHitSizeAdjust (see the top).
constexpr std::size_t kWeaponHitAdjust=0x8C4;
constexpr std::size_t kSeatPad=seatsw::kSeatPad,kSeatButtons=seatsw::kSeatButtons;

using UpdateFn=void(__fastcall*)(void*,void*);
void* nextUpdate=nullptr;   // set before the slot is written (edf::ChainVtableSlot): the game may call the hook at once
bool hooked=false;

// The carriers in flight, by address and weak-this control block (a new round at a freed one's address is another one).
struct Carrier {
    const void* bullet=nullptr;
    const void* ctrl=nullptr;
    const void* owner=nullptr;
    const void* ownerCtrl=nullptr;
    Fuse fuse=Fuse::none;
    float pos[3]{},vel[3]{};
    float damage=0.0f;
    std::int32_t age=0;
    ULONGLONG seen=0;          // the game frame of its last update
    bool due=false;            // the airburst fuse found it low enough (AndryushaTick): it bursts at its next update
    bool fused=false;          // it was told to burst (its burst is ours, not its impact)
};
constexpr int kMaxCarriers=128;   // 16 a salvo, 25 s at most in the air: a few trucks' worth
Carrier carriers[kMaxCarriers];
// Bursts waiting to split (AndryushaTick drains them on the game thread).
struct Burst { const void* owner; const void* ownerCtrl; Fuse fuse; float at[3]; float damage; };
constexpr int kMaxBursts=64;
Burst bursts[kMaxBursts];
int burstCount=0;
SRWLOCK lock=SRWLOCK_INIT;
int fullSaid=0,droppedSaid=0,ownerGoneSaid=0,unreadySaid=0;

Carrier* Find(const void* b,const void* ctrl,bool make) noexcept {
    Carrier* empty=nullptr;
    for(auto& c:carriers) {
        if(c.bullet==b && c.ctrl==ctrl)return &c;
        if(!c.bullet && !empty)empty=&c;
    }
    return make ? empty : nullptr;
}

// Before the stock update of a round: ours (marked, alive) or not; an airburst carrier found due is told to burst now.
bool Before(unsigned char* b) noexcept {
    std::uint32_t bits;std::memcpy(&bits,b+kMark,4);
    const Fuse fuse=andryusha::FuseOf(bits);
    if(fuse==Fuse::none)return false;
    const auto flags=At<std::uint32_t>(b,kFlags);
    if(flags&kDead)return false;
    const void* const ctrl=At<const void*>(b,kWeakCtrl);
    AcquireSRWLockExclusive(&lock);
    Carrier* c=Find(b,ctrl,true);
    if(c && !c->bullet) {   // its first update
        *c=Carrier{};
        c->bullet=b;c->ctrl=ctrl;c->fuse=fuse;
        c->owner=At<const void*>(b,kOwner);c->ownerCtrl=At<const void*>(b,kOwnerCtrl);
        c->damage=At<float>(b,kDamage);
    }
    if(c) {
        if(c->due && !c->fused) {
            Put<std::uint32_t>(b,kFlags,flags|kBurstOnExpiry);
            Put<std::int32_t>(b,kAge,At<std::int32_t>(b,kLife));
            c->fused=true;
        }
        c->seen=GameFrame();
    } else if(fullSaid<3) {
        ++fullSaid;
        Log("ANDRYUSHA more than %d carriers in flight: this one keeps its impact fuse and does not split",kMaxCarriers);
    }
    ReleaseSRWLockExclusive(&lock);
    return c!=nullptr;
}

// After it: its state noted; dead now and burst (it hit something, or its fuse burst it): queued to split there. One that
// only ran out of life (no burst) is let go.
void After(unsigned char* b) noexcept {
    const void* const ctrl=At<const void*>(b,kWeakCtrl);
    const auto flags=At<std::uint32_t>(b,kFlags);
    const auto age=At<std::int32_t>(b,kAge),life=At<std::int32_t>(b,kLife);
    AcquireSRWLockExclusive(&lock);
    if(Carrier* c=Find(b,ctrl,false)) {
        std::memcpy(c->pos,b+kPos,12);std::memcpy(c->vel,b+kVel,12);c->age=age;
        if(flags&kDead) {
            const bool burst=c->fused || age<life;
            if(burst && burstCount<kMaxBursts) {
                Burst& q=bursts[burstCount++];
                q.owner=c->owner;q.ownerCtrl=c->ownerCtrl;q.fuse=c->fused ? Fuse::airburst : Fuse::impact;
                std::memcpy(q.at,c->pos,12);q.damage=c->damage;
            } else if(burst && droppedSaid<3) {
                ++droppedSaid;
                Log("ANDRYUSHA more than %d bursts in one frame: one does not split",kMaxBursts);
            }
            *c=Carrier{};
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

void __fastcall UpdateHook(void* b,void* ctx) {
    bool ours=false;
    __try { ours=Before(static_cast<unsigned char*>(b)); } __except(EXCEPTION_EXECUTE_HANDLER){ours=false;}
    reinterpret_cast<UpdateFn>(nextUpdate)(b,ctx);
    if(!ours)return;
    __try { After(static_cast<unsigned char*>(b)); } __except(EXCEPTION_EXECUTE_HANDLER){}
}

bool OwnerAlive(const void* ctrl) noexcept {
    return ctrl && Readable(ctrl,0x10) && At<std::int32_t>(ctrl,0x8)>0;
}

float Ground(void*,float x,float z,float y) noexcept {
    const float from[3]={x,y+200.0f,z},to[3]={x,y-300.0f,z};
    float hit[3];
    return MapRay(from,to,hit)>=0.0f ? hit[1] : y;
}

// One burst split into its bomblets (andryusha.h), each fired by the carrier's owner.
void Split(const Burst& q) noexcept {
    if(!OwnerAlive(q.ownerCtrl)) {
        if(ownerGoneSaid<3){++ownerGoneSaid;Log("ANDRYUSHA burst at (%.0f,%.0f,%.0f): its truck is gone, no bomblets",q.at[0],q.at[1],q.at[2]);}
        return;
    }
    float from[3],aims[andryusha::kBomblets][3];
    const std::uint64_t seed=andryusha::SeedAt(q.at);
    if(q.fuse==Fuse::airburst) {
        const float down[3]={q.at[0],q.at[1]-andryusha::kBurstHeight*3.0f,q.at[2]};
        float below[3];
        if(MapRay(q.at,down,below)<0.0f){below[0]=q.at[0];below[1]=q.at[1]-andryusha::kBurstHeight;below[2]=q.at[2];}
        andryusha::AirburstAims(q.at,below,seed,&Ground,nullptr,from,aims);
    } else andryusha::ImpactAims(q.at,seed,&Ground,nullptr,from,aims);
    const auto by=static_cast<const unsigned char*>(q.owner);
    int fired=0;
    for(auto& aim:aims)if(AndryushaBomblet(by,from,aim,q.damage*andryusha::kBlastShare,q.damage*andryusha::kFireShare))++fired;
    if(!fired && unreadySaid<3) {
        ++unreadySaid;
        Log("ANDRYUSHA burst: no bomblet fired (EDF6VC_ANDRYUSHA_BLAST / _FIRE.SGO not preloaded this mission: rerun the installer)");
    } else if(Cfg().debug)
        Log("ANDRYUSHA %s burst at (%.0f,%.0f,%.0f) by %p: %d bomblets, %.0f / %.0f damage each",q.fuse==Fuse::airburst ? "airburst" :
            "impact",q.at[0],q.at[1],q.at[2],q.owner,fired,q.damage*andryusha::kBlastShare,q.damage*andryusha::kFireShare);
}

bool KeyDown(int vk) noexcept {   // payload.cpp KeyDown
    if(vk<=0 || MapHoldsKeys())return false;
    DWORD pid=0;
    GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (edf::AsyncKey(vk)&0x8000)!=0;
}
// The switch's key or pad chord (VehicleSecondarySwitchKey / -Button: the Naegling's class has no own secondary to
// cycle, payload.cpp Switch, so the key is free on this truck), held last frame.
const void* switchVehicle=nullptr;
bool switchHeld=false;
}  // namespace

void AndryushaTick() noexcept {
    if(!hooked)return;
    const ULONGLONG frame=GameFrame();
    // The airburst probes: copies taken under the lock, the rays outside it.
    struct Probe { const void* bullet; const void* ctrl; float pos[3]; float vy; std::int32_t age; };
    Probe probes[kMaxCarriers];
    int n=0;
    Burst drained[kMaxBursts];
    int nb=0;
    AcquireSRWLockExclusive(&lock);
    for(auto& c:carriers) {
        if(!c.bullet)continue;
        if(c.seen+2<frame){c=Carrier{};continue;}   // not updated for two frames: gone without a burst we saw
        if(c.fuse==Fuse::airburst && !c.due && c.vel[1]<0.0f)
            probes[n++]=Probe{c.bullet,c.ctrl,{c.pos[0],c.pos[1],c.pos[2]},c.vel[1],c.age};
    }
    nb=burstCount;
    std::memcpy(drained,bursts,sizeof(Burst)*static_cast<std::size_t>(nb));
    burstCount=0;
    ReleaseSRWLockExclusive(&lock);
    for(int i=0;i<n;++i) {
        const Probe& p=probes[i];
        const float to[3]={p.pos[0],p.pos[1]-andryusha::ProbeDepth(p.vy),p.pos[2]};
        float hit[3];
        const float height=MapRay(p.pos,to,hit)>=0.0f ? p.pos[1]-hit[1] : -1.0f;
        if(!andryusha::AirburstDue(height,p.vy,p.age))continue;
        AcquireSRWLockExclusive(&lock);
        if(Carrier* c=Find(p.bullet,p.ctrl,false))c->due=true;
        ReleaseSRWLockExclusive(&lock);
    }
    for(int i=0;i<nb;++i)Split(drained[i]);
}

int AndryushaSeat(const unsigned char* v,const unsigned char* seat,unsigned char* weapon) noexcept {
    std::uint32_t bits=0;
    if(!weapon || !Readable(weapon+kWeaponHitAdjust,4))return 0;
    std::memcpy(&bits,weapon+kWeaponHitAdjust,4);
    const Fuse fuse=andryusha::FuseOf(bits);
    if(fuse==Fuse::none)return 0;
    if(switchVehicle!=v){switchVehicle=v;switchHeld=true;}   // a key held while boarding is no press
    const bool keys=At<unsigned char>(seat,kSeatPad)==0;
    const int vk=Cfg().vehicleSecondarySwitchKey,mask=Cfg().vehicleSecondarySwitchButton;
    std::uint16_t pad=0;std::uint8_t lt=0,rt=0;
    const bool down=keys ? KeyDown(vk) : mask>0 && !MapHoldsKeys() && MapPadState(&pad,&lt,&rt) && (pad&mask)==mask;
    const bool press=down && !switchHeld;
    switchHeld=down;
    if(!press)return static_cast<int>(fuse);
    const Fuse next=andryusha::Toggled(fuse);
    const std::uint32_t mark=andryusha::MarkOf(next);
    std::memcpy(weapon+kWeaponHitAdjust,&mark,4);
    Log("ANDRYUSHA v=%p fuse: %s",v,next==Fuse::airburst ? "airburst (proximity)" : "impact");
    return static_cast<int>(next);
}

void ResetAndryusha() noexcept {
    AcquireSRWLockExclusive(&lock);
    for(auto& c:carriers)c=Carrier{};
    burstCount=0;
    ReleaseSRWLockExclusive(&lock);
    switchVehicle=nullptr;switchHeld=false;
}

bool InstallAndryusha() noexcept {
    __try {
        if(!Matches(0x236888,kAgeStep,sizeof(kAgeStep)) || !Matches(0x236899,kExpiry,sizeof(kExpiry))) {
            Log("HOOK andryusha=0 (GrenadeBullet01's expiry is not as known: the Andryusha's rockets do not split)");
            return false;
        }
        const auto slot=reinterpret_cast<void**>(image+kGrenadeVtable)+kUpdateSlot;
        if(*slot!=image+kGrenadeUpdate)Log("ANDRYUSHA grenade update: chaining onto %p (EDF6AutoTurret's fuses)",*slot);
        hooked=edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&UpdateHook),&nextUpdate);
        Log("HOOK andryusha=%d (the Andryusha's carriers: fuses and bomblets)",hooked);
        return hooked;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
