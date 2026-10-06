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
// The pointer's point: the map ray under it (mapcmd_logic.h ScreenRay on the view the HUD last drew), else that ray's
// meeting with the level ground through the focus (RayLevel).
// Online the commands are off (InSession): the plugin's AI runs on each machine for its own copies (the call aircraft
// have no network identity, docs/online-re.md §1-3), so an order given here would change this machine's copy alone.
#include "crew.h"
#include "layout.h"
#include "map_cam.h"
#include <Xinput.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace crew {
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

struct Keys { bool tab,shift,ctrl,guard,follow,release,left,padNext,padGuard,padFollow,padRelease,
             engage,focus,board,dismount,dismiss,recruit,digit[9]; };

// --- The game thread's own ---
struct Game {
    mapcmd::Selection sel;
    Keys was;
    ULONGLONG frameAt;          // wall ms of the last frame (a gap: the map was closed, no edges the first frame)
    int count;
    Entry list[kCmdUnits];
    float px,py;                // the pointer (screen px)
    bool boxing,pressing;       // a Ctrl + left drag / a plain left press (a click unless it moves kClickMove)
    float bx,by,moved;
    wchar_t note[80];
    ULONGLONG noteAt;
};
Game game{};
std::atomic<bool> boxingNow{false};

// --- The view the HUD last drew the map with (under `viewLock`) ---
struct View { float vp[16],w,h; ULONGLONG at; };
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

const float* PosOf(const void* v) noexcept { return reinterpret_cast<const float*>(static_cast<const unsigned char*>(v)+kPosition); }

// The ground point along the ray from `eye` along unit `dir`: the map ray's hit, else the level plane at `level`.
bool GroundAlong(const float* eye,const float* dir,float level,float* point) noexcept {
    const float end[3]={eye[0]+dir[0]*kPointFar,eye[1]+dir[1]*kPointFar,eye[2]+dir[2]*kPointFar};
    float hit[3];
    if(MapRay(eye,end,hit)>=0.0f && std::isfinite(hit[0]+hit[1]+hit[2])){std::memcpy(point,hit,12);return true;}
    return mapcmd::RayLevel(eye,dir,level,point);
}
// The ground's height under (x, z), else `fallback`.
float GroundAt(float x,float z,float fallback) noexcept {
    const float top[3]={x,fallback+2000.0f,z},bottom[3]={x,fallback-2000.0f,z};
    float hit[3];
    return MapRay(top,bottom,hit)>=0.0f && std::isfinite(hit[1]) ? hit[1] : fallback;
}

Keys ReadKeys(const MapCmdInput& in) noexcept {
    Keys k{};
    if(in.front) {
        k.tab=Down(VK_TAB);k.shift=Down(VK_SHIFT);k.ctrl=Down(VK_CONTROL);
        k.guard=Down('G');k.follow=Down('V');k.release=Down('X');k.left=Down(VK_LBUTTON);
        // The squads' orders (§6.1): keys the map's own camera does not use (WASD QE RF are its pan / turn / tilt).
        k.engage=Down('J');k.focus=Down('H');k.board=Down('B');k.dismount=Down('N');k.dismiss=Down('K');k.recruit=Down('U');
        for(int d=0;d<9;++d)k.digit[d]=Down('1'+d);
    }
    if(in.pad) {
        const WORD b=in.buttons;
        k.padNext=(b&XINPUT_GAMEPAD_X)!=0;k.padGuard=(b&XINPUT_GAMEPAD_Y)!=0;
        k.padFollow=(b&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0;k.padRelease=(b&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0;
    }
    return k;
}

const wchar_t* OrderName(Order o) noexcept {
    switch(o) {
    case Order::guard: return L"GUARD";
    case Order::follow: return L"FOLLOW";
    case Order::engage: return L"ENGAGE";
    case Order::focus: return L"FOCUS FIRE";
    case Order::board: return L"BOARD";
    case Order::dismount: return L"DISMOUNT";
    case Order::dismiss: return L"DISMISS";
    case Order::recruit: return L"RECRUIT";
    case Order::none: break;
    }
    return L"RELEASE";
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

// The units' icons on the screen of view `v` (a ground unit's at the top of its pin, as hud.cpp MapPin draws it).
int Marks(const Game& g,const View& v,const MapCmdInput& in,mapcmd::Mark* out) noexcept {
    const float d[3]={in.look[0]-in.eye[0],in.look[1]-in.eye[1],in.look[2]-in.eye[2]};
    const float dist=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    const float pitch=dist>1e-3f ? std::asin(mapcam::Clamp(-d[1]/dist,-1.0f,1.0f)) : mapcam::kStartPitch;
    const float pin=mapcam::PinHeight(dist,pitch);
    for(int i=0;i<g.count;++i) {
        const Entry& e=g.list[i];
        float p[3];std::memcpy(p,PosOf(e.u.v),12);
        if(!e.u.air)p[1]+=pin;
        out[i]=mapcmd::Mark{e.u.v,0.0f,0.0f,false};
        out[i].on=mapcmd::Project(v.vp,p,v.w,v.h,&out[i].x,&out[i].y);
    }
    return g.count;
}

// The pointer, the box and the clicks (the mouse; `v`: the view, or nullptr while the HUD has drawn none).
void Pointer(Game& g,const MapCmdInput& in,const Keys& k,const View* v) noexcept {
    if(!v){g.boxing=g.pressing=false;return;}
    const float s=v->h/1080.0f;
    // The pointer moves with the mouse unless a button moves the map (the ground slides under it); a box drags it.
    const bool right=in.front && Down(VK_RBUTTON);
    if(in.mouse && (g.boxing || (!k.left && !right))) {
        g.px=mapcam::Clamp(g.px+in.dx*kPointerGain*s,0.0f,v->w-1.0f);
        g.py=mapcam::Clamp(g.py+in.dy*kPointerGain*s,0.0f,v->h-1.0f);
    }
    if(in.mouse && g.pressing)g.moved+=std::fabs(in.dx)+std::fabs(in.dy);
    if(k.left && !g.was.left) {
        if(k.ctrl){g.boxing=true;g.bx=g.px;g.by=g.py;}
        else{g.pressing=true;g.moved=0.0f;}
    }
    if(k.left || !g.was.left)return;
    // Let go: a box, or a click (a Ctrl box too small to be one, or a plain press that did not pan).
    mapcmd::Mark marks[kCmdUnits];
    const int n=Marks(g,*v,in,marks);
    const bool box=g.boxing && (std::fabs(g.px-g.bx)>=kClickBox*s || std::fabs(g.py-g.by)>=kClickBox*s);
    const bool click=(g.boxing && !box) || (g.pressing && g.moved<kClickMove);
    if(box)mapcmd::Box(g.sel,marks,n,g.bx,g.by,g.px,g.py,k.shift);
    else if(click)mapcmd::Click(g.sel,marks,n,g.px,g.py,kClickRadius*s,k.shift);
    g.boxing=g.pressing=false;
}

// The point G sends the selection to: under the pointer (mouse), at the screen's centre (pad, or no view yet).
bool TargetPoint(const Game& g,const MapCmdInput& in,const View* v,float* point) noexcept {
    std::memcpy(point,in.look,12);
    float eye[3],dir[3];
    if(!in.usingPad && v && mapcmd::ScreenRay(v->vp,v->w,v->h,g.px,g.py,eye,dir))return GroundAlong(eye,dir,in.look[1],point);
    float c[3]={in.look[0]-in.eye[0],in.look[1]-in.eye[1],in.look[2]-in.eye[2]};
    const float len=std::sqrt(c[0]*c[0]+c[1]*c[1]+c[2]*c[2]);
    if(!(len>1e-3f) || !std::isfinite(len))return false;
    for(float& x:c)x/=len;
    return GroundAlong(in.eye,c,in.look[1],point);
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
        if(!Give(e,c))continue;
        e.u.now=c;
        ++given;
    }
    return given;
}

void Publish(const Game& g,bool allowed,bool pointOk,const float* point,bool pointer) noexcept {
    // The game's memory is read before the lock is taken: a fault in a read (a unit gone between its listing and here)
    // must not leave the lock held, which would block the draw thread for good.
    float pos[kCmdUnits][3];
    for(int i=0;i<g.count;++i)std::memcpy(pos[i],PosOf(g.list[i].u.v),12);
    SquadRow rows[16];
    const int squads=SquadRows(rows,16);
    AcquireSRWLockExclusive(&lock);
    MapCommandReadout& r=readout;
    r.allowed=allowed;r.all=mapcmd::IsAll(g.sel,g.count);r.selected=g.sel.n;r.pointOk=pointOk;
    std::memcpy(r.point,point,12);
    r.pointer=pointer;r.px=g.px;r.py=g.py;r.boxing=pointer && g.boxing;r.bx=g.bx;r.by=g.by;
    r.count=g.count;
    for(int i=0;i<g.count;++i) {
        const Entry& e=g.list[i];
        CmdMark& m=r.unit[i];
        std::memcpy(m.pos,pos[i],12);
        m.now=e.u.now;m.air=e.u.air;m.selected=g.sel.Has(e.u.v);m.locked=e.u.locked;
        std::snprintf(m.name,sizeof(m.name),"%s%s",e.owner==Owner::heli ? "HELI " : e.owner==Owner::jet ? "JET " : "",e.u.name ? e.u.name : "?");
    }
    r.squads=squads;
    std::memcpy(r.squad,rows,sizeof(rows));
    for(int i=0;i<r.squads;++i)r.squadSelected[i]=g.sel.Has(r.squad[i].leader);
    std::memcpy(r.note,g.note,sizeof(r.note));
    r.noteFresh=g.noteAt && GetTickCount64()-g.noteAt<=kNoteMs;
    readoutAt=GetTickCount64();
    ReleaseSRWLockExclusive(&lock);
}
}  // namespace

bool MapCommandFrame(const MapCmdInput& in,float* centre) noexcept {
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
        g.was=k;g.boxing=g.pressing=false;
        g.px=haveView ? v.w*0.5f : 960.0f;g.py=haveView ? v.h*0.5f : 540.0f;
    }
    g.frameAt=now;
    List(g);
    const void* ids[kCmdUnits];
    for(int i=0;i<g.count;++i)ids[i]=g.list[i].u.v;
    mapcmd::Keep(g.sel,ids,g.count);
    Pointer(g,in,k,haveView ? &v : nullptr);
    boxingNow.store(g.boxing);
    const bool next=(k.tab && !g.was.tab && !k.shift) || (k.padNext && !g.was.padNext),prev=k.tab && !g.was.tab && k.shift;
    bool picked=false;
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
    const mapcmd::Press p{next,prev,(k.guard && !g.was.guard) || (k.padGuard && !g.was.padGuard),
                          (k.follow && !g.was.follow) || (k.padFollow && !g.was.padFollow),
                          (k.release && !g.was.release) || (k.padRelease && !g.was.padRelease),
                          k.engage && !g.was.engage,k.focus && !g.was.focus,k.board && !g.was.board,
                          k.dismount && !g.was.dismount,k.dismiss && !g.was.dismiss,k.recruit && !g.was.recruit};
    g.was=k;
    float point[3];
    const bool pointOk=TargetPoint(g,in,haveView ? &v : nullptr,point);
    const bool allowed=!InSession();
    const mapcmd::Step s=mapcmd::Decide(g.sel.n,p,allowed,point,pointOk,NpcMarked());
    if(s.why==mapcmd::Refusal::online)Note(g,L"COMMANDS: OFFLINE ONLY");
    else if(s.why==mapcmd::Refusal::noUnit)Note(g,g.count ? L"SELECT UNITS FIRST (%ls)" : L"NO UNIT TO COMMAND",in.usingPad ? L"X" : L"CTRL+DRAG / CLICK / TAB");
    else if(s.why==mapcmd::Refusal::noPoint)Note(g,L"NO GROUND THERE");
    else if(s.why==mapcmd::Refusal::noMark)Note(g,L"MARK AN ENEMY FIRST (Q ON FOOT)");
    if(s.issue) {
        int skipped=0;
        const int given=Issue(g,s.cmd,&skipped);
        wchar_t tail[40]{};
        if(skipped)_snwprintf_s(tail,_countof(tail),_TRUNCATE,L" (%d CANNOT)",skipped);
        if(s.cmd.order==Order::guard)Note(g,L"%ls (%.0f, %.0f): %d UNIT%ls%ls",OrderName(s.cmd.order),s.cmd.at[0],s.cmd.at[2],given,given==1 ? L"" : L"S",tail);
        else Note(g,L"%ls: %d UNIT%ls%ls",OrderName(s.cmd.order),given,given==1 ? L"" : L"S",tail);
        Log("MAPCMD %ls (%.0f,%.0f,%.0f) to %d selected: %d of %d units took it",OrderName(s.cmd.order),s.cmd.at[0],s.cmd.at[1],s.cmd.at[2],
            g.sel.n,given,g.count);
    }
    Publish(g,allowed,pointOk,point,haveView && !in.usingPad);
    if(!picked)return false;
    std::memcpy(centre,PosOf(g.sel.id[0]),12);
    return std::isfinite(centre[0]+centre[1]+centre[2]);
}

void ResetMapCommands() noexcept {
    game=Game{};
    boxingNow.store(false);
    AcquireSRWLockExclusive(&lock);
    readoutAt=0;
    ReleaseSRWLockExclusive(&lock);
}

void MapCommandView(const float* viewProj,float width,float height) noexcept {
    AcquireSRWLockExclusive(&viewLock);
    std::memcpy(view.vp,viewProj,sizeof(view.vp));view.w=width;view.h=height;view.at=GetTickCount64();
    ReleaseSRWLockExclusive(&viewLock);
}

bool MapCommandBoxing() noexcept { return boxingNow.load(); }

bool PlayerMapCommands(MapCommandReadout* out) noexcept {
    AcquireSRWLockShared(&lock);
    const bool fresh=readoutAt && GetTickCount64()-readoutAt<=kFreshMs;
    if(fresh)*out=readout;
    ReleaseSRWLockShared(&lock);
    return fresh;
}
}  // namespace crew
