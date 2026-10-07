// The vehicle sight's magnification (src/sightzoom.h; docs/zoom-re.md): the toggle, kept per seat the player sits at
// (SightZoomFrame, from the seat's own frame: a stock vehicle's input, the gunship gunner's GunnerFrame), and the
// camera: the player's CharacterGhostCamera's per-frame step (vtable 0x1768C10 slot 4, 0xF86A0; map.cpp hooks the same
// slot, chained) runs, then its field of view (cam+0x24) is scaled by the magnification; its previous write is retracted before the next step.
//  - Only the player's own camera (its target the player's soldier, as map.cpp tells it), only while they ride (the
//    soldier's vehicle, +0x1550), never while the map or the Tempest's TV holds the view; the magnification published
//    by a seat's frame within kCueMs: when the player gets out, the vehicle is wrecked or gone, SightZoom or the plugin
//    is switched off, the view is restored on the next camera step, including its no-target branch.
//  - A new seat or vehicle starts at 1x; a new mission too (ResetSightZoom).
//  - On a pad the button is R3 by default; where the high view is offered (highcam.cpp, R3 too) the high view keeps it.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "layout.h"
#include "sightzoom.h"
#include "edf/patch.h"
#include "memory.h"

namespace crew {
namespace {
constexpr unsigned kCamVtable=0x1768C10,kCamStep=0xF86A0;
constexpr std::size_t kCamStepSlot=4,kCamTargetRef=0x350,kCamTarget=0x360,kCamFov=0x24;
// The stock field of view's write in the step (movss xmm0,[pi/4]; divss xmm0,[rdi+410h]; movss [rdi+24h],xmm0).
constexpr unsigned kFovWrite=0xF8906;
const unsigned char kFovWriteCode[]={0xF3,0x0F,0x10,0x05,0x12,0xD1,0x66,0x01,0xF3,0x0F,0x5E,0x87,0x10,0x04,0x00,0x00,0xF3,0x0F,0x11,0x47,0x24};
const unsigned char kCamStepCode[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x90};
// The seat's input (docs/stores-re.md §4): 1 = a pad (0 keyboard and mouse), the pad's button bits.
constexpr std::size_t kSeatPad=0x2B0,kSeatButtons=0x2E8;
constexpr ULONGLONG kCueMs=200;

using CamStepFn=void(__fastcall*)(void*,void*);
CamStepFn nextCamStep=nullptr;
bool installed=false;

struct Toggle { ObjRef ref,human; const void* v; unsigned seat; int step; bool held; ULONGLONG at; };   // at: its last frame
Toggle toggle{};
// Publish the entire seat identity together: a camera must not combine another seat's zoom and timestamp.
struct Cue { ObjRef vehicle,human; unsigned seat; float zoom; ULONGLONG at; };
Cue cue{};
SRWLOCK cueLock=SRWLOCK_INIT;
// Camera-thread state only. The original step may leave FOV untouched when its target expires.
struct Applied { void* camera; float before,written; } applied{};

Cue Snapshot() noexcept {
    AcquireSRWLockShared(&cueLock);
    const Cue c=cue;
    ReleaseSRWLockShared(&cueLock);
    return c;
}

bool Current(const Cue& c) noexcept {
    if(!Cfg().enabled || !Cfg().sightZoom || !c.at || GetTickCount64()-c.at>kCueMs)return false;
    const unsigned char* human=PlayerHuman();
    if(!human || !c.human.Is(human) || human[kDead])return false;
    const auto ctrl=At<const unsigned char*>(human,kHumanVehicleCtrl);
    if(!Readable(ctrl,16) || At<int>(ctrl,8)==0)return false;
    const auto v=At<unsigned char*>(human,kHumanVehicleCtrl-8);
    if(!c.vehicle.Is(v) || v[kDead] || c.seat>=SeatCount(v))return false;
    const unsigned char* seat=SeatAt(v,c.seat);
    return SeatRider(seat)==Rider::player && At<const void*>(seat,kSeatRider)==human;
}

bool KeyHeld(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;   // the map view holds the player's keys (map.cpp)
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}

void Publish(unsigned char* v,unsigned seat,float zoom) noexcept {
    const Cue next{ObjRef::Of(v),ObjRef::Of(PlayerHuman()),seat,zoom,GetTickCount64()};
    AcquireSRWLockExclusive(&cueLock);
    cue=next;
    ReleaseSRWLockExclusive(&cueLock);
}

void __fastcall CamStepHook(void* cam,void* step) {
    // Retract only our own write before the original step; it may not write FOV on every branch.
    // Never dereference a remembered camera: only the camera passed to this invocation is live.
    __try {
        auto c=static_cast<unsigned char*>(cam);
        if(applied.camera==cam && At<float>(c,kCamFov)==applied.written)
            *reinterpret_cast<float*>(c+kCamFov)=applied.before;
        if(applied.camera==cam)applied=Applied{};
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    nextCamStep(cam,step);
    if(MapOwnsView())return;
    const Cue state=Snapshot();
    __try {
        auto c=static_cast<unsigned char*>(cam);
        if(!Current(state) || !(state.zoom>1.0f))return;
        const void* human=state.human.obj;
        if(At<const void*>(c,kCamTargetRef)!=human && At<const void*>(c,kCamTarget)!=human)return;
        const float before=At<float>(c,kCamFov);
        if(!std::isfinite(before) || !(before>0.0f))return;
        const float written=before/state.zoom;
        applied=Applied{cam,before,written};
        *reinterpret_cast<float*>(c+kCamFov)=written;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

}  // namespace

bool InstallSightZoom() noexcept {
    if(installed)return true;
    if(!Matches(kCamStep,kCamStepCode,sizeof(kCamStepCode)) || !Matches(kFovWrite,kFovWriteCode,sizeof(kFovWriteCode))) {
        Log("SIGHTZOOM the camera's field of view is not set where docs/zoom-re.md has it: no sight zoom");
        return false;
    }
    auto slot=reinterpret_cast<void**>(image+kCamVtable)+kCamStepSlot;
    if(!edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&CamStepHook),reinterpret_cast<void**>(&nextCamStep))){Log("SIGHTZOOM the camera's step could not be hooked");return false;}
    installed=true;
    return true;
}

void SightZoomFrame(unsigned char* v,unsigned seat,bool padButton) noexcept {
    const Config& c=Cfg();
    if(!installed || !c.enabled || !c.sightZoom){ResetSightZoom();return;}
    if(!v || v[kDead] || seat>=SeatCount(v))return;
    const unsigned char* s=SeatAt(v,seat);
    if(SeatRider(s)!=Rider::player || At<const void*>(s,kSeatRider)!=PlayerHuman())return;
    const ULONGLONG now=GetTickCount64();
    // A seat just taken (another one, or this one again after a break): 1x, a key held while boarding no press.
    if(!toggle.ref.Is(v) || !toggle.human.Is(PlayerHuman()) || toggle.seat!=seat || now-toggle.at>kCueMs)
        toggle=Toggle{ObjRef::Of(v),ObjRef::Of(PlayerHuman()),v,seat,0,true,now};
    toggle.at=now;
    const bool keys=At<unsigned char>(s,kSeatPad)==0;
    const bool down=keys ? KeyHeld(c.sightZoomKey)
                         : padButton && c.sightZoomButton && (At<std::uint16_t>(s,kSeatButtons)&static_cast<std::uint16_t>(c.sightZoomButton))!=0;
    if(MapHoldsKeys()){toggle.held=true;Publish(v,seat,sightzoom::At(toggle.step));return;}
    if(down && !toggle.held) {
        toggle.step=sightzoom::Next(toggle.step);
        Log("SIGHTZOOM v=%p seat %u: %.0fx by the %s",v,seat,sightzoom::At(toggle.step),keys ? "key" : "pad button");
    }
    toggle.held=down;
    Publish(v,seat,sightzoom::At(toggle.step));
}

void SightZoomStock(unsigned char* v) noexcept {
    if(!Cfg().enabled || !Cfg().sightZoom){ResetSightZoom();return;}
    if(v[kDead]){if(toggle.ref.Is(v))ResetSightZoom();return;}
    const unsigned n=SeatCount(v);
    for(unsigned i=0;i<n;++i) {
        if(SeatRider(SeatAt(v,i))!=Rider::player || At<const void*>(SeatAt(v,i),kSeatRider)!=PlayerHuman())continue;
        SightZoomFrame(v,i,!(i==0 && HighCamOffered(v) && (Cfg().highCamButton & Cfg().sightZoomButton)!=0));
        return;
    }
    if(toggle.ref.Is(v))ResetSightZoom();
}

float SightZoomNow(const void* vehicle) noexcept {
    const Cue c=Snapshot();
    __try {
        if((vehicle && c.vehicle.obj!=vehicle) || !Current(c))return 1.0f;
        return c.zoom;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 1.0f;}
}

void ResetSightZoom() noexcept {
    toggle=Toggle{};
    AcquireSRWLockExclusive(&cueLock);
    cue=Cue{};
    ReleaseSRWLockExclusive(&cueLock);
    // The camera hook still owns its last FOV write until that camera next steps.
}

}  // namespace crew
