// Production accepted-child stream against isolated native factory/state seams.
#define ENEMY_CONTROL_DEIROI_NET_TEST
#include "../src/enemy_control_deiroi_net.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <vector>
namespace crew {unsigned char* image=nullptr;}
namespace {
using namespace crew;using namespace crew::enemy_native;using namespace crew::enemy_native::deiroi_net;
constexpr NativeProfile profile{"Deiroi",0x17AA648,0x31ED90,0x31E680,1,52,0x30,Kind::deiroi,3,4};
struct alignas(16) Body {unsigned char object[0x2000]{},control[32]{},parameter[0x500]{};};
Body owners[2],guns[2][33],foreign;unsigned activeGunCount=2,gunKeys[33]{};
alignas(16) unsigned char mainRows[2][0x290]{},mainParameters[2][0x300]{};unsigned mainCounts[2]{};
NativeGunFactoryProvider registeredGun{};NativeUnitFactoryProvider registeredUnit{};bool dispatchReady=true;
NativeLease leases[2]{};bool roles[2]={true,true},explicitTransition=false,refuse=false,throwAfter=false;
bool offlineMode=false,manualGun=true,missingLock=false,expiredLock=false,staleAiTarget=false,protectLockAfterCreate=false,reenterLifecycle=false,reenterReplay=false,sourceTransition=false;const void* reentryWire=nullptr;std::size_t reentryWireSize=0;unsigned reentryChecks=0;DWORD lockOldProtection=0;unsigned targetReleases=0,registrations=0,unitSuccessCalls=0;
unsigned char canonicalTarget[32]{};std::uint32_t canonicalOrdinal=2;unsigned lastEntityTeam=0;void* lastEntityTarget=nullptr;
unsigned checks=0,failures=0,factoryCalls=0,stateSends=0,stockShots=0,stockSets=0,stockSends=0,controlApplies=0;
bool lastFirstShot=false;std::uint64_t lastSeed=0;unsigned last90=0,last94=0,lastActual[6]{};float lastMatrix[16]{};
void Check(bool value,const char* what,int line){++checks;if(!value){++failures;std::printf("FAIL %d: %s\n",line,what);if(failures>=20)std::exit(1);}}
#define CHECK(x) Check((x),#x,__LINE__)
void __fastcall Dispose(void*){}
void* refVtable[]={reinterpret_cast<void*>(&Dispose),reinterpret_cast<void*>(&Dispose)};
int OwnerIndex(const void* object){for(int i=0;i<2;++i)if(object==owners[i].object)return i;return -1;}
bool GunIndex(const void* object,unsigned* owner,unsigned* key){for(unsigned i=0;i<2;++i)for(unsigned j=0;j<activeGunCount;++j)if(object==guns[i][j].object){*owner=i;*key=gunKeys[j];return true;}return false;}
void RefBody(Body& body){Put<void*>(body.object,0x30,body.control);Put<void*>(body.control,0,refVtable);Put<LONG>(body.control,8,1);Put<LONG>(body.control,12,1);}
void Matrix(float* out){const float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,10,20,30,1};std::memcpy(out,matrix,sizeof(matrix));}
void Reset(){
    activeGunCount=2;for(unsigned j=0;j<33;++j)gunKeys[j]=j;
    std::memset(mainRows,0,sizeof(mainRows));std::memset(mainParameters,0,sizeof(mainParameters));mainCounts[0]=mainCounts[1]=0;dispatchReady=true;
    std::memset(owners,0,sizeof(owners));std::memset(guns,0,sizeof(guns));std::memset(&foreign,0,sizeof(foreign));
    roles[0]=roles[1]=true;explicitTransition=false;refuse=false;throwAfter=false;
    offlineMode=false;manualGun=true;missingLock=expiredLock=staleAiTarget=protectLockAfterCreate=reenterLifecycle=reenterReplay=sourceTransition=false;reentryWire=nullptr;reentryWireSize=0;reentryChecks=0;targetReleases=registrations=unitSuccessCalls=0;
    std::memset(canonicalTarget,0,32);canonicalTarget[0]=4;canonicalTarget[24]=88;canonicalOrdinal=2;
    factoryCalls=stateSends=stockShots=stockSets=stockSends=controlApplies=0;
    for(unsigned i=0;i<2;++i){RefBody(owners[i]);leases[i]={};leases[i].object=owners[i].object;leases[i].control=owners[i].control;leases[i].profile=&profile;
        for(unsigned j=0;j<33;++j){auto& gun=guns[i][j];RefBody(gun);Put<void*>(gun.object,0,image+kBatteryVtable);Put<void*>(gun.object,0x38,owners[i].object);Put<void*>(gun.object,0x5C0,owners[i].object);
            Put<void*>(gun.object,kHolder+0x20,gun.parameter);Put<void*>(gun.parameter,0,image+0x1000);Put<void*>(gun.object,kMachine+0x18,image+0x32D0C0);Put<unsigned>(gun.parameter,0x90,100+i*10+j);Put<unsigned>(gun.parameter,0x94,200+i*10+j);
            Put<void*>(gun.parameter,0x28,image+0x2000);Put<float>(gun.parameter+0xA0,0x94,12.5f+i);Put<unsigned>(gun.parameter+0xA0,0x98,33+i);Put<float>(gun.parameter+0xA0,0x9C,440.5f+i);}

    }
    RefBody(foreign);
}
void __fastcall Factory(void* holder,const float* matrix,std::uint64_t* seed){
    ++factoryCalls;lastFirstShot=At<unsigned>(holder,0x34)==0;lastSeed=*seed;std::memcpy(lastMatrix,matrix,sizeof(lastMatrix));auto* parameter=At<unsigned char*>(holder,0x20);
    last90=At<unsigned>(parameter,0x90);last94=At<unsigned>(parameter,0x94);
    // The real attempt counter advances even if the final native factory is empty.
    const bool entity=At<const void*>(At<const void* const*>(parameter,0),8)==image+0x6CF910;
    auto* descriptor=parameter+(entity ? 0x280 : 0xA0);auto* actual=descriptor+(entity ? 0x80 : 0);void* factory=At<void*>(parameter,0x28);
    if(entity){Put<unsigned>(descriptor,0x38,900+last90);if(staleAiTarget&&!DeiroiAcceptedEntityFactoryScope(parameter)){Put<unsigned>(holder,0x34,At<unsigned>(holder,0x34)+1);return;}}
    // Native writes final per-shot words before the real Unit factory. Pattern,
    // damage and row-dependent speed intentionally differ between local peers.
    Put<unsigned>(actual,0x90,last90);Put<unsigned>(actual,0x1A0,static_cast<unsigned>(*seed));Put<unsigned>(actual,0x1A4,At<unsigned>(holder,0x34));
    if(registeredUnit.Owns(factory,descriptor))registeredUnit.Invoke(nullptr,matrix,factory,descriptor);
    Put<unsigned>(actual,0x1A4,At<unsigned>(actual,0x1A4)+1);
    if(entity)++registrations; // 6D03C2 is a same-child-ID registry operation, not another creation.
    Put<unsigned>(holder,0x34,At<unsigned>(holder,0x34)+1);*seed=*seed*0x5D588B656C078965ULL+0x269EC3ULL;
    Put<unsigned>(parameter,0x90,last90+1);Put<unsigned>(parameter,0x94,last94+1);
    if(throwAfter)RaiseException(0xE1234567,0,0,nullptr);
}
void __fastcall NativeState(void*){++stateSends;}
void __fastcall NativeStockShot(void*,void*,const float*,std::uint32_t){++stockShots;}
void __fastcall NativeStockSet(void*,std::uint8_t,void*){++stockSets;}
void __fastcall NativeStockSend(void*,const float*,std::uint32_t){++stockSends;}
void __fastcall NativeMainSet(void* machine,const void* callback,void*) {
    Put<const void*>(machine,0x18,callback);
    if(callback==image+0x31CB60)Put<unsigned>(static_cast<unsigned char*>(machine)+0x50,0x34,0);
}
bool __fastcall NativeNetwork(void*){++stockSends;return true;}
void SetupMain(unsigned index) {
    mainCounts[index]=1;Put<void*>(mainRows[index],0,owners[index].object);
    Put<const void*>(mainRows[index]+0xB0,0x18,image+0x31CDB0);Put<void*>(mainRows[index]+0x100,0x20,mainParameters[index]);
    Put<void*>(mainParameters[index],0,image+0x1000);Put<unsigned>(mainParameters[index],0x90,300+index);Put<unsigned>(mainParameters[index],0x94,400+index);Put<void*>(mainParameters[index],0x28,image+0x2000);
    Put<float>(mainParameters[index]+0xA0,0x94,72.5f+index);Put<unsigned>(mainParameters[index]+0xA0,0x98,4+index);Put<float>(mainParameters[index]+0xA0,0x9C,999.5f+index);
}
bool __fastcall NativeTick(void*){++stockSets;return false;}
bool __fastcall NativeIs(void* machine,const void* description){return At<const void*>(machine,0x18)==At<const void*>(description,0);}
void __fastcall NativeSet(void* machine,const void* description,void*){
    auto* child=static_cast<unsigned char*>(machine)-kMachine;const auto callback=At<const void*>(description,0);Put<const void*>(machine,0x18,callback);
    if(callback==image+0x32D0C0)Idle(child);else if(callback==image+0x32C750)Charge(child);else if(callback==image+0x32C880)Fire(child);else if(callback==image+0x32C6A0)Close(child);
}
void ReleaseAll(){for(unsigned i=0;i<2;++i)CHECK(AbortDeiroiAcceptedActions(leases[i]));}
void Prepare(){
    Reset();CHECK(BindDeiroiAcceptedContext(leases[0]));CHECK(BindDeiroiAcceptedContext(leases[1]));
    unsigned char bytes[512]{};std::size_t used=0;
    CHECK(CaptureDeiroiAcceptedAction(leases[0],bytes,sizeof(bytes),&used)&&used>0);
    CHECK(ApplyDeiroiAcceptedAction(leases[1],bytes,used)==Result::applied);
    factoryCalls=stateSends=stockShots=stockSets=stockSends=controlApplies=0;
}
}
namespace crew::enemy_native {
const NativeProfile* Profiles(std::size_t* count) noexcept {*count=1;return &profile;}
bool NativeActionRole(const void* root,bool* authority) noexcept {const int i=OwnerIndex(root);if(i<0 || !roles[i] || offlineMode)return false;*authority=i==0;return true;}
unsigned DeiroiGunCount(const NativeLease& lease) noexcept {return OwnerIndex(lease.object)>=0 ? activeGunCount : 0u;}
bool DeiroiGunOrdinal(const NativeLease&,unsigned index,unsigned* key) noexcept {if(index>=activeGunCount)return false;*key=gunKeys[index];return true;}
bool ResolveDeiroiControlledGun(const void* object,NativeLease* root,unsigned* key) noexcept {unsigned index=0;if(!GunIndex(object,&index,key) || !roles[index] || static_cast<const unsigned char*>(object)[0x2E8] || owners[index].object[0x2E8])return false;*root=leases[index];return true;}
bool ResolveDeiroiControlledGunKey(const NativeLease& lease,unsigned key,void** child) noexcept {const int i=OwnerIndex(lease.object);if(i<0)return false;for(unsigned j=0;j<activeGunCount;++j)if(gunKeys[j]==key){*child=guns[i][j].object;return true;}return false;}
bool ResolveDeiroiRetainedGunKey(const NativeLease& lease,unsigned key,void** child) noexcept {return ResolveDeiroiControlledGunKey(lease,key,child);}
bool DeiroiExplicitTransition(const void*) noexcept {return explicitTransition;}
bool DeiroiGunManual(const NativeLease&,unsigned) noexcept{return manualGun;}
bool DeiroiNativeTransitionActive(const NativeLease&) noexcept{return sourceTransition;}
bool ReadDeiroiMainGunCount(const NativeLease& lease,unsigned* out) noexcept {const int i=OwnerIndex(lease.object);if(i<0)return false;*out=mainCounts[i];return true;}
bool ResolveDeiroiControlledHolderKey(const NativeLease& lease,unsigned key,void** actor,void** holder) noexcept {
    const int i=OwnerIndex(lease.object);if(i<0)return false;
    if((key>>16)==32){if((key&0xFFFF)>=mainCounts[i])return false;*actor=owners[i].object;*holder=mainRows[i]+0x100;return true;}
    if(!ResolveDeiroiRetainedGunKey(lease,key,actor))return false;*holder=static_cast<unsigned char*>(*actor)+kHolder;return true;
}
bool ResolveDeiroiControlledHolder(const void* holder,NativeLease* root,unsigned* key) noexcept {
    for(unsigned i=0;i<2;++i) {
        if(!roles[i])continue;
        for(unsigned j=0;j<activeGunCount;++j)if(holder==guns[i][j].object+kHolder){*root=leases[i];*key=gunKeys[j];return true;}
        if(mainCounts[i] && holder==mainRows[i]+0x100){*root=leases[i];*key=32u<<16;return true;}
    }
    return false;
}
bool ApplyDeiroiAcceptedControl(const NativeLease& lease,unsigned key,unsigned phase,std::uint64_t,std::uint64_t,const float*) noexcept {
    ++controlApplies;void* child=nullptr;
    if((key>>16)==32){void* holder=nullptr;if(!ResolveDeiroiControlledHolderKey(lease,key,&child,&holder) || phase<6 || phase>7)return false;NativeMainSet(static_cast<unsigned char*>(holder)-0x50,image+(phase==6 ? 0x31CB60 : 0x31CDB0),nullptr);return true;}
    if(!ResolveDeiroiRetainedGunKey(lease,key,&child))return false;
    if(lease.object[0x2E8] || static_cast<unsigned char*>(child)[0x2E8])return true;
    const unsigned callbacks[]={0x32C750,0x32D0C0,0x32C750,0x32C880,0x32C6A0,0x32D0C0};
    const void* desc[2]={image+callbacks[phase],nullptr};NativeSet(static_cast<unsigned char*>(child)+kMachine,desc,nullptr);return true;
}
bool RegisterNativeGunFactoryProvider(const NativeGunFactoryProvider& provider) noexcept {registeredGun=provider;return true;}
bool RegisterNativeUnitFactoryProvider(const NativeUnitFactoryProvider& provider) noexcept {registeredUnit=provider;return true;}
bool RegisterNativeUnitFactoryCallsite(std::uint32_t rva) noexcept {return rva==0x84C5A;}
bool InstallNativeGunFactoryDispatcher() noexcept {return true;}
bool InstallNativeUnitFactoryDispatcher() noexcept {return true;}
bool NativeGunFactoryProviderReady(const NativeGunFactoryProvider& provider) noexcept {return dispatchReady&&provider.tag==registeredGun.tag&&provider.Owns==registeredGun.Owns&&provider.Invoke==registeredGun.Invoke;}
bool NativeUnitFactoryProviderReady(const NativeUnitFactoryProvider& provider) noexcept {return dispatchReady&&provider.tag==registeredUnit.tag&&provider.Owns==registeredUnit.Owns&&provider.Invoke==registeredUnit.Invoke;}
bool NativeUnitFactoryCallsiteReady(std::uint32_t rva) noexcept {return dispatchReady&&rva==0x84C5A;}
bool DeiroiEntityFactoryAimReady() noexcept{return dispatchReady;}
void InvokeOriginalNativeGunFactory(void* holder,const float* matrix,std::uint64_t* seed){Factory(holder,matrix,seed);}
void* InvokeOriginalNativeUnitFactory(void*,const float*,void*,void* descriptor){
    if(reenterReplay){reenterReplay=false;const unsigned attempts=factoryCalls;CHECK(ApplyDeiroiAcceptedAction(leases[1],reentryWire,reentryWireSize)==Result::pending);CHECK(factoryCalls==attempts);}
    if(reenterLifecycle) {
        reenterLifecycle=false;CHECK(sample&&sample->stream);auto* active=sample->stream;const int owner=OwnerIndex(active->root.object);CHECK(owner>=0);
        CHECK(active->nativeInFlight>0);CHECK(!AbortDeiroiAcceptedActions(leases[owner]));CHECK(!RestoreDeiroiAcceptedActions(leases[owner]));
        CHECK(Find(leases[owner])==active&&active->rootStrong&&active->nativeInFlight>0);++reentryChecks;
    }
    const bool entity=sample&&sample->entity;auto* actual=static_cast<unsigned char*>(descriptor)+(entity ? 0x80 : 0);
    for(unsigned i=0;i<6;++i)lastActual[i]=At<unsigned>(actual,actualOffsets[i]);
    if(entity){lastEntityTeam=At<unsigned>(descriptor,0x38);lastEntityTarget=At<void*>(descriptor,0x50);CHECK(descriptor!=sample->descriptor);}
    // Nested constructors use a DIFFERENT descriptor even if a shot scope is
    // active. It must not be mistaken for a second creation by this provider.
    CHECK(!registeredUnit.Owns(nullptr,foreign.parameter));
    if(!refuse)++unitSuccessCalls;
    if(protectLockAfterCreate){protectLockAfterCreate=false;CHECK(sample&&sample->target&&sample->target->control);CHECK(VirtualProtect(const_cast<void*>(sample->target->control),16,PAGE_READONLY,&lockOldProtection)!=0);}
    return refuse ? nullptr : foreign.object;
}
bool CaptureNativeProjectileLock(const void* weak,unsigned char id[32],std::uint32_t* ordinal) noexcept {
    std::memset(id,0,32);*ordinal=0;
    if(!At<const void*>(weak,8))return true;std::memcpy(id,canonicalTarget,32);*ordinal=canonicalOrdinal;return true;
}
bool ResolveNativeProjectileActor(const unsigned char id[32],void** object,const void** control) noexcept {
    *object=nullptr;*control=nullptr;unsigned any=0;for(unsigned i=0;i<32;++i)any|=id[i];if(!any||expiredLock)return true;
    if(missingLock)return false;*object=foreign.object;auto* targetControl=At<unsigned char*>(foreign.object,0x30);*control=targetControl;InterlockedIncrement(reinterpret_cast<volatile LONG*>(targetControl+8));return true;
}
bool ResolveNativeProjectileLock(const unsigned char id[32],std::uint32_t,void** object,const void** control) noexcept {
    *object=nullptr;*control=nullptr;unsigned any=0;for(unsigned i=0;i<32;++i)any|=id[i];if(!any||expiredLock)return true;
    if(missingLock)return false;*object=foreign.object;auto* targetControl=At<unsigned char*>(foreign.object,0x30);*control=targetControl;InterlockedIncrement(reinterpret_cast<volatile LONG*>(targetControl+8));return true;
}
bool ReleaseNativeProjectileLock(void*,const void* control,bool* consumed) noexcept {
    if(consumed)*consumed=false;if(!control)return true;if(!Readable(control,16,true))return false;
    InterlockedDecrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(const_cast<void*>(control))+8));
    if(consumed)*consumed=true;++targetReleases;return true;
}
void* DeiroiNetTestFunction(unsigned rva) noexcept {
    switch(rva){case 0x6D05C0:return reinterpret_cast<void*>(&Factory);case 0x32B790:return reinterpret_cast<void*>(&NativeState);
    case 0x6CF6C0:return reinterpret_cast<void*>(&NativeStockShot);case 0x32C180:return reinterpret_cast<void*>(&NativeStockSet);
    case 0x3211C0:return reinterpret_cast<void*>(&NativeMainSet);case 0x7748F0:return reinterpret_cast<void*>(&NativeNetwork);case 0x32B460:return reinterpret_cast<void*>(&NativeStockSend);case 0x32BA80:return reinterpret_cast<void*>(&NativeTick);case 0x32A890:return reinterpret_cast<void*>(&NativeIs);case 0x32C310:return reinterpret_cast<void*>(&NativeSet);default:return nullptr;}
}
}
namespace crew {
bool EnemyControlNetworkContext(const void* root,bool* online) noexcept {if(OwnerIndex(root)<0)return false;*online=!offlineMode;return true;}
bool EnemyControlWeaponLockIdentity(const void* root,unsigned char id[32],int* ordinal) noexcept {
    if(OwnerIndex(root)<0)return false;std::memcpy(id,canonicalTarget,32);*ordinal=canonicalOrdinal==0xFFFFFFFFu ? -1 : static_cast<int>(canonicalOrdinal);return true;
}
}
namespace {
struct TraceGun {unsigned key,total,interval,charge;bool missile;};
int DumpTrace(const char* path,unsigned logicHz,const char* configuration) {
    if(logicHz!=30 && logicHz!=60 && logicHz!=144)return 3;
    std::ifstream input(configuration);const std::string json{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    const std::regex row(R"re("key"\s*:\s*(\d+)\s*,\s*"object"\s*:\s*"([^"]+)"\s*,\s*"total"\s*:\s*(\d+)\s*,\s*"intervalFrames"\s*:\s*(\d+)\s*,\s*"chargeFrames"\s*:\s*(\d+)\s*,\s*"factoryCount"\s*:\s*(\d+))re");
    std::vector<TraceGun> config;
    for(std::sregex_iterator it(json.begin(),json.end(),row),end;it!=end;++it) {
        const auto& m=*it;if(std::stoul(m[6].str())!=1)return 4;
        config.push_back({static_cast<unsigned>(std::stoul(m[1].str())),static_cast<unsigned>(std::stoul(m[3].str())),
            static_cast<unsigned>(std::stoul(m[4].str())),static_cast<unsigned>(std::stoul(m[5].str())),m[2].str().find("Missile")!=std::string::npos});
    }
    if(config.size()!=33)return 5;unsigned missiles=0;for(const auto& gun:config)missiles+=gun.missile;
    if(missiles!=3)return 6;FILE* output=nullptr;if(fopen_s(&output,path,"wb") || !output)return 7;
    std::fprintf(output,"# production-including fixture; native allocation seam succeeds; NOT gameplay or observed native cadence\n");
    std::fprintf(output,"# source=E509_DEIROI_XL.SGO; config=%s; native_logic_updates_per_second=%u; duration_s=60; beam=30;missile=3;root_main=1\n",configuration,logicHz);
    std::fprintf(output,"# original intervalFrames preserved; logic30/60/144 are separate stress assumptions, not render FPS\n");
    Reset();activeGunCount=33;manualGun=false;std::memset(canonicalTarget,0,32);canonicalOrdinal=0xFFFFFFFFu;
    for(unsigned j=0;j<33;++j){gunKeys[j]=config[j].key;if(config[j].missile)Put<void*>(guns[0][j].parameter,0,image+0x1100);}
    SetupMain(0);if(!BindDeiroiAcceptedContext(leases[0])){std::fclose(output);return 8;}
    alignas(16) float matrix[16];Matrix(matrix);unsigned char bytes[512]{};std::uint64_t seed=12345;unsigned captured=0;
    for(unsigned frame=0;frame<logicHz*60;++frame) {
        for(unsigned j=0;j<33;++j) {
            const auto& gun=config[j];const unsigned first=gun.charge+2,last=first+(gun.total-1)*gun.interval,cycle=last+3,at=frame%cycle;
            unsigned callback=at==0 ? 0x32C750u : at==first ? 0x32C880u : at==last+1 ? 0x32C6A0u : at==last+2 ? 0x32D0C0u : 0;
            if(callback){Desc state{image+callback};NativeSet(guns[0][j].object+kMachine,&state,nullptr);}
            if(at>=first && at<=last && !((at-first)%gun.interval))Shot(guns[0][j].object+kHolder,matrix,&seed);
        }
        const unsigned mainAt=frame%182;
        if(mainAt==0)MainTransition(mainRows[0]+0xB0,image+0x31CB60,nullptr);
        if(mainAt==181){Shot(mainRows[0]+0x100,matrix,&seed);MainTransition(mainRows[0]+0xB0,image+0x31CDB0,nullptr);}
        while(!DeiroiAcceptedActionsEmpty(leases[0])) {
            std::size_t used=0;if(!CaptureDeiroiAcceptedAction(leases[0],bytes,sizeof(bytes),&used) || !used){std::fclose(output);return 9;}
            deiroi_wire::Event events[12]{};unsigned count=0,actual=0;if(!deiroi_wire::Decode(bytes,used,events,12,&count)){std::fclose(output);return 10;}
            for(unsigned i=0;i<count;++i)if(events[i].kind!=deiroi_wire::Kind::control)++actual;
            std::fprintf(output,"%llu 0 %u ",static_cast<unsigned long long>(frame)*1000000u/logicHz,actual);
            for(std::size_t i=0;i<used;++i)std::fprintf(output,"%02x",bytes[i]);std::fputc('\n',output);captured+=actual;
        }
    }
    std::fprintf(output,"# captured_actual_outer_units=%u; raw_stub_outer_successes=%u; native_post_registrations=%u\n",captured,unitSuccessCalls,registrations);
    std::fclose(output);ReleaseAll();return failures ? 11 : 0;
}
}
int main(int argc,char** argv){
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!image)return 2;
    for(const auto& signature:signatures)std::memcpy(image+signature.rva,signature.bytes,16);
    Put<void*>(image+0x1000,8,image+0x6CF7D0);Put<void*>(image+0x1000,16,image+0x6CFEF0);
    Put<void*>(image+0x1100,8,image+0x6CF910);Put<void*>(image+0x1100,16,image+0x6CFF00);Put<void*>(image+0x1100,24,image+0x6D0470);
    for(unsigned i=0;i<18;++i){image[sites[i]]=opcodes[i];Put<int>(image+sites[i],1,static_cast<int>(targets[i]-sites[i]-5));}
    CHECK(InstallDeiroiAcceptedActions());CHECK(InstallDeiroiAcceptedActions());CHECK(DeiroiAcceptedActionsReady());
    dispatchReady=false;CHECK(!DeiroiAcceptedActionsReady());dispatchReady=true;
    const auto savedRelay=At<int>(image+sites[0],1);Put<int>(image+sites[0],1,0);CHECK(!DeiroiAcceptedActionsReady());Put<int>(image+sites[0],1,savedRelay);CHECK(DeiroiAcceptedActionsReady());
    if(argc==5 && !std::strcmp(argv[1],"--dump-trace"))return DumpTrace(argv[2],static_cast<unsigned>(std::strtoul(argv[3],nullptr,10)),argv[4]);
    Reset();Charge(guns[0][0].object);CHECK(At<LONG>(owners[0].control,8)==2&&At<LONG>(guns[0][0].control,8)==2);ReleaseAll();
    Prepare();CHECK(At<LONG>(owners[0].control,8)==2 && At<LONG>(guns[0][0].control,8)==2);
    alignas(16) float matrix[16];Matrix(matrix);std::uint64_t seed=77;unsigned char wire[512]{};std::size_t used=0;
    explicitTransition=true;Charge(guns[0][0].object);explicitTransition=false;CHECK(stateSends==0);
    float point[3]={1000,2000,3000};CHECK(QueueDeiroiAcceptedControl(leases[0],0,0,9,123,point));
    Fire(guns[0][0].object);Shot(guns[0][0].object+kHolder,matrix,&seed);CHECK(factoryCalls==1&&At<unsigned>(guns[0][0].object,kHolder+0x34)==1);
    CHECK(stateSends==0);CHECK(!DeiroiAcceptedActionsEmpty(leases[0]));CHECK(!RestoreDeiroiAcceptedActions(leases[0]));
    CHECK(CaptureDeiroiAcceptedAction(leases[0],wire,sizeof(wire),&used)&&used>0);CHECK(OwnsDeiroiAcceptedAction(wire,used));
    deiroi_wire::Event events[12]{};unsigned count=0;CHECK(deiroi_wire::Decode(wire,used,events,12,&count)&&count==3);
    CHECK(events[0].phase==0&&events[1].phase==3&&events[2].seed==77&&events[2].rng90==100&&events[2].rng94==200);
    CHECK(events[2].actual[0]==100&&events[2].actual[2]==33&&events[2].actual[4]==77&&events[2].actual[5]==0);
    CHECK(!registeredUnit.Owns(nullptr,guns[0][0].parameter+0xA0));
    const auto native90=At<unsigned>(guns[1][0].parameter,0x90),native94=At<unsigned>(guns[1][0].parameter,0x94);
    CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::applied);CHECK(factoryCalls==2&&lastSeed==77&&last90==100&&last94==200);
    CHECK(At<unsigned>(guns[1][0].parameter,0x90)==native90&&At<unsigned>(guns[1][0].parameter,0x94)==native94);
    CHECK(std::memcmp(lastActual,events[2].actual,sizeof(lastActual))==0);
    CHECK(At<float>(guns[1][0].parameter+0xA0,0x94)==13.5f&&At<unsigned>(guns[1][0].parameter+0xA0,0x98)==34&&At<float>(guns[1][0].parameter+0xA0,0x9C)==441.5f);
    CHECK(std::memcmp(matrix,lastMatrix,sizeof(matrix))==0);CHECK(controlApplies==2);
    CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::applied&&factoryCalls==2&&controlApplies==2);
    events[2].seed=78;std::size_t conflicting=0;CHECK(deiroi_wire::Encode(events,count,wire,sizeof(wire),&conflicting));
    CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,conflicting)==Result::failed&&factoryCalls==2);
    seed=0;const auto remoteBefore=At<unsigned>(guns[1][0].object,kHolder+0x34);
    for(unsigned mode=0;mode<3;++mode){Put<unsigned>(guns[1][0].object,0xD00,mode);Shot(guns[1][0].object+kHolder,matrix,&seed);}
    CHECK(factoryCalls==2&&At<unsigned>(guns[1][0].object,kHolder+0x34)==remoteBefore);
    StockShot(guns[1][0].object+kHolder,nullptr,matrix,1);StockState(guns[1][0].object+kMachine,1,nullptr);StockSend(guns[0][0].object,matrix,1);
    CHECK(stockShots==0&&stockSets==0&&stockSends==0);
    CHECK(!Tick(guns[1][0].object)&&stockSets==0);
    Desc target{image+0x32C6A0};const auto previousState=At<const void*>(guns[1][0].object,kMachine+0x18);
    Transition(guns[1][0].object+kMachine,&target,nullptr);CHECK(At<const void*>(guns[1][0].object,kMachine+0x18)==previousState);
    guns[1][0].object[0x639]=1;Transition(guns[1][0].object+kMachine,&target,nullptr);CHECK(At<const void*>(guns[1][0].object,kMachine+0x18)==target.function);guns[1][0].object[0x639]=0;CHECK(!Tick(guns[0][0].object)&&stockSets==1);
    StateFn unboundState=&NativeState;(void)unboundState;Charge(foreign.object);CHECK(stateSends==1);
    StockSend(foreign.object,matrix,1);CHECK(stockSends==1);
    CHECK(QuiesceDeiroiAcceptedActions(leases[0]));CHECK(DeiroiAcceptedQuiescing(owners[0].object));CHECK(Tick(guns[0][0].object)&&stockSets==1);
    const unsigned once=factoryCalls;Shot(guns[0][0].object+kHolder,matrix,&seed);CHECK(factoryCalls==once);
    CHECK(DeiroiAcceptedActionsEmpty(leases[0]));ReleaseAll();CHECK(At<LONG>(owners[0].control,8)==1&&At<LONG>(guns[0][0].control,8)==1&&At<LONG>(owners[0].control,12)==1);
    // A constructor callback cannot retire/reuse the stream while native is
    // still executing with its TLS pointer, for source, guest or offline roots.
    Prepare();reenterLifecycle=true;Shot(guns[0][0].object+kHolder,matrix,&seed);CHECK(reentryChecks==1&&Find(leases[0])->nativeInFlight==0);
    CHECK(QuiesceDeiroiAcceptedActions(leases[0]));CHECK(CaptureDeiroiAcceptedAction(leases[0],wire,512,&used)&&used>0);
    reenterLifecycle=true;reenterReplay=true;reentryWire=wire;reentryWireSize=used;CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::applied&&reentryChecks==2&&Find(leases[1])->nativeInFlight==0);ReleaseAll();
    Reset();offlineMode=true;CHECK(BindDeiroiAcceptedContext(leases[0]));reenterLifecycle=true;Shot(guns[0][0].object+kHolder,matrix,&seed);
    CHECK(reentryChecks==1&&Find(leases[0])->nativeInFlight==0&&DeiroiAcceptedActionsEmpty(leases[0]));ReleaseAll();
    Prepare();sourceTransition=true;CHECK(!RestoreDeiroiAcceptedActions(leases[0]));CHECK(!AbortDeiroiAcceptedActions(leases[0]));
    CHECK(!DeiroiAcceptedActionsEmpty(leases[0])&&Find(leases[0])->rootStrong);sourceTransition=false;
    CHECK(QuiesceDeiroiAcceptedActions(leases[0]));ReleaseAll();
    // Whole accepted batch preflight is mutation-free if a destination is missing.
    Prepare();Shot(guns[0][0].object+kHolder,matrix,&seed);CHECK(CaptureDeiroiAcceptedAction(leases[0],wire,512,&used));
    auto* stream=Find(leases[1]);CHECK(stream!=nullptr);stream->parts[0].key=7;
    const unsigned beforePending=factoryCalls;const auto appliedBeforePending=stream->applied;CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::pending&&factoryCalls==beforePending&&stream->applied==appliedBeforePending);
    stream->parts[0].key=0;CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::applied&&factoryCalls==beforePending+1);
    ReleaseAll();
    // Late real emissions retain actual dead native context, never a replacement actor.
    Prepare();seed=0;Shot(guns[0][1].object+kHolder,matrix,&seed);owners[0].object[0x2E8]=guns[0][1].object[0x2E8]=1;roles[0]=false;
    CHECK(CaptureDeiroiAcceptedAction(leases[0],wire,512,&used));owners[1].object[0x2E8]=guns[1][1].object[0x2E8]=1;
    CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::applied&&lastSeed==0);CHECK(At<unsigned>(guns[1][1].object,kHolder+0x34)==1);
    ReleaseAll();CHECK(At<LONG>(owners[1].control,8)==1&&At<LONG>(guns[1][1].control,8)==1);
    // Mid-burst acquisition initializes Fire effects before replay and keeps the
    // real source shot index: shot 40 must never become another first-shot event.
    Reset();Put<const void*>(guns[0][0].object,kMachine+0x18,image+0x32C880);Put<unsigned>(guns[0][0].object,kHolder+0x34,40);
    CHECK(BindDeiroiAcceptedContext(leases[0]));CHECK(BindDeiroiAcceptedContext(leases[1]));
    seed=111;Shot(guns[0][0].object+kHolder,matrix,&seed);CHECK(CaptureDeiroiAcceptedAction(leases[0],wire,512,&used));
    CHECK(deiroi_wire::Decode(wire,used,events,12,&count)&&count==3&&events[0].phase==3&&events[2].shotIndex==40);
    CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::applied&&!lastFirstShot);
    CHECK(At<unsigned>(guns[1][0].object,kHolder+0x34)==41);ReleaseAll();
    // Main row mode0 has no stock projectile receiver. It uses the SAME actual
    // factory stream, but its actor identity is root and its holder is row+100.
    Reset();SetupMain(0);SetupMain(1);CHECK(BindDeiroiAcceptedContext(leases[0]));CHECK(BindDeiroiAcceptedContext(leases[1]));
    CHECK(At<LONG>(owners[0].control,8)==3);CHECK(CaptureDeiroiAcceptedAction(leases[0],wire,512,&used));CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::applied);
    CHECK(!MainNetwork(owners[0].object+0x120)&&!MainNetwork(owners[1].object+0x120));
    MainTransition(mainRows[0]+0xB0,image+0x31CB60,nullptr);CHECK(At<const void*>(mainRows[0]+0xB0,0x18)==image+0x31CB60);
    MainTransition(mainRows[1]+0xB0,image+0x31CB60,nullptr);CHECK(At<const void*>(mainRows[1]+0xB0,0x18)==image+0x31CDB0);
    Put<unsigned>(mainRows[0]+0x100,0x34,40);seed=4321;Shot(mainRows[0]+0x100,matrix,&seed);
    CHECK(CaptureDeiroiAcceptedAction(leases[0],wire,512,&used));CHECK(deiroi_wire::Decode(wire,used,events,12,&count)&&count==2&&events[0].phase==6&&events[1].key==(32u<<16));
    CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::applied&&At<unsigned>(mainRows[1]+0x100,0x34)==41&&!lastFirstShot);
    CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::applied&&At<unsigned>(mainRows[1]+0x100,0x34)==41);
    CHECK(QuiesceDeiroiAcceptedActions(leases[0]));CHECK(CaptureDeiroiAcceptedAction(leases[0],wire,512,&used));CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::applied);
    CHECK(At<const void*>(mainRows[1]+0xB0,0x18)==image+0x31CDB0);ReleaseAll();CHECK(At<LONG>(owners[0].control,8)==1);
    // TypeEntity Missile preserves its true lock point and nested constructor,
    // while the original same-child-ID registration executes once on each peer.
    Prepare();Put<void*>(guns[0][0].parameter,0,image+0x1100);Put<void*>(guns[1][0].parameter,0,image+0x1100);
    Put<float>(guns[0][0].parameter+0x300,0x94,18.5f);Put<float>(guns[0][0].parameter+0x300,0x9C,818.5f);Put<unsigned>(guns[0][0].parameter+0x300,0x98,7);
    staleAiTarget=true;seed=6789;Shot(guns[0][0].object+kHolder,matrix,&seed);CHECK(factoryCalls==1&&registrations==1&&lastEntityTarget==foreign.object);
    CHECK(CaptureDeiroiAcceptedAction(leases[0],wire,512,&used)&&used==169);CHECK(deiroi_wire::Decode(wire,used,events,12,&count)&&count==1&&events[0].kind==deiroi_wire::Kind::entity);
    CHECK(events[0].entityTeam==1000&&events[0].lockOrdinal==2&&!std::memcmp(events[0].targetActorId,canonicalTarget,32));
    missingLock=true;const auto beforeMissing=factoryCalls;CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::pending&&factoryCalls==beforeMissing);
    missingLock=false;CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::applied&&registrations==2&&lastEntityTeam==1000&&lastEntityTarget==foreign.object);
    CHECK(std::memcmp(lastActual,events[0].actual,sizeof(lastActual))==0&&At<LONG>(foreign.control,8)==1);
    CHECK(!At<void*>(guns[1][0].parameter+0x280,0x50));ReleaseAll();
    // A real projectile is already accepted when a later target release is
    // temporarily unwritable. Its persistent strong ledger must survive closing.
    Prepare();Put<void*>(guns[0][0].parameter,0,image+0x1100);Put<void*>(guns[1][0].parameter,0,image+0x1100);
    auto* dedicatedControl=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));CHECK(dedicatedControl!=nullptr);
    Put<void*>(dedicatedControl,0,refVtable);Put<LONG>(dedicatedControl,8,1);Put<LONG>(dedicatedControl,12,1);Put<void*>(foreign.object,0x30,dedicatedControl);
    protectLockAfterCreate=true;Shot(guns[0][0].object+kHolder,matrix,&seed);auto* lockedStream=Find(leases[0]);
    CHECK(lockedStream->faulted&&lockedStream->count==1&&lockedStream->targets[0].held&&lockedStream->targets[0].actorHeld&&At<LONG>(dedicatedControl,8)==3);
    DWORD lockIgnored=0;CHECK(VirtualProtect(dedicatedControl,16,lockOldProtection,&lockIgnored)!=0);
    Sleep(20); // memory-region admission is cached for one native tick
    const bool lockQuiesced=QuiesceDeiroiAcceptedActions(leases[0]);
    CHECK(lockQuiesced&&!lockedStream->targets[0].held&&At<LONG>(dedicatedControl,8)==1);
    CHECK(CaptureDeiroiAcceptedAction(leases[0],wire,512,&used)&&used>0);CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::applied);
    ReleaseAll();Put<void*>(foreign.object,0x30,foreign.control);CHECK(VirtualFree(dedicatedControl,0,MEM_RELEASE)!=0);
    // A legal blind shot has no fabricated actor or LockPoint.
    Prepare();Put<void*>(guns[0][0].parameter,0,image+0x1100);std::memset(canonicalTarget,0,32);canonicalOrdinal=0xFFFFFFFFu;
    Shot(guns[0][0].object+kHolder,matrix,&seed);CHECK(lastEntityTarget==nullptr);CHECK(CaptureDeiroiAcceptedAction(leases[0],wire,512,&used));
    CHECK(deiroi_wire::Decode(wire,used,events,12,&count)&&events[0].lockOrdinal==0);unsigned idBits=0;for(auto byte:events[0].targetActorId)idBits|=byte;CHECK(idBits==0);ReleaseAll();
    // Restored automatic missile fire captures the original AI lock, not focus.
    Prepare();manualGun=false;Put<void*>(guns[0][0].parameter,0,image+0x1100);Put<void*>(guns[0][0].parameter+0x280,0x50,foreign.object);Put<void*>(guns[0][0].parameter+0x280,0x58,foreign.control);
    Shot(guns[0][0].object+kHolder,matrix,&seed);CHECK(CaptureDeiroiAcceptedAction(leases[0],wire,512,&used));CHECK(deiroi_wire::Decode(wire,used,events,12,&count)&&events[0].lockOrdinal==2);ReleaseAll();
    // Offline uses the same real-target constructor scope, with no wire outbox.
    Reset();offlineMode=true;Put<void*>(guns[0][0].parameter,0,image+0x1100);CHECK(BindDeiroiAcceptedContext(leases[0]));staleAiTarget=true;
    Shot(guns[0][0].object+kHolder,matrix,&seed);CHECK(factoryCalls==1&&lastEntityTarget==foreign.object&&DeiroiAcceptedActionsEmpty(leases[0]));ReleaseAll();
    // Refused native factories have no committed shot record; overflow is explicit.
    Prepare();refuse=true;Shot(guns[0][0].object+kHolder,matrix,&seed);refuse=false;CHECK(DeiroiAcceptedActionsEmpty(leases[0]));CHECK(At<unsigned>(guns[0][0].object,kHolder+0x34)==1);
    for(unsigned i=0;i<kQueued;++i)CHECK(QueueDeiroiAcceptedControl(leases[0],0,0,i+1,1,point));
    CHECK(!QueueDeiroiAcceptedControl(leases[0],0,0,kQueued+1,1,point));CHECK(DeiroiAcceptedQuiescing(owners[0].object));
    CHECK(!CaptureDeiroiAcceptedAction(leases[0],wire,512,&used));CHECK(QuiesceDeiroiAcceptedActions(leases[0]));
    unsigned drained=0;
    while(!DeiroiAcceptedActionsEmpty(leases[0])){CHECK(CaptureDeiroiAcceptedAction(leases[0],wire,512,&used));CHECK(deiroi_wire::Decode(wire,used,events,12,&count));drained+=count;}
    CHECK(drained==kQueued);CHECK(RestoreDeiroiAcceptedActions(leases[0]));CHECK(RestoreDeiroiAcceptedActions(leases[1]));
    Prepare();Shot(guns[0][0].object+kHolder,matrix,&seed);CHECK(CaptureDeiroiAcceptedAction(leases[0],wire,512,&used));
    refuse=true;CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::failed);refuse=false;CHECK(Find(leases[1])->faulted&&Find(leases[1])->applied==2);ReleaseAll();
    // A temporarily read-only retained control is mutation-free and retryable.
    Prepare();auto* retained=Find(leases[1]);CHECK(retained&&retained->rootStrong&&retained->parts[0].strong);
    const auto rootUses=At<LONG>(owners[1].control,8),partUses=At<LONG>(guns[1][0].control,8);DWORD oldProtection=0;
    CHECK(VirtualProtect(guns[1][0].control,16,PAGE_READONLY,&oldProtection)!=0);
    CHECK(!RestoreDeiroiAcceptedActions(leases[1]));CHECK(retained->rootStrong&&retained->parts[0].strong);
    CHECK(At<LONG>(owners[1].control,8)==rootUses&&At<LONG>(guns[1][0].control,8)==partUses);
    DWORD ignored=0;CHECK(VirtualProtect(guns[1][0].control,16,oldProtection,&ignored)!=0);
    CHECK(RestoreDeiroiAcceptedActions(leases[1]));CHECK(At<LONG>(owners[1].control,8)==1&&At<LONG>(guns[1][0].control,8)==1);ReleaseAll();
    // Exact real-factory failure is terminal and cannot be acknowledged/retried as a new shot.
    Prepare();throwAfter=true;Shot(guns[0][0].object+kHolder,matrix,&seed);throwAfter=false;
    CHECK(Find(leases[0])->faulted);CHECK(!CaptureDeiroiAcceptedAction(leases[0],wire,512,&used));
    CHECK(!DeiroiAcceptedActionsEmpty(leases[0])&&Find(leases[0])->count==1&&Find(leases[0])->reserved==0);
    CHECK(QuiesceDeiroiAcceptedActions(leases[0]));CHECK(CaptureDeiroiAcceptedAction(leases[0],wire,512,&used)&&used>0);
    CHECK(ApplyDeiroiAcceptedAction(leases[1],wire,used)==Result::applied);ReleaseAll();
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("Deiroi accepted stream: %u checks, %u failures\n",checks,failures);return failures ? 1 : 0;
}
