#include "native_bone_update.h"
#include "native_bone_range.h"
#include "crew.h"
#include "edf/memory.h"
#include <climits>
#include <immintrin.h>
#include <intrin.h>

namespace crew {
namespace {
using namespace native_bones;
constexpr unsigned kUpdateBranch=0x1100010,kBranchLoop=0x1100080,kBranchAdvance=0x11001B8;
constexpr unsigned kSites[]={0x110053B,0x1100BC8,0x1100BDA};
constexpr unsigned char kUpdateSig[]={0x48,0x83,0xEC,0x48,0x4C,0x8B,0x51,0x10,0x4C,0x8B,0xD9,
    0x48,0x63,0xC2,0x48,0x69,0xC8,0x10,0x01,0x00,0x00,0x4A,0x63,0x44,0x11,0x14};
constexpr unsigned char kLoopSig[]={0x80,0x7A,0x80,0x01,0x0F,0x85,0x2E,0x01,0x00,0x00,0x48,0x63,0x42,0x88};
constexpr unsigned char kAdvanceSig[]={0x49,0x81,0xC0,0x10,0x01,0x00,0x00,0x48,0x81,0xC2,0x10,0x01,0x00,0x00};
constexpr unsigned kRecords=0x10,kCount=0x20,kFirstChild=0x14,kAfterBranch=0x20;
using UpdateFn=void(__fastcall*)(unsigned char*,int);

bool AvxAvailable() noexcept {
    int registers[4]{};
    __cpuid(registers,0);
    if(registers[0]<1)return false;
    __cpuid(registers,1);
    constexpr int kAvxAndOsxsave=(1<<28)|(1<<27);
    return (registers[2]&kAvxAndOsxsave)==kAvxAndOsxsave && (_xgetbv(0)&6)==6;
}

bool TryUpdateBranch(unsigned char* skeleton,int root) noexcept {
    if(!skeleton || root<0)return false;
    const auto count=edf::At<std::uint64_t>(skeleton,kCount);
    auto* const records=edf::At<unsigned char*>(skeleton,kRecords);
    if(!records || count>INT_MAX || static_cast<unsigned>(root)>=count)return false;
    const auto* base=records+static_cast<std::ptrdiff_t>(root)*kStride;
    const int begin=edf::At<int>(base,kFirstChild);
    if(begin==-1)return true;
    const int stop=edf::At<int>(base,kAfterBranch);
    const int end=stop==-1 ? static_cast<int>(count) : stop;
    if(begin<0 || end<begin || static_cast<unsigned>(end)>count)return false;
    if(begin!=end)ApplyAvx(records,begin,end);
    return true;
}

void __fastcall UpdateBranch(unsigned char* skeleton,int root) noexcept {
    if(!TryUpdateBranch(skeleton,root))reinterpret_cast<UpdateFn>(image+kUpdateBranch)(skeleton,root);
}
}

unsigned InstallNativeBoneUpdate() noexcept {
    if(!image || !AvxAvailable() || !edf::Matches(image,kUpdateBranch,kUpdateSig,sizeof(kUpdateSig)) ||
       !edf::Matches(image,kBranchLoop,kLoopSig,sizeof(kLoopSig)) ||
       !edf::Matches(image,kBranchAdvance,kAdvanceSig,sizeof(kAdvanceSig)))return 0;
    unsigned installed=0;
    for(const auto site:kSites) {
        bool changed=false;
        edf::RedirectCall(image+site,image+kUpdateBranch,reinterpret_cast<void*>(&UpdateBranch),changed);
        installed+=changed;
    }
    return installed;
}
}
