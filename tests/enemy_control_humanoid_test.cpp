// Production per-weapon native fire selection on private memory/vtables.
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include "../src/enemy_control_humanoid.h"
#include "../src/crew.h"
#include "../src/memory.h"
namespace crew { unsigned char* image=nullptr; }
#include "../src/enemy_control_humanoid.cpp"
namespace {
int checks=0,failed=0;unsigned fired[5]{};bool failInput=false,foreignCall=false,busyWeapon=false,clearLeaseDuringInput=false;
alignas(16) unsigned char body[0x2000]{},other[0x2000]{},weak[32]{},weapons[5][0x1800]{},controls[5][32]{},rows[5][16]{};
crew::enemy_native::NativeProfile nativeProfile{"EDF6_Martian",0,0,0,1,86,0x1F9,crew::enemy_native::Kind::humanoid,7,5};
void Check(bool yes,const char* label){++checks;if(!yes){++failed;std::printf("FAIL %s\n",label);}}
bool __fastcall NativeBusy(const void*){return busyWeapon;}
void Jump(unsigned char* at,const void* fn){at[0]=0x48;at[1]=0xB8;std::memcpy(at+2,&fn,8);at[10]=0xFF;at[11]=0xE0;}
bool __fastcall Fire0(void*){++fired[0];return true;}
bool __fastcall Fire1(void*){++fired[1];return true;}
bool __fastcall Fire2(void*){++fired[2];return true;}
bool __fastcall Fire3(void*){++fired[3];return true;}
bool __fastcall PassiveShield(void*){++fired[4];return false;}
crew::enemy_native::NativeLease MakeLease() {
    using namespace crew;using namespace crew::enemy_native;
    std::memset(body,0,sizeof(body));std::memset(other,0,sizeof(other));std::memset(weak,0,sizeof(weak));
    std::memset(weapons,0,sizeof(weapons));std::memset(controls,0,sizeof(controls));std::memset(rows,0,sizeof(rows));
    Put<const void*>(body,0x30,weak);Put<LONG>(weak,8,1);
    Put<const void*>(body,0x1120,rows);Put<std::size_t>(body,0x1130,5);
    for(unsigned i=0;i<5;++i) {
        Put<const void*>(rows[i],0,weapons[i]);Put<const void*>(rows[i],8,controls[i]);Put<LONG>(controls[i],8,1);Put<const void*>(controls[i],16,weapons[i]);Put<const void*>(weapons[i],0x30,controls[i]);
        Put<const void*>(weapons[i],0x5B0,body);weapons[i][0x5E2]=1;
        Put<const void*>(weapons[i],0,i<4 ? image+weaponHooks[i].vtable : image+0x17C1918);
    }
    NativeLease lease{};lease.object=body;lease.control=weak;lease.profile=&nativeProfile;return lease;
}
void Call(unsigned index) {
    using Fn=bool(__fastcall*)(void*);
    const auto vt=crew::At<const void* const*>(weapons[index],0);
    reinterpret_cast<Fn>(const_cast<void*>(vt[17]))(weapons[index]);
}
}
namespace crew::enemy_native {
bool RunInput(NativeLease& lease,const Input&,InputFn,void*,std::uint64_t* result) noexcept {
    lease.borrowed=true;
    if(clearLeaseDuringInput)lease={};
    for(unsigned i=0;i<5;++i)Call(i);
    if(foreignCall) {
        const auto saved=At<const void*>(weapons[0],0x5B0);Put<const void*>(weapons[0],0x5B0,other);Call(0);Put<const void*>(weapons[0],0x5B0,saved);
    }
    if(result)*result=123;
    lease.borrowed=false;
    return !failInput;
}
}
int main() {
    using namespace crew;using namespace crew::enemy_native;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!image)return 2;
    for(auto& hook:weaponHooks){std::memcpy(image+hook.fire,hook.signature,16);Put<const void*>(image+hook.vtable,17*8,image+hook.fire);}
    image[weaponHooks[3].fire]=0;Check(!InstallHumanoidControl(),"mismatched native fire code does not patch any weapon class");
    for(const auto& hook:weaponHooks)Check(At<const void*>(image+hook.vtable,17*8)==image+hook.fire,"failed preflight leaves the native table untouched");
    for(auto& hook:weaponHooks)std::memcpy(image+hook.fire,hook.signature,16);
    Check(InstallHumanoidControl(),"all exact supported fire methods install together");
    FireFn fake[]={&Fire0,&Fire1,&Fire2,&Fire3};for(unsigned i=0;i<4;++i)weaponHooks[i].next=fake[i];
    Put<const void*>(image+0x17C1918,17*8,reinterpret_cast<const void*>(&PassiveShield));
    for(unsigned i=0;i<4;++i){Put<const void*>(image+weaponHooks[i].vtable,18*8,image+kBusy[i]);Jump(image+kBusy[i],reinterpret_cast<const void*>(&NativeBusy));}
    FlushInstructionCache(GetCurrentProcess(),image,0x1800000);
    auto lease=MakeLease();Check(HumanoidWeaponCount(lease)==4,"all four actual guns are selectable while passive shield is not fake fire");
    unsigned index=99;Check(HumanoidWeaponIndex(lease,3,&index) && index==3,"the native weapon slot identity is preserved");
    enemy_control::Ability ability{};ability.id=123;wcscpy_s(ability.name,L"keep identity");
    Check(ReadHumanoidWeaponState(lease,2,&ability) && ability.state==enemy_control::AbilityState::native && ability.id==123 && std::wcscmp(ability.name,L"keep identity")==0,"state refresh uses real weapon readiness without changing identity");
    busyWeapon=true;Check(ReadHumanoidWeaponState(lease,2,&ability) && ability.state==enemy_control::AbilityState::cooldown && !ability.cooldownKnown,"actual native busy query exposes no made-up seconds");busyWeapon=false;
    weapons[2][0x5E2]=0;Check(ReadHumanoidWeaponState(lease,2,&ability) && ability.state==enemy_control::AbilityState::unavailable,"native unusable weapon state is unavailable");weapons[2][0x5E2]=1;
    Put<const void*>(weapons[2],0x30,controls[1]);Check(HumanoidWeaponCount(lease)==UINT_MAX && !ReadHumanoidWeaponState(lease,2,&ability),"reused weapon weak identity refuses a partial supposedly complete catalogue");Put<const void*>(weapons[2],0x30,controls[2]);
    unsigned validated=99;const auto originalWeaponTable=At<const void*>(weapons[2],0);
    Put<const void*>(weapons[2],0,image+0x17C1900);Check(!ReadHumanoidWeaponCount(lease,&validated) && validated==0,"unknown live weapon type cannot be silently omitted from complete skills");Put<const void*>(weapons[2],0,originalWeaponTable);
    const auto originalFire=At<const void*>(image+weaponHooks[2].vtable,17*8);Put<const void*>(image+weaponHooks[2].vtable,17*8,image+weaponHooks[2].fire);
    Check(!ReadHumanoidWeaponCount(lease,&validated),"lost native fire selection hook refuses unsupported independent weapon control");Put<const void*>(image+weaponHooks[2].vtable,17*8,originalFire);
    Input input{};std::uint64_t result=0;
    Check(RunHumanoidInput(lease,input,nullptr,nullptr,1ULL<<2,&result) && result==123,"native input result is preserved inside the selection scope");
    Check(fired[0]==0 && fired[1]==0 && fired[2]==1 && fired[3]==0 && fired[4]==1,"only the chosen third gun fires; native passive shield path remains untouched");
    Check(At<const void*>(body,0x1120)==rows && At<std::size_t>(body,0x1130)==5,"selection never edits the equipment vector or its native network indices");
    Call(0);Check(fired[0]==1,"outside the controlled input call every native weapon dispatch is unchanged");
    foreignCall=true;RunHumanoidInput(lease,input,nullptr,nullptr,0);foreignCall=false;
    Check(fired[0]==2 && fired[2]==1,"another body's weapon is never filtered by this player's scoped selection");
    RunHumanoidInput(lease,input,nullptr,nullptr,~0ULL);
    Check(fired[0]==3 && fired[1]==1 && fired[2]==2 && fired[3]==1,"generic salvo retains all four native fire calls");
    Put<LONG>(controls[1],8,0);Check(HumanoidWeaponCount(lease)==3 && HumanoidWeaponIndex(lease,1,&index) && index==2,"broken arm is skipped without renumbering another gun's native slot");
    failInput=true;Check(!RunHumanoidInput(lease,input,nullptr,nullptr,0),"native input failure propagates");failInput=false;
    const unsigned before=fired[3];Call(3);Check(fired[3]==before+1,"failed native input cannot leave its thread-local fire restriction active");
    clearLeaseDuringInput=true;const unsigned beforeClear0=fired[0],beforeClear1=fired[1],beforeClear2=fired[2],beforeClear3=fired[3];
    RunHumanoidInput(lease,input,nullptr,nullptr,1ULL<<2);clearLeaseDuringInput=false;
    Check(fired[0]==beforeClear0 && fired[1]==beforeClear1 && fired[2]==beforeClear2 && fired[3]==beforeClear3,"reentrant lease clear cannot turn the old controlled body's unselected weapons into unfiltered foreign weapons");
    lease=MakeLease();
    Put<const void*>(body,0x30,nullptr);Check(HumanoidWeaponCount(lease)==UINT_MAX,"reused body identity cannot retain old weapon selection");
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("enemy control humanoid weapons: %d checks, %d failures\n",checks,failed);return failed ? 1 : 0;
}
