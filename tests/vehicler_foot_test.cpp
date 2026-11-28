// The Vehicler on foot's pure part (src/vehicler_foot_logic.h): every site decodes, the gate rule (stock weapons as before, growth for
// a > 1, NaN skipped), the falloff mirror for the Gyrojet's AmmoDamageReduce, and the gate's code itself run on this machine (it is
// position independent: called with rdi = a fake core, its two ways out are stubs that say which one was taken).
#include "vehicler_foot_logic.h"
#include <Windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace vf=crew::vehicler_foot;
namespace {
int checks=0,failed=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok) { ++failed; std::printf("FAIL %s\n",what); }
}
bool Near(float a,float b) { return std::fabs(a-b)<=1e-3f*(std::fabs(b)+1.0f); }

// [wrapper][cave][stub skip][stub refresh]: the wrapper (push rdi; mov rdi,rcx; mov rax,cave; call rax; pop rdi; ret) gives the cave
// its rdi; each stub (mov eax,1|2; ret) returns to the wrapper.
using RunFn=int(*)(void*);
struct Runner {
    unsigned char* page=nullptr;
    RunFn run=nullptr;
    Runner() {
        page=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        if(!page)return;
        unsigned char* cave=page+64;
        unsigned char* skip=page+256;
        unsigned char* refresh=page+272;
        const unsigned char wrap[]={0x57,0x48,0x89,0xCF,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,0x5F,0xC3};
        std::memcpy(page,wrap,sizeof(wrap));
        const auto at=reinterpret_cast<std::uint64_t>(cave);
        std::memcpy(page+6,&at,8);
        unsigned char code[vf::kCaveSize];
        vf::BuildCave(code,reinterpret_cast<std::uint64_t>(skip),reinterpret_cast<std::uint64_t>(refresh));
        std::memcpy(cave,code,sizeof(code));
        const unsigned char one[]={0xB8,1,0,0,0,0xC3},two[]={0xB8,2,0,0,0,0xC3};
        std::memcpy(skip,one,sizeof(one));std::memcpy(refresh,two,sizeof(two));
        FlushInstructionCache(GetCurrentProcess(),page,4096);
        run=reinterpret_cast<RunFn>(page);
    }
    ~Runner() { if(page)VirtualFree(page,0,MEM_RELEASE); }
    // 2: refreshed (0x235E8C), 1: skipped (0x235E9C)
    int Gate(float a) const {
        std::vector<unsigned char> core(0xB00,0xCC);
        std::memcpy(core.data()+0xA10,&a,4);
        return run(core.data());
    }
};
}  // namespace

int main() {
    // the sites
    for(const vf::Print& p:vf::kPrints) {
        unsigned char b[vf::kMostPrintBytes];
        Check(vf::Decode(p.hex,b,sizeof(b))>0,p.what);
        Check(vf::Find(p.rva)==&p,"each site listed once");
    }
    {
        unsigned char b[vf::kMostPrintBytes];
        Check(vf::Decode(vf::Find(vf::kGate)->hex,b,sizeof(b))==vf::kGateSize,"the gate print is the 17 bytes replaced");
        Check(vf::kGate+vf::kGateSize==vf::kRefresh,"the refresh follows the gate");
        Check(b[15]==0x76 && vf::kRefresh+b[16]==vf::kSkip,"the stock jbe goes where the cave's skip does");
        Check(vf::Decode(vf::Find(0x179E1D8)->hex,b,sizeof(b))==4,"the 0.99f");
        float gate=0;std::memcpy(&gate,b,4);
        Check(gate==vf::kStockGate,"the cave's 0.99f is the game's");
        Check(vf::Decode("4g",b,sizeof(b))==0 && vf::Decode("abc",b,sizeof(b))==0,"malformed prints rejected");
    }
    // the dash: only the Vehicler
    Check(vf::DashHeldBack(4) && !vf::DashHeldBack(5) && !vf::DashHeldBack(-1) && !vf::DashHeldBack(0),"only type 4 loses the dash");

    // the gate rule
    const float nan=std::numeric_limits<float>::quiet_NaN();
    Check(!vf::Refreshes(1.0f),"[1, 1] (1617 stock weapons): no refresh, as stock");
    Check(vf::Refreshes(0.5f) && vf::Refreshes(0.0f) && vf::Refreshes(-1.0f),"a < 0.99: refreshed, as stock");
    Check(!vf::Refreshes(0.99f) && !vf::Refreshes(0.995f),"0.99 <= a <= 1: skipped, as stock");
    Check(vf::Refreshes(1.0001f) && vf::Refreshes(5.0f),"a > 1: refreshed (growth)");
    Check(!vf::Refreshes(nan),"NaN: skipped, as stock");

    // the falloff mirror: stock fall, the Gyrojet's growth [5, 2] from 40
    Check(Near(vf::Falloff(100,0.5f,1,0,60),100) && Near(vf::Falloff(100,0.5f,1,30,60),75) && Near(vf::Falloff(100,0.5f,1,60,60),50),
          "stock [0.5, 1] falls linearly to half");
    Check(Near(vf::Falloff(40,5,2,0,150),40),"Gyrojet point blank: 40");
    Check(Near(vf::Falloff(40,5,2,150,150),200),"Gyrojet at the end of its life: 200");
    Check(Near(vf::Falloff(40,5,2,300,150),200),"t clamped at 1");
    float last=0;bool rising=true;
    for(int age=0;age<=150;age+=10) { const float d=vf::Falloff(40,5,2,age,150);rising=rising && d>=last;last=d; }
    Check(rising,"Gyrojet damage never falls with flight");

    // the cave itself
    Runner r;
    Check(r.run!=nullptr,"executable page");
    if(r.run) {
        for(const float a:{1.0f,0.99f,0.995f,0.5f,0.0f,-2.0f,1.0001f,5.0f,nan,std::numeric_limits<float>::infinity()}) {
            char what[64];std::snprintf(what,sizeof(what),"cave for a=%g matches Refreshes",static_cast<double>(a));
            Check(r.Gate(a)==(vf::Refreshes(a) ? 2 : 1),what);
        }
    }
    std::printf("%d checks, %d failed\n",checks,failed);
    return failed ? 1 : 0;
}
