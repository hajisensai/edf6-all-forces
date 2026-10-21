// First person on foot (README 第一人称; plan: the user, 2026-10-12). RE: docs/fp-render-re.md (the draw, its passes,
// the draw pose), docs/fp-soldier-re.md (the aim frame, the aim the network carries). Two hooks, both this machine's
// picture only (nothing here changes what the body does, where shots start or what the network sends):
//  - The camera (H layout, camera-re.md §3): CharacterGhostCamera's per-frame slot 4 (0xF86A0) is chained; after it the
//    eye row (+0x250) is moved to the local player's head, the rotation left as the stock step built it (the soldier's
//    aim frame +0x60, whose pitch and yaw the third-person camera turns with). The eye is the head bone's place in the
//    yaw-only body frame (+0x5E0), eased, so the animation's bob does not shake the view. The stock matrix is put back
//    before the next step (its easing reads it), as map.cpp does. The map and the TV chain outside this hook: their
//    views win while they are open and they ease from this one.
//  - The draw (H, fp-render-re.md §1-2, §17): AnimationModel's draw (vtable slot 1, 0x6BF9F0) runs on a render thread
//    (measured: not the camera step's, and up to three update steps after it: T is composed there, DrawTransforms) and
//    builds the skinning palette from the draw pose array ([inst+0x30], one 4x4 a bone) inside the call. For the local
//    player's body and weapons, in the colour passes (1 G-buffer, 2 translucent, 5 the far camera) that array is
//    rewritten for the call and put back after: every pose P becomes P·T (fp_view.h: the viewmodel in front of the
//    camera). The body colour draw submits only triangles weighted to one visible arm (fp_skin.h / fp_skin_gpu.cpp);
//    torso and mixed shoulder triangles are excluded, not collapsed. The shadow pass (3) stays native and the
//    occluder depth pass (0) skips the viewmodel. The
//    bones' world matrices (+0xB0), which the muzzles, the shots and the network read, are never touched.
//  - The muzzle flash (H, fp-render-re.md §5): a shot of the player's weapon (0x696FD0) hands its flash the muzzle's shot
//    matrix through weapon+0x4C0 (the flash keeps that pointer and copies the matrix each of its 3 updates); the call to
//    the effect factory there (0x697FD0) is redirected so that, for the player's weapons in first person, the pointer
//    becomes an owned matrix after native construction: the shot's immutable muzzle-to-weapon offset through the
//    current weapon pose and T. Both primary and Umbra deleting destructors retire its borrowed matrix; a camera
//    reset freezes it until then. The round itself was made before that call from the real muzzle.
//  - The aim line (H call, M meaning): a soldier's weapon with a trajectory line (the grenade launchers' red arc) builds
//    it with 0x6899F0(line, &start, &velocity, &gravity) from its muzzle (the call at 0x6888D9); the call is redirected and,
//    for a line that starts at the player in first person, its start is moved through T (the viewmodel's muzzle); the
//    arc keeps its own velocity and gravity. Only the picture moves: the round flies from the real muzzle as before.
//  - The culling (H setter, measured): Umbra culls a model by its bounds box (model+0x110, handed over by 0x11B30C0).
//    The real weapon and arms go below the view in a reload's third-person animation and were culled with the viewmodel
//    in front of the camera (the whole player, shadow too, for half a second). The real box through T was not enough
//    (the Wing Diver's weapon, that near the eye, was still drawn in one frame in seven): in first person each camera
//    step hands Umbra a box around the eye for the body and the weapons in hand (the camera inside it: never culled),
//    and the real boxes back on the way out. That is done as the frame's render starts (System::update's call of the
//    render 0x11978D0 at 0x1198FA4, redirected): a weapon's own update (slot 3 -> 0x6C06A0 -> 0x11B30C0) may come after
//    the camera step and put its real box back. That was not all: with the camera in the head the Wing Diver's gun was
//    still left out of the visible set four frames in five (drawn every frame in the third person, measured; the same
//    with the placement off). So the main view's opaque pass is completed: after its list (0x11E1630 for each object,
//    pass 1) and before the pass ends (0x11E1590(system, 1), its call at 0x11E2572 redirected), a model of the
//    viewmodel the pass has not drawn this frame is submitted the same way.
//  - Two hands (measured: the Fencer's weapons hung below the view): each weapon in hand (each weapon set's current one)
//    belongs to the hand its mount bone is on (weapon+0xE88, the record of `arms_r` / `arms_l` / `armsFlip_l`...: the
//    name's _l), and each hand has its own T anchored on its own weapon, the left grip the right one mirrored. A hand
//    with none shares the other's (the Ranger's left hand on the rifle's guard); with none in either, the aim frame.
//    The body's bones whose name ends in _l take the left T, the others the right. Weapons not in hand (on the back,
//    put away) are not drawn in the colour passes (their shadow is).
//  - The batches (H, measured: the Wing Diver's gun was drawn one frame in four): the draw submit 0x11E1630 hands an
//    object whose +0x18 holds a batch to that batch (it draws its members itself, not through their slot 1); only one
//    without a batch is drawn by its own draw. In first person each weapon in hand is taken out of its batch (+0x18
//    cleared, the batch kept) so its draw comes here; the batch goes back when the weapon leaves the hand or the view
//    leaves first person, if the weapon is still one of the player's (never into a weapon already gone).
//  - The camera's first row points to the screen's left (measured: a grip at +x drew on the left), so every "right" the
//    ini and the motion speak of is mirrored into it here (x, yaw and roll negated).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "firstperson.h"
#include "edf/weapon.h"
#include "fp_view.h"
#include "vehicle_look.h"
#include "fp_motion.h"
#include "fp_skin_gpu.h"
#include "thermal.h"
#include "enemy_control.h"
#include "map.h"
#include "memory.h"
#include "edf/host.h"
#include <d3d11.h>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
using fpview::M4;

// The camera (map.cpp's layout): its per-frame slot, target soldier, world matrix rows, near clip.
constexpr unsigned kCamVtable=0x1768C10,kCamStep=0xF86A0;
constexpr std::size_t kCamStepSlot=4,kCamTargetRef=0x350,kCamTarget=0x360,kCamMatrix=0x220,kCamEyeRow=0x250,kCamNear=0x28;
const unsigned char kCamStepCode[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x90};
// AnimationModel's draw (slot 1): draw(model, ctx, pass, view). Its LODs: the base one embedded at +0x160, the others
// an array at [+0x250] (count +0x260, 0xF0 each); a LOD's skeleton instance at its +0x58.
constexpr unsigned kModelVtable=0x17C4030,kModelDraw=0x6BF9F0;
constexpr unsigned kMeshCall=0x1103FD1,kMeshDraw=0x10FF4E0;
constexpr unsigned kMeshPrepare=0x10FFAC0,kMeshPass=0x1151330,kFlushDrawState=0x11597E0;
constexpr unsigned kLaserDrawVtable=0x17E2480,kLaserDraw=0x6882A0;
const unsigned char kMeshDrawCode[]={0x40,0x53,0x55,0x41,0x56,0x48,0x83,0xEC,0x40};
const unsigned char kLaserDrawCode[]={0x4C,0x8B,0xDC,0x55,0x53,0x56,0x57};
constexpr std::size_t kModelDrawSlot=1,kBaseLod=0x160,kLods=0x250,kLodCount=0x260,kLodStride=0xF0,kLodInst=0x58;
const unsigned char kModelDrawCode[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,
                                      0x4C,0x8B,0x91,0x50,0x02,0x00,0x00,0x48,0x8D,0xB9,0x60,0x01,0x00,0x00};
// A skeleton instance: bone records [+0x10] (0x110 each: +0 the name, +0xB0 the world matrix), count +0x20, the draw
// pose array [+0x30] (0x40 a bone).
constexpr std::size_t kInstBones=0x10,kInstCount=0x20,kInstPose=0x30,kBoneStride=0x110,kBoneWorld=0xB0;
constexpr int kMostBones=256,kMostInsts=4;
// The soldier: its aim frame +0x60 (pitch and yaw), yaw-only body frame +0x5E0, body model +0x860 and its skeleton
// +0x900, ragdoll +0x1180, a class gadget's own view +0x224 (the Air Raider's strike view: the stock camera stays), the
// look smoothing +0x1260 (0.3 a frame; fp-soldier-re.md §5), weapons [+0x1950] (count +0x1960) each with its model at
// +0xF30.
constexpr std::size_t kAim=0x60,kBodyFrame=0x5E0,kBodyModel=0x860,kBodyInst=0x900,kRagdoll=0x1180,kSpecialView=0x224,
                      kLookSmooth=0x1260,kHumanVehicle=0x1548,kWeapons=0x1950,kWeaponCount=0x1960,kWeaponModel=0xF30;
constexpr unsigned kSoldierVts[]={0x17CDF28,0x17D0FF8,0x17CF5B8,0x17CF100};   // Ranger, Wing Diver, Fencer, Air Raider
constexpr int kMostWeapons=16;
// The motion's cues (fp-soldier-re.md §3-4): the smoothed look (pitch, negative up; yaw) +0x1240/+0x1244, the position
// +0x90, the character proxy's support +0x711 (2 = in the air), frames on the ground +0x1264 (0 -> 1 = a landing), the
// action limits +0x5D0 (each stock move writes its own value on entry); the hands' weapon sets [+0x1970] (count +0x1980,
// 0x150 each, the weapon in hand **(set+0x40)). A weapon: shots fired +0xBD0, its fire state +0xBD4 (3 = a reload's
// animation runs, 4 = its _finish: 0x693DB5 / 0x693D0E), the reload's frames left +0xE68 (both states run while it is
// above 0: 0x693BAF; payload.cpp's reload bar reads it too), ReloadTime +0x20C (frames), FireRecoil +0x37C.
constexpr std::size_t kLook=0x1240,kPos=0x90,kSupport=0x711,kGroundFrames=0x1264,kActLimit=0x5D0,
                      kSets=0x1970,kSetCount=0x1980,kSetStride=0x150,kSetWeapon=0x40,
                      kShotCount=0xBD0,kFireState=0xBD4,kReloadTime=0x20C,kFireRecoil=0x37C,
                      kReloadLeft=0xE68;
constexpr int kMostSets=4,kMostReloadFrames=900;
// The stock moves by the value they write to +0x5D0 (§3.3; H for the Ranger's sprint and the rolls, L for the Fencer's
// and the Wing Diver's: Debug=1 logs every change to match them). Only the Ranger's is a sprint (a run held); the
// Fencer's (0xA38 its dash, measured 2026-10-14: 0.2 s a press, toggling at each one) and the Wing Diver's are bursts
// of thrust: a dash (fp_motion.h).
constexpr std::uint32_t kSprints[]={0x29E},kDashes[]={0x29,0x3D,0xA38,0x25,0xE39},kRolls[]={0xE3C};
// The muzzle flash: the shot's call of the effect factory (rcx the factory, rdx, r8 the flash class [weapon+0x480], r9
// &weapon+0x488: the flash takes its matrix pointer from weapon+0x4C0 = r9+0x38).
constexpr unsigned kFlashCall=0x697FD0,kEffectCreate=0x1194280;
constexpr std::size_t kFlashParam=0x488,kFlashMatrix=0x4C0;
constexpr int kFlashSlots=64;   // never reused until the native effect is destroyed
// The aim line's build (a soldier weapon's call of it) and how near the player's chest its start must be to be theirs.
constexpr unsigned kAimLineCall=0x6888D9,kAimLineBuild=0x6899F0;
// Umbra's object transform setter (0x6C06A0 calls it with model+0x110 after an update).
constexpr unsigned kUmbraPlace=0x11B30C0;
constexpr std::size_t kModelBounds=0x110,kUmbraObject=0x10;
constexpr float kEyeBox=2.0f;
// The frame's render, its call in System::update (rcx the system, dl, r8b).
constexpr unsigned kRenderCall=0x1198FA4,kRender=0x11978D0;
// Native camera update submits its camera-to-world to Umbra before our camera callback returns. Capture that exact
// frame at the verified main-camera call site, rather than comparing the later, differently timed camera matrix.
constexpr unsigned kCameraSubmitCall=0x118E10A,kCameraSubmit=0x11D46D0;
using RenderFn=void(__fastcall*)(void*,std::uint8_t,std::uint8_t);
// The main view's opaque pass: each object submitted by 0x11E1630(system, object, 1); the pass ended by
// 0x11E1590(system, 1), called at 0x11E2572.
constexpr unsigned kSubmit=0x11E1630,kPassEnd=0x11E1590,kPassEndCall=0x11E2572;
using SubmitFn=void(__fastcall*)(void*,void*,int);
using PassEndFn=void(__fastcall*)(void*,int);
ULONGLONG renderFrame=0;               // the renders started
struct Drawn1 { const void* model; ULONGLONG frame; };
constexpr std::size_t kBatch=0x18;   // a drawn object's batch (0x11E1630)
constexpr int kMostUnbatched=8;   // m each way from the eye: the box Umbra gets for the viewmodel's models
const unsigned char kUmbraPlaceCode[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x55,0x48,0x8D,0x68,0xA1,0x48,0x81,0xEC,0x90};
using PlaceFn=void(__fastcall*)(void*,const float*);
bool placeOk=false;
constexpr float kAimLineReach=2.0f,kChestUp=1.2f;
// Without a weapon to anchor on (the Fencer's two hands, empty hands) the viewmodel turns about a point near where the
// hands are (the eye's frame: right, up, ahead, m); with one, about its grip.
const float kPivot[3]={-0.2f,-0.3f,0.4f};   // in the camera's frame: x to the left
constexpr std::size_t kWeaponMount=0xE88;   // the record of the owner's bone the weapon hangs on (0x68AEE9)
constexpr int kRight=0,kLeft=1;
constexpr float kGadgetFar=10.0f,kGadgetHigh=3.7f;   // m from the feet: the stock eye of a gadget's view (VR mod: >10 m, >2 m over the head)
// Deforming upper arms (including twists), forearms, palms and fingers. The clavicle and weapon mount are not
// deformable limb sections: their torso-side cloth faces would otherwise move with an unrelated segment. The
// colour topology mask excludes those and mixed torso seams while retaining the upper arms, rather than cutting
// the sleeve at the elbow. FitArm solves a connected two-bone chain whose open shoulder end stays outside the view.
const wchar_t* const kArmBones[]={L"jowan",L"kawan",L"te_",L"fing",L"ring",L"little",L"index",L"middle",
                                  L"thumb"};

using CamStepFn=void(__fastcall*)(void*,void*);
using DrawFn=void(__fastcall*)(void*,void*,int,void*);
CamStepFn nextCamStep=nullptr;
bool camOk=false,drawOk=false;
// The module that holds the model draw when another plugin replaced it outright (not a chain onto the stock draw
// our hook could sit on): EDF6RangerFPS swaps the slot and recolours the same skin palette, so the two first
// persons cannot both draw. Named in the install line and once when the key is pressed (empty: not the reason).
char drawOwner[64]{};
bool drawOwnerToldOnKey=false;

// The draw hook is on every vtable whose slot 1 is the stock draw (a model class that keeps it): each its own next.
struct Hooked { const void* vtable; DrawFn next; };
Hooked hooked[8]{};
int hookedCount=0;
void __fastcall DrawHook(void* model,void* ctx,int pass,void* view);
bool HookDrawOf(void** vt) noexcept;
int HandOfModel(const void* model) noexcept;
const unsigned char* HandWeapon(int hand,bool* flip) noexcept;
bool HandBounds(int hand,const unsigned char* w,M4* out) noexcept;
int BoneIndex(const unsigned char* inst,const wchar_t* name) noexcept;

struct Unbatched { const void* model; void* batch; const void* ctrl; };
struct alignas(16) Flash { float m[16]; M4 local; const void* model; void* effect; int hand; unsigned reserved[3]; };
struct CameraApplied { void* cam=nullptr;fpview::CameraWrite edit{}; };
struct KeyGesture {
    const unsigned char* human=nullptr;
    const void* control=nullptr;
    std::uint64_t press=0;
    bool was=false,rode=false,cancelled=false;
};
// --- What the camera step leaves for the draws (State: game thread only; the draws read only DrawSnap) ---
// What the draws read of a camera's state (docs/fp-render-re.md §17.5): the draws run on a render thread while the
// game thread's camera step rewrites State, so the step publishes this as a whole (Publish, a sequence count: odd
// while it is written) and each draw takes its own consistent copy (TakeSnapshot) instead of reading State fields
// as they change under it. What a draw itself writes is DrawOwn's, never State's.
struct DrawSnap {
    bool on,riding;
    const void* body;
    const void* bodyCtrl;const void* rideCtrl;
    const void* rideModel;      // riding: the vehicle's model (its instruments)
    const void* weapons[kMostWeapons];
    const void* weaponCtrls[kMostWeapons];
    int weaponCount;
    const void* inHand[kMostSets];
    const void* inHandCtrls[kMostSets];
    int handOf[kMostSets];
    int inHandCount;
    bool viewOk[2],shown[2],onOther[2],placeWeaponOk[2],placeBodyOk[2],shoulderOk[2],boundsOk[2];
    M4 view[2],otherBox[2],placeWeapon[2],placeBody[2],bounds[2];
    wchar_t bodyMount[2][24];
    float shoulder[2][3];       // each arm's shoulder in the camera's own frame (x left, y up, z ahead), shrunk by s
    int reloadLeft;
    bool hasCam;                // a camera is bound to this state (the single-view route without native parameters)
    M4 camera;                  // the camera matrix the step left (a draw without its own native view)
    int rideSeat;               // riding: the seat, its instruments' readings (FirstPersonInstrumentReadings)
    bool instrumentsReady;
    instruments::Readings instruments;
    M4 probeAnchor[2];bool probeAnchored[2];   // Debug=1: each hand's anchor as the camera step read it
};

struct State {
    Unbatched unbatched[kMostUnbatched]{};
    Flash flashes[kFlashSlots]{};
    int flashNext=0;
    CameraApplied cameraApplied{};
    KeyGesture key{};
    const unsigned char* nativeWeapon[2]{};
    float nativeRest[2][3]{};
    bool on;                    // the local player's underlying camera is in first person
    bool overridden;            // an outer camera owner (map/TV, including its transition) draws this viewport
    unsigned char* human;       // that player
    unsigned char* cam;         // its camera
    const void* body;           // its body model
    const void* bodyCtrl;const void* rideCtrl;
    const void* weapons[kMostWeapons];
    const void* weaponCtrls[kMostWeapons];
    int weaponCount;
    const void* inHand[kMostSets];   // the weapons in hand (each set's current one): their models
    const void* inHandCtrls[kMostSets];
    int handOf[kMostSets];           // ...and the hand each is in (kRight, kLeft)
    int inHandCount;
    float eyeLocal[3];          // the eased eye in the body frame
    bool fresh;                 // the eye taken as it is next frame
    float savedNear,savedSmooth,stepNear,writtenNear;
    bool nearSet,smoothSet,stepNearSet;
    // The motion: last frame's readings and the springs.
    bool have;
    float pitch,yaw,pos[3],vy;
    std::int32_t groundFrames;
    std::uint32_t act;
    const unsigned char* held[kMostSets];
    std::int32_t shots[kMostSets],fire[kMostSets];
    int reloadLeft;
    fpmotion::State motion;
    struct ShotEvent { const unsigned char* weapon; int count; float recoil; } shotEvent[2];
    int sampledShots,notifiedShots;
    M4 moved[2];                // the motion as a matrix about each hand's grip (fp_view.h MotionMatrix)
    bool viewOk[2];             // each hand's T this frame (kRight, kLeft), built by the camera step
    M4 view[2];
    float grip[2][3],hold[3];    // the hold the camera step chose for each hand (camera frame, x left), the aim frame's
    // Each hand's anchor placed in the camera's own frame by the camera step: its weapon's root, and the body's bone
    // that root hangs on (bodyMount). The draw composes its T from these, the poses it draws and its own camera
    // (DrawTransforms): the draws run on another thread, up to three update steps after the camera step (§17).
    M4 placeWeapon[2],placeBody[2];
    bool placeWeaponOk[2],placeBodyOk[2];
    wchar_t bodyMount[2][24];
    float shoulder[2][3];bool shoulderOk[2];   // each arm's own shoulder in the camera's frame, shrunk (Placements)
    bool shown[2];              // visible arm ownership for the colour topology mask (neither while riding)
    bool axisOk[2];             // a point on the axis of the weapon each hand holds, after T (the weapon's root)
    float axis[2][3];
    bool onOther[2];            // a free hand drawn on the other hand's weapon, whose bounds after T are otherBox
    M4 otherBox[2];
    // Each hand's weapon's size along its own axes (WeaponBounds), taken once a weapon: the hold it is fitted to is the
    // weapon's, not the frame's (a reload's magazine bone does not move the gun).
    // Debug=1: the steadiness probe (Probe), the camera step's half (the draws' half is DrawOwn's).
    bool probeHave[2],builtReady;
    float probeRoot[2][3],probeStep[2],probeSubmitOff;
    unsigned probeSubmits,probeSubmitsOff,probeFrames,probeAir;
    float probeSpeed,probeMountGap[2],probeMountLeast[2];
    bool probeMountHave[2];
    M4 built;                   // the camera matrix the viewmodel was built on this step
    M4 probeAnchor[2];bool probeAnchored[2];
    unsigned probeCamThread,probeCamSteps;
    const unsigned char* boundsOf[2];
    bool boundsOk[2];
    M4 bounds[2];
    bool riding;                // in a vehicle: the eye at the seat's cockpit, the rider not drawn (Ride)
    RideView ride;
    CockpitScreens screens;     // ...and its screens round the eye (VehicleScreens), for the HUD
    instruments::Readings instruments;
    bool instrumentsReady;
};
/// Each local camera owns its pose, motion, batches and effects. Never evict another local player's view.
constexpr int kLocalViews=4;
State states[kLocalViews]{};
// Each local camera's player as its last step saw it (game thread): this machine's players, split screen's two.
struct LocalSeen { unsigned char* human; ULONGLONG at; };
LocalSeen localSeen[kLocalViews]{};
constexpr ULONGLONG kLocalSeenMs=1000;
// What the camera step and the camera submit leave for the draws (docs/fp-render-re.md §17.5), each published whole
// under a sequence count (odd while it is written: SeqPublish) and read as a whole copy (SeqTake). The draws run on a
// render thread, up to three update steps behind the game thread, so nothing on their path reads a State field.
// The camera's submitted views (CaptureRenderCamera), the last kViewHistory of them: a draw is made with the view of the
// update it renders, which may be a few submits older than the newest (§17.2: 0-3 steps), and matching only the newest
// left a lagging draw of a turning camera unowned (L1 error past 0.1: the stock body and gun for that draw).
constexpr int kViewHistory=8;
// ok: submitted in first person; sent: submitted (any view), by the local player `who` the camera followed then.
struct ViewHistory { M4 view[kViewHistory]; bool ok[kViewHistory]; bool sent[kViewHistory]; const void* who[kViewHistory]; int next; };
// The draws' copies, beside the states (a State is reset by assigning it whole: never under a reader's feet). last /
// lastViews: the render thread's own (the last whole copy, while a writer keeps the count odd).
struct Published { DrawSnap snap; volatile LONG seq; DrawSnap last; bool lastOk;
                   ViewHistory views; volatile LONG viewSeq; ViewHistory lastViews; bool lastViewsOk; };
Published published[kLocalViews]{};
template<class T,class Write> void SeqPublish(volatile LONG* seq,T* value,Write write) noexcept {
    InterlockedIncrement(seq);   // odd: being written (a full barrier)
    write(*value);
    InterlockedIncrement(seq);   // even: whole
}
// A copy torn by a writer meanwhile is taken again; while a writer keeps it busy the last whole copy stands. False: none.
template<class T> bool SeqTake(volatile LONG* seq,const T* value,T* out,T* last,bool* lastOk) noexcept {
    for(int tries=0;tries<64;++tries) {
        const LONG before=InterlockedCompareExchange(seq,0,0);
        if(before&1){YieldProcessor();continue;}
        std::memcpy(out,value,sizeof(*out));
        MemoryBarrier();
        if(InterlockedCompareExchange(seq,0,0)==before){*last=*out;*lastOk=true;return true;}
    }
    if(!*lastOk)return false;
    *out=*last;
    return true;
}
thread_local const DrawSnap* drawSnap=nullptr;   // the draw in progress's copy (render thread)
const DrawSnap& Snap() noexcept { static const DrawSnap none{};return drawSnap ? *drawSnap : none; }
struct SnapScope {
    const DrawSnap* before;
    explicit SnapScope(const DrawSnap* next) noexcept : before(drawSnap) { drawSnap=next; }
    ~SnapScope() { drawSnap=before; }
};
// Only inspect the current native model's embedded owner field; never dereference a cached control block.
// A pooled model address is not its identity. An old snapshot must not pose a new NPC/weapon at that address.
bool ModelOwnerIs(const void* model,std::size_t ownerOffset,const void* control) noexcept {
    if(!model || !control)return false;
    const auto owner=static_cast<const unsigned char*>(model)-ownerOffset;
    return Readable(owner,kSelfCtrl+8) && !(owner[0x18]&4) && At<const void*>(owner,kSelfCtrl)==control;
}
int SnapHand(const DrawSnap& d,const void* model) noexcept {
    for(int i=0;i<d.inHandCount && i<kMostSets;++i)if(d.inHand[i]==model && ModelOwnerIs(model,kWeaponModel,d.inHandCtrls[i]))return d.handOf[i];
    return -1;
}
void Publish() noexcept;
void ResetToggleGestures() noexcept;
bool TakeSnapshotOf(int index,DrawSnap* out) noexcept {
    Published& p=published[index];
    return SeqTake(&p.seq,&p.snap,out,&p.last,&p.lastOk);
}
bool TakeViews(int index,ViewHistory* out) noexcept {
    Published& p=published[index];
    return SeqTake(&p.viewSeq,&p.views,out,&p.lastViews,&p.lastViewsOk);
}
// Each thread its own selection: the camera step (game thread) and the draws (render thread) select states at once.
thread_local State* selected=&states[0];
State& Current() noexcept { return *selected; }
// This draw's copy of the selected state's published one (render thread). False: none yet.
bool TakeSnapshot(DrawSnap* out) noexcept { return TakeSnapshotOf(static_cast<int>(selected-states),out); }
struct StateScope {
    State* before;
    explicit StateScope(State* next) noexcept : before(selected) { selected=next; }
    ~StateScope() { selected=before; }
};
State* CameraState(const void* camera) noexcept {
    for(auto& state:states)if(state.cam==camera)return &state;
    return nullptr;
}
State* AcquireCamera(void* camera) noexcept {
    if(auto found=CameraState(camera))return found;
    for(auto& state:states)if(!state.cam){state.cam=static_cast<unsigned char*>(camera);return &state;}
    return nullptr;
}
// A state's submitted views retired (game thread): it left first person, or the mission's cameras are gone.
void ClearViews(int index) noexcept {
    Published& p=published[index];
    SeqPublish(&p.viewSeq,&p.views,[](ViewHistory& v){v=ViewHistory{};});
}
// Mission reset is published from preload; only the game/render thread drops its camera objects and GPU cache.
std::atomic<unsigned> session{0};
unsigned appliedSession=0;
void SyncSession() noexcept {
    const unsigned next=session.load(std::memory_order_acquire);
    if(next==appliedSession)return;
    ResetToggleGestures();
    for(auto& state:states){
        Flash flashes[kFlashSlots];std::memcpy(flashes,state.flashes,sizeof(flashes));
        for(auto& f:flashes)f.model=nullptr;
        state=State{};std::memcpy(state.flashes,flashes,sizeof(flashes));
        selected=&state;Publish();ClearViews(static_cast<int>(&state-states));
    }
    for(auto& seen:localSeen)seen=LocalSeen{};
    selected=&states[0];fpskin::Clear();appliedSession=next;
}
// --- What the draws keep for themselves (render thread only: the game thread never writes or resets it) ---
// The opaque pass's record and the draws' half of the Debug=1 probe. A State is reset whole by the game thread
// (SyncSession, Leave, a caught fault) while a draw may be writing: what a draw writes lives here instead, per state.
struct DrawOwn {
    bool live;unsigned session;
    Drawn1 drawn1[kMostSets+1];   // the viewmodel's models drawn in the opaque pass since its last end
    ULONGLONG pass1;              // that pass's count (PassEndSelected ends it)
    bool wristHave[2];
    float wrist[2][3],wristStep[2],bend[2],drawRoot,drawMount;
    unsigned support[2],supportFallback[2],draws,drawThread;
    ULONGLONG loggedAt;
};
DrawOwn drawOwn[kLocalViews]{};
DrawOwn& Own() noexcept {
    DrawOwn& o=drawOwn[selected-states];
    const unsigned now=session.load(std::memory_order_acquire);
    if(!o.live || o.session!=now){o=DrawOwn{};o.live=true;o.session=now;o.pass1=1;}   // a new mission: nothing carried
    return o;
}
unsigned char* CameraPlayer(const unsigned char* cam) noexcept;
void CaptureRenderCamera(void* camera,const float* world) noexcept {
    State* state=CameraState(camera);
    if(!state)return;
    M4 frame;std::memcpy(frame.m,world,64);
    M4 native;
    const bool sent=fpview::NativeView(frame,&native);
    const bool ok=sent && state->on && !state->overridden;
    const void* const who=CameraPlayer(static_cast<const unsigned char*>(camera));
    Published& p=published[state-states];
    SeqPublish(&p.viewSeq,&p.views,[&](ViewHistory& v){
        const int at=v.next>=0 && v.next<kViewHistory ? v.next : 0;
        v.view[at]=native;v.ok[at]=ok;v.sent[at]=sent && who;v.who[at]=who;v.next=(at+1)%kViewHistory;
    });
    if(Cfg().debug && state->on && state->builtReady && !state->riding) {
        float off=0.0f;for(int i=0;i<16;++i)off+=std::fabs(frame.m[i]-state->built.m[i]);
        ++state->probeSubmits;
        if(off>1e-3f){++state->probeSubmitsOff;state->probeSubmitOff=std::fmax(state->probeSubmitOff,off);}
    }
}
using CameraSubmitFn=void(__fastcall*)(void*,const float*);
void __fastcall CameraSubmitHook(void* wrapper,const float* world) {
    __try { CaptureRenderCamera(static_cast<unsigned char*>(wrapper)-0x70,world); }
    __except(EXCEPTION_EXECUTE_HANDLER){}
    reinterpret_cast<CameraSubmitFn>(image+kCameraSubmit)(wrapper,world);
}
// Native main-view matrix = inverse(camera + 0x220) with its X/Z columns negated (the render convention).
// +0x60 is the projection, not a second camera. Cache by matrix value as the same parameter block is reused for
// shadow cascades and both local viewports; its pointer and the last camera update cannot identify its owner.
// A draw's view is matched against each camera's recent submits (ViewHistory), the frame that draw renders among them,
// at the same tolerance. `live`: some camera has a first-person view among its submits.
State* MatchView(const M4& native,bool* live=nullptr) noexcept {
    State* match=nullptr;
    bool ambiguous=false;
    if(live)*live=false;
    for(int k=0;k<kLocalViews;++k) {
        ViewHistory v;
        if(!TakeViews(k,&v))continue;
        bool mine=false;
        for(int i=0;i<kViewHistory;++i) {
            if(!v.ok[i])continue;
            if(live)*live=true;
            float error=0;for(int j=0;j<16;++j)error+=std::fabs(native.m[j]-v.view[i].m[j]);
            if(std::isfinite(error) && error<0.1f){mine=true;break;}
        }
        if(!mine)continue;
        if(match)ambiguous=true; // Identical camera frames are ambiguous; never borrow another player's model.
        match=&states[k];
    }
    return ambiguous ? nullptr : match;
}
State* ViewState(const void* view) noexcept {
    if(!view || !Readable(view,0x60)) {
        // Test/legacy single-view submissions without native parameters remain unambiguous only with one camera.
        State* only=nullptr;int cameras=0;
        for(int k=0;k<kLocalViews;++k) {
            DrawSnap s;
            if(!TakeSnapshotOf(k,&s) || !s.hasCam)continue;
            ++cameras;if(s.on)only=&states[k];
        }
        return cameras==1 ? only : nullptr;
    }
    M4 native;std::memcpy(native.m,static_cast<const unsigned char*>(view)+0x20,64);
    // The draws' own cache (render thread): the owner holds while no camera submitted, or retired, a view since.
    thread_local M4 cached{};
    thread_local LONG cachedSeq[kLocalViews]{};
    thread_local bool cachedOk=false;
    thread_local State* owner=nullptr;
    LONG seq[kLocalViews];bool settled=true;
    for(int k=0;k<kLocalViews;++k){seq[k]=InterlockedCompareExchange(&published[k].viewSeq,0,0);settled=settled && !(seq[k]&1);}
    if(cachedOk && settled && std::memcmp(cachedSeq,seq,sizeof(seq))==0 && std::memcmp(cached.m,native.m,64)==0)return owner;
    bool live=false;
    owner=MatchView(native,&live);cached=native;std::memcpy(cachedSeq,seq,sizeof(seq));cachedOk=settled;
    if(Cfg().debug && At<int>(view,4)==0) {
        thread_local ULONGLONG logged=0;thread_local unsigned matched=0,unmatched=0;
        if(owner)++matched;else if(live)++unmatched;
        const ULONGLONG now=GetTickCount64();
        if(now-logged>=5000){Log("FP view routing: matched %u, unmatched %u main-view matrices",matched,unmatched);matched=unmatched=0;logged=now;}
    }
    return owner;
}
using CreateFn=void*(__fastcall*)(void*,void*,void*,void*);
std::atomic<bool> toggled{false};   // read by FirstPersonGunnerSight on the draw thread too
// One physical key still toggles the shared preference once. Gestures themselves
// belong to a camera's actual human/control identity, so another local player's
// map or controlled body cannot cancel them. Delayed camera passes share a press
// serial rather than relying on which viewport happens to update first.
std::uint64_t keyPress=0,keyConsumed=0;
bool keyPhysicalDown=false;
const unsigned char* keyClaimHuman=nullptr;
const void* keyClaimControl=nullptr;
int passLog[2][6]{};   // the body's draws, the weapons' in hand, by pass
ULONGLONG passLogAt=0;

bool KeyInputAllowed(const unsigned char* human) noexcept {
    if(MapHoldsHumanKeys(human))return false;
    DWORD pid=0;
    GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    return pid==GetCurrentProcessId();
}

bool IsSoldier(const unsigned char* h) noexcept {
    const auto vt=At<const unsigned char*>(h,0);
    for(unsigned v:kSoldierVts)if(vt==image+v)return true;
    return false;
}

M4 MatrixAt(const unsigned char* p) noexcept { M4 m;std::memcpy(m.m,p,sizeof(m.m));return m; }

// The bone record named `name` (exactly) in a skeleton instance, else nullptr.
const unsigned char* BoneNamed(const unsigned char* inst,const wchar_t* name) noexcept {
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    const auto n=At<std::int32_t>(inst,kInstCount);
    if(!bones || n<=0 || n>kMostBones)return nullptr;
    for(std::int32_t i=0;i<n;++i) {
        const unsigned char* rec=bones+static_cast<std::size_t>(i)*kBoneStride;
        const auto nm=At<const wchar_t*>(rec,0);
        if(nm && std::wcscmp(nm,name)==0)return rec;
    }
    return nullptr;
}

bool ArmBone(const wchar_t* name) noexcept {
    if(!name)return false;
    for(const wchar_t* p:kArmBones)if(std::wcsncmp(name,p,std::wcslen(p))==0)return true;
    return false;
}

// The stock step put the camera far off and high over the soldier (the Air Raider's strike view, measured: +0x224 is 1
// for the Air Raider all the time, so it does not tell): the stock view stays. Called before this file moves the eye.
bool GadgetView(const unsigned char* cam,const unsigned char* h) noexcept {
    if(At<const unsigned char*>(h,0)!=image+kSoldierVts[3] || h[kRagdoll])return false;
    const float* eye=reinterpret_cast<const float*>(cam+kCamEyeRow);
    const float* at=reinterpret_cast<const float*>(h+kPos);
    const float d[3]={eye[0]-at[0],eye[1]-at[1],eye[2]-at[2]};
    return d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>kGadgetFar*kGadgetFar && d[1]>kGadgetHigh;
}

// In a vehicle, why its own view keeps the camera (nullptr: none does): the high view over the round's fall and its
// way back (turretcam.cpp), the seat's mounted optic (sightzoom.cpp: War Thunder's gunner sight, at every
// magnification while first person is on: the view on the gun, its reticle; the seats without one see from their
// cockpit or station monitor).
// A seat without its cockpit's eye (no vc_eye_ marker: fp_vehicle.cpp) keeps the stock camera too, as a reason here:
// found only in the camera step (Ride), it turned first person on and off again every frame (measured 2026-10-14: the
// gunship's gun seat, "FP view on" / "back to the stock camera" 190 times a minute).
const char* VehicleYields(unsigned char* h) noexcept {
    const void* v=At<const void*>(h,kHumanVehicle);
    if(HighCamOn(v) || TurretCamHighTransition(v))return "the high view";
    if(SightZoomMounted(v))return "the gunner's sight";   // at 1x too in first person (FirstPersonGunnerSight)
    float eye[3];
    RideView ride{};
    if(!VehicleEye(h,eye,&ride))return "no cockpit in this seat";
    return nullptr;
}

// Why this machine's player is not to be seen in first person through `cam` now (nullptr: they are).
const char* NotWanted(const unsigned char* cam,unsigned char* h) noexcept {
    const Config& c=Cfg();
    if(!camOk || !drawOk)return "hooks unavailable";
    if(!c.enabled || !c.firstPerson || !toggled)return "off";
    if(!h || !IsSoldier(h))return "no soldier";
    if(At<const void*>(cam,kCamTarget)!=h && At<const void*>(cam,kCamTargetRef)!=h)return "another camera";
    if(h[kDead])return "dead";
    if(!HumanOnFoot(h))return c.fpVehicle ? VehicleYields(h) : "riding";
    if(GadgetView(cam,h))return "a class gadget's view";
    return nullptr;
}

bool Wanted(const unsigned char* cam,unsigned char* h) noexcept {
    const char* why=NotWanted(cam,h);
    static ULONGLONG at=0;
    if(why && toggled && h && Cfg().debug && GetTickCount64()-at>=5000) {
        at=GetTickCount64();
        Log("FP not on camera %p: %s (special view %d, vtable +0x%llX)",static_cast<const void*>(cam),why,
            At<std::int32_t>(h,kSpecialView),static_cast<unsigned long long>(At<const unsigned char*>(h,0)-image));
    }
    return !why;
}

// Whether a vehicle the player rides uses the key `vk` itself: the Sazabi's keys (sazabi.cpp; its melee is V too).
bool KeyTaken(const unsigned char* h,int vk) noexcept {
    const Config& c=Cfg();
    if(HumanOnFoot(h) || !IsSazabi(At<const void*>(h,kHumanVehicle)))return false;
    return vk==c.sazabiMeleeKey || vk==c.sazabiGuardKey || vk==c.sazabiFunnelKey || vk==c.sazabiCannonKey ||
           vk==c.sazabiDashKey || vk==c.sazabiDescendKey || vk==c.sazabiLockKey;
}

// The key switches first person on foot and in a vehicle (one switch: the user, 2026-10-13, "共用"), on its release:
// held while boarding it chose the gunship's gunner seat (GunshipGunnerKey, V too, playerjet.cpp), so a press over
// which the player got in or out switches nothing (it used to switch on the press, the boarding with it). Where the
// vehicle uses the key itself (KeyTaken) it is that vehicle's: the switch stays as it was set before boarding.
bool ToggleDown() noexcept {
    const Config& c=Cfg();
    const bool down=c.firstPersonKey>0 && (edf::AsyncKey(c.firstPersonKey)&0x8000)!=0;
    if(down && drawOwner[0] && !drawOwnerToldOnKey && c.enabled && c.firstPerson) {
        drawOwnerToldOnKey=true;
        Log("FP key 0x%X: first person is off, the model draw is %s's (another first-person mod)",c.firstPersonKey,drawOwner);
    }
    if(down && !keyPhysicalDown){if(++keyPress==0)++keyPress;keyClaimHuman=nullptr;keyClaimControl=nullptr;}
    keyPhysicalDown=down;
    return down;
}
void ResetToggleGestures() noexcept {
    ToggleDown();keyConsumed=keyPress;keyClaimHuman=nullptr;keyClaimControl=nullptr; // consume any in-flight press
}
bool BindToggle(const unsigned char* human,bool down) noexcept {
    auto& key=Current().key;
    const void* control=human && Readable(human,kSelfCtrl+8) ? At<const void*>(human,kSelfCtrl) : nullptr;
    if(!human || !control){key.was=false;key.cancelled=down;key.press=0;return false;}
    if(key.human!=human || key.control!=control) {
        const bool replaced=key.human!=nullptr;
        key=KeyGesture{};key.human=human;key.control=control;key.cancelled=replaced && down;
    }
    return true;
}
void CancelToggle(const unsigned char* human) noexcept {
    const bool down=ToggleDown();
    if(human)BindToggle(human,down);
    Current().key.was=false;Current().key.cancelled=down;
}
void Toggle(const unsigned char* human) noexcept {
    const Config& c=Cfg();
    const bool down=ToggleDown();
    if(!BindToggle(human,down))return;
    auto& key=Current().key;
    // Ownership loss cancels only this camera's gesture. Keep its cancellation
    // through the map's close/drain or enemy-control exit until physical release.
    if(!c.enabled || !c.firstPerson || !camOk || !drawOk || !KeyInputAllowed(human) || KeyTaken(human,c.firstPersonKey)) {
        key.was=false;key.cancelled=down;return;
    }
    if(key.cancelled){if(!down)key.cancelled=false;return;}
    const bool riding=!HumanOnFoot(human);
    if(down && !key.was) {
        key.rode=riding;key.press=keyPress;
        if(!keyClaimHuman){keyClaimHuman=human;keyClaimControl=key.control;}
    }
    if(!down && key.was && key.rode==riding && key.press==keyPress && key.press!=keyConsumed &&
       keyClaimHuman==human && keyClaimControl==key.control) {
        keyConsumed=key.press;
        const bool on=!toggled.load();toggled.store(on);Log("FP %s (key 0x%X)",on ? "on" : "off",c.firstPersonKey);
    }
    key.was=down;
}

void Gather(unsigned char* h) noexcept {
    Current().body=h+kBodyModel;Current().bodyCtrl=At<const void*>(h,kSelfCtrl);
    Current().weaponCount=0;
    const auto list=At<unsigned char* const*>(h,kWeapons);
    const auto n=At<std::uint64_t>(h,kWeaponCount);
    if(!list || n>kMostWeapons)return;
    for(std::uint64_t i=0;i<n;++i) {
        if(!list[i])continue;
        const unsigned char* model=list[i]+kWeaponModel;
        Current().weapons[Current().weaponCount]=model;Current().weaponCtrls[Current().weaponCount++]=At<const void*>(list[i],kSelfCtrl);
        HookDrawOf(At<void**>(model,0));   // a weapon's model class may be its own (its slot 1 still the stock draw)
    }
}

const unsigned char* SetWeapon(const unsigned char* h,std::uint64_t k) noexcept {
    const auto sets=At<const unsigned char*>(h,kSets);
    if(!sets)return nullptr;
    const auto entry=At<unsigned char* const*>(sets+k*kSetStride,kSetWeapon);
    return entry ? *entry : nullptr;
}

fpmotion::Move MoveOf(std::uint32_t act) noexcept {
    for(std::uint32_t v:kRolls)if(act==v)return fpmotion::Move::roll;
    for(std::uint32_t v:kSprints)if(act==v)return fpmotion::Move::sprint;
    for(std::uint32_t v:kDashes)if(act==v)return fpmotion::Move::dash;
    return fpmotion::Move::none;
}

float Wrap(float a) noexcept {
    while(a>3.14159265f)a-=6.2831853f;
    while(a<-3.14159265f)a+=6.2831853f;
    return a;
}

// The recoil a shot of `w` kicks with (0.3..1 from its FireRecoil).
float RecoilOf(const unsigned char* w) noexcept {
    const float r=At<float>(w,kFireRecoil);
    return std::isfinite(r) ? 0.3f+0.7f*std::fmin(std::fabs(r),1.0f) : 0.5f;
}

// One hand's weapon this frame: its shots, and the frames its reload has left (`*reload`, the larger of the hands').
// The reload is the game's own count (+0xE68 while the state is 3 or 4), not ours from ReloadTime at the state's start:
// that one ran on through a reload cut short, a weapon switch or the second hand's pause, and missed the _finish.
void Hand(const unsigned char* w,std::uint64_t k,std::int32_t shots,std::int32_t fire,fpmotion::Cue& c,int* reload) noexcept {
    const std::int32_t d=shots-Current().shots[k];
    int fired=d>0 && d<10 ? d : 0;
    Current().sampledShots+=fired;
    float recoil=RecoilOf(w);
    const int hand=HandOfModel(w+kWeaponModel);
    if(hand>=0) {
        auto& event=Current().shotEvent[hand];
        if(event.weapon==w){Current().notifiedShots+=event.count;fired=(std::max)(fired,event.count);recoil=std::fmax(recoil,event.recoil);event=State::ShotEvent{};}
    }
    if(fired){c.shots+=fired;c.recoil=std::fmax(c.recoil,recoil);}
    const std::int32_t left=fire==3 || fire==4 ? At<std::int32_t>(w,kReloadLeft) : 0;
    if(left>0)*reload=(std::max)(*reload,left<kMostReloadFrames ? left : kMostReloadFrames);
    if(fire==3 && Current().fire[k]!=3 && Cfg().debug)
        Log("FP reload %d of %d frames (FireRecoil %.3f)",left,At<std::int32_t>(w,kReloadTime),At<float>(w,kFireRecoil));
}

// The hands' weapons this frame: shots, a reload started, a switch.
void Weapons(const unsigned char* h,fpmotion::Cue& c) noexcept {
    Current().sampledShots=Current().notifiedShots=0;
    int reload=0;
    auto n=At<std::uint64_t>(h,kSetCount);
    if(n>kMostSets)n=kMostSets;
    for(std::uint64_t k=0;k<n;++k) {
        const unsigned char* w=SetWeapon(h,k);
        const std::int32_t shots=w ? At<std::int32_t>(w,kShotCount) : 0,fire=w ? At<std::int32_t>(w,kFireState) : 0;
        if(Current().have && w!=Current().held[k] && Current().held[k])c.switched=true;
        if(Current().have && w && w==Current().held[k])Hand(w,k,shots,fire,c,&reload);
        Current().held[k]=w;Current().shots[k]=shots;Current().fire[k]=fire;
    }
    Current().reloadLeft=reload;
}

// What the soldier did since the last frame (fp_motion.h Cue), the readings kept for the next.
fpmotion::Cue Cues(const unsigned char* h) noexcept {
    fpmotion::Cue c{};c.sway=Cfg().fpSway;
    const float pitch=At<float>(h,kLook),yaw=At<float>(h,kLook+4);
    float pos[3];
    std::memcpy(pos,h+kPos,12);
    const std::int32_t ground=At<std::int32_t>(h,kGroundFrames);
    const std::uint32_t act=At<std::uint32_t>(h,kActLimit);
    c.grounded=h[kSupport]!=2;
    if(Current().have) {
        c.lookYaw=Wrap(yaw-Current().yaw);
        c.lookPitch=-(pitch-Current().pitch);
        const float dx=pos[0]-Current().pos[0],dz=pos[2]-Current().pos[2];
        c.speed=std::sqrt(dx*dx+dz*dz);
        if(c.speed>3.0f)c.speed=0.0f;   // a warp, not a walk
        if(ground>0 && Current().groundFrames==0 && Current().vy<0.0f)c.fallSpeed=-Current().vy;
        Current().vy=pos[1]-Current().pos[1];
        if(act!=Current().act && Cfg().debug)
            Log("FP act 0x%X -> 0x%X (state %p / %p)",Current().act,act,At<const void*>(h,0x1F00),At<const void*>(h,0x2458));
    }
    Weapons(h,c);
    c.reloading=Current().reloadLeft>0;
    c.move=MoveOf(act);
    Current().pitch=pitch;Current().yaw=yaw;std::memcpy(Current().pos,pos,12);Current().groundFrames=ground;Current().act=act;Current().have=true;
    return c;
}

// The motion this frame about each hand's pivot (right is -x in the camera's frame: x, yaw and roll mirrored).
void Motion(const unsigned char* h,const float (*pivot)[3]) noexcept {
    const auto cue=Cues(h);
    if(Cfg().debug){Current().probeSpeed=std::fmax(Current().probeSpeed,cue.speed);Current().probeAir+=!cue.grounded;++Current().probeFrames;}
    const fpmotion::Pose p=fpmotion::Advance(Current().motion,cue);
    if(Cfg().debug && cue.shots)Log("FP recoil: shots %d (counter %d events %d), strength %.3f, back %.4f pitch %.4f",
                                  cue.shots,Current().sampledShots,Current().notifiedShots,cue.recoil,p.z,p.pitch);
    for(int hand=kRight;hand<=kLeft;++hand)Current().moved[hand]=fpview::MotionMatrix(-p.x,p.y,p.z,p.pitch,-p.yaw,-p.roll,pivot[hand]);
}

// A model's root bone as it will be drawn (its base LOD's draw pose, bone 0).
bool RootPose(const unsigned char* model,M4* out) noexcept {
    const auto inst=At<const unsigned char*>(model,kBaseLod+kLodInst);
    if(!inst || At<std::int32_t>(inst,kInstCount)<=0)return false;
    const auto pose=At<const unsigned char*>(inst,kInstPose);
    if(!pose)return false;
    *out=MatrixAt(pose);
    return true;
}

// The shot reads muzzle-node bone records (+0xB0), rather than the draw palette. Capture its offset in that
// same native root frame; the draw palette may already belong to a different queued render/update frame.
bool ShotRoot(const unsigned char* model,M4* out) noexcept {
    const auto inst=At<const unsigned char*>(model,kBaseLod+kLodInst);
    if(!inst || At<std::int32_t>(inst,kInstCount)<=0)return false;
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    if(!bones)return false;
    *out=MatrixAt(bones+kBoneWorld);
    return fpview::Finite(*out);
}

// Umbra's place of a model: a box around `eye` (kEyeBox metres each way), or its real bounds (`eye` null: the box the
// update computes from the bones, 0x11005E0 at the skeleton instance's +0x70 = model+0x110, axes scaled by the extents;
// 0x6C06A0 hands that to 0x11B30C0, which keeps model+0x110 as it is).
// Only a model Umbra knows (its object at +0x10: 0x11B2870, the test 0x68FD00 makes before it moves a weapon; a weapon
// not in hand has none) and a box that is one (finite, sane axes, near the player): a bad one crashed Umbra's
// visibility thread (umbra_sandlot Camera::resolveVisibility, run 6).
bool Placeable(const unsigned char* model,const M4& box) noexcept {
    if(!At<const void*>(model,kUmbraObject) || !fpview::Finite(box))return false;
    for(int r=0;r<3;++r) {
        const float* a=box.m+r*4;
        const float len=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
        if(!(len>1e-4f && len<50.0f))return false;
    }
    const float* at=reinterpret_cast<const float*>(Current().human+kPos);
    const float d[3]={box.m[12]-at[0],box.m[13]-at[1],box.m[14]-at[2]};
    return d[0]*d[0]+d[1]*d[1]+d[2]*d[2]<100.0f*100.0f;
}

void Place(const void* model,const float* eye) noexcept {
    if(!placeOk || !Cfg().fpCulling || !model || !Current().human)return;
    const auto m=static_cast<const unsigned char*>(model);
    const M4 box=MatrixAt(m+kModelBounds);
    if(!Placeable(m,box)) {
        static const void* logged=nullptr;
        if(logged!=model && Cfg().debug && At<const void*>(m,kUmbraObject)){logged=model;Log("FP culling: model %p's box is not one (left alone)",model);}
        return;
    }
    M4 at=box;
    if(eye){at=fpview::Scale(kEyeBox);std::memcpy(at.m+12,eye,12);}
    reinterpret_cast<PlaceFn>(image+kUmbraPlace)(const_cast<void*>(model),at.m);
}

// The body and the weapons in hand around the eye, or (`real`) where they are.
void PlaceAll(bool real) noexcept {
    const float* eye=real ? nullptr : reinterpret_cast<const float*>(Current().cam+kCamEyeRow);
    if(ModelOwnerIs(Current().body,kBodyModel,Current().bodyCtrl))Place(Current().body,eye);
    for(int i=0;i<Current().inHandCount;++i)
        if(ModelOwnerIs(Current().inHand[i],kWeaponModel,Current().inHandCtrls[i]))Place(Current().inHand[i],eye);
}

// The hand weapon `w` hangs in: the name of its mount bone ends in _l for the left (arms_l, armsFlip_l...).
int HandOf(const unsigned char* w) noexcept {
    const auto mount=At<const unsigned char*>(w,kWeaponMount);
    const auto name=mount ? At<const wchar_t*>(mount,0) : nullptr;
    const std::size_t n=name ? std::wcslen(name) : 0;
    return n>=2 && name[n-2]==L'_' && name[n-1]==L'l' ? kLeft : kRight;
}

// Debug=1, once a weapon: its model's class (vtable, draw slot, hooked) and the hand it is taken to be in.
void LogWeapon(const unsigned char* w,int hand) noexcept {
    static const void* logged[16]{};
    static int next=0;
    if(!Cfg().debug)return;
    for(const void* l:logged)if(l==w)return;
    logged[next]=w;next=(next+1)%16;
    const unsigned char* model=w+kWeaponModel;
    void** vt=At<void**>(model,0);
    bool hookedHere=false;
    for(int i=0;i<hookedCount;++i)hookedHere=hookedHere || hooked[i].vtable==vt;
    const auto mount=At<const unsigned char*>(w,kWeaponMount);
    const auto name=mount ? At<const wchar_t*>(mount,0) : nullptr;
    const M4 box=MatrixAt(model+kModelBounds);
    float axis[3];
    for(int r=0;r<3;++r)axis[r]=std::sqrt(box.m[r*4]*box.m[r*4]+box.m[r*4+1]*box.m[r*4+1]+box.m[r*4+2]*box.m[r*4+2]);
    Log("FP weapon %p: vtable +0x%llX (weapon +0x%llX) draw slot +0x%llX hooked=%d mount %ls -> %s hand, umbra %p, box %.2f x %.2f x %.2f",
        static_cast<const void*>(w),static_cast<unsigned long long>(reinterpret_cast<unsigned char*>(vt)-image),
        static_cast<unsigned long long>(At<const unsigned char*>(w,0)-image),
        static_cast<unsigned long long>(static_cast<unsigned char*>(vt[kModelDrawSlot])-image),hookedHere,
        name ? name : L"?",hand==kLeft ? "left" : "right",At<const void*>(model,kUmbraObject),axis[0],axis[1],axis[2]);
}

bool StillPlayers(const void* model,const void* control) noexcept {
    for(int i=0;i<Current().weaponCount;++i)if(Current().weapons[i]==model && Current().weaponCtrls[i]==control && ModelOwnerIs(model,kWeaponModel,control))return true;
    return false;
}

// The batches back: of the weapons no longer in hand (all: `all`), into those still the player's.
void Rebatch(bool all) noexcept {
    for(Unbatched& u:Current().unbatched) {
        if(!u.model)continue;
        if(!ModelOwnerIs(u.model,kWeaponModel,u.ctrl)){u=Unbatched{};continue;}
        if(!all && HandOfModel(u.model)>=0)continue;
        if(StillPlayers(u.model,u.ctrl) && !At<void*>(u.model,kBatch))Put<void*>(const_cast<void*>(u.model),kBatch,u.batch);
        u=Unbatched{};
    }
}

// The weapons in hand out of their batches (kept).
void Unbatch() noexcept {
    for(int i=0;i<Current().inHandCount;++i) {
        if(!ModelOwnerIs(Current().inHand[i],kWeaponModel,Current().inHandCtrls[i]))continue;
        void* const batch=At<void*>(Current().inHand[i],kBatch);
        if(!batch)continue;
        for(Unbatched& u:Current().unbatched) {
            if(u.model)continue;
            u=Unbatched{Current().inHand[i],batch,Current().inHandCtrls[i]};
            Put<void*>(const_cast<void*>(Current().inHand[i]),kBatch,nullptr);
            if(Cfg().debug)Log("FP weapon model %p out of its batch %p",Current().inHand[i],batch);
            break;
        }
    }
}

// The weapons in hand and, for each hand, the root of its weapon as it will be drawn (the draw pose's bone 0:
// 0x1100B90 writes the weapon's matrix to its first bone record, 0x1100F10 copies it into the draw pose).
void Hands(const unsigned char* h,M4* anchor,bool* anchored) noexcept {
    anchored[kRight]=anchored[kLeft]=false;
    Current().inHandCount=0;
    auto n=At<std::uint64_t>(h,kSetCount);
    if(n>kMostSets)n=kMostSets;
    for(std::uint64_t k=0;k<n;++k) {
        const unsigned char* w=SetWeapon(h,k);
        if(!w)continue;
        const int hand=HandOf(w);
        Current().inHand[Current().inHandCount]=w+kWeaponModel;Current().inHandCtrls[Current().inHandCount]=At<const void*>(w,kSelfCtrl);Current().handOf[Current().inHandCount++]=hand;
        LogWeapon(w,hand);
        if(!anchored[hand])anchored[hand]=RootPose(w+kWeaponModel,&anchor[hand]);
    }
}

// The hand a weapon model is in, -1: not in hand.
int HandOfModel(const void* model) noexcept {
    for(int i=0;i<Current().inHandCount;++i)if(Current().inHand[i]==model && ModelOwnerIs(model,kWeaponModel,Current().inHandCtrls[i]))return Current().handOf[i];
    return -1;
}

// A flash owns its muzzle-to-weapon transform, captured at that shot. The native muzzle matrix is rewritten only
// when the gun fires (and reused by the next shot), so keeping its pointer and multiplying it by a later T makes
// the old world position drift against a moving weapon. Rebuild from the live weapon root instead.
void Flashes() noexcept {
    for(Flash& f:Current().flashes) {
        if(!f.model)continue;
        if(HandOfModel(f.model)!=f.hand){f.model=nullptr;continue;}   // never read a removed weapon's pose
        M4 root;
        if(Current().viewOk[f.hand] && RootPose(static_cast<const unsigned char*>(f.model),&root))
            std::memcpy(f.m,fpview::Mul(fpview::Mul(f.local,root),Current().view[f.hand]).m,64);
    }
}

// A shot's flash about to be made for weapon `w`: in first person, its matrix pointer is one of the Current().flashes'.
Flash* Relocate(unsigned char* w) noexcept {
    const int hand=Current().on ? HandOfModel(w+kWeaponModel) : -1;
    if(hand<0 || !Current().viewOk[hand])return nullptr;
    auto& event=Current().shotEvent[hand];
    if(event.weapon!=w)event=State::ShotEvent{w,0,0};
    if(event.count<3)++event.count;
    event.recoil=RecoilOf(w);
    const auto src=At<const float*>(w,kFlashMatrix);
    if(!src)return nullptr;
    M4 root,inverse;
    if(!ShotRoot(w+kWeaponModel,&root) || !fpview::InverseAffine(root,&inverse))return nullptr;
    Flash* free=nullptr;
    for(int k=0;k<kFlashSlots;++k) {
        Flash& candidate=Current().flashes[(Current().flashNext+k)%kFlashSlots];
        if(candidate.model || candidate.effect)continue; // never rebind a matrix an older effect still reads
        free=&candidate;Current().flashNext=(Current().flashNext+k+1)%kFlashSlots;break;
    }
    if(!free)return nullptr;   // native flash, rather than another live shot's matrix
    Flash& f=*free;
    const M4 shot=MatrixAt(reinterpret_cast<const unsigned char*>(src));
    f.local=fpview::Mul(shot,inverse);f.model=w+kWeaponModel;f.effect=nullptr;f.hand=hand;
    std::memcpy(f.m,fpview::Mul(shot,Current().view[hand]).m,64);
    Put<const float*>(w,kFlashMatrix,f.m);
    return &f;
}

// The start of a line `start` (x, y, z, w) moved through T into `out` when it is the player's in first person.
bool MovedStart(const float* start,float* out) noexcept {
    const int hand=Current().viewOk[kRight] ? kRight : kLeft;
    if(!Current().on || !Current().viewOk[hand] || !Current().human)return false;
    const float* at=reinterpret_cast<const float*>(Current().human+kPos);
    const float d[3]={start[0]-at[0],start[1]-at[1]-kChestUp,start[2]-at[2]};
    if(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>kAimLineReach*kAimLineReach)return false;
    fpview::Point(Current().view[hand],start,out);
    out[3]=start[3];
    return true;
}

using LineFn=void*(__fastcall*)(void*,const float*,const float*,const float*);
void* __fastcall AimLineHook(void* line,const float* start,const float* velocity,const float* gravity) {
    alignas(16) float moved[4];
    const float* from=start;
    State* before=selected;
    __try {
        int matches=0;
        for(auto& state:states)if(state.on) {
            selected=&state;float candidate[4];
            if(MovedStart(start,candidate)){++matches;std::memcpy(moved,candidate,16);}
        }
        selected=before;if(matches==1)from=moved;
    } __except(EXCEPTION_EXECUTE_HANDLER){from=start;}
    selected=before;
    return reinterpret_cast<LineFn>(image+kAimLineBuild)(line,from,velocity,gravity);
}

// The factory returns the raw GameObject, and every supported MuzzleFlashBase descendant borrows +0x120's
// matrix until its real deleting destructor. Track both routes: Umbra's secondary destructor thunk jumps straight
// to the primary body, so hooking only the primary vtable would miss that route. Native profile is audited offline.
struct FlashClass {
    unsigned vtable,dtor,umbra,umbraDtor;
    void* next=nullptr;void* umbraNext=nullptr;
};
FlashClass flashClasses[]={
    {0x17A5F98,0x2BA580,0,0},           // MuzzleFlashBase
    {0x17A6068,0x2BAED0,0,0},           // EfsMuzzleFlash
    {0x17A6158,0x2BBA10,0x17A61C0,0x2BBA04}, // LaserMuzzleFlash01
    {0x17A6268,0x2BCCF0,0x17A62D0,0x2BCCE4}, // LaserMuzzleFlash02
    {0x17A6378,0x2BE660,0x17A63E0,0x2BE654}, // LaserMuzzleFlash03
    {0x17A6478,0x2BF990,0x17A64E0,0x2BF984}, // MuzzleFlash01
    {0x17A6578,0x2C0A70,0x17A65E0,0x2C0A64}, // MuzzleFlash02
    {0x17A6678,0x2C1800,0,0},           // MuzzleFlashRailGun
    {0x17A6880,0x2C3120,0x17A68E8,0x2C3114}, // PileBunkerMuzzleFlash01
    {0x17A6978,0x2C3FB0,0x17A69E0,0x2C3F98}, // TestMuzzleFlash
};
using FlashDtorFn=void*(__fastcall*)(void*,unsigned);
void RetireFlash(const void* effect) noexcept {
    for(auto& state:states)for(auto& f:state.flashes)if(f.effect==effect){f.effect=nullptr;f.model=nullptr;}
}
template<int Class> void* __fastcall FlashDtorHook(void* effect,unsigned flags) {
    RetireFlash(effect);
    return reinterpret_cast<FlashDtorFn>(flashClasses[Class].next)(effect,flags);
}
template<int Class> void* __fastcall FlashUmbraDtorHook(void* object,unsigned flags) {
    RetireFlash(static_cast<unsigned char*>(object)-0x130);
    return reinterpret_cast<FlashDtorFn>(flashClasses[Class].umbraNext)(object,flags);
}
const FlashDtorFn flashDtorHooks[]={
    &FlashDtorHook<0>,&FlashDtorHook<1>,&FlashDtorHook<2>,&FlashDtorHook<3>,&FlashDtorHook<4>,
    &FlashDtorHook<5>,&FlashDtorHook<6>,&FlashDtorHook<7>,&FlashDtorHook<8>,&FlashDtorHook<9>,
};
const FlashDtorFn flashUmbraDtorHooks[]={
    &FlashUmbraDtorHook<0>,&FlashUmbraDtorHook<1>,&FlashUmbraDtorHook<2>,&FlashUmbraDtorHook<3>,&FlashUmbraDtorHook<4>,
    &FlashUmbraDtorHook<5>,&FlashUmbraDtorHook<6>,&FlashUmbraDtorHook<7>,&FlashUmbraDtorHook<8>,&FlashUmbraDtorHook<9>,
};
bool HookFlashLifetime(void* effect) noexcept {
    const auto vt=At<const unsigned char*>(effect,0);
    for(auto& c:flashClasses)if(vt==image+c.vtable) {
        const auto index=static_cast<std::size_t>(&c-flashClasses);
        if(!c.next) {
            auto slot=reinterpret_cast<void**>(image+c.vtable)+1;
            if(*slot!=image+c.dtor || !edf::ChainVtableSlot(slot,reinterpret_cast<void*>(flashDtorHooks[index]),&c.next))return false;
        }
        if(c.umbra && !c.umbraNext) {
            if(At<const unsigned char*>(effect,0x130)!=image+c.umbra)return false;
            auto slot=reinterpret_cast<void**>(image+c.umbra);
            if(*slot!=image+c.umbraDtor || !edf::ChainVtableSlot(slot,reinterpret_cast<void*>(flashUmbraDtorHooks[index]),&c.umbraNext))return false;
        }
        return true;
    }
    return false;   // an unknown/custom effect retains its native matrix and ownership
}
void* __fastcall FlashCreateHook(void* factory,void* a,void* cls,void* param) {
    // Keep native creation/shot world transforms intact. Only after a supported effect and its destructor exist
    // does it borrow one of our matrices; a rejected custom class or a full pool remains entirely native.
    void* const effect=reinterpret_cast<CreateFn>(image+kEffectCreate)(factory,a,cls,param);
    if(!effect)return effect;
    State* before=selected;
    __try {
        auto weapon=static_cast<unsigned char*>(param)-kFlashParam;
        for(auto& state:states)if(state.on && !state.overridden) {
            selected=&state;
            if(HandOfModel(weapon+kWeaponModel)<0)continue;
            if(HookFlashLifetime(effect)) {
                const auto native=At<const float*>(weapon,kFlashMatrix);
                if(auto f=Relocate(weapon)){f->effect=effect;Put<const float*>(effect,0x120,f->m);}
                Put<const float*>(weapon,kFlashMatrix,native);
            }
            break;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    selected=before;
    return effect;
}

// Debug=1: what the viewmodel is built from, every 5 s (every half second through a reload).
void Probe(const bool* anchored,const M4* anchor,const M4& camera) noexcept {
    if(!Cfg().debug)return;
    // The steadiness (docs/fp-render-re.md §17): each hand's weapon root in the camera's own frame (x left, y up, z
    // ahead) frame to frame, the most it moved in a frame over the window.
    M4 inverse;
    const bool invOk=fpview::InverseAffine(camera,&inverse);
    float root[2][3]{};
    for(int hand=kRight;hand<=kLeft;++hand) {
        if(!invOk || !anchored[hand] || !Current().viewOk[hand]){Current().probeHave[hand]=false;continue;}
        float world[3];fpview::Point(Current().view[hand],anchor[hand].m+12,world);fpview::Point(inverse,world,root[hand]);
        if(Current().probeHave[hand]) {
            float d[3];fpview::Sub(root[hand],Current().probeRoot[hand],d);
            Current().probeStep[hand]=std::fmax(Current().probeStep[hand],fpview::Length(d));
        }
        std::memcpy(Current().probeRoot[hand],root[hand],12);Current().probeHave[hand]=true;
        // The body's own mount bone (the weapon's root natively is that bone's matrix, fp-render-re.md §8) as drawn,
        // against the weapon's root as drawn, in the root's frame: equal when both were updated in the same step.
        bool flip=false;
        const unsigned char* w=HandWeapon(hand,&flip);
        const auto mount=w ? At<const unsigned char*>(w,kWeaponMount) : nullptr;
        const auto name=mount ? At<const wchar_t*>(mount,0) : nullptr;
        const auto inst=Current().body ? At<const unsigned char*>(Current().body,kBaseLod+kLodInst) : nullptr;
        const auto pose=inst ? At<const float*>(inst,kInstPose) : nullptr;
        const int at=name && inst && pose ? BoneIndex(inst,name) : -1;
        M4 rootInv;
        if(at>=0 && fpview::InverseAffine(anchor[hand],&rootInv)) {
            float off[3];fpview::Point(rootInv,pose+at*16+12,off);
            const float gap=fpview::Length(off);
            Current().probeMountGap[hand]=std::fmax(Current().probeMountGap[hand],gap);
            Current().probeMountLeast[hand]=Current().probeMountHave[hand] ? std::fmin(Current().probeMountLeast[hand],gap) : gap;
            Current().probeMountHave[hand]=true;
        }
    }
    static ULONGLONG at=0;
    const ULONGLONG now=GetTickCount64();
    if(now-at<(Current().reloadLeft>0 ? 500u : 5000u))return;
    at=now;
    const float* eye=reinterpret_cast<const float*>(Current().cam+kCamEyeRow);
    const float* f=camera.m+8;
    const float yaw=std::atan2(f[0],f[2])*57.29578f;
    for(int hand=kRight;hand<=kLeft;++hand) {
        const M4& a=anchor[hand];
        Log("FP probe %s hand: anchored=%d view=%d | root (%.2f,%.2f,%.2f) from the eye | facing %.0f deg | root in the camera (%.3f,%.3f,%.3f), most moved in a frame %.4f | in hand %d, reload %d, act 0x%X",
            hand==kRight ? "right" : "left",anchored[hand],Current().viewOk[hand],a.m[12]-eye[0],a.m[13]-eye[1],a.m[14]-eye[2],
            yaw,root[hand][0],root[hand][1],root[hand][2],Current().probeStep[hand],
            Current().inHandCount,Current().reloadLeft,Current().act);
        if(Current().probeMountHave[hand])
            Log("FP probe %s mount: the body's mount bone from the weapon's root as drawn, %.4f..%.4f m (scaled by nothing: world)",
                hand==kRight ? "right" : "left",Current().probeMountLeast[hand],Current().probeMountGap[hand]);
        Current().probeMountHave[hand]=false;Current().probeMountGap[hand]=0.0f;
        Current().probeStep[hand]=0.0f;
        // What the hold was fitted to before (the Umbra box, world axes) against the weapon's own size, this frame.
        bool flip=false;
        const unsigned char* w=HandWeapon(hand,&flip);
        M4 own;
        if(w && HandBounds(hand,w,&own)) {
            float worldHalf[3],ownHalf[3],before[3]{},after[3]{};
            fpview::HalfSizes(MatrixAt(w+kWeaponModel+kModelBounds),worldHalf);fpview::HalfSizes(own,ownHalf);
            fpview::FitGrip(worldHalf,false,flip,1.0f,before);fpview::FitGrip(ownHalf,false,flip,1.0f,after);
            Log("FP probe %s weapon: Umbra box %.2f x %.2f x %.2f -> fit (%.3f,%.3f,%.3f); own size %.2f x %.2f x %.2f -> fit (%.3f,%.3f,%.3f)",
                hand==kRight ? "right" : "left",worldHalf[0],worldHalf[1],worldHalf[2],before[0],before[1],before[2],
                ownHalf[0],ownHalf[1],ownHalf[2],after[0],after[1],after[2]);
        }
    }
    Log("FP probe camera: submitted %u frames, %u of them not the matrix the viewmodel was built on (most off %.4f)",
        Current().probeSubmits,Current().probeSubmitsOff,Current().probeSubmitOff);
    Log("FP probe order: %u camera steps (thread %u)",Current().probeCamSteps,Current().probeCamThread);
    Current().probeCamSteps=0;
    Log("FP probe motion: %u frames, fastest %.3f m/frame, %u in the air, bob phase %.2f",Current().probeFrames,
        Current().probeSpeed,Current().probeAir,Current().motion.phase);
    Current().probeSubmits=Current().probeSubmitsOff=0;Current().probeSubmitOff=0.0f;
    Current().probeFrames=Current().probeAir=0;Current().probeSpeed=0.0f;
}

// Return only the near-plane value this camera still owns. The outer map/another camera feature may already
// have written its own precision; its latest value must not be replaced by an older first-person baseline.
void ReturnNear(unsigned char* cam) noexcept {
    if(!Current().nearSet)return;
    if(At<float>(cam,kCamNear)==Current().writtenNear)ViewWriteNear(cam,Current().savedNear);
    Current().nearSet=false;
}

// The camera, the player's state given back: the near clip, the look smoothing.
void Leave() noexcept {
    const bool humanOwned=Current().human && ModelOwnerIs(Current().human+kBodyModel,kBodyModel,Current().bodyCtrl);
    if(Current().on && humanOwned){PlaceAll(true);Rebatch(true);}
    if(Current().cam)ReturnNear(Current().cam);
    if(humanOwned && Current().smoothSet)Put<float>(Current().human,kLookSmooth,Current().savedSmooth);
    if(Current().on)Log("FP view back to the stock camera");
    // Keep the camera binding even when first person is off: its independent free-look restore still runs.
    // Its submitted views go with it: a draw still to come of a first-person frame is drawn stock, never routed to a
    // state with no viewmodel published (nor to another player's).
    auto cam=Current().cam;const auto applied=Current().cameraApplied;const auto key=Current().key;
    Flash flashes[kFlashSlots];std::memcpy(flashes,Current().flashes,sizeof(flashes));const int flashNext=Current().flashNext;
    for(auto& f:flashes)f.model=nullptr;   // freeze detached effects; their destructor releases the borrowed matrix
    Current()=State{};Current().cam=cam;Current().cameraApplied=applied;Current().key=key;
    ClearViews(static_cast<int>(selected-states));
    std::memcpy(Current().flashes,flashes,sizeof(flashes));Current().flashNext=flashNext;
}

// The weapon in `hand` (nullptr: none) and whether it hangs on a flipped mount (armsFlip_*: the Fencer's shields).
const unsigned char* HandWeapon(int hand,bool* flip) noexcept {
    *flip=false;
    for(int i=0;i<Current().inHandCount;++i) {
        if(Current().handOf[i]!=hand)continue;
        const unsigned char* w=static_cast<const unsigned char*>(Current().inHand[i])-kWeaponModel;
        const auto mount=At<const unsigned char*>(w,kWeaponMount);
        const auto name=mount ? At<const wchar_t*>(mount,0) : nullptr;
        *flip=name && std::wcsncmp(name,L"armsFlip",8)==0;
        return w;
    }
    return nullptr;
}

bool NativeGrip(const unsigned char* weapon) noexcept {
    if(!weapon || !image)return false;
    // Read the class contract, not a per-weapon model/row exception. All pointers and descriptor RVAs must belong
    // to the already verified native image. Unknown/custom classes keep the existing firearm placement.
    const auto vt=At<const unsigned char*>(weapon,0);
    const auto base=reinterpret_cast<std::uintptr_t>(image),end=base+edf::kImageSize;
    const auto v=reinterpret_cast<std::uintptr_t>(vt);
    if(v<base+8 || v>end-8)return false;
    const auto locator=At<const unsigned char*>(vt-8,0);
    const auto col=reinterpret_cast<std::uintptr_t>(locator);
    if(col<base || col>end-0x10)return false;
    const auto td=At<std::uint32_t>(locator,0xC);
    if(td>=edf::kImageSize-0x50)return false;
    const char* name=reinterpret_cast<const char*>(image+td+0x10);
    return std::strcmp(name,".?AVWeapon_Swing@@")==0 || std::strcmp(name,".?AVWeapon_ImpactHammer@@")==0 ||
           std::strcmp(name,".?AVWeapon_PileBanker@@")==0 || std::strcmp(name,".?AVWeapon_Shield@@")==0;
}

// The weapon `w`'s size along its own axes (fp_view.h FrameBounds, docs/fp-render-re.md §17): its model's skeleton
// instance (model+0xA0, the one whose +0x70 is the Umbra box model+0x110) keeps each bone's box in its bone record
// (flag +0x09, half sizes +0xF0, centre +0x100, the world matrix +0xB0: the records 0x11005E0 reads); measured in
// bone 0's frame (the weapon's root). False: no bone with a box.
bool WeaponBounds(const unsigned char* w,M4* out) noexcept {
    constexpr std::size_t kInst=0xA0,kBoxFlag=0x09,kBoxHalf=0xF0,kBoxCentre=0x100;
    constexpr int kMostBoxes=64;
    const unsigned char* inst=w+kWeaponModel+kInst;
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    const auto n=At<std::int32_t>(inst,kInstCount);
    if(!bones || n<=0 || n>kMostBones)return false;
    fpview::BoneBox boxes[kMostBoxes];
    int count=0;
    for(std::int32_t i=0;i<n && count<kMostBoxes;++i) {
        const unsigned char* rec=bones+static_cast<std::size_t>(i)*kBoneStride;
        if(!rec[kBoxFlag])continue;
        fpview::BoneBox& b=boxes[count++];
        b.world=MatrixAt(rec+kBoneWorld);
        std::memcpy(b.half,rec+kBoxHalf,12);std::memcpy(b.centre,rec+kBoxCentre,12);
    }
    return fpview::FrameBounds(MatrixAt(bones+kBoneWorld),boxes,count,out);
}

// The size of the weapon in `hand` (cached a weapon: WeaponBounds), false: none.
bool HandBounds(int hand,const unsigned char* w,M4* out) noexcept {
    if(!w)return false;
    if(Current().boundsOf[hand]!=w) {
        Current().boundsOf[hand]=w;
        Current().boundsOk[hand]=WeaponBounds(w,&Current().bounds[hand]);
        if(Cfg().debug) {
            float half[3]{},native[3]{};
            fpview::HalfSizes(Current().bounds[hand],half);
            const M4 box=MatrixAt(w+kWeaponModel+kModelBounds);
            fpview::HalfSizes(box,native);
            Log("FP weapon %p size along its own axes %.2f x %.2f x %.2f (ok %d); its Umbra box %.2f x %.2f x %.2f, off-diagonal %.3f",
                static_cast<const void*>(w),half[0],half[1],half[2],Current().boundsOk[hand],native[0],native[1],native[2],
                std::fabs(box.m[1])+std::fabs(box.m[2])+std::fabs(box.m[4])+std::fabs(box.m[6])+std::fabs(box.m[8])+std::fabs(box.m[9]));
        }
    }
    if(Current().boundsOk[hand])*out=Current().bounds[hand];
    return Current().boundsOk[hand];
}

// A bulky or long weapon held further off (measured half sizes right x up x ahead: the Air Raider's limpet gun
// 0.28 x 0.17 x 0.46 filled half the view at the rifle's grip; the Mag Blaster 0.03 x 0.12 x 0.50 did not), a shield
// out to the side (fp_view.h FitGrip). The size is the weapon's along its own axes (WeaponBounds), never the Umbra box
// model+0x110: that one is along the world's axes, so the hold moved with the way the player faced (the user,
// 2026-10-18: centred facing east or west, off to the right with the arms showing facing north or south) and with the
// walk's sway (the twitch). `grip` in the camera's frame (x to the left); `dual`: both hands hold one (their grips are
// already set for the Fencer's big weapons: only a shield is moved).
void Fit(int hand,bool dual,float* grip) noexcept {
    bool flip=false;
    const unsigned char* w=HandWeapon(hand,&flip);
    M4 box;
    if(!HandBounds(hand,w,&box))return;
    float half[3];
    fpview::HalfSizes(box,half);
    fpview::FitGrip(half,dual,flip,hand==kRight ? -1.0f : 1.0f,grip);   // away from the middle, in the camera's x
}

// Each hand's T from the poses as they are now (above: its own weapon's anchor, else the other hand's T, else the aim
// frame), with the hold the camera step chose (Current().grip, .hold, .moved, .nativeRest).
void BuildViews(const unsigned char* h,const unsigned char* cam,const M4* anchor,const bool* anchored) noexcept {
    const Config& c=Cfg();
    const M4 view=MatrixAt(cam+kCamMatrix);
    for(int hand=kRight;hand<=kLeft;++hand) {
        bool flip=false;
        const auto weapon=HandWeapon(hand,&flip);
        const bool native=Current().nativeWeapon[hand]!=nullptr && Current().nativeWeapon[hand]==weapon;
        Current().viewOk[hand]=anchored[hand] && (native
            ? fpview::NativeGripTransform(anchor[hand],MatrixAt(h+kAim),view,Current().grip[hand],c.fpScale,&Current().view[hand],Current().moved[hand],Current().nativeRest[hand])
            : fpview::AnchoredTransform(anchor[hand],view,Current().grip[hand],c.fpScale,&Current().view[hand],Current().moved[hand]));
        Current().axisOk[hand]=Current().viewOk[hand];
        if(Current().axisOk[hand])fpview::Point(Current().view[hand],anchor[hand].m+12,Current().axis[hand]);
    }
    for(int hand=kRight;hand<=kLeft;++hand) {
        const int other=1-hand;
        Current().shown[hand]=true;
        Current().onOther[hand]=false;
        if(Current().viewOk[hand])continue;
        if(Current().viewOk[other]) {
            Current().view[hand]=Current().view[other];Current().viewOk[hand]=true;
            // Keep the native support-hand/reload animation; proximity is not a visibility switch. The hand is held to
            // the other weapon's box along its own axes, placed by its root as drawn (anchor) and moved through its T.
            bool flip=false;
            const auto weapon=HandWeapon(other,&flip);
            M4 box;
            Current().onOther[hand]=HandBounds(other,weapon,&box);
            if(Current().onOther[hand])Current().otherBox[hand]=fpview::Mul(fpview::Mul(box,anchor[other]),Current().view[other]);
            Current().axisOk[hand]=true;
            for(int i=0;i<3;++i)Current().axis[hand][i]=Current().axis[other][i];
            continue;
        }
        Current().viewOk[hand]=fpview::ViewTransform(MatrixAt(h+kAim),view,Current().hold,c.fpScale,&Current().view[hand],Current().moved[kRight]);
    }
    Current().built=view;Current().builtReady=true;
    for(int hand=kRight;hand<=kLeft;++hand){Current().probeAnchor[hand]=anchor[hand];Current().probeAnchored[hand]=anchored[hand];}
}

// The camera step's placement of each hand's anchors in the camera's frame (L = anchor * T * C^-1): where the draw
// must show them whatever the poses have become by then.
void Placements(const M4* anchor,const bool* anchored,const M4& camera) noexcept {
    M4 inv;
    const bool invOk=fpview::InverseAffine(camera,&inv);
    const auto inst=Current().body ? At<const unsigned char*>(Current().body,kBaseLod+kLodInst) : nullptr;
    const auto pose=inst ? At<const float*>(inst,kInstPose) : nullptr;
    for(int hand=kRight;hand<=kLeft;++hand) {
        Current().placeWeaponOk[hand]=invOk && anchored[hand] && Current().viewOk[hand];
        if(Current().placeWeaponOk[hand])Current().placeWeapon[hand]=fpview::Mul(fpview::Mul(anchor[hand],Current().view[hand]),inv);
        // The body's anchor: the bone its hand's weapon hangs on (the other hand's for a free hand on it), else the
        // hand's own mount bone. Natively the weapon's root is that bone's matrix (§8; measured equal at the step).
        bool flip=false;
        const unsigned char* w=anchored[hand] ? HandWeapon(hand,&flip) : Current().onOther[hand] ? HandWeapon(1-hand,&flip) : nullptr;
        const auto mount=w ? At<const unsigned char*>(w,kWeaponMount) : nullptr;
        const wchar_t* name=mount ? At<const wchar_t*>(mount,0) : nullptr;
        if(!name)name=hand==kRight ? L"arms_r" : L"arms_l";
        wcsncpy_s(Current().bodyMount[hand],name,_TRUNCATE);
        const int at=pose ? BoneIndex(inst,Current().bodyMount[hand]) : -1;
        Current().placeBodyOk[hand]=invOk && at>=0 && Current().viewOk[hand];
        if(Current().placeBodyOk[hand])
            Current().placeBody[hand]=fpview::Mul(fpview::Mul(MatrixAt(reinterpret_cast<const unsigned char*>(pose+at*16)),Current().view[hand]),inv);
        // The soldier's own shoulder (the upper arm's head, jowan0) seen from the eye, shrunk with the viewmodel: where
        // a support arm's upper arm points (§17.4: 0.2 m out, 0.17 m behind the eye for the Ranger; the kata clavicle
        // spacing the chain uses is 0.084 m).
        const int head=pose ? BoneIndex(inst,hand==kRight ? L"jowan0_r" : L"jowan0_l") : -1;
        Current().shoulderOk[hand]=invOk && head>=0;
        if(Current().shoulderOk[hand]) {
            fpview::Point(inv,pose+head*16+12,Current().shoulder[hand]);
            for(float& v:Current().shoulder[hand])v*=Cfg().fpScale;
        }
    }
}

// The camera step: which weapons, the hold (grip, fitted; the motion's springs a step), each hand's T.
void Views(const unsigned char* h,const unsigned char* cam) noexcept {
    const Config& c=Cfg();
    M4 anchor[2]={fpview::Identity(),fpview::Identity()};
    bool anchored[2];
    Hands(h,anchor,anchored);
    const bool both=anchored[kRight] && anchored[kLeft];
    const float gx=fpview::CenteredGrip(both ? c.fpDualGripX : c.fpGripX,c.fpCenter),gy=both ? c.fpDualGripY : c.fpGripY,gz=both ? c.fpDualGripZ : c.fpGripZ;
    float grip[2][3]={{-gx,gy,gz},{gx,gy,gz}};   // right is -x
    if(c.fpFit)for(int hand=kRight;hand<=kLeft;++hand)Fit(hand,both,grip[hand]);
    std::memcpy(Current().grip,grip,sizeof(grip));
    Current().hold[0]=-c.fpHoldX;Current().hold[1]=c.fpHoldY;Current().hold[2]=c.fpHoldZ;
    const float pivots[2][3]={{anchored[kRight] ? grip[kRight][0] : kPivot[0],anchored[kRight] ? gy : kPivot[1],anchored[kRight] ? gz : kPivot[2]},
                              {anchored[kLeft] ? grip[kLeft][0] : -kPivot[0],anchored[kLeft] ? gy : kPivot[1],anchored[kLeft] ? gz : kPivot[2]}};
    if(c.fpMotion)Motion(h,pivots);
    else Current().moved[kRight]=Current().moved[kLeft]=fpview::Identity();
    for(int hand=kRight;hand<=kLeft;++hand) {
        bool flip=false;
        const auto weapon=HandWeapon(hand,&flip);
        const bool native=NativeGrip(weapon);
        if(native && anchored[hand] && Current().nativeWeapon[hand]!=weapon) {
            M4 inverse;
            if(fpview::InverseAffine(MatrixAt(h+kAim),&inverse)) {
                fpview::Point(inverse,anchor[hand].m+12,Current().nativeRest[hand]);
                Current().nativeWeapon[hand]=weapon;
            }
        }
        if(!native)Current().nativeWeapon[hand]=nullptr;
    }
    BuildViews(h,cam,anchor,anchored);
    Placements(anchor,anchored,MatrixAt(cam+kCamMatrix));
    if(c.debug){Current().probeCamThread=GetCurrentThreadId();++Current().probeCamSteps;}
    Probe(anchored,anchor,MatrixAt(cam+kCamMatrix));
}

// Debug=1 with the view stock: the weapon in hand's draws counted as well (the same counters), to compare.
std::atomic<const void*> watched{nullptr};   // read by the draws (render thread)
void Watch(const unsigned char* h) noexcept {
    const unsigned char* w=Cfg().debug && h ? SetWeapon(h,0) : nullptr;
    watched.store(w ? w+kWeaponModel : nullptr,std::memory_order_relaxed);
}

// The soldier this camera follows (+0x360, the SoldierBase cast of its target: null for a vehicle) when it is this
// machine's player (measured: PlayerHuman, fed by the board prompt's walk and stale after 2 s, dropped the view out
// of first person mid-mission with no vehicle near).
// In a vehicle (camera step, after the stock one): the eye at the seat's cockpit (fp_vehicle.cpp), the rider not drawn:
// both hands' T the identity and neither arm shown, so its colour index list is empty. The native model draw still
// runs (its secondary physics keep running, fp-render-re.md §3), and its shadow stays.
// The game puts the rider's own weapons away; the near clip and the look smoothing are the stock ones in a vehicle.
// False: no eye (no cockpit in this seat: the stock camera then).
bool Ride(unsigned char* cam,unsigned char* h) noexcept {
    float eye[3];
    RideView ride{};
    if(!VehicleEye(h,eye,&ride))return false;
    // Boarding ends the on-foot viewmodel. Refresh ownership before returning its batches:
    // a weapon may have been removed since the last camera step. Do this before Ride drops
    // the weapon lists, otherwise StillPlayers cannot return even a surviving weapon's batch.
    Gather(h);
    Rebatch(true);
    if(!Current().riding || Current().ride.vehicle!=ride.vehicle || Current().ride.seat!=ride.seat) {
        Current().instrumentsReady=false;
        Current().have=false;Current().reloadLeft=0;Current().motion=fpmotion::State{};
        for(auto& event:Current().shotEvent)event=State::ShotEvent{};
        Log("FP in a vehicle: %s %p seat %d, eye at its cockpit (%.2f %.2f %.2f)",ride.name ? ride.name : "?",
            static_cast<void*>(ride.vehicle),ride.seat,eye[0],eye[1],eye[2]);
    }
    Current().riding=true;Current().ride=ride;Current().rideCtrl=At<const void*>(ride.vehicle,kSelfCtrl);
    HookDrawOf(At<void**>(ride.vehicle+0xE40,0));
    // Most seats follow their physical cockpit frame. Sazabi's decorated neck is not its pilot's view controller:
    // preserve the physical eye while taking rotation from the same heading/pitch that drives its aim.
    SazabiFirstPersonFrame(ride.vehicle,static_cast<unsigned>(ride.seat),ride.frame);
    std::memcpy(cam+kCamMatrix,ride.frame,64);
    VehicleLookCamera(h,reinterpret_cast<float*>(cam+kCamMatrix));
    VehicleScreens(ride.vehicle,ride.seat,eye,&Current().screens);
    std::memcpy(Current().screens.frame,Current().ride.frame,64);
    ReturnNear(cam);
    if(Current().smoothSet){Put<float>(h,kLookSmooth,Current().savedSmooth);Current().smoothSet=false;}
    Current().body=h+kBodyModel;Current().weaponCount=0;Current().inHandCount=0;
    for(int hand=kRight;hand<=kLeft;++hand) {
        Current().view[hand]=fpview::Identity();Current().viewOk[hand]=true;Current().shown[hand]=false;Current().axisOk[hand]=false;Current().onOther[hand]=false;
    }
    return true;
}

unsigned char* CameraPlayer(const unsigned char* cam) noexcept {
    auto h=At<unsigned char*>(cam,kCamTarget);
    return h && IsSoldier(h) && edf::IsPlayer(h) ? h : nullptr;
}

void Camera(unsigned char* cam) noexcept {
    const Config& c=Cfg();
    unsigned char* const h=CameraPlayer(cam);
    Toggle(h);
    if(!Wanted(cam,h)){if(Current().on && Current().cam==cam)Leave();if(!Current().on)Watch(h);return;}
    watched.store(nullptr,std::memory_order_relaxed);
    if(!Current().on || Current().cam!=cam || Current().human!=h || Current().bodyCtrl!=At<const void*>(h,kSelfCtrl)) {
        Leave();
        Current().on=true;Current().cam=cam;Current().human=h;Current().fresh=true;Current().moved[kRight]=Current().moved[kLeft]=fpview::Identity();
        Log("FP view on %p (player %p)",cam,h);
    }
    if(!HumanOnFoot(h)) {
        if(!Ride(cam,h))Leave();
        return;
    }
    if(Current().riding){Current().riding=false;Current().fresh=true;Log("FP on foot again");}
    const unsigned char* head=BoneNamed(h+kBodyInst,L"head");
    const M4 body=MatrixAt(h+kBodyFrame);
    float headAt[3];
    if(head)std::memcpy(headAt,head+kBoneWorld+48,12);
    else {const float up[3]={0.0f,1.7f,0.0f};fpview::EyeWorld(body,up,headAt);}
    fpview::EaseEye(body,headAt,c.fpEyeUp,c.fpEyeAhead,c.fpViewHeightEase,Current().fresh,Current().eyeLocal);
    Current().fresh=false;
    float eye[3];
    fpview::EyeWorld(body,Current().eyeLocal,eye);
    std::memcpy(cam+kCamEyeRow,eye,12);
    Current().stepNearSet=false;
    const bool depthReady=ViewDepthReady();
    if(!depthReady)ReturnNear(cam);
    if(depthReady && c.fpNear>0.0f && !Current().overridden){
        Current().stepNear=At<float>(cam,kCamNear);Current().stepNearSet=true;
        if(!Current().nearSet){Current().savedNear=Current().stepNear;Current().nearSet=true;}
        ViewWriteNear(cam,c.fpNear);Current().writtenNear=c.fpNear;
    }
    if(c.fpAimSnap){if(!Current().smoothSet){Current().savedSmooth=At<float>(h,kLookSmooth);Current().smoothSet=true;}Put<float>(h,kLookSmooth,1.0f);}
    Gather(h);
    Views(h,cam);
    Rebatch(Current().overridden);
    if(!Current().overridden)Unbatch();
    Flashes();
}

// The render about to start: the viewmodel's models placed around the eye for Umbra (above).
void __fastcall RenderHook(void* system,std::uint8_t a,std::uint8_t b) {
    SyncSession();++renderFrame;
    State* before=selected;
    __try { for(auto& state:states)if(state.on && !state.overridden){selected=&state;PlaceAll(false);} } __except(EXCEPTION_EXECUTE_HANDLER){}
    selected=before;
    reinterpret_cast<RenderFn>(image+kRender)(system,a,b);
}

// A model of the viewmodel drawn in the selected view's opaque pass in progress (render thread: counted by that pass's
// own ends, not by the game thread's renders, which run up to three steps ahead of the draws).
void Drew1(const void* model) noexcept {
    DrawOwn& o=Own();
    Drawn1* free=nullptr;
    for(Drawn1& d:o.drawn1) {
        if(d.model==model){d.frame=o.pass1;return;}
        if(!free && d.frame!=o.pass1)free=&d;
    }
    if(free)*free=Drawn1{model,o.pass1};
}

bool Drew1Now(const void* model) noexcept {
    const DrawOwn& o=Own();
    for(const Drawn1& d:o.drawn1)if(d.model==model && d.frame==o.pass1)return true;
    return false;
}

// The opaque pass's end: what of the viewmodel it left out, submitted.
void PassEndSelected(void* system,int pass) {
    __try {
        const DrawSnap& d=Snap();
        if(d.on && pass==1) {
            const void* models[kMostSets+1];
            int n=0;
            if(ModelOwnerIs(d.body,kBodyModel,d.bodyCtrl))models[n++]=d.body;
            for(int i=0;i<d.inHandCount && n<kMostSets+1;++i)
                if(ModelOwnerIs(d.inHand[i],kWeaponModel,d.inHandCtrls[i]))models[n++]=d.inHand[i];
            for(int i=0;i<n;++i)
                if(models[i] && At<const void*>(models[i],kUmbraObject) && !Drew1Now(models[i]))
                    reinterpret_cast<SubmitFn>(image+kSubmit)(system,const_cast<void*>(models[i]),1);
        }
        if(pass==1)++Own().pass1;   // the pass ends: the next starts with none drawn
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    reinterpret_cast<PassEndFn>(image+kPassEnd)(system,pass);
}

void __fastcall PassEndHook(void* system,int pass) {
    State* state=ViewState(static_cast<unsigned char*>(system)+0xE0);
    if(!state){reinterpret_cast<PassEndFn>(image+kPassEnd)(system,pass);return;}
    StateScope scope(state);
    DrawSnap snap;
    if(!TakeSnapshot(&snap)){reinterpret_cast<PassEndFn>(image+kPassEnd)(system,pass);return;}
    SnapScope taken(&snap);PassEndSelected(system,pass);
}

// The camera step's state as the draws are to read it (game thread).
void Publish() noexcept {
    Published& p=published[selected-states];
    const State& c=Current();
    SeqPublish(&p.seq,&p.snap,[&](DrawSnap& d){
        d.on=c.on && !c.overridden;d.riding=c.riding;d.body=c.body;d.bodyCtrl=c.bodyCtrl;d.rideCtrl=c.rideCtrl;
        d.rideModel=c.riding && c.ride.vehicle ? c.ride.vehicle+0xE40 : nullptr;
        std::memcpy(d.weapons,c.weapons,sizeof(d.weapons));std::memcpy(d.weaponCtrls,c.weaponCtrls,sizeof(d.weaponCtrls));d.weaponCount=c.weaponCount;
        std::memcpy(d.inHand,c.inHand,sizeof(d.inHand));std::memcpy(d.inHandCtrls,c.inHandCtrls,sizeof(d.inHandCtrls));
        std::memcpy(d.handOf,c.handOf,sizeof(d.handOf));d.inHandCount=c.inHandCount;
        for(int k=0;k<2;++k) {
            d.viewOk[k]=c.viewOk[k];d.shown[k]=c.shown[k];d.onOther[k]=c.onOther[k];
            d.placeWeaponOk[k]=c.placeWeaponOk[k];d.placeBodyOk[k]=c.placeBodyOk[k];d.shoulderOk[k]=c.shoulderOk[k];
            d.view[k]=c.view[k];d.otherBox[k]=c.otherBox[k];d.placeWeapon[k]=c.placeWeapon[k];d.placeBody[k]=c.placeBody[k];
            d.boundsOk[k]=c.boundsOk[k];d.bounds[k]=c.bounds[k];
            std::memcpy(d.bodyMount[k],c.bodyMount[k],sizeof(d.bodyMount[k]));std::memcpy(d.shoulder[k],c.shoulder[k],12);
        }
        d.reloadLeft=c.reloadLeft;
        d.hasCam=c.cam!=nullptr;d.camera=c.builtReady ? c.built : fpview::Identity();
        d.rideSeat=c.ride.seat;d.instrumentsReady=c.instrumentsReady;d.instruments=c.instruments;
        for(int k=0;k<2;++k){d.probeAnchor[k]=c.probeAnchor[k];d.probeAnchored[k]=c.probeAnchored[k];}
    });
}

void CameraAfterNative(unsigned char* cam) noexcept {
    const M4 native=MatrixAt(cam+kCamMatrix);
    __try {
        const auto human=CameraPlayer(cam);
        const float* wasEye=native.m+12;
        const float wasLook[3]={wasEye[0]+native.m[8]*100.0f,wasEye[1]+native.m[9]*100.0f,wasEye[2]+native.m[10]*100.0f};
        float enemyEye[3],enemyLook[3];M4 controlled;
        if(human && EnemyControlCamera(human,wasEye,wasLook,enemyEye,enemyLook) && fpview::LookFrame(enemyEye,enemyLook,&controlled)) {
            // A controlled enemy owns this source player's view. Retire human viewmodel/near/batches and cancel any
            // interrupted FP-key gesture. The map/TV chain still runs outside this step and has final precedence.
            CancelToggle(human);if(Current().on)Leave();
            std::memcpy(cam+kCamMatrix,controlled.m,64);
        } else {
            Camera(cam);
            if(!Current().on || Current().cam!=cam || !Current().riding)
                VehicleLookCamera(CameraPlayer(cam),reinterpret_cast<float*>(cam+kCamMatrix));
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER){CancelToggle(nullptr);const auto key=Current().key;Current()=State{};Current().cam=cam;Current().key=key;ClearViews(static_cast<int>(selected-states));}
    // One transaction for the final local view, irrespective of its owner. Rotation and eye are separate fields;
    // another layer moving only the eye must not leave our old rotation as next frame's native starting frame.
    Current().cameraApplied={cam,fpview::CameraWriteOf(native,MatrixAt(cam+kCamMatrix))};
    Publish();
}

void CamStepSelected(void* camera,void* step) {
    auto cam=static_cast<unsigned char*>(camera);
    if(Current().cameraApplied.cam==cam) {
        M4 current=MatrixAt(cam+kCamMatrix);
        fpview::ReturnCameraWrite(&current,Current().cameraApplied.edit);
        std::memcpy(cam+kCamMatrix,current.m,64);
        Current().cameraApplied=CameraApplied{};
    }
    nextCamStep(camera,step);
    CameraAfterNative(cam);
}

void SeeLocal(const State* state,unsigned char* cam) noexcept {
    if(state)localSeen[state-states]=LocalSeen{CameraPlayer(cam),GetTickCount64()};
}
void __fastcall CamStepHook(void* camera,void* step) {
    SyncSession();
    auto cam=static_cast<unsigned char*>(camera);
    if(auto state=CameraState(camera)){StateScope scope(state);CamStepSelected(camera,step);SeeLocal(state,cam);return;}
    nextCamStep(camera,step);
    if(!CameraPlayer(cam))return; // Menu and remote cameras must not consume local player slots.
    if(auto state=AcquireCamera(camera)){StateScope scope(state);CameraAfterNative(cam);SeeLocal(state,cam);}
}

// --- The draw ---
DrawFn NextDraw(const void* model) noexcept {
    const void* vt=At<const void*>(model,0);
    for(int i=0;i<hookedCount;++i)if(hooked[i].vtable==vt)return hooked[i].next;
    return reinterpret_cast<DrawFn>(image+kModelDraw);
}

// What a drawn model is to the first person: not the player's, their body, a weapon in a hand, one put away.
enum class Part { none,body,inHand,stowed,instruments };
Part PartOf(const void* model,int* hand) noexcept {
    const DrawSnap& d=Snap();
    *hand=kRight;
    if(!d.on)return Part::none;
    if(d.riding && model==d.rideModel && ModelOwnerIs(model,0xE40,d.rideCtrl))return Part::instruments;
    if(model==d.body && ModelOwnerIs(model,kBodyModel,d.bodyCtrl))return Part::body;
    if((*hand=SnapHand(d,model))>=0)return Part::inHand;
    *hand=kRight;
    for(int i=0;i<d.weaponCount && i<kMostWeapons;++i)if(d.weapons[i]==model && ModelOwnerIs(model,kWeaponModel,d.weaponCtrls[i]))return Part::stowed;
    return Part::none;
}

// The skeleton instances a model draws with (its LODs'), each once.
int InstancesOf(unsigned char* model,unsigned char** out) noexcept {
    int n=0;
    auto add=[&](unsigned char* inst){
        if(!inst || n>=kMostInsts)return;
        for(int i=0;i<n;++i)if(out[i]==inst)return;
        out[n++]=inst;
    };
    if(Readable(model+0xA0,kInstCount+4) && At<std::int32_t>(model+0xA0,kInstCount)>0)add(model+0xA0);
    add(At<unsigned char*>(model,kBaseLod+kLodInst));
    const auto lods=At<unsigned char*>(model,kLods);
    const auto count=At<std::uint64_t>(model,kLodCount);
    if(lods && count<=8)for(std::uint64_t i=0;i<count;++i)add(At<unsigned char*>(lods+i*kLodStride,kLodInst));
    return n;
}

// --- The arms posed for the first person (fp_view.h RollAngle / TurnAbout / ReachShoulder / SegmentMap) ---
// The place the upper arm turns toward, in the eye's frame (right, up, ahead; times the viewmodel's scale): down and a
// little behind, out by half the soldier's own shoulder width (kata_l to kata_r). The way the elbow is rolled toward
// (seen along the view): out, and more down.
constexpr float kShoulderDown=-0.24f,kShoulderBack=-0.06f,kElbowOut=1.0f,kElbowDown=-1.5f;
constexpr float kSupportWristMost=0.30f;   // rad (17 deg): the support hand's wrist bent past the animation at most
constexpr float kSleeve=0.07f;   // m: the upper arm's sleeve radius (Ranger / Air Raider, rounded up)
struct ArmFit { bool ok; M4 upper,fore,hand; };   // after T: the upper arm's move, the forearm's, the hand's and fingers'

int BoneIndex(const unsigned char* inst,const wchar_t* name) noexcept {
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    const auto n=At<std::int32_t>(inst,kInstCount);
    for(std::int32_t i=0;bones && i<n && i<kMostBones;++i) {
        const auto nm=At<const wchar_t*>(bones+static_cast<std::size_t>(i)*kBoneStride,0);
        if(nm && std::wcscmp(nm,name)==0)return i;
    }
    return -1;
}

// One arm's moves (`hand` kRight / kLeft) from the instance's draw pose (`pose`, before T) and the hand's T.
ArmFit FitArm(const unsigned char* inst,const float* pose,const M4& t,int hand,const M4& camera,const fpview::Frustum& frustum,
              const M4* otherBox) noexcept {
    ArmFit fit{};
    const DrawSnap& snap=Snap();
    const wchar_t* const names[2][4]={{L"jowan0_r",L"kawan0_r",L"te_r",L"kata_r"},{L"jowan0_l",L"kawan0_l",L"te_l",L"kata_l"}};
    int at[4];
    for(int k=0;k<4;++k)if((at[k]=BoneIndex(inst,names[hand][k]))<0)return fit;
    const int otherKata=BoneIndex(inst,names[1-hand][3]);
    if(otherKata<0)return fit;
    float s0[3],e0[3],h0[3];
    fpview::Point(t,pose+at[0]*16+12,s0);fpview::Point(t,pose+at[1]*16+12,e0);fpview::Point(t,pose+at[2]*16+12,h0);
    float d[3];
    fpview::Sub(pose+at[3]*16+12,pose+otherKata*16+12,d);
    const float half=0.5f*fpview::Length(d),scale=Cfg().fpScale,out=hand==kRight ? 1.0f : -1.0f;
    const float* cam=camera.m;   // the camera this draw is made with
    float shoulder[3],pole[3];
    for(int i=0;i<3;++i) {
        const float right=-cam[i],up=cam[4+i],ahead=cam[8+i];
        shoulder[i]=cam[12+i]+scale*(out*half*right+kShoulderDown*up+kShoulderBack*ahead);
        pole[i]=out*kElbowOut*right+kElbowDown*up;
    }
    // The elbow bends the way the animation bends it (docs/fp-render-re.md §17): toward the native elbow as seen from
    // the wrist. A pole fixed in the camera (out and down) re-aimed the forearm while the hand kept its turn on the
    // gun: measured 48-80 degrees of added wrist bend (the user's support hand at a right angle to the forearm).
    if(Cfg().fpNativeElbow)for(int i=0;i<3;++i)pole[i]=e0[i]-h0[i];
    float wrist[3];std::memcpy(wrist,h0,12);
    if(snap.onOther[hand] && snap.reloadLeft==0) {
        float move[3];
        if(otherBox && fpview::SnapToBox(otherBox[hand],h0,0.035f*scale,move))
            for(int i=0;i<3;++i)wrist[i]+=move[i];
    }
    fit.hand=fpview::Translate(wrist[0]-h0[0],wrist[1]-h0[1],wrist[2]-h0[2]);
    const bool baseLod=Cfg().debug && snap.body && inst==At<const unsigned char*>(snap.body,kBaseLod+kLodInst);
    if(baseLod && snap.probeAnchored[hand]) {
        // The draw-time probe: the body's mount bone now against the weapon's root T was built from.
        const int mount=BoneIndex(inst,hand==kRight ? L"arms_r" : L"arms_l");
        if(mount>=0) {
            float gap[3];fpview::Sub(pose+mount*16+12,snap.probeAnchor[hand].m+12,gap);
            Own().drawMount=std::fmax(Own().drawMount,fpview::Length(gap));
        }
    }
    if(baseLod) {   // the steadiness probe: the wrist in the camera's frame (the base LOD's)
        DrawOwn& o=Own();
        M4 inverse;float local[3];
        if(fpview::InverseAffine(camera,&inverse)) {
            fpview::Point(inverse,wrist,local);
            if(o.wristHave[hand]) {
                float dd[3];fpview::Sub(local,o.wrist[hand],dd);
                o.wristStep[hand]=std::fmax(o.wristStep[hand],fpview::Length(dd));
            }
            std::memcpy(o.wrist[hand],local,12);o.wristHave[hand]=true;
        }
    }
    // The support hand (on the other hand's weapon): its forearm goes with the hand unturned, the upper arm toward the
    // soldier's own shoulder and, if that would show its open end, down out of the view (fp_view.h SupportArm, 17.4).
    // The hand holding its own weapon keeps the two-bone chain (within reach: its wrist bend measured 10-25 deg).
    fit.ok=false;
    if(snap.onOther[hand] && snap.shoulderOk[hand] && Cfg().fpNativeElbow) {
        float want[3];fpview::Point(camera,snap.shoulder[hand],want);
        fit.ok=fpview::SupportArm(s0,e0,h0,wrist,want,frustum,kSleeve*scale,kSupportWristMost,&fit.upper,&fit.fore);
        if(Cfg().debug){if(fit.ok)++Own().support[hand];else ++Own().supportFallback[hand];}
    }
    if(!fit.ok)fit.ok=fpview::ArmChainTo(s0,e0,h0,wrist,shoulder,pole,&fit.upper,&fit.fore);
    if(baseLod && fit.ok) {
        // The probe: how far the forearm was turned against the hand (the hand keeps its own turn on the gun), deg.
        float e1[3],before[3],after[3];
        fpview::Point(fit.upper,e0,e1);fpview::Sub(h0,e0,before);fpview::Sub(wrist,e1,after);
        if(fpview::Unit(before) && fpview::Unit(after))
            Own().bend[hand]=std::fmax(Own().bend[hand],std::acos(fpview::Clamp(fpview::Dot3(before,after),-1.0f,1.0f))*57.29578f);
    }
    return fit;
}

// A restore record exists only after a complete original-pose copy. Keep the exact native buffer identity:
// skipped LODs have no backup, and a native reallocation during draw must never receive an older buffer's pose.
struct PoseRestore {
    unsigned char* instance=nullptr;float* pose=nullptr;const unsigned char* bones=nullptr;
    int count=0;unsigned reserved=0;
};
// Arm/weapon poses through T, restored after the draw. Non-arm body bones stay native: fp_skin_gpu submits only
// triangles owned by one visible arm, excluding mixed torso/shoulder and opposite-arm faces instead of stretching
// their skinning weights. `t` is each hand's transform (a weapon uses its own hand's twice).
void Pose(unsigned char* inst,const M4* t,bool body,const M4& camera,const fpview::Frustum& frustum,const M4* otherBox,float* backup,PoseRestore* captured=nullptr) noexcept {
    const DrawSnap& d=Snap();
    const auto n=At<std::int32_t>(inst,kInstCount);
    auto pose=At<float*>(inst,kInstPose);
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    if(!pose || !bones || n<=0 || n>kMostBones)return;
    std::memcpy(backup,pose,static_cast<std::size_t>(n)*64);
    if(captured)*captured=PoseRestore{inst,pose,bones,n,0};
    ArmFit fit[2]{};
    if(body && Cfg().fpArmIk && !d.riding)for(int hand=kRight;hand<=kLeft;++hand)fit[hand]=FitArm(inst,backup,t[hand],hand,camera,frustum,otherBox);
    for(std::int32_t i=0;i<n;++i) {
        float* p=pose+i*16;
        const wchar_t* name=At<const wchar_t*>(bones+static_cast<std::size_t>(i)*kBoneStride,0);
        const std::size_t len=name ? std::wcslen(name) : 0;
        const bool left=body && len>=2 && name[len-2]==L'_' && name[len-1]==L'l';
        if(body && (!ArmBone(name) || !d.shown[left ? kLeft : kRight])) {
            // The colour draw uses an arm-only index buffer. Leave body bones intact instead of collapsing mixed
            // shoulder triangles toward the camera (which made long sheets during the sprint).
            continue;
        }
        const int hand=left ? kLeft : kRight;
        M4 moved=fpview::Mul(MatrixAt(reinterpret_cast<const unsigned char*>(p)),t[hand]);
        if(body && fit[hand].ok && name)   // the arm posed for the first person: the upper arm, the forearm, the hand
            moved=fpview::Mul(moved,std::wcsncmp(name,L"jowan",5)==0 ? fit[hand].upper
                                    : std::wcsncmp(name,L"kawan",5)==0 ? fit[hand].fore : fit[hand].hand);
        std::memcpy(p,moved.m,64);
    }
}

void Unpose(unsigned char* inst,const float* backup,const PoseRestore* captured=nullptr) noexcept {
    if(captured && (!captured->pose || captured->instance!=inst))return;
    const auto n=At<std::int32_t>(inst,kInstCount);
    auto pose=At<float*>(inst,kInstPose);
    if(captured && (pose!=captured->pose || n!=captured->count || At<const unsigned char*>(inst,kInstBones)!=captured->bones))return;
    if(pose && n>0 && n<=kMostBones)std::memcpy(pose,backup,static_cast<std::size_t>(n)*64);
}
void RestoreCapturedPoses(unsigned char* const* insts,const float (*backup)[kMostBones*16],const PoseRestore* captured,int count) noexcept {
    for(int i=0;i<count;++i)if(captured[i].pose) {
        __try { Unpose(insts[i],backup[i],&captured[i]); }
        __except(EXCEPTION_EXECUTE_HANDLER){Log("FP pose restore skipped: native instance %p no longer readable",insts[i]);}
    }
}

fpskin::Mask BodyMask(const void* palette) noexcept {
    fpskin::Mask mask{};
    const DrawSnap& d=Snap();
    if(!palette || !ModelOwnerIs(d.body,kBodyModel,d.bodyCtrl))return mask;
    unsigned char* insts[kMostInsts];
    const int instances=InstancesOf(const_cast<unsigned char*>(static_cast<const unsigned char*>(d.body)),insts);
    for(int k=0;k<instances;++k) {
        const auto buffer=At<const unsigned char*>(insts[k],0x68);
        if(!buffer || At<const void*>(buffer,0)!=palette)continue;
        const auto bones=At<const unsigned char*>(insts[k],kInstBones);
        const auto count=At<std::int32_t>(insts[k],kInstCount);
        if(!bones || count<=0 || count>kMostBones)return mask;
        mask.count=static_cast<unsigned>(count);
        for(int i=0;i<count;++i) {
            const auto name=At<const wchar_t*>(bones+static_cast<std::size_t>(i)*kBoneStride,0);
            const auto len=name ? std::wcslen(name) : 0;
            const bool left=len>=2 && name[len-2]==L'_' && name[len-1]==L'l';
            mask.arm[i]=ArmBone(name) && d.shown[left ? kLeft : kRight];
            mask.side[i]=left ? 2 : 1;
        }
        return mask;
    }
    return mask;
}

// Debug=1: an attitude card's readings and its pose as drawn, every 2 s (render thread): the bank / pitch it was given,
// the drawn card's turn and drop against the model's own, and the bone's row lengths (its screen-height units).
std::atomic<ULONGLONG> attitudeLogAt{0};
void AttitudeProbe(const wchar_t* name,const M4& original,const M4& moved,const DrawSnap& d,bool ready) noexcept {
    const ULONGLONG now=GetTickCount64();
    ULONGLONG last=attitudeLogAt.load(std::memory_order_relaxed);
    if(now-last<2000 || !attitudeLogAt.compare_exchange_strong(last,now))return;
    auto len=[](const float* r){return std::sqrt(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]);};
    auto dot=[](const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    const float sx=len(original.m),sy=len(original.m+4);
    // The card's turn about its own facing: its drawn x row against the model's x and y rows.
    const float turned=sx>0 && sy>0 ? std::atan2(dot(moved.m,original.m+4)/(sx*sy),dot(moved.m,original.m)/(sx*sx))/0.01745329252f : 0.0f;
    float shift[3];fpview::Sub(moved.m+12,original.m+12,shift);
    const float drop=sy>0 ? dot(shift,original.m+4)/(sy*sy) : 0.0f;
    const auto& r=d.instruments;
    Log("FP attitude %ls: ready %d flight %d pitch %.1f roll %.1f deg -> card turned %.1f deg, dropped %.4f (rows %.4f %.4f %.4f)",
        name,ready,r.flight,r.value[instruments::pitch],r.value[instruments::roll],turned,drop,sx,sy,len(original.m+8));
}

void InstrumentPose(unsigned char* inst,float* backup,PoseRestore* captured=nullptr) noexcept {
    const DrawSnap& d=Snap();
    const auto n=At<std::int32_t>(inst,kInstCount);
    auto pose=At<float*>(inst,kInstPose);
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    if(!pose || !bones || n<=0 || n>kMostBones)return;
    std::memcpy(backup,pose,static_cast<std::size_t>(n)*64);
    if(captured)*captured=PoseRestore{inst,pose,bones,n,0};
    for(int i=0;i<n;++i) {
        const auto name=At<const wchar_t*>(bones+static_cast<std::size_t>(i)*kBoneStride,0);
        if(name && std::wcsncmp(name,L"vc_shell_",9)==0) {
            unsigned mask=0,parent=0;
            if(swscanf_s(name,L"vc_shell_%u_%u",&mask,&parent)==2) {
                const M4 original=MatrixAt(reinterpret_cast<const unsigned char*>(pose+i*16));
                const M4 moved=instruments::ShellPose(original,mask,d.rideSeat);
                std::memcpy(pose+i*16,moved.m,64);
            }
            continue;
        }
        if(!name || std::wcsncmp(name,L"vc_i_",5)!=0)continue;
        int seat=-1,screen=-1,field=-1,place=0,segment=0;wchar_t mode=0;
        const int fields=swscanf_s(name,L"vc_i_%d_%d_%lc_%d_%d_%d",&seat,&screen,&mode,1u,&field,&place,&segment);
        if(fields<4 || ((mode==L'd' || mode==L'D' || mode==L'Z' || mode==L'p') && fields!=6))continue;
        const instruments::SeatPose how=instruments::SeatPoseOf(seat==d.rideSeat,mode,field,place,fields);
        if(how==instruments::SeatPose::skip)continue;
        const bool ready=d.instrumentsReady && how==instruments::SeatPose::live;
        const M4 original=MatrixAt(reinterpret_cast<const unsigned char*>(pose+i*16));
        const M4 moved=instruments::Pose(original,mode,field,place,segment,d.instruments,ready);
        std::memcpy(pose+i*16,moved.m,64);
        if(mode==L'g' && Cfg().debug)AttitudeProbe(name,original,moved,d,ready);
    }
}

void LogPass(Part part,int pass) noexcept {
    if(!Cfg().debug || pass<0 || pass>5 || part==Part::stowed || part==Part::instruments)return;
    ++passLog[part==Part::body ? 0 : 1][pass];
    const ULONGLONG now=GetTickCount64();
    if(now-passLogAt<5000)return;
    if(passLogAt)Log("FP draws in 5 s by pass 0/1/2/3/5: body %d/%d/%d/%d/%d, weapons in hand %d/%d/%d/%d/%d (weapons %d)",
                     passLog[0][0],passLog[0][1],passLog[0][2],passLog[0][3],passLog[0][5],
                     passLog[1][0],passLog[1][1],passLog[1][2],passLog[1][3],passLog[1][5],Snap().weaponCount);
    std::memset(passLog,0,sizeof(passLog));passLogAt=now;
}

// Debug=1: a model not the player's drawn (colour pass) where the weapon in the right hand really is: what it is.
void NearWeapon(const void* model,int pass) noexcept {
    static const void* logged[8]{};
    static int next=0;
    const DrawSnap& snap=Snap();
    if(!Cfg().debug || pass!=1 || snap.inHandCount<=0 || !snap.inHand[0] || SnapHand(snap,snap.inHand[0])<0)return;
    M4 root,gun;
    if(!RootPose(static_cast<const unsigned char*>(model),&root) || !RootPose(static_cast<const unsigned char*>(snap.inHand[0]),&gun))return;
    const float d[3]={root.m[12]-gun.m[12],root.m[13]-gun.m[13],root.m[14]-gun.m[14]};
    if(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>0.3f*0.3f)return;
    for(const void* l:logged)if(l==model)return;
    logged[next]=model;next=(next+1)%8;
    const auto weapon=static_cast<const unsigned char*>(snap.inHand[0])-kWeaponModel;
    const long long off=static_cast<const unsigned char*>(model)-weapon;
    Log("FP a model %p drawn at the weapon %p's place: vtable +0x%llX, weapon%+lld",model,static_cast<const void*>(weapon),
        static_cast<unsigned long long>(At<const unsigned char*>(model,0)-image),off);
}

// The camera a draw is made with: its native view (view+0x20: the camera's inverse, X and Z negated, §2026-10-17)
// undone; without one (offline) the camera matrix the published viewmodel was built on.
M4 DrawCamera(const void* view) noexcept {
    if(view && Readable(view,0x60)) {
        M4 native;std::memcpy(native.m,static_cast<const unsigned char*>(view)+0x20,64);
        for(int row=0;row<4;++row){native.m[row*4]*=-1;native.m[row*4+2]*=-1;}
        M4 camera;
        if(fpview::InverseAffine(native,&camera))return camera;
    }
    return Snap().camera;
}

// The view a draw is made with as a frustum: its camera's axes (the first row points to the screen's left), the
// projection's scales (view+0x60, row-major: [0] x, [5] y) and the near clip this file sets. Offline: 90 degrees wide.
fpview::Frustum DrawFrustum(const void* view,const M4& camera) noexcept {
    fpview::Frustum f{};
    for(int i=0;i<3;++i){f.right[i]=-camera.m[i];f.up[i]=camera.m[4+i];f.ahead[i]=camera.m[8+i];f.eye[i]=camera.m[12+i];}
    f.xs=1.0f;f.ys=16.0f/9.0f;
    if(view && Readable(view,0xA0)) {
        const float* proj=reinterpret_cast<const float*>(static_cast<const unsigned char*>(view)+0x60);
        if(std::isfinite(proj[0]) && std::isfinite(proj[5]) && std::fabs(proj[0])>0.05f && std::fabs(proj[5])>0.05f)
            {f.xs=std::fabs(proj[0]);f.ys=std::fabs(proj[5]);}
    }
    f.nearClip=Cfg().fpNear>0.0f ? Cfg().fpNear : 0.1f;
    return f;
}

// The T of each hand for drawing the skeleton instance `inst` of a part (docs/fp-render-re.md §17): its anchor as this
// draw's pose array has it (a weapon's root; the body's mount bone) put where the camera step placed it in the camera's
// frame, seen from this draw's camera: T = anchor_now^-1 * L * C_draw. So the hold neither follows the update steps the
// game took after the camera step (the walk's and the jump's twitch) nor a camera the draw does not use. False: no
// placement (the camera step's T is used).
bool DrawTransforms(const unsigned char* inst,Part part,int hand,const M4& camera,M4* t) noexcept {
    const DrawSnap& d=Snap();
    const auto pose=At<const float*>(inst,kInstPose);
    if(!pose)return false;
    if(part==Part::inHand) {
        M4 inv;
        if(!d.placeWeaponOk[hand] || !fpview::InverseAffine(MatrixAt(reinterpret_cast<const unsigned char*>(pose)),&inv))return false;
        t[0]=t[1]=fpview::Mul(fpview::Mul(inv,d.placeWeapon[hand]),camera);
        return fpview::Finite(t[0]);
    }
    for(int k=kRight;k<=kLeft;++k) {
        const int at=d.placeBodyOk[k] ? BoneIndex(inst,d.bodyMount[k]) : -1;
        M4 inv;
        if(at<0 || !fpview::InverseAffine(MatrixAt(reinterpret_cast<const unsigned char*>(pose+at*16)),&inv))return false;
        t[k]=fpview::Mul(fpview::Mul(inv,d.placeBody[k]),camera);
        if(!fpview::Finite(t[k]))return false;
    }
    return true;
}

// Debug=1: the draws' half of the probe (DrawOwn), logged by the render thread itself every 5 s (half a second through
// a reload), as Probe logs the camera step's.
void DrawProbe() noexcept {
    DrawOwn& o=Own();
    const ULONGLONG now=GetTickCount64();
    if(!o.loggedAt){o.loggedAt=now;return;}
    if(now-o.loggedAt<(Snap().reloadLeft>0 ? 500u : 5000u))return;
    o.loggedAt=now;
    for(int hand=kRight;hand<=kLeft;++hand) {
        Log("FP probe %s draw: wrist in the camera (%.3f,%.3f,%.3f), most moved %.4f | the forearm turned against the hand by %.1f deg at most; support arm %u draws, chain fallback %u",
            hand==kRight ? "right" : "left",o.wrist[hand][0],o.wrist[hand][1],o.wrist[hand][2],o.wristStep[hand],
            o.bend[hand],o.support[hand],o.supportFallback[hand]);
        o.wristStep[hand]=o.bend[hand]=0.0f;o.support[hand]=o.supportFallback[hand]=0;
    }
    Log("FP probe draws: %u weapon draws (thread %u), the drawn root %.4f m at most from the camera step's; the body's mount bone %.4f m",
        o.draws,o.drawThread,o.drawRoot,o.drawMount);
    o.draws=0;o.drawRoot=o.drawMount=0.0f;
}

// Only an owned viewmodel enters this stack frame. Native/foreign models keep the small fast path, while each
// posed invocation (including nested calls) owns its independent backup for the duration of the native draw.
__declspec(noinline) void DrawOwnedModel(void* model,void* ctx,int pass,void* view,Part part,int hand,DrawFn next) {
    if(pass==1 && part!=Part::instruments)Drew1(model);
    if(Cfg().debug && pass==1) {
        // The draw-time probe: has the weapon's root moved since the camera step built T from it (the drawn weapon
        // then moves in the view), and does the body's mount bone still meet it (else the hand leaves the grip)?
        __try {
            if(part==Part::inHand && Snap().probeAnchored[hand]) {
                M4 root;
                if(RootPose(static_cast<const unsigned char*>(model),&root)) {
                    DrawOwn& o=Own();
                    float d[3];fpview::Sub(root.m+12,Snap().probeAnchor[hand].m+12,d);
                    o.drawRoot=std::fmax(o.drawRoot,fpview::Length(d));++o.draws;
                    o.drawThread=GetCurrentThreadId();
                }
            }
            DrawProbe();
        } __except(EXCEPTION_EXECUTE_HANDLER){}
    }
    unsigned char* insts[kMostInsts];
    // A draw may re-enter another owned model, and separate render workers must never share a backup. The native
    // pose belongs to this invocation until it is returned; a global/thread-global scratch array cannot own it.
    alignas(16) float saved[kMostInsts][kMostBones*16];
    PoseRestore captured[kMostInsts]{};
    int n=0;
    __try {
        const DrawSnap& d=Snap();
        if(part==Part::instruments || (d.viewOk[kRight] && d.viewOk[kLeft])) {
            const M4 stepT[2]={part==Part::body ? d.view[kRight] : d.view[hand],part==Part::body ? d.view[kLeft] : d.view[hand]};
            const M4 camera=part==Part::instruments ? M4{} : DrawCamera(view);
            const fpview::Frustum frustum=DrawFrustum(view,camera);
            // A free hand's weapon box, placed as the draw shows that weapon (its box along its own axes, at the
            // other hand's placement, from this draw's camera).
            M4 otherBox[2]{};
            for(int k=kRight;k<=kLeft;++k) {
                if(part==Part::body && d.onOther[k] && d.placeWeaponOk[1-k] && d.boundsOk[1-k])
                    otherBox[k]=fpview::Mul(fpview::Mul(d.bounds[1-k],d.placeWeapon[1-k]),camera);
                else otherBox[k]=d.otherBox[k];
            }
            n=InstancesOf(static_cast<unsigned char*>(model),insts);
            for(int i=0;i<n;++i) {
                if(part==Part::instruments){InstrumentPose(insts[i],saved[i],&captured[i]);continue;}
                M4 t[2];
                const bool drawn=!d.riding && DrawTransforms(insts[i],part,hand,camera,t);
                if(!drawn){t[0]=stepT[0];t[1]=stepT[1];}
                Pose(insts[i],t,part==Part::body,camera,frustum,otherBox,saved[i],&captured[i]);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){RestoreCapturedPoses(insts,saved,captured,n);n=0;}
    if(part==Part::body)fpskin::Begin(fpskin::Mask{}); // the mesh draw selects its actual LOD palette
    next(model,ctx,pass,view);
    if(part==Part::body)fpskin::End();
    RestoreCapturedPoses(insts,saved,captured,n);
}

void DrawSelected(void* model,void* ctx,int pass,void* view) {
    const DrawFn next=NextDraw(model);
    Part part=Part::none;
    int hand=kRight;
    __try { part=PartOf(model,&hand); } __except(EXCEPTION_EXECUTE_HANDLER){part=Part::none;}
    const bool on=Snap().on;
    if(part!=Part::none)LogPass(part,pass);
    else if(!on && model==watched.load(std::memory_order_relaxed))LogPass(Part::inHand,pass);
    else if(on){__try { NearWeapon(model,pass); } __except(EXCEPTION_EXECUTE_HANDLER){}}
    if(part==Part::none || pass==3 || pass==4 || pass>5){next(model,ctx,pass,view);return;}
    if((pass==0 && part!=Part::instruments) || part==Part::stowed)return;
    DrawOwnedModel(model,ctx,pass,view,part,hand,next);
}

void __fastcall DrawHook(void* model,void* ctx,int pass,void* view) {
    fpskin::ModelScope maskScope;
    if(pass==3 || pass==4 || pass>5){NextDraw(model)(model,ctx,pass,view);return;}
    State* state=ViewState(view);
    if(!state){NextDraw(model)(model,ctx,pass,view);return;}
    StateScope scope(state);
    DrawSnap snap;
    if(!TakeSnapshot(&snap)){NextDraw(model)(model,ctx,pass,view);return;}
    SnapScope taken(&snap);DrawSelected(model,ctx,pass,view);
}

using MeshFn=void(__fastcall*)(void*,void*,int,std::uint8_t,void*);
void __fastcall MeshHook(void* mesh,void* ctx,int pass,std::uint8_t flag,void* cache) {
    const bool skin=fpskin::Active();
    if(!skin && !thermal::WantsCapture()){
        reinterpret_cast<MeshFn>(image+kMeshDraw)(mesh,ctx,pass,flag,cache);
        thermal::ObserveSurface(At<ID3D11DeviceContext*>(ctx,8),pass);
        return;
    }
    // Repeat the supported native helper's prepare/bind/flush sequence, replacing only its DrawIndexed submission.
    // No COM vtable hooks: Steam's graphics wrapper may replace those while a frame is being built.
    using PrepareFn=std::int64_t(__fastcall*)(void*,void*,int,int,std::uint8_t);
    using PassFn=void(__fastcall*)(void*,void*,std::int64_t,std::uint64_t);
    using FlushFn=void(__fastcall*)(void*,ID3D11DeviceContext*);
    const auto technique=reinterpret_cast<PrepareFn>(image+kMeshPrepare)(mesh,ctx,pass,0,flag);
    if(technique<0)return;
    const auto descriptor=At<const unsigned char*>(mesh,0);
    void* const material=At<void*>(mesh,8);
    void* const holder=At<void*>(material,0);
    const auto program=At<const unsigned char*>(holder,0);
    const auto table=At<const unsigned char*>(program,0x58);
    const auto passes=At<std::uint64_t>(table,static_cast<std::size_t>(technique)*0x20+0x18);
    const auto count=At<unsigned>(descriptor,0x18);
    auto context=At<ID3D11DeviceContext*>(ctx,8);
    for(std::uint64_t i=0;i<passes;++i) {
        reinterpret_cast<PassFn>(image+kMeshPass)(ctx,holder,technique,i);
        if(At<unsigned>(ctx,0x78))reinterpret_cast<FlushFn>(image+kFlushDrawState)(static_cast<unsigned char*>(ctx)+0x20,context);
        if(skin) {
            ID3D11Buffer* palette=nullptr;context->VSGetConstantBuffers(1,1,&palette);
            fpskin::Mask mask{};
            __try { mask=BodyMask(palette); } __except(EXCEPTION_EXECUTE_HANDLER){}
            if(palette)palette->Release();
            fpskin::Begin(mask);
            fpskin::Submit(context,count,0,0);
            thermal::CaptureIndexed(context,count,0,0);
        } else {
            // Test the same native depth/stencil state before the opaque draw writes depth. Sampling afterward
            // with EQUAL would heat a coplanar fragment which native LESS rejected. Capture restores GPU state.
            thermal::CaptureIndexed(context,count,0,0);
            context->DrawIndexed(count,0,0);
        }
    }
    if(cache) {
        Put<void*>(cache,0,passes==1 ? material : nullptr);
        Put<std::int64_t>(cache,8,passes==1 ? technique : -1);
    }
}

void* nextLaserDraw=nullptr;
// The laser sight's start as the game builds it (WeaponAttachment_LaserSight slot 2, 0x6890A0, H): the weapon's first
// muzzle entry ([weapon+0x1D0], count +0x1E0, 0xF0 each; fp-soldier-re.md §2.1) at its +0x90, the local matrix (+0x10)
// times its bone's world record (+0 -> +0xB0, 0x6969A0), less 0.02 (0x17AA990) of that matrix's up row; slot 3
// (0x688970) copies it to the draw's +0x120. Both run in the update steps, not at this draw: the bone record and the
// muzzle entry are written by the weapon's own update (slot 5 -> 0x6934F0 -> 0x6969A0) on its own schedule, and the
// draws run up to three steps behind (§17). So the start the draw finds belongs to some step, the weapon's drawn pose to
// another: moving or jumping, the line left the gun (the user, 2026-10-19: 「跳或者移动的时候红外瞄准的线没有实时跟随」).
// The start is rebuilt here from the very pose array the weapon is drawn with: local x pose[bone], then for the first
// person through that draw's own T (DrawTransforms), the one the viewmodel is drawn with.
constexpr std::size_t kLaserStart=0x120;
constexpr float kLaserBelow=0.02f;
// The muzzle's matrix in the weapon's draw pose and the skeleton instance holding its bone; false: not readable as one.
bool LaserMuzzle(const unsigned char* weapon,unsigned char** inst,M4* out) noexcept {
    if(!Readable(weapon,kWeaponModel+kBaseLod+kLodInst+8))return false;
    const unsigned char* const muzzle=edf::MuzzleAt(weapon,0);
    if(!muzzle)return false;
    const auto bone=At<const unsigned char*>(muzzle,0);
    unsigned char* insts[kMostInsts];
    const int n=InstancesOf(const_cast<unsigned char*>(weapon)+kWeaponModel,insts);
    for(int i=0;i<n;++i) {
        if(!Readable(insts[i],kInstPose+8))continue;
        const auto bones=At<const unsigned char*>(insts[i],kInstBones);
        const auto bonesCount=At<std::int32_t>(insts[i],kInstCount);
        const auto pose=At<const unsigned char*>(insts[i],kInstPose);
        if(!bones || !pose || bonesCount<=0 || bonesCount>kMostBones || bone<bones)continue;
        const auto off=static_cast<std::size_t>(bone-bones);
        if(off%kBoneStride || off/kBoneStride>=static_cast<std::size_t>(bonesCount))continue;
        const std::size_t at=off/kBoneStride;
        if(!Readable(pose+at*64,64))continue;
        // The same muzzle matrix a shot is built with (edf::MuzzleWorld), its bone's pose this draw's.
        if(!edf::MuzzleWorld(muzzle,reinterpret_cast<const float*>(pose+at*64),out->m))return false;
        *inst=insts[i];
        return fpview::Finite(*out);
    }
    return false;
}
// The start of the drawn line: `m`'s place less kLaserBelow of its up row (as 0x6890A0 builds it).
void LaserStartOf(const M4& m,float* out) noexcept {
    for(int j=0;j<3;++j)out[j]=m.m[12+j]-kLaserBelow*m.m[4+j];
}
void LaserDrawSelected(void* object,void* ctx,int pass,void* view) {
    auto draw=static_cast<unsigned char*>(object);
    float savedStart[4]{};bool moved=false;
    __try {
        // object is the attachment's umbra base (+0x10); attachment+8 owns the weapon. The render-only endpoints
        // are object+0x120/+0x130. The world raycast's origin, direction and hit point remain untouched.
        const auto weapon=At<const unsigned char*>(draw-0x10,8);
        const DrawSnap& d=Snap();
        const int hand=d.on && !d.riding && weapon ? SnapHand(d,weapon+kWeaponModel) : -1;
        unsigned char* inst=nullptr;M4 muzzle;
        const bool posed=weapon && LaserMuzzle(weapon,&inst,&muzzle);
        float from[3];bool have=false;
        if(hand>=0 && d.viewOk[hand]) {
            M4 t[2];
            if(posed && DrawTransforms(inst,Part::inHand,hand,DrawCamera(view),t)) {
                LaserStartOf(fpview::Mul(muzzle,t[0]),from);have=true;
            } else {
                // No drawn pose to place it by: the camera step's T on the native start (the viewmodel's own fallback).
                float native[4];std::memcpy(native,draw+kLaserStart,16);fpview::Point(d.view[hand],native,from);have=true;
            }
        } else if(posed) {
            LaserStartOf(muzzle,from);have=true;   // third person, any soldier's: the gun as it is drawn
        }
        if(have && std::isfinite(from[0]) && std::isfinite(from[1]) && std::isfinite(from[2])) {
            std::memcpy(savedStart,draw+kLaserStart,16);
            std::memcpy(draw+kLaserStart,from,12);moved=true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    reinterpret_cast<DrawFn>(nextLaserDraw)(object,ctx,pass,view);
    if(moved)std::memcpy(draw+kLaserStart,savedStart,16);
}

void __fastcall LaserDrawHook(void* object,void* ctx,int pass,void* view) {
    if(pass==3 || pass==4 || pass>5 || !Cfg().enabled){reinterpret_cast<DrawFn>(nextLaserDraw)(object,ctx,pass,view);return;}
    // Not a first-person view (or none published): no snapshot, so LaserDrawSelected takes the third-person path.
    State* state=ViewState(view);
    if(!state){LaserDrawSelected(object,ctx,pass,view);return;}
    StateScope scope(state);
    DrawSnap snap;
    if(!TakeSnapshot(&snap)){LaserDrawSelected(object,ctx,pass,view);return;}
    SnapScope taken(&snap);LaserDrawSelected(object,ctx,pass,view);
}

bool HookDrawOf(void** vt) noexcept {
    for(int i=0;i<hookedCount;++i)if(hooked[i].vtable==vt)return true;
    if(!vt || hookedCount>=static_cast<int>(sizeof(hooked)/sizeof(hooked[0])))return false;
    if(vt[kModelDrawSlot]!=image+kModelDraw)return false;   // another class's own draw: not this layout's
    void* next=nullptr;
    if(!edf::ChainVtableSlot(vt+kModelDrawSlot,reinterpret_cast<void*>(&DrawHook),&next))return false;
    hooked[hookedCount++]=Hooked{vt,reinterpret_cast<DrawFn>(next)};
    if(next!=image+kModelDraw)Log("FP model draw of vtable %p was patched already (%p): chained onto it",static_cast<void*>(vt),next);
    return true;
}
}  // namespace

// Called by the outer camera hook after it has chosen this viewport's final view. Keep the underlying eye for
// its transition back to first person, while returning every render-side override to the native world model.
void FirstPersonCameraOverride(const void* camera,bool overridden) noexcept {
    State* const state=CameraState(camera);
    if(!state || state->overridden==overridden)return;
    StateScope scope(state);
    state->overridden=overridden;
    if(state->on) {
        if(overridden){
            PlaceAll(true);Rebatch(true);
            // The map raised its near plane before our camera step. Return that frame's value, rather than
            // replacing the map's precision with the viewmodel's close clip for its first frame.
            if(state->stepNearSet && At<float>(camera,kCamNear)==state->writtenNear)
                ViewWriteNear(const_cast<void*>(camera),state->stepNear);
            state->stepNearSet=false;
        }
        else if(!state->riding){
            Unbatch();
            if(ViewDepthReady() && Cfg().fpNear>0.0f){
                if(!state->nearSet){state->savedNear=At<float>(camera,kCamNear);state->nearSet=true;}
                ViewWriteNear(const_cast<void*>(camera),Cfg().fpNear);state->writtenNear=Cfg().fpNear;
            }
        }
    }
    ClearViews(static_cast<int>(state-states));
    Publish();
}

void ResetFirstPerson() noexcept { session.fetch_add(1,std::memory_order_release); }

int LocalPlayers(unsigned char** out,int most) noexcept {
    int n=0;
    const ULONGLONG now=GetTickCount64();
    for(const auto& seen:localSeen) {
        if(!seen.human || now-seen.at>kLocalSeenMs || n>=most)continue;
        bool twice=false;
        for(int i=0;i<n;++i)twice=twice || out[i]==seen.human;
        if(!twice)out[n++]=seen.human;
    }
    return n;
}
const void* LocalViewPlayer(const void* view) noexcept {
    if(!view || !Readable(view,0x60))return nullptr;
    M4 native;std::memcpy(native.m,static_cast<const unsigned char*>(view)+0x20,64);
    const void* match=nullptr;
    for(int k=0;k<kLocalViews;++k) {
        ViewHistory v;
        if(!TakeViews(k,&v))continue;
        for(int i=0;i<kViewHistory;++i) {
            if(!v.sent[i])continue;
            float error=0;for(int j=0;j<16;++j)error+=std::fabs(native.m[j]-v.view[i].m[j]);
            if(!std::isfinite(error) || error>=0.1f)continue;
            if(match && match!=v.who[i])return nullptr;   // two players' cameras on one frame: neither's
            match=v.who[i];
        }
    }
    return match;
}

bool FirstPersonOn() noexcept { for(const auto& state:states)if(state.on)return true;return false; }
void FirstPersonInstrumentReadings(const instruments::Readings& readings,const unsigned char* human) noexcept {
    // Game thread (the HUD's vehicle snapshot): published at once, as the draws read only what is published.
    for(auto& state:states)if(state.on && state.riding && state.human==human) {
        state.instruments=readings;state.instrumentsReady=true;
        // The bank for the attitude indicator, from the seat's own frame (vc_eye_<seat>: rows left, up, ahead in the
        // world): right wing down positive. A flying vehicle's only, as its pitch.
        const float* m=state.ride.frame;
        state.instruments.value[instruments::roll]=readings.flight ? instruments::BankOf(m) : -1.0f;
        static ULONGLONG readingsLogAt=0;
        const ULONGLONG now=GetTickCount64();
        if(Cfg().debug && now-readingsLogAt>=2000) {
            readingsLogAt=now;
            Log("FP attitude readings: flight %d pitch %.1f deg, seat frame left.y %.3f up.y %.3f ahead.y %.3f -> roll %.1f deg",
                readings.flight,readings.value[instruments::pitch],m[1],m[5],m[9],state.instruments.value[instruments::roll]);
        }
        StateScope scope(&state);Publish();
    }
}
bool FirstPersonVehicleView(const void* vehicle,unsigned seat) noexcept {
    const Config& c=Cfg();
    if(!vehicle || !camOk || !drawOk || !c.enabled || !c.firstPerson || !c.fpVehicle || !toggled)return false;
    __try {
        auto v=static_cast<unsigned char*>(const_cast<void*>(vehicle));
        if(!Readable(v,kSeatCount+8) || v[kDead] || (v[0x18]&4) || seat>=SeatCount(v))return false;
        const auto vehicleCtrl=At<const unsigned char*>(v,kSelfCtrl);
        const auto station=SeatAt(v,seat);
        auto human=At<unsigned char*>(station,kSeatRider);
        const auto riderCtrl=At<const unsigned char*>(station,edf::kSeatRiderCtrl);
        if(!vehicleCtrl || !Readable(vehicleCtrl,edf::kCtrlUses+4) || At<std::int32_t>(vehicleCtrl,edf::kCtrlUses)<=0 ||
           !riderCtrl || !Readable(riderCtrl,edf::kCtrlUses+4) || At<std::int32_t>(riderCtrl,edf::kCtrlUses)<=0 ||
           !human || !Readable(human,kHumanVehicle+8) || At<const void*>(human,kSelfCtrl)!=riderCtrl ||
           !edf::IsPlayer(human) || human[kDead] || (human[0x18]&4) || HumanOnFoot(human) || At<const void*>(human,kHumanVehicle)!=v)return false;
        float eye[3];RideView ride{};
        return VehicleEye(human,eye,&ride) && ride.vehicle==v && ride.seat==static_cast<int>(seat);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool FirstPersonGunnerSight() noexcept {
    const Config& c=Cfg();
    return camOk && drawOk && c.enabled && c.firstPerson && c.fpVehicle && toggled;
}
bool FirstPersonHidesStockHud(const void* camera) noexcept {
    const auto state=CameraState(camera);
    return state && state->on && !state->overridden && state->riding && state->screens.n>0 && Cfg().fpHideStockHud;
}
bool FirstPersonScreens(CockpitScreens* out,const unsigned char* human) noexcept {
    for(const auto& state:states)if(state.on && !state.overridden && state.riding && state.screens.n && state.human==human){*out=state.screens;return true;}
    return false;
}

bool InstallFirstPerson() noexcept {
    if(!Matches(kCamStep,kCamStepCode,sizeof(kCamStepCode)) || !Matches(kModelDraw,kModelDrawCode,sizeof(kModelDrawCode))) {
        Log("FP off: native profile mismatch");
        return false;
    }
    void* next=nullptr;
    auto slot=reinterpret_cast<void**>(image+kCamVtable)+kCamStepSlot;
    camOk=edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&CamStepHook),&next);
    VehicleLookSetReady(camOk);
    if(!camOk){Log("FP off: the camera step not chained");return false;}
    nextCamStep=reinterpret_cast<CamStepFn>(next);
    drawOk=HookDrawOf(reinterpret_cast<void**>(image+kModelVtable));
    if(!drawOk) {
        void* const held=reinterpret_cast<void**>(image+kModelVtable)[kModelDrawSlot];
        HMODULE owner=nullptr;
        wchar_t path[MAX_PATH]{};
        if(held!=image+kModelDraw &&
           GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              static_cast<LPCWSTR>(held),&owner) && GetModuleFileNameW(owner,path,MAX_PATH)) {
            const wchar_t* name=wcsrchr(path,L'\\');
            _snprintf_s(drawOwner,sizeof(drawOwner),_TRUNCATE,"%ls",name ? name+1 : path);
        } else if(held!=image+kModelDraw) {
            _snprintf_s(drawOwner,sizeof(drawOwner),_TRUNCATE,"code at %p",held);
        }
        if(drawOwner[0])Log("FP off: the model draw (EDF+%X slot %zu) was replaced by %s, not chained: its first person, not ours",
                            kModelVtable,kModelDrawSlot,drawOwner);
    }
    bool changed=false;
    const bool cameraSubmitOk=RedirectCall(image+kCameraSubmitCall,image+kCameraSubmit,reinterpret_cast<void*>(&CameraSubmitHook),changed);
    drawOk=drawOk && cameraSubmitOk;
    const bool skinOk=Matches(kMeshDraw,kMeshDrawCode,sizeof(kMeshDrawCode)) &&
        RedirectCall(image+kMeshCall,image+kMeshDraw,reinterpret_cast<void*>(&MeshHook),changed);
    drawOk=drawOk && skinOk;
    const bool laserOk=Matches(kLaserDraw,kLaserDrawCode,sizeof(kLaserDrawCode)) &&
        edf::ChainVtableSlot(reinterpret_cast<void**>(image+kLaserDrawVtable)+1,reinterpret_cast<void*>(&LaserDrawHook),&nextLaserDraw);
    const bool flashOk=RedirectCall(image+kFlashCall,image+kEffectCreate,reinterpret_cast<void*>(&FlashCreateHook),changed);
    placeOk=Matches(kUmbraPlace,kUmbraPlaceCode,sizeof(kUmbraPlaceCode));
    const bool lineOk=RedirectCall(image+kAimLineCall,image+kAimLineBuild,reinterpret_cast<void*>(&AimLineHook),changed);
    placeOk=placeOk && RedirectCall(image+kRenderCall,image+kRender,reinterpret_cast<void*>(&RenderHook),changed);
    const bool passOk=placeOk && RedirectCall(image+kPassEndCall,image+kPassEnd,reinterpret_cast<void*>(&PassEndHook),changed);
    toggled=Cfg().firstPersonStart;
    Log("FP installed: camera=%d draw=%d flash=%d aim line=%d culling=%d pass=%d start=%d",camOk,drawOk,flashOk,lineOk,placeOk,passOk,toggled.load());
    Log("FP skin=%d laser=%d",skinOk,laserOk);
    return drawOk;
}
}  // namespace crew
