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
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "body506.h"
#include "jetaudio.h"
#include "memory.h"
#include "vecmath.h"
#include <cmath>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
using vec::Clamp;using vec::Cross;using vec::Dot;using vec::Len;using vec::Normalize;
constexpr std::size_t kBody=0x1650;
constexpr std::size_t kInLateral=0x1540,kInThrottle=0x1544,kInForward=0x1548,kInW=0x154C,kInYaw=0x1550;
constexpr std::size_t kHpMax=0x2F4,kHp=0x2F8,kObjFlags=0x18,kCtrlUses=8;
constexpr unsigned char kObjDeleted=4;
// The seat's stick block (docs/heli-input-re.md §4): left stick, right stick, the ascend trigger (the heli's
// collective: analog 0..1 on a pad, 0 or 1 on the keyboard).
constexpr std::size_t kSeatLX=0x2C0,kSeatLY=0x2C4,kSeatRX=0x2D0,kSeatRY=0x2D4,kSeatAscend=0x2E0;
constexpr std::size_t kSeatPad=0x2B0;   // 1: the rider plays on a pad, 0: the keyboard and mouse (heli-input-re.md §4)
constexpr std::size_t kSeatButtons=0x2E8;   // word: pad A B X Y LB RB L3 R3 (docs/stores-re.md §4)
constexpr std::uint16_t kButtonLB=0x10,kButtonX=0x04;
constexpr std::size_t kFireStore=0x2021;    // the 506's secondary fire byte (holder 2)
// The bomb's fall (Impact): kFallStep s a segment, at most kFallMost s.
constexpr float kFallStep=0.25f,kFallMost=40.0f;
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
    float ram;           // m: the reach of what it rams (its size: the fighter's model is 16 m across, the strike jet's 25 m)
};
constexpr Kind kKinds[]={
    {"fighter",7201, 65.0f,75.0f,260.0f, 16.0f,32.0f, 6.0f,150.0f, 2.6f, 130.0f, 10.0f},   // 1 g at 61 m/s
    {"strike", 7202, 60.0f,70.0f,240.0f, 11.0f,26.0f, 5.0f,140.0f, 1.6f, 120.0f, 12.0f},   // 1 g at 63 m/s
};
constexpr float kAutoRotate=20.0f;     // m/s over rotate: it lifts off without the stick...
constexpr float kAutoThrottle=0.6f;    // ...with the throttle at least this open (not rolling out a landing)
constexpr float kLiftOffClimb=5.0f;    // m/s up the moment it lifts off
constexpr float kThrottleRate=1.0f;    // the throttle lever's travel a second (closed to open in 1 s)
// In the air the throttle is Ace Combat's: held forward (or ascend) it boosts to full, held back it closes and brakes,
// let go it returns to kCruiseThrottle; on the ground it stays where the stick left it (taxi, hold, roll out).
constexpr float kCruiseThrottle=0.55f;
constexpr float kAirThrottleRate=1.5f;
constexpr float kDeadZone=0.08f;
constexpr float kTaxiTurn=0.8f;        // rad/s: the slowest taxi turn rate (the nose wheel), less fast
constexpr float kTaxiFull=25.0f;       // ...from this ground speed on (rate times kTaxiFull / speed)
constexpr float kGroundBrake=12.0f;    // m/s^2 rolling with the throttle closed
constexpr float kParkSpeed=0.5f;       // below this, throttle closed: parked (the stock code holds it)
// The plane's own up (PJet::up), carried along its path: the roll rotates it about the nose at kRollRate; let go it
// returns to the bank the turn stick asks for (kTurnBank at full, a coordinated turn: Air), at most kLevelRate, unless the pitch stick is held
// (kLevelPull: pulled through the top it loops, as in Ace Combat) or it points
// within kVertical of straight up or down (no "level" there: it keeps its up).
constexpr float kRollRate=4.2f;        // rad/s at full roll stick (a full roll in 1.5 s)
constexpr float kLevelRate=1.8f;       // rad/s
constexpr float kTurnBank=1.2f;        // rad (69 deg, 2.8 g to hold the height): the bank of a full turn stick
constexpr float kRollDead=0.08f;       // the roll stick under this is let go
constexpr float kLevelPull=0.15f;      // ...and it levels only with the pitch stick under this (held, it loops)
constexpr float kVertical=0.97f;       // sine of the climb past which there is no level to return to
// The lift with the stick let go (Air Hold): the flight control holds kHold of what keeps the path from bending
// (g * cos(climb), along the plane's up), so level flight starts to sink; the nearer the sink to kSettleSink the
// more it holds, all of it there: level flight settles into a sink of kSettleSink (over ~13 s), a climb bends
// slowly over, a dive is held. Never more than the wing gives at its speed.
constexpr float kHold=0.988f,kSettleSink=1.5f;   // m/s
constexpr float kMinBankCos=0.25f;     // a turn's hold (its lift / cos bank) at most 4 g of it
constexpr float kPush=0.5f;            // the stick forward: down to kPush of the most lift, negative
// Drag (Air): parasitic, Kind::thrust * (speed / top)^2 (full throttle levels off at top); induced, kInduced per g^2
// pulled at the corner speed, more as the square of corner / speed (a 6 g turn at the corner: 11 m/s^2).
constexpr float kInduced=0.3f;
constexpr float kStallWarn=1.05f;
// The mouse's aim (AimSteer): its heading turns kAimPerUnit rad per unit of a frame's mouse X (the seat's right stick
// on the keyboard: the frame's movement, at most 1), times ini PlayerJetMouseSpeed, its elevation likewise; the mouse
// takes it no farther than kAimMaxEl from level (an aim already past, the nose's when W was let go, stays). The plane
// turns its path toward it at kSteer times the angle off (rad/s), the lift for that and for holding the path up (Hold)
// along its up. Turning (the aim kAimTurnFrom off or more) it banks toward that lift at kRollRate; on the aim it only
// levels its wings, gently (kLevelRate), and not at all climbing or diving steeper than kAimSteep (sine): letting W go
// in a steep climb or over the top of a loop keeps the attitude it has (it snapped upright at kRollRate before,
// 2026-10-04). Under kAimBankMin g of lift it keeps its bank.
constexpr float kMouseMoved=0.02f;    // a frame's mouse movement past this hands the plane to the aim
constexpr float kAimPerUnit=0.05f,kAimMaxEl=1.3f,kSteer=1.6f,kAimBankMin=0.3f,kAimTurnFrom=0.09f,kAimSteep=0.77f;
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
constexpr float kCeilingGap=12.0f;
// The world's walls (kWorldWallIn inside WorldHalf: the Havok broadphase's edge, 3000 m a side unless ini BigWorld raises it): a path out through one is
// turned along it and kWallIn back in, so the plane never stops at the wall (WallTurn).
constexpr float kWorldWallIn=600.0f,kWallIn=0.3f,kWallAlong=0.9f;   // kWorldWallIn: m inside the world's edge (WorldHalf)
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
    float up[3];                 // the plane's own up in the air (see kRollRate)
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
    bool bomb,hasImpact;         // the store picked is a bomb; where it would hit now (Impact)
    float impact[3];
    bool targetHeld;             // the target key / X down last frame
    int lock;                    // the picked store's lock (StoreLock), for the cockpit
    float lockAt[3],lockProgress;
    bool stall;                  // ...and whether all its wing gives is too little to hold its path (kStallWarn)
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
    ULONGLONG frame;             // GameFrame of its last flight step
    ULONGLONG wetFrame;          // GameFrame of the last water message (0: none)
    bool dieLogged;
    const unsigned char* model;  // the bone array the elevons were found in
    unsigned char* elevon[2];
    float elevonBind[2][16],elevonSet[2][16],elevonAt[2];
};
constexpr int kMaxJets=16;
PJet jets[kMaxJets]{};

bool flyOk=false;

float Axis(const unsigned char* seat,std::size_t at) noexcept {
    const float x=At<float>(seat,at);
    if(!std::isfinite(x) || std::fabs(x)<kDeadZone)return 0.0f;
    return Clamp(x,-1.0f,1.0f);
}

const Kind* KindOf(const unsigned char* v) noexcept {
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
        j=PJet{};j.ref=ObjRef::Of(v);j.vehicle=v;j.kind=kind;
        return &j;
    }
    static const void* refused=nullptr;
    if(refused!=v)Log("PJET v=%p: %d player jets known already: not flown",v,kMaxJets);
    refused=v;
    return nullptr;
}

// Metres over what is under `p`: the ground or the water's surface, the higher (map rays see the seabed under the
// sea, docs/water-re.md); kNoGround with neither. `water`: it is the water.
float Clear(const float* p,bool* water) noexcept {
    const float ground=GroundClearance(p);
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
struct Stick { float turn,pitch,throttle,yaw,roll; float lx,ly,rx,ry,ascend; bool keys; float aimX,aimY; bool switchStore,nextTarget; };

// Whether the virtual key `vk` is down while the game has the foreground (0: never).
bool KeyDown(int vk) noexcept {
    if(vk<=0)return false;
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

// The world's walls (see kWorldWallIn): a path (unit) out through one beyond it is turned along it (its way along
// kept, or the right of it when it flew straight at the wall) and kWallIn back in. Never less than the speed it has:
// only the direction turns.
void WallTurn(const float* pos,float* dir) noexcept {
    for(int i=0;i<3;i+=2) {
        if(std::fabs(pos[i])<WorldHalf()-kWorldWallIn || dir[i]*pos[i]<=0.0f)continue;
        const int o=2-i;   // the other horizontal axis: along the wall
        const float out=pos[i]>0.0f ? 1.0f : -1.0f;
        float along=dir[o];
        if(std::fabs(along)<0.2f) {   // at the wall head on: along it to its right
            float right[3];RightOf(dir,right);
            along=right[o]>=0.0f ? 1.0f : -1.0f;
        }
        dir[o]=(along>0.0f ? 1.0f : -1.0f)*kWallAlong;
        dir[i]=-out*kWallIn;
        dir[1]=Clamp(dir[1],-0.3f,0.3f);
        if(!Normalize(dir)){dir[0]=0.0f;dir[1]=0.0f;dir[2]=1.0f;}
    }
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

// A hard hit: `sink` m/s into the ground, `speed` over it, `banked` wings too steep. Damage (see kCrashBase). `ram`:
// where it rammed something (not the ground, not the water): the enemies round it take that much damage (ini
// PlayerJetRamDamage times the share of its own max HP it lost) within its kind's reach. At most one a kCrashMs.
void Crash(PJet& j,unsigned char* v,float sink,float speed,bool banked,ULONGLONG ms,const float* ram) noexcept {
    if(ms-j.crashAt<kCrashMs)return;
    j.crashAt=ms;
    const float hpMax=At<float>(v,kHpMax),hp=At<float>(v,kHp);
    const float share=Clamp(kCrashBase+kCrashPerSink*(sink>kLandSink ? sink-kLandSink : 0.0f)+
                            kCrashPerSpeed*(speed>j.kind->landMax ? speed-j.kind->landMax : 0.0f)+(banked ? kBankCrash : 0.0f),
                            kCrashBase,kCrashMax);
    const float taken=share*(hpMax>0.0f ? hpMax : 1000.0f),left=hp-taken;
    Log("PJET v=%p crash: sink %.1f m/s, speed %.0f m/s%s: %.0f%% of max HP, hp %.0f -> %.0f",v,sink,speed,banked ? ", banked" : "",
        share*100.0f,hp,left>0.0f ? left : 0.0f);
    if(ram && Cfg().playerJetRamDamage>0.0f) {
        const float damage=taken*Cfg().playerJetRamDamage;
        const bool dealt=ImpactDamage(v,ram,damage,j.kind->ram);
        Log("PJET v=%p rammed at (%.0f,%.0f,%.0f): %.0f damage within %.0f m%s",v,ram[0],ram[1],ram[2],damage,j.kind->ram,
            dealt ? "" : " (not dealt: no charge this mission)");
    }
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

// On the ground: it rolls along its nose (level), turns at the nose wheel's rate, speeds up with the throttle
// and brakes with it closed; it lifts off at its rotate speed with the stick back, or kAutoRotate faster.
void Ground(PJet& j,const unsigned char* v,const Stick& s,float clear,float dt) noexcept {
    const Kind& k=*j.kind;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float nose[3]={m[8],0.0f,m[10]};
    if(!Normalize(nose)){nose[0]=0.0f;nose[2]=1.0f;}
    float speed=Dot(j.vel,nose);
    if(speed<0.0f)speed=0.0f;
    const float want=j.throttle*k.top;
    if(j.throttle<0.02f)speed-=kGroundBrake*dt;
    else speed+=Clamp(want-speed,-k.brake*dt,k.thrust*dt/(j.burden.mass>1.0f ? j.burden.mass : 1.0f));
    if(speed<0.0f)speed=0.0f;
    // The nose wheel: kTaxiTurn at taxi speeds, less from kTaxiFull on.
    const float rate=kTaxiTurn*(speed>kTaxiFull ? kTaxiFull/speed : 1.0f)*(speed>0.5f || s.throttle>0.0f ? 1.0f : 0.0f);
    const float a=-s.turn*rate*dt,co=std::cos(a),si=std::sin(a);
    const float turned[3]={nose[0]*co+nose[2]*si,0.0f,nose[2]*co-nose[0]*si};
    const float vy=j.measured[1]<0.0f ? (j.measured[1]>-30.0f ? j.measured[1] : -30.0f) : 0.0f;
    for(int i=0;i<3;i+=2)j.vel[i]=turned[i]*speed;
    j.vel[1]=vy;
    const float up[3]={0.0f,1.0f,0.0f};
    BodyAttitude(v,turned,up,kAttGain,k.roll,j.omega);
    if(speed>=k.rotate && (s.pitch>0.2f || (speed>=k.rotate+kAutoRotate && j.throttle>=kAutoThrottle))) {
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

// Touching the ground in the air: a landing (it rolls on) or a crash. Touching the water is always a crash.
void Touch(PJet& j,unsigned char* v,float speed,bool water,ULONGLONG ms) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float sink=j.vel[1]<0.0f ? -j.vel[1] : 0.0f;
    const float dirY=speed>1.0f ? j.vel[1]/speed : 0.0f;
    const bool banked=m[5]<kLandBank;
    if(!water && sink<=kLandSink && !banked && dirY>=kLandNose && speed<=j.kind->landMax) {
        j.phase=Phase::rolling;j.vel[1]=0.0f;
        Log("PJET v=%p landed at %.0f m/s, sink %.1f m/s",v,speed,sink);
        return;
    }
    if(water)Log("PJET v=%p hit the water at %.0f m/s, sink %.1f m/s",v,speed,sink);
    Crash(j,v,sink,speed,banked,ms,nullptr);
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

// The plane's up for this step (see kRollRate): the roll stick turns it about the path, let go it returns toward the
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
        Turn(j.up,dir,s.roll*kRollRate*dt);   // dir x up is the right: a right roll tips the up toward it
    } else if(!vertical && std::fabs(s.pitch)<kLevelPull) {   // pulling through the top: a loop, not a half roll
        float want[3];std::memcpy(want,level,12);
        Turn(want,dir,Clamp(s.yaw,-1.0f,1.0f)*kTurnBank);
        BankToward(j.up,dir,want,kLevelRate*dt);
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

// The mouse's aim moved by this frame's mouse (see kAimPerUnit): its heading about the world's up, its elevation.
void MoveAim(PJet& j,const Stick& s) noexcept {
    float flat[3]={j.aim[0],0.0f,j.aim[2]};
    if(!Normalize(flat)){flat[0]=0.0f;flat[2]=1.0f;}
    const float k=kAimPerUnit*Cfg().playerJetMouseSpeed;
    float right[3];RightOf(flat,right);
    const float a=s.aimX*k,co=std::cos(a),si=std::sin(a);
    for(int i=0;i<3;++i)flat[i]=flat[i]*co+right[i]*si;
    const float was=std::asin(Clamp(j.aim[1],-1.0f,1.0f));
    const float el=Clamp(was+s.aimY*k,was<-kAimMaxEl ? was : -kAimMaxEl,was>kAimMaxEl ? was : kAimMaxEl);
    j.aim[0]=flat[0]*std::cos(el);j.aim[1]=std::sin(el);j.aim[2]=flat[2]*std::cos(el);
}

// Whether the aim's mark lies within kAimOnScreen of the screen (view-projection `vp`, row vectors).
bool AimOnScreen(const float* vp,const float* pos,const float* aim) noexcept {
    const float p[3]={pos[0]+aim[0]*kAimMark,pos[1]+aim[1]*kAimMark,pos[2]+aim[2]*kAimMark};
    float c[4];
    for(int k=0;k<4;++k)c[k]=p[0]*vp[k]+p[1]*vp[4+k]+p[2]*vp[8+k]+vp[12+k];
    return c[3]>1e-3f && std::fabs(c[0]/c[3])<=kAimOnScreen && std::fabs(c[1]/c[3])<=kAimOnScreen;
}

// See kAimOnScreen: the aim as it was (`was`) when this frame's mouse took it off the screen; still off (the camera
// turned), toward the flight path `dir` until it is on.
void KeepAimOnScreen(PJet& j,const unsigned char* v,const float* dir,const float* was) noexcept {
    if(j.autopilot)return;   // the catch's autopilot aims off the player's screen
    float vp[16];
    if(!LastViewProj(vp))return;
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    if(AimOnScreen(vp,pos,j.aim))return;
    std::memcpy(j.aim,was,12);
    for(int step=0;step<10 && !AimOnScreen(vp,pos,j.aim);++step) {
        float a[3];
        for(int i=0;i<3;++i)a[i]=j.aim[i]*0.8f+dir[i]*0.2f;
        if(!Normalize(a))break;
        std::memcpy(j.aim,a,12);
    }
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
    const float off=std::acos(c),turn=kSteer*off*speed,across=Len(gPerp);
    float lift[3];
    for(int i=0;i<3;++i)lift[i]=toward[i]*turn-(across>1e-4f ? gPerp[i]/across*hold : 0.0f);
    float want[3];std::memcpy(want,lift,12);
    const bool turning=off>=kAimTurnFrom,steep=vertical || std::fabs(dir[1])>kAimSteep;
    if(Len(lift)>kAimBankMin*kG && (turning || !steep) && Across(want,dir))
        BankToward(j.up,dir,want,(turning ? kRollRate : kLevelRate)*dt);
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
    const bool aiming=j.autopilot || (s.keys && Cfg().playerJetMouseFlight && j.mouseFlies && !keyDown);
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
    float next[3];
    for(int i=0;i<3;++i)next[i]=dir[i]+(lift[i]+gPerp[i])*dt/speed;
    if(!Normalize(next))std::memcpy(next,dir,12);
    WallTurn(pos,next);
    Across(j.up,next);   // carried along the new path
    if(!aiming){std::memcpy(j.aim,next,12);j.hasAim=s.keys;}
    FlightWatch(j,v,s,next,!s.keys ? 2 : aiming ? 1 : 0);
    float bodyUp[3];std::memcpy(bodyUp,j.up,12);
    const float g=pitch/kG,top2=k.top*k.top;
    j.load=g;
    const float thrust=k.thrust*want*want/top2/mass;   // the engine's force over a heavier jet
    const float slow=k.corner/speed,drag=(k.thrust*speed*speed/top2*(1.0f+j.burden.drag))/mass+kInduced*g*g*slow*slow*mass;
    const float airbrake=s.throttle<0.0f ? k.brake*speed*speed/top2 : 0.0f;
    speed+=(thrust-drag-airbrake-kG*next[1])*dt;
    speed=Clamp(speed,kStallFloor,kBodyTop);
    for(int i=0;i<3;++i)j.vel[i]=next[i]*speed;
    // The ceiling the stock input holds every body under: it levels off under it.
    if(pos[1]>CeilingY()-kCeilingGap && j.vel[1]>0.0f)j.vel[1]=0.0f;
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
    BodyAttitude(v,nose,bodyUp,kAttGain,k.roll,j.omega);
    // The floor (ground or water): under it, out (it went through); touching it or about to within kFloorSweep
    // frames, a landing or a crash, its descent cut to stop kFloorGap over it (jet.cpp HoldOffGround).
    if(clear==kNoGround)return;
    if(clear<0.0f){j.vel[1]=j.vel[1]>kUnderClimb ? j.vel[1] : kUnderClimb;return;}
    float floorY=pos[1]-clear;
    if(j.vel[1]<0.0f && !water) {
        const float end[3]={pos[0]+j.vel[0]*dt*kFloorSweep,pos[1]+j.vel[1]*dt*kFloorSweep-kFloorGap,pos[2]+j.vel[2]*dt*kFloorSweep};
        float hit[3];
        if(MapRay(pos,end,hit)>=0.0f && hit[1]>floorY && hit[1]<pos[1])floorY=hit[1];
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
    // Where it hit: its nose along the way it was sent.
    float ram[3];
    for(int i=0;i<3;++i)ram[i]=pos[i]+j.sent[i]/sent*j.kind->ram*0.5f;
    Crash(j,v,0.0f,sent-made+j.kind->landMax,false,ms,ram);
    if(!j.active)return;
    if(j.phase!=Phase::air){j.vel[0]=j.vel[2]=0.0f;return;}
    for(int i=0;i<3;i+=2)j.vel[i]=-j.vel[i];
    float dir[3]={j.vel[0],0.2f*Len(j.vel),j.vel[2]};
    if(!Normalize(dir))return;
    for(int i=0;i<3;++i)j.vel[i]=dir[i]*j.kind->minAir;
}

// Where a bomb let go now would hit: its fall from `pos` at the jet's velocity under gravity (the game's bomb has no
// drag), the first ground a map ray finds along it, kFallStep s a segment.
bool Impact(const PJet& j,const float* pos,float* hit) noexcept {
    float at[3]={pos[0],pos[1],pos[2]},vel[3]={j.vel[0],j.vel[1],j.vel[2]};
    for(float t=0.0f;t<kFallMost;t+=kFallStep) {
        const float next[3]={at[0]+vel[0]*kFallStep,at[1]+(vel[1]-0.5f*kG*kFallStep)*kFallStep,at[2]+vel[2]*kFallStep};
        if(MapRay(at,next,hit)>=0.0f)return true;
        std::memcpy(at,next,12);
        vel[1]-=kG*kFallStep;
    }
    return false;
}

// Its stores (stores.h): the switch (key or LB, on its press) moves to the next with rounds left; one emptied, the
// next; the secondary fire (the stock fire byte, taken so the 506 does not fire holder 2 itself) pulls the picked
// one's trigger; what they weigh; the cockpit's list and, a bomb picked, where it would hit.
void Stores(PJet& j,unsigned char* v,const Stick& s,const float* pos) noexcept {
    Store st[kMostStores];
    const int n=ReadStores(v,st,kMostStores);
    j.stores=n;j.bomb=j.hasImpact=false;j.lock=0;
    j.burden=BurdenOf(static_cast<float>(j.kind->mark),st,n);
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
        if(j.store!=was){ClearStoreLock(st[was]);ClearStoreLock(st[j.store]);}   // no lock left on the store put away
        if(press)Log("PJET v=%p store: %s (%d left)",v,st[j.store].spec->name,st[j.store].ammo);
    }
    const bool next=s.nextTarget && !j.targetHeld;
    j.targetHeld=s.nextTarget;
    if(next && st[j.store].spec->role!=StoreRole::bomb){NextStoreTarget(st[j.store]);Log("PJET v=%p target: the next one",v);}
    j.lock=st[j.store].spec->role==StoreRole::bomb ? 0 : StoreLock(st[j.store],j.lockAt,&j.lockProgress);
    audio::LockTone(j.lock,j.lockProgress);
    const bool fire=v[kFireStore]!=0;
    v[kFireStore]=0;
    if(fire)TriggerStore(st[j.store]);
    for(int i=0;i<n;++i){j.storeName[i]=st[i].spec->name;j.storeRounds[i]=st[i].ammo;}
    j.bomb=st[j.store].spec->role==StoreRole::bomb;
    if(j.bomb && j.phase==Phase::air)j.hasImpact=Impact(j,pos,j.impact);
}

// The caught jet's speed for its boarding (see kCatchAfterMs).
// The catch's jet flying in (see kCatchFrom): the jet, where it makes for (under the parachuting player, led by their
// drift), its speed there.
struct CatchFlight { const void* v; float target[3],speed; } catchFlight{};

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
        std::memcpy(j.vel,j.sent,12);
        j.phase=Phase::air;j.throttle=1.0f;j.autopilot=false;
        catchFlight=CatchFlight{};
    }
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
constexpr float kCatchHoming=250.0f,kCatchReach=9.0f,kCatchLead=1.0f;
struct Bailout {
    Eject state; ULONGLONG at; float carry[2],vy;
    float mark,heading[3],speed;     // the jet left: its kind's mark, its nose, its speed (the catch)
    ObjRef caught; ULONGLONG caughtAt;
} bail{};
struct PlayerJetFile { float mark; const wchar_t* sgo; const wchar_t* file; };
constexpr PlayerJetFile kPlayerJetFiles[]={{7201.0f,L"app:/object/edf6vc_pjet_fighter.sgo",L"EDF6VC_PJET_FIGHTER.SGO"},
                                          {7202.0f,L"app:/object/edf6vc_pjet_strike.sgo",L"EDF6VC_PJET_STRIKE.SGO"}};
bool playerJetPreloaded[2]{};
constexpr unsigned kPreloadFn=0x7A3780,kCreateObjectFn=0x11945E0,kInitParamVt=0x1762068;
constexpr std::size_t kPreloadMgrAt=0x20B29A8,kObjectMgrAt=0x20B2958;
struct alignas(16) SpawnParam { const void* vtable; unsigned char rest[0x28]; };

unsigned char* SpawnCatchJet(float mark,const float* m) noexcept {
    for(int i=0;i<2;++i) {
        if(kPlayerJetFiles[i].mark!=mark || !playerJetPreloaded[i] || !jet::SpawnReady() || !At<void*>(image,kObjectMgrAt))continue;
        SpawnParam param{image+kInitParamVt,{}};
        unsigned char* v=nullptr;
        __try {
            v=reinterpret_cast<unsigned char*(*)(void*,const float*,const wchar_t*,SpawnParam*)>(image+kCreateObjectFn)(
                At<void*>(image,kObjectMgrAt),m,kPlayerJetFiles[i].sgo,&param);
        } __except(EXCEPTION_EXECUTE_HANDLER){playerJetPreloaded[i]=false;Log("PJET catch: the game faulted building %ls: off",kPlayerJetFiles[i].file);return nullptr;}
        if(!v)return nullptr;
        FixBodyPart506(v,"PJET");
        SetObjectTeam(v,kTeamVehicle);
        LevelVehicle(v);
        return v;
    }
    return nullptr;
}

void Catch(unsigned char* h,ULONGLONG ms) noexcept {
    const float* p=reinterpret_cast<const float*>(h+kPosition);
    const float* hv=reinterpret_cast<const float*>(h+kHumanVel);
    if(!bail.caught) {
        if(!Cfg().playerJetCatch || ms-bail.at<kCatchAfterMs || bail.mark<=0.0f)return;
        const float clear=GroundClearance(p);
        if(clear!=kNoGround && clear<kCatchClear){bail.mark=0.0f;Log("PJET catch: too low (%.0f m), the parachute goes on",clear);return;}
        float f[3]={bail.heading[0],0.0f,bail.heading[2]};
        if(!Normalize(f)){f[0]=0.0f;f[2]=1.0f;}
        float at[3]={p[0]-f[0]*kCatchFrom,p[1],p[2]-f[2]*kCatchFrom};
        const float under=GroundClearance(at);
        if(under!=kNoGround && under<kCatchFloor*2.0f)at[1]+=kCatchFloor*2.0f-under;
        alignas(16) const float m[16]={f[2],0,-f[0],0, 0,1,0,0, f[0],0,f[2],0, at[0],at[1],at[2],1};
        unsigned char* const v=SpawnCatchJet(bail.mark,m);
        if(!v){Log("PJET catch: no jet of mark %.0f could be made",bail.mark);bail.mark=0.0f;return;}
        const Kind* const k=KindOf(v);
        const float speed=std::fmax(bail.speed,(k ? k->rotate : 75.0f)+kCatchOver);
        catchFlight=CatchFlight{v,{p[0],p[1]-kCatchBelow,p[2]},speed};
        bail.caught=ObjRef::Of(v);bail.caughtAt=ms;
        Log("PJET catch: v=%p made %.0f m out at (%.0f,%.0f,%.0f), flying in at %.0f m/s to the player at (%.0f,%.0f,%.0f)",v,kCatchFrom,
            at[0],at[1],at[2],speed,p[0],p[1],p[2]);
        return;
    }
    unsigned char* const v=const_cast<unsigned char*>(static_cast<const unsigned char*>(bail.caught.obj));
    if(!bail.caught.Is(v) || v[kDead]){Log("PJET catch: the jet is gone");bail.state=Eject::none;catchFlight=CatchFlight{};return;}
    if(ms-bail.caughtAt>kCatchMostMs) {
        Log("PJET catch: given up, the player not aboard in %.0f s",static_cast<float>(kCatchMostMs)*0.001f);
        bail.caught=ObjRef{};bail.mark=0.0f;catchFlight=CatchFlight{};
        return;
    }
    // Where it makes for: under the player, kCatchLead s ahead of their drift.
    for(int i=0;i<3;++i)catchFlight.target[i]=p[i]+hv[i]*kCatchLead;
    catchFlight.target[1]-=kCatchBelow;
    const float* vp=reinterpret_cast<const float*>(v+kPosition);
    const float d[3]={p[0]-vp[0],p[1]-vp[1],p[2]-vp[2]};
    if(Len(d)<kCatchReach)PressBoardButton(h);
}

void EjectStart(const PJet& j,const unsigned char* v) noexcept {
    bail=Bailout{Eject::pending,GameMs(),{j.vel[0]*kEjectCarry,j.vel[2]*kEjectCarry},0.0f,0.0f,{0.0f,0.0f,1.0f},0.0f,ObjRef{},0};
    if(const Kind* k=KindOf(v))bail.mark=static_cast<float>(k->mark);
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    bail.heading[0]=m[8];bail.heading[1]=m[9];bail.heading[2]=m[10];
    bail.speed=Len(j.vel);
}

void EjectTick() noexcept {
    if(bail.state==Eject::none)return;
    unsigned char* const h=PlayerHuman();
    const ULONGLONG ms=GameMs();
    if(!h || h[kDead] || At<std::int32_t>(h,kHumanAttach)!=0){bail.state=Eject::none;return;}
    float* const vel=reinterpret_cast<float*>(h+kHumanVel);
    if(bail.state==Eject::pending) {
        if(At<std::uint32_t>(h,kHumanFlags)&kRiding) {   // the stock exit not through yet
            if(ms-bail.at>kEjectWaitMs)bail.state=Eject::none;
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
        bail.state=Eject::none;
        return;
    }
    Catch(h,ms);
    const unsigned char support=h[kHumanSupport];
    if(ms-bail.at>kChuteMostMs || (support==2 && ms-bail.at>300) || vel[1]>bail.vy+kChuteBoost) {
        Log("PJET parachute: %s",support==2 ? "landed" : vel[1]>bail.vy+kChuteBoost ? "flying by itself" : "too long");
        bail.state=Eject::none;
        return;
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
}

void Leave(PJet& j,unsigned char* v) noexcept {
    if(j.phase==Phase::air) {
        const float* pos=reinterpret_cast<const float*>(v+kPosition);
        const float clear=GroundClearance(pos);
        if(clear==kNoGround || clear>kEjectFrom)EjectStart(j,v);
    }
    j.driven=false;j.active=false;j.turnIn=j.pitchIn=j.yawIn=j.rollIn=0.0f;j.hasUp=false;j.hasAim=false;
    if(j.insetSaved){Put<float>(v,kAreaInset,j.savedInset);j.insetSaved=false;}
    Log("PJET v=%p left (%s, %.0f m/s)",v,kPhaseNames[static_cast<int>(j.phase)],Len(j.vel));
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
// at the target, the throttle full until kCatchHoming, then the speed it flies in at; its last kCatchHoming m straight
// for the target at that speed. Never under kCatchFloor over the ground.
void AutoFly(PJet& j,unsigned char* v,const float* pos,float dt,ULONGLONG ms) noexcept {
    if(!j.autopilot) {
        j.autopilot=true;j.driven=false;j.phase=Phase::air;j.hasUp=false;
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        for(int i=0;i<3;++i)j.vel[i]=m[8+i]*catchFlight.speed;
        j.throttle=1.0f;j.keys=true;j.mouseFlies=true;
        Log("PJET catch: v=%p on the autopilot",v);
    }
    bool water=false;
    const float clear=Clear(pos,&water);
    float to[3]={catchFlight.target[0]-pos[0],catchFlight.target[1]-pos[1],catchFlight.target[2]-pos[2]};
    if(clear!=kNoGround && clear<kCatchFloor && to[1]<0.0f)to[1]=0.0f;   // not into the ground
    const float dist=Len(to);
    if(!Normalize(to))return;
    std::memcpy(j.aim,to,12);j.hasAim=true;
    j.throttle=dist>kCatchHoming ? 1.0f : 0.7f;
    Stick s{};
    s.keys=true;
    Put<float>(v,kInLateral,0.0f);Put<float>(v,kInForward,0.0f);Put<float>(v,kInYaw,0.0f);
    Put<float>(v,kInThrottle,0.0f);Put<float>(v,kInW,1.0f);
    Put<float>(v,kAreaInset,kNoInset);
    j.clear=clear;j.climb=j.vel[1];
    Air(j,v,s,pos,clear,water,dt,ms);
    if(dist<kCatchHoming)for(int i=0;i<3;++i)j.vel[i]=to[i]*catchFlight.speed;   // the last stretch: straight in
    j.active=!v[kDead];
    std::memcpy(j.sent,j.vel,12);
    Elevons(j,v,s.pitch,s.roll,dt);
    JetFlames(v,j.throttle,j.throttle>0.95f,ms);
}

void Fly(PJet& j,unsigned char* v,ULONGLONG ms) noexcept {
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float dt=Measure(j,pos,ms);
    const bool wet=j.wetFrame && j.wetFrame+1>=GameFrame();   // a water message this frame or the last
    const bool driven=SeatCount(v)>0 && SeatRider(SeatAt(v,0))==Rider::player;
    if(!driven && catchFlight.v==v && !v[kDead]){AutoFly(j,v,pos,dt,ms);return;}
    if(!driven) {
        if(j.autopilot){j.autopilot=false;j.active=false;}
        if(j.driven)Leave(j,v);
        if(wet)Crash(j,v,0.0f,0.0f,false,ms,nullptr);   // empty and afloat: it breaks up
        return;
    }
    bool water=false;
    const float clear=Clear(pos,&water);
    if(!j.driven)Board(j,v,pos,clear);
    Stick s=ReadStick(SeatAt(v,0));
    SmoothStick(j,s,dt);
    j.keys=s.keys;
    Stores(j,v,s,pos);
    // The heli stays out of it: no rotor lift, no heli stick (docs/heli-input-re.md §2a).
    Put<float>(v,kInLateral,0.0f);Put<float>(v,kInForward,0.0f);Put<float>(v,kInYaw,0.0f);
    Put<float>(v,kInThrottle,0.0f);Put<float>(v,kInW,1.0f);
    Put<float>(v,kAreaInset,kNoInset);
    Blocked(j,v,pos,ms);
    if(v[kDead]){j.active=false;return;}
    Lever(j,v,s,dt);
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
    Elevons(j,v,s.pitch,s.roll,dt);
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
    ShieldBlock(v,j->vel);   // its own velocity (shield.cpp)
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

void PlayerEjectTick() noexcept { EjectTick(); }

void PreloadPlayerJets() noexcept {
    for(int i=0;i<2;++i) {
        playerJetPreloaded[i]=false;
        const auto mgr=At<void*>(image,kPreloadMgrAt);
        if(!mgr || !jet::SpawnReady() || !jet::ModFileThere(kPlayerJetFiles[i].file))continue;
        __try {
            reinterpret_cast<void(*)(void*,const wchar_t*,std::int32_t,std::int32_t)>(image+kPreloadFn)(mgr,kPlayerJetFiles[i].sgo,2,-1);
            playerJetPreloaded[i]=true;
        } __except(EXCEPTION_EXECUTE_HANDLER){}
    }
    bail=Bailout{};catchFlight=CatchFlight{};
    Log("PJET preload for the catch: fighter=%d strike=%d",playerJetPreloaded[0],playerJetPreloaded[1]);
}

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
            r.air=air;r.stall=air && j.stall;r.ground=ground;r.keys=j.keys;r.aiming=air && j.keys && j.hasAim && Cfg().playerJetMouseFlight && j.mouseFlies;
            float path[3]={j.vel[0],j.vel[1],j.vel[2]};
            if(!Normalize(path))std::memcpy(path,j.aim,12);
            for(int i=0;i<3;++i){r.aim[i]=pos[i]+j.aim[i]*kAimMark;r.path[i]=pos[i]+path[i]*kAimMark;}
            r.stores=j.stores;r.store=j.store;
            for(int i=0;i<j.stores && i<kMostStores;++i){r.storeName[i]=j.storeName[i];r.storeRounds[i]=j.storeRounds[i];}
            r.bomb=j.bomb;r.hasImpact=j.hasImpact;std::memcpy(r.impact,j.impact,12);
            r.lock=j.lock;std::memcpy(r.lockAt,j.lockAt,12);r.lockProgress=j.lockProgress;
            *out=r;
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){continue;}
    }
    return false;
}

bool IsPlayerJet(const void* vehicle) noexcept {
    __try { return KindOf(static_cast<const unsigned char*>(vehicle))!=nullptr; }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void PlayerJetFrame(unsigned char* v) noexcept {
    if(!flyOk || !Cfg().playerJet)return;
    const Kind* kind=KindOf(v);
    if(!kind || v[kDead])return;
    const ULONGLONG ms=GameMs();
    PJet* j=Find(v);
    if(!j)j=Make(v,kind);
    if(!j)return;
    if(!j->bodyFixed){FixBodyPart506(v,"PJET");j->bodyFixed=true;}   // looked up once: logged when missing
    j->frame=GameFrame();
    Fly(*j,v,ms);
}

bool InstallPlayerJets() noexcept {
    flyOk=Body506Ok();
    if(!flyOk)Log("PJET: no 506 physics hook (body506): player jets off");
    Log("HOOK player jets fly=%d water=%d die=%d bodyPart=%d",flyOk,Body506MessageOk(),Die506Ok(),BodyPartOk());
    return flyOk;
}

// A new mission (mission.cpp MissionStart): the last mission's jets are gone with it.
void ResetPlayerJets() noexcept {
    for(auto& j:jets)j=PJet{};
}
}  // namespace crew
