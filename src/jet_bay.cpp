// The jets' IndirectFireControl users (jet.cpp): the bomb bay of a jet that takes over a bomber, the gunship's
// shells and its cannon, and the impact charges (ImpactDamage) a crash of the plugin's aircraft sets off.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "jet_internal.h"
#include <malloc.h>
#include <cwchar>

namespace crew {
namespace jet {
namespace {
// The bomb bay of a jet that takes over a bomber (JetLaunchBomber): an IndirectFireControl of its own, set
// up as BombingPlane_Init (0x5AABB0) sets up the bomber's (plane+0xC20) and driven as the bomber's update
// (0x5AB240) drives it: opened (0x2B4340) when the target is fireDist ahead along the line, which is what
// the bomber computes into +0xC14 (frames of fire (0x2B8470: (shots-1) x (interval+1)) x speed a frame x
// target_adjust + target_distance), then stepped once a frame (0x2B95A0) with the drop point (+0x20, each
// bomb lands on it within the spread: the shot is solved ballistically onto it) and the release point the
// jet itself (+0x300, used as +0x2F9 says). The stock bomber moves its fixed speed a frame (0x5AB240), so
// its drop point does too and the carpet is frames x speed a frame long; ours moves the same, a step at a
// time (BayFrame), whatever the jet's real flight does meanwhile.
// The bombs are the bomber's: its bombing_plane_param, damage, spread, seed and owner.
constexpr unsigned kIfcCtor=0x2B3940,kIfcDtor=0x2B3C90,kIfcConfig=0x2B5F40,kIfcOwner=0x2B8390,kIfcDamage=0x2B82E0,
                   kIfcSpread=0x2B8460,kIfcFrames=0x2B8470,kIfcOpen=0x2B4340,kIfcStep=0x2B95A0,kIfcDone=0x2B7B90;
constexpr std::size_t kIfcSize=0x600,kIfcAim=0x20,kIfcShots=0x2F0,kIfcBallistic=0x2F8,kIfcFromJet=0x2F9,kIfcFrom=0x300;
constexpr float kLineGain=250.0f;   // m off the bombing line that turn it back at the most
// ms the bay's last bombs (and a cluster's bomblets) still pass the bomber's flight after it closes.
constexpr ULONGLONG kBombClearMs=15000;
struct Sig { unsigned rva; unsigned char bytes[12]; };
const Sig kBaySigs[]={
    {kIfcCtor,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x4C,0x24,0x08,0x57,0x48}},
    {kIfcDtor,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48}},
    {kIfcConfig,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x54}},
    {kIfcOwner,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48}},
    {kIfcDamage,{0xF3,0x0F,0x11,0x89,0xDC,0x00,0x00,0x00,0xC3,0xCC,0xCC,0xCC}},
    {kIfcSpread,{0xF3,0x0F,0x11,0x89,0x24,0x02,0x00,0x00,0xC3,0xCC,0xCC,0xCC}},
    {kIfcFrames,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89}},
    {kIfcOpen,{0x48,0x8B,0x81,0xC0,0x02,0x00,0x00,0x0F,0x57,0xC0,0x48,0xBA}},
    {kIfcStep,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x48}},
    {kIfcDone,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89}},
};
bool bayOk=false;        // the spawn functions matched (InstallJets) and the bay's (kBaySigs)

// A bomber's model -> its body: the mesh bone that names it (bone records as jet_flight.cpp's kInstBones says).
constexpr std::size_t kInstBones=0x10;   // the bone count and record size: body506.h
struct BomberModel { const wchar_t* bone; JetBody body; };
const BomberModel kBomberModels[]={{L"bomber501_2",JetBody::bomber501_2},{L"bomber401",JetBody::bomber401}};

// Shells (Shell): a DemoIndirectFire object (docs/mission-airstrike-re.md, docs/carrier-laser-re.md §2-3) made
// with CreateObject as carrierlaser.cpp's beams are, and fired once by the plugin's aircraft: owned by it (its IFC
// takes the owner's team every step: team, kills, its own hull not hit), started from where it says (+0x2F9 /
// +0x300) and aimed (+0x20), its damage written (+0xDC). The object deletes itself when done. The IFC calls are
// the bay's (bayOk); the object's class is checked by its vtable.
constexpr unsigned kDemoVtable=0x17D4B20;
constexpr std::size_t kDemoIfc=0x170;
bool shellsOk=false;     // bayOk, and the DemoIndirectFire vtable is there
// The gunship's shells (GunshipFire): the missions' whale gunship round, DEMOGUNSHIPFIREE25 (one RocketBullet01
// round at 8 m/frame, 10 m blast, 60 frames before it goes), from the gunship itself at its target's lock
// point, so it is seen leaving the gunship instead of the stock off-screen sky point.
constexpr ULONGLONG kGunshipGapMs=2500;   // between shells
constexpr float kGunshipReach=1800.0f;    // m from the gunship to its target at the most
constexpr float kGunshipDamage=300.0f;    // a shell's damage (the SGO's own factor is the missions' 250)
const wchar_t kGunshipSgo[]=L"app:/object/demogunshipfiree25.sgo";
bool gunshipReady=false;                  // the shell SGO was preloaded for this mission (PreloadShells)
// The gunship's long-range side cannon (CannonShot; README 炮舰机的机炮, the user 2026-10-05: "炮舰机应该加装远距离
// 机炮", an AC-130's 30-40 mm side gun): tools/make_jets.py's EDF6VC_GUNSHIP_CANNON.SGO, an impact charge (the stock
// gunship's solid round, one round, no wait) made a 40 mm HE round: 16 m a frame (kCannonSpeed), no fall, 170 frames
// (2720 m, past kCannonReach), a 4 m blast, a thin orange tracer. Fired straight (IFC +0x2F8 = 0) from the gunship at
// its aim, a round every kCannonGapMs: 2 a second, at kCannonDamage a round at the base tier times the gunship's tier
// (Tier: the same factor the ram's damage takes, playerjet.cpp RamDamage), so 5 rounds in a shell's 2.5 s gap carry its
// 300 at the base tier: as strong as the shells, but far, quick and exact where the shells are slow and wide. Its own
// gap: the shells and the cannon are two guns. Without the file (an install from before) there is no cannon: the
// gunship has its shells alone, as before.
constexpr ULONGLONG kCannonGapMs=500;
constexpr float kCannonReach=2500.0f;     // m from the gunship to what it fires at, at the most
constexpr float kCannonDamage=60.0f;      // a round's at the base tier
constexpr float kCannonSpeed=960.0f;      // m/s: the round's (make_jets.py CANNON_SPEED, 16 m a frame), for the NPCs' lead
const wchar_t kCannonSgo[]=L"app:/object/edf6vc_gunship_cannon.sgo";
const wchar_t kCannonFile[]=L"EDF6VC_GUNSHIP_CANNON.SGO";
bool cannonReady=false;                   // preloaded this mission (PreloadShells)
// Impact charges (ImpactDamage): tools/make_jets.py's EDF6VC_IMPACT_*.SGO, the gunship round made a one-round,
// no-wait GrenadeBullet01 that bursts at the end of its kImpact life (or on what it meets first), its blast
// radius the charge's (indirect_fire_param #9 AmmoExplosion): a blast's radius is the SGO's, so one charge per
// radius; its damage the plugin writes. Fired straight (IFC +0x2F8 = 0) from kImpactDrop over the impact point
// down onto it, it bursts there. The blast spares the owner's team's friends: the IFC's team is its owner's.
struct Charge { const wchar_t* sgo; const wchar_t* file; float radius; };
const Charge kCharges[]={
    {L"app:/object/edf6vc_impact_08.sgo",L"EDF6VC_IMPACT_08.SGO",8.0f},
    {L"app:/object/edf6vc_impact_16.sgo",L"EDF6VC_IMPACT_16.SGO",16.0f},
    {L"app:/object/edf6vc_impact_32.sgo",L"EDF6VC_IMPACT_32.SGO",32.0f},
    {L"app:/object/edf6vc_impact_64.sgo",L"EDF6VC_IMPACT_64.SGO",64.0f},
};
constexpr int kChargeCount=static_cast<int>(sizeof(kCharges)/sizeof(kCharges[0]));
bool chargeReady[kChargeCount]{};         // preloaded this mission (PreloadShells)
// m over the impact the charge starts, straight down (make_jets.py IMPACT_*: 10 m a frame for 6 frames, bursting on
// what it meets: the ground under the impact, or the enemy rammed in the air)
constexpr float kImpactDrop=2.0f;
// The drill tank's charge (DrillCharge, drill.cpp): tools/make_drill.py's EDF6VC_DRILL_CHARGE.SGO, the impact charges'
// recipe with a small blast (pylib/vcobjects.py DRILL_CHARGE: 4 m, at least the 3 m from which a blast breaks buildings,
// docs/drill-re.md §3) and a short flight (from the drill's base along its axis, it meets what the drill touches).
const wchar_t kDrillChargeSgo[]=L"app:/object/edf6vc_drill_charge.sgo";
const wchar_t kDrillChargeFile[]=L"EDF6VC_DRILL_CHARGE.SGO";
bool drillReady=false;                    // preloaded this mission (PreloadShells)

using PreloadFn=void(*)(void*,const wchar_t*,std::int32_t,std::int32_t);
constexpr unsigned kPreload=0x7A3780;

// A bay set up from a bomber's payload (see kIfcCtor); `fireDist` gets where it opens at `perFrame`
// metres a frame. nullptr when it cannot be made.
unsigned char* BayMake(const BombLoad& l,float perFrame,float* fireDist) noexcept {
    auto ifc=static_cast<unsigned char*>(_aligned_malloc(kIfcSize,16));
    if(!ifc)return nullptr;
    std::memset(ifc,0,kIfcSize);
    __try {
        reinterpret_cast<void(*)(void*)>(image+kIfcCtor)(ifc);
        reinterpret_cast<void(*)(void*,const void*,std::int32_t)>(image+kIfcConfig)(ifc,l.param,l.seed);
        reinterpret_cast<void(*)(void*,const void*)>(image+kIfcOwner)(ifc,l.owner);
        ifc[kIfcFromJet]=1;
        reinterpret_cast<void(*)(void*,float)>(image+kIfcDamage)(ifc,l.damage);
        reinterpret_cast<void(*)(void*,float)>(image+kIfcSpread)(ifc,l.spread);
        const std::int32_t frames=reinterpret_cast<std::int32_t(*)(void*)>(image+kIfcFrames)(ifc);
        *fireDist=static_cast<float>(frames)*perFrame*l.adjust+l.reach;
        return ifc;
    } __except(FaultLog("JET bay setup (left as is)",GetExceptionInformation())) {
        return nullptr;
    }
}

Body BodyOfBomber(JetBody b) noexcept {
    switch(b) {
        case JetBody::bomber401: return Body::bomber401;
        case JetBody::bomber501_2: return Body::bomber501_2;
        default: return Body::strike;
    }
}

unsigned char* ShellCreate(const wchar_t* sgo,const float* m,bool& ok) noexcept {
    InitParam param{image+kInitParamVtable,{}};
    __try { return reinterpret_cast<CreateObjectFn>(image+kCreateObject)(At<void*>(image,kObjectMgr),m,sgo,&param); }
    __except(FaultLog("JET shell create (off for this mission)",GetExceptionInformation())) {
        ok=false;
        return nullptr;
    }
}

// Shell `sgo` (preloaded: `ok`) fired by `owner` from `from` at `aim` with `damage` (see kDemoVtable): whether
// it was. `ok` goes false for the mission when the game cannot build it or it is no DemoIndirectFire.
// `straight`: the shot flies the line from `from` to `aim` (IFC +0x2F8 = 0), not the ballistic arc the IFC
// solves by default.
bool Shell(const wchar_t* sgo,bool& ok,const unsigned char* owner,const float* from,const float* aim,float damage,bool straight,
           const char* what) noexcept {
    if(!ok || !shellsOk || !At<void*>(image,kObjectMgr))return false;
    alignas(16) const float m[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, aim[0],aim[1],aim[2],1};
    unsigned char* const o=ShellCreate(sgo,m,ok);
    if(!o)return false;
    __try {
        if(At<const void*>(o,0)!=image+kDemoVtable) {
            Log("JET %s: %p is no DemoIndirectFire: deleted, off for this mission",what,o);
            reinterpret_cast<DeleteFn>(image+kDelete)(o);
            ok=false;
            return false;
        }
        unsigned char* const ifc=o+kDemoIfc;
        const void* const weak[2]={At<const void*>(owner,kSelf),At<const void*>(owner,kSelfCtrl)};
        reinterpret_cast<void(*)(void*,const void*)>(image+kIfcOwner)(ifc,weak);
        reinterpret_cast<void(*)(void*,float)>(image+kIfcDamage)(ifc,damage);
        ifc[kIfcFromJet]=1;
        if(straight)ifc[kIfcBallistic]=0;
        alignas(16) const float st[4]={from[0],from[1],from[2],1.0f},am[4]={aim[0],aim[1],aim[2],1.0f};
        std::memcpy(ifc+kIfcFrom,st,16);std::memcpy(ifc+kIfcAim,am,16);
        return true;
    } __except(FaultLog("JET shell setup",GetExceptionInformation())){return false;}
}

// The charge for `radius`: the smallest preloaded one at least that wide, else the widest preloaded; -1: none.
int ChargeFor(float radius) noexcept {
    int best=-1;
    for(int i=0;i<kChargeCount;++i) {
        if(!chargeReady[i])continue;
        best=i;
        if(kCharges[i].radius>=radius)return i;
    }
    return best;
}

// The tier the game gave aircraft `v` (its max HP over its SGO durability, stores.inc kJetMasses): what a weapon's
// damage is scaled by (the ram's too, playerjet.cpp RamDamage). 1 for a body without a durability.
float Tier(const unsigned char* v) noexcept {
    const JetMass* const kind=JetMassOf(BodyMark(v));
    const float hpMax=At<float>(v,kHpMax);
    return kind && kind->durability>0.0f && hpMax>0.0f && std::isfinite(hpMax) ? hpMax/kind->durability : 1.0f;
}

// A cannon round fired by `who` from the gunship (`pos`) at `at` (see kCannonSgo): kCannonGapMs after its last, within
// kCannonReach. Every tenth logged (Debug): two a second would drown the log.
bool CannonShot(Jet& j,const unsigned char* v,const float* pos,const float* at,ULONGLONG ms,const char* who) noexcept {
    if(!cannonReady || ms-j.shells.cannonAt<kCannonGapMs)return false;
    const float d[3]={at[0]-pos[0],at[1]-pos[1],at[2]-pos[2]};
    if(Len(d)>kCannonReach)return false;
    j.shells.cannonAt=ms;
    const float damage=kCannonDamage*Tier(v);
    if(!Shell(kCannonSgo,cannonReady,v,pos,at,damage,true,"gunship cannon"))return false;
    if(Cfg().debug && j.shells.cannonShots%10==0)
        Log("JET v=%p gunship cannon round #%d from %s at (%.0f,%.0f,%.0f), %.0f m, %.0f damage",v,j.shells.cannonShots+1,who,at[0],at[1],
            at[2],Len(d),damage);
    ++j.shells.cannonShots;
    return true;
}

// The NPC crew's cannon at its target (j.t: a ground one), led: where a round fired now meets it as it moves on
// (tgtVel, m/s) over the round's flight to where it is now.
bool CannonAtTarget(Jet& j,const unsigned char* v,const float* pos,ULONGLONG ms,const char* who) noexcept {
    if(!cannonReady || !j.t.target || j.t.flyer)return false;
    const float d[3]={j.t.aim[0]-pos[0],j.t.aim[1]-pos[1],j.t.aim[2]-pos[2]};
    const float t=Len(d)/kCannonSpeed;
    const float at[3]={j.t.aim[0]+j.t.tgtVel[0]*t,j.t.aim[1]+j.t.tgtVel[1]*t,j.t.aim[2]+j.t.tgtVel[2]*t};
    return CannonShot(j,v,pos,at,ms,who);
}
}  // namespace

// The bombing run (Mode::bomb): level at bombAlt along the bomber's line through the target at its speed,
// turning back onto the line when off it; the bay opens fireDist (and a frame) short of the target, and
// with the last bomb gone it flies on and withdraws.
void BombRun(Jet& j,const float* pos,ULONGLONG ms,float* want,float* speed) noexcept {
    BayState& b=j.bay;
    const float rel[3]={b.bombAt[0]-pos[0],0,b.bombAt[2]-pos[2]};
    const float side[3]={b.bombDir[2],0,-b.bombDir[0]};
    const float along=Dot(rel,b.bombDir),off=-Dot(rel,side);   // ahead to the target; right of the line
    const float c=Clamp(off/kLineGain,-0.7f,0.7f);
    const float dir[3]={b.bombDir[0]-side[0]*c,0,b.bombDir[2]-side[2]*c};
    Level(pos,dir,b.bombAlt,want);
    *speed=b.bombSpeed;
    if(Cfg().debug && ms-j.t.gateAt>1000){j.t.gateAt=ms;Log("JET v=%p bomb run: %.0f m to the target, %.0f m off the line",j.Vehicle(),along,off);}
    // The last bomb out (BayFrame may have torn the bay down already, its bombs gone too): it flies on, leaving.
    if(b.bombing && (!b.ifc || At<std::int32_t>(b.ifc,kIfcShots)<=0)) {
        Log("JET v=%p bombs away",j.Vehicle());
        std::memcpy(j.t.out,b.bombDir,12);Withdraw(j,"bombs dropped",ms);
        return;
    }
    if(b.ifc && !b.bombing && along<b.fireDist+b.bombSpeed/60.0f) {
        reinterpret_cast<void(*)(void*)>(image+kIfcOpen)(b.ifc);
        b.bombing=true;b.bayFrom=-along;b.baySteps=0;b.bayOpenAt=ms;
        Log("JET v=%p bay open: %.0f m short of the target, %.0f m off the line, %d to drop",j.Vehicle(),along,off,At<std::int32_t>(b.ifc,kIfcShots));
    }
}

// Tears down the bay (the game's destructor, then the memory). Only for a bay of this mission: the mission's
// reset forgets the last one's (ResetJets).
void BayFree(unsigned char*& ifc) noexcept {
    if(!ifc)return;
    __try { reinterpret_cast<void(*)(void*)>(image+kIfcDtor)(ifc); } __except(FaultLog("JET bay tear-down",GetExceptionInformation())) {}
    _aligned_free(ifc);
    ifc=nullptr;
}

// A frame of the open bay: drop point and release point as the bomber's update sets them, one step; torn
// down once the last bomb is out and none it tracks is left. The stock bomber (0x5AB240) aims
// target_distance ahead of itself on its straight line from its start to the target (Init 0x5AABB0 points
// it there), and moves its speed a frame: the drop point here is where that would be this step, on the
// line from where the bay opened. Off the nose, every swing of the jet's heading swept it sideways; off the
// jet's position, the carpet stretched with however far the jet really flew a step.
void BayFrame(Jet& j,const float* pos) noexcept {
    BayState& b=j.bay;
    if(!b.ifc || !b.bombing)return;
    const float perFrame=b.bombSpeed/60.0f;
    const float along=b.bayFrom+b.reach+static_cast<float>(b.baySteps)*perFrame;
    alignas(16) const float aim[4]={b.bombAt[0]+b.bombDir[0]*along,b.bombAt[1],b.bombAt[2]+b.bombDir[2]*along,1.0f};
    alignas(16) const float from[4]={pos[0],pos[1],pos[2],1.0f};
    std::memcpy(b.ifc+kIfcAim,aim,16);std::memcpy(b.ifc+kIfcFrom,from,16);
    const float frame=1.0f;
    reinterpret_cast<void(*)(void*,const float*)>(image+kIfcStep)(b.ifc,&frame);
    if(At<std::int32_t>(b.ifc,kIfcShots)>0)++b.baySteps;
    if(At<std::int32_t>(b.ifc,kIfcShots)<=0 && reinterpret_cast<bool(*)(void*)>(image+kIfcDone)(b.ifc)) {
        BayFree(b.ifc);
        const ULONGLONG ms=GameMs();
        b.bombClear=ms+kBombClearMs;
        Log("JET v=%p bay closed: dropped over %d steps (%.0f m from %.0f m to %.0f m along the line) in %.1f s",j.Vehicle(),b.baySteps,
            static_cast<float>(b.baySteps)*perFrame,b.bayFrom+b.reach,along,static_cast<float>(ms-b.bayOpenAt)*0.001f);
    }
}

// A gunship's guns while its weapons are free (WeaponsFree, as every jet weapon) at a ground target: its cannon
// (CannonAtTarget) within kCannonReach, and a shell every kGunshipGapMs within kGunshipReach, from the gunship (`pos`)
// onto the target's lock point.
void GunshipFire(Jet& j,const unsigned char* v,const float* pos,ULONGLONG ms) noexcept {
    if(!WeaponsFree(j) || j.t.flyer)return;
    CannonAtTarget(j,v,pos,ms,"its NPC crew");
    if(ms-j.shells.gunAt<kGunshipGapMs)return;
    const float d[3]={j.t.aim[0]-pos[0],j.t.aim[1]-pos[1],j.t.aim[2]-pos[2]};
    if(Len(d)>kGunshipReach)return;
    j.shells.gunAt=ms;
    if(!Shell(kGunshipSgo,gunshipReady,v,pos,j.t.aim,kGunshipDamage,false,"gunship shell"))return;
    ++j.shells.gunShots;
    if(Cfg().debug)Log("JET v=%p gunship shell #%d at %p (%.0f m)",v,j.shells.gunShots,j.t.target,Len(d));
}

// The player's aircraft (playerjet_board.inc). A bay not yet open: its bombs.
int BayLeft(const unsigned char* v) noexcept {
    const Jet* const j=FindJet(v);
    if(!j || !j->bay.ifc || j->bay.bombing)return 0;
    const std::int32_t shots=At<std::int32_t>(j->bay.ifc,kIfcShots);
    return shots>0 ? shots : 0;
}

// The bay opened by the player: its first bomb on `at` (the cockpit's CCIP), the carpet laid on from there along the
// jet's track at its speed when the bay opened, a step a frame as the stock bomber lays it (BayFrame: the drop point
// starts at bayFrom + reach along the line from bombAt).
bool PlayerOpenBay(unsigned char* v,const float* at,const float* vel) noexcept {
    Jet* const j=FindJet(v);
    if(!j || !BayLeft(v))return false;
    BayState& b=j->bay;
    float dir[3]={vel[0],0.0f,vel[2]};
    const float speed=Len(dir);
    if(!Normalize(dir) || speed<1.0f)return false;
    std::memcpy(b.bombAt,at,12);std::memcpy(b.bombDir,dir,12);
    b.bombSpeed=speed;b.bayFrom=-b.reach;b.baySteps=0;b.bayOpenAt=GameMs();b.bombing=true;
    reinterpret_cast<void(*)(void*)>(image+kIfcOpen)(b.ifc);
    Log("JET v=%p bay opened by the player: %d to drop from (%.0f,%.0f,%.0f) along its track at %.0f m/s",v,At<std::int32_t>(b.ifc,kIfcShots),
        at[0],at[1],at[2],speed);
    return true;
}

void PlayerBayFrame(unsigned char* v,const float* pos) noexcept {
    if(Jet* const j=FindJet(v))BayFrame(*j,pos);
}

namespace {
// A gunship's shell fired by its crew at `at`: as GunshipFire's (kGunshipGapMs apart, within kGunshipReach). One gun:
// the pilot's SHELLS, the player at the gunner seat and the NPC gunner under a player pilot share its gap.
bool CrewFire(Jet& j,unsigned char* v,const float* at,ULONGLONG ms,const char* who) noexcept {
    if(ms-j.shells.gunAt<kGunshipGapMs)return false;
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float d[3]={at[0]-pos[0],at[1]-pos[1],at[2]-pos[2]};
    if(Len(d)>kGunshipReach)return false;
    j.shells.gunAt=ms;
    if(!Shell(kGunshipSgo,gunshipReady,v,pos,at,kGunshipDamage,false,"gunship shell"))return false;
    ++j.shells.gunShots;
    if(Cfg().debug)Log("JET v=%p gunship shell #%d from %s at (%.0f,%.0f,%.0f), %.0f m",v,j.shells.gunShots,who,at[0],at[1],at[2],Len(d));
    return true;
}
}  // namespace

// The gunship's shell from the player (the pilot's SHELLS, the gunner seat): at `at`.
bool PlayerShell(unsigned char* v,const float* at,ULONGLONG ms) noexcept {
    Jet* const j=FindJet(v);
    return j && CrewFire(*j,v,at,ms,"the player");
}

// The gunship's cannon from the player (the pilot's CANNON, the gunner seat): at `at`, where they aim (no lead: the
// round flies 2.6 s to its reach, the player leads a mover themselves).
bool PlayerCannon(unsigned char* v,const float* at,ULONGLONG ms) noexcept {
    Jet* const j=FindJet(v);
    return j && CannonShot(*j,v,reinterpret_cast<const float*>(v+kPosition),at,ms,"the player");
}

// The NPC at the gun under a player pilot (playerjet_crew.inc CrewGunner): GunshipFire's target, picked round the
// gunship itself within the longer gun's reach (PickTarget; the entry's target is its own again when it is handed back:
// ResumeNpc); the cannon at it (led) and a shell when it is within the shells' reach, each gun when it is ready.
bool CrewShell(unsigned char* v,float dt,ULONGLONG ms) noexcept {
    Jet* const j=FindJet(v);
    if(!j || !Cfg().jetPilot)return false;
    const bool cannon=cannonReady && ms-j->shells.cannonAt>=kCannonGapMs,shell=ms-j->shells.gunAt>=kGunshipGapMs;
    if(!cannon && !shell)return false;
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    PickTarget(*j,v,pos,pos,cannonReady ? kCannonReach : kGunshipReach,dt,ms);
    if(!j->t.target || j->t.flyer)return false;
    const bool fired=cannon && CannonAtTarget(*j,v,pos,ms,"its NPC gunner");
    return (shell && CrewFire(*j,v,j->t.aim,ms,"its NPC gunner")) || fired;
}

float ShellWait(const unsigned char* v,ULONGLONG ms) noexcept {
    const Jet* const j=FindJet(v);
    if(!j)return 0.0f;
    const ULONGLONG since=ms-j->shells.gunAt;
    return since>=kGunshipGapMs ? 0.0f : static_cast<float>(kGunshipGapMs-since)*0.001f;
}

float ShellReach() noexcept { return kGunshipReach; }

bool ShellsReady() noexcept { return gunshipReady && shellsOk; }

float CannonWait(const unsigned char* v,ULONGLONG ms) noexcept {
    const Jet* const j=FindJet(v);
    if(!j)return 0.0f;
    const ULONGLONG since=ms-j->shells.cannonAt;
    return since>=kCannonGapMs ? 0.0f : static_cast<float>(kCannonGapMs-since)*0.001f;
}

float CannonReach() noexcept { return kCannonReach; }

bool CannonReady() noexcept { return cannonReady && shellsOk; }

// How far from its anchor kind `k` takes targets (jet.cpp PickTarget): its range; a gunship with its cannon reaches out
// further, to where the cannon still reaches them from anywhere on its circle (kCannonReach over the circle's height,
// less the circle and a step of its spacing): about 1800 m instead of 1500, the targets past the shells' reach the
// cannon's alone.
float TargetRange(const Kind& k) noexcept {
    if(k.weapon!=Weapon::shells || !CannonReady())return k.range;
    const float out=std::sqrt(kCannonReach*kCannonReach-k.alt*k.alt)-k.patrol-k.patrolStep;
    return out>k.range ? out : k.range;
}

bool InstallBay(bool spawnOk) noexcept {
    bayOk=spawnOk;
    for(const auto& b:kBaySigs)bayOk=bayOk && Matches(b.rva,b.bytes,sizeof(b.bytes));
    shellsOk=bayOk && Readable(image+kDemoVtable,8);
    return bayOk;
}

void PreloadShells(void* mgr,bool gunship) noexcept {
    gunshipReady=shellsOk && gunship;
    if(gunshipReady)reinterpret_cast<PreloadFn>(image+kPreload)(mgr,kGunshipSgo,2,-1);
    cannonReady=gunshipReady && ModFileThere(kCannonFile);
    if(cannonReady)reinterpret_cast<PreloadFn>(image+kPreload)(mgr,kCannonSgo,2,-1);
    else if(gunshipReady)Log("JET the gunship has no cannon this mission: no %ls (python tools/make_jets.py, or the installer)",kCannonFile);
    for(int i=0;i<kChargeCount;++i) {
        chargeReady[i]=shellsOk && ModFileThere(kCharges[i].file);
        if(chargeReady[i])reinterpret_cast<PreloadFn>(image+kPreload)(mgr,kCharges[i].sgo,2,-1);
    }
    drillReady=shellsOk && ModFileThere(kDrillChargeFile);
    if(drillReady)reinterpret_cast<PreloadFn>(image+kPreload)(mgr,kDrillChargeSgo,2,-1);
    Log("JET preload gunship shells=%d cannon=%d impact charges %d/%d/%d/%d drill charge %d",gunshipReady,cannonReady,chargeReady[0],
        chargeReady[1],chargeReady[2],chargeReady[3],drillReady);
}

void ResetShells() noexcept {
    gunshipReady=false;
    cannonReady=false;
    for(auto& c:chargeReady)c=false;
    drillReady=false;
}
}  // namespace jet

using namespace jet;

JetBody BomberBody(const unsigned char* inst) noexcept {
    __try {
        const auto bones=At<const unsigned char*>(inst,kInstBones);
        const auto count=At<std::int32_t>(inst,kInstBoneCount);
        if(!bones || count<=0 || count>256 || !Readable(bones,static_cast<std::size_t>(count)*kBoneStride))return JetBody::kind;
        for(std::int32_t i=0;i<count;++i) {
            const auto name=At<const wchar_t*>(bones+static_cast<std::size_t>(i)*kBoneStride,0);
            if(!Readable(name,2))continue;
            for(const auto& m:kBomberModels)if(wcsncmp(name,m.bone,32)==0)return m.body;
        }
    } __except(FaultLog("JET bomber body",GetExceptionInformation())) {}
    return JetBody::kind;
}

bool JetLaunchBomber(const float* from,const float* heading,const float* target,const BombLoad& load,DWORD fuelSec,const void* source,
                     JetBody body,const void* hold) noexcept {
    if(!bayOk)return false;
    // The bomber's own model when its body is there this mission, else the strike jet's.
    const Body want=BodyOfBomber(body);
    const Body b=Preloaded(want) ? want : Body::strike;
    __try {
        const Kind& k=KindOf(Row(b).role);
        const float stock=load.speed*60.0f;
        // The bomber's own speed, whatever kind it is (most fly 180 m/s, the Kamui 450).
        const float own=std::isfinite(stock) && stock>k.minSpeed ? stock : k.attack;
        const float speed=own<kBodyTop ? own : kBodyTop;
        float fireDist=0.0f;
        unsigned char* ifc=BayMake(load,speed/60.0f,&fireDist);
        if(!ifc)return false;
        Jet* j=Launch(b,from,heading,target,fuelSec,speed,source);
        if(!j){BayFree(ifc);return false;}
        float dir[3]={heading[0],0.0f,heading[2]};
        if(!Normalize(dir)){dir[0]=0;dir[2]=1;}
        BayState& bay=j->bay;
        bay.ifc=ifc;std::memcpy(bay.bombAt,target,12);std::memcpy(bay.bombDir,dir,12);
        bay.bombOwner=Readable(load.owner,8) ? *static_cast<const void* const*>(load.owner) : nullptr;bay.bombClear=0;bay.hold=hold;
        bay.bombAlt=At<float>(j->Vehicle(),kPosition+4);bay.bombSpeed=speed;bay.fireDist=fireDist;bay.reach=load.reach;
        j->mode=Mode::bomb;j->m.top=speed*1.1f>k.attack*1.3f ? speed*1.1f : k.attack*1.3f;
        Log("JET v=%p bomber: %.0f m/s (its own %.0f) at %.0f m, bay opens %.0f m short, %d to drop, damage %.0f spread %.0f",j->Vehicle(),speed,
            own,bay.bombAlt-target[1],fireDist,At<std::int32_t>(ifc,kIfcShots),load.damage,load.spread);
        Publish(true);
        return true;
    } __except(FaultLog("JET bomber launch",GetExceptionInformation())){return false;}
}

// An impact of `by` (a crash: jet.cpp, playerjet.cpp) at `at`: the impact charge for `radius` (see kCharges),
// fired by `by` with `damage`: its team's enemies hurt, its kills, friends spared as the game's team filter
// spares them for every shell of a side (the IFC's team is its owner's).
bool ImpactDamage(const unsigned char* by,const float* at,float damage,float radius) noexcept {
    if(!by || !at || !std::isfinite(at[0]+at[1]+at[2]) || !std::isfinite(damage) || damage<=0.0f)return false;
    const int c=ChargeFor(radius);
    if(c<0) {
        static ULONGLONG loggedAt=0;
        const ULONGLONG now=GetTickCount64();
        if(now-loggedAt>10000){loggedAt=now;Log("JET impact %.0f damage %.0f m: no impact charge preloaded (python tools/make_jets.py)",damage,radius);}
        return false;
    }
    const float from[3]={at[0],at[1]+kImpactDrop,at[2]};
    const bool fired=Shell(kCharges[c].sgo,chargeReady[c],by,from,at,damage,true,"impact charge");
    if(fired && Cfg().debug)Log("JET impact by %p at (%.0f,%.0f,%.0f): %.0f damage, %.0f m charge (asked %.0f m)",by,at[0],at[1],at[2],damage,
                              kCharges[c].radius,radius);
    return fired;
}
// A bite of the drill tank's drill (drill.cpp): the drill charge fired by `by` straight from `from` (the drill's base)
// at `at` (what it touches) with `damage`; its team's enemies hurt, its kills, the map's buildings and rocks too.
bool DrillCharge(const unsigned char* by,const float* from,const float* at,float damage) noexcept {
    if(!by || !from || !at || !std::isfinite(from[0]+from[1]+from[2]+at[0]+at[1]+at[2]) || !std::isfinite(damage) || damage<=0.0f)return false;
    if(!drillReady) {
        static ULONGLONG loggedAt=0;
        const ULONGLONG now=GetTickCount64();
        if(now-loggedAt>10000){loggedAt=now;Log("DRILL no drill charge preloaded this mission (python tools/make_drill.py)");}
        return false;
    }
    return Shell(kDrillChargeSgo,drillReady,by,from,at,damage,true,"drill charge");
}
}  // namespace crew
