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
void SupportCallStatus(wchar_t* out,std::size_t capacity) noexcept {if(out && capacity)out[0]=0;}
int SupportCallCount() noexcept { return 3; }
const wchar_t* SupportCallName(int) noexcept { return L"Support"; }
SupportIcon SupportCallIcon(int) noexcept { return SupportIcon::jet; }
SupportVariant SupportCallVariant(int) noexcept { return SupportVariant::none; }
SupportReadiness SupportCallReadiness() noexcept { return {SupportReady::ready,0}; }
int supportCalls=0,supportChosen=-1;float supportTarget[3]{};
bool SupportCallAt(int index,const float* target,wchar_t* note,std::size_t capacity) noexcept {
    ++supportCalls;supportChosen=index;std::memcpy(supportTarget,target,12);
    _snwprintf_s(note,capacity,_TRUNCATE,L"support received");return true;
}
int payloadRequests=0,payloadSeat=-1,payloadEntry=-1;std::uint64_t payloadToken=0;
bool RequestPayloadSelection(std::uint64_t token,int seat,int entry) noexcept {
    ++payloadRequests;payloadToken=token;payloadSeat=seat;payloadEntry=entry;return true;
}
unsigned char* PlayerHuman() noexcept {return nullptr;}
const void* remoteAuthority=nullptr;bool allRemote=false,mapOnline=false;
bool IsOnlineAuthority(const void* object) noexcept {return !allRemote && object!=remoteAuthority;}
CommandNetworkResult networkReply{};unsigned networkSubmits=0,networkUnits=0,networkTotal=0;
std::uint32_t networkSlots[kCommandNetUnits]{};ObjRef networkIdentities[kCommandNetUnits]{};Command networkCommand{};
std::uint32_t SubmitMapCommand(const ObjRef&,const ObjRef* ids,unsigned count,const mapcmd::Command& command,const ObjRef&,
    wchar_t* note,std::size_t size,const std::uint32_t* slots,std::uint32_t total) noexcept {
    ++networkSubmits;networkUnits=count;networkTotal=total;networkCommand=command;
    for(unsigned i=0;i<count && i<kCommandNetUnits;++i){networkIdentities[i]=ids[i];networkSlots[i]=slots ? slots[i] : 0;}
    networkReply={};networkReply.request=91;networkReply.state=CommandNetworkState::pending;networkReply.count=count;
    _snwprintf_s(note,size,_TRUNCATE,L"queued");return 91;
}
bool ReadMapCommandNetworkResult(CommandNetworkResult* out) noexcept {*out=networkReply;return out->request!=0;}
bool MapCommandNetworkReady() noexcept {return true;}

ObjRef NpcMarkedIdentity() noexcept {return {};}
NpcCommandResult NpcSquadCommandForRequester(const ObjRef& id,const mapcmd::Command& command,const ObjRef&,const ObjRef&) noexcept {
    return SquadCommand(id.obj,command) ? NpcCommandResult{NpcCommandReason::none,1} : NpcCommandResult{NpcCommandReason::failed,0};
}
unsigned char* image=nullptr;
bool NpcDriver(const unsigned char* v) noexcept {
    if(!v || !SeatCount(v))return false;
    auto* seat=SeatAt(const_cast<unsigned char*>(v),0);const auto who=SeatRider(seat);
    if(who==Rider::dummy)return true;
    const auto* human=At<const unsigned char*>(seat,kSeatRider);
    return who==Rider::other && !AnyPlayerIn(seat) && human && At<const void*>(human,0)==image+0x17CDF28;
}

void Log(const char*,...) noexcept {}
bool InSession() noexcept { return mapOnline; }
float MapRay(const float*,const float*,float*) noexcept { return -1.0f; }
float MapFloorRay(const float*,const float*,float*) noexcept { return -1.0f; }
bool groundReady=false;
bool MapGroundNear(float,float,float level,float* out,bool) noexcept {*out=level;return groundReady;}
int HeliCommandUnits(CommandUnit*,int) noexcept { return 0; }
int JetCommandUnits(CommandUnit*,int) noexcept { return 0; }
int GroundCommandUnits(CommandUnit*,int) noexcept { return 0; }
bool HeliCommand(const void*,const Command&) noexcept { return false; }
bool JetCommand(const void*,const Command&) noexcept { return false; }
bool GroundCommand(const void*,const Command&) noexcept { return false; }
bool HeliSharesPost() noexcept { return true; }
// One stand-in squad (when `squadOn`), its last order; the enemies the lock registry would list; the mark npcai.cpp keeps.
unsigned char squadObj[0x100]{},squadCtrl[0x10]{};
bool squadOn=false,squadLocked=false,squadRiding=false;
Command squadGot{};int squadOrders=0;
int SquadCommandUnits(CommandUnit* out,int most) noexcept {
    if(!squadOn || most<1)return 0;
    out[0]=CommandUnit{squadObj,"squad",Command{},false,{0.0f,0.0f,0.0f}};
    out[0].locked=squadLocked;out[0].riding=squadRiding;
    return 1;
}
int TankCommandUnits(CommandUnit*,int) noexcept {return 0;}
int SquadRows(SquadRow*,int,SquadTally* tally) noexcept {if(tally)*tally=SquadTally{};return 0;}
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
// The formation menu (npcai.cpp): the shapes set, the guarding squad's defence.
int guardSet=-1,marchSet=-1;
int SetGuardFormation(const void* leader,int shape) noexcept { if(leader!=guardLeader)return -1;guardSet=shape;return shape; }
int NpcGuardShape(const void* leader) noexcept { return leader==guardLeader ? 10 : -1; }
int SetMarchFormation(int shape) noexcept { marchSet=shape;return shape; }
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
    Put<const void*>(rider,0,image+0x17CDF28);Put<unsigned char>(rider,edf::kHumanPlayer,0);Put<void*>(rider,edf::kHumanPad,nullptr);
    Check(ReadCommandUnit(ref,"real NPC",Command{},false,&unit),"vehicle with a real soldier driver stays visible and commandable after dummy removal");
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
    Check(game.armedOrder && game.armed==Order::guard,"the guard's button arms the next click on the ground");
    click();
    Check(!game.armedOrder,"clicked again: disarmed");
    click();
    in.dy=150.0f/kPointerGain/(720.0f/1080.0f);MapCommandFrame(in,centre);in.dy=0.0f;   // off the buttons, on the ground
    click();
    Check(!game.armedOrder && game.sel.n==0,"armed, a click on the ground is the guard's point (taken, disarmed), no unit picked");
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
    game.armedOrder=true;game.armed=Order::move;SuspendMapCommands();
    Check(!game.armedOrder && !view.buttons && game.sel.Has(squadObj),"closing clears armed buttons and their stale rectangles but keeps selection");
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
    const float at[3]={50.0f,2.0f,-80.0f};groundReady=true;
    Check(MapCommandGuardAt(at)==1 && squadOrders==2 && squadGot.order==Order::guard && squadGot.at[0]==50.0f &&
          squadGot.at[2]==-80.0f,"the mark key's point: the selected squad guards it");
    game.sel.Clear();
    Check(MapCommandGuardAt(at)==-1 && squadOrders==2,"the mark key's point with nothing selected: no order");
    squadOn=false;stubEnemyCount=0;groundReady=false;ResetMapCommands();
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
    MapCommandReadout read{};
    Check(PlayerMapCommands(&read) && read.supports==3 && read.support[1].name[0] && read.supportPick==1 && read.noteFresh,
          "the catalog, its pick and the support result published for the HUD's bar");
    game.armedSupport=2;SuspendMapCommands();Check(game.armedSupport<0,"closing map cancels pending support placement");
    std::memset(inputstub::keys,0,sizeof(inputstub::keys));ResetMapCommands();
}
void UiCaptureAndSnapshots() noexcept {
    ResetMapCommands();view=View{};config=Config{};
    std::memset(inputstub::keys,0,sizeof(inputstub::keys));squadOn=true;payloadRequests=0;
    Put<void*>(squadObj,kSelfCtrl,squadCtrl);
    MapCmdInput in{};in.front=true;in.mouse=true;in.eye[1]=100;float centre[3]{};
    const float vp[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    MapCommandView(vp,1280,720);MapCommandFrame(in,centre);
    const float panel[4]={500,300,900,500},row[4]={600,340,680,380};
    MapCommandUiPanels(panel,1);ObjRef identity=ObjRef::Of(squadObj);MapCommandSquadButtons(row,&identity,1);
    inputstub::keys[VK_CONTROL]=true;inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);
    Check(MapCommandPointerCaptured() && !MapCommandBoxing(),"Ctrl-left begun on a squad row captures UI instead of drawing a map box");
    inputstub::keys[VK_LBUTTON]=false;MapCommandFrame(in,centre);
    Check(game.sel.Has(squadObj) && !MapCommandPointerCaptured(),"click selects the published squad identity and releases capture");
    inputstub::keys[VK_CONTROL]=false;game.sel.Clear();RememberSelection(game);
    inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);
    unsigned char replacement[0x10]{};Put<void*>(squadObj,kSelfCtrl,replacement);
    inputstub::keys[VK_LBUTTON]=false;MapCommandFrame(in,centre);
    Check(!game.sel.n,"a rendered row cannot select a replacement at a recycled address");
    Put<void*>(squadObj,kSelfCtrl,squadCtrl);MapCommandSquadButtons(nullptr,nullptr,0);
    const int entry=3;MapCommandPayloadButtons(row,77,2,&entry,1);
    inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);inputstub::keys[VK_LBUTTON]=false;MapCommandFrame(in,centre);
    Check(payloadRequests==1 && payloadToken==77 && payloadSeat==2 && payloadEntry==3 && !game.sel.n,
        "payload click submits the exact rendered token/seat/entry and never selects a world object");
    inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);
    MapCommandPayloadButtons(row,78,2,&entry,1);inputstub::keys[VK_LBUTTON]=false;MapCommandFrame(in,centre);
    Check(payloadRequests==1,"payload topology changing during a click cancels the old hitbox action");
    inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);in.front=false;MapCommandFrame(in,centre);
    Check(payloadRequests==1 && !game.uiLeft,"losing foreground cancels UI press instead of treating it as a click release");
    in.front=true;MapCommandFrame(in,centre);
    Check(MapCommandPointerCaptured(),"restored foreground suppresses the old held gesture until release");
    inputstub::keys[VK_LBUTTON]=false;MapCommandFrame(in,centre);
    Check(payloadRequests==1,"foreground restoration cannot replay a cancelled press");
    inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);view.at=0;MapCommandFrame(in,centre);
    MapCommandView(vp,1280,720);MapCommandFrame(in,centre);
    inputstub::keys[VK_LBUTTON]=false;MapCommandFrame(in,centre);
    Check(payloadRequests==1,"stale rendered view cancels an in-progress click even when the same token returns");
    MapCommandView(vp,1920,1080);
    Check(!view.buttons && !view.squads && !view.payloads && !view.panels,"viewport resize invalidates every old-layout hitbox before new panels are published");
    MapCommandView(vp,1280,720);MapCommandUiPanels(panel,1);MapCommandPayloadButtons(row,78,2,&entry,1);
    inputstub::keys[VK_RBUTTON]=true;MapCommandFrame(in,centre);
    Check(MapCommandPointerCaptured(),"right press on panel captures camera turning");
    in.dx=700;MapCommandFrame(in,centre);in.dx=0;
    Check(MapCommandPointerCaptured() && game.pointer.x>900,"UI capture keeps a free pointer and stays captured outside the panel");
    inputstub::keys[VK_RBUTTON]=false;MapCommandFrame(in,centre);
    Check(!MapCommandPointerCaptured(),"right release ends its capture");
    in.dx=(550-game.pointer.x)/(720.0f/1080.0f);MapCommandFrame(in,centre);in.dx=0;
    game.armedOrder=true;game.armed=Order::guard;inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);
    inputstub::keys[VK_LBUTTON]=false;MapCommandFrame(in,centre);
    Check(game.armedOrder && !game.armedClick,"inert panel background does not consume the armed world's guard destination");
    inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);SuspendMapCommands();
    Check(!MapCommandPointerCaptured() && !view.squads && !view.payloads && !view.panels,"closing map drops captures and all stale UI snapshots");
    std::memset(inputstub::keys,0,sizeof(inputstub::keys));squadOn=false;ResetMapCommands();
}
// Only the units that take orders are picked (the user, 2026-10-09: "总有一些我不能指挥的"; "坦克上的…框选的时候应该去重"):
// Tab passes over a script's squad and a riding one, a script's squad's panel row says why instead of selecting it, a
// riding squad's row still picks it (for its dismount), and the log of a refused order counts why per unit.
void PickOnlyCommandable() noexcept {
    ResetMapCommands();view=View{};config=Config{};
    std::memset(inputstub::keys,0,sizeof(inputstub::keys));squadOn=true;squadLocked=true;squadRiding=false;
    Put<void*>(squadObj,kSelfCtrl,squadCtrl);
    MapCmdInput in{};in.front=true;in.mouse=true;in.eye[1]=100.0f;float centre[3]{};
    const float vp[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    MapCommandView(vp,1280,720);MapCommandFrame(in,centre);
    const auto tab=[&]{inputstub::keys[VK_TAB]=true;MapCommandFrame(in,centre);inputstub::keys[VK_TAB]=false;MapCommandFrame(in,centre);};
    tab();
    MapCommandReadout r{};
    Check(!game.sel.n && PlayerMapCommands(&r) && r.count==1 && r.pickable==0,"Tab passes over a script's squad (shown, not picked)");
    const float panel[4]={500,300,900,500},row[4]={600,340,680,380};
    MapCommandUiPanels(panel,1);ObjRef identity=ObjRef::Of(squadObj);MapCommandSquadButtons(row,&identity,1);
    inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);inputstub::keys[VK_LBUTTON]=false;MapCommandFrame(in,centre);
    Check(!game.sel.n && std::wcscmp(game.note,CommandFailureText(NpcCommandReason::scripted))==0,
          "a script's squad's row: not selected, the note says why");
    squadLocked=false;squadRiding=true;
    inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);inputstub::keys[VK_LBUTTON]=false;MapCommandFrame(in,centre);
    Check(game.sel.Has(squadObj),"a riding squad's row picks it (its dismount)");
    game.sel.Clear();RememberSelection(game);MapCommandSquadButtons(nullptr,nullptr,0);MapCommandUiPanels(nullptr,0);
    tab();
    Check(!game.sel.n,"Tab passes over a riding squad (its vehicle is the unit)");
    squadRiding=false;tab();
    Check(game.sel.Has(squadObj),"Tab picks a squad on foot that takes orders");
    // The squad panel's summary row: a click opens the panel (published), another folds it.
    const float foldRow[4]={600,400,680,420};
    MapCommandUiPanels(panel,1);MapCommandSquadFold(foldRow);
    in.dx=(640.0f-game.pointer.x)/(720.0f/1080.0f);in.dy=(410.0f-game.pointer.y)/(720.0f/1080.0f);MapCommandFrame(in,centre);in.dx=in.dy=0.0f;
    inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);inputstub::keys[VK_LBUTTON]=false;MapCommandFrame(in,centre);
    Check(game.panelOpen && PlayerMapCommands(&r) && r.squadOpen,"the summary row opens the squad panel");
    inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);inputstub::keys[VK_LBUTTON]=false;MapCommandFrame(in,centre);
    Check(!game.panelOpen,"clicked again: folded");
    MapCommandSquadFold(nullptr);MapCommandUiPanels(nullptr,0);
    std::memset(inputstub::keys,0,sizeof(inputstub::keys));squadOn=false;ResetMapCommands();
}
// The formation button opens a menu of the shapes (the user, 2026-10-09: "这个编队应该点击以后展开选择里面的东西"; before,
// each click stepped to the next shape): a guarding squad selected, its defences offered with its own lit; a row's
// click sets that shape and closes the menu; the right button closes it with no order; T still steps through.
void FormationMenu() noexcept {
    ResetMapCommands();view=View{};config=Config{};
    std::memset(inputstub::keys,0,sizeof(inputstub::keys));squadOn=true;squadLocked=squadRiding=false;
    Put<void*>(squadObj,kSelfCtrl,squadCtrl);guardLeader=squadObj;guardSet=marchSet=-1;squadOrders=0;guardCalls=marchCalls=0;
    MapCmdInput in{};in.front=true;in.mouse=true;in.eye[1]=100.0f;float centre[3]{};
    const float vp[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    MapCommandView(vp,1280,720);MapCommandFrame(in,centre);
    game.sel.Add(squadObj);game.selected[0]=ObjRef::Of(squadObj);
    const float button[4]={600,340,680,380};const int id=static_cast<int>(mapbtn::Id::formation);
    MapCommandButtons(button,&id,1);
    const auto click=[&]{inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);inputstub::keys[VK_LBUTTON]=false;MapCommandFrame(in,centre);};
    click();
    MapCommandReadout r{};
    Check(PlayerMapCommands(&r) && r.formationMenu && r.formationGuard && !r.formationMarch && r.formationGuardShape==10 && guardCalls==0,
          "the formation button opens its menu: the guarding squad's defences, its own lit, nothing cycled");
    const float rows[8]={600,250,760,276, 600,280,760,306};const int entries[2]={mapbtn::MenuEntry(true,1),mapbtn::MenuEntry(true,5)};
    MapCommandFormationButtons(rows,entries,2);
    in.dy=(293.0f-game.pointer.y)/(720.0f/1080.0f);MapCommandFrame(in,centre);in.dy=0.0f;
    click();
    Check(guardSet==5 && !game.formationMenu && game.sel.Has(squadObj) && squadOrders==0,"a menu row: that defence set, the menu closed, the selection kept");
    MapCommandFormationButtons(nullptr,nullptr,0);
    in.dy=(360.0f-game.pointer.y)/(720.0f/1080.0f);MapCommandFrame(in,centre);in.dy=0.0f;
    click();
    Check(game.formationMenu,"opened again");
    inputstub::keys[VK_RBUTTON]=true;MapCommandFrame(in,centre);inputstub::keys[VK_RBUTTON]=false;MapCommandFrame(in,centre);
    Check(!game.formationMenu && squadOrders==0,"the right button closes the menu, no move given");
    click();
    game.sel.Clear();RememberSelection(game);MapCommandFrame(in,centre);
    Check(!game.formationMenu,"no squad selected: the menu closes");
    MapCommandButtons(nullptr,nullptr,0);
    std::memset(inputstub::keys,0,sizeof(inputstub::keys));squadOn=false;guardLeader=nullptr;ResetMapCommands();
}
// RTS (the user, 2026-10-09): the right button let go on the map without a drag moves the selection there, on an enemy
// attacks it; a right drag (the map's turn) gives nothing; Z attack-moves; an armed move button takes the next left
// click, the right button cancels it; a support row arms its call for the next left click. A squad seated in a vehicle
// is offered no recruitment and takes no point order.
void RtsClicks() noexcept {
    ResetMapCommands();view=View{};config=Config{};groundReady=true;
    std::memset(inputstub::keys,0,sizeof(inputstub::keys));
    squadOn=true;squadOrders=0;squadGot={};marked=nullptr;markCalls=0;stubEnemyCount=0;
    Put<void*>(squadObj,kSelfCtrl,squadCtrl);
    MapCmdInput in{};in.front=true;in.mouse=true;in.eye[1]=100.0f;float centre[3]{};
    const float vp[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    MapCommandView(vp,1280,720);MapCommandFrame(in,centre);
    game.sel.Add(squadObj);game.selected[0]=ObjRef::Of(squadObj);
    const auto right=[&]{inputstub::keys[VK_RBUTTON]=true;MapCommandFrame(in,centre);inputstub::keys[VK_RBUTTON]=false;MapCommandFrame(in,centre);};
    right();
    Check(squadOrders==1 && squadGot.order==Order::move,"right click on the ground: the selection moves there");
    inputstub::keys[VK_RBUTTON]=true;MapCommandFrame(in,centre);
    in.dx=40.0f;MapCommandFrame(in,centre);in.dx=0.0f;   // the map turned by a right drag
    inputstub::keys[VK_RBUTTON]=false;MapCommandFrame(in,centre);
    Check(squadOrders==1,"a right drag turns the map: no order");
    unsigned char foe[0x400]{},foeCtrl[0x10]{};
    Put<void*>(foe,kSelfCtrl,foeCtrl);Put<long>(foeCtrl,8,1);Put<long>(foeCtrl,0xC,1);
    // The pointer is at x 1280/2 + 40 * (720/1080) px now: an enemy under it.
    const float px=(game.pointer.x-640.0f)/640.0f,py=(360.0f-game.pointer.y)/360.0f;
    stubEnemies[0]=StubEnemy{foe,{px,py,0.5f}};stubEnemyCount=1;
    MapCommandFrame(in,centre);
    right();
    Check(squadOrders==2 && squadGot.order==Order::focus && marked==foe,"right click on an enemy: marked and attacked");
    stubEnemyCount=0;MapCommandFrame(in,centre);npcmark::Assign(game.hover,{});
    inputstub::keys['Z']=true;MapCommandFrame(in,centre);inputstub::keys['Z']=false;MapCommandFrame(in,centre);
    Check(squadOrders==3 && squadGot.order==Order::attackMove,"Z: an attack-move to the pointer");
    const float rect[4]={1100,600,1180,640};const int id=static_cast<int>(mapbtn::Id::move);
    MapCommandButtons(rect,&id,1);
    const float back=game.pointer.x;
    in.dx=(1140.0f-game.pointer.x)/(720.0f/1080.0f);in.dy=(620.0f-game.pointer.y)/(720.0f/1080.0f);MapCommandFrame(in,centre);in.dx=in.dy=0.0f;
    const auto left=[&]{inputstub::keys[VK_LBUTTON]=true;MapCommandFrame(in,centre);inputstub::keys[VK_LBUTTON]=false;MapCommandFrame(in,centre);};
    left();
    Check(game.armedOrder && game.armed==Order::move && squadOrders==3,"the move button arms the next left click");
    right();
    Check(!game.armedOrder && squadOrders==3,"the right button cancels the armed move, gives no order");
    left();
    in.dx=(back-game.pointer.x)/(720.0f/1080.0f);in.dy=(300.0f-game.pointer.y)/(720.0f/1080.0f);MapCommandFrame(in,centre);in.dx=in.dy=0.0f;
    left();
    Check(squadOrders==4 && squadGot.order==Order::move && !game.armedOrder && game.sel.Has(squadObj),
          "armed, a left click on the ground is the move's point; the selection kept");
    // The support bar: a row's click arms that call (published), the next left click on the map calls it there.
    supportCalls=0;supportChosen=-1;
    const float rows[8]={20,200,240,226, 20,230,240,256};const int entries[2]={0,2};
    MapCommandSupportButtons(rows,entries,2);
    in.dx=(100.0f-game.pointer.x)/(720.0f/1080.0f);in.dy=(243.0f-game.pointer.y)/(720.0f/1080.0f);MapCommandFrame(in,centre);in.dx=in.dy=0.0f;
    left();
    MapCommandReadout r{};
    Check(game.armedSupport==2 && PlayerMapCommands(&r) && r.supportArmed==2 && !supportCalls,"a support row arms its call, nothing called yet");
    left();
    Check(game.armedSupport<0 && !supportCalls,"its row again: disarmed");
    left();
    in.dx=(640.0f-game.pointer.x)/(720.0f/1080.0f);MapCommandFrame(in,centre);in.dx=0.0f;
    left();
    Check(supportCalls==1 && supportChosen==2 && game.armedSupport<0 && game.sel.Has(squadObj),
          "armed, a left click on the map calls that support there, the selection kept");
    MapCommandSupportButtons(nullptr,nullptr,0);MapCommandButtons(nullptr,nullptr,0);
    // Nothing selected: the right button does nothing.
    game.sel.Clear();RememberSelection(game);right();
    Check(squadOrders==4,"nothing selected: a right click gives no order");
    // A squad seated in a vehicle: no RECRUIT offered, no point order taken; release still is.
    Game g{};g.count=1;
    CommandUnit riding{squadObj,"squad",Command{},false,{0,0,0}};riding.riding=true;riding.recruitable=false;
    g.list[0]=Entry{riding,Owner::squad};
    Check(!Takes(g.list[0],Order::recruit) && !Takes(g.list[0],Order::follow) && !Takes(g.list[0],Order::move) &&
          !Takes(g.list[0],Order::guard) && Takes(g.list[0],Order::dismount) && Takes(g.list[0],Order::none),
          "a riding squad: no recruit / follow / point order, dismount and release");
    CommandUnit free{squadObj,"squad",Command{},false,{0,0,0}};free.recruitable=true;g.list[0]=Entry{free,Owner::squad};
    Check(Takes(g.list[0],Order::recruit) && Takes(g.list[0],Order::move) && Takes(g.list[0],Order::attackMove),"a free squad on foot: all of them");
    CommandUnit tank{squadObj,"tank",Command{},false,{0,0,0}};g.list[0]=Entry{tank,Owner::tank};
    Check(Takes(g.list[0],Order::move) && !Takes(g.list[0],Order::follow),"a tank takes a move (as its post), not follow");
    std::memset(inputstub::keys,0,sizeof(inputstub::keys));squadOn=false;groundReady=false;marked=nullptr;ResetMapCommands();
}
void RemoteCommandResults() noexcept {
    ResetMapCommands();mapOnline=true;groundReady=true;networkSubmits=0;squadOrders=0;squadOn=true;
    unsigned char remote[0x100]{},remoteCtrl[0x10]{};Put<void*>(squadObj,kSelfCtrl,squadCtrl);Put<void*>(remote,kSelfCtrl,remoteCtrl);
    remoteAuthority=remote;
    Game mixed{};mixed.count=2;
    mixed.list[0]={CommandUnit{squadObj,"local",{},false,{}},Owner::squad};
    mixed.list[1]={CommandUnit{remote,"remote",{},false,{}},Owner::squad};
    mixed.sel.Add(squadObj);mixed.sel.Add(remote);int skipped=0;
    Check(Issue(mixed,Command{Order::guard,{10,2,20}},&skipped)==1 && !skipped && mixed.networkQueued==1,
        "mixed selection executes only local authority immediately and queues its remote squad");
    Check(networkSubmits==1 && networkUnits==1 && networkSlots[0]==1 && networkTotal==2 &&
          networkCommand.at[0]==10 && networkCommand.at[2]==20 && networkIdentities[0].ctrl==remoteCtrl,
        "remote guard carries original global formation slot/total and unshifted target identity");
    float expected[3];mapcmd::Formation(0,2,networkCommand.at,mapcmd::kFormationSpacing,expected);
    Check(squadOrders==1 && squadGot.at[0]==expected[0] && squadGot.at[2]==expected[2] && mixed.list[1].u.now.order==Order::none,
        "local guard uses matching global slot while remote snapshot is not fabricated as executed");
    networkReply.state=CommandNetworkState::completed;networkReply.units[0]={NpcCommandReason::none,3};
    CommandNetworkReply(mixed);
    Check(!mixed.networkRequest && mixed.noteAt && squadOrders==1,"authority result updates feedback without executing another local command");
    mixed.networkRequest=92;networkReply.request=92;networkReply.units[0]={NpcCommandReason::notOwner,0};CommandNetworkReply(mixed);
    Check(std::wcscmp(mixed.note,CommandFailureText(NpcCommandReason::notOwner))==0,"remote ownership refusal displays the actual reason");
    mixed.networkRequest=94;networkReply.request=93;CommandNetworkReply(mixed);
    Check(mixed.networkRequest==94,"late result cannot replace another pending request");
    networkReply.request=94;networkReply.state=CommandNetworkState::timedOut;CommandNetworkReply(mixed);
    Check(!mixed.networkRequest && std::wcscmp(mixed.note,hudtext::Tr(hudtext::Tx::cmdNpcNetworkTimeout))==0,"timeout ends waiting with a concrete message");
    unsigned char many[17][0x100]{},controls[17][0x10]{};Game over{};over.count=17;allRemote=true;
    for(int i=0;i<17;++i){Put<void*>(many[i],kSelfCtrl,controls[i]);over.list[i]={CommandUnit{many[i],"remote",{},false,{}},Owner::squad};over.sel.Add(many[i]);}
    Check(Issue(over,Command{Order::engage,{}},&skipped)==0 && skipped==17 && networkSubmits==1,
        "more than sixteen remote squads are rejected before any partial submission");
    allRemote=false;remoteAuthority=nullptr;mapOnline=false;groundReady=false;squadOn=false;ResetMapCommands();
}
void SelectionCapabilityMask() noexcept {
    Game selection{};selection.count=1;selection.list[0]={CommandUnit{squadObj,"tank",{},false,{}},Owner::tank};selection.sel.Add(squadObj);
    const float point[3]{};Publish(selection,true,true,point,true);MapCommandReadout r{};PlayerMapCommands(&r);
    Check(r.allowedOrders==((1u<<static_cast<unsigned>(Order::guard))|(1u<<static_cast<unsigned>(Order::move))|
                            (1u<<static_cast<unsigned>(Order::attackMove))|1u) && !r.selectedSquads,
        "tank selection enables only its post's point orders (guard, move, attack-move as its post) and release");
    selection.list[0].owner=Owner::squad;selection.list[0].u.locked=true;Publish(selection,true,true,point,true);PlayerMapCommands(&r);
    Check(!r.allowedOrders && !r.selectedSquads,"script locked squad exposes no executable buttons");
}
}  // namespace
}  // namespace crew
int main() {
    crew::UnitLifetime();crew::PointerAndInput();crew::CameraIsolation();crew::FormationKey();crew::ButtonClicks();
    crew::FocusButtonPreservesMark();crew::MarkFromMap();crew::MarkLifetimeAndConfig();
    crew::SupportInput();
    crew::UiCaptureAndSnapshots();crew::RtsClicks();crew::PickOnlyCommandable();crew::FormationMenu();
    crew::RemoteCommandResults();crew::SelectionCapabilityMask();
    std::printf("map_command_runtime_check: %d checks, %d failed\n",crew::cases,crew::failures);
    return crew::failures ? 1 : 0;
}
