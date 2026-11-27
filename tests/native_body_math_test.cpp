// Check the two native physics-table patches (rigid-body table growth, motion ID limit) on a private EDF.dll mapping.
#include "../src/native_body_math.cpp"
#include <array>
#include <cstdio>
#include <random>
#include <vector>

namespace crew { unsigned char* image=nullptr; void Log(const char*,...) noexcept {} }

namespace {
using namespace crew;

template<class Fn> Fn Redirected(unsigned site) noexcept {
    const auto* const instruction=image+site;
    return reinterpret_cast<Fn>(instruction+5+edf::At<std::int32_t>(instruction,1));
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
    if(InstallNativeBodyMath()!=kNativeBodyMathSites) {
        std::puts("FAIL: native body growth or motion ID limit call sites or signatures do not match");
        return 1;
    }
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
    std::puts("native body math: body growth and motion ID limit ok");
    return 0;
}
