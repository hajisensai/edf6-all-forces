// Production turret aim, muzzle transforms, round prediction and launcher frame on stand-in native memory.
// No game process or installed assets. Link src/rounds.cpp, src/launcher.cpp, edf6common and user32.
#include "../src/crew.h"
namespace crew {
bool observation=false;
bool VehicleLookActive(const void*) noexcept { return observation; }
bool lookOffered=true;   // vehicle_look.cpp VehicleLookOffered: false where the stock vehicle reads the button (a Grape's L3 horn)
bool VehicleLookOffered(const void*,unsigned,bool) noexcept { return lookOffered; }
bool TurretTestRound(const unsigned char*,RoundModel*) noexcept; }
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
ULONGLONG stepNow=100;   // the scene's step (crew.cpp GameFrame): the chase follower's clock, 1/60 s a step
ULONGLONG GameFrame() noexcept { return stepNow; }
void Log(const char*,...) noexcept {}
bool mapKeys=false,mapView=false;
bool MapHoldsKeys() noexcept { return mapKeys; }
bool MapOwnsView() noexcept { return mapView; }
const void* firstPersonVehicle=nullptr;unsigned firstPersonSeat=0;
bool FirstPersonVehicleView(const void* vehicle,unsigned seat) noexcept { return vehicle==firstPersonVehicle && seat==firstPersonSeat; }
unsigned char* selectedGun=nullptr;
unsigned char* localHuman=nullptr;
unsigned char* PlayerHuman() noexcept{return localHuman;}
unsigned char* PayloadPicked(const void*) noexcept { return nullptr; } // legacy payload choice is not the sight target
unsigned char* PayloadSightPicked(const void*,unsigned) noexcept { return selectedGun; }
int owner=0;
int AutoTurretSteers(const void*,unsigned) noexcept { return owner; }
// The one player turret aim (turretaim.cpp PlayerTurretLead, aimlink.h V4) as the camera asks it: every frame, with the
// gun the turret turns; `leadOn` the peer's answer (AUTO with a lock), `leadPoint` where the round meets the lock.
int leadAsks=0;bool leadOn=false;float leadPoint[3]{};const void* leadGun=nullptr;
bool PlayerTurretLead(const void*,unsigned,const void* gun,float* point) noexcept {
    ++leadAsks;leadGun=gun;if(leadOn)std::memcpy(point,leadPoint,12);return leadOn;
}
bool AutoTurretReadout(edf::aimlink::TurretReadoutV1*) noexcept { return false; }
// stab.cpp StabHeld with no stabilizer holding the gun: the axes as they are, no hull turn.
bool StabHeld(const void* aim,float* held,float* hull,float*) noexcept {
    const auto* a=static_cast<const unsigned char*>(aim);
    held[0]=At<float>(a,kAimAxes+kAxisAngle);held[1]=At<float>(a,kAimAxes+kAxisStride+kAxisAngle);hull[0]=hull[1]=0.0f;
    return false;
}
float SightZoomNow(const void*) noexcept { return 1.0f; }
bool mountedOptic=false;
bool SightZoomMounted(const void*) noexcept{return mountedOptic;}
// The optic present on seat 0 (sightzoom.cpp SightZoomCanMount) and raised (SightZoomRaised): with both,
// the gunner's sight is up and the free-look key is not the turret camera's (TurretCamFreeLook).
bool canMount=false,gunnerSight=false;
bool SightZoomCanMount(const void*,unsigned) noexcept{return canMount;}
bool SightZoomRaised(const void*) noexcept{return gunnerSight;}   // sightzoom.cpp: first person's sight raised (Press)
bool highOn=true;
bool HighCamOn(const void*) noexcept { return highOn; }
int cameraReads=0;
bool CameraRay(float* eye,float* dir) noexcept {
    ++cameraReads;
    eye[0]=999;eye[1]=500;eye[2]=-999;dir[0]=1;dir[1]=0;dir[2]=0;
    return true;
}
bool terrain=true;
float floorY=-25.0f;
bool wall=false;float wallZ=0.0f;   // a wall across the world at z = wallZ (a building behind the vehicle)
float groundEdge=INFINITY;           // the map's ground ends groundEdge m across from groundCentre: the void past it
float groundCentre[2]={0.0f,0.0f};
PlayArea MapPlayArea() noexcept { return PlayArea{{-2400.0f,-2400.0f},{2400.0f,2400.0f},false,0.0f,false}; }
float MapRay(const float* from,const float* to,float* hit) noexcept {
    float t=2.0f;
    if(terrain && from[1]>=floorY && to[1]<=floorY && from[1]!=to[1]) {
        t=(floorY-from[1])/(to[1]-from[1]);
        const float x=from[0]+t*(to[0]-from[0])-groundCentre[0],z=from[2]+t*(to[2]-from[2])-groundCentre[1];
        if(!(std::sqrt(x*x+z*z)<=groundEdge))t=2.0f;
    }
    if(wall && from[2]!=to[2]) {
        const float w=(wallZ-from[2])/(to[2]-from[2]);
        if(w>=0.0f && w<t)t=w;
    }
    if(t>1.0f)return -1.0f;
    for(int i=0;i<3;++i)hit[i]=from[i]+t*(to[i]-from[i]);
    return vec::Dist(from,hit);
}
int loftCalls=0;bool loftWanted=false;float loftElevation=0.0f;
void SetLauncherLoft(const void*,bool aim,float elevation) noexcept { ++loftCalls;loftWanted=aim;loftElevation=elevation; }
// katyusha.cpp's Rack (the Katyusha's rack posed off its axes): only `rackPosed` vehicles have one; the V603's lofted
// guns (the TOS-1, the X-Morph missile launcher) do not.
bool rackPosed=false;
bool LauncherLoft(const void*,LoftReadout* readout) noexcept { if(rackPosed)*readout=LoftReadout{}; return rackPosed; }
int AndryushaSeat(const unsigned char*,const unsigned char*,unsigned char*) noexcept { return 0; }   // andryusha.cpp: not an Andryusha
void Unexpected() noexcept { static volatile bool fail=true;if(fail)std::abort(); }
bool PlayerHeliCamera(const unsigned char*,const float*,const float*,float*,float*) noexcept { Unexpected();return false; }
bool GunshipGunnerCamera(const unsigned char*,const float*,const float*,float*,float*) noexcept { return false; }
bool DreadnoughtCamera(const unsigned char*,float*,float*) noexcept { return false; }
bool SeatTurretCamera(const unsigned char*,float*,float*) noexcept { return false; }   // seat_turret.cpp: no gunner-seat turrets here
bool SazabiCamera(const unsigned char*,const float*,const float*,float*,float*) noexcept { Unexpected();return false; }
bool indirectFixture=false;
bool IndirectFireSeat(const unsigned char*) noexcept { if(!indirectFixture)Unexpected();return true; }
void StabStep(void*,const float*,AimStepFn) noexcept { Unexpected(); }
bool jetFixture=false;   // playerjet.cpp IsPlayerJet / PlayerRotorCraft: the chase fixture's craft
bool IsPlayerJet(const void*) noexcept { return jetFixture; }
bool PlayerRotorCraft(const void*) noexcept { return false; }
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
float* __fastcall CameraPoint(const void*,float* rows) { Matrix(rows,0,5,-20);return rows; }
// Query boundary reproduces F6760: native collision pivot, not a vehicle anchor.
// The native audit executes the same routine privately with these boundary responses.
void NativeCameraClip(float* eye,float* look,const float* desired,float floor,float margin,int policy) {
    bool firstHit=false;
    const auto hit=[&](const float* from,int query,float* corrected) {
        const bool surface=from[1]<=floor+1e-4f;
        const bool yes=(query==0 ? policy==2 && surface : policy!=0 && (surface || firstHit));
        if(yes){corrected[0]=from[0];corrected[1]=floor+margin;corrected[2]=from[2];}
        return yes;
    };
    if(vec::Dist(look,desired)<10)firstHit=hit(desired,0,look);
    hit(look,1,eye);
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
// 2026-11-21 (「载具/玩家之类的视角优化一下」): the chase follower (chase_cam.h) as the camera hook runs it.
void ChaseChecks(unsigned char* vehicle,unsigned char* seat,unsigned char* human,unsigned char* control,unsigned char* gun) {
    float* const m=reinterpret_cast<float*>(vehicle+kMatrix);
    struct Trace { float dir[3],eye[3]; bool ray; };
    // The turret's normal view on a hull that drives 15 m/s for 1.5 s over a rough patch and stops: once with the follower,
    // once without, the same input.
    const auto drive=[&](bool chaseOn,std::vector<Trace>& trace,bool high) {
        ResetTurretCam();lookOk=true;nextAim=&UnexpectedAim;selectedGun=gun;config.decoupledTurretCam=true;config.chaseCam=chaseOn;
        highOn=false;mountedOptic=false;firstPersonVehicle=nullptr;mapKeys=mapView=observation=false;owner=0;
        Put<void*>(seat,kSeatRider,human);localHuman=human;
        Put<float>(seat,kSeatStick,0);Put<float>(seat,kSeatStick+4,0);
        Matrix(vehicle+kMatrix);
        unsigned char camera[0x700]{};float targets[20]{};
        const float noInput[2]{};float noCommand[2]{};
        for(int frameIndex=0;frameIndex<200;++frameIndex) {
            ++stepNow;
            if(frameIndex>=20 && frameIndex<110)m[14]+=0.25f;
            if(frameIndex>=40 && frameIndex<70)m[13]+=frameIndex%2 ? 0.08f : -0.08f;
            highOn=high && frameIndex>=120 && frameIndex<150;
            TurretCamFrame(vehicle);Aim(seat,noInput,noCommand);std::memset(targets,0,sizeof(targets));Camera(seat,targets,camera);
            if(high && frameIndex==140)Check(!camSide.chase.valid,"the high view never runs the follower (its own centre eases)");
            if(high && frameIndex==150)Check(camSide.chase.valid && vec::Dist(camSide.chase.pos,m+12)<1e-4f,
                                             "back from the high view the follower starts over on the hull");
            AcquireSRWLockShared(&lock);const Shared s=shared;ReleaseSRWLockShared(&lock);
            Trace t{};t.ray=s.normalRay && !s.resume;std::memcpy(t.dir,s.normalDir,12);std::memcpy(t.eye,s.normalEye,12);trace.push_back(t);
        }
    };
    std::vector<Trace> on,off,highTrace;
    drive(true,on,false);drive(false,off,false);
    bool sameRay=on.size()==off.size(),rays=true;float jolt[2]={0.0f,0.0f};
    for(std::size_t i=20;i<on.size() && i<off.size();++i) {
        rays=rays && on[i].ray && off[i].ray;
        sameRay=sameRay && vec::Dist(on[i].dir,off[i].dir)<1e-5f;
        if(i>40 && i<70){jolt[0]=std::fmax(jolt[0],std::fabs(on[i].eye[1]-on[i-1].eye[1]));jolt[1]=std::fmax(jolt[1],std::fabs(off[i].eye[1]-off[i-1].eye[1]));}
    }
    Check(rays,"the normal view publishes its own ray every frame with the follower on");
    Check(sameRay,"the follower lags the eye only: the ray handed to the turret (PublishNormalRay / AimPoint) keeps the view's direction");
    const float behind=off[100].eye[2]-on[100].eye[2];
    Check(behind>1.0f && behind<2.0f,"driving 15 m/s the eye trails the hull by about v / omega (kGround), not rigidly on it");
    Check(jolt[0]<0.5f*jolt[1],"a jolting hull shakes the followed eye less than half as much");
    Check(vec::Dist(on.back().eye,off.back().eye)<1e-3f,"stopped, the followed eye settles where the rigid one is");
    drive(true,highTrace,true);
    config.chaseCam=true;highOn=false;

    // The camera hook's own followers: a seat whose rider is this camera's player.
    unsigned char rider[0x1600]{},riderCtrl[0x10]{},chaseCamera[0x700]{};
    rider[kHumanPlayer]=1;Put<void*>(rider,kHumanPad,control);
    Put<std::int32_t>(riderCtrl,8,1);Put<void*>(rider,kHumanVehicleCtrl-8,vehicle);Put<void*>(rider,kHumanVehicleCtrl,riderCtrl);
    Put<void*>(seat,kSeatRider,rider);Put<void*>(chaseCamera,kCamTarget,rider);
    ResetTurretCam();Matrix(vehicle+kMatrix);jetFixture=true;
    float stockEye[3]={0,5,-20},stockLook[3]={0,2,10},eye[3],look[3];
    ++stepNow;
    Check(PlayerJetChase(chaseCamera,seat,stockEye,stockLook,eye,look) && vec::Dist(eye,stockEye)<1e-5f && vec::Dist(look,stockLook)<1e-5f,
          "the player's jet: chased from the stock locators' points, on them as it starts");
    for(int i=0;i<240;++i) {
        ++stepNow;m[14]+=100.0f/60.0f;stockEye[2]+=100.0f/60.0f;stockLook[2]+=100.0f/60.0f;
        PlayerJetChase(chaseCamera,seat,stockEye,stockLook,eye,look);
    }
    Check(vec::Dist(eye,stockEye)<1e-2f && vec::Dist(look,stockLook)<1e-2f,"flying straight at 100 m/s: on the stock points, no lag for the speed");
    const auto viewRows=[&](float* rows) {
        float f[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]};vec::Normalize(f);
        float l[3],u[3];chase::LevelFrame(f,l,u);
        std::memset(rows,0,64);
        for(int i=0;i<3;++i){rows[i]=l[i];rows[4+i]=u[i];rows[8+i]=f[i];rows[12+i]=eye[i];}
    };
    const auto bankOf=[](const float* rows){return std::atan2(rows[1],rows[5])*180.0f/tcam::kPi;};   // cockpit_instruments.h BankOf
    float rows[16];viewRows(rows);ChaseCamUp(chaseCamera,rows);
    Check(std::fabs(bankOf(rows))<1e-3f,"wings level: the picture is not rolled");
    // Banked 30 deg (right wing down), the locators turning with the airframe round its origin.
    const float b=30.0f*tcam::kPi/180.0f,origin[3]={m[12],m[13],m[14]};
    m[0]=std::cos(b);m[1]=std::sin(b);m[2]=0;m[4]=-std::sin(b);m[5]=std::cos(b);m[6]=0;
    for(int i=0;i<3;++i){stockEye[i]=origin[i]+m[4+i]*5.0f-m[8+i]*20.0f;stockLook[i]=origin[i]+m[4+i]*2.0f+m[8+i]*10.0f;}
    for(int i=0;i<300;++i){++stepNow;PlayerJetChase(chaseCamera,seat,stockEye,stockLook,eye,look);}
    viewRows(rows);ChaseCamUp(chaseCamera,rows);
    Check(std::fabs(bankOf(rows)-0.4f*30.0f)<1.5f,"banked 30 deg: the picture rolls about 40% of it (kJet), the same way as BankOf");
    Check(std::fabs((eye[0]-origin[0])+5.0f*std::sin(0.4f*b))<0.05f,"the eye over the craft swings round it by that share, not welded to the airframe");
    float again[16];viewRows(again);std::memcpy(rows,again,64);ChaseCamUp(chaseCamera,rows);
    Check(std::memcmp(rows,again,64)==0,"a step's roll is used once");
    PlayerJetChase(chaseCamera,seat,stockEye,stockLook,eye,look);++stepNow;viewRows(rows);std::memcpy(again,rows,64);ChaseCamUp(chaseCamera,rows);
    Check(std::memcmp(rows,again,64)==0,"a roll from an earlier step is never applied");
    firstPersonVehicle=vehicle;firstPersonSeat=0;
    Check(!PlayerJetChase(chaseCamera,seat,stockEye,stockLook,eye,look),"first person: no chase (the cockpit's bone)");
    firstPersonVehicle=nullptr;mountedOptic=true;
    Check(!PlayerJetChase(chaseCamera,seat,stockEye,stockLook,eye,look),"the mounted optic: no chase");
    mountedOptic=false;config.chaseCam=false;
    Check(!PlayerJetChase(chaseCamera,seat,stockEye,stockLook,eye,look),"ChaseCam=0: no chase");
    config.chaseCam=true;Put<void*>(chaseCamera,kCamTarget,human);
    Check(!PlayerJetChase(chaseCamera,seat,stockEye,stockLook,eye,look),"another player's camera never follows this seat");
    Put<void*>(chaseCamera,kCamTarget,rider);jetFixture=false;
    Check(!PlayerJetChase(chaseCamera,seat,stockEye,stockLook,eye,look),"not one of the player's jets: not chased as one");
    // A ground seat (the Sazabi's, a gunner's): the origin sprung, the direction the input's.
    Matrix(vehicle+kMatrix);ForgetFollower(chaseCamera);
    bool direction=true;float trail=0.0f;
    for(int i=0;i<120;++i) {
        ++stepNow;m[12]+=0.2f;
        const float yaw=0.02f*i;
        float e[3]={m[12]-20.0f*std::sin(yaw),m[13]+6.0f,m[14]-20.0f*std::cos(yaw)},l[3]={m[12],m[13]+2.0f,m[14]};
        const float want[3]={l[0]-e[0],l[1]-e[1],l[2]-e[2]},rigid[3]={e[0],e[1],e[2]};
        direction=direction && FollowSeat(chaseCamera,seat,ChaseKind::ground,e,l);
        const float got[3]={l[0]-e[0],l[1]-e[1],l[2]-e[2]};
        direction=direction && vec::Dist(want,got)<1e-4f;
        trail=vec::Dist(rigid,e);
    }
    Check(direction,"a ground seat's follower keeps the input's view direction exactly");
    Check(trail>0.5f,"... and lags the moving vehicle");
    ResetTurretCam();
    Put<void*>(seat,kSeatRider,human);
}

void Run() {
    config.debug=false;
    std::vector<unsigned char> module(0x20B2960);
    image=module.data();unsigned char world[0x80]{},physics[0x30]{};
    unsigned char pointJump[14]={0xff,0x25,0,0,0,0};const auto pointFn=reinterpret_cast<std::uintptr_t>(&CameraPoint);
    std::memcpy(pointJump+6,&pointFn,8);DWORD protection=0;VirtualProtect(image+kPoint,14,PAGE_EXECUTE_READWRITE,&protection);
    std::memcpy(image+kPoint,pointJump,14);FlushInstructionCache(GetCurrentProcess(),image+kPoint,14);
    void* gravityVtable[]={reinterpret_cast<void*>(&Gravity)};
    Put<void*>(image,0x20B2958,world);Put<void*>(world,0x68,physics);Put<void*>(physics,0x20,gravityVtable);
    unsigned char vehicle[0x1100]{},vehicleBones[0x220]{},axisMap[2][0x38]{},seat[kSeatStride]{},human[0x360]{},control[0x10]{};
    localHuman=human;
    Matrix(vehicle+kMatrix);Put<void*>(vehicle,kSeats,seat);Put<std::uint64_t>(vehicle,kSeatCount,1);
    Put<float>(vehicle,kObjCamLook+4,2);Put<float>(vehicle,kObjCamEye+4,5);Put<float>(vehicle,kObjCamEye+8,-20);
    const float normalEye[3]={0,5,-20},normalLook[3]={0,2,100};
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
    // No landing (2026-10-09, the user: "稍微远就跳到了指数级别的距离"): the ground under where the shot ends, within its
    // reach, never its point in the air (that put the view hundreds of metres up and away); no ground found: the hull's.
    const float* hull=reinterpret_cast<const float*>(vehicle+kMatrix)+12;
    const auto across=[&]{ return std::sqrt((shared.focus[0]-hull[0])*(shared.focus[0]-hull[0])+(shared.focus[2]-hull[2])*(shared.focus[2]-hull[2])); };
    Put<int>(picked.data,edf::kWeaponAmmoAlive,30);ShotFocus(shared);
    float airborne[3];std::memcpy(airborne,shared.focus,12);
    Check(shared.focusValid && !shared.focusHit && std::fabs(shared.focus[1]-floorY)<0.001f,"no landing: the ground under the shot's end");
    // The map's ground ends short of it (2026-10-25, "俯瞰视角的移动视角会一下去很远的地方，而且动的时候地图会整个变灰一下"): the
    // farthest point along the line still over ground (tcam::GroundReach), never the void past the edge.
    const float shotDir[2]={airborne[0]-hull[0],airborne[2]-hull[2]};
    groundCentre[0]=hull[0];groundCentre[1]=hull[2];groundEdge=0.5f*across();ShotFocus(shared);
    Check(shared.focusValid && !shared.focusHit && std::fabs(shared.focus[1]-floorY)<0.001f && across()<=groundEdge &&
          across()>groundEdge-tcam::kGroundFine-15.0f,"no landing past the ground's edge: the last ground along the shot");
    Check(std::fabs((shared.focus[0]-hull[0])*shotDir[1]-(shared.focus[2]-hull[2])*shotDir[0])<0.05f*across()*std::hypot(shotDir[0],shotDir[1])+15.0f,
          "... on the shot's own line");
    groundEdge=INFINITY;
    terrain=false;ShotFocus(shared);
    Check(shared.focusValid && !shared.focusHit && std::fabs(shared.focus[1]-hull[1])<0.001f && across()<15.0f,
          "no landing, no ground anywhere: back at the muzzle (10 m ahead of the hull) at the hull's height, never the shot's end in the air or the void");
    Put<int>(picked.data,kWeaponLockon,kHoming);ShotFocus(shared);
    Check(shared.focusValid && !shared.focusHit && std::fabs(shared.focus[1]-hull[1])<0.001f && across()<15.0f,
          "guided/unpredictable shot: under the real rail within kAimFar, not up in the air");
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
        owner=0;lookOffered=false;game=GameSide{};shared.orbitYaw=0;Aim(seat,input,cmd);
        Check(cmd[0]==input[0] && cmd[1]==input[1] && shared.orbitYaw==0,
              "the button is the stock vehicle's there (a Grape's L3 horn): no free look, native gun input kept");
        lookOffered=true;
        owner=0;Put<std::uint16_t>(seat,kSeatButtons,0);Put<float>(seat,kSeatStick,0);Put<float>(seat,kSeatStick+4,0);
    }
    Check(cameraReads==0,"high Aim never re-reads downward observation ray");
    shared.high=false;shared.view=false;shared.decoupled=false;
    float returned[2]{};const float neutral[2]{};
    Aim(seat,neutral,returned);
    Check(cameraReads==0 && !shared.highView && !shared.returning,
          "leaving coupled high mode seeds native axes without sampling the observation camera");
    terrain=true;Put<int>(picked.data,edf::kWeaponAmmoAlive,1200);Put<int>(picked.data,edf::kWeaponMark,edf::kMarkLofted);
    // 2026-11-21 (「重型导弹这种开启俯瞰视角应该高抛弹道」): the high view lofts onto the range the native pitch axis names
    // (launcher.cpp AxisLoftWant), never onto a camera ray: the camera observes the landing.
    Put<float>(seat,kSeatAim+kAimAxes+kAxisStride+8,-0.3f);
    LauncherFrame(vehicle);
    const float highLoft=loftElevation;
    Check(loftCalls==1 && loftWanted && highLoft>0.79f && cameraReads==0,"high launcher lofts onto its axis' range without consuming CameraRay");
    Put<float>(seat,kSeatAim+kAimAxes+kAxisStride+8,-0.15f);LauncherFrame(vehicle);
    Check(loftCalls==2 && loftWanted && loftElevation>highLoft+0.01f && cameraReads==0,
          "high launcher: the axis lowered names a nearer range, the high arc to it is steeper");
    Put<float>(seat,kSeatAim+kAimAxes+kAxisStride+8,-0.3f);
    highOn=false;PublishObservation(seat,true);LauncherFrame(vehicle);
    Check(loftCalls==3 && loftWanted && std::fabs(loftElevation-highLoft)<1e-5f && cameraReads==0,
          "exiting high camera keeps the axis-named loft while the camera blends even after native aim resumes");
    Put<float>(seat,kSeatAim+kAimAxes+kAxisStride+8,0.0f);
    Check(TurretCamHighTransition(vehicle) && !TurretCamHighTransition(human),"observation belongs only to this vehicle");
    PublishObservation(seat,false);LauncherFrame(vehicle);
    Check(loftCalls==4 && cameraReads>0,"normal launcher retains the existing camera-to-loft path once blending ends");
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
    // The Katyusha's rack is katyusha.cpp's to pose (its Rack): its axes get the bore line.
    rackPosed=true;
    Check(Wants(seat,frame,target,true,shellWant) && std::fabs(shellWant[1]-boreWant[1])<1e-6f,
          "a posed lofted launcher (the Katyusha's rack) retains its independent elevation controller");
    Readout(seat,shared,true,target,true);
    Check(vec::Dist(out.r.gun,boreReadout.gun)<1e-5f,"a posed lofted launcher's readout matches its independent bore-line command");
    rackPosed=false;
    // A lofted gun nothing else poses (the V603's TOS-1 / X-Morph missile launcher, 2026-11-21: 「重型导弹这种开启俯瞰视角
    // 应该高抛弹道」): its axes lay the high arc within the pitch stops (here up to 1.5 rad) while the round lives to come
    // down (here 2 m a frame, 1 g of 9.8: 800 m out the high arc flies 1409 frames), else the low arc.
    {
        const double drop=9.8/3600.0;
        const float farPoint[3]={-4,8,810},nearPoint[3]={-4,8,210};
        float high=0.0f,low=0.0f,n=0.0f,nearLow=0.0f,nearHigh=0.0f,want[2]{};
        edf::BallisticArc(800.0,0.0,2.0,drop,true,high,n);edf::BallisticArc(800.0,0.0,2.0,drop,false,low,n);
        edf::BallisticArc(200.0,0.0,2.0,drop,false,nearLow,n);edf::BallisticArc(200.0,0.0,2.0,drop,true,nearHigh,n);
        Put<int>(primary.data,edf::kWeaponAmmoAlive,1200);
        Check(Wants(seat,frame,farPoint,true,want) && std::fabs(want[1]+low)<1e-4f && low<0.5f,
              "unposed lofted gun: a high arc past the round's life falls back to the low arc");
        Put<int>(primary.data,edf::kWeaponAmmoAlive,1500);
        Check(Wants(seat,frame,farPoint,true,want) && std::fabs(want[1]+high)<1e-4f && high>1.0f && high<1.5f,
              "unposed lofted gun: its axes lay the high arc while the round lives to land");
        Check(nearHigh>1.5f && Wants(seat,frame,nearPoint,true,want) && std::fabs(want[1]+nearLow)<1e-4f,
              "unposed lofted gun: a high arc over the pitch stop falls back to the low arc");
        Check(Wants(seat,frame,farPoint,false,want) && std::fabs(want[1]-boreWant[1])<1e-6f,
              "unposed lofted gun: a view with no real hit keeps the bore line (no made-up range)");
        // The readout: the round on the bore's arc where it has come the point's distance across (a high arc's flight
        // is far longer than the straight range over its speed): laid on the high arc, the mark is on the point.
        Put<float>(primary.bone,edf::kBoneRows+0x24,std::sin(high));Put<float>(primary.bone,edf::kBoneRows+0x28,std::cos(high));
        Readout(seat,shared,true,farPoint,true);
        Check(vec::Dist(out.r.gun,farPoint)<3.0f,"unposed lofted gun laid on its high arc: the readout's mark is on the point");
        Matrix(primary.bone+edf::kBoneRows,-4,8,10);
        Put<float>(primary.bone,edf::kBoneRows+0x24,std::sin(0.2f));Put<float>(primary.bone,edf::kBoneRows+0x28,std::cos(0.2f));
        Put<int>(primary.data,edf::kWeaponAmmoAlive,1200);
    }
    // The high view of that gun (HighLoft, 2026-11-21: 「重型导弹这种开启俯瞰视角应该高抛弹道」): the yaw input stays native,
    // the pitch input moves a range (set on entry from where the gun's elevation throws over level ground) and the pitch
    // axis is steered onto the high arc to the ground there. The camera's ray and the observed landing (ShotFocus) are
    // never inputs: a different observed focus gives the same command.
    {
        Put<int>(primary.data,edf::kWeaponAmmoAlive,1500);
        const float params[3]={0.3f,0.1f,0.02f};std::memcpy(seat+kSeatAim+kAimParams,params,12);
        float* pitchAxis=reinterpret_cast<float*>(seat+kSeatAim+kAimAxes+kAxisStride);
        pitchAxis[2]=-0.5f;Put<float>(pitchAxis,kAxisRate,0.0f);
        game=GameSide{};shared.high=true;shared.highView=false;shared.observing=false;shared.decoupled=true;
        shared.focusValid=true;shared.focusHit=true;shared.focus[0]=900;shared.focus[1]=-25;shared.focus[2]=-300;
        cameraReads=0;owner=0;Put<std::uint16_t>(seat,kSeatButtons,0);
        const double drop=9.8/3600.0;
        const float rest[2]{};float cmd[2]{9,9};
        Aim(seat,rest,cmd);
        const float entry=edf::LevelRange(2.0,drop,0.5f);
        Check(game.loft.set && std::fabs(game.loft.range-entry)<1e-3f*entry && entry>1000.0f,
              "high view, lofted gun: the range is set on entry from where its elevation throws over level ground");
        float expect=0.0f;
        Check(edf::LoftElevation(entry,floorY-8.0,2.0,drop,1500,-0.5f,1.5f,expect)==edf::LoftArc::high && cmd[1]<0.0f && cmd[0]==0.0f,
              "high view, lofted gun: the pitch is steered up toward the high arc to that range, the yaw input native");
        const GameSide before=game;const float angle=pitchAxis[2];
        Aim(seat,rest,cmd);const float firstCmd=cmd[1];
        game=before;pitchAxis[2]=angle;shared.focus[0]=-700;shared.focus[2]=1200;shared.focusHit=false;
        Aim(seat,rest,cmd);
        Check(cmd[1]==firstCmd && cameraReads==0,"high view, lofted gun: the observed landing and the camera are never its target (no loop)");
        bool native=true;
        for(int f=0;f<600;++f) {
            const float yawIn[2]={0.4f,0.0f};
            Aim(seat,yawIn,cmd);native=native && cmd[0]==0.4f;
            tcam::Axis x{pitchAxis[0],pitchAxis[1],pitchAxis[2],At<float>(pitchAxis,kAxisRate)};
            tcam::AxisStep(x,cmd[1],params);pitchAxis[2]=x.angle;Put<float>(pitchAxis,kAxisRate,x.rate);
        }
        Check(native && std::fabs(pitchAxis[2]+expect)<0.01f && expect>0.8f,
              "high view, lofted gun: the axis settles on the high arc to the input's range");
        const float settled=game.loft.range;
        for(int f=0;f<30;++f){const float up[2]={0.0f,-1.0f};Aim(seat,up,cmd);}
        const float farther=game.loft.range;
        for(int f=0;f<60;++f){const float down[2]={0.0f,1.0f};Aim(seat,down,cmd);}
        Check(farther>settled+100.0f && game.loft.range<farther-200.0f && cameraReads==0,
              "high view, lofted gun: the native pitch input moves the range (raise: farther, lower: nearer)");
        for(int f=0;f<2000;++f){const float down[2]={0.0f,1.0f};Aim(seat,down,cmd);}
        Check(std::fabs(game.loft.range-tcam::kLoftLeast)<1e-3f,"high view, lofted gun: the range stops at its nearest");
        const float held=game.loft.range;
        config.freeLookButton=0x10;Put<std::uint16_t>(seat,kSeatButtons,0x10);
        const float in[2]={0.3f,-0.6f};Aim(seat,in,cmd);
        Check(cmd[0]==0.0f && cmd[1]==0.0f && game.loft.range==held,"high view, lofted gun: free look holds the gun and the range");
        Put<std::uint16_t>(seat,kSeatButtons,0);
        owner=1;Aim(seat,in,cmd);
        Check(cmd[0]==in[0] && cmd[1]==in[1] && !game.loft.set,"high view, lofted gun: another hand's input keeps the gun; the range is set anew after");
        owner=0;rackPosed=true;Aim(seat,in,cmd);
        Check(cmd[0]==in[0] && cmd[1]==in[1],"high view: the Katyusha's posed rack keeps its native axes (katyusha.cpp lifts the rack)");
        rackPosed=false;
        const float zero[3]{};std::memcpy(seat+kSeatAim+kAimParams,zero,12);
        pitchAxis[2]=0.0f;Put<float>(pitchAxis,kAxisRate,0.0f);
        game=GameSide{};shared.high=false;shared.highView=false;shared.focusValid=false;
        Put<int>(primary.data,edf::kWeaponAmmoAlive,1200);
    }
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
        PublishObservation(seat,false);PublishNormalRay(seat,shared.take,normalEye,normalLook);TurretCamFrame(vehicle);
        Check(cameraReads==0 && game.hasAim,"normal frame samples only the owned normal request after observation ends");
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
    Check(cameraReads==0 && shared.view && shared.resume && !game.hasAim,
          "leaving optic seeds native axes and cannot sample the stale rendered optic");
    Camera(seat,opticTarget,opticCamera);TurretCamFrame(vehicle);
    Check(cameraReads==0 && !shared.resume && game.hasAim,
          "normal camera request restores control independently of the rendered optic cache");
    // A body/neck-mounted cockpit can look left of the requested gun direction, and the renderer may keep that
    // camera cached for arbitrarily many simulation steps. Neither condition is an aim input.
    ResetTurretCam();lookOk=true;nextAim=&UnexpectedAim;selectedGun=primary.data;
    config.decoupledTurretCam=true;highOn=false;cameraReads=0;firstPersonVehicle=vehicle;
    Put<float>(seat,kSeatStick,0);Put<float>(seat,kSeatStick+4,0);
    Put<float>(seat,kSeatAim+kAimAxes,-tcam::kPi);Put<float>(seat,kSeatAim+kAimAxes+4,tcam::kPi);
    Put<float>(seat,kSeatAim+kAimAxes+8,0);Put<float>(seat,kSeatAim+kAimAxes+kAxisStride+8,0);
    unsigned char normalCamera[0x700]{};float normalTargets[20]{};
    const float noInput[2]{};float noCommand[2]{};
    bool quiet=true;
    for(int frameIndex=0;frameIndex<40;++frameIndex) {
        TurretCamFrame(vehicle);Aim(seat,noInput,noCommand);Camera(seat,normalTargets,normalCamera);
        quiet=quiet && noCommand[0]==0 && noCommand[1]==0 && !game.hasAim && !shared.steering && !camSide.owned;
    }
    Check(quiet && cameraReads==0,"first-person body/neck view cannot drive its own mount left at zero input");
    Check(TurretCamServes(vehicle) && rigInfo.v==vehicle && rigInfo.normalRadius>0,
          "first person preserves seat identity and the measured rig needed by the high view");
    shared.decoupled=shared.steering=true;shared.resume=false;
    Check(!TurretCamTurret(vehicle,0) && !TurretCamSteers(vehicle),"external first-person owner cannot publish stale camera steering");
    Aim(seat,input,command);
    Check(command[0]==input[0] && command[1]==input[1] && !shared.decoupled,
          "first person preserves the native driver's full turn input");
    firstPersonVehicle=nullptr;
    TurretCamFrame(vehicle);Aim(seat,noInput,noCommand);
    Check(!game.hasAim && shared.resume && noCommand[0]==0 && noCommand[1]==0,
          "FP to third person starts from native axes before a normal camera request exists");
    Camera(seat,normalTargets,normalCamera);
    bool resumed=true;
    for(int frameIndex=0;frameIndex<8;++frameIndex) {
        TurretCamFrame(vehicle);Aim(seat,noInput,noCommand);Camera(seat,normalTargets,normalCamera);
        resumed=resumed && game.hasAim && shared.normalRay && !shared.resume && std::fabs(shared.yaw)<.01f;
    }
    Check(resumed && cameraReads==0,"returning third-person control uses its own request while old FP render rays remain left");
    // The free-look key's owner (2026-10-21: "鼠标中键的时候应该是绕着车转…战雷一样的那个圆的鼠标指示点怎么没了"): in third
    // person the turret camera's own look (its rig orbit, TurretMark's held circle), never vehicle_look.cpp's in-place turn.
    Check(TurretCamFreeLook(vehicle,0) && !TurretCamFreeLook(vehicle,1) && !TurretCamFreeLook(nullptr,0),
          "third person on the turret it places: the free look is the turret camera's, seat 0 only");
    firstPersonVehicle=vehicle;Check(!TurretCamFreeLook(vehicle,0),"first person: the cockpit eye's own head turn");
    firstPersonVehicle=nullptr;canMount=true;gunnerSight=true;Check(!TurretCamFreeLook(vehicle,0),"the gunner's sight up: the optic's observation");
    gunnerSight=false;Check(TurretCamFreeLook(vehicle,0),"an optic at 1x in third person is not up: still the turret camera's");
    canMount=false;mapView=true;Check(!TurretCamFreeLook(vehicle,0),"the map's view: nobody's free look");mapView=false;
    for(int ownerKind=0;ownerKind<3;++ownerKind) {
        mapKeys=ownerKind==0;mapView=ownerKind==1;observation=ownerKind==2;
        game.hasAim=game.steer.hasWant=true;shared.free=shared.returning=shared.steering=true;
        TurretCamFrame(vehicle);Aim(seat,input,command);Camera(seat,normalTargets,normalCamera);
        Check(!game.hasAim && !game.steer.hasWant && !shared.normalRay && !shared.steering && !camSide.owned &&
              command[0]==input[0] && command[1]==input[1],"map keys, map return blend and vehicle look each revoke the old aim source");
        mapKeys=mapView=observation=false;
        TurretCamFrame(vehicle);Aim(seat,noInput,noCommand);
        Check(shared.resume && !game.hasAim,"return from another owner waits for an identified normal source");
        Camera(seat,normalTargets,normalCamera);TurretCamFrame(vehicle);
        Check(game.hasAim && !shared.resume && cameraReads==0,"map/observation return ignores stale rendered view and uses normal request");
    }
    const unsigned sourceTake=shared.take;
    shared.rayTake=sourceTake-1;shared.normalRay=true;shared.resume=false;
    TurretCamFrame(vehicle);
    Check(!game.hasAim && !TurretCamTurret(vehicle,0),"a normal ray from another seat take cannot become the current aim or camera owner");
    PublishNormalRay(seat,sourceTake-1,normalEye,normalLook);
    Check(shared.rayTake!=sourceTake,"late previous-seat camera publication cannot replace this take's source");
    Put<void*>(seat,kSeatRider,nullptr);TurretCamFrame(vehicle);
    Put<void*>(seat,kSeatRider,human);TurretCamFrame(vehicle);
    Check(shared.take!=sourceTake && shared.resume && !shared.normalRay && !game.hasAim,
          "leaving and returning to the driver seat starts a new source identity");
    Put<void*>(otherSeat,kSeatRider,human);localHuman=human;TurretCamFrame(otherVehicle);
    Check(shared.v==otherVehicle && shared.resume && !shared.normalRay && !game.hasAim,
          "switching vehicles cannot reuse the previous cockpit or normal source");
    TurretCamFrame(vehicle);
    firstPersonVehicle=vehicle;highOn=true;TurretCamFrame(vehicle);Aim(seat,noInput,noCommand);
    Camera(seat,normalTargets,normalCamera);
    Check(shared.high && shared.focusValid && camSide.owned && !shared.normalRay && !shared.steering,
          "high view takes priority over the ordinary cockpit without camera-driven gun input");
    highOn=false;TurretCamFrame(vehicle);Aim(seat,noInput,noCommand);Camera(seat,normalTargets,normalCamera);
    Check(!camSide.owned && !shared.observing && !shared.highView && !shared.normalRay,
          "high view returns directly to the cockpit without retaining its observation blend");
    firstPersonVehicle=nullptr;cameraReads=0;
    // Full input -> sampled real projectile -> launcher -> camera target -> native collision order.
    // Only the original camera-locator accessor and the collision query outcomes are stand-ins.
    mountedOptic=false;localHuman=human;lookOk=true;nextAim=&UnexpectedAim;highOn=true;terrain=true;floorY=0;
    config.highCamHeight=45;config.highCamBack=35;config.highCamPitch=40;
    Put<float>(seat,kSeatAim+kAimAxes,-tcam::kPi);Put<float>(seat,kSeatAim+kAimAxes+4,tcam::kPi);
    Put<float>(vehicle,kObjCamLook+4,2);Put<float>(vehicle,kObjCamEye+4,5);Put<float>(vehicle,kObjCamEye+8,-20);
    for(bool launcher:{false,true}) {
        ResetTurretCam();lookOk=true;selectedGun=primary.data;game=GameSide{};
        Put<int>(primary.data,edf::kWeaponMark,launcher ? edf::kMarkLofted : 0);
        Put<float>(primary.data,edf::kWeaponAmmoSpeed,launcher ? 2.f : 4.f);
        Put<int>(primary.data,edf::kWeaponAmmoAlive,launcher ? 1500 : 1200);Matrix(primary.bone+edf::kBoneRows,-4,8,10);
        unsigned char camera[0x700]{};Put<float>(camera,0x528,.1f);
        Put<float>(camera,kCamEye+4,5);Put<float>(camera,kCamEye+8,-20);Put<float>(camera,kCamLook+4,2);
        bool stable=true,realFocus=true,inputUnchanged=true,onFocus=true,still=true,eased=true,back=true,clear=true;
        // The optical ray (eye through the written look) passes within this many m of the shot's landing (across the
        // ground): the collision pivot sits at the eye's end of that ray (tcam::HighPivot), not on the landing.
        const auto offRay=[&](const float* e,const float* l)->float {
            float d[3]={l[0]-e[0],l[1]-e[1],l[2]-e[2]};
            if(!vec::Normalize(d) || !(d[1]<-1e-3f))return 1e9f;
            const float t=(shared.focus[1]-e[1])/d[1];
            return std::hypot(e[0]+d[0]*t-shared.focus[0],e[2]+d[2]*t-shared.focus[2]);
        };
        const float wantPitch=-std::atan2(45.f,35.f);
        float settledEye[3]{},lastLook[3]{};
        for(int frameIndex=0;frameIndex<120;++frameIndex) {
            const float quietInput[2]{};float output[2]{};Aim(seat,quietInput,output); // native input/aim stage
            inputUnchanged=inputUnchanged && output[0]==0 && output[1]==0;
            TurretCamFrame(vehicle);if(launcher)LauncherFrame(vehicle);
            realFocus=realFocus && shared.focusValid && shared.focusHit && std::fabs(shared.focus[1])<1e-4f;
            float targets[20]{};targets[1]=2;Camera(seat,targets,camera);
            auto eye=reinterpret_cast<float*>(camera+kCamEye);auto look=reinterpret_cast<float*>(camera+kCamLook);
            // 2026-11-21 ("俯瞰视角移动的时候会白屏一下……会抖动"): a building 30 m tall 10 m in front of the landing (between
            // it and the eye) must not be on the segment the game's collision sweeps (look -> eye): before, the pivot was
            // the landing and that segment ran through the wall, pulling the eye onto it.
            if(frameIndex>20) {
                // The point observed: where the optical ray meets the landing's ground (the eased centre while the gun rocks).
                float ray[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]};vec::Normalize(ray);
                const float reach=(shared.focus[1]-eye[1])/ray[1];
                const float seen[3]={eye[0]+ray[0]*reach,shared.focus[1],eye[2]+ray[2]*reach};
                const float h[2]={eye[0]-seen[0],eye[2]-seen[2]};
                const float run=std::hypot(h[0],h[1]);
                const float wallAt=10.0f,wallTop=seen[1]+30.0f;
                const auto crosses=[&](const float* from)->bool {
                    const float a=(from[0]-seen[0])*h[0]/run+(from[2]-seen[2])*h[1]/run;
                    const float b=run;
                    if(!(a<wallAt && b>wallAt))return false;
                    const float t=(wallAt-a)/(b-a);
                    return from[1]+(eye[1]-from[1])*t<wallTop;
                };
                clear=clear && !crosses(look) && crosses(seen);
            }
            NativeCameraClip(eye,look,targets,floorY,At<float>(camera,0x528),frameIndex%3);
            const float delta[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]};
            const float pitch=vec::Len(delta)<1e-6f ? 0 : tcam::PitchOf(delta);
            if(frameIndex>20)stable=stable && std::fabs(pitch-wantPitch)<1e-4f;
            // 2026-10-25 ("俯瞰视角目的是为了看落点位置的信息"): with the gun steady the view settles on the real shot's
            // landing (the screen's middle, where the HUD's mark is) and holds still there. From frame 40 the gun rocks
            // (its muzzle moved every frame, the landing with it): the view follows eased, never a jump to each new
            // landing; once the gun is steady again (frame 70) it settles back on the landing.
            if(frameIndex>20 && frameIndex<=40)onFocus=onFocus && offRay(eye,look)<0.5f;
            if(frameIndex==30)std::memcpy(settledEye,eye,12);
            if(frameIndex>30 && frameIndex<=40)still=still && vec::Dist(eye,settledEye)<1e-2f;
            if(frameIndex>40 && frameIndex<70)eased=eased && std::hypot(look[0]-lastLook[0],look[2]-lastLook[2])<12.0f;
            if(frameIndex>110)back=back && offRay(eye,look)<0.5f;
            std::memcpy(lastLook,look,12);
            if(frameIndex>=40 && frameIndex<70)Matrix(primary.bone+edf::kBoneRows,-4,8+static_cast<float>(frameIndex%5),10+static_cast<float>(frameIndex%7));
            if(frameIndex==70)Matrix(primary.bone+edf::kBoneRows,-4,8,10);
        }
        Matrix(primary.bone+edf::kBoneRows,-4,8,10);
        Check(onFocus,"the high view looks at the shot's landing, not the vehicle");
        Check(clear,"the high view's collision sweep stays at the eye: a building before the landing never pulls the eye in");
        Check(still,"a steady gun holds the high view still");
        Check(eased,"a rocking gun's landing is followed eased, without jumps");
        Check(back,"the high view settles back on the landing");
        Check(inputUnchanged,"high camera closed loop never generates native gun input from observation");
        Check(realFocus,"collision pivot must not change the real ballistic terrain focus");
        Check(stable,launcher ? "Katyusha high camera remains stable after native surface collision" : "twin artillery high camera remains stable after native surface collision");
    }
    // 2026-10-09 (fb aim #2, the user: "按了右键使用机枪以后，自瞄和瞄具等都无法使用了。需要按左键发射主炮才能恢复"): the
    // Titan's right trigger picks its hull gatling (`front_gun` on `body`). The turret, its camera and the mouse command
    // stay the turret gun's; the pick stays the HUD's.
    {
        Weapon hullGun(0,3,0);
        Put<void*>(hullGun.holder,0x18,vehicleBones);Put<void*>(hullGun.data,0xE88,vehicleBones);Put<void*>(hullGun.data,0xF40,hullGun.bone);
        Put<void*>(hullGun.data,weaponmount::kWeaponModel+0x10,hullGun.bone);Put<int>(hullGun.data,weaponmount::kWeaponModel+0x20,1);
        Put<int>(hullGun.bone,0xC,0);Put<int>(hullGun.bone,0x10,-1);
        unsigned char* three[]={primary.holder,hullGun.holder,picked.holder};
        Put<void*>(seat,kSeatWeapons,three);Put<std::uint64_t>(seat,kSeatWeaponCount,3);
        Put<int>(primary.data,edf::kWeaponMark,0);Put<int>(primary.data,kWeaponLockon,0);
        ResetTurretCam();lookOk=true;nextAim=&UnexpectedAim;config.decoupledTurretCam=true;highOn=false;mountedOptic=false;
        Put<float>(seat,kSeatAim+kAimAxes,-tcam::kPi);Put<float>(seat,kSeatAim+kAimAxes+4,tcam::kPi);
        selectedGun=hullGun.data;TurretCamFrame(vehicle);
        Check(TurretGun(vehicle,seat)==primary.data,"a hull gun pick leaves the turret to the seat's articulated gun");
        Check(TurretCamServes(vehicle) && shared.decoupled && !shared.physicalOnly,"the hull gun pick keeps the turret camera decoupled");
        Check(leadGun==primary.data,"auto-aim is asked with the turret's gun, not the hull gun");
        selectedGun=primary.data;TurretCamFrame(vehicle);
        Check(shared.decoupled && !shared.physicalOnly && TurretGun(vehicle,seat)==primary.data,"back on the cannon: the same, symmetric");
        // The overhead view observes the turret's gun too (integration review): after the hull gun's trigger its point is
        // the cannon's, the one the view is handed back onto.
        terrain=true;floorY=-25.0f;
        selectedGun=primary.data;ShotFocus(shared);const float cannonFocus[3]={shared.focus[0],shared.focus[1],shared.focus[2]};
        const bool cannonValid=shared.focusValid;
        selectedGun=hullGun.data;ShotFocus(shared);
        Check(cannonValid && shared.focusValid && vec::Dist(shared.focus,cannonFocus)<1e-3f,"hull gun pick: the overhead point is the cannon's");
        selectedGun=picked.data;TurretCamFrame(vehicle);
        Check(TurretGun(vehicle,seat)==picked.data,"an articulated pick turns the turret by its own bore");
        unsigned char* only[]={hullGun.holder};Put<void*>(seat,kSeatWeapons,only);Put<std::uint64_t>(seat,kSeatWeaponCount,1);
        selectedGun=hullGun.data;TurretCamFrame(vehicle);
        Check(TurretGun(vehicle,seat)==hullGun.data && shared.physicalOnly,"a seat with a fixed gun alone stays physical-only");
        Put<void*>(seat,kSeatWeapons,three);Put<std::uint64_t>(seat,kSeatWeaponCount,3);
        // The lock's lead replaces the screen's centre (aimlink.h V4); asked every frame, also through the high view.
        selectedGun=primary.data;leadOn=true;leadPoint[0]=12;leadPoint[1]=3;leadPoint[2]=400;
        PublishNormalRay(seat,shared.take,normalEye,normalLook);TurretCamFrame(vehicle);
        Check(game.hasAim && game.aimHit && game.aim[0]==12 && game.aim[2]==400,"AUTO with a lock: the turret goes to the lead point");
        leadOn=false;TurretCamFrame(vehicle);
        Check(game.hasAim && !(game.aim[0]==12 && game.aim[2]==400),"no lock: the screen's centre again");
        // 2026-10-25 ("坦克的炮管会360度动"): the requested eye is placed before the game's camera collision, so a wall
        // between it and the rig's point (here 10 m behind the turret, the eye 20 m back) is met first along the screen
        // centre's line. That point is behind the turret: its want is the view's heading + 180 deg and the turret turns
        // round to it. The aim point is looked for ahead of the rig's point only.
        {
            const float wallEye[3]={0,5,-20},wallLook[3]={0,4,0};
            wall=true;wallZ=-10.0f;
            PublishNormalRay(seat,shared.take,wallEye,wallLook);TurretCamFrame(vehicle);
            Check(game.hasAim && game.aimHit && game.aim[2]>0.0f && std::fabs(game.aim[1]-floorY)<1e-3f,
                  "a wall between the requested eye and the turret is never the turret's aim point");
            float yawWant[2];const float level[9]={1,0,0,0,1,0,0,0,1};
            Check(Wants(seat,level,game.aim,false,yawWant) && std::fabs(yawWant[0])<0.1f,
                  "with a wall behind the vehicle the turret's yaw want stays on the view's heading (no 180 deg turn)");
            wallZ=50.0f;PublishNormalRay(seat,shared.take,wallEye,wallLook);TurretCamFrame(vehicle);
            Check(game.hasAim && game.aimHit && std::fabs(game.aim[2]-50.0f)<1e-3f,"a wall ahead of the turret is still a real target");
            wall=false;
        }
        const int asked=leadAsks;highOn=true;TurretCamFrame(vehicle);
        Check(leadAsks==asked+1 && !game.hasAim,"the high view still asks (the lock is kept) but steers nothing");
        highOn=false;Put<void*>(seat,kSeatWeapons,holders);Put<std::uint64_t>(seat,kSeatWeaponCount,2);
    }
    Shared intent=shared;intent.high=false;intent.decoupled=true;intent.physicalOnly=false;
    const float commandPoint[3]={0,3,250};
    Readout(seat,intent,false,commandPoint,false,true);Check(out.r.aimValid,"valid normal mouse intent is published independently of hit/on-target status");
    Readout(seat,intent,false,commandPoint,false,false);Check(!out.r.aimValid,"missing current camera target cannot publish stale mouse intent");
    intent.physicalOnly=true;Readout(seat,intent,false,commandPoint,false,true);Check(!out.r.aimValid,"fixed gun physical endpoint is not a movable command circle");
    intent.physicalOnly=false;intent.high=true;Readout(seat,intent,false,commandPoint,false,true);Check(!out.r.aimValid,"high camera ballistic focus is not mouse intent");
    ChaseChecks(vehicle,seat,human,control,primary.data);
    DWORD ignored=0;VirtualProtect(image+kPoint,14,protection,&ignored);
    image=nullptr;
}
}  // namespace
}  // namespace crew
int main() {
    crew::Run();std::printf("turret_cam_runtime_check: %d checks, %d failed\n",crew::checks,crew::failures);
    return crew::failures ? 1 : 0;
}
