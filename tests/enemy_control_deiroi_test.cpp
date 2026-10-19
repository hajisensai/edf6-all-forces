// Executes the production adapter with synthetic native boundaries only.
// Never loads/runs the game or writes its directory.
#include "../src/enemy_control_deiroi.cpp"
#include <cstdio>
#include <limits>
namespace crew {
unsigned char* image=nullptr;
bool fixtureCoreActive=false,fixtureCanonical=false,fixtureContextKnown=true,fixtureOnline=false,fixtureFresh=true;std::uint64_t fixtureFreshSequence=0;float fixturePoint[3]{};
bool EnemyControlCurrentInput(const void*,enemy_control::Input* input) noexcept {if(!fixtureFresh || !input)return false;*input={};input->absoluteAim=true;input->sequence=fixtureFreshSequence;return true;}
bool EnemyControlNetworkContext(const void*,bool* online) noexcept {if(!online || !fixtureContextKnown)return false;*online=fixtureOnline;return true;}
bool EnemyControlTargetActive(const void*) noexcept {return fixtureCoreActive;}
bool EnemyControlWeaponAim(const void*,float* point) noexcept {
    if(!fixtureCanonical)return false;std::memcpy(point,fixturePoint,12);return true;
}
}
namespace crew::edf41c {
bool AimPoint(const void* object,float* out) noexcept {
    std::memcpy(out,static_cast<const unsigned char*>(object)+0x90,12);return true;
}
}
namespace crew::enemy_native {
bool fixtureRole=false,fixtureAuthority=true,fixtureQuiescing=false,fixtureNetReady=true,fixtureNetFlight=false;unsigned queuedControls=0;
bool NativeActionRole(const void*,bool* authority) noexcept {if(!fixtureRole)return false;*authority=fixtureAuthority;return true;}
bool InstallDeiroiEntityFactoryAim() noexcept {return true;}
bool DeiroiEntityFactoryAimReady() noexcept {return true;}
bool InstallDeiroiAcceptedActions() noexcept {return true;}
bool DeiroiAcceptedActionsReady() noexcept {return fixtureNetReady;}
bool DeiroiAcceptedNativeInFlight(const NativeLease&) noexcept {return fixtureNetFlight;}
bool BindDeiroiAcceptedContext(const NativeLease&) noexcept {return true;}
bool DeiroiAcceptedQuiescing(const void*) noexcept {return fixtureQuiescing;}
bool QueueDeiroiAcceptedControl(const NativeLease&,unsigned,unsigned,std::uint64_t,std::uint64_t,const float*) noexcept {++queuedControls;return true;}
bool RestoreDeiroiAcceptedActions(const NativeLease&) noexcept {return true;}
constexpr NativeProfile fixtureProfile={"Deiroi",0x17AA648,0x31ED90,0x31ED90,1,45,0x30,Kind::deiroi,7,4};
const NativeProfile* Profiles(std::size_t* count) noexcept {*count=1;return &fixtureProfile;}
bool SetSeed(const NativeLease& lease,std::uint64_t seed) noexcept {
    crew::Put<std::uint64_t>(lease.object,0x3E8,seed);crew::Put<std::uint64_t>(lease.object,0x490,seed);return true;
}
}
namespace {
using namespace crew;using namespace crew::enemy_native;using namespace crew::enemy_native::deiroi_control;
int checks=0,failures=0,setCalls=0,mainSetCalls=0,cancelCalls=0,weaponCalls=0,poseCalls=0;bool nativeFault=false,mainFault=false,reenter=false,sawTransition=false,reentrantRestore=true,reentrantExecute=true;
void Check(bool yes,const char* what,int line){++checks;if(!yes){++failures;std::printf("FAIL %d %s\n",line,what);}}
#define CHECK(x) Check((x),#x,__LINE__)
struct Body {
    unsigned char* b=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char* control=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
};
Body owner,parts[3],other;
alignas(16) unsigned char rows[2*0x40]{},controllers[2][0x30]{},nodes[2][3*0x10]{},records[3][0x28]{};
unsigned char* mainRows=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
void* children[3]{};float savedMatrix[16]{},savedPoint[4]{},savedGain=0;
std::uint32_t savedSeed=0;float locator[16]={1,0,0,0,0,1,0,0,0,0,1,0,20,30,40,1};
void Reenter() {
    if(!reenter)return;reenter=false;NativeLease lease{};lease.object=owner.b;lease.control=owner.control;lease.profile=&fixtureProfile;
    sawTransition=DeiroiNativeTransitionActive(lease);reentrantRestore=RestoreDeiroiControl(lease);
    reentrantExecute=ExecuteDeiroiAbility(lease,0x10000,true,9999,9999);
}
void __fastcall FakeSet(void* machine,const Desc* next,void*) {
    ++setCalls;
    const auto previous=At<const void*>(machine,0x18);
    if(previous==image+kFire || previous==image+kCharge || previous==image+kClose)++cancelCalls;
    Put<const void*>(machine,0x18,next->function);Reenter();
    if(next->function==image+kIdle) {
        auto* child=static_cast<unsigned char*>(machine)-kMachine;
        Put<std::uint64_t>(child,0x3E8,At<std::uint64_t>(child,0x3E8)*0x5D588B656C078965ull+0x269EC3ull);
    }
    if(nativeFault){nativeFault=false;RaiseException(0xE0421001,0,0,nullptr);}
}
void __fastcall FakeMainSet(void* machine,const void* callback,void*) {++mainSetCalls;Put<const void*>(machine,0x18,callback);Reenter();if(mainFault){mainFault=false;RaiseException(0xE0421002,0,0,nullptr);}}
bool __fastcall FakeState(void* machine,const Desc* state){return At<const void*>(machine,0x18)==state->function;}
void __fastcall FakePose(void*,const float* point,float gain){++poseCalls;std::memcpy(savedPoint,point,sizeof(savedPoint));savedGain=gain;}
void __fastcall FakeWeapon(void*,float* matrix,std::uint32_t seed){++weaponCalls;std::memcpy(savedMatrix,matrix,sizeof(savedMatrix));savedSeed=seed;}
float* __fastcall FakeLocator(void*,float* out){std::memcpy(out,locator,sizeof(locator));return out;}
void Jump(unsigned rva,const void* fn){unsigned char code[12]={0x48,0xB8};std::memcpy(code+2,&fn,8);code[10]=0xFF;code[11]=0xE0;std::memcpy(image+rva,code,sizeof(code));}
void RefBody(Body& body){Put<void*>(body.b,0x30,body.control);Put<LONG>(body.control,8,1);Put<LONG>(body.control,12,1);}
NativeLease Setup() {
    std::memset(owner.b,0,0x3000);std::memset(owner.control,0,32);
    for(auto& part:parts){std::memset(part.b,0,0x3000);std::memset(part.control,0,32);}
    std::memset(other.b,0,0x3000);std::memset(other.control,0,32);
    RefBody(owner);RefBody(other);
    std::memset(mainRows,0,2*0x290);Put<void*>(owner.b,0x1638,mainRows);
    Put<std::size_t>(owner.b,0x1640,2);Put<std::size_t>(owner.b,0x1648,2);
    for(unsigned i=0;i<2;++i){auto* row=mainRows+i*0x290;Put<void*>(row,0,owner.b);Put<void*>(row,0xB8,row+0x10);Put<void*>(row,0xC0,row);Put<void*>(row,0xC8,image+kMainIdle);}
    Put<void*>(owner.b,0xAE8,rows);Put<std::size_t>(owner.b,0xAF0,2);Put<std::size_t>(owner.b,0xAF8,2);
    Put<void*>(owner.b,0x48,children);Put<std::size_t>(owner.b,0x50,3);Put<std::size_t>(owner.b,0x58,3);
    for(unsigned leg=0;leg<2;++leg) {
        Put<void*>(rows+leg*0x40,0x28,controllers[leg]);Put<void*>(controllers[leg],0,owner.b);
        Put<void*>(controllers[leg],0x10,nodes[leg]);Put<std::size_t>(controllers[leg],0x18,leg ? 1 : 2);Put<std::size_t>(controllers[leg],0x20,leg ? 1 : 2);
    }
    for(unsigned i=0;i<3;++i) {
        RefBody(parts[i]);Put<void*>(parts[i].b,0,image+kBatteryVtable);
        Put<void*>(parts[i].b,0x38,owner.b);Put<void*>(parts[i].b,0x5C0,owner.b);
        Put<void*>(parts[i].b,kMachine+0x28,parts[i].b);Put<void*>(parts[i].b,kMachine+0x18,image+kIdle);
        Put<float>(parts[i].b,0xB40+8,1);Put<void*>(parts[i].b,0xB80,parts[i].b+0x100);
        const unsigned leg=i/2,part=i%2;Put<void*>(nodes[leg]+part*0x10,8,records[i]);Put<void*>(records[i],8,parts[i].b);
        children[2-i]=parts[i].b; // scene-registration order is deliberately different
    }
    NativeLease lease{};lease.object=owner.b;lease.control=owner.control;lease.profile=&fixtureProfile;return lease;
}
void ResetFrame(float* out){std::memcpy(out,locator,sizeof(locator));}
void MemoryTick(){const auto tick=GetTickCount64();do {Sleep(1);}while(GetTickCount64()==tick);}
}
int main(){
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!image)return 2;
    for(const auto& p:kPrints)std::memcpy(image+p.at,p.bytes,sizeof(p.bytes));
    const unsigned sites[]={kAutoCall,kPoseCall,kWeaponCall},targets[]={kSet,0x3296E0,0x6CFC60};
    for(unsigned i=0;i<3;++i){image[sites[i]]=0xE8;Put<std::int32_t>(image+sites[i],1,static_cast<std::int32_t>(targets[i]-sites[i]-5));}
    auto* col=image+0x10000;Put<void*>(image+kBatteryVtable-8,0,col);Put<unsigned>(col,0,1);Put<unsigned>(col,12,0x10100);Put<unsigned>(col,20,0x10000);
    constexpr char batteryName[]=".?AVDeiroiFootParts_Battery@@";std::memcpy(image+0x10100+16,batteryName,sizeof(batteryName));
    Put<void*>(image+kBatteryVtable,5*8,image+0x32B210);Put<void*>(image+kBatteryVtable,7*8,image+0x54A000);
    CHECK(InstallDeiroiControl());CHECK(InstallDeiroiControl());CHECK(DeiroiWeaponAimReady());
    fixtureNetReady=false;CHECK(!DeiroiWeaponAimReady());CHECK(!InstallDeiroiControl());fixtureNetReady=true;CHECK(DeiroiWeaponAimReady());
    image[kPoseCall+1]^=1;CHECK(!DeiroiWeaponAimReady());image[kPoseCall+1]^=1;CHECK(DeiroiWeaponAimReady());
    Jump(kMainSet,reinterpret_cast<void*>(&FakeMainSet));
    Jump(kSet,reinterpret_cast<void*>(&FakeSet));Jump(kIsState,reinterpret_cast<void*>(&FakeState));
    Jump(0x3296E0,reinterpret_cast<void*>(&FakePose));Jump(0x6CFC60,reinterpret_cast<void*>(&FakeWeapon));Jump(0x6BB5A0,reinterpret_cast<void*>(&FakeLocator));
    auto lease=Setup();CHECK(DeiroiGunCount(lease)==3);
    unsigned completeCount=0;CHECK(ReadDeiroiGunCount(lease,&completeCount)&&completeCount==3);
    enemy_control::Ability state{};state.id=123;state.cooldownKnown=true;state.name[0]=L'X';
    CHECK(ReadDeiroiAbilityState(lease,1,true,&state)&&state.state==enemy_control::AbilityState::active&&!state.cooldownKnown&&state.id==123&&state.name[0]==L'X');
    CHECK(ReadDeiroiAbilityState(lease,1,false,&state)&&state.state==enemy_control::AbilityState::native);
    Put<void*>(parts[2].b,0,image+0x17AB4B0);CHECK(ReadDeiroiGunCount(lease,&completeCount)&&completeCount==2);
    Put<void*>(parts[2].b,0,image+0x17AB118+8);CHECK(DeiroiGunCount(lease)==~0u);Put<void*>(parts[2].b,0,image+0x17AB118);
    Put<int>(parts[2].b,0xB30,9);CHECK(DeiroiGunCount(lease)==~0u);Put<int>(parts[2].b,0xB30,0);
    for(auto& part:parts)part.b[0x2E8]=1;CHECK(ReadDeiroiGunCount(lease,&completeCount)&&completeCount==0&&DeiroiGunCount(lease)==0);
    for(auto& part:parts)part.b[0x2E8]=0;
    unsigned key=999;CHECK(DeiroiGunOrdinal(lease,0,&key)&&key==0);CHECK(DeiroiGunOrdinal(lease,1,&key)&&key==1);
    CHECK(DeiroiGunOrdinal(lease,2,&key)&&key==0x10000);CHECK(!DeiroiGunOrdinal(lease,3,&key));
    parts[0].b[0x2E8]=1;CHECK(DeiroiGunCount(lease)==2);CHECK(DeiroiGunOrdinal(lease,0,&key)&&key==1);CHECK(DeiroiGunOrdinal(lease,1,&key)&&key==0x10000);parts[0].b[0x2E8]=0;
    children[0]=other.b;CHECK(DeiroiGunCount(lease)==2);children[0]=parts[2].b;
    Put<void*>(parts[1].b,0x5C0,other.b);CHECK(DeiroiGunCount(lease)==~0u);CHECK(!ReadDeiroiGunCount(lease,&completeCount)&&completeCount==0);Put<void*>(parts[1].b,0x5C0,owner.b);
    Put<void*>(parts[1].b,0x38,other.b);CHECK(DeiroiGunCount(lease)==~0u);Put<void*>(parts[1].b,0x38,owner.b);
    Put<void*>(controllers[0],0,other.b);CHECK(DeiroiGunCount(lease)==~0u);Put<void*>(controllers[0],0,owner.b);
    Put<std::size_t>(controllers[0],0x20,129);CHECK(DeiroiGunCount(lease)==~0u);Put<std::size_t>(controllers[0],0x20,2);
    auto fake=*lease.profile;lease.profile=&fake;CHECK(DeiroiGunCount(lease)==~0u);lease.profile=&fixtureProfile;
    CHECK(UpdateDeiroiAim(lease,1.5707963268f,0));CHECK(!UpdateDeiroiAim(lease,0,std::numeric_limits<float>::quiet_NaN()));
    image[kPoseCall+1]^=1;
    CHECK(!UpdateDeiroiAim(lease,0,0));CHECK(!ExecuteDeiroiAbility(lease,1,false,99,99));
    image[kPoseCall+1]^=1;CHECK(UpdateDeiroiAim(lease,1.5707963268f,0));
    unsigned mainCount=99;CHECK(ReadDeiroiMainGunCount(lease,&mainCount)&&mainCount==2);CHECK(DeiroiGunManual(lease,0x200001));CHECK(!DeiroiGunManual(lease,0x200002));
    NativeLease mainRoot{};unsigned mainKey=0;void* mainActor=nullptr;void* mainHolder=nullptr;
    CHECK(ResolveDeiroiControlledHolder(mainRows+0x290+0x100,&mainRoot,&mainKey)&&mainRoot.object==owner.b&&mainKey==0x200001);
    CHECK(ResolveDeiroiControlledHolderKey(lease,0x200001,&mainActor,&mainHolder)&&mainActor==owner.b&&mainHolder==mainRows+0x390);
    CHECK(ResolveDeiroiControlledHolder(parts[1].b+kHolder,&mainRoot,&mainKey)&&mainKey==1);
    CHECK(!ResolveDeiroiControlledHolder(mainRows+0x101,&mainRoot,&mainKey)); // interior/forged holder is not a native row
    Put<std::size_t>(owner.b,0x1648,3);CHECK(!ReadDeiroiMainGunCount(lease,&mainCount)&&mainCount==0);Put<std::size_t>(owner.b,0x1648,2);
    Put<void*>(mainRows,0,other.b);CHECK(!ReadDeiroiMainGunCount(lease,&mainCount));Put<void*>(mainRows,0,owner.b);
    Put<void*>(mainRows,0xB8,other.b);CHECK(!ReadDeiroiMainGunCount(lease,&mainCount));Put<void*>(mainRows,0xB8,mainRows+0x10);
    Put<void*>(mainRows,0xC0,other.b);CHECK(!ReadDeiroiMainGunCount(lease,&mainCount));Put<void*>(mainRows,0xC0,mainRows);
    fixtureContextKnown=false;CHECK(!ExecuteDeiroiAbility(lease,1,false,100,100));
    fixtureContextKnown=true;fixtureOnline=true;CHECK(!ExecuteDeiroiAbility(lease,1,false,101,101));fixtureOnline=false;
    parts[2].b[0x639]=1;CHECK(ReadDeiroiAbilityState(lease,0x10000,false,&state)&&state.state==enemy_control::AbilityState::unavailable);CHECK(!ExecuteDeiroiAbility(lease,0x10000,false,1,90)&&!Controlled(parts[2].b));parts[2].b[0x639]=0;
    Put<void*>(parts[2].b,kMachine+0x18,image+kFire);
    CHECK(ReadDeiroiAbilityState(lease,0x10000,false,&state)&&state.state==enemy_control::AbilityState::cooldown);
    CHECK(!ExecuteDeiroiAbility(lease,0x10000,false,2,91)&&!Controlled(parts[2].b));
    Put<void*>(parts[2].b,kMachine+0x18,image+kIdle);
    nativeFault=true;CHECK(!ExecuteDeiroiAbility(lease,0x10000,false,3,92)&&!Controlled(parts[2].b));
    CHECK(At<const void*>(parts[2].b,kMachine+0x18)==image+kIdle&&At<LONG>(parts[2].control,8)==1&&At<LONG>(owner.control,8)==1);
    Put<int>(parts[1].b,0xB00,999); // AI random delay is not a weapon reload clock
    CHECK(ExecuteDeiroiAbility(lease,1,false,1,123));CHECK(At<int>(parts[1].b,0xB00)==999);
    CHECK(At<const void*>(parts[1].b,kMachine+0x18)==image+kCharge);CHECK(DeiroiGunManual(lease,1));
    CHECK(ReadDeiroiAbilityState(lease,1,false,&state)&&state.state==enemy_control::AbilityState::active);
    CHECK(ReadDeiroiAbilityState(lease,1,true,&state)&&state.state==enemy_control::AbilityState::native);
    CHECK(At<std::uint64_t>(owner.b,0x3E8)==123&&At<std::uint64_t>(owner.b,0x490)==123&&At<std::uint64_t>(parts[1].b,0x3E8)==123);
    CHECK(At<const void*>(parts[0].b,kMachine+0x18)==image+kIdle);CHECK(At<LONG>(owner.control,8)==1&&At<LONG>(parts[1].control,8)==1);
    const int once=setCalls;CHECK(ExecuteDeiroiAbility(lease,1,false,1,123)&&setCalls==once);CHECK(!ExecuteDeiroiAbility(lease,1,false,1,124)&&setCalls==once);
    CHECK(!ExecuteDeiroiAbility(lease,1,false,2,124)&&setCalls==once); // charging: no reset/refill shortcut
    Desc charge{image+kCharge};Auto(parts[1].b+kMachine,&charge,nullptr);CHECK(setCalls==once);
    Auto(parts[0].b+kMachine,&charge,nullptr);CHECK(setCalls==once+1); // unrelated battery native auto
    Put<void*>(parts[1].b,kMachine+0x18,image+kFire);
    alignas(16) float matrix[16];ResetFrame(matrix);Pose(parts[1].b,locator+12,0.1f);
    CHECK(poseCalls==1&&savedPoint[0]>9000&&std::fabs(savedPoint[2])<0.01f&&savedGain==0.1f);
    Weapon(parts[1].b+kHolder,matrix,42);CHECK(weaponCalls==1&&savedSeed==42);CHECK(matrix[8]>0.999f&&std::fabs(matrix[10])<0.01f);
    CHECK(std::memcmp(matrix,savedMatrix,sizeof(matrix))==0);CHECK(std::memcmp(matrix+12,locator+12,16)==0); // native sender sees same in-place matrix
    Put<int>(parts[1].b,0xB30,1);const float q=0.7071067812f;
    float spread[16]={q,0,-q,0,0,1,0,0,q,0,q,0,20,30,40,1};
    Weapon(parts[1].b+kHolder,spread,43);CHECK(spread[8]>0.70f&&spread[10]<-0.70f);CHECK(std::fabs(Dot(spread,spread+8))<1e-5f);
    float desired[3]={savedPoint[0]-spread[12],savedPoint[1]-spread[13],savedPoint[2]-spread[14]},normalized[3];
    CHECK(Direction(desired,normalized)&&std::fabs(Dot(spread+8,normalized)-q)<1e-5f); // preserves native45-degree fan
    fixtureCoreActive=true;fixtureCanonical=true;fixturePoint[0]=120;fixturePoint[1]=60;fixturePoint[2]=80;
    Pose(parts[1].b,locator+12,0.1f);CHECK(std::memcmp(savedPoint,fixturePoint,12)==0&&savedPoint[3]==1);
    fixtureCanonical=false;Pose(parts[1].b,locator+12,0.1f);CHECK(std::memcmp(savedPoint,fixturePoint,12)==0&&savedPoint[3]==1); // stale burst holds last accepted point
    fixtureCoreActive=false;
    CHECK(ExecuteDeiroiAbility(lease,1,true,3,125));CHECK(cancelCalls>=1&&At<const void*>(parts[1].b,kMachine+0x18)==image+kIdle);
    CHECK(!DeiroiGunManual(lease,1));
    const int autoAgain=setCalls;Auto(parts[1].b+kMachine,&charge,nullptr);CHECK(setCalls==autoAgain+1);
    ResetFrame(matrix);Weapon(parts[1].b+kHolder,matrix,44);CHECK(std::memcmp(matrix,locator,sizeof(matrix))==0); // restored auto: native matrix unchanged
    Put<void*>(parts[1].b,kMachine+0x18,image+kIdle);parts[1].b[0x639]=1;CHECK(!ExecuteDeiroiAbility(lease,1,false,4,126));parts[1].b[0x639]=0;
    CHECK(ExecuteDeiroiAbility(lease,1,false,5,127));Put<void*>(parts[1].b,kMachine+0x18,image+kFire);
    DWORD protection=0;CHECK(VirtualProtect(parts[1].b,0x3000,PAGE_READONLY,&protection)!=0);
    CHECK(!RestoreDeiroiControl(lease));
    CHECK(At<const void*>(parts[1].b,kMachine+0x18)==image+kFire&&At<LONG>(owner.control,12)==2&&At<LONG>(parts[1].control,12)==2);
    bool retained=false;for(const auto& g:Owner(lease,false)->guns)if(g.key==1)retained=g.owned&&g.manual;CHECK(retained);
    DWORD ignored=0;CHECK(VirtualProtect(parts[1].b,0x3000,protection,&ignored)!=0);
    CHECK(RestoreDeiroiControl(lease));CHECK(At<LONG>(owner.control,12)==1&&At<LONG>(parts[1].control,12)==1);
    ResetFrame(matrix);Weapon(parts[1].b+kHolder,matrix,45);CHECK(std::memcmp(matrix,locator,sizeof(matrix))==0);
    fixtureCoreActive=true;fixtureCanonical=false;
    CHECK(!ExecuteDeiroiAbility(lease,0x10000,false,30,300)&&!Controlled(parts[2].b)); // fresh attack may not use stale/AI point
    fixtureCanonical=true;fixturePoint[0]=90;fixturePoint[1]=100;fixturePoint[2]=110;
    fixtureFresh=false;CHECK(!ExecuteDeiroiAbility(lease,0x10000,false,32,302));
    fixtureFresh=true;fixtureFreshSequence=32;CHECK(!ExecuteDeiroiAbility(lease,0x10000,false,33,303));
    fixtureFreshSequence=34;CHECK(ExecuteDeiroiAbility(lease,0x10000,false,34,304));
    CHECK(ExecuteDeiroiAbility(lease,0x10000,true,35,305));
    fixtureRole=true;fixtureAuthority=true;fixtureQuiescing=true;
    const int beforeQuiesce=setCalls;Auto(parts[0].b+kMachine,&charge,nullptr);CHECK(setCalls==beforeQuiesce);
    fixtureQuiescing=false;fixtureAuthority=false;fixtureContextKnown=false;
    const int beforeRemote=setCalls;CHECK(ExecuteDeiroiAbility(lease,0x10000,false,36,301)&&setCalls==beforeRemote&&!Controlled(parts[2].b));
    CHECK(ApplyDeiroiAcceptedControl(lease,0x10000,0,1,301,fixturePoint)&&setCalls==beforeRemote+1&&Controlled(parts[2].b));
    CHECK(ApplyDeiroiAcceptedControl(lease,0x10000,0,1,301,fixturePoint)&&setCalls==beforeRemote+1);
    CHECK(!ApplyDeiroiAcceptedControl(lease,0x10000,0,1,302,fixturePoint)&&setCalls==beforeRemote+1);
    CHECK(ApplyDeiroiAcceptedControl(lease,0x10000,3,2,302,fixturePoint)&&At<const void*>(parts[2].b,kMachine+0x18)==image+kFire);
    CHECK(ApplyDeiroiAcceptedControl(lease,0x10000,1,3,303,fixturePoint)&&!Controlled(parts[2].b)&&At<const void*>(parts[2].b,kMachine+0x18)==image+kIdle);
    CHECK(ApplyDeiroiAcceptedControl(lease,0x10000,5,4,0x123456789ABCDEFull,fixturePoint));
    CHECK(At<std::uint64_t>(parts[2].b,0x3E8)==0x123456789ABCDEFull); // nativeIdle consumes exactly one original RNG sample
    CHECK(RestoreDeiroiControl(lease));fixtureRole=false;fixtureAuthority=true;fixtureCoreActive=false;fixtureCanonical=false;fixtureContextKnown=true;
    CHECK(UpdateDeiroiAim(lease,0,0));
    const std::uint64_t mainRng=At<std::uint64_t>(owner.b,0x3E8);
    CHECK(ApplyDeiroiAcceptedControl(lease,0x200001,6,1,567,fixturePoint)&&mainSetCalls==1&&At<const void*>(mainRows+0x290,0xC8)==image+kMainFire);
    CHECK(At<std::uint64_t>(owner.b,0x3E8)==mainRng); // separate root producer RNG must never rewind
    CHECK(ApplyDeiroiAcceptedControl(lease,0x200001,6,1,567,fixturePoint)&&mainSetCalls==1);
    CHECK(!ApplyDeiroiAcceptedControl(lease,0x200001,6,1,568,fixturePoint));
    CHECK(!ApplyDeiroiAcceptedControl(lease,0x200001,0,2,567,fixturePoint));
    CHECK(!ApplyDeiroiAcceptedControl(lease,1,6,2,567,fixturePoint));
    CHECK(ApplyDeiroiAcceptedControl(lease,0x200001,7,2,0,fixturePoint)&&mainSetCalls==2&&At<const void*>(mainRows+0x290,0xC8)==image+kMainIdle);
    CHECK(ApplyDeiroiAcceptedControl(lease,0x200000,6,3,0,fixturePoint)&&mainSetCalls==3);
    CHECK(VirtualProtect(mainRows,0x1000,PAGE_READONLY,&protection)!=0);MemoryTick();
    CHECK(!RestoreDeiroiControl(lease)&&At<const void*>(mainRows,0xC8)==image+kMainFire&&At<LONG>(owner.control,12)==2);
    CHECK(VirtualProtect(mainRows,0x1000,protection,&ignored)!=0);MemoryTick();
    owner.b[0x2E8]=1;CHECK(ResolveDeiroiControlledHolderKey(lease,0x200000,&mainActor,&mainHolder)&&mainHolder==mainRows+0x100);
    CHECK(ApplyDeiroiAcceptedControl(lease,0x200001,6,4,0,fixturePoint)&&mainSetCalls==3); // retained death drains without a new state
    owner.b[0x2E8]=0;CHECK(RestoreDeiroiControl(lease)&&mainSetCalls==4&&At<const void*>(mainRows,0xC8)==image+kMainIdle);
    CHECK(UpdateDeiroiAim(lease,0,0));mainFault=true;
    CHECK(!ApplyDeiroiAcceptedControl(lease,0x200000,6,9,0,fixturePoint)&&At<const void*>(mainRows,0xC8)==image+kMainFire);
    CHECK(Owner(lease,false)->main[0].owned);
    CHECK(RestoreDeiroiControl(lease)&&At<const void*>(mainRows,0xC8)==image+kMainIdle&&At<LONG>(owner.control,12)==1);
    CHECK(ReadDeiroiAbilityState(lease,0xFFFF,false,&state)&&state.state==enemy_control::AbilityState::unavailable);
    CHECK(!ExecuteDeiroiAbility(lease,0xFFFF,false,6,128));CHECK(!ExecuteDeiroiAbility(lease,1,false,0,128));
    float opposite[3]={0,0,-1},forward[3]={0,0,1};ResetFrame(matrix);CHECK(Rotate(matrix,forward,opposite,locator)&&matrix[10]<-0.999f);
    float zero[3]{};ResetFrame(matrix);CHECK(!Rotate(matrix,zero,opposite,locator)&&std::memcmp(matrix,locator,sizeof(matrix))==0);
    CHECK(UpdateDeiroiAim(lease,0,0));Put<void*>(parts[2].b,kMachine+0x18,image+kIdle);
    CHECK(ExecuteDeiroiAbility(lease,0x10000,false,20,140));owner.b[0x2E8]=1;
    void* retainedChild=nullptr;CHECK(ResolveDeiroiRetainedGunKey(lease,0x10000,&retainedChild)&&retainedChild==parts[2].b);
    CHECK(ApplyDeiroiAcceptedControl(lease,0x10000,2,77,999,fixturePoint)); // consume dead phase without entering a new state
    CHECK(RestoreDeiroiControl(lease)&&At<LONG>(owner.control,12)==1&&At<LONG>(parts[2].control,12)==1);
    CHECK(At<const void*>(parts[2].b,kMachine+0x18)==image+kIdle);
    owner.b[0x2E8]=0;CHECK(UpdateDeiroiAim(lease,0,0));
    CHECK(VirtualProtect(owner.control,0x1000,PAGE_READONLY,&protection)!=0);
    CHECK(!RestoreDeiroiControl(lease)&&At<LONG>(owner.control,12)==2);
    CHECK(VirtualProtect(owner.control,0x1000,protection,&ignored)!=0);
    CHECK(RestoreDeiroiControl(lease)&&At<LONG>(owner.control,12)==1);
    CHECK(UpdateDeiroiAim(lease,0,0)&&RestoreDeiroiControl(lease));
    CHECK(UpdateDeiroiAim(lease,0,0));Put<void*>(parts[2].b,kMachine+0x18,image+kIdle);
    reenter=true;sawTransition=false;CHECK(ExecuteDeiroiAbility(lease,0x10000,false,300,300));
    CHECK(sawTransition&&!reentrantRestore&&!reentrantExecute&&!DeiroiNativeTransitionActive(lease));
    CHECK(RestoreDeiroiControl(lease));
    CHECK(UpdateDeiroiAim(lease,0,0));reenter=true;sawTransition=false;
    CHECK(ApplyDeiroiAcceptedControl(lease,0x200000,6,300,0,fixturePoint));
    CHECK(sawTransition&&!reentrantRestore&&!reentrantExecute&&!DeiroiNativeTransitionActive(lease));
    CHECK(RestoreDeiroiControl(lease)&&At<LONG>(owner.control,12)==1);
    CHECK(UpdateDeiroiAim(lease,0,0));fixtureNetFlight=true;
    CHECK(!RestoreDeiroiControl(lease)&&At<LONG>(owner.control,12)==2);
    CHECK(!ExecuteDeiroiAbility(lease,0x10000,false,301,301));
    fixtureNetFlight=false;CHECK(RestoreDeiroiControl(lease)&&At<LONG>(owner.control,12)==1);
    Put<LONG>(owner.control,8,0);CHECK(DeiroiGunCount(lease)==~0u);CHECK(!ExecuteDeiroiAbility(lease,1,false,7,129));
    for(auto& part:parts){VirtualFree(part.b,0,MEM_RELEASE);VirtualFree(part.control,0,MEM_RELEASE);}
    VirtualFree(mainRows,0,MEM_RELEASE);
    VirtualFree(owner.b,0,MEM_RELEASE);VirtualFree(owner.control,0,MEM_RELEASE);VirtualFree(other.b,0,MEM_RELEASE);VirtualFree(other.control,0,MEM_RELEASE);
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("enemy Deiroi control: %d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
