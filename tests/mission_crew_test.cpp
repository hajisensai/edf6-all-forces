#include "../src/mission_crew.cpp"
#include <cstdio>
#include <cstdlib>
namespace crew {
unsigned char* image=nullptr;Config config{};
const Config& Cfg() noexcept {return config;}
void Log(const char*,...) noexcept {}
void EnsureInputs() noexcept {}
bool PrepareNpcVehicle(unsigned char*,bool) noexcept {return true;}
bool NpcDriver(const unsigned char*) noexcept {return false;}
bool OnlineMaySeatNpc(const void*) noexcept {return true;}
}
namespace {
int requests=0,checks=0;
bool Factory(unsigned char*,bool) noexcept {++requests;return true;}
bool Id(const unsigned char* v,unsigned char* out) noexcept {std::memset(out,0,32);std::memcpy(out,&v,sizeof(v));return true;}
void Check(bool b,const char* why){++checks;if(!b){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
}
int main() {
 using namespace crew;
 image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 Check(image!=nullptr,"private native image");
 unsigned tables[2]={0x1000,0x2000};
 Put<void*>(image+tables[0],50*8,image+kStockRide);Put<void*>(image+tables[1],50*8,image+kProteusRide);
 std::memcpy(image+kStockRide,kRideSig,sizeof(kRideSig));
 Check(InstallMissionCrewHooks(tables,2),"both stock and Proteus slots intercepted");
 Check(At<void*>(image+tables[0],50*8)==reinterpret_cast<void*>(&RideHook) &&
       At<void*>(image+tables[1],50*8)==reinterpret_cast<void*>(&RideHook),"Proteus cannot create extra Dummy seats");
 unsigned char vehicle[0xE40]{},ctrl[16]{};
 Put<void*>(vehicle,0,image+tables[0]);Put<void*>(vehicle,kSelfCtrl,ctrl);Put<int>(ctrl,8,1);
 auto call=reinterpret_cast<RideFn>(At<void*>(image+tables[0],50*8));
 call(vehicle,true);
 Check(At<int>(vehicle,0xE30)==1 && entries[0].requested,"script creation retains intercepted snapshot origin");
 MissionCrewVehicleFrame(vehicle);Check(requests==0,"unready support waits without manufacturing a rider");
 ConfigureMissionCrew(&Factory,&Id);MissionCrewVehicleFrame(vehicle);
 Check(requests==1 && entries[0].dispatched,"unified support factory dispatches script crew once");
 MissionCrewVehicleFrame(vehicle);Check(requests==1,"frame updates do not duplicate deployment");
 unsigned char id[32]{};Id(vehicle,id);Check(MissionVehicleById(id)==vehicle,"existing vehicle resolved by canonical ID");
 call(vehicle,false);MissionCrewVehicleFrame(vehicle);
 Check(requests==2 && entries[0].restored,"snapshot restore reuses real-crew request hook");
 vehicle[kDead]=1;Check(MissionVehicleById(id)==nullptr,"dead vehicle cannot receive network crew");vehicle[kDead]=0;
 ResetMissionCrew();Check(MissionVehicleById(id)==nullptr,"mission reset discards prior vehicle identities");
 VirtualFree(image,0,MEM_RELEASE);std::printf("mission_crew: %d checks passed\n",checks);
}
