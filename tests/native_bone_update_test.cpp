// The real and reimplemented subtree traversal, using private native DLL mapping and independent skeleton copies.
#include "../src/native_bone_update.cpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

namespace crew { unsigned char* image=nullptr; }

namespace {
using namespace crew;
using namespace crew::native_bones;
struct Record { unsigned char bytes[kStride]{}; };
struct Skeleton {
    unsigned char raw[0x28]{};
    explicit Skeleton(std::vector<Record>& bones) noexcept {
        edf::Put<unsigned char*>(raw,kRecords,bones.front().bytes);
        edf::Put<std::uint64_t>(raw,kCount,bones.size());
    }
};

std::vector<Record> MakeBones(int count,std::mt19937& rng) {
    std::vector<Record> result(static_cast<std::size_t>(count));
    std::uniform_real_distribution<float> number(-1.25f,1.25f);
    for(int i=0;i<count;++i) {
        auto* const bone=result[static_cast<std::size_t>(i)].bytes;
        bone[kOwnedLocally]=static_cast<unsigned char>((i%7==0) ? 2 : (i%5==0) ? 0 : 1);
        edf::Put<int>(bone,kParent,i==0 ? -1 : (i-1)/3);
        for(unsigned element=0;element<16;++element) {
            const float value=element%7==0 ? -0.0f : number(rng);
            edf::Put<float>(bone,kLocal+element*4,value);
            edf::Put<float>(bone,kWorld+element*4,number(rng));
        }
    }
    return result;
}

bool SameBranch(UpdateFn stock,UpdateFn candidate,std::vector<Record> fixture,int root,int first,int end) {
    auto* const node=fixture[static_cast<std::size_t>(root)].bytes;
    edf::Put<int>(node,kFirstChild,first);
    edf::Put<int>(node,kAfterBranch,end);
    auto changed=fixture;
    Skeleton a(fixture),b(changed);
    stock(a.raw,root);
    candidate(b.raw,root);
    return std::memcmp(fixture.data(),changed.data(),fixture.size()*sizeof(Record))==0;
}

double Bench(UpdateFn update,std::vector<Record>& fixture) {
    Skeleton skeleton(fixture);
    const auto first=std::chrono::steady_clock::now();
    for(int i=0;i<100000;++i)update(skeleton.raw,0);
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-first).count();
}
}

int main(int argc,char** argv) {
    if(!AvxAvailable()) {
        std::puts("SKIP: the optimized path requires OS-enabled AVX");
        return 77;
    }
    if(argc<2 || !argv[1][0] || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: the native comparison requires the Steam EDF.dll");
        return 77;
    }
    image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    if(!image)return 1;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
    if(nt->FileHeader.TimeDateStamp!=0x678CCB46 || InstallNativeBoneUpdate()!=std::size(kSites)) {
        std::puts("FAIL: native kernel or one of its three call sites does not match");
        return 1;
    }
    const auto stock=reinterpret_cast<UpdateFn>(image+kUpdateBranch);
    std::array<UpdateFn,std::size(kSites)> redirects{};
    for(std::size_t i=0;i<redirects.size();++i) {
        const auto* site=image+kSites[i];
        if(site[0]!=(i==0 ? 0xE8 : 0xE9))return 1;
        redirects[i]=reinterpret_cast<UpdateFn>(site+5+edf::At<std::int32_t>(site,1));
    }
    std::mt19937 rng(20261126);
    int compared=0;
    for(const auto update:redirects) {
        for(int i=0;i<300;++i) {
            const int count=3+static_cast<int>(rng()%253);
            const auto bones=MakeBones(count,rng);
            const int start=2+static_cast<int>(rng()%static_cast<unsigned>(count-2));
            if(!SameBranch(stock,update,bones,0,1,-1) || !SameBranch(stock,update,bones,1,start,count-1) ||
               !SameBranch(stock,update,bones,0,-1,-1) || !SameBranch(stock,update,bones,0,start,start)) {
                std::printf("FAIL: subtree comparison (case %d, %d bones)\n",i,count);
                return 1;
            }
            compared+=4;
        }
    }
    auto fixture=MakeBones(192,rng);
    edf::Put<int>(fixture[0].bytes,kFirstChild,1);
    edf::Put<int>(fixture[0].bytes,kAfterBranch,-1);
    auto packed=fixture;
    std::array<double,5> original{},updated{};
    for(std::size_t i=0;i<original.size();++i) {
        if(i%2) {
            updated[i]=Bench(redirects[0],packed);
            original[i]=Bench(stock,fixture);
        } else {
            original[i]=Bench(stock,fixture);
            updated[i]=Bench(redirects[0],packed);
        }
    }
    if(std::memcmp(fixture.data(),packed.data(),fixture.size()*sizeof(Record))!=0)return 1;
    std::sort(original.begin(),original.end());
    std::sort(updated.begin(),updated.end());
    std::printf("native bone update: %d byte-identical subtrees across three native call sites\n",compared);
    std::printf("100,000 updates of a 192-bone tree: %.2f -> %.2f ms (median of five runs)\n",original[2],updated[2]);
    return 0;
}
