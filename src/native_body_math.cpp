// Native physics patches for the stock EDF.dll:
//  1. Rigid-body table growth: the stock automatic +0x1000 resize becomes proportional (cur/8) growth.
//  2. Motion-property ID limit: ID 0xFFFF is the failure sentinel, so allocation is capped below it and
//     exhausted requests fall back to the built-in presets (pop guard + weak-wrapper renewal included).
//  3. Static body pose commits skip the quaternion that only the dynamic branch reads.
//  4. Body notifications return early when both listener lists are empty.
//  5. Collision proxy pose publication prefetches the body ID and world reference.
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

constexpr unsigned kBodyQuaternionCall=0xE51651,kNativeBodyQuaternion=0x98BF30;
constexpr unsigned char kBodyQuaternionStock[]{0xE8,0xDA,0xA8,0xB3,0xFF};

bool InstallStaticBodyQuaternionSkip() noexcept {
    // The changed-pose path leaves the hknpBody in rdi until it notifies the world.
    constexpr unsigned char setter[]{
        0x48,0x8D,0x4C,0x24,0x30,0x48,0x8B,0xD6,0x4C,0x89,0xBC,0x24,0xA8,0,0,0,
        0xE8,0xDA,0xA8,0xB3,0xFF,0x4D,0x8B,0xC6,0x48,0x8B,0xD6,0x48,0x8B,0xCF,
        0xE8,0x5C,0x80,0xF9,0xFF,0x8B,0x84,0x24,0xB0,0,0,0,0x4C,0x8D,0x44,0x24,0x30,
        0x45,0x33,0xC9,0x89,0x44,0x24,0x20,0x48,0x8B,0xD7,0x49,0x8D,0x4D,0xE8,0xE8,0xDD,0xEF,0xF5,0xFF,
    };
    constexpr unsigned char motionId[]{0x8B,0xBA,0x80,0,0,0};
    constexpr unsigned char motionBranch[]{0x85,0xFF,0x0F,0x84,0x2E,0x07,0,0};
    constexpr unsigned char staticUpdate[]{0x49,0x8B,0xCE,0xE8,0x73,0x88,0x03,0};
    constexpr unsigned char dynamicQuaternion[]{0x4D,0x85,0xC9,0x0F,0x84,0x64,1,0,0,0x41,0x0F,0x10,0x9E,
        0xA0,0,0,0,0x41,0x0F,0x10,0x29};
    constexpr unsigned char quaternionSig[]{0x40,0x53,0x48,0x83,0xEC,0x10,0xF3,0x0F,0x10,0x1A,0x0F,0x57,0xC0};
    if(!edf::Matches(image,0xE51641,setter,sizeof(setter)) ||
       !edf::Matches(image,0xDB06CB,motionId,sizeof(motionId)) ||
       !edf::Matches(image,0xDB07DF,motionBranch,sizeof(motionBranch)) ||
       !edf::Matches(image,0xDB0F15,staticUpdate,sizeof(staticUpdate)) ||
       !edf::Matches(image,0xDB0911,dynamicQuaternion,sizeof(dynamicQuaternion)) ||
       !edf::Matches(image,kNativeBodyQuaternion,quaternionSig,sizeof(quaternionSig)))return false;

    // Motion ID zero goes directly to the static update without reading the supplied quaternion.
    // Use the home space for MXCSR and keep the native math when FP exceptions are unmasked.
    unsigned char code[]{
        0x83,0xBF,0x80,0,0,0,0,          // cmp dword ptr [rdi+80h],0
        0x75,0x16,                     // jne stock
        0x0F,0xAE,0x5C,0x24,0x08,       // stmxcsr [rsp+8]
        0x8B,0x44,0x24,0x08,            // mov eax,[rsp+8]
        0x25,0x80,0x1F,0,0,            // and eax,1f80h
        0x3D,0x80,0x1F,0,0,            // cmp eax,1f80h
        0x75,0x01,0xC3,                // jne stock; ret
        0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0,
    };
    auto* const site=image+kBodyQuaternionCall;
    auto* const original=image+kNativeBodyQuaternion;
    std::memcpy(code+33,&original,sizeof(original));
    auto* const thunk=edf::AllocateNearCode(site,code,sizeof(code));
    if(!thunk)return false;
    const auto delta=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(site+5);
    if(delta>=INT32_MIN && delta<=INT32_MAX) {
        unsigned char call[]{0xE8,0,0,0,0};
        const auto rel32=static_cast<std::int32_t>(delta);
        std::memcpy(call+1,&rel32,sizeof(rel32));
        if(edf::PatchCode(site,kBodyQuaternionStock,call,sizeof(call)))return true;
    }
    VirtualFree(thunk,0,MEM_RELEASE);
    return false;
}

constexpr unsigned kBodySignalStart=0xE51683,kBodySignalDone=0xE51793;
constexpr unsigned char kBodySignalStock[]{
    0x49,0x8B,0x85,0x60,0x07,0,0,0x4D,0x8D,0xB5,0x60,0x07,0,0,
    0x48,0x83,0xE0,0xFD,0x48,0x83,0xC8,0x01,0x48,0x8B,0xC8,
    0x49,0x89,0x85,0x60,0x07,0,0,0x48,0x83,0xE1,0xFC,0x74,0x50,0x0F,0x1F,0x80,0,0,0,0,
};
constexpr unsigned char kBodySignalEmptyGate[]{
    0x4D,0x8D,0xB5,0x60,0x07,0,0,  // lea r14,[r13+760h]
    0x49,0x8B,0x06,                // mov rax,[r14]
    0x48,0x85,0xC0,0x75,0x0E,      // test rax,rax; jne dispatch
    0x49,0x83,0xBD,0xB8,0x07,0,0,0,// cmp qword ptr [r13+7b8h],0
    0x0F,0x84,0xF3,0,0,0,          // je 0xE51793, the shared epilogue
    0x24,0xFD,0x0C,0x01,           // dispatch: clear bit 1 and set bit 0
    0x48,0x8B,0xC8,0x49,0x89,0x06, // mov rcx,rax; mov [r14],rax
    0x48,0x83,0xE1,0xFC,0x74,0x49, // and rcx,-4; je 0xE516F9
};
static_assert(sizeof(kBodySignalStock)==sizeof(kBodySignalEmptyGate));
static_assert(kBodySignalStart+sizeof(kBodySignalStock)==0xE516B0);
static_assert(kBodySignalStart+29+kBodySignalEmptyGate[25]==kBodySignalDone);

bool InstallEmptyBodySignalGate() noexcept {
    constexpr unsigned char second[]{
        0x49,0x8B,0x85,0xB8,0x07,0,0,0x48,0x83,0xE0,0xFD,0x48,0x83,0xC8,1,0x48,0x8B,0xC8,
        0x49,0x89,0x85,0xB8,0x07,0,0,0x48,0x83,0xE1,0xFC,0x74,0x4E,
    };
    constexpr unsigned char epilogue[]{
        0x0F,0x28,0x74,0x24,0x50,0x0F,0x28,0x7C,0x24,0x40,0x48,0x83,0xC4,0x60,0x41,0x5E,0x41,0x5D,
        0x5F,0x5E,0x5B,0xC3,
    };
    if(!edf::Matches(image,0xE5170B,second,sizeof(second)) ||
       !edf::Matches(image,kBodySignalDone,epilogue,sizeof(epilogue)))return false;
    // The empty heads have no dispatch state. A listener or any flag keeps both original traversals.
    // The shared epilogue is also used by the stock tolerance check. r12 and r15 have only been saved.
    return edf::PatchCode(image+kBodySignalStart,kBodySignalStock,kBodySignalEmptyGate,sizeof(kBodySignalStock));
}

constexpr unsigned kPoseSetter=0x109B50,kPoseSetterCall=0x6EDC0C;
constexpr unsigned char kPoseSetterStock[]{
    0x41,0x0F,0x28,0x00,0x0F,0x29,0x42,0x60,0x41,0x0F,0x28,0x48,0x10,0x0F,0x29,0x4A,
    0x70,0x41,0x0F,0x28,0x40,0x20,0x0F,0x29,0x82,0x80,0x00,0x00,0x00,0x41,0x0F,0x28,
    0x48,0x30,0x0F,0x29,0x8A,0x90,0x00,0x00,0x00,0xC3,
};
constexpr unsigned char kPoseSetterSite[]{
    0x48,0x8B,0x93,0xB0,0x00,0x00,0x00,0x48,0x8D,0x8E,0x10,0x01,0x00,0x00,0x4C,0x8D,
    0x44,0x24,0x60,0xE8,0x3F,0xBF,0xA1,0xFF,
};
constexpr unsigned char kPoseCompoundSubmit[]{
    0x48,0x8D,0x8E,0x10,0x01,0x00,0x00,0xE8,0x28,0xBF,0xA1,0xFF,0x48,0x8B,0x8E,0xC0,
    0x00,0x00,0x00,0x48,0x8B,0x49,0x58,0xE8,0x48,0x28,0x6C,0x00,
};

bool InstallEarlyProxyBodyPrefetch() noexcept {
    constexpr unsigned callOffset=kPoseSetterCall-0x6EDBF9;
    if(!edf::Matches(image,kPoseSetter,kPoseSetterStock,sizeof(kPoseSetterStock)) ||
       !edf::Matches(image,kPoseSetterCall-callOffset,kPoseSetterSite,sizeof(kPoseSetterSite)) ||
       !edf::Matches(image,0x6EDC4C,kPoseCompoundSubmit,sizeof(kPoseCompoundSubmit)))return false;
    // Warm the body ID and world reference during pose publication, before the later compound walk.
    constexpr unsigned char prefetch[]{
        0x48,0x8B,0x42,0x10,
        0x0F,0x18,0x88,0xF0,0,0,0,
        0x0F,0x18,0x88,0,1,0,0,
    };
    unsigned char code[sizeof(prefetch)+sizeof(kPoseSetterStock)];
    std::memcpy(code,prefetch,sizeof(prefetch));
    std::memcpy(code+sizeof(prefetch),kPoseSetterStock,sizeof(kPoseSetterStock));
    auto* const site=image+kPoseSetterCall;
    auto* const thunk=edf::AllocateNearCode(site,code,sizeof(code));
    if(!thunk)return false;
    const auto delta=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(site+5);
    if(delta>=INT32_MIN && delta<=INT32_MAX) {
        unsigned char call[]{0xE8,0,0,0,0};
        const auto rel32=static_cast<std::int32_t>(delta);
        std::memcpy(call+1,&rel32,sizeof(rel32));
        if(edf::PatchCode(site,kPoseSetterSite+callOffset,call,sizeof(call)))return true;
    }
    VirtualFree(thunk,0,MEM_RELEASE);
    return false;
}
}

unsigned InstallNativeBodyMath() noexcept {
    if(!image)return 0;
    unsigned installed=0;
    installed+=InstallMotionIdLimit();
    installed+=InstallBodyCapacityGrowth();
    installed+=InstallStaticBodyQuaternionSkip();
    installed+=InstallEmptyBodySignalGate();
    installed+=InstallEarlyProxyBodyPrefetch();
    return installed;
}
}
