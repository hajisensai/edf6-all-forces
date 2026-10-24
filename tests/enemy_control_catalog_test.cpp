// Production live-equipment catalogue; no game process or DLL execution.
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <iterator>
#include "../src/enemy_control_catalog.h"
#include "../src/enemy_control_types.h"
#include "../src/crew.h"
#include "../src/memory.h"
namespace crew { unsigned char* image=nullptr; }
namespace crew::enemy_native {
constexpr NativeProfile fixtureProfiles[]={
    {"Monster501",0x17C5628,0x4ACD70,0x4ACD10,1,52,0xDF0,Kind::monster,7,4},
    {"EDF6_FlyingDyno",0x17B6270,0x3B3ED0,0x3B39B0,2,55,0x3DCF,Kind::flying,15,4},
    {"Humanoid_Basic",0x17C4D70,0x4A6780,0x46EFF0,3,86,0x1F9,Kind::humanoid,7,5},
    {"EDF6_Merman",0x17B5370,0x3A4AF0,0x3A4340,4,55,0xFF,Kind::flying,15,4},
    {"GiantAnt",0,0,0,5,62,0xF0,Kind::insect,7,4},
    {"GiantBee",0,0,0,6,59,0xF0,Kind::insect,7,4},
    {"GiantSpider",0,0,0,7,60,0xF0,Kind::insect,7,4},
    {"GiantDango",0,0,0,8,59,0xF0,Kind::insect,7,4},
    {"DragonSmall",0,0,0,9,59,0xF0,Kind::insect,7,4},
    {"Nephila",0,0,0,10,45,0x30,Kind::insect,7,4},
    {"EDF6_Berserker_TypeA",0,0,0,11,79,0x31,Kind::walking,7,4},
    {"EDF6_Berserker_TypeGrapple",0,0,0,12,79,0x31,Kind::walking,7,4},
    {"Deiroi",0,0,0,13,45,0x30,Kind::deiroi,7,4},
    {"EDF6_TimeShip",0x17B93C0,0x3EE300,0x3EDE70,14,55,0xB,Kind::flying,15,4},
    {"Humanoid_BigGreyBoss",0x17BEC68,0x444E90,0x46EFF0,15,86,0x1F9,Kind::humanoid,7,5}
};
const NativeProfile* Profiles(std::size_t* count) noexcept { if(count)*count=std::size(fixtureProfiles);return fixtureProfiles; }
unsigned fixtureHumanoidGuns=0;bool fixtureHumanoidMissing=false,fixtureHumanoidRosterValid=true;
bool ReadHumanoidWeaponState(const NativeLease&,unsigned,enemy_control::Ability* out) noexcept { if(!out)return false;out->state=fixtureHumanoidMissing ? enemy_control::AbilityState::unavailable : enemy_control::AbilityState::native;out->cooldownKnown=false;return true; }
unsigned HumanoidWeaponCount(const NativeLease& lease) noexcept { return lease.profile && lease.profile->kind==Kind::humanoid ? fixtureHumanoidGuns : 0; }
bool ReadHumanoidWeaponCount(const NativeLease& lease,unsigned* count) noexcept {if(!count || !fixtureHumanoidRosterValid)return false;*count=HumanoidWeaponCount(lease);return true;}
bool HumanoidWeaponIndex(const NativeLease&,unsigned visible,unsigned* index) noexcept { if(!index || visible>=fixtureHumanoidGuns)return false;*index=visible*2;return *index<64; }
unsigned fixtureDeiroiGuns=0,fixtureDeiroiOrdinal=999;bool fixtureDeiroiAutomatic=false;bool fixtureDeiroiRosterValid=true;
unsigned DeiroiGunCount(const NativeLease& lease) noexcept { return lease.profile && lease.profile->kind==Kind::deiroi ? fixtureDeiroiGuns : 0; }
bool ReadDeiroiGunCount(const NativeLease& lease,unsigned* count) noexcept {if(!count || !fixtureDeiroiRosterValid)return false;*count=DeiroiGunCount(lease);return true;}
bool DeiroiGunOrdinal(const NativeLease&,unsigned index,unsigned* key) noexcept { if(!key || index>=fixtureDeiroiGuns)return false;*key=((index/3)<<16)|(index%3);return true; }
bool ReadDeiroiAbilityState(const NativeLease&,unsigned,bool automatic,enemy_control::Ability* out) noexcept { if(!out)return false;out->state=automatic ? enemy_control::AbilityState::active : enemy_control::AbilityState::native;out->cooldownKnown=false;return true; }
bool ExecuteDeiroiAbility(const NativeLease&,unsigned ordinal,bool automatic,std::uint64_t,std::uint64_t) noexcept { fixtureDeiroiOrdinal=ordinal;fixtureDeiroiAutomatic=automatic;return true; }
bool fixtureCommandInstalled=true,fixtureAcceptedHandled=false,fixtureAcceptedResult=true;
unsigned fixtureAcceptedCommands=0;
bool InstallCommandAcceptedActions() noexcept {return fixtureCommandInstalled;}
bool ExecuteAcceptedCommand(const NativeLease&,const NativeAbility&,std::uint64_t,std::uint64_t,bool* handled) noexcept {
    if(!handled)return false;*handled=fixtureAcceptedHandled;
    if(fixtureAcceptedHandled){++fixtureAcceptedCommands;return fixtureAcceptedResult;}return true;
}
unsigned fixtureWalkingActions=1,fixtureWalkingGuns=0,fixtureWalkingActionCalls=0,fixtureWalkingGunCalls=0;bool fixtureWalkingRosterValid=true;
bool ReadWalkingActionCount(const NativeLease&,unsigned* count) noexcept {if(!count || !fixtureWalkingRosterValid)return false;*count=fixtureWalkingActions;return true;}
bool WalkingNativeMainAttackAvailable(const NativeLease&) noexcept {return fixtureWalkingRosterValid && fixtureWalkingActions!=0;}
unsigned ListWalkingActionAbilities(const NativeLease&,NativeAbility* out,unsigned capacity) noexcept {
    if(fixtureWalkingActions!=1)return fixtureWalkingActions;
    if(out && capacity)out[0]=NativeAbility{"walking_attack_0",L"明确主招式",0,0,AbilityKind::walkingAction,L"attack",0};
    return 1;
}
unsigned ListWalkingGunAbilities(const NativeLease&,NativeAbility* out,unsigned capacity) noexcept {
    if(fixtureWalkingGuns!=1)return fixtureWalkingGuns;
    if(out && capacity)out[0]=NativeAbility{"walking_gun_0",L"独立臂炮",0,0,AbilityKind::walkingGun,nullptr,0};
    return 1;
}
bool ExecuteWalkingAction(const NativeLease&,unsigned,std::uint64_t,std::uint64_t) noexcept {++fixtureWalkingActionCalls;return true;}
bool ExecuteWalkingGun(const NativeLease&,unsigned,std::uint64_t,std::uint64_t) noexcept {++fixtureWalkingGunCalls;return true;}
bool ReadWalkingActionState(const NativeLease&,unsigned,enemy_control::Ability* out) noexcept {if(!out)return false;out->state=enemy_control::AbilityState::active;return true;}
bool ReadWalkingGunState(const NativeLease&,unsigned,enemy_control::Ability* out) noexcept {if(!out)return false;out->state=enemy_control::AbilityState::native;return true;}
unsigned fixtureTimeShipCalls=0;std::int32_t fixtureTimeShipCommand=-1;
unsigned ListTimeShipAbilities(const NativeLease& lease,NativeAbility* out,unsigned capacity) noexcept {
    if(!lease.profile || std::strcmp(lease.profile->name,"EDF6_TimeShip")!=0)return 0;
    static constexpr NativeAbility commands[]={
        {"timeship_warp",L"原地跃迁",0,0,AbilityKind::timeShipCommand,nullptr,0},
        {"timeship_anchor",L"向准星部署锚",0,0,AbilityKind::timeShipCommand,nullptr,2}
    };
    for(unsigned i=0;out && i<capacity && i<std::size(commands);++i)out[i]=commands[i];
    return static_cast<unsigned>(std::size(commands));
}
bool ReadTimeShipAbilityState(const NativeLease&,const NativeAbility&,enemy_control::Ability* out) noexcept {if(!out)return false;out->state=enemy_control::AbilityState::native;return true;}
bool ExecuteTimeShipAbility(const NativeLease&,const NativeAbility& ability,std::uint64_t,std::uint64_t) noexcept {++fixtureTimeShipCalls;fixtureTimeShipCommand=ability.argument;return true;}
bool SetSeed(const NativeLease& lease,std::uint64_t seed) noexcept { crew::Put<std::uint64_t>(lease.object,0x3E8,seed);crew::Put<std::uint64_t>(lease.object,0x490,seed);return true; }
}
#include "../src/enemy_control_catalog.cpp"
namespace {
int checks=0,failed=0;
void Check(bool yes,const char* label) { ++checks;if(!yes){++failed;std::printf("FAIL %s\n",label);} }
alignas(16) unsigned char object[0x3000]{},control[32]{},table[64*0xE0]{};
int nativeCalls=0,lastArgument=-1;bool lastPlayerTrigger=false;
void __fastcall FakeMonster(void* body,int action) { ++nativeCalls;lastArgument=action;crew::Put<int>(body,0x14D0,action); }
void __fastcall FakeMerman(void* body,int action,bool triggered) {
    ++nativeCalls;lastArgument=action;lastPlayerTrigger=triggered;
    const auto rows=crew::At<const unsigned char*>(body,0x16F8);
    crew::Put<const void*>(body,0x1710,rows+static_cast<std::size_t>(action)*0x88);
}
void Jump(unsigned char* at,const void* fn) {
    const unsigned char op[]={0x48,0xB8};std::memcpy(at,op,2);std::memcpy(at+2,&fn,8);at[10]=0xFF;at[11]=0xE0;
}
crew::enemy_native::NativeLease Lease(unsigned profile) {
    std::memset(object,0,sizeof(object));std::memset(control,0,sizeof(control));std::memset(table,0,sizeof(table));
    crew::Put<const void*>(object,0x30,control);crew::Put<LONG>(control,8,1);
    crew::enemy_native::NativeLease lease{};lease.object=object;lease.control=control;lease.profile=crew::enemy_native::fixtureProfiles+profile;return lease;
}
}
int main() {
    using namespace crew;using namespace crew::enemy_native;
    NativeAbility list[96]{};auto lease=Lease(0);
    Put<const void*>(object,0x14A8,table);Put<std::size_t>(object,0x14B8,18);
    for(unsigned i=0;i<18;++i)Put<const wchar_t*>(table,static_cast<std::size_t>(i)*0x38,L"attack_chop_l");
    Check(Abilities(lease,list,96)==20,"18 native attacks plus two meaningful movement controls");
    Check(Abilities(lease,nullptr,0)==20,"count query preserves the complete catalogue");
    Check(list[2].kind==AbilityKind::monsterAction && list[2].argument==0 && list[19].argument==17,"every actual attack table entry has an independent command");
    Check(std::wcscmp(list[2].name,L"左手劈击")==0,"known attack names are localized from their real table value");
    Check(std::strcmp(list[2].id,list[19].id)!=0,"same-named native entries retain distinct index identities");
    fixtureCommandInstalled=false;Check(Abilities(lease,list,96)==0,"a missing accepted-command codec refuses incomplete native skill support");fixtureCommandInstalled=true;
    NativeAbility bounded[2]{};Check(Abilities(lease,bounded,2)==20 && bounded[1].held==(1u<<8),"bounded output returns total without truncating the actual count");
    enemy_control::Ability status{};status.id=99;wcscpy_s(status.name,L"stable skill");
    Put<int>(object,0x14D0,-1);Put<unsigned>(object,0x8B0,4);
    Check(ReadAbilityState(lease,list[19],&status) && status.state==enemy_control::AbilityState::native && status.id==99 && std::wcscmp(status.name,L"stable skill")==0,"live native readiness refresh preserves immutable ability identity");
    Put<int>(object,0x14D0,17);Check(ReadAbilityState(lease,list[19],&status) && status.state==enemy_control::AbilityState::active,"monster active index comes from the original native attack state");
    Put<int>(object,0x14D0,-1);Put<unsigned>(object,0x8B0,0);Check(ReadAbilityState(lease,list[19],&status) && status.state==enemy_control::AbilityState::cooldown && !status.cooldownKnown,"monster busy gate reports no fabricated seconds");
    Put<std::size_t>(object,0x14B8,15);Check(Abilities(lease,list,96)==17,"King's shorter table never inherits the base monster's final attacks");
    Put<std::size_t>(object,0x14B8,38);for(unsigned i=0;i<38;++i)Put<const wchar_t*>(table,static_cast<std::size_t>(i)*0x38,L"attack_spark");
    Check(Abilities(lease,list,96)==40 && list[39].argument==37,"38-entry Archelus table is complete beyond 32 skills");
    Put<std::size_t>(object,0x14B8,4097);Check(Abilities(lease,list,96)==0,"unadmitted table size refuses incomplete skill support");
    Put<std::size_t>(object,0x14B8,18);Put<LONG>(control,8,0);Check(Abilities(lease,list,96)==0,"expired weak identity cannot publish old skills");
    Put<LONG>(control,8,1);Put<const void*>(object,0x30,nullptr);Check(Abilities(lease,list,96)==0,"object address reuse invalidates the live table");
    lease=Lease(1);Put<std::size_t>(object,0x1408,7);
    Check(Abilities(lease,list,96)==7,"M051's seven Radon actions do not acquire second-form moves");
    Put<std::size_t>(object,0x1408,10);Check(Abilities(lease,list,96)==11,"full Radon has ten native actions plus original transformation");
    bool fire=false,transform=false;for(unsigned i=0;i<11;++i){if(std::wcscmp(list[i].name,L"火球")==0)fire=true;if(std::wcscmp(list[i].name,L"进入第二形态")==0)transform=true;}
    Check(fire && transform,"Radon names come from the verified native action ordering");
    lease=Lease(3);Put<const void*>(object,0x16F8,table);Put<std::size_t>(object,0x1708,10);
    for(unsigned i=0;i<10;++i){Put<const wchar_t*>(table,static_cast<std::size_t>(i)*0x88,L"dive");Put<int>(table,static_cast<std::size_t>(i)*0x88+8,1);Put<int>(table,static_cast<std::size_t>(i)*0x88+0x1C,1);}
    Check(Abilities(lease,list,96)==10,"Merman publishes all ten absolute actions across three movement modes");
    Put<int>(object,0x17F0,1);Check(Abilities(lease,list,96)==10 && list[0].kind==AbilityKind::mermanAction && list[9].argument==9,"changing Merman modes never aliases relative pad actions or changes the skill catalogue");
    Put<int>(object,0x17F0,0);Check(ReadAbilityState(lease,list[0],&status) && status.state==enemy_control::AbilityState::unavailable,"Merman actual mode mismatch is visible without deleting the skill ID");
    Put<int>(object,0x17F0,1);object[0x17F4]=1;Check(ReadAbilityState(lease,list[0],&status) && status.state==enemy_control::AbilityState::cooldown,"Merman busy animation gate is read from native state");object[0x17F4]=0;
    Put<int>(table,0x1C,-1);Check(Abilities(lease,list,96)==0,"an unresolved native Merman action refuses incomplete admission");
    lease=Lease(4);Check(Abilities(lease,list,96)==2 && list[0].held==(1u<<4) && list[1].held==(1u<<5),"ants expose attack/capture and contextual throw/bite without unused keys");
    lease=Lease(5);Check(Abilities(lease,list,96)==2 && std::wcscmp(list[0].name,L"起飞")==0,"bee flight and needle attack have distinct native meanings");
    lease=Lease(6);Check(Abilities(lease,list,96)==2 && std::wcscmp(list[0].name,L"跳跃")==0,"spider jump is not mislabeled as a second projectile");
    lease=Lease(7);Check(Abilities(lease,list,96)==2 && list[1].held==(1u<<6),"pillbug uses its real roll-attack record6");
    lease=Lease(8);Check(Abilities(lease,list,96)==3 && !(list[2].held&(1u<<7)),"dragon's native forced-damage key is not a player skill");
    lease=Lease(9);Check(Abilities(lease,list,96)==1 && list[0].held==(1u<<4),"Nephila unused near-animation key is not a fabricated attack");
    lease=Lease(10);Check(Abilities(lease,list,96)==3 && list[2].kind==AbilityKind::walkingAction,"identical native android pad records are not duplicated while the actual attack table is explicit");
    lease=Lease(11);Check(Abilities(lease,list,96)==4,"grapple android preserves separate context and held-tether inputs plus native indexed actions");
    fixtureWalkingGuns=1;Check(Abilities(lease,list,96)==5 && list[4].kind==AbilityKind::walkingGun,"every configured independent Walking gun is appended without replacing contextual root attacks");
    Check(ExecuteAbility(lease,list[3],17,97) && fixtureWalkingActionCalls==1 && ExecuteAbility(lease,list[4],18,98) && fixtureWalkingGunCalls==1,"Walking table actions and individual batteries route to distinct original native adapters");
    Check(ReadAbilityState(lease,list[3],&status) && status.state==enemy_control::AbilityState::active,"Walking explicit action status comes from its real FSM reader");
    fixtureWalkingGuns=0;fixtureWalkingRosterValid=false;Check(Abilities(lease,list,96)==0,"invalid Walking action configuration refuses partial skill admission");fixtureWalkingRosterValid=true;
    fixtureWalkingActions=0;Check(Abilities(lease,list,96)==2 && list[0].held==1 && list[1].held==(1u<<4),"validated zero real CAS attacks suppress phantom generic melee while preserving real movement and contextual inputs");fixtureWalkingActions=1;
    fixtureWalkingActions=4096;Check(Abilities(lease,list,96)==0,"Walking expansion cannot silently overflow total catalogue limit");fixtureWalkingActions=1;
    lease=Lease(12);fixtureDeiroiGuns=5;Check(Abilities(lease,list,96)==12,"Deiroi retains body skills and fire/automatic command for every configured foot battery");
    Check(std::wcscmp(list[10].name,L"腿 2 炮位 2：准星射击")==0 && list[10].argument==0x10001,"Deiroi gun names and stable ordinals distinguish identical batteries");
    Check(ExecuteAbility(lease,list[10],19,99) && fixtureDeiroiOrdinal==0x10001 && !fixtureDeiroiAutomatic,"selected foot battery dispatches its own manual command");
    Check(ExecuteAbility(lease,list[11],20,100) && fixtureDeiroiOrdinal==0x10001 && fixtureDeiroiAutomatic,"automatic can be restored independently for the selected battery");
    Check(ReadAbilityState(lease,list[11],&status) && status.state==enemy_control::AbilityState::active,"Deiroi state refresh dispatches the real automatic-mode reader");
    fixtureDeiroiGuns=4096;Check(Abilities(lease,list,96)==0,"foot-battery command expansion fails rather than truncating beyond4096");
    fixtureDeiroiGuns=0;fixtureDeiroiRosterValid=false;Check(Abilities(lease,list,96)==0,"unvalidated Deiroi parts cannot silently publish only the two body skills");fixtureDeiroiRosterValid=true;
    Check(Abilities(lease,list,96)==2,"a completely validated zero-battery Deiroi remains distinct from enumeration failure");
    lease=Lease(13);Check(Abilities(lease,list,96)==2 && list[0].kind==AbilityKind::timeShipCommand && !list[0].held && !list[1].pressed,"TimeShip canonical commands replace camera-local pad actions");
    Check(ExecuteAbility(lease,list[1],21,101) && fixtureTimeShipCalls==1 && fixtureTimeShipCommand==2,"TimeShip full native command is routed through its authoritative adapter");
    Check(ReadAbilityState(lease,list[0],&status) && status.state==enemy_control::AbilityState::native,"TimeShip exposes actual native status through its specialized reader");
    auto forgedTimeShip=list[1];forgedTimeShip.argument=99;Check(!ExecuteAbility(lease,forgedTimeShip,22,102) && fixtureTimeShipCalls==1,"unlisted TimeShip command values cannot reach native AS entry points");
    lease=Lease(2);const unsigned n=Abilities(lease,list,96);bool actualFire=false;
    for(unsigned i=0;i<n;++i)if(list[i].held&(1u<<5))actualFire=(list[i].held&(1u<<4))!=0;
    Check(actualFire,"humanoid weapon fire supplies original aim-ready and trigger gates");
    fixtureHumanoidGuns=4;const unsigned withGuns=Abilities(lease,list,96);unsigned single=0;bool lastSlot=false;
    for(unsigned i=0;i<withGuns;++i)if(list[i].kind==AbilityKind::humanoidWeapon){++single;lastSlot=lastSlot || (list[i].argument==6 && list[i].held==0x30);}
    Check(single==4 && lastSlot,"four independent arm guns retain original vector slots including holes");
    fixtureHumanoidMissing=true;Check(ReadAbilityState(lease,list[withGuns-1],&status) && status.state==enemy_control::AbilityState::unavailable,"removed independent arm weapon is shown unavailable without mutating the catalogue");fixtureHumanoidMissing=false;
    fixtureHumanoidRosterValid=false;Check(Abilities(lease,list,96)==0,"unknown live humanoid equipment cannot silently fall back to generic-only skills");
    fixtureHumanoidRosterValid=true;fixtureHumanoidGuns=0;
    Check(!ExecuteAbility(lease,list[0],1,1),"pad actions cannot bypass their original native input pass");
    NativeAbility forged{"boss_genocide",L"forged",0,0,AbilityKind::namedAction,L"attack_genocide",0};
    Check(!ExecuteAbility(lease,forged,2,2),"an unadmitted class cannot invoke a Boss action");
    // These private executable stubs only record the native boundary's arguments.
    // The independent native audit pins these boundaries to the original complete
    // state machines; no installed game code or files are executed/modified here.
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    constexpr unsigned char monsterPrefix[]={0x48,0x83,0xEC,0x38,0xF6,0x81,0xB0,0x08,0x00,0x00,0x04};
    std::memcpy(image+0x4B7AC0,monsterPrefix,sizeof(monsterPrefix));
    constexpr unsigned char unstack[]={0x48,0x83,0xC4,0x38};std::memcpy(image+0x4B7ACB,unstack,sizeof(unstack));
    Jump(image+0x4B7ACF,reinterpret_cast<const void*>(&FakeMonster));
    constexpr unsigned char mermanPrefix[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
    std::memcpy(image+0x3A6710,mermanPrefix,sizeof(mermanPrefix));image[0x3A6720]=0x5F;
    Jump(image+0x3A6721,reinterpret_cast<const void*>(&FakeMerman));
    FlushInstructionCache(GetCurrentProcess(),image,0x1200000);
    lease=Lease(0);Put<const void*>(object,0x14A8,table);Put<std::size_t>(object,0x14B8,38);Put<unsigned>(object,0x8B0,4);
    for(unsigned i=0;i<38;++i)Put<const wchar_t*>(table,static_cast<std::size_t>(i)*0x38,L"attack_spark");
    const unsigned monsterCount=Abilities(lease,list,96);Check(monsterCount==40,"private native-boundary fixture retains 38 original table entries");
    fixtureAcceptedHandled=true;Check(ExecuteAbility(lease,list[39],1,40) && fixtureAcceptedCommands==1 && nativeCalls==0 && At<std::uint64_t>(object,0x490)==0,"accepted-command extension consumes guest intent before local native entry or RNG mutation");
    fixtureAcceptedResult=false;Check(!ExecuteAbility(lease,list[39],2,41) && fixtureAcceptedCommands==2 && nativeCalls==0,"refused authoritative command cannot fall through to the offline native path");fixtureAcceptedHandled=false;fixtureAcceptedResult=true;
    Check(ExecuteAbility(lease,list[39],3,42) && lastArgument==37 && nativeCalls==1,"selected last skill dispatches its exact original table index through the native attack entry");
    Check(At<std::uint64_t>(object,0x3E8)==42 && At<std::uint64_t>(object,0x490)==42,"mask-zero command seeds both original native RNG cursors");
    Put<unsigned>(object,0x8B0,0);Check(!ExecuteAbility(lease,list[2],4,43) && nativeCalls==1,"native busy capability gate blocks a second attack without invoking it");
    Check(At<std::uint64_t>(object,0x490)==42,"a native busy rejection consumes no new command seed");
    Put<unsigned>(object,0x8B0,4);NativeAbility bad=list[2];bad.argument=38;
    Check(!ExecuteAbility(lease,bad,5,44) && nativeCalls==1,"forged index cannot call beyond live native table");
    lease=Lease(3);Put<const void*>(object,0x16F8,table);Put<std::size_t>(object,0x1708,10);
    for(unsigned i=0;i<10;++i){const std::size_t at=static_cast<std::size_t>(i)*0x88;Put<const wchar_t*>(table,at,L"dive");Put<int>(table,at+8,1);Put<int>(table,at+0x1C,1);}
    Abilities(lease,list,96);Put<int>(object,0x17F0,1);
    Check(ExecuteAbility(lease,list[9],6,45) && lastArgument==9 && lastPlayerTrigger && nativeCalls==2,"Merman passes absolute skill and the native playerTriggered flag");
    Put<int>(object,0x17F0,0);Check(!ExecuteAbility(lease,list[9],7,46) && nativeCalls==2,"Merman reproduces native current-form admission before unchecked starter");
    Put<int>(object,0x17F0,1);object[0x17F4]=1;Check(!ExecuteAbility(lease,list[9],8,47) && nativeCalls==2,"Merman preserves native busy animation gate");
    object[0x17F4]=0;image[0x3A6710]=0;Check(!ExecuteAbility(lease,list[9],9,48) && nativeCalls==2,"changed native starter is refused before invocation");
    lease=Lease(0);Put<const void*>(object,0x14A8,table);Put<std::size_t>(object,0x14B8,128);Put<unsigned>(object,0x8B0,4);
    for(unsigned i=0;i<128;++i)Put<const wchar_t*>(table,static_cast<std::size_t>(i)*0x38,L"attack_spark");
    NativeAbility complete[160]{};Check(Abilities(lease,complete,160)==130,"synthetic complete 128-action table is never truncated to 96");
    Check(ExecuteAbility(lease,complete[129],10,49) && lastArgument==127 && nativeCalls==3,"command after index96 is validated against the entire count-query-sized catalogue");
    {   // 银之人 (E516): its own five attacks and its locomotion, nothing of an armed soldier, no dead moves.
        for(const auto& p:kExtraPrints)std::memcpy(image+p.at,p.bytes,sizeof(p.bytes));
        constexpr unsigned char stateFound[]={0x31,0xC0,0xC3};   // after the print's trailing REX.W: xor rax,rax; ret -> CAS state index 0
        std::memcpy(image+0x11664F0+16,stateFound,sizeof(stateFound));FlushInstructionCache(GetCurrentProcess(),image,0x1200000);
        lease=Lease(14);alignas(16) static unsigned char model[0x400]{},parts[16]{};
        Put<const void*>(object,0xA70,model);Put<int>(object,0xEB0,1);Put<const void*>(object,0x17E8,parts);
        const unsigned count=Abilities(lease,list,96);unsigned pads=0,named=0,hands=0;bool soldier=false,dead=false;
        for(unsigned i=0;i<count;++i) {
            const auto& a=list[i];
            if(a.kind==AbilityKind::pad){++pads;soldier=soldier || (a.held&~static_cast<unsigned>((1u<<0)|(1u<<6)|(1u<<7)))!=0 || (a.pressed&((1u<<3)|(1u<<4)|(1u<<5)|(1u<<8)))!=0;}
            else if(a.kind==AbilityKind::namedAction){++named;dead=dead || !std::wcscmp(a.action,L"attack_rollingLazer") || !std::wcscmp(a.action,L"attack_genocide");}
            else if(a.kind==AbilityKind::objectAction){++hands;dead=dead || (a.argument!=4 && a.argument!=5);}
            else soldier=true;
            soldier=soldier || !std::wcscmp(a.name,L"举枪瞄准") || !std::wcscmp(a.name,L"装备武器齐射") || !std::wcscmp(a.name,L"进入／退出掩体") || !std::wcscmp(a.name,L"蹲伏");
        }
        Check(count==8 && pads==3 && named==3 && hands==2,"E516 lists jump / roll / its own dodge and its five original attacks");
        Check(!soldier,"E516 has no soldier aim, volley, cover or crouch");
        Check(!dead,"E516 never lists the rolling laser (missing centre-cannon part), genocide or spear");
        const auto calls=nativeCalls;
        for(const auto& forgedBoss:{NativeAbility{"boss_rolling_laser",L"回旋激光",0,0,AbilityKind::namedAction,L"attack_rollingLazer",0},
                                    NativeAbility{"boss_genocide",L"灭绝光炮",0,0,AbilityKind::namedAction,L"attack_genocide",0},
                                    NativeAbility{"boss_hand_spear",L"光枪齐射",0,0,AbilityKind::objectAction,nullptr,6}})
            Check(!ExecuteAbility(lease,forgedBoss,11,50) && nativeCalls==calls,"a dead E516 move cannot reach the native named / object action entry");
    }
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    NativeProfile fake=*lease.profile;lease.profile=&fake;Check(Abilities(lease,list,96)==0,"a copied profile cannot impersonate an admitted native profile");
    std::printf("enemy control catalogue: %d checks, %d failures\n",checks,failed);return failed ? 1 : 0;
}
