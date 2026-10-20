// Execute the production control/publication path on real local-seat memory.
// GPU rendering and native ABI have separate WARP/read-only-binary tests.
#include "../src/thermal.cpp"
#include <cstdio>

namespace {
ULONGLONG testFrame=1;
bool testPaused=false,testMap=false,testOptic=true,testTrait=true,testOnFoot=false,testKey=false,testFp=false;
unsigned char* testHuman=nullptr;
unsigned char* testLocal[2]{};int testLocals=0;   // split screen: the local cameras' players (firstperson.cpp LocalPlayers)
const void* testViewOwner=nullptr;
HWND testWindow=nullptr;
SHORT WINAPI Key(int key) {return key==0x4E && testKey ? static_cast<SHORT>(0x8000) : 0;}
HWND WINAPI Window() {return testWindow;}
}
namespace crew {
unsigned char* image=nullptr;
ULONGLONG GameFrame() noexcept {return testFrame;}
bool GamePaused() noexcept {return testPaused;}
bool MapHoldsKeys() noexcept {return testMap;}
bool MapOwnsView() noexcept {return testMap;}
unsigned char* PlayerHuman() noexcept {return testHuman;}
Config config{};
const Config& Cfg() noexcept {return config;}
int LocalPlayers(unsigned char** out,int most) noexcept {
    int n=0;for(int i=0;i<testLocals && n<most;++i)out[n++]=testLocal[i];
    return n;
}
const void* LocalViewPlayer(const void*) noexcept {return testViewOwner;}
bool HumanOnFoot(const unsigned char*) noexcept {return testOnFoot;}
bool KnownVehicle(const void*) noexcept {return true;}
bool GunshipCrewSeats(const void*) noexcept {return false;}
bool SightZoomMounted(const void*) noexcept {return testOptic;}
bool VehicleHasThermal(const unsigned char*,unsigned) noexcept {return testTrait;}
bool FirstPersonVehicleView(const void*,unsigned) noexcept {return testFp;}
bool VisitEnemies(const unsigned char*,EnemyVisitor,void*) noexcept {return true;}
// Deliberately false: this function means online session, which must not gate offline thermal seats.
bool InSession() noexcept {return false;}
void Log(const char*,...) noexcept {}
}
namespace crew::thermal {
struct Renderer::Impl {};
Renderer::Renderer() noexcept=default;
Renderer::~Renderer() noexcept=default;
bool Renderer::BeginView(ID3D11DeviceContext*,std::uint64_t) noexcept {return true;}
bool Renderer::ObserveSurface(ID3D11DeviceContext*) noexcept {return true;}
bool Renderer::CaptureIndexed(ID3D11DeviceContext*,unsigned,unsigned,int) noexcept {return true;}
bool Renderer::Composite(ID3D11DeviceContext*,Polarity) noexcept {return true;}
void Renderer::Reset() noexcept {}
std::size_t EnemyModels(const void*,const void**,std::size_t) noexcept {return 0;}
}
int main() {
    using namespace crew;
    using namespace crew::thermal;
    int checks=0,failures=0;
    const auto check=[&](bool ok,const char* what){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",what);}};
    // A message-only test window belongs to this process; injected input never uses the user's desktop keys.
    testWindow=CreateWindowExW(0,L"STATIC",L"thermal input fixture",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!testWindow)return 2;
    edf::SetInputSource(&Key,&Window);
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!image)return 2;
    unsigned char human[0x1800]{},vehicle[0x3000]{},seats[2*kSeatStride]{},control[16]{},riderControl[16]{};
    testHuman=human;Put<void*>(human,kHumanVehicleCtrl,control);Put<void*>(human,kHumanVehicleCtrl-8,vehicle);
    Put<void*>(human,edf::kHumanPad,human);human[edf::kHumanPlayer]=1;
    Put<void*>(vehicle,kSelfCtrl,control);Put<int>(control,8,1);Put<int>(riderControl,8,1);
    Put<void*>(vehicle,kSeats,seats);Put<std::uint64_t>(vehicle,kSeatCount,2);
    Put<void*>(seats,kSeatRider,human);Put<void*>(seats,kSeatRiderCtrl,riderControl);
    installed.store(true);Reset();
    const auto step=[&](bool key){testKey=key;++testFrame;Tick(true,0x4E);};
    step(false);check(!InSession() && GetStatus().eligible,"offline local thermal seat remains eligible");
    check(GetStatus().requested==Mode::optical,"new seat starts optical");
    step(true);check(GetStatus().requested==Mode::whiteHot,"actual configured key enters white-hot offline");
    step(true);check(GetStatus().requested==Mode::whiteHot,"held key is one edge");
    step(false);step(true);check(GetStatus().requested==Mode::blackHot,"second key edge enters black-hot");
    step(false);step(true);check(GetStatus().requested==Mode::optical,"third key edge returns optical");
    testMap=true;step(false);step(true);
    check(GetStatus().requested==Mode::optical && GetStatus().reason==Reason::notInSight,"map input cannot cycle mode");
    check(GetStatus().eligible,"physical equipment remains eligible while the map owns its view");
    testMap=false;step(true);check(GetStatus().requested==Mode::optical,"holding N while closing map cannot trigger a new edge");
    testPaused=true;step(false);step(true);check(GetStatus().requested==Mode::optical,"pause prevents mode key admission");
    testPaused=false;step(false);step(true);check(GetStatus().requested==Mode::whiteHot,"key works again after pause");
    testOptic=false;step(false);
    check(GetStatus().effective==Mode::optical && GetStatus().reason==Reason::notInSight,"leaving sight falls back to optical");
    check(SetMode(Mode::blackHot) && GetStatus().requested==Mode::blackHot && GetStatus().effective==Mode::optical &&
          GetStatus().reason==Reason::notInSight,"a request outside the sight is kept, drawn optical with the reason");
    step(true);check(GetStatus().requested==Mode::optical,"the key cycles the equipped seat's mode outside the sight view");
    step(false);step(true);check(GetStatus().requested==Mode::whiteHot && GetStatus().effective==Mode::optical &&
          GetStatus().reason==Reason::notInSight,"a mode chosen outside the sight answers at once and waits for the sight");
    testFp=true;step(false);
    check(GetStatus().reason==Reason::waitingFrame,"the seat's own first-person view counts as looking through its sight");
    testFp=false;step(false);
    testOptic=true;step(false);check(SetMode(Mode::blackHot) && GetStatus().reason==Reason::waitingFrame,"UI accepts supported active sight");
    Put<void*>(seats,kSeatRider,nullptr);Put<void*>(seats+kSeatStride,kSeatRider,human);Put<void*>(seats+kSeatStride,kSeatRiderCtrl,riderControl);
    step(false);check(GetStatus().requested==Mode::optical,"different seat cannot inherit previous sensor mode");
    testTrait=false;step(false);check(!GetStatus().eligible && GetStatus().reason==Reason::unsupportedSeat,"unsupported equipment is distinguished from no vehicle");
    check(!GetStatus().rejected,"no press on the unsupported seat: nothing to answer");
    step(true);check(GetStatus().rejected && GetStatus().requested==Mode::optical,"a press on an unsupported seat is answered, never a request");
    step(false);
    testTrait=true;testOnFoot=true;step(false);
    check(!GetStatus().eligible && GetStatus().reason==Reason::optical,"on-foot state retires sensor without unsupported-seat warning");
    check(!SetMode(Mode::whiteHot) && SetMode(Mode::optical),"only optical is admissible on foot");
    testOnFoot=false;step(false);installed.store(false);
    check(SetMode(Mode::whiteHot) && GetStatus().effective==Mode::optical && GetStatus().reason==Reason::hooksUnavailable,
          "missing native hooks keep optical and say so instead of dropping the press");
    installed.store(true);check(SetMode(Mode::whiteHot),"restore fixture hook readiness");
    Reset();check(GetStatus().requested==Mode::optical && GetStatus().models==0 && !GetStatus().eligible && !GetStatus().rejected,"mission reset clears all published identities and requests");

    // A pad seat (seat +0x2B0 = 1): the seat-button chord (ini VehicleThermalButton, default L3 + R3) toggles it; the
    // keyboard key does not (it is another player's keyboard).
    unsigned char* const padSeat=seats+kSeatStride;
    padSeat[edf::kSeatPad]=1;config.thermalButton=0xC0;
    const auto pad=[&](std::uint16_t buttons){Put<std::uint16_t>(padSeat,edf::kSeatButtons,buttons);testKey=false;++testFrame;Tick(true,0x4E);};
    pad(0);check(GetStatus().eligible && GetStatus().pad && GetStatus().requested==Mode::optical,"a pad seat is eligible and says so");
    pad(0x40);check(GetStatus().requested==Mode::optical,"L3 alone is no thermal press");
    pad(0xC0);check(GetStatus().requested==Mode::whiteHot,"L3 held, then R3: one press, white hot");
    pad(0xC0);check(GetStatus().requested==Mode::whiteHot,"the chord held is one edge");
    pad(0x80);pad(0xC0);check(GetStatus().requested==Mode::blackHot,"R3 again while L3 is held: the next mode");
    testKey=true;++testFrame;Tick(true,0x4E);
    check(GetStatus().requested==Mode::blackHot,"the keyboard key does not toggle a pad seat");
    testMap=true;pad(0);pad(0xC0);check(GetStatus().requested==Mode::blackHot,"the chord gives way to the map");
    testMap=false;pad(0);testPaused=true;pad(0xC0);check(GetStatus().requested==Mode::blackHot,"the chord gives way to the pause");
    testPaused=false;pad(0);
    config.thermalButton=0;pad(0xC0);check(GetStatus().requested==Mode::blackHot,"VehicleThermalButton=0: no pad toggle");
    config.thermalButton=0xC0;padSeat[edf::kSeatPad]=0;Put<std::uint16_t>(padSeat,edf::kSeatButtons,0);

    // Split screen: two local players, each their own seat, mode and published image; each view draws its own.
    Reset();
    unsigned char human2[0x1800]{},vehicle2[0x3000]{},seats2[kSeatStride]{},control2[16]{},riderControl2[16]{};
    Put<void*>(human2,kHumanVehicleCtrl,control2);Put<void*>(human2,kHumanVehicleCtrl-8,vehicle2);
    Put<void*>(human2,edf::kHumanPad,human2);human2[edf::kHumanPlayer]=1;
    Put<void*>(vehicle2,kSelfCtrl,control2);Put<int>(control2,8,1);Put<int>(riderControl2,8,1);
    Put<void*>(vehicle2,kSeats,seats2);Put<std::uint64_t>(vehicle2,kSeatCount,1);
    Put<void*>(seats2,kSeatRider,human2);Put<void*>(seats2,kSeatRiderCtrl,riderControl2);
    seats2[edf::kSeatPad]=1;
    testLocal[0]=human;testLocal[1]=human2;testLocals=2;
    const auto both=[&](bool key,std::uint16_t buttons){
        testKey=key;Put<std::uint16_t>(seats2,edf::kSeatButtons,buttons);++testFrame;Tick(true,0x4E);
    };
    both(false,0);
    check(GetStatus(human).eligible && GetStatus(human2).eligible && !GetStatus(human).pad && GetStatus(human2).pad,
          "both local players have their own equipped seat");
    both(true,0);check(GetStatus(human).requested==Mode::whiteHot && GetStatus(human2).requested==Mode::optical,
                       "the first player's key is the first player's");
    both(false,0xC0);check(GetStatus(human).requested==Mode::whiteHot && GetStatus(human2).requested==Mode::whiteHot,
                           "the second player's chord is the second player's");
    both(false,0);both(false,0xC0);check(GetStatus(human2).requested==Mode::blackHot && GetStatus(human).requested==Mode::whiteHot,
                                         "each player cycles alone");
    // The render: a view goes to the player whose camera submitted it (LocalViewPlayer), each with its own mode.
    alignas(16) unsigned char view1[0x100]{},view2[0x100]{};
    Put<unsigned>(view2,0,1);   // not the first camera slot's block
    testViewOwner=human2;
    check(ViewPlayer(view2)==Find(human2) && Find(human2)->requested.load()==Mode::blackHot,"the second viewport is the second player's");
    testViewOwner=human;check(ViewPlayer(view1)==Find(human),"the first viewport is the first player's");
    testViewOwner=nullptr;
    check(ViewPlayer(view1)==Find(human) && ViewPlayer(view2)==nullptr,
          "no camera match: only the first camera slot's view, as the first player's (no other player's image borrowed)");
    testLocals=1;
    for(int i=0;i<200;++i){++testFrame;Tick(true,0x4E);}
    check(GetStatus(human).eligible,"the first player stays");
    testLocals=0;testLocal[0]=testLocal[1]=nullptr;Reset();
    edf::SetInputSource(nullptr,nullptr);DestroyWindow(testWindow);VirtualFree(image,0,MEM_RELEASE);
    std::printf("thermal runtime: %d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
