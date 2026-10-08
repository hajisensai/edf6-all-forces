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
int SupportCallCount() noexcept { return 3; }
const wchar_t* SupportCallName(int) noexcept { return L"Support"; }
int supportCalls=0,supportChosen=-1;float supportTarget[3]{};
bool SupportCallAt(int index,const float* target,wchar_t* note,std::size_t capacity) noexcept {
    ++supportCalls;supportChosen=index;std::memcpy(supportTarget,target,12);
    _snwprintf_s(note,capacity,_TRUNCATE,L"support received");return true;
}
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
Config config{};
const Config& Cfg() noexcept { return config; }   // NpcMarkKey 'Q'
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

// The mark key and H with the pointer on an enemy (the user, 2026-10-07: "应该在地图里面也能按q标记"); the mark key on
// foot with no enemy near the centre sends the selection there (MapCommandGuardAt).
void FocusButtonPreservesMark() noexcept {
    ResetMapCommands();view=View{};config=Config{};
    std::memset(inputstub::keys,0,sizeof(inputstub::keys));
    unsigned char foe[0x400]{},control[0x10]{};static int earlier;
    Put<void*>(foe,kSelfCtrl,control);Put<long>(control,8,1);Put<long>(control,0xC,1);
    stubEnemies[0]=StubEnemy{foe,{0,0,0.5f}};stubEnemyCount=1;marked=&earlier;markCalls=0;
    squadOn=true;squadOrders=0;Put<void*>(squadObj,kSelfCtrl,squadCtrl);
    MapCmdInput in{};in.front=true;in.mouse=true;in.eye[1]=100.0f;float centre[3]{};
    const float vp[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    MapCommandView(vp,1280,720);MapCommandFrame(in,centre);
    game.sel.Add(squadObj);game.selected[0]=ObjRef::Of(squadObj);
    const float rect[4]={600,340,680,380};const int id=static_cast<int>(mapbtn::Id::focus);
    MapCommandButtons(rect,&id,1);
    inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);
    inputstub::keys[VK_LBUTTON]=false;MapCommandFrame(in,centre);
    Check(squadOrders==1 && squadGot.order==Order::focus && marked==&earlier && markCalls==0,
          "the focus button uses the existing mark, never the map enemy behind its HUD rectangle");
    Check(!game.hover && At<long>(control,0xC)==1,"HUD buttons release and occlude enemy hover identities");
    game.guardArmed=true;SuspendMapCommands();
    Check(!game.guardArmed && !view.buttons && game.sel.Has(squadObj),"closing clears armed buttons and their stale rectangles but keeps selection");
    ResetMapCommands();view=View{};squadOn=false;stubEnemyCount=0;marked=nullptr;
}

void MarkFromMap() noexcept {
    ResetMapCommands();view=View{};marked=nullptr;markCalls=0;squadOn=false;squadOrders=0;
    const float vp[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};   // world (x, y) in [-1, 1] across the screen
    unsigned char foe[0x400]{},foeCtrl[0x10]{};
    Put<void*>(foe,kSelfCtrl,foeCtrl);Put<long>(foeCtrl,8,1);Put<long>(foeCtrl,0xC,1);
    stubEnemies[0]=StubEnemy{foe,{0.0f,0.0f,0.5f}};stubEnemyCount=1;   // under the centred pointer
    MapCmdInput in{};in.front=true;in.eye[1]=100.0f;
    float centre[3]{};
    MapCommandView(vp,1280,720);
    MapCommandFrame(in,centre);   // opened: the pointer at the centre, on the enemy
    MapCommandReadout r{};
    Check(PlayerMapCommands(&r) && r.hover,"an enemy under the pointer is shown as such");
    Check(At<long>(foeCtrl,0xC)==2,"hover pins the enemy's control block without keeping the enemy alive");
    inputstub::keys['Q']=true;
    Check(MapCommandEats(true),"Q on an enemy is not the map's turn");
    Check(At<long>(foeCtrl,0xC)==3,"the pending press holds its own identity token");
    MapCommandFrame(in,centre);
    Check(marked==foe && markCalls==1,"Q on an enemy marks it");
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
    Check(marked==foe && squadOrders==1 && squadGot.order==Order::focus,"H on an enemy marks it and the squad focuses on it");
    // On foot: the point goes to the selection kept from the map.
    const float at[3]={50.0f,2.0f,-80.0f};
    Check(MapCommandGuardAt(at)==1 && squadOrders==2 && squadGot.order==Order::guard && squadGot.at[0]==50.0f &&
          squadGot.at[2]==-80.0f,"the mark key's point: the selected squad guards it");
    game.sel.Clear();
    Check(MapCommandGuardAt(at)==-1 && squadOrders==2,"the mark key's point with nothing selected: no order");
    squadOn=false;stubEnemyCount=0;ResetMapCommands();
    Check(At<long>(foeCtrl,0xC)==1,"map reset balances both hover and press weak references");
}

void MarkLifetimeAndConfig() noexcept {
    auto* foe=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(foe!=nullptr,"allocate a hover target");
    if(!foe)return;
    unsigned char control[0x10]{},replacement[0x10]{};
    const float vp[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    MapCmdInput in{};in.front=true;in.eye[1]=100.0f;
    float centre[3]{};
    for(int scenario=0;scenario<8;++scenario) {
        ResetMapCommands();view=View{};config=Config{};marked=nullptr;markCalls=0;
        std::memset(inputstub::keys,0,sizeof(inputstub::keys));
        std::memset(foe,0,4096);Put<void*>(foe,kSelfCtrl,control);Put<long>(control,8,1);Put<long>(replacement,8,1);
        Put<long>(control,0xC,1);Put<long>(replacement,0xC,1);
        stubEnemies[0]=StubEnemy{foe,{0,0,0.5f}};stubEnemyCount=1;
        MapCommandView(vp,1280,720);MapCommandFrame(in,centre);
        // The captured pointer predates the next update. Removal, identity reuse and toggling settings all fit in one frame.
        DWORD protect=0;
        if(scenario==0)Put<void*>(foe,kSelfCtrl,replacement);
        if(scenario==1)foe[kDead]=1;
        if(scenario==2)foe[0x18]=4;
        if(scenario==3)Put<long>(control,8,0);
        if(scenario==4)Check(VirtualProtect(foe,4096,PAGE_NOACCESS,&protect)!=FALSE,"protect a removed hover target");
        if(scenario==5)config.customNpcAi=false;
        if(scenario==6)config.enabled=false;
        if(scenario==7) {
            game.sel.Add(squadObj);
            SuspendMapCommands();
            Check(game.sel.Has(squadObj),"closing the map preserves the selected units");
            MapCommandReadout read{};
            Check(!PlayerMapCommands(&read) && !view.at,"closing discards its readout and previous view immediately");
        }
        stubEnemyCount=0;
        inputstub::keys['Q']=true;
        Check(!MapCommandEats(true),"invalid or disabled hover cannot swallow the camera key");
        MapCommandFrame(in,centre);
        Check(markCalls==0 && !marked,"invalid or disabled hover cannot mark a replacement or removed enemy");
        Check(At<long>(control,0xC)==1,"discarded hover releases its original control block");
        if(scenario==4){DWORD ignored=0;VirtualProtect(foe,4096,protect,&ignored);}
    }
    // Revalidate after the press was latched too: the target can disappear before dispatch in this frame.
    ResetMapCommands();view=View{};config=Config{};marked=nullptr;markCalls=0;
    std::memset(inputstub::keys,0,sizeof(inputstub::keys));
    Put<void*>(foe,kSelfCtrl,control);Put<long>(control,8,1);Put<long>(control,0xC,1);
    stubEnemies[0]=StubEnemy{foe,{0,0,0.5f}};stubEnemyCount=1;
    MapCommandView(vp,1280,720);MapCommandFrame(in,centre);
    inputstub::keys['Q']=true;Check(MapCommandEats(true),"live target press is latched");
    Put<void*>(foe,kSelfCtrl,replacement);stubEnemyCount=0;
    MapCommandFrame(in,centre);
    Check(!markCalls,"replacement after latching is not marked");

    // With NPC AI off, even a current registry entry must not be offered as Q/H's target.
    for(bool disabled:{false,true}) {
        ResetMapCommands();view=View{};config=Config{};marked=nullptr;markCalls=0;
        std::memset(inputstub::keys,0,sizeof(inputstub::keys));
        config.customNpcAi=!disabled;config.enabled=disabled;
        stubEnemyCount=1;
        MapCommandView(vp,1280,720);MapCommandFrame(in,centre);
        inputstub::keys['Q']=true;inputstub::keys['H']=true;
        Check(!MapCommandEats(true),"disabled NPC marking does not consume its camera key");
        MapCommandFrame(in,centre);
        MapCommandReadout read{};
        Check(PlayerMapCommands(&read) && !read.hover && !markCalls,"disabled marking exposes no target or action");
    }
    config=Config{};std::memset(inputstub::keys,0,sizeof(inputstub::keys));stubEnemyCount=0;ResetMapCommands();
    VirtualFree(foe,0,MEM_RELEASE);
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
void SupportInput() noexcept {
    ResetMapCommands();view=View{};config=Config{};supportCalls=0;supportChosen=-1;
    std::memset(inputstub::keys,0,sizeof(inputstub::keys));
    MapCmdInput in{};in.front=true;in.usingPad=true;in.eye[1]=100.0f;in.look[0]=80;in.look[2]=120;
    float centre[3]{};MapCommandFrame(in,centre);
    inputstub::keys[VK_OEM_6]=true;MapCommandFrame(in,centre);
    Check(game.supportPick==1 && !supportCalls,"catalog choice does not dispatch a unit");
    MapCommandFrame(in,centre);Check(game.supportPick==1,"holding next advances once");
    inputstub::keys[VK_OEM_6]=false;inputstub::keys['C']=true;MapCommandFrame(in,centre);
    Check(supportCalls==1 && supportChosen==1 && std::fabs(supportTarget[0]-80)<0.01f && std::fabs(supportTarget[2]-120)<0.01f,
          "C dispatches chosen support to map ground without selected units");
    MapCommandFrame(in,centre);Check(supportCalls==1,"holding call does not spawn every frame");
    MapCommandReadout read{};Check(PlayerMapCommands(&read) && read.supportName[0] && read.noteFresh,"support result published for HUD");
    game.supportArmed=true;SuspendMapCommands();Check(!game.supportArmed,"closing map cancels pending support placement");
    std::memset(inputstub::keys,0,sizeof(inputstub::keys));ResetMapCommands();
}
}  // namespace
}  // namespace crew
int main() {
    crew::UnitLifetime();crew::PointerAndInput();crew::CameraIsolation();crew::FormationKey();crew::ButtonClicks();
    crew::FocusButtonPreservesMark();crew::MarkFromMap();crew::MarkLifetimeAndConfig();
    crew::SupportInput();
    std::printf("map_command_runtime_check: %d checks, %d failed\n",crew::cases,crew::failures);
    return crew::failures ? 1 : 0;
}
