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
//  - an enemy under the pointer (the user, 2026-10-07: "应该在地图里面也能按q标记"): the mark key (NpcMarkKey, Q) marks it
//    for the NPCs (npcai.cpp NpcMarkEnemy; the same one again: let go) instead of turning the map, and H (focus fire)
//    marks it and sends the selected squads at it in one press. With no enemy under the pointer Q turns the map and H
//    focuses on the mark there is (made on foot or here).
// The pointer's point: the map ray under it (mapcmd_logic.h ScreenRay on the view the HUD last drew), else that ray's
// meeting with the level ground through the focus (RayLevel).
// Online the commands are off (InSession): the plugin's AI runs on each machine for its own copies (the call aircraft
// have no network identity, docs/online-re.md §1-3), so an order given here would change this machine's copy alone.
#include "crew.h"
#include "formation.h"
#include "map_buttons.h"
#include "hudtext.h"
#include "npcai.h"
#include "layout.h"
#include "memory.h"
#include "map_cam.h"
#include "npc_mark.h"
#include "support_call.h"
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
constexpr float kFormationSpacing=30.0f; // m between the slots of a formation round a guard point

enum class Owner : std::uint8_t { heli, jet, ground, squad, tank };
struct Entry { CommandUnit u; Owner owner; };

struct Keys { bool tab,shift,ctrl,guard,follow,release,left,right,padNext,padGuard,padFollow,padRelease,
             engage,focus,board,dismount,dismiss,recruit,mark,formation,split,merge,sweep,health,digit[9],supportPrev,supportNext,supportCall; };

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
    bool guardArmed,guardClick; // the guard button clicked: the next click on the ground (guardClick) is its point
    int supportPick;
    bool supportArmed,supportClick;
    ObjRef hover;               // original identity under the pointer; never recaptured from a cached address
    float hoverAt[3];
    bool eat,eatWas;            // the mark key's press took the enemy under the pointer: not the map's (MapCommandEats)
    ObjRef eatHover;            // ...that enemy's original identity, its lock point
    float eatAt[3];
    enum class UiKind : std::uint8_t { none,command,squad,payload,panel };
    struct UiHit { UiKind kind=UiKind::none;int id=-1;ObjRef identity{};std::uint64_t token=0;int seat=-1,entry=-1; };
    UiHit uiPress{};
    bool uiLeft=false,uiRight=false,rowPicked=false;
};
Game game{};
std::atomic<bool> boxingNow{false};
std::atomic<bool> pointerCaptured{false};

// --- The view the HUD last drew the map with (under `viewLock`) ---
constexpr int kUiItems=16;
struct View {
    float vp[16],w,h; ULONGLONG at; int buttons; mapbtn::Rect button[mapbtn::kCount]; int id[mapbtn::kCount];
    int squads=0,payloads=0,panels=0;
    mapbtn::Rect squad[kUiItems]{},payload[kUiItems]{},panel[kUiItems]{};
    ObjRef squadIdentity[kUiItems]{};
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

bool Give(const Entry& e,const Command& c) noexcept {
    switch(e.owner) {
    case Owner::heli: return HeliCommand(e.u.v,c);
    case Owner::jet: return JetCommand(e.u.v,c);
    case Owner::ground: return GroundCommand(e.u.v,c);
    case Owner::squad: return SquadCommand(e.u.v,c);
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
// The ground under (x, z) on the level of height `y` a unit can stand on (a formation slot round a guard point in a
// cave: its floor, not the roof over it nor a level above; over a building: its roof, not the ground inside it), else `y`.
float GroundAt(float x,float z,float y) noexcept {
    float h;
    return MapGroundNear(x,z,y,&h,true) ? h : y;
}

Keys ReadKeys(const MapCmdInput& in) noexcept {
    Keys k{};
    if(in.front) {
        k.tab=Down(VK_TAB);k.shift=Down(VK_SHIFT);k.ctrl=Down(VK_CONTROL);
        k.guard=Down('G');k.follow=Down('V');k.release=Down('X');k.left=Down(VK_LBUTTON);k.right=Down(VK_RBUTTON);
        // The squads' orders (§6.1): keys the map's own camera does not use (WASD QE RF are its pan / turn / tilt).
        k.engage=Down('J');k.focus=Down('H');k.board=Down('B');k.dismount=Down('N');k.dismiss=Down('K');k.recruit=Down('U');k.formation=Down('T');k.split=Down('P');k.merge=Down('L');k.sweep=Down('Y');k.health=Down('O');
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
    default: return hudtext::Tr(Tx::orderRelease);
    }
}
// Whether `e` takes order `o` at all: a locked squad (a script's) none, a vehicle only guard / follow / release, a tank
// no follow (it keeps a post).
bool Takes(const Entry& e,Order o) noexcept {
    if(e.u.locked)return false;
    if(e.owner==Owner::squad)return true;
    if(e.owner==Owner::tank)return o==Order::guard || o==Order::none;
    return mapcmd::VehicleOrder(o);
}

void Note(Game& g,const wchar_t* format,...) noexcept {
    va_list a;va_start(a,format);
    _vsnwprintf_s(g.note,_countof(g.note),_TRUNCATE,format,a);
    va_end(a);
    g.noteAt=GetTickCount64();
}

// How tall a ground icon's pin stands in the camera of `in` (hud.cpp MapPin).
float PinOf(const MapCmdInput& in) noexcept {
    const float d[3]={in.look[0]-in.eye[0],in.look[1]-in.eye[1],in.look[2]-in.eye[2]};
    const float dist=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    const float pitch=dist>1e-3f ? std::asin(mapcam::Clamp(-d[1]/dist,-1.0f,1.0f)) : mapcam::kStartPitch;
    return mapcam::PinHeight(dist,pitch);
}

// The units' icons on the screen of view `v` (a ground unit's at the top of its pin, as hud.cpp MapPin draws it).
int Marks(const Game& g,const View& v,const MapCmdInput& in,mapcmd::Mark* out) noexcept {
    const float pin=PinOf(in);
    for(int i=0;i<g.count;++i) {
        const Entry& e=g.list[i];
        float p[3];std::memcpy(p,e.u.pos,12);
        if(!e.u.air)p[1]+=pin;
        out[i]=mapcmd::Mark{e.u.v,0.0f,0.0f,false};
        out[i].on=mapcmd::Project(v.vp,p,v.w,v.h,&out[i].x,&out[i].y);
    }
    return g.count;
}

Game::UiHit UiAt(const View& v,float x,float y) noexcept {
    Game::UiHit hit;
    int i=mapbtn::Hit(v.button,v.buttons,x,y);
    if(i>=0){hit.kind=Game::UiKind::command;hit.id=v.id[i];return hit;}
    i=mapbtn::Hit(v.squad,v.squads,x,y);
    if(i>=0){hit.kind=Game::UiKind::squad;hit.identity=v.squadIdentity[i];return hit;}
    i=mapbtn::Hit(v.payload,v.payloads,x,y);
    if(i>=0){hit.kind=Game::UiKind::payload;hit.token=v.payloadToken;hit.seat=v.payloadSeat;hit.entry=v.payloadEntry[i];return hit;}
    if(mapbtn::Hit(v.panel,v.panels,x,y)>=0)hit.kind=Game::UiKind::panel;
    return hit;
}
bool SameUi(const Game::UiHit& a,const Game::UiHit& b) noexcept {
    return a.kind==b.kind && a.id==b.id && a.identity.obj==b.identity.obj && a.identity.ctrl==b.identity.ctrl &&
        a.token==b.token && a.seat==b.seat && a.entry==b.entry;
}
void UiClick(Game& g,const Game::UiHit& hit,bool shift) noexcept {
    if(hit.kind==Game::UiKind::command){g.button=hit.id;return;}
    if(hit.kind==Game::UiKind::payload) {
        const bool queued=RequestPayloadSelection(hit.token,hit.seat,hit.entry);
        Note(g,L"%ls",hudtext::Tr(queued ? hudtext::Tx::cmdPayloadQueued : hudtext::Tx::cmdPayloadStale));return;
    }
    if(hit.kind!=Game::UiKind::squad)return;
    // Resolve the original rendered identity in this frame's validated list. Never recapture from
    // its raw address or reinterpret a row number after sort/recruitment changes the next snapshot.
    for(int i=0;i<g.count;++i)if(g.list[i].owner==Owner::squad && g.list[i].u.v==hit.identity.obj) {
        if(!Readable(hit.identity.obj,kSelfCtrl+sizeof(void*)) || !hit.identity.Is(hit.identity.obj))return;
        if(!shift)g.sel.Clear();
        if(shift && g.sel.Has(hit.identity.obj))g.sel.Remove(hit.identity.obj);else g.sel.Add(hit.identity.obj);
        g.rowPicked=g.sel.n==1;return;
    }
}

// UI owns presses that began on a rendered panel, including its inert background. Capture remains
// until release outside the panel; a changed identity/token between press and release cancels action.
void Pointer(Game& g,const MapCmdInput& in,const Keys& k,const View* v) noexcept {
    if(!v) {
        g.boxing=g.pressing=false;g.uiLeft=g.uiLeft && k.left;g.uiRight=g.uiRight && k.right;
        pointerCaptured.store(g.uiLeft || g.uiRight);return;
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
    if(k.right && !g.was.right)g.uiRight=UiAt(*v,g.pointer.x,g.pointer.y).kind!=Game::UiKind::none;
    if(!k.right)g.uiRight=false;
    if(k.left && !g.was.left) {
        g.uiPress=UiAt(*v,g.pointer.x,g.pointer.y);g.uiLeft=g.uiPress.kind!=Game::UiKind::none;g.moved=0;
        if(g.uiLeft)g.boxing=g.pressing=false;
        else if(k.ctrl){g.boxing=true;g.bx=g.pointer.x;g.by=g.pointer.y;}
        else g.pressing=true;
    }
    pointerCaptured.store(g.uiLeft || g.uiRight);
    if(k.left || !g.was.left)return;
    if(g.uiLeft) {
        if(g.moved<kClickMove && SameUi(g.uiPress,UiAt(*v,g.pointer.x,g.pointer.y)))UiClick(g,g.uiPress,k.shift);
        g.uiLeft=false;g.uiPress={};g.boxing=g.pressing=false;pointerCaptured.store(g.uiRight);return;
    }
    mapcmd::Mark marks[kCmdUnits];const int n=Marks(g,*v,in,marks);
    const bool box=g.boxing && (std::fabs(g.pointer.x-g.bx)>=kClickBox*s || std::fabs(g.pointer.y-g.by)>=kClickBox*s);
    const bool click=(g.boxing && !box) || (g.pressing && g.moved<kClickMove);
    if(click && UiAt(*v,g.pointer.x,g.pointer.y).kind!=Game::UiKind::none){} // release over UI never selects what is underneath
    else if(click && g.supportArmed && !g.boxing)g.supportClick=true;
    else if(click && g.guardArmed && !g.boxing)g.guardClick=true;
    else if(box)mapcmd::Box(g.sel,marks,n,g.bx,g.by,g.pointer.x,g.pointer.y,k.shift);
    else if(click)mapcmd::Click(g.sel,marks,n,g.pointer.x,g.pointer.y,kClickRadius*s,k.shift);
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

// The command to every selected unit; a guard's formation round the point (helis sharing one orbit stay on it).
int Issue(Game& g,const Command& cmd,int* skipped) noexcept {
    int k=0;
    *skipped=0;
    for(int i=0;i<g.count;++i)k+=g.sel.Has(g.list[i].u.v) && Takes(g.list[i],cmd.order) ? 1 : 0;
    const bool share=HeliSharesPost();
    int given=0,slot=0;
    for(int i=0;i<g.count;++i) {
        Entry& e=g.list[i];
        if(!g.sel.Has(e.u.v))continue;
        if(!Takes(e,cmd.order)){++*skipped;continue;}
        Command c=cmd;
        if(cmd.order==Order::guard && !(e.owner==Owner::heli && share)) {
            mapcmd::Formation(slot++,k,cmd.at,kFormationSpacing,c.at);
            c.at[1]=GroundAt(c.at[0],c.at[2],cmd.at[1]);
        }
        if(!Give(e,c)){++*skipped;continue;}
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
    SquadRow rows[16]{};
    const int squads=SquadRows(rows,16);
    AcquireSRWLockExclusive(&lock);
    MapCommandReadout& r=readout;
    r.allowed=allowed;r.all=mapcmd::IsAll(g.sel,g.count);r.selected=g.sel.n;r.pointOk=pointOk;
    std::memcpy(r.point,point,12);
    r.pointer=pointer;r.px=g.pointer.x;r.py=g.pointer.y;r.boxing=pointer && g.boxing;r.bx=g.bx;r.by=g.by;
    r.count=g.count;
    for(int i=0;i<g.count;++i) {
        const Entry& e=g.list[i];
        CmdMark& m=r.unit[i];
        std::memcpy(m.pos,e.u.pos,12);
        m.now=e.u.now;m.air=e.u.air;m.selected=g.sel.Has(e.u.v);m.locked=e.u.locked;
        m.owner=e.owner==Owner::heli ? kCmdOwnerHeli : e.owner==Owner::jet ? kCmdOwnerJet : kCmdOwnerGround;
        std::snprintf(m.name,sizeof(m.name),"%s",e.u.name ? e.u.name : "?");
    }
    r.squads=squads;
    std::memcpy(r.squad,rows,sizeof(rows));
    for(int i=0;i<r.squads;++i)r.squadSelected[i]=g.sel.Has(r.squad[i].leader);
    r.hover=static_cast<bool>(g.hover);
    std::memcpy(r.hoverAt,g.hoverAt,12);
    std::memcpy(r.note,g.note,sizeof(r.note));
    r.sweepOn=NpcSweepOn();r.healthOn=NpcPickupHealthOn();r.guardArmed=g.guardArmed;r.march=NpcMarchShape();
    r.supportArmed=g.supportArmed;
    _snwprintf_s(r.supportName,_countof(r.supportName),_TRUNCATE,L"%ls",SupportCallName(g.supportPick));
    SupportCallStatus(r.supportStatus,_countof(r.supportStatus));
    r.noteFresh=g.noteAt && GetTickCount64()-g.noteAt<=kNoteMs;
    readoutAt=GetTickCount64();
    ReleaseSRWLockExclusive(&lock);
}
}  // namespace

bool MapCommandFrame(MapCmdInput& in,float* centre) noexcept {
    Game& g=game;
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
        g.was=k;g.boxing=g.pressing=g.uiLeft=g.uiRight=false;g.uiPress={};pointerCaptured.store(false);g.guardArmed=g.supportArmed=false;npcmark::Assign(g.hover,{});
        g.pointer=mapcmd::PointerPosition{};
    }
    g.frameAt=now;
    const bool mouseOrKey=(in.mouse && (in.dx!=0.0f || in.dy!=0.0f)) || (k.left && !g.was.left) ||
        (k.tab && !g.was.tab) || (k.guard && !g.was.guard) || (k.follow && !g.was.follow) || (k.release && !g.was.release) ||
        (k.supportCall && !g.was.supportCall) || (k.supportPrev && !g.was.supportPrev) || (k.supportNext && !g.was.supportNext);
    const bool padPress=(k.padNext && !g.was.padNext) || (k.padGuard && !g.was.padGuard) ||
        (k.padFollow && !g.was.padFollow) || (k.padRelease && !g.was.padRelease);
    in.usingPad=mapcmd::UsingPad(in.usingPad,mouseOrKey,padPress);
    const void* ids[kCmdUnits];
    Refresh(g,ids);
    g.button=-1;g.guardClick=g.supportClick=false;g.rowPicked=false;
    Pointer(g,in,k,haveView ? &v : nullptr);
    using mapbtn::Id;
    const auto clicked=[&](Id b){return g.button==static_cast<int>(b);};
    if(clicked(Id::guard)) {   // armed: the next click on the ground; clicked again: not
        g.supportArmed=false;
        g.guardArmed=!g.guardArmed;
        if(g.guardArmed)Note(g,L"%ls",hudtext::Tr(hudtext::Tx::cmdGuardArmed));
    }
    const int supportCount=SupportCallCount();
    if(supportCount>0) {
        const bool previous=clicked(Id::supportPrev) || (k.supportPrev && !g.was.supportPrev);
        const bool following=clicked(Id::supportNext) || (k.supportNext && !g.was.supportNext);
        if(previous || following) {
            g.supportPick=(g.supportPick+(previous ? -1 : 1)+supportCount)%supportCount;
            Note(g,hudtext::Tr(hudtext::Tx::cmdSupportPicked),SupportCallName(g.supportPick));
        }
        if(clicked(Id::supportCall)) {
            g.guardArmed=false;g.supportArmed=!g.supportArmed;
            if(g.supportArmed)Note(g,L"%ls",hudtext::Tr(hudtext::Tx::cmdSupportPlace));
        }
    }
    Hover(g,in,haveView ? &v : nullptr);
    boxingNow.store(g.boxing);
    const bool next=(k.tab && !g.was.tab && !k.shift) || (k.padNext && !g.was.padNext),prev=k.tab && !g.was.tab && k.shift;
    bool picked=g.rowPicked;
    if(next || prev) {
        mapcmd::Cycle(g.sel,ids,g.count,next ? 1 : -1);
        picked=g.sel.n==1;
    }
    // The number keys pick a squad of the panel (its rows in SquadRows' order; Shift adds or takes out).
    SquadRow rows[9];
    const int rowCount=SquadRows(rows,9);
    for(int d=0;d<rowCount;++d) {
        if(!k.digit[d] || g.was.digit[d])continue;
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
                          k.dismount && !g.was.dismount,k.dismiss && !g.was.dismiss,k.recruit && !g.was.recruit};
    p.guard=p.guard || g.guardClick;
    if(g.guardClick)g.guardArmed=false;
    p.follow=p.follow || clicked(Id::follow);p.release=p.release || clicked(Id::release);p.engage=p.engage || clicked(Id::engage);
    p.focus=p.focus || clicked(Id::focus);p.board=p.board || clicked(Id::board);p.dismount=p.dismount || clicked(Id::dismount);
    p.dismiss=p.dismiss || clicked(Id::dismiss);p.recruit=p.recruit || clicked(Id::recruit);
    const bool formation=(k.formation && !g.was.formation) || clicked(Id::formation),split=(k.split && !g.was.split) || clicked(Id::split),
               merge=(k.merge && !g.was.merge) || clicked(Id::merge),sweep=(k.sweep && !g.was.sweep) || clicked(Id::sweep),
               health=(k.health && !g.was.health) || clicked(Id::health);
    const bool markPress=k.mark && !g.was.mark;
    const bool supportPress=g.supportClick || (k.supportCall && !g.was.supportCall);
    g.was=k;
    float point[3];
    const bool pointOk=TargetPoint(g,in,haveView ? &v : nullptr,point);
    const bool allowed=!InSession();
    if(supportPress) {
        g.supportArmed=false;
        if(pointOk){SupportCallAt(g.supportPick,point,g.note,_countof(g.note));g.noteAt=GetTickCount64();}
        else Note(g,L"%ls",hudtext::Tr(hudtext::Tx::cmdSupportNoPoint));
    }
    using hudtext::Tr;
    using hudtext::Tx;
    // The enemy under the pointer: the mark key marks it (or lets it go), the focus order marks it first.
    if(markPress && g.eat && npcmark::Enabled() && npcmark::Alive(g.eatHover))
        Note(g,L"%ls",Tr(NpcMarkEnemy(g.eatHover.obj,g.eatAt,true) ? Tx::cmdMarked : Tx::cmdUnmarked));
    if(p.focus && npcmark::Alive(g.hover) && allowed && g.sel.n)NpcMarkEnemy(g.hover.obj,g.hoverAt,false);
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
        if(s.cmd.order==Order::guard)Note(g,Tr(Tx::cmdGuardResult),OrderText(s.cmd.order),s.cmd.at[0],s.cmd.at[2],given,tail);
        else Note(g,Tr(Tx::cmdOrderResult),OrderText(s.cmd.order),given,tail);
        Log("MAPCMD %s (%.0f,%.0f,%.0f) to %d selected: %d of %d units took it",OrderName(s.cmd.order),s.cmd.at[0],s.cmd.at[1],s.cmd.at[2],
            g.sel.n,given,g.count);
    }
    if(formation)Formation(g,allowed);
    if(split || merge)Teams(g,allowed,split);
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

bool MapCommandBoxing() noexcept { return boxingNow.load(); }
bool MapCommandPointerCaptured() noexcept { return pointerCaptured.load(); }

void MapCommandSquadButtons(const float* rects,const ObjRef* identities,int n) noexcept {
    n=rects && identities ? (std::max)(0,(std::min)(n,kUiItems)) : 0;
    AcquireSRWLockExclusive(&viewLock);view.squads=n;
    for(int i=0;i<n;++i){view.squad[i]={rects[i*4],rects[i*4+1],rects[i*4+2],rects[i*4+3]};view.squadIdentity[i]=identities[i];}
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
    g.frameAt=0;g.eat=g.eatWas=false;g.boxing=g.pressing=false;g.guardArmed=g.guardClick=false;g.supportArmed=g.supportClick=false;g.button=-1;g.uiLeft=g.uiRight=false;g.uiPress={};
    boxingNow.store(false);pointerCaptured.store(false);
    AcquireSRWLockExclusive(&viewLock);view.at=0;view.buttons=view.squads=view.payloads=view.panels=0;ReleaseSRWLockExclusive(&viewLock);
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
    if(InSession())return -2;
    Game& g=game;
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
