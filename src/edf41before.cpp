// EDF4.1's pre-mission scenes (BEFORE.BVM: the story's M002, M003, M014, M015, M016, M190, M192) played on EDF6.
//
// EDF6's "int Before()" (EDF.dll 0x70DA00, registered for MAINSCRIPT at 0x71D8DC) plays <mission>/before.bvm when the
// file opens (0x70DAD2) and the save has not marked it seen: 0x70DBE3 loads the scene's script (0x1F68D0, the selector
// Mission() uses too), 0x70DBF1 marks the executor as a before scene, and the native suspends the main script until the
// scene's script ends. No EDF6 (or EDF5) mission has a before.bvm, so that path is never taken by the game's own data.
//
// MAINSCRIPT.AS (EDF6 and EDF5 alike) leaves the menu up when it gets there: HQMain ends with g_fade.Out(); g_fade.Wait()
// (the screen faded to black) and returns "PlayMission_Offline", which calls Before() before PlayMission_Common, and only
// PlayMission_Common's DeleteAllMenuUi() (UTILITY.H: g_bg.Stop(); g_main_frame.Stop(); g_fade.Destroy();
// g_resource.DestroyIcon(); _DeleteAllMenuUi()) takes the menu's UIs (the black fade among them) off the screen. So the
// scene played under the menu's black fade: the player saw a black screen with only what is drawn above it (the
// subtitles / radio mark), heard the voices, and the mission then showed as usual. EDF4.1's MAINSCRIPT closed the HQ's UIs
// and deleted its fade UI (WaitAllUiClose(); ...; DeleteFadeUi()) before returning "PlayMission_Offline", so its
// Before() played on a screen with no menu UI.
//
// This gives EDF6's Before() that state: the load of a scene's script (the call at 0x70DBE3, reached only when a scene
// will play) first runs _DeleteAllMenuUi's native (0x710300: close UI groups 2, 4, 3; registered at 0x71D433), what
// DeleteAllMenuUi() itself ends with. The script's own DeleteAllMenuUi() in PlayMission_Common still runs afterwards
// and finds those UIs gone, as it finds the loading icon its comment says _DeleteAllMenuUi takes. A mission without a
// before.bvm never reaches the call: nothing changes for EDF6's or EDF5's missions.
// EDF.dll TimeDateStamp 0x678CCB46; every byte the patch relies on is checked first (tests/edf41_before_native_audit.py).
#include "crew.h"
#include "memory.h"

#include <cstdint>

namespace crew {
namespace {
constexpr std::uint32_t kTimeDateStamp=0x678CCB46;
constexpr std::size_t kBeforeOpen=0x70DACE,kSceneLoad=0x70DBDC,kSceneLoadCall=0x70DBE3,kScriptLoad=0x1F68D0;
constexpr std::size_t kDeleteAllMenuUi=0x710300;

// lea rcx,[rbp-0x38]; call 0x50E20 (can <mission>/before.bvm be opened); movzx r14d,al
const unsigned char kBeforeOpenCode[]={0x48,0x8D,0x4D,0xC8,0xE8,0x49,0x33,0x94,0xFF,0x44,0x0F,0xB6,0xF0};
// lea rdx,[rbp-0x38]; mov rcx,r15; call 0x1F68D0; mov dl,1; mov rcx,[rip+..] (the script manager); call 0x1F6F30
const unsigned char kSceneLoadCode[]={0x48,0x8D,0x55,0xC8,0x49,0x8B,0xCF,0xE8,0xE8,0x8C,0xAE,0xFF,0xB2,0x01,0x48,0x8B,
    0x0D,0xB7,0x4C,0x9A,0x01,0xE8,0x3A,0x93,0xAE,0xFF};
// sub rsp,0x28; mov ecx,2; call 0x7E3590; mov ecx,4; call 0x7E3590; mov ecx,3; add rsp,0x28; jmp 0x7E3590
const unsigned char kDeleteAllMenuUiCode[]={0x48,0x83,0xEC,0x28,0xB9,0x02,0x00,0x00,0x00,0xE8,0x82,0x32,0x0D,0x00,0xB9,
    0x04,0x00,0x00,0x00,0xE8,0x78,0x32,0x0D,0x00,0xB9,0x03,0x00,0x00,0x00,0x48,0x83,0xC4,0x28,0xE9,0x6A,0x32,0x0D,0x00};

using ScriptLoadFn=void*(__fastcall*)(void* manager,void* path);
using DeleteAllMenuUiFn=void(__fastcall*)();

std::uint32_t TimeDateStamp() noexcept {
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
    return nt->FileHeader.TimeDateStamp;
}

// The call at 0x70DBE3: a before scene is about to play. The menu's UIs go first, as EDF4.1's MAINSCRIPT had them gone.
void* __fastcall SceneLoadHook(void* manager,void* path) {
    reinterpret_cast<DeleteAllMenuUiFn>(image+kDeleteAllMenuUi)();
    Log("EDF41BEFORE a before scene plays: the menu's UIs (its black fade) are closed first");
    return reinterpret_cast<ScriptLoadFn>(image+kScriptLoad)(manager,path);
}
}  // namespace

bool InstallEdf41Before() noexcept {
    static bool tried=false,ready=false;
    if(tried)return ready;
    tried=true;
    if(TimeDateStamp()!=kTimeDateStamp || !Matches(kBeforeOpen,kBeforeOpenCode,sizeof(kBeforeOpenCode)) ||
       !Matches(kSceneLoad,kSceneLoadCode,sizeof(kSceneLoadCode)) ||
       !Matches(kDeleteAllMenuUi,kDeleteAllMenuUiCode,sizeof(kDeleteAllMenuUiCode))) {
        Log("EDF41BEFORE off: EDF.dll is not the known build (stamp %08X)",TimeDateStamp());
        return false;
    }
    bool changed=false;
    ready=RedirectCall(image+kSceneLoadCall,image+kScriptLoad,reinterpret_cast<void*>(&SceneLoadHook),changed);
    Log("EDF41BEFORE before scenes play without the menu's black fade: %s",
        ready ? "on" : changed ? "half patched" : "not patched");
    return ready;
}
}  // namespace crew
