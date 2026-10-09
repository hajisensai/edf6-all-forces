// Commanding the plugin's NPC units from the map view (README 地图 → 指挥 NPC; the user, 2026-10-06: "在地图上可以指挥
// npc", "操作 需要一个框选吧"). While the map (map.cpp) is open:
//  - the units: the helis the plugin flies (heli.cpp), its jets, rotor carriers and drones with no carrier (jet.cpp), the
//    Depth Crawlers it drives (ground.cpp) -- each module lists the ones that take an order now (mapcmd.h);
//  - the mouse moves a pointer the HUD draws (the game's own mouse delta: the game has no cursor of its own there). The
//    map's mouse stays as it was (map.cpp Steer): a left drag pans, a right drag turns, the wheel zooms; the pointer
//    stands still while they do. So the box is Ctrl + left drag (Shift adds), a left click with no drag selects the unit
//    under the pointer (Shift adds / takes it out) or, on empty ground, clears the selection (mapcmd_logic.h Box, Click);
//  - Tab / Shift+Tab (pad X) step through one unit at a time, then ALL (mapcmd_logic.h Cycle); the map centres on a unit
//    selected so;
//  - G (pad Y): every selected unit guards the point under the pointer (with a pad: the screen's centre) -- goes there,
//    fights what comes within its range of it, circles / patrols it; several stand round it in a formation
//    (mapcmd_logic.h Formation; guard helis share one orbit round the point instead: heli.cpp GuardOrbit spaces them).
//    V (pad RB): follow the player; X (pad LB): release (back to what it did before any command).
//  - RTS (the user, 2026-10-09): the right button let go without a drag (a right drag still turns the map) gives the
//    selection a move to the ground under the pointer, or, on an enemy, focus fire on it (mapcmd_logic.h
//    RightClickOrder); Z an attack-move there. A point order's button (move, attack-move, guard) or a support's row arms
//    the next left click on the map as its point; the right button cancels that instead.
//  - an enemy under the pointer (the user, 2026-10-07: "应该在地图里面也能按q标记"): the mark key (NpcMarkKey, Q) marks it
//    for the NPCs (npcai.cpp NpcMarkEnemy; the same one again: let go) instead of turning the map, and H (focus fire)
//    marks it and sends the selected squads at it in one press. With no enemy under the pointer Q turns the map and H
//    focuses on the mark there is (made on foot or here).
// The pointer's point: the map ray under it (mapcmd_logic.h ScreenRay on the view the HUD last drew), else that ray's
// meeting with the level ground through the focus (RayLevel).
// Online, owned units execute through their real AI authority. Replica squads are selected by native identity and
// requested through command_net; accepted transport is displayed separately from the correlated authority result.
// Remote vehicle commands are not fabricated by writing a local copy.
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

enum class Owner : std::uint8_t { heli, jet, ground, squad, tank };
struct Entry { CommandUnit u; Owner owner; };

struct Keys { bool tab,shift,ctrl,guard,follow,release,left,right,padNext,padGuard,padFollow,padRelease,
             engage,focus,board,dismount,dismiss,recruit,attackMove,mark,formation,split,merge,sweep,health,digit[9],supportPrev,supportNext,supportCall; };

// --- The game thread's own ---
struct Game {
    mapcmd::Selection sel;
    ObjRef selected[kCmdUnits]; // identity when selected; an address recycled since then is another unit
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
    bool armedClick=false;
    int supportPick;
    bool panelOpen=false;       // the squad panel opened past its commandable rows (mapcmd_logic.h SquadRowsShown)
    bool formationMenu=false;   // the formation button's menu open (map_buttons.h MenuEntry)
    bool rpressing=false,rOnUi=false;float rmoved=0.0f; // a right press (a click unless it moves kClickMove), begun on UI
    bool rightClick=false,rightOnUi=false;               // ...let go without a drag this frame
    ObjRef hover;               // original identity under the pointer; never recaptured from a cached address
    float hoverAt[3];
    bool eat,eatWas;            // the mark key's press took the enemy under the pointer: not the map's (MapCommandEats)
    ObjRef eatHover;            // ...that enemy's original identity, its lock point
    float eatAt[3];
    enum class UiKind : std::uint8_t { none,command,squad,payload,support,panel,fold,formation };
    struct UiHit { UiKind kind=UiKind::none;int id=-1;ObjRef identity{};std::uint64_t token=0;int seat=-1,entry=-1; };
    UiHit uiPress{};
    bool uiLeft=false,uiRight=false,rowPicked=false,suppressLeft=false,suppressRight=false;
    ObjRef requester{},focus{};
    NpcCommandReason failure=NpcCommandReason::none;
    unsigned affected=0;
    // The last command's units that did not take it, by why (the log: "0 of 32 units took it" said nothing of why).
    int skipScript=0,skipRiding=0,skipCannot=0,skipRemote=0,skipFailed=0;
    std::uint32_t networkRequest=0;unsigned networkQueued=0;
    Order networkOrder=Order::none;
    wchar_t networkMessage[128]{};
};
Game game{};
std::atomic<bool> boxingNow{false};
std::atomic<bool> pointerCaptured{false};

// --- The view the HUD last drew the map with (under `viewLock`) ---
constexpr int kUiItems=16;
struct View {
    float vp[16],w,h; ULONGLONG at; int buttons; mapbtn::Rect button[mapbtn::kCount]; int id[mapbtn::kCount];
    int squads=0,payloads=0,panels=0,supports=0;
    mapbtn::Rect squad[kUiItems]{},payload[kUiItems]{},panel[kUiItems]{},support[kMapSupports]{};
    int supportEntry[kMapSupports]{};
    ObjRef squadIdentity[kUiItems]{};
    bool folds=false;mapbtn::Rect fold{};   // the squad panel's summary row
    int menus=0;mapbtn::Rect menu[kMapFormationEntries]{};int menuEntry[kMapFormationEntries]{};   // the formation menu's rows
    std::uint64_t payloadToken=0;int payloadSeat=-1,payloadEntry[kUiItems]{};
};
View view{};
SRWLOCK viewLock=SRWLOCK_INIT;

// --- Published for the draw (under `lock`) ---
MapCommandReadout readout{};
ULONGLONG readoutAt=0;
SRWLOCK lock=SRWLOCK_INIT;

bool Down(int vk) noexcept { return (GetAsyncKeyState(vk)&0x8000)!=0; }

// Every unit that takes an order now, in a stable order (the vehicles' addresses).
void List(Game& g) noexcept {
    CommandUnit buf[kCmdUnits];
    g.count=0;
    auto add=[&](int n,Owner o){ for(int i=0;i<n && g.count<kCmdUnits;++i)g.list[g.count++]=Entry{buf[i],o}; };
    add(HeliCommandUnits(buf,kCmdUnits),Owner::heli);
    add(JetCommandUnits(buf,kCmdUnits-g.count),Owner::jet);
    add(GroundCommandUnits(buf,kCmdUnits-g.count),Owner::ground);
    add(TankCommandUnits(buf,kCmdUnits-g.count),Owner::tank);
    add(SquadCommandUnits(buf,kCmdUnits-g.count),Owner::squad);
    std::sort(g.list,g.list+g.count,[](const Entry& a,const Entry& b){ return std::less<const void*>()(a.u.v,b.u.v); });
}

void KeepSelection(Game& g,const void* const* ids) noexcept {
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
    for(int i=0;i<kCmdUnits;++i)g.selected[i]=i<g.sel.n ? ObjRef::Of(g.sel.id[i]) : ObjRef{};
}

bool Give(Game& g,const Entry& e,const Command& order) noexcept {
    // A transport carrying its squad (transport.cpp; the user, 2026-10-09: "卡车之类的运输载具改成断剑那种操作方式"): a point
    // order to the vehicle is the squad's (they ride there and get off short of the point), unless the squad is selected
    // too (its own row gives it, once).
    if(e.owner!=Owner::squad && mapcmd::PointOrder(order.order))
        if(const void* rider=TransportRiderOf(e.u.v); rider && !g.sel.Has(rider)) {
            const auto result=NpcSquadCommandForRequester(ObjRef::Of(rider),order,g.requester,g.focus);
            if(!result.Accepted())g.failure=result.reason;
            else g.affected+=result.affected;
            return result.Accepted();
        }
    // A vehicle's module keeps one point (its post / anchor): a move or an attack-move reaches it as a guard of it.
    const Command c=e.owner==Owner::squad ? order : Command{mapcmd::VehicleCommandOf(order.order),{order.at[0],order.at[1],order.at[2]}};
    switch(e.owner) {
    case Owner::heli: return HeliCommand(e.u.v,c,g.focus);
    case Owner::jet: return JetCommand(e.u.v,c,g.focus);
    case Owner::ground: return GroundCommand(e.u.v,c);
    case Owner::squad: {
        const auto result=NpcSquadCommandForRequester(ObjRef::Of(e.u.v),c,g.requester,g.focus);
        if(!result.Accepted())g.failure=result.reason;
        else g.affected+=result.affected;
        return result.Accepted();
    }
    case Owner::tank: return TankCommand(e.u.v,c);
    }
    return false;
}

// The ground point along the ray from `eye` along unit `dir`: the first floor the map ray meets (a cave's roof seen from
// above passed through: map_floor.h), else the level plane at `level`.
bool GroundAlong(const float* eye,const float* dir,float level,float* point) noexcept {
    const float end[3]={eye[0]+dir[0]*kPointFar,eye[1]+dir[1]*kPointFar,eye[2]+dir[2]*kPointFar};
    float hit[3];
    if(MapFloorRay(eye,end,hit)>=0.0f && std::isfinite(hit[0]+hit[1]+hit[2])){std::memcpy(point,hit,12);return true;}
    return mapcmd::RayLevel(eye,dir,level,point);
}
Keys ReadKeys(const MapCmdInput& in) noexcept {
    Keys k{};
    if(in.front) {
        k.tab=Down(VK_TAB);k.shift=Down(VK_SHIFT);k.ctrl=Down(VK_CONTROL);
        k.guard=Down('G');k.follow=Down('V');k.release=Down('X');k.left=Down(VK_LBUTTON);k.right=Down(VK_RBUTTON);
        // The squads' orders (§6.1): keys the map's own camera does not use (WASD QE RF are its pan / turn / tilt).
        k.engage=Down('J');k.focus=Down('H');k.board=Down('B');k.dismount=Down('N');k.dismiss=Down('K');k.recruit=Down('U');k.formation=Down('T');k.split=Down('P');k.merge=Down('L');k.sweep=Down('Y');k.health=Down('O');
        k.attackMove=Down('Z');
        k.mark=Cfg().npcMarkKey>0 && Down(Cfg().npcMarkKey);
        for(int d=0;d<9;++d)k.digit[d]=Down('1'+d);
        k.supportPrev=Down(VK_OEM_4);k.supportNext=Down(VK_OEM_6);k.supportCall=Down('C');
    }
    if(in.pad) {
        const WORD b=in.buttons;
        k.padNext=(b&XINPUT_GAMEPAD_X)!=0;k.padGuard=(b&XINPUT_GAMEPAD_Y)!=0;
        k.padFollow=(b&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0;k.padRelease=(b&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0;
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
    case Order::dismiss: return "DISMISS";
    case Order::recruit: return "RECRUIT";
    case Order::move: return "MOVE";
    case Order::attackMove: return "ATTACK-MOVE";
    case Order::withdraw: return "WITHDRAW";
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
    case Order::dismiss: return hudtext::Tr(Tx::orderDismiss);
    case Order::recruit: return hudtext::Tr(Tx::orderRecruit);
    case Order::move: return hudtext::Tr(Tx::orderMove);
    case Order::attackMove: return hudtext::Tr(Tx::orderAttackMove);
    case Order::withdraw: return hudtext::Tr(Tx::orderWithdraw);
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
    if(e.owner==Owner::squad) {
        if(o==Order::recruit)return e.u.recruitable;
        if(o==Order::dismiss)return e.u.recruited;
        if(o==Order::board)return !e.u.riding;
        if(o==Order::dismount)return e.u.riding;
        // Its transport (transport.cpp): WITHDRAW sends it off; aboard it the point orders take it there (it gets off short
        // of the point and carries them out on foot).
        if(o==Order::withdraw)return e.u.transport;
        if(e.u.riding)return o!=Order::follow && o!=Order::engage && o!=Order::focus && (e.u.transport || !mapcmd::PointOrder(o));
        return true;
    }
    if(e.owner==Owner::tank)return o==Order::none || mapcmd::PointOrder(o);
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
        "boarding unavailable","no vehicle","riding","no transport"};
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
    int i=mapbtn::Hit(v.menu,v.menus,x,y);   // drawn over the rest
    if(i>=0){hit.kind=Game::UiKind::formation;hit.entry=v.menuEntry[i];return hit;}
    i=mapbtn::Hit(v.button,v.buttons,x,y);
    if(i>=0){hit.kind=Game::UiKind::command;hit.id=v.id[i];return hit;}
    i=mapbtn::Hit(v.squad,v.squads,x,y);
    if(i>=0){hit.kind=Game::UiKind::squad;hit.identity=v.squadIdentity[i];return hit;}
    if(v.folds && mapbtn::Hit(&v.fold,1,x,y)>=0){hit.kind=Game::UiKind::fold;return hit;}
    i=mapbtn::Hit(v.payload,v.payloads,x,y);
    if(i>=0){hit.kind=Game::UiKind::payload;hit.token=v.payloadToken;hit.seat=v.payloadSeat;hit.entry=v.payloadEntry[i];return hit;}
    i=mapbtn::Hit(v.support,v.supports,x,y);
    if(i>=0){hit.kind=Game::UiKind::support;hit.entry=v.supportEntry[i];return hit;}
    if(mapbtn::Hit(v.panel,v.panels,x,y)>=0)hit.kind=Game::UiKind::panel;
    return hit;
}
bool SameUi(const Game::UiHit& a,const Game::UiHit& b) noexcept {
    return a.kind==b.kind && a.id==b.id && a.identity.obj==b.identity.obj && a.identity.ctrl==b.identity.ctrl &&
        a.token==b.token && a.seat==b.seat && a.entry==b.entry;
}
// One thing armed at a time: a point order or a support call (`order` false: the support `support`, -1 nothing).
void Arm(Game& g,bool order,Order o,int support) noexcept {
    g.armedOrder=order;g.armed=order ? o : Order::none;g.armedSupport=order ? -1 : support;
    if(order || support>=0)g.formationMenu=false;
}
// The formation menu's pick (map_buttons.h MenuEntry): a defence for the selected squads on a guard point, or the march
// of the player's squads; the menu closed.
void PickFormation(Game& g,int entry) noexcept {
    using hudtext::Tr;using hudtext::Tx;
    g.formationMenu=false;
    const int shape=mapbtn::MenuShape(entry);
    if(InSession() || !Cfg().enabled){Note(g,L"%ls",Tr(Tx::cmdOfflineOnly));return;}
    int done=0;
    if(mapbtn::MenuGuard(entry)) {
        for(int i=0;i<g.count;++i)if(g.sel.Has(g.list[i].u.v) && g.list[i].owner==Owner::squad && SetGuardFormation(g.list[i].u.v,shape)>=0)++done;
    } else if(SetMarchFormation(shape)>=0)done=1;
    if(done)Note(g,Tr(Tx::cmdFormationResult),FormationText(shape),done);
    else Note(g,L"%ls",Tr(Tx::cmdNoUnit));
    Log("MAPCMD formation menu: %s %s, %d",mapbtn::MenuGuard(entry) ? "defence" : "march",
        npc::formation::Name(npc::formation::FromInt(shape)),done);
}
void UiClick(Game& g,const Game::UiHit& hit,bool shift) noexcept {
    if(hit.kind==Game::UiKind::command){g.button=hit.id;return;}
    if(hit.kind==Game::UiKind::fold){g.panelOpen=!g.panelOpen;return;}
    if(hit.kind==Game::UiKind::formation){PickFormation(g,hit.entry);return;}
    if(hit.kind==Game::UiKind::support) {   // its row or chip: armed (again: disarmed)
        const bool again=g.armedSupport==hit.entry;
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
    if(in.mouse && (g.uiLeft || g.uiRight || g.boxing || (!k.left && !k.right))) {
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
    if(k.left && !g.was.left && !g.suppressLeft) {
        g.uiPress=UiAt(*v,g.pointer.x,g.pointer.y);g.uiLeft=g.uiPress.kind!=Game::UiKind::none;g.moved=0;
        if(g.uiLeft)g.boxing=g.pressing=false;
        else if(k.ctrl){g.boxing=true;g.bx=g.pointer.x;g.by=g.pointer.y;}
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
    else if(click && (g.armedOrder || g.armedSupport>=0) && !g.boxing)g.armedClick=true;
    else if(box)mapcmd::Box(g.sel,marks,n,g.bx,g.by,g.pointer.x,g.pointer.y,k.shift);
    else if(click) {
        const Entry* hit=EntryOf(g,mapcmd::Click(g.sel,marks,n,g.pointer.x,g.pointer.y,kClickRadius*s,k.shift));
        if(hit && !Picked(*hit))Note(g,L"%ls",CommandFailureText(NotPicked(*hit)));
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
    if(!v || !npcmark::Enabled())return;
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

// The command to every selected unit; a point order's formation round the point (helis sharing one orbit stay on it).
int Issue(Game& g,const Command& cmd,int* skipped) noexcept {
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
        if(g.sel.Has(e.u.v) && e.owner==Owner::squad && Takes(e,cmd.order) && !IsOnlineAuthority(e.u.v)) {
            if(remotes<kCommandNetUnits){remote[remotes]=ObjRef::Of(e.u.v);remoteSlots[remotes]=slots[i];}++remotes;
        }
    }
    if(remotes>kCommandNetUnits) {
        *skipped=k;g.failure=NpcCommandReason::unsupported;
        _snwprintf_s(g.networkMessage,_countof(g.networkMessage),_TRUNCATE,L"%ls",hudtext::Tr(hudtext::Tx::cmdNpcNetworkLimit));return 0;
    }
    if(remotes) {
        const auto request=SubmitMapCommand(g.requester,remote,remotes,cmd,g.focus,g.networkMessage,_countof(g.networkMessage),
            mapcmd::PointOrder(cmd.order) ? remoteSlots : nullptr,mapcmd::PointOrder(cmd.order) ? static_cast<std::uint32_t>(k) : 0);
        if(request){g.networkRequest=request;g.networkQueued=remotes;g.networkOrder=cmd.order;}
    }
    int given=0;
    for(int i=0;i<g.count;++i) {
        Entry& e=g.list[i];
        if(!g.sel.Has(e.u.v))continue;
        if(!Takes(e,cmd.order)) {
            g.failure=e.u.locked ? NpcCommandReason::scripted : e.u.riding ? NpcCommandReason::riding : NpcCommandReason::unsupported;
            ++(e.u.locked ? g.skipScript : e.u.riding ? g.skipRiding : g.skipCannot);
            ++*skipped;continue;
        }
        if(!IsOnlineAuthority(e.u.v)) {
            if(e.owner==Owner::squad && g.networkQueued)continue;
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
        if(!Give(g,e,c)){if(g.failure==NpcCommandReason::none)g.failure=NpcCommandReason::failed;++g.skipFailed;++*skipped;continue;}
        e.u.now=c;
        ++given;
    }
    return given;
}


// T (the map open): the selected squads' formation. A squad guarding a point cycles its own defence; the others
// (the player's recruited squads following them) the march, once however many are selected.
void Formation(Game& g,bool allowed) noexcept {
    using hudtext::Tr;
    using hudtext::Tx;
    if(!allowed){Note(g,L"%ls",Tr(Tx::cmdOfflineOnly));return;}
    int guards=0,marching=0,shape=-1,marchShape=-1;
    for(int i=0;i<g.count;++i) {
        const Entry& e=g.list[i];
        if(!g.sel.Has(e.u.v) || e.owner!=Owner::squad)continue;
        const int r=CycleGuardFormation(e.u.v);
        if(r>=0){shape=r;++guards;}
        else if(r==-1)++marching;
    }
    if(marching)marchShape=CycleMarchFormation();
    if(!guards && !marching){Note(g,L"%ls",Tr(Tx::cmdNoUnit));return;}
    if(marching)Note(g,Tr(Tx::cmdFormationResult),FormationText(marchShape),marching);
    else Note(g,Tr(Tx::cmdFormationResult),FormationText(shape),guards);
    Log("MAPCMD formation: %d guarding squad(s) %s, the march %s",guards,shape>=0 ? npc::formation::Name(static_cast<npc::formation::Shape>(shape)) : "-",
        marchShape>=0 ? npc::formation::Name(static_cast<npc::formation::Shape>(marchShape)) : "unchanged");
}

// P (the map open): each selected squad split in two fireteams; L: the selected squads joined under the first selected.
void Teams(Game& g,bool allowed,bool split) noexcept {
    using hudtext::Tr;
    using hudtext::Tx;
    if(!allowed){Note(g,L"%ls",Tr(Tx::cmdOfflineOnly));return;}
    const void* first=nullptr;
    int done=0,squads=0;
    for(int i=0;i<g.count;++i) {
        const Entry& e=g.list[i];
        if(!g.sel.Has(e.u.v) || e.owner!=Owner::squad)continue;
        ++squads;
        if(split){if(SplitSquad(e.u.v)>0)++done;continue;}
        if(!first){first=e.u.v;continue;}
        if(MergeSquads(first,e.u.v))++done;
    }
    if(!squads){Note(g,L"%ls",Tr(Tx::cmdNoUnit));return;}
    if(split)Note(g,Tr(Tx::cmdSplitResult),done,squads-done);
    else Note(g,Tr(Tx::cmdMergeResult),done,squads>1 ? squads-1-done : 0);
    Log("MAPCMD %s: %d of %d selected squads",split ? "split" : "merge",done,squads);
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

void Publish(const Game& g,bool allowed,bool pointOk,const float* point,bool pointer) noexcept {
    // The game's memory is read before the lock is taken: a fault in a read (a unit gone between its listing and here)
    // must not leave the lock held, which would block the draw thread for good.
    // The formation menu's sections: the defences when a selected squad guards a point (its shape lit), the march when
    // one follows the player; no squad selected: closed.
    struct { bool open,guard,march;int guardShape; } menu{false,false,false,-1};
    for(int i=0;i<g.count && g.formationMenu;++i) {
        const Entry& e=g.list[i];
        if(!g.sel.Has(e.u.v) || e.owner!=Owner::squad || e.u.locked || e.u.riding)continue;
        const int shape=NpcGuardShape(e.u.v);
        if(shape>=0){if(!menu.guard)menu.guardShape=shape;menu.guard=true;}
        else if(shape==-1)menu.march=true;
    }
    menu.open=menu.guard || menu.march;
    SquadRow rows[16]{};
    SquadTally tally{};
    const int squads=SquadRows(rows,16,&tally);
    const void* pickable[kCmdUnits];
    const int pickCount=PickedIds(g,pickable);
    TransportLink links[kMapTransportLinks];
    const int linkCount=TransportLinks(links,kMapTransportLinks);
    const bool marked=NpcMarked();
    AcquireSRWLockExclusive(&lock);
    MapCommandReadout& r=readout;
    r.allowed=allowed;r.all=mapcmd::IsAll(g.sel,pickCount);r.pickable=pickCount;r.selected=g.sel.n;r.pointOk=pointOk;
    r.allowedOrders=0;r.selectedSquads=0;
    r.squadToolsAllowed=!InSession();
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
    }
    r.squads=squads;r.squadTally=tally;r.squadOpen=g.panelOpen;
    r.links=linkCount;std::memcpy(r.link,links,sizeof(TransportLink)*static_cast<std::size_t>(linkCount));
    r.formationMenu=menu.open;r.formationGuard=menu.guard;r.formationGuardShape=menu.guardShape;r.formationMarch=menu.march;
    std::memcpy(r.squad,rows,sizeof(rows));
    for(int i=0;i<r.squads;++i)r.squadSelected[i]=g.sel.Has(r.squad[i].leader);
    r.hover=static_cast<bool>(g.hover);
    std::memcpy(r.hoverAt,g.hoverAt,12);
    std::memcpy(r.note,g.note,sizeof(r.note));
    r.sweepOn=NpcSweepOn();r.healthOn=NpcPickupHealthOn();r.march=NpcMarchShape();
    r.armedOrder=g.armedOrder;r.armed=g.armed;r.supportArmed=g.armedSupport;r.supportPick=g.supportPick;
    r.supports=(std::min)(SupportCallCount(),kMapSupports);
    for(int i=0;i<r.supports;++i) {
        _snwprintf_s(r.support[i].name,_countof(r.support[i].name),_TRUNCATE,L"%ls",SupportCallName(i));
        r.support[i].icon=SupportCallIcon(i);r.support[i].variant=SupportCallVariant(i);
    }
    r.supportReady=SupportCallReadiness();
    SupportCallStatus(r.supportStatus,_countof(r.supportStatus));
    r.noteFresh=g.noteAt && GetTickCount64()-g.noteAt<=kNoteMs;
    readoutAt=GetTickCount64();
    ReleaseSRWLockExclusive(&lock);
}
}  // namespace

bool MapCommandFrame(MapCmdInput& in,float* centre) noexcept {
    Game& g=game;
    g.requester=in.requester.obj ? in.requester : ObjRef::Of(PlayerHuman());
    CommandNetworkReply(g);
    const ULONGLONG now=GetTickCount64();
    const Keys k=ReadKeys(in);
    View v{};
    AcquireSRWLockShared(&viewLock);
    v=view;
    ReleaseSRWLockShared(&viewLock);
    const bool haveView=v.at && now-v.at<=kFreshMs && v.w>0.0f && v.h>0.0f;
    // The first frame after a gap (the map just opened): what is held now is no press (the key that opened it, a key
    // held from before), the pointer at the centre.
    if(now-g.frameAt>kFreshMs) {
        g.was=k;g.boxing=g.pressing=g.uiLeft=g.uiRight=g.rpressing=false;g.uiPress={};pointerCaptured.store(false);Arm(g,false,Order::none,-1);
        npcmark::Assign(g.hover,{});
        g.pointer=mapcmd::PointerPosition{};
    }
    g.frameAt=now;
    const bool mouseOrKey=(in.mouse && (in.dx!=0.0f || in.dy!=0.0f)) || (k.left && !g.was.left) || (k.right && !g.was.right) ||
        (k.tab && !g.was.tab) || (k.guard && !g.was.guard) || (k.follow && !g.was.follow) || (k.release && !g.was.release) ||
        (k.attackMove && !g.was.attackMove) ||
        (k.supportCall && !g.was.supportCall) || (k.supportPrev && !g.was.supportPrev) || (k.supportNext && !g.was.supportNext);
    const bool padPress=(k.padNext && !g.was.padNext) || (k.padGuard && !g.was.padGuard) ||
        (k.padFollow && !g.was.padFollow) || (k.padRelease && !g.was.padRelease);
    in.usingPad=mapcmd::UsingPad(in.usingPad,mouseOrKey,padPress);
    const void* ids[kCmdUnits];
    Refresh(g,ids);
    g.button=-1;g.armedClick=g.rightClick=g.rightOnUi=false;g.rowPicked=false;
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
    const bool cancel=g.rightClick && (g.armedOrder || g.armedSupport>=0);   // RTS: the right button leaves the targeting
    if(cancel){Arm(g,false,Order::none,-1);g.rightClick=false;Note(g,L"%ls",hudtext::Tr(hudtext::Tx::cmdArmCancelled));}
    if(g.rightClick && g.formationMenu){g.formationMenu=false;g.rightClick=false;}   // ...and closes the formation menu
    const int supportCount=SupportCallCount();
    if(supportCount>0) {
        const bool previous=k.supportPrev && !g.was.supportPrev,following=k.supportNext && !g.was.supportNext;
        if(previous || following) {
            g.supportPick=(g.supportPick+(previous ? -1 : 1)+supportCount)%supportCount;
            Note(g,hudtext::Tr(hudtext::Tx::cmdSupportPicked),SupportCallName(g.supportPick));
        }
    }
    Hover(g,in,haveView ? &v : nullptr);
    boxingNow.store(g.boxing);
    const bool next=(k.tab && !g.was.tab && !k.shift) || (k.padNext && !g.was.padNext),prev=k.tab && !g.was.tab && k.shift;
    bool picked=g.rowPicked;
    if(next || prev) {
        const void* pickable[kCmdUnits];
        mapcmd::Cycle(g.sel,pickable,PickedIds(g,pickable),next ? 1 : -1);
        picked=g.sel.n==1;
    }
    // The number keys pick a squad of the panel: its rows shown (SquadRows' order, folded or open: mapcmd_logic.h
    // SquadRowsShown); Shift adds or takes out. A script's squad says why instead.
    SquadRow rows[9];
    const int listed=SquadRows(rows,9);
    int ranks[9];
    for(int d=0;d<listed;++d)ranks[d]=rows[d].rank;
    const int rowCount=mapcmd::SquadRowsShown(ranks,listed,g.panelOpen,9);
    for(int d=0;d<rowCount;++d) {
        if(!k.digit[d] || g.was.digit[d])continue;
        if(rows[d].locked){Note(g,L"%ls",CommandFailureText(NpcCommandReason::scripted));continue;}
        if(!k.shift)g.sel.Clear();
        if(k.shift && g.sel.Has(rows[d].leader))g.sel.Remove(rows[d].leader);
        else g.sel.Add(rows[d].leader);
        mapcmd::Keep(g.sel,ids,g.count);
        picked=g.sel.n==1;
    }
    mapcmd::Press p{next,prev,(k.guard && !g.was.guard) || (k.padGuard && !g.was.padGuard),
                          (k.follow && !g.was.follow) || (k.padFollow && !g.was.padFollow),
                          (k.release && !g.was.release) || (k.padRelease && !g.was.padRelease),
                          k.engage && !g.was.engage,k.focus && !g.was.focus,k.board && !g.was.board,
                          k.dismount && !g.was.dismount,k.dismiss && !g.was.dismiss,k.recruit && !g.was.recruit,
                          false,k.attackMove && !g.was.attackMove};
    // The armed point order's click on the ground: that order there (disarmed).
    int supportAt=-1;
    if(g.armedClick && g.armedOrder) {
        p.move=p.move || g.armed==Order::move;p.attackMove=p.attackMove || g.armed==Order::attackMove;p.guard=p.guard || g.armed==Order::guard;
        Arm(g,false,Order::none,-1);
    } else if(g.armedClick && g.armedSupport>=0){supportAt=g.armedSupport;Arm(g,false,Order::none,-1);}
    // The right button let go on the map (RTS): a move to the ground there, or focus fire on the enemy under it.
    const mapcmd::RightClick right=mapcmd::RightClickOrder(g.rightClick && !g.rightOnUi ? g.sel.n : 0,npcmark::Alive(g.hover),true);
    p.move=p.move || (right.issue && right.order==Order::move);
    const bool rightFocus=right.issue && right.order==Order::focus;
    p.focus=p.focus || rightFocus;
    p.follow=p.follow || clicked(Id::follow);p.release=p.release || clicked(Id::release);p.engage=p.engage || clicked(Id::engage);
    p.focus=p.focus || clicked(Id::focus);p.board=p.board || clicked(Id::board);p.dismount=p.dismount || clicked(Id::dismount);
    p.dismiss=p.dismiss || clicked(Id::dismiss);p.recruit=p.recruit || clicked(Id::recruit);
    if(clicked(Id::formation))g.formationMenu=!g.formationMenu;
    if(g.formationMenu) {   // no squad that takes orders selected any more: nothing to arrange
        bool squads=false;
        for(int i=0;i<g.count && !squads;++i)squads=g.sel.Has(g.list[i].u.v) && g.list[i].owner==Owner::squad && Picked(g.list[i]);
        g.formationMenu=squads;
    }
    const bool formation=k.formation && !g.was.formation,split=(k.split && !g.was.split) || clicked(Id::split),
               merge=(k.merge && !g.was.merge) || clicked(Id::merge),sweep=(k.sweep && !g.was.sweep) || clicked(Id::sweep),
               health=(k.health && !g.was.health) || clicked(Id::health);
    const bool markPress=k.mark && !g.was.mark;
    if(supportAt<0 && k.supportCall && !g.was.supportCall)supportAt=g.supportPick;   // C: the picked call at the pointer
    g.was=k;
    float point[3];
    const bool pointOk=TargetPoint(g,in,haveView ? &v : nullptr,point);
    const bool allowed=Cfg().enabled;
    if(supportAt>=0) {
        if(pointOk){SupportCallAt(supportAt,point,g.note,_countof(g.note));g.noteAt=GetTickCount64();}
        else Note(g,L"%ls",hudtext::Tr(hudtext::Tx::cmdSupportNoPoint));
    }
    using hudtext::Tr;
    using hudtext::Tx;
    // The enemy under the pointer: the mark key marks it (or lets it go), the focus order marks it first.
    if(markPress && g.eat && npcmark::Enabled() && npcmark::Alive(g.eatHover))
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
        const int given=Issue(g,s.cmd,&skipped);
        wchar_t tail[40]{};
        if(skipped)_snwprintf_s(tail,_countof(tail),_TRUNCATE,Tr(Tx::cmdCannot),skipped);
        if(g.networkQueued)Note(g,Tr(Tx::cmdNpcNetworkQueued),OrderText(s.cmd.order),g.networkQueued,given);
        else if(!given && g.networkMessage[0])Note(g,L"%ls",g.networkMessage);
        else if(!given && g.failure!=NpcCommandReason::none)Note(g,L"%ls",CommandFailureText(g.failure));
        else if(s.cmd.order==Order::board)Note(g,Tr(Tx::cmdBoardAssigned),g.affected,tail);
        else if(mapcmd::PointOrder(s.cmd.order))Note(g,Tr(Tx::cmdGuardResult),OrderText(s.cmd.order),s.cmd.at[0],s.cmd.at[2],given,tail);
        else Note(g,Tr(Tx::cmdOrderResult),OrderText(s.cmd.order),given,tail);
        Log("MAPCMD %s (%.0f,%.0f,%.0f) to %d selected: %d of %d units took it; not: %d script, %d riding, %d cannot take it, "
            "%d another machine's, %d refused (%s)",OrderName(s.cmd.order),s.cmd.at[0],s.cmd.at[1],s.cmd.at[2],g.sel.n,given,g.count,
            g.skipScript,g.skipRiding,g.skipCannot,g.skipRemote,g.skipFailed,ReasonName(g.failure));
    }
    if(formation)Formation(g,allowed && !InSession());
    if(split || merge)Teams(g,allowed && !InSession(),split);
    if(sweep)Sweep(g);
    if(health)Health(g);
    RememberSelection(g);
    Publish(g,allowed,pointOk,point,haveView && !in.usingPad);
    if(!picked)return false;
    const int selected=mapcmd::IndexOf(ids,g.count,g.sel.id[0]);
    if(selected<0)return false;
    std::memcpy(centre,g.list[selected].u.pos,12);
    return std::isfinite(centre[0]+centre[1]+centre[2]);
}

void ResetMapCommands() noexcept {
    npcmark::Assign(game.hover,{});npcmark::Assign(game.eatHover,{});
    game=Game{};
    boxingNow.store(false);pointerCaptured.store(false);
    AcquireSRWLockExclusive(&lock);
    readoutAt=0;
    ReleaseSRWLockExclusive(&lock);
}

void MapCommandView(const float* viewProj,float width,float height) noexcept {
    AcquireSRWLockExclusive(&viewLock);
    if(view.w!=width || view.h!=height){view.buttons=view.squads=view.payloads=view.panels=view.supports=view.menus=0;view.folds=false;}
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
    for(int i=0;i<n;++i){view.support[i]={rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3]};view.supportEntry[i]=entries[i];}
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
    n=rects ? (std::max)(0,(std::min)(n,kUiItems)) : 0;
    AcquireSRWLockExclusive(&viewLock);view.panels=n;
    for(int i=0;i<n;++i)view.panel[i]={rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3]};
    ReleaseSRWLockExclusive(&viewLock);
}

void SuspendMapCommands() noexcept {
    // Closing the map ends a hover/press even if reopened inside kFreshMs. Keep the user's selection.
    Game& g=game;
    npcmark::Assign(g.hover,{});npcmark::Assign(g.eatHover,{});
    g.frameAt=0;g.eat=g.eatWas=false;g.boxing=g.pressing=g.rpressing=false;Arm(g,false,Order::none,-1);g.armedClick=g.rightClick=false;
    g.button=-1;g.uiLeft=g.uiRight=false;g.uiPress={};g.formationMenu=false;
    boxingNow.store(false);pointerCaptured.store(false);
    AcquireSRWLockExclusive(&viewLock);view.at=0;view.buttons=view.squads=view.payloads=view.panels=view.supports=view.menus=0;view.folds=false;ReleaseSRWLockExclusive(&viewLock);
    AcquireSRWLockExclusive(&lock);readoutAt=0;ReleaseSRWLockExclusive(&lock);
}

bool MapCommandEats(bool front) noexcept {
    Game& g=game;
    if(GetTickCount64()-g.frameAt>kFreshMs)npcmark::Assign(g.hover,{});
    const int vk=Cfg().npcMarkKey;
    const bool down=front && vk>0 && Down(vk);
    if(!down || !npcmark::Enabled()){g.eat=false;npcmark::Assign(g.eatHover,{});}
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
    const int given=Issue(g,Command{Order::guard,{at[0],at[1],at[2]}},&skipped);
    RememberSelection(g);
    Log("MAPCMD GUARD (%.0f,%.0f,%.0f) by the mark key on foot to %d selected: %d of %d units took it",at[0],at[1],at[2],
        g.sel.n,given,g.count);
    return given;
}

bool PlayerMapCommands(MapCommandReadout* out) noexcept {
    AcquireSRWLockShared(&lock);
    const bool fresh=readoutAt && GetTickCount64()-readoutAt<=kFreshMs;
    if(fresh)*out=readout;
    ReleaseSRWLockShared(&lock);
    return fresh;
}
}  // namespace crew
