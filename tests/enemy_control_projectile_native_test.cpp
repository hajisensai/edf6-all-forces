// Exercises the production semantic descriptor helper on private fixture memory.
// Native boundaries are explicit seams; no EDF.dll or game process is loaded.
#define ENEMY_CONTROL_PROJECTILE_NATIVE_TEST
#include "../src/enemy_control_projectile_native.cpp"
#include <cstdio>
#include <limits>
#include <type_traits>
namespace crew {unsigned char* image=nullptr;}
namespace {
using namespace crew;
using namespace crew::enemy_native;
using Ref=projectile_native::Ref;
static_assert(kProjectileWords==30 && kProjectileBytes==11,"declared semantic scalar fields");
static_assert(std::is_same<decltype(NativeProjectileSample::matrix),float[16]>::value,"matrix values, no address");
static_assert(std::is_same<decltype(NativeProjectileSample::scalar),std::uint32_t[30]>::value,"explicit words, no opaque descriptor");
static_assert(std::is_same<decltype(NativeProjectileSample::bytes),unsigned char[11]>::value,"explicit bytes");
static_assert(std::is_same<decltype(NativeProjectileSample::target),unsigned char[32]>::value &&
              std::is_same<decltype(NativeProjectileSample::extra),unsigned char[32]>::value,"portable full actor IDs");
int checks=0,failures=0;
void Check(bool ok,const char* label,int line){++checks;if(!ok){++failures;std::printf("FAIL %d %s\n",line,label);}}
#define CHECK(x) Check((x),#x,__LINE__)
struct ActorMemory {alignas(16) unsigned char body[0x400]{},control[32]{},id[32]{};};
ActorMemory actors[7];
alignas(16) unsigned char lockPoints[2][2][32]{},lockControls[2][2][32]{},lockRows[2][32]{};
alignas(16) unsigned char templates[2][0x1E0]{},actual[0x1E0]{},resources[2][2][16]{},payloads[2][2][32]{};
alignas(16) float muzzle[2][16]{};
alignas(16) unsigned char manager[0x1000]{};
void* factoryTable[3]{};void* factory=factoryTable;
void* controlTable[2]{};
unsigned side=0,copyCalls=0,destroyCalls=0,createCalls=0,spawnCalls=0,moves=0;
unsigned strongDestroyed=0,weakDestroyed=0;
bool wrongResolver=false,resolveFails=false,copyFault=false,createFault=false,returnNull=false;
bool inspected=false,copyPartial=false,destroyFault=false,strongFault=false,dropRootDuringCreate=false,protectAtCreate=false;
bool dropActorOnIdRead=false;unsigned char* dropInterfaceOnIdRead=nullptr;
unsigned journalEmits=0;void* protectPage=nullptr;DWORD protectSaved=0;
NativeLease expectedLease{};NativeProjectileSample expectedSample{};
constexpr std::size_t kImageSize=0x20B4000;
constexpr unsigned kWeaks[]={0x38,0x60,0x78,0x1D0};
void __fastcall StrongDestroyed(void*){++strongDestroyed;if(strongFault)RaiseException(0xE0421914,0,0,nullptr);}
void __fastcall WeakDestroyed(void*){++weakDestroyed;}
void Identity(float* m,float x=0){std::memset(m,0,64);m[0]=m[5]=m[10]=m[15]=1;m[12]=x;m[13]=4;m[14]=-3;}
void InitControl(unsigned char* c){Put<void*>(c,0,controlTable);Put<LONG>(c,8,1);Put<LONG>(c,12,20);}
void WeakRetain(Ref r){if(r.control)InterlockedIncrement(reinterpret_cast<volatile LONG*>(r.control+12));}
void WeakRelease(Ref r){if(r.control && InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+12))==0)++weakDestroyed;}
// These boundary fixtures implement the proven native ownership contract, not
// the production capture/replay logic. The read-only audit checks the real bodies.
void* __fastcall Copy(void* dst,const void* source){
    ++copyCalls;if(copyFault){if(copyPartial){std::memcpy(dst,source,0x1E0);WeakRetain(At<Ref>(dst,0x38));}RaiseException(0xE0421911,0,0,nullptr);}
    std::memcpy(dst,source,0x1E0);
    for(unsigned offset:kWeaks)WeakRetain(At<Ref>(dst,offset));
    for(unsigned offset:{0x180u,0x190u}) {
        auto* c=At<unsigned char*>(source,offset);
        if(c){InterlockedIncrement(reinterpret_cast<volatile LONG*>(c));InterlockedIncrement(reinterpret_cast<volatile LONG*>(c+4));Put<void*>(dst,offset+8,At<void*>(c,8));}
    }
    return dst;
}
void __fastcall Destroy(void* p){
    ++destroyCalls;if(destroyFault)RaiseException(0xE0421913,0,0,nullptr);
    for(unsigned offset:kWeaks)WeakRelease(At<Ref>(p,offset));
    for(unsigned offset:{0x180u,0x190u})if(auto* c=At<unsigned char*>(p,offset)) {
        InterlockedDecrement(reinterpret_cast<volatile LONG*>(c));InterlockedDecrement(reinterpret_cast<volatile LONG*>(c+4));
    }
    std::memset(p,0xDD,0x1E0);
}
void __fastcall Move(void* dst,Ref* source){
    ++moves;const Ref old=At<Ref>(dst,0);Put<Ref>(dst,0,*source);*source={};WeakRelease(old);
}
Ref* __fastcall Lock(void* actor,Ref* out,int index){
    *out={};if(index>=0 && static_cast<std::size_t>(index)<At<std::size_t>(actor,0x378)) {
        const auto* rows=At<const unsigned char*>(actor,0x368);*out=At<Ref>(rows+index*16,0);WeakRetain(*out);
    }
    return out;
}
NativeLease Lease(unsigned peer){NativeLease l{};l.object=actors[peer*3].body;l.control=actors[peer*3].control;return l;}
}  // namespace
namespace crew::netref_stub {extern int ambiguousSlot;}   // net_object_ref_stub.h
namespace {
bool Balanced(){
    for(const auto& a:actors)if(At<LONG>(a.control,8)!=1 || At<LONG>(a.control,12)!=20)return false;
    for(const auto& peer:lockControls)for(const auto& c:peer)if(At<LONG>(c,8)!=1 || At<LONG>(c,12)!=20)return false;
    for(const auto& peer:resources)for(const auto& c:peer)if(At<LONG>(c,0)!=3 || At<LONG>(c,4)!=7)return false;
    return true;
}
void Setup(){
    Put<void*>(image,0x20B2AC0,nullptr);
    side=0;wrongResolver=resolveFails=copyFault=createFault=returnNull=inspected=false;
    copyPartial=destroyFault=strongFault=dropRootDuringCreate=protectAtCreate=false;dropActorOnIdRead=false;dropInterfaceOnIdRead=nullptr;journalEmits=0;protectPage=nullptr;protectSaved=0;
    copyCalls=destroyCalls=createCalls=spawnCalls=moves=strongDestroyed=weakDestroyed=0;
    std::memset(actors,0,sizeof(actors));std::memset(lockPoints,0,sizeof(lockPoints));std::memset(lockControls,0,sizeof(lockControls));
    std::memset(lockRows,0,sizeof(lockRows));std::memset(templates,0,sizeof(templates));std::memset(actual,0,sizeof(actual));
    std::memset(resources,0,sizeof(resources));std::memset(payloads,0,sizeof(payloads));
    for(unsigned i=0;i<7;++i){
        auto& a=actors[i];InitControl(a.control);Put<void*>(a.body,0x28,a.body);Put<void*>(a.body,0x30,a.control);Put<int>(a.body,0x318,2);
        Put<std::uint32_t>(a.id,0,0x13572468);a.id[8]=static_cast<unsigned char>(i%3+1);a.id[31]=static_cast<unsigned char>(0x50+i%3);
    }
    for(unsigned peer=0;peer<2;++peer){
        Identity(muzzle[peer],10.0f+peer);
        auto& target=actors[peer*3+1];Put<void*>(target.body,0x368,lockRows[peer]);Put<std::size_t>(target.body,0x370,2);Put<std::size_t>(target.body,0x378,2);
        for(unsigned j=0;j<2;++j){InitControl(lockControls[peer][j]);Put<void*>(lockPoints[peer][j],8,target.body);Put<Ref>(lockRows[peer]+j*16,0,{lockPoints[peer][j],lockControls[peer][j]});}
        auto* t=templates[peer];Put<void*>(t,0,image+0x1769028);Put<std::uint16_t>(t,0xF8,0xFFFF);Put<std::uint16_t>(t,0x1C0,0xFFFF);
        Put<Ref>(t,0x38,{actors[6].body,actors[6].control});Put<Ref>(t,0x1D0,{actors[6].body,actors[6].control});
        for(unsigned j=0;j<2;++j){Put<LONG>(resources[peer][j],0,3);Put<LONG>(resources[peer][j],4,7);Put<void*>(resources[peer][j],8,payloads[peer][j]);Put<void*>(t,0x180+j*16,resources[peer][j]);Put<void*>(t,0x188+j*16,payloads[peer][j]);}
        // Resource pointers and local handle IDs differ between simulated peers.
        Put<void*>(t,0x100,payloads[peer][0]);Put<void*>(t,0x120,payloads[peer][1]);Put<std::uint32_t>(t,0x128,100+peer);
    }
    std::memcpy(actual,templates[0],sizeof(actual));
    Put<Ref>(actual,0x38,{actors[0].body,actors[0].control});Put<Ref>(actual,0x60,{lockPoints[0][1],lockControls[0][1]});
    Put<Ref>(actual,0x78,{actors[2].body,actors[2].control});Put<void*>(actual,0x88,actors[2].body);Put<float*>(actual,0x70,muzzle[0]);
    for(unsigned i=0;i<kProjectileWords;++i)Put<std::uint32_t>(actual,kProjectileScalar32[i],0x10203040u+i*0x101u);
    for(unsigned i=0;i<kProjectileBytes;++i)Put<unsigned char>(actual,kProjectileScalar8[i],static_cast<unsigned char>(i*13+3));
    Put<std::uint16_t>(actual,0x178,0x5713);Put<std::uint32_t>(actual,0x1A0,0xF1234567);Put<std::uint32_t>(actual,0x1A4,0x89ABCDEF);Put<std::uint32_t>(actual,0x1A8,0xA1B2C3D4);
    factoryTable[0]=factoryTable[1]=nullptr;factoryTable[2]=reinterpret_cast<void*>(&StrongDestroyed);factory=factoryTable;
    Put<void*>(image+0x20B2958,0,manager);expectedLease=Lease(1);expectedSample={};
}
void* CaptureCreated(void*,const float* matrix,void* p) noexcept {
    ++createCalls;inspected=true;
    CHECK((reinterpret_cast<std::uintptr_t>(matrix)&15u)==0 && (reinterpret_cast<std::uintptr_t>(p)&15u)==0);
    CHECK(std::memcmp(matrix,expectedSample.matrix,64)==0);
    CHECK(At<const void*>(p,0x38)==expectedLease.object && At<const void*>(p,0x40)==expectedLease.control);
    CHECK(At<const void*>(p,0x60)==(projectile_native::Zero(expectedSample.target) ? nullptr : lockPoints[side][expectedSample.lock]) && At<const void*>(p,0x68)==(projectile_native::Zero(expectedSample.target) ? nullptr : lockControls[side][expectedSample.lock]));
    CHECK(At<const void*>(p,0x78)==(projectile_native::Zero(expectedSample.extra) ? nullptr : actors[side*3+2].body) && At<const void*>(p,0x80)==(projectile_native::Zero(expectedSample.extra) ? nullptr : actors[side*3+2].control));
    CHECK(At<const void*>(p,0x88)==(expectedSample.extraObject ? actors[side*3+2].body : nullptr));
    CHECK(At<const void*>(p,0x70)==(expectedSample.hasMuzzle ? muzzle[side] : nullptr));
    bool scalar=true;for(unsigned i=0;i<kProjectileWords;++i)scalar=scalar && At<std::uint32_t>(p,kProjectileScalar32[i])==expectedSample.scalar[i];
    for(unsigned i=0;i<kProjectileBytes;++i)scalar=scalar && At<unsigned char>(p,kProjectileScalar8[i])==expectedSample.bytes[i];
    CHECK(scalar && At<std::uint16_t>(p,0x178)==expectedSample.scalar178 && At<std::uint32_t>(p,0x1A0)==expectedSample.random && At<std::uint32_t>(p,0x1A4)==expectedSample.shot && At<std::uint32_t>(p,0x1A8)==expectedSample.flags);
    CHECK(At<const void*>(p,0x180)==((expectedSample.resourceMask&1) ? resources[side][0] : nullptr) && At<const void*>(p,0x188)==((expectedSample.resourceMask&1) ? payloads[side][0] : nullptr) && At<const void*>(p,0x190)==((expectedSample.resourceMask&2) ? resources[side][1] : nullptr));
    CHECK(At<const void*>(p,0x100)==payloads[side][0] && At<std::uint32_t>(p,0x128)==100+side);
    CHECK(At<LONG>(resources[side][0],0)==3+((expectedSample.resourceMask&1) ? 1 : 0) && At<LONG>(resources[side][0],4)==7+((expectedSample.resourceMask&1) ? 1 : 0) && At<LONG>(resources[side][1],0)==3+((expectedSample.resourceMask&2) ? 1 : 0) && At<LONG>(resources[side][1],4)==7+((expectedSample.resourceMask&2) ? 1 : 0));
    CHECK(At<LONG>(actors[side*3].control,8)==2 && At<LONG>(actors[side*3+1].control,8)==(projectile_native::Zero(expectedSample.target) ? 1 : 2) && At<LONG>(actors[side*3+2].control,8)==(projectile_native::Zero(expectedSample.extra) ? 1 : 2));
    CHECK(At<LONG>(actors[6].control,12)==21); // template owner was replaced; its asset weak remains owned
    if(createFault)RaiseException(0xE0421912,0,0,nullptr);
    return returnNull ? nullptr : manager+32;
}
void* __fastcall Spawn(void* m,const float* matrix,void* f,void* p){++spawnCalls;CHECK(m==manager && f==&factory);return CaptureCreated(nullptr,matrix,p);}

NativeProjectileSample Sample();
void* JournalCreate(void*,const float*,void* p) noexcept {
 ++createCalls;++journalEmits;
 if(dropRootDuringCreate)Put<LONG>(actors[side*3].control,8,1);
 if(protectAtCreate){protectPage=At<void*>(p,0x180);CHECK(VirtualProtect(protectPage,0x1000,PAGE_READONLY,&protectSaved)!=0);Sleep(20);}
 if(createFault)RaiseException(0xE0421912,0,0,nullptr);
 return returnNull ? nullptr : manager+32;
}
void* RegistryCreate(void* user,const float* matrix,void* p) noexcept {
 const unsigned mask=*static_cast<const unsigned*>(user);
 if(mask&1)CHECK(!At<const void*>(p,0x60) && !At<const void*>(p,0x68));
 if(mask&2)CHECK(!At<const void*>(p,0x78) && !At<const void*>(p,0x80) && !At<const void*>(p,0x88));
 return JournalCreate(nullptr,matrix,p);
}
// Fixture-only teardown knows which mock operation ran. This is deliberately
// not a recovery procedure for an uncertain real native destructor.
void DisposeFixture(NativeProjectileJournal& j,bool partial=false){
 destroyFault=strongFault=copyFault=createFault=false;
 if(j.made)Destroy(j.descriptor);else if(partial)WeakRelease(At<Ref>(j.descriptor,0x38));
 for(unsigned i=0;i<3;++i)if(j.heldMask&(1u<<i))projectile_native::Unpin({j.objects[i],static_cast<unsigned char*>(const_cast<void*>(j.controls[i]))});
 j={};
}

void RegistryChecks(){
 Setup();auto s=Sample();side=1;
 alignas(16) unsigned char session[0xE0]{},iface[0x60]{},ifaceControl[32]{},registry[0x100]{},bucket[16]{},sentinel[16]{},nodes[3][0x40]{},records[3][0x20]{};
 InitControl(ifaceControl);Put<void*>(session,0xD0,iface);Put<void*>(session,0xD8,ifaceControl);Put<void*>(iface,0x50,registry);
 Put<void*>(registry,0xC8,sentinel);Put<void*>(registry,0xD8,bucket);Put<std::size_t>(registry,0xF0,0);
 for(unsigned i=0;i<3;++i){
  const auto* id=i==2 ? s.extra : s.target;std::memcpy(nodes[i]+0x10,id,20);std::memcpy(nodes[i]+0x28,id+24,8);
  Put<std::uint32_t>(nodes[i],0x24,0xCAFEBABE); // native key padding is not part of canonical identity
  Put<void*>(nodes[i],8,i ? nodes[i-1] : sentinel);Put<void*>(nodes[i],0x30,records[i]);
 }
 nodes[0][0x2F]^=0x80;Put<void*>(nodes[0],0x30,reinterpret_cast<void*>(1)); // same first20, another suffix; must skip without touching record
 Put<Ref>(records[1],0x10,{actors[4].body,actors[4].control});Put<Ref>(records[2],0x10,{actors[5].body,actors[5].control});
 Put<void*>(bucket,0,nodes[0]);Put<void*>(bucket,8,nodes[2]);Put<void*>(image,0x20B2AC0,session+0x98);
 Ref found{};CHECK(projectile_native::ResolveActor(s.target,&found) && found.object==actors[4].body && found.control==actors[4].control);
 void* strongActor=nullptr;const void* actorControl=nullptr;bool actorConsumed=false;
 CHECK(ResolveNativeProjectileActor(s.target,&strongActor,&actorControl) && strongActor==actors[4].body && actorControl==actors[4].control && At<LONG>(actors[4].control,8)==2);
 CHECK(ReleaseNativeProjectileLock(strongActor,actorControl,&actorConsumed) && actorConsumed && At<LONG>(actors[4].control,8)==1);
 Ref point{lockPoints[1][1],lockControls[1][1]};unsigned char lockId[32]{};std::uint32_t ordinal=99;
 CHECK(CaptureNativeProjectileLock(&point,lockId,&ordinal) && ordinal==1 && !std::memcmp(lockId,s.target,32));
 Ref badPoint{reinterpret_cast<void*>(1),lockControls[1][1]};std::memset(lockId,0xCC,32);ordinal=99;
 CHECK(!CaptureNativeProjectileLock(&badPoint,lockId,&ordinal) && projectile_native::Zero(lockId) && ordinal==0);
 void* strongPoint=reinterpret_cast<void*>(1);const void* strongControl=reinterpret_cast<void*>(1);
 CHECK(ResolveNativeProjectileLock(s.target,1,&strongPoint,&strongControl) && strongPoint==lockPoints[1][1] && strongControl==lockControls[1][1] && At<LONG>(lockControls[1][1],8)==2);
 bool consumed=false;CHECK(ReleaseNativeProjectileLock(strongPoint,strongControl,&consumed) && consumed && At<LONG>(lockControls[1][1],8)==1);
 CHECK(NativeProjectileDependenciesReady(Lease(1),s));const auto savedTemplate=At<void*>(templates[1],0);Put<void*>(templates[1],0,nullptr);
 CHECK(NativeProjectileDependenciesReady(Lease(1),s));Put<void*>(templates[1],0,savedTemplate);

 CHECK(At<LONG>(ifaceControl,8)==1 && At<LONG>(actors[4].control,8)==1 && CanPrepareNativeProjectileSample(Lease(1),templates[1],muzzle[1],s));
 actors[4].body[0x2E8]=1;CHECK(projectile_native::ResolveActor(s.target,&found) && found.object==actors[4].body && NativeProjectileDependenciesReady(Lease(1),s));CHECK(ResolveNativeProjectileActor(s.target,&strongActor,&actorControl) && strongActor==actors[4].body);
 CHECK(ReleaseNativeProjectileLock(strongActor,actorControl,&actorConsumed) && actorConsumed);actors[4].body[0x2E8]=0;
 auto missing=s;missing.target[31]^=0x20;CHECK(!CanPrepareNativeProjectileSample(Lease(1),templates[1],muzzle[1],missing) && !NativeProjectileDependenciesReady(Lease(1),missing));
 CHECK(!ResolveNativeProjectileLock(missing.target,1,&strongPoint,&strongControl) && !strongPoint && !strongControl);
 CHECK(!ResolveNativeProjectileActor(missing.target,&strongActor,&actorControl) && !strongActor && !actorControl);
 actors[4].id[31]^=1;CHECK(!projectile_native::ResolveActor(s.target,&found));actors[4].id[31]^=1;
 // A player target (its mission slot, net_object_ref.h) with no object here (left, deleted): an empty actor, as an expired
 // NetId's record, not a pending one (that would fault the stream after its timeout). Two of the slot: a failure.
 {unsigned char gone[32]{};CHECK(crew::netref::MakePlayer(3,gone));
  CHECK(projectile_native::ResolveActor(gone,&found) && !found.object && !found.control);
  void* none=nullptr;const void* noneControl=nullptr;
  CHECK(ResolveNativeProjectileActor(gone,&none,&noneControl) && !none && !noneControl);
  CHECK(ResolveNativeProjectileLock(gone,1,&none,&noneControl) && !none && !noneControl);
  crew::netref_stub::ambiguousSlot=3;CHECK(!projectile_native::ResolveActor(gone,&found));
  CHECK(!ResolveNativeProjectileActor(gone,&none,&noneControl) && !none);crew::netref_stub::ambiguousSlot=-1;}
 Put<LONG>(actors[4].control,8,0);Put<Ref>(records[1],0x10,{reinterpret_cast<void*>(1),actors[4].control});
 CHECK(projectile_native::ResolveActor(s.target,&found) && !found.object && !found.control);
 CHECK(ResolveNativeProjectileActor(s.target,&strongActor,&actorControl) && !strongActor && !actorControl);
 CHECK(ResolveNativeProjectileLock(s.target,1,&strongPoint,&strongControl) && !strongPoint && !strongControl && NativeProjectileDependenciesReady(Lease(1),s));
 CHECK(CanPrepareNativeProjectileSample(Lease(1),templates[1],muzzle[1],s));
 NativeProjectileJournal j{};unsigned expired=1;CHECK(WithNativeProjectileSampleInPlace(j,Lease(1),templates[1],muzzle[1],s,&RegistryCreate,&expired)==manager+32 && NativeProjectileJournalEmpty(j));
 CHECK(At<LONG>(actors[4].control,8)==0 && At<LONG>(ifaceControl,8)==1 && At<LONG>(resources[1][0],0)==3);
 Put<LONG>(actors[4].control,8,1);Put<Ref>(records[1],0x10,{actors[4].body,actors[4].control});
 Put<LONG>(actors[5].control,8,0);Put<Ref>(records[2],0x10,{reinterpret_cast<void*>(1),actors[5].control});
 CHECK(projectile_native::ResolveActor(s.extra,&found) && !found.object && CanPrepareNativeProjectileSample(Lease(1),templates[1],muzzle[1],s));
 expired=2;CHECK(WithNativeProjectileSampleInPlace(j,Lease(1),templates[1],muzzle[1],s,&RegistryCreate,&expired)==manager+32 && NativeProjectileJournalEmpty(j));
 Put<LONG>(actors[5].control,8,1);Put<Ref>(records[2],0x10,{actors[5].body,actors[5].control});
 Put<void*>(bucket,8,sentinel);CHECK(!projectile_native::ResolveActor(s.target,&found));Put<void*>(bucket,8,nodes[2]);
 Put<LONG>(ifaceControl,8,0);CHECK(!projectile_native::ResolveActor(s.target,&found));Put<LONG>(ifaceControl,8,1);
 // Lookup itself owns no temporary actor/session strong reference. Even a
 // lifetime change during a read must refuse before the final LP handoff;
 // there is no helper-owned destructor to throw after publishing a lock.
 dropActorOnIdRead=strongFault=true;const auto beforeLookupDtor=strongDestroyed;
 CHECK(!ResolveNativeProjectileLock(s.target,1,&strongPoint,&strongControl) && !strongPoint && !strongControl);
 strongFault=false;CHECK(At<LONG>(actors[4].control,8)==0 && At<LONG>(lockControls[1][1],8)==1 && strongDestroyed==beforeLookupDtor);Put<LONG>(actors[4].control,8,1);
 dropInterfaceOnIdRead=ifaceControl;strongFault=true;
 CHECK(!ResolveNativeProjectileLock(s.target,1,&strongPoint,&strongControl) && !strongPoint && !strongControl);
 strongFault=false;CHECK(At<LONG>(ifaceControl,8)==0 && At<LONG>(lockControls[1][1],8)==1 && strongDestroyed==beforeLookupDtor);Put<LONG>(ifaceControl,8,1);
 CHECK(Balanced() && At<LONG>(ifaceControl,8)==1 && At<LONG>(ifaceControl,12)==20);Put<void*>(image,0x20B2AC0,nullptr);
 auto* releaseControl=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));CHECK(releaseControl!=nullptr);if(!releaseControl)return;
 InitControl(releaseControl);DWORD old=0;CHECK(VirtualProtect(releaseControl,0x1000,PAGE_READONLY,&old)!=0);Sleep(20);consumed=true;
 CHECK(!ReleaseNativeProjectileLock(point.object,releaseControl,&consumed) && !consumed);
 CHECK(VirtualProtect(releaseControl,0x1000,PAGE_READWRITE,&old)!=0);Sleep(20);
 CHECK(ReleaseNativeProjectileLock(point.object,releaseControl,&consumed) && consumed && At<LONG>(releaseControl,8)==0);
 InitControl(releaseControl);strongFault=true;consumed=false;const auto destructions=strongDestroyed;
 CHECK(!ReleaseNativeProjectileLock(point.object,releaseControl,&consumed) && consumed);strongFault=false;
 CHECK(At<LONG>(releaseControl,8)==0 && strongDestroyed==destructions+1); // consumed: deliberately no retry
 VirtualFree(releaseControl,0,MEM_RELEASE);
}

void JournalChecks(){
 Setup();auto s=Sample();side=1;NativeProjectileJournal j{};
 CHECK(NativeProjectileJournalEmpty(j) && RestoreNativeProjectileJournal(j));
 auto outcome=WithNativeProjectileOutcomeInPlace(j,Lease(1),templates[1],muzzle[1],s,&JournalCreate,nullptr);
 CHECK(outcome.created==manager+32 && outcome.cleanupComplete && NativeProjectileJournalEmpty(j) && Balanced());
 returnNull=true;CHECK(!WithNativeProjectileSampleInPlace(j,Lease(1),templates[1],muzzle[1],s,&JournalCreate,nullptr) && NativeProjectileJournalEmpty(j) && Balanced());returnNull=false;
 createFault=true;const auto dtors=destroyCalls;
 CHECK(!WithNativeProjectileSampleInPlace(j,Lease(1),templates[1],muzzle[1],s,&JournalCreate,nullptr));createFault=false;
 CHECK(j.uncertain && j.createEntered && j.made && j.heldMask==7 && !j.busy && j.created==nullptr && destroyCalls==dtors);
 CHECK(j.ownerObject==Lease(1).object && j.ownerControl==Lease(1).control && At<const void*>(j.descriptor,0x38)==Lease(1).object && At<LONG>(resources[1][0],0)==4);
 const auto emitted=journalEmits,copied=copyCalls;
 CHECK(!WithNativeProjectileSampleInPlace(j,Lease(1),templates[1],muzzle[1],s,&JournalCreate,nullptr) && journalEmits==emitted && copyCalls==copied);
 CHECK(!RestoreNativeProjectileJournal(j) && destroyCalls==dtors && j.heldMask==7);DisposeFixture(j);CHECK(Balanced());
 Setup();s=Sample();side=1;j={};copyFault=copyPartial=true;
 CHECK(!WithNativeProjectileSampleInPlace(j,Lease(1),templates[1],muzzle[1],s,&JournalCreate,nullptr));copyFault=copyPartial=false;
 CHECK(j.copyEntered && j.uncertain && !j.made && j.heldMask==7 && createCalls==0 && destroyCalls==0 && At<LONG>(actors[6].control,12)==21);
 CHECK(At<const void*>(j.descriptor,0x38)==actors[6].body && !RestoreNativeProjectileJournal(j));DisposeFixture(j,true);CHECK(Balanced());
 Setup();s=Sample();side=1;j={};destroyFault=true;
 outcome=WithNativeProjectileOutcomeInPlace(j,Lease(1),templates[1],muzzle[1],s,&JournalCreate,nullptr);destroyFault=false;
 CHECK(outcome.created==manager+32 && !outcome.cleanupComplete);
 CHECK(j.created==manager+32 && j.made && j.cleanupEntered && j.uncertain && j.heldMask==7 && destroyCalls==1);
 CHECK(At<const void*>(j.descriptor,8)==j.matrix && !std::memcmp(j.matrix,s.matrix,64) && (reinterpret_cast<std::uintptr_t>(j.matrix)&15u)==0);
 CHECK(!RestoreNativeProjectileJournal(j) && destroyCalls==1 && j.created==manager+32);
 CHECK(!WithNativeProjectileSampleInPlace(j,Lease(1),templates[1],muzzle[1],s,&JournalCreate,nullptr) && journalEmits==1 && destroyCalls==1);DisposeFixture(j);CHECK(Balanced());
 Setup();s=Sample();side=1;j={};auto* r=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));CHECK(r!=nullptr);if(!r)return;
 Put<LONG>(r,0,3);Put<LONG>(r,4,7);Put<void*>(r,8,payloads[1][0]);Put<void*>(templates[1],0x180,r);Put<void*>(templates[1],0x188,payloads[1][0]);protectAtCreate=true;
 outcome=WithNativeProjectileOutcomeInPlace(j,Lease(1),templates[1],muzzle[1],s,&JournalCreate,nullptr);protectAtCreate=false;
 CHECK(outcome.created==manager+32 && !outcome.cleanupComplete);
 CHECK(j.created==manager+32 && j.made && j.heldMask==7 && !j.cleanupEntered && !j.uncertain && destroyCalls==0);
 CHECK(!RestoreNativeProjectileJournal(j) && destroyCalls==0);const auto readonlyEmits=journalEmits;
 outcome=WithNativeProjectileOutcomeInPlace(j,Lease(1),templates[1],muzzle[1],s,&JournalCreate,nullptr);
 CHECK(!outcome.created && !outcome.cleanupComplete && journalEmits==readonlyEmits);
 DWORD old=0;CHECK(VirtualProtect(r,0x1000,PAGE_READWRITE,&old)!=0);Sleep(20);
 CHECK(RestoreNativeProjectileJournal(j) && NativeProjectileJournalEmpty(j) && destroyCalls==1 && Balanced());
 CHECK(At<LONG>(r,0)==3 && At<LONG>(r,4)==7 && RestoreNativeProjectileJournal(j) && destroyCalls==1);VirtualFree(r,0,MEM_RELEASE);
 Setup();s=Sample();side=1;j={};strongFault=dropRootDuringCreate=true;
 CHECK(!WithNativeProjectileSampleInPlace(j,Lease(1),templates[1],muzzle[1],s,&JournalCreate,nullptr));strongFault=dropRootDuringCreate=false;
 CHECK(j.created==manager+32 && !j.made && j.uncertain && j.cleanupEntered && j.heldMask==6 && strongDestroyed==1);
 CHECK(At<LONG>(actors[3].control,8)==0 && At<LONG>(actors[4].control,8)==2 && At<LONG>(actors[5].control,8)==2 && !RestoreNativeProjectileJournal(j));
 CHECK(j.heldMask==6 && strongDestroyed==1 && At<LONG>(actors[3].control,8)==0);DisposeFixture(j);
 Setup();s=Sample();side=1;CHECK(NativeProjectileJournalsEmpty(Lease(1)));createFault=true;
 CHECK(!WithNativeProjectileSample(Lease(1),templates[1],muzzle[1],s,&JournalCreate,nullptr));createFault=false;
 CHECK(!NativeProjectileJournalsEmpty(Lease(1)) && !RestoreNativeProjectileJournals(Lease(1)));const auto arenaEmits=journalEmits;
 CHECK(!WithNativeProjectileSample(Lease(1),templates[1],muzzle[1],s,&JournalCreate,nullptr) && journalEmits==arenaEmits);
 const auto beforeOther=journalEmits;CHECK(NativeProjectileJournalsEmpty(Lease(0)));
 CHECK(WithNativeProjectileSample(Lease(0),templates[0],muzzle[0],s,&JournalCreate,nullptr)==manager+32 && journalEmits==beforeOther+1);
 for(auto& pending:projectile_native::journals)if(!NativeProjectileJournalEmpty(pending))DisposeFixture(pending);
 CHECK(NativeProjectileJournalsEmpty(Lease(1)) && NativeProjectileJournalsEmpty(Lease(0)) && Balanced());
}

NativeProjectileSample Sample(){NativeProjectileSample s{};CHECK(CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&s));return s;}
}
namespace crew {
bool ReadNativeObjectId(const unsigned char* object,unsigned char* out) noexcept {
    for(const auto& a:actors)if(object==a.body){
        std::memcpy(out,a.id,32);
        if(object==actors[4].body && dropActorOnIdRead){dropActorOnIdRead=false;InterlockedDecrement(reinterpret_cast<volatile LONG*>(actors[4].control+8));}
        if(object==actors[4].body && dropInterfaceOnIdRead){InterlockedDecrement(reinterpret_cast<volatile LONG*>(dropInterfaceOnIdRead+8));dropInterfaceOnIdRead=nullptr;}
        return true;
    }return false;
}
bool ResolveMarkIdentities(const unsigned char*,const unsigned char* id,MarkIdentities* out) noexcept {
    *out={};if(resolveFails)return false;
    for(unsigned i=0;i<3;++i)if(!std::memcmp(id,actors[side*3+i].id,32)) {
        const auto& a=actors[side*3+(wrongResolver ? (i+1)%3 : i)];out->target={a.body,a.control};return true;
    }
    return true;
}
}
namespace crew::enemy_native {
void* ProjectileNativeTestFunction(unsigned rva) noexcept {
    switch(rva){case 0x32EF10:return reinterpret_cast<void*>(&Copy);case 0x100C90:return reinterpret_cast<void*>(&Destroy);
    case 0x58EB40:return reinterpret_cast<void*>(&Move);case 0x548EF0:return reinterpret_cast<void*>(&Lock);case 0x1194280:return reinterpret_cast<void*>(&Spawn);default:return nullptr;}
}
}
int main(){
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,kImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!image)return 2;
    controlTable[0]=reinterpret_cast<void*>(&StrongDestroyed);controlTable[1]=reinterpret_cast<void*>(&WeakDestroyed);
    for(const auto& p:projectile_native::prints)std::memcpy(image+p.rva,p.bytes,16);
    Setup();image[0x32EF10]^=1;NativeProjectileSample s{};
    CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&s));image[0x32EF10]^=1;
    s=Sample();CHECK(kProjectileWords==30 && kProjectileBytes==11 && s.resourceMask==3 && s.hasMuzzle);
    bool values=true;for(unsigned i=0;i<kProjectileWords;++i)values=values && s.scalar[i]==At<std::uint32_t>(actual,kProjectileScalar32[i]);
    for(unsigned i=0;i<kProjectileBytes;++i)values=values && s.bytes[i]==At<unsigned char>(actual,kProjectileScalar8[i]);
    CHECK(values && s.scalar178==0x5713 && s.random==0xF1234567 && s.shot==0x89ABCDEF && s.flags==0xA1B2C3D4);
    CHECK(!std::memcmp(s.target,actors[1].id,32) && s.lock==1 && !std::memcmp(s.extra,actors[2].id,32) && s.extraObject);
    CHECK(!std::memcmp(s.matrix,muzzle[0],64) && !std::memcmp(s.muzzleMatrix,muzzle[0],64) && Balanced());
    side=1;expectedSample=s;CHECK(CanReplayNativeProjectileSample(Lease(1),&factory,templates[1],muzzle[1],s));
    CHECK(WithNativeProjectileSample(Lease(1),templates[1],muzzle[1],s,&CaptureCreated,nullptr)==manager+32);
    CHECK(inspected && copyCalls==1 && destroyCalls==1 && moves==3 && Balanced());
    CHECK(!std::memcmp(muzzle[1],s.muzzleMatrix,64));
    CHECK(ReplayNativeProjectileSample(Lease(1),&factory,templates[1],muzzle[1],s)==manager+32 && spawnCalls==1 && Balanced());
    returnNull=true;CHECK(!WithNativeProjectileSample(Lease(1),templates[1],muzzle[1],s,&CaptureCreated,nullptr) && Balanced());returnNull=false;
    CHECK(copyCalls==destroyCalls && strongDestroyed==0 && weakDestroyed==0);
    const auto calls=createCalls;CHECK(!WithNativeProjectileSample(Lease(1),templates[1],muzzle[1],s,nullptr,nullptr) && createCalls==calls && Balanced());
    factoryTable[2]=nullptr;CHECK(!CanReplayNativeProjectileSample(Lease(1),&factory,templates[1],muzzle[1],s));factoryTable[2]=reinterpret_cast<void*>(&StrongDestroyed);
    CHECK(!CanPrepareNativeProjectileSample(Lease(1),templates[1],nullptr,s));
    auto bad=s;bad.resourceMask=4;CHECK(!ValidNativeProjectileSample(bad));bad=s;bad.resourceMask=1;
    CHECK(!CanPrepareNativeProjectileSample(Lease(1),templates[1],muzzle[1],bad));
    bad=s;bad.target[20]=1;CHECK(!ValidNativeProjectileSample(bad));bad=s;bad.target[31]^=0x80;
    CHECK(ValidNativeProjectileSample(bad) && !CanPrepareNativeProjectileSample(Lease(1),templates[1],muzzle[1],bad));
    wrongResolver=true;CHECK(!CanPrepareNativeProjectileSample(Lease(1),templates[1],muzzle[1],s));wrongResolver=false;
    resolveFails=true;CHECK(!CanPrepareNativeProjectileSample(Lease(1),templates[1],muzzle[1],s));resolveFails=false;
    bad=s;bad.lock=2;CHECK(!CanPrepareNativeProjectileSample(Lease(1),templates[1],muzzle[1],bad));
    Put<void*>(lockPoints[1][1],8,actors[3].body);CHECK(!CanPrepareNativeProjectileSample(Lease(1),templates[1],muzzle[1],s));Put<void*>(lockPoints[1][1],8,actors[4].body);
    Put<LONG>(lockControls[1][1],8,0);CHECK(!CanPrepareNativeProjectileSample(Lease(1),templates[1],muzzle[1],s));Put<LONG>(lockControls[1][1],8,1);
    Put<void*>(actors[4].body,0x30,actors[5].control);CHECK(!CanPrepareNativeProjectileSample(Lease(1),templates[1],muzzle[1],s));Put<void*>(actors[4].body,0x30,actors[4].control);
    bad=s;bad.matrix[0]=std::numeric_limits<float>::quiet_NaN();CHECK(!ValidNativeProjectileSample(bad));bad=s;bad.matrix[15]=0;CHECK(!ValidNativeProjectileSample(bad));
    bad=s;std::memcpy(bad.matrix+4,bad.matrix,16);CHECK(!ValidNativeProjectileSample(bad));
    const auto number=[](NativeProjectileSample& value,unsigned index,float scalar){std::memcpy(value.scalar+index,&scalar,sizeof(scalar));};
    bad=s;number(bad,6,std::numeric_limits<float>::quiet_NaN());CHECK(!ValidNativeProjectileSample(bad));
    bad=s;number(bad,6,std::numeric_limits<float>::infinity());CHECK(!ValidNativeProjectileSample(bad));
    bad=s;Identity(bad.matrix);bad.matrix[10]=(std::numeric_limits<float>::max)();number(bad,6,2);CHECK(!ValidNativeProjectileSample(bad));
    bad=s;Identity(bad.matrix);number(bad,6,(std::numeric_limits<float>::max)());number(bad,1,0);number(bad,2,0);number(bad,3,(std::numeric_limits<float>::max)());CHECK(!ValidNativeProjectileSample(bad));
    bad=s;number(bad,6,2);number(bad,1,std::numeric_limits<float>::quiet_NaN());CHECK(ValidNativeProjectileSample(bad)); // original optional-velocity sentinel
    number(bad,2,std::numeric_limits<float>::quiet_NaN());number(bad,3,std::numeric_limits<float>::quiet_NaN());CHECK(ValidNativeProjectileSample(bad));
    bad=s;number(bad,1,std::numeric_limits<float>::infinity());number(bad,2,0);number(bad,3,0);CHECK(!ValidNativeProjectileSample(bad));
    number(bad,2,-std::numeric_limits<float>::infinity());CHECK(ValidNativeProjectileSample(bad)); // native ordered sum is NaN: the optional vector is not read
    bad=s;Identity(bad.matrix);bad.matrix[10]=(std::numeric_limits<float>::max)();number(bad,6,0);CHECK(ValidNativeProjectileSample(bad)); // finite zero product; no arbitrary speed/scale cap
    bad=s;number(bad,6,-2);CHECK(ValidNativeProjectileSample(bad));
    bad=s;std::memset(bad.target,0,32);CHECK(!ValidNativeProjectileSample(bad));bad=s;std::memset(bad.extra,0,32);CHECK(!ValidNativeProjectileSample(bad));
    side=0;Setup();s=Sample();NativeProjectileSample untouched{};untouched.random=77;
    Put<void*>(actual,0x38,actors[3].body);CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched) && untouched.random==77);Put<void*>(actual,0x38,actors[0].body);
    CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],nullptr,actual,&untouched));CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,nullptr));
    Put<void*>(actual,0x88,actors[1].body);CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched));Put<void*>(actual,0x88,actors[2].body);
    Put<void*>(actual,0x188,payloads[0][1]);CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched));Put<void*>(actual,0x188,payloads[0][0]);
    Put<void*>(actual,0x120,payloads[0][0]);CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched));Put<void*>(actual,0x120,payloads[0][1]);
    Put<std::uint32_t>(actual,0x128,999);CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched));Put<std::uint32_t>(actual,0x128,100);
    Put<std::uint16_t>(actual,0xF8,3);CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched));Put<std::uint16_t>(actual,0xF8,0xFFFF);
    Put<std::uint16_t>(templates[0],0xF8,0);Put<std::uint16_t>(actual,0xF8,0);Put<std::uint64_t>(actual,0xE8,1);
    CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched));Put<std::uint64_t>(actual,0xE8,0);Put<std::uint64_t>(actual,0xF0,0xDEADBEEF);
    CHECK(CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched)); // type0 payload is only8 bytes
    Put<std::uint16_t>(templates[0],0xF8,2);Put<std::uint16_t>(actual,0xF8,2);
    CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched)); // type2 includes second qword
    Put<std::uint64_t>(actual,0xF0,0);CHECK(CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched));
    Put<std::uint16_t>(templates[0],0xF8,0xFFFF);Put<std::uint16_t>(actual,0xF8,0xFFFF);
    Put<std::uint16_t>(templates[0],0x1C0,1);Put<std::uint16_t>(actual,0x1C0,1);Put<std::uint64_t>(actual,0x1B0,1);
    CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched));Put<std::uint64_t>(actual,0x1B0,0);
    CHECK(CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched));
    Put<std::uint16_t>(templates[0],0x1C0,0xFFFF);Put<std::uint16_t>(actual,0x1C0,0xFFFF);
    Put<std::uint64_t>(actual,0x48,0xBAD0F00D);CHECK(CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched)); // uncopied padding
    Put<std::uint64_t>(actual,0x48,0);
    for(unsigned offset:{0x12Cu,0x13Cu,0x14Cu,0x15Cu,0x16Cu})Put<std::uint32_t>(actual,offset,0xCAFEBABE);
    CHECK(CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched)); // handle trailing padding is not its ID
    Setup();Put<Ref>(actual,0x60,{});Put<Ref>(actual,0x78,{});Put<void*>(actual,0x88,nullptr);Put<void*>(actual,0x70,nullptr);s=Sample();
    CHECK(!s.hasMuzzle && !s.extraObject && s.lock==0 && projectile_native::Zero(s.target) && projectile_native::Zero(s.extra));
    side=1;CHECK(CanPrepareNativeProjectileSample(Lease(1),templates[1],nullptr,s));expectedSample=s;
    CHECK(WithNativeProjectileSample(Lease(1),templates[1],nullptr,s,&CaptureCreated,nullptr)==manager+32 && Balanced());
    for(unsigned mask=0;mask<3;++mask){
        Setup();for(unsigned peer=0;peer<2;++peer)for(unsigned j=0;j<2;++j)if(!(mask&(1u<<j))){Put<void*>(templates[peer],0x180+j*16,nullptr);Put<void*>(templates[peer],0x188+j*16,nullptr);}
        for(unsigned j=0;j<2;++j)if(!(mask&(1u<<j))){Put<void*>(actual,0x180+j*16,nullptr);Put<void*>(actual,0x188+j*16,nullptr);}
        s=Sample();CHECK(s.resourceMask==mask);side=1;expectedSample=s;
        CHECK(WithNativeProjectileSample(Lease(1),templates[1],muzzle[1],s,&CaptureCreated,nullptr)==manager+32 && Balanced());
    }
    Setup();Put<LONG>(lockControls[0][1],8,0);Put<LONG>(actors[2].control,8,0);Put<void*>(actual,0x88,nullptr);s=Sample();
    CHECK(projectile_native::Zero(s.target) && projectile_native::Zero(s.extra) && !s.extraObject);
    Put<void*>(actual,0x88,actors[2].body);CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched));
    Setup();Put<Ref>(actual,0x60,{reinterpret_cast<void*>(1),lockControls[0][1]});CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched));
    Setup();Put<LONG>(actors[0].control,8,0);CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched));
    Setup();Put<LONG>(resources[0][0],0,0);CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched));
    Setup();Put<LONG>(actors[2].control,12,0x7FFFFFFF);CHECK(!CaptureNativeProjectileSample(Lease(0),templates[0],muzzle[0],actual,&untouched));
    RegistryChecks();JournalChecks();
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("enemy control projectile native: %d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
// Object references over the fixture's NetIds (net_object_resolve.h).
#include "net_object_ref_stub.h"
