// 整备: armory.h. EDF.dll TimeDateStamp 0x678CCB46 RVAs; the RE is docs/armory-re.md.
//  - ui::HUiHQMain's vtable 0x1804DD8 and ui::HUiRoom's 0x180AC40: slot 2 (Draw) at +0x10 of each, both the stock
//    HUiBase::Draw 0x7FA970 (read and chained, never assumed: another plugin may be there first); the draw context's
//    ID3D11DeviceContext* at +8 and its viewport {x, y, w, h} at +0x398 (docs/worldmap-select-re.md §2, the same
//    contract worldmap.cpp checks, kViewportSig / kDeviceSig).
//  - The UI thread builds the frame's View (armory_logic.h Build) and publishes it under viewLock; the render thread
//    paints the published copy after the screen's own draw, when the screen object drawing is the one it was opened on.
#include "armory.h"
#include "armory_draw.h"
#include "armory_logic.h"
#include "armory_presets.h"
#include "host_settings.h"
#include "hud.h"
#include "ini_text.h"
#include "loadout_file.h"
#include "loadout_store.h"
#include "online_authority.h"
#include "player_slots.h"
#include "room_dialog_logic.h"
#include "room_preview.h"
#include "room_pvp.h"
#include "room_sync.h"
#include "room_ui_logic.h"
#include "support_call.h"
#include "support_config.h"
#include "support_pages.h"
#include "support_points_logic.h"
#include "weapon_equip.h"
#include "weapon_page_logic.h"
#include "worldmap_logic.h"
#include "edf/input_source.h"
#include "edf_layout.h"
#include "config.h"
#include "plugin_log.h"
#include "infantryscope.h"
#include "hudtext.h"
#include "map.h"
#include "memory.h"
#include "ui_gpu.h"
#include "stores.h"
#include "ui_text.h"
#include <Windows.h>
#include <Xinput.h>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <new>
#include <set>
#include <string>

namespace crew::armoryui {
namespace {
namespace ar=crew::armory;
namespace rm=crew::roommenu;
namespace sp=crew::supportpages;
using hudtext::Tx;

// --- The draw contract (docs/armory-re.md §1) ---
constexpr unsigned kHqDrawSlot=0x1804DE8,kRoomDrawSlot=0x180AC50,kStockDraw=0x7FA970;
constexpr std::size_t kDrawDevice=0x8,kDrawViewport=0x398;
const unsigned char kDrawSig[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x55,0x57,0x41,0x56,0x48,0x8D};
const unsigned char kViewportSig[]={0x0F,0x11,0x81,0x98,0x03,0x00,0x00};   // 0x113E90B: movups [rcx+398h],xmm0
const unsigned char kDeviceSig[]={0x48,0x8B,0x49,0x08};                     // 0x113E928: mov rcx,[rcx+8]
struct Sig { unsigned rva; const unsigned char* bytes; std::size_t size; };
const Sig kSigs[]={{kStockDraw,kDrawSig,sizeof(kDrawSig)},{0x113E90B,kViewportSig,sizeof(kViewportSig)},
                   {0x113E928,kDeviceSig,sizeof(kDeviceSig)}};

using DrawFn=std::uint64_t(__fastcall*)(unsigned char*,unsigned char*);
DrawFn nextHqDraw=nullptr,nextRoomDraw=nullptr;
bool installed=false;

// --- What the render thread paints (published by the UI thread) ---
std::mutex viewLock;
ar::View published;                          // under viewLock
std::atomic<unsigned char*> drawOn{nullptr}; // the screen object it is open on (nullptr: closed)
// 整备 closed for the world map, still drawn (armory_logic.h Handoff): `holding` for the render thread; `mapDrawn`:
// the select drew since the UI thread last looked, `mapShown`: it is in place (worldmap.cpp's Draw, MapDrawn / MapShown).
std::atomic<bool> holding{false},mapDrawn{false},mapShown{false};
// Opened from the world map's title bar, waiting for its screen's input under the closing select (armory_logic.h
// Arrival): the select's draw paints it over itself (PaintOverSelect).
std::atomic<bool> overSelect{false};
std::atomic<unsigned char*> gone{nullptr};   // a destructor ran on this object since the UI thread last looked

// The render thread's: the renderer, its labels, the viewport last drawn into (for the pointer).
std::mutex gpuLock;
uigpu::Renderer* renderer=nullptr;
uitext::Labels* labels=nullptr;
std::atomic<int> gpuBroken{0};
unsigned drawFrame=0;
struct Geometry { float vp[4]{}; float targetW=0.f,targetH=0.f; };
SRWLOCK geometryLock=SRWLOCK_INIT;
Geometry geometry;

// --- Input (the UI thread; the wheel and the typed characters from the window procedure) ---
std::atomic<bool> wantText{false};   // open: the wheel and WM_CHAR are ours
std::atomic<int> wheel{0};
SRWLOCK typedLock=SRWLOCK_INIT;
wchar_t typed[8]{};
int ntyped=0;
HWND subclassed=nullptr;
WNDPROC prevProc=nullptr;
bool unicodeProc=true;
LRESULT CALLBACK WindowProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if(wantText.load(std::memory_order_relaxed)) {
        if(msg==WM_MOUSEWHEEL){wheel.fetch_add(GET_WHEEL_DELTA_WPARAM(wp),std::memory_order_relaxed);return 0;}
        if(msg==WM_CHAR && wp>=0x20 && wp!=0x7F) {
            AcquireSRWLockExclusive(&typedLock);
            if(ntyped<static_cast<int>(std::size(typed))-1)typed[ntyped++]=static_cast<wchar_t>(wp);
            ReleaseSRWLockExclusive(&typedLock);
            return 0;
        }
    }
    return unicodeProc ? CallWindowProcW(prevProc,w,msg,wp,lp) : CallWindowProcA(prevProc,w,msg,wp,lp);
}
void Subclass() noexcept {
    if(subclassed)return;
    const HWND w=edf::ForegroundWindow();
    DWORD pid=0;
    if(!w || !GetWindowThreadProcessId(w,&pid) || pid!=GetCurrentProcessId())return;
    unicodeProc=IsWindowUnicode(w)!=FALSE;
    prevProc=reinterpret_cast<WNDPROC>(unicodeProc ? GetWindowLongPtrW(w,GWLP_WNDPROC) : GetWindowLongPtrA(w,GWLP_WNDPROC));
    if(!prevProc)return;
    const LONG_PTR was=unicodeProc ? SetWindowLongPtrW(w,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&WindowProc))
                                   : SetWindowLongPtrA(w,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&WindowProc));
    if(!was){Log("ARMORY the window's procedure could not be subclassed (%lu): no wheel, no typed names (the pad's suggestions only)",GetLastError());return;}
    prevProc=reinterpret_cast<WNDPROC>(was);
    subclassed=w;
    Log("ARMORY window %p subclassed for the wheel and typed names",static_cast<void*>(w));
}

bool InFront() noexcept {
    DWORD pid=0;
    GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    return pid==GetCurrentProcessId();
}
bool Down(int vk) noexcept { return (edf::AsyncKey(vk)&0x8000)!=0; }

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
    const auto get=edf::PadSource() ? reinterpret_cast<XInputGetStateFn>(edf::PadSource()) : xinputGet;
    if(!get)return false;
    if(get(padIndex,s)==ERROR_SUCCESS)return true;
    const ULONGLONG now=GetTickCount64();
    if(now-padSearchAt<2000)return false;
    padSearchAt=now;
    for(DWORD i=0;i<XUSER_MAX_COUNT;++i)if(get(i,s)==ERROR_SUCCESS){padIndex=i;return true;}
    return false;
}

void ReadPointer(ar::Input& in) noexcept {
    const HWND w=edf::ForegroundWindow();
    POINT p{};
    RECT client{};
    if(!w || !GetCursorPos(&p) || !ScreenToClient(w,&p) || !GetClientRect(w,&client))return;
    AcquireSRWLockShared(&geometryLock);
    const Geometry g=geometry;
    ReleaseSRWLockShared(&geometryLock);
    const ::worldmap::Screen screen{ar::skin::kWidth,ar::skin::kHeight};
    const ::worldmap::Pixel at=::worldmap::ClientToDesign(static_cast<float>(p.x),static_cast<float>(p.y),
        static_cast<float>(client.right),static_cast<float>(client.bottom),g.targetW,g.targetH,g.vp,screen);
    in.pointer=at.x>=0.f && at.y>=0.f && at.x<screen.w && at.y<screen.h;
    in.x=at.x;in.y=at.y;
}

// A stick axis past the pad's right dead zone, -1..1 (0 inside it).
float StickAxis(SHORT v) noexcept {
    constexpr float kStickDead=XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE,kFull=32767.f;
    const float a=static_cast<float>(v);
    if(a>kStickDead)return a>=kFull ? 1.f : (a-kStickDead)/(kFull-kStickDead);
    if(a<-kStickDead)return a<=-kFull ? -1.f : (a+kStickDead)/(kFull-kStickDead);
    return 0.f;
}
// Seconds since the last call (the frame's dt), 0 the first time; at most 0.1.
float FrameSeconds() noexcept {
    static LARGE_INTEGER last{},freq{};
    LARGE_INTEGER now{};
    if(!freq.QuadPart && !QueryPerformanceFrequency(&freq))return 0.f;
    QueryPerformanceCounter(&now);
    const float s=last.QuadPart ? static_cast<float>(now.QuadPart-last.QuadPart)/static_cast<float>(freq.QuadPart) : 0.f;
    last=now;
    return s<0.f ? 0.f : s>0.1f ? 0.1f : s;
}

// The frame's input: nothing while the game is not in front or the mission's map holds the keys.
ar::Input ReadInput(bool toggleDown) noexcept {
    ar::Input in;
    in.held[ar::kToggle]=toggleDown;
    {
        AcquireSRWLockExclusive(&typedLock);
        for(int i=0;i<ntyped;++i)in.typed[i]=typed[i];
        in.ntyped=ntyped;ntyped=0;
        ReleaseSRWLockExclusive(&typedLock);
    }
    const int notches=wheel.exchange(0,std::memory_order_relaxed)/WHEEL_DELTA;
    if(!InFront() || MapHoldsKeys()){in.ntyped=0;return in;}
    in.wheel=notches;
    bool* h=in.held;
    h[ar::kUp]=Down(VK_UP);h[ar::kDown]=Down(VK_DOWN);h[ar::kLeft]=Down(VK_LEFT);h[ar::kRight]=Down(VK_RIGHT);
    h[ar::kConfirm]=Down(VK_RETURN);h[ar::kCancel]=Down(VK_ESCAPE);h[ar::kBackspace]=Down(VK_BACK);
    h[ar::kTabPrev]=Down('Q');h[ar::kTabNext]=Down('E');h[ar::kSubPrev]=Down('Z');h[ar::kSubNext]=Down('C');
    h[ar::kStore]=Down('R');h[ar::kPresetMenu]=Down('F');
    h[ar::kAltPrev]=Down('A');h[ar::kAltNext]=Down('D');h[ar::kJumpPrev]=Down(VK_PRIOR);h[ar::kJumpNext]=Down(VK_NEXT);
    h[ar::kSearch]=Down(VK_TAB);
    for(int d=0;d<8;++d)h[ar::kDigit1+d]=Down('1'+d);
    h[ar::kMouseLeft]=Down(VK_LBUTTON);h[ar::kMouseRight]=Down(VK_RBUTTON);
    XINPUT_STATE pad{};
    if(PadState(&pad)) {
        const WORD b=pad.Gamepad.wButtons;
        h[ar::kUp]|=(b&XINPUT_GAMEPAD_DPAD_UP)!=0;h[ar::kDown]|=(b&XINPUT_GAMEPAD_DPAD_DOWN)!=0;
        h[ar::kLeft]|=(b&XINPUT_GAMEPAD_DPAD_LEFT)!=0;h[ar::kRight]|=(b&XINPUT_GAMEPAD_DPAD_RIGHT)!=0;
        h[ar::kConfirm]|=(b&XINPUT_GAMEPAD_A)!=0;h[ar::kCancel]|=(b&XINPUT_GAMEPAD_B)!=0;
        h[ar::kStore]|=(b&XINPUT_GAMEPAD_X)!=0;h[ar::kPresetMenu]|=(b&XINPUT_GAMEPAD_Y)!=0;
        h[ar::kTabPrev]|=(b&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0;h[ar::kTabNext]|=(b&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0;
        h[ar::kSubPrev]|=pad.Gamepad.bLeftTrigger>XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        h[ar::kSubNext]|=pad.Gamepad.bRightTrigger>XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        // the right stick: left / right a page's alternate (the weapons page's category), up / down its jump; a page
        // looking round its 3D view takes it whole instead (Page::Look: lookX / lookY, the keys only it holds)
        constexpr int kStick=16000;
        const bool stick[4]={pad.Gamepad.sThumbRX<-kStick,pad.Gamepad.sThumbRX>kStick,pad.Gamepad.sThumbRY>kStick,
                             pad.Gamepad.sThumbRY<-kStick};
        const int keys[4]={ar::kAltPrev,ar::kAltNext,ar::kJumpPrev,ar::kJumpNext};
        for(int i=0;i<4;++i)
            if(stick[i] && !h[keys[i]]){h[keys[i]]=true;in.stickKeys|=1u<<keys[i];}
        in.lookX=StickAxis(pad.Gamepad.sThumbRX);in.lookY=StickAxis(pad.Gamepad.sThumbRY);
    }
    ReadPointer(in);
    return in;
}
bool AnyHeld(const ar::Input& in) noexcept {
    for(int k=0;k<ar::kKeyCount;++k)if(k!=ar::kMouseLeft && k!=ar::kMouseRight && in.held[k])return true;
    return false;
}

// --- The pages (placeholders: read-only views of what each tab will edit) ---
void Utf16(const std::string& s,wchar_t* out,std::size_t cap) noexcept {
    out[0]=0;
    if(s.empty())return;
    const int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out,static_cast<int>(cap)-1);
    out[n>0 ? n : 0]=0;
}
void ReadOnly(ar::Line* out) noexcept {
    ar::CopyText(out->label,ar::kTextMost,hudtext::Tr(Tx::armoryReadOnly));
    out->dim=true;
}

// The weapons page (weapon_page_logic.h) on the game: weapon_equip writes, room_preview shows (player 1).
class WeaponsGame final : public ar::wpage::Game {
public:
    bool built=false;   // the catalog built from the table loaded now (Open)
    bool Ready() const noexcept override { return built && wequip::Ready() && wequip::RowCount()>=0; }
    int CurrentClass() const noexcept override { return wequip::CurrentClass(0); }
    bool Read(int cls,wlist::Loadout* out) noexcept override {
        try { return wequip::ReadLoadout(0,cls,out); } catch(...) { return false; }
    }
    wlist::Verdict Equip(int cls,int slot,int row) noexcept override {
        const wequip::EquipResult r=wequip::Equip(0,cls,slot,row);
        if(r.written && wequip::CurrentClass(0)==cls)roompreview::ShowPlayerWeapon(cls,slot,row);
        return r.verdict;
    }
    void Preview(int cls,int slot,int row) noexcept override { roompreview::ShowPlayerWeapon(cls,slot,row); }
    void SaveFavourites(const std::set<std::wstring>& ids) noexcept override {
        try {
            if(!SaveIniFlag("WeaponFavourites",wlist::SerializeFavourites(ids).c_str()))
                Log("ARMORY the weapon favourites could not be written to the ini");
        } catch(...) {}
    }
    bool ReadSpec(int row,wlist::WeaponSpec* out) noexcept override {
        try { return wequip::ReadSpec(row,out); } catch(...) { return false; }
    }
};

// The mission: where missions are picked, and in the room the 对战 pages.
class MissionPage final : public ar::Page {
public:
    void Bind(bool inRoom) noexcept { room=inRoom; }
    const wchar_t* Name() const noexcept override { return hudtext::Tr(Tx::armoryTabMission); }
    int Rows(int) const noexcept override { return room ? 2 : 1; }
    void RowLine(int,int row,ar::Line* out) const noexcept override {
        if(row==0){ar::CopyText(out->label,ar::kTextMost,hudtext::Tr(Tx::armoryWorldMap));return;}
        ar::CopyText(out->label,ar::kTextMost,hudtext::Tr(Tx::armoryPvp));
    }
    int Items(const ar::Pos& p) const noexcept override { return p.row==0 || p.row==1 ? 1 : 0; }
    void ItemLine(const ar::Pos& p,ar::Line* out) const noexcept override {
        ar::CopyText(out->label,ar::kTextMost,hudtext::Tr(p.row==0 ? Tx::armoryWorldMap : Tx::armoryPvp));
    }
    int Fields(const ar::Pos& p) const noexcept override { return p.row==0 || p.row==1 ? 1 : 0; }
    void FieldLine(const ar::Pos&,ar::Line* out) const noexcept override {
        ar::CopyText(out->label,ar::kTextMost,hudtext::Tr(Tx::armoryPvpOpen));
        out->acts=true;
    }
    // Row 0: the world map (the screen's own mission select: room_ui.cpp presses its button); row 1: the 对战 pages.
    bool Activate(const ar::Pos& p) noexcept override {
        if(p.row==0){mapAsked=true;return true;}
        if(p.row!=1)return false;
        asked=true;
        return true;
    }
    bool asked=false,mapAsked=false;
private:
    bool room=false;
};

// --- The UI thread's state ---
sp::Model support;            // the 支援编成 / 载具 pages' working copy (big: static)
sp::SquadPage squadPage;
sp::VehiclePage vehiclePage;
WeaponsGame weaponsGame;
wlist::Catalog weaponCatalog;
ar::wpage::Texts weaponTexts;
ar::wpage::WeaponsPage weaponsPage;
MissionPage missionPage;
ar::Page* const kPages[]={&weaponsPage,&squadPage,&vehiclePage,&missionPage};
constexpr int kWeaponsTab=0,kSquadTab=1,kVehicleTab=2;
constexpr int kPageCount=static_cast<int>(std::size(kPages));

ar::State state;
ar::Presets presets;
ar::Words words;
ar::View building;            // big: not on the stack
unsigned char* openOn=nullptr;
bool latched=false;           // closed with one of its keys still held: the screen's input stays taken until they are up
bool pvpAsked=false;
rm::IniRead iniRead=nullptr;  // room_ui's ini reader (a preset switch reloads the support pages through it)
void* iniContext=nullptr;
bool openRoomNow=false;       // the screen it is open on is the room (the status strip's room lines)
// The status strip under the hint bar (ar::StatusLines; the old room menu's status lines, room_ui_logic.h
// ComposeStatus): the last save's result while open, the generator, the PvP start held, the host's settings and the
// room's sync. The generator's state is read from its status file: polled at most every kGenPollMs.
wchar_t saveNote[ar::kTextMost]{};
bool saveWarn=false;
constexpr ULONGLONG kGenPollMs=500;
ULONGLONG genPolled=0;
GenerateStatus genNow{};
// Edits dropped because the game took the screen away (a dialog, a transition, the screen ending): said at the next
// open (docs/armory-re.md).
int editsDropped=0;

// The pages' saver: the lines written as one batch through the loadout store (all or none), the variants then generated
// (the same path as a preset switch, armory_presets.cpp Switch).
bool SaveSupport(void*,const crew::roomui::SaveItem* items,int n,wchar_t* why,std::size_t cap) noexcept {
    static LoadoutSave lines[kLoadoutSaveMost];
    if(n<=0)return true;
    if(n>kLoadoutSaveMost){if(why && cap)wcsncpy_s(why,cap,L"too many lines in one save",_TRUNCATE);return false;}
    for(int i=0;i<n;++i)lines[i]=LoadoutSave{items[i].field,items[i].unit,items[i].value};
    int failed=-1;
    if(!SaveSupportLoadouts(lines,n,&failed,why,cap)) {
        Log("ARMORY support save refused at %d (%ls): %ls (nothing written)",failed,
            failed>=0 && failed<n && items[failed].unit ? items[failed].unit : L"-",why ? why : L"");
        _snwprintf_s(saveNote,_TRUNCATE,hudtext::Tr(Tx::roomSaveFailed),why && *why ? why : L"-");
        saveWarn=true;
        return false;
    }
    wchar_t gen[160]{};
    if(!StartVariantGeneration(gen,std::size(gen))) {
        Log("ARMORY the variant generator not started: %ls",gen);
        _snwprintf_s(saveNote,_TRUNCATE,hudtext::Tr(Tx::roomGenStartFailed),gen);
        saveWarn=true;
    } else {
        _snwprintf_s(saveNote,_TRUNCATE,hudtext::Tr(Tx::roomSaved),n);
        saveWarn=false;
    }
    genPolled=0;   // the generator just started: its line now, not half a second late
    Log("ARMORY support saved: %d lines",n);
    return true;
}

// The vehicle page's fire keys (sp::Env::storeKey): each pylon store's own key (its StoreSpec, stores.inc, the files
// support_loadout.py writes for a pylon: EDF6VC_<NAME>_<rounds>.SGO) and this machine's bindings, named as the HUD names
// them (hud.cpp PayloadControls).
static_assert(sp::kOwnKeysLast==kFireLastKey && sp::kFirstOwnKey==kSecondTriggerKey+1,"stores.h's fire keys");
void FillFireKeys(sp::Env& e) noexcept {
    for(const LoadStore& s:kLoadStores) {
        wchar_t prefix[48];
        _snwprintf_s(prefix,_TRUNCATE,L"EDF6VC_%ls_",s.name);
        const StoreSpec* spec=StoreSpecOfPrefix(prefix);
        if(!spec){Log("ARMORY the pylon store %ls has no StoreSpec (stores.inc): its fire key shown as the cycle's",s.name);continue;}
        e.storeKey[s.code]=!spec->nativeControl && spec->fireKey>=sp::kFirstOwnKey && spec->fireKey<=sp::kOwnKeysLast ?
                           spec->fireKey : 0;
    }
    wchar_t k[sp::kKeyNameMost],b[sp::kKeyNameMost];
    ControlKeyName(Cfg().playerJetSwitchKey,k,sp::kKeyNameMost);
    _snwprintf_s(e.switchKey,_TRUNCATE,L"%ls / LB",k);
    for(int n=sp::kFirstOwnKey;n<=sp::kOwnKeysLast;++n) {
        ControlKeyName(FireKeyVk(static_cast<unsigned char>(n)),k,sp::kKeyNameMost);
        const int pad=FireKeyButton(static_cast<unsigned char>(n));
        if(pad>0){ControlPadName(pad,b,sp::kKeyNameMost);_snwprintf_s(e.ownKey[n],_TRUNCATE,L"%ls / %ls",k,b);}
        else wcsncpy_s(e.ownKey[n],k,_TRUNCATE);
    }
}

// Each menu unit's catalog entry (SupportCallKey matched by its key; -1: not in the catalog), found at Open.
int unitCatalog[rm::kMostUnits]{};
// Each unit's call rule (sp::Env::rule) as the room plays by it (host_settings.h SupportRulesInEffect): offline and as
// the host this machine's SupportDisabled / SupportPtScale, as a guest the host's (its room word v4; until it arrived the
// panel says it waits). Every frame: the host's word may arrive or change while the page is open.
void FillRules() noexcept {
    sp::Env& e=support.env;
    const hostcfg::SupportRules r=SupportRulesInEffect();
    e.host=OnlineHostOnly();
    e.rulesKnown=r.known;
    for(int i=0;i<support.menu.unitCount && i<rm::kMostUnits;++i) {
        const int c=unitCatalog[i];
        if(c<0){e.rule[i]=sp::HostRule{};continue;}
        const std::uint32_t pt=SupportCallPt(c);
        e.rule[i]=sp::HostRule{true,hostcfg::SupportCallable(r,c),pt,support_points::Price(pt,r.ptScale)};
    }
}
// The pages' environment: online or not, who hosts, the crew kinds and each unit's call rule (FillRules).
void FillEnv(bool room,bool previewOn) noexcept {
    sp::Env& e=support.env;
    e=sp::Env{};
    e.online=room;
    e.previewOn=previewOn;
    e.fileThere=roompreview::PreviewFileThere;
    e.placed=roompreview::PreviewPlacement;
    e.callouts=roompreview::PreviewCallouts;
    e.kinds=crew::roomui::CrewKinds{SupportCfg().vehicleCrew,SupportCfg().aircraftCrew};
    for(int i=0;i<rm::kMostUnits;++i) {
        unitCatalog[i]=-1;
        const wchar_t* key=i<support.menu.unitCount ? support.menu.unit[i].key : nullptr;
        if(!key || !*key)continue;
        for(int c=0;c<SupportCallCount();++c)
            if(const wchar_t* k=SupportCallKey(c);k && _wcsicmp(k,key)==0){unitCatalog[i]=c;break;}
    }
    FillRules();
    FillFireKeys(e);
}
void BindSupport() noexcept {
    squadPage.Bind(&support);
    vehiclePage.Bind(&support);
}

void FillWeaponTexts() noexcept {
    ar::wpage::Texts& t=weaponTexts;
    t.name=hudtext::Tr(Tx::armoryTabPlayer);
    static constexpr Tx kClassNames[]={Tx::classRanger,Tx::classWingDiver,Tx::armoryClassAirRaider,Tx::classFencer};
    for(int c=0;c<ar::wpage::kClasses;++c)t.cls[c]=hudtext::Tr(kClassNames[c]);
    t.slotFormat=hudtext::Tr(Tx::armorySlot);
    t.empty=hudtext::Tr(Tx::armoryEmpty);
    t.notReady=hudtext::Tr(Tx::armoryNoPlayer);
    t.noWeapons=hudtext::Tr(Tx::armoryWpNone);
    t.all=hudtext::Tr(Tx::armoryWpAll);
    t.search=hudtext::Tr(Tx::armoryWpSearch);
    t.owned=hudtext::Tr(Tx::armoryWpOwned);
    t.equip=hudtext::Tr(Tx::armoryWpEquip);
    t.expand=hudtext::Tr(Tx::armoryWpExpand);
    t.collapse=hudtext::Tr(Tx::armoryWpCollapse);
    t.favourite=hudtext::Tr(Tx::armoryWpFavourite);
    t.source=hudtext::Tr(Tx::armoryWpSource);
    t.showUnowned=hudtext::Tr(Tx::armoryWpShowUnowned);
    t.favouritesOnly=hudtext::Tr(Tx::armoryWpFavOnly);
    t.on=hudtext::Tr(Tx::armoryWpOn);
    t.off=hudtext::Tr(Tx::armoryWpOff);
    t.level=hudtext::Tr(Tx::armoryWpLevel);
    t.badgeNew=hudtext::Tr(Tx::armoryWpNew);
    t.badgeUp=hudtext::Tr(Tx::armoryWpUp);
    t.previewOther=hudtext::Tr(Tx::armoryWpPreviewOther);
    t.verdict[static_cast<int>(wlist::Verdict::notOwned)]=hudtext::Tr(Tx::armoryWpNotOwned);
    t.verdict[static_cast<int>(wlist::Verdict::capped)]=hudtext::Tr(Tx::armoryWpCapped);
    t.verdict[static_cast<int>(wlist::Verdict::wrongSlot)]=hudtext::Tr(Tx::armoryWpWrongSlot);
    t.verdict[static_cast<int>(wlist::Verdict::duplicate)]=hudtext::Tr(Tx::armoryWpDuplicate);
    t.verdict[static_cast<int>(wlist::Verdict::absent)]=hudtext::Tr(Tx::armoryWpAbsent);
    t.hints[0]=hudtext::Tr(Tx::armoryWpHintList);
    t.hints[1]=hudtext::Tr(Tx::armoryWpHintCentre);
    t.hints[2]=hudtext::Tr(Tx::armoryWpHintDetails);
    t.typingHint=hudtext::Tr(Tx::armoryWpHintTyping);
    t.lang=static_cast<int>(hudtext::InUse());
}

// The favourites ([VehicleCrew] WeaponFavourites, ids comma-separated) as the ini has them now.
std::set<std::wstring> ReadWeaponFavourites() noexcept {
    std::set<std::wstring> ids;
    try {
        std::string ini,value;
        const wchar_t* path=ConfigPath();
        if(path && ReadFileBytes(path,&ini) && initext::Get(ini,"VehicleCrew","WeaponFavourites",&value) &&
           !wlist::ParseFavourites(value,&ids))
            Log("ARMORY WeaponFavourites in the ini is malformed: ignored");
    } catch(...) {}
    return ids;
}

// The weapons page's 3D view each frame: the weapon under the cursor in the player's hands (Tick). Where the view goes
// (the page's hole) is PreviewWanted's: room_ui.cpp DrivePreview is the one caller of SetViewPlacement /
// ClearViewPlacement, so the pages never undo each other's placement.
void WeaponsView(bool active) noexcept {
    weaponsPage.Tick(state.pos,state.column,active);
}
bool openRoom=false;   // opened over the room (else the HQ)
bool mapAsked=false;   // the world map asked (the 任务 tab chosen on the HQ, or its row activated): room_ui opens it
int openTab=-1;        // the world map's title bar asked 整备 open at this tab (the next frame the screen has its input)

void FillWords() noexcept {
    words.title=hudtext::Tr(Tx::armoryTitle);
    words.add=L"＋";
    words.actions[ar::kActNew]=hudtext::Tr(Tx::armoryActNew);
    words.actions[ar::kActCopy]=hudtext::Tr(Tx::armoryActCopy);
    words.actions[ar::kActRename]=hudtext::Tr(Tx::armoryActRename);
    words.actions[ar::kActDelete]=hudtext::Tr(Tx::armoryActDelete);
    words.actions[ar::kActStore]=hudtext::Tr(Tx::armoryActStore);
    words.confirmDelete=hudtext::Tr(Tx::armoryConfirmDelete);
    words.renameTitle=hudtext::Tr(Tx::armoryRenameTitle);
    words.hints[0]=hudtext::Tr(Tx::armoryHintList);
    words.hints[1]=hudtext::Tr(Tx::armoryHintCentre);
    words.hints[2]=hudtext::Tr(Tx::armoryHintDetails);
    words.hints[3]=hudtext::Tr(Tx::armoryHintPopup);
    words.hints[4]=hudtext::Tr(Tx::armoryHintRename);
    words.rootExit=hudtext::Tr(openRoomNow ? Tx::armoryHintLobbyBack : Tx::armoryHintMapBack);
    words.deleteHint=hudtext::Tr(Tx::armoryHintDelete);
    static constexpr Tx kSuggest[]={Tx::armorySuggest1,Tx::armorySuggest2,Tx::armorySuggest3,Tx::armorySuggest4};
    words.nsuggest=static_cast<int>(std::size(kSuggest));
    for(int i=0;i<words.nsuggest;++i)words.suggest[i]=hudtext::Tr(kSuggest[i]);
    FillWeaponTexts();
    words.unsaved=hudtext::Tr(Tx::spUnsaved);
    words.unsavedTitle=hudtext::Tr(Tx::spUnsavedTitle);
    words.unsavedActions[ar::kUnsavedSave]=hudtext::Tr(Tx::spUnsavedSave);
    words.unsavedActions[ar::kUnsavedDiscard]=hudtext::Tr(Tx::spUnsavedDiscard);
    words.unsavedActions[ar::kUnsavedBack]=hudtext::Tr(Tx::spUnsavedBack);
}

// The status strip, in order: the start held (the room's PvP lock), the last save, the generator, the room's sync, the
// host's settings (as many as ar::kStatusMost).
void FillStatus(ar::StatusLines* out) noexcept {
    *out=ar::StatusLines{};
    wchar_t line[ar::kTextMost];
    if(openRoomNow && roomsync::RoomSyncStartHeldLine(line,std::size(line)))out->Add(line,true);
    if(openRoomNow && RoomPvpWaitingLine(line,std::size(line)))out->Add(line,true);
    out->Add(saveNote,saveWarn);
    const ULONGLONG now=GetTickCount64();
    if(!genPolled || now-genPolled>=kGenPollMs){genNow=VariantGenerationStatus();genPolled=now;}
    if(crew::roomui::GenerateLine(genNow,line,std::size(line)))
        out->Add(line,genNow.state==GenerateState::failed || genNow.state==GenerateState::missing);
    if(!openRoomNow)return;
    roomsync::SyncLine sync[ar::kStatusMost]{};
    const int nsync=roomsync::RoomSyncLines(sync,ar::kStatusMost);
    for(int i=0;i<nsync;++i)out->Add(sync[i].text,sync[i].warn);
    const int nhost=HostSettingsLines(sync,ar::kStatusMost);
    for(int i=0;i<nhost;++i)out->Add(sync[i].text,sync[i].warn);
}
ar::StatusLines statusLines;

void Publish(bool open) noexcept {
    if(open){FillStatus(&statusLines);ar::Build(state,kPages,kPageCount,presets,words,&building,&statusLines);}
    else building.Clear();
    building.open=open;
    {
        std::lock_guard<std::mutex> hold(viewLock);
        std::memcpy(static_cast<void*>(&published),&building,sizeof(ar::View));
    }
    drawOn.store(open ? openOn : nullptr);
}

// The game took the screen away under an open 整备 (no input: a dialog / transition over it; the screen changed or
// ended; Forget): there is nobody to ask "save or discard" (Popup::unsaved), and saving silently is not what the
// player chose. The pages' unsaved edits are discarded, logged, and said at the next open.
void DropEdits(const char* why) noexcept {
    if(!state.open)return;
    int dropped=0;
    for(ar::Page* p:kPages)
        if(p->Dirty()){p->Discard();++dropped;Log("ARMORY unsaved edits on the %ls page discarded (%s)",p->Name(),why);}
    editsDropped+=dropped;
}

ar::HandoffState handoff;
ar::Arrival arrival=ar::Arrival::none;   // the UI thread's; overSelect follows it for the render thread
// `hold` (the world map asked): the view as last published stays drawn on its screen until the select takes over.
void CloseNow(const char* why,bool hold=false) noexcept {
    if(!state.open && !openOn)return;
    arrival=ar::Arrival::none;
    overSelect.store(false);
    ar::Close(state);
    WeaponsView(false);
    wantText.store(false);
    if(hold && openOn && drawOn.load()==openOn) {
        ar::HandoffAsk(handoff);
        mapDrawn.store(false);mapShown.store(false);
        holding.store(true);
        Log("ARMORY closed (%s): still drawn until the select is in place",why);
        return;
    }
    handoff=ar::HandoffState{};
    holding.store(false);
    Publish(false);
    Log("ARMORY closed (%s)",why);
    openOn=nullptr;
}
void EndHandoff(const char* why) noexcept {
    if(handoff.phase==ar::Handoff::none && !holding.load())return;
    handoff=ar::HandoffState{};
    holding.store(false);
    Publish(false);
    openOn=nullptr;
    Log("ARMORY no longer drawn for the world map (%s)",why);
}

// --- The render thread ---
bool ReadDrawCtx(const unsigned char* ctx,ID3D11DeviceContext** device,int* vp) noexcept {
    __try {
        *device=*reinterpret_cast<ID3D11DeviceContext* const*>(ctx+kDrawDevice);
        std::memcpy(vp,ctx+kDrawViewport,4*sizeof(int));
        return *device!=nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER){ return false; }
}
ar::View painting;   // the render thread's copy (under gpuLock)
// `opaque`: on a base covering the whole viewport first (painted over the select: armory_logic.h Arrival).
void Paint(unsigned char* ctx,bool opaque=false) noexcept {
    ID3D11DeviceContext* device=nullptr;
    int vp[4]{};
    if(!ReadDrawCtx(ctx,&device,vp) || vp[2]<=0 || vp[3]<=0)return;
    std::lock_guard<std::mutex> hold(gpuLock);
    {
        std::lock_guard<std::mutex> view(viewLock);
        if(!published.open)return;
        std::memcpy(static_cast<void*>(&painting),&published,sizeof(ar::View));
    }
    if(!renderer)renderer=new(std::nothrow) uigpu::Renderer();
    if(!labels)labels=new(std::nothrow) uitext::Labels();
    if(!renderer || !labels)return;
    const uigpu::Viewport viewport{static_cast<float>(vp[0]),static_cast<float>(vp[1]),static_cast<float>(vp[2]),
                                   static_cast<float>(vp[3])};
    const uigpu::Renderer::Begun begun=renderer->Begin(device,viewport);
    if(begun==uigpu::Renderer::Begun::broken) {
        if(gpuBroken.exchange(1)==0)Log("ARMORY the screen cannot be drawn on this device: %s",renderer->Why());
        renderer->Reset();
        labels->Reset();
        return;
    }
    if(begun!=uigpu::Renderer::Begun::ok)return;
    struct Ender { uigpu::Renderer* r; ~Ender() { r->End(); } } ender{renderer};
    if(opaque)renderer->Rect(0.f,0.f,ar::skin::kWidth,ar::skin::kHeight,ar::skin::kDiamondDark);
    {
        AcquireSRWLockExclusive(&geometryLock);
        for(int i=0;i<4;++i)geometry.vp[i]=static_cast<float>(vp[i]);
        geometry.targetW=static_cast<float>(renderer->TargetWidth());
        geometry.targetH=static_cast<float>(renderer->TargetHeight());
        ReleaseSRWLockExclusive(&geometryLock);
    }
    ar::Paint(*renderer,*labels,painting,hudtext::InUse(),++drawFrame);
}
std::uint64_t DrawOver(DrawFn next,unsigned char* self,unsigned char* ctx) noexcept {
    const std::uint64_t result=next(self,ctx);
    if(ctx && self && drawOn.load()==self && gone.load()!=self && !gpuBroken.load() && !(holding.load() && mapShown.load()))
        Paint(ctx);
    return result;
}
std::uint64_t __fastcall HqDrawHook(unsigned char* self,unsigned char* ctx) { return DrawOver(nextHqDraw,self,ctx); }
std::uint64_t __fastcall RoomDrawHook(unsigned char* self,unsigned char* ctx) { return DrawOver(nextRoomDraw,self,ctx); }

bool Chain(unsigned slotRva,void* hook,DrawFn& next,const char* what) noexcept {
    void** const slot=reinterpret_cast<void**>(image+slotRva);
    void* const current=Readable(slot,8) ? *slot : nullptr;
    if(!current){Log("HOOK armory REFUSED: the %s screen's Draw slot is not readable / empty",what);return false;}
    if(current!=image+kStockDraw)Log("HOOK armory: the %s screen's Draw chains onto %p (another plugin)",what,current);
    next=reinterpret_cast<DrawFn>(current);
    if(PatchVtableSlot(slot,current,hook))return true;
    Log("HOOK armory REFUSED: the %s screen's Draw slot could not be patched",what);
    return false;
}
}  // namespace

bool Install() noexcept {
    for(const Sig& s:kSigs)
        if(!Matches(s.rva,s.bytes,s.size)) {
            Log("HOOK armory REFUSED: the code at EDF+0x%X is not as expected: no 整备 screen",s.rva);
            return false;
        }
    const bool hq=Chain(kHqDrawSlot,reinterpret_cast<void*>(&HqDrawHook),nextHqDraw,"HQ");
    const bool room=Chain(kRoomDrawSlot,reinterpret_cast<void*>(&RoomDrawHook),nextRoomDraw,"room");
    installed=hq && room;
    Log("HOOK armory Draw chained: HQ=%d room=%d (vtables 0x1804DD8 / 0x180AC40 slot 2)",hq,room);
    return installed;
}
bool Installed() noexcept { return installed; }

void Open(unsigned char* self,bool room,const roommenu::Menu* units,roommenu::IniRead read,void* readContext,bool previewOn,
          int tab,bool waitInput) noexcept {
    if(!installed || !self)return;
    Subclass();
    openRoomNow=room;
    FillWords();
    iniRead=read;iniContext=readContext;
    sp::Bind(support,units);
    support.saver=&SaveSupport;support.saverContext=nullptr;
    FillEnv(room,previewOn);
    BindSupport();
    squadPage.aspect=previewOn ? roompreview::ScreenAspectNow() : 16.f/9.f;
    try {
        weaponsGame.built=wequip::BuildCatalog(&weaponCatalog);
    } catch(...) { weaponsGame.built=false; }
    if(!weaponsGame.built)Log("ARMORY the weapons page: the weapon table is not loaded (or weapon_equip is off)");
    weaponsPage.Attach(&weaponsGame,&weaponCatalog,&weaponTexts);
    weaponsPage.SetFavourites(ReadWeaponFavourites());
    missionPage.Bind(room);
    missionPage.asked=false;
    missionPage.mapAsked=false;
    pvpAsked=false;
    mapAsked=false;
    openRoom=room;
    handoff=ar::HandoffState{};
    holding.store(false);
    saveNote[0]=0;saveWarn=false;genPolled=0;
    if(tab>=0 && tab<kPageCount)state.tab=tab;
    wchar_t why[160]{};
    const bool presetsOn=armorypresets::Load(why,std::size(why));
    armorypresets::Fill(&presets);
    openOn=self;
    gone.store(nullptr);
    latched=true;
    ar::Open(state,ReadInput(true),state.tab);   // resets the state, its note too: the notes below come after
    if(!presetsOn) {
        wchar_t note[ar::kTextMost];
        _snwprintf_s(note,_TRUNCATE,hudtext::Tr(Tx::armoryPresetsOff),why);
        ar::Note(state,note);
    }
    if(editsDropped) {
        wchar_t note[ar::kTextMost];
        _snwprintf_s(note,_TRUNCATE,hudtext::Tr(Tx::armoryEditsDropped),editsDropped);
        ar::Note(state,note);
        editsDropped=0;
    }
    // Waiting (the select still closing over the screen): the wheel and typed characters stay the game's until it arrives.
    arrival=waitInput ? ar::Arrival::waiting : ar::Arrival::none;
    overSelect.store(waitInput);
    wantText.store(!waitInput);
    Publish(true);
    Log("ARMORY opened on the %s screen %p (%d presets)%s",room ? "room" : "HQ",static_cast<void*>(self),presets.count,
        waitInput ? ": drawn, waiting for the screen's input (the select over it closing)" : "");
}
bool IsOpen() noexcept { return state.open; }

bool Frame(unsigned char* self,bool hasInput,bool toggleDown) noexcept {
    if(handoff.phase!=ar::Handoff::none) {
        if(self!=openOn || drawOn.load()!=openOn || gone.load()==openOn)EndHandoff("its screen changed or ended");
        else if(mapShown.load())EndHandoff("the select in place");
        else if(!ar::HandoffFrame(handoff,hasInput,mapDrawn.exchange(false)))
            EndHandoff(hasInput ? "the screen has its input again without the select" : "the screen lost its input before the press");
    }
    if(state.open && (self!=openOn || gone.load()==openOn)) {
        DropEdits("its screen changed or ended");
        CloseNow("its screen changed or ended");
        latched=false;
    }
    if(!state.open) {
        if(!latched)return false;
        latched=AnyHeld(ReadInput(toggleDown));
        return latched;
    }
    bool arrived=false;
    switch(ar::ArrivalFrame(arrival,hasInput)) {
        case ar::ArrivalStep::waiting: Publish(true);return false;   // drawn; nothing read, nothing taken
        case ar::ArrivalStep::arrived:
            arrived=true;
            overSelect.store(false);
            wantText.store(true);
            Log("ARMORY on the %s screen %p: the screen has its input: 整备 takes it",openRoom ? "room" : "HQ",static_cast<void*>(openOn));
            break;
        case ar::ArrivalStep::open: break;
    }
    if(!hasInput) {   // something of the game's opened over the screen (a dialog, a sub-screen, a transition)
        DropEdits("the screen lost its input");
        CloseNow("the screen lost its input");
        latched=false;
        return false;
    }
    FillWords();
    FillRules();
    ar::Input in=ReadInput(toggleDown);
    in.dt=FrameSeconds();
    if(arrived)ar::Latch(state,in);   // the keys held as the select went (the tab's click, Q / E) do nothing here
    const int tabWas=state.tab;
    const ar::Effects fx=ar::Step(state,kPages,kPageCount,presets,words,in);
    // Esc at the root of HQ's armory returns to the replacement mission map. The room's own lobby remains its parent.
    if(fx.backToTasks && !openRoom)mapAsked=true;
    // The HQ's 任务 tab is the world map itself (its page has nothing else there): chosen, the map opens. In the room the
    // page keeps its 对战 row; its world map row opens the map.
    if(state.open && !openRoom && tabWas!=state.tab && kPages[state.tab]==&missionPage)missionPage.mapAsked=true;
    if(fx.op!=ar::Op::none) {
        wchar_t why[160]{};
        if(!armorypresets::Apply(fx,why,std::size(why))) {
            Log("ARMORY preset op %d refused: %ls",static_cast<int>(fx.op),why);
            ar::Note(state,why);
        } else if(fx.op==ar::Op::switchTo) {
            roomsync::RoomSyncNudge();   // the room's start gate reads the switched-in set's files at once
            weaponsPage.Refresh();
            if(iniRead)sp::Reload(support,iniRead,iniContext);   // the preset rewrote the support keys: shown as written
            BindSupport();
        }
        armorypresets::Fill(&presets);
    }
    if(missionPage.asked){missionPage.asked=false;pvpAsked=true;ar::Close(state);}
    if(missionPage.mapAsked){missionPage.mapAsked=false;mapAsked=true;ar::Close(state);}
    WeaponsView(state.open && state.tab==kWeaponsTab && state.popup==ar::Popup::none);
    if(!state.open) {
        CloseNow(pvpAsked ? "the 对战 pages asked" : mapAsked ? "the world map asked" : "its own keys",mapAsked);
        latched=AnyHeld(in);
        return true;   // this frame's keys were ours
    }
    Publish(true);
    return true;
}

bool PreviewWanted(roompreview::PreviewSpec* spec,roompreview::ViewRect* rect,roompreview::ViewCamera* camera,
                   bool* own,roompreview::SquadSpec* squad) noexcept {
    if(own)*own=false;
    if(squad)squad->count=0;
    // held for the world map: the model stays in the drawn view's hole too
    if((!state.open && handoff.phase==ar::Handoff::none) || !support.env.previewOn)return false;   // as the page's Hole: the popups are drawn over the model
    if(state.tab==kWeaponsTab) {
        // the player's own model holding the weapon under the cursor, in the details column's hole (front camera)
        ar::skin::Rect hole{};
        if(state.popup!=ar::Popup::none || !weaponsPage.Hole(state.pos,&hole))return false;
        if(rect)*rect=roompreview::ViewRect{hole.x0,hole.y0,hole.x1-hole.x0,hole.y1-hole.y0};
        if(camera)*camera=roompreview::PresetOf(roompreview::CameraPreset::front);
        if(own)*own=true;
        return true;
    }
    const sp::UnitsPage* page=state.tab==kSquadTab ? static_cast<const sp::UnitsPage*>(&squadPage) :
                              state.tab==kVehicleTab ? static_cast<const sp::UnitsPage*>(&vehiclePage) : nullptr;
    if(!page || !page->PreviewOf(state.pos,spec))return false;
    if(squad) {
        squad->count=page->SquadOf(state.pos,squad->soldier);
        if(squad->count<2)squad->count=0;
        else if(spec)*spec=squad->soldier[0];
    }
    if(rect)*rect=sp::ViewRectOf();
    if(camera)*camera=page->Camera();
    return true;
}

bool TakePvpRequest() noexcept {
    const bool asked=pvpAsked;
    pvpAsked=false;
    return asked;
}
bool TakeMapRequest() noexcept {
    const bool asked=mapAsked;
    mapAsked=false;
    return asked;
}
void MapPressed(bool pressed) noexcept {
    if(handoff.phase!=ar::Handoff::asked)return;
    mapDrawn.store(false);
    ar::HandoffPressed(handoff,pressed);
    if(!pressed)EndHandoff("the select button not pressed");
}
void MapDrawn() noexcept { if(holding.load())mapDrawn.store(true); }
void MapShown() noexcept { if(holding.load())mapShown.store(true); }
bool WaitingOverSelect() noexcept { return overSelect.load(); }
bool PaintOverSelect(unsigned char* ctx) noexcept {
    unsigned char* const on=drawOn.load();
    if(!ctx || !overSelect.load() || !on || gone.load()==on || gpuBroken.load())return false;
    Paint(ctx,true);
    return true;
}
void AskOpen(int tab) noexcept { openTab=tab>=0 && tab<kPageCount ? tab : -1; }
int TakeOpenAsk() noexcept {
    const int tab=openTab;
    openTab=-1;
    return tab;
}

void Forget() noexcept {
    DropEdits("forgotten");
    CloseNow("forgotten: a mission starting, the screen left or the plugin off");
    latched=false;
    pvpAsked=false;
    mapAsked=false;
}

void ScreenGone(unsigned char* self) noexcept {
    gone.store(self);
    unsigned char* expected=self;
    if(drawOn.compare_exchange_strong(expected,nullptr)) {
        overSelect.store(false);
        // open on it: the window procedure stops taking the wheel / WM_CHAR now, not at the next HQ / room frame (none
        // runs in a mission: a screen ended under an open 整备 would otherwise eat them through the whole mission)
        wantText.store(false);
        std::lock_guard<std::mutex> hold(gpuLock);
        if(renderer)renderer->Reset();
        if(labels)labels->Reset();
    }
}

}  // namespace crew::armoryui
