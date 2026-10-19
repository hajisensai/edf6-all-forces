// Private-memory integration of real Boss gun scope + final Unit provider.
// No game DLL is loaded or executed.
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#define ENEMY_CONTROL_BOSS_GUN_NET_TEST
#include "../src/crew.h"
#include "../src/memory.h"
namespace crew {unsigned char* image=nullptr;}
#include "../src/enemy_control_boss_gun_net.cpp"
namespace {
using namespace crew;using namespace crew::enemy_native;using namespace crew::enemy_native::boss_gun_net;
struct Fixture {
    alignas(16) unsigned char root[0x2800]{},rootControl[32]{},part[0xC00]{},partControl[32]{},gun[0x320]{},gunControl[32]{},maker[0x280]{},makerControl[32]{},factory[16]{},node[0x50]{},head[16]{},rows[16]{};
    NativeLease lease{};bool role=true,authority=false;
};
Fixture actors[2]{};NativeProfile profile{"Humanoid_BigGreyBoss",0x17BEC68,0,0,77,86,0,Kind::humanoid,7,5};
NativeActionExtension extension{};NativeGunFactoryProvider gunProvider{};NativeUnitFactoryProvider unitProvider{};
int checks=0,failures=0,gunCalls=0,effects=0,actual=0,replayed=0,deleted=0;
bool nestedGun=false,nestedReplay=false;Result nestedReplayResult=Result::failed;unsigned char replayWire[512]{};std::size_t replayWireSize=0;
bool canReplay=true,createNull=false,replayNull=false,throwFactory=false,restoreInside=false;bool restoreInsideResult=true;
unsigned char targetId[32]{};NativeProjectileSample replaySample{};
alignas(16) unsigned char bullets[8192][0x180]{};unsigned bulletCursor=0;void* controlVtable[2]{};
void Check(bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL %s\n",text);}}
bool controlThrows=false,controlReenter=false;void* faultControl=nullptr;unsigned controlEntries=0;
void __fastcall ControlNoop(void* value){
    ++controlEntries;
    if(controlReenter){controlReenter=false;Check(!extension.Empty(actors[0].lease),"native destructor callback cannot report an executing restore empty");Check(!extension.Restore(actors[0].lease),"native destructor callback cannot restore the same references twice");Check(!extension.Abort(actors[0].lease),"native destructor callback cannot abort an executing restore");Check(!extension.Before(actors[0].lease,true,nullptr),"native destructor callback cannot rebind partial ownership");}
    if(controlThrows && value==faultControl)RaiseException(0xE0437012,0,0,nullptr);
}
void* NewBullet(){auto* result=bullets[bulletCursor++%8192];std::memset(result,0,0x180);Put<unsigned>(result,0x128,0xFFFFFFFFu);return result;}
void Matrix(float* out){std::memset(out,0,64);out[0]=out[5]=out[10]=out[15]=1.0f;out[12]=40;out[13]=50;out[14]=60;}
void Control(unsigned char* c){Put<void*>(c,0,controlVtable);Put<LONG>(c,8,1);Put<LONG>(c,12,1);}
unsigned SpeedIndex(){for(unsigned i=0;i<kProjectileWords;++i)if(kProjectileScalar32[i]==0x94)return i;return kProjectileWords;}
void Setup(Fixture& a,bool authority){
    std::memset(&a,0,sizeof(a));a.role=true;a.authority=authority;Control(a.rootControl);Control(a.partControl);Control(a.gunControl);Control(a.makerControl);Put<void*>(a.gunControl,0x10,a.gun);Put<void*>(a.makerControl,0x10,a.maker);
    Put<void*>(a.root,0,image+0x17BEC68);Put<void*>(a.root,0x30,a.rootControl);Put<void*>(a.root,0x17E8,a.head);Put<std::size_t>(a.root,0x17F0,1);
    Put<void*>(a.head,0,a.node);Put<void*>(a.head,8,a.node);Put<void*>(a.node,0,a.head);Put<void*>(a.node,8,a.head);
    const wchar_t name[]=L"left";std::memcpy(a.node+0x10,name,sizeof(name));Put<std::size_t>(a.node,0x20,4);Put<std::size_t>(a.node,0x28,7);
    Put<void*>(a.node,0x40,a.part);Put<void*>(a.node,0x48,a.partControl);Put<void*>(a.part,0,image+0x17BFAE0);Put<void*>(a.part,0x30,a.partControl);
    Put<void*>(a.part,0xAE8,a.root);Put<void*>(a.part,0xAF0,a.rootControl);Put<void*>(a.part,0xB60,a.rows);Put<std::size_t>(a.part,0xB70,1);
    Put<void*>(a.rows,0,a.gun);Put<void*>(a.rows,8,a.gunControl);Put<void*>(a.gun,0x200,a.root);Put<void*>(a.gun,0x208,a.rootControl);
    Put<void*>(a.gun,0x210,a.part);Put<void*>(a.gun,0x218,a.partControl);Put<void*>(a.gun,0x30,a.root);Put<void*>(a.gun,0x38,a.rootControl);
    Put<void*>(a.gun,0x40,a.maker);Put<void*>(a.gun,0x48,a.makerControl);Put<unsigned>(a.gun,0x5C,240);Put<unsigned>(a.gun,0x64,1);Put<float>(a.gun,0x68,1);
    Put<void*>(a.maker,0,image+0x17E6EE0);Put<void*>(a.maker,0x28,a.factory);Put<void*>(a.maker+0xA0,0,image+0x1769028);
    Put<void*>(a.maker+0xA0,0x38,a.root);Put<void*>(a.maker+0xA0,0x40,a.rootControl);Put<float>(a.maker+0xA0,0x94,33.5f);
    a.lease.object=a.root;a.lease.control=a.rootControl;a.lease.profile=&profile;
}
void Reset(){
    for(auto& a:actors)a.role=false;for(auto& s:streams){if(s.held && !s.retired){s.count=0;NativeLease l=Lease(s);Restore(l);}ResetStream(s);}activeScope=nullptr;
    nestedGun=nestedReplay=false;nestedReplayResult=Result::failed;replayWireSize=0;gunCalls=effects=actual=replayed=deleted=0;bulletCursor=0;canReplay=true;createNull=replayNull=throwFactory=restoreInside=false;restoreInsideResult=true;
    for(unsigned i=0;i<32;++i)targetId[i]=static_cast<unsigned char>(i+1);std::memset(targetId+20,0,4);
    Setup(actors[0],true);Setup(actors[1],false);Check(extension.Before(actors[0].lease,true,nullptr),"host binds exact root/part/gun/maker strong identities");Check(extension.Before(actors[1].lease,false,nullptr),"guest binds its own stable asset ordinal");
}
void Fire(Fixture& a){alignas(16) float m[16]{};Matrix(m);std::uint64_t seed=123;Check(gunProvider.Owns(a.gun+0x20),"provider recognizes owned native holder");gunProvider.Invoke(a.gun+0x20,m,&seed);}
bool Pack(Fixture& a,unsigned char* bytes,std::size_t* used){return extension.Capture(a.lease,bytes,512,used);}
void __fastcall Delete(void*){++deleted;}
}
namespace crew::enemy_native {
const NativeProfile* Profiles(std::size_t* count) noexcept{*count=1;return &profile;}
bool NativeActionRole(const void* root,bool* authority) noexcept{for(auto& a:actors)if(a.root==root && a.role){if(authority)*authority=a.authority;return true;}return false;}
bool NativeActionQuiescing(const void* root) noexcept{const auto* s=FindRoot(root);return s && s->quiescing;}
bool RegisterNativeActionExtension(const NativeActionExtension& value) noexcept{extension=value;return true;}
bool RegisterNativeGunFactoryProvider(const NativeGunFactoryProvider& value) noexcept{gunProvider=value;return true;}
bool RegisterNativeUnitFactoryProvider(const NativeUnitFactoryProvider& value) noexcept{unitProvider=value;return true;}
bool RegisterNativeUnitFactoryCallsite(std::uint32_t at) noexcept{return at==0x84C5A;}
bool InstallNativeGunFactoryDispatcher() noexcept{return true;}
bool InstallNativeUnitFactoryDispatcher() noexcept{return true;}
bool NativeGunFactoryProviderReady(const NativeGunFactoryProvider& value) noexcept{return value.tag==gunProvider.tag;}
bool NativeUnitFactoryProviderReady(const NativeUnitFactoryProvider& value) noexcept{return value.tag==unitProvider.tag;}
bool NativeUnitFactoryCallsiteReady(std::uint32_t at) noexcept{return at==0x84C5A;}
void InvokeOriginalNativeGunFactory(void* holder,const float* matrix,std::uint64_t* seed){
    ++gunCalls;*seed=*seed*0x5D588B656C078965ull+0x269EC3ull;auto* h=static_cast<unsigned char*>(holder);Put<unsigned>(h,0x34,At<unsigned>(h,0x34)+1);
    auto* maker=At<unsigned char*>(h,0x20);auto* descriptor=maker+0xA0;auto* factory=At<void*>(maker,0x28);
    if(restoreInside && activeScope && activeScope->stream)restoreInsideResult=extension.Restore(Lease(*activeScope->stream));
    if(unitProvider.Owns(factory,descriptor))unitProvider.Invoke(nullptr,matrix,factory,descriptor);else InvokeOriginalNativeUnitFactory(nullptr,matrix,factory,descriptor);
    ++effects; // native 6D0220's separate muzzle effect still executes
}
void* InvokeOriginalNativeUnitFactory(void*,const float*,void*,void*){if(throwFactory)RaiseException(0xE0433333,0,0,nullptr);if(createNull)return nullptr;if(nestedGun){nestedGun=false;std::uint64_t seed=4;alignas(16) float m[16];Matrix(m);gunProvider.Invoke(actors[0].gun+0x20,m,&seed);}++actual;return NewBullet();}
bool CaptureNativeProjectileSample(const NativeLease&,const void* localTemplate,const float* matrix,const void* descriptor,NativeProjectileSample* out) noexcept{
    if(localTemplate!=descriptor || !out)return false;*out={};std::memcpy(out->matrix,matrix,64);
    for(unsigned i=0;i<kProjectileWords;++i)out->scalar[i]=At<unsigned>(descriptor,kProjectileScalar32[i]);for(unsigned i=0;i<kProjectileBytes;++i)out->bytes[i]=At<unsigned char>(descriptor,kProjectileScalar8[i]);
    out->random=1234;out->shot=77;out->flags=4;out->resourceMask=3;out->lock=9;std::memcpy(out->target,targetId,32);return true;
}
bool ValidNativeProjectileSample(const NativeProjectileSample& sample) noexcept{for(float f:sample.matrix)if(!std::isfinite(f))return false;return sample.matrix[15]==1 && sample.resourceMask<=3;}
bool CanReplayNativeProjectileSample(const NativeLease&,void*,const void*,float*,const NativeProjectileSample& sample) noexcept{return canReplay && ValidNativeProjectileSample(sample);}
void* ReplayNativeProjectileSample(const NativeLease&,void*,const void*,float*,const NativeProjectileSample& sample) noexcept{if(replayNull)return nullptr;if(nestedReplay){nestedReplay=false;nestedReplayResult=extension.Apply(actors[1].lease,replayWire,replayWireSize);Check(!extension.Restore(actors[1].lease),"reentrant restore retains replay-owned native resources");}++replayed;replaySample=sample;return NewBullet();}
bool NativeProjectileJournalEmpty(const NativeProjectileJournal& j) noexcept{return !j.uncertain && !j.busy && !j.ownerObject && !j.ownerControl && !j.heldMask && !j.made && !j.copyEntered && !j.createEntered && !j.cleanupEntered;}
bool RestoreNativeProjectileJournal(NativeProjectileJournal& j) noexcept{if(j.uncertain || j.busy || j.copyEntered || j.createEntered || j.cleanupEntered)return false;j={};return true;}
void* ReplayNativeProjectileSampleInPlace(NativeProjectileJournal&,const NativeLease& l,void* factory,const void* local,float* muzzle,const NativeProjectileSample& sample) noexcept{return ReplayNativeProjectileSample(l,factory,local,muzzle,sample);}
void* BossGunNetTestFunction(unsigned at) noexcept{return at==0x118A1B0 ? reinterpret_cast<void*>(&Delete) : nullptr;}
}
void StrongCleanupRegression(){
    for(unsigned stage=0;stage<4;++stage){
        controlThrows=false;Reset();auto* st=Find(actors[0].lease);auto& b=st->bindings[0];
        auto* target=stage==0 ? actors[0].makerControl : stage==1 ? actors[0].gunControl : stage==2 ? actors[0].partControl : actors[0].rootControl;
        Put<LONG>(target,8,1);faultControl=target;controlThrows=true;const auto entered=controlEntries;
        Check(!extension.Restore(actors[0].lease),"final-reference native destructor SEH fails restore");controlThrows=false;
        Check(st->cleanupUncertain && st->faulted && !st->retired && !st->restoring && controlEntries==entered+1,"failed native decrement is retained as a faulted cleanup record");
        const bool owned=stage==0 ? b.makerHeld : stage==1 ? b.gunHeld : stage==2 ? b.partHeld : st->rootHeld;
        Check(!owned && At<LONG>(target,8)==0,"native decrement consumes only its exact owned reference before SEH");
        Check(!extension.Restore(actors[0].lease) && !extension.Abort(actors[0].lease) && !extension.Empty(actors[0].lease) && controlEntries==entered+1,"uncertain cleanup never retries an already consumed decrement");
        Check(!extension.Before(actors[0].lease,true,nullptr),"uncertain cleanup cannot rebind a root into native actions");const auto fired=gunCalls;Fire(actors[0]);Check(gunCalls==fired,"uncertain cleanup claims the old role without invoking a partially destroyed launcher");
    }
    controlThrows=false;Reset();Put<LONG>(actors[0].makerControl,8,1);controlReenter=true;
    Check(extension.Restore(actors[0].lease) && Find(actors[0].lease)->retired,"guarded native destructor callbacks permit one final retirement");const auto entered=controlEntries;
    Check(extension.Restore(actors[0].lease) && controlEntries==entered,"retired receipt-only record never decrements an owner again");
    for(unsigned stage=0;stage<2;++stage){
        Reset();auto* old=Find(actors[0].lease);Check(extension.Restore(actors[0].lease),"partial-acquire fixture first releases its old owners");ResetStream(*old);Setup(actors[0],true);
        auto* target=stage==0 ? actors[0].gunControl : actors[0].makerControl;Put<LONG>(target,8,(std::numeric_limits<LONG>::max)());
        Check(!extension.Before(actors[0].lease,true,nullptr),"a saturated control refuses a later binding Pin");auto* st=Find(actors[0].lease);const auto& b=st->bindings[0];
        Check(st->rootHeld && b.partHeld && b.gunHeld==(stage==1) && !b.makerHeld && !b.held,"each successful partial Pin is immediately retained in the stream");
        Check(extension.Abort(actors[0].lease) && At<LONG>(actors[0].partControl,8)==1 && At<LONG>(actors[0].rootControl,8)==1 && At<LONG>(target,8)==(std::numeric_limits<LONG>::max)(),"partial binding releases only references this stream actually acquired");
    }
    Reset();
}
int main(){
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!image)return 2;
    controlVtable[0]=reinterpret_cast<void*>(&ControlNoop);controlVtable[1]=reinterpret_cast<void*>(&ControlNoop);
    for(const auto& sig:kSigs)std::memcpy(image+sig.rva,sig.bytes,16);
    Put<void*>(image+0x17BEC68-8,0,image+0x200);Put<void*>(image+0x17BFAE0-8,0,image+0x240);
    Put<void*>(image+0x17E6EE0,8,image+0x6CF7D0);Put<void*>(image+0x17E6EE0,0x18,image+0x6D0460);
    Check(InstallBossGunAcceptedProjectiles(),"installs one gun scope and exact 84C5A Unit callsite");Reset();
    Check(At<LONG>(actors[0].rootControl,8)==2 && At<LONG>(actors[0].partControl,8)==2 && At<LONG>(actors[0].gunControl,8)==2 && At<LONG>(actors[0].makerControl,8)==2,"all four owner links are held strongly");
    Fire(actors[0]);Check(actual==1 && effects==1 && gunCalls==1 && Find(actors[0].lease)->count==1,"host queues only a real final native factory outcome");
    Fire(actors[1]);Check(actual==1 && effects==2 && gunCalls==2 && At<unsigned>(actors[1].gun+0x20,0x34)==1,"guest runs native counters and FX while final projectile factory is suppressed");
    unsigned char bytes[512]{};std::size_t used=0;Check(Pack(actors[0],bytes,&used) && used==kHeader+kEventBytes,"semantic sample encodes inside 512-byte reliable record");
    Check(extension.Apply(actors[1].lease,bytes,used)==Result::applied && replayed==1,"guest ACK requires one actual native projectile replay");
    float speed=0;std::memcpy(&speed,&replaySample.scalar[SpeedIndex()],4);Check(speed==33.5f && !std::memcmp(replaySample.target,targetId,32) && replaySample.lock==9 && replaySample.resourceMask==3,"temporary speed, full target ID, lock and local resource presence survive wire");
    Check(extension.Apply(actors[1].lease,bytes,used)==Result::applied && replayed==1,"duplicate accepted outcome does not create another projectile");
    bytes[used-1]^=1;Check(extension.Apply(actors[1].lease,bytes,used)==Result::failed && replayed==1,"same serial with changed typed content fails without replay");bytes[used-1]^=1;
    Reset();Put<unsigned>(actors[0].gun,0x224,2);Check(extension.Before(actors[0].lease,true,nullptr),"runtime MAB aim mode is excluded from asset definition identity");Fire(actors[0]);Check(actual==1,"changed native aim mode still produces a real accepted bullet");
    Reset();createNull=true;Fire(actors[0]);Check(actual==0 && !Find(actors[0].lease)->count && After(actors[0].lease),"native factory null advances lifecycle but never queues acceptance");
    Reset();Fire(actors[0]);Check(Pack(actors[0],bytes,&used),"capture pending replay fixture");canReplay=false;Check(extension.Apply(actors[1].lease,bytes,used)==Result::pending && !replayed,"unresolved canonical target/resource is pending before any mutation");canReplay=true;Check(extension.Apply(actors[1].lease,bytes,used)==Result::applied && replayed==1,"pending retry creates exactly once after complete preflight");
    Reset();Fire(actors[0]);Pack(actors[0],bytes,&used);replayNull=true;Check(extension.Apply(actors[1].lease,bytes,used)==Result::failed && !Find(actors[1].lease)->applied,"failed native replay is never ACKed");
    Reset();nestedGun=true;Fire(actors[0]);Check(actual==1 && Find(actors[0].lease)->count==1 && !After(actors[0].lease),"nested same-root gun factory cannot reuse a pending accepted serial");Quiesce(actors[0].lease);Check(Pack(actors[0],bytes,&used),"outer accepted bullet survives a nested native fault for drain");
    Reset();Fire(actors[0]);Pack(actors[0],bytes,&used);std::memcpy(replayWire,bytes,used);replayWireSize=used;nestedReplay=true;Check(extension.Apply(actors[1].lease,bytes,used)==Result::applied && nestedReplayResult==Result::pending && replayed==1,"reentrant accepted replay is pending and cannot create twice before receipt commit");
    Reset();restoreInside=true;Fire(actors[0]);Check(!restoreInsideResult && Find(actors[0].lease) && actual==1,"reentrant restore cannot erase an in-flight factory scope");
    Reset();for(unsigned i=0;i<kQueued;++i)Fire(actors[0]);Check(Find(actors[0].lease)->count==kQueued && actual==static_cast<int>(kQueued),"bounded queue accepts exactly its capacity");Fire(actors[0]);Check(actual==static_cast<int>(kQueued) && !After(actors[0].lease),"overflow faults before another final projectile is spawned");
    Reset();Fire(actors[0]);Quiesce(actors[0].lease);Fire(actors[0]);Check(actual==1 && effects==2 && !Restore(actors[0].lease),"closing stops future bullets and preserves already accepted drain");Pack(actors[0],bytes,&used);Check(Restore(actors[0].lease) && At<LONG>(actors[0].rootControl,8)==1 && At<LONG>(actors[0].gunControl,8)==1,"drained restore releases all strong identities");
    const int before=actual;Fire(actors[0]);Check(actual==before && gunCalls==3 && effects==3,"root role surviving stream release cannot leak a native projectile");actors[0].role=false;Check(!gunProvider.Owns(actors[0].gun+0x20),"after root role ends ordinary AI factories are unclaimed");
    Reset();Put<void*>(actors[0].gun,0x208,actors[1].rootControl);Fire(actors[0]);Check(!actual && !After(actors[0].lease),"reused owner weak identity faults before native projectile creation");
    Reset();Fire(actors[0]);Pack(actors[0],bytes,&used);extension.Apply(actors[1].lease,bytes,used);Check(extension.Restore(actors[1].lease) && Find(actors[1].lease)->retired,"restored stream retains a receipt-only closing tombstone");Check(extension.Apply(actors[1].lease,bytes,used)==Result::applied && replayed==1,"late duplicate during surviving root role ACKs without another bullet");
    Reset();alignas(16) float persistent[16];Matrix(persistent);Put<void*>(actors[0].maker+0xA0,0x70,persistent);Fire(actors[0]);Check(!actual && !After(actors[0].lease),"unknown persistent-muzzle variant cannot bypass continuous-state coverage");
    Reset();auto saved=Find(actors[0].lease)->bindings[0].factory;Find(actors[0].lease)->bindings[0].factory=nullptr;Fire(actors[0]);Check(!actual && !After(actors[0].lease),"mismatched local resource cannot silently fall through");Find(actors[0].lease)->bindings[0].factory=saved;
    StrongCleanupRegression();
    for(auto& a:actors)extension.Abort(a.lease);VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("Boss gun actual outcomes: %d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
