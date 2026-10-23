// Commanding the plugin's NPC units from the map view (README 地图 → 指挥 NPC; the user, 2026-10-06: "在地图上可以指挥
// npc", "操作 需要一个框选吧"). While the map (map.cpp) is open:
//  - the units: the helis the plugin flies (heli.cpp), its jets, rotor carriers and drones with no carrier (jet.cpp), the
//    Depth Crawlers it drives (ground.cpp) -- each module lists the ones that take an order now (mapcmd.h);
//  - the mouse moves a pointer the HUD draws (the game's own mouse delta: the game has no cursor of its own there). RTS
//    since 2026-10-19 (the user: 「合并分组经常无效，分开也是，感觉你这个分组是不是和rts不太一样」): a left drag boxes the
//    units (Shift adds; with no unit to box, or a point armed, it pans the map as before), the middle drag and WASD pan,
//    a right drag turns, the wheel zooms; a left click selects the unit under the pointer (Shift adds / takes it out,
//    twice or Ctrl: every unit of its kind on the screen) or, on empty ground, clears the selection (mapcmd_logic.h Box,
//    Click, SelectKind); Ctrl+A every unit;
//  - the RTS groups (mapcmd_logic.h GroupKey): Ctrl+1-9 keeps the selection as a group, 1-9 selects it again (twice: the
//    map centres on it), held by every member's identity (mapcmd_logic.h UnitRef) and found again through any member
//    alive after its leader's death, on a guest too (npcai.cpp NpcSquadTopOf; NpcSquadSuccessor on the host);
//    the pad's D-pad groups 1-4. They replace SPLIT / JOIN (P / L), which rearranged the native follow trees;
//  - Tab / Shift+Tab (pad X) step through one unit at a time, then ALL (mapcmd_logic.h Cycle); the map centres on a unit
//    selected so;
//  - Z (pad Y): every selected unit attack-moves to the point under the pointer (with a pad: the screen's centre) and
//    guards it there; several stand round it in a formation (mapcmd_logic.h Formation; guard helis share one orbit round
//    the point instead: heli.cpp GuardOrbit spaces them). H (pad Y held): HOLD, each unit where it stands. V (pad RB):
//    follow the player; X (pad LB): stop (back to what it did before any command).
//  - the right button let go without a drag (a right drag still turns the map), by what is under it: an enemy: focus
//    fire on it; the player's own mark: FOLLOW; the ground: a move there (mapcmd_logic.h RightClickOrder). Shift with it
//    (or with Z) queues the point after the ones before it (mapcmd_logic.h Waypoints), kept by the unit's authority
//    (MapCommandAuthority: this machine's for its own units, the host's for the others through command protocol v7's
//    queued flag) and advanced by the scene's tick (MapCommandQueueTick: every frame, any map shut, the player dead or
//    watching). A point order's button (move, attack-move) or a support's row arms the next left click on the map as its
//    point; the right button cancels that instead.
//  - an enemy under the pointer (the user, 2026-10-07: "应该在地图里面也能按q标记"): the mark key (NpcMarkKey, Q) marks it
//    for the NPCs (npcai.cpp NpcMarkEnemy; the same one again: let go) instead of turning the map. With no enemy under the
//    pointer Q turns the map.
// The pointer's point: the map ray under it (mapcmd_logic.h ScreenRay on the view the HUD last drew), else that ray's
// meeting with the level ground through the focus (RayLevel).
// Online, owned units execute through their real AI authority. Replica squads are selected by native identity and
// requested through command_net; accepted transport is displayed separately from the correlated authority result.
// Remote vehicle commands are not fabricated by writing a local copy. Every order goes one way (Dispatch: this machine's
// units through MapCommandAuthority, the others' in one request), the formation (T and its menu) too.
#include "crew.h"
#include "formation.h"
#include "map_buttons.h"
#include "hudtext.h"
#include "npcai.h"
#include "npc_command.h"
#include "command_net.h"
#include "online_authority.h"
#include "layout.h"
#include "memory.h"
#include "map_cam.h"
#include "npc_mark.h"
#include "support_call.h"
#include "transport.h"
#include <Xinput.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include "map_pvp_input.h"
#include "map_enemy_ui.h"
#include "march_pace.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace crew {
bool RequestPayloadSelection(std::uint64_t token,int seat,int entry) noexcept;
namespace {
constexpr ULONGLONG kFreshMs=300;        // a readout older than this (wall) is the map closed
constexpr ULONGLONG kNoteMs=3000;        // the last command's word shown this long
constexpr float kPointFar=8000.0f;       // m: the map ray under the pointer reaches this far
constexpr float kPointerGain=1.0f;       // px (at 1080 lines) a mouse unit
constexpr float kClickMove=6.0f;         // mouse units: a left press that moved less is a click, not a pan
constexpr float kClickBox=6.0f;          // px: a Ctrl box smaller than this both ways is a click
constexpr float kClickRadius=22.0f;      // px (at 1080 lines) round a unit's icon a click takes it
constexpr float kPadPickRadius=90.0f;    // px (at 1080 lines) round the screen centre L3 takes an enemy

enum class Owner : std::uint8_t { heli, jet, ground, squad, tank };
struct Entry { CommandUnit u; Owner owner; ObjRef id{}; };   // id: its identity when listed (the module had it live)
struct DispatchFlags { bool queue=false,paced=false; };      // Dispatch: Shift (a point after the ones before it); the paced march (P)

struct Keys { bool tab,shift,ctrl,follow,release,left,right,middle,padNext,padGuard,padFollow,padRelease,
             engage,hold,board,dismount,dismiss,recruit,attackMove,mark,formation,sweep,health,pace,all,digit[9],supportPrev,supportNext,supportCall,
             // The test range's 「摆放」 panel (range_place.h): , . / Enter Backspace; the pad's D-pad, A, B.
             placePrev,placeNext,placeCategory,placeEnter,placeClose,padUp,padDown,padLeft,padRight,padA,padB,padPvp,padEnemy; };

// --- The game thread's own ---
struct Game {
    mapcmd::Selection sel;
    ObjRef selected[kCmdUnits]; // identity when selected; an address recycled since then is another unit
    mapcmd::UnitRef selRef[kCmdUnits]; // ...and its members' (a squad found again through any of them)
    Keys was;
    ULONGLONG frameAt;          // wall ms of the last frame (a gap: the map was closed, no edges the first frame)
    int count;
    Entry list[kCmdUnits];
    mapcmd::PointerPosition pointer; // placed once the first rendered viewport arrives
    bool boxing,pressing;       // a Ctrl + left drag / a plain left press (a click unless it moves kClickMove)
    float bx,by,moved;
    wchar_t note[80];
    ULONGLONG noteAt;
    int button;                 // the button a click let go on this frame (mapbtn::Id), -1 none
    // What the next left click on the ground is (one at a time: arming one disarms the other): a point order's point
    // (its button), a support call's point (its bar's row or chip); `armedClick` that click this frame.
    bool armedOrder=false;Order armed=Order::none;
    int armedSupport=-1;
    // The armed support's composed load (map_buttons.h ComposePanel), while it has seats; -1 none.
    int composeEntry=-1;SupportLoadout compose{};
    bool armedClick=false;
    int supportPick;
    int marchPace=-1;           // the paced march (P): 1 the slowest unit's pace, 0 each at full speed, -1 MapMarchPace's
    bool panelOpen=false;       // the squad panel opened past its commandable rows (mapcmd_logic.h SquadRowsShown)
    bool formationMenu=false;   // the formation button's menu open (map_buttons.h MenuEntry)
    bool rpressing=false,rOnUi=false;float rmoved=0.0f; // a right press (a click unless it moves kClickMove), begun on UI
    bool rightClick=false,rightOnUi=false;               // ...let go without a drag this frame
    ObjRef hover;               // original identity under the pointer; never recaptured from a cached address
    float hoverAt[3];
    bool eat,eatWas;            // the mark key's press took the enemy under the pointer: not the map's (MapCommandEats)
    ObjRef eatHover;            // ...that enemy's original identity, its lock point
    float eatAt[3];
    enum class UiKind : std::uint8_t { none,command,squad,payload,support,panel,fold,formation,compose,stats,place,pvp,enemy,legend };
    struct UiHit { UiKind kind=UiKind::none;int id=-1;ObjRef identity{};std::uint64_t token=0;int seat=-1,entry=-1; PvpStatus pvp{}; enemy_control::Snapshot enemy{}; };
    UiHit uiPress{};
    bool uiLeft=false,uiRight=false,rowPicked=false,suppressLeft=false,suppressRight=false;
    ObjRef requester{},focus{};
    // The RTS groups (mapcmd_logic.h GroupKey): each one's units by every member's identity (mapcmd_logic.h UnitRef: a
    // squad found again through any member alive), kept while the map is shut and dropped with the mission
    // (ResetMapCommands); the number keys' double tap; the pad's held buttons.
    mapcmd::UnitRef group[mapcmd::kGroups][mapcmd::kMaxSel]{};int groupN[mapcmd::kGroups]{};
    mapcmd::Tap tap{};
    mapcmd::Held padY{},padDir[4]{};
    const void* clickUnit=nullptr;ULONGLONG clickAt=0;   // the last click's unit and when (a second one: its kind)
    NpcCommandReason failure=NpcCommandReason::none;
    unsigned affected=0;
    // The last command's units that did not take it, by why (the log: "0 of 32 units took it" said nothing of why).
    int skipScript=0,skipRiding=0,skipCannot=0,skipRemote=0,skipFailed=0;
    std::uint32_t networkRequest=0;unsigned networkQueued=0;
    Order networkOrder=Order::none;
    wchar_t networkMessage[128]{};
    // The test range's 「摆放」 panel (range_place.h): its state, this frame's presses on it (the clicks UiClick takes, the
    // keys), whether it is there this mission (debug_spawn.cpp RangePlaceOn).
    rangeplace::Panel place{};
    rangeplace::Press placePress{};
    rangeplace::Repeat placeUp{},placeDown{};   // , . and the D-pad held: the rows walked (range_place.h Repeats)
    bool placeOn=false;
    pvpmenu::Pad pvpPad{};
    // The enemy-control target: chosen only by the user (a left click on its map mark, L3 at
    // the screen centre, or the card's arrows), never by where the pointer happens to pass.
    ObjRef enemyCandidate{};
    float enemyAimOffset[3]{};  // its picked lock point less its position, to keep the map mark on it
    bool enemyOpen=false;       // the card unfolded: changed only by its title click, a pick or L3
    enemyui::State enemyMenu{};
    MapEnemyControlReadout enemy{};
    bool enemyPadFire=false;int enemyPadEntry=-1;enemy_control::Snapshot enemyPadFireSnapshot{};
};
Game game{};
std::atomic<bool> pvpHoldsPad{false};
std::atomic<bool> enemyHoldsPad{false};
std::atomic<int> pvpPadSelection{-1};
std::atomic<bool> placeHoldsPad{false};   // MapPlaceHoldsPad
std::atomic<bool> pvpOpen{false};         // MapPvpOpen: the PVP panel unfolded by a click on its title
std::atomic<bool> legendOpen{false};      // MapLegendOpen: the legend unfolded by a click on its title
std::atomic<bool> boxingNow{false};
std::atomic<bool> pointerCaptured{false};

// --- The view the HUD last drew the map with (under `viewLock`) ---
constexpr int kUiItems=16;
struct View {
    float vp[16],w,h; ULONGLONG at; int buttons; mapbtn::Rect button[mapbtn::kCount]; int id[mapbtn::kCount];
    int squads=0,payloads=0,panels=0,supports=0;
    mapbtn::Rect squad[kUiItems]{},payload[kUiItems]{},panel[kMapUiPanels]{},support[kMapSupports]{};
    int supportEntry[kMapSupports]{};
    int composes=0;mapbtn::Rect compose[mapbtn::kComposeItems]{};int composeCode[mapbtn::kComposeItems]{};
    bool pvpVisible=false;
    int pvps=0;mapbtn::Rect pvp[kMapPvpItems]{};int pvpCode[kMapPvpItems]{};PvpStatus pvpStatus{};
    bool enemyVisible=false;int enemies=0;mapbtn::Rect enemy[kMapEnemyItems]{};int enemyCode[kMapEnemyItems]{};enemy_control::Snapshot enemyStatus{};
    int places=0;mapbtn::Rect place[kMapPlaceItems]{};int placeCode[kMapPlaceItems]{};   // the 「摆放」 panel
    int placeWindow=0;                                     // ...its list's rows drawn (0: shut / not drawn)
    bool legend=false;mapbtn::Rect legendRow{};            // the legend's title row
    ObjRef squadIdentity[kUiItems]{};
    bool folds=false;mapbtn::Rect fold{};   // the squad panel's summary row
    int menus=0;mapbtn::Rect menu[kMapFormationEntries]{};int menuEntry[kMapFormationEntries]{};   // the formation menu's rows
    std::uint64_t payloadToken=0;int payloadSeat=-1,payloadEntry[kUiItems]{};
};
View view{};
SRWLOCK viewLock=SRWLOCK_INIT;

// --- Published for the draw (under `lock`) ---
MapCommandReadout readout{};
MapEnemyControlReadout enemyReadout{};
ULONGLONG readoutAt=0;
SRWLOCK lock=SRWLOCK_INIT;

bool Down(int vk) noexcept { return (edf::AsyncKey(vk)&0x8000)!=0; }

// Every unit that takes an order now, in a stable order (the vehicles' addresses), into `list`; how many.
int ListUnits(Entry* list) noexcept {
    CommandUnit buf[kCmdUnits];
    int count=0;
    auto add=[&](int n,Owner o){ for(int i=0;i<n && count<kCmdUnits;++i)list[count++]=Entry{buf[i],o,ObjRef::Of(buf[i].v)}; };
    add(HeliCommandUnits(buf,kCmdUnits),Owner::heli);
    add(JetCommandUnits(buf,kCmdUnits-count),Owner::jet);
    add(GroundCommandUnits(buf,kCmdUnits-count),Owner::ground);
    add(TankCommandUnits(buf,kCmdUnits-count),Owner::tank);
    add(SquadCommandUnits(buf,kCmdUnits-count),Owner::squad);
    // A vehicle unit carrying a squad that takes orders: DISMOUNT / DISMOUNT ALL given to it are that squad's (the user,
    // 2026-10-09: the crew's squad is not boxed with its vehicle any more, so the vehicle is where its crew is told off).
    for(int i=0;i<count;++i) {
        const Entry& q=list[i];
        if(q.owner!=Owner::squad || q.u.locked || !q.u.riding || !q.u.vehicle)continue;
        for(int k=0;k<count;++k)if(list[k].owner!=Owner::squad && list[k].u.v==q.u.vehicle)list[k].u.carries=true;
    }
    std::sort(list,list+count,[](const Entry& a,const Entry& b){ return std::less<const void*>()(a.u.v,b.u.v); });
    return count;
}
void List(Game& g) noexcept { g.count=ListUnits(g.list); }

// --- A unit's identity that outlives its top (mapcmd_logic.h UnitRef) ---
mapcmd::UnitKey Key(const ObjRef& r) noexcept { return mapcmd::UnitKey{r.obj,r.ctrl}; }
ObjRef RefOf(const mapcmd::UnitKey& k) noexcept { return ObjRef{k.obj,k.ctrl}; }
// The unit `e` as a UnitRef: its top (the vehicle, the squad's top) and, a squad, its members now (npcai.cpp).
mapcmd::UnitRef Capture(const Entry& e) noexcept {
    mapcmd::UnitRef r{};r.top=Key(e.id);
    if(e.owner!=Owner::squad)return r;
    ObjRef members[mapcmd::kUnitMembers];
    const int n=NpcSquadMembers(e.u.v,members,mapcmd::kUnitMembers);
    mapcmd::UnitKey keys[mapcmd::kUnitMembers];
    for(int i=0;i<n;++i)keys[i]=Key(members[i]);
    mapcmd::Remember(r,r.top,keys,n);
    return r;
}
// The top of what `k` names now: a living soldier's squad top (the native follow tree), a live vehicle itself; false
// for one dead or gone (another object at its address).
bool TopNow(const mapcmd::UnitKey& k,mapcmd::UnitKey* top) noexcept {
    if(!k.obj)return false;
    ObjRef t;
    if(NpcSquadTopOf(RefOf(k),&t)){*top=Key(t);return true;}
    __try {
        const auto* o=static_cast<const unsigned char*>(k.obj);
        if(!Readable(o,kSelfCtrl+sizeof(void*)) || !Readable(o,kDead+1) || !RefOf(k).Is(o) || o[kDead])return false;   // a live soldier: NpcSquadTopOf above
        *top=k;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Where `r`'s unit is in `list` now (mapcmd_logic.h ResolveUnit; the host's own record of a hand-over first), -1 none;
// `alive`: whether any of it lives still.
int Resolve(mapcmd::UnitRef& r,const Entry* list,int count,bool* alive) noexcept {
    mapcmd::UnitKey keys[kCmdUnits];
    for(int i=0;i<count;++i)keys[i]=Key(list[i].id);
    ObjRef next;
    if(NpcSquadSuccessor(RefOf(r.top),&next))
        for(int i=0;i<count;++i)if(mapcmd::SameKey(keys[i],Key(next))){r.top=keys[i];if(alive)*alive=true;return i;}
    return mapcmd::ResolveUnit(r,keys,count,&TopNow,alive);
}
// `r` refreshed from its unit listed now (`e`): its members as they are, the ones it had and still may come back kept.
void Refresh(mapcmd::UnitRef& r,const Entry& e) noexcept {
    const mapcmd::UnitRef now=Capture(e);
    mapcmd::Remember(r,now.top,now.member,now.n);
}

void KeepSelection(Game& g,const void* const* ids) noexcept {
    // A selected squad whose top died is the same squad under its new top, found through any member still alive
    // (mapcmd_logic.h UnitRef), not lost from the selection.
    for(int i=0;i<g.sel.n;++i) {
        if(mapcmd::IndexOf(ids,g.count,g.sel.id[i])>=0 || g.selected[i].obj!=g.sel.id[i])continue;
        const int k=Resolve(g.selRef[i],g.list,g.count,nullptr);
        if(k<0 || g.sel.Has(g.list[k].u.v))continue;
        g.sel.id[i]=g.list[k].u.v;g.selected[i]=g.list[k].id;
    }
    mapcmd::Keep(g.sel,ids,g.count);
    for(int i=g.sel.n-1;i>=0;--i) {
        const void* id=g.sel.id[i];
        bool same=false;
        if(Readable(id,kSelfCtrl+sizeof(void*)))
            for(const auto& old:g.selected)if(old.Is(id)){same=true;break;}
        if(!same)g.sel.Remove(id);
    }
}

void RememberSelection(Game& g) noexcept {
    static mapcmd::UnitRef was[kCmdUnits];   // game thread only; too big for its stack
    std::memcpy(static_cast<void*>(was),g.selRef,sizeof(was));
    for(int i=0;i<kCmdUnits;++i) {
        g.selected[i]=i<g.sel.n ? ObjRef::Of(g.sel.id[i]) : ObjRef{};
        g.selRef[i]=mapcmd::UnitRef{};
        if(i>=g.sel.n)continue;
        g.selRef[i].top=Key(g.selected[i]);
        for(const auto& old:was)if(mapcmd::Names(old,g.selRef[i].top)){g.selRef[i]=old;g.selRef[i].top=Key(g.selected[i]);break;}
        for(int k=0;k<g.count;++k)if(g.list[k].u.v==g.sel.id[i]){Refresh(g.selRef[i],g.list[k]);break;}
    }
}

// An order to unit `e` (this machine its authority) from `requester`: what its module did with it. `list` the units
// listed with it (a vehicle's riding squads); `sel` the selection it came with (a squad selected too gives its own), may
// be null (a queued point, a network request).
NpcCommandResult GiveUnit(const Entry& e,const Command& order,const ObjRef& requester,const ObjRef& focus,
                          const Entry* list,int count,const mapcmd::Selection* sel) noexcept {
    const auto picked=[&](const void* v){return sel && sel->Has(v);};
    // DISMOUNT / DISMOUNT ALL to a vehicle: its riding squads' (each once: not those selected themselves, their own row
    // gives it).
    if(e.owner!=Owner::squad && mapcmd::DismountOrder(order.order)) {
        NpcCommandResult all{NpcCommandReason::failed,0};bool any=false;
        for(int i=0;i<count;++i) {
            const Entry& q=list[i];
            if(q.owner!=Owner::squad || q.u.locked || q.u.vehicle!=e.u.v || picked(q.u.v))continue;
            const auto result=NpcSquadCommandForRequester(ObjRef::Of(q.u.v),order,requester,focus);
            if(!result.Accepted()){if(!any)all.reason=result.reason;}
            else{all.affected+=result.affected;any=true;all.reason=NpcCommandReason::none;}
        }
        return all;
    }
    // A transport carrying its squad (transport.cpp; the user, 2026-10-09: "卡车之类的运输载具改成断剑那种操作方式"): a point
    // order to the vehicle is the squad's (they ride there and get off short of the point), unless the squad is selected
    // too (its own row gives it, once).
    if(e.owner!=Owner::squad && mapcmd::PointOrder(order.order))
        if(const void* rider=TransportRiderOf(e.u.v); rider && !picked(rider))
            return NpcSquadCommandForRequester(ObjRef::Of(rider),order,requester,focus);
    // A vehicle's module keeps one point (its post / anchor): a move or an attack-move reaches it as a guard of it.
    const Command c=e.owner==Owner::squad ? order : Command{mapcmd::VehicleCommandOf(order.order),{order.at[0],order.at[1],order.at[2]}};
    const auto done=[](bool ok){return ok ? NpcCommandResult{NpcCommandReason::none,1} : NpcCommandResult{NpcCommandReason::failed,0};};
    switch(e.owner) {
    case Owner::heli: return done(HeliCommand(e.u.v,c,focus));
    case Owner::jet: return done(JetCommand(e.u.v,c,focus));
    case Owner::ground: return done(GroundCommand(e.u.v,c));
    case Owner::squad: return NpcSquadCommandForRequester(ObjRef::Of(e.u.v),c,requester,focus);
    case Owner::tank: return NpcMapCommandForRequester(ObjRef::Of(e.u.v),c,requester,focus);
    }
    return NpcCommandResult{NpcCommandReason::failed,0};
}
bool Give(Game& g,const Entry& e,const Command& order) noexcept {
    const auto result=GiveUnit(e,order,g.requester,g.focus,g.list,g.count,&g.sel);
    if(!result.Accepted())g.failure=result.reason;
    else g.affected+=result.affected;
    return result.Accepted();
}

// The ground point along the ray from `eye` along unit `dir`: the first floor the map ray meets (a cave's roof seen from
// above passed through: map_floor.h), else the level plane at `level`.
bool GroundAlong(const float* eye,const float* dir,float level,float* point) noexcept {
    const float end[3]={eye[0]+dir[0]*kPointFar,eye[1]+dir[1]*kPointFar,eye[2]+dir[2]*kPointFar};
    float hit[3];
    if(MapFloorRay(eye,end,hit)>=0.0f && std::isfinite(hit[0]+hit[1]+hit[2])){std::memcpy(point,hit,12);return true;}
    return mapcmd::RayLevel(eye,dir,level,point);
}
// `placeKeys`: the 「摆放」 panel can be used this frame (the test range, offline): only then are its keys read, so on any other map
// , . / Enter Backspace stay the game's and never count as the keyboard taking over from the pad.
Keys ReadKeys(const MapCmdInput& in,bool placeKeys) noexcept {
    Keys k{};
    if(in.front) {
        k.tab=Down(VK_TAB);k.shift=Down(VK_SHIFT);k.ctrl=Down(VK_CONTROL);
        k.follow=Down('V');k.release=Down('X');k.left=Down(VK_LBUTTON);k.right=Down(VK_RBUTTON);k.middle=Down(VK_MBUTTON);
        // The squads' orders (§6.1): keys the map's own camera does not use (WASD QE RF are its pan / turn / tilt; A and S
        // stay its pan, so RTS's A-move is Z and its hold is H). Ctrl+A: every unit (the map does not pan with Ctrl held).
        k.engage=Down('J');k.hold=Down('H');k.board=Down('B');k.dismount=Down('N');k.dismiss=Down('K');k.recruit=Down('U');k.formation=Down('T');k.sweep=Down('Y');k.health=Down('O');
        k.pace=Down('P');   // the paced march (march_pace.h): P was SPLIT's, gone with the RTS groups
        k.attackMove=Down('Z');k.all=k.ctrl && Down('A');
        k.mark=Cfg().npcMarkKey>0 && Down(Cfg().npcMarkKey);
        for(int d=0;d<9;++d)k.digit[d]=Down('1'+d);
        k.supportPrev=Down(VK_OEM_4);k.supportNext=Down(VK_OEM_6);k.supportCall=Down('C');
        if(placeKeys) {
            k.placePrev=Down(VK_OEM_COMMA);k.placeNext=Down(VK_OEM_PERIOD);k.placeCategory=Down(VK_OEM_2);
            k.placeEnter=Down(VK_RETURN);k.placeClose=Down(VK_BACK);
        }
    }
    if(in.pad) {
        const WORD b=in.buttons;
        k.padNext=(b&XINPUT_GAMEPAD_X)!=0;k.padGuard=(b&XINPUT_GAMEPAD_Y)!=0;
        k.padFollow=(b&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0;k.padRelease=(b&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0;
        k.padUp=(b&XINPUT_GAMEPAD_DPAD_UP)!=0;k.padDown=(b&XINPUT_GAMEPAD_DPAD_DOWN)!=0;
        k.padLeft=(b&XINPUT_GAMEPAD_DPAD_LEFT)!=0;k.padRight=(b&XINPUT_GAMEPAD_DPAD_RIGHT)!=0;
        k.padA=(b&XINPUT_GAMEPAD_A)!=0;k.padB=(b&XINPUT_GAMEPAD_B)!=0;
        k.padPvp=(b&XINPUT_GAMEPAD_RIGHT_THUMB)!=0;
        k.padEnemy=(b&XINPUT_GAMEPAD_LEFT_THUMB)!=0;
    }
    return k;
}

const char* OrderName(Order o) noexcept {
    switch(o) {
    case Order::guard: return "GUARD";
    case Order::follow: return "FOLLOW";
    case Order::engage: return "ENGAGE";
    case Order::focus: return "FOCUS FIRE";
    case Order::board: return "BOARD";
    case Order::dismount: return "DISMOUNT";
    case Order::dismountAll: return "DISMOUNT ALL";
    case Order::dismiss: return "DISMISS";
    case Order::recruit: return "RECRUIT";
    case Order::move: return "MOVE";
    case Order::attackMove: return "ATTACK-MOVE";
    case Order::withdraw: return "WITHDRAW";
    case Order::hold: return "HOLD";
    case Order::formation: return "FORMATION";
    default: return "RELEASE";
    }
}
const wchar_t* OrderText(Order o) noexcept {
    using hudtext::Tx;
    switch(o) {
    case Order::guard: return hudtext::Tr(Tx::orderGuard);
    case Order::follow: return hudtext::Tr(Tx::orderFollow);
    case Order::engage: return hudtext::Tr(Tx::orderEngage);
    case Order::focus: return hudtext::Tr(Tx::orderFocus);
    case Order::board: return hudtext::Tr(Tx::orderBoard);
    case Order::dismount: return hudtext::Tr(Tx::orderDismount);
    case Order::dismountAll: return hudtext::Tr(Tx::orderDismountAll);
    case Order::dismiss: return hudtext::Tr(Tx::orderDismiss);
    case Order::recruit: return hudtext::Tr(Tx::orderRecruit);
    case Order::move: return hudtext::Tr(Tx::orderMove);
    case Order::attackMove: return hudtext::Tr(Tx::orderAttackMove);
    case Order::withdraw: return hudtext::Tr(Tx::orderWithdraw);
    case Order::hold: return hudtext::Tr(Tx::orderHold);
    case Order::formation: return hudtext::Tr(Tx::btnFormationShort);
    default: return hudtext::Tr(Tx::orderRelease);
    }
}
// Whether `e` takes order `o` at all: a locked squad (a script's) none, a vehicle only the point orders / follow /
// release, a tank no follow (it keeps a post); a squad riding a vehicle no recruitment and no point order (its vehicle
// takes those), a squad recruitment is not offered for no RECRUIT (mapcmd::OffersRecruit, npcai.cpp). The card shows
// no order that would only be refused (the user, 2026-10-09: "解散解除交战集火是不是重叠了"): DISMISS for the player's
// own squads alone (recruit and dismiss never both), BOARD on foot, DISMOUNT riding.
bool Takes(const Entry& e,Order o) noexcept {
    if(e.u.locked)return false;
    if(o==Order::formation)return e.owner==Owner::squad && !e.u.joinOnly && !e.u.riding;   // a squad on foot's (T)
    if(e.owner==Owner::squad) {
        if(e.u.joinOnly)return (o==Order::recruit || o==Order::follow) && e.u.recruitable;   // the script keeps the rest
        if(o==Order::recruit)return e.u.recruitable;
        if(o==Order::dismiss)return e.u.recruited;
        if(o==Order::board)return !e.u.riding;
        if(mapcmd::DismountOrder(o))return e.u.riding;
        // Its transport (transport.cpp): WITHDRAW sends it off; aboard it the point orders take it there (it gets off short
        // of the point and carries them out on foot).
        if(o==Order::withdraw)return e.u.transport;
        if(e.u.riding)return o!=Order::follow && o!=Order::engage && o!=Order::focus && o!=Order::hold &&
                             (e.u.transport || !mapcmd::PointOrder(o));
        return true;
    }
    if(mapcmd::DismountOrder(o))return e.u.carries;   // its riding squad's (Give)
    if(e.owner==Owner::tank)return o==Order::none || o==Order::focus || mapcmd::PlaceOrder(o);
    if(e.owner==Owner::heli || e.owner==Owner::jet)return mapcmd::AirOrder(o);
    return mapcmd::VehicleOrder(o);
}
// Whether the map's box, click and Tab take `e` (mapcmd_logic.h Mark::pick): not a squad a script drives (it takes no
// order at all), not a squad riding a vehicle (its vehicle is the unit on the map; its row in the panel picks it).
bool Picked(const Entry& e) noexcept { return !e.u.locked && !(e.owner==Owner::squad && e.u.riding); }
// Why `e` is not picked, for the note (CommandFailureText).
NpcCommandReason NotPicked(const Entry& e) noexcept { return e.u.locked ? NpcCommandReason::scripted : NpcCommandReason::riding; }
// The units the box, click and Tab take (their identities, in the list's order); how many.
int PickedIds(const Game& g,const void** out) noexcept {
    int n=0;
    for(int i=0;i<g.count;++i)if(Picked(g.list[i]))out[n++]=g.list[i].u.v;
    return n;
}
const Entry* EntryOf(const Game& g,const void* id) noexcept {
    for(int i=0;i<g.count;++i)if(g.list[i].u.v==id)return &g.list[i];
    return nullptr;
}

// The log's word for the last refusal (npc_command.h NpcCommandReason).
const char* ReasonName(NpcCommandReason why) noexcept {
    static const char* const kNames[]={"none","invalid requester","not found","not a leader","not its authority","not its owner",
        "scripted","not friendly","cooling down","no target","no seat","unsupported","failed","disabled","stale",
        "boarding unavailable","no vehicle","riding","no transport","no passengers"};
    static_assert(sizeof(kNames)/sizeof(kNames[0])==static_cast<std::size_t>(NpcCommandReason::count),"a name a reason");
    const auto i=static_cast<std::size_t>(why);
    return i<sizeof(kNames)/sizeof(kNames[0]) ? kNames[i] : "?";
}
const wchar_t* CommandFailureText(NpcCommandReason why) noexcept {
    using hudtext::Tx;using hudtext::Tr;using Reason=NpcCommandReason;
    switch(why) {
    case Reason::disabled:return Tr(Tx::cmdNpcDisabled);
    case Reason::invalidRequester:return Tr(Tx::cmdNpcRequester);
    case Reason::notFound:case Reason::notLeader:case Reason::stale:return Tr(Tx::cmdNpcStale);
    case Reason::notAuthority:return Tr(Tx::cmdNpcAuthority);
    case Reason::notOwner:return Tr(Tx::cmdNpcOwner);
    case Reason::scripted:return Tr(Tx::cmdNpcScript);
    case Reason::notFriendly:return Tr(Tx::cmdNpcFriendly);
    case Reason::cooldown:return Tr(Tx::cmdNpcCooldown);
    case Reason::noTarget:return Tr(Tx::cmdNpcTarget);
    case Reason::noVehicle:return Tr(Tx::cmdNpcNoVehicle);
    case Reason::noSeat:return Tr(Tx::cmdNpcNoSeat);
    case Reason::boardingUnavailable:return Tr(Tx::cmdNpcBoardOff);
    case Reason::unsupported:return Tr(Tx::cmdNpcUnsupported);
    case Reason::riding:return Tr(Tx::cmdNpcRiding);
    case Reason::noTransport:return Tr(Tx::cmdNpcNoTransport);
    case Reason::noPassengers:return Tr(Tx::cmdNpcNoPassengers);
    default:return Tr(Tx::cmdNpcFailed);
    }
}


void Note(Game& g,const wchar_t* format,...) noexcept {
    va_list a;va_start(a,format);
    _vsnwprintf_s(g.note,_countof(g.note),_TRUNCATE,format,a);
    va_end(a);
    g.noteAt=GetTickCount64();
}

// How tall a large enemy's pin stands in the camera of `in` (hud.cpp MapPin).
float PinOf(const MapCmdInput& in) noexcept {
    const float d[3]={in.look[0]-in.eye[0],in.look[1]-in.eye[1],in.look[2]-in.eye[2]};
    const float dist=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    const float pitch=dist>1e-3f ? std::asin(mapcam::Clamp(-d[1]/dist,-1.0f,1.0f)) : mapcam::kStartPitch;
    return mapcam::PinHeight(dist,pitch);
}

// The units' marks on the screen of view `v`: on their bodies (mapcmd_logic.h BodyPoint), as hud.cpp MapCommands draws
// them.
int Marks(const Game& g,const View& v,mapcmd::Mark* out) noexcept {
    for(int i=0;i<g.count;++i) {
        const Entry& e=g.list[i];
        float p[3];mapcmd::BodyPoint(e.u.pos,e.u.air,p);
        out[i]=mapcmd::Mark{e.u.v,0.0f,0.0f,false,Picked(e)};
        out[i].on=mapcmd::Project(v.vp,p,v.w,v.h,&out[i].x,&out[i].y);
    }
    return g.count;
}

Game::UiHit UiAt(const View& v,float x,float y) noexcept {
    Game::UiHit hit;
    // The damage statistics' page (over everything while shown) or the map's STATS tab: its click code.
    if(const int code=DamageStatsUiAt(x,y)){hit.kind=Game::UiKind::stats;hit.entry=code;return hit;}
    int i=mapbtn::Hit(v.menu,v.menus,x,y);   // drawn over the rest
    if(i>=0){hit.kind=Game::UiKind::formation;hit.entry=v.menuEntry[i];return hit;}
    i=mapbtn::Hit(v.button,v.buttons,x,y);
    if(i>=0){hit.kind=Game::UiKind::command;hit.id=v.id[i];return hit;}
    i=mapbtn::Hit(v.squad,v.squads,x,y);
    if(i>=0){hit.kind=Game::UiKind::squad;hit.identity=v.squadIdentity[i];return hit;}
    if(v.folds && mapbtn::Hit(&v.fold,1,x,y)>=0){hit.kind=Game::UiKind::fold;return hit;}
    i=mapbtn::Hit(v.payload,v.payloads,x,y);
    if(i>=0){hit.kind=Game::UiKind::payload;hit.token=v.payloadToken;hit.seat=v.payloadSeat;hit.entry=v.payloadEntry[i];return hit;}
    i=mapbtn::Hit(v.compose,v.composes,x,y);
    if(i>=0){hit.kind=Game::UiKind::compose;hit.entry=v.composeCode[i];return hit;}
    i=mapbtn::Hit(v.enemy,v.enemies,x,y);
    if(i>=0){hit.kind=Game::UiKind::enemy;hit.entry=v.enemyCode[i];hit.enemy=v.enemyStatus;return hit;}
    i=mapbtn::Hit(v.pvp,v.pvps,x,y);
    if(i>=0){hit.kind=Game::UiKind::pvp;hit.entry=v.pvpCode[i];hit.pvp=v.pvpStatus;return hit;}
    i=mapbtn::Hit(v.place,v.places,x,y);
    if(i>=0){hit.kind=Game::UiKind::place;hit.entry=v.placeCode[i];return hit;}
    if(v.legend && mapbtn::Hit(&v.legendRow,1,x,y)>=0){hit.kind=Game::UiKind::legend;return hit;}
    i=mapbtn::Hit(v.support,v.supports,x,y);
    if(i>=0){hit.kind=Game::UiKind::support;hit.entry=v.supportEntry[i];return hit;}
    if(mapbtn::Hit(v.panel,v.panels,x,y)>=0)hit.kind=Game::UiKind::panel;
    return hit;
}
bool SameEnemy(const enemy_control::Snapshot& a,const enemy_control::Snapshot& b) noexcept {
    return a.revision==b.revision && a.lease==b.lease && a.request==b.request && a.ownerHumanKey==b.ownerHumanKey &&
        a.profileHash==b.profileHash && a.phase==b.phase && a.selectedAbility==b.selectedAbility &&
        std::memcmp(a.owner,b.owner,sizeof(a.owner))==0 && std::memcmp(a.target,b.target,sizeof(a.target))==0;
}
bool SameUi(const Game::UiHit& a,const Game::UiHit& b) noexcept {
    return a.kind==b.kind && a.id==b.id && a.identity.obj==b.identity.obj && a.identity.ctrl==b.identity.ctrl &&
        a.token==b.token && a.seat==b.seat && a.entry==b.entry && pvpmenu::Same(a.pvp,b.pvp) && SameEnemy(a.enemy,b.enemy);
}
// One thing armed at a time: a point order or a support call (`order` false: the support `support`, -1 nothing).
void Arm(Game& g,bool order,Order o,int support) noexcept {
    g.armedOrder=order;g.armed=order ? o : Order::none;g.armedSupport=order ? -1 : support;
    if(order || support>=0){g.formationMenu=false;g.place.armed=-1;}   // one thing armed at a time (the 「摆放」 row too)
    // A support with seats opens its composition from the load it brings uncomposed (SupportCallPreset); armed again it
    // keeps what was put together; anything else closes it.
    if(g.armedSupport!=g.composeEntry) {
        g.composeEntry=-1;g.compose=SupportLoadout{};
        if(g.armedSupport>=0 && SupportCallPreset(g.armedSupport,&g.compose))g.composeEntry=g.armedSupport;
    }
}
// The formation menu's pick (map_buttons.h MenuEntry): a defence for the selected squads on a guard point, or the march
// of the player's squads; the menu closed.
int Dispatch(Game& g,const Command& cmd,int* skipped,DispatchFlags flags) noexcept;
void FormationNote(Game& g,int given,int shape) noexcept;
void PickFormation(Game& g,int entry) noexcept {
    using hudtext::Tr;using hudtext::Tx;
    g.formationMenu=false;
    const int shape=mapbtn::MenuShape(entry);
    if(!Cfg().enabled){Note(g,L"%ls",Tr(Tx::cmdOfflineOnly));return;}
    // The order goes to every selected squad (online their authority's: command protocol v7); a guarding one takes the
    // defence, one following the player the march.
    const Command c{Order::formation,{0.0f,0.0f,0.0f},mapbtn::MenuGuard(entry) ? mapcmd::FormationParam(shape,-1) : mapcmd::FormationParam(-1,shape)};
    int skipped=0;
    const int given=Dispatch(g,c,&skipped,{});
    // Sent to another machine's squads: this machine's own march follows the shape asked (the next T steps from it).
    if(g.networkQueued && !mapbtn::MenuGuard(entry))SetMarchFormation(shape);
    FormationNote(g,given,shape);
    Log("MAPCMD formation menu: %s %s, %d given, %d not",mapbtn::MenuGuard(entry) ? "defence" : "march",
        npc::formation::Name(npc::formation::FromInt(shape)),given,skipped);
}
void CycleEnemy(Game& g,int direction) noexcept;
void UiClick(Game& g,const Game::UiHit& hit,bool shift) noexcept {
    if(hit.kind==Game::UiKind::enemy) {
        if(hit.entry==enemyui::kFold){g.enemyOpen=!g.enemyOpen;return;}
        if(hit.entry==enemyui::kTargetPrev || hit.entry==enemyui::kTargetNext){CycleEnemy(g,hit.entry==enemyui::kTargetPrev ? -1 : 1);return;}
        const auto& snapshot=hit.enemy;
        bool accepted=true;
        if(snapshot.ownerHumanKey!=reinterpret_cast<std::uintptr_t>(g.requester.obj))accepted=false;
        else if(hit.entry==enemyui::kBegin)accepted=EnemyControlRequest(snapshot);
        else if(hit.entry==enemyui::kExit)accepted=EnemyControlExit(snapshot);
        else if(hit.entry==enemyui::kPrev || hit.entry==enemyui::kNext) {
            const enemyui::Model model{{},true,snapshot.phase==enemy_control::Phase::active,static_cast<int>(snapshot.abilityCount)};
            enemyui::Page(g.enemyMenu,model,hit.entry==enemyui::kPrev ? -1 : 1);
        }   // an attack row acts while it is held (MapCommandFrame: EnemyControlHoldAbility), not on release
        if(!accepted)Note(g,L"%ls",hudtext::Tr(hudtext::Tx::enemyControlStale));
        return;
    }
    if(hit.kind==Game::UiKind::stats){DamageStatsClick(hit.entry);return;}
    if(hit.kind==Game::UiKind::legend){legendOpen.store(!legendOpen.load());return;}
    if(hit.kind==Game::UiKind::pvp && hit.entry==kPvpFold){pvpOpen.store(!pvpOpen.load());return;}
    if(hit.kind==Game::UiKind::pvp) {
        const auto& snapshot=hit.pvp;
        bool accepted=false;
        if(pvpmenu::Allowed(snapshot,hit.entry)) {
            if(hit.entry==kPvpModeToggle)accepted=SetPvpEnabledAt(!snapshot.enabled,snapshot.epoch,snapshot.revision,snapshot.localPuid);
            else accepted=SetPvpLocalTeamAt(hit.entry,snapshot.epoch,snapshot.revision,snapshot.localPuid);
        }
        Note(g,L"%ls",hudtext::Tr(accepted ? hudtext::Tx::pvpPending : hudtext::Tx::pvpChanged));
        return;
    }
    if(hit.kind==Game::UiKind::place) {   // the 「摆放」 panel: taken by MapCommandFrame's one Step this frame
        auto& k=g.placePress;
        if(hit.entry==kPlaceToggle)k.toggle=true;
        else if(hit.entry==kPlacePageUp)k.page=-1;
        else if(hit.entry==kPlacePageDown)k.page=1;
        else if(hit.entry>=kPlaceTab && hit.entry<kPlaceTab+debugspawn::kCategoryCount)k.tab=hit.entry-kPlaceTab;
        else if(hit.entry>=0 && hit.entry<debugspawn::kEntryCount)k.row=hit.entry;
        return;
    }
    if(hit.kind==Game::UiKind::command){g.button=hit.id;return;}
    if(hit.kind==Game::UiKind::fold){g.panelOpen=!g.panelOpen;return;}
    if(hit.kind==Game::UiKind::formation){PickFormation(g,hit.entry);return;}
    if(hit.kind==Game::UiKind::compose) {   // a kind: one more aboard; a taken seat: emptied
        if(g.composeEntry>=0 && !mapbtn::ComposeApply(g.compose,SupportCallSeats(g.composeEntry),hit.entry) && hit.entry>=0)
            Note(g,L"%ls",hudtext::Tr(hudtext::Tx::supportLoadoutTooMany));
        return;
    }
    if(hit.kind==Game::UiKind::support) {   // its row or chip: armed (again: disarmed); one the mission forbids: why
        const bool again=g.armedSupport==hit.entry;
        if(const wchar_t* why=again ? nullptr : SupportCallForbidden(hit.entry)){Note(g,L"%ls",why);return;}
        Arm(g,false,Order::none,again ? -1 : hit.entry);
        if(!again){g.supportPick=hit.entry;Note(g,hudtext::Tr(hudtext::Tx::cmdSupportArmed),SupportCallName(hit.entry));}
        return;
    }
    if(hit.kind==Game::UiKind::payload) {
        const bool queued=RequestPayloadSelection(hit.token,hit.seat,hit.entry);
        Note(g,L"%ls",hudtext::Tr(queued ? hudtext::Tx::cmdPayloadQueued : hudtext::Tx::cmdPayloadStale));return;
    }
    if(hit.kind!=Game::UiKind::squad)return;
    // Resolve the original rendered identity in this frame's validated list. Never recapture from
    // its raw address or reinterpret a row number after sort/recruitment changes the next snapshot.
    for(int i=0;i<g.count;++i)if(g.list[i].owner==Owner::squad && g.list[i].u.v==hit.identity.obj) {
        if(!Readable(hit.identity.obj,kSelfCtrl+sizeof(void*)) || !hit.identity.Is(hit.identity.obj))return;
        // A script's squad takes no order: its row says why instead of selecting it (a riding one is picked here).
        if(g.list[i].u.locked){Note(g,L"%ls",CommandFailureText(NpcCommandReason::scripted));return;}
        if(!shift)g.sel.Clear();
        if(shift && g.sel.Has(hit.identity.obj))g.sel.Remove(hit.identity.obj);else g.sel.Add(hit.identity.obj);
        g.rowPicked=g.sel.n==1;return;
    }
}

// UI owns presses that began on a rendered panel, including its inert background. Capture remains
// until release outside the panel; a changed identity/token between press and release cancels action.
bool EnemyControlling(const Game& g) noexcept;
bool PickEnemyAt(Game& g,const MapCmdInput& in,const View& v,float x,float y,float radius) noexcept;
void Pointer(Game& g,const MapCmdInput& in,const Keys& k,const View* v) noexcept {
    if(!in.front) {
        g.boxing=g.pressing=g.uiLeft=g.uiRight=g.rpressing=false;g.uiPress={};
        g.suppressLeft=g.suppressRight=true;pointerCaptured.store(false);return;
    }
    if(!k.left)g.suppressLeft=false;
    if(!k.right)g.suppressRight=false;
    if(!v) {
        g.boxing=g.pressing=g.uiLeft=g.uiRight=g.rpressing=false;g.uiPress={};
        g.suppressLeft=g.suppressLeft || k.left;g.suppressRight=g.suppressRight || k.right;
        pointerCaptured.store((g.suppressLeft && k.left) || (g.suppressRight && k.right));return;
    }
    if(mapcmd::FitPointer(g.pointer,v->w,v->h)) {
        g.boxing=g.pressing=false;
        if(g.uiLeft)g.uiPress=Game::UiHit{Game::UiKind::panel};
    }
    const float s=v->h/1080.0f;
    if(in.mouse && (g.uiLeft || g.uiRight || g.boxing || (!k.left && !k.right && !k.middle))) {
        g.pointer.x=mapcam::Clamp(g.pointer.x+in.dx*kPointerGain*s,0.0f,v->w-1.0f);
        g.pointer.y=mapcam::Clamp(g.pointer.y+in.dy*kPointerGain*s,0.0f,v->h-1.0f);
    }
    if(in.mouse && (g.pressing || g.uiLeft))g.moved+=std::fabs(in.dx)+std::fabs(in.dy);
    if(in.mouse && g.rpressing)g.rmoved+=std::fabs(in.dx)+std::fabs(in.dy);
    if(k.right && !g.was.right && !g.suppressRight) {
        g.uiRight=UiAt(*v,g.pointer.x,g.pointer.y).kind!=Game::UiKind::none;
        g.rpressing=true;g.rOnUi=g.uiRight;g.rmoved=0.0f;   // a click unless it turns the map (a drag)
    }
    if(!k.right && g.was.right && g.rpressing){g.rightClick=g.rmoved<kClickMove;g.rightOnUi=g.rOnUi;g.rpressing=false;}
    if(!k.right)g.uiRight=false;
    // RTS (the user, 2026-10-19): a left drag on the map is the selection box whenever there is a unit to box (Ctrl
    // too, as before); with none, or with a point armed (its click), it pans the map as it did (map.cpp Steer; the
    // middle button's drag pans always).
    const bool armed=g.armedOrder || g.armedSupport>=0 || g.place.armed>=0;
    bool boxes=false;
    for(int i=0;i<g.count && !boxes;++i)boxes=Picked(g.list[i]);
    if(k.left && !g.was.left && !g.suppressLeft) {
        g.uiPress=UiAt(*v,g.pointer.x,g.pointer.y);g.uiLeft=g.uiPress.kind!=Game::UiKind::none;g.moved=0;
        if(g.uiLeft)g.boxing=g.pressing=false;
        else if(k.ctrl || (boxes && !armed)){g.boxing=true;g.bx=g.pointer.x;g.by=g.pointer.y;}
        else g.pressing=true;
    }
    pointerCaptured.store(g.uiLeft || g.uiRight || (g.suppressLeft && k.left) || (g.suppressRight && k.right));
    if(k.left || !g.was.left)return;
    if(g.uiLeft) {
        if(g.moved<kClickMove && SameUi(g.uiPress,UiAt(*v,g.pointer.x,g.pointer.y)))UiClick(g,g.uiPress,k.shift);
        g.uiLeft=false;g.uiPress={};g.boxing=g.pressing=false;pointerCaptured.store(g.uiRight || (g.suppressRight && k.right));return;
    }
    mapcmd::Mark marks[kCmdUnits];const int n=Marks(g,*v,marks);
    const bool box=g.boxing && (std::fabs(g.pointer.x-g.bx)>=kClickBox*s || std::fabs(g.pointer.y-g.by)>=kClickBox*s);
    const bool click=(g.boxing && !box) || (g.pressing && g.moved<kClickMove);
    if(click && UiAt(*v,g.pointer.x,g.pointer.y).kind!=Game::UiKind::none){} // release over UI never selects what is underneath
    else if(click && armed && !g.boxing)g.armedClick=true;
    else if(box)mapcmd::Box(g.sel,marks,n,g.bx,g.by,g.pointer.x,g.pointer.y,k.shift);
    else if(click && Cfg().enemyControl && !EnemyControlling(g) && mapcmd::Nearest(marks,n,g.pointer.x,g.pointer.y,kClickRadius*s)<0 &&
            PickEnemyAt(g,in,*v,g.pointer.x,g.pointer.y,kClickRadius*s)) {
        // A left click on an enemy (no unit of ours under it) picks it as the enemy-control
        // target; the NPC selection stays as it was.
        Note(g,L"%ls",hudtext::Tr(hudtext::Tx::enemyControlPicked));
    }
    else if(click) {
        const void* id=mapcmd::Click(g.sel,marks,n,g.pointer.x,g.pointer.y,kClickRadius*s,k.shift);
        const Entry* hit=EntryOf(g,id);
        if(hit && !Picked(*hit))Note(g,L"%ls",CommandFailureText(NotPicked(*hit)));
        else if(hit) {
            // A second click on the same unit within the double-click time, or Ctrl+click: every unit of its kind on the
            // screen (RTS; Shift adds them).
            const ULONGLONG at=GetTickCount64();
            const bool twice=g.clickUnit==id && at-g.clickAt<=mapcmd::kDoubleTapMs;
            if(twice || k.ctrl) {
                const char* kind[kCmdUnits];int owner[kCmdUnits];
                for(int i=0;i<n;++i){kind[i]=g.list[i].u.name;owner[i]=static_cast<int>(g.list[i].owner);}
                mapcmd::SelectKind(g.sel,marks,kind,owner,n,static_cast<int>(hit-g.list),k.shift);
            }
            g.clickUnit=twice ? nullptr : id;g.clickAt=at;
        }
    }
    g.boxing=g.pressing=false;
}

// The point G sends the selection to: under the pointer (mouse), at the screen's centre (pad, or no view yet).
bool TargetPoint(const Game& g,const MapCmdInput& in,const View* v,float* point) noexcept {
    std::memcpy(point,in.look,12);
    float eye[3],dir[3];
    if(!in.usingPad && v && mapcmd::ScreenRay(v->vp,v->w,v->h,g.pointer.x,g.pointer.y,eye,dir))return GroundAlong(eye,dir,in.look[1],point);
    float c[3]={in.look[0]-in.eye[0],in.look[1]-in.eye[1],in.look[2]-in.eye[2]};
    const float len=std::sqrt(c[0]*c[0]+c[1]*c[1]+c[2]*c[2]);
    if(!(len>1e-3f) || !std::isfinite(len))return false;
    for(float& x:c)x/=len;
    return GroundAlong(in.eye,c,in.look[1],point);
}

// The enemies the NPCs can be set on (the lockable lock points, as npcai.cpp's frame list) on the screen of view `v`: each
// lock point at its point and up its pin (a large enemy's icon stands at its pin's top, a small one's dot on it).
constexpr int kEnemyMarks=4096;   // two a lock point
struct EnemyMarks { const View* v; float pin; int n; mapcmd::Mark m[kEnemyMarks]; float at[kEnemyMarks][3]; };
EnemyMarks enemyMarks{};   // the game thread's (too big for its stack)
void SeeEnemyMark(void* ctx,const void* object,const float* aim) {
    auto& e=*static_cast<EnemyMarks*>(ctx);
    const float up[3]={aim[0],aim[1]+e.pin,aim[2]};
    for(const float* p:{aim,up}) {
        if(e.n>=kEnemyMarks)return;
        mapcmd::Mark& m=e.m[e.n];
        m=mapcmd::Mark{object,0.0f,0.0f,false};
        m.on=mapcmd::Project(e.v->vp,p,e.v->w,e.v->h,&m.x,&m.y);
        std::memcpy(e.at[e.n++],aim,12);
    }
}
// The enemy under the pointer (with a pad: the screen's centre), within the click's radius of one of its marks.
void Hover(Game& g,const MapCmdInput& in,const View* v) noexcept {
    npcmark::Assign(g.hover,{});
    if(!v || !npcmark::MarkOn())return;
    if(!in.usingPad && UiAt(*v,g.pointer.x,g.pointer.y).kind!=Game::UiKind::none)return;
    EnemyMarks& e=enemyMarks;
    e.v=v;e.pin=PinOf(in);e.n=0;
    VisitEnemiesOf(player.team,&SeeEnemyMark,&e);
    const float x=in.usingPad ? v->w*0.5f : g.pointer.x,y=in.usingPad ? v->h*0.5f : g.pointer.y;
    const int i=mapcmd::Nearest(e.m,e.n,x,y,kClickRadius*v->h/1080.0f);
    if(i<0)return;
    npcmark::Assign(g.hover,npcmark::Capture(e.m[i].id));
    std::memcpy(g.hoverAt,e.at[i],12);
}

// --- Picking the enemy-control target (the map owner's hostile registry, independent of the NPC mark feature) ---
bool RequesterTeam(const Game& g,std::int32_t* team) noexcept {
    __try {
        if(!Readable(g.requester.obj,kTeam+sizeof(std::int32_t)) || !g.requester.Is(g.requester.obj))return false;
        *team=At<std::int32_t>(g.requester.obj,kTeam);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ReadPosition(const void* object,float* out) noexcept {
    __try {
        if(!Readable(object,kPosition+12))return false;
        std::memcpy(out,static_cast<const unsigned char*>(object)+kPosition,12);
        return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlling(const Game& g) noexcept {
    using Phase=enemy_control::Phase;const auto phase=g.enemy.snapshot.phase;
    return phase==Phase::active || phase==Phase::pending || phase==Phase::restoring;
}
void SetEnemyCandidate(Game& g,const void* object,const float* aim) noexcept {
    npcmark::Assign(g.enemyCandidate,npcmark::Capture(object));
    float position[3];
    if(g.enemyCandidate && ReadPosition(object,position))for(int i=0;i<3;++i)g.enemyAimOffset[i]=aim[i]-position[i];
    else for(float& x:g.enemyAimOffset)x=0.0f;
    // Picked, not unfolded (the user, 2026-10-19: 「这个展开和收起会自动变，我不好点」): the card is folded and unfolded by
    // its title alone (and L3 into it); a pick only marks the enemy and starts its card's list from the top.
    if(g.enemyCandidate)g.enemyMenu.first=g.enemyMenu.row=0;
}
// The enemy whose map mark is nearest (x, y) within `radius` px: one enemy control admits
// before any other (a nest, a part, a fortress), so a mark overlapping its spawner still
// takes the monster; an inadmissible one alone is still taken, for its card to say why.
bool PickEnemyAt(Game& g,const MapCmdInput& in,const View& v,float x,float y,float radius) noexcept {
    std::int32_t team=0;
    if(!RequesterTeam(g,&team))return false;
    EnemyMarks& marks=enemyMarks;marks.v=&v;marks.pin=PinOf(in);marks.n=0;
    VisitEnemiesOf(team,&SeeEnemyMark,&marks);
    int best=-1,any=-1;float bestD2=radius*radius,anyD2=radius*radius;
    for(int i=0;i<marks.n;++i) {
        const mapcmd::Mark& m=marks.m[i];
        if(!m.on)continue;
        const float dx=m.x-x,dy=m.y-y,d2=dx*dx+dy*dy;
        if(d2<=anyD2){anyD2=d2;any=i;}
        if(d2<=bestD2 && EnemyControlPickable(m.id)){bestD2=d2;best=i;}
    }
    const int pick=best>=0 ? best : any;
    if(pick<0)return false;
    SetEnemyCandidate(g,marks.m[pick].id,marks.at[pick]);
    return static_cast<bool>(g.enemyCandidate);
}
// Every controllable enemy once (an object has several lock points), nearest the requester first.
constexpr int kEnemyPicks=1024;
struct EnemyPicks { float from[3];int n;enemyui::TargetSlot slot[kEnemyPicks]; };
EnemyPicks enemyPicks{};   // the game thread's (too big for its stack)
void SeeEnemyPick(void* ctx,const void* object,const float* aim) {
    auto& e=*static_cast<EnemyPicks*>(ctx);
    if(e.n>=kEnemyPicks)return;   // the family probe runs after the walk, outside the team manager's lock
    for(int i=0;i<e.n;++i)if(e.slot[i].id==object)return;
    const float dx=aim[0]-e.from[0],dy=aim[1]-e.from[1],dz=aim[2]-e.from[2];
    auto& slot=e.slot[e.n++];slot.id=object;slot.d2=dx*dx+dy*dy+dz*dz;std::memcpy(slot.at,aim,12);
}
void CycleEnemy(Game& g,int direction) noexcept {
    using hudtext::Tr;using hudtext::Tx;
    std::int32_t team=0;EnemyPicks& e=enemyPicks;e.n=0;
    if(EnemyControlling(g) || !RequesterTeam(g,&team) || !ReadPosition(g.requester.obj,e.from))return;
    VisitEnemiesOf(team,&SeeEnemyPick,&e);
    int kept=0;
    for(int i=0;i<e.n;++i)if(EnemyControlPickable(e.slot[i].id))e.slot[kept++]=e.slot[i];
    e.n=kept;
    const int i=enemyui::CycleTarget(e.slot,e.n,g.enemyCandidate.obj,direction);
    if(i<0){Note(g,L"%ls",Tr(Tx::enemyControlNoTarget));return;}
    SetEnemyCandidate(g,e.slot[i].id,e.slot[i].at);
}
void EnemyControlCard(Game& g) noexcept {
    using Phase=enemy_control::Phase;
    enemy_control::Snapshot snapshot{};
    const bool controlling=EnemyControlReadSnapshot(g.requester,&snapshot) &&
        (snapshot.phase==Phase::active || snapshot.phase==Phase::pending || snapshot.phase==Phase::restoring);
    g.enemy.marked=false;
    if(!controlling) {
        // A target that died or left is let go: the card says how to pick another, no stale reason.
        if(g.enemyCandidate && !npcmark::Alive(g.enemyCandidate))npcmark::Assign(g.enemyCandidate,{});
        float position[3];
        if(g.enemyCandidate && ReadPosition(g.enemyCandidate.obj,position)) {
            for(int i=0;i<3;++i)g.enemy.markAt[i]=position[i]+g.enemyAimOffset[i];
            g.enemy.marked=true;
        }
        EnemyControlDescribe(g.requester,g.enemyCandidate,&snapshot);
    }
    g.enemy.visible=Cfg().enemyControl;
    g.enemy.expanded=g.enemyOpen;
    g.enemy.snapshot=snapshot;
    const int total=static_cast<int>((std::min)(snapshot.abilityCount,static_cast<std::uint32_t>(0x7FFFFFFF)));
    if(g.enemyMenu.first<0 || g.enemyMenu.first>=total)g.enemyMenu.first=0;
    g.enemy.first=g.enemyMenu.first;
    g.enemy.count=static_cast<int>(EnemyControlAbilities(snapshot,static_cast<unsigned>(g.enemy.first),g.enemy.ability,enemyui::kPageRows));
    g.enemy.focused=g.enemyMenu.focused;g.enemy.padRow=g.enemyMenu.focused ? g.enemyMenu.row : -1;
}
enemyui::Model EnemyModel(const enemy_control::Snapshot& s,bool visible) noexcept {
    // Menu identity is the same immutable lease/revision checked again by the core.
    std::uint32_t target=2166136261u;
    for(unsigned char byte:s.target)target=(target^byte)*16777619u;
    return {{s.revision,static_cast<std::uint64_t>(s.ownerHumanKey),s.lease,target,s.profileHash,s.request},
        visible,s.phase==enemy_control::Phase::active,static_cast<int>((std::min)(s.abilityCount,std::uint32_t{0x7FFFFFFF})),
        s.phase==enemy_control::Phase::pending || s.phase==enemy_control::Phase::restoring};
}

// The units that take an order now (`ids`: their identities), the selection kept to those of them selected before.
void Refresh(Game& g,const void** ids) noexcept {
    List(g);
    for(int i=0;i<g.count;++i)ids[i]=g.list[i].u.v;
    KeepSelection(g,ids);
}
void CommandNetworkReply(Game& g) noexcept {
    if(!g.networkRequest)return;
    CommandNetworkResult result;
    if(!ReadMapCommandNetworkResult(&result) || result.request!=g.networkRequest || result.state==CommandNetworkState::pending)return;
    using hudtext::Tx;using hudtext::Tr;
    if(result.state==CommandNetworkState::completed) {
        unsigned accepted=0,affected=0;NpcCommandReason failure=NpcCommandReason::none;
        for(unsigned i=0;i<result.count && i<kCommandNetUnits;++i) {
            if(result.units[i].Accepted()){++accepted;affected+=result.units[i].affected;}
            else if(failure==NpcCommandReason::none)failure=result.units[i].reason;
        }
        if(!accepted)Note(g,L"%ls",CommandFailureText(failure));
        else Note(g,Tr(Tx::cmdNpcNetworkDone),OrderText(g.networkOrder),accepted,result.count,affected,
            failure==NpcCommandReason::none ? L"" : CommandFailureText(failure));
    } else Note(g,L"%ls",Tr(result.state==CommandNetworkState::timedOut ? Tx::cmdNpcNetworkTimeout :
        result.state==CommandNetworkState::interrupted || result.state==CommandNetworkState::stale ? Tx::cmdNpcNetworkInterrupted : Tx::cmdNpcNetworkUnavailable));
    g.networkRequest=0;
}

// How near (m, level) a unit is to its point to be there for its queue (mapcmd_logic.h Advance): a squad within its
// guard's radius of it (npcai.cpp turns its move into a guard nearer than that), a heli on its orbit, a jet circling it.
float Reach(const Entry& e) noexcept {
    switch(e.owner) {
    case Owner::heli: return std::fmax(40.0f,Cfg().heliGuardRadius+40.0f);
    case Owner::jet: return 600.0f;
    case Owner::squad: return Cfg().npcGuardRadius+10.0f;
    default: return 30.0f;
    }
}

// --- The paced march (march_pace.h), the authority's ---
// The units this machine is the authority for that took one move / attack-move with the pace on, by march (`group`):
// each one's way and its share of its own full speed now (MapPaceScale, read by the modules that drive them). A unit
// leaves its march at its point (Reach), on another order (its order now not this point), or dead; a jet does not
// march (it cannot fly slower than it does). Game thread.
struct PacedUnit { ObjRef id; std::uint32_t group; float at[3]; pace::Member m; float scale; };
PacedUnit paced[kCmdUnits]{};
std::uint32_t paceGroups=0;

float Across(const float* a,const float* b) noexcept { return std::hypot(b[0]-a[0],b[2]-a[2]); }
static_assert(pace::kShares==kCmdUnits,"a share for every unit that can march");
// The shares the driving modules read (march_pace.h MapPaceScale): the marching units', no one else's.
void PublishShares() noexcept {
    for(int i=0;i<kCmdUnits;++i)
        pace::shares[i]=paced[i].m.active ? pace::Share{paced[i].id.obj,paced[i].id.ctrl,paced[i].scale} : pace::Share{};
}
void Unpace(const ObjRef& id) noexcept {
    for(auto& p:paced)if(p.m.active && p.id.obj==id.obj && p.id.ctrl==id.ctrl)p=PacedUnit{};
    PublishShares();
}
// Unit `e` given `c` (accepted, given now): in march `group` (0 none) from here, out of any it was in.
void Pace(const Entry& e,const Command& c,std::uint32_t group) noexcept {
    Unpace(e.id);
    if(!group || e.owner==Owner::jet || (c.order!=Order::move && c.order!=Order::attackMove))return;
    for(auto& p:paced)if(!p.m.active) {
        p=PacedUnit{e.id,group,{c.at[0],c.at[1],c.at[2]},{},1.0f};
        pace::Begin(p.m,Across(e.u.pos,c.at),GameMs());
        return;
    }
}
// Every march on: each unit's way left from where it is now, and its share of full speed.
void PaceTick(const Entry* list,int count) noexcept {
    const ULONGLONG now=GameMs();
    bool any=false;
    for(auto& p:paced) {
        if(!p.m.active)continue;
        const Entry* e=nullptr;
        for(int i=0;i<count && !e;++i)if(list[i].id.obj==p.id.obj && list[i].id.ctrl==p.id.ctrl)e=&list[i];
        if(!e)continue;   // out of the listing a frame (between drivers): kept, its way as it was
        const float left=Across(e->u.pos,p.at);
        // Its order now: still this point (a squad's move turned into a guard of it there: mapcmd_logic.h Arrive)?
        if(!mapcmd::PointOrder(e->u.now.order) || Across(e->u.now.at,p.at)>2.0f || left<=Reach(*e)){p=PacedUnit{};continue;}
        pace::Update(p.m,left,now);
        any=true;
    }
    if(!any){PublishShares();return;}
    for(auto& p:paced)if(p.m.active && p.group) {
        bool first=true;   // each march once: at its first unit in the table
        for(const auto& o:paced){if(&o==&p)break;if(o.m.active && o.group==p.group){first=false;break;}}
        if(!first)continue;
        pace::Member m[kCmdUnits];int idx[kCmdUnits];int n=0;
        for(int i=0;i<kCmdUnits;++i)if(paced[i].m.active && paced[i].group==p.group){m[n]=paced[i].m;idx[n++]=i;}
        float scale[kCmdUnits];
        pace::Scales(m,n,now,scale);
        for(int i=0;i<n;++i)paced[idx[i]].scale=scale[i];
    }
    PublishShares();
}
}  // namespace

std::uint32_t MapPaceGroup() noexcept { if(!++paceGroups)++paceGroups;return paceGroups; }

// --- The Shift queues: the authority's (mapcmd_logic.h Waypoints) ---
// Kept where the unit's AI runs, whoever gave the points: this machine's map for its own units, a client's through the
// network (command protocol v7's queued flag, command_net.cpp Execute). By the unit's UnitRef (a squad goes on under its
// next top), the player who gave them. Advanced by the scene's tick (MapCommandQueueTick), dropped with the mission.
namespace {
struct UnitQueue { mapcmd::UnitRef unit; ObjRef requester; mapcmd::Waypoints w; };
UnitQueue unitQueues[kCmdUnits]{};
Entry authorityList[kCmdUnits];   // the authority's own listing (a network request, the tick): never the map's Game

UnitQueue* QueueFor(const mapcmd::UnitKey& top,bool make) noexcept {
    if(!top.obj)return nullptr;
    UnitQueue* free=nullptr;
    for(auto& q:unitQueues) {
        if(q.w.active && mapcmd::SameKey(q.unit.top,top))return &q;
        if(!free && !q.w.active)free=&q;
    }
    if(!make || !free)return nullptr;
    *free=UnitQueue{};
    return free;
}
const UnitQueue* QueueOfListed(const ObjRef& id) noexcept {
    for(const auto& q:unitQueues)if(q.w.active && mapcmd::SameKey(q.unit.top,Key(id)))return &q;
    return nullptr;
}

// The order `c` to unit `e` (this machine its authority), from `requester`, through `give` (the module's own way:
// GiveUnit). `queue` (Shift, a point order): after the points it is on its way to (mapcmd_logic.h Enqueue: stored,
// `*stored` true, nothing given now), else given; without it the unit's queue is gone (Replace: nothing registered).
// A refused order drops its queue. The one way the map's Issue, the queue's tick and a network request reach a unit.
template<class GiveFn> NpcCommandResult Authority(const Entry& e,const Command& c,const ObjRef& requester,bool queue,
                                                  bool* stored,GiveFn give) noexcept {
    *stored=false;
    const mapcmd::UnitKey top=Key(e.id);
    UnitQueue* q=QueueFor(top,queue && mapcmd::PointOrder(c.order));
    if(queue && mapcmd::PointOrder(c.order) && !q) {   // no slot left (one a listed unit: not expected): refused, not given at once
        Log("MAPCMD queue table full: a queued point for %p refused",e.u.v);
        return NpcCommandResult{NpcCommandReason::failed,0};
    }
    if(queue && mapcmd::PointOrder(c.order)) {
        if(!q->w.active)q->unit=Capture(e);
        q->requester=requester;
        if(!mapcmd::Enqueue(q->w,e.u.now,e.u.pos,Reach(e),c)){*stored=true;return NpcCommandResult{NpcCommandReason::none,1};}
    } else if(q)mapcmd::Replace(q->w,c);
    const NpcCommandResult result=give();
    if(!result.Accepted())if(UnitQueue* drop=QueueFor(top,false))drop->w=mapcmd::Waypoints{};
    return result;
}

// The command to every selected unit (Dispatch); a point order's formation round the point (helis sharing one orbit stay
// on it). `queue` (Shift): a move / attack-move goes after the points a unit is on its way to (Authority); without it
// every unit's queue is gone. This machine's units are given it here (Authority), the others' authority asked for it in
// one request (command_net SubmitMapCommand, the same flag).
int Dispatch(Game& g,const Command& cmd,int* skipped,DispatchFlags flags) noexcept {
    const bool queue=flags.queue && mapcmd::PointOrder(cmd.order);
    const bool marching=flags.paced && !queue && (cmd.order==Order::move || cmd.order==Order::attackMove);
    const std::uint32_t group=marching ? MapPaceGroup() : 0;
    int k=0;
    g.failure=NpcCommandReason::none;g.affected=0;
    g.skipScript=g.skipRiding=g.skipCannot=g.skipRemote=g.skipFailed=0;
    g.networkQueued=0;g.networkMessage[0]=0;
    *skipped=0;
    for(int i=0;i<g.count;++i)k+=g.sel.Has(g.list[i].u.v) && Takes(g.list[i],cmd.order) ? 1 : 0;
    ObjRef remote[kCommandNetUnits];std::uint32_t remoteSlots[kCommandNetUnits]{};unsigned remotes=0;
    const bool share=HeliSharesPost();
    std::uint32_t slots[kCmdUnits]{};std::uint32_t slot=0;
    for(int i=0;i<g.count;++i)if(g.sel.Has(g.list[i].u.v) && Takes(g.list[i],cmd.order)) {
        slots[i]=slot;
        if(!(g.list[i].owner==Owner::heli && share))++slot;
    }
    if(InSession())for(int i=0;i<g.count;++i) {
        const auto& e=g.list[i];
        if(g.sel.Has(e.u.v) && (e.owner==Owner::squad || e.owner==Owner::tank) && Takes(e,cmd.order) && !IsOnlineAuthority(e.u.v)) {
            if(remotes<kCommandNetUnits){remote[remotes]=ObjRef::Of(e.u.v);remoteSlots[remotes]=slots[i];}++remotes;
        }
    }
    if(remotes>kCommandNetUnits) {
        *skipped=k;g.failure=NpcCommandReason::unsupported;
        _snwprintf_s(g.networkMessage,_countof(g.networkMessage),_TRUNCATE,L"%ls",hudtext::Tr(hudtext::Tx::cmdNpcNetworkLimit));return 0;
    }
    if(remotes) {
        const auto request=SubmitMapCommand(g.requester,remote,remotes,cmd,g.focus,g.networkMessage,_countof(g.networkMessage),
            mapcmd::PointOrder(cmd.order) ? remoteSlots : nullptr,mapcmd::PointOrder(cmd.order) ? static_cast<std::uint32_t>(k) : 0,queue);
        if(request){g.networkRequest=request;g.networkQueued=remotes;g.networkOrder=cmd.order;}
    }
    int given=0;
    for(int i=0;i<g.count;++i) {
        Entry& e=g.list[i];
        if(!g.sel.Has(e.u.v))continue;
        if(!Takes(e,cmd.order)) {
            const bool script=e.u.locked || (e.owner==Owner::squad && e.u.joinOnly);
            g.failure=script ? NpcCommandReason::scripted : e.u.riding ? NpcCommandReason::riding : NpcCommandReason::unsupported;
            ++(script ? g.skipScript : e.u.riding ? g.skipRiding : g.skipCannot);
            ++*skipped;continue;
        }
        if(!IsOnlineAuthority(e.u.v)) {
            if((e.owner==Owner::squad || e.owner==Owner::tank) && g.networkQueued)continue;
            g.failure=NpcCommandReason::notAuthority;++g.skipRemote;++*skipped;continue;
        }
        Command c=cmd;
        if(mapcmd::PointOrder(cmd.order) && !(e.owner==Owner::heli && share)) {
            mapcmd::Formation(static_cast<int>(slots[i]),k,cmd.at,mapcmd::kFormationSpacing,c.at);
            float ground;
            if(!MapGroundNear(c.at[0],c.at[2],cmd.at[1],&ground,true) || !std::isfinite(ground)) {
                g.failure=NpcCommandReason::noTarget;++g.skipFailed;++*skipped;continue;
            }
            c.at[1]=ground;
        }
        if(cmd.order==Order::hold) {   // its own spot (on the ground under it): the module's guard post / the squad's point
            float ground;
            if(!MapGroundNear(e.u.pos[0],e.u.pos[2],e.u.pos[1],&ground,true) || !std::isfinite(ground)) {
                g.failure=NpcCommandReason::noTarget;++g.skipFailed;++*skipped;continue;
            }
            c.at[0]=e.u.pos[0];c.at[1]=ground;c.at[2]=e.u.pos[2];
        }
        bool stored=false;
        const auto result=Authority(e,c,g.requester,queue,&stored,[&]{
            return GiveUnit(e,c,g.requester,g.focus,g.list,g.count,&g.sel);
        });
        if(stored){++given;continue;}
        if(!result.Accepted()) {
            g.failure=result.reason==NpcCommandReason::none ? NpcCommandReason::failed : result.reason;
            ++g.skipFailed;++*skipped;continue;
        }
        g.affected+=result.affected;
        e.u.now=c;
        Pace(e,c,group);
        ++given;
    }
    return given;
}


// The formation order's word (T, the menu): the shape this machine's squads have now (a guard's defence, else the
// march), the request's when another machine's are waiting for their authority.
void FormationNote(Game& g,int given,int shape) noexcept {
    using hudtext::Tr;using hudtext::Tx;
    if(g.networkQueued){Note(g,Tr(Tx::cmdNpcNetworkQueued),OrderText(Order::formation),g.networkQueued,given);return;}
    if(!given){Note(g,L"%ls",g.networkMessage[0] ? g.networkMessage : g.failure!=NpcCommandReason::none ?
                              CommandFailureText(g.failure) : Tr(Tx::cmdNoUnit));return;}
    for(int i=0;i<g.count && shape<0;++i)if(g.sel.Has(g.list[i].u.v) && g.list[i].owner==Owner::squad)shape=NpcGuardShape(g.list[i].u.v);
    if(shape<0)shape=NpcMarchShape();
    Note(g,Tr(Tx::cmdFormationResult),FormationText(shape),given);
}
// T (the map open): the selected squads' formation. A squad guarding a point cycles its own defence; the others
// (the player's recruited squads following them) the march, to the one after this machine's march now however many are
// selected (set, not cycled: each squad given it sets the same one). Online their authority's (Order::formation, v7).
void Formation(Game& g,bool allowed) noexcept {
    using hudtext::Tr;
    using hudtext::Tx;
    if(!allowed){Note(g,L"%ls",Tr(Tx::cmdOfflineOnly));return;}
    const int march=static_cast<int>(npc::formation::Next(npc::formation::FromInt(NpcMarchShape()),false));
    int skipped=0;
    const int given=Dispatch(g,Command{Order::formation,{0.0f,0.0f,0.0f},mapcmd::FormationParam(-2,march)},&skipped,{});
    if(g.networkQueued)SetMarchFormation(march);   // the next T steps on from it (the squads' march is their authority's)
    FormationNote(g,given,-1);
    Log("MAPCMD formation: %d squad(s) took it, %d not, the march to %s",given,skipped,
        npc::formation::Name(static_cast<npc::formation::Shape>(march)));
}

// A group's members now (mapcmd_logic.h GroupKey): each unit by its UnitRef, a squad whose top died found again under
// the top of any member alive (Resolve: on a guest too); a unit with nothing of it alive is dropped; one only out of the
// list a while (its soldiers out of this machine's update, a vehicle between drivers) is kept, it comes back.
void KeepGroup(Game& g,int n,const void* const*) noexcept {
    int kept=0;
    for(int i=0;i<g.groupN[n];++i) {
        mapcmd::UnitRef ref=g.group[n][i];
        bool alive=false;
        const int k=Resolve(ref,g.list,g.count,&alive);
        if(k>=0)Refresh(ref,g.list[k]);
        else if(!alive)continue;
        bool twice=false;
        for(int j=0;j<kept && !twice;++j)twice=mapcmd::SameKey(g.group[n][j].top,ref.top);
        if(!twice)g.group[n][kept++]=ref;
    }
    g.groupN[n]=kept;
}
// Group `n`'s act (a number key's, the pad's D-pad's); true when the map is to centre on `centre` (the group's middle).
bool GroupAct(Game& g,int n,mapcmd::GroupAct act,const void* const* ids,float* centre) noexcept {
    using hudtext::Tr;using hudtext::Tx;using mapcmd::GroupAct;
    if(n<0 || n>=mapcmd::kGroups || act==GroupAct::none)return false;
    if(act==GroupAct::assign || act==GroupAct::append) {
        if(act==GroupAct::assign)g.groupN[n]=0;
        else KeepGroup(g,n,ids);
        for(int i=0;i<g.sel.n && g.groupN[n]<mapcmd::kMaxSel;++i) {
            const int k=mapcmd::IndexOf(ids,g.count,g.sel.id[i]);
            bool in=false;
            for(int j=0;j<g.groupN[n] && !in && k>=0;++j)in=mapcmd::SameKey(g.group[n][j].top,Key(g.list[k].id));
            if(!in && k>=0)g.group[n][g.groupN[n]++]=Capture(g.list[k]);
        }
        Note(g,Tr(g.groupN[n] ? Tx::cmdGroupSet : Tx::cmdGroupCleared),n+1,g.groupN[n]);
        Log("MAPCMD group %d %s: %d unit(s)",n+1,act==GroupAct::assign ? "set" : "added to",g.groupN[n]);
        return false;
    }
    KeepGroup(g,n,ids);
    if(!g.groupN[n]){Note(g,Tr(Tx::cmdGroupEmpty),n+1,n+1);return false;}
    if(act!=GroupAct::add)g.sel.Clear();
    float sum[3]{};int at=0;
    for(int i=0;i<g.groupN[n];++i) {
        const int k=mapcmd::IndexOf(ids,g.count,g.group[n][i].top.obj);
        if(k<0)continue;   // out of the list now: kept in the group, not selectable this frame
        g.sel.Add(g.list[k].u.v);
        for(int c=0;c<3;++c)sum[c]+=g.list[k].u.pos[c];
        ++at;
    }
    Note(g,Tr(Tx::cmdGroupPicked),n+1,at);
    if(act!=GroupAct::centre || !at)return false;
    for(int c=0;c<3;++c)centre[c]=sum[c]/static_cast<float>(at);
    return true;
}

// Whether the pointer (right button let go) is on the player's own mark (the map's owner): FOLLOW for the selection.
bool OnRequester(const Game& g,const View* v) noexcept {
    if(!v)return false;
    __try {
        if(!Readable(g.requester.obj,kPosition+12) || !g.requester.Is(g.requester.obj))return false;
        float body[3],x,y;
        mapcmd::BodyPoint(static_cast<const float*>(static_cast<const void*>(static_cast<const unsigned char*>(g.requester.obj)+kPosition)),false,body);
        if(!mapcmd::Project(v->vp,body,v->w,v->h,&x,&y))return false;
        const float dx=x-g.pointer.x,dy=y-g.pointer.y,r=kClickRadius*v->h/1080.0f;
        return dx*dx+dy*dy<=r*r;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// Y / the sweep button: the selected squads (none: the player's recruited squads) out for the boxes, or called back.
void Sweep(Game& g) noexcept {
    using hudtext::Tr;
    using hudtext::Tx;
    const void* tops[16];
    int n=0;
    for(int i=0;i<g.count && n<16;++i)if(g.sel.Has(g.list[i].u.v) && g.list[i].owner==Owner::squad)tops[n++]=g.list[i].u.v;
    const bool was=NpcSweepOn();
    const bool on=NpcSweepToggle(tops,n);
    if(on)Note(g,Tr(Tx::cmdSweepOn),n);
    else Note(g,L"%ls",Tr(was ? Tx::cmdSweepOff : Tx::cmdSweepUnavailable));
    Log("MAPCMD box sweep: %s (%d selected squads)",on ? "out" : "called back",n);
}

// O / the health-box button: health boxes for the hurt soldiers, or left for the player.
void Health(Game& g) noexcept {
    using hudtext::Tr;
    using hudtext::Tx;
    const bool on=NpcPickupHealthToggle();
    Note(g,L"%ls%ls",Tr(on ? Tx::btnHealthOn : Tx::btnHealthOff),on && InSession() ? Tr(Tx::cmdHealthOnline) : L"");
}

// The 「摆放」 panel's row `row` made at the map's `point` (debug_spawn.cpp PlaceRow: the debug spawn tool's making), its
// result in the note: an object row that cannot be made now says why first (not installed, not preloaded, faulted).
void Place(Game& g,int row,bool pointOk,const float* point) noexcept {
    using hudtext::Tr;using hudtext::Tx;using debugspawn::RowState;
    if(row<0 || row>=debugspawn::kEntryCount)return;
    const debugspawn::Entry& e=debugspawn::kEntries[row];
    if(!pointOk){Note(g,L"%ls",Tr(Tx::placeNoGround));return;}
    const RowState state=SpawnRowState(row);
    if(state==RowState::online){Note(g,L"%ls",Tr(Tx::placeOnline));return;}
    // A row not loaded yet (or still loading) goes on: its spawn queues the load and is made here once it is complete.
    if(e.sgo && state!=RowState::ready && state!=RowState::notPreloaded && state!=RowState::loading) {
        Note(g,L"%ls%ls",e.name,Tr(state==RowState::missing ? Tx::placeMissing : state==RowState::broken ? Tx::placeBroken :
                                    Tx::debugSpawnUnavailable));
        return;
    }
    wchar_t answer[96]{};
    const int status=PlaceRow(row,point,answer,_countof(answer));
    switch(status) {
    case kDebugSpawnDone: Note(g,Tr(Tx::placeDone),e.name);break;
    case kDebugSpawnRequested: Note(g,Tr(Tx::debugSpawnRequested),e.name,answer);break;
    case kDebugSpawnLoading: Note(g,Tr(Tx::placeLoadingNote),e.name);break;
    case kDebugSpawnRefused: Note(g,Tr(Tx::debugSpawnRefused),e.name,answer);break;
    case kDebugSpawnOnline: Note(g,L"%ls",Tr(Tx::placeOnline));break;
    case kDebugSpawnNoPlace: Note(g,L"%ls",Tr(Tx::placeNoGround));break;
    default: Note(g,Tr(Tx::placeFailed),e.name);break;
    }
}

void Publish(const Game& g,bool allowed,bool pointOk,const float* point,bool pointer) noexcept {
    // The game's memory is read before the lock is taken: a fault in a read (a unit gone between its listing and here)
    // must not leave the lock held, which would block the draw thread for good.
    // The formation menu's sections: the defences when a selected squad guards a point (its shape lit), the march when
    // one follows the player; no squad selected: closed.
    struct { bool open,guard,march;int guardShape; } menu{false,false,false,-1};
    for(int i=0;i<g.count && g.formationMenu;++i) {
        const Entry& e=g.list[i];
        if(!g.sel.Has(e.u.v) || e.owner!=Owner::squad || e.u.locked || e.u.riding)continue;
        // Another machine's squad (online): whether it guards or marches is its authority's (this machine lists it
        // with no order): both sections offered, the authority applies the one that fits it (else says unsupported).
        if(!IsOnlineAuthority(e.u.v)){menu.guard=menu.march=true;continue;}
        const int shape=NpcGuardShape(e.u.v);
        if(shape>=0){if(!menu.guard)menu.guardShape=shape;menu.guard=true;}
        else if(shape==-1)menu.march=true;
    }
    menu.open=menu.guard || menu.march;
    SquadRow rows[16]{};
    SquadTally tally{};
    const int squads=SquadRows(rows,16,&tally);
    // The 「摆放」 rows' states, read before the lock as the rest (support readiness, files, sessions).
    static debugspawn::RowState placeState[debugspawn::kEntryCount];
    const bool placeOnline=g.placeOn && InSession();
    if(g.placeOn)for(int i=0;i<debugspawn::kEntryCount;++i)placeState[i]=SpawnRowState(i);
    const void* pickable[kCmdUnits];
    const int pickCount=PickedIds(g,pickable);
    TransportLink links[kMapTransportLinks];
    const int linkCount=TransportLinks(links,kMapTransportLinks);
    const bool marked=NpcMarked();
    // Each unit's RTS groups and Shift-queued points (by identity: no game memory read under the lock).
    std::uint16_t groups[kCmdUnits]{};const UnitQueue* queues[kCmdUnits]{};
    for(int i=0;i<g.count;++i) {
        const ObjRef& id=g.list[i].id;
        for(int n=0;n<mapcmd::kGroups;++n)for(int j=0;j<g.groupN[n];++j)
            if(mapcmd::SameKey(g.group[n][j].top,Key(id)))groups[i]|=static_cast<std::uint16_t>(1u<<n);
        queues[i]=QueueOfListed(id);   // this machine's units' (another machine's are kept by their authority)
    }
    // Another machine's units: the points their authority keeps, as it published them (command protocol v7 Kind::ways).
    static float hostWays[kCmdUnits][mapcmd::kWaypointsMost][3];unsigned hostWayCount[kCmdUnits]{};
    static Command hostNow[kCmdUnits];
    if(InSession())for(int i=0;i<g.count;++i)
        if(!queues[i] && !IsOnlineAuthority(g.list[i].u.v))
            hostWayCount[i]=ReadMapCommandWays(g.list[i].id,&hostNow[i],hostWays[i],mapcmd::kWaypointsMost);
    AcquireSRWLockExclusive(&lock);
    MapCommandReadout& r=readout;
    r.allowed=allowed;r.all=mapcmd::IsAll(g.sel,pickCount);r.pickable=pickCount;r.selected=g.sel.n;r.pointOk=pointOk;
    r.allowedOrders=0;r.selectedSquads=0;
    r.squadToolsAllowed=!InSession() || MapCommandNetworkVersion()>=7;   // the formation order online (v7)
    for(int i=0;i<g.count;++i)if(g.sel.Has(g.list[i].u.v)) {
        const auto& entry=g.list[i];
        if(entry.owner==Owner::squad && !entry.u.locked)++r.selectedSquads;
        if(allowed)for(unsigned order=0;order<=static_cast<unsigned>(mapcmd::kLastOrder);++order)
            if(Takes(entry,static_cast<Order>(order)))r.allowedOrders|=std::uint32_t{1}<<order;
    }
    // FOCUS FIRE's button with an enemy marked (Q, or the right button on one: that gives the order itself).
    if(!marked)r.allowedOrders&=~(std::uint32_t{1}<<static_cast<unsigned>(Order::focus));
    std::memcpy(r.point,point,12);
    r.pointer=pointer;r.px=g.pointer.x;r.py=g.pointer.y;r.boxing=pointer && g.boxing;r.bx=g.bx;r.by=g.by;
    r.count=g.count;
    for(int i=0;i<g.count;++i) {
        const Entry& e=g.list[i];
        CmdMark& m=r.unit[i];
        std::memcpy(m.pos,e.u.pos,12);
        m.now=e.u.now;m.air=e.u.air;m.selected=g.sel.Has(e.u.v);m.locked=e.u.locked;m.riding=e.owner==Owner::squad && e.u.riding;
        m.owner=e.owner==Owner::heli ? kCmdOwnerHeli : e.owner==Owner::jet ? kCmdOwnerJet : kCmdOwnerGround;
        std::snprintf(m.name,sizeof(m.name),"%s",e.u.name ? e.u.name : "?");
        m.groups=groups[i];m.ways=0;
        if(const UnitQueue* q=queues[i])
            for(int w=0;w<q->w.n;++w)std::memcpy(m.way[m.ways++],q->w.next[w].at,12);
        else if(hostWayCount[i]) {   // another machine's: its current order from its host (this machine lists it with none)
            m.now=hostNow[i];
            for(unsigned w=0;w<hostWayCount[i] && w<mapcmd::kWaypointsMost;++w)std::memcpy(m.way[m.ways++],hostWays[i][w],12);
        }
    }
    r.squads=squads;r.squadTally=tally;r.squadOpen=g.panelOpen;
    r.links=linkCount;std::memcpy(r.link,links,sizeof(TransportLink)*static_cast<std::size_t>(linkCount));
    r.formationMenu=menu.open;r.formationGuard=menu.guard;r.formationGuardShape=menu.guardShape;r.formationMarch=menu.march;
    std::memcpy(r.squad,rows,sizeof(rows));
    for(int i=0;i<r.squads;++i) {
        r.squadSelected[i]=g.sel.Has(r.squad[i].leader);
        r.squadGroups[i]=0;
        for(int k=0;k<g.count;++k)if(g.list[k].u.v==r.squad[i].leader)r.squadGroups[i]=groups[k];
    }
    r.hover=static_cast<bool>(g.hover);
    std::memcpy(r.hoverAt,g.hoverAt,12);
    std::memcpy(r.note,g.note,sizeof(r.note));
    r.sweepOn=NpcSweepOn();r.healthOn=NpcPickupHealthOn();r.march=NpcMarchShape();
    r.armedOrder=g.armedOrder;r.armed=g.armed;r.supportArmed=g.armedSupport;r.supportPick=g.supportPick;
    r.composeEntry=g.composeEntry;r.composeSeats=g.composeEntry>=0 ? SupportCallSeats(g.composeEntry) : 0;r.compose=g.compose;
    r.supports=(std::min)(SupportMenuCount(),kMapSupports);   // the sea rescue is no map call
    for(int i=0;i<r.supports;++i) {
        _snwprintf_s(r.support[i].name,_countof(r.support[i].name),_TRUNCATE,L"%ls",SupportCallName(i));
        r.support[i].icon=SupportCallIcon(i);r.support[i].variant=SupportCallVariant(i);r.support[i].allowed=!SupportCallForbidden(i);
    }
    r.supportReady=SupportCallReadiness();
    SupportCallStatus(r.supportStatus,_countof(r.supportStatus));
    r.noteFresh=g.noteAt && GetTickCount64()-g.noteAt<=kNoteMs;
    r.placeOn=g.placeOn;r.placeOnline=placeOnline;r.placeOpen=g.placeOn && g.place.open;r.placeCategory=g.place.category;
    r.placePick=g.place.category>=0 && g.place.category<debugspawn::kCategoryCount ? g.place.pick[g.place.category] : 0;
    r.placeArmed=g.place.armed;
    r.placeTop=g.place.category>=0 && g.place.category<debugspawn::kCategoryCount ? g.place.top[g.place.category] : 0;
    if(g.placeOn)std::memcpy(r.placeState,placeState,sizeof(placeState));
    enemyReadout=g.enemy;
    readoutAt=GetTickCount64();
    ReleaseSRWLockExclusive(&lock);
}
}  // namespace

bool MapCommandFrame(MapCmdInput& in,float* centre) noexcept {
    Game& g=game;
    g.requester=in.requester.obj ? in.requester : ObjRef::Of(PlayerHuman());
    CommandNetworkReply(g);
    const ULONGLONG now=GetTickCount64();
    // Online enemy rows use the reliable host transaction; unsupported row kinds retain their own RowState::online.
    const bool placeKeys=RangePlaceOn();
    Keys k=ReadKeys(in,placeKeys);
    // The damage statistics' page shown: only the pointer's buttons (its clicks); no order key reaches the units under it.
    if(DamageStatsShown()){const Keys all=k;k=Keys{};k.left=all.left;k.right=all.right;k.shift=all.shift;}
    const Keys rawKeys=k;
    View v{};
    AcquireSRWLockShared(&viewLock);
    v=view;
    ReleaseSRWLockShared(&viewLock);
    const bool haveView=v.at && now-v.at<=kFreshMs && v.w>0.0f && v.h>0.0f;
    // The first frame after a gap (the map just opened): what is held now is no press (the key that opened it, a key
    // held from before), the pointer at the centre.
    if(now-g.frameAt>kFreshMs) {
        g.was=k;g.boxing=g.pressing=g.uiLeft=g.uiRight=g.rpressing=false;g.uiPress={};pointerCaptured.store(false);Arm(g,false,Order::none,-1);
        g.pvpPad={};pvpHoldsPad.store(false);pvpPadSelection.store(-1);
        g.enemyMenu={};g.enemyPadFire=false;enemyHoldsPad.store(false);
        g.place.armed=-1;   // the 「摆放」 panel stays as it was, nothing armed
        npcmark::Assign(g.hover,{});
        g.pointer=mapcmd::PointerPosition{};
    }
    g.frameAt=now;
    const bool mouseOrKey=(in.mouse && (in.dx!=0.0f || in.dy!=0.0f)) || (k.left && !g.was.left) || (k.right && !g.was.right) ||
        (k.tab && !g.was.tab) || (k.hold && !g.was.hold) || (k.follow && !g.was.follow) || (k.release && !g.was.release) ||
        (k.attackMove && !g.was.attackMove) ||
        (k.supportCall && !g.was.supportCall) || (k.supportPrev && !g.was.supportPrev) || (k.supportNext && !g.was.supportNext) ||
        (k.placePrev && !g.was.placePrev) || (k.placeNext && !g.was.placeNext) || (k.placeCategory && !g.was.placeCategory) ||
        (k.placeEnter && !g.was.placeEnter);
    const bool padPress=(k.padEnemy && !g.was.padEnemy) || (k.padPvp && !g.was.padPvp) || (k.padNext && !g.was.padNext) || (k.padGuard && !g.was.padGuard) ||
        (k.padFollow && !g.was.padFollow) || (k.padRelease && !g.was.padRelease) ||
        (!placeKeys && ((k.padUp && !g.was.padUp) || (k.padDown && !g.was.padDown) || (k.padLeft && !g.was.padLeft) ||
                        (k.padRight && !g.was.padRight))) ||
        (placeKeys && ((k.padUp && !g.was.padUp) || (k.padDown && !g.was.padDown) || (k.padLeft && !g.was.padLeft) ||
                       (k.padRight && !g.was.padRight) || (g.place.open && k.padA && !g.was.padA)));
    in.usingPad=mapcmd::UsingPad(in.usingPad,mouseOrKey,padPress);
    EnemyControlCard(g);
    const bool enemyToggle=k.padEnemy && !g.was.padEnemy;
    const bool pvpToggle=k.padPvp && !g.was.padPvp;
    // L3 into the card takes the enemy under the screen centre (the pad's pointer), else
    // keeps the target there is, else the nearest controllable one; the card unfolds.
    if(enemyToggle && in.front && haveView && v.enemyVisible && !g.enemyMenu.focused && !EnemyControlling(g)) {
        if(!PickEnemyAt(g,in,v,v.w*0.5f,v.h*0.5f,kPadPickRadius*v.h/1080.0f) && !g.enemyCandidate)CycleEnemy(g,1);
        g.enemyOpen=true;EnemyControlCard(g);
    }
    const auto enemyInput=enemyui::Step(g.enemyMenu,EnemyModel(g.enemy.snapshot,in.front && haveView && v.enemyVisible),
        {enemyToggle,k.padUp && !g.was.padUp,k.padDown && !g.was.padDown,
         k.padLeft && !g.was.padLeft,k.padRight && !g.was.padRight,k.padA && !g.was.padA,
         k.padB && !g.was.padB,k.padA,k.padB,mouseOrKey || pvpToggle});
    enemyHoldsPad.store(g.enemyMenu.held);
    if(enemyInput.consumed) {
        g.pvpPad.focused=false;g.place.open=false;g.place.armed=-1;Arm(g,false,Order::none,-1);
        k.padPvp=pvpToggle; // R3 transfers focus to PVP on this edge; A/B remain consumed.
        k.padNext=k.padGuard=k.padFollow=k.padRelease=k.padUp=k.padDown=k.padLeft=k.padRight=k.padA=k.padB=false;
    }
    if(enemyInput.cycle) {
        // The user's own step keeps the card focused on the new target.
        CycleEnemy(g,enemyInput.cycle);EnemyControlCard(g);
        g.enemyMenu.identity=EnemyModel(g.enemy.snapshot,true).identity;g.enemyMenu.row=0;
    }
    if(enemyInput.action!=enemyui::kNone) {
        Game::UiHit hit;hit.kind=Game::UiKind::enemy;hit.entry=enemyInput.action;hit.enemy=g.enemy.snapshot;UiClick(g,hit,false);
        if(enemyInput.action>=0){g.enemyPadFireSnapshot=g.enemy.snapshot;g.enemyPadEntry=enemyInput.action;}
    }
    const auto pvpInput=pvpmenu::Step(g.pvpPad,v.pvpStatus,haveView && v.pvpVisible,
        {k.padPvp && !g.was.padPvp,k.padUp && !g.was.padUp,k.padDown && !g.was.padDown,
         k.padA && !g.was.padA,k.padB && !g.was.padB,k.padA,k.padB,mouseOrKey});
    pvpHoldsPad.store(g.pvpPad.held);pvpPadSelection.store(g.pvpPad.focused ? g.pvpPad.selected : -1);
    if(pvpInput.consumed) {
        // The side panel owns this whole pad gesture, including B/R3 leaving it. Held keys cannot become an NPC order.
        g.place.open=false;g.place.armed=-1;Arm(g,false,Order::none,-1);
        k.padNext=k.padGuard=k.padFollow=k.padRelease=k.padUp=k.padDown=k.padLeft=k.padRight=k.padA=k.padB=false;
    }
    if(pvpInput.action>=0) {
        Game::UiHit hit;hit.kind=Game::UiKind::pvp;hit.entry=pvpInput.action;hit.pvp=v.pvpStatus;UiClick(g,hit,false);
    }
    // A pad button a side panel took is not let go as a tap of Y / the D-pad's groups either (its release is swallowed).
    if(enemyInput.consumed || pvpInput.consumed) {
        for(mapcmd::Held* h:{&g.padY,&g.padDir[0],&g.padDir[1],&g.padDir[2],&g.padDir[3]}){h->down=true;h->fired=true;}
    }
    const void* ids[kCmdUnits];
    Refresh(g,ids);
    g.button=-1;g.armedClick=g.rightClick=g.rightOnUi=false;g.rowPicked=false;g.placePress={};
    Pointer(g,in,k,haveView ? &v : nullptr);
    using mapbtn::Id;
    const auto clicked=[&](Id b){return g.button==static_cast<int>(b);};
    // A point order's button: armed for the next click on the ground; clicked again (or the right button): not.
    if(g.button>=0 && mapbtn::Arms(static_cast<Id>(g.button))) {
        const Order o=mapbtn::OrderOf(static_cast<Id>(g.button));
        const bool again=g.armedOrder && g.armed==o;
        Arm(g,!again,o,-1);
        if(!again)Note(g,hudtext::Tr(hudtext::Tx::cmdOrderArmed),OrderText(o));
    }
    const bool cancel=g.rightClick && (g.armedOrder || g.armedSupport>=0 || g.place.armed>=0);   // RTS: the right button leaves the targeting
    if(cancel && g.place.armed>=0){g.place.armed=-1;g.rightClick=false;Note(g,L"%ls",hudtext::Tr(hudtext::Tx::placeStopped));}
    else if(cancel){Arm(g,false,Order::none,-1);g.rightClick=false;Note(g,L"%ls",hudtext::Tr(hudtext::Tx::cmdArmCancelled));}
    if(g.rightClick && g.formationMenu){g.formationMenu=false;g.rightClick=false;}   // ...and closes the formation menu
    const int supportCount=SupportMenuCount();
    if(supportCount>0) {
        const bool previous=k.supportPrev && !g.was.supportPrev,following=k.supportNext && !g.was.supportNext;
        if(previous || following) {
            g.supportPick=(g.supportPick+(previous ? -1 : 1)+supportCount)%supportCount;
            Note(g,hudtext::Tr(hudtext::Tx::cmdSupportPicked),SupportCallName(g.supportPick));
        }
    }
    Hover(g,in,haveView ? &v : nullptr);
    EnemyControlCard(g);
    // An attack row of the card is that attack while it is held (mouse button or A on it):
    // every attack, including those past the last key, without selecting one first.
    const bool mouseSkill=rawKeys.left && g.uiLeft && g.uiPress.kind==Game::UiKind::enemy &&
        g.uiPress.entry>=0 && SameEnemy(g.uiPress.enemy,g.enemy.snapshot);
    const int padRow=enemyui::Action(EnemyModel(g.enemy.snapshot,true),g.enemyMenu.first,g.enemyMenu.row);
    const bool padSkill=enemyui::HoldFire(g.enemyPadFire,enemyInput.action>=0,rawKeys.padA,g.enemyMenu.focused,
        padRow>=0 && padRow==g.enemyPadEntry,SameEnemy(g.enemyPadFireSnapshot,g.enemy.snapshot));
    std::uint32_t heldAbility=0;
    if(mouseSkill || padSkill) {
        enemy_control::Ability ability{};
        if(EnemyControlAbilities(g.enemy.snapshot,static_cast<unsigned>(mouseSkill ? g.uiPress.entry : g.enemyPadEntry),&ability,1)==1)heldAbility=ability.id;
    }
    EnemyControlHoldAbility(g.enemy.snapshot,heldAbility);
    boxingNow.store(g.boxing);
    const bool next=(k.tab && !g.was.tab && !k.shift) || (k.padNext && !g.was.padNext),prev=k.tab && !g.was.tab && k.shift;
    bool picked=g.rowPicked;
    if(next || prev) {
        const void* pickable[kCmdUnits];
        mapcmd::Cycle(g.sel,pickable,PickedIds(g,pickable),next ? 1 : -1);
        picked=g.sel.n==1;
    }
    // RTS groups (mapcmd_logic.h GroupKey): Ctrl+N keeps the selection as group N, Ctrl+Shift+N adds it to group N, N
    // selects group N (Shift: added), N twice centres the map on it; Ctrl+A every unit. The pad's D-pad: groups 1-4
    // (a tap selects, twice centres, held it keeps the selection), not on the test range (its 「摆放」 panel has it).
    bool centred=false;float centreAt[3]{};
    if(k.all && !g.was.all) {
        const void* pickable[kCmdUnits];const int n=PickedIds(g,pickable);
        g.sel.Clear();for(int i=0;i<n;++i)g.sel.Add(pickable[i]);
        Note(g,hudtext::Tr(hudtext::Tx::cmdSelectedAll),g.sel.n);
    }
    for(int d=0;d<mapcmd::kGroups;++d) {
        if(!k.digit[d] || g.was.digit[d])continue;
        const bool again=mapcmd::DoubleTap(g.tap,d,now);
        centred=GroupAct(g,d,mapcmd::GroupKey(k.ctrl,k.shift,again),ids,centreAt) || centred;
    }
    if(!placeKeys) {
        const bool dir[4]={k.padUp,k.padRight,k.padDown,k.padLeft};
        for(int d=0;d<4;++d) {
            const mapcmd::TapHold t=mapcmd::StepHeld(g.padDir[d],dir[d],now,mapcmd::kPadHoldMs);
            const int group=mapcmd::PadGroup(d);
            if(t==mapcmd::TapHold::hold)GroupAct(g,group,mapcmd::GroupAct::assign,ids,centreAt);
            else if(t==mapcmd::TapHold::tap) {
                const bool again=mapcmd::DoubleTap(g.tap,100+d,now);
                centred=GroupAct(g,group,again ? mapcmd::GroupAct::centre : mapcmd::GroupAct::recall,ids,centreAt) || centred;
            }
        }
    }
    // The pad's Y: tapped an attack-move at the screen's centre, held HOLD (each unit where it stands).
    const mapcmd::TapHold padY=mapcmd::StepHeld(g.padY,k.padGuard,now,mapcmd::kPadHoldMs);
    mapcmd::Press p{next,prev,padY==mapcmd::TapHold::tap,
                          (k.follow && !g.was.follow) || (k.padFollow && !g.was.padFollow),
                          (k.release && !g.was.release) || (k.padRelease && !g.was.padRelease),
                          k.engage && !g.was.engage,false,k.board && !g.was.board,
                          k.dismount && !g.was.dismount && !k.shift,k.dismiss && !g.was.dismiss,k.recruit && !g.was.recruit,
                          false,k.attackMove && !g.was.attackMove,k.dismount && !g.was.dismount && k.shift,   // Shift+N: ALL OUT
                          false,(k.hold && !g.was.hold) || padY==mapcmd::TapHold::hold};
    // Shift with a move (the right button) or an attack-move (Z): after the points before it (mapcmd_logic.h Waypoints).
    const bool queue=k.shift;
    // The armed point order's click on the ground: that order there (disarmed).
    int supportAt=-1;
    SupportLoadout load{};   // the armed support's composed load, taken before disarming closes it
    if(g.armedClick && g.armedOrder) {
        p.move=p.move || g.armed==Order::move;p.attackMove=p.attackMove || g.armed==Order::attackMove;p.guard=p.guard || g.armed==Order::guard;
        Arm(g,false,Order::none,-1);
    } else if(g.armedClick && g.armedSupport>=0) {
        supportAt=g.armedSupport;
        if(g.composeEntry==supportAt)load=g.compose;
        Arm(g,false,Order::none,-1);
    }
    // The test range's 「摆放」 panel (range_place.h): its keys and pad this frame (its clicks UiClick took), one step; the
    // armed row's click on the map places it there and keeps it armed (one after another).
    int placeAt=-1;
    g.placeOn=RangePlaceOn();
    if(g.placeOn) {
        if(haveView && v.placeWindow>0)g.place.window=v.placeWindow;   // a page is what the HUD has room for
        rangeplace::Press& pk=g.placePress;   // the clicks (UiClick) as they are; the keys and the pad only when it works
        const bool pad=placeKeys;
        pk.prev=rangeplace::Repeats(g.placeUp,k.placePrev || (pad && k.padUp),g.was.placePrev || (pad && g.was.padUp),now);
        pk.next=rangeplace::Repeats(g.placeDown,k.placeNext || (pad && k.padDown),g.was.placeNext || (pad && g.was.padDown),now);
        pk.catPrev=pad && k.padLeft && !g.was.padLeft;
        pk.catNext=(k.placeCategory && !g.was.placeCategory) || (pad && k.padRight && !g.was.padRight);
        pk.close=(k.placeClose && !g.was.placeClose) || (pad && k.padB && !g.was.padB);
        pk.place=(k.placeEnter && !g.was.placeEnter) || (pad && k.padA && !g.was.padA);
        const rangeplace::Result r=rangeplace::Step(g.place,pk);
        using hudtext::Tx;
        if(r.act==rangeplace::Act::armed) {
            Arm(g,false,Order::none,-1);   // no order or support armed with it
            Note(g,hudtext::Tr(Tx::placeArmed),debugspawn::kEntries[r.row].name);
        } else if(r.act==rangeplace::Act::disarmed)Note(g,L"%ls",hudtext::Tr(Tx::placeStopped));
        else if(r.act==rangeplace::Act::place)placeAt=r.row;
        else if(r.act==rangeplace::Act::opened)Log("MAPCMD place panel open (category %d)",g.place.category);
        if(g.armedClick && g.place.armed>=0 && !g.armedOrder && g.armedSupport<0)placeAt=g.place.armed;
    } else g.place=rangeplace::Panel{};
    placeHoldsPad.store(placeKeys && g.placeOn && (g.place.open || (placeHoldsPad.load() && (k.padA || k.padB))));
    // The right button let go on the map (RTS, by what is under it): focus fire on an enemy, FOLLOW on the player's own
    // mark, else a move to the ground there.
    const mapcmd::RightClick right=mapcmd::RightClickOrder(g.rightClick && !g.rightOnUi ? g.sel.n : 0,npcmark::Alive(g.hover),true,
                                                           g.rightClick && !g.rightOnUi && OnRequester(g,haveView ? &v : nullptr));
    p.move=p.move || (right.issue && right.order==Order::move);
    p.follow=p.follow || (right.issue && right.order==Order::follow);
    const bool rightFocus=right.issue && right.order==Order::focus;
    p.focus=p.focus || rightFocus;
    p.follow=p.follow || clicked(Id::follow);p.release=p.release || clicked(Id::release);p.engage=p.engage || clicked(Id::engage);
    p.focus=p.focus || clicked(Id::focus);p.board=p.board || clicked(Id::board);p.dismount=p.dismount || clicked(Id::dismount);
    p.dismountAll=p.dismountAll || clicked(Id::dismountAll);p.withdraw=clicked(Id::withdraw);p.hold=p.hold || clicked(Id::hold);
    p.dismiss=p.dismiss || clicked(Id::dismiss);p.recruit=p.recruit || clicked(Id::recruit);
    // The formation menu and an armed order or support (its composition panel) are one at a time, as Arm keeps them.
    if(clicked(Id::formation)){g.formationMenu=!g.formationMenu;if(g.formationMenu)Arm(g,false,Order::none,-1);}
    if(g.formationMenu) {   // no squad that takes orders selected any more: nothing to arrange
        bool squads=false;
        for(int i=0;i<g.count && !squads;++i)squads=g.sel.Has(g.list[i].u.v) && g.list[i].owner==Owner::squad && Picked(g.list[i]);
        g.formationMenu=squads;
    }
    const bool formation=k.formation && !g.was.formation,sweep=(k.sweep && !g.was.sweep) || clicked(Id::sweep),
               health=(k.health && !g.was.health) || clicked(Id::health);
    const bool markPress=k.mark && !g.was.mark;
    if(g.marchPace<0)g.marchPace=Cfg().mapMarchPace ? 1 : 0;
    if(k.pace && !g.was.pace) {
        g.marchPace=g.marchPace ? 0 : 1;
        Note(g,L"%ls",hudtext::Tr(g.marchPace ? hudtext::Tx::cmdPaceSlowest : hudtext::Tx::cmdPaceFull));
        Log("MAPCMD the march's pace: %s",g.marchPace ? "the slowest unit's" : "each at full speed");
    }
    if(supportAt<0 && k.supportCall && !g.was.supportCall) {   // C: the picked call at the pointer (its composition if open)
        supportAt=g.supportPick;
        if(g.composeEntry==supportAt)load=g.compose;
    }
    g.was=rawKeys;
    float point[3];
    const bool pointOk=TargetPoint(g,in,haveView ? &v : nullptr,point);
    const bool allowed=Cfg().enabled;
    if(supportAt>=0) {
        if(pointOk){SupportCallComposedAt(supportAt,point,load.count>0 ? &load : nullptr,g.note,_countof(g.note));g.noteAt=GetTickCount64();}
        else Note(g,L"%ls",hudtext::Tr(hudtext::Tx::cmdSupportNoPoint));
    }
    if(placeAt>=0)Place(g,placeAt,pointOk,point);
    using hudtext::Tr;
    using hudtext::Tx;
    // The enemy under the pointer: the mark key marks it (or lets it go), the focus order marks it first.
    if(markPress && g.eat && npcmark::MarkOn() && npcmark::Alive(g.eatHover))
        Note(g,L"%ls",Tr(NpcMarkEnemy(g.eatHover.obj,g.eatAt,true) ? Tx::cmdMarked : Tx::cmdUnmarked));
    if(p.focus && npcmark::Alive(g.hover) && allowed && g.sel.n)NpcMarkEnemy(g.hover.obj,g.hoverAt,false);
    else if(rightFocus)p.focus=false;   // the enemy left between the press and here: no focus on an older mark
    g.focus=NpcMarkedIdentity();
    const mapcmd::Step s=mapcmd::Decide(g.sel.n,p,allowed,point,pointOk,NpcMarked());
    if(s.why==mapcmd::Refusal::online)Note(g,L"%ls",Tr(Tx::cmdOfflineOnly));
    else if(s.why==mapcmd::Refusal::noUnit && g.count)Note(g,Tr(Tx::cmdSelectFirst),in.usingPad ? L"X" : Tr(Tx::cmdSelectHowMouse));
    else if(s.why==mapcmd::Refusal::noUnit)Note(g,L"%ls",Tr(Tx::cmdNoUnit));
    else if(s.why==mapcmd::Refusal::noPoint)Note(g,L"%ls",Tr(Tx::cmdNoGround));
    else if(s.why==mapcmd::Refusal::noMark)Note(g,L"%ls",Tr(Tx::cmdNoMark));
    if(s.issue) {
        int skipped=0;
        const bool queued=queue && (s.cmd.order==Order::move || s.cmd.order==Order::attackMove);
        const int given=Dispatch(g,s.cmd,&skipped,{queued,g.marchPace==1});
        wchar_t tail[40]{};
        if(skipped)_snwprintf_s(tail,_countof(tail),_TRUNCATE,Tr(Tx::cmdCannot),skipped);
        if(g.networkQueued)Note(g,Tr(Tx::cmdNpcNetworkQueued),OrderText(s.cmd.order),g.networkQueued,given);
        else if(!given && g.networkMessage[0])Note(g,L"%ls",g.networkMessage);
        else if(!given && g.failure!=NpcCommandReason::none)Note(g,L"%ls",CommandFailureText(g.failure));
        else if(s.cmd.order==Order::board)Note(g,Tr(Tx::cmdBoardAssigned),g.affected,tail);
        else if(queued)Note(g,Tr(Tx::cmdQueued),OrderText(s.cmd.order),s.cmd.at[0],s.cmd.at[2],given,tail);
        else if(mapcmd::PointOrder(s.cmd.order))Note(g,Tr(Tx::cmdGuardResult),OrderText(s.cmd.order),s.cmd.at[0],s.cmd.at[2],given,tail);
        else Note(g,Tr(Tx::cmdOrderResult),OrderText(s.cmd.order),given,tail);
        Log("MAPCMD %s%s (%.0f,%.0f,%.0f) to %d selected: %d of %d units took it; not: %d script, %d riding, %d cannot take it, "
            "%d another machine's, %d refused (%s)",OrderName(s.cmd.order),queued ? " (queued)" : "",s.cmd.at[0],s.cmd.at[1],s.cmd.at[2],g.sel.n,given,g.count,
            g.skipScript,g.skipRiding,g.skipCannot,g.skipRemote,g.skipFailed,ReasonName(g.failure));
    }
    if(formation)Formation(g,allowed);
    if(sweep)Sweep(g);
    if(health)Health(g);
    RememberSelection(g);
    Publish(g,allowed,pointOk,point,haveView && !in.usingPad);
    if(centred){std::memcpy(centre,centreAt,12);return std::isfinite(centre[0]+centre[1]+centre[2]);}
    if(!picked)return false;
    const int selected=mapcmd::IndexOf(ids,g.count,g.sel.id[0]);
    if(selected<0)return false;
    std::memcpy(centre,g.list[selected].u.pos,12);
    return std::isfinite(centre[0]+centre[1]+centre[2]);
}

void ResetMapCommands() noexcept {
    // A new mission (map.cpp ResetMap from MissionStart): no group, selection or queue outlives the world its
    // identities were taken in (an address of the last mission may be another object of this one).
    for(auto& q:unitQueues)q=UnitQueue{};
    for(auto& p:paced)p=PacedUnit{};
    PublishShares();
    npcmark::Assign(game.hover,{});npcmark::Assign(game.eatHover,{});
    EnemyControlHoldAbility(game.enemy.snapshot,0);
    npcmark::Assign(game.enemyCandidate,{});
    static const Game kFresh{};   // the state is large (its UnitRefs): copied from here, no temporary on the stack
    game=kFresh;
    boxingNow.store(false);pointerCaptured.store(false);placeHoldsPad.store(false);pvpHoldsPad.store(false);pvpPadSelection.store(-1);enemyHoldsPad.store(false);
    AcquireSRWLockExclusive(&lock);
    readoutAt=0;
    ReleaseSRWLockExclusive(&lock);
}

void MapCommandView(const float* viewProj,float width,float height) noexcept {
    AcquireSRWLockExclusive(&viewLock);
    if(view.w!=width || view.h!=height){view.buttons=view.squads=view.payloads=view.panels=view.supports=view.menus=view.composes=view.places=view.placeWindow=view.pvps=view.enemies=0;view.folds=view.legend=false;view.pvpVisible=view.enemyVisible=false;}
    std::memcpy(view.vp,viewProj,sizeof(view.vp));view.w=width;view.h=height;view.at=GetTickCount64();
    ReleaseSRWLockExclusive(&viewLock);
}

void MapCommandButtons(const float* rects,const int* ids,int n) noexcept {
    if(n<0)n=0;
    if(n>mapbtn::kCount)n=mapbtn::kCount;
    AcquireSRWLockExclusive(&viewLock);
    view.buttons=n;
    for(int i=0;i<n;++i){view.button[i]=mapbtn::Rect{rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3]};view.id[i]=ids[i];}
    ReleaseSRWLockExclusive(&viewLock);
}

void MapCommandSupportButtons(const float* rects,const int* entries,int n) noexcept {
    n=rects && entries ? (std::max)(0,(std::min)(n,kMapSupports)) : 0;
    AcquireSRWLockExclusive(&viewLock);view.supports=n;
    if(!n)view.composes=0;   // no bar drawn: no composition panel beside it either
    for(int i=0;i<n;++i){view.support[i]={rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3]};view.supportEntry[i]=entries[i];}
    ReleaseSRWLockExclusive(&viewLock);
}

void MapCommandPlaceButtons(const float* rects,const int* codes,int n,int window) noexcept {
    n=rects && codes ? (std::max)(0,(std::min)(n,kMapPlaceItems)) : 0;
    AcquireSRWLockExclusive(&viewLock);view.places=n;view.placeWindow=(std::max)(0,(std::min)(window,rangeplace::kWindow));
    for(int i=0;i<n;++i){view.place[i]={rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3]};view.placeCode[i]=codes[i];}
    ReleaseSRWLockExclusive(&viewLock);
}
bool MapPlaceHoldsPad() noexcept { return placeHoldsPad.load() || pvpHoldsPad.load() || enemyHoldsPad.load(); }
bool MapPvpOpen() noexcept { return pvpOpen.load(); }
bool MapLegendOpen() noexcept { return legendOpen.load(); }
void MapCommandLegendButton(const float* rect) noexcept {
    AcquireSRWLockExclusive(&viewLock);
    view.legend=rect!=nullptr;
    if(rect)view.legendRow={rect[0],rect[1],rect[2],rect[3]};
    ReleaseSRWLockExclusive(&viewLock);
}
bool MapCommandWheel(int delta) noexcept {
    Game& g=game;
    static int part=0;   // a fine wheel's part-notches, kept until they make one
    if(!delta || !g.placeOn || !g.place.open || !g.pointer.placed){part=0;return false;}
    // Over the list the HUD drew (its click targets' bounds: the title, the tabs, the rows and the arrows).
    AcquireSRWLockShared(&viewLock);
    const int n=view.places;
    float x0=1e30f,y0=1e30f,x1=-1e30f,y1=-1e30f;
    for(int i=0;i<n;++i) {
        const mapbtn::Rect& q=view.place[i];
        x0=(std::min)(x0,q.x0);y0=(std::min)(y0,q.y0);x1=(std::max)(x1,q.x1);y1=(std::max)(y1,q.y1);
    }
    ReleaseSRWLockShared(&viewLock);
    const float x=g.pointer.x,y=g.pointer.y;
    if(!n || x<x0 || x>=x1 || y<y0 || y>=y1){part=0;return false;}
    part+=delta;
    const int notches=part/WHEEL_DELTA;
    part-=notches*WHEEL_DELTA;
    if(notches) {
        rangeplace::Press k{};k.scroll=-notches*rangeplace::kWheelRows;   // away from the user: up the list
        rangeplace::Step(g.place,k);
    }
    return true;
}
void MapCommandPvpButtons(const float* rects,const int* codes,int n,const PvpStatus* snapshot) noexcept {
    n=rects && codes && snapshot ? (std::max)(0,(std::min)(n,kMapPvpItems)) : 0;
    AcquireSRWLockExclusive(&viewLock);view.pvps=n;view.pvpVisible=snapshot!=nullptr;view.pvpStatus=snapshot ? *snapshot : PvpStatus{};
    for(int i=0;i<n;++i){view.pvp[i]={rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3]};view.pvpCode[i]=codes[i];}
    ReleaseSRWLockExclusive(&viewLock);
}
int MapPvpPadSelection() noexcept { return pvpPadSelection.load(); }

bool ReadMapEnemyControl(MapEnemyControlReadout* out) noexcept {
    if(!out)return false;
    AcquireSRWLockShared(&lock);
    const bool fresh=readoutAt && GetTickCount64()-readoutAt<=kFreshMs;
    if(fresh)*out=enemyReadout;
    ReleaseSRWLockShared(&lock);return fresh;
}
void MapCommandEnemyButtons(const float* rects,const int* codes,int n,const enemy_control::Snapshot* snapshot) noexcept {
    n=rects && codes && snapshot ? (std::max)(0,(std::min)(n,kMapEnemyItems)) : 0;
    AcquireSRWLockExclusive(&viewLock);view.enemies=n;view.enemyVisible=snapshot!=nullptr;
    view.enemyStatus=snapshot ? *snapshot : enemy_control::Snapshot{};
    for(int i=0;i<n;++i){view.enemy[i]={rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3]};view.enemyCode[i]=codes[i];}
    ReleaseSRWLockExclusive(&viewLock);
}
void MapCommandComposeButtons(const float* rects,const int* codes,int n) noexcept {
    n=rects && codes ? (std::max)(0,(std::min)(n,mapbtn::kComposeItems)) : 0;
    AcquireSRWLockExclusive(&viewLock);view.composes=n;
    for(int i=0;i<n;++i){view.compose[i]={rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3]};view.composeCode[i]=codes[i];}
    ReleaseSRWLockExclusive(&viewLock);
}

bool MapCommandBoxing() noexcept { return boxingNow.load(); }
bool MapCommandPointerCaptured() noexcept { return pointerCaptured.load(); }

void MapCommandSquadButtons(const float* rects,const ObjRef* identities,int n) noexcept {
    n=rects && identities ? (std::max)(0,(std::min)(n,kUiItems)) : 0;
    AcquireSRWLockExclusive(&viewLock);view.squads=n;
    for(int i=0;i<n;++i){view.squad[i]={rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3]};view.squadIdentity[i]=identities[i];}
    ReleaseSRWLockExclusive(&viewLock);
}
void MapCommandFormationButtons(const float* rects,const int* entries,int n) noexcept {
    n=rects && entries ? (std::max)(0,(std::min)(n,kMapFormationEntries)) : 0;
    AcquireSRWLockExclusive(&viewLock);view.menus=n;
    for(int i=0;i<n;++i){view.menu[i]={rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3]};view.menuEntry[i]=entries[i];}
    ReleaseSRWLockExclusive(&viewLock);
}
void MapCommandSquadFold(const float* rect) noexcept {
    AcquireSRWLockExclusive(&viewLock);
    view.folds=rect!=nullptr;
    if(rect)view.fold={rect[0],rect[1],rect[2],rect[3]};
    ReleaseSRWLockExclusive(&viewLock);
}
void MapCommandPayloadButtons(const float* rects,std::uint64_t token,int seat,const int* entries,int n) noexcept {
    n=rects && entries && token && seat>=0 ? (std::max)(0,(std::min)(n,kUiItems)) : 0;
    AcquireSRWLockExclusive(&viewLock);view.payloads=n;view.payloadToken=token;view.payloadSeat=seat;
    for(int i=0;i<n;++i){view.payload[i]={rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3]};view.payloadEntry[i]=entries[i];}
    ReleaseSRWLockExclusive(&viewLock);
}
void MapCommandUiPanels(const float* rects,int n) noexcept {
    n=rects ? (std::max)(0,(std::min)(n,kMapUiPanels)) : 0;
    AcquireSRWLockExclusive(&viewLock);view.panels=n;
    for(int i=0;i<n;++i)view.panel[i]={rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3]};
    ReleaseSRWLockExclusive(&viewLock);
}

void SuspendMapCommands() noexcept {
    // Closing the map ends a hover/press even if reopened inside kFreshMs. Keep the user's selection.
    Game& g=game;
    EnemyControlHoldAbility(g.enemy.snapshot,0);
    npcmark::Assign(g.enemyCandidate,{});g.enemyMenu={};g.enemyPadFire=false;g.enemy={};enemyHoldsPad.store(false);
    npcmark::Assign(g.hover,{});npcmark::Assign(g.eatHover,{});
    g.frameAt=0;g.eat=g.eatWas=false;g.boxing=g.pressing=g.rpressing=false;Arm(g,false,Order::none,-1);g.armedClick=g.rightClick=false;
    g.button=-1;g.uiLeft=g.uiRight=false;g.uiPress={};g.formationMenu=false;
    boxingNow.store(false);pointerCaptured.store(false);
    AcquireSRWLockExclusive(&viewLock);view.at=0;view.buttons=view.squads=view.payloads=view.panels=view.supports=view.menus=view.composes=view.places=view.placeWindow=view.pvps=view.enemies=0;view.folds=view.legend=false;view.pvpVisible=view.enemyVisible=false;ReleaseSRWLockExclusive(&viewLock);
    g.place.armed=-1;g.pvpPad={};placeHoldsPad.store(false);pvpHoldsPad.store(false);pvpPadSelection.store(-1);
    AcquireSRWLockExclusive(&lock);readoutAt=0;ReleaseSRWLockExclusive(&lock);
}

bool MapCommandEats(bool front) noexcept {
    Game& g=game;
    if(GetTickCount64()-g.frameAt>kFreshMs)npcmark::Assign(g.hover,{});
    const int vk=Cfg().npcMarkKey;
    const bool down=front && vk>0 && Down(vk);
    if(!down || !npcmark::MarkOn()){g.eat=false;npcmark::Assign(g.eatHover,{});}
    else if(!g.eatWas) {   // the press begins: the pointer's enemy of the last frame (Steer reads before the frame)
        g.eat=npcmark::Alive(g.hover);
        npcmark::Assign(g.eatHover,g.eat ? g.hover : ObjRef{});
        std::memcpy(g.eatAt,g.hoverAt,12);
    }
    g.eatWas=down;
    return g.eat;
}

int MapCommandGuardAt(const float* at) noexcept {
    Game& g=game;
    g.requester=ObjRef::Of(PlayerHuman());g.focus={};
    const void* ids[kCmdUnits];
    Refresh(g,ids);
    if(!g.sel.n)return -1;
    int skipped=0;
    const int given=Dispatch(g,Command{Order::guard,{at[0],at[1],at[2]}},&skipped,{});
    RememberSelection(g);
    Log("MAPCMD GUARD (%.0f,%.0f,%.0f) by the mark key on foot to %d selected: %d of %d units took it",at[0],at[1],at[2],
        g.sel.n,given,g.count);
    return given;
}

NpcCommandResult MapCommandAuthority(const ObjRef& unit,const Command& c,const ObjRef& requester,const ObjRef& focus,
                                     bool queued,MapCommandGive executor,std::uint32_t paceGroup) noexcept {
    const int count=ListUnits(authorityList);
    const Entry* e=nullptr;
    for(int i=0;i<count && !e;++i)if(authorityList[i].id.obj==unit.obj && authorityList[i].id.ctrl==unit.ctrl)e=&authorityList[i];
    const auto give=[&]{return executor ? executor(unit,c,requester,focus) : NpcCommandResult{NpcCommandReason::disabled,0};};
    if(!e) {
        // Not one of this machine's commandable units now: a queued point has no order now to go after (refused, never
        // given at once instead); any other order is the executor's to judge.
        if(queued && mapcmd::PointOrder(c.order))return NpcCommandResult{NpcCommandReason::stale,0};
        for(auto& q:unitQueues)if(q.w.active && mapcmd::SameKey(q.unit.top,Key(unit)))q.w=mapcmd::Waypoints{};
        return give();
    }
    bool stored=false;
    const auto result=Authority(*e,c,requester,queued,&stored,give);
    if(!stored && result.Accepted())Pace(*e,c,queued ? 0 : paceGroup);
    if(stored)Log("MAPCMD queued for %p by a network request: %s (%.0f,%.0f,%.0f)",unit.obj,OrderName(c.order),c.at[0],c.at[1],c.at[2]);
    return result;
}

// Host: this machine's queues out to the clients' maps (command_net.cpp SyncMapCommandWays: what changed, what went).
void ShareQueues() noexcept {
    static MapCommandWays out[kCmdUnits];
    unsigned n=0;
    for(const auto& q:unitQueues)if(q.w.active && q.w.n) {
        if(!mapcmd::PointOrder(q.w.issued.order))continue;   // a HOLD it is on: no point a client draws from
        MapCommandWays& w=out[n++];w.obj=q.unit.top.obj;w.ctrl=q.unit.top.ctrl;w.n=static_cast<unsigned>(q.w.n);
        w.current=q.w.issued;   // the one it is on its way to: the client's line runs unit -> it -> the rest
        for(int i=0;i<q.w.n && i<mapcmd::kWaypointsMost;++i)std::memcpy(w.at[i],q.w.next[i].at,12);
    }
    SyncMapCommandWays(out,n);
}
void MapCommandQueueTick() noexcept {
    static ULONGLONG frame=~0ull;   // once a game frame (every soldier's pre-update and every vehicle's input call it)
    const ULONGLONG f=GameFrame();
    if(f==frame)return;
    frame=f;
    bool any=false,marching=false;
    for(const auto& q:unitQueues)any=any || q.w.active;
    for(const auto& p:paced)marching=marching || p.m.active;
    if(!any && !marching){ShareQueues();return;}   // none left: any still on the clients' maps cleared
    const int count=ListUnits(authorityList);
    PaceTick(authorityList,count);
    for(auto& q:unitQueues) {
        if(!q.w.active)continue;
        bool alive=false;
        const int k=Resolve(q.unit,authorityList,count,&alive);
        if(k<0) {   // out of the list a while (between drivers, its soldiers out of the update): kept while any of it lives
            if(!alive){Log("MAPCMD queue of %p dropped: nothing of the unit lives",q.unit.top.obj);q.w=mapcmd::Waypoints{};}
            continue;
        }
        Entry& e=authorityList[k];
        if(!IsOnlineAuthority(e.u.v)){Log("MAPCMD queue of %p dropped: another machine's unit now",q.unit.top.obj);q.w=mapcmd::Waypoints{};continue;}
        Refresh(q.unit,e);
        Command next{};
        const mapcmd::QueueStep step=mapcmd::Advance(q.w,e.u.now,e.u.pos,Reach(e),&next);
        if(step==mapcmd::QueueStep::dropped)Log("MAPCMD queue of %p dropped: its order changed",q.unit.top.obj);
        if(step!=mapcmd::QueueStep::give)continue;
        const auto result=GiveUnit(e,next,q.requester,ObjRef{},authorityList,count,nullptr);
        Unpace(e.id);   // the next point of its queue: no march's
        if(result.Accepted())e.u.now=next;
        else q.w=mapcmd::Waypoints{};
        Log("MAPCMD queue of %p: %s (%.0f,%.0f,%.0f)%s, %d more",q.unit.top.obj,OrderName(next.order),next.at[0],next.at[1],next.at[2],
            result.Accepted() ? "" : " refused, queue dropped",q.w.n);
    }
    ShareQueues();
}

bool PlayerMapCommands(MapCommandReadout* out) noexcept {
    AcquireSRWLockShared(&lock);
    const bool fresh=readoutAt && GetTickCount64()-readoutAt<=kFreshMs;
    if(fresh)*out=readout;
    ReleaseSRWLockShared(&lock);
    return fresh;
}
}  // namespace crew
