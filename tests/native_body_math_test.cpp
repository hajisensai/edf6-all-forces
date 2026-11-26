// Compare the audited attachment call sites with the original math on a private EDF.dll mapping.
#include "../src/native_body_math.cpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <limits>
#include <random>

namespace crew { unsigned char* image=nullptr; }

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
    unsigned char continuation[sizeof(kCapture)];
    std::memcpy(continuation,image+kBoxMergeEnd,sizeof(continuation));
    if(!edf::PatchCode(image+kBoxMergeEnd,continuation,kCapture,sizeof(kCapture))) {
        VirtualFree(mem,0,MEM_RELEASE);return nullptr;
    }
    return mem;
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
    auto* const harness=BoxHarness();
    if(!harness || InstallNativeBodyMath()!=kNativeBodyMathSites || image[kBoxMergeCall]!=0xE8 ||
       image[kBoxMerge]!=0x48 || image[kBoxMerge+2]!=0x4D) {
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
    VirtualFree(harness,0,MEM_RELEASE);
    return 0;
}
