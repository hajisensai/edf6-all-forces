// Runs the production transaction against explicitly synthetic native-service callbacks. No game
// process, DLL entry point, renderer, Havok, or installation is touched.
#include "../src/support_spawn.cpp"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <limits>

namespace fixture {
bool signatures=true,failSetup=false,badClass=false,occupied=false;
int checks=0,preloads=0,makes=0,applies=0,disposals=0,deletes=0,teams=0,levels=0,owners=0;
alignas(16) unsigned char object[0x2000]{},seats[5*edf::kSeatStride]{};
const void* expectedOwner=nullptr;
float lastPosition[3]{};
void Check(bool ok,const char* what) { ++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);} }
void Preload(void*,const wchar_t* path,std::int32_t type,std::int32_t slot) {
    Check(path && type==2 && slot==-1,"native vehicle preload arguments");++preloads;
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
    for(const auto& row:crew::kSupportVehicles)if(std::wcscmp(path,row.sgo)==0) {
        edf::Put<const void*>(object,0,crew::image+row.vtable+(badClass ? 8 : 0));
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
    PreloadSupportVehicles();Check(fixture::preloads==3,"all audited resources preloaded");
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
    Check(fixture::lastPosition[0]==100 && fixture::lastPosition[1]==20 && fixture::lastPosition[2]==300,"entry retained exactly");
    Check(!DeleteSupportVehicle(v+16),"unowned hull cannot be rolled back");
    const void* oldControl=At<const void*>(v,kSelfCtrl);
    Put<const void*>(v,kSelfCtrl,nullptr);
    Check(!DeleteSupportVehicle(v),"reused address with different control block rejected");
    Put<const void*>(v,kSelfCtrl,oldControl);
    fixture::occupied=true;Check(!DeleteSupportVehicle(v),"occupied hull not deleted by rollback");
    fixture::occupied=false;Check(DeleteSupportVehicle(v) && fixture::deletes==1,"empty owned hull rollback");
    Check(!DeleteSupportVehicle(v),"rollback idempotent");
    fixture::failSetup=true;
    Check(!SpawnSupportVehicle(tank,manned,at,forward,nullptr),"setup fault cannot return partial hull");
    Check(fixture::disposals==2 && fixture::deletes==2 && fixture::owners==1,"setup fault disposes variant and hull before ownership");
    PreloadSupportVehicles();Check(!SupportVehicleReady(tank,manned),"faulted resource stays disabled across mission reset");
    fixture::failSetup=false;
    v=SpawnSupportVehicle(SupportVehicleKind::civilianTruck,SupportCrewMode::unmanned,at,forward,nullptr);
    Check(v && SeatCount(v)==5,"empty-delivery mode creates same real five-seat hull, no dummy");
    Check(DeleteSupportVehicle(v),"truck rollback");
    fixture::badClass=true;
    Check(!SpawnSupportVehicle(SupportVehicleKind::transport,manned,at,forward,nullptr),"wrong class rejected and deleted");
    Check(fixture::deletes==4,"wrong class rollback once");
    fixture::signatures=false;PreloadSupportVehicles();
    Check(!SupportVehicleReady(SupportVehicleKind::civilianTruck,manned),"profile mismatch clears old mission readiness");
    Check(SupportVehicleEnvironmentAllowed(tank,SupportEnvironment::underground),"underground vehicle decision belongs to real route geometry");
    Check(!SupportVehicleEnvironmentAllowed(tank,SupportEnvironment::wasteland),"military tank excluded from wasteland catalog");
    Check(SupportVehicleEnvironmentAllowed(SupportVehicleKind::civilianTruck,SupportEnvironment::wasteland),"civilian truck valid in wasteland");
    Check(!SupportVehicleEnvironmentAllowed(tank,static_cast<SupportEnvironment>(255)),"unknown environment fails closed");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("support_spawn_test: %d checks passed\n",fixture::checks);
}
