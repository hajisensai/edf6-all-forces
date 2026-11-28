// The RuneBlade's melee and special arms at run time (runeblade_melee.h; what each arm does: runeblade_melee_logic.h; the
// design, the facts it stands on and what is still to make: docs/newclass-re/runeblade-melee-plan.md).
//  - The frame: AssultSoldier's live vtable slot 5 chained (edf::ChainVtableSlot, after vehicler_foot.cpp's if that came
//    first). The clones copy every slot they do not own from the live table before each object is made (soldier_factory.cpp),
//    so the hook is the RuneBlade's too. Only the local player's RuneBlade (its vptr the RuneBlade clone's, and
//    PlayerHuman) is stepped, after the stock frame: every other soldier passes through untouched.
//  - The facts read (docs/fp-soldier-re.md §3-4, docs/gravity-re.md §6.3, docs/npc-mobility-re.md §2): the position +0x90,
//    the smoothed look +0x1240 (pitch) / +0x1244 (yaw), the trigger +0xD70, frames off the ground +0x1264 (> 0: in the
//    air), the weapons +0x1950 (count +0x1960; the one in hand has +0x13E set), a weapon's held secondary trigger +0x13C and
//    its AmmoDamage +0x89C; the arms of the slots from the game status (wequip::ReadEquipped, player 0, class 5).
//  - What it does: the strikes become area hits (edf41c::Blast: a still GrenadeBullet01 of the strike's radius, owned by the
//    player, so the kill and its credit are theirs), the velocity is written to +0x6B0, the ether paid (runeblade_ether.h),
//    the bolts and Firefly's rounds fired as LaserBullet02 (edf41c::Fire).
//  - Online: the player's machine deals the damage (online_authority.h ShotCounts: a round of a player of this machine counts
//    here); every strike, ring and bolt goes to every peer (ZWRM), who shows it with damage 0 owned by that player's copy.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "runeblade_melee.h"
#include "runeblade_melee_logic.h"
#include "runeblade_melee_net.h"
#include "runeblade_ether.h"
#include "command_identity.h"
#include "config.h"
#include "edf41classes.h"
#include "edf_layout.h"
#include "heli.h"
#include "memory.h"
#include "net_object_resolve.h"
#include "netprobe.h"
#include "plugin_log.h"
#include "scene.h"
#include "soldier_classes_logic.h"
#include "soldier_factory.h"
#include "weapon_equip.h"
#include "edf/patch.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crew {
namespace {
namespace rb=runeblade;
using FrameFn=void(__fastcall*)(void*,void*,void*,void*);

// AssultSoldier (vehicler_foot_logic.h kSoldierVtable / kFrameSlot / kStockFrame, the same slot).
constexpr std::size_t kSoldierVtable=0x17CDF28,kFrameSlot=5,kStockFrame=0x550C30;
const unsigned char kFrameHead[]={0x40,0x53,0x48,0x81,0xEC,0xE0,0x00,0x00,0x00,0x48,0x8B,0xD9};
// The soldier and its weapons (the head comment).
constexpr std::size_t kLook=0x1240,kTrigger=0xD70,kAirFrames=0x1264,kVel=0x6B0,kWeapons=0x1950,kWeaponCount=0x1960;
constexpr std::size_t kInHand=0x13E,kSecondHeld=0x13C,kAmmoDamage=0x89C;
constexpr std::uint64_t kWeaponsMost=8;
constexpr int kEquipEvery=30;            // frames between the game status' slot reads
constexpr float kSeek=150.0f;            // R3's bolts: enemies this near
constexpr float kBoltSpeed=4.0f,kBoltSize=1.2f,kFireflySpeed=6.0f,kFireflySize=0.6f;
constexpr int kBoltLife=60,kFireflyLife=40;
constexpr int kRemoteRings=8;

void* nextFrame=nullptr;
bool installed=false;

// ---- the local RuneBlade ----
struct Local {
    const void* soldier=nullptr;
    rb::Machine m;
    rb::Queue<32> q;
    rb::Marks marks;
    rb::V3 markAt[rb::kMarksMost]{};
    rb::Arm slot[3]{};
    int equipIn=0;
    bool blastFailed=false;
};
Local me;

// ---- the peers' (shown) ----
struct RemoteRing { bool used=false; int slot=-1,frame=0; rb::V3 from{}; float yaw=0.0f; };
rb::Queue<64> remoteQ;
RemoteRing rings[kRemoteRings];

void* PlayerOfSlot(int slot) noexcept {
    unsigned char ref[32];
    if(slot<0 || !netref::MakePlayer(slot,ref))return nullptr;
    NetResolved r{};
    return ResolveNetRef(ref,NetLife::live,&r) && r.found ? const_cast<void*>(r.obj.obj) : nullptr;
}
void BlastAt(void* owner,const rb::Hit& h,float damage) noexcept {
    const float at[3]={h.at.x,h.at.y,h.at.z};
    if(!edf41c::Blast(owner,at,h.radius,damage) && !me.blastFailed) {
        me.blastFailed=true;
        Log("RUNEBLADE the area hit (GrenadeBullet01) was not made: the arms' hits do nothing");
    }
}
// LaserBullet02 needs its custom parameter ([mode] 0, edf41classes.cpp DevShoot).
alignas(4) const std::int32_t kPlainLaser[6]={0,1,12,1,0,0};
void FireBolt(void* owner,rb::V3 from,rb::V3 dir,float speed,float size,int life,float damage,bool firefly) noexcept {
    const float f[3]={from.x,from.y,from.z},d[3]={dir.x,dir.y,dir.z};
    alignas(16) float m[16];
    edf41c::AimMatrix(f,d,m);
    edf41c::Bullet b;
    b.factory=L"LaserBullet02";b.speed=speed;b.life=life;b.damage=damage;b.size=size;
    b.color[0]=firefly ? 1.0f : 0.6f;b.color[1]=firefly ? 0.85f : 0.4f;b.color[2]=firefly ? 0.4f : 1.0f;
    b.custom=reinterpret_cast<const edf41c::SgoNode*>(kPlainLaser);
    edf41c::Fire(owner,m,b);
}

// ---- the channel's sends ----
bool MyRef(const void* soldier,unsigned char out[32]) noexcept {
    return EncodeNetRef(soldier,out) && netref::IsPlayer(out);
}
void Send(const zwrb::Msg& m) noexcept {
    unsigned char bytes[zwrb::kWire];
    if(zwrb::Encode(m,bytes,sizeof(bytes)))plugin_net::Broadcast(bytes,sizeof(bytes));
}
void SendStrike(const void* soldier,const rb::Strike& s) noexcept {
    if(!InSession())return;
    zwrb::Msg m;m.kind=zwrb::Kind::strike;m.s=s;
    if(MyRef(soldier,m.player))Send(m);
}

// ---- reading the soldier ----
rb::Arm ArmOfRow(int row) noexcept {
    wequip::RowFacts facts;
    return row>=0 && wequip::ReadRow(row,&facts) ? rb::ArmOf(facts.id) : rb::Arm::none;
}
void ReadSlots() noexcept {
    std::array<int,wlist::kSlotsMost> rows{};
    if(!wequip::ReadEquipped(0,zwclass::kRuneBlade,&rows))return;
    for(int i=0;i<3;++i)me.slot[i]=ArmOfRow(rows[static_cast<std::size_t>(i)]);
}
// The weapon of slot `i` (the soldier's list in its slots' order), null when none.
const unsigned char* WeaponAt(const unsigned char* s,std::uint64_t i) noexcept {
    const auto count=At<std::uint64_t>(s,kWeaponCount);
    const auto list=At<const unsigned char* const*>(s,kWeapons);
    if(i>=count || count>kWeaponsMost || !list || !Readable(list,count*sizeof(void*)))return nullptr;
    const unsigned char* w=list[i];
    return w && Readable(w,kAmmoDamage+4) ? w : nullptr;
}
int SlotInHand(const unsigned char* s) noexcept {
    for(std::uint64_t i=0;i<3;++i){const unsigned char* w=WeaponAt(s,i);if(w && At<unsigned char>(w,kInHand))return static_cast<int>(i);}
    return -1;
}
float DamageOf(const unsigned char* w) noexcept {
    const float d=w ? At<float>(w,kAmmoDamage) : 0.0f;
    return std::isfinite(d) && d>0.0f ? d : 0.0f;
}

// ---- marks: the enemies of the player's side near a point / a line ----
struct MarkScan { rb::V3 a{},dir{}; float length=0.0f,radius=0.0f; };
void Mark(void* ctx,const void* object,const float* aim) noexcept {
    const auto* k=static_cast<const MarkScan*>(ctx);
    if(!object || !aim)return;
    const rb::V3 p{aim[0],aim[1],aim[2]};
    if(rb::SegmentDistance(p,k->a,k->dir,k->length)>k->radius)return;
    const int before=me.marks.count;
    if(me.marks.Add(reinterpret_cast<std::uintptr_t>(object)) && me.marks.count>before)me.markAt[before]=p;
}
struct Seek { rb::V3 from{}; const void* best[rb::kArraysMost]{}; rb::V3 at[rb::kArraysMost]{}; float d[rb::kArraysMost]{}; int n=0; };
void SeekNear(void* ctx,const void* object,const float* aim) noexcept {
    auto* k=static_cast<Seek*>(ctx);
    if(!object || !aim)return;
    const rb::V3 p{aim[0],aim[1],aim[2]};
    const float d=rb::Len(rb::Add(p,rb::Mul(k->from,-1.0f)));
    if(!(d<=kSeek))return;
    int i=k->n<rb::kArraysMost ? k->n++ : rb::kArraysMost-1;
    if(i==rb::kArraysMost-1 && k->n==rb::kArraysMost && d>=k->d[i])return;
    for(;i>0 && k->d[i-1]>d;--i){k->best[i]=k->best[i-1];k->at[i]=k->at[i-1];k->d[i]=k->d[i-1];}
    k->best[i]=object;k->at[i]=p;k->d[i]=d;
}

// ---- one frame of the local RuneBlade ----
void Drive(unsigned char* s) noexcept {
    if(s!=me.soldier){me=Local{};me.soldier=s;rb::Reset(me.m);}
    if(--me.equipIn<=0){ReadSlots();me.equipIn=kEquipEvery;}
    const int hand=SlotInHand(s);
    const unsigned char* const w=hand>=0 ? WeaponAt(s,static_cast<std::uint64_t>(hand)) : nullptr;
    rb::Frame f;
    f.inHand=hand>=0 && hand<3 ? me.slot[hand] : rb::Arm::none;
    f.ranged=me.slot[1];
    f.held=At<unsigned char>(s,kTrigger)!=0;
    f.second=w && At<unsigned char>(w,kSecondHeld)!=0;
    f.air=At<std::int32_t>(s,kAirFrames)>0;
    const float* p=reinterpret_cast<const float*>(s+kPosition);
    f.pos={p[0],p[1],p[2]};
    f.pitch=At<float>(s,kLook);f.yaw=At<float>(s,kLook+4);
    if(!std::isfinite(f.pitch) || !std::isfinite(f.yaw) || !std::isfinite(f.pos.x))return;
    f.base=DamageOf(w);
    f.rangedBase=DamageOf(WeaponAt(s,1));
    f.gauge=rb::ether::Snapshot(s);
    const rb::Out o=rb::Step(me.m,f);

    for(int i=0;i<o.strikes;++i)if(me.q.Push(o.strike[i],0))SendStrike(s,o.strike[i]);
    if(o.velocity){float* v=reinterpret_cast<float*>(s+kVel);v[0]=o.vel.x;v[1]=o.vel.y;v[2]=o.vel.z;}
    if(o.spend>0.0f)crew::runeblade::EtherSpend(s,o.spend);
    if(o.drain)crew::runeblade::EtherDrain(static_cast<void*>(s));
    const std::int32_t team=At<std::int32_t>(s,kTeam);
    if(o.ringThrown) {
        me.marks.Clear();
        if(InSession()){zwrb::Msg m;m.kind=zwrb::Kind::ring;m.s.arm=rb::Arm::stars;m.s.from=o.ringFrom;m.yaw=o.ringYaw;if(MyRef(s,m.player))Send(m);}
    }
    if(o.ringOut){MarkScan k{o.ringAt,{0.0f,0.0f,1.0f},0.0f,rb::kRingRadius};VisitEnemiesOf(team,&Mark,&k);}
    if(o.ringBack) {
        for(int i=0;i<me.marks.count;++i){const rb::Strike b=rb::MarkBurst(rb::Arm::stars,i,me.markAt[i],f.base);if(me.q.Push(b,0))SendStrike(s,b);}
        me.marks.Clear();
    }
    if(o.markLine && o.strikes) {
        me.marks.Clear();
        const rb::Strike& cut=o.strike[0];
        MarkScan k{cut.from,cut.dir,cut.length,cut.radius};
        VisitEnemiesOf(team,&Mark,&k);
        for(int i=0;i<me.marks.count;++i){const rb::Strike b=rb::MarkBurst(rb::Arm::afterglow,i,me.markAt[i],f.base);if(me.q.Push(b,0))SendStrike(s,b);}
        me.marks.Clear();
    }
    const rb::V3 look=rb::LookDir(f.pitch,f.yaw);
    auto bolt=[&](rb::Arm arm,rb::Effect fx,rb::V3 from,rb::V3 dir,float speed,float size,int life,float damage) {
        FireBolt(s,from,dir,speed,size,life,damage,fx==rb::Effect::firefly);
        if(!InSession())return;
        zwrb::Msg m;m.kind=zwrb::Kind::bolt;m.s.arm=arm;m.s.effect=fx;m.s.from=from;m.s.dir=dir;m.s.length=speed;m.s.radius=size;
        m.s.damage=damage;
        if(MyRef(s,m.player))Send(m);
    };
    if(o.firefly)bolt(rb::Arm::firefly,rb::Effect::firefly,rb::Add(rb::Add(f.pos,{0.0f,1.4f,0.0f}),look),look,kFireflySpeed,kFireflySize,
                      kFireflyLife,f.rangedBase);
    if(o.salvo>0) {
        Seek k;k.from=f.pos;
        VisitEnemiesOf(team,&SeekNear,&k);
        for(int i=0;i<o.salvo;++i) {
            const rb::V3 from=rb::Add(f.pos,rb::ArrayOffset(i,f.yaw));
            const rb::V3 dir=k.n ? rb::Unit(rb::Add(k.at[i%k.n],rb::Mul(from,-1.0f)),look) : look;
            bolt(rb::Arm::septenary,rb::Effect::bolt,from,dir,kBoltSpeed,kBoltSize,kBoltLife,f.base);
        }
    }
    me.q.Step([&](const rb::Hit& h){BlastAt(s,h,h.damage);});
}
bool DriveGuarded(unsigned char* s) noexcept {
    __try { Drive(s); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void __fastcall FrameHook(void* self,void* a,void* b,void* c) noexcept {
    reinterpret_cast<FrameFn>(nextFrame)(self,a,b,c);
    const void* vt=soldier::zwVtables[1];
    if(!vt || !self || *static_cast<const void* const*>(self)!=vt)return;
    auto* s=static_cast<unsigned char*>(self);
    if(s!=PlayerHuman() || !Cfg().enabled)return;
    static bool faulted=false;
    if(!DriveGuarded(s) && !faulted){faulted=true;Log("RUNEBLADE the arms' frame faulted (the soldier's layout is not what this reads)");}
}

// ---- the channel ----
void ShowGuarded(const rb::Hit& h) noexcept {
    __try { if(void* owner=PlayerOfSlot(static_cast<int>(h.owner)))BlastAt(owner,h,0.0f); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void Update(const CommandNetworkContext&,std::uint64_t) noexcept {
    for(RemoteRing& r:rings) {
        if(!r.used)continue;
        void* owner=PlayerOfSlot(r.slot);
        if(!owner || ++r.frame>rb::kRingFrames){r.used=false;continue;}
        if(r.frame%rb::kRingEvery)continue;
        const float* p=reinterpret_cast<const float*>(static_cast<unsigned char*>(owner)+kPosition);
        const rb::V3 at=rb::RingAt(r.from,{p[0],p[1],p[2]},r.yaw,r.frame);
        rb::Strike s;s.arm=rb::Arm::stars;s.effect=rb::Effect::ring;s.from=at;s.radius=rb::kRingRadius;
        remoteQ.Push(s,static_cast<std::uint32_t>(r.slot));
    }
    remoteQ.Step([](const rb::Hit& h){ShowGuarded(h);});
}
bool Receive(std::uint32_t,const char* puid,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    zwrb::Msg m;
    if(!zwrb::Decode(bytes,size,m))return true;
    MarkIdentities who{};
    if(!SenderIsPlayer(m.player,puid,&who) || who.slot<0 || !who.player)return true;
    switch(m.kind) {
    case zwrb::Kind::strike: m.s.damage=0.0f;remoteQ.Push(m.s,static_cast<std::uint32_t>(who.slot));break;
    case zwrb::Kind::ring:
        for(RemoteRing& r:rings)if(!r.used){r.used=true;r.slot=who.slot;r.frame=0;r.from=m.s.from;r.yaw=m.yaw;break;}
        break;
    case zwrb::Kind::bolt:
        FireBolt(const_cast<void*>(who.player.obj),m.s.from,m.s.dir,m.s.length,m.s.radius,
                 m.s.effect==rb::Effect::firefly ? kFireflyLife : kBoltLife,0.0f,m.s.effect==rb::Effect::firefly);
        break;
    }
    return true;
}
void ResetNet() noexcept {
    remoteQ.Clear();
    for(RemoteRing& r:rings)r.used=false;
}
}  // namespace

plugin_net::Channel RuneBladeMeleeNetChannel() noexcept {
    plugin_net::Channel c;
    c.name="runeblade melee";c.magic=zwrb::kMagic;c.version=zwrb::kVersion;
    c.minSize=zwrb::kWire;c.maxSize=zwrb::kWire;
    c.receive=&Receive;c.update=&Update;c.reset=&ResetNet;
    return c;
}

bool InstallRuneBladeMelee() noexcept {
    if(installed)return true;
    if(!Readable(image+kStockFrame,sizeof(kFrameHead)) || !Matches(kStockFrame,kFrameHead,sizeof(kFrameHead))) {
        Log("RUNEBLADE AssultSoldier's frame (EDF.dll+0x%zX) is not what this was written for: no RuneBlade arms",kStockFrame);
        return false;
    }
    auto** slot=reinterpret_cast<void**>(image+kSoldierVtable)+kFrameSlot;
    if(!edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&FrameHook),&nextFrame))return false;
    installed=true;
    Log("RUNEBLADE arms in: AssultSoldier frame chained (M1-M5, R2, R3)");
    return true;
}
}  // namespace crew
