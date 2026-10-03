// NPC jets (docs/jet-plan.md). The game has no fighter, so a jet is a Vehicle506_Helicopter body (rigid
// body and .cas collision, HP, weapons on the "body" bone, the stock crash and wreck) spawned from a
// derived SGO (testrange/gen.py: edf6tr_jet_*), told apart by its speed gain k (veh+0x162C), which the
// derived SGO sets to kStrikeMark or kFighterMark (no stock heli is anywhere near). Its NPC pilot does
// nothing; the plugin flies it in two stages a frame:
//  - input (slot 55, from HeliFrame): the target, the guidance step (JetSteer: a wing's lift along the
//    body's up, at most its kind's maxG, so it banks before it turns; thrust and gravity along the path
//    set the speed), and the fire bytes slot 57 reads (0x2020 both guns, 0x2021 the missile;
//    vehicle_weapon_setting puts them along the body's nose), and a bomber's bay (BayFrame);
//  - physics (slot 57, after the stock code wrote the heli's velocity and spin): the body's linear
//    velocity (0x11B18F0) and an angular velocity (0x11B1760) that turns the nose onto that velocity,
//    banked into the turn.
// Roles (Kind): strike (dive attacks on ground targets; JetLaunchBomber's bombers first fly the stock
// bomber's run and drop its bombs) and fighter (flying targets first). Neither reloads; out
// of ammo, out of fuel (cfg.jetFuelSec, a launched sortie cfg.jetSortieSec) or below kWithdrawHp of its HP
// it flies off and is deleted out of the player's sight.
// Two ways in: a mission places one (the test range's CreateFriend: it guards the player), or JetLaunch
// makes one at run time (the airstrike takeovers, airstrike.cpp) exactly like the script's CreateFriend:
// CreateObject on the preloaded SGO, team friend, RideAi(true). Only RideAi with true reads the SGO's
// mission_setup (0x633063 -> slot 46), which is what writes the jet mark, the weapons and the heli
// parameters; it then flies at its strike point from the first frame.
// Time is the plugin's game clock (GameMs): wall time that stops while no vehicle updates (pause menu,
// loading), so a pause neither burns fuel nor makes a jet look gone. A table entry belongs to one object:
// the control block of its weak-this (object +0x30, what Delete 0x118A1B0 hands the manager), which a new
// object at a freed jet's address does not share.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cwchar>
#include <malloc.h>

namespace crew {
namespace {
constexpr unsigned kHeli506=0x17DB238,kPhysics506=0x61B710;   // slot 57 of the 506
constexpr std::size_t kSlotPhysics=57;
constexpr std::size_t kSpeedGain=0x162C,kBody=0x1650;
constexpr float kStrikeMark=7001.0f,kFighterMark=7002.0f;
constexpr unsigned kSetLinearVelocity=0x11B18F0,kSetAngularVelocity=0x11B1760,kDelete=0x118A1B0;
constexpr std::size_t kInLateral=0x1540,kInThrottle=0x1544,kInForward=0x1548,kInW=0x154C,kInYaw=0x1550;
constexpr std::size_t kFireGun=0x2020,kFireMissile=0x2021;
constexpr std::size_t kHpMax=0x2F4,kHp=0x2F8;
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kHolderWeapon=0x10;
constexpr std::size_t kWeaponLockon=0x6B0,kWeaponSpeed=0x894,kWeaponAlive=0x898,kWeaponGravity=0x8E0,kWeaponAmmo=0xBE8;
constexpr std::int32_t kHoming=1;
// The guns are seat weapons 0 and 1 (what 0x2020 fires, testrange/gen.py); after the missile gen.py puts
// the 506's fuel tank (v_fuel01, its "ammo" ~1e6 burnt by the throttle), which is no gun.
constexpr std::uint64_t kGunWeapons=2;
// The stock input (0x6543A0) pushes the body under the ceiling *(*(image+kCeiling)+0x3C) every frame.
constexpr std::size_t kCeiling=0x20B2998,kCeilingY=0x3C;
constexpr float kPi=3.14159265f,kG=9.8f,kGravity=14.7f;

// Flight, per kind (Kind). Speeds m/s, heights m above the target (or the anchor: the player, or where
// it first flew). The stock bombers fly 3 m a frame (180 m/s): the strike jet attacks at that, the fighter
// is faster and pulls harder. Every distance of an attack scales with the turn radius v^2/(n g).
struct Kind {
    float cruise,attack,minSpeed;   // m/s
    float thrust,brake;             // m/s^2 toward the wanted speed (gravity along the path comes on top)
    float maxG;                     // lift: at most this many g
    float roll;                     // rad/s: how fast it rolls (and pitches) its body round
    float alt;                      // m over the target or the anchor it cruises at
    float diveStart,pullAlt,extendOut;   // Strike
    float gunOpen,gunClose;         // m: the guns fire from gunOpen (or their reach) in to gunClose
    float patrol,patrolStep;        // m: patrol circle, plus this per jet
    float overrun,chaseOver;        // Chase
    float range;                    // m from the anchor it takes targets in
};
constexpr Kind kKinds[2]={
    {150.0f,180.0f,85.0f, 10.0f,15.0f, 5.0f,1.4f, 250.0f, 850.0f,70.0f,1500.0f, 500.0f,60.0f, 1000.0f,120.0f, 120.0f,30.0f, 1200.0f},
    {180.0f,195.0f,110.0f,15.0f,20.0f, 7.0f,2.4f, 320.0f, 950.0f,80.0f,1700.0f, 500.0f,60.0f, 1400.0f,150.0f, 160.0f,40.0f, 1800.0f},
};
// m/s^2 of speed lost per g pulled over 1 (induced drag): with thrust that sustains 1 + thrust/kTurnBleed
// g (strike ~4.3, fighter 6); harder it slows, and the turn tightens, as a wing does.
constexpr float kTurnBleed=3.0f;
// s: the path turns toward `want` at angle/kSteerTau (at most maxG). Turning at maxG until within a frame's
// turn and then snapping onto it (as before) asked full bank one frame and none the next: in the patrol
// circle the jets flicked between level and knife edge.
constexpr float kSteerTau=0.5f;
// m/s no jet is commanded past. Havok caps every dynamic body at its motion properties' maxLinearSpeed
// (hknpMotionProperties+0x10, 200 m/s in the preset the vehicles use): the 18:58 run's jets commanded
// 260-360 m/s flew 200-211, so turn radius, lead and bomb release were planned for a speed never flown.
constexpr float kBodyTop=195.0f;
// Patrol: the speed that holds the patrol circle at a 45 degree bank (tan 1), between kLoiterMin times the
// stall speed and cruise. At cruise the circle took 5 g and 78 degrees of bank the whole time.
constexpr float kLoiterTan=1.0f,kLoiterMin=1.15f;
constexpr float kAttGain=6.0f;         // 1/s: the body closes on the attitude it should have this fast
constexpr float kNegG=1.0f;            // g: the most it pushes (lift down the body's up)
constexpr float kMinAlt=25.0f;         // never lower over the ground than this
constexpr float kLookAhead=2.5f;       // s: the ground and the ceiling are checked this far ahead too
constexpr float kLaunchClear=100.0f;   // a launched jet starts at least this high over the ground
constexpr float kCeilingGap=12.0f;
constexpr float kTakeoffClear=30.0f;   // m over the ground: done taking off
// Strike: approach at alt; from diveStart out with the target within kDiveCone of the nose, dive onto the
// gun's lead point (alt over diveStart: ~15-20 deg), guns from gunOpen in to gunClose; pull out under
// pullAlt over the target or gunClose from it, climb back, fly on extendOut and turn in.
constexpr float kDiveCone=0.52f;
constexpr float kGunCone=0.035f,kHitRadius=4.0f;   // rad (2 deg), or what puts kHitRadius on the target
// The guns fire only flying where the nose points (cos 10 deg off): never flank first. And never with the
// player along the rounds' path, or a wingman (kJetSpan) when its rounds cannot pass through (FriendInLine);
// other friends are hit as the stock game hits them.
constexpr float kGunSlip=0.985f,kJetSpan=20.0f;
constexpr float kMissileCone=0.2f,kMissileMin=120.0f,kMissileMax=500.0f;
constexpr ULONGLONG kMissileMs=2500,kPullMs=7000,kExtendMs=12000;
// Fighter: lead pursuit at the target's speed plus chaseOver; closer than overrun it breaks off
// (extends kRunOutMs) so it does not ram or sit on its tail.
constexpr float kFlyerClear=15.0f;
constexpr ULONGLONG kRunOutMs=3000;
// Withdrawing it climbs toward the ceiling and flies away from the player at full speed; it is deleted
// only out there (never in front of the player): kGone from the player, or, held in by the map's edge,
// kGoneStuck after kStuckMs of withdrawing.
constexpr float kWithdrawHp=0.25f,kGone=1600.0f,kGoneStuck=900.0f,kWithdrawClimb=300.0f;
constexpr ULONGLONG kStuckMs=60000;
// The body is held back by what the plugin does not see (the map's edge, buildings, other jets): it is
// blocked once for kBlockedMs it made less than kBlockedPart of the commanded way along it. The command
// then follows the body (along what holds it), and the obstacle, unless another jet or something near the
// ground, is learned as a wall (a vertical plane) that Guard turns off before.
constexpr float kBlockedPart=0.5f,kWallJet=60.0f,kWallGround=40.0f,kWallSame=60.0f;
constexpr ULONGLONG kBlockedMs=250;
// The map's edge (docs/map-edge-re.md): the heli input (slot 55, 0x6543A0) clamps the body into the
// mission's move area shrunk by veh+kAreaInset (0x5A9E50) and teleports it back, every frame, so a jet at
// the edge stopped dead and slid flank first. A jet's inset is set to kNoInset (the box grown 1e6 m: no
// clamp; the stock bombers are never clamped either). Out there the Havok broadphase ends at 3000 m a
// side: walls at kWorldWall keep the jets in, and one past kWorldGone is deleted.
constexpr std::size_t kAreaInset=0xE00;
constexpr float kNoInset=-1.0e6f,kWorldWall=2400.0f,kWorldGone=2700.0f;
constexpr ULONGLONG kStaleMs=1500;
// Flights (docs/bullet-pass-re.md): the jets a mission places are one flight; launched jets from one
// source (an Air Raider's call, a mission's strike) within kFlightGapMs of the last are one. A jet's rounds
// pass through the other jets of its flight: the bullets' candidate collector (vtable kAddBodySlot, slot 0
// addBody kAddBody) leaves out the body of a wingman (kBodyObject: body id -> object) when the bullet's
// owner (core = collector+kCollectorCore, owner at core+kBulletOwner) is a jet of the same flight; all
// else is the stock function's (friendly fire stays as it is).
// Elevons (EDF6VC_JET.MRAB, tools/mdb_jet.py, docs/mdb-format.md §3-4): the tailless bomber's outer
// trailing edges, bones elevon_L/R under bomber501, hinged along their local X. The model instance is at
// veh+kModelInst (bone records at +kInstBones, kBoneStride each, name at +0, local 4x4 at +kBoneLocal,
// count at +kInstBoneCount); local = Rx(theta) x bind (row vectors), theta > 0 = trailing edge up. Both up
// pitch the nose up, opposite they roll: kElevonMax at the full pitch rate (maxG) or roll rate, moved at
// most kElevonRate. The engine makes world = local x parent world each frame.
constexpr std::size_t kModelInst=0xEE0,kInstBones=0x10,kInstBoneCount=0x20,kBoneStride=0x110,kBoneAuto=0x8,kBoneLocal=0x70;
constexpr float kElevonMax=0.35f,kElevonRate=2.0f;
const wchar_t* const kElevonNames[2]={L"elevon_L",L"elevon_R"};
constexpr unsigned kPlacedFlight=1;
constexpr ULONGLONG kFlightGapMs=20000;
constexpr unsigned kAddBodySlot=0x179E128,kAddBody=0x232AA0,kBodyObject=0x108260;
constexpr std::size_t kCollectorCore=0x88,kBulletOwner=0x9A8;
const unsigned char kAddBodySig[]={0x48,0x89,0x4C,0x24,0x08,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,
                                   0x83,0xEC,0x30,0x4C,0x8B,0xF1,0x45,0x33,0xE4,0x44,0x89,0xA4,0x24,0x80,0x00,0x00};
const unsigned char kBodyObjectSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x05};
const unsigned char kBodyObjectSig2[]={0x8B,0xD1,0x48,0x8D,0x48,0x10,0xE8};   // at +11   // game ms: a table entry not flown this long is free
constexpr ULONGLONG kFlyerMemoMs=500;
// Diving it must keep the height a maxG pull-out takes (v^2/(n g) (1 - cos dive)) plus kReact seconds of
// sink: the time to roll the lift round before it pulls.
constexpr float kReact=1.0f;
constexpr std::size_t kSelfCtrl=0x30,kObjFlags=0x18;
constexpr unsigned char kObjDeleted=4;

// Run-time spawning (docs/mission-airstrike-re.md §3): the preload manager *(image+kPreloadMgr), the
// object manager *(image+kObjectMgr), CreateObject(manager, &matrix, path, &InitParam) -> the object (the
// manager owns it), SetTeam(object, team, 1).
constexpr unsigned kPreload=0x7A3780,kCreateObject=0x11945E0,kSetTeam=0x54EE70,kInitParamVtable=0x1762068;
constexpr std::size_t kPreloadMgr=0x20B29A8,kObjectMgr=0x20B2958;
constexpr std::int32_t kTeamFriend=2;
// The bodies (JetBody): the strike and fighter jets (the BOMBER501 model, with elevons), and the strike
// jets that take over a BOMBER401 or BOMBER501_2 in that bomber's own model (tools/make_jets.py).
constexpr int kBodyCount=4;
const wchar_t* const kJetSgo[kBodyCount]={L"app:/object/edf6vc_jet_strike.sgo",L"app:/object/edf6vc_jet_fighter.sgo",
                                          L"app:/object/edf6vc_bomber401.sgo",L"app:/object/edf6vc_bomber501_2.sgo"};
const wchar_t* const kJetFile[kBodyCount]={L"EDF6VC_JET_STRIKE.SGO",L"EDF6VC_JET_FIGHTER.SGO",L"EDF6VC_BOMBER401.SGO",
                                           L"EDF6VC_BOMBER501_2.SGO"};
// A bomber's model -> its body: the mesh bone that names it (bone records as kInstBones says).
struct BomberModel { const wchar_t* bone; JetBody body; };
const BomberModel kBomberModels[]={{L"bomber501_2",JetBody::bomber501_2},{L"bomber401",JetBody::bomber401}};

enum class Mode { takeoff, patrol, approach, dive, pull, extend, chase, runOut, withdraw, bomb };
const char* const kModeNames[]={"takeoff","patrol","approach","dive","pull","extend","chase","runOut","withdraw","bomb"};

// The bomb bay of a jet that takes over a bomber (JetLaunchBomber): an IndirectFireControl of its own, set
// up as BombingPlane_Init (0x5AABB0) sets up the bomber's (plane+0xC20) and driven as the bomber's update
// (0x5AB240) drives it: opened (0x2B4340) when the target is fireDist ahead along the line, which is what
// the bomber computes into +0xC14 (frames of fire (0x2B8470) x speed a frame x target_adjust +
// target_distance), then stepped once a frame (0x2B95A0) with the drop point target_distance ahead of the
// nose at the target's height (+0x20) and the release point the jet itself (+0x300, used as +0x2F9 says).
// The bombs are the bomber's: its bombing_plane_param, damage, spread, seed and owner.
constexpr unsigned kIfcCtor=0x2B3940,kIfcDtor=0x2B3C90,kIfcConfig=0x2B5F40,kIfcOwner=0x2B8390,kIfcDamage=0x2B82E0,
                   kIfcSpread=0x2B8460,kIfcFrames=0x2B8470,kIfcOpen=0x2B4340,kIfcStep=0x2B95A0,kIfcDone=0x2B7B90;
constexpr std::size_t kIfcSize=0x600,kIfcAim=0x20,kIfcShots=0x2F0,kIfcFromJet=0x2F9,kIfcFrom=0x300;
constexpr float kLineGain=250.0f;
// ms the bay's last bombs (and a cluster's bomblets) still pass the bomber's flight after it closes.
constexpr ULONGLONG kBombClearMs=15000;   // m off the bombing line that turn it back at the most

struct Jet {
    unsigned char* vehicle;
    const void* ctrl;        // the vehicle's weak-this control block: which object this entry is
    bool fighter;
    Mode mode;
    ULONGLONG bornAt,seen,modeAt,missileAt,loggedAt;
    LARGE_INTEGER last;
    float vel[3],omega[3];   // what the physics stage writes
    bool ready;              // vel/omega hold this frame's command
    float anchor[3];         // where it patrols when there is no player
    const void* target;
    bool flyer;
    float aim[3],tgtPrev[3],tgtVel[3];
    float out[3];            // extend / run-out direction
    bool reap;               // withdrawn: delete from another object's update (JetReap)
    const char* why;         // why it withdrew
    bool launched;           // made by JetLaunch: anchor is its strike point
    unsigned flight;         // its rounds pass through the other jets of this flight (kPlacedFlight)
    const unsigned char* model;   // the bone array its elevons were found in (null: not looked yet)
    unsigned char* elevon[2];     // their bone records, null without (a model without elevons)
    float elevonBind[2][16],elevonAt[2],elevonSet[2][16];
    bool elevonWritten,elevonLogged;
    ULONGLONG gateAt;             // the last gun gate log (Fire)
    ULONGLONG fuelMs;
    unsigned char* ifc;      // the bomb bay (see kIfcCtor), or nullptr
    float bombAt[3],bombDir[3],bombAlt,bombSpeed,fireDist,reach;
    bool bombing;            // the bay is open
    const void* bombOwner;   // whose bombs the bay drops (the caller: the bomb rounds' owner)
    ULONGLONG bombClear;     // game ms until which the owner's rounds still pass its flight (0: bay open)
    float top;               // m/s it never goes past: its kind's, or a faster bomber's speed
    float prevPos[3];        // where the body was at prevAt (Sense)
    ULONGLONG prevAt,blockedFor;
    float real;              // m/s the body really flies (game time, smoothed): against Len(vel), what it is told
};
constexpr int kMaxJets=64,kPatrolRings=6;
Jet jets[kMaxJets]{};

// Walls learned this mission (see kBlockedPart): where one was met and its horizontal normal, into it.
struct Wall { float at[3],n[3]; };
Wall walls[16]{};
int wallCount=0;
unsigned wallNext=0;


const void* SelfCtrl(const unsigned char* v) noexcept { return At<const void*>(v,kSelfCtrl); }

// The entry flying `v` (the same object, flown within kStaleMs), or nullptr.
Jet* FindJet(const unsigned char* v,ULONGLONG ms) noexcept {
    for(auto& j:jets)
        if(j.vehicle==v && j.ctrl==SelfCtrl(v) && ms-j.seen<=kStaleMs)return &j;
    return nullptr;
}

// A slot for a new jet: an empty one, else one not flown for kStaleMs; every other entry of `v` is
// cleared. nullptr with all kMaxJets flying.
Jet* FreeSlot(const unsigned char* v,ULONGLONG ms) noexcept {
    Jet* free=nullptr;
    for(auto& j:jets) {
        if(j.vehicle==v)j=Jet{};
        if(!free && (!j.vehicle || ms-j.seen>kStaleMs)) {
            // A bay left by a jet that stopped being flown (the mission ended): not torn down, as the game
            // it was made in may be gone.
            if(j.ifc)Log("JET bay %p of a vanished jet left alone",j.ifc);
            free=&j;
        }
    }
    return free;
}

float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
float Len(const float* a) noexcept { return std::sqrt(Dot(a,a)); }
float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
void Cross(const float* a,const float* b,float* out) noexcept {
    const float c[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
    std::memcpy(out,c,12);
}
// Scales `a` to unit length; false (and `a` untouched) when it is too short.
bool Normalize(float* a) noexcept {
    const float l=Len(a);
    if(!std::isfinite(l) || l<1e-4f)return false;
    a[0]/=l;a[1]/=l;a[2]/=l;
    return true;
}
float HorizDist(const float* a,const float* b) noexcept {
    const float dx=a[0]-b[0],dz=a[2]-b[2];
    return std::sqrt(dx*dx+dz*dz);
}

bool IsJetVehicle(const unsigned char* v,bool* fighter) noexcept {
    if(!Readable(v,kSpeedGain+4) || At<const unsigned char*>(v,0)!=image+kHeli506)return false;
    const float k=At<float>(v,kSpeedGain);
    if(fighter)*fighter=k==kFighterMark;
    return k==kStrikeMark || k==kFighterMark;
}

void SetMode(Jet& j,Mode m,ULONGLONG ms) noexcept {
    if(j.mode==m)return;
    if(cfg.debug)Log("JET v=%p %s -> %s",j.vehicle,kModeNames[static_cast<int>(j.mode)],kModeNames[static_cast<int>(m)]);
    j.mode=m;j.modeAt=ms;
}

// Metres of ground (terrain, buildings) under `p`, or -1 with none seen.
float Clearance(const float* p) noexcept {
    const float down[3]={p[0],p[1]-400.0f,p[2]};
    float hit[3];
    return MapRay(p,down,hit)>=0.0f ? p[1]-hit[1] : -1.0f;
}

float Ceiling() noexcept {
    const auto p=At<const unsigned char*>(image,kCeiling);
    if(!p || !Readable(p+kCeilingY,4))return 1e9f;
    const float y=At<float>(p,kCeilingY);
    return std::isfinite(y) ? y : 1e9f;
}

// The pilot's seat weapons: guns (straight, fastest round speed for the lead), the homing missile.
struct Arms { float gunSpeed,gunGravity,gunRange; std::int32_t guns,missiles; bool hasGun,hasMissile; };
Arms ReadArms(unsigned char* v) noexcept {
    Arms a{240.0f,0.0f,400.0f,0,0,false,false};
    if(SeatCount(v)==0)return a;
    const auto seat=SeatAt(v,0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(count>8 || !Readable(holders,count*8))return a;
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,kWeaponAmmo+4))continue;
        const std::int32_t ammo=At<std::int32_t>(w,kWeaponAmmo);
        if(At<std::int32_t>(w,kWeaponLockon)==kHoming){a.hasMissile=true;a.missiles+=ammo>0 ? ammo : 0;continue;}
        const float speed=At<float>(w,kWeaponSpeed)*60.0f,reach=At<float>(w,kWeaponSpeed)*static_cast<float>(At<std::int32_t>(w,kWeaponAlive));
        if(i>=kGunWeapons || !std::isfinite(speed) || speed<=1.0f)continue;
        a.guns+=ammo>0 ? ammo : 0;
        if(!a.hasGun || speed>a.gunSpeed) {
            a.hasGun=true;a.gunSpeed=speed;
            const float g=At<float>(w,kWeaponGravity);
            a.gunGravity=std::isfinite(g) && g>0.0f ? g : 0.0f;
            a.gunRange=std::isfinite(reach) && reach>0.0f ? reach : 400.0f;
        }
    }
    return a;
}

// Where to point the guns to hit `aim` moving at `tv` from `from` (round flight time and drop).
void Lead(const float* from,const float* aim,const float* tv,const Arms& a,float* out) noexcept {
    std::memcpy(out,aim,12);
    for(int pass=0;pass<2;++pass) {
        const float d[3]={out[0]-from[0],out[1]-from[1],out[2]-from[2]};
        const float t=Len(d)/a.gunSpeed;
        for(int i=0;i<3;++i)out[i]=aim[i]+tv[i]*t;
        out[1]+=0.5f*a.gunGravity*kGravity*t*t;
    }
}

// The target: flyers first for a fighter (any enemy more than kFlyerClear over the ground), ground
// enemies first for a strike jet; nearest to the jet among those within `range` of `anchor`, the current
// one counting 100 m nearer.
struct Pick { Jet* j; const float* pos; const float* anchor; float range; ULONGLONG ms; const void* best; float score,aim[3]; bool flyer; };
// Whether `object` at `p` flies (no ground within the probe, or more than kFlyerClear over it); one ray
// per object per kFlyerMemoMs, shared by every jet.
struct FlyerMemo { const void* object; ULONGLONG at; bool flyer; };
FlyerMemo flyerMemo[256]{};
bool Flies(const void* object,const float* p,ULONGLONG ms) noexcept {
    auto& m=flyerMemo[(reinterpret_cast<std::uintptr_t>(object)>>4)&255];
    if(m.object==object && ms-m.at<kFlyerMemoMs)return m.flyer;
    const float clear=Clearance(p);
    m={object,ms,clear<0.0f || clear>kFlyerClear};
    return m.flyer;
}

void VisitTarget(void* ctx,const void* object,const float* p) noexcept {
    auto& k=*static_cast<Pick*>(ctx);
    const float d[3]={p[0]-k.anchor[0],p[1]-k.anchor[1],p[2]-k.anchor[2]};
    if(Dot(d,d)>k.range*k.range)return;
    const bool flyer=Flies(object,p,k.ms);
    const float f[3]={p[0]-k.pos[0],p[1]-k.pos[1],p[2]-k.pos[2]};
    float score=Len(f);
    if(object==k.j->target)score-=100.0f;
    if(flyer!=k.j->fighter)score+=2000.0f;   // the other role's kind of target: only with none of its own
    if(!k.best || score<k.score){k.best=object;k.score=score;std::memcpy(k.aim,p,12);k.flyer=flyer;}
}

const Kind& KindOf(const Jet& j) noexcept { return kKinds[j.fighter ? 1 : 0]; }

// A wing's flight step. The lift it wants: gravity held up plus the turn toward `want` (unit), at most
// maxG; `up` gets that lift's direction across the path, where Attitude rolls the body. The lift it gets
// is only along the body's up as it is (`bodyUp`), at most maxG and kNegG pushing: so it banks first and
// then turns, like a wing, instead of sliding round. The speed closes on `speed` at thrust/brake, while
// gravity along the path takes it off climbing and adds it diving.
void JetSteer(Jet& j,const Kind& k,const float* fwd,const float* bodyUp,const float* want,float speed,float dt,float* up) noexcept {
    float dir[3]={j.vel[0],j.vel[1],j.vel[2]};
    float s=Len(dir);
    if(s<1.0f || !Normalize(dir)){std::memcpy(dir,fwd,12);s=k.minSpeed;}
    const float most=k.maxG*kG/(s>k.minSpeed ? s : k.minSpeed)*dt;
    const float angle=std::acos(Clamp(Dot(dir,want),-1.0f,1.0f));
    const float eased=angle*(dt<kSteerTau ? dt/kSteerTau : 1.0f);
    const float turn=eased<most ? eased : most;
    float next[3];
    if(angle<1e-5f)std::memcpy(next,want,12);
    else {
        float axis[3];Cross(dir,want,axis);
        if(!Normalize(axis)){axis[0]=0;axis[1]=1;axis[2]=0;}
        float side[3];Cross(axis,dir,side);   // in the turn plane, toward `want`
        for(int i=0;i<3;++i)next[i]=dir[i]*std::cos(turn)+side[i]*std::sin(turn);
        Normalize(next);
    }
    float lift[3]={0,kG,0};
    for(int i=0;i<3;++i)lift[i]+=(next[i]-dir[i])/dt*s;
    const float along=Dot(lift,dir);
    for(int i=0;i<3;++i)lift[i]-=dir[i]*along;
    std::memcpy(up,lift,12);
    if(!Normalize(up)){up[0]=0;up[1]=1;up[2]=0;}
    // What the wing gives along the body's up now, and the path that bends.
    const float pull=Clamp(Dot(lift,bodyUp),-kNegG*kG,k.maxG*kG);
    float acc[3]={bodyUp[0]*pull,bodyUp[1]*pull-kG,bodyUp[2]*pull};
    const float accAlong=Dot(acc,dir);
    for(int i=0;i<3;++i)next[i]=dir[i]+(acc[i]-dir[i]*accAlong)*dt/s;
    if(!Normalize(next))std::memcpy(next,dir,12);
    const float bleed=pull>kG ? (pull/kG-1.0f)*kTurnBleed : 0.0f;
    s+=Clamp(speed-s,-k.brake*dt,k.thrust*dt)-(kG*next[1]+bleed)*dt;
    const float top=j.top>0.0f ? j.top : k.attack*1.3f;
    s=Clamp(s,k.minSpeed*0.8f,top<kBodyTop ? top : kBodyTop);
    for(int i=0;i<3;++i)j.vel[i]=next[i]*s;
}

// The angular velocity that turns the body's rows (right, up, forward at veh+0x60) onto `nose` and
// `up`: sin(angle) * axis from the three rows, times kAttGain.
void Attitude(Jet& j,const Kind& k,const unsigned char* v,const float* nose,const float* up) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float* r=m;const float* u=m+4;const float* f=m+8;
    float rx[3];Cross(u,f,rx);
    const float hand=Dot(rx,r)>=0.0f ? 1.0f : -1.0f;   // right = hand * up x forward
    float right[3];Cross(up,nose,right);
    for(int i=0;i<3;++i)right[i]*=hand;
    float w[3]={0,0,0},c[3];
    Cross(r,right,c);for(int i=0;i<3;++i)w[i]+=c[i];
    Cross(u,up,c);for(int i=0;i<3;++i)w[i]+=c[i];
    Cross(f,nose,c);for(int i=0;i<3;++i)w[i]+=c[i];
    for(int i=0;i<3;++i)w[i]*=0.5f*kAttGain;
    const float l=Len(w);
    if(l>k.roll)for(int i=0;i<3;++i)w[i]*=k.roll/l;
    std::memcpy(j.omega,w,12);
}

// The bone record named `name` in model instance `inst`, or nullptr.
unsigned char* BoneRecord(const unsigned char* inst,const wchar_t* name) noexcept {
    if(!Readable(inst,kInstBoneCount+4))return nullptr;
    const auto count=At<std::int32_t>(inst,kInstBoneCount);
    const auto bones=At<unsigned char*>(inst,kInstBones);
    if(count<=0 || count>256 || !Readable(bones,static_cast<std::size_t>(count)*kBoneStride))return nullptr;
    for(std::int32_t i=0;i<count;++i) {
        unsigned char* rec=bones+static_cast<std::size_t>(i)*kBoneStride;
        const auto n=At<const wchar_t*>(rec,0);
        if(n && Readable(n,32) && std::wcsncmp(n,name,16)==0)return rec;
    }
    return nullptr;
}

// The elevons after the commanded turn (see kElevonMax).
void Elevons(Jet& j,const Kind& k,unsigned char* v,float dt) noexcept {
    const unsigned char* inst=v+kModelInst;
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    if(!bones)return;
    if(bones!=j.model) {
        j.model=bones;j.elevonWritten=false;
        for(int i=0;i<2;++i) {
            j.elevon[i]=BoneRecord(inst,kElevonNames[i]);
            if(j.elevon[i])std::memcpy(j.elevonBind[i],j.elevon[i]+kBoneLocal,64);
            j.elevonAt[i]=0.0f;
        }
        Log("JET v=%p elevons: %s (auto %d/%d) among %d bones",v,j.elevon[0] && j.elevon[1] ? "found" : "none in this model",
            j.elevon[0] ? j.elevon[0][kBoneAuto] : -1,j.elevon[1] ? j.elevon[1][kBoneAuto] : -1,At<std::int32_t>(inst,kInstBoneCount));
    }
    if(!j.elevon[0] || !j.elevon[1])return;
    // Something else writing them (the animation) would undo every frame: said once.
    if(j.elevonWritten && !j.elevonLogged && (std::memcmp(j.elevon[0]+kBoneLocal,j.elevonSet[0],64) ||
                                             std::memcmp(j.elevon[1]+kBoneLocal,j.elevonSet[1],64))) {
        j.elevonLogged=true;
        Log("JET v=%p elevons: their local matrices were rewritten by the game between frames",v);
    }
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float* r=m;const float* u=m+4;const float* f=m+8;   // r: the model's +x, the right wing
    float c[3];
    Cross(j.omega,f,c);const float pitch=Dot(c,u);             // nose toward the body's up
    Cross(j.omega,r,c);const float roll=-Dot(c,u);             // right wing going down
    const float s=Len(j.vel),pitchMax=k.maxG*kG/(s>k.minSpeed ? s : k.minSpeed);
    const float p=Clamp(pitch/pitchMax,-1.0f,1.0f),q=Clamp(roll/k.roll,-1.0f,1.0f);
    const float want[2]={Clamp((p-q)*kElevonMax,-kElevonMax,kElevonMax),Clamp((p+q)*kElevonMax,-kElevonMax,kElevonMax)};
    for(int i=0;i<2;++i) {
        j.elevonAt[i]+=Clamp(want[i]-j.elevonAt[i],-kElevonRate*dt,kElevonRate*dt);
        const float co=std::cos(j.elevonAt[i]),si=std::sin(j.elevonAt[i]);
        const float* b=j.elevonBind[i];
        float* o=j.elevonSet[i];
        for(int x=0;x<4;++x) {
            o[x]=b[x];
            o[4+x]=co*b[4+x]+si*b[8+x];
            o[8+x]=-si*b[4+x]+co*b[8+x];
            o[12+x]=b[12+x];
        }
        std::memcpy(j.elevon[i]+kBoneLocal,o,64);
    }
    j.elevonWritten=true;
}

// A unit direction to `goal` whose climb is limited to `maxClimb` (sine).
void Toward(const float* pos,const float* goal,float* out) noexcept {
    out[0]=goal[0]-pos[0];out[1]=goal[1]-pos[1];out[2]=goal[2]-pos[2];
    if(!Normalize(out)){out[0]=0;out[1]=0;out[2]=1;}
}

// Level flight (climbing or sinking toward `height`) along the horizontal `dir`.
void Level(const float* pos,const float* dir,float height,float* out) noexcept {
    float h[3]={dir[0],0,dir[2]};
    if(!Normalize(h)){h[0]=0;h[2]=1;}
    const float climb=Clamp((height-pos[1])/150.0f,-0.45f,0.6f);
    out[0]=h[0];out[1]=climb;out[2]=h[2];
    Normalize(out);
}

// Learns the wall met at `at` facing `n`, or moves up the one it is (same facing, within kWallSame).
void LearnWall(const float* at,const float* n) noexcept {
    for(int i=0;i<wallCount;++i) {
        Wall& w=walls[i];
        if(Dot(w.n,n)<0.9f || std::fabs((at[0]-w.at[0])*w.n[0]+(at[2]-w.at[2])*w.n[2])>kWallSame)continue;
        std::memcpy(w.at,at,12);
        return;
    }
    Wall& w=walls[wallCount<16 ? wallCount++ : static_cast<int>(wallNext++%16)];
    std::memcpy(w.at,at,12);std::memcpy(w.n,n,12);
    Log("JET wall learned at (%.0f,%.0f,%.0f) facing (%.2f,%.2f)",at[0],at[1],at[2],n[0],n[2]);
}

// Another jet within kWallJet of `pos` (what a jet runs into is no wall).
bool JetNear(const Jet& self,const float* pos,ULONGLONG ms) noexcept {
    for(const auto& o:jets) {
        if(&o==&self || !o.vehicle || ms-o.seen>kStaleMs)continue;
        const float d[3]={o.prevPos[0]-pos[0],o.prevPos[1]-pos[1],o.prevPos[2]-pos[2]};
        if(Dot(d,d)<kWallJet*kWallJet)return true;
    }
    return false;
}

// The way the body went since the last frame against the commanded velocity (see kBlockedPart). Blocked,
// the command turns along what holds it, so the nose never points where the jet cannot go. Returns
// whether it was blocked this frame.
bool Sense(Jet& j,const float* pos,ULONGLONG ms) noexcept {
    const ULONGLONG since=j.prevAt ? ms-j.prevAt : 0;
    const float moved[3]={pos[0]-j.prevPos[0],pos[1]-j.prevPos[1],pos[2]-j.prevPos[2]};
    std::memcpy(j.prevPos,pos,12);j.prevAt=ms;
    // The game steps a frame at a time: a slow frame moves the body no more than 1/60 s of the way.
    const float wall=static_cast<float>(since)*0.001f,s=Len(j.vel),dt=wall<1.0f/60.0f ? wall : 1.0f/60.0f;
    if(since)j.real+=(Len(moved)/(static_cast<float>(since)*0.001f)-j.real)*0.1f;
    if(!since || !j.ready || s<1.0f)return false;
    const float dir[3]={j.vel[0]/s,j.vel[1]/s,j.vel[2]/s};
    if(Dot(moved,dir)>=s*dt*kBlockedPart){j.blockedFor=0;return false;}
    j.blockedFor+=since;
    if(j.blockedFor<kBlockedMs)return false;
    j.blockedFor=0;
    float n[3]={dir[0]*s-moved[0]/dt,0.0f,dir[2]*s-moved[2]/dt};
    if(Len(n)<s*0.3f || !Normalize(n))return false;   // held from below or above: Guard's
    const float clear=Clearance(pos);
    const bool jet=JetNear(j,pos,ms);
    if(!jet && (clear<0.0f || clear>kWallGround))LearnWall(pos,n);
    float slide[3]={dir[0],dir[1],dir[2]};
    const float into=Dot(slide,n);
    for(int i=0;i<3;++i)slide[i]-=n[i]*into;
    if(!Normalize(slide)){slide[0]=n[2];slide[1]=0;slide[2]=-n[0];}
    // Along it at the speed the body kept (at least the least it flies at), not at the commanded speed:
    // a jump to that sideways is the snap that slid the body flank first.
    const float kept=Clamp(Dot(moved,slide)/dt,KindOf(j).minSpeed,s);
    for(int i=0;i<3;++i)j.vel[i]=slide[i]*kept;
    Log("JET v=%p blocked (%.0f of %.0f m/s)%s: turned along it at %.0f",j.vehicle,Dot(moved,dir)/dt,s,jet ? " by a jet" : "",kept);
    return true;
}

// A learned wall within `range` ahead of `pos`.
bool NearWall(const float* pos,float range) noexcept {
    for(int i=0;i<wallCount;++i)
        if((walls[i].at[0]-pos[0])*walls[i].n[0]+(walls[i].at[2]-pos[2])*walls[i].n[2]<range)return true;
    return false;
}

// Keeps `want` off the ground and under the ceiling. The ground is the highest under it now and
// kLookAhead seconds along its track; sinking, the lowest it gets is where a maxG pull-out started
// kReact seconds from now bottoms out (so a dive runs down to kMinAlt instead of pulling up 100 m early);
// climbing, the ceiling is checked kLookAhead seconds out.
void Guard(const Jet& j,const float* pos,float* want) noexcept {
    const float s=Len(j.vel);
    // Learned walls: turned off from a turn's radius out (more closing fast), never flown into.
    const float r=s*s/(KindOf(j).maxG*kG);
    for(int i=0;i<wallCount;++i) {
        const Wall& w=walls[i];
        const float gap=(w.at[0]-pos[0])*w.n[0]+(w.at[2]-pos[2])*w.n[2];
        const float closing=j.vel[0]*w.n[0]+j.vel[2]*w.n[2];
        const float reach=r*1.5f+(closing>0.0f ? closing*kReact : 0.0f);
        if(gap>reach)continue;
        const float push=Clamp(1.0f-gap/reach,0.2f,1.0f),into=want[0]*w.n[0]+want[2]*w.n[2];
        if(into<=-push)continue;
        want[0]-=w.n[0]*(into+push);want[2]-=w.n[2]*(into+push);
        Normalize(want);
    }
    const float ahead[3]={pos[0]+j.vel[0]*kLookAhead,pos[1]+j.vel[1]*kLookAhead,pos[2]+j.vel[2]*kLookAhead};
    const float probe[3]={ahead[0],pos[1]>ahead[1] ? pos[1] : ahead[1],ahead[2]};
    const float here=Clearance(pos),there=Clearance(probe);
    const float lowest=probe[1]-(there>=0.0f ? there : 1e9f);
    const float floorY=(here>=0.0f ? pos[1]-here : -1e9f)>lowest ? pos[1]-here : lowest;
    float bottom=pos[1];
    if(s>1.0f && j.vel[1]<0.0f) {
        const float sinDive=Clamp(-j.vel[1]/s,0.0f,1.0f),cosDive=std::sqrt(1.0f-sinDive*sinDive);
        bottom-=s*s/(KindOf(j).maxG*kG)*(1.0f-cosDive)-j.vel[1]*kReact;
    }
    if(bottom<floorY+kMinAlt) {
        const float need=Clamp((floorY+kMinAlt-bottom)/40.0f,0.3f,0.8f);
        if(want[1]<need){want[1]=need;Normalize(want);}
    }
    const float top=Ceiling()-kCeilingGap;
    const float rising=pos[1]+(j.vel[1]>0.0f ? j.vel[1]*kLookAhead : 0.0f);
    if(rising>top && want[1]>-0.15f){want[1]=-0.15f;Normalize(want);}
}

// The circle round the anchor, patrol out (plus patrolStep per jet, so they do not share one circle),
// counterclockwise. Returns the speed to fly it at (see kLoiterTan).
float Patrol(const Jet& j,const float* pos,const float* anchor,float height,float* want) noexcept {
    const Kind& k=KindOf(j);
    const float r=k.patrol+k.patrolStep*static_cast<float>((&j-jets)%kPatrolRings);
    float out[3]={pos[0]-anchor[0],0,pos[2]-anchor[2]};
    float dist=Len(out);
    if(!Normalize(out)){out[0]=1;out[2]=0;dist=0.0f;}
    const float tangent[3]={out[2],0,-out[0]};
    const float pull=Clamp((r-dist)/r,-1.5f,1.5f);
    const float dir[3]={tangent[0]+out[0]*pull,0,tangent[2]+out[2]*pull};
    Level(pos,dir,height,want);
    return Clamp(std::sqrt(r*kG*kLoiterTan),k.minSpeed*kLoiterMin,k.cruise);
}

// Whether `at` lies inside the circle the jet turns on toward it (level, at its kind's g, at its speed):
// it cannot bring the nose onto it without flying out first.
bool InsideTurn(const Jet& j,const float* pos,const float* at) noexcept {
    const Kind& k=KindOf(j);
    const float s=Len(j.vel),r=s*s/(kG*std::sqrt(k.maxG*k.maxG-1.0f));
    float v[3]={j.vel[0],0,j.vel[2]};
    if(!Normalize(v))return false;
    const float to[3]={at[0]-pos[0],0,at[2]-pos[2]};
    const float side=to[0]*v[2]-to[2]*v[0]>=0.0f ? 1.0f : -1.0f;   // the target off the (v.z,-v.x) side or not
    const float c[3]={pos[0]+v[2]*side*r-at[0],0,pos[2]-v[0]*side*r-at[2]};
    return Dot(c,c)<r*r;
}

// Strike attack (see kDiveCone). Returns whether the guns may fire (diving at the lead point).
bool Strike(Jet& j,const float* pos,const float* lead,float height,ULONGLONG ms,float* want,float* speed) noexcept {
    const Kind& k=KindOf(j);
    const float dh=HorizDist(pos,lead),over=pos[1]-lead[1];
    const float to[3]={lead[0]-pos[0],0,lead[2]-pos[2]};
    float vdir[3]={j.vel[0],0,j.vel[2]};
    if(!Normalize(vdir)){vdir[0]=to[0];vdir[2]=to[2];Normalize(vdir);}
    float toN[3]={to[0],0,to[2]};Normalize(toN);
    const float off=std::acos(Clamp(Dot(vdir,toN),-1.0f,1.0f));
    *speed=k.attack;
    switch(j.mode) {
    case Mode::dive:
        if(over<k.pullAlt || dh<k.gunClose*0.7f || Len(to)<k.gunClose){SetMode(j,Mode::pull,ms);break;}
        Toward(pos,lead,want);
        return true;
    case Mode::pull:
        if(pos[1]>=height-20.0f || ms-j.modeAt>kPullMs) {
            std::memcpy(j.out,vdir,12);SetMode(j,Mode::extend,ms);break;
        }
        want[0]=vdir[0];want[1]=0.7f;want[2]=vdir[2];Normalize(want);
        return false;
    case Mode::extend:
        if(dh>k.extendOut || ms-j.modeAt>kExtendMs){SetMode(j,Mode::approach,ms);break;}
        Level(pos,j.out,height,want);
        return false;
    default:
        if(j.mode!=Mode::approach)SetMode(j,Mode::approach,ms);
        break;
    }
    // Approach: at the target at height; dive once in the window, else fly out and come round.
    if(dh<=k.diveStart && dh>k.gunClose*2.0f && off<kDiveCone && over>k.pullAlt+30.0f){SetMode(j,Mode::dive,ms);Toward(pos,lead,want);return true;}
    if(off>kDiveCone && InsideTurn(j,pos,lead)){std::memcpy(j.out,vdir,12);SetMode(j,Mode::extend,ms);Level(pos,vdir,height,want);return false;}
    Level(pos,to,height,want);
    *speed=k.cruise;
    return false;
}

// Air-to-air: lead pursuit; breaks off when it overruns.
bool Chase(Jet& j,const float* pos,const float* lead,ULONGLONG ms,float* want,float* speed) noexcept {
    const Kind& k=KindOf(j);
    const float d[3]={lead[0]-pos[0],lead[1]-pos[1],lead[2]-pos[2]};
    if(j.mode==Mode::runOut) {
        if(ms-j.modeAt<kRunOutMs){std::memcpy(want,j.out,12);*speed=k.attack;return false;}
        SetMode(j,Mode::chase,ms);
    }
    if(j.mode!=Mode::chase)SetMode(j,Mode::chase,ms);
    if(Len(d)<k.overrun) {
        float dir[3]={j.vel[0],j.vel[1]+Len(j.vel)*0.3f,j.vel[2]};
        if(!Normalize(dir)){dir[0]=0;dir[1]=0.3f;dir[2]=1;Normalize(dir);}
        std::memcpy(j.out,dir,12);SetMode(j,Mode::runOut,ms);
        std::memcpy(want,dir,12);*speed=k.attack;return false;
    }
    Toward(pos,lead,want);
    const float s=Len(j.tgtVel)+k.chaseOver;
    *speed=Clamp(s,k.minSpeed+20.0f,k.attack);
    return true;
}

// The bombing run (Mode::bomb): level at bombAlt along the bomber's line through the target at its speed,
// turning back onto the line when off it; the bay opens fireDist (and a frame) short of the target, and
// with the last bomb gone it flies on (extend) and goes on as a strike jet.
void Withdraw(Jet& j,const char* why,ULONGLONG ms) noexcept;

void BombRun(Jet& j,const float* pos,ULONGLONG ms,float* want,float* speed) noexcept {
    const float rel[3]={j.bombAt[0]-pos[0],0,j.bombAt[2]-pos[2]};
    const float side[3]={j.bombDir[2],0,-j.bombDir[0]};
    const float along=Dot(rel,j.bombDir),off=-Dot(rel,side);   // ahead to the target; right of the line
    const float c=Clamp(off/kLineGain,-0.7f,0.7f);
    const float dir[3]={j.bombDir[0]-side[0]*c,0,j.bombDir[2]-side[2]*c};
    Level(pos,dir,j.bombAlt,want);
    *speed=j.bombSpeed;
    if(!j.bombing && along<j.fireDist+j.bombSpeed/60.0f) {
        reinterpret_cast<void(*)(void*)>(image+kIfcOpen)(j.ifc);
        j.bombing=true;
        Log("JET v=%p bay open: %.0f m short of the target, %.0f m off the line, %d to drop",j.vehicle,along,off,At<std::int32_t>(j.ifc,kIfcShots));
    }
    if(j.bombing && At<std::int32_t>(j.ifc,kIfcShots)<=0) {
        Log("JET v=%p bombs away",j.vehicle);
        std::memcpy(j.out,j.bombDir,12);Withdraw(j,"bombs dropped",ms);
    }
}

// Tears down the bay (the game's destructor, then the memory).
void BayFree(unsigned char*& ifc) noexcept {
    if(!ifc)return;
    __try { reinterpret_cast<void(*)(void*)>(image+kIfcDtor)(ifc); } __except(EXCEPTION_EXECUTE_HANDLER) { Log("JET bay %p: fault tearing down",ifc); }
    _aligned_free(ifc);
    ifc=nullptr;
}

// A frame of the open bay: drop point and release point as the bomber's update sets them, one step; torn
// down once the last bomb is out and none it tracks is left.
void BayFrame(Jet& j,const float* pos,const float* nose) noexcept {
    if(!j.ifc || !j.bombing)return;
    float flat[3]={nose[0],0,nose[2]};
    if(!Normalize(flat))std::memcpy(flat,j.bombDir,12);
    alignas(16) const float aim[4]={pos[0]+flat[0]*j.reach,j.bombAt[1],pos[2]+flat[2]*j.reach,1.0f};
    alignas(16) const float from[4]={pos[0],pos[1],pos[2],1.0f};
    std::memcpy(j.ifc+kIfcAim,aim,16);std::memcpy(j.ifc+kIfcFrom,from,16);
    const float frame=1.0f;
    reinterpret_cast<void(*)(void*,const float*)>(image+kIfcStep)(j.ifc,&frame);
    if(At<std::int32_t>(j.ifc,kIfcShots)<=0 && reinterpret_cast<bool(*)(void*)>(image+kIfcDone)(j.ifc)) {
        BayFree(j.ifc);
        j.bombClear=GameMs()+kBombClearMs;
        Log("JET v=%p bay closed",j.vehicle);
    }
}

void Withdraw(Jet& j,const char* why,ULONGLONG ms) noexcept {
    if(j.mode==Mode::withdraw)return;
    j.why=why;SetMode(j,Mode::withdraw,ms);
    Log("JET v=%p withdraws: %s",j.vehicle,why);
}

// The fire bytes: guns while the nose is on the lead point within reach, the missile on a rough aim.
void Fire(Jet& j,unsigned char* v,const float* pos,const float* nose,const float* lead,bool gunsOk,const Arms& a,ULONGLONG ms) noexcept {
    bool gun=false,missile=false;
    if(j.target && cfg.heliFire && j.mode!=Mode::withdraw) {
        const float d[3]={lead[0]-pos[0],lead[1]-pos[1],lead[2]-pos[2]};
        const float dist=Len(d);
        const float miss=dist>1.0f ? std::acos(Clamp(Dot(d,nose)/dist,-1.0f,1.0f)) : 0.0f;
        const float wide=dist>1.0f ? std::atan(kHitRadius/dist) : 1.0f;
        const Kind& k=KindOf(j);
        const float reach=a.gunRange<k.gunOpen ? a.gunRange : k.gunOpen;
        const float path[3]={pos[0]+nose[0]*a.gunRange,pos[1]+nose[1]*a.gunRange,pos[2]+nose[2]*a.gunRange};
        float flight[3]={j.vel[0],j.vel[1],j.vel[2]};
        const bool straight=Normalize(flight) && Dot(flight,nose)>kGunSlip;
        const bool friendly=FriendInLine(pos,path,v);
        gun=gunsOk && straight && a.guns>0 && dist<reach && dist>k.gunClose*0.8f && miss<(wide>kGunCone ? wide : kGunCone) && !friendly;
        if(cfg.debug && dist<reach && ms-j.gateAt>250) {
            j.gateAt=ms;
            Log("JET v=%p gun gate: %s mode=%s dist=%.0f miss=%.1f cone=%.1f deg slip=%.3f friend=%d aimed=%d",v,gun ? "FIRE" : "hold",
                kModeNames[static_cast<int>(j.mode)],dist,miss*180.0f/kPi,(wide>kGunCone ? wide : kGunCone)*180.0f/kPi,
                Dot(flight,nose),friendly,gunsOk);
        }
        missile=a.missiles>0 && dist>kMissileMin && dist<kMissileMax && miss<kMissileCone && ms-j.missileAt>kMissileMs &&
                !FriendInLine(pos,lead,v);
        if(missile)j.missileAt=ms;
    }
    v[kFireGun]=gun;v[kFireMissile]=missile;
}

void JetLog(const Jet& j,const unsigned char* v,const float* pos,const Arms& a,float speed,float clear,ULONGLONG ms) noexcept {
    const float hp=At<float>(v,kHp),hpMax=At<float>(v,kHpMax);
    const float d=j.target ? std::sqrt((j.aim[0]-pos[0])*(j.aim[0]-pos[0])+(j.aim[1]-pos[1])*(j.aim[1]-pos[1])+(j.aim[2]-pos[2])*(j.aim[2]-pos[2])) : 0.0f;
    Log("JET v=%p %s %s y=%.0f clear=%.0f ceil=%.0f spd=%.0f/%.0f real=%.0f vy=%.1f bank=%.0f target=%p%s dist=%.0f guns=%d msl=%d hp=%.0f/%.0f fuel=%.0fs fire=%d/%d",
        v,j.fighter ? "fighter" : "strike",kModeNames[static_cast<int>(j.mode)],pos[1],clear,Ceiling(),Len(j.vel),speed,j.real,j.vel[1],std::acos(Clamp(At<float>(v,kMatrix+0x14)/std::sqrt(1.0f-At<float>(v,kMatrix+0x24)*At<float>(v,kMatrix+0x24)+1e-6f),-1.0f,1.0f))*180.0f/kPi,
        j.target,j.flyer ? "(air)" : "",d,a.guns,a.missiles,hp,hpMax,
        static_cast<float>(j.fuelMs)*0.001f-static_cast<float>(ms-j.bornAt)*0.001f,v[kFireGun],v[kFireMissile]);
    // Each weapon's barrel against the nose: the guns must point where the nose does.
    if(SeatCount(const_cast<unsigned char*>(v))==0)return;
    const auto seat=SeatAt(const_cast<unsigned char*>(v),0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(count>8 || !Readable(holders,count*8))return;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        float at[3],dir[3];
        if(!w)continue;
        if(!GunBarrel(v,w,at,dir)){Log("JET v=%p gun %llu: no barrel frame",v,static_cast<unsigned long long>(i));continue;}
        Log("JET v=%p gun %llu: dir=(%.2f,%.2f,%.2f) nose=(%.2f,%.2f,%.2f) dot=%.2f at=(%.1f,%.1f,%.1f) from the body",v,
            static_cast<unsigned long long>(i),dir[0],dir[1],dir[2],m[8],m[9],m[10],dir[0]*m[8]+dir[1]*m[9]+dir[2]*m[10],
            at[0]-pos[0],at[1]-pos[1],at[2]-pos[2]);
    }
}

using PhysicsFn=void(__fastcall*)(void*);   // slot 57: void(vehicle)
PhysicsFn nextPhysics=nullptr;
using SetVecFn=void(*)(void*,const float*);
using DeleteFn=void(*)(void*);
using KickFn=void(*)(void*,void*);

// Slot 57 of the 506, after the stock step: the jet's velocity and spin replace the heli's.
void __fastcall PhysicsHook(void* vehicle) {
    nextPhysics(vehicle);
    __try {
        auto v=static_cast<unsigned char*>(vehicle);
        const ULONGLONG ms=GameMs();
        Jet* j=FindJet(v,ms);
        if(!j || !j->ready || v[kDead] || ms-j->seen>200 || !IsJetVehicle(v,nullptr))return;
        const auto body=At<void*>(v,kBody);
        if(!body)return;
        alignas(16) float lin[4]={j->vel[0],j->vel[1],j->vel[2],0.0f},ang[4]={j->omega[0],j->omega[1],j->omega[2],0.0f};
        reinterpret_cast<SetVecFn>(image+kSetLinearVelocity)(body,lin);
        reinterpret_cast<SetVecFn>(image+kSetAngularVelocity)(body,ang);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

bool physicsOk=false;
const unsigned char kPhysicsSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8};   // 0x61B710: push rbx; sub rsp,20h; mov rbx,rcx; call
// Both jump through the Havok world interface; only the slot differs (0xA8 linear, 0xB0 angular).
const unsigned char kSetLinSig[]={0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00,0x45,0x33,0xC9,0x4C,0x8B,0x50,0x58,0x49,0x8B,0x42,0x18,0x49,0x8D,0x4A,0x18,0x48,0xFF,0xA0,0xA8,0x00,0x00};
const unsigned char kSetAngSig[]={0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00,0x45,0x33,0xC9,0x4C,0x8B,0x50,0x58,0x49,0x8B,0x42,0x18,0x49,0x8D,0x4A,0x18,0x48,0xFF,0xA0,0xB0,0x00,0x00};
const unsigned char kDeleteSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48};
const unsigned char kPreloadSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
const unsigned char kCreateObjectSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9};
const unsigned char kSetTeamSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x41};
bool spawnOk=false;      // the spawn functions matched (InstallJets)
bool bayOk=false;        // ...and the bomb bay's (kIfcCtor)
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
bool preloaded[kBodyCount]{};   // the jet SGOs were preloaded for this mission (PreloadJets)

// InitParamBase as DemoAirStrike's ctor builds it on its stack (0x5B433A): the vtable, the rest zero.
struct alignas(16) InitParam { const void* vtable; unsigned char rest[0x28]; };
using PreloadFn=void(*)(void*,const wchar_t*,std::int32_t,std::int32_t);
using CreateObjectFn=unsigned char*(*)(void*,const float*,const wchar_t*,InitParam*);
using SetTeamFn=void(*)(void*,std::int32_t,bool);
using RideAiFn=void(*)(void*,bool);

// Whether Mods/OBJECT (next to the game's exe) holds the jet SGO: tools/make_jets.py writes them.
bool JetFileThere(int kind) noexcept {
    wchar_t path[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    wchar_t* slash=n && n<MAX_PATH ? wcsrchr(path,L'\\') : nullptr;
    if(!slash)return false;
    *slash=0;
    if(wcscat_s(path,L"\\Mods\\OBJECT\\")!=0 || wcscat_s(path,kJetFile[kind])!=0)return false;
    return GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;
}
}  // namespace

void PreloadJets() noexcept {
    __try {
        // A new mission, a new map: only the world's walls (see kWorldWall).
        wallCount=0;wallNext=0;
        for(int i=0;i<4;++i) {
            const float x=i<2 ? (i==0 ? 1.0f : -1.0f) : 0.0f,z=i<2 ? 0.0f : (i==2 ? 1.0f : -1.0f);
            walls[wallCount++]=Wall{{x*kWorldWall,0.0f,z*kWorldWall},{x,0.0f,z}};
        }
        if(!spawnOk)return;
        const auto mgr=At<void*>(image,kPreloadMgr);
        for(int k=0;k<kBodyCount;++k) {
            preloaded[k]=mgr && JetFileThere(k);
            if(preloaded[k])reinterpret_cast<PreloadFn>(image+kPreload)(mgr,kJetSgo[k],2,-1);
        }
        Log("JET preload strike=%d fighter=%d bomber401=%d bomber501_2=%d",preloaded[0],preloaded[1],preloaded[2],preloaded[3]);
    } __except(EXCEPTION_EXECUTE_HANDLER){for(auto& p:preloaded)p=false;}
}

namespace {
// Raises `p` to at least kLaunchClear over the ground (terrain or buildings) under it: the jet has a
// rigid body, unlike the rail planes whose start points it takes.
void ClearGround(float* p) noexcept {
    const float top[3]={p[0],p[1]+600.0f,p[2]},bottom[3]={p[0],p[1]-1500.0f,p[2]};
    float hit[3];
    if(MapRay(top,bottom,hit)>=0.0f && p[1]<hit[1]+kLaunchClear)p[1]=hit[1]+kLaunchClear;
}

// CreateFriend's steps (CreateObject, SetTeam, RideAi(true)); the object, deleted again when it is not
// a jet after all (an SGO without the mark), or nullptr.
unsigned char* SpawnJet(int body,const float* m) noexcept {
    InitParam param{image+kInitParamVtable,{}};
    unsigned char* v=reinterpret_cast<CreateObjectFn>(image+kCreateObject)(At<void*>(image,kObjectMgr),m,kJetSgo[body],&param);
    if(!v)return nullptr;
    reinterpret_cast<SetTeamFn>(image+kSetTeam)(v,kTeamFriend,true);
    reinterpret_cast<RideAiFn*>(At<void**>(v,0))[kSlotRideAi](v,true);
    if(IsJetVehicle(v,nullptr))return v;
    Log("JET launch: %p is no jet (mark %.0f): deleted",v,At<float>(v,kSpeedGain));
    reinterpret_cast<DeleteFn>(image+kDelete)(v);
    return nullptr;
}
}  // namespace

namespace {
// JetLaunch's work: the new jet's entry, flying along `heading` at `speed`, or nullptr.
// The flight a jet launched now from `source` joins (see kFlightGapMs).
unsigned FlightFor(const void* source,ULONGLONG ms) noexcept {
    static struct { const void* source; unsigned flight; ULONGLONG at; } last[4]{};
    static unsigned next=kPlacedFlight+1;
    for(auto& l:last)
        if(l.source==source && ms-l.at<kFlightGapMs){l.at=ms;return l.flight;}
    auto& l=last[next%4];
    l.source=source;l.flight=next++;l.at=ms;
    return l.flight;
}

// `body`: a bomber's own (JetBody), else the kind's; a bomber body not preloaded falls back to the kind's.
Jet* Launch(bool fighter,const float* from,const float* heading,const float* target,DWORD fuelSec,float speed,const void* source,
            JetBody body=JetBody::kind) noexcept {
    const int kind=fighter ? 1 : 0;
    int b=body==JetBody::kind ? kind : static_cast<int>(body);
    if(!preloaded[b])b=kind;
    if(!spawnOk || !cfg.jetPilot || !preloaded[b] || !At<void*>(image,kObjectMgr))return nullptr;
    const ULONGLONG ms=GameMs();
    Jet* j=FreeSlot(nullptr,ms);
    if(!j){Log("JET launch: %d jets flying",kMaxJets);return nullptr;}
    float fwd[3]={heading[0],0.0f,heading[2]};
    if(!Normalize(fwd)){fwd[0]=0;fwd[2]=1;}
    float start[3]={from[0],from[1],from[2]};
    ClearGround(start);
    // Rows right, up, forward, position, as BombingPlane_Init builds its matrix (right = up x forward).
    alignas(16) const float m[16]={fwd[2],0,-fwd[0],0, 0,1,0,0, fwd[0],0,fwd[2],0, start[0],start[1],start[2],1};
    unsigned char* v=SpawnJet(b,m);
    if(!v)return nullptr;
    FreeSlot(v,ms);   // entries left at this address by a jet shot down there
    *j=Jet{};j->vehicle=v;j->ctrl=SelfCtrl(v);j->fighter=fighter;j->launched=true;j->bornAt=j->modeAt=j->seen=ms;
    QueryPerformanceCounter(&j->last);
    std::memcpy(j->anchor,target,12);j->mode=Mode::patrol;j->fuelMs=static_cast<ULONGLONG>(fuelSec)*1000;
    j->flight=FlightFor(source,ms);
    for(int i=0;i<3;++i)j->vel[i]=fwd[i]*speed;
    Log("JET v=%p launched: %s (%ls) flight %u from (%.0f,%.0f,%.0f) at (%.0f,%.0f,%.0f) %.0f m/s fuel=%lus driver=%d",v,
        fighter ? "fighter" : "strike",kJetFile[b],j->flight,
        start[0],start[1],start[2],target[0],target[1],target[2],speed,fuelSec,SeatCount(v)>0 && SeatRider(SeatAt(v,0))==Rider::dummy);
    return j;
}

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
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("JET bay: fault setting up (left as is)");
        return nullptr;
    }
}
}  // namespace

bool JetLaunch(bool fighter,const float* from,const float* heading,const float* target,DWORD fuelSec,const void* source) noexcept {
    __try { return Launch(fighter,from,heading,target,fuelSec,kKinds[fighter ? 1 : 0].cruise,source)!=nullptr; }
    __except(EXCEPTION_EXECUTE_HANDLER){Log("JET launch: fault");return false;}
}

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
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return JetBody::kind;
}

bool JetLaunchBomber(const float* from,const float* heading,const float* target,const BombLoad& load,DWORD fuelSec,const void* source,
                     JetBody body) noexcept {
    if(!bayOk)return false;
    __try {
        const Kind& k=kKinds[0];
        const float stock=load.speed*60.0f;
        // The bomber's own speed, whatever kind it is (most fly 180 m/s, the Kamui 450).
        const float own=std::isfinite(stock) && stock>k.minSpeed ? stock : k.attack;
        const float speed=own<kBodyTop ? own : kBodyTop;
        float fireDist=0.0f;
        unsigned char* ifc=BayMake(load,speed/60.0f,&fireDist);
        if(!ifc)return false;
        Jet* j=Launch(false,from,heading,target,fuelSec,speed,source,body);
        if(!j){BayFree(ifc);return false;}
        float dir[3]={heading[0],0.0f,heading[2]};
        if(!Normalize(dir)){dir[0]=0;dir[2]=1;}
        j->ifc=ifc;std::memcpy(j->bombAt,target,12);std::memcpy(j->bombDir,dir,12);
        j->bombOwner=Readable(load.owner,8) ? *static_cast<const void* const*>(load.owner) : nullptr;j->bombClear=0;
        j->bombAlt=At<float>(j->vehicle,kPosition+4);j->bombSpeed=speed;j->fireDist=fireDist;j->reach=load.reach;
        j->mode=Mode::bomb;j->top=speed*1.1f>k.attack*1.3f ? speed*1.1f : k.attack*1.3f;
        Log("JET v=%p bomber: %.0f m/s (its own %.0f) at %.0f m, bay opens %.0f m short, %d to drop, damage %.0f spread %.0f",j->vehicle,speed,own,
            j->bombAlt-target[1],fireDist,At<std::int32_t>(ifc,kIfcShots),load.damage,load.spread);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("JET bomber launch: fault");return false;}
}

namespace {
bool passOk=false;
}  // namespace

// Only without the pass-through (passOk): a wingman of `self`'s flight by the segment.
bool JetInLine(const float* from,const float* to,const void* self) noexcept {
    if(passOk)return false;
    const ULONGLONG ms=GameMs();
    const Jet* me=nullptr;
    for(const auto& o:jets)if(o.vehicle==self)me=&o;
    if(!me)return false;
    for(const auto& o:jets)
        if(o.vehicle && &o!=me && o.flight==me->flight && o.prevAt && ms-o.seen<=kStaleMs && NearLine(from,to,o.prevPos,kJetSpan))return true;
    return false;
}

namespace {
using AddBodyFn=void(__fastcall*)(void*,std::uint32_t);
using BodyObjectFn=const void*(__fastcall*)(std::uint32_t);
AddBodyFn nextAddBody=nullptr;
struct PassLog { ULONGLONG at; unsigned passed,strangers; } passLog{};

// Whether a round of `owner` is a bomb (or bomblet) of a bomber in `target`'s flight: the stock bombers
// have no body to hit, ours do, and their bombs leave the bay inside them.
bool BombOf(const void* owner,const Jet& target,ULONGLONG ms) noexcept {
    for(const auto& j:jets)
        if(j.vehicle && j.flight==target.flight && j.bombOwner==owner && (j.ifc || ms<j.bombClear))return true;
    return false;
}

const Jet* FlownJet(const void* v,ULONGLONG ms) noexcept {
    if(!v)return nullptr;
    for(const auto& j:jets)if(j.vehicle==v && ms-j.seen<=kStaleMs)return &j;
    return nullptr;
}

// Whether the bullet whose candidate collector this is, fired by a jet, would take in `body` of a wingman.
bool Wingman(void* collector,std::uint32_t body) noexcept {
    const ULONGLONG ms=GameMs();
    bool any=false;
    for(const auto& j:jets)any=any || (j.vehicle && ms-j.seen<=kStaleMs);
    if(!any)return false;
    const auto core=At<const unsigned char*>(collector,kCollectorCore);
    if(!core)return false;
    const void* const owner=At<const void*>(core,kBulletOwner);
    const Jet* target=FlownJet(reinterpret_cast<BodyObjectFn>(image+kBodyObject)(body),ms);
    if(!target)return false;
    const Jet* shooter=FlownJet(owner,ms);
    const bool pass=shooter ? shooter!=target && shooter->flight==target->flight : owner && BombOf(owner,*target,ms);
    if(pass)++passLog.passed;
    else if(!shooter && owner)++passLog.strangers;
    if(cfg.debug && ms-passLog.at>2000 && (passLog.passed || passLog.strangers)) {
        Log("BULLET through wingmen: %u candidates passed, %u near a jet from a non-jet owner (last %p)",passLog.passed,passLog.strangers,owner);
        passLog=PassLog{ms,0,0};
    }
    return pass;
}

void __fastcall AddBodyHook(void* collector,std::uint32_t body) {
    bool pass=false;
    __try { pass=Wingman(collector,body); } __except(EXCEPTION_EXECUTE_HANDLER) { pass=false; }
    if(!pass)nextAddBody(collector,body);
}
}  // namespace

bool IsJet(const void* vehicle) noexcept {
    return IsJetVehicle(static_cast<const unsigned char*>(vehicle),nullptr);
}

void JetFrame(unsigned char* v) noexcept {
    if(!physicsOk)return;
    const ULONGLONG ms=GameMs();
    Jet* j=FindJet(v,ms);
    LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    if(!j) {
        j=FreeSlot(v,ms);
        if(!j)return;   // kMaxJets flying: this one hovers until a slot frees
        *j=Jet{};j->vehicle=v;j->ctrl=SelfCtrl(v);IsJetVehicle(v,&j->fighter);j->bornAt=j->modeAt=ms;j->last=now;
        std::memcpy(j->anchor,pos,12);j->mode=Mode::takeoff;j->flight=kPlacedFlight;j->fuelMs=static_cast<ULONGLONG>(cfg.jetFuelSec)*1000;
        Log("JET v=%p crewed: %s, hp=%.0f, ceiling=%.0f",v,j->fighter ? "fighter" : "strike",At<float>(v,kHp),Ceiling());
    }
    j->seen=ms;
    Put<float>(v,kAreaInset,kNoInset);
    if(std::fabs(pos[0])>kWorldGone || std::fabs(pos[2])>kWorldGone) {
        if(!j->reap)Log("JET v=%p at the world's edge (%.0f,%.0f): deleting",v,pos[0],pos[2]);
        j->reap=true;
    }
    const float dt=Clamp(static_cast<float>(now.QuadPart-j->last.QuadPart)/static_cast<float>(freq.QuadPart),0.004f,0.1f);
    j->last=now;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float nose[3]={m[8],m[9],m[10]};
    if(!Normalize(nose)){nose[0]=0;nose[1]=0;nose[2]=1;}
    // The stock input stays out of it: rotor spinning, no stick.
    Put<float>(v,kInLateral,0.0f);Put<float>(v,kInForward,0.0f);Put<float>(v,kInYaw,0.0f);
    Put<float>(v,kInThrottle,1.0f);Put<float>(v,kInW,1.0f);

    const Arms arms=ReadArms(v);
    const bool follow=player.at && ms-player.at<10000;
    // A launched jet works round its strike point; a placed one guards the player. Either withdraws away
    // from the player (`viewer`), so it is deleted out of their sight.
    const float* anchor=follow && !j->launched ? player.pos : j->anchor;
    const float* viewer=follow ? player.pos : anchor;
    const float hp=At<float>(v,kHp),hpMax=At<float>(v,kHpMax);
    if(ms-j->bornAt>j->fuelMs)Withdraw(*j,"fuel",ms);
    else if(hpMax>0.0f && hp<hpMax*kWithdrawHp)Withdraw(*j,"damaged",ms);
    else if(arms.guns<=0 && arms.missiles<=0 && (arms.hasGun || arms.hasMissile))Withdraw(*j,"out of ammo",ms);

    const bool walled=Sense(*j,pos,ms);

    // The target and its motion.
    const Kind& kind=KindOf(*j);
    Pick pick{j,pos,anchor,kind.range,ms,nullptr,0.0f,{},false};
    if(j->mode!=Mode::withdraw && j->mode!=Mode::takeoff)VisitEnemies(v,&VisitTarget,&pick);
    if(pick.best) {
        const bool same=pick.best==j->target;
        for(int i=0;i<3;++i) {
            const float raw=(pick.aim[i]-j->tgtPrev[i])/dt;
            j->tgtVel[i]=same && std::fabs(raw)<80.0f ? j->tgtVel[i]+(raw-j->tgtVel[i])*0.2f : 0.0f;
        }
        std::memcpy(j->tgtPrev,pick.aim,12);std::memcpy(j->aim,pick.aim,12);
        j->target=pick.best;j->flyer=pick.flyer;
    } else j->target=nullptr;
    float lead[3];
    if(j->target)Lead(pos,j->aim,j->tgtVel,arms,lead);
    else std::memcpy(lead,pos,12);

    // Guidance.
    const float clear=Clearance(pos);
    const float base=j->target && !j->flyer ? j->aim[1] : anchor[1];
    const float height=base+kind.alt;
    float want[3]={nose[0],0,nose[2]},speed=kind.cruise;
    bool gunsOk=false;
    switch(j->mode) {
    case Mode::takeoff:
        want[0]=nose[0];want[1]=0.6f;want[2]=nose[2];Normalize(want);
        if(clear>kTakeoffClear || clear<0.0f)SetMode(*j,Mode::patrol,ms);
        break;
    case Mode::bomb:
        BombRun(*j,pos,ms,want,&speed);
        break;
    case Mode::withdraw: {
        // A bomber flies on along its run; the others away from the player.
        const bool bomber=j->bombSpeed>0.0f;
        float away[3]={pos[0]-viewer[0],0,pos[2]-viewer[2]};
        if(bomber)std::memcpy(away,j->bombDir,12);
        if(!Normalize(away)){away[0]=nose[0];away[2]=nose[2];}
        const float top=Ceiling()-kCeilingGap*2.0f,climb=viewer[1]+kWithdrawClimb;
        Level(pos,away,climb<top ? climb : top,want);speed=bomber && j->bombSpeed>kind.attack ? j->bombSpeed : kind.attack;
        const float d[3]={pos[0]-viewer[0],pos[1]-viewer[1],pos[2]-viewer[2]};
        const float gone=Len(d),turn=Len(j->vel)*Len(j->vel)/(kind.maxG*kG);
        const bool edge=walled || NearWall(pos,turn*1.5f);
        if(gone>kGone || (gone>kGoneStuck && (edge || ms-j->modeAt>kStuckMs))){
            if(!j->reap)Log("JET v=%p out of sight (%.0f m from the player): deleting",v,gone);
            j->reap=true;
        }
        break;
    }
    default:
        if(!j->target) {
            if(j->mode!=Mode::patrol)SetMode(*j,Mode::patrol,ms);
            speed=Patrol(*j,pos,anchor,height,want);
        } else if(j->flyer)gunsOk=Chase(*j,pos,lead,ms,want,&speed);
        else gunsOk=Strike(*j,pos,lead,height,ms,want,&speed);
        break;
    }
    Guard(*j,pos,want);
    float up[3];
    float bodyUp[3]={m[4],m[5],m[6]};
    if(!Normalize(bodyUp)){bodyUp[0]=0;bodyUp[1]=1;bodyUp[2]=0;}
    JetSteer(*j,kind,nose,bodyUp,want,speed,dt,up);
    float dir[3]={j->vel[0],j->vel[1],j->vel[2]};
    if(!Normalize(dir))std::memcpy(dir,nose,12);
    Attitude(*j,kind,v,dir,up);
    Elevons(*j,kind,v,dt);
    j->ready=true;
    BayFrame(*j,pos,nose);
    Fire(*j,v,pos,nose,lead,gunsOk,arms,ms);
    if(cfg.debug && ms-j->loggedAt>1000){j->loggedAt=ms;JetLog(*j,v,pos,arms,speed,clear,ms);}
}

void JetReap(const void* self) noexcept {
    const ULONGLONG ms=GameMs();
    for(auto& j:jets) {
        // The bay of a jet shot down (no longer flown, its bombs still tracked) goes with it.
        if(j.ifc && j.vehicle && ms-j.seen<=kStaleMs && Readable(j.vehicle,kDead+1) && j.vehicle[kDead])BayFree(j.ifc);
        if(!j.vehicle || !j.reap || j.vehicle==self)continue;
        unsigned char* v=j.vehicle;
        // Only the same object, flown a moment ago (game time): a jet shot down meanwhile is the game's to
        // clean up (and may be gone).
        const bool live=ms-j.seen<=kStaleMs && Readable(v,kSeats+8) && SelfCtrl(v)==j.ctrl;
        const void* const ctrl=j.ctrl;
        if(live)BayFree(j.ifc);
        j=Jet{};
        if(!live || v[kDead] || (v[kObjFlags]&kObjDeleted) || SelfCtrl(v)!=ctrl || !IsJetVehicle(v,nullptr))continue;
        if(SeatCount(v)>0 && SeatRider(SeatAt(v,0))==Rider::dummy)reinterpret_cast<KickFn>(image+kSeatKick)(v,SeatAt(v,0));
        reinterpret_cast<DeleteFn>(image+kDelete)(v);
        Log("JET v=%p gone (deleted)",v);
    }
}

bool InstallJets() noexcept {
    __try {
        const bool sig=Matches(kPhysics506,kPhysicsSig,sizeof(kPhysicsSig)) && Matches(kSetLinearVelocity,kSetLinSig,sizeof(kSetLinSig)) &&
                       Matches(kSetAngularVelocity,kSetAngSig,sizeof(kSetAngSig)) && Matches(kDelete,kDeleteSig,sizeof(kDeleteSig));
        if(!sig){Log("JET profile mismatch: jets off");return false;}
        const auto slot=reinterpret_cast<void**>(image+kHeli506)+kSlotPhysics;
        void* const current=*slot;
        if(current!=image+kPhysics506)Log("JET physics: chaining onto %p (another plugin)",current);
        nextPhysics=reinterpret_cast<PhysicsFn>(current);
        physicsOk=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&PhysicsHook));
        spawnOk=physicsOk && Matches(kPreload,kPreloadSig,sizeof(kPreloadSig)) && Matches(kCreateObject,kCreateObjectSig,sizeof(kCreateObjectSig)) &&
                Matches(kSetTeam,kSetTeamSig,sizeof(kSetTeamSig)) && Readable(image+kInitParamVtable,8);
        bayOk=spawnOk;
        for(const auto& b:kBaySigs)bayOk=bayOk && Matches(b.rva,b.bytes,sizeof(b.bytes));
        const auto passSlot=reinterpret_cast<void**>(image+kAddBodySlot);
        if(Matches(kAddBody,kAddBodySig,sizeof(kAddBodySig)) && Matches(kBodyObject,kBodyObjectSig,sizeof(kBodyObjectSig)) &&
           Matches(kBodyObject+11,kBodyObjectSig2,sizeof(kBodyObjectSig2)) && *passSlot) {
            void* const was=*passSlot;
            if(was!=image+kAddBody)Log("JET bullets: addBody chaining onto %p (another plugin)",was);
            nextAddBody=reinterpret_cast<AddBodyFn>(was);
            passOk=PatchVtableSlot(passSlot,was,reinterpret_cast<void*>(&AddBodyHook));
        }
        Log("HOOK jets physics=%d spawn=%d bay=%d wingmenPass=%d",physicsOk,spawnOk,bayOk,passOk);
        return physicsOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
