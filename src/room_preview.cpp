// The room / HQ 3D preview of a support soldier or vehicle (room_preview.h; docs/room-3d-preview-re.md plan A). The game's own
// UiHQ_3DModelView (index 0) shows the soldier while the menu asks: its async build's Desc copy is rewritten on the
// worker (path, class, colours, a sentinel outfit, the weapons), so the view builds and draws the soldier as it would the
// player. GameStatus and the save are only read.
//
// Threads: ShowPreview / HidePreview / TickPreview and the view's update / destructor run on the UI thread; every message
// to the view is sent from the view's own update (where it is alive), to the view itself (its OnMessage slot), never
// broadcast (msg 3 broadcast reaches every UI object; 0x8BDE80 is that broadcast). The build hook runs on the worker and
// reads only the override, under `shared.lock`, and the Desc copy it is handed.
//
// The squad (section 6.3): soldiers 1..3 of a squad each in a UiHQ_3DModelView made here, inside the index-0 view's
// update, as HUiHQ3DModel 0x8782C0 makes its own (operator new 0x450, createInfo, constructor 0x938760: the UI system
// at view +0x48 holds the only strong ref, ours is dropped at once), each with its own override armed on its own Async
// (so the game's msg 3 / 6 broadcasts to it build the soldier too), killed by 0x11A4450 (the system's next frame drops
// its ref; the deleting destructor runs through the hook here). Not in HUiHQ3DModel's list +0x770: its close does not
// kill them, so they are killed here when the squad ends, the placement ends, TickPreview stops (the index-0 view's
// orphan count) or the index-0 view goes.
#include "room_preview.h"
#include "room_preview_logic.h"
#include "preview_camera.h"
#include "code_signatures.h"
#include "crew.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <cstddef>
#include <cwchar>
#include <iterator>

namespace crew::roompreview {
namespace {
// EDF.dll 0x678CCB46 RVAs.
constexpr std::size_t kViewVtable=0x180FA60;     // UiHQ_3DModelView: slot 1 update 0x93CE30, 3 OnMessage 0x93B320, 5 deleting dtor 0x93A2C0
constexpr std::size_t kSlotUpdate=1,kSlotMessage=3,kSlotDelete=5;
constexpr std::size_t kBuildVtable1=0x180FDB0;   // OnMessage lambda_1 (msg 3) std::function impl: slot 2 _Do_call 0x9424A0
constexpr std::size_t kBuildVtable3=0x180FE20;   // lambda_3 (msg 6): slot 2 0x942590; both call 0x93F910(Async+0x28, this+8)
constexpr std::size_t kSlotDoCall=2,kLambdaDesc=8;
constexpr std::size_t kGameStatus=0x20B2890;     // GS*
constexpr std::size_t kAssign=0x3D450;           // std::wstring::assign(this, const wchar_t*, size_t)
constexpr std::size_t kWeaponRow=0xE1BA0;        // (GS+0x130, const wchar_t* name) -> WeaponTable row, -1
constexpr std::size_t kPlayerOutfit=0xD8700;     // (GS, player) -> outfit
constexpr std::size_t kGsTables=0x130,kGsClass=0x6E90,kGsPlayerStride=0x3E60;
// The view.
constexpr std::size_t kViewAsync=0xD0,kViewPlayer=0x2AC,kViewShownColour=0x158,kViewShownOutfit=0x178,kViewShownClass=0x180,
    kViewYaw=0x2A4,kViewZoom=0x308,kViewHidden=0x3EC;
constexpr std::size_t kViewCamera[4]={0x2C0,0x2D0,0x2E0,0x2F0};
constexpr unsigned kMsgBuild=3,kMsgColour=5,kMsgLock=8,kMsgUnlock=9;
// The unified model file (docs/unified-model-re.md): the model slot load 0x93AA00 asks 0x6BF380 for "animation_model" at
// this call; the hook asks for kMenuModelKey instead when the document has it.
constexpr std::size_t kMenuModelCall=0x93AD30,kModelLoad=0x6BF380,kFindMember=0x179EBA8,kDocTag=0x10;
// The placement (docs/room-3d-preview-re.md section 6.2): the update hands the model matrix to SetWorldMatrix at this
// call, on the object [[view+0x120]+0x88]+0xA0; msg 4 a player weapon {player, slot, row, 0}; +0x304 the msg 7 selection
// (2: another player's, the update clears +0x30C and Draw skips the view).
constexpr std::size_t kWorldMatrixCall=0x93D1A1,kSetWorldMatrix=0x1100B90,kViewScene=0x120,kSceneModel=0x88,kModelMatrix=0xA0,
    kViewSelect=0x304;
constexpr unsigned kMsgWeapon=4;
constexpr std::int32_t kSelectOther=2,kSelectNone=0;
constexpr int kWeaponSlots=4;
// The squad's views (section 6.3).
constexpr std::size_t kOperatorNew=0x12D85B0,kViewConstruct=0x938760,kViewCreateInfo=0x1800508,kKill=0x11A4450;
constexpr std::size_t kViewSize=0x450,kNodeFlags=0x18,kNodeSystem=0x48;
constexpr std::uint32_t kNodeKilled=1;
constexpr std::int32_t kHiddenDrawn=0,kHiddenSkipped=1;

using UpdateFn=std::uintptr_t(__fastcall*)(unsigned char*,void*);
using DeleteFn=void*(__fastcall*)(unsigned char*,unsigned);
using MessageFn=bool(__fastcall*)(unsigned char*,unsigned,void*);
using DoCallFn=void(__fastcall*)(unsigned char*,unsigned char*);
using AssignFn=void*(__fastcall*)(void*,const wchar_t*,std::size_t);
using RowFn=int(__fastcall*)(void*,const wchar_t*);
using OutfitFn=int(__fastcall*)(void*,int);
UpdateFn nextUpdate=nullptr;
DeleteFn nextDelete=nullptr;
DoCallFn nextBuild1=nullptr,nextBuild3=nullptr;

// Shared with the worker. Slot 0 the adopted index-0 view's, 1..3 the squad's views made here.
struct Armed {
    const unsigned char* async=nullptr;   // the view's Async (view +0xD0: made by its constructor, kept for its life)
    bool on=false;
    DescOverride ov;
    wchar_t path[96]{};
    unsigned applied=0;                   // builds the override went into
};
struct Shared {
    SRWLOCK lock=SRWLOCK_INIT;
    Armed slot[kSquadMost];
} shared;

// The UI thread's.
struct Ui {
    unsigned char* view=nullptr;
    Want want;
    Plan plan;
    CameraShift camera;
    Turntable turntable;         // a vehicle's turn; the stand's yaw put back after it
    std::int32_t savedHidden=0;
    unsigned posted=0;           // soldier builds asked for (shared.applied catches up)
    float sinceTick=0.f;         // 60 Hz frames since TickPreview (by the UI's dt)
    bool menuOpen=false;
    // SetViewPlacement: the rect / camera, and the selection / hidden state forced visible while placed (put back after).
    struct Placement {
        bool on=false,forced=false;
        ViewRect rect;
        ViewCamera camera;
        float aspect=16.f/9.f;
        std::int32_t savedSelect=0,savedHidden=0;
    } place;
    // ShowPlayerWeapon, sent from the update: one per slot (a preset switch shows all four in one frame), the latest
    // row of a slot wins.
    struct Weapon { bool pending=false;std::int32_t row=0,cls=0; } weapon[kWeaponSlots];
    // The placement drew the model (WorldMatrix: placedMatrix below) at view update `drawnAt` (PreviewPlacement).
    unsigned frames=0,drawnAt=0;
    bool drawn=false;
    float placedAspect=16.f/9.f;
    // The squad: ShowSquad's soldiers; extra[1..3] the views made here (extra[0] unused: soldier 0 is the index-0 view's).
    struct Extra {
        unsigned char* view=nullptr;
        Want want;               // on: the soldier's file is there (SetOverride), so the view is wanted
        SquadPlan plan;
        unsigned posted=0;       // builds asked for (shared.slot[i].applied catches up)
        float yaw=0;             // the stand's yaw held while it lives (the view reads the turn input as well)
        bool yawKept=false;
    } extra[kSquadMost];
    bool squadOn=false;
    int squadCount=0;
    bool squadBroken=false;      // a fault while making / running a made view: no more made this session
    unsigned char* updating=nullptr;   // the view whose update runs now (WorldMatrix is called inside it)
} ui;
Matrix4 placedMatrix;   // Ui's, apart: its 16-byte alignment would pad Ui
bool installed=false;

using Signature=CodeSignature;
#include "room_preview_signatures.inc"

bool CheckCode() noexcept { return CheckSignatures(kSignatures,"ROOM preview"); }

unsigned char* GameStatus() noexcept { return At<unsigned char*>(image,kGameStatus); }
float* At4(unsigned char* view,std::size_t off) noexcept { return reinterpret_cast<float*>(view+off); }

bool Send(unsigned char* view,unsigned id,void* payload) noexcept {
    const auto vt=At<void* const*>(view,0);
    return reinterpret_cast<MessageFn>(vt[kSlotMessage])(view,id,payload);
}

// --- The placement: the model matrix the update hands SetWorldMatrix (UI thread, inside the view's update) ---
using WorldMatrixFn=void*(__fastcall*)(void*,const float*);
ModelFrame FrameOf(VehicleBody body) noexcept {
    // A soldier ~1.8 m with its feet at the origin; a vehicle framed by the size VehicleScale gives it (L, as there).
    const float scale=body==VehicleBody::none ? 1.f : VehicleScale(body);
    return {1.8f*scale,0.9f*scale};
}
// The squad's columns now (0: no squad, the index-0 view fills the rect).
int SquadColumns() noexcept {
    return ui.squadOn && ui.place.on && ui.squadCount>=2 ? (ui.squadCount>kSquadMost ? kSquadMost : ui.squadCount) : 0;
}
// Which view `v` is: 0 the index-0 view, 1..3 a squad view made here, -1 neither.
int SlotOf(const unsigned char* v) noexcept {
    if(!v)return -1;
    if(v==ui.view)return 0;
    for(int i=1;i<kSquadMost;++i)if(ui.extra[i].view==v)return i;
    return -1;
}
// The update hook names the view whose update runs (ui.updating): its model matrix placed in its column. Only the
// index-0 view's placed matrix is kept for PreviewPlacement (the callouts are a vehicle's, never a made squad view's).
void* __fastcall WorldMatrix(void* obj,const float* m) {
    const auto set=reinterpret_cast<WorldMatrixFn>(image+kSetWorldMatrix);
    unsigned char* view=ui.updating;
    const int slot=SlotOf(view);
    if(!ui.place.on || slot<0)return set(obj,m);
    Matrix4 placed;
    bool ours=false;
    __try {
        unsigned char* scene=At<unsigned char*>(view,kViewScene);
        ours=scene && obj==At<unsigned char*>(scene,kSceneModel)+kModelMatrix;
        const int columns=SquadColumns();
        if(ours && slot>0 && columns<=slot)ours=false;   // a made view past the squad's columns (killed this frame)
        if(ours)placed=Place(columns ? SquadRectOf(ui.place.rect,columns,slot) : ui.place.rect,ui.place.camera,
                             FrameOf(slot==0 && ui.want.on ? ui.want.body : VehicleBody::none),At<float>(view,kViewYaw),
                             ui.place.aspect);
    } __except(EXCEPTION_EXECUTE_HANDLER){ours=false;}
    if(ours && slot==0){placedMatrix=placed;ui.placedAspect=ui.place.aspect;ui.drawnAt=ui.frames;ui.drawn=true;}
    return set(obj,ours ? &placed.m[0][0] : m);
}
// The screen's aspect: the process's visible top-level window (the swap chain fills its client area; M).
BOOL CALLBACK FindGameWindow(HWND w,LPARAM out) {
    DWORD pid=0;
    GetWindowThreadProcessId(w,&pid);
    if(pid!=GetCurrentProcessId() || !IsWindowVisible(w) || GetWindow(w,GW_OWNER))return TRUE;
    *reinterpret_cast<HWND*>(out)=w;
    return FALSE;
}
float ScreenAspect() noexcept {
    HWND w=nullptr;
    EnumWindows(&FindGameWindow,reinterpret_cast<LPARAM>(&w));
    RECT r{};
    if(!w || !GetClientRect(w,&r) || r.right<=0 || r.bottom<=0)return 16.f/9.f;
    return static_cast<float>(r.right)/static_cast<float>(r.bottom);
}
// While placed the view stays drawn under another screen (the armory covering the HQ): its selection not "another
// player's" (+0x304 2) and its hidden state not 1 (Draw skips it). Both put back when the placement ends.
void HoldVisible(unsigned char* view) noexcept {
    if(ui.place.on) {
        if(!ui.place.forced){ui.place.savedSelect=At<std::int32_t>(view,kViewSelect);ui.place.savedHidden=At<std::int32_t>(view,kViewHidden);ui.place.forced=true;}
        if(At<std::int32_t>(view,kViewSelect)==kSelectOther)Put<std::int32_t>(view,kViewSelect,kSelectNone);
        if(At<std::int32_t>(view,kViewHidden)==1)Put<std::int32_t>(view,kViewHidden,0);
    } else if(ui.place.forced) {
        Put<std::int32_t>(view,kViewSelect,ui.place.savedSelect);
        if(!ui.plan.locked)Put<std::int32_t>(view,kViewHidden,ui.place.savedHidden);
        ui.place.forced=false;
    }
}
// ShowPlayerWeapon: msg 4 to the player's own model only (a support soldier shown: its weapons are the override's).
// The only sender of msg 4 in the plugin (weapon_equip.cpp writes the slot, its callers tell the view here).
void SendWeapon(unsigned char* view) noexcept {
    if(ui.want.on || ui.plan.locked)return;
    const std::int32_t shown=At<std::int32_t>(view,kViewShownClass);
    for(std::int32_t slot=0;slot<kWeaponSlots;++slot){
        Ui::Weapon& w=ui.weapon[slot];
        if(!w.pending)continue;
        w.pending=false;
        if(shown!=w.cls)continue;   // another class shown: its slots differ
        std::int32_t m[4]={At<std::int32_t>(view,kViewPlayer),slot,w.row,0};
        Send(view,kMsgWeapon,m);
    }
}

// --- The unified model file: the menu reads menu_animation_model (worker thread; reads the document only) ---
struct FindQuery { const wchar_t* key; std::int32_t index; };
using FindFn=void(__fastcall*)(const unsigned char*,FindQuery*);
using ModelLoadFn=bool(__fastcall*)(void*,void*,unsigned char*,const wchar_t*,int);
bool __fastcall MenuModelLoad(void* model,void* arg,unsigned char* doc,const wchar_t* key,int flag) {
    const std::uint16_t tag=doc ? *reinterpret_cast<const std::uint16_t*>(doc+kDocTag) : 0xFFFF;
    if(tag==kDocClassic || tag==kDocDsgo) {
        FindQuery q{kMenuModelKey,-1};
        reinterpret_cast<FindFn const*>(image+kFindMember)[tag](doc,&q);
        if(q.index!=-1)key=kMenuModelKey;
    }
    return reinterpret_cast<ModelLoadFn>(image+kModelLoad)(model,arg,doc,key,flag);
}

// --- What the menu asks for ---
bool Generated(const wchar_t* file) noexcept {
    wchar_t path[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));
    wchar_t* slash=n && n<std::size(path) ? wcsrchr(path,L'\\') : nullptr;
    if(!slash)return false;
    *slash=0;
    if(wcscat_s(path,L"\\Mods\\") || wcscat_s(path,file))return false;
    const DWORD a=GetFileAttributesW(path);
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);
}
// Mods\<file> read whole into `out` (at most `cap` bytes); -1: not there, unreadable or larger.
long ReadModsFile(const wchar_t* file,char* out,std::size_t cap) noexcept {
    wchar_t path[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));
    wchar_t* slash=n && n<std::size(path) ? wcsrchr(path,L'\\') : nullptr;
    if(!slash)return -1;
    *slash=0;
    if(wcscat_s(path,L"\\Mods\\") || wcscat_s(path,file))return -1;
    const HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return -1;
    LARGE_INTEGER size{};
    DWORD got=0;
    const bool ok=GetFileSizeEx(h,&size) && size.QuadPart>=0 && size.QuadPart<=static_cast<LONGLONG>(cap) &&
        ReadFile(h,out,static_cast<DWORD>(size.QuadPart),&got,nullptr) && got==static_cast<DWORD>(size.QuadPart);
    CloseHandle(h);
    return ok ? static_cast<long>(got) : -1;
}
int WeaponRow(const wchar_t* name) noexcept {
    __try {
        unsigned char* gs=GameStatus();
        return gs ? reinterpret_cast<RowFn>(image+kWeaponRow)(gs+kGsTables,name) : -1;
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
}
// Logs once per unit that its unified file (or the jets' menu CAS) is not in Mods: the install is broken, that unit is
// not previewed (no stock stand-in: docs/unified-model-re.md).
bool Present(const wchar_t* file,bool& said) noexcept {
    if(Generated(file))return true;
    if(!said){said=true;Log("ROOM preview: Mods\\%ls missing (install the plugin again): not previewed",file);}
    return false;
}
// False: nothing to show (a tank / mech base, which is no unified file; or a unified file not in Mods).
bool SetOverride(int slot,const PreviewSpec& spec) noexcept {
    DescOverride ov;
    wchar_t file[96],path[96];
    if(spec.body!=VehicleBody::none) {
        if(!VehicleFileOf(spec.body,spec.load,false,file,std::size(file)) || !VehicleFileOf(spec.body,spec.load,true,path,std::size(path)))
            return false;
        // A loaded variant not there yet (edited, not generated) is the page's to say (support_pages.h FileThere).
        static bool said[kVehicleBodyLast+1]{},saidCas=false;
        if(!spec.load.On() && !Present(file,said[static_cast<int>(spec.body)]))return false;
        if(spec.load.On() && !Generated(file))return false;
        if(VehicleJet(spec.body) && !Present(kJetMenuCas,saidCas))return false;
        ov=VehicleOverride();
    } else {
        if(!SoldierFileOf(spec.kind,spec.leader,false,file,std::size(file)) || !SoldierFileOf(spec.kind,spec.leader,true,path,std::size(path)))
            return false;
        static bool said[kSupportWeaponCount][2]{};
        if(!Present(file,said[static_cast<int>(spec.kind)][spec.leader ? 1 : 0]))return false;
        ov=OverrideOf(spec.kind,spec.leader,spec.look,[](const wchar_t* n) noexcept {return WeaponRow(n);});
    }
    AcquireSRWLockExclusive(&shared.lock);
    Armed& a=shared.slot[slot];
    a.on=true;a.ov=ov;
    wcscpy_s(a.path,path);
    ReleaseSRWLockExclusive(&shared.lock);
    return true;
}
void ClearOverride(int slot) noexcept {
    AcquireSRWLockExclusive(&shared.lock);
    shared.slot[slot].on=false;
    ReleaseSRWLockExclusive(&shared.lock);
}
// Arms slot `slot` on a view's Async (null: disarmed, its override off); its applied count starts again.
void Arm(int slot,const unsigned char* async) noexcept {
    AcquireSRWLockExclusive(&shared.lock);
    shared.slot[slot].async=async;shared.slot[slot].applied=0;
    if(!async)shared.slot[slot].on=false;
    ReleaseSRWLockExclusive(&shared.lock);
}
void WantOf(const PreviewSpec& spec,Want& w) noexcept {
    w.on=true;w.kind=spec.kind;w.leader=spec.leader;w.body=spec.body;
    const StockColours& stock=StockOf(spec.kind,spec.leader);
    w.colour[0]=ColourOf(spec.look.primary,stock.c[0]);
    w.colour[1]=ColourOf(spec.look.secondary,stock.c[1]);
}

// --- The worker: the build's Desc copy ---
void Rewrite(unsigned char* lambda,const unsigned char* async) noexcept {
    DescOverride ov;wchar_t path[96];bool apply=false;
    AcquireSRWLockExclusive(&shared.lock);
    for(Armed& a:shared.slot)
        if(a.on && async && async==a.async){ov=a.ov;wcscpy_s(path,a.path);++a.applied;apply=true;break;}
    ReleaseSRWLockExclusive(&shared.lock);
    if(!apply)return;
    __try {
        unsigned char* desc=lambda+kLambdaDesc;
        RewriteDesc(desc,ov);
        reinterpret_cast<AssignFn>(image+kAssign)(desc+kDescPath,path,wcslen(path));
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
void __fastcall Build1(unsigned char* lambda,unsigned char* async) { Rewrite(lambda,async);nextBuild1(lambda,async); }
void __fastcall Build3(unsigned char* lambda,unsigned char* async) { Rewrite(lambda,async);nextBuild3(lambda,async); }

// --- The view's frame: the plan carried out ---
struct BuildMsg { std::int32_t player,cls,outfit; };
struct ColourMsg { std::int32_t player,cls,outfit,channel; float rgba[4]; };

bool Pending(int slot,unsigned posted) noexcept {
    AcquireSRWLockShared(&shared.lock);
    const bool pending=static_cast<int>(posted-shared.slot[slot].applied)>0;
    ReleaseSRWLockShared(&shared.lock);
    return pending;
}
Shown ReadShown(unsigned char* view,int slot,unsigned posted) noexcept {
    Shown s;
    s.cls=At<std::int32_t>(view,kViewShownClass);
    s.outfit=At<std::int32_t>(view,kViewShownOutfit);
    s.colour[0]=At4(view,kViewShownColour);
    s.colour[1]=At4(view,kViewShownColour+16);
    s.pending=Pending(slot,posted);
    return s;
}
void BuildPlayer(unsigned char* view,std::int32_t who) noexcept {
    unsigned char* gs=GameStatus();
    if(!gs)return;
    BuildMsg m{who,At<std::int32_t>(gs,kGsClass+static_cast<std::size_t>(who)*kGsPlayerStride),
               reinterpret_cast<OutfitFn>(image+kPlayerOutfit)(gs,who)};
    Send(view,kMsgBuild,&m);
}
void Recolour(unsigned char* view,const Want& want,std::int32_t who,int channel) noexcept {
    ColourMsg m{who,ShownClassOf(want),kSentinelOutfit,channel,{}};
    std::memcpy(m.rgba,want.colour[channel].v,sizeof(m.rgba));
    Send(view,kMsgColour,&m);
}
void Execute(unsigned char* view,const Actions& a) noexcept {
    const std::int32_t who=At<std::int32_t>(view,kViewPlayer);
    if(a.lock) {
        ui.savedHidden=At<std::int32_t>(view,kViewHidden);
        Put<std::int32_t>(view,kViewHidden,0);   // drawn (1: Draw skips it; 2 / 3 its fades)
        Send(view,kMsgLock,nullptr);
    }
    if(a.buildSoldier){BuildMsg m{who,ShownClassOf(ui.want),0};++ui.posted;Send(view,kMsgBuild,&m);}
    for(int ch=0;ch<2;++ch)if(a.recolour[ch])Recolour(view,ui.want,who,ch);
    if(a.buildPlayer){ClearOverride(0);BuildPlayer(view,who);}
    if(a.unlock){Send(view,kMsgUnlock,nullptr);Put<std::int32_t>(view,kViewHidden,ui.savedHidden);}
}
void Camera(unsigned char* view) noexcept {
    float* x[4];
    for(int i=0;i<4;++i)x[i]=At4(view,kViewCamera[i]);
    if(ui.menuOpen)ui.camera.Apply(x,kCameraShiftX,ui.want.on ? VehicleScale(ui.want.body) : 1.f);
    else ui.camera.Restore(x);
}
// The index-0 view the game made (not one of ours; not a killed one, which still runs its update this frame).
bool Adopt(unsigned char* view) noexcept {
    if(ui.view)return ui.view==view;
    if(At<std::int32_t>(view,kViewPlayer)!=0 || SlotOf(view)>0 || (At<std::uint32_t>(view,kNodeFlags)&kNodeKilled))return false;
    ui.view=view;
    const unsigned char* async=At<const unsigned char*>(view,kViewAsync);
    Arm(0,async);
    Log("ROOM preview: view %p (async %p)",static_cast<void*>(view),static_cast<const void*>(async));
    return true;
}

// --- The squad's views ---
struct CreateInfo {               // HUiHQ3DModel's, built at 0x8783B1 (rbp-0x80)
    const void* type;             // +0x00 the createInfo's type (image 0x1800508)
    void* system;                 // +0x08 the UI system (HQ +0x48; the view keeps it at +0x48)
    void* resource;               // +0x10 / +0x18 optional, null as the HQ passes them
    void* layout;
    void* out;                    // +0x20 the strong shared_ptr the base constructor writes
    std::int32_t count,index;     // +0x28 / +0x2C: the view's +0x2A8 / +0x2AC
    std::uint8_t first;           // +0x30 1 as the HQ passes it (0x7E3220(view, 4, 2))
    std::uint8_t pad[7];
};
static_assert(offsetof(CreateInfo,out)==0x20 && offsetof(CreateInfo,count)==0x28 && offsetof(CreateInfo,first)==0x30,
              "HUiHQ3DModel's createInfo");
struct alignas(16) StrongRef { unsigned char* obj; unsigned char* ctrl; };
using NewFn=void*(__fastcall*)(std::size_t);
using ConstructFn=void*(__fastcall*)(void*,CreateInfo*);
using KillFn=void(__fastcall*)(unsigned char*);
using CtrlFn=void(__fastcall*)(unsigned char*);
// The MSVC control block's strong ref dropped as 0x87843E drops it (uses +8; at 0 _Destroy, then the weaks +0xC).
void DropStrong(unsigned char* ctrl) noexcept {
    if(!ctrl)return;
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(ctrl+8))!=0)return;
    void* const* vt=At<void* const*>(ctrl,0);
    reinterpret_cast<CtrlFn>(vt[0])(ctrl);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(ctrl+0xC))==0)reinterpret_cast<CtrlFn>(vt[1])(ctrl);
}
// One view made beside `main`, inside main's update (the system's lock +0x60 is free there: the new view waits in its
// pending list +0x18 until the next frame, 0x119D270). Null when it could not be made.
unsigned char* MakeView(unsigned char* main) noexcept {
    unsigned char* view=nullptr;
    __try {
        void* system=At<void*>(main,kNodeSystem);
        if(!system)return nullptr;
        void* mem=reinterpret_cast<NewFn>(image+kOperatorNew)(kViewSize);
        if(!mem)return nullptr;
        StrongRef out{};
        CreateInfo info{image+kViewCreateInfo,system,nullptr,nullptr,&out,1,0,1,{}};
        reinterpret_cast<ConstructFn>(image+kViewConstruct)(mem,&info);
        view=static_cast<unsigned char*>(mem);
        // hidden until its own build is the soldier (its constructor posted player 0's: room_preview_logic.h SquadStep)
        Put<std::int32_t>(view,kViewHidden,kHiddenSkipped);
        DropStrong(out.ctrl);   // the system's list holds its own (0x119CDA0)
    } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
    return view;
}
void KillView(unsigned char* view) noexcept {
    __try { reinterpret_cast<KillFn>(image+kKill)(view); } __except(EXCEPTION_EXECUTE_HANDLER){}
}
void Drop(int i) noexcept {
    Ui::Extra& e=ui.extra[i];
    Arm(i,nullptr);
    e.view=nullptr;e.plan=SquadPlan{};e.posted=0;e.yawKept=false;
}
void KillExtra(int i) noexcept {
    unsigned char* view=ui.extra[i].view;
    if(!view)return;
    Log("ROOM preview: squad view %d %p killed",i,static_cast<void*>(view));
    Drop(i);
    KillView(view);
}
void KillExtras() noexcept { for(int i=1;i<kSquadMost;++i)KillExtra(i); }
// From the index-0 view's frame: the made views as the squad asks.
void Squad(unsigned char* main) noexcept {
    const int wanted=ui.squadBroken ? 0 :
        ExtraViewsWanted(ui.squadOn && ui.menuOpen,ui.squadCount,ui.place.on,ui.want.on && ui.want.body==VehicleBody::none);
    for(int i=1;i<kSquadMost;++i) {
        Ui::Extra& e=ui.extra[i];
        if(i>wanted || !e.want.on){KillExtra(i);continue;}
        if(e.view)continue;
        unsigned char* view=MakeView(main);
        if(!view) {
            ui.squadBroken=true;
            Log("ROOM preview: making a squad view failed; the squad shows its first soldier only");
            KillExtras();
            return;
        }
        Drop(i);
        e.view=view;
        const unsigned char* async=At<const unsigned char*>(view,kViewAsync);
        Arm(i,async);
        Log("ROOM preview: squad view %d %p made (async %p)",i,static_cast<void*>(view),static_cast<const void*>(async));
    }
}
// A made view's frame (its own update): its soldier built, recoloured, drawn once shown.
void ExtraFrame(unsigned char* view,int i) noexcept {
    Ui::Extra& e=ui.extra[i];
    const SquadActions a=SquadStep(e.plan,e.want,ReadShown(view,i,e.posted));
    if(a.build){BuildMsg m{0,ShownClassOf(e.want),0};++e.posted;Send(view,kMsgBuild,&m);}
    for(int ch=0;ch<2;++ch)if(a.recolour[ch])Recolour(view,e.want,0,ch);
    Put<std::int32_t>(view,kViewHidden,a.reveal ? kHiddenDrawn : kHiddenSkipped);
    if(At<std::int32_t>(view,kViewSelect)==kSelectOther)Put<std::int32_t>(view,kViewSelect,kSelectNone);
}

void Frame(unsigned char* view,float frames) noexcept {
    if(!Adopt(view))return;
    ++ui.frames;
    if((ui.sinceTick+=frames)>static_cast<float>(kOrphanFrames)){ui.want.on=false;ui.menuOpen=false;}
    const Actions a=Step(ui.plan,ui.want,ReadShown(view,0,ui.posted));
    if(a.Any())Execute(view,a);
    HoldVisible(view);
    SendWeapon(view);
    Camera(view);
    Squad(view);
}
void Forget() noexcept {
    KillExtras();   // the index-0 view gone (HUiHQ3DModel closed) or faulted: the made views with it
    Arm(0,nullptr);
    ui.view=nullptr;ui.plan=Plan{};ui.camera=CameraShift{};ui.turntable=Turntable{};ui.posted=0;
    ui.place.forced=false;ui.drawn=false;
    for(auto& w:ui.weapon)w.pending=false;
}

// A made view's update: its frame, the game's, then the menu's keys' turn / zoom put back (it reads them as well).
std::uintptr_t ExtraUpdate(unsigned char* view,void* ctx,int slot) {
    float yaw=0,zoom=0;
    bool hold=false;
    __try {
        ExtraFrame(view,slot);
        Ui::Extra& e=ui.extra[slot];
        if(!e.yawKept){e.yaw=At<float>(view,kViewYaw);e.yawKept=true;}
        yaw=e.yaw;zoom=At<float>(view,kViewZoom);hold=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("ROOM preview: fault in squad view %d's frame; the squad shows its first soldier only",slot);
        ui.squadBroken=true;
        KillExtra(slot);
    }
    ui.updating=view;
    const std::uintptr_t r=nextUpdate(view,ctx);
    ui.updating=nullptr;
    if(hold && ui.extra[slot].view==view){Put<float>(view,kViewYaw,yaw);Put<float>(view,kViewZoom,zoom);}
    return r;
}
// The UI update's context {.., ui::Input*, float dt} (worldmap.cpp kCtxDt): this frame's length in 60 Hz frames.
float FramesOf(const void* ctx) noexcept {
    float dt=0.f;
    __try { dt=*reinterpret_cast<const float*>(static_cast<const unsigned char*>(ctx)+0x10); } __except(EXCEPTION_EXECUTE_HANDLER){ dt=0.f; }
    return dt>0.f && dt<0.25f ? dt*60.f : 1.f;
}

std::uintptr_t __fastcall Update(unsigned char* view,void* ctx) {
    const int slot=SlotOf(view);
    if(slot>0)return ExtraUpdate(view,ctx,slot);
    bool ours=false;float yaw=0,zoom=0;
    const float frames=ctx ? FramesOf(ctx) : 1.f;
    __try {
        Frame(view,frames);
        ours=ui.view==view && ui.menuOpen;
        if(ours){yaw=At<float>(view,kViewYaw);zoom=At<float>(view,kViewZoom);}
        // The menu closed while a vehicle turned: the stand's own yaw back before the view's frame (the player's model).
        else if(ui.view==view && ui.turntable.on)Put<float>(view,kViewYaw,ui.turntable.Hold(At<float>(view,kViewYaw),false));
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("ROOM preview: fault in the view's frame; preview off for this view");Forget();}
    ui.updating=view;
    const std::uintptr_t r=nextUpdate(view,ctx);
    ui.updating=nullptr;
    // While the menu is open its keys are the menu's: the view's own turn / zoom from them is put back; a vehicle turns
    // slowly on its stand instead (every pylon comes round), and a soldier after it faces where the stand faced before.
    if(ours) {
        yaw=ui.turntable.Hold(yaw,ui.want.on && ui.want.body!=VehicleBody::none,frames);
        Put<float>(view,kViewYaw,yaw);Put<float>(view,kViewZoom,zoom);
    }
    return r;
}
void* __fastcall Delete(unsigned char* view,unsigned flags) {
    const int slot=SlotOf(view);
    if(slot==0){Log("ROOM preview: view %p destroyed",static_cast<void*>(view));Forget();}
    else if(slot>0){Log("ROOM preview: squad view %d %p destroyed unkilled",slot,static_cast<void*>(view));Drop(slot);}
    return nextDelete(view,flags);
}

bool Chain(std::size_t vtable,std::size_t slot,void* hook,void** next) noexcept {
    return edf::ChainVtableSlot(reinterpret_cast<void**>(image+vtable)+slot,hook,next);
}
bool Hook() noexcept {
    bool changed=false;
    if(!edf::RedirectCall(image+kMenuModelCall,image+kModelLoad,reinterpret_cast<void*>(&MenuModelLoad),changed))return false;
    if(!edf::RedirectCall(image+kWorldMatrixCall,image+kSetWorldMatrix,reinterpret_cast<void*>(&WorldMatrix),changed))return false;
    // The build hooks first: armed stays null (no rewrite) until the view's update adopts a view.
    if(!Chain(kBuildVtable1,kSlotDoCall,reinterpret_cast<void*>(&Build1),reinterpret_cast<void**>(&nextBuild1)))return false;
    if(!Chain(kBuildVtable3,kSlotDoCall,reinterpret_cast<void*>(&Build3),reinterpret_cast<void**>(&nextBuild3)))return false;
    if(!Chain(kViewVtable,kSlotDelete,reinterpret_cast<void*>(&Delete),reinterpret_cast<void**>(&nextDelete)))return false;
    return Chain(kViewVtable,kSlotUpdate,reinterpret_cast<void*>(&Update),reinterpret_cast<void**>(&nextUpdate));
}
}  // namespace

bool InstallPreviewHooks() noexcept {
    if(installed)return true;
    if(!image || !CheckCode())return false;
    if(!Hook()){Log("ROOM preview REFUSED: a view vtable slot changed under us (partly hooked: inert until a view is adopted)");return false;}
    installed=true;
    Log("ROOM preview: hooks installed");
    return true;
}
void ShowPreview(const PreviewSpec& spec) noexcept {
    if(!installed || static_cast<int>(spec.kind)<0 || static_cast<int>(spec.kind)>=kSupportWeaponCount)return;
    if(!SetOverride(0,spec)){ui.want.on=false;return;}
    WantOf(spec,ui.want);
    ui.sinceTick=0;
}
void HidePreview() noexcept { ui.want.on=false;HideSquad(); }
void ShowSquad(const SquadSpec& squad) noexcept {
    if(!installed)return;
    if(squad.count<2){HideSquad();return;}
    ui.squadOn=true;
    ui.squadCount=squad.count>kSquadMost ? kSquadMost : squad.count;
    for(int i=1;i<kSquadMost;++i) {
        Ui::Extra& e=ui.extra[i];
        const PreviewSpec& spec=squad.soldier[i];
        const bool soldier=i<ui.squadCount && spec.body==VehicleBody::none && static_cast<int>(spec.kind)>=0 &&
                           static_cast<int>(spec.kind)<kSupportWeaponCount;
        if(!soldier || !SetOverride(i,spec)){e.want.on=false;continue;}
        WantOf(spec,e.want);
    }
}
void HideSquad() noexcept { ui.squadOn=false;ui.squadCount=0; }
bool PreviewFileThere(const wchar_t* modsFile) noexcept { return Generated(modsFile); }
bool PreviewPlacement(Matrix4* matrix,float* aspect) noexcept {
    // Drawn in this or the last view update (the armory draws between two), the vehicle the menu asks for.
    if(!ui.drawn || !ui.place.on || !ui.view || !ui.want.on || ui.want.body==VehicleBody::none || ui.frames-ui.drawnAt>1)
        return false;
    *matrix=placedMatrix;*aspect=ui.placedAspect;
    return true;
}
bool PreviewCallouts(const wchar_t* modsFile,menucallouts::Callouts* out) noexcept {
    static wchar_t last[96]{};
    static menucallouts::Callouts cached;
    static bool ok=false;
    if(!modsFile || wcslen(modsFile)>=std::size(last))return false;
    if(_wcsicmp(last,modsFile)!=0) {
        wcscpy_s(last,modsFile);
        static char text[menucallouts::kFileMost];
        wchar_t file[128]{};
        const long n=menucallouts::FileOf(modsFile,file,std::size(file)) ? ReadModsFile(file,text,sizeof(text)) : -1;
        ok=n>=0 && menucallouts::Parse(text,static_cast<std::size_t>(n),&cached);
        if(!ok)Log("ROOM preview: Mods\\%ls missing or not a callouts file (install the plugin again): callouts unplaced",file);
    }
    if(ok)*out=cached;
    return ok;
}
float ScreenAspectNow() noexcept { return ScreenAspect(); }
void SetViewPlacement(const ViewRect& rect,const ViewCamera& camera) noexcept {
    ui.place.rect=rect;ui.place.camera=camera;ui.place.aspect=ScreenAspect();ui.place.on=rect.w>0 && rect.h>0;
}
void ClearViewPlacement() noexcept { ui.place.on=false; }
void ShowPlayerWeapon(int cls,int slot,int row) noexcept {
    if(slot<0 || slot>=kWeaponSlots || row<0)return;
    ui.weapon[slot]={true,row,cls};
}
void TickPreview(bool menuOpen) noexcept {
    ui.menuOpen=menuOpen;
    ui.sinceTick=0.f;
}

}  // namespace crew::roompreview
