// Runs the production transaction against explicitly synthetic native-service callbacks. No game
// process, DLL entry point, renderer, Havok, or installation is touched.
#include <cwchar>
#include "../src/scene.h"
// Whether the installer wrote Mods/OBJECT/<file>: the fixture's answer (the drill transport's SGO, the Vehicler's vehicles').
namespace fixture {
bool drillFile=true,zwFiles=false;int drillFileAsked=0;
inline bool DrillFileThere(const wchar_t* dir,const wchar_t* file) noexcept {
    ++drillFileAsked;
    if(!dir || !file || std::wcscmp(dir,L"OBJECT")!=0)return false;
    if(std::wcscmp(file,L"EDF6VC_DRILL_TRANSPORT.SGO")==0)return drillFile;
    return zwFiles && std::wcsncmp(file,L"EDF6VC_ZW_",10)==0;   // every Vehicler vehicle's file (zw_vehicles.inc)
}
}
#define EDF6_SUPPORT_SPAWN_MOD_FILE(dir,file) fixture::DrillFileThere(dir,file)
#include "../src/support_spawn.cpp"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <limits>

namespace fixture {
bool signatures=true,failSetup=false,badClass=false,occupied=false,variantBadClass=false,failMark=false,failFirepower=false,
     firepowerReady=true;
int marks=0,firepowers=0,markedAt=-1;crew::supportlevel::Curve lastCurve=crew::supportlevel::Curve::eros;
const wchar_t* lastPath=nullptr;
const wchar_t* const kLoaded=L"app:/object/edf6vc_lo_tank_4000000000000c81.sgo";
int checks=0,preloads=0,weaponPreloads=0,makes=0,applies=0,disposals=0,deletes=0,teams=0,levels=0,owners=0;
alignas(16) unsigned char object[0x2000]{},seats[13*edf::kSeatStride]{};
const void* expectedOwner=nullptr;
float lastPosition[3]{};
void Check(bool ok,const char* what) { ++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);} }
void Preload(void*,const wchar_t* path,std::int32_t type,std::int32_t slot) {
    Check(path && type==2 && slot==-1,"native vehicle preload arguments");
    // A hull's weapons are queued with it (vehicle_weapons.h): counted apart from the hulls.
    if(!_wcsnicmp(path,L"app:/weapon/",12))++weaponPreloads;else ++preloads;
}
void ReadSetup(void*,void* setup) { edf::Put<std::uint16_t>(setup,0x10,0); }
void Apply(void*,void*) {
    ++applies;
    if(failSetup)RaiseException(0xE0010001,0,0,nullptr);
}
void Dispose(void*,void*) { ++disposals; }
void Delete(void*) { ++deletes; }
void Team(void*,std::int32_t team,bool registered) { Check(team==2 && registered,"friend registration");++teams; }
void Level(void*,float level) { Check(level==1.0f,"mission level uses native scaling");++levels; }
unsigned char* Create(void*,const float* matrix,const wchar_t* path,crew::InitParam* param) {
    ++makes;
    Check(param->vtable==crew::image+crew::kInitVtable,"native init parameter");
    for(int i=0;i<3;++i)lastPosition[i]=matrix[12+i];
    lastPath=path;
    // A loaded tank (support_loadout.h VehicleVariantFile): the stock tank's class and seats, holders past the stock one.
    const bool loaded=std::wcscmp(path,kLoaded)==0;
    static crew::SupportVehicleSpec zw[crew::kZwVehicleCount]{};   // every Vehicler vehicle (zw_vehicles.inc)
    for(int k=0;k<crew::kZwVehicleCount;++k)zw[k]=crew::ZwHullSpec(crew::kZwVehicles[k]);
    const crew::SupportVehicleSpec* rows[4+crew::kZwVehicleCount]={&crew::kSupportVehicles[0],&crew::kSupportVehicles[1],
                                                                   &crew::kSupportVehicles[2],&crew::kSupportDrillTransport};
    for(int k=0;k<crew::kZwVehicleCount;++k)rows[4+k]=&zw[k];
    for(const auto* each:rows)if(const auto& row=*each;std::wcscmp(path,row.sgo)==0 || (loaded && &row==&crew::kSupportVehicles[0])) {
        edf::Put<const void*>(object,0,crew::image+row.vtable+(badClass || (loaded && variantBadClass) ? 8 : 0));
        edf::Put<unsigned>(object,edf::kSeatCount,row.seats);
        edf::Put<unsigned char*>(object,edf::kSeats,seats);
        edf::Put<const void*>(object,edf::kSelfCtrl,seats+makes);
        edf::Put<crew::SetupFn>(crew::image+row.vtable,crew::kApplySetupSlot*8,&Apply);
        return object;
    }
    return nullptr;
}
template<class F> void Jump(unsigned rva,F fn) {
    unsigned char code[]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto address=reinterpret_cast<std::uintptr_t>(fn);
    std::memcpy(code+2,&address,8);std::memcpy(crew::image+rva,code,sizeof(code));
}
}
namespace edf {
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept { return fixture::signatures; }
bool Readable(const void* p,std::size_t,bool) noexcept { return p!=nullptr; }
unsigned SeatCount(const unsigned char* v) noexcept { return At<unsigned>(v,kSeatCount); }
Rider SeatRider(const unsigned char*,const unsigned char*) noexcept { return fixture::occupied ? Rider::player : Rider::none; }
}
namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
// zw_vehicle_catalog.cpp: the mission's snapshot, taken with the preload.
int snapshots=0;
void SnapshotZwVehicles() noexcept { ++snapshots; }
// support_firepower.cpp: an armed hull is marked before its setup, its firepower written after it.
bool MarkSupportFirepower(unsigned char* v,supportlevel::Curve curve) noexcept {
    fixture::Check(v==fixture::object,"the made hull marked");
    ++fixture::marks;fixture::markedAt=fixture::applies;fixture::lastCurve=curve;
    return !fixture::failMark;
}
bool SupportFirepowerReady() noexcept { return fixture::firepowerReady; }
bool ApplySupportFirepower(unsigned char* v) noexcept {
    // An unarmed hull (the light truck, the drill transport) was never marked: the real one leaves it as the game's.
    if(fixture::markedAt<0)return true;
    fixture::Check(v==fixture::object && fixture::applies>fixture::markedAt,"firepower written after its setup");
    fixture::markedAt=-1;
    ++fixture::firepowers;
    return !fixture::failFirepower;
}
void NoteLocalCopy(const void* v,const void* owner) noexcept {
    fixture::Check(v==fixture::object && owner==fixture::expectedOwner,"copy owner recorded");
    fixture::Check(fixture::applies>0 && fixture::teams>0 && fixture::levels>0,"owner recorded after setup/team/level");
    ++fixture::owners;
}
}
int main() {
    using namespace crew;using fixture::Check;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"allocate synthetic image");
    fixture::Jump(kPreload,&fixture::Preload);fixture::Jump(kCreate,&fixture::Create);
    fixture::Jump(kReadSetup,&fixture::ReadSetup);fixture::Jump(kDelete,&fixture::Delete);
    fixture::Jump(kSetTeam,&fixture::Team);fixture::Jump(kSetLevel,&fixture::Level);
    Put<SetupFn>(image,kSetupDtors,&fixture::Dispose);
    Put<void*>(image,kPreloadMgr,image);Put<void*>(image,kObjectMgr,image);
    const float at[]={100,20,300},forward[]={1,0,1},zero[]={0,0,0};
    const auto tank=SupportVehicleKind::tank;const auto manned=SupportCrewMode::soldiers;
    Check(!SpawnSupportVehicle(tank,manned,at,forward,nullptr),"no spawn before mission preload");
    Check(!SupportDrillTransportReady() && !SpawnSupportDrillTransport(at,forward,nullptr),"no drill transport before mission preload");
    PreloadSupportVehicles();Check(fixture::preloads==4,"all audited resources preloaded, and the installer's drill transport");
    Check(fixture::weaponPreloads==3,"the hulls' weapons queued with them: the Blacker's two, the rescue carrier's one");
    Check(SupportDrillTransportReady(),"the drill transport preloaded");
    Check(!SpawnSupportVehicle(static_cast<SupportVehicleKind>(255),manned,at,forward,nullptr),"reject invalid resource");
    Check(!SpawnSupportVehicle(tank,static_cast<SupportCrewMode>(255),at,forward,nullptr),"reject invalid mode");
    Check(!SpawnSupportVehicle(tank,manned,at,zero,nullptr),"reject missing heading without inventing one");
    float bad[]={std::numeric_limits<float>::quiet_NaN(),0,0};
    Check(!SpawnSupportVehicle(tank,manned,bad,forward,nullptr),"reject nonfinite entry");
    Check(fixture::makes==0,"validation precedes native construction");
    fixture::expectedOwner=fixture::seats;
    auto v=SpawnSupportVehicle(tank,manned,at,forward,fixture::expectedOwner);
    fixture::expectedOwner=nullptr;
    Check(v && fixture::applies==1 && fixture::disposals==1 && fixture::owners==1,"successful complete initialization");
    Check(fixture::marks==1 && fixture::firepowers==1 && fixture::lastCurve==supportlevel::Curve::blacker,
          "the tank's firepower by level on the Blacker's request curve");
    Check(fixture::lastPosition[0]==100 && fixture::lastPosition[1]==20 && fixture::lastPosition[2]==300,"entry retained exactly");
    Check(!DeleteSupportVehicle(v+16),"unowned hull cannot be rolled back");
    const void* oldControl=At<const void*>(v,kSelfCtrl);
    Put<const void*>(v,kSelfCtrl,nullptr);
    Check(!DeleteSupportVehicle(v),"reused address with different control block rejected");
    Put<const void*>(v,kSelfCtrl,oldControl);
    fixture::occupied=true;Check(!DeleteSupportVehicle(v),"occupied hull not deleted by rollback");
    fixture::occupied=false;Check(DeleteSupportVehicle(v) && fixture::deletes==1,"empty owned hull rollback");
    Check(!DeleteSupportVehicle(v),"rollback idempotent");
    // A loaded tank (SupportVehicle_<key>): made from its generated file, checked as the stock hull is.
    v=SpawnSupportVehicle(tank,manned,at,forward,nullptr,fixture::kLoaded);
    Check(v && !std::wcscmp(fixture::lastPath,fixture::kLoaded),"a loaded tank is made from its own file");
    Check(DeleteSupportVehicle(v),"loaded tank rollback");
    fixture::variantBadClass=true;
    Check(!SpawnSupportVehicle(tank,manned,at,forward,nullptr,fixture::kLoaded),"a loaded file of another class is refused");
    Check(SupportVehicleReady(tank,manned),"a bad loaded file refuses itself only: the stock tank stays callable");
    fixture::variantBadClass=false;
    v=SpawnSupportVehicle(tank,manned,at,forward,nullptr);
    Check(v && !std::wcscmp(fixture::lastPath,kSupportVehicles[0].sgo),"no variant: the stock V505_TANK_MISSION");
    Check(DeleteSupportVehicle(v),"stock tank rollback");
    // Its firepower not marked (no level, the marks full): this hull refused and deleted, before any setup; the tank
    // stays callable (not a broken resource).
    {
        const int applies=fixture::applies,deletes=fixture::deletes;
        fixture::failMark=true;
        Check(!SpawnSupportVehicle(tank,manned,at,forward,nullptr),"an unmarked tank is refused");
        Check(fixture::applies==applies && fixture::deletes==deletes+1,"an unmarked tank is deleted before its setup");
        fixture::failMark=false;
        Check(SupportVehicleReady(tank,manned),"an unmarked tank does not disable the tank");
        v=SpawnSupportVehicle(tank,manned,at,forward,nullptr);
        Check(v,"the next tank is made");
        Check(DeleteSupportVehicle(v),"the next tank's rollback");
    }
    // The firepower step not installed: the armed hulls are not ready (none made to be refused), the unarmed truck is.
    fixture::firepowerReady=false;
    Check(!SupportVehicleReady(tank,manned) && !SupportVehicleReady(SupportVehicleKind::transport,manned),
          "no firepower step: no armed support hull");
    Check(SupportVehicleReady(SupportVehicleKind::civilianTruck,SupportCrewMode::unmanned),"no firepower step: the truck still");
    fixture::firepowerReady=true;
    fixture::failSetup=true;
    Check(!SpawnSupportVehicle(tank,manned,at,forward,nullptr),"setup fault cannot return partial hull");
    Check(fixture::disposals==5 && fixture::deletes==7 && fixture::owners==4,"setup fault disposes variant and hull before ownership");
    PreloadSupportVehicles();Check(!SupportVehicleReady(tank,manned),"faulted resource stays disabled across mission reset");
    fixture::failSetup=false;
    v=SpawnSupportVehicle(SupportVehicleKind::civilianTruck,SupportCrewMode::unmanned,at,forward,nullptr);
    Check(v && SeatCount(v)==5,"empty-delivery mode creates same real five-seat hull, no dummy");
    Check(!kSupportVehicles[2].armed,"the light truck has no weapons to level");
    Check(DeleteSupportVehicle(v),"truck rollback");
    // The transport: the Grape's curve; its firepower not marked / not written: refused, no partial hull.
    v=SpawnSupportVehicle(SupportVehicleKind::transport,manned,at,forward,nullptr);
    Check(v && fixture::lastCurve==supportlevel::Curve::grape,"the transport's firepower on the Grape's request curve");
    Check(DeleteSupportVehicle(v),"transport rollback");
    // The drill transport (underground infantry, support_dispatch.cpp DrillPlan): its own SGO, the Blacker's class with
    // thirteen seats, no firepower to level (its one weapon is the drill's bit); rolled back as any support hull.
    {
        const int marks=fixture::marks;
        v=SpawnSupportDrillTransport(at,forward,nullptr);
        Check(v && fixture::lastPath && std::wcscmp(fixture::lastPath,L"app:/Object/EDF6VC_DRILL_TRANSPORT.SGO")==0 && SeatCount(v)==13 &&
              fixture::marks==marks && fixture::lastPosition[0]==at[0] && fixture::lastPosition[2]==at[2],
              "the drill transport made from its own SGO at the entry: thirteen seats, nothing marked");
        Check(DeleteSupportVehicle(v),"drill transport rollback");
        Check(kSupportDrillTransport.vtable==kSupportVehicles[0].vtable && !kSupportDrillTransport.armed &&
              kSupportDrillTransport.seats==13 && kSupportDrillTransportResource>=0x20000u+kSupportVehicleCount &&
              kSupportDrillTransportResource<0x30000u,"the Blacker's class, unarmed, a ground hull resource past the kinds'");
        // Not installed (a broken install): not preloaded, refused; the stock hulls stay callable.
        fixture::drillFile=false;const int preloads=fixture::preloads;PreloadSupportVehicles();
        Check(fixture::preloads==preloads+2 && !SupportDrillTransportReady() && !SpawnSupportDrillTransport(at,forward,nullptr) &&
              SupportVehicleReady(SupportVehicleKind::transport,manned),"its SGO not installed: not preloaded, refused, the stock hulls stay");
        fixture::drillFile=true;PreloadSupportVehicles();
        Check(SupportDrillTransportReady(),"installed again: preloaded at the next mission");
    }
    // The Vehicler's vehicles (zw_vehicle_catalog.h): the installer's files, preloaded with their weapons when installed,
    // made from their own SGO with the table's class / seats, their firepower on their curve; each kind refused alone.
    {
        Check(snapshots>0,"the Vehicler snapshot is taken with each mission's preload");
        Check(!ZwVehicleReady(0) && !SpawnZwVehicle(0,at,forward,nullptr),"not installed: no Vehicler vehicle");
        fixture::zwFiles=true;const int preloads=fixture::preloads,snaps=snapshots;PreloadSupportVehicles();
        Check(fixture::preloads==preloads+3+kZwVehicleCount && snapshots==snaps+1,"installed: every Vehicler vehicle preloaded");
        for(int k=0;k<kZwVehicleCount;++k)Check(ZwVehicleReady(k),"a Vehicler vehicle ready");
        Check(!ZwVehicleReady(-1) && !ZwVehicleReady(kZwVehicleCount) && !SpawnZwVehicle(kZwVehicleCount,at,forward,nullptr),
              "no such kind");
        for(int k=0;k<kZwVehicleCount;++k) {
            const int marks=fixture::marks;
            v=SpawnZwVehicle(k,at,forward,nullptr);
            Check(v && !std::wcscmp(fixture::lastPath,kZwVehicles[k].sgo) && SeatCount(v)==kZwVehicles[k].seats &&
                  fixture::marks==marks+1 && fixture::lastCurve==kZwVehicles[k].curve && fixture::lastPosition[0]==at[0],
                  "a Vehicler vehicle made from its own SGO, its firepower on its curve, at the entry");
            Check(DeleteSupportVehicle(v),"a Vehicler vehicle's rollback");
        }
        // Not wanted by this mission's set: not preloaded, not ready.
        {constexpr int b=static_cast<int>(preload::Item::zwVehicle0);preload::detail::missionSet[b>>6].store(~(std::uint64_t{1}<<(b&63)));}
        PreloadSupportVehicles();
        Check(!ZwVehicleReady(0) && ZwVehicleReady(1),"a kind this mission's set lacks is not preloaded; the others are");
        preload::detail::missionSet[0].store(~std::uint64_t{0});preload::detail::missionSet[1].store(~std::uint64_t{0});PreloadSupportVehicles();
        // The game made another class of it: deleted, that kind off until restart, the others and the stock hulls stay.
        fixture::badClass=true;const int deletes=fixture::deletes;
        Check(!SpawnZwVehicle(1,at,forward,nullptr) && fixture::deletes==deletes+1,"a Vehicler vehicle of another class: refused, deleted");
        fixture::badClass=false;PreloadSupportVehicles();
        Check(!ZwVehicleReady(1) && ZwVehicleReady(0) && ZwVehicleReady(2) && SupportVehicleReady(SupportVehicleKind::transport,manned),
              "a broken Vehicler vehicle stays off; the others and the stock hulls stay");
        fixture::zwFiles=false;
    }
    const int deletes=fixture::deletes;
    fixture::badClass=true;
    Check(!SpawnSupportVehicle(SupportVehicleKind::transport,manned,at,forward,nullptr),"wrong class rejected and deleted");
    Check(fixture::deletes==deletes+1,"wrong class rollback once");
    // A drill transport the game made of another class: deleted, and off until restart (as a stock row).
    Check(!SpawnSupportDrillTransport(at,forward,nullptr) && fixture::deletes==deletes+2,"a drill transport of another class: rejected, deleted");
    PreloadSupportVehicles();Check(!SupportDrillTransportReady(),"a broken drill transport stays off across mission reset");
    // The airdrop's container-made hull (ApplySupportVehicleSetup): not marked / its firepower not written: refused.
    fixture::badClass=false;Put<const void*>(fixture::object,0,image+kSupportVehicles[1].vtable);
    const int applies=fixture::applies;fixture::failMark=true;
    Check(!ApplySupportVehicleSetup(fixture::object,SupportVehicleKind::transport) && fixture::applies==applies,
          "an unmarked airdropped hull is refused before its setup");
    fixture::failMark=false;fixture::failFirepower=true;
    Check(!ApplySupportVehicleSetup(fixture::object,SupportVehicleKind::transport) && fixture::applies==applies+1,
          "an airdropped hull whose firepower failed is refused");
    fixture::failFirepower=false;
    Check(ApplySupportVehicleSetup(fixture::object,SupportVehicleKind::transport) && fixture::lastCurve==supportlevel::Curve::grape,
          "an airdropped transport fires on the Grape's curve");
    fixture::markedAt=-1;
    fixture::signatures=false;PreloadSupportVehicles();
    Check(!SupportVehicleReady(SupportVehicleKind::civilianTruck,manned),"profile mismatch clears old mission readiness");
    Check(!SupportVehicleEnvironmentAllowed(tank,SupportEnvironment::underground),"no support hull underground (only soldiers and the drill request)");
    Check(!SupportVehicleEnvironmentAllowed(SupportVehicleKind::civilianTruck,SupportEnvironment::underground),"nor the light truck");
    Check(!SupportVehicleEnvironmentAllowed(tank,SupportEnvironment::wasteland),"military tank excluded from wasteland catalog");
    Check(SupportVehicleEnvironmentAllowed(SupportVehicleKind::civilianTruck,SupportEnvironment::wasteland),"civilian truck valid in wasteland");
    Check(!SupportVehicleEnvironmentAllowed(tank,static_cast<SupportEnvironment>(255)),"unknown environment fails closed");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("support_spawn_test: %d checks passed\n",fixture::checks);
}
