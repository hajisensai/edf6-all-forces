// Exercise the production command frame and snapshot publication, with stand-in units and keyboard state.
// Link edf6common and user32. No game process or installed game files are used.
#include <Windows.h>
namespace inputstub {
bool keys[256]{};
SHORT Down(int key) noexcept { return key>=0 && key<256 && keys[key] ? static_cast<SHORT>(0x8000) : 0; }
}
#define GetAsyncKeyState inputstub::Down
#include "../src/mapcmd.cpp"
#undef GetAsyncKeyState
#include "../src/map_stock_hud.h"
#include <cstdio>

namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
bool InSession() noexcept { return false; }
float MapRay(const float*,const float*,float*) noexcept { return -1.0f; }
float MapFloorRay(const float*,const float*,float*) noexcept { return -1.0f; }
bool MapGroundNear(float,float,float,float*,bool) noexcept { return false; }
int HeliCommandUnits(CommandUnit*,int) noexcept { return 0; }
int JetCommandUnits(CommandUnit*,int) noexcept { return 0; }
int GroundCommandUnits(CommandUnit*,int) noexcept { return 0; }
bool HeliCommand(const void*,const Command&) noexcept { return false; }
bool JetCommand(const void*,const Command&) noexcept { return false; }
bool GroundCommand(const void*,const Command&) noexcept { return false; }
bool HeliSharesPost() noexcept { return true; }
int SquadCommandUnits(CommandUnit*,int) noexcept {return 0;}
int TankCommandUnits(CommandUnit*,int) noexcept {return 0;}
int SquadRows(SquadRow*,int) noexcept {return 0;}
bool SquadCommand(const void*,const Command&) noexcept {return false;}
bool TankCommand(const void*,const Command&) noexcept {return false;}
bool NpcMarked() noexcept {return false;}
// The formations (npcai.cpp): a squad at `guardLeader` guards, the others follow; the calls counted.
const void* guardLeader=nullptr;
int guardCalls=0,marchCalls=0;
int CycleGuardFormation(const void* leader) noexcept { ++guardCalls;return leader==guardLeader ? 10 : -1; }
int CycleMarchFormation() noexcept { ++marchCalls;return 3; }
int SplitSquad(const void*) noexcept {return -1;}
bool MergeSquads(const void*,const void*) noexcept {return false;}
int sweepCalls=0,healthCalls=0;bool sweepState=false,healthState=false;
bool NpcSweepToggle(const void* const*,int) noexcept { ++sweepCalls;sweepState=!sweepState;return sweepState; }
bool NpcSweepOn() noexcept { return sweepState; }
bool NpcPickupHealthToggle() noexcept { ++healthCalls;healthState=!healthState;return healthState; }
bool NpcPickupHealthOn() noexcept { return healthState; }
int NpcMarchShape() noexcept { return 0; }
const wchar_t* FormationText(int) noexcept { return L"SHAPE"; }
namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what) noexcept {
    ++cases;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
// A stale address must not be read in Publish. Catch faults here just as the map's outer frame hook does, then check
// the actual production lock with TryAcquire, so a regression fails instead of hanging the test process.
bool PublishGuarded(const Game& state) noexcept {
    const float point[3]{};
    __try { Publish(state,true,true,point,true);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void UnitLifetime() noexcept {
    auto* vehicle=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!vehicle){Check(false,"allocate stand-in vehicle");return;}
    unsigned char seat[edf::kSeatStride]{},rider[0x400]{},ctrl[0x10]{};
    Put<void*>(vehicle,kSelfCtrl,ctrl);Put<void*>(vehicle,kSeats,seat);Put<std::uint64_t>(vehicle,kSeatCount,1);
    Put<void*>(seat,kSeatRider,rider);Put<void*>(seat,kSeatRiderCtrl,ctrl);Put<int>(ctrl,edf::kCtrlUses,1);
    Put<const void*>(rider,0,image+edf::kDummyRiderVtable);
    const float pos[3]={12.0f,34.0f,56.0f};std::memcpy(vehicle+kPosition,pos,12);
    const ObjRef ref=ObjRef::Of(vehicle);
    CommandUnit unit{};
    Check(ReadCommandUnit(ref,"test",Command{},false,&unit),"live NPC unit sampled with production seat checks");
    vehicle[kDead]=1;
    Check(!ReadCommandUnit(ref,"test",Command{},false,&unit),"dead unit rejected inside freshness window");
    vehicle[kDead]=0;Put<void*>(vehicle,kSelfCtrl,vehicle);
    Check(!ReadCommandUnit(ref,"test",Command{},false,&unit),"reused address with another identity rejected");
    Put<void*>(vehicle,kSelfCtrl,ctrl);Put<void*>(rider,0,nullptr);
    Put<unsigned char>(rider,edf::kHumanPlayer,1);Put<void*>(rider,edf::kHumanPad,rider);
    Check(!ReadCommandUnit(ref,"test",Command{},false,&unit),"player takeover rejected before next NPC input");
    Put<const void*>(rider,0,image+edf::kDummyRiderVtable);
    Check(ReadCommandUnit(ref,"test",Command{},false,&unit),"NPC unit sampled again");
    DWORD old=0;
    const bool protectedPage=VirtualProtect(vehicle,4096,PAGE_NOACCESS,&old)!=FALSE;
    Check(protectedPage,"make formerly live unit inaccessible");
    if(protectedPage) {
        CommandUnit gone{};
        Check(!ReadCommandUnit(ref,"test",Command{},false,&gone),"unmapped or newly protected unit rejected even with cached Readable");
        Game snap{};snap.count=1;snap.list[0]=Entry{unit,Owner::ground};
        Check(PublishGuarded(snap),"publication uses sampled coordinates after object becomes inaccessible");
        const bool unlocked=TryAcquireSRWLockShared(&lock)!=FALSE;
        Check(unlocked,"publication leaves HUD lock available");
        if(unlocked) {
            ReleaseSRWLockShared(&lock);
            MapCommandReadout r{};
            Check(PlayerMapCommands(&r) && r.count==1 && r.unit[0].pos[0]==12.0f && r.unit[0].pos[2]==56.0f,
                  "published snapshot readable and preserves sampled position");
        }
        VirtualProtect(vehicle,4096,old,&old);
    }
    VirtualFree(vehicle,0,MEM_RELEASE);
}
void PointerAndInput() noexcept {
    ResetMapCommands();view=View{};
    MapCmdInput in{};in.front=true;in.usingPad=true;in.eye[1]=100.0f;
    float centre[3]{};
    MapCommandFrame(in,centre);
    Check(!game.pointer.placed,"opening without a rendered viewport defers pointer placement");
    const float vp[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    MapCommandView(vp,1280,720);
    MapCommandFrame(in,centre);
    Check(game.pointer.placed && game.pointer.x==640.0f && game.pointer.y==360.0f,"first 720p view centres the pointer");
    in.mouse=true;in.dx=18.0f;
    MapCommandFrame(in,centre);
    MapCommandReadout r{};
    Check(!in.usingPad && PlayerMapCommands(&r) && r.pointer && r.px==652.0f,"mouse motion after gamepad open reveals and moves pointer");
    in.mouse=false;in.dx=0.0f;in.pad=true;in.buttons=XINPUT_GAMEPAD_Y;
    MapCommandFrame(in,centre);
    Check(in.usingPad && PlayerMapCommands(&r) && !r.pointer,"gamepad command changes source without stick movement");
    in.buttons=0;inputstub::keys['G']=true;
    MapCommandFrame(in,centre);
    Check(!in.usingPad && PlayerMapCommands(&r) && r.pointer,"keyboard command restores pointer input without map movement");
    inputstub::keys['G']=false;
    MapCommandView(vp,3840,2160);MapCommandFrame(in,centre);
    Check(game.pointer.x==1956.0f && game.pointer.y==1080.0f,"viewport resize preserves normalized pointer position");
    game.frameAt=GetTickCount64()-kFreshMs-1;view.at=0;
    MapCommandFrame(in,centre);
    Check(!game.pointer.placed,"reopened map awaits its new view");
    MapCommandView(vp,800,600);MapCommandFrame(in,centre);
    Check(game.pointer.x==400.0f && game.pointer.y==300.0f,"reopened small viewport starts inside at centre");
}
// T on the map: each selected squad that guards cycles its own defence, the march once however many follow; online
// (orders refused) nothing changes.
void FormationKey() noexcept {
    static int a=0,b=0,c=0,d=0;
    Game g{};
    g.count=4;
    const void* who[4]={&a,&b,&c,&d};
    for(int i=0;i<4;++i){g.list[i]=Entry{CommandUnit{who[i],"squad",Command{}},Owner::squad};}
    g.sel.Add(&a);g.sel.Add(&b);g.sel.Add(&c);   // d not selected
    guardLeader=&a;guardCalls=marchCalls=0;
    Formation(g,true);
    Check(guardCalls==3 && marchCalls==1,"T: the guard squad cycled, the march once for two following squads");
    guardCalls=marchCalls=0;
    Formation(g,false);
    Check(guardCalls==0 && marchCalls==0,"T online: nothing cycled");
    g.sel.Clear();g.sel.Add(&a);guardCalls=marchCalls=0;
    Formation(g,true);
    Check(guardCalls==1 && marchCalls==0,"T on a guarding squad alone: the march untouched");
}
// The map's buttons (hud.cpp draws them and hands their rectangles over): a click on the sweep's button runs it and
// selects nothing; the guard's arms the next click on the ground (no unit picked by it), clicked again disarms it;
// Y and O do what the sweep's and the health switch's buttons do.
void ButtonClicks() noexcept {
    ResetMapCommands();view=View{};
    MapCmdInput in{};in.front=true;in.mouse=true;in.eye[1]=100.0f;
    float centre[3]{};
    const float vp[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    MapCommandView(vp,1280,720);
    MapCommandFrame(in,centre);   // the pointer placed at the centre (640, 360)
    const float rects[8]={600,340,680,380, 700,340,780,380};
    const int ids[2]={static_cast<int>(mapbtn::Id::sweep),static_cast<int>(mapbtn::Id::guard)};
    MapCommandButtons(rects,ids,2);
    sweepCalls=healthCalls=0;sweepState=healthState=false;
    const auto click=[&]{inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);inputstub::keys[VK_LBUTTON]=false;MapCommandFrame(in,centre);};
    click();
    Check(sweepCalls==1 && sweepState && game.sel.n==0,"a click on the sweep's button runs it, nothing selected by it");
    // The pointer onto the guard's button (100 px right): armed; again: disarmed.
    in.dx=100.0f/kPointerGain/(720.0f/1080.0f);MapCommandFrame(in,centre);in.dx=0.0f;
    click();
    Check(game.guardArmed,"the guard's button arms the next click on the ground");
    click();
    Check(!game.guardArmed,"clicked again: disarmed");
    click();
    in.dy=150.0f/kPointerGain/(720.0f/1080.0f);MapCommandFrame(in,centre);in.dy=0.0f;   // off the buttons, on the ground
    click();
    Check(!game.guardArmed && game.sel.n==0,"armed, a click on the ground is the guard's point (taken, disarmed), no unit picked");
    inputstub::keys['Y']=true;MapCommandFrame(in,centre);inputstub::keys['Y']=false;MapCommandFrame(in,centre);
    Check(sweepCalls==2 && !sweepState,"Y as the sweep's button: called back");
    inputstub::keys['O']=true;MapCommandFrame(in,centre);inputstub::keys['O']=false;MapCommandFrame(in,centre);
    Check(healthCalls==1 && healthState,"O flips the health-box switch");
    MapCommandReadout r{};
    Check(PlayerMapCommands(&r) && r.healthOn && !r.sweepOn,"the readout carries the switches for the buttons' labels");
}
void CameraIsolation() noexcept {
    maphud::Record r{};int first=0,second=0;unsigned char shown=1;
    maphud::Step(r,&first,1,true,&shown);
    Check(maphud::Hides(r,&first,1),"owning camera follower gauge hidden");
    Check(!maphud::Hides(r,&second,1),"other split-screen camera follower gauge preserved");
    Check(!maphud::Hides(r,&first,2),"old mission hold cannot hide new mission gauge");
    maphud::Step(r,&first,1,false,&shown);
    Check(!maphud::Hides(r,&first,1),"closing map restores owning camera follower gauge");
}
}  // namespace
}  // namespace crew
int main() {
    crew::UnitLifetime();crew::PointerAndInput();crew::CameraIsolation();crew::FormationKey();crew::ButtonClicks();
    std::printf("map_command_runtime_check: %d checks, %d failed\n",crew::cases,crew::failures);
    return crew::failures ? 1 : 0;
}
