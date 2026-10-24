#define ENEMY_CONTROL_GUN_FACTORY_TEST
#include "../src/enemy_control_gun_factory.cpp"
#include <cstdio>
#include <cstdlib>
namespace crew {unsigned char* image=nullptr;}
namespace {
using namespace crew;using namespace crew::enemy_native;using namespace crew::enemy_native::native_factory;
unsigned beamOriginal=0,beamA=0,beamB=0,stepOriginal=0,stepA=0,stepB=0;void* seenStepState=nullptr;void* seenDt=nullptr;alignas(16) unsigned char bulletA[0xC40]{},bulletB[0xC40]{},bulletUnknown[0xC40]{};bool beamActorFault=false;
unsigned checks=0,failures=0,gunOriginal=0,unitOriginal=0,gunA=0,gunB=0,unitA=0,unitB=0,faults=0;
int holderA=0,holderB=0,unknown=0,factoryA=0,factoryB=0,descriptorA=0,descriptorB=0,manager=0,created=0;bool refuse=false,throwGun=false,throwOriginal=false,actorGunFault=false,actorUnitFault=false,leaseAactive=true,conflictActive=true;
const void* seenHolder=nullptr;const float* seenMatrix=nullptr;std::uint64_t* seenSeed=nullptr;void* seenManager=nullptr;void* seenFactory=nullptr;void* seenDescriptor=nullptr;
void Check(bool value,const char* expression,int line){++checks;if(!value){++failures;std::fprintf(stderr,"line %d: %s\n",line,expression);}}
#define CHECK(x) Check((x),#x,__LINE__)
void __fastcall OriginalGun(void* holder,const float* matrix,std::uint64_t* seed){if(throwOriginal)RaiseException(0xE2233445u,0,0,nullptr);++gunOriginal;seenHolder=holder;seenMatrix=matrix;seenSeed=seed;}
void* __fastcall OriginalUnit(void* m,const float* matrix,void* factory,void* descriptor){if(throwOriginal)RaiseException(0xE2233445u,0,0,nullptr);++unitOriginal;seenManager=m;seenMatrix=matrix;seenFactory=factory;seenDescriptor=descriptor;return refuse ? nullptr : &created;}
bool GunOwnA(const void* holder) noexcept {if(throwGun && holder==&holderA)RaiseException(0xE1122334u,0,0,nullptr);return leaseAactive && holder==&holderA;}
bool GunOwnB(const void* holder) noexcept {return holder==&holderB;}
void GunInvokeA(void* holder,const float* matrix,std::uint64_t* seed){if(actorGunFault)return;++gunA;InvokeOriginalNativeGunFactory(holder,matrix,seed);}
void GunInvokeB(void* holder,const float* matrix,std::uint64_t* seed){++gunB;InvokeOriginalNativeGunFactory(holder,matrix,seed);}
void GunFault(const void* holder) noexcept{++faults;if(holder==&holderA)actorGunFault=true;}
bool UnitOwnA(const void* factory,const void* descriptor) noexcept{return leaseAactive && factory==&factoryA && descriptor==&descriptorA;}
bool UnitOwnConflict(const void* factory,const void* descriptor) noexcept{return conflictActive && UnitOwnA(factory,descriptor);}
bool UnitOwnB(const void* factory,const void* descriptor) noexcept{return factory==&factoryB && descriptor==&descriptorB;}
void* UnitInvokeA(void* m,const float* matrix,void* factory,void* descriptor){if(actorUnitFault)return nullptr;++unitA;return InvokeOriginalNativeUnitFactory(m,matrix,factory,descriptor);}
void* UnitInvokeB(void* m,const float* matrix,void* factory,void* descriptor){++unitB;return InvokeOriginalNativeUnitFactory(m,matrix,factory,descriptor);}
void UnitFault(const void* factory,const void*) noexcept{++faults;if(factory==&factoryA)actorUnitFault=true;}
const NativeGunFactoryProvider ga{1,GunOwnA,GunInvokeA,GunFault},gb{2,GunOwnB,GunInvokeB,GunFault};
const NativeUnitFactoryProvider ua{3,UnitOwnA,UnitInvokeA,UnitFault},ub{4,UnitOwnB,UnitInvokeB,UnitFault};
void __fastcall OriginalBeam(void* bullet){if(throwOriginal)RaiseException(0xE2233445u,0,0,nullptr);++beamOriginal;if(At<unsigned>(bullet,0xC30)==1 && !(At<unsigned>(bullet,0xC34)&(1u<<14)))Put<unsigned>(bullet,0xC34,At<unsigned>(bullet,0xC34)|(1u<<13));}
bool BeamOwnA(const void* bullet) noexcept{return leaseAactive && bullet==bulletA;}
bool BeamOwnB(const void* bullet) noexcept{return bullet==bulletB;}
void BeamInvokeA(void* bullet){if(beamActorFault)return;++beamA;InvokeOriginalNativeBeamRefresh(bullet);}
void BeamInvokeB(void* bullet){++beamB;InvokeOriginalNativeBeamRefresh(bullet);}
void BeamFault(const void* bullet) noexcept{++faults;if(bullet==bulletA)beamActorFault=true;}
const NativeBeamRefreshProvider ba{6,BeamOwnA,BeamInvokeA,BeamFault},bb{7,BeamOwnB,BeamInvokeB,BeamFault};
void TestBeam(){
 void* table[13]{};table[12]=reinterpret_cast<void*>(&OriginalBeam);Put<void*>(bulletA,0,table);Put<void*>(bulletB,0,table);Put<void*>(bulletUnknown,0,table);
 Put<unsigned>(bulletA,0xC30,1);Put<unsigned>(bulletB,0xC30,1);Put<unsigned>(bulletUnknown,0xC30,1);
 CHECK(!InstallNativeBeamRefreshDispatcher());CHECK(!RegisterNativeBeamRefreshProvider({}));auto missing=ba;missing.Fault=nullptr;CHECK(!RegisterNativeBeamRefreshProvider(missing));
 CHECK(RegisterNativeBeamRefreshProvider(ba));CHECK(RegisterNativeBeamRefreshProvider(ba));auto duplicate=ba;duplicate.Invoke=BeamInvokeB;CHECK(!RegisterNativeBeamRefreshProvider(duplicate));CHECK(RegisterNativeBeamRefreshProvider(bb));
 CHECK(!InstallNativeBeamRefreshDispatcher());constexpr unsigned char expected[]={0x48,0x8B,0x01,0xFF,0x50,0x60,0xE9,0x31,0x05,0,0};std::memcpy(image+kBeamSite,expected,11);
 image[kBeamSite-1]=0x23;image[kBeamSite+11]=0x45;CHECK(InstallNativeBeamRefreshDispatcher());CHECK(InstallNativeBeamRefreshDispatcher());CHECK(NativeBeamRefreshProviderReady(ba) && NativeBeamRefreshProviderReady(bb));
 CHECK(image[kBeamSite-1]==0x23 && image[kBeamSite+11]==0x45);std::int32_t next=0;std::memcpy(&next,image+kBeamSite+6,4);CHECK(static_cast<std::int64_t>(kBeamSite)+10+next==kBeamContinue);
 for(unsigned i=0;i<10000;++i){void* bullet=i%2 ? static_cast<void*>(bulletA) : bulletB;BeamRelay(bullet);CHECK(At<unsigned>(bullet,0xC34)&(1u<<13));}
 CHECK(beamA==5000 && beamB==5000 && beamOriginal==10000);BeamRelay(bulletUnknown);CHECK(beamOriginal==10001 && (At<unsigned>(bulletUnknown,0xC34)&(1u<<13)));
 Put<unsigned>(bulletA,0xC34,1u<<14);BeamRelay(bulletA);CHECK(At<unsigned>(bulletA,0xC34)==(1u<<14)); // inactive native callback cannot be called accepted merely because it returned
 bool caught=false;throwOriginal=true;__try {BeamRelay(bulletUnknown);}__except(EXCEPTION_EXECUTE_HANDLER){caught=true;}throwOriginal=false;CHECK(caught && NativeBeamRefreshProviderReady(ba));
 const auto byte=image[kBeamSite+10];image[kBeamSite+10]^=1;CHECK(!NativeBeamRefreshProviderReady(ba) && !InstallNativeBeamRefreshDispatcher());image[kBeamSite+10]=byte;CHECK(NativeBeamRefreshProviderReady(ba));
 std::int32_t relative=0;std::memcpy(&relative,image+kBeamSite+1,4);auto* thunk=image+kBeamSite+5+relative;thunk[3]^=1;CHECK(!NativeBeamRefreshProviderReady(ba));thunk[3]^=1;CHECK(NativeBeamRefreshProviderReady(ba));
 throwOriginal=true;BeamRelay(bulletA);throwOriginal=false;CHECK(beamActorFault && NativeBeamRefreshProviderReady(ba));const auto original=beamOriginal;BeamRelay(bulletB);CHECK(beamOriginal==original+1);
 leaseAactive=false;BeamRelay(bulletA);CHECK(beamOriginal==original+2);leaseAactive=true;beamActorFault=false;BeamRelay(bulletA);CHECK(beamOriginal==original+3);
}
void __fastcall OriginalStep(void* state,void* dt){if(throwOriginal)RaiseException(0xE2233445u,0,0,nullptr);++stepOriginal;seenStepState=state;seenDt=dt;}
void StepInvokeA(void* bullet,void* dt){if(beamActorFault)return;++stepA;InvokeOriginalNativeBeamStep(bullet,dt);}
void StepInvokeB(void* bullet,void* dt){++stepB;InvokeOriginalNativeBeamStep(bullet,dt);}
const NativeBeamStepProvider sa{8,BeamOwnA,StepInvokeA,BeamFault},sb{9,BeamOwnB,StepInvokeB,BeamFault};
void TestBeamStep(){
 CHECK(!InstallNativeBeamStepDispatcher());CHECK(!RegisterNativeBeamStepProvider({}));CHECK(RegisterNativeBeamStepProvider(sa));CHECK(RegisterNativeBeamStepProvider(sa));CHECK(RegisterNativeBeamStepProvider(sb));
 auto changed=sa;changed.Invoke=StepInvokeB;CHECK(!RegisterNativeBeamStepProvider(changed));CHECK(!InstallNativeBeamStepDispatcher());
 image[kBeamStepSite]=0xE9;const auto target=static_cast<std::int32_t>(kBeamStep-(kBeamStepSite+5));std::memcpy(image+kBeamStepSite+1,&target,4);
 CHECK(InstallNativeBeamStepDispatcher() && InstallNativeBeamStepDispatcher());CHECK(NativeBeamStepProviderReady(sa));alignas(16) unsigned char dt[16]{};
 for(unsigned i=0;i<10000;++i){auto* bullet=i%2 ? bulletA : bulletB;BeamStepRelay(bullet+0x140,dt);CHECK(seenStepState==bullet+0x140 && seenDt==dt);}
 CHECK(stepA==5000 && stepB==5000 && stepOriginal==10000);BeamStepRelay(bulletUnknown+0x140,dt);CHECK(stepOriginal==10001 && seenStepState==bulletUnknown+0x140);
 bool caught=false;throwOriginal=true;__try {BeamStepRelay(bulletUnknown+0x140,dt);}__except(EXCEPTION_EXECUTE_HANDLER){caught=true;}throwOriginal=false;CHECK(caught && NativeBeamStepProviderReady(sa));
 const auto marker=image[kBeamStepSite+4];image[kBeamStepSite+4]^=1;CHECK(!NativeBeamStepProviderReady(sa) && !InstallNativeBeamStepDispatcher());image[kBeamStepSite+4]=marker;
 std::int32_t relative=0;std::memcpy(&relative,image+kBeamStepSite+1,4);auto* thunk=image+kBeamStepSite+5+relative;thunk[2]^=1;CHECK(!NativeBeamStepProviderReady(sa));thunk[2]^=1;CHECK(NativeBeamStepProviderReady(sa));
 throwOriginal=true;BeamStepRelay(bulletA+0x140,dt);throwOriginal=false;CHECK(beamActorFault && NativeBeamStepProviderReady(sa));const auto before=stepOriginal;BeamStepRelay(bulletB+0x140,dt);CHECK(stepOriginal==before+1);
 leaseAactive=false;BeamStepRelay(bulletA+0x140,dt);CHECK(stepOriginal==before+2);leaseAactive=true;beamActorFault=false;BeamStepRelay(bulletA+0x140,dt);CHECK(stepOriginal==before+3);
}
void CallBytes(unsigned at,unsigned target,unsigned char opcode=0xE8){image[at]=opcode;const auto displacement=static_cast<std::int32_t>(static_cast<std::int64_t>(target)-at-5);std::memcpy(image+at+1,&displacement,4);}
void TestAdmission(){
 CHECK(!InstallNativeGunFactoryDispatcher());CHECK(!InstallNativeUnitFactoryDispatcher());CHECK(!RegisterNativeGunFactoryProvider({}));CHECK(!RegisterNativeUnitFactoryProvider({}));
 auto missingFault=ga;missingFault.Fault=nullptr;CHECK(!RegisterNativeGunFactoryProvider(missingFault));CHECK(RegisterNativeGunFactoryProvider(ga));CHECK(RegisterNativeGunFactoryProvider(ga));auto changed=ga;changed.Invoke=GunInvokeB;CHECK(!RegisterNativeGunFactoryProvider(changed));
 CHECK(RegisterNativeGunFactoryProvider(gb));CHECK(!NativeGunFactoryProviderReady(ga));CHECK(!InstallNativeGunFactoryDispatcher());
 for(const auto& s:signatures)std::memcpy(image+s.rva,s.bytes,16);CHECK(!InstallNativeGunFactoryDispatcher());CallBytes(kGunSite,kGun);
 CHECK(InstallNativeGunFactoryDispatcher());CHECK(InstallNativeGunFactoryDispatcher());CHECK(NativeGunFactoryProviderReady(ga) && NativeGunFactoryProviderReady(gb));
 auto missingUnitFault=ua;missingUnitFault.Fault=nullptr;CHECK(!RegisterNativeUnitFactoryProvider(missingUnitFault));CHECK(RegisterNativeUnitFactoryProvider(ua));CHECK(RegisterNativeUnitFactoryProvider(ub));CHECK(RegisterNativeUnitFactoryProvider(ua));auto wrong=ua;wrong.Owns=UnitOwnB;CHECK(!RegisterNativeUnitFactoryProvider(wrong));
 CHECK(!RegisterNativeUnitFactoryCallsite(0));CHECK(!RegisterNativeUnitFactoryCallsite(0x1000));CallBytes(0x1000,kGun);CHECK(!RegisterNativeUnitFactoryCallsite(0x1000));CallBytes(0x1000,kUnit,0xE9);
 CHECK(RegisterNativeUnitFactoryCallsite(0x1000));CHECK(RegisterNativeUnitFactoryCallsite(0x1000));CHECK(!NativeUnitFactoryProviderReady(ua));
 CHECK(InstallNativeUnitFactoryDispatcher());CHECK(NativeUnitFactoryCallsiteReady(0x1000));CHECK(NativeUnitFactoryProviderReady(ua));
 for(unsigned i=1;i<kSites;++i){const unsigned site=0x1000+i*16;CallBytes(site,kUnit);CHECK(RegisterNativeUnitFactoryCallsite(site));}
 CallBytes(0x5000,kUnit);CHECK(!RegisterNativeUnitFactoryCallsite(0x5000));CHECK(InstallNativeUnitFactoryDispatcher());CHECK(InstallNativeUnitFactoryDispatcher());
 for(unsigned i=0;i<kSites;++i)CHECK(NativeUnitFactoryCallsiteReady(0x1000+i*16));
 auto* replacement=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));CHECK(replacement!=nullptr);const auto saved=image;image=replacement;
 CHECK(!InstallNativeGunFactoryDispatcher() && !InstallNativeUnitFactoryDispatcher());image=saved;VirtualFree(replacement,0,MEM_RELEASE);
}
void TestRelay(){
 alignas(16) float matrix[16]{};std::uint64_t seed=0x123456789ABCDull;
 GunRelay(&unknown,matrix,&seed);CHECK(gunOriginal==1 && !gunA && !gunB);CHECK(seenHolder==&unknown && seenMatrix==matrix && seenSeed==&seed);
 for(unsigned i=0;i<10000;++i){void* holder=i%2 ? static_cast<void*>(&holderA) : &holderB;GunRelay(holder,matrix,&seed);CHECK(seenHolder==holder && seenMatrix==matrix && seenSeed==&seed);}
 CHECK(gunA==5000 && gunB==5000 && gunOriginal==10001);
 auto* output=UnitRelay(&manager,matrix,&unknown,&descriptorA);CHECK(output==&created && unitOriginal==1 && !unitA && !unitB);CHECK(seenManager==&manager && seenFactory==&unknown && seenDescriptor==&descriptorA);
 for(unsigned i=0;i<10000;++i){void* factory=i%2 ? static_cast<void*>(&factoryA) : &factoryB;void* descriptor=i%2 ? static_cast<void*>(&descriptorA) : &descriptorB;
  output=UnitRelay(&manager,matrix,factory,descriptor);CHECK(output==&created && seenManager==&manager && seenMatrix==matrix && seenFactory==factory && seenDescriptor==descriptor);}
 CHECK(unitA==5000 && unitB==5000 && unitOriginal==10001);CHECK(DispatchNativeUnitFactory(&manager,matrix,&factoryB,&descriptorB)==&created && unitB==5001 && unitOriginal==10002);refuse=true;const auto attempts=unitOriginal;CHECK(!UnitRelay(&manager,matrix,&factoryA,&descriptorA));CHECK(unitOriginal==attempts+1);refuse=false;
 // Native failure is preserved. This dispatcher never promotes an attempt or
 // an incrementing native counter into an accepted projectile or an ACK.
}
void TestOwnership(){
 unsigned char original[5]{};std::memcpy(original,image+kGunSite,5);image[kGunSite+4]^=1;CHECK(!NativeGunFactoryProviderReady(ga));CHECK(!InstallNativeGunFactoryDispatcher());std::memcpy(image+kGunSite,original,5);CHECK(NativeGunFactoryProviderReady(ga));
 std::memcpy(original,image+0x1000,5);image[0x1000+1]^=1;CHECK(!NativeUnitFactoryProviderReady(ua));CHECK(!NativeUnitFactoryCallsiteReady(0x1000));CHECK(!InstallNativeUnitFactoryDispatcher());std::memcpy(image+0x1000,original,5);CHECK(NativeUnitFactoryProviderReady(ua));
 const auto fingerprint=image[kUnit];image[kUnit]^=1;CHECK(!NativeUnitFactoryProviderReady(ua));CHECK(!NativeGunFactoryProviderReady(ga));image[kUnit]=fingerprint;CHECK(NativeGunFactoryProviderReady(ga));
}
void TestNativeTransparency(){
 alignas(16) float matrix[16]{};std::uint64_t seed=0;bool caught=false;throwOriginal=true;
 __try {GunRelay(&unknown,matrix,&seed);}__except(EXCEPTION_EXECUTE_HANDLER){caught=true;}
 CHECK(caught && NativeGunFactoryProviderReady(ga));caught=false;
 __try {UnitRelay(&manager,matrix,&unknown,&unknown);}__except(EXCEPTION_EXECUTE_HANDLER){caught=true;}
 CHECK(caught && NativeUnitFactoryProviderReady(ua));throwOriginal=false;
 std::int32_t relative=0;std::memcpy(&relative,image+kGunSite+1,4);auto* thunk=image+kGunSite+5+relative;thunk[2]^=1;
 CHECK(!NativeGunFactoryProviderReady(ga) && !InstallNativeGunFactoryDispatcher());thunk[2]^=1;CHECK(NativeGunFactoryProviderReady(ga));
}
void TestFaultIsolation(){
 alignas(16) float matrix[16]{};std::uint64_t seed=1;throwGun=true;const auto beforeGun=gunOriginal;GunRelay(&holderA,matrix,&seed);throwGun=false;
 CHECK(NativeGunFactoryProviderReady(ga) && gunOriginal==beforeGun && faults>0 && actorGunFault);GunRelay(&unknown,matrix,&seed);CHECK(gunOriginal==beforeGun+1);GunRelay(&holderA,matrix,&seed);CHECK(gunOriginal==beforeGun+1);
 const auto otherGun=gunB;GunRelay(&holderB,matrix,&seed);CHECK(gunB==otherGun+1 && gunOriginal==beforeGun+2);
 leaseAactive=false;GunRelay(&holderA,matrix,&seed);CHECK(gunOriginal==beforeGun+3);leaseAactive=true;actorGunFault=false;
 GunRelay(&holderA,matrix,&seed);CHECK(gunOriginal==beforeGun+4 && NativeGunFactoryProviderReady(ga));
 const NativeUnitFactoryProvider conflicting{5,UnitOwnConflict,UnitInvokeB,UnitFault};CHECK(RegisterNativeUnitFactoryProvider(conflicting));const auto beforeUnit=unitOriginal;
 CHECK(!UnitRelay(&manager,matrix,&factoryA,&descriptorA));CHECK(unitOriginal==beforeUnit && NativeUnitFactoryProviderReady(ua) && actorUnitFault);CHECK(UnitRelay(&manager,matrix,&unknown,&unknown)==&created && unitOriginal==beforeUnit+1);
 const auto otherUnit=unitB;CHECK(UnitRelay(&manager,matrix,&factoryB,&descriptorB)==&created && unitB==otherUnit+1);
 leaseAactive=false;CHECK(UnitRelay(&manager,matrix,&factoryA,&descriptorA)==&created);leaseAactive=true;actorUnitFault=false;conflictActive=false;
 CHECK(UnitRelay(&manager,matrix,&factoryA,&descriptorA)==&created && NativeUnitFactoryProviderReady(ua));
}
}
namespace crew::enemy_native {
void* GunFactoryTestFunction(unsigned rva) noexcept{if(rva==kGun)return reinterpret_cast<void*>(&OriginalGun);if(rva==kUnit)return reinterpret_cast<void*>(&OriginalUnit);if(rva==kBeamStep)return reinterpret_cast<void*>(&OriginalStep);std::abort();}
}
int main(){image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!image)return 2;TestAdmission();TestRelay();TestOwnership();TestNativeTransparency();TestBeam();TestBeamStep();TestFaultIsolation();VirtualFree(image,0,MEM_RELEASE);std::printf("%u checks, %u failures\n",checks,failures);return failures ? 1 : 0;}
