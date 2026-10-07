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
// One stand-in squad (when `squadOn`), its last order; the enemies the lock registry would list; the mark npcai.cpp keeps.
unsigned char squadObj[0x100]{},squadCtrl[0x10]{};
bool squadOn=false;
Command squadGot{};int squadOrders=0;
int SquadCommandUnits(CommandUnit* out,int most) noexcept {
    if(!squadOn || most<1)return 0;
    out[0]=CommandUnit{squadObj,"squad",Command{},false,{0.0f,0.0f,0.0f}};
    return 1;
}
int TankCommandUnits(CommandUnit*,int) noexcept {return 0;}
int SquadRows(SquadRow*,int) noexcept {return 0;}
bool SquadCommand(const void* leader,const Command& c) noexcept {
    if(leader!=squadObj)return false;
    squadGot=c;++squadOrders;
    return true;
}
bool TankCommand(const void*,const Command&) noexcept {return false;}
struct StubEnemy { const void* object; float aim[3]; };
StubEnemy stubEnemies[2]{};int stubEnemyCount=0;
bool VisitEnemiesOf(std::int32_t,EnemyVisitor visit,void* ctx) noexcept {
    for(int i=0;i<stubEnemyCount;++i)visit(ctx,stubEnemies[i].object,stubEnemies[i].aim);
    return true;
}
const void* marked=nullptr;int markCalls=0;
bool NpcMarked() noexcept {return marked!=nullptr;}
bool NpcMarkEnemy(const void* object,const float*,bool toggle) noexcept {
    ++markCalls;
    if(toggle && marked==object){marked=nullptr;return false;}
    marked=object;
    return true;
}
PlayerFix player{};
const Config& Cfg() noexcept { static const Config c{};return c; }   // NpcMarkKey 'Q'
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
// The mark key and H with the pointer on an enemy (the user, 2026-10-07: "应该在地图里面也能按q标记"); the mark key on
// foot with no enemy near the centre sends the selection there (MapCommandGuardAt).
void MarkFromMap() noexcept {
    ResetMapCommands();view=View{};marked=nullptr;markCalls=0;squadOn=false;squadOrders=0;
    const float vp[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};   // world (x, y) in [-1, 1] across the screen
    const char foe=0;
    stubEnemies[0]=StubEnemy{&foe,{0.0f,0.0f,0.5f}};stubEnemyCount=1;   // under the centred pointer
    MapCmdInput in{};in.front=true;in.eye[1]=100.0f;
    float centre[3]{};
    MapCommandView(vp,1280,720);
    MapCommandFrame(in,centre);   // opened: the pointer at the centre, on the enemy
    MapCommandReadout r{};
    Check(PlayerMapCommands(&r) && r.hover,"an enemy under the pointer is shown as such");
    inputstub::keys['Q']=true;
    Check(MapCommandEats(true),"Q on an enemy is not the map's turn");
    MapCommandFrame(in,centre);
    Check(marked==&foe && markCalls==1,"Q on an enemy marks it");
    Check(MapCommandEats(true),"the press held: still not the map's");
    MapCommandFrame(in,centre);
    Check(markCalls==1,"Q held: marked once");
    inputstub::keys['Q']=false;
    Check(!MapCommandEats(true),"Q let go");
    MapCommandFrame(in,centre);
    inputstub::keys['Q']=true;MapCommandEats(true);MapCommandFrame(in,centre);
    Check(marked==nullptr && markCalls==2,"Q again on the marked enemy lets it go");
    inputstub::keys['Q']=false;MapCommandEats(true);MapCommandFrame(in,centre);
    // The pointer off the enemy: Q turns the map, marks nothing.
    in.mouse=true;in.dx=300.0f;MapCommandFrame(in,centre);in.mouse=false;in.dx=0.0f;
    Check(PlayerMapCommands(&r) && !r.hover,"the pointer off the enemy: no enemy under it");
    inputstub::keys['Q']=true;
    Check(!MapCommandEats(true),"Q off an enemy turns the map");
    MapCommandFrame(in,centre);
    Check(markCalls==2,"Q off an enemy marks nothing");
    inputstub::keys['Q']=false;MapCommandEats(true);MapCommandFrame(in,centre);
    // H with no mark and nothing under the pointer: refused, nothing marked.
    Put<void*>(squadObj,kSelfCtrl,squadCtrl);squadOn=true;
    game.sel.Clear();game.sel.Add(squadObj);game.selected[0]=ObjRef::Of(squadObj);
    inputstub::keys['H']=true;MapCommandFrame(in,centre);inputstub::keys['H']=false;MapCommandFrame(in,centre);
    Check(squadOrders==0 && marked==nullptr,"H with no mark and no enemy under the pointer: refused");
    // H on the enemy: marked and the squad focuses on it in one press.
    in.mouse=true;in.dx=-300.0f;MapCommandFrame(in,centre);in.mouse=false;in.dx=0.0f;
    inputstub::keys['H']=true;MapCommandFrame(in,centre);inputstub::keys['H']=false;MapCommandFrame(in,centre);
    Check(marked==&foe && squadOrders==1 && squadGot.order==Order::focus,"H on an enemy marks it and the squad focuses on it");
    // On foot: the point goes to the selection kept from the map.
    const float at[3]={50.0f,2.0f,-80.0f};
    Check(MapCommandGuardAt(at)==1 && squadOrders==2 && squadGot.order==Order::guard && squadGot.at[0]==50.0f &&
          squadGot.at[2]==-80.0f,"the mark key's point: the selected squad guards it");
    game.sel.Clear();
    Check(MapCommandGuardAt(at)==-1 && squadOrders==2,"the mark key's point with nothing selected: no order");
    squadOn=false;stubEnemyCount=0;
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
    crew::UnitLifetime();crew::PointerAndInput();crew::MarkFromMap();crew::CameraIsolation();
    std::printf("map_command_runtime_check: %d checks, %d failed\n",crew::cases,crew::failures);
    return crew::failures ? 1 : 0;
}
