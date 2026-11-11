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
#include "worldmap_text.h"
#include "worldmap_tiles.h"
#include "crew.h"
#include "memory.h"
#include "map.h"

#include <Xinput.h>
#include <atomic>
#include <cmath>
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
constexpr unsigned kFocus=0x953350;     // (HuiButtonListUtil* select+0x770, int i): the native focus to display entry i
constexpr unsigned kConfirm=0x8A85C0;   // cbRequestMission(select, const Result*): confirms entry +0x778 (rdx not read)
constexpr unsigned kTextOf=0x7A3500;    // const wchar_t* (text table, const wchar_t* key)
constexpr unsigned kNumber=0x52F50;     // double (const Handle*): a number node's value
constexpr unsigned kNotOpenKey=0x1810C80;   // L"MissionSelect_NotOpenMissionTitle" (the locked row's title, 0x9591B6)
constexpr std::size_t kTextTable=0x20B29A8,kTitles=0x248,kCurrent=0x48;   // [EDF+..]: texts; [GS]+0x248 the TXT list; GS+0x48
constexpr std::size_t kButtons=0x770,kSelectMode=0x7B4;   // the select's HuiButtonListUtil; +0x7B4 bit 0: confirmable
constexpr std::size_t kRepeat=0x4528;   // ui::Input: every pad's repeat mask (1 up, 2 down, 4 left, 8 right)
constexpr unsigned kLeftBit=4,kRightBit=8;

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
const unsigned char kCurrentSig[]={0x89,0x51,0x48,0x45,0x84,0xC0,0x74,0x06,0x89,0x91,0xA4,0x57,0x00,0x00,0xC3};
const Sig kSigs[]={{kUpdate,kUpdateSig,sizeof(kUpdateSig)},{kDraw,kDrawSig,sizeof(kDrawSig)},{kGone,kGoneSig,sizeof(kGoneSig)},
    {kFind,kFindSig,sizeof(kFindSig)},{0x7E999C,kShowSig,sizeof(kShowSig)},{kRowCount,kRowCountSig,sizeof(kRowCountSig)},
    {kChildIndex,kChildIndexSig,sizeof(kChildIndexSig)},{kNodeType,kNodeTypeSig,sizeof(kNodeTypeSig)},
    {kNodeCount,kNodeCountSig,sizeof(kNodeCountSig)},{kNodeText,kNodeTextSig,sizeof(kNodeTextSig)},
    {0xE0DAE,kKeyReadSig,sizeof(kKeyReadSig)},{0x8A2BC1,kCfgSig,sizeof(kCfgSig)},{0x8A85E8,kCursorSig,sizeof(kCursorSig)},
    {0x8A5029,kInputSig,sizeof(kInputSig)},{0x8A5303,kDtSig,sizeof(kDtSig)},{0x11A43EA,kExecSig,sizeof(kExecSig)},
    {0x113E90B,kViewportSig,sizeof(kViewportSig)},{0x113E928,kDeviceSig,sizeof(kDeviceSig)},
    {kFocus,kFocusSig,sizeof(kFocusSig)},{kConfirm,kConfirmSig,sizeof(kConfirmSig)},{kTextOf,kTextOfSig,sizeof(kTextOfSig)},
    {kNumber,kNumberSig,sizeof(kNumberSig)},{0x9591AF,kNotOpenSig,sizeof(kNotOpenSig)},{0x958A66,kTitleSig,sizeof(kTitleSig)},
    {0x8A26AA,kModeSig,sizeof(kModeSig)},{0x8A5371,kRepeatSig,sizeof(kRepeatSig)},{0xDC0B0,kCurrentSig,sizeof(kCurrentSig)}};

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
    int targets[kMaxTargets]{};   // the places of its open successors (the routes)
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
    // The routes: the current mission's successors that are open (worldmap_logic.h RouteTargets), by their places.
    const int total=RowCount();
    static int stateOfRow[kMaxEntries];
    for(int i=0;i<kMaxEntries;++i)stateOfRow[i]=4;
    for(int i=0;i<r.n;++i)if(r.row[i]>=0 && r.row[i]<kMaxEntries)stateOfRow[r.row[i]]=r.state[i];
    int next[kMaxTargets],open[kMaxTargets];
    const int nextCount=r.current>=0 ? Successors(r.current,next,kMaxTargets) : 0;
    const int openCount=RouteTargets(r.current,next,nextCount,stateOfRow,total<kMaxEntries ? total : kMaxEntries,open,kMaxTargets);
    for(int i=0;i<openCount;++i) {
        int place=-1;
        for(int e=0;e<r.n;++e)if(r.row[e]==open[i]){place=r.place[e];break;}
        bool seen=place<0 || place==r.currentPlace;
        for(int k=0;k<r.targetCount && !seen;++k)seen=r.targets[k]==place;
        if(!seen)r.targets[r.targetCount++]=place;
    }
    NotOpenTitle(r.notOpen,std::size(r.notOpen));
    Log("WORLDMAP select %p: %d entries, %d pins (%d entries without a place), current row %d (entry %d, place %ls), "
        "%d successors, %d open, %d route places",static_cast<void*>(self),r.n,r.pinCount,unknown,r.current,r.currentEntry,
        r.currentPlace>=0 ? kPlaces[r.currentPlace].id : L"-",nextCount,openCount,r.targetCount);
}
int PinOfPlace(int place) noexcept {
    for(int i=0;i<rows.pinCount;++i)if(rows.pins[i].place==place)return i;
    return -1;
}
int EntryPlace(int entry) noexcept { return entry>=0 && entry<rows.n ? rows.place[entry] : -1; }

// --- What the UI thread hands the draw (under shotLock) ---
constexpr int kShownLines=kPanelRows-1;   // the panel's last line: the keys
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
    int chosen=-1,current=-1;
    int targetCount=0;
    int targets[kMaxTargets]{};
    // The panel: the chosen place's missions from `first` (of `total`).
    int lines=0,first=0,total=0;
    PanelLine line[kShownLines]{};
    hudtext::Lang lang=hudtext::Lang::en;
};
SRWLOCK shotLock=SRWLOCK_INIT;
Shot shot;
// ...and what the draw hands back: the viewport and the target's size (the pointer's mapping, the sharpness).
struct Geometry { float vp[4]{}; float targetW=0.f,targetH=0.f; };
Geometry geometry;
void Publish(const Shot& s) noexcept { AcquireSRWLockExclusive(&shotLock);shot=s;ReleaseSRWLockExclusive(&shotLock); }
void Read(Shot& s) noexcept { AcquireSRWLockShared(&shotLock);s=shot;ReleaseSRWLockShared(&shotLock); }
Geometry ReadGeometry() noexcept { AcquireSRWLockShared(&shotLock);const Geometry g=geometry;ReleaseSRWLockShared(&shotLock);return g; }

// --- The draw's state (render thread; the destructor takes gpuLock to release it) ---
std::mutex gpuLock;
gpu::Renderer* renderer=nullptr;            // never freed (no device calls at the DLL's unload)
worldmaptext::Labels* labels=nullptr;       // the same
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
    bool confirm=false;               // confirm entry +0x778 after the stock OnUpdate (a second click on its line)
    int panelFirst=0,panelLines=0;    // the panel's lines as last published (the mouse's hit test)
    int panelEntry[kShownLines]{};
};
Ui ui;
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
// The cursor to entry i: the native focus asked (it moves in the stock OnUpdate after this hook).
void Choose(unsigned char* self,int entry,const char* why) noexcept {
    if(entry<0 || entry>=rows.n)return;
    Focus(self,entry);
    ui.focusing=entry;
    ui.focusFrames=0;
    Log("WORLDMAP select %p: %s: entry %d (row %d, %ls)",static_cast<void*>(self),why,entry,rows.row[entry],
        rows.place[entry]>=0 ? kPlaces[rows.place[entry]].id : L"-");
}
// The entry to choose at pin k: its first enterable, else its first (a locked place's panel shown).
int EntryOfPin(int k) noexcept {
    const int e=FirstEnterable(rows.place,rows.state,rows.n,rows.pins[k].place);
    return e>=0 ? e : rows.pins[k].first;
}

// This frame's map input; true when the stock OnUpdate must not get it (a click, a drag, the wheel, a stick, a zoom).
bool TakeInput(unsigned char* self,const unsigned char* ctx,const Screen& s,float dt,bool input) noexcept {
    const bool front=InFront() && !MapHoldsKeys();   // (the mission's map view is never open over the select; the census)
    ReadPointer(s);
    const bool overMap=ui.pointerShown && !OverPanels(ui.pointer) && !OverPanel(ui.pointer);
    const bool overPanel=ui.pointerShown && OverPanel(ui.pointer);
    wheelToMap.store(input && front && overMap,std::memory_order_relaxed);
    const int notches=wheel.exchange(0);
    XINPUT_STATE pad{};
    const bool havePad=(front || edf::PadSource()) && PadState(&pad);
    const bool r3=havePad && (pad.Gamepad.wButtons&XINPUT_GAMEPAD_RIGHT_THUMB)!=0;
    const bool rKey=front && Down('R');
    const bool lb=front && Down(VK_LBUTTON);
    if(!input) {
        ui.dragging=false;
        ui.lbWas=lb;ui.r3Was=r3;ui.rKeyWas=rKey;
        return false;
    }
    bool used=false;
    const int cursor=CursorIndex(self);
    // A click: a panel line (the cursor's line again: confirm), a pin (its place chosen), else the map dragged.
    if(lb && !ui.lbWas) {
        if(overPanel) {
            const int line=PanelLineAt(ui.pointer,ui.panelLines);
            if(line>=0) {
                const int entry=ui.panelEntry[line];
                if(entry==cursor)ui.confirm=Confirmable(self);
                else Choose(self,entry,"a panel line clicked");
            }
            used=true;
        } else if(overMap) {
            Pixel at[kPlaceCount];
            PinPixels(ui.view,s,at);
            const int k=Pick(at,rows.pinCount,ui.pointer,22.f);
            if(k>=0)Choose(self,EntryOfPin(k),"a pin clicked");
            else{ui.dragging=true;ui.dragAt=ui.pointer;}
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
    if(dir!=0 && rows.pinCount>0) {
        Pixel at[kPlaceCount];
        bool ok[kPlaceCount];
        PinPixels(ui.view,s,at);
        for(int i=0;i<rows.pinCount;++i)ok[i]=Enterable(rows.pins[i].state);
        int from=PinOfPlace(EntryPlace(cursor));
        int k=-1;
        if(from>=0)k=Neighbour(at,ok,rows.pinCount,from,dir);
        else {
            // No pin chosen: the nearest to the map's focus that way.
            Pixel withFocus[kPlaceCount+1];
            bool okFocus[kPlaceCount+1];
            for(int i=0;i<rows.pinCount;++i){withFocus[i]=at[i];okFocus[i]=ok[i];}
            withFocus[rows.pinCount]=kMapFocus;okFocus[rows.pinCount]=false;
            k=Neighbour(withFocus,okFocus,rows.pinCount+1,rows.pinCount,dir);
        }
        if(k>=0)Choose(self,EntryOfPin(k),dir<0 ? "left" : "right");
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
        const bool out=Down('Q'),in=Down('E');
        if(out || in){const float f=HoldZoom(out,in,dt);if(f!=1.f)ZoomAt(ui.view,s,kMapFocus,f);ui.moving=false;}
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

// The shot's pins and panel from the rows and the cursor.
void Describe(Shot& out,unsigned char* self) noexcept {
    out.pinCount=rows.pinCount;
    for(int i=0;i<rows.pinCount;++i)out.pins[i]=rows.pins[i];
    const int cursor=CursorIndex(self);
    out.chosen=EntryPlace(cursor);
    out.current=rows.currentPlace;
    out.targetCount=rows.targetCount;
    for(int i=0;i<rows.targetCount;++i)out.targets[i]=rows.targets[i];
    out.lang=hudtext::InUse();
    ui.panelLines=0;
    if(out.chosen<0)return;
    int at[kMaxEntries];
    const int count=EntriesAt(rows.place,rows.n,out.chosen,at,kMaxEntries);
    int line=-1;
    for(int i=0;i<count;++i)if(at[i]==cursor)line=i;
    const int first=ScrollFirst(count,line,kShownLines);
    const int total=RowCount();
    out.first=first;out.total=count;
    for(int i=first;i<count && out.lines<kShownLines;++i) {
        const int e=at[i];
        PanelLine& l=out.line[out.lines];
        MissionNumber(rows.row[e],rows.field5[e],l.num,std::size(l.num));
        if(rows.state[e]==3)wcsncpy_s(l.title,rows.notOpen,_TRUNCATE);
        else if(rows.row[e]<total)Title(rows.row[e],l.title,std::size(l.title));
        else l.title[0]=0;
        l.state=rows.state[e];
        l.cursor=e==cursor;
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
    if(ui.confirm) {
        ui.confirm=false;
        const int cursor=CursorIndex(self);
        Log("WORLDMAP select %p: confirm entry %d (row %d) by a click",static_cast<void*>(self),cursor,
            cursor>=0 && cursor<rows.n ? rows.row[cursor] : -1);
        Confirm(self);
    }
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
    if(ui.started)Describe(on,self);
    Publish(on);
    std::uint64_t result;
    if(used) {
        // The stock OnUpdate as when it does not own the input (§5.2): no scroll, no button; its cursor refresh runs.
        alignas(8) unsigned char copy[kCtxSize];
        std::memcpy(copy,ctx,kCtxSize);
        std::memset(copy+kCtxInput,0,sizeof(void*));
        result=nextUpdate(self,copy);
    } else {
        result=nextUpdate(self,ctx);
    }
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
    return n;
}

float Seconds() noexcept { return static_cast<float>(GetTickCount64()%1000000ull)/1000.f; }

// A label at (x, y) (its top left), shadowed; at most `maxW` wide (cut at its right); its width.
float Text(const wchar_t* text,float x,float y,float size,bool bold,std::uint32_t argb,float maxW=4000.f) noexcept {
    const worldmaptext::Label l=labels->Get(*renderer,text,size,bold,drawn.lang,frame);
    if(!l.view)return 0.f;
    const float w=l.w<maxW ? l.w : maxW,u1=l.w>0.f ? w/l.w : 1.f;
    renderer->Image(l.view,x+1.5f,y+1.5f,x+1.5f+w,y+1.5f+l.h,0.f,0.f,u1,1.f,0xC0000000u);
    renderer->Image(l.view,x,y,x+w,y+l.h,0.f,0.f,u1,1.f,argb);
    return w;
}
void Sprite(ID3D11ShaderResourceView* view,Pixel c,float r,std::uint32_t argb) noexcept {
    if(view)renderer->Image(view,c.x-r,c.y-r,c.x+r,c.y+r,0.f,0.f,1.f,1.f,argb);
}
bool Near(Pixel p,const Screen& s,float margin) noexcept {
    return p.x>-margin && p.y>-margin && p.x<s.w+margin && p.y<s.h+margin;
}

// The routes (dashed great circles from the current mission's place), its ring, the pins.
void DrawMarks(const View& v,const Screen& s) noexcept {
    const Shot& d=drawn;
    const float now=Seconds();
    if(d.current>=0) {
        const Place& from=kPlaces[d.current];
        for(int t=0;t<d.targetCount;++t) {
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
                            if(phase<16.f)
                                renderer->Line(p[i-1].x+dx*a/len,p[i-1].y+dy*a/len,p[i-1].x+dx*b/len,p[i-1].y+dy*b/len,
                                               pass==0 ? 7.f : 3.5f,pass==0 ? 0x90000000u : 0xFFFFE680u);
                            a=b;
                        }
                        along+=len;
                    }
                }
            }
        }
        // The current mission's place: a steady ring and one pulsing out from it.
        const Pixel c=ToScreen(v,s,PlacePoint(d.current));
        if(Near(c,s,80.f)) {
            const float t=Pulse(now);
            const std::uint32_t a=static_cast<std::uint32_t>((1.f-t)*230.f);
            Sprite(labels->Ring(*renderer),c,20.f+30.f*t,(a<<24)|0x00FFE680u);
            Sprite(labels->Ring(*renderer),c,21.f,0xF0FFE680u);
        }
    }
    ID3D11ShaderResourceView* const disc=labels->Disc(*renderer);
    ID3D11ShaderResourceView* const ring=labels->Ring(*renderer);
    // The chosen place's pin last (on top).
    for(int pass=0;pass<2;++pass)
        for(int i=0;i<d.pinCount;++i) {
            const Pin& pin=d.pins[i];
            const bool chosen=pin.place==d.chosen;
            if(chosen!=(pass==1))continue;
            const Pixel c=ToScreen(v,s,PlacePoint(pin.place));
            if(!Near(c,s,40.f))continue;
            const float r=chosen ? 17.f : 13.f;
            Sprite(disc,c,r+3.f,0xB0000000u);
            Sprite(disc,c,r,StateColour(pin.state));
            if(chosen)Sprite(ring,c,r+9.f,0xFFFFFFFFu);
            wchar_t count[8];
            MissionNumber(pin.count-1,0,count,std::size(count));
            const worldmaptext::Label l=labels->Get(*renderer,count,chosen ? 22.f : 18.f,true,drawn.lang,frame);
            if(l.view)renderer->Image(l.view,c.x-l.w*0.5f,c.y-l.h*0.5f,c.x+l.w*0.5f,c.y+l.h*0.5f,0.f,0.f,1.f,1.f,
                                      pin.state==3 ? 0xFFFFFFFFu : 0xFF101010u);
        }
}

// The keys the map adds, in the game's language (the stock key guide is the select's own text; not changed).
const wchar_t* KeysLine(hudtext::Lang l) noexcept {
    switch(l) {
    case hudtext::Lang::zhCN: return L"←→ 切换地点   LB/LT·Q/E 缩放   R3·R 当前任务";
    case hudtext::Lang::zhTW: return L"←→ 切換地點   LB/LT·Q/E 縮放   R3·R 目前任務";
    case hudtext::Lang::ja: return L"←→ 地点   LB/LT・Q/E ズーム   R3・R 現在の任務";
    default: return L"\x2190\x2192 Place   LB/LT, Q/E Zoom   R3, R Current";
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
        Text(kPlaces[d.chosen].name[lang>=0 && lang<4 ? lang : 0],x,kPanel.y0+12.f,30.f,true,0xFFFFFFFFu,w-60.f);
        wchar_t count[8];
        MissionNumber(d.total-1,0,count,std::size(count));
        const worldmaptext::Label l=labels->Get(*renderer,count,24.f,false,d.lang,frame);
        if(l.view)Text(count,kPanel.x1-16.f-l.w,kPanel.y0+16.f,24.f,false,0xFFB8C4D0u);
    }
    renderer->Rect(x,kPanel.y0+kPanelHeading-6.f,x+w,kPanel.y0+kPanelHeading-4.f,0x80B8C4D0u);
    ID3D11ShaderResourceView* const disc=labels->Disc(*renderer);
    for(int i=0;i<d.lines;++i) {
        const PanelLine& l=d.line[i];
        const float y=kPanel.y0+kPanelHeading+kPanelRow*static_cast<float>(i);
        if(l.cursor)renderer->Rect(kPanel.x0+4.f,y,kPanel.x1-4.f,y+kPanelRow-2.f,0x70FFD23Cu);
        Sprite(disc,Pixel{x+8.f,y+kPanelRow*0.5f},7.f,StateColour(l.state));
        const std::uint32_t colour=l.state==3 ? 0xFFA0A0A0u : 0xFFFFFFFFu;
        Text(l.num,x+24.f,y+5.f,24.f,false,colour,64.f);
        Text(l.title,x+92.f,y+5.f,24.f,false,colour,w-92.f);
    }
    // More above / below: small arrows at the list's right edge.
    const float ax=kPanel.x1-22.f;
    if(d.first>0) {
        const float y=kPanel.y0+kPanelHeading+2.f;
        const float tri[4][2]={{ax,y},{ax,y},{ax-8.f,y+10.f},{ax+8.f,y+10.f}};
        renderer->Quad(tri,0xFFFFFFFFu);
    }
    if(d.first+d.lines<d.total) {
        const float y=kPanel.y0+kPanelHeading+kPanelRow*static_cast<float>(kShownLines)-4.f;
        const float tri[4][2]={{ax-8.f,y-10.f},{ax+8.f,y-10.f},{ax,y},{ax,y}};
        renderer->Quad(tri,0xFFFFFFFFu);
    }
    Text(KeysLine(d.lang),x,kPanel.y1-36.f,20.f,false,0xFFD0D8E0u,w);
}

// Where the select's window skins draw their large empty frame (Window02_SoldierSel1 / 2's .B / .c meshes reach x
// 1920, y 113..956; docs/worldmap-select-re.md §7): the map drawn again over it after the stock draw. The kept windows
// (x 842..1382, y 223..851 with their borders) are left alone; the frame's left bar shows above and below them.
constexpr Area kCovers[]={{1384.f,96.f,1920.f,984.f},{1336.f,96.f,1384.f,212.f},{1336.f,856.f,1384.f,984.f}};

// Pass 1 (before the stock draw): the map, the routes, the pins, a stage-2 hook; the tiles fetched.
void DrawMap(unsigned char* self,unsigned char* ctx) noexcept {
    ID3D11DeviceContext* device=nullptr;
    int vp[4]{};
    if(!ReadDrawCtx(ctx,&device,vp))return;
    std::lock_guard<std::mutex> hold(gpuLock);
    if(!renderer)renderer=new(std::nothrow) gpu::Renderer();
    if(!labels)labels=new(std::nothrow) worldmaptext::Labels();
    if(!renderer || !labels)return;
    const gpu::Viewport viewport{static_cast<float>(vp[0]),static_cast<float>(vp[1]),static_cast<float>(vp[2]),
                                 static_cast<float>(vp[3])};
    const gpu::Renderer::Begun begun=renderer->Begin(device,viewport);
    if(begun==gpu::Renderer::Begun::broken) {
        if(gpuState.exchange(kGpuBroken)!=kGpuBroken)
            Log("WORLDMAP the map cannot be drawn on this device: %s: the mission select stays stock",renderer->Why());
        renderer->Reset();
        labels->Reset();
        return;
    }
    if(begun!=gpu::Renderer::Begun::ok)return;
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
        renderer->Upload(tile,LevelOfIndex(tile.index),frame);
    }
    const int level=LevelFor(v,screen);
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

// Pass 2 (after the stock draw, under every later screen: a dialog, the main frame's title and key guide): the map
// again over the window skins' empty frame, the panel on it, the pointer over everything of the select.
void DrawOver(unsigned char* ctx) noexcept {
    ID3D11DeviceContext* device=nullptr;
    int vp[4]{};
    if(!ReadDrawCtx(ctx,&device,vp))return;
    std::lock_guard<std::mutex> hold(gpuLock);
    if(!renderer || !labels || gpuState.load()!=kGpuReady)return;
    const gpu::Viewport viewport{static_cast<float>(vp[0]),static_cast<float>(vp[1]),static_cast<float>(vp[2]),
                                 static_cast<float>(vp[3])};
    if(renderer->Begin(device,viewport)!=gpu::Renderer::Begun::ok)return;
    struct Ender { gpu::Renderer* r; ~Ender() { r->End(); } } ender{renderer};
    const Screen screen{1920.f,1080.f,viewport.w};
    View v=drawn.view;
    Clamp(v,screen);
    int xy[2*kMostVisible];
    for(const Area& a:kCovers) {
        renderer->Clip(a.x0,a.y0,a.x1,a.y1);
        renderer->Rect(a.x0,a.y0,a.x1,a.y1,0xFF0B1B30u);
        DrawTiles(v,screen,xy);
        DrawMarks(v,screen);
    }
    renderer->Unclip();
    DrawPanel();
    // The pointer: a small cross (the select draws no cursor of its own over the map).
    if(drawn.pointerShown) {
        const float x=drawn.pointer.x,y=drawn.pointer.y;
        renderer->Line(x-11.f,y,x+11.f,y,4.f,0xC0000000u);
        renderer->Line(x,y-11.f,x,y+11.f,4.f,0xC0000000u);
        renderer->Line(x-10.f,y,x+10.f,y,2.f,0xFFFFFFFFu);
        renderer->Line(x,y-10.f,x,y+10.f,2.f,0xFFFFFFFFu);
    }
}

std::uint64_t __fastcall DrawHook(unsigned char* self,unsigned char* ctx) {
    bool mapped=false;
    if(ctx) {
        Read(drawn);
        mapped=drawn.on && drawn.select==self && gpuState.load()!=kGpuBroken && store;
        if(mapped)DrawMap(self,ctx);
    }
    const std::uint64_t result=nextDraw(self,ctx);
    if(mapped)DrawOver(ctx);
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
            wheelToMap.store(false);
            std::lock_guard<std::mutex> hold(gpuLock);
            if(renderer)renderer->Reset();   // every texture and device object (made again by the next select)
            if(labels)labels->Reset();
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
