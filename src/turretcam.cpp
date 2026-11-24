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
//    turret onto the normal rig request's centre (seat/take-owned ray + MapRay, else kAimFar) at the stock rates
//    (turretcam.h AxisCommand): the round's low ballistic arc through a real map hit, the bore line through the made-up
//    point of a view that hits nothing (turretcam.h BallisticAim). EDF6AutoTurret turns this turret only onto the
//    player's lock and says so (common/edf/aimlink.h V2 Steers): that frame the turret is its, the view the rider's; an
//    EDF6AutoTurret without the V2 link is told apart by its input being off the stick (turretcam.h Foreign).
//  - Free look (FreeLookKey / FreeLookButton held): the view turns, the turret holds the point it was on (decoupled) or
//    stands still (coupled); let go, the view swings back to where it was (coupled: onto the stock camera's) and hands
//    over again.
//  - The rig (docs/camera-re.md §5): the stock locators' (A) or the vehicle's game_object_camera_setting (B, +0x170 /
//    +0x180, the authored third-person framing the riding camera never reads), whichever puts the eye farther back; the
//    eye never under the rig's own height over its point. The high view looks down on the shot's predicted landing
//    (ShotFocus) from HighCamHeight / HighCamBack (scaled up for a big vehicle), from behind along the gun's traverse
//    (tcam::OrbitPlace, tcam::GunHeading): the landing and its surroundings in the screen's middle, where the HUD draws
//    its mark; the gun's elevation moves it along that line only, and the view follows it eased (kRigEase) in the hull's
//    frame of motion. The game's collision runs from a pivot at the eye (tcam::HighPivot), the eye kept over what is
//    under it (tcam::HighLift). With no landing to show (no readable gun) it looks down on the hull. Free look turns it
//    round its centre; native input controls the gun, but for a lofted gun no other module poses (HighLoft: the V603's
//    launchers) the pitch input sets a range the gun is laid onto on its high arc (the camera never the target).
// A vehicle qualifies when the player drives it from seat 0, its seat camera is Type 1 (seat+0x200), its seat holds a
// weapon and its yaw axis turns (more than kMinTraverse): a turret, or the gun is indirect fire (a fixed one: the
// howitzer). Helicopters and the plugin's jets do not. The view is
// decoupled only while the aim hook sees that seat's aim stepped (a class that turns its turret some other way keeps the
// stock camera). The AddSe aim step's hook is also the gun stabilizer's way in (stab.cpp StabStep runs the stock step and
// then holds the gun), for every seat it holds, the player's or not; a gun it holds is steered from where it holds it
// (Steer: stab.cpp StabHeld). All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "seat_switch_logic.h"
#include "vehicle_look.h"
#include "firstperson.h"
#include "heli.h"
#include "layout.h"
#include "memory.h"
#include "turretcam.h"
#include "playarea.h"
#include "weapon_mount.h"
#include "stab.h"
#include "sight.h"
#include "sightzoom.h"
#include "turretaim.h"
#include "seat_turret.h"
#include "chase_cam.h"
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
// The camera: its eased eye and look-at (state 2), the per-tick shares it eases them by (copied from the seat's +0x228
// Param on entering state 2: docs/camera-re.md §3b), the soldier it follows (the player riding: firstperson.cpp).
constexpr std::size_t kCamEye=0x630,kCamLook=0x640,kCamEaseEye=0x650,kCamEaseLook=0x654,kCamCollisionMargin=0x528,kCamTarget=0x360;
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
constexpr ULONGLONG kLogMs=1000;
// The frame's / the aim hook's handoff to the camera step and the other modules: fresh within stepfresh::kEveryStep steps
// (step_fresh.h; both run once a step). The pause menu: no step runs, the camera step keeps the last view as it did.
// The chase follower's clock (chase_cam.h): the scene's steps (GameFrame), each 1/60 s of game time (System::Update adds
// n / 60 to its time for n steps, docs/unlimited-fps-re.md §1), the camera stepped once in each. Not the wall clock: a
// catch-up frame runs several steps within a millisecond and the clock's 16 / 17 ms reads are off the steps the vehicle
// moved by; a sprung point on a fast vehicle would shake by the difference. The pause menu's camera steps add none.
constexpr float kStepSeconds=1.0f/60.0f;
constexpr ULONGLONG kChaseGapSteps=60;          // a longer gap than this (1 s) is a new start, not a step

using PointFn=float*(__fastcall*)(const void*,float*);
AimStepFn nextAim=nullptr;
bool lookOk=false;

// What the game thread hands the camera (under `lock`).
struct Shared {
    ObjRef ref;
    unsigned char* v;
    unsigned char* seat;
    stepfresh::Step seenStep,aimStep;   // the frame saw the player there / the aim hook stepped that seat's aim
    bool decoupled,high;       // DecoupledTurretCam / highcam.cpp's toggle, this frame
    bool physicalOnly=true;
    bool free,returning;       // free look held / swinging back
    bool view;                 // yaw / pitch hold the plugin's view
    bool resume;               // no normal source since an owner/seat change: native input until Camera publishes one
    bool normalRay;            // the normal rig request, never the last HUD/render camera
    unsigned rayTake;
    float normalEye[3],normalDir[3];
    float normalReach;         // m from normalEye to the rig's point O along normalDir (tcam::ViewRayStart)
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
    bool point;                // that placement was the high view's (tcam::OrbitPlace)
    bool highSet;              // highYaw / highCentre hold this high view's
    float highYaw;             // the high view's heading (tcam::GunHeading; on entry the view's own)
    float highCentre[3];       // what it looks at, as an offset from the hull's origin, eased: the shot's landing, else 0
    float highLift;            // m the eye is raised over what is under it (tcam::HighLift)
    float carry;               // a switch between the two placements: the share of the old one's offset still carried
    float carryEye[3],carryLook[3];
    chase::Spring chase;       // the normal rig's sprung hull origin (chase_cam.h; not the high view's: its centre eases)
    bool chaseStarted;
    ULONGLONG chaseStep;       // GameFrame of its last step
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
    tcam::LoftRange loft;      // the high view's range for a lofted gun (HighLoft) and its pitch command's state
    tcam::SteerState loftSteer;
    bool foreign;              // the aim's input was not the rider's stick last frame
    ULONGLONG logAt;
};
Shared shared{};
unsigned takes=0;              // Shared::take's counter (under `lock`)
CamSide camSide{};
GameSide game{};
SRWLOCK lock=SRWLOCK_INIT;

struct Out { TurretCamReadout r; stepfresh::Step at; };
Out out{};
SRWLOCK outLock=SRWLOCK_INIT;

bool KeyHeld(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;   // the map view holds the player's keys (map.cpp)
    DWORD pid=0;
    GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (edf::AsyncKey(vk)&0x8000)!=0;
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
// air: the ground's height there by a map ray straight down (kGroundProbe m above and below the hull). Never over the
// void past the map's ground either (tcam::GroundReach): a point not landed on comes back along the line to the last
// ground under it, the hull's own height at the muzzle when there is none at all.
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
        // Never over the void past the map's ground (tcam::GroundReach): the farthest point along the line with ground.
        float r=tcam::AcrossDistance(muzzle,s.focus);
        float h[2]={s.focus[0]-muzzle[0],s.focus[2]-muzzle[2]};
        if(r>1e-3f){h[0]/=r;h[1]/=r;}else{h[0]=0.0f;h[1]=0.0f;}
        float start=r;   // where the ground's edge is looked for from: the measured one on this line
        const PlayArea a=MapPlayArea();
        if(a.ground) {
            const float lo[2]={a.lo[0]-area::kVoidMargin,a.lo[1]-area::kVoidMargin},hi[2]={a.hi[0]+area::kVoidMargin,a.hi[1]+area::kVoidMargin};
            start=tcam::AreaReach(lo,hi,muzzle[0],muzzle[2],h[0],h[1],r);
        }
        const float top=std::fmax(origin[1],muzzle[1])+kGroundProbe;
        float y=origin[1];
        r=tcam::GroundReach([&](float d,float* at) noexcept {
            const float x=muzzle[0]+h[0]*d,z=muzzle[2]+h[1]*d;
            const float from[3]={x,top,z},to[3]={x,origin[1]-kGroundProbe,z};
            float hit[3];
            if(!(MapRay(from,to,hit)>=0.0f) || !std::isfinite(hit[1]))return false;
            *at=hit[1];
            return true;
        },r,start,&y);
        s.focus[0]=muzzle[0]+h[0]*r;s.focus[1]=y;s.focus[2]=muzzle[2]+h[1]*r;
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

bool FreeHeld(const void* v,const unsigned char* seat) noexcept;
void Publish(const TurretCamReadout& r) noexcept;

// A camera request with an explicit seat/take owner. A rendered FP, map, optic or previous vehicle's ray cannot
// become a command to the same bones that produced it, even when rendering lags several simulation frames.
bool ExternalView(const void* v,bool high) noexcept {
    return MapHoldsKeys() || MapOwnsView() || (!high &&
        (SightZoomMounted(v) || FirstPersonVehicleView(v,0) || VehicleLookActive(v)));
}
void SuspendView(const void* v,const unsigned char* seat) noexcept {
    AcquireSRWLockExclusive(&lock);
    if(shared.seat==seat) {
        shared.normalRay=false;shared.resume=true;shared.view=false;shared.decoupled=false;
        shared.free=false;shared.returning=false;shared.steering=false;shared.highView=false;shared.observing=false;
    }
    ReleaseSRWLockExclusive(&lock);
    game.hasAim=false;game.aimHit=false;game.holdHit=false;game.steer=tcam::SteerState{};
    std::memset(game.aim,0,sizeof(game.aim));std::memset(game.holdAt,0,sizeof(game.holdAt));
    game.held=FreeHeld(v,seat);game.foreign=false;
    Publish(TurretCamReadout{});
}
void PublishNormalRay(const unsigned char* seat,unsigned take,const float* eye,const float* look) noexcept {
    float dir[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]};
    const bool valid=std::isfinite(eye[0]+eye[1]+eye[2]) && vec::Normalize(dir);
    AcquireSRWLockExclusive(&lock);
    if(shared.seat==seat && shared.take==take && !shared.high && !shared.observing) {
        shared.normalRay=valid;shared.resume=!valid;shared.rayTake=take;
        if(valid){std::memcpy(shared.normalEye,eye,12);std::memcpy(shared.normalDir,dir,12);shared.normalReach=vec::Dist(eye,look);}
    }
    ReleaseSRWLockExclusive(&lock);
}
// The normal rig's centre: first real map hit ahead of the turret (tcam::ViewRayStart: never the stretch between the
// requested eye and the rig's point, which lies behind the turret), else kAimFar along this owned request. The rendered
// HUD cache has neither the source nor the take of its camera and is intentionally never a control input here.
// `pivot`: the turret's yaw centre (TurretPivot), or null.
bool AimPoint(const Shared& s,const float* pivot,float* p,bool* hit) noexcept {
    float at[3];const float* eye=s.normalEye;const float* dir=s.normalDir;
    *hit=false;
    if(!s.normalRay || s.resume || s.rayTake!=s.take)return false;
    const float start=tcam::ViewRayStart(eye,dir,s.normalReach,pivot);
    const float from[3]={eye[0]+dir[0]*start,eye[1]+dir[1]*start,eye[2]+dir[2]*start};
    const float end[3]={eye[0]+dir[0]*kSightFar,eye[1]+dir[1]*kSightFar,eye[2]+dir[2]*kSightFar};
    if(start<kSightFar && MapRay(from,end,at)>=0.0f && vec::Dist(eye,at)<=kSightFar){std::memcpy(p,at,12);*hit=true;return true;}
    const float fallback=std::fmax(kAimFar,start);
    for(int i=0;i<3;++i)p[i]=eye[i]+dir[i]*fallback;
    return true;
}

// The turret camera's seat is seat 0: on a pad its free look leaves the buttons the stock input reads there as
// vehicle_look.cpp's does (VehicleLookOffered: a Grape's L3 is its horn).
bool FreeHeld(const void* v,const unsigned char* seat) noexcept {
    const Config& c=Cfg();
    const bool keys=At<unsigned char>(seat,kSeatPad)==0;
    if(!VehicleLookOffered(v,0,keys))return false;
    if(keys)return KeyHeld(c.freeLookKey);
    const std::uint16_t buttons=At<std::uint16_t>(seat,kSeatButtons);
    return (buttons&static_cast<std::uint16_t>(c.freeLookButton))!=0 &&
        !seatsw::ChordOwns(buttons,static_cast<unsigned>(c.thermalButton),static_cast<unsigned>(c.freeLookButton));   // the thermal chord's
}

void Publish(const TurretCamReadout& r) noexcept {
    AcquireSRWLockExclusive(&outLock);
    out=Out{r,stepfresh::Now()};
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

// Whether katyusha.cpp poses this vehicle's lofted launcher itself (its Rack: the Katyusha's rack bone lifted off the
// aim axes onto the arc, launcher.cpp LoftWant). Only then is the arc not the axes' to lay.
bool PosedLauncher(const void* v) noexcept {
    LoftReadout l{};
    return LauncherLoft(v,&l);
}

// A lofted gun's elevation (rad, up) to `y` up `x` across with its round falling `drop` m/frame^2 (edf::LoftElevation:
// the high arc within the pitch axis' stops while the round lives to come down there, else the low arc, else the
// farthest within its life).
float LoftedElevation(const unsigned char* seat,const RoundModel& model,double drop,float x,float y) noexcept {
    const float* pitch=AxisAt(seat,1);   // the stops, negative up
    float e=0.0f;
    edf::LoftElevation(x,y,model.speed,drop,model.alive,-pitch[1],-pitch[0],e);
    return e;
}

// The axes' wants (yaw, pitch: the aim's own senses) that put the gun on `p`, seen in `frame` (world rows x, up, nose:
// the frame the stabilizer's held axes are seen in, else the hull's now; stab.h HeldIn): the bore line through `p` from
// the bore's point at the turret's pivot (turretcam.h AimOrigin: not from the muzzle, which swings with the gun), with
// `ballistic` the low arc for a gun whose rounds drop, the high arc for a lofted launcher (LoftedElevation; the user,
// 2026-11-21: 「重型导弹这种开启俯瞰视角应该高抛弹道」: the V603's lofted guns are laid by their axes; the Katyusha's arc
// is katyusha.cpp's, PosedLauncher: it gets the line), else the bore line (turretcam.h BallisticAim).
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
    const bool lofted=edf::Lofted(At<std::int32_t>(gun,edf::kWeaponMark));
    // Guided rounds and motor-driven rockets do not follow the fixed-gravity arc solved here.
    if(ballistic && arc && !(lofted && PosedLauncher(shared.v)) && model.speed>0.0f && model.factor>0.0f && edf::WorldGravity(image,g)) {
        const double drop=model.factor*-(g[0]*frame[3]+g[1]*frame[4]+g[2]*frame[5])/3600.0;
        float e=elevation;
        if(drop>0.0 && lofted && model.alive>0)e=LoftedElevation(seat,model,drop,x,l[1]);
        else if(drop>0.0 && !lofted && !edf::BallisticArc(x,l[1],model.speed,drop,false,e,frames))e=elevation;
        if(std::isfinite(e))elevation=e;
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
bool Steer(const unsigned char* seat,const Steering& s,const float* want,float* in,tcam::SteerState& state=game.steer) noexcept {
    const tcam::Axis axes[2]={{AxisAt(seat,0)[0],AxisAt(seat,0)[1],AxisAt(seat,0)[2],At<float>(AxisAt(seat,0),kAxisRate)},
                              {AxisAt(seat,1)[0],AxisAt(seat,1)[1],AxisAt(seat,1)[2],At<float>(AxisAt(seat,1),kAxisRate)}};
    return tcam::SteerAxes(state,want,s.held,s.hull,axes,reinterpret_cast<const float*>(seat+kSeatAim+kAimParams),kOnTarget,in);
}

// The high view of a lofted gun no other module poses (the V603's TOS-1 and X-Morph missile launcher, whose aim axis is
// the rack; the user, 2026-11-21: 「重型导弹这种开启俯瞰视角应该高抛弹道」): the yaw input stays native; the pitch input
// moves the range (tcam::StepLoftRange, set on entry from where the gun's elevation then throws over level ground) and
// the pitch axis is steered onto the high arc (LoftedElevation, as Wants lays it in the normal view) to the ground that
// range out along the gun's traverse (a map ray straight down; none: level with the gun). Neither the camera nor the
// landing it watches (ShotFocus, from the real muzzles) is an input: no loop. False (native input) for any other gun.
bool HighLoft(unsigned char* seat,const Shared& s,const float* in,float* cmd) noexcept {
    const unsigned char* gun=TurretGun(s.v,seat);
    RoundModel model{};
    float muzzle[3],dir[3],g[3];
    if(!gun || !edf::Lofted(At<std::int32_t>(gun,edf::kWeaponMark)) || PosedLauncher(s.v) || !ReadRound(gun,&model) ||
       model.kind!=RoundKind::arc || !(model.speed>0.0f) || !(model.factor>0.0f) || model.alive<=0 ||
       !edf::MeanMuzzle(gun,16,muzzle,dir) || !edf::WorldGravity(image,g))return false;
    Steering st{};
    SteeringOf(s.v,seat,&st);
    const double drop=model.factor*-(g[0]*st.frame[3]+g[1]*st.frame[4]+g[2]*st.frame[5])/3600.0;
    float reach=0.0f;
    const float farthest=edf::LoftFarthest(model.speed,drop,model.alive,&reach);
    if(!(reach>tcam::kLoftLeast) || !std::isfinite(st.held[0]) || !std::isfinite(st.held[1]))return false;
    if(!game.loft.set) {
        const float e=-st.held[1];
        game.loft.set=true;game.loft.range=edf::LevelRange(model.speed,drop,e<farthest ? e : farthest);
        game.loftSteer=tcam::SteerState{};
    }
    const float range=tcam::StepLoftRange(game.loft,in[1],tcam::kLoftLeast,reach);
    float pivot[3],origin[3];
    tcam::AimOrigin(muzzle,dir,TurretPivot(seat,muzzle,pivot) ? pivot : nullptr,origin);
    const float sy=std::sin(st.held[0]),cy=std::cos(st.held[0]);
    float p[3];
    for(int i=0;i<3;++i)p[i]=origin[i]+(st.frame[i]*sy+st.frame[6+i]*cy)*range;
    const float top[3]={p[0],p[1]+kGroundProbe,p[2]},bottom[3]={p[0],p[1]-kGroundProbe,p[2]};
    float ground[3];
    if(MapRay(top,bottom,ground)>=0.0f && std::isfinite(ground[1]))p[1]=ground[1];
    float l[3],x=0.0f;
    tcam::LocalTo(st.frame,origin,p,l,&x);
    const float e=LoftedElevation(seat,model,drop,x,l[1]);
    if(!std::isfinite(e))return false;
    const float want[2]={st.held[0],-e};
    float steer[2]={0.0f,0.0f};
    Steer(seat,st,want,steer,game.loftSteer);
    cmd[0]=in[0];cmd[1]=steer[1];
    return true;
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
    const bool lofted=edf::Lofted(At<std::int32_t>(gun,edf::kWeaponMark));
    if(ballistic && arc && !(lofted && PosedLauncher(s.v)) && model.speed>0.0f && model.factor>0.0f && edf::WorldGravity(image,g)) {
        const float vel[3]={dir[0]*model.speed,dir[1]*model.speed,dir[2]*model.speed};
        const float drop[3]={g[0]*model.factor/3600.0f,g[1]*model.factor/3600.0f,g[2]*model.factor/3600.0f};
        // A lofted round's flight to the point is far longer than the straight range over its speed (a high arc
        // climbs and comes back down): where it is when it has come the point's distance across the ground.
        const float flat=std::sqrt(vel[0]*vel[0]+vel[2]*vel[2]);
        const float frames=lofted && flat>1e-4f ? tcam::AcrossDistance(muzzle,holdAt)/flat : range/model.speed;
        sight::RoundAfter(muzzle,vel,drop,frames,r.gun);
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
    s.aimStep=stepfresh::Now();
    const float stockBase=rigInfo.v==s.v ? rigInfo.stockBase : 0.0f;
    if(ExternalView(s.v,s.high)) {
        // Native input (and StabStep after us) keeps its exact ownership. A cockpit/optic is attached to the
        // controlled mount; its view must never be reinterpreted as a new world target for that mount.
        cmd[0]=in[0];cmd[1]=in[1];SuspendView(s.v,seat);
        return;
    }
    const bool held=FreeHeld(s.v,seat);
    const bool leavingHigh=s.highView && !s.high;
    const bool press=held && (!game.held || leavingHigh),release=!held && game.held && !leavingHigh;
    game.held=held;
    if(s.high) {
        if(!s.highView){s.orbitYaw=0.0f;s.orbitPitch=0.0f;game.loft=tcam::LoftRange{};}
        s.highView=true;s.view=true;s.steering=false;s.free=held;s.returning=false;
        game.steer.hasWant=false;
        const float stick[2]={-At<float>(seat,kSeatStick),At<float>(seat,kSeatStick+4)};
        const bool foreign=tcam::Foreign(AutoTurretSteers(s.v,0),in,stick,kForeign);
        if(held) {
            const float rate=c.turretCamRate*kPi/180.0f/60.0f;
            s.orbitYaw=tcam::Wrap(s.orbitYaw-stick[0]*rate);
            s.orbitPitch=vec::Clamp(s.orbitPitch+stick[1]*rate,-tcam::kPitchMost,tcam::kPitchMost);
        }   // let go: the view stays where free look turned it (a world-held heading, nothing to swing back onto)
        // The stock axis step (and stabilizer) owns gun motion. Looking round must not feed the observation back.
        cmd[0]=held && !foreign ? 0.0f : in[0];cmd[1]=held && !foreign ? 0.0f : in[1];
        // A lofted gun's pitch lays its high arc to the range the native pitch input sets (HighLoft); another hand's
        // input keeps the gun, the range set anew from the gun when it gives it back.
        if(foreign)game.loft=tcam::LoftRange{};
        else if(!held)HighLoft(seat,s,in,cmd);
        AcquireSRWLockExclusive(&lock);
        if(shared.seat==seat) {
            shared.aimStep=s.aimStep;shared.view=s.view;shared.highView=true;shared.steering=false;
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
        s.yaw=tcam::YawOf(dir);s.pitch=tcam::PitchOf(dir)+stockBase;
        s.highView=false;s.view=true;s.free=held;s.returning=false;s.steering=false;
        game.hasAim=false;game.aimHit=false;game.steer.hasWant=false;
        // Re-arm free look after the blend: its normal entry must capture a fresh, normal-camera aim point.
        game.held=false;
        const float stick[2]={-At<float>(seat,kSeatStick),At<float>(seat,kSeatStick+4)};
        const bool foreign=tcam::Foreign(AutoTurretSteers(s.v,0),in,stick,kForeign);
        cmd[0]=held && !foreign ? 0.0f : in[0];cmd[1]=held && !foreign ? 0.0f : in[1];
        AcquireSRWLockExclusive(&lock);
        if(shared.seat==seat) {
            shared.aimStep=s.aimStep;shared.view=true;shared.highView=false;shared.steering=false;
            shared.yaw=s.yaw;shared.pitch=s.pitch;shared.free=s.free;shared.returning=false;
        }
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    if(s.resume || !s.view) {
        float dir[3];
        if(!s.resume && s.normalRay && s.rayTake==s.take)std::memcpy(dir,s.normalDir,12);
        else AxesDir(s.v,seat,dir);
        s.yaw=tcam::YawOf(dir);s.pitch=tcam::PitchOf(dir)+(s.resume ? stockBase : 0.0f);
        s.view=true;s.highView=false;
        if(s.resume) {
            game.hasAim=false;game.aimHit=false;game.steer.hasWant=false;game.held=held;
            cmd[0]=in[0];cmd[1]=in[1];
            AcquireSRWLockExclusive(&lock);
            if(shared.seat==seat){shared.aimStep=s.aimStep;shared.view=true;shared.yaw=s.yaw;shared.pitch=s.pitch;shared.steering=false;}
            ReleaseSRWLockExclusive(&lock);
            return;
        }
    }
    if(press) {
        std::memcpy(game.holdAt,game.aim,12);game.holdHit=game.aimHit;
        if(!game.hasAim){float d[3];AxesDir(s.v,seat,d);const float* p=reinterpret_cast<const float*>(s.v+kMatrix)+12;for(int i=0;i<3;++i)game.holdAt[i]=p[i]+d[i]*kAimFar;game.holdHit=false;}
        game.backYaw=s.yaw;game.backPitch=s.pitch;
        if(!s.decoupled && !s.highView) {   // coupled: the view starts where the screen's centre is
            if(s.normalRay && s.rayTake==s.take){s.yaw=tcam::YawOf(s.normalDir);s.pitch=tcam::PitchOf(s.normalDir);}
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
        if(!s.decoupled){float d[3];AxesDir(s.v,seat,d);to[0]=tcam::YawOf(d);to[1]=tcam::PitchOf(d)+stockBase;}
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
    const bool ballistic=tcam::BallisticAim(hold ? game.holdHit : game.aimHit);
    const bool steer=s.decoupled && !foreign && (game.hasAim || hold);
    s.steering=false;
    Steering st{};
    if(steer)SteeringOf(s.v,seat,&st);
    if(foreign){cmd[0]=in[0];cmd[1]=in[1];game.steer.hasWant=false;}
    else if(steer && Wants(seat,st.frame,target,ballistic,want)){on=Steer(seat,st,want,cmd);s.steering=true;}
    else if(s.free || s.returning){cmd[0]=0.0f;cmd[1]=0.0f;}   // coupled: the turret stands while the view looks round
    else{cmd[0]=in[0];cmd[1]=in[1];game.steer.hasWant=false;}
    AcquireSRWLockExclusive(&lock);
    if(shared.seat==seat){shared.aimStep=s.aimStep;shared.free=s.free;shared.returning=s.returning;shared.view=s.view;shared.yaw=s.yaw;shared.pitch=s.pitch;shared.steering=s.steering;
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
        // The pivot is put at the eye's end of the same optical ray (tcam::HighPivot): neither that collapse nor a
        // building or slope between the landing and the eye can pull the eye in. The real projectile focus stays
        // unchanged and collinear, so it still projects to the same optical centre.
        const float margin=At<float>(cam,kCamCollisionMargin);
        tcam::HighPivot(eye,focus,std::isfinite(margin) ? std::fmax(tcam::kHighPivot,4.0f*margin) : tcam::kHighPivot,collisionLook);
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

// --- the chase follower (chase_cam.h; README 载具镜头跟随) ---
// 2026-11-21 (「载具/玩家之类的视角优化一下」): every camera this hook places was on its vehicle's origin exactly each tick
// (Place below writes the eased points too, so the native per-tick ease cam+0x650 / +0x654 did nothing), and the player's
// jets kept the stock locators welded to `mdl` with no roll. The follower gives them inertia and roll:
//  - the cameras whose direction is the player's input (the Sazabi, the mouse-flown heli, the gunship's gun seat, a
//    Dreadnought's or a gunner seat's turret; the turret view in Camera) spring the vehicle's origin only, eye and look-at
//    moved by the one offset: the screen's centre keeps the input's direction (the turret's PublishNormalRay, the
//    Dreadnought's CameraRay aim: the same ray, laid from the drawn eye); a heli rolls by a share of its bank;
//  - the player's jets (stock locators, PlayerJetChase): the locators held in the craft's frame, that frame eased onto the
//    craft's attitude with part of the bank kept, the look-at leading along the path (chase::FollowCraft);
//  - never first person (the cockpit's eye is the seat's bone), the mounted optic, nor the turret's high view (its centre
//    eases on its own); a take-over, another seat or vehicle, first person in between start it over on the vehicle.
// One follower per camera (split screen: each local player's own), stepped on the scene's steps (kStepSeconds). The roll
// is the native step's rows turned after it (firstperson.cpp CameraAfterNative -> ChaseCamUp), in its write transaction.
enum class ChaseKind { ground,heli,air,jet,rotor };
const char* const kChaseNames[]={"ground","heli","air","jet","rotor"};
const chase::Tuning& TuningOf(ChaseKind k) noexcept {
    switch(k) {
    case ChaseKind::heli:return chase::kHeli;
    case ChaseKind::air:return chase::kAir;
    case ChaseKind::jet:return chase::kJet;
    case ChaseKind::rotor:return chase::kRotor;
    default:return chase::kGround;
    }
}
struct Follower {
    const unsigned char* cam;
    const unsigned char* seat;
    ObjRef vehicle;
    ChaseKind kind;
    bool started;
    ULONGLONG step;            // GameFrame of its last step
    chase::Chase chase;
    bool up;                   // `upDir` is this step's roll for CameraAfterNative (ChaseCamUp), taken at `upStep`
    float upDir[3];
    ULONGLONG upStep;
};
constexpr int kFollowers=4;
Follower followers[kFollowers]{};

// Game seconds since the last step (0 on a start or a step not taken yet: the follower holds), the steps counted.
float StepSeconds(bool& started,ULONGLONG& last) noexcept {
    const ULONGLONG now=GameFrame();
    const bool had=started;const ULONGLONG was=last;
    started=true;last=now;
    if(!had || now<was)return 0.0f;
    return now-was>kChaseGapSteps ? 2.0f*chase::kMostDt : static_cast<float>(now-was)*kStepSeconds;
}

Follower* FollowerOf(const unsigned char* cam) noexcept {
    Follower* spare=nullptr;
    for(Follower& f:followers) {
        if(f.cam==cam)return &f;
        if(!spare || !f.cam || (spare->cam && f.step<spare->step))spare=&f;
    }
    *spare=Follower{};spare->cam=cam;
    return spare;
}
void ForgetFollower(const unsigned char* cam) noexcept {
    for(Follower& f:followers)if(f.cam==cam)f=Follower{};
}

// The vehicle `seat` is in when its rider is the player this camera follows (its seat index in `index`), else null.
unsigned char* RiddenBy(const unsigned char* cam,const unsigned char* seat,unsigned* index) noexcept {
    const auto rider=At<const unsigned char*>(seat,kSeatRider);
    if(!rider || rider!=At<const unsigned char*>(cam,kCamTarget) || !Readable(rider,kHumanVehicleCtrl+8) || !IsPlayer(rider))return nullptr;
    const auto ctrl=At<const unsigned char*>(rider,kHumanVehicleCtrl);
    if(!Readable(ctrl,16) || At<std::int32_t>(ctrl,8)==0)return nullptr;
    const auto v=At<unsigned char*>(rider,kHumanVehicleCtrl-8);
    if(!Readable(v,kMatrix+64) || v[kDead])return nullptr;
    const unsigned n=SeatCount(v);
    for(unsigned i=0;i<n && i<16;++i)if(SeatAt(v,i)==seat){*index=i;return v;}
    return nullptr;
}

// The follower of `cam` onto this step's `eye` / `look` (placed off `v`, the player's at its seat `index`): both replaced
// by the followed ones. False: it does not follow here (the plugin's switch off, first person, the optic, refused input)
// and has left them as they were.
bool Follow(unsigned char* cam,const unsigned char* seat,unsigned char* v,unsigned index,ChaseKind kind,float* eye,float* look) noexcept {
    const Config& c=Cfg();
    if(!c.enabled || !c.chaseCam || FirstPersonVehicleView(v,index) || SightZoomMounted(v))return false;
    Follower& f=*FollowerOf(cam);
    if(f.seat!=seat || !f.vehicle.Is(v) || f.kind!=kind){f=Follower{};f.cam=cam;f.seat=seat;f.vehicle=ObjRef::Of(v);f.kind=kind;}
    const bool fresh=!f.chase.anchor.valid;
    const float dt=StepSeconds(f.started,f.step);
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    chase::Placement placed{};
    const bool craft=kind==ChaseKind::jet || kind==ChaseKind::rotor;
    if(!(craft ? chase::FollowCraft(f.chase,TuningOf(kind),m,eye,look,dt,&placed) : chase::FollowAnchor(f.chase,TuningOf(kind),m,eye,look,dt,&placed))) {
        f=Follower{};
        return false;
    }
    // Whether the native ease is near 1 (the "picture stuck on the screen") or not: once a take, from the camera itself.
    if(fresh && c.debug)Log("CHASE cam=%p v=%p seat %u: the %s follower takes the camera; native ease eye %.3f look %.3f (cam+0x650 / +0x654)",
                            static_cast<const void*>(cam),static_cast<const void*>(v),index,kChaseNames[static_cast<int>(kind)],
                            At<float>(cam,kCamEaseEye),At<float>(cam,kCamEaseLook));
    std::memcpy(eye,placed.eye,12);std::memcpy(look,placed.look,12);
    f.up=placed.hasUp;f.upStep=GameFrame();
    if(placed.hasUp)std::memcpy(f.upDir,placed.up,12);
    return true;
}
bool FollowSeat(unsigned char* cam,const unsigned char* seat,ChaseKind kind,float* eye,float* look) noexcept {
    unsigned index=0;
    unsigned char* const v=RiddenBy(cam,seat,&index);
    return v && Follow(cam,seat,v,index,kind,eye,look);
}

// The player's jet at its seat 0 in third person: the stock locators' targets (`stockEye` / `stockLook`) chased.
bool PlayerJetChase(unsigned char* cam,const unsigned char* seat,const float* stockEye,const float* stockLook,float* eye,float* look) noexcept {
    unsigned index=0;
    unsigned char* const v=RiddenBy(cam,seat,&index);
    if(!v || index!=0 || !IsPlayerJet(v))return false;
    std::memcpy(eye,stockEye,12);std::memcpy(look,stockLook,12);
    return Follow(cam,seat,v,index,PlayerRotorCraft(v) ? ChaseKind::rotor : ChaseKind::jet,eye,look);
}

// The turret's normal rig: its hull origin sprung (kGround), into `at`; false: the hull's own (the switch off, refused).
bool TurretAnchor(const float* origin,float* at) noexcept {
    const float dt=StepSeconds(camSide.chaseStarted,camSide.chaseStep);
    if(!Cfg().chaseCam || chase::Follow(camSide.chase,chase::kGround,origin,dt,at)==chase::Step::refused) {
        camSide.chase=chase::Spring{};
        return false;
    }
    return true;
}

void Camera(const unsigned char* seat,float* lookTarget,unsigned char* cam) noexcept {
    const Config& c=Cfg();
    AcquireSRWLockShared(&lock);
    const Shared s=shared;
    ReleaseSRWLockShared(&lock);
    const bool live=c.enabled && seat==s.seat && s.v && stepfresh::Fresh(s.seenStep) && s.ref.Is(s.v);
    const bool aimed=live && stepfresh::Fresh(s.aimStep) && s.view;
    const bool viewed=aimed && (s.decoupled || s.free || s.returning);
    const bool own=live && (viewed || s.high);
    if(!live){camSide=CamSide{};PublishObservation(seat,false);return;}
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
    if(ExternalView(s.v,s.high)) {
        // Keep the measured rig/classification (the high view still needs it), but no hidden third-person
        // placement or return blend may survive under an external camera owner.
        camSide=CamSide{};
        AcquireSRWLockExclusive(&lock);
        if(shared.seat==seat){shared.normalRay=false;shared.resume=true;shared.observing=false;}
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    if(!own) {
        Leave(seat,lookTarget,cam);
        PublishObservation(seat,camSide.owned && (camSide.point || camSide.carry>0.0f));
        if(!s.high && !camSide.owned)PublishNormalRay(seat,s.take,stockEye,stockLook);
        return;
    }
    const float scale=std::fmax(1.0f,normal.radius/20.0f);
    tcam::Rig high=tcam::High(c.highCamHeight*scale,c.highCamBack*scale,c.highCamPitch);
    std::memcpy(high.fixed,normal.fixed,sizeof(high.fixed));
    const float highBase=-c.highCamPitch*kPi/180.0f;
    // Both coupled and decoupled high cameras look down on the vehicle.
    const bool point=s.high;
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
        if(c.debug)Log("TURRETCAM v=%p: camera taken (%s%s), rig %s r=%.1f up=%.1f rise=%.1f, stock r=%.1f, native ease eye %.3f look %.3f (cam+0x650 / +0x654)",
                       s.v,viewed ? "view" : "coupled",s.high ? ", high" : "",b && (!a || rigB.radius>=rigA.radius) ? "authored" : "stock",normal.radius,
                       normal.up,normal.rise,a ? rigA.radius : 0.0f,At<float>(cam,kCamEaseEye),At<float>(cam,kCamEaseLook));
    }
    tcam::Ease(camSide.rig,want,kRigEase);
    camSide.offset+=(offset-camSide.offset)*kRigEase;
    float eye[3],look[3];
    if(point) {
        // The user, 2026-10-25: "俯瞰视角目的是为了看落点位置的信息，不是把视角单单放在车更高的地方，改一下。现在完全没办法看瞄准
        // 位置". Centred on the hull (2026-10-23), a landing a few hundred metres out was off the top of the screen. The
        // view looks at the shot's predicted landing (ShotFocus: the same prediction the HUD's mark is drawn from), from
        // behind it along the line the shot flies; with no landing to show, at the hull. Eased (kRigEase a frame, as the
        // rig's own moves): the landing of a gun rocked over the ground runs up and down that line (the 2026-10-23
        // complaint "会随着炮塔上下摇晃"), and the view follows it without jerking; the HUD's mark stays exact.
        // 2026-11-21 ("俯瞰视角移动的时候会白屏一下……并且这个俯瞰视角会抖动。某些载具上"): the landing is eased as an offset
        // from the hull, not as a world point (a world point eased behind a driving hull trails it by speed x the ease's
        // time and wobbles with every change of speed); the heading is the gun's traverse (tcam::GunHeading), not the
        // hull-to-landing line that swung the orbit by the landing's sideways wobble.
        const float hull[3]={m[12],m[13],m[14]};
        camSide.chase=chase::Spring{};   // the normal rig's follower starts over on the hull when it is back (the carry eases)
        float rel[3]={0.0f,0.0f,0.0f};
        if(s.focusValid)for(int i=0;i<3;++i)rel[i]=s.focus[i]-hull[i];
        if(!camSide.highSet) {   // entering it: on the landing at once
            std::memcpy(camSide.highCentre,rel,12);camSide.highSet=true;camSide.highLift=0.0f;
            const float d[3]={camSide.look[0]-camSide.eye[0],0.0f,camSide.look[2]-camSide.eye[2]};
            camSide.highYaw=d[0]*d[0]+d[2]*d[2]>1e-6f ? tcam::YawOf(d) : 0.0f;
        }
        for(int i=0;i<3;++i)camSide.highCentre[i]+=(rel[i]-camSide.highCentre[i])*kRigEase;
        camSide.highYaw=tcam::GunHeading(m,AxisAt(seat,0)[2],camSide.highYaw);
        const float centre[3]={hull[0]+camSide.highCentre[0],hull[1]+camSide.highCentre[1],hull[2]+camSide.highCentre[2]};
        tcam::OrbitPlace(centre,camSide.highYaw,c.highCamHeight*scale,c.highCamBack*scale,s.orbitYaw,s.orbitPitch,eye,look);
        // The eye over whatever is under it (a hill or a tower behind the landing): the collision no longer does it.
        const float top[3]={eye[0],eye[1]+kGroundProbe,eye[2]},bottom[3]={eye[0],eye[1]-tcam::kHighClear,eye[2]};
        float under[3];
        const bool ground=MapRay(top,bottom,under)>=0.0f && std::isfinite(under[1]);
        camSide.highLift=tcam::HighLift(eye[1],ground,ground ? under[1] : 0.0f,tcam::kHighClear,camSide.highLift,kRigEase);
        eye[1]+=camSide.highLift;
    } else {
        camSide.highSet=false;
        float yaw,pitch;
        if(viewed){yaw=s.yaw;pitch=s.pitch+camSide.offset;}
        else {
            float a3[3];AxesDir(s.v,seat,a3);
            yaw=tcam::YawOf(a3);pitch=tcam::PitchOf(a3)+camSide.offset;
        }
        pitch=vec::Clamp(pitch,-kViewMost,kViewMost);
        float d[3];
        tcam::Dir(yaw,pitch,d);
        // The rig round the hull's sprung origin (TurretAnchor): the view's direction is still exactly the input's, so
        // the ray PublishNormalRay hands the turret is the screen's centre, from the eye as drawn.
        float anchor[3];
        tcam::Place(camSide.rig,TurretAnchor(m+12,anchor) ? anchor : m+12,yaw,d,eye,look);
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
    if(!s.high && !point && camSide.carry==0.0f)PublishNormalRay(seat,s.take,eye,look);
}

float* __fastcall LookHook(const void* point,float* lookOut,unsigned char* cam) {
    float* const r=reinterpret_cast<PointFn>(image+kPoint)(point,lookOut);
    __try {
        const unsigned char* const seat=static_cast<const unsigned char*>(point)-kSeatCamLook;
        float eye[3],look[3];
        bool followed=false;   // the chase follower placed this step (else it starts over next time)
        // the player's Sazabi first: its own rig, the screen's centre its aim (sazabi.cpp, sazabi_camera.inc); a turret
        // seat's pointer kept past its vehicle (a stale `shared`) must never stand in its way
        if(SazabiCamera(seat,reinterpret_cast<const float*>(cam+kCamEye),reinterpret_cast<const float*>(cam+kCamLook),eye,look)) {
            followed=FollowSeat(cam,seat,ChaseKind::ground,eye,look);
            Place(lookOut+12,lookOut+28,cam,eye,look);
        } else if(PlayerHeliCamera(seat,lookOut+28,lookOut+12,eye,look)) {
            followed=FollowSeat(cam,seat,ChaseKind::heli,eye,look);
            Place(lookOut+12,lookOut+28,cam,eye,look);
        } else if(GunshipGunnerCamera(seat,lookOut+28,lookOut+12,eye,look)) {   // the gunship's gun seat: the rider's own view (playerjet_crew.inc)
            followed=FollowSeat(cam,seat,ChaseKind::air,eye,look);
            Place(lookOut+12,lookOut+28,cam,eye,look);
        } else if(DreadnoughtCamera(seat,eye,look)) {   // a Dreadnought turret's gunner: theirs (dreadnought_turrets.inc)
            followed=FollowSeat(cam,seat,ChaseKind::ground,eye,look);
            Place(lookOut+12,lookOut+28,cam,eye,look);
        } else if(SeatTurretCamera(seat,eye,look)) {   // a gunner seat's own turret: the gunner's view (seat_turret.cpp)
            followed=FollowSeat(cam,seat,ChaseKind::ground,eye,look);
            Place(lookOut+12,lookOut+28,cam,eye,look);
        } else if(shared.seat && seat==shared.seat)Camera(seat,lookOut+12,cam);
        // the player's jet on its stock locators: chased (PlayerJetChase); the native ease then has nothing left to do
        else if(PlayerJetChase(cam,seat,lookOut+28,lookOut+12,eye,look)){followed=true;Place(lookOut+12,lookOut+28,cam,eye,look);}
        // A seat it does not place: a deployed Proteus raises the stock targets (proteus.cpp), the game eases onto them.
        else if(Cfg().enabled)ProteusViewLift(seat,lookOut+12,lookOut+28);
        if(!followed)ForgetFollower(cam);
    } __except(EXCEPTION_EXECUTE_HANDLER){ForgetFollower(cam);}
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
        shared=Shared{};shared.ref=ObjRef::Of(v);shared.v=v;shared.seat=seat;shared.take=++takes;shared.resume=true;
        ReleaseSRWLockExclusive(&lock);
        game=GameSide{};game.held=FreeHeld(v,seat);   // a key held while boarding is no press
        Log("TURRETCAM v=%p: a turret the player drives (decoupled=%d, free look key 0x%X / button 0x%X, aim hook %d)",v,
            c.decoupledTurretCam,c.freeLookKey,c.freeLookButton,nextAim!=nullptr);
    }
    Shared shot{};shot.seat=seat;shot.v=v;
    const bool high=HighCamOn(v);
    if(high)ShotFocus(shot);
    AcquireSRWLockShared(&lock);
    const bool observing=shared.highView || shared.observing;
    ReleaseSRWLockShared(&lock);
    const bool external=ExternalView(v,high);
    if(external)SuspendView(v,seat);
    AcquireSRWLockShared(&lock);
    const Shared normalSource=shared;
    ReleaseSRWLockShared(&lock);
    // The turret's yaw centre for the aim point's search (AimPoint): the bore's point nearest the look-at bone.
    float pivot[3];bool pivotOk=false;
    {
        const unsigned char* gun=TurretGun(v,seat);
        float muzzle[3],dir[3];
        if(gun && edf::MeanMuzzle(gun,16,muzzle,dir))pivotOk=TurretPivot(seat,muzzle,pivot);
    }
    game.hasAim=!high && !observing && !external && AimPoint(normalSource,pivotOk ? pivot : nullptr,game.aim,&game.aimHit);
    // The one player turret aim (aimlink.h V4, turretaim.cpp): asked every frame so its bindings and lock stay current
    // through the high view and the optic too; in AUTO with a lock the turret goes to where the round meets the target,
    // through the same arc solve (Wants) as the screen's centre, the view staying the player's.
    float lead[3];
    if(PlayerTurretLead(v,0,TurretGun(v,seat),lead) && game.hasAim){std::memcpy(game.aim,lead,12);game.aimHit=true;}
    const auto freedom=weaponmount::OfWeapon(v,seat,TurretGun(shared.v,seat));
    AcquireSRWLockExclusive(&lock);
    shared.seat=seat;shared.seenStep=stepfresh::Now();
    shared.physicalOnly=!(freedom.known && freedom.yaw && freedom.pitch);
    shared.decoupled=c.decoupledTurretCam && nextAim && !shared.physicalOnly && !external;
    if(high){shared.normalRay=false;shared.resume=true;}
    shared.high=high;shared.focusValid=shot.focusValid;shared.focusHit=shot.focusHit;
    std::memcpy(shared.focus,shot.focus,sizeof(shared.focus));
    if(!shared.decoupled && !shared.free && !shared.returning)shared.view=false;
    ReleaseSRWLockExclusive(&lock);
    const ULONGLONG now=GetTickCount64();
    if(c.debug && now-game.logAt>=kLogMs && shared.aimStep) {
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

const unsigned char* PlayerTurretGun(const unsigned char* vehicle,const unsigned char* seat) noexcept {
    return vehicle && seat ? TurretGun(vehicle,seat) : nullptr;
}

bool TurretCamHighTransition(const void* vehicle) noexcept {
    AcquireSRWLockShared(&lock);
    const bool active=shared.v==vehicle && (shared.highView || shared.observing)
                      && stepfresh::Fresh(shared.seenStep);
    ReleaseSRWLockShared(&lock);
    return active;
}

bool TurretCamTurret(const void* vehicle,unsigned seat) noexcept {
    if(!lookOk || !nextAim || seat!=0 || !vehicle)return false;
    AcquireSRWLockShared(&lock);
    const Shared s=shared;
    ReleaseSRWLockShared(&lock);
    return Cfg().enabled && s.v==vehicle && s.decoupled && !s.resume && s.normalRay && s.rayTake==s.take &&
        !s.highView && !s.observing && !ExternalView(vehicle,s.high) &&
        stepfresh::Fresh(s.seenStep) && s.ref.Is(vehicle);
}

bool TurretCamFreeLook(const void* vehicle,unsigned seat) noexcept {
    if(!lookOk || seat!=0 || !vehicle || !Cfg().enabled)return false;
    AcquireSRWLockShared(&lock);
    const Shared s=shared;
    ReleaseSRWLockShared(&lock);
    if(s.v!=vehicle || !s.ref.Is(vehicle) || !stepfresh::Fresh(s.seenStep))return false;
    // The same owners ExternalView yields to, but the free look itself (vehicle_look.cpp asks this to know whether it
    // is its look at all): the map, first person's cockpit eye, the optic up (magnified, or the gunner sight at 1x).
    if(MapHoldsKeys() || MapOwnsView() || HighCamOn(vehicle) || FirstPersonVehicleView(vehicle,0))return false;
    return !(SightZoomCanMount(vehicle,0) && SightZoomRaised(vehicle));
}

bool TurretCamSteers(const void* vehicle) noexcept {
    if(!TurretCamTurret(vehicle,0))return false;
    AcquireSRWLockShared(&lock);
    const bool steering=shared.v==vehicle && shared.steering && stepfresh::Fresh(shared.aimStep);
    ReleaseSRWLockShared(&lock);
    return steering;
}

bool PlayerTurretCam(TurretCamReadout* r) noexcept {
    AcquireSRWLockShared(&outLock);
    const Out o=out;
    ReleaseSRWLockShared(&outLock);
    if(!stepfresh::Fresh(o.at))return false;
    *r=o.r;
    return true;
}

void ResetTurretCam() noexcept {
    AcquireSRWLockExclusive(&lock);
    shared=Shared{};
    ReleaseSRWLockExclusive(&lock);
    game=GameSide{};camSide=CamSide{};rigInfo=RigInfo{};
    for(Follower& f:followers)f=Follower{};
    AcquireSRWLockExclusive(&outLock);
    out=Out{};
    ReleaseSRWLockExclusive(&outLock);
}

void ChaseCamUp(const unsigned char* cam,float* rows) noexcept {
    for(Follower& f:followers)if(f.cam==cam && f.up) {
        f.up=false;
        if(rows && f.upStep==GameFrame())chase::UpRows(rows,f.upDir);
    }
}
}  // namespace crew
