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
// The viewmodel turns about a point near the grip (the eye's frame: right, up, ahead, m).
const float kPivot[3]={0.2f,-0.3f,0.4f};
// The arms (the four classes share the names, fp-render-re.md §8): the shoulder, upper and lower arm and their twists,
// the hand, its fingers, the weapon mounts.
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

// --- What the camera step leaves for the draws (game thread only: the draw and the camera step both run on it) ---
struct State {
    bool on;                    // the local player's camera is in first person
    unsigned char* human;       // that player
    unsigned char* cam;         // its camera
    const void* body;           // its body model
    const void* weapons[kMostWeapons];
    int weaponCount;
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
    M4 moved;                   // the motion as a matrix (fp_view.h MotionMatrix)
};
ULONGLONG epoch=0;              // a camera step: T rebuilt at the next draw
State st{};
struct Drawn { ULONGLONG epoch; bool ok; M4 t; };
Drawn drawn{};
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

// Whether this machine's player is to be seen in first person through `cam` now.
bool Wanted(const unsigned char* cam,const unsigned char* h) noexcept {
    const Config& c=Cfg();
    if(!c.enabled || !c.firstPerson || !toggled || !h || !IsSoldier(h))return false;
    if(At<const void*>(cam,kCamTarget)!=h && At<const void*>(cam,kCamTargetRef)!=h)return false;
    return !h[kDead] && HumanOnFoot(h) && !h[kRagdoll] && At<std::int32_t>(h,kSpecialView)!=1;
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

void Motion(const unsigned char* h) noexcept {
    const fpmotion::Pose p=fpmotion::Advance(st.motion,Cues(h));
    st.moved=fpview::MotionMatrix(p.x,p.y,p.z,p.pitch,p.yaw,p.roll,kPivot);
}

// The camera, the player's state given back: the near clip, the look smoothing.
void Leave() noexcept {
    if(st.cam && st.nearSet)Put<float>(st.cam,kCamNear,st.savedNear);
    if(st.human && st.smoothSet)Put<float>(st.human,kLookSmooth,st.savedSmooth);
    if(st.on)Log("FP view back to the stock camera");
    st=State{};
}

void Camera(unsigned char* cam) noexcept {
    const Config& c=Cfg();
    unsigned char* const h=PlayerHuman();
    Toggle(h);
    if(!Wanted(cam,h)){if(st.on && st.cam==cam)Leave();return;}
    if(!st.on || st.cam!=cam || st.human!=h) {
        Leave();
        st.on=true;st.cam=cam;st.human=h;st.fresh=true;st.moved=fpview::Identity();
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
    if(c.fpMotion)Motion(h);
    else st.moved=fpview::Identity();
    ++epoch;
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

// T for this frame (the camera and the aim frame as the last step left them), built once a frame.
bool ViewNow(M4* t) noexcept {
    if(drawn.epoch!=epoch) {
        const Config& c=Cfg();
        const float hold[3]={c.fpHoldX,c.fpHoldY,c.fpHoldZ};
        drawn.epoch=epoch;
        drawn.ok=fpview::ViewTransform(MatrixAt(st.human+kAim),MatrixAt(st.cam+kCamMatrix),hold,c.fpScale,&drawn.t,st.moved);
    }
    *t=drawn.t;
    return drawn.ok;
}

enum class Part { none,body,weapon };
Part PartOf(const void* model) noexcept {
    if(!st.on)return Part::none;
    if(model==st.body)return Part::body;
    for(int i=0;i<st.weaponCount;++i)if(st.weapons[i]==model)return Part::weapon;
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

// Every pose of `inst` through T; the body's bones that are not the arms' collapsed onto their own joint.
void Pose(unsigned char* inst,const M4& t,bool body,float* backup) noexcept {
    const auto n=At<std::int32_t>(inst,kInstCount);
    auto pose=At<float*>(inst,kInstPose);
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    if(!pose || !bones || n<=0 || n>kMostBones)return;
    std::memcpy(backup,pose,static_cast<std::size_t>(n)*64);
    for(std::int32_t i=0;i<n;++i) {
        float* p=pose+i*16;
        if(body && !ArmBone(At<const wchar_t*>(bones+static_cast<std::size_t>(i)*kBoneStride,0))) {
            float at[3];
            fpview::Point(t,p+12,at);
            std::memcpy(p,fpview::Collapse(at).m,64);
            continue;
        }
        const M4 moved=fpview::Mul(MatrixAt(reinterpret_cast<const unsigned char*>(p)),t);
        std::memcpy(p,moved.m,64);
    }
}

void Unpose(unsigned char* inst,const float* backup) noexcept {
    const auto n=At<std::int32_t>(inst,kInstCount);
    auto pose=At<float*>(inst,kInstPose);
    if(pose && n>0 && n<=kMostBones)std::memcpy(pose,backup,static_cast<std::size_t>(n)*64);
}

void LogPass(Part part,int pass) noexcept {
    if(!Cfg().debug || pass<0 || pass>5)return;
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
    __try { part=PartOf(model); } __except(EXCEPTION_EXECUTE_HANDLER){part=Part::none;}
    if(part!=Part::none)LogPass(part,pass);
    if(part==Part::none || pass==3 || pass==4 || pass>5){next(model,ctx,pass,view);return;}
    if(pass==0)return;   // the occluder depth: not where the body really is in this picture
    unsigned char* insts[kMostInsts];
    int n=0;
    M4 t;
    __try {
        if(ViewNow(&t)) {
            n=InstancesOf(static_cast<unsigned char*>(model),insts);
            for(int i=0;i<n;++i)Pose(insts[i],t,part==Part::body,saved[i]);
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
    toggled=Cfg().firstPersonStart;
    Log("FP installed: camera=%d draw=%d start=%d",camOk,drawOk,toggled);
    return drawOk;
}
}  // namespace crew
