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
//    is a matrix of this file's: the shot matrix through T, kept up to date every camera step. The round itself was
//    made before that call from the real muzzle.
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
#include "fp_view.h"
#include "vehicle_look.h"
#include "fp_motion.h"
#include "fp_skin_gpu.h"
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
// animation starts), ReloadTime +0x20C (frames), FireRecoil +0x37C.
constexpr std::size_t kLook=0x1240,kPos=0x90,kSupport=0x711,kGroundFrames=0x1264,kActLimit=0x5D0,
                      kSets=0x1970,kSetCount=0x1980,kSetStride=0x150,kSetWeapon=0x40,
                      kShotCount=0xBD0,kFireState=0xBD4,kReloadTime=0x20C,kFireRecoil=0x37C;
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
constexpr int kFlashSlots=16,kFlashFrames=8;   // matrices handed out (a flash lives 3 updates)
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

struct Unbatched { const void* model; void* batch; };
struct alignas(16) Flash { float m[16]; const float* src; int age,hand; };
struct LookApplied { void* cam=nullptr;float before[16]{},written[16]{}; };
// --- What the camera step leaves for the draws (game thread only: the draw and the camera step both run on it) ---
struct State {
    Unbatched unbatched[kMostUnbatched]{};
    Flash flashes[kFlashSlots]{};
    int flashNext=0;
    Drawn1 drawn1[kMostSets+1]{};
    LookApplied lookApplied{};
    const unsigned char* nativeWeapon[2]{};
    float nativeRest[2][3]{};
    bool on;                    // the local player's camera is in first person
    unsigned char* human;       // that player
    unsigned char* cam;         // its camera
    const void* body;           // its body model
    const void* weapons[kMostWeapons];
    int weaponCount;
    const void* inHand[kMostSets];   // the weapons in hand (each set's current one): their models
    int handOf[kMostSets];           // ...and the hand each is in (kRight, kLeft)
    int inHandCount;
    M4 renderView{};bool renderViewReady=false;
    float stock[16];            // the stock matrix this frame (put back before the next step)
    float eyeLocal[3];          // the eased eye in the body frame
    bool fresh;                 // the eye taken as it is next frame
    float savedNear,savedSmooth;
    bool nearSet,smoothSet;
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
    bool shown[2];              // visible arm ownership for the colour topology mask (neither while riding)
    bool axisOk[2];             // a point on the axis of the weapon each hand holds, after T (the weapon's root)
    float axis[2][3];
    bool onOther[2];            // a free hand drawn on the other hand's weapon, whose bounds after T are otherBox
    M4 otherBox[2];
    // Each hand's weapon's size along its own axes (WeaponBounds), taken once a weapon: the hold it is fitted to is the
    // weapon's, not the frame's (a reload's magazine bone does not move the gun).
    // Debug=1: the steadiness probe (Probe).
    bool probeHave[2],probeWristHave[2],builtReady;
    float probeRoot[2][3],probeStep[2],probeWrist[2][3],probeWristStep[2],probeSubmitOff;
    unsigned probeSubmits,probeSubmitsOff,probeFrames,probeAir;
    float probeSpeed,probeMountGap[2],probeMountLeast[2];
    bool probeMountHave[2];
    float probeBend[2];
    M4 built;                   // the camera matrix the viewmodel was built on this step
    M4 probeAnchor[2];bool probeAnchored[2];float probeDrawRoot,probeDrawMount;unsigned probeDraws;
    unsigned probeCamThread,probeDrawThread,probeCamSteps;
    const unsigned char* boundsOf[2];
    bool boundsOk[2];
    M4 bounds[2];
    bool riding;                // in a vehicle: the eye at the seat's cockpit, the rider not drawn (Ride)
    RideView ride;
    CockpitScreens screens;     // ...and its screens round the eye (VehicleScreens), for the HUD
    instruments::Readings instruments;
    bool instrumentsReady;
};
// Each local camera owns its pose, motion, batches and effects. Never evict another local player's view.
constexpr int kLocalViews=4;
State states[kLocalViews]{};
State* selected=&states[0];
ULONGLONG viewRevision=0;
State& Current() noexcept { return *selected; }
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
// Mission reset is published from preload; only the game/render thread drops its camera objects and GPU cache.
std::atomic<unsigned> session{0};
unsigned appliedSession=0;
void SyncSession() noexcept {
    const unsigned next=session.load(std::memory_order_acquire);
    if(next==appliedSession)return;
    for(auto& state:states)state=State{};
    selected=&states[0];fpskin::Clear();appliedSession=next;
}
void CaptureRenderCamera(void* camera,const float* world) noexcept {
    State* state=CameraState(camera);
    if(!state)return;
    M4 frame;std::memcpy(frame.m,world,64);
    state->renderViewReady=fpview::NativeView(frame,&state->renderView);
    if(Cfg().debug && state->on && state->builtReady && !state->riding) {
        float off=0.0f;for(int i=0;i<16;++i)off+=std::fabs(frame.m[i]-state->built.m[i]);
        ++state->probeSubmits;
        if(off>1e-3f){++state->probeSubmitsOff;state->probeSubmitOff=std::fmax(state->probeSubmitOff,off);}
    }
    ++viewRevision;
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
State* MatchView(const M4& native) noexcept {
    State* match=nullptr;
    for(auto& state:states) {
        if(!state.on || !state.cam)continue;
        if(!state.renderViewReady)continue;
        const M4& expected=state.renderView;
        float error=0;for(int i=0;i<16;++i)error+=std::fabs(native.m[i]-expected.m[i]);
        if(!std::isfinite(error) || error>=0.1f)continue;
        if(match)return nullptr; // Identical camera frames are ambiguous; never borrow another player's model.
        match=&state;
    }
    return match;
}
State* ViewState(const void* view) noexcept {
    if(!view || !Readable(view,0x60)) {
        // Test/legacy single-view submissions without native parameters remain unambiguous only with one camera.
        State* only=nullptr;int cameras=0;
        for(auto& state:states)if(state.cam){++cameras;if(state.on)only=&state;}
        return cameras==1 ? only : nullptr;
    }
    M4 native;std::memcpy(native.m,static_cast<const unsigned char*>(view)+0x20,64);
    static M4 cached{};
    static ULONGLONG frame=~ULONGLONG(0);
    static unsigned generation=~0u;
    static ULONGLONG revision=~ULONGLONG(0);
    static State* owner=nullptr;
    // Camera callbacks and HUD publication also run in a frame; the validity bits prevent reusing a retired view.
    if(frame==renderFrame && generation==appliedSession && revision==viewRevision && std::memcmp(cached.m,native.m,64)==0)return owner;
    cached=native;frame=renderFrame;generation=appliedSession;revision=viewRevision;owner=MatchView(native);
    if(Cfg().debug && At<int>(view,4)==0) {
        static ULONGLONG logged=0;static unsigned matched=0,unmatched=0;
        if(owner)++matched;else if(FirstPersonOn())++unmatched;
        if(renderFrame-logged>=300){Log("FP view routing: matched %u, unmatched %u main-view matrices",matched,unmatched);matched=unmatched=0;logged=renderFrame;}
    }
    return owner;
}
using CreateFn=void*(__fastcall*)(void*,void*,void*,void*);
std::atomic<bool> toggled{false};   // read by FirstPersonGunnerSight on the draw thread too
bool keyWas=false,keyRode=false,keyCancelled=false;   // keyRode: riding when the key went down
int passLog[2][6]{};   // the body's draws, the weapons' in hand, by pass
ULONGLONG passLogAt=0;

bool KeyInputAllowed() noexcept {
    if(MapHoldsKeys())return false;
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
void Toggle(const unsigned char* h) noexcept {
    const Config& c=Cfg();
    const bool down=c.firstPersonKey>0 && (edf::AsyncKey(c.firstPersonKey)&0x8000)!=0;
    // Loss of input ownership cancels this gesture; it is not a key release. If the
    // physical key is still held when focus/the map returns, wait for its release
    // before allowing another gesture, so the held boarding/map key cannot toggle.
    if(!h || !c.enabled || !c.firstPerson || !camOk || !drawOk || !KeyInputAllowed() || KeyTaken(h,c.firstPersonKey)) {
        keyWas=false;keyCancelled=down;return;
    }
    if(keyCancelled){if(!down)keyCancelled=false;return;}
    const bool riding=h && !HumanOnFoot(h);
    if(down && !keyWas)keyRode=riding;
    if(!down && keyWas && keyRode==riding){const bool on=!toggled.load();toggled.store(on);Log("FP %s (key 0x%X)",on ? "on" : "off",c.firstPersonKey);}
    keyWas=down;
}

void Gather(unsigned char* h) noexcept {
    Current().body=h+kBodyModel;
    Current().weaponCount=0;
    const auto list=At<unsigned char* const*>(h,kWeapons);
    const auto n=At<std::uint64_t>(h,kWeaponCount);
    if(!list || n>kMostWeapons)return;
    for(std::uint64_t i=0;i<n;++i) {
        if(!list[i])continue;
        const unsigned char* model=list[i]+kWeaponModel;
        Current().weapons[Current().weaponCount++]=model;
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

// One hand's weapon this frame: its shots, a reload started.
void Hand(const unsigned char* w,std::uint64_t k,std::int32_t shots,std::int32_t fire,fpmotion::Cue& c) noexcept {
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
    if(fire!=3 || Current().fire[k]==3)return;
    const std::int32_t t=At<std::int32_t>(w,kReloadTime);
    Current().reloadLeft=t>0 ? (t<kMostReloadFrames ? t : kMostReloadFrames) : 0;
    if(Cfg().debug)Log("FP reload %d frames (FireRecoil %.3f)",t,At<float>(w,kFireRecoil));
}

// The hands' weapons this frame: shots, a reload started, a switch.
void Weapons(const unsigned char* h,fpmotion::Cue& c) noexcept {
    Current().sampledShots=Current().notifiedShots=0;
    auto n=At<std::uint64_t>(h,kSetCount);
    if(n>kMostSets)n=kMostSets;
    for(std::uint64_t k=0;k<n;++k) {
        const unsigned char* w=SetWeapon(h,k);
        const std::int32_t shots=w ? At<std::int32_t>(w,kShotCount) : 0,fire=w ? At<std::int32_t>(w,kFireState) : 0;
        if(Current().have && w!=Current().held[k] && Current().held[k]){c.switched=true;Current().reloadLeft=0;}
        if(Current().have && w && w==Current().held[k])Hand(w,k,shots,fire,c);
        Current().held[k]=w;Current().shots[k]=shots;Current().fire[k]=fire;
    }
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
    if(Current().reloadLeft>0)--Current().reloadLeft;
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
    Place(Current().body,eye);
    for(int i=0;i<Current().inHandCount;++i)Place(Current().inHand[i],eye);
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

bool StillPlayers(const void* model) noexcept {
    for(int i=0;i<Current().weaponCount;++i)if(Current().weapons[i]==model)return true;
    return false;
}

// The batches back: of the weapons no longer in hand (all: `all`), into those still the player's.
void Rebatch(bool all) noexcept {
    for(Unbatched& u:Current().unbatched) {
        if(!u.model || (!all && HandOfModel(u.model)>=0))continue;
        if(StillPlayers(u.model) && !At<void*>(u.model,kBatch))Put<void*>(const_cast<void*>(u.model),kBatch,u.batch);
        u=Unbatched{};
    }
}

// The weapons in hand out of their batches (kept).
void Unbatch() noexcept {
    for(int i=0;i<Current().inHandCount;++i) {
        void* const batch=At<void*>(Current().inHand[i],kBatch);
        if(!batch)continue;
        for(Unbatched& u:Current().unbatched) {
            if(u.model)continue;
            u=Unbatched{Current().inHand[i],batch};
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
        Current().inHand[Current().inHandCount]=w+kWeaponModel;Current().handOf[Current().inHandCount++]=hand;
        LogWeapon(w,hand);
        if(!anchored[hand])anchored[hand]=RootPose(w+kWeaponModel,&anchor[hand]);
    }
}

// The hand a weapon model is in, -1: not in hand.
int HandOfModel(const void* model) noexcept {
    for(int i=0;i<Current().inHandCount;++i)if(Current().inHand[i]==model)return Current().handOf[i];
    return -1;
}

// The Current().flashes handed out lately, moved with the viewmodel (their shot matrix through this frame's T).
void Flashes() noexcept {
    for(Flash& f:Current().flashes) {
        if(!f.src || f.age>=kFlashFrames){f.src=nullptr;continue;}
        ++f.age;
        if(Current().viewOk[f.hand])std::memcpy(f.m,fpview::Mul(MatrixAt(reinterpret_cast<const unsigned char*>(f.src)),Current().view[f.hand]).m,64);
    }
}

// A shot's flash about to be made for weapon `w`: in first person, its matrix pointer is one of the Current().flashes'.
void Relocate(unsigned char* w) noexcept {
    const int hand=Current().on ? HandOfModel(w+kWeaponModel) : -1;
    if(hand<0 || !Current().viewOk[hand])return;
    auto& event=Current().shotEvent[hand];
    if(event.weapon!=w)event=State::ShotEvent{w,0,0};
    if(event.count<3)++event.count;
    event.recoil=RecoilOf(w);
    const auto src=At<const float*>(w,kFlashMatrix);
    if(!src)return;
    Flash& f=Current().flashes[Current().flashNext];
    Current().flashNext=(Current().flashNext+1)%kFlashSlots;
    f.src=src;f.age=0;f.hand=hand;
    std::memcpy(f.m,fpview::Mul(MatrixAt(reinterpret_cast<const unsigned char*>(src)),Current().view[hand]).m,64);
    Put<const float*>(w,kFlashMatrix,f.m);
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

void* __fastcall FlashCreateHook(void* factory,void* a,void* cls,void* param) {
    State* before=selected;
    __try {
        auto weapon=static_cast<unsigned char*>(param)-kFlashParam;
        for(auto& state:states)if(state.on) {
            selected=&state;
            if(HandOfModel(weapon+kWeaponModel)>=0){Relocate(weapon);break;}
        }
        selected=before;
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    selected=before;
    return reinterpret_cast<CreateFn>(image+kEffectCreate)(factory,a,cls,param);
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
        Log("FP probe %s hand: anchored=%d view=%d | root (%.2f,%.2f,%.2f) from the eye | facing %.0f deg | root in the camera (%.3f,%.3f,%.3f), most moved in a frame %.4f | wrist in the camera (%.3f,%.3f,%.3f), most moved %.4f | in hand %d, reload %d, act 0x%X",
            hand==kRight ? "right" : "left",anchored[hand],Current().viewOk[hand],a.m[12]-eye[0],a.m[13]-eye[1],a.m[14]-eye[2],
            yaw,root[hand][0],root[hand][1],root[hand][2],Current().probeStep[hand],
            Current().probeWrist[hand][0],Current().probeWrist[hand][1],Current().probeWrist[hand][2],Current().probeWristStep[hand],
            Current().inHandCount,Current().reloadLeft,Current().act);
        if(Current().probeMountHave[hand])
            Log("FP probe %s mount: the body's mount bone from the weapon's root as drawn, %.4f..%.4f m (scaled by nothing: world)",
                hand==kRight ? "right" : "left",Current().probeMountLeast[hand],Current().probeMountGap[hand]);
        Current().probeMountHave[hand]=false;Current().probeMountGap[hand]=0.0f;
        Log("FP probe %s arm: the forearm turned against the hand by %.1f deg at most",hand==kRight ? "right" : "left",Current().probeBend[hand]);
        Current().probeBend[hand]=0.0f;
        Current().probeStep[hand]=Current().probeWristStep[hand]=0.0f;
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
    Log("FP probe draws: %u weapon draws, the drawn root %.4f m at most from the camera step's; the body's mount bone %.4f m",
        Current().probeDraws,Current().probeDrawRoot,Current().probeDrawMount);
    Log("FP probe order: %u camera steps (thread %u), weapon draws on thread %u",
        Current().probeCamSteps,Current().probeCamThread,Current().probeDrawThread);
    Current().probeCamSteps=0;
    Current().probeDraws=0;Current().probeDrawRoot=Current().probeDrawMount=0.0f;
    Log("FP probe motion: %u frames, fastest %.3f m/frame, %u in the air, bob phase %.2f",Current().probeFrames,
        Current().probeSpeed,Current().probeAir,Current().motion.phase);
    Current().probeSubmits=Current().probeSubmitsOff=0;Current().probeSubmitOff=0.0f;
    Current().probeFrames=Current().probeAir=0;Current().probeSpeed=0.0f;
}

// The camera, the player's state given back: the near clip, the look smoothing.
void Leave() noexcept {
    ++viewRevision;
    if(Current().on){PlaceAll(true);Rebatch(true);}
    if(Current().cam && Current().nearSet)Put<float>(Current().cam,kCamNear,Current().savedNear);
    if(Current().human && Current().smoothSet)Put<float>(Current().human,kLookSmooth,Current().savedSmooth);
    if(Current().on)Log("FP view back to the stock camera");
    // Keep the camera binding even when first person is off: its independent free-look restore still runs.
    auto cam=Current().cam;const auto look=Current().lookApplied;
    const auto renderView=Current().renderView;const bool renderReady=Current().renderViewReady;
    Flash flashes[kFlashSlots];std::memcpy(flashes,Current().flashes,sizeof(flashes));const int flashNext=Current().flashNext;
    Current()=State{};Current().cam=cam;Current().lookApplied=look;
    Current().renderView=renderView;Current().renderViewReady=renderReady;
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
const void* watched=nullptr;
void Watch(const unsigned char* h) noexcept {
    const unsigned char* w=Cfg().debug && h ? SetWeapon(h,0) : nullptr;
    watched=w ? w+kWeaponModel : nullptr;
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
    Current().riding=true;Current().ride=ride;
    HookDrawOf(At<void**>(ride.vehicle+0xE40,0));
    // The eye marker is also the seat's frame: cockpit, eye and horizon must roll/pitch together.
    std::memcpy(cam+kCamMatrix,ride.frame,64);
    VehicleLookCamera(h,reinterpret_cast<float*>(cam+kCamMatrix));
    VehicleScreens(ride.vehicle,ride.seat,eye,&Current().screens);
    if(Current().nearSet){Put<float>(cam,kCamNear,Current().savedNear);Current().nearSet=false;}
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
    ++viewRevision;
    const Config& c=Cfg();
    unsigned char* const h=CameraPlayer(cam);
    static ULONGLONG toggleFrame=~ULONGLONG(0);
    if(toggleFrame!=renderFrame){toggleFrame=renderFrame;Toggle(h);}
    if(!Wanted(cam,h)){if(Current().on && Current().cam==cam)Leave();if(!Current().on)Watch(h);return;}
    watched=nullptr;
    if(!Current().on || Current().cam!=cam || Current().human!=h) {
        Leave();
        Current().on=true;Current().cam=cam;Current().human=h;Current().fresh=true;Current().moved[kRight]=Current().moved[kLeft]=fpview::Identity();
        Log("FP view on %p (player %p)",cam,h);
    }
    std::memcpy(Current().stock,cam+kCamMatrix,sizeof(Current().stock));
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
    if(c.fpNear>0.0f){if(!Current().nearSet){Current().savedNear=At<float>(cam,kCamNear);Current().nearSet=true;}Put<float>(cam,kCamNear,c.fpNear);}
    if(c.fpAimSnap){if(!Current().smoothSet){Current().savedSmooth=At<float>(h,kLookSmooth);Current().smoothSet=true;}Put<float>(h,kLookSmooth,1.0f);}
    Gather(h);
    Views(h,cam);
    Rebatch(false);
    Unbatch();
    Flashes();
}

// The render about to start: the viewmodel's models placed around the eye for Umbra (above).
void __fastcall RenderHook(void* system,std::uint8_t a,std::uint8_t b) {
    SyncSession();++renderFrame;
    State* before=selected;
    __try { for(auto& state:states)if(state.on){selected=&state;PlaceAll(false);} } __except(EXCEPTION_EXECUTE_HANDLER){}
    selected=before;
    reinterpret_cast<RenderFn>(image+kRender)(system,a,b);
}

// A model of the viewmodel drawn in the opaque pass this render.
void Drew1(const void* model) noexcept {
    Drawn1* free=nullptr;
    for(Drawn1& d:Current().drawn1) {
        if(d.model==model){d.frame=renderFrame;return;}
        if(!free && d.frame!=renderFrame)free=&d;
    }
    if(free)*free=Drawn1{model,renderFrame};
}

bool Drew1Now(const void* model) noexcept {
    for(const Drawn1& d:Current().drawn1)if(d.model==model && d.frame==renderFrame)return true;
    return false;
}

// The opaque pass's end: what of the viewmodel it left out, submitted.
void PassEndSelected(void* system,int pass) {
    __try {
        if(Current().on && pass==1) {
            const void* models[kMostSets+1];
            int n=0;
            models[n++]=Current().body;
            for(int i=0;i<Current().inHandCount && n<kMostSets+1;++i)models[n++]=Current().inHand[i];
            for(int i=0;i<n;++i)
                if(models[i] && At<const void*>(models[i],kUmbraObject) && !Drew1Now(models[i]))
                    reinterpret_cast<SubmitFn>(image+kSubmit)(system,const_cast<void*>(models[i]),1);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    reinterpret_cast<PassEndFn>(image+kPassEnd)(system,pass);
}

void __fastcall PassEndHook(void* system,int pass) {
    State* state=ViewState(static_cast<unsigned char*>(system)+0xE0);
    if(!state){reinterpret_cast<PassEndFn>(image+kPassEnd)(system,pass);return;}
    StateScope scope(state);PassEndSelected(system,pass);
}

void CameraAfterNative(unsigned char* cam) noexcept {
    __try {
        Camera(cam);
        if(!Current().on || Current().cam!=cam || !Current().riding) {
            LookApplied record;record.cam=cam;std::memcpy(record.before,cam+kCamMatrix,64);
            if(VehicleLookCamera(CameraPlayer(cam),reinterpret_cast<float*>(cam+kCamMatrix))) {
                std::memcpy(record.written,cam+kCamMatrix,64);Current().lookApplied=record;
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER){Current()=State{};}
}

void CamStepSelected(void* camera,void* step) {
    auto cam=static_cast<unsigned char*>(camera);
    if(Current().lookApplied.cam==cam && std::memcmp(cam+kCamMatrix,Current().lookApplied.written,64)==0)
        std::memcpy(cam+kCamMatrix,Current().lookApplied.before,64);
    if(Current().lookApplied.cam==cam)Current().lookApplied=LookApplied{};
    if(Current().on && Current().cam==cam)std::memcpy(cam+kCamMatrix,Current().stock,sizeof(Current().stock));
    nextCamStep(camera,step);
    CameraAfterNative(cam);
}

void __fastcall CamStepHook(void* camera,void* step) {
    SyncSession();
    if(auto state=CameraState(camera)){StateScope scope(state);CamStepSelected(camera,step);return;}
    nextCamStep(camera,step);
    auto cam=static_cast<unsigned char*>(camera);
    if(!CameraPlayer(cam))return; // Menu and remote cameras must not consume local player slots.
    if(auto state=AcquireCamera(camera)){StateScope scope(state);CameraAfterNative(cam);}
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
    *hand=kRight;
    if(!Current().on)return Part::none;
    if(Current().riding && model==Current().ride.vehicle+0xE40)return Part::instruments;
    if(model==Current().body)return Part::body;
    if((*hand=HandOfModel(model))>=0)return Part::inHand;
    *hand=kRight;
    for(int i=0;i<Current().weaponCount;++i)if(Current().weapons[i]==model)return Part::stowed;
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

float saved[kMostInsts][kMostBones*16];

// --- The arms posed for the first person (fp_view.h RollAngle / TurnAbout / ReachShoulder / SegmentMap) ---
// The place the upper arm turns toward, in the eye's frame (right, up, ahead; times the viewmodel's scale): down and a
// little behind, out by half the soldier's own shoulder width (kata_l to kata_r). The way the elbow is rolled toward
// (seen along the view): out, and more down.
constexpr float kShoulderDown=-0.24f,kShoulderBack=-0.06f,kElbowOut=1.0f,kElbowDown=-1.5f;
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
ArmFit FitArm(const unsigned char* inst,const float* pose,const M4& t,int hand,const M4& camera,const M4* otherBox) noexcept {
    ArmFit fit{};
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
    if(Current().onOther[hand] && Current().reloadLeft==0) {
        float move[3];
        if(otherBox && fpview::SnapToBox(otherBox[hand],h0,0.035f*scale,move))
            for(int i=0;i<3;++i)wrist[i]+=move[i];
    }
    fit.hand=fpview::Translate(wrist[0]-h0[0],wrist[1]-h0[1],wrist[2]-h0[2]);
    if(Cfg().debug && Current().probeAnchored[hand] && inst==At<const unsigned char*>(Current().body,kBaseLod+kLodInst)) {
        // The draw-time probe: the body's mount bone now against the weapon's root T was built from.
        const int mount=BoneIndex(inst,hand==kRight ? L"arms_r" : L"arms_l");
        if(mount>=0) {
            float gap[3];fpview::Sub(pose+mount*16+12,Current().probeAnchor[hand].m+12,gap);
            Current().probeDrawMount=std::fmax(Current().probeDrawMount,fpview::Length(gap));
        }
    }
    if(Cfg().debug && Current().body && inst==At<const unsigned char*>(Current().body,kBaseLod+kLodInst)) {   // the steadiness probe: the wrist in the camera's frame (the base LOD's)
        M4 inverse;float local[3];
        if(fpview::InverseAffine(camera,&inverse)) {
            fpview::Point(inverse,wrist,local);
            if(Current().probeWristHave[hand]) {
                float dd[3];fpview::Sub(local,Current().probeWrist[hand],dd);
                Current().probeWristStep[hand]=std::fmax(Current().probeWristStep[hand],fpview::Length(dd));
            }
            std::memcpy(Current().probeWrist[hand],local,12);Current().probeWristHave[hand]=true;
        }
    }
    fit.ok=fpview::ArmChainTo(s0,e0,h0,wrist,shoulder,pole,&fit.upper,&fit.fore);
    if(Cfg().debug && fit.ok && inst==At<const unsigned char*>(Current().body,kBaseLod+kLodInst)) {
        // The probe: how far the forearm was turned against the hand (the hand keeps its own turn on the gun), deg.
        float e1[3],before[3],after[3];
        fpview::Point(fit.upper,e0,e1);fpview::Sub(h0,e0,before);fpview::Sub(wrist,e1,after);
        if(fpview::Unit(before) && fpview::Unit(after))
            Current().probeBend[hand]=std::fmax(Current().probeBend[hand],std::acos(fpview::Clamp(fpview::Dot3(before,after),-1.0f,1.0f))*57.29578f);
    }
    return fit;
}

// Arm/weapon poses through T, restored after the draw. Non-arm body bones stay native: fp_skin_gpu submits only
// triangles owned by one visible arm, excluding mixed torso/shoulder and opposite-arm faces instead of stretching
// their skinning weights. `t` is each hand's transform (a weapon uses its own hand's twice).
void Pose(unsigned char* inst,const M4* t,bool body,const M4& camera,const M4* otherBox,float* backup) noexcept {
    const auto n=At<std::int32_t>(inst,kInstCount);
    auto pose=At<float*>(inst,kInstPose);
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    if(!pose || !bones || n<=0 || n>kMostBones)return;
    std::memcpy(backup,pose,static_cast<std::size_t>(n)*64);
    ArmFit fit[2]{};
    if(body && Cfg().fpArmIk && !Current().riding)for(int hand=kRight;hand<=kLeft;++hand)fit[hand]=FitArm(inst,backup,t[hand],hand,camera,otherBox);
    for(std::int32_t i=0;i<n;++i) {
        float* p=pose+i*16;
        const wchar_t* name=At<const wchar_t*>(bones+static_cast<std::size_t>(i)*kBoneStride,0);
        const std::size_t len=name ? std::wcslen(name) : 0;
        const bool left=body && len>=2 && name[len-2]==L'_' && name[len-1]==L'l';
        if(body && (!ArmBone(name) || !Current().shown[left ? kLeft : kRight])) {
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

void Unpose(unsigned char* inst,const float* backup) noexcept {
    const auto n=At<std::int32_t>(inst,kInstCount);
    auto pose=At<float*>(inst,kInstPose);
    if(pose && n>0 && n<=kMostBones)std::memcpy(pose,backup,static_cast<std::size_t>(n)*64);
}

fpskin::Mask BodyMask(const void* palette) noexcept {
    fpskin::Mask mask{};
    if(!palette || !Current().body)return mask;
    unsigned char* insts[kMostInsts];
    const int instances=InstancesOf(const_cast<unsigned char*>(static_cast<const unsigned char*>(Current().body)),insts);
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
            mask.arm[i]=ArmBone(name) && Current().shown[left ? kLeft : kRight];
            mask.side[i]=left ? 2 : 1;
        }
        return mask;
    }
    return mask;
}

void InstrumentPose(unsigned char* inst,float* backup) noexcept {
    const auto n=At<std::int32_t>(inst,kInstCount);
    auto pose=At<float*>(inst,kInstPose);
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    if(!pose || !bones || n<=0 || n>kMostBones)return;
    std::memcpy(backup,pose,static_cast<std::size_t>(n)*64);
    for(int i=0;i<n;++i) {
        const auto name=At<const wchar_t*>(bones+static_cast<std::size_t>(i)*kBoneStride,0);
        if(name && std::wcsncmp(name,L"vc_shell_",9)==0) {
            unsigned mask=0,parent=0;
            if(swscanf_s(name,L"vc_shell_%u_%u",&mask,&parent)==2) {
                const M4 original=MatrixAt(reinterpret_cast<const unsigned char*>(pose+i*16));
                const M4 moved=instruments::ShellPose(original,mask,Current().ride.seat);
                std::memcpy(pose+i*16,moved.m,64);
            }
            continue;
        }
        if(!name || std::wcsncmp(name,L"vc_i_",5)!=0)continue;
        int seat=-1,screen=-1,field=-1,place=0,segment=0;wchar_t mode=0;
        const int fields=swscanf_s(name,L"vc_i_%d_%d_%lc_%d_%d_%d",&seat,&screen,&mode,1u,&field,&place,&segment);
        if(fields<4 || ((mode==L'd' || mode==L'D' || mode==L'p') && fields!=6))continue;
        const bool mine=seat==Current().ride.seat;
        const bool rangedNeedle=mode==L'n' && fields>=5;
        const bool neutralWidget=mode==L'D' || mode==L'B' || mode==L'T';
        // Adjacent stations visible from this seat use the same language, while their
        // new health/percent widgets remain neutral without that seat's live data.
        if(!mine && mode!=L'l' && mode!=L'c' && mode!=L'p' && mode!=L'v' && mode!=L'u'
            && mode!=L'r' && mode!=L'q' && mode!=L'a' && !rangedNeedle && !neutralWidget)continue;
        const bool ready=Current().instrumentsReady && (mine || (mode==L'l' && place==0));
        const M4 original=MatrixAt(reinterpret_cast<const unsigned char*>(pose+i*16));
        const M4 moved=instruments::Pose(original,mode,field,place,segment,Current().instruments,ready);
        std::memcpy(pose+i*16,moved.m,64);
    }
}

void LogPass(Part part,int pass) noexcept {
    if(!Cfg().debug || pass<0 || pass>5 || part==Part::stowed || part==Part::instruments)return;
    ++passLog[part==Part::body ? 0 : 1][pass];
    const ULONGLONG now=GetTickCount64();
    if(now-passLogAt<5000)return;
    if(passLogAt)Log("FP draws in 5 s by pass 0/1/2/3/5: body %d/%d/%d/%d/%d, weapons in hand %d/%d/%d/%d/%d (weapons %d)",
                     passLog[0][0],passLog[0][1],passLog[0][2],passLog[0][3],passLog[0][5],
                     passLog[1][0],passLog[1][1],passLog[1][2],passLog[1][3],passLog[1][5],Current().weaponCount);
    std::memset(passLog,0,sizeof(passLog));passLogAt=now;
}

// Debug=1: a model not the player's drawn (colour pass) where the weapon in the right hand really is: what it is.
void NearWeapon(const void* model,int pass) noexcept {
    static const void* logged[8]{};
    static int next=0;
    if(!Cfg().debug || pass!=1 || !Current().inHandCount)return;
    M4 root,gun;
    if(!RootPose(static_cast<const unsigned char*>(model),&root) || !RootPose(static_cast<const unsigned char*>(Current().inHand[0]),&gun))return;
    const float d[3]={root.m[12]-gun.m[12],root.m[13]-gun.m[13],root.m[14]-gun.m[14]};
    if(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>0.3f*0.3f)return;
    for(const void* l:logged)if(l==model)return;
    logged[next]=model;next=(next+1)%8;
    const auto weapon=static_cast<const unsigned char*>(Current().inHand[0])-kWeaponModel;
    const long long off=static_cast<const unsigned char*>(model)-weapon;
    Log("FP a model %p drawn at the weapon %p's place: vtable +0x%llX, weapon%+lld",model,static_cast<const void*>(weapon),
        static_cast<unsigned long long>(At<const unsigned char*>(model,0)-image),off);
}

// The camera a draw is made with: its native view (view+0x20: the camera's inverse, X and Z negated, §2026-10-17)
// undone; without one (offline) the camera matrix.
M4 DrawCamera(const void* view) noexcept {
    if(view && Readable(view,0x60)) {
        M4 native;std::memcpy(native.m,static_cast<const unsigned char*>(view)+0x20,64);
        for(int row=0;row<4;++row){native.m[row*4]*=-1;native.m[row*4+2]*=-1;}
        M4 camera;
        if(fpview::InverseAffine(native,&camera))return camera;
    }
    return MatrixAt(Current().cam+kCamMatrix);
}

// The T of each hand for drawing the skeleton instance `inst` of a part (docs/fp-render-re.md §17): its anchor as this
// draw's pose array has it (a weapon's root; the body's mount bone) put where the camera step placed it in the camera's
// frame, seen from this draw's camera: T = anchor_now^-1 * L * C_draw. So the hold neither follows the update steps the
// game took after the camera step (the walk's and the jump's twitch) nor a camera the draw does not use. False: no
// placement (the camera step's T is used).
bool DrawTransforms(const unsigned char* inst,Part part,int hand,const M4& camera,M4* t) noexcept {
    const auto pose=At<const float*>(inst,kInstPose);
    if(!pose)return false;
    if(part==Part::inHand) {
        M4 inv;
        if(!Current().placeWeaponOk[hand] || !fpview::InverseAffine(MatrixAt(reinterpret_cast<const unsigned char*>(pose)),&inv))return false;
        t[0]=t[1]=fpview::Mul(fpview::Mul(inv,Current().placeWeapon[hand]),camera);
        return fpview::Finite(t[0]);
    }
    for(int k=kRight;k<=kLeft;++k) {
        const int at=Current().placeBodyOk[k] ? BoneIndex(inst,Current().bodyMount[k]) : -1;
        M4 inv;
        if(at<0 || !fpview::InverseAffine(MatrixAt(reinterpret_cast<const unsigned char*>(pose+at*16)),&inv))return false;
        t[k]=fpview::Mul(fpview::Mul(inv,Current().placeBody[k]),camera);
        if(!fpview::Finite(t[k]))return false;
    }
    return true;
}

void DrawSelected(void* model,void* ctx,int pass,void* view) {
    const DrawFn next=NextDraw(model);
    Part part=Part::none;
    int hand=kRight;
    __try { part=PartOf(model,&hand); } __except(EXCEPTION_EXECUTE_HANDLER){part=Part::none;}
    if(part!=Part::none)LogPass(part,pass);
    else if(!Current().on && model==watched)LogPass(Part::inHand,pass);
    else if(Current().on){__try { NearWeapon(model,pass); } __except(EXCEPTION_EXECUTE_HANDLER){}}
    if(part==Part::none || pass==3 || pass==4 || pass>5){next(model,ctx,pass,view);return;}
    if((pass==0 && part!=Part::instruments) || part==Part::stowed)return;
    if(pass==1 && part!=Part::instruments)Drew1(model);
    if(Cfg().debug && pass==1) {
        // The draw-time probe: has the weapon's root moved since the camera step built T from it (the drawn weapon
        // then moves in the view), and does the body's mount bone still meet it (else the hand leaves the grip)?
        __try {
            if(part==Part::inHand && Current().probeAnchored[hand]) {
                M4 root;
                if(RootPose(static_cast<const unsigned char*>(model),&root)) {
                    float d[3];fpview::Sub(root.m+12,Current().probeAnchor[hand].m+12,d);
                    Current().probeDrawRoot=std::fmax(Current().probeDrawRoot,fpview::Length(d));++Current().probeDraws;
                    Current().probeDrawThread=GetCurrentThreadId();
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER){}
    }
    unsigned char* insts[kMostInsts];
    int n=0;
    __try {
        if(part==Part::instruments || (Current().viewOk[kRight] && Current().viewOk[kLeft])) {
            const M4 stepT[2]={part==Part::body ? Current().view[kRight] : Current().view[hand],part==Part::body ? Current().view[kLeft] : Current().view[hand]};
            const M4 camera=part==Part::instruments ? M4{} : DrawCamera(view);
            // A free hand's weapon box, placed as the draw shows that weapon (its box along its own axes, at the
            // other hand's placement, from this draw's camera).
            M4 otherBox[2]{};
            for(int k=kRight;k<=kLeft;++k) {
                M4 box;bool flip=false;
                if(part==Part::body && Current().onOther[k] && Current().placeWeaponOk[1-k] && HandBounds(1-k,HandWeapon(1-k,&flip),&box))
                    otherBox[k]=fpview::Mul(fpview::Mul(box,Current().placeWeapon[1-k]),camera);
                else otherBox[k]=Current().otherBox[k];
            }
            n=InstancesOf(static_cast<unsigned char*>(model),insts);
            for(int i=0;i<n;++i) {
                if(part==Part::instruments){InstrumentPose(insts[i],saved[i]);continue;}
                M4 t[2];
                const bool drawn=!Current().riding && DrawTransforms(insts[i],part,hand,camera,t);
                if(!drawn){t[0]=stepT[0];t[1]=stepT[1];}
                Pose(insts[i],t,part==Part::body,camera,otherBox,saved[i]);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){for(int i=0;i<n;++i)Unpose(insts[i],saved[i]);n=0;}
    if(part==Part::body)fpskin::Begin(fpskin::Mask{}); // the mesh draw selects its actual LOD palette
    next(model,ctx,pass,view);
    if(part==Part::body)fpskin::End();
    __try { for(int i=0;i<n;++i)Unpose(insts[i],saved[i]); } __except(EXCEPTION_EXECUTE_HANDLER){}
}

void __fastcall DrawHook(void* model,void* ctx,int pass,void* view) {
    if(pass==3 || pass==4 || pass>5){NextDraw(model)(model,ctx,pass,view);return;}
    State* state=ViewState(view);
    if(!state){NextDraw(model)(model,ctx,pass,view);return;}
    StateScope scope(state);DrawSelected(model,ctx,pass,view);
}

using MeshFn=void(__fastcall*)(void*,void*,int,std::uint8_t,void*);
void __fastcall MeshHook(void* mesh,void* ctx,int pass,std::uint8_t flag,void* cache) {
    if(!fpskin::Active()){reinterpret_cast<MeshFn>(image+kMeshDraw)(mesh,ctx,pass,flag,cache);return;}
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
        ID3D11Buffer* palette=nullptr;context->VSGetConstantBuffers(1,1,&palette);
        fpskin::Mask mask{};
        __try { mask=BodyMask(palette); } __except(EXCEPTION_EXECUTE_HANDLER){}
        if(palette)palette->Release();
        fpskin::Begin(mask);
        fpskin::Submit(context,count,0,0);
    }
    if(cache) {
        Put<void*>(cache,0,passes==1 ? material : nullptr);
        Put<std::int64_t>(cache,8,passes==1 ? technique : -1);
    }
}

void* nextLaserDraw=nullptr;
void LaserDrawSelected(void* object,void* ctx,int pass,void* view) {
    auto draw=static_cast<unsigned char*>(object);
    float savedStart[4]{};bool moved=false;
    __try {
        // object is the attachment's umbra base (+0x10); attachment+8 owns the weapon. The render-only endpoints
        // are object+0x120/+0x130. The world raycast's origin, direction and hit point remain untouched.
        const auto weapon=At<const unsigned char*>(draw-0x10,8);
        const int hand=Current().on && !Current().riding && weapon ? HandOfModel(weapon+kWeaponModel) : -1;
        if(hand>=0 && Current().viewOk[hand]) {
            std::memcpy(savedStart,draw+0x120,16);
            float from[3];fpview::Point(Current().view[hand],savedStart,from);
            std::memcpy(draw+0x120,from,12);moved=true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    reinterpret_cast<DrawFn>(nextLaserDraw)(object,ctx,pass,view);
    if(moved)std::memcpy(draw+0x120,savedStart,16);
}

void __fastcall LaserDrawHook(void* object,void* ctx,int pass,void* view) {
    if(pass==3 || pass==4 || pass>5){reinterpret_cast<DrawFn>(nextLaserDraw)(object,ctx,pass,view);return;}
    State* state=ViewState(view);
    if(!state){reinterpret_cast<DrawFn>(nextLaserDraw)(object,ctx,pass,view);return;}
    StateScope scope(state);LaserDrawSelected(object,ctx,pass,view);
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

void ResetFirstPerson() noexcept { session.fetch_add(1,std::memory_order_release); }

bool FirstPersonOn() noexcept { for(const auto& state:states)if(state.on)return true;return false; }
void FirstPersonInstrumentReadings(const instruments::Readings& readings,const unsigned char* human) noexcept {
    for(auto& state:states)if(state.on && state.riding && state.human==human){state.instruments=readings;state.instrumentsReady=true;}
}
bool FirstPersonGunnerSight() noexcept {
    const Config& c=Cfg();
    return camOk && drawOk && c.enabled && c.firstPerson && c.fpVehicle && toggled;
}
bool FirstPersonHidesStockHud(const void* camera) noexcept {
    const auto state=CameraState(camera);
    return state && state->on && state->riding && state->screens.n>0 && Cfg().fpHideStockHud;
}
bool FirstPersonScreens(CockpitScreens* out,const unsigned char* human) noexcept {
    for(const auto& state:states)if(state.on && state.riding && state.screens.n && state.human==human){*out=state.screens;return true;}
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
