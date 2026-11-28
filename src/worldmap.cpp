// The world map mission select: worldmap.h. EDF.dll TimeDateStamp 0x678CCB46, RVAs; the RE is
// docs/worldmap-select-re.md (draw slot, layout, cursor, input, viewport) and docs/plan-2026-11-10-worldmap.md §2.
//
// ui::HUiHQMissionSelect's vtable 0x18052E8, three slots chained (read and chained, never assumed stock):
//  - slot 1, OnUpdate 0x8A4F60 (UI thread): the list's elements hidden (shown flag +0x1F8), the map's input read
//    (a left drag pans, the wheel zooms about the pointer, the right stick pans, LB / LT and Z / C zoom, Q / E step the
//    整备 tabs as in 整备; only while the select
//    owns the input: ctx+8, its ui::Input*, not null), the opening move (the whole Earth, then in about a second to
//    the current mission and its routes' ends), choosing (left / right, R3, a pin or panel line clicked: the native
//    focus 0x953350; a second click: the stock confirm 0x8A85C0), the rows, pins and panel published for the draw. On a frame the map used input
//    the stock OnUpdate gets a copy of its ctx with +8 null (§5.2: it then neither scrolls the hidden list nor
//    presses its buttons; its cursor refresh still runs).
//  - slot 2, Draw 0x7FA970 (the render command queue's thread): the map drawn into the select's viewport (ctx+0x398)
//    on its D3D11 context (ctx+8) through ui_gpu.h (the shared private pipeline; the tiles' cache is this file's TileCache) with the routes and pins, then the stock draw (the kept
//    windows: thumbnail, medals, briefing), then the map again over their skins' empty frame on the right and the
//    place's panel on it (docs/worldmap-select-re.md §8.1); every later screen (a dialog, the main frame) over all.
//  - slot 5, the scalar deleting destructor 0x8A3650: the object's end. Every texture and the device state go with it
//    (the next select makes them again); the tiles file stays open (worldmap_tiles.h: its table read once).
// The tiles file (Mods/Plugins/EDF6VehicleCrew.worldmap.bin) is opened at the first select; when it cannot be (not
// installed, another layout) the log says why once and every select stays stock. The same when the draw cannot run
// on this device (the list is hidden only once the map has been drawn).
#include "worldmap.h"
#include "ui_text.h"
#include "armory.h"
#include "armory_presets.h"
#include "worldmap_tiles.h"
#include "edf/input_source.h"
#include "edf_layout.h"
#include "config.h"
#include "plugin_log.h"
#include "infantryscope.h"
#include "loadout_store.h"
#include "room_sync.h"
#include "memory.h"
#include "map.h"

#include <Xinput.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <new>
#include <vector>

namespace crew {
namespace {
using namespace worldmap;
namespace gpu=uigpu;

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
constexpr unsigned kFocus=0x953350;     // (HuiButtonListUtil* select+0x770, int i): the native focus to display entry i
constexpr unsigned kConfirm=0x8A85C0;   // cbRequestMission(select, const Result*): confirms entry +0x778 (rdx not read)
constexpr unsigned kVerticalMouseCall=0x83FAE3,kVerticalMouse=0x7F0480;
constexpr unsigned kTextOf=0x7A3500;    // const wchar_t* (text table, const wchar_t* key)
constexpr unsigned kNumber=0x52F50;     // double (const Handle*): a number node's value
constexpr unsigned kNotOpenKey=0x1810C80;   // L"MissionSelect_NotOpenMissionTitle" (the locked row's title, 0x9591B6)
constexpr std::size_t kTextTable=0x20B29A8,kTitles=0x248,kCurrent=0x48;   // [EDF+..]: texts; [GS]+0x248 the TXT list; GS+0x48
constexpr std::size_t kButtons=0x770,kSelectMode=0x7B4;   // the select's HuiButtonListUtil; +0x7B4 bit 0: confirmable
constexpr std::size_t kRepeat=0x4528;
constexpr unsigned kTransform=0x7F9E90;   // float* (HUiBase*, float out[16]): the screen's transform (HUiBase::Draw 0x7FAA0A)   // ui::Input: every pad's repeat mask (1 up, 2 down, 4 left, 8 right)
constexpr unsigned kLeftBit=4,kRightBit=8;
// ui::Input (docs/worldmap-select-re.md §8.2): the pads' slots +0x4518 (two ints), each pad's record pad * 0xDD0 with
// its pressed mask at +0xDBC, the merged masks +0x4520..+0x452C (held, pressed, repeat, released); 0x80 = 返回.
constexpr std::size_t kInputPads=0x4518,kPadRecord=0xDD0,kPadPressed=0xDBC,kInputPressed=0x4524,kInputCopy=0x4530;
constexpr unsigned char kBackBit=0x80;
constexpr unsigned kInvokeSlot=0x18053A0,kInvoke=0x8A4060;   // Agent_Object<HUiHQMissionSelect> vtable 0x1805398 slot 1
constexpr std::size_t kFunctorScreen=0x8,kFunctorPmf=0x18;

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
// 0x953350: test edx,edx; js; mov [rsp+8],rbx; mov [rsp+10h],rsi (the focus request, docs/worldmap-select-re.md §3.1)
const unsigned char kFocusSig[]={0x85,0xD2,0x0F,0x88,0x22,0x01,0x00,0x00,0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74};
const unsigned char kConfirmSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x55,0x48,0x8D,0x6C,0x24,0xA9};
const unsigned char kTextOfSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48};
const unsigned char kNumberSig[]={0x48,0x83,0xEC,0x28,0x4C,0x8B,0xD1,0xE8,0x54,0xFF,0xFF,0xFF,0x84,0xC0,0x74,0x11};
// 0x9591AF (a locked row's title): mov rcx,[EDF+20B29A8h]; lea rdx,[kNotOpenKey]; call 0x7A3500
const unsigned char kNotOpenSig[]={0x48,0x8B,0x0D,0xF2,0x97,0x75,0x01,0x48,0x8D,0x15,0xC3,0x7A,0xEB,0x00,0xE8,0x3E,0xA3,0xE4,0xFF};
// 0x958A66 (a row's title): mov rax,[GS]; mov rbx,rcx; mov r10,[rax+248h]
const unsigned char kTitleSig[]={0x48,0x8B,0x05,0x23,0x9E,0x75,0x01,0x48,0x8B,0xD9,0x4C,0x8B,0x90,0x48,0x02,0x00,0x00};
// 0x8A26AA (the select's constructor): test byte [rsi+7B4h],1 (0: the room guest's view, no button confirmable)
const unsigned char kModeSig[]={0xF6,0x86,0xB4,0x07,0x00,0x00,0x01};
// 0x8A5371 (the stock OnUpdate): lea r8,[r14+4528h] (the input's merged repeat mask)
const unsigned char kRepeatSig[]={0x4D,0x8D,0x86,0x28,0x45,0x00,0x00};
// 0xDC0B0: mov [rcx+48h],edx; test r8b,r8b; je; mov [rcx+57A4h],edx; ret (the current mission row, as the cursor sets it)
// 0x7F9E90: mov rax,rsp; mov [rax+8],rbx; mov [rax+10h],rsi; mov [rax+18h],rdi; push rbp
const unsigned char kTransformSig[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x55};
const unsigned char kCurrentSig[]={0x89,0x51,0x48,0x45,0x84,0xC0,0x74,0x06,0x89,0x91,0xA4,0x57,0x00,0x00,0xC3};
// Stage 6 (docs/worldmap-select-re.md §10). 0x84B544 (HUiLayout's update, the 返回 key): mov eax,[rbx+34h]; cmp eax,-1;
// jne; lea rax,[rdi+4524h]; jmp; movsxd rcx,[rdi+rax*4+4518h]; imul rax,rcx,0DD0h; add rax,0DBCh; add rax,rdi;
// test byte [rax],80h (the pressed mask's 返回 bit, merged or the screen's pad's)
const unsigned char kBackSig[]={0x8B,0x43,0x34,0x83,0xF8,0xFF,0x75,0x09,0x48,0x8D,0x87,0x24,0x45,0x00,0x00,0xEB,0x18,0x48,
    0x63,0x8C,0x87,0x18,0x45,0x00,0x00,0x48,0x69,0xC1,0xD0,0x0D,0x00,0x00,0x48,0x05,0xBC,0x0D,0x00,0x00,0x48,0x03,0xC7,0xF6,
    0x00,0x80};
// 0x8A4060 Agent_Object<HUiHQMissionSelect>::Invoke (vtable 0x1805398 slot 1) and its call 0x8A40E9:
// movsxd rcx,[rbp+20h]; mov rdx,r14; add rcx,rsi; call [rbp+18h] (the screen locked from the weak_ptr at +8, pmf +0x18)
const unsigned char kInvokeSig[]={0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x30};
const unsigned char kInvokeCallSig[]={0x48,0x63,0x4D,0x20,0x49,0x8B,0xD6,0x48,0x03,0xCE,0xFF,0x55,0x18};
const Sig kSigs[]={{kUpdate,kUpdateSig,sizeof(kUpdateSig)},{kDraw,kDrawSig,sizeof(kDrawSig)},{kGone,kGoneSig,sizeof(kGoneSig)},
    {kFind,kFindSig,sizeof(kFindSig)},{0x7E999C,kShowSig,sizeof(kShowSig)},{kRowCount,kRowCountSig,sizeof(kRowCountSig)},
    {kChildIndex,kChildIndexSig,sizeof(kChildIndexSig)},{kNodeType,kNodeTypeSig,sizeof(kNodeTypeSig)},
    {kNodeCount,kNodeCountSig,sizeof(kNodeCountSig)},{kNodeText,kNodeTextSig,sizeof(kNodeTextSig)},
    {0xE0DAE,kKeyReadSig,sizeof(kKeyReadSig)},{0x8A2BC1,kCfgSig,sizeof(kCfgSig)},{0x8A85E8,kCursorSig,sizeof(kCursorSig)},
    {0x8A5029,kInputSig,sizeof(kInputSig)},{0x8A5303,kDtSig,sizeof(kDtSig)},{0x11A43EA,kExecSig,sizeof(kExecSig)},
    {0x113E90B,kViewportSig,sizeof(kViewportSig)},{0x113E928,kDeviceSig,sizeof(kDeviceSig)},
    {kFocus,kFocusSig,sizeof(kFocusSig)},{kConfirm,kConfirmSig,sizeof(kConfirmSig)},{kTextOf,kTextOfSig,sizeof(kTextOfSig)},
    {kNumber,kNumberSig,sizeof(kNumberSig)},{0x9591AF,kNotOpenSig,sizeof(kNotOpenSig)},{0x958A66,kTitleSig,sizeof(kTitleSig)},
    {0x8A26AA,kModeSig,sizeof(kModeSig)},{0x8A5371,kRepeatSig,sizeof(kRepeatSig)},{0xDC0B0,kCurrentSig,sizeof(kCurrentSig)},
    {kTransform,kTransformSig,sizeof(kTransformSig)}};
// Stage 6's, each part checked on its own (a mismatch leaves that part out, not the map).
const Sig kBackSigs[]={{0x84B544,kBackSig,sizeof(kBackSig)}};
const Sig kLaunchSigs[]={{kInvoke,kInvokeSig,sizeof(kInvokeSig)},{0x8A40E9,kInvokeCallSig,sizeof(kInvokeCallSig)}};
template<std::size_t N> bool AllMatch(const Sig (&sigs)[N]) noexcept {
    for(const Sig& s:sigs)if(!Matches(s.rva,s.bytes,s.size))return false;
    return true;
}

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
// The display list's states (docs/mission-list-re.md 0x8A1530: 0 cleared, 1 open, 2 open branch, 3 shown locked) counted
// into n[4], the first locked entry's index in *locked (-1: none); false when the list is not readable.
bool CountStates(const unsigned char* self,int* n,int* locked) noexcept {
    n[0]=n[1]=n[2]=n[3]=0;
    *locked=-1;
    __try {
        const unsigned char* begin=*reinterpret_cast<unsigned char* const*>(self+kEntries);
        const unsigned char* end=*reinterpret_cast<unsigned char* const*>(self+kEntriesEnd);
        if(!begin || end<begin)return false;
        const std::size_t count=static_cast<std::size_t>(end-begin)/kEntrySize;
        for(std::size_t i=0;i<count;++i) {
            const int state=*reinterpret_cast<const int*>(begin+i*kEntrySize+8);
            if(state>=0 && state<=3)++n[state];
            if(state==3 && *locked<0)*locked=static_cast<int>(i);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){ return false; }
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
// A row's successors (MISSIONLIST field 3, docs/mission-list-re.md: row numbers, read as 0xE1900 reads them) into
// out; how many (0: none or not readable).
using NumberFn=double(__fastcall*)(const Handle*);
int Successors(int row,int* out,int capacity) noexcept {
    __try {
        unsigned char* const gs=*reinterpret_cast<unsigned char**>(image+kGameStatus);
        if(!gs || row<0)return 0;
        unsigned char* const cfg=gs+kModeCfg;
        if(row>=reinterpret_cast<int(__fastcall*)(unsigned char*)>(image+kRowCount)(cfg))return 0;
        const Handle list=*reinterpret_cast<const Handle*>(cfg+kListDoc);
        if(!list.tree)return 0;
        const Handle line{list.tree,reinterpret_cast<IndexFn>(image+kChildIndex)(&list,static_cast<std::uint32_t>(row)),0};
        if(reinterpret_cast<TypeFn>(image+kNodeType)(&line)!=3 || reinterpret_cast<TypeFn>(image+kNodeCount)(&line)<=3)return 0;
        const Handle next{list.tree,reinterpret_cast<IndexFn>(image+kChildIndex)(&line,3),0};
        if(reinterpret_cast<TypeFn>(image+kNodeType)(&next)!=3)return 0;
        const int count=reinterpret_cast<TypeFn>(image+kNodeCount)(&next);
        int k=0;
        for(int i=0;i<count && k<capacity;++i) {
            const Handle n{list.tree,reinterpret_cast<IndexFn>(image+kChildIndex)(&next,static_cast<std::uint32_t>(i)),0};
            out[k++]=static_cast<int>(std::lround(reinterpret_cast<NumberFn>(image+kNumber)(&n)));
        }
        return k;
    } __except(EXCEPTION_EXECUTE_HANDLER){ return 0; }
}
// The list's row count (0: not readable).
int RowCount() noexcept {
    __try {
        unsigned char* const gs=*reinterpret_cast<unsigned char**>(image+kGameStatus);
        return gs ? reinterpret_cast<int(__fastcall*)(unsigned char*)>(image+kRowCount)(gs+kModeCfg) : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER){ return 0; }
}
// A row's title copied into out (docs/worldmap-select-re.md §4: [GS]+0x248, the TXT list, row's child 0; no
// allocation, as 0x958A60 reads it); the caller keeps row < RowCount().
void Title(int row,wchar_t* out,std::size_t capacity) noexcept {
    out[0]=0;
    __try {
        const unsigned char* const gs=*reinterpret_cast<unsigned char* const*>(image+kGameStatus);
        const unsigned char* const n=gs ? *reinterpret_cast<const unsigned char* const*>(gs+kTitles) : nullptr;
        if(!n || row<0)return;
        const unsigned char* const c=n+*reinterpret_cast<const std::int32_t*>(n+8);
        const unsigned char* const r=c+12*static_cast<std::size_t>(row);
        const unsigned char* const a=r+*reinterpret_cast<const std::int32_t*>(r+8);
        const wchar_t* const title=reinterpret_cast<const wchar_t*>(a+*reinterpret_cast<const std::int32_t*>(a+8));
        wcsncpy_s(out,capacity,title,_TRUNCATE);
    } __except(EXCEPTION_EXECUTE_HANDLER){ out[0]=0; }
}
// The locked row's title (MissionSelect_NotOpenMissionTitle in the game's text table) copied into out.
void NotOpenTitle(wchar_t* out,std::size_t capacity) noexcept {
    out[0]=0;
    __try {
        void* const table=*reinterpret_cast<void**>(image+kTextTable);
        if(!table)return;
        const wchar_t* const text=reinterpret_cast<const wchar_t*(__fastcall*)(void*,const wchar_t*)>(image+kTextOf)(
            table,reinterpret_cast<const wchar_t*>(image+kNotOpenKey));
        if(text)wcsncpy_s(out,capacity,text,_TRUNCATE);
    } __except(EXCEPTION_EXECUTE_HANDLER){ out[0]=0; }
}
// The current mission row (GS+0x48; the select's cursor refresh rewrites it, so read before its first refresh).
int CurrentRow() noexcept {
    __try {
        const unsigned char* const gs=*reinterpret_cast<unsigned char* const*>(image+kGameStatus);
        return gs ? *reinterpret_cast<const int*>(gs+kCurrent) : -1;
    } __except(EXCEPTION_EXECUTE_HANDLER){ return -1; }
}
int CursorIndex(const unsigned char* self) noexcept {
    __try { return *reinterpret_cast<const int*>(self+kCursor); } __except(EXCEPTION_EXECUTE_HANDLER){ return -1; }
}
// Its buttons confirmable (0: the room guest's view of the host's choice).
bool Confirmable(const unsigned char* self) noexcept {
    __try { return (self[kSelectMode]&1)!=0; } __except(EXCEPTION_EXECUTE_HANDLER){ return false; }
}
// The native focus to entry i (the stock OnUpdate's layout update moves it; its focus callback then writes +0x778).
void Focus(unsigned char* self,int i) noexcept {
    __try { reinterpret_cast<void(__fastcall*)(unsigned char*,int)>(image+kFocus)(self+kButtons,i); }
    __except(EXCEPTION_EXECUTE_HANDLER){ Log("WORLDMAP the focus request faulted"); }
}
void Confirm(unsigned char* self) noexcept {
    __try { reinterpret_cast<void(__fastcall*)(unsigned char*,const void*)>(image+kConfirm)(self,nullptr); }
    __except(EXCEPTION_EXECUTE_HANDLER){ Log("WORLDMAP the stock confirm faulted"); }
}
unsigned RepeatMask(const unsigned char* ctx) noexcept {
    __try {
        const unsigned char* const input=*reinterpret_cast<const unsigned char* const*>(ctx+kCtxInput);
        return input ? *reinterpret_cast<const unsigned*>(input+kRepeat) : 0u;
    } __except(EXCEPTION_EXECUTE_HANDLER){ return 0u; }
}

// --- The input devices ---
bool InFront() noexcept {
    DWORD pid=0;
    GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    return pid==GetCurrentProcessId();
}
bool Down(int vk) noexcept { return (edf::AsyncKey(vk)&0x8000)!=0; }

std::atomic<int> wheel{0};            // WM_MOUSEWHEEL's delta since the UI thread last took it
// Until when (GetTickCount64) the wheel is the map's: the select taken over and the pointer over the map (not its
// windows), as its last frame saw it. A deadline, not a flag: a select whose frames stop (hidden under another menu,
// gone) gives the wheel back by itself.
std::atomic<unsigned long long> wheelToMapUntil{0};
constexpr unsigned long long kWheelHoldMs=250;
bool WheelToMap() noexcept { return GetTickCount64()<wheelToMapUntil.load(std::memory_order_relaxed); }
HWND subclassed=nullptr;
WNDPROC prevProc=nullptr;
bool unicodeProc=true;
LRESULT CALLBACK WheelProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_MOUSEWHEEL && WheelToMap()) {
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
    if(!was){Log("WORLDMAP the window's procedure could not be subclassed (%lu): no wheel zoom (LB / LT, Z / C, the stick)",GetLastError());return;}
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

// --- The select's rows as the map shows them (UI thread; read once a select, its list does not change while open) ---
constexpr int kMaxEntries=512;   // the display list (ModeSave's 512 rows at most; docs/mission-list-re.md)
constexpr int kMaxTargets=16;
struct Rows {
    int n=0;
    int row[kMaxEntries]{},state[kMaxEntries]{},field5[kMaxEntries]{},place[kMaxEntries]{};
    Pin pins[kPlaceCount]{};
    int pinCount=0;
    int current=-1;          // the current mission's row (GS+0x48 before the select's first refresh), -1 none
    int currentEntry=-1;     // its display entry
    int currentPlace=-1;
    int targets[kMaxTargets]{};   // the places of its successors shown (the routes), each place's best state
    int targetState[kMaxTargets]{};
    int targetCount=0;
    wchar_t notOpen[64]{};
};
Rows rows;

// Unknown keys logged once each (a list the plugin's table does not know: no pin).
std::uint32_t loggedKeys[64];
int loggedKeyCount=0;
void LogUnknownKey(int row,const wchar_t* key) noexcept {
    std::uint32_t h=2166136261u;
    for(const wchar_t* c=key;*c;++c)h=(h^static_cast<std::uint32_t>(*c))*16777619u;
    for(int i=0;i<loggedKeyCount;++i)if(loggedKeys[i]==h)return;
    if(loggedKeyCount<static_cast<int>(std::size(loggedKeys)))loggedKeys[loggedKeyCount++]=h;
    Log("WORLDMAP row %d (key %ls) has no place: no pin",row,key);
}

void LoadRows(unsigned char* self) noexcept {
    Rows& r=rows;
    const int current=r.current;
    r=Rows{};
    r.current=current;
    __try {
        const unsigned char* begin=*reinterpret_cast<unsigned char* const*>(self+kEntries);
        const unsigned char* end=*reinterpret_cast<unsigned char* const*>(self+kEntriesEnd);
        if(begin && end>begin) {
            std::size_t count=static_cast<std::size_t>(end-begin)/kEntrySize;
            if(count>static_cast<std::size_t>(kMaxEntries))count=kMaxEntries;
            for(std::size_t i=0;i<count;++i) {
                const unsigned char* e=begin+i*kEntrySize;
                r.row[i]=*reinterpret_cast<const int*>(e);
                r.state[i]=*reinterpret_cast<const int*>(e+8);
                r.field5[i]=*reinterpret_cast<const int*>(e+0xC);
            }
            r.n=static_cast<int>(count);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){ r.n=0; }
    int unknown=0;
    for(int i=0;i<r.n;++i) {
        const wchar_t* key=RowKey(r.row[i]);
        r.place[i]=key ? PlaceOfKey(key) : -1;
        if(r.place[i]<0){++unknown;if(key)LogUnknownKey(r.row[i],key);}
        if(r.row[i]==r.current && r.currentEntry<0)r.currentEntry=i;
    }
    r.pinCount=GroupPins(r.place,r.state,r.n,r.pins,kPlaceCount);
    if(r.currentEntry>=0)r.currentPlace=r.place[r.currentEntry];
    // The routes: the current mission's successors on the list (worldmap_logic.h RouteTargets, any shown state: see
    // there), by their places.
    const int total=RowCount();
    static int stateOfRow[kMaxEntries];
    for(int i=0;i<kMaxEntries;++i)stateOfRow[i]=4;
    for(int i=0;i<r.n;++i)if(r.row[i]>=0 && r.row[i]<kMaxEntries)stateOfRow[r.row[i]]=r.state[i];
    int next[kMaxTargets],open[kMaxTargets];
    const int nextCount=r.current>=0 ? Successors(r.current,next,kMaxTargets) : 0;
    const int openCount=RouteTargets(r.current,next,nextCount,stateOfRow,total<kMaxEntries ? total : kMaxEntries,open,kMaxTargets,
                                     false);
    for(int i=0;i<openCount;++i) {
        int place=-1;
        for(int e=0;e<r.n;++e)if(r.row[e]==open[i]){place=r.place[e];break;}
        if(place<0 || place==r.currentPlace)continue;
        const int state=stateOfRow[open[i]];
        int k=0;
        while(k<r.targetCount && r.targets[k]!=place)++k;
        if(k==r.targetCount){r.targets[r.targetCount]=place;r.targetState[r.targetCount++]=state;}
        else if(StateRank(state)<StateRank(r.targetState[k]))r.targetState[k]=state;
    }
    NotOpenTitle(r.notOpen,std::size(r.notOpen));
    Log("WORLDMAP select %p: %d entries, %d pins (%d entries without a place), current row %d (entry %d, place %ls), "
        "%d successors, %d shown, %d route places",static_cast<void*>(self),r.n,r.pinCount,unknown,r.current,r.currentEntry,
        r.currentPlace>=0 ? kPlaces[r.currentPlace].id : L"-",nextCount,openCount,r.targetCount);
}
int PinOfPlace(int place) noexcept {
    for(int i=0;i<rows.pinCount;++i)if(rows.pins[i].place==place)return i;
    return -1;
}
int EntryPlace(int entry) noexcept { return entry>=0 && entry<rows.n ? rows.place[entry] : -1; }

// --- What the UI thread hands the draw (under shotLock) ---
constexpr int kShownLines=kPanelLines;   // under 本关出动, over the two key lines
struct PanelLine { wchar_t num[8]; wchar_t title[64]; int state; bool cursor; };
struct Shot {
    bool on=false;
    unsigned char* select=nullptr;
    View view;
    Pixel pointer{-1.f,-1.f};
    bool pointerShown=false;
    // The pins, the chosen place (the cursor's), the current mission's place and its routes' places.
    int pinCount=0;
    Pin pins[kPlaceCount]{};
    // The pins as drawn: near ones one group (worldmap_logic.h GroupNear, at this view); pin i in group groupOf[i].
    int groupCount=0;
    Group groups[kPlaceCount]{};
    int groupOf[kPlaceCount]{};
    int chosen=-1,current=-1;
    int targetCount=0;
    int targets[kMaxTargets]{},targetState[kMaxTargets]{};
    // The panel: the chosen place's missions from `first` (of `total`).
    int lines=0,first=0,total=0;
    PanelLine line[kShownLines]{};
    hudtext::Lang lang=hudtext::Lang::en;
    // Stage 6: the title bar (the 整备 tabs, this the last; the preset chips), 本关出动, the campaign's progress.
    wchar_t tab[kTabCount][24]{};
    int presetCount=0,presetCurrent=-1;
    int launch=-1;            // the preset the chosen mission starts with (-1: as equipped, no presets)
    bool remembered=false;    // ...remembered for it (else the one in force)
    wchar_t launchLabel[24]{},launchName[64]{},launchKeys[48]{};
    wchar_t placeKeys[64]{},zoomKeys[48]{},tabKeys[80]{};   // the panel's key lines (hudtext mapPlaceKeys / ...)
    // A start held for the generator (Launch): what 本关出动's row says instead of the preset; failed: an error.
    wchar_t launchNote[160]{};
    bool launchFailed=false;
    Progress progress;
    wchar_t progressText[64]{};
};
SRWLOCK shotLock=SRWLOCK_INIT;
Shot shot;
// ...and what the draw hands back: the viewport and the target's size (the pointer's mapping, the sharpness).
struct Geometry { float vp[4]{}; float targetW=0.f,targetH=0.f; };
Geometry geometry;
void Publish(const Shot& s) noexcept { AcquireSRWLockExclusive(&shotLock);shot=s;ReleaseSRWLockExclusive(&shotLock); }
void Read(Shot& s) noexcept { AcquireSRWLockShared(&shotLock);s=shot;ReleaseSRWLockShared(&shotLock); }
Geometry ReadGeometry() noexcept { AcquireSRWLockShared(&shotLock);const Geometry g=geometry;ReleaseSRWLockShared(&shotLock);return g; }

// The base map's tiles on the device (render thread, under gpuLock): one immutable B8G8R8A8 texture each (no mip chain:
// worldmap_logic.h LevelFor draws the coarsest level whose pixels are no larger than the screen's, so a tile is shown
// between 1x and 2x minified, where a bilinear sample needs no mips), at most kCacheTiles of them (1350 x 1350 x 4 =
// 7.3 MB each), evicted by worldmap_logic.h Victim. Viewed in the target's colour space at upload (a tile is read as it
// is written): a target of the other space, or another device, drops them all.
constexpr int kCacheTiles=48;
class TileCache {
public:
    // Before the frame's tile calls (after the renderer's Begin).
    void Bind(gpu::Painter& paint) noexcept {
        ID3D11Device* const d=paint.Device();
        if(d!=device || paint.Srgb()!=srgb){tiles.clear();device=d;srgb=paint.Srgb();}
    }
    bool Has(int index) const noexcept {
        for(const Tile& t:tiles)if(t.index==index)return true;
        return false;
    }
    // `tile` as a texture. False: not made (no device, a failed device call, or the cache full of tiles that may not go).
    bool Upload(const TilePixels& tile,int level,unsigned frame) noexcept {
        if(!device || tile.w<=0 || tile.h<=0 ||
           tile.bgra.size()!=static_cast<std::size_t>(tile.w)*static_cast<std::size_t>(tile.h))return false;
        if(Has(tile.index))return true;
        std::size_t slot=tiles.size();
        if(tiles.size()>=static_cast<std::size_t>(kCacheTiles)) {
            int levels[kCacheTiles];
            unsigned used[kCacheTiles];
            const int n=kCacheTiles;
            for(int i=0;i<n;++i){levels[i]=tiles[static_cast<std::size_t>(i)].level;used[i]=tiles[static_cast<std::size_t>(i)].used;}
            const int victim=Victim(levels,used,n,frame);
            if(victim<0)return false;
            slot=static_cast<std::size_t>(victim);
        }
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        view.Attach(gpu::MakeImage(device,tile.bgra.data(),tile.w,tile.h,srgb));
        if(!view)return false;
        Tile t{tile.index,level,frame,std::move(view)};
        try {
            if(slot<tiles.size())tiles[slot]=std::move(t);
            else tiles.push_back(std::move(t));
        } catch(...) { return false; }
        return true;
    }
    // The cached tile's (u0, v0)..(u1, v1) over the rectangle, marked drawn in `frame`.
    void Draw(gpu::Painter& paint,int index,float x0,float y0,float x1,float y1,float u0,float v0,float u1,float v1,
              unsigned frame) noexcept {
        for(Tile& t:tiles)
            if(t.index==index){t.used=frame;paint.Image(t.view.Get(),x0,y0,x1,y1,u0,v0,u1,v1,0xFFFFFFFFu);return;}
    }
    void Reset() noexcept { tiles.clear();device=nullptr; }
private:
    struct Tile { int index; int level; unsigned used; Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view; };
    std::vector<Tile> tiles;
    ID3D11Device* device=nullptr;   // compared only (the renderer holds it)
    bool srgb=false;
};

// --- The draw's state (render thread; the destructor takes gpuLock to release it) ---
std::mutex gpuLock;
gpu::Renderer* renderer=nullptr;            // never freed (no device calls at the DLL's unload)
uitext::Labels* labels=nullptr;       // the same
TileCache* cache=nullptr;              // the same
enum : int { kGpuUnknown, kGpuReady, kGpuBroken };
std::atomic<int> gpuState{kGpuUnknown};
unsigned frame=0;
bool failed[kTileCount]{};          // tiles whose JPEG did not decode: not asked again (logged once each)
bool geometryLogged=false;
Shot drawn;                         // the render thread's copy of this frame's shot

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
    float moveT=0.f,moveSeconds=1.f;
    bool dragging=false,lbWas=false,r3Was=false,rKeyWas=false;
    Pixel dragAt{};
    Pixel pointer{-1.f,-1.f};
    bool pointerShown=false;
    int lastCursor=-1,lastPlace=-1;   // the cursor's entry and place the map last followed
    int focusing=-1,focusFrames=0;    // an entry asked of the native focus, frames waited for +0x778 to follow
    bool focusLogged=false;
    int confirm=-1;                   // the entry a second click on its line confirms after the stock OnUpdate (-1: none)
    int panelFirst=0,panelLines=0;    // the panel's lines as last published (the mouse's hit test)
    int panelEntry[kShownLines]{};
    // Stage 6
    bool rbWas=false,digitWas[8]{},qWas=false,eWas=false;
    int backTab=-1;           // a 整备 tab clicked: the stock 返回 given to this frame's stock OnUpdate, 整备 opened there
    crew::armory::Presets ps; // the preset book's chips (armory_presets.h), read when the select opened
    // A start held for the generator (Launch): the entry confirmed, the last poll; the entry whose start was refused
    // for a failed generation (confirmed again: started with the files as they are); the line 本关出动 shows.
    int pending=-1,failedEntry=-1;
    ULONGLONG polled=0;
    wchar_t note[160]{};
    bool noteFailed=false;
};
Ui ui;
bool backOk=false,launchOk=false;   // kBackSig / the confirm callback's slot checked at load
thread_local bool mapUpdatingStock=false;
using ListMouseFn=bool(__fastcall*)(void*);

// Behavior_Vertical tries the mouse first, then uses the navigation repeat mask when it returns false.
bool __fastcall VerticalListMouse(void* list) noexcept {
    return !mapUpdatingStock && reinterpret_cast<ListMouseFn>(image+kVerticalMouse)(list);
}
std::atomic<unsigned> selectGeneration{0};   // bumped by each select's destructor (GoneHook)
std::atomic<unsigned char*> gone{nullptr};   // the select whose destructor ran (set there, taken by the next frame)
constexpr float kOpeningSeconds=1.0f,kFollowSeconds=0.5f;

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

MapPoint PlacePoint(int place) noexcept { return Project(kPlaces[place].lat,kPlaces[place].lon); }
void MoveTo(const View& to,float seconds) noexcept {
    ui.from=ui.view;ui.to=to;ui.moving=true;ui.moveT=0.f;ui.moveSeconds=seconds;
}

// The rows read; the opening move: the whole Earth, then the current mission and where its open successors are (or
// every pin when there is no current mission on the list).
void Start(unsigned char* self,const Screen& s) noexcept {
    LoadRows(self);
    MapPoint p[kPlaceCount];
    int n=0;
    if(rows.currentPlace>=0) {
        p[n++]=PlacePoint(rows.currentPlace);
        for(int i=0;i<rows.targetCount && n<kPlaceCount;++i)p[n++]=PlacePoint(rows.targets[i]);
    } else {
        for(int i=0;i<rows.pinCount && n<kPlaceCount;++i)p[n++]=PlacePoint(rows.pins[i].place);
    }
    const int cursor=CursorIndex(self);
    ui.lastCursor=cursor;
    ui.lastPlace=EntryPlace(cursor);
    if(n==0) {
        ui.view=Whole(0.5f,s);
        ui.moving=false;
        Log("WORLDMAP select %p: no row has a place: the whole Earth",static_cast<void*>(self));
    } else {
        const View end=FrameIn(p,n,s);
        ui.view=Whole(p[0].u,s);
        MoveTo(end,kOpeningSeconds);
        Log("WORLDMAP select %p: opening on %d places (%ls first), zoom %.2f",static_cast<void*>(self),n,
            rows.currentPlace>=0 ? kPlaces[rows.currentPlace].id : L"every pin",static_cast<double>(end.zoom));
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

// The pins on screen (the copy nearest the view's centre).
void PinPixels(const View& v,const Screen& s,Pixel* at) noexcept {
    for(int i=0;i<rows.pinCount;++i)at[i]=ToScreen(v,s,PlacePoint(rows.pins[i].place));
}
// The pins as drawn at view v: near ones grouped (at the nearest view: set apart instead).
struct Layout { int count=0; Group groups[kPlaceCount]; int groupOf[kPlaceCount]; };
void LayOut(const View& v,const Screen& s,Layout& l) noexcept {
    Pixel at[kPlaceCount];
    PinPixels(v,s,at);
    l.count=GroupNear(at,rows.pins,rows.pinCount,kGroupDistance,Nearest(v,s),l.groupOf,l.groups);
}
int GroupOfPlace(const Layout& l,int place) noexcept {
    for(int i=0;i<rows.pinCount;++i)if(rows.pins[i].place==place)return l.groupOf[i];
    return -1;
}
// The cursor to entry i: the native focus asked (it moves in the stock OnUpdate after this hook).
void Choose(unsigned char* self,int entry,const char* why) noexcept {
    if(entry<0 || entry>=rows.n)return;
    Focus(self,entry);
    ui.focusing=entry;
    ui.focusFrames=0;
    const wchar_t* const key=RowKey(rows.row[entry]);
    Log("WORLDMAP select %p: %s: entry %d (row %d, key %ls, %ls)",static_cast<void*>(self),why,entry,rows.row[entry],
        key ? key : L"-",rows.place[entry]>=0 ? kPlaces[rows.place[entry]].id : L"-");
}
// The entry to choose at pin k: its first enterable, else its first (a locked place's panel shown).
int EntryOfPin(int k) noexcept {
    const int e=FirstEnterable(rows.place,rows.state,rows.n,rows.pins[k].place);
    return e>=0 ? e : rows.pins[k].first;
}
// Group k of layout l chosen: one place, its entry; several, the map opens it up (SplitView) and the cursor goes to
// the first of its places in list order that has a mission to enter (else its first place's first entry).
void ChooseGroup(unsigned char* self,const Layout& l,int k,const Screen& s,const char* why,bool expand=true) noexcept {
    MapPoint members[kPlaceCount];
    int n=0,best=-1,bestEntry=-1;
    for(int i=0;i<rows.pinCount;++i) {
        if(l.groupOf[i]!=k)continue;
        members[n++]=PlacePoint(rows.pins[i].place);
        const int e=FirstEnterable(rows.place,rows.state,rows.n,rows.pins[i].place);
        if(e>=0 && (bestEntry<0 || e<bestEntry)){bestEntry=e;best=i;}
        if(best<0 && bestEntry<0)best=i;
    }
    if(n==0)return;
    if(n>1 && expand) {
        MoveTo(SplitView(ui.moving ? ui.to : ui.view,members,n,s),kFollowSeconds);
        Log("WORLDMAP select %p: %s: a group of %d places opened",static_cast<void*>(self),why,n);
    }
    Choose(self,bestEntry>=0 ? bestEntry : EntryOfPin(best),why);
}

// --- Stage 6: 本关出动 (the preset a mission starts with, loadout_preset.h [Missions]) ---
// The preset remembered for display entry `entry`, -1 none.
int RememberedOf(int entry) noexcept {
    if(entry<0 || entry>=rows.n)return -1;
    const wchar_t* const key=RowKey(rows.row[entry]);
    return key ? armorypresets::MissionPreset(key) : -1;
}
// Entry `entry` to start with preset `index` (the book written).
void Remember(int entry,int index,const char* how) noexcept {
    if(entry<0 || entry>=rows.n || index<0 || index>=ui.ps.count)return;
    const wchar_t* const key=RowKey(rows.row[entry]);
    if(!key)return;
    wchar_t why[160]{};
    if(!armorypresets::RememberMission(key,index,why,std::size(why)))
        Log("WORLDMAP 本关出动 of %ls not remembered (%s): %ls",key,how,why);
}
// The cursor's mission is being started (the stock confirm callback, or our click's confirm): its remembered preset
// switched in when it is not the one in force (armory_presets.h Apply: the ini's support keys, the player's weapons).
void StartWithPreset(unsigned char* self,const char* how) noexcept {
    const int cursor=CursorIndex(self);
    if(cursor<0 || cursor>=rows.n || !Enterable(rows.state[cursor]) || !Confirmable(self))return;
    const int want=RememberedOf(cursor);
    armorypresets::Fill(&ui.ps);
    if(want<0 || want>=ui.ps.count || want==ui.ps.current)return;
    crew::armory::Effects fx;
    fx.op=crew::armory::Op::switchTo;
    fx.index=want;
    wchar_t why[160]{};
    const bool ok=armorypresets::Apply(fx,why,std::size(why));
    armorypresets::Fill(&ui.ps);
    if(ok)roomsync::RoomSyncNudge();   // in a room the start gate reads the switched-in set's files at once
    if(ok)Log("WORLDMAP %s: row %d starts with preset %d (switched in)",how,rows.row[cursor],want+1);
    else Log("WORLDMAP %s: row %d: preset %d not switched in: %ls",how,rows.row[cursor],want+1,why);
}
// The cursor's mission confirmed (the stock confirm callback, our click's confirm): its preset switched in, then the
// generator asked: true when the start goes on now. While it runs the start is held (ui.pending: AfterStock starts it
// when the generator is done; the cursor moved, it is dropped); failed / no generator: not started, the error on
// 本关出动's row, and the same mission confirmed again starts with the files as they are (the player's choice, logged).
constexpr ULONGLONG kGenPollMs=500;
bool Launch(unsigned char* self,const char* how) noexcept {
    StartWithPreset(self,how);
    const int cursor=CursorIndex(self);
    if(cursor<0 || cursor>=rows.n || !Enterable(rows.state[cursor]) || !Confirmable(self))return true;   // the stock's own
    const GenerateStatus g=VariantGenerationStatus();
    ui.pending=-1;
    if(g.state==GenerateState::running) {
        ui.pending=cursor;ui.polled=GetTickCount64();
        _snwprintf_s(ui.note,_TRUNCATE,hudtext::Tr(hudtext::Tx::mapLaunchWaiting),g.message);
        ui.noteFailed=false;
        Log("WORLDMAP %s: row %d held: the generator is running (%ls)",how,rows.row[cursor],g.message);
        return false;
    }
    if(g.state==GenerateState::failed || g.state==GenerateState::missing) {
        if(ui.failedEntry==cursor) {
            Log("WORLDMAP %s: row %d confirmed again after a failed generation (%ls): started with the files as they are",
                how,rows.row[cursor],g.message);
            ui.failedEntry=-1;ui.note[0]=0;
            return true;
        }
        ui.failedEntry=cursor;
        _snwprintf_s(ui.note,_TRUNCATE,hudtext::Tr(hudtext::Tx::mapLaunchFailed),g.message);
        ui.noteFailed=true;
        Log("WORLDMAP %s: row %d not started: the generation %s (%ls)",how,rows.row[cursor],
            g.state==GenerateState::failed ? "failed" : "has no generator",g.message);
        return false;
    }
    ui.failedEntry=-1;ui.note[0]=0;
    return true;
}
// A held start (Launch) each frame after the stock OnUpdate: dropped when the cursor left it or the select cannot
// confirm; the generator polled; done, the start made (the direct confirm, as a click's).
void FollowPending(unsigned char* self) noexcept {
    if(ui.pending<0)return;
    const int cursor=CursorIndex(self);
    if(cursor!=ui.pending || !Confirmable(self)) {
        Log("WORLDMAP select %p: the held start of entry %d dropped (the cursor on %d)",static_cast<void*>(self),ui.pending,cursor);
        ui.pending=-1;ui.note[0]=0;
        return;
    }
    const ULONGLONG now=GetTickCount64();
    if(now-ui.polled<kGenPollMs)return;
    ui.polled=now;
    const GenerateStatus g=VariantGenerationStatus();
    if(g.state==GenerateState::running) {
        _snwprintf_s(ui.note,_TRUNCATE,hudtext::Tr(hudtext::Tx::mapLaunchWaiting),g.message);
        return;
    }
    ui.pending=-1;
    if(g.state==GenerateState::failed || g.state==GenerateState::missing) {
        ui.failedEntry=cursor;
        _snwprintf_s(ui.note,_TRUNCATE,hudtext::Tr(hudtext::Tx::mapLaunchFailed),g.message);
        ui.noteFailed=true;
        Log("WORLDMAP select %p: entry %d not started: the generation %s (%ls)",static_cast<void*>(self),cursor,
            g.state==GenerateState::failed ? "failed" : "has no generator",g.message);
        return;
    }
    ui.note[0]=0;
    Log("WORLDMAP select %p: the generator is done (%ls): entry %d started",static_cast<void*>(self),g.message,cursor);
    if(launchOk)roomsync::RoomSyncNudge();
    Confirm(self);
}
// A 整备 tab asked for (`why`: clicked, a key): the select closed the stock way, 整备 opened at it.
void GoTab(unsigned char* self,int tab,const char* why) noexcept {
    if(!backOk){Log("WORLDMAP tab %d (%s): the 返回 key's code did not check out at load: not left",tab,why);return;}
    if(!crew::armoryui::Installed()){Log("WORLDMAP tab %d (%s): 整备 is not installed: not left",tab,why);return;}
    ui.backTab=tab;
    Log("WORLDMAP select %p: tab %d (%s): back to the screen under it, 整备 there",static_cast<void*>(self),tab,why);
}
// The title bar clicked: a 整备 tab (GoTab), a chip (that preset in force).
void TitleClick(unsigned char* self) noexcept {
    const int tab=TitleTabAt(ui.pointer);
    if(tab>=0 && tab!=kMapTab){GoTab(self,tab,"clicked");return;}
    const int chip=TitleChipAt(ui.ps.count,ui.pointer);
    if(chip<0 || chip==ui.ps.current)return;
    crew::armory::Effects fx;
    fx.op=crew::armory::Op::switchTo;
    fx.index=chip;
    wchar_t why[160]{};
    if(!armorypresets::Apply(fx,why,std::size(why)))Log("WORLDMAP preset %d not switched in: %ls",chip+1,why);
    armorypresets::Fill(&ui.ps);
}

// This frame's map input; true when the stock OnUpdate must not get it (a click, a drag, the wheel, a stick, a zoom).
bool TakeInput(unsigned char* self,const unsigned char* ctx,const Screen& s,float dt,bool input) noexcept {
    const bool front=InFront() && !MapHoldsKeys();   // (the mission's map view is never open over the select; the census)
    const Pixel previous=ui.pointer;
    ReadPointer(s);
    const bool overTitle=ui.pointerShown && OverTitle(ui.pointer);
    const bool overMap=ui.pointerShown && !overTitle && !OverPanels(ui.pointer) && !OverPanel(ui.pointer);
    const bool overPanel=ui.pointerShown && OverPanel(ui.pointer);
    const bool overLaunch=ui.pointerShown && In(kLaunchRow,ui.pointer);
    const bool rb=front && Down(VK_RBUTTON);
    wheelToMapUntil.store(input && front && overMap ? GetTickCount64()+kWheelHoldMs : 0,std::memory_order_relaxed);
    const int notches=wheel.exchange(0);
    XINPUT_STATE pad{};
    const bool havePad=(front || edf::PadSource()) && PadState(&pad);
    const bool r3=havePad && (pad.Gamepad.wButtons&XINPUT_GAMEPAD_RIGHT_THUMB)!=0;
    const bool rKey=front && Down('R');
    const bool lb=front && Down(VK_LBUTTON);
    bool digit[8];
    for(int i=0;i<8;++i)digit[i]=front && Down('1'+i);
    const bool qKey=front && Down('Q'),eKey=front && Down('E');
    if(!input) {
        ui.dragging=false;
        ui.lbWas=lb;ui.r3Was=r3;ui.rKeyWas=rKey;ui.rbWas=rb;ui.qWas=qKey;ui.eWas=eKey;
        for(int i=0;i<8;++i)ui.digitWas[i]=digit[i];
        return false;
    }
    bool used=false;
    const int cursor=CursorIndex(self);
    // 本关出动: 1..8 pick the cursor mission's preset; a click on its row steps it (left next, right previous).
    for(int i=0;i<8;++i) {
        if(digit[i] && !ui.digitWas[i] && i<ui.ps.count)Remember(cursor,i,"a number key");
        ui.digitWas[i]=digit[i];
    }
    if(overLaunch && ((lb && !ui.lbWas) || (rb && !ui.rbWas))) {
        const int launchNow=LaunchPreset(RememberedOf(cursor),ui.ps.current,ui.ps.count);
        Remember(cursor,StepPreset(launchNow,lb && !ui.lbWas ? 1 : -1,ui.ps.count),"its row clicked");
        ui.lbWas=lb;
        used=true;
    }
    // (the stock screen's right click is its 返回: not over our rows)
    if((overTitle || overLaunch) && rb)used=true;
    ui.rbWas=rb;
    const bool mapClick=lb && !ui.lbWas && overMap;
    const bool moved=ui.pointer.x!=previous.x || ui.pointer.y!=previous.y;
    bool overPin=false;
    if(overMap && (mapClick || (!lb && moved))) {
        Layout l;
        LayOut(ui.view,s,l);
        Pixel at[kPlaceCount];
        for(int k=0;k<l.count;++k)at[k]=l.groups[k].at;
        const int pin=Pick(at,l.count,ui.pointer,kPinRadius);
        overPin=pin>=0;
        if(overPin && (mapClick || GroupOfPlace(l,EntryPlace(cursor))!=pin)) {
            ChooseGroup(self,l,pin,s,mapClick ? "a pin clicked" : "the pointer entered a pin",mapClick);
            used=true;
        }
    }
    // A click: a panel line (the cursor's line again: confirm), a pin (its place chosen), else the map dragged.
    if(lb && !ui.lbWas) {
        if(overTitle) {
            TitleClick(self);
            used=true;
        } else if(overPanel) {
            const int line=PanelLineAt(ui.pointer,ui.panelLines);
            if(line>=0) {
                const int entry=ui.panelEntry[line];
                if(entry==cursor)ui.confirm=Confirmable(self) ? entry : -1;
                else Choose(self,entry,"a panel line clicked");
            }
            used=true;
        } else if(overMap) {
            if(!overPin){ui.dragging=true;ui.dragAt=ui.pointer;}
            used=true;
        }
    } else if(lb && ui.dragging) {
        if(ui.pointerShown){Pan(ui.view,s,ui.pointer.x-ui.dragAt.x,ui.pointer.y-ui.dragAt.y);ui.dragAt=ui.pointer;}
        used=true;
    }
    if(!lb)ui.dragging=false;
    ui.lbWas=lb;
    if(notches && overMap){ZoomAt(ui.view,s,ui.pointer,WheelZoom(notches));used=true;}
    // Left / right (the stock select reads neither: its list is vertical): the nearest place that way with a mission
    // that may be chosen. The stock OnUpdate still gets the input (up / down / confirm in the same frame are its).
    const unsigned repeat=RepeatMask(ctx);
    const int dir=(repeat&kLeftBit) ? -1 : (repeat&kRightBit) ? 1 : 0;
    if(dir!=0 && rows.pinCount>0 && ui.confirm<0) {   // (a confirm this frame: no other choice)
        // Over the pins as drawn: a group is one stop (chosen, it opens up).
        Layout l;
        LayOut(ui.moving ? ui.to : ui.view,s,l);
        Pixel at[kPlaceCount+1];
        bool ok[kPlaceCount+1];
        for(int k=0;k<l.count;++k){at[k]=l.groups[k].at;ok[k]=Enterable(l.groups[k].state);}
        int from=GroupOfPlace(l,EntryPlace(cursor));
        if(from<0){at[l.count]=kMapFocus;ok[l.count]=false;from=l.count;}   // none chosen: from the map's focus
        const int k=Neighbour(at,ok,from==l.count ? l.count+1 : l.count,from,dir);
        if(k>=0)ChooseGroup(self,l,k,s,dir<0 ? "left" : "right");
    }
    // R3 / R: back to the current mission.
    if(((r3 && !ui.r3Was) || (rKey && !ui.rKeyWas)) && rows.currentEntry>=0) {
        Choose(self,rows.currentEntry,"back to the current mission");
        if(rows.currentPlace>=0)MoveTo(CentreOn(ui.view,PlacePoint(rows.currentPlace),s),kFollowSeconds);
    }
    ui.r3Was=r3;ui.rKeyWas=rKey;
    if(havePad) {
        const float rx=Stick(pad.Gamepad.sThumbRX),ry=Stick(pad.Gamepad.sThumbRY);
        if(rx!=0.f || ry!=0.f){const Pixel d=StickPan(rx,ry,dt);Pan(ui.view,s,d.x,d.y);used=true;ui.moving=false;}
        // LB out, LT in (RB / RT scroll the briefing: the stock's).
        const bool out=(pad.Gamepad.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0;
        const bool in=pad.Gamepad.bLeftTrigger>XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        if(out || in) {
            const float f=HoldZoom(out,in,dt);
            if(f!=1.f)ZoomAt(ui.view,s,kMapFocus,f);
            used=true;ui.moving=false;
        }
    }
    if(front) {
        const bool out=Down('Z'),in=Down('C');
        if(out || in){const float f=HoldZoom(out,in,dt);if(f!=1.f)ZoomAt(ui.view,s,kMapFocus,f);ui.moving=false;}
    }
    // Q / E: the 整备 tabs, as in 整备 (Q the previous, E the next; the map is the last: E stays). The pad has none here
    // (LB / LT zoom, RB / RT scroll the briefing: the stock's).
    const int tabDir=(qKey && !ui.qWas ? -1 : 0)+(eKey && !ui.eWas ? 1 : 0);
    ui.qWas=qKey;ui.eWas=eKey;
    if(tabDir!=0) {
        const int tab=StepMapTab(kMapTab,tabDir);
        if(tab!=kMapTab){GoTab(self,tab,tabDir<0 ? "the Q key" : "the E key");used=true;}
    }
    if(used && (ui.dragging || notches))ui.moving=false;
    return used;
}

// The cursor moved to another place (up / down, our choice): the map follows when that place is not in its open area.
void FollowCursor(unsigned char* self,const Screen& s) noexcept {
    const int cursor=CursorIndex(self);
    if(cursor==ui.lastCursor)return;
    ui.lastCursor=cursor;
    const int place=EntryPlace(cursor);
    if(place<0 || place==ui.lastPlace)return;
    ui.lastPlace=place;
    const MapPoint p=PlacePoint(place);
    const View end=ui.moving ? ui.to : ui.view;
    if(!In(kOpenArea,ToScreen(end,s,p)))MoveTo(CentreOn(end,p,s),kFollowSeconds);
}

void DescribeLine(PanelLine& l,int entry,int cursor,int titleCount) noexcept {
    MissionNumber(rows.row[entry],rows.field5[entry],l.num,std::size(l.num));
    if(rows.state[entry]==3)wcsncpy_s(l.title,rows.notOpen,_TRUNCATE);
    else if(rows.row[entry]<titleCount)Title(rows.row[entry],l.title,std::size(l.title));
    else l.title[0]=0;
    wchar_t* w=l.title;
    for(const wchar_t* c=l.title;*c;++c)if(*c!=static_cast<wchar_t>(0x2020))*w++=*c;
    *w=0;
    l.state=rows.state[entry];
    l.cursor=entry==cursor;
}

// The shot's pins and both lists from the rows and the cursor.
void Describe(Shot& out,unsigned char* self,const Screen& s) noexcept {
    out.pinCount=rows.pinCount;
    for(int i=0;i<rows.pinCount;++i)out.pins[i]=rows.pins[i];
    static Layout lay;   // UI thread only
    LayOut(out.view,s,lay);
    out.groupCount=lay.count;
    for(int k=0;k<lay.count;++k)out.groups[k]=lay.groups[k];
    for(int i=0;i<rows.pinCount;++i)out.groupOf[i]=lay.groupOf[i];
    const int cursor=CursorIndex(self);
    out.chosen=EntryPlace(cursor);
    out.current=rows.currentPlace;
    out.targetCount=rows.targetCount;
    for(int i=0;i<rows.targetCount;++i){out.targets[i]=rows.targets[i];out.targetState[i]=rows.targetState[i];}
    out.lang=hudtext::InUse();
    for(int i=0;i<kTabCount;++i)wcsncpy_s(out.tab[i],crew::armory::tabs::Name(i),_TRUNCATE);   // 整备's own tab table
    out.presetCount=ui.ps.count;out.presetCurrent=ui.ps.current;
    const int remembered=RememberedOf(cursor);
    out.launch=LaunchPreset(remembered,ui.ps.current,ui.ps.count);
    out.remembered=out.launch>=0 && out.launch==remembered;
    wcsncpy_s(out.launchLabel,hudtext::Tr(hudtext::Tx::mapLaunch),_TRUNCATE);
    if(out.launch>=0)_snwprintf_s(out.launchName,_TRUNCATE,L"%d  %ls",out.launch+1,ui.ps.names[out.launch]);
    else wcsncpy_s(out.launchName,hudtext::Tr(hudtext::Tx::mapLaunchNone),_TRUNCATE);
    wcsncpy_s(out.launchKeys,hudtext::Tr(hudtext::Tx::mapLaunchKeys),_TRUNCATE);
    wcsncpy_s(out.placeKeys,hudtext::Tr(hudtext::Tx::mapPlaceKeys),_TRUNCATE);
    wcsncpy_s(out.zoomKeys,hudtext::Tr(hudtext::Tx::mapZoomKeys),_TRUNCATE);
    wcsncpy_s(out.tabKeys,hudtext::Tr(hudtext::Tx::mapTabKeys),_TRUNCATE);
    wcsncpy_s(out.launchNote,ui.note,_TRUNCATE);
    out.launchFailed=ui.noteFailed;
    out.progress=CountProgress(rows.state,rows.n);
    _snwprintf_s(out.progressText,_TRUNCATE,hudtext::Tr(hudtext::Tx::mapProgress),out.progress.cleared,out.progress.shown);
    const int total=RowCount();
    ui.panelLines=0;
    if(out.chosen<0)return;
    int at[kMaxEntries];
    const int count=EntriesAt(rows.place,rows.n,out.chosen,at,kMaxEntries);
    int line=-1;
    for(int i=0;i<count;++i)if(at[i]==cursor)line=i;
    const int first=ScrollFirst(count,line,kShownLines);
    out.first=first;out.total=count;
    for(int i=first;i<count && out.lines<kShownLines;++i) {
        const int e=at[i];
        PanelLine& l=out.line[out.lines];
        DescribeLine(l,e,cursor,total);
        ui.panelEntry[out.lines]=e;
        ++out.lines;
    }
    ui.panelFirst=first;
    ui.panelLines=out.lines;
}

// A new select object (or the same address after its destructor ran): everything of the last one dropped; the current
// mission read before the stock OnUpdate's first cursor refresh rewrites it.
void Follow(unsigned char* self) noexcept {
    unsigned char* const was=gone.exchange(nullptr);
    if(self==ui.self && was!=self)return;
    ui=Ui{};
    ui.self=self;
    rows.n=0;
    rows.pinCount=0;
    rows.current=CurrentRow();
    wchar_t why[160]{};
    if(!armorypresets::Load(why,std::size(why)))
        Log("WORLDMAP select %p: the preset book did not read (%ls): 本关出动 shows the loadout in force",static_cast<void*>(self),why);
    armorypresets::Fill(&ui.ps);
}

// The ctx's ui::Input copied into `out` with 返回 pressed (the merged mask and both pads' records): false when it is not
// readable (the click then does nothing).
bool CopyWithBack(const unsigned char* ctx,unsigned char* out) noexcept {
    __try {
        const unsigned char* const in=*reinterpret_cast<unsigned char* const*>(ctx+kCtxInput);
        if(!in)return false;
        std::memcpy(out,in,kInputCopy);
        out[kInputPressed]|=kBackBit;
        for(int slot=0;slot<2;++slot) {
            const int pad=*reinterpret_cast<const int*>(out+kInputPads+4*slot);
            if(pad>=0 && static_cast<std::size_t>(pad)*kPadRecord+kPadPressed<kInputPads)out[pad*kPadRecord+kPadPressed]|=kBackBit;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){ return false; }
}

float ReadDt(const unsigned char* ctx) noexcept {
    float dt=0.f;
    __try { dt=*reinterpret_cast<const float*>(ctx+kCtxDt); } __except(EXCEPTION_EXECUTE_HANDLER){ dt=0.f; }
    return dt>0.f && dt<0.25f ? dt : 1.f/60.f;
}
bool OwnsInput(const unsigned char* ctx) noexcept {
    __try { return *reinterpret_cast<void* const*>(ctx+kCtxInput)!=nullptr; } __except(EXCEPTION_EXECUTE_HANDLER){ return false; }
}

// After the stock OnUpdate: the asked focus checked (+0x778 follows it there), a confirm made.
void AfterStock(unsigned char* self) noexcept {
    if(ui.focusing>=0) {
        if(CursorIndex(self)==ui.focusing)ui.focusing=-1;
        else if(++ui.focusFrames>=3) {
            if(!ui.focusLogged)Log("WORLDMAP select %p: the native focus did not reach entry %d (cursor %d)",
                                   static_cast<void*>(self),ui.focusing,CursorIndex(self));
            ui.focusLogged=true;
            ui.focusing=-1;
        }
    }
    if(ui.confirm>=0) {
        const int want=ui.confirm;
        ui.confirm=-1;
        const int cursor=CursorIndex(self);
        if(cursor!=want) {
            // The stock OnUpdate moved the cursor off the clicked line: the line clicked is not what would start.
            Log("WORLDMAP select %p: a click confirmed entry %d but the cursor is on %d: not confirmed",
                static_cast<void*>(self),want,cursor);
        } else {
            Log("WORLDMAP select %p: confirm entry %d (row %d) by a click",static_cast<void*>(self),cursor,
                cursor>=0 && cursor<rows.n ? rows.row[cursor] : -1);
            // (the direct call skips the callback's hook)
            if(!launchOk || Launch(self,"a click's confirm"))Confirm(self);
        }
    }
    FollowPending(self);
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
        FollowCursor(self,s);
        used=TakeInput(self,ctx,s,dt,input);
        if(ui.moving) {
            ui.moveT+=dt/ui.moveSeconds;
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
    static Shot on;   // UI thread only (4 KB: not on the game's stack)
    on=Shot{};
    on.on=true;on.select=self;on.view=ui.view;on.pointer=ui.pointer;on.pointerShown=ui.pointerShown;
    if(ui.started)Describe(on,self,s);
    Publish(on);
    std::uint64_t result;
    const bool wasUpdatingStock=mapUpdatingStock;
    mapUpdatingStock=true;
    const int backTab=ui.backTab;
    ui.backTab=-1;
    alignas(16) static unsigned char backInput[kInputCopy];   // UI thread only
    if(backTab>=0 && input && CopyWithBack(ctx,backInput)) {
        // A 整备 tab clicked: this frame's stock OnUpdate reads 返回 pressed (its layout's cancel_action: set_exit_code
        // 255, the select closed, the HQ / room shown again), and that screen opens 整备 at the tab (room_ui.cpp).
        alignas(8) unsigned char copy[kCtxSize];
        std::memcpy(copy,ctx,kCtxSize);
        unsigned char* const in=backInput;
        std::memcpy(copy+kCtxInput,&in,sizeof(in));
        crew::armoryui::AskOpen(backTab);
        result=nextUpdate(self,copy);
    } else if(used) {
        // The stock OnUpdate as when it does not own the input (§5.2): no scroll, no button; its cursor refresh runs.
        alignas(8) unsigned char copy[kCtxSize];
        std::memcpy(copy,ctx,kCtxSize);
        std::memset(copy+kCtxInput,0,sizeof(void*));
        result=nextUpdate(self,copy);
    } else {
        result=nextUpdate(self,ctx);
    }
    mapUpdatingStock=wasUpdatingStock;
    if(ui.started)AfterStock(self);
    return result;
}

// --- The draw ---
bool ReadDrawCtx(const unsigned char* ctx,ID3D11DeviceContext** device,int* vp) noexcept {
    __try {
        *device=*reinterpret_cast<ID3D11DeviceContext* const*>(ctx+kDrawDevice);
        std::memcpy(vp,ctx+kDrawViewport,4*sizeof(int));
        return *device!=nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER){ return false; }
}

// The base map's tiles of view v (the coarser ancestor where a tile is not there); their indices, centre first, in xy.
constexpr int kMostVisible=64;
int DrawTiles(const View& v,const Screen& screen,int* xy) noexcept {
    const int level=LevelFor(v,screen);
    const TileSpan span=Visible(v,screen,level);
    const int n=CentreFirst(v,screen,span,xy,kMostVisible);
    auto has=[](int i) { return i>=0 && cache->Has(i); };
    for(int i=0;i<n;++i) {
        const int x=xy[2*i],y=xy[2*i+1];
        int ax=0,ay=0;
        MapRect uv{};
        const int z=Cover(level,WrapColumn(level,x),y,has,&ax,&ay,&uv);
        if(z<0)continue;
        const MapRect r=TileRect(level,x,y);
        const Pixel p0=ToScreenUnwrapped(v,screen,r.u0,r.v0),p1=ToScreenUnwrapped(v,screen,r.u1,r.v1);
        cache->Draw(*renderer,TileIndex(z,ax,ay),p0.x,p0.y,p1.x,p1.y,uv.u0,uv.v0,uv.u1,uv.v1,frame);
    }
    return n;
}

float Seconds() noexcept { return static_cast<float>(GetTickCount64()%1000000ull)/1000.f; }

// A label at (x, y) (its top left), shadowed; at most `maxW` wide (0: any), fitted as text_fit.h's shrink says (smaller,
// then cut with an ellipsis; ui_text.h Labels::Draw); its width as drawn.
float Text(const wchar_t* text,float x,float y,float size,bool bold,std::uint32_t argb,float maxW=0.f) noexcept {
    return labels->Draw(*renderer,text,x,y,size,argb,uitext::Align::left,bold,drawn.lang,frame,maxW,true).w;
}
void Sprite(ID3D11ShaderResourceView* view,Pixel c,float r,std::uint32_t argb) noexcept {
    if(view)renderer->Image(view,c.x-r,c.y-r,c.x+r,c.y+r,0.f,0.f,1.f,1.f,argb);
}
bool Near(Pixel p,const Screen& s,float margin) noexcept {
    return p.x>-margin && p.y>-margin && p.x<s.w+margin && p.y<s.h+margin;
}

// A route's colour by its end's state: open bright, cleared pale, locked dim.
std::uint32_t RouteColour(int state) noexcept {
    switch(state) { case 1: return 0xFFFFE680u; case 2: return 0xFFFFA040u; case 0: return 0xE0D8DCE0u; default: return 0xC0C04848u; }
}

// The group drawing place p (-1: none).
int DrawnGroup(int place) noexcept {
    for(int i=0;i<drawn.pinCount;++i)if(drawn.pins[i].place==place)return drawn.groupOf[i];
    return -1;
}

// The routes (dashed great circles from the current mission's place, under the pins and clear of them; one shorter
// than two pin diameters not drawn, its end's pin ringed instead), the current mission's ring, the pins.
void DrawMarks(const View& v,const Screen& s) noexcept {
    const Shot& d=drawn;
    const float now=Seconds();
    const float clear=kPinRadius+6.f;
    int marked[kMaxTargets];
    int markedCount=0;
    const int fromGroup=d.current>=0 ? DrawnGroup(d.current) : -1;
    if(d.current>=0 && fromGroup>=0) {
        const Place& from=kPlaces[d.current];
        const Pixel a0=d.groups[fromGroup].at;
        for(int t=0;t<d.targetCount;++t) {
            const int toGroup=DrawnGroup(d.targets[t]);
            if(toGroup<0)continue;
            const Pixel b0=d.groups[toGroup].at;
            float cut=0.f;
            if(toGroup==fromGroup || !RouteClear(a0,b0,clear,&cut)) {
                if(markedCount<kMaxTargets)marked[markedCount++]=t;
                continue;
            }
            const Place& to=kPlaces[d.targets[t]];
            LatLon line[48];
            const int n=GreatCircle(LatLon{from.lat,from.lon},LatLon{to.lat,to.lon},line,48);
            MapPoint m[48];
            for(int i=0;i<n;++i)m[i]=Project(line[i].lat,line[i].lon);
            Unwrap(m,n);
            for(int copy=-1;copy<=1;++copy) {
                Pixel p[48];
                bool any=false;
                for(int i=0;i<n;++i){p[i]=ToScreenUnwrapped(v,s,m[i].u+static_cast<float>(copy),m[i].v);any=any || Near(p[i],s,0.f);}
                if(!any)continue;
                // Dashes 16 on, 10 off, marching away from the current mission.
                for(int pass=0;pass<2;++pass) {
                    float along=-std::fmod(now*40.f,26.f);
                    for(int i=1;i<n;++i) {
                        const float dx=p[i].x-p[i-1].x,dy=p[i].y-p[i-1].y,len=std::sqrt(dx*dx+dy*dy);
                        if(!(len>0.f))continue;
                        float a=0.f;
                        while(a<len) {
                            const float phase=std::fmod(along+a+2600.f,26.f);
                            const float step=phase<16.f ? 16.f-phase : 26.f-phase;
                            const float b=a+step<len ? a+step : len;
                            const float mx=p[i-1].x+dx*(a+b)*0.5f/len,my=p[i-1].y+dy*(a+b)*0.5f/len;
                            const bool nearEnd=copy==0 && ((mx-a0.x)*(mx-a0.x)+(my-a0.y)*(my-a0.y)<clear*clear ||
                                                           (mx-b0.x)*(mx-b0.x)+(my-b0.y)*(my-b0.y)<clear*clear);
                            if(phase<16.f && !nearEnd)
                                renderer->Line(p[i-1].x+dx*a/len,p[i-1].y+dy*a/len,p[i-1].x+dx*b/len,p[i-1].y+dy*b/len,
                                               pass==0 ? 7.f : 3.5f,pass==0 ? 0x90000000u : RouteColour(d.targetState[t]));
                            a=b;
                        }
                        along+=len;
                    }
                }
            }
        }
        // The current mission's place: a steady ring and one pulsing out from it.
        const Pixel c=d.groups[fromGroup].at;
        if(Near(c,s,80.f)) {
            const float t=Pulse(now);
            const std::uint32_t a=static_cast<std::uint32_t>((1.f-t)*230.f);
            Sprite(labels->Ring(*renderer),c,20.f+30.f*t,(a<<24)|0x00FFE680u);
            Sprite(labels->Ring(*renderer),c,21.f,0xF0FFE680u);
        }
    }
    ID3D11ShaderResourceView* const disc=labels->Disc(*renderer);
    ID3D11ShaderResourceView* const ring=labels->Ring(*renderer);
    const int chosenGroup=d.chosen>=0 ? DrawnGroup(d.chosen) : -1;
    // A short route's end: its pin ringed in the route's colour.
    for(int m=0;m<markedCount;++m) {
        const int g=DrawnGroup(d.targets[marked[m]]);
        if(g>=0 && g!=fromGroup)Sprite(ring,d.groups[g].at,kPinRadius+14.f,RouteColour(d.targetState[marked[m]]));
    }
    // The chosen group last (on top). A group of several places: a second ring round it in its colour.
    for(int pass=0;pass<2;++pass)
        for(int k=0;k<d.groupCount;++k) {
            const Group& g=d.groups[k];
            const bool chosen=k==chosenGroup;
            if(chosen!=(pass==1))continue;
            const Pixel c=g.at;
            if(!Near(c,s,40.f))continue;
            const bool many=g.members>1;
            const float r=kPinRadius+(chosen ? 4.f : 0.f)+(many ? 2.f : 0.f);
            Sprite(disc,c,r+3.f,0xB0000000u);
            if(many) {
                Sprite(disc,c,r+9.f,0x90000000u);
                Sprite(ring,c,r+8.f,StateColour(g.state));
            }
            Sprite(disc,c,r,StateColour(g.state));
            if(chosen)Sprite(ring,c,r+(many ? 14.f : 9.f),0xFFFFFFFFu);
            wchar_t count[8];
            MissionNumber(g.missions-1,0,count,std::size(count));
            const uitext::Label l=labels->Get(*renderer,count,chosen ? 22.f : 18.f,true,drawn.lang,frame);
            if(l.view)renderer->Image(l.view,c.x-l.w*0.5f,c.y-l.h*0.5f,c.x+l.w*0.5f,c.y+l.h*0.5f,0.f,0.f,1.f,1.f,
                                      g.state==3 ? 0xFFFFFFFFu : 0xFF101010u);
        }
}

// The chosen place's panel: its name, its missions (number, title, state), the cursor's line lit, the keys.
void DrawPanel() noexcept {
    const Shot& d=drawn;
    renderer->Rect(kPanel.x0,kPanel.y0,kPanel.x1,kPanel.y1,0xC80A1420u);
    renderer->Rect(kPanel.x0,kPanel.y0,kPanel.x1,kPanel.y0+2.f,0xFFB8C4D0u);
    renderer->Rect(kPanel.x0,kPanel.y1-2.f,kPanel.x1,kPanel.y1,0xFFB8C4D0u);
    const float x=kPanel.x0+16.f,w=kPanel.x1-kPanel.x0-32.f;
    if(d.chosen>=0) {
        const int lang=static_cast<int>(d.lang);
        const float named=Text(kPlaces[d.chosen].name[lang>=0 && lang<4 ? lang : 0],x,kPanel.y0+12.f,30.f,true,0xFFFFFFFFu,
                               w-60.f);
        wchar_t count[8];
        MissionNumber(d.total-1,0,count,std::size(count));
        Text(count,x+named+12.f,kPanel.y0+17.f,24.f,false,0xFFB8C4D0u);
    }
    renderer->Rect(x,kPanel.y0+kPanelHeading-6.f,x+w,kPanel.y0+kPanelHeading-4.f,0x80B8C4D0u);
    // 本关出动: the preset the chosen mission starts with (lit when remembered for it), its keys at the right.
    {
        const float y=kLaunchRow.y0;
        // A held start (Launch): its line instead of the preset, amber while the generator runs, red when it failed.
        const bool noted=d.launchNote[0]!=0;
        const std::uint32_t back=noted ? (d.launchFailed ? 0xC0A02020u : 0xA0806010u) : d.remembered ? 0x90F07A10u : 0x60302A1Eu;
        renderer->Rect(kLaunchRow.x0+4.f,y,kLaunchRow.x1-4.f,y+kPanelRow-4.f,back);
        const float label=noted ? 0.f : Text(d.launchLabel,x,y+7.f,22.f,true,0xFFEDE6D2u,140.f);
        Text(noted ? d.launchNote : d.launchName,x+(noted ? 0.f : label+14.f),y+(noted ? 9.f : 7.f),noted ? 18.f : 22.f,false,
             0xFFFFFFFFu,noted ? w : w-label-14.f);
    }
    ID3D11ShaderResourceView* const disc=labels->Disc(*renderer);
    for(int i=0;i<d.lines;++i) {
        const PanelLine& l=d.line[i];
        const float y=PanelLineTop(i);
        if(l.cursor)renderer->Rect(kPanel.x0+4.f,y,kPanel.x1-4.f,y+kPanelRow-2.f,0x70FFD23Cu);
        Sprite(disc,Pixel{x+8.f,y+kPanelRow*0.5f},7.f,StateColour(l.state));
        const std::uint32_t colour=l.state==3 ? 0xFFA0A0A0u : 0xFFFFFFFFu;
        Text(l.num,x+24.f,y+5.f,24.f,false,colour,64.f);
        Text(l.title,x+92.f,y+5.f,24.f,false,colour,w-92.f);
    }
    // More above / below: small arrows at the list's right edge.
    const float ax=kPanel.x1-22.f;
    if(d.first>0) {
        const float y=PanelLineTop(0)+2.f;
        const float tri[4][2]={{ax,y},{ax,y},{ax-8.f,y+10.f},{ax+8.f,y+10.f}};
        renderer->Quad(tri,0xFFFFFFFFu);
    }
    if(d.first+d.lines<d.total) {
        const float y=PanelLineTop(kShownLines)-4.f;
        const float tri[4][2]={{ax-8.f,y-10.f},{ax+8.f,y-10.f},{ax,y},{ax,y}};
        renderer->Quad(tri,0xFFFFFFFFu);
    }
    // The keys the map adds (the stock key guide is the main frame's text, not changed), three lines under the missions
    // (worldmap_logic.h kPanelKeysTop), left of the corner piece some HQ themes draw at the bottom right: the 整备 tabs,
    // places, then zoom and 本关出动.
    Text(d.tabKeys,x,PanelKeyTop(0),20.f,false,0xFFD0D8E0u,w);
    Text(d.placeKeys,x,PanelKeyTop(1),20.f,false,0xFFD0D8E0u,300.f);
    wchar_t third[112];
    _snwprintf_s(third,_TRUNCATE,L"%ls   %ls",d.zoomKeys,d.launchKeys);   // 本关出动's keys after the zoom's
    Text(third,x,PanelKeyTop(2),20.f,false,0xFFD0D8E0u,w);
}

// The 整备 title bar (armory_skin.h: the same bar, tabs, chips as the armory's; the map's tab lit) and the campaign's
// progress under it.
void DrawTitle() noexcept {
    namespace skin=crew::armory::skin;
    const Shot& d=drawn;
    static skin::Quads quads;   // render thread, under gpuLock
    quads.n=0;quads.dropped=0;
    skin::Bar(&quads,kMapTitle);
    for(int i=0;i<kTabCount;++i)skin::Tab(&quads,MapTab(i),i==kMapTab);
    const int chips=d.presetCount<skin::layout::kChipMost ? d.presetCount : skin::layout::kChipMost-1;
    for(int i=0;i<chips;++i) {
        const skin::Rect r=MapChip(i,chips);
        skin::Chip(&quads,r,i==d.presetCurrent,
                   (d.pointerShown && r.Contains(d.pointer.x,d.pointer.y)) || i==d.launch);
    }
    for(int i=0;i<quads.n;++i)renderer->QuadColours(quads.q[i].xy,quads.q[i].argb);
    for(int i=0;i<kTabCount;++i) {
        const skin::Rect r=MapTab(i);
        Text(d.tab[i],r.x0+25.f,r.y0+9.f,24.f,true,skin::TextOn(i==kMapTab),r.W()-35.f);
    }
    for(int i=0;i<chips;++i) {
        const skin::Rect r=MapChip(i,chips);
        wchar_t n[4];
        _snwprintf_s(n,_TRUNCATE,L"%d",i+1);
        Text(n,r.x0+13.f,r.y0+6.f,22.f,true,skin::TextOn(i==d.presetCurrent),r.W()-10.f);
    }
    if(d.progress.shown>0) {
        const Area& b=kProgressBar;
        renderer->Rect(b.x0,b.y0,b.x1,b.y1,0xC0141310u);
        renderer->Rect(b.x0+2.f,b.y0+2.f,b.x0+2.f+(b.x1-b.x0-4.f)*ProgressShare(d.progress),b.y1-2.f,0xFFD8C69Cu);
        Text(d.progressText,b.x1+12.f,b.y0-4.f,16.f,false,0xFFEDE6D2u,500.f);
    }
}

// Where the select's window skins draw their large empty frame (Window02_SoldierSel1 / 2's .B / .c meshes reach x
// 1920, y 113..956; docs/worldmap-select-re.md §8.1): the map drawn again over it after the stock draw. The kept windows
// (x 842..1382, y 223..851 with their borders) are left alone; the frame's left bar shows above and below them.
constexpr Area kCovers[]={{1384.f,96.f,1920.f,984.f},{1336.f,96.f,1384.f,212.f},{1336.f,856.f,1384.f,984.f}};

// Pass 1 (before the stock draw): the map, the routes, the pins, a stage-2 hook; the tiles fetched.
void DrawMap(unsigned char* self,unsigned char* ctx) noexcept {
    ID3D11DeviceContext* device=nullptr;
    int vp[4]{};
    if(!ReadDrawCtx(ctx,&device,vp))return;
    std::lock_guard<std::mutex> hold(gpuLock);
    if(!renderer)renderer=new(std::nothrow) gpu::Renderer();
    if(!labels)labels=new(std::nothrow) uitext::Labels();
    if(!cache)cache=new(std::nothrow) TileCache();
    if(!renderer || !labels || !cache)return;
    const gpu::Viewport viewport{static_cast<float>(vp[0]),static_cast<float>(vp[1]),static_cast<float>(vp[2]),
                                 static_cast<float>(vp[3])};
    const gpu::Renderer::Begun begun=renderer->Begin(device,viewport);
    if(begun==gpu::Renderer::Begun::broken) {
        if(gpuState.exchange(kGpuBroken)!=kGpuBroken)
            Log("WORLDMAP the map cannot be drawn on this device: %s: the mission select stays stock",renderer->Why());
        renderer->Reset();
        labels->Reset();
        cache->Reset();
        return;
    }
    if(begun!=gpu::Renderer::Begun::ok)return;
    cache->Bind(*renderer);
    struct Ender { gpu::Renderer* r; ~Ender() { r->End(); } } ender{renderer};
    ++frame;
    const Screen screen{1920.f,1080.f,viewport.w};
    View v=drawn.view;
    Clamp(v,screen);
    // The ocean's colour under tiles not there yet (the first frames: nothing decoded).
    renderer->Rect(0.f,0.f,screen.w,screen.h,0xFF0B1B30u);
    int xy[2*kMostVisible];
    const int n=DrawTiles(v,screen,xy);
    DrawMarks(v,screen);
    if(drawHook) {
        worldmapui::Frame f;
        f.view=v;f.screen=screen;f.viewport=viewport;f.pointer=drawn.pointer;f.pointerShown=drawn.pointerShown;f.select=self;
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
        cache->Upload(tile,LevelOfIndex(tile.index),frame);
    }
    const int level=LevelFor(v,screen);
    int want[3+kMostVisible];
    int wanted=0;
    for(int i=0;i<3;++i)if(!cache->Has(i) && !failed[i])want[wanted++]=i;
    for(int i=0;i<n;++i) {
        const int index=TileIndex(level,WrapColumn(level,xy[2*i]),xy[2*i+1]);
        if(index>=0 && !failed[index] && !cache->Has(index))want[wanted++]=index;
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

// Pass 2 (after the stock draw, under every later screen: a dialog, the main frame's title and key guide): the map
// again over the window skins' empty frame (`whole`: over all the select drew, its windows moving: worldmap_logic.h
// SelectCover), the panel on it, the pointer over everything of the select. True when drawn.
constexpr Area kWholeScreen[]={{0.f,0.f,1920.f,1080.f}};
bool DrawOver(unsigned char* ctx,bool whole) noexcept {
    ID3D11DeviceContext* device=nullptr;
    int vp[4]{};
    if(!ReadDrawCtx(ctx,&device,vp))return false;
    std::lock_guard<std::mutex> hold(gpuLock);
    if(!renderer || !labels || !cache || gpuState.load()!=kGpuReady)return false;
    const gpu::Viewport viewport{static_cast<float>(vp[0]),static_cast<float>(vp[1]),static_cast<float>(vp[2]),
                                 static_cast<float>(vp[3])};
    if(renderer->Begin(device,viewport)!=gpu::Renderer::Begun::ok)return false;
    struct Ender { gpu::Renderer* r; ~Ender() { r->End(); } } ender{renderer};
    cache->Bind(*renderer);
    const Screen screen{1920.f,1080.f,viewport.w};
    View v=drawn.view;
    Clamp(v,screen);
    int xy[2*kMostVisible];
    const Area* const areas=whole ? kWholeScreen : kCovers;
    const std::size_t count=whole ? std::size(kWholeScreen) : std::size(kCovers);
    for(std::size_t i=0;i<count;++i) {
        const Area& a=areas[i];
        renderer->Clip(a.x0,a.y0,a.x1,a.y1);
        renderer->Rect(a.x0,a.y0,a.x1,a.y1,0xFF0B1B30u);
        DrawTiles(v,screen,xy);
        DrawMarks(v,screen);
    }
    renderer->Unclip();
    DrawPanel();
    DrawTitle();
    // The pointer: a small cross (the select draws no cursor of its own over the map).
    if(drawn.pointerShown) {
        const float x=drawn.pointer.x,y=drawn.pointer.y;
        renderer->Line(x-11.f,y,x+11.f,y,4.f,0xC0000000u);
        renderer->Line(x,y-11.f,x,y+11.f,4.f,0xC0000000u);
        renderer->Line(x-10.f,y,x+10.f,y,2.f,0xFFFFFFFFu);
        renderer->Line(x,y-10.f,x,y+10.f,2.f,0xFFFFFFFFu);
    }
    return true;
}

// The select's own transform (0x7F9E90, what the stock draw places its layout by; the 0.3 s slide in and out,
// docs/worldmap-select-re.md §1.2): its translation in *x / *y; false when not readable.
bool Translation(unsigned char* self,float* x,float* y) noexcept {
    alignas(16) float m[16]{};
    __try {
        reinterpret_cast<float*(__fastcall*)(unsigned char*,float*)>(image+kTransform)(self,m);
    } __except(EXCEPTION_EXECUTE_HANDLER){ return false; }
    *x=m[12];*y=m[13];
    return true;
}
// Render thread: the select's motion (worldmap_logic.h MotionStep), the select it is of.
// (A select is told apart by its generation too: the next one may be allocated where the last was.)
struct Slide { unsigned char* self=nullptr; unsigned generation=0; MotionTrack track; Motion logged=Motion::rest; bool any=false; };
Slide slide;
Motion MotionOf(unsigned char* self) noexcept {
    float x=0.f,y=0.f;
    const bool readable=Translation(self,&x,&y);
    const unsigned generation=selectGeneration.load();
    if(slide.self!=self || slide.generation!=generation){slide=Slide{};slide.self=self;slide.generation=generation;}
    const Motion m=MotionStep(slide.track,readable,x,y);
    if(!slide.any || m!=slide.logged) {
        static const char* const kName[]={"entering (the whole screen covered)","at rest (the panel over the empty frame)",
                                          "leaving (the whole screen covered)"};
        Log("WORLDMAP select %p: transform translation (%.1f, %.1f)%s: %s",static_cast<void*>(self),static_cast<double>(x),
            static_cast<double>(y),readable ? "" : " (not readable)",kName[static_cast<int>(m)]);
        slide.any=true;slide.logged=m;
    }
    return m;
}

// Who is on screen over the select (worldmap_logic.h SelectCover; docs/worldmap-select-re.md §11): the map under the
// stock draw and, after it, over its empty frame at rest or over all of it while its windows slide; 整备 over it while
// it closes for 整备 (armory.h PaintOverSelect).
std::uint64_t __fastcall DrawHook(unsigned char* self,unsigned char* ctx) {
    if(!ctx)return nextDraw(self,ctx);
    Read(drawn);
    const bool mapped=drawn.on && drawn.select==self && gpuState.load()!=kGpuBroken && store;
    const Motion motion=MotionOf(self);   // the transform the stock draw places its layout by (0x7F9E90): a getter
    const SelectCover cover=CoverFor(mapped,motion,armoryui::WaitingOverSelect());
    if(cover==SelectCover::frames || cover==SelectCover::whole)DrawMap(self,ctx);
    const std::uint64_t result=nextDraw(self,ctx);
    armoryui::MapDrawn();   // 整备 held drawn on its screen since it asked for the map: given up once the map is on
    bool drew=false;
    if(cover==SelectCover::frames || cover==SelectCover::whole)drew=DrawOver(ctx,cover==SelectCover::whole);
    else if(cover==SelectCover::armory)drew=armoryui::PaintOverSelect(ctx);
    if(MapInPlace(cover,drew,motion))armoryui::MapShown();
    return result;
}

void* __fastcall GoneHook(unsigned char* self,unsigned flags) {
    {
        AcquireSRWLockExclusive(&shotLock);
        const bool ours=shot.select==self;
        if(ours)shot=Shot{};
        ReleaseSRWLockExclusive(&shotLock);
        if(ours || ui.self==self) {
            gone.store(self);
            wheelToMapUntil.store(0);
            selectGeneration.fetch_add(1);
            if(store)store->Cancel();   // the queued and decoded tiles not taken (up to the whole wanted set)
            std::lock_guard<std::mutex> hold(gpuLock);
            if(renderer)renderer->Reset();   // every texture and device object (made again by the next select)
            if(labels)labels->Reset();
            if(cache)cache->Reset();
            Log("WORLDMAP select %p gone: its textures released",static_cast<void*>(self));
        }
    }
    return nextGone(self,flags);
}

// The select's confirm callback (Agent_Object<HUiHQMissionSelect>::Invoke, run by its button inside the stock OnUpdate,
// the HQ's and the room's alike): 本关出动's preset switched in before the stock confirm starts the mission.
using InvokeFn=std::uint64_t(__fastcall*)(unsigned char*,void*);
InvokeFn nextInvoke=nullptr;
std::uint64_t __fastcall InvokeHook(unsigned char* functor,void* result) {
    unsigned char* screen=nullptr;
    std::uintptr_t pmf=0;
    __try {
        screen=*reinterpret_cast<unsigned char* const*>(functor+kFunctorScreen);
        pmf=*reinterpret_cast<const std::uintptr_t*>(functor+kFunctorPmf);
    } __except(EXCEPTION_EXECUTE_HANDLER){ screen=nullptr; }
    // Held (Launch false): the stock confirm not called, as Invoke itself skips it when the screen is gone (0x8A40E4:
    // its return value is cbRequestMission's void, nobody reads it); AfterStock starts it when the generator is done.
    if(screen && screen==ui.self && pmf==reinterpret_cast<std::uintptr_t>(image+kConfirm) && gone.load()!=screen &&
       !Launch(screen,"the stock confirm"))return 0;
    return nextInvoke(functor,result);
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
    bool mouseChanged=false;
    if(!edf::RedirectCall(image+kVerticalMouseCall,image+kVerticalMouse,reinterpret_cast<void*>(&VerticalListMouse),mouseChanged))
        return false;
    // The destructor first (a new select at a dead one's address must not inherit its state), then the draw, then the
    // update (which hides the list): a later slot that fails leaves the earlier ones inert.
    if(!Chain(kGoneSlot,kGone,reinterpret_cast<void*>(&GoneHook),nextGone,"destructor") ||
       !Chain(kDrawSlot,kDraw,reinterpret_cast<void*>(&DrawHook),nextDraw,"Draw") ||
       !Chain(kUpdateSlot,kUpdate,reinterpret_cast<void*>(&UpdateHook),nextUpdate,"OnUpdate"))return false;
    Log("HOOK worldmap select chained (vtable 0x18052E8 slots 1, 2, 5): the list becomes the world map once a select opens");
    // Stage 6, each on its own (a mismatch leaves that part out, not the map): the 返回 a 整备 tab gives, 本关出动.
    backOk=AllMatch(kBackSigs);
    if(!backOk)Log("HOOK worldmap: the 返回 key's code at EDF+0x84B544 is not as expected: the map's 整备 tabs do nothing");
    launchOk=AllMatch(kLaunchSigs) && Chain(kInvokeSlot,kInvoke,reinterpret_cast<void*>(&InvokeHook),nextInvoke,"confirm callback");
    if(!launchOk)Log("HOOK worldmap: the select's confirm callback is not as expected: 本关出动 is shown, not switched in");
    return true;
}

}  // namespace crew
