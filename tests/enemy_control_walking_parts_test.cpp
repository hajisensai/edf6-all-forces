// Production complete-group/typed-factory logic on isolated native ABI seams.
#define ENEMY_CONTROL_WALKING_PARTS_TEST
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <limits>
#include "../src/enemy_control_walking_parts.h"
#include "../src/crew.h"
#include "../src/memory.h"
namespace crew {unsigned char* image=nullptr;}
#include "../src/enemy_control_walking_parts.cpp"
namespace {
using namespace crew;using namespace crew::enemy_native;using namespace crew::enemy_native::walking_parts;
int checks=0,failures=0;bool enabled=true,role=true,pvpReady=true,preflight=true,definitionOkay=true,dependencies=true,cleanupBlock=false,entryPending=false;
bool failFactory=false,failPvp=false,omitCtorBridge=false,corruptOwner=false,clearRoleInFactory=false,badCtor=false,useFallback=false,throwCtor=false,badBulletPin=false,throwRootDrop=false;
unsigned nativeGroups=0,nativeCtors=0,nativeFactories=0,deleted=0,refused=0,noNullReturns=0,partNow=0,activations=0;
std::uint64_t definition=0xAABBCCDDEEFF1234ull;float canonical[3]={31,41,59};
NativeUnitFactoryProvider unitProvider{};NativeActionExtension extension{};
NativeProfile profiles[]={
 {"EDF6_Berserker_TypeA",0x17AE968,0,0,123,79,0x31,Kind::walking,7,4},
 {"EDF6_Berserker_TypeGrapple",0x17AF200,0,0,124,79,0x31,Kind::walking,7,4},
 {"EDF6_Berserker_Large",0x17ADA78,0,0,125,79,0x31,Kind::walking,7,4}};
struct Peer {
 alignas(16) unsigned char body[0x1AE0]{},control[32]{},groups[2][0x100]{},entries[2][kParts][0x28]{},nodes[2][kParts][0xF0]{};
 alignas(16) unsigned char child[32][0x1040]{},childControl[32][32]{},bullet[32][0x1000]{},bulletControl[32][32]{},created[64][16]{};
 unsigned childCount=0,bulletCount=0;float offset=0;Ctor observed[32]{};NativeProjectileSample emitted[32]{};
};
Peer peers[2];Peer* current=&peers[0];void* weakTable[2]{};float identity[16]{};
void Check(bool yes,const char* message){++checks;if(!yes){++failures;std::printf("FAIL %s\n",message);}}
void __fastcall Destroy(void* pointer){if(throwRootDrop && (pointer==peers[0].control || pointer==peers[1].control))RaiseException(0xE0426677,0,0,nullptr);}
void Identity(float* m,float offset=0){std::memset(m,0,64);m[0]=m[5]=m[10]=m[15]=1;m[12]=offset;}
void Weak(unsigned char* object,unsigned char* control) {
 Put<const void*>(control,0,weakTable);Put<LONG>(control,8,1);Put<LONG>(control,12,1);Put<const void*>(object,0x30,control);
}
void __fastcall Delete(void* object){if(!object)return;auto* p=static_cast<unsigned char*>(object);if(!(p[0x18]&4)){p[0x18]|=4;++deleted;}}
Peer* Owner(void* root){for(auto& p:peers)if(root==p.body)return &p;return nullptr;}
NativeLease LeaseFor(Peer& p,unsigned family=0){NativeLease l{};l.object=p.body;l.control=p.control;l.profile=profiles+family;return l;}
void Reset(unsigned count=2) {
 std::memset(streams,0,sizeof(streams));for(auto& s:streams)s.next=1;walking_parts::scope=nullptr;
 std::memset(peers,0,sizeof(peers));weakTable[0]=reinterpret_cast<void*>(&Destroy);weakTable[1]=reinterpret_cast<void*>(&Destroy);Identity(identity);
 for(unsigned n=0;n<2;++n){auto& p=peers[n];p.offset=n ? 987.0f : 123.0f;Weak(p.body,p.control);Put<void*>(p.body,0x28,p.body);
  Put<void*>(p.body,0x17F0,p.groups);Put<std::size_t>(p.body,0x17F8,2);Put<std::size_t>(p.body,0x1800,2);
  Put<void*>(p.body,0x1810,p.created);Put<std::size_t>(p.body,0x1818,64);
  Put<std::uint64_t>(p.body,0x3E8,0x12345678);Put<std::uint64_t>(p.body,0x490,0x87654321);p.body[0x1940]=14;
  for(unsigned group=0;group<2;++group){Put<void*>(p.groups[group],8,p.entries[group]);Put<std::size_t>(p.groups[group],0x10,count);Put<std::size_t>(p.groups[group],0x18,count);
   for(unsigned i=0;i<count;++i)Put<void*>(p.entries[group][i],0,p.nodes[group][i]);}
 }
 current=&peers[0];enabled=true;role=true;pvpReady=true;preflight=true;definitionOkay=true;dependencies=true;cleanupBlock=false;entryPending=false;
 failFactory=false;failPvp=false;omitCtorBridge=false;corruptOwner=false;clearRoleInFactory=false;badCtor=false;useFallback=false;throwCtor=false;badBulletPin=false;throwRootDrop=false;
 nativeGroups=nativeCtors=nativeFactories=deleted=refused=noNullReturns=partNow=activations=0;
}
void* __fastcall ActualFallback(void*,void* out,const float* matrix,void* descriptor);
void* __fastcall NativeCtor(void* storage,const void* descriptor) {
 ++nativeCtors;auto& p=*current;auto* child=static_cast<unsigned char*>(storage);const unsigned n=p.childCount++;
 Weak(child,p.childControl[n]);Put<void*>(child,0x38,p.body);Put<void*>(child,0x5C0,p.body+0x8A0);
 CaptureCtor(descriptor,p.observed+n);
 alignas(16) unsigned char actual[0x1E0]{};Put<const void*>(actual,0x38,p.body);Put<const void*>(actual,0x40,p.control);
 Put<std::uint32_t>(actual,0x90,0x1234);Put<std::uint32_t>(actual,0x1A0,At<unsigned>(descriptor,0xF4));Put<std::uint32_t>(actual,0x1A4,partNow);
 alignas(16) float matrix[16];Identity(matrix,p.offset+partNow*10);matrix[13]=42;
 void* bullet=nullptr;
 if(useFallback){void* out[2]={reinterpret_cast<void*>(0x12345678),reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x87654321u))};InvokeWalkingPartFallback(nullptr,out,matrix,actual,&ActualFallback);bullet=out[0];}
 else if(unitProvider.Owns(nullptr,actual))bullet=unitProvider.Invoke(nullptr,matrix,reinterpret_cast<void*>(0x1234),actual);
 if(bullet){Put<void*>(child,0x5D0,bullet);Put<const void*>(child,0x5D8,At<const void*>(bullet,0x30));}
 auto* slot=static_cast<unsigned char*>(At<void*>(descriptor,0x20));Put<void*>(slot,0,child);Put<void*>(slot,8,p.childControl[n]);
 if(throwCtor)RaiseException(0xE0424444,0,0,nullptr);
 return child;
}
void* GuardCtor(void* storage,const void* descriptor) {
 void* child=omitCtorBridge ? NativeCtor(storage,descriptor) : InvokeWalkingPartConstructor(storage,descriptor,&NativeCtor);
 if(!child){++refused;child=NativeCtor(storage,descriptor);Delete(child);} // sole PvP guard preserves native nonnull ABI
 if(child)++noNullReturns;return child;
}
void __fastcall NativeGroup(void* object,int group) {
 ++nativeGroups;auto* p=Owner(object);if(!p)return;current=p;const auto count=At<std::size_t>(p->groups[group],0x18);
 for(unsigned i=0;i<count;++i){partNow=i;alignas(16) unsigned char descriptor[0x148]{};alignas(16) float world[16],model[16];Identity(world,p->offset);Identity(model,p->offset+7);
  void* out[2]{};Put<void*>(descriptor,8,world);Put<void*>(descriptor,0x20,out);Put<void*>(descriptor,0x30,corruptOwner ? nullptr : p->body);
  Put<void*>(descriptor,0x38,p->body+0x8A0);Put<void*>(descriptor,0x40,p->nodes[group][i]);
  for(unsigned k=0;k<11;++k)Put<float>(descriptor,kCtorWords[k],static_cast<float>(k+1)+p->offset);Put<unsigned char>(descriptor,0x110,1);
  const auto seed=At<std::uint64_t>(p->body,0x3E8)*0x5D588B656C078965ull+0x269EC3;Put<std::uint64_t>(p->body,0x3E8,seed);Put<unsigned>(descriptor,0xF4,static_cast<unsigned>((seed>>32)&0xFFFF));
  float vector[4]={1001,1002,1003,1};std::memcpy(descriptor+0x60,vector,16);std::memcpy(descriptor+0x70,vector,16);std::memcpy(descriptor+0x80,model,64);
  if(badCtor)Put<float>(descriptor,0x48,(std::numeric_limits<float>::quiet_NaN)());
  auto* child=static_cast<unsigned char*>(GuardCtor(p->child[p->childCount],descriptor));
  if(!child)RaiseException(0xE0425555,0,0,nullptr);
  if(child[0x18]&4)continue;
  const auto at=At<std::size_t>(p->body,0x1820);Put<void*>(p->created[at],0,child);Put<const void*>(p->created[at],8,At<const void*>(child,0x30));Put<std::size_t>(p->body,0x1820,at+1);
  p->body[0x1940]=static_cast<unsigned char>((p->body[0x1940]+1)&15);
 }
}
void* CreateBullet(const float* matrix,void* descriptor) {
 if(failFactory)return nullptr;auto& p=*current;const unsigned n=p.bulletCount++;auto* bullet=p.bullet[n];Weak(bullet,p.bulletControl[n]);
 auto& sample=p.emitted[n];std::memcpy(sample.matrix,matrix,64);for(unsigned i=0;i<kProjectileWords;++i)sample.scalar[i]=At<unsigned>(descriptor,kProjectileScalar32[i]);
 sample.random=At<unsigned>(descriptor,0x1A0);sample.shot=At<unsigned>(descriptor,0x1A4);
 if(badBulletPin)Put<LONG>(p.bulletControl[n],8,(std::numeric_limits<LONG>::max)());
 ++nativeFactories;if(clearRoleInFactory)enabled=false;return bullet;
}
void* __fastcall ActualFallback(void*,void* out,const float* matrix,void* descriptor) {
 void* bullet=CreateBullet(matrix,descriptor);Put<void*>(out,0,bullet);Put<const void*>(out,8,bullet ? At<const void*>(bullet,0x30) : nullptr);return out;
}
}
namespace crew {
bool EnemyControlBerserkerPvpReady() noexcept{return pvpReady;}
bool EnemyControlBerserkerPvpPreflight(const ObjRef&,unsigned) noexcept{return preflight;}
bool EnemyControlBerserkerPvpExecuteParts(void* root,int index,void(__fastcall* native)(void*,int)) noexcept {
 __try {if(failPvp)return false;native(root,index);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlBerserkerPvpExecutePartsValidated(void* root,int index,void(__fastcall* native)(void*,int),bool(*validate)(void*) noexcept,void* context) noexcept {
 __try {if(failPvp)return false;native(root,index);if(!validate(context))return false;++activations;return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlWeaponAim(const void*,float out[3]) noexcept{std::memcpy(out,canonical,12);return true;}
}
namespace crew::enemy_native {
const NativeProfile* Profiles(std::size_t* count) noexcept{*count=3;return profiles;}
bool NativeActionRole(const void*,bool* authority) noexcept{*authority=role;return enabled;}
bool NativeActionQuiescing(const void*) noexcept{return false;}
bool WalkingAcceptedEntryPending(const NativeLease&) noexcept{return entryPending;}
bool WalkingAcceptedAimPoint(const void*,float out[3]) noexcept{std::memcpy(out,canonical,12);return true;}
bool RegisterNativeActionExtension(const NativeActionExtension& e) noexcept{extension=e;return true;}
bool RegisterNativeUnitFactoryProvider(const NativeUnitFactoryProvider& e) noexcept{unitProvider=e;return true;}
bool NativeUnitFactoryProviderReady(const NativeUnitFactoryProvider& e) noexcept{return e.tag==unitProvider.tag;}
void* InvokeOriginalNativeUnitFactory(void*,const float* matrix,void*,void* descriptor){return CreateBullet(matrix,descriptor);}
void* WalkingPartsTestFunction(unsigned at) noexcept {if(at==0x33FCB0)return reinterpret_cast<void*>(&NativeGroup);if(at==0x118A1B0)return reinterpret_cast<void*>(&Delete);return nullptr;}
bool WalkingPartsTestDefinition(const NativeLease&,unsigned,std::uint64_t* out) noexcept{*out=definition;return definitionOkay;}
bool ValidNativeProjectileSample(const NativeProjectileSample& s) noexcept{return Matrix(s.matrix) && (!s.hasMuzzle || Matrix(s.muzzleMatrix)) && s.resourceMask<=3;}
bool CaptureNativeProjectileSample(const NativeLease&,const void*,const float* matrix,const void* descriptor,NativeProjectileSample* out) noexcept {
 NativeProjectileSample s{};std::memcpy(s.matrix,matrix,64);for(unsigned i=0;i<kProjectileWords;++i)s.scalar[i]=At<unsigned>(descriptor,kProjectileScalar32[i]);
 for(unsigned i=0;i<kProjectileBytes;++i)s.bytes[i]=At<unsigned char>(descriptor,kProjectileScalar8[i]);s.random=At<unsigned>(descriptor,0x1A0);s.shot=At<unsigned>(descriptor,0x1A4);s.flags=At<unsigned>(descriptor,0x1A8);
 *out=s;return ValidNativeProjectileSample(s);
}
bool NativeProjectileDependenciesReady(const NativeLease&,const NativeProjectileSample&) noexcept{return dependencies;}
bool CanPrepareNativeProjectileSample(const NativeLease&,const void*,float*,const NativeProjectileSample& s) noexcept{return dependencies && ValidNativeProjectileSample(s);}
bool CanReplayNativeProjectileSample(const NativeLease& l,void*,const void* d,float* m,const NativeProjectileSample& s) noexcept{return CanPrepareNativeProjectileSample(l,d,m,s);}
bool NativeProjectileJournalEmpty(const NativeProjectileJournal& j) noexcept{return !j.ownerObject && !j.made && !j.uncertain && !j.busy;}
bool RestoreNativeProjectileJournal(NativeProjectileJournal& j) noexcept{if(cleanupBlock || j.uncertain)return false;j={};return true;}
void* WithNativeProjectileSampleInPlace(NativeProjectileJournal& j,const NativeLease& l,const void* local,float*,const NativeProjectileSample& s,NativeProjectileCreateFn create,void* user) noexcept {
 if(!NativeProjectileJournalEmpty(j) || !dependencies)return nullptr;j.ownerObject=l.object;std::memcpy(j.descriptor,local,0x1E0);
 for(unsigned i=0;i<kProjectileWords;++i)Put<unsigned>(j.descriptor,kProjectileScalar32[i],s.scalar[i]);for(unsigned i=0;i<kProjectileBytes;++i)Put<unsigned char>(j.descriptor,kProjectileScalar8[i],s.bytes[i]);
 Put<unsigned>(j.descriptor,0x1A0,s.random);Put<unsigned>(j.descriptor,0x1A4,s.shot);Put<unsigned>(j.descriptor,0x1A8,s.flags);
 j.created=create(user,s.matrix,j.descriptor);void* result=j.created;if(cleanupBlock){j.made=true;return nullptr;}j={};return result;
}
}
int main() {
 using namespace crew;using namespace crew::enemy_native;using namespace crew::enemy_native::walking_parts;
 image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!image)return 2;
 Reset();pvpReady=false;Check(!InstallWalkingPartsAcceptedActions(),"PvP sole caller guards must be ready before the entry hook");pvpReady=true;
 Check(InstallWalkingPartsAcceptedActions() && WalkingPartsControlReady(),"complete native group adapter installs with central provider and extension");
 auto host=LeaseFor(peers[0]),guest=LeaseFor(peers[1]);
 Check(BeforeWalkingParts(host,true,nullptr),"host binds exact retained root");
 Shoot(host.object,0);Check(nativeGroups==1 && nativeCtors==2 && nativeFactories==2,"host creates every real linked part with one original group");
 auto* h=Find(host);Check(h && h->count==2 && h->queue[0].record.complete && h->queue[1].record.complete,"publish waits for complete native links and PvP group success");
 Check(activations==1,"the complete-group observer validates real outcomes before activation publication");
 Check(peers[0].body[0x1940]==0,"native binding ordinal wraps as the original engine does");
 unsigned char packets[4][512]{};std::size_t sizes[4]{};
 entryPending=true;std::size_t yielded=99;const auto heldBefore=At<LONG>(peers[0].bulletControl[0],8);
 Check(CaptureWalkingParts(host,packets[0],512,&yielded) && !yielded,"uncaptured root action entry forces a successful zero-byte parts yield");
 Check(h->count==2 && !h->head && !h->queue[0].phase && At<LONG>(peers[0].bulletControl[0],8)==heldBefore,"entry-prefix yield neither dequeues nor releases any actual linked outcome");
 entryPending=false;
 for(unsigned i=0;i<4;++i)Check(CaptureWalkingParts(host,packets[i],512,sizes+i) && sizes[i]>0,"one bounded typed outcome fragment captured");
 for(unsigned packet=0;packet<4;++packet)for(std::size_t n=0;n<sizes[packet];++n){Packet decoded{};Check(!Decode(packets[packet],n,&decoded),"every truncated packet is rejected without a partial decode");}
 Check(sizes[0]==kCtorPacketBytes && sizes[1]==kShotPacketBytes && sizes[1]<=512,"constructor and actual sample packets have explicit bounded sizes");
 Check(WalkingPartsEmpty(host),"host journal releases all extra refs only after capture");
 role=false;Check(BeforeWalkingParts(guest,false,nullptr),"guest binds independent current root");
 const auto before=nativeGroups;Shoot(guest.object,0);Check(nativeGroups==before && !peers[1].childCount,"guest MAB defers before any native constructor or PvP prepare");
 Check(ApplyWalkingParts(guest,packets[1],sizes[1])==Result::failed,"out of order shot has no native mutation");
 for(unsigned i=0;i<3;++i)Check(ApplyWalkingParts(guest,packets[i],sizes[i])==Result::applied,"partial group is persistently staged and exactly acknowledged");
 Check(!peers[1].childCount && !WalkingPartsEmpty(guest) && !RestoreWalkingParts(guest),"incomplete accepted group retains identity without creating a partial body");
 const auto seed=At<std::uint64_t>(guest.object,0x3E8);dependencies=false;
 Check(ApplyWalkingParts(guest,packets[3],sizes[3])==Result::pending && !peers[1].childCount && At<std::uint64_t>(guest.object,0x3E8)==seed,"missing projectile dependency stays pending before all native/RNG changes");
 const auto pendingByte=packets[3][sizes[3]-1];packets[3][sizes[3]-1]^=1;
 Check(ApplyWalkingParts(guest,packets[3],sizes[3])==Result::failed && !peers[1].childCount,"a conflicting pending final fragment cannot replace its staged native outcome");packets[3][sizes[3]-1]=pendingByte;
 dependencies=true;Check(ApplyWalkingParts(guest,packets[3],sizes[3])==Result::applied,"final accepted fragment atomically creates the complete native group");
 Check(peers[1].childCount==2 && peers[1].bulletCount==2 && noNullReturns==4,"every native constructor returns its genuine linked bullet body");
 for(unsigned i=0;i<2;++i){Check(peers[0].observed[i].values[16]==canonical[0] && peers[0].observed[i].values[17]==canonical[1] && peers[0].observed[i].values[18]==canonical[2],"host constructor replaces an unrelated AI target with the single accepted canonical point");
  Check(peers[1].observed[i].values[12]==peers[0].observed[i].values[12],"guest constructor uses actual host world pose while keeping local resources");
  Check(!std::memcmp(peers[1].emitted[i].matrix,peers[0].emitted[i].matrix,64) && peers[1].emitted[i].random==peers[0].emitted[i].random,"guest physical factory gets exact actual host matrix and random value");
  Check(At<const void*>(peers[1].child[i],0x5D0)==peers[1].bullet[i] && At<const void*>(peers[1].child[i],0x5D8)==peers[1].bulletControl[i],"original ctor preserves the real 5D0/5D8 bullet link");}
 Check(At<std::uint64_t>(host.object,0x3E8)==At<std::uint64_t>(guest.object,0x3E8) && host.object[0x1940]==guest.object[0x1940],"continuing native RNG and part identity cursor match host outcomes");
 const auto made=nativeFactories;for(unsigned i=0;i<4;++i)Check(ApplyWalkingParts(guest,packets[i],sizes[i])==Result::applied,"accepted duplicate fragment matches its receipt");
 Check(nativeFactories==made,"duplicate packets never respawn a linked native bullet");
 auto changed=packets[3][sizes[3]-1];packets[3][sizes[3]-1]^=1;Check(ApplyWalkingParts(guest,packets[3],sizes[3])==Result::failed,"conflicting duplicate is rejected");packets[3][sizes[3]-1]=changed;
 Check(QuiesceWalkingParts(guest) && WalkingPartsEmpty(guest) && RestoreWalkingParts(guest),"normal close drains all accepted creation and restores references");
 Check(ApplyWalkingParts(guest,packets[3],sizes[3])==Result::applied,"retired journal still acknowledges an exact old duplicate");
 Reset(1);host=LeaseFor(peers[0]);guest=LeaseFor(peers[1]);useFallback=true;Check(BeforeWalkingParts(host,true,nullptr),"native fallback host admitted");Shoot(host.object,1);
 Check(peers[0].bulletCount==1 && Find(host)->queue[0].record.fallback,"native direct fallback is captured as an actual linked creation");
 for(unsigned i=0;i<2;++i)Check(CaptureWalkingParts(host,packets[i],512,sizes+i),"fallback outcome captured");role=false;Check(BeforeWalkingParts(guest,false,nullptr),"fallback guest binds");
 Check(ApplyWalkingParts(guest,packets[0],sizes[0])==Result::applied && ApplyWalkingParts(guest,packets[1],sizes[1])==Result::applied && peers[1].bulletCount==1,"fallback replay synchronously returns a true native weak pair");
 Reset(1);host=LeaseFor(peers[0]);Check(BeforeWalkingParts(host,true,nullptr),"partial-failure host binds");badCtor=true;Shoot(host.object,0);
 Check(Find(host)->faulted && !Find(host)->queue[0].record.complete && !nativeFactories && refused==1 && noNullReturns==1,"constructor refusal stays behind PvP ABI guard and publishes no fake/empty shot");
 std::size_t used=0;Check(!CaptureWalkingParts(host,packets[0],512,&used),"failed whole group cannot be published as accepted");
 Check(QuiesceWalkingParts(host) && CaptureWalkingParts(host,packets[0],512,&used) && !used && WalkingPartsEmpty(host) && RestoreWalkingParts(host),"quiescence clears only unpublished failed groups through native cleanup");
 Reset(1);host=LeaseFor(peers[0]);guest=LeaseFor(peers[1]);Check(BeforeWalkingParts(host,true,nullptr),"cleanup test host binds");Shoot(host.object,0);
 for(unsigned i=0;i<2;++i)Check(CaptureWalkingParts(host,packets[i],512,sizes+i),"cleanup fixture actual group captured");role=false;Check(BeforeWalkingParts(guest,false,nullptr),"cleanup test guest binds");
 Check(ApplyWalkingParts(guest,packets[0],sizes[0])==Result::applied,"cleanup test stages constructor");cleanupBlock=true;
 Check(ApplyWalkingParts(guest,packets[1],sizes[1])==Result::failed && peers[1].bulletCount==1 && At<const void*>(peers[1].child[0],0x5D0)==peers[1].bullet[0],"successful creation with delayed cleanup keeps its real link and never ACKs");
 Check(!WalkingPartsEmpty(guest) && !RestoreWalkingParts(guest) && !AbortWalkingParts(guest),"failed descriptor cleanup retains the complete persistent ownership ledger");
 cleanupBlock=false;Check(AbortWalkingParts(guest) && deleted==2,"trusted abort restores journal then removes the unacknowledged real objects");
 Reset(1);host=LeaseFor(peers[0]);Check(BeforeWalkingParts(host,true,nullptr),"reentry host binds");clearRoleInFactory=true;Shoot(host.object,0);
 Check(Find(host)->faulted && !Find(host)->queue[0].record.complete,"authority loss during native callbacks cannot publish the old group");
 Check(!activations,"failed actual linked-group validation prevents final native activation");
 enabled=true;Check(AbortWalkingParts(host),"trusted old-epoch abort cleans the held partial body");
 Reset(1);host=LeaseFor(peers[0]);NativeProfile copy=profiles[0];host.profile=&copy;Check(!BeforeWalkingParts(host,true,nullptr),"copied unadmitted profile cannot bind the creator");
 host=LeaseFor(peers[0]);Put<std::size_t>(host.object,0x1800,4097);Check(BeforeWalkingParts(host,true,nullptr),"root binding precedes immutable group preflight");Shoot(host.object,0);Check(!nativeGroups && Find(host)->faulted,"oversized native group table is refused before original creation");

 Reset(1);host=LeaseFor(peers[0]);Check(BeforeWalkingParts(host,true,nullptr),"fallback refusal fixture binds");
 auto* fallbackStream=Find(host);Scope fallbackScope{};fallbackScope.stream=fallbackStream;fallbackScope.meta.count=1;fallbackScope.current=kParts;
 void* sentinel[2]={reinterpret_cast<void*>(0x1234),reinterpret_cast<void*>(0x5678)};walking_parts::scope=&fallbackScope;
 Check(InvokeWalkingPartFallback(nullptr,sentinel,identity,nullptr,&ActualFallback)==sentinel && !sentinel[0] && !sentinel[1],"fallback refusal zeroes the exact native out weak pair before returning");walking_parts::scope=nullptr;
 Check(AbortWalkingParts(host),"empty refused fallback restores its root pin");
 Reset(1);host=LeaseFor(peers[0]);Check(BeforeWalkingParts(host,true,nullptr),"native nonnull pin failure fixture binds");badBulletPin=true;Shoot(host.object,0);
 Check(peers[0].bulletCount==1 && At<const void*>(peers[0].child[0],0x5D0)==peers[0].bullet[0],"a genuine created bullet is never swallowed when subsequent pin admission fails");
 Check(Find(host)->uncertain && !WalkingPartsEmpty(host) && !RestoreWalkingParts(host),"unfixed genuine identity retains an uncertain ledger instead of allowing a respawn");
 Reset(1);host=LeaseFor(peers[0]);Check(BeforeWalkingParts(host,true,nullptr),"missing constructor hook fixture binds");omitCtorBridge=true;Shoot(host.object,0);
 Check(!nativeFactories && Find(host)->faulted && !activations,"missing typed constructor scope cannot fall through to an uncontrolled central factory");
 Check(AbortWalkingParts(host),"missing-scope attempt has no forgotten projectile journal");
 Reset(1);host=LeaseFor(peers[0]);Check(BeforeWalkingParts(host,true,nullptr),"closing host fixture binds");Shoot(host.object,0);Check(QuiesceWalkingParts(host),"close stops future native group triggers");
 const auto groupsBefore=nativeGroups;Shoot(host.object,0);Check(nativeGroups==groupsBefore,"quiescing never starts a fresh native group");
 for(unsigned i=0;i<2;++i)Check(CaptureWalkingParts(host,packets[i],512,sizes+i),"an already completed accepted group remains drainable while closing");
 Check(WalkingPartsEmpty(host) && RestoreWalkingParts(host),"close only releases after both actual fragments are captured");
 Reset(1);host=LeaseFor(peers[0]);Check(BeforeWalkingParts(host,true,nullptr),"wrong asset fixture host binds");Shoot(host.object,0);
 for(unsigned i=0;i<2;++i)Check(CaptureWalkingParts(host,packets[i],512,sizes+i),"wrong asset fixture actual output captured");guest=LeaseFor(peers[1]);role=false;Check(BeforeWalkingParts(guest,false,nullptr),"wrong asset fixture guest binds");
 ++definition;Check(ApplyWalkingParts(guest,packets[0],sizes[0])==Result::failed && !peers[1].childCount && WalkingPartsEmpty(guest),"semantic asset mismatch refuses before any stage or native creation");--definition;
 Check(ApplyWalkingParts(guest,packets[0],sizes[0])==Result::applied,"matching semantic asset stages normally");
 Packet bad{};Check(Decode(packets[1],sizes[1],&bad),"actual sample packet decodes for an independent malformed-field fixture");bad.sample.hasMuzzle=true;Identity(bad.sample.muzzleMatrix);
 std::size_t invalidSize=0;Check(!Encode(bad,packets[2],512,&invalidSize),"the native Part constructor's nonexistent persistent muzzle pointer cannot be introduced by wire data");
 Check(AbortWalkingParts(guest) && WalkingPartsEmpty(guest),"trusted abort discards an uncreated partial group without spawning anything");

 Reset(1);host=LeaseFor(peers[0]);Check(BeforeWalkingParts(host,true,nullptr),"last-root native destruction fixture binds");Put<LONG>(host.control ? const_cast<void*>(host.control) : nullptr,8,1);throwRootDrop=true;
 Check(!RestoreWalkingParts(host) && Find(host) && Find(host)->uncertain && !WalkingPartsEmpty(host),"native root destructor uncertainty remains discoverable after its single decrement");
 Check(!RestoreWalkingParts(host) && !AbortWalkingParts(host),"uncertain final root release is never retried or forgotten as an empty stream");
 std::printf("%d checks, %d failed\n",checks,failures);VirtualFree(image,0,MEM_RELEASE);return failures ? 1 : 0;
}
