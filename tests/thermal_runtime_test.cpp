// Execute the production control/publication path on real local-seat memory.
// GPU rendering and native ABI have separate WARP/read-only-binary tests.
#include "../src/thermal.cpp"
#include "../src/native_pad.cpp"   // the game's pad read the night vision's chord is kept from (its ReadHook, real code)
#include <cstdio>
#include "../src/netprobe.h"

namespace {
ULONGLONG testFrame=1;
bool testPaused=false,testMap=false,testOptic=true,testTrait=true,testOnFoot=false,testKey=false,testFp=false;
unsigned char* testHuman=nullptr;
unsigned char* testLocal[2]{};int testLocals=0;   // split screen: the local cameras' players (firstperson.cpp LocalPlayers)
const void* testViewOwner=nullptr;
const void* testLensOwner=nullptr;            // the scope lens's view's soldier (infantryscope.cpp InfantryScopeLensOwner)
bool testPadPresent=false;std::uint16_t testPadButtons=0;   // the first XInput pad (map.cpp MapPadButtons)
HWND testWindow=nullptr;
SHORT WINAPI Key(int key) {return key==0x4E && testKey ? static_cast<SHORT>(0x8000) : 0;}
HWND WINAPI Window() {return testWindow;}
std::uint16_t gamePadButtons=0;   // what the game's own XInputGetState (0x11883D1's slot) reads from the pad
DWORD WINAPI GameGetState(DWORD,XINPUT_STATE* s) {*s={};s->Gamepad.wButtons=gamePadButtons;return ERROR_SUCCESS;}
// The buttons the game is handed for `user` when the pad holds `buttons` (native_pad.cpp ReadHook).
std::uint16_t GameSees(DWORD user,std::uint16_t buttons) {
    gamePadButtons=buttons;XINPUT_STATE s{};crew::nativepad::ReadHook(user,&s);return s.Gamepad.wButtons;
}
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
// The soldier's scope lens at the world's end (infantryscope.cpp): its own test is scope_crosshair_test.
void InfantryScopeWorldEnd(ID3D11DeviceContext*,const void*) noexcept {}
const unsigned char* InfantryScopeLensOwner(const void*) noexcept {return static_cast<const unsigned char*>(testLensOwner);}
bool MapPadButtons(std::uint16_t* buttons) noexcept {*buttons=testPadButtons;return testPadPresent;}
// GPU particles at the world's begin / end (src/gpufx): their own tests are gpufx_sim_test / gpufx_gpu_test.
namespace gpufx {
void WorldBegin(ID3D11DeviceContext*,const void*) noexcept {}
void WorldEnd(ID3D11DeviceContext*,const void*) noexcept {}
}
}
namespace crew::thermal {
struct Renderer::Impl {};
Renderer::Renderer() noexcept=default;
Renderer::~Renderer() noexcept=default;
bool Renderer::BeginView(ID3D11DeviceContext*,std::uint64_t) noexcept {return true;}
bool Renderer::ObserveSurface(ID3D11DeviceContext*) noexcept {return true;}
bool Renderer::CaptureIndexed(ID3D11DeviceContext*,unsigned,unsigned,int) noexcept {return true;}
bool Renderer::Composite(ID3D11DeviceContext*,Polarity) noexcept {return true;}
bool Renderer::NightVision(ID3D11DeviceContext*,const NightLook&) noexcept {return true;}
bool Renderer::Ready(ID3D11DeviceContext*) noexcept {return true;}
void Renderer::Invalidate() noexcept {}
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

    // Night vision on foot (every class): the same key switches it off / on; drawn while on foot and the player's view is up.
    Put<void*>(seats,kSeatRider,nullptr);Put<void*>(seats+kSeatStride,kSeatRider,nullptr);
    Put<int>(human,enemy_control::kHumanLocalPad,0);   // this machine's first local player: the keyboard's and pad 0's
    testOnFoot=true;testTrait=true;config.nightVision=true;config.nightVisionButton=0xC0;config.nightVisionGain=6.0f;
    step(false);check(GetStatus().nightEligible && !GetStatus().nightVision && !GetStatus().eligible,"on foot: night vision usable, off, no thermal");
    check(!ReadSnapshot(*Find(human)).night,"nothing drawn before the press");
    step(true);check(GetStatus().nightVision && ReadSnapshot(*Find(human)).night,"N on foot switches night vision on and publishes it");
    check(GetStatus().requested==Mode::optical,"the on-foot press is no thermal request");
    step(true);check(GetStatus().nightVision,"held N is one edge");
    step(false);step(true);check(!GetStatus().nightVision && !ReadSnapshot(*Find(human)).night,"the second press switches it off");
    step(false);step(true);check(GetStatus().nightVision,"and on again");
    testMap=true;step(false);check(GetStatus().nightVision && !ReadSnapshot(*Find(human)).night,"the map's view is not tinted, the choice kept");
    step(true);check(GetStatus().nightVision,"the map holds the key: no switch");
    testMap=false;step(true);step(false);
    testPaused=true;step(true);check(GetStatus().nightVision,"the pause holds the key");
    testPaused=false;step(false);
    // Pad 0's chord (XInput bits: 0xC0 L3 + R3).
    testPadPresent=true;
    const auto padFoot=[&](std::uint16_t buttons){testPadButtons=buttons;testKey=false;++testFrame;Tick(true,0x4E);};
    padFoot(0x40);check(GetStatus().nightVision,"L3 alone is no night vision press");
    padFoot(0xC0);check(!GetStatus().nightVision,"L3 + R3 switches it off");
    padFoot(0xC0);check(!GetStatus().nightVision,"the chord held is one edge");
    padFoot(0);padFoot(0xC0);check(GetStatus().nightVision,"the chord again: on");
    config.nightVisionButton=0;padFoot(0);padFoot(0xC0);check(GetStatus().nightVision,"InfantryNightVisionButton=0: no pad toggle");
    config.nightVisionButton=0xC0;padFoot(0);testPadPresent=false;
    // Another local pad's soldier (split screen's second player): neither the keyboard nor pad 0 is theirs.
    Put<int>(human,enemy_control::kHumanLocalPad,1);step(false);step(true);check(GetStatus().nightVision,"local pad 1: the keyboard is not theirs");
    Put<int>(human,enemy_control::kHumanLocalPad,0);step(false);
    // Into a vehicle: not drawn there, the key is the seat's thermal again; out again: night vision as it was.
    Put<void*>(seats,kSeatRider,human);testOnFoot=false;step(false);
    check(GetStatus().nightVision && !GetStatus().nightEligible && !ReadSnapshot(*Find(human)).night,"in a vehicle: night vision kept, not drawn");
    step(true);check(GetStatus().requested==Mode::whiteHot && GetStatus().nightVision,"in a vehicle N cycles thermal, night vision untouched");
    step(false);Put<void*>(seats,kSeatRider,nullptr);testOnFoot=true;step(false);
    check(GetStatus().nightVision && ReadSnapshot(*Find(human)).night && GetStatus().requested==Mode::optical,
          "out of the vehicle: night vision back, thermal retired");
    check(SetNightVision(false) && !GetStatus().nightVision && SetNightVision(true) && GetStatus().nightVision,"UI request on foot");
    // Dead: not usable, not drawn; the ini off: never on.
    human[kDead]=1;step(false);check(!GetStatus().nightEligible && !ReadSnapshot(*Find(human)).night,"a dead soldier's view is not tinted");
    check(!SetNightVision(true),"UI cannot switch it on while not usable");
    human[kDead]=0;config.nightVision=false;step(false);
    check(!GetStatus().nightVision && !GetStatus().nightEligible && !ReadSnapshot(*Find(human)).night,"InfantryNightVision=0: off");
    step(true);check(!GetStatus().nightVision,"InfantryNightVision=0: the key does nothing on foot");
    config.nightVision=true;step(false);step(true);check(GetStatus().nightVision,"enabled again");
    // The chord kept from the game's own pad read (2026-11-26 实机: L3 + R3 switched night vision and the Ranger dashed:
    // the stock soldier took the same L3 / R3 as its own presses).
    {
        padchord::Swallow s;
        check(s.Filter(0x40,0xC0)==0x40,"a chord button alone reaches the game");
        check(s.Filter(0xC0,0xC0)==0,"the whole chord in one read: neither button reaches the game");
        check(s.Filter(0x40,0xC0)==0 && s.Filter(0x40,0)==0,"one let go: the other kept until released, owned or not");
        check(s.Filter(0x1040,0)==0x1000,"the other buttons pass meanwhile");
        check(s.Filter(0,0)==0 && s.Filter(0x40,0xC0)==0x40,"released: L3 is the game's again");
        check(s.Filter(0xC0,0)==0xC0,"a chord not owned is the game's");
    }
    nativepad::gameGet=reinterpret_cast<void*>(&GameGetState);
    testPadPresent=true;
    const auto padStep=[&](std::uint16_t buttons){testPadButtons=buttons;testKey=false;++testFrame;Tick(true,0x4E);};
    padStep(0);
    check(GameSees(0,0x40)==0x40 && GameSees(0,0x80)==0x80 && GameSees(0,0)==0,"on foot: L3 / R3 alone are the game's (dash, zoom)");
    {
        const bool was=GetStatus().nightVision;
        check(GameSees(0,0xC0)==0,"on foot, the night vision's chord: kept from the game's read");
        padStep(0xC0);check(GetStatus().nightVision!=was,"...and it still switches night vision (the plugin's own pad read)");
        check(GameSees(0,0xC0)==0 && GameSees(0,0x40)==0,"held, then R3 let go: L3 still kept (no stock press of it)");
        check(GameSees(0,0x1040)==0x1000,"other buttons pass while it is kept");
        padStep(0);check(GameSees(0,0)==0 && GameSees(0,0x40)==0x40,"both released: a new L3 is the game's");
        GameSees(0,0);
    }
    check(GameSees(1,0xC0)==0xC0,"another XInput user's pad (split screen's second player) is untouched");
    Put<unsigned>(image,nativepad::kPadSwap,1);
    check(GameSees(1,0xC0)==0 && GameSees(1,0)==0 && GameSees(0,0xC0)==0xC0,"pads swapped (0x2136504 == 1): user 1 is the first player's");
    GameSees(0,0);Put<unsigned>(image,nativepad::kPadSwap,0);
    testPaused=true;check(GameSees(0,0xC0)==0xC0,"paused: the chord is the menu's (not owned)");
    GameSees(0,0);testPaused=false;
    testMap=true;padStep(0);check(GameSees(0,0xC0)==0xC0,"the map holds the keys: the chord not owned");
    GameSees(0,0);testMap=false;padStep(0);
    config.nightVision=false;padStep(0);check(GameSees(0,0xC0)==0xC0,"InfantryNightVision=0: L3 + R3 are the game's");
    GameSees(0,0);config.nightVision=true;padStep(0);
    config.nightVisionButton=0;padStep(0);check(GameSees(0,0xC0)==0xC0,"InfantryNightVisionButton=0: nothing owned");
    GameSees(0,0);config.nightVisionButton=0xC0;padStep(0);
    Put<int>(human,enemy_control::kHumanLocalPad,1);padStep(0);
    check(GameSees(0,0xC0)==0,"a split screen's second player's frame leaves the first player's claim as it was");
    GameSees(0,0);Put<int>(human,enemy_control::kHumanLocalPad,0);
    testOnFoot=false;padStep(0);check(GameSees(0,0xC0)==0xC0,"not on foot: L3 + R3 are the game's");
    GameSees(0,0);testOnFoot=true;padStep(0);
    stepfresh::See(stepfresh::Now()+stepfresh::kEveryStep+1);
    check(GameSees(0,0xC0)==0xC0,"a claim no longer renewed (the soldier's frame not run) owns nothing");
    GameSees(0,0);padStep(0);check(GameSees(0,0xC0)==0,"renewed: owned again");
    GameSees(0,0);
    Reset();check(GameSees(0,0xC0)==0xC0,"mission reset: nothing owned");
    GameSees(0,0);padStep(0);testPadPresent=false;nativepad::gameGet=nullptr;
    step(false);
    if(!GetStatus().nightVision){step(true);step(false);}
    // The render: a fresh night snapshot marks the view; its end draws night vision (stub) and reports it shown.
    alignas(16) unsigned char nightView[0x100]{};
    testViewOwner=human;
    ViewBegin(nullptr,nightView);
    check(frame.night && !frame.active,"the night view is marked, no thermal capture");
    ThermalEnd(nullptr,nightView);check(GetStatus().nightShown && !frame.night,"the world's end drew night vision");
    // The scope lens's own view (InfantryScope=2) is the soldier's too.
    testViewOwner=nullptr;Put<unsigned>(nightView,0,3);testLensOwner=human;
    check(ViewPlayer(nightView)==Find(human),"the scope lens's view is its soldier's");
    testLensOwner=nullptr;Put<unsigned>(nightView,0,0);
    Reset();check(!GetStatus().nightVision,"mission reset: night vision off");
    testOnFoot=false;
    edf::SetInputSource(nullptr,nullptr);DestroyWindow(testWindow);VirtualFree(image,0,MEM_RELEASE);
    std::printf("thermal runtime: %d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
