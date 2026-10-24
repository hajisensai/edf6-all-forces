// The Dreadnought (docs/dreadnought-plan.md, docs/feedback-2026-10-21-dreadnought.md): the Dreadnought game's heavy dreadnought the
// player commands, on the V506 heli body like the Sazabi (sazabi.cpp), told apart by its mark (body506.cpp kMarks:
// 7601-7699; tools/make_dreadnought.py writes EDF6VC_DREADNOUGHT.SGO). The plugin never crews it (crew.cpp Crew) and never
// flies it as a heli (heli.cpp); it flies it in two stages a frame:
//  - input (slot 55, after the stock step, from crew.cpp InputHook: DreadnoughtFrame): with the player in seat 0 (its
//    bridge) the seat's sticks are read and the stock heli input zeroed. Flown as the Dreadnought game's ships are (the
//    user, 2026-10-21: 「移动之类的你好好写一下…这好歹是无畏舰欸」): the left stick ahead / back (W / S) sets its engines'
//    throttle, which stays where it is set (Ship::throttle, reverse to kReverse of its way ahead), the left stick across
//    (A / D) turns it, the ascend trigger (Space / LT) lifts it and the descend key (Ctrl / LB) lowers it, the strafe keys
//    (Q / E) push it sideways, the boost (Shift / A) throws its engines on at DreadnoughtBoost while its boost lasts
//    (kBoostSec, back over kBoostBackSec). Its mass answers slowly: its velocity closes on the one asked at DreadnoughtAccel
//    at most (as a whole: carried round a turn it drifts), its turn at kTurnAccel; it leans into its turns and dips its
//    bow as it gathers way (Attitude: a few degrees, its deck stays walkable). The view is free of its heading (the
//    mouse / right stick, DreadnoughtView): the screen's centre is where its turrets nobody mans train and fire on the
//    primary trigger (dreadnought_turrets.inc). Its keel is held DreadnoughtFloor m over the highest ground under it (map
//    rays at its bow, stern, both beams and middle: Clearance). With nobody on its bridge after someone flew it, it
//    settles on the ground under it (Settle) and holds there, upright, facing as it faces;
//  - physics (506 slot 57, body506.cpp -> DreadnoughtBodyStep): that velocity, and the spin that holds its attitude
//    (body506.h BodyAttitude). Never flown (just delivered): nothing written, the stock body holds it.
// The stock heli's guns and missiles are never fired (its fire bytes cleared each frame: its weapons are its turrets);
// its seats are its bridge and its turrets', moved between with seatswitch.cpp's keys; its HUD is hud.cpp DreadnoughtHud
// (DreadnoughtCue); its hangar launches and takes back its fighters, and a player leaving any of its seats stands on its
// bridge (dreadnought_hangar.inc).
// Online the machine its work counts on (online_authority.h IsOnlineAuthority) flies it; the others see the stock 506's
// own replication of its body.
// Water is no hazard: the 506's ditching message is taken whole (map rays see the seabed under the sea).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "body506.h"
#include "dreadnought_deck.h"
#include "dreadnought_net.h"
#include "drill_net.h"
#include "firstperson.h"
#include "seat_net_transport.h"
#include "heli.h"
#include "layout.h"
#include "map.h"
#include "memory.h"
#include "online_authority.h"
#include "turretcam.h"
#include "vecmath.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
using vec::Clamp;
constexpr float kDreadnoughtMark=7601.0f;   // pylib/vcobjects.py DREADNOUGHT_MARK (tools/selftest.py dreadnought_agrees)
constexpr float kDnFighterMark=7041.0f;     // its first fighter's (jet_internal.h kBodies Body::dnFighter, then one a kind)
constexpr std::size_t kBody=0x1650;
constexpr std::size_t kContact=0x1580;   // heli.cpp: bit 2, the body rests on something
constexpr unsigned char kContactGround=2;
constexpr std::size_t kInLateral=0x1540,kInThrottle=0x1544,kInForward=0x1548,kInW=0x154C,kInYaw=0x1550;
constexpr std::size_t kFireGun=0x2020,kFireMissile=0x2021;   // the 506's fire bytes (heli.cpp)
constexpr std::size_t kAreaInset=0xE00;   // playerjet.cpp: the move-area clamp's inset
constexpr float kNoInset=-1.0e6f;
constexpr std::size_t kSeatLX=0x2C0,kSeatLY=0x2C4,kSeatRX=0x2D0,kSeatRY=0x2D4,kSeatAscend=0x2E0,kSeatPad=0x2B0,
                      kSeatButtons=0x2E8;
constexpr float kDeadZone=0.08f;
constexpr float kDeg=0.017453292f,kPi=3.14159265f;
// Its hull (tools/make_dreadnought.py measures the SGO's box; tools/selftest.py dreadnought_agrees holds these to it):
// the vehicle's position is its box's centre, kKeel m over its keel; the probes for the ground under it, in its frame
// (x left, z ahead), at its bow and stern kReach, its beams kBeam off its centre line. kLength: its bow to stern
// (tools/prep_dreadnought.py SHIP_LENGTH: its fighters a fighter's size, the user 2026-10-21 「无畏舰太小了…飞机也小了」).
constexpr float kLength=500.0f;
constexpr float kKeel=55.25f,kReach=0.44f*kLength,kBeam=0.2f*kLength;   // kKeel: pylib/vcobjects.py DREADNOUGHT_KEEL
constexpr float kProbeUp=2.0f;            // m over the keel the rays start (under what it rests on)
constexpr float kSettleSpeed=8.0f;        // m/s down, nobody in it, to the ground
constexpr float kSettleGap=0.5f;          // m: settled, its keel this far over the ground (the collision takes the rest)
constexpr float kFloorGain=1.5f;          // 1/s: the keel pushed back over the floor
constexpr float kHoldGain=0.8f;           // 1/s: no climb asked, its height held (the solver's gravity after the step
                                          // takes ~g dt off the velocity written: it sank 0.15 m/s, range 2026-10-21)
constexpr float kTurnAccel=8.0f;          // deg/s^2 its turn changes at most
constexpr float kThrottleRate=0.5f;       // of its full way ahead a second, the throttle stick held
constexpr float kReverse=0.35f;           // its throttle astern at most (of its way ahead)
constexpr float kThrottleStop=0.04f;      // let go this near stop, the throttle is stop
constexpr float kStrafeShare=0.35f;       // of its speed sideways
constexpr float kBoostSec=8.0f,kBoostBackSec=16.0f;   // s its boost lasts, and comes back over from empty
constexpr float kAttitudeGain=1.6f,kAttitudeMost=0.5f;   // BodyAttitude: 1/s, rad/s
constexpr float kLeanMost=4.0f*kDeg;      // rad its lean at a full turn at full way ahead
constexpr float kDipMost=1.5f*kDeg;       // rad its bow's dip (or lift) at its full acceleration (braking)
constexpr float kDipEase=2.0f;            // 1/s the measured acceleration it dips by is eased at
constexpr ULONGLONG kLogMs=1000;

struct Ship {
    ObjRef ref;
    unsigned char* vehicle=nullptr;
    bool driven=false,active=false,insetSaved=false,havePrev=false,everDriven=false;
    bool flown=false;      // a player has flown it: left empty, it holds where it is (Drive), never down onto the ground
    float savedInset=0.0f;
    ULONGLONG frame=0,lastMs=0,logAt=0;
    float prev[3]{},measured[3]{},vel[3]{},omega[3]{};
    float heading=0.0f,turn=0.0f,clear=0.0f;
    float throttle=0.0f;   // -kReverse .. 1: its engines' setting, kept between frames
    float boost=1.0f;      // 0 .. 1: the boost it has left
    bool boosting=false;
    float ahead=0.0f,dip=0.0f;   // its way ahead (m/s) last frame, and its eased acceleration along it (m/s^2)
    bool holding=false;    // holdY: the height (its position's y) it keeps while no climb is asked
    float holdY=0.0f;
};
constexpr int kMaxShips=4;
Ship ships[kMaxShips]{};
bool installed=false;
bool netReady=false;   // the turrets' blocks (dreadnought_net.h) go over seat_net

bool Live(const Ship& s) noexcept {
    const auto ctrl=static_cast<const unsigned char*>(s.ref.ctrl);
    return s.vehicle && Readable(ctrl,12) && At<std::int32_t>(ctrl,8)>0 && s.ref.Is(s.vehicle) && !(s.vehicle[0x18]&4) &&
           !s.vehicle[kDead];
}
Ship* Find(const unsigned char* v) noexcept {
    for(auto& s:ships)if(s.vehicle==v && s.ref.Is(v))return &s;
    return nullptr;
}
bool KeyDown(int vk) noexcept {   // playerjet.cpp KeyDown
    if(vk<=0 || MapHoldsKeys())return false;
    DWORD pid=0;
    GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (edf::AsyncKey(vk)&0x8000)!=0;
}
float Axis(const unsigned char* seat,std::size_t at) noexcept {
    const float x=At<float>(seat,at);
    if(!std::isfinite(x) || std::fabs(x)<kDeadZone)return 0.0f;
    return Clamp(x,-1.0f,1.0f);
}
bool Pad(const unsigned char* seat) noexcept { return At<unsigned char>(seat,kSeatPad)!=0; }
bool Button(const unsigned char* seat,int bits) noexcept {
    return bits && (At<std::uint16_t>(seat,kSeatButtons)&static_cast<std::uint16_t>(bits))!=0;
}

// The bridge's controls this frame: the throttle stick (-1 astern .. 1 ahead), the turn (deg/s asked, left positive), the
// strafe (-1 left .. 1 right), the climb (-1 down .. 1 up), the boost.
struct Controls { float thrust,turn,strafe,climb; bool boost; };
Controls Read(const unsigned char* seat) noexcept {
    Controls c{};
    c.thrust=-Axis(seat,kSeatLY);
    c.turn=-Axis(seat,kSeatLX)*Cfg().dreadnoughtTurn;
    const float a=At<float>(seat,kSeatAscend);
    const float up=std::isfinite(a) ? Clamp(a,0.0f,1.0f) : 0.0f;
    if(!Pad(seat)) {
        c.climb=up-(KeyDown(Cfg().dreadnoughtDescendKey) ? 1.0f : 0.0f);
        c.strafe=(KeyDown(Cfg().dreadnoughtStrafeRightKey) ? 1.0f : 0.0f)-(KeyDown(Cfg().dreadnoughtStrafeLeftKey) ? 1.0f : 0.0f);
        c.boost=KeyDown(Cfg().dreadnoughtBoostKey);
    } else {
        c.climb=up-(Button(seat,Cfg().dreadnoughtDescendButton) ? 1.0f : 0.0f);
        c.boost=Button(seat,Cfg().dreadnoughtBoostButton);
    }
    return c;
}

float Measure(Ship& s,const float* pos,ULONGLONG ms) noexcept {
    const ULONGLONG since=s.lastMs ? ms-s.lastMs : 0;
    s.lastMs=ms;
    const float dt=GameStep(since);
    if(s.havePrev && since>0)for(int i=0;i<3;++i)s.measured[i]=(pos[i]-s.prev[i])/dt;
    std::memcpy(s.prev,pos,12);s.havePrev=true;
    return dt;
}

// The height of `p` over the map under it, any moving body passed (a ship over another's deck, a fighter on its own):
// GroundClearance's (body506.cpp) on MapFixedRay.
float FixedClearance(const float* p) noexcept {
    const float down[3]={p[0],p[1]-3000.0f,p[2]};
    float hit[3];
    if(MapFixedRay(p,down,hit)>=0.0f)return p[1]-hit[1];
    const float top[3]={p[0],p[1]+600.0f,p[2]};
    return MapFixedRay(top,p,hit)>=0.0f ? p[1]-hit[1] : kNoGround;
}

// Its keel's height over the highest ground under its five probes (kNoGround: none seen, off the map).
float Clearance(const unsigned char* v) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    const float at[5][2]={{0.0f,0.0f},{0.0f,kReach},{0.0f,-kReach},{kBeam,0.0f},{-kBeam,0.0f}};
    float best=kNoGround;
    for(const auto& q:at) {
        const float probe[3]={p[0]+m[0]*q[0]+m[8]*q[1],p[1]-kKeel+kProbeUp,p[2]+m[2]*q[0]+m[10]*q[1]};
        const float c=FixedClearance(probe);
        if(c!=kNoGround && (best==kNoGround || c-kProbeUp<best))best=c-kProbeUp;
    }
    return best;
}

// The velocity closes on `want` at `accel` m/s^2 at most (as a whole: it turns and slows together).
void Approach(float* vel,const float* want,float accel,float dt) noexcept {
    float d[3]={want[0]-vel[0],want[1]-vel[1],want[2]-vel[2]};
    const float len=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]),most=accel*dt;
    const float k=len>most && len>0.0f ? most/len : 1.0f;
    for(int i=0;i<3;++i)vel[i]+=d[i]*k;
}

// Its keel kept over DreadnoughtFloor m: the vertical velocity pushed up by the shortfall, at most what `accel` brings to
// rest on the floor (sqrt(2 a d)). Set whole, a keel 25 m low went up at 37.7 m/s and, braked at 4 m/s^2, stopped 160 m
// over the floor (range 2026-10-21).
void Floor(Ship& s,float floor,float accel) noexcept {
    if(s.clear==kNoGround || s.clear>=floor)return;
    const float d=floor-s.clear;
    s.vel[1]=std::fmax(s.vel[1],std::fmin(d*kFloorGain,std::sqrt(2.0f*accel*d)));
}

// No climb asked: a height kept against the solver's gravity. Let go climbing or sinking, the height it comes to rest at
// braking at `accel` (latched there, it had braked past the height let go and been pulled back 17 m); under the floor, the
// floor's. Toward it no faster than `accel` stops it there (sqrt(2 a d): 0.8 d had overshot the floor 11 m).
float HoldClimb(Ship& s,const unsigned char* v,float climb,float floor,float accel) noexcept {
    const float y=reinterpret_cast<const float*>(v+kPosition)[1];
    if(climb!=0.0f){s.holding=false;return climb*Cfg().dreadnoughtClimb;}
    if(!s.holding){s.holdY=y+s.vel[1]*std::fabs(s.vel[1])/(2.0f*accel);s.holding=true;}
    if(s.clear!=kNoGround && s.clear<floor)s.holdY=std::fmax(s.holdY,y+floor-s.clear);
    const float gap=s.holdY-y,most=std::fmin(Cfg().dreadnoughtClimb,std::sqrt(2.0f*accel*std::fabs(gap)));
    return Clamp(gap*kHoldGain,-most,most);
}

void Board(Ship& s,unsigned char* v) noexcept {
    s.driven=true;s.everDriven=true;s.flown=true;
    if(!s.insetSaved){s.savedInset=At<float>(v,kAreaInset);s.insetSaved=true;}
    const float* f=reinterpret_cast<const float*>(v+kMatrix)+8;
    s.heading=std::atan2(f[0],f[2]);
    s.turn=0.0f;s.throttle=0.0f;s.dip=0.0f;
    s.holding=false;
    std::memcpy(s.vel,s.measured,12);
    s.ahead=s.vel[0]*f[0]+s.vel[2]*f[2];
    Log("DREADNOUGHT v=%p boarded by the player: hp %.0f/%.0f, keel %.1f m over the ground",v,At<float>(v,kHp),At<float>(v,kHpMax),s.clear);
}

void Leave(Ship& s,unsigned char* v,bool alive,const char* how) noexcept {
    s.driven=false;s.boosting=false;
    if(s.insetSaved && alive)Put<float>(v,kAreaInset,s.savedInset);
    s.insetSaved=false;
    Log("DREADNOUGHT v=%p left: %s",v,how);
}

// The heli stays out of it (docs/heli-input-re.md §2a), and its own guns and missiles never fire: its turrets are its
// weapons (the user, 2026-10-21: 「为什么还有原版的油料和机炮」).
void StockInputOff(unsigned char* v) noexcept {
    Put<float>(v,kInLateral,0.0f);Put<float>(v,kInForward,0.0f);Put<float>(v,kInYaw,0.0f);
    Put<float>(v,kInThrottle,0.0f);Put<float>(v,kInW,1.0f);
    v[kFireGun]=0;v[kFireMissile]=0;
}

// Its attitude: facing its heading, leaning `lean` rad into its turn (left down positive) and its bow down `dip` rad.
void Attitude(Ship& s,unsigned char* v,float lean,float dip) noexcept {
    const float sh=std::sin(s.heading),ch=std::cos(s.heading);
    const float f[3]={sh,0.0f,ch},r[3]={-ch,0.0f,sh};
    const float cl=std::cos(lean),sl=std::sin(lean),cd=std::cos(dip),sd=std::sin(dip);
    float up[3],nose[3];
    for(int i=0;i<3;++i) {
        const float u=(i==1 ? cl : 0.0f)-r[i]*sl;   // up leaned toward its left side
        nose[i]=f[i]*cd-u*sd;
        up[i]=u*cd+f[i]*sd;
    }
    BodyAttitude(v,nose,up,kAttitudeGain,kAttitudeMost,s.omega);
    StockInputOff(v);
    s.active=true;
}

// Nobody on its bridge, never flown (as it came): down onto the ground under it, and held there.
void Settle(Ship& s,unsigned char* v,float dt) noexcept {
    const float* f=reinterpret_cast<const float*>(v+kMatrix)+8;
    if(std::fabs(f[0])+std::fabs(f[2])>1e-3f)s.heading=std::atan2(f[0],f[2]);
    s.turn=0.0f;s.throttle=0.0f;
    // Resting on something (the contact bit), it is down: pushed on, it had kept -6 m/s into the ground 4.6 m under its
    // keel's probes (range 2026-10-21).
    const bool resting=(v[kContact]&kContactGround)!=0;
    const float down=s.clear==kNoGround || resting ? 0.0f : -Clamp((s.clear-kSettleGap)*kFloorGain,0.0f,kSettleSpeed);
    const float want[3]={0.0f,down,0.0f};
    Approach(s.vel,want,Cfg().dreadnoughtAccel,dt);
    if(resting)s.vel[1]=std::fmax(s.vel[1],0.0f);
    Floor(s,0.0f,Cfg().dreadnoughtAccel);
    s.holding=false;
    Attitude(s,v,0.0f,0.0f);
}

void Fly(Ship& s,unsigned char* v,const Controls& c,float dt) noexcept {
    // the throttle: moved by the stick, kept where it is let go (stop when let go near it)
    if(c.thrust!=0.0f)s.throttle=Clamp(s.throttle+c.thrust*kThrottleRate*dt,-kReverse,1.0f);
    else if(std::fabs(s.throttle)<kThrottleStop)s.throttle=0.0f;
    // the boost: while held, ahead and left, and only its engines' way ahead
    s.boosting=c.boost && s.boost>0.0f && s.throttle>0.0f;
    s.boost=s.boosting ? std::fmax(0.0f,s.boost-dt/kBoostSec) : std::fmin(1.0f,s.boost+dt/kBoostBackSec);
    const float most=kTurnAccel*dt;
    s.turn+=Clamp(Clamp(c.turn,-Cfg().dreadnoughtTurn,Cfg().dreadnoughtTurn)-s.turn,-most,most);
    s.heading+=s.turn*kDeg*dt;
    if(s.heading>kPi)s.heading-=2.0f*kPi;
    if(s.heading<-kPi)s.heading+=2.0f*kPi;
    const float boost=s.boosting ? Cfg().dreadnoughtBoost : 1.0f;
    const float speed=Cfg().dreadnoughtSpeed;
    const float sh=std::sin(s.heading),ch=std::cos(s.heading);
    // ahead (sin, 0, cos); its right is -x of its frame (x is its left): (-cos, 0, sin)
    const float ahead=s.throttle*speed*boost,right=c.strafe*speed*kStrafeShare;
    const float accel=Cfg().dreadnoughtAccel*boost;
    const float want[3]={sh*ahead-ch*right,HoldClimb(s,v,c.climb,Cfg().dreadnoughtFloor,accel),ch*ahead+sh*right};
    Approach(s.vel,want,accel,dt);
    Floor(s,Cfg().dreadnoughtFloor,accel);
    // its lean into the turn (as fast as it goes), its bow's dip as it gathers way (eased: the measured change is rough)
    const float way=s.vel[0]*sh+s.vel[2]*ch;
    if(dt>0.0f)s.dip+=((way-s.ahead)/dt-s.dip)*std::fmin(1.0f,kDipEase*dt);
    s.ahead=way;
    const float pace=Clamp(std::fabs(way)/speed,0.0f,1.0f);
    Attitude(s,v,kLeanMost*Clamp(s.turn/Cfg().dreadnoughtTurn,-1.0f,1.0f)*pace,
             kDipMost*Clamp(s.dip/Cfg().dreadnoughtAccel,-1.0f,1.0f));
    Put<float>(v,kAreaInset,kNoInset);   // the move area's clamp off (playerjet.cpp)
}

void Report(Ship& s,const unsigned char* v,const Controls& c,ULONGLONG ms) noexcept {
    if(!Cfg().debug || ms-s.logAt<kLogMs)return;
    s.logAt=ms;
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    Log("DREADNOUGHT v=%p %s vel=(%.1f,%.1f,%.1f) keel %.1f m heading %.0f turn %.1f throttle %.2f boost %.2f%s pos=(%.0f,%.0f,%.0f) hp %.0f in(thrust %.2f strafe %.2f turn %.1f climb %.2f)",
        v,s.driven ? "FLOWN" : s.flown ? "HOLDING" : "SETTLING",s.vel[0],s.vel[1],s.vel[2],s.clear,s.heading/kDeg,s.turn,s.throttle,s.boost,
        s.boosting ? " (on)" : "",p[0],p[1],p[2],At<float>(v,kHp),c.thrust,c.strafe,c.turn,c.climb);
}

#include "dreadnought_turrets.inc"
#include "dreadnought_hangar.inc"

// Slot `i` new: everything kept per ship (its Ship, its turrets' rigs, remote gunners and network gate, its hangars'
// rig, fitting, bays, escorts and sorties, its hull's bodies in the bullets' hook) goes together. Reused for another ship, a slot
// had kept the last one's turrets and hangar (review 2026-10-24: the rigs' `bones==... && ok` would take the dead
// ship's bone records); every array indexed by slot is reset here alone (tests/dreadnought_slot_guard.py).
void ResetSlot(int i) noexcept {
    ships[i]=Ship{};
    turrets[i]=Turrets{};
    hangars[i]=Hangar{};
    HullBodies(i,nullptr);
}

// A ship taken on: the first slot free (never used, or its ship gone), new (ResetSlot).
Ship* Take(unsigned char* v) noexcept {
    for(int i=0;i<kMaxShips;++i) {
        if(ships[i].vehicle && Live(ships[i]))continue;
        ResetSlot(i);
        Ship* const s=&ships[i];
        s->ref=ObjRef::Of(v);s->vehicle=v;
        // Held from its first frame (Settle: on the ground under it): its body is its mesh compound (body506.cpp
        // MakeAirframe), which, delivered a little into the ground, fell 270 m through the map left to the stock step
        // (range 2026-10-21); the box it had been had been pushed out.
        s->everDriven=true;
        return s;
    }
    static const void* refused=nullptr;
    if(refused!=v)Log("DREADNOUGHT v=%p: %d already known: not flown",v,kMaxShips);
    refused=v;
    return nullptr;
}

void Drive(Ship& s,unsigned char* v,ULONGLONG ms) noexcept {
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float dt=Measure(s,pos,ms);
    const bool driven=SeatCount(v)>0 && SeatRider(SeatAt(v,0))==Rider::player;
    // the bridge's view and the turrets: on every machine (each local player's seat is theirs)
    TurretsFrame(s,v,dt,ms);
    HangarFrame(s,v,dt,ms);
    if(drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)) && !IsOnlineAuthority(v)) {
        s.active=false;   // another machine flies it: its body is the stock replication's
        v[kFireGun]=0;v[kFireMissile]=0;
        return;
    }
    s.clear=Clearance(v);
    if(s.driven && !driven) {
        Leave(s,v,true,"got out");
        // Nobody left in its seats: it stops where it is, its way off at once (the game's ground does not carry a soldier
        // standing on a moving hull: put on its bridge, the player slid off a ship still making way; range 2026-10-21).
        bool anyone=false;
        for(unsigned i=0;i<SeatCount(v);++i)anyone=anyone || SeatRider(SeatAt(v,i))==Rider::player;
        if(!anyone){s.vel[0]=0.0f;s.vel[2]=0.0f;s.turn=0.0f;}
    }
    Controls c{};
    // Its commander gone to a gun (any player still in one of its seats): it cruises on as left, its height held.
    bool crewed=false;
    for(unsigned i=1;i<SeatCount(v);++i)crewed=crewed || SeatRider(SeatAt(v,i))==Rider::player;
    if(!driven && crewed && s.everDriven) {
        Fly(s,v,c,dt);
        Report(s,v,c,ms);
        return;
    }
    if(!driven && s.flown) {
        // Nobody aboard, after a player flew it: it holds where it was left, its height kept (Fly, nothing asked), its way
        // off: a soldier stands on a still deck. It had gone down onto the ground at kSettleSpeed, and the soldiers on its
        // deck fell after it, landing and falling again (the user, 2026-10-21: 「这个无畏舰站不住，站上去会跳起来」: got out
        // 470 m up, on its deck 14.7-20.5 m over its centre as it went down at 8 m/s).
        s.throttle=0.0f;
        Fly(s,v,c,dt);
        Report(s,v,c,ms);
        return;
    }
    if(!driven) {
        if(s.everDriven)Settle(s,v,dt);
        else{v[kFireGun]=0;v[kFireMissile]=0;}
        Report(s,v,c,ms);
        return;
    }
    if(!s.driven)Board(s,v);
    c=Read(SeatAt(v,0));
    HangarKey(s,v,SeatAt(v,0));
    Fly(s,v,c,dt);
    Report(s,v,c,ms);
}
}  // namespace

bool IsDreadnought(const void* vehicle) noexcept { return BodyOf(vehicle)==PluginBody::dreadnought; }

void DreadnoughtFrame(unsigned char* v) noexcept {
    if(!installed || !Cfg().dreadnought || !IsDreadnought(v))return;
    Ship* s=Find(v);
    if(v[kDead]) {
        if(s && s->driven)Leave(*s,v,false,"destroyed");
        if(s)HullBodies(static_cast<int>(s-ships),nullptr);
        return;
    }
    if(!s && (s=Take(v))==nullptr)return;
    s->frame=GameFrame();
    HullBodies(static_cast<int>(s-ships),v);   // its turrets' rounds through its own hull (jet_hooks.cpp)
    Drive(*s,v,GameMs());
    Publish(*s,v);           // the local player's panel, in any of its seats
    WatchPlayer(GameMs());   // once a frame: the local player getting out onto its bridge (of its seats, or of a fighter brought home)
}

void DreadnoughtInput(unsigned char* v) noexcept {
    if(!installed || !Cfg().dreadnought || !IsDreadnought(v) || v[kDead])return;
    const int here=LocalSeat(v);
    if(here<0)return;
    const unsigned char* const seat=SeatAt(v,static_cast<unsigned>(here));
    aimIn=AimIn{seat,GameFrame(),At<float>(seat,kSeatAimStick),At<float>(seat,kSeatAimStick+4)};
}

// The 506 physics step (body506.cpp), after the stock one: its velocity, the spin that holds its attitude.
bool DreadnoughtBodyStep(unsigned char* v,float* lin,float* ang) noexcept {
    if(!Cfg().enabled || !Cfg().dreadnought)return false;
    if(drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)) && !IsOnlineAuthority(v))return false;
    const Ship* s=Find(v);
    if(!s || !s->active || !(s->driven || s->everDriven) || v[kDead] || s->frame+1<GameFrame() || !At<void*>(v,kBody))
        return false;
    for(int i=0;i<3;++i){lin[i]=s->vel[i];ang[i]=s->omega[i];}
    return true;
}

// The 506's messages to it: the water is no ditching for a ship (taken whole).
bool DreadnoughtMessage(unsigned char* v,std::uint32_t msg,void*,MessageRestore*) noexcept {
    if(!Cfg().enabled || !Cfg().dreadnought)return false;
    (void)v;
    return msg==kMsgWater;
}

// A turret block from another machine (dreadnought_net.h): the turrets its players train turn, and fire of no damage,
// here.
void ReceivedTurrets(unsigned char* v,const void* block,std::size_t size) noexcept {
    dreadnought_net::State s;
    if(!installed || !Cfg().enabled || !Cfg().dreadnought || !InSession() || !IsDreadnought(v) || v[kDead] ||
       !dreadnought_net::Decode(block,size,s))return;
    Ship* sh=Find(v);
    if(!sh && (sh=Take(v))==nullptr)return;
    Turrets& r=TurretsOf(*sh);
    if(!r.gate.Admit(s.head,true,false,-1))return;
    const ULONGLONG ms=GameMs();
    for(int n=0;n<kTurrets;++n) {
        if(!(s.manned&(1u<<n)))continue;
        Remote& rm=r.remote[n];
        rm.yaw=s.yaw[n];rm.pitch=s.pitch[n];rm.at=ms;
        if(s.fired&(1u<<n)){rm.fire=true;std::memcpy(rm.aim,s.aim[n],12);}
    }
}

bool InstallDreadnought() noexcept {
    installed=Body506Ok();
    netReady=installed && seat_net::Install() && seat_net::Register(dreadnought_net::kMagic,&ReceivedTurrets) &&
             seat_net::ChainSlot(0x17DB4C8,"Dreadnought (506)");   // the 506's NetworkObject slot 17 (sazabi_net.cpp's)
    Log("HOOK dreadnought body=%d network=%d (mark %.0f)",installed,netReady,kDreadnoughtMark);
    return installed;
}

void ResetDreadnought() noexcept {
    for(int i=0;i<kMaxShips;++i)ResetSlot(i);
    ResetBridge();
    AcquireSRWLockExclusive(&viewLock);view=View{};ReleaseSRWLockExclusive(&viewLock);
    sightFrame=0;cueFrame=0;aimIn=AimIn{};
}

// The camera hook's side of the local player's view aboard (dreadnought_turrets.inc Look): theirs, from its bridge or
// their turret, or first person from the seat's eye.
bool DreadnoughtCamera(const unsigned char* seat,float* eye,float* look) noexcept {
    AcquireSRWLockShared(&viewLock);const View w=view;ReleaseSRWLockShared(&viewLock);
    if(!Cfg().enabled || !Cfg().dreadnought || !w.seat || seat!=w.seat || w.frame+2<GameFrame() ||
       !w.ref.Is(w.vehicle) || w.vehicle[kDead] || SeatRider(seat)!=Rider::player ||
       At<const void*>(seat,kSeatRider)!=PlayerHuman())return false;
    return ViewPlace(w,eye,look);
}

// A board press of `human` on foot visiting Dreadnought `v` (crew.cpp FindSeatHook): the local player standing over one of
// its fitted hangars has a fighter of that hangar brought out to its bay's mouth for them (dreadnought_hangar.inc
// HangarBoard). True: the press was over one of its hangars (no seat of the ship's for it).
bool DreadnoughtHangarBoard(unsigned char* v,unsigned char* human) noexcept {
    if(!installed || !Cfg().enabled || !Cfg().dreadnought || !v || !human || !IsDreadnought(v) || v[kDead] ||
       human!=PlayerHuman() || human[kDead] || !HumanOnFoot(human))return false;
    Ship* const s=Find(v);
    if(!s || !Live(*s))return false;
    return HangarBoard(*s,v,human);
}

// Whether the pilot getting out of `fighter` now stands on its ship (no parachute, no catch: playerjet.cpp Leave,
// playerjet_board.inc Left). Only a fighter a hangar running here holds at its own bay's mouth while it puts its pilot
// out onto the bridge (BayJob::alight: brought home), on the spot and moving with the ship (dreadnought_deck.h
// StandsAboard); anywhere else, over the hull's box too, they eject as from any jet.
bool DreadnoughtTakesBack(const unsigned char* fighter) noexcept {
    if(!installed || !Cfg().enabled || !Cfg().dreadnought || !IsDnFighter(fighter) || fighter[kDead])return false;
    for(auto& s:ships) {
        if(!s.vehicle || !Live(s))continue;
        const Hangar& h=HangarOf(s);
        if(!h.ok)continue;
        for(int i=0;i<kBays;++i) {
            const Bay& b=h.bay[i];
            if(!b.fitted || !b.jet.Is(fighter))continue;
            const float* mouth=BoneAt(b.mouth);
            const float* p=reinterpret_cast<const float*>(fighter+kPosition);
            const float d[3]={mouth[0]-p[0],mouth[1]-p[1],mouth[2]-p[2]};
            auto* const f=const_cast<unsigned char*>(fighter);
            const float rel=Relative(f,s.measured);
            const bool alighting=b.job==BayJob::alight && kStandsOnHull;
            const bool aboard=dreadnought::StandsAboard(h.authority,alighting,vec::Len(d),rel);
            Log("DREADNOUGHT v=%p: its pilot gets out of bay %d's %s (%s), %.1f m from its mouth, %.1f m/s over the ship's: %s",
                s.vehicle,i,kKindNames[KindOfBay(i)],alighting ? "brought home" : "not being taken in",vec::Len(d),rel,
                aboard ? "aboard (no parachute)" : "not put aboard (ejects)");
            return aboard;
        }
    }
    return false;
}

// The local player's sight aboard, fresh this frame (hud.cpp GunnerMarks).
bool DreadnoughtGunnerHud(GunnerReadout* out) noexcept {
    if(!out || !sightFrame || sightFrame+1<GameFrame())return false;
    *out=sight;
    return true;
}

// The local player's ship readout, fresh this frame (hud.cpp DreadnoughtHud).
bool PlayerDreadnoughtCue(DreadnoughtCue* out) noexcept {
    if(!out || !cueFrame || cueFrame+1<GameFrame())return false;
    *out=cue;
    return true;
}
}  // namespace crew
