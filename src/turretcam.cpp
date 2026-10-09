// The turret camera (README 炮塔镜头; docs/camera-re.md §3b, §5): in a ground vehicle with a turret the player drives,
// the mouse turns the camera and the turret follows at its own stock rate (War Thunder's way), a free-look key turns
// the camera alone, and the camera sits where the vehicle does not fill the view; the high view (highcam.cpp's toggle)
// observes the current weapon's real projectile endpoint.
//  - Where the riding camera comes from (H, docs/camera-re.md §3b): the player's CharacterGhostCamera keeps the player
//    as its target; while they ride, its state 2 (0xFB9F0) places the camera at the seat's MAB camera locators (seat
//    +0x208 the eye, +0x218 the look-at, each {locator, bone}; 0x6BB5A0 gives the point's matrix), both hung on turret
//    bones (the Blacker's eye on `cannon`, its look-at on `cannon_main`): the camera turns with the turret because its
//    points are on it. Every frame of seat camera Type 1 the look-at is fetched at 0xFC01B with the eye's target in the
//    same frame (rbp-0x40 / rbp+0x30, the camera in rsi); then the camera eases its eye (+0x630) and look-at (+0x640)
//    toward them, keeps the eye out of the map (0xF6760) and looks from the one to the other.
//  - So the call at 0xFC01B is redirected (a shim hands rsi on as a third argument): the stock look-at is fetched, and
//    while the plugin owns the view both targets and the eased points are replaced with the rig's (turretcam.h Place):
//    the game's collision and look-at matrix work on them as on its own.
//  - The view (decoupled): a heading and an elevation of the plugin's, world-held (the hull turning under it does not
//    turn it). The seat's aim (VehicleWeaponAimAddSe, vtable 0x17D8A90 slot 2 0x5FCD80) gets the rider's stick each
//    frame; the hook turns the view by the rider's own stick (seat+0x2D0, docs/camera-re.md §4: -1..1 a frame, the
//    mouse's excess carried over by 0x56DBC0, handed to the aim as (-x, y)) and hands the aim the input that turns the
//    turret onto the point under the screen's centre (CameraRay + MapRay, else kAimFar along it) at the stock rates
//    (turretcam.h AxisCommand): the round's low ballistic arc through a real map hit, the bore line through it in
//    EDF6AutoTurret's lead-circle mode (the circle already solved the arc) and through the made-up point of a view that
//    hits nothing (turretcam.h BallisticAim). EDF6AutoTurret turns this turret only onto the player's lock in its
//    auto-aim mode and says so (common/edf/aimlink.h V2 Steers): that frame the turret is its, the view the rider's; an
//    EDF6AutoTurret without the V2 link is told apart by its input being off the stick (turretcam.h Foreign).
//  - Free look (FreeLookKey / FreeLookButton held): the view turns, the turret holds the point it was on (decoupled) or
//    stands still (coupled); let go, the view swings back to where it was (coupled: onto the stock camera's) and hands
//    over again.
//  - The rig (docs/camera-re.md §5): the stock locators' (A) or the vehicle's game_object_camera_setting (B, +0x170 /
//    +0x180, the authored third-person framing the riding camera never reads), whichever puts the eye farther back; the
//    eye never under the rig's own height over its point. The high view orbits the selected weapon's actual impact
//    (or flight endpoint) at HighCamHeight / HighCamBack, scaled up for a big vehicle; native input controls the gun.
// A vehicle qualifies when the player drives it from seat 0, its seat camera is Type 1 (seat+0x200), its seat holds a
// weapon and its yaw axis turns (more than kMinTraverse): a turret, or the gun is indirect fire (a fixed one: the
// howitzer). Helicopters and the plugin's jets do not. The view is
// decoupled only while the aim hook sees that seat's aim stepped (a class that turns its turret some other way keeps the
// stock camera). The AddSe aim step's hook is also the gun stabilizer's way in (stab.cpp StabStep runs the stock step and
// then holds the gun), for every seat it holds, the player's or not; a gun it holds is steered from where it holds it
// (Steer: stab.cpp StabHeld). All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "turretcam.h"
#include "weapon_mount.h"
#include "stab.h"
#include "sight.h"
#include "sightzoom.h"
#include "turretaim.h"
#include "edf/weapon.h"
#include <cmath>
#include <cstring>

namespace crew {
// Implemented by the mounted-optic camera (sightzoom.cpp); camera-space rays must not steer that same gun.
bool SightZoomMounted(const void* vehicle) noexcept;
namespace {
using tcam::kPi;
// The seat aim (VehicleWeaponAimAddSe) and its step; the axes' params {brake, accel, top} and an axis' rate (rad/frame).
constexpr unsigned kAimVtable=0x17D8A90,kAimStep=0x5FCD80;
constexpr std::size_t kAimStepSlot=2,kAimParams=0x90,kAxisRate=0xC;
// The riding camera's look-at fetch in state 2 (0xFC013: lea rcx,[rbx+18h]; lea rdx,[rbp-40h]; call 0x6BB5A0; the eased
// eye's and look-at's addresses in the camera; the eye's target at rbp+30h).
constexpr unsigned kLookSite=0xFC013,kLookCall=0xFC01B,kPoint=0x6BB5A0;
const unsigned char kLookSiteCode[]={0x48,0x8D,0x4B,0x18,0x48,0x8D,0x55,0xC0,0xE8,0x80,0xF5,0x5B,0x00,0xF3,0x0F,0x10,0x86,0x50,0x06,0x00,0x00,
                                      0x48,0x8D,0xBE,0x30,0x06,0x00,0x00,0x0F,0x28,0x55,0x30,0x48,0x8D,0x9E,0x40,0x06,0x00,0x00};
const unsigned char kAimStepCode[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x48,0x8B,0xFA,0x48,0x8B,0xD9,0xE8};
// The seat's camera (0x62B430 from the MAB locator's own SGO): Type at +0x200 (1: the eye / look-at locators), the eye
// point {locator, bone} at +0x208, the look-at's at +0x218; a bone's world rows at +0xB0 (translation +0xE0).
constexpr std::size_t kSeatCamType=0x200,kSeatCamEye=0x208,kSeatCamLook=0x218,kPointBone=0x8,kBoneOrigin=0xE0;
// The camera: its eased eye and look-at (state 2).
constexpr std::size_t kCamEye=0x630,kCamLook=0x640,kCamCollisionMargin=0x528;
const unsigned char kCollisionMarginCode[]={0xC7,0x87,0x28,0x05,0,0,0xCD,0xCC,0xCC,0x3D}; // F5058: 0.1 m
// game_object_camera_setting as the object holds it (0x54DDF0): look-at, eye, in its frame.
constexpr std::size_t kObjCamLook=0x170,kObjCamEye=0x180;
// The seat's input (docs/stores-re.md §4): 1 = a pad, its button bits; the rider's aim stick (0x572DF0 writes it, the
// game's inversion options applied), which the classes hand to the aim as (-x, y) (0x5FD948, 0x6214D8, 0x61AD38, ...).
constexpr std::size_t kSeatPad=0x2B0,kSeatButtons=0x2E8,kSeatStick=0x2D0;
constexpr float kForeign=0.02f;                // the aim's input this far off the rider's stick: another hand steers it
constexpr float kMinTraverse=0.17f;            // rad of yaw stops: less is no turret
constexpr float kAimFar=800.0f,kSightFar=3000.0f;
constexpr float kGroundProbe=1000.0f;          // m above and below the hull the high view's ground point is looked for
constexpr float kFocusSearch=2.0f*kSightFar;   // m (3D): how far the high view's landing is looked for (ShotFocus)
using tcam::kViewMost;
constexpr float kReturnRate=0.25f,kReturnDone=0.005f;   // the free look's swing back: share a frame, done within (rad)
constexpr float kRigEase=0.12f;                // a frame, the rig's move between its shapes (normal / high)
constexpr int kBlendFrames=15;                 // frames the view eases in when the plugin takes the camera over
constexpr float kOnTarget=0.0087f;             // rad (0.5 deg): the turret is on the point
constexpr float kPivotReach=40.0f;             // m: a turret pivot farther from the muzzle than this is not one (TurretPivot)
constexpr float kLargeRig=17.0f;               // m: a rig this long counts as a big vehicle (HighCamClass 2)
constexpr ULONGLONG kFreshMs=200,kAimFreshMs=150,kLogMs=1000;

using PointFn=float*(__fastcall*)(const void*,float*);
AimStepFn nextAim=nullptr;
bool lookOk=false;

// What the game thread hands the camera (under `lock`).
struct Shared {
    ObjRef ref;
    unsigned char* v;
    unsigned char* seat;
    ULONGLONG seenMs,aimMs;    // the frame saw the player there / the aim hook stepped that seat's aim
    bool decoupled,high;       // DecoupledTurretCam / highcam.cpp's toggle, this frame
    bool physicalOnly=true;
    bool free,returning;       // free look held / swinging back
    bool view;                 // yaw / pitch hold the plugin's view
    bool steering;             // the aim's last step had the camera's command (not the stick's, not another plugin's)
    bool highView;             // the aim hook is observing the projectile, with native gun input
    bool observing;            // camera still showing the shot or blending away from it (camera thread publishes)
    bool focusValid,focusHit;  // endpoint of the real shot; a hit or the actual flight endpoint
    float focus[3],orbitYaw,orbitPitch;
    float yaw,pitch;
    unsigned take;             // which take-over this is (each new vehicle or seat a new one): the camera eases in anew
};
// The camera's own (its hook only): the rig it has eased to, its last placement, how far into a take-over.
struct CamSide {
    const unsigned char* seat;
    unsigned take;             // Shared::take it was placing for: another one starts from the stock camera again
    bool owned;
    int blend;                 // frames left of easing in (taken over) or out (handing back: `leaving`)
    bool leaving;
    tcam::Rig rig;
    float offset;              // the view's pitch over the player's, eased (the high view looks further down)
    float eye[3],look[3];      // the last placement
    bool point;                // that placement was the high view's projectile endpoint (tcam::ImpactPlace)
    float carry;               // a switch between the two placements: the share of the old one's offset still carried
    float carryEye[3],carryLook[3];
};
// The rig's numbers the game thread reads (benign races: floats it only steers by): the stock locators' base pitch
// against the aim (the coupled free look swings back onto it), the rig's radius (highcam.cpp's big vehicles), the
// normal rig (the high view hands the view back onto its point through it: tcam::AimAt).
struct RigInfo { float stockBase,normalRadius; tcam::Rig normal; const void* v; };
RigInfo rigInfo{};
// The game thread's own: keys, the free look's latch, the turret's last want (for its drift), the aim point.
struct GameSide {
    bool held;                 // the free-look input as last read
    float holdAt[3];           // the point the turret holds while looking round
    float backYaw,backPitch;   // the view to swing back to (decoupled)
    float aim[3];bool hasAim;  // the point under the screen's centre this frame
    bool aimHit,holdHit;       // ... a real map hit (else kAimFar along an empty view) / the held point's
    tcam::SteerState steer;    // the turret command's last wants and their drift (turretcam.h SteerAxes)
    bool foreign;              // the aim's input was not the rider's stick last frame
    ULONGLONG logAt;
};
Shared shared{};
unsigned takes=0;              // Shared::take's counter (under `lock`)
CamSide camSide{};
GameSide game{};
SRWLOCK lock=SRWLOCK_INIT;

struct Out { TurretCamReadout r; ULONGLONG at; };
Out out{};
SRWLOCK outLock=SRWLOCK_INIT;

bool KeyHeld(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;   // the map view holds the player's keys (map.cpp)
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}

const float* AxisAt(const unsigned char* seat,int i) noexcept {
    return reinterpret_cast<const float*>(seat+kSeatAim+kAimAxes+static_cast<std::size_t>(i)*kAxisStride);
}

// Resolve the same active optic before and after acquisition; no bind/drop fallback loop.
const unsigned char* Gun(const unsigned char* vehicle,const unsigned char* seat) noexcept {
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!count || count>16 || !Readable(holders,count*8))return nullptr;
    const auto selected=PayloadSightPicked(vehicle,0);
    if(!selected)return nullptr;
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(Readable(w,edf::kWeaponAccuracyScale+4) && At<std::uint64_t>(w,edf::kMuzzleCount)>0) {
            if(w==selected)return w;
        }
    }
    return nullptr;
}

// The gun the seat's turret turns (2026-10-09, the user: "按了右键使用机枪以后，自瞄和瞄具等都无法使用了。需要按左键发射主
// 炮才能恢复"): whether the camera is decoupled, what the turret is steered by and the mouse command belong to the seat's
// articulated turret, not to whichever trigger fired last. The fire-control pick (Gun) when its mount turns with both of
// the seat's axes; otherwise the first live weapon of the seat that does (the Titan's hull `front_gun` hangs on `body`,
// its cannon under `cannon_main`: the right trigger must not take the turret off the view); with none, the pick itself
// (a fixed or single-axis gun stays physical-only, as before). No pick (the seat has no usable weapon): none.
const unsigned char* TurretGun(const unsigned char* vehicle,const unsigned char* seat) noexcept {
    const unsigned char* picked=Gun(vehicle,seat);
    if(!picked)return nullptr;
    const auto turns=[&](const unsigned char* w) noexcept {
        const auto f=weaponmount::OfWeapon(vehicle,seat,w);
        return f.known && f.yaw && f.pitch;
    };
    if(turns(picked))return picked;
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    for(std::uint64_t i=0;i<count && i<16;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto ctrl=At<const unsigned char*>(holders[i],kHolderCtrl);
        if(!Readable(ctrl,12) || At<std::int32_t>(ctrl,8)<=0)continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(w!=picked && Readable(w,edf::kWeaponAccuracyScale+4) && At<std::uint64_t>(w,edf::kMuzzleCount)>0 && turns(w))return w;
    }
    return picked;
}

// Same shot model and muzzle transform as the vehicle HUD. No camera ray participates in this prediction.
// RoundLands also returns the flight endpoint on a miss; an unreadable/guided model uses the actual bore path.
// The observed point is the landing while it is within reach, else the ground under where the shot ends, at most
// kSightFar (kAimFar for a round with no model: the bore line) across the ground: tcam::HighFocus. The landing is looked
// for out to kFocusSearch, twice that cap: the search is a 3D sphere checked a segment of frames at a time, so a round
// leaving it in the air (a howitzer's shell crosses 3 km hundreds of metres up) is then past the cap across the ground
// and the view stays at the cap, not stepping back by the round's height and the segment. Never a point in the
// air: the ground's height there by a map ray straight down (kGroundProbe m above and below the hull), the hull's own
// height with none.
void ShotFocus(Shared& s) noexcept {
    s.focusValid=false;s.focusHit=false;
    // The turret's gun (TurretGun), the one the turret is laid by: after the hull gatling's right trigger the overhead
    // point must still be the cannon's, or the view handed back on leaving it (tcam::AimAt) puts the cannon off it.
    const auto gun=TurretGun(s.v,s.seat);
    float muzzle[3],dir[3];
    if(!gun || !edf::MeanMuzzle(gun,64,muzzle,dir) || !vec::Normalize(dir))return;
    float end[3];
    for(int i=0;i<3;++i)end[i]=muzzle[i]+dir[i]*kAimFar;
    float most=kAimFar;
    RoundModel model{};
    if(ReadRound(gun,&model) && model.kind!=RoundKind::none && model.kind!=RoundKind::homing) {
        float flight=0.0f;
        s.focusHit=RoundLands(gun,model,muzzle,dir,kFocusSearch,end,&flight);
        most=kSightFar;
    }
    for(float v:end)if(!std::isfinite(v))return;
    const float* origin=reinterpret_cast<const float*>(s.v+kMatrix)+12;
    tcam::HighFocus(muzzle,dir,s.focusHit,end,most,origin[1],s.focus);
    if(!(s.focusHit && tcam::AcrossDistance(muzzle,end)<=most)) {
        s.focusHit=false;   // not the landing: no on-target claim for it
        const float top=std::fmax(origin[1],muzzle[1])+kGroundProbe;
        const float from[3]={s.focus[0],top,s.focus[2]},to[3]={s.focus[0],origin[1]-kGroundProbe,s.focus[2]};
        float hit[3];
        if(MapRay(from,to,hit)>=0.0f && std::isfinite(hit[1]))s.focus[1]=hit[1];
    }
    for(float v:s.focus)if(!std::isfinite(v))return;
    s.focusValid=true;
}

// A seat 0 the plugin's camera serves: Type 1 locators, a weapon, a yaw axis that turns, or an indirect-fire gun that
// does not (the self-propelled howitzer's turret is fixed, tools/make_artillery.py TURRET_LIMITS: its rider still picks
// the target with the view and takes the high view; the guns only elevate, the hull is turned to lay them).
bool Turret(const unsigned char* v,const unsigned char* seat) noexcept {
    if(IsHelicopter(v) || IsPlayerJet(v) || At<std::int32_t>(seat,kSeatCamType)!=1 || !Gun(v,seat))return false;
    const float* yaw=AxisAt(seat,0);
    return std::isfinite(yaw[0]) && std::isfinite(yaw[1]) && (yaw[1]-yaw[0]>kMinTraverse || IndirectFireSeat(seat));
}

// The aim's direction in the world: the hull's rows turned by the axes (yaw: right positive; pitch: negative up).
void AxesDir(const unsigned char* v,const unsigned char* seat,float* d) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float yaw=AxisAt(seat,0)[2],elev=-AxisAt(seat,1)[2];
    const float l[3]={std::sin(yaw)*std::cos(elev),std::sin(elev),std::cos(yaw)*std::cos(elev)};
    for(int i=0;i<3;++i)d[i]=m[i]*l[0]+m[4+i]*l[1]+m[8+i]*l[2];
    vec::Normalize(d);
}

// The point under the screen's centre: the first map hit along it (`*hit` true), else kAimFar out. False with no camera
// yet.
bool AimPoint(float* p,bool* hit) noexcept {
    float eye[3],dir[3],at[3];
    *hit=false;
    if(!CameraRay(eye,dir))return false;
    const float end[3]={eye[0]+dir[0]*kSightFar,eye[1]+dir[1]*kSightFar,eye[2]+dir[2]*kSightFar};
    if(MapRay(eye,end,at)>=0.0f && vec::Dist(eye,at)<=kSightFar){std::memcpy(p,at,12);*hit=true;return true;}
    for(int i=0;i<3;++i)p[i]=eye[i]+dir[i]*kAimFar;
    return true;
}

// EDF6AutoTurret's lead-circle mode on the player's own gun (its readout: common/edf/aimlink.h).
bool LeadCircleOn() noexcept {
    edf::aimlink::TurretReadoutV1 r{};
    // Only while a circle is there (a target and its solve): with none, the screen's centre takes the arc as in AUTO (a
    // tank's driver seat is the player's own gun since aimlink.h V4 and must not lose its drop compensation to a mode).
    return AutoTurretReadout(&r) && r.ownGun && r.mode==edf::aimlink::Mode::leadCircle && r.target && r.lead;
}

bool FreeHeld(const unsigned char* seat) noexcept {
    const Config& c=Cfg();
    if(At<unsigned char>(seat,kSeatPad)==0)return KeyHeld(c.freeLookKey);
    return c.freeLookButton>0 && (At<std::uint16_t>(seat,kSeatButtons)&static_cast<std::uint16_t>(c.freeLookButton))!=0;
}

void Publish(const TurretCamReadout& r) noexcept {
    AcquireSRWLockExclusive(&outLock);
    out=Out{r,GameMs()};
    ReleaseSRWLockExclusive(&outLock);
}

void Drop(const char* why) noexcept {
    AcquireSRWLockExclusive(&lock);
    const bool had=shared.v!=nullptr;
    const void* v=shared.v;
    shared=Shared{};
    ReleaseSRWLockExclusive(&lock);
    game=GameSide{};
    if(had)Log("TURRETCAM v=%p: released (%s)",v,why);
}

// --- the turret, from the aim step (game thread) ---

// The centre the seat's turret turns about: its look-at camera point's bone (seat +0x218 {locator, bone}), the turret's
// yaw axis the stock camera orbits (StockRig takes it so too; docs/camera-re.md §3b: the Blacker's `cannon_main`, the
// Grape's `Barrel` whose origin is its trunnion on the bore line, 0.42 m ahead of the yaw axis). False when unreadable.
bool TurretPivot(const unsigned char* seat,const float* muzzle,float* pivot) noexcept {
    const auto bone=At<const unsigned char*>(seat,kSeatCamLook+kPointBone);
    if(!Readable(bone,kBoneOrigin+12))return false;
    std::memcpy(pivot,bone+kBoneOrigin,12);
    return std::isfinite(pivot[0]+pivot[1]+pivot[2]) && vec::Dist(pivot,muzzle)<kPivotReach;
}

// The axes' wants (yaw, pitch: the aim's own senses) that put the gun on `p`, seen in `frame` (world rows x, up, nose:
// the frame the stabilizer's held axes are seen in, else the hull's now; stab.h HeldIn): the bore line through `p` from
// the bore's point at the turret's pivot (turretcam.h AimOrigin: not from the muzzle, which swings with the gun), with
// `ballistic` the low arc for a gun whose rounds drop (a lofted launcher's arc is katyusha.cpp's: it gets the line), else
// the bore line (turretcam.h BallisticAim).
bool Wants(const unsigned char* seat,const float* frame,const float* p,bool ballistic,float* want) noexcept {
    const unsigned char* gun=TurretGun(shared.v,seat);
    float muzzle[3],dir[3];
    if(!gun || !edf::MeanMuzzle(gun,16,muzzle,dir))return false;
    float pivot[3],origin[3];
    tcam::AimOrigin(muzzle,dir,TurretPivot(seat,muzzle,pivot) ? pivot : nullptr,origin);
    float l[3],x=0.0f;
    tcam::LocalTo(frame,origin,p,l,&x);
    if(!(x>1e-3f || std::fabs(l[1])>1e-3f))return false;
    float elevation=std::atan2(l[1],x),frames=0.0f,g[3];
    RoundModel model{};
    const bool arc=ReadRound(gun,&model) && model.kind==RoundKind::arc;
    const bool lofted=At<std::int32_t>(gun,edf::kWeaponMark)==edf::kMarkLofted;
    // Guided rounds and motor-driven rockets do not follow the fixed-gravity arc solved here.
    if(ballistic && arc && !lofted && model.speed>0.0f && model.factor>0.0f && edf::WorldGravity(image,g)) {
        const double drop=model.factor*-(g[0]*frame[3]+g[1]*frame[4]+g[2]*frame[5])/3600.0;
        float e=0.0f;
        if(drop>0.0 && edf::BallisticArc(x,l[1],model.speed,drop,false,e,frames) && std::isfinite(e))elevation=e;
    }
    want[0]=std::atan2(l[0],l[2]);
    want[1]=-elevation;
    return std::isfinite(want[0]) && std::isfinite(want[1]);
}

// What the turret is steered against this frame: `held` / `hull` (stab.cpp StabHeld: a gun the stabilizer holds is
// steered from the axes it holds the gun at with no command, the hull's turn out of the drift; else the axes as they are,
// 0) and the frame the wants are seen in (the one `held` is seen in; the hull's now with nothing held).
struct Steering { float held[2],hull[2],frame[9]; };
void SteeringOf(const unsigned char* v,const unsigned char* seat,Steering* s) noexcept {
    if(StabHeld(seat+kSeatAim,s->held,s->hull,s->frame))return;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    stab::Frame now{};
    if(stab::FromMatrix(m,&now))std::memcpy(s->frame,now.r,sizeof(now.r));
    else for(int r=0;r<3;++r)std::memcpy(s->frame+3*r,m+4*r,12);
}

// The input that turns each axis onto `want` (turretcam.h SteerAxes), and whether it is on.
bool Steer(const unsigned char* seat,const Steering& s,const float* want,float* in) noexcept {
    const tcam::Axis axes[2]={{AxisAt(seat,0)[0],AxisAt(seat,0)[1],AxisAt(seat,0)[2],At<float>(AxisAt(seat,0),kAxisRate)},
                              {AxisAt(seat,1)[0],AxisAt(seat,1)[1],AxisAt(seat,1)[2],At<float>(AxisAt(seat,1),kAxisRate)}};
    return tcam::SteerAxes(game.steer,want,s.held,s.hull,axes,reinterpret_cast<const float*>(seat+kSeatAim+kAimParams),kOnTarget,in);
}

void Readout(const unsigned char* seat,const Shared& s,bool on,const float* holdAt,bool ballistic,bool commandValid=false) noexcept {
    TurretCamReadout r{};
    r.physicalOnly=s.physicalOnly;
    r.aimValid=commandValid && s.decoupled && !s.physicalOnly && !s.high &&
               std::isfinite(holdAt[0]) && std::isfinite(holdAt[1]) && std::isfinite(holdAt[2]);
    r.decoupled=s.decoupled;r.freeLook=s.free || s.returning;r.high=s.high;r.onTarget=on;
    const unsigned char* gun=TurretGun(s.v,seat);
    float muzzle[3],dir[3];
    if(!gun || !edf::MeanMuzzle(gun,64,muzzle,dir))return;
    std::memcpy(r.aim,holdAt,12);
    std::memcpy(r.muzzle,muzzle,12);std::memcpy(r.gunDir,dir,12);
    if(s.high && s.focusValid) {
        std::memcpy(r.aim,s.focus,12);std::memcpy(r.gun,s.focus,12);
        r.onTarget=s.focusHit;Publish(r);return;
    }
    // Where the gun's round would be at the aim point's range: along its arc for a gun that drops, aimed by its arc;
    // along its bore line when the line is what is put on the point (BallisticAim), so the mark meets the point.
    const float range=vec::Dist(holdAt,muzzle);
    float g[3];
    RoundModel model{};
    const bool arc=ReadRound(gun,&model) && model.kind==RoundKind::arc;
    const bool lofted=At<std::int32_t>(gun,edf::kWeaponMark)==edf::kMarkLofted;
    if(ballistic && arc && !lofted && model.speed>0.0f && model.factor>0.0f && edf::WorldGravity(image,g)) {
        const float vel[3]={dir[0]*model.speed,dir[1]*model.speed,dir[2]*model.speed};
        const float drop[3]={g[0]*model.factor/3600.0f,g[1]*model.factor/3600.0f,g[2]*model.factor/3600.0f};
        sight::RoundAfter(muzzle,vel,drop,range/model.speed,r.gun);
    } else for(int i=0;i<3;++i)r.gun[i]=muzzle[i]+dir[i]*range;
    Publish(r);
}

// The aim step's work for the player's seat: the view turned by the stick, the free look's latch and swing back, the
// turret's input. `in` the stock input, `cmd` what the aim gets.
void Aim(unsigned char* seat,const float* in,float* cmd) noexcept {
    const Config& c=Cfg();
    AcquireSRWLockExclusive(&lock);
    Shared s=shared;
    ReleaseSRWLockExclusive(&lock);
    if(seat!=s.seat || !s.v)return;
    s.aimMs=GameMs();
    if(!s.high && SightZoomMounted(s.v)) {
        // The sightzoom camera follows the actual barrel. Feeding its own last-frame ray back into the
        // decoupled controller pins the view or creates a feedback loop. Keep the native/stabilizer input.
        cmd[0]=in[0];cmd[1]=in[1];
        game.hasAim=false;game.aimHit=false;game.steer.hasWant=false;game.held=FreeHeld(seat);
        s.view=false;s.decoupled=false;s.free=false;s.returning=false;s.steering=false;s.highView=false;s.observing=false;
        ShotFocus(s);
        AcquireSRWLockExclusive(&lock);
        if(shared.seat==seat) {
            shared.aimMs=s.aimMs;shared.view=false;shared.decoupled=false;shared.free=false;shared.returning=false;
            shared.steering=false;shared.highView=false;shared.observing=false;
        }
        ReleaseSRWLockExclusive(&lock);
        if(s.focusValid)Readout(seat,s,s.focusHit,s.focus,false);
        return;
    }
    const bool held=FreeHeld(seat);
    const bool leavingHigh=s.highView && !s.high;
    const bool press=held && (!game.held || leavingHigh),release=!held && game.held && !leavingHigh;
    game.held=held;
    if(s.high) {
        if(!s.highView){s.orbitYaw=0.0f;s.orbitPitch=0.0f;}
        s.highView=true;s.view=true;s.steering=false;s.free=held;s.returning=false;
        game.steer.hasWant=false;
        const float stick[2]={-At<float>(seat,kSeatStick),At<float>(seat,kSeatStick+4)};
        const bool foreign=tcam::Foreign(AutoTurretSteers(s.v,0),in,stick,kForeign);
        if(held) {
            const float rate=c.turretCamRate*kPi/180.0f/60.0f;
            s.orbitYaw=tcam::Wrap(s.orbitYaw-stick[0]*rate);
            s.orbitPitch=vec::Clamp(s.orbitPitch+stick[1]*rate,-tcam::kPitchMost,tcam::kPitchMost);
        } else {s.orbitYaw*=1.0f-kReturnRate;s.orbitPitch*=1.0f-kReturnRate;}
        // The stock axis step (and stabilizer) owns gun motion. Looking round must not feed the observation back.
        cmd[0]=held && !foreign ? 0.0f : in[0];cmd[1]=held && !foreign ? 0.0f : in[1];
        AcquireSRWLockExclusive(&lock);
        if(shared.seat==seat) {
            shared.aimMs=s.aimMs;shared.view=s.view;shared.highView=true;shared.steering=false;
            shared.free=s.free;shared.returning=false;shared.orbitYaw=s.orbitYaw;shared.orbitPitch=s.orbitPitch;
        }
        ReleaseSRWLockExclusive(&lock);
        if(s.focusValid)Readout(seat,s,s.focusHit,s.focus,false);
        return;
    }
    if(s.highView || s.observing) {
        // Throughout the actual camera blend, follow the native axes and leave gun motion native. A frame of stick
        // must not cancel this isolation and let the remaining overhead ray become a new turret target.
        float dir[3];AxesDir(s.v,seat,dir);
        s.yaw=tcam::YawOf(dir);s.pitch=tcam::PitchOf(dir)+rigInfo.stockBase;
        s.highView=false;s.view=true;s.free=held;s.returning=false;s.steering=false;
        game.hasAim=false;game.aimHit=false;game.steer.hasWant=false;
        // Re-arm free look after the blend: its normal entry must capture a fresh, normal-camera aim point.
        game.held=false;
        const float stick[2]={-At<float>(seat,kSeatStick),At<float>(seat,kSeatStick+4)};
        const bool foreign=tcam::Foreign(AutoTurretSteers(s.v,0),in,stick,kForeign);
        cmd[0]=held && !foreign ? 0.0f : in[0];cmd[1]=held && !foreign ? 0.0f : in[1];
        AcquireSRWLockExclusive(&lock);
        if(shared.seat==seat) {
            shared.aimMs=s.aimMs;shared.view=true;shared.highView=false;shared.steering=false;
            shared.yaw=s.yaw;shared.pitch=s.pitch;shared.free=s.free;shared.returning=false;
        }
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    if(!s.view) {   // the view's start: the screen's centre as it is
        float eye[3],dir[3];
        if(!CameraRay(eye,dir))AxesDir(s.v,seat,dir);
        s.yaw=tcam::YawOf(dir);s.pitch=tcam::PitchOf(dir);s.view=true;
        s.highView=false;
    }
    if(press) {
        std::memcpy(game.holdAt,game.aim,12);game.holdHit=game.aimHit;
        if(!game.hasAim){float d[3];AxesDir(s.v,seat,d);const float* p=reinterpret_cast<const float*>(s.v+kMatrix)+12;for(int i=0;i<3;++i)game.holdAt[i]=p[i]+d[i]*kAimFar;game.holdHit=false;}
        game.backYaw=s.yaw;game.backPitch=s.pitch;
        if(!s.decoupled && !s.highView) {   // coupled: the view starts where the screen's centre is
            float eye[3],dir[3];
            if(CameraRay(eye,dir)){s.yaw=tcam::YawOf(dir);s.pitch=tcam::PitchOf(dir);}
        }
        s.free=true;s.returning=false;
        if(c.debug)Log("TURRETCAM free look on (%s)",s.decoupled ? "decoupled" : "coupled");
    }
    if(release){s.free=false;s.returning=true;}
    // The view turns by the rider's own stick (as the aim gets it: a positive yaw input turns the axis to the hull's +x,
    // its left, so the heading falls; a positive pitch input lowers the gun). The aim's own input may be another
    // plugin's (EDF6AutoTurret on the player's lock): then the turret is theirs this frame, the view the rider's.
    const float stick[2]={-At<float>(seat,kSeatStick),At<float>(seat,kSeatStick+4)};
    const bool foreign=tcam::Foreign(AutoTurretSteers(s.v,0),in,stick,kForeign);
    // Magnified (sightzoom.cpp) the view turns slower by as much: the same sweep across the screen at any zoom.
    const float rate=sightzoom::Rate(std::fmax(c.turretCamRate*kPi/180.0f/60.0f,At<float>(seat,kSeatAim+kAimParams+8)),
                                     SightZoomNow(s.v));
    const bool turning=s.decoupled || s.free;
    if(turning && !s.returning) {
        s.yaw=tcam::Wrap(s.yaw-stick[0]*rate);
        s.pitch=vec::Clamp(s.pitch-stick[1]*rate,-tcam::kPitchMost,tcam::kPitchMost);
    }
    if(s.returning) {
        float to[2]={game.backYaw,game.backPitch};
        if(!s.decoupled){float d[3];AxesDir(s.v,seat,d);to[0]=tcam::YawOf(d);to[1]=tcam::PitchOf(d)+rigInfo.stockBase;}
        const float dy=tcam::Wrap(to[0]-s.yaw),dp=to[1]-s.pitch;
        s.yaw=tcam::Wrap(s.yaw+dy*kReturnRate);s.pitch+=dp*kReturnRate;
        if(std::fabs(dy)<kReturnDone && std::fabs(dp)<kReturnDone) {
            s.returning=false;s.yaw=to[0];
            s.pitch=to[1];
            if(!s.decoupled)s.view=false;
        }
    }
    float want[2];
    bool on=false;
    // The free look's held point, otherwise the current normal camera's screen centre.
    const bool hold=s.free || s.returning;
    const float* target=hold ? game.holdAt : game.aim;
    const bool ballistic=tcam::BallisticAim(hold ? game.holdHit : game.aimHit,LeadCircleOn());
    const bool steer=s.decoupled && !foreign && (game.hasAim || hold);
    s.steering=false;
    Steering st{};
    if(steer)SteeringOf(s.v,seat,&st);
    if(foreign){cmd[0]=in[0];cmd[1]=in[1];game.steer.hasWant=false;}
    else if(steer && Wants(seat,st.frame,target,ballistic,want)){on=Steer(seat,st,want,cmd);s.steering=true;}
    else if(s.free || s.returning){cmd[0]=0.0f;cmd[1]=0.0f;}   // coupled: the turret stands while the view looks round
    else{cmd[0]=in[0];cmd[1]=in[1];game.steer.hasWant=false;}
    AcquireSRWLockExclusive(&lock);
    if(shared.seat==seat){shared.aimMs=s.aimMs;shared.free=s.free;shared.returning=s.returning;shared.view=s.view;shared.yaw=s.yaw;shared.pitch=s.pitch;shared.steering=s.steering;
                          shared.highView=s.highView;}
    ReleaseSRWLockExclusive(&lock);
    if(foreign!=game.foreign && c.debug)Log("TURRETCAM the aim's input is %s",foreign ? "another hand's (the turret is theirs, the view the rider's)" : "the rider's again");
    game.foreign=foreign;
    if(s.decoupled || s.free || s.returning)Readout(seat,s,on,target,ballistic,game.hasAim || hold);
}

void __fastcall AimHook(void* aim,const float* in) {
    alignas(16) float cmd[4];
    std::memcpy(cmd,in,sizeof(cmd));
    if(Cfg().enabled && lookOk && aim==(shared.seat ? shared.seat+kSeatAim : nullptr)) {
        __try { Aim(static_cast<unsigned char*>(aim)-kSeatAim,in,cmd); } __except(EXCEPTION_EXECUTE_HANDLER){std::memcpy(cmd,in,sizeof(cmd));}
    }
    StabStep(aim,cmd,nextAim);   // the stock step, then the gun stabilizer's turn (stab.cpp; every AddSe seat it holds)
}

// --- the camera, from the look-at fetch (the camera's update) ---

// The stock locators' rig (A): their eye and look-at as the turret holds them, as a rig round the turret's yaw axis
// (the look-at bone's origin) at the look-at's height; its base pitch against the aim's.
bool StockRig(const unsigned char* v,const unsigned char* seat,const float* eye,const float* look,tcam::Rig* rig,float* base) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const auto bone=At<const unsigned char*>(seat,kSeatCamLook+kPointBone);
    if(!Readable(bone,kBoneOrigin+16))return false;
    const float* origin=reinterpret_cast<const float*>(bone+kBoneOrigin);
    float d[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]};
    const float r=vec::Len(d);
    if(!(r>0.5f) || !vec::Normalize(d))return false;
    float axes[3];AxesDir(v,seat,axes);
    *base=tcam::PitchOf(d)-tcam::PitchOf(axes);
    // The eye's least height over the point: where the locators put it with the gun level (the base pitch's).
    *rig=tcam::Rig{look[1]-m[13],0.0f,0.0f,r,-std::sin(*base)*r,{origin[0]-m[12],0.0f,origin[2]-m[14]}};
    return std::isfinite(rig->rise) && std::isfinite(rig->up) && std::isfinite(*base);
}

// game_object_camera_setting's rig (B), and its base pitch.
bool AuthoredRig(const unsigned char* v,tcam::Rig* rig,float* base) noexcept {
    const float* look=reinterpret_cast<const float*>(v+kObjCamLook);
    const float* eye=reinterpret_cast<const float*>(v+kObjCamEye);
    for(int i=0;i<3;++i)if(!std::isfinite(look[i]) || !std::isfinite(eye[i]))return false;
    if(!tcam::Authored(look,eye,rig))return false;
    const float d[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]};
    *base=tcam::PitchOf(d);
    return true;
}

void Place(float* lookOut,float* eyeOut,unsigned char* cam,const float* eye,const float* focus,bool surfaceFocus=false) noexcept {
    float collisionLook[3];std::memcpy(collisionLook,focus,12);
    if(surfaceFocus) {
        // F6760 queries desired-look -> look, then look -> eye. A ballistic intersection lies ON a collider;
        // a zero-distance hit can collapse eye and look before native LookTo (0/-90 degree snapping).
        // Move only the collision pivot toward the eye by the native retreat margin. The real projectile
        // focus stays unchanged and collinear, so it still projects to the same optical centre.
        float towardEye[3]={eye[0]-focus[0],eye[1]-focus[1],eye[2]-focus[2]};
        const float distance=vec::Len(towardEye),margin=At<float>(cam,kCamCollisionMargin);
        if(distance>1e-5f && std::isfinite(margin) && margin>0.0f) {
            const float share=std::fmin(margin,distance*0.5f)/distance;
            for(int i=0;i<3;++i)collisionLook[i]+=towardEye[i]*share;
        }
    }
    const float* look=collisionLook;
    std::memcpy(lookOut,look,12);lookOut[3]=1.0f;
    std::memcpy(eyeOut,eye,12);eyeOut[3]=1.0f;
    float* camEye=reinterpret_cast<float*>(cam+kCamEye);
    float* camLook=reinterpret_cast<float*>(cam+kCamLook);
    std::memcpy(camEye,eye,12);camEye[3]=1.0f;
    std::memcpy(camLook,look,12);camLook[3]=1.0f;
}

// Handing the camera back: from the last placement onto the stock targets (the look-at just fetched, the eye's in
// the same frame: lookTarget+16) over kBlendFrames, then the stock camera's own (its look-at would jump: it is not eased).
void Leave(const unsigned char* seat,float* lookTarget,unsigned char* cam) noexcept {
    if(!camSide.owned || camSide.seat!=seat){camSide=CamSide{};return;}
    if(!camSide.leaving){camSide.leaving=true;camSide.blend=kBlendFrames;}
    if(camSide.blend<=0){camSide=CamSide{};return;}
    const float k=1.0f/static_cast<float>(camSide.blend--);
    float eye[3],look[3];
    for(int i=0;i<3;++i){eye[i]=camSide.eye[i]+(lookTarget[16+i]-camSide.eye[i])*k;look[i]=camSide.look[i]+(lookTarget[i]-camSide.look[i])*k;}
    for(int i=0;i<3;++i)if(!std::isfinite(eye[i]) || !std::isfinite(look[i])){camSide=CamSide{};return;}
    std::memcpy(camSide.eye,eye,12);std::memcpy(camSide.look,look,12);
    Place(lookTarget,lookTarget+16,cam,eye,look);
}

void PublishObservation(const unsigned char* seat,bool observing) noexcept {
    AcquireSRWLockExclusive(&lock);
    if(shared.seat==seat)shared.observing=observing;
    ReleaseSRWLockExclusive(&lock);
}

void Camera(const unsigned char* seat,float* lookTarget,unsigned char* cam) noexcept {
    const Config& c=Cfg();
    AcquireSRWLockShared(&lock);
    const Shared s=shared;
    ReleaseSRWLockShared(&lock);
    const ULONGLONG now=GameMs();
    const bool live=c.enabled && seat==s.seat && s.v && now-s.seenMs<=kFreshMs && s.ref.Is(s.v);
    const bool aimed=live && now-s.aimMs<=kAimFreshMs && s.view;
    const bool viewed=aimed && (s.decoupled || s.free || s.returning);
    const bool own=live && (viewed || s.high);
    if(!live){camSide=CamSide{};PublishObservation(seat,false);return;}
    if(!s.high && SightZoomMounted(s.v)){camSide=CamSide{};PublishObservation(seat,false);return;}
    // Another take (the player left the seat and came back, Drop between: this hook does not run while nobody is
    // served, so it never saw them go): its camera starts from the stock one again, easing in.
    if(camSide.owned && camSide.take!=s.take)camSide=CamSide{};
    // The stock points: the look-at just fetched, the eye's as its locator puts it (no XAngleAdjust).
    alignas(16) float eyeM[16];
    reinterpret_cast<PointFn>(image+kPoint)(seat+kSeatCamEye,eyeM);
    const float* stockEye=eyeM+12;
    const float* stockLook=lookTarget;
    tcam::Rig rigA{},rigB{},normal{};
    float baseA=0.0f,baseB=0.0f,base=0.0f;
    const bool a=StockRig(s.v,seat,stockEye,stockLook,&rigA,&baseA);
    const bool b=AuthoredRig(s.v,&rigB,&baseB);
    if(b && (!a || rigB.radius>=rigA.radius)){normal=rigB;base=baseB;}
    else if(a){normal=rigA;base=baseA;}
    else{camSide=CamSide{};PublishObservation(seat,false);return;}
    rigInfo=RigInfo{a ? baseA : 0.0f,normal.radius,normal,s.v};
    if(!own || (s.high && !s.focusValid)) {
        Leave(seat,lookTarget,cam);
        PublishObservation(seat,camSide.owned && (camSide.point || camSide.carry>0.0f));
        return;
    }
    const float scale=std::fmax(1.0f,normal.radius/20.0f);
    tcam::Rig high=tcam::High(c.highCamHeight*scale,c.highCamBack*scale,c.highCamPitch);
    std::memcpy(high.fixed,normal.fixed,sizeof(high.fixed));
    const float highBase=-c.highCamPitch*kPi/180.0f;
    // Both coupled and decoupled high cameras observe the same real shot endpoint.
    const bool point=s.high && s.focusValid;
    const tcam::Rig& want=s.high && !viewed ? high : normal;
    const float* m=reinterpret_cast<const float*>(s.v+kMatrix);
    // The view: the plugin's (decoupled, free look), else the aim's at the rig's base pitch (coupled high view).
    const float offset=viewed ? 0.0f : highBase;
    if(!camSide.owned || camSide.seat!=seat || camSide.leaving) {   // taken over: from the stock locators' rig, easing in
        const bool back=camSide.owned && camSide.seat==seat;   // taken back while handing over: from where it is
        const CamSide was=camSide;
        camSide=CamSide{};
        camSide.seat=seat;camSide.take=s.take;camSide.owned=true;camSide.blend=kBlendFrames;
        camSide.rig=back ? was.rig : a ? rigA : want;camSide.offset=back ? was.offset : viewed ? 0.0f : baseA;
        std::memcpy(camSide.eye,cam+kCamEye,12);std::memcpy(camSide.look,cam+kCamLook,12);
        camSide.point=point;
        if(c.debug)Log("TURRETCAM v=%p: camera taken (%s%s), rig %s r=%.1f up=%.1f rise=%.1f, stock r=%.1f",s.v,viewed ? "view" : "coupled",
                       s.high ? ", high" : "",b && (!a || rigB.radius>=rigA.radius) ? "authored" : "stock",normal.radius,normal.up,normal.rise,a ? rigA.radius : 0.0f);
    }
    tcam::Ease(camSide.rig,want,kRigEase);
    camSide.offset+=(offset-camSide.offset)*kRigEase;
    float eye[3],look[3];
    if(point)tcam::ImpactPlace(m+12,s.focus,s.yaw,c.highCamHeight*scale,c.highCamBack*scale,
                              s.orbitYaw,s.orbitPitch,eye,look);
    else {
        float yaw,pitch;
        if(viewed){yaw=s.yaw;pitch=s.pitch+camSide.offset;}
        else {
            float a3[3];AxesDir(s.v,seat,a3);
            yaw=tcam::YawOf(a3);pitch=tcam::PitchOf(a3)+camSide.offset;
        }
        pitch=vec::Clamp(pitch,-kViewMost,kViewMost);
        float d[3];
        tcam::Dir(yaw,pitch,d);
        tcam::Place(camSide.rig,m+12,yaw,d,eye,look);
    }
    // A switch between the shot observation and the rig: from where the camera was, the old placement's offset eased out
    // (carried in the hull's motion, not lagging it).
    if(point!=camSide.point) {
        for(int i=0;i<3;++i){camSide.carryEye[i]=camSide.eye[i]-eye[i];camSide.carryLook[i]=camSide.look[i]-look[i];}
        camSide.carry=1.0f;camSide.point=point;
    }
    if(camSide.carry>0.0f) {
        camSide.carry*=1.0f-kRigEase;
        if(camSide.carry<1e-3f)camSide.carry=0.0f;
        for(int i=0;i<3;++i){eye[i]+=camSide.carryEye[i]*camSide.carry;look[i]+=camSide.carryLook[i]*camSide.carry;}
    }
    if(camSide.blend>0) {   // the first frames ease from where the camera was
        const float k=1.0f/static_cast<float>(camSide.blend--);
        for(int i=0;i<3;++i){eye[i]=camSide.eye[i]+(eye[i]-camSide.eye[i])*k;look[i]=camSide.look[i]+(look[i]-camSide.look[i])*k;}
    }
    for(int i=0;i<3;++i)if(!std::isfinite(eye[i]) || !std::isfinite(look[i]))return;
    PublishObservation(seat,point || camSide.carry>0.0f);
    std::memcpy(camSide.eye,eye,12);std::memcpy(camSide.look,look,12);
    Place(lookTarget,lookTarget+16,cam,eye,look,point);   // lookTarget+16 floats = out+0x70: the eye's target
}

float* __fastcall LookHook(const void* point,float* lookOut,unsigned char* cam) {
    float* const r=reinterpret_cast<PointFn>(image+kPoint)(point,lookOut);
    __try {
        float eye[3],look[3];
        if(shared.seat && static_cast<const unsigned char*>(point)==shared.seat+kSeatCamLook)
            Camera(static_cast<const unsigned char*>(point)-kSeatCamLook,lookOut+12,cam);
        // the player's Sazabi: its own rig, the screen's centre its aim (sazabi.cpp, sazabi_camera.inc)
        else if(SazabiCamera(static_cast<const unsigned char*>(point)-kSeatCamLook,reinterpret_cast<const float*>(cam+kCamEye),
                             reinterpret_cast<const float*>(cam+kCamLook),eye,look))
            Place(lookOut+12,lookOut+28,cam,eye,look);
        // A seat it does not place: a deployed Proteus raises the stock targets (proteus.cpp), the game eases onto them.
        else if(Cfg().enabled)ProteusViewLift(static_cast<const unsigned char*>(point)-kSeatCamLook,lookOut+12,lookOut+28);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return r;
}

// The look-at fetch's redirect (the camera) and the AddSe aim step's chain (the turret, and the gun stabilizer's way
// in: stab.cpp StabStep) go in apart: either one's code changed leaves the other in.
bool InstallLook() noexcept {
    if(!Matches(kLookSite,kLookSiteCode,sizeof(kLookSiteCode)) || !Matches(0xF5058,kCollisionMarginCode,sizeof(kCollisionMarginCode))) {
        Log("TURRETCAM the riding camera's code changed: the stock vehicle cameras (no turret camera, no high view)");
        return false;
    }
    // The shim: mov r8, rsi (the camera); jmp [rip] -> LookHook.
    unsigned char shim[3+14]={0x49,0x89,0xF0,0xFF,0x25,0,0,0,0};
    const auto hook=reinterpret_cast<std::uintptr_t>(&LookHook);
    std::memcpy(shim+9,&hook,8);
    void* const page=AllocateNearCode(image+kLookCall,shim,sizeof(shim));
    if(!page){Log("TURRETCAM no code page near EDF.dll");return false;}
    bool changed=false;
    if(!RedirectCall(image+kLookCall,image+kPoint,page,changed)){VirtualFree(page,0,MEM_RELEASE);Log("TURRETCAM look-at call not redirected");return false;}
    return true;
}

void InstallAim() noexcept {
    if(!Matches(kAimStep,kAimStepCode,sizeof(kAimStepCode))){Log("TURRETCAM the seat aim's step changed: the high view only, no stabilizer on its seats");return;}
    auto slot=reinterpret_cast<void**>(image+kAimVtable)+kAimStepSlot;
    void* next=nullptr;
    if(*slot!=image+kAimStep)Log("TURRETCAM the seat aim's step is patched already (%p): left alone, the high view only",*slot);   // another plugin's turret aim
    else if(!edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&AimHook),&next))Log("TURRETCAM the seat aim's step not hooked: the high view only");
    else nextAim=reinterpret_cast<AimStepFn>(next);
}
}  // namespace

bool InstallTurretCam() noexcept {
    __try {
        lookOk=InstallLook();
        InstallAim();
        Log("TURRETCAM hooks: look-at=%d aim=%d",lookOk,nextAim!=nullptr);
        return lookOk || nextAim;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void TurretCamFrame(unsigned char* v) noexcept {
    const Config& c=Cfg();
    const bool mine=shared.v==v;
    if(!lookOk)return;
    if(!c.enabled || SeatCount(v)==0){if(mine)Drop(c.enabled ? "no seat" : "plugin off");return;}
    unsigned char* seat=SeatAt(v,0);
    const bool driven=!v[kDead] && SeatRider(seat)==Rider::player && At<const void*>(seat,kSeatRider)==PlayerHuman() && Turret(v,seat);
    if(!driven){if(mine)Drop(v[kDead] ? "the vehicle is wrecked" : "the player got out");return;}
    if(!mine || !shared.ref.Is(v)) {
        if(shared.v)Drop("another vehicle");
        AcquireSRWLockExclusive(&lock);
        shared=Shared{};shared.ref=ObjRef::Of(v);shared.v=v;shared.seat=seat;shared.take=++takes;
        ReleaseSRWLockExclusive(&lock);
        game=GameSide{};game.held=FreeHeld(seat);   // a key held while boarding is no press
        Log("TURRETCAM v=%p: a turret the player drives (decoupled=%d, free look key 0x%X / button 0x%X, aim hook %d)",v,
            c.decoupledTurretCam,c.freeLookKey,c.freeLookButton,nextAim!=nullptr);
    }
    Shared shot{};shot.seat=seat;shot.v=v;
    const bool high=HighCamOn(v);
    if(high)ShotFocus(shot);
    AcquireSRWLockShared(&lock);
    const bool observing=shared.highView || shared.observing;
    ReleaseSRWLockShared(&lock);
    const bool mounted=!high && SightZoomMounted(v);
    game.hasAim=!high && !observing && !mounted && AimPoint(game.aim,&game.aimHit);
    // The one player turret aim (aimlink.h V4, turretaim.cpp): asked every frame so its bindings and lock stay current
    // through the high view and the optic too; in AUTO with a lock the turret goes to where the round meets the target,
    // through the same arc solve (Wants) as the screen's centre, the view staying the player's.
    float lead[3];
    if(PlayerTurretLead(v,0,TurretGun(v,seat),lead) && game.hasAim){std::memcpy(game.aim,lead,12);game.aimHit=true;}
    const auto freedom=weaponmount::OfWeapon(v,seat,TurretGun(shared.v,seat));
    AcquireSRWLockExclusive(&lock);
    shared.seat=seat;shared.seenMs=GameMs();
    shared.physicalOnly=!(freedom.known && freedom.yaw && freedom.pitch);
    shared.decoupled=c.decoupledTurretCam && nextAim && !shared.physicalOnly && !mounted;
    shared.high=high;shared.focusValid=shot.focusValid;shared.focusHit=shot.focusHit;
    std::memcpy(shared.focus,shot.focus,sizeof(shared.focus));
    if(!shared.decoupled && !shared.free && !shared.returning)shared.view=false;
    ReleaseSRWLockExclusive(&lock);
    const ULONGLONG now=GetTickCount64();
    if(c.debug && now-game.logAt>=kLogMs && shared.aimMs) {
        game.logAt=now;
        TurretCamReadout r{};
        const bool fresh=PlayerTurretCam(&r);
        Log("TURRETCAM v=%p view=(%.1f,%.1f) yaw=%.1f pitch=%.1f free=%d high=%d range=%.0f on=%d aim=%d rig r=%.1f base=%.1f",v,
            shared.yaw*180.0f/kPi,shared.pitch*180.0f/kPi,AxisAt(seat,0)[2]*180.0f/kPi,AxisAt(seat,1)[2]*180.0f/kPi,shared.free,shared.high,
            shared.focusValid ? vec::Dist(shared.focus,reinterpret_cast<const float*>(v+kMatrix)+12) : -1.0f,fresh && r.onTarget,game.hasAim,rigInfo.normalRadius,rigInfo.stockBase*180.0f/kPi);
    }
}

bool TurretCamLarge(const void* vehicle) noexcept {
    return shared.v==vehicle && rigInfo.v==vehicle && rigInfo.normalRadius>=kLargeRig;
}

bool TurretCamServes(const void* vehicle) noexcept { return lookOk && shared.v==vehicle; }

bool TurretCamHighTransition(const void* vehicle) noexcept {
    AcquireSRWLockShared(&lock);
    const bool active=shared.v==vehicle && (shared.highView || shared.observing)
                      && GameMs()-shared.seenMs<=kFreshMs;
    ReleaseSRWLockShared(&lock);
    return active;
}

bool TurretCamTurret(const void* vehicle,unsigned seat) noexcept {
    if(!lookOk || !nextAim || seat!=0 || !vehicle)return false;
    AcquireSRWLockShared(&lock);
    const Shared s=shared;
    ReleaseSRWLockShared(&lock);
    return Cfg().enabled && s.v==vehicle && s.decoupled && GameMs()-s.seenMs<=kFreshMs && s.ref.Is(vehicle);
}

bool TurretCamSteers(const void* vehicle) noexcept {
    if(!TurretCamTurret(vehicle,0))return false;
    AcquireSRWLockShared(&lock);
    const bool steering=shared.v==vehicle && shared.steering && GameMs()-shared.aimMs<=kAimFreshMs;
    ReleaseSRWLockShared(&lock);
    return steering;
}

bool PlayerTurretCam(TurretCamReadout* r) noexcept {
    AcquireSRWLockShared(&outLock);
    const Out o=out;
    ReleaseSRWLockShared(&outLock);
    if(!o.at || GameMs()-o.at>kFreshMs)return false;
    *r=o.r;
    return true;
}

void ResetTurretCam() noexcept {
    AcquireSRWLockExclusive(&lock);
    shared=Shared{};
    ReleaseSRWLockExclusive(&lock);
    game=GameSide{};camSide=CamSide{};rigInfo=RigInfo{};
    AcquireSRWLockExclusive(&outLock);
    out=Out{};
    ReleaseSRWLockExclusive(&outLock);
}
}  // namespace crew
