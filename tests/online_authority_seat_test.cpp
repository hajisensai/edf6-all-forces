// Read production seat/weak/network flags from stand-in game objects, then count authorities on both machines.
#include "../src/online_authority.cpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
namespace crew {
unsigned char imageBytes[1]{};
unsigned char* image=imageBytes;
bool InSession() noexcept { return true; }
bool KnownVehicle(const void*) noexcept { return true; }
void Log(const char*,...) noexcept {}
}
namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
}
int main() {
    using namespace crew;
    alignas(16) unsigned char hostVehicle[0x700]{},clientVehicle[0x700]{},hostSeat[0x340]{},clientSeat[0x340]{};
    alignas(16) unsigned char dummy[0x200]{},driverObject[0x200]{},dummyCtrl[16]{},playerCtrl[16]{};
    Put<void*>(hostVehicle,kSeats,hostSeat);Put<std::uint64_t>(hostVehicle,kSeatCount,1);
    Put<void*>(clientVehicle,kSeats,clientSeat);Put<std::uint64_t>(clientVehicle,kSeatCount,1);
    Put<LONG>(dummyCtrl,8,1);Put<LONG>(playerCtrl,8,1);Put<std::uint16_t>(driverObject,0x128,1);
    Put<void*>(hostSeat,kSeatRider,dummy);Put<void*>(hostSeat,kSeatRiderCtrl,dummyCtrl);
    // The old client's driverObject remains alive in both last-rider fields. Native 630F90 calls it local on that client.
    for(unsigned char* seat:{hostSeat,clientSeat}) { Put<void*>(seat,0x300,driverObject);Put<void*>(seat,0x308,playerCtrl); }
    Check(HostSeat0(hostVehicle) && HostSeat0(clientVehicle),"host dummy and client empty select the same host-seat rule");
    auto h=online::Facts{true,true,true,2,true,HostSeat0(hostVehicle),online::kCopyHost,1};
    auto c=online::Facts{true,true,false,1,true,HostSeat0(clientVehicle),online::kCopyHost,1};
    Check(online::Authority(h) && !online::Authority(c),"client's stock last-driver local answer cannot create a second authority");
    Check(online::ShotCounts(h,online::Shooter::vehicle) && !online::ShotCounts(c,online::Shooter::vehicle),
          "host NPC takeover deals the plugin round exactly once");
    Put<void*>(hostSeat,kSeatRider,nullptr);Put<void*>(hostSeat,kSeatRiderCtrl,nullptr);
    Check(HostSeat0(hostVehicle) && HostSeat0(clientVehicle),"both empty still select host with a live last driver");
    Put<void*>(hostSeat,kSeatRider,driverObject);Put<void*>(hostSeat,kSeatRiderCtrl,playerCtrl);
    Put<void*>(clientSeat,kSeatRider,driverObject);Put<void*>(clientSeat,kSeatRiderCtrl,playerCtrl);
    Check(!HostSeat0(hostVehicle) && !HostSeat0(clientVehicle),"registered current driver restores stock-runner selection");
    h.hostSeat0=HostSeat0(hostVehicle);h.stock=2;c.hostSeat0=HostSeat0(clientVehicle);c.stock=1;
    Check(!online::Authority(h) && online::Authority(c),"new client driver owns the vehicle once");
    Put<LONG>(playerCtrl,8,0);
    Check(HostSeat0(hostVehicle) && HostSeat0(clientVehicle),"expired current driver returns both copies to host");
    std::printf("online_authority_seat_test: %d checks passed\n",checks);
}
