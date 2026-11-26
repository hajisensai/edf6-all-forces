// Compare the audited attachment call sites with the original math on a private EDF.dll mapping.
#include "../src/native_body_math.cpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

namespace crew { unsigned char* image=nullptr; void Log(const char*,...) noexcept {} }
namespace crew::perfprobe {
bool traceCommands=false;
unsigned tracedCommands[256]{};
bool ContactCommandsActive() noexcept {return traceCommands;}
void ObserveContactCommand(unsigned kind) noexcept {++tracedCommands[kind];}
}

namespace {
using namespace crew;
using BoxRun=void(__fastcall*)(void*,void*,const void*);
using BasisFn=void(__fastcall*)(float*);
struct alignas(16) Matrix { float data[16]; };
struct alignas(16) Box { float data[8]; };
struct alignas(16) Proxy { unsigned char prefix[0x20]; Box source,output; Matrix matrix; };
struct alignas(16) MergeFrame {
    Box bounds;
    unsigned char scratch[0x20];
    std::uintptr_t arg8,arg9;
    unsigned update;
};
static_assert(offsetof(Proxy,matrix)==0x60 && offsetof(MergeFrame,arg8)==0x40);

// Run the original block copied from the PE or the installed block in the private mapping.
// The wrapper recreates the aligned body-update stack and supplies rbp (bounds) and rbx (proxy).
constexpr unsigned char kRunner[]={
    0x53,0x55,0x48,0x83,0xEC,0x30,0x48,0x8B,0xE9,0x48,0x8B,0xDA,0x41,0xFF,0xD0,
    0x48,0x83,0xC4,0x30,0x5D,0x5B,0xC3,
};
// At the native continuation verify the two registers and fifth stack argument, then return.
constexpr unsigned char kCapture[]={
    0x4C,0x89,0x45,0x40,0x4C,0x89,0x4D,0x48,0x8B,0x44,0x24,0x20,0x89,0x45,0x50,0xC3,
};
constexpr std::size_t kCopiedBox=32,kPackedBox=256;

unsigned char* BoxHarness() noexcept {
    auto* const mem=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!mem)return nullptr;
    std::memcpy(mem,kRunner,sizeof(kRunner));
    const auto copy=[&](std::size_t offset,std::uintptr_t callee) {
        auto* const block=mem+offset;
        auto* const tail=block+sizeof(kBoxMergeStock);
        auto* const thunk=tail+sizeof(kCapture);
        std::memcpy(block,image+kBoxMerge,sizeof(kBoxMergeStock));
        constexpr unsigned call=kBoxCall-kBoxMerge;
        const auto rel=static_cast<std::int32_t>(thunk-(block+call+5));
        std::memcpy(block+call+1,&rel,sizeof(rel));
        std::memcpy(tail,kCapture,sizeof(kCapture));
        unsigned char jump[]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
        std::memcpy(jump+2,&callee,sizeof(callee));
        std::memcpy(thunk,jump,sizeof(jump));
    };
    copy(kCopiedBox,reinterpret_cast<std::uintptr_t>(image+kTransformBox));
    copy(kPackedBox,reinterpret_cast<std::uintptr_t>(&TransformBox));
    DWORD protect=0;
    if(!VirtualProtect(mem,4096,PAGE_EXECUTE_READ,&protect)) {VirtualFree(mem,0,MEM_RELEASE);return nullptr;}
    FlushInstructionCache(GetCurrentProcess(),mem,4096);
    return mem;
}

bool CaptureBoxArguments() noexcept {
    unsigned char continuation[sizeof(kCapture)];
    std::memcpy(continuation,image+kBoxMergeEnd,sizeof(continuation));
    return edf::PatchCode(image+kBoxMergeEnd,continuation,kCapture,sizeof(kCapture));
}

bool ArgumentsMatch(const MergeFrame& frame,const Proxy& proxy) noexcept {
    return frame.arg8==reinterpret_cast<std::uintptr_t>(&proxy.matrix) &&
           frame.arg9==reinterpret_cast<std::uintptr_t>(frame.scratch) && frame.update==1;
}

template<class Fn> Fn Redirected(unsigned site) noexcept {
    const auto* const instruction=image+site;
    return reinterpret_cast<Fn>(instruction+5+edf::At<std::int32_t>(instruction,1));
}

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
bool BoundsValidation() noexcept {
    using Check=bool(__fastcall*)(const float*);
    const auto* const code=image+kBoundsCheck;
    if(code[4]!=0xE8 || code[11]!=0x75 || kBoundsCheck+13+code[12]!=kBoundsInvalid ||
       code[17]!=0xEB || kBoundsCheck+19+code[18]!=kBoundsValid)return false;
    const auto old=reinterpret_cast<Check>(image+kScalarBoundsCheck);
    const auto check=Redirected<Check>(kBoundsCheckCall);
    std::mt19937 rng(130027);
    const unsigned mxcsr=_mm_getcsr(),clean=mxcsr&~0x3Fu;
    for(unsigned i=0;i<100000;++i) {
        Box bounds{};
        for(float& f:bounds.data) {
            const std::uint32_t bits=rng();
            std::memcpy(&f,&bits,4);
        }
        if(i%7==0)bounds.data[i%8]=std::numeric_limits<float>::quiet_NaN();
        if(i%11==0)bounds.data[i%8]=std::numeric_limits<float>::infinity();
        _mm_setcsr(clean);
        const bool expected=old(bounds.data)||old(bounds.data+4);
        const auto expectedFlags=_mm_getcsr()&0x3Fu;
        _mm_setcsr(clean);
        if(check(bounds.data)!=expected || (_mm_getcsr()&0x3Fu)!=expectedFlags) {
            _mm_setcsr(mxcsr);
            std::printf("FAIL: packed bounds validation, case %u\n",i);
            return false;
        }
    }
    _mm_setcsr(mxcsr);
    std::puts("native bounds validation: 100,000 matching results and floating-point flags");
    return true;
}

bool AvoidanceNeighbors() noexcept {
    using Sift=int(__fastcall*)(AvoidanceHeap*,int) noexcept;
    using Raise=void(__fastcall*)(AvoidanceHeap*,int) noexcept;
    std::mt19937 rng(2101);
    constexpr unsigned cases=24000,capacity=64;
    const auto compare=[&](unsigned call,auto run) {
        for(unsigned i=0;i<cases;++i) {
            std::array<AvoidanceNeighbor,capacity> expected{},actual{};
            const int count=static_cast<int>(rng()%(capacity+1));
            const int root=count ? static_cast<int>(rng()%static_cast<unsigned>(count)) : 0;
            for(int j=0;j<count;++j) {
                float distance=static_cast<float>(rng()%31);
                if(i%7==0 && j%11==0)distance=std::numeric_limits<float>::quiet_NaN();
                if(i%9==0 && j%7==0)distance=-0.0f;
                expected[static_cast<std::size_t>(j)]={rng(),distance,static_cast<std::uint32_t>(rng())};
            }
            actual=expected;
            AvoidanceHeap a{expected.data(),count},b{actual.data(),count};
            if(!run(a,b,root) || a.count!=b.count ||
               std::memcmp(expected.data(),actual.data(),sizeof(expected))!=0) {
                std::printf("FAIL: neighbor heap at %x, case %u\n",call,i);
                return false;
            }
        }
        return true;
    };

    const auto stockSift=reinterpret_cast<Sift>(image+kAvoidanceSift);
    for(const auto& site:kAvoidanceCalls) {
        if(image[site.call]!=0xE8)return false;
        const auto candidate=Redirected<Sift>(site.call);
        if(candidate==stockSift || !compare(site.call,[&](AvoidanceHeap& a,AvoidanceHeap& b,int root) {
            return stockSift(&a,root)==candidate(&b,root);
        }))return false;
    }
    const auto stockRaise=reinterpret_cast<Raise>(image+kAvoidanceRaise);
    if(!compare(kAvoidancePush.call,[&](AvoidanceHeap& a,AvoidanceHeap& b,int root) {
        stockRaise(&a,root);
        RaiseAvoidanceNeighbor(&b,root);
        return true;
    }))return false;

    using Push=void(__fastcall*)(AvoidanceHeap*) noexcept;
    const auto* const entry=image+kAvoidanceOverflow;
    if(entry[0]!=0x48 || entry[1]!=0x8B || entry[2]!=0xCB || entry[3]!=0xE8 ||
       entry[8]!=0xEB || kAvoidancePop+entry[9]!=kAvoidanceDone)return false;
    const auto overflow=Redirected<Push>(kAvoidanceOverflowCall);
    if(!compare(kAvoidanceOverflowCall,[&](AvoidanceHeap& a,AvoidanceHeap& b,int root) {
        if(a.count==0)return true;
        const int last=a.count-1;
        if(root%6==0 && last>0) {
            // A far candidate, with strict distances forward and sideways along its path.
            for(int i=0;i<last;++i)a.entries[i].distanceSquared=static_cast<float>(last-i);
            a.entries[last].distanceSquared=FLT_MAX;
        } else if(root%2==0) {
            a.count=last;
            for(int i=last/2;i>0;--i)stockSift(&a,i-1);
            a.count=last+1;
        }
        std::copy_n(a.entries,a.count,b.entries);
        stockRaise(&a,last);
        if(last==0) {
            a.count=0;
        } else {
            const bool farther=a.entries[last].distanceSquared>a.entries[0].distanceSquared;
            a.entries[0].handle=a.entries[last].handle;
            a.entries[0].distanceSquared=a.entries[last].distanceSquared;
            a.count=last;
            if(!farther)stockSift(&a,0);
        }
        overflow(&b);
        return true;
    }))return false;
    std::printf("native avoidance heap: %u byte-identical cases per sift, raise, and overflow path\n",cases);
    return true;
}

struct ScanBounds { std::uint32_t lower[4],upper[4]; };
static_assert(sizeof(ScanBounds)==0x20 && offsetof(ScanBounds,upper)==0x10);
using Scan=const ScanBounds*(__fastcall*)(const ScanBounds*,const ScanBounds*,std::uint32_t);

bool AvoidanceBoxes() noexcept {
    constexpr unsigned char entry[]{
        0x53,0x56,0x57,0x41,0x56,    // save nonvolatile registers used by the stock loop
        0x45,0x8B,0xC8,              // mov r9d,r8d  X cutoff
        0x4C,0x8B,0xC1,              // mov r8,rcx   reference box
    };
    constexpr unsigned char missed[]{0x33,0xC0,0x41,0x5E,0x5F,0x5E,0x5B,0xC3};
    constexpr unsigned char found[]{0x48,0x8B,0xC2,0x41,0x5E,0x5F,0x5E,0x5B,0xC3};
    constexpr auto bytes=sizeof(kAvoidanceScanStock);
    const auto* const patched=image+kAvoidanceScan;
    if(patched[kPackedOverlapDisp-1]!=0x74 ||
       kPackedOverlapDisp+1+patched[kPackedOverlapDisp]!=kAvoidanceScanOverlap-kAvoidanceScan)return false;
    auto* const mem=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!mem)return false;
    const auto copy=[&](std::size_t offset,const unsigned char* body,unsigned branchDisp) {
        auto* const runner=mem+offset;
        auto* const loop=runner+sizeof(entry);
        std::memcpy(runner,entry,sizeof(entry));
        std::memcpy(loop,body,bytes);
        std::memcpy(loop+bytes,missed,sizeof(missed));
        std::memcpy(loop+bytes+sizeof(missed),found,sizeof(found));
        loop[branchDisp]=static_cast<unsigned char>(bytes+sizeof(missed)-branchDisp-1);
        return reinterpret_cast<Scan>(runner);
    };
    const auto stock=copy(0,kAvoidanceScanStock,kStockOverlapDisp);
    const auto candidate=copy(128,patched,kPackedOverlapDisp);
    DWORD protect=0;
    if(!VirtualProtect(mem,4096,PAGE_EXECUTE_READ,&protect)) {VirtualFree(mem,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),mem,4096);

    std::mt19937 rng(22712);
    std::array<ScanBounds,33> bounds{};
    ScanBounds reference{};
    constexpr unsigned cases=60000;
    bool matched=true;
    for(unsigned i=0;i<cases && matched;++i) {
        for(unsigned j=0;j<bounds.size();++j) {
            auto& x=bounds[j];
            for(unsigned k=1;k<4;++k) {x.lower[k]=rng();x.upper[k]=rng();}
            x.lower[0]=j+i;
        }
        for(unsigned k=1;k<4;++k) {reference.lower[k]=rng();reference.upper[k]=rng();}
        const auto limit=static_cast<std::uint32_t>(i+1u+rng()%31);
        if(i%4==0) {
            // Exact Y/Z edges count as overlap. Put one either side of the strict X cutoff.
            const auto index=(limit-i)-(i%8==0 ? 1u : 0u);
            for(unsigned k=1;k<=2;++k) {
                bounds[index].lower[k]=reference.upper[k];
                bounds[index].upper[k]=reference.lower[k];
            }
        }
        matched=stock(&reference,bounds.data(),limit)==candidate(&reference,bounds.data(),limit);
    }
    if(matched) {
        std::puts("native avoidance sweep: 60,000 identical first overlaps or cutoff results");
        // Force the complete short sweep and a final true hit for a stable instruction-path comparison.
        reference.lower[1]=reference.lower[2]=100;
        reference.upper[1]=reference.upper[2]=200;
        for(unsigned j=0;j<bounds.size();++j)bounds[j]={{j,300,300,0},{j,400,400,0}};
        bounds[11]={{11,150,150,0},{11,160,160,0}};
        const ScanBounds* volatile result=nullptr;
        Bench("12-box avoidance sweeps",
              [&](unsigned){result=stock(&reference,bounds.data(),12);},
              [&](unsigned){result=candidate(&reference,bounds.data(),12);});
        matched=result==&bounds[11];
    }
    VirtualFree(mem,0,MEM_RELEASE);
    return matched;
}

struct StreamBlock {
    std::uint16_t records=0;
    unsigned char padding[6]{};
    StreamBlock* next=nullptr;
    unsigned char footer[16]{};
    unsigned char payload[0xEE0]{};
};
static_assert(offsetof(StreamBlock,payload)==0x20 && offsetof(StreamBlock,next)==8);
struct StreamWriter {
    void* stream=nullptr;
    void* allocator=nullptr;
    StreamBlock* block=nullptr;
    int size=0,count=0;
};
static_assert(offsetof(StreamWriter,size)==0x18);
struct StreamResults {
    std::uint64_t digest=0;
    unsigned calls=0;
};
struct StreamHandler {
    void** table;
    StreamResults* state;
    unsigned kind;
};

void __fastcall TestCommand(StreamHandler* handler,unsigned char* record) {
    auto& state=*handler->state;
    state.digest=state.digest*131+edf::At<std::uint32_t>(record,4)+handler->kind;
    ++state.calls;
    if(record[2]&2)record[0]=16; // The virtual callback may change the recorded length.
}
void WriteRecord(StreamBlock& block,unsigned offset,std::uint16_t size,unsigned char flags,
                 unsigned char kind,std::uint32_t payload) {
    auto* const record=block.payload+offset;
    std::memcpy(record,&size,sizeof(size));
    record[2]=flags;
    record[3]=kind;
    std::memcpy(record+4,&payload,sizeof(payload));
}
// The collector ABI is larger than the result consumed by a clearance caller. Verify the full
// reset state against the native reset, including unused payload and the chosen dispatch table.
bool RayResetTemplates() {
    using Reset=void(__fastcall*)(void*);
    using Get=const void*(__fastcall*)(const void*);
    using Add=void(__fastcall*)(void*,const void*);
    struct alignas(16) Hits { unsigned char bytes[0xA0]{}; };
    const auto reset=reinterpret_cast<Reset>(image+0xFDF00);
    const auto get=reinterpret_cast<Get>(image+0xFDDB0);
    const auto add=reinterpret_cast<Add>(image+0xD93980);
    const void* tables[]={image+0x1768B78,image+0x179CBE8};
    auto setup=[&](unsigned kind,Hits& hits) {
        std::memcpy(hits.bytes,&tables[kind],sizeof(void*));
        reset(&hits);
    };
    Hits starts[2]{};
    setup(0,starts[0]);setup(1,starts[1]);
    for(unsigned i=0;i<2;++i) {
        Hits old{};
        setup(i,old);
        auto clone=starts[i];
        if(std::memcmp(&old,&clone,sizeof(Hits))!=0)return false;
        alignas(16) unsigned char hit[0x70]{};
        const float fraction=0.5f;
        std::memcpy(hit+0x20,&fraction,sizeof(fraction));
        hit[0x6C]=2;
        add(&old,hit);add(&clone,hit);
        if(std::memcmp(&old,&clone,sizeof(Hits))!=0 || edf::At<std::int32_t>(clone.bytes,0x0C)!=1)return false;
        clone=starts[i];
        if(edf::At<std::int32_t>(clone.bytes,0x0C)!=0 || get(&clone)!=clone.bytes+0x30)return false;
    }
    Bench("ray collector initializations",[&](unsigned i){
        Hits hits{};
        setup(i&1u,hits);
        get(&hits);
    },[&](unsigned i){
        auto hits=starts[i&1u];
        get(&hits);
    });
    return true;
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



struct TestMotionGrowthListener {
    const void* table;
    std::uintptr_t next;
    unsigned calls;
};

void __fastcall TestMotionGrowth(TestMotionGrowthListener* listener,void* library) {
    ++listener->calls;
    auto* const rows=edf::At<unsigned char*>(library,0x40);
    edf::Put(rows,20*kMotionEntrySize,0xFFFFFFu);
    edf::Put(library,0x5C,20u);
}

bool MotionGrowthGateCases(void* library,const unsigned char* key) {
    const auto add=reinterpret_cast<AddMotionFn>(image+kMotionAdd);
    auto* const lib=static_cast<unsigned char*>(library);
    TestMotionGrowthListener stock{image+kMotionGrowSlot,0,0};
    edf::Put(lib,0x48,kMotionSlots);
    edf::Put(lib,0x5C,0xFFFFFFu);
    edf::Put(lib,0x38,&stock);
    if(!SkipExhaustedMotionGrowth(lib))return false;
    stock.next=1;
    if(SkipExhaustedMotionGrowth(lib))return false;
    stock.next=0;
    edf::Put(lib,0x48,kMotionSlots-1);
    if(SkipExhaustedMotionGrowth(lib))return false;
    edf::Put(lib,0x48,kMotionSlots);
    edf::Put(lib,0x38,reinterpret_cast<std::uintptr_t>(&stock)|1u);
    if(SkipExhaustedMotionGrowth(lib))return false;
    edf::Put(lib,0x38,&stock);
    std::uint16_t id=0;
    if(add(lib,&id,key)!=&id || id!=0xFFFF || edf::At<const void*>(lib,0x38)!=&stock)return false;

    const void* const alternateTable[]{nullptr,nullptr,nullptr,reinterpret_cast<void*>(&TestMotionGrowth)};
    TestMotionGrowthListener alternate{alternateTable,0,0};
    stock.next=reinterpret_cast<std::uintptr_t>(&alternate);
    if(SkipExhaustedMotionGrowth(lib))return false;
    if(add(lib,&id,key)!=&id || id!=20 || alternate.calls!=1 || edf::At<const void*>(lib,0x38)!=&stock)return false;
    const auto* const rows=edf::At<const unsigned char*>(lib,0x40);
    if(std::memcmp(rows+20*kMotionEntrySize,key,kMotionEntrySize)!=0)return false;

    // On a private test mapping, compare the stock notification with the installed gate using the same cap.
    stock.next=0;
    edf::Put(lib,0x5C,0xFFFFFFu);
    auto* const site=image+kMotionGrowSignal;
    unsigned char installed[sizeof(kMotionGrowStock)];
    std::memcpy(installed,site,sizeof(installed));
    DWORD protection=0;
    if(!VirtualProtect(site,sizeof(installed),PAGE_EXECUTE_READWRITE,&protection))return false;
    const auto measure=[&](const unsigned char* code) {
        std::memcpy(site,code,sizeof(installed));
        FlushInstructionCache(GetCurrentProcess(),site,sizeof(installed));
        return Time([&](unsigned){add(lib,&id,key);});
    };
    std::array<double,5> before{},after{};
    for(unsigned i=0;i<before.size();++i) {
        if(i&1){after[i]=measure(installed);before[i]=measure(kMotionGrowStock);}
        else {before[i]=measure(kMotionGrowStock);after[i]=measure(installed);}
    }
    std::memcpy(site,installed,sizeof(installed));
    FlushInstructionCache(GetCurrentProcess(),site,sizeof(installed));
    DWORD discarded=0;
    if(!VirtualProtect(site,sizeof(installed),protection,&discarded))return false;
    std::sort(before.begin(),before.end());
    std::sort(after.begin(),after.end());
    std::printf("500,000 exhausted motion growth checks: %.3f -> %.3f ms (median of five runs)\n",
                before[2],after[2]);
    return id==0xFFFF && edf::At<const void*>(lib,0x38)==&stock;
}



bool MotionLibraryCases(bool bounded) {
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
    if(!finds(0,key,0xFFFFu) || !finds(-1,key,0xFFFFu))return false;
    for(unsigned field=1;field<key.size();++field) {
        rows[0]=key;
        rows[0][field]^=1;
        rows[31]=rows[32]=rows[35]=rows[38]=rows[40]=key;
        if(!finds(41,key,31))return false;
        rows[31][field]^=1;
        if(!finds(41,key,32))return false;
        rows[32][field]^=1;
        rows[35][field]^=1;
        rows[38][field]^=1;
        if(!finds(41,key,40) || !finds(40,key,0xFFFFu))return false;
        rows[40][field]^=1;
    }
    rows[65566]=key;
    if(!finds(large,key,bounded ? 0xFFFFu : 30) || !finds(65566,key,0xFFFFu))return false;
    rows[65566][27]^=1;
    if(!finds(large,key,0xFFFFu))return false;

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
        if(!bounded)return true;

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
               finds(kMotionSlots,exclusive,65534u) &&
               MotionGrowthGateCases(library,reinterpret_cast<const unsigned char*>(exclusive.data()));

    };
    const bool valid=verify();
    edf::Put(image,memcpySlot,copy);
    edf::Put(image,memsetSlot,fill);
    DWORD old=0;
    return VirtualProtect(imports,24,protection,&old) && valid;
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



bool ContactManifolds() {
    alignas(16) unsigned char world[0x8A0]{},bodies[4*0xB0]{},motions[4*0x80]{};
    alignas(16) unsigned char event[0xB0]{},cache[0x40]{},compressed[0x240]{},lookup[0x60]{};
    alignas(16) unsigned char expected[0x80],actual[0x80];
    const void* const table=image+kContactWorldVtable;
    edf::Put(world,0x20,table);
    edf::Put(world,0x38,bodies+0);
    edf::Put(world,0x40,4u);
    edf::Put(world,0x178,motions+0x80);
    edf::Put(world,0x540+0x160,1.0f);
    for(unsigned i=0;i<4;++i) {
        edf::Put(world,0x540+0x40+i*4,0.25f);
        edf::Put(world,0x540+0x70+i*4,0.125f);
    }
    for(unsigned i=0;i<4;++i) {
        edf::Put(bodies,i*0xB0+0x50,0x80000000u+i);
        edf::Put(bodies,i*0xB0+0x80,static_cast<std::int32_t>(i)-1);
        auto* const motion=motions+i*0x80;
        edf::Put(motion,0x0C,1.0f);
        edf::Put(motion,0x10,1.25+static_cast<double>(i));
        edf::Put(motion,0x18,-1.0-static_cast<double>(i));
        edf::Put(motion,0x20,0.5*static_cast<double>(i));
        edf::Put(motion,0x4C,i&1u);
    }
    edf::Put(event,0x20,cache+0);
    edf::Put(cache,0x20,compressed+0);
    edf::Put(cache,0x28,static_cast<std::uint16_t>(0x10));
    auto* const packed=compressed+0x10;
    for(unsigned i=0;i<4;++i)edf::Put(packed,0x10+4*i,0.25f*static_cast<float>(i+1));

    const auto stock=reinterpret_cast<DecodeContactFn>(image+kDecodeContact);
    const auto compare=[&](DecodeContactFn fast,const void* result) {
        std::memset(expected,0xA5,sizeof(expected));
        std::memset(actual,0xA5,sizeof(actual));
        const auto* const before=stock(event,world,expected);
        const auto* const after=fast(event,world,actual);
        return before==(result==actual ? expected : result) && after==result &&
               std::memcmp(expected,actual,sizeof(actual))==0;
    };
    std::mt19937 rng(20261127);
    for(const auto site:kContactDecoders) {
        const auto fast=Redirected<DecodeContactFn>(site);
        if(fast==stock)return false;
        edf::Put(event,0x30,7u);
        if(!compare(fast,event+0x30) || fast(event,nullptr,actual)!=event+0x30)return false;
        edf::Put(event,0x30,0xFFFFFFFFu);
        for(unsigned i=0;i<6000;++i) {
            const unsigned a=rng()%4,b=rng()%4;
            const auto aId=0x80000000u+a,bId=0x80000000u+b;
            edf::Put(event,8,aId);
            edf::Put(event,0xC,bId);
            for(unsigned j=0;j<4;++j)bodies[j*0xB0+0x54]=static_cast<unsigned char>(j&1u ? 1 : 2);
            world[0x898]=static_cast<unsigned char>((i%4)*8);
            packed[3]=static_cast<unsigned char>(1+rng()%4);
            packed[4]=static_cast<unsigned char>(i&1u ? 8 : 0);
            edf::Put(packed,0x20,i&2u ? lookup+0 : nullptr);
            for(unsigned j=0;j<8;++j)edf::Put(packed,0x30+2*j,static_cast<std::uint16_t>(rng()));
            const void* result=nullptr;
            switch(i%12) {
                case 0: edf::Put(event,0xC,0xFFFFFFFFu); break;
                case 1: edf::Put(event,8,0x00FFFFFFu); break;
                case 2: edf::Put(event,0xC,0x80000004u); break;
                case 3: edf::Put(event,8,a); break;
                case 4: bodies[b*0xB0+0x54]=4; break;
                case 5: bodies[a*0xB0+0x54]=0; break;
                default: result=actual; break;
            }
            if(!compare(fast,result)) {
                std::printf("FAIL: contact decoder %x, case %u\n",site,i);
                return false;
            }
        }
        edf::Put(event,8,0x80000001u);
        edf::Put(event,0xC,0x80000003u);
        bodies[0xB0+0x54]=2;
        bodies[3*0xB0+0x54]=1;
        const void* alternate[32];
        std::memcpy(alternate,table,sizeof(alternate));
        edf::Put(world,0x20,alternate+0);
        if(!compare(fast,actual))return false;
        alternate[0x68/sizeof(void*)]=reinterpret_cast<const void*>(+[](const void*,std::uint32_t) {return 0u;});
        if(!compare(fast,nullptr))return false;
        edf::Put(world,0x20,table);
    }

    packed[4]=0;
    edf::Put(packed,0x20,lookup+0);
    world[0x898]=0;
    const auto fast=Redirected<DecodeContactFn>(kContactDecoders[0]);
    for(unsigned points:{1u,4u}) {
        packed[3]=static_cast<unsigned char>(points);
        char label[70];
        std::snprintf(label,sizeof(label),"contact manifold decode with %u point(s)",points);
        Bench(label,[&](unsigned){stock(event,world,expected);},[&](unsigned){fast(event,world,actual);});
    }
    return true;
}


bool ContactStreams() {
    void* callbacks[4]={nullptr,nullptr,nullptr,reinterpret_cast<void*>(&TestCommand)};
    void* dispatcher[7]{};
    StreamResults state{};
    StreamHandler first{callbacks,&state,4},second{callbacks,&state,5};
    dispatcher[5]=&first;
    dispatcher[6]=&second;
    const auto stock=reinterpret_cast<ContactStreamFn>(image+kStockStream);
    const auto fast=Redirected<ContactStreamFn>(kStreamCall);
    if(image[kStreamCall]!=0xE8)return false;

    for(bool emptyNext:{false,true}) {
        StreamResults results[2]{};
        StreamBlock output[2]{};
        StreamWriter writers[2]={{nullptr,nullptr,&output[0]},{nullptr,nullptr,&output[1]}};
        for(unsigned version=0;version<2;++version) {
            state={};
            StreamBlock a{},b{},empty{};
            a.records=3;
            b.records=4;
            a.next=emptyNext ? &empty : &b;
            empty.next=&b;
            WriteRecord(a,0,16,1,4,10);
            WriteRecord(a,16,32,3,5,11);
            WriteRecord(a,32,16,0,0,12);
            WriteRecord(b,0,32,1,4,20);
            WriteRecord(b,32,16,0,0,21);
            WriteRecord(b,48,16,1,5,22);
            WriteRecord(b,64,16,1,4,23);
            const ContactRange late{reinterpret_cast<unsigned char*>(&b),64,1,1,nullptr};
            const ContactRange missing{reinterpret_cast<unsigned char*>(&b),0,0,3,&late};
            const ContactRange start{reinterpret_cast<unsigned char*>(&a),0,3,6,&missing};
            perfprobe::traceCommands=version!=0;
            std::fill(std::begin(perfprobe::tracedCommands),std::end(perfprobe::tracedCommands),0u);
            (version ? fast : stock)(nullptr,dispatcher,&start,&writers[version]);
            results[version]=state;
        }
        perfprobe::traceCommands=false;
        const unsigned count=emptyNext ? 3 : 5;
        if(results[0].digest!=results[1].digest || results[0].calls!=count || results[1].calls!=count ||
           writers[0].count!=(emptyNext ? 1 : 2) || writers[0].count!=writers[1].count ||
           writers[0].size!=writers[1].size || std::memcmp(output[0].payload,output[1].payload,sizeof(output[0].payload))!=0 ||
           perfprobe::tracedCommands[4]!=(emptyNext ? 2u : 3u) || perfprobe::tracedCommands[5]!=(emptyNext ? 1u : 2u))
            return false;
    }

    state={};
    constexpr unsigned blocks=70,perBlock=56,repeats=1000;
    auto* chain=new StreamBlock[blocks]{};
    for(unsigned block=0;block<blocks;++block) {
        chain[block].records=perBlock;
        if(block+1<blocks)chain[block].next=&chain[block+1];
        for(unsigned j=0;j<perBlock;++j)WriteRecord(chain[block],j*64,64,1,4,block*perBlock+j);
    }
    const ContactRange range{reinterpret_cast<unsigned char*>(chain),0,perBlock,blocks*perBlock,nullptr};
    std::array<double,5> oldTimes{},newTimes{};
    const auto time=[&](ContactStreamFn run) {
        const auto before=std::chrono::steady_clock::now();
        for(unsigned i=0;i<repeats;++i)run(nullptr,dispatcher,&range,nullptr);
        return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-before).count()/
            (static_cast<double>(repeats)*blocks*perBlock);
    };
    for(unsigned trial=0;trial<oldTimes.size();++trial) {
        if(trial%2){newTimes[trial]=time(fast);oldTimes[trial]=time(stock);}
        else {oldTimes[trial]=time(stock);newTimes[trial]=time(fast);}
    }
    std::sort(oldTimes.begin(),oldTimes.end());
    std::sort(newTimes.begin(),newTimes.end());
    std::printf("contact command stream: %.3f -> %.3f ns/record, %u records per batch\n",
                oldTimes[2],newTimes[2],blocks*perBlock);
    delete[] chain;
    return state.calls==repeats*blocks*perBlock*oldTimes.size()*2;
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
    if(nt->FileHeader.TimeDateStamp!=0x678CCB46 || !edf::Matches(image,kBoxMerge,kBoxMergeStock,sizeof(kBoxMergeStock)))return 1;
    if(!MotionLibraryCases(false)) {
        std::puts("FAIL: stock motion library behavior does not match the fixture");
        return 1;
    }
    auto* const harness=BoxHarness();
    if(!harness || InstallNativeBodyMath()!=kNativeBodyMathSites || image[kBoxMergeCall]!=0xE8 ||
       image[kBoxMerge]!=0x48 || image[kBoxMerge+2]!=0x4D || !CaptureBoxArguments()) {
        std::puts("FAIL: native attachment math call sites or signatures do not match");
        return 1;
    }
    const auto runBox=reinterpret_cast<BoxRun>(harness);
    const auto* const oldBox=harness+kCopiedBox;
    const auto* const packedBox=harness+kPackedBox;
    const auto* const newBox=image+kBoxMerge;
    const auto oldBasis=reinterpret_cast<BasisFn>(image+kBasis);
    const BasisFn newBases[]={Redirected<BasisFn>(0x6ED9EB),Redirected<BasisFn>(0x6EDBF4)};
    std::mt19937 rng(20261126);
    std::uniform_real_distribution<float> number(-32.0f,32.0f);
    std::array<Matrix,64> matrices{};
    std::array<Box,64> boxes{};
    constexpr int samples=20000;
    const unsigned mxcsr=_mm_getcsr();
    for(int i=0;i<samples;++i) {
        if(i==samples/2)_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
        Matrix matrix{};
        Box source{};
        for(float& v:matrix.data)v=number(rng);
        for(float& v:source.data)v=number(rng);
        if(i%8==0)matrix.data[0]=matrix.data[1]=matrix.data[2]=-0.0f;
        if(i%8==1)std::copy_n(matrix.data,3,matrix.data+8);
        if(i%8==2)matrix.data[3]=std::numeric_limits<float>::quiet_NaN();  // padding is left exactly as it was
        if(i%8==3)matrix.data[0]=FLT_MAX;                                  // stock normalization fallback on overflow
        if(i%8==4)source.data[6]=-0.0f;
        if(i%8==5)matrix.data[2]=std::numeric_limits<float>::denorm_min();
        Proxy expectedProxy{{},source,{},matrix},actualProxy=expectedProxy;
        MergeFrame expectedFrame{};
        for(float& v:expectedFrame.bounds.data)v=number(rng);
        if(i%8==6)expectedFrame.bounds.data[0]=expectedFrame.bounds.data[6]=std::numeric_limits<float>::quiet_NaN();
        auto actualFrame=expectedFrame;
        runBox(&expectedFrame,&expectedProxy,oldBox);
        runBox(&actualFrame,&actualProxy,newBox);
        if(std::memcmp(&expectedProxy,&actualProxy,sizeof(Proxy))!=0 ||
           std::memcmp(&expectedFrame.bounds,&actualFrame.bounds,sizeof(Box))!=0 ||
           !ArgumentsMatch(expectedFrame,expectedProxy) || !ArgumentsMatch(actualFrame,actualProxy)) {
            std::printf("FAIL: transformed and merged box or its continuation arguments, case %d\n",i);return 1;
        }
        auto expectedBasis=matrix;
        oldBasis(expectedBasis.data);
        for(const auto newBasis:newBases) {
            auto actualBasis=matrix;
            newBasis(actualBasis.data);
            if(std::memcmp(&expectedBasis,&actualBasis,sizeof(Matrix))!=0) {
                std::printf("FAIL: normalized basis case %d\n",i);return 1;
            }
        }
        if(i%8==7) {
            const auto slot=static_cast<std::size_t>(i/8)%matrices.size();
            matrices[slot]=matrix;
            boxes[slot]=source;
        }
    }
    _mm_setcsr(mxcsr);
    std::printf("native body math: %d byte-identical outputs per call site\n",samples);
    auto oldMatrices=matrices,newMatrices=matrices;
    std::array<Proxy,64> oldProxies{},packedProxies{},newProxies{};
    for(std::size_t i=0;i<oldProxies.size();++i) {
        oldProxies[i].source=boxes[i];
        oldProxies[i].matrix=matrices[i];
    }
    newProxies=packedProxies=oldProxies;
    MergeFrame before{},packed{},after{};
    Bench("box transforms and unions",[&](unsigned i){runBox(&before,&oldProxies[i],oldBox);},
          [&](unsigned i){runBox(&after,&newProxies[i],newBox);});
    Bench("packed transforms with separate vs fused unions",[&](unsigned i){runBox(&packed,&packedProxies[i],packedBox);},
          [&](unsigned i){runBox(&after,&newProxies[i],newBox);});
    Bench("basis normalizations",[&](unsigned i){oldBasis(oldMatrices[i].data);},
          [&](unsigned i){newBases[1](newMatrices[i].data);});
    if(std::memcmp(&before.bounds,&after.bounds,sizeof(Box))!=0 ||
       std::memcmp(&packed.bounds,&after.bounds,sizeof(Box))!=0 ||
       std::memcmp(oldProxies.data(),newProxies.data(),sizeof(oldProxies))!=0 ||
       std::memcmp(packedProxies.data(),newProxies.data(),sizeof(packedProxies))!=0 ||
       std::memcmp(oldMatrices.data(),newMatrices.data(),sizeof(oldMatrices))!=0) {
        std::puts("FAIL: repeated updates changed the outputs");return 1;
    }
    if(!RayResetTemplates()) {
        std::puts("FAIL: ray collector reset template does not reproduce the native state");
        return 1;
    }
    if(!BoundsValidation() || !AvoidanceNeighbors())return 1;
    if(!AvoidanceBoxes()) {
        std::puts("FAIL: avoidance sweep changed the first overlap or the X cutoff");
        return 1;
    }
    if(!MotionPresetWeakCache()) {
        std::puts("FAIL: exhausted motion preset wrapper lifetime mismatch");
        return 1;
    }
    if(!MotionLibraryCases(true)) {
        std::puts("FAIL: motion library ID order, miss or insertion mismatch");
        return 1;
    }
    if(!BodySignalDispatch()) {
        std::puts("FAIL: native empty or subscribed body signal dispatch mismatch");
        return 1;
    }
    if(!BodyQuaternionUpdates()) {
        std::puts("FAIL: native body quaternion fast path or fallback mismatch");
        return 1;
    }
    if(!ContactManifolds()) {
        std::puts("FAIL: native contact manifold return or output mismatch");
        return 1;
    }
    if(!ContactStreams()) {
        std::puts("FAIL: contact stream changed record dispatch, deferred bytes or command accounting");
        return 1;
    }
    VirtualFree(harness,0,MEM_RELEASE);
    return 0;
}
