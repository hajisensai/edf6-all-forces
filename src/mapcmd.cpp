// Commanding the plugin's NPC units from the map view (README 地图 → 指挥 NPC; the user, 2026-10-06: "在地图上可以指挥
// npc"). While the map (map.cpp) is open:
//  - the units: the helis the plugin flies (heli.cpp), its jets, rotor carriers and drones with no carrier (jet.cpp), the
//    Depth Crawlers it drives (ground.cpp) -- each module lists the ones that take an order now (mapcmd.h);
//  - Tab / Shift+Tab (pad X) select the next / previous of them, then ALL (mapcmd_logic.h Cycle); the map centres on a
//    unit selected so;
//  - G (pad Y): guard the point at the screen's centre (go there, fight what comes within its range of it, circle /
//    patrol it); V (pad RB): follow the player; X (pad LB): release (back to what it did before any command).
// The point: the map ray from the camera's eye through its focus (the screen's centre), else that ray's meeting with the
// level ground through the focus (mapcmd_logic.h RayLevel).
// Online the commands are off (InSession): the plugin's AI runs on each machine for its own copies (the call aircraft
// have no network identity, docs/online-re.md §1-3), so an order given here would change this machine's copy alone.
#include "crew.h"
#include "layout.h"
#include <Xinput.h>
#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace crew {
namespace {
constexpr ULONGLONG kFreshMs=300;        // a readout older than this (wall) is the map closed
constexpr ULONGLONG kNoteMs=3000;        // the last command's word shown this long
constexpr float kPointFar=8000.0f;       // m: the map ray along the screen's centre reaches this far

enum class Owner : std::uint8_t { heli, jet, ground };
struct Entry { CommandUnit u; Owner owner; };

struct Keys { bool tab,shift,guard,follow,release,padNext,padGuard,padFollow,padRelease; };

// --- The game thread's own ---
struct Game {
    const void* sel;            // the selection (a vehicle, mapcmd::All(), nullptr)
    Keys was;
    ULONGLONG frameAt;          // wall ms of the last frame (a gap: the map was closed, no edges the first frame)
    int count;
    Entry list[kCmdUnits];
    wchar_t note[80];
    ULONGLONG noteAt;
};
Game game{};

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

// The ground point at the screen's centre (`eye` looking at `look`).
bool CentrePoint(const float* eye,const float* look,float* point) noexcept {
    float dir[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]};
    const float len=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
    if(!(len>1e-3f) || !std::isfinite(len))return false;
    for(float& d:dir)d/=len;
    const float end[3]={eye[0]+dir[0]*kPointFar,eye[1]+dir[1]*kPointFar,eye[2]+dir[2]*kPointFar};
    float hit[3];
    if(MapRay(eye,end,hit)>=0.0f && std::isfinite(hit[0]+hit[1]+hit[2])){std::memcpy(point,hit,12);return true;}
    return mapcmd::RayLevel(eye,dir,look[1],point);
}

Keys ReadKeys(bool front,bool pad,WORD b) noexcept {
    Keys k{};
    if(front){k.tab=Down(VK_TAB);k.shift=Down(VK_SHIFT);k.guard=Down('G');k.follow=Down('V');k.release=Down('X');}
    if(pad) {
        k.padNext=(b&XINPUT_GAMEPAD_X)!=0;k.padGuard=(b&XINPUT_GAMEPAD_Y)!=0;
        k.padFollow=(b&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0;k.padRelease=(b&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0;
    }
    return k;
}

const wchar_t* OrderName(Order o) noexcept { return o==Order::guard ? L"GUARD" : o==Order::follow ? L"FOLLOW" : L"RELEASE"; }

void Note(Game& g,const wchar_t* format,...) noexcept {
    va_list a;va_start(a,format);
    _vsnwprintf_s(g.note,_countof(g.note),_TRUNCATE,format,a);
    va_end(a);
    g.noteAt=GetTickCount64();
}

void Publish(const Game& g,bool allowed,bool pointOk,const float* point) noexcept {
    AcquireSRWLockExclusive(&lock);
    MapCommandReadout& r=readout;
    r.allowed=allowed;r.all=g.sel==mapcmd::All();r.pointOk=pointOk;
    std::memcpy(r.point,point,12);
    r.count=g.count;
    for(int i=0;i<g.count;++i) {
        const Entry& e=g.list[i];
        CmdMark& m=r.unit[i];
        std::memcpy(m.pos,static_cast<const unsigned char*>(e.u.v)+kPosition,12);
        m.now=e.u.now;m.air=e.u.air;m.selected=mapcmd::Targets(g.sel,e.u.v);
        std::snprintf(m.name,sizeof(m.name),"%s%s",e.owner==Owner::heli ? "HELI " : e.owner==Owner::jet ? "JET " : "",e.u.name ? e.u.name : "?");
    }
    std::memcpy(r.note,g.note,sizeof(r.note));
    r.noteFresh=g.noteAt && GetTickCount64()-g.noteAt<=kNoteMs;
    readoutAt=GetTickCount64();
    ReleaseSRWLockExclusive(&lock);
}
}  // namespace

bool MapCommandFrame(bool front,bool pad,WORD buttons,const float* eye,const float* look,float* centre) noexcept {
    Game& g=game;
    const ULONGLONG now=GetTickCount64();
    const Keys k=ReadKeys(front,pad,buttons);
    // The first frame after a gap (the map just opened): what is held now is no press (the key that opened it, a key
    // held from before).
    if(now-g.frameAt>kFreshMs)g.was=k;
    g.frameAt=now;
    const mapcmd::Press p{(k.tab && !g.was.tab && !k.shift) || (k.padNext && !g.was.padNext),k.tab && !g.was.tab && k.shift,
                          (k.guard && !g.was.guard) || (k.padGuard && !g.was.padGuard),
                          (k.follow && !g.was.follow) || (k.padFollow && !g.was.padFollow),
                          (k.release && !g.was.release) || (k.padRelease && !g.was.padRelease)};
    g.was=k;
    List(g);
    const void* ids[kCmdUnits];
    for(int i=0;i<g.count;++i)ids[i]=g.list[i].u.v;
    float point[3]={look[0],look[1],look[2]};
    const bool pointOk=CentrePoint(eye,look,point);
    const bool allowed=!InSession();
    const mapcmd::Step s=mapcmd::Decide(ids,g.count,g.sel,p,allowed,point,pointOk);
    const bool picked=s.sel!=g.sel && s.sel && s.sel!=mapcmd::All();
    g.sel=s.sel;
    if(s.why==mapcmd::Refusal::online)Note(g,L"COMMANDS: OFFLINE ONLY");
    else if(s.why==mapcmd::Refusal::noUnit)Note(g,g.count ? L"SELECT A UNIT FIRST (%ls)" : L"NO UNIT TO COMMAND",pad ? L"X" : L"TAB");
    else if(s.why==mapcmd::Refusal::noPoint)Note(g,L"NO GROUND AT THE CROSSHAIR");
    if(s.issue) {
        int given=0;
        for(int i=0;i<g.count;++i) {
            Entry& e=g.list[i];
            if(!mapcmd::Targets(g.sel,e.u.v) || !Give(e,s.cmd))continue;
            e.u.now=s.cmd;
            ++given;
        }
        if(s.cmd.order==Order::guard)Note(g,L"%ls (%.0f, %.0f): %d UNIT%ls",OrderName(s.cmd.order),s.cmd.at[0],s.cmd.at[2],given,given==1 ? L"" : L"S");
        else Note(g,L"%ls: %d UNIT%ls",OrderName(s.cmd.order),given,given==1 ? L"" : L"S");
        Log("MAPCMD %ls (%.0f,%.0f,%.0f) to %s: %d of %d units took it",OrderName(s.cmd.order),s.cmd.at[0],s.cmd.at[1],s.cmd.at[2],
            g.sel==mapcmd::All() ? "all" : "one",given,g.count);
    }
    Publish(g,allowed,pointOk,point);
    if(!picked)return false;
    std::memcpy(centre,static_cast<const unsigned char*>(g.sel)+kPosition,12);
    return std::isfinite(centre[0]+centre[1]+centre[2]);
}

void ResetMapCommands() noexcept {
    game=Game{};
    AcquireSRWLockExclusive(&lock);
    readoutAt=0;
    ReleaseSRWLockExclusive(&lock);
}

bool PlayerMapCommands(MapCommandReadout* out) noexcept {
    AcquireSRWLockShared(&lock);
    const bool fresh=readoutAt && GetTickCount64()-readoutAt<=kFreshMs;
    if(fresh)*out=readout;
    ReleaseSRWLockShared(&lock);
    return fresh;
}
}  // namespace crew
