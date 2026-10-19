// Production typed containers and birth guards against private native-layout buffers.
#define EDF6_ENEMY_BERSERKER_TEST
#include "../src/enemy_control_pvp_berserker.cpp"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
namespace {
using namespace crew;using namespace berserker_pvp_detail;
int checks=0,failures=0,nativeShots=0,prepared=0,adopted=0,finished=0,faulted=0,published=0,activated=0,deleted=0,configured=0,defaults=0,callerReleased=0,bindingsCalled=0,centralCalls=0,constructorBridges=0,fallbackBridges=0;unsigned freeEmitters=512,lastReserveEmitters=0;
bool leased=false,temporary=false,prepareOk=true,adoptOk=true,useDefault=false,throwDelete=false;
int rejectCtorBefore=0,rejectCtorAfter=0,rejectFallback=0,nativeConstructors=0,bulletPublished=0,deleteBulletNoticeAt=0;
ObjRef controlled{},owned[256]{};unsigned ownedCount=0,remaining=0;int baselineTeam=3,baselineLast=4,capturedBody=-9;
const void* selectorRoot=nullptr;const void* selectorFunction=nullptr;controlled_pvp::SpawnProof spawnProof{};
void Check(bool v,const char* s,int n){++checks;if(!v){++failures;std::fprintf(stderr,"line %d: %s\n",n,s);}}
#define CHECK(v) Check((v),#v,__LINE__)
void __fastcall ControlNoop(const void*){}
void* controlTable[2]={reinterpret_cast<void*>(&ControlNoop),reinterpret_cast<void*>(&ControlNoop)};
struct Object;void NotifyBullet(Object&);
struct Object {
 unsigned char* p=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 unsigned char* c=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 explicit Object(unsigned type=0){if(!p || !c)std::exit(2);Put<const void*>(p,0,type<9 ? image+kTypes[type].table : image+0x1700000);Put<void*>(p,0x28,p);Put<void*>(p,0x30,c);Put<void*>(c,0,controlTable);Put<LONG>(c,8,1);Put<LONG>(c,12,1);Put<int>(p,0x314,3);Put<int>(p,0x318,4);Put<unsigned>(p,0x380,0x40);auto* head=p+0x3000;Put<void*>(head,0,head);Put<void*>(head,8,head);Put<void*>(p,0x550,head);Put<void*>(p,0x48,p+0x3200);Put<std::size_t>(p,0x50,32);}
 ObjRef Ref()const{return {p,c};}
 ~Object(){VirtualFree(p,0,MEM_RELEASE);VirtualFree(c,0,MEM_RELEASE);}
};
std::vector<std::unique_ptr<Object>> pool;
Object* nextPart=nullptr;Object* lastBullet=nullptr;
Object& Fresh(unsigned type){pool.emplace_back(std::make_unique<Object>(type));return *pool.back();}
void __fastcall SetTeam(void* p,int t,bool reg){Put<int>(p,kTeam,t);if(t!=-1)Put<int>(p,0x318,t);auto flags=At<unsigned>(p,0x380);Put<unsigned>(p,0x380,reg ? flags|0x40u : flags&~0x40u);}
void __fastcall Delete(void* p){++deleted;Put<unsigned char>(p,0x18,4);if(throwDelete){throwDelete=false;RaiseException(0xE0424242,0,0,nullptr);}}
void __fastcall NativePublish(void*,const ObjRef*){++published;}
void __fastcall NativeActivate(void*,void*){++activated;}
void Link(Object& root,Object& child){const auto n=At<std::size_t>(root.p,0x58);CHECK(n<32);Put<void*>(root.p+0x3200,n*8,child.p);Put<std::size_t>(root.p,0x58,n+1);Put<void*>(child.p,0x38,root.p);}
void Actions(Object& root,unsigned n){Put<void*>(root.p,0x17F0,root.p+0x2000);Put<std::size_t>(root.p,0x17F8,1);Put<std::size_t>(root.p,0x1800,1);Put<void*>(root.p+0x2000,8,root.p+0x3300);Put<std::size_t>(root.p+0x2000,0x10,n);Put<std::size_t>(root.p+0x2000,0x18,n);}
void Sources(Object& bullet,const ObjRef& owner,int filter=3,int damage=4){Put<const void*>(bullet.p,0xAE8,owner.obj);Put<const void*>(bullet.p,0xAF0,owner.ctrl);Put<const void*>(bullet.p,0x880,owner.obj);Put<const void*>(bullet.p,0x888,owner.ctrl);Put<int>(bullet.p,0xB40,filter);Put<int>(bullet.p,0x894,damage);}
unsigned char* __fastcall NativeFactory(void*,const float*,const wchar_t*,void* init){++configured;auto& b=Fresh(9);Sources(b,{At<const void*>(init,0x38),At<const void*>(init,0x40)},At<int>(init,0x90),At<int>(init,0x90));lastBullet=&b;NotifyBullet(b);return b.p;}
ObjRef* __fastcall NativeDefault(void*,ObjRef* out,const float*,void* init){++defaults;auto& b=Fresh(9);Sources(b,{At<const void*>(init,0x38),At<const void*>(init,0x40)},At<int>(init,0x90),At<int>(init,0x90));lastBullet=&b;*out=b.Ref();NotifyBullet(b);return out;}
unsigned char* __fastcall NativeRocket(void* memory,void* rawInit){
 ++nativeConstructors;const void* owner=At<const void*>(rawInit,0x30);Object* parent=nullptr;
 for(const auto& obj:pool)if(obj->p==owner)parent=obj.get();auto* child=nextPart;CHECK(parent && child && child->p==memory);if(!parent)return nullptr;Link(*parent,*child);
 RocketTeam(memory,At<int>(parent->p,0x318),true);
 alignas(16) unsigned char init[0x1D0]{};Put<const void*>(init,0x38,parent->p);Put<const void*>(init,0x40,parent->c);Put<int>(init,0x90,At<int>(parent->p,0x314));
 lastBullet=nullptr;
 if(useDefault){ObjRef out{reinterpret_cast<void*>(1),reinterpret_cast<void*>(2)};CHECK(DefaultFactory(nullptr,&out,nullptr,init)==&out);if(out){Put<const void*>(memory,0x5D0,out.obj);Put<const void*>(memory,0x5D8,out.ctrl);}}
 else {auto* b=Factory(nullptr,nullptr,const_cast<wchar_t*>(L"native-bullet"),init);if(b){Put<const void*>(memory,0x5D0,b);Put<const void*>(memory,0x5D8,At<const void*>(b,0x30));}}
 const auto damage=reinterpret_cast<std::uintptr_t>(At<const void*>(memory,0x5D0));
 BindDamage(reinterpret_cast<void*>(damage+0xDA0),nullptr);BindFinish(reinterpret_cast<void*>(damage+0xDE0),nullptr);
 return static_cast<unsigned char*>(memory);
}
unsigned char* __fastcall NativeBomb(void* memory,void* init){
 const ObjRef parent{At<const void*>(init,0x30),At<const void*>(init,0x38)};Object* owner=nullptr;for(const auto& o:pool)if(o->p==parent.obj)owner=o.get();
 CHECK(owner && nextPart && nextPart->p==memory);if(owner)Link(*owner,*nextPart);Put<const void*>(memory,0xFC0,parent.obj);Put<const void*>(memory,0xFC8,parent.ctrl);SetTeam(memory,1,true);return static_cast<unsigned char*>(memory);
}
void* callbackThis=nullptr;const void* callbackArgument=nullptr;const void* callbackMetadata=nullptr;const void* callbackObject=nullptr;unsigned callbackStage=0;unsigned char* publicationThunk=nullptr;
void __fastcall SceneCallback(void* self,const void* argument,const void* metadata){++published;callbackThis=self;callbackArgument=argument;callbackMetadata=metadata;callbackObject=At<const void*>(argument,0);callbackStage=At<unsigned>(metadata,0);}
void __fastcall BulletObserver(void* self,const void* argument,const void* metadata){++bulletPublished;CHECK(self!=nullptr);CHECK(At<unsigned>(metadata,0)==37);CHECK(At<const void*>(argument,0)!=nullptr);if(deleteBulletNoticeAt>0 && --deleteBulletNoticeAt==0)Delete(const_cast<void*>(At<const void*>(argument,0)));}
void NotifyBullet(Object& bullet) {
 static void* table[3]={nullptr,nullptr,reinterpret_cast<void*>(&BulletObserver)};static void* callback=table;
 const void* argument=bullet.p;unsigned metadata=37;
 if(useDefault){alignas(16) unsigned char manager[0xCC0]{};Put<void*>(manager,0xCA8,&callback);Put<unsigned>(manager,0xCB0,metadata);DamageActivate(manager,bullet.p);}
 else BulletPublication(&callback,&argument,&metadata,reinterpret_cast<void*>(&BulletObserver));
}
unsigned char* __fastcall NativeCreateScene(void*,const float*,const wchar_t*,void* init){
 nextPart=&Fresh(7);auto* child=BombCtor(nextPart->p,init);unsigned stage=0;const void* arg=child;ScenePublication(reinterpret_cast<void*>(0x123),&arg,&stage,reinterpret_cast<void*>(&SceneCallback));
 if(At<unsigned char>(child,0x18)&4u){++callerReleased;Put<LONG>(nextPart->c,8,0);return nullptr;}return child;
}
void __fastcall NativeShoot(void* raw,int index){++nativeShots;auto* root=static_cast<Object*>(nullptr);for(const auto& obj:pool)if(obj->p==raw)root=obj.get();CHECK(root!=nullptr);if(!root)return;
 const auto* table=At<const unsigned char*>(raw,0x17F0);const auto n=At<std::size_t>(table+static_cast<std::size_t>(index)*0x100,0x18);
 for(std::size_t i=0;i<n;++i){nextPart=&Fresh(6);alignas(16) unsigned char init[0x148]{};Put<void*>(init,0x30,root->p);auto* p=RocketCtor(nextPart->p,init);ObjRef strong=nextPart->Ref();Publish(nullptr,&strong);Activate(nullptr,p);if(At<unsigned char>(p,0x18)&4u){++callerReleased;Put<LONG>(nextPart->c,8,0);}}
}
void Image(){image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!image)std::exit(2);
 for(const auto& a:kAnchors)std::memcpy(image+a.at,a.bytes,a.size);
 std::memcpy(image+kPublication,kPublicationBytes,9);
 std::memcpy(image+kBulletPublication,kBulletPublicationBytes,13);
 for(const auto& h:sites){image[h.at]=0xE8;Put<std::int32_t>(image+h.at,1,static_cast<std::int32_t>(h.target-h.at-5));}
 unsigned i=0;for(const auto& t:kTypes){const unsigned col=0x2000000+i*0x400,td=col+0x100;Put<void*>(image+t.table-8,0,image+col);Put<unsigned>(image+col,0,1);Put<unsigned>(image+col,12,td);Put<unsigned>(image+col,20,col);auto* name=image+td+16;std::memcpy(name,".?AV",4);const auto n=std::strlen(t.name);std::memcpy(name+4,t.name,n);std::memcpy(name+4+n,"@@",3);for(unsigned j=0;j<t.slots;++j)Put<void*>(image+t.table,j*8,image+0x1000+j*16);++i;}
 const unsigned col=0x2050000,chd=col+0x100,list=col+0x120,bcd=col+0x140;Put<void*>(image+0x1700000-8,0,image+col);Put<unsigned>(image+col,0,1);Put<unsigned>(image+col,16,chd);Put<unsigned>(image+col,20,col);Put<unsigned>(image+chd,8,1);Put<unsigned>(image+chd,12,list);Put<unsigned>(image+list,0,bcd);Put<unsigned>(image+bcd,0,0x2022E80);Put<int>(image+bcd,12,-1);
}
void __fastcall NativeBind(void* target,const void*){++bindingsCalled;const void* observed=*reinterpret_cast<const void* const volatile*>(static_cast<const unsigned char*>(target)+0x38);(void)observed;}
bool Collect(Object& r,ObjRef* m,Emitter* e,unsigned* n,unsigned* k,unsigned mc=controlled_pvp::kMaxMembers,unsigned ec=controlled_pvp::kMaxEmitters){return EnemyControlBerserkerPvpCollect(r.Ref(),m,mc,n,e,ec,k);}
void End(){leased=temporary=false;controlled={};ownedCount=0;CHECK(EnemyControlBerserkerPvpReset());pool.clear();prepareOk=adoptOk=true;useDefault=false;remaining=0;nextPart=lastBullet=nullptr;selectorRoot=selectorFunction=nullptr;freeEmitters=512;rejectCtorBefore=rejectCtorAfter=rejectFallback=deleteBulletNoticeAt=0;}
void Families(){
 for(unsigned type=0;type<6;++type){auto& r=Fresh(type);ObjRef m[256]{};Emitter e[512]{};unsigned n=9,k=9;CHECK(EnemyControlBerserkerPvpSupports(r.Ref()));CHECK(Collect(r,m,e,&n,&k));CHECK(n==1 && k==2);CHECK(e[0].member.obj==r.p && e[1].member.obj==r.p);CHECK(e[0].kind==EmitterKind::liveOwnerTeam && e[1].kind==EmitterKind::liveOwnerLastTeam);CHECK(EnemyControlBerserkerPvpPreflight(r.Ref()));}
 End();
 auto& r=Fresh(4);auto& wire=Fresh(8);Link(r,wire);Put<int>(wire.p,0x314,0x12345678);ObjRef m[256]{};Emitter e[512]{};unsigned n=0,k=0;CHECK(Collect(r,m,e,&n,&k));CHECK(n==1);CHECK(At<int>(wire.p,0x314)==0x12345678);CHECK(!EnemyControlBerserkerPvpSupports(wire.Ref()));
 auto* clone=r.p+0x3508;Put<const void*>(clone-8,0,At<const void*>(image+kTypes[4].table-8,0));std::memcpy(clone,image+kTypes[4].table,kTypes[4].slots*8);Put<void*>(r.p,0,clone);Put<void*>(clone,14*8,image+0x12345);CHECK(!EnemyControlBerserkerPvpSupports(r.Ref()));selectorRoot=r.p;selectorFunction=image+0x12345;CHECK(EnemyControlBerserkerPvpSupports(r.Ref()));Put<void*>(clone,15*8,image+0x12346);CHECK(!EnemyControlBerserkerPvpSupports(r.Ref()));End();
}
void Beams(){
 auto& r=Fresh(1);auto& b=Fresh(9);Sources(b,r.Ref());Put<int>(b.p,0xC30,1);
 alignas(16) unsigned char shared[0x700]{},muzzle[0xD0]{};auto* unit=shared+16;Put<LONG>(shared,8,1);Put<LONG>(shared,12,1);Put<void*>(unit,0x2B0,r.p);Put<void*>(unit,0x2B8,r.c);Put<void*>(unit,0x278,muzzle);Put<std::size_t>(unit,0x280,1);Put<std::size_t>(unit,0x288,1);Put<void*>(muzzle,0xB0,b.p);Put<void*>(muzzle,0xB8,b.c);Put<void*>(r.p,0x1A60,unit);Put<void*>(r.p,0x1A68,shared);
 ObjRef m[256]{};Emitter e[512]{};unsigned n=0,k=0;CHECK(Collect(r,m,e,&n,&k));CHECK(k==5);unsigned cache=0;while(cache<k && e[cache].kind!=EmitterKind::ownerLastTeamCache)++cache;CHECK(cache<k);auto filter=e[cache],damage=e[cache+1];int t=-9;
 CHECK(EnemyControlBerserkerPvpRead(filter,&t)==Life::live && t==3);CHECK(EnemyControlBerserkerPvpBaseline(filter,3,4,&t) && t==3);CHECK(EnemyControlBerserkerPvpBaseline(damage,3,4,&t) && t==4);CHECK(EnemyControlBerserkerPvpWrite(filter,2));CHECK(EnemyControlBerserkerPvpWrite(damage,2));CHECK(At<int>(b.p,0x314)==3);CHECK(EnemyControlBerserkerPvpWrite(filter,3));CHECK(EnemyControlBerserkerPvpWrite(damage,4));
 Put<void*>(unit,0x2B8,b.c);CHECK(!Collect(r,m,e,&n,&k));CHECK(n==0 && k==0);Put<void*>(unit,0x2B8,r.c);Put<int>(b.p,0xC30,0);CHECK(Collect(r,m,e,&n,&k));CHECK(k==3);CHECK(EnemyControlBerserkerPvpRead(filter,&t)==Life::live);CHECK(EnemyControlBerserkerPvpWrite(filter,1));CHECK(At<int>(b.p,0xB40)==1);CHECK(EnemyControlBerserkerPvpWrite(filter,3));
 Put<int>(b.p,0xC30,1);Put<void*>(b.p,0x888,r.p);CHECK(!Collect(r,m,e,&n,&k));Put<void*>(b.p,0x888,r.c);CHECK(Collect(r,m,e,&n,&k));DWORD old=0;CHECK(VirtualProtect(b.p,0x1000,PAGE_READONLY,&old)!=0);CHECK(!EnemyControlBerserkerPvpWrite(filter,2));DWORD ignored=0;CHECK(VirtualProtect(b.p,0x1000,old,&ignored)!=0);
 CHECK(EnemyControlBerserkerPvpWrite(filter,2));CHECK(EnemyControlBerserkerPvpWrite(damage,2));Put<void*>(muzzle,0xB0,nullptr);Put<void*>(muzzle,0xB8,nullptr);Put<LONG>(r.c,8,0);
 CHECK(EnemyControlBerserkerPvpRead(filter,&t)==Life::live && t==2);CHECK(EnemyControlBerserkerPvpWrite(filter,3));CHECK(EnemyControlBerserkerPvpWrite(damage,4));CHECK(At<int>(b.p,0xB40)==3 && At<int>(b.p,0x894)==4);
 Put<LONG>(b.c,8,0);CHECK(EnemyControlBerserkerPvpRead(filter,&t)==Life::gone);
 End();CHECK(At<LONG>(shared,12)==1);
}
void SceneAndBaseline(){
 auto& r=Fresh(3);auto& bomb=Fresh(7);Link(r,bomb);Put<void*>(bomb.p,0xFC0,r.p);Put<void*>(bomb.p,0xFC8,r.c);alignas(16) unsigned char list[16]{};Put<void*>(list,0,bomb.p);Put<void*>(list,8,bomb.c);Put<void*>(r.p,0x1A68,list);Put<std::size_t>(r.p,0x1A70,1);Put<std::size_t>(r.p,0x1A78,1);
 ObjRef m[256]{};Emitter e[512]{};unsigned n=0,k=0;CHECK(Collect(r,m,e,&n,&k));CHECK(n==2 && k==2);int t=-1,l=-1;CHECK(EnemyControlBerserkerPvpBaseline(r.Ref(),bomb.Ref(),3,4,&t,&l));CHECK(t==3 && l==4);CHECK(!Collect(r,m,e,&n,&k,1));CHECK(n==0 && k==0);Put<void*>(bomb.p,0xFC8,bomb.c);CHECK(!Collect(r,m,e,&n,&k));Put<void*>(bomb.p,0xFC8,r.c);End();
 auto& stable=Fresh(1);auto& b=Fresh(9);leased=true;temporary=false;controlled=stable.Ref();Sources(b,stable.Ref(),5,4);Put<int>(b.p,0xC30,1);alignas(16) unsigned char shared[0x700]{},slot[0xD0]{};auto* unit=shared+16;Put<LONG>(shared,8,1);Put<LONG>(shared,12,1);Put<void*>(unit,0x2B0,stable.p);Put<void*>(unit,0x2B8,stable.c);Put<void*>(unit,0x278,slot);Put<std::size_t>(unit,0x280,1);Put<std::size_t>(unit,0x288,1);Put<void*>(slot,0xB0,b.p);Put<void*>(slot,0xB8,b.c);Put<void*>(stable.p,0x1A60,unit);Put<void*>(stable.p,0x1A68,shared);CHECK(Collect(stable,m,e,&n,&k));unsigned i=0;while(i<k && e[i].kind!=EmitterKind::ownerLastTeamCache)++i;CHECK(i<k);CHECK(EnemyControlBerserkerPvpBaseline(e[i],3,4,&t) && t==5);CHECK(EnemyControlBerserkerPvpBaseline(e[i+1],3,4,&t) && t==4);CHECK(!EnemyControlBerserkerPvpReset());End();
}
void Birth(){
 auto& r=Fresh(4);Actions(r,2);ObjRef m[256]{};Emitter e[512]{};unsigned n=0,k=0;CHECK(Collect(r,m,e,&n,&k));leased=temporary=true;controlled=r.Ref();SetTeam(r.p,2,true);const int beforeP=published,beforeA=adopted,beforeF=configured;
 CHECK(EnemyControlBerserkerPvpPreflight(r.Ref()));const int oldCentral=centralCalls,oldCtors=constructorBridges;CHECK(EnemyControlBerserkerPvpExecuteParts(r.p,0,&NativeShoot));CHECK(centralCalls==oldCentral+2 && constructorBridges==oldCtors+2);CHECK(nativeShots>0);CHECK(published==beforeP+2 && adopted==beforeA+2 && configured==beforeF+2);CHECK(capturedBody==baselineLast);CHECK(remaining==0);CHECK(Collect(r,m,e,&n,&k));CHECK(n==1 && k==6);unsigned i=0;while(i<k && e[i].kind!=EmitterKind::ownerTeamCache)++i;CHECK(i<k);int t=-9;CHECK(EnemyControlBerserkerPvpBaseline(e[i],3,4,&t) && t==baselineTeam);CHECK(At<int>(lastBullet->p,0xB40)==2);CHECK(!EnemyControlBerserkerPvpReset());End();
 for(bool fallback:{false,true}){auto& x=Fresh(2);Actions(x,1);CHECK(Collect(x,m,e,&n,&k));leased=temporary=true;controlled=x.Ref();SetTeam(x.p,2,true);useDefault=fallback;prepareOk=false;const int ns=nativeShots,ps=published,cf=configured,df=defaults;EnemyControlBerserkerPvpExecuteParts(x.p,0,&NativeShoot);CHECK(nativeShots==ns && published==ps && configured==cf && defaults==df);End();
  auto& y=Fresh(5);Actions(y,1);CHECK(Collect(y,m,e,&n,&k));leased=temporary=true;controlled=y.Ref();SetTeam(y.p,2,true);useDefault=fallback;adoptOk=false;const int pu=published,ac=activated,dc=deleted,fc=configured,fd=defaults,rr=callerReleased;EnemyControlBerserkerPvpExecuteParts(y.p,0,&NativeShoot);CHECK(published==pu && activated==ac && configured==fc && defaults==fd);CHECK(deleted==dc+1 && callerReleased==rr+1);CHECK(remaining==1);End();}
 auto& ordinary=Fresh(0);Actions(ordinary,1);useDefault=true;const int ns=nativeShots,df=defaults,ps=published,fb=fallbackBridges;EnemyControlBerserkerPvpExecuteParts(ordinary.p,0,&NativeShoot);CHECK(nativeShots==ns+1 && defaults==df+1 && published==ps+1 && fallbackBridges==fb+1);End();
}
int expectedBulletNotices=0;
bool RejectGroup(void* seen) noexcept {CHECK(activated==*static_cast<int*>(seen));CHECK(bulletPublished==expectedBulletNotices);return false;}
bool AcceptGroup(void* seen) noexcept {CHECK(activated==*static_cast<int*>(seen) || activated==*static_cast<int*>(seen)+2);CHECK(bulletPublished==expectedBulletNotices || bulletPublished==expectedBulletNotices+2);return true;}
bool RemoveHolders(void* seen) noexcept {
 CHECK(activated==*static_cast<int*>(seen));CHECK(active && active->tracked==2);
 for(unsigned i=0;i<active->tracked;++i){Delete(const_cast<void*>(active->children[i].ref.obj));Put<LONG>(const_cast<void*>(active->children[i].ref.ctrl),8,0);}
 return false;
}
void BridgeRejection(){
 for(unsigned mode=0;mode<3;++mode) {
  auto& r=Fresh(4);Actions(r,2);ObjRef m[256]{};Emitter e[512]{};unsigned n=0,k=0;CHECK(Collect(r,m,e,&n,&k));
  leased=temporary=true;controlled=r.Ref();SetTeam(r.p,2,true);
  if(mode==0)rejectCtorBefore=2;else if(mode==1)rejectCtorAfter=2;else {useDefault=true;rejectFallback=2;}
  const int nc=nativeConstructors,ac=activated,dc=deleted,bn=bulletPublished;
  CHECK(!EnemyControlBerserkerPvpExecuteParts(r.p,0,&NativeShoot));
  CHECK(nativeConstructors==nc+2);CHECK(activated==ac && bulletPublished==bn);CHECK(active==nullptr && construction==nullptr && constructingRocket==nullptr);
  CHECK(deleted==dc+(mode==1 ? 4 : 3));
  for(const auto& p:pool)if(p->p!=r.p)CHECK((At<unsigned char>(p->p,0x18)&4u)!=0);
  End();
 }
 for(bool fallback:{false,true}) {
  auto& r=Fresh(4);Actions(r,2);ObjRef m[256]{};Emitter e[512]{};unsigned n=0,k=0;CHECK(Collect(r,m,e,&n,&k));leased=temporary=true;controlled=r.Ref();SetTeam(r.p,2,true);useDefault=fallback;deleteBulletNoticeAt=1;
  const int ac=activated,dc=deleted,bc=bulletPublished;CHECK(!EnemyControlBerserkerPvpExecuteParts(r.p,0,&NativeShoot));CHECK(activated==ac && deleted==dc+4 && bulletPublished==bc+1);End();
 }
 for(bool ownedGroup:{false,true}) {
  auto& r=Fresh(2);Actions(r,2);ObjRef m[256]{};Emitter e[512]{};unsigned n=0,k=0;CHECK(Collect(r,m,e,&n,&k));
  leased=temporary=ownedGroup;controlled=r.Ref();if(ownedGroup)SetTeam(r.p,2,true);
  int ac=activated;const int dc=deleted,pr=prepared;expectedBulletNotices=bulletPublished;
  CHECK(!EnemyControlBerserkerPvpExecutePartsValidated(r.p,0,&NativeShoot,&RejectGroup,&ac));
  CHECK(activated==ac && deleted==dc+4);CHECK(prepared==pr+(ownedGroup ? 1 : 0));
  End();
  auto& detached=Fresh(4);Actions(detached,2);CHECK(Collect(detached,m,e,&n,&k));ac=activated;const int dd=deleted;expectedBulletNotices=bulletPublished;
  CHECK(!EnemyControlBerserkerPvpExecutePartsValidated(detached.p,0,&NativeShoot,&RemoveHolders,&ac));CHECK(activated==ac && deleted==dd+4);
  for(const auto& obj:pool)if(obj->p!=detached.p)CHECK((At<unsigned char>(obj->p,0x18)&4u)!=0);End();
  auto& good=Fresh(5);Actions(good,2);CHECK(Collect(good,m,e,&n,&k));ac=activated;expectedBulletNotices=bulletPublished;
  CHECK(EnemyControlBerserkerPvpExecutePartsValidated(good.p,0,&NativeShoot,&AcceptGroup,&ac));CHECK(activated==ac+2 && bulletPublished==expectedBulletNotices+2);End();
 }
}
void BombBirthAndShim(){
 auto& root=Fresh(3);ObjRef m[256]{};Emitter e[512]{};unsigned n=0,k=0;CHECK(Collect(root,m,e,&n,&k));leased=temporary=true;controlled=root.Ref();SetTeam(root.p,2,true);
 alignas(16) unsigned char init[0x100]{};Put<const void*>(init,0x30,root.p);Put<const void*>(init,0x38,root.c);
 const int pub=published,ad=adopted;auto* child=CreateBomb(nullptr,nullptr,L"native-bomb",init);CHECK(child!=nullptr);CHECK(published==pub+1 && adopted==ad+1);CHECK(capturedBody==1);CHECK(At<int>(child,0x314)==2);CHECK(Collect(root,m,e,&n,&k));CHECK(n==1);int t=-1,l=-1;CHECK(EnemyControlBerserkerPvpBaseline(root.Ref(),ObjRef::Of(child),3,4,&t,&l));CHECK(t==1 && l==1);Put<LONG>(root.c,8,0);Object* childObject=nullptr;for(const auto& item:pool)if(item->p==child)childObject=item.get();CHECK(childObject!=nullptr);CHECK(Collect(*childObject,m,e,&n,&k));CHECK(n==1 && k==0);End();
 auto& denied=Fresh(3);CHECK(Collect(denied,m,e,&n,&k));leased=temporary=true;controlled=denied.Ref();SetTeam(denied.p,2,true);Put<const void*>(init,0x30,denied.p);Put<const void*>(init,0x38,denied.c);prepareOk=false;const int np=published,nd=deleted;CHECK(CreateBomb(nullptr,nullptr,L"native-bomb",init)==nullptr);CHECK(published==np && deleted==nd);End();
 auto& rejected=Fresh(3);CHECK(Collect(rejected,m,e,&n,&k));leased=temporary=true;controlled=rejected.Ref();SetTeam(rejected.p,2,true);Put<const void*>(init,0x30,rejected.p);Put<const void*>(init,0x38,rejected.c);adoptOk=false;const int rp=published,rd=deleted;CHECK(CreateBomb(nullptr,nullptr,L"native-bomb",init)==nullptr);CHECK(published==rp && deleted==rd+1);CHECK(!pendingDenied);End();
 for(bool failedPrepare:{false,true}) {
  auto& direct=Fresh(3);CHECK(Collect(direct,m,e,&n,&k));leased=temporary=true;controlled=direct.Ref();SetTeam(direct.p,2,true);
  Put<const void*>(init,0x30,direct.p);Put<const void*>(init,0x38,direct.c);prepareOk=!failedPrepare;adoptOk=failedPrepare;
  const int dp=published,dd=deleted,da=adopted;
  // This calls the generic native creator directly, bypassing the higher-level CreateBomb guard.
  CHECK(NativeCreateScene(nullptr,nullptr,L"native-bomb",init)==nullptr);
  CHECK(published==dp && deleted==dd+1 && adopted==da);CHECK(activeBomb==nullptr && !pendingDenied);End();
 }
 // Native caller fragment includes the real null-callback branch and untouched indirect CALL/MOV.
 unsigned char code[]={0x55,0x48,0x83,0xEC,0x70,0x48,0x8D,0x6C,0x24,0x70,0x4C,0x89,0x4C,0x24,0x38,0x4C,0x8B,0x12,0x4C,0x89,0x55,0xB7,0x41,0x8B,0x00,0x89,0x44,0x24,0x48,0x48,0x85,0xC9,0x74,0x13,0x48,0x8B,0x01,0x49,0xBA,0,0,0,0,0,0,0,0,0x41,0xFF,0xD2,0xFF,0x50,0x10,0x48,0x8B,0x4C,0x24,0x38,0x48,0x89,0xC8,0x48,0x83,0xC4,0x70,0x5D,0xC3};
 const auto target=reinterpret_cast<std::uintptr_t>(publicationThunk);std::memcpy(code+39,&target,8);auto* executable=VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);CHECK(executable!=nullptr);std::memcpy(executable,code,sizeof(code));FlushInstructionCache(GetCurrentProcess(),executable,sizeof(code));
 using Caller=void*(__fastcall*)(void*,const void*,const void*,void*);void* table[3]={nullptr,nullptr,reinterpret_cast<void*>(&SceneCallback)};void* callback=table;auto& obj=Fresh(6);const void* argument=obj.p;unsigned metadata=17;auto* replay=reinterpret_cast<void*>(std::uintptr_t{0xAABBCCDD});const int p=published;
 CHECK(reinterpret_cast<Caller>(executable)(&callback,&argument,&metadata,replay)==replay);CHECK(published==p+1);CHECK(callbackThis==&callback);CHECK(callbackObject==obj.p && callbackStage==17);
 CHECK(Hold(obj.Ref()));pendingDenied=obj.Ref();CHECK(reinterpret_cast<Caller>(executable)(&callback,&argument,&metadata,replay)==replay);CHECK(published==p+1 && !pendingDenied);CHECK(At<LONG>(obj.c,12)==1);
 CHECK(reinterpret_cast<Caller>(executable)(nullptr,&argument,&metadata,replay)==replay);CHECK(published==p+1);VirtualFree(executable,0,MEM_RELEASE);End();
}
void BulletNotificationLeaf(){
 // Execute the true caller stack slots and original FF5010 around the 13-byte argument-only leaf.
 unsigned char code[]={0x55,0x48,0x81,0xEC,0xA0,0,0,0,0x4C,0x89,0x4C,0x24,0x38,0x4C,0x8B,0x12,0x4C,0x89,0x94,0x24,0x80,0,0,0,0x41,0x8B,0x00,0x89,0x44,0x24,0x70,0x48,0x85,0xC9,0x74,0x13,0x48,0x8B,0x01,0x49,0xBA,0,0,0,0,0,0,0,0,0x41,0xFF,0xD2,0xFF,0x50,0x10,0x48,0x8B,0x4C,0x24,0x38,0x48,0x89,0xC8,0x48,0x81,0xC4,0xA0,0,0,0,0x5D,0xC3};
 const auto relay=reinterpret_cast<std::uintptr_t>(bulletPublicationRelay);std::memcpy(code+41,&relay,8);
 auto* executable=VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);CHECK(executable!=nullptr);std::memcpy(executable,code,sizeof(code));FlushInstructionCache(GetCurrentProcess(),executable,sizeof(code));
 using Caller=void*(__fastcall*)(void*,const void*,const void*,void*);void* table[3]={nullptr,nullptr,reinterpret_cast<void*>(&SceneCallback)};void* callback=table;auto& bullet=Fresh(9);const void* argument=bullet.p;unsigned metadata=29;auto* sentinel=reinterpret_cast<void*>(std::uintptr_t{0x987654});int seen=published;
 CHECK(reinterpret_cast<Caller>(executable)(&callback,&argument,&metadata,sentinel)==sentinel);CHECK(published==seen+1 && callbackThis==&callback && callbackObject==bullet.p && callbackStage==metadata);
 CHECK(reinterpret_cast<Caller>(executable)(nullptr,&argument,&metadata,sentinel)==sentinel);CHECK(published==seen+1);
 auto& root=Fresh(2);auto& child=Fresh(6);Sources(bullet,root.Ref());Shot state{};state.root=root.Ref();active=&state;Construction ctor{};ctor.memory=child.p;ctor.creatingDamage=true;construction=&ctor;CHECK(TrackChild(child.Ref())!=nullptr);const auto uses=At<LONG>(bullet.c,8);
 CHECK(reinterpret_cast<Caller>(executable)(&callback,&argument,&metadata,sentinel)==sentinel);CHECK(published==seen+1);CHECK(state.children[0].notice.metadata==29 && Same(state.children[0].notice.object,bullet.Ref()));CHECK(At<LONG>(bullet.c,8)==uses+1);
 CHECK(DropStrong(state.children[0].notice.object));CHECK(At<LONG>(bullet.c,8)==uses);CHECK(Drop(state.children[0].ref));active=nullptr;construction=nullptr;VirtualFree(executable,0,MEM_RELEASE);End();
}
void TailAndCapacity(){
 auto& root=Fresh(2);Actions(root,2);ObjRef m[256]{};Emitter e[512]{};unsigned n=0,k=0;CHECK(Collect(root,m,e,&n,&k));leased=temporary=true;controlled=root.Ref();SetTeam(root.p,2,true);freeEmitters=3;const int old=nativeShots;CHECK(!EnemyControlBerserkerPvpPreflight(root.Ref()));EnemyControlBerserkerPvpExecuteParts(root.p,0,&NativeShoot);CHECK(nativeShots==old);End();
 auto& owner=Fresh(4);auto& child=Fresh(6);Link(owner,child);CHECK(Record(owner.Ref(),child.Ref(),4,4,3,4,true));Recorded(child.Ref())->denied=true;
 Shot shot{};shot.root=owner.Ref();shot.owned=shot.failed=true;active=&shot;constructingRocket=child.p;leased=temporary=true;controlled=owner.Ref();
 // Execute the exact native ADD/LEA sequences before the two guarded callback bindings.
 unsigned char code[128]{};unsigned used=0;const unsigned char start[]={0x55,0x48,0x81,0xEC,0,0x05,0,0,0x48,0x8D,0xAC,0x24,0x80,0,0,0,0x48,0x89,0x4D,0xD0};std::memcpy(code,start,sizeof(start));used+=sizeof(start);
 const unsigned char first[]={0x48,0x8B,0x4D,0xD0,0x48,0x81,0xC1,0xA0,0x0D,0,0,0x48,0x8D,0x95,0xE0,0x03,0,0};const unsigned char second[]={0x48,0x8B,0x4D,0xD0,0x48,0x81,0xC1,0xE0,0x0D,0,0,0x48,0x8D,0x95,0x20,0x04,0,0};
 for(unsigned which=0;which<2;++which){std::memcpy(code+used,which ? second : first,sizeof(first));used+=sizeof(first);code[used++]=0x49;code[used++]=0xBB;const auto fn=reinterpret_cast<std::uintptr_t>(which ? &BindFinish : &BindDamage);std::memcpy(code+used,&fn,8);used+=8;code[used++]=0x41;code[used++]=0xFF;code[used++]=0xD3;}
 const unsigned char finish[]={0x48,0x81,0xC4,0,0x05,0,0,0x5D,0xC3};std::memcpy(code+used,finish,sizeof(finish));used+=sizeof(finish);auto* executable=VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);CHECK(executable!=nullptr);std::memcpy(executable,code,used);FlushInstructionCache(GetCurrentProcess(),executable,used);using Tail=void(__fastcall*)(void*);const int before=bindingsCalled;reinterpret_cast<Tail>(executable)(nullptr);CHECK(bindingsCalled==before);
 auto& bullet=Fresh(9);reinterpret_cast<Tail>(executable)(bullet.p);CHECK(bindingsCalled==before+2);VirtualFree(executable,0,MEM_RELEASE);active=nullptr;constructingRocket=nullptr;End();
}


}
namespace crew {
unsigned char* image=nullptr;
bool EnemyControlOwnsTargetSelector(const void* root,const void* function) noexcept{return root==selectorRoot && function==selectorFunction;}
void* EnemyBerserkerTestFunction(unsigned at) noexcept {if(at==kSetTeam)return reinterpret_cast<void*>(&SetTeam);if(at==kDelete)return reinterpret_cast<void*>(&Delete);if(at==kShoot)return reinterpret_cast<void*>(&NativeShoot);if(at==kRocketCtor)return reinterpret_cast<void*>(&NativeRocket);if(at==kBombCtor)return reinterpret_cast<void*>(&NativeBomb);if(at==0x11945E0)return reinterpret_cast<void*>(&NativeCreateScene);if(at==0x353290 || at==0x353420)return reinterpret_cast<void*>(&NativeBind);if(at==kFactory)return reinterpret_cast<void*>(&NativeFactory);if(at==kDefaultFactory)return reinterpret_cast<void*>(&NativeDefault);if(at==0x1195A20)return reinterpret_cast<void*>(&NativePublish);if(at==0x1197050)return reinterpret_cast<void*>(&NativeActivate);return nullptr;}
namespace enemy_native {
void* DispatchNativeUnitFactory(void* manager,const float* matrix,void* factory,void* descriptor){++centralCalls;return NativeFactory(manager,matrix,static_cast<const wchar_t*>(factory),descriptor);}
void* InvokeWalkingPartConstructor(void* memory,const void* descriptor,WalkingPartCtorFn original){++constructorBridges;if(rejectCtorBefore>0 && --rejectCtorBefore==0)return nullptr;void* result=original(memory,descriptor);if(rejectCtorAfter>0 && --rejectCtorAfter==0)return nullptr;return result;}
void* InvokeWalkingPartFallback(void* manager,void* out,const float* matrix,void* descriptor,WalkingPartFallbackFn original){++fallbackBridges;if(rejectFallback>0 && --rejectFallback==0)return out;return original(manager,out,matrix,descriptor);}
}
namespace controlled_pvp {
bool OwnsMember(const ObjRef& r) noexcept {if(!leased)return false;if(Same(r,controlled))return true;for(unsigned i=0;i<ownedCount;++i)if(Same(r,owned[i]))return true;return false;}
bool HasTemporaryTeam(const ObjRef& r) noexcept {return temporary && OwnsMember(r);}
bool ReadBaselineForMember(const ObjRef& r,int* t,int* l) noexcept {if(!OwnsMember(r) || !t || !l)return false;*t=baselineTeam;*l=baselineLast;return true;}
bool CanPrepareSpawns(const ObjRef& r,const SpawnProof& p,unsigned n) noexcept{return OwnsMember(r) && prepareOk && n<=128 && p.maxMembers==1 && p.maxEmitters<=2 && n*p.maxEmitters<=freeEmitters;}
bool PrepareSpawns(const ObjRef& r,const SpawnProof& p,unsigned n,std::uint64_t* token) noexcept {if(token)*token=0;if(!token || !CanPrepareSpawns(r,p,n))return false;spawnProof=p;lastReserveEmitters=n*p.maxEmitters;remaining=n;*token=123;++prepared;return true;}
bool AdoptSpawn(std::uint64_t token,const ObjRef& parent,const ObjRef& child) noexcept {if(token!=123 || !remaining || !OwnsMember(parent) || !adoptOk)return false;ObjRef m[1]{};Emitter e[1]{};unsigned n=0,k=0;if(!spawnProof.hooks.collect(nullptr,child,m,1,&n,e,1,&k) || n!=1 || k!=0)return false;capturedBody=At<int>(child.obj,kTeam);owned[ownedCount++]=child;SetTeam(const_cast<void*>(child.obj),2,true);ObjRef rootMembers[256]{};Emitter emission[512]{};if(!EnemyControlBerserkerPvpCollect(parent,rootMembers,256,&n,emission,512,&k))return false;for(unsigned i=0;i<n;++i)if(Same(rootMembers[i],child))return false;--remaining;++adopted;return true;}
void FinishSpawns(std::uint64_t token) noexcept {if(token)++finished;}
bool FaultForMember(const ObjRef& r,Result) noexcept {if(!OwnsMember(r))return false;++faulted;return true;}
}
}
namespace edf {
bool Readable(const void* ptr,std::size_t n,bool write) noexcept {if(!ptr)return false;auto p=reinterpret_cast<std::uintptr_t>(ptr);if(n>UINTPTR_MAX-p)return false;const auto end=p+n;while(p<end){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<const void*>(p),&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;if(write && !(m.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;const auto next=reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize;if(next<=p)return false;p=next;}return true;}
void* AllocateNearCode(const void*,const unsigned char* code,std::size_t size) noexcept {static unsigned index=0;auto* thunk=image+0x190000+index++*0x1000;if(size==24)publicationThunk=thunk;DWORD old=0;if(!VirtualProtect(thunk,0x1000,PAGE_EXECUTE_READWRITE,&old))return nullptr;std::memcpy(thunk,code,size);FlushInstructionCache(GetCurrentProcess(),thunk,size);return thunk;}
bool RedirectCall(unsigned char* p,void*,void* function,bool& changed) noexcept {static unsigned slot=0;auto* target=image+0x100000+slot++*0x100;const unsigned char bytes[]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};std::memcpy(target,bytes,sizeof(bytes));std::memcpy(target+2,&function,8);const auto offset=static_cast<std::int32_t>(target-(p+5));std::memcpy(p+1,&offset,4);changed=true;return true;}
}
int main(){Image();CHECK(EnemyControlBerserkerPvpReady());CHECK(EnemyControlBerserkerPvpInstall());Families();Beams();SceneAndBaseline();Birth();BridgeRejection();BombBirthAndShim();BulletNotificationLeaf();TailAndCapacity();auto* relay=image+sites[0].at+5+At<std::int32_t>(image+sites[0].at,1);relay[0]^=1;CHECK(!EnemyControlBerserkerPvpReady());relay[0]^=1;CHECK(EnemyControlBerserkerPvpReady());image[sites[0].at+1]^=1;CHECK(!EnemyControlBerserkerPvpReady());image[sites[0].at+1]^=1;CHECK(EnemyControlBerserkerPvpReady());CHECK(EnemyControlBerserkerPvpReset());VirtualFree(image,0,MEM_RELEASE);std::printf("%d checks, %d failures\n",checks,failures);return failures ? 1 : 0;}
