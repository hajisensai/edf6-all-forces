// The map view (README 地图; the user, 2026-10-06: "M opens a map with the terrain, zoomed in and out like a normal 3D
// map"). Design and RE: docs/camera-re.md §8. M (ini MapKey) or a pad button (MapButton, XInput bits) lifts the player's
// own camera into an overhead view of the real world: the game keeps drawing its terrain, buildings, vehicles and
// enemies, the mouse / left stick pans it, the right drag / right stick turns and tilts it, the wheel / +- / triggers
// zoom it from 200 m to 3 km; hud.cpp lays 3D pins over it (a grid with distances, north, the player, the squad and
// friendly vehicles, every enemy the stock radar finds, the plugin's aircraft and carriers, the locks, the objective
// markers). While it is open the player's controls are held (the game runs on: there is no pause the plugin may take).
//  - The hold (H): every soldier class's pre-update (slot 4) is 0x572DF0. Before it reads a pad it tests the human's
//    pad pointer (0x572F0C: mov rbx,[rsi+340h]; test rbx,rbx; je 0x573A4D); with none it takes the stock no-pad path
//    0x573A4D, which clears the human's input block (0x56D300 on +0xD50) and, riding, the seat's (0x62C120 on seat
//    +0x2C0): the human neither moves, looks nor fires, the vehicle gets no stick, trigger or buttons. Those 16 bytes
//    jump to a shim that calls MapHumanFrame(human) (the map's per-frame step, on the game thread, on foot or riding)
//    and takes the no-pad path while it says the map is open. (0x572EFF, the player test before it, stays as it is:
//    both plugins check its bytes.)
//  - The view (H for the layout, M that nothing after slot 4 rebuilds it): the player's CharacterGhostCamera (vtable
//    0x1768C10, its target the player: +0x360) builds its world matrix at +0x220 (rows; +0x250 the eye) in its per-frame
//    slot 4 (0xF86A0) through its state; the slot is chained and, the map open, the matrix is rebuilt after it by the
//    game's own look-to (0x4E220, as state 2 builds it at 0xFC0D3) from the map's eye to its focus, eased in and out.
//  - The far clip: view.cpp ViewMapClip (MapViewDistance) while it is open; the HUD keeps the last game view for the
//    aim (CameraRay), so a turret, a launcher or a sight does not swing onto the map's view.
//  - The marks: the friendly side by the team walk the board prompt makes (0x5E11D0, sidecar.cpp), the enemies by the
//    hostile walk the stock radar makes (0x5E0F20: large ones pins, small ones dots), the objective markers (DestinationMarker, vtable 0x17D4378: its
//    transform update, slot 3, puts it in a table, its destructor, slot 1, takes it out; its matrix at +0x1C0, L that
//    these are the mission's objective markers), gathered every kGatherMs.
//  - The wheel: the game window's procedure is subclassed for WM_MOUSEWHEEL (swallowed while the map is open); the
//    mouse's motion is the game's own per-frame delta (the pad record the rider aim reads, 0x56DCED: pad +0x66C / +0x684
//    of record [human+0xD40], before its /24); a pad is read through XInput (the hold clears the game's own pad input).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "layout.h"
#include "map_cam.h"
#include "map_marks.h"
#include "map_camera_state.h"
#include "map_stock_hud.h"
#include "memory.h"
#include "turretaim.h"
#include "tvguide.h"
#include "vecmath.h"
#include <Xinput.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace crew {
namespace {
// The hold: the soldier pre-update's pad test, its resume and the stock no-pad path.
constexpr unsigned kHoldAt=0x572F0C,kHoldResume=0x572F1C,kNoPad=0x573A4D;
const unsigned char kHoldCode[]={0x48,0x8B,0x9E,0x40,0x03,0x00,0x00,0x48,0x85,0xDB,0x0F,0x84,0x31,0x0B,0x00,0x00};
const unsigned char kNoPadCode[]={0xF6,0x46,0x1A,0x08,0x75,0x3C,0x0F,0xB6,0x86,0x28,0x01,0x00,0x00};
// The camera: CharacterGhostCamera's per-frame slot, its target soldier, its world matrix; the look-to it builds the
// matrix with (state 2's call at 0xFC0D3, then the rows copied to +0x220 and the eye to +0x250).
constexpr unsigned kCamVtable=0x1768C10,kCamStep=0xF86A0,kLookTo=0x4E220;
constexpr std::size_t kCamStepSlot=4,kCamTargetRef=0x350,kCamTarget=0x360,kCamMatrix=0x220,kCamEyeRow=0x250,kCamFwdRow=0x240;
const unsigned char kCamStepCode[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x90};
const unsigned char kLookToCode[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x81,0xEC,0x90,0x00,0x00,0x00,0x0F,0x10,0x0A};
const unsigned char kLookToUse[]={0xE8,0x48,0x21,0xF5,0xFF,0x0F,0x10,0x00,0x0F,0x11,0x86,0x20,0x02,0x00,0x00};   // 0xFC0D3
// The human: its pad's record index and the record's mouse delta (x, y) the rider aim reads; the vehicle it rides.
constexpr std::size_t kHumanRecord=0xD40,kRecordStride=0xA80,kMouseX=0x66C,kMouseY=0x684,kHumanVehicle=0x1548;
constexpr std::int32_t kRecords=4;
// The team walk (sidecar.cpp): 0x5E11D0(manager, team, functor) calls functor slot 1 with every object of every team
// friendly to `team` (relation 1) -- not team 5's. The one-team walk 0x5E0D60(manager, team, functor) the same for one
// team's set alone: the board prompt walks team 5 (nobody's vehicles) with it first (0x56D75C: mov edx,5; mov rcx,[mgr];
// call 0x5E0D60) and the friends after it (map_marks.h).
constexpr unsigned kTeamWalk=0x5E11D0,kTeamManager=0x20B2978,kOneTeamWalk=0x5E0D60,kBoardTeam5Call=0x56D75C;
const unsigned char kTeamWalkCode[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
const unsigned char kOneTeamWalkCode[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x41,0x56};
const unsigned char kBoardTeam5Code[]={0xBA,0x05,0x00,0x00,0x00,0x48,0x8B,0x0D,0x10,0x52,0xB4,0x01,0xE8,0xF3,0x35,0x07,0x00};
// The hostile walk the stock radar makes (HUiHudRader 0x82B4D0, at 0x82B8CA and 0x82BBB0): the same functor call for
// every live object of every team hostile to `team` (relation 2; the dead, +0x2E8, skipped).
constexpr unsigned kHostileWalk=0x5E0F20;
const unsigned char kHostileWalkCode[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x55,0x41,0x56};
const unsigned char kRadarCall[]={0x48,0x8B,0x0D,0xAE,0x70,0x88,0x01,0xE8,0x51,0x56,0xDB,0xFF};   // 0x82B8C3: mov rcx,[mgr]; call
// DestinationMarker: its vtable, destructor (slot 1), transform update (slot 3: lea rdx,[rcx+1C0h]; add rcx,120h; jmp).
constexpr unsigned kMarkerVtable=0x17D4378,kMarkerDtor=0x5B0410,kMarkerUpdate=0x5B2750;
constexpr std::size_t kMarkerDtorSlot=1,kMarkerUpdateSlot=3,kMarkerAt=0x1F0;
const unsigned char kMarkerUpdateCode[]={0x48,0x8D,0x91,0xC0,0x01,0x00,0x00,0x48,0x81,0xC1,0x20,0x01,0x00,0x00,0xE9};
constexpr int kMarkers=16;
// The stock HUD's switch (map_stock_hud.h, docs/hud-re.md §11): the camera's +0x200, written by 0x118DF30 (mov [rcx+200h],
// dl; ret) from the scripts' SetPlayerHudShow (0x1BA790, its call at kHudShowCall, once per viewport camera from the
// viewport table 0x1195BE0 walks); read by the HUD draws (HUiHud*Object 0x8168B0, the crosshair 0x8039F0, the rescue line
// 0x808410, each through its Hud's camera +0x18); the camera's per-frame call (0x118DF40) steps the camera through its
// slot 4 (the map's kCamStep: CharacterGhostCamera is such a camera) and its HUD list +0x208 on the same object.
constexpr unsigned kHudShow=0x118DF30,kHudShowCall=0x1BA811;
constexpr std::size_t kCamHudShown=0x200;
struct HudSig { unsigned rva; const unsigned char* bytes; std::size_t size; };
const unsigned char kHudShowCode[]={0x88,0x91,0x00,0x02,0x00,0x00,0xC3};
const unsigned char kHudShowUse[]={0x40,0x0F,0xB6,0xD6,0xE8,0x1A,0x37,0xFD,0x00};              // 0x1BA80D: movzx edx,sil; call
const unsigned char kHudShowLoop[]={0x8B,0xD7,0x48,0x8B,0x0D,0x8F,0x81,0xEF,0x01,0xE8,0x12,0xB4,0xFD,0x00};   // 0x1BA7C0: slot i
const unsigned char kHudObjectDraw[]={0x48,0x8B,0x49,0x18,0x80,0xB9,0x00,0x02,0x00,0x00,0x00,0x0F,0x84};   // 0x8168CF
const unsigned char kRescueDraw[]={0x48,0x8B,0x49,0x18,0x80,0xB9,0x00,0x02,0x00,0x00,0x00};               // 0x80843B
const unsigned char kCrossDraw[]={0x80,0xB9,0x00,0x02,0x00,0x00,0x00};                                   // 0x803AC9
const unsigned char kViewportStep[]={0x48,0x8B,0x01,0x48,0x8B,0xF2,0x48,0x8B,0xF9,0xFF,0x50,0x20,0x48,0x8B,0xBF,0x08,0x02,0x00,0x00};   // 0x118DF4F
const HudSig kHudSigs[]={{kHudShow,kHudShowCode,sizeof(kHudShowCode)},{0x1BA80D,kHudShowUse,sizeof(kHudShowUse)},
    {0x1BA7C0,kHudShowLoop,sizeof(kHudShowLoop)},{0x8168CF,kHudObjectDraw,sizeof(kHudObjectDraw)},
    {0x80843B,kRescueDraw,sizeof(kRescueDraw)},{0x803AC9,kCrossDraw,sizeof(kCrossDraw)},
    {0x118DF4F,kViewportStep,sizeof(kViewportStep)}};

constexpr ULONGLONG kFreshMs=300;       // a pose / readout older than this (wall) is the map closed (paused, loading)
constexpr ULONGLONG kGatherMs=100;      // the marks gathered this often
constexpr int kEaseIn=24,kEaseOut=15;   // frames the camera eases into the map's view and back out
constexpr float kEyeClear=40.0f;        // m: the eye kept at least this over the ground under it
constexpr float kStickDead=0.2f;
constexpr float kMouseMost=4000.0f;     // a mouse delta past this is no delta (an unread record)
constexpr float kFlyingClear=15.0f;     // m over the ground under it: an enemy flies (its pin's stem goes down)
constexpr float kLargeShare=6.0f;       // an enemy whose HP max is this many times the median of all of them is large
constexpr float kLargeHp=30000.0f;      // ...or this much HP max whatever the others have

using CamStepFn=void(__fastcall*)(void*,void*);
using LookToFn=float*(__fastcall*)(float*,const float*);
using WalkFn=void(__fastcall*)(void*,std::int32_t,void*);
using MarkerDtorFn=void*(__fastcall*)(void*,unsigned);
using MarkerUpdateFn=void*(__fastcall*)(void*,void*,void*,void*);
using XInputGetStateFn=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);

bool holdOk=false,camOk=false,walkOk=false,nobodysOk=false,hostileOk=false,markerOk=false;
CamStepFn nextCamStep=nullptr;
MarkerDtorFn nextMarkerDtor=nullptr;
MarkerUpdateFn nextMarkerUpdate=nullptr;

// --- What the game thread publishes (under `lock`) ---
struct Pose { bool open; const void* human; float eye[3],look[3]; ULONGLONG at; };
Pose pose{};
MapReadout readout{};
ULONGLONG readoutAt=0;
SRWLOCK lock=SRWLOCK_INIT;
std::atomic<bool> holds{false};
mapcam::CameraSession cameraSession;
std::atomic<int> wheel{0};   // WM_MOUSEWHEEL's delta since the game thread last took it

// --- The game thread's own ---
struct Keys { bool map,esc,centre,padMap,padClose,padCentre; };
struct Game {
    ObjRef human;               // the player the map is for
    bool open,follow,pad;
    bool draining;              // closed by a key still down: the input held until every closing key is let go
    mapcam::View view;
    Keys was;                   // the toggles as last read (a press is the edge)
    LARGE_INTEGER last;         // the last frame (dt)
    ULONGLONG frameAt,gatherAt;  // wall ms: the last frame of the owner, the last gather
    int count;
    MapUnit unit[kMapUnits];
    int dots,larges;            // the small enemies' dots, the large enemies' pins (of `count`)
    MapDot dot[kMapDots];
    bool loggedMouse;
    ULONGLONG loggedAt;         // the marks' debug line
};
Game game{};

// --- The camera hook's own ---
// The stock matrix (`stock`) is kept and put back before the camera's next step: its state 1 eases its eye from the
// last +0x250 (0xFB03E..0xFB101), so the map's eye left there would pull the stock camera down from the sky after.
using CamSide=mapcam::CameraState;
CamSide camSide{};

// --- The stock HUD's switch (under `hudLock`: the camera step and the scripts' SetPlayerHudShow) ---
using HudShowFn=void(__fastcall*)(void*,bool);
bool hudOk=false;
maphud::Record hudRecord{};
SRWLOCK hudLock=SRWLOCK_INIT;

// --- The objective markers (under `markerLock`; the destructor takes one out before it is freed) ---
const void* markers[kMarkers]{};
SRWLOCK markerLock=SRWLOCK_INIT;

// --- The window's wheel ---
HWND subclassed=nullptr;
WNDPROC prevProc=nullptr;
bool unicodeProc=true;

// --- XInput ---
XInputGetStateFn xinputGet=nullptr;
bool xinputTried=false;
DWORD padIndex=0;
ULONGLONG padSearchAt=0;

bool InFront() noexcept {
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId();
}
bool Down(int vk) noexcept { return vk>0 && (GetAsyncKeyState(vk)&0x8000)!=0; }

LRESULT CALLBACK WheelProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_MOUSEWHEEL && holds.load(std::memory_order_relaxed)) {
        wheel.fetch_add(GET_WHEEL_DELTA_WPARAM(wp),std::memory_order_relaxed);
        return 0;   // the map's: not the game's weapon change
    }
    return unicodeProc ? CallWindowProcW(prevProc,w,msg,wp,lp) : CallWindowProcA(prevProc,w,msg,wp,lp);
}

// The game's window (in front: the map was just opened by its key) subclassed for the wheel, once.
void Subclass() noexcept {
    if(subclassed)return;
    const HWND w=GetForegroundWindow();
    DWORD pid=0;
    if(!w || !GetWindowThreadProcessId(w,&pid) || pid!=GetCurrentProcessId())return;
    unicodeProc=IsWindowUnicode(w)!=FALSE;
    // The current procedure first, so a message between the two calls already finds where to go on.
    prevProc=reinterpret_cast<WNDPROC>(unicodeProc ? GetWindowLongPtrW(w,GWLP_WNDPROC) : GetWindowLongPtrA(w,GWLP_WNDPROC));
    if(!prevProc)return;
    const LONG_PTR was=unicodeProc ? SetWindowLongPtrW(w,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&WheelProc))
                                   : SetWindowLongPtrA(w,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&WheelProc));
    if(!was){Log("MAP the window's procedure could not be subclassed (%lu): no wheel zoom (+ / - zoom)",GetLastError());return;}
    prevProc=reinterpret_cast<WNDPROC>(was);
    subclassed=w;
    Log("MAP window %p subclassed for the wheel",static_cast<void*>(w));
}

// The first connected XInput pad (searched again every 2 s while none), its state; false with none.
bool PadState(XINPUT_STATE* s) noexcept {
    if(!xinputTried) {
        xinputTried=true;
        HMODULE m=LoadLibraryW(L"xinput1_4.dll");
        if(!m)m=LoadLibraryW(L"xinput9_1_0.dll");
        if(m)xinputGet=reinterpret_cast<XInputGetStateFn>(GetProcAddress(m,"XInputGetState"));
        Log("MAP XInput %s",xinputGet ? "found" : "not found: no pad on the map");
    }
    if(!xinputGet)return false;
    if(xinputGet(padIndex,s)==ERROR_SUCCESS)return true;
    const ULONGLONG now=GetTickCount64();
    if(now-padSearchAt<2000)return false;
    padSearchAt=now;
    for(DWORD i=0;i<XUSER_MAX_COUNT;++i)if(xinputGet(i,s)==ERROR_SUCCESS){padIndex=i;return true;}
    return false;
}
float Stick(SHORT v) noexcept {
    const float f=static_cast<float>(v)/32767.0f;
    return std::fabs(f)<kStickDead ? 0.0f : (f-std::copysign(kStickDead,f))/(1.0f-kStickDead);
}

// The game's own mouse delta this frame (the rider aim's record: in mouse units, before its /24), false with none.
bool MouseDelta(const unsigned char* human,float* dx,float* dy) noexcept {
    const auto pad=At<const unsigned char*>(human,kHumanPad);
    const auto index=At<std::int32_t>(human,kHumanRecord);
    if(!pad || index<0 || index>=kRecords)return false;
    const unsigned char* rec=pad+static_cast<std::size_t>(index)*kRecordStride;
    if(!Readable(rec+kMouseY,4))return false;
    *dx=At<float>(rec,kMouseX);*dy=At<float>(rec,kMouseY);
    return std::isfinite(*dx) && std::isfinite(*dy) && std::fabs(*dx)<kMouseMost && std::fabs(*dy)<kMouseMost;
}

const float* PosOf(const void* o) noexcept { return reinterpret_cast<const float*>(static_cast<const unsigned char*>(o)+kPosition); }

// What `human` stands for on the map: the vehicle they ride (alive), else themselves.
const unsigned char* Body(const unsigned char* human) noexcept {
    const auto ctrl=At<const unsigned char*>(human,kHumanVehicleCtrl);
    if(!ctrl || !Readable(ctrl,0x10) || At<std::int32_t>(ctrl,edf::kCtrlUses)==0)return human;
    const auto v=At<const unsigned char*>(human,kHumanVehicle);
    return Readable(v,kPosition+12) ? v : human;
}

// The ground's height under (x, z): the first map hit from high over it, else `fallback`.
float GroundAt(float x,float z,float fallback) noexcept {
    const float top[3]={x,fallback+4000.0f,z},bottom[3]={x,fallback-4000.0f,z};
    float hit[3];
    return MapRay(top,bottom,hit)>=0.0f && std::isfinite(hit[1]) ? hit[1] : fallback;
}

// --- The marks ---
MapUnit* Add(Game& g,const float* p,MapKind kind) noexcept {
    if(g.count>=kMapUnits || !std::isfinite(p[0]+p[1]+p[2]))return nullptr;
    MapUnit& u=g.unit[g.count++];
    u=MapUnit{};
    std::memcpy(u.pos,p,12);u.ground=p[1];u.hp=-1.0f;u.kind=kind;
    return &u;
}
// An object's level heading (its matrix's forward row) into the unit.
void Heading(MapUnit* u,const unsigned char* o) noexcept {
    const float* m=reinterpret_cast<const float*>(o+kMatrix);
    const float x=m[8],z=m[10],len=std::sqrt(x*x+z*z);
    if(len>1e-3f && std::isfinite(len)){u->dir[0]=x/len;u->dir[1]=z/len;}
}
float HpShare(const unsigned char* o,float* hpMax) noexcept {
    const float most=At<float>(o,kHpMax),hp=At<float>(o,kHp);
    *hpMax=std::isfinite(most) && most>0.0f ? most : 0.0f;
    return *hpMax>0.0f && std::isfinite(hp) ? vec::Clamp(hp/most,0.0f,1.0f) : -1.0f;
}
bool Aircraft(const void* v) noexcept { return IsJet(v) || IsPlayerJet(v) || IsHelicopter(v); }

// The friendly side's walks (map_marks.h): `nobodys` while it walks team 5.
struct Walk { void** vtable; Game* g; const void* self; const void* ride; std::int32_t team; bool nobodys; };
void __fastcall WalkVisit(void* self,void* object) noexcept {
    __try {
        const auto& w=*static_cast<Walk*>(self);
        const auto o=static_cast<const unsigned char*>(object);
        if(!o || o==w.self || o==w.ride || !Readable(o,kHp+4) || o[kDead])return;
        const bool vehicle=KnownVehicle(o);
        const mapmarks::Seen seen{vehicle,vehicle && IsSub(o),vehicle && (IsJet(o) || IsPlayerJet(o)),vehicle && IsHelicopter(o),
                                  At<std::int32_t>(o,kTeam)==w.team,w.nobodys};
        MapKind kind;
        std::uint8_t flags;
        if(!mapmarks::FriendlyMark(seen,&kind,&flags))return;
        MapUnit* u=Add(*w.g,PosOf(o),kind);
        if(!u)return;
        u->flags|=flags;
        if(vehicle)Heading(u,o);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void __fastcall WalkDtor(void*,unsigned) noexcept {}
void* kWalkVtable[]={reinterpret_cast<void*>(&WalkDtor),reinterpret_cast<void*>(&WalkVisit)};

// The enemies as the stock radar finds them (HUiHudRader's update 0x82B4D0 walks 0x5E0F20(manager, its team, functor):
// every live object of every team hostile to it, relation 2), all of them gathered here (kFoes at most).
struct Foe { const unsigned char* o; float d2,hpMax; };
constexpr int kFoes=4096;
Foe foes[kFoes];
int foeCount=0;
struct Hostile { void** vtable; const float* me; };
void __fastcall HostileVisit(void* self,void* object) noexcept {
    __try {
        const auto o=static_cast<const unsigned char*>(object);
        if(foeCount>=kFoes || !o || !Readable(o,kHp+4) || o[kDead])return;
        const float* p=PosOf(o);
        const float* me=static_cast<Hostile*>(self)->me;
        const float d[3]={p[0]-me[0],p[1]-me[1],p[2]-me[2]};
        const float d2=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
        if(!std::isfinite(d2))return;
        float hpMax=0.0f;HpShare(o,&hpMax);
        foes[foeCount++]=Foe{o,d2,hpMax};
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void* kHostileVtable[]={reinterpret_cast<void*>(&WalkDtor),reinterpret_cast<void*>(&HostileVisit)};

// The lock-registry fallback (the hostile walk's code not as expected): the enemies the lock-on system knows.
void EnemyVisit(void* ctx,const void* object,const float* aim) {
    (void)aim;
    auto& h=*static_cast<Hostile*>(ctx);
    HostileVisit(&h,const_cast<void*>(object));
}

// The ground under a small enemy, for its dot's "flying" (the user, 2026-10-06: small enemies get no pin, so no ray of
// their own): a cache of the ground's height on a kCellM grid (one map ray a cell at its centre, the first time a small
// enemy is in it; at most kCellRays new cells a gather, the rest known by the next ones), dropped with the mission.
constexpr float kCellM=48.0f;
constexpr int kCells=8192,kCellRays=48;
struct Cell { std::int32_t x,z; float h; bool used; };
Cell cells[kCells];
int cellsUsed=0;
// The cached ground at (x, z): true with `h`; false not known yet (a ray is cast when `rays` allows, counting down).
bool CellGround(float x,float z,float y,int* rays,float* h) noexcept {
    const auto cx=static_cast<std::int32_t>(std::floor(x/kCellM)),cz=static_cast<std::int32_t>(std::floor(z/kCellM));
    std::uint32_t k=(static_cast<std::uint32_t>(cx)*73856093u)^(static_cast<std::uint32_t>(cz)*19349663u);
    for(int probe=0;probe<16;++probe,++k) {
        Cell& c=cells[k%kCells];
        if(c.used && c.x==cx && c.z==cz){*h=c.h;return true;}
        if(c.used)continue;
        if(*rays<=0 || cellsUsed>=kCells*3/4)return false;   // full: the rest stay "on the ground"
        --*rays;++cellsUsed;
        c=Cell{cx,cz,GroundAt((static_cast<float>(cx)+0.5f)*kCellM,(static_cast<float>(cz)+0.5f)*kCellM,y),true};
        *h=c.h;
        return true;
    }
    return false;
}

// The enemies (the user, 2026-10-06: "按距离取最近 256 个 不合理吧，我觉得优先显示大型敌人"): large (HP max kLargeShare
// times the median of all of them, or kLargeHp) ones are pins, kMapLargeEnemies at most, the highest HP max first, each
// with its own ground ray (flying: an aircraft, or kFlyingClear over the ground under it: its stem goes down) and HP
// bar; the small ones are dots, kMapDots at most, the nearest first, flying by the cell cache. No count switches the
// style: large or small is the only split. The nearest enemy of all is flagged, pin or dot.
void Enemies(Game& g) noexcept {
    g.dots=0;g.larges=0;
    if(!foeCount)return;
    static float hps[kFoes];
    int n=0;
    for(int i=0;i<foeCount;++i)if(foes[i].hpMax>0.0f)hps[n++]=foes[i].hpMax;
    float median=0.0f;
    if(n){std::nth_element(hps,hps+n/2,hps+n);median=hps[n/2];}
    int nearest=0;
    for(int i=1;i<foeCount;++i)if(foes[i].d2<foes[nearest].d2)nearest=i;
    const unsigned char* nearestObj=foes[nearest].o;
    // Large ones to the front, the highest HP max first; the small ones after them, the nearest first.
    auto large=[&](const Foe& f){ return f.hpMax>=kLargeHp || (n>=2 && f.hpMax>=median*kLargeShare); };
    Foe* split=std::partition(foes,foes+foeCount,large);
    const int larges=static_cast<int>(split-foes),smalls=foeCount-larges;
    const int keepLarge=larges<kMapLargeEnemies ? larges : kMapLargeEnemies,keepSmall=smalls<kMapDots ? smalls : kMapDots;
    std::partial_sort(foes,foes+keepLarge,split,[](const Foe& a,const Foe& b){return a.hpMax>b.hpMax;});
    std::partial_sort(split,split+keepSmall,foes+foeCount,[](const Foe& a,const Foe& b){return a.d2<b.d2;});
    for(int i=0;i<keepLarge;++i) {
        const unsigned char* o=foes[i].o;
        const float* p=PosOf(o);
        const float ground=GroundAt(p[0],p[2],p[1]);
        const bool flying=(KnownVehicle(o) && Aircraft(o)) || p[1]-ground>kFlyingClear;
        MapUnit* u=Add(g,p,flying ? MapKind::enemyAir : MapKind::enemy);
        if(!u)break;
        if(flying)u->ground=ground;
        Heading(u,o);
        float hpMax=0.0f;
        u->hp=HpShare(o,&hpMax);
        u->flags|=kMapLarge|(o==nearestObj ? kMapNearest : 0);
        ++g.larges;
    }
    int rays=kCellRays;
    for(int i=0;i<keepSmall;++i) {
        const unsigned char* o=split[i].o;
        const float* p=PosOf(o);
        if(!std::isfinite(p[0]+p[1]+p[2]))continue;
        MapDot& d=g.dot[g.dots++];
        std::memcpy(d.pos,p,12);
        float h=0.0f;
        const bool flying=(KnownVehicle(o) && Aircraft(o)) || (CellGround(p[0],p[2],p[1],&rays,&h) && p[1]-h>kFlyingClear);
        d.flags=static_cast<std::uint8_t>((flying ? kMapFlying : 0)|(o==nearestObj ? kMapNearest : 0));
    }
}

// The player's locks this moment (EDF6AutoTurret's designation, a player jet's or a stock vehicle's or heli's homing
// store): a box each.
void Locks(Game& g) noexcept {
    auto add=[&](int state,const float* at){
        if(state<=0)return;
        if(MapUnit* u=Add(g,at,MapKind::lock))u->flags|=state==1 ? kMapAcquiring : 0;
    };
    edf::aimlink::TurretReadoutV1 t{};
    if(AutoTurretReadout(&t) && t.target)add(t.lock==edf::aimlink::Lock::locked ? 2 : t.lock==edf::aimlink::Lock::acquiring ? 1 : 0,t.at);
    PlayerJetReadout j{};
    if(PlayerJetHud(&j))add(j.lock,j.lockAt);
    HeliSightReadout h{};
    if(PlayerHeliSight(&h))add(h.lock,h.armAt);
    StockHudReadout s{};
    if(PlayerStockHud(&s))for(int i=0;i<s.arms && i<kStockArms;++i)add(s.arm[i].lock,s.arm[i].at);
}

void Gather(Game& g,const unsigned char* human) noexcept {
    g.count=0;
    const std::int32_t team=At<std::int32_t>(human,kTeam);
    const unsigned char* body=Body(human);
    const auto manager=At<void*>(image,kTeamManager);
    Hostile hostile{kHostileVtable,PosOf(body)};
    foeCount=0;
    if(hostileOk && manager)reinterpret_cast<WalkFn>(image+kHostileWalk)(manager,team,&hostile);
    else VisitEnemiesOf(team,&EnemyVisit,&hostile);
    Enemies(g);
    if(walkOk && manager) {
        const mapmarks::Walks walks=mapmarks::WalksFor(team);
        Walk w{kWalkVtable,&g,human,body!=human ? body : nullptr,team,false};
        reinterpret_cast<WalkFn>(image+kTeamWalk)(manager,team,&w);
        if(walks.nobodys && nobodysOk) {   // nobody's vehicles: the empty ones, the parked and delivered aircraft
            w.nobodys=true;
            reinterpret_cast<WalkFn>(image+kOneTeamWalk)(manager,mapmarks::kNobodysTeam,&w);
        }
        for(int i=0;i<g.count;++i)   // the friendly aircraft's ground (outside the walk: it holds the team lock)
            if(g.unit[i].kind==MapKind::air || g.unit[i].kind==MapKind::carrier)g.unit[i].ground=GroundAt(g.unit[i].pos[0],g.unit[i].pos[2],g.unit[i].pos[1]);
    }
    if(markerOk) {
        AcquireSRWLockShared(&markerLock);
        for(const void* m:markers) {
            if(!m)continue;
            const float* p=reinterpret_cast<const float*>(static_cast<const unsigned char*>(m)+kMarkerAt);
            if(Readable(p,12))Add(g,p,MapKind::marker);
        }
        ReleaseSRWLockShared(&markerLock);
    }
    Locks(g);
    if(Cfg().debug && GetTickCount64()-g.loggedAt>=5000) {
        g.loggedAt=GetTickCount64();
        int air=0,empty=0;
        for(int i=0;i<g.count;++i){air+=g.unit[i].kind==MapKind::air;empty+=(g.unit[i].flags&kMapEmpty)!=0;}
        Log("MAP marks: %d enemies (%s): %d large pins, %d small dots; %d pins in all (%d aircraft, %d empty from team 5), %d ground cells",
            foeCount,hostileOk ? "the radar's hostile walk" : "the lock registry",g.larges,g.dots,g.count,air,empty,cellsUsed);
    }
}

void Publish(const Game& g,const unsigned char* human,const float* eye,const float* look) noexcept {
    const Config& c=Cfg();
    const unsigned char* body=Body(human);
    AcquireSRWLockExclusive(&lock);
    pose=Pose{g.open,human,{eye[0],eye[1],eye[2]},{look[0],look[1],look[2]},GetTickCount64()};
    MapReadout& r=readout;
    r.pad=g.pad;r.follow=g.follow;
    std::memcpy(r.focus,g.view.focus,12);r.yaw=g.view.yaw;r.pitch=g.view.pitch;r.height=g.view.height;
    std::memcpy(r.me,PosOf(body),12);
    const float* m=reinterpret_cast<const float*>(body+kMatrix);
    r.meDir[0]=m[8];r.meDir[1]=0.0f;r.meDir[2]=m[10];
    if(!vec::Normalize(r.meDir)){r.meDir[0]=0.0f;r.meDir[2]=1.0f;}
    r.count=g.count;
    std::memcpy(r.unit,g.unit,sizeof(MapUnit)*static_cast<std::size_t>(g.count));
    r.dots=g.dots;
    std::memcpy(r.dot,g.dot,sizeof(MapDot)*static_cast<std::size_t>(g.dots));
    r.mapKey=c.mapKey;r.mapButton=c.mapButton;
    readoutAt=pose.at;
    ReleaseSRWLockExclusive(&lock);
}

// The map shut (any reason but a closing key: that one drains first, see Frame); its hold let go.
void Close(const char* why) noexcept {
    if(game.draining){game.draining=false;holds.store(false);}
    if(!game.open)return;
    game.open=false;
    holds.store(false);
    AcquireSRWLockExclusive(&lock);
    pose.open=false;readoutAt=0;
    ReleaseSRWLockExclusive(&lock);
    ViewMapClip(false,0.0f,0.0f);
    Log("MAP closed (%s)",why);
}

void Open(const unsigned char* human) noexcept {
    const unsigned char* body=Body(human);
    const float* p=PosOf(body);
    game.view.focus[0]=p[0];game.view.focus[2]=p[2];game.view.focus[1]=GroundAt(p[0],p[2],p[1]);
    float eye[3],dir[3];
    if(CameraRay(eye,dir) && dir[0]*dir[0]+dir[2]*dir[2]>1e-4f)game.view.yaw=std::atan2(dir[0],dir[2]);
    else {
        const float* m=reinterpret_cast<const float*>(body+kMatrix);
        game.view.yaw=std::atan2(m[8],m[10]);
    }
    game.view.pitch=mapcam::kStartPitch;game.view.height=mapcam::kStartHeight;
    game.open=true;game.follow=true;game.gatherAt=0;game.loggedMouse=false;
    holds.store(true);
    Subclass();
    wheel.store(0);
    Log("MAP open at (%.0f,%.0f,%.0f), heading %.0f deg (camera hook %d, hold %d, marks: team walk %d, markers %d)",p[0],p[1],p[2],
        game.view.yaw*180.0f/mapcam::kPi,camOk,holdOk,walkOk,markerOk);
}

// The map's input for `dt` s: keys and mouse (in front), the wheel, a pad.
void Steer(const unsigned char* human,float dt,bool front,const XINPUT_STATE* pad) noexcept {
    mapcam::View& v=game.view;
    float ahead=0.0f,right=0.0f,turn=0.0f,tilt=0.0f,zoom=0.0f;
    bool keys=false;
    if(front) {
        ahead=static_cast<float>((Down('W') || Down(VK_UP))-(Down('S') || Down(VK_DOWN)));
        right=static_cast<float>((Down('D') || Down(VK_RIGHT))-(Down('A') || Down(VK_LEFT)));
        turn=static_cast<float>(Down('E')-Down('Q'))*mapcam::kTurnRate*dt;
        tilt=static_cast<float>(Down('R')-Down('F'))*mapcam::kTurnRate*0.5f*dt;
        zoom=static_cast<float>((Down(VK_OEM_PLUS) || Down(VK_ADD) || Down(VK_PRIOR))-(Down(VK_OEM_MINUS) || Down(VK_SUBTRACT) || Down(VK_NEXT)));
        keys=ahead!=0.0f || right!=0.0f || turn!=0.0f || tilt!=0.0f || zoom!=0.0f;
        float dx=0.0f,dy=0.0f;
        if(MouseDelta(human,&dx,&dy) && (dx!=0.0f || dy!=0.0f)) {
            if(!game.loggedMouse && Cfg().debug){game.loggedMouse=true;Log("MAP mouse delta (%.1f,%.1f) a frame",dx,dy);}
            // Ctrl + left drag is the NPC commands' selection box (mapcmd.cpp), not a pan.
            if(Down(VK_LBUTTON) && !Down(VK_CONTROL) && !MapCommandBoxing()){mapcam::Drag(v,dx,dy);game.follow=false;keys=true;}
            else if(Down(VK_RBUTTON)){mapcam::Turn(v,dx*mapcam::kDragTurn,dy*mapcam::kDragTurn);keys=true;}
        }
    }
    const int notches=wheel.exchange(0);
    if(notches){mapcam::Zoom(v,static_cast<float>(notches)/static_cast<float>(WHEEL_DELTA));keys=true;}
    if(pad) {
        const XINPUT_GAMEPAD& p=pad->Gamepad;
        const float lx=Stick(p.sThumbLX),ly=Stick(p.sThumbLY),rx=Stick(p.sThumbRX),ry=Stick(p.sThumbRY);
        const float trig=(static_cast<float>(p.bRightTrigger)-static_cast<float>(p.bLeftTrigger))/255.0f;
        if(lx!=0.0f || ly!=0.0f || rx!=0.0f || ry!=0.0f || std::fabs(trig)>0.1f) {
            ahead+=ly;right+=lx;turn+=rx*mapcam::kTurnRate*dt;tilt-=ry*mapcam::kTurnRate*0.5f*dt;
            if(std::fabs(trig)>0.1f)zoom+=trig;
            game.pad=true;
        }
    }
    if(keys)game.pad=false;
    if(ahead!=0.0f || right!=0.0f){mapcam::Pan(v,vec::Clamp(ahead,-1.0f,1.0f),vec::Clamp(right,-1.0f,1.0f),dt);game.follow=false;}
    mapcam::Turn(v,turn,tilt);
    if(zoom!=0.0f)v.height=mapcam::Clamp(v.height*std::pow(mapcam::kZoomRate,-zoom*dt),mapcam::kMinHeight,mapcam::kMaxHeight);
}

// The toggles' edges this frame.
Keys ReadToggles(bool front,const XINPUT_STATE* pad) noexcept {
    const Config& c=Cfg();
    Keys k{};
    if(front){k.map=Down(c.mapKey);k.esc=Down(VK_ESCAPE);k.centre=Down(VK_SPACE) || Down(VK_HOME);}
    if(pad) {
        const WORD b=pad->Gamepad.wButtons;
        k.padMap=c.mapButton>0 && (b&static_cast<WORD>(c.mapButton))==static_cast<WORD>(c.mapButton);
        k.padClose=(b&XINPUT_GAMEPAD_B)!=0;
        k.padCentre=(b&XINPUT_GAMEPAD_A)!=0;
    }
    return k;
}

// The map's frame for the player `human` (game thread, from the soldier pre-update before it reads the pad): true
// while the map is open (the shim then takes the no-pad path: the player's input held).
bool Frame(unsigned char* human) noexcept {
    const Config& c=Cfg();
    const ULONGLONG now=GetTickCount64();
    // One player (a second local player is never held: the map is the first one's while its frames come; the old one
    // is not read, it may be gone).
    if(game.human.obj && game.human.obj!=human && now-game.frameAt<kFreshMs)return false;
    if(!game.human.Is(human)){Close("another player");game.human=ObjRef::Of(human);game.was=Keys{true,true,true,true,true,true};}
    LARGE_INTEGER t,hz;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&hz);
    float dt=game.last.QuadPart ? static_cast<float>(t.QuadPart-game.last.QuadPart)/static_cast<float>(hz.QuadPart) : 0.0f;
    game.last=t;
    if(!(dt>0.0f) || dt>0.1f)dt=1.0f/60.0f;
    if(game.open && now-game.frameAt>kFreshMs)Close("the game was paused");   // its Esc read by the pause menu, not here
    game.frameAt=now;
    if(!c.enabled || !c.map){Close(c.enabled ? "Map=0" : "plugin off");return false;}
    const bool front=InFront();
    XINPUT_STATE padState{};
    const bool pad=c.mapButton>0 && PadState(&padState);
    const Keys k=ReadToggles(front,pad ? &padState : nullptr);
    const bool toggle=(k.map && !game.was.map) || (k.padMap && !game.was.padMap);
    const bool close=(k.esc && !game.was.esc) || (k.padClose && !game.was.padClose);
    const bool centre=(k.centre && !game.was.centre) || (k.padCentre && !game.was.padCentre);
    game.was=k;
    if(game.open && (toggle || close)) {
        Close(close ? "Esc / B" : "the map key");
        // The key that closed it is still down: let through now, the stock pad read would take this frame's B as the
        // vehicle's or the soldier's (the seat switch, a stock B action), Esc as the pause menu's. The hold stays on
        // (and the plugin's keys with it, MapHoldsKeys) until every closing key and button is let go.
        game.draining=true;holds.store(true);
    }
    if(game.draining) {
        if(k.map || k.esc || k.padMap || k.padClose)return true;
        game.draining=false;holds.store(false);
    }
    if(!game.open) {
        if(!toggle)return false;
        Open(human);
        game.pad=k.padMap && !k.map;
    }
    if(centre)game.follow=true;
    Steer(human,dt,front,pad ? &padState : nullptr);
    mapcam::View& v=game.view;
    const float* me=PosOf(Body(human));
    if(game.follow){v.focus[0]=me[0];v.focus[2]=me[2];}
    v.focus[1]+=(GroundAt(v.focus[0],v.focus[2],v.focus[1])-v.focus[1])*0.2f;   // the ground under the focus, eased
    float eye[3],look[3];
    mapcam::Place(v,eye,look);
    const float under=GroundAt(eye[0],eye[2],eye[1]-v.height);
    if(eye[1]<under+kEyeClear)eye[1]=under+kEyeClear;   // a ridge behind the focus: over it, still looking at the focus
    // The NPC commands (mapcmd.cpp): a unit selected by its key centres the map on it.
    float onto[3];
    MapCmdInput in{front,pad,game.pad,false,pad ? padState.Gamepad.wButtons : static_cast<WORD>(0),0.0f,0.0f,{},{}};
    in.mouse=front && MouseDelta(human,&in.dx,&in.dy);
    std::memcpy(in.eye,eye,12);std::memcpy(in.look,look,12);
    if(MapCommandFrame(in,onto)){v.focus[0]=onto[0];v.focus[2]=onto[2];game.follow=false;}
    game.pad=in.usingPad;
    if(now-game.gatherAt>=kGatherMs){game.gatherAt=now;Gather(game,human);}
    Publish(game,human,eye,look);
    ViewMapClip(true,c.mapViewDistance,vec::Clamp(mapcam::Distance(v)*0.004f,0.5f,5.0f));
    return true;
}

// The player's input for the Tempest's TV (tvguide.cpp): mouse and keys in front, a pad.
TvInput TvRead(const unsigned char* human) noexcept {
    TvInput in{InFront(),0.0f,0.0f,0.0f,0.0f,false,false};
    if(in.front) {
        if(!MouseDelta(human,&in.dx,&in.dy))in.dx=in.dy=0.0f;
        in.fire=Down(VK_LBUTTON);in.leave=Down(VK_ESCAPE);
    }
    XINPUT_STATE pad{};
    if(PadState(&pad)) {
        in.rx=Stick(pad.Gamepad.sThumbRX);in.ry=Stick(pad.Gamepad.sThumbRY);
        in.fire=in.fire || pad.Gamepad.bRightTrigger>128;
        in.leave=in.leave || (pad.Gamepad.wButtons&XINPUT_GAMEPAD_B)!=0;
    }
    return in;
}

// Called by the shim (rcx = the soldier) from every soldier's pre-update, the player's or not.
bool __fastcall MapHumanFrame(unsigned char* human) noexcept {
    __try {
        if(!human || !human[kHumanPlayer] || !IsPlayer(human))return false;
        const bool open=Frame(human);
        return TvFrame(human,open && game.open,TvRead(human)) || open;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// --- The camera ---
// The matrix the game's look-to makes from `eye` toward `look`, into the camera.
bool PutView(unsigned char* cam,const float* eye,const float* look) noexcept {
    alignas(16) float dir[4]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2],0.0f};
    if(!vec::Normalize(dir))return false;
    alignas(16) float m[16];
    reinterpret_cast<LookToFn>(image+kLookTo)(m,dir);
    for(int i=0;i<16;++i)if(!std::isfinite(m[i]))return false;
    m[12]=eye[0];m[13]=eye[1];m[14]=eye[2];m[15]=1.0f;
    std::memcpy(cam+kCamMatrix,m,sizeof(m));
    return true;
}

void Ease(float* at,const float* to,float k) noexcept { for(int i=0;i<3;++i)at[i]+=(to[i]-at[i])*k; }

void Camera(unsigned char* cam,std::uint64_t generation) noexcept {
    if(camSide.shown && camSide.cam!=cam)return;   // another camera (a second local player's): the map is not its
    AcquireSRWLockShared(&lock);
    const Pose p=pose;
    ReleaseSRWLockShared(&lock);
    if(!cameraSession.Current(generation))return;
    // The camera's target: the soldier it follows (+0x350 its weak reference, +0x360 the SoldierBase cast of it).
    const bool mine=p.human && (At<const void*>(cam,kCamTargetRef)==p.human || At<const void*>(cam,kCamTarget)==p.human);
    const bool mapOpen=p.open && mine && GetTickCount64()-p.at<=kFreshMs;
    // The Tempest's TV (tvguide.cpp) shows through the same hook (one owner of the camera step) when the map does not.
    const void* tvHuman=nullptr;
    float tvEye[3],tvLook[3];
    const bool tv=!mapOpen && TvView(&tvHuman,tvEye,tvLook) &&
                  (At<const void*>(cam,kCamTargetRef)==tvHuman || At<const void*>(cam,kCamTarget)==tvHuman);
    const bool open=mapOpen || tv;
    const float* toEye=mapOpen ? p.eye : tvEye;
    const float* toLook=mapOpen ? p.look : tvLook;
    if(!open && !camSide.shown)return;
    // The stock view this frame (its eye; a point ahead along its forward row): where the map eases from and back to.
    const float* stockEye=reinterpret_cast<const float*>(cam+kCamEyeRow);
    const float* fwd=reinterpret_cast<const float*>(cam+kCamFwdRow);
    const float stockLook[3]={stockEye[0]+fwd[0]*100.0f,stockEye[1]+fwd[1]*100.0f,stockEye[2]+fwd[2]*100.0f};
    std::memcpy(camSide.stock,cam+kCamMatrix,sizeof(camSide.stock));camSide.haveStock=true;
    if(open) {
        if(!camSide.shown || camSide.leaving) {   // taken: from where the camera is (or the way back), easing in
            if(!camSide.shown){std::memcpy(camSide.eye,stockEye,12);std::memcpy(camSide.look,stockLook,12);}
            camSide.cam=cam;camSide.shown=true;camSide.leaving=false;camSide.blend=kEaseIn;
            cameraSession.Publish(generation,true);
        }
        const float k=camSide.blend>0 ? 1.0f/static_cast<float>(camSide.blend--) : 1.0f;
        Ease(camSide.eye,toEye,k);Ease(camSide.look,toLook,k);
    } else {
        if(!camSide.leaving){camSide.leaving=true;camSide.blend=kEaseOut;}
        if(camSide.blend<=0){camSide=CamSide{};cameraSession.Publish(generation,false);return;}
        const float k=1.0f/static_cast<float>(camSide.blend--);
        Ease(camSide.eye,stockEye,k);Ease(camSide.look,stockLook,k);
    }
    if(!PutView(cam,camSide.eye,camSide.look)){camSide=CamSide{};cameraSession.Publish(generation,false);}
}

// The stock HUD held off while the map's view is on this camera (easing in, open, easing back), put back as the game last
// asked when it is not: after the camera eased back, after a fault (camSide dropped), with the map closed any way.
void StockHud(unsigned char* cam,std::uint64_t generation) noexcept {
    if(!hudOk)return;
    const bool hide=camSide.shown && camSide.cam==cam && cameraSession.Current(generation);
    AcquireSRWLockExclusive(&hudLock);
    const bool was=hudRecord.hidden && hudRecord.cam==cam;
    __try { maphud::Step(hudRecord,cam,generation,hide,cam+kCamHudShown); }
    __except(EXCEPTION_EXECUTE_HANDLER){hudRecord=maphud::Record{};}
    const bool now=hudRecord.hidden && hudRecord.cam==cam,want=hudRecord.want;
    ReleaseSRWLockExclusive(&hudLock);
    if(was!=now)Log(now ? "MAP the stock HUD hidden on camera %p (its switch +0x200 was %d)" : "MAP the stock HUD back on camera %p (%d)",
                    static_cast<void*>(cam),now ? static_cast<int>(want) : static_cast<int>(cam[kCamHudShown]));
}

// SetPlayerHudShow's write (its call to 0x118DF30): kept for the end of the hold on the map's camera, else written.
void __fastcall HudShowHook(void* cam,bool show) {
    AcquireSRWLockExclusive(&hudLock);
    const bool write=maphud::GameSet(hudRecord,cam,cameraSession.Generation(),show);
    ReleaseSRWLockExclusive(&hudLock);
    if(write)reinterpret_cast<HudShowFn>(image+kHudShow)(cam,show);
    else Log("MAP the mission asked the HUD %s on camera %p while the map shows: done when it closes",show ? "shown" : "hidden",cam);
}

void __fastcall CamStepHook(void* cam,void* step) {
    const auto generation=cameraSession.Begin(camSide);
    if(camSide.shown && camSide.cam==cam && camSide.haveStock)std::memcpy(static_cast<unsigned char*>(cam)+kCamMatrix,camSide.stock,sizeof(camSide.stock));
    nextCamStep(cam,step);
    __try { Camera(static_cast<unsigned char*>(cam),generation); }
    __except(EXCEPTION_EXECUTE_HANDLER){camSide=CamSide{};cameraSession.Publish(generation,false);}
    StockHud(static_cast<unsigned char*>(cam),generation);
}

// --- The objective markers ---
void* __fastcall MarkerUpdateHook(void* marker,void* a,void* b,void* c) {
    AcquireSRWLockExclusive(&markerLock);
    bool known=false;
    for(const void* m:markers)known=known || m==marker;
    if(!known)for(auto& m:markers)if(!m){m=marker;Log("MAP marker %p (a DestinationMarker) on the map",marker);break;}
    ReleaseSRWLockExclusive(&markerLock);
    return nextMarkerUpdate(marker,a,b,c);
}
void* __fastcall MarkerDtorHook(void* marker,unsigned flags) {
    AcquireSRWLockExclusive(&markerLock);
    for(auto& m:markers)if(m==marker)m=nullptr;
    ReleaseSRWLockExclusive(&markerLock);
    return nextMarkerDtor(marker,flags);
}

bool InstallHold() noexcept {
    if(!Matches(kHoldAt,kHoldCode,sizeof(kHoldCode)) || !Matches(kNoPad,kNoPadCode,sizeof(kNoPadCode)))return false;
    // The shim: rcx = the soldier; MapHumanFrame (al: hold); then the stock test, the no-pad path when held.
    //   mov rcx,rsi / sub rsp,20h / mov rax,imm64 / call rax / add rsp,20h / mov rbx,[rsi+340h] / test al,al / jnz hold /
    //   test rbx,rbx / jz hold / jmp [rip] resume / hold: jmp [rip] no-pad
    unsigned char shim[]={0x48,0x89,0xF1, 0x48,0x83,0xEC,0x20, 0x48,0xB8,0,0,0,0,0,0,0,0, 0xFF,0xD0, 0x48,0x83,0xC4,0x20,
                          0x48,0x8B,0x9E,0x40,0x03,0x00,0x00, 0x84,0xC0, 0x75,0x13, 0x48,0x85,0xDB, 0x74,0x0E,
                          0xFF,0x25,0,0,0,0, 0,0,0,0,0,0,0,0, 0xFF,0x25,0,0,0,0, 0,0,0,0,0,0,0,0};
    const auto fn=reinterpret_cast<std::uintptr_t>(&MapHumanFrame);
    const auto resume=reinterpret_cast<std::uintptr_t>(image+kHoldResume),noPad=reinterpret_cast<std::uintptr_t>(image+kNoPad);
    std::memcpy(shim+9,&fn,8);std::memcpy(shim+45,&resume,8);std::memcpy(shim+59,&noPad,8);
    void* const page=AllocateNearCode(image+kHoldAt,shim,sizeof(shim));
    if(!page)return false;
    unsigned char patch[sizeof(kHoldCode)];
    std::memset(patch,0xCC,sizeof(patch));
    patch[0]=0xE9;
    const std::int64_t rel=reinterpret_cast<std::intptr_t>(page)-reinterpret_cast<std::intptr_t>(image+kHoldAt+5);
    if(rel<INT32_MIN || rel>INT32_MAX){VirtualFree(page,0,MEM_RELEASE);return false;}
    const auto rel32=static_cast<std::int32_t>(rel);
    std::memcpy(patch+1,&rel32,4);
    if(!edf::PatchCode(image+kHoldAt,kHoldCode,patch,sizeof(patch))){VirtualFree(page,0,MEM_RELEASE);return false;}
    return true;
}

bool InstallCamera() noexcept {
    if(!Matches(kCamStep,kCamStepCode,sizeof(kCamStepCode)) || !Matches(kLookTo,kLookToCode,sizeof(kLookToCode)) ||
       !Matches(0xFC0D3,kLookToUse,sizeof(kLookToUse)))return false;
    auto slot=reinterpret_cast<void**>(image+kCamVtable)+kCamStepSlot;
    if(*slot!=image+kCamStep)Log("MAP the camera's step is patched already (%p): chained onto it",*slot);
    void* next=nullptr;
    if(!edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&CamStepHook),&next))return false;
    nextCamStep=reinterpret_cast<CamStepFn>(next);
    return true;
}

bool InstallMarkers() noexcept {
    auto vt=reinterpret_cast<void**>(image+kMarkerVtable);
    if(vt[kMarkerDtorSlot]!=image+kMarkerDtor || vt[kMarkerUpdateSlot]!=image+kMarkerUpdate ||
       !Matches(kMarkerUpdate,kMarkerUpdateCode,sizeof(kMarkerUpdateCode)))return false;
    void* next=nullptr;
    if(!edf::ChainVtableSlot(vt+kMarkerDtorSlot,reinterpret_cast<void*>(&MarkerDtorHook),&next))return false;
    nextMarkerDtor=reinterpret_cast<MarkerDtorFn>(next);
    if(!edf::ChainVtableSlot(vt+kMarkerUpdateSlot,reinterpret_cast<void*>(&MarkerUpdateHook),&next))return true;   // the dtor alone: harmless
    nextMarkerUpdate=reinterpret_cast<MarkerUpdateFn>(next);
    return true;
}
// The stock HUD's switch: every byte it stands on as expected, then SetPlayerHudShow's call redirected; else the stock HUD
// is never touched (the map shows over it, as before).
bool InstallHudSwitch() noexcept {
    for(const HudSig& s:kHudSigs)
        if(!Matches(s.rva,s.bytes,s.size)) {
            Log("MAP the stock HUD's switch: EDF+%X is not as expected: the stock HUD stays up on the map",s.rva);
            return false;
        }
    bool changed=false;
    if(!RedirectCall(image+kHudShowCall,image+kHudShow,reinterpret_cast<void*>(&HudShowHook),changed)) {
        Log("MAP the stock HUD's switch: SetPlayerHudShow's call could not be redirected: the stock HUD stays up on the map");
        return false;
    }
    return true;
}
}  // namespace

bool InstallMap() noexcept {
    __try {
        // The camera first: without it the map would only hold the player.
        camOk=InstallCamera();
        holdOk=camOk && InstallHold();
        walkOk=Matches(kTeamWalk,kTeamWalkCode,sizeof(kTeamWalkCode));
        nobodysOk=walkOk && Matches(kOneTeamWalk,kOneTeamWalkCode,sizeof(kOneTeamWalkCode)) &&
                  Matches(kBoardTeam5Call,kBoardTeam5Code,sizeof(kBoardTeam5Code));
        hostileOk=Matches(kHostileWalk,kHostileWalkCode,sizeof(kHostileWalkCode)) && Matches(0x82B8C3,kRadarCall,sizeof(kRadarCall));
        markerOk=holdOk && InstallMarkers() && nextMarkerUpdate;
        hudOk=holdOk && InstallHudSwitch();
        Log("MAP hooks: camera=%d hold=%d team walk=%d team 5 walk=%d hostile walk=%d markers=%d stock HUD switch=%d%s",camOk,holdOk,
            walkOk,nobodysOk,hostileOk,markerOk,hudOk,
            holdOk ? "" : " (the map is off: unexpected EDF.dll code)");
        return holdOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void ResetMap() noexcept {
    Close("a new mission");
    ResetMapCommands();
    game.human=ObjRef{};game.count=0;game.dots=0;
    for(auto& c:cells)c=Cell{};
    cellsUsed=0;
    AcquireSRWLockExclusive(&lock);
    cameraSession.Reset();   // the camera hook drops its old matrix on its own thread, before restoring it
    pose=Pose{};readoutAt=0;
    ReleaseSRWLockExclusive(&lock);
}

bool PlayerMap(MapReadout* out) noexcept {
    AcquireSRWLockShared(&lock);
    const bool fresh=pose.open && readoutAt && GetTickCount64()-readoutAt<=kFreshMs;
    if(fresh)*out=readout;
    ReleaseSRWLockShared(&lock);
    return fresh;
}

bool MapHoldsKeys() noexcept { return holds.load(std::memory_order_relaxed) || TvHoldsKeys(); }
bool MapOwnsView() noexcept { return cameraSession.Owns(); }
bool MapHidesStockHud(const void* camera) noexcept {
    AcquireSRWLockShared(&hudLock);
    const bool hidden=maphud::Hides(hudRecord,camera,cameraSession.Generation());
    ReleaseSRWLockShared(&hudLock);
    return hidden;
}
}  // namespace crew

// EDF6AutoTurret asks whether the map holds the keys before it reads its own (common/edf/aimlink.h InputHeldV1).
extern "C" __declspec(dllexport) bool __cdecl EDF6VehicleCrew_InputHeldV1() { return crew::MapHoldsKeys(); }
