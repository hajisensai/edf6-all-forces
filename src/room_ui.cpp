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
//      HUiTextField::SetText, the chips' colour through the ColorChipBox model skin, icons by the texture skin, every
//      element's shown flag (+0x1F8), each written only when it changes (Written). The root is hidden from the screen's
//      first frame on while the menu is not shown: the layout file shows it.
// Nothing is patched unless every address checks out (REFUSED in the log: both screens as they are). The slots are read
// and chained, never assumed stock: EDF6Coop chains the room's too.
#include "room_ui.h"
#include "crew.h"
#include "hud.h"
#include "loadout_store.h"
#include "map.h"
#include "memory.h"
#include "online_authority.h"
#include "room_preview.h"
#include "room_sync.h"
#include "room_ui_logic.h"
#include "support_call.h"
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
constexpr unsigned kGetInput=0x8DAD00;      // HUiBase::GetInput(this, ctx): ctx+8, nullptr while covered / in a transition
constexpr unsigned kFindIndex=0x839600;     // HUiLayout: (this, const std::wstring* name) -> the first index or -1
constexpr unsigned kComponentAt=0x839560;   // HUiLayout: (this, shared_ptr<ui::Component>* out, index), no cast
constexpr unsigned kTextAt=0x8859C0;        // HUiLayout: (this, shared_ptr<HUiTextField>* out, index), null if no TextField
constexpr unsigned kSetText=0x863690;       // HUiTextField::SetText(this, const wchar_t*): the literal text
constexpr unsigned kFind=0x839FD0;          // HUiLayout: (this, shared_ptr<ui::Component>* out, const std::wstring* name)
constexpr unsigned kModelParam=0x824D40;    // (weak_ptr<Skin>* by value: released by it, const wchar_t* model,
                                            //  const std::string* param, const void* value): the model skin's parameter
constexpr unsigned kTextureSkin=0x953D80;   // (shared_ptr<Skin_Texture>* out, shared_ptr<Component>* by value: released by it)
constexpr unsigned kSetTexture=0x7F63E0;    // Skin_Texture: (this, const std::wstring* path) -> bool
constexpr std::size_t kShown=0x1F8;         // a Component's shown flag (its constructor sets 1: 0x7E999C)
constexpr std::size_t kSkinPtr=0x98,kSkinCtrl=0xA0;   // a Component's skin, a weak_ptr (0x88014D)
constexpr std::size_t kFontColour=0x2B0;    // HUiTextField: font colour RGBA float4 (0x8BC68C; drawn by 0x7F890C)
constexpr std::size_t kCtxSize=0x18,kCtxInput=0x8;
constexpr unsigned kPadTable=0x2136530;     // two slots of a pointer per pad channel (0x1188315)

struct Sig { unsigned rva; const unsigned char* bytes; std::size_t size; };
// The functions called, by their first bytes, and the instructions the offsets above come from.
const unsigned char kGetInputSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x83,0xB9,0xD8,0x00,0x00};
const unsigned char kFindIndexSig[]={0x40,0x53,0x48,0x83,0xEC,0x40,0x4C,0x8B,0xC2,0x48,0x8D,0x54,0x24,0x20,0xE8,0x1D};
const unsigned char kComponentAtSig[]={0x40,0x57,0x48,0x83,0xEC,0x30,0x48,0x8B,0xFA,0x48,0x8D,0x54,0x24,0x20,0xE8,0x8D};
const unsigned char kTextAtSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x48};
const unsigned char kSetTextSig[]={0x48,0x83,0xB9,0x28,0x02,0x00,0x00,0x00,0x74,0x11,0x48,0x85,0xD2,0x75,0x07,0x48};
const unsigned char kFindSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x48};
const unsigned char kModelParamSig[]={0x48,0x89,0x5C,0x24,0x10,0x4C,0x89,0x4C,0x24,0x20,0x4C,0x89,0x44,0x24,0x18,0x48};
const unsigned char kTextureSkinSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
const unsigned char kSetTextureSig[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
// The HQ palette's chip (0x88014D): rax=[comp+98h]; rax=[comp+0A0h]; lock inc [ctrl+0Ch] (a weak ref for the callee);
// r9 = the float4, r8 = the std::string "color", rdx = L"ColorChipBox", rcx = the weak_ptr copy; call 0x824D40.
const unsigned char kChipSiteSig[]={0x48,0x8B,0x81,0x98,0x00,0x00,0x00,0x48,0x89,0x45,0xC0,0x48,0x8B,0x81,0xA0,0x00,0x00,0x00,
    0x48,0x89,0x45,0xC8,0xF0,0xFF,0x40,0x0C,0x4C,0x8D,0x4D,0x70,0x4C,0x8D,0x85,0x80,0x00,0x00,0x00,0x48,0x8D,0x15,0x57,0x0E,0xF8,0x00,
    0x48,0x8D,0x4D,0xC0,0xE8,0xBE,0x4B,0xFA,0xFF};
const unsigned char kFontColourSig[]={0x89,0x81,0xB0,0x02,0x00,0x00};                       // 0x8BC68C: mov [rcx+2B0h],eax
const unsigned char kFontDrawSig[]={0xF3,0x0F,0x10,0x9B,0xBC,0x02,0x00,0x00};               // 0x7F890C: movss xmm3,[rbx+2BCh]
const unsigned char kShowSig[]={0xC6,0x87,0xF8,0x01,0x00,0x00,0x01};                        // 0x7E999C: mov byte [rdi+1F8h],1
const Sig kDrawSigs[]={{kGetInput,kGetInputSig,sizeof(kGetInputSig)},{kFindIndex,kFindIndexSig,sizeof(kFindIndexSig)},
    {kComponentAt,kComponentAtSig,sizeof(kComponentAtSig)},{kTextAt,kTextAtSig,sizeof(kTextAtSig)},
    {kSetText,kSetTextSig,sizeof(kSetTextSig)},{kFind,kFindSig,sizeof(kFindSig)},{kModelParam,kModelParamSig,sizeof(kModelParamSig)},
    {kTextureSkin,kTextureSkinSig,sizeof(kTextureSkinSig)},{kSetTexture,kSetTextureSig,sizeof(kSetTextureSig)},
    {0x88014D,kChipSiteSig,sizeof(kChipSiteSig)},{0x8BC68C,kFontColourSig,sizeof(kFontColourSig)},
    {0x7F890C,kFontDrawSig,sizeof(kFontDrawSig)},{0x7E999C,kShowSig,sizeof(kShowSig)}};
// The screens' own OnUpdates (whatever their slots hold now, these are what the game put there).
const unsigned char kRoomUpdateSig[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
// push rbx; sub rsp,30h; mov rbx,rcx; call 0x84B490 (HUiLayout's update: focus, cancel, buttons)
const unsigned char kHqUpdateSig[]={0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0xE8,0x42,0xBD,0xFA,0xFF,0x48,0x8D};
const Sig kScreenSigs[]={{kRoomUpdate,kRoomUpdateSig,sizeof(kRoomUpdateSig)},{kHqUpdate,kHqUpdateSig,sizeof(kHqUpdateSig)}};
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
// std::string "color" (in place: at most 15 characters).
struct NarrowString { char text[16]; std::uint64_t size,capacity; };
constexpr NarrowString kColourParam{{'c','o','l','o','r'},5,15};

using UpdateFn=std::uint64_t(__fastcall*)(unsigned char*,void*);
using GetInputFn=void*(__fastcall*)(unsigned char*,void*);
using FindIndexFn=int(__fastcall*)(unsigned char*,const WideString*);
using AtFn=Shared*(__fastcall*)(unsigned char*,Shared*,int);
using SetTextFn=void(__fastcall*)(void*,const wchar_t*);
using FindFn=Shared*(__fastcall*)(unsigned char*,Shared*,const WideString*);
using ModelParamFn=void(__fastcall*)(Shared*,const wchar_t*,const NarrowString*,const void*);
using TextureSkinFn=Shared*(__fastcall*)(Shared*,Shared*);
using SetTextureFn=bool(__fastcall*)(void*,const WideString*);

struct Screen { const char* name; unsigned slot,stock; bool room; UpdateFn next; bool ok; };
Screen screens[2]={{"room",kRoomSlot,kRoomUpdate,true,nullptr,false},{"hq",kHqSlot,kHqUpdate,false,nullptr,false}};
bool padOk=false;   // the game's pad table checked (else the keyboard alone)

// What was last written into each element of the screen now (the calls reload a texture / re-lay a text: only when it
// changes). Invalid when the screen object changes and when the menu opens: everything written afresh.
enum Slot : int { kSlotRoot, kSlotHead, kSlotHint, kSlotUnit, kSlotBox=kSlotUnit+kRows, kSlotText=kSlotBox+kRows,
                  kSlotNote=kSlotText+kRows, kSlotStatus=kSlotNote+kRows, kSlotCount=kSlotStatus+kStatusLines };
struct Written {
    std::uint64_t key[kSlotCount];     // texts and shown flags: Hash
    std::uint32_t chip[kRows][2];      // a chip's RGBA, 0 hidden
    const wchar_t* icon[kRows];        // the icon's path, nullptr hidden
    bool valid;
};

// --- The menu's state: the UI thread's alone ---
struct State {
    unsigned char* self=nullptr;   // the screen object the menu belongs to now
    bool room=false;
    rm::Menu menu{};
    rm::Keys held{};
    Gate gate{};
    bool previewShown=false;
    roompreview::PreviewSpec preview{};
    wchar_t saveNote[kLineMost]{};
    bool saveWarn=false;
    bool missingLogged=false;
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

void NoteSave(Tx t,const wchar_t* arg,bool warn) noexcept {
    _snwprintf_s(st.saveNote,_TRUNCATE,hudtext::Tr(t),arg ? arg : L"");
    st.saveWarn=warn;
}

// --- Input ---
bool KeyDown(int vk) noexcept { return vk>0 && (GetAsyncKeyState(vk)&0x8000)!=0; }
bool Foreground() noexcept {
    const HWND w=GetForegroundWindow();
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
    wchar_t pad[48];
    PadComboName(static_cast<std::uint16_t>(Cfg().roomLoadoutButton),pad,std::size(pad));
    if(pad[0]){const std::size_t used=std::wcslen(out);_snwprintf_s(out+used,static_cast<std::size_t>(size)-used,_TRUNCATE,L"%ls%ls",used ? L" / " : L"",pad);}
}

// --- Reading the ini (room_menu.h IniRead) ---
std::size_t IniRead(void*,const wchar_t* key,wchar_t* out,std::size_t capacity) noexcept {
    const wchar_t* path=ConfigPath();
    if(!out || !capacity)return 0;
    out[0]=0;
    if(!path || !*path)return 0;
    return GetPrivateProfileStringW(L"VehicleCrew",key,L"",out,static_cast<DWORD>(capacity),path);
}

// --- The preview ---
void HidePreview() noexcept {
    if(!st.previewShown)return;
    roompreview::HidePreview();
    st.previewShown=false;
}
void FollowPreview(bool shown) noexcept {
    roompreview::PreviewSpec p{};
    if(!shown || !PreviewFor(st.menu,&p)){HidePreview();return;}
    if(st.previewShown && SamePreview(p,st.preview))return;
    roompreview::ShowPreview(p);
    st.preview=p;st.previewShown=true;
}

// --- The menu's actions ---
void Opened() noexcept {
    st.specCount=CatalogUnits(SupportCallCount(),&SupportCallKey,&SupportCallSeats,&SupportCallName,st.specs,rm::kMostUnits);
    rm::Load(st.menu,st.specCount ? st.specs : rm::kDefaultUnits,st.specCount ? st.specCount : rm::kDefaultUnitCount,&IniRead,nullptr);
    st.menu.pageRows=kRows;
    st.saveNote[0]=0;st.saveWarn=false;
    st.written.valid=false;   // everything written afresh once
    int refused=0;
    for(int i=0;i<st.menu.unitCount;++i)refused+=st.menu.unit[i].refused;
    Log("ROOMUI menu opened on the %s screen %p: %d units (%d values the parsers refused, shown stock)",st.room ? "room" : "HQ",
        st.self,st.menu.unitCount,refused);
}
// A unit written: what it holds now is what the ini holds.
void MarkUnitSaved(rm::Unit& u) noexcept {
    u.refused=false;
    u.squadLoaded=u.squad;u.vehicleLoaded=u.vehicle;
}
void Save() noexcept {
    SaveItem items[rm::kMostUnits];
    const int n=rm::CollectSave(st.menu,&saveSet) ? SaveItems(st.menu,saveSet,items,rm::kMostUnits) : -1;
    if(n<0){NoteSave(Tx::roomSaveRefused,nullptr,true);Log("ROOMUI save refused: a value would not read back as edited");return;}
    int at=0;
    for(int i=0;i<st.menu.unitCount && at<n;++i) {
        rm::Unit& u=st.menu.unit[i];
        if(!rm::UnitChanged(u))continue;
        wchar_t why[160]{};
        if(!SaveSupportLoadout(items[at].field,items[at].unit,items[at].value,why,std::size(why))) {
            Log("ROOMUI save %ls=%ls failed: %ls (%d of %d written)",items[at].unit,items[at].value,why,at,n);
            NoteSave(Tx::roomSaveFailed,why,true);
            return;   // the units before it are written and marked; this one and the rest stay edited
        }
        MarkUnitSaved(u);
        ++at;
    }
    rm::MarkSaved(st.menu);
    st.menu.open=true;   // stays up on the unit list: the generator's progress is shown
    _snwprintf_s(st.saveNote,_TRUNCATE,hudtext::Tr(Tx::roomSaved),n);
    st.saveWarn=false;
    wchar_t why[160]{};
    if(!StartVariantGeneration(why,std::size(why))) {
        Log("ROOMUI generator not started: %ls",why);
        NoteSave(Tx::roomGenStartFailed,why,true);
    }
    st.genAt=0;   // read its status now
    Log("ROOMUI saved %d units, generator started",n);
}
void Act(rm::Act a) noexcept {
    switch(a) {
    case rm::Act::opened: Opened(); break;
    case rm::Act::save: Save(); break;
    case rm::Act::closed: Log("ROOMUI menu closed"); break;
    default: break;
    }
}

// --- The screen object changed (another room, the HQ, a new instance): the old one's menu goes ---
void Forget(bool leftRoom) noexcept {
    HidePreview();
    if(st.menu.open)rm::Discard(st.menu);
    if(leftRoom)roomsync::LeaveRoomSync();
    st.self=nullptr;st.gate=Gate{};st.missingLogged=false;st.written.valid=false;
}
void Follow(unsigned char* self,bool room) noexcept {
    if(self==st.self)return;
    if(st.self)Log("ROOMUI screen %p (%s) replaced by %p (%s)",st.self,st.room ? "room" : "HQ",self,room ? "room" : "HQ");
    Forget(st.room && st.self);
    st.self=self;st.room=room;
}

// --- Drawing ---
std::uint64_t Hash(const wchar_t* text,std::uint32_t colour,bool shown) noexcept {
    std::uint64_t h=0xCBF29CE484222325ull;
    const auto mix=[&](std::uint64_t v){h^=v;h*=0x100000001B3ull;};
    for(const wchar_t* t=text ? text : L"";*t;++t)mix(static_cast<std::uint64_t>(*t));
    mix(colour);mix(shown ? 0x5A : 0xA5);
    return h;
}
// Every element unknown: the next frame writes each once.
void Validate() noexcept {
    if(st.written.valid)return;
    for(auto& k:st.written.key)k=~0ull;
    for(auto& c:st.written.chip)c[0]=c[1]=~0u;
    for(auto& p:st.written.icon)p=L"?";   // no path the menu uses
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
// A TextField's text, font colour and shown flag (the canonical call: index by name, then the TextField at it, 0x886800).
void SetText(unsigned char* self,int slot,const wchar_t* name,const wchar_t* text,std::uint32_t rgba=kWhite) noexcept {
    const bool shown=text && *text;
    if(!Changed(slot,Hash(text,rgba,shown)))return;
    const WideString n=Wide(name);
    Shared field{},node{};
    __try {
        const int index=reinterpret_cast<FindIndexFn>(image+kFindIndex)(self,&n);
        if(index>=0) {
            reinterpret_cast<AtFn>(image+kTextAt)(self,&field,index);
            reinterpret_cast<AtFn>(image+kComponentAt)(self,&node,index);
        }
        if(field.object) {
            reinterpret_cast<SetTextFn>(image+kSetText)(field.object,shown ? text : L" ");
            float colour[4];
            ChipColour(rgba,colour);
            std::memcpy(static_cast<unsigned char*>(field.object)+kFontColour,colour,sizeof(colour));
        }
        if(node.object)Show(node.object,shown);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    Release(field);
    Release(node);
}
// A ColorChipBox chip: the model skin's "color" (the HQ palette's call, 0x88014D) and its shown flag.
void SetChip(unsigned char* self,int row,int which,const wchar_t* name,bool shown,std::uint32_t rgba) noexcept {
    const std::uint32_t key=shown ? rgba : 0u;
    if(st.written.chip[row][which]==key)return;
    Shared c=Find(self,name);
    bool done=c.object!=nullptr;
    __try {
        if(c.object && shown) {
            ControlBlock* const ctrl=*reinterpret_cast<ControlBlock**>(static_cast<unsigned char*>(c.object)+kSkinCtrl);
            if(ctrl) {
                InterlockedIncrement(&ctrl->weaks);   // the callee's copy: it releases it
                Shared skin{*reinterpret_cast<void**>(static_cast<unsigned char*>(c.object)+kSkinPtr),ctrl};
                alignas(16) float colour[4];
                ChipColour(rgba,colour);
                reinterpret_cast<ModelParamFn>(image+kModelParam)(&skin,L"ColorChipBox",&kColourParam,colour);
            } else done=false;
        }
        if(c.object)Show(c.object,shown && done);
    } __except(EXCEPTION_EXECUTE_HANDLER){ done=false; }
    Release(c);
    if(done)st.written.chip[row][which]=key;
}
// A texture skin's image (the game's own icon call: 0x8C4256: by name, the Skin_Texture, 0x7F63E0) and its shown flag;
// a texture the game cannot load leaves the icon hidden.
void SetIcon(unsigned char* self,int row,const wchar_t* name,const wchar_t* path) noexcept {
    if(st.written.icon[row]==path)return;
    Shared c=Find(self,name);
    bool loaded=false;
    __try {
        if(c.object && path && c.control) {
            InterlockedIncrement(&c.control->uses);   // the callee's by-value copy: it releases it
            Shared copy=c,tex{};
            reinterpret_cast<TextureSkinFn>(image+kTextureSkin)(&tex,&copy);
            if(tex.object) {
                const WideString w=Wide(path);
                loaded=reinterpret_cast<SetTextureFn>(image+kSetTexture)(tex.object,&w);
            }
            Release(tex);
        }
        if(c.object)Show(c.object,loaded);
    } __except(EXCEPTION_EXECUTE_HANDLER){ loaded=false; }
    if(path && !loaded && c.object)Log("ROOMUI icon %ls not loaded (the element stays hidden)",path);
    Release(c);
    if(c.object || !path)st.written.icon[row]=path;   // a failed load is not retried every frame
}

void DrawRow(unsigned char* self,int i,const ViewRow* r) noexcept {
    wchar_t name[kNameMost],line[kLineMost];
    const Look l=LookOf(r);
    RowName(i,Part::box,name);
    if(Changed(kSlotBox+i,l.shown ? 1 : 2))SetShown(self,name,l.shown);
    if(!l.shown)return;
    RowLine(*r,line,kLineMost);
    RowName(i,Part::text,name);SetText(self,kSlotText+i,name,line,RowColour(r));
    NoteLine(*r,line,kLineMost);
    RowName(i,Part::note,name);SetText(self,kSlotNote+i,name,line,RowColour(r));
    RowName(i,Part::primary,name);SetChip(self,i,0,name,l.primary,l.primaryRgba);
    RowName(i,Part::secondary,name);SetChip(self,i,1,name,l.secondary,l.secondaryRgba);
    RowName(i,Part::icon,name);SetIcon(self,i,name,l.icon);
}
// The root's shown flag (the layout file shows it: hidden here from the screen's first frame on); false when this screen's
// layout has no menu elements.
bool Draw(unsigned char* self,bool shown) noexcept {
    Validate();
    if(!Changed(kSlotRoot,shown ? 1 : 2))return true;   // as it is: nothing to look up
    if(SetShown(self,kRootName,shown))return true;
    if(!st.missingLogged)Log("ROOMUI the %s screen's layout has no menu elements (Mods/UI not installed by the installer, or "
                             "another mod's layout): the menu is not drawn there",st.room ? "room" : "HQ");
    st.missingLogged=true;
    st.written.key[kSlotRoot]=~0ull;   // looked for again next frame (a layout cannot gain them, but it is one lookup)
    return false;
}
void DrawMenu(unsigned char* self) noexcept {
    const rm::View v=rm::Snapshot(st.menu);
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
    wchar_t key[48];
    ToggleName(key,48);
    HintLine(key,line,kLineMost);
    SetText(self,kSlotHint,kHintName,line);
}

// --- One frame of either screen ---
std::uint64_t Frame(Screen& screen,unsigned char* self,void* ctx) noexcept {
    if(resetAsked.exchange(false)) {
        Log("ROOMUI a mission started since the last screen frame: the room is left, the menu dropped");
        Forget(st.room && st.self);
        st.held=rm::Keys{};
    }
    Follow(self,screen.room);
    const bool on=Cfg().enabled && Cfg().roomLoadout;
    hudtext::Use(hudtext::Resolve(Cfg().hudLanguage,GameTextLanguage()));
    bool hasInput=false;
    if(on)__try { hasInput=reinterpret_cast<GetInputFn>(image+kGetInput)(self,ctx)!=nullptr; } __except(EXCEPTION_EXECUTE_HANDLER){ hasInput=false; }
    const rm::Keys down=on ? KeysDown(ReadRaw(),static_cast<std::uint16_t>(Cfg().roomLoadoutButton)) : rm::Keys{};
    const rm::Keys press=rm::Presses(st.held,down);
    if(!on && st.menu.open){rm::Discard(st.menu);Log("ROOMUI menu closed: RoomLoadout / Enabled turned off");}
    const Plan plan=PlanFrame(hasInput,st.menu.open);
    if(plan.step)Act(rm::Step(st.menu,press));
    const bool take=TakeInput(st.gate,st.menu.open,AnyDown(down));
    std::uint64_t result;
    if(take && ctx) {
        alignas(16) unsigned char copy[kCtxSize];
        std::memcpy(copy,ctx,kCtxSize);
        std::memset(copy+kCtxInput,0,sizeof(void*));
        result=screen.next(self,copy);
    } else result=screen.next(self,ctx);
    if(screen.room)roomsync::TickRoomSync();
    const bool shown=PlanFrame(hasInput,st.menu.open).show;
    FollowPreview(shown);
    roompreview::TickPreview(shown);
    if(Draw(self,shown) && shown)DrawMenu(self);
    return result;
}
std::uint64_t __fastcall RoomHook(unsigned char* self,void* ctx) { return Frame(screens[0],self,ctx); }
std::uint64_t __fastcall HqHook(unsigned char* self,void* ctx) { return Frame(screens[1],self,ctx); }

bool AllMatch(const Sig* sigs,std::size_t n,const char* what,const char* then) noexcept {
    for(std::size_t i=0;i<n;++i)
        if(!Matches(sigs[i].rva,sigs[i].bytes,sigs[i].size)) {
            Log("HOOK roomui REFUSED: %s: the code at EDF+0x%X is not as expected (%s)",what,sigs[i].rva,then);
            return false;
        }
    return true;
}
// EDF6Coop beside us but not loaded yet: it would find the room's slot not stock and refuse every patch of its own
// (it checks the slot points at 0x8FDFE0). EDFModLoader loads the plugins in name order (ModLoader.log: EDF6Coop before
// EDF6VehicleCrew), so this is not expected; if it happens the room is left to Coop.
bool CoopStillToLoad() noexcept {
    wchar_t path[MAX_PATH];
    const wchar_t* ini=ConfigPath();
    if(!ini || wcscpy_s(path,ini))return false;
    wchar_t* slash=wcsrchr(path,L'\\');
    if(!slash || wcscpy_s(slash+1,static_cast<std::size_t>(std::end(path)-(slash+1)),L"EDF6Coop.dll"))return false;
    return GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES && !GetModuleHandleW(L"EDF6Coop.dll");
}
bool Chain(Screen& s,void* hook) noexcept {
    void** const slot=reinterpret_cast<void**>(image+s.slot);
    if(!Readable(slot,8)){Log("HOOK roomui REFUSED: the %s screen's OnUpdate slot is not readable",s.name);return false;}
    void* const current=*slot;
    if(!current){Log("HOOK roomui REFUSED: the %s screen's OnUpdate slot is empty",s.name);return false;}
    if(s.room && current==image+s.stock && CoopStillToLoad()) {
        Log("HOOK roomui REFUSED: the room: EDF6Coop.dll is beside us but not loaded yet (it would refuse a chained room slot); "
            "the room menu is off this run, the HQ's is not");
        return false;
    }
    if(current!=image+s.stock)Log("HOOK roomui: the %s screen's OnUpdate chains onto %p (another plugin: EDF6Coop)",s.name,current);
    s.next=reinterpret_cast<UpdateFn>(current);
    s.ok=PatchVtableSlot(slot,current,hook);
    return s.ok;
}
}  // namespace

bool InstallRoomUi() noexcept {
    constexpr const char* kUntouched="both screens stay as they are";
    if(!AllMatch(kDrawSigs,std::size(kDrawSigs),"the layout calls",kUntouched) ||
       !AllMatch(kScreenSigs,std::size(kScreenSigs),"the screens' updates",kUntouched))
        return false;
    padOk=AllMatch(kPadSigs,std::size(kPadSigs),"the pad table","the menu: keyboard only");
    Log("HOOK roomui preview=%d (stage 7; 0: the menu runs without the 3D preview)",roompreview::InstallPreviewHooks());
    const bool room=Chain(screens[0],reinterpret_cast<void*>(&RoomHook));
    const bool hq=Chain(screens[1],reinterpret_cast<void*>(&HqHook));
    Log("HOOK roomui room=%d hq=%d (the support loadout menu in the online room and the offline HQ)",room,hq);
    return room || hq;
}

void ResetRoomUi() noexcept {
    // MissionStart's thread is not known to be the UI thread: the state is the UI thread's, and room_sync.h /
    // room_preview.h are called from it only. Asked here, done by the next screen frame (Frame).
    resetAsked.store(true);
}
}  // namespace crew
