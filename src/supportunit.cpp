// Support units (SupportUnitBullet01: the Air Raider's and Fencer's life vendors, power posts, guard posts...) stick to
// allies as to anything else, after KittopiaCreator's SupportUnitCanBePlacedOnAllies patch (docs/supportunit-allies-re.md).
// The rounds' hit filter 0x232BCF drops a hit on an ally (the object's team +0x314 against the round's +0xA00 in the
// teams' table: 1) when the round's bullet part (+0x140) has +0xA27 or +0xA28 set: the round flies through allies. The
// SupportUnitBullet01 ctor 0x2AB680 runs with +0x140+0xA28 (the round's +0xB68) set, so a unit thrown at a teammate
// fell through to the ground under them. The hook clears that byte in the ctor, at 0x2ABB10 (mov [rdi+0xF0C],eax, right
// after the bullet part's set up): the round then hits allies and sticks where it hits, as it does on the ground.
// ini SupportUnitOnAllies, live (the code cave reads the plugin's byte for every unit thrown). Each machine runs the
// hit filter for its own copy of the round: in a room every player should set the same. All addresses are RVAs into
// EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include <atomic>
#include <cstdint>
#include <cstring>

namespace crew {
namespace {
constexpr std::size_t kCtorVtable=0x2AB6EB,kCtorStore=0x2ABB10,kHitFilter=0x232D02;
// lea rax,[SupportUnitBullet01's vtable 0x17A5B38] / mov [rdi],rax: the ctor is the support unit's.
const unsigned char kCtorVtableSig[]={0x48,0x8D,0x05,0x46,0xA4,0x4F,0x01,0x48,0x89,0x07};
// mov eax,[rdi+0xB60] / mov [rdi+0xF0C],eax: the hook takes the second (6 bytes, nothing jumps into it).
const unsigned char kCtorStoreSig[]={0x8B,0x87,0x60,0x0B,0x00,0x00,0x89,0x87,0x0C,0x0F,0x00,0x00};
constexpr std::size_t kStoreOffset=6,kStoreSize=6;
// cmp byte [rsi+0xA27],0 / jne / cmp byte [rsi+0xA28],0 / je: the filter still reads the byte the hook clears.
const unsigned char kHitFilterSig[]={0x80,0xBE,0x27,0x0A,0x00,0x00,0x00,0x75,0x09,0x80,0xBE,0x28,0x0A,0x00,0x00,0x00,0x74,0x4F};

std::atomic<unsigned char> onAllies{1};   // ini SupportUnitOnAllies: the code cave reads it as a plain byte
static_assert(sizeof(std::atomic<unsigned char>)==1 && std::atomic<unsigned char>::is_always_lock_free,"a plain byte");
bool hooked=false;
}  // namespace

void SetSupportUnitOnAllies(bool on) noexcept { onAllies.store(on ? 1 : 0,std::memory_order_relaxed); }

bool InstallSupportUnitOnAllies() noexcept {
    if(hooked)return true;
    if(!Matches(kCtorVtable,kCtorVtableSig,sizeof(kCtorVtableSig)) || !Matches(kCtorStore-kStoreOffset,kCtorStoreSig,sizeof(kCtorStoreSig)) ||
       !Matches(kHitFilter,kHitFilterSig,sizeof(kHitFilterSig))) {
        Log("SUPPORTUNIT: unexpected EDF.dll code: support units pass through allies (stock)");
        return false;
    }
    // The cave: the stock store, then, the ini on, clear the round's pass-through-allies byte; back after the store.
    // The flags it leaves are dead (the next instructions mov, then cmp).
    //   mov [rdi+0xF0C],eax / push rax / mov rax,&onAllies / cmp byte [rax],0 / pop rax / je +7 /
    //   mov byte [rdi+0xB68],0 / jmp back
    unsigned char cave[]={0x89,0x87,0x0C,0x0F,0x00,0x00, 0x50, 0x48,0xB8,0,0,0,0,0,0,0,0, 0x80,0x38,0x00, 0x58, 0x74,0x07,
                          0xC6,0x87,0x68,0x0B,0x00,0x00,0x00, 0xE9,0,0,0,0};
    constexpr std::size_t kFlagAt=9,kBackAt=31;
    const auto flag=reinterpret_cast<std::uintptr_t>(&onAllies);
    std::memcpy(cave+kFlagAt,&flag,8);
    // The cave's jmp back is a rel32 from where it lands, so it is written once the page is known: the page is
    // made with a placeholder, then the real cave (with its rel32) copied in under a write protection.
    void* const page=edf::AllocateNearCode(image+kCtorStore,cave,sizeof(cave));
    if(!page){Log("SUPPORTUNIT: no code page within reach: support units pass through allies (stock)");return false;}
    const auto at=static_cast<unsigned char*>(page);
    const std::int64_t back=reinterpret_cast<std::intptr_t>(image+kCtorStore+kStoreSize)-reinterpret_cast<std::intptr_t>(at+kBackAt+4);
    const std::int64_t to=reinterpret_cast<std::intptr_t>(at)-reinterpret_cast<std::intptr_t>(image+kCtorStore+5);
    if(back<INT32_MIN || back>INT32_MAX || to<INT32_MIN || to>INT32_MAX){VirtualFree(page,0,MEM_RELEASE);return false;}
    const auto back32=static_cast<std::int32_t>(back);
    std::memcpy(cave+kBackAt,&back32,4);
    if(!edf::PatchCode(at,at,cave,sizeof(cave))){VirtualFree(page,0,MEM_RELEASE);return false;}
    unsigned char patch[kStoreSize]={0xE9,0,0,0,0,0x90};
    const auto to32=static_cast<std::int32_t>(to);
    std::memcpy(patch+1,&to32,4);
    if(!edf::PatchCode(image+kCtorStore,kCtorStoreSig+kStoreOffset,patch,sizeof(patch))){VirtualFree(page,0,MEM_RELEASE);return false;}
    hooked=true;
    Log("HOOK supportunit on allies (ini SupportUnitOnAllies=%d now)",onAllies.load(std::memory_order_relaxed));
    return true;
}
}  // namespace crew
