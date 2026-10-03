// The submarine carrier (docs/subcarrier-re.md): the 潜水母艦 of the missions (Pandora / Epimetheus /
// Siren: the mission object EV603_MARINE, a FarEventObject with no HP, no collision and no weapons of its
// own) made a friendly NPC vehicle. Like the jets (jet.cpp) it is a Vehicle506_Helicopter body from a
// derived SGO (tools/make_sub.py EDF6VC_SUB_CARRIER.SGO, testrange/gen.py 'edf6tr_sub_carrier_mission'):
// the stock HP (+0x2F4 / +0x2F8), the crash and wreck, the seat weapons, and the hull's rigid box, which
// is what bullets hit and what soldiers stand on. Its model is EV603_MARINE at the missions' size (1664 m),
// told apart by its speed gain k (veh+0x162C) = kSubMark (the jets use 7001-7010).
// The plugin drives it, as jet.cpp drives a jet, in two stages a frame:
//  - input (slot 55, from HeliFrame, NPC pilot only): it sits surfaced (hull bottom kClear over the highest
//    ground under it), holds its post and follows the player at a ship's pace once they are kLeash away,
//    turns its bow (the turrets' and the missile bay's facing) onto the nearest enemy, fires the turret guns
//    (0x2020) when one is on the bow and the homing missiles (0x2021, the bay's チラン爆雷 / missiles) once
//    the game has locked a target; empty weapons are reloaded aboard (WEAPONTEXT: the missiles are loaded
//    inside the carrier) after kReloadMs;
//  - physics (506 slot 57, chained after jet.cpp's): its linear and angular velocity.
// Its HP is shown as the game's follower gauge (HudPlayer_FollowerDurability 0x8040E0 draws one over every
// follower of the player, 0x804300): the plugin calls that drawer once more with a stand-in owner whose
// follower list holds a stand-in object per carrier (only +0x90 position, +0x2F4 / +0x2F8 HP and +0x550,
// an empty follower list, are read).
// Its HP is split into the hull and four deck parts (kSystems: two turrets, the missile bay, the drone bay),
// docs/subcarrier-re.md §8: every hit reaches it as the damage message through the 506's slot 9, which the
// plugin hooks for carriers only. A hit within a part's reach wears that part (its own HP, its own gauge) and
// not the hull; any other hit on the hull counts only from a heavy source (kHeavy: the Mothership's Genocide
// cannon, the dropships' portal laser) or a single hit of cfg.subHeavyHit. A worn-out part stops working (its
// gun or missiles go dry, the bay launches no drones) until the crew repairs it kRepairMs on. The drone bay
// launches jet.cpp's gun drones (JetLaunchDrone) while it has a target.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
constexpr unsigned kHeli506=0x17DB238;
constexpr std::size_t kSlotPhysics=57;
constexpr std::size_t kSpeedGain=0x162C,kBody=0x1650;
constexpr unsigned kSetLinearVelocity=0x11B18F0,kSetAngularVelocity=0x11B1760,kDelete=0x118A1B0;
constexpr std::size_t kInLateral=0x1540,kInForward=0x1548,kInYaw=0x1550;
constexpr std::size_t kFireGun=0x2020,kFireMissile=0x2021;
constexpr std::size_t kHpMax=0x2F4,kHp=0x2F8;
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kHolderWeapon=0x10;
constexpr std::size_t kWeaponLockon=0x6B0,kWeaponAmmo=0xBE8;
constexpr std::size_t kWeaponLockAngle=0x6C0,kWeaponLockRange=0x6D0,kWeaponLockSpeed=0x790,kWeaponLocked=0xC68;
constexpr std::int32_t kHoming=1;
constexpr std::size_t kAreaInset=0xE00;   // jet.cpp: the move-area clamp's inset; kNoInset = no clamp
constexpr float kNoInset=-1.0e6f;
constexpr std::size_t kObjFlags=0x18;
constexpr unsigned char kObjDeleted=4;
constexpr unsigned kPreload=0x7A3780,kCreateObject=0x11945E0,kSetTeam=0x54EE70,kInitParamVtable=0x1762068;
constexpr std::size_t kPreloadMgr=0x20B29A8,kObjectMgr=0x20B2958;
constexpr std::int32_t kTeamFriend=2;
constexpr unsigned kFindPart=0x6EA4B0;
constexpr std::size_t kParts=0x1320,kBodyPart=0x1530;
// The gauge (docs/subcarrier-re.md §4): the follower HUD's draw (vtable 0x17F6C08 slot 3) calls the
// drawer at kGaugeCall: (hud, view-projection, owner, r9, 5th) -> a gauge per object in owner+0x550's list.
constexpr unsigned kGaugeHud=0x17F6C08,kGaugeDraw=0x8040E0,kGaugeCall=0x8042AD,kGaugeFn=0x804300;
constexpr std::size_t kFollowers=0x550,kProxySize=0x560;
// Damage (docs/subcarrier-re.md §8): 0x543920 sends each queued hit to its object as message kMsgDamage (slot 10,
// then slot 9, then slot 11; the message data the GameDamageInfo); the 506's slot 9 (0x652E70) passes it to
// 0x62ECB0 -> 0x54A530 -> 0x547C30, which takes the damage at +0x50 off the HP +0x2F8 (friends' rounds left out).
constexpr std::size_t kSlotMessage=9;
constexpr unsigned kMessage506=0x652E70;
constexpr std::uint32_t kMsgDamage=0x10000000;
// GameDamageInfo: the attacker (weak_ptr: object, control block, its use count at +8), its team, where it hit
// (the round's position, or the blast's centre), the damage.
constexpr std::size_t kDmgAttacker=0x10,kDmgAttackerCtrl=0x18,kDmgTeam=0x24,kDmgAt=0x30,kDmgAmount=0x50,kCtrlUses=8;
// TeamManager (*(image+kTeams), heli.cpp): rows at +kTeamRows, kTeamStride each, the row's relations (int[]) at +0x18.
constexpr std::size_t kTeams=0x20B2978,kTeamRows=0x38,kTeamStride=0x38,kTeamRelation=0x18;
constexpr std::int32_t kEnemyRelation=2,kMaxTeam=64;   // heli.cpp Relations
// The heavy sources: an attacker of these classes (the vtable, checked against its RTTI name at install) hits the
// hull in full. The Genocide cannon (e511_mothership_genocide_l / _s.sgo: UfoMother511CoreBigCannon / Small), the
// Mothership (should its parts' rounds be owned by it), the e508 dropship (UfoCarrier508: it fires nothing in the
// stock game, so its rounds are the portal laser, EDF6VC_PORTAL_LASER.SGO, if it is fired as the dropship's own).
struct Heavy { unsigned vtable; const char* rtti; };
const Heavy kHeavy[]={{0x17CACC0,".?AVUfoMother511CoreBigCannon@@"},{0x17CB0A0,".?AVUfoMother511CoreSmallCannon@@"},
                      {0x17CA278,".?AVUfoMother511@@"},{0x17C9DC0,".?AVUfoCarrier508@@"}};
constexpr int kHeavyCount=sizeof(kHeavy)/sizeof(kHeavy[0]);
constexpr std::uintptr_t kImageSpan=0x3000000;   // past EDF.dll's last section

constexpr float kSubMark=7101.0f;            // testrange/gen.py JETS['edf6tr_sub_carrier_mission'].mark
const wchar_t* const kSubSgo=L"app:/object/edf6vc_sub_carrier.sgo";
// What SubLaunch needs in Mods (tools/make_sub.py): the SGO, its model, the turret guns.
const wchar_t* const kSubFiles[]={L"\\Mods\\OBJECT\\EDF6VC_SUB_CARRIER.SGO",L"\\Mods\\OBJECT\\EDF6VC_SUB.MRAB",
                                  L"\\Mods\\WEAPON\\EDF6VC_JET_GUN_L.SGO",L"\\Mods\\WEAPON\\EDF6VC_JET_GUN_R.SGO"};
constexpr int kMaxSubs=3;                     // M123: three carriers attack at once (BE151_157)

// The hull (the SGO's box, the model at its own size): its bottom kHullBottom under the body origin,
// kHalfLength fore and aft; the tower top kTop over it; the turrets' guns kGunHeight over it.
constexpr float kHullBottom=166.58f,kHalfLength=790.0f,kHalfWidth=116.0f,kTop=366.0f,kGunHeight=241.0f;
// Driving: metres, m/s, m/s^2, rad/s.
constexpr float kClear=0.6f;                 // hull bottom over the highest ground under it
// Afloat, its origin this far under the water's surface: where M082 puts the stock carrier (origin -130,
// its main deck at about +63; the sea there assumed at y=0, docs/water-re.md has the log that checks it).
constexpr float kDraft=130.0f;
// A player on its deck is up to 830 m from its post: it sets off only past the hull.
constexpr float kLeash=1500.0f,kStop=1000.0f;  // it sets off after a player this far from its post, stops this near
constexpr float kCruise=25.0f,kAccel=3.0f,kClimb=8.0f,kSink=4.0f,kClimbAccel=6.0f,kPosGain=0.2f;
constexpr float kTurnRate=0.05f,kTurnGain=0.8f,kRollGain=1.5f,kMaxPitch=0.05f;   // 3 deg/s, 3 deg
// Combat.
constexpr float kRange=2000.0f;              // it engages enemies this far from it
constexpr float kGunRange=580.0f,kGunCone=0.07f;   // the guns' reach (gen.JET_GUN_REACH 600), 4 deg
constexpr float kMissileRange=2000.0f,kMissileMin=60.0f;
constexpr float kLockMargin=1.15f,kLockAngle=1.2f,kLockSpeed=2.0f;
constexpr ULONGLONG kMissileMs=2500,kReloadMs=12000,kStaleMs=1500,kGaugeMs=1000;
constexpr float kPi=3.14159265f;

// The deck parts: each a capsule (segment a-b, `reach` round it) in the body frame (the model's metres, rows
// right, up, nose; docs/subcarrier-re.md §1.1 / §8), its HP and the seat weapon it is (-1: none). The turrets'
// capsules run from the deck (the hull box top kDeckTop: nothing above it collides) up to their tilt bones
// (gunA/gunB_tilt_l, seat weapons 0 and 1); the missile bay spans missle_l..missle_r (172 m up, under the bow
// deck at 193; seat weapon 2); the drone bay is a stretch of the after deck the plugin picks (no bone there).
// Rounds collide with the hull box only (its top the deck): what hits a part lands on the deck round it.
// `gauge` is where its HP gauge stands, `launch` where the bay's drones start.
enum System { turretA, turretB, missiles, droneBay, kSystemCount };
struct SystemSpec { const char* name; float a[3],b[3]; float reach,hp; int weapon; float gauge[3]; };
const SystemSpec kSystems[kSystemCount]={
    {"turretA",{17.7f,193.08f,-16.3f},{17.7f,229.5f,-16.3f},20.0f,6000.0f,0,{17.7f,255.0f,-16.3f}},
    {"turretB",{17.7f,193.08f,-52.2f},{17.7f,245.9f,-52.2f},20.0f,6000.0f,1,{17.7f,271.0f,-52.2f}},
    {"missiles",{-56.5f,172.0f,592.0f},{56.5f,172.0f,592.0f},30.0f,5000.0f,2,{0.0f,215.0f,592.0f}},
    {"dronebay",{0.0f,193.08f,-560.0f},{0.0f,193.08f,-640.0f},35.0f,5000.0f,-1,{0.0f,215.0f,-600.0f}},
};
constexpr float kBayLaunch[3]={0.0f,253.0f,-600.0f};   // 60 m over the bay
constexpr ULONGLONG kRepairMs=90000;           // a worn-out part is repaired this long on (game time)
constexpr int kBayDrones=4;                    // the drone bay keeps at most this many out...
constexpr ULONGLONG kBayGapMs=10000;           // ...launching one this often while it has a target
constexpr ULONGLONG kHitLogMs=1000;

struct Sub {
    unsigned char* vehicle;
    const void* ctrl;
    ULONGLONG seen,bornAt,missileAt,logAt;
    ULONGLONG gaugeTick;          // GetTickCount64 of the last frame (the gauge may be drawn on another thread)
    LARGE_INTEGER last;
    float post[3];
    float floor;                  // the lowest its origin goes: a mission's own height for it (-inf: called in)
    float lin[3],ang[3];
    bool moving,ready,launched;
    bool sea;                     // called in afloat: it keeps to the water (Follow)
    std::int32_t full[4];         // each seat weapon's ammo when first seen
    ULONGLONG emptyAt[4];
    float target[3];
    bool hasTarget;
    float wear[kSystemCount];         // damage each deck part took: worn out at its hp
    bool down[kSystemCount];          // worn out (since downAt, game ms)
    ULONGLONG downAt[kSystemCount];
    bool bayLogged;                   // the bay's launch failure was logged
    unsigned char* drone[kBayDrones]; // the bay's drones out (and their control blocks)
    const void* droneCtrl[kBayDrones];
    ULONGLONG droneAt;
    // Hits since hitLogAt (the "SUB hit" line): on parts, on the hull (taken, held off), the last one.
    unsigned hits;
    float partDmg,hullDmg,heldDmg,lastDmg;
    int lastPart;                     // -1: the hull
    std::uintptr_t lastFrom;          // the last attacker's vtable RVA (0: none)
    float lastAt[3];                  // where it hit (body frame)
    bool lastHeavy;
    ULONGLONG hitLogAt;
};
Sub subs[kMaxSubs]{};
// The gauges' stand-in objects (see the top): per carrier the hull's and each part's, position and HP copied
// every frame.
constexpr int kGauges=1+kSystemCount;
alignas(16) unsigned char proxies[kMaxSubs][kGauges][kProxySize]{};
bool heavyOk[kHeavyCount]{};
struct Node { Node* next; Node* prev; void* object; };   // the game's list node: next, prev, value at +0x10
Node noFollowers{};

bool spawnOk=false,physicsOk=false,gaugeOk=false,bodyPartOk=false,damageOk=false;
bool preloaded=false,broken=false;

struct alignas(16) InitParam { const void* vtable; unsigned char rest[0x28]; };
using PreloadFn=void(*)(void*,const wchar_t*,std::int32_t,std::int32_t);
using CreateObjectFn=unsigned char*(*)(void*,const float*,const wchar_t*,InitParam*);
using SetTeamFn=void(*)(void*,std::int32_t,bool);
using RideAiFn=void(*)(void*,bool);
using DeleteFn=void(*)(void*);
using SetVecFn=void(*)(void*,const float*);
using PhysicsFn=void(__fastcall*)(void*);
using FindPartFn=std::int32_t(__fastcall*)(void*,const wchar_t*);
using GaugeFn=void(__fastcall*)(void*,void*,void*,void*,void*);
using MessageFn=bool(__fastcall*)(void*,std::uint32_t,void*);
PhysicsFn nextPhysics=nullptr;
MessageFn nextMessage=nullptr;

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
// `cur` moved toward `want` by at most `step`.
float Approach(float cur,float want,float step) noexcept { return cur+Clamp(want-cur,-step,step); }

const void* SelfCtrl(const unsigned char* v) noexcept { return At<const void*>(v,kSelfCtrl); }

Sub* FindSub(const unsigned char* v,ULONGLONG ms) noexcept {
    for(auto& s:subs)
        if(s.vehicle==v && s.ctrl==SelfCtrl(v) && ms-s.seen<=kStaleMs)return &s;
    return nullptr;
}
// A free entry (empty, or not driven for kStaleMs); every stale entry of `v` cleared. nullptr: kMaxSubs live.
Sub* FreeSub(const unsigned char* v,ULONGLONG ms) noexcept {
    Sub* free=nullptr;
    for(auto& s:subs) {
        if(s.vehicle==v)s=Sub{};
        if(!free && (!s.vehicle || ms-s.seen>kStaleMs))free=&s;
    }
    return free;
}
int LiveSubs(ULONGLONG ms) noexcept {
    int n=0;
    for(const auto& s:subs)n+=s.vehicle && ms-s.seen<=kStaleMs ? 1 : 0;
    return n;
}

// Body frame (m: rows right, up, nose, position) <-> world.
void Local(const float* m,const float* p,float* out) noexcept {
    const float rel[3]={p[0]-m[12],p[1]-m[13],p[2]-m[14]};
    out[0]=Dot(rel,m);out[1]=Dot(rel,m+4);out[2]=Dot(rel,m+8);
}
void World(const float* m,const float* l,float* out) noexcept {
    for(int i=0;i<3;++i)out[i]=m[12+i]+m[i]*l[0]+m[4+i]*l[1]+m[8+i]*l[2];
}
// How far body-frame point `p` is from part `k`'s segment, in its reaches (<= 1: within it).
float Reaches(const SystemSpec& k,const float* p) noexcept {
    const float ab[3]={k.b[0]-k.a[0],k.b[1]-k.a[1],k.b[2]-k.a[2]},ap[3]={p[0]-k.a[0],p[1]-k.a[1],p[2]-k.a[2]};
    const float len2=Dot(ab,ab);
    const float t=len2>0.0f ? Clamp(Dot(ap,ab)/len2,0.0f,1.0f) : 0.0f;
    const float d[3]={ap[0]-ab[0]*t,ap[1]-ab[1]*t,ap[2]-ab[2]*t};
    return Len(d)/k.reach;
}
// The working part a hit at body-frame `p` lands on (the one it is fewest reaches from), or -1: the hull.
int PartAt(const Sub& s,const float* p) noexcept {
    int best=-1;
    float nearest=1.0f;
    for(int k=0;k<kSystemCount;++k) {
        const float r=s.down[k] ? INFINITY : Reaches(kSystems[k],p);
        if(r<=nearest){nearest=r;best=k;}
    }
    return best;
}

// Whether side `team` is the enemy of `v`'s (the TeamManager's relation; unreadable: any other side).
bool Hostile(const unsigned char* v,std::int32_t team) noexcept {
    const auto own=At<std::int32_t>(v,kTeam);
    if(team==own)return false;
    if(own<0 || own>=kMaxTeam || team<0 || team>=kMaxTeam)return true;
    const auto mgr=At<const unsigned char*>(image,kTeams);
    if(!Readable(mgr,kTeamRows+8))return true;
    const auto row=At<const unsigned char*>(mgr,kTeamRows)+own*kTeamStride;
    if(!Readable(row,kTeamStride))return true;
    const auto relation=At<const std::int32_t*>(row,kTeamRelation);
    return !Readable(relation+team,4) || relation[team]==kEnemyRelation;
}
// The live attacker's vtable RVA (0: none, gone, or no EDF.dll class).
std::uintptr_t Attacker(const unsigned char* gdi) noexcept {
    const auto obj=At<const unsigned char*>(gdi,kDmgAttacker);
    const auto ctrl=At<const unsigned char*>(gdi,kDmgAttackerCtrl);
    if(!obj || !Readable(ctrl,kCtrlUses+4) || At<std::int32_t>(ctrl,kCtrlUses)<=0 || !Readable(obj,8))return 0;
    const auto vtable=At<const unsigned char*>(obj,0);
    return vtable>image && vtable<image+kImageSpan ? static_cast<std::uintptr_t>(vtable-image) : 0;
}
bool HeavySource(std::uintptr_t from) noexcept {
    for(int k=0;k<kHeavyCount;++k)
        if(heavyOk[k] && from==kHeavy[k].vtable)return true;
    return false;
}

void Wear(Sub& s,int k,float dmg) noexcept {
    s.partDmg+=dmg;
    s.wear[k]+=dmg;
    if(s.wear[k]<kSystems[k].hp)return;
    s.wear[k]=kSystems[k].hp;s.down[k]=true;s.downAt[k]=GameMs();
    Log("SUB v=%p part %s destroyed (the crew repairs it in %llus)",s.vehicle,kSystems[k].name,kRepairMs/1000);
}

// A damage message to a carrier, routed (see the top): its damage cut to 0 when a part takes it or the hull holds it
// off. Returns where to put the damage back after the stock handler (`was`), or nullptr: the message as it came.
float* Route(unsigned char* v,unsigned char* gdi,float* was) noexcept {
    if(At<float>(v,kSpeedGain)!=kSubMark || v[kDead] || !Readable(gdi,kDmgAmount+4,true))return nullptr;
    Sub* s=FindSub(v,GameMs());
    const float dmg=At<float>(gdi,kDmgAmount);
    // Heals (< 0) and friends' rounds (the stock rule) are left alone.
    if(!s || !s->ready || !(dmg>0.0f) || !std::isfinite(dmg) || !Hostile(v,At<std::int32_t>(gdi,kDmgTeam)))return nullptr;
    float at[3];
    Local(reinterpret_cast<const float*>(v+kMatrix),reinterpret_cast<const float*>(gdi+kDmgAt),at);
    const int part=PartAt(*s,at);
    const std::uintptr_t from=Attacker(gdi);
    const bool heavy=HeavySource(from) || (cfg.subHeavyHit>0.0f && dmg>=cfg.subHeavyHit);
    ++s->hits;s->lastDmg=dmg;s->lastPart=part;s->lastFrom=from;s->lastHeavy=heavy;
    std::memcpy(s->lastAt,at,12);
    if(part<0 && heavy){s->hullDmg+=dmg;return nullptr;}
    if(part>=0)Wear(*s,part,dmg);
    else s->heldDmg+=dmg;
    *was=dmg;
    Put<float>(gdi,kDmgAmount,0.0f);
    return reinterpret_cast<float*>(gdi+kDmgAmount);
}

// The 506's slot 9 (every message to it): damage to a carrier routed first. The damage is put back after (the
// queue's own copy: nothing reads it on, but the message is left as it came).
bool __fastcall MessageHook(void* obj,std::uint32_t msg,void* data) {
    float* back=nullptr;
    float was=0.0f;
    if(msg==kMsgDamage) {
        __try { back=Route(static_cast<unsigned char*>(obj),static_cast<unsigned char*>(data),&was); }
        __except(EXCEPTION_EXECUTE_HANDLER){back=nullptr;}
    }
    const bool handled=nextMessage(obj,msg,data);
    if(back) {
        __try { *back=was; } __except(EXCEPTION_EXECUTE_HANDLER){}
    }
    return handled;
}

// The hull made as thick as cfg.subHullHp (its HP raised in proportion), once per carrier.
void Thicken(unsigned char* v) noexcept {
    const float max=At<float>(v,kHpMax),hp=At<float>(v,kHp);
    if(!(cfg.subHullHp>max) || !(max>0.0f))return;
    Put<float>(v,kHpMax,cfg.subHullHp);
    Put<float>(v,kHp,hp/max*cfg.subHullHp);
    Log("SUB v=%p hull hp %.0f/%.0f -> %.0f/%.0f",v,hp,max,At<float>(v,kHp),cfg.subHullHp);
}

// Worn-out parts back in order kRepairMs after they wore out.
void Repair(Sub& s,ULONGLONG ms) noexcept {
    for(int k=0;k<kSystemCount;++k) {
        if(!s.down[k] || ms-s.downAt[k]<kRepairMs)continue;
        s.down[k]=false;s.wear[k]=0.0f;
        Log("SUB v=%p part %s repaired (hp %.0f)",s.vehicle,kSystems[k].name,kSystems[k].hp);
    }
}

// Whether seat weapon `w`'s part is worn out.
bool WeaponDown(const Sub& s,int w) noexcept {
    for(int k=0;k<kSystemCount;++k)
        if(kSystems[k].weapon==w && s.down[k])return true;
    return false;
}

// The drone bay: while it works and the carrier has a target, a gun drone every kBayGapMs, kBayDrones out at most;
// they guard the carrier (their anchor the launch point) and go when jet.cpp withdraws them.
void Bay(Sub& s,const float* m,ULONGLONG ms) noexcept {
    int out=0;
    for(int k=0;k<kBayDrones;++k) {
        if(s.drone[k] && !JetFlying(s.drone[k],s.droneCtrl[k])){s.drone[k]=nullptr;s.droneCtrl[k]=nullptr;}
        out+=s.drone[k] ? 1 : 0;
    }
    if(s.down[droneBay] || !s.hasTarget || out>=kBayDrones || ms-s.droneAt<kBayGapMs)return;
    s.droneAt=ms;
    float from[3];
    World(m,kBayLaunch,from);
    const float nose[3]={m[8],m[9],m[10]};
    unsigned char* d=JetLaunchDrone(from,nose,from,cfg.jetFuelSec,s.vehicle,false);
    if(!d) {
        if(!s.bayLogged)Log("SUB v=%p drone bay: no drone launched (JetPilot off, EDF6VC_JET_DRONE.SGO not preloaded, or jets full)",s.vehicle);
        s.bayLogged=true;
        return;
    }
    for(int k=0;k<kBayDrones;++k)
        if(!s.drone[k]){s.drone[k]=d;s.droneCtrl[k]=SelfCtrl(d);break;}
    Log("SUB v=%p drone bay launched %p (%d out)",s.vehicle,d,out+1);
}

// "SUB hit": the hits since the last line, at most one a kHitLogMs.
void HitLog(Sub& s,ULONGLONG ms) noexcept {
    if(!s.hits || ms-s.hitLogAt<kHitLogMs)return;
    s.hitLogAt=ms;
    Log("SUB v=%p hit x%u: parts %.0f, hull %.0f, held off %.0f; last %.0f on %s at (%.0f,%.0f,%.0f) from EDF+%llX%s",s.vehicle,s.hits,
        s.partDmg,s.hullDmg,s.heldDmg,s.lastDmg,s.lastPart>=0 ? kSystems[s.lastPart].name : "hull",s.lastAt[0],s.lastAt[1],s.lastAt[2],
        static_cast<unsigned long long>(s.lastFrom),s.lastHeavy ? " heavy" : "");
    s.hits=0;s.partDmg=s.hullDmg=s.heldDmg=0.0f;
}

bool FilesThere() noexcept {
    wchar_t dir[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,dir,MAX_PATH);
    wchar_t* slash=n && n<MAX_PATH ? wcsrchr(dir,L'\\') : nullptr;
    if(!slash)return false;
    *slash=0;
    for(const auto file:kSubFiles) {
        wchar_t path[MAX_PATH];
        if(wcscpy_s(path,dir)!=0 || wcscat_s(path,file)!=0)return false;
        if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)return false;
    }
    return true;
}

// Metres of the highest ground (terrain, buildings) under the hull's footprint at `pos` along `nose`, or
// false with none seen.
bool GroundUnder(const float* pos,const float* nose,float* ground) noexcept {
    const float side[3]={nose[2],0.0f,-nose[0]};
    const float along[5]={0.0f,kHalfLength,-kHalfLength,0.0f,0.0f},across[5]={0.0f,0.0f,0.0f,kHalfWidth,-kHalfWidth};
    bool any=false;
    for(int i=0;i<5;++i) {
        const float x=pos[0]+nose[0]*along[i]+side[0]*across[i],z=pos[2]+nose[2]*along[i]+side[2]*across[i];
        const float top[3]={x,pos[1]+kTop,z},bottom[3]={x,pos[1]-kHullBottom-900.0f,z};
        float hit[3];
        if(MapRay(top,bottom,hit)<0.0f)continue;
        if(!any || hit[1]>*ground)*ground=hit[1];
        any=true;
    }
    return any;
}

struct Nearest { float from[3]; float best; float aim[3]; bool found; };
void SeeEnemy(void* ctx,const void*,const float* aim) noexcept {
    auto& n=*static_cast<Nearest*>(ctx);
    const float d[3]={aim[0]-n.from[0],aim[1]-n.from[1],aim[2]-n.from[2]};
    const float dist=Len(d);
    if(!std::isfinite(dist) || dist>=n.best)return;
    n.best=dist;std::memcpy(n.aim,aim,12);n.found=true;
}

// The seat weapons: their homing ones lock out to kMissileRange (wider and faster than stock), an empty
// one is refilled to what it held at first kReloadMs after it ran dry. Out: gun ammo, missiles, locks.
void Arm(Sub& s,unsigned char* v,ULONGLONG ms,std::int32_t* guns,std::int32_t* missiles,std::int32_t* locked) noexcept {
    *guns=*missiles=*locked=0;
    if(SeatCount(v)==0)return;
    const auto seat=SeatAt(v,0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(count>8 || !Readable(holders,count*8))return;
    for(std::uint64_t i=0;i<count && i<4;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,kWeaponLocked+8,true))continue;
        const bool homing=At<std::int32_t>(w,kWeaponLockon)==kHoming;
        if(i==3 || (!homing && i>=2))continue;        // the fuel tank (v_fuel01) after the missile
        std::int32_t ammo=At<std::int32_t>(w,kWeaponAmmo);
        if(ammo>s.full[i])s.full[i]=ammo;
        if(WeaponDown(s,static_cast<int>(i))) {   // its part worn out: dry, reloaded kReloadMs after the repair
            Put<std::int32_t>(w,kWeaponAmmo,0);s.emptyAt[i]=ms;
            continue;
        }
        if(ammo>0)s.emptyAt[i]=0;
        else if(!s.emptyAt[i])s.emptyAt[i]=ms;
        else if(ms-s.emptyAt[i]>=kReloadMs && s.full[i]>0) {
            ammo=s.full[i];Put<std::int32_t>(w,kWeaponAmmo,ammo);s.emptyAt[i]=0;
            if(cfg.debug)Log("SUB v=%p weapon %llu reloaded aboard: %d",v,static_cast<unsigned long long>(i),ammo);
        }
        if(!homing){*guns+=ammo>0 ? ammo : 0;continue;}
        *missiles+=ammo>0 ? ammo : 0;
        const auto l=At<std::uint64_t>(w,kWeaponLocked);
        *locked+=l<64 ? static_cast<std::int32_t>(l) : 0;
        auto& range=*reinterpret_cast<float*>(w+kWeaponLockRange);
        if(range<kMissileRange*kLockMargin)range=kMissileRange*kLockMargin;
        for(int k=0;k<2;++k) {
            auto& a=*reinterpret_cast<float*>(w+kWeaponLockAngle+4*k);
            if(a<kLockAngle)a=kLockAngle;
        }
        auto& speed=*reinterpret_cast<float*>(w+kWeaponLockSpeed);
        if(speed<kLockSpeed)speed=kLockSpeed;
    }
}

// Its post follows the player at a ship's pace once they are kLeash from it, until kStop. A called-in
// carrier (afloat: s.sea) keeps to the water: its post does not move onto a spot with none (it waits at
// the shore), and over water it floats kDraft under that spot's surface.
void Follow(Sub& s,float dt) noexcept {
    if(!player.at || GetTickCount64()-player.at>5000)return;
    const float d[3]={player.pos[0]-s.post[0],0.0f,player.pos[2]-s.post[2]};
    const float dist=Len(d);
    if(dist>kLeash)s.moving=true;
    if(dist<kStop)s.moving=false;
    if(!s.moving || dist<1.0f)return;
    const float step=kCruise*dt;
    const float x=s.post[0]+d[0]/dist*step,z=s.post[2]+d[2]/dist*step;
    if(s.sea) {
        float surface=0.0f;
        const Sea sea=SeaAt(x,z,&surface);
        if(sea==Sea::land)return;
        if(sea==Sea::water)s.floor=surface-kDraft;
    }
    s.post[0]=x;s.post[2]=z;
}

// The velocity toward its post (level, at most kCruise), holding the hull kClear over the ground and the
// origin at s.floor at least: a mission puts it where it floats (M082: -130, its deck at sea), and over a
// sea the ray finds only the seabed, far under it.
void Drive(Sub& s,const float* pos,const float* nose,float dt) noexcept {
    float want[3]={(s.post[0]-pos[0])*kPosGain,0.0f,(s.post[2]-pos[2])*kPosGain};
    const float speed=Len(want);
    if(speed>kCruise){want[0]*=kCruise/speed;want[2]*=kCruise/speed;}
    s.lin[0]=Approach(s.lin[0],want[0],kAccel*dt);
    s.lin[2]=Approach(s.lin[2],want[2],kAccel*dt);
    float ground=0.0f;
    const bool seen=GroundUnder(pos,nose,&ground);
    float hold=seen ? ground+kHullBottom+kClear : s.floor;
    if(hold<s.floor)hold=s.floor;
    const float wantY=std::isfinite(hold) ? Clamp(hold-pos[1],-kSink,kClimb) : 0.0f;
    s.lin[1]=Approach(s.lin[1],wantY,kClimbAccel*dt);
}

// The spin that turns the bow onto `want` (at most kTurnRate) and keeps the deck level across.
void Steer(Sub& s,const float* m,const float* want) noexcept {
    float nose[3]={m[8],m[9],m[10]},right[3]={m[0],m[1],m[2]};
    if(!Normalize(nose) || !Normalize(right)){s.ang[0]=s.ang[1]=s.ang[2]=0.0f;return;}
    float axis[3];
    Cross(nose,want,axis);
    const float sinA=Len(axis),angle=std::atan2(sinA,Dot(nose,want));
    float rate=angle*kTurnGain;
    if(rate>kTurnRate)rate=kTurnRate;
    if(sinA>1e-4f) {
        for(int i=0;i<3;++i)s.ang[i]=axis[i]/sinA*rate;
    } else if(Dot(nose,want)<0.0f) {
        s.ang[0]=0.0f;s.ang[1]=kTurnRate;s.ang[2]=0.0f;   // dead astern: turn about up
    } else {
        s.ang[0]=s.ang[1]=s.ang[2]=0.0f;
    }
    // Roll: the right side down (right.y < 0) is undone by turning about the bow.
    for(int i=0;i<3;++i)s.ang[i]-=nose[i]*right[1]*kRollGain;
}

// Where the bow wants to point: the target (from the turrets, pitch within kMaxPitch), else the way it
// moves, else as it is (level).
void Heading(const Sub& s,const float* pos,const float* m,float* want) noexcept {
    if(s.hasTarget) {
        float d[3]={s.target[0]-pos[0],s.target[1]-(pos[1]+kGunHeight),s.target[2]-pos[2]};
        const float flat=std::sqrt(d[0]*d[0]+d[2]*d[2]);
        if(flat>1.0f) {
            const float pitch=Clamp(std::atan2(d[1],flat),-kMaxPitch,kMaxPitch);
            want[0]=d[0]/flat*std::cos(pitch);want[1]=std::sin(pitch);want[2]=d[2]/flat*std::cos(pitch);
            return;
        }
    }
    float flat[3]={s.lin[0],0.0f,s.lin[2]};
    if(s.moving && Len(flat)>2.0f && Normalize(flat)){std::memcpy(want,flat,12);return;}
    float nose[3]={m[8],0.0f,m[10]};
    if(!Normalize(nose)){nose[0]=0.0f;nose[2]=1.0f;}
    std::memcpy(want,nose,12);
}

void Fire(Sub& s,unsigned char* v,const float* pos,const float* m,ULONGLONG ms) noexcept {
    std::int32_t guns=0,missiles=0,locked=0;
    Arm(s,v,ms,&guns,&missiles,&locked);
    bool gun=false,missile=false;
    if(s.hasTarget && cfg.heliFire) {
        const float up[3]={m[4],m[5],m[6]};
        const float muzzle[3]={pos[0]+up[0]*kGunHeight,pos[1]+up[1]*kGunHeight,pos[2]+up[2]*kGunHeight};
        float nose[3]={m[8],m[9],m[10]};
        if(!Normalize(nose)){nose[0]=0.0f;nose[1]=0.0f;nose[2]=1.0f;}
        const float d[3]={s.target[0]-muzzle[0],s.target[1]-muzzle[1],s.target[2]-muzzle[2]};
        const float dist=Len(d);
        const float off=dist>1.0f ? std::acos(Clamp(Dot(d,nose)/dist,-1.0f,1.0f)) : 0.0f;
        const float path[3]={muzzle[0]+nose[0]*kGunRange,muzzle[1]+nose[1]*kGunRange,muzzle[2]+nose[2]*kGunRange};
        gun=guns>0 && dist<kGunRange && off<kGunCone && !FriendInLine(muzzle,path,v);
        missile=missiles>0 && locked>0 && dist>kMissileMin && dist<kMissileRange && ms-s.missileAt>kMissileMs &&
                !FriendInLine(muzzle,s.target,v);
        if(missile) {
            s.missileAt=ms;
            if(cfg.debug)Log("SUB v=%p missiles: %.0f m, %d locked, %d left",v,dist,locked,missiles);
        }
    }
    v[kFireGun]=gun;v[kFireMissile]=missile;
}

// A gauge stand-in: at body-frame `at`, hp of max.
void Gauge(unsigned char* p,const float* m,const float* at,float max,float hp) noexcept {
    Put<void*>(p,kFollowers,&noFollowers);
    float w[4]={0.0f,0.0f,0.0f,1.0f};
    World(m,at,w);
    std::memcpy(p+kPosition,w,16);
    const float top=max>1.0f ? max : 1.0f;
    Put<float>(p,kHpMax,top);
    Put<float>(p,kHp,Clamp(hp,0.0f,top));
}
// The gauges' stand-ins for carrier i: the hull's over the tower, then each part's.
void Proxy(int i,const Sub& s,const unsigned char* v,const float* m) noexcept {
    const float tower[3]={0.0f,kTop,0.0f};
    Gauge(proxies[i][0],m,tower,At<float>(v,kHpMax),At<float>(v,kHp));
    for(int k=0;k<kSystemCount;++k)Gauge(proxies[i][1+k],m,kSystems[k].gauge,kSystems[k].hp,kSystems[k].hp-s.wear[k]);
}

int SubFault(const EXCEPTION_POINTERS* e) noexcept {
    const auto r=e->ExceptionRecord;
    Log("SUB the game faulted building it (%08lX at EDF+%llX): the carrier is off until the game restarts",r->ExceptionCode,
        static_cast<unsigned long long>(static_cast<const unsigned char*>(r->ExceptionAddress)-image));
    broken=true;preloaded=false;
    return EXCEPTION_EXECUTE_HANDLER;
}

unsigned char* CreateSub(const float* m,InitParam* param) noexcept {
    __try { return reinterpret_cast<CreateObjectFn>(image+kCreateObject)(At<void*>(image,kObjectMgr),m,kSubSgo,param); }
    __except(SubFault(GetExceptionInformation())) { return nullptr; }
}

// The heli's "body" part (jet.cpp FixBodyPart): the crash step reads its index unchecked.
void FixBodyPart(unsigned char* v) noexcept {
    if(!bodyPartOk || At<std::int32_t>(v,kBodyPart)!=-1)return;
    const auto i=reinterpret_cast<FindPartFn>(image+kFindPart)(v+kParts,L"body");
    if(i>=0)Put<std::int32_t>(v,kBodyPart,i);
    else Log("SUB v=%p has no body part (going down it would crash)",v);
}

void __fastcall PhysicsHook(void* vehicle) {
    nextPhysics(vehicle);
    __try {
        auto v=static_cast<unsigned char*>(vehicle);
        const ULONGLONG ms=GameMs();
        Sub* s=FindSub(v,ms);
        if(!s || !s->ready || v[kDead] || ms-s->seen>200 || !IsSub(v))return;
        const auto body=At<void*>(v,kBody);
        if(!body)return;
        alignas(16) float lin[4]={s->lin[0],s->lin[1],s->lin[2],0.0f},ang[4]={s->ang[0],s->ang[1],s->ang[2],0.0f};
        reinterpret_cast<SetVecFn>(image+kSetLinearVelocity)(body,lin);
        reinterpret_cast<SetVecFn>(image+kSetAngularVelocity)(body,ang);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// The follower gauges as the game draws them, then one per live carrier (see the top).
void __fastcall GaugeHook(void* hud,void* viewProj,void* owner,void* r9,void* fifth) {
    const auto draw=reinterpret_cast<GaugeFn>(image+kGaugeFn);
    draw(hud,viewProj,owner,r9,fifth);
    __try {
        alignas(16) unsigned char stand[kProxySize]{};
        Node head{};head.next=&head;head.prev=&head;
        Node nodes[kMaxSubs*kGauges]{};
        const ULONGLONG tick=GetTickCount64();
        int n=0;
        for(int i=0;i<kMaxSubs;++i) {
            const Sub& s=subs[i];
            if(!s.vehicle || !s.ready || tick-s.gaugeTick>kGaugeMs)continue;
            for(int g=0;g<kGauges;++g) {
                Node& node=nodes[n++];
                node.object=proxies[i][g];
                node.prev=head.prev;node.next=&head;
                head.prev->next=&node;head.prev=&node;
            }
        }
        if(!n)return;
        Put<void*>(stand,kFollowers,&head);
        draw(hud,viewProj,stand,r9,fifth);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

const unsigned char kPhysicsSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8};
const unsigned char kSetLinSig[]={0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00,0x45,0x33,0xC9,0x4C,0x8B,0x50,0x58,0x49,0x8B,0x42,0x18,0x49,0x8D,0x4A,0x18,0x48,0xFF,0xA0,0xA8,0x00,0x00};
const unsigned char kSetAngSig[]={0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00,0x45,0x33,0xC9,0x4C,0x8B,0x50,0x58,0x49,0x8B,0x42,0x18,0x49,0x8D,0x4A,0x18,0x48,0xFF,0xA0,0xB0,0x00,0x00};
const unsigned char kDeleteSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48};
const unsigned char kPreloadSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
const unsigned char kCreateObjectSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9};
const unsigned char kSetTeamSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x41};
const unsigned char kFindPartSig[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x56,0x48,0x83,0xEC,0x50};
struct Sig { unsigned rva; unsigned char bytes[8]; std::size_t size; };
// The drawer and what it reads of the owner and of each object (docs/subcarrier-re.md §4).
const Sig kGaugeSigs[]={
    {kGaugeFn,{0x4C,0x8B,0xDC,0x55,0x53,0x56,0x57,0x41},8},
    {0x804329,{0x49,0x8B,0xB8,0x50,0x05,0x00,0x00},7},          // mov rdi,[r8+550h]: the owner's followers
    {0x8043F4,{0x0F,0x10,0x90,0x90,0x00,0x00,0x00},7},          // movups xmm2,[rax+90h]: the object's position
    {0x804633,{0xF3,0x0F,0x10,0x90,0xF8,0x02,0x00,0x00},8},     // its HP
    {0x80463B,{0xF3,0x0F,0x5E,0x90,0xF4,0x02,0x00,0x00},8},     // over its max HP
    {kGaugeCall,{0xE8,0x4E,0x00,0x00,0x00},5},
    {kGaugeDraw,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74},8},
};
// The damage path (docs/subcarrier-re.md §8) the hook stands on.
const Sig kDamageSigs[]={
    {kMessage506,{0x48,0x89,0x5C,0x24,0x08,0x55,0x56,0x57},8},
    {0x652E8E,{0x81,0xFA,0x25,0x00,0x00,0x10},6},               // cmp edx,10000025h: the rest to 0x62ECB0
    {0x543AB9,{0xC7,0x45,0x6F,0x00,0x00,0x00,0x10},7},          // the flush's message: 10000000h...
    {0x543ADE,{0xFF,0x50,0x48},3},                              // ...sent through slot 9
    {0x54A586,{0xE8,0xA5,0xD6,0xFF,0xFF},5},                    // 0x54A530, that message: call 0x547C30
    {0x547C70,{0x4C,0x8B,0xEA},3},                              // mov r13,rdx: the GameDamageInfo
    {0x547DB7,{0x49,0x63,0x45,0x24},4},                         // its team
    {0x547DBF,{0x4C,0x63,0x87,0x14,0x03,0x00,0x00},7},          // the object's team (+0x314)
    {0x548109,{0xF3,0x41,0x0F,0x10,0x75,0x50},6},               // its damage
};
// Whether the vtable at RVA `vtable` is of the class `rtti` (its CompleteObjectLocator's TypeDescriptor name).
bool ClassIs(unsigned vtable,const char* rtti) noexcept {
    const auto col=At<const unsigned char*>(image,vtable-8);
    if(col<image || col>=image+kImageSpan || !Readable(col,0x10))return false;
    const unsigned char* name=image+At<std::uint32_t>(col,0xC)+0x10;
    const std::size_t n=std::strlen(rtti)+1;
    return Readable(name,n) && std::memcmp(name,rtti,n)==0;
}
}  // namespace

bool IsSub(const void* vehicle) noexcept {
    const auto v=static_cast<const unsigned char*>(vehicle);
    return Readable(v,kSpeedGain+4) && At<const unsigned char*>(v,0)==image+kHeli506 && At<float>(v,kSpeedGain)==kSubMark;
}

namespace {
// The rigid box (testrange/gen.py 'edf6tr_sub_carrier_mission' rigid): centre (0, 13.25, -7.58), half sizes
// (121, 179.83, 832) in the body frame, so its top, the main deck the model has at y≈193 (§1.1), is kDeckTop
// over the origin. The bow (z > 280) is flat there and about ±69 wide: a deck point is taken on it, between
// kDeckAft and kDeckFore along the nose and within kDeckSide of the keel line, nearest to the asker.
constexpr float kDeckTop=13.25f+179.83f,kBoxHalfX=121.0f,kBoxHalfZ=832.0f,kBoxCentreZ=-7.58f;
constexpr float kDeckAft=320.0f,kDeckFore=700.0f,kDeckSide=40.0f;

// The live carrier nearest (horizontally) to `from`, or nullptr; `dist` gets the distance.
const Sub* NearestSub(const float* from,float* dist) noexcept {
    const ULONGLONG ms=GameMs();
    const Sub* best=nullptr;
    for(const auto& s:subs) {
        if(!s.vehicle || ms-s.seen>kStaleMs || !Readable(s.vehicle,kSpeedGain+4) || s.vehicle[kDead] || SelfCtrl(s.vehicle)!=s.ctrl)continue;
        const float* p=reinterpret_cast<const float*>(s.vehicle+kPosition);
        const float d=std::sqrt((p[0]-from[0])*(p[0]-from[0])+(p[2]-from[2])*(p[2]-from[2]));
        if(!best || d<*dist){best=&s;*dist=d;}
    }
    return best;
}
}  // namespace

bool SubDeck(const float* from,float* deck) noexcept {
    __try {
        float dist=0.0f;
        const Sub* s=NearestSub(from,&dist);
        if(!s)return false;
        const float* m=reinterpret_cast<const float*>(s->vehicle+kMatrix);
        const float* pos=m+12;
        const float rel[3]={from[0]-pos[0],from[1]-pos[1],from[2]-pos[2]};
        const float x=Clamp(Dot(rel,m),-kDeckSide,kDeckSide),z=Clamp(Dot(rel,m+8),kDeckAft,kDeckFore);
        for(int i=0;i<3;++i)deck[i]=pos[i]+m[i]*x+m[4+i]*kDeckTop+m[8+i]*z;
        return std::isfinite(deck[0]+deck[1]+deck[2]);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

float SubHullGap(const float* p) noexcept {
    __try {
        float dist=0.0f;
        const Sub* s=NearestSub(p,&dist);
        if(!s)return -1.0f;
        const float* m=reinterpret_cast<const float*>(s->vehicle+kMatrix);
        const float rel[3]={p[0]-m[12],0.0f,p[2]-m[14]};
        float right[3]={m[0],0.0f,m[2]},nose[3]={m[8],0.0f,m[10]};
        if(!Normalize(right) || !Normalize(nose))return -1.0f;
        const float x=std::fabs(Dot(rel,right))-kBoxHalfX,z=std::fabs(Dot(rel,nose)-kBoxCentreZ)-kBoxHalfZ;
        const float ox=x>0.0f ? x : 0.0f,oz=z>0.0f ? z : 0.0f;
        return std::sqrt(ox*ox+oz*oz);
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1.0f;}
}

void PreloadSub() noexcept {
    __try {
        preloaded=false;
        for(auto& s:subs)s=Sub{};
        if(!spawnOk || broken)return;
        const auto mgr=At<void*>(image,kPreloadMgr);
        preloaded=mgr && FilesThere();
        if(preloaded)reinterpret_cast<PreloadFn>(image+kPreload)(mgr,kSubSgo,2,-1);
        Log("SUB preload carrier=%d",preloaded);
    } __except(EXCEPTION_EXECUTE_HANDLER){preloaded=false;}
}

unsigned char* SubLaunch(const float* pos,const float* heading) noexcept {
    __try {
        const ULONGLONG ms=GameMs();
        if(!spawnOk || !preloaded || !pos || !heading || !At<void*>(image,kObjectMgr))return nullptr;
        if(LiveSubs(ms)>=kMaxSubs){Log("SUB launch: %d carriers out already",kMaxSubs);return nullptr;}
        float fwd[3]={heading[0],0.0f,heading[2]};
        if(!Normalize(fwd)){fwd[0]=0.0f;fwd[2]=1.0f;}
        // Only where there is water: on a map without any, or over land, nothing comes (the call fails).
        // Afloat its origin is kDraft under the surface (and Drive holds it there: s.floor); with the probe
        // off (unknown) it surfaces on the ground as before.
        float surface=0.0f;
        const Sea sea=SeaAt(pos[0],pos[2],&surface);
        if(sea==Sea::land){Log("SUB launch: no water at (%.0f,%.0f): the carrier stays away",pos[0],pos[2]);return nullptr;}
        float start[3]={pos[0],pos[1]+kHullBottom+kClear,pos[2]};
        float ground=0.0f;
        if(GroundUnder(pos,fwd,&ground))start[1]=ground+kHullBottom+kClear;
        if(sea==Sea::water && start[1]<surface-kDraft)start[1]=surface-kDraft;
        // Rows right, up, forward, position (jet.cpp Launch).
        alignas(16) const float m[16]={fwd[2],0,-fwd[0],0, 0,1,0,0, fwd[0],0,fwd[2],0, start[0],start[1],start[2],1};
        InitParam param{image+kInitParamVtable,{}};
        unsigned char* v=CreateSub(m,&param);
        if(!v)return nullptr;
        FixBodyPart(v);
        reinterpret_cast<SetTeamFn>(image+kSetTeam)(v,kTeamFriend,true);
        reinterpret_cast<RideAiFn*>(At<void**>(v,0))[kSlotRideAi](v,true);
        if(!IsSub(v)) {
            Log("SUB launch: %p is no carrier (mark %.0f): deleted",v,At<float>(v,kSpeedGain));
            reinterpret_cast<DeleteFn>(image+kDelete)(v);
            return nullptr;
        }
        Sub* s=FreeSub(v,ms);
        if(s) {
            *s=Sub{};s->vehicle=v;s->ctrl=SelfCtrl(v);s->seen=s->bornAt=ms;s->launched=true;
            s->sea=sea==Sea::water;s->floor=s->sea ? surface-kDraft : -INFINITY;
            QueryPerformanceCounter(&s->last);
            std::memcpy(s->post,start,12);
        }
        Thicken(v);
        Log("SUB v=%p launched at (%.0f,%.0f,%.0f) heading (%.2f,%.2f) %s hp=%.0f driver=%d",v,start[0],start[1],start[2],fwd[0],fwd[2],
            sea==Sea::water ? "afloat" : "on the ground (water unknown)",At<float>(v,kHp),SeatCount(v)>0 && SeatRider(SeatAt(v,0))==Rider::dummy);
        return v;
    } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}

void SubFrame(unsigned char* v) noexcept {
    if(!physicsOk)return;
    const ULONGLONG ms=GameMs();
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    Sub* s=FindSub(v,ms);
    if(!s) {
        s=FreeSub(v,ms);
        if(!s)return;   // kMaxSubs out: this one is left to the game
        *s=Sub{};s->vehicle=v;s->ctrl=SelfCtrl(v);s->bornAt=ms;
        QueryPerformanceCounter(&s->last);
        std::memcpy(s->post,pos,12);
        s->floor=pos[1];
        Log("SUB v=%p crewed (placed by the mission) at y=%.0f: hp=%.0f/%.0f",v,pos[1],At<float>(v,kHp),At<float>(v,kHpMax));
        Thicken(v);
    }
    s->seen=ms;s->gaugeTick=GetTickCount64();
    LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
    const float dt=Clamp(static_cast<float>(now.QuadPart-s->last.QuadPart)/static_cast<float>(freq.QuadPart),0.004f,0.1f);
    s->last=now;
    // The stock input stays out of it, the move-area clamp too (it would teleport the hull back).
    Put<float>(v,kInLateral,0.0f);Put<float>(v,kInForward,0.0f);Put<float>(v,kInYaw,0.0f);
    Put<float>(v,kAreaInset,kNoInset);
    float nose[3]={m[8],0.0f,m[10]};
    if(!Normalize(nose)){nose[0]=0.0f;nose[2]=1.0f;}
    Nearest prey{{pos[0],pos[1],pos[2]},kRange,{0,0,0},false};
    VisitEnemies(v,&SeeEnemy,&prey);
    s->hasTarget=prey.found;
    if(prey.found)std::memcpy(s->target,prey.aim,12);
    Follow(*s,dt);
    Drive(*s,pos,nose,dt);
    float want[3];
    Heading(*s,pos,m,want);
    Steer(*s,m,want);
    s->ready=true;
    Repair(*s,ms);
    Fire(*s,v,pos,m,ms);
    Bay(*s,m,ms);
    HitLog(*s,ms);
    Proxy(static_cast<int>(s-subs),*s,v,m);
    if(cfg.debug && ms-s->logAt>2000) {
        s->logAt=ms;
        float ground=0.0f;
        const bool seen=GroundUnder(pos,nose,&ground);
        Log("SUB v=%p pos=(%.0f,%.0f,%.0f) clear=%.1f post=(%.0f,%.0f)%s vel=(%.1f,%.1f,%.1f) yawRate=%.1f target=%s%.0f hp=%.0f/%.0f fire=%d/%d",
            v,pos[0],pos[1],pos[2],seen ? pos[1]-kHullBottom-ground : -1.0f,s->post[0],s->post[2],s->moving ? " moving" : "",
            s->lin[0],s->lin[1],s->lin[2],s->ang[1]*180.0f/kPi,s->hasTarget ? "yes " : "no ",
            s->hasTarget ? prey.best : 0.0f,At<float>(v,kHp),At<float>(v,kHpMax),v[kFireGun],v[kFireMissile]);
    }
}

bool InstallSub() noexcept {
    __try {
        const bool sig=Matches(0x61B710,kPhysicsSig,sizeof(kPhysicsSig)) && Matches(kSetLinearVelocity,kSetLinSig,sizeof(kSetLinSig)) &&
                       Matches(kSetAngularVelocity,kSetAngSig,sizeof(kSetAngSig)) && Matches(kDelete,kDeleteSig,sizeof(kDeleteSig));
        if(!sig){Log("SUB profile mismatch: carriers off");return false;}
        // Slot 57 of the 506, after whatever is there (jet.cpp's hook: InstallJets runs first).
        const auto slot=reinterpret_cast<void**>(image+kHeli506)+kSlotPhysics;
        void* const current=*slot;
        nextPhysics=reinterpret_cast<PhysicsFn>(current);
        physicsOk=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&PhysicsHook));
        spawnOk=physicsOk && Matches(kPreload,kPreloadSig,sizeof(kPreloadSig)) && Matches(kCreateObject,kCreateObjectSig,sizeof(kCreateObjectSig)) &&
                Matches(kSetTeam,kSetTeamSig,sizeof(kSetTeamSig)) && Readable(image+kInitParamVtable,8);
        bodyPartOk=spawnOk && Matches(kFindPart,kFindPartSig,sizeof(kFindPartSig));
        if(!bodyPartOk)spawnOk=false;   // going down without its body part crashes (jet.cpp)
        bool gauge=Readable(image+kGaugeHud+3*8,8) && At<const unsigned char*>(image,kGaugeHud+3*8)==image+kGaugeDraw;
        for(const auto& g:kGaugeSigs)gauge=gauge && Matches(g.rva,g.bytes,g.size);
        noFollowers.next=&noFollowers;noFollowers.prev=&noFollowers;
        bool changed=false;
        gaugeOk=gauge && RedirectCall(image+kGaugeCall,image+kGaugeFn,reinterpret_cast<void*>(&GaugeHook),changed);
        // Slot 9 of the 506, after whatever is there (no other hook takes it today).
        bool damage=physicsOk;
        for(const auto& d:kDamageSigs)damage=damage && Matches(d.rva,d.bytes,d.size);
        const auto message=reinterpret_cast<void**>(image+kHeli506)+kSlotMessage;
        void* const was=*message;
        if(damage && was!=image+kMessage506)Log("SUB damage: 506 slot 9 is %p, not EDF+%X: chained onto it",was,kMessage506);
        nextMessage=reinterpret_cast<MessageFn>(was);
        damageOk=damage && PatchVtableSlot(message,was,reinterpret_cast<void*>(&MessageHook));
        int heavy=0;
        for(int k=0;k<kHeavyCount;++k) {
            heavyOk[k]=ClassIs(kHeavy[k].vtable,kHeavy[k].rtti);
            heavy+=heavyOk[k] ? 1 : 0;
            if(!heavyOk[k])Log("SUB heavy source %s: no such vtable at EDF+%X",kHeavy[k].rtti,kHeavy[k].vtable);
        }
        Log("HOOK sub physics=%d spawn=%d gauge=%d damage=%d heavy=%d/%d (chained physics onto %p)",physicsOk,spawnOk,gaugeOk,damageOk,
            heavy,kHeavyCount,current);
        return physicsOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
