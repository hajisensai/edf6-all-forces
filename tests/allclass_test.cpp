// Every class in every seat (src/allclass.cpp, src/seat_class.h, ini AllClassesRide), without the game: the real hooks
// run against a private image whose seat build (0x62B430) and CAS state call (0x116C2D0) are jumps to stand-ins.
//  - seatclass::Widen on every stock mask (15, 9, 143) and on 0: the stock seat test then admits all four classes in
//    each open seat, a closed seat (0, the Proteus rework's) stays closed, bit 7 (another team) is kept;
//  - SeatBuild: the stock build runs, the seat it appended (and only it) is widened; an entry that built no seat
//    changes nothing; with the option off (or the plugin off) the stock mask is kept;
//  - the Proteus rework's closing over a widened seat (proteus.cpp Seats / OpenSeats: save, write 0, put back): closed
//    to every class while closed, every class again after;
//  - the plugin's own seat tests (the same triple test as the stock's) agree with the stock test on every case;
//  - RideState: a state the rider's CAS has is played as asked; a refused one is followed by the bench pose
//    (vehicle_striker_rear) with the option on, not with it off; a null name is not retried.
#include "../src/allclass.cpp"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

namespace crew {
unsigned char* image=nullptr;
Config testConfig{};
const Config& Cfg() noexcept { return testConfig; }
void Log(const char*,...) noexcept {}
}  // namespace crew

namespace {
int checks=0;
void Check(bool b,const char* what) {
    ++checks;
    if(!b){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}
}

// The fake vehicle: its seat vector as the stock keeps it (+0x608 data, +0x618 count, stride 0x340).
unsigned char vehicle[0x700]{};
std::vector<unsigned char> seatData(8*edf::kSeatStride);
std::uint32_t nextMask=0;
bool buildsSeat=true;
int builds=0;

void __fastcall StockSeatBuild(unsigned char* v,void*) {
    ++builds;
    if(!buildsSeat)return;
    const auto count=crew::At<std::uint64_t>(v,edf::kSeatCount);
    unsigned char* seat=crew::At<unsigned char*>(v,edf::kSeats)+count*edf::kSeatStride;
    std::memset(seat,0,edf::kSeatStride);
    crew::Put<std::uint32_t>(seat,0x30,nextMask);
    crew::Put<std::uint32_t>(seat,0x34,0xFFFFFFFFu);   // the seat constructor's (0x6293A4)
    crew::Put<std::uint64_t>(v,edf::kSeatCount,count+1);
}

std::vector<std::wstring> casStates;   // the rider CAS's states
std::vector<std::wstring> played;      // every name asked of the CAS
bool __fastcall StockSetCasState(void*,std::int32_t,const wchar_t* name,std::int32_t) {
    played.push_back(name ? name : L"<null>");
    if(!name)return false;
    for(const auto& s:casStates)if(s==name)return true;
    return false;
}

// An absolute jump at image+rva to `to` (jmp [rip+0]; dq to).
void JumpAt(std::size_t rva,void* to) {
    unsigned char* at=crew::image+rva;
    const unsigned char jmp[6]={0xFF,0x25,0x00,0x00,0x00,0x00};
    std::memcpy(at,jmp,6);std::memcpy(at+6,&to,8);
}

void ResetVehicle() {
    std::memset(vehicle,0,sizeof(vehicle));
    crew::Put<unsigned char*>(vehicle,edf::kSeats,seatData.data());
    crew::Put<std::uint64_t>(vehicle,edf::kSeatCount,0);
    builds=0;buildsSeat=true;
}
unsigned char* Seat(unsigned i) { return seatData.data()+i*edf::kSeatStride; }
std::uint32_t MaskOf(unsigned i) { return crew::At<std::uint32_t>(Seat(i),0x30); }

// The stock test (0x6346FC) and the plugin's, which make the same one (npcai.cpp SeatTakes, seatswitch.cpp ClassMay,
// crew.cpp PlayerBoardingEntrance, heli.cpp DoorSeat).
bool StockTest(std::uint32_t human,const unsigned char* seat) {
    return seatclass::Admits(human,crew::At<std::uint32_t>(seat,0x34),crew::At<std::uint32_t>(seat,0x30));
}
bool PluginTest(std::uint32_t human,const unsigned char* seat) {
    return (human&crew::At<std::uint32_t>(seat,0x30)&crew::At<std::uint32_t>(seat,0x34))!=0;
}
constexpr std::uint32_t kClasses[4]={1,2,4,8};   // game_object_vehicle_mask: R, WD, F, AR
}  // namespace

int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"private image");
    JumpAt(0x62B430,reinterpret_cast<void*>(&StockSeatBuild));
    JumpAt(0x116C2D0,reinterpret_cast<void*>(&StockSetCasState));

    // The policy on every stock mask.
    Check(seatclass::Widen(15)==15 && seatclass::Widen(9)==15 && seatclass::Widen(143)==143 && seatclass::Widen(0)==0,
          "stock masks: 15 and 143 stay, 9 opens, 0 stays closed");
    Check(seatclass::Widen(0x80)==0x80 && seatclass::Widen(2)==15 && seatclass::Widen(0x81)==0x8F,"bit 7 kept; one class opens all");
    for(std::uint32_t m:{9u,15u,143u})for(std::uint32_t c:kClasses)
        Check(seatclass::Admits(c,0xFFFFFFFFu,seatclass::Widen(m)),"every class admitted by every widened stock mask");
    Check(!seatclass::Admits(2,0xFFFFFFFFu,9) && !seatclass::Admits(4,0xFFFFFFFFu,9) && seatclass::Admits(1,0xFFFFFFFFu,9) &&
          seatclass::Admits(8,0xFFFFFFFFu,9),"stock 9: Ranger and Air Raider only (the bug the option fixes)");
    for(std::uint32_t c:kClasses)Check(!seatclass::Admits(c,0xFFFFFFFFu,seatclass::Widen(0)),"a closed seat admits no class");

    // SeatBuild, option on: each appended seat widened.
    testConfig.enabled=true;testConfig.allClassesRide=true;
    ResetVehicle();
    const std::uint32_t stock[]={9,15,143,0};
    for(unsigned i=0;i<4;++i){nextMask=stock[i];SeatBuild(vehicle,nullptr);}
    Check(builds==4 && At<std::uint64_t>(vehicle,edf::kSeatCount)==4,"the stock build ran once per entry");
    Check(MaskOf(0)==15 && MaskOf(1)==15 && MaskOf(2)==143 && MaskOf(3)==0,"each appended seat widened, the closed one kept");
    Check(At<std::uint32_t>(Seat(0),0x34)==0xFFFFFFFFu,"seat+0x34 untouched");
    for(unsigned i=0;i<3;++i)for(std::uint32_t c:kClasses)
        Check(StockTest(c,Seat(i)) && PluginTest(c,Seat(i)),"stock and plugin tests admit every class in an open seat");
    for(std::uint32_t c:kClasses)Check(!StockTest(c,Seat(3)) && !PluginTest(c,Seat(3)),"...and none in the closed one");

    // An entry of another shape builds no seat: the seat before it is not touched again.
    Put<std::uint32_t>(Seat(0),0x30,9);   // as if built while the option was off
    buildsSeat=false;SeatBuild(vehicle,nullptr);
    Check(At<std::uint64_t>(vehicle,edf::kSeatCount)==4 && MaskOf(0)==9 && MaskOf(3)==0,"no seat built: nothing widened");

    // Option off, plugin off: the stock mask.
    testConfig.allClassesRide=false;
    ResetVehicle();nextMask=9;SeatBuild(vehicle,nullptr);
    Check(MaskOf(0)==9 && !StockTest(2,Seat(0)) && !StockTest(4,Seat(0)),"AllClassesRide=0: the stock 9");
    testConfig.allClassesRide=true;testConfig.enabled=false;
    ResetVehicle();nextMask=9;SeatBuild(vehicle,nullptr);
    Check(MaskOf(0)==9,"the plugin off: the stock 9");
    testConfig.enabled=true;

    // The Proteus rework's closing over a widened seat (proteus.cpp Seats / OpenSeats).
    ResetVehicle();nextMask=15;SeatBuild(vehicle,nullptr);nextMask=9;SeatBuild(vehicle,nullptr);
    const std::int32_t saved=At<std::int32_t>(Seat(1),0x30);
    Put<std::int32_t>(Seat(1),0x30,0);
    for(std::uint32_t c:kClasses)Check(!StockTest(c,Seat(1)) && !PluginTest(c,Seat(1)),"closed by the Proteus rework: no class");
    Put<std::int32_t>(Seat(1),0x30,saved);
    for(std::uint32_t c:kClasses)Check(StockTest(c,Seat(1)),"opened again: every class (the widened mask put back)");

    // RideState: the seat's state, else the bench pose.
    casStates={L"vehicle_striker_rear",L"stand"};   // a Wing Diver's / Fencer's CAS: no bike state
    played.clear();
    Check(RideState(nullptr,0,L"vehicle_striker_rear",0) && played.size()==1,"a state the CAS has: played as asked, once");
    played.clear();
    Check(RideState(nullptr,0,L"vehicle503_bike_driver",0) && played.size()==2 && played[1]==seatclass::kFallbackRide,
          "a state the CAS lacks: the bench pose next");
    played.clear();
    Check(!RideState(nullptr,0,nullptr,0) && played.size()==1,"a null name is not retried");
    testConfig.allClassesRide=false;played.clear();
    Check(!RideState(nullptr,0,L"vehicle503_bike_driver",0) && played.size()==1,"AllClassesRide=0: refused as the stock refuses");
    testConfig.allClassesRide=true;
    casStates={L"stand"};played.clear();
    Check(!RideState(nullptr,0,L"vehicle503_bike_driver",0) && played.size()==2,"no bench pose either: false, as the stock");
    casStates={L"vehicle503_bike_driver",L"vehicle_striker_rear"};played.clear();   // a Ranger's CAS
    Check(RideState(nullptr,0,L"vehicle503_bike_driver",0) && played.size()==1,"the Ranger keeps the bike pose");

    std::printf("allclass: %d checks passed\n",checks);
    return 0;
}
