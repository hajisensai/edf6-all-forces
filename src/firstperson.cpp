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
//    camera), and the body's bones that are not the arms' collapse onto their own joint, so only the arms draw. The
//    shadow pass (3) draws them as they are (the body's shadow stays) and the occluder depth pass (0) skips them. The
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
//    and the real boxes back on the way out.
//  - Two hands (measured: the Fencer's weapons hung below the view): each weapon in hand (each weapon set's current one)
//    belongs to the hand its mount bone is on (weapon+0xE88, the record of `arms_r` / `arms_l` / `armsFlip_l`...: the
//    name's _l), and each hand has its own T anchored on its own weapon, the left grip the right one mirrored. A hand
//    with none shares the other's (the Ranger's left hand on the rifle's guard); with none in either, the aim frame.
//    The body's bones whose name ends in _l take the left T, the others the right. Weapons not in hand (on the back,
//    put away) are not drawn in the colour passes (their shadow is).
//  - The camera's first row points to the screen's left (measured: a grip at +x drew on the left), so every "right" the
//    ini and the motion speak of is mirrored into it here (x, yaw and roll negated).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "firstperson.h"
#include "fp_view.h"
#include "fp_motion.h"
#include "map.h"
#include "memory.h"
#include <cmath>
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
                      kLookSmooth=0x1260,kWeapons=0x1950,kWeaponCount=0x1960,kWeaponModel=0xF30;
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
// and the Wing Diver's: Debug=1 logs every change to match them).
constexpr std::uint32_t kSprints[]={0x29E,0x29,0x3D,0xA38,0x25,0xE39},kRolls[]={0xE3C};
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
constexpr float kEyeBox=2.0f;   // m each way from the eye: the box Umbra gets for the viewmodel's models
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
constexpr float kOnWeapon=0.18f;   // m: a free hand's wrist this near its other hand's weapon's axis holds it
// The arms (the four classes share the names, fp-render-re.md §8): the forearm and its twists, the hand, its fingers,
// the weapon mounts, as a first-person game shows them. Not the shoulder (kata: its pad hangs in front of the view) nor
// the upper arm (jowan: it reaches back to the eye, its guards fill the view's corners); the elbow's vertices, weighed
// by both, lie on the rays the forearm is seen along (Pose).
const wchar_t* const kArmBones[]={L"kawan",L"te_",L"fing",L"ring",L"little",L"index",L"middle",
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
    M4 moved[2];                // the motion as a matrix about each hand's grip (fp_view.h MotionMatrix)
    bool viewOk[2];             // each hand's T this frame (kRight, kLeft), built by the camera step
    M4 view[2];
    bool shown[2];              // each arm drawn (a free hand off the weapon is not)
};
State st{};
// The flashes' matrices: each the shot matrix `src` through T, kept for kFlashFrames camera steps.
struct alignas(16) Flash { float m[16]; const float* src; int age,hand; };
Flash flashes[kFlashSlots]{};
int flashNext=0;
using CreateFn=void*(__fastcall*)(void*,void*,void*,void*);
bool toggled=false,keyWas=false;
int passLog[6]{};
ULONGLONG passLogAt=0;

bool KeyDown(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
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

// Why this machine's player is not to be seen in first person through `cam` now (nullptr: they are).
const char* NotWanted(const unsigned char* cam,const unsigned char* h) noexcept {
    const Config& c=Cfg();
    if(!c.enabled || !c.firstPerson || !toggled)return "off";
    if(!h || !IsSoldier(h))return "no soldier";
    if(At<const void*>(cam,kCamTarget)!=h && At<const void*>(cam,kCamTargetRef)!=h)return "another camera";
    if(h[kDead])return "dead";
    if(!HumanOnFoot(h))return "riding";
    if(h[kRagdoll])return "down";
    if(GadgetView(cam,h))return "a class gadget's view";
    return nullptr;
}

bool Wanted(const unsigned char* cam,const unsigned char* h) noexcept {
    const char* why=NotWanted(cam,h);
    static ULONGLONG at=0;
    if(why && toggled && h && Cfg().debug && GetTickCount64()-at>=5000) {
        at=GetTickCount64();
        Log("FP not on camera %p: %s (special view %d, vtable +0x%llX)",static_cast<const void*>(cam),why,
            At<std::int32_t>(h,kSpecialView),static_cast<unsigned long long>(At<const unsigned char*>(h,0)-image));
    }
    return !why;
}

void Toggle(const unsigned char* h) noexcept {
    const Config& c=Cfg();
    const bool down=h && HumanOnFoot(h) && KeyDown(c.firstPersonKey);
    if(down && !keyWas){toggled=!toggled;Log("FP %s (key 0x%X)",toggled ? "on" : "off",c.firstPersonKey);}
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
    if(d>0 && d<10){c.shots+=d;c.recoil=std::fmax(c.recoil,RecoilOf(w));}
    if(fire!=3 || st.fire[k]==3)return;
    const std::int32_t t=At<std::int32_t>(w,kReloadTime);
    st.reloadLeft=t>0 ? (t<kMostReloadFrames ? t : kMostReloadFrames) : 0;
    if(Cfg().debug)Log("FP reload %d frames (FireRecoil %.3f)",t,At<float>(w,kFireRecoil));
}

// The hands' weapons this frame: shots, a reload started, a switch.
void Weapons(const unsigned char* h,fpmotion::Cue& c) noexcept {
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
    fpmotion::Cue c{};
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
    const fpmotion::Pose p=fpmotion::Advance(st.motion,Cues(h));
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
    if(!placeOk || !model || !st.human)return;
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

// Whether the hand `hand` without a weapon of its own is on the other hand's weapon (`anchor`: its root; the Ranger's
// left hand on the rifle's guard): its wrist (te_l / te_r) within kOnWeapon of the weapon's axis, along its first metre
// and a half ahead of the grip. A free hand off it (the Wing Diver's) is not drawn.
bool HandOnWeapon(const unsigned char* h,int hand,const M4& anchor) noexcept {
    const unsigned char* wrist=BoneNamed(h+kBodyInst,hand==kLeft ? L"te_l" : L"te_r");
    if(!wrist)return false;
    const float* p=reinterpret_cast<const float*>(wrist+kBoneWorld+48);
    M4 w;
    if(!fpview::Orthonormal(anchor,&w))return false;
    const float d[3]={p[0]-w.m[12],p[1]-w.m[13],p[2]-w.m[14]};
    const float along=d[0]*w.m[8]+d[1]*w.m[9]+d[2]*w.m[10];
    if(along<-0.1f || along>1.5f)return false;
    const float off[3]={d[0]-w.m[8]*along,d[1]-w.m[9]*along,d[2]-w.m[10]*along};
    return off[0]*off[0]+off[1]*off[1]+off[2]*off[2]<kOnWeapon*kOnWeapon;
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
    Log("FP weapon %p: vtable +0x%llX (weapon +0x%llX) draw slot +0x%llX hooked=%d mount %ls -> %s hand, umbra %p",
        static_cast<const void*>(w),static_cast<unsigned long long>(reinterpret_cast<unsigned char*>(vt)-image),
        static_cast<unsigned long long>(At<const unsigned char*>(w,0)-image),
        static_cast<unsigned long long>(static_cast<unsigned char*>(vt[kModelDrawSlot])-image),hookedHere,
        name ? name : L"?",hand==kLeft ? "left" : "right",At<const void*>(model,kUmbraObject));
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
    if(st.on)PlaceAll(true);
    if(st.cam && st.nearSet)Put<float>(st.cam,kCamNear,st.savedNear);
    if(st.human && st.smoothSet)Put<float>(st.human,kLookSmooth,st.savedSmooth);
    if(st.on)Log("FP view back to the stock camera");
    st=State{};
}

// Each hand's T this frame (above: its own weapon's anchor, else the other hand's T, else the aim frame).
void Views(const unsigned char* h,const unsigned char* cam) noexcept {
    const Config& c=Cfg();
    M4 anchor[2]={fpview::Identity(),fpview::Identity()};
    bool anchored[2];
    Hands(h,anchor,anchored);
    const bool both=anchored[kRight] && anchored[kLeft];
    const float gx=both ? c.fpDualGripX : c.fpGripX,gy=both ? c.fpDualGripY : c.fpGripY,gz=both ? c.fpDualGripZ : c.fpGripZ;
    const float grip[2][3]={{-gx,gy,gz},{gx,gy,gz}};   // right is -x
    const float hold[3]={-c.fpHoldX,c.fpHoldY,c.fpHoldZ};
    const float pivots[2][3]={{anchored[kRight] ? grip[kRight][0] : kPivot[0],anchored[kRight] ? gy : kPivot[1],anchored[kRight] ? gz : kPivot[2]},
                              {anchored[kLeft] ? grip[kLeft][0] : -kPivot[0],anchored[kLeft] ? gy : kPivot[1],anchored[kLeft] ? gz : kPivot[2]}};
    if(c.fpMotion)Motion(h,pivots);
    else st.moved[kRight]=st.moved[kLeft]=fpview::Identity();
    const M4 view=MatrixAt(cam+kCamMatrix);
    for(int hand=kRight;hand<=kLeft;++hand) {
        st.viewOk[hand]=anchored[hand] && fpview::AnchoredTransform(anchor[hand],view,grip[hand],c.fpScale,&st.view[hand],st.moved[hand]);
    }
    for(int hand=kRight;hand<=kLeft;++hand) {
        const int other=1-hand;
        st.shown[hand]=true;
        if(st.viewOk[hand])continue;
        if(st.viewOk[other]) {
            st.view[hand]=st.view[other];st.viewOk[hand]=true;
            st.shown[hand]=HandOnWeapon(h,hand,anchor[other]);
            continue;
        }
        st.viewOk[hand]=fpview::ViewTransform(MatrixAt(h+kAim),view,hold,c.fpScale,&st.view[hand],st.moved[kRight]);
    }
    Probe(anchored,anchor);
}

void Camera(unsigned char* cam) noexcept {
    const Config& c=Cfg();
    unsigned char* const h=PlayerHuman();
    Toggle(h);
    if(!Wanted(cam,h)){if(st.on && st.cam==cam)Leave();return;}
    if(!st.on || st.cam!=cam || st.human!=h) {
        Leave();
        st.on=true;st.cam=cam;st.human=h;st.fresh=true;st.moved[kRight]=st.moved[kLeft]=fpview::Identity();
        Log("FP view on %p (player %p)",cam,h);
    }
    std::memcpy(st.stock,cam+kCamMatrix,sizeof(st.stock));
    const unsigned char* head=BoneNamed(h+kBodyInst,L"head");
    const M4 body=MatrixAt(h+kBodyFrame);
    float headAt[3];
    if(head)std::memcpy(headAt,head+kBoneWorld+48,12);
    else {const float up[3]={0.0f,1.7f,0.0f};fpview::EyeWorld(body,up,headAt);}
    fpview::EaseEye(body,headAt,c.fpEyeUp,c.fpEyeAhead,c.fpEyeEase,st.fresh,st.eyeLocal);
    st.fresh=false;
    float eye[3];
    fpview::EyeWorld(body,st.eyeLocal,eye);
    std::memcpy(cam+kCamEyeRow,eye,12);
    if(c.fpNear>0.0f){if(!st.nearSet){st.savedNear=At<float>(cam,kCamNear);st.nearSet=true;}Put<float>(cam,kCamNear,c.fpNear);}
    if(c.fpAimSnap){if(!st.smoothSet){st.savedSmooth=At<float>(h,kLookSmooth);st.smoothSet=true;}Put<float>(h,kLookSmooth,1.0f);}
    Gather(h);
    Views(h,cam);
    PlaceAll(false);
    Flashes();
}

void __fastcall CamStepHook(void* camera,void* step) {
    auto cam=static_cast<unsigned char*>(camera);
    if(st.on && st.cam==cam)std::memcpy(cam+kCamMatrix,st.stock,sizeof(st.stock));
    nextCamStep(camera,step);
    __try { Camera(cam); }
    __except(EXCEPTION_EXECUTE_HANDLER){st=State{};}
}

// --- The draw ---
DrawFn NextDraw(const void* model) noexcept {
    const void* vt=At<const void*>(model,0);
    for(int i=0;i<hookedCount;++i)if(hooked[i].vtable==vt)return hooked[i].next;
    return reinterpret_cast<DrawFn>(image+kModelDraw);
}

// What a drawn model is to the first person: not the player's, their body, a weapon in a hand, one put away.
enum class Part { none,body,inHand,stowed };
Part PartOf(const void* model,int* hand) noexcept {
    *hand=kRight;
    if(!st.on)return Part::none;
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
    add(At<unsigned char*>(model,kBaseLod+kLodInst));
    const auto lods=At<unsigned char*>(model,kLods);
    const auto count=At<std::uint64_t>(model,kLodCount);
    if(lods && count<=8)for(std::uint64_t i=0;i<count;++i)add(At<unsigned char*>(lods+i*kLodStride,kLodInst));
    return n;
}

float saved[kMostInsts][kMostBones*16];

// Every pose of `inst` through T; the body's bones that are not the arms' collapsed onto the eye. A vertex weighed by
// an arm bone and one of those lies between its arm place and the eye, on the ray the arm place is seen along, so it
// shows where the arm does (at another depth): no triangle is stretched across the view; one weighed by those alone is
// a point at the eye, nothing.
// `t` the two hands' T (a weapon: its hand's twice); the body's bones take the left one when their name ends in _l.
void Pose(unsigned char* inst,const M4* t,bool body,const float* eye,float* backup) noexcept {
    const auto n=At<std::int32_t>(inst,kInstCount);
    auto pose=At<float*>(inst,kInstPose);
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    if(!pose || !bones || n<=0 || n>kMostBones)return;
    std::memcpy(backup,pose,static_cast<std::size_t>(n)*64);
    for(std::int32_t i=0;i<n;++i) {
        float* p=pose+i*16;
        const wchar_t* name=At<const wchar_t*>(bones+static_cast<std::size_t>(i)*kBoneStride,0);
        const std::size_t len=name ? std::wcslen(name) : 0;
        const bool left=body && len>=2 && name[len-2]==L'_' && name[len-1]==L'l';
        if(body && (!ArmBone(name) || !st.shown[left ? kLeft : kRight])) {
            std::memcpy(p,fpview::Collapse(eye).m,64);
            continue;
        }
        const M4 moved=fpview::Mul(MatrixAt(reinterpret_cast<const unsigned char*>(p)),t[left ? kLeft : kRight]);
        std::memcpy(p,moved.m,64);
    }
}

void Unpose(unsigned char* inst,const float* backup) noexcept {
    const auto n=At<std::int32_t>(inst,kInstCount);
    auto pose=At<float*>(inst,kInstPose);
    if(pose && n>0 && n<=kMostBones)std::memcpy(pose,backup,static_cast<std::size_t>(n)*64);
}

void LogPass(Part part,int pass) noexcept {
    if(!Cfg().debug || pass<0 || pass>5 || part==Part::stowed)return;
    passLog[pass]+=part==Part::body ? 1 : 100;
    const ULONGLONG now=GetTickCount64();
    if(now-passLogAt<5000)return;
    if(passLogAt)Log("FP draws in 5 s by pass (body + 100 x weapons): 0:%d 1:%d 2:%d 3:%d 4:%d 5:%d (weapons held %d)",
                     passLog[0],passLog[1],passLog[2],passLog[3],passLog[4],passLog[5],st.weaponCount);
    std::memset(passLog,0,sizeof(passLog));passLogAt=now;
}

void __fastcall DrawHook(void* model,void* ctx,int pass,void* view) {
    const DrawFn next=NextDraw(model);
    Part part=Part::none;
    int hand=kRight;
    __try { part=PartOf(model,&hand); } __except(EXCEPTION_EXECUTE_HANDLER){part=Part::none;}
    if(part!=Part::none)LogPass(part,pass);
    if(part==Part::none || pass==3 || pass==4 || pass>5){next(model,ctx,pass,view);return;}
    if(pass==0 || part==Part::stowed)return;   // the occluder depth (not where it is in this picture); put away: unseen
    unsigned char* insts[kMostInsts];
    int n=0;
    __try {
        if(st.viewOk[kRight] && st.viewOk[kLeft]) {
            const M4 t[2]={part==Part::body ? st.view[kRight] : st.view[hand],part==Part::body ? st.view[kLeft] : st.view[hand]};
            n=InstancesOf(static_cast<unsigned char*>(model),insts);
            const float* eye=reinterpret_cast<const float*>(st.cam+kCamEyeRow);   // the drawn view's eye
            for(int i=0;i<n;++i)Pose(insts[i],t,part==Part::body,eye,saved[i]);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){for(int i=0;i<n;++i)Unpose(insts[i],saved[i]);n=0;}
    next(model,ctx,pass,view);
    __try { for(int i=0;i<n;++i)Unpose(insts[i],saved[i]); } __except(EXCEPTION_EXECUTE_HANDLER){}
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

bool InstallFirstPerson() noexcept {
    if(!Matches(kCamStep,kCamStepCode,sizeof(kCamStepCode)) || !Matches(kModelDraw,kModelDrawCode,sizeof(kModelDrawCode))) {
        Log("FP off: native profile mismatch");
        return false;
    }
    void* next=nullptr;
    auto slot=reinterpret_cast<void**>(image+kCamVtable)+kCamStepSlot;
    camOk=edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&CamStepHook),&next);
    if(!camOk){Log("FP off: the camera step not chained");return false;}
    nextCamStep=reinterpret_cast<CamStepFn>(next);
    drawOk=HookDrawOf(reinterpret_cast<void**>(image+kModelVtable));
    bool changed=false;
    const bool flashOk=RedirectCall(image+kFlashCall,image+kEffectCreate,reinterpret_cast<void*>(&FlashCreateHook),changed);
    placeOk=Matches(kUmbraPlace,kUmbraPlaceCode,sizeof(kUmbraPlaceCode));
    const bool lineOk=RedirectCall(image+kAimLineCall,image+kAimLineBuild,reinterpret_cast<void*>(&AimLineHook),changed);
    toggled=Cfg().firstPersonStart;
    Log("FP installed: camera=%d draw=%d flash=%d aim line=%d culling=%d start=%d",camOk,drawOk,flashOk,lineOk,placeOk,toggled);
    return drawOk;
}
}  // namespace crew
