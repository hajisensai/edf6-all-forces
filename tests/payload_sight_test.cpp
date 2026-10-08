// Production payload read/control/redirect paths with synthetic native seat memory and input devices.
// Holder layout is the Titan's real s / s+3 layout at kHolderStride, not a packed list of weapons.
#include <Windows.h>
namespace sightinput {
bool keys[256]{};
SHORT Key(int vk) noexcept { return vk>=0 && vk<256 && keys[vk] ? static_cast<SHORT>(0x8000) : 0; }
HWND Window() noexcept { return nullptr; }
DWORD Process(HWND,LPDWORD pid) noexcept { *pid=GetCurrentProcessId();return 1; }
}
#define GetAsyncKeyState sightinput::Key
#define GetForegroundWindow sightinput::Window
#define GetWindowThreadProcessId sightinput::Process
#include "../src/payload.cpp"
#undef GetAsyncKeyState
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#include <cstdio>
namespace crew {
unsigned char* image=nullptr;
Config cfg{};
ULONGLONG now=100;
bool mapHeld=false;
unsigned char* localHuman=nullptr;
const Config& Cfg() noexcept { return cfg; }
ULONGLONG GameMs() noexcept { return now; }
bool MapHoldsKeys() noexcept { return mapHeld; }
unsigned char* PlayerHuman() noexcept { return localHuman; }
PluginBody BodyOf(const void*) noexcept { return PluginBody::none; }
bool AiGunner(const unsigned char*,const unsigned char*) noexcept { return false; }
void Log(const char*,...) noexcept {}
bool IsFuelTank(const unsigned char* w) noexcept { return w[0x100]==3; }
bool IsLoadoutWeapon(const unsigned char* w) noexcept { return w[0x100]==2; }
bool IsStoreWeapon(const unsigned char*) noexcept { return false; }
const StoreSpec* StoreOf(const unsigned char*) noexcept { return nullptr; }
const wchar_t* WeaponFile(const unsigned char* w,std::size_t* n) noexcept {
    const auto f=At<const wchar_t*>(w,8);*n=f ? std::wcslen(f) : 0;return f;
}
void ClearWeaponLock(unsigned char*) noexcept {}
int WeaponLock(const unsigned char*,float*,float*) noexcept { return 0; }
bool ReadRound(const unsigned char*,RoundModel* out) noexcept { *out={};return false; }
namespace audio { void LockTone(int,float) noexcept {} }
}
namespace edf { bool MeanMuzzle(const unsigned char*,std::uint64_t,float*,float*) noexcept { return false; } }
namespace {
using namespace crew;
struct VehicleFixture {
    unsigned char vehicle[0x3000]{},seats[edf::kSeatStride*3]{},vehicleCtrl[16]{};
    unsigned char holders[8][kHolderStride]{},weapon[8][0x1600]{},weaponCtrl[8][16]{};
    unsigned char* seatHolders[3][4]{};
} cars[2];
unsigned char people[3][0x400]{},personCtrl[3][16]{};
int checks=0,failed=0;
void Check(bool yes,const char* message) { ++checks;if(!yes){++failed;std::printf("FAIL: %s\n",message);} }
void Tick(VehicleFixture& c) { ++now;PayloadFrame(c.vehicle); }
unsigned char* Sight(VehicleFixture& c,unsigned seat=0) { return PayloadSightPicked(c.vehicle,seat); }
void Board(VehicleFixture& c,unsigned seat,int person) {
    auto s=SeatAt(c.vehicle,seat);
    Put<void*>(s,kSeatRider,people[person]);Put<void*>(s,kSeatRiderCtrl,personCtrl[person]);
    localHuman=people[person];
}
void Leave(VehicleFixture& c,unsigned seat) {
    auto s=SeatAt(c.vehicle,seat);Put<void*>(s,kSeatRider,nullptr);Put<void*>(s,kSeatRiderCtrl,nullptr);
}
void Triggers(VehicleFixture& c,unsigned seat,float rt,float lt) {
    auto s=SeatAt(c.vehicle,seat);Put<float>(s,0x2E4,rt);Put<float>(s,0x2E0,lt);
}
bool NoShots(const VehicleFixture& c) { for(const auto& w:c.weapon)if(w[kWeaponTrigger])return false;return true; }
void SetUp() {
    ResetPayload();std::memset(cars,0,sizeof(cars));std::memset(people,0,sizeof(people));std::memset(personCtrl,0,sizeof(personCtrl));
    std::memset(sightinput::keys,0,sizeof(sightinput::keys));mapHeld=false;
    cfg.enabled=cfg.stockStores=true;cfg.playerJetSwitchKey='R';pullOk=true;
    for(int i=0;i<3;++i) {
        Put<void*>(people[i],0,image+0x17CDF28);Put<void*>(people[i],kSelfCtrl,personCtrl[i]);
        Put<void*>(people[i],edf::kHumanPad,people[i]);people[i][edf::kHumanPlayer]=1;Put<int>(personCtrl[i],8,1);
    }
    for(auto& c:cars) {
        Put<void*>(c.vehicle,0,image+kVt404);Put<void*>(c.vehicle,kSelfCtrl,c.vehicleCtrl);
        Put<void*>(c.vehicle,kSeats,c.seats);Put<std::uint64_t>(c.vehicle,kSeatCount,3);
        Put<void*>(c.vehicle,kHolders,c.holders);Put<std::uint64_t>(c.vehicle,kHolderCount,8);
        for(int i=0;i<8;++i) {
            Put<void*>(c.holders[i],kHolderWeapon,c.weapon[i]);Put<void*>(c.holders[i],kHolderCtrl,c.weaponCtrl[i]);Put<int>(c.weaponCtrl[i],8,1);
            Put<int>(c.weapon[i],kWeaponAmmo,10);Put<int>(c.weapon[i],kWeaponCapacity,20);Put<int>(c.weapon[i],kWeaponReloadTime,-1);
            Put<const wchar_t*>(c.weapon[i],kWeaponName,i<3 ? L"MAIN" : i<6 ? L"SECONDARY" : L"STORE");
            Put<const wchar_t*>(c.weapon[i],8,i==6 ? L"EDF6VC_ROCKET_12.SGO" : L"V_404TANK_CANNON01.SGO");
        }
        c.weapon[6][0x100]=2;c.weapon[7][0x100]=3;
        for(unsigned s=0;s<3;++s) {
            c.seatHolders[s][0]=c.holders[s];c.seatHolders[s][1]=c.holders[s+3];
            if(s==0){c.seatHolders[s][2]=c.holders[6];c.seatHolders[s][3]=c.holders[7];}
            Put<void*>(SeatAt(c.vehicle,s),kSeatWeapons,c.seatHolders[s]);Put<std::uint64_t>(SeatAt(c.vehicle,s),kSeatWeaponCount,s==0 ? 4 : 2);
        }
    }
    Board(cars[0],0,0);
}
}
int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)return 2;
    SetUp();auto& c=cars[0];
    Tick(c);PayloadReadout r{};PlayerPayload(&r);
    Check(r.count==3 && r.entry[0].fire==PayloadFire::primary && r.entry[1].fire==PayloadFire::secondary && r.entry[2].fire==PayloadFire::store,"Titan holder 0/3 and added store decode distinctly; fuel is excluded");
    Check(PayloadPicked(c.vehicle)==c.weapon[3] && Sight(c)==c.weapon[0],"default sight is main cannon even while payload cycle points at stock secondary");
    Triggers(c,0,0,1);Tick(c);Check(Sight(c)==c.weapon[3],"new LT edge selects real holder s+3");
    Triggers(c,0,1,1);Tick(c);Check(Sight(c)==c.weapon[0],"new RT edge returns to main while LT stays held");
    Triggers(c,0,0,0);Tick(c);Triggers(c,0,1,1);Tick(c);
    Check(Sight(c)==c.weapon[0],"simultaneous fresh RT and LT edges prefer main");
    Check(NoShots(c),"sampling trigger edges never writes a weapon firing latch");
    Triggers(c,0,0,0);Tick(c);sightinput::keys['R']=true;Tick(c);
    Check(PayloadPicked(c.vehicle)==c.weapon[6] && Sight(c)==c.weapon[6],"R chooses added mount for preview without either fire trigger");
    Check(NoShots(c),"R preview does not fire a weapon");
    Tick(c);Check(PayloadPicked(c.vehicle)==c.weapon[6],"held R is one selection, not repeated cycling");
    PullHook(c.holders[3]);Check(c.weapon[6][kWeaponTrigger]==1 && !c.weapon[3][kWeaponTrigger],"actual secondary holder pull redirects to the previewed mount");
    c.weapon[6][kWeaponTrigger]=0;PullHook(c.holders[0]);
    Check(c.weapon[0][kWeaponTrigger]==1 && !c.weapon[6][kWeaponTrigger],"primary holder remains main cannon after secondary payload selection");
    c.weapon[0][kWeaponTrigger]=0;
    sightinput::keys['R']=false;Tick(c);Triggers(c,0,1,0);Tick(c);
    Check(Sight(c)==c.weapon[0] && PayloadPicked(c.vehicle)==c.weapon[6],"RT changes sight without corrupting selected secondary store");
    Triggers(c,0,0,0);Tick(c);Triggers(c,0,0,1);Tick(c);
    Check(Sight(c)==c.weapon[6],"LT follows real trigger redirection to selected mount");

    SetUp();Put<unsigned char>(SeatAt(c.vehicle,0),kSeatPad,1);Tick(c);
    Put<std::uint16_t>(SeatAt(c.vehicle,0),kSeatButtons,kButtonLB);Tick(c);
    Check(Sight(c)==c.weapon[6] && NoShots(c),"pad LB previews store independently of firing");
    SetUp();sightinput::keys['R']=true;Triggers(c,0,0,1);Tick(c);
    Check(Sight(c)==c.weapon[0] && PayloadPicked(c.vehicle)==c.weapon[3],"R and LT already held on boarding are not fresh actions");
    sightinput::keys['R']=false;Triggers(c,0,0,0);Tick(c);Triggers(c,0,0,1);Tick(c);
    Check(Sight(c)==c.weapon[3],"release then press LT after boarding is honored");
    Board(c,0,1);Triggers(c,0,0,0);Tick(c);Check(Sight(c)==c.weapon[0],"changing human in same seat resets sight to primary");
    Triggers(c,0,0,1);Tick(c);Leave(c,0);Board(c,1,1);Tick(c);
    Check(Sight(c,1)==c.weapon[1] && !Sight(c,0),"moving seats uses new s primary and rejects old seat");
    Triggers(c,1,0,1);Tick(c);Check(Sight(c,1)==c.weapon[4],"gunner seat 1 LT resolves actual holder 4, not seat list index 1 globally");
    Leave(c,1);Board(cars[1],0,1);Tick(cars[1]);Check(Sight(cars[1])==cars[1].weapon[0],"changing vehicles resets sight to that vehicle's primary");
    // A second local player's vehicle is not the PlayerHuman camera/control context.
    Board(c,0,0);localHuman=people[1];Tick(c);Check(!Sight(c),"another local player's seat cannot supply this player's sight");
    Check(Sight(cars[1])==cars[1].weapon[0],"unrelated vehicle tick does not steal current player's sight");

    SetUp();Tick(c);sightinput::keys['R']=true;Tick(c);sightinput::keys['R']=false;
    Put<int>(c.weapon[6],kWeaponAmmo,0);Put<int>(c.weapon[6],kWeaponReloadTime,120);Put<int>(c.weapon[6],kWeaponReloadLeft,60);Tick(c);
    PlayerPayload(&r);Check(Sight(c)==c.weapon[6] && r.entry[2].ready==0.5f && r.entry[2].reloadSec==1.0f,"reloading selected mount keeps sight and real reload readout");
    Put<int>(c.weapon[6],kWeaponReloadTime,-1);
    Check(Sight(c)==c.weapon[0],"permanently spent redirected store is rejected immediately before next frame");
    Tick(c);Check(Sight(c)==c.weapon[3] && PayloadPicked(c.vehicle)==c.weapon[3],"spent store auto-cycles to remaining usable secondary");
    Put<int>(c.weapon[6],kWeaponAmmo,10);sightinput::keys['R']=true;Tick(c);sightinput::keys['R']=false;
    Put<int>(c.weaponCtrl[6],8,0);
    Check(Sight(c)==c.weapon[0],"expired selected holder cannot return stale store pointer");
    c.weapon[6][kWeaponTrigger]=0;PullHook(c.holders[3]);
    Check(!c.weapon[6][kWeaponTrigger],"expired selected holder cannot receive redirected fire before next frame");
    c.weapon[3][kWeaponTrigger]=0;
    Tick(c);PlayerPayload(&r);Check(r.count==2 && Sight(c)==c.weapon[3],"holder expiry removes actual mount then reselects live secondary");
    Put<int>(c.weaponCtrl[6],8,1);Triggers(c,0,1,0);Tick(c);Put<int>(c.weaponCtrl[0],8,0);
    Check(!Sight(c),"expired primary does not fabricate another primary sight");
    Put<int>(c.weaponCtrl[0],8,1);c.vehicle[kDead]=1;Check(!Sight(c),"dead vehicle has no usable sight");c.vehicle[kDead]=0;
    Put<int>(personCtrl[0],8,0);Check(!Sight(c),"expired occupant reference cannot retain sight");
    SetUp();Tick(c);Triggers(c,0,0,1);mapHeld=true;Tick(c);Check(Sight(c)==c.weapon[0],"map-owned trigger edge does not switch weapon sight");
    mapHeld=false;Tick(c);Check(Sight(c)==c.weapon[0],"closing map with trigger held does not synthesize a new edge");
    ResetPayload();Check(PayloadPicked(c.vehicle)==nullptr && Sight(c)==c.weapon[0],"mission reset discards selection and returns only current real primary");
    SetUp();Tick(c);Triggers(c,0,0,1);Tick(c);Put<void*>(people[0],kSelfCtrl,personCtrl[2]);
    Check(Sight(c)==c.weapon[0],"human object reused at same address loses old sight identity immediately");
    Tick(c);Check(Sight(c)==c.weapon[0],"reused human samples held trigger as baseline, not an edge");
    SetUp();Tick(c);Triggers(c,0,0,1);Tick(c);Put<void*>(c.vehicle,kSelfCtrl,cars[1].vehicleCtrl);
    Check(Sight(c)==c.weapon[0],"vehicle object reused at same address loses old sight identity immediately");
    Tick(c);Check(Sight(c)==c.weapon[0],"reused vehicle does not carry old secondary-control edge");
    SetUp();Tick(c);sightinput::keys['R']=true;Tick(c);now+=201;
    Check(Sight(c)==c.weapon[3] && !PayloadPicked(c.vehicle),"expired payload preview falls back to currently live native secondary");
    Check(NoShots(c),"read-only sight queries and expiration never generate shots");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("payload sight: %d checks, %d failures\n",checks,failed);return failed ? 1 : 0;
}
