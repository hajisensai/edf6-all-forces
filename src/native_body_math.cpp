// Two native physics-table patches for the stock EDF.dll:
//  1. Rigid-body table growth: the stock automatic +0x1000 resize becomes proportional (cur/8) growth.
//  2. Motion-property ID limit: ID 0xFFFF is the failure sentinel, so allocation is capped below it and
//     exhausted requests fall back to the built-in presets (pop guard + weak-wrapper renewal included).
#include "native_body_math.h"
#include "edf_layout.h"
#include "plugin_log.h"
#include <algorithm>
#include "edf/memory.h"
#include "edf/patch.h"
#include <atomic>
#include <cstring>

namespace crew {
namespace {
bool NativeCodeMatches(unsigned begin,unsigned end,std::uint64_t expected) noexcept {
    const auto* const code=image+begin;
    const unsigned bytes=end-begin;
    if(!edf::Readable(code,bytes))return false;
    std::uint64_t fingerprint=14695981039346656037ull;
    for(unsigned i=0;i<bytes;++i)fingerprint=(fingerprint^code[i])*1099511628211ull;
    return fingerprint==expected;
}

constexpr unsigned kMotionEntrySize=0x70;
constexpr int kMotionSearchLimit=0xFFFF;
constexpr unsigned kMotionAdd=0xE15190,kMotionReserve=0xE15C40;
constexpr int kMotionSlots=kMotionSearchLimit;   // ID 0xFFFF is the failure sentinel.
constexpr unsigned kMotionPop=0xE152B5;
constexpr unsigned char kMotionPopStock[]{
    0x81,0xFF,0xFF,0xFF,0xFF,0,0x74,0x3B,0x0F,0x1F,0,0x48,0x8B,0x4B,0x40,0x44,0x8B,0xCF,
    0x49,0x6B,0xD1,0x70,0x8B,0x0C,0x0A,0x8D,0x90,0,0,0,1,0x33,0xC8,0x81,0xE1,0xFF,0xFF,0xFF,0,
    0x33,0xCA,0xF0,0x0F,0xB1,0x4B,0x5C,0x74,0x3F,0x8B,0x43,0x5C,0x8B,0xF8,0x81,0xE7,0xFF,0xFF,0xFF,0,
    0x81,0xFF,0xFF,0xFF,0xFF,0,0x75,0xC8,
};
constexpr unsigned kMotionRetryCompare=0xE152F0-kMotionPop;
constexpr unsigned kMotionGrowthCall=0x11AE93C;
constexpr unsigned kMotionObjectCall=0x11ACF89;
constexpr unsigned kMotionWeakStart=0x11AD047,kMotionWeakExpire=0x11AD060,kMotionWeakCreate=0x11AD080;
constexpr unsigned char kMotionWeakStock[]{
    0x74,0x2B,0x41,0x8B,0x40,0x08,0x85,0xC0,0x74,0x23,0x8D,0x50,1,0xF0,0x41,0x0F,0xB1,0x50,8,
    0x74,0x10,0x85,0xC0,0x75,0xF1,0x49,0x89,0x36,0x4D,0x89,0x4E,8,0xE9,0x71,1,0,0,
};
constexpr unsigned char kMotionWeakRenew[]{
    0x74,0x17,0x41,0x8B,0x40,0x08,0x85,0xC0,0x74,0x0F,0x8D,0x50,1,0xF0,0x41,0x0F,0xB1,0x50,8,
    0x74,0x10,0x85,0xC0,0x75,0xF1,
    0x83,0xBC,0x24,0x98,0,0,0,5, // cmp dword ptr [rsp+98h],5
    0x72,0x16,0xEB,0x08,         // jb create wrapper; jmp stock empty result
};
static_assert(sizeof(kMotionWeakStock)==sizeof(kMotionWeakRenew));
static_assert(kMotionWeakStart+2+kMotionWeakRenew[1]==kMotionWeakExpire);
static_assert(kMotionWeakExpire+10+kMotionWeakRenew[34]==kMotionWeakCreate);

constexpr unsigned kMotionRelease=0xE15670,kMotionReleaseCall=0xDA90CB;
using AddMotionFn=std::uint16_t*(__fastcall*)(unsigned char*,std::uint16_t*,const unsigned char*);
using ReserveMotionFn=int*(__fastcall*)(unsigned char*,int*,int);
using ReleaseMotionFn=void(__fastcall*)(unsigned char*,std::uint16_t);

int* __fastcall CapMotionGrowth(unsigned char* library,int* out,int wanted) {
    if(edf::At<int>(library,0x48)>=kMotionSlots) {
        *out=static_cast<int>(0x80040200u);   // The native reserve function's failure value.
        return out;
    }
    return reinterpret_cast<ReserveMotionFn>(image+kMotionReserve)(library,out,wanted<kMotionSlots ? wanted : kMotionSlots);
}

std::uint16_t* __fastcall AddObjectMotionProperties(unsigned char* library,std::uint16_t* out,const unsigned char* key) {
    auto* const result=reinterpret_cast<AddMotionFn>(image+kMotionAdd)(library,out,key);
    if(*out!=kMotionSlots || edf::At<int>(library,0x48)<kMotionSlots)return result;

    static std::atomic<bool> reported{false};
    if(!reported.load(std::memory_order_relaxed) && !reported.exchange(true,std::memory_order_relaxed))
        Log("PHYSICS motion property IDs exhausted: using built-in presets for new objects");

    // These constructors consume the ID immediately. Only the built-in slots outlive both
    // xgs-owned bodies and native controllers that do not retain an xgs property handle.
    const auto* row=edf::At<const unsigned char*>(library,0x40);
    for(std::uint16_t i=0;i<5;++i,row+=kMotionEntrySize) {
        if(!std::memcmp(row+4,key+4,kMotionEntrySize-4)) {
            *out=i;
            return result;
        }
    }
    // The native stationary, dynamic and zero-gravity presets remain valid past exhaustion.
    *out=edf::At<float>(key,0x10)==0.0f && edf::At<float>(key,0x14)==0.0f ? 0 :
         edf::At<float>(key,8)==0.0f ? 2 : 1;
    return result;
}

void __fastcall ReleaseMotionProperties(unsigned char* library,std::uint16_t id) {
    // Native controllers can hold a preset after exhaustion, but unlike xgs they use the raw release API.
    if((id<5 || id==kMotionSlots) && edf::At<int>(library,0x48)>=kMotionSlots)return;
    reinterpret_cast<ReleaseMotionFn>(image+kMotionRelease)(library,id);
}

bool InstallMotionIdLimit() noexcept {
    enum class Target { add,grow,release };
    struct Call {
        unsigned site;
        Target target;
        unsigned char stock[5];
        unsigned char code[5]{0xE8,0,0,0,0};
    };
    Call calls[]{
        {kMotionObjectCall,Target::add,{0xE8,0x02,0x82,0xC6,0xFF}},
        {0xDE39E2,Target::add,{0xE8,0xA9,0x17,0x03,0}},
        {0xE167E6,Target::add,{0xE8,0xA5,0xE9,0xFF,0xFF}},
        {kMotionGrowthCall,Target::grow,{0xE8,0xFF,0x72,0xC6,0xFF}},
        {kMotionReleaseCall,Target::release,{0xE8,0xA0,0xC5,0x06,0}},
    };
    constexpr unsigned last=static_cast<unsigned>(sizeof(calls)/sizeof(calls[0]))-1;
    constexpr unsigned char savedId[]{0x44,0x0F,0xB7,0x8C,0x24,0x80,0,0,0,0x44,0x89,0x8C,0x24,0x98,0,0,0};
    if(!edf::Matches(image,kMotionPop,kMotionPopStock,sizeof(kMotionPopStock)) ||
       !edf::Matches(image,kMotionObjectCall+5,savedId,sizeof(savedId)) ||
       !edf::Matches(image,kMotionWeakStart,kMotionWeakStock,sizeof(kMotionWeakStock)))return false;
    for(const auto& call:calls)
        if(!edf::Matches(image,call.site,call.stock,sizeof(call.stock)))return false;
    void* const thunks[]{
        edf::AllocateNearThunk(image+kMotionObjectCall,reinterpret_cast<void*>(&AddObjectMotionProperties)),
        edf::AllocateNearThunk(image+kMotionGrowthCall,reinterpret_cast<void*>(&CapMotionGrowth)),
        edf::AllocateNearThunk(image+kMotionReleaseCall,reinterpret_cast<void*>(&ReleaseMotionProperties)),
    };
    if(!thunks[0] || !thunks[1] || !thunks[2]) {
        for(auto* thunk:thunks)if(thunk)VirtualFree(thunk,0,MEM_RELEASE);
        return false;
    }

    unsigned installed=0;
    for(;installed<=last;++installed) {
        auto& call=calls[installed];
        const auto delta=reinterpret_cast<std::intptr_t>(thunks[static_cast<unsigned>(call.target)])-
                         reinterpret_cast<std::intptr_t>(image+call.site+sizeof(call.code));
        if(delta<INT32_MIN || delta>INT32_MAX)break;
        const auto offset=static_cast<std::int32_t>(delta);
        std::memcpy(call.code+1,&offset,sizeof(offset));
        if(!edf::PatchCode(image+call.site,call.stock,call.code,sizeof(call.code)))break;
    }
    unsigned char guarded[sizeof(kMotionPopStock)];
    std::memcpy(guarded,kMotionPopStock,sizeof(guarded));
    guarded[4]=guarded[kMotionRetryCompare+4]=0;
    guarded[6]=0x73;                     // jae: the sentinel and higher indices cannot be allocated.
    guarded[kMotionRetryCompare+6]=0x72; // jb: recheck the same bound after a failed compare/exchange.
    // Preset wrappers leave their weak map entry behind when the last owner is destroyed.
    // Renew expired preset wrappers in the stock creation path before allowing those IDs as fallbacks.
    const bool weakPatched=installed>last && edf::PatchCode(image+kMotionWeakStart,kMotionWeakStock,
                                                           kMotionWeakRenew,sizeof(kMotionWeakStock));
    if(weakPatched && edf::PatchCode(image+kMotionPop,kMotionPopStock,guarded,sizeof(guarded)))return true;

    bool restored=!weakPatched || edf::PatchCode(image+kMotionWeakStart,kMotionWeakRenew,kMotionWeakStock,
                                               sizeof(kMotionWeakStock));
    while(installed) {
        const auto& call=calls[--installed];
        restored=edf::PatchCode(image+call.site,call.code,call.stock,sizeof(call.stock)) && restored;
    }
    if(restored)for(auto* thunk:thunks)VirtualFree(thunk,0,MEM_RELEASE);
    return false;
}



constexpr unsigned kBodyGrowthListener=0x11AE980,kBodyGrowthCall=0x11AE99F;
constexpr unsigned kBodyResize=0xE42730,kBodyArrayResize=0xE42FE0;
constexpr unsigned kBodyGrowthStep=0x1000,kBodyGrowthMaxStep=0x8000,kBodyRecordBytes=0xB0;
constexpr unsigned kBodyCapacityLimit=(INT32_MAX-0x20u)/kBodyRecordBytes;
constexpr unsigned kBodyCommitBytesPerSlot=512;
using BodyResizeFn=void*(__fastcall*)(void*,void*,void*,int);

// Only the stock automatic-growth request is eligible. Explicit reserves and unusual sizes retain their arguments.
int BodyGrowthCapacity(int current,int requested,std::uint64_t availableCommit) noexcept {
    if(current<=0 || static_cast<std::int64_t>(current)+kBodyGrowthStep!=requested)return requested;
    const auto count=static_cast<unsigned>(current);
    const auto proportional=(count/8+kBodyGrowthStep-1)&~(kBodyGrowthStep-1);
    if(proportional<=kBodyGrowthStep)return requested;
    const auto step=(std::min)(proportional,kBodyGrowthMaxStep);
    const auto target=static_cast<std::uint64_t>(count)+step;
    // The native body-buffer byte count is signed. Also allow room for side tables during relocation.
    if(target>kBodyCapacityLimit || target*kBodyCommitBytesPerSlot>availableCommit)return requested;
    return static_cast<int>(target);
}

void* __fastcall GrowBodyCapacity(void* manager,void* result,void* external,int requested) {
    if(!external) {
        const int current=edf::At<int>(manager,0x18);
        if(BodyGrowthCapacity(current,requested,UINT64_MAX)!=requested) {
            MEMORYSTATUSEX memory{};
            memory.dwLength=sizeof(memory);
            if(GlobalMemoryStatusEx(&memory))
                requested=BodyGrowthCapacity(current,requested,memory.ullAvailPageFile);
        }
    }
    return reinterpret_cast<BodyResizeFn>(image+kBodyResize)(manager,result,external,requested);
}

bool InstallBodyCapacityGrowth() noexcept {
    if(!NativeCodeMatches(kBodyGrowthListener,0x11AE9A9,0xA18F36A1E1457D94ull) ||
       !NativeCodeMatches(kBodyResize,0xE42782,0x93EA381687190B60ull) ||
       !NativeCodeMatches(0xE43190,0xE432AE,0xDFAC3BCDCEE283E0ull))return false;
    bool changed=false;
    edf::RedirectCall(image+kBodyGrowthCall,image+kBodyResize,reinterpret_cast<void*>(&GrowBodyCapacity),changed);
    return changed;
}
}

unsigned InstallNativeBodyMath() noexcept {
    if(!image)return 0;
    unsigned installed=0;
    installed+=InstallMotionIdLimit();
    installed+=InstallBodyCapacityGrowth();
    return installed;
}
}
