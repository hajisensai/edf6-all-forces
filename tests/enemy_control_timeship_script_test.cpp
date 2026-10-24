#include "../src/enemy_control_timeship_script.cpp"
#include <cstdio>
#include <cstdlib>
namespace crew {unsigned char* image=nullptr;}
namespace {
using namespace crew;using namespace crew::enemy_native;
int checks=0,failed=0,calls=0;bool online=false,controlled=false,role=false,authority=false,quiescing=false,contextKnown=true;int lastFrames=0;
void Check(bool yes,const char* label,int line){++checks;if(!yes){++failed;std::printf("%d: %s\n",line,label);}}
#define CHECK(x) Check((x),#x,__LINE__)
void Jump(unsigned char* at,const void* fn){at[0]=0x48;at[1]=0xB8;std::memcpy(at+2,&fn,8);at[10]=0xFF;at[11]=0xE0;}
void __fastcall FakeWarp(void*,const void*,int frames){++calls;lastFrames=frames;}
void __fastcall FakeTarget(void*,const void*){++calls;}
void __fastcall FakeVoid(void*){++calls;}
void Image(){
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2000000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!image)std::exit(2);
    for(const auto& site:timeship_script::sites){
        const unsigned char before[]={0x48,0x83,0xEC,0x28},after[]={0x48,0x83,0xC4,0x28,0xC3};
        std::memcpy(image+site.rva-4,before,4);std::memcpy(image+site.rva+5,after,5);image[site.rva]=0xE8;
        const auto relative=static_cast<std::int32_t>(site.target-site.rva-5);std::memcpy(image+site.rva+1,&relative,4);
        const void* fake=site.target==0x3F3040 || site.target==0x3F3160 ? reinterpret_cast<const void*>(&FakeWarp) : site.target==0x3ECA80 ? reinterpret_cast<const void*>(&FakeTarget) : reinterpret_cast<const void*>(&FakeVoid);
        Jump(image+site.target,fake);
    }
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
}
void InvokeAll(){
    for(const auto& site:timeship_script::sites){
        void* actor=reinterpret_cast<void*>(0x1234);alignas(16) float matrix[16]{};
        if(site.target==0x3F3040 || site.target==0x3F3160)reinterpret_cast<timeship_script::WarpFn>(image+site.rva-4)(actor,matrix,600);
        else if(site.target==0x3ECA80)reinterpret_cast<timeship_script::TargetFn>(image+site.rva-4)(actor,matrix);
        else reinterpret_cast<timeship_script::VoidFn>(image+site.rva-4)(actor);
    }
}
}
namespace crew {
bool InSession() noexcept{return online;}
bool EnemyControlNetworkContext(const void*,bool* out) noexcept{if(!controlled || !contextKnown || !out)return false;*out=online;return true;}
bool EnemyControlTargetActive(const void*) noexcept{return controlled;}
}
namespace crew::enemy_native {
bool NativeActionQuiescing(const void*) noexcept{return quiescing;}
bool NativeActionRole(const void*,bool* host) noexcept{if(host)*host=authority;return role;}
}
int main(){
    Image();auto& first=timeship_script::sites[0];const auto saved=At<std::int32_t>(image,first.rva+1);
    Put<std::int32_t>(image,first.rva+1,saved+1);
    CHECK(!InstallTimeShipScriptGates());
    for(const auto& site:timeship_script::sites)CHECK(!site.changed && !site.thunk);
    Put<std::int32_t>(image,first.rva+1,saved);
    CHECK(InstallTimeShipScriptGates());CHECK(InstallTimeShipScriptGates());
    for(const auto& site:timeship_script::sites)CHECK(site.changed && site.thunk && std::memcmp(image+site.rva,site.patched,5)==0);
    InvokeAll();CHECK(calls==6 && lastFrames==600);
    online=true;InvokeAll();CHECK(calls==12); // autonomous/uncontrolled vanilla scripts still work
    controlled=true;InvokeAll();CHECK(calls==12); // unknown role fails closed
    role=true;InvokeAll();CHECK(calls==12); // guests never create entry0 effects twice
    authority=true;InvokeAll();CHECK(calls==18);
    online=false;authority=false;role=true;InvokeAll();CHECK(calls==18);
    controlled=false;InvokeAll();CHECK(calls==18);controlled=true;
    authority=true;quiescing=true;InvokeAll();CHECK(calls==18);
    controlled=false;InvokeAll();CHECK(calls==18);controlled=true;
    quiescing=false;authority=false;role=false;contextKnown=false;InvokeAll();CHECK(calls==18);
    contextKnown=true;InvokeAll();CHECK(calls==24);
    image[first.rva]=0xCC;CHECK(!RestoreTimeShipScriptGates());CHECK(first.changed && first.thunk);
    CHECK(!InstallTimeShipScriptGates());CHECK(first.changed && first.thunk); // no stealing foreign mutation
    std::memcpy(image+first.rva,first.patched,5);CHECK(RestoreTimeShipScriptGates());
    for(const auto& site:timeship_script::sites)CHECK(!site.changed && !site.thunk && std::memcmp(image+site.rva,site.original,5)==0);
    CHECK(RestoreTimeShipScriptGates());InvokeAll();CHECK(calls==30);
    CHECK(InstallTimeShipScriptGates());auto* previous=image;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2000000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!image)return 2;
    CHECK(!InstallTimeShipScriptGates());VirtualFree(image,0,MEM_RELEASE);image=previous;
    CHECK(RestoreTimeShipScriptGates());VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    CHECK(!InstallTimeShipScriptGates());
    std::printf("TimeShip AS authority gates: %d checks, %d failures\n",checks,failed);return failed ? 1 : 0;
}
