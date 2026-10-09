// The submarine carrier (docs/subcarrier-re.md): the 潜水母艦 of the missions (Pandora / Epimetheus /
// Siren: the mission object EV603_MARINE, a FarEventObject with no HP, no collision and no weapons of its
// own) made a friendly NPC vehicle. Like the jets (jet.cpp) it is a Vehicle506_Helicopter body from a
// derived SGO (tools/make_sub.py EDF6VC_SUB_CARRIER.SGO, testrange/gen.py 'edf6tr_sub_carrier_mission'):
// the stock HP (+0x2F4 / +0x2F8), the crash and wreck, the seat weapons, and the hull's rigid box, which
// is what bullets hit and what soldiers stand on. Its model is EV603_MARINE at the missions' size (1664 m),
// told apart by its mark (body506.cpp BodyOf: PluginBody::sub).
//
// Who drives it is the plugin's own table (subs[], one entry per carrier, by ObjRef): an entry is made when the
// plugin launches a carrier (SubLaunch) or first sees a mission's (Adopt), and dropped only at a new mission
// (ResetSubs) or when its object is dead or gone; nothing about its seats decides it. Each game frame, once
// (Tick), from its input stage (SubFrame, from HeliFrame while its seat-0 NPC driver is aboard), or, while that
// stage does not come (the driver gone), from its physics step (506 slot 57, body506.cpp -> SubBodyStep, every
// frame the body exists), it is driven: it sits surfaced (afloat kDraft under the water's surface, or its hull bottom kClear over the
// highest ground under it), holds its post and follows the player at a ship's pace once they are kLeash away,
// turns its bow onto the nearest enemy on land (afloat it keeps its heading: people stand on its deck), aims
// its two turrets (the tilt bones its guns hang on) at the enemy within their arc, fires them (0x2020) while
// a barrel is on it and the homing missiles (0x2021) once the game has locked a target; empty weapons are
// reloaded aboard after kReloadMs. The fire bytes are written in the input stage (after the stock input, which
// owns them); with no input stage (the driver gone) the physics step writes them too, and says so once.
// Its HP is shown by hud.cpp (HudDraw): a bar over the tower for the hull and one over each deck part, drawn
// from the game's follower gauge call (HudPlayer_FollowerDurability 0x8040E0 -> 0x804300, hooked at kGaugeCall:
// the HUD's draw pass, its view-projection and context). The draw runs on another thread: it reads only what the
// game thread published last (Publish / Latest, a triple buffer), never the carriers' entries.
// Its HP is split into the hull and four deck parts (kSystems: two turrets, the missile bay, the drone bay),
// docs/subcarrier-re.md §8: every hit reaches it as the damage message through the 506's slot 9 (body506.cpp
// hooks it, SubMessage). A hit within a part's reach wears that part (its own HP, its own gauge) and not the
// hull; any other hit on the hull counts only from a heavy source (kHeavy) or a single hit of
// Cfg().subHeavyHit. A worn-out part stops working until the crew repairs it kRepairMs on. The drone bay
// launches jet.cpp's gun drones (JetLaunchDrone) while it has a target.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "subcarrier.h"
#include "body506.h"
#include "memory.h"
#include "online_authority.h"
#include "vecmath.h"
#include <cmath>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
using vec::Approach;using vec::Clamp;using vec::Cross;using vec::Dot;using vec::Len;using vec::Normalize;
using vec::ToLocal;using vec::ToWorld;
constexpr unsigned kDelete=0x118A1B0;
constexpr std::size_t kInLateral=0x1540,kInForward=0x1548,kInYaw=0x1550;
constexpr std::size_t kFireGun=0x2020,kFireMissile=0x2021;
constexpr std::size_t kHpMax=0x2F4,kHp=0x2F8;
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kHolderWeapon=0x10;
constexpr std::size_t kWeaponLockon=0x6B0,kWeaponAmmo=0xBE8;
constexpr std::size_t kWeaponLockRange=0x6D0,kWeaponLocked=0xC68,kWeaponSpeed=0x894,kWeaponAlive=0x898;
constexpr std::int32_t kHoming=1;
// A weapon's muzzles (heli.cpp MuzzleFrame, H for the 410's guns): each {bone record, local 4x4 at +0x10, mode
// +0xE0}; the round leaves at row 3 of local x bone world (+0xB0), along row 2 of the bone's (mode != 0) or of the
// weapon's own matrix (+0x150, mode 0).
constexpr std::size_t kMuzzles=0x1D0,kMuzzleCount=0x1E0,kMuzzleStride=0xF0,kMuzzleLocal=0x10,kBoneRows=0xB0;
constexpr std::size_t kMuzzleMode=0xE0,kWeaponRows=0x150;
constexpr std::int32_t kModeWeaponRows=0;
constexpr std::size_t kAreaInset=0xE00;   // jet.cpp: the move-area clamp's inset; kNoInset = no clamp
constexpr float kNoInset=-1.0e6f;
constexpr std::size_t kObjFlags=0x18,kCtrlUses=8;
constexpr unsigned char kObjDeleted=4;
constexpr unsigned kPreload=0x7A3780,kCreateObject=0x11945E0,kInitParamVtable=0x1762068;   // SetTeam: crew.h kSetTeam
constexpr std::size_t kPreloadMgr=0x20B29A8,kObjectMgr=0x20B2958;
constexpr std::int32_t kTeamFriend=2;
// The gauge call (docs/subcarrier-re.md §4): the follower HUD's draw (vtable 0x17F6C08 slot 3) calls the
// drawer at kGaugeCall: (hud, view-projection, owner, r9 = draw context, 5th = viewport). The plugin's HUD is
// drawn right after it (GaugeHook), in the same pass.
constexpr unsigned kGaugeHud=0x17F6C08,kGaugeDraw=0x8040E0,kGaugeCall=0x8042AD,kGaugeFn=0x804300;
// GameDamageInfo (docs/subcarrier-re.md §8.1): the attacker (weak_ptr: object, control block), its team, where it
// hit (the round's position, or the blast's centre), the damage.
constexpr std::size_t kDmgAttacker=0x10,kDmgAttackerCtrl=0x18,kDmgTeam=0x24,kDmgAt=0x30,kDmgAmount=0x50;
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

const wchar_t* const kSubSgo=L"app:/object/edf6vc_sub_carrier.sgo";
// What SubLaunch needs in Mods (tools/make_sub.py): the SGO, its model, the turret guns, the missile.
const wchar_t* const kSubFiles[]={L"\\Mods\\OBJECT\\EDF6VC_SUB_CARRIER.SGO",L"\\Mods\\OBJECT\\EDF6VC_SUB.MRAB",
                                  L"\\Mods\\WEAPON\\EDF6VC_JET_GUN_L.SGO",L"\\Mods\\WEAPON\\EDF6VC_JET_GUN_R.SGO",
                                  L"\\Mods\\WEAPON\\EDF6VC_ESSM_32.SGO"};   // vcobjects.store_file('ESSM', 32)
constexpr int kMaxSubs=3;                     // M123: three carriers attack at once (BE151_157)

// The hull (the SGO's box, the model at its own size): its bottom kHullBottom under the body origin,
// kHalfLength fore and aft; the tower top kTop over it; the turrets' guns kGunHeight over it.
constexpr float kHullBottom=-(kSubBoxCentreY-kSubBoxHalfY),kHalfLength=790.0f,kHalfWidth=116.0f,kTop=366.0f,kGunHeight=241.0f;
// Driving: metres, m/s, m/s^2, rad/s.
constexpr float kClear=0.6f;                 // hull bottom over the highest ground under it
// Afloat, its main deck (kSubDeckTop over the origin) kFreeboard over the water: deeper than the stock
// carrier sits in M082 (deck some 49 m up), the user's ask. Its box is only the slab under the deck, so the
// hull under the water never meets the seabed.
constexpr float kFreeboard=15.0f,kDraft=kSubDeckTop-kFreeboard;
// A player on its deck is up to 830 m from its post: it sets off only past the hull.
constexpr float kLeash=1500.0f,kStop=1000.0f;  // it sets off after a player this far from its post, stops this near
constexpr float kCruise=25.0f,kAccel=3.0f,kClimb=8.0f,kSink=4.0f,kClimbAccel=6.0f,kPosGain=0.2f;
constexpr float kTurnRate=0.05f,kTurnGain=0.8f,kRollGain=1.5f,kMaxPitch=0.05f;   // 3 deg/s, 3 deg
// Combat.
constexpr float kRange=2000.0f;              // it engages enemies this far from it
// The guns fire within their own reach (Sub::gunReach: their SGO's AmmoSpeed x AmmoAlive, less kGunReachIn), the
// missiles within their lock range (Sub::missileReach: the carrier's missile SGO, vcobjects STORES['ESSM'], sets
// it and its wide cone: the missile bay need not face the target).
constexpr float kGunCone=0.07f,kGunReachIn=0.97f;   // 4 deg off the barrel
constexpr float kMissileMin=60.0f;
constexpr ULONGLONG kMissileMs=2500,kReloadMs=12000,kGaugeMs=1000;
constexpr float kPi=3.14159265f;
// The turrets (see Turrets): the arc a barrel turns in (elevation over the deck plane, rad), how fast it turns,
// how near a weapon's barrel must be to a part's pivot to be that part's (metres, body frame).
constexpr float kTurretDip=0.35f,kTurretRise=1.4f,kTurretSlew=1.0f,kMatchReach=60.0f;

// The deck parts: each a capsule (segment a-b, `reach` round it) in the body frame (the model's metres, rows
// right, up, nose; docs/subcarrier-re.md §1.1 / §8), its HP; the seat weapon it is: the one whose barrel is within
// kMatchReach of `pivot` (the bone it hangs on, tools/make_sub.py PLUGIN_BONES), which must be holder `holder`
// (0x2020 fires holders 0 and 1, 0x2021 holder 2: gen.py) and `homing` (-1 holder: no weapon). The turrets'
// capsules run from the deck (the hull box top: nothing above it collides) up to their tilt bones (`bone`, which
// the plugin turns); the missile bay spans missle_l..missle_r (172 m up, under the bow deck at 193); the drone bay
// is a stretch of the after deck the plugin picks (no bone there). Rounds collide with the hull box only (its top
// the deck): what hits a part lands on the deck round it. `gauge` is where its HP gauge stands.
enum System { turretA, turretB, missiles, droneBay, kSystemCount };
struct SystemSpec { const char* name; float a[3],b[3]; float reach,hp; int holder; bool homing; float pivot[3]; const wchar_t* bone; float gauge[3]; };
const SystemSpec kSystems[kSystemCount]={
    {"turretA",{17.7f,kSubDeckTop,-16.3f},{17.7f,229.5f,-16.3f},20.0f,6000.0f,0,false,{17.7f,229.5f,-16.3f},L"gunA_tilt_l",{17.7f,255.0f,-16.3f}},
    {"turretB",{17.7f,kSubDeckTop,-52.2f},{17.7f,245.9f,-52.2f},20.0f,6000.0f,1,false,{17.7f,245.9f,-52.2f},L"gunB_tilt_l",{17.7f,271.0f,-52.2f}},
    {"missiles",{-56.5f,172.0f,592.0f},{56.5f,172.0f,592.0f},30.0f,5000.0f,2,true,{0.0f,172.0f,592.0f},nullptr,{0.0f,215.0f,592.0f}},
    {"dronebay",{0.0f,kSubDeckTop,-560.0f},{0.0f,kSubDeckTop,-640.0f},35.0f,5000.0f,-1,false,{0.0f,0.0f,0.0f},nullptr,{0.0f,215.0f,-600.0f}},
};
constexpr int kTurrets=2;                      // turretA, turretB: the first two systems
constexpr float kBayLaunch[3]={0.0f,253.0f,-600.0f};   // 60 m over the bay
constexpr ULONGLONG kRepairMs=90000;           // a worn-out part is repaired this long on (game time)
constexpr int kBayDrones=4;                    // the drone bay keeps at most this many out...
constexpr ULONGLONG kBayGapMs=10000;           // ...launching one this often while it has a target
constexpr ULONGLONG kHitLogMs=1000;

// A turret's barrel as the plugin turns it: the tilt bone's record and bind pose, the way it points now (body
// frame, unit), whether the gun's muzzles follow the bone (mode != 0) as far as that can be read.
struct Turret { unsigned char* rec; const unsigned char* bones; float bind[16]; float aim[3]; std::int32_t mode; bool posed,logged; };

struct Sub {
    ObjRef ref;
    unsigned char* vehicle;
    ULONGLONG bornAt,missileAt,logAt,tickMs;
    ULONGLONG frame,fireFrame;   // GameFrame of the last tick, fire step
    float gunReach,missileReach; // m: its guns' reach and its missiles' lock range, as their SGOs set them (Arm)
    float tier;                  // its HP over its SGO's (Thicken: the request's tier, pylib/vcobjects.py JET_TIER)
    ULONGLONG inputAt;           // GameMs of the last input stage
    float dt;                     // the game's step of the last tick (s)
    bool noInputLogged;
    float post[3];
    float floor;                  // the lowest its origin goes: a mission's own height for it (-inf: called in)
    float lin[3],ang[3];
    bool moving,ready,launched;
    bool sea;                     // afloat: it keeps to the water (Follow) and its heading
    // Its seat weapons per part (Resolve): the weapon (nullptr: none, or off), resolved from holders `holders`.
    unsigned char* weapon[kSystemCount];
    const void* holders;
    std::uint64_t holderCount;
    bool resolved,waitLogged,offLogged[kSystemCount];
    std::int32_t full[kSystemCount];  // each part's weapon's ammo when first seen
    ULONGLONG emptyAt[kSystemCount];
    Turret turret[kTurrets];
    float target[3];
    bool hasTarget;
    float wear[kSystemCount];         // damage each deck part took: worn out at its hp
    bool down[kSystemCount];          // worn out (since downAt, game ms)
    ULONGLONG downAt[kSystemCount];
    bool bayLogged;                   // the bay's launch failure was logged
    ObjRef drone[kBayDrones];         // the bay's drones out
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
const void* refused=nullptr;          // the last carrier no entry was free for (logged once)

// The gauges (see the top): per carrier its panel (hud.cpp: the hull and each part, where each stands). The game
// thread builds them into `staging` every tick and publishes the whole set; the draw reads the last published one.
const char* const kPartNames[kSystemCount]={"TURRET A","TURRET B","MISSILES","DRONE BAY"};
struct Gauges { CarrierPanel panel; bool shown; };
struct Snapshot { ULONGLONG tick; Gauges sub[kMaxSubs]; };
Gauges staging[kMaxSubs]{};
// The triple buffer: the game thread writes shots[writing] and swaps it in as `latest` (marked kFresh); the draw
// swaps a fresh `latest` for shots[reading]. Each side only ever touches its own and swaps through `latest`, so
// neither reads a snapshot half written. (One drawing thread, the follower gauge's.)
constexpr LONG kFresh=4;
Snapshot shots[3]{};
volatile LONG latest=0;
int writing=1,reading=2;

bool heavyOk[kHeavyCount]{};
bool spawnOk=false,gaugeOk=false,gaugeTried=false,damageOk=false;
bool subOk=false;   // InstallSub ran (the heli profile checked out): carriers are driven at all
// The input stage missed this long (game clock): the physics step drives the carrier (its seat-0 driver gone).
constexpr ULONGLONG kNoInputMs=50;
bool preloaded=false,broken=false;

struct alignas(16) InitParam { const void* vtable; unsigned char rest[0x28]; };
using PreloadFn=void(*)(void*,const wchar_t*,std::int32_t,std::int32_t);
using CreateObjectFn=unsigned char*(*)(void*,const float*,const wchar_t*,InitParam*);
using SetTeamFn=void(*)(void*,std::int32_t,bool);
using DeleteFn=void(*)(void*);
using GaugeFn=void(__fastcall*)(void*,void*,void*,void*,void*);

const void* SelfCtrl(const unsigned char* v) noexcept { return At<const void*>(v,kSelfCtrl); }

// Whether the entry's object is there and alive: its control block still counts it, it is the same object
// (ObjRef), not deleted, not dead. Read under the caller's __try.
bool Live(const Sub& s) noexcept {
    const auto ctrl=static_cast<const unsigned char*>(s.ref.ctrl);
    return s.vehicle && Readable(ctrl,kCtrlUses+4) && At<std::int32_t>(ctrl,kCtrlUses)>0 && Readable(s.vehicle,kTeam+4) &&
           s.ref.Is(s.vehicle) && !(s.vehicle[kObjFlags]&kObjDeleted) && !s.vehicle[kDead];
}
// The entries whose carrier is dead or gone, dropped (with their gauges).
void Sweep() noexcept {
    for(int i=0;i<kMaxSubs;++i) {
        Sub& s=subs[i];
        if(!s.vehicle || Live(s))continue;
        Log("SUB v=%p dropped: %s",s.vehicle,Readable(s.vehicle,kDead+1) && s.ref.Is(s.vehicle) && s.vehicle[kDead] ? "destroyed" : "gone");
        s=Sub{};staging[i].shown=false;
    }
}
Sub* FindSub(const unsigned char* v) noexcept {
    for(auto& s:subs)
        if(s.vehicle==v && s.ref.Is(v))return &s;
    return nullptr;
}
Sub* FreeSub() noexcept {
    Sweep();
    for(auto& s:subs)if(!s.vehicle)return &s;
    return nullptr;
}
int LiveSubs() noexcept {
    Sweep();
    int n=0;
    for(const auto& s:subs)n+=s.vehicle ? 1 : 0;
    return n;
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

// A part's HP: its own at the base tier, times the carrier's (Thicken).
float PartHp(const Sub& s,int k) noexcept { return kSystems[k].hp*(s.tier>1.0f ? s.tier : 1.0f); }

void Wear(Sub& s,int k,float dmg) noexcept {
    s.partDmg+=dmg;
    s.wear[k]+=dmg;
    if(s.wear[k]<PartHp(s,k))return;
    s.wear[k]=PartHp(s,k);s.down[k]=true;s.downAt[k]=GameMs();
    Log("SUB v=%p part %s destroyed (the crew repairs it in %llus)",s.vehicle,kSystems[k].name,kRepairMs/1000);
}

// A damage message to a carrier, routed (see the top): its damage cut to 0 when a part takes it or the hull holds it
// off. Returns where to put the damage back after the stock handler (`was`), or nullptr: the message as it came.
float* Route(unsigned char* v,unsigned char* gdi,float* was) noexcept {
    if(!damageOk || v[kDead] || !Readable(gdi,kDmgAmount+4,true))return nullptr;
    Sub* s=FindSub(v);
    const float dmg=At<float>(gdi,kDmgAmount);
    // Heals (< 0) and friends' rounds (the stock rule) are left alone.
    if(!s || !s->ready || !(dmg>0.0f) || !std::isfinite(dmg) || !Hostile(v,At<std::int32_t>(gdi,kDmgTeam)))return nullptr;
    float at[3];
    ToLocal(reinterpret_cast<const float*>(v+kMatrix),reinterpret_cast<const float*>(gdi+kDmgAt),at);
    const int part=PartAt(*s,at);
    const std::uintptr_t from=Attacker(gdi);
    const bool heavy=HeavySource(from) || (Cfg().subHeavyHit>0.0f && dmg>=Cfg().subHeavyHit);
    ++s->hits;s->lastDmg=dmg;s->lastPart=part;s->lastFrom=from;s->lastHeavy=heavy;
    std::memcpy(s->lastAt,at,12);
    if(part<0 && heavy){s->hullDmg+=dmg;return nullptr;}
    if(part>=0)Wear(*s,part,dmg);
    else s->heldDmg+=dmg;
    *was=dmg;
    Put<float>(gdi,kDmgAmount,0.0f);
    return reinterpret_cast<float*>(gdi+kDmgAmount);
}

// The carrier's tier (its HP over its SGO's kSgoHull: the multiplier its SGO's mission_setup[0] or its request
// gave, 25 at the highest), and its hull made Cfg().subHullHp at the base tier times that (its HP kept in proportion;
// 0: the game's). Its parts' HP scale the same (PartHp). Once per carrier.
constexpr float kSgoHull=30000.0f;   // pylib/vcobjects.py JETS['edf6tr_sub_carrier_mission'].durability
void Thicken(Sub& s,unsigned char* v) noexcept {
    const float max=At<float>(v,kHpMax),hp=At<float>(v,kHp);
    if(!(max>0.0f))return;
    s.tier=max/kSgoHull>1.0f ? max/kSgoHull : 1.0f;
    const float want=Cfg().subHullHp*s.tier;
    if(!(want>0.0f) || want==max){Log("SUB v=%p tier x%.1f, hull hp %.0f",v,s.tier,max);return;}
    Put<float>(v,kHpMax,want);
    Put<float>(v,kHp,hp/max*want);
    Log("SUB v=%p tier x%.1f, hull hp %.0f/%.0f -> %.0f/%.0f",v,s.tier,hp,max,At<float>(v,kHp),want);
}

// Worn-out parts back in order kRepairMs after they wore out.
void Repair(Sub& s,ULONGLONG ms) noexcept {
    for(int k=0;k<kSystemCount;++k) {
        if(!s.down[k] || ms-s.downAt[k]<kRepairMs)continue;
        s.down[k]=false;s.wear[k]=0.0f;
        Log("SUB v=%p part %s repaired (hp %.0f)",s.vehicle,kSystems[k].name,PartHp(s,k));
    }
}

// The drone bay: while it works and the carrier has a target, a gun drone every kBayGapMs, kBayDrones out at most;
// they guard the carrier (their anchor the launch point) and go when jet.cpp withdraws them.
void Bay(Sub& s,const float* m,ULONGLONG ms) noexcept {
    int out=0;
    for(auto& d:s.drone) {
        if(d && !JetFlying(d.obj,d.ctrl))d=ObjRef{};
        out+=d ? 1 : 0;
    }
    if(s.down[droneBay] || !s.hasTarget || out>=kBayDrones || ms-s.droneAt<kBayGapMs)return;
    s.droneAt=ms;
    float from[3];
    ToWorld(m,kBayLaunch,from);
    const float nose[3]={m[8],m[9],m[10]};
    unsigned char* d=JetLaunchDrone(from,nose,from,Cfg().jetFuelSec,s.vehicle,false);
    if(!d) {
        if(!s.bayLogged)Log("SUB v=%p drone bay: no drone launched (JetPilot off, EDF6VC_JET_DRONE.SGO not preloaded, or jets full)",s.vehicle);
        s.bayLogged=true;
        return;
    }
    for(auto& slot:s.drone)
        if(!slot){slot=ObjRef::Of(d);break;}
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

// A weapon's barrel: the mean of its muzzles' frames (heli.cpp Barrel, without its 30 m reach: the carrier's
// turrets are 230 m over its origin), and the first muzzle's mode.
bool Barrel(const unsigned char* weapon,float* pos,float* dir,std::int32_t* mode) noexcept {
    const auto muzzles=At<const unsigned char*>(weapon,kMuzzles);
    const auto count=At<std::uint64_t>(weapon,kMuzzleCount);
    if(count==0 || count>8 || !Readable(muzzles,count*kMuzzleStride))return false;
    std::memset(pos,0,12);std::memset(dir,0,12);
    for(std::uint64_t i=0;i<count;++i) {
        const unsigned char* mz=muzzles+i*kMuzzleStride;
        const auto bone=At<const unsigned char*>(mz,0);
        if(!Readable(bone,kBoneRows+0x40))return false;
        float b[4][4],l[4][4],p[3],f[3];
        std::memcpy(b,bone+kBoneRows,sizeof(b));std::memcpy(l,mz+kMuzzleLocal,sizeof(l));
        for(int c=0;c<3;++c) {
            f[c]=l[2][0]*b[0][c]+l[2][1]*b[1][c]+l[2][2]*b[2][c];
            p[c]=l[3][0]*b[0][c]+l[3][1]*b[1][c]+l[3][2]*b[2][c]+l[3][3]*b[3][c];
        }
        if(i==0)*mode=At<std::int32_t>(mz,kMuzzleMode);
        if(At<std::int32_t>(mz,kMuzzleMode)==kModeWeaponRows)std::memcpy(f,weapon+kWeaponRows+0x20,12);
        for(int c=0;c<3;++c){pos[c]+=p[c];dir[c]+=f[c];}
    }
    for(int c=0;c<3;++c)pos[c]/=static_cast<float>(count);
    return std::isfinite(pos[0]+pos[1]+pos[2]) && Normalize(dir);
}

// Which part each seat weapon is (see kSystems), resolved from where its barrel is, again whenever the holders
// change. A weapon that is not where a part's is, or one whose holder the fire bytes do not fire as that part
// needs, leaves that part without a weapon (it never fires), said once.
void Resolve(Sub& s,unsigned char* v,const float* m) noexcept {
    if(SeatCount(v)==0)return;
    const auto seat=SeatAt(v,0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(s.resolved && holders==s.holders && count==s.holderCount)return;
    for(auto& w:s.weapon)w=nullptr;
    s.resolved=false;
    if(count>8 || !Readable(holders,count*8))return;
    unsigned char* got[kSystemCount]{};
    int found[kSystemCount];
    for(auto& f:found)f=-1;
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,kMuzzleCount+8) || At<std::uint64_t>(w,kMuzzleCount)==0)continue;   // no barrel (the fuel tank): no part's
        float pos[3],dir[3];std::int32_t mode=0;
        if(!Barrel(w,pos,dir,&mode)) {   // its muzzles not built yet: the next frame resolves them all
            if(!s.waitLogged)Log("SUB v=%p weapons: holder %llu has no barrel frame yet",v,static_cast<unsigned long long>(i));
            s.waitLogged=true;
            return;
        }
        float local[3];
        ToLocal(m,pos,local);
        int best=-1;float bestD=kMatchReach;
        for(int k=0;k<kSystemCount;++k) {
            if(kSystems[k].holder<0)continue;
            const float d[3]={local[0]-kSystems[k].pivot[0],local[1]-kSystems[k].pivot[1],local[2]-kSystems[k].pivot[2]};
            if(Len(d)<bestD){bestD=Len(d);best=k;}
        }
        if(best<0)continue;   // nothing of the carrier's parts
        const bool usable=Readable(w,kWeaponLocked+8,true);
        const bool homing=usable && At<std::int32_t>(w,kWeaponLockon)==kHoming;
        if(!usable || static_cast<int>(i)!=kSystems[best].holder || homing!=kSystems[best].homing || found[best]!=-1) {
            if(!s.offLogged[best])Log("SUB v=%p %s: its weapon is holder %llu (homing %d), the fire bytes need holder %d (homing %d): off "
                                      "(tools/make_sub.py writes the weapons in that order)",v,kSystems[best].name,
                                      static_cast<unsigned long long>(i),homing,kSystems[best].holder,kSystems[best].homing);
            s.offLogged[best]=true;found[best]=-2;got[best]=nullptr;
            continue;
        }
        found[best]=static_cast<int>(i);
        got[best]=w;
    }
    for(int k=0;k<kSystemCount;++k)
        if(kSystems[k].holder>=0 && found[k]==-1 && !s.offLogged[k]) {
            Log("SUB v=%p %s: no seat weapon at its bone: off",v,kSystems[k].name);
            s.offLogged[k]=true;
        }
    std::memcpy(s.weapon,got,sizeof(got));
    s.holders=holders;s.holderCount=count;s.resolved=true;
    if(Cfg().debug)Log("SUB v=%p weapons: turretA %p, turretB %p, missiles %p",v,s.weapon[turretA],s.weapon[turretB],s.weapon[missiles]);
}

// The parts' seat weapons: an empty one is refilled to what it held at first kReloadMs after it ran dry, a worn-out
// part's is kept dry; their reach as their SGOs set it (s.gunReach, s.missileReach). Out: each part's ammo, the
// missiles' locks.
void Arm(Sub& s,ULONGLONG ms,std::int32_t* ammoOf,std::int32_t* locked) noexcept {
    *locked=0;
    s.gunReach=s.missileReach=0.0f;
    for(int k=0;k<kSystemCount;++k) {
        ammoOf[k]=0;
        unsigned char* w=s.weapon[k];
        if(!w)continue;
        std::int32_t ammo=At<std::int32_t>(w,kWeaponAmmo);
        if(ammo>s.full[k])s.full[k]=ammo;
        if(s.down[k]) {   // its part worn out: dry, reloaded kReloadMs after the repair
            Put<std::int32_t>(w,kWeaponAmmo,0);s.emptyAt[k]=ms;
            continue;
        }
        if(ammo>0)s.emptyAt[k]=0;
        else if(!s.emptyAt[k])s.emptyAt[k]=ms;
        else if(ms-s.emptyAt[k]>=kReloadMs && s.full[k]>0) {
            ammo=s.full[k];Put<std::int32_t>(w,kWeaponAmmo,ammo);s.emptyAt[k]=0;
            if(Cfg().debug)Log("SUB v=%p %s reloaded aboard: %d",s.vehicle,kSystems[k].name,ammo);
        }
        ammoOf[k]=ammo>0 ? ammo : 0;
        if(!kSystems[k].homing) {
            const float reach=At<float>(w,kWeaponSpeed)*static_cast<float>(At<std::int32_t>(w,kWeaponAlive))*kGunReachIn;
            if(std::isfinite(reach) && (s.gunReach<=0.0f || reach<s.gunReach))s.gunReach=reach;
            continue;
        }
        const auto l=At<std::uint64_t>(w,kWeaponLocked);
        *locked+=l<64 ? static_cast<std::int32_t>(l) : 0;
        const float range=At<float>(w,kWeaponLockRange);
        if(std::isfinite(range) && range>s.missileReach)s.missileReach=range;
    }
}

// Whether the segment a-b (world) passes through the hull box (body frame, subcarrier.h): a round along it would
// hit the carrier itself.
bool HullBlocks(const float* m,const float* a,const float* b) noexcept {
    float la[3],lb[3];
    ToLocal(m,a,la);ToLocal(m,b,lb);
    const float c[3]={0.0f,kSubBoxCentreY,kSubBoxCentreZ},h[3]={kSubBoxHalfX,kSubBoxHalfY,kSubBoxHalfZ};
    float t0=0.0f,t1=1.0f;
    for(int i=0;i<3;++i) {
        const float d=lb[i]-la[i],lo=c[i]-h[i]-la[i],hi=c[i]+h[i]-la[i];
        if(std::fabs(d)<1e-6f){if(lo>0.0f || hi<0.0f)return false;continue;}
        float u=lo/d,w=hi/d;
        if(u>w){const float t=u;u=w;w=t;}
        if(u>t0)t0=u;
        if(w<t1)t1=w;
        if(t0>t1)return false;
    }
    return true;
}

// `cur` (unit) turned toward `want` (unit) by at most `step` rad.
void TurnToward(float* cur,const float* want,float step) noexcept {
    const float c=Clamp(Dot(cur,want),-1.0f,1.0f),angle=std::acos(c);
    if(angle<=step){std::memcpy(cur,want,12);return;}
    float axis[3];
    Cross(cur,want,axis);
    if(!Normalize(axis)){axis[0]=0.0f;axis[1]=1.0f;axis[2]=0.0f;}   // opposite: about up
    float k[3];Cross(axis,cur,k);
    const float co=std::cos(step),si=std::sin(step);
    for(int i=0;i<3;++i)cur[i]=cur[i]*co+k[i]*si;
    Normalize(cur);
}

// Turret t's tilt bone posed so that its +z (the barrel, docs/subcarrier-re.md §1.1: tilt_l +z is the bow) points
// along body-frame `aim`: the bone's parent is taken as the body frame (the pan bone on the levelled body, both bound
// unrotated), its up kept as near the bind's as the aim allows, its row lengths and position the bind's. Rows of a
// bone's local matrix are its axes in its parent (jet.cpp PoseSurfaces).
void Pose(Turret& t,const float* aim) noexcept {
    const float* b=t.bind;
    const float lx=Len(b),ly=Len(b+4),lz=Len(b+8);
    float yb[3]={b[4],b[5],b[6]},zb[3]={b[8],b[9],b[10]},xb[3]={b[0],b[1],b[2]};
    float c[3];Cross(yb,zb,c);
    const float hand=Dot(c,xb)>=0.0f ? 1.0f : -1.0f;
    float z[3]={aim[0],aim[1],aim[2]},x[3],y[3];
    if(!Normalize(z) || !Normalize(yb))return;
    Cross(yb,z,x);
    if(!Normalize(x))return;   // straight up the bind's up: keep the last pose
    for(int i=0;i<3;++i)x[i]*=hand;
    Cross(z,x,y);
    for(int i=0;i<3;++i)y[i]*=hand;
    float o[16];
    for(int i=0;i<3;++i){o[i]=x[i]*lx;o[4+i]=y[i]*ly;o[8+i]=z[i]*lz;}
    o[3]=b[3];o[7]=b[7];o[11]=b[11];
    std::memcpy(o+12,b+12,16);
    std::memcpy(t.rec+kBoneLocal506,o,64);
    t.posed=true;
}

// The turrets: each turns (at most kTurretSlew) toward the target while it is within its arc (kTurretDip under to
// kTurretRise over the deck plane, the line not through the hull), else back to the bow; true (into `on`) for each
// whose barrel, as the game builds its muzzles, points at the target within kGunCone, in range, clear of the hull;
// each barrel's line (its reach along it) into `path` (from, to) where it has one (`hasPath`).
// No pose is written for a turret whose bone is not in the model.
void Turrets(Sub& s,unsigned char* v,const float* m,float dt,bool* on,bool* hasPath,float* path) noexcept {
    const unsigned char* inst=v+kModelInst506;
    const auto bones=At<const unsigned char*>(inst,kInstBones506);
    for(int t=0;t<kTurrets;++t) {
        on[t]=false;
        Turret& tu=s.turret[t];
        if(!bones){tu.bones=nullptr;tu.rec=nullptr;}   // no model (being rebuilt): its bone record went with it
        else if(bones!=tu.bones) {   // a model (again): the bone looked up, its bind pose kept
            tu=Turret{};tu.bones=bones;
            tu.rec=BoneRecord506(inst,kSystems[t].bone);
            if(tu.rec)std::memcpy(tu.bind,tu.rec+kBoneLocal506,64);
            tu.aim[2]=1.0f;
            if(!tu.rec)Log("SUB v=%p %s: no bone %ls in its model: the turret does not turn",v,kSystems[t].name,kSystems[t].bone);
        }
        unsigned char* w=s.weapon[t];
        float pos[3],dir[3];std::int32_t mode=0;
        const bool barrel=w && Barrel(w,pos,dir,&mode);
        hasPath[t]=barrel;
        if(barrel)for(int i=0;i<3;++i){path[t*6+i]=pos[i];path[t*6+3+i]=pos[i]+dir[i]*s.gunReach;}
        float want[3]={0.0f,0.0f,1.0f};   // the bow
        bool engage=false;
        if(barrel && s.hasTarget && !s.down[t]) {
            const float to[3]={s.target[0]-pos[0],s.target[1]-pos[1],s.target[2]-pos[2]};
            float local[3]={Dot(to,m),Dot(to,m+4),Dot(to,m+8)};
            if(Normalize(local)) {
                const float elevation=std::asin(Clamp(local[1],-1.0f,1.0f));
                engage=elevation>=-kTurretDip && elevation<=kTurretRise && Len(to)<s.gunReach && !HullBlocks(m,pos,s.target);
                if(engage)std::memcpy(want,local,12);
            }
        }
        if(tu.rec) {
            TurnToward(tu.aim,want,kTurretSlew*dt);
            Pose(tu,tu.aim);
        }
        if(!barrel || !engage)continue;
        const float d[3]={s.target[0]-pos[0],s.target[1]-pos[1],s.target[2]-pos[2]};
        const float dist=Len(d),off=dist>1.0f ? std::acos(Clamp(Dot(d,dir)/dist,-1.0f,1.0f)) : 0.0f;
        on[t]=off<kGunCone;
        if(!tu.logged && tu.posed && Cfg().debug) {   // what the real game makes of the pose (docs/subcarrier-re.md §6)
            tu.logged=true;
            float aimW[3];
            for(int i=0;i<3;++i)aimW[i]=m[i]*tu.aim[0]+m[4+i]*tu.aim[1]+m[8+i]*tu.aim[2];
            Log("SUB v=%p %s: muzzle mode %d, barrel %.1f deg off the pose",v,kSystems[t].name,mode,
                std::acos(Clamp(Dot(aimW,dir),-1.0f,1.0f))*180.0f/kPi);
        }
    }
}

// Its post follows the player at a ship's pace once they are kLeash from it, until kStop. A called-in
// carrier (afloat: s.sea) keeps to the water: its post does not move onto a spot with none (it waits at
// the shore), and over water it floats kDraft under that spot's surface.
void Follow(Sub& s,float dt) noexcept {
    if(!player.at || GameMs()-player.at>5000)return;
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
// origin at s.floor at least: a mission puts it where it floats. Afloat (s.sea) only the water holds it: its
// origin at s.floor, kDraft under the surface, whatever the ground (a ship does not climb the shore), so the
// ground under its hull is not even looked at.
void Drive(Sub& s,const float* pos,const float* nose,float dt) noexcept {
    float want[3]={(s.post[0]-pos[0])*kPosGain,0.0f,(s.post[2]-pos[2])*kPosGain};
    const float speed=Len(want);
    if(speed>kCruise){want[0]*=kCruise/speed;want[2]*=kCruise/speed;}
    s.lin[0]=Approach(s.lin[0],want[0],kAccel*dt);
    s.lin[2]=Approach(s.lin[2],want[2],kAccel*dt);
    float hold=s.floor,ground=0.0f;
    if(!s.sea && GroundUnder(pos,nose,&ground) && ground+kHullBottom+kClear>hold)hold=ground+kHullBottom+kClear;
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

// Where the bow wants to point: the target (the missile bay faces it, pitch within kMaxPitch), else the way it
// moves, else as it is (level).
// Afloat it keeps its heading and stays level: people stand on its deck, and turning a 1664 m hull onto
// each target swung the deck under them (2026-10-04: ±3 deg/s back and forth, the bow up and down 40 m). The
// turrets turn on their own (Turrets).
void Heading(const Sub& s,const float* pos,const float* m,float* want) noexcept {
    if(s.hasTarget && !s.sea) {
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

// The fire step (see the top): the turrets turned and the fire bytes set, at most once a frame.
void FireStep(Sub& s,unsigned char* v,const float* m,ULONGLONG ms) noexcept {
    if(s.fireFrame==GameFrame())return;
    s.fireFrame=GameFrame();
    Resolve(s,v,m);
    std::int32_t ammo[kSystemCount],locked=0;
    Arm(s,ms,ammo,&locked);
    bool on[kTurrets]{},hasPath[kTurrets]{};
    float path[kTurrets*6]{};
    Turrets(s,v,m,s.dt,on,hasPath,path);
    bool gun=false,missile=false;
    if(s.hasTarget && Cfg().heliFire) {
        // 0x2020 fires both guns: one on the target is enough, so long as neither's line passes a friend.
        bool clear=true,any=false;
        for(int t=0;t<kTurrets;++t) {
            if(ammo[t]<=0)continue;
            any=any || on[t];
            if(hasPath[t])clear=clear && !FriendInLine(path+t*6,path+t*6+3,v);
        }
        gun=any && clear;
        const float* from=reinterpret_cast<const float*>(v+kPosition);
        const float muzzle[3]={from[0]+m[4]*kGunHeight,from[1]+m[5]*kGunHeight,from[2]+m[6]*kGunHeight};
        const float d[3]={s.target[0]-muzzle[0],s.target[1]-muzzle[1],s.target[2]-muzzle[2]};
        const float dist=Len(d);
        missile=ammo[missiles]>0 && locked>0 && dist>kMissileMin && dist<s.missileReach && ms-s.missileAt>kMissileMs &&
                !FriendInLine(muzzle,s.target,v);
        if(missile) {
            s.missileAt=ms;
            if(Cfg().debug)Log("SUB v=%p missiles: %.0f m, %d locked, %d left",v,dist,locked,ammo[missiles]);
        }
    }
    v[kFireGun]=gun;v[kFireMissile]=missile;
}

// Carrier i's panel into the staging set: the hull over the tower, then each deck part over its own place.
void Stage(int i,const Sub& s,const unsigned char* v,const float* m,ULONGLONG ms) noexcept {
    Gauges& g=staging[i];
    CarrierPanel& p=g.panel;
    p.key=v;
    const float tower[3]={0.0f,kTop,0.0f};
    ToWorld(m,tower,p.at);
    p.hullMax=At<float>(v,kHpMax)>1.0f ? At<float>(v,kHpMax) : 1.0f;
    p.hull=Clamp(At<float>(v,kHp),0.0f,p.hullMax);
    p.parts=kSystemCount;
    for(int k=0;k<kSystemCount;++k) {
        const ULONGLONG done=s.downAt[k]+kRepairMs;
        const float left=s.down[k] && done>ms ? static_cast<float>(done-ms)*0.001f : 0.0f;
        auto& part=p.part[k];
        part={kPartNames[k],Clamp(PartHp(s,k)-s.wear[k],0.0f,PartHp(s,k)),PartHp(s,k),left,s.down[k],{}};
        ToWorld(m,kSystems[k].gauge,part.at);
    }
    g.shown=true;
}
// The staging set published whole for the draw (see `latest`).
void Publish() noexcept {
    Snapshot& w=shots[writing];
    std::memcpy(w.sub,staging,sizeof(staging));
    w.tick=GetTickCount64();
    const LONG prev=InterlockedExchange(&latest,writing|kFresh);
    writing=prev&3;
}
// The draw's snapshot: the newest published one.
const Snapshot& Latest() noexcept {
    if(latest&kFresh) {
        const LONG prev=InterlockedExchange(&latest,reading);
        reading=prev&3;
    }
    return shots[reading];
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

// The follower gauges as the game draws them, then the plugin's HUD (hud.cpp: the vehicle readouts, and per carrier
// of the last published snapshot its world bars and its panel). The followers' bars are the one stock HUD piece the
// camera's HUD switch does not reach (0x8040E0 never reads it): while the map holds that switch off they give way too
// (docs/hud-re.md §11).
void __fastcall GaugeHook(void* hud,void* viewProj,void* owner,void* r9,void* fifth) {
    const auto draw=reinterpret_cast<GaugeFn>(image+kGaugeFn);
    bool hide=false;
    __try { hide=MapHidesStockHud(At<const void*>(hud,0x18)); }  // HUiHud's owning camera, as the other stock HUDs use
    __except(EXCEPTION_EXECUTE_HANDLER){}
    if(!hide)draw(hud,viewProj,owner,r9,fifth);
    __try {
        const Snapshot& shot=Latest();
        const bool fresh=GetTickCount64()-shot.tick<=kGaugeMs;   // the game thread still publishing (not paused)
        CarrierPanel panels[kMaxSubs]{};
        int count=0;
        for(int i=0;fresh && i<kMaxSubs;++i)
            if(shot.sub[i].shown)panels[count++]=shot.sub[i].panel;
        HudDraw(static_cast<const float*>(viewProj),r9,fifth,panels,count);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

const unsigned char kDeleteSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48};
const unsigned char kPreloadSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
const unsigned char kCreateObjectSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9};
const unsigned char kSetTeamSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x41};
struct Sig { unsigned rva; unsigned char bytes[8]; std::size_t size; };
// The drawer and its call (docs/subcarrier-re.md §4).
const Sig kGaugeSigs[]={
    {kGaugeFn,{0x4C,0x8B,0xDC,0x55,0x53,0x56,0x57,0x41},8},
    {kGaugeCall,{0xE8,0x4E,0x00,0x00,0x00},5},
    {kGaugeDraw,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74},8},
};
// The damage path (docs/subcarrier-re.md §8) the routing stands on; the 506's slot 9 itself is body506.cpp's.
const Sig kDamageSigs[]={
    {0x543AB9,{0xC7,0x45,0x6F,0x00,0x00,0x00,0x10},7},          // the flush's message: 10000000h...
    {0x543ADE,{0xFF,0x50,0x48},3},                              // ...sent through slot 9
    // 0x54A530, that message: call 0x547C30 at kDamageCall (DamageCallReaches)
    {0x547C70,{0x4C,0x8B,0xEA},3},                              // mov r13,rdx: the GameDamageInfo
    {0x547DB7,{0x49,0x63,0x45,0x24},4},                         // its team
    {0x547DBF,{0x4C,0x63,0x87,0x14,0x03,0x00,0x00},7},          // the object's team (+0x314)
    {0x548109,{0xF3,0x41,0x0F,0x10,0x75,0x50},6},               // its damage
};
// The damage call 0x54A586 still reaches 0x547C30 (the stock bytes E8 A5 D6 FF FF).
constexpr unsigned kDamageCall=0x54A586,kDamageTarget=0x547C30;
bool DamageCallReaches() noexcept {
    const unsigned char* const site=image+kDamageCall;
    if(!Readable(site,5) || site[0]!=0xE8)return false;
    std::int32_t rel=0;
    std::memcpy(&rel,site+1,4);
    const unsigned char* const to=site+5+rel;
    return to==image+kDamageTarget;
}

// Whether the vtable at RVA `vtable` is of the class `rtti` (its CompleteObjectLocator's TypeDescriptor name).
bool ClassIs(unsigned vtable,const char* rtti) noexcept {
    const auto col=At<const unsigned char*>(image,vtable-8);
    if(col<image || col>=image+kImageSpan || !Readable(col,0x10))return false;
    const unsigned char* name=image+At<std::uint32_t>(col,0xC)+0x10;
    const std::size_t n=std::strlen(rtti)+1;
    return Readable(name,n) && std::memcmp(name,rtti,n)==0;
}

// A carrier the plugin made or a mission placed gets its entry (see the top). A mission's: it floats where it
// was put (on water kDraft under the surface). nullptr: dead, no carrier, or kMaxSubs out (said once).
Sub* Adopt(unsigned char* v) noexcept {
    if(v[kDead] || BodyOf(v)!=PluginBody::sub)return nullptr;
    Sub* s=FreeSub();
    if(!s) {
        if(refused!=v)Log("SUB v=%p: %d carriers driven already: this one is held still, not driven",v,kMaxSubs);
        refused=v;
        return nullptr;
    }
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    *s=Sub{};s->ref=ObjRef::Of(v);s->vehicle=v;s->bornAt=GameMs();s->inputAt=s->bornAt;
    std::memcpy(s->post,pos,12);
    s->floor=pos[1];
    float surface=0.0f;
    s->sea=SeaAt(pos[0],pos[2],&surface)==Sea::water;
    if(s->sea)s->floor=surface-kDraft;
    FixBodyPart506(v,"SUB");
    Log("SUB v=%p crewed (placed by the mission) at y=%.0f: %s y=%.0f, hp=%.0f/%.0f",v,pos[1],s->sea ? "afloat at" : "held at",
        s->floor,At<float>(v,kHp),At<float>(v,kHpMax));
    Thicken(*s,v);
    return s;
}
// Carrier v's entry (made if it has none): nullptr when it has none and gets none, or its object is no longer
// live (its entry dropped).
Sub* Entry(unsigned char* v) noexcept {
    Sub* s=FindSub(v);
    if(!s)return Adopt(v);
    if(Live(*s))return s;
    Sweep();
    return nullptr;
}

// One frame of carrier `s` (see the top), at most once a game frame.
void Tick(Sub& s,unsigned char* v,ULONGLONG ms) noexcept {
    if(s.frame==GameFrame())return;
    s.frame=GameFrame();
    const float dt=GameStep(s.tickMs ? ms-s.tickMs : 0);
    s.tickMs=ms;s.dt=dt;
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    // The stock input stays out of it, the move-area clamp too (it would teleport the hull back).
    Put<float>(v,kInLateral,0.0f);Put<float>(v,kInForward,0.0f);Put<float>(v,kInYaw,0.0f);
    Put<float>(v,kAreaInset,kNoInset);
    float nose[3]={m[8],0.0f,m[10]};
    if(!Normalize(nose)){nose[0]=0.0f;nose[2]=1.0f;}
    Nearest prey{{pos[0],pos[1],pos[2]},kRange,{0,0,0},false};
    VisitEnemies(v,&SeeEnemy,&prey);
    s.hasTarget=prey.found;
    if(prey.found)std::memcpy(s.target,prey.aim,12);
    Follow(s,dt);
    Drive(s,pos,nose,dt);
    float want[3];
    Heading(s,pos,m,want);
    Steer(s,m,want);
    s.ready=true;
    Repair(s,ms);
    Bay(s,m,ms);
    HitLog(s,ms);
    Stage(static_cast<int>(&s-subs),s,v,m,ms);
    Publish();
    CarrierLaserFrame(v);   // the portal laser's ships know the carriers from SubCarriers; this only steps it
    if(Cfg().debug && ms-s.logAt>2000) {
        s.logAt=ms;
        float ground=0.0f;
        const bool seen=!s.sea && GroundUnder(pos,nose,&ground);
        Log("SUB v=%p pos=(%.0f,%.0f,%.0f) clear=%.1f post=(%.0f,%.0f)%s vel=(%.1f,%.1f,%.1f) yawRate=%.1f target=%s%.0f hp=%.0f/%.0f fire=%d/%d",
            v,pos[0],pos[1],pos[2],seen ? pos[1]-kHullBottom-ground : -1.0f,s.post[0],s.post[2],s.moving ? " moving" : "",
            s.lin[0],s.lin[1],s.lin[2],s.ang[1]*180.0f/kPi,s.hasTarget ? "yes " : "no ",
            s.hasTarget ? prey.best : 0.0f,At<float>(v,kHp),At<float>(v,kHpMax),v[kFireGun],v[kFireMissile]);
    }
}
}  // namespace

// The 506 physics step (body506.cpp), after the stock one: the carrier driven here while its input stage is missing,
// its velocity and spin. A carrier without an entry (kMaxSubs out) is held still: never the stock heli's flight.
bool SubBodyStep(unsigned char* v,float* lin,float* ang) noexcept {
    if(!subOk || !Cfg().enabled)return false;   // the plugin off (Enabled=0) or the carriers not installed
    if(v[kDead]){Sweep();return false;}   // the wreck falls as the stock 506's does
    Sub* s=Entry(v);
    if(!s) {
        for(int i=0;i<3;++i)lin[i]=ang[i]=0.0f;
        return true;
    }
    // No input stage last frame or this one (its seat-0 driver gone): the frame and the fire step are this one's.
    // Only then: the input stage is where they ran from the start (map rays, new objects), the physics step only
    // sets the body's velocity.
    if(GameMs()-s->inputAt>kNoInputMs) {
        if(!s->noInputLogged)Log("SUB v=%p: no input stage (its seat-0 driver gone?): driven from its physics step",v);
        s->noInputLogged=true;
        const ULONGLONG ms=GameMs();
        Tick(*s,v,ms);
        FireStep(*s,v,reinterpret_cast<const float*>(v+kMatrix),ms);
    }
    for(int i=0;i<3;++i){lin[i]=s->lin[i];ang[i]=s->ang[i];}
    return true;
}

// The 506's messages to a carrier (body506.cpp): the water's taken whole (a carrier is a ship: the 506's handler
// would take it as a heli ditching and send itself twice its HP in damage each frame, M082 2026-10-04), damage routed.
bool SubMessage(unsigned char* v,std::uint32_t msg,void* data,MessageRestore* restore) noexcept {
    if(msg==kMsgWater)return true;
    if(msg!=kMsgDamage)return false;
    float was=0.0f;
    float* back=Route(v,static_cast<unsigned char*>(data),&was);
    if(back){restore->at=back;restore->was=was;}
    return false;
}

bool IsSub(const void* vehicle) noexcept { return BodyOf(vehicle)==PluginBody::sub; }

int SubCarriers(SubView* out,int max) noexcept {
    __try {
        int n=0;
        for(int i=0;i<kMaxSubs && n<max;++i) {
            const Sub& s=subs[i];
            if(!s.vehicle || !s.ready || !Live(s))continue;
            SubView& o=out[n++];
            o.ref=s.ref;
            std::memcpy(o.pos,s.vehicle+kPosition,12);
            std::memcpy(o.vel,s.lin,12);
            o.team=At<std::int32_t>(s.vehicle,kTeam);
        }
        return n;
    } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}

namespace {
const Sub* SubOf(const ObjRef& ref) noexcept {
    for(const auto& s:subs)
        if(s.vehicle && s.ref.obj==ref.obj && s.ref.ctrl==ref.ctrl && Live(s))return &s;
    return nullptr;
}
// The deck's flat bow (z > 280, about ±69 wide, docs/subcarrier-re.md §1.1): a deck point is taken on it, between
// kDeckAft and kDeckFore along the nose and within kDeckSide of the keel line, nearest to the asker.
constexpr float kDeckAft=320.0f,kDeckFore=700.0f,kDeckSide=40.0f;

// The live carrier nearest (horizontally) to `from`, or nullptr; `dist` gets the distance.
const Sub* NearestSub(const float* from,float* dist) noexcept {
    const Sub* best=nullptr;
    for(const auto& s:subs) {
        if(!s.vehicle || !Live(s))continue;
        const float* p=reinterpret_cast<const float*>(s.vehicle+kPosition);
        const float d=vec::Flat(p,from);
        if(!best || d<*dist){best=&s;*dist=d;}
    }
    return best;
}
}  // namespace

bool SubSpot(const ObjRef& sub,float along,float over,float* out) noexcept {
    __try {
        const Sub* s=SubOf(sub);
        if(!s)return false;
        const float local[3]={0.0f,kSubDeckTop+over,along};
        ToWorld(reinterpret_cast<const float*>(s->vehicle+kMatrix),local,out);
        return std::isfinite(out[0]+out[1]+out[2]);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool SubDeckUnder(const ObjRef& sub,const float* p,float* out) noexcept {
    __try {
        const Sub* s=SubOf(sub);
        if(!s)return false;
        const float* m=reinterpret_cast<const float*>(s->vehicle+kMatrix);
        const float* up=m+4;
        float over=0.0f;
        for(int i=0;i<3;++i)over+=(p[i]-(m[12+i]+up[i]*kSubDeckTop))*up[i];
        float drop=up[1]>0.5f ? over/up[1] : over;
        drop=Clamp(drop,5.0f,1000.0f);
        out[0]=p[0];out[1]=p[1]-drop;out[2]=p[2];
        return std::isfinite(out[1]);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool SubDeck(const float* from,float* deck) noexcept {
    __try {
        float dist=0.0f;
        const Sub* s=NearestSub(from,&dist);
        if(!s)return false;
        const float* m=reinterpret_cast<const float*>(s->vehicle+kMatrix);
        const float* pos=m+12;
        const float rel[3]={from[0]-pos[0],from[1]-pos[1],from[2]-pos[2]};
        const float x=Clamp(Dot(rel,m),-kDeckSide,kDeckSide),z=Clamp(Dot(rel,m+8),kDeckAft,kDeckFore);
        for(int i=0;i<3;++i)deck[i]=pos[i]+m[i]*x+m[4+i]*kSubDeckTop+m[8+i]*z;
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
        const float x=std::fabs(Dot(rel,right))-kSubBoxHalfX,z=std::fabs(Dot(rel,nose)-kSubBoxCentreZ)-kSubBoxHalfZ;
        const float ox=x>0.0f ? x : 0.0f,oz=z>0.0f ? z : 0.0f;
        return std::sqrt(ox*ox+oz*oz);
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1.0f;}
}

void PreloadSub() noexcept {
    __try {
        preloaded=false;
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
        if(LiveSubs()>=kMaxSubs){Log("SUB launch: %d carriers out already",kMaxSubs);return nullptr;}
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
        NoteLocalCopy(v,nullptr);   // whose its damage is online: the call's (online_authority.h)
        FixBodyPart506(v,"SUB");
        reinterpret_cast<SetTeamFn>(image+kSetTeam)(v,kTeamFriend,true);
        LevelVehicle(v);   // as a script's CreateFriend: the hull's tier (Thicken) is then the difficulty's
        SeatNpcRider(v,true);   // a copy this machine just made: its own here (online_authority.h)
        if(!IsSub(v)) {
            Log("SUB launch: %p is no carrier (mark %.0f): deleted",v,BodyMark(v));
            reinterpret_cast<DeleteFn>(image+kDelete)(v);
            return nullptr;
        }
        // Its entry before it ever runs: one it cannot have (the count above raced) is no carrier left undriven.
        Sub* s=FreeSub();
        if(!s) {
            Log("SUB launch: %p has no entry free (%d driven): deleted",v,kMaxSubs);
            reinterpret_cast<DeleteFn>(image+kDelete)(v);
            return nullptr;
        }
        *s=Sub{};s->ref=ObjRef::Of(v);s->vehicle=v;s->bornAt=ms;s->launched=true;s->inputAt=ms;
        s->sea=sea==Sea::water;s->floor=s->sea ? surface-kDraft : -INFINITY;
        std::memcpy(s->post,start,12);
        Thicken(*s,v);
        Log("SUB v=%p launched at (%.0f,%.0f,%.0f) heading (%.2f,%.2f) %s hp=%.0f driver=%d",v,start[0],start[1],start[2],fwd[0],fwd[2],
            sea==Sea::water ? "afloat" : "on the ground (water unknown)",At<float>(v,kHp),SeatCount(v)>0 && SeatRider(SeatAt(v,0))==Rider::dummy);
        return v;
    } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}

// The input stage (HeliFrame, after the stock input, while the NPC driver is aboard): the frame, if the physics step
// has not done it, and the fire bytes.
void SubFrame(unsigned char* v) noexcept {
    if(!subOk || !Body506Ok())return;
    Sub* s=Entry(v);
    if(!s)return;
    const ULONGLONG ms=GameMs();
    if(s->noInputLogged){Log("SUB v=%p: its input stage is back",v);s->noInputLogged=false;}
    s->inputAt=ms;
    Tick(*s,v,ms);
    FireStep(*s,v,reinterpret_cast<const float*>(v+kMatrix),ms);
}

bool InstallGauge() noexcept {
    if(gaugeTried)return gaugeOk;
    gaugeTried=true;
    __try {
        bool gauge=Readable(image+kGaugeHud+3*8,8) && At<const unsigned char*>(image,kGaugeHud+3*8)==image+kGaugeDraw;
        for(const auto& g:kGaugeSigs)gauge=gauge && Matches(g.rva,g.bytes,g.size);
        bool changed=false;
        gaugeOk=gauge && RedirectCall(image+kGaugeCall,image+kGaugeFn,reinterpret_cast<void*>(&GaugeHook),changed);
        Log("HOOK gauge=%d (carrier gauges, vehicle HUD)",gaugeOk);
        return gaugeOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool InstallSub() noexcept {
    __try {
        if(!Body506Ok()){Log("SUB: no 506 physics hook (body506): carriers off");return false;}
        spawnOk=Matches(kDelete,kDeleteSig,sizeof(kDeleteSig)) && Matches(kPreload,kPreloadSig,sizeof(kPreloadSig)) &&
                Matches(kCreateObject,kCreateObjectSig,sizeof(kCreateObjectSig)) && Matches(kSetTeam,kSetTeamSig,sizeof(kSetTeamSig)) &&
                Readable(image+kInitParamVtable,8) && BodyPartOk();   // going down without its body part crashes (jet.cpp)
        damageOk=Body506MessageOk() && DamageCallReaches();
        for(const auto& d:kDamageSigs)damageOk=damageOk && Matches(d.rva,d.bytes,d.size);
        int heavy=0;
        for(int k=0;k<kHeavyCount;++k) {
            heavyOk[k]=ClassIs(kHeavy[k].vtable,kHeavy[k].rtti);
            heavy+=heavyOk[k] ? 1 : 0;
            if(!heavyOk[k])Log("SUB heavy source %s: no such vtable at EDF+%X",kHeavy[k].rtti,kHeavy[k].vtable);
        }
        Log("HOOK sub spawn=%d gauge=%d damage=%d heavy=%d/%d",spawnOk,gaugeOk,damageOk,heavy,kHeavyCount);
        subOk=true;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// A new mission (mission.cpp MissionStart): the last mission's carriers are gone with it; their gauges too.
void ResetSubs() noexcept {
    for(auto& s:subs)s=Sub{};
    for(auto& g:staging)g.shown=false;
    refused=nullptr;
    Publish();
}
}  // namespace crew
