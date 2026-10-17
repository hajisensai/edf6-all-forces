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
//    loop finished, then EDF6Coop's layout for AngelScript's (multislot SpawnOffset: along players 2-4's directions,
//    2x, 3x... as far, from the 33rd rings around player 1); with four or fewer the loop's own, untouched,
//  - players 5.. are not written into the four rows (0x22B69D, 0x22B802): the script's natives that read the rows by
//    player index see 0..3, as EDF6Coop bounds them (BvmPlayerTableHooks). Their object list (+0x1C8) and the
//    script's group (0x1F74A0) are vectors and get everyone.
// And the one creation's online half (0x22AB90) dereferences 0x591130's result unchecked (0x22AC3F), where
// AngelScript's (0x1DC544) gives back an empty player: 0x591130 returns null for an index no user stands at, or one the
// game's online player records do not take (0x5959E1 `cmp edi, 4`, widened by EDF6Coop's Extend=1). The call is given
// an empty record then, which 0x22AB90 takes its own empty path on (0x22ACDC -> 0x22AEF2).
#include "crew.h"
#include "memory.h"
#include "mission_participant_gate.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <initializer_list>

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
// 0x22B802: mov [rdi+rsi+0x168],ebx - the row's place in the object list.
constexpr unsigned kRowIndexAt=0x22B802,kRowIndexBack=0x22B809;
const unsigned char kRowIndexCode[]={0x89,0x9C,0x37,0x68,0x01,0x00,0x00};
// 0x22AC29: mov rdx,r9; mov ecx,r13d; call 0x591130 - the one creation's online player object.
constexpr unsigned kObjectAt=0x22AC29,kObjectCall=0x22AC2F,kOnlineObject=0x591130;
const unsigned char kObjectCode[]={0x49,0x8B,0xD1,0x41,0x8B,0xCD,0xE8,0xFC,0x64,0x36,0x00};

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

bool ready=false;
bool clampLogged=false;
bool gateLogged=false;
bool extendLogged=false;
bool noObjectLogged=false;

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

// sin and cos of a turn's slot/seats by arithmetic alone: the CRT's x64 sin/cos pick an FMA3 path by the CPU, and
// every machine places every player. The quarter turn nearest, then Taylor to x^17 on |x| <= pi/4 (error < 1e-16).
void TurnSinCos(std::uint32_t slot,std::uint32_t seats,double& s,double& c) noexcept {
    const double quarters=4.0*static_cast<double>(slot)/static_cast<double>(seats);
    const auto q=static_cast<std::uint32_t>(quarters+0.5);
    const double x=(quarters-static_cast<double>(q))*1.5707963267948966;
    const double x2=x*x;
    double sx=x,cx=1.0,term=x;
    for(int k=1;k<=8;++k){term*=-x2/static_cast<double>((2*k)*(2*k+1));sx+=term;}
    term=1.0;
    for(int k=1;k<=8;++k){term*=-x2/static_cast<double>((2*k-1)*(2*k));cx+=term;}
    switch(q&3) {
        case 0: s=sx;c=cx;break;
        case 1: s=cx;c=-sx;break;
        case 2: s=-sx;c=-cx;break;
        default: s=-cx;c=sx;break;
    }
}

// Where player `index` starts relative to the spawn point, from the loop's four finished offsets (`table`, float4 each,
// y 0): EDF6Coop's layout for AngelScript's loop, whose four finished offsets are these (the same constants 0x1765B30,
// 0x1792F10..30 and scale 0x1765B90, normalised, scaled and turned the same way: 0x1D96B0 / 0x22B490). Players 1-4 keep
// the loop's; 5..32 stand along players 2-4's directions, 2x, 3x... as far (x and z scaled); from the 33rd rings around
// player 1, one spacing (players 1-2's distance, 3 when under 0.5) apart, the first just past the 32's lines.
void Edf5BvmSpawnOffset(std::uint32_t index,const float* table,float* out) noexcept {
    constexpr std::uint32_t kLinePlayers=32;
    if(index<kBvmPlayerSlots) {std::memcpy(out,table+index*4,4*sizeof(float));return;}
    if(index<kLinePlayers) {
        const std::uint32_t extra=index-kBvmPlayerSlots;
        std::memcpy(out,table+(1+extra%3)*4,4*sizeof(float));
        const float scale=2.0f+static_cast<float>(extra/3);
        out[0]*=scale;
        out[2]*=scale;
        return;
    }
    std::memcpy(out,table,4*sizeof(float));
    const float dx=table[4]-table[0],dz=table[6]-table[2];
    float spacing=std::sqrt(dx*dx+dz*dz);
    if(!(spacing>0.5f))spacing=3.0f;
    const float first=spacing*(2.0f+static_cast<float>((kLinePlayers-5)/3))+spacing;
    std::uint32_t slot=index-kLinePlayers;
    for(int ring=0;;++ring) {
        const float radius=first+spacing*static_cast<float>(ring);
        const auto seats=static_cast<std::uint32_t>(6.2831853f*radius/spacing);
        if(slot<seats) {
            double s=0.0,c=1.0;
            TurnSinCos(slot,seats,s,c);
            out[0]=table[0]+static_cast<float>(static_cast<double>(radius)*c);
            out[2]=table[2]+static_cast<float>(static_cast<double>(radius)*s);
            return;
        }
        slot-=seats;
    }
}

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
}  // namespace

// The BVM creation loop's count online: the session's players, with what each needs armed for the creation
// (Edf5BvmOnlinePlayerArgs). Called from the patched 0x22B36C on the game thread (its registers: rax back to the loop
// as r13d; rcx..r11 free there). 0, no player created, when the per-player arguments cannot be put right (the
// participant gate, which wraps the creation, not installed, or the session's users unreadable): every machine creating
// every player for its own pads and splitting the screen by them is worse than no player.
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
        Log("EDF5 online: %u players, past the script loop's %u: players %u.. stand as EDF6Coop places AngelScript's; the "
            "script's per-player natives see players 1-%u",count,kBvmPlayerSlots,kBvmPlayerSlots+1,kBvmPlayerSlots);
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
// The stubs the loop's four sites jump to (one page near the image, position independent: absolute jumps back).
struct Stubs {
    unsigned char code[256];
    std::size_t size=0;
    std::size_t normalize=0,layoutAt=0,row=0,rowIndex=0;
    void Bytes(std::initializer_list<unsigned char> bytes) noexcept {for(unsigned char b:bytes)code[size++]=b;}
    void Imm64(std::uint64_t value) noexcept {std::memcpy(code+size,&value,8);size+=8;}
    void Imm32(std::uint32_t value) noexcept {std::memcpy(code+size,&value,4);size+=4;}
    void JumpTo(unsigned rva) noexcept {Bytes({0xFF,0x25,0,0,0,0});Imm64(reinterpret_cast<std::uint64_t>(image+rva));}  // jmp [rip]
};
void BuildStubs(Stubs& s) noexcept {
    // 0x22B470: lea rbx,[rbp+0x170]; mov edi,4; cmp r13d,edi; cmovb edi,r13d - the normalisation over min(count, 4).
    s.normalize=s.size;
    s.Bytes({0x48,0x8D,0x9D,0x70,0x01,0x00,0x00,0xBF,0x04,0x00,0x00,0x00,0x41,0x39,0xFD,0x41,0x0F,0x42,0xFD});
    s.JumpTo(kNormalizeBack);
    // 0x22B569: mov rdi,r15; cmp r13d,4; ja extended; the loop's own lea r15 / lea r12; back. Extended:
    // Edf5BvmPlayerLayout(rbp+0x170, rbp+0x150, r13d); r15 its offsets, r12 its pads; back.
    s.layoutAt=s.size;
    s.Bytes({0x49,0x8B,0xFF,0x41,0x83,0xFD,0x04,0x77,0x1C});
    s.Bytes({0x4C,0x8D,0xBD,0x70,0x01,0x00,0x00,0x4C,0x8D,0xA5,0x50,0x01,0x00,0x00});
    s.JumpTo(kLayoutBack);
    s.Bytes({0x48,0x8D,0x8D,0x70,0x01,0x00,0x00,0x48,0x8D,0x95,0x50,0x01,0x00,0x00,0x45,0x8B,0xC5,0x48,0xB8});
    s.Imm64(reinterpret_cast<std::uint64_t>(&Edf5BvmPlayerLayout));
    s.Bytes({0xFF,0xD0,0x4C,0x8B,0xF8,0x4C,0x8D,0xA0});
    s.Imm32(static_cast<std::uint32_t>(kLayoutPads));
    s.JumpTo(kLayoutBack);
    // 0x22B69D: cmp r14d,4; jae skip; the row's own three instructions; back. Skip: past the row's write, 0x22B6E8.
    s.row=s.size;
    s.Bytes({0x41,0x83,0xFE,0x04,0x73,0x18,0x45,0x33,0xC0,0x33,0xC0,0x48,0x8B,0x4C,0x24,0x68});
    s.JumpTo(kRowBack);
    s.JumpTo(kRowSkip);
    // 0x22B802: cmp r14d,4; jae +7; mov [rdi+rsi+0x168],ebx; back.
    s.rowIndex=s.size;
    s.Bytes({0x41,0x83,0xFE,0x04,0x73,0x07,0x89,0x9C,0x37,0x68,0x01,0x00,0x00});
    s.JumpTo(kRowIndexBack);
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
// A redirected call back on its original target (through the thunk it now lands on).
void UndoRedirect(unsigned callRva,unsigned originalRva) noexcept {
    std::int32_t rel=0;
    std::memcpy(&rel,image+callRva+1,4);
    bool undone=false;
    RedirectCall(image+callRva,image+callRva+5+rel,image+originalRva,undone);
}
}  // namespace

// All or nothing: players preloaded but never created is the black screen this fixes, created but not preloaded worse,
// and a loop of more than four with any of its four sites stock writes past the loop's stack and the script's table.
bool InstallEdf5Online() noexcept {
    static bool tried=false;
    if(tried)return ready;
    tried=true;
    struct Site { unsigned rva; const unsigned char* code; std::size_t size; };
    const Site sites[]={{kNormalizeAt,kNormalizeCode,sizeof(kNormalizeCode)},{kLayoutAt,kLayoutCode,sizeof(kLayoutCode)},
                        {kRowAt,kRowCode,sizeof(kRowCode)},{kRowIndexAt,kRowIndexCode,sizeof(kRowIndexCode)}};
    bool known=Matches(kPreloadCallAt,kPreloadCallCode,sizeof(kPreloadCallCode)) &&
               Matches(kBvmPreload,kBvmPreloadSig,sizeof(kBvmPreloadSig)) &&
               Matches(kAsPreload,kAsPreloadSig,sizeof(kAsPreloadSig)) &&
               Matches(kCreateAt,kCreateCode,sizeof(kCreateCode)) &&
               Matches(kObjectAt,kObjectCode,sizeof(kObjectCode));
    for(const Site& site:sites)known=known && Matches(site.rva,site.code,site.size);
    if(!known) {
        Log("HOOK edf5 online=0 (the BVM player natives are not the known build's)");
        return false;
    }
    Stubs stubs;
    BuildStubs(stubs);
    auto* const page=static_cast<unsigned char*>(AllocateNearCode(image+kCreateLoop,stubs.code,stubs.size));
    unsigned char jumps[4][sizeof(kLayoutCode)]{};
    const std::size_t entries[]={stubs.normalize,stubs.layoutAt,stubs.row,stubs.rowIndex};
    bool built=page!=nullptr;
    for(int i=0;i<4 && built;++i)built=JumpSite(sites[i].rva,sites[i].size,page+entries[i],jumps[i]);
    if(!built) {
        Log("HOOK edf5 online=0 (no code page within reach of CreatePlayer's loop)");
        return false;
    }
    unsigned char patch[kCreatePatchSize]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,0x44,0x8B,0xE8,0xEB,0};
    const auto count=reinterpret_cast<std::uintptr_t>(&Edf5BvmOnlinePlayers);
    std::memcpy(patch+2,&count,sizeof(count));
    patch[16]=static_cast<unsigned char>(kCreateLoop-(kCreateOnline+kCreatePatchSize));

    onlineObject=reinterpret_cast<OnlineObjectFn>(image+kOnlineObject);
    bool changed=false;
    if(!RedirectCall(image+kPreloadCall,image+kBvmPreload,image+kAsPreload,changed)) {
        Log("HOOK edf5 online=0 (native 0x10's call %s)",changed ? "half patched" : "not patched");
        return false;
    }
    changed=false;
    if(!RedirectCall(image+kObjectCall,image+kOnlineObject,reinterpret_cast<void*>(&Edf5BvmOnlineObject),changed)) {
        UndoRedirect(kPreloadCall,kBvmPreload);
        Log("HOOK edf5 online=0 (the creation's online object call %s)",changed ? "half patched" : "not patched");
        return false;
    }
    // The loop's four sites, then the online branch into it: until that last write nothing reaches a loop of more than
    // the local players.
    int written=0;
    bool ok=true;
    while(ok && written<4) {
        ok=WriteCode(sites[written].rva,jumps[written],sites[written].size);
        if(ok)++written;
    }
    ok=ok && WriteCode(kCreateOnline,patch,sizeof(patch));
    if(!ok) {
        for(int i=0;i<written;++i)WriteCode(sites[i].rva,sites[i].code,sites[i].size);
        UndoRedirect(kObjectCall,kOnlineObject);
        UndoRedirect(kPreloadCall,kBvmPreload);
        Log("HOOK edf5 online=0 (CreatePlayer not patched; native 0x10 back to the BVM preload)");
        return false;
    }
    ready=true;
    Log("HOOK edf5 online=1 (EDF5 scripts online: PreloadPlayerResource is AngelScript's, CreatePlayer creates the session's "
        "players, up to %u; past the loop's %u as EDF6Coop places AngelScript's)",kBvmPlayerCapacity,kBvmPlayerSlots);
    return true;
}
bool Edf5OnlineReady() noexcept { return ready; }
}  // namespace crew
