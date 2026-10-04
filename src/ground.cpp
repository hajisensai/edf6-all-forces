// Depth Crawler driver (Vehicle502_GroundRobo). EDF.dll has no AI for it (docs/ground-ai-re.md): it is a
// VehicleBase with the 54-slot base vtable, so it has neither the CarBase AI action (slot 72, 0x661440,
// which tanks and the Grape run under Vehicle_RideAi) nor anything else that reads an AI target. Its
// controls come only from seat 0's stick block, which only a human with a pad writes; under the stock NPC
// rider (DummyVehicleRider) the block stays zero and the crawler stands still.
//
// So for a crawler with an NPC in seat 0, after its stock pre-update (slot 4, 0x612D20) has copied the stick
// block into the vehicle's own input block, we overwrite that block the way a pad would have filled it:
//   veh+0x15F0 vec4 move  (x along matrix row 0 "right", z along row 2 "forward", w 1.0)
//   veh+0x1600 vec4 look  (x: aim pitch, +0.02 rad/frame per unit into veh+0x1B10; y: body turn rate; w 1.0)
//   byte veh+0x1611+i     trigger of weapon holder i (0 gatling, 1 left arm, 2 right arm)
//   byte veh+0x1610 jump, veh+0x1614 dash: left 0
// Slot 5 (0x614510) consumes them later the same frame. The move works on the vehicle's own rows, so its
// sign is right by construction; the world sign of the turn and of the aim pitch are learned online from
// how the heading and the gun barrel actually move (as heli.cpp does for yaw and the 410 door guns).
//
// Behaviour: no enemy: it follows the player and stops Cfg().groundFollow metres from them. An enemy within
// Cfg().groundRange of it (and not far past the leash round the player): it turns its body and arms onto the
// enemy's lock point, closes to about 70% of its guns' reach, and fires each gun whose own barrel is on
// the target, with a clear map ray and the player not in the line. It never goes more than
// Cfg().groundLeash metres from the player while it has one to follow.
// Per crawler the module keeps a Robo, keyed by its ObjRef (a new object at an old address is a new crawler),
// dropped at a new mission (ResetGround) and reused only once its crawler has not been driven for kStaleMs:
// a full table drives no new crawler rather than drop a live one.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include <cmath>

namespace crew {
namespace {
// Input block written by slot 4 (0x612D20) and read by slot 5 (0x614510)
constexpr std::size_t kMove=0x15F0,kLook=0x1600,kJump=0x1610,kFire=0x1611,kDash=0x1614;
constexpr std::size_t kAimPitch=0x1B10;      // rad, clamped to +-pi/2, += look.x * kPitchPerInput a frame
constexpr float kPitchPerInput=0.02f;        // 0x614D16
// Slot 5 pulls the weapon holders (layout.h kHolders) 0..2.
constexpr int kGuns=3;
constexpr float kPi=3.14159265f;
constexpr float kDefaultReach=150.0f;  // m: a gun whose round's reach cannot be read
constexpr float kMaxReach=400.0f;      // m: never shoot past this
constexpr float kMinStandoff=25.0f;    // m: it closes no nearer than this to its target
constexpr float kStandoffShare=0.7f;   // of its longest gun's reach
constexpr float kKeepTarget=20.0f;     // m the current target counts nearer
constexpr float kLeashSlack=60.0f;     // m: it engages enemies this far past its leash round the player
constexpr float kMoveRamp=10.0f;       // m over which the stick ramps from 0 to full
constexpr float kMoveHysteresis=5.0f;  // m: once stopped it moves again only this far past its stop
constexpr float kTurnGain=1.5f;        // stick per rad of heading error
constexpr float kTurnDeadband=0.03f;   // rad
constexpr float kPitchSettle=6.0f;     // frames to close the pitch error in
constexpr float kFireCone=0.05f;       // rad (about 3 deg), widened up close so a 3 m miss still fires
constexpr float kHitRadius=3.0f;
constexpr float kFireHold=1.6f;        // a firing gun keeps firing until this many cones off
constexpr ULONGLONG kStuckMs=1000;     // aim held at a stop this long without closing: pitch sign flipped
constexpr ULONGLONG kPlayerFixMs=10000;
constexpr float kNoElev=-100.0f;       // no barrel elevation seen last frame

struct Signature { std::size_t rva; unsigned char bytes[16]; std::size_t size; };
const Signature kGroundSignatures[]={
    {0x612D20,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48},16},   // slot 4
    {0x612D85,{0xF3,0x0F,0x10,0x82,0xC0,0x02,0x00,0x00,0xF3,0x0F,0x59,0x05,0x37,0x3C,0x62,0x01},16},   // move.x = -LX
    {0x61468B,{0x0F,0x10,0x93,0xF0,0x15,0x00,0x00,0xF3,0x0F,0x10,0x9B,0xE8,0x17,0x00,0x00,0x0F},16},   // move read
    {0x614862,{0x48,0x8D,0xB3,0x11,0x16,0x00,0x00},7},                                                 // triggers
    {0x614D0E,{0xF3,0x0F,0x10,0x83,0x00,0x16,0x00,0x00,0xF3,0x0F,0x59,0x05,0xDA,0x43,0x15,0x01},16},   // pitch
};
bool profileOk=false;

struct Gun { unsigned char* weapon; float reach,pos[3],dir[3]; bool barrel; };

struct Robo {
    ObjRef ref;
    ULONGLONG seen,loggedAt;
    ObjRef target;
    bool prevValid,moving;
    float prevHeading,lastTurn;
    int yawSign,votes;      // +1: a positive turn input increases atan2(fwd.x, fwd.z)
    bool yawLocked;
    float pitchSign;        // +1: a rising veh+0x1B10 raises the barrel
    float prevAim,prevElev;
    ULONGLONG stuckAt;
    bool firing[kGuns];
};
Robo robos[16]{};
constexpr ULONGLONG kStaleMs=2000;   // a crawler driven every frame; one not driven this long is gone (or not NPC-driven)
ULONGLONG fullLoggedAt=0;

float Dot3(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
float Wrap(float a) noexcept { while(a>kPi)a-=2*kPi; while(a<-kPi)a+=2*kPi; return a; }
float Horiz(const float* a,const float* b) noexcept {
    const float d[2]={b[0]-a[0],b[2]-a[2]};
    return std::sqrt(d[0]*d[0]+d[1]*d[1]);
}
float Dist(const float* a,const float* b) noexcept {
    const float d[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]};
    return std::sqrt(Dot3(d,d));
}

// The crawler's state, a new one in a free slot, the slot of a gone object at the same address or a stale
// one's; nullptr with every slot a live crawler's (logged: that one is not driven).
Robo* RoboFor(const void* vehicle,ULONGLONG ms) noexcept {
    Robo* slot=nullptr;
    for(auto& r:robos) {
        if(r.ref.Is(vehicle))return &r;
        if(!slot && (!r.ref || r.ref.obj==vehicle || ms-r.seen>kStaleMs))slot=&r;
    }
    if(!slot) {
        if(ms-fullLoggedAt>10000){fullLoggedAt=ms;Log("GROUND table full: v=%p not driven",vehicle);}
        return nullptr;
    }
    *slot=Robo{};slot->ref=ObjRef::Of(vehicle);slot->yawSign=1;slot->pitchSign=1.0f;slot->prevElev=kNoElev;
    Log("GROUND v=%p crawler driven by the plugin",vehicle);
    return slot;
}

// The guns slot 5 pulls (holders 0..2): live, loaded weapons, their reach and barrel.
int Guns(unsigned char* v,Gun* guns) noexcept {
    const auto holders=At<unsigned char*>(v,kHolders);
    const auto count=At<std::uint64_t>(v,kHolderCount);
    if(count==0 || count>16 || !Readable(holders,count*kHolderStride))return 0;
    const int n=count<kGuns ? static_cast<int>(count) : kGuns;
    for(int i=0;i<n;++i) {
        Gun& g=guns[i];g=Gun{};
        const auto holder=holders+i*kHolderStride;
        const auto ctrl=At<const unsigned char*>(holder,kHolderCtrl);
        if(!ctrl || !Readable(ctrl,0x10) || At<std::int32_t>(ctrl,8)==0)continue;
        const auto weapon=At<unsigned char*>(holder,kHolderWeapon);
        if(!Readable(weapon,kWeaponAmmo+4))continue;
        if(At<std::int32_t>(weapon,kWeaponAmmo)<=0)continue;
        const float reach=At<float>(weapon,kWeaponSpeed)*static_cast<float>(At<std::int32_t>(weapon,kWeaponAlive));
        g.weapon=weapon;
        g.reach=std::isfinite(reach) && reach>1.0f ? (reach<kMaxReach ? reach : kMaxReach) : kDefaultReach;
        g.barrel=GunBarrel(v,weapon,g.pos,g.dir);
    }
    return n;
}

struct Pick { const float* from; const float* leader; float range,leash; ObjRef keep; const void* best; float score,aim[3]; };

void Consider(void* ctx,const void* object,const float* aim) {
    auto& p=*static_cast<Pick*>(ctx);
    const float d=Dist(p.from,aim);
    if(d>p.range)return;
    if(p.leader && Horiz(p.leader,aim)>p.leash+kLeashSlack)return;
    const float score=d-(p.keep.Is(object) ? kKeepTarget : 0.0f);
    if(!p.best || score<p.score){p.best=object;p.score=score;std::memcpy(p.aim,aim,12);}
}

// Where it goes: the point and how near it stops.
struct Goal { bool any; float at[3],stop; };

Goal GoalOf(const float* pos,const float* leader,const void* target,const float* aim,float reach) noexcept {
    Goal g{};
    const bool leashed=leader && Horiz(pos,leader)>Cfg().groundLeash;
    if(target && !leashed) {
        float standoff=reach*kStandoffShare;
        if(standoff<kMinStandoff)standoff=kMinStandoff;
        g.any=true;std::memcpy(g.at,aim,12);g.stop=standoff;
    } else if(leader) {
        g.any=true;std::memcpy(g.at,leader,12);g.stop=Cfg().groundFollow;
    }
    return g;
}

// The move stick (vehicle frame: x right, z forward) towards the goal.
void MoveStick(Robo& r,const unsigned char* v,const Goal& g,float* stick) noexcept {
    stick[0]=stick[1]=0.0f;
    if(!g.any){r.moving=false;return;}
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float d[3]={g.at[0]-pos[0],0.0f,g.at[2]-pos[2]};
    const float dist=std::sqrt(d[0]*d[0]+d[2]*d[2]);
    const float start=g.stop+(r.moving ? 0.0f : kMoveHysteresis);
    if(dist<start || dist<0.5f){r.moving=false;return;}
    r.moving=true;
    const float mag=Clamp((dist-g.stop)/kMoveRamp,0.0f,1.0f)/dist;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float x=Dot3(d,m)*mag,z=Dot3(d,m+8)*mag;
    const float len=std::sqrt(x*x+z*z);
    if(len>1.0f){x/=len;z/=len;}
    stick[0]=x;stick[1]=z;
}

// The turn input towards `want` (a world direction; nullptr: none), learning its sign.
float Turn(Robo& r,const unsigned char* v,const float* want) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float fx=m[8],fz=m[10];
    if(std::sqrt(fx*fx+fz*fz)<0.3f){r.prevValid=false;return 0.0f;}   // on a wall: heading undefined
    const float heading=std::atan2(fx,fz);
    if(r.prevValid && !r.yawLocked && std::fabs(r.lastTurn)>0.3f) {
        const float turned=Wrap(heading-r.prevHeading);
        if(std::fabs(turned)>0.002f) {
            r.votes+=(turned>0)==(r.lastTurn*static_cast<float>(r.yawSign)>0) ? 1 : -1;
            if(r.votes<=-15){r.yawSign=-r.yawSign;r.votes=0;Log("GROUND v=%p turn sign flipped to %d",v,r.yawSign);}
            else if(r.votes>=30){r.yawLocked=true;Log("GROUND v=%p turn sign locked at %d",v,r.yawSign);}
        }
    }
    r.prevHeading=heading;r.prevValid=true;
    float in=0.0f;
    if(want && want[0]*want[0]+want[2]*want[2]>0.01f) {
        const float err=Wrap(std::atan2(want[0],want[2])-heading);
        if(std::fabs(err)>kTurnDeadband)in=Clamp(err*kTurnGain,-1.0f,1.0f)*static_cast<float>(r.yawSign);
    }
    r.lastTurn=in;
    return in;
}

// The pitch input: the barrel's elevation onto the target's (or the aim back level with none), learning
// which way veh+0x1B10 moves the barrel.
float Pitch(Robo& r,const unsigned char* v,const Gun* ref,const float* aim,ULONGLONG ms) noexcept {
    const float pitch=At<float>(v,kAimPitch);
    if(!std::isfinite(pitch))return 0.0f;
    if(!ref || !aim){r.stuckAt=0;r.prevElev=kNoElev;return Clamp(-pitch/(kPitchPerInput*kPitchSettle),-1.0f,1.0f);}
    const float elev=std::asin(Clamp(ref->dir[1],-1.0f,1.0f));
    if(r.prevElev>-10.0f) {
        const float moved=pitch-r.prevAim,turned=elev-r.prevElev;
        const float ratio=std::fabs(moved)>0.004f ? turned/moved : 0.0f;
        if(std::fabs(ratio)>0.3f && std::fabs(ratio)<3.0f)r.pitchSign+=0.2f*((ratio>0.0f ? 1.0f : -1.0f)-r.pitchSign);
    }
    r.prevAim=pitch;r.prevElev=elev;
    const float d[3]={aim[0]-ref->pos[0],aim[1]-ref->pos[1],aim[2]-ref->pos[2]};
    const float want=std::atan2(d[1],std::sqrt(d[0]*d[0]+d[2]*d[2]));
    const float err=want-elev;
    const float sign=r.pitchSign>=0.0f ? 1.0f : -1.0f;
    // Held at a stop with the error not closing: the sign is wrong.
    if(std::fabs(pitch)>1.5f && std::fabs(err)>0.2f) {
        if(!r.stuckAt)r.stuckAt=ms;
        else if(ms-r.stuckAt>kStuckMs){r.pitchSign=-sign;r.stuckAt=0;Log("GROUND v=%p aim stuck at a stop: pitch sign flipped",v);}
    } else r.stuckAt=0;
    return Clamp(err*sign/(kPitchPerInput*kPitchSettle),-1.0f,1.0f);
}

// Whether gun `g` is on `aim`: its barrel within the cone (held longer once firing), the round reaching it,
// the map ray clear and the player out of the line.
bool OnTarget(const unsigned char* v,const Gun& g,const float* aim,bool firing,bool losClear) noexcept {
    if(!g.weapon || !g.barrel || !losClear)return false;
    const float to[3]={aim[0]-g.pos[0],aim[1]-g.pos[1],aim[2]-g.pos[2]};
    const float dist=std::sqrt(Dot3(to,to));
    if(dist<1.0f || dist>g.reach)return false;
    const float off=std::acos(Clamp(Dot3(to,g.dir)/dist,-1.0f,1.0f));
    const float wide=std::atan(kHitRadius/dist);
    const float cone=(wide>kFireCone ? wide : kFireCone)*(firing ? kFireHold : 1.0f);
    return off<cone && !FriendInLine(g.pos,aim,v);
}

void Drive(Robo& r,unsigned char* v,ULONGLONG ms) noexcept {
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const bool hasLeader=player.at && GameMs()-player.at<kPlayerFixMs;
    const float* leader=hasLeader ? player.pos : nullptr;
    Gun guns[kGuns]{};
    const int n=Guns(v,guns);
    float reach=0.0f;const Gun* ref=nullptr;
    for(int i=0;i<n;++i) {
        if(!guns[i].weapon)continue;
        if(guns[i].reach>reach)reach=guns[i].reach;
        if(!ref && guns[i].barrel)ref=&guns[i];
    }
    // The target: the nearest enemy lock point in range (and near enough to the player).
    Pick p{};p.from=pos;p.leader=leader;p.range=Cfg().groundRange;p.leash=Cfg().groundLeash;p.keep=r.target;
    if(reach>0.0f)VisitEnemies(v,&Consider,&p);
    r.target=ObjRef::Of(p.best);
    const float* aim=p.best ? p.aim : nullptr;
    // Move, turn and aim.
    const Goal g=GoalOf(pos,leader,p.best,p.aim,reach);
    float move[2];
    MoveStick(r,v,g,move);
    float face[3]{};
    const float* want=nullptr;
    if(aim){face[0]=aim[0]-pos[0];face[2]=aim[2]-pos[2];want=face;}
    else if(r.moving){face[0]=g.at[0]-pos[0];face[2]=g.at[2]-pos[2];want=face;}
    const float turn=Turn(r,v,want);
    const float pitch=Pitch(r,v,ref,aim,ms);
    // Fire: one map ray per frame from the reference barrel.
    bool fire[kGuns]{};
    if(aim && Cfg().groundFire && ref) {
        float hit[3];
        const float d=Dist(ref->pos,aim);
        const float wall=MapRay(ref->pos,aim,hit);
        const bool clear=wall<0.0f || wall>d-3.0f;
        for(int i=0;i<n;++i)fire[i]=OnTarget(v,guns[i],aim,r.firing[i],clear);
    }
    // The input block, as slot 4 leaves it for a pad.
    Put<float>(v,kMove,move[0]);Put<float>(v,kMove+4,0.0f);Put<float>(v,kMove+8,move[1]);Put<float>(v,kMove+12,1.0f);
    Put<float>(v,kLook,pitch);Put<float>(v,kLook+4,turn);Put<float>(v,kLook+8,0.0f);Put<float>(v,kLook+12,1.0f);
    v[kJump]=0;v[kDash]=0;
    for(int i=0;i<kGuns;++i){v[kFire+i]=fire[i] ? 1 : 0;r.firing[i]=fire[i];}
    if(Cfg().debug && ms-r.loggedAt>1000) {
        r.loggedAt=ms;
        Log("GROUND v=%p pos=(%.0f,%.0f,%.0f) leader=%.0f target=%p dist=%.0f reach=%.0f goal=%d stop=%.0f moving=%d move=(%.2f,%.2f) turn=%.2f sign=%d%s votes=%d pitch=%.2f aim=%.2f psign=%+.1f fire=%d%d%d",
            v,pos[0],pos[1],pos[2],leader ? Horiz(pos,leader) : -1.0f,p.best,aim ? Dist(pos,aim) : -1.0f,reach,g.any,g.stop,r.moving,
            move[0],move[1],turn,r.yawSign,r.yawLocked ? "(locked)" : "",r.votes,pitch,At<float>(v,kAimPitch),r.pitchSign,
            fire[0],fire[1],fire[2]);
    }
}
}  // namespace

bool IsGroundRobo(const void* vehicle) noexcept {
    return Readable(vehicle,8) && At<const unsigned char*>(vehicle,0)==image+kVt502;
}

void GroundFrame(unsigned char* vehicle) noexcept {
    if(!profileOk || !Cfg().groundPilot || vehicle[kDead])return;
    if(SeatCount(vehicle)==0 || SeatRider(SeatAt(vehicle,0))!=Rider::dummy)return;   // only NPC drivers
    const ULONGLONG ms=GameMs();
    Robo* const r=RoboFor(vehicle,ms);
    if(!r)return;
    r->seen=ms;
    Drive(*r,vehicle,ms);
}

bool CheckGroundProfile() noexcept {
    __try {
        for(const auto& s:kGroundSignatures)if(!Matches(s.rva,s.bytes,s.size)){Log("GROUND profile mismatch at %#zx",s.rva);return false;}
        profileOk=true;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// A new mission (mission.cpp MissionStart): the last mission's crawlers are gone.
void ResetGround() noexcept {
    for(auto& r:robos)r=Robo{};
    fullLoggedAt=0;
}
}  // namespace crew
