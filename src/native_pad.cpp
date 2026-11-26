// The game's pad read filtered by the plugin's owned chord (native_pad.h, pad_chord_logic.h).
#include "native_pad.h"
#include "crew.h"
#include "memory.h"
#include "pad_chord_logic.h"
#include "step_fresh.h"
#include "edf/patch.h"
#include <Xinput.h>
#include <atomic>

namespace crew::nativepad {
namespace {
// 0x11883CA: lea rdx,[rsp+60h]; mov ecx,esi; 0x11883D1: call [XInputGetState] (the slot 0x2136510, filled by the
// static initializer 0x2F7A0 from xinput9_1_0.dll); test eax,eax; jne. esi: the player's XInput user (P1: 0, or 1 when
// the pads are swapped, 0x2136504 == 1: 0x118839E). The one call to that slot (0x2F7FB stores it, 0x11883D1 reads it).
constexpr unsigned kReadPad=0x11883D1;
constexpr std::size_t kPadSwap=0x2136504,kPadSlot=0x2136510;   // the call's [rip+0xFAE139]: 0x11883D7+0xFAE139
const unsigned char kBefore[]={0x48,0x8D,0x54,0x24,0x60,0x8B,0xCE};
const unsigned char kReadPadCode[]={0xFF,0x15,0x39,0xE1,0xFA,0x00};
const unsigned char kAfter[]={0x85,0xC0,0x75,0x75};
using GetStateFn=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);

void* gameGet=nullptr;               // what the slot held (the game's XInputGetState)
std::atomic<bool> installed{false};
std::atomic<std::uint32_t> owned{0};             // OwnChord's chord
std::atomic<stepfresh::Step> ownedAt{0};         // ...and the step it was claimed in
padchord::Swallow swallow;                       // the input callback's own (its one thread reads the pad)
unsigned swallowLogs=0;
constexpr unsigned kSwallowLogs=32;

bool SiteContext() noexcept {
    return Matches(kReadPad-sizeof(kBefore),kBefore,sizeof(kBefore)) &&
           Matches(kReadPad+sizeof(kReadPadCode),kAfter,sizeof(kAfter));
}
std::uint16_t ChordNow() noexcept {
    const auto chord=static_cast<std::uint16_t>(owned.load(std::memory_order_acquire));
    if(!chord || !stepfresh::Fresh(ownedAt.load(std::memory_order_acquire)) || GamePaused())return 0;
    return chord;
}
DWORD FirstUser() noexcept {
    __try { return At<unsigned>(image,kPadSwap)==1 ? 1u : 0u; } __except(EXCEPTION_EXECUTE_HANDLER){ return 0u; }
}
DWORD WINAPI ReadHook(DWORD user,XINPUT_STATE* state) noexcept {
    // Through the slot every read, not the pointer it held at the install: whoever fills or rewrites it later (the
    // game's own init, an overlay, the autopilot's input source) is called as the shipped code would.
    void* const slotNow=At<void*>(image,kPadSlot);
    const DWORD result=reinterpret_cast<GetStateFn>(slotNow ? slotNow : gameGet)(user,state);
    if(result!=ERROR_SUCCESS || !state || user!=FirstUser())return result;
    const std::uint16_t raw=state->Gamepad.wButtons,before=swallow.kept;
    state->Gamepad.wButtons=swallow.Filter(raw,ChordNow());
    if(!before && swallow.kept && swallowLogs<kSwallowLogs) {
        ++swallowLogs;
        Log("PADCHORD 0x%X held on user %lu: kept from the game until released (raw 0x%04X -> game 0x%04X)%s",
            static_cast<unsigned>(swallow.kept),user,static_cast<unsigned>(raw),static_cast<unsigned>(state->Gamepad.wButtons),
            swallowLogs==kSwallowLogs ? " (no more of these lines)" : "");
    }
    return result;
}
}  // namespace

bool Install() noexcept {
    if(installed.load(std::memory_order_acquire))return true;
    bool ok=false;
    __try {
        ok=SiteContext() && edf::RedirectImportCall(image+kReadPad,kReadPadCode,reinterpret_cast<void*>(&ReadHook),&gameGet);
    } __except(EXCEPTION_EXECUTE_HANDLER){ ok=false; }
    installed.store(ok,std::memory_order_release);
    Log("PADCHORD the game's pad read 0x%X %s",kReadPad,ok ? "filtered (an owned chord is kept from the game)" :
        "is not as read (another hook?): a plugin chord reaches the game as its own buttons too");
    return ok;
}
bool ReadSiteKnown() noexcept {
    if(installed.load(std::memory_order_acquire))return true;
    __try { return SiteContext() && Matches(kReadPad,kReadPadCode,sizeof(kReadPadCode)); }
    __except(EXCEPTION_EXECUTE_HANDLER){ return false; }
}
void OwnChord(std::uint16_t chord) noexcept {
    ownedAt.store(chord ? stepfresh::Now() : 0,std::memory_order_release);
    owned.store(chord,std::memory_order_release);
}
}  // namespace crew::nativepad
