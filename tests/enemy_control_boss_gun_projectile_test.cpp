// Private-memory integration of real Boss gun scope + final Unit provider.
// No game DLL is loaded or executed.
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#define ENEMY_CONTROL_BOSS_GUN_NET_TEST
#define ENEMY_CONTROL_PROJECTILE_NATIVE_TEST
#include "../src/crew.h"
#include "../src/memory.h"
namespace crew {unsigned char* image=nullptr;}
#include "../src/enemy_control_boss_gun_net.cpp"
#include "../src/enemy_control_projectile_native.cpp"
namespace {
using namespace crew;using namespace crew::enemy_native;using namespace crew::enemy_native::boss_gun_net;
struct Fixture {
    alignas(16) unsigned char root[0x2800]{},rootControl[32]{},part[0xC00]{},partControl[32]{},gun[0x320]{},gunControl[32]{},maker[0x280]{},makerControl[32]{},factory[16]{},node[0x50]{},head[16]{},rows[16]{};
    alignas(16) unsigned char target[0x400]{},targetControl[32]{},lock[16]{},lockControl[32]{},lockRows[160]{},resources[2][16]{};
    NativeLease lease{};bool role=true,authority=false;
};
Fixture actors[2]{};NativeProfile profile{"Humanoid_BigGreyBoss",0x17BEC68,0,0,77,86,0,Kind::humanoid,7,5};
NativeActionExtension extension{};NativeGunFactoryProvider gunProvider{};NativeUnitFactoryProvider unitProvider{};
int checks=0,failures=0,gunCalls=0,effects=0,actual=0,replayed=0,deleted=0;
bool traceMode=false;bool cleanupThrows=false;bool nestedGun=false,nestedReplay=false;Result nestedReplayResult=Result::failed;unsigned char replayWire[512]{};std::size_t replayWireSize=0;
bool canReplay=true,createNull=false,replayNull=false,throwFactory=false,restoreInside=false;bool restoreInsideResult=true;
unsigned char targetId[32]{};NativeProjectileSample replaySample{};void* factoryVtable[3]{};
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
    Put<void*>(a.root,0x28,a.root);Put<void*>(a.root,0,image+0x17BEC68);Put<void*>(a.root,0x30,a.rootControl);Put<void*>(a.root,0x17E8,a.head);Put<std::size_t>(a.root,0x17F0,1);
    Put<void*>(a.head,0,a.node);Put<void*>(a.head,8,a.node);Put<void*>(a.node,0,a.head);Put<void*>(a.node,8,a.head);
    const wchar_t name[]=L"left";std::memcpy(a.node+0x10,name,sizeof(name));Put<std::size_t>(a.node,0x20,4);Put<std::size_t>(a.node,0x28,7);
    Put<void*>(a.node,0x40,a.part);Put<void*>(a.node,0x48,a.partControl);Put<void*>(a.part,0,image+0x17BFAE0);Put<void*>(a.part,0x30,a.partControl);
    Put<void*>(a.part,0xAE8,a.root);Put<void*>(a.part,0xAF0,a.rootControl);Put<void*>(a.part,0xB60,a.rows);Put<std::size_t>(a.part,0xB70,1);
    Put<void*>(a.rows,0,a.gun);Put<void*>(a.rows,8,a.gunControl);Put<void*>(a.gun,0x200,a.root);Put<void*>(a.gun,0x208,a.rootControl);
    Put<void*>(a.gun,0x210,a.part);Put<void*>(a.gun,0x218,a.partControl);Put<void*>(a.gun,0x30,a.root);Put<void*>(a.gun,0x38,a.rootControl);
    Put<void*>(a.gun,0x40,a.maker);Put<void*>(a.gun,0x48,a.makerControl);Put<unsigned>(a.gun,0x5C,240);Put<unsigned>(a.gun,0x64,1);Put<float>(a.gun,0x68,1);
    Put<void*>(a.maker,0,image+0x17E6EE0);Put<void*>(a.maker,0x28,a.factory);Put<void*>(a.maker+0xA0,0,image+0x1769028);
    Put<void*>(a.maker+0xA0,0x38,a.root);Put<void*>(a.maker+0xA0,0x40,a.rootControl);Put<float>(a.maker+0xA0,0x94,33.5f);
    Control(a.targetControl);Control(a.lockControl);Put<void*>(a.target,0x28,a.target);Put<void*>(a.target,0x30,a.targetControl);Put<void*>(a.target,0x368,a.lockRows);Put<std::size_t>(a.target,0x370,10);Put<std::size_t>(a.target,0x378,10);Put<void*>(a.lock,8,a.target);Put<void*>(a.lockRows,9*16,a.lock);Put<void*>(a.lockRows,9*16+8,a.lockControl);
    auto* descriptor=a.maker+0xA0;Put<void*>(descriptor,0x60,a.lock);Put<void*>(descriptor,0x68,a.lockControl);Put<std::uint16_t>(descriptor,0xF8,0xFFFF);Put<std::uint16_t>(descriptor,0x1C0,0xFFFF);Put<unsigned>(descriptor,0x1A0,1234);Put<unsigned>(descriptor,0x1A4,77);Put<unsigned>(descriptor,0x1A8,4);
    for(unsigned i=0;i<2;++i){Put<LONG>(a.resources[i],0,1);Put<LONG>(a.resources[i],4,1);Put<void*>(a.resources[i],8,a.resources[i]);Put<void*>(descriptor,0x180+i*16,a.resources[i]);Put<void*>(descriptor,0x188+i*16,a.resources[i]);}Put<void*>(a.factory,0,factoryVtable);
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
    if(traceMode){Put<unsigned>(descriptor,0x1A0,static_cast<unsigned>((*seed>>32)&0xFFFFu));Put<unsigned>(descriptor,0x1A4,At<unsigned>(h,0x34)-1u);}
    if(restoreInside && activeScope && activeScope->stream)restoreInsideResult=extension.Restore(Lease(*activeScope->stream));
    if(unitProvider.Owns(factory,descriptor))unitProvider.Invoke(nullptr,matrix,factory,descriptor);else InvokeOriginalNativeUnitFactory(nullptr,matrix,factory,descriptor);
    ++effects; // native 6D0220's separate muzzle effect still executes
}
void* InvokeOriginalNativeUnitFactory(void*,const float*,void*,void*){if(throwFactory)RaiseException(0xE0433333,0,0,nullptr);if(createNull)return nullptr;if(nestedGun){nestedGun=false;std::uint64_t seed=4;alignas(16) float m[16];Matrix(m);gunProvider.Invoke(actors[0].gun+0x20,m,&seed);}++actual;return NewBullet();}
void* __fastcall SampleCopy(void* to,const void* from){std::memcpy(to,from,0x1E0);for(unsigned offset:{0x38u,0x60u,0x78u,0x1D0u})if(auto* c=At<unsigned char*>(to,offset+8))InterlockedIncrement(reinterpret_cast<volatile LONG*>(c+12));return to;}
void __fastcall SampleDrop(void* descriptor){if(cleanupThrows)RaiseException(0xE0431234,0,0,nullptr);for(unsigned offset:{0x38u,0x60u,0x78u,0x1D0u})if(auto* c=At<unsigned char*>(descriptor,offset+8))InterlockedDecrement(reinterpret_cast<volatile LONG*>(c+12));}
void __fastcall SampleMove(void* target,projectile_native::Ref* source){const auto old=At<projectile_native::Ref>(target,0);std::memcpy(target,source,16);*source={};if(old.control)InterlockedDecrement(reinterpret_cast<volatile LONG*>(old.control+12));}
void* __fastcall SampleCreate(void*,const float* matrix,void* factory,void* descriptor){
    if(replayNull)return nullptr;if(nestedReplay){nestedReplay=false;nestedReplayResult=extension.Apply(actors[1].lease,replayWire,replayWireSize);Check(!extension.Restore(actors[1].lease),"reentrant restore retains replay-owned native resources");}++replayed;replaySample={};std::memcpy(replaySample.matrix,matrix,64);
    Check(factory==actors[1].factory,"real helper reconstructs peer-local factory resource");Check(At<const void*>(descriptor,0x38)==actors[1].root,"real helper reconstructs local owner weak");
    Check(At<const void*>(descriptor,0x60)==actors[1].lock,"real helper resolves full target ID to local native lock weak");
    for(unsigned i=0;i<kProjectileWords;++i)replaySample.scalar[i]=At<unsigned>(descriptor,kProjectileScalar32[i]);std::memcpy(replaySample.target,targetId,32);replaySample.lock=9;replaySample.resourceMask=projectile_native::Resources(descriptor);replaySample.random=At<unsigned>(descriptor,0x1A0);replaySample.shot=At<unsigned>(descriptor,0x1A4);replaySample.flags=At<unsigned>(descriptor,0x1A8);Check(At<const void*>(descriptor,0x180)==actors[1].resources[0],"real helper keeps resource controls local instead of sending pointers");return NewBullet();
}
void* ProjectileNativeTestFunction(unsigned at) noexcept {if(at==0x32EF10)return reinterpret_cast<void*>(&SampleCopy);if(at==0x100C90)return reinterpret_cast<void*>(&SampleDrop);if(at==0x58EB40)return reinterpret_cast<void*>(&SampleMove);if(at==0x1194280)return reinterpret_cast<void*>(&SampleCreate);return nullptr;}
void* BossGunNetTestFunction(unsigned at) noexcept{return at==0x118A1B0 ? reinterpret_cast<void*>(&Delete) : nullptr;}
}
namespace crew {
bool ReadNativeObjectId(const unsigned char* object,unsigned char* out) noexcept{for(const auto& a:actors)if(a.target==object){std::memcpy(out,targetId,32);return true;}return false;}
bool ResolveMarkIdentities(const unsigned char[32],const unsigned char* id,MarkIdentities* out) noexcept{*out={};if(!canReplay || !id || std::memcmp(id,targetId,32))return false;out->target={actors[1].target,actors[1].targetControl};return true;}
}
namespace {
struct TraceScenario {const char* name;const char* clip;double mab;unsigned hands,total,interval,perCall;bool refused;};
void TraceSetup(const TraceScenario& scenario) {
    for(auto& a:actors)a.role=false;for(auto& st:streams){if(st.held && !st.retired){st.count=0;extension.Restore(Lease(st));}ResetStream(st);}activeScope=nullptr;
    gunCalls=effects=actual=replayed=deleted=0;bulletCursor=0;canReplay=true;createNull=replayNull=throwFactory=restoreInside=false;nestedGun=nestedReplay=false;cleanupThrows=false;
    for(unsigned i=0;i<32;++i)targetId[i]=static_cast<unsigned char>(i+1);std::memset(targetId+20,0,4);
    Setup(actors[0],true);Setup(actors[1],false);actors[1].role=false;
    if(scenario.hands==2) {
        auto& h=actors[0];auto& r=actors[1];
        Put<void*>(h.node,0,r.node);Put<void*>(r.node,8,h.node);Put<void*>(r.node,0,h.head);Put<void*>(h.head,8,r.node);Put<std::size_t>(h.root,0x17F0,2);
        const wchar_t right[]=L"right";std::memset(r.node+0x10,0,0x20);std::memcpy(r.node+0x10,right,sizeof(right));Put<std::size_t>(r.node,0x20,5);Put<std::size_t>(r.node,0x28,7);
        Put<void*>(r.part,0xAE8,h.root);Put<void*>(r.part,0xAF0,h.rootControl);Put<void*>(r.gun,0x200,h.root);Put<void*>(r.gun,0x208,h.rootControl);Put<void*>(r.gun,0x30,h.root);Put<void*>(r.gun,0x38,h.rootControl);
        Put<void*>(r.maker+0xA0,0x38,h.root);Put<void*>(r.maker+0xA0,0x40,h.rootControl);Put<void*>(r.maker+0xA0,0x60,h.lock);Put<void*>(r.maker+0xA0,0x68,h.lockControl);
    }
    for(unsigned i=0;i<scenario.hands;++i){auto* holder=actors[i].gun+0x20;Put<unsigned>(holder,0x30,scenario.interval);Put<unsigned>(holder,0x3C,scenario.total);Put<unsigned>(holder,0x44,scenario.perCall);Put<unsigned>(holder,0x34,0);Put<unsigned>(holder,0x38,0);Put<unsigned>(actors[i].gun,8,180);}
    Check(extension.Before(actors[0].lease,true,nullptr),"trace binds source asset ordinals before any actual Unit");
}
int DumpTrace(const char* path) {
    FILE* out=nullptr;if(fopen_s(&out,path,"wb") || !out)return 2;traceMode=true;
    const TraceScenario scenarios[]={
        {"beam_single_left","attack_handBullet_l_add",80,1,240,1,1,false},
        {"beam_single_right","attack_handBullet_r_add",80,1,240,1,1,false},
        {"beam_double","attack_poseB_add",95.5,2,240,1,1,false},
        {"homing_double","attack_poseA_add",95.5,2,16,2,1,false},
        {"spear_double","attack_poseC_add",72,2,120,0,2,false},
        {"genocide_single","native_genocide_gun_activation",-1,1,8,1,1,false},
        {"beam_single_refused_factory","attack_handBullet_l_add",80,1,240,1,1,true},
    };
    unsigned outcomes=0;
    for(const auto& scenario:scenarios) {
        TraceSetup(scenario);std::uint64_t seeds[2]={0x12345678u,0x87654321u};unsigned nativeTick=0,attempts=0,successes=0;bool refused=false;
        std::fprintf(out,"{\"record\":\"scenario\",\"scenario\":\"%s\",\"source\":\"Boss.privateGun.slot5-to-6CFC60-to-6D05C0-to-84C5A\",\"clip\":\"%s\",\"mabCoordinate\":%.1f,\"hands\":%u,\"chargeGateFrames\":180,\"totalShotsPerGun\":%u,\"intervalUpdates\":%u,\"shotsPerCall\":%u,\"encodedOutcomeBytes\":%zu,\"clock\":\"native part UpdateContext count; no Hz assumption\",\"allocation\":\"native factory ABI fixture seam; production queue/Capture/Encode\"}\n",scenario.name,scenario.clip,scenario.mab,scenario.hands,scenario.total,scenario.interval,scenario.perCall,kHeader+kEventBytes);
        for(;;++nativeTick) {
            const int before=actual;unsigned frameAttempts=0;bool any=false;
            for(unsigned hand=0;hand<scenario.hands;++hand) {
                auto* holder=actors[hand].gun+0x20;if(At<unsigned>(holder,0x34)>=scenario.total)continue;any=true;
                // 44E18C compares the outer private-gun controller frame before
                // calling 6CFC60. First eligible call is update index 181.
                if(nativeTick<=180)continue;
                const unsigned elapsed=At<unsigned>(holder,0x38);
                if(!scenario.interval || !(elapsed%scenario.interval))for(unsigned j=0;j<scenario.perCall;++j) {
                    ++attempts;++frameAttempts;alignas(16) float matrix[16];Matrix(matrix);matrix[12]+=static_cast<float>(hand)*7.0f;matrix[14]+=static_cast<float>(At<unsigned>(holder,0x34))*0.25f;
                    createNull=scenario.refused && !refused;if(createNull)refused=true;
                    Check(gunProvider.Owns(holder),"trace actual native attempt remains claimed");gunProvider.Invoke(holder,matrix,seeds+hand);createNull=false;
                }
                Put<unsigned>(holder,0x38,elapsed+1);
            }
            if(!any)break;
            unsigned packets=0;for(;;) {
                unsigned char bytes[512]{};std::size_t used=0;if(!extension.Capture(actors[0].lease,bytes,sizeof(bytes),&used)){std::fclose(out);return 3;}if(!used)break;
                Event event{};if(!Decode(bytes,used,&event)){std::fclose(out);return 4;}++packets;++successes;++outcomes;
                std::fprintf(out,"{\"record\":\"packet\",\"scenario\":\"%s\",\"provider\":\"BBG1\",\"nativeUpdateTick\":%u,\"eligibleUpdate\":%u,\"actualUnits\":1,\"serial\":%llu,\"partOrdinal\":%u,\"seed\":%u,\"shot\":%u,\"encodedBytes\":%zu,\"hex\":\"",scenario.name,nativeTick,nativeTick-181,static_cast<unsigned long long>(event.serial),event.key,event.sample.random,event.sample.shot,used);
                for(std::size_t n=0;n<used;++n)std::fprintf(out,"%02x",static_cast<unsigned>(bytes[n]));std::fprintf(out,"\"}\n");
            }
            std::fprintf(out,"{\"record\":\"frame\",\"scenario\":\"%s\",\"nativeUpdateTick\":%u,\"factoryAttempts\":%u,\"actualUnits\":%d,\"packets\":%u}\n",scenario.name,nativeTick,frameAttempts,actual-before,packets);
        }
        std::fprintf(out,"{\"record\":\"end\",\"scenario\":\"%s\",\"nativeUpdates\":%u,\"factoryAttempts\":%u,\"actualSuccessfulUnits\":%u,\"falseFactoryOutcomes\":%u}\n",scenario.name,nativeTick,attempts,successes,scenario.refused ? 1u : 0u);
        Check(extension.Empty(actors[0].lease),"trace captured every accepted actual Unit");Check(extension.Quiesce(actors[0].lease) && extension.Restore(actors[0].lease),"trace burst fully drains before source phase changes");
    }
    std::fclose(out);traceMode=false;std::printf("Boss production trace: %u actual successful Unit packets written to %s\n",outcomes,path);return failures ? 1 : 0;
}
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
int main(int argc,char** argv){
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!image)return 2;
    controlVtable[0]=reinterpret_cast<void*>(&ControlNoop);controlVtable[1]=reinterpret_cast<void*>(&ControlNoop);
    for(const auto& sig:kSigs)std::memcpy(image+sig.rva,sig.bytes,16);for(const auto& sig:projectile_native::prints)std::memcpy(image+sig.rva,sig.bytes,16);Put<void*>(image,0x20B2958,image+0x4000);factoryVtable[2]=reinterpret_cast<void*>(&SampleCreate);
    Put<void*>(image+0x17BEC68-8,0,image+0x200);Put<void*>(image+0x17BFAE0-8,0,image+0x240);
    Put<void*>(image+0x17E6EE0,8,image+0x6CF7D0);Put<void*>(image+0x17E6EE0,0x18,image+0x6D0460);
    Check(InstallBossGunAcceptedProjectiles(),"installs one gun scope and exact 84C5A Unit callsite");if(argc==3 && !std::strcmp(argv[1],"--dump-trace"))return DumpTrace(argv[2]);Reset();
    Check(At<LONG>(actors[0].rootControl,8)==2 && At<LONG>(actors[0].partControl,8)==2 && At<LONG>(actors[0].gunControl,8)==2 && At<LONG>(actors[0].makerControl,8)==2,"all four owner links are held strongly");
    Fire(actors[0]);Check(actual==1 && effects==1 && gunCalls==1 && Find(actors[0].lease)->count==1,"host queues only a real final native factory outcome");
    Put<float>(actors[0].maker+0xA0,0x94,222.0f);Put<float>(actors[1].maker+0xA0,0x94,999.0f);Put<unsigned>(actors[1].maker+0xA0,0x1A0,0xFEEDu);
    Fire(actors[1]);Check(actual==1 && effects==2 && gunCalls==2 && At<unsigned>(actors[1].gun+0x20,0x34)==1,"guest runs native counters and FX while final projectile factory is suppressed");
    unsigned char bytes[512]{};std::size_t used=0;Check(Pack(actors[0],bytes,&used) && used==kHeader+kEventBytes,"semantic sample encodes inside 512-byte reliable record");
    Check(extension.Apply(actors[1].lease,bytes,used)==Result::applied && replayed==1,"guest ACK requires one actual native projectile replay");
    float speed=0;std::memcpy(&speed,&replaySample.scalar[SpeedIndex()],4);Check(replaySample.random==1234 && replaySample.shot==77 && replaySample.flags==4,"real helper replays authoritative per-bullet seed, counter and flags");
    Check(speed==33.5f && !std::memcmp(replaySample.target,targetId,32) && replaySample.lock==9 && replaySample.resourceMask==3,"temporary speed, full target ID, lock and local resource presence survive wire");
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
    for(auto& a:actors)extension.Abort(a.lease);
    Reset();Fire(actors[0]);Pack(actors[0],bytes,&used);cleanupThrows=true;Check(extension.Apply(actors[1].lease,bytes,used)==Result::failed,"native descriptor cleanup exception is not ACKed");cleanupThrows=false;
    auto* failed=Find(actors[1].lease);Check(failed && failed->projectile.uncertain && !extension.Empty(actors[1].lease),"uncertain native cleanup persists its descriptor and pin journal");
    const int completed=replayed;Check(extension.Apply(actors[1].lease,bytes,used)==Result::failed && replayed==completed,"uncertain native cleanup never blindly recreates a projectile on retry");
    Check(!extension.Restore(actors[1].lease) && !extension.Abort(actors[1].lease) && At<LONG>(actors[1].rootControl,8)>=3,"failed cleanup retains root role and pins instead of forgetting ownership");
    // This final private-process case deliberately retains the uncertain journal.
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("Boss gun with real semantic sample: %d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
// Object references over the fixture's NetIds (net_object_resolve.h).
#include "net_object_ref_stub.h"
