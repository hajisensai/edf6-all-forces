// Actual mount classifier and stock ballistic consumer; only world terrain is synthetic.
#include "../src/vhud.cpp"
#include <cstdio>
#include <vector>
namespace crew {
unsigned char* image=nullptr;Config config{};
const Config& Cfg() noexcept{return config;}
void Log(const char*,...) noexcept{}
int WeaponLock(const unsigned char*,float*,float*) noexcept{return 0;}
bool terrain=true;
float MapRay(const float* a,const float* b,float* hit) noexcept {
    const float floor=a[0]<0 ? 0.0f : -10.0f;
    if(!terrain || a[1]<floor || b[1]>floor || a[1]==b[1])return -1;
    const float t=(floor-a[1])/(b[1]-a[1]);
    for(int i=0;i<3;++i)hit[i]=a[i]+(b[i]-a[i])*t;
    return vec::Dist(a,hit);
}
ULONGLONG GameMs() noexcept{return 100;}
ULONGLONG GameFrame() noexcept{return 100;}
bool CameraRay(float*,float*) noexcept{return false;}
PluginBody BodyOf(const void*) noexcept{return PluginBody::none;}
bool HighCamOn(const void*) noexcept{return false;}
bool TurretCamHighTransition(const void*) noexcept{return false;}
float SightZoomNow(const void*) noexcept{return 1;}
int StabState(unsigned char*,unsigned) noexcept{return 0;}
bool IsSub(const void*) noexcept{return false;}
bool IsPlayerJet(const void*) noexcept{return false;}
namespace jet {int LockersOf(const void*,float (*)[3],int) noexcept{return 0;}}
int MissilesHomingAt(const float*,float,float (*)[3],int) noexcept{return 0;}
unsigned char* hudHuman=nullptr;
unsigned char* PlayerHuman() noexcept{return hudHuman;}
bool IsHelicopter(const void*) noexcept{return false;}
bool VisitEnemies(const unsigned char*,void(*)(void*,const void*,const float*),void*) noexcept{return false;}
const char* VehicleClassName(const void*) noexcept{return "fixture";}
bool HudReady() noexcept{return true;}
bool FuelGauge(const void*,FuelReading*) noexcept{return false;}
bool IsFuelTank(const unsigned char*) noexcept{return false;}
unsigned char* PayloadPicked(const void*) noexcept{return nullptr;}
unsigned char* PayloadSightPicked(const void*,unsigned) noexcept{return nullptr;}
int PayloadSightWeapons(const void*,unsigned,unsigned char**,int) noexcept{return 0;}
// The Proteus's borrowed stock mounts (proteus.cpp): `proteusLent` lent to seat `proteusSeat`.
const unsigned char* proteusLent=nullptr;unsigned proteusSeat=0;
int ProteusBorrowedWeapons(const unsigned char*,unsigned seat,const unsigned char** out,int max) noexcept{
    if(!proteusLent || seat!=proteusSeat || max<1)return 0;
    out[0]=proteusLent;return 1;
}
namespace {
int checks=0,failed=0;
void Check(bool b,const char* name){++checks;if(!b){++failed;std::printf("FAIL %s\n",name);}}
void Matrix(unsigned char* b,float x=0,float y=5,float z=0){const float m[]={1.f,0.f,0.f,0.f,0.f,1.f,0.f,0.f,0.f,0.f,1.f,0.f,x,y,z,1.f};std::memcpy(b,m,64);}
const float* __fastcall Gravity(void*){static float g[]={0.f,-9.8f,0.f,0.f};return g;}
struct Fixture {
    unsigned char vehicle[0x1100]{},seat[kSeatStride]{},holder[0x30]{},weapon[0x1100]{},muzzle[2*edf::kMuzzleStride]{};
    unsigned char bones[4*weaponmount::kStride]{},own[2*weaponmount::kStride]{},mapping[2][0x38]{};
    Fixture() {
        Put<void*>(vehicle,weaponmount::kVehicleModel+0x10,bones);Put<int>(vehicle,weaponmount::kVehicleModel+0x20,4);
        for(int i=0;i<4;++i){Put<int>(bones+i*weaponmount::kStride,0xC,i);Put<int>(bones+i*weaponmount::kStride,0x10,i-1);}
        Put<void*>(holder,kHolderWeapon,weapon);Put<void*>(holder,0x18,bones+3*weaponmount::kStride);
        Put<void*>(weapon,edf::kMuzzles,muzzle);Put<std::uint64_t>(weapon,edf::kMuzzleCount,1);
        Matrix(weapon+edf::kWeaponMatrix);Matrix(muzzle+edf::kMuzzleLocal);
        Put<float>(weapon,edf::kWeaponAmmoSpeed,2);Put<float>(weapon,edf::kWeaponAmmoGravity,1);Put<int>(weapon,edf::kWeaponAmmoAlive,300);
        for(int i=0;i<2;++i){auto axis=seat+kSeatAim+kAimAxes+i*kAxisStride;Put<float>(axis,0,-1);Put<float>(axis,4,1);Put<void*>(axis,0x28,mapping[i]);Put<std::uint64_t>(axis,0x38,1);Put<int>(mapping[i],0,i+1);Put<float>(mapping[i],4,-1);Put<float>(mapping[i],8,1);}
    }
    weaponmount::Freedom Freedom(){return weaponmount::Of(vehicle,seat,holder);}
};
void Run(){
    Fixture f;auto a=f.Freedom();Check(a.known&&a.yaw&&a.pitch,"slide inherits both native seat axes");
    Put<float>(f.mapping[0],4,1);Put<float>(f.mapping[0],8,-1);Check(f.Freedom().yaw,"reversed bone rotation mapping remains articulated");
    Put<void*>(f.holder,0x18,f.bones);a=f.Freedom();Check(a.known&&!a.yaw&&!a.pitch,"body fixed gun ignores unrelated seat turret axes");
    Put<void*>(f.holder,0x18,f.bones+3*weaponmount::kStride);
    Put<float>(f.seat,kSeatAim+kAimAxes,0);Put<float>(f.seat,kSeatAim+kAimAxes+4,0);a=f.Freedom();Check(a.known&&!a.yaw&&a.pitch,"dual artillery is pitch-only and cannot get free camera steering");
    Put<float>(f.seat,kSeatAim+kAimAxes,-1);Put<float>(f.seat,kSeatAim+kAimAxes+4,1);
    Put<int>(f.bones+3*weaponmount::kStride,0x10,3);Check(!f.Freedom().known,"cyclic ancestry fails closed");
    Put<int>(f.bones+3*weaponmount::kStride,0x10,2);
    Put<void*>(f.holder,0x18,f.bones+1);Check(!f.Freedom().known,"misaligned attached bone fails closed");
    Put<void*>(f.holder,0x18,f.bones+3*weaponmount::kStride);
    Put<std::uint64_t>(f.seat+kSeatAim+kAimAxes,0x38,0x100000001ULL);Check(!f.Freedom().yaw,"axis mapping count uses full 64 bits");
    Put<std::uint64_t>(f.seat+kSeatAim+kAimAxes,0x38,1);
    Put<int>(f.muzzle,edf::kMuzzleMode,1);Put<void*>(f.weapon,0xE88,f.bones+3*weaponmount::kStride);Put<void*>(f.weapon,0xF40,f.own);
    Put<void*>(f.weapon,weaponmount::kWeaponModel+0x10,f.own);Put<int>(f.weapon,weaponmount::kWeaponModel+0x20,2);
    for(int i=0;i<2;++i){Put<int>(f.own+i*weaponmount::kStride,0xC,i);Put<int>(f.own+i*weaponmount::kStride,0x10,i-1);f.own[i*weaponmount::kStride+8]=1;}
    Put<void*>(f.muzzle,0,f.own+weaponmount::kStride);a=f.Freedom();Check(a.known&&a.yaw&&a.pitch,"mode1 weapon-model root bridges to actual vehicle holder ancestry");
    f.own[weaponmount::kStride+8]=0;Check(!f.Freedom().known,"independent weapon bone cannot claim seat articulation");f.own[weaponmount::kStride+8]=1;
    Put<void*>(f.weapon,0xE88,f.bones);Check(!f.Freedom().known,"different bridge bone cannot claim articulation");
    Put<int>(f.muzzle,edf::kMuzzleMode,2);Check(!f.Freedom().known,"unknown transform mode fails closed");
    Check(!weaponmount::Of(nullptr,f.seat,f.holder).known,"null vehicle fails closed");
    Put<int>(f.muzzle,edf::kMuzzleMode,1);Matrix(f.own+weaponmount::kStride+edf::kBoneRows,-3);
    // A second actual muzzle sits right of the first and sees a different terrain height.
    std::memcpy(f.muzzle+edf::kMuzzleStride,f.muzzle,edf::kMuzzleStride);Matrix(f.muzzle+edf::kMuzzleStride+edf::kMuzzleLocal,6,0,0);
    Put<std::uint64_t>(f.weapon,edf::kMuzzleCount,2);
    std::vector<unsigned char> module(0x20B2960);image=module.data();unsigned char world[0x80]{},physics[0x30]{};
    void* table[]={reinterpret_cast<void*>(&Gravity)};Put<void*>(image,0x20B2958,world);Put<void*>(world,0x68,physics);Put<void*>(physics,0x20,table);
    StockArm fixed{};Arm(f.weapon,true,fixed);
    Check(fixed.aimed&&fixed.paths==2&&fixed.path[0].hit&&fixed.path[1].hit,"production consumer predicts both real muzzles");
    Check(fixed.path[0].at[0]<0&&fixed.path[1].at[0]>0&&fixed.path[0].at[1]==0&&fixed.path[1].at[1]==-10,"two barrels never use averaged impact between terrains");
    target.ok=true;target.at[0]=-3;target.at[1]=5;target.at[2]=30;
    StockArm withTarget{};Arm(f.weapon,true,withTarget);
    Check(!withTarget.ranged&&withTarget.ladder.ticks==0,"fixed gun has no camera-selected movable guide");
    Check(withTarget.hit==fixed.hit&&vec::Dist(withTarget.at,fixed.at)<.001f&&withTarget.flight==fixed.flight,"camera target cannot move fixed physical hit");
    StockArm turret{};turret.physicalOnly=false;Arm(f.weapon,true,turret);
    Check(turret.ranged&&turret.targetRange>0,"articulated gun retains optional target lead guide");
    Check(turret.hit==fixed.hit&&vec::Dist(turret.at,fixed.at)<.001f&&turret.flight==fixed.flight,"articulated target guide also cannot replace physical map hit");
    terrain=false;StockArm sky{};Arm(f.weapon,true,sky);
    Check(sky.paths==2&&!sky.hit&&!sky.path[1].hit&&sky.flight==5,"no terrain keeps explicit finite lifetime endpoints");
    Check(sky.path[0].at[0]<0&&sky.path[1].at[0]>0&&sky.at[2]>500,"no-hit path still follows actual barrels");
    Put<float>(f.weapon,edf::kWeaponAmmoOwnerMove,1);Put<float>(f.weapon,edf::kWeaponOwnerVel,12);StockArm moving{};Arm(f.weapon,true,moving);
    Check(moving.at[0]>sky.at[0]+50,"physical path retains inherited vehicle velocity");
    Put<std::uint64_t>(f.weapon,edf::kMuzzleCount,0);StockArm absent{};Arm(f.weapon,true,absent);Check(!absent.aimed&&!absent.paths,"missing muzzle has no fictitious point");
    // Feed production ReadRound with checked factory RTTI, then production Arm/GunMarkOf.
    const auto named=[&](unsigned vt,const char* name,unsigned scratch) {
        Put<void*>(image,vt-8,image+scratch);Put<unsigned>(image+scratch,0xC,scratch+0x40);
        strcpy_s(reinterpret_cast<char*>(image+scratch+0x50),100,name);
    };
    named(0x179ECA8,".?AVFactory@EfsExposureBullet@@",0x10000);
    named(0x179FC60,".?AVFactory@LaserBullet01@@",0x10200);
    named(0x17A3E90,".?AVFactory@SolidBullet01@@",0x10400);
    named(0x17E5E40,".?AVWeapon_VehicleMaser@@",0x10600);
    InstallRounds();
    unsigned char factory[16]{};Put<void*>(f.weapon,0x7F8,factory);Put<std::uint64_t>(f.weapon,edf::kMuzzleCount,2);
    Put<float>(f.weapon,edf::kWeaponAmmoOwnerMove,0);Put<float>(f.weapon,edf::kWeaponAmmoGravity,0);
    Put<float>(f.weapon,edf::kWeaponAmmoSpeed,10);Put<int>(f.weapon,edf::kWeaponAmmoAlive,30);
    Put<void*>(factory,0,image+0x179ECA8);StockArm nixBeam{};nixBeam.physicalOnly=false;Arm(f.weapon,true,nixBeam);
    Check(nixBeam.style==WeaponStyle::beam && nixBeam.paths==2 && nixBeam.ladder.ticks==0 && !nixBeam.ranged,
          "Nix EfsExposure beam retains real muzzle endpoints without ballistic ladder or target lead");
    Put<void*>(factory,0,image+0x179FC60);StockArm laser{};laser.physicalOnly=false;Arm(f.weapon,true,laser);
    Check(laser.style==WeaponStyle::laser && laser.paths==2 && laser.ladder.ticks==0 && !laser.ranged,
          "native LaserBullet gun has energy semantics and no artificial drop marks");
    Put<void*>(factory,0,image+0x17A3E90);Put<void*>(f.weapon,0,image+0x17E5E40);
    StockArm maser{};maser.physicalOnly=false;Arm(f.weapon,true,maser);
    Check(maser.style==WeaponStyle::maser && !std::strcmp(maser.label,"MASER") && maser.ladder.ticks==0,
          "actual Weapon_VehicleMaser wins over SolidBullet carrier class and does not become a machine gun");
    Put<void*>(f.weapon,0,nullptr);StockArm ordinary{};ordinary.physicalOnly=false;Arm(f.weapon,true,ordinary);
    Check(ordinary.style==WeaponStyle::projectile && !std::strcmp(ordinary.label,"GUN") && ordinary.ladder.ticks>0,
          "ordinary SolidBullet gun retains its genuine gun semantics and range ladder");
    // The production HUD consumer lists the stock mounts a Proteus seat borrows, though native seat 0 holds none.
    unsigned char human[0x400]{},riderCtrl[16]{},hudSeats[2*kSeatStride]{};
    human[edf::kHumanPlayer]=1;Put<void*>(human,edf::kHumanPad,human);Put<int>(riderCtrl,8,1);hudHuman=human;
    Put<void*>(f.vehicle,kSeats,hudSeats);Put<std::uint64_t>(f.vehicle,kSeatCount,2);
    Put<void*>(hudSeats,kSeatRider,human);Put<void*>(hudSeats,kSeatRiderCtrl,riderCtrl);
    Put<std::uint64_t>(f.weapon,edf::kMuzzleCount,2);
    config.stockVehicleHud=true;proteusLent=f.weapon;proteusSeat=0;StockHudFrame(f.vehicle);StockHudReadout hud{};
    Check(PlayerStockHud(&hud) && hud.seat==0 && hud.arms==1 && hud.aimOk && hud.arm[0].coFired && hud.arm[0].aimed,
          "the driver's no-holder HUD lists the stock cannon it borrows, from its own muzzle");
    proteusLent=nullptr;StockHudFrame(f.vehicle);PlayerStockHud(&hud);
    Check(hud.arms==0 && !hud.aimOk,"nothing borrowed: no stale weapon on the driver's HUD");
    Put<void*>(hudSeats,kSeatRider,nullptr);Put<void*>(hudSeats,kSeatRiderCtrl,nullptr);
    Put<void*>(hudSeats+kSeatStride,kSeatRider,human);Put<void*>(hudSeats+kSeatStride,kSeatRiderCtrl,riderCtrl);
    proteusLent=f.weapon;proteusSeat=1;StockHudFrame(f.vehicle);PlayerStockHud(&hud);
    Check(hud.seat==1 && hud.arms==1 && hud.arm[0].coFired && hud.arm[0].aimed && hud.arm[0].paths==2,
          "the gunner's HUD appends the paired right cannon's firing paths");
    proteusLent=nullptr;hudHuman=nullptr;image=nullptr;
}
}}
int main(){crew::Run();std::printf("fixed_weapon_sight: %d checks, %d failed\n",crew::checks,crew::failed);return crew::failed ? 1 : 0;}
