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
#include "player_slots.h"
#include "room_dialog_logic.h"
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

// The player's class and weapons now (GameStatus), one row per class.
class PlayerPage final : public ar::Page {
public:
    void Refresh() noexcept { have=armorypresets::ReadPlayer(&player); }
    const wchar_t* Name() const noexcept override { return hudtext::Tr(Tx::armoryTabPlayer); }
    int Rows(int) const noexcept override { return have ? playerslots::kClasses : 1; }
    void RowLine(int,int row,ar::Line* out) const noexcept override {
        if(!have){ar::CopyText(out->label,ar::kTextMost,hudtext::Tr(Tx::armoryNoPlayer));out->dim=true;return;}
        static constexpr Tx kNames[]={Tx::classRanger,Tx::classWingDiver,Tx::armoryClassAirRaider,Tx::classFencer};
        ar::CopyText(out->label,ar::kTextMost,hudtext::Tr(kNames[row]));
        if(row==player.cls)ar::CopyText(out->value,ar::kTextMost,L"◆");
    }
    int Items(const ar::Pos& p) const noexcept override { return have ? playerslots::SlotsOf(p.row) : 0; }
    void ItemLine(const ar::Pos& p,ar::Line* out) const noexcept override {
        _snwprintf_s(out->label,ar::kTextMost,_TRUNCATE,hudtext::Tr(Tx::armorySlot),p.item+1);
        const std::string& w=player.weapon[p.row][p.item];
        if(w.empty())ar::CopyText(out->value,ar::kTextMost,hudtext::Tr(Tx::armoryEmpty));
        else Utf16(w,out->value,ar::kTextMost);
    }
    int Fields(const ar::Pos& p) const noexcept override { return have && p.item>=0 ? 1 : 0; }
    void FieldLine(const ar::Pos&,ar::Line* out) const noexcept override { ReadOnly(out); }
private:
    crew::preset::Player player;
    bool have=false;
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
UnitsPage squadPage(Tx::armoryTabSquad,true),vehiclePage(Tx::armoryTabVehicle,false);
PlayerPage playerPage;
MissionPage missionPage;
ar::Page* const kPages[]={&squadPage,&vehiclePage,&playerPage,&missionPage};
constexpr int kPageCount=static_cast<int>(std::size(kPages));

ar::State state;
ar::Presets presets;
ar::Words words;
ar::View building;            // big: not on the stack
unsigned char* openOn=nullptr;
bool latched=false;           // closed with one of its keys still held: the screen's input stays taken until they are up
bool pvpAsked=false;
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
    static constexpr Tx kSuggest[]={Tx::armorySuggest1,Tx::armorySuggest2,Tx::armorySuggest3,Tx::armorySuggest4};
    words.nsuggest=static_cast<int>(std::size(kSuggest));
    for(int i=0;i<words.nsuggest;++i)words.suggest[i]=hudtext::Tr(kSuggest[i]);
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

void Open(unsigned char* self,bool room,const roommenu::Menu* units,int tab) noexcept {
    if(!installed || !self)return;
    Subclass();
    FillWords();
    squadPage.Bind(units);
    vehiclePage.Bind(units);
    playerPage.Refresh();
    missionPage.Bind(room);
    missionPage.asked=false;
    missionPage.mapAsked=false;
    pvpAsked=false;
    mapAsked=false;
    openRoom=room;
    if(tab>=0 && tab<kPageCount)state.tab=tab;
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
    const int tabWas=state.tab;
    const ar::Effects fx=ar::Step(state,kPages,kPageCount,presets,words,in);
    // The HQ's 任务 tab is the world map itself (its page has nothing else there): chosen, the map opens. In the room the
    // page keeps its 对战 row; its world map row opens the map.
    if(state.open && !openRoom && tabWas!=state.tab && kPages[state.tab]==&missionPage)missionPage.mapAsked=true;
    if(fx.op!=ar::Op::none) {
        wchar_t why[160]{};
        if(!armorypresets::Apply(fx,why,std::size(why))) {
            Log("ARMORY preset op %d refused: %ls",static_cast<int>(fx.op),why);
            ar::Note(state,why);
        } else if(fx.op==ar::Op::switchTo)playerPage.Refresh();
        armorypresets::Fill(&presets);
    }
    if(missionPage.asked){missionPage.asked=false;pvpAsked=true;ar::Close(state);}
    if(missionPage.mapAsked){missionPage.mapAsked=false;mapAsked=true;ar::Close(state);}
    if(!state.open) {
        CloseNow(pvpAsked ? "the 对战 pages asked" : mapAsked ? "the world map asked" : "its own keys");
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
bool TakeMapRequest() noexcept {
    const bool asked=mapAsked;
    mapAsked=false;
    return asked;
}
void AskOpen(int tab) noexcept { openTab=tab>=0 && tab<kPageCount ? tab : -1; }
int TakeOpenAsk() noexcept {
    const int tab=openTab;
    openTab=-1;
    return tab;
}

void Forget() noexcept {
    CloseNow("forgotten: a mission starting, the screen left or the plugin off");
    latched=false;
    pvpAsked=false;
    mapAsked=false;
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
