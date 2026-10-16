// First person on foot (README 第一人称; plan: the user, 2026-10-12). RE: docs/fp-render-re.md (the draw, its passes,
// the draw pose), docs/fp-soldier-re.md (the aim frame, the aim the network carries). Two hooks, both this machine's
// picture only (nothing here changes what the body does, where shots start or what the network sends):
//  - The camera (H layout, camera-re.md §3): CharacterGhostCamera's per-frame slot 4 (0xF86A0) is chained; after it the
//    eye row (+0x250) is moved to the local player's head, the rotation left as the stock step built it (the soldier's
//    aim frame +0x60, whose pitch and yaw the third-person camera turns with). The eye is the head bone's place in the
//    yaw-only body frame (+0x5E0), eased, so the animation's bob does not shake the view. The stock matrix is put back
//    before the next step (its easing reads it), as map.cpp does. The map and the TV chain outside this hook: their
//    views win while they are open and they ease from this one.
//  - The draw (H, fp-render-re.md §1-2): AnimationModel's draw (vtable slot 1, 0x6BF9F0) runs on the game thread and
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
using RenderFn=void(__fastcall*)(void*,std::uint8_t,std::uint8_t);
// The main view's opaque pass: each object submitted by 0x11E1630(system, object, 1); the pass ended by
// 0x11E1590(system, 1), called at 0x11E2572.
constexpr unsigned kSubmit=0x11E1630,kPassEnd=0x11E1590,kPassEndCall=0x11E2572;
using SubmitFn=void(__fastcall*)(void*,void*,int);
using PassEndFn=void(__fastcall*)(void*,int);
ULONGLONG renderFrame=0;               // the renders started
struct Drawn1 { const void* model; ULONGLONG frame; };
Drawn1 drawn1[kMostSets+1]{};          // the viewmodel's models and the render they were last drawn in pass 1 by
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
// The arms (the four classes share the names, fp-render-re.md §8): the upper arm, the forearm and their twists, the
// hand, its fingers, the weapon mounts and shoulder pad. The shoulder pad follows the upper arm; collapsing it
// separately from its sleeve stretches the shared shoulder vertices. The arm ends at
// the shoulder, which FitArm turns toward a place beside and below the eye (the third-person arm moved with its weapon
// to the bottom of the view reached back up through the middle of it). Ending it at the elbow (the user, 2026-10-13, with a screenshot: "the arm
// half shown, clearly hollow") left the forearm's open sleeve in the middle of the view: collapsing the rest onto the
// eye did not reliably hide the mixed shoulder triangles. The colour draw now excludes those faces; the arm's
// open end must still lie outside the view.
const wchar_t* const kArmBones[]={L"kata",L"jowan",L"kawan",L"te_",L"fing",L"ring",L"little",L"index",L"middle",
                                  L"thumb",L"arm"};

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

// --- What the camera step leaves for the draws (game thread only: the draw and the camera step both run on it) ---
struct State {
    bool on;                    // the local player's camera is in first person
    unsigned char* human;       // that player
    unsigned char* cam;         // its camera
    const void* body;           // its body model
    const void* weapons[kMostWeapons];
    int weaponCount;
    const void* inHand[kMostSets];   // the weapons in hand (each set's current one): their models
    int handOf[kMostSets];           // ...and the hand each is in (kRight, kLeft)
    int inHandCount;
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
    bool shown[2];              // visible arm ownership for the colour topology mask (neither while riding)
    bool axisOk[2];             // a point on the axis of the weapon each hand holds, after T (the weapon's root)
    float axis[2][3];
    bool onOther[2];            // a free hand drawn on the other hand's weapon, whose bounds after T are otherBox
    M4 otherBox[2];
    bool riding;                // in a vehicle: the eye at the seat's cockpit, the rider not drawn (Ride)
    RideView ride;
    CockpitScreens screens;     // ...and its screens round the eye (VehicleScreens), for the HUD
    instruments::Readings instruments;
    bool instrumentsReady;
};
// The weapon models taken out of their batch, with the batch to give back.
struct Unbatched { const void* model; void* batch; };
Unbatched unbatched[kMostUnbatched]{};
State st{};
// The flashes' matrices: each the shot matrix `src` through T, kept for kFlashFrames camera steps.
struct alignas(16) Flash { float m[16]; const float* src; int age,hand; };
Flash flashes[kFlashSlots]{};
int flashNext=0;
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
    if(h[kRagdoll])return "down";
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
    st.body=h+kBodyModel;
    st.weaponCount=0;
    const auto list=At<unsigned char* const*>(h,kWeapons);
    const auto n=At<std::uint64_t>(h,kWeaponCount);
    if(!list || n>kMostWeapons)return;
    for(std::uint64_t i=0;i<n;++i) {
        if(!list[i])continue;
        const unsigned char* model=list[i]+kWeaponModel;
        st.weapons[st.weaponCount++]=model;
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
    const std::int32_t d=shots-st.shots[k];
    int fired=d>0 && d<10 ? d : 0;
    st.sampledShots+=fired;
    float recoil=RecoilOf(w);
    const int hand=HandOfModel(w+kWeaponModel);
    if(hand>=0) {
        auto& event=st.shotEvent[hand];
        if(event.weapon==w){st.notifiedShots+=event.count;fired=(std::max)(fired,event.count);recoil=std::fmax(recoil,event.recoil);event=State::ShotEvent{};}
    }
    if(fired){c.shots+=fired;c.recoil=std::fmax(c.recoil,recoil);}
    if(fire!=3 || st.fire[k]==3)return;
    const std::int32_t t=At<std::int32_t>(w,kReloadTime);
    st.reloadLeft=t>0 ? (t<kMostReloadFrames ? t : kMostReloadFrames) : 0;
    if(Cfg().debug)Log("FP reload %d frames (FireRecoil %.3f)",t,At<float>(w,kFireRecoil));
}

// The hands' weapons this frame: shots, a reload started, a switch.
void Weapons(const unsigned char* h,fpmotion::Cue& c) noexcept {
    st.sampledShots=st.notifiedShots=0;
    auto n=At<std::uint64_t>(h,kSetCount);
    if(n>kMostSets)n=kMostSets;
    for(std::uint64_t k=0;k<n;++k) {
        const unsigned char* w=SetWeapon(h,k);
        const std::int32_t shots=w ? At<std::int32_t>(w,kShotCount) : 0,fire=w ? At<std::int32_t>(w,kFireState) : 0;
        if(st.have && w!=st.held[k] && st.held[k]){c.switched=true;st.reloadLeft=0;}
        if(st.have && w && w==st.held[k])Hand(w,k,shots,fire,c);
        st.held[k]=w;st.shots[k]=shots;st.fire[k]=fire;
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
    if(st.have) {
        c.lookYaw=Wrap(yaw-st.yaw);
        c.lookPitch=-(pitch-st.pitch);
        const float dx=pos[0]-st.pos[0],dz=pos[2]-st.pos[2];
        c.speed=std::sqrt(dx*dx+dz*dz);
        if(c.speed>3.0f)c.speed=0.0f;   // a warp, not a walk
        if(ground>0 && st.groundFrames==0 && st.vy<0.0f)c.fallSpeed=-st.vy;
        st.vy=pos[1]-st.pos[1];
        if(act!=st.act && Cfg().debug)
            Log("FP act 0x%X -> 0x%X (state %p / %p)",st.act,act,At<const void*>(h,0x1F00),At<const void*>(h,0x2458));
    }
    Weapons(h,c);
    c.reloading=st.reloadLeft>0;
    if(st.reloadLeft>0)--st.reloadLeft;
    c.move=MoveOf(act);
    st.pitch=pitch;st.yaw=yaw;std::memcpy(st.pos,pos,12);st.groundFrames=ground;st.act=act;st.have=true;
    return c;
}

// The motion this frame about each hand's pivot (right is -x in the camera's frame: x, yaw and roll mirrored).
void Motion(const unsigned char* h,const float (*pivot)[3]) noexcept {
    const auto cue=Cues(h);
    const fpmotion::Pose p=fpmotion::Advance(st.motion,cue);
    if(Cfg().debug && cue.shots)Log("FP recoil: shots %d (counter %d events %d), strength %.3f, back %.4f pitch %.4f",
                                  cue.shots,st.sampledShots,st.notifiedShots,cue.recoil,p.z,p.pitch);
    for(int hand=kRight;hand<=kLeft;++hand)st.moved[hand]=fpview::MotionMatrix(-p.x,p.y,p.z,p.pitch,-p.yaw,-p.roll,pivot[hand]);
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
    const float* at=reinterpret_cast<const float*>(st.human+kPos);
    const float d[3]={box.m[12]-at[0],box.m[13]-at[1],box.m[14]-at[2]};
    return d[0]*d[0]+d[1]*d[1]+d[2]*d[2]<100.0f*100.0f;
}

void Place(const void* model,const float* eye) noexcept {
    if(!placeOk || !Cfg().fpCulling || !model || !st.human)return;
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
    const float* eye=real ? nullptr : reinterpret_cast<const float*>(st.cam+kCamEyeRow);
    Place(st.body,eye);
    for(int i=0;i<st.inHandCount;++i)Place(st.inHand[i],eye);
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
    for(int i=0;i<st.weaponCount;++i)if(st.weapons[i]==model)return true;
    return false;
}

// The batches back: of the weapons no longer in hand (all: `all`), into those still the player's.
void Rebatch(bool all) noexcept {
    for(Unbatched& u:unbatched) {
        if(!u.model || (!all && HandOfModel(u.model)>=0))continue;
        if(StillPlayers(u.model) && !At<void*>(u.model,kBatch))Put<void*>(const_cast<void*>(u.model),kBatch,u.batch);
        u=Unbatched{};
    }
}

// The weapons in hand out of their batches (kept).
void Unbatch() noexcept {
    for(int i=0;i<st.inHandCount;++i) {
        void* const batch=At<void*>(st.inHand[i],kBatch);
        if(!batch)continue;
        for(Unbatched& u:unbatched) {
            if(u.model)continue;
            u=Unbatched{st.inHand[i],batch};
            Put<void*>(const_cast<void*>(st.inHand[i]),kBatch,nullptr);
            if(Cfg().debug)Log("FP weapon model %p out of its batch %p",st.inHand[i],batch);
            break;
        }
    }
}

// The weapons in hand and, for each hand, the root of its weapon as it will be drawn (the draw pose's bone 0:
// 0x1100B90 writes the weapon's matrix to its first bone record, 0x1100F10 copies it into the draw pose).
void Hands(const unsigned char* h,M4* anchor,bool* anchored) noexcept {
    anchored[kRight]=anchored[kLeft]=false;
    st.inHandCount=0;
    auto n=At<std::uint64_t>(h,kSetCount);
    if(n>kMostSets)n=kMostSets;
    for(std::uint64_t k=0;k<n;++k) {
        const unsigned char* w=SetWeapon(h,k);
        if(!w)continue;
        const int hand=HandOf(w);
        st.inHand[st.inHandCount]=w+kWeaponModel;st.handOf[st.inHandCount++]=hand;
        LogWeapon(w,hand);
        if(!anchored[hand])anchored[hand]=RootPose(w+kWeaponModel,&anchor[hand]);
    }
}

// The hand a weapon model is in, -1: not in hand.
int HandOfModel(const void* model) noexcept {
    for(int i=0;i<st.inHandCount;++i)if(st.inHand[i]==model)return st.handOf[i];
    return -1;
}

// The flashes handed out lately, moved with the viewmodel (their shot matrix through this frame's T).
void Flashes() noexcept {
    for(Flash& f:flashes) {
        if(!f.src || f.age>=kFlashFrames){f.src=nullptr;continue;}
        ++f.age;
        if(st.viewOk[f.hand])std::memcpy(f.m,fpview::Mul(MatrixAt(reinterpret_cast<const unsigned char*>(f.src)),st.view[f.hand]).m,64);
    }
}

// A shot's flash about to be made for weapon `w`: in first person, its matrix pointer is one of the flashes'.
void Relocate(unsigned char* w) noexcept {
    const int hand=st.on ? HandOfModel(w+kWeaponModel) : -1;
    if(hand<0 || !st.viewOk[hand])return;
    auto& event=st.shotEvent[hand];
    if(event.weapon!=w)event=State::ShotEvent{w,0,0};
    if(event.count<3)++event.count;
    event.recoil=RecoilOf(w);
    const auto src=At<const float*>(w,kFlashMatrix);
    if(!src)return;
    Flash& f=flashes[flashNext];
    flashNext=(flashNext+1)%kFlashSlots;
    f.src=src;f.age=0;f.hand=hand;
    std::memcpy(f.m,fpview::Mul(MatrixAt(reinterpret_cast<const unsigned char*>(src)),st.view[hand]).m,64);
    Put<const float*>(w,kFlashMatrix,f.m);
}

// The start of a line `start` (x, y, z, w) moved through T into `out` when it is the player's in first person.
bool MovedStart(const float* start,float* out) noexcept {
    const int hand=st.viewOk[kRight] ? kRight : kLeft;
    if(!st.on || !st.viewOk[hand] || !st.human)return false;
    const float* at=reinterpret_cast<const float*>(st.human+kPos);
    const float d[3]={start[0]-at[0],start[1]-at[1]-kChestUp,start[2]-at[2]};
    if(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>kAimLineReach*kAimLineReach)return false;
    fpview::Point(st.view[hand],start,out);
    out[3]=start[3];
    return true;
}

using LineFn=void*(__fastcall*)(void*,const float*,const float*,const float*);
void* __fastcall AimLineHook(void* line,const float* start,const float* velocity,const float* gravity) {
    alignas(16) float moved[4];
    const float* from=start;
    __try { if(MovedStart(start,moved))from=moved; } __except(EXCEPTION_EXECUTE_HANDLER){from=start;}
    return reinterpret_cast<LineFn>(image+kAimLineBuild)(line,from,velocity,gravity);
}

void* __fastcall FlashCreateHook(void* factory,void* a,void* cls,void* param) {
    __try { Relocate(static_cast<unsigned char*>(param)-kFlashParam); } __except(EXCEPTION_EXECUTE_HANDLER){}
    return reinterpret_cast<CreateFn>(image+kEffectCreate)(factory,a,cls,param);
}

// Debug=1: what the viewmodel is built from, every 5 s (every half second through a reload).
void Probe(const bool* anchored,const M4* anchor) noexcept {
    static ULONGLONG at=0;
    const ULONGLONG now=GetTickCount64();
    if(!Cfg().debug || now-at<(st.reloadLeft>0 ? 500u : 5000u))return;
    at=now;
    const float* eye=reinterpret_cast<const float*>(st.cam+kCamEyeRow);
    for(int hand=kRight;hand<=kLeft;++hand) {
        const M4& a=anchor[hand];
        Log("FP probe %s hand: anchored=%d view=%d | root (%.2f,%.2f,%.2f) from the eye | in hand %d, reload %d, act 0x%X",
            hand==kRight ? "right" : "left",anchored[hand],st.viewOk[hand],a.m[12]-eye[0],a.m[13]-eye[1],a.m[14]-eye[2],
            st.inHandCount,st.reloadLeft,st.act);
    }
}

// The camera, the player's state given back: the near clip, the look smoothing.
void Leave() noexcept {
    if(st.on){PlaceAll(true);Rebatch(true);}
    fpskin::Clear();
    if(st.cam && st.nearSet)Put<float>(st.cam,kCamNear,st.savedNear);
    if(st.human && st.smoothSet)Put<float>(st.human,kLookSmooth,st.savedSmooth);
    if(st.on)Log("FP view back to the stock camera");
    st=State{};
}

// The weapon in `hand` (nullptr: none) and whether it hangs on a flipped mount (armsFlip_*: the Fencer's shields).
const unsigned char* HandWeapon(int hand,bool* flip) noexcept {
    *flip=false;
    for(int i=0;i<st.inHandCount;++i) {
        if(st.handOf[i]!=hand)continue;
        const unsigned char* w=static_cast<const unsigned char*>(st.inHand[i])-kWeaponModel;
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

// A bulky or long weapon held further off (measured boxes, half sizes right x up x ahead: the Air Raider's limpet gun
// 0.28 x 0.17 x 0.46 filled half the view at the rifle's grip; the Mag Blaster 0.03 x 0.12 x 0.50 did not), a shield
// out to the side. `grip` in the camera's frame (x to the left); `dual`: both hands hold one (their grips are already
// set for the Fencer's big weapons: only a shield is moved).
void Fit(int hand,bool dual,float* grip) noexcept {
    bool flip=false;
    const unsigned char* w=HandWeapon(hand,&flip);
    if(!w)return;
    const M4 box=MatrixAt(w+kWeaponModel+kModelBounds);
    float half[3];
    for(int r=0;r<3;++r)half[r]=std::sqrt(box.m[r*4]*box.m[r*4]+box.m[r*4+1]*box.m[r*4+1]+box.m[r*4+2]*box.m[r*4+2]);
    if(!std::isfinite(half[0]+half[1]+half[2]) || half[0]>3.0f || half[1]>3.0f || half[2]>3.0f)return;
    const float out=hand==kRight ? -1.0f : 1.0f;   // away from the middle, in the camera's x
    float x=0.0f,y=0.0f,z=0.0f;
    if(!dual) {
        const float girth=std::fmax(half[0],half[1]);
        z=0.8f*std::fmax(0.0f,girth-0.12f)+0.3f*std::fmax(0.0f,half[2]-0.5f);
        x=0.6f*std::fmax(0.0f,half[0]-0.08f);
        y=-0.4f*std::fmax(0.0f,half[1]-0.12f);
    }
    if(flip){x+=0.22f;z+=0.15f;}
    grip[0]+=out*x;grip[1]+=y;grip[2]+=z;
}

// Each hand's T this frame (above: its own weapon's anchor, else the other hand's T, else the aim frame).
void Views(const unsigned char* h,const unsigned char* cam) noexcept {
    const Config& c=Cfg();
    M4 anchor[2]={fpview::Identity(),fpview::Identity()};
    bool anchored[2];
    Hands(h,anchor,anchored);
    const bool both=anchored[kRight] && anchored[kLeft];
    const float gx=fpview::CenteredGrip(both ? c.fpDualGripX : c.fpGripX,c.fpCenter),gy=both ? c.fpDualGripY : c.fpGripY,gz=both ? c.fpDualGripZ : c.fpGripZ;
    float grip[2][3]={{-gx,gy,gz},{gx,gy,gz}};   // right is -x
    if(c.fpFit)for(int hand=kRight;hand<=kLeft;++hand)Fit(hand,both,grip[hand]);
    const float hold[3]={-c.fpHoldX,c.fpHoldY,c.fpHoldZ};
    const float pivots[2][3]={{anchored[kRight] ? grip[kRight][0] : kPivot[0],anchored[kRight] ? gy : kPivot[1],anchored[kRight] ? gz : kPivot[2]},
                              {anchored[kLeft] ? grip[kLeft][0] : -kPivot[0],anchored[kLeft] ? gy : kPivot[1],anchored[kLeft] ? gz : kPivot[2]}};
    if(c.fpMotion)Motion(h,pivots);
    else st.moved[kRight]=st.moved[kLeft]=fpview::Identity();
    const M4 view=MatrixAt(cam+kCamMatrix);
    for(int hand=kRight;hand<=kLeft;++hand) {
        bool flip=false;
        const bool native=NativeGrip(HandWeapon(hand,&flip));
        st.viewOk[hand]=anchored[hand] && (native
            ? fpview::NativeGripTransform(anchor[hand],MatrixAt(h+kAim),view,grip[hand],c.fpScale,&st.view[hand],st.moved[hand])
            : fpview::AnchoredTransform(anchor[hand],view,grip[hand],c.fpScale,&st.view[hand],st.moved[hand]));
        st.axisOk[hand]=st.viewOk[hand];
        if(st.axisOk[hand])fpview::Point(st.view[hand],anchor[hand].m+12,st.axis[hand]);
    }
    for(int hand=kRight;hand<=kLeft;++hand) {
        const int other=1-hand;
        st.shown[hand]=true;
        st.onOther[hand]=false;
        if(st.viewOk[hand])continue;
        if(st.viewOk[other]) {
            st.view[hand]=st.view[other];st.viewOk[hand]=true;
            // Keep the native support-hand/reload animation; proximity is not a visibility switch.
            st.onOther[hand]=true;
            st.axisOk[hand]=true;
            for(int i=0;i<3;++i)st.axis[hand][i]=st.axis[other][i];
            continue;
        }
        st.viewOk[hand]=fpview::ViewTransform(MatrixAt(h+kAim),view,hold,c.fpScale,&st.view[hand],st.moved[kRight]);
    }
    Probe(anchored,anchor);
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
    if(!st.riding || st.ride.vehicle!=ride.vehicle || st.ride.seat!=ride.seat) {
        st.instrumentsReady=false;
        st.have=false;st.reloadLeft=0;st.motion=fpmotion::State{};
        for(auto& event:st.shotEvent)event=State::ShotEvent{};
        Log("FP in a vehicle: %s %p seat %d, eye at its cockpit (%.2f %.2f %.2f)",ride.name ? ride.name : "?",
            static_cast<void*>(ride.vehicle),ride.seat,eye[0],eye[1],eye[2]);
    }
    st.riding=true;st.ride=ride;
    HookDrawOf(At<void**>(ride.vehicle+0xE40,0));
    // The eye marker is also the seat's frame: cockpit, eye and horizon must roll/pitch together.
    std::memcpy(cam+kCamMatrix,ride.frame,64);
    VehicleLookCamera(h,reinterpret_cast<float*>(cam+kCamMatrix));
    VehicleScreens(ride.vehicle,ride.seat,eye,&st.screens);
    if(st.nearSet){Put<float>(cam,kCamNear,st.savedNear);st.nearSet=false;}
    if(st.smoothSet){Put<float>(h,kLookSmooth,st.savedSmooth);st.smoothSet=false;}
    st.body=h+kBodyModel;st.weaponCount=0;st.inHandCount=0;
    for(int hand=kRight;hand<=kLeft;++hand) {
        st.view[hand]=fpview::Identity();st.viewOk[hand]=true;st.shown[hand]=false;st.axisOk[hand]=false;st.onOther[hand]=false;
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
    if(!Wanted(cam,h)){if(st.on && st.cam==cam)Leave();if(!st.on)Watch(h);return;}
    watched=nullptr;
    if(!st.on || st.cam!=cam || st.human!=h) {
        Leave();
        st.on=true;st.cam=cam;st.human=h;st.fresh=true;st.moved[kRight]=st.moved[kLeft]=fpview::Identity();
        Log("FP view on %p (player %p)",cam,h);
    }
    std::memcpy(st.stock,cam+kCamMatrix,sizeof(st.stock));
    if(!HumanOnFoot(h)) {
        if(!Ride(cam,h))Leave();
        return;
    }
    if(st.riding){st.riding=false;st.fresh=true;Log("FP on foot again");}
    const unsigned char* head=BoneNamed(h+kBodyInst,L"head");
    const M4 body=MatrixAt(h+kBodyFrame);
    float headAt[3];
    if(head)std::memcpy(headAt,head+kBoneWorld+48,12);
    else {const float up[3]={0.0f,1.7f,0.0f};fpview::EyeWorld(body,up,headAt);}
    fpview::EaseEye(body,headAt,c.fpEyeUp,c.fpEyeAhead,c.fpViewHeightEase,st.fresh,st.eyeLocal);
    st.fresh=false;
    float eye[3];
    fpview::EyeWorld(body,st.eyeLocal,eye);
    std::memcpy(cam+kCamEyeRow,eye,12);
    if(c.fpNear>0.0f){if(!st.nearSet){st.savedNear=At<float>(cam,kCamNear);st.nearSet=true;}Put<float>(cam,kCamNear,c.fpNear);}
    if(c.fpAimSnap){if(!st.smoothSet){st.savedSmooth=At<float>(h,kLookSmooth);st.smoothSet=true;}Put<float>(h,kLookSmooth,1.0f);}
    Gather(h);
    Views(h,cam);
    Rebatch(false);
    Unbatch();
    Flashes();
}

// The render about to start: the viewmodel's models placed around the eye for Umbra (above).
void __fastcall RenderHook(void* system,std::uint8_t a,std::uint8_t b) {
    ++renderFrame;
    __try { if(st.on)PlaceAll(false); } __except(EXCEPTION_EXECUTE_HANDLER){}
    reinterpret_cast<RenderFn>(image+kRender)(system,a,b);
}

// A model of the viewmodel drawn in the opaque pass this render.
void Drew1(const void* model) noexcept {
    Drawn1* free=nullptr;
    for(Drawn1& d:drawn1) {
        if(d.model==model){d.frame=renderFrame;return;}
        if(!free && d.frame!=renderFrame)free=&d;
    }
    if(free)*free=Drawn1{model,renderFrame};
}

bool Drew1Now(const void* model) noexcept {
    for(const Drawn1& d:drawn1)if(d.model==model && d.frame==renderFrame)return true;
    return false;
}

// The opaque pass's end: what of the viewmodel it left out, submitted.
void __fastcall PassEndHook(void* system,int pass) {
    __try {
        if(st.on && pass==1) {
            const void* models[kMostSets+1];
            int n=0;
            models[n++]=st.body;
            for(int i=0;i<st.inHandCount && n<kMostSets+1;++i)models[n++]=st.inHand[i];
            for(int i=0;i<n;++i)
                if(models[i] && At<const void*>(models[i],kUmbraObject) && !Drew1Now(models[i]))
                    reinterpret_cast<SubmitFn>(image+kSubmit)(system,const_cast<void*>(models[i]),1);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    reinterpret_cast<PassEndFn>(image+kPassEnd)(system,pass);
}

struct LookApplied { void* cam=nullptr;float before[16]{},written[16]{}; } lookApplied;
void __fastcall CamStepHook(void* camera,void* step) {
    auto cam=static_cast<unsigned char*>(camera);
    if(lookApplied.cam==cam && std::memcmp(cam+kCamMatrix,lookApplied.written,64)==0)
        std::memcpy(cam+kCamMatrix,lookApplied.before,64);
    if(lookApplied.cam==cam)lookApplied=LookApplied{};
    if(st.on && st.cam==cam)std::memcpy(cam+kCamMatrix,st.stock,sizeof(st.stock));
    nextCamStep(camera,step);
    __try {
        Camera(cam);
        if(!st.on || st.cam!=cam || !st.riding) {
            LookApplied record;record.cam=cam;std::memcpy(record.before,cam+kCamMatrix,64);
            if(VehicleLookCamera(CameraPlayer(cam),reinterpret_cast<float*>(cam+kCamMatrix))) {
                std::memcpy(record.written,cam+kCamMatrix,64);lookApplied=record;
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER){st=State{};}
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
    if(!st.on)return Part::none;
    if(st.riding && model==st.ride.vehicle+0xE40)return Part::instruments;
    if(model==st.body)return Part::body;
    if((*hand=HandOfModel(model))>=0)return Part::inHand;
    *hand=kRight;
    for(int i=0;i<st.weaponCount;++i)if(st.weapons[i]==model)return Part::stowed;
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
ArmFit FitArm(const unsigned char* inst,const float* pose,const M4& t,int hand) noexcept {
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
    const float* cam=reinterpret_cast<const float*>(st.cam+kCamMatrix);
    float shoulder[3],pole[3];
    for(int i=0;i<3;++i) {
        const float right=-cam[i],up=cam[4+i],ahead=cam[8+i];
        shoulder[i]=cam[12+i]+scale*(out*half*right+kShoulderDown*up+kShoulderBack*ahead);
        pole[i]=out*kElbowOut*right+kElbowDown*up;
    }
    fit.hand=fpview::Identity(); // The palm and fingers keep the animated contact; never snap to a bounds box.
    fit.ok=fpview::ArmChain(s0,e0,h0,shoulder,pole,&fit.upper,&fit.fore);
    return fit;
}

// Arm/weapon poses through T, restored after the draw. Non-arm body bones stay native: fp_skin_gpu submits only
// triangles owned by one visible arm, excluding mixed torso/shoulder and opposite-arm faces instead of stretching
// their skinning weights. `t` is each hand's transform (a weapon uses its own hand's twice).
void Pose(unsigned char* inst,const M4* t,bool body,const float*,float* backup) noexcept {
    const auto n=At<std::int32_t>(inst,kInstCount);
    auto pose=At<float*>(inst,kInstPose);
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    if(!pose || !bones || n<=0 || n>kMostBones)return;
    std::memcpy(backup,pose,static_cast<std::size_t>(n)*64);
    ArmFit fit[2]{};
    if(body && Cfg().fpArmIk && !st.riding)for(int hand=kRight;hand<=kLeft;++hand)fit[hand]=FitArm(inst,backup,t[hand],hand);
    for(std::int32_t i=0;i<n;++i) {
        float* p=pose+i*16;
        const wchar_t* name=At<const wchar_t*>(bones+static_cast<std::size_t>(i)*kBoneStride,0);
        const std::size_t len=name ? std::wcslen(name) : 0;
        const bool left=body && len>=2 && name[len-2]==L'_' && name[len-1]==L'l';
        if(body && (!ArmBone(name) || !st.shown[left ? kLeft : kRight])) {
            // The colour draw uses an arm-only index buffer. Leave body bones intact instead of collapsing mixed
            // shoulder triangles toward the camera (which made long sheets during the sprint).
            continue;
        }
        const int hand=left ? kLeft : kRight;
        M4 moved=fpview::Mul(MatrixAt(reinterpret_cast<const unsigned char*>(p)),t[hand]);
        if(body && fit[hand].ok && name)   // the arm posed for the first person: the upper arm, the forearm, the hand
            moved=fpview::Mul(moved,(std::wcsncmp(name,L"jowan",5)==0 || std::wcsncmp(name,L"kata",4)==0) ? fit[hand].upper
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
    if(!palette || !st.body)return mask;
    unsigned char* insts[kMostInsts];
    const int instances=InstancesOf(const_cast<unsigned char*>(static_cast<const unsigned char*>(st.body)),insts);
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
            mask.arm[i]=ArmBone(name) && st.shown[left ? kLeft : kRight];
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
                const M4 moved=instruments::ShellPose(original,mask,st.ride.seat);
                std::memcpy(pose+i*16,moved.m,64);
            }
            continue;
        }
        if(!name || std::wcsncmp(name,L"vc_i_",5)!=0)continue;
        int seat=-1,screen=-1,field=-1,place=0,segment=0;wchar_t mode=0;
        const int fields=swscanf_s(name,L"vc_i_%d_%d_%lc_%d_%d_%d",&seat,&screen,&mode,1u,&field,&place,&segment);
        if(fields<4 || ((mode==L'd' || mode==L'D' || mode==L'p') && fields!=6))continue;
        const bool mine=seat==st.ride.seat;
        const bool rangedNeedle=mode==L'n' && fields>=5;
        const bool neutralWidget=mode==L'D' || mode==L'B' || mode==L'T';
        // Adjacent stations visible from this seat use the same language, while their
        // new health/percent widgets remain neutral without that seat's live data.
        if(!mine && mode!=L'l' && mode!=L'c' && mode!=L'p' && mode!=L'v' && mode!=L'u'
            && mode!=L'r' && mode!=L'q' && mode!=L'a' && !rangedNeedle && !neutralWidget)continue;
        const bool ready=st.instrumentsReady && (mine || (mode==L'l' && place==0));
        const M4 original=MatrixAt(reinterpret_cast<const unsigned char*>(pose+i*16));
        const M4 moved=instruments::Pose(original,mode,field,place,segment,st.instruments,ready);
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
                     passLog[1][0],passLog[1][1],passLog[1][2],passLog[1][3],passLog[1][5],st.weaponCount);
    std::memset(passLog,0,sizeof(passLog));passLogAt=now;
}

// Debug=1: a model not the player's drawn (colour pass) where the weapon in the right hand really is: what it is.
void NearWeapon(const void* model,int pass) noexcept {
    static const void* logged[8]{};
    static int next=0;
    if(!Cfg().debug || pass!=1 || !st.inHandCount)return;
    M4 root,gun;
    if(!RootPose(static_cast<const unsigned char*>(model),&root) || !RootPose(static_cast<const unsigned char*>(st.inHand[0]),&gun))return;
    const float d[3]={root.m[12]-gun.m[12],root.m[13]-gun.m[13],root.m[14]-gun.m[14]};
    if(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>0.3f*0.3f)return;
    for(const void* l:logged)if(l==model)return;
    logged[next]=model;next=(next+1)%8;
    const auto weapon=static_cast<const unsigned char*>(st.inHand[0])-kWeaponModel;
    const long long off=static_cast<const unsigned char*>(model)-weapon;
    Log("FP a model %p drawn at the weapon %p's place: vtable +0x%llX, weapon%+lld",model,static_cast<const void*>(weapon),
        static_cast<unsigned long long>(At<const unsigned char*>(model,0)-image),off);
}

void __fastcall DrawHook(void* model,void* ctx,int pass,void* view) {
    const DrawFn next=NextDraw(model);
    Part part=Part::none;
    int hand=kRight;
    __try { part=PartOf(model,&hand); } __except(EXCEPTION_EXECUTE_HANDLER){part=Part::none;}
    if(part!=Part::none)LogPass(part,pass);
    else if(!st.on && model==watched)LogPass(Part::inHand,pass);
    else if(st.on){__try { NearWeapon(model,pass); } __except(EXCEPTION_EXECUTE_HANDLER){}}
    if(part==Part::none || pass==3 || pass==4 || pass>5){next(model,ctx,pass,view);return;}
    if((pass==0 && part!=Part::instruments) || part==Part::stowed)return;
    if(pass==1 && part!=Part::instruments)Drew1(model);
    unsigned char* insts[kMostInsts];
    int n=0;
    __try {
        if(part==Part::instruments || (st.viewOk[kRight] && st.viewOk[kLeft])) {
            const M4 t[2]={part==Part::body ? st.view[kRight] : st.view[hand],part==Part::body ? st.view[kLeft] : st.view[hand]};
            n=InstancesOf(static_cast<unsigned char*>(model),insts);
            const float* eye=reinterpret_cast<const float*>(st.cam+kCamEyeRow);   // the drawn view's eye
            for(int i=0;i<n;++i) {
                if(part==Part::instruments)InstrumentPose(insts[i],saved[i]);
                else Pose(insts[i],t,part==Part::body,eye,saved[i]);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){for(int i=0;i<n;++i)Unpose(insts[i],saved[i]);n=0;}
    if(part==Part::body)fpskin::Begin(fpskin::Mask{}); // the mesh draw selects its actual LOD palette
    next(model,ctx,pass,view);
    if(part==Part::body)fpskin::End();
    __try { for(int i=0;i<n;++i)Unpose(insts[i],saved[i]); } __except(EXCEPTION_EXECUTE_HANDLER){}
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
void __fastcall LaserDrawHook(void* object,void* ctx,int pass,void* view) {
    auto draw=static_cast<unsigned char*>(object);
    float savedStart[4]{};bool moved=false;
    __try {
        // object is the attachment's umbra base (+0x10); attachment+8 owns the weapon. The render-only endpoints
        // are object+0x120/+0x130. The world raycast's origin, direction and hit point remain untouched.
        const auto weapon=At<const unsigned char*>(draw-0x10,8);
        const int hand=st.on && !st.riding && weapon ? HandOfModel(weapon+kWeaponModel) : -1;
        if(hand>=0 && st.viewOk[hand]) {
            std::memcpy(savedStart,draw+0x120,16);
            float from[3];fpview::Point(st.view[hand],savedStart,from);
            std::memcpy(draw+0x120,from,12);moved=true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    reinterpret_cast<DrawFn>(nextLaserDraw)(object,ctx,pass,view);
    if(moved)std::memcpy(draw+0x120,savedStart,16);
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

bool FirstPersonOn() noexcept { return st.on; }
void FirstPersonInstrumentReadings(const instruments::Readings& readings) noexcept {
    if(st.on && st.riding){st.instruments=readings;st.instrumentsReady=true;}
}
bool FirstPersonGunnerSight() noexcept {
    const Config& c=Cfg();
    return camOk && drawOk && c.enabled && c.firstPerson && c.fpVehicle && toggled;
}
bool FirstPersonHidesStockHud(const void* camera) noexcept {
    return st.on && st.riding && st.cam==camera && st.screens.n>0 && Cfg().fpHideStockHud;
}
bool FirstPersonScreens(CockpitScreens* out) noexcept {
    if(!st.on || !st.riding || !st.screens.n)return false;
    *out=st.screens;
    return true;
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
