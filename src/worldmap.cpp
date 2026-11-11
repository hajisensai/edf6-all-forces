// The world map mission select: worldmap.h. EDF.dll TimeDateStamp 0x678CCB46, RVAs; the RE is
// docs/worldmap-select-re.md (draw slot, layout, cursor, input, viewport) and docs/plan-2026-11-10-worldmap.md §2.
//
// ui::HUiHQMissionSelect's vtable 0x18052E8, three slots chained (read and chained, never assumed stock):
//  - slot 1, OnUpdate 0x8A4F60 (UI thread): the list's elements hidden (shown flag +0x1F8), the map's input read
//    (a left drag pans, the wheel zooms about the pointer, the right stick pans, LB / RB zoom; only while the select
//    owns the input: ctx+8, its ui::Input*, not null), the opening move (the whole Earth, then in about a second to
//    the place of the row under the cursor +0x778), the view published for the draw. On a frame the map used input
//    the stock OnUpdate gets a copy of its ctx with +8 null (§5.2: it then neither scrolls the hidden list nor
//    presses its buttons; its cursor refresh still runs).
//  - slot 2, Draw 0x7FA970 (the render command queue's thread): the map drawn into the select's viewport (ctx+0x398)
//    on its D3D11 context (ctx+8) through worldmap_gpu.h, then the stock draw: the select's kept windows (thumbnail,
//    medals, briefing) and every later screen (a dialog) over it.
//  - slot 5, the scalar deleting destructor 0x8A3650: the object's end. Every texture and the device state go with it
//    (the next select makes them again); the tiles file stays open (worldmap_tiles.h: its table read once).
// The tiles file (Mods/Plugins/EDF6VehicleCrew.worldmap.bin) is opened at the first select; when it cannot be (not
// installed, another layout) the log says why once and every select stays stock. The same when the draw cannot run
// on this device (the list is hidden only once the map has been drawn).
#include "worldmap.h"
#include "worldmap_tiles.h"
#include "crew.h"
#include "memory.h"

#include <Xinput.h>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <new>

namespace crew {
namespace {
using namespace worldmap;
namespace gpu=worldmapgpu;

// --- The game's side (EDF.dll 0x678CCB46) ---
constexpr unsigned kUpdateSlot=0x18052F0,kUpdate=0x8A4F60;   // vtable 0x18052E8 slot 1: OnUpdate(this, UpdateContext*)
constexpr unsigned kDrawSlot=0x18052F8,kDraw=0x7FA970;       // slot 2: HUiBase::Draw(this, xgl::gs::Context*)
constexpr unsigned kGoneSlot=0x1805310,kGone=0x8A3650;       // slot 5: (this, flags): ~HUiHQMissionSelect, delete 0x920
constexpr unsigned kFind=0x839FD0;          // HUiLayout: (this, shared_ptr<ui::Component>* out, const std::wstring* name)
constexpr unsigned kRowCount=0xE0650;       // int (cfg): the mode's list rows
constexpr unsigned kChildIndex=0x52DE0;     // u32 (const Handle*, i): the node's i-th child's index
constexpr unsigned kNodeType=0x52E50;       // (const Handle*): 1 a string, 3 an array
constexpr unsigned kNodeCount=0x53060;      // int (const Handle*): an array's children
constexpr unsigned kNodeText=0x551E0;       // const wchar_t* (const Handle*): a string node's text
constexpr std::size_t kGameStatus=0x20B2890,kModeCfg=0x130,kListDoc=0xF0;   // [GS]+0x130: the list's mode config, +0xF0 its table
constexpr std::size_t kCursor=0x778,kEntries=0x798,kEntriesEnd=0x7A0,kEntrySize=0x14;   // the select's cursor, display list
constexpr std::size_t kShown=0x1F8;         // a Component's shown flag
constexpr std::size_t kCtxSize=0x18,kCtxInput=0x8,kCtxDt=0x10;   // UpdateContext {.., ui::Input*, float dt}
constexpr std::size_t kDrawDevice=0x8,kDrawViewport=0x398;       // xgl::gs::Context: ID3D11DeviceContext*, {x, y, w, h}

struct Sig { unsigned rva; const unsigned char* bytes; std::size_t size; };
const unsigned char kUpdateSig[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x48,0x89,0x70,0x20,0x55,0x57,0x41,0x55,0x41};
const unsigned char kDrawSig[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x55,0x57,0x41,0x56,0x48,0x8D};
// mov [rsp+8],rbx; push rdi; sub rsp,20h; mov ebx,edx; mov rdi,rcx; call ~HUiHQMissionSelect 0x8A31B0; test bl,1; je;
// mov edx,920h
const unsigned char kGoneSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0xDA,0x48,0x8B,0xF9,0xE8,0x4C,0xFB,
    0xFF,0xFF,0xF6,0xC3,0x01,0x74,0x0D,0xBA,0x20,0x09,0x00,0x00};
const unsigned char kFindSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x48};
const unsigned char kShowSig[]={0xC6,0x87,0xF8,0x01,0x00,0x00,0x01};                     // 0x7E999C: mov byte [rdi+1F8h],1
const unsigned char kRowCountSig[]={0x48,0x8B,0x91,0xF0,0x00,0x00,0x00,0x48,0x85,0xD2,0x75,0x03,0x33,0xC0,0xC3,0x44};
const unsigned char kChildIndexSig[]={0x4C,0x8B,0x09,0x44,0x8B,0x51,0x08,0x44,0x8B,0xC2,0x4E,0x8D,0x04,0x85,0x00,0x00};
const unsigned char kNodeTypeSig[]={0x48,0x8B,0x11,0x48,0x85,0xD2,0x75,0x04,0x8D,0x42,0x05,0xC3,0x44,0x8B,0x49,0x08};
const unsigned char kNodeCountSig[]={0x48,0x8B,0x11,0x44,0x8B,0x49,0x08,0x4C,0x39,0x4A,0x18,0x76,0x26,0x48,0x8B,0x42};
const unsigned char kNodeTextSig[]={0x48,0x83,0xEC,0x28,0x4C,0x8B,0xD1,0xE8,0xC4,0xDC,0xFF,0xFF,0x84,0xC0,0x74,0x1A};
const unsigned char kKeyReadSig[]={0x48,0x8D,0x99,0xF0,0x00,0x00,0x00};   // 0xE0DAE (the row key reader): lea rbx,[rcx+0F0h]
// 0x8A2BC1 (the select's constructor): mov rcx,[GS]; add rcx,130h; call 0xE0650 (the rows of the list it shows)
const unsigned char kCfgSig[]={0x48,0x8B,0x0D,0xC8,0xFC,0x80,0x01,0x48,0x81,0xC1,0x30,0x01,0x00,0x00,0xE8};
// 0x8A85E8 (cbRequestMission): movsxd rcx,[rcx+778h]; cmp byte [rdi+7B0h],0; jne; lea rdx,[rcx+rcx*4];
// mov rax,[rdi+798h] (the cursor, the display list of 0x14-byte entries)
const unsigned char kCursorSig[]={0x48,0x63,0x89,0x78,0x07,0x00,0x00,0x80,0xBF,0xB0,0x07,0x00,0x00,0x00,0x75,0x1E,0x48,0x8D,
    0x14,0x89,0x48,0x8B,0x87,0x98,0x07,0x00,0x00};
const unsigned char kInputSig[]={0x4D,0x8B,0x75,0x08};          // 0x8A5029: mov r14,[r13+8] (the ctx's ui::Input*)
const unsigned char kDtSig[]={0xF3,0x41,0x0F,0x10,0x5D,0x10};   // 0x8A5303: movss xmm3,[r13+10h] (the ctx's dt)
const unsigned char kExecSig[]={0x48,0xFF,0x60,0x10};           // 0x11A43EA: jmp [rax+10h] (the UI draw command: slot 2)
const unsigned char kViewportSig[]={0x0F,0x11,0x81,0x98,0x03,0x00,0x00};   // 0x113E90B: movups [rcx+398h],xmm0
const unsigned char kDeviceSig[]={0x48,0x8B,0x49,0x08};         // 0x113E928: mov rcx,[rcx+8] (then RSSetViewports)
const Sig kSigs[]={{kUpdate,kUpdateSig,sizeof(kUpdateSig)},{kDraw,kDrawSig,sizeof(kDrawSig)},{kGone,kGoneSig,sizeof(kGoneSig)},
    {kFind,kFindSig,sizeof(kFindSig)},{0x7E999C,kShowSig,sizeof(kShowSig)},{kRowCount,kRowCountSig,sizeof(kRowCountSig)},
    {kChildIndex,kChildIndexSig,sizeof(kChildIndexSig)},{kNodeType,kNodeTypeSig,sizeof(kNodeTypeSig)},
    {kNodeCount,kNodeCountSig,sizeof(kNodeCountSig)},{kNodeText,kNodeTextSig,sizeof(kNodeTextSig)},
    {0xE0DAE,kKeyReadSig,sizeof(kKeyReadSig)},{0x8A2BC1,kCfgSig,sizeof(kCfgSig)},{0x8A85E8,kCursorSig,sizeof(kCursorSig)},
    {0x8A5029,kInputSig,sizeof(kInputSig)},{0x8A5303,kDtSig,sizeof(kDtSig)},{0x11A43EA,kExecSig,sizeof(kExecSig)},
    {0x113E90B,kViewportSig,sizeof(kViewportSig)},{0x113E928,kDeviceSig,sizeof(kDeviceSig)}};

using UpdateFn=std::uint64_t(__fastcall*)(unsigned char*,unsigned char*);
using DrawFn=std::uint64_t(__fastcall*)(unsigned char*,unsigned char*);
using GoneFn=void*(__fastcall*)(unsigned char*,unsigned);
UpdateFn nextUpdate=nullptr;
DrawFn nextDraw=nullptr;
GoneFn nextGone=nullptr;

// --- Game objects (MSVC std::shared_ptr / wstring as EDF.dll builds them; as room_ui.cpp) ---
struct ControlBlock { void** vtable; volatile long uses,weaks; };
struct Shared { void* object; ControlBlock* control; };
void Release(Shared& p) noexcept {
    ControlBlock* const c=p.control;
    p=Shared{};
    if(c && InterlockedDecrement(&c->uses)==0) {
        reinterpret_cast<void(__fastcall*)(ControlBlock*)>(c->vtable[0])(c);
        if(InterlockedDecrement(&c->weaks)==0)reinterpret_cast<void(__fastcall*)(ControlBlock*)>(c->vtable[1])(c);
    }
}
struct WideString { union { wchar_t text[8]; const wchar_t* ptr; }; std::uint64_t size,capacity; };
WideString Wide(const wchar_t* s) noexcept {
    WideString w{};
    w.size=std::wcslen(s);
    if(w.size<=7){std::memcpy(w.text,s,w.size*sizeof(wchar_t));w.capacity=7;}
    else{w.ptr=s;w.capacity=w.size;}   // the callee only reads it
    return w;
}
using FindFn=Shared*(__fastcall*)(unsigned char*,Shared*,const WideString*);
Shared Find(unsigned char* self,const wchar_t* name) noexcept {
    Shared s{};
    const WideString n=Wide(name);
    __try { reinterpret_cast<FindFn>(image+kFind)(self,&s,&n); } __except(EXCEPTION_EXECUTE_HANDLER){ s=Shared{}; }
    return s;
}
void Show(void* component,bool shown) noexcept {
    __try { static_cast<unsigned char*>(component)[kShown]=shown ? 1 : 0; } __except(EXCEPTION_EXECUTE_HANDLER){}
}
bool SetShown(unsigned char* self,const wchar_t* name,bool shown) noexcept {
    Shared c=Find(self,name);
    const bool found=c.object!=nullptr;
    if(found)Show(c.object,shown);
    Release(c);
    return found;
}
// The list and its scroll bar (docs/worldmap-select-re.md §2), and the decorative panel model in the screen's middle.
constexpr const wchar_t* kHidden[]={L"LeftButtonsBaseMSel",L"ScrollBarBase",L"Panel03_MissionSelect"};

// --- The list's key of a row (MISSIONLIST field 2, read as 0xE0D80 reads it) ---
struct Handle { const unsigned char* tree; std::uint32_t index,pad; };
static_assert(sizeof(Handle)==16,"the native SGO handle");
using IndexFn=std::uint32_t(__fastcall*)(const Handle*,std::uint32_t);
using TypeFn=int(__fastcall*)(const Handle*);
using TextFn=const wchar_t*(__fastcall*)(const Handle*);
// The display list's row under the cursor (-1: none), and that row's key (null: none).
int CursorRow(const unsigned char* self) noexcept {
    __try {
        const int cursor=*reinterpret_cast<const int*>(self+kCursor);
        const unsigned char* begin=*reinterpret_cast<unsigned char* const*>(self+kEntries);
        const unsigned char* end=*reinterpret_cast<unsigned char* const*>(self+kEntriesEnd);
        if(!begin || end<begin || cursor<0 || static_cast<std::size_t>(end-begin)/kEntrySize<=static_cast<std::size_t>(cursor))
            return -1;
        return *reinterpret_cast<const int*>(begin+static_cast<std::size_t>(cursor)*kEntrySize);
    } __except(EXCEPTION_EXECUTE_HANDLER){ return -1; }
}
const wchar_t* RowKey(int row) noexcept {
    __try {
        unsigned char* const gs=*reinterpret_cast<unsigned char**>(image+kGameStatus);
        if(!gs || row<0)return nullptr;
        unsigned char* const cfg=gs+kModeCfg;
        if(row>=reinterpret_cast<int(__fastcall*)(unsigned char*)>(image+kRowCount)(cfg))return nullptr;
        const Handle list=*reinterpret_cast<const Handle*>(cfg+kListDoc);
        if(!list.tree)return nullptr;
        const Handle line{list.tree,reinterpret_cast<IndexFn>(image+kChildIndex)(&list,static_cast<std::uint32_t>(row)),0};
        if(reinterpret_cast<TypeFn>(image+kNodeType)(&line)!=3 || reinterpret_cast<TypeFn>(image+kNodeCount)(&line)<=2)
            return nullptr;
        const Handle key{list.tree,reinterpret_cast<IndexFn>(image+kChildIndex)(&line,2),0};
        if(reinterpret_cast<TypeFn>(image+kNodeType)(&key)!=1)return nullptr;
        return reinterpret_cast<TextFn>(image+kNodeText)(&key);
    } __except(EXCEPTION_EXECUTE_HANDLER){ return nullptr; }
}

// --- The input devices ---
bool InFront() noexcept {
    DWORD pid=0;
    GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    return pid==GetCurrentProcessId();
}
bool Down(int vk) noexcept { return (edf::AsyncKey(vk)&0x8000)!=0; }

std::atomic<int> wheel{0};            // WM_MOUSEWHEEL's delta since the UI thread last took it
std::atomic<bool> wheelToMap{false};  // the select taken over and the pointer over the map (not its windows)
HWND subclassed=nullptr;
WNDPROC prevProc=nullptr;
bool unicodeProc=true;
LRESULT CALLBACK WheelProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_MOUSEWHEEL && wheelToMap.load(std::memory_order_relaxed)) {
        wheel.fetch_add(GET_WHEEL_DELTA_WPARAM(wp),std::memory_order_relaxed);
        return 0;   // the map's zoom, not the hidden list's scroll
    }
    return unicodeProc ? CallWindowProcW(prevProc,w,msg,wp,lp) : CallWindowProcA(prevProc,w,msg,wp,lp);
}
// The game's window (in front) subclassed for the wheel, once (map.cpp does the same for its own; the two chain).
void Subclass() noexcept {
    if(subclassed)return;
    const HWND w=edf::ForegroundWindow();
    DWORD pid=0;
    if(!w || !GetWindowThreadProcessId(w,&pid) || pid!=GetCurrentProcessId())return;
    unicodeProc=IsWindowUnicode(w)!=FALSE;
    prevProc=reinterpret_cast<WNDPROC>(unicodeProc ? GetWindowLongPtrW(w,GWLP_WNDPROC) : GetWindowLongPtrA(w,GWLP_WNDPROC));
    if(!prevProc)return;
    const LONG_PTR was=unicodeProc ? SetWindowLongPtrW(w,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&WheelProc))
                                   : SetWindowLongPtrA(w,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&WheelProc));
    if(!was){Log("WORLDMAP the window's procedure could not be subclassed (%lu): no wheel zoom (LB / RB, the stick)",GetLastError());return;}
    prevProc=reinterpret_cast<WNDPROC>(was);
    subclassed=w;
    Log("WORLDMAP window %p subclassed for the wheel",static_cast<void*>(w));
}

using XInputGetStateFn=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
XInputGetStateFn xinputGet=nullptr;
bool xinputTried=false;
DWORD padIndex=0;
ULONGLONG padSearchAt=0;
bool PadState(XINPUT_STATE* s) noexcept {
    if(!xinputTried) {
        xinputTried=true;
        HMODULE m=LoadLibraryW(L"xinput1_4.dll");
        if(!m)m=LoadLibraryW(L"xinput9_1_0.dll");
        if(m)xinputGet=reinterpret_cast<XInputGetStateFn>(GetProcAddress(m,"XInputGetState"));
    }
    // A background host's pad instead (edf/input_source.h).
    const auto get=edf::PadSource() ? reinterpret_cast<XInputGetStateFn>(edf::PadSource()) : xinputGet;
    if(!get)return false;
    if(get(padIndex,s)==ERROR_SUCCESS)return true;
    const ULONGLONG now=GetTickCount64();
    if(now-padSearchAt<2000)return false;
    padSearchAt=now;
    for(DWORD i=0;i<XUSER_MAX_COUNT;++i)if(get(i,s)==ERROR_SUCCESS){padIndex=i;return true;}
    return false;
}

// --- The tiles (opened once, kept) ---
TileStore* store=nullptr;   // never freed: its thread is not joined at the DLL's unload
enum class Tiles { untried, open, missing };
Tiles tiles=Tiles::untried;
bool OpenTiles() noexcept {
    if(tiles!=Tiles::untried)return tiles==Tiles::open;
    tiles=Tiles::missing;
    wchar_t path[MAX_PATH];
    const wchar_t* ini=ConfigPath();
    wchar_t* slash=nullptr;
    // <Mods>/Plugins/EDF6VehicleCrew.ini -> <Mods>/ + kTilesFile
    if(!ini || wcscpy_s(path,ini) || !(slash=wcsrchr(path,L'\\')))
        {Log("WORLDMAP the plugin's folder is not known: the mission select stays stock");return false;}
    *slash=0;
    if(!(slash=wcsrchr(path,L'\\'))){Log("WORLDMAP Mods not found above %ls: the mission select stays stock",path);return false;}
    slash[1]=0;
    if(wcscat_s(path,kTilesFile)){Log("WORLDMAP the tiles file's path is too long: the mission select stays stock");return false;}
    for(wchar_t* c=path;*c;++c)if(*c==L'/')*c=L'\\';
    store=new(std::nothrow) TileStore();
    if(!store || !store->Open(path,kTile,kTileLevels,kTilesFormat)) {
        Log("WORLDMAP %ls: %s: the mission select stays stock (the list)",path,store ? store->Why() : "out of memory");
        return false;
    }
    Log("WORLDMAP %ls open: %d tiles (%d levels of %d px)",path,store->Count(),kTileLevels,kTile);
    tiles=Tiles::open;
    return true;
}

// --- What the UI thread hands the draw (under shotLock) ---
struct Shot { bool on=false; unsigned char* select=nullptr; View view; Pixel pointer{-1.f,-1.f}; bool pointerShown=false; };
SRWLOCK shotLock=SRWLOCK_INIT;
Shot shot;
// ...and what the draw hands back: the viewport and the target's size (the pointer's mapping, the sharpness).
struct Geometry { float vp[4]{}; float targetW=0.f,targetH=0.f; };
Geometry geometry;
void Publish(const Shot& s) noexcept { AcquireSRWLockExclusive(&shotLock);shot=s;ReleaseSRWLockExclusive(&shotLock); }
Shot Read() noexcept { AcquireSRWLockShared(&shotLock);const Shot s=shot;ReleaseSRWLockShared(&shotLock);return s; }
Geometry ReadGeometry() noexcept { AcquireSRWLockShared(&shotLock);const Geometry g=geometry;ReleaseSRWLockShared(&shotLock);return g; }

// --- The draw's state (render thread; the destructor takes gpuLock to release it) ---
std::mutex gpuLock;
gpu::Renderer* renderer=nullptr;   // never freed (no device calls at the DLL's unload)
enum : int { kGpuUnknown, kGpuReady, kGpuBroken };
std::atomic<int> gpuState{kGpuUnknown};
unsigned frame=0;
bool failed[kTileCount]{};          // tiles whose JPEG did not decode: not asked again (logged once each)
bool geometryLogged=false;

worldmapui::DrawHook drawHook=nullptr;
void* drawUser=nullptr;
worldmapui::UpdateHook updateHook=nullptr;
void* updateUser=nullptr;

// --- The UI thread's state ---
struct Ui {
    unsigned char* self=nullptr;   // the select followed (its destructor not run)
    bool hidden=false;             // the list's elements hidden by us
    bool started=false;            // the opening move begun (the map drawn once)
    View view,from,to;
    bool moving=false;
    float moveT=0.f;
    bool dragging=false,lbWas=false;
    Pixel dragAt{};
    Pixel pointer{-1.f,-1.f};
    bool pointerShown=false;
};
Ui ui;
std::atomic<unsigned char*> gone{nullptr};   // the select whose destructor ran (set there, taken by the next frame)
constexpr float kMoveSeconds=1.0f;

// Each element's shown flag; the first time for a select, which of them its layout has (the log).
void ShowList(unsigned char* self,bool shown,bool report=false) noexcept {
    int found=0;
    for(const wchar_t* name:kHidden) {
        const bool there=SetShown(self,name,shown);
        found+=there ? 1 : 0;
        if(report && !there)Log("WORLDMAP select %p: its layout has no %ls (left as it is)",static_cast<void*>(self),name);
    }
    if(report)Log("WORLDMAP select %p: %d of %d list elements hidden",static_cast<void*>(self),found,
                  static_cast<int>(std::size(kHidden)));
}

// The row under the cursor's place: the opening move's end (the whole Earth's centre when the key is not known).
void Start(unsigned char* self,const Screen& s) noexcept {
    const int row=CursorRow(self);
    const wchar_t* key=RowKey(row);
    const int place=key ? PlaceOfKey(key) : -1;
    if(place<0) {
        ui.view=Whole(0.5f,s);
        ui.moving=false;
        Log("WORLDMAP select %p: the cursor's row %d (key %ls) has no place: the whole Earth",static_cast<void*>(self),row,
            key ? key : L"(none)");
    } else {
        const MapPoint p=Project(kPlaces[place].lat,kPlaces[place].lon);
        ui.from=Whole(p.u,s);
        ui.to=FramePlace(p,s);
        ui.view=ui.from;
        ui.moving=true;
        ui.moveT=0.f;
        Log("WORLDMAP select %p: opening on %ls (row %d, key %ls)",static_cast<void*>(self),kPlaces[place].id,row,key);
    }
    ui.started=true;
}

// The pointer in design units (the game in front, over its client area).
void ReadPointer(const Screen& s) noexcept {
    ui.pointer=Pixel{-1.f,-1.f};
    ui.pointerShown=false;
    const HWND w=edf::ForegroundWindow();
    DWORD pid=0;
    if(!w || !GetWindowThreadProcessId(w,&pid) || pid!=GetCurrentProcessId())return;
    POINT p{};
    RECT client{};
    if(!GetCursorPos(&p) || !ScreenToClient(w,&p) || !GetClientRect(w,&client))return;
    const Geometry g=ReadGeometry();
    ui.pointer=ClientToDesign(static_cast<float>(p.x),static_cast<float>(p.y),static_cast<float>(client.right),
                              static_cast<float>(client.bottom),g.targetW,g.targetH,g.vp,s);
    ui.pointerShown=OnScreen(ui.pointer,s);
}

// This frame's pan / zoom; true when the map used input.
bool TakeInput(const Screen& s,float dt,bool input) noexcept {
    const bool front=InFront();
    ReadPointer(s);
    const bool overMap=ui.pointerShown && !OverPanels(ui.pointer);
    wheelToMap.store(input && front && overMap,std::memory_order_relaxed);
    const int notches=wheel.exchange(0);
    if(!input) {
        ui.dragging=false;
        ui.lbWas=front && Down(VK_LBUTTON);
        return false;
    }
    bool used=false;
    const bool lb=front && Down(VK_LBUTTON);
    if(lb && !ui.lbWas && overMap){ui.dragging=true;ui.dragAt=ui.pointer;used=true;}
    else if(lb && ui.dragging) {
        if(ui.pointerShown){Pan(ui.view,s,ui.pointer.x-ui.dragAt.x,ui.pointer.y-ui.dragAt.y);ui.dragAt=ui.pointer;}
        used=true;
    }
    if(!lb)ui.dragging=false;
    ui.lbWas=lb;
    if(notches && overMap){ZoomAt(ui.view,s,ui.pointer,WheelZoom(notches));used=true;}
    XINPUT_STATE pad{};
    if((front || edf::PadSource()) && PadState(&pad)) {
        const float rx=Stick(pad.Gamepad.sThumbRX),ry=Stick(pad.Gamepad.sThumbRY);
        if(rx!=0.f || ry!=0.f){const Pixel d=StickPan(rx,ry,dt);Pan(ui.view,s,d.x,d.y);used=true;}
        const bool out=(pad.Gamepad.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0;
        const bool in=(pad.Gamepad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0;
        if(out || in) {
            const float f=HoldZoom(out,in,dt);
            if(f!=1.f)ZoomAt(ui.view,s,kMapFocus,f);
            used=true;
        }
    }
    return used;
}

// A new select object (or the same address after its destructor ran): everything of the last one dropped.
void Follow(unsigned char* self) noexcept {
    unsigned char* const was=gone.exchange(nullptr);
    if(self==ui.self && was!=self)return;
    ui=Ui{};
    ui.self=self;
}

float ReadDt(const unsigned char* ctx) noexcept {
    float dt=0.f;
    __try { dt=*reinterpret_cast<const float*>(ctx+kCtxDt); } __except(EXCEPTION_EXECUTE_HANDLER){ dt=0.f; }
    return dt>0.f && dt<0.25f ? dt : 1.f/60.f;
}
bool OwnsInput(const unsigned char* ctx) noexcept {
    __try { return *reinterpret_cast<void* const*>(ctx+kCtxInput)!=nullptr; } __except(EXCEPTION_EXECUTE_HANDLER){ return false; }
}

std::uint64_t __fastcall UpdateHook(unsigned char* self,unsigned char* ctx) {
    Follow(self);
    const int gs=gpuState.load();
    if(!Cfg().enabled || !ctx || !OpenTiles() || gs==kGpuBroken) {
        if(ui.hidden){ShowList(self,true);ui.hidden=false;}
        Shot off;
        Publish(off);
        return nextUpdate(self,ctx);
    }
    const Geometry g=ReadGeometry();
    const Screen s{1920.f,1080.f,g.vp[2]};
    const float dt=ReadDt(ctx);
    const bool input=OwnsInput(ctx);
    bool used=false;
    if(gs==kGpuReady) {
        // The list goes only once the map has been drawn (a device the draw cannot use keeps the stock list).
        ShowList(self,false,!ui.hidden);
        ui.hidden=true;
        if(!ui.started){Start(self,s);Subclass();}
        used=TakeInput(s,dt,input);
        if(used)ui.moving=false;   // any input: the opening move ends where it is
        if(ui.moving) {
            ui.moveT+=dt/kMoveSeconds;
            ui.view=Blend(ui.from,ui.to,ui.moveT,s);
            if(ui.moveT>=1.f)ui.moving=false;
        }
    } else if(!ui.started) {
        ui.view=Whole(0.5f,s);   // the first draw (the device's objects made) shows the whole Earth
    }
    Clamp(ui.view,s);
    if(gs==kGpuReady && updateHook) {
        worldmapui::Frame f;
        f.view=ui.view;f.screen=s;
        f.viewport=gpu::Viewport{g.vp[0],g.vp[1],g.vp[2],g.vp[3]};
        f.pointer=ui.pointer;f.pointerShown=ui.pointerShown;f.select=self;
        if(updateHook(f,input && !used,dt,updateUser))used=true;
    }
    Shot on;
    on.on=true;on.select=self;on.view=ui.view;on.pointer=ui.pointer;on.pointerShown=ui.pointerShown;
    Publish(on);
    if(used) {
        // The stock OnUpdate as when it does not own the input (§5.2): no scroll, no button; its cursor refresh runs.
        alignas(8) unsigned char copy[kCtxSize];
        std::memcpy(copy,ctx,kCtxSize);
        std::memset(copy+kCtxInput,0,sizeof(void*));
        return nextUpdate(self,copy);
    }
    return nextUpdate(self,ctx);
}

// --- The draw ---
bool ReadDrawCtx(const unsigned char* ctx,ID3D11DeviceContext** device,int* vp) noexcept {
    __try {
        *device=*reinterpret_cast<ID3D11DeviceContext* const*>(ctx+kDrawDevice);
        std::memcpy(vp,ctx+kDrawViewport,4*sizeof(int));
        return *device!=nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER){ return false; }
}

void DrawMap(unsigned char* self,unsigned char* ctx,const Shot& s) noexcept {
    ID3D11DeviceContext* device=nullptr;
    int vp[4]{};
    if(!ReadDrawCtx(ctx,&device,vp))return;
    std::lock_guard<std::mutex> hold(gpuLock);
    if(!renderer)renderer=new(std::nothrow) gpu::Renderer();
    if(!renderer)return;
    const gpu::Viewport viewport{static_cast<float>(vp[0]),static_cast<float>(vp[1]),static_cast<float>(vp[2]),
                                 static_cast<float>(vp[3])};
    const gpu::Renderer::Begun begun=renderer->Begin(device,viewport);
    if(begun==gpu::Renderer::Begun::broken) {
        if(gpuState.exchange(kGpuBroken)!=kGpuBroken)
            Log("WORLDMAP the map cannot be drawn on this device: %s: the mission select stays stock",renderer->Why());
        renderer->Reset();
        return;
    }
    if(begun!=gpu::Renderer::Begun::ok)return;
    struct Ender { gpu::Renderer* r; ~Ender() { r->End(); } } ender{renderer};
    ++frame;
    const Screen screen{1920.f,1080.f,viewport.w};
    View v=s.view;
    Clamp(v,screen);
    // The ocean's colour under tiles not there yet (the first frames: nothing decoded).
    renderer->Rect(0.f,0.f,screen.w,screen.h,0xFF0B1B30u);
    const int level=LevelFor(v,screen);
    const TileSpan span=Visible(v,screen,level);
    constexpr int kMostVisible=64;
    int xy[2*kMostVisible];
    const int n=CentreFirst(v,screen,span,xy,kMostVisible);
    auto has=[](int i) { return i>=0 && renderer->Has(i); };
    for(int i=0;i<n;++i) {
        const int x=xy[2*i],y=xy[2*i+1];
        int ax=0,ay=0;
        MapRect uv{};
        const int z=Cover(level,WrapColumn(level,x),y,has,&ax,&ay,&uv);
        if(z<0)continue;
        const MapRect r=TileRect(level,x,y);
        const Pixel p0=ToScreenUnwrapped(v,screen,r.u0,r.v0),p1=ToScreenUnwrapped(v,screen,r.u1,r.v1);
        renderer->DrawTile(TileIndex(z,ax,ay),p0.x,p0.y,p1.x,p1.y,uv.u0,uv.v0,uv.u1,uv.v1,frame);
    }
    // The pointer: a small cross (the select draws no cursor of its own over the map; stage 2's pins are under it).
    if(s.pointerShown) {
        const float x=s.pointer.x,y=s.pointer.y;
        renderer->Line(x-11.f,y,x+11.f,y,4.f,0xC0000000u);
        renderer->Line(x,y-11.f,x,y+11.f,4.f,0xC0000000u);
        renderer->Line(x-10.f,y,x+10.f,y,2.f,0xFFFFFFFFu);
        renderer->Line(x,y-10.f,x,y+10.f,2.f,0xFFFFFFFFu);
    }
    if(drawHook) {
        worldmapui::Frame f;
        f.view=v;f.screen=screen;f.viewport=viewport;f.pointer=s.pointer;f.pointerShown=s.pointerShown;f.select=self;
        drawHook(*renderer,f,drawUser);
    }
    // What arrived since the last frame into textures (drawn from the next frame); then what is missing, wanted:
    // levels 0 and 1 (resident: under everything) first, then this view's tiles from the centre out.
    TilePixels tile;
    for(int k=0;k<3 && store->Take(tile);++k) {
        if(tile.index<0 || tile.index>=kTileCount)continue;
        if(tile.w<=0) {
            if(!failed[tile.index])Log("WORLDMAP tile %d (level %d) did not decode: left out",tile.index,LevelOfIndex(tile.index));
            failed[tile.index]=true;
            continue;
        }
        renderer->Upload(tile,LevelOfIndex(tile.index),frame);
    }
    int want[3+kMostVisible];
    int wanted=0;
    for(int i=0;i<3;++i)if(!renderer->Has(i) && !failed[i])want[wanted++]=i;
    for(int i=0;i<n;++i) {
        const int index=TileIndex(level,WrapColumn(level,xy[2*i]),xy[2*i+1]);
        if(index>=0 && !failed[index] && !renderer->Has(index))want[wanted++]=index;
    }
    store->Want(want,wanted);
    // The viewport and the target's size for the UI thread (the pointer, the sharpness).
    Geometry g;
    g.vp[0]=viewport.x;g.vp[1]=viewport.y;g.vp[2]=viewport.w;g.vp[3]=viewport.h;
    g.targetW=static_cast<float>(renderer->TargetWidth());
    g.targetH=static_cast<float>(renderer->TargetHeight());
    AcquireSRWLockExclusive(&shotLock);
    geometry=g;
    ReleaseSRWLockExclusive(&shotLock);
    if(!geometryLogged) {
        geometryLogged=true;
        Log("WORLDMAP drawing: viewport (%d, %d) %d x %d, target %d x %d",vp[0],vp[1],vp[2],vp[3],renderer->TargetWidth(),
            renderer->TargetHeight());
    }
    gpuState.store(kGpuReady);
}

std::uint64_t __fastcall DrawHook(unsigned char* self,unsigned char* ctx) {
    if(ctx) {
        const Shot s=Read();
        if(s.on && s.select==self && gpuState.load()!=kGpuBroken && store)DrawMap(self,ctx,s);
    }
    return nextDraw(self,ctx);
}

void* __fastcall GoneHook(unsigned char* self,unsigned flags) {
    {
        AcquireSRWLockExclusive(&shotLock);
        const bool ours=shot.select==self;
        if(ours)shot=Shot{};
        ReleaseSRWLockExclusive(&shotLock);
        if(ours || ui.self==self) {
            gone.store(self);
            wheelToMap.store(false);
            std::lock_guard<std::mutex> hold(gpuLock);
            if(renderer)renderer->Reset();   // every texture and device object (made again by the next select)
            Log("WORLDMAP select %p gone: its textures released",static_cast<void*>(self));
        }
    }
    return nextGone(self,flags);
}

// `next` is set before the slot points at the hook.
template<class Fn> bool Chain(unsigned slotRva,unsigned stock,void* hook,Fn& next,const char* what) noexcept {
    void** const slot=reinterpret_cast<void**>(image+slotRva);
    void* const current=Readable(slot,8) ? *slot : nullptr;
    if(!current){Log("HOOK worldmap REFUSED: the select's %s slot is not readable / empty",what);return false;}
    if(current!=image+stock)Log("HOOK worldmap: the select's %s chains onto %p (another plugin)",what,current);
    next=reinterpret_cast<Fn>(current);
    if(PatchVtableSlot(slot,current,hook))return true;
    Log("HOOK worldmap REFUSED: the select's %s slot could not be patched",what);
    return false;
}
}  // namespace

namespace worldmapui {
void SetDrawHook(DrawHook hook,void* user) noexcept { drawUser=user;drawHook=hook; }
void SetUpdateHook(UpdateHook hook,void* user) noexcept { updateUser=user;updateHook=hook; }
}  // namespace worldmapui

bool InstallWorldMap() noexcept {
    for(const Sig& s:kSigs)
        if(!Matches(s.rva,s.bytes,s.size)) {
            Log("HOOK worldmap REFUSED: the code at EDF+0x%X is not as expected: the mission select stays stock (the list)",s.rva);
            return false;
        }
    // The destructor first (a new select at a dead one's address must not inherit its state), then the draw, then the
    // update (which hides the list): a later slot that fails leaves the earlier ones inert.
    if(!Chain(kGoneSlot,kGone,reinterpret_cast<void*>(&GoneHook),nextGone,"destructor") ||
       !Chain(kDrawSlot,kDraw,reinterpret_cast<void*>(&DrawHook),nextDraw,"Draw") ||
       !Chain(kUpdateSlot,kUpdate,reinterpret_cast<void*>(&UpdateHook),nextUpdate,"OnUpdate"))return false;
    Log("HOOK worldmap select chained (vtable 0x18052E8 slots 1, 2, 5): the list becomes the world map once a select opens");
    return true;
}

}  // namespace crew
