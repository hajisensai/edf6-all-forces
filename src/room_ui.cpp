// The room / HQ support loadout menu's screen: room_ui.h. EDF.dll TimeDateStamp 0x678CCB46, RVAs; the RE is
// docs/room-ui-re.md (input, sub-screens, icons, layout) and docs/feature-2026-10-12-room-loadout-menu.md 阶段 4.
//
// Each frame of HUiRoom::OnUpdate (slot EDF+0x180AC48) / HUiHQMain::OnUpdate (slot EDF+0x1804DE0), on the UI thread:
//   1. the toggle (keyboard VK + a pad combination read from the game's own pad table, room_ui_logic.h KeysDown /
//      PadBits) and, open, the menu's keys step
//      room_menu.h Step; only while the screen itself would read input (0x8DAD00 with the real ctx: no sub-screen,
//      dialog or transition over it), otherwise the menu is hidden and frozen;
//   2. while open (and after closing until the closing key is up) the screen's update runs on a copy of its ctx with
//      +8 (the effective UiInput*) null: the game's own "not the input owner" state (network, members, chat run on);
//   3. after it: the room's pre-sync ticked (room_sync.h), the preview (room_preview.h) shown for the soldier under the
//      cursor, and the menu drawn into the elements tools/room_layout.py added (room_ui_logic.h decides what): texts by
//      HUiTextField::SetText, the panel, the chips and the icons by their texture skins (the panel and the chips bind
//      kWhiteTexture and tint it: Skin_Texture +0xF0 / +0x100), every element's shown flag (+0x1F8), each written only
//      when it changes (Written). The layout draws nothing by itself (empty texts, texture skins with no texture): without
//      the plugin the screen looks stock. While the menu is not shown everything the plugin set is taken back (texts
//      emptied, tints transparent, elements and the root hidden).
// Nothing is patched unless every address checks out (REFUSED in the log: both screens as they are). The slots are read
// and chained, never assumed stock: EDF6Coop chains the room's too.
#include "room_ui.h"
#include "pvp.h"
#include "outfit41.h"
#include "crew.h"
#include "hud.h"
#include "loadout_store.h"
#include "map.h"
#include "memory.h"
#include "online_authority.h"
#include "room_preview.h"
#include "room_coop_capability.h"
#include "room_sync.h"
#include "room_ui_logic.h"
#include "support_call.h"
#include "support_config.h"
#include <atomic>
#include <cstring>
#include <cwchar>
#include <iterator>

namespace crew {
namespace {
using namespace roomui;
namespace rm=roommenu;

// --- The game's side (EDF.dll 0x678CCB46; docs/room-ui-re.md, the 2026-10-12 RE in 阶段 4) ---
constexpr unsigned kRoomSlot=0x180AC48,kRoomUpdate=0x8FDFE0;   // HUiRoom vtable 0x180AC40 slot 1 (OnUpdate)
constexpr unsigned kHqSlot=0x1804DE0,kHqUpdate=0x89F740;       // HUiHQMain vtable 0x1804DD8 slot 1
// Their scalar deleting destructors (slot 5; (this, flags), bit 0: delete): the screen object's end. A new screen may be
// made at the address of the one that went: the identity is "the one whose destructor has not run", never the pointer.
constexpr unsigned kRoomGoneSlot=0x180AC68,kRoomGone=0x8F7510;  // calls ~HUiRoom 0x8F5950, then delete 0xCA0 bytes
constexpr unsigned kHqGoneSlot=0x1804E00,kHqGone=0x89F480;      // ~HUiHQMain inline, then delete 0x810 bytes
constexpr unsigned kGetInput=0x8DAD00;      // HUiBase::GetInput(this, ctx): ctx+8, nullptr while covered / in a transition
constexpr unsigned kFindIndex=0x839600;     // HUiLayout: (this, const std::wstring* name) -> the first index or -1
constexpr unsigned kComponentAt=0x839560;   // HUiLayout: (this, shared_ptr<ui::Component>* out, index), no cast
constexpr unsigned kTextAt=0x8859C0;        // HUiLayout: (this, shared_ptr<HUiTextField>* out, index), null if no TextField
constexpr unsigned kSetText=0x863690;       // HUiTextField::SetText(this, const wchar_t*): the literal text
constexpr unsigned kFind=0x839FD0;          // HUiLayout: (this, shared_ptr<ui::Component>* out, const std::wstring* name)
constexpr unsigned kTextureSkin=0x953D80;   // (shared_ptr<Skin_Texture>* out, shared_ptr<Component>* by value: released by it)
constexpr unsigned kSetTexture=0x7F63E0;    // Skin_Texture: (this, const std::wstring* path) -> bool
constexpr std::size_t kShown=0x1F8;         // a Component's shown flag (its constructor sets 1: 0x7E999C)
constexpr std::size_t kTint=0xF0,kTintFrom=0x100;   // Skin_Texture: RGBA float4s drawn lerp(+0x100, +0xF0, +0xE4) (0x7F707F,
                                                    // 0x7F70FC; the constructor sets both 1,1,1,1: 0x7F5CD2)
constexpr std::size_t kTextFont=0x228;      // HUiTextField: SetText returns at once while it is null (0x863690)
constexpr std::size_t kFontColour=0x2B0;    // HUiTextField: font colour RGBA float4 (0x8BC68C; drawn by 0x7F890C)
constexpr std::size_t kCtxSize=0x18,kCtxInput=0x8;
// The stock HQ appends menu buttons through this exact AddByName path; the new definition is not in layout_tree.
constexpr unsigned kCreateButton=0x8456D0,kScreenWeak=0x811FC0,kMakeClick=0x8FB930,kAddAction=0x7EAA40,kLockWeak=0x83E4F0,kSetButtonText=0x7F8BB0;
constexpr unsigned kEnableComponent=0x7EE280,kHasFocus=0x7EBDB0,kFocusComponent=0x7EB360;
constexpr std::size_t kComponentEnabled=0x70; // native nav's current state; +71 is the public setter's pending state
constexpr std::size_t kButtonLabel=0x220;   // HUiButton's native weak_ptr<ui::TextField> (0x83B96A / 0x83B97C)
constexpr const wchar_t* kNativeEntryName=L"vcLoad";
constexpr unsigned kPadTable=0x2136530;     // two slots of a pointer per pad channel (0x1188315)

struct Sig { unsigned rva; const unsigned char* bytes; std::size_t size; };
// The functions called, by their first bytes, and the instructions the offsets above come from.
const unsigned char kGetInputSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x83,0xB9,0xD8,0x00,0x00};
const unsigned char kFindIndexSig[]={0x40,0x53,0x48,0x83,0xEC,0x40,0x4C,0x8B,0xC2,0x48,0x8D,0x54,0x24,0x20,0xE8,0x1D};
const unsigned char kComponentAtSig[]={0x40,0x57,0x48,0x83,0xEC,0x30,0x48,0x8B,0xFA,0x48,0x8D,0x54,0x24,0x20,0xE8,0x8D};
const unsigned char kTextAtSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x48};
const unsigned char kSetTextSig[]={0x48,0x83,0xB9,0x28,0x02,0x00,0x00,0x00,0x74,0x11,0x48,0x85,0xD2,0x75,0x07,0x48};
const unsigned char kFindSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x48};
const unsigned char kTextureSkinSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
const unsigned char kSetTextureSig[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
// Skin_Texture's draw (vtable 0x17F6228 slot 3, 0x7F7040): movss xmm3..6,[rdi+100h..10Ch] (0x7F707F), then
// movss xmm0/xmm1,[rdi+0F0h/0F4h] (0x7F70FC): the tint pair; its constructor's mov dword [rbx+0F0h],1.0 (0x7F5CD2).
const unsigned char kTintFromSig[]={0xF3,0x0F,0x10,0x9F,0x00,0x01,0x00,0x00,0xF3,0x0F,0x10,0xA7,0x04,0x01,0x00,0x00,
    0xF3,0x0F,0x10,0xAF,0x08,0x01,0x00,0x00,0xF3,0x0F,0x10,0xB7,0x0C,0x01,0x00,0x00};
const unsigned char kTintSig[]={0xF3,0x0F,0x10,0x87,0xF0,0x00,0x00,0x00,0xF3,0x0F,0x10,0x8F,0xF4,0x00,0x00,0x00};
const unsigned char kTintInitSig[]={0xC7,0x83,0xF0,0x00,0x00,0x00,0x00,0x00,0x80,0x3F};
const unsigned char kFontColourSig[]={0x89,0x81,0xB0,0x02,0x00,0x00};                       // 0x8BC68C: mov [rcx+2B0h],eax
const unsigned char kFontDrawSig[]={0xF3,0x0F,0x10,0x9B,0xBC,0x02,0x00,0x00};               // 0x7F890C: movss xmm3,[rbx+2BCh]
const unsigned char kShowSig[]={0xC6,0x87,0xF8,0x01,0x00,0x00,0x01};                        // 0x7E999C: mov byte [rdi+1F8h],1
const Sig kDrawSigs[]={{kGetInput,kGetInputSig,sizeof(kGetInputSig)},{kFindIndex,kFindIndexSig,sizeof(kFindIndexSig)},
    {kComponentAt,kComponentAtSig,sizeof(kComponentAtSig)},{kTextAt,kTextAtSig,sizeof(kTextAtSig)},
    {kSetText,kSetTextSig,sizeof(kSetTextSig)},{kFind,kFindSig,sizeof(kFindSig)},
    {kTextureSkin,kTextureSkinSig,sizeof(kTextureSkinSig)},{kSetTexture,kSetTextureSig,sizeof(kSetTextureSig)},
    {0x7F707F,kTintFromSig,sizeof(kTintFromSig)},{0x7F70FC,kTintSig,sizeof(kTintSig)},{0x7F5CD2,kTintInitSig,sizeof(kTintInitSig)},
    {0x8BC68C,kFontColourSig,sizeof(kFontColourSig)},
    {0x7F890C,kFontDrawSig,sizeof(kFontDrawSig)},{0x7E999C,kShowSig,sizeof(kShowSig)}};
// These contracts are independent of the hotkey overlay: a refused native entry keeps the existing menu usable.
const unsigned char kCreateButtonSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C};
const unsigned char kScreenWeakSig[]={0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x4C,0x8B,0x41,0x10,0x48,0x8B};
const unsigned char kMakeClickSig[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x48,0x89,0x54,0x24,0x10,0x56};
const unsigned char kAddActionSig[]={0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x48,0x83,0xEC,0x60,0x48,0x8B,0x05,0x05};
const unsigned char kLockWeakSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x48};
const unsigned char kSetButtonTextSig[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x55,0x57,0x41,0x54,0x41,0x56};
const unsigned char kEnableComponentSig[]={0x88,0x51,0x71,0xC3};
const unsigned char kHasFocusSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8B,0x89,0x88,0x00,0x00,0x00};
const unsigned char kFocusComponentSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x8D};
const unsigned char kFocusEnabledSig[]={0x41,0x80,0x79,0x70,0x00}; // 0x7F02D9: native next-focus candidate eligibility
const unsigned char kButtonLabelControlSig[]={0x4C,0x8B,0x81,0x28,0x02,0x00,0x00};
const unsigned char kButtonLabelObjectSig[]={0x48,0x8B,0xB9,0x20,0x02,0x00,0x00};
const unsigned char kClickInvokeSig[]={0x48,0x63,0x4D,0x20,0x49,0x8B,0xD6,0x48,0x03,0xCE,0xFF,0x55,0x18};
const Sig kNativeMenuSigs[]={{kCreateButton,kCreateButtonSig,sizeof(kCreateButtonSig)},
    {kScreenWeak,kScreenWeakSig,sizeof(kScreenWeakSig)},{kMakeClick,kMakeClickSig,sizeof(kMakeClickSig)},
    {kAddAction,kAddActionSig,sizeof(kAddActionSig)},{kLockWeak,kLockWeakSig,sizeof(kLockWeakSig)},
    {kSetButtonText,kSetButtonTextSig,sizeof(kSetButtonTextSig)},
    {kEnableComponent,kEnableComponentSig,sizeof(kEnableComponentSig)},{kHasFocus,kHasFocusSig,sizeof(kHasFocusSig)},
    {kFocusComponent,kFocusComponentSig,sizeof(kFocusComponentSig)},{0x7F02D9,kFocusEnabledSig,sizeof(kFocusEnabledSig)},
    {0x83B96A,kButtonLabelControlSig,sizeof(kButtonLabelControlSig)},
    {0x83B97C,kButtonLabelObjectSig,sizeof(kButtonLabelObjectSig)},{0x8F8AC9,kClickInvokeSig,sizeof(kClickInvokeSig)}};
// The screens' own OnUpdates (whatever their slots hold now, these are what the game put there).
const unsigned char kRoomUpdateSig[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
// push rbx; sub rsp,30h; mov rbx,rcx; call 0x84B490 (HUiLayout's update: focus, cancel, buttons)
const unsigned char kHqUpdateSig[]={0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0xE8,0x42,0xBD,0xFA,0xFF,0x48,0x8D};
// HUiRoom's deleting destructor: mov ebx,edx; mov rdi,rcx; call ~HUiRoom (0x8F5950); test bl,1; je; mov edx,0CA0h.
const unsigned char kRoomGoneSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0xDA,0x48,0x8B,0xF9,0xE8,0x2C,0xE4,0xFF,
    0xFF,0xF6,0xC3,0x01,0x74,0x0D,0xBA,0xA0,0x0C,0x00,0x00};
// ~HUiRoom's first store: lea rax,[HUiRoom's vtable 0x180AC40]; mov [rcx],rax (0x8F5962).
const unsigned char kRoomDtorSig[]={0x48,0x8D,0x05,0xD7,0x52,0xF1,0x00,0x48,0x89,0x01};
// HUiHQMain's deleting destructor: lea rax,[its vtable 0x1804DD8]; ...; mov [rcx],rax; mov ebp,edx (0x89F480).
const unsigned char kHqGoneSig[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x48,0x83,0xEC,0x20,0x48,0x8D,0x05,0x47,0x59,0xF6,0x00,0x48,0x89,
    0x7C,0x24,0x38,0x48,0x89,0x01,0x8B,0xEA};
const Sig kScreenSigs[]={{kRoomUpdate,kRoomUpdateSig,sizeof(kRoomUpdateSig)},{kHqUpdate,kHqUpdateSig,sizeof(kHqUpdateSig)},
    {kRoomGone,kRoomGoneSig,sizeof(kRoomGoneSig)},{0x8F5962,kRoomDtorSig,sizeof(kRoomDtorSig)},{kHqGone,kHqGoneSig,sizeof(kHqGoneSig)}};
// The pad table: its channel writer 0x1182A00 (+48h value, +4Ch held, +4Dh = 0); the loop's base (0x1188315: lea r13,
// [0x2136530]); A and B from wButtons bits 12 / 13 into channels +70h / +78h (0x1188A4D).
const unsigned char kPadWriterSig[]={0xF3,0x0F,0x11,0x49,0x48,0x44,0x88,0x41,0x4C,0xC6,0x41,0x4D,0x00,0xC3};
const unsigned char kPadBaseSig[]={0x4C,0x8D,0x2D,0x14,0xE2,0xFA,0x00};
const unsigned char kPadAbSig[]={0x0F,0xB7,0x44,0x24,0x64,0x66,0xC1,0xE8,0x0C,0x24,0x01,0x74,0x05,0x0F,0x28,0xCF,0xEB,0x03,0x0F,0x28,
    0xCE,0x4A,0x8B,0x4C,0x2B,0x70,0x44,0x0F,0xB6,0xC0,0xE8,0x90,0x9F,0xFF,0xFF,0x0F,0xB7,0x44,0x24,0x64,0x66,0xC1,0xE8,0x0D,0x24,
    0x01,0x74,0x05,0x0F,0x28,0xCF,0xEB,0x03,0x0F,0x28,0xCE,0x4A,0x8B,0x4C,0x2B,0x78};
const Sig kPadSigs[]={{0x1182A00,kPadWriterSig,sizeof(kPadWriterSig)},{0x1188315,kPadBaseSig,sizeof(kPadBaseSig)},
    {0x1188A4D,kPadAbSig,sizeof(kPadAbSig)}};

// --- Game objects (MSVC std::shared_ptr / weak_ptr / string as EDF.dll builds them) ---
struct ControlBlock { void** vtable; volatile long uses,weaks; };
struct Shared { void* object; ControlBlock* control; };
void Release(Shared& p) noexcept {
    ControlBlock* const c=p.control;
    p=Shared{};
    if(c && InterlockedDecrement(&c->uses)==0) {
        reinterpret_cast<void(__fastcall*)(ControlBlock*)>(c->vtable[0])(c);          // _Destroy
        if(InterlockedDecrement(&c->weaks)==0)reinterpret_cast<void(__fastcall*)(ControlBlock*)>(c->vtable[1])(c);   // _Delete_this
    }
}
// std::wstring: at most 7 characters in place (every element name room_ui_logic.h makes), else a pointer (capacity > 7:
// the callee only reads it; 0x7F63E0 copies the path first).
struct WideString { union { wchar_t text[8]; const wchar_t* ptr; }; std::uint64_t size,capacity; };
WideString Wide(const wchar_t* s) noexcept {
    WideString w{};
    w.size=std::wcslen(s);
    if(w.size<=7){std::memcpy(w.text,s,w.size*sizeof(wchar_t));w.capacity=7;}
    else{w.ptr=s;w.capacity=w.size;}
    return w;
}

using UpdateFn=std::uint64_t(__fastcall*)(unsigned char*,void*);
using GoneFn=void*(__fastcall*)(unsigned char*,unsigned);
using GetInputFn=void*(__fastcall*)(unsigned char*,void*);
using FindIndexFn=int(__fastcall*)(unsigned char*,const WideString*);
using AtFn=Shared*(__fastcall*)(unsigned char*,Shared*,int);
using SetTextFn=void(__fastcall*)(void*,const wchar_t*);
using FindFn=Shared*(__fastcall*)(unsigned char*,Shared*,const WideString*);
using TextureSkinFn=Shared*(__fastcall*)(Shared*,Shared*);
using SetTextureFn=bool(__fastcall*)(void*,const WideString*);
using CreateButtonFn=Shared*(__fastcall*)(unsigned char*,Shared*,Shared*,const wchar_t*);
using ScreenWeakFn=Shared*(__fastcall*)(unsigned char*,Shared*);
using NativeClickFn=void(__fastcall*)(unsigned char*,void*);
struct MemberClick { NativeClickFn call; std::int32_t adjust; std::uint32_t padding; };
static_assert(sizeof(MemberClick)==16,"the native callback copies one MSVC member function pointer");
using MakeClickFn=Shared*(__fastcall*)(Shared*,Shared*,const MemberClick*);
using AddActionFn=void(__fastcall*)(void*,const wchar_t*,const Shared*);
using LockWeakFn=Shared*(__fastcall*)(Shared*,const Shared*);
using SetButtonTextFn=void(__fastcall*)(void*,const wchar_t*,const void*);
using EnableComponentFn=void(__fastcall*)(void*,bool);
using HasFocusFn=bool(__fastcall*)(void*);
using FocusComponentFn=void(__fastcall*)(void*,bool);

struct Screen { const char* name; unsigned slot,stock,goneSlot,goneStock; bool room; UpdateFn next; GoneFn gone; bool ok; };
Screen screens[2]={{"room",kRoomSlot,kRoomUpdate,kRoomGoneSlot,kRoomGone,true,nullptr,nullptr,false},
                   {"hq",kHqSlot,kHqUpdate,kHqGoneSlot,kHqGone,false,nullptr,nullptr,false}};
bool nativeEntryOk=false;
bool padOk=false;   // the game's pad table checked (else the keyboard alone)

// What was last written into each element of the screen now (the calls reload a texture / re-lay a text: only when it
// changes). Invalid when the screen object changes and when the menu opens: everything written afresh.
enum Slot : int { kSlotRoot, kSlotHead, kSlotHint, kSlotHelp2, kSlotHelp3, kSlotEntry, kSlotUnit, kSlotBox=kSlotUnit+kRows, kSlotText=kSlotBox+kRows,
                  kSlotNote=kSlotText+kRows, kSlotStatus=kSlotNote+kRows, kSlotNativeDesc=kSlotStatus+kStatusLines, kSlotCount };
enum class RootState : std::uint8_t { unknown, found, missing };
// The texture-skinned elements: the panel, each row's two chips and its icon.
enum TexSlot : int { kTexBack, kTexChip, kTexIcon=kTexChip+2*kRows, kTexCount=kTexIcon+kRows };
struct Written {
    std::uint64_t key[kSlotCount];     // texts and shown flags: Hash
    std::uint64_t tex[kTexCount];      // a textured element's (path, tint, shown): Hash
    const wchar_t* bound[kTexCount];   // the texture it was given (paths are the constants room_ui_logic.h names)
    RootState root;                    // whether this screen's layout has the menu (vcRoot)
    bool valid;
};

// --- The menu's state: the UI thread's alone ---
struct State {
    unsigned char* self=nullptr;   // the screen object the menu belongs to now
    bool room=false;
    rm::Menu menu{};
    rm::Keys held{};
    NativeEntry nativeEntry{};
    const wchar_t* nativeLabel=nullptr;
    bool nativeFocused=false;
    Gate gate{};
    bool previewShown=false;
    roompreview::PreviewSpec preview{};
    wchar_t saveNote[kLineMost]{};
    bool saveWarn=false;
    bool missingLogged=false;
    bool drew=false;               // the menu was drawn on this screen object (off: taken back, else never touched)
    Switches switches{};
    GenerateStatus gen{};
    ULONGLONG genAt=0;
    rm::UnitSpec specs[rm::kMostUnits]{};
    int specCount=0;
    Written written{};
};
State st;
rm::SaveSet saveSet;   // big: not on the stack
// MissionStart (room_ui.h ResetRoomUi) asks; the next screen frame (the UI thread: room_sync.h's and room_preview.h's
// contract) does it.
std::atomic<bool> resetAsked{false};
// The screen object the menu follows (st.self, published for the destructor hooks, whatever thread runs them) and whether
// its destructor has run since: the next frame forgets it even when the new screen sits at the same address.
std::atomic<unsigned char*> followed{nullptr};
std::atomic<bool> followedGone{false};

void NoteSave(Tx t,const wchar_t* arg,bool warn) noexcept {
    _snwprintf_s(st.saveNote,_TRUNCATE,hudtext::Tr(t),arg ? arg : L"");
    st.saveWarn=warn;
}

// --- Input ---
bool KeyDown(int vk) noexcept { return vk>0 && (edf::AsyncKey(vk)&0x8000)!=0; }
bool Foreground() noexcept {
    const HWND w=edf::ForegroundWindow();
    DWORD pid=0;
    if(w)GetWindowThreadProcessId(w,&pid);
    return pid==GetCurrentProcessId();
}
Raw ReadRaw() noexcept {
    Raw r;
    r.foreground=Foreground() && !MapHoldsKeys();   // the map never opens over the room / HQ: the rule every key reader keeps
    if(!r.foreground)return r;
    r.toggleKey=KeyDown(Cfg().roomLoadoutKey);
    r.up=KeyDown(VK_UP);r.down=KeyDown(VK_DOWN);r.left=KeyDown(VK_LEFT);r.right=KeyDown(VK_RIGHT);
    r.enter=KeyDown(VK_RETURN);r.escape=KeyDown(VK_ESCAPE);r.insert=KeyDown(VK_INSERT);r.del=KeyDown(VK_DELETE);
    r.home=KeyDown(VK_HOME);
    r.padOk=padOk;
    if(padOk)__try {
        r.pad=PadBits([](int slot,std::uint32_t offset) -> const unsigned char* {
            return *reinterpret_cast<const unsigned char* const*>(image+kPadTable+slot*kPadSlotSize+offset);
        });
    } __except(EXCEPTION_EXECUTE_HANDLER){ r.padOk=false;r.pad=0; }
    return r;
}
void ToggleName(wchar_t* out,int size) noexcept {
    const int vk=Cfg().roomLoadoutKey;
    out[0]=0;
    if(vk>=VK_F1 && vk<=VK_F24)_snwprintf_s(out,static_cast<std::size_t>(size),_TRUNCATE,L"F%d",vk-VK_F1+1);
    else if(vk>0) {
        const UINT scan=MapVirtualKeyW(static_cast<UINT>(vk),MAPVK_VK_TO_VSC);
        if(!scan || GetKeyNameTextW(static_cast<LONG>(scan<<16),out,size)<=0)_snwprintf_s(out,static_cast<std::size_t>(size),_TRUNCATE,L"0x%X",vk);
    }
    wchar_t pad[96];
    const auto combo=static_cast<std::uint16_t>(Cfg().roomLoadoutButton);
    PadComboName(PadToggleAvailable(padOk,combo) ? combo : 0,pad,std::size(pad));
    if(pad[0]){const std::size_t used=std::wcslen(out);_snwprintf_s(out+used,static_cast<std::size_t>(size)-used,_TRUNCATE,L"%ls%ls",used ? L" / " : L"",pad);}
}

bool ToggleAvailable() noexcept {
    return Cfg().roomLoadoutKey>0 || PadToggleAvailable(padOk,static_cast<std::uint16_t>(Cfg().roomLoadoutButton));
}

// --- Reading the ini (room_menu.h IniRead) ---
std::size_t IniRead(void*,const wchar_t* key,wchar_t* out,std::size_t capacity) noexcept {
    const wchar_t* path=ConfigPath();
    if(!out || !capacity)return 0;
    out[0]=0;
    if(!path || !*path)return 0;
    return GetPrivateProfileStringW(L"VehicleCrew",key,L"",out,static_cast<DWORD>(capacity),path);
}

// --- The crews' kinds (what SupportVehicleCrewWeapon / SupportAircraftCrewWeapon say now) ---
CrewKinds Kinds() noexcept {
    const SupportConfig& c=SupportCfg();
    return CrewKinds{c.vehicleCrew,c.aircraftCrew};
}

// --- The preview ---
void HidePreview() noexcept {
    if(!st.previewShown)return;
    roompreview::HidePreview();
    st.previewShown=false;
}
void FollowPreview(bool shown) noexcept {
    roompreview::PreviewSpec p{};
    if(!shown || !PreviewFor(st.menu,&p,Kinds())){HidePreview();return;}
    if(st.previewShown && SamePreview(p,st.preview))return;
    roompreview::ShowPreview(p);
    st.preview=p;st.previewShown=true;
}

// --- The menu's actions ---
void Opened() noexcept {
    st.specCount=SupportCallCount()>0 ?
        CatalogUnits(SupportCallCount(),&SupportCallKey,&SupportCallSeats,&SupportCallName,st.specs,rm::kMostUnits) :
        DefaultUnits(st.specs,rm::kMostUnits);
    rm::Load(st.menu,st.specs,st.specCount,&IniRead,nullptr);
    st.menu.pageRows=kRows;
    st.saveNote[0]=0;st.saveWarn=false;
    st.written.valid=false;   // everything written afresh once
    int refused=0;
    for(int i=0;i<st.menu.unitCount;++i)refused+=st.menu.unit[i].refused+st.menu.unit[i].crewRefused;
    Log("ROOMUI menu opened on the %s screen %p: %d units (%d values the parsers refused, shown stock)",st.room ? "room" : "HQ",
        st.self,st.menu.unitCount,refused);
}
SaveItem saveItems[2*rm::kMostUnits];   // rm::SaveSet::line's size   // one per line: a unit's preset / loadout and its crew's colours
void Save() noexcept {
    const int n=rm::CollectSave(st.menu,&saveSet) ? SaveItems(st.menu,saveSet,saveItems,static_cast<int>(std::size(saveItems))) : -1;
    if(n<0){NoteSave(Tx::roomSaveRefused,nullptr,true);Log("ROOMUI save refused: a value would not read back as edited");return;}
    // All lines in one write (all or none: a refused one leaves the file and every edit as they were).
    static LoadoutSave lines[kLoadoutSaveMost];
    static_assert(std::size(saveItems)<=kLoadoutSaveMost,"a whole save fits one SaveSupportLoadouts");
    for(int at=0;at<n;++at)lines[at]=LoadoutSave{saveItems[at].field,saveItems[at].unit,saveItems[at].value};
    wchar_t why[160]{};
    int failed=-1;
    if(!SaveSupportLoadouts(lines,n,&failed,why,std::size(why))) {
        if(failed>=0)Log("ROOMUI save refused at %ls (field %d)=%ls: %ls (nothing written, %d lines)",saveItems[failed].unit,
                         static_cast<int>(saveItems[failed].field),saveItems[failed].value,why,n);
        else Log("ROOMUI save failed: %ls (nothing written, %d lines)",why,n);
        NoteSave(Tx::roomSaveFailed,why,true);
        return;   // every line stays edited
    }
    for(int at=0;at<n;++at)MarkItemSaved(st.menu,saveItems[at]);
    int units=0;
    for(int at=0;at<n;++at)units+=at==0 || saveItems[at].unitIndex!=saveItems[at-1].unitIndex;
    rm::MarkSaved(st.menu);
    st.menu.open=true;   // stays up on the unit list: the generator's progress is shown
    _snwprintf_s(st.saveNote,_TRUNCATE,hudtext::Tr(Tx::roomSaved),units);
    st.saveWarn=false;
    why[0]=0;
    if(!StartVariantGeneration(why,std::size(why))) {
        Log("ROOMUI generator not started: %ls",why);
        NoteSave(Tx::roomGenStartFailed,why,true);
    }
    st.genAt=0;   // read its status now
    Log("ROOMUI saved %d units (%d lines), generator started",units,n);
}
void Act(rm::Act a) noexcept {
    switch(a) {
    case rm::Act::opened: Opened(); break;
    case rm::Act::save: Save(); break;
    case rm::Act::closed: Log("ROOMUI menu closed"); break;
    default: break;
    }
}

// --- The screen object changed (another room, the HQ, a new instance, one ended by its destructor): its menu goes ---
// The old object is never touched (it may be gone): nothing drawn is taken back from it.
// MissionStart drops mission/menu state but does not prove the native room object died: online rooms can be retained.
// Keep its native button/action identity until Follow observes a different object or its chained destructor.
void ForgetMenu() noexcept {
    HidePreview();
    if(st.menu.open)rm::Discard(st.menu);
    if(LeaveOnForget(st.switches))roomsync::LeaveRoomSync();
    NativeEntryInput(st.nativeEntry,false);
    st.gate=Gate{};st.missingLogged=false;st.written.valid=false;st.drew=false;
}
void Forget() noexcept {
    ForgetMenu();
    st.self=nullptr;st.nativeEntry=NativeEntry{};st.nativeLabel=nullptr;st.nativeFocused=false;
    followed.store(nullptr);
    followedGone.store(false);
}
void Follow(unsigned char* self,bool room) noexcept {
    if(followedGone.exchange(false) && st.self) {
        Log("ROOMUI screen %p (%s) ended (its destructor ran): forgotten",st.self,st.room ? "room" : "HQ");
        Forget();
    }
    if(self==st.self)return;
    if(st.self)Log("ROOMUI screen %p (%s) replaced by %p (%s)",st.self,st.room ? "room" : "HQ",self,room ? "room" : "HQ");
    Forget();
    st.self=self;st.room=room;
    followed.store(self);
}
// A screen's destructor (any thread): the one followed is marked gone; nothing else is touched here.
void NoteGone(unsigned char* self) noexcept {
    if(self && followed.load()==self)followedGone.store(true);
}

// --- Drawing ---
std::uint64_t Hash(const wchar_t* text,std::uint32_t colour,bool shown) noexcept {
    std::uint64_t h=0xCBF29CE484222325ull;
    const auto mix=[&](std::uint64_t v){h^=v;h*=0x100000001B3ull;};
    for(const wchar_t* t=text ? text : L"";*t;++t)mix(static_cast<std::uint64_t>(*t));
    mix(colour);mix(shown ? 0x5A : 0xA5);
    return h;
}
// Every element unknown: the next frame writes each once (the root looked up again).
void Validate() noexcept {
    if(st.written.valid)return;
    for(auto& k:st.written.key)k=~0ull;
    for(auto& k:st.written.tex)k=~0ull;
    for(auto& p:st.written.bound)p=nullptr;
    st.written.root=RootState::unknown;
    st.written.valid=true;
}
// True (and remembered) when `key` is not what slot `i` was last given.
bool Changed(int i,std::uint64_t key) noexcept {
    if(st.written.key[i]==key)return false;
    st.written.key[i]=key;
    return true;
}

// The component `name` (a reference held: Release it).
Shared Find(unsigned char* self,const wchar_t* name) noexcept {
    Shared s{};
    const WideString n=Wide(name);
    __try { reinterpret_cast<FindFn>(image+kFind)(self,&s,&n); } __except(EXCEPTION_EXECUTE_HANDLER){ s=Shared{}; }
    return s;
}
void Show(void* component,bool shown) noexcept {
    __try { static_cast<unsigned char*>(component)[kShown]=shown ? 1 : 0; } __except(EXCEPTION_EXECUTE_HANDLER){}
}
// The shown flag of `name`; false when the layout has no such element.
bool SetShown(unsigned char* self,const wchar_t* name,bool shown) noexcept {
    Shared c=Find(self,name);
    const bool found=c.object!=nullptr;
    if(found)Show(c.object,shown);
    Release(c);
    return found;
}
// Called by the game's own weak-screen member callback, after native Button mouse/keyboard/pad activation.
// Only queue here: changing the preview/menu while Component::Click is traversing its action list would be reentrant.
void __fastcall NativeEntryClicked(unsigned char* self,void*) noexcept {
    const bool current=self==st.self && followed.load()==self && !followedGone.load() &&
                       Cfg().enabled && Cfg().roomLoadout && !st.menu.open;
    NativeEntryClick(st.nativeEntry,current);
}
void __fastcall NativeEntryFocused(unsigned char* self,void*) noexcept {
    if(self==st.self && followed.load()==self && !followedGone.load()) {
        st.nativeFocused=true;
        st.written.key[kSlotNativeDesc]=~0ull;
    }
}
void __fastcall NativeEntryUnfocused(unsigned char* self,void*) noexcept {
    if(self==st.self && followed.load()==self && !followedGone.load())st.nativeFocused=false;
}
bool BindNativeEntryAction(unsigned char* self,void* button,const wchar_t* event,NativeClickFn callback) noexcept {
    Shared weak{},action{};
    reinterpret_cast<ScreenWeakFn>(image+kScreenWeak)(self,&weak);
    if(!weak.control)return false;
    const MemberClick member{callback,0,0};
    reinterpret_cast<MakeClickFn>(image+kMakeClick)(&action,&weak,&member); // consumes weak, owns native weak lifetime
    const bool bound=action.object && action.control;
    if(bound)reinterpret_cast<AddActionFn>(image+kAddAction)(button,event,&action); // copies, does not consume
    Release(action);
    return bound;
}
void MoveNativeEntryFocus(unsigned char* self) noexcept {
    const wchar_t* names[2]={st.room ? L"Button_Mission" : L"ButtonGotoSelectMission",
                             st.room ? L"Button_Exit" : L"ButtonGotoTitle"};
    for(const wchar_t* name:names) {
        Shared target=Find(self,name);
        const bool enabled=target.object && target.control && static_cast<const unsigned char*>(target.object)[kComponentEnabled];
        if(enabled)reinterpret_cast<FocusComponentFn>(image+kFocusComponent)(target.object,true);
        Release(target);
        if(enabled)return;
    }
}
void NativeEntryVisible(unsigned char* self,void* button,bool visible) noexcept {
    reinterpret_cast<EnableComponentFn>(image+kEnableComponent)(button,visible);
    if(!visible) {
        // Parent navigation precedes its children's pending->current update. Commit *our* button's disable now:
        // otherwise a hidden entry can still be selected for one frame. Showing uses the native transition path.
        static_cast<unsigned char*>(button)[kComponentEnabled]=0;
        if(reinterpret_cast<HasFocusFn>(image+kHasFocus)(button))MoveNativeEntryFocus(self);
    }
    Show(button,visible);
}
void EnsureNativeEntry(unsigned char* self) noexcept {
    if(st.nativeEntry.attempted || !nativeEntryOk)return;
    st.nativeEntry.attempted=true;
    // Old/foreign layouts can keep using the hotkey. Never create a button outside our installed layout contract.
    Shared root=Find(self,kRootName);
    const bool installed=root.object!=nullptr;
    Release(root);
    if(!installed)return;
    Shared parent=Find(self,st.room ? L"ButtonsGroup" : L"LeftButtonsBase2");
    if(!parent.object || !parent.control){Release(parent);return;}
    InterlockedIncrement(&parent.control->uses);
    Shared parentCopy=parent,button{};
    reinterpret_cast<CreateButtonFn>(image+kCreateButton)(self,&button,&parentCopy,kNativeEntryName); // consumes copy
    Release(parent);
    if(!button.object || !button.control){Release(button);Log("ROOMUI native entry absent: reinstall layouts; hotkey remains available");return;}
    // Disable before binding too: a failed weak/action allocation must never leave a hidden focusable Button behind.
    NativeEntryVisible(self,button.object,false);
    st.nativeEntry.ready=BindNativeEntryAction(self,button.object,L"Click",&NativeEntryClicked);
    if(st.nativeEntry.ready) {
        BindNativeEntryAction(self,button.object,L"focus",&NativeEntryFocused);
        BindNativeEntryAction(self,button.object,L"kill_focus",&NativeEntryUnfocused);
    }
    Release(button);
    Log("ROOMUI native %s menu entry=%d (stock Button Click/focus, native weak-screen lifetime)",
        st.room ? "room" : "HQ",st.nativeEntry.ready);
}
void NativeEntryDisplay(unsigned char* self,bool shown) noexcept {
    if(!st.nativeEntry.ready)return;
    Shared button=Find(self,kNativeEntryName);
    if(button.object && button.control) {
        const wchar_t* label=Title(false);
        if(st.nativeLabel!=label) {
            Shared text{};
            reinterpret_cast<LockWeakFn>(image+kLockWeak)(&text,
                reinterpret_cast<const Shared*>(static_cast<unsigned char*>(button.object)+kButtonLabel));
            if(text.object) {
                // Button owns ui::TextField, not HUiTextField: its native SetText accepts a nullable font override.
                reinterpret_cast<SetButtonTextFn>(image+kSetButtonText)(text.object,label,nullptr);
                st.nativeLabel=label;
            }
            Release(text);
        }
        NativeEntryVisible(self,button.object,shown && st.nativeLabel!=nullptr);
    }
    Release(button);
}
// A TextField's text, font colour and shown flag (the canonical call: index by name, then the TextField at it, 0x886800).
// No text: the empty text (what SetText gives a null one: 0x86369F), as the layout file has it.
void SetText(unsigned char* self,int slot,const wchar_t* name,const wchar_t* text,std::uint32_t rgba=kWhite) noexcept {
    const bool shown=text && *text;
    if(!Changed(slot,Hash(text,rgba,shown)))return;
    const WideString n=Wide(name);
    Shared field{},node{};
    bool done=false;
    __try {
        const int index=reinterpret_cast<FindIndexFn>(image+kFindIndex)(self,&n);
        if(index>=0) {
            reinterpret_cast<AtFn>(image+kTextAt)(self,&field,index);
            reinterpret_cast<AtFn>(image+kComponentAt)(self,&node,index);
        }
        // SetText does nothing while the field has no font yet (+0x228 null: its first instruction): not written then.
        done=field.object && *reinterpret_cast<void* const*>(static_cast<unsigned char*>(field.object)+kTextFont);
        if(done) {
            reinterpret_cast<SetTextFn>(image+kSetText)(field.object,shown ? text : L"");
            float colour[4];
            ChipColour(rgba,colour);
            std::memcpy(static_cast<unsigned char*>(field.object)+kFontColour,colour,sizeof(colour));
        }
        if(node.object)Show(node.object,shown);
    } __except(EXCEPTION_EXECUTE_HANDLER){ done=false; }
    Release(field);
    Release(node);
    if(!done)st.written.key[slot]=~0ull;   // tried again next frame
}
// A texture-skinned element (the panel, a chip, an icon: the game's own icon call 0x8C4256: by name, its Skin_Texture
// 0x953D80, the texture 0x7F63E0): `path` bound once, tinted `rgba` (both tint float4s), and shown. Not shown: the tint
// transparent and the element hidden (a texture bound stays bound: nothing draws it). A texture the game cannot load
// leaves the element hidden and is not tried again until what it should show changes.
void SetTextured(unsigned char* self,int t,const wchar_t* name,const wchar_t* path,std::uint32_t rgba,bool shown) noexcept {
    shown=shown && path;
    const std::uint64_t key=shown ? Hash(path,rgba,true) : Hash(nullptr,0,false);
    if(st.written.tex[t]==key)return;
    Shared c=Find(self,name);
    const bool found=c.object!=nullptr;
    bool reached=!found,drawn=false;   // no such element: nothing to retry
    __try {
        if(found && c.control) {
            InterlockedIncrement(&c.control->uses);   // the callee's by-value copy: it releases it
            Shared copy=c,tex{};
            reinterpret_cast<TextureSkinFn>(image+kTextureSkin)(&tex,&copy);
            if(tex.object) {
                reached=true;
                if(shown && st.written.bound[t]!=path) {
                    const WideString w=Wide(path);
                    if(reinterpret_cast<SetTextureFn>(image+kSetTexture)(tex.object,&w))st.written.bound[t]=path;
                    else Log("ROOMUI texture %ls not loaded into %ls (the element stays hidden)",path,name);
                }
                drawn=shown && st.written.bound[t]==path;
                float colour[4];
                ChipColour(drawn ? rgba : 0u,colour);
                unsigned char* const skin=static_cast<unsigned char*>(tex.object);
                std::memcpy(skin+kTint,colour,sizeof(colour));
                std::memcpy(skin+kTintFrom,colour,sizeof(colour));
            }
            Release(tex);
        }
        if(found)Show(c.object,drawn);
    } __except(EXCEPTION_EXECUTE_HANDLER){ reached=false; }
    Release(c);
    if(reached)st.written.tex[t]=key;
}

// Row `i` of the row column; nullptr: the row empty (every part emptied and hidden).
void DrawRow(unsigned char* self,int i,const ViewRow* r) noexcept {
    wchar_t name[kNameMost],line[kLineMost];
    const Look l=LookOf(r);
    RowName(i,Part::box,name);
    if(Changed(kSlotBox+i,l.shown ? 1 : 2))SetShown(self,name,l.shown);
    line[0]=0;
    if(r)RowLine(*r,line,kLineMost);
    RowName(i,Part::text,name);SetText(self,kSlotText+i,name,line,RowColour(r));
    line[0]=0;
    if(r)NoteLine(*r,line,kLineMost);
    RowName(i,Part::note,name);SetText(self,kSlotNote+i,name,line,RowColour(r));
    RowName(i,Part::primary,name);SetTextured(self,kTexChip+2*i,name,kWhiteTexture,l.primaryRgba,l.primary);
    RowName(i,Part::secondary,name);SetTextured(self,kTexChip+2*i+1,name,kWhiteTexture,l.secondaryRgba,l.secondary);
    RowName(i,Part::icon,name);SetTextured(self,kTexIcon+i,name,l.icon,kIconColour,l.icon!=nullptr);
}
// Everything the menu sets taken back: texts empty, tints transparent, elements hidden (the layout's own look: nothing).
void ClearMenu(unsigned char* self) noexcept {
    wchar_t name[kNameMost];
    SetText(self,kSlotHead,kHeadName,nullptr);
    SetText(self,kSlotHint,kHintName,nullptr);
    SetText(self,kSlotHelp2,kHelp2Name,nullptr);
    SetText(self,kSlotHelp3,kHelp3Name,nullptr);
    SetTextured(self,kTexBack,kBackName,kWhiteTexture,kPanelColour,false);
    for(int i=0;i<kRows;++i){UnitName(i,name);SetText(self,kSlotUnit+i,name,nullptr);DrawRow(self,i,nullptr);}
    for(int i=0;i<kStatusLines;++i){StatusName(i,name);SetText(self,kSlotStatus+i,name,nullptr);}
}
// The root's shown flag, and while the menu is not shown everything it set taken back; false when this screen's layout
// has no menu elements (not looked up again until the screen object changes or the menu opens).
bool Draw(unsigned char* self,bool shown,bool entry=false) noexcept {
    Validate();
    const bool rootShown=shown || entry;
    if(st.written.root==RootState::missing)return false;
    if(Changed(kSlotRoot,rootShown ? 1 : 2) || st.written.root==RootState::unknown) {
        if(!SetShown(self,kRootName,rootShown)) {
            if(!st.missingLogged)Log("ROOMUI the %s screen's layout has no menu elements (Mods/UI not installed by the installer, "
                                     "or another mod's layout): the menu is not drawn there",st.room ? "room" : "HQ");
            st.missingLogged=true;
            st.written.root=RootState::missing;
            return false;
        }
        st.written.root=RootState::found;
    }
    if(!shown)ClearMenu(self);
    if(!entry)SetText(self,kSlotEntry,kEntryName,nullptr);
    return true;
}
void DrawMenu(unsigned char* self) noexcept {
    rm::View v=rm::Snapshot(st.menu);
    DressCrewRows(v,st.menu,Kinds());
    SetTextured(self,kTexBack,kBackName,kWhiteTexture,kPanelColour,true);
    SetText(self,kSlotHead,kHeadName,Title(v.dirty));
    wchar_t name[kNameMost],line[kLineMost];
    for(int i=0;i<kRows;++i) {
        UnitName(i,name);
        if(i<v.units.count){UnitLine(v.units.row[i],line,kLineMost);SetText(self,kSlotUnit+i,name,line,RowColour(&v.units.row[i]));}
        else SetText(self,kSlotUnit+i,name,nullptr);
    }
    for(int i=0;i<kRows;++i)DrawRow(self,i,i<v.rows.count ? &v.rows.row[i] : nullptr);
    // The status lines: the client's note, the save, the generator (read twice a second), the room's sync.
    const ULONGLONG now=GetTickCount64();
    if(!st.genAt || now-st.genAt>500){st.gen=VariantGenerationStatus();st.genAt=now;}
    roomsync::SyncLine sync[kStatusLines]{};
    const int syncCount=st.room ? roomsync::RoomSyncLines(sync,kStatusLines) : 0;
    const Status s=ComposeStatus(st.room && !OnlineHostOnly(),st.saveNote,st.saveWarn,&st.gen,sync,syncCount);
    for(int i=0;i<kStatusLines;++i) {
        StatusName(i,name);
        SetText(self,kSlotStatus+i,name,i<s.count ? s.text[i] : nullptr,StatusColour(i<s.count && s.warn[i]));
    }
    wchar_t key[128];
    ToggleName(key,128);
    HelpLine(v.level,0,key,line,kLineMost);SetText(self,kSlotHint,kHintName,line);
    HelpLine(v.level,1,key,line,kLineMost);SetText(self,kSlotHelp2,kHelp2Name,line);
    HelpLine(v.level,2,key,line,kLineMost);SetText(self,kSlotHelp3,kHelp3Name,line);
}

// --- One frame of either screen ---
// Off (Enabled / RoomLoadout turned 0 since the load): the screen's own update with its own ctx, nothing read; the menu
// dropped, the preview put back, and what the menu drew on this screen taken back (write-on-change: once).
std::uint64_t Off(Screen& screen,unsigned char* self,void* ctx) noexcept {
    if(st.menu.open){rm::Discard(st.menu);Log("ROOMUI menu closed: RoomLoadout / Enabled turned off");}
    st.held=rm::Keys{};st.gate=Gate{};
    NativeEntryInput(st.nativeEntry,false);NativeEntryDisplay(self,false);
    HidePreview();
    roompreview::TickPreview(false);
    const std::uint64_t result=screen.next(self,ctx);
    if(st.drew)Draw(self,false);
    return result;
}
std::uint64_t Frame(Screen& screen,unsigned char* self,void* ctx) noexcept {
    if(resetAsked.exchange(false)) {
        Log("ROOMUI a mission started since the last screen frame: the room is left, the menu dropped");
        ForgetMenu();
        st.held=rm::Keys{};
    }
    Follow(self,screen.room);
    PvpRoomTick();       // capability advertisement only; native team changes stay on the game-thread PvpFrame
    Outfit41RoomTick();   // the EDF4.1 outfit told to the room whatever the menu's switches (outfit41.cpp)
    Edf5OnlineRoomTick(); // this machine's word that it creates every player of an EDF5/4.1 mission (edf5online_room.cpp)
    const bool on=Cfg().enabled && Cfg().roomLoadout;
    const SwitchPlan sw=PlanSwitches(st.switches,on,Cfg().roomPreSync,screen.room);
    if(sw.leave) {   // a switch turned off: withdrawn, and the next mission does not take the host's list either
        roomsync::LeaveRoomSync();
        roomsync::ForgetRoomHostList();
        Log("ROOMSYNC stopped (Enabled / RoomLoadout / RoomPreSync off): this machine's keys withdrawn, the host's list dropped");
    }
    if(sw.pass)return Off(screen,self,ctx);
    hudtext::Use(hudtext::Resolve(Cfg().hudLanguage,GameTextLanguage()));
    bool hasInput=false;
    __try { hasInput=reinterpret_cast<GetInputFn>(image+kGetInput)(self,ctx)!=nullptr; } __except(EXCEPTION_EXECUTE_HANDLER){ hasInput=false; }
    EnsureNativeEntry(self);
    const rm::Keys down=KeysDown(ReadRaw(),static_cast<std::uint16_t>(Cfg().roomLoadoutButton));
    const rm::Keys press=rm::Presses(st.held,down);
    const Plan plan=PlanFrame(hasInput,st.menu.open);
    if(plan.step)Act(rm::Step(st.menu,press));
    const bool take=TakeInput(st.gate,st.menu.open,AnyDown(down));
    NativeEntryInput(st.nativeEntry,hasInput && !st.menu.open && !st.gate.latched && Foreground());
    NativeEntryDisplay(self,st.nativeEntry.enabled);
    std::uint64_t result;
    if(take && ctx) {
        alignas(16) unsigned char copy[kCtxSize];
        std::memcpy(copy,ctx,kCtxSize);
        std::memset(copy+kCtxInput,0,sizeof(void*));
        result=screen.next(self,copy);
    } else result=screen.next(self,ctx);
    if(TakeNativeEntry(st.nativeEntry,self==st.self && !followedGone.load() && !st.menu.open)) {
        rm::Keys clicked;clicked.toggle=true;
        Act(rm::Step(st.menu,clicked));
        st.gate.latched=true;
        NativeEntryInput(st.nativeEntry,false);
        NativeEntryDisplay(self,false);
    }
    if(st.nativeFocused && hasInput && !st.menu.open) {
        wchar_t key[128],line[kLineMost];
        ToggleName(key,128);
        if(key[0])EntryLine(key,line,kLineMost);
        else wcscpy_s(line,Title(false)); // the native menu remains available when both shortcut bindings are disabled
        SetText(self,kSlotNativeDesc,L"MenuDescText",line);
    }
    if(sw.tick)roomsync::TickRoomSync();
    const Plan display=PlanFrame(hasInput,st.menu.open,ToggleAvailable(),!st.gate.latched);
    FollowPreview(display.show);
    roompreview::TickPreview(display.show);
    if(Draw(self,display.show,display.entry)) {
        if(display.show)DrawMenu(self);
        if(display.entry) {
            wchar_t key[128],line[kLineMost];
            ToggleName(key,128);EntryLine(key,line,kLineMost);
            SetText(self,kSlotEntry,kEntryName,line);
        }
        if(display.show || display.entry)st.drew=true;
    }
    return result;
}
std::uint64_t __fastcall RoomHook(unsigned char* self,void* ctx) { return Frame(screens[0],self,ctx); }
std::uint64_t __fastcall HqHook(unsigned char* self,void* ctx) { return Frame(screens[1],self,ctx); }
void* __fastcall RoomGoneHook(unsigned char* self,unsigned flags) { NoteGone(self); return screens[0].gone(self,flags); }
void* __fastcall HqGoneHook(unsigned char* self,unsigned flags) { NoteGone(self); return screens[1].gone(self,flags); }

bool AllMatch(const Sig* sigs,std::size_t n,const char* what,const char* then) noexcept {
    for(std::size_t i=0;i<n;++i)
        if(!Matches(sigs[i].rva,sigs[i].bytes,sigs[i].size)) {
            Log("HOOK roomui REFUSED: %s: the code at EDF+0x%X is not as expected (%s)",what,sigs[i].rva,then);
            return false;
        }
    return true;
}
// An older EDF6Coop refuses a room slot another plugin already hooked. Member-attribute ABI v1 arrived together
// with Coop's chaining support (#33): a disk image exposing it can load after us. Probe its exports without executing
// or registering it; the real mod loader must still run its initialization. Unknown/old builds retain the guard.
bool CoopStillToLoad() noexcept {
    wchar_t path[MAX_PATH];
    const wchar_t* ini=ConfigPath();
    if(!ini || wcscpy_s(path,ini))return false;
    wchar_t* slash=wcsrchr(path,L'\\');
    if(!slash || wcscpy_s(slash+1,static_cast<std::size_t>(std::end(path)-(slash+1)),L"EDF6Coop.dll"))return false;
    return GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES && !GetModuleHandleW(L"EDF6Coop.dll") &&
           !HasDllExport(path,"EDF6CoopGetMemberAttributeApi");
}
// The screen's destructor chained first (whatever its slot holds now): without it a new screen at a dead one's address
// would inherit its menu, so a screen whose destructor cannot be chained gets no menu.
bool ChainGone(Screen& s,void* hook) noexcept {
    void** const slot=reinterpret_cast<void**>(image+s.goneSlot);
    void* const current=Readable(slot,8) ? *slot : nullptr;
    if(!current){Log("HOOK roomui REFUSED: the %s screen's destructor slot is not readable / empty",s.name);return false;}
    if(current!=image+s.goneStock)Log("HOOK roomui: the %s screen's destructor chains onto %p (another plugin)",s.name,current);
    s.gone=reinterpret_cast<GoneFn>(current);
    if(PatchVtableSlot(slot,current,hook))return true;
    Log("HOOK roomui REFUSED: the %s screen's destructor slot could not be patched",s.name);
    return false;
}
bool Chain(Screen& s,void* hook,void* goneHook) noexcept {
    void** const slot=reinterpret_cast<void**>(image+s.slot);
    if(!Readable(slot,8)){Log("HOOK roomui REFUSED: the %s screen's OnUpdate slot is not readable",s.name);return false;}
    void* const current=*slot;
    if(!current){Log("HOOK roomui REFUSED: the %s screen's OnUpdate slot is empty",s.name);return false;}
    if(s.room && current==image+s.stock && CoopStillToLoad()) {
        Log("HOOK roomui REFUSED: the room: an older EDF6Coop.dll is beside us but not loaded yet (it would refuse a chained room slot); "
            "the room menu is off this run, the HQ's is not");
        return false;
    }
    if(current!=image+s.stock)Log("HOOK roomui: the %s screen's OnUpdate chains onto %p (another plugin: EDF6Coop)",s.name,current);
    if(!ChainGone(s,goneHook))return false;
    s.next=reinterpret_cast<UpdateFn>(current);
    s.ok=PatchVtableSlot(slot,current,hook);
    return s.ok;
}
}  // namespace

bool InstallRoomUi() noexcept {
    constexpr const char* kUntouched="both screens stay as they are";
    // RoomLoadout=0 at load: nothing is installed, nothing checked (crew.h: both screens untouched; the room pre-sync never
    // runs). Turning it on takes a restart. Enabled (the plugin's live switch) and RoomLoadout turned 0 later: the frames
    // pass straight through (Off).
    if(!Cfg().roomLoadout) {
        Log("HOOK roomui off (RoomLoadout=0 at load): nothing installed, both screens untouched, no room pre-sync");
        return false;
    }
    if(!AllMatch(kDrawSigs,std::size(kDrawSigs),"the layout calls",kUntouched) ||
       !AllMatch(kScreenSigs,std::size(kScreenSigs),"the screens' updates",kUntouched))
        return false;
    nativeEntryOk=AllMatch(kNativeMenuSigs,std::size(kNativeMenuSigs),"the native menu button contracts","hotkey menu still available");
    padOk=AllMatch(kPadSigs,std::size(kPadSigs),"the pad table","the menu: keyboard only");
    Log("HOOK roomui preview=%d (stage 7; 0: the menu runs without the 3D preview)",roompreview::InstallPreviewHooks());
    const bool room=Chain(screens[0],reinterpret_cast<void*>(&RoomHook),reinterpret_cast<void*>(&RoomGoneHook));
    const bool hq=Chain(screens[1],reinterpret_cast<void*>(&HqHook),reinterpret_cast<void*>(&HqGoneHook));
    Log("HOOK roomui room=%d hq=%d (the support loadout menu in the online room and the offline HQ)",room,hq);
    return room || hq;
}

void ResetRoomUi() noexcept {
    // MissionStart's thread is not known to be the UI thread: the state is the UI thread's, and room_sync.h /
    // room_preview.h are called from it only. Asked here, done by the next screen frame (Frame).
    resetAsked.store(true);
}
}  // namespace crew
