// Execute the exact supported native LookTo against the production controller-owned camera frame.
// Private DONT_RESOLVE image; only four verified math imports are connected. No DllMain/game process/file writes.
#include <Windows.h>
#include "../src/fp_view.h"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace {
float NativeAtan2(float y,float x) { return std::atan2(y,x); }
float NativeSqrt(float x) { return std::sqrt(x); }
float NativeSin(float x) { return std::sin(x); }
float NativeCos(float x) { return std::cos(x); }
bool Connect(unsigned char* image,std::size_t offset,void* target) {
    DWORD old=0;
    if(!VirtualProtect(image+offset,sizeof(target),PAGE_READWRITE,&old))return false;
    std::memcpy(image+offset,&target,sizeof(target));
    DWORD ignored=0;return VirtualProtect(image+offset,sizeof(target),old,&ignored)!=0;
}
bool Profile(const unsigned char* image) {
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
       nt->FileHeader.TimeDateStamp!=0x678CCB46 || nt->OptionalHeader.SizeOfImage!=0x22CE000)return false;
    struct Import { unsigned thunk,slot; };
    constexpr Import imports[]={{0x12DA90C,0x1756450},{0x12DA8BE,0x1756438},{0x12DA8B8,0x1756408},{0x12DA8B2,0x1756400}};
    for(const auto& entry:imports) {
        if(image[entry.thunk]!=0xFF || image[entry.thunk+1]!=0x25)return false;
        std::int32_t displacement=0;std::memcpy(&displacement,image+entry.thunk+2,4);
        if(static_cast<std::int64_t>(entry.thunk)+6+displacement!=entry.slot)return false;
    }
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned i=0;i<0x226;++i)hash=(hash^image[0x4E220+i])*1099511628211ull;
    return hash==0x6645D8F448087943ull;
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2 || !argv[1][0] || GetFileAttributesW(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: supported EDF.dll required");return 77;
    }
    HMODULE module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!module){std::puts("FAIL: private native image mapping");return 1;}
    auto image=reinterpret_cast<unsigned char*>(module);
    if(!Profile(image) ||
       !Connect(image,0x1756450,reinterpret_cast<void*>(&NativeAtan2)) ||
       !Connect(image,0x1756438,reinterpret_cast<void*>(&NativeSqrt)) ||
       !Connect(image,0x1756408,reinterpret_cast<void*>(&NativeSin)) ||
       !Connect(image,0x1756400,reinterpret_cast<void*>(&NativeCos))) {
        std::puts("FAIL: native profile or math boundary");FreeLibrary(module);return 1;
    }
    using NativeFn=float*(__fastcall*)(float*,const float*);
    const auto lookTo=reinterpret_cast<NativeFn>(image+0x4E220);
    int checks=0,failed=0;
    const float eye[3]={17,23,-31};
    for(float yaw:{-3.0f,-2.0f,-1.0f,0.0f,.4f,1.0f,2.0f,3.0f})for(float pitch:{-1.2f,-.4f,0.0f,.4f,1.2f}) {
        const auto controlled=crew::fpview::HeadingFrame(yaw,pitch,eye);
        alignas(16) float direction[4]={controlled.m[8],controlled.m[9],controlled.m[10],0};
        alignas(16) float native[16]{};
        lookTo(native,direction);
        float error=0;
        for(int i=0;i<12;++i)error=std::fmax(error,std::fabs(native[i]-controlled.m[i]));
        ++checks;if(error>2e-5f){++failed;std::printf("FAIL native basis yaw=%g pitch=%g error=%g\n",yaw,pitch,error);}
        float target[3];for(int i=0;i<3;++i)target[i]=eye[i]+direction[i]*100;
        crew::fpview::M4 enemy;
        const bool built=crew::fpview::LookFrame(eye,target,&enemy);
        error=0;for(int i=0;i<12;++i)error=std::fmax(error,std::fabs(native[i]-enemy.m[i]));
        ++checks;if(!built || error>2e-5f || std::memcmp(enemy.m+12,eye,12)!=0)++failed;
    }
    FreeLibrary(module);
    std::printf("camera frame native: %d checks, %d failed\n",checks,failed);
    return failed ? 1 : 0;
}
