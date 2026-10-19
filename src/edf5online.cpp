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
// The loop itself was written for four (2026-10-18, a room of eight on an EDF4.1 pack: players 5..8 had no soldier):
// the session's count is the script's no more than AngelScript's (the script calls CreatePlayer once, with one spawn
// point), but the loop keeps four of three things - spawn offsets on its stack (rbp+0x170..0x1AF, the stack cookie at
// rbp+0x1B0), pads on its stack (rbp+0x150..0x15F), and the script's player table in the BVM object (+0x168, four
// 0x18-byte rows, its object list at +0x1C8). Each is given what AngelScript's loop is given for a fifth player and on
// (EDF6Coop's MultiSlot, which extends that loop: docs/edf5-online-re.md):
//  - its offset normalisation (0x22B490) runs over the four entries only (0x22B470: min(count, 4)),
//  - players 5.. read their offsets and pads from Edf5BvmPlayerLayout's arrays (0x22B569: r15, r12): the four the
//    loop finished, then the layout EDF6Coop gives AngelScript's (spawn_layout.h, one source in both plugins); with
//    four or fewer the loop's own, untouched,
//  - players 5.. are not written past the four rows (0x22B69D, 0x22B802) but into this plugin's rows of that BVM object
//    (Table: rows 4.., the same 0x18-byte {object list index, weak_ptr} as the game's, dropped with the object's own
//    rows, 0x20EB81). The script's natives that read the table by player index read them there (below). The object
//    list (+0x1C8) and the script's group (0x1F74A0) are vectors and get everyone.
// The table's readers (1E0000..240000, every use of +0x168/+0x170/+0x178 of the BVM object; docs/edf5-online-re.md):
//  - 0x21F380, native 1200, the route guide of each of this machine's players (its loop over 0x225290's count, the row
//    at 0x21F3C2): rows 4.. from the plugin's (the stub computes r8 for them; EDF6Coop's hook at 0x21F3CF bounds the
//    game's four and is passed by for them),
//  - 0x2214B0, Factor_PlayerAreaIn (the script waits for a player in an area): four weak_ptrs copied and handed to
//    EventFactor_ObjectAreaIn::Initialize (0x207870, count 4, 0x221687): every player's, as EDF6Coop does for
//    AngelScript's (its AreaFactorCallHandler),
//  - 0x222740 / 0x22A650 / 0x228AC0, natives 0x3ED / 0x3EE / 0x3EF (a player by the script's index: its vf 0x298, a
//    wait on its +0x1588 flags, its +0x158C): for index 4.. each called on a view whose row 0 is the player's row,
//  - 0x2252D0 / 0x2253A0 (every player's +0x380 bit 11 / bit 7, four rows): called again on the plugin's rows, four at
//    a time,
//  - 0x229040 (the first player there, four rows): its loop goes on into the plugin's rows (0x22912B),
//  - 0x22B9CB in 0x22B1C0 itself: reorders the four rows by a map that is never filled: nothing.
// And the one creation's online half (0x22AB90) dereferences 0x591130's result unchecked (0x22AC3F), where
// AngelScript's (0x1DC544) gives back an empty player: 0x591130 returns null for an index no user stands at, or one the
// game's online player records do not take (0x5959E1 `cmp edi, 4`, widened by EDF6Coop's Extend=1). The call is given
// an empty record then, which 0x22AB90 takes its own empty path on (0x22ACDC -> 0x22AEF2).
// More than four only where every machine creates them (edf5online_room.h): EDF6Coop 2.5.0's BvmPlayerTableHooks here
// and every mission player's machine telling the room it runs this; otherwise every one creates four, and says why.
#include "crew.h"
#include "memory.h"
#include "mission_participant_gate.h"
#include "spawn_layout.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <initializer_list>
#include <new>

namespace crew {
namespace {
constexpr unsigned kGameStatus=0x20B2890;
constexpr unsigned kLocalPlayers=0x14FF4,kSessionPlayers=0x14FF8;
constexpr unsigned kBvmPlayerSlots=4;           // the loop's own: its offsets, its pads, the script's player table rows
// The most a session holds: EDF6Coop's kMaxPlayers (its room size, EDF6COOP_EXTENSION_MAX_PEERS).
constexpr unsigned kBvmPlayerCapacity=1024;

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

// The loop's four (22B1C0's frame: rbp = its rsp + 0x100; the loop index r14d, its row offset rdi = index * 0x18).
// 0x22B470: lea rbx,[rbp+0x170]; mov rdi,r13 - the offset normalisation's start and count (0x22B490..0x22B563).
constexpr unsigned kNormalizeAt=0x22B470,kNormalizeBack=0x22B47A;
const unsigned char kNormalizeCode[]={0x48,0x8D,0x9D,0x70,0x01,0x00,0x00,0x49,0x8B,0xFD};
// 0x22B569: mov rdi,r15 (r15 0: the row offset); lea r15,[rbp+0x170] (the offsets); lea r12,[rbp+0x150] (the pads).
constexpr unsigned kLayoutAt=0x22B569,kLayoutBack=0x22B57A;
const unsigned char kLayoutCode[]={0x49,0x8B,0xFF,0x4C,0x8D,0xBD,0x70,0x01,0x00,0x00,0x4C,0x8D,0xA5,0x50,0x01,0x00,0x00};
// 0x22B69D: xor r8d,r8d; xor eax,eax; mov rcx,[rsp+0x68] - the row's weak_ptr written (to 0x22B6E5), skipped to 0x22B6E8.
constexpr unsigned kRowAt=0x22B69D,kRowBack=0x22B6A7,kRowSkip=0x22B6E8;
const unsigned char kRowCode[]={0x45,0x33,0xC0,0x33,0xC0,0x48,0x8B,0x4C,0x24,0x68};
// 0x22B802: mov [rdi+rsi+0x168],ebx - the row's place in the object list. The player's locked shared_ptr is still at
// rsp+0x60 (0x22B660..0x22B66C, let go at 0x22B809): a row of the plugin's takes it there.
constexpr unsigned kRowIndexAt=0x22B802,kRowIndexBack=0x22B809;
const unsigned char kRowIndexCode[]={0x89,0x9C,0x37,0x68,0x01,0x00,0x00};
// 0x22AC29: mov rdx,r9; mov ecx,r13d; call 0x591130 - the one creation's online player object.
constexpr unsigned kObjectAt=0x22AC29,kObjectCall=0x22AC2F,kOnlineObject=0x591130;
const unsigned char kObjectCode[]={0x49,0x8B,0xD1,0x41,0x8B,0xCD,0xE8,0xFC,0x64,0x36,0x00};

// The table's readers. 0x21F380 (r15 the BVM object, r12 the loop's player): 0x21F3C2 lea r8,[r12+r12*2]; xorps
// xmm0,xmm0 - the row (r15 + r8*8 + 0x168) of each pass (0x21F8D2 loops back to it); 0x21F3C9 movdqu [rsp+0x30],xmm0;
// EDF6Coop's 0x21F3CF mov rdx,[r15+r8*8+0x178]; 0x21F3D7 test rdx,rdx; then 0x21F504 / 0x21F511 its +0x170 / +0x178.
constexpr unsigned kGuideAt=0x21F3C2,kGuideBack=0x21F3C9,kGuideRead=0x21F3D7;
const unsigned char kGuideCode[]={0x4F,0x8D,0x04,0x64,0x0F,0x57,0xC0};
const unsigned char kGuideClear[]={0xF3,0x0F,0x7F,0x44,0x24,0x30};
const unsigned char kGuideTest[]={0x48,0x85,0xD2,0x74,0x26};
const unsigned char kGuideObject[]={0x4B,0x8B,0x9C,0xC7,0x70,0x01,0x00,0x00};
const unsigned char kGuideCtrl[]={0x4B,0x8B,0x84,0xC7,0x78,0x01,0x00,0x00};
// 0x229040 (r13 the BVM object): rdi walks the rows' +0x170 (0x229099), r12 = -0x170 (0x2290A0); 0x2290B0 the pass
// reads [r12+rdi+0x178], [rdi], [rdi+8]; 0x22912B inc r14; add rdi,0x18; cmp r14,4; jne 0x2290B0; then 0x22913C
// jmp 0x229208 (nobody there).
constexpr unsigned kFirstAt=0x22912B,kFirstLoop=0x2290B0,kFirstNone=0x22913C;
const unsigned char kFirstCode[]={0x49,0xFF,0xC6,0x48,0x83,0xC7,0x18,0x49,0x83,0xFE,0x04,0x0F,0x85,0x74,0xFF,0xFF,0xFF};
const unsigned char kFirstStart[]={0x49,0x8D,0xBD,0x70,0x01,0x00,0x00,0x4D,0x8B,0xE5,0x4C,0x2B,0xE7};
const unsigned char kFirstPass[]={0x33,0xED,0x33,0xF6,0x49,0x8B,0x94,0x3C,0x78,0x01,0x00,0x00};
const unsigned char kFirstTake[]={0x48,0x8B,0x2F,0x48,0x8B,0x77,0x08};
const unsigned char kFirstNoneCode[]={0xE9,0xC7,0x00,0x00,0x00};
// The natives by the script's index (rcx the BVM object, edx the index): movsxd rax,edx; lea rdx,[rax+rax*2]; lea
// rN,[rcx+rdx*8] ... up to EDF6Coop's site, and the +0x170 read after the lock. They read nothing else of rcx but
// 0x22A650's [rsi+0x70] (rsi = rcx: the script's process, 0x22A73D and 0x22A77B).
constexpr unsigned kGet=0x222740,kWait=0x22A650,kSet=0x228AC0;
const unsigned char kGetHead[]={0x48,0x63,0xC2,0x48,0x8D,0x14,0x40,0x4C,0x8D,0x04,0xD1,0x33,0xF6,0x44,0x8B,0xD6,0x8B,0xDE};
const unsigned char kGetObject[]={0x4D,0x8B,0x90,0x70,0x01,0x00,0x00};
const unsigned char kSetHead[]={0x48,0x63,0xC2,0x48,0x8D,0x14,0x40,0x4C,0x8D,0x0C,0xD1,0x45,0x33,0xD2,0x33,0xFF};
const unsigned char kSetObject[]={0x4D,0x8B,0x91,0x70,0x01,0x00,0x00};
const unsigned char kWaitHead[]={0x48,0x8B,0xF1,0x48,0x63,0xC2,0x48,0x8D,0x14,0x40,0x4C,0x8D,0x14,0xD1,0x45,0x33,0xFF,0x45,
                                 0x8B,0xDF,0x41,0x8B,0xDF};
const unsigned char kWaitObject[]={0x4D,0x8B,0x9A,0x70,0x01,0x00,0x00};
const unsigned char kWaitProcess[]={0x48,0x8B,0x46,0x70};
// 0x2252D0 / 0x2253A0 (rcx the BVM object, dl on / off): lea rdi,[rcx+0x170]; mov esi,4 - the four rows.
constexpr unsigned kFlag11=0x2252D0,kFlag7=0x2253A0,kFlagRows=0x17;
const unsigned char kFlagRowsCode[]={0x48,0x8D,0xB9,0x70,0x01,0x00,0x00,0xBE,0x04,0x00,0x00,0x00};
// 0x2214B0 Factor_PlayerAreaIn: mov rbp,rcx (0x2214DE); lea rdi,[rbp+0x170] (0x2215D7) the four rows copied to
// rsp+0x60; 0x221676 mov r9d,4; lea r8,[rsp+0x60]; mov rdx,r14; mov rcx,rax; call 0x207870.
constexpr unsigned kAreaKeep=0x2214DE,kAreaRows=0x2215D7,kAreaArgs=0x221676,kAreaCall=0x221687,kAreaInit=0x207870;
const unsigned char kAreaKeepCode[]={0x48,0x8B,0xE9};
const unsigned char kAreaRowsCode[]={0x48,0x8D,0xBD,0x70,0x01,0x00,0x00};
const unsigned char kAreaArgsCode[]={0x41,0xB9,0x04,0x00,0x00,0x00,0x4C,0x8D,0x44,0x24,0x60,0x49,0x8B,0xD6,0x48,0x8B,0xC8};
// The BVM object's destructor 0x20EA00 lets its rows go: lea rcx,[rdi+0x168]; lea r9,[row dtor]; mov edx,0x18; lea
// r8d,[rdx-0x14] (4); call `eh vector destructor iterator` 0x12D87E8.
constexpr unsigned kRowsGoneArgs=0x20EB6A,kRowsGoneCall=0x20EB81,kVectorDtor=0x12D87E8;
const unsigned char kRowsGoneCode[]={0x48,0x8D,0x8F,0x68,0x01,0x00,0x00,0x4C,0x8D,0x0D,0x68,0x01,0x00,0x00,0xBA,0x18,0x00,
                                     0x00,0x00,0x44,0x8D,0x42,0xEC};
// The dispatcher's calls of the natives by index / of every player (0x20FF70).
constexpr unsigned kGetCall=0x214480,kWaitCall=0x214558,kSetCall=0x214610,kFlag11Call=0x214007,kFlag7Call=0x21405F;
constexpr unsigned kRows=0x168;   // the BVM object's four rows

// The session's players in mission order: the session object (*(EDF+0x20B2AC0)-0x98)+0xD0 and its slot 1, which
// fills a std::vector<std::shared_ptr<User>> (AngelScript's PreloadPlayerResource 0x1B8D13..0x1B8D60, and 0x734670 the
// creation's copy of it, 0x1D984D). A user is remote when bit 1 of its +0x10 is set (0x12AC420, read for every user
// by the creation at 0x1D98B1).
constexpr unsigned kSessionBase=0x20B2AC0,kSessionBack=0x98,kSessionList=0xD0,kUserFlags=0x10,kUserIndex=0x48;
constexpr unsigned kDelete=0x12D85EC;   // the game's sized operator delete
// A created player whose +0x128 bit 0 is set is not counted as this machine's (0x1D9B50).
constexpr unsigned kPlayerNotLocal=0x128;

struct Shared { void* obj; void* ctrl; };
struct SharedVector { Shared* first; Shared* last; Shared* end; };
using ListFn=SharedVector*(__fastcall*)(void*,SharedVector*);
using DeleteFn=void(__fastcall*)(void*,std::size_t);
using CtrlFn=void(__fastcall*)(void*);
DeleteFn freeFn=nullptr;   // the game's delete; tests/edf5_online_native_test.cpp sets its own (no CRT in its mapping)
// 0x591130(ecx index, rdx matrix, r8d mode): the online player object's record (+0x28 object, +0x30 control block),
// null when the game makes none. Set at install; the test points it at its own.
using OnlineObjectFn=void*(__fastcall*)(int,const void*,int);
OnlineObjectFn onlineObject=nullptr;
// What 0x22AB90 gets for none: a record whose control block (+0x30) is null, so it locks nothing, casts null
// (0x22AC7F) and returns an empty player (0x22ACDC -> 0x22AEF2). Read only.
alignas(16) const unsigned char kNoOnlineObject[0x40]{};

// What the patched creation loop needs per index, from AngelScript's creation (0x1D9A60..0x1D9A7B and 0x1D9B5F): this
// machine's pad for a user of its own (its local players counted as they are created), -1 for a remote one (0x22AB90
// then neither binds input nor makes a viewport, 0x22ACFE), and the split by the local players (GS+0x14FF4), not the
// session's: one local player of two in the session is the whole screen, not its left half.
struct Creation {
    bool armed=false;
    std::uint32_t count=0;
    bool remote[kBvmPlayerCapacity]{};
    int localMade=0;
    int split=1;
};
Creation creation;

// Players 5.. of a loop of more than four: where each starts (relative to the spawn point, as the loop's own finished
// offsets are) and its pad. The patched loop walks r15 over `offset` and r12 over `pad`.
struct Layout {
    alignas(16) float offset[kBvmPlayerCapacity][4];
    std::int32_t pad[kBvmPlayerCapacity];
};
Layout layout{};
constexpr std::size_t kLayoutPads=offsetof(Layout,pad);

// The script's player table past its four rows, per BVM object: row i is player 4 + i, as the game's rows are laid out
// ({object list index, weak_ptr}, 0x18 bytes), each holding a weak reference as the game's do (0x22B6B2). `capacity`
// a multiple of four (0x2252D0 / 0x2253A0 run over four rows at a time), `used` the players created into it.
struct Row { std::int32_t index; std::int32_t pad; void* obj; void* ctrl; };
static_assert(sizeof(Row)==0x18 && offsetof(Row,obj)==8 && offsetof(Row,ctrl)==0x10,"the game's row");
struct Table { const void* bvm; std::uint32_t used,capacity; Row* row; Table* next; };
Table* tables=nullptr;

bool ready=false;
bool clampLogged=false;
bool gateLogged=false;
bool extendLogged=false;
bool noObjectLogged=false;
bool tableLogged=false;

// The BVM natives' own online test (0x22B34D..0x22B36A, the same in 0x225E30 and AngelScript's 0x1B8CC0): a session
// (GS+0x38 != -1) whose mode object's virtual base has +0x68 set.
bool BvmOnline() noexcept {
    __try {
        const auto status=At<unsigned char*>(image,kGameStatus);
        if(!status)return false;
        const std::int32_t slot=At<std::int32_t>(status,0x38);
        if(slot==-1)return false;
        const auto modes=At<unsigned char**>(status,0x20);
        const auto mode=At<unsigned char*>(modes[static_cast<std::uint32_t>(slot)],0x10);
        return At<std::int32_t>(mode,static_cast<std::size_t>(At<std::int32_t>(mode,8))+0x68)!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool WriteImage(unsigned rva,const unsigned char* code,std::size_t size) noexcept {
    unsigned char* at=image+rva;
    DWORD old=0;
    if(!VirtualProtect(at,size,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(at,code,size);
    edf::RestoreProtection(at,size,old);
    FlushInstructionCache(GetCurrentProcess(),at,size);
    return true;
}
// The install's writer (tests/edf5_online_native_test.cpp's rollback run fails one on purpose).
using WriteFn=bool(*)(unsigned,const unsigned char*,std::size_t) noexcept;
WriteFn writeCode=&WriteImage;

// MSVC's _Ref_count_base::_Decref (as booster.cpp DropShared).
void DropShared(void* ctrl) noexcept {
    if(!ctrl)return;
    if(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(static_cast<unsigned char*>(ctrl)+8),-1)!=1)return;
    (*reinterpret_cast<CtrlFn* const*>(ctrl))[0](ctrl);
    if(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(static_cast<unsigned char*>(ctrl)+0xC),-1)==1)
        (*reinterpret_cast<CtrlFn* const*>(ctrl))[1](ctrl);
}
// A weak reference let go as the game's rows let theirs go (0x22B6D3: +0xC down, the block's vtbl[1] at the last).
void DropWeak(void* ctrl) noexcept {
    if(!ctrl)return;
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
// Whether mission player 0..count-1 is remote. Player i is the user whose +0x48 is i, not the list's i-th: the creation
// 591130 finds it by that (its predicate 0x5A35D0 compares user+0x48), and AngelScript's copy 0x734670 puts each user at
// out[user+0x48] (0x734705..0x7347FC), holes left empty. A player no user stands for is remote here: 591130 makes no
// object for it (Edf5BvmOnlineObject: an empty player), and -1 binds no pad. False when the list cannot be read.
bool ReadRemote(std::uint32_t count,bool* remote) noexcept {
    for(std::uint32_t i=0;i<count;++i)remote[i]=true;
    SharedVector list{};
    bool ok=ReadList(list);
    __try {
        for(Shared* p=list.first;ok && p && p!=list.last;++p) {
            if(!p->obj)continue;
            const auto index=At<std::int32_t>(p->obj,kUserIndex);
            if(index>=0 && static_cast<std::uint32_t>(index)<count)remote[index]=(At<std::uint32_t>(p->obj,kUserFlags)>>1)&1;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
    __try {FreeList(list);} __except(EXCEPTION_EXECUTE_HANDLER){}
    return ok;
}

// Where player `index` starts relative to the spawn point, from the loop's four finished offsets (`table`, float4 each,
// y 0): EDF6Coop's layout for AngelScript's loop, whose four finished offsets are these (the same constants 0x1765B30,
// 0x1792F10..30 and scale 0x1765B90, normalised, scaled and turned the same way: 0x1D96B0 / 0x22B490). One source in
// both plugins (spawn_layout.h): an EDF5 / EDF4.1 mission's players stand where a stock mission's do, to the bit.
void Edf5BvmSpawnOffset(std::uint32_t index,const float* table,float* out) noexcept { edf6spawn::SpawnOffset(index,table,out); }

// The patched 0x22B569, for a loop of more than four only: `finished` the loop's four offsets (rbp+0x170, normalised,
// scaled, turned, y 0), `pads` its four pads (rbp+0x150, {0,1,2,3}). Players past four get no pad of the loop's own
// (online the participant gate gives each its pad, Edf5BvmOnlinePlayerArgs). r15 and r12 walk the arrays returned.
Layout* Edf5BvmPlayerLayout(const float* finished,const std::int32_t* pads,std::uint32_t count) noexcept {
    if(count>kBvmPlayerCapacity)count=kBvmPlayerCapacity;   // never: Edf5BvmOnlinePlayers holds the count to it
    for(std::uint32_t i=0;i<count;++i) {
        Edf5BvmSpawnOffset(i,finished,layout.offset[i]);
        layout.pad[i]=i<kBvmPlayerSlots ? pads[i] : -1;
    }
    return &layout;
}

// The patched 0x22AC2F: 0x591130's record, or an empty one where it made none (AngelScript's creation, 0x1DC544).
// Not noexcept: whatever the game's function throws goes on to the game's caller as before.
void* __fastcall Edf5BvmOnlineObject(int index,const void* matrix,int mode) {
    void* const made=onlineObject ? onlineObject(index,matrix,mode) : nullptr;
    if(made)return made;
    if(!noObjectLogged)Log("EDF5 online: no player object for mission player %d (no user at that index, or one the game's "
                           "online records do not take): an empty player, as AngelScript's creation makes",index);
    noObjectLogged=true;
    return const_cast<unsigned char*>(kNoOnlineObject);
}

// --- The plugin's rows (game thread: the BVM executor and the BVM object's destructor)
Table* Find(const void* bvm) noexcept {
    for(Table* t=tables;t;t=t->next)if(t->bvm==bvm)return t;
    return nullptr;
}
// The table of `bvm` holding player `index`, made or grown (rows moved with their references); null when out of memory.
Table* Hold(const void* bvm,std::uint32_t index) noexcept {
    Table* t=Find(bvm);
    const std::uint32_t want=((index-kBvmPlayerSlots)/4+1)*4;
    if(t && t->capacity>=want)return t;
    Row* rows=new(std::nothrow) Row[want];
    if(!rows)return nullptr;
    std::memset(rows,0,sizeof(Row)*want);
    if(!t) {
        t=new(std::nothrow) Table{bvm,0,0,nullptr,tables};
        if(!t){delete[] rows;return nullptr;}
        tables=t;
    } else if(t->row) {
        std::memcpy(rows,t->row,sizeof(Row)*t->capacity);
        delete[] t->row;
    }
    t->row=rows;
    t->capacity=want;
    return t;
}

// Row `index` (4..) of `bvm`'s table, null when the plugin holds none for it: the stubs at 0x21F3C2 and 0x22912B and the
// natives' wrappers. A row whose address is not 8 bytes on from the object's (r8's * 8 at 0x21F3CF) is none either.
const void* __fastcall Edf5BvmRowAt(const void* bvm,std::uint64_t index) noexcept {
    if(index<kBvmPlayerSlots)return nullptr;
    const Table* t=Find(bvm);
    if(!t || index-kBvmPlayerSlots>=t->used)return nullptr;
    const Row* row=&t->row[index-kBvmPlayerSlots];
    if((reinterpret_cast<std::uintptr_t>(row)-reinterpret_cast<std::uintptr_t>(bvm))&7)return nullptr;
    return row;
}
// The patched 0x22B802 for player `index` 4..: its row written as the game writes its four (0x22B69D..0x22B6E5 and
// 0x22B802): the player's weak_ptr (the locked shared_ptr at rsp+0x60 copied, its weak count up, the row's old one let
// go) and its place in the object list.
void __fastcall Edf5BvmStoreRow(const void* bvm,std::uint32_t index,std::int32_t listIndex,const Shared* made) noexcept {
    if(index<kBvmPlayerSlots || index>=kBvmPlayerCapacity || !made)return;
    Table* t=Hold(bvm,index);
    if(!t) {
        if(!tableLogged)Log("EDF5 online: no memory for the script's player table: mission player %u not in it",index+1);
        tableLogged=true;
        return;
    }
    Row& r=t->row[index-kBvmPlayerSlots];
    void* const old=r.ctrl;
    if(made->ctrl)_InterlockedIncrement(reinterpret_cast<volatile long*>(static_cast<unsigned char*>(made->ctrl)+0xC));
    r.obj=made->ctrl ? made->obj : nullptr;
    r.ctrl=made->ctrl;
    r.index=listIndex;
    DropWeak(old);
    if(index-kBvmPlayerSlots+1>t->used)t->used=index-kBvmPlayerSlots+1;
}
// `bvm`'s table let go: the object's own rows are going (its destructor, 0x20EB81).
void Edf5BvmRowsGone(const void* bvm) noexcept {
    for(Table** at=&tables;*at;at=&(*at)->next) {
        Table* t=*at;
        if(t->bvm!=bvm)continue;
        *at=t->next;
        for(std::uint32_t i=0;i<t->capacity;++i)DropWeak(t->row[i].ctrl);
        delete[] t->row;
        delete t;
        return;
    }
}

using RowsGoneFn=void(__fastcall*)(void*,std::size_t,std::size_t,void*);
void __fastcall RowsGoneHook(void* rows,std::size_t size,std::size_t count,void* dtor) {
    reinterpret_cast<RowsGoneFn>(image+kVectorDtor)(rows,size,count,dtor);
    Edf5BvmRowsGone(static_cast<unsigned char*>(rows)-kRows);
}

// A BVM object as a native by index reads it, its row 0 the plugin's row of the player: the process pointer (+0x70,
// 0x22A650's wait) and the row (+0x168). An alias: the plugin's row keeps the reference through the call.
struct alignas(16) View {
    unsigned char bytes[kRows+sizeof(Row)];
    View(const unsigned char* bvm,const Row& row) noexcept {
        std::memset(bytes,0,sizeof(bytes));
        std::memcpy(bytes+0x70,bvm+0x70,8);
        std::memcpy(bytes+kRows,&row,sizeof(row));
    }
};
const Row* RowOf(const void* bvm,std::int32_t index) noexcept {
    return index>=0 ? static_cast<const Row*>(Edf5BvmRowAt(bvm,static_cast<std::uint64_t>(index))) : nullptr;
}
using GetFn=std::int32_t(__fastcall*)(void*,std::int32_t);
using WaitFn=void(__fastcall*)(void*,std::int32_t,std::int32_t,std::int32_t);
using SetFn=void(__fastcall*)(void*,std::int32_t,std::int32_t);
using FlagFn=void(__fastcall*)(void*,bool);
std::int32_t __fastcall GetHook(unsigned char* bvm,std::int32_t index) {
    if(const Row* r=RowOf(bvm,index)){View v(bvm,*r);return reinterpret_cast<GetFn>(image+kGet)(v.bytes,0);}
    return reinterpret_cast<GetFn>(image+kGet)(bvm,index);
}
void __fastcall WaitHook(unsigned char* bvm,std::int32_t index,std::int32_t mask,std::int32_t enough) {
    if(const Row* r=RowOf(bvm,index)){View v(bvm,*r);reinterpret_cast<WaitFn>(image+kWait)(v.bytes,0,mask,enough);return;}
    reinterpret_cast<WaitFn>(image+kWait)(bvm,index,mask,enough);
}
void __fastcall SetHook(unsigned char* bvm,std::int32_t index,std::int32_t value) {
    if(const Row* r=RowOf(bvm,index)){View v(bvm,*r);reinterpret_cast<SetFn>(image+kSet)(v.bytes,0,value);return;}
    reinterpret_cast<SetFn>(image+kSet)(bvm,index,value);
}
// Every player's: the game's four, then the plugin's rows four at a time (the function walks rcx+0x170 on).
template<unsigned kFn>
void __fastcall FlagHook(unsigned char* bvm,bool on) {
    reinterpret_cast<FlagFn>(image+kFn)(bvm,on);
    if(const Table* t=Find(bvm))
        for(std::uint32_t i=0;i<t->used;i+=4)
            reinterpret_cast<FlagFn>(image+kFn)(reinterpret_cast<unsigned char*>(&t->row[i])-kRows,on);
}

// Factor_PlayerAreaIn's players: the game's four copies, then the plugin's rows (aliases: Initialize takes its own
// reference of each, 0x2079B3, and keeps an empty one empty). `areaBvm` set by the stub the call lands on (rbp there:
// 0x2214DE). Game thread; Initialize does not come back here.
using AreaFn=bool(__fastcall*)(void*,void*,const Shared*,std::uint32_t,std::int32_t,std::int32_t*);
const void* areaBvm=nullptr;
Shared areaPlayers[kBvmPlayerCapacity];
bool __fastcall AreaHook(void* factor,void* area,const Shared* players,std::uint32_t count,std::int32_t flag,std::int32_t* mode) {
    const Table* t=Find(areaBvm);
    const auto init=reinterpret_cast<AreaFn>(image+kAreaInit);
    if(!t || !t->used || count!=kBvmPlayerSlots || !players)return init(factor,area,players,count,flag,mode);
    std::uint32_t n=0;
    for(;n<kBvmPlayerSlots;++n)areaPlayers[n]=players[n];
    for(std::uint32_t i=0;i<t->used && n<kBvmPlayerCapacity;++i,++n)
        areaPlayers[n]=t->row[i].ctrl ? Shared{t->row[i].obj,t->row[i].ctrl} : Shared{};
    return init(factor,area,areaPlayers,n,flag,mode);
}
}  // namespace

// The BVM creation loop's count online: the session's players, with what each needs armed for the creation
// (Edf5BvmOnlinePlayerArgs). Called from the patched 0x22B36C on the game thread (its registers: rax back to the loop
// as r13d; rcx..r11 free there). 0, no player created, when the per-player arguments cannot be put right (the
// participant gate, which wraps the creation, not installed, or the session's users unreadable): every machine creating
// every player for its own pads and splitting the screen by them is worse than no player. More than four only when
// every machine makes them (edf5online_room.h): otherwise four, as every machine of the room then does.
std::uint32_t Edf5BvmOnlinePlayers() noexcept {
    creation=Creation{};
    noObjectLogged=false;
    std::uint32_t players=0,local=0;
    bool read=false;
    __try {
        const auto status=At<unsigned char*>(image,kGameStatus);
        if(status){players=At<std::uint32_t>(status,kSessionPlayers);local=At<std::uint32_t>(status,kLocalPlayers);read=true;}
    } __except(EXCEPTION_EXECUTE_HANDLER){read=false;}
    if(!read){Log("EDF5 online: no game status, no player created");return 0;}
    std::uint32_t count=players;
    if(players>kBvmPlayerCapacity) {
        if(!clampLogged)Log("EDF5 online: %u players in the session, an EDF5 mission creates %u",players,kBvmPlayerCapacity);
        clampLogged=true;
        count=kBvmPlayerCapacity;
    } else clampLogged=false;
    if(!MissionParticipantGateReady()) {
        if(!gateLogged)Log("EDF5 online: the player creation's wrapper (mission_participant_gate) is not installed: no player created");
        gateLogged=true;
        return 0;
    }
    if(count>kBvmPlayerSlots) {
        char why[320]="";
        bool allowed=Edf5CoopBvmTableHooked();
        if(!allowed)std::snprintf(why,sizeof(why),"EDF6Coop 2.5.0+'s BvmPlayerTableHooks are not in place on this machine");
        else allowed=Edf5RoomAllowsBvmPlayers(count,why,sizeof(why));
        if(!allowed) {
            Log("EDF5 online: %u players, but %s: every machine of the room creates players 1-%u only (players %u.. have "
                "no soldier in this mission)",count,why,kBvmPlayerSlots,kBvmPlayerSlots+1);
            count=kBvmPlayerSlots;
            extendLogged=false;
        }
    }
    if(!ReadRemote(count,creation.remote)) {
        Log("EDF5 online: the session's %u users unreadable: no player created",count);
        creation=Creation{};
        return 0;
    }
    bool localFound=false;
    for(std::uint32_t i=0;i<count;++i)localFound=localFound || !creation.remote[i];
    if(players>count && !localFound)Log("EDF5 online: this machine's player is past the %u a mission creates: it has none",count);
    else if(!localFound)Log("EDF5 online: no user of this machine among the session's %u: it gets no player",count);
    if(count>kBvmPlayerSlots && !extendLogged)
        Log("EDF5 online: %u players, past the script loop's %u: players %u.. stand as EDF6Coop places AngelScript's and are "
            "in the script's player table (route guide, area waits, per-player natives)",count,kBvmPlayerSlots,kBvmPlayerSlots+1);
    extendLogged=count>kBvmPlayerSlots;
    creation.count=count;
    creation.split=local>0 ? static_cast<int>(local) : 1;
    creation.armed=count>0;
    return count;
}

// Before the BVM creation of mission player `index` (0x22B626 -> 0x22AB90, its 5th and 6th arguments): online, this
// machine's pad or -1, and the split by the local players. False offline and outside the armed online loop.
bool Edf5BvmOnlinePlayerArgs(int index,int* pad,int* split) noexcept {
    if(!creation.armed || index<0 || static_cast<std::uint32_t>(index)>=creation.count || !pad || !split)return false;
    // An online loop left early (0x22B5EC: no spawn point) is still armed: never its arguments for an offline creation.
    if(!BvmOnline()){creation=Creation{};return false;}
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

// A mission start (mission.cpp): whatever an earlier mission's loop left armed is gone.
void ResetEdf5Online() noexcept { creation=Creation{}; }

namespace {
// Everything the patch adds, in one page near the image (position independent: absolute jumps and calls): the stubs the
// sites jump to, the thunks the redirected calls land on. Labels as offsets into `code`.
struct Stubs {
    unsigned char code[0x800];
    std::size_t size=0;
    std::size_t normalize=0,layoutAt=0,row=0,rowIndex=0,guide=0,first=0,area=0;
    std::size_t object=0,get=0,wait=0,set=0,flag11=0,flag7=0,rowsGone=0;
    void Bytes(std::initializer_list<unsigned char> bytes) noexcept {for(unsigned char b:bytes)code[size++]=b;}
    void Imm64(std::uint64_t value) noexcept {std::memcpy(code+size,&value,8);size+=8;}
    void Imm32(std::uint32_t value) noexcept {std::memcpy(code+size,&value,4);size+=4;}
    void JumpTo(const void* to) noexcept {Bytes({0xFF,0x25,0,0,0,0});Imm64(reinterpret_cast<std::uint64_t>(to));}  // jmp [rip]
    void JumpTo(unsigned rva) noexcept {JumpTo(image+rva);}
    void Call(const void* fn) noexcept {Bytes({0x48,0xB8});Imm64(reinterpret_cast<std::uint64_t>(fn));Bytes({0xFF,0xD0});}  // mov rax; call rax
    std::size_t Jcc8(unsigned char op) noexcept {Bytes({op,0});return size-1;}   // its rel8 bound later
    void Bind(std::size_t rel8) noexcept {code[rel8]=static_cast<unsigned char>(size-(rel8+1));}
};
void BuildStubs(Stubs& s) noexcept {
    // 0x22B470: lea rbx,[rbp+0x170]; mov edi,4; cmp r13d,edi; cmovb edi,r13d - the normalisation over min(count, 4).
    s.normalize=s.size;
    s.Bytes({0x48,0x8D,0x9D,0x70,0x01,0x00,0x00,0xBF,0x04,0x00,0x00,0x00,0x41,0x39,0xFD,0x41,0x0F,0x42,0xFD});
    s.JumpTo(kNormalizeBack);
    // 0x22B569: mov rdi,r15; cmp r13d,4; ja extended; the loop's own lea r15 / lea r12; back. Extended:
    // Edf5BvmPlayerLayout(rbp+0x170, rbp+0x150, r13d); r15 its offsets, r12 its pads; back.
    s.layoutAt=s.size;
    s.Bytes({0x49,0x8B,0xFF,0x41,0x83,0xFD,0x04});
    const std::size_t extended=s.Jcc8(0x77);
    s.Bytes({0x4C,0x8D,0xBD,0x70,0x01,0x00,0x00,0x4C,0x8D,0xA5,0x50,0x01,0x00,0x00});
    s.JumpTo(kLayoutBack);
    s.Bind(extended);
    s.Bytes({0x48,0x8D,0x8D,0x70,0x01,0x00,0x00,0x48,0x8D,0x95,0x50,0x01,0x00,0x00,0x45,0x8B,0xC5});
    s.Call(reinterpret_cast<const void*>(&Edf5BvmPlayerLayout));
    s.Bytes({0x4C,0x8B,0xF8,0x4C,0x8D,0xA0});
    s.Imm32(static_cast<std::uint32_t>(kLayoutPads));
    s.JumpTo(kLayoutBack);
    // 0x22B69D: cmp r14d,4; jae skip; the row's own three instructions; back. Skip: past the row's write, 0x22B6E8.
    s.row=s.size;
    s.Bytes({0x41,0x83,0xFE,0x04});
    const std::size_t skip=s.Jcc8(0x73);
    s.Bytes({0x45,0x33,0xC0,0x33,0xC0,0x48,0x8B,0x4C,0x24,0x68});
    s.JumpTo(kRowBack);
    s.Bind(skip);
    s.JumpTo(kRowSkip);
    // 0x22B802: cmp r14d,4; jae plugin; mov [rdi+rsi+0x168],ebx; back. Plugin: Edf5BvmStoreRow(rsi, r14d, ebx,
    // rsp+0x60); back (rsp is the body's: 16-aligned, its [rsp..rsp+0x1F] the outgoing shadow, nothing volatile live
    // after 0x22B809).
    s.rowIndex=s.size;
    s.Bytes({0x41,0x83,0xFE,0x04});
    const std::size_t plugin=s.Jcc8(0x73);
    s.Bytes({0x89,0x9C,0x37,0x68,0x01,0x00,0x00});
    s.JumpTo(kRowIndexBack);
    s.Bind(plugin);
    s.Bytes({0x48,0x8B,0xCE,0x41,0x8B,0xD6,0x44,0x8B,0xC3,0x4C,0x8D,0x4C,0x24,0x60});
    s.Call(reinterpret_cast<const void*>(&Edf5BvmStoreRow));
    s.JumpTo(kRowIndexBack);
    // 0x21F3C2 (each pass of the route guide's loop): cmp r12,4; jb stock; rax = Edf5BvmRowAt(r15, r12); test rax,rax;
    // jz stock; r8 = (rax - r15 - 0x168) / 8 (the row r15 + r8*8 + 0x168 is the plugin's); the pass's xorps / movdqu and
    // its mov rdx,[r15+r8*8+0x178]; on at 0x21F3D7. Stock: lea r8,[r12+r12*2]; xorps xmm0,xmm0; back to 0x21F3C9 (on to
    // EDF6Coop's 0x21F3CF). rsp the body's (16-aligned, shadow free); rax..r11 dead at the loop's head.
    s.guide=s.size;
    s.Bytes({0x49,0x83,0xFC,0x04});
    const std::size_t low=s.Jcc8(0x72);
    s.Bytes({0x49,0x8B,0xCF,0x49,0x8B,0xD4});
    s.Call(reinterpret_cast<const void*>(&Edf5BvmRowAt));
    s.Bytes({0x48,0x85,0xC0});
    const std::size_t none=s.Jcc8(0x74);
    s.Bytes({0x49,0x2B,0xC7,0x48,0x2D,0x68,0x01,0x00,0x00,0x48,0xC1,0xF8,0x03,0x4C,0x8B,0xC0,0x0F,0x57,0xC0,
             0xF3,0x0F,0x7F,0x44,0x24,0x30,0x4B,0x8B,0x94,0xC7,0x78,0x01,0x00,0x00});
    s.JumpTo(kGuideRead);
    s.Bind(low);
    s.Bind(none);   // both rel8s land here: Bind writes each from its own byte
    s.Bytes({0x4F,0x8D,0x04,0x64,0x0F,0x57,0xC0});
    s.JumpTo(kGuideBack);
    // 0x22912B (the first player's search): inc r14; add rdi,0x18; cmp r14,4; jae plugin; back to the pass. Plugin:
    // rax = Edf5BvmRowAt(r13, r14); none: on at 0x22913C (nobody); else rdi = its +8 (the pass reads [rdi], [rdi+8]
    // and [r12+rdi+0x178], r12 = -0x170); back to the pass. rsp the body's; rax..r11 dead at the pass's head.
    s.first=s.size;
    s.Bytes({0x49,0xFF,0xC6,0x48,0x83,0xC7,0x18,0x49,0x83,0xFE,0x04});
    const std::size_t past=s.Jcc8(0x73);
    s.JumpTo(kFirstLoop);
    s.Bind(past);
    s.Bytes({0x49,0x8B,0xCD,0x49,0x8B,0xD6});
    s.Call(reinterpret_cast<const void*>(&Edf5BvmRowAt));
    s.Bytes({0x48,0x85,0xC0});
    const std::size_t nobody=s.Jcc8(0x74);
    s.Bytes({0x48,0x8D,0x78,0x08});
    s.JumpTo(kFirstLoop);
    s.Bind(nobody);
    s.JumpTo(kFirstNone);
    // 0x221687's call lands here: areaBvm = rbp (0x2214B0's BVM object); on to AreaHook, its arguments untouched.
    s.area=s.size;
    s.Bytes({0x48,0xB8});s.Imm64(reinterpret_cast<std::uint64_t>(&areaBvm));
    s.Bytes({0x48,0x89,0x28});
    s.JumpTo(reinterpret_cast<const void*>(&AreaHook));
    // The redirected calls' thunks.
    s.object=s.size;s.JumpTo(reinterpret_cast<const void*>(&Edf5BvmOnlineObject));
    s.get=s.size;s.JumpTo(reinterpret_cast<const void*>(&GetHook));
    s.wait=s.size;s.JumpTo(reinterpret_cast<const void*>(&WaitHook));
    s.set=s.size;s.JumpTo(reinterpret_cast<const void*>(&SetHook));
    s.flag11=s.size;s.JumpTo(reinterpret_cast<const void*>(&FlagHook<kFlag11>));
    s.flag7=s.size;s.JumpTo(reinterpret_cast<const void*>(&FlagHook<kFlag7>));
    s.rowsGone=s.size;s.JumpTo(reinterpret_cast<const void*>(&RowsGoneHook));
}
// `jmp rel32` to `to`, the rest of the site's `size` bytes int3 (never run: the stub jumps back past them).
bool JumpSite(unsigned rva,std::size_t size,const unsigned char* to,unsigned char* bytes) noexcept {
    const auto rel=to-(image+rva+5);
    if(rel<INT32_MIN || rel>INT32_MAX || size<5)return false;
    bytes[0]=0xE9;
    const auto rel32=static_cast<std::int32_t>(rel);
    std::memcpy(bytes+1,&rel32,4);
    for(std::size_t i=5;i<size;++i)bytes[i]=0xCC;
    return true;
}
// The rel32 call at `rva`, whose target is `target`.
bool CallsTo(unsigned rva,unsigned target) noexcept {
    if(image[rva]!=0xE8)return false;
    std::int32_t rel=0;
    std::memcpy(&rel,image+rva+1,4);
    return image+rva+5+rel==image+target;
}
// A site's new bytes and the bytes it had (put back if a later write fails).
struct Write { unsigned rva; std::size_t size; unsigned char bytes[kCreatePatchSize]; unsigned char was[kCreatePatchSize]; };
}  // namespace

// All or nothing: players preloaded but never created is the black screen this fixes, created but not preloaded worse,
// a loop of more than four with any of its sites stock writes past the loop's stack and the script's table, and players
// 5.. in the table with any of its readers stock are seen by some natives and not others. Every byte relied on checked
// first; one page for every stub and thunk; on a failed write every site written is put back and the page freed.
bool InstallEdf5Online() noexcept {
    static bool tried=false;
    if(tried)return ready;
    tried=true;
    struct Sig { unsigned rva; const unsigned char* code; std::size_t size; };
    const Sig sigs[]={{kPreloadCallAt,kPreloadCallCode,sizeof(kPreloadCallCode)},{kBvmPreload,kBvmPreloadSig,sizeof(kBvmPreloadSig)},
                      {kAsPreload,kAsPreloadSig,sizeof(kAsPreloadSig)},{kCreateAt,kCreateCode,sizeof(kCreateCode)},
                      {kObjectAt,kObjectCode,sizeof(kObjectCode)},{kNormalizeAt,kNormalizeCode,sizeof(kNormalizeCode)},
                      {kLayoutAt,kLayoutCode,sizeof(kLayoutCode)},{kRowAt,kRowCode,sizeof(kRowCode)},
                      {kRowIndexAt,kRowIndexCode,sizeof(kRowIndexCode)},
                      {kGuideAt,kGuideCode,sizeof(kGuideCode)},{kGuideBack,kGuideClear,sizeof(kGuideClear)},
                      {kGuideRead,kGuideTest,sizeof(kGuideTest)},{0x21F504,kGuideObject,sizeof(kGuideObject)},
                      {0x21F511,kGuideCtrl,sizeof(kGuideCtrl)},
                      {kFirstAt,kFirstCode,sizeof(kFirstCode)},{0x229099,kFirstStart,sizeof(kFirstStart)},
                      {kFirstLoop,kFirstPass,sizeof(kFirstPass)},{0x2290E0,kFirstTake,sizeof(kFirstTake)},
                      {kFirstNone,kFirstNoneCode,sizeof(kFirstNoneCode)},
                      {kGet+0xF,kGetHead,sizeof(kGetHead)},{0x222784,kGetObject,sizeof(kGetObject)},
                      {kSet+0x1D,kSetHead,sizeof(kSetHead)},{0x228B10,kSetObject,sizeof(kSetObject)},
                      {kWait+0x1E,kWaitHead,sizeof(kWaitHead)},{0x22A6B0,kWaitObject,sizeof(kWaitObject)},
                      {0x22A73D,kWaitProcess,sizeof(kWaitProcess)},{0x22A77B,kWaitProcess,sizeof(kWaitProcess)},
                      {kFlag11+kFlagRows,kFlagRowsCode,sizeof(kFlagRowsCode)},{kFlag7+kFlagRows,kFlagRowsCode,sizeof(kFlagRowsCode)},
                      {kAreaKeep,kAreaKeepCode,sizeof(kAreaKeepCode)},{kAreaRows,kAreaRowsCode,sizeof(kAreaRowsCode)},
                      {kAreaArgs,kAreaArgsCode,sizeof(kAreaArgsCode)},{kRowsGoneArgs,kRowsGoneCode,sizeof(kRowsGoneCode)}};
    struct Call { unsigned rva,target; };
    const Call calls[]={{kPreloadCall,kBvmPreload},{kObjectCall,kOnlineObject},{kGetCall,kGet},{kWaitCall,kWait},
                        {kSetCall,kSet},{kFlag11Call,kFlag11},{kFlag7Call,kFlag7},{kAreaCall,kAreaInit},
                        {kRowsGoneCall,kVectorDtor}};
    bool known=true;
    for(const Sig& s:sigs)known=known && Matches(s.rva,s.code,s.size);
    for(const Call& c:calls)known=known && CallsTo(c.rva,c.target);
    if(!known) {
        Log("HOOK edf5 online=0 (the BVM player natives are not the known build's)");
        return false;
    }
    Stubs stubs;
    BuildStubs(stubs);
    auto* const page=static_cast<unsigned char*>(AllocateNearCode(image+kCreateLoop,stubs.code,stubs.size));
    if(!page) {
        Log("HOOK edf5 online=0 (no code page within reach of CreatePlayer's loop)");
        return false;
    }
    // The writes, the creation's online branch into the loop last: until it, nothing reaches a loop of more than the
    // local players, and every other site does what it did for four or fewer (no table of the plugin's exists).
    Write writes[24]{};
    std::size_t count=0;
    bool built=true;
    const auto jump=[&](unsigned rva,std::size_t size,std::size_t entry) noexcept {
        Write& w=writes[count++];
        w.rva=rva;w.size=size;
        std::memcpy(w.was,image+rva,size);
        built=built && JumpSite(rva,size,page+entry,w.bytes);
    };
    const auto call=[&](unsigned rva,const unsigned char* to) noexcept {
        Write& w=writes[count++];
        w.rva=rva;w.size=5;
        std::memcpy(w.was,image+rva,5);
        const auto rel=to-(image+rva+5);
        built=built && rel>=INT32_MIN && rel<=INT32_MAX;
        const auto rel32=static_cast<std::int32_t>(rel);
        w.bytes[0]=0xE8;
        std::memcpy(w.bytes+1,&rel32,4);
    };
    call(kPreloadCall,image+kAsPreload);
    call(kObjectCall,page+stubs.object);
    call(kRowsGoneCall,page+stubs.rowsGone);
    call(kGetCall,page+stubs.get);
    call(kWaitCall,page+stubs.wait);
    call(kSetCall,page+stubs.set);
    call(kFlag11Call,page+stubs.flag11);
    call(kFlag7Call,page+stubs.flag7);
    call(kAreaCall,page+stubs.area);
    jump(kGuideAt,sizeof(kGuideCode),stubs.guide);
    jump(kFirstAt,sizeof(kFirstCode),stubs.first);
    jump(kNormalizeAt,sizeof(kNormalizeCode),stubs.normalize);
    jump(kLayoutAt,sizeof(kLayoutCode),stubs.layoutAt);
    jump(kRowAt,sizeof(kRowCode),stubs.row);
    jump(kRowIndexAt,sizeof(kRowIndexCode),stubs.rowIndex);
    {
        Write& w=writes[count++];
        w.rva=kCreateOnline;w.size=kCreatePatchSize;
        std::memcpy(w.was,image+kCreateOnline,kCreatePatchSize);
        const unsigned char patch[kCreatePatchSize]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,0x44,0x8B,0xE8,0xEB,
            static_cast<unsigned char>(kCreateLoop-(kCreateOnline+kCreatePatchSize))};
        std::memcpy(w.bytes,patch,sizeof(patch));
        const auto fn=reinterpret_cast<std::uintptr_t>(&Edf5BvmOnlinePlayers);
        std::memcpy(w.bytes+2,&fn,sizeof(fn));
    }
    if(!built) {
        VirtualFree(page,0,MEM_RELEASE);
        Log("HOOK edf5 online=0 (a site out of the code page's reach)");
        return false;
    }
    onlineObject=reinterpret_cast<OnlineObjectFn>(image+kOnlineObject);
    std::size_t written=0;
    while(written<count && writeCode(writes[written].rva,writes[written].bytes,writes[written].size))++written;
    if(written<count) {
        const unsigned failed=writes[written].rva;
        bool restored=true;
        while(written>0){--written;restored=writeCode(writes[written].rva,writes[written].was,writes[written].size) && restored;}
        if(restored)VirtualFree(page,0,MEM_RELEASE);   // kept if a site could not be put back: it may still jump there
        onlineObject=nullptr;
        Log("HOOK edf5 online=0 (EDF+0x%X not writable: %s)",failed,
            restored ? "every site written put back, nothing left patched" : "A SITE COULD NOT BE PUT BACK, its stubs kept");
        return false;
    }
    ready=true;
    Log("HOOK edf5 online=1 (EDF5 scripts online: PreloadPlayerResource is AngelScript's, CreatePlayer creates the session's "
        "players; past the loop's %u, up to %u, only with EDF6Coop 2.5.0+'s BvmPlayerTableHooks (EDF+0x21F3CF/0x222761/"
        "0x228AED/0x22A685) in place and every member running this build (EDF6Coop member attribute EDF6X_BVMPLAYERS): placed "
        "as EDF6Coop places AngelScript's, in the script's player table for its route guide, area waits and per-player "
        "natives; else four everywhere)",kBvmPlayerSlots,kBvmPlayerCapacity);
    return true;
}
bool Edf5OnlineReady() noexcept { return ready; }
}  // namespace crew
