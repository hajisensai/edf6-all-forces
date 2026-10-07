// Player-flown jets (docs/player-jet-re.md). A player jet is, like the NPC jets (jet.cpp), a Vehicle506_Helicopter
// body from a derived SGO (testrange/gen.py 'edf6tr_pjet_*', tools/make_jets.py EDF6VC_PJET_*.SGO) told apart by
// its mark (body506.cpp BodyOf: PluginBody::playerJet; kKinds, 7201-7202). The plugin never crews it (crew.cpp
// Crew): it stands empty until the player boards it with the stock board button; then, while the player holds
// seat 0, the plugin flies it as a fixed-wing arcade plane, in two stages a frame like jet.cpp:
//  - input (slot 55, after the stock step, from crew.cpp InputHook): the stock heli inputs are read off the
//    seat (the left and right sticks and the ascend trigger, as the stock heli reads them), the heli's own
//    input block is zeroed (no rotor lift, no heli steering), and the flight step runs: on the ground it
//    taxis and rolls along its nose, accelerates with the throttle and lifts off at its rotate speed; in the
//    air it flies as a wing does (Air): its lift grows with the square of its speed (slow, it cannot hold itself
//    up: it sinks, a stall), along the plane's own up (banked or inverted, gravity is no longer held off), the
//    engine pulls against a drag that grows with speed and with the g it pulls (climbs and hard turns cost
//    speed, dives gain it); Ace Combat's controls: the right stick / mouse turns (it banks into the turn and
//    pulls to hold its height) and pitches, the left stick's sideways rolls it about its nose, let go it levels
//    and its flight control holds a little under 1 g (it settles into a shallow sink). On the keyboard and mouse
//    (the seat's pad flag off) the mouse moves an aim in the world (heading and elevation: AimSteer) the plane
//    banks and pulls toward, W / ascend pull up, S pushes down, A / D roll (held, they fly it by hand and the
//    aim follows the nose), the boost and brake keys (ini) work the throttle; touching down gently
//    it lands and rolls out, hitting the ground (or a building, or anything else in its way) hard it is damaged,
//    destroyed at 0 HP; what it rams takes damage too (ImpactDamage);
//  - physics (506 slot 57, body506.cpp -> PlayerJetBodyStep): its linear and angular velocity, as jet.cpp writes
//    them. Parked (stopped on the ground, throttle closed) nothing is written: the stock heli code holds it.
// Every step is in the game's time (body506.h GameStep): the game moves a body 1/60 s of its velocity a frame
// however long the frame took, so a slow frame is neither a faster plane nor a wall it was held back by.
// Water is not ground for it: the 506's ditching (twice its HP in damage every frame, body506.cpp's message hook)
// never reaches it; touching the water is a crash in the plugin's own model, and afloat it breaks up.
// The primary trigger fires the two guns as the stock 506 does (holders 0 and 1, fire byte 0x2020). The secondary
// fires the store picked (stores.h: its missiles and bombs, one holder each; the switch key or LB cycles them):
// the stock fire byte 0x2021 (holder 2) is taken and that store's own trigger pulled (Stores). What it carries
// weighs on its flight (Burden: thrust, lift and drag) and, a bomb picked, the cockpit shows where it would hit.
// The fighter HUD's symbols (Sight: the gun sight and the lead; Threats: the missiles and locks on it) are gathered
// here each frame for hud.cpp FighterHud (docs/hud-re.md §5); the stock gun aim lines are hidden (crew.cpp AimLines).
// Every other aircraft of the plugin (the NPC jets, carriers, drones, the gunship, the bombers: playerjet_kinds.h) is
// boarded and flown through this same record and these steps; what is theirs alone is in playerjet_board.inc.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "body506.h"
#include "lockon.h"
#include "edf/weapon.h"
#include "gear.h"
#include "heliaim.h"
#include "hover_lift.h"
#include "jetaudio.h"
#include "jetsound_state.h"
#include "layout.h"
#include "memory.h"
#include "online_authority.h"
#include "playarea.h"
#include "sight.h"
#include "vehicleram.h"
#include "playerjet_kinds.h"
#include "pjet_catch.h"
#include "pjet_handling.h"
#include "vecmath.h"
#include "warn.h"
#include <cmath>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
using vec::Clamp;using vec::Cross;using vec::Dot;using vec::Len;using vec::Normalize;
constexpr std::size_t kBody=0x1650;
constexpr std::size_t kInLateral=0x1540,kInThrottle=0x1544,kInForward=0x1548,kInW=0x154C,kInYaw=0x1550;
constexpr std::size_t kObjFlags=0x18,kCtrlUses=8;   // the HP: layout.h
constexpr unsigned char kObjDeleted=4;
// The seat's stick block (docs/heli-input-re.md §4): left stick, right stick, the ascend trigger (the heli's
// collective: analog 0..1 on a pad, 0 or 1 on the keyboard).
constexpr std::size_t kSeatLX=0x2C0,kSeatLY=0x2C4,kSeatRX=0x2D0,kSeatRY=0x2D4,kSeatAscend=0x2E0;
constexpr std::size_t kSeatPad=0x2B0;   // 1: the rider plays on a pad, 0: the keyboard and mouse (heli-input-re.md §4)
constexpr std::size_t kSeatButtons=0x2E8;   // word: pad A B X Y LB RB L3 R3 (docs/stores-re.md §4)
constexpr std::uint16_t kButtonLB=0x10,kButtonX=0x04;
constexpr std::size_t kFireStore=0x2021;    // the 506's secondary fire byte (holder 2)
// The bomb's fall (Impact): at most kFallMost s with no weapon to read its life off; muzzles averaged, at most this many.
constexpr float kFallMost=40.0f;
constexpr std::uint64_t kMostBombMuzzles=8;
constexpr std::size_t kAreaInset=0xE00;   // jet.cpp kAreaInset: the move-area clamp's inset
constexpr float kNoInset=-1.0e6f;
constexpr float kG=9.8f;

struct Kind {
    const char* name;
    int mark;            // its SGO's mark (testrange/gen.py JETS; body506.cpp names the range)
    float minAir;        // m/s: the speed the engine at idle holds in level flight (slower is kStallFloor's)
    float rotate;        // m/s: it can lift off from here (on its own kAutoRotate faster)
    float top;           // m/s at full throttle (the body's own motion properties lift Havok's 200 m/s: jetprops.cpp)
    float thrust;        // m/s^2: the engine at full throttle (as much as the drag at `top`: Air)
    float brake;         // m/s^2: the airbrake (the stick back) at `top`, less as the square of the speed below it
    float maxG;          // the most lift, in g...
    float corner;        // m/s: ...from this speed up; below, maxG * (speed / corner)^2 (1 g at corner / sqrt(maxG))
    float roll;          // rad/s: how fast the body turns onto its attitude
    float landMax;       // m/s: the fastest it can touch down without damage
    float ram;           // m: its ram's blast radius, half its model's width (the fighter's model is 16 m across, the strike jet's 25 m)
};
constexpr Kind kKinds[]={
    {"fighter",7201, 65.0f,75.0f,260.0f, 16.0f,32.0f, 6.0f,150.0f, 2.6f, 130.0f, 8.0f},    // 1 g at 61 m/s
    {"strike", 7202, 60.0f,70.0f,240.0f, 11.0f,26.0f, 5.0f,140.0f, 1.6f, 120.0f, 12.0f},   // 1 g at 63 m/s
};
// Every other aircraft of the plugin the player can board (playerjet_kinds.h, the NPC's own performance): their Kinds.
#include "playerjet_kinds.inc"
constexpr float kAutoRotate=20.0f;     // m/s over rotate: it lifts off without the stick...
constexpr float kAutoThrottle=0.6f;    // ...with the throttle at least this open (not rolling out a landing)
constexpr float kLiftOffClimb=5.0f;    // m/s up the moment it lifts off
constexpr float kThrottleRate=1.0f;    // the throttle lever's travel a second (closed to open in 1 s)
// In the air the throttle is Ace Combat's: held forward (or ascend) it boosts to full, held back it closes and brakes,
// let go it returns to kCruiseThrottle; on the ground it stays where the stick left it (taxi, hold, roll out).
constexpr float kCruiseThrottle=0.55f;
static_assert(kCruiseThrottle==handling::kCruiseThrottle,"pjet_handling.h's cruise is the throttle let go");
constexpr float kAirThrottleRate=1.5f;
constexpr float kDeadZone=0.08f;
constexpr float kTaxiTurn=0.8f;        // rad/s: the slowest taxi turn rate (the nose wheel), less fast
constexpr float kTaxiFull=25.0f;       // ...from this ground speed on (rate times kTaxiFull / speed)
constexpr float kGroundBrake=12.0f;    // m/s^2 rolling with the throttle closed
constexpr float kParkSpeed=0.5f;       // below this, throttle closed: parked (the stock code holds it)
constexpr float kBellyBrake=10.0f;     // m/s^2 sliding on its belly (gear.cpp: landed with the gear not down)
// The floor's slope a wing on the ground follows (Ground, pjet_handling.h GroundUp): its heights kGroundSpan m ahead,
// behind and to either side of its centre; a slope over kGroundTilt rad is no runway (the contacts alone take that).
constexpr float kGroundSpan=3.0f,kGroundTilt=0.35f;
// The plane's own up (PJet::up), carried along its path: the roll rotates it about the nose at its kind's PathRoll; let go
// it returns to the bank the turn stick asks for (its kind's TurnBank at full, a coordinated turn: Air), at most
// kLevelRate, unless the pitch stick is held (kLevelPull: pulled through the top it loops, as in Ace Combat) or it points
// within kVertical of straight up or down (no "level" there: it keeps its up). The rates, the bank and the body's cap per
// kind: pjet_handling.h (until 2026-10-06 kRollRate 4.2 and a 69 deg bank for every kind, the body capped at Kind::roll).
using handling::kLevelRate;
using handling::kMinBankCos;   // a turn's hold (its lift / cos bank) at most 5 g of it
constexpr float kRollDead=0.08f;       // the roll stick under this is let go
constexpr float kLevelPull=0.15f;      // ...and it levels only with the pitch stick under this (held, it loops)
constexpr float kVertical=0.97f;       // sine of the climb past which there is no level to return to
// The lift with the stick let go (Air Hold): the flight control holds kHold of what keeps the path from bending
// (g * cos(climb), along the plane's up), so level flight starts to sink; the nearer the sink to kSettleSink the
// more it holds, all of it there: level flight settles into a sink of kSettleSink (over ~13 s), a climb bends
// slowly over, a dive is held. Never more than the wing gives at its speed.
constexpr float kHold=0.988f,kSettleSink=1.5f;   // m/s
constexpr float kPush=0.5f;            // the stick forward: down to kPush of the most lift, negative
// Drag (Air): parasitic, Kind::thrust * (speed / top)^2 (full throttle levels off at top); induced, kInduced per g^2
// pulled at the corner speed, more as the square of corner / speed (a 6 g turn at the corner: 11 m/s^2).
constexpr float kInduced=0.3f;
constexpr float kStallWarn=1.05f;
// The mouse's aim (AimSteer): its heading turns kAimPerUnit rad per unit of a frame's mouse X (the seat's right stick
// on the keyboard: the frame's movement, at most 1), times ini PlayerJetMouseSpeed, its elevation likewise; the mouse
// takes it no farther than aim::kMaxEl from level (an aim already past, the nose's when W was let go, stays). The plane
// turns its path toward it at kSteer times the angle off (rad/s), the lift for that and for holding the path up (Hold)
// along its up. Turning it banks toward that lift at handling::AimRoll (its PathRoll kAimRollFull off, easing to kLevelRate
// on the aim: a step at kAimTurnFrom before 2026-10-06), the sideways demand eased in near the aim (handling::AimShare,
// times ini PlayerJetAimGain); on the aim it only levels its wings, and not at all climbing or diving steeper than
// kAimSteep (sine): letting W go in a steep climb or over the top of a loop keeps the attitude it has (it snapped upright
// at the full roll rate before,
// 2026-10-04). Under kAimBankMin g of lift it keeps its bank.
constexpr float kMouseMoved=0.02f;    // a frame's mouse movement past this hands the plane to the aim
constexpr float kAimPerUnit=aim::kPerUnit,kSteer=1.6f,kAimBankMin=0.3f,kAimTurnFrom=0.09f,kAimSteep=0.77f;
// The mouse's aim stays on the screen: its mark (kAimMark ahead) within kAimOnScreen of the screen's half size
// (the user, 2026-10-05: the aim ran off the screen and the jet turned on after it, unseen). A frame's mouse that
// would take it out is not taken; one the camera's turn left outside is drawn back toward the flight path.
constexpr float kAimOnScreen=0.85f;
constexpr float kAimMark=800.0f;       // m: the readout's aim and path points ahead of the plane      // the readout's STALL: the most lift under this many times what holds the path
// Angle of attack (jet.cpp kAoaPerG): the nose rides this far above the path per g pulled at the middle of
// the speed range, more as it slows (lift ~ aoa * speed^2), kAoaMin to kAoaMax, eased over kAoaTau s. Only
// pitch, along the body's up: the nose never slips sideways off the path.
constexpr float kAoaPerG=0.026f,kAoaMin=-0.05f,kAoaMax=0.2f,kAoaTau=0.3f;
constexpr float kAttGain=6.0f;         // 1/s: the body closes on its attitude this fast (jet.cpp kAttGain)
// The least speed in the air: its wing gives next to nothing there (6% of 1 g for the fighter), so a plane
// slowed this far (pulled up too long) has its path fall through: the nose drops and it dives out, a stall.
constexpr float kStallFloor=25.0f;
constexpr float kBodyTop=340.0f;       // m/s: the steepest dive's (the drag holds it about there; jetprops.cpp 600)
// The walls where the map's ground ends (playarea.h, WallTurn): a path out through one is turned along it and kWallIn
// back in, so the plane never stops at the wall and never flies out over the void.
constexpr float kWallIn=0.3f;   // past a wall, at least this share of the path points back in
constexpr float kAreaWarn=kEdgeBuffer+500.0f;   // m: heading out at a wall this near, the cockpit's AREA caution
// The ground (Clear): the body's origin rests on the ground (the models are grounded and the boxes measured off
// them, pylib/jet_models.py grounded / vcobjects.on_origin), so under kTouch it is on it; over kOffGround in the air.
constexpr float kTouch=3.0f,kOffGround=6.0f;
constexpr float kFloorGap=1.0f,kFloorSweep=3.0f,kUnderClimb=40.0f;
// Touching down: at most kLandSink m/s down, the wings within kLandBank (cosine of the up row's y), the nose
// no lower than kLandNose (sine), on ground (water is no runway): a landing; else a crash: kCrashBase of its max
// HP plus kCrashPerSink per m/s over kLandSink and kCrashPerSpeed per m/s over landMax (at most kCrashMax), and one
// more at most every kCrashMs (scraping along the ground is one crash; afloat it is one a kCrashMs: it breaks up).
constexpr float kLandSink=10.0f,kLandBank=0.77f,kLandNose=-0.26f;
constexpr float kCrashBase=0.2f,kCrashPerSink=0.04f,kCrashPerSpeed=0.01f,kCrashMax=1.5f,kBankCrash=0.3f;
constexpr ULONGLONG kCrashMs=1000;
// Blocked (a building, an enemy, the map's own walls): for kBlockedMs it made less than kBlockedPart of the way
// it was sent, at kBlockedMin m/s or more in the air, kRollBlockedMin rolling; a crash at the speed it lost (and
// the same share of its max HP, times Cfg().playerJetRamDamage, to the enemies round the impact); in the air it bounces back
// off at minAir, rolling it stops.
constexpr float kBlockedPart=0.5f,kBlockedMin=40.0f,kRollBlockedMin=20.0f;
constexpr ULONGLONG kBlockedMs=150;
constexpr ULONGLONG kLogMs=2000;
// Elevons (jet.cpp Elevons): bones elevon_L/R of the jet model, hinged along their local X.
// The elevons follow the pilot (2026-10-05, the user: the control surfaces should visibly move with the controls): the
// keys' or a pad's pitch and roll stick as they are, else (the mouse's aim) kElevonGain times the turn's share of its
// most; up to kElevonMax (~29 deg), kElevonRate rad/s.
constexpr float kElevonMax=0.5f,kElevonRate=4.0f,kElevonGain=3.0f;
const wchar_t* const kElevonNames[2]={L"elevon_L",L"elevon_R"};

enum class Phase { parked, rolling, air };
const char* const kPhaseNames[]={"parked","rolling","air"};

struct PJet {
    ObjRef ref;                  // the object (a new one at the address is not this one)
    unsigned char* vehicle;
    const Kind* kind;
    bool driven;                 // the player holds seat 0
    bool active;                 // slot 57 writes vel / omega this frame
    bool autopilot;              // flown by the catch's autopilot, no one aboard (AutoFly)
    bool bodyFixed;
    Phase phase;
    float throttle;              // 0..1, the lever the stick moves
    float turnIn,pitchIn;        // the stick's turn and pitch, smoothed (SmoothStick)
    float yawIn,rollIn;          // ...the air's turn (right stick) and roll (left stick sideways), smoothed
    float up[3];                 // the plane's own up in the air (see kLevelPull, pjet_handling.h)
    bool hasUp;
    float throttleIn;            // the stick's throttle command last frame (-1, 0, +1): a change is logged
    float clear,climb;           // its height over the floor and climb last frame (the cockpit readout)
    float load;                  // the g it pulled last frame (the cockpit readout)
    float aim[3];                // the mouse's aim, a world direction (AimSteer); hasAim: set
    bool hasAim;
    bool keys;                   // flown on the keyboard and mouse last frame
    bool mouseFlies;             // ...by the mouse's aim: it moved since a key was last pressed (Air)
    int flier;                   // who flew it last frame: 0 the keys, 1 the mouse's aim, 2 a pad (FlightWatch)
    float watchBank,watchHead;   // its bank and heading kWatchMs ago (FlightWatch), rad
    ULONGLONG watchAt,watchLogAt;
    int store;                   // the store the secondary fires (an index into ReadStores' list)
    bool switchHeld;             // the switch key / LB down last frame
    Burden burden;               // what its stores weigh (BurdenOf)
    int stores;                  // what it carries, for the cockpit
    const char* storeName[kMostStores];
    int storeRounds[kMostStores];
    int storeRole[kMostStores];
    bool bomb,hasImpact;         // the store picked is a bomb; where it would hit now (Impact)
    float impact[3];
    bool targetHeld;             // the target key / X down last frame
    int lock;                    // the picked store's lock (WeaponLock), for the cockpit
    float lockAt[3],lockProgress;
    bool stall;                  // ...and whether all its wing gives is too little to hold its path (kStallWarn)
    float stallShare;            // ...the share of all its wing gives its path needs, kStallWarn over (>= 1: stall)
    Gpws gpws;                   // the ground-proximity warning (Proximity), impactIn s to the impact (<0: none)
    float impactIn;
    int area;                    // the cockpit's AREA state (WallTurn): 2 turned back by a wall, 1 heading out near one
    float vel[3],omega[3];
    float prev[3];               // its position last frame
    bool havePrev;
    float measured[3];           // how fast it went last frame (m/s of game time)
    bool fresh;                  // ...measured this frame
    float sent[3];               // the velocity it was sent with last frame
    float aoa;                   // the nose above the path (rad, see kAoaPerG)
    float savedInset;
    bool insetSaved;
    ULONGLONG lastMs,logAt,crashAt,blockedSince;
    int threat;                  // 2 a missile homing on it, 1 an enemy's missile lock on it, 0 none (StoresStep)
    int flares;                  // flare pairs left (Cfg().playerJetFlares when the entry is made)
    bool flareHeld;
    ULONGLONG flareAt;           // game ms of its last pair
    ULONGLONG frame;             // GameFrame of its last flight step
    ULONGLONG wetFrame;          // GameFrame of the last water message (0: none)
    bool dieLogged;
    const unsigned char* model;  // the bone array the elevons were found in
    unsigned char* elevon[2];
    float elevonBind[2][16],elevonSet[2][16],elevonAt[2];
    float targetWas[3],targetVel[3];   // the picked store's target last frame, its velocity (TrackTarget)
    bool targetSeen;
    PlayerJetSymbols sym;        // the fighter HUD's (Sight, Threats)
    // Any other aircraft of the plugin the player boards (playerjet_board.inc): its row (nullptr: a player jet).
    const pjet::Boardable* board;
    bool steered;                // the plugin steers it by `aim` this frame (the gunship's pylon turn): Air as the autopilot's
    bool keep;                   // left on the ground by the player: it waits there for them (no NPC takes it back)
    float yaw;                   // a rotor craft's heading (rad, the nose at (sin, 0, cos))
    aim::Hold hover;             // ...its mouse-aim flight's setpoint and height held (heliaim.h, HoverStep), its top speed
    float hoverTop;
    ULONGLONG blastAt;           // a charge drone's charge fired (game ms; 0: not)
    bool specialHeld;            // the target key / X down last frame (a special store's own action)
    bool orbiting;               // the gunship's pylon turn round orbitAt (orbitR m out, at the height orbitAlt)
    float orbitAt[3],orbitR,orbitAlt;
    float sight[3];              // where the camera's centre looks (the shells' and drones' aim), hasSight: found
    bool hasSight;
    struct Hail {                // called down for the player (HailTick): its approach, landing and wait
        int phase;               // HailPhase
        ULONGLONG at;            // game ms the phase began
        float stop[3],dir[3],touch[3];   // a wing's strip: where it stops, its heading, where it touches down
        int tries;               // a wing's approaches flown
        bool spot;               // a rotor craft's spot (stop) picked
        int cand;                // the strip search's next candidate
        float best;              // ...the best one's cost so far (0: none)
        float bestStop[3],bestDir[3];
    } hail;
};
// playerjet_board.inc (any of the plugin's aircraft under the player): what the flight steps above call.
void Boarded(PJet& j,unsigned char* v,const float* pos,float clear) noexcept;
void Left(PJet& j,unsigned char* v,float clear,bool alive,bool eject) noexcept;
void HandBack(PJet& j,unsigned char* v,const char* why) noexcept;
float RestOver(const unsigned char* v,const float* pos) noexcept;
float FloorClear(const PJet& j,const unsigned char* v,const float* pos,float clear) noexcept;
void Forget(const unsigned char* v) noexcept;
int SpecialRoom(const PJet& j) noexcept;
int SpecialStore(PJet& j,const unsigned char* v,Store* out) noexcept;
void FireSpecial(PJet& j,unsigned char* v,const Store& st,const float* pos) noexcept;
bool FallsAsBomb(const Store& st) noexcept;
constexpr int kMaxJets=16;
PJet jets[kMaxJets]{};

bool flyOk=false;

float Axis(const unsigned char* seat,std::size_t at) noexcept {
    const float x=At<float>(seat,at);
    if(!std::isfinite(x) || std::fabs(x)<kDeadZone)return 0.0f;
    return Clamp(x,-1.0f,1.0f);
}

const Kind* KindOf(const unsigned char* v) noexcept {
    if(BodyOf(v)==PluginBody::jet)return BoardKindOf(v);   // an NPC aircraft the player may board
    if(BodyOf(v)!=PluginBody::playerJet)return nullptr;
    const int mark=static_cast<int>(BodyMark(v));
    for(const auto& kind:kKinds)if(mark==kind.mark)return &kind;
    return nullptr;
}

// Whether the entry's object is there and alive (subcarrier.cpp Live).
bool Live(const PJet& j) noexcept {
    const auto ctrl=static_cast<const unsigned char*>(j.ref.ctrl);
    return j.vehicle && Readable(ctrl,kCtrlUses+4) && At<std::int32_t>(ctrl,kCtrlUses)>0 && Readable(j.vehicle,kTeam+4) &&
           j.ref.Is(j.vehicle) && !(j.vehicle[kObjFlags]&kObjDeleted) && !j.vehicle[kDead];
}
PJet* Find(const unsigned char* v) noexcept {
    for(auto& j:jets)if(j.vehicle==v && j.ref.Is(v))return &j;
    return nullptr;
}
// A new entry for `v`: a free one, or one whose jet is dead or gone. nullptr: kMaxJets live (said once).
PJet* Make(unsigned char* v,const Kind* kind) noexcept {
    for(auto& j:jets) {
        if(j.vehicle && Live(j))continue;
        j=PJet{};j.ref=ObjRef::Of(v);j.vehicle=v;j.kind=kind;j.flares=Cfg().playerJetFlares;
        return &j;
    }
    static const void* refused=nullptr;
    if(refused!=v)Log("PJET v=%p: %d player jets known already: not flown",v,kMaxJets);
    refused=v;
    return nullptr;
}

// Metres over what is under `p`: the ground or the water's surface, the higher (map rays see the seabed under the
// sea, docs/water-re.md), a void within the walls floored (see below); kNoGround with neither. `water`: it is the water.
float Clear(const float* p,bool* water) noexcept {
    // A void within the walls (the big map's seams) floored at the area's lowest ground (playarea.h FloorClear): a map
    // ray finds nothing there, and with no floor the jet sank on into it uncrashed.
    const float ground=area::FloorClear(MapPlayArea(),p,GroundClearance(p),kNoGround);
    float surface=0.0f;
    *water=false;
    if(SeaAt(p[0],p[2],&surface)!=Sea::water)return ground;
    const float over=p[1]-surface;
    if(ground!=kNoGround && ground<over)return ground;   // a ship's deck, a pier: over the water
    *water=true;
    return over;
}

// The elevons after the body's turn (jet.cpp Elevons): both up pitch the nose up, opposite they roll.
void Elevons(PJet& j,unsigned char* v,float stickPitch,float stickRoll,float dt) noexcept {
    const unsigned char* inst=v+kModelInst506;
    const auto bones=At<const unsigned char*>(inst,kInstBones506);
    if(!bones)return;
    if(bones!=j.model) {
        j.model=bones;
        for(int i=0;i<2;++i) {
            j.elevon[i]=BoneRecord506(inst,kElevonNames[i]);
            if(j.elevon[i])std::memcpy(j.elevonBind[i],j.elevon[i]+kBoneLocal506,64);
            j.elevonAt[i]=0.0f;
        }
        if(Cfg().debug)Log("PJET v=%p elevons: %s",v,j.elevon[0] && j.elevon[1] ? "found" : "none in this model");
    }
    if(!j.elevon[0] || !j.elevon[1])return;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float* r=m;const float* u=m+4;const float* f=m+8;
    float c[3];
    Cross(j.omega,f,c);const float pitch=Dot(c,u);
    Cross(j.omega,r,c);const float roll=-Dot(c,u);
    const float speed=Len(j.vel),pitchMax=j.kind->maxG*kG/(speed>j.kind->minAir ? speed : j.kind->minAir);
    float p=Clamp(kElevonGain*pitch/pitchMax,-1.0f,1.0f),q=Clamp(kElevonGain*roll/j.kind->roll,-1.0f,1.0f);
    if(std::fabs(stickPitch)>=kRollDead)p=Clamp(stickPitch,-1.0f,1.0f);   // the stick held: the surfaces where it puts them
    if(std::fabs(stickRoll)>=kRollDead)q=Clamp(stickRoll,-1.0f,1.0f);
    const float want[2]={Clamp((p-q)*kElevonMax,-kElevonMax,kElevonMax),Clamp((p+q)*kElevonMax,-kElevonMax,kElevonMax)};
    for(int i=0;i<2;++i) {
        j.elevonAt[i]+=Clamp(want[i]-j.elevonAt[i],-kElevonRate*dt,kElevonRate*dt);
        HingePose(j.elevonBind[i],j.elevonAt[i],j.elevonSet[i]);
        std::memcpy(j.elevon[i]+kBoneLocal506,j.elevonSet[i],64);
    }
}

// The pilot's stick, read as the stock heli reads it (docs/player-jet-re.md §2). turn > 0: right (the stock
// yaw is -RX and turns the heading right for RX > 0; the stock lateral -LX moves it right for LX > 0); pitch
// > 0: nose up (stick back / mouse up: the right stick's Y is negative pushed up, as LY is for forward);
// throttle: +1 forward stick or ascend, -1 back stick. turn: either stick sideways (the ground's nose wheel); in the
// air yaw (the right stick: turn) and roll (the left stick's sideways: roll > 0 rolls right) are apart.
// keys (the keyboard and mouse): pitch from W / ascend (+1) and S (-1), throttle from the ini's boost and brake keys,
// no yaw: the mouse (aimX, aimY: the frame's movement, no dead zone; aimY > 0 up) moves the aim instead.
struct Stick { float turn,pitch,throttle,yaw,roll; float lx,ly,rx,ry,ascend; bool keys; float aimX,aimY; bool switchStore,nextTarget,flare; };

// Whether the virtual key `vk` is down while the game has the foreground (0: never).
bool KeyDown(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;   // the map view holds the player's keys (map.cpp)
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}
float Raw(const unsigned char* seat,std::size_t at) noexcept {
    const float x=At<float>(seat,at);
    return std::isfinite(x) ? Clamp(x,-1.0f,1.0f) : 0.0f;
}

Stick ReadStick(const unsigned char* seat) noexcept {
    Stick s{};
    s.lx=Axis(seat,kSeatLX);s.ly=Axis(seat,kSeatLY);s.rx=Axis(seat,kSeatRX);s.ry=Axis(seat,kSeatRY);
    const float a=At<float>(seat,kSeatAscend);
    s.ascend=std::isfinite(a) ? Clamp(a,0.0f,1.0f) : 0.0f;
    s.turn=Clamp(s.rx+s.lx,-1.0f,1.0f);
    s.roll=s.lx;
    s.keys=At<unsigned char>(seat,kSeatPad)==0;
    s.switchStore=s.keys ? KeyDown(Cfg().playerJetSwitchKey) : (At<std::uint16_t>(seat,kSeatButtons)&kButtonLB)!=0;
    s.nextTarget=s.keys ? KeyDown(Cfg().playerJetTargetKey) : (At<std::uint16_t>(seat,kSeatButtons)&kButtonX)!=0;
    s.flare=KeyDown(Cfg().playerJetFlareKey);   // the keyboard's (a pad's buttons are the game's)
    if(s.keys) {
        // The mouse steers only with ini PlayerJetMouseFlight; off, the keys fly the plane alone (W / ascend pull, S
        // push, A / D roll, let go it levels) and the mouse is left to the camera (the user's ask, 2026-10-04).
        if(!Cfg().playerJetMouseFlight)s.turn=s.lx;
        s.aimX=Raw(seat,kSeatRX);
        s.aimY=Cfg().playerJetInvertPitch ? Raw(seat,kSeatRY) : -Raw(seat,kSeatRY);
        s.pitch=(s.ascend>0.5f || s.ly<-0.3f ? 1.0f : 0.0f)-(s.ly>0.3f ? 1.0f : 0.0f);
        s.throttle=(KeyDown(Cfg().playerJetBoostKey) ? 1.0f : 0.0f)-(KeyDown(Cfg().playerJetBrakeKey) ? 1.0f : 0.0f);
        return s;
    }
    s.yaw=s.rx;
    s.pitch=Cfg().playerJetInvertPitch ? s.ry : -s.ry;
    s.throttle=s.ascend>0.5f || s.ly<-0.3f ? 1.0f : s.ly>0.3f ? -1.0f : 0.0f;
    return s;
}

// The stick's turn and pitch through a first-order lag of kStickTau (game time). The mouse reaches the heli's
// right stick as each frame's movement: a run of frames reads 1, 0, 0.6, 0, ... while the hand moves evenly, and
// fed straight to the flight that shook the plane frame to frame. The lag turns it into the even command the
// hand meant; a pad's stick, already even, only gains kStickTau of response time.
constexpr float kStickTau=0.12f;
void SmoothStick(PJet& j,Stick& s,float dt) noexcept {
    const float k=1.0f-std::exp(-dt/kStickTau);
    j.turnIn+=(s.turn-j.turnIn)*k;
    j.pitchIn+=(s.pitch-j.pitchIn)*k;
    j.yawIn+=(s.yaw-j.yawIn)*k;
    j.rollIn+=(s.roll-j.rollIn)*k;
    s.turn=j.turnIn;s.pitch=j.pitchIn;s.yaw=j.yawIn;s.roll=j.rollIn;
}

// The right of a path along `dir`, level: the way a right turn bends it (a heading angle a has its nose at
// (sin a, 0, cos a) and a right turn lowers a; docs/player-jet-re.md §2).
void RightOf(const float* dir,float* right) noexcept {
    right[0]=-dir[2];right[1]=0.0f;right[2]=dir[0];
    if(!Normalize(right)){right[0]=-1.0f;right[1]=0.0f;right[2]=0.0f;}
}

// The walls (playarea.h): where the map's ground ends, kVoidMargin inside it (until 2026-10-06 the play edge, crew.h
// PlayEdge: the physics world's square, km out over the void on a stock map). From kEdgeBuffer m in the share of the
// path allowed out at a wall falls from all of it to none at the wall, and past it the path must point back in by
// kWallIn: the plane is bent round smoothly along it, its heading never flipped (a hard turn at the wall flipped its
// sense between frames: the heading snapped +-17 deg every few seconds along it). Returns the cockpit's AREA state
// (area::EdgeState: 2 turned back by a wall this frame or past one, 1 heading out at one within kAreaWarn m, 0 neither).
int WallTurn(const float* pos,float* dir) noexcept {
    const PlayArea a=MapPlayArea();
    float was[3]={dir[0],dir[1],dir[2]};
    const bool bent=area::EdgeTurn(a,pos,dir,kEdgeBuffer,kWallIn);
    return bent ? 2 : area::EdgeState(a,pos,was,kAreaWarn);
}
// A rotor craft's velocity asked for (HoverStep) bent off the walls as a wing's path is, its speed kept (it hovers: a
// velocity under kWallSlow is left as it is). The same AREA state.
constexpr float kWallSlow=0.5f;
int WallTurnVelocity(const float* pos,float* vel) noexcept {
    const float flat=std::sqrt(vel[0]*vel[0]+vel[2]*vel[2]);
    float dir[3]={vel[0],0.0f,vel[2]};
    if(flat<kWallSlow){dir[0]=0.0f;dir[2]=0.0f;return area::EdgeState(MapPlayArea(),pos,dir,kAreaWarn);}
    dir[0]/=flat;dir[2]/=flat;
    const int state=WallTurn(pos,dir);
    vel[0]=dir[0]*flat;vel[2]=dir[2]*flat;
    return state;
}

// Its death (see body506.h Die506). Without that path it is kept at 1 HP: alive and flying, the next hit the
// game's own death; never 0 HP and still in the air.
void Kill(PJet& j,unsigned char* v,const char* why) noexcept {
    j.active=false;
    if(Die506(v)){Log("PJET v=%p destroyed: %s",v,why);return;}
    Put<float>(v,kHp,1.0f);
    if(!j.dieLogged)Log("PJET v=%p would be destroyed (%s) but the death message is off on this EDF.dll: kept at 1 HP",v,why);
    j.dieLogged=true;
}

// What it rammed (Blocked): where (its nose), and how fast it closed on it along the contact's normal (the speed lost
// along the way it was sent: what the obstacle stopped).
struct RamHit { float at[3]; float closing; };

// The ram's damage to what it hit (the user, 2026-10-05: "the blast of a plane should not only go by its speed but by
// its mass too"): the kinetic energy of the closing speed, E = 1/2 m v^2, with m the aircraft's mass now (its kind's
// clean mass, stores.inc kJetMasses, times what its stores add: Burden) and v the closing speed along the normal; in the
// game's damage at 287 kJ a point (vehicleram.h ram::Damage, the one formula the ground vehicles' ram takes too), scaled
// by the tier the game gave this aircraft (its max HP over its SGO durability: the same factor scales a weapon's damage,
// so the ram keeps pace with the mission's difficulty), times ini PlayerJetRamDamage. A 16 t fighter ramming at 200 m/s
// (320 MJ) hits as about 3/4 of a Mk 82, at 100 m/s a quarter of that. A kind without a mass deals nothing. The blast is
// the kind's size (Kind::ram, half its model's width), never the damage's: the damage already carries mass and speed.
// The aircraft's mass now (kg; 0: its kind has none).
float RamMass(const PJet& j,const unsigned char* v) noexcept {
    const JetMass* const kind=JetMassOf(BodyMark(v));
    return kind ? kind->mass*(j.burden.mass>1.0f ? j.burden.mass : 1.0f) : 0.0f;
}
float RamDamage(const PJet& j,const unsigned char* v,float closing) noexcept {
    const JetMass* const kind=JetMassOf(BodyMark(v));
    if(!kind || !(closing>0.0f))return 0.0f;
    const float kg=RamMass(j,v);
    const float hpMax=At<float>(v,kHpMax);
    const float tier=kind->durability>0.0f && hpMax>0.0f ? hpMax/kind->durability : 1.0f;
    return ram::Damage(kg,closing,tier,Cfg().playerJetRamDamage);
}

void Ram(const PJet& j,unsigned char* v,const RamHit& hit) noexcept {
    const float damage=RamDamage(j,v,hit.closing);
    const bool dealt=damage>0.0f && ImpactDamage(v,hit.at,damage,j.kind->ram);
    Log("PJET v=%p rammed at (%.0f,%.0f,%.0f), closing %.0f m/s, %.0f t: %.0f damage within %.0f m%s",v,hit.at[0],hit.at[1],hit.at[2],
        hit.closing,RamMass(j,v)*0.001f,damage,j.kind->ram,dealt ? "" : " (not dealt: no charge this mission, or no mass for this kind)");
}

// A hard hit: `sink` m/s into the ground, `speed` over it, `banked` wings too steep. Damage (see kCrashBase). `ram`:
// what it rammed (not the ground, not the water): the enemies round it take RamDamage within its kind's reach. At
// most one a kCrashMs.
void Crash(PJet& j,unsigned char* v,float sink,float speed,bool banked,ULONGLONG ms,const RamHit* ram) noexcept {
    if(ms-j.crashAt<kCrashMs)return;
    j.crashAt=ms;
    const float hpMax=At<float>(v,kHpMax),hp=At<float>(v,kHp);
    const float share=Clamp(kCrashBase+kCrashPerSink*(sink>kLandSink ? sink-kLandSink : 0.0f)+
                            kCrashPerSpeed*(speed>j.kind->landMax ? speed-j.kind->landMax : 0.0f)+(banked ? kBankCrash : 0.0f),
                            kCrashBase,kCrashMax);
    const float taken=share*(hpMax>0.0f ? hpMax : 1000.0f),left=hp-taken;
    Log("PJET v=%p crash: sink %.1f m/s, speed %.0f m/s%s: %.0f%% of max HP, hp %.0f -> %.0f",v,sink,speed,banked ? ", banked" : "",
        share*100.0f,hp,left>0.0f ? left : 0.0f);
    if(ram && Cfg().playerJetRamDamage>0.0f)Ram(j,v,*ram);
    if(left<=0.0f){Kill(j,v,"crashed");return;}
    Put<float>(v,kHp,left);
}

// The throttle lever, moved by the stick.
void Lever(PJet& j,const unsigned char* v,const Stick& s,float dt) noexcept {
    if(j.phase==Phase::air) {
        const float want=s.throttle>0.0f ? 1.0f : s.throttle<0.0f ? 0.0f : kCruiseThrottle;
        const float step=kAirThrottleRate*dt;
        j.throttle=j.throttle<want ? (j.throttle+step<want ? j.throttle+step : want) : (j.throttle-step>want ? j.throttle-step : want);
    } else j.throttle=Clamp(j.throttle+s.throttle*kThrottleRate*dt,0.0f,1.0f);
    if(s.throttle!=j.throttleIn) {   // what the throttle keys read as, each time it changes: the controls' evidence
        Log("PJET v=%p throttle %s (LY %.2f ascend %.2f): lever %.2f",v,s.throttle>0.0f ? "up" : s.throttle<0.0f ? "down" : "held",
            s.ly,s.ascend,j.throttle);
        j.throttleIn=s.throttle;
    }
}

// The floor's up under a wing on the ground at `pos`, its level nose `nose` (pjet_handling.h GroundUp): straight up
// where a probe finds no floor (water, the map's edge).
void GroundUpAt(const float* pos,const float* nose,float* up) noexcept {
    const float side[3]={-nose[2],0.0f,nose[0]};
    const float* const dirs[2]={nose,side};
    float h[4];
    for(int d=0;d<2;++d)
        for(int e=0;e<2;++e) {
            const float s=e ? -kGroundSpan : kGroundSpan;
            const float p[3]={pos[0]+dirs[d][0]*s,pos[1],pos[2]+dirs[d][2]*s};
            const float clear=GroundClearance(p);
            if(clear==kNoGround){up[0]=0.0f;up[1]=1.0f;up[2]=0.0f;return;}
            h[d*2+e]=p[1]-clear;
        }
    handling::GroundUp(h[0],h[1],h[2],h[3],kGroundSpan,nose,side,kGroundTilt,up);
}

// On the ground: it rolls along its nose (along the slope under it), turns at the nose wheel's rate, speeds up with the throttle
// and brakes with it closed; it lifts off at its rotate speed with the stick back, or kAutoRotate faster.
void Ground(PJet& j,const unsigned char* v,const Stick& s,float clear,float dt) noexcept {
    const Kind& k=*j.kind;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float nose[3]={m[8],0.0f,m[10]};
    if(!Normalize(nose)){nose[0]=0.0f;nose[2]=1.0f;}
    float speed=Dot(j.vel,nose);
    if(speed<0.0f)speed=0.0f;
    const float want=j.throttle*k.top;
    const bool belly=!GearDown(v);   // on its belly (gear.cpp): it slides to a stop, no thrust, no steering, no takeoff
    if(j.throttle<0.02f || belly)speed-=(belly ? kBellyBrake : kGroundBrake)*dt;
    else speed+=Clamp(want-speed,-k.brake*dt,k.thrust*dt/(j.burden.mass>1.0f ? j.burden.mass : 1.0f));
    if(speed<0.0f)speed=0.0f;
    // The nose wheel: kTaxiTurn at taxi speeds, less from kTaxiFull on.
    const float rate=belly ? 0.0f : kTaxiTurn*(speed>kTaxiFull ? kTaxiFull/speed : 1.0f)*(speed>0.5f || s.throttle>0.0f ? 1.0f : 0.0f);
    const float a=-s.turn*rate*dt,co=std::cos(a),si=std::sin(a);
    const float turned[3]={nose[0]*co+nose[2]*si,0.0f,nose[2]*co-nose[0]*si};
    const float vy=j.measured[1]<0.0f ? (j.measured[1]>-30.0f ? j.measured[1] : -30.0f) : 0.0f;
    for(int i=0;i<3;i+=2)j.vel[i]=turned[i]*speed;
    j.vel[1]=vy;
    float up[3],along[3];
    GroundUpAt(reinterpret_cast<const float*>(v+kPosition),turned,up);
    std::memcpy(along,turned,12);
    const float lift=Dot(along,up);
    for(int i=0;i<3;++i)along[i]-=up[i]*lift;   // its nose along the slope
    if(!Normalize(along))std::memcpy(along,turned,12);
    BodyAttitude(v,along,up,kAttGain,k.roll,j.omega);
    if(!belly && j.throttle>0.02f && speed>=k.rotate && (s.pitch>0.2f || (speed>=k.rotate+kAutoRotate && j.throttle>=kAutoThrottle))) {
        j.phase=Phase::air;j.vel[1]=kLiftOffClimb;j.hasAim=false;
        Log("PJET v=%p takeoff at %.0f m/s (throttle %.2f, stick %.2f)",v,speed,j.throttle,s.pitch);
        return;
    }
    if(clear!=kNoGround && clear>kOffGround) {   // rolled off an edge: flying (or falling to minAir)
        j.phase=Phase::air;j.hasAim=false;
        Log("PJET v=%p off the ground at %.0f m/s (%.0f m over it)",v,speed,clear);
        return;
    }
    j.phase=speed<kParkSpeed && j.throttle<0.02f ? Phase::parked : Phase::rolling;
}

// The transition to ground control: an uncommanded air cruise setting must not power the rollout. An explicit boost
// remains a touch-and-go request. No air lift/aim/warning state survives a supported landing.
void Landed(PJet& j) noexcept {
    j.phase=Phase::rolling;j.vel[1]=0.0f;
    if(j.throttleIn<=0.0f)j.throttle=0.0f;
    j.hasUp=false;j.hasAim=false;j.mouseFlies=false;j.aoa=0.0f;
    j.stall=false;j.stallShare=0.0f;j.load=1.0f;j.gpws=Gpws::none;j.impactIn=-1.0f;
}

// A landing with the gear not down and locked (gear.cpp): a crash (Crash's damage for its sink and speed), then it slides
// on its belly to a stop (Ground) until the gear is down.
void BellyLanding(PJet& j,unsigned char* v,float sink,float speed,ULONGLONG ms) noexcept {
    Log("PJET v=%p belly landing at %.0f m/s, sink %.1f m/s: the gear is not down",v,speed,sink);
    Crash(j,v,sink,speed,false,ms,nullptr);
    Landed(j);
}

// Touching the ground in the air: a landing (it rolls on) or a crash. Touching the water is always a crash.
void Touch(PJet& j,unsigned char* v,float speed,bool water,ULONGLONG ms) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float sink=j.vel[1]<0.0f ? -j.vel[1] : 0.0f;
    const float dirY=speed>1.0f ? j.vel[1]/speed : 0.0f;
    const bool banked=m[5]<kLandBank;
    if(!water && sink<=kLandSink && !banked && dirY>=kLandNose && speed<=j.kind->landMax) {
        if(!GearDown(v)){BellyLanding(j,v,sink,speed,ms);return;}
        Landed(j);
        Log("PJET v=%p landed at %.0f m/s, sink %.1f m/s",v,speed,sink);
        return;
    }
    if(water)Log("PJET v=%p hit the water at %.0f m/s, sink %.1f m/s",v,speed,sink);
    Crash(j,v,sink,speed,banked,ms,nullptr);
}

void ReconcileGround(PJet& j,unsigned char* v,float clear,bool water,bool wet,ULONGLONG ms) noexcept {
    if(j.phase==Phase::air && !water && !wet && (v[0x1580]&2)!=0 && clear!=kNoGround && clear<=kTouch)
        Touch(j,v,Len(j.vel),false,ms);
}

// `v` turned `angle` about the unit `axis` (toward axis x v).
void Turn(float* v,const float* axis,float angle) noexcept {
    float c[3];Cross(axis,v,c);
    const float co=std::cos(angle),si=std::sin(angle),a=Dot(axis,v)*(1.0f-co);
    for(int i=0;i<3;++i)v[i]=v[i]*co+c[i]*si+axis[i]*a;
}

// `u` made across the unit `dir` and unit; false when it lay along it.
bool Across(float* u,const float* dir) noexcept {
    const float a=Dot(u,dir);
    for(int i=0;i<3;++i)u[i]-=dir[i]*a;
    return Normalize(u);
}

// The kind's handling (pjet_handling.h): its path's roll rate (times ini PlayerJetRollScale), its body's rate cap, the
// bank of a full turn stick.
float PathRoll(const Kind& k) noexcept { return handling::PathRoll(k.roll,Cfg().playerJetRollScale); }
float BodyCap(const Kind& k) noexcept { return handling::BodyCap(k.roll,k.maxG,k.corner,Cfg().playerJetRollScale); }
float TurnBank(const Kind& k) noexcept { return handling::TurnBank(k.maxG,k.corner,k.minAir,k.top); }

// The plane's up for this step (see kLevelPull): the roll stick turns it about the path, let go it returns toward the
// bank the turn stick asks for. `level`: the world's up off the path (valid unless `vertical`).
// The plane's up across `dir`: newly in the air, the body's own up.
void EnsureUp(PJet& j,const unsigned char* v,const float* dir,const float* level,bool vertical) noexcept {
    if(j.hasUp && Across(j.up,dir))return;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    j.up[0]=m[4];j.up[1]=m[5];j.up[2]=m[6];
    if(!Across(j.up,dir))std::memcpy(j.up,vertical ? m+8 : level,12);
    if(!Across(j.up,dir)){j.up[0]=0.0f;j.up[1]=1.0f;j.up[2]=0.0f;Across(j.up,dir);}
    j.hasUp=true;
}

// `up` (across `dir`) turned about `dir` toward `want` (across it too), at most `most` rad.
void BankToward(float* up,const float* dir,const float* want,float most) noexcept {
    float c[3];Cross(up,want,c);
    Turn(up,dir,Clamp(std::atan2(Dot(c,dir),Dot(up,want)),-most,most));
    Across(up,dir);
}

void Roll(PJet& j,const unsigned char* v,const Stick& s,const float* dir,const float* level,bool vertical,float dt) noexcept {
    EnsureUp(j,v,dir,level,vertical);
    if(std::fabs(s.roll)>kRollDead) {
        Turn(j.up,dir,s.roll*PathRoll(*j.kind)*dt);   // dir x up is the right: a right roll tips the up toward it
    } else if(!vertical && std::fabs(s.pitch)<kLevelPull) {   // pulling through the top: a loop, not a half roll
        float want[3];std::memcpy(want,level,12);
        Turn(want,dir,Clamp(s.yaw,-1.0f,1.0f)*TurnBank(*j.kind));
        BankToward(j.up,dir,want,std::fmin(kLevelRate,PathRoll(*j.kind))*dt);
    }
    Across(j.up,dir);
}

// The flight's own record of who flies it and of its attitude jumping (the user, 2026-10-05: "flying, it reset my
// direction again"; the 2 s air lines are too coarse to see it): every handover between the keys, the mouse's aim
// and a pad is logged, and so is a bank or heading change of more than kWatchJump in kWatchMs, with the inputs then.
constexpr ULONGLONG kWatchMs=200,kWatchLogMs=500;
constexpr float kWatchJump=0.52f;   // rad (30 deg)
constexpr float kWatchPi=3.14159265f;
const char* const kFliers[]={"keys","mouse aim","pad"};

float BankOf(const float* dir,const float* up) noexcept {
    float level[3]={-dir[0]*dir[1],1.0f-dir[1]*dir[1],-dir[2]*dir[1]};
    if(!Normalize(level))return 0.0f;
    float right[3];Cross(dir,level,right);
    return std::atan2(Dot(up,right),Dot(up,level));
}

void FlightWatch(PJet& j,const unsigned char* v,const Stick& s,const float* dir,int flier) noexcept {
    const ULONGLONG ms=GameMs();
    if(flier!=j.flier) {
        Log("PJET v=%p flown by: %s -> %s (pitch %.2f roll %.2f mouse %.3f,%.3f)",v,kFliers[j.flier],kFliers[flier],s.pitch,s.roll,
            s.aimX,s.aimY);
        j.flier=flier;
    }
    const float bank=BankOf(dir,j.up),head=std::atan2(dir[0],dir[2]);
    if(!j.watchAt){j.watchAt=ms;j.watchBank=bank;j.watchHead=head;return;}
    if(ms-j.watchAt<kWatchMs)return;
    const float db=std::remainder(bank-j.watchBank,2.0f*kWatchPi),dh=std::remainder(head-j.watchHead,2.0f*kWatchPi);
    if((std::fabs(db)>kWatchJump || (std::fabs(dir[1])<0.9f && std::fabs(dh)>kWatchJump)) && ms-j.watchLogAt>kWatchLogMs) {
        j.watchLogAt=ms;
        Log("PJET v=%p attitude jump in %.0f ms: bank %.0f -> %.0f deg, heading %.0f -> %.0f deg, climb %.0f deg; flown by %s "
            "(pitch %.2f roll %.2f mouse %.3f,%.3f keys %d)",v,static_cast<float>(ms-j.watchAt),j.watchBank*180.0f/kWatchPi,bank*180.0f/kWatchPi,
            j.watchHead*180.0f/kWatchPi,head*180.0f/kWatchPi,std::asin(Clamp(dir[1],-1.0f,1.0f))*180.0f/kWatchPi,kFliers[flier],s.pitch,s.roll,s.aimX,
            s.aimY,s.keys);
    }
    j.watchAt=ms;j.watchBank=bank;j.watchHead=head;
}

// The mouse's aim moved by this frame's mouse (see kAimPerUnit; heliaim.h Move): its heading about the world's up, its
// elevation.
void MoveAim(PJet& j,const Stick& s) noexcept { aim::Move(j.aim,s.aimX,s.aimY,kAimPerUnit*Cfg().playerJetMouseSpeed); }

// See kAimOnScreen (heliaim.h KeepOnScreen): the aim as it was (`was`) when this frame's mouse took it off the screen;
// still off (the camera turned), toward the flight path `dir` until it is on.
void KeepAimOnScreen(PJet& j,const unsigned char* v,const float* dir,const float* was) noexcept {
    if(j.autopilot || j.steered)return;   // the catch's autopilot (the pylon turn) aims off the player's screen
    float vp[16];
    if(!LastViewProj(vp))return;
    aim::KeepOnScreen(vp,reinterpret_cast<const float*>(v+kPosition),j.aim,was,dir,kAimMark,kAimOnScreen);
}

// The lift (along the plane's up, as Air's pitch) that turns its path toward the mouse's aim (kSteer) and holds it
// up (`hold`, Hold's: the same slight sag as the stick let go); the plane banked toward where that lift points. Too
// slow for it, the wing gives what it can (a turn too tight for it is wider, a climb too steep sinks).
float AimSteer(PJet& j,const unsigned char* v,const Stick& s,const float* dir,const float* level,bool vertical,float speed,
               float most,float hold,const float* gPerp,float dt) noexcept {
    EnsureUp(j,v,dir,level,vertical);
    if(!j.hasAim){std::memcpy(j.aim,dir,12);j.hasAim=true;}
    float was[3];std::memcpy(was,j.aim,12);
    MoveAim(j,s);
    KeepAimOnScreen(j,v,dir,was);
    const float c=Clamp(Dot(j.aim,dir),-1.0f,1.0f);
    float toward[3]={j.aim[0]-dir[0]*c,j.aim[1]-dir[1]*c,j.aim[2]-dir[2]*c};
    if(!Normalize(toward)) {   // dead ahead: no turn; dead behind: round to the right
        if(c>0.0f)toward[0]=toward[1]=toward[2]=0.0f;
        else RightOf(dir,toward);
    }
    const float off=std::acos(c),turn=kSteer*Cfg().playerJetAimGain*handling::AimShare(off)*off*speed,across=Len(gPerp);
    float lift[3];
    for(int i=0;i<3;++i)lift[i]=toward[i]*turn-(across>1e-4f ? gPerp[i]/across*hold : 0.0f);
    float want[3];std::memcpy(want,lift,12);
    const bool turning=off>=kAimTurnFrom,steep=vertical || std::fabs(dir[1])>kAimSteep;
    if(Len(lift)>kAimBankMin*kG && (turning || !steep) && Across(want,dir))
        BankToward(j.up,dir,want,handling::AimRoll(PathRoll(*j.kind),off)*dt);
    return Clamp(Dot(lift,j.up),-kPush*most,most);
}

// The lift (m/s^2, along the plane's up) its flight control asks for with the pitch stick let go (see kHold):
// `across` the gravity across the path, `sink` m/s down. A turn stick held asks for a coordinated turn: that over
// the cosine of its bank (its height held as it banks), by how far the stick is over.
float Hold(const PJet& j,const Stick& s,const float* level,bool vertical,float across,float sink) noexcept {
    float lift=across*Clamp(1.0f-(1.0f-kHold)*(1.0f-sink/kSettleSink),kHold,1.0f);
    if(vertical || std::fabs(s.yaw)<kRollDead)return lift;
    const float bank=Dot(j.up,level);
    if(bank<=kMinBankCos)return lift;
    return lift*(1.0f+std::fabs(Clamp(s.yaw,-1.0f,1.0f))*(1.0f/bank-1.0f));
}

// In the air (see the file comment): at least kStallFloor. Its lift, along the plane's own up (Roll), at most
// maxG * (speed / corner)^2 g: the pitch stick let go Hold's, pulled more toward the most, pushed toward kPush of
// it below 0; gravity acts on the path whole (banked it sinks, inverted it falls, a roll costs a little height).
// Its speed: the engine (the throttle's share of thrust, kept where the parasitic drag levels it off at the
// throttle's speed), less the drag and the airbrake, less gravity along the path.
void Air(PJet& j,unsigned char* v,const Stick& s,const float* pos,float clear,bool water,float dt,ULONGLONG ms) noexcept {
    const Kind& k=*j.kind;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float dir[3]={j.vel[0],j.vel[1],j.vel[2]};
    float speed=Len(dir);
    if(!Normalize(dir)){dir[0]=m[8];dir[1]=m[9];dir[2]=m[10];if(!Normalize(dir)){dir[0]=0;dir[1]=0;dir[2]=1;}}
    if(speed<kStallFloor)speed=kStallFloor;
    float up[3]={-dir[0]*dir[1],1.0f-dir[1]*dir[1],-dir[2]*dir[1]};   // world up off the path
    const bool vertical=!Normalize(up) || std::fabs(dir[1])>kVertical;
    if(vertical){up[0]=0;up[1]=1;up[2]=0;}
    // The mouse's aim steers unless W / S / A / D fly it by hand (the aim then follows the nose).
    // Who flies it on the keyboard and mouse: the keys while one is down, and on after they are let go (it levels its
    // wings) until the mouse moves; from then the mouse's aim, starting on the nose, until a key is pressed again. So
    // letting a key go hands nothing to an aim the player never touched.
    const bool keyDown=std::fabs(s.pitch)>=kRollDead || std::fabs(s.roll)>=kRollDead;
    if(keyDown)j.mouseFlies=false;
    else if(std::fabs(s.aimX)+std::fabs(s.aimY)>kMouseMoved)j.mouseFlies=true;
    const bool aiming=j.autopilot || j.steered || (s.keys && Cfg().playerJetMouseFlight && j.mouseFlies && !keyDown);
    if(!aiming)Roll(j,v,s,dir,up,vertical,dt);
    const float wing=speed<k.corner ? (speed/k.corner)*(speed/k.corner) : 1.0f;
    const float mass=j.burden.mass>1.0f ? j.burden.mass : 1.0f;   // its mass over clean: the same wing lifts less g
    const float most=k.maxG*kG*wing/mass;   // all the wing gives at this speed
    const float gPerp[3]={dir[0]*kG*dir[1],-kG+dir[1]*kG*dir[1],dir[2]*kG*dir[1]};   // gravity across the path
    const float across=Len(gPerp);   // g * cos(climb)
    const float want=k.minAir+j.throttle*(k.top-k.minAir);
    const float hold=Clamp(Hold(j,s,up,vertical,across,-dir[1]*speed),0.0f,most);
    const float pitch=aiming ? AimSteer(j,v,s,dir,up,vertical,speed,most,hold,gPerp,dt)
                             : s.pitch>=0.0f ? hold+s.pitch*(most-hold) : hold+s.pitch*(hold+kPush*most);
    float lift[3];
    for(int i=0;i<3;++i)lift[i]=j.up[i]*pitch;
    j.stall=most<kStallWarn*across;
    j.stallShare=most>1e-6f ? kStallWarn*across/most : 2.0f;
    float next[3];
    for(int i=0;i<3;++i)next[i]=dir[i]+(lift[i]+gPerp[i])*dt/speed;
    if(!Normalize(next))std::memcpy(next,dir,12);
    j.area=WallTurn(pos,next);
    Across(j.up,next);   // carried along the new path
    if(!aiming){std::memcpy(j.aim,next,12);j.hasAim=s.keys;}
    FlightWatch(j,v,s,next,!s.keys ? 2 : aiming ? 1 : 0);
    float bodyUp[3];std::memcpy(bodyUp,j.up,12);
    const float g=pitch/kG,top2=k.top*k.top;
    j.load=g;
    const float thrust=k.thrust*want*want/top2/mass;   // the engine's force over a heavier jet
    const float slow=k.corner/speed,drag=(k.thrust*speed*speed/top2*(1.0f+j.burden.drag+GearDragShare(v)))/mass+kInduced*g*g*slow*slow*mass;
    const float airbrake=s.throttle<0.0f ? k.brake*speed*speed/top2 : 0.0f;
    speed+=(thrust-drag-airbrake-kG*next[1])*dt;
    speed=Clamp(speed,kStallFloor,kBodyTop);
    for(int i=0;i<3;++i)j.vel[i]=next[i]*speed;
    // The nose above the path by what the wing needs, pitched about the body's right only (no sideslip).
    const float mid=0.5f*(k.minAir+k.top),past=mid/speed;
    const float aoaWant=Clamp(kAoaPerG*g*past*past,kAoaMin,kAoaMax);
    j.aoa+=(aoaWant-j.aoa)*(dt<kAoaTau ? dt/kAoaTau : 1.0f);
    float nose[3]={next[0],next[1],next[2]};
    const float along=Dot(bodyUp,nose);
    for(int i=0;i<3;++i)bodyUp[i]-=nose[i]*along;
    if(Normalize(bodyUp)){const float c=std::cos(j.aoa),sn=std::sin(j.aoa);
        for(int i=0;i<3;++i){const float n=nose[i],u=bodyUp[i];nose[i]=n*c+u*sn;bodyUp[i]=u*c-n*sn;}}
    else std::memcpy(bodyUp,up,12);
    BodyAttitude(v,nose,bodyUp,kAttGain,BodyCap(k),j.omega);
    // The floor (ground or water): under it, out (it went through); touching it or about to within kFloorSweep
    // frames, a landing or a crash, its descent cut to stop kFloorGap over it (jet.cpp HoldOffGround).
    if(clear==kNoGround)return;
    // `clear` is its bottom's (FloorClear): through the ground is its position under the surface, not its wheels a
    // few cm into a bump as it touches down (that is Touch's; jet_flight.cpp HoldOffGround).
    const float rest=RestOver(v,pos);
    if(clear< -rest){j.vel[1]=j.vel[1]>kUnderClimb ? j.vel[1] : kUnderClimb;return;}
    float floorY=pos[1]-clear;
    if(j.vel[1]<0.0f && !water) {
        const float from[3]={pos[0],pos[1]-rest,pos[2]};
        const float end[3]={pos[0]+j.vel[0]*dt*kFloorSweep,from[1]+j.vel[1]*dt*kFloorSweep-kFloorGap,pos[2]+j.vel[2]*dt*kFloorSweep};
        float hit[3];
        if(MapRay(from,end,hit)>=0.0f && hit[1]+rest>floorY && hit[1]<from[1])floorY=hit[1]+rest;
    }
    const float need=(floorY+kTouch-pos[1])/dt;
    if(j.vel[1]>=0.0f || j.vel[1]>=need)return;
    Touch(j,v,speed,water,ms);
    if(j.phase==Phase::air && j.vel[1]<need)j.vel[1]=need<0.0f ? need : 0.0f;
}

// Held back by what it flew or rolled into (see kBlockedPart): a crash (what it rammed takes damage too); in the
// air it bounces off, rolling it stops.
void Blocked(PJet& j,unsigned char* v,const float* pos,ULONGLONG ms) noexcept {
    const float sent=Len(j.sent);
    const float least=j.phase==Phase::air ? kBlockedMin : kRollBlockedMin;
    if(j.phase==Phase::parked || !j.fresh || sent<least || Dot(j.measured,j.sent)>=kBlockedPart*sent*sent){j.blockedSince=0;return;}
    if(!j.blockedSince){j.blockedSince=ms;return;}
    if(ms-j.blockedSince<kBlockedMs)return;
    j.blockedSince=0;
    const float made=Dot(j.measured,j.sent)/sent;
    Log("PJET v=%p blocked %s: sent %.0f m/s, made %.0f",v,kPhaseNames[static_cast<int>(j.phase)],sent,made);
    LogImpact("PJET",v,pos,j.sent);   // what it ran into (impact.cpp)
    // Where it hit: its nose (half its size, Kind::ram, ahead) along the way it was sent; how fast it closed: what it
    // lost of that way. The blast round the nose, its radius the same half size (Ram): what the airframe ran into.
    RamHit ram{};
    for(int i=0;i<3;++i)ram.at[i]=pos[i]+j.sent[i]/sent*j.kind->ram;
    ram.closing=sent-made;
    Crash(j,v,0.0f,sent-made+j.kind->landMax,false,ms,&ram);
    if(!j.active)return;
    if(j.phase!=Phase::air){j.vel[0]=j.vel[2]=0.0f;return;}
    for(int i=0;i<3;i+=2)j.vel[i]=-j.vel[i];
    float dir[3]={j.vel[0],0.2f*Len(j.vel),j.vel[2]};
    if(!Normalize(dir))return;
    for(int i=0;i<3;++i)j.vel[i]=dir[i]*j.kind->minAir;
}

// Where a bomb let go now would hit, as the game flies it (the user's CCIP: it used 9.8 m/s^2, the world's is 14.7):
// from the store's muzzle, at the jet's velocity (AmmoOwnerMove 1) plus its eject along the muzzle (AmmoSpeed, m a
// frame), falling at the world's gravity x its AmmoGravityFactor, stepped a frame at a time for its life (RoundImpact).
// No weapon (the bomb bay of a bomber taken over, playerjet_board.inc): the jet's position, factor 1, no eject.
bool Impact(const PJet& j,const float* pos,const unsigned char* weapon,float* hit) noexcept {
    float g[3],from[3]={pos[0],pos[1],pos[2]},dir[3]={0.0f,0.0f,0.0f};
    if(!edf::WorldGravity(image,g))return false;
    float factor=1.0f,eject=0.0f;
    std::int32_t frames=static_cast<std::int32_t>(kFallMost*60.0f);
    if(weapon) {
        factor=At<float>(weapon,edf::kWeaponAmmoGravity);eject=At<float>(weapon,edf::kWeaponAmmoSpeed);
        frames=At<std::int32_t>(weapon,edf::kWeaponAmmoAlive);
        if(!std::isfinite(factor) || !std::isfinite(eject) || frames<=0)return false;
        if(!edf::MeanMuzzle(weapon,kMostBombMuzzles,from,dir))std::memcpy(from,pos,12);
    }
    const float vel[3]={j.vel[0]/60.0f+dir[0]*eject,j.vel[1]/60.0f+dir[1]*eject,j.vel[2]/60.0f+dir[2]*eject};
    const float drop[3]={g[0]*factor/3600.0f,g[1]*factor/3600.0f,g[2]*factor/3600.0f};
    float took=0.0f;
    return RoundImpact(from,vel,drop,frames,hit,&took);
}

// Its stores (stores.h): the switch (key or LB, on its press) moves to the next with rounds left; one emptied, the
// next; the secondary fire (the stock fire byte, taken so the 506 does not fire holder 2 itself) pulls the picked
// one's trigger; what they weigh; the cockpit's list and, a bomb picked, where it would hit.
// Flares (the user, 2026-10-05): the flare key drops a pair, one either side behind the jet, at most one pair every
// kFlareGapMs, while it has pairs left (missile.cpp FlareDrop: the missiles coming for it may take one); its burning
// flares drawn every frame (booster.cpp FlareFlames).
constexpr ULONGLONG kFlareGapMs=1000;
constexpr float kFlareBack=6.0f,kFlareSide=12.0f,kFlareDown=4.0f,kFlareKeep=0.6f;   // m behind; m/s out, down; share of its speed
constexpr int kFlaresDrawn=8;
constexpr float kFlareBreakLock=0.6f;   // each enemy jet locking on to it loses its lock with this, a drop
void Flares(PJet& j,unsigned char* v,const Stick& s,const float* pos) noexcept {
    const ULONGLONG ms=GameMs();
    const bool press=s.flare && !j.flareHeld;
    j.flareHeld=s.flare;
    if(press && j.phase==Phase::air && j.flares>0 && ms-j.flareAt>=kFlareGapMs) {
        j.flares--;j.flareAt=ms;
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        float right[3]={m[0],m[1],m[2]},nose[3]={m[8],m[9],m[10]};
        Normalize(right);Normalize(nose);
        for(int side=-1;side<=1;side+=2) {
            float at[3],vel[3];
            for(int i=0;i<3;++i) {
                at[i]=pos[i]-nose[i]*kFlareBack;
                vel[i]=j.vel[i]*kFlareKeep+right[i]*kFlareSide*static_cast<float>(side);
            }
            vel[1]-=kFlareDown;
            FlareDrop(v,at,vel,nose,side<0);
        }
        const int broke=jet::BreakLocks(v,kFlareBreakLock);
        Log("PJET v=%p flares (%d pairs left)%s",v,j.flares,broke ? ": an enemy lost its lock" : "");
    }
    float at[kFlaresDrawn][3],vel[kFlaresDrawn][3];
    const int n=FlaresOf(v,at,vel,kFlaresDrawn);
    FlareFlames(v,at,vel,n,ms);
}

// A missile's lock point this close to the jet is a missile coming for it (missile.cpp MissileHoming).
constexpr float kThreatRadius=20.0f;

void Stores(PJet& j,unsigned char* v,const Stick& s,const float* pos) noexcept {
    Store st[kMostStores];
    const int real=ReadStores(v,st,j.board ? kMostStores-SpecialRoom(j) : kMostStores);
    const int n=real+SpecialStore(j,v,st+real);   // the aircraft's own weapons as more stores (playerjet_board.inc)
    j.stores=n;j.bomb=j.hasImpact=false;j.lock=0;
    j.burden=BurdenOf(static_cast<float>(j.kind->mark),st,real);
    if(n==0)return;   // none known: the 506's own fire bytes stand
    if(j.store>=n || j.store<0)j.store=0;
    const bool press=s.switchStore && !j.switchHeld;
    j.switchHeld=s.switchStore;
    if(press || st[j.store].ammo<=0) {
        const int was=j.store;
        for(int k=1;k<=n;++k) {
            const int at=(j.store+k)%n;
            if(st[at].ammo>0 || k==n){j.store=at;break;}
        }
        if(j.store!=was){ClearWeaponLock(st[was].weapon);ClearWeaponLock(st[j.store].weapon);}   // no lock left on the store put away
        if(press)Log("PJET v=%p store: %s (%d left)",v,st[j.store].spec->name,st[j.store].ammo);
    }
    const bool next=s.nextTarget && !j.targetHeld;
    j.targetHeld=s.nextTarget;
    if(next && st[j.store].spec->role!=StoreRole::bomb){NextLockTarget(st[j.store].weapon);Log("PJET v=%p target: the next one",v);}
    j.lock=st[j.store].spec->role==StoreRole::bomb ? 0 : WeaponLock(st[j.store].weapon,j.lockAt,&j.lockProgress);
    audio::LockTone(j.lock,j.lockProgress);
    // Being locked on (the user, 2026-10-05: "being locked on should sound a warning too"): a missile homing on it
    // (its lock point within kThreatRadius), else an enemy jet's missile lock on it.
    // Heard through warn.cpp WarnTick (from the threats Threats gathers), with every aircraft the player flies.
    j.threat=MissileHoming(pos,kThreatRadius) ? 2 : jet::LockingOn(v) ? 1 : 0;
    Flares(j,v,s,pos);
    for(int i=0;i<n;++i){j.storeName[i]=st[i].spec->name;j.storeRounds[i]=st[i].ammo;j.storeRole[i]=static_cast<int>(st[i].spec->role);}
    // The impact point before the trigger: the bomb bay opens on it (FireSpecial kBay). Only what falls as a bomb (a
    // bomb store, the bay); the shells' and drones' cross is their aim point (SpecialFrame), no fall to trace.
    j.bomb=st[j.store].spec->role==StoreRole::bomb;
    if(j.bomb && j.phase==Phase::air && FallsAsBomb(st[j.store]))j.hasImpact=Impact(j,pos,st[j.store].weapon,j.impact);
    else if(st[j.store].spec->role==StoreRole::rocket && st[j.store].weapon) {
        RoundModel model{};float from[3],dir[3],seconds=0.0f;
        const unsigned char* const weapon=st[j.store].weapon;
        if(ReadRound(weapon,&model) && edf::MeanMuzzle(weapon,kMostBombMuzzles,from,dir) && Normalize(dir))
            j.hasImpact=RoundLands(weapon,model,from,dir,3000.0f,j.impact,&seconds);
    }
    const bool fire=v[kFireStore]!=0;
    v[kFireStore]=0;
    if(fire && st[j.store].weapon)TriggerStore(st[j.store]);
    else if(fire)FireSpecial(j,v,st[j.store],pos);
}

// The gun sight (the user, 2026-10-05: "the stock gun's two red lines: delete them, make our own"; crew.cpp AimLines
// hides them, ini PlayerJetGunSight). The guns (seat 0's holders 0 and 1, the two the primary fires) fire along the
// body's nose; a round flies at its weapon's AmmoSpeed (+0x894, m a frame) for AmmoAlive frames (+0x898), falling at its
// gravity factor (+0x8E0) times kRoundGravity (the jet guns' is 0: a straight line), and it does not take the
// aircraft's velocity (their AmmoOwnerMove is the stock gatling's 0, pylib/vcobjects.py jet_guns; docs/stores-re.md:
// a round's velocity is its direction x AmmoSpeed + the shooter's x AmmoOwnerMove). So a round fired now is at
// sight::RoundAt(pos, nose, speed, drop, t): the pipper is that point at the time of flight to the picked store's
// target, led (sight::Intercept with the target's velocity: put the pipper on the lead mark and the rounds meet it), or
// at kPipperRange (or the rounds' reach, the nearer) with none. The target's velocity is measured off its lock point
// frame to frame, smoothed over kTargetTau; a jump faster than kTargetMost is another target (no velocity yet).
constexpr float kRoundGravity=14.7f;   // m/s^2: the world's (heli.cpp kGravity, measured)
constexpr float kPipperRange=600.0f,kTargetTau=0.25f,kTargetMost=600.0f,kMovingSpeed=5.0f;
constexpr std::uint64_t kGunHolders=2;

// The guns' round: speed (m/s), fall (m/s^2), life (s); false with no gun (a missile in holders 0 and 1, or none).
bool GunRound(unsigned char* v,float* speed,float* drop,float* life) noexcept {
    if(SeatCount(v)==0)return false;
    const auto seat=SeatAt(v,0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(count>8 || !Readable(holders,count*8))return false;
    *speed=0.0f;
    for(std::uint64_t i=0;i<count && i<kGunHolders;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,kWeaponAmmo+4) || At<std::int32_t>(w,kWeaponLockon)==kHoming)continue;
        const float s=At<float>(w,kWeaponSpeed)*60.0f,g=At<float>(w,kWeaponGravity);
        const std::int32_t alive=At<std::int32_t>(w,kWeaponAlive);
        if(!std::isfinite(s) || s<=*speed || alive<=0)continue;
        *speed=s;*drop=std::isfinite(g) && g>0.0f ? g*kRoundGravity : 0.0f;*life=static_cast<float>(alive)/60.0f;
    }
    return *speed>1.0f;
}

// The picked store's target's velocity (see kTargetTau), off its lock point (Stores: j.lock, j.lockAt).
void TrackTarget(PJet& j,float dt) noexcept {
    if(!j.lock || !(dt>0.0f)){j.targetSeen=false;return;}
    float v[3];
    for(int i=0;i<3;++i)v[i]=(j.lockAt[i]-j.targetWas[i])/dt;
    if(!j.targetSeen || !(Len(v)<=kTargetMost))std::memset(j.targetVel,0,12);
    else {
        const float k=1.0f-std::exp(-dt/kTargetTau);
        for(int i=0;i<3;++i)j.targetVel[i]+=(v[i]-j.targetVel[i])*k;
    }
    std::memcpy(j.targetWas,j.lockAt,12);j.targetSeen=true;
}

// The sight's symbols (PlayerJetSymbols) for this frame.
void Sight(PJet& j,unsigned char* v,const float* pos,float dt) noexcept {
    PlayerJetSymbols& y=j.sym;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    std::memcpy(y.pos,pos,12);
    y.nose[0]=m[8];y.nose[1]=m[9];y.nose[2]=m[10];
    if(!Normalize(y.nose)){y.nose[0]=0.0f;y.nose[1]=0.0f;y.nose[2]=1.0f;}
    std::memcpy(y.dir,j.vel,12);
    y.moving=Len(j.vel)>kMovingSpeed && Normalize(y.dir);
    TrackTarget(j,dt);
    float speed=0.0f,drop=0.0f,life=0.0f;
    y.gun=GunRound(v,&speed,&drop,&life);
    y.lead=y.leadInRange=false;
    if(!y.gun)return;
    y.gunRange=speed*life;
    float t=(kPipperRange<y.gunRange ? kPipperRange : y.gunRange)/speed;
    if(j.lock) {
        const float d[3]={j.lockAt[0]-pos[0],j.lockAt[1]-pos[1],j.lockAt[2]-pos[2]};
        const float hit=sight::Intercept(d,j.targetVel,speed);
        if(hit>0.0f) {
            t=hit;y.lead=true;y.leadInRange=hit<=life;y.leadRange=speed*hit;
            for(int i=0;i<3;++i)y.leadAt[i]=j.lockAt[i]+j.targetVel[i]*hit;
        }
    }
    sight::RoundAt(pos,y.nose,speed,drop,t,y.pipper);
}

// What threatens it, for the HUD's threat ring (the user, 2026-10-05: "locked on, it should show the direction"): the
// missiles coming for it (MissileHoming's, as the MISSILE! warning), then the enemy jets locking on to it (LockingOn's).
void Threats(PJet& j,const unsigned char* v,const float* pos) noexcept {
    PlayerJetSymbols& y=j.sym;
    int n=MissilesHomingAt(pos,kThreatRadius,y.threatAt,kMostThreats);
    if(n>kMostThreats)n=kMostThreats;
    for(int i=0;i<n;++i)y.threatKind[i]=2;
    const int locks=jet::LockersOf(v,y.threatAt+n,kMostThreats-n);
    for(int i=n;i<n+locks;++i)y.threatKind[i]=1;
    y.threats=n+locks;
}

// The ground-proximity warning (the user, 2026-10-05: "warn me to pull up when I'm about to hit the ground"; 2026-10-06:
// "more realistic"): warn.cpp ClosureIn looking kTerrainSeconds ahead, a real GPWS's modes (GpwsOf): PULL UP within
// kPullUpSeconds of the ground or what stands on it, TERRAIN / SINK RATE as a caution before. A landing (sinking no
// faster than the airframe lands: kLandSink for a wing, the kind's landMax for a rotor craft) raises nothing. In the air
// only; last frame's velocity and climb (one frame late, as the HUD shows them).
void Proximity(PJet& j,const float* pos,float clear) noexcept {
    j.gpws=Gpws::none;j.impactIn=-1.0f;
    if(j.phase!=Phase::air)return;
    const bool rotor=j.board && j.board->frame==pjet::Airframe::rotor;
    bool rising=false;
    j.impactIn=ClosureIn(pos,j.vel,j.climb,clear,rotor ? j.kind->landMax : kLandSink,kTerrainSeconds,&rising);
    j.gpws=GpwsOf(j.impactIn,rising);
}

// The caught jet's speed for its boarding (see kCatchAfterMs).
// The catch's jet flying in (see kCatchFrom): the jet, where it makes for (under the parachuting player, led by their
// drift), its speed there.
// `drift`: the player's velocity (the formation's); `heading`: the way it flew in, level (its nose in the formation).
struct CatchFlight { const void* v; float target[3],speed; float drift[3],heading[3]; bool hasDoor=false; } catchFlight{};
// The catch's last stretch (the user, 2026-10-05: "the catch jet twitches under my feet": it made straight for its
// point at full speed and overshot it every frame): within kCatchHoming it flies in formation, the player's drift
// plus a correction at kCatchGain of the gap, no more than a stop at kCatchBrake would allow, its nose level along
// its heading.
constexpr float kCatchGain=1.5f,kCatchBrake=40.0f;
// The board press is retried; a refusal says which of the stock button's gates holds (docs/rescue-re.md: +0x128
// bit 0, +0x5D0 bit 2, +0x39C) every kCatchSayMs.
constexpr ULONGLONG kCatchSayMs=2000;
ULONGLONG catchSaidAt=0,catchReachAt=0;

void Board(PJet& j,unsigned char* v,const float* pos,float clear) noexcept {
    j.driven=true;j.blockedSince=0;
    if(!j.insetSaved){j.savedInset=At<float>(v,kAreaInset);j.insetSaved=true;}
    const float speed=Len(j.measured);
    const bool air=clear==kNoGround || clear>kOffGround;
    j.phase=air ? Phase::air : speed>kParkSpeed ? Phase::rolling : Phase::parked;
    j.hasUp=false;j.hasAim=false;
    std::memcpy(j.vel,j.measured,12);
    j.throttle=air ? 0.5f : 0.0f;
    if(j.autopilot) {   // the catch's jet: the player takes it over in flight, as fast as it flew in
        for(int i=0;i<3;++i)j.vel[i]=catchFlight.heading[i]*catchFlight.speed;   // (its formation speed would stall)
        j.phase=Phase::air;j.throttle=1.0f;j.autopilot=false;
        catchFlight=CatchFlight{};
    }
    j.autopilot=false;
    Boarded(j,v,pos,clear);   // any of the plugin's other aircraft: its own (playerjet_board.inc)
    Log("PJET v=%p boarded: %s, hp %.0f/%.0f, %s at (%.0f,%.0f,%.0f), %.0f m over the ground, %.0f m/s",v,j.kind->name,
        At<float>(v,kHp),At<float>(v,kHpMax),kPhaseNames[static_cast<int>(j.phase)],pos[0],pos[1],pos[2],clear,speed);
}

// Getting out in the air (the user, 2026-10-05): the player is thrown up as by an ejection seat and comes down under a
// parachute (docs/player-jet-re.md §7). The soldier's own walk controller (human +0x680) carries its velocity in m/s
// (+0x6B0/+0x6B4/+0x6B8; in the air only y changes, by gravity each frame); the stock exit zeroes it as its ride
// state ends (0x57B11A), which also clears the riding bit (+0x380 0x80). Once that bit is clear the launch is the
// game's own jump request (+0x1294 speed, +0x1290 flag: consumed at 0x575A7A, sent to the other players), with
// kEjectCarry of the jet's horizontal velocity. Past the top the fall is held at kChuteSink m/s and the horizontal
// speed bleeds off (kChuteBleed a second, the walk's carried push +0x1210 and the damage shove +0x11F0 too) until
// the soldier stands (support +0x711 = 2), dies, is thrown as a ragdoll, or flies by itself (a Wing Diver's or
// Fencer's boost: its vertical speed rising).
constexpr std::size_t kHumanVel=0x6B0,kHumanSupport=0x711,kHumanFlags=0x380,kHumanAttach=0x39C;
constexpr std::size_t kJumpFlag=0x1290,kJumpSpeed=0x1294,kWalkPush=0x1210,kDamageShove=0x11F0;
constexpr std::uint32_t kRiding=0x80;
constexpr float kEjectUp=25.0f,kEjectCarry=0.3f,kEjectFrom=15.0f;   // m/s up; share of the jet's; m over the ground
constexpr float kChuteSink=6.0f,kChuteBleed=0.6f,kChuteBoost=3.0f;   // m/s down at most; a second; m/s up in a frame
constexpr ULONGLONG kEjectWaitMs=2000,kChuteMostMs=180000;
// The parachute ends on any ground contact (support 2 standing, 1 sliding down a slope: it hung on there) or within
// kChuteLand m of the ground, and when the player cuts it (ini PlayerJetChuteCutKey) after kChuteCutAfterMs (the
// user, 2026-10-05: it should go on landing, and I should be able to cut it in the air).
constexpr float kChuteLand=1.5f;
constexpr ULONGLONG kChuteCutAfterMs=500;
enum class Eject { none, pending, chute };
// The catch (Cfg().playerJetCatch; the user: "don't wait till they land, catch them in the air"; 2026-10-05: "it
// should fly in from outside"): kCatchAfterMs into the parachute, with the player kCatchClear over the ground, a jet
// of the kind they left (its SGO, preloaded at the mission's start: PreloadPlayerJets), empty and on nobody's team at
// the mission's level (LevelVehicle), is made kCatchFrom back along the old heading and flown in by the plugin
// (AutoFly: the player jet's own flight model, steered by the mouse aim's law at a point kCatchBelow under the
// player led by their drift, at least kCatchFloor over the ground); its last kCatchHoming m it makes straight for
// that point. Within kCatchReach of the player the board button is pressed for them; boarded, it is theirs at the
// speed it flew in. kCatchMostMs without them aboard, it is given up (it flies on, empty, and comes down).
constexpr ULONGLONG kCatchAfterMs=4000,kCatchMostMs=45000;
constexpr float kCatchClear=40.0f,kCatchBelow=2.0f,kCatchOver=40.0f,kCatchFrom=1500.0f,kCatchFloor=30.0f;
constexpr float kCatchHoming=250.0f;
struct Bailout {
    Eject state; ULONGLONG at; float carry[2],vy;
    float heading[3],speed;          // the jet left: its nose, its speed (the catch)
    // The jet the catch makes (pjet::kCatchFiles, kCatchNone: none), why that one, and what was left (the log). Made
    // when nothing is coming for them (caught empty): the jet left comes back itself (playerjet_board.inc Left) only
    // when it can, and if it is lost on the way, this one is made after all (`self`).
    int catchWith=pjet::kCatchNone; const char* catchWhy; const char* left;
    bool self;                       // `caught` is the jet they left, flying back for them
    ObjRef caught; ULONGLONG caughtAt;
    bool open;                       // past the top: the parachute is open (the canopy shows: Chute)
    const char* why;                 // how it ended (state none), for the canopy's CHUTE line
    bool cutUp;                      // the cut key seen up since the ejection (see EjectTick)
} bail{};

// The ejection over (state none), `why` kept for the canopy's line (ChuteTick): its one exit. Caught (the player in
// the catch's jet): Board takes the catch over (its speed, its pilot). Any other end (landed, dead, attached, cut,
// never out, too long) lets the catch go here: its jet, no one aboard, ends its autopilot in Fly (a player jet flies
// on, empty, and comes down; one of the plugin's other aircraft goes back to its NPC pilot). It used to keep circling
// the spot to the mission's end.
void BailEnd(const char* why,bool caught=false) noexcept {
    bail.state=Eject::none;bail.why=why;
    if(!caught){catchFlight=CatchFlight{};bail.caught=ObjRef{};bail.self=false;}
}
// The catch's SGOs (playerjet_kinds.h kCatchFiles), each preloaded for this mission (PreloadPlayerJets).
bool playerJetPreloaded[pjet::kCatchFileCount]{};
static_assert(pjet::kCatchFiles[pjet::kCatchPlayerFighter].mark==7201.0f && pjet::kCatchFiles[pjet::kCatchPlayerStrike].mark==7202.0f,
              "the player jets' catch SGOs carry kKinds' marks");

// The catch for the jet `j` left in the air: a player jet its own SGO (by its mark), one of the plugin's other aircraft
// its row's (playerjet_kinds.h catchWith); kCatchNone when there is none.
int CatchOf(const PJet& j,const char** why) noexcept {
    *why="its own SGO";
    if(j.board){*why=j.board->catchWhy;return j.board->catchWith;}
    if(!j.kind)return pjet::kCatchNone;
    for(int i=0;i<pjet::kCatchFileCount;++i)
        if(pjet::kCatchFiles[i].player && pjet::kCatchFiles[i].mark==static_cast<float>(j.kind->mark))return i;
    return pjet::kCatchNone;
}
constexpr unsigned kPreloadFn=0x7A3780,kCreateObjectFn=0x11945E0,kInitParamVt=0x1762068;
constexpr std::size_t kPreloadMgrAt=0x20B29A8,kObjectMgrAt=0x20B2958;
struct alignas(16) SpawnParam { const void* vtable; unsigned char rest[0x28]; };

// The SGO's mission_setup applied with no AI aboard (the catch jet stays empty for the player): the first half of
// RideAi(true) (0x633030): 0x62D6E0(vehicle, &setup) reads it, the vehicle's slot 46 applies it (the jet mark, its
// weapons, the heli parameters), the setup's variant destroyed through its type's entry (*(image+0x1765220)[type]).
// Without it the catch jet had mark 0: no kind, no autopilot, it fell like a stone (2026-10-05 13:34 / 13:52, "PJET
// catch jet ... frame: no kind (mark 0, body 0)").
constexpr unsigned kReadSetup=0x62D6E0,kSetupDtors=0x1765220;
constexpr std::size_t kSlotApplySetup=46,kSetupType=0x10;
const unsigned char kReadSetupSig[]={0x48,0x89,0x5C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x40,0x48,0x8B,0xDA};
void MissionSetup(unsigned char* v) noexcept {
    __try {
        if(!Matches(kReadSetup,kReadSetupSig,sizeof(kReadSetupSig))){Log("PJET catch: mission setup profile mismatch");return;}
        alignas(16) unsigned char setup[0x40]{};
        alignas(16) unsigned char scratch[0x40]{};
        reinterpret_cast<void(__fastcall*)(void*,void*)>(image+kReadSetup)(v,setup);
        reinterpret_cast<void(__fastcall* const*)(void*,void*)>(At<void* const*>(v,0))[kSlotApplySetup](v,setup);
        const std::uint16_t type=At<std::uint16_t>(setup,kSetupType);
        if(type!=0xFFFF)reinterpret_cast<void(__fastcall* const*)(void*,void*)>(image+kSetupDtors)[type](setup,scratch);
        Log("PJET catch: mission setup applied (mark %.0f)",BodyMark(v));
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("PJET catch: the game faulted applying the mission setup");}
}

// The catch's jet `which` (pjet::kCatchFiles) at `m`, empty, on nobody's team; nullptr (said why) when it cannot be made.
unsigned char* SpawnCatchJet(int which,const float* m) noexcept {
    if(which<0 || which>=pjet::kCatchFileCount){Log("PJET catch: no catch jet for what they left");return nullptr;}
    const pjet::CatchFile& f=pjet::kCatchFiles[which];
    if(!playerJetPreloaded[which] || !jet::SpawnReady() || !At<void*>(image,kObjectMgrAt)) {
        Log("PJET catch: %ls (%s) not preloaded this mission%s",f.file,f.name,
            f.player || (Cfg().playerJetAll && Cfg().playerJetCatch) ? " (not installed?)" : " (PlayerJetAll / PlayerJetCatch were off at its start)");
        return nullptr;
    }
    SpawnParam param{image+kInitParamVt,{}};
    unsigned char* v=nullptr;
    __try {
        v=reinterpret_cast<unsigned char*(*)(void*,const float*,const wchar_t*,SpawnParam*)>(image+kCreateObjectFn)(
            At<void*>(image,kObjectMgrAt),m,f.sgo,&param);
    } __except(EXCEPTION_EXECUTE_HANDLER){playerJetPreloaded[which]=false;Log("PJET catch: the game faulted building %ls: off",f.file);return nullptr;}
    if(!v)return nullptr;
    FixBodyPart506(v,"PJET");
    MissionSetup(v);
    SetObjectTeam(v,kTeamVehicle);
    LevelVehicle(v);
    return v;
}

void Catch(unsigned char* h,ULONGLONG ms) noexcept {
    const float* p=reinterpret_cast<const float*>(h+kPosition);
    const float* hv=reinterpret_cast<const float*>(h+kHumanVel);
    if(!bail.caught) {
        if(!Cfg().playerJetCatch || ms-bail.at<kCatchAfterMs || bail.catchWith==pjet::kCatchNone)return;
        const float clear=GroundClearance(p);
        if(clear!=kNoGround && clear<kCatchClear) {
            bail.catchWith=pjet::kCatchNone;
            Log("PJET catch: too low (%.0f m), the parachute goes on",clear);
            return;
        }
        float f[3]={bail.heading[0],0.0f,bail.heading[2]};
        if(!Normalize(f)){f[0]=0.0f;f[2]=1.0f;}
        float at[3]={p[0]-f[0]*kCatchFrom,p[1],p[2]-f[2]*kCatchFrom};
        const float under=GroundClearance(at);
        if(under!=kNoGround && under<kCatchFloor*2.0f)at[1]+=kCatchFloor*2.0f-under;
        alignas(16) const float m[16]={f[2],0,-f[0],0, 0,1,0,0, f[0],0,f[2],0, at[0],at[1],at[2],1};
        const int which=bail.catchWith;
        bail.catchWith=pjet::kCatchNone;   // one try: a jet made, or none could be (said why)
        unsigned char* const v=SpawnCatchJet(which,m);
        if(!v)return;
        Log("PJET catch: making %ls (%s) for the %s they left: %s",pjet::kCatchFiles[which].file,pjet::kCatchFiles[which].name,
            bail.left ? bail.left : "jet",bail.catchWhy ? bail.catchWhy : "");
        const Kind* const k=KindOf(v);
        const float speed=std::fmax(bail.speed,(k ? k->rotate : 75.0f)+kCatchOver);
        catchFlight=CatchFlight{v,{p[0],p[1]-kCatchBelow,p[2]},speed,{hv[0],hv[1],hv[2]},{f[0],0.0f,f[2]}};
        catchReachAt=0;
        bail.caught=ObjRef::Of(v);bail.caughtAt=ms;
        Log("PJET catch: v=%p made %.0f m out at (%.0f,%.0f,%.0f), flying in at %.0f m/s to the player at (%.0f,%.0f,%.0f)",v,kCatchFrom,
            at[0],at[1],at[2],speed,p[0],p[1],p[2]);
        return;
    }
    unsigned char* const v=const_cast<unsigned char*>(static_cast<const unsigned char*>(bail.caught.obj));
    if(!bail.caught.Is(v) || v[kDead]) {
        if(bail.self && bail.catchWith!=pjet::kCatchNone) {   // the jet they left, lost on its way back: one is made (above)
            Log("PJET catch: the %s coming back for them is gone: another jet is made",bail.left ? bail.left : "jet");
            Forget(v);   // its wreck the game's: not held for the player (jet.cpp JetFrame takes it again)
            bail.caught=ObjRef{};bail.self=false;catchFlight=CatchFlight{};
            return;
        }
        Log("PJET catch: the jet is gone");Forget(v);BailEnd("the catch jet is gone");return;
    }
    if(ms-bail.caughtAt>kCatchMostMs) {
        Log("PJET catch: given up, the player not aboard in %.0f s",static_cast<float>(kCatchMostMs)*0.001f);
        bail.caught=ObjRef{};bail.catchWith=pjet::kCatchNone;bail.self=false;catchFlight=CatchFlight{};
        return;
    }
    // Only the native boarding point is a valid rendezvous. Relative motion predicts its next physics step, while
    // the player's velocity (minus the door's rotation) is supplied separately as formation feed-forward.
    const float* vp=reinterpret_cast<const float*>(v+kPosition);
    float seatAt[3],reach=0.0f;
    catchFlight.hasDoor=SeatPoint(v,0,seatAt,&reach) && reach>0.0f;
    if(!catchFlight.hasDoor) {
        // No door to read (its seats not set up yet, or unreadable): no boarding and no formation on a guess, but the
        // jet still flies (AutoFly's Air) for the player as it was made to: its body kCatchBelow under them.
        for(int i=0;i<3;++i){catchFlight.target[i]=p[i];catchFlight.drift[i]=hv[i];}
        catchFlight.target[1]-=kCatchBelow;
        return;
    }
    const PJet* j=Find(v);
    const float zero[3]={0.0f,0.0f,0.0f};
    pjet::CatchDoor(p,hv,vp,seatAt,j ? j->vel : zero,j ? j->omega : zero,GameStep(0),catchFlight.target,catchFlight.drift);
    const float d[3]={p[0]-seatAt[0],p[1]-seatAt[1],p[2]-seatAt[2]};
    const float gap=Len(d);
    if(gap>=reach) {
        if(ms-catchSaidAt>kCatchSayMs) {
            catchSaidAt=ms;
            Log("PJET catch: native door gap %.2f m (reach %.2f), player y %.2f door y %.2f body y %.2f",gap,reach,p[1],seatAt[1],vp[1]);
        }
        return;
    }
    if(!catchReachAt){catchReachAt=ms;Log("PJET catch: the seat in reach (%.1f m, reach %.1f m): boarding",gap,reach);}
    PressBoardButton(h);
    if(ms-catchReachAt>1000 && ms-catchSaidAt>kCatchSayMs) {
        catchSaidAt=ms;
        Log("PJET catch: not aboard yet, %.1f m from the seat; gates +0x128=%02x +0x5D0=%08x +0x39C=%d",gap,
            At<unsigned char>(h,0x128),At<std::uint32_t>(h,0x5D0),At<std::int32_t>(h,0x39C));
    }
}

// `alive`: the jet still there to read (a shot-down one may be deleted already: its kind and its path from the PJet).
void EjectStart(const PJet& j,const unsigned char* v,bool alive) noexcept {
    bail=Bailout{};
    bail.state=Eject::pending;bail.at=GameMs();bail.carry[0]=j.vel[0]*kEjectCarry;bail.carry[1]=j.vel[2]*kEjectCarry;
    bail.catchWith=CatchOf(j,&bail.catchWhy);
    bail.left=j.kind ? j.kind->name : nullptr;
    float nose[3]={j.vel[0],j.vel[1],j.vel[2]};
    if(alive){const float* m=reinterpret_cast<const float*>(v+kMatrix);nose[0]=m[8];nose[1]=m[9];nose[2]=m[10];}
    else if(!Normalize(nose)){nose[0]=0.0f;nose[1]=0.0f;nose[2]=1.0f;}
    std::memcpy(bail.heading,nose,12);
    bail.speed=Len(j.vel);
}
void EjectTick() noexcept {
    if(bail.state==Eject::none)return;
    unsigned char* const h=PlayerHuman();
    const ULONGLONG ms=GameMs();
    if(!h || h[kDead] || At<std::int32_t>(h,kHumanAttach)!=0) {
        // It used to end here without a word: the parachute and the catch stopped unseen (2026-10-05: "no plane
        // came for me", no line after the catch's).
        Log("PJET ejection over: %s (state %d)",!h ? "no player found" : h[kDead] ? "the player died" : "the player attached/ragdolled",
            static_cast<int>(bail.state));
        BailEnd(!h ? "no player found" : h[kDead] ? "the player died" : "the player attached/ragdolled");return;
    }
    float* const vel=reinterpret_cast<float*>(h+kHumanVel);
    if(bail.state==Eject::pending) {
        if(At<std::uint32_t>(h,kHumanFlags)&kRiding) {   // the stock exit not through yet
            if(ms-bail.at>kEjectWaitMs){BailEnd("the player never left the seat");Log("PJET ejection over: the player never left the seat");}
            return;
        }
        Put<float>(h,kJumpSpeed,kEjectUp);h[kJumpFlag]=1;
        vel[0]=bail.carry[0];vel[2]=bail.carry[1];
        bail.state=Eject::chute;bail.at=ms;bail.vy=kEjectUp;
        Log("PJET ejected: %.0f m/s up, %.0f m/s carried; the parachute opens past the top",kEjectUp,
            std::sqrt(bail.carry[0]*bail.carry[0]+bail.carry[1]*bail.carry[1]));
        return;
    }
    if(At<std::uint32_t>(h,kHumanFlags)&kRiding) {   // caught: in the new jet
        Log("PJET catch: the player is in");
        BailEnd("caught: the player is in the catch jet",true);
        return;
    }
    const unsigned char support=h[kHumanSupport];
    const float clear=GroundClearance(reinterpret_cast<const float*>(h+kPosition));
    const bool landed=ms-bail.at>300 && (support!=0 || (clear!=kNoGround && clear<kChuteLand));
    // A press made after the ejection cuts it, not one held through it: the cut key is the flare key (X both), and X
    // held for flares through a bail-out under a missile cut the parachute 0.5 s in, the catch with it (the log of
    // 2026-10-05 22:35 has a cut 2.4 s into an ejection, before any catch: a held key is the likely one).
    const bool cutKey=KeyDown(Cfg().playerJetChuteCutKey);
    if(!cutKey)bail.cutUp=true;
    const bool cut=ms-bail.at>kChuteCutAfterMs && bail.cutUp && cutKey;
    if(ms-bail.at>kChuteMostMs || landed || cut || vel[1]>bail.vy+kChuteBoost) {
        const char* const why=landed ? "landed" : cut ? "cut by the player" : vel[1]>bail.vy+kChuteBoost ? "flying by itself" : "too long";
        Log("PJET parachute: %s",why);
        BailEnd(why);
        return;
    }
    if(!bail.open && vel[1]<=0.0f) {   // past the top: the canopy shows (ChuteTick)
        bail.open=true;
        Log("PJET parachute open, %.0f m/s carried",std::sqrt(vel[0]*vel[0]+vel[2]*vel[2]));
    }
    const float keep=1.0f-kChuteBleed/60.0f;
    if(vel[1]<-kChuteSink)vel[1]=-kChuteSink;
    if(vel[1]<0.0f) {
        vel[0]*=keep;vel[2]*=keep;
        float* const push=reinterpret_cast<float*>(h+kWalkPush);
        float* const shove=reinterpret_cast<float*>(h+kDamageShove);
        push[0]*=keep;push[2]*=keep;shove[0]*=keep;shove[2]*=keep;
    }
    bail.vy=vel[1];
    Catch(h,ms);   // guide with the parachute's clamped velocity actually sent to the next physics step
}

// The parachute's canopy (the user, 2026-10-05: the parachute "only slows the fall, no model"): from the parachute's
// opening (past the top, bail.open) to its end, one object over the player, CHUTE lines when it is made and gone (and
// why). The object is a FarEventObject (docs/player-jet-re.md §8; tools/make_chute.py EDF6VC_CHUTE.SGO, the stock
// far-off plant's SGO with pylib/chute_model.py's dome and lines as its model, preloaded at the mission's start when the
// file is there: PreloadPlayerJets): CreateObject with the plain InitParamBase@SceneObject as the catch jet's, its class
// checked by its vtable. Its update (vtable slot 5, 0x5C4FB0) only renders its model at its matrix (+0x60..+0x9F,
// times its setting.scale) and ticks an optional animation; it never writes that matrix, so the plugin's write each
// frame moves it (H: the whole update read). Its physics body comes only from a "ragdoll" entry (0x5C4970, H), which
// the SGO has none of: nothing collides with it, pushes the player or stops a bullet (M: the GameObjectBase base adds
// no collider of its own that the stock far-off objects show). It goes on the neutral team (SetObjectTeam: hostile to
// nobody, so no enemy turns on it; M) and is deleted when the parachute ends, or forgotten at a mission's start (the
// mission's objects go with it). Upright, kChuteUp over the player's feet (the model's origin: pylib/chute_model.py
// CANOPY_UP), its front along the drift (the last one held while the drift is under kChuteTurnSpeed).
constexpr unsigned kFarEventVtable=0x17D5DA0,kFarEventUpdate=0x5C4FB0,kDeleteFn=0x118A1B0;
constexpr std::size_t kVtableUpdate=5;
constexpr float kChuteUp=4.0f,kChuteTurnSpeed=0.5f;
const unsigned char kFarEventUpdateSig[]={0x40,0x53,0x48,0x83,0xEC,0x70,0x48,0x8B,0xD9,0xE8};
const wchar_t* const kChuteSgo=L"app:/object/edf6vc_chute.sgo";
const wchar_t* const kChuteFile=L"EDF6VC_CHUTE.SGO";
struct Chute {
    unsigned char* obj; const void* ctrl;
    bool tried;                      // made (or failed to be) for this ejection: not tried again every frame
    float face[2];                   // its front (x, z)
} chute{};
alignas(16) float chuteMatrix[16];   // where it is put (ChutePose)
bool chuteOk=false,chutePreloaded=false;

bool ChuteLive() noexcept {
    unsigned char* const o=chute.obj;
    return o && Readable(o,kTeam+4) && At<const void*>(o,0)==image+kFarEventVtable && At<const void*>(o,kSelfCtrl)==chute.ctrl &&
           !(o[kObjFlags]&kObjDeleted);
}

// Its matrix: upright, kChuteUp over the feet at `p`, its front along the drift `v` (held when slow).
void ChutePose(const float* p,const float* v) noexcept {
    float f[3]={v[0],0.0f,v[2]};
    if(Len(f)>=kChuteTurnSpeed && Normalize(f)){chute.face[0]=f[0];chute.face[1]=f[2];}
    const float x=chute.face[0],z=chute.face[1];
    const float pose[16]={z,0,-x,0, 0,1,0,0, x,0,z,0, p[0],p[1]+kChuteUp,p[2],1};
    std::memcpy(chuteMatrix,pose,sizeof(pose));
}

void ChuteMake(const unsigned char* h) noexcept {
    chute.tried=true;
    if(!chuteOk || !chutePreloaded || !jet::SpawnReady() || !At<void*>(image,kObjectMgrAt)) {
        Log("CHUTE not made: %s",!chuteOk ? "FarEventObject profile mismatch" : !chutePreloaded ? "EDF6VC_CHUTE.SGO not preloaded (not installed?)" :
            "no object manager");
        return;
    }
    float f[3]={bail.heading[0],0.0f,bail.heading[2]};
    if(!Normalize(f)){f[0]=0.0f;f[2]=1.0f;}
    chute.face[0]=f[0];chute.face[1]=f[2];
    const float* p=reinterpret_cast<const float*>(h+kPosition);
    ChutePose(p,reinterpret_cast<const float*>(h+kHumanVel));
    SpawnParam param{image+kInitParamVt,{}};
    unsigned char* o=nullptr;
    __try {
        o=reinterpret_cast<unsigned char*(*)(void*,const float*,const wchar_t*,SpawnParam*)>(image+kCreateObjectFn)(
            At<void*>(image,kObjectMgrAt),chuteMatrix,kChuteSgo,&param);
    } __except(EXCEPTION_EXECUTE_HANDLER){chutePreloaded=false;Log("CHUTE not made: the game faulted building it: off this mission");return;}
    if(!o){Log("CHUTE not made: the game made no object");return;}
    if(At<const void*>(o,0)!=image+kFarEventVtable) {
        Log("CHUTE %p is no FarEventObject (vtable %p): deleted",o,At<const void*>(o,0));
        reinterpret_cast<void(*)(void*)>(image+kDeleteFn)(o);
        return;
    }
    const std::int32_t team=At<std::int32_t>(o,kTeam);
    SetObjectTeam(o,kTeamNeutral);
    chute.obj=o;chute.ctrl=At<const void*>(o,kSelfCtrl);
    Log("CHUTE made: %p over the player at (%.0f,%.0f,%.0f), team %d -> %d",o,p[0],p[1],p[2],team,At<std::int32_t>(o,kTeam));
}

void ChuteFree(const char* why) noexcept {
    unsigned char* const o=chute.obj;
    __try {
        if(ChuteLive())reinterpret_cast<void(*)(void*)>(image+kDeleteFn)(o);
        else Log("CHUTE %p: already gone",o);
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("CHUTE %p: the game faulted deleting it",o);}
    chute.obj=nullptr;chute.ctrl=nullptr;
    Log("CHUTE gone: %s",why ? why : "the ejection ended");
}

// A frame (after EjectTick): the canopy made at the parachute's opening, kept over the player while it is open,
// deleted once it is not.
void ChuteTick() noexcept {
    const bool open=bail.state==Eject::chute && bail.open;
    if(!open) {
        if(chute.obj)ChuteFree(bail.state==Eject::none ? bail.why : "a new ejection");
        if(bail.state!=Eject::chute)chute.tried=false;
        return;
    }
    const unsigned char* const h=PlayerHuman();
    if(!h)return;   // EjectTick ends it next frame
    if(!chute.obj) {
        if(!chute.tried)ChuteMake(h);
        return;
    }
    __try {
        if(!ChuteLive()){chute.obj=nullptr;chute.ctrl=nullptr;Log("CHUTE gone: deleted by the game");return;}
        ChutePose(reinterpret_cast<const float*>(h+kPosition),reinterpret_cast<const float*>(h+kHumanVel));
        std::memcpy(chute.obj+kMatrix,chuteMatrix,sizeof(chuteMatrix));
    } __except(EXCEPTION_EXECUTE_HANDLER){chute.obj=nullptr;chute.ctrl=nullptr;Log("CHUTE gone: the game faulted moving it");}
}

// A mission's start: the last mission's canopy went with its objects.
void ChuteForget() noexcept {
    if(chute.obj)Log("CHUTE gone: a new mission (forgotten with the last one's objects)");
    chute=Chute{};
}

// The player out of the jet: got out, or the jet destroyed under them (the user, 2026-10-05: "a shot-down one should
// eject too, high enough"). In the air over kEjectFrom (`clear`: now, or the last frame's for a jet gone) they are
// thrown up and come down under the parachute (EjectTick). `alive`: the jet still there to write back to.
void Leave(PJet& j,unsigned char* v,float clear,bool alive,const char* how) noexcept {
    const bool eject=j.phase==Phase::air && (clear==kNoGround || clear>kEjectFrom);
    if(eject)EjectStart(j,v,alive);
    j.driven=false;j.active=false;j.turnIn=j.pitchIn=j.yawIn=j.rollIn=0.0f;j.hasUp=false;j.hasAim=false;
    if(j.insetSaved && alive)Put<float>(v,kAreaInset,j.savedInset);
    j.insetSaved=false;
    Log("PJET v=%p left: %s (%s, %.0f m/s, %.0f m over the ground)%s",v,how,kPhaseNames[static_cast<int>(j.phase)],Len(j.vel),
        clear==kNoGround ? -1.0f : clear,eject ? ": ejected" : "");
    Left(j,v,clear,alive,eject);   // any of the plugin's other aircraft: parked, caught or handed back (playerjet_board.inc)
}

// Whether a local player sits in a seat of `v` other than `seat` (seatswitch.cpp moves them between the gunship's seats).
bool AboardElsewhere(unsigned char* v,unsigned seat) noexcept {
    const unsigned count=SeatCount(v);
    for(unsigned i=0;i<count;++i)if(i!=seat && SeatRider(SeatAt(v,i))==Rider::player)return true;
    return false;
}

// The player moved from the stick to another seat of the aircraft (seatswitch.cpp: the gunship's gun): not out of it, so
// no ejection and no catch; the flight given up as Leave gives it up, its NPC pilot flying on (HandBack: one seated by
// the stock RideAi when the gunner's EnsurePilot has not already).
void Moved(PJet& j,unsigned char* v) noexcept {
    j.driven=false;j.active=false;j.turnIn=j.pitchIn=j.yawIn=j.rollIn=0.0f;j.hasUp=false;j.hasAim=false;
    if(j.insetSaved)Put<float>(v,kAreaInset,j.savedInset);
    j.insetSaved=false;
    Log("PJET v=%p the player moved to another seat (%s, %.0f m/s): no ejection",v,kPhaseNames[static_cast<int>(j.phase)],Len(j.vel));
    if(j.board)HandBack(j,v,"the player moved to another seat");
}

void Report(PJet& j,const unsigned char* v,const Stick& s,const float* pos,float clear,bool water,ULONGLONG ms) noexcept {
    if(!Cfg().debug || ms-j.logAt<kLogMs)return;
    j.logAt=ms;
    const float speed=Len(j.vel);
    Log("PJET v=%p %s %.0f m/s climb %.1f thr %.2f g %.2f%s pos=(%.0f,%.0f,%.0f) clear %.0f%s hp %.0f in(turn %.2f pitch %.2f "
        "roll %.2f) seat(LX %.2f LY %.2f RX %.2f RY %.2f asc %.2f)",v,kPhaseNames[static_cast<int>(j.phase)],speed,j.vel[1],j.throttle,
        j.load,j.stall ? " STALL" : "",pos[0],pos[1],pos[2],clear,water ? " (water)" : "",At<float>(v,kHp),s.turn,s.pitch,s.roll,
        s.lx,s.ly,s.rx,s.ry,s.ascend);
}

// How it moved since last frame (see GameStep): `measured`, and the game time the frame stepped.
float Measure(PJet& j,const float* pos,ULONGLONG ms) noexcept {
    const ULONGLONG since=j.lastMs ? ms-j.lastMs : 0;
    j.lastMs=ms;
    const float dt=GameStep(since);
    j.fresh=j.havePrev && since>0;
    if(j.fresh)for(int i=0;i<3;++i)j.measured[i]=(pos[i]-j.prev[i])/dt;
    std::memcpy(j.prev,pos,12);j.havePrev=true;
    return dt;
}

// The catch's jet with no one aboard (see kCatchFrom): the player jet's own flight (Air) with the mouse aim's steering
// at the target, the throttle full until kCatchHoming, then the speed it flies in at; its last kCatchHoming m in
// formation on the door (Catch). Never under kCatchFloor over the ground. With no door known (Catch: a jet just made,
// or one whose seat cannot be read) it is the flight all the way, its terrain, stall and crash with it: never a frame
// without one, its velocity left as it was.
void AutoFly(PJet& j,unsigned char* v,const float* pos,float dt,ULONGLONG ms) noexcept {
    if(!j.autopilot) {
        j.autopilot=true;j.driven=false;j.phase=Phase::air;j.hasUp=false;
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        for(int i=0;i<3;++i)j.vel[i]=m[8+i]*catchFlight.speed;
        j.throttle=1.0f;j.keys=true;j.mouseFlies=true;
        Log("PJET catch: v=%p on the autopilot",v);
    }
    bool water=false;
    const float clear=FloorClear(j,v,pos,Clear(pos,&water));
    float to[3]={catchFlight.target[0]-pos[0],catchFlight.target[1]-pos[1],catchFlight.target[2]-pos[2]};
    if(clear!=kNoGround && clear<kCatchFloor && to[1]<0.0f)to[1]=0.0f;   // not into the ground
    const float dist=Len(to);
    if(Normalize(to)){std::memcpy(j.aim,to,12);j.hasAim=true;}
    j.throttle=dist>kCatchHoming ? 1.0f : 0.7f;
    Stick s{};
    s.keys=true;
    Put<float>(v,kInLateral,0.0f);Put<float>(v,kInForward,0.0f);Put<float>(v,kInYaw,0.0f);
    Put<float>(v,kInThrottle,0.0f);Put<float>(v,kInW,1.0f);
    Put<float>(v,kAreaInset,kNoInset);
    j.clear=clear;j.climb=j.vel[1];
    if(catchFlight.hasDoor && dist<kCatchHoming) {   // formation is not an airborne wing: no Air's stall floor or landing/crash step
        pjet::CatchVelocity(pos,catchFlight.target,catchFlight.drift,catchFlight.speed,kCatchGain,kCatchBrake,j.vel);
        if(clear!=kNoGround)j.vel[1]=std::fmax(j.vel[1],std::fmin((kCatchFloor-clear)/dt,kUnderClimb));
        const float up[3]={0.0f,1.0f,0.0f};
        if(j.kind)BodyAttitude(v,catchFlight.heading,up,kAttGain,BodyCap(*j.kind),j.omega);
    } else Air(j,v,s,pos,clear,water,dt,ms);
    j.active=!v[kDead];
    std::memcpy(j.sent,j.vel,12);
    Elevons(j,v,s.pitch,s.roll,dt);
    GearStep(v,true,dt,false);   // the catch's jet flies in with its gear up
    JetFlames(v,j.throttle,j.throttle>0.95f,ms);
}

// The landing gear (gear.cpp PlayerGear): the gear key (keyboard and mouse) or the pad's gear button toggles it.
void PilotGear(PJet& j,unsigned char* v,float dt) noexcept {
    const unsigned char* seat=SeatAt(v,0);
    const bool held=j.keys ? KeyDown(Cfg().playerJetGearKey)
                           : (At<std::uint16_t>(seat,kSeatButtons)&static_cast<std::uint16_t>(Cfg().playerJetGearButton))!=0;
    PlayerGear(v,held,j.keys,j.phase==Phase::air,Len(j.vel),j.clear,j.climb,dt);
}
// Any of the plugin's other aircraft under the player (boarding, a rotor craft's flight, the special stores, the hail).
#include "playerjet_board.inc"
// The gunship's crew: its gunner seat (the player at the gun, an NPC at it under the player at the stick).
#include "playerjet_crew.inc"

void Fly(PJet& j,unsigned char* v,ULONGLONG ms) noexcept {
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float dt=Measure(j,pos,ms);
    const bool wet=j.wetFrame && j.wetFrame+1>=GameFrame();   // a water message this frame or the last
    const bool driven=SeatCount(v)>0 && SeatRider(SeatAt(v,0))==Rider::player;
    if(!driven && catchFlight.v==v && !v[kDead]){AutoFly(j,v,pos,dt,ms);return;}
    if(!driven && j.hail.phase!=kHailNone && !v[kDead]){HailFly(j,v,pos,dt,ms);return;}   // called down for the player
    if(!driven) {
        if(j.autopilot){j.autopilot=false;j.active=false;if(j.board)HandBack(j,v,"the catch is over");}
        if(j.driven && AboardElsewhere(v,0))Moved(j,v);
        else if(j.driven)Leave(j,v,FloorClear(j,v,pos,GroundClearance(pos)),true,"got out");
        if(wet)Crash(j,v,0.0f,0.0f,false,ms,nullptr);   // empty and afloat: it breaks up
        return;
    }
    bool water=false;
    const float clear=FloorClear(j,v,pos,Clear(pos,&water));   // every airframe's actual bottom, not its rigid-body centre
    if(!j.driven)Board(j,v,pos,clear);
    Stick s=ReadStick(SeatAt(v,0));
    SmoothStick(j,s,dt);
    j.keys=s.keys;
    Stores(j,v,s,pos);
    Sight(j,v,pos,dt);
    Threats(j,v,pos);
    Proximity(j,pos,clear);
    SpecialFrame(j,v,s,pos,ms);
    CrewGunner(j,v,dt,ms);   // the gunship's NPC gunner (playerjet_crew.inc)
    // The heli stays out of it: no rotor lift, no heli stick (docs/heli-input-re.md §2a).
    Put<float>(v,kInLateral,0.0f);Put<float>(v,kInForward,0.0f);Put<float>(v,kInYaw,0.0f);
    Put<float>(v,kInThrottle,0.0f);Put<float>(v,kInW,1.0f);
    Put<float>(v,kAreaInset,kNoInset);
    if(j.board && j.board->frame==pjet::Airframe::rotor){HoverStep(j,v,s,pos,clear,water || wet,dt,ms);Report(j,v,s,pos,clear,water,ms);return;}
    Blocked(j,v,pos,ms);
    if(v[kDead]){j.active=false;return;}
    Lever(j,v,s,dt);   // record this frame's command before touchdown decides whether this is a touch-and-go
    // A physics-supported airframe can be held just above the ray's old origin threshold, or rebound with upward
    // velocity. Reconcile that real contact before choosing air/ground motion; a nearby unit is not ground.
    ReconcileGround(j,v,clear,water,wet,ms);
    if(v[kDead]){j.active=false;return;}
    j.clear=clear;j.climb=j.vel[1];
    if(j.phase==Phase::air) {
        // In the water, though the surface probe saw none (off, unknown map): it touched it.
        if(wet && !water)Touch(j,v,Len(j.vel),true,ms);
        else Air(j,v,s,pos,clear,water,dt,ms);
    } else {
        Ground(j,v,s,clear,dt);
        if(water || wet)Crash(j,v,0.0f,Len(j.vel),false,ms,nullptr);   // afloat: it breaks up, one crash a kCrashMs
    }
    j.active=j.phase!=Phase::parked && !v[kDead];
    std::memcpy(j.sent,j.vel,12);
    MirrorEntry(j,v);
    Elevons(j,v,s.pitch,s.roll,dt);
    PilotGear(j,v,dt);
    Report(j,v,s,pos,clear,water,ms);
    // The exhaust (booster.cpp JetFlames) with the throttle; the lever full forward is the afterburner.
    JetFlames(v,j.throttle,j.throttle>0.95f,ms);
}
}  // namespace

// The 506 physics step (body506.cpp), after the stock one: the player jet's velocity and spin.
bool PlayerJetBodyStep(unsigned char* v,float* lin,float* ang) noexcept {
    PJet* j=Find(v);
    // Only what this frame's (or the last one's) flight step sent: a jet the step no longer runs for (the plugin
    // turned off) is the stock body's again.
    if(!Cfg().enabled || !Cfg().playerJet)return false;   // handed back to the stock step
    if(!j || !j->active || !(j->driven || j->autopilot) || v[kDead] || j->frame+1<GameFrame())return false;
    const auto body=At<void*>(v,kBody);
    if(!body)return false;
    JetMotionProps(body);
    // A shield hit as a building's (shield.cpp): its speed across the face gone, and the crash Blocked would give.
    if(const float lost=ShieldBlock(v,j->vel);lost>0.0f && j->phase==Phase::air)
        Crash(*j,v,0.0f,lost+j->kind->landMax,false,GameMs(),nullptr);
    for(int i=0;i<3;++i){lin[i]=j->vel[i];ang[i]=j->omega[i];}
    return true;
}

// The 506's messages to a player jet (body506.cpp): the water's taken whole (the 506 would take it as a heli
// ditching, twice its HP in damage every frame): the plugin's crash model has it (Fly).
bool PlayerJetMessage(unsigned char* v,std::uint32_t msg,void* data,MessageRestore* restore) noexcept {
    (void)data;(void)restore;
    if(msg!=kMsgWater || !Cfg().enabled || !Cfg().playerJet)return false;
    PJet* j=Find(v);
    if(!j || !j->driven)return false;   // parked or not flown by the plugin: the stock ditching stands
    j->wetFrame=GameFrame();
    return true;
}

// A jet the player flew that is dead or gone without its frame having seen it (its object deleted at once, or no
// input frame for a wreck): the same as PlayerJetFrame's, from the last frame's clearance.
bool Gone(const PJet& j) noexcept {
    __try { return !j.ref.Is(j.vehicle) || j.vehicle[kDead]; } __except(EXCEPTION_EXECUTE_HANDLER){return true;}
}

void PlayerEjectTick() noexcept {
    if(flyOk && Cfg().playerJet)
        for(auto& j:jets)if(j.driven && j.vehicle && Gone(j))Leave(j,j.vehicle,j.clear,false,"destroyed");
    EjectTick();
    ChuteTick();
    FlaresStep();
    if(flyOk && Cfg().playerJet){HailTick();GunnerTick();}
}

// The catch's SGOs (pjet::kCatchFiles) for this mission: the player jets' whenever installed, the requested twins of the
// plugin's other aircraft only when the player may board those (PlayerJetAll) and the catch is on (PlayerJetCatch):
// ~10 KB of SGO each, their models the NPC bodies' (jet_spawn.cpp PreloadJets has them loaded already).
void PreloadPlayerJets() noexcept {
    const bool twins=Cfg().playerJetAll && Cfg().playerJetCatch;
    char line[256];
    int at=0;
    for(int i=0;i<pjet::kCatchFileCount;++i) {
        const pjet::CatchFile& f=pjet::kCatchFiles[i];
        playerJetPreloaded[i]=false;
        const auto mgr=At<void*>(image,kPreloadMgrAt);
        if((f.player || twins) && mgr && jet::SpawnReady() && jet::ModFileThere(f.file)) {
            __try {
                reinterpret_cast<void(*)(void*,const wchar_t*,std::int32_t,std::int32_t)>(image+kPreloadFn)(mgr,f.sgo,2,-1);
                playerJetPreloaded[i]=true;
            } __except(EXCEPTION_EXECUTE_HANDLER){}
        }
        const int n=sprintf_s(line+at,sizeof(line)-at,"%s%s=%d",i ? " " : "",f.name,playerJetPreloaded[i]);
        if(n>0)at+=n;
    }
    chutePreloaded=false;   // the parachute's canopy (ChuteMake), when installed
    if(const auto mgr=At<void*>(image,kPreloadMgrAt);chuteOk && mgr && jet::SpawnReady() && jet::ModFileThere(kChuteFile)) {
        __try {
            reinterpret_cast<void(*)(void*,const wchar_t*,std::int32_t,std::int32_t)>(image+kPreloadFn)(mgr,kChuteSgo,2,-1);
            chutePreloaded=true;
        } __except(EXCEPTION_EXECUTE_HANDLER){}
    }
    bail=Bailout{};catchFlight=CatchFlight{};
    Log("PJET preload for the catch: %s (twins %s); the parachute's canopy=%d",line,twins ? "on" : "off: PlayerJetAll / PlayerJetCatch",
        chutePreloaded);
}

// The guns' rounds for the cockpit's stores line (the stock gauge's gun panels gone, stockgauge.cpp): seat 0's weapons
// that are neither a store nor the fuel tank (pylib/vcobjects.py JETS: guns L / R first), how many, and the fewest rounds
// in one (the pair fires together: the first dry stops the burst's other half).
namespace {
void GunRounds(const unsigned char* v,PlayerJetReadout& r) noexcept {
    r.guns=0;r.gunRounds=0;
    const unsigned char* const seat=SeatAt(const_cast<unsigned char*>(v),0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!n || n>8 || !Readable(holders,n*8))return;
    for(std::uint64_t i=0;i<n;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const unsigned char* const w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,kWeaponAmmo+4) || IsStoreWeapon(w) || IsFuelTank(w))continue;
        const std::int32_t ammo=At<std::int32_t>(w,kWeaponAmmo);
        r.gunRounds=r.guns++==0 || ammo<r.gunRounds ? ammo : r.gunRounds;
    }
}
}  // namespace

bool PlayerJetHud(PlayerJetReadout* out) noexcept {
    if(!flyOk || !Cfg().enabled || !Cfg().playerJet)return false;
    for(const auto& j:jets) {
        if(!j.ref || !j.driven || !j.kind)continue;
        const unsigned char* v=j.vehicle;
        __try {
            if(!j.ref.Is(v) || v[kDead])continue;
            const bool air=j.phase==Phase::air,ground=j.clear!=kNoGround;
            const float* pos=reinterpret_cast<const float*>(v+kPosition);
            PlayerJetReadout r{};
            r.speed=Len(j.vel);r.throttle=j.throttle;r.clear=ground ? j.clear : pos[1];r.climb=j.climb;
            r.hp=At<float>(v,kHp);r.hpMax=At<float>(v,kHpMax);r.load=air ? j.load : 1.0f;
            const Kind* const kind=KindOf(v);
            r.rotate=kind ? kind->rotate : 0.0f;
            r.air=air;r.stall=air && j.stall;r.ground=ground;r.pullUp=air && j.gpws==Gpws::pullUp;r.threat=j.threat;r.flares=j.flares;r.keys=j.keys;r.aiming=air && j.keys && j.hasAim && Cfg().playerJetMouseFlight && j.mouseFlies;
            float path[3]={j.vel[0],j.vel[1],j.vel[2]};
            if(!Normalize(path))std::memcpy(path,j.aim,12);
            for(int i=0;i<3;++i){r.aim[i]=pos[i]+j.aim[i]*kAimMark;r.path[i]=pos[i]+path[i]*kAimMark;}
            r.stores=j.stores;r.store=j.store;r.storeButton=kButtonLB;r.targetButton=kButtonX;
            for(int i=0;i<j.stores && i<kMostStores;++i){r.storeName[i]=j.storeName[i];r.storeRounds[i]=j.storeRounds[i];r.storeRole[i]=j.storeRole[i];}
            r.bomb=j.bomb;r.hasImpact=j.hasImpact;std::memcpy(r.impact,j.impact,12);
            r.lock=j.lock;std::memcpy(r.lockAt,j.lockAt,12);r.lockProgress=j.lockProgress;
            r.sym=j.sym;
            FuelGauge(v,&r.fuel);   // its 506 body's tank, which the stock FUEL gauge showed (stockgauge.cpp)
            GunRounds(v,r);
            r.gpws=air ? j.gpws : Gpws::none;r.impactIn=r.gpws!=Gpws::none ? j.impactIn : -1.0f;
            r.area=air ? j.area : 0;
            const bool rotor=j.board && j.board->frame==pjet::Airframe::rotor;
            r.liftShare=air && !rotor ? j.stallShare : 0.0f;
            if(rotor) {   // the helicopter HUD's (hud.cpp HeliHud)
                HeliFlight& f=r.heli;
                r.rotor=true;r.aiming=false;
                std::memcpy(f.vel,j.vel,12);
                f.speed=std::sqrt(j.vel[0]*j.vel[0]+j.vel[2]*j.vel[2]);f.clear=r.clear;f.ground=ground;f.climb=j.climb;
                f.hp=r.hp;f.hpMax=r.hpMax;f.keys=j.keys;f.landed=!air;
                f.aiming=j.keys && j.hasAim && Cfg().heliMouseAim;f.holding=f.aiming && j.hover.holding;
                f.setSpeed=j.hover.speed;f.top=j.hoverTop;std::memcpy(f.aim,r.aim,12);
                f.gpws=r.gpws;f.impactIn=r.impactIn;
            }
            *out=r;
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){continue;}
    }
    return false;
}

bool PlayerJetHailHint(const float* from,float* at,float* distance,bool* coming) noexcept {
    if(!flyOk || !Cfg().enabled || !Cfg().playerJet || !Cfg().playerJetAll || !Cfg().playerJetHailKey)return false;
    __try {
        for(const auto& j:jets)
            if(j.vehicle && j.hail.phase!=kHailNone && Live(j)) {
                const float* p=reinterpret_cast<const float*>(j.vehicle+kPosition);
                std::memcpy(at,p,12);*distance=vec::Dist(p,from);*coming=true;
                return true;
            }
        float d=0.0f;
        const jet::Jet* const e=HailChoice(from,nullptr,&d);
        if(!e)return false;
        std::memcpy(at,e->Vehicle()+kPosition,12);*distance=d;*coming=false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool PlayerJetBoardable(const void* vehicle) noexcept {
    __try { return BoardableNow(static_cast<const unsigned char*>(vehicle)); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool GunshipCrewSeats(const void* vehicle) noexcept {
    if(!flyOk || !Cfg().playerJet || !Cfg().playerJetAll)return false;
    __try { return GunshipSeats(static_cast<const unsigned char*>(vehicle)); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

unsigned GunshipBoardSeat() noexcept {
    return Cfg().gunshipBoardGunner!=KeyDown(Cfg().gunshipGunnerKey) ? kGunnerSeat : 0u;
}

bool PlayerGunnerOrder(const void* vehicle,GunnerOrder* out) noexcept {
    __try {
        if(!gunner.ref.Is(vehicle) || gunner.frame+1<GameFrame())return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    out->centred=gunner.centred;
    std::memcpy(out->at,gunner.centre,12);std::memcpy(out->home,gunner.home,12);
    return true;
}

bool PlayerGunnerHud(GunnerReadout* out) noexcept {
    if(!gunner.ref || gunner.frame+1<GameFrame())return false;
    *out=gunner.hud;
    return true;
}

bool jetsound::PlayerState(const unsigned char* v,jetsound::State* out) noexcept {
    if(!flyOk || !Cfg().playerJet)return false;
    const PJet* const j=Find(v);
    if(!j || j->frame!=GameFrame())return false;
    // A record can survive getting out; its old phase/throttle must not start an empty engine.
    const bool controlled=j->driven || j->autopilot || j->hail.phase!=kHailNone || j->keep;
    if(!controlled)return false;
    *out={false,controlled,j->keep || j->phase==Phase::parked,
          j->board && j->board->frame==pjet::Airframe::rotor,j->throttle,Len(j->vel)};
    return true;
}

bool PlayerJetHolds(const void* vehicle) noexcept {
    if(!flyOk || !Cfg().playerJet)return false;
    __try { return BodyOf(vehicle)==PluginBody::jet && Held(static_cast<const unsigned char*>(vehicle)); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool IsPlayerJet(const void* vehicle) noexcept {
    __try { return BodyOf(vehicle)==PluginBody::playerJet && KindOf(static_cast<const unsigned char*>(vehicle))!=nullptr; }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool PlayerJetOwnSight(const void* vehicle) noexcept {
    // KindOf, not IsPlayerJet: the plugin's other aircraft the player boards fly this path too (playerjet_kinds.h).
    if(!flyOk || !Cfg().enabled || !Cfg().playerJet || !Cfg().playerJetGunSight)return false;
    __try { return KindOf(static_cast<const unsigned char*>(vehicle))!=nullptr; }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// The catch jet's frame turned away before its autopilot (debug, once a reason): its catch jet fell like a stone and
// no autopilot line came (2026-10-05 12:56).
void CatchWhy(const unsigned char* v,int why) noexcept {
    static const void* saidFor=nullptr;
    static int said=-1;
    if(v!=catchFlight.v || (v==saidFor && why==said))return;
    saidFor=v;said=why;
    static const char* const kWhy[]={"no kind (mark %.0f, body %d)","dead","no table entry","to its flight"};
    char text[96];
    std::snprintf(text,sizeof(text),kWhy[why],BodyMark(v),static_cast<int>(BodyOf(v)));
    Log("PJET catch jet %p frame: %s",v,text);
}

void PlayerJetFrame(unsigned char* v) noexcept {
    if(!flyOk || !Cfg().playerJet)return;
    GunnerFrame(v);   // the gunship's gunner seat (playerjet_crew.inc): before Held, which taking the gun may end
    // An NPC aircraft is the player's only while they fly it, it comes down for them, catches them or waits for them
    // (Held); its entry let go of once it is not (playerjet_board.inc).
    if(BodyOf(v)==PluginBody::jet && !Held(v)){Forget(v);return;}
    const Kind* kind=KindOf(v);
    if(!kind){CatchWhy(v,0);return;}
    if(v[kDead]) {   // destroyed with the player in it: out they go (Leave)
        if(PJet* j=Find(v);j && j->driven)Leave(*j,v,j->clear,true,"destroyed");
        CatchWhy(v,1);
        return;
    }
    const ULONGLONG ms=GameMs();
    PJet* j=Find(v);
    if(!j && (j=Make(v,kind))!=nullptr)j->board=BoardRowOf(v);
    if(!j){CatchWhy(v,2);return;}
    CatchWhy(v,3);
    if(!j->bodyFixed){FixBodyPart506(v,"PJET");j->bodyFixed=true;}   // looked up once: logged when missing
    j->frame=GameFrame();
    Fly(*j,v,ms);
}

bool InstallPlayerJets() noexcept {
    flyOk=Body506Ok();
    if(!flyOk)Log("PJET: no 506 physics hook (body506): player jets off");
    chuteOk=Matches(kFarEventUpdate,kFarEventUpdateSig,sizeof(kFarEventUpdateSig)) &&
            Readable(image+kFarEventVtable,(kVtableUpdate+1)*8) &&
            At<const void*>(image+kFarEventVtable,kVtableUpdate*8)==image+kFarEventUpdate;
    Log("HOOK player jets fly=%d water=%d die=%d bodyPart=%d chute=%d",flyOk,Body506MessageOk(),Die506Ok(),BodyPartOk(),chuteOk);
    for(int i=0;i<pjet::kBoardableCount && Cfg().debug;++i) {   // what each of the plugin's aircraft flies like under the player
        const Kind& k=kBoardKinds[i];
        Log("PJET boardable %s (%s): minAir %.0f rotate %.0f top %.0f m/s, thrust %.1f, %.1f g, roll %.2f, land %.0f, ram %.0f m",k.name,
            pjet::kBoardable[i].frame==pjet::Airframe::rotor ? "rotor" : "wing",k.minAir,k.rotate,k.top,k.thrust,k.maxG,k.roll,k.landMax,k.ram);
    }
    return flyOk;
}

// A new mission (mission.cpp MissionStart): the last mission's jets are gone with it.
void ResetPlayerJets() noexcept {
    for(auto& j:jets)j=PJet{};
    ChuteForget();
}
}  // namespace crew
