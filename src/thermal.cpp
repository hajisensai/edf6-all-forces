#include "thermal.h"
#include "thermal_gpu.h"
#include "thermal_models.h"
#include "crew.h"
#include "memory.h"
#include "sight_contacts.h"
#include "firstperson.h"
#include "stores.h"
#include "edf/input_source.h"
#include "seat_switch_logic.h"
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
// Each local player's own thermal sight (split screen: two players, each their seat, request and image; the second
// player had none while one global state read only PlayerHuman() and the first camera slot's view). Slots are kept by
// the player's object (seatswitch.cpp RiderFor's way): a player not seen for kForgetMs gives theirs up.
constexpr int kPlayers=2;
constexpr ULONGLONG kForgetMs=2000;
struct Player {
    ObjRef human;ULONGLONG seen=0;
    Snapshot published{};
    std::atomic<bool> eligible{false},inSight{false};
    std::atomic<Mode> requested{Mode::optical},effective{Mode::optical};
    std::atomic<Reason> reason{Reason::optical};
    std::atomic<unsigned> modelCount{0};
    std::atomic<ULONGLONG> rejectedAt{0};
    std::atomic<bool> pad{false};
    ObjRef owner{};unsigned ownerSeat=0;bool keyHeld=true,padHeld=true;
};
Player players[kPlayers];
SRWLOCK snapshotLock=SRWLOCK_INIT;
std::atomic<bool> installed{false};
// A key edge on a seat with no thermal equipment: the HUD says so for kRejectMs instead of ignoring the press silently.
constexpr ULONGLONG kRejectMs=2500;
ULONGLONG ticked=~ULONGLONG(0);
struct RenderFrame {
    Renderer gpu;
    Snapshot shot;
    const void* view=nullptr;
    Player* player=nullptr;
    std::uint64_t serial=0;
    Mode mode=Mode::optical;
    bool active=false,target=false,failed=false;
};
thread_local RenderFrame frame;

void Clear(Player& p) noexcept {
    AcquireSRWLockExclusive(&snapshotLock);p.published=Snapshot{};ReleaseSRWLockExclusive(&snapshotLock);
    p.eligible.store(false,std::memory_order_release);p.inSight.store(false,std::memory_order_release);
    p.modelCount.store(0,std::memory_order_release);p.requested.store(Mode::optical,std::memory_order_release);
    p.effective.store(Mode::optical,std::memory_order_release);p.reason.store(Reason::optical,std::memory_order_release);
    p.rejectedAt.store(0,std::memory_order_release);p.pad.store(false,std::memory_order_release);
    p.owner={};p.keyHeld=p.padHeld=true;
}
// Game thread: the player's slot (a free or forgotten one taken for a player not seen before), null when both are held.
Player* PlayerFor(unsigned char* human,ULONGLONG now) noexcept {
    Player* slot=nullptr;
    for(auto& p:players) {
        if(p.human.Is(human)){p.seen=now;return &p;}
        if(!slot && (!p.human || p.human.obj==human || now-p.seen>kForgetMs))slot=&p;
    }
    if(slot){Clear(*slot);slot->human=ObjRef::Of(human);slot->seen=now;}
    return slot;
}
// Any thread: the slot of `human` (its identity only, never read through), null when it has none.
Player* Find(const void* human) noexcept {
    if(!human)return nullptr;
    for(auto& p:players)if(p.human.obj==human)return &p;
    return nullptr;
}
void Publish(Player& p,const Snapshot& value) noexcept {
    AcquireSRWLockExclusive(&snapshotLock);p.published=value;ReleaseSRWLockExclusive(&snapshotLock);
    p.eligible.store(value.eligible,std::memory_order_release);
    p.inSight.store(value.inSight,std::memory_order_release);
    p.modelCount.store(value.count,std::memory_order_release);
}
Snapshot ReadSnapshot(const Player& p) noexcept {
    AcquireSRWLockShared(&snapshotLock);const Snapshot value=p.published;ReleaseSRWLockShared(&snapshotLock);return value;
}
bool MainView(const void* view) noexcept {
    // Native Commander copies these two values from the original camera slot into its frame block.
    return view && At<unsigned>(view,0)==0 && At<unsigned>(view,4)==0;
}
// Render thread: whose world view `view` is. The local player whose camera submitted it (firstperson.cpp
// LocalViewPlayer: split screen's second viewport is its second player's); without that match (the camera step not
// chained, or a frame no camera has submitted yet) the first camera slot's view is the first player's, as before.
Player* ViewPlayer(const void* view) noexcept {
    if(Player* p=Find(LocalViewPlayer(view)))return p;
    return MainView(view) ? Find(PlayerHuman()) : nullptr;
}
ID3D11DeviceContext* Context(void* native) noexcept {return native ? At<ID3D11DeviceContext*>(native,8) : nullptr;}
bool Target(const void* object) noexcept {
    for(unsigned i=0;i<frame.shot.count;++i)if(frame.shot.model[i]==object)return true;
    return false;
}
void Failed() noexcept {
    frame.failed=true;
    if(frame.player) {
        frame.player->effective.store(Mode::optical,std::memory_order_release);
        frame.player->reason.store(Reason::unsupportedSurface,std::memory_order_release);
    }
}
void __fastcall BeginHook(void* manager,void* native,const void* view) {
    reinterpret_cast<StageFn>(image+kViewBegin)(manager,native,view);
    if(!installed.load(std::memory_order_acquire))return;
    Player* const p=ViewPlayer(view);
    if(!p)return;
    frame.active=false;frame.target=false;frame.failed=false;frame.view=view;frame.player=p;
    frame.shot=ReadSnapshot(*p);frame.mode=p->requested.load(std::memory_order_acquire);
    if(frame.mode==Mode::optical || !frame.shot.enabled || !frame.shot.eligible || !frame.shot.inSight ||
       GetTickCount64()-frame.shot.at>kFreshMs) {frame.gpu.Reset();return;}
    frame.active=frame.gpu.BeginView(Context(native),++frame.serial);
    if(!frame.active)Failed();
}
void __fastcall EndHook(void* manager,void* native,const void* view) {
    reinterpret_cast<StageFn>(image+kViewEnd)(manager,native,view);
    if(!frame.active || frame.view!=view || !frame.player)return;
    Player& p=*frame.player;
    const Mode now=p.requested.load(std::memory_order_acquire);
    if(!frame.failed && now==frame.mode) {
        if(frame.gpu.Composite(Context(native),frame.mode==Mode::blackHot ? Polarity::blackHot : Polarity::whiteHot)) {
            p.effective.store(frame.mode,std::memory_order_release);p.reason.store(Reason::active,std::memory_order_release);
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
bool Seat(unsigned char* human,unsigned char** vehicle,unsigned* index) noexcept {
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
// The seat's toggle held: on a keyboard seat the configured key, in this game's foreground window; on a pad seat the
// configured chord of the seat's buttons (ini VehicleThermalButton, seat_switch_logic.h ChordHeld: the same button
// word PayloadReadout.switchButton's LB is read from). Each its own held state (a press is its own edge).
struct Toggle { bool key=false,pad=false; };
Toggle Down(unsigned char* vehicle,unsigned seat,unsigned toggleKey) noexcept {
    const unsigned char* const s=SeatAt(vehicle,seat);
    Toggle t{};
    if(At<unsigned char>(s,edf::kSeatPad)==0) {
        DWORD process=0;GetWindowThreadProcessId(edf::ForegroundWindow(),&process);
        t.key=toggleKey && toggleKey<=0xFF && process==GetCurrentProcessId() && (edf::AsyncKey(static_cast<int>(toggleKey))&0x8000)!=0;
    } else {
        t.pad=seatsw::ChordHeld(At<std::uint16_t>(s,edf::kSeatButtons),static_cast<unsigned>(Cfg().thermalButton));
    }
    return t;
}
// The toggle's press this frame (its key's or its chord's edge); the held states are kept every frame.
bool Pressed(Player& p,const Toggle& t) noexcept {
    const bool press=(t.key && !p.keyHeld) || (t.pad && !p.padHeld);
    p.keyHeld=t.key;p.padHeld=t.pad;
    return press;
}
bool SetModeOf(Player& p,Mode mode) noexcept {
    if(mode!=Mode::optical && mode!=Mode::whiteHot && mode!=Mode::blackHot)return false;
    // A request needs the equipped seat only; whether it can be drawn now (in the sight, hooks installed) is the
    // reason the HUD reports, never a press silently dropped.
    if(mode!=Mode::optical && !p.eligible.load(std::memory_order_acquire))return false;
    p.requested.store(mode,std::memory_order_release);p.effective.store(Mode::optical,std::memory_order_release);
    const Reason why=mode==Mode::optical ? Reason::optical : !installed.load(std::memory_order_acquire) ? Reason::hooksUnavailable :
        !p.inSight.load(std::memory_order_acquire) ? Reason::notInSight : Reason::waitingFrame;
    p.reason.store(why,std::memory_order_release);return true;
}
Mode CycleModeOf(Player& p) noexcept {
    const Mode was=p.requested.load(std::memory_order_acquire);
    const Mode next=was==Mode::optical ? Mode::whiteHot : was==Mode::whiteHot ? Mode::blackHot : Mode::optical;
    return SetModeOf(p,next) ? next : was;
}
void TickPlayer(Player& p,unsigned char* human,bool enabled,unsigned toggleKey) noexcept {
    Snapshot shot{};shot.at=GetTickCount64();shot.enabled=enabled;
    unsigned char* vehicle=nullptr;unsigned seat=0;
    const bool seated=enabled && Seat(human,&vehicle,&seat);
    p.pad.store(seated && At<unsigned char>(SeatAt(vehicle,seat),edf::kSeatPad)!=0,std::memory_order_release);
    // The toggle gives way to the map and the pause, and it chooses a mode only on a seat with the equipment
    // (VehicleHasThermal: the seat's actual optic or sensor display); elsewhere the press is answered on the HUD.
    const bool admitted=!GamePaused() && !MapHoldsKeys();
    const bool equipped=seated && VehicleHasThermal(vehicle,seat);
    if(!equipped) {
        // A seat without the equipment: the press is answered on the HUD (rejectedAt), never turned into a request.
        const Toggle t=seated ? Down(vehicle,seat,toggleKey) : Toggle{true,true};
        if(Pressed(p,t) && admitted && seated)p.rejectedAt.store(GetTickCount64(),std::memory_order_release);
        p.owner={};p.requested.store(Mode::optical,std::memory_order_release);
        p.effective.store(Mode::optical,std::memory_order_release);
        p.reason.store(seated ? Reason::unsupportedSeat : Reason::optical,std::memory_order_release);Publish(p,shot);return;
    }
    if(!p.owner.Is(vehicle) || p.ownerSeat!=seat) {
        p.owner=ObjRef::Of(vehicle);p.ownerSeat=seat;p.keyHeld=p.padHeld=true;
        p.requested.store(Mode::optical,std::memory_order_release);p.effective.store(Mode::optical,std::memory_order_release);
    }
    p.rejectedAt.store(0,std::memory_order_release);
    shot.eligible=true;
    // Looking through the seat's sight: the gunship gunner's sensor display, the model-authored optic (SightZoomMounted),
    // or this seat's own first-person view (the only view a lens-less eligible seat, an AA gun or an attack heli, has).
    shot.inSight=!MapOwnsView() && ((GunshipCrewSeats(vehicle) && seat==kGunnerSeat) || SightZoomMounted(vehicle) ||
                                    FirstPersonVehicleView(vehicle,seat));
    Publish(p,shot); // SetModeOf/CycleModeOf can now admit this exact seat's request.
    // The toggle chooses the mode on the equipped seat at any time (2026-10-19: 「切换成像没反应」: an edge outside the
    // sight view was dropped with no change on screen). The image itself is drawn only through the sight; until then the
    // HUD shows the chosen mode and that it waits for the sight.
    const bool press=Pressed(p,Down(vehicle,seat,toggleKey));
    if(admitted && equipped && press)CycleModeOf(p);
    const Mode want=p.requested.load(std::memory_order_acquire);
    if(!shot.inSight) {
        p.effective.store(Mode::optical,std::memory_order_release);p.reason.store(Reason::notInSight,std::memory_order_release);
    } else if(want==Mode::optical) {
        p.effective.store(Mode::optical,std::memory_order_release);p.reason.store(Reason::optical,std::memory_order_release);
    } else if(!installed.load(std::memory_order_acquire)) {
        p.effective.store(Mode::optical,std::memory_order_release);p.reason.store(Reason::hooksUnavailable,std::memory_order_release);
    } else {
        // Into the sight with a mode already chosen: waiting for the GPU's first composite (EndHook: active), or its
        // failure (unsupportedSurface) as the render reported it; not the reason left from outside the sight.
        const Reason was=p.reason.load(std::memory_order_acquire);
        if(was==Reason::notInSight || was==Reason::optical || was==Reason::hooksUnavailable)
            p.reason.store(Reason::waitingFrame,std::memory_order_release);
        VisitEnemies(vehicle,&Gather,&shot);Publish(p,shot);
    }
}
void TickNative(bool enabled,unsigned toggleKey) noexcept {
    // This machine's players: each local camera's (firstperson.cpp LocalPlayers), else the one PlayerHuman finds.
    unsigned char* humans[kPlayers]{};
    int n=LocalPlayers(humans,kPlayers);
    if(n==0 && (humans[0]=PlayerHuman())!=nullptr)n=1;
    const ULONGLONG now=GetTickCount64();
    for(int i=0;i<n;++i)if(Player* p=PlayerFor(humans[i],now))TickPlayer(*p,humans[i],enabled,toggleKey);
    // A player gone (out of the split screen, the mission over): their slot back to optical, nothing drawn for it.
    for(auto& p:players)if(p.human && now-p.seen>kForgetMs){Clear(p);p.human={};}
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
    __except(EXCEPTION_EXECUTE_HANDLER) {Reset();}
}
bool SetMode(Mode mode,const void* human) noexcept {
    Player* p=Find(human ? human : PlayerHuman());
    return p && SetModeOf(*p,mode);
}
Mode CycleMode(const void* human) noexcept {
    Player* p=Find(human ? human : PlayerHuman());
    return p ? CycleModeOf(*p) : Mode::optical;
}
Status GetStatus(const void* human) noexcept {
    const Player* p=Find(human ? human : PlayerHuman());
    Status s{};s.installed=installed.load(std::memory_order_acquire);
    if(!p)return s;
    const ULONGLONG at=p->rejectedAt.load(std::memory_order_acquire);
    s.requested=p->requested.load(std::memory_order_acquire);s.effective=p->effective.load(std::memory_order_acquire);
    s.reason=p->reason.load(std::memory_order_acquire);s.eligible=p->eligible.load(std::memory_order_acquire);
    s.models=p->modelCount.load(std::memory_order_acquire);s.rejected=at && GetTickCount64()-at<kRejectMs;
    s.pad=p->pad.load(std::memory_order_acquire);
    return s;
}
void Reset() noexcept {
    for(auto& p:players){Clear(p);p.human={};p.seen=0;}
    ticked=~ULONGLONG(0);
}
bool WantsCapture() noexcept {return frame.active && frame.target && !frame.failed;}
void ObserveSurface(ID3D11DeviceContext* context,int pass) noexcept {
    if(frame.active && !frame.failed && pass==1 && !frame.gpu.ObserveSurface(context))Failed();
}
void CaptureIndexed(ID3D11DeviceContext* context,unsigned count,unsigned first,int base) noexcept {
    if(WantsCapture() && !frame.gpu.CaptureIndexed(context,count,first,base))Failed();
}
} // namespace crew::thermal
