// Check the native physics patches against the original code on a private EDF.dll mapping.
#include "../src/native_body_math.cpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <intrin.h>
#include <memory>
#include <random>
#include <vector>

namespace crew { unsigned char* image=nullptr; void Log(const char*,...) noexcept {} }

namespace {
using namespace crew;

template<class Fn> Fn Redirected(unsigned site) noexcept {
    const auto* const instruction=image+site;
    return reinterpret_cast<Fn>(instruction+5+edf::At<std::int32_t>(instruction,1));
}

struct alignas(16) Matrix { float data[16]; };
struct alignas(16) Box { float data[8]; };
struct alignas(16) Proxy { unsigned char prefix[0x20]; Box source,output; Matrix matrix; };

template<class Call> double Time(Call call) {
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<500000;++i)call(i%64);
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}

template<class Before,class After> void Bench(const char* name,Before before,After after) {
    std::array<double,5> oldTimes{},newTimes{};
    for(std::size_t i=0;i<oldTimes.size();++i) {
        if(i%2) {
            newTimes[i]=Time(after);
            oldTimes[i]=Time(before);
        } else {
            oldTimes[i]=Time(before);
            newTimes[i]=Time(after);
        }
    }
    std::sort(oldTimes.begin(),oldTimes.end());
    std::sort(newTimes.begin(),newTimes.end());
    std::printf("500,000 %s: %.2f -> %.2f ms (median of five runs)\n",name,oldTimes[2],newTimes[2]);
}

bool ProxyPosePrefetchCases() {
    using Setter=void(__fastcall*)(void*,void*,const float*);
    const auto stock=reinterpret_cast<Setter>(image+kPoseSetter);
    const auto early=Redirected<Setter>(kPoseSetterCall);
    if(early==stock)return false;
    unsigned char body[0x110]{};
    alignas(16) Matrix matrix{};
    alignas(16) Proxy expected{},actual{};
    std::mt19937 rng(1264);
    const auto saved=_mm_getcsr();
    for(unsigned i=0;i<2048;++i) {
        for(unsigned j=0;j<16;++j)edf::Put(&matrix,j*4,static_cast<std::uint32_t>(rng()));
        std::memset(&expected,0xA7,sizeof(expected));
        edf::Put(&expected,0x10,i%2 ? body+0 : static_cast<unsigned char*>(nullptr));
        actual=expected;
        const unsigned mode=(i&2) ? 0xBFC0u : 0x1F80u;
        _mm_setcsr(mode);
        stock(nullptr,&expected,matrix.data);
        early(nullptr,&actual,matrix.data);
        if(std::memcmp(&actual,&expected,sizeof(actual))!=0 || _mm_getcsr()!=mode) {
            _mm_setcsr(saved);
            return false;
        }
    }
    _mm_setcsr(saved);
    return true;
}

struct BodySignalResult {
    std::uint64_t calls{}, removed{};
};
struct BodySignalListener {
    const void* const* methods;
    std::uintptr_t next;
    unsigned id;
    BodySignalListener* subscribeSecond;
    BodySignalResult* result;
};

void __fastcall TestBodySignalRemove(BodySignalListener* listener,unsigned remove) {
    auto& calls=listener->result->removed;
    calls=calls*131+listener->id*7+remove;
}

void __fastcall TestBodySignalNotify(BodySignalListener* listener,unsigned char* world,unsigned id) {
    const auto tag1=edf::At<std::uintptr_t>(world,0x778)&3;
    const auto tag2=edf::At<std::uintptr_t>(world,0x7D0)&3;
    auto& calls=listener->result->calls;
    calls=calls*131+id+listener->id*17+tag1*3+tag2*5;
    if(listener->subscribeSecond)edf::Put(world,0x7D0,listener->subscribeSecond);
}

bool BodySignalDispatch() {
    using Run=void(__fastcall*)(void*,unsigned,const void*);
    constexpr unsigned char caller[]{
        0x53,0x56,0x57,0x41,0x55,0x41,0x56,0x48,0x81,0xEC,0xB0,0,0,0,
        0x4C,0x8B,0xE9,0x8B,0xDA,0x4C,0x89,0xA4,0x24,0xA0,0,0,0,
        0x4C,0x89,0xBC,0x24,0xA8,0,0,0,0x41,0xFF,0xE0,
        0x48,0x81,0xC4,0xB0,0,0,0,0x41,0x5E,0x41,0x5D,0x5F,0x5E,0x5B,0xC3,
    };
    constexpr unsigned stockOffset=64,fastOffset=512,resumeOffset=38;
    constexpr unsigned codeSize=kBodySignalDone-kBodySignalStart;
    std::array<unsigned char,fastOffset+codeSize+5> executable{};
    std::memcpy(executable.data(),caller,sizeof(caller));
    for(const auto offset:{stockOffset,fastOffset}) {
        auto* const fn=executable.data()+offset;
        std::memcpy(fn,image+kBodySignalStart,codeSize);
        if(offset==stockOffset)std::memcpy(fn,kBodySignalStock,sizeof(kBodySignalStock));
        fn[codeSize]=0xE9;
        const auto rel=static_cast<std::int32_t>(resumeOffset)-static_cast<std::int32_t>(offset+codeSize+5);
        edf::Put(fn,codeSize+1,rel);
    }
    auto* const mem=static_cast<unsigned char*>(
        edf::AllocateNearCode(image+kBodySignalStart,executable.data(),executable.size()));
    if(!mem)return false;
    const auto run=reinterpret_cast<Run>(mem);
    const void* const methods[]{reinterpret_cast<const void*>(&TestBodySignalRemove),nullptr,nullptr,
                                reinterpret_cast<const void*>(&TestBodySignalNotify)};
    BodySignalResult seen{};
    BodySignalListener first{methods,0,1,nullptr,&seen},second{methods,0,2,nullptr,&seen},third{methods,0,3,nullptr,&seen};
    const auto addr=[](const auto* p) {return reinterpret_cast<std::uintptr_t>(p);};
    alignas(16) unsigned char world[0x7E0]{};
    const auto check=[&] {
        if(!edf::Matches(image,kBodySignalStart,kBodySignalEmptyGate,sizeof(kBodySignalEmptyGate)))return false;
        for(unsigned i=0;i<8;++i) {
            first.next=(i&1) ? addr(&second)|(i&2 ? 1u : 0u) : 0;
            first.subscribeSecond=(i&4) ? &third : nullptr;
            const auto head=i==0 ? 0 : i==2 ? 3 : addr(&first)|(i&3u);
            const auto tail=i==0 ? 0 : i==1 ? 2 : addr(&second)|(i&1u);
            // Include a second-list-only dispatch.
            const auto initialFirst=i==5 ? 0 : head;
            std::array<BodySignalResult,2> results{};
            std::array<std::uintptr_t,2> heads{},tails{};
            for(unsigned version=0;version<2;++version) {
                seen={};
                edf::Put(world,0x778,initialFirst);
                edf::Put(world,0x7D0,tail);
                run(world+0x18,0xF1230000u+i,mem+(version ? fastOffset : stockOffset));
                results[version]=seen;
                heads[version]=edf::At<std::uintptr_t>(world,0x778);
                tails[version]=edf::At<std::uintptr_t>(world,0x7D0);
            }
            if(results[0].calls!=results[1].calls || results[0].removed!=results[1].removed ||
               heads[0]!=heads[1] || tails[0]!=tails[1])return false;
            if(i==0 && (heads[1] || tails[1] || seen.calls || seen.removed))return false;
            if(i!=0 && !seen.calls)return false;
            if((i&3)==3 && !seen.removed)return false;
        }
        return true;
    };
    const bool valid=check();
    if(valid) {
        edf::Put(world,0x778,std::uintptr_t{0});
        edf::Put(world,0x7D0,std::uintptr_t{0});
        Bench("unsubscribed body notifications",
              [&](unsigned i){run(world+0x18,i,mem+stockOffset);},
              [&](unsigned i){run(world+0x18,i,mem+fastOffset);});
    }
    VirtualFree(mem,0,MEM_RELEASE);
    return valid;
}

bool BodyQuaternionUpdates() {
    using Quaternion=void(__fastcall*)(float*,const float*);
    using Run=void(__fastcall*)(float*,const float*,const void*,Quaternion);
    // The native setter has already loaded the body into the nonvolatile rdi.
    constexpr unsigned char caller[]{
        0x57,0x48,0x83,0xEC,0x20,0x49,0x8B,0xF8,0x41,0xFF,0xD1,
        0x48,0x83,0xC4,0x20,0x5F,0xC3,
    };
    const auto thunk=edf::AllocateNearCode(image+kBodyQuaternionCall,caller,sizeof(caller));
    if(!thunk)return false;
    const auto run=reinterpret_cast<Run>(thunk);
    const auto stock=reinterpret_cast<Quaternion>(image+kNativeBodyQuaternion);
    const auto fast=Redirected<Quaternion>(kBodyQuaternionCall);
    alignas(16) unsigned char body[0xB0]{};
    alignas(16) float expected[4],actual[4];
    std::array<Matrix,64> matrices{};
    for(unsigned i=0;i<matrices.size();++i) {
        const float angle=static_cast<float>(i)*0.09817477f;
        const float c=std::cos(angle),s=std::sin(angle);
        auto& m=matrices[i].data;
        m[0]=m[15]=1.0f;
        m[5]=m[10]=c;
        m[6]=s;
        m[9]=-s;
    }
    const auto saved=_mm_getcsr();
    const auto check=[&] {
        if(stock==fast)return false;
        for(const auto masks:{0x1F80u,0x1F00u}) {
            for(const unsigned motion:{0u,1u,0xFFFFFFFFu}) {
                edf::Put(body,0x80,motion);
                for(const auto& matrix:matrices) {
                    std::memset(actual,0xA5,sizeof(actual));
                    _mm_setcsr(masks);
                    if(masks==0x1F80 && !motion) {
                        std::memset(expected,0xA5,sizeof(expected));
                        run(actual,nullptr,body,fast);
                        if(_mm_getcsr()!=masks)return false;
                    } else {
                        stock(expected,matrix.data);
                        const auto flags=_mm_getcsr();
                        _mm_setcsr(masks);
                        run(actual,matrix.data,body,fast);
                        if(_mm_getcsr()!=flags)return false;
                    }
                    if(std::memcmp(actual,expected,sizeof(actual))!=0)return false;
                }
            }
        }
        return true;
    };
    const bool valid=check();
    _mm_setcsr(saved);
    if(valid) {
        edf::Put(body,0x80,0u);
        Bench("static body's unused quaternions",
              [&](unsigned i){run(expected,matrices[i].data,body,stock);},
              [&](unsigned i){run(actual,matrices[i].data,body,fast);});
    }
    VirtualFree(thunk,0,MEM_RELEASE);
    return valid;
}

void* __fastcall BodyTestAllocate(void*,std::size_t bytes) noexcept {
    return VirtualAlloc(nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
}
void __fastcall BodyTestRelease(void*,void* allocation,std::size_t) noexcept {
    VirtualFree(allocation,0,MEM_RELEASE);
}

bool BodyGrowthCases() {
    constexpr std::uint64_t enough=UINT64_MAX;
    if(BodyGrowthCapacity(0,4096,enough)!=4096 ||
       BodyGrowthCapacity(32768,36864,enough)!=36864 ||
       BodyGrowthCapacity(131072,135168,enough)!=147456 ||
       BodyGrowthCapacity(163840,167936,enough)!=184320 ||
       BodyGrowthCapacity(163840,167936,184320ull*kBodyCommitBytesPerSlot-1)!=167936 ||
       BodyGrowthCapacity(262144,266240,enough)!=294912 ||
       BodyGrowthCapacity(kBodyCapacityLimit-kBodyGrowthStep,kBodyCapacityLimit,enough)!=kBodyCapacityLimit ||
       BodyGrowthCapacity(163840,168000,enough)!=168000 ||
       Redirected<BodyResizeFn>(kBodyGrowthCall)==reinterpret_cast<BodyResizeFn>(image+kBodyResize))return false;

    constexpr unsigned allocatorSlot=0x1F826E0,memcpySlot=0x1756058;
    DWORD allocatorProtection=0,memcpyProtection=0;
    if(!VirtualProtect(image+allocatorSlot,sizeof(void*),PAGE_READWRITE,&allocatorProtection))return false;
    if(!VirtualProtect(image+memcpySlot,sizeof(void*),PAGE_READWRITE,&memcpyProtection)) {
        DWORD previous=0;
        VirtualProtect(image+allocatorSlot,sizeof(void*),allocatorProtection,&previous);
        return false;
    }
    const auto savedAllocator=edf::At<void*>(image,allocatorSlot);
    const auto savedMemcpy=edf::At<void*>(image,memcpySlot);
    const void* const methods[]{nullptr,reinterpret_cast<const void*>(&BodyTestAllocate),
                               reinterpret_cast<const void*>(&BodyTestRelease)};
    const void* const* allocator=methods;
    edf::Put(image,allocatorSlot,&allocator);
    edf::Put(image,memcpySlot,reinterpret_cast<void*>(&std::memcpy));
    alignas(16) unsigned char array[0x30]{};
    edf::Put(array,0x1C,0x00FFFFFFu);
    using Resize=void*(__fastcall*)(void*,int*,void*,int,int);
    const auto resize=reinterpret_cast<Resize>(image+kBodyArrayResize);
    const bool valid=[&] {
        int status=-1;
        if(resize(array,&status,nullptr,64,16)!=&status || status || edf::At<int>(array,8)!=64)return false;
        auto* const first=edf::At<unsigned char*>(array,0);
        if(!first || edf::At<int>(first,0)!=1)return false;
        edf::Put(array,0x1C,2u);
        edf::Put(first,0x50,0x31000000u);
        edf::Put(first+kBodyRecordBytes,0x50,0x31000001u);
        std::array<unsigned char,kBodyRecordBytes*2> live{};
        std::memcpy(live.data(),first,live.size());
        if(resize(array,&status,nullptr,128,16)!=&status || status || edf::At<int>(array,8)!=128)return false;
        const auto* const second=edf::At<const unsigned char*>(array,0);
        if(!second || std::memcmp(live.data(),second,live.size()) || edf::At<unsigned>(array,0x1C)!=2)return false;
        // Native growth appends free IDs without reordering the previous free chain.
        for(int i=2;i<128;++i) {
            const int next=edf::At<int>(second,static_cast<std::size_t>(i)*kBodyRecordBytes);
            if(next!=(i==127 ? -1 : i+1))return false;
        }
        return true;
    }();
    if(auto* const storage=edf::At<void*>(array,0x10))VirtualFree(storage,0,MEM_RELEASE);
    edf::Put(image,allocatorSlot,savedAllocator);
    edf::Put(image,memcpySlot,savedMemcpy);
    DWORD ignored=0;
    const bool memRestored=VirtualProtect(image+memcpySlot,sizeof(void*),memcpyProtection,&ignored)!=FALSE;
    const bool allocatorRestored=VirtualProtect(image+allocatorSlot,sizeof(void*),allocatorProtection,&ignored)!=FALSE;
    return valid && memRestored && allocatorRestored;
}

bool MotionPresetWeakCache() {
    // Run the exact native weak-lookup fragment with a fake entry and report which continuation it takes.
    constexpr unsigned start=0x11AD03B,emptyResult=0x11AD074;
    constexpr unsigned char enter[]{
        0x56,0x48,0x81,0xEC,0xA0,0,0,0,0x89,0x94,0x24,0x98,0,0,0, // save row ID in the original frame slot
    };
    constexpr unsigned char absent[]{0x33,0xC0,0xEB,0x0D};
    constexpr unsigned char renew[]{0xB8,1,0,0,0,0x48,0x81,0xC4,0xA0,0,0,0,0x5E,0xC3};
    unsigned char fragment[sizeof(enter)+kMotionWeakCreate-start+sizeof(renew)]{};
    std::memcpy(fragment,enter,sizeof(enter));
    std::memcpy(fragment+sizeof(enter),image+start,kMotionWeakCreate-start);
    std::memcpy(fragment+sizeof(enter)+emptyResult-start,absent,sizeof(absent));
    std::memcpy(fragment+sizeof(enter)+kMotionWeakCreate-start,renew,sizeof(renew));
    auto* const executable=edf::AllocateNearCode(image+start,fragment,sizeof(fragment));
    if(!executable)return false;
    const auto run=reinterpret_cast<bool(__fastcall*)(void*,unsigned)>(executable);
    alignas(8) unsigned char entry[0x28]{},control[0x10]{};
    bool valid=run(entry,0) && run(entry,4) && !run(entry,5) && !run(entry,0x105);
    edf::Put(entry,0x20,control+0);
    valid=valid && run(entry,1) && !run(entry,0x105);
    edf::Put(control,8,1u);
    valid=valid && !run(entry,2) && edf::At<unsigned>(control,8)==2u;
    VirtualFree(executable,0,MEM_RELEASE);
    return valid;
}

bool MotionLibraryCases() {
    using Row=std::array<std::uint32_t,28>;
    using Add=std::uint16_t*(__fastcall*)(void*,std::uint16_t*,const void*);
    constexpr int large=70656;
    std::vector<Row> rows(large);
    alignas(8) unsigned char library[0x68]{};
    edf::Put(library,0x40,rows.data());
    edf::Put(library,0x5C,0xFFFFFFu);
    const auto add=reinterpret_cast<Add>(image+0xE15190);
    std::mt19937 rng(0xE15190);
    for(int i=0;i<large;++i) {
        for(auto& word:rows[i])word=rng();
        rows[i][0]=1u;
    }
    Row key{};
    for(unsigned i=1;i<key.size();++i)key[i]=rng();
    key[4]=0x7FC00001u;
    key[5]=0x80000000u;
    const auto finds=[&](int count,const Row& wanted,std::uint16_t expected) {
        std::uint16_t out=0x1234;
        edf::Put(library,0x48,count);
        return add(library,&out,&wanted)==&out && out==expected;
    };
    // The private PE mapping leaves the CRT imports unresolved. Restore both after checking insertion.
    constexpr unsigned memcpySlot=0x1756058,memsetSlot=0x1756068;
    auto* const imports=image+memcpySlot;
    DWORD protection=0;
    if(!VirtualProtect(imports,24,PAGE_READWRITE,&protection))return false;
    const auto copy=edf::At<void*>(image,memcpySlot),fill=edf::At<void*>(image,memsetSlot);
    edf::Put(image,memcpySlot,reinterpret_cast<void*>(&std::memcpy));
    edf::Put(image,memsetSlot,reinterpret_cast<void*>(&std::memset));
    const auto insert=[&](unsigned slot,const Row& props) {
        rows[slot][0]=0xFFFFFFu;
        edf::Put(library,0x5C,slot);
        if(!finds(41,props,static_cast<std::uint16_t>(slot)))return false;
        return std::memcmp(rows[slot].data(),props.data(),kMotionEntrySize)==0 &&
               (edf::At<std::uint32_t>(library,0x5C)&0xFFFFFFu)==0xFFFFFFu;
    };
    auto exclusive=key;
    exclusive[0]=1u;
    rows[8]=exclusive;
    const auto verify=[&] {
        if(!insert(9,key) || !insert(10,exclusive) || !finds(41,key,9) ||
           !finds(41,exclusive,0xFFFFu) || edf::At<std::uint32_t>(library,0x60)!=2)return false;
        const Row slot0=rows[0],slot1=rows[1];
        for(unsigned invalid:{65535u,65536u,65537u}) {
            rows[invalid].fill(0xFFFFFFu);
            const Row before=rows[invalid];
            edf::Put(library,0x5C,invalid);
            if(!finds(large,exclusive,0xFFFFu) || rows[invalid]!=before || rows[0]!=slot0 || rows[1]!=slot1 ||
               edf::At<std::uint32_t>(library,0x5C)!=invalid)return false;
        }
        rows[65534][0]=0xFFFFFFu;
        edf::Put(library,0x5C,65534u);
        if(!finds(kMotionSlots,exclusive,65534u) || rows[65534]!=exclusive)return false;

        const auto reserve=Redirected<ReserveMotionFn>(kMotionGrowthCall);
        int status=0;
        if(reserve(library,&status,65536)!=&status || status>=0 || edf::At<int>(library,0x48)!=kMotionSlots)return false;

        const auto objectAdd=Redirected<AddMotionFn>(kMotionObjectCall);
        const auto fallback=[&](const Row& request,std::uint16_t expected) {
            std::uint16_t out=0xFFFF;
            return objectAdd(library,&out,reinterpret_cast<const unsigned char*>(request.data()))==&out && out==expected &&
                   edf::At<std::uint32_t>(library,0x60)==3;
        };
        rows[4]=exclusive;
        if(!fallback(exclusive,4))return false;
        Row request=exclusive;
        request[8]^=1;
        request[2]=0x3F800000u;
        request[4]=0x43480000u;
        if(!fallback(request,1))return false;
        request[2]=0;
        if(!fallback(request,2))return false;
        request[4]=request[5]=0;
        if(!fallback(request,0))return false;
        const auto release=Redirected<ReleaseMotionFn>(kMotionReleaseCall);
        release(library,4);
        release(library,0xFFFF);
        if(rows[4]!=exclusive || edf::At<std::uint32_t>(library,0x60)!=3)return false;
        release(library,65534);
        return edf::At<std::uint32_t>(library,0x60)==2 &&
               (edf::At<std::uint32_t>(library,0x5C)&0xFFFFFFu)==65534u &&
               finds(kMotionSlots,exclusive,65534u);

    };
    const bool valid=verify();
    edf::Put(image,memcpySlot,copy);
    edf::Put(image,memsetSlot,fill);
    DWORD old=0;
    return VirtualProtect(imports,24,protection,&old) && valid;
}

// The shared motion index's sites, saved before and after InstallNativeBodyMath so one process can run the stock and
// the indexed code side by side on two libraries fed the same operations.
struct MotionIndexSite { unsigned rva; unsigned size; std::vector<unsigned char> stock,patched; };
std::array<MotionIndexSite,6> motionIndexSites{{
    {kMotionSearch,kMotionSearchHit-kMotionSearch,{},{}},{kMotionAllocCopy,5,{},{}},{kMotionUpdateCopy,5,{},{}},
    {kMotionSlotReleaseCall,5,{},{}},{kMotionBuildCalls[0],5,{},{}},{kMotionBuildCalls[1],5,{},{}},
}};

void SaveMotionIndexSites(bool patched) {
    for(auto& site:motionIndexSites)
        (patched ? site.patched : site.stock).assign(image+site.rva,image+site.rva+site.size);
}

bool UseMotionIndex(bool patched) noexcept {
    for(auto& site:motionIndexSites) {
        auto* const at=image+site.rva;
        DWORD old=0;
        if(!VirtualProtect(at,site.size,PAGE_EXECUTE_READWRITE,&old))return false;
        std::memcpy(at,(patched ? site.patched : site.stock).data(),site.size);
        DWORD ignored=0;
        VirtualProtect(at,site.size,old,&ignored);
    }
    return FlushInstructionCache(GetCurrentProcess(),nullptr,0)!=FALSE;
}

// Points the CRT imports and the Havok allocator the motion library uses at local stand-ins while alive.
struct MotionRuntime {
    static constexpr unsigned kImports=0x1756058,kAllocator=0x1F826E0;
    const void* methods[6]{nullptr,reinterpret_cast<const void*>(&BodyTestAllocate),
                           reinterpret_cast<const void*>(&BodyTestRelease),nullptr,nullptr,nullptr};
    const void* const* allocator=methods;
    void* savedImports[3]{};
    void* savedAllocator=nullptr;
    DWORD importProtection=0,allocatorProtection=0;
    bool ready=false;
    MotionRuntime() {
        if(!VirtualProtect(image+kImports,24,PAGE_READWRITE,&importProtection))return;
        if(!VirtualProtect(image+kAllocator,8,PAGE_READWRITE,&allocatorProtection))return;
        std::memcpy(savedImports,image+kImports,sizeof(savedImports));
        savedAllocator=edf::At<void*>(image,kAllocator);
        edf::Put(image,kImports,reinterpret_cast<void*>(&std::memcpy));
        edf::Put(image,kImports+8,reinterpret_cast<void*>(&std::memmove));
        edf::Put(image,kImports+16,reinterpret_cast<void*>(&std::memset));
        edf::Put(image,kAllocator,&allocator);
        ready=true;
    }
    ~MotionRuntime() {
        if(!ready)return;
        std::memcpy(image+kImports,savedImports,sizeof(savedImports));
        edf::Put(image,kAllocator,savedAllocator);
        DWORD ignored=0;
        VirtualProtect(image+kImports,24,importProtection,&ignored);
        VirtualProtect(image+kAllocator,8,allocatorProtection,&ignored);
    }
};

using MotionRecord=std::array<std::uint32_t,28>;
using MotionAddFn=std::uint16_t*(__fastcall*)(unsigned char*,std::uint16_t*,const void*);
using MotionUpdateFn=void(__fastcall*)(unsigned char*,std::uint16_t,const void*);
using MotionCtorFn=unsigned char*(__fastcall*)(unsigned char*,int);
using MotionGrowFn=int*(__fastcall*)(unsigned char*,int*,int);

struct MotionPair {
    alignas(16) unsigned char stock[0x100]{},indexed[0x100]{};
    std::uint64_t steps=0,fullCompares=0,hits=0,misses=0,freedLinkZeroHits=0;
    const char* failure=nullptr;

    static int Count(const unsigned char* lib) {return edf::At<int>(lib,0x48);}
    static const unsigned char* Rows(const unsigned char* lib) {return edf::At<const unsigned char*>(lib,0x40);}

    // Runs `op` on the stock library with the stock code, then on the indexed one with the patched code.
    template<class Op> bool Both(Op op) {
        if(!UseMotionIndex(false))return Fail("cannot restore the stock code");
        const auto a=op(stock);
        if(!UseMotionIndex(true))return Fail("cannot install the indexed code");
        const auto b=op(indexed);
        ++steps;
        return a==b || Fail("results differ");
    }
    bool Fail(const char* what) {
        if(!failure)failure=what;
        return false;
    }
    bool Headers() {
        for(const unsigned offset:{0x48u,0x5Cu,0x60u,0x64u})
            if(edf::At<std::uint32_t>(stock,offset)!=edf::At<std::uint32_t>(indexed,offset))return Fail("library header differs");
        return true;
    }
    bool Row(int id) {
        if(id<0 || id>=Count(stock))return true;
        return !std::memcmp(Rows(stock)+static_cast<std::size_t>(id)*kMotionEntrySize,
                            Rows(indexed)+static_cast<std::size_t>(id)*kMotionEntrySize,kMotionEntrySize) ||
               Fail("touched row differs");
    }
    bool Table() {
        ++fullCompares;
        return Headers() &&
               (!std::memcmp(Rows(stock),Rows(indexed),static_cast<std::size_t>(Count(stock))*kMotionEntrySize) ||
                Fail("entries table differs"));
    }
    bool Construct() {
        return Both([](unsigned char* lib) {
            std::memset(lib,0,0x100);
            reinterpret_cast<MotionCtorFn>(image+0xE150C0)(lib,0x400);
            return 0;
        }) && Both([](unsigned char* lib) {
            // The stock build for the stock library; the redirected build (resets the list) for the indexed one.
            const auto build=image[kMotionBuildCalls[0]]==0xE8 ? Redirected<MotionBuildFn>(kMotionBuildCalls[0])
                                                                : reinterpret_cast<MotionBuildFn>(image+kMotionBuild);
            build(lib,0.5f);
            return 0;
        }) && Table();
    }
    // The ID written back, or 0x10000 + the low 16 bits of a wrong return pointer.
    bool Add(const MotionRecord& key,bool full) {
        std::uint32_t id=0;
        const auto live=edf::At<std::uint32_t>(stock,0x60);
        if(!Both([&](unsigned char* lib) {
            MotionRecord copy=key;
            std::uint16_t out=0x5A5A;
            const auto* const result=reinterpret_cast<MotionAddFn>(image+kMotionAdd)(lib,&out,copy.data());
            id=result==&out ? out : 0x10000u;
            return id;
        }))return false;
        if(!key[0])++(id<0xFFFF && edf::At<std::uint32_t>(stock,0x60)==live ? hits : misses);
        return Headers() && Row(static_cast<int>(id)) && (!full || Table());
    }
    bool Update(int id,const MotionRecord& key,bool full) {
        return Both([&](unsigned char* lib) {
            MotionRecord copy=key;
            reinterpret_cast<MotionUpdateFn>(image+0xE15CC0)(lib,static_cast<std::uint16_t>(id),copy.data());
            return 0;
        }) && Headers() && Row(id) && (!full || Table());
    }
    bool Release(int id,bool raw,bool full) {
        return Both([&](unsigned char* lib) {
            if(raw)reinterpret_cast<ReleaseMotionFn>(image+kMotionRelease)(lib,static_cast<std::uint16_t>(id));
            else Redirected<ReleaseMotionFn>(kMotionReleaseCall)(lib,static_cast<std::uint16_t>(id));
            return 0;
        }) && Headers() && Row(id) && (!full || Table());
    }
    bool Grow(int wanted) {
        return Both([&](unsigned char* lib) {
            int status=0;
            const auto* const result=Redirected<MotionGrowFn>(kMotionGrowthCall)(lib,&status,wanted);
            return result==&status ? status : 1;
        }) && Table();
    }
    bool LiveRow(int id) const {
        return id>=0 && id<Count(stock) && Rows(stock)[static_cast<std::size_t>(id)*kMotionEntrySize+0x38]!=0xFF;
    }
    // A free row whose link is 0 (slot 0 heads the free list) compares like a shared entry in the stock scan.
    int FreeRowWithZeroLink() const {
        const int count=Count(stock);
        for(int id=0;id<count;++id) {
            const auto* const row=Rows(stock)+static_cast<std::size_t>(id)*kMotionEntrySize;
            if(row[0x38]==0xFF && !edf::At<std::uint32_t>(row,0))return id;
        }
        return -1;
    }
    MotionRecord RowRecord(int id) const {
        MotionRecord record{};
        std::memcpy(record.data(),Rows(stock)+static_cast<std::size_t>(id)*kMotionEntrySize,kMotionEntrySize);
        return record;
    }
    void Free() {
        for(auto* lib:{stock+0,indexed+0})
            if(edf::At<int>(lib,0x4C)>=0 && edf::At<void*>(lib,0x40))VirtualFree(edf::At<void*>(lib,0x40),0,MEM_RELEASE);
    }
};

// The same random operations on a stock and an indexed library through the real EDF.dll code, compared at every step.
bool MotionIndexLockstep() {
    MotionRuntime runtime;
    if(!runtime.ready)return false;
    auto pair=std::make_unique<MotionPair>();
    auto& p=*pair;
    std::mt19937 rng(0x5EA2C4);
    const auto word=[&] {return static_cast<std::uint32_t>(rng());};
    std::vector<MotionRecord> pool;
    for(unsigned i=0;i<24;++i) {
        MotionRecord record{};
        for(auto& w:record)w=word();
        record[0]=0;
        pool.push_back(record);
        pool.push_back(record);                        // a duplicate request
        for(unsigned field:{1u,13u,27u}) {             // records that differ in one field (low bit, sign, NaN payload)
            MotionRecord variant=record;
            variant[field]^=field==1 ? 1u : field==13 ? 0x80000000u : 0x00400001u;
            pool.push_back(variant);
        }
    }
    const auto exclusive=[&] {
        MotionRecord record=pool[rng()%pool.size()];
        record[0]=1;
        record[2]^=word()&0xFF;
        return record;
    };
    const auto anyId=[&](int extra) {return static_cast<int>(rng()%static_cast<unsigned>(MotionPair::Count(p.stock)+extra));};
    const auto liveId=[&] {
        for(unsigned tries=0;tries<64;++tries) {
            const int id=anyId(0);
            if(p.LiveRow(id))return id;
        }
        return -1;
    };
    bool ok=p.Construct();

    // Phase 1: a small library, the whole table compared after every operation.
    for(unsigned step=0;ok && step<6000;++step) {
        if(step==3000)ok=p.Construct();             // a new library at the same address drops the old list
        const unsigned roll=rng()%100;
        if(roll<34) {
            ok=p.Add(pool[rng()%pool.size()],true);
        } else if(roll<50) {
            ok=p.Add(exclusive(),true);
        } else if(roll<64) {
            const int id=liveId();
            // In-place rewrites: shared contents (duplicates of other slots), exclusive ones, flag flips.
            if(id>=0)ok=p.Update(id,rng()%3 ? pool[rng()%pool.size()] : exclusive(),true);
        } else if(roll<74) {
            const int id=liveId();
            if(id>=0)ok=p.Release(id,false,true);
        } else if(roll<86) {
            // The stock release trusts its ID; only live rows, the five presets now and then.
            const int preset=static_cast<int>(rng()%5);
            const int id=rng()%8 || !p.LiveRow(preset) ? liveId() : preset;
            if(id>=0)ok=p.Release(id,true,true);
        } else if(roll<90) {
            const int id=p.FreeRowWithZeroLink();
            if(id>=0) {
                const auto before=p.hits;
                ok=p.Add(p.RowRecord(id),true);
                p.freedLinkZeroHits+=p.hits!=before;
            }
        } else if(roll<93) {
            ok=p.Grow(MotionPair::Count(p.stock)+64);
        } else {
            // A request equal to a live shared row, wherever it is: the lowest equal ID wins.
            const int id=liveId();
            if(id>=0 && !edf::At<std::uint32_t>(MotionPair::Rows(p.stock)+id*kMotionEntrySize,0))
                ok=p.Add(p.RowRecord(id),true);
        }
    }

    // Phase 2: fill to the 65,535-slot limit with exclusive entries and some shared ones; touched rows each step.
    // Until the last slot is taken (stops at a step limit if the library never fills).
    for(unsigned step=0;ok && step<200000 && (MotionPair::Count(p.stock)<kMotionSlots ||
                                              (edf::At<std::uint32_t>(p.stock,0x5C)&0xFFFFFF)!=0xFFFFFF);++step) {
        const bool full=step%4096==0;
        if((edf::At<std::uint32_t>(p.stock,0x5C)&0xFFFFFF)==0xFFFFFF && MotionPair::Count(p.stock)<kMotionSlots) {
            ok=p.Grow((std::min)(MotionPair::Count(p.stock)+1024,kMotionSlots));
            continue;
        }
        const unsigned roll=rng()%100;
        if(roll<80)ok=p.Add(exclusive(),full);
        else if(roll<90)ok=p.Add(pool[rng()%pool.size()],full);
        else if(roll<95) {
            const int id=liveId();
            if(id>=0)ok=p.Release(id,roll&1,full);
        }
        else {
            const int id=liveId();
            if(id>=0)ok=p.Update(id,pool[rng()%pool.size()],full);
        }
    }
    const int filled=MotionPair::Count(p.stock);
    ok=ok && filled==kMotionSlots && p.Table() && p.Grow(kMotionSlots+1);

    // Phase 3: the full library: misses, releases and re-adds that reuse the freed IDs.
    for(unsigned step=0;ok && step<4000;++step) {
        const unsigned roll=rng()%100;
        if(roll<40)ok=p.Add(pool[rng()%pool.size()],step%256==0);
        else if(roll<60)ok=p.Add(exclusive(),step%256==0);
        else if(roll<80) {
            const int id=liveId();
            if(id>=0)ok=p.Release(id,roll&1,step%256==0);
        }
        else {
            const int id=liveId();
            if(id>=0)ok=p.Update(id,rng()%2 ? pool[rng()%pool.size()] : exclusive(),step%256==0);
        }
    }
    ok=ok && p.Table() && UseMotionIndex(true);
    std::printf("motion index lockstep: %llu paired steps, %llu full-table compares, %d slots, "
                "%llu shared hits, %llu misses, %llu hits on free rows with link 0%s%s\n",
                static_cast<unsigned long long>(p.steps),static_cast<unsigned long long>(p.fullCompares),filled,
                static_cast<unsigned long long>(p.hits),static_cast<unsigned long long>(p.misses),
                static_cast<unsigned long long>(p.freedLinkZeroHits),p.failure ? ": " : "",p.failure ? p.failure : "");
    p.Free();
    return ok;
}

// The field's library: 70,656 rows, about 30 shared at the front, the rest exclusive; no free slot, so a miss fails.
bool MotionIndexTiming() {
    MotionRuntime runtime;
    if(!runtime.ready)return false;
    constexpr int rows=70656,shared=30;
    std::vector<MotionRecord> table(rows);
    std::mt19937 rng(0x70A56);
    for(auto& row:table) {
        for(auto& w:row)w=static_cast<std::uint32_t>(rng());
        row[0]=1;
    }
    std::vector<int> sharedIds;
    for(int i=0;i<shared;++i)sharedIds.push_back(5+i*(i<25 ? 1 : 997));   // a few beyond the first block
    for(const int id:sharedIds)table[id][0]=0;
    alignas(16) unsigned char library[0x100]{};
    edf::Put(library,0x40,table.data());
    edf::Put(library,0x48,rows);
    edf::Put(library,0x4C,0x80000000u|rows);
    edf::Put(library,0x5C,0xFFFFFFu);
    std::vector<MotionRecord> keys(256);
    for(std::size_t i=0;i<keys.size();++i) {
        keys[i]=table[sharedIds[i%sharedIds.size()]];
        if(i%2)keys[i][7]^=1;                          // half are misses that scan the whole stock table
    }
    const auto add=reinterpret_cast<MotionAddFn>(image+kMotionAdd);
    std::vector<std::uint16_t> stockIds(keys.size()),indexIds(keys.size());
    const auto run=[&](bool patched,std::vector<std::uint16_t>& ids) {
        UseMotionIndex(patched);
        const auto start=std::chrono::steady_clock::now();
        for(std::size_t i=0;i<keys.size();++i)add(library,&ids[i],keys[i].data());
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    };
    run(true,indexIds);                                // makes the index once
    std::array<double,5> stockTimes{},indexTimes{};
    for(std::size_t i=0;i<stockTimes.size();++i) {
        if(i%2) {
            indexTimes[i]=run(true,indexIds);
            stockTimes[i]=run(false,stockIds);
        } else {
            stockTimes[i]=run(false,stockIds);
            indexTimes[i]=run(true,indexIds);
        }
    }
    const bool restored=UseMotionIndex(true);
    std::sort(stockTimes.begin(),stockTimes.end());
    std::sort(indexTimes.begin(),indexTimes.end());
    unsigned hits=0;
    for(std::size_t i=0;i<keys.size();++i)hits+=stockIds[i]!=0xFFFF;
    std::printf("motion index timing: %d rows, %d shared, 256 adds (%u hits): %.3f -> %.4f ms (median of five runs)\n",
                rows,shared,hits,stockTimes[2],indexTimes[2]);
    return restored && stockIds==indexIds && hits==128 && edf::At<int>(library,0x48)==rows;
}

}

int main(int argc,char** argv) {
    if(argc<2 || !argv[1][0] || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: native body math comparison requires EDF.dll");
        return 77;
    }
    image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    if(!image)return 1;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
    if(nt->FileHeader.TimeDateStamp!=0x678CCB46)return 1;
    SaveMotionIndexSites(false);
    if(InstallNativeBodyMath()!=kNativeBodyMathSites) {
        std::puts("FAIL: native physics call sites or signatures do not match");
        return 1;
    }
    SaveMotionIndexSites(true);
    if(!BodyGrowthCases()) {
        std::puts("FAIL: body capacity policy or native free ID order");
        return 1;
    }
    if(!MotionPresetWeakCache()) {
        std::puts("FAIL: exhausted motion preset wrapper lifetime mismatch");
        return 1;
    }
    if(!MotionLibraryCases()) {
        std::puts("FAIL: motion library ID order, miss or insertion mismatch");
        return 1;
    }
    if(!ProxyPosePrefetchCases()) {
        std::puts("FAIL: collision proxy pose publication mismatch");
        return 1;
    }
    if(!BodySignalDispatch()) {
        std::puts("FAIL: empty body signal gate dispatch mismatch");
        return 1;
    }
    if(!BodyQuaternionUpdates()) {
        std::puts("FAIL: static body quaternion skip mismatch");
        return 1;
    }
    if(!MotionIndexLockstep()) {
        std::puts("FAIL: shared motion index differs from the stock scan");
        return 1;
    }
    if(!MotionIndexTiming()) {
        std::puts("FAIL: shared motion index timing run returned different IDs");
        return 1;
    }
    std::puts("native body math: body growth, motion ID limit, shared motion index, proxy prefetch, signal gate and quaternion skip ok");
    return 0;
}
