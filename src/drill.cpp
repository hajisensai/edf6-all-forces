// The drill tank (钻头战车, docs/drill-re.md): EDF6VC_DRILL.SGO (tools/make_drill.py), the Blacker's class
// (Vehicle505_Tank) in the drill tank's model, its drill on a bone (kSpinBone) that the plugin spins.
// Melee: holding fire spins it up (DrillInput takes the trigger off the seat before the stock input sees it, so the
// stock cannon path never fires), letting go spins it down; the RPM sets how fast the drill turns, the damage it
// deals and how fast it breaks what it bores into. What it touches (an enemy's body in the box the drill and the
// hull's front sweep, or the map along rays through it) gets a drill charge every kBiteSec (jet_bay.cpp DrillCharge):
// a stock DemoIndirectFire round with a kChargeRadius blast fired by the tank, so the damage is the game's own: the
// enemies of its side only (the round's team is the tank's: no friendly fire), its kills, and the map's buildings
// and rocks hurt through the stock break-building path (a blast of 3 m or more, GameDamageInfo +0x60 bit 0, takes
// its damage off the map object's HP: docs/drill-re.md §3).
// The launch (the user, 2026-10-06: "钻头可以发射喷气的那种然后射完回收类似回旋镖攻击"): DrillLaunchKey / Button
// sends the drill off the hull on a jet (the flares' Booster flame, booster.cpp FlareFlames) at DrillLaunchSpeed,
// slowing to a stop at DrillLaunchRange (or bursting on the map where it meets it), then back to the hull like a
// boomerang, biting the enemies it passes (a charge every kFlightBiteSec, DrillLaunchDamage each). Drawn by writing
// the spin bone's local matrix from the world pose it should have: local = world x inverse(`body`'s world)
// (kSpinBone is a leaf of `body`, docs/drill-re.md §5.6).
// Heat: turning heats the drill (more the faster, more biting; a launch adds DrillLaunchHeat); overheated it stops,
// no bite, until it has cooled to DrillResumeHeat (the player's and an NPC's alike). Every enemy the drill kills
// sheds DrillKillCool of it (the user, 2026-10-06: "改成击杀减热量"): an enemy it bit that is dead within
// kKillWindowMs counts as its kill (the charge's own kill is not reported to the plugin: the bite is the evidence).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "body506.h"
#include "exhaust_pose.h"
#include "drill_net.h"
#include "online_authority.h"
#include "layout.h"
#include "memory.h"
#include <cmath>
#include <cstdio>
#include <cwchar>

namespace crew {
namespace {
// Vehicle505_Tank (the Blacker): its vtable, and its input (slot 55, 0x61ACD0) reads seat 0's primary trigger
// (seat+0x2E4) through 0x62DE50 (>= 0.8) and pulls weapon holder 0 with it (0x61AD14..0x61AD33).
constexpr unsigned kVt505=0x17DADB0;
constexpr std::size_t kSeatTrigger=0x2E4;
constexpr float kTriggerOn=0.8f;
const unsigned char kTriggerSig[]={0xF3,0x0F,0x10,0x8B,0xE4,0x02,0x00,0x00,0x48,0x8D,0x8B,0xC0,0x02,0x00,0x00,0xE8};
constexpr unsigned kTriggerRead=0x61AD14;
// The seat's input: 1 = a pad (its button bits), 0 = keyboard and mouse (the key, GetAsyncKeyState) (highcam.cpp).
constexpr std::size_t kSeatPad=0x2B0,kSeatButtons=0x2E8;
// The drill tank's bones (pylib/drill_model.py): kDrillBone, a marker of our own at the drill's base that tells the
// drill tank from a stock Blacker, and kSpinBone, the stock bone the drill's geometry rides (moved to the drill's
// base, its +Z the axis), so turning its local matrix about Z spins the drill in place. Why a stock bone: the drawn
// pose reaches only the bones the vehicle's CAS skeleton (v505_tank.cas) names. Our own bone's world matrix did follow
// every write (the 2026-10-05 19:56 play: "written 47 deg, the engine's world 38 deg", a frame late) and yet the drill
// never turned on screen; the jets' elevons, bones of our own too, were never seen moving either, while the Katyusha's
// launcher, a stock bone written the same way, was. `catapi_body` (the track rig's root) is in the CAS skeleton, and
// nothing in the SGO, the ragdoll or EDF.dll names it (docs/drill-re.md §5.4). The drawn pose's array (the model
// instance's +0x28 vector, data +0x30, count +0x40: built by 0x11002D0, the render command 0x1100D50 / 0x1100E90
// copies the records' world matrices into it, the skin palette 0x11009A0 = inverse bind (rec+0x30) x that array) is
// read back in the Debug pose log, so the next play shows which matrices the renderer got. kParentBone is the spin
// bone's parent (pylib/drill_model.py DRILL_PARENT): its world composes the spin bone's (local x parent.world).
// The drill's length, base radius and base in the model (= the vehicle's frame: the 505's slot 45 0x61AD70 roots the
// model at veh+0x60, SetWorld 0x1100B90), as built (m: pylib/drill_model.py DRILL_LENGTH / DRILL_RADIUS /
// DRILL_BASE; tools/selftest.py holds them equal). The bone record layout is the engine's model instance (0x1110FC0
// builds it, 0x1100010 composes world = local x parent.world), no class's own: the 505's instance is at veh+0xEE0
// like the 506's (0x61AE51).
const wchar_t kDrillBone[]=L"edf6vc_drill";
const wchar_t kSpinBone[]=L"catapi_body";
const wchar_t kParentBone[]=L"body";
constexpr float kDrillLength=6.55f,kDrillRadius=1.55f;
constexpr float kDrillBaseY=4.21f,kDrillBaseZ=3.35f;
constexpr std::size_t kModelInst=kModelInst506,kBoneLocal=kBoneLocal506,kBoneWorld=kBoneWorld506;
constexpr std::size_t kRecIndex=0x0C,kInstPose=0x30,kInstPoseCount=0x40,kPoseStride=0x40;
// The hull's front (m along the vehicle's forward: the model's hull vertices under the drill end at z 3.7; the
// Blacker's collision shapes, which the drill tank keeps, reach ~3.4). Pressed against a wall most of the drill is
// inside it (the old partial drill's base was already ~1.8 m inside): a ray from the bit can start inside the building
// and find nothing. So the map rays start over the vehicle's origin (inside the hull: the hull keeps the walls
// out), and a charge starts no farther back than kChargeFrom (past the collision shapes: it must not meet the tank).
constexpr float kHullFront=3.7f,kChargeFrom=3.5f;
// What the drill reaches (2026-10-05 19:56 play: rammed into ants at 300 rpm, not one bite: the nearest lock points
// were 5.5..6.5 m from the drill's axis, which is up over the hull while an ant's lock point is ~1 m up and its body on
// the ground; a cylinder round the axis never meets an enemy the drill is ploughing through). So the contact volume is
// the space the drill and the hull's front sweep: a box in the vehicle's frame from the ground up to the drill's top
// (+ kTopMargin), kBoxHalfX either side (the hull's half width: the hull is 4.8 m wide), from the hull's front to
// kAhead past the drill's tip. An enemy touches it with its body: the segment from its position (object +0x90: its
// feet / root) to its lock point, kBodyPad thick (a lock point sits inside the body, the body reaches past it).
constexpr float kBoxHalfX=2.4f,kTopMargin=0.3f,kAhead=1.0f,kBodyPad=1.0f;
constexpr float kBoxTop=kDrillBaseY+kDrillRadius+kTopMargin,kBoxFront=kDrillBaseZ+kDrillLength+kAhead;
// The map: rays forward from over the vehicle's origin to kBoxFront at these (x aside, y up) of the vehicle's frame:
// down the middle at four heights (a rock or low wall to the drill's top; not under kRayLow: a ray that low meets the
// ground ahead on any rise, and the drill cannot reach below the hull's nose anyway) and two to the sides.
struct RayAt { float x,y; };
constexpr float kRayLow=1.2f;
constexpr RayAt kRays[]={{0.0f,kRayLow},{0.0f,2.9f},{0.0f,kDrillBaseY},{0.0f,5.2f},{-1.6f,2.9f},{1.6f,2.9f}};
constexpr float kBiteSec=0.2f;      // a charge this often while it touches something and turns at kWorkShare or more
constexpr ULONGLONG kBiteMs=200,kFrameMs=50;   // the same in ms; "biting" (heat) = touched within a bite and a bit
constexpr float kWorkShare=0.15f;   // of the top RPM: slower, it neither hurts nor breaks anything
constexpr float kInto=1.0f;         // m past the map hit the charge is aimed (it meets the wall on its way)
constexpr float kLead=2.0f;         // m short of what it touches the charge starts (never behind kChargeFrom)
constexpr float kPi=3.14159265f;
// How it looks turning. The drill's mesh repeats every 1/16 turn (its flutes; with the texture every 1/4: measured on
// the OBJ, docs/drill-re.md §4). 300 RPM at 60 frames a second is 30 deg a frame, 1.33 of that repeat: frame after
// frame the eye would see the flutes creep or flicker, not a drill turning. So the drawn turn takes at most
// kSpinStepMost a frame (0.4 of the repeat: always seen turning forward), in proportion to the RPM; the RPM itself
// (damage, the HUD) is not capped.
constexpr float kSpinRepeat=2.0f*kPi/16.0f;
constexpr float kSpinStepMost=0.4f*kSpinRepeat;
// Heat (the user, 2026-10-05: the drill heats up and must stop when it overheats): per second +share x (1 + kBiteHeat
// while biting) / DrillOverheatSec, -(1 - share) / DrillCoolSec (share = RPM / top RPM): the top RPM idling heats it
// from cold in DrillOverheatSec, standing still cools it in DrillCoolSec. At 1 it overheats: no spin, no bite, until
// it is down to DrillResumeHeat. A launch adds DrillLaunchHeat at once, a kill takes DrillKillCool off.
constexpr float kBiteHeat=0.5f;
// The launch. Out: from the drill's place on the hull along the vehicle's forward at DrillLaunchSpeed, slowing evenly
// (kOutStop of that speed left is a stop) so that it stops at DrillLaunchRange; a map ray each frame from the tip's
// last place (at the launch: over the vehicle's origin, inside the hull, as kRays) to its next: a hit is a bite on
// the map there and the turn back. Back: from rest, speeding up as evenly to DrillLaunchSpeed, straight at the drill's
// place on the hull (where the vehicle is now); caught within kCatchM (or the step), or snapped home after kBackMostMs.
// Its axis: out, the launch's direction; back, nose toward the hull, then aligned with the socket over kAlignM.
constexpr float kOutStop=0.05f,kCatchM=1.0f,kAlignM=15.0f;
constexpr float kReturnTurnRate=2.0f*kPi; // rad/s: half a second to reverse, including exactly opposite axes
constexpr ULONGLONG kBackMostMs=12000;
// In flight it bites every kFlightBiteSec the enemy whose body (root..lock point, kBodyPad thick) is in its reach,
// nearest its axis; not one within kNearHullM of the vehicle's origin (the charge would start in or meet the tank).
// Its reach is the hull's contact box carried along with it (2026-10-09: "发射钻头的时候没伤害，回收的时候有"): the
// drill flies at the height it left the hull, its axis kDrillBaseY over the ground, and a cylinder of its radius +
// kBodyPad round that axis passed over every enemy on the ground (an ant's root is on the ground, its lock point ~1 m
// up: 3..4 m under the axis; the 2026-10-05 lesson of the hull's probe, §5.5). The bites the user saw were the hull's
// own probe once it was caught (its RPM at the top). So, in the drill's frame (origin its base, z its axis, y the
// vehicle's up made square to it), the reach is kBoxHalfX aside, from kDrillBaseY under the axis to kDrillRadius +
// kTopMargin over it, from its base to kAhead past its tip: the hull's box (Touch) less the hull.
constexpr float kFlightBiteSec=0.1f,kNearHullM=7.0f;
constexpr float kReachLo[3]={-kBoxHalfX,-kDrillBaseY,0.0f},kReachHi[3]={kBoxHalfX,kDrillRadius+kTopMargin,kDrillLength+kAhead};
// Kills: an enemy bitten in the last kKillWindowMs that is dead (or gone) is the drill's (up to kVictims tracked).
constexpr ULONGLONG kKillWindowMs=1500;
constexpr int kVictims=16;
// An NPC driver has no trigger: its drill spins while it touches something (a probe every kBiteSec), kNpcHoldMs on.
// It never launches the drill.
constexpr ULONGLONG kNpcHoldMs=1500;
constexpr ULONGLONG kStaleMs=2000;   // a drill tank not seen this long is gone: its slot is free
constexpr ULONGLONG kLogMs=1000;     // Debug: a drill's contact / bite line at most this often, its pose 3x rarer
constexpr int kMaxDrills=8;

enum class Flight : unsigned char { home, out, back };

struct Victim { ObjRef ref; ULONGLONG at; };

struct Drill {
    ObjRef ref;
    ULONGLONG seen,lastMs,touchAt,loggedAt,biteLogAt,poseLogAt,frame;
    float rpm,angle,bite,heat;
    bool held,player,npc,overheated;   // seat 0: the player, an NPC (RideAi's dummy), or (neither) empty
    const void* bones;            // the model's bone array the records below are in (looked up again when it changes)
    unsigned char* rec;           // kSpinBone's record (the one turned)
    unsigned char* marker;        // kDrillBone's (the Debug pose log's reference)
    const unsigned char* parent;  // kParentBone's (its world composes the spin bone's: the launch's pose)
    float bind[16],set[16];
    bool written,rewritten;
    int bites,misses,kills;
    // The launch (see kOutStop): where the drill's base is (world), the way it was sent, its speed (m/s), how far it
    // has gone out, its tip's last place (the next map ray's start), its axis as drawn, when it turned back.
    Flight flight;
    bool launchHeld,launchAsked,flaming,poseFailed;
    float pos[3],dir[3],axis[3],speed,flown,lastTip[3],flightBite;
    ULONGLONG backAt;
    Victim victims[kVictims];
    drill_net::Gate received;
    bool networked;
    bool keys=true;               // input device used by the local pilot; the HUD labels the actual launch binding
    ULONGLONG sentAt,receivedAt;
    Flight sentFlight;
};
Drill drills[kMaxDrills]{};
bool triggerOk=false;

// The local player's drill now (game thread writes, the HUD's draw thread reads).
SRWLOCK cueLock=SRWLOCK_INIT;
DrillCue cue{};
ULONGLONG cueAt=0;
constexpr ULONGLONG kCueFreshMs=300;

float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
float Clamp(float x,float lo,float hi) noexcept { return x<lo ? lo : x>hi ? hi : x; }
float Len(const float* a) noexcept { return std::sqrt(Dot(a,a)); }
void Cross(const float* a,const float* b,float* out) noexcept {
    const float r[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
    std::memcpy(out,r,12);
}
// a / |a| into out; false (out untouched) for a zero vector.
bool Unit(const float* a,float* out) noexcept {
    const float l=Len(a);
    if(!(l>1e-6f))return false;
    for(int i=0;i<3;++i)out[i]=a[i]/l;
    return true;
}
// A unit-axis interpolation that also defines the half-turn (a linear blend of +Z/-Z collapses at its midpoint).
void BlendAxis(const float* from,const float* to,const float* up,float amount,float* out) noexcept {
    const float dot=Clamp(Dot(from,to),-1.0f,1.0f),angle=std::acos(dot);
    if(angle<1e-5f){std::memcpy(out,to,12);return;}
    float tangent[3];
    for(int i=0;i<3;++i)tangent[i]=to[i]-from[i]*dot;
    if(!Unit(tangent,tangent)) {
        Cross(up,from,tangent);
        if(!Unit(tangent,tangent)) { const float side[3]={1,0,0};Cross(side,from,tangent);Unit(tangent,tangent); }
    }
    const float a=angle*Clamp(amount,0.0f,1.0f);
    for(int i=0;i<3;++i)out[i]=from[i]*std::cos(a)+tangent[i]*std::sin(a);
}
// How far p is from the segment a..b.
float SegmentGap(const float* p,const float* a,const float* b) noexcept {
    const float ab[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]},ap[3]={p[0]-a[0],p[1]-a[1],p[2]-a[2]};
    const float l2=Dot(ab,ab);
    const float t=l2>1e-9f ? Clamp(Dot(ap,ab)/l2,0.0f,1.0f) : 0.0f;
    const float d[3]={ap[0]-ab[0]*t,ap[1]-ab[1]*t,ap[2]-ab[2]*t};
    return Len(d);
}

// Whether the virtual key `vk` is down while the game has the foreground (0: never; highcam.cpp's).
bool KeyHeld(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;   // the map view holds the player's keys (map.cpp)
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}

bool Is505(const void* v) noexcept { return At<const unsigned char*>(v,0)==image+kVt505; }

// v's spin bone record (nullptr: not the drill tank's model); the bind pose is taken from it the first time.
unsigned char* DrillBone(Drill& d,const unsigned char* v) noexcept {
    const unsigned char* inst=v+kModelInst;
    const auto bones=At<const void*>(inst,kInstBones506);
    if(!bones)return nullptr;
    if(bones==d.bones)return d.rec;
    d.bones=bones;d.written=false;d.rewritten=false;
    d.marker=BoneRecord506(inst,kDrillBone);
    d.rec=d.marker ? BoneRecord506(inst,kSpinBone) : nullptr;
    d.parent=d.rec ? BoneRecord506(inst,kParentBone) : nullptr;
    if(d.rec)std::memcpy(d.bind,d.rec+kBoneLocal,64);
    return d.rec;
}

// The drill tank's state: a new one in a free slot (or a gone vehicle's at the same address, or a stale one's).
Drill* DrillOf(const void* v,ULONGLONG ms) noexcept {
    Drill* slot=nullptr;
    for(auto& d:drills) {
        if(d.ref.Is(v))return &d;
        if(!slot && (!d.ref || d.ref.obj==v || ms-d.seen>kStaleMs))slot=&d;
    }
    if(slot)*slot=Drill{ObjRef::Of(v),ms};
    return slot;
}

// The drill tank v is (505 class with the marker bone), its state; nullptr for any other vehicle.
Drill* DrillTank(unsigned char* v) noexcept {
    if(!Is505(v) || v[kDead] || SeatCount(v)==0)return nullptr;
    // Probe the marker before taking a slot: it is only ever in the drill tank's model.
    if(!BoneRecord506(v+kModelInst,kDrillBone))return nullptr;
    const ULONGLONG ms=GameMs();
    Drill* const d=DrillOf(v,ms);
    if(!d || !DrillBone(*d,v))return nullptr;
    return d;
}

// The rows x, y of a drill turned `angle` about its axis z from its unturned rows x0, y0 (row vectors: rows 0 and 1
// turn in their plane; the axis stays).
void Turn(const float* x0,const float* y0,float angle,float* x,float* y,int n) noexcept {
    const float co=std::cos(angle),si=std::sin(angle);
    for(int k=0;k<n;++k){x[k]=co*x0[k]+si*y0[k];y[k]=-si*x0[k]+co*y0[k];}
}

void WriteLocal(Drill& d) noexcept {
    if(d.written && std::memcmp(d.rec+kBoneLocal,d.set,64)!=0 && !d.rewritten) {
        d.rewritten=true;   // something else (an animation) writes it every frame: the spin would not show. Said once.
        Log("DRILL v=%p: its spin bone's local matrix was rewritten by the game between frames",d.ref.obj);
    }
    std::memcpy(d.rec+kBoneLocal,d.set,64);
    d.written=true;
}

// The drill on the hull, spun `angle` rad about its own axis: local = Rz(angle) x bind. The engine composes the world
// matrix from it in the 505's slot 45 (0x61AD70 -> SetWorld 0x1100B90 -> 0x1100010: every bone whose rec+8 flag is
// 1, as 0x1110FC0 sets it), which VehicleBase's update (slot 5, 0x630250) calls every frame.
void PoseHome(Drill& d) noexcept {
    Turn(d.bind,d.bind+4,d.angle,d.set,d.set+4,4);
    std::memcpy(d.set+8,d.bind+8,32);
    WriteLocal(d);
}

// The drill in flight: its world pose (base at d.pos, +Z along d.axis, x / y from the vehicle's up, spun d.angle)
// brought into the parent bone's frame (local = world x parent.world^-1, the parent's world as last composed: a
// frame late at most). False (nothing written) without the parent or with a degenerate one.
// The flying drill's unturned rows x, y round its axis d.axis: x = the vehicle's up x the axis (the vehicle's rows:
// x left = up x forward), y = axis x x (the vehicle's up made square to the axis). False for a degenerate axis.
bool FlightRows(const unsigned char* v,const Drill& d,float* x0,float* y0) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    Cross(m+4,d.axis,x0);
    if(!Unit(x0,x0)){Cross(d.axis,m+8,x0);if(!Unit(x0,x0))return false;}
    Cross(d.axis,x0,y0);
    return true;
}

bool PoseFlight(const unsigned char* v,Drill& d) noexcept {
    if(!d.parent)return false;
    float x0[3],y0[3];
    if(!FlightRows(v,d,x0,y0))return false;
    float w[16]{};
    Turn(x0,y0,d.angle,w,w+4,3);
    std::memcpy(w+8,d.axis,12);
    std::memcpy(w+12,d.pos,12);
    w[15]=1.0f;
    float inv[16];
    if(!exhaust::Inverse(reinterpret_cast<const float*>(d.parent+kBoneWorld),inv))return false;
    exhaust::Mul(w,inv,d.set);
    WriteLocal(d);
    return true;
}

// The vehicle's frame (veh+0x60: rows right, up, forward, then the position; the model's root): `local` (x aside, y
// up, z forward) to the world, and back.
void ToWorld(const unsigned char* v,const float* local,float* out) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    for(int i=0;i<3;++i)out[i]=m[12+i]+m[i]*local[0]+m[4+i]*local[1]+m[8+i]*local[2];
}
void ToLocal(const unsigned char* v,const float* world,float* out) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float p[3]={world[0]-m[12],world[1]-m[13],world[2]-m[14]};
    for(int i=0;i<3;++i)out[i]=Dot(p,m+4*i);
}
// The drill's place on the hull (its base, world).
void HomeBase(const unsigned char* v,float* out) noexcept {
    const float base[3]={0.0f,kDrillBaseY,kDrillBaseZ};
    ToWorld(v,base,out);
}

// A world matrix's turn about the drill's axis in the vehicle's frame (deg): its x row against the vehicle's rows.
float TurnDeg(const unsigned char* v,const float* w) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    return std::atan2(Dot(w,m+4),Dot(w,m))*180.0f/kPi;
}

// The drawn pose's matrix of bone record `rec` (the instance's pose array, see kSpinBone), or nullptr.
const float* DrawnPose(const unsigned char* v,const unsigned char* rec) noexcept {
    const unsigned char* inst=v+kModelInst;
    if(!rec || !Readable(inst,kInstPoseCount+8))return nullptr;
    const auto data=At<const unsigned char*>(inst,kInstPose);
    const auto count=At<std::uint64_t>(inst,kInstPoseCount);
    const auto idx=At<std::int32_t>(rec,kRecIndex);
    if(!data || idx<0 || static_cast<std::uint64_t>(idx)>=count || count>256)return nullptr;
    const unsigned char* p=data+static_cast<std::size_t>(idx)*kPoseStride;
    return Readable(p,kPoseStride) ? reinterpret_cast<const float*>(p) : nullptr;
}

// Debug (3 kLogMs): the drawn spin as the engine has it: the spin bone's world (its angle about the axis: the
// written one, a frame late, when the world follows the local matrix), its matrix in the drawn pose array (what the
// skin palette is built from; "n/a" when unreadable), the marker's world (a bone of our own: it does not turn), and
// how far the spin bone's world origin is from where the drill should be (its place on the hull, or in flight d.pos).
void LogPose(const unsigned char* v,Drill& d,ULONGLONG ms) noexcept {
    if(!Cfg().debug || ms-d.poseLogAt<kLogMs*3)return;
    d.poseLogAt=ms;
    const float* w=reinterpret_cast<const float*>(d.rec+kBoneWorld);
    const float* drawn=DrawnPose(v,d.rec);
    const float* mark=d.marker ? reinterpret_cast<const float*>(d.marker+kBoneWorld) : w;
    float base[3];
    if(d.flight==Flight::home)HomeBase(v,base);
    else std::memcpy(base,d.pos,12);
    const float off[3]={w[12]-base[0],w[13]-base[1],w[14]-base[2]};
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    char seen[32]="n/a";
    if(drawn)std::snprintf(seen,sizeof(seen),"%.0f deg",TurnDeg(v,drawn));
    Log("DRILL v=%p pose (%s): %.0f rpm, written %.0f deg, spin bone world %.0f deg, drawn pose %s (marker world %.0f deg); "
        "bone origin %.2f m off where the drill should be, its axis . forward %.2f",v,
        d.flight==Flight::home ? "home" : d.flight==Flight::out ? "out" : "back",d.rpm,d.angle*180.0f/kPi,TurnDeg(v,w),seen,
        TurnDeg(v,mark),Len(off),Dot(w+8,m+8)/std::sqrt(Dot(w+8,w+8)+1e-12f));
}

// Where the segment a..b (vehicle frame) first enters the box lo..hi (its t in [0, 1]), false when it misses.
bool SegmentInBox(const float* a,const float* b,const float* lo,const float* hi,float* t) noexcept {
    float t0=0.0f,t1=1.0f;
    for(int i=0;i<3;++i) {
        const float d=b[i]-a[i];
        if(std::fabs(d)<1e-6f) {
            if(a[i]<lo[i] || a[i]>hi[i])return false;
            continue;
        }
        const float u=(lo[i]-a[i])/d,w=(hi[i]-a[i])/d;
        t0=std::fmax(t0,std::fmin(u,w));t1=std::fmin(t1,std::fmax(u,w));
        if(t0>t1)return false;
    }
    *t=t0;
    return true;
}

// How far point p (vehicle frame) is outside the box lo..hi (0 inside).
float BoxGap(const float* p,const float* lo,const float* hi) noexcept {
    float s=0.0f;
    for(int i=0;i<3;++i) {
        const float g=std::fmax(std::fmax(lo[i]-p[i],p[i]-hi[i]),0.0f);
        s+=g*g;
    }
    return std::sqrt(s);
}

// Whether an enemy's body (the segment root..lock, both in the box's frame, kBodyPad thick) is in the box lo..hi.
bool BodyInBox(const float* root,const float* lock,const float* lo,const float* hi) noexcept {
    float l[3],h[3],t=0.0f;
    for(int i=0;i<3;++i){l[i]=lo[i]-kBodyPad;h[i]=hi[i]+kBodyPad;}
    return SegmentInBox(root,lock,l,h,&t);
}

// An enemy's root (object +0x90, world), or its lock point when that is not a number.
void RootOf(const void* object,const float* aim,float* out) noexcept {
    const float* pos=reinterpret_cast<const float*>(static_cast<const unsigned char*>(object)+kPosition);
    std::memcpy(out,std::isfinite(pos[0]+pos[1]+pos[2]) ? pos : aim,12);
}

// The enemies against the contact box (EnemyVisitor), in the vehicle's frame: the one whose body (root..lock point,
// kBodyPad thick) is in the box nearest the hull, and the nearest of all to the box (for the log).
struct Reach { const unsigned char* v; float lo[3],hi[3],at[3],along,nearest,closest[3]; int seen; bool found; const void* who; };
void SeeEnemy(void* ctx,const void* object,const float* aim) noexcept {
    auto& r=*static_cast<Reach*>(ctx);
    float lock[3],root[3],rootWorld[3];
    ToLocal(r.v,aim,lock);
    RootOf(object,aim,rootWorld);
    ToLocal(r.v,rootWorld,root);
    const float gap=std::fmin(BoxGap(lock,r.lo,r.hi),BoxGap(root,r.lo,r.hi))-kBodyPad;
    if(r.seen++==0 || gap<r.nearest){r.nearest=gap<0.0f ? 0.0f : gap;std::memcpy(r.closest,lock,12);}
    if(!BodyInBox(root,lock,r.lo,r.hi))return;
    if(r.found && lock[2]>=r.along)return;
    r.found=true;r.along=lock[2];std::memcpy(r.at,aim,12);r.who=object;
}

// What one probe saw (the bite's and the log's): an enemy or the map, the charge's start and aim, the map hit (m
// along the vehicle's forward, the ray's index; -1: none) and the enemies.
struct Contact { bool enemy,map; float from[3],at[3],mapAlong; int ray; Reach reach; };

// The map along ray kRays[k], from over the vehicle's origin to kBoxFront: the hit's distance, or -1.
float MapAlong(const unsigned char* v,int k,float* hit) noexcept {
    const float a0[3]={kRays[k].x,kRays[k].y,0.0f},b0[3]={kRays[k].x,kRays[k].y,kBoxFront};
    float a[3],b[3];
    ToWorld(v,a0,a);ToWorld(v,b0,b);
    const float d=MapRay(a,b,hit);
    return d<0.0f ? -1.0f : d;
}

// What the drill touches now: an enemy in the contact box (its lock point the charge's aim) or the map along the
// rays (a point kInto past the nearest hit); the charge starts kLead short of it at its height and side, never
// behind kChargeFrom. False with nothing.
bool Touch(const unsigned char* v,Contact& c) noexcept {
    c=Contact{};c.mapAlong=-1.0f;c.ray=-1;
    Reach& r=c.reach;
    r.v=v;
    const float lo[3]={-kBoxHalfX,0.0f,kHullFront},hi[3]={kBoxHalfX,kBoxTop,kBoxFront};
    std::memcpy(r.lo,lo,12);std::memcpy(r.hi,hi,12);
    VisitEnemies(v,&SeeEnemy,&r);
    if(r.found) {
        c.enemy=true;std::memcpy(c.at,r.at,12);
    } else {
        float hit[3];
        for(int k=0;k<static_cast<int>(sizeof(kRays)/sizeof(kRays[0]));++k) {
            float h[3];
            const float d=MapAlong(v,k,h);
            if(d<0.0f || (c.ray>=0 && d>=c.mapAlong))continue;
            c.mapAlong=d;c.ray=k;std::memcpy(hit,h,12);
        }
        if(c.ray<0)return false;
        c.map=true;
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        for(int i=0;i<3;++i)c.at[i]=hit[i]+m[8+i]*kInto;
    }
    float aim[3];ToLocal(v,c.at,aim);
    const float from[3]={Clamp(aim[0],-kBoxHalfX,kBoxHalfX),Clamp(aim[1],kRayLow,kBoxTop),std::fmax(aim[2]-kLead,kChargeFrom)};
    ToWorld(v,from,c.from);
    return true;
}

// Debug (kLogMs): what a probe decided and why.
void LogTouch(const unsigned char* v,Drill& d,const Contact& c,bool touched,float share,ULONGLONG ms) noexcept {
    if(!Cfg().debug || ms-d.loggedAt<kLogMs)return;
    d.loggedAt=ms;
    const Reach& r=c.reach;
    if(!touched) {
        Log("DRILL v=%p touch: nothing (%.0f rpm, heat %.0f%%): no map hit on %d rays (0..%.1f m along, %.1f..%.1f m up); "
            "%d enemies seen, the nearest body %.1f m from the drill box (lock point %.1f aside, %.1f up, %.1f along); "
            "box x +-%.1f, y 0..%.2f, z %.1f..%.2f, body pad %.1f",v,d.rpm,d.heat*100.0f,
            static_cast<int>(sizeof(kRays)/sizeof(kRays[0])),kBoxFront,kRayLow,kRays[3].y,r.seen,r.seen ? r.nearest : -1.0f,
            r.closest[0],r.closest[1],r.closest[2],kBoxHalfX,kBoxTop,kHullFront,kBoxFront,kBodyPad);
        return;
    }
    float aim[3];ToLocal(v,c.at,aim);
    Log("DRILL v=%p touch: %s at (%.1f,%.1f,%.1f) = %.1f aside, %.1f up, %.1f along%s, charge from (%.1f,%.1f,%.1f); %.0f rpm%s, heat %.0f%%%s",
        v,c.enemy ? "enemy" : "map",c.at[0],c.at[1],c.at[2],aim[0],aim[1],aim[2],
        c.enemy ? " (its body in the box)" : c.ray>=0 && kRays[c.ray].x==0.0f ? " (centre ray)" : " (side ray)",
        c.from[0],c.from[1],c.from[2],d.rpm,share<kWorkShare ? " (too slow to bite)" : "",d.heat*100.0f,
        d.overheated ? " OVERHEATED: no bite" : "");
}

// An enemy the drill just bit: remembered (kKillWindowMs) to see whether it dies of it.
void Bitten(Drill& d,const void* who,ULONGLONG ms) noexcept {
    if(!who)return;
    Victim* slot=nullptr;
    for(auto& x:d.victims) {
        if(x.ref.Is(who)){x.at=ms;return;}
        if(!slot && (!x.ref || ms-x.at>kKillWindowMs))slot=&x;
    }
    if(!slot) {   // all fresh: the oldest makes room
        slot=&d.victims[0];
        for(auto& x:d.victims)if(x.at<slot->at)slot=&x;
    }
    *slot=Victim{ObjRef::Of(who),ms};
}

// One charge fired by the drill (`who`: the enemy it is aimed at, null for the map), counted and logged (kLogMs).
void Charge(unsigned char* v,Drill& d,const float* from,const float* at,float damage,const void* who,const char* what,
            ULONGLONG ms) noexcept {
    const bool fired=DrillCharge(v,from,at,damage);
    if(fired){++d.bites;if(who)Bitten(d,who,ms);}
    else ++d.misses;
    if(Cfg().debug && (ms-d.biteLogAt>=kLogMs || !fired)) {
        d.biteLogAt=ms;
        Log("DRILL v=%p bite: %s, %.0f damage, charge %s (%d fired, %d not, %d kills)",v,what,damage,
            fired ? "fired" : "NOT fired",d.bites,d.misses,d.kills);
    }
}

// One probe and, turning fast enough and not overheated, a bite: a drill charge onto what it touches, its damage the
// RPM's share of the per-second value times kBiteSec.
void Bite(unsigned char* v,Drill& d,float share,ULONGLONG ms) noexcept {
    Contact c;
    const bool touched=Touch(v,c);
    LogTouch(v,d,c,touched,share,ms);
    if(!touched)return;
    d.touchAt=ms;
    if(share<kWorkShare || d.overheated)return;
    const float perSec=c.enemy ? Cfg().drillDamage : Cfg().drillBreak;
    const float damage=perSec*share*kBiteSec;
    if(!(damage>0.0f))return;
    Charge(v,d,c.from,c.at,damage,c.enemy ? c.reach.who : nullptr,c.enemy ? "enemy" : "map",ms);
}

// Heat shed (a kill, d.heat 0..1), with the cooled-down line when that brings it to DrillResumeHeat.
void Shed(const unsigned char* v,Drill& d,float amount) noexcept {
    d.heat=d.heat-amount<0.0f ? 0.0f : d.heat-amount;
    if(d.overheated && d.heat<=Cfg().drillResumeHeat) {
        d.overheated=false;
        Log("DRILL v=%p cooled to %.0f%%: it turns again",v,d.heat*100.0f);
    }
}

// Latch as soon as any heat source reaches the cap, before a subsequent cooling step can lower it again.
void LatchOverheat(const unsigned char* v,Drill& d) noexcept {
    if(!d.overheated && d.heat>=1.0f) {
        d.overheated=true;
        Log("DRILL v=%p overheated (%s): it stops until it cools to %.0f%%",v,d.player ? "player" : d.npc ? "NPC" : "empty",
            Cfg().drillResumeHeat*100.0f);
    }
}

// The heat over `dt` s at RPM share `share` (`biting`: touched within the last bite): see kBiteHeat. It overheats at 1
// and cools to DrillResumeHeat before it turns again; both said in the log.
void Heat(const unsigned char* v,Drill& d,float share,bool biting,float dt) noexcept {
    const auto& c=Cfg();
    d.heat+=(share*(1.0f+(biting ? kBiteHeat : 0.0f))/c.drillOverheatSec-(1.0f-share)/c.drillCoolSec)*dt;
    d.heat=d.heat<0.0f ? 0.0f : d.heat>1.0f ? 1.0f : d.heat;
    LatchOverheat(v,d);
    if(d.overheated && d.heat<=c.drillResumeHeat)Shed(v,d,0.0f);
}

// Whether the enemy `r` is dead: its control block's count gone, the object reused, its dead flag or no HP left.
bool Died(const ObjRef& r) noexcept {
    if(!r.ctrl || !Readable(r.ctrl,0x10) || At<long>(r.ctrl,8)<=0)return true;
    const auto o=static_cast<const unsigned char*>(r.obj);
    if(!Readable(o,kHp+4) || At<const void*>(o,kSelfCtrl)!=r.ctrl)return true;
    return o[kDead]!=0 || !(At<float>(o,kHp)>0.0f);
}

// The drill's kills this frame: each one sheds DrillKillCool of its heat.
void Kills(const unsigned char* v,Drill& d,ULONGLONG ms) noexcept {
    for(auto& x:d.victims) {
        if(!x.ref)continue;
        if(ms-x.at>kKillWindowMs){x=Victim{};continue;}
        if(!Died(x.ref))continue;
        x=Victim{};
        ++d.kills;
        const float before=d.heat;
        Shed(v,d,Cfg().drillKillCool);
        if(Cfg().debug)Log("DRILL v=%p kill %d: heat %.0f%% -> %.0f%%",v,d.kills,before*100.0f,d.heat*100.0f);
    }
}

// The launch: the drill leaves its place on the hull along the vehicle's forward, its heat up by DrillLaunchHeat.
void Launch(unsigned char* v,Drill& d) noexcept {
    const auto& c=Cfg();
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    HomeBase(v,d.pos);
    std::memcpy(d.dir,m+8,12);
    std::memcpy(d.axis,d.dir,12);
    const float over[3]={0.0f,kDrillBaseY,0.0f};   // the first map ray from over the vehicle's origin (see kRays)
    ToWorld(v,over,d.lastTip);
    d.speed=c.drillLaunchSpeed;d.flown=0.0f;d.flightBite=kFlightBiteSec;d.backAt=0;
    d.flight=Flight::out;
    d.rpm=c.drillMaxRpm;   // its jet spins it at the top from the launch on, whatever the trigger did before (FlightSpin)
    d.heat=d.heat+c.drillLaunchHeat>1.0f ? 1.0f : d.heat+c.drillLaunchHeat;
    LatchOverheat(v,d);
    Log("DRILL v=%p launched: %.0f m/s out to %.0f m, heat %.0f%%",v,d.speed,c.drillLaunchRange,d.heat*100.0f);
}

void TurnBack(const unsigned char* v,Drill& d,ULONGLONG ms,const char* why) noexcept {
    d.flight=Flight::back;d.speed=0.0f;d.backAt=ms;
    if(Cfg().debug)Log("DRILL v=%p turns back after %.0f m (%s)",v,d.flown,why);
}

// The drill caught back on the hull (or snapped there): its flame burns down.
void Catch(const unsigned char* v,Drill& d,ULONGLONG ms,const char* how) noexcept {
    d.flight=Flight::home;d.speed=0.0f;
    if(d.flaming){FlareFlames(v,nullptr,nullptr,0,ms);d.flaming=false;}
    Log("DRILL v=%p back on the hull (%s; %d kills so far)",v,how,d.kills);
}

// Out one frame: the tip's ray to its next place; a map hit bursts a charge there and turns it back.
void FlyOut(unsigned char* v,Drill& d,float dt,ULONGLONG ms) noexcept {
    const auto& c=Cfg();
    const float slow=c.drillLaunchSpeed*c.drillLaunchSpeed/(2.0f*c.drillLaunchRange);
    const float step=d.speed*dt;
    float tip[3],hit[3];
    for(int i=0;i<3;++i)tip[i]=d.pos[i]+d.dir[i]*(kDrillLength+step);
    if(MapRay(d.lastTip,tip,hit)>=0.0f) {
        float at[3],from[3];
        for(int i=0;i<3;++i){at[i]=hit[i]+d.dir[i]*kInto;from[i]=hit[i]-d.dir[i]*kLead;d.pos[i]=hit[i]-d.dir[i]*kDrillLength;}
        if(!d.overheated)Charge(v,d,from,at,c.drillBreak*kBiteSec*2.0f,nullptr,"map (launched)",ms);
        TurnBack(v,d,ms,"met the map");
        return;
    }
    for(int i=0;i<3;++i){d.pos[i]+=d.dir[i]*step;d.lastTip[i]=d.pos[i]+d.dir[i]*kDrillLength;}
    d.flown+=step;
    d.speed-=slow*dt;
    if(d.flown>=c.drillLaunchRange || d.speed<=kOutStop*c.drillLaunchSpeed)TurnBack(v,d,ms,"at its range");
}

// Back one frame: turn the WHOLE drill nose-first toward the hull. Near its socket, turn back to the vehicle's
// forward so it docks in its bind orientation. The rear jet follows this same axis rather than jumping to the tip.
void FlyBack(unsigned char* v,Drill& d,float dt,ULONGLONG ms) noexcept {
    const auto& c=Cfg();
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float quick=c.drillLaunchSpeed*c.drillLaunchSpeed/(2.0f*c.drillLaunchRange);
    float home[3];HomeBase(v,home);
    const float to[3]={home[0]-d.pos[0],home[1]-d.pos[1],home[2]-d.pos[2]};
    const float gap=Len(to);
    d.speed=d.speed+quick*dt>c.drillLaunchSpeed ? c.drillLaunchSpeed : d.speed+quick*dt;
    const float step=d.speed*dt;
    if(gap<=kCatchM+step){Catch(v,d,ms,"caught");return;}
    if(ms-d.backAt>kBackMostMs){Catch(v,d,ms,"snapped: too long on its way back");return;}
    for(int i=0;i<3;++i)d.pos[i]+=to[i]/gap*step;
    const float align=std::fmax(kAlignM,d.speed*kPi/kReturnTurnRate+kDrillLength);
    const float w=Clamp((gap-step)/align,0.0f,1.0f);
    float homeDir[3],axis[3],next[3];
    for(int i=0;i<3;++i)homeDir[i]=to[i]/gap;
    BlendAxis(homeDir,m+8,m+4,1.0f-w,axis);
    const float turn=std::acos(Clamp(Dot(d.axis,axis),-1.0f,1.0f));
    BlendAxis(d.axis,axis,m+4,turn>1e-5f ? std::fmin(1.0f,kReturnTurnRate*dt/turn) : 1.0f,next);
    std::memcpy(d.axis,next,12);
}

// The enemy in the flying drill's reach (kReachLo..kReachHi in its frame: rows x, y, axis from its base `a`) nearest
// its axis a..b (EnemyVisitor), not within kNearHullM of the vehicle's origin.
struct Sweep { const unsigned char* v; float a[3],b[3],x[3],y[3],z[3],at[3],gap; const void* who; };
void InFlightFrame(const Sweep& s,const float* p,float* out) noexcept {
    const float d[3]={p[0]-s.a[0],p[1]-s.a[1],p[2]-s.a[2]};
    out[0]=Dot(d,s.x);out[1]=Dot(d,s.y);out[2]=Dot(d,s.z);
}
void SweepEnemy(void* ctx,const void* object,const float* aim) noexcept {
    auto& s=*static_cast<Sweep*>(ctx);
    float root[3];RootOf(object,aim,root);
    float rootIn[3],lockIn[3];
    InFlightFrame(s,root,rootIn);InFlightFrame(s,aim,lockIn);
    if(!BodyInBox(rootIn,lockIn,kReachLo,kReachHi))return;
    const float mid[3]={(root[0]+aim[0])*0.5f,(root[1]+aim[1])*0.5f,(root[2]+aim[2])*0.5f};
    const float gap=std::fmin(std::fmin(SegmentGap(aim,s.a,s.b),SegmentGap(root,s.a,s.b)),SegmentGap(mid,s.a,s.b));
    if(s.who && gap>=s.gap)return;
    const float* m=reinterpret_cast<const float*>(s.v+kMatrix);
    const float off[3]={aim[0]-m[12],aim[1]-m[13],aim[2]-m[14]};
    if(Len(off)<kNearHullM)return;
    s.gap=gap;s.who=object;std::memcpy(s.at,aim,12);
}

// In flight (kFlightBiteSec): a charge at the enemy nearest its axis, from kLead short of it on the drill's side.
void FlightBite(unsigned char* v,Drill& d,ULONGLONG ms) noexcept {
    Sweep s{v};
    if(!FlightRows(v,d,s.x,s.y))return;
    std::memcpy(s.a,d.pos,12);std::memcpy(s.z,d.axis,12);
    for(int i=0;i<3;++i)s.b[i]=d.pos[i]+d.axis[i]*kDrillLength;
    VisitEnemies(v,&SweepEnemy,&s);
    if(!s.who)return;
    d.touchAt=ms;
    if(d.overheated)return;
    float back[3]={d.pos[0]-s.at[0],d.pos[1]-s.at[1],d.pos[2]-s.at[2]};
    const float l=Len(back);
    if(!Unit(back,back))std::memcpy(back,d.axis,12);
    float from[3];
    for(int i=0;i<3;++i)from[i]=s.at[i]+back[i]*(l<kLead ? (l>0.3f ? l : 0.3f) : kLead);
    Charge(v,d,from,s.at,Cfg().drillLaunchDamage,s.who,"enemy (launched)",ms);
}

// The jet is physically attached to the same rear socket throughout flight, oriented by the drawn drill axis.
void Jet(const unsigned char* v,Drill& d,ULONGLONG ms) noexcept {
    float at[1][3],vel[1][3];
    const float s=d.speed>1.0f ? d.speed : 1.0f;
    std::memcpy(at[0],d.pos,12);
    for(int i=0;i<3;++i)vel[0][i]=d.axis[i]*s;
    FlareFlames(v,at,vel,1,ms);
    d.flaming=true;
}

// The flight this frame (launched): its move, its bites, its jet and its pose. A pose that cannot be written (no
// parent bone) ends the flight at once: a drill flying unseen would bite from nowhere.
void Fly(unsigned char* v,Drill& d,float dt,ULONGLONG ms) noexcept {
    if(d.flight==Flight::out)FlyOut(v,d,dt,ms);
    else FlyBack(v,d,dt,ms);
    if(d.flight==Flight::home)return;
    d.flightBite+=dt;
    if(d.flightBite>=kFlightBiteSec){d.flightBite=0.0f;FlightBite(v,d,ms);}
    Jet(v,d,ms);
    if(PoseFlight(v,d))return;
    if(!d.poseFailed){d.poseFailed=true;Log("DRILL v=%p: no %ls bone to pose the launched drill by: launch off",v,kParentBone);}
    Catch(v,d,ms,"no pose");
}

void Publish(const Drill& d) noexcept {
    AcquireSRWLockExclusive(&cueLock);
    cue=DrillCue{d.rpm,Cfg().drillMaxRpm,d.heat,GameMs()-d.touchAt<=500,d.overheated,d.flight!=Flight::home,
                 d.flight==Flight::back,d.keys};
    cueAt=GetTickCount64();
    ReleaseSRWLockExclusive(&cueLock);
}

// The drill's one round (the user, 2026-10-09: "钻头为什么有25的弹药"): the weapon is the Blacker's cannon made to fire
// nothing (tools/make_drill.py BIT), whose stock magazine of 25 the HUDs showed. Its AmmoCount is 1 (the one drill),
// and its live count (weapon +0xBE8, layout.h kWeaponAmmo, what the stock gauge and vhud.cpp read) is kept at that 1:
// an NPC driver's AI that pulls the bit's trigger (0x69820E takes a round) would otherwise empty it for good (the bit's
// ReloadTime is -1: +0x20C = -1, and the weapon's update skips its whole reload block on a negative one, 0x693B2F), and
// the HUDs would call the drill tank EMPTY / NO AMMO. Whether the drill is away is the drill line's (LAUNCHED /
// RETURNING), not the count's: a count of 0 while it flies would raise that same NO AMMO warning (hud.cpp: every arm
// at 0 that cannot reload) and shut the weapon's fire gate (0x693FC6), with nothing gained. (No other effect of a 0
// was found: the reload block is skipped either way; the other readers of +0xBE8 outside the weapon class, 0x567811 and
// 0x5A1F75, walk a soldier's own weapons, human +0x1970 / +0x1980, not a vehicle's.)
void KeepRound(unsigned char* v) noexcept {
    const unsigned char* seat=SeatAt(v,0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    if(!At<std::uint64_t>(seat,kSeatWeaponCount) || !Readable(holders,8) || !Readable(holders[0],kHolderWeapon+8))return;
    const auto w=At<unsigned char*>(holders[0],kHolderWeapon);
    if(Readable(w,kWeaponAmmo+4) && At<std::int32_t>(w,kWeaponAmmo)!=1)Put<std::int32_t>(w,kWeaponAmmo,1);
}

// Every copy poses the same drill; only the current vehicle authority integrates its path and tests contacts.
// Periodic absolute states recover a lost launch/catch and keep RPM/heat in step, including a late joiner.
void SendState(unsigned char* v,Drill& d,ULONGLONG ms) noexcept {
    if(!d.networked)return;
    const ULONGLONG period=d.flight==Flight::home ? 500 : 50;
    if(d.sentAt && d.sentFlight==d.flight && ms-d.sentAt<period)return;
    drill_net::State s;
    s.phase=static_cast<drill_net::Phase>(d.flight);s.overheated=d.overheated ? 1u : 0u;
    std::memcpy(s.pos,d.pos,12);std::memcpy(s.dir,d.dir,12);std::memcpy(s.axis,d.axis,12);
    s.speed=d.speed;s.rpm=d.rpm;s.heat=d.heat;s.angle=d.angle;
    s.flown=d.flown;s.backAgeMs=d.flight==Flight::back ? static_cast<std::uint32_t>(ms-d.backAt) : 0;
    if(DrillNetSend(v,s)){d.sentAt=ms;d.sentFlight=d.flight;}
}
}  // namespace

bool InstallDrill() noexcept {
    triggerOk=Matches(kTriggerRead,kTriggerSig,sizeof(kTriggerSig));
    Log("HOOK drill trigger=%d%s",triggerOk,triggerOk ? "" : " (unexpected EDF.dll code: the drill tank's drill is off)");
    if(triggerOk)InstallDrillNet();
    return triggerOk;
}

bool IsDrillTank(const void* v) noexcept {
    return v && Is505(v) && BoneRecord506(static_cast<const unsigned char*>(v)+kModelInst,kDrillBone)!=nullptr;
}

// Before the stock input (crew.cpp InputHook): the player's trigger is the drill's, taken off the seat so the
// stock input never pulls weapon holder 0; the launch key / button pressed this frame asks for a launch.
void DrillInput(unsigned char* v) noexcept {
    if(!triggerOk || !Cfg().drill)return;
    Drill* const d=DrillTank(v);
    if(!d)return;
    unsigned char* const seat=SeatAt(v,0);
    const Rider rider=SeatRider(seat);
    d->player=rider==Rider::player;d->npc=rider==Rider::dummy;
    if(!d->player){d->launchHeld=false;return;}
    float* const trigger=reinterpret_cast<float*>(seat+kSeatTrigger);
    d->held=*trigger>=kTriggerOn;
    *trigger=0.0f;
    const auto& c=Cfg();
    const bool keys=At<unsigned char>(seat,kSeatPad)==0;
    d->keys=keys;
    const bool down=keys ? KeyHeld(c.drillLaunchKey)
                         : (At<std::uint16_t>(seat,kSeatButtons)&static_cast<std::uint16_t>(c.drillLaunchButton))!=0;
    if(down && !d->launchHeld)d->launchAsked=true;
    d->launchHeld=down;
}

// After the stock input: the RPM toward the top (held or launched, not overheated) or nothing, the heat and the kills,
// the launch (asked on the hull, not overheated), the drill turned by it (once a frame, the drawn step capped:
// kSpinStepMost) on the hull or in flight, a probe / bite every kBiteSec on the hull.
void DrillFrame(unsigned char* v) noexcept {
    if(!triggerOk || !Cfg().drill)return;
    Drill* const d=DrillTank(v);
    if(!d)return;
    const ULONGLONG ms=GameMs(),frame=GameFrame();
    if(d->frame==frame)return;   // once a frame (a second call would step the spin twice)
    d->frame=frame;
    const float dt=GameStep(d->lastMs ? ms-d->lastMs : 0);
    d->lastMs=d->seen=ms;
    const auto& c=Cfg();
    const float top=c.drillMaxRpm;
    const bool networked=drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128));
    if(d->networked!=networked) {
        // Leaving a session cannot leave a remote animation or a queued local keypress running offline.
        Catch(v,*d,ms,"session changed");
        d->received=drill_net::Gate{};d->launchAsked=false;d->launchHeld=false;
        d->sentAt=d->receivedAt=0;d->networked=networked;
    }
    if(networked && !IsOnlineAuthority(v)) {
        d->launchAsked=false;
        if(d->flight!=Flight::home) {
            Jet(v,*d,ms);
            if(!PoseFlight(v,*d))Catch(v,*d,ms,"no remote pose");
        } else PoseHome(*d);
        KeepRound(v);
        return;
    }
    if(d->launchAsked) {
        d->launchAsked=false;
        if(c.drillLaunch && d->player && d->flight==Flight::home && !d->overheated)Launch(v,*d);
    }
    const bool flying=d->flight!=Flight::home;
    // An NPC's drill spins while it touches something; an empty tank's never (it once bored on by itself, left
    // against a wall or with the drill in a slope, until the mission's end). Launched, it spins at the top on its jet.
    // Overheated, none.
    if(!d->player)d->held=d->npc && d->touchAt && ms-d->touchAt<kNpcHoldMs;
    if(flying)d->held=true;
    if(d->overheated)d->held=false;
    const float rate=d->held ? top/c.drillSpinUpSec : -top/c.drillSpinDownSec;
    d->rpm+=rate*dt;
    d->rpm=d->rpm<0.0f ? 0.0f : d->rpm>top ? top : d->rpm;
    const float share=top>0.0f ? d->rpm/top : 0.0f;
    Heat(v,*d,share,d->touchAt && ms-d->touchAt<=kBiteMs+kFrameMs,dt);
    Kills(v,*d,ms);
    const float step=d->rpm/60.0f*2.0f*kPi*dt;
    d->angle=std::fmod(d->angle+(step<kSpinStepMost*share ? step : kSpinStepMost*share),2.0f*kPi);
    if(flying)Fly(v,*d,dt,ms);
    if(d->flight==Flight::home) {
        PoseHome(*d);
        d->bite+=dt;
        if(d->bite>=kBiteSec) {
            d->bite=0.0f;
            // An NPC probes for something to bore into even standing still (that is what spins it up).
            if(d->rpm>0.0f || (d->npc && !d->overheated))Bite(v,*d,share,ms);
        }
    }
    if(d->rpm>0.0f || d->flight!=Flight::home)LogPose(v,*d,ms);
    if(d->player)Publish(*d);
    KeepRound(v);
    SendState(v,*d,ms);
}

void DrillNetReceived(unsigned char* v,const drill_net::State& s) noexcept {
    if(!triggerOk || !drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)))return;
    Drill* const d=DrillTank(v);
    if(!d || !d->received.Admit(s,true,IsOnlineAuthority(v),DrillNetController(v)))return;
    const ULONGLONG ms=GameMs();
    d->networked=true;d->receivedAt=ms;d->seen=ms;
    const Flight previous=d->flight;
    d->flight=static_cast<Flight>(s.phase);d->overheated=s.overheated!=0;
    std::memcpy(d->pos,s.pos,12);std::memcpy(d->dir,s.dir,12);std::memcpy(d->axis,s.axis,12);
    d->speed=s.speed;d->rpm=s.rpm;d->heat=s.heat;d->angle=s.angle;
    // Enough integrator state to continue a flight if seat ownership moves here while the drill is away.
    for(int i=0;i<3;++i)d->lastTip[i]=d->pos[i]+d->dir[i]*kDrillLength;
    d->flown=s.flown;
    if(d->flight==Flight::back)d->backAt=ms>=s.backAgeMs ? ms-s.backAgeMs : 0;
    if(d->flight==Flight::home && previous!=Flight::home)Catch(v,*d,ms,"network catch");
}

bool PlayerDrillCue(DrillCue* out) noexcept {
    AcquireSRWLockShared(&cueLock);
    const bool fresh=cueAt && GetTickCount64()-cueAt<=kCueFreshMs;
    if(fresh)*out=cue;
    ReleaseSRWLockShared(&cueLock);
    return fresh;
}

void ResetDrills() noexcept {
    for(auto& d:drills)d=Drill{};
}
}  // namespace crew
