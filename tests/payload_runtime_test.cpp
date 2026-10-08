#include "../src/payload.cpp"
#include <cstdio>
#include <initializer_list>
namespace crew {
void PumpAircraftPayloadUi(unsigned char*) noexcept {}
unsigned char* image=nullptr;
Config cfg;
ULONGLONG now=100;
bool authority=true;
unsigned char* trackedHuman=nullptr;
unsigned char* PlayerHuman() noexcept { return trackedHuman; }
Config& MutableConfig() { return cfg; }
const Config& Cfg() noexcept { return cfg; }
ULONGLONG GameMs() noexcept { return now; }
bool MapHoldsKeys() noexcept { return false; }
PluginBody BodyOf(const void*) noexcept { return PluginBody::none; }
bool AiGunner(const unsigned char*,const unsigned char* s) noexcept { return authority && SeatRider(s)==Rider::other; }
void Log(const char*,...) noexcept {}
bool IsFuelTank(const unsigned char* w) noexcept { return w[0x100]==3; }
bool IsLoadoutWeapon(const unsigned char* w) noexcept { return w[0x100]==2; }
bool IsStoreWeapon(const unsigned char* w) noexcept { return w[0x100]==2; }
const StoreSpec airSpec{L"AA", "AA", StoreRole::air,0,0},groundSpec{L"AG", "AG", StoreRole::ground,0,0};
const StoreSpec* StoreOf(const unsigned char* w) noexcept { return w[0x101]==1 ? &airSpec : w[0x101]==2 ? &groundSpec : nullptr; }
int fileQueries=0;
const wchar_t* WeaponFile(const unsigned char* w,std::size_t* n) noexcept {
    ++fileQueries;const auto file=At<const wchar_t*>(w,0x08);*n=file ? std::wcslen(file) : 0;return file;
}
void ClearWeaponLock(unsigned char*) noexcept {}
int WeaponLock(const unsigned char*,float*,float*) noexcept { return 0; }
bool ReadRound(const unsigned char* w,RoundModel* m) noexcept {
    *m={};m->kind=w[0x101] ? RoundKind::homing : RoundKind::arc;m->speed=2;m->alive=300;return true;
}
namespace audio { void LockTone(int,float) noexcept {} }
}
namespace edf { bool MeanMuzzle(const unsigned char* w,std::uint64_t,float*,float*) noexcept { return w[0x102]!=1; } }
int main() {
    using namespace crew;
    int fail=0,checks=0;
    auto check=[&](bool ok,const char* msg){++checks;if(!ok){++fail;std::printf("FAIL: %s\n",msg);}};
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char vehicles[2][0x2400]{},seats[2][edf::kSeatStride*2]{},ctrl[2][16]{},riders[2][0x2000]{};
    unsigned char weapons[2][3][0x1600]{},holders[2][3][0x80]{};
    unsigned char* seatHolders[2][3]{};
    cfg.enabled=cfg.stockStores=cfg.npcGunners=true;pullOk=true;
    for(int j=0;j<2;++j) {
        auto v=vehicles[j];Put<void*>(v,0,image+kVt403);Put<void*>(v,kSelfCtrl,ctrl[j]);
        Put<void*>(v,kSeats,seats[j]);Put<std::uint64_t>(v,kSeatCount,2);Put<void*>(v,kHolders,holders[j]);Put<std::uint64_t>(v,kHolderCount,3);
        Put<int>(ctrl[j],8,1);
        auto seat=SeatAt(v,0);Put<void*>(seat,kSeatRider,riders[j]);Put<void*>(seat,kSeatRiderCtrl,ctrl[j]);
        for(int i=0;i<3;++i) {
            auto w=weapons[j][i];seatHolders[j][i]=holders[j][i];Put<void*>(holders[j][i],kHolderWeapon,w);Put<void*>(holders[j][i],kHolderCtrl,ctrl[j]);
            w[0x100]=i ? 2 : 0;w[0x101]=static_cast<unsigned char>(i);
            Put<int>(w,kWeaponAmmo,10);Put<int>(w,kWeaponCapacity,10);Put<int>(w,kWeaponReloadTime,-1);
            Put<float>(w,0x224,1000);Put<float>(w,0x89C,10);Put<float>(w,0x6D0,1000);Put<const wchar_t*>(w,kWeaponName,i ? L"missile" : L"cannon");
            Put<const wchar_t*>(w,0x08,i ? L"EDF6VC_AAM_S_2.SGO" : L"V_403TANK_CANNON01.SGO");
        }
        Put<void*>(seat,kSeatWeapons,seatHolders[j]);Put<std::uint64_t>(seat,kSeatWeaponCount,3);
    }
    check(NpcPayloadSelect(vehicles[0],0,75,false)==weapons[0][0],"driver uses loaded main cannon at close ground range");
    check(NpcPayloadSelect(vehicles[0],0,800,false)==weapons[0][2],"driver uses ground missile beyond cannon lifetime range");
    Put<int>(weapons[0][2],kWeaponAmmo,0);
    check(!NpcPayloadSelect(vehicles[0],0,800,false),"exhausted ground missile cannot be replaced by out-of-range cannon or AA missile");
    Put<int>(weapons[0][2],kWeaponAmmo,10);
    check(NpcPayloadSelect(vehicles[0],0,400,true)==weapons[0][1],"air target selects actual AA store");
    Put<const wchar_t*>(weapons[0][1],0x08,L"EDF6VC_COAX_MG.SGO");
    check(NpcPayloadSelect(vehicles[0],0,400,true)==weapons[0][0],"NPC excludes exact retired coax resource");
    Put<const wchar_t*>(weapons[0][1],0x08,L"EDF6VC_AAM_S_2.SGO");
    NpcPayloadSelect(vehicles[0],0,400,true);
    PullHook(holders[0][0]);check(weapons[0][1][kWeaponTrigger]==1 && !weapons[0][0][kWeaponTrigger],"native pull redirected once to selected weapon");
    check(NpcPayloadSelect(vehicles[1],0,400,false)==weapons[1][2],"second vehicle independently selects ground missile");
    PullHook(holders[0][0]);PullHook(holders[1][0]);check(weapons[0][1][kWeaponTrigger] && weapons[1][2][kWeaponTrigger],"multiple vehicle selections coexist");
    Put<int>(weapons[0][1],kWeaponAmmo,0);
    check(NpcPayloadSelect(vehicles[0],0,400,true)==weapons[0][0],"empty AA falls back to reachable installed cannon");
    Put<int>(weapons[0][0],kWeaponAmmo,0);
    check(!NpcPayloadSelect(vehicles[0],0,400,true),"ground missile cannot replace exhausted AA against aircraft");
    check(!NpcPayloadSelect(vehicles[1],0,1500,false),"all out of range returns no usable weapon");
    Put<int>(weapons[0][1],kWeaponAmmo,10);weapons[0][1][0x102]=1;
    check(!NpcPayloadSelect(vehicles[0],0,400,true),"missing muzzle cannot create a usable mount");
    weapons[0][1][0x102]=0;Put<float>(weapons[0][1],0x89C,-150);
    check(!NpcPayloadSelect(vehicles[0],0,400,true),"healer never selected for enemy");
    Put<float>(weapons[0][1],0x89C,10);NpcPayloadSelect(vehicles[0],0,400,true);
    weapons[0][1][kWeaponTrigger]=0;authority=false;PullHook(holders[0][0]);
    check(!weapons[0][1][kWeaponTrigger],"authority loss invalidates redirect before another shot");
    check(!NpcPayloadSelect(vehicles[0],0,400,true),"remote seat cannot select locally");authority=true;
    NpcPayloadSelect(vehicles[0],0,400,true);now+=201;PullHook(holders[0][0]);
    check(!weapons[0][1][kWeaponTrigger],"expired selection is not reused");
    // A removed or expired holder must not leave a stale redirect to an old weapon.
    now+=1;NpcPayloadSelect(vehicles[0],0,400,true);
    Put<std::uint64_t>(SeatAt(vehicles[0],0),kSeatWeaponCount,1);PullHook(holders[0][0]);
    check(!weapons[0][1][kWeaponTrigger],"removed store cannot receive a stale redirect");
    Put<std::uint64_t>(SeatAt(vehicles[0],0),kSeatWeaponCount,3);
    Put<int>(ctrl[0],8,0);
    check(!NpcPayloadSelect(vehicles[0],0,400,true),"expired holder is not an installed weapon");
    Put<int>(ctrl[0],8,1);
    // Real human control must never be selected by the NPC path.
    riders[0][edf::kHumanPlayer]=1;Put<void*>(riders[0],edf::kHumanPad,riders[0]);
    trackedHuman=riders[0];
    check(!NpcPayloadSelect(vehicles[0],0,400,true),"local human seat remains player controlled");
    PayloadFrame(vehicles[0]);PayloadReadout readout{};
    check(PlayerPayload(&readout) && readout.count==3 && readout.choices==3,"player HUD lists three real mounted weapons");
    check(readout.entry[0].rounds==0 && !wcscmp(readout.entry[0].name,L"cannon"),"HUD reports weapon identity and actual remaining rounds");
    Put<const wchar_t*>(weapons[0][1],0x08,L"edf6vc_coax_mg.sgo");PayloadFrame(vehicles[0]);
    check(PlayerPayload(&readout) && readout.count==2 && readout.choices==2,"player HUD and switching exclude retired coax data");
    for(const wchar_t* file:{L"V_403TANK_MACHINEGUN.SGO",L"EDF6VC_COAX_MG.SGO.OTHER"}) {
        Put<const wchar_t*>(weapons[0][1],0x08,file);PayloadFrame(vehicles[0]);
        check(PlayerPayload(&readout) && readout.count==3,"stock machine gun and other custom filenames remain mounted");
    }
    check(fileQueries>0,"selection queries real resource filenames");
    Put<std::uint64_t>(SeatAt(vehicles[0],0),kSeatWeaponCount,1);PayloadFrame(vehicles[0]);
    check(PlayerPayload(&readout) && readout.count==1 && readout.choices==0,"vehicle without additional mounts has no invented switch choices");
    ResetPayload();check(!PayloadPicked(vehicles[0]),"mission reset clears choices");
    VirtualFree(image,0,MEM_RELEASE);std::printf("payload runtime: %d checks, %d failures\n",checks,fail);return fail ? 1 : 0;
}
