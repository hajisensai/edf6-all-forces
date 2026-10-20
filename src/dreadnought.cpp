// The Dreadnought (docs/dreadnought-plan.md): the Dreadnought game's heavy dreadnought, 500 m bow to stern, the player
// pilots, on the V506 heli body like the Sazabi (sazabi.cpp), told apart by its mark (body506.cpp kMarks: 7601-7699;
// tools/make_dreadnought.py writes EDF6VC_DREADNOUGHT.SGO). The plugin never crews it (crew.cpp Crew) and never flies it
// as a heli (heli.cpp); it flies it in two stages a frame:
//  - input (slot 55, after the stock step, from crew.cpp InputHook: DreadnoughtFrame): with the player in seat 0 the
//    seat's sticks are read and the stock heli input zeroed; the left stick drives it ahead and sideways, the right stick
//    or the mouse turns it, the ascend trigger lifts it and the descend key (or the right stick back) lowers it, the dash
//    key throws it on at DreadnoughtBoost times its speed. A ship of its size answers slowly: its velocity closes on the one
//    asked at DreadnoughtAccel at most, its turn at kTurnAccel. Its keel is held DreadnoughtFloor m over the highest
//    ground under it (map rays at its bow, stern, both beams and middle: Clearance). With nobody in it after someone flew
//    it, it settles on the ground under it (Settle) and holds there, upright, facing as it faces: the player walks back to
//    its door, or up onto its deck (its collision is the model's own: pylib/aircraft_collision.py);
//  - physics (506 slot 57, body506.cpp -> DreadnoughtBodyStep): that velocity, and the spin that keeps it level facing its
//    heading (body506.h BodyAttitude). Never flown (just delivered): nothing written, the stock body holds it.
// Its weapons are the stock heli's triggers on its bow gun and its missile cells (vcobjects DREADNOUGHT_WEAPON_BONES).
// Online the machine its work counts on (online_authority.h IsOnlineAuthority) flies it; the others see the stock 506's
// own replication of its body.
// Water is no hazard: the 506's ditching message is taken whole (map rays see the seabed under the sea).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "body506.h"
#include "drill_net.h"
#include "heli.h"
#include "layout.h"
#include "map.h"
#include "memory.h"
#include "online_authority.h"
#include "vecmath.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
using vec::Clamp;
constexpr float kDreadnoughtMark=7601.0f;   // pylib/vcobjects.py DREADNOUGHT_MARK (tools/selftest.py dreadnought_agrees)
constexpr std::size_t kBody=0x1650;
constexpr std::size_t kInLateral=0x1540,kInThrottle=0x1544,kInForward=0x1548,kInW=0x154C,kInYaw=0x1550;
constexpr std::size_t kAreaInset=0xE00;   // playerjet.cpp: the move-area clamp's inset
constexpr float kNoInset=-1.0e6f;
constexpr std::size_t kSeatLX=0x2C0,kSeatLY=0x2C4,kSeatRX=0x2D0,kSeatRY=0x2D4,kSeatAscend=0x2E0,kSeatPad=0x2B0,
                      kSeatButtons=0x2E8;
constexpr std::uint16_t kButtonA=0x01;
constexpr float kDeadZone=0.08f;
constexpr float kDeg=0.017453292f,kPi=3.14159265f;
// Its hull (tools/make_dreadnought.py measures the SGO's box; tools/selftest.py dreadnought_agrees holds these to it):
// the vehicle's position is its box's centre, kKeel m over its keel; the probes for the ground under it, in its frame
// (x left, z ahead), at its bow and stern kReach, its beams kBeam off its centre line.
constexpr float kKeel=53.812f,kReach=220.0f,kBeam=100.0f;
constexpr float kProbeUp=2.0f;            // m over the keel the rays start (under what it rests on)
constexpr float kSettleSpeed=8.0f;        // m/s down, nobody in it, to the ground
constexpr float kSettleGap=0.5f;          // m: settled, its keel this far over the ground (the collision takes the rest)
constexpr float kFloorGain=1.5f;          // 1/s: the keel pushed back over the floor
constexpr float kTurnAccel=10.0f;         // deg/s^2 its turn changes at most
constexpr float kAttitudeGain=2.0f,kAttitudeMost=0.6f;   // BodyAttitude: 1/s, rad/s
constexpr float kStrafeShare=0.4f;        // of its speed sideways
constexpr ULONGLONG kLogMs=1000;

struct Ship {
    ObjRef ref;
    unsigned char* vehicle=nullptr;
    bool driven=false,active=false,insetSaved=false,havePrev=false,everDriven=false;
    float savedInset=0.0f;
    ULONGLONG frame=0,lastMs=0,logAt=0;
    float prev[3]{},measured[3]{},vel[3]{},omega[3]{};
    float heading=0.0f,turn=0.0f,clear=0.0f;
};
constexpr int kMaxShips=4;
Ship ships[kMaxShips]{};
bool installed=false;

bool Live(const Ship& s) noexcept {
    const auto ctrl=static_cast<const unsigned char*>(s.ref.ctrl);
    return s.vehicle && Readable(ctrl,12) && At<std::int32_t>(ctrl,8)>0 && s.ref.Is(s.vehicle) && !(s.vehicle[0x18]&4) &&
           !s.vehicle[kDead];
}
Ship* Find(const unsigned char* v) noexcept {
    for(auto& s:ships)if(s.vehicle==v && s.ref.Is(v))return &s;
    return nullptr;
}
Ship* Make(unsigned char* v) noexcept {
    for(auto& s:ships) {
        if(s.vehicle && Live(s))continue;
        s=Ship{};s.ref=ObjRef::Of(v);s.vehicle=v;
        return &s;
    }
    static const void* refused=nullptr;
    if(refused!=v)Log("DREADNOUGHT v=%p: %d already known: not flown",v,kMaxShips);
    refused=v;
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
float Raw(const unsigned char* seat,std::size_t at) noexcept {
    const float x=At<float>(seat,at);
    return std::isfinite(x) ? Clamp(x,-1.0f,1.0f) : 0.0f;
}

// The seat's controls this frame: move (ahead, to its right, -1..1), turn (deg/s asked), climb (-1 down .. 1 up), boost.
struct Controls { float forward,right,turn,climb; bool boost; };
Controls Read(const unsigned char* seat,float dt) noexcept {
    Controls c{};
    c.forward=-Axis(seat,kSeatLY);c.right=Axis(seat,kSeatLX);
    const float a=At<float>(seat,kSeatAscend);
    const float up=std::isfinite(a) ? Clamp(a,0.0f,1.0f) : 0.0f;
    if(At<unsigned char>(seat,kSeatPad)==0) {   // the mouse's frame movement turns it; the ini's keys boost and descend
        c.turn=-Raw(seat,kSeatRX)*Cfg().dreadnoughtMouseTurn/(dt>0.0f ? dt : 1.0f/60.0f);
        c.climb=up-(KeyDown(Cfg().dreadnoughtDescendKey) ? 1.0f : 0.0f);
        c.boost=KeyDown(Cfg().dreadnoughtBoostKey);
    } else {   // a pad: the right stick turns (x) and, pulled back, lowers it (y)
        c.turn=-Axis(seat,kSeatRX)*Cfg().dreadnoughtTurn;
        c.climb=up-std::fmax(0.0f,Axis(seat,kSeatRY));
        c.boost=(At<std::uint16_t>(seat,kSeatButtons)&kButtonA)!=0;
    }
    c.turn=Clamp(c.turn,-Cfg().dreadnoughtTurn,Cfg().dreadnoughtTurn);
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

// Its keel's height over the highest ground under its five probes (kNoGround: none seen, off the map).
float Clearance(const unsigned char* v) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    const float at[5][2]={{0.0f,0.0f},{0.0f,kReach},{0.0f,-kReach},{kBeam,0.0f},{-kBeam,0.0f}};
    float best=kNoGround;
    for(const auto& q:at) {
        const float probe[3]={p[0]+m[0]*q[0]+m[8]*q[1],p[1]-kKeel+kProbeUp,p[2]+m[2]*q[0]+m[10]*q[1]};
        const float c=GroundClearance(probe);
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

// Its keel kept over DreadnoughtFloor m: the vertical velocity pushed up by the shortfall.
void Floor(Ship& s,float floor) noexcept {
    if(s.clear==kNoGround)return;
    if(s.clear<floor)s.vel[1]=std::fmax(s.vel[1],(floor-s.clear)*kFloorGain);
}

void Board(Ship& s,unsigned char* v) noexcept {
    s.driven=true;s.everDriven=true;
    if(!s.insetSaved){s.savedInset=At<float>(v,kAreaInset);s.insetSaved=true;}
    const float* f=reinterpret_cast<const float*>(v+kMatrix)+8;
    s.heading=std::atan2(f[0],f[2]);
    s.turn=0.0f;
    std::memcpy(s.vel,s.measured,12);
    Log("DREADNOUGHT v=%p boarded by the player: hp %.0f/%.0f, keel %.1f m over the ground",v,At<float>(v,kHp),At<float>(v,kHpMax),s.clear);
}

void Leave(Ship& s,unsigned char* v,bool alive,const char* how) noexcept {
    s.driven=false;
    if(s.insetSaved && alive)Put<float>(v,kAreaInset,s.savedInset);
    s.insetSaved=false;
    Log("DREADNOUGHT v=%p left: %s",v,how);
}

// The heli stays out of it (docs/heli-input-re.md §2a).
void StockInputOff(unsigned char* v) noexcept {
    Put<float>(v,kInLateral,0.0f);Put<float>(v,kInForward,0.0f);Put<float>(v,kInYaw,0.0f);
    Put<float>(v,kInThrottle,0.0f);Put<float>(v,kInW,1.0f);
}

void Hold(Ship& s,unsigned char* v) noexcept {
    const float nose[3]={std::sin(s.heading),0.0f,std::cos(s.heading)},up[3]={0.0f,1.0f,0.0f};
    BodyAttitude(v,nose,up,kAttitudeGain,kAttitudeMost,s.omega);
    StockInputOff(v);
    s.active=true;
}

// Nobody in it, after someone flew it: down onto the ground under it, and held there.
void Settle(Ship& s,unsigned char* v,float dt) noexcept {
    const float* f=reinterpret_cast<const float*>(v+kMatrix)+8;
    if(std::fabs(f[0])+std::fabs(f[2])>1e-3f)s.heading=std::atan2(f[0],f[2]);
    s.turn=0.0f;
    const float down=s.clear==kNoGround ? 0.0f : -Clamp((s.clear-kSettleGap)*kFloorGain,0.0f,kSettleSpeed);
    const float want[3]={0.0f,down,0.0f};
    Approach(s.vel,want,Cfg().dreadnoughtAccel,dt);
    Floor(s,0.0f);
    Hold(s,v);
}

void Fly(Ship& s,unsigned char* v,const Controls& c,float dt) noexcept {
    const float most=kTurnAccel*dt;
    s.turn+=Clamp(c.turn-s.turn,-most,most);
    s.heading+=s.turn*kDeg*dt;
    if(s.heading>kPi)s.heading-=2.0f*kPi;
    if(s.heading<-kPi)s.heading+=2.0f*kPi;
    const float speed=Cfg().dreadnoughtSpeed*(c.boost ? Cfg().dreadnoughtBoost : 1.0f);
    const float sh=std::sin(s.heading),ch=std::cos(s.heading);
    // ahead (sin, 0, cos); its right is -x of its frame (x is its left): (-cos, 0, sin)
    const float ahead=c.forward*speed,right=c.right*speed*kStrafeShare;
    const float want[3]={sh*ahead-ch*right,c.climb*Cfg().dreadnoughtClimb,ch*ahead+sh*right};
    Approach(s.vel,want,Cfg().dreadnoughtAccel*(c.boost ? Cfg().dreadnoughtBoost : 1.0f),dt);
    Floor(s,Cfg().dreadnoughtFloor);
    Hold(s,v);
    Put<float>(v,kAreaInset,kNoInset);   // the move area's clamp off (playerjet.cpp)
}

void Report(Ship& s,const unsigned char* v,const Controls& c,ULONGLONG ms) noexcept {
    if(!Cfg().debug || ms-s.logAt<kLogMs)return;
    s.logAt=ms;
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    Log("DREADNOUGHT v=%p %s vel=(%.1f,%.1f,%.1f) keel %.1f m heading %.0f turn %.1f pos=(%.0f,%.0f,%.0f) hp %.0f in(f %.2f r %.2f turn %.1f climb %.2f boost %d)",
        v,s.driven ? "FLOWN" : "SETTLING",s.vel[0],s.vel[1],s.vel[2],s.clear,s.heading/kDeg,s.turn,p[0],p[1],p[2],At<float>(v,kHp),
        c.forward,c.right,c.turn,c.climb,c.boost);
}

void Drive(Ship& s,unsigned char* v,ULONGLONG ms) noexcept {
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float dt=Measure(s,pos,ms);
    if(drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)) && !IsOnlineAuthority(v)) {
        s.active=false;   // another machine flies it: its body is the stock replication's
        return;
    }
    s.clear=Clearance(v);
    const bool driven=SeatCount(v)>0 && SeatRider(SeatAt(v,0))==Rider::player;
    if(s.driven && !driven)Leave(s,v,true,"got out");
    Controls c{};
    if(!driven) {
        if(s.everDriven)Settle(s,v,dt);
        Report(s,v,c,ms);
        return;
    }
    if(!s.driven)Board(s,v);
    c=Read(SeatAt(v,0),dt);
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
        return;
    }
    if(!s && (s=Make(v))==nullptr)return;
    s->frame=GameFrame();
    Drive(*s,v,GameMs());
}

// The 506 physics step (body506.cpp), after the stock one: its velocity, the spin that keeps it level.
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

bool InstallDreadnought() noexcept {
    installed=Body506Ok();
    Log("HOOK dreadnought body=%d (mark %.0f)",installed,kDreadnoughtMark);
    return installed;
}

void ResetDreadnought() noexcept {
    for(auto& s:ships)s=Ship{};
}
}  // namespace crew
