// The vehicle sight's magnification (src/sightzoom.h; docs/zoom-re.md): the toggle, kept per seat the player sits at
// (SightZoomFrame, from the seat's own frame: a stock vehicle's input, the gunship gunner's GunnerFrame), and the
// camera: the player's CharacterGhostCamera's per-frame step (vtable 0x1768C10 slot 4, 0xF86A0; map.cpp hooks the same
// slot, chained) runs, then its field of view (cam+0x24) is set again with the magnification (sightzoom::Fov).
//  - Only the player's own camera (its target the player's soldier, as map.cpp tells it), only while they ride (the
//    soldier's vehicle, +0x1550), never while the map or the Tempest's TV holds the view; the magnification published
//    by a seat's frame within kCueMs: when the player gets out, the vehicle is wrecked or gone, SightZoom or the plugin
//    is switched off, the frames stop and the view is the stock one the next frame (the game sets its field of view
//    afresh every frame: nothing to put back).
//  - A new seat or vehicle starts at 1x; a new mission too (ResetSightZoom).
//  - On a pad the button is R3 by default; where the high view is offered (highcam.cpp, R3 too) the high view keeps it.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "layout.h"
#include "sightzoom.h"
#include "edf/patch.h"
#include <atomic>

namespace crew {
namespace {
constexpr unsigned kCamVtable=0x1768C10,kCamStep=0xF86A0;
constexpr std::size_t kCamStepSlot=4,kCamTargetRef=0x350,kCamTarget=0x360,kCamFov=0x24,kCamZoom=0x410;
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

struct Toggle { ObjRef ref; const void* v; unsigned seat; int step; bool held; ULONGLONG at; };   // at: its last frame
Toggle toggle{};
// The magnification as the seat's frame left it (game thread), read by the camera's step: its bits and when.
std::atomic<float> cueZoom{1.0f};
std::atomic<const void*> cueVehicle{nullptr};
std::atomic<ULONGLONG> cueAt{0};

bool KeyHeld(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;   // the map view holds the player's keys (map.cpp)
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}

void Publish(const void* v,float zoom) noexcept {
    cueZoom.store(zoom,std::memory_order_relaxed);
    cueVehicle.store(v,std::memory_order_relaxed);
    cueAt.store(GetTickCount64(),std::memory_order_release);
}

void __fastcall CamStepHook(void* cam,void* step) {
    nextCamStep(cam,step);
    const float zoom=SightZoomNow(nullptr);
    if(!(zoom>1.0f) || MapOwnsView())return;
    __try {
        auto c=static_cast<unsigned char*>(cam);
        unsigned char* const human=PlayerHuman();
        if(!human || (At<const void*>(c,kCamTargetRef)!=human && At<const void*>(c,kCamTarget)!=human))return;
        if(!At<const void*>(human,kHumanVehicleCtrl))return;   // on foot: the soldier's own scope is the stock one
        *reinterpret_cast<float*>(c+kCamFov)=sightzoom::Fov(At<float>(c,kCamZoom),zoom);
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
    void* next=nullptr;
    if(!edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&CamStepHook),&next)){Log("SIGHTZOOM the camera's step could not be hooked");return false;}
    nextCamStep=reinterpret_cast<CamStepFn>(next);
    installed=true;
    return true;
}

void SightZoomFrame(unsigned char* v,unsigned seat,bool padButton) noexcept {
    const Config& c=Cfg();
    if(!installed || !c.enabled || !c.sightZoom)return;
    const unsigned char* s=SeatAt(v,seat);
    if(!s)return;
    const ULONGLONG now=GetTickCount64();
    // A seat just taken (another one, or this one again after a break): 1x, a key held while boarding no press.
    if(!toggle.ref.Is(v) || toggle.seat!=seat || now-toggle.at>kCueMs)toggle=Toggle{ObjRef::Of(v),v,seat,0,true,now};
    toggle.at=now;
    const bool keys=At<unsigned char>(s,kSeatPad)==0;
    const bool down=keys ? KeyHeld(c.sightZoomKey)
                         : padButton && c.sightZoomButton && (At<std::uint16_t>(s,kSeatButtons)&static_cast<std::uint16_t>(c.sightZoomButton))!=0;
    if(down && !toggle.held) {
        toggle.step=sightzoom::Next(toggle.step);
        Log("SIGHTZOOM v=%p seat %u: %.0fx by the %s",v,seat,sightzoom::At(toggle.step),keys ? "key" : "pad button");
    }
    toggle.held=down;
    Publish(v,sightzoom::At(toggle.step));
}

void SightZoomStock(unsigned char* v) noexcept {
    if(v[kDead])return;
    const unsigned n=SeatCount(v);
    for(unsigned i=0;i<n;++i) {
        if(SeatRider(SeatAt(v,i))!=Rider::player)continue;
        SightZoomFrame(v,i,!(i==0 && HighCamOffered(v)));
        return;
    }
}

float SightZoomNow(const void* vehicle) noexcept {
    const ULONGLONG at=cueAt.load(std::memory_order_acquire);
    if(!at || GetTickCount64()-at>kCueMs)return 1.0f;
    if(vehicle && cueVehicle.load(std::memory_order_relaxed)!=vehicle)return 1.0f;
    return cueZoom.load(std::memory_order_relaxed);
}

void ResetSightZoom() noexcept {
    toggle=Toggle{};
    cueAt.store(0,std::memory_order_release);
    cueZoom.store(1.0f,std::memory_order_relaxed);
    cueVehicle.store(nullptr,std::memory_order_relaxed);
}
}  // namespace crew
