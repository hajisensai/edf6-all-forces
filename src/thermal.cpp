#include "thermal.h"
#include "thermal_gpu.h"
#include "thermal_models.h"
#include "crew.h"
#include "memory.h"
#include "sight_contacts.h"
#include "firstperson.h"
#include "stores.h"
#include "edf/input_source.h"
#include <d3d11.h>
#include <atomic>
#include <cstring>
#include <iterator>

namespace crew::thermal {
namespace {
constexpr unsigned kViewBeginCall=0x11E223D,kViewBegin=0x11B6340;
constexpr unsigned kViewEndCall=0x11E3AE2,kViewEnd=0x11B63A0;
constexpr unsigned kSubmitCall=0x11E255C,kSubmit=0x11E1630;
constexpr unsigned char kBeginGuard[]={0x4D,0x8D,0x85,0xE0,0,0,0,0x49,0x8B,0x55,0x10,0x48,0x8B,0x0D,0xAB,0x0D,0xED,0,0xE8,0xFE,0x40,0xFD,0xFF};
constexpr unsigned char kEndGuard[]={0x4D,0x8B,0xC6,0x49,0x8B,0x55,0x10,0x48,0x8B,0x0D,0x06,0xF5,0xEC,0,0xE8,0xB9,0x28,0xFD,0xFF};
constexpr unsigned char kSubmitGuard[]={0x41,0xB8,1,0,0,0,0x48,0x8B,0x13,0x49,0x8B,0xCD,0xE8,0xCF,0xF0,0xFF,0xFF};
constexpr unsigned char kStageCode[]={0x48,0x89,0x5C,0x24,8,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0x59,0x30};
using StageFn=void(__fastcall*)(void*,void*,const void*);
using SubmitFn=void(__fastcall*)(void*,void*,int);
using DrawFn=void(__fastcall*)(void*,void*,int,void*);
constexpr int kModels=2048;
constexpr ULONGLONG kFreshMs=1000;
struct Snapshot {
    ULONGLONG at=0;
    bool enabled=false,eligible=false,inSight=false;
    unsigned count=0;
    const void* model[kModels]{};
};
Snapshot published{};
SRWLOCK snapshotLock=SRWLOCK_INIT;
std::atomic<bool> installed{false},eligible{false},inSight{false};
std::atomic<Mode> requested{Mode::optical},effective{Mode::optical};
std::atomic<Reason> reason{Reason::optical};
std::atomic<unsigned> modelCount{0};
ObjRef owner{};unsigned ownerSeat=0;bool keyHeld=true;
// A key edge on a seat with no thermal equipment: the HUD says so for kRejectMs instead of ignoring the press silently.
constexpr ULONGLONG kRejectMs=2500;
std::atomic<ULONGLONG> rejectedAt{0};
ULONGLONG ticked=~ULONGLONG(0);
struct RenderFrame {
    Renderer gpu;
    Snapshot shot;
    const void* view=nullptr;
    std::uint64_t serial=0;
    Mode mode=Mode::optical;
    bool active=false,target=false,failed=false;
};
thread_local RenderFrame frame;

void Publish(const Snapshot& value) noexcept {
    AcquireSRWLockExclusive(&snapshotLock);published=value;ReleaseSRWLockExclusive(&snapshotLock);
    eligible.store(value.eligible,std::memory_order_release);
    inSight.store(value.inSight,std::memory_order_release);
    modelCount.store(value.count,std::memory_order_release);
}
Snapshot ReadSnapshot() noexcept {
    AcquireSRWLockShared(&snapshotLock);const Snapshot value=published;ReleaseSRWLockShared(&snapshotLock);return value;
}
bool MainView(const void* view) noexcept {
    // Native Commander copies these two values from the original camera slot into its frame block.
    return view && At<unsigned>(view,0)==0 && At<unsigned>(view,4)==0;
}
ID3D11DeviceContext* Context(void* native) noexcept {return native ? At<ID3D11DeviceContext*>(native,8) : nullptr;}
bool Target(const void* object) noexcept {
    for(unsigned i=0;i<frame.shot.count;++i)if(frame.shot.model[i]==object)return true;
    return false;
}
void Failed() noexcept {
    frame.failed=true;effective.store(Mode::optical,std::memory_order_release);
    reason.store(Reason::unsupportedSurface,std::memory_order_release);
}
void __fastcall BeginHook(void* manager,void* native,const void* view) {
    reinterpret_cast<StageFn>(image+kViewBegin)(manager,native,view);
    if(!installed.load(std::memory_order_acquire) || !MainView(view))return;
    frame.active=false;frame.target=false;frame.failed=false;frame.view=view;
    frame.shot=ReadSnapshot();frame.mode=requested.load(std::memory_order_acquire);
    if(frame.mode==Mode::optical || !frame.shot.enabled || !frame.shot.eligible || !frame.shot.inSight ||
       GetTickCount64()-frame.shot.at>kFreshMs) {frame.gpu.Reset();return;}
    frame.active=frame.gpu.BeginView(Context(native),++frame.serial);
    if(!frame.active)Failed();
}
void __fastcall EndHook(void* manager,void* native,const void* view) {
    reinterpret_cast<StageFn>(image+kViewEnd)(manager,native,view);
    if(!frame.active || frame.view!=view || !MainView(view))return;
    const Mode now=requested.load(std::memory_order_acquire);
    if(!frame.failed && now==frame.mode) {
        if(frame.gpu.Composite(Context(native),frame.mode==Mode::blackHot ? Polarity::blackHot : Polarity::whiteHot)) {
            effective.store(frame.mode,std::memory_order_release);reason.store(Reason::active,std::memory_order_release);
        } else Failed();
    }
    frame.active=false;frame.target=false;
}
void __fastcall SubmitHook(void* system,void* object,int pass) {
    if(!frame.active || frame.failed || pass!=1 || static_cast<unsigned char*>(system)+0xE0!=frame.view || !Target(object)) {
        reinterpret_cast<SubmitFn>(image+kSubmit)(system,object,pass);return;
    }
    // Native Submit would enqueue this model in its batch. Submit this one object through its ordinary draw slot
    // instead, so only its meshes carry the target scope. No batch pointer, owner, pose or object lifetime changes.
    const bool previous=frame.target;frame.target=true;
    const auto table=At<void* const*>(object,0);
    reinterpret_cast<DrawFn>(table[1])(object,At<void*>(system,0x10),pass,static_cast<unsigned char*>(system)+0xE0);
    frame.target=previous;
}
void Gather(void* context,const void* object,const float*) noexcept {
    auto& shot=*static_cast<Snapshot*>(context);
    const void* models[128]{};
    const std::size_t count=EnemyModels(object,models,std::size(models));
    for(std::size_t k=0;k<count;++k) {
        bool duplicate=false;
        for(unsigned i=0;i<shot.count;++i)if(shot.model[i]==models[k]){duplicate=true;break;}
        if(!duplicate && shot.count<kModels)shot.model[shot.count++]=models[k];
    }
}
bool Seat(unsigned char** vehicle,unsigned* index) noexcept {
    const auto human=PlayerHuman();
    if(!Readable(human,kHumanVehicleCtrl+8) || human[kDead] || HumanOnFoot(human))return false;
    const auto ctrl=At<const unsigned char*>(human,kHumanVehicleCtrl);
    auto v=At<unsigned char*>(human,kHumanVehicleCtrl-8);
    if(!Readable(ctrl,12) || At<int>(ctrl,8)<=0 || !Readable(v,kSeatCount+8) || At<const void*>(v,kSelfCtrl)!=ctrl ||
       v[kDead] || !KnownVehicle(v))return false;
    for(unsigned i=0;i<SeatCount(v);++i) {
        const auto seat=SeatAt(v,i);
        if(SeatRider(seat)==Rider::player && At<const void*>(seat,kSeatRider)==human){*vehicle=v;*index=i;return true;}
    }
    return false;
}
// The configured key held on this keyboard seat, in this game's foreground window.
bool KeyDown(unsigned char* vehicle,unsigned seat,unsigned toggleKey) noexcept {
    const bool keys=At<unsigned char>(SeatAt(vehicle,seat),edf::kSeatPad)==0;
    DWORD process=0;GetWindowThreadProcessId(edf::ForegroundWindow(),&process);
    return keys && toggleKey && toggleKey<=0xFF && process==GetCurrentProcessId() && (edf::AsyncKey(static_cast<int>(toggleKey))&0x8000)!=0;
}
void TickNative(bool enabled,unsigned toggleKey) noexcept {
    Snapshot shot{};shot.at=GetTickCount64();shot.enabled=enabled;
    unsigned char* vehicle=nullptr;unsigned seat=0;
    const bool seated=enabled && Seat(&vehicle,&seat);
    const bool admitted=!GamePaused() && !MapHoldsKeys();
    if(!seated || !VehicleHasThermal(vehicle,seat)) {
        // A seat without the equipment: the press is answered on the HUD (rejectedAt), never turned into a request.
        const bool down=seated && KeyDown(vehicle,seat,toggleKey);
        if(seated && admitted && down && !keyHeld)rejectedAt.store(GetTickCount64(),std::memory_order_release);
        owner={};keyHeld=seated ? down : true;requested.store(Mode::optical,std::memory_order_release);
        effective.store(Mode::optical,std::memory_order_release);
        reason.store(seated ? Reason::unsupportedSeat : Reason::optical,std::memory_order_release);Publish(shot);return;
    }
    if(!owner.Is(vehicle) || ownerSeat!=seat) {
        owner=ObjRef::Of(vehicle);ownerSeat=seat;keyHeld=true;
        requested.store(Mode::optical,std::memory_order_release);effective.store(Mode::optical,std::memory_order_release);
    }
    rejectedAt.store(0,std::memory_order_release);
    shot.eligible=true;
    // Looking through the seat's sight: the gunship gunner's sensor display, the model-authored optic (SightZoomMounted),
    // or this seat's own first-person view (the only view a lens-less eligible seat, an AA gun or an attack heli, has).
    shot.inSight=!MapOwnsView() && ((GunshipCrewSeats(vehicle) && seat==kGunnerSeat) || SightZoomMounted(vehicle) ||
                                    FirstPersonVehicleView(vehicle,seat));
    Publish(shot); // SetMode/CycleMode can now admit this exact seat's request.
    // The key chooses the mode on the equipped seat at any time (2026-10-19: 「切换成像没反应」: an edge outside the sight
    // view was dropped with no change on screen). The image itself is drawn only through the sight; until then the HUD
    // shows the chosen mode and that it waits for the sight.
    const bool down=KeyDown(vehicle,seat,toggleKey);
    if(admitted && down && !keyHeld)CycleMode();
    keyHeld=down;
    const Mode want=requested.load(std::memory_order_acquire);
    if(!shot.inSight) {
        effective.store(Mode::optical,std::memory_order_release);reason.store(Reason::notInSight,std::memory_order_release);
    } else if(want==Mode::optical) {
        effective.store(Mode::optical,std::memory_order_release);reason.store(Reason::optical,std::memory_order_release);
    } else if(!installed.load(std::memory_order_acquire)) {
        effective.store(Mode::optical,std::memory_order_release);reason.store(Reason::hooksUnavailable,std::memory_order_release);
    } else {
        // Into the sight with a mode already chosen: waiting for the GPU's first composite (EndHook: active), or its
        // failure (unsupportedSurface) as the render reported it; not the reason left from outside the sight.
        const Reason was=reason.load(std::memory_order_acquire);
        if(was==Reason::notInSight || was==Reason::optical || was==Reason::hooksUnavailable)
            reason.store(Reason::waitingFrame,std::memory_order_release);
        VisitEnemies(vehicle,&Gather,&shot);Publish(shot);
    }
}
} // namespace

bool Install() noexcept {
    if(installed.load(std::memory_order_acquire))return true;
    __try {
        if(!Matches(0x11E222B,kBeginGuard,sizeof(kBeginGuard)) || !Matches(0x11E3AD4,kEndGuard,sizeof(kEndGuard)) ||
           !Matches(0x11E2550,kSubmitGuard,sizeof(kSubmitGuard)) || !Matches(kViewBegin,kStageCode,sizeof(kStageCode)) ||
           !Matches(kViewEnd,kStageCode,sizeof(kStageCode))) {Log("THERMAL native render profile mismatch; optical retained");return false;}
        bool changed=false;
        const bool ok=RedirectCall(image+kViewBeginCall,image+kViewBegin,reinterpret_cast<void*>(&BeginHook),changed) &&
            RedirectCall(image+kViewEndCall,image+kViewEnd,reinterpret_cast<void*>(&EndHook),changed) &&
            RedirectCall(image+kSubmitCall,image+kSubmit,reinterpret_cast<void*>(&SubmitHook),changed);
        installed.store(ok,std::memory_order_release);Log("THERMAL native world begin/end and individual opaque submission=%d",ok);return ok;
    } __except(EXCEPTION_EXECUTE_HANDLER) {Log("THERMAL profile fault; optical retained");return false;}
}
void Tick(bool enabled,unsigned toggleKey) noexcept {
    const ULONGLONG now=GameFrame();if(ticked==now)return;ticked=now;
    __try {TickNative(enabled,toggleKey);}
    __except(EXCEPTION_EXECUTE_HANDLER) {Reset();reason.store(Reason::unsupportedSeat,std::memory_order_release);}
}
bool SetMode(Mode mode) noexcept {
    if(mode!=Mode::optical && mode!=Mode::whiteHot && mode!=Mode::blackHot)return false;
    // A request needs the equipped seat only; whether it can be drawn now (in the sight, hooks installed) is the
    // reason the HUD reports, never a press silently dropped.
    if(mode!=Mode::optical && !eligible.load(std::memory_order_acquire))return false;
    requested.store(mode,std::memory_order_release);effective.store(Mode::optical,std::memory_order_release);
    const Reason why=mode==Mode::optical ? Reason::optical : !installed.load(std::memory_order_acquire) ? Reason::hooksUnavailable :
        !inSight.load(std::memory_order_acquire) ? Reason::notInSight : Reason::waitingFrame;
    reason.store(why,std::memory_order_release);return true;
}
Mode CycleMode() noexcept {
    const Mode was=requested.load(std::memory_order_acquire);
    const Mode next=was==Mode::optical ? Mode::whiteHot : was==Mode::whiteHot ? Mode::blackHot : Mode::optical;
    return SetMode(next) ? next : was;
}
bool Rejected() noexcept {
    const ULONGLONG at=rejectedAt.load(std::memory_order_acquire);
    return at && GetTickCount64()-at<kRejectMs;
}
Status GetStatus() noexcept {
    return {requested.load(std::memory_order_acquire),effective.load(std::memory_order_acquire),reason.load(std::memory_order_acquire),
        eligible.load(std::memory_order_acquire),installed.load(std::memory_order_acquire),modelCount.load(std::memory_order_acquire),
        Rejected()};
}
void Reset() noexcept {
    Publish(Snapshot{});requested.store(Mode::optical,std::memory_order_release);effective.store(Mode::optical,std::memory_order_release);
    reason.store(Reason::optical,std::memory_order_release);owner={};keyHeld=true;ticked=~ULONGLONG(0);
    rejectedAt.store(0,std::memory_order_release);
}
bool WantsCapture() noexcept {return frame.active && frame.target && !frame.failed;}
void ObserveSurface(ID3D11DeviceContext* context,int pass) noexcept {
    if(frame.active && !frame.failed && pass==1 && !frame.gpu.ObserveSurface(context))Failed();
}
void CaptureIndexed(ID3D11DeviceContext* context,unsigned count,unsigned first,int base) noexcept {
    if(WantsCapture() && !frame.gpu.CaptureIndexed(context,count,first,base))Failed();
}
} // namespace crew::thermal
