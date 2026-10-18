// An NPC soldier's backpack weapon (its sub weapon slots), used the way the game uses a player's (docs/npc-backpack-re.md;
// the user, 2026-10-18: 「让npc可以使用背包位武器如突击的手雷，飞翼的短剑等」). EDF.dll TimeDateStamp 0x678CCB46, RVAs.
//
// Why an NPC never used one (both H, static):
//  1. it had none: every stock AI template declares soldier_sub_weapon_slot = 0 and loads its one AI weapon, so the
//     setup 0x592FA0 leaves the sub weapon vector (+0x19F8: +0x08 records of 0x20, +0x18 their count) empty. The player
//     templates declare [[2,'weapon',0],[3,'weapon',0]]. A support soldier now comes from a generated template carrying its
//     backpack weapon the player's way (tools/support_loadout.py with_pack);
//  2. the backpack's input 0x5A1D70 (called from SoldierBase slot 5 at 0x59AC65, every soldier's frame) returns at once
//     for a soldier without a pad (0x5A1DA4: cmp [rcx+0x340],0; je): no NPC press ever reaches it.
// The use itself is not the player's input: it is what that input does on a tap and what every other machine does on its
// message (SoldierBase receive 0x59E210, message 0x10, 0x59E6FD), which has no pad test: WeaponSet 0's sub weapon (+0x140)
// set, its state machine (+0x110) moved to the sub weapon state 0x5A28A0 (the weapon's own animation, its trigger, back),
// then +0x1A20 cleared. NpcPackUse does that with the same gates the input keeps, and sends the same message 0x10 the input
// sends (NetworkObject +0x120 slot 14) when this soldier is the registered copy every other machine follows.
// The code read for all of it is held to its bytes (npc_backpack_signatures.inc, tools/npc_backpack_sigs.py; checked
// against EDF.dll by tests/npc_backpack_native_audit.py): one byte different and no NPC uses a backpack weapon.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "npc_pack_table.h"
#include "online_authority.h"
#include "stores.h"
#include <cstring>

namespace crew {
namespace {
struct Signature { std::size_t rva; const unsigned char* bytes; std::size_t size; };
#include "npc_backpack_signatures.inc"

// --- The soldier ---
constexpr std::size_t kSubs=0x1A00,kSubCount=0x1A10,kSubStride=0x20;   // the sub weapon records (vector at +0x19F8)
constexpr std::size_t kSubIndex=0x0;                                   // a record: its weapon's index into +0x1950
constexpr std::size_t kWeaponList=0x1950,kWeaponListCount=0x1960;
constexpr std::size_t kSets=0x1970,kSetCount=0x1980;                   // the WeaponSets (stride 0x150): set 0 first
constexpr std::size_t kSetSub=0x140,kSetWeapon=0x40,kSetMachine=0x110; // its sub weapon in use, its weapon, its state machine
constexpr std::size_t kMainBusy=0xEC4;                                 // the set's weapon: cleared as a use begins (u16)
constexpr std::size_t kSubChain=0x1A20;                                // set: a new use may cut the one going (cleared on a use)
constexpr std::size_t kSubLock=0x1A28,kSubLockCount=0x1A30;            // something holds the backpack (with a count >= 1)
constexpr std::size_t kStateBits=0x5D0;constexpr unsigned char kStateNoPack=4;
constexpr std::size_t kHumanState=0x39C;constexpr std::uint32_t kStateFree=~2u;
constexpr std::size_t kControlMask=0x158C;
constexpr std::uint32_t MaskOf(int slot) noexcept { return slot==0 ? 0x2000u : 0x4000u; }   // 0x5A1DEE..0x5A1DF6
constexpr std::size_t kNet=0x120,kNetFlags=0x128;
constexpr int kSlotSend=14;                                            // NetworkObject +0x70
// --- A weapon ---
constexpr std::size_t kReloadType=0x690,kReloadPress=0x13B;
// --- The code it calls ---
constexpr unsigned kMachineGo=0x5A0A60,kSubState=0x5A28A0,kDynamicCast=0x12DA7AA;
constexpr unsigned kTypeWeaponBase=0x2045768,kTypeWeaponSub=0x20476E8;   // type descriptors (names at +0x10)
constexpr unsigned kConstruct=0x79A460,kDestroy=0x760180,kWriteSmall=0x12B5790;
constexpr std::int8_t kMessageSub=0x10;
using CastFn=void*(__cdecl*)(void*,long,void*,void*,int);
using GoFn=void(__fastcall*)(void*,void*,std::uint64_t);
using SendFn=bool(__fastcall*)(void*,void*,std::uint64_t);
template<class F> F Fn(unsigned rva) noexcept { return reinterpret_cast<F>(image+rva); }
struct Stream {
    alignas(16) unsigned char bytes[0x600]{};
    Stream() { Fn<void*(__fastcall*)(void*,int)>(kConstruct)(bytes,0x40); }
    ~Stream() { Fn<void(__fastcall*)(void*)>(kDestroy)(bytes); }
};
bool ready=false;

unsigned char* Set0(const unsigned char* h) noexcept {
    if(!At<std::uint64_t>(h,kSetCount))return nullptr;
    unsigned char* set=At<unsigned char*>(h,kSets);
    return Readable(set,kSetSub+8) ? set : nullptr;
}
// Sub slot `slot`'s weapon as the game reads it (0x5A1E5A..: the record's index into the weapon list), nullptr none.
unsigned char* SlotWeapon(const unsigned char* h,int slot) noexcept {
    const std::uint64_t count=At<std::uint64_t>(h,kSubCount);
    if(slot<0 || static_cast<std::uint64_t>(slot)>=count || count>8)return nullptr;
    const auto records=At<const unsigned char*>(h,kSubs);
    if(!Readable(records+slot*kSubStride,kSubStride))return nullptr;
    const std::int32_t index=At<std::int32_t>(records+slot*kSubStride,kSubIndex);
    const std::uint64_t weapons=At<std::uint64_t>(h,kWeaponListCount);
    if(index<0 || static_cast<std::uint64_t>(index)>=weapons || weapons>32)return nullptr;
    const auto list=At<unsigned char* const*>(h,kWeaponList);
    return Readable(list+index,8) ? list[index] : nullptr;
}
// The weapon as a Weapon_Sub (the game's own cast, 0x5A1EAC), nullptr when it is another kind (an accessory).
unsigned char* AsSub(unsigned char* w) noexcept {
    if(!w || !Readable(w,8))return nullptr;
    return static_cast<unsigned char*>(Fn<CastFn>(kDynamicCast)(w,0,image+kTypeWeaponBase,image+kTypeWeaponSub,0));
}
// Message 0x10 with `slot` (0x5A2020..0x5A207A): the stream is an object to unwind, so not in a function with __try.
void SendUse(unsigned char* h,int slot) {
    Stream stream;
    const auto write=Fn<bool(__fastcall*)(void*,std::int8_t)>(kWriteSmall);
    if(!write(stream.bytes,kMessageSub) || !write(stream.bytes,static_cast<std::int8_t>(slot)))return;
    void* net=h+kNet;
    reinterpret_cast<SendFn>((*reinterpret_cast<void***>(net))[kSlotSend])(net,stream.bytes,0);
}
// The table's row of weapon `w` (its SGO file), nullptr when it is none of npc_pack_table.h's.
const PackWeapon* RowOf(const unsigned char* w) noexcept {
    std::size_t n=0;
    const wchar_t* file=WeaponFile(w,&n);
    if(!file || n<=4 || _wcsnicmp(file+n-4,L".SGO",4))return nullptr;
    return PackNamed(file,file+n-4);
}
struct UseState { unsigned char* weapon; unsigned char* sub; unsigned char* set; };
// Shared by the read-only readiness check and the actual use. The AI must not take the main weapon's look / trigger
// for a backpack which the native state, slot mask or lock currently forbids.
bool PrepareUse(unsigned char* h,int slot,UseState* out) noexcept {
    if(!ready || !h || IsAnyPlayer(h))return false;
    unsigned char* weapon=SlotWeapon(h,slot);
    unsigned char* sub=AsSub(weapon);
    unsigned char* set=Set0(h);
    if(!sub || !set || !RowOf(weapon))return false;
    // The input's gates (0x5A1DD3, 0x5A1EBD, 0x5A1F75), one stricter: a use going on is never cut.
    if(At<const void*>(set,kSetSub))return false;
    if(h[kStateBits]&kStateNoPack)return false;
    if(At<std::uint32_t>(h,kHumanState)&kStateFree)return false;
    if(At<const void*>(h,kSubLock) && At<std::int32_t>(h,kSubLockCount)>=1)return false;
    if(!(At<std::uint32_t>(h,kControlMask)&MaskOf(slot)))return false;
    if(At<std::uint32_t>(weapon,kWeaponAmmo)==0)return false;
    *out=UseState{weapon,sub,set};
    return true;
}
}  // namespace

bool InstallNpcBackpack() noexcept {
    ready=false;
    for(const auto& s:kSignatures) {
        bool same=false;
        __try { same=Matches(s.rva,s.bytes,s.size); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if(!same){Log("NPCAI backpack off: EDF+0x%zX is not the code read (%zu bytes): no NPC uses a backpack weapon",s.rva,s.size);return false;}
    }
    ready=true;
    Log("NPCAI backpack on (sub weapon use 0x5A28A0, message 0x10)");
    return true;
}
bool NpcBackpackReady() noexcept { return ready; }

int NpcPackSlots(const unsigned char* h,NpcPackSlot* out,int most) noexcept {
    if(!ready || !h || !out)return 0;
    int n=0;
    __try {
        const std::uint64_t count=At<std::uint64_t>(h,kSubCount);
        for(int slot=0;slot<static_cast<int>(count) && slot<2 && n<most;++slot) {
            unsigned char* w=SlotWeapon(h,slot);   // the list's WeaponBase: its fields read as the input reads them
            const PackWeapon* row=AsSub(w) ? RowOf(w) : nullptr;
            if(!row || !Readable(w,kWeaponAmmo+4))continue;
            out[n++]=NpcPackSlot{w,row,slot,At<std::int32_t>(At<const unsigned char*>(h,kSubs)+slot*kSubStride,kSubIndex)};
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
    return n;
}

bool NpcPackIndex(const unsigned char* h,int index) noexcept {
    __try {
        const std::uint64_t count=At<std::uint64_t>(h,kSubCount);
        if(!count || count>8)return false;
        const auto records=At<const unsigned char*>(h,kSubs);
        if(!Readable(records,count*kSubStride))return false;
        for(std::uint64_t i=0;i<count;++i)if(At<std::int32_t>(records+i*kSubStride,kSubIndex)==index)return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return false;
}

bool NpcPackBusy(const unsigned char* h) noexcept {
    __try {
        const unsigned char* set=Set0(h);
        return set && At<const void*>(set,kSetSub)!=nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER){return true;}
}

bool NpcPackCanUse(unsigned char* h,int slot) noexcept {
    __try { UseState use{};return PrepareUse(h,slot,&use); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool NpcPackUse(unsigned char* h,int slot) noexcept {
    bool begun=false;
    __try {
        UseState use{};
        if(!PrepareUse(h,slot,&use))return false;
        unsigned char* set=use.set;
        // The use (0x5A1FC1..0x5A1FF8, 0x59E7A8..0x59E7DE).
        const auto held=At<unsigned char* const*>(set,kSetWeapon);
        if(held && Readable(held,8) && *held && Readable(*held,kMainBusy+2))Put<std::uint16_t>(*held,kMainBusy,0);
        Put<void*>(set,kSetSub,use.sub);
        Fn<GoFn>(kMachineGo)(set+kSetMachine,image+kSubState,0);
        h[kSubChain]=0;
        begun=true;
        // Every other machine's copy (0x5A2013..0x5A207A): this soldier registered and run here.
        if(InSession() && (At<std::uint16_t>(h,kNetFlags)&(online::kNetRemote|online::kNetLocal))==online::kNetLocal)SendUse(h,slot);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("NPCAI backpack: soldier %p slot %d faulted (%s): backpack use off",h,slot,begun ? "after the use began" : "before it");
        ready=false;
        return false;
    }
    return begun;
}

bool NpcPackReload(unsigned char* weapon) noexcept {
    if(!ready || !weapon)return false;
    __try {
        unsigned char* sub=AsSub(weapon);   // rsi at 0x5A208B: the cast
        if(!sub || !Readable(sub,kReloadType+4) || !At<std::int32_t>(sub,kReloadType))return false;
        sub[kReloadPress]=1;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
