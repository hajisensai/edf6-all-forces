// The room / HQ 3D preview of a support soldier or vehicle (room_preview.h; docs/room-3d-preview-re.md plan A). The game's own
// UiHQ_3DModelView (index 0) shows the soldier while the menu asks: its async build's Desc copy is rewritten on the
// worker (path, class, colours, a sentinel outfit, the weapons), so the view builds and draws the soldier as it would the
// player. GameStatus and the save are only read.
//
// Threads: ShowPreview / HidePreview / TickPreview and the view's update / destructor run on the UI thread; every message
// to the view is sent from the view's own update (where it is alive), to the view itself (its OnMessage slot), never
// broadcast (msg 3 broadcast reaches every UI object; 0x8BDE80 is that broadcast). The build hook runs on the worker and
// reads only the override, under `shared.lock`, and the Desc copy it is handed.
#include "room_preview.h"
#include "room_preview_logic.h"
#include "preview_camera.h"
#include "code_signatures.h"
#include "crew.h"
#include "edf/memory.h"
#include "edf/patch.h"
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

// Shared with the worker.
struct Shared {
    SRWLOCK lock=SRWLOCK_INIT;
    const unsigned char* armed=nullptr;   // the adopted view's Async (view +0xD0: made by its constructor, kept for its life)
    bool on=false;
    DescOverride ov;
    wchar_t path[96]{};
    unsigned applied=0;                   // builds the override went into
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
    unsigned sinceTick=0;
    bool menuOpen=false;
    // SetViewPlacement: the rect / camera, and the selection / hidden state forced visible while placed (put back after).
    struct Placement {
        bool on=false,forced=false;
        ViewRect rect;
        ViewCamera camera;
        float aspect=16.f/9.f;
        std::int32_t savedSelect=0,savedHidden=0;
    } place;
    struct Weapon { bool pending=false;std::int32_t slot=0,row=0,cls=0; } weapon;   // ShowPlayerWeapon, sent from the update
} ui;
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
void* __fastcall WorldMatrix(void* obj,const float* m) {
    const auto set=reinterpret_cast<WorldMatrixFn>(image+kSetWorldMatrix);
    if(!ui.place.on || !ui.view)return set(obj,m);
    Matrix4 placed;
    bool ours=false;
    __try {
        unsigned char* scene=At<unsigned char*>(ui.view,kViewScene);
        ours=scene && obj==At<unsigned char*>(scene,kSceneModel)+kModelMatrix;
        if(ours)placed=Place(ui.place.rect,ui.place.camera,FrameOf(ui.want.on ? ui.want.body : VehicleBody::none),
                             At<float>(ui.view,kViewYaw),ui.place.aspect);
    } __except(EXCEPTION_EXECUTE_HANDLER){ours=false;}
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
void SendWeapon(unsigned char* view) noexcept {
    if(!ui.weapon.pending || ui.want.on || ui.plan.locked)return;
    ui.weapon.pending=false;
    if(At<std::int32_t>(view,kViewShownClass)!=ui.weapon.cls)return;   // another class shown: its slots differ
    std::int32_t m[4]={At<std::int32_t>(view,kViewPlayer),ui.weapon.slot,ui.weapon.row,0};
    Send(view,kMsgWeapon,m);
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
bool SetOverride(const PreviewSpec& spec) noexcept {
    DescOverride ov;
    wchar_t file[96],path[96];
    if(spec.body!=VehicleBody::none) {
        if(!VehicleFileOf(spec.body,false,file,std::size(file)) || !VehicleFileOf(spec.body,true,path,std::size(path)))return false;
        static bool said[kVehicleBodyLast+1]{},saidCas=false;
        if(!Present(file,said[static_cast<int>(spec.body)]) || !Present(kJetMenuCas,saidCas))return false;
        ov=VehicleOverride();
    } else {
        if(!SoldierFileOf(spec.kind,spec.leader,false,file,std::size(file)) || !SoldierFileOf(spec.kind,spec.leader,true,path,std::size(path)))
            return false;
        static bool said[kSupportWeaponCount][2]{};
        if(!Present(file,said[static_cast<int>(spec.kind)][spec.leader ? 1 : 0]))return false;
        ov=OverrideOf(spec.kind,spec.leader,spec.look,[](const wchar_t* n) noexcept {return WeaponRow(n);});
    }
    AcquireSRWLockExclusive(&shared.lock);
    shared.on=true;shared.ov=ov;
    wcscpy_s(shared.path,path);
    ReleaseSRWLockExclusive(&shared.lock);
    return true;
}
void ClearOverride() noexcept {
    AcquireSRWLockExclusive(&shared.lock);
    shared.on=false;
    ReleaseSRWLockExclusive(&shared.lock);
}

// --- The worker: the build's Desc copy ---
void Rewrite(unsigned char* lambda,const unsigned char* async) noexcept {
    DescOverride ov;wchar_t path[96];bool apply=false;
    AcquireSRWLockExclusive(&shared.lock);
    if(shared.on && async && async==shared.armed){ov=shared.ov;wcscpy_s(path,shared.path);++shared.applied;apply=true;}
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

bool Pending() noexcept {
    AcquireSRWLockShared(&shared.lock);
    const bool pending=static_cast<int>(ui.posted-shared.applied)>0;
    ReleaseSRWLockShared(&shared.lock);
    return pending;
}
Shown ReadShown(unsigned char* view) noexcept {
    Shown s;
    s.cls=At<std::int32_t>(view,kViewShownClass);
    s.outfit=At<std::int32_t>(view,kViewShownOutfit);
    s.colour[0]=At4(view,kViewShownColour);
    s.colour[1]=At4(view,kViewShownColour+16);
    s.pending=Pending();
    return s;
}
void BuildPlayer(unsigned char* view,std::int32_t who) noexcept {
    unsigned char* gs=GameStatus();
    if(!gs)return;
    BuildMsg m{who,At<std::int32_t>(gs,kGsClass+static_cast<std::size_t>(who)*kGsPlayerStride),
               reinterpret_cast<OutfitFn>(image+kPlayerOutfit)(gs,who)};
    Send(view,kMsgBuild,&m);
}
void Recolour(unsigned char* view,std::int32_t who,int channel) noexcept {
    ColourMsg m{who,ShownClassOf(ui.want),kSentinelOutfit,channel,{}};
    std::memcpy(m.rgba,ui.want.colour[channel].v,sizeof(m.rgba));
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
    for(int ch=0;ch<2;++ch)if(a.recolour[ch])Recolour(view,who,ch);
    if(a.buildPlayer){ClearOverride();BuildPlayer(view,who);}
    if(a.unlock){Send(view,kMsgUnlock,nullptr);Put<std::int32_t>(view,kViewHidden,ui.savedHidden);}
}
void Camera(unsigned char* view) noexcept {
    float* x[4];
    for(int i=0;i<4;++i)x[i]=At4(view,kViewCamera[i]);
    if(ui.menuOpen)ui.camera.Apply(x,kCameraShiftX,ui.want.on ? VehicleScale(ui.want.body) : 1.f);
    else ui.camera.Restore(x);
}
bool Adopt(unsigned char* view) noexcept {
    if(ui.view)return ui.view==view;
    if(At<std::int32_t>(view,kViewPlayer)!=0)return false;
    ui.view=view;
    AcquireSRWLockExclusive(&shared.lock);
    shared.armed=At<const unsigned char*>(view,kViewAsync);
    ReleaseSRWLockExclusive(&shared.lock);
    Log("ROOM preview: view %p (async %p)",static_cast<void*>(view),static_cast<const void*>(shared.armed));
    return true;
}
void Frame(unsigned char* view) noexcept {
    if(!Adopt(view))return;
    if(++ui.sinceTick>kOrphanFrames){ui.want.on=false;ui.menuOpen=false;}
    const Actions a=Step(ui.plan,ui.want,ReadShown(view));
    if(a.Any())Execute(view,a);
    HoldVisible(view);
    SendWeapon(view);
    Camera(view);
}
void Forget() noexcept {
    AcquireSRWLockExclusive(&shared.lock);
    shared.armed=nullptr;shared.on=false;shared.applied=0;
    ReleaseSRWLockExclusive(&shared.lock);
    ui.view=nullptr;ui.plan=Plan{};ui.camera=CameraShift{};ui.turntable=Turntable{};ui.posted=0;
    ui.place.forced=false;ui.weapon.pending=false;
}

std::uintptr_t __fastcall Update(unsigned char* view,void* ctx) {
    bool ours=false;float yaw=0,zoom=0;
    __try {
        Frame(view);
        ours=ui.view==view && ui.menuOpen;
        if(ours){yaw=At<float>(view,kViewYaw);zoom=At<float>(view,kViewZoom);}
        // The menu closed while a vehicle turned: the stand's own yaw back before the view's frame (the player's model).
        else if(ui.view==view && ui.turntable.on)Put<float>(view,kViewYaw,ui.turntable.Hold(At<float>(view,kViewYaw),false));
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("ROOM preview: fault in the view's frame; preview off for this view");Forget();}
    const std::uintptr_t r=nextUpdate(view,ctx);
    // While the menu is open its keys are the menu's: the view's own turn / zoom from them is put back; a vehicle turns
    // slowly on its stand instead (every pylon comes round), and a soldier after it faces where the stand faced before.
    if(ours) {
        yaw=ui.turntable.Hold(yaw,ui.want.on && ui.want.body!=VehicleBody::none);
        Put<float>(view,kViewYaw,yaw);Put<float>(view,kViewZoom,zoom);
    }
    return r;
}
void* __fastcall Delete(unsigned char* view,unsigned flags) {
    if(view==ui.view){Log("ROOM preview: view %p destroyed",static_cast<void*>(view));Forget();}
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
    if(!SetOverride(spec)){ui.want.on=false;return;}
    ui.want.on=true;ui.want.kind=spec.kind;ui.want.leader=spec.leader;ui.want.body=spec.body;
    const StockColours& stock=StockOf(spec.kind,spec.leader);
    ui.want.colour[0]=ColourOf(spec.look.primary,stock.c[0]);
    ui.want.colour[1]=ColourOf(spec.look.secondary,stock.c[1]);
    ui.sinceTick=0;
}
void HidePreview() noexcept { ui.want.on=false; }
float ScreenAspectNow() noexcept { return ScreenAspect(); }
void SetViewPlacement(const ViewRect& rect,const ViewCamera& camera) noexcept {
    ui.place.rect=rect;ui.place.camera=camera;ui.place.aspect=ScreenAspect();ui.place.on=rect.w>0 && rect.h>0;
}
void ClearViewPlacement() noexcept { ui.place.on=false; }
void ShowPlayerWeapon(int cls,int slot,int row) noexcept {
    if(slot<0 || slot>=kWeaponSlots || row<0)return;
    ui.weapon={true,slot,row,cls};
}
void TickPreview(bool menuOpen) noexcept {
    ui.menuOpen=menuOpen;
    ui.sinceTick=0;
}

}  // namespace crew::roompreview
