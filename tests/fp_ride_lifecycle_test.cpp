// Production Ride / Gather / Rebatch on synthetic native layouts: boarding must return a
// surviving on-foot weapon's batch, without writing a weapon removed in this frame.
#include "../src/crew.h"
namespace {
bool keyDown=false,focused=true,mapOwnsKeys=false,onFoot=true,sazabi=false;
const unsigned char* mapKeyHuman=nullptr;
SHORT TestGetAsyncKeyState(int) { return keyDown ? static_cast<SHORT>(0x8000) : 0; }
HWND TestGetForegroundWindow() { return nullptr; }
DWORD TestGetWindowThreadProcessId(HWND,LPDWORD pid) { *pid=focused ? 42 : 43;return 0; }
DWORD TestGetCurrentProcessId() { return 42; }
}
#define GetWindowThreadProcessId TestGetWindowThreadProcessId
#define GetCurrentProcessId TestGetCurrentProcessId
#include "../src/firstperson.cpp"
#undef GetWindowThreadProcessId
#undef GetCurrentProcessId
#include <cstdio>
#include <initializer_list>
#include <atomic>
#include <thread>

namespace edf {
bool Readable(const void* p,std::size_t n,bool) noexcept {
    MEMORY_BASIC_INFORMATION m{};
    if(!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
    const auto begin=reinterpret_cast<std::uintptr_t>(p),end=reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize;
    return begin<=end && n<=end-begin;
}
bool ChainVtableSlot(void**,void*,void**) noexcept { return false; }
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept { return false; }
bool IsPlayer(const unsigned char* h) noexcept { return h!=nullptr; }
unsigned SeatCount(const unsigned char* vehicle) noexcept {
    const auto count=At<std::uint64_t>(vehicle,kSeatCount);
    const auto seats=At<const unsigned char*>(vehicle,kSeats);
    return count>0 && count<=kMaxSeats && Readable(seats,count*kSeatStride,false) ? static_cast<unsigned>(count) : 0;
}

bool RedirectCall(unsigned char*,void*,void*,bool&) noexcept { return false; }
}
namespace crew::thermal {
bool WantsCapture() noexcept { return false; }
void ObserveSurface(ID3D11DeviceContext*,int) noexcept {}
void CaptureIndexed(ID3D11DeviceContext*,unsigned,unsigned,int) noexcept {}
}

namespace crew {
unsigned char* image=nullptr;
Config config{};
bool depthPatchReady=true;
bool ViewDepthReady() noexcept { return depthPatchReady; }
void ViewWriteNear(void* camera,float value) noexcept { Put<float>(camera,kCamNear,value); }
void VehicleLookSetReady(bool) noexcept {}
bool enemyCamera=false;const unsigned char* enemyCameraHuman=nullptr;
bool EnemyControlCamera(const unsigned char* human,const float*,const float*,float* eye,float* look) noexcept {
    if(!enemyCamera || human!=enemyCameraHuman)return false;
    const float targetEye[3]={70,80,90},targetLook[3]={70,80,190};
    std::memcpy(eye,targetEye,12);std::memcpy(look,targetLook,12);return true;
}
bool SazabiFirstPersonFrame(const void*,unsigned,float*) noexcept { return false; }
bool observeCamera=false;
bool VehicleLookCamera(const unsigned char*,float* matrix) noexcept {
    if(!observeCamera)return false;matrix[8]+=.1f;return true;
}
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
bool IsSazabi(const void*) noexcept { return sazabi; }
bool HighCamOn(const void*) noexcept { return false; }
bool TurretCamHighTransition(const void*) noexcept { return false; }
float SightZoomNow(const void*) noexcept { return 1.0f; }
bool SightZoomMounted(const void*) noexcept { return false; }
bool HumanOnFoot(const unsigned char*) noexcept { return onFoot; }
bool MapHoldsKeys() noexcept { return mapOwnsKeys; }
bool MapHoldsHumanKeys(const unsigned char* human) noexcept { return mapOwnsKeys && (!mapKeyHuman || mapKeyHuman==human); }
bool haveEye=true;
unsigned char rideVehicle[0x1200]{};
fpview::M4 rideFrame=fpview::Identity();
bool VehicleEye(unsigned char*,float* eye,RideView* out) noexcept {
    if(!haveEye)return false;
    *out=RideView{};out->vehicle=rideVehicle;
    std::memcpy(out->frame,rideFrame.m,64);std::memcpy(eye,rideFrame.m+12,12);return true;
}
int VehicleScreens(const unsigned char*,int,const float*,CockpitScreens* out) noexcept { out->n=0;return 0; }
void* retiredThis=nullptr;unsigned retiredFlags=0;
void* __fastcall RecordFlashDtor(void* object,unsigned flags) { retiredThis=object;retiredFlags=flags;return object; }
bool drawArmMask=false;
void __fastcall RecordBodyDraw(void*,void*,int,void*) { drawArmMask=fpskin::Active(); }
void* nestedOuter=nullptr;void* nestedInner=nullptr;bool nestedDrawn=false;
bool nestedOuterMask=false,nestedForeignMask=false,nestedRestoredMask=false;
void __fastcall RecordNestedSkinDraw(void* model,void* context,int pass,void* view) {
    if(model==nestedOuter) {
        nestedOuterMask=fpskin::Active();DrawHook(nestedInner,context,pass,view);nestedRestoredMask=fpskin::Active();
    } else nestedForeignMask=fpskin::Active();
}

void __fastcall RecordNestedDraw(void* model,void* context,int pass,void* view) {
    if(model==nestedOuter && !nestedDrawn){nestedDrawn=true;DrawHook(nestedInner,context,pass,view);}
}

float laserStart[4]{},laserEnd[4]{};
void __fastcall RecordLaser(void* object,void*,int,void*) {
    const auto p=static_cast<const unsigned char*>(object);
    std::memcpy(laserStart,p+0x120,16);std::memcpy(laserEnd,p+0x130,16);
}
}

int main() {
    using namespace crew;
    edf::SetInputSource(&TestGetAsyncKeyState,&TestGetForegroundWindow);   // the plugin's key source (edf/input_source.h)
    unsigned char human[0x2100]{},cam[0x400]{},weapon[0x1400]{};
    int humanOwnerCtrl[4]={0,0,1,0},weaponOwnerCtrl[4]={0,0,1,0};
    Put<void*>(human,kSelfCtrl,humanOwnerCtrl);Put<void*>(weapon,kSelfCtrl,weaponOwnerCtrl);
    void* vt[2]{};
    unsigned char* list[]={weapon};
    auto model=weapon+kWeaponModel;
    int batch=1,replacement=2;
    int failed=0;
    const auto check=[&](bool ok,const char* what){std::printf("%s %s\n",ok ? "ok" : "FAIL",what);if(!ok)++failed;};
    unsigned char nativeImage[1024]{};
    image=nativeImage;
    Put<void*>(weapon,0,nativeImage+64);Put<void*>(nativeImage,56,nativeImage+128);
    Put<std::uint32_t>(nativeImage,128+0xC,256);
    for(const char* type:{".?AVWeapon_Swing@@",".?AVWeapon_ImpactHammer@@",".?AVWeapon_PileBanker@@",".?AVWeapon_Shield@@"}) {
        std::memcpy(nativeImage+272,type,std::strlen(type)+1);
        check(NativeGrip(weapon),"native melee and shield families preserve the authored holding pose");
    }
    const char* gun=".?AVWeapon_BasicShoot@@";std::memcpy(nativeImage+272,gun,std::strlen(gun)+1);
    check(!NativeGrip(weapon),"firearms retain their bore-aligned placement");
    Put<std::uint32_t>(nativeImage,128+0xC,edf::kImageSize);
    check(!NativeGrip(weapon),"a descriptor outside the native image is rejected");
    image=nullptr;
    config.debug=false;
    Put<void*>(model,0,vt);
    Put<void*>(human,kWeapons,list);
    Put<std::uint64_t>(human,kWeaponCount,1);
    const auto reset=[&](){
        Current()=State{};Current().on=true;Current().human=human;Current().cam=cam;
        Current().bodyCtrl=At<const void*>(human,kSelfCtrl);
        Current().weapons[0]=model;Current().weaponCtrls[0]=At<const void*>(weapon,kSelfCtrl);Current().weaponCount=1;
        Current().inHand[0]=model;Current().inHandCtrls[0]=At<const void*>(weapon,kSelfCtrl);Current().inHandCount=1;
        for(auto& u:Current().unbatched)u=Unbatched{};
        Current().unbatched[0]=Unbatched{model,&batch,weaponOwnerCtrl};Put<void*>(model,kBatch,nullptr);
        Put<std::uint64_t>(human,kWeaponCount,1);haveEye=true;
    };
    reset();
    unsigned char lodA[0x80]{},lodB[0x80]{},recordsA[3*kBoneStride]{},recordsB[3*kBoneStride]{},lods[kLodStride]{};
    void* bufferA[1]{&batch};void* bufferB[1]{&replacement};
    Put<void*>(model,kBaseLod+kLodInst,lodA);Put<void*>(model,kLods,lods);Put<std::uint64_t>(model,kLodCount,1);
    Put<void*>(lods,kLodInst,lodB);Put<void*>(lodA,kInstBones,recordsA);Put<int>(lodA,kInstCount,3);Put<void*>(lodA,0x68,bufferA);
    Put<void*>(lodB,kInstBones,recordsB);Put<int>(lodB,kInstCount,3);Put<void*>(lodB,0x68,bufferB);
    Put<const wchar_t*>(recordsA,0,L"body");Put<const wchar_t*>(recordsA+kBoneStride,0,L"te_r");Put<const wchar_t*>(recordsA+2*kBoneStride,0,L"head");
    Put<const wchar_t*>(recordsB,0,L"te_r");Put<const wchar_t*>(recordsB+kBoneStride,0,L"body");Put<const wchar_t*>(recordsB+2*kBoneStride,0,L"head");
    Current().body=model;Current().shown[0]=Current().shown[1]=true;
    Put<void*>(static_cast<unsigned char*>(model)-kBodyModel,kSelfCtrl,humanOwnerCtrl); // native-layout body alias fixture
    Publish();DrawSnap maskSnap{};TakeSnapshot(&maskSnap);drawSnap=&maskSnap;   // the draw path reads its own copy
    const auto baseMask=BodyMask(&batch),lodMask=BodyMask(&replacement);
    drawSnap=nullptr;
    check(baseMask.count==3 && !baseMask.arm[0] && baseMask.arm[1] && lodMask.arm[0] && !lodMask.arm[1],
          "body filtering selects the bound GPU palette's LOD bone ordering, not the base skeleton's");
    Put<void*>(model,kBaseLod+kLodInst,nullptr);Put<void*>(model,kLods,nullptr);Put<std::uint64_t>(model,kLodCount,0);
    reset();
    Current().viewOk[0]=true;Current().view[0]=fpview::Identity();
    float flashMatrix[16]{};flashMatrix[0]=flashMatrix[5]=flashMatrix[10]=flashMatrix[15]=1;
    Put<float>(weapon,kFireRecoil,.075f);Current().shots[0]=0;
    for(int i=0;i<2;++i){Put<const float*>(weapon,kFlashMatrix,flashMatrix);Relocate(weapon);}
    fpmotion::Cue firing{};Hand(weapon,0,0,0,firing);
    check(firing.shots==2 && firing.recoil>=.3f,"actual shot events drive recoil even when the sampled counter does not advance");
    firing=fpmotion::Cue{};Hand(weapon,0,0,0,firing);
    check(firing.shots==0,"shot events are consumed once");
    for(int i=0;i<2;++i){Put<const float*>(weapon,kFlashMatrix,flashMatrix);Relocate(weapon);}
    firing=fpmotion::Cue{};Hand(weapon,0,2,0,firing);
    check(firing.shots==2,"counter and flash observations of the same shots are not double counted");
    // The shot's native muzzle matrix stays frozen until another shot. A moving viewmodel must carry an
    // immutable muzzle offset through the weapon's current root, rather than reusing that old world position.
    Put<void*>(model,kBaseLod+kLodInst,lodA);Put<int>(lodA,kInstCount,1);
    const M4 shotRoot=fpview::Translate(10,2,3);std::memcpy(recordsA+kBoneWorld,shotRoot.m,64);
    M4 flashRoot=fpview::Translate(9,2,3);Put<float*>(lodA,kInstPose,flashRoot.m); // a queued draw's older pose
    Current().flashes[0]=Flash{};Current().flashNext=0;Current().view[0]=fpview::Translate(-10,-2,-3);
    const M4 muzzle=fpview::Translate(10,2.1f,3.7f);std::memcpy(flashMatrix,muzzle.m,64);
    Put<const float*>(weapon,kFlashMatrix,flashMatrix);Relocate(weapon);
    const float* ownFlash=At<const float*>(weapon,kFlashMatrix);
    check(ownFlash!=flashMatrix,"a shot receives its own immutable muzzle-to-weapon anchor");
    check(std::fabs(Current().flashes[0].local.m[12])<1e-5f,
          "muzzle anchoring uses the shot's native bone frame even when the queued draw pose is one frame older");
    flashRoot=fpview::Translate(15,4,6);Current().view[0]=fpview::Translate(-15,-4,-6);
    flashMatrix[12]=999;flashMatrix[13]=888;flashMatrix[14]=777;Flashes();
    check(std::fabs(ownFlash[12])<1e-5f && std::fabs(ownFlash[13]-.1f)<1e-5f && std::fabs(ownFlash[14]-.7f)<1e-5f,
          "movement and a later native muzzle rewrite cannot move an existing flash off its viewmodel muzzle");
    for(int k=0;k<kFlashSlots;++k) {
        Current().flashes[k].model=model;
    }
    Put<const float*>(weapon,kFlashMatrix,flashMatrix);Relocate(weapon);
    check(At<const float*>(weapon,kFlashMatrix)==flashMatrix,
          "a full flash pool preserves the native shot rather than overwriting a matrix still borrowed by another flash");
    Current().inHandCount=0;Flashes();
    check(Current().flashes[0].model==nullptr,"retiring a held weapon never follows its freed or replaced pose");
    Current().inHand[0]=model;Current().inHandCount=1;Put<void*>(model,kBaseLod+kLodInst,nullptr);
    // Both calls go through the production DrawHook/Pose/native-next/Unpose path. An inner weapon's original
    // pose must never replace the outer weapon's saved original (the old file-global backup did exactly that).
    unsigned char nestedWeapon[0x1400]{},nestedInstA[0x80]{},nestedInstB[0x80]{},nestedBonesA[kBoneStride]{},nestedBonesB[kBoneStride]{};
    M4 nestedPoseA=fpview::Translate(1,2,3),nestedPoseB=fpview::Translate(7,8,9);
    const M4 originalPoseA=nestedPoseA,originalPoseB=nestedPoseB;
    auto nestedModel=nestedWeapon+kWeaponModel;int nestedCtrl[4]={0,0,1,0};Put<void*>(nestedWeapon,kSelfCtrl,nestedCtrl);
    Put<void*>(model,0,vt);Put<void*>(nestedModel,0,vt);
    Put<void*>(model,kBaseLod+kLodInst,nestedInstA);Put<void*>(nestedModel,kBaseLod+kLodInst,nestedInstB);
    Put<void*>(nestedInstA,kInstBones,nestedBonesA);Put<void*>(nestedInstB,kInstBones,nestedBonesB);
    Put<int>(nestedInstA,kInstCount,1);Put<int>(nestedInstB,kInstCount,1);
    Put<void*>(nestedInstA,kInstPose,nestedPoseA.m);Put<void*>(nestedInstB,kInstPose,nestedPoseB.m);
    Put<const wchar_t*>(nestedBonesA,0,L"body");Put<const wchar_t*>(nestedBonesB,0,L"body");
    Current().inHand[0]=model;Current().inHand[1]=nestedModel;Current().inHandCtrls[0]=weaponOwnerCtrl;Current().inHandCtrls[1]=nestedCtrl;Current().handOf[0]=0;Current().handOf[1]=1;Current().inHandCount=2;
    Current().viewOk[0]=Current().viewOk[1]=true;Current().view[0]=fpview::Translate(10,0,0);Current().view[1]=fpview::Translate(20,0,0);
    hooked[0]=Hooked{vt,&RecordNestedDraw};hookedCount=1;nestedOuter=model;nestedInner=nestedModel;nestedDrawn=false;
    Publish();DrawHook(model,nullptr,1,nullptr);
    check(nestedDrawn && std::memcmp(nestedPoseA.m,originalPoseA.m,64)==0 && std::memcmp(nestedPoseB.m,originalPoseB.m,64)==0,
          "nested production model draws restore their own original poses without borrowing another model's backup");
    unsigned char skippedInst[0x80]{},skippedLod[kLodStride]{};
    M4 skippedPose=fpview::Translate(91,82,73);const M4 skippedOriginal=skippedPose;
    Put<int>(skippedInst,kInstCount,1);Put<void*>(skippedInst,kInstPose,skippedPose.m); // missing bones: no backup was captured
    Put<void*>(skippedLod,kLodInst,skippedInst);Put<void*>(model,kLods,skippedLod);Put<std::uint64_t>(model,kLodCount,1);
    nestedDrawn=false;DrawHook(model,nullptr,1,nullptr);
    check(std::memcmp(skippedPose.m,skippedOriginal.m,64)==0 && std::memcmp(nestedPoseA.m,originalPoseA.m,64)==0,
          "a LOD skipped before capture is never restored from uninitialised backup while valid LODs still return their own poses");
    // Inject a partial transform fault after this LOD's full backup, before any future LOD is visited.
    Put<void*>(skippedInst,kInstBones,reinterpret_cast<void*>(1));nestedDrawn=false;DrawHook(model,nullptr,1,nullptr);
    check(std::memcmp(skippedPose.m,skippedOriginal.m,64)==0 && std::memcmp(nestedPoseA.m,originalPoseA.m,64)==0,
          "partial transform failure restores only captured native buffers");
    Put<void*>(model,kLods,nullptr);Put<std::uint64_t>(model,kLodCount,0);
    // The same production entry also isolates its skin gate for a nested foreign/native model.
    unsigned char foreignModel[0x300]{};Put<void*>(foreignModel,0,vt);Put<void*>(human+kBodyModel,0,vt);
    Current().body=human+kBodyModel;Current().bodyCtrl=humanOwnerCtrl;
    hooked[0]=Hooked{vt,&RecordNestedSkinDraw};nestedOuter=human+kBodyModel;nestedInner=foreignModel;
    nestedOuterMask=nestedForeignMask=nestedRestoredMask=false;Publish();DrawHook(nestedOuter,nullptr,1,nullptr);
    check(nestedOuterMask && !nestedForeignMask && nestedRestoredMask && !fpskin::Active(),
          "a nested foreign/NPC model sees no local body skin mask and restores the enclosing body's exact gate");
    hookedCount=0;Put<void*>(model,kBaseLod+kLodInst,nullptr);reset();Current().viewOk[0]=true;
    unsigned char laser[0x190]{};Put<void*>(laser,8,weapon);
    const float from[4]={1,2,3,1},hit[4]={20,30,40,1};
    std::memcpy(laser+0x130,from,16);std::memcpy(laser+0x140,hit,16);
    unsigned char originalLaser[sizeof(laser)];std::memcpy(originalLaser,laser,sizeof(laser));
    Current().view[0]=fpview::Translate(3,4,5);nextLaserDraw=reinterpret_cast<void*>(&RecordLaser);
    Publish();LaserDrawHook(laser+0x10,nullptr,2,nullptr);
    check(laserStart[0]==4 && laserStart[1]==6 && laserStart[2]==8 && std::memcmp(laserEnd,hit,16)==0,
          "the owning weapon's visible laser start follows its viewmodel while the hit endpoint stays in the world");
    check(std::memcmp(laser,originalLaser,sizeof(laser))==0,"all laser attachment state is restored after native drawing");
    Put<void*>(laser,8,human);LaserDrawHook(laser+0x10,nullptr,2,nullptr);
    check(std::memcmp(laserStart,from,16)==0,"another owner's laser is not relocated even when nearby");
    reset();int pooledControl[4]={0,0,1,0};Put<void*>(weapon,kSelfCtrl,pooledControl);Gather(human);
    Current().inHandCtrls[0]=pooledControl;Rebatch(false);
    check(!Current().unbatched[0].model && At<void*>(model,kBatch)==nullptr,
          "an old batch record is discarded before the in-hand shortcut when the weapon address belongs to a new owner");
    Put<void*>(weapon,kSelfCtrl,weaponOwnerCtrl);
    reset();
    Current().shotEvent[0]=State::ShotEvent{weapon,2,.5f};Current().motion.recoilZ.at=-.07f;Current().have=true;Current().reloadLeft=30;
    check(Ride(cam,human),"a fitted vehicle accepts the ride");
    check(Current().shotEvent[0].count==0 && Current().motion.recoilZ.at==0 && !Current().have && !Current().reloadLeft,
          "boarding retires pending on-foot shots, recoil and reload state");
    check(At<void*>(model,kBatch)==&batch,"boarding returns the surviving weapon's original batch");
    check(!Current().unbatched[0].model && Current().weaponCount==0 && Current().inHandCount==0,"boarding retires the batch record and viewmodel lists");
    for(float roll:{0.0f,1.5707963f,3.14159265f,-1.5707963f}) {
        const float axis[3]={0,0,1};rideFrame=fpview::Rotation(axis,std::cos(roll),std::sin(roll));
        rideFrame.m[12]=5;rideFrame.m[13]=2;rideFrame.m[14]=7;
        check(Ride(cam,human) && std::memcmp(cam+kCamMatrix,rideFrame.m,64)==0,
              "the production ride copies the complete seat frame, including bank/inversion and position");
    }
    // Production instrument pose on a synthetic skeleton, then the same restore used after native draw.
    unsigned char inst[0x50]{},bones[2*kBoneStride]{};float pose[32]{},backup[32]{};
    Put<void*>(inst,kInstBones,bones);Put<int>(inst,kInstCount,2);Put<float*>(inst,kInstPose,pose);
    Put<const wchar_t*>(bones,0,L"vc_i_0_0_d_0_0_1");
    Put<const wchar_t*>(bones+kBoneStride,0,L"vc_i_1_0_d_0_0_1");
    const auto id=fpview::Identity();std::memcpy(pose,id.m,64);std::memcpy(pose+16,id.m,64);
    const float originals[32]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    // The draw (render thread) poses from the camera step's published copy, never from State (§17.5).
    const auto drawInstruments=[&]{Publish();DrawSnap s{};TakeSnapshot(&s);SnapScope taken(&s);InstrumentPose(inst,backup);};
    Current().ride.seat=0;Current().instrumentsReady=true;Current().instruments.value[0]=5;
    drawInstruments();
    check(pose[0]==0 && pose[16]==1,"inactive digit segment collapses, another seat's instrument is unchanged");
    Unpose(inst,backup);
    {
        Publish();DrawSnap published0{};TakeSnapshot(&published0);
        Current().ride.seat=1;Current().instrumentsReady=false;   // the game thread moves on, not yet published
        {SnapScope taken(&published0);InstrumentPose(inst,backup);}
        check(pose[0]==0 && pose[16]==1,"the instruments draw reads the published seat and readings, not the game thread's live State");
        Unpose(inst,backup);
        Current().ride.seat=0;Current().instrumentsReady=true;
    }
    check(std::memcmp(pose,originals,sizeof(pose))==0,"the native draw pose is restored byte for byte");
    Put<const wchar_t*>(bones,0,L"vc_i_0_0_l_0_0");
    Put<const wchar_t*>(bones+kBoneStride,0,L"vc_i_0_0_l_1_0");
    Current().instruments.language=1;drawInstruments();
    check(pose[0]==0 && pose[16]==1,"native draw path selects Chinese geometry and hides English geometry");
    Unpose(inst,backup);
    check(std::memcmp(pose,originals,sizeof(pose))==0,"language selection never persists in the native skeleton");
    Put<const wchar_t*>(bones,0,L"vc_i_1_0_l_0_0");
    Put<const wchar_t*>(bones+kBoneStride,0,L"vc_i_1_0_l_1_0");
    drawInstruments();
    check(pose[0]==0 && pose[16]==1,"adjacent station captions also select exactly one language");
    Unpose(inst,backup);
    Put<const wchar_t*>(bones,0,L"vc_i_1_0_c_0_0");
    Put<const wchar_t*>(bones+kBoneStride,0,L"vc_i_1_0_c_0_2");
    drawInstruments();
    check(pose[0]==0 && pose[16]==0,"adjacent station does not mix overlapping damage colours without its readings");
    Unpose(inst,backup);
    Put<const wchar_t*>(bones,0,L"vc_i_1_0_D_7_0_0");
    Put<const wchar_t*>(bones+kBoneStride,0,L"vc_i_1_0_D_7_0_6");
    Current().instruments.value[instruments::ammo]=99999;drawInstruments();
    check(pose[0]==0 && pose[16]==1,"new neighbouring digits show a dash, never a fully lit 88888");
    Unpose(inst,backup);
    Put<const wchar_t*>(bones,0,L"vc_i_1_0_B_2");
    Put<const wchar_t*>(bones+kBoneStride,0,L"vc_i_1_0_T_0");
    Current().instruments.share[instruments::fuel]=1;Current().instruments.contact[0]=true;drawInstruments();
    check(pose[0]==0 && pose[16]==0,"new neighbouring bars and contacts do not inherit this seat's readings");
    Unpose(inst,backup);
    check(std::memcmp(pose,originals,sizeof(pose))==0,"new widget dispatch restores every original pose byte");


    reset();Put<std::uint64_t>(human,kWeaponCount,0);
    check(Ride(cam,human),"boarding after the weapon was removed");
    check(!At<void*>(model,kBatch) && !Current().unbatched[0].model,"a removed weapon is never written and its stale record is dropped");
    reset();Put<void*>(model,kBatch,&replacement);
    check(Ride(cam,human) && At<void*>(model,kBatch)==&replacement,"boarding preserves a replacement batch owned by the game");
    reset();haveEye=false;
    check(!Ride(cam,human) && !At<void*>(model,kBatch) && Current().unbatched[0].model==model,"no cockpit leaves the on-foot state for Leave to restore");
    Current().on=true;Current().riding=true;Current().cam=cam;Current().screens.n=0;
    check(!FirstPersonHidesStockHud(cam),"an eye-only passenger seat keeps the stock HUD");
    Current().screens.n=1;
    check(FirstPersonHidesStockHud(cam),"a seat with replacement screens hides the stock HUD");
    config.fpHideStockHud=false;
    check(!FirstPersonHidesStockHud(cam),"the hide stock HUD preference still controls fitted screens");
    config.fpHideStockHud=true;
    check(!FirstPersonHidesStockHud(human),"another camera's HUD is never hidden");
    camOk=drawOk=true;
    const auto resetKey=[&](){toggled=false;keyPress=keyConsumed=0;keyPhysicalDown=false;keyClaimHuman=nullptr;keyClaimControl=nullptr;Current().key={};keyDown=false;focused=true;mapOwnsKeys=false;mapKeyHuman=nullptr;onFoot=true;sazabi=false;};
    resetKey();keyDown=true;Toggle(human);keyDown=false;Toggle(human);
    check(toggled,"an uninterrupted physical press and release toggles first person");
    resetKey();keyDown=true;Toggle(human);focused=false;Toggle(human);
    check(!toggled,"losing focus while held does not act as release");
    focused=true;Toggle(human);keyDown=false;Toggle(human);
    check(!toggled,"returning focus while held discards the interrupted gesture");
    keyDown=true;Toggle(human);keyDown=false;Toggle(human);
    check(toggled,"a fresh gesture after focus returns still works");
    resetKey();keyDown=true;Toggle(human);mapOwnsKeys=true;Toggle(human);
    check(!toggled,"map input ownership does not act as release");
    mapOwnsKeys=false;Toggle(human);keyDown=false;Toggle(human);
    check(!toggled,"closing the map while held discards the interrupted gesture");
    resetKey();keyDown=true;Toggle(human);Toggle(nullptr);keyDown=false;Toggle(human);
    check(!toggled,"a missing player cancels without toggling");
    resetKey();keyDown=true;Toggle(human);onFoot=false;keyDown=false;Toggle(human);
    check(!toggled,"holding the view key across boarding does not toggle");
    keyDown=true;Toggle(human);keyDown=false;Toggle(human);
    check(toggled,"a new press and release in the vehicle still toggles");
    resetKey();onFoot=false;sazabi=true;keyDown=true;Toggle(human);keyDown=false;Toggle(human);
    check(!toggled,"the Sazabi melee key keeps ownership of its press");
    toggled=true;camOk=true;drawOk=false;
    check(std::strcmp(NotWanted(cam,human),"hooks unavailable")==0,"a camera hook without a draw hook cannot activate first person");
    camOk=false;drawOk=true;
    check(std::strcmp(NotWanted(cam,human),"hooks unavailable")==0,"a draw hook without a camera hook cannot activate first person");
    Current()=State{};Current().cam=cam;camOk=drawOk=false;observeCamera=true;
    const auto nativeCamera=fpview::Identity();std::memcpy(cam+kCamMatrix,nativeCamera.m,64);
    nextCamStep=[](void*,void*){};
    CamStepHook(cam,nullptr);CamStepHook(cam,nullptr);
    check(std::fabs(At<float>(cam,kCamMatrix+32)-.1f)<1e-6f,
          "a native camera branch that leaves its matrix untouched cannot accumulate observation rotations");
    // The outer layer/physics changes only translation between steps. Rotation is still ours and must be
    // retracted independently; whole-matrix memcmp used to accumulate another .1 of left turn each iteration.
    bool stableRotation=true;
    for(int i=0;i<40;++i) {
        Put<float>(cam,kCamEyeRow,100.0f+static_cast<float>(i));CamStepHook(cam,nullptr);
        stableRotation=stableRotation && std::fabs(At<float>(cam,kCamMatrix+32)-.1f)<1e-6f &&
            At<float>(cam,kCamEyeRow)==100.0f+static_cast<float>(i);
    }
    check(stableRotation,"an independent eye-position write cannot accumulate an observation rotation over native no-write steps");
    observeCamera=false;CamStepHook(cam,nullptr);
    const M4 returned=MatrixAt(cam+kCamMatrix);
    check(std::memcmp(returned.m,nativeCamera.m,48)==0 && returned.m[12]==139,
          "observation returns only its rotation and preserves the outer owner's newer eye position");
    // Interleave the actual camera hook as the game does for local split-screen players.
    for(auto& state:states)state=State{};
    selected=&states[0];observeCamera=false;onFoot=true;camOk=drawOk=true;toggled=true;config.firstPerson=true;
    unsigned char p1[0x2100]{},p2[0x2100]{},c1[0x400]{},c2[0x400]{},view1[0xA0]{},view2[0xA0]{};
    int p1OwnerCtrl[4]={0,0,1,0},p2OwnerCtrl[4]={0,0,1,0};
    Put<void*>(p1,kSelfCtrl,p1OwnerCtrl);Put<void*>(p2,kSelfCtrl,p2OwnerCtrl);
    image=nativeImage;
    Put<const void*>(p1,0,image+kSoldierVts[0]);Put<const void*>(p2,0,image+kSoldierVts[0]);
    Put<void*>(c1,kCamTarget,p1);Put<void*>(c2,kCamTarget,p2);
    const auto aim1=fpview::Translate(10,0,30),aim2=fpview::Translate(-20,0,15);
    std::memcpy(p1+kAim,aim1.m,64);std::memcpy(p2+kAim,aim2.m,64);
    std::memcpy(p1+kBodyFrame,aim1.m,64);std::memcpy(p2+kBodyFrame,aim2.m,64);
    std::memcpy(c1+kCamMatrix,aim1.m,64);std::memcpy(c2+kCamMatrix,aim2.m,64);
    ++renderFrame;CamStepHook(c1,nullptr);CamStepHook(c2,nullptr);
    auto a=CameraState(c1),b=CameraState(c2);
    check(a && b && a!=b && a->on && b->on && a->human==p1 && b->human==p2,
          "interleaved P1/P2 native camera steps keep independent active viewmodels");
    // An independent camera owner replaces direction after our eye-only on-foot edit. Native must see that
    // direction at the next step; blindly restoring the previous 64-byte stock matrix erased it.
    M4 nativeSeen{}; // recorded through a stable address, not through a capturing native callback
    static M4* nativeSeenOut=nullptr;nativeSeenOut=&nativeSeen;
    nextCamStep=[](void* camera,void*){*nativeSeenOut=MatrixAt(static_cast<unsigned char*>(camera)+kCamMatrix);};
    const M4 newDirection=fpview::HeadingFrame(.8f,-.2f,MatrixAt(c1+kCamMatrix).m+12);
    std::memcpy(c1+kCamMatrix,newDirection.m,48);toggled=false;++renderFrame;CamStepHook(c1,nullptr);
    check(std::memcmp(nativeSeen.m,newDirection.m,48)==0 && !a->on,
          "leaving first person preserves another owner's newer camera direction for the actual native step");
    toggled=true;++renderFrame;CamStepHook(c1,nullptr);
    nextCamStep=[](void*,void*){};
    // Only this local seat may request the marker-backed mode, before the final camera is rendered.
    unsigned char requestSeat[kSeatStride]{};int requestVehicleCtrl[4]={0,0,1,0},requestRiderCtrl[4]={0,0,1,0};
    Put<void*>(rideVehicle,kSelfCtrl,requestVehicleCtrl);Put<void*>(p1,kSelfCtrl,requestRiderCtrl);
    Put<void*>(requestSeat,edf::kSeatRiderCtrl,requestRiderCtrl);
    Put<void*>(rideVehicle,kSeats,requestSeat);Put<std::uint64_t>(rideVehicle,kSeatCount,1);
    Put<void*>(requestSeat,kSeatRider,p1);Put<void*>(p1,kHumanVehicle,rideVehicle);onFoot=false;haveEye=true;
    check(FirstPersonVehicleView(rideVehicle,0),"the actual live local rider can request its marker-backed first-person mode");
    check(!FirstPersonVehicleView(rideVehicle,1),"a different or absent seat cannot borrow that first-person request");
    requestRiderCtrl[2]=0;check(!FirstPersonVehicleView(rideVehicle,0),"an expired occupant control cannot claim first-person camera ownership");requestRiderCtrl[2]=1;
    Put<void*>(p1,kSelfCtrl,requestVehicleCtrl);check(!FirstPersonVehicleView(rideVehicle,0),"a reused human address with another control cannot borrow the old seat request");Put<void*>(p1,kSelfCtrl,requestRiderCtrl);
    haveEye=false;check(!FirstPersonVehicleView(rideVehicle,0),"a seat without a physical eye cannot disable its native camera controller");haveEye=true;
    toggled=false;check(!FirstPersonVehicleView(rideVehicle,0),"turning first person off immediately removes its vehicle ownership request");toggled=true;
    onFoot=true;Put<void*>(p1,kHumanVehicle,nullptr);
    // Enemy control uses the same real camera step, with no human viewmodel, and never leaks P1's camera to P2.
    enemyCamera=true;enemyCameraHuman=p1;keyDown=true;{StateScope scope(a);Current().key={};Toggle(p1);}++renderFrame;CamStepHook(c1,nullptr);
    check(!a->on && At<float>(c1,kCamEyeRow)==70 && At<float>(c1,kCamEyeRow+4)==80 && At<float>(c1,kCamEyeRow+8)==90,
          "the local enemy-control camera takes priority over the human first-person eye and model");
    ++renderFrame;CamStepHook(c2,nullptr);
    check(b->on && At<float>(c2,kCamEyeRow)!=70,"another local camera retains its own first-person source during P1 enemy control");
    enemyCamera=false;enemyCameraHuman=nullptr;keyDown=false;++renderFrame;CamStepHook(c1,nullptr);
    check(a->on && toggled && At<float>(c1,kCamEyeRow)!=70,
          "enemy-control exit restores native camera input and resumes the requested first-person mode without a false key release");
    // Exercise actual interleaved camera entry points, not just Toggle: a controlled
    // viewport may cancel only its own identity while the shared preference flips once.
    const auto cameraFrame=[&](bool reverse) {
        ++renderFrame;
        if(reverse){CamStepHook(c2,nullptr);CamStepHook(c1,nullptr);}
        else {CamStepHook(c1,nullptr);CamStepHook(c2,nullptr);}
    };
    const auto freshCameraKeys=[&]() {
        keyDown=false;focused=true;mapOwnsKeys=false;mapKeyHuman=nullptr;
        keyPress=keyConsumed=0;keyPhysicalDown=false;keyClaimHuman=nullptr;keyClaimControl=nullptr;
        a->key={};b->key={};toggled=false;
    };
    for(bool reverse:{false,true}) {
        freshCameraKeys();enemyCamera=true;enemyCameraHuman=p1;cameraFrame(reverse);
        keyDown=true;cameraFrame(reverse);check(!toggled,"P2 FP key waits for release while P1 controls an enemy");
        keyDown=false;cameraFrame(reverse);
        check(toggled && !a->on && b->on,"P2 completes its own FP gesture during P1 control in either camera order");
    }
    freshCameraKeys();enemyCamera=false;enemyCameraHuman=nullptr;cameraFrame(false);
    keyDown=true;cameraFrame(false); // P1 really owned this press before control took over.
    enemyCamera=true;enemyCameraHuman=p1;cameraFrame(false);
    keyDown=false;cameraFrame(true);
    check(!toggled,"a P1 gesture interrupted by enemy control cannot transfer its release to P2");
    keyDown=true;cameraFrame(true);keyDown=false;cameraFrame(false);
    check(toggled && b->on,"P2 can start a fresh gesture after P1's interrupted press has drained");

    for(bool reverse:{false,true}) {
        freshCameraKeys();enemyCamera=true;enemyCameraHuman=p1;
        mapOwnsKeys=true;mapKeyHuman=p1;cameraFrame(reverse);
        keyDown=true;cameraFrame(reverse);keyDown=false;cameraFrame(reverse);
        check(toggled && b->on,"P1 map open or close-drain ownership does not cancel P2 FP input");
    }
    freshCameraKeys();enemyCamera=true;enemyCameraHuman=p1;cameraFrame(false);
    keyDown=true;cameraFrame(false);
    mapOwnsKeys=true;mapKeyHuman=p2;cameraFrame(true);cameraFrame(false); // map open, then its closing-key drain
    mapOwnsKeys=false;cameraFrame(true);keyDown=false;cameraFrame(false);
    check(!toggled,"P2's own map and close-drain discard its interrupted gesture through release");
    keyDown=true;cameraFrame(true);keyDown=false;cameraFrame(false);
    check(toggled,"a fresh P2 gesture works once its own map drain ends");

    freshCameraKeys();enemyCamera=true;enemyCameraHuman=p1;cameraFrame(false);
    keyDown=true;cameraFrame(true);
    int nextP2Control[4]={0,0,1,0};Put<void*>(p2,kSelfCtrl,nextP2Control);
    keyDown=false;cameraFrame(false);
    check(!toggled,"a reused human address with another control cannot inherit the old FP release");
    keyDown=true;cameraFrame(true);keyDown=false;cameraFrame(false);
    check(toggled,"the replacement human identity can start its own fresh FP gesture");
    Put<void*>(p2,kSelfCtrl,p2OwnerCtrl);cameraFrame(false);

    freshCameraKeys();enemyCamera=true;enemyCameraHuman=p1;cameraFrame(false);
    keyDown=true;cameraFrame(true);ResetFirstPerson();cameraFrame(false);
    a=CameraState(c1);b=CameraState(c2);
    keyDown=false;cameraFrame(true);
    check(a && b && !toggled,"mission reset consumes the held old-source gesture before new local cameras bind");
    keyDown=true;cameraFrame(true);keyDown=false;cameraFrame(false);
    check(toggled && !a->on && b->on,"fresh P2 input resumes after the mission boundary while P1 remains controlled");

    freshCameraKeys();enemyCamera=false;enemyCameraHuman=nullptr;cameraFrame(true);
    keyDown=true;cameraFrame(true);keyDown=false;++renderFrame;CamStepHook(c2,nullptr);
    check(toggled,"ordinary split cameras still share one first-person switch per complete gesture");
    ++renderFrame;CamStepHook(c1,nullptr);
    check(toggled,"a delayed second camera release cannot toggle the shared preference twice");
    cameraFrame(false);

    a->savedNear=.1f;a->nearSet=true;a->writtenNear=.03f;Put<float>(c1,kCamNear,.03f);
    depthPatchReady=false;++renderFrame;CamStepHook(c1,nullptr);
    check(std::fabs(At<float>(c1,kCamNear)-.1f)<1e-6f && !a->nearSet,
          "a failed/unavailable depth patch returns a previously modified first-person near plane to native");
    a->nearSet=true;a->savedNear=.1f;a->writtenNear=.03f;Put<float>(c1,kCamNear,.8f);
    {StateScope scope(a);ReturnNear(c1);}
    check(std::fabs(At<float>(c1,kCamNear)-.8f)<1e-6f && !a->nearSet,
          "returning first-person near ownership preserves a newer clip written by the outer map");
    Put<float>(c1,kCamNear,.1f);
    depthPatchReady=true;++renderFrame;CamStepHook(c1,nullptr);
    check(std::fabs(At<float>(c1,kCamNear)-config.fpNear)<1e-6f && a->nearSet,
          "a successfully created corrected depth shader enables the requested first-person near plane");
    const auto p1Motion=a->motion;a->reloadLeft=25;a->shotEvent[0].count=2;
    ++renderFrame;CamStepHook(c2,nullptr);
    check(a->reloadLeft==25 && a->shotEvent[0].count==2 && std::memcmp(&a->motion,&p1Motion,sizeof(p1Motion))==0,
          "P2 updates cannot consume P1 recoil, reload or motion state");
    auto camera1=MatrixAt(c1+kCamMatrix),camera2=MatrixAt(c2+kCamMatrix);M4 inverse1,inverse2;
    fpview::NativeView(camera1,&inverse1);fpview::NativeView(camera2,&inverse2);
    CaptureRenderCamera(c1,camera1.m);CaptureRenderCamera(c2,camera2.m);
    std::memcpy(view1+0x20,inverse1.m,64);std::memcpy(view2+0x20,inverse2.m,64);
    check(ViewState(view1)==a && ViewState(view2)==b,"render passes select their camera, independent of update order");
    Put<float>(c1,kCamEyeRow,123.0f);++renderFrame;
    check(ViewState(view1)==a,"render ownership uses the submitted camera even after the next camera step moves its eye");
    {
        // A draw lags the camera step by up to three update steps (docs/fp-render-re.md §17.2): turning fast, the frame
        // it renders is a few submits older than the newest. It is still that camera's (its own submit), at the same
        // tolerance: matching only the newest drew it stock (the body and gun flashing).
        const float up[3]={0,1,0};
        M4 turned[4],lagViews[4];unsigned char lag[0xA0]{};
        for(int k=0;k<4;++k) {
            const float angle=.3f*static_cast<float>(k+1);
            turned[k]=fpview::Mul(fpview::Rotation(up,std::cos(angle),std::sin(angle)),camera1);
            fpview::NativeView(turned[k],&lagViews[k]);CaptureRenderCamera(c1,turned[k].m);
        }
        bool all=true;
        for(int k=0;k<4;++k){std::memcpy(lag+0x20,lagViews[k].m,64);all=all && ViewState(lag)==a;}
        check(all,"a draw of a frame up to three submits old is still routed to its camera while it turns fast");
        M4 never=fpview::Mul(fpview::Rotation(up,std::cos(2.5f),std::sin(2.5f)),camera1),neverView;fpview::NativeView(never,&neverView);
        std::memcpy(lag+0x20,neverView.m,64);
        check(!ViewState(lag),"a view no camera submitted is not borrowed (the tolerance is unchanged)");
        for(int k=0;k<kViewHistory;++k)CaptureRenderCamera(c1,camera1.m);
        std::memcpy(lag+0x20,lagViews[0].m,64);
        check(!ViewState(lag) && ViewState(view1)==a,"the view history is bounded: a frame kViewHistory submits old is no longer its camera's");
    }
    {StateScope scope(a);int hand=0;DrawSnap snapA{};TakeSnapshot(&snapA);SnapScope taken(&snapA);
        check(PartOf(p1+kBodyModel,&hand)==Part::body && PartOf(p2+kBodyModel,&hand)==Part::none,
              "P2 remains a native world body when drawn in P1's viewport");
        const auto oldControl=At<const void*>(p1,kSelfCtrl);Put<void*>(p1,kSelfCtrl,p2OwnerCtrl);
        check(PartOf(p1+kBodyModel,&hand)==Part::none,
              "a delayed draw snapshot rejects an NPC/new actor reusing the local body model address with another owner");
        Put<const void*>(p1,kSelfCtrl,oldControl);p1[0x18]|=4;
        check(PartOf(p1+kBodyModel,&hand)==Part::none,"a logically retired native actor cannot be posed or actively resubmitted from an old snapshot");p1[0x18]&=static_cast<unsigned char>(~4);
        Drew1(p1+kBodyModel);
    }
    {StateScope scope(b);check(!Drew1Now(p1+kBodyModel),"opaque-pass completion records belong to the viewport");}
    {
        StateScope scope(a);const State keep=*a;
        Current()=State{};   // the game thread resets a State whole (Leave, a caught fault) while the pass is drawn
        check(Drew1Now(p1+kBodyModel),"the opaque pass's record is the draws' own: a game-thread State reset mid-pass leaves it");
        Current()=keep;
    }
    Put<void*>(p1+kBodyModel,0,vt);Put<void*>(p2+kBodyModel,0,vt);
    hooked[0]=Hooked{vt,&RecordBodyDraw};hookedCount=1;
    DrawHook(p1+kBodyModel,nullptr,1,view1);const bool ownMasked=drawArmMask;
    DrawHook(p2+kBodyModel,nullptr,1,view1);const bool foreignNative=!drawArmMask;
    DrawHook(p2+kBodyModel,nullptr,1,view2);const bool secondMasked=drawArmMask;
    check(ownMasked && foreignNative && secondMasked && !fpskin::Active(),
          "actual draw hook masks each viewport's own body, keeps the other player native, and restores mask state");
    hookedCount=0;
    // An outer camera owner changes only P1's final view. Its underlying FP eye remains the transition target,
    // but no old submitted matrix may force arms/guns into the map (or borrow P2's model).
    a->weapons[0]=model;a->weaponCtrls[0]=weaponOwnerCtrl;a->weaponCount=1;
    a->inHand[0]=model;a->inHandCtrls[0]=weaponOwnerCtrl;a->inHandCount=1;
    a->unbatched[0]=Unbatched{model,&batch,weaponOwnerCtrl};Put<void*>(model,kBatch,nullptr);
    a->stepNearSet=true;a->stepNear=.8f;a->writtenNear=.03f;Put<float>(c1,kCamNear,.03f);
    FirstPersonCameraOverride(c1,true);
    check(std::fabs(At<float>(c1,kCamNear)-.8f)<1e-6f,"the map retains its own near clip on the first takeover frame");
    DrawSnap overridden{};TakeSnapshotOf(static_cast<int>(a-states),&overridden);
    check(a->on && a->overridden && !overridden.on && b->on && !b->overridden,
          "the map suspends only its own viewport's viewmodel while retaining its underlying first-person eye");
    check(At<void*>(model,kBatch)==&batch && !a->unbatched[0].model,
          "taking the camera returns the weapon to the native batch before map drawing");
    CaptureRenderCamera(c1,camera1.m);
    check(!ViewState(view1) && ViewState(view2)==b,
          "a map camera and its retired first-person history cannot trigger pass completion or P2 arms");
    FirstPersonCameraOverride(c1,false);CaptureRenderCamera(c1,camera1.m);
    check(ViewState(view1)==a && !a->overridden && At<void*>(model,kBatch)==nullptr,
          "returning the camera resumes its first-person draw and unbatch ownership");
    {StateScope scope(a);Rebatch(true);a->weaponCount=a->inHandCount=0;}
    p1[kRagdoll]=1;
    check(NotWanted(c1,p1)==nullptr,"knockback retains the requested first-person camera");
    p1[kRagdoll]=0;Put<float>(c1,kCamEyeRow+4,20);Put<std::uint32_t>(p1,kActLimit,kSprints[0]);
    check(NotWanted(c1,p1)==nullptr,"Ranger sprint looking down cannot be mistaken for an Air Raider strike camera");
    Put<const void*>(p1,0,image+kSoldierVts[3]);
    check(GadgetView(c1,p1),"the actual Air Raider elevated strike view still yields to the native camera");
    p1[kRagdoll]=1;check(!GadgetView(c1,p1),"Air Raider knockback is not treated as a strike camera");
    {StateScope scope(a);Leave();}
    check(b->on && b->human==p2,"leaving P1 first person does not retire P2 state");
    check(!ViewState(view1) && ViewState(view2)==b,"a non-first-person split viewport cannot borrow the remaining player's arms");
    b->riding=true;b->screens.n=1;
    instruments::Readings reading{};reading.language=3;
    FirstPersonInstrumentReadings(reading,p1);
    check(!b->instrumentsReady,"P1 HUD readings cannot populate P2 instruments");
    FirstPersonInstrumentReadings(reading,p2);CockpitScreens screens{};
    check(b->instrumentsReady && b->instruments.language==3 && FirstPersonScreens(&screens,p2) && !FirstPersonScreens(&screens,p1),
          "cockpit feed and physical screens follow their exact human owner");
    {DrawSnap fed{};check(TakeSnapshotOf(static_cast<int>(b-states),&fed) && fed.instrumentsReady && fed.instruments.language==3,
                          "the cockpit feed reaches the instruments draw through the published copy");}
    check(!ArmBone(L"kata_l") && !ArmBone(L"arms_l") && ArmBone(L"jowan0_l") && ArmBone(L"kawan0_l"),
          "body topology excludes torso-side clavicle and mount faces while retaining complete upper arms");
    int nativeFlash=0;
    b->flashes[0].effect=&nativeFlash;b->flashes[0].model=model;b->flashes[0].m[12]=42;
    ResetFirstPerson();SyncSession();
    check(b->flashes[0].effect==&nativeFlash && !b->flashes[0].model && b->flashes[0].m[12]==42,
          "mission reset freezes the matrix still borrowed by a native effect until its actual destructor");
    flashClasses[0].next=reinterpret_cast<void*>(&RecordFlashDtor);
    FlashDtorHook<0>(&nativeFlash,1);
    check(!b->flashes[0].effect && retiredThis==&nativeFlash && retiredFlags==1,
          "the native primary destructor releases its matrix and forwards this/flags unchanged");
    flashClasses[0].next=nullptr;
    unsigned char secondaryFlash[0x150]{};b->flashes[0].effect=secondaryFlash;b->flashes[0].model=model;
    flashClasses[2].umbraNext=reinterpret_cast<void*>(&RecordFlashDtor);
    FlashUmbraDtorHook<2>(secondaryFlash+0x130,3);
    check(!b->flashes[0].effect && retiredThis==secondaryFlash+0x130 && retiredFlags==3,
          "the secondary destructor retires the root while forwarding the native adjusted-this thunk correctly");
    flashClasses[2].umbraNext=nullptr;
    check(!FirstPersonOn() && !CameraState(c1) && !CameraState(c2),"mission reset retires all local cameras without restoring freed native objects");
    {StateScope scope(a);check(!Drew1Now(p1+kBodyModel) && !ViewState(view2),"a new mission starts the draws' own records and the submitted views empty");}
    {
        // Production Fit on the native bone-record layout (docs/fp-render-re.md §17): the skeleton instance at
        // model+0xA0, records with the box flag +0x09, half sizes +0xF0, centre +0x100, world +0xB0. A rifle facing
        // four ways, its Umbra box (model+0x110) the game's world-axis one: the fitted grip is the same every way.
        unsigned char gunRecords[2*kBoneStride]{};
        auto gunModel=weapon+kWeaponModel;
        Put<void*>(gunModel+0xA0,kInstBones,gunRecords);Put<int>(gunModel+0xA0,kInstCount,2);
        float first[3]{};bool same=true,umbraMoved=false;float firstUmbra=0;
        for(int k=0;k<4;++k) {
            const float heading=k*1.5707963f,axis[3]={0,1,0};
            fpview::M4 root=fpview::Rotation(axis,std::cos(heading),std::sin(heading));root.m[12]=10;root.m[13]=2;root.m[14]=-4;
            std::memcpy(gunRecords+kBoneWorld,root.m,64);
            const fpview::M4 bodyBone=fpview::Mul(fpview::Translate(0,.03f,.05f),root);
            unsigned char* rec=gunRecords+kBoneStride;
            std::memcpy(rec+kBoneWorld,bodyBone.m,64);rec[0x09]=1;
            const float half[3]={.035f,.09f,.42f},centre[3]={0,.02f,.22f};
            std::memcpy(rec+0xF0,half,12);std::memcpy(rec+0x100,centre,12);
            fpview::BoneBox box{bodyBone,{half[0],half[1],half[2]},{centre[0],centre[1],centre[2]}};
            const fpview::M4 umbra=fpview::WorldBounds(&box,1);
            std::memcpy(gunModel+kModelBounds,umbra.m,64);
            if(k==0)firstUmbra=umbra.m[0];else umbraMoved=umbraMoved || std::fabs(umbra.m[0]-firstUmbra)>.1f;
            Current()=State{};Current().on=true;Current().human=human;Current().cam=cam;
            Current().inHand[0]=gunModel;Current().handOf[0]=kRight;Current().inHandCount=1;
            float grip[3]={-.1f,-.2f,.4f};Fit(kRight,false,grip);
            if(k==0)std::memcpy(first,grip,12);
            for(int i=0;i<3;++i)same=same && std::fabs(grip[i]-first[i])<1e-5f;
        }
        check(umbraMoved && same,"the hold is fitted to the weapon's own size (bone records), not the world-axis Umbra box: the same facing any way");
        Put<void*>(gunModel+0xA0,kInstBones,nullptr);Put<int>(gunModel+0xA0,kInstCount,0);
    }
    {
        // Threads (docs/fp-render-re.md §17.5): the draws run on a render thread while the camera step runs. A
        // selection made on one thread is not the other's, and a draw's copy is never half of one step and half of
        // the next (a writer publishing two different states as fast as it can, each with matching markers).
        for(auto& state:states)state=State{};
        selected=&states[0];
        State* mine=&states[0];State* other=&states[1];
        unsigned char camW[0x400]{};mine->cam=camW;
        // The two frames the writer submits by turns (CaptureRenderCamera, game thread) and their native views.
        const float up[3]={0,1,0};
        const M4 submit[2]={fpview::Translate(1,2,3),fpview::Mul(fpview::Rotation(up,std::cos(1.0f),std::sin(1.0f)),fpview::Translate(-4,5,6))};
        M4 expect[2];fpview::NativeView(submit[0],&expect[0]);fpview::NativeView(submit[1],&expect[1]);
        std::atomic<bool> stop{false};
        std::atomic<int> rounds{0};   // the reader starts once the writer has published and submitted (never vacuous)
        std::thread writer([&]{
            StateScope scope(mine);
            for(int k=0;!stop.load();++k) {
                const float v=static_cast<float>(k&1 ? 1 : 2);
                Current().on=true;Current().inHandCount=1;Current().handOf[0]=k&1;
                for(int h=0;h<2;++h){Current().view[h]=fpview::Translate(v,v,v);Current().placeWeapon[h]=fpview::Translate(v,v,v);}
                Current().ride.seat=k&1;Current().instrumentsReady=(k&1)!=0;Current().instruments.value[0]=v;
                for(int h=0;h<2;++h){Current().probeAnchor[h]=fpview::Translate(v,v,v);Current().probeAnchored[h]=(k&1)!=0;}
                Publish();
                CaptureRenderCamera(camW,submit[k&1].m);
                rounds.fetch_add(1);
            }
        });
        while(rounds.load()<2)std::this_thread::yield();
        bool consistent=true,ownSelection=true,viewsWhole=true;
        int viewsSeen=0;
        for(int i=0;i<20000;++i) {
            StateScope scope(mine);
            ViewHistory h{};
            if(TakeViews(0,&h)) {
                for(int k=0;k<kViewHistory;++k) {
                    if(!h.ok[k])continue;
                    ++viewsSeen;
                    viewsWhole=viewsWhole && (std::memcmp(h.view[k].m,expect[0].m,64)==0 || std::memcmp(h.view[k].m,expect[1].m,64)==0);
                }
            }
            DrawSnap snap{};
            if(!TakeSnapshot(&snap))continue;
            const float v=snap.view[0].m[12];
            const bool odd=v==1.0f;
            consistent=consistent && snap.view[1].m[12]==v && snap.placeWeapon[0].m[12]==v && snap.placeWeapon[1].m[12]==v &&
                       snap.handOf[0]==(odd ? 1 : 0) && snap.rideSeat==(odd ? 1 : 0) && snap.instrumentsReady==odd &&
                       snap.instruments.value[0]==v && snap.probeAnchor[0].m[12]==v && snap.probeAnchor[1].m[12]==v &&
                       snap.probeAnchored[0]==odd && snap.probeAnchored[1]==odd;
            {StateScope inner(other);ownSelection=ownSelection && &Current()==other;}
            ownSelection=ownSelection && &Current()==mine;
        }
        stop=true;writer.join();
        check(consistent,"a draw's copy of the camera step's state (seat, instruments, probe anchors too) is whole, never torn by a concurrent publish");
        check(viewsWhole && viewsSeen>0,"the submitted views a draw matches against are whole, never torn by a concurrent submit");
        check(ownSelection,"each thread keeps its own state selection (the camera step cannot switch a draw's camera)");
        for(auto& state:states)state=State{};
        selected=&states[0];
    }
    return failed ? 1 : 0;
}
