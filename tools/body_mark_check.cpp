// BodyMark's test order (src/body506.cpp) offline, on real memory and the real Readable: the vtable read on one page
// first, the mark's 0x1630 bytes only for a 506, answers what the old order (all 0x1630 bytes, then the vtable) did --
// for a 506 and another class, wholly readable, cut short by a PAGE_NOACCESS page inside the mark's span, and with the
// head itself unreadable or null.
//   cmake --build build --target body_mark_check && build\body_mark_check.exe      (exit code 1 on a failure)
#include "edf/memory.h"
#include <cstdio>
#include <cstring>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,long long a=0,long long b=0) {
    ++cases;
    if(!ok){++failures;std::printf("FAIL: %s (%lld, %lld)\n",what,a,b);}
}
constexpr std::size_t kSpeedGain=0x162C;
unsigned char vtables[2][8];
const unsigned char* const heli506=vtables[0];   // stands for image+kHeli506
const unsigned char* const other=vtables[1];

float Old(const void* vehicle) noexcept {
    const auto v=static_cast<const unsigned char*>(vehicle);
    if(!edf::Readable(v,kSpeedGain+4) || *reinterpret_cast<const unsigned char* const*>(v)!=heli506)return 0.0f;
    float k;std::memcpy(&k,v+kSpeedGain,4);return k;
}
float New(const void* vehicle) noexcept {
    const auto v=static_cast<const unsigned char*>(vehicle);
    if(!edf::Readable(v,8) || *reinterpret_cast<const unsigned char* const*>(v)!=heli506 || !edf::Readable(v,kSpeedGain+4))
        return 0.0f;
    float k;std::memcpy(&k,v+kSpeedGain,4);return k;
}
}  // namespace

int main() {
    // Four pages; the object placed at every 0x10 step of the first two, its class one or the other; the third page
    // readable or not (a cut inside the mark's span for most placements, past it for the rest).
    auto* block=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!block){std::printf("VirtualAlloc failed\n");return 1;}
    int differ=0,marks=0,zeros=0,runs=0;
    for(int cut=0;cut<2;++cut) {
        DWORD old=0;
        VirtualProtect(block+0x2000,0x1000,cut ? PAGE_NOACCESS : PAGE_READWRITE,&old);
        // Readable caches regions per tick: wait the tick out so this protection is what it sees.
        const ULONGLONG t=GetTickCount64();
        while(GetTickCount64()==t){}
        for(std::size_t at=0;at<0x2000;at+=0x10)
            for(int cls=0;cls<2;++cls) {
                unsigned char* v=block+at;
                const unsigned char* vt=cls ? other : heli506;
                std::memcpy(v,&vt,8);
                const float mark=1.5f+static_cast<float>(at);
                if(!cut || at+kSpeedGain+4<=0x2000)std::memcpy(v+kSpeedGain,&mark,4);
                const float a=Old(v),b=New(v);
                differ+=std::memcmp(&a,&b,4)!=0;marks+=a!=0.0f;zeros+=a==0.0f;++runs;
            }
    }
    Check(differ==0,"placed objects: the same mark both ways",differ,runs);
    Check(marks>100 && zeros>100,"both answers drawn: a 506 wholly readable, and the rest",marks,zeros);
    // The head itself unreadable, and null.
    DWORD old=0;
    VirtualProtect(block+0x3000,0x1000,PAGE_NOACCESS,&old);
    const ULONGLONG t=GetTickCount64();
    while(GetTickCount64()==t){}
    Check(Old(block+0x3000)==0.0f && New(block+0x3000)==0.0f,"an unreadable head: 0 both ways");
    Check(Old(nullptr)==0.0f && New(nullptr)==0.0f,"null: 0 both ways");
    VirtualFree(block,0,MEM_RELEASE);
    std::printf("  %d placements\nbody_mark_check: %d cases, %d failures\n",runs,cases,failures);
    return failures ? 1 : 0;
}
