// Production TimeShip adapter with executable ABI boundary fixtures. No game runs.
#include "../src/enemy_control_timeship.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace crew {unsigned char* image=nullptr;}
namespace {
using namespace crew;using namespace crew::enemy_native;
int checks=0,failed=0,calls=0,scriptRestores=0;bool scriptInstall=true,podInstall=true,podThrow=false;bool online=false,contextKnown=true,role=false,authority=false,current=true,aim=true,podRestore=true,badWarp=false,quiescing=false;
std::uint64_t sequence=1,lastSeed=0;alignas(16) float aimPoint[3]={15,25,35},recorded[16]{};int delay=-1;bool aligned=false;
const NativeProfile profile[]={ {"EDF6_TimeShip",0x17B93C0,0x3EE300,0x3EDE70,0xF9DF8468,55,0xB,Kind::flying,15,4} };
alignas(16) unsigned char object[0x2000]{},control[32]{},controller[0x200]{},link[0x100]{},anchor[0x400]{},anchorControl[32]{};
void Check(bool yes,const char* label,int line){++checks;if(!yes){++failed;std::printf("%d: %s\n",line,label);}}
#define CHECK(x) Check((x),#x,__LINE__)
void Jump(unsigned char* at,const void* fn){at[0]=0x48;at[1]=0xB8;std::memcpy(at+2,&fn,8);at[10]=0xFF;at[11]=0xE0;}
void __fastcall FakeWarp(void* root,const timeship_control::Matrix* matrix,int frames){
    ++calls;delay=frames;aligned=(reinterpret_cast<std::uintptr_t>(matrix)&15u)==0;std::memcpy(recorded,matrix->values,sizeof(recorded));
    std::memcpy(static_cast<unsigned char*>(root)+0x60,matrix->values,sizeof(recorded));
    Put<unsigned>(root,0x1470,static_cast<unsigned>(frames));Put<unsigned>(root,0x1168,7);
    Put<const void*>(root,0x10C8,image+(badWarp ? 0x3F1170 : 0x3F1250));
}
void __fastcall FakeWarp2(void* root,const timeship_control::Matrix* matrix,int frames){FakeWarp(root,matrix,frames);Put<const void*>(root,0x10C8,image+0x3F1430);}
void __fastcall FakeTarget(void* root,const float* target){++calls;static_cast<unsigned char*>(root)[0x1410]=1;std::memcpy(static_cast<unsigned char*>(root)+0x1420,target,16);}
NativeLease Reset(){
    NativeLease prior{};prior.object=object;prior.control=control;prior.profile=profile;podRestore=true;RestoreTimeShipControl(prior);
    std::memset(object,0,sizeof(object));std::memset(control,0,sizeof(control));std::memset(controller,0,sizeof(controller));std::memset(link,0,sizeof(link));
    std::memset(anchor,0,sizeof(anchor));std::memset(anchorControl,0,sizeof(anchorControl));
    Put<const void*>(object,0x30,control);Put<LONG>(control,8,1);Put<const void*>(object,0x10C8,image+0x3F1170);
    for(unsigned i=0;i<4;++i)Put<float>(object,0x60+i*20,1);
    Put<float>(object,0x90,123);Put<float>(object,0x94,321);Put<float>(object,0x98,-456);
    Put<const void*>(object,0x1110,controller);Put<const void*>(controller,0x28,link);Put<const void*>(link,0x48,anchor);Put<const void*>(link,0x50,anchorControl);
    Put<int>(link,0x58,-1);Put<const void*>(anchor,0x30,anchorControl);Put<LONG>(anchorControl,8,1);
    online=false;contextKnown=true;role=false;authority=false;current=true;aim=true;sequence=1;lastSeed=0;badWarp=false;quiescing=false;calls=0;
    NativeLease lease{};lease.object=object;lease.control=control;lease.profile=profile;return lease;
}
void BuildImage(){
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2000000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!image)std::exit(2);
    for(const auto& p:timeship_control::prints)std::memcpy(image+p.rva,p.bytes,p.size);
    const unsigned char undo[]={0x48,0x83,0xC4,0x30,0x5F};
    std::memcpy(image+0x3F304F,undo,sizeof(undo));Jump(image+0x3F3054,reinterpret_cast<const void*>(&FakeWarp));
    std::memcpy(image+0x3F316F,undo,sizeof(undo));Jump(image+0x3F3174,reinterpret_cast<const void*>(&FakeWarp2));
    Jump(image+0x3ECA8E,reinterpret_cast<const void*>(&FakeTarget));FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
}
}
namespace crew {
bool InSession() noexcept{return online;}
bool EnemyControlNetworkContext(const void*,bool* out) noexcept{if(!contextKnown || !out)return false;*out=online;return true;}
bool EnemyControlCurrentInput(const void*,enemy_control::Input* input) noexcept{if(!current || !input)return false;input->sequence=sequence;return true;}
bool EnemyControlWeaponAim(const void*,float point[3]) noexcept{if(!aim)return false;std::memcpy(point,aimPoint,sizeof(aimPoint));return true;}
}
namespace crew::enemy_native {
const NativeProfile* Profiles(std::size_t* count) noexcept{if(count)*count=1;return profile;}
bool NativeActionRole(const void*,bool* host) noexcept{if(host)*host=authority;return role;}
bool InstallTimeShipScriptGates() noexcept{return scriptInstall;}
bool RestoreTimeShipScriptGates() noexcept{++scriptRestores;return true;}
bool InstallTimeShipPodControl() noexcept{if(podThrow)RaiseException(0xE0423242,0,0,nullptr);return podInstall;}
bool NativeActionQuiescing(const void*) noexcept{return quiescing;}
bool SetSeed(const NativeLease&,std::uint64_t seed) noexcept{lastSeed=seed;return true;}
unsigned ListTimeShipPodAbilities(const NativeLease&,NativeAbility*,unsigned) noexcept{return 4;}
bool ReadTimeShipPodAbilityState(const NativeLease&,const NativeAbility&,enemy_control::Ability*) noexcept{return false;}
bool ExecuteTimeShipPodAbility(const NativeLease&,const NativeAbility&,std::uint64_t,std::uint64_t) noexcept{return false;}
bool RestoreTimeShipPodControl(const NativeLease&) noexcept{return podRestore;}
}
int main(){
    BuildImage();auto lease=Reset();NativeAbility list[8]{};
    CHECK(InstallTimeShipControl());podInstall=false;
    CHECK(!InstallTimeShipControl() && scriptRestores==1);podInstall=true;scriptInstall=false;
    CHECK(!InstallTimeShipControl() && scriptRestores==1);scriptInstall=true;podThrow=true;
    CHECK(!InstallTimeShipControl() && scriptRestores==2);podThrow=false;
    CHECK(InstallTimeShipControl());
    CHECK(ListTimeShipAbilities(lease,list,8)==9);CHECK(ListTimeShipAbilities(lease,nullptr,0)==9);
    NativeAbility shortList[1]{};CHECK(ListTimeShipAbilities(lease,shortList,1)==9 && shortList[0].argument==0);
    for(unsigned i=0;i<5;++i)CHECK(list[i].kind==AbilityKind::timeShipCommand && !list[i].held && !list[i].pressed && list[i].argument==static_cast<int>(i));
    enemy_control::Ability state{};state.id=99;wcscpy_s(state.name,L"preserve identity");
    CHECK(ReadTimeShipAbilityState(lease,list[0],&state) && state.state==enemy_control::AbilityState::native && state.id==99 && !state.cooldownKnown);
    CHECK(ExecuteTimeShipAbility(lease,list[0],1,42) && calls==1 && delay==600 && aligned && lastSeed==42);
    CHECK(recorded[12]==123 && recorded[13]==321 && recorded[14]==-456 && recorded[15]==1);
    CHECK(ReadTimeShipAbilityState(lease,list[0],&state) && state.state==enemy_control::AbilityState::active);
    CHECK(!ExecuteTimeShipAbility(lease,list[1],2,43) && calls==1 && lastSeed==42);
    Put<const void*>(object,0x10C8,image+0x3F1170);Put<unsigned>(object,0x1168,0);
    CHECK(!ExecuteTimeShipAbility(lease,list[0],1,43) && calls==1);
    CHECK(!ExecuteTimeShipAbility(lease,list[1],1,43) && calls==1);
    sequence=2;CHECK(ExecuteTimeShipAbility(lease,list[1],2,43) && calls==2 && delay==600);
    CHECK(ReadTimeShipAbilityState(lease,list[1],&state) && state.state==enemy_control::AbilityState::active);
    CHECK(RestoreTimeShipControl(lease));Put<const void*>(object,0x10C8,image+0x3F1170);Put<unsigned>(object,0x1168,0);sequence=1;
    CHECK(ExecuteTimeShipAbility(lease,list[0],1,44) && calls==3);
    lease=Reset();current=false;CHECK(!ExecuteTimeShipAbility(lease,list[0],1,1) && calls==0);current=true;
    CHECK(!ExecuteTimeShipAbility(lease,list[0],2,1) && calls==0);
    Put<float>(object,0x60,std::numeric_limits<float>::quiet_NaN());CHECK(!ExecuteTimeShipAbility(lease,list[0],1,1) && calls==0);
    Put<float>(object,0x60,0);CHECK(!ExecuteTimeShipAbility(lease,list[0],1,1) && calls==0);
    lease=Reset();contextKnown=false;CHECK(!ExecuteTimeShipAbility(lease,list[0],1,1) && calls==0);
    contextKnown=true;online=true;CHECK(!ExecuteTimeShipAbility(lease,list[0],1,1) && calls==0);
    role=true;CHECK(!ExecuteTimeShipAbility(lease,list[0],1,1) && calls==0);
    online=false;CHECK(!ExecuteTimeShipAbility(lease,list[0],1,1) && calls==0);
    authority=true;quiescing=true;CHECK(!ExecuteTimeShipAbility(lease,list[0],1,1) && calls==0);
    quiescing=false;online=true;
    CHECK(ExecuteTimeShipAbility(lease,list[0],1,1) && calls==1);
    lease=Reset();CHECK(ExecuteTimeShipAbility(lease,list[2],1,2) && calls==1 && object[0x1410]);
    CHECK(At<float>(object,0x1420)==15 && At<float>(object,0x1424)==25 && At<float>(object,0x1428)==35 && At<float>(object,0x142C)==1);
    CHECK(ReadTimeShipAbilityState(lease,list[2],&state) && state.state==enemy_control::AbilityState::active);
    sequence=2;CHECK(!ExecuteTimeShipAbility(lease,list[2],2,3) && calls==1);
    lease=Reset();aim=false;CHECK(!ExecuteTimeShipAbility(lease,list[2],1,1));aim=true;
    aimPoint[1]=std::numeric_limits<float>::infinity();CHECK(!ExecuteTimeShipAbility(lease,list[2],1,1) && calls==0);aimPoint[1]=25;
    for(unsigned reason=0;reason<5;++reason){
        lease=Reset();
        if(reason==0)controller[0x120]=1;if(reason==1)controller[0x121]=1;if(reason==2)link[0x70]=1;
        if(reason==3)Put<int>(link,0x58,30);if(reason==4)Put<LONG>(anchorControl,8,0);
        CHECK(!ExecuteTimeShipAbility(lease,list[2],1,1) && calls==0);
        CHECK(ReadTimeShipAbilityState(lease,list[2],&state) && state.state==enemy_control::AbilityState::unavailable);
    }
    lease=Reset();CHECK(ExecuteTimeShipAbility(lease,list[3],1,1) && At<unsigned>(object,0x1474)==2);
    CHECK(ReadTimeShipAbilityState(lease,list[3],&state) && state.state==enemy_control::AbilityState::active);
    lease=Reset();CHECK(ExecuteTimeShipAbility(lease,list[4],1,1) && At<unsigned>(object,0x1474)==1);
    lease=Reset();NativeAbility forged=list[0];forged.held=1;CHECK(!ExecuteTimeShipAbility(lease,forged,1,1));
    forged=list[0];forged.id="different";CHECK(!ExecuteTimeShipAbility(lease,forged,1,1));
    forged=list[0];forged.argument=99;CHECK(!ExecuteTimeShipAbility(lease,forged,1,1));
    NativeProfile copy=profile[0];lease.profile=&copy;CHECK(ListTimeShipAbilities(lease,list,8)==0 && !ExecuteTimeShipAbility(lease,forged,1,1));
    lease=Reset();Put<LONG>(control,8,0);CHECK(ListTimeShipAbilities(lease,list,8)==0);
    lease=Reset();object[0x2E8]=1;CHECK(ListTimeShipAbilities(lease,list,8)==0);
    lease=Reset();Put<const void*>(object,0x30,anchorControl);CHECK(ListTimeShipAbilities(lease,list,8)==0);
    lease=Reset();image[0x3F3040]=0;CHECK(ListTimeShipAbilities(lease,list,8)==0);image[0x3F3040]=0x48;
    lease=Reset();badWarp=true;CHECK(!ExecuteTimeShipAbility(lease,list[0],1,1) && calls==1);
    badWarp=false;Put<unsigned>(object,0x1168,0);sequence=2;CHECK(!ExecuteTimeShipAbility(lease,list[0],2,2) && calls==1);
    CHECK(ReadTimeShipAbilityState(lease,list[0],&state) && state.state==enemy_control::AbilityState::unavailable);
    podRestore=false;CHECK(!RestoreTimeShipControl(lease));CHECK(!ExecuteTimeShipAbility(lease,list[0],2,2) && calls==1);
    podRestore=true;CHECK(RestoreTimeShipControl(lease));CHECK(ExecuteTimeShipAbility(lease,list[0],2,2) && calls==2);
    CHECK(RestoreTimeShipControl(lease));VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("TimeShip command adapter: %d checks, %d failures\n",checks,failed);return failed ? 1 : 0;
}
