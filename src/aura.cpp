// The aura vehicles (README 光环载具, docs/feature-aura-vehicles.md; ini Aura*; the rules aura_logic.h). Three trucks
// built on V512_KEITRUCK (tools/make_aura_vehicles.py), each told by its model's marker bone (aura::kMarkers): the
// command truck (attack), the repair truck (HP back), the ammo truck (rounds back). An aura stands while its truck is
// whole and driven (a player or an NPC in seat 0) or parked; its friends (the player's side: soldiers, NPCs, vehicles)
// within AuraRadius take it, one kind never stacking (the strongest stands).
//  - Attack: the native per-frame dealt multiplier obj+0x390 (0x54BE8D reads it, 0x54BEE5 makes +0x398 = +0x390 x
//    +0x388, 0x54BF13 sets it back to 1; the soldier's round reads +0x398 at 0x59B536, a vehicle's at 0x6303C4), raised
//    to the bonus as the native support devices raise it (a max, 0x2AC8CA). Written on every copy: the shooter's own
//    machine is the one whose rounds count.
//  - Repair: HP (+0x2F8, within +0x2F0..+0x2F4) on the machine that owns the friend (proteus_field.inc's rule); online
//    what was given also goes into the native HP-diff accumulator +0x5B4 (0x54AA06 adds a damage event's diff there,
//    slot 40 sends it at 0x54CD06), so every copy gets it.
//  - Ammo: each weapon's magazine (+0xBE8 of +0x248) or its reload under way (+0xE68 of +0x20C frames: 0x693F3B counts
//    an auto reload down, 0x693F41 completes it at 0) hurried by the share; not the points reloads (ReloadType 2).
//    Weapon state is each machine's own: written on every copy.
// Online the host decides which auras stand and sends them (AFAU, state stream auraVehicles, every second and at a
// change); every machine applies them. All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "aura.h"
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "body506.h"
#include "heli.h"
#include "vhud.h"
#include "online_authority.h"
#include "plugin_net.h"
#include "command_net.h"
#include "lazy_rows.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
struct Sig { unsigned rva; unsigned char bytes[20]; std::size_t size; };
const Sig kSigs[]={
    {0x5E11D0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57},16},   // the team walk
    {0x54BE8D,{0xF3,0x0F,0x10,0x91,0x90,0x03,0x00,0x00},8},                                            // reads +0x390
    {0x54BEE5,{0xF3,0x0F,0x59,0x97,0x88,0x03,0x00,0x00,0xF3,0x0F,0x11,0x97,0x98,0x03,0x00,0x00},16},   // +0x398 = x +0x388
    {0x54BF13,{0xC7,0x87,0x90,0x03,0x00,0x00,0x00,0x00,0x80,0x3F},10},                                 // +0x390 = 1
    {0x59B536,{0xF3,0x0F,0x59,0x8B,0x98,0x03,0x00,0x00},8},                                            // a soldier's round
    {0x547870,{0x80,0xB9,0xE8,0x02,0x00,0x00,0x00,0x75,0x20,0xF3,0x0F,0x58,0x89,0xF8,0x02,0x00,0x00,0xF3,0x0F,0x5D},20},   // heal
    {0x54AA06,{0xF3,0x0F,0x58,0x83,0xB4,0x05,0x00,0x00},8},                                            // the HP diff
    {0x54CD06,{0xF3,0x0F,0x2C,0x9E,0xB4,0x05,0x00,0x00},8},                                            // ...sent
    {0x693F3B,{0xFF,0x8E,0x68,0x0E,0x00,0x00},6},                                                      // auto reload
    {0x693F41,{0x83,0xBE,0x68,0x0E,0x00,0x00,0x00},7},                                                 // ...done at 0
};
constexpr unsigned kTeamWalk=0x5E11D0,kTeamManager=0x20B2978;
using WalkFn=void(__fastcall*)(void*,std::int32_t,void*);
// The soldiers (proteus.cpp's): AssultSoldier, PaleWing, HeavyArmor, Engineer; one riding a vehicle (+0x1548) is
// covered by its vehicle.
constexpr unsigned kVtSoldiers[]={0x17CDF28,0x17D0FF8,0x17CF5B8,0x17CF100};
constexpr std::size_t kHumanVehicle=0x1548,kHumanWeapons=0x1950,kHumanWeaponCount=0x1960;
constexpr std::size_t kBuff=0x390,kHpLeast=0x2F0,kHpDiff=0x5B4;
constexpr std::size_t kReloadType=0x208,kReloadTime=0x20C,kCapacity=0x248,kReloadLeft=0xE68,kReloading=0xE6C;
constexpr std::int32_t kReloadAuto=1;
constexpr int kTrucks=64,kCarries=1024,kTeams=4;
constexpr ULONGLONG kGoneMs=2000,kSpeedMs=100,kRingMs=500;
constexpr ULONGLONG kCarryFrames=600;   // a weapon not filled this long: its fraction of a round forgotten
constexpr float kRingReach=400.0f;       // m: an aura's ring is drawn when its truck is this near the player
constexpr float kPi=3.14159265f;
static_assert(kAuraRing<=64,"a ring's rays every kRingMs");

struct Truck {
    ObjRef ref;
    int kind=-2;             // -2: its model not looked at yet; -1: no aura truck
    float pos[3]{};
    float at[3]{};           // where it was at `atMs` (its speed)
    ULONGLONG atMs=0,seen=0;
    float speed=-1.0f;       // m/s; -1: not measured yet
    bool driven=false;
    aura::StateMark mark;    // its last state line (LogStates)
};
LazyRows<Truck,kTrucks> trucks;   // heap from the first use (plugin_image_size_guard: no image table)
bool ok=false,netReady=false;
unsigned logs=0;
aura::Msg heard{},sentMsg{};
ULONGLONG heardAt=0,sentAt=0,lastMs=0,lastFrame=0;
bool sent=false;

struct Carry { const void* weapon; float carry; ULONGLONG frame; };
LazyRows<Carry,kCarries> carries;

SRWLOCK viewLock=SRWLOCK_INIT;
AuraView view{};
ULONGLONG ringAt=0;
unsigned stateLines=0;            // state lines this mission (aura::kStateLines)
bool hereLogged=false;            // the kinds reaching the player: last logged (Publish)
std::uint8_t hereKinds=0;
ULONGLONG hereAt=0;

const float* Pos(const void* o) noexcept { return reinterpret_cast<const float*>(static_cast<const unsigned char*>(o)+kPosition); }

bool Soldier(const void* o) noexcept {
    const void* const vt=At<const void*>(o,0);
    for(unsigned x:kVtSoldiers)if(vt==image+x)return true;
    return false;
}

// The aura kind of `v`'s model (its marker bone), -1 for none; -2 while its bones are not there yet.
int KindOf(const unsigned char* v) noexcept {
    if(!Readable(v,kModelInst506+kInstBoneCount+4))return -2;
    const unsigned char* const inst=v+kModelInst506;
    if(!At<const void*>(inst,kInstBones506) || At<std::int32_t>(inst,kInstBoneCount)<=0)return -2;
    for(int k=0;k<aura::kKinds;++k)if(BoneRecord506(inst,aura::kMarkers[k]))return k;
    return -1;
}

Truck* TruckOf(const unsigned char* v,ULONGLONG ms) noexcept {
    Truck* free=nullptr;
    for(Truck& t:trucks) {
        if(t.ref.Is(v))return &t;
        if(!free && (!t.ref || ms-t.seen>kGoneMs))free=&t;
    }
    if(!free)return nullptr;
    *free=Truck{};free->ref=ObjRef::Of(v);
    return free;
}

bool TruckLive(const Truck& t) noexcept {
    // Still this truck (deleted or dead too: what it wrote is undone on it until it is freed), objref.h.
    return objref::Identity(t.ref)>=objref::Life::removed && Readable(t.ref.obj,kHpMax+4);
}

float Strength(aura::Kind k,const Config& c) noexcept {
    return k==aura::Kind::command ? c.auraCommandAttack : k==aura::Kind::repair ? c.auraRepairShare : c.auraAmmoShare;
}

// The auras standing as this machine sees its trucks (the host's, or offline).
aura::Msg Standing(ULONGLONG ms) noexcept {
    aura::Msg m{};
    const Config& c=Cfg();
    for(const Truck& t:trucks) {
        if(m.count>=aura::kMaxSources)break;
        if(t.kind<0 || ms-t.seen>kGoneMs || !TruckLive(t))continue;
        const auto* v=static_cast<const unsigned char*>(t.ref.obj);
        if(!aura::Active(v[kDead]!=0,t.driven,t.speed,c.auraParkSpeed))continue;
        aura::Source s{};
        s.kind=static_cast<aura::Kind>(t.kind);
        std::memcpy(s.pos,t.pos,12);
        s.radius=c.auraRadius;s.strength=Strength(s.kind,c);s.team=player.team;
        if(s.radius>0.0f && s.strength>0.0f && aura::SourceOk(s))m.sources[m.count++]=s;
    }
    return m;
}

void SendState(const aura::Msg& m,ULONGLONG ms) noexcept {
    if(!aura::Due(sent,sentMsg,sentAt,m,ms))return;
    unsigned char bytes[aura::kWire];
    if(!aura::Encode(m,bytes,sizeof(bytes)))return;
    plugin_net::BroadcastState(plugin_net::StateKey(plugin_net::StateStream::auraVehicles,0),bytes,sizeof(bytes));
    sent=true;sentMsg=m;sentAt=ms;
}

float* CarryOf(const void* w,ULONGLONG frame) noexcept {
    const std::uintptr_t h=(reinterpret_cast<std::uintptr_t>(w)>>4)*0x9E3779B97F4A7C15ull;
    Carry* free=nullptr;
    for(int i=0;i<16;++i) {
        Carry& e=carries[(h+static_cast<std::uintptr_t>(i))%kCarries];
        if(e.weapon==w){e.frame=frame;return &e.carry;}
        if(!free && (!e.weapon || frame-e.frame>kCarryFrames))free=&e;
    }
    if(!free)return nullptr;
    *free=Carry{w,0.0f,frame};
    return &free->carry;
}

// One weapon's rounds back: a reload under way hurried, else its magazine filled.
void Fill(unsigned char* w,float share,float dt,ULONGLONG frame) noexcept {
    if(!Readable(w,kReloadLeft+8,true))return;
    const std::int32_t type=At<std::int32_t>(w,kReloadType);
    if(!aura::Refillable(type))return;
    float* const carry=CarryOf(w,frame);
    if(!carry)return;
    const std::int32_t left=At<std::int32_t>(w,kReloadLeft),frames=At<std::int32_t>(w,kReloadTime);
    if(left>0 && frames>0 && (type==kReloadAuto || At<std::uint8_t>(w,kReloading)!=0)) {
        const std::int32_t done=frames-(left<frames ? left : frames);
        const std::int32_t next=aura::Refill(done,frames,share,dt,*carry);
        const std::int32_t rest=frames-next;
        Put<std::int32_t>(w,kReloadLeft,rest>1 ? rest : 1);   // the game completes it on its own count (0x693F41)
        return;
    }
    const std::int32_t cap=At<std::int32_t>(w,kCapacity),rounds=At<std::int32_t>(w,kWeaponAmmo);
    if(cap<=0 || rounds>=cap){*carry=0.0f;return;}
    Put<std::int32_t>(w,kWeaponAmmo,aura::Refill(rounds,cap,share,dt,*carry));
}

bool HolderAlive(const unsigned char* holder) noexcept {
    const auto ctrl=At<const unsigned char*>(holder,kHolderCtrl);
    return ctrl && Readable(ctrl,0x10) && At<std::int32_t>(ctrl,8)>0;
}

void FillAll(unsigned char* o,bool soldier,float share,float dt,ULONGLONG frame) noexcept {
    if(soldier) {
        const auto list=At<unsigned char* const*>(o,kHumanWeapons);const auto n=At<std::uint64_t>(o,kHumanWeaponCount);
        if(n && n<=16 && Readable(list,n*8))for(std::uint64_t i=0;i<n;++i)Fill(list[i],share,dt,frame);
        return;
    }
    const auto n=At<std::uint64_t>(o,kHolderCount);const auto base=At<unsigned char*>(o,kHolders);
    if(n && n<=16 && Readable(base,n*kHolderStride))for(std::uint64_t i=0;i<n;++i) {
        const auto h=base+i*kHolderStride;
        if(HolderAlive(h))Fill(At<unsigned char*>(h,kHolderWeapon),share,dt,frame);
    }
}

// The machine whose HP of `o` counts (proteus_field.inc FieldOwnedHere).
bool OwnedHere(const unsigned char* o) noexcept {
    if(!InSession())return true;
    const auto flags=At<std::uint16_t>(o,0x128);
    return online::LocalCopy(flags) ? IsOnlineAuthority(o) : (flags&1)==0;
}

struct Walk {
    void** vtable;
    const aura::Source* sources;
    int count;
    std::int32_t team;
    float dt;
    ULONGLONG frame;
    int friends;
};
void __fastcall WalkVisit(void* f,void* object) noexcept {
    __try {
        auto& w=*static_cast<Walk*>(f);
        auto o=static_cast<unsigned char*>(object);
        if(!o || o[kDead])return;
        const aura::Effect e=aura::Select(w.sources,w.count,Pos(o),w.team);
        if(!e.kinds)return;
        const bool soldier=Soldier(o);
        if(soldier && At<const void*>(o,kHumanVehicle))return;   // riding: its vehicle takes it
        if(!soldier && !KnownVehicle(o))return;
        ++w.friends;
        if(e.attack>0.0f)Put<float>(o,kBuff,aura::AttackMultiplier(At<float>(o,kBuff),e.attack));
        if(e.ammo>0.0f)FillAll(o,soldier,e.ammo,w.dt,w.frame);
        if(e.repair>0.0f && OwnedHere(o)) {
            float given=0.0f;
            const float hp=aura::Heal(At<float>(o,kHp),At<float>(o,kHpLeast),At<float>(o,kHpMax),e.repair,w.dt,&given);
            if(given>0.0f) {
                Put<float>(o,kHp,hp);
                if(InSession())Put<float>(o,kHpDiff,At<float>(o,kHpDiff)+given);   // the native diff: every copy
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
void __fastcall WalkDtor(void*,unsigned) noexcept {}
void* kWalkVtable[]={reinterpret_cast<void*>(&WalkDtor),reinterpret_cast<void*>(&WalkVisit)};

void Apply(const aura::Msg& m,float dt,ULONGLONG frame) noexcept {
    const auto manager=At<void*>(image,kTeamManager);
    if(!manager || !m.count || !(dt>0.0f))return;
    std::int32_t teams[kTeams];int nt=0;
    for(int i=0;i<m.count;++i) {
        bool have=false;
        for(int k=0;k<nt;++k)have=have || teams[k]==m.sources[i].team;
        if(!have && nt<kTeams)teams[nt++]=m.sources[i].team;
    }
    for(int k=0;k<nt;++k) {
        Walk w{kWalkVtable,m.sources,m.count,teams[k],dt,frame,0};
        reinterpret_cast<WalkFn>(image+kTeamWalk)(manager,teams[k],&w);
    }
}

// The HUD's view: the rings near the player (each kRingMs, on the ground under them) and what reaches it.
void Publish(const aura::Msg& m,ULONGLONG ms) noexcept {
    const unsigned char* const human=PlayerHuman();
    aura::Effect here{};
    float at[3]{};
    const bool have=human && Readable(human,kPosition+12);
    if(have){std::memcpy(at,Pos(human),12);here=aura::Select(m.sources,m.count,at,player.team);}
    const bool rays=ms-ringAt>=kRingMs || ms<ringAt;
    AuraView next{};
    if(rays) {
        ringAt=ms;
        for(int i=0;i<m.count && have;++i) {
            const aura::Source& s=m.sources[i];
            if(s.team!=player.team || aura::Dist2(s.pos,at)>(kRingReach+s.radius)*(kRingReach+s.radius))continue;
            AuraRingView& r=next.ring[next.rings++];
            r.kind=s.kind;r.points=kAuraRing;
            for(int j=0;j<kAuraRing;++j) {
                const float a=2.0f*kPi*static_cast<float>(j)/static_cast<float>(kAuraRing);
                const float x=s.pos[0]+s.radius*std::cos(a),z=s.pos[2]+s.radius*std::sin(a);
                const float top[3]={x,s.pos[1]+60.0f,z},bottom[3]={x,s.pos[1]-60.0f,z};
                float hit[3];
                r.pt[j][0]=x;r.pt[j][2]=z;
                r.pt[j][1]=(MapRay(top,bottom,hit)>=0.0f ? hit[1] : s.pos[1])+0.5f;
            }
        }
    }
    // what the HUD block shows, logged when it changes (a kind flickering: which source and how far, LogStates)
    if(stateLines<aura::kStateLines && (!hereLogged || here.kinds!=hereKinds) && (ms<hereAt || ms-hereAt>=aura::kStateGapMs)) {
        ++stateLines;hereLogged=true;hereKinds=here.kinds;hereAt=ms;
        Log("AURA here: kinds 0x%X (attack %.2f repair %.3f ammo %.3f) of %d standing, player %s",static_cast<unsigned>(here.kinds),
            static_cast<double>(here.attack),static_cast<double>(here.repair),static_cast<double>(here.ammo),m.count,
            have ? "seen" : "not seen");
    }
    AcquireSRWLockExclusive(&viewLock);
    if(rays){view.rings=next.rings;std::memcpy(view.ring,next.ring,sizeof(next.ring));}
    if(!m.count)view.rings=0;
    view.here=here;
    ReleaseSRWLockExclusive(&viewLock);
}

void ClearView() noexcept {
    AcquireSRWLockExclusive(&viewLock);
    view.rings=0;view.here=aura::Effect{};
    ReleaseSRWLockExclusive(&viewLock);
    ringAt=0;
}

bool Receive(std::uint32_t peer,const char*,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    aura::Msg m{};
    if(!aura::Decode(bytes,size,m)) {
        if(logs++<16)Log("AURA peer %u: a message that does not read (%zu bytes): dropped",peer,size);
        return true;
    }
    if(!netReady || plugin_net::Host() || peer!=plugin_net::HostPeer() || peer==0) {
        if(netReady && logs++<16)Log("AURA peer %u: auras not from the host: dropped",peer);
        return true;
    }
    heard=m;
    const ULONGLONG ms=GameMs();
    heardAt=ms ? ms : 1;
    return true;
}
void Update(const CommandNetworkContext& context,std::uint64_t) noexcept {
    const bool ready=context.ready && context.sendState;
    if(ready==netReady)return;
    netReady=ready;
    sent=false;heard=aura::Msg{};heardAt=0;
}
void NetReset() noexcept { netReady=false;sent=false;heard=aura::Msg{};heardAt=0; }
void Joined(std::uint32_t) noexcept { sent=false; }   // a new member: the list again at once
}  // namespace

bool InstallAura() noexcept {
    for(const Sig& s:kSigs)if(!Matches(s.rva,s.bytes,s.size)) {
        Log("AURA off: EDF.dll+0x%X is not as expected",s.rva);
        ok=false;
        return false;
    }
    if(!trucks.Allocate("aura trucks") || !carries.Allocate("aura carries")){ok=false;return false;}
    ok=true;
    Log("AURA ready: %zu sites checked",sizeof(kSigs)/sizeof(kSigs[0]));
    return true;
}

void AuraVehicleFrame(unsigned char* v) noexcept {
    if(!ok || !v || !Cfg().auraVehicles)return;
    const ULONGLONG ms=GameMs();
    for(Truck& t:trucks)if(t.ref.Is(v)) {
        if(t.kind==-1){t.seen=ms;return;}
        break;
    }
    Truck* const t=TruckOf(v,ms);
    if(!t)return;
    t->seen=ms;
    if(t->kind==-2)t->kind=KindOf(v);
    if(t->kind<0)return;
    std::memcpy(t->pos,Pos(v),12);
    if(!t->atMs || ms<t->atMs){std::memcpy(t->at,t->pos,12);t->atMs=ms;}
    else if(ms-t->atMs>=kSpeedMs) {
        t->speed=std::sqrt(aura::Dist2(t->pos,t->at))*1000.0f/static_cast<float>(ms-t->atMs);
        std::memcpy(t->at,t->pos,12);t->atMs=ms;
    }
    const Rider r=SeatCount(v)>0 ? SeatRider(SeatAt(v,0)) : Rider::none;
    t->driven=r==Rider::player || r==Rider::other;
}

// Each aura truck this machine sees: why its aura stands or not and how far the player is (aura::StateLogDue paces it).
// Read with AURA here: a kind that comes and goes with `inside` flipping is the player crossing that truck's radius.
void LogStates(ULONGLONG ms) noexcept {
    static const char* const kNames[aura::kKinds]={"command","repair","ammo"};
    const Config& c=Cfg();
    const unsigned char* const human=PlayerHuman();
    const bool have=human && Readable(human,kPosition+12);
    for(Truck& t:trucks) {
        if(t.kind<0 || t.kind>=aura::kKinds || ms-t.seen>kGoneMs || !TruckLive(t))continue;
        const auto* v=static_cast<const unsigned char*>(t.ref.obj);
        const bool dead=v[kDead]!=0;
        const bool active=aura::Active(dead,t.driven,t.speed,c.auraParkSpeed);
        aura::Source s{};
        std::memcpy(s.pos,t.pos,12);s.radius=c.auraRadius;
        const bool inside=have && aura::Inside(s,Pos(human));
        if(!aura::StateLogDue(t.mark,active,inside,ms,stateLines))continue;
        ++stateLines;
        t.mark=aura::StateMark{true,active,inside,ms};
        const float d=have ? std::sqrt(aura::Dist2(t.pos,Pos(human))) : -1.0f;
        Log("AURA truck %p %s: active=%d (dead=%d driven=%d speed=%.2f m/s, park %.2f) at (%.1f,%.1f,%.1f), player %.1f m "
            "(radius %.0f, inside=%d)",t.ref.obj,kNames[t.kind],active ? 1 : 0,dead ? 1 : 0,t.driven ? 1 : 0,
            static_cast<double>(t.speed),static_cast<double>(c.auraParkSpeed),static_cast<double>(t.pos[0]),
            static_cast<double>(t.pos[1]),static_cast<double>(t.pos[2]),static_cast<double>(d),
            static_cast<double>(c.auraRadius),inside ? 1 : 0);
    }
}

void AuraTick() noexcept {
    const ULONGLONG frame=GameFrame();
    if(frame==lastFrame)return;
    lastFrame=frame;
    const ULONGLONG ms=GameMs();
    float dt=lastMs && ms>lastMs ? static_cast<float>(ms-lastMs)/1000.0f : 0.0f;
    lastMs=ms;
    if(dt>0.1f)dt=0.1f;   // a hitch (a load, a pause) is no 10 s of repair
    if(!ok || !Cfg().enabled || !Cfg().auraVehicles){ClearView();return;}
    aura::Msg m{};
    if(!InSession() || plugin_net::Host()) {
        m=Standing(ms);
        if(InSession() && netReady)SendState(m,ms);
    } else if(netReady && aura::Fresh(heardAt,ms))m=heard;
    LogStates(ms);
    Apply(m,dt,frame);
    Publish(m,ms);
}

void ResetAura() noexcept {
    for(Truck& t:trucks)t=Truck{};
    for(Carry& c:carries)c=Carry{};
    sent=false;sentMsg=aura::Msg{};sentAt=0;heard=aura::Msg{};heardAt=0;lastMs=0;
    stateLines=0;hereLogged=false;hereKinds=0;hereAt=0;
    ClearView();
}

bool AuraViewNow(AuraView* out) noexcept {
    if(!out)return false;
    AcquireSRWLockShared(&viewLock);
    *out=view;
    ReleaseSRWLockShared(&viewLock);
    return out->rings>0 || out->here.kinds!=0;
}

plugin_net::Channel AuraNetChannel() noexcept {
    plugin_net::Channel c;
    c.name="aura vehicles";c.magic=aura::kMagic;c.version=aura::kVersion;
    c.minSize=c.maxSize=aura::kWire;c.takesState=&aura::TakesState;
    c.receive=&Receive;c.update=&Update;c.reset=&NetReset;c.joined=&Joined;
    return c;
}
}  // namespace crew
