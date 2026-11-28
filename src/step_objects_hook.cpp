// stepobj's periods (common/edf/step_objects.h, docs/step-objects-re.md): the System's release 0x1197680 (its three
// calls) and its teardown 0x11956C0 (its 25 calls, one a tail jump), both void(System*). Every site or stepobj stays
// off: a release or a teardown not seen would leave a period trusting freed objects.
#include "step_objects_hook.h"
#include "edf_layout.h"
#include "config.h"
#include "plugin_log.h"
#include "edf/game_frame.h"
#include "edf/memory.h"
#include "edf/step_objects.h"

namespace crew {
namespace {
constexpr unsigned kRelease=0x1197680,kFrameRelease=0x1198F71;
constexpr unsigned kFlushReleases[]={0x11959F8,0x1196A0D};
constexpr unsigned kTeardown=0x11956C0;
constexpr unsigned kTeardowns[]={0x9B9F9,0x9BC3B,0x9D2D3,0x9E5BE,0x9E832,0x9EA82,0x9EB96,0x1B9947,0x1DDEFE,0x1DF10E,
                                 0x1F699B,0x21020C,0x2104E2,0x214FBB,0x220B8B,0x222C89,0x22487D,0x2268C0,0x6B4778,
                                 0x6B48B8,0x70FE44,0x1192E37,0x1193417,0x11A312E,0x11D986B};
constexpr std::size_t kFlushCount=sizeof(kFlushReleases)/sizeof(kFlushReleases[0]);
constexpr std::size_t kTeardownCount=sizeof(kTeardowns)/sizeof(kTeardowns[0]);
using SystemFn=void(__fastcall*)(void*);

void __fastcall FrameRelease(void* system) {
    stepobj::Releasing();
    reinterpret_cast<SystemFn>(image+kRelease)(system);
    stepobj::Released(true);
    // Once a frame on the game thread before its steps, vehicles or none (the vehicles' input hook was the only
    // caller: a battle without one never reloaded, perfrun --flip's B windows ran A, 2026-11-28). At most a file
    // time a second (IniWatch); the steps' workers are idle here.
    ReloadConfigIfChanged();
}
void __fastcall FlushRelease(void* system) {
    stepobj::Releasing();
    reinterpret_cast<SystemFn>(image+kRelease)(system);
    stepobj::Released(false);
}
void __fastcall Teardown(void* system) {
    stepobj::Releasing();
    reinterpret_cast<SystemFn>(image+kTeardown)(system);
    stepobj::Released(false);
    stepobj::Close();   // the scene's lists are empty: the next frame's release reopens
}

struct Site { unsigned rva,target; void* hook; };

bool CallsTarget(const Site& s) noexcept {
    bool yes=false;
    __try {
        const unsigned char op=image[s.rva];
        const auto rel=At<std::int32_t>(image,s.rva+1);
        yes=(op==0xE8 || op==0xE9) && static_cast<std::int64_t>(s.rva)+5+rel==static_cast<std::int64_t>(s.target);
    } __except(EXCEPTION_EXECUTE_HANDLER) { yes=false; }
    return yes;
}
}  // namespace

bool InstallStepObjects() noexcept {
    Site sites[1+kFlushCount+kTeardownCount]{};
    std::size_t n=0;
    sites[n++]={kFrameRelease,kRelease,reinterpret_cast<void*>(&FrameRelease)};
    for(unsigned s:kFlushReleases)sites[n++]={s,kRelease,reinterpret_cast<void*>(&FlushRelease)};
    for(unsigned s:kTeardowns)sites[n++]={s,kTeardown,reinterpret_cast<void*>(&Teardown)};
    // Every site's bytes first: one not as expected leaves stepobj off (every caller asks Readable as before).
    for(const Site& s:sites)
        if(!CallsTarget(s)){Log("STEPOBJ refused: EDF+%#x is not a call of EDF+%#x (every Readable kept)",s.rva,s.target);return false;}
    for(std::size_t i=0;i<n;++i) {
        bool changed=false;
        if(edf::RedirectCall(image+sites[i].rva,image+sites[i].target,sites[i].hook,changed))continue;
        // The hooks done stay (they only call the stock function); stepobj is never installed, so every answer is false.
        Log("STEPOBJ refused: EDF+%#x could not be redirected%s (every Readable kept)",sites[i].rva,changed ? ", half patched" : "");
        return false;
    }
    stepobj::Install(reinterpret_cast<const void* const*>(image+gameframe::kSystem));
    Log("STEPOBJ release sites=%u teardown sites=%u",static_cast<unsigned>(1+kFlushCount),static_cast<unsigned>(kTeardownCount));
    return true;
}
}  // namespace crew
