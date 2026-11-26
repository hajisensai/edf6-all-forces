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
using BoxFn=void(__fastcall*)(float*,const float*,const float*);
using BasisFn=void(__fastcall*)(float*);
struct alignas(16) Matrix { float data[16]; };
struct alignas(16) Box { float data[8]; };

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
    if(nt->FileHeader.TimeDateStamp!=0x678CCB46 || InstallNativeBodyMath()!=kNativeBodyMathSites) {
        std::puts("FAIL: native attachment math call sites or signatures do not match");
        return 1;
    }
    const auto oldBox=reinterpret_cast<BoxFn>(image+kTransformBox),newBox=Redirected<BoxFn>(kBoxCall);
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
        Box source{},expectedBox{},actualBox{};
        for(float& v:matrix.data)v=number(rng);
        for(float& v:source.data)v=number(rng);
        if(i%8==0)matrix.data[0]=matrix.data[1]=matrix.data[2]=-0.0f;
        if(i%8==1)std::copy_n(matrix.data,3,matrix.data+8);
        if(i%8==2)matrix.data[3]=std::numeric_limits<float>::quiet_NaN();  // padding is left exactly as it was
        if(i%8==3)matrix.data[0]=FLT_MAX;                                  // stock normalization fallback on overflow
        if(i%8==4)source.data[6]=-0.0f;
        if(i%8==5)matrix.data[2]=std::numeric_limits<float>::denorm_min();
        oldBox(expectedBox.data,source.data,matrix.data);
        newBox(actualBox.data,source.data,matrix.data);
        if(std::memcmp(&expectedBox,&actualBox,sizeof(Box))!=0) {
            std::printf("FAIL: transformed box case %d\n",i);return 1;
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
    Box a{},b{};
    Bench("box transforms",[&](unsigned i){oldBox(a.data,boxes[i].data,matrices[i].data);},
          [&](unsigned i){newBox(b.data,boxes[i].data,matrices[i].data);});
    Bench("basis normalizations",[&](unsigned i){oldBasis(oldMatrices[i].data);},
          [&](unsigned i){newBases[1](newMatrices[i].data);});
    if(std::memcmp(&a,&b,sizeof(Box))!=0 || std::memcmp(oldMatrices.data(),newMatrices.data(),sizeof(oldMatrices))!=0) {
        std::puts("FAIL: repeated updates changed the outputs");return 1;
    }
    return 0;
}
