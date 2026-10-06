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
#include "hudtext.h"
#include "layout.h"
#include "memory.h"
#include "map_cam.h"
#include <Xinput.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace crew {
// A recently stepped address is not a lifetime guarantee. Read game memory only before publishing, and tolerate a
// vehicle removed since the last input frame. In particular no external object is dereferenced under the HUD lock.
bool CommandVehicleLive(const ObjRef& ref) noexcept {
    __try {
        auto* v=static_cast<unsigned char*>(const_cast<void*>(ref.obj));
        return v && Readable(v,kSeatCount+8) && !v[kDead] && ref.Is(v) && SeatCount(v)>0 &&
               SeatRider(SeatAt(v,0))==Rider::dummy;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ReadCommandUnit(const ObjRef& ref,const char* name,const Command& cmd,bool air,CommandUnit* out) noexcept {
    __try {
        if(!CommandVehicleLive(ref))return false;
        CommandUnit unit{ref.obj,name,cmd,air,{}};
        std::memcpy(unit.pos,static_cast<const unsigned char*>(ref.obj)+kPosition,12);
        if(!std::isfinite(unit.pos[0]+unit.pos[1]+unit.pos[2]))return false;
        *out=unit;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
namespace {
constexpr ULONGLONG kFreshMs=300;        // a readout older than this (wall) is the map closed
constexpr ULONGLONG kNoteMs=3000;        // the last command's word shown this long
constexpr float kPointFar=8000.0f;       // m: the map ray under the pointer reaches this far
constexpr float kPointerGain=1.0f;       // px (at 1080 lines) a mouse unit
constexpr float kClickMove=6.0f;         // mouse units: a left press that moved less is a click, not a pan
constexpr float kClickBox=6.0f;          // px: a Ctrl box smaller than this both ways is a click
constexpr float kClickRadius=22.0f;      // px (at 1080 lines) round a unit's icon a click takes it
constexpr float kFormationSpacing=30.0f; // m between the slots of a formation round a guard point

enum class Owner : std::uint8_t { heli, jet, ground };
struct Entry { CommandUnit u; Owner owner; };

struct Keys { bool tab,shift,ctrl,guard,follow,release,left,padNext,padGuard,padFollow,padRelease; };

// --- The game thread's own ---
struct Game {
    mapcmd::Selection sel;
    Keys was;
    ULONGLONG frameAt;          // wall ms of the last frame (a gap: the map was closed, no edges the first frame)
    int count;
    Entry list[kCmdUnits];
    mapcmd::PointerPosition pointer; // placed once the first rendered viewport arrives
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
    std::sort(g.list,g.list+g.count,[](const Entry& a,const Entry& b){ return std::less<const void*>()(a.u.v,b.u.v); });
}

bool Give(const Entry& e,const Command& c) noexcept {
    switch(e.owner) {
    case Owner::heli: return HeliCommand(e.u.v,c);
    case Owner::jet: return JetCommand(e.u.v,c);
    case Owner::ground: return GroundCommand(e.u.v,c);
    }
    return false;
}

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
    }
    if(in.pad) {
        const WORD b=in.buttons;
        k.padNext=(b&XINPUT_GAMEPAD_X)!=0;k.padGuard=(b&XINPUT_GAMEPAD_Y)!=0;
        k.padFollow=(b&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0;k.padRelease=(b&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0;
    }
    return k;
}

// An order's name for the log (English) and its word on the HUD (the HUD's language).
const char* OrderName(Order o) noexcept { return o==Order::guard ? "GUARD" : o==Order::follow ? "FOLLOW" : "RELEASE"; }
const wchar_t* OrderText(Order o) noexcept {
    using hudtext::Tx;
    return hudtext::Tr(o==Order::guard ? Tx::orderGuard : o==Order::follow ? Tx::orderFollow : Tx::orderRelease);
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
        float p[3];std::memcpy(p,e.u.pos,12);
        if(!e.u.air)p[1]+=pin;
        out[i]=mapcmd::Mark{e.u.v,0.0f,0.0f,false};
        out[i].on=mapcmd::Project(v.vp,p,v.w,v.h,&out[i].x,&out[i].y);
    }
    return g.count;
}

// The pointer, the box and the clicks (the mouse; `v`: the view, or nullptr while the HUD has drawn none).
void Pointer(Game& g,const MapCmdInput& in,const Keys& k,const View* v) noexcept {
    if(!v){g.boxing=g.pressing=false;return;}
    if(mapcmd::FitPointer(g.pointer,v->w,v->h))g.boxing=g.pressing=false;
    const float s=v->h/1080.0f;
    // The pointer moves with the mouse unless a button moves the map (the ground slides under it); a box drags it.
    const bool right=in.front && Down(VK_RBUTTON);
    if(in.mouse && (g.boxing || (!k.left && !right))) {
        g.pointer.x=mapcam::Clamp(g.pointer.x+in.dx*kPointerGain*s,0.0f,v->w-1.0f);
        g.pointer.y=mapcam::Clamp(g.pointer.y+in.dy*kPointerGain*s,0.0f,v->h-1.0f);
    }
    if(in.mouse && g.pressing)g.moved+=std::fabs(in.dx)+std::fabs(in.dy);
    if(k.left && !g.was.left) {
        if(k.ctrl){g.boxing=true;g.bx=g.pointer.x;g.by=g.pointer.y;}
        else{g.pressing=true;g.moved=0.0f;}
    }
    if(k.left || !g.was.left)return;
    // Let go: a box, or a click (a Ctrl box too small to be one, or a plain press that did not pan).
    mapcmd::Mark marks[kCmdUnits];
    const int n=Marks(g,*v,in,marks);
    const bool box=g.boxing && (std::fabs(g.pointer.x-g.bx)>=kClickBox*s || std::fabs(g.pointer.y-g.by)>=kClickBox*s);
    const bool click=(g.boxing && !box) || (g.pressing && g.moved<kClickMove);
    if(box)mapcmd::Box(g.sel,marks,n,g.bx,g.by,g.pointer.x,g.pointer.y,k.shift);
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

// The command to every selected unit; a guard's formation round the point (helis sharing one orbit stay on it).
int Issue(Game& g,const Command& cmd) noexcept {
    int k=0;
    for(int i=0;i<g.count;++i)k+=g.sel.Has(g.list[i].u.v) ? 1 : 0;
    const bool share=HeliSharesPost();
    int given=0,slot=0;
    for(int i=0;i<g.count;++i) {
        Entry& e=g.list[i];
        if(!g.sel.Has(e.u.v))continue;
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
        m.now=e.u.now;m.air=e.u.air;m.selected=g.sel.Has(e.u.v);
        m.owner=e.owner==Owner::heli ? kCmdOwnerHeli : e.owner==Owner::jet ? kCmdOwnerJet : kCmdOwnerGround;
        std::snprintf(m.name,sizeof(m.name),"%s",e.u.name ? e.u.name : "?");
    }
    std::memcpy(r.note,g.note,sizeof(r.note));
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
        g.was=k;g.boxing=g.pressing=false;
        g.pointer=mapcmd::PointerPosition{};
    }
    g.frameAt=now;
    const bool mouseOrKey=(in.mouse && (in.dx!=0.0f || in.dy!=0.0f)) || (k.left && !g.was.left) ||
        (k.tab && !g.was.tab) || (k.guard && !g.was.guard) || (k.follow && !g.was.follow) || (k.release && !g.was.release);
    const bool padPress=(k.padNext && !g.was.padNext) || (k.padGuard && !g.was.padGuard) ||
        (k.padFollow && !g.was.padFollow) || (k.padRelease && !g.was.padRelease);
    in.usingPad=mapcmd::UsingPad(in.usingPad,mouseOrKey,padPress);
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
    const mapcmd::Press p{next,prev,(k.guard && !g.was.guard) || (k.padGuard && !g.was.padGuard),
                          (k.follow && !g.was.follow) || (k.padFollow && !g.was.padFollow),
                          (k.release && !g.was.release) || (k.padRelease && !g.was.padRelease)};
    g.was=k;
    float point[3];
    const bool pointOk=TargetPoint(g,in,haveView ? &v : nullptr,point);
    const bool allowed=!InSession();
    const mapcmd::Step s=mapcmd::Decide(g.sel.n,p,allowed,point,pointOk);
    using hudtext::Tr;
    using hudtext::Tx;
    if(s.why==mapcmd::Refusal::online)Note(g,L"%ls",Tr(Tx::cmdOfflineOnly));
    else if(s.why==mapcmd::Refusal::noUnit && g.count)Note(g,Tr(Tx::cmdSelectFirst),in.usingPad ? L"X" : Tr(Tx::cmdSelectHowMouse));
    else if(s.why==mapcmd::Refusal::noUnit)Note(g,L"%ls",Tr(Tx::cmdNoUnit));
    else if(s.why==mapcmd::Refusal::noPoint)Note(g,L"%ls",Tr(Tx::cmdNoGround));
    if(s.issue) {
        const int given=Issue(g,s.cmd);
        if(s.cmd.order==Order::guard)Note(g,Tr(given==1 ? Tx::cmdGuardOne : Tx::cmdGuardMany),OrderText(s.cmd.order),s.cmd.at[0],s.cmd.at[2],given);
        else Note(g,Tr(given==1 ? Tx::cmdGivenOne : Tx::cmdGivenMany),OrderText(s.cmd.order),given);
        Log("MAPCMD %s (%.0f,%.0f,%.0f) to %d selected: %d of %d units took it",OrderName(s.cmd.order),s.cmd.at[0],s.cmd.at[1],s.cmd.at[2],
            g.sel.n,given,g.count);
    }
    Publish(g,allowed,pointOk,point,haveView && !in.usingPad);
    if(!picked)return false;
    const int selected=mapcmd::IndexOf(ids,g.count,g.sel.id[0]);
    if(selected<0)return false;
    std::memcpy(centre,g.list[selected].u.pos,12);
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
