// The one player turret aim (common/edf/aimlink.h V4) on the production autoturret/src/designate.cpp: the export
// EDF6VehicleCrew's turret camera asks every frame, for a vehicle of any class. Stand-in native memory: a vehicle, its
// seat, the player in it, a gun with one muzzle, the world's lock points; the keys are a fixture (fb 2026-10-09 aim):
//  - V (the mode) and Q (the lock, on its release) reach the aim through the export alone, the tank classes the plugin
//    never hooks included ("v键自瞄按了没反应");
//  - a press locks what the crosshair is on, the range counted from the vehicle, not the camera 37 m behind it
//    ("标记的东西和实际鼠标指向不符");
//  - AUTO with a lock: the steer point is where the round meets the moving target; the lead circle, the observation view
//    and no lock: no steering; the flak's gun and a tank's get the same answer from the same code.
#include <Windows.h>
namespace aimkeys {
bool down[256]{};
SHORT Key(int vk) noexcept { return vk>=0 && vk<256 && down[vk] ? static_cast<SHORT>(0x8000) : 0; }
HWND Window() noexcept { return nullptr; }
DWORD Process(HWND,LPDWORD pid) noexcept { *pid=GetCurrentProcessId();return 1; }
}
#define GetAsyncKeyState aimkeys::Key
#define GetForegroundWindow aimkeys::Window
#define GetWindowThreadProcessId aimkeys::Process
#include "../autoturret/src/designate.cpp"
#undef GetAsyncKeyState
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#include <cstdio>

namespace autoturret {
unsigned char imageBytes[16]{};
unsigned char* image=imageBytes;
Config cfg{};
ULONGLONG frameNow=1;
int seenOnce=0;
constexpr int kEnemies=4;
Enemy enemies[kEnemies]{};
unsigned char objects[kEnemies][0x400]{},objectCtrl[kEnemies][16]{};
int enemyCount=0;
std::int32_t relation[kMaxTeam]{};
void Log(const char*,...) noexcept {}
void SeeVehicle(const void*) noexcept {}
void SeeVehicleOnce(const void*) noexcept { ++seenOnce; }
ULONGLONG Frame() noexcept { return frameNow; }
bool Same(const void* obj,const void* ctrl) noexcept {
    for(int i=0;i<enemyCount;++i)if(obj==objects[i] && ctrl==objectCtrl[i])return true;
    return false;
}
const Enemy* World(int* count) noexcept { *count=enemyCount;return enemies; }
const std::int32_t* Relations(std::int32_t team) noexcept { return team==0 ? relation : nullptr; }
void ReloadConfigIfChanged() noexcept {}
float Down(const unsigned char*) noexcept { return 9.8f; }
}  // namespace autoturret

namespace {
using namespace autoturret;
int checks=0,failures=0;
void Check(bool ok,const char* what) { ++checks;if(!ok){++failures;std::printf("FAIL %s\n",what);} }

float viewEye[3]={0,5,-37},viewAt[3]={0,0,200};
bool __cdecl View(float* eye,float* dir) {
    float d[3],l=0;
    for(int i=0;i<3;++i){eye[i]=viewEye[i];d[i]=viewAt[i]-viewEye[i];l+=d[i]*d[i];}
    l=std::sqrt(l);
    for(int i=0;i<3;++i)dir[i]=d[i]/l;
    return true;
}
bool observing=false;
bool __cdecl Observer(const void*,unsigned) { return observing; }

void Identity(void* to,float x=0,float y=0,float z=0) {
    const float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1};std::memcpy(to,m,sizeof(m));
}
struct Gun {
    unsigned char data[0x1100]{},muzzle[edf::kMuzzleStride]{},bone[0x110]{};
    Gun(std::int32_t mark) {
        edf::Put<const void*>(data,edf::kMuzzles,muzzle);edf::Put<std::uint64_t>(data,edf::kMuzzleCount,1);
        edf::Put<const void*>(muzzle,0,bone);edf::Put<int>(muzzle,edf::kMuzzleMode,1);
        Identity(muzzle+edf::kMuzzleLocal);Identity(bone+edf::kBoneRows,0,3,4);Identity(data+edf::kWeaponMatrix);
        edf::Put<float>(data,kAmmoSpeed,8.0f);edf::Put<float>(data,kAmmoGravity,1.0f);edf::Put<std::int32_t>(data,kAmmoAlive,37);
        edf::Put<std::int32_t>(data,edf::kWeaponMark,mark);
    }
};
struct Ride {
    unsigned char v[0x2C00]{},seats[edf::kSeatStride]{},human[0x400]{},ctrl[16]{},humanCtrl[16]{};
    Ride() {
        Identity(v+kMatrix);edf::Put<void*>(v,kSelfCtrl,ctrl);edf::Put<int>(ctrl,edf::kCtrlUses,1);
        edf::Put<void*>(v,edf::kSeats,seats);edf::Put<std::uint64_t>(v,edf::kSeatCount,1);edf::Put<std::int32_t>(v,kTeam,0);
        edf::Put<void*>(seats,edf::kSeatRider,human);edf::Put<void*>(seats,edf::kSeatRiderCtrl,humanCtrl);edf::Put<int>(humanCtrl,edf::kCtrlUses,1);
        human[edf::kHumanPlayer]=1;edf::Put<void*>(human,edf::kHumanPad,human);
    }
};

void SetEnemy(int i,float x,float y,float z) {
    enemies[i].object=objects[i];enemies[i].team=1;
    enemies[i].pos[0]=enemies[i].origin[0]=x;enemies[i].pos[1]=enemies[i].origin[1]=y;enemies[i].pos[2]=enemies[i].origin[2]=z;
    edf::Put<void*>(objects[i],kSelfCtrl,objectCtrl[i]);edf::Put<int>(objectCtrl[i],edf::kCtrlUses,1);
}

edf::aimlink::PlayerAimV4 Ask(Ride& r,Gun& g) {
    ++frameNow;
    edf::aimlink::PlayerAimV4 out{};
    Check(EDF6AutoTurret_PlayerAimV4(r.v,0,g.data,&out),"the export answers for the player's seat");
    return out;
}
void Press(Ride& r,Gun& g,int vk) { aimkeys::down[vk]=true;Ask(r,g);aimkeys::down[vk]=false;Ask(r,g); }
void Look(float x,float y,float z) { viewAt[0]=x;viewAt[1]=y;viewAt[2]=z; }
}  // namespace

int main() {
    relation[1]=kEnemyRelation;
    viewRay=&View;turretObserver=&Observer;
    const ULONGLONG now=GetTickCount64();
    mapTried=heldTried=bindingTried=stabTried=cameraTried=zoneTried=now;
    cfg.enabled=true;cfg.lockCone=20.0f;cfg.lockRange=0.0f;cfg.lockClearMs=600;cfg.modeKey=0x56;cfg.lockKey=0x51;cfg.aimMode=0;
    Ride tank;Gun cannon(0);
    // A 505-class tank: its class has no hook of this plugin; the export is its only clock and key reader.
    auto a=Ask(tank,cannon);
    Check(seenOnce>0,"the export steps the clock for a vehicle no own hook sees");
    Check(a.mode==edf::aimlink::Mode::autoAim && !a.steer,"AUTO without a lock: the view steers");
    edf::aimlink::TurretReadoutV1 shown{};
    Check(EDF6AutoTurret_TurretReadoutV1(&shown) && shown.ownGun && shown.modeKey==0x56,"the HUD reads the player's own gun, V shown");
    Press(tank,cannon,0x56);
    Check(LeadCircle(),"V reaches the mode through the export (fb #1)");
    Press(tank,cannon,0x56);
    Check(!LeadCircle(),"V again: AUTO back, symmetric");
    // Three enemies 200 m out; the crosshair on A.
    enemyCount=3;SetEnemy(0,0,0,200);SetEnemy(1,30,0,200);SetEnemy(2,-60,0,200);
    Look(0,0,200);Press(tank,cannon,0x51);
    Check(Designated(tank.v,nullptr)==objects[0],"Q locks the enemy under the crosshair");
    Look(30,0,200);Press(tank,cannon,0x51);
    Check(Designated(tank.v,nullptr)==objects[1],"view moved onto B: the press locks B, not the one after A (fb #3)");
    Press(tank,cannon,0x51);
    Check(Designated(tank.v,nullptr)!=objects[1] && Designated(tank.v,nullptr)!=nullptr,"pressing again over the same spot steps on");
    // Range from the vehicle: the gun reaches 296 m (8 m/frame x 37); D 270 m ahead of the hull, 307 m from the eye.
    enemyCount=4;SetEnemy(3,0,0,270);Look(0,0,270);
    enemies[0].pos[2]=enemies[1].pos[2]=enemies[2].pos[2]=900.0f;   // the others out of range
    Press(tank,cannon,0x51);
    Check(Designated(tank.v,nullptr)==objects[3],"the lock range is counted from the vehicle, not the camera behind it");
    // AUTO with the lock on a target crossing at 0.5 m/frame: the steer point is ahead of it by its flight.
    edf::aimlink::PlayerAimV4 lead{};
    for(int f=0;f<20;++f){enemies[3].pos[0]+=0.5f;lead=Ask(tank,cannon);}
    const float ahead=lead.point[0]-enemies[3].pos[0];
    Check(lead.steer && ahead>12.0f && ahead<20.0f,"AUTO + lock: the turret is sent where the round meets the crossing target");
    Check(EDF6AutoTurret_TurretReadoutV1(&shown) && shown.lock!=edf::aimlink::Lock::none && shown.lead && shown.ownGun,
          "the readout carries the lock and its lead for the HUD");
    // The flak's marked gun in a flak: the same export, the same code, the same answer for the same round.
    Ride flak;Gun flakGun(edf::kMarkAir);
    std::memcpy(flak.v+kMatrix,tank.v+kMatrix,64);
    Ask(flak,flakGun);Look(enemies[3].pos[0],0,270);Press(flak,flakGun,0x51);
    for(int f=0;f<20;++f){enemies[3].pos[0]+=0.5f;lead=Ask(flak,flakGun);}
    const float flakAhead=lead.point[0]-enemies[3].pos[0];
    Check(lead.steer && std::fabs(flakAhead-ahead)<0.5f,"the flak's auto-aim is the tanks': same key, same lock, same lead");
    Press(flak,flakGun,0x56);
    Check(!Ask(flak,flakGun).steer,"the lead circle: the turret is the player's");
    Press(flak,flakGun,0x56);
    observing=true;Check(!Ask(flak,flakGun).steer,"the overhead view / optic: no steering");
    observing=false;Check(Ask(flak,flakGun).steer,"back: steering again");
    // No player in the seat: no answer.
    edf::Put<int>(flak.humanCtrl,edf::kCtrlUses,0);edf::aimlink::PlayerAimV4 none{};
    Check(!EDF6AutoTurret_PlayerAimV4(flak.v,0,flakGun.data,&none) && !none.steer,"an empty seat gets nothing");
    std::printf("player_aim_test: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
