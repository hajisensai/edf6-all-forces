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
//    rbp-0x58; nothing else read before the loop sets it). The loop's per-player pad (from {0,1,2,3}) and split (its
//    count) are the offline ones, wrong online: 0x22AB90 binds that pad's input and makes a viewport of 1/split of
//    the screen. The participant gate's wrapper of that call (mission_participant_gate.cpp) gives it AngelScript's:
//    -1 for a remote user (no input, no viewport), this machine's own count for a local one, split by the local
//    players (Edf5BvmOnlinePlayerArgs).
// The BVM loop holds four players: its spawn offsets are four 16-byte slots on the stack (rbp+0x170..0x1A0, the
// stack cookie at rbp+0x1B0) and its player table four 0x18-byte rows (object +0x168..+0x1C7, the object list at
// +0x1C8). A session of more is created four, logged; never written past those.
#include "crew.h"
#include "memory.h"
#include "mission_participant_gate.h"
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

// The session's players in mission order: the session object (*(EDF+0x20B2AC0)-0x98)+0xD0 and its slot 1, which
// fills a std::vector<std::shared_ptr<User>> (AngelScript's PreloadPlayerResource 0x1B8D13..0x1B8D60, and 0x734670 the
// creation's copy of it, 0x1D984D). A user is remote when bit 1 of its +0x10 is set (0x12AC420, read for every user
// by the creation at 0x1D98B1).
constexpr unsigned kSessionBase=0x20B2AC0,kSessionBack=0x98,kSessionList=0xD0,kUserFlags=0x10;
constexpr unsigned kDelete=0x12D85EC;   // the game's sized operator delete
// A created player whose +0x128 bit 0 is set is not counted as this machine's (0x1D9B50).
constexpr unsigned kPlayerNotLocal=0x128;

struct Shared { void* obj; void* ctrl; };
struct SharedVector { Shared* first; Shared* last; Shared* end; };
using ListFn=SharedVector*(__fastcall*)(void*,SharedVector*);
using DeleteFn=void(__fastcall*)(void*,std::size_t);
using CtrlFn=void(__fastcall*)(void*);
DeleteFn freeFn=nullptr;   // the game's delete; tests/edf5_online_native_test.cpp sets its own (no CRT in its mapping)

// What the patched creation loop needs per index, from AngelScript's creation (0x1D9A60..0x1D9A7B and 0x1D9B5F): this
// machine's pad for a user of its own (its local players counted as they are created), -1 for a remote one (0x22AB90
// then neither binds input nor makes a viewport, 0x22ACFE), and the split by the local players (GS+0x14FF4), not the
// session's: one local player of two in the session is the whole screen, not its left half.
struct Creation {
    bool armed=false;
    std::uint32_t count=0;
    bool remote[kBvmPlayerSlots]{};
    int localMade=0;
    int split=1;
};
Creation creation;

bool ready=false;
bool clampLogged=false;
bool gateLogged=false;

bool WriteCode(unsigned rva,const unsigned char* code,std::size_t size) noexcept {
    unsigned char* at=image+rva;
    DWORD old=0;
    if(!VirtualProtect(at,size,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(at,code,size);
    VirtualProtect(at,size,old,&old);
    FlushInstructionCache(GetCurrentProcess(),at,size);
    return true;
}

// MSVC's _Ref_count_base::_Decref (as booster.cpp DropShared).
void DropShared(void* ctrl) noexcept {
    if(!ctrl)return;
    if(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(static_cast<unsigned char*>(ctrl)+8),-1)!=1)return;
    (*reinterpret_cast<CtrlFn* const*>(ctrl))[0](ctrl);
    if(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(static_cast<unsigned char*>(ctrl)+0xC),-1)==1)
        (*reinterpret_cast<CtrlFn* const*>(ctrl))[1](ctrl);
}
// The vector let go as the game does (0x1B8DA4..0x1B8E1F): every element, then the storage; a big one's (0x1000 bytes
// and over) real block 8 bytes before it.
void FreeList(SharedVector& v) noexcept {
    if(!v.first)return;
    for(Shared* p=v.first;p!=v.last;++p)DropShared(p->ctrl);
    std::size_t bytes=static_cast<std::size_t>(reinterpret_cast<unsigned char*>(v.end)-reinterpret_cast<unsigned char*>(v.first))&
                      ~std::size_t{15};
    void* block=v.first;
    if(bytes>=0x1000){bytes+=0x27;block=reinterpret_cast<void**>(v.first)[-1];}
    (freeFn ? freeFn : reinterpret_cast<DeleteFn>(image+kDelete))(block,bytes);
    v={};
}

bool ReadList(SharedVector& list) noexcept {
    __try {
        auto* base=At<unsigned char*>(image,kSessionBase);
        void* session=base ? At<void*>(base-kSessionBack,kSessionList) : nullptr;
        if(!session)return false;
        SharedVector* got=(*reinterpret_cast<ListFn* const*>(session))[1](session,&list);
        if(got && got!=&list){list=*got;*got=SharedVector{};}
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Which of the session's first `count` users are remote. False when the list cannot be read or is shorter.
bool ReadRemote(std::uint32_t count,bool* remote) noexcept {
    SharedVector list{};
    bool ok=ReadList(list);
    __try {
        ok=ok && static_cast<std::uint32_t>(list.last-list.first)>=count;
        for(std::uint32_t i=0;ok && i<count;++i)
            remote[i]=list.first[i].obj && ((At<std::uint32_t>(list.first[i].obj,kUserFlags)>>1)&1);
    } __except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
    __try {FreeList(list);} __except(EXCEPTION_EXECUTE_HANDLER){}
    return ok;
}
}  // namespace

// The BVM creation loop's count online: the session's players, at most the loop's four, with what each needs armed for
// the creation (Edf5BvmOnlinePlayerArgs). Called from the patched 0x22B36C on the game thread (its registers: rax back
// to the loop as r13d; rcx..r11 free there). 0, no player created, when the per-player arguments cannot be put right
// (the participant gate, which wraps the creation, not installed, or the session's users unreadable): every machine
// creating every player for its own pads and splitting the screen by them is worse than no player.
std::uint32_t Edf5BvmOnlinePlayers() noexcept {
    creation=Creation{};
    std::uint32_t players=0,local=0;
    bool read=false;
    __try {
        const auto status=At<unsigned char*>(image,kGameStatus);
        if(status){players=At<std::uint32_t>(status,kSessionPlayers);local=At<std::uint32_t>(status,kLocalPlayers);read=true;}
    } __except(EXCEPTION_EXECUTE_HANDLER){read=false;}
    if(!read){Log("EDF5 online: no game status, no player created");return 0;}
    std::uint32_t count=players;
    if(players>kBvmPlayerSlots) {
        if(!clampLogged)Log("EDF5 online: %u players in the session, an EDF5 mission creates %u (its script's player slots)",
                            players,kBvmPlayerSlots);
        clampLogged=true;
        count=kBvmPlayerSlots;
    } else clampLogged=false;
    if(!MissionParticipantGateReady()) {
        if(!gateLogged)Log("EDF5 online: the player creation's wrapper (mission_participant_gate) is not installed: no player created");
        gateLogged=true;
        return 0;
    }
    if(!ReadRemote(count,creation.remote)) {
        Log("EDF5 online: the session's %u users unreadable: no player created",count);
        creation=Creation{};
        return 0;
    }
    bool localFound=false;
    for(std::uint32_t i=0;i<count;++i)localFound=localFound || !creation.remote[i];
    if(players>count && !localFound)Log("EDF5 online: this machine's player is past the script's %u slots: it has none",count);
    creation.count=count;
    creation.split=local>0 ? static_cast<int>(local) : 1;
    creation.armed=count>0;
    return count;
}

// Before the BVM creation of mission player `index` (0x22B626 -> 0x22AB90, its 5th and 6th arguments): online, this
// machine's pad or -1, and the split by the local players. False offline and outside the armed online loop.
bool Edf5BvmOnlinePlayerArgs(int index,int* pad,int* split) noexcept {
    if(!creation.armed || index<0 || static_cast<std::uint32_t>(index)>=creation.count || !pad || !split)return false;
    *pad=creation.remote[index] ? -1 : creation.localMade;
    *split=creation.split;
    return true;
}
// After it: a created player of this machine's counts (0x1D9B50); the loop's last index disarms.
void Edf5BvmOnlinePlayerMade(int index,const void* made) noexcept {
    if(!creation.armed || index<0 || static_cast<std::uint32_t>(index)>=creation.count)return;
    __try {
        if(!creation.remote[index] && made && !(At<unsigned char>(made,kPlayerNotLocal)&1))++creation.localMade;
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    if(static_cast<std::uint32_t>(index)+1==creation.count)creation=Creation{};
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
        // Back to the BVM preload: the call now lands on our near thunk.
        std::int32_t rel=0;
        std::memcpy(&rel,image+kPreloadCall+1,4);
        bool undone=false;
        RedirectCall(image+kPreloadCall,image+kPreloadCall+5+rel,image+kBvmPreload,undone);
        Log("HOOK edf5 online=0 (CreatePlayer not patched; native 0x10 %s)",undone ? "back to the BVM preload" : "left on AngelScript's");
        return false;
    }
    ready=true;
    Log("HOOK edf5 online=1 (EDF5 scripts online: PreloadPlayerResource is AngelScript's, CreatePlayer creates the session's "
        "players, at most %u)",kBvmPlayerSlots);
    return true;
}
bool Edf5OnlineReady() noexcept { return ready; }
}  // namespace crew
