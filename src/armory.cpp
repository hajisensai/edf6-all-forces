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
#include "ini_text.h"
#include "loadout_file.h"
#include "loadout_store.h"
#include "room_dialog_logic.h"
#include "room_preview.h"
#include "weapon_equip.h"
#include "weapon_page_logic.h"
#include "worldmap_logic.h"
#include "crew.h"
#include "hudtext.h"
#include "map.h"
#include "memory.h"
#include "ui_gpu.h"
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
        // the right stick: left / right a page's alternate (the weapons page's category), up / down its jump
        constexpr int kStick=16000;
        h[ar::kAltPrev]|=pad.Gamepad.sThumbRX<-kStick;h[ar::kAltNext]|=pad.Gamepad.sThumbRX>kStick;
        h[ar::kJumpPrev]|=pad.Gamepad.sThumbRY>kStick;h[ar::kJumpNext]|=pad.Gamepad.sThumbRY<-kStick;
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

// The support units of one kind, from room_ui's menu (loaded from the ini when the screen opened).
class UnitsPage final : public ar::Page {
public:
    UnitsPage(Tx name,bool squads) noexcept : name(name),squads(squads) {}
    void Bind(const rm::Menu* m) noexcept {
        menu=m;n=0;
        if(!m)return;
        for(int i=0;i<m->unitCount && n<rm::kMostUnits;++i)
            if(rm::IsSquad(m->unit[i])==squads && !rm::IsCrewUnit(m->unit[i]))index[n++]=i;
    }
    const wchar_t* Name() const noexcept override { return hudtext::Tr(name); }
    int Rows(int) const noexcept override { return n; }
    void RowLine(int,int row,ar::Line* out) const noexcept override {
        if(const rm::Unit* u=Unit(row))roomdialog::UnitLine(*u,out->label,ar::kTextMost);
    }
    int Items(const ar::Pos& p) const noexcept override {
        const rm::Unit* u=Unit(p.row);
        if(!u)return 0;
        return squads && u->squad.count>0 ? u->squad.count : 1;
    }
    void ItemLine(const ar::Pos& p,ar::Line* out) const noexcept override {
        const rm::Unit* u=Unit(p.row);
        if(!u)return;
        if(squads && u->squad.count>0){roomdialog::SoldierLine(*u,p.item,out->label,ar::kTextMost);return;}
        ar::CopyText(out->label,ar::kTextMost,hudtext::Tr(Tx::armoryLoadout));
        ar::CopyText(out->value,ar::kTextMost,squads ? hudtext::Tr(Tx::armoryStockLoad) : hudtext::Tr(Tx::armoryReadOnly));
    }
    int Fields(const ar::Pos& p) const noexcept override { return Unit(p.row) ? 1 : 0; }
    void FieldLine(const ar::Pos&,ar::Line* out) const noexcept override { ReadOnly(out); }
private:
    const rm::Unit* Unit(int row) const noexcept {
        return menu && row>=0 && row<n ? &menu->unit[index[row]] : nullptr;
    }
    Tx name;
    bool squads;
    const rm::Menu* menu=nullptr;
    int index[rm::kMostUnits]{};
    int n=0;
};

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
};

// The mission: where missions are picked, and in the room the 对战 pages.
class MissionPage final : public ar::Page {
public:
    void Bind(bool inRoom) noexcept { room=inRoom; }
    const wchar_t* Name() const noexcept override { return hudtext::Tr(Tx::armoryTabMission); }
    int Rows(int) const noexcept override { return room ? 2 : 1; }
    void RowLine(int,int row,ar::Line* out) const noexcept override {
        if(row==0){ar::CopyText(out->label,ar::kTextMost,hudtext::Tr(Tx::armoryWorldMap));out->dim=true;return;}
        ar::CopyText(out->label,ar::kTextMost,hudtext::Tr(Tx::armoryPvp));
    }
    int Items(const ar::Pos& p) const noexcept override { return p.row==1 ? 1 : 0; }
    void ItemLine(const ar::Pos&,ar::Line* out) const noexcept override {
        ar::CopyText(out->label,ar::kTextMost,hudtext::Tr(Tx::armoryPvp));
    }
    int Fields(const ar::Pos& p) const noexcept override { return p.row==1 ? 1 : 0; }
    void FieldLine(const ar::Pos&,ar::Line* out) const noexcept override {
        ar::CopyText(out->label,ar::kTextMost,hudtext::Tr(Tx::armoryPvpOpen));
        out->acts=true;
    }
    bool Activate(const ar::Pos& p) noexcept override {
        if(p.row!=1)return false;
        asked=true;
        return true;
    }
    bool asked=false;
private:
    bool room=false;
};

// --- The UI thread's state ---
UnitsPage squadPage(Tx::armoryTabSquad,true),vehiclePage(Tx::armoryTabVehicle,false);
WeaponsGame weaponsGame;
wlist::Catalog weaponCatalog;
ar::wpage::Texts weaponTexts;
ar::wpage::WeaponsPage weaponsPage;
MissionPage missionPage;
ar::Page* const kPages[]={&weaponsPage,&squadPage,&vehiclePage,&missionPage};
constexpr int kWeaponsTab=0;
constexpr int kPageCount=static_cast<int>(std::size(kPages));

ar::State state;
ar::Presets presets;
ar::Words words;
ar::View building;            // big: not on the stack
unsigned char* openOn=nullptr;
bool latched=false;           // closed with one of its keys still held: the screen's input stays taken until they are up
bool pvpAsked=false;

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

// The weapons page's 3D view each frame: the weapon under the cursor in the player's hands, the view in the page's hole
// (room_preview SetViewPlacement) while it has one; the game's own placement back otherwise.
void WeaponsView(bool active) noexcept {
    weaponsPage.Tick(state.pos,state.column,active);
    ar::skin::Rect hole{};
    if(active && weaponsPage.Hole(state.pos,&hole)) {
        roompreview::SetViewPlacement(roompreview::ViewRect{hole.x0,hole.y0,hole.x1-hole.x0,hole.y1-hole.y0},
                                      roompreview::PresetOf(roompreview::CameraPreset::front));
    } else roompreview::ClearViewPlacement();
}

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
    static constexpr Tx kSuggest[]={Tx::armorySuggest1,Tx::armorySuggest2,Tx::armorySuggest3,Tx::armorySuggest4};
    words.nsuggest=static_cast<int>(std::size(kSuggest));
    for(int i=0;i<words.nsuggest;++i)words.suggest[i]=hudtext::Tr(kSuggest[i]);
    FillWeaponTexts();
}

void Publish(bool open) noexcept {
    if(open)ar::Build(state,kPages,kPageCount,presets,words,&building);
    else building.Clear();
    building.open=open;
    {
        std::lock_guard<std::mutex> hold(viewLock);
        std::memcpy(static_cast<void*>(&published),&building,sizeof(ar::View));
    }
    drawOn.store(open ? openOn : nullptr);
}

void CloseNow(const char* why) noexcept {
    if(!state.open && !openOn)return;
    ar::Close(state);
    WeaponsView(false);
    wantText.store(false);
    Publish(false);
    Log("ARMORY closed (%s)",why);
    openOn=nullptr;
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
void Paint(unsigned char* ctx) noexcept {
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
    if(ctx && self && drawOn.load()==self && gone.load()!=self && !gpuBroken.load())Paint(ctx);
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

void Open(unsigned char* self,bool room,const roommenu::Menu* units) noexcept {
    if(!installed || !self)return;
    Subclass();
    FillWords();
    squadPage.Bind(units);
    vehiclePage.Bind(units);
    try {
        weaponsGame.built=wequip::BuildCatalog(&weaponCatalog);
    } catch(...) { weaponsGame.built=false; }
    if(!weaponsGame.built)Log("ARMORY the weapons page: the weapon table is not loaded (or weapon_equip is off)");
    weaponsPage.Attach(&weaponsGame,&weaponCatalog,&weaponTexts);
    weaponsPage.SetFavourites(ReadWeaponFavourites());
    missionPage.Bind(room);
    missionPage.asked=false;
    pvpAsked=false;
    wchar_t why[160]{};
    if(!armorypresets::Load(why,std::size(why))) {
        wchar_t note[ar::kTextMost];
        _snwprintf_s(note,_TRUNCATE,hudtext::Tr(Tx::armoryPresetsOff),why);
        ar::Note(state,note);
    }
    armorypresets::Fill(&presets);
    openOn=self;
    gone.store(nullptr);
    latched=true;
    ar::Open(state,ReadInput(true),state.tab);
    wantText.store(true);
    Publish(true);
    Log("ARMORY opened on the %s screen %p (%d presets)",room ? "room" : "HQ",static_cast<void*>(self),presets.count);
}
bool IsOpen() noexcept { return state.open; }

bool Frame(unsigned char* self,bool hasInput,bool toggleDown) noexcept {
    if(state.open && (self!=openOn || gone.load()==openOn)){CloseNow("its screen changed or ended");latched=false;}
    if(!state.open) {
        if(!latched)return false;
        latched=AnyHeld(ReadInput(toggleDown));
        return latched;
    }
    if(!hasInput) {   // something of the game's opened over the screen (a dialog, a sub-screen, a transition)
        for(ar::Page* p:kPages)p->Commit(nullptr,0);
        CloseNow("the screen lost its input");
        latched=false;
        return false;
    }
    FillWords();
    const ar::Input in=ReadInput(toggleDown);
    const ar::Effects fx=ar::Step(state,kPages,kPageCount,presets,words,in);
    if(fx.op!=ar::Op::none) {
        wchar_t why[160]{};
        if(!armorypresets::Apply(fx,why,std::size(why))) {
            Log("ARMORY preset op %d refused: %ls",static_cast<int>(fx.op),why);
            ar::Note(state,why);
        } else if(fx.op==ar::Op::switchTo)weaponsPage.Refresh();
        armorypresets::Fill(&presets);
    }
    if(missionPage.asked){missionPage.asked=false;pvpAsked=true;ar::Close(state);}
    WeaponsView(state.open && state.tab==kWeaponsTab && state.popup==ar::Popup::none);
    if(!state.open) {
        CloseNow(pvpAsked ? "the 对战 pages asked" : "its own keys");
        latched=AnyHeld(in);
        return true;   // this frame's keys were ours
    }
    Publish(true);
    return true;
}

bool TakePvpRequest() noexcept {
    const bool asked=pvpAsked;
    pvpAsked=false;
    return asked;
}

void Forget() noexcept {
    CloseNow("forgotten: a mission starting, the screen left or the plugin off");
    latched=false;
    pvpAsked=false;
}

void ScreenGone(unsigned char* self) noexcept {
    gone.store(self);
    unsigned char* expected=self;
    if(drawOn.compare_exchange_strong(expected,nullptr)) {
        std::lock_guard<std::mutex> hold(gpuLock);
        if(renderer)renderer->Reset();
        if(labels)labels->Reset();
    }
}

}  // namespace crew::armoryui
