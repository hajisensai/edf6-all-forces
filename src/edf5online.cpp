// The EDF5 campaign's missions online (2026-10-10: an online EDF5 pack mission stayed black on every machine).
// Its missions are EDF5's own compiled scripts (MISSION/EDF5_OLD_SCRIPT/<id>/MISSION.BVM), run by EDF.dll's BVM
// executor; stock EDF6 runs none of them, offline or online. Two of that executor's natives were left without their
// online half (static, H; docs/edf5-online-re.md):
//  - native 0x10 PreloadPlayerResource (0x225E30): online it assigns an empty player list (0x225EA9 call 0x20C5B0 with
//    rdx = r8 = 0) and returns, so no session player's resources are preloaded. AngelScript's PreloadPlayerResource
//    (0x1B8CC0) is the same function whole: the same online test, the session's players from the session object
//    (0x1B8D13..0x1B8D60) each through 0x59DC90, and offline the same 0x59DE50 loop. Neither reads an argument. The
//    executor's one call of the native (0x210AE0) is pointed at it.
//  - natives 0x3E8..0x3EA CreatePlayer / _NoWeapon / _InitWeapon (0x22B1C0): online it assigns an empty list and
//    jumps past the creation loop (0x22B36C..0x22B39F), so no player is created at all. AngelScript's creation
//    (0x1D9520) runs one loop for both modes, only its count differs: GS+0x14FF4 (the local players) offline,
//    GS+0x14FF8 (the session's) online (0x1D95F4). The BVM loop (0x22B3B0) creates by index through 0x22AB90,
//    whose own online half (0x22AC2F -> 0x591130) is intact. At 0x22B36C the patch sets the loop's count (r13d) to
//    the session's and enters it, the state there being the offline path's (xmm0 0, r15 0, the empty list at
//    rbp-0x58; nothing else read before the loop sets it).
// The BVM loop holds four players: its spawn offsets are four 16-byte slots on the stack (rbp+0x170..0x1A0, the
// stack cookie at rbp+0x1B0) and its player table four 0x18-byte rows (object +0x168..+0x1C7, the object list at
// +0x1C8). A session of more is created four, logged; never written past those.
#include "crew.h"
#include "memory.h"
#include <cstdint>
#include <cstring>

namespace crew {
namespace {
constexpr unsigned kGameStatus=0x20B2890;
constexpr unsigned kLocalPlayers=0x14FF4,kSessionPlayers=0x14FF8;
constexpr unsigned kBvmPlayerSlots=4;

// Native 0x10's call in the BVM dispatcher (0x20FF70): lea rcx,[r14-0x30]; call 0x225E30.
constexpr unsigned kPreloadCallAt=0x210ADC,kPreloadCall=0x210AE0,kBvmPreload=0x225E30,kAsPreload=0x1B8CC0;
const unsigned char kPreloadCallCode[]={0x49,0x8D,0x4E,0xD0,0xE8,0x4B,0x53,0x01,0x00};
const unsigned char kAsPreloadSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x8B,0xEC,
                                     0x48,0x83,0xEC,0x70};
const unsigned char kBvmPreloadSig[]={0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x08,0x49,0x89,0x73,0x18,0x57};

// 0x22B33F..0x22B3A3: the count, the online test and its empty branch, ending jmp 0x22BB94.
constexpr unsigned kCreateAt=0x22B33F,kCreateOnline=0x22B36C,kCreateLoop=0x22B3B0;
const unsigned char kCreateCode[]={
    0x48,0x8B,0x05,0x4A,0x75,0xE8,0x01,0x44,0x8B,0xA8,0xF4,0x4F,0x01,0x00,0x8B,0x48,0x38,0x83,0xF9,0xFF,0x74,0x5B,
    0x48,0x8B,0x40,0x20,0x48,0x8B,0x0C,0xC8,0x48,0x8B,0x41,0x10,0x48,0x63,0x48,0x08,0x83,0x7C,0x01,0x68,0x00,0x74,0x44,
    0xF3,0x0F,0x7F,0x85,0x50,0x01,0x00,0x00,0x4C,0x89,0xBD,0x60,0x01,0x00,0x00,0xF3,0x0F,0x7F,0x85,0xD0,0x00,0x00,0x00,
    0x4C,0x89,0xBD,0xE0,0x00,0x00,0x00,0x44,0x0F,0xB6,0x4C,0x24,0x50,0x45,0x33,0xC0,0x33,0xD2,0x48,0x8D,0x4D,0xA8,0xE8,
    0x12,0x12,0xFE,0xFF,0x90,0xE9,0xF0,0x07,0x00,0x00};
static_assert(sizeof(kCreateCode)==0x22B3A4-kCreateAt);
constexpr unsigned kCreatePatchSize=17;   // mov rax,imm64; call rax; mov r13d,eax; jmp short kCreateLoop

bool ready=false;
bool clampLogged=false;

bool WriteCode(unsigned rva,const unsigned char* code,std::size_t size) noexcept {
    unsigned char* at=image+rva;
    DWORD old=0;
    if(!VirtualProtect(at,size,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(at,code,size);
    VirtualProtect(at,size,old,&old);
    FlushInstructionCache(GetCurrentProcess(),at,size);
    return true;
}
}  // namespace

// The BVM creation loop's count online: the session's players, at most the loop's four. Called from the patched
// 0x22B36C on the game thread (its registers: rax back to the loop as r13d; rcx..r11 free there).
std::uint32_t Edf5BvmOnlinePlayers() noexcept {
    std::uint32_t players=0;
    __try {
        const auto status=At<unsigned char*>(image,kGameStatus);
        players=status ? At<std::uint32_t>(status,kSessionPlayers) : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER){players=0;}
    if(players<=kBvmPlayerSlots){clampLogged=false;return players;}
    if(!clampLogged)Log("EDF5 online: %u players in the session, an EDF5 mission creates %u (its script's player slots)",
                        players,kBvmPlayerSlots);
    clampLogged=true;
    return kBvmPlayerSlots;
}

// Both or neither: players preloaded but never created is the black screen this fixes, created but not preloaded worse.
bool InstallEdf5Online() noexcept {
    static bool tried=false;
    if(tried)return ready;
    tried=true;
    if(!Matches(kPreloadCallAt,kPreloadCallCode,sizeof(kPreloadCallCode)) ||
       !Matches(kBvmPreload,kBvmPreloadSig,sizeof(kBvmPreloadSig)) ||
       !Matches(kAsPreload,kAsPreloadSig,sizeof(kAsPreloadSig)) ||
       !Matches(kCreateAt,kCreateCode,sizeof(kCreateCode))) {
        Log("HOOK edf5 online=0 (the BVM player natives are not the known build's)");
        return false;
    }
    unsigned char patch[kCreatePatchSize]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,0x44,0x8B,0xE8,0xEB,0};
    const auto count=reinterpret_cast<std::uintptr_t>(&Edf5BvmOnlinePlayers);
    std::memcpy(patch+2,&count,sizeof(count));
    patch[16]=static_cast<unsigned char>(kCreateLoop-(kCreateOnline+kCreatePatchSize));
    bool changed=false;
    if(!RedirectCall(image+kPreloadCall,image+kBvmPreload,image+kAsPreload,changed)) {
        Log("HOOK edf5 online=0 (native 0x10's call %s)",changed ? "half patched" : "not patched");
        return false;
    }
    if(!WriteCode(kCreateOnline,patch,sizeof(patch))) {
        Log("HOOK edf5 online=0 (CreatePlayer not patched; its preload is AngelScript's, harmless alone)");
        return false;
    }
    ready=true;
    Log("HOOK edf5 online=1 (EDF5 scripts online: PreloadPlayerResource is AngelScript's, CreatePlayer creates the session's "
        "players, at most %u)",kBvmPlayerSlots);
    return true;
}
bool Edf5OnlineReady() noexcept { return ready; }
}  // namespace crew
