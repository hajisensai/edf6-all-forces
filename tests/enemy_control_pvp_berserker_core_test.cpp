// Real generic PVP ownership + real Berserker containers and birth/publication guards.
// Only native-address ABI calls and the backing game image are stand-ins; no controlled_pvp API is stubbed.
#define EDF6_ENEMY_BERSERKER_TEST
#include "../src/pvp_controlled.cpp"
#include "../src/enemy_control_pvp_berserker.cpp"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
namespace {
using namespace crew;using namespace berserker_pvp_detail;
int rocketCtors=0,bombCtors=0,sceneCreates=0,controlDeletes=0,lastCtorBody=-9,ownerTeam=2;bool rejectBombApply=false;
enum class Acceptance {none,guestDeferred,hostAccepted,acceptedReplay};Acceptance acceptance=Acceptance::none;
unsigned centralCalls=0,ctorBridges=0,fallbackBridges=0,deferredCalls=0;
unsigned rejectCtorAt=0,rejectCtorAfterAt=0,rejectFallbackAt=0,emptyFallbackReturns=0;bool retireFirstHolder=false;
void __fastcall DeleteControl(void*) noexcept {++controlDeletes;}
void __fastcall ControlNoop(const void*) noexcept {}
struct NoticeEvent {const void* object=nullptr;bool bullet=false;};
int bulletPublished=0,deleteBulletNoticeAt=0;bool traceNotices=false;
std::vector<NoticeEvent> noticeEvents;
int checks=0,failures=0,nativeShots=0,prepared=0,adopted=0,finished=0,faulted=0,published=0,activated=0,deleted=0,configured=0,defaults=0,callerReleased=0,bindingsCalled=0;unsigned freeEmitters=512,lastReserveEmitters=0;
bool leased=false,temporary=false,prepareOk=true,adoptOk=true,useDefault=false,throwDelete=false;
ObjRef controlled{},owned[256]{};unsigned ownedCount=0,remaining=0;int baselineTeam=3,baselineLast=4,capturedBody=-9;
const void* selectorRoot=nullptr;const void* selectorFunction=nullptr;controlled_pvp::SpawnProof spawnProof{};
void Check(bool v,const char* s,int n){++checks;if(!v){++failures;std::fprintf(stderr,"line %d: %s\n",n,s);}}
#define CHECK(v) Check((v),#v,__LINE__)
struct Object {
 unsigned char* p=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 unsigned char* c=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 explicit Object(unsigned type=0){if(!p || !c)std::exit(2);static void* controls[2]={reinterpret_cast<void*>(&ControlNoop),reinterpret_cast<void*>(&DeleteControl)};Put<void*>(c,0,controls);Put<const void*>(p,0,type<9 ? image+kTypes[type].table : image+0x1700000);Put<void*>(p,0x28,p);Put<void*>(p,0x30,c);Put<LONG>(c,8,1);Put<LONG>(c,12,1);Put<int>(p,0x314,3);Put<int>(p,0x318,4);Put<unsigned>(p,0x380,0x40);auto* head=p+0x3000;Put<void*>(head,0,head);Put<void*>(head,8,head);Put<void*>(p,0x550,head);Put<void*>(p,0x48,p+0x3200);Put<std::size_t>(p,0x50,32);}
 ObjRef Ref()const{return {p,c};}
 ~Object(){VirtualFree(p,0,MEM_RELEASE);VirtualFree(c,0,MEM_RELEASE);}
};
std::vector<std::unique_ptr<Object>> pool;
Object* nextPart=nullptr;Object* lastBullet=nullptr;
Object& Fresh(unsigned type){pool.emplace_back(std::make_unique<Object>(type));return *pool.back();}
void __fastcall SetTeam(void* p,int t,bool reg){if(rejectBombApply && t==2 && At<const void*>(p,0)==image+kTypes[7].table)return;Put<int>(p,kTeam,t);if(t!=-1)Put<int>(p,0x318,t);auto flags=At<unsigned>(p,0x380);Put<unsigned>(p,0x380,reg ? flags|0x40u : flags&~0x40u);}
void __fastcall Delete(void* p){++deleted;Put<unsigned char>(p,0x18,4);if(throwDelete){throwDelete=false;RaiseException(0xE0424242,0,0,nullptr);}}
void __fastcall NativePublish(void*,const ObjRef*){++published;}
void __fastcall NativeActivate(void*,void* object){++activated;if(traceNotices)noticeEvents.push_back({object,false});}
void Link(Object& root,Object& child){const auto n=At<std::size_t>(root.p,0x58);CHECK(n<32);Put<void*>(root.p+0x3200,n*8,child.p);Put<std::size_t>(root.p,0x58,n+1);Put<void*>(child.p,0x38,root.p);}
void Actions(Object& root,unsigned n){Put<void*>(root.p,0x17F0,root.p+0x2000);Put<std::size_t>(root.p,0x17F8,1);Put<std::size_t>(root.p,0x1800,1);Put<void*>(root.p+0x2000,8,root.p+0x3300);Put<std::size_t>(root.p+0x2000,0x10,n);Put<std::size_t>(root.p+0x2000,0x18,n);}
void Sources(Object& bullet,const ObjRef& owner,int filter=3,int damage=4){Put<const void*>(bullet.p,0xAE8,owner.obj);Put<const void*>(bullet.p,0xAF0,owner.ctrl);Put<const void*>(bullet.p,0x880,owner.obj);Put<const void*>(bullet.p,0x888,owner.ctrl);Put<int>(bullet.p,0xB40,filter);Put<int>(bullet.p,0x894,damage);}
void NotifyBullet(Object&);
unsigned char* __fastcall NativeFactory(void*,const float*,const wchar_t*,void* init){++configured;auto& b=Fresh(9);Sources(b,{At<const void*>(init,0x38),At<const void*>(init,0x40)},At<int>(init,0x90),At<int>(init,0x90));lastBullet=&b;NotifyBullet(b);return b.p;}
ObjRef* __fastcall NativeDefault(void*,ObjRef* out,const float*,void* init){++defaults;auto& b=Fresh(9);Sources(b,{At<const void*>(init,0x38),At<const void*>(init,0x40)},At<int>(init,0x90),At<int>(init,0x90));lastBullet=&b;*out=b.Ref();NotifyBullet(b);return out;}
unsigned char* __fastcall NativeRocket(void* memory,void* rawInit){++rocketCtors;
 Object* parent=nullptr;const auto rawParent=At<const void*>(rawInit,0x30);for(const auto& object:pool)if(object->p==rawParent)parent=object.get();
 auto* child=nextPart;CHECK(parent && child && child->p==memory);if(!parent)return nullptr;Link(*parent,*child);
 RocketTeam(memory,At<int>(parent->p,0x318),true);
 alignas(16) unsigned char init[0x1D0]{};Put<const void*>(init,0x38,parent->p);Put<const void*>(init,0x40,parent->c);Put<int>(init,0x90,At<int>(parent->p,0x314));
 lastBullet=nullptr;
 if(useDefault){ObjRef out{reinterpret_cast<void*>(1),reinterpret_cast<void*>(2)};CHECK(DefaultFactory(nullptr,&out,nullptr,init)==&out);if(out){Put<const void*>(memory,0x5D0,out.obj);Put<const void*>(memory,0x5D8,out.ctrl);}else {++emptyFallbackReturns;CHECK(!out.obj && !out.ctrl);}}
 else {auto* b=Factory(nullptr,nullptr,const_cast<wchar_t*>(L"native-bullet"),init);if(b){Put<const void*>(memory,0x5D0,b);Put<const void*>(memory,0x5D8,At<const void*>(b,0x30));}}
 const auto damage=reinterpret_cast<std::uintptr_t>(At<const void*>(memory,0x5D0));
 BindDamage(reinterpret_cast<void*>(damage+0xDA0),nullptr);BindFinish(reinterpret_cast<void*>(damage+0xDE0),nullptr);
 return static_cast<unsigned char*>(memory);
}
unsigned char* __fastcall NativeBomb(void* memory,void* init){++bombCtors;
 const ObjRef parent{At<const void*>(init,0x30),At<const void*>(init,0x38)};Object* owner=nullptr;for(const auto& o:pool)if(o->p==parent.obj)owner=o.get();
 CHECK(owner && nextPart && nextPart->p==memory);if(owner)Link(*owner,*nextPart);Put<const void*>(memory,0xFC0,parent.obj);Put<const void*>(memory,0xFC8,parent.ctrl);SetTeam(memory,1,true);lastCtorBody=At<int>(memory,0x314);return static_cast<unsigned char*>(memory);
}
void* callbackThis=nullptr;const void* callbackArgument=nullptr;const void* callbackMetadata=nullptr;const void* callbackObject=nullptr;unsigned callbackStage=0;unsigned char* publicationThunk=nullptr;
void __fastcall SceneCallback(void* self,const void* argument,const void* metadata){++published;callbackThis=self;callbackArgument=argument;callbackMetadata=metadata;callbackObject=At<const void*>(argument,0);callbackStage=At<unsigned>(metadata,0);}
void __fastcall BulletObserver(void* self,const void* argument,const void* metadata) {
 ++bulletPublished;CHECK(self!=nullptr);CHECK(At<unsigned>(metadata,0)==37);
 const void* bullet=At<const void*>(argument,0);CHECK(bullet!=nullptr);
 CHECK(At<LONG>(At<const void*>(bullet,0x30),8)==2); // deferred notification owns one native strong reference
 if(traceNotices)noticeEvents.push_back({bullet,true});
 if(deleteBulletNoticeAt>0 && --deleteBulletNoticeAt==0)Delete(const_cast<void*>(bullet));
}
void NotifyBullet(Object& bullet) {
 static void* table[3]={nullptr,nullptr,reinterpret_cast<void*>(&BulletObserver)};static void* callback=table;
 const void* argument=bullet.p;unsigned metadata=37;const int before=bulletPublished;
 if(useDefault){alignas(16) unsigned char manager[0xCC0]{};Put<void*>(manager,0xCA8,&callback);Put<unsigned>(manager,0xCB0,metadata);DamageActivate(manager,bullet.p);}
 else BulletPublication(&callback,&argument,&metadata,reinterpret_cast<void*>(&BulletObserver));
 if(active){CHECK(bulletPublished==before);CHECK(At<LONG>(bullet.c,8)==2);}
}
unsigned char* __fastcall NativeCreateScene(void*,const float*,const wchar_t*,void* init){++sceneCreates;
 nextPart=&Fresh(7);auto* child=BombCtor(nextPart->p,init);unsigned stage=0;const void* arg=child;ScenePublication(reinterpret_cast<void*>(0x123),&arg,&stage,reinterpret_cast<void*>(&SceneCallback));
 if(At<unsigned char>(child,0x18)&4u){++callerReleased;Put<LONG>(nextPart->c,8,0);if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(nextPart->c+12))==0)DeleteControl(nextPart->c);return nullptr;}return child;
}
void __fastcall NativeShoot(void* raw,int index){++nativeShots;auto* root=static_cast<Object*>(nullptr);for(const auto& obj:pool)if(obj->p==raw)root=obj.get();CHECK(root!=nullptr);if(!root)return;
 const int notices=bulletPublished,activations=activated;
 const auto* table=At<const unsigned char*>(raw,0x17F0);const auto n=At<std::size_t>(table+static_cast<std::size_t>(index)*0x100,0x18);
 for(std::size_t i=0;i<n;++i){nextPart=&Fresh(6);alignas(16) unsigned char descriptor[0x148]{};Put<const void*>(descriptor,0x30,root->p);Put<const void*>(descriptor,0x38,root->c);auto* p=RocketCtor(nextPart->p,descriptor);CHECK(p!=nullptr && p==nextPart->p);ObjRef strong=nextPart->Ref();Publish(nullptr,&strong);Activate(nullptr,p);if(active){CHECK(bulletPublished==notices && activated==activations);}if(At<unsigned char>(p,0x18)&4u){++callerReleased;Put<LONG>(nextPart->c,8,0);if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(nextPart->c+12))==0)DeleteControl(nextPart->c);}if(retireFirstHolder && i==0 && n>1 && At<LONG>(nextPart->c,8)>0){Put<unsigned char>(p,0x18,4);Put<LONG>(nextPart->c,8,0);CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(nextPart->c+12))>0);}}
}
void Image(){image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!image)std::exit(2);
 for(const auto& a:kAnchors)std::memcpy(image+a.at,a.bytes,a.size);
 std::memcpy(image+kPublication,kPublicationBytes,9);
 std::memcpy(image+kBulletPublication,kBulletPublicationBytes,13);
 for(const auto& h:sites){image[h.at]=0xE8;Put<std::int32_t>(image+h.at,1,static_cast<std::int32_t>(h.target-h.at-5));}
 unsigned i=0;for(const auto& t:kTypes){const unsigned col=0x2000000+i*0x400,td=col+0x100;Put<void*>(image+t.table-8,0,image+col);Put<unsigned>(image+col,0,1);Put<unsigned>(image+col,12,td);Put<unsigned>(image+col,20,col);auto* name=image+td+16;std::memcpy(name,".?AV",4);const auto n=std::strlen(t.name);std::memcpy(name+4,t.name,n);std::memcpy(name+4+n,"@@",3);for(unsigned j=0;j<t.slots;++j)Put<void*>(image+t.table,j*8,image+0x1000+j*16);++i;}
 const unsigned col=0x2050000,chd=col+0x100,list=col+0x120,bcd=col+0x140;Put<void*>(image+0x1700000-8,0,image+col);Put<unsigned>(image+col,0,1);Put<unsigned>(image+col,16,chd);Put<unsigned>(image+col,20,col);Put<unsigned>(image+chd,8,1);Put<unsigned>(image+chd,12,list);Put<unsigned>(image+list,0,bcd);Put<unsigned>(image+bcd,0,0x2022E80);Put<int>(image+bcd,12,-1);
}
void __fastcall NativeBind(void* target,const void*){++bindingsCalled;const void* observed=*reinterpret_cast<const void* const volatile*>(static_cast<const unsigned char*>(target)+0x38);(void)observed;}
bool Collect(Object& r,ObjRef* m,Emitter* e,unsigned* n,unsigned* k,unsigned mc=controlled_pvp::kMaxMembers,unsigned ec=controlled_pvp::kMaxEmitters){return EnemyControlBerserkerPvpCollect(r.Ref(),m,mc,n,e,ec,k);}
using controlled_pvp::Lease;
bool CoreCollect(void*,const ObjRef& r,ObjRef* m,unsigned mc,unsigned* n,controlled_pvp::Emitter* e,unsigned ec,unsigned* k) noexcept {return EnemyControlBerserkerPvpCollect(r,m,mc,n,e,ec,k);}
controlled_pvp::Life CoreRead(void*,const controlled_pvp::Emitter& e,int* team) noexcept {return EnemyControlBerserkerPvpRead(e,team);}
bool CoreWrite(void*,const controlled_pvp::Emitter& e,int team) noexcept {return EnemyControlBerserkerPvpWrite(e,team);}
bool CoreMemberBaseline(void*,const ObjRef& r,const ObjRef& m,int t,int l,int* a,int* b) noexcept {return EnemyControlBerserkerPvpBaseline(r,m,t,l,a,b);}
bool CoreEmitterBaseline(void*,const controlled_pvp::Emitter& e,int t,int l,int* a) noexcept {return EnemyControlBerserkerPvpBaseline(e,t,l,a);}
controlled_pvp::Hooks Hooks() noexcept {return {nullptr,&CoreCollect,&CoreRead,&CoreWrite,&CoreMemberBaseline,&CoreEmitterBaseline};}
// Stand-in for the accepted walking-action caller: guests defer BEFORE entering the real Berserker transaction.
bool CallAcceptedParts(Object& root,int index,Acceptance phase) {
 if(phase==Acceptance::guestDeferred){++deferredCalls;return true;}
 const auto old=acceptance;acceptance=phase;
 const bool result=EnemyControlBerserkerPvpExecuteParts(root.p,index,&NativeShoot);acceptance=old;return result;
}
void EndCore(Lease& lease) {
 CHECK(controlled_pvp::Restore(&lease));CHECK(!lease.id);CHECK(EnemyControlBerserkerPvpReset());
 pool.clear();nextPart=lastBullet=nullptr;ownerTeam=2;rejectBombApply=false;useDefault=false;rejectCtorAt=rejectCtorAfterAt=rejectFallbackAt=0;retireFirstHolder=false;deleteBulletNoticeAt=0;traceNotices=false;noticeEvents.clear();
}
void NearEmitterCapacityRefusesWholeRocketWave() {
 auto& root=Fresh(1);Actions(root,2);
 alignas(16) unsigned char shared[0x700]{};auto* battery=shared+16;
 static void* controls[2]={reinterpret_cast<void*>(&ControlNoop),reinterpret_cast<void*>(&DeleteControl)};
 Put<void*>(shared,0,controls);Put<LONG>(shared,8,1);Put<LONG>(shared,12,1);
 std::vector<unsigned char> muzzles(254*0xD0);
 Put<void*>(battery,0x2B0,root.p);Put<void*>(battery,0x2B8,root.c);Put<void*>(battery,0x278,muzzles.data());
 Put<std::size_t>(battery,0x280,254);Put<std::size_t>(battery,0x288,254);
 Put<void*>(root.p,0x1A60,battery);Put<void*>(root.p,0x1A68,shared);
 for(unsigned i=0;i<254;++i){auto& bullet=Fresh(9);Sources(bullet,root.Ref(),3,4);Put<int>(bullet.p,0xC30,1);Put<void*>(muzzles.data()+i*0xD0,0xB0,bullet.p);Put<void*>(muzzles.data()+i*0xD0,0xB8,bullet.c);}
 Lease lease{};CHECK(controlled_pvp::Acquire(root.Ref(),"owner1",Hooks(),&lease));controlled_pvp::Status status{};
 CHECK(controlled_pvp::ReadStatus(lease,&status));CHECK(status.members==1 && status.emitters==511);
 const int shots=nativeShots,ctors=rocketCtors,factories=configured,publications=published;
 unsigned actionCount=0;CHECK(EnemyControlBerserkerPvpReady() && Room()>=2);
 CHECK(ActionCount(root.Ref(),0,&actionCount) && actionCount==2);
 CHECK(Proof().maxEmitters==2 && !controlled_pvp::CanPrepareSpawns(root.Ref(),Proof(),2));
 CHECK(!EnemyControlBerserkerPvpPreflight(root.Ref()));CHECK(!CallAcceptedParts(root,0,Acceptance::hostAccepted));
 CHECK(nativeShots==shots && rocketCtors==ctors && configured==factories && published==publications);
 CHECK(controlled_pvp::OwnsMember(root.Ref()));EndCore(lease);CHECK(At<LONG>(shared,12)==1);
}
void RocketGroupsCachesAndSealedOwnerDeath() {
 auto& root=Fresh(4);Actions(root,2);Lease lease{};CHECK(controlled_pvp::Acquire(root.Ref(),"owner1",Hooks(),&lease));
 const int before=published,beforeDeleted=deleted;CHECK(CallAcceptedParts(root,0,Acceptance::hostAccepted));CHECK(published==before+2 && activated>=2);
 std::vector<Object*> rockets,bullets;for(const auto& object:pool) {
  if(At<const void*>(object->p,0)==image+kTypes[6].table)rockets.push_back(object.get());
  if(At<const void*>(object->p,0)==image+0x1700000)bullets.push_back(object.get());
 }
 CHECK(rockets.size()==2 && bullets.size()==2);CHECK(controlled_pvp::Frame(lease));controlled_pvp::Status status{};
 CHECK(controlled_pvp::ReadStatus(lease,&status));CHECK(status.members==3 && status.emitters==6);
 for(auto* rocket:rockets){int t=-1,l=-1;CHECK(controlled_pvp::ReadBaselineForMember(rocket->Ref(),&t,&l));CHECK(t==4 && l==4 && At<int>(rocket->p,0x314)==2);}
 for(auto* bullet:bullets){CHECK(At<int>(bullet->p,0xB40)==2 && At<int>(bullet->p,0x894)==2);CHECK(At<int>(bullet->p,0x314)==3 && At<int>(bullet->p,0x318)==4);CHECK(At<const void*>(bullet->p,0xAE8)==root.p && At<const void*>(bullet->p,0xAF0)==root.c && At<const void*>(bullet->p,0x880)==root.p && At<const void*>(bullet->p,0x888)==root.c);}
 Put<void*>(rockets[0]->p,0x5D0,nullptr);Put<void*>(rockets[0]->p,0x5D8,nullptr);
 CHECK(controlled_pvp::Frame(lease));CHECK(At<int>(bullets[0]->p,0xB40)==3 && At<int>(bullets[0]->p,0x894)==3);
 Put<LONG>(root.c,8,0);CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(root.c+12))>0);
 CHECK(controlled_pvp::Restore(&lease));CHECK(!lease.id);
 CHECK(At<int>(bullets[1]->p,0xB40)==3 && At<int>(bullets[1]->p,0x894)==3);
 for(auto* rocket:rockets)CHECK(At<int>(rocket->p,0x314)==4 && At<int>(rocket->p,0x318)==4);
 for(auto* bullet:bullets)CHECK(At<const void*>(bullet->p,0xAE8)==root.p && At<const void*>(bullet->p,0xAF0)==root.c && At<const void*>(bullet->p,0x880)==root.p && At<const void*>(bullet->p,0x888)==root.c);
 CHECK(deleted==beforeDeleted);CHECK(EnemyControlBerserkerPvpReset());for(auto* bullet:bullets)CHECK(At<LONG>(bullet->c,12)==1);for(auto* rocket:rockets)CHECK(At<LONG>(rocket->c,12)==1);
 CHECK(At<LONG>(root.c,12)==0);pool.clear();nextPart=lastBullet=nullptr;
}
void DeferredCallerAndAcceptedFallbackReplay() {
 auto& root=Fresh(2);Actions(root,1);Lease lease{};CHECK(controlled_pvp::Acquire(root.Ref(),"owner1",Hooks(),&lease));
 const int shots=nativeShots,ctors=rocketCtors,publications=published;const auto bridges=ctorBridges;
 CHECK(CallAcceptedParts(root,0,Acceptance::guestDeferred));controlled_pvp::Status status{};
 CHECK(controlled_pvp::ReadStatus(lease,&status));CHECK(!status.restoring && status.followsOwnerTeam);
 CHECK(nativeShots==shots && rocketCtors==ctors && published==publications && ctorBridges==bridges);
 useDefault=true;const auto fallbacks=fallbackBridges;CHECK(CallAcceptedParts(root,0,Acceptance::hostAccepted));
 CHECK(fallbackBridges==fallbacks+1 && nativeShots==shots+1 && rocketCtors==ctors+1);CHECK(controlled_pvp::Frame(lease));EndCore(lease);
 auto& replay=Fresh(5);Actions(replay,1);CHECK(controlled_pvp::Acquire(replay.Ref(),"owner1",Hooks(),&lease));
 const auto configuredBefore=centralCalls;CHECK(CallAcceptedParts(replay,0,Acceptance::acceptedReplay));
 CHECK(centralCalls==configuredBefore+1 && acceptance==Acceptance::none);CHECK(controlled_pvp::Frame(lease));EndCore(lease);
}
void RejectedBridgeTailsAndWholeGroupRollback() {
 for(unsigned kind=0;kind<4;++kind)for(unsigned rejectedOrdinal=1;rejectedOrdinal<=2;++rejectedOrdinal) {
  if(kind==3 && rejectedOrdinal!=2)continue;
  auto& root=Fresh(4);Actions(root,rejectedOrdinal);Lease lease{};CHECK(controlled_pvp::Acquire(root.Ref(),"owner1",Hooks(),&lease));
  useDefault=kind==2;retireFirstHolder=kind==3;const auto empty=emptyFallbackReturns;
  if(kind==0 || kind==3)rejectCtorAt=ctorBridges+rejectedOrdinal;
  else if(kind==1)rejectCtorAfterAt=ctorBridges+rejectedOrdinal;
  else rejectFallbackAt=fallbackBridges+rejectedOrdinal;
  const int ctors=rocketCtors,registrations=published,callbacks=activated,factories=configured,fallbacks=defaults,binds=bindingsCalled,notices=bulletPublished;
  CHECK(!CallAcceptedParts(root,0,Acceptance::hostAccepted));
  CHECK(rocketCtors==ctors+static_cast<int>(rejectedOrdinal)); // one real ctor even if the bridge never entered it, never a second ctor after returned-null
  CHECK(published==registrations+static_cast<int>(rejectedOrdinal)-1 && activated==callbacks && bulletPublished==notices);
  unsigned bodies=0,bullets=0;
  for(const auto& object:pool) {
   if(At<const void*>(object->p,0)==image+kTypes[6].table){++bodies;CHECK((At<unsigned char>(object->p,0x18)&4u)!=0);}
   if(At<const void*>(object->p,0)==image+0x1700000){++bullets;CHECK((At<unsigned char>(object->p,0x18)&4u)!=0);CHECK(At<const void*>(object->p,0xAE8)==root.p && At<const void*>(object->p,0xAF0)==root.c);}
  }
  const unsigned createdBullets=rejectedOrdinal-(kind==1 ? 0u : 1u);
  CHECK(bodies==rejectedOrdinal && bullets==createdBullets);
  CHECK(configured+defaults==factories+fallbacks+static_cast<int>(createdBullets));
  CHECK(bindingsCalled==binds+static_cast<int>(createdBullets)*2); // rejected empty tail never calls [0+DA0/DE0+38]
  if(kind==2)CHECK(emptyFallbackReturns==empty+1);
  controlled_pvp::Status status{};CHECK(controlled_pvp::ReadStatus(lease,&status));CHECK(status.restoring);
  CHECK(At<int>(root.p,0x314)==3 && At<int>(root.p,0x318)==4);EndCore(lease);
 }
}
struct Validation {int beforeActivate=0;unsigned called=0;bool accept=false;};
bool ValidateWholeGroup(void* context) noexcept {auto& v=*static_cast<Validation*>(context);++v.called;CHECK(activated==v.beforeActivate);return v.accept;}
void ValidatedModeOffOrdinaryGroupRollsBack() {
 auto& root=Fresh(2);Actions(root,1);const int callbacks=activated,registrations=published;Validation validation{activated,0,false};
 acceptance=Acceptance::hostAccepted;
 CHECK(!EnemyControlBerserkerPvpExecutePartsValidated(root.p,0,&NativeShoot,&ValidateWholeGroup,&validation));acceptance=Acceptance::none;
 CHECK(validation.called==1 && activated==callbacks && published==registrations+1 && !controlled_pvp::HasLeases());
 for(const auto& object:pool)if(object->p!=root.p)CHECK((At<unsigned char>(object->p,0x18)&4u)!=0);
 CHECK(At<int>(root.p,0x314)==3 && At<int>(root.p,0x318)==4);CHECK(EnemyControlBerserkerPvpReset());pool.clear();nextPart=lastBullet=nullptr;
}
struct NoticeValidation {int notices=0,activations=0;unsigned called=0;bool rejectAfter=false;};
bool ValidateNoticeWave(void* context) noexcept {
 auto& v=*static_cast<NoticeValidation*>(context);++v.called;
 if(v.called==1){CHECK(bulletPublished==v.notices && activated==v.activations);CHECK(noticeEvents.empty());}
 else {CHECK(v.called==2);CHECK(bulletPublished==v.notices+2 && activated==v.activations+2);CHECK(noticeEvents.size()==4);}
 return !(v.rejectAfter && v.called==2);
}
void BulletNoticeTransactionsUseRealCore() {
 for(bool fallback:{false,true})for(int side:{0,2}) {
  auto& root=Fresh(4);Actions(root,2);ownerTeam=side;Lease lease{};
  CHECK(controlled_pvp::Acquire(root.Ref(),"owner1",Hooks(),&lease));useDefault=fallback;traceNotices=true;noticeEvents.clear();
  NoticeValidation validation{bulletPublished,activated,0,false};const int deletes=deleted;
  acceptance=Acceptance::hostAccepted;
  CHECK(EnemyControlBerserkerPvpExecutePartsValidated(root.p,0,&NativeShoot,&ValidateNoticeWave,&validation));acceptance=Acceptance::none;
  CHECK(validation.called==2 && bulletPublished==validation.notices+2 && activated==validation.activations+2);
  CHECK(noticeEvents.size()==4);
  if(noticeEvents.size()==4)for(unsigned i=0;i<4;i+=2) {
   CHECK(noticeEvents[i].bullet && !noticeEvents[i+1].bullet);
   CHECK(At<const void*>(noticeEvents[i+1].object,0x5D0)==noticeEvents[i].object);
   CHECK(At<LONG>(At<const void*>(noticeEvents[i].object,0x30),8)==1);
   CHECK(At<int>(noticeEvents[i].object,0xB40)==(side==2 ? 2 : 3));
  }
  CHECK(deleted==deletes);CHECK(controlled_pvp::Frame(lease));CHECK(controlled_pvp::Restore(&lease));
  for(const auto& object:pool)if(At<const void*>(object->p,0)==image+0x1700000) {
   CHECK(At<int>(object->p,0xB40)==3 && At<int>(object->p,0x894)==3);
   CHECK(At<const void*>(object->p,0xAE8)==root.p && At<const void*>(object->p,0xAF0)==root.c);
  }
  CHECK(deleted==deletes);EndCore(lease);
 }
 for(bool fallback:{false,true})for(bool observerReject:{false,true}) {
  auto& root=Fresh(4);Actions(root,2);Lease lease{};CHECK(controlled_pvp::Acquire(root.Ref(),"owner1",Hooks(),&lease));
  useDefault=fallback;traceNotices=true;noticeEvents.clear();deleteBulletNoticeAt=observerReject ? 1 : 0;
  NoticeValidation validation{bulletPublished,activated,0,!observerReject};
  acceptance=Acceptance::hostAccepted;
  CHECK(!EnemyControlBerserkerPvpExecutePartsValidated(root.p,0,&NativeShoot,&ValidateNoticeWave,&validation));acceptance=Acceptance::none;
  CHECK(validation.called==(observerReject ? 1u : 2u));
  CHECK(bulletPublished==validation.notices+(observerReject ? 1 : 2));
  CHECK(activated==validation.activations+(observerReject ? 0 : 2));
  for(const auto& object:pool)if(object->p!=root.p) {
   CHECK((At<unsigned char>(object->p,0x18)&4u)!=0);
   if(At<const void*>(object->p,0)==image+0x1700000)CHECK(At<LONG>(object->c,8)==1);
  }
  controlled_pvp::Status status{};CHECK(controlled_pvp::ReadStatus(lease,&status) && status.restoring);
  CHECK(At<int>(root.p,0x314)==3 && At<int>(root.p,0x318)==4);EndCore(lease);
 }
}
void BommerOriginalOneAndPublicationGuards() {
 auto& root=Fresh(3);Lease lease{};CHECK(controlled_pvp::Acquire(root.Ref(),"owner1",Hooks(),&lease));
 alignas(16) unsigned char init[0x100]{};Put<const void*>(init,0x30,root.p);Put<const void*>(init,0x38,root.c);
 const int p=published,d=deleted,ctors=bombCtors;auto* child=CreateBomb(nullptr,nullptr,L"native-bomb",init);
 CHECK(child && lastCtorBody==1 && bombCtors==ctors+1 && published==p+1);CHECK(At<int>(child,0x314)==2);
 int t=-1,l=-1;CHECK(controlled_pvp::ReadBaselineForMember(ObjRef::Of(child),&t,&l));CHECK(t==1 && l==1);
 CHECK(controlled_pvp::Restore(&lease));CHECK(At<int>(child,0x314)==1 && At<int>(child,0x318)==1);
 CHECK(At<int>(root.p,0x314)==3 && At<int>(root.p,0x318)==4 && deleted==d);
 CHECK(At<const void*>(child,0xFC0)==root.p && At<const void*>(child,0xFC8)==root.c);CHECK(EnemyControlBerserkerPvpReset());CHECK(deleted==d);pool.clear();
 auto& pending=Fresh(3);CHECK(controlled_pvp::Acquire(pending.Ref(),"owner1",Hooks(),&lease));Put<const void*>(init,0x30,pending.p);Put<const void*>(init,0x38,pending.c);
 ownerTeam=-1;const int pendingCtor=bombCtors,pendingCreate=sceneCreates,pendingPublish=published,pendingDelete=deleted;
 CHECK(CreateBomb(nullptr,nullptr,L"native-bomb",init)==nullptr);CHECK(bombCtors==pendingCtor && sceneCreates==pendingCreate && published==pendingPublish && deleted==pendingDelete);
 ownerTeam=2;EndCore(lease);
 auto& rejected=Fresh(3);CHECK(controlled_pvp::Acquire(rejected.Ref(),"owner1",Hooks(),&lease));Put<const void*>(init,0x30,rejected.p);Put<const void*>(init,0x38,rejected.c);
 rejectBombApply=true;const int rejectedPub=published,rejectedDelete=deleted,rejectedCtor=bombCtors;
 CHECK(CreateBomb(nullptr,nullptr,L"native-bomb",init)==nullptr);CHECK(bombCtors==rejectedCtor+1 && published==rejectedPub && deleted==rejectedDelete+1 && !pendingDenied);
 CHECK(controlled_pvp::HasLeases());rejectBombApply=false;CHECK(controlled_pvp::Restore(&lease));CHECK(!lease.id);
 Object* failed=nullptr;for(const auto& object:pool)if(At<const void*>(object->p,0)==image+kTypes[7].table)failed=object.get();CHECK(failed);
 if(failed)CHECK(At<LONG>(failed->c,12)==1);CHECK(EnemyControlBerserkerPvpReset());if(failed)CHECK(At<LONG>(failed->c,12)==0);pool.clear();
}
}
namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
int PvpSupportTeam(const char*) noexcept {return ownerTeam;}
bool EnemyControlOwnsTargetSelector(const void* root,const void* function) noexcept{return root==selectorRoot && function==selectorFunction;}
void* EnemyBerserkerTestFunction(unsigned at) noexcept {
 if(at==kSetTeam)return reinterpret_cast<void*>(&SetTeam);if(at==kDelete)return reinterpret_cast<void*>(&Delete);if(at==kShoot)return reinterpret_cast<void*>(&NativeShoot);
 if(at==kRocketCtor)return reinterpret_cast<void*>(&NativeRocket);if(at==kBombCtor)return reinterpret_cast<void*>(&NativeBomb);if(at==0x11945E0)return reinterpret_cast<void*>(&NativeCreateScene);
 if(at==0x353290 || at==0x353420)return reinterpret_cast<void*>(&NativeBind);if(at==kFactory)return reinterpret_cast<void*>(&NativeFactory);if(at==kDefaultFactory)return reinterpret_cast<void*>(&NativeDefault);
 if(at==0x1195A20)return reinterpret_cast<void*>(&NativePublish);if(at==0x1197050)return reinterpret_cast<void*>(&NativeActivate);return nullptr;
}
}
namespace crew::enemy_native {
// Accepted ctor/fallback and central factory boundaries are explicit ABI stand-ins in this seam fixture.
void* DispatchNativeUnitFactory(void* manager,const float* matrix,void* factory,void* descriptor) {
 ++centralCalls;CHECK(acceptance==Acceptance::hostAccepted || acceptance==Acceptance::acceptedReplay);
 if(acceptance==Acceptance::none || acceptance==Acceptance::guestDeferred)return nullptr;
 return NativeFactory(manager,matrix,static_cast<const wchar_t*>(factory),descriptor);
}
void* InvokeWalkingPartConstructor(void* memory,const void* descriptor,WalkingPartCtorFn original) {
 ++ctorBridges;CHECK(acceptance==Acceptance::hostAccepted || acceptance==Acceptance::acceptedReplay);
 if(acceptance==Acceptance::none || acceptance==Acceptance::guestDeferred)return nullptr;
 if(ctorBridges==rejectCtorAt)return nullptr;
 void* result=original(memory,descriptor);return ctorBridges==rejectCtorAfterAt ? nullptr : result;
}
void* InvokeWalkingPartFallback(void* manager,void* out,const float* matrix,void* descriptor,WalkingPartFallbackFn original) {
 ++fallbackBridges;CHECK(acceptance==Acceptance::hostAccepted || acceptance==Acceptance::acceptedReplay);
 if(acceptance==Acceptance::none || acceptance==Acceptance::guestDeferred)return out;
 if(fallbackBridges==rejectFallbackAt){CHECK(At<const void*>(out,0)==nullptr && At<const void*>(out,8)==nullptr);return nullptr;}
 return original(manager,out,matrix,descriptor);
}
}
namespace edf {
bool Readable(const void* ptr,std::size_t n,bool write) noexcept {if(!ptr)return false;auto p=reinterpret_cast<std::uintptr_t>(ptr);if(n>UINTPTR_MAX-p)return false;const auto end=p+n;while(p<end){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<const void*>(p),&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;if(write && !(m.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;const auto next=reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize;if(next<=p)return false;p=next;}return true;}
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept {return true;}
void* AllocateNearCode(const void*,const unsigned char* code,std::size_t size) noexcept {static unsigned index=0;auto* thunk=image+0x190000+index++*0x1000;if(size==24)publicationThunk=thunk;DWORD old=0;if(!VirtualProtect(thunk,0x1000,PAGE_EXECUTE_READWRITE,&old))return nullptr;std::memcpy(thunk,code,size);FlushInstructionCache(GetCurrentProcess(),thunk,size);return thunk;}
bool RedirectCall(unsigned char* site,void* expected,void* replacement,bool& changed) noexcept {
 static unsigned used=0;changed=false;
 if(!Readable(site,5) || site[0]!=0xE8 || site+5+At<std::int32_t>(site,1)!=expected || used>=32)return false;
 auto* relay=crew::image+0x180000+used++*32;
 unsigned char code[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
 const auto target=reinterpret_cast<std::uintptr_t>(replacement);std::memcpy(code+2,&target,8);std::memcpy(relay,code,sizeof(code));
 site[0]=0xE8;Put<std::int32_t>(site,1,static_cast<std::int32_t>(relay-site-5));
 FlushInstructionCache(GetCurrentProcess(),relay,sizeof(code));changed=true;return true;
}
}
int main(){Image();unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};const auto target=reinterpret_cast<std::uintptr_t>(&SetTeam);std::memcpy(jump+2,&target,8);std::memcpy(image+kSetTeam,jump,sizeof(jump));FlushInstructionCache(GetCurrentProcess(),image+kSetTeam,sizeof(jump));
 CHECK(EnemyControlBerserkerPvpReady());CHECK(EnemyControlBerserkerPvpInstall());NearEmitterCapacityRefusesWholeRocketWave();RocketGroupsCachesAndSealedOwnerDeath();DeferredCallerAndAcceptedFallbackReplay();RejectedBridgeTailsAndWholeGroupRollback();ValidatedModeOffOrdinaryGroupRollsBack();BulletNoticeTransactionsUseRealCore();BommerOriginalOneAndPublicationGuards();CHECK(!controlled_pvp::HasLeases());CHECK(EnemyControlBerserkerPvpReset());VirtualFree(image,0,MEM_RELEASE);image=nullptr;std::printf("enemy_control_pvp_berserker_core_test: %d checks, %d failed\n",checks,failures);return failures ? 1 : 0;}
