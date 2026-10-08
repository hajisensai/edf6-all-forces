// Production turret aim, muzzle transforms, round prediction and launcher frame on stand-in native memory.
// No game process or installed assets. Link src/rounds.cpp, src/launcher.cpp, edf6common and user32.
#include "../src/crew.h"
namespace crew { bool TurretTestRound(const unsigned char*,RoundModel*) noexcept; }
#define ReadRound TurretTestRound
#include "../src/turretcam.cpp"
#undef ReadRound
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace crew {
// Keep the real decoder for all native-memory fixtures. The rocket steering contract injects only its decoded
// kind: class/custom-parameter installation depends on validated executable signatures, outside this fixture.
const unsigned char* motorWeapon=nullptr;
bool TurretTestRound(const unsigned char* weapon,RoundModel* model) noexcept {
    const bool read=ReadRound(weapon,model);
    if(read && weapon==motorWeapon)model->kind=RoundKind::rocket;
    return read;
}
unsigned char* image=nullptr;
Config config{};
const Config& Cfg() noexcept { return config; }
ULONGLONG GameMs() noexcept { return 1000; }
void Log(const char*,...) noexcept {}
bool MapHoldsKeys() noexcept { return false; }
unsigned char* selectedGun=nullptr;
unsigned char* localHuman=nullptr;
unsigned char* PlayerHuman() noexcept{return localHuman;}
unsigned char* PayloadPicked(const void*) noexcept { return nullptr; } // legacy payload choice is not the sight target
unsigned char* PayloadSightPicked(const void*,unsigned) noexcept { return selectedGun; }
int owner=0;
int AutoTurretSteers(const void*,unsigned) noexcept { return owner; }
bool AutoTurretReadout(edf::aimlink::TurretReadoutV1*) noexcept { return false; }
bool StabHeld(const void*,float*,float*,float*) noexcept { return false; }
float SightZoomNow(const void*) noexcept { return 1.0f; }
bool mountedOptic=false;
bool SightZoomMounted(const void*) noexcept{return mountedOptic;}
bool highOn=true;
bool HighCamOn(const void*) noexcept { return highOn; }
int cameraReads=0;
bool CameraRay(float* eye,float* dir) noexcept {
    ++cameraReads;
    eye[0]=999;eye[1]=500;eye[2]=-999;dir[0]=0;dir[1]=-1;dir[2]=0;
    return true;
}
bool terrain=true;
float floorY=-25.0f;
float MapRay(const float* from,const float* to,float* hit) noexcept {
    if(!terrain || from[1]<floorY || to[1]>floorY || from[1]==to[1])return -1.0f;
    const float t=(floorY-from[1])/(to[1]-from[1]);
    for(int i=0;i<3;++i)hit[i]=from[i]+t*(to[i]-from[i]);
    return vec::Dist(from,hit);
}
int loftCalls=0;bool loftWanted=false;
void SetLauncherLoft(const void*,bool aim,float) noexcept { ++loftCalls;loftWanted=aim; }
bool LauncherLoft(const void*,LoftReadout*) noexcept { return false; }
void Unexpected() noexcept { static volatile bool fail=true;if(fail)std::abort(); }
bool SazabiCamera(const unsigned char*,const float*,const float*,float*,float*) noexcept { Unexpected();return false; }
bool indirectFixture=false;
bool IndirectFireSeat(const unsigned char*) noexcept { if(!indirectFixture)Unexpected();return true; }
void StabStep(void*,const float*,AimStepFn) noexcept { Unexpected(); }
bool IsPlayerJet(const void*) noexcept { return false; }
bool IsHelicopter(const void*) noexcept { return false; }
bool ProteusViewLift(const unsigned char*,float*,float*) noexcept { Unexpected();return false; }

namespace {
int checks=0,failures=0;
void Check(bool value,const char* name) {
    ++checks;if(!value){++failures;std::printf("FAIL %s\n",name);}
}
void Matrix(void* to,float x=0,float y=0,float z=0) {
    const float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1};std::memcpy(to,m,sizeof(m));
}
const float* __fastcall Gravity(void*) { static float g[4]={0,-9.8f,0,0};return g; }
void __fastcall UnexpectedAim(void*,const float*) { Unexpected(); }
struct Weapon {
    unsigned char data[0x1100]{},muzzle[edf::kMuzzleStride]{},bone[0x110]{},holder[0x20]{},weaponControl[0x10]{};
    Weapon(float x,float speed,float elev) {
        Put<const void*>(data,edf::kMuzzles,muzzle);Put<std::uint64_t>(data,edf::kMuzzleCount,1);
        Put<const void*>(muzzle,0,bone);Put<int>(muzzle,edf::kMuzzleMode,1);
        Matrix(muzzle+edf::kMuzzleLocal);Matrix(bone+edf::kBoneRows,x,8,10);Matrix(data+edf::kWeaponMatrix);
        Put<float>(bone,edf::kBoneRows+0x24,std::sin(elev));Put<float>(bone,edf::kBoneRows+0x28,std::cos(elev));
        Put<float>(data,edf::kWeaponAmmoSpeed,speed);Put<float>(data,edf::kWeaponAmmoGravity,1);
        Put<int>(data,edf::kWeaponAmmoAlive,1200);Put<void*>(holder,kHolderWeapon,data);Put<void*>(holder,kHolderCtrl,weaponControl);Put<int>(weaponControl,8,1);
    }
};
void Run() {
    config.debug=false;
    std::vector<unsigned char> module(0x20B2960);
    image=module.data();unsigned char world[0x80]{},physics[0x30]{};
    void* gravityVtable[]={reinterpret_cast<void*>(&Gravity)};
    Put<void*>(image,0x20B2958,world);Put<void*>(world,0x68,physics);Put<void*>(physics,0x20,gravityVtable);
    unsigned char vehicle[0x1100]{},vehicleBones[0x220]{},axisMap[2][0x38]{},seat[kSeatStride]{},human[0x360]{},control[0x10]{};
    localHuman=human;
    Matrix(vehicle+kMatrix);Put<void*>(vehicle,kSeats,seat);Put<std::uint64_t>(vehicle,kSeatCount,1);
    Put<void*>(seat,kSeatRider,human);Put<void*>(seat,kSeatRiderCtrl,control);Put<int>(control,edf::kCtrlUses,1);
    human[kHumanPlayer]=1;Put<void*>(human,kHumanPad,control);
    seat[kSeatPad]=1;Put<float>(seat,kSeatAim+kAimAxes,-tcam::kPi);Put<float>(seat,kSeatAim+kAimAxes+4,tcam::kPi);
    Put<float>(seat,kSeatAim+kAimAxes+kAxisStride,-1.5f);Put<float>(seat,kSeatAim+kAimAxes+kAxisStride+4,0.5f);
    Weapon primary(-4,2,0.2f),picked(7,1.6f,1.15f),elsewhere(30,1,0);
    // Native vehicle/weapon model bridge and axis mappings: this fixture is a genuinely articulated turret.
    Put<void*>(vehicle,weaponmount::kVehicleModel+0x10,vehicleBones);Put<int>(vehicle,weaponmount::kVehicleModel+0x20,2);
    Put<int>(vehicleBones,0xC,0);Put<int>(vehicleBones,0x10,-1);Put<int>(vehicleBones+0x110,0xC,1);Put<int>(vehicleBones+0x110,0x10,0);
    for(int i=0;i<2;++i){auto axis=seat+kSeatAim+kAimAxes+i*kAxisStride;Put<void*>(axis,0x28,axisMap[i]);Put<std::uint64_t>(axis,0x38,1);Put<int>(axisMap[i],0,1);Put<float>(axisMap[i],4,-1);Put<float>(axisMap[i],8,1);}
    for(auto w:{&primary,&picked}) {
        Put<void*>(w->holder,0x18,vehicleBones+0x110);Put<void*>(w->data,0xE88,vehicleBones+0x110);Put<void*>(w->data,0xF40,w->bone);
        Put<void*>(w->data,weaponmount::kWeaponModel+0x10,w->bone);Put<int>(w->data,weaponmount::kWeaponModel+0x20,1);
        Put<int>(w->bone,0xC,0);Put<int>(w->bone,0x10,-1);
    }
    unsigned char* holders[]={primary.holder,picked.holder};
    Put<void*>(seat,kSeatWeapons,holders);Put<std::uint64_t>(seat,kSeatWeaponCount,2);
    shared=Shared{};shared.v=vehicle;shared.seat=seat;shared.high=true;shared.decoupled=true;shared.seenMs=GameMs();
    selectedGun=picked.data;
    Check(Gun(vehicle,seat)==picked.data,"selected payload belongs to the seat");
    selectedGun=elsewhere.data;Check(Gun(vehicle,seat)==nullptr,"invalid fire-control target cannot silently select a different gun");
    selectedGun=picked.data;ShotFocus(shared);
    Check(shared.focusValid && shared.focusHit,"real muzzle round reaches lowered terrain");
    Check(std::fabs(shared.focus[1]-floorY)<0.001f && shared.focus[2]>300,"focus is high-arc impact, not camera ground point");
    Check(cameraReads==0,"shot focus never queries the camera");
    float first[3];std::memcpy(first,shared.focus,12);
    Put<float>(picked.data,edf::kWeaponAmmoOwnerMove,1);Put<float>(picked.data,edf::kWeaponOwnerVel,24);
    ShotFocus(shared);Check(shared.focus[0]>first[0]+50,"production predictor retains launcher's inherited motion");
    terrain=false;Put<int>(picked.data,edf::kWeaponAmmoAlive,30);ShotFocus(shared);
    Check(shared.focusValid && !shared.focusHit && shared.focus[1]>20,"no hit retains actual airborne lifetime endpoint");
    Put<int>(picked.data,kWeaponLockon,kHoming);ShotFocus(shared);
    Check(shared.focusValid && !shared.focusHit && shared.focus[1]>500,"guided/unpredictable shot falls back along real rail, not ground");
    Put<int>(picked.data,kWeaponLockon,0);
    for(bool coupled:{false,true}) {
        shared.decoupled=!coupled;shared.high=true;game=GameSide{};
        const float input[2]={0.3f,-0.2f};float cmd[2]={9,9};
        Aim(seat,input,cmd);
        Check(cmd[0]==input[0] && cmd[1]==input[1],"high camera preserves native gun input in either camera mode");
        Check(!shared.steering && !game.steer.hasWant,"observation never starts a screen-point steering loop");
        config.freeLookButton=0x10;Put<std::uint16_t>(seat,kSeatButtons,0x10);
        Put<float>(seat,kSeatStick,-0.5f);Put<float>(seat,kSeatStick+4,0.4f);
        Aim(seat,input,cmd);
        Check(cmd[0]==0 && cmd[1]==0 && shared.orbitYaw!=0,"free look orbits actual endpoint and holds native gun input");
        owner=1;Aim(seat,input,cmd);Check(cmd[0]==input[0] && cmd[1]==input[1],"foreign gun controller keeps ownership in free look");
        owner=0;Put<std::uint16_t>(seat,kSeatButtons,0);Put<float>(seat,kSeatStick,0);Put<float>(seat,kSeatStick+4,0);
    }
    Check(cameraReads==0,"high Aim never re-reads downward observation ray");
    shared.high=false;shared.view=false;shared.decoupled=false;
    float returned[2]{};const float neutral[2]{};
    Aim(seat,neutral,returned);
    Check(cameraReads==0 && !shared.highView && !shared.returning,
          "leaving coupled high mode seeds native axes without sampling the observation camera");
    terrain=true;Put<int>(picked.data,edf::kWeaponAmmoAlive,1200);Put<int>(picked.data,edf::kWeaponMark,edf::kMarkLofted);
    LauncherFrame(vehicle);
    Check(loftCalls==1 && !loftWanted && cameraReads==0,"high launcher releases camera-driven loft without consuming CameraRay");
    highOn=false;PublishObservation(seat,true);LauncherFrame(vehicle);
    Check(loftCalls==2 && !loftWanted && cameraReads==0,
          "exiting high camera keeps loft released while the camera blends even after native aim resumes");
    Check(TurretCamHighTransition(vehicle) && !TurretCamHighTransition(human),"observation belongs only to this vehicle");
    PublishObservation(seat,false);LauncherFrame(vehicle);
    Check(loftCalls==3 && cameraReads>0,"normal launcher retains the existing camera-to-loft path once blending ends");
    // Identical real muzzle and target: only a freely falling shell may request a low arc or a dropped reticle.
    selectedGun=primary.data;shared.high=false;
    const float frame[9]={1,0,0,0,1,0,0,0,1},target[3]={-4,8,210};
    float boreWant[2],shellWant[2],guidedWant[2],motorWant[2];
    Check(Wants(seat,frame,target,false,boreWant),"normal sight has a bore-line control");
    Check(Wants(seat,frame,target,true,shellWant) && shellWant[1]<boreWant[1]-0.01f,
          "ordinary cannon keeps its low-arc elevation compensation");
    Readout(seat,shared,true,target,false);const TurretCamReadout boreReadout=out.r;
    Readout(seat,shared,true,target,true);
    Check(out.r.gun[1]<boreReadout.gun[1]-1,"ordinary cannon readout retains gravity drop");
    Put<int>(primary.data,kWeaponLockon,kHoming);
    Check(Wants(seat,frame,target,true,guidedWant) && std::fabs(guidedWant[1]-boreWant[1])<1e-6f,
          "guided missile cannot acquire a free-fall low-arc command");
    Readout(seat,shared,true,target,true);
    Check(vec::Dist(out.r.gun,boreReadout.gun)<1e-5f,"guided missile reticle remains on its bore line");
    Put<int>(primary.data,kWeaponLockon,0);motorWeapon=primary.data;
    Check(Wants(seat,frame,target,true,motorWant) && std::fabs(motorWant[1]-boreWant[1])<1e-6f,
          "propelled rocket cannot acquire a free-fall low-arc command");
    Readout(seat,shared,true,target,true);
    Check(vec::Dist(out.r.gun,boreReadout.gun)<1e-5f,"propelled rocket reticle remains on its bore line");
    motorWeapon=nullptr;Put<int>(primary.data,edf::kWeaponMark,edf::kMarkLofted);
    Check(Wants(seat,frame,target,true,shellWant) && std::fabs(shellWant[1]-boreWant[1])<1e-6f,
          "lofted launcher retains its independent elevation controller");
    Readout(seat,shared,true,target,true);
    Check(vec::Dist(out.r.gun,boreReadout.gun)<1e-5f,"lofted launcher readout matches its independent bore-line command");
    // Exercise the production frame sampler too: a closed toggle is not sufficient while the displayed camera is
    // still blending out. Nonzero stick and a new free-look press used to clear the timer and consume that old ray.
    lookOk=true;nextAim=&UnexpectedAim;shared.ref=ObjRef::Of(vehicle);Put<int>(seat,kSeatCamType,1);
    for(bool decoupled:{false,true}) {
        config.decoupledTurretCam=decoupled;cameraReads=0;game=GameSide{};
        shared.high=false;shared.highView=true;shared.observing=true;shared.view=false;
        shared.free=false;shared.returning=false;
        game.hasAim=true;game.aimHit=true;game.aim[0]=999;game.aim[1]=-100;game.aim[2]=-999;
        Put<float>(seat,kSeatStick,-0.3f);Put<float>(seat,kSeatStick+4,-0.2f);
        const float input[2]={0.3f,-0.2f};float cmd[2]{};
        TurretCamFrame(vehicle);
        Check(cameraReads==0 && !game.hasAim,"return blend does not sample the overhead camera ray");
        Aim(seat,input,cmd);
        Check(cameraReads==0 && cmd[0]==input[0] && cmd[1]==input[1] && !shared.steering,
              "nonzero native stick cannot reactivate screen-point steering during the return blend");
        Put<std::uint16_t>(seat,kSeatButtons,0x10);Aim(seat,input,cmd);
        Check(cameraReads==0 && cmd[0]==0 && cmd[1]==0,
              "free-look entry during the blend cannot sample the overhead ray");
        Put<std::uint16_t>(seat,kSeatButtons,0);Aim(seat,input,cmd);
        Check(cameraReads==0 && cmd[0]==input[0] && cmd[1]==input[1] && !shared.returning,
              "free-look release during the blend returns directly to native input");
        PublishObservation(seat,false);TurretCamFrame(vehicle);
        Check(cameraReads==1 && game.hasAim,"normal frame resumes screen sampling only after observation ends");
        Aim(seat,input,cmd);
        Check(shared.steering==decoupled,"normal camera ownership resumes after the return blend");
    }
    indirectFixture=true;config.decoupledTurretCam=true;shared.high=false;shared.highView=false;shared.observing=false;shared.returning=false;
    Put<float>(seat,kSeatAim+kAimAxes,0);Put<float>(seat,kSeatAim+kAimAxes+4,0);
    TurretCamFrame(vehicle);float input[2]={.2f,-.2f},command[2]{};Aim(seat,input,command);
    Check(shared.physicalOnly && !shared.decoupled && !shared.steering,"pitch-only artillery cannot acquire a freely movable camera aim");
    // The hooked frame runs for every vehicle. An unavailable current optic must not oscillate take/drop.
    ResetTurretCam();lookOk=true;selectedGun=nullptr;
    for(int i=0;i<3;++i){TurretCamFrame(vehicle);Check(!TurretCamServes(vehicle),"unavailable optic never alternates initial fallback and bound rejection");}
    selectedGun=primary.data;TurretCamFrame(vehicle);const unsigned take=shared.take;
    rigInfo.v=vehicle;rigInfo.normalRadius=30;
    unsigned char otherVehicle[0x1100]{},otherSeat[kSeatStride]{},otherHuman[0x500]{};
    std::memcpy(otherVehicle,vehicle,sizeof(vehicle));std::memcpy(otherSeat,seat,sizeof(seat));
    Put<void*>(otherVehicle,kSeats,otherSeat);Put<void*>(otherSeat,kSeatRider,otherHuman);otherHuman[kHumanPlayer]=1;Put<void*>(otherHuman,kHumanPad,control);
    for(int i=0;i<3;++i){TurretCamFrame(otherVehicle);Check(shared.v==vehicle && shared.take==take && TurretCamLarge(vehicle),"another local player's frame cannot replace camera owner or large rig");TurretCamFrame(vehicle);}
    mountedOptic=true;cameraReads=0;game.hasAim=game.steer.hasWant=true;
    shared.high=false;shared.observing=false;shared.highView=false;shared.view=shared.decoupled=shared.steering=true;
    owner=1;Aim(seat,input,command);
    Check(command[0]==input[0] && command[1]==input[1],"mounted optic preserves native or external aim input without scaling it");
    Check(!game.hasAim && !game.steer.hasWant && !shared.view && !shared.steering && !shared.decoupled && cameraReads==0,
          "mounted optic cannot feed its own CameraRay into the turret controller");
    camSide.owned=true;camSide.seat=seat;float opticTarget[20]{};unsigned char opticCamera[0x700]{};
    Camera(seat,opticTarget,opticCamera);Check(!camSide.owned,"mounted optic bypasses third-person camera placement and blend");
    TurretCamFrame(vehicle);Check(cameraReads==0&&!shared.decoupled,"frame sampler stays off the mounted optic ray");
    mountedOptic=false;owner=0;TurretCamFrame(vehicle);Aim(seat,input,command);
    Check(cameraReads>0 && shared.view,"leaving optic reinitializes the regular view from current camera state");
    image=nullptr;
}
}  // namespace
}  // namespace crew
int main() {
    crew::Run();std::printf("turret_cam_runtime_check: %d checks, %d failed\n",crew::checks,crew::failures);
    return crew::failures ? 1 : 0;
}
