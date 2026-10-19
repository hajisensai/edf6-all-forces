// Production BigGreyBoss family against private buffers; no game DLL execution.
#define EDF6_ENEMY_BOSS_TEST
#include "../src/enemy_control_pvp_boss.cpp"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
namespace {
using namespace crew;
int checks=0,failures=0,published=0,deleted=0,prepared=0,adopted=0,faulted=0,finished=0,lastPublished=-1,lastCaptured=-1;
bool controlling=false,prepareOk=true,adoptOk=true,deleteFault=false;ObjRef adoptedRefs[256]{};unsigned adoptedCount=0;
ObjRef controller{};int originalTeam=0,originalLast=0;unsigned planned=0,remaining=0;
controlled_pvp::SpawnProof proof{};
ObjRef selectorOwner{};const void* selectorTable=nullptr;const void* selectorFunction=nullptr;
ObjRef cacheOwner{},cacheStorage{};int* cacheFirst=nullptr;int* cacheSecond=nullptr;
struct SavedCache {controlled_pvp::Emitter binding{};int original=-1;bool used=false;};
SavedCache savedCaches[2]{};unsigned retainedReads=0,retainedWrites=0,blockedReads=0;
const unsigned char* blockedObject=nullptr;
bool SameBinding(const controlled_pvp::Emitter& a,const controlled_pvp::Emitter& b) {
    return a.kind==b.kind && boss_pvp_detail::Same(a.member,b.member) && a.object==b.object &&
        a.incarnation==b.incarnation && boss_pvp_detail::Same(a.storage,b.storage);
}
void Check(bool v,const char* s,int line){++checks;if(!v){++failures;std::fprintf(stderr,"line %d: %s\n",line,s);}}
#define CHECK(v) Check((v),#v,__LINE__)
void __fastcall Publish(void*,const ObjRef* arg,unsigned kind,unsigned,int,int){++published;CHECK(kind==2);lastPublished=edf::At<int>(arg->obj,edf::kTeam);crew::boss_pvp_detail::DropControl(arg->ctrl);}
void __fastcall Delete(void* p){++deleted;edf::Put<unsigned char>(p,0x18,4);if(deleteFault){deleteFault=false;RaiseException(0xE0424242,0,0,nullptr);}}
void __fastcall SetTeam(void* p,int team,bool reg){edf::Put<int>(p,edf::kTeam,team);if(team!=-1)edf::Put<int>(p,0x318,team);auto f=edf::At<unsigned>(p,0x380);edf::Put<unsigned>(p,0x380,reg ? f|0x40u : f&~0x40u);}
struct Object {
    unsigned char* p=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char* c=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    explicit Object(unsigned type=0){if(!p || !c)std::exit(2);edf::Put<const void*>(p,0,image+boss_pvp_detail::kTypes[type].table);edf::Put<void*>(p,0x28,p);edf::Put<void*>(p,0x30,c);edf::Put<LONG>(c,8,1);edf::Put<LONG>(c,12,1);edf::Put<int>(p,0x314,0);edf::Put<int>(p,0x318,0);edf::Put<unsigned>(p,0x380,0x40);Empty();}
    void Empty(){auto* h=p+0x3000;edf::Put<void*>(h,0,h);edf::Put<void*>(h,8,h);edf::Put<void*>(p,0x550,h);auto* parts=p+0x3100;edf::Put<void*>(parts,0,parts);edf::Put<void*>(parts,8,parts);edf::Put<void*>(p,0x17E8,parts);}
    ObjRef Ref()const{return {p,c};}
    ~Object(){VirtualFree(p,0,MEM_RELEASE);VirtualFree(c,0,MEM_RELEASE);}
};
void Image(){image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!image)std::exit(2);
    for(const auto& a:boss_pvp_detail::kAnchors)std::memcpy(image+a.rva,a.bytes,a.size);
    std::memcpy(image+boss_pvp_detail::kPublishCall,boss_pvp_detail::kPublishBytes,5);
    edf::Put<void*>(image+0x17E6EE0,8,image+0x6CF7D0);edf::Put<void*>(image+0x17E6EE0,16,image+0x6CFEF0);edf::Put<void*>(image+0x17E6F08,8,image+0x6CF910);edf::Put<void*>(image+0x17E6F08,16,image+0x6CFF00);
    unsigned i=0;for(const auto& t:boss_pvp_detail::kTypes){const unsigned col=0x2000000+i*0x400,td=col+0x100;edf::Put<void*>(image+t.table-8,0,image+col);edf::Put<unsigned>(image+col,0,1);edf::Put<unsigned>(image+col,12,td);edf::Put<unsigned>(image+col,20,col);auto* name=image+td+16;std::memcpy(name,".?AV",4);const auto n=std::strlen(t.name);std::memcpy(name+4,t.name,n);std::memcpy(name+4+n,"@@",3);for(unsigned j=0;j<t.slots;++j)edf::Put<void*>(image+t.table,j*8,image+0x1000+j*16);edf::Put<void*>(image+t.table,22*8,image+0x54ABD0);++i;}}
void Born(Object& root,Object& child,int index=0,int total=20,unsigned wave=1){edf::Put<int>(child.p,0x314,edf::At<int>(root.p,0x314));edf::Put<int>(child.p,0x318,edf::At<int>(root.p,0x318));InterlockedIncrement(reinterpret_cast<volatile LONG*>(child.c+12));auto weak=child.Ref();boss_pvp_detail::OnMeteor(root.p,&weak,2,wave,index,total);}
bool Collect(Object& root,unsigned* n,unsigned* e,ObjRef* members=nullptr,controlled_pvp::Emitter* emitters=nullptr,unsigned cap=controlled_pvp::kMaxMembers){ObjRef local[controlled_pvp::kMaxMembers]{};controlled_pvp::Emitter le[controlled_pvp::kMaxEmitters]{};return EnemyControlBossPvpCollect(root.Ref(),members ? members : local,cap,n,emitters ? emitters : le,controlled_pvp::kMaxEmitters,e);}
void End(){controlling=false;controller={};CHECK(EnemyControlBossPvpReset());prepareOk=adoptOk=true;remaining=0;adoptedCount=0;}
void SelectorClone(){
    Object root,other;alignas(16) unsigned char clone[(86+1)*8]{};
    std::memcpy(clone,image+boss_pvp_detail::kTypes[0].table-8,sizeof(clone));
    auto* table=clone+8;edf::Put<void*>(root.p,0,table);
    CHECK(EnemyControlBossPvpSupports(root.Ref()));
    const auto original=edf::At<const void*>(table,14*8);const auto known=image+0x9900;
    edf::Put<const void*>(table,14*8,known);CHECK(!EnemyControlBossPvpSupports(root.Ref()));
    selectorOwner=root.Ref();selectorTable=table;selectorFunction=known;
    edf::Put<void*>(root.p,0x30,other.c);CHECK(!EnemyControlBossPvpSupports(ObjRef::Of(root.p)));edf::Put<void*>(root.p,0x30,root.c);
    CHECK(EnemyControlBossPvpSupports(root.Ref()));unsigned n=0,e=0;CHECK(Collect(root,&n,&e));CHECK(n==1 && e==1);
    edf::Put<void*>(table,14*8,image+0x9910);CHECK(!EnemyControlBossPvpSupports(root.Ref()));
    edf::Put<const void*>(table,14*8,known);selectorOwner=other.Ref();CHECK(!EnemyControlBossPvpSupports(root.Ref()));
    selectorOwner=root.Ref();selectorTable=image+boss_pvp_detail::kTypes[0].table;CHECK(!EnemyControlBossPvpSupports(root.Ref()));
    selectorTable=table;const auto s13=edf::At<const void*>(table,13*8);edf::Put<void*>(table,13*8,image+0x9920);CHECK(!EnemyControlBossPvpSupports(root.Ref()));edf::Put<const void*>(table,13*8,s13);
    selectorOwner={};selectorTable=nullptr;selectorFunction=nullptr;edf::Put<const void*>(table,14*8,original);CHECK(EnemyControlBossPvpSupports(root.Ref()));End();
}
void CachedBeams(){
    Object root,beam;int& first=*reinterpret_cast<int*>(beam.p+0xB40);int& second=*reinterpret_cast<int*>(beam.p+0x894);
    first=4;second=3;cacheOwner=root.Ref();cacheStorage=beam.Ref();cacheFirst=&first;cacheSecond=&second;
    ObjRef m[256]{};controlled_pvp::Emitter emitters[512]{};unsigned count=0,emitted=0;
    CHECK(Collect(root,&count,&emitted,m,emitters));CHECK(count==1 && emitted==3);
    int a=-1,b=-1;CHECK(EnemyControlBossPvpBaseline(emitters[0],0,0,&a));CHECK(EnemyControlBossPvpBaseline(emitters[1],0,0,&b));CHECK(a==4 && b==3);
    controlling=true;controller=root.Ref();edf::Put<int>(root.p,0x314,2);edf::Put<int>(root.p,0x318,2);
    CHECK(EnemyControlBossPvpWrite(emitters[0],2));CHECK(EnemyControlBossPvpWrite(emitters[1],2));CHECK(first==2 && second==2);int inherited=-1;CHECK(EnemyControlBossPvpBaseline(emitters[0],5,5,&inherited) && inherited==5);
    CHECK(EnemyControlBossPvpWrite(emitters[0],a));CHECK(EnemyControlBossPvpWrite(emitters[1],b));CHECK(first==4 && second==3);
    auto foreign=emitters[0];foreign.object=root.p+0x1000;CHECK(!EnemyControlBossPvpWrite(foreign,1));CHECK(edf::At<int>(root.p,0x1000)==0);
    foreign=emitters[0];foreign.storage.ctrl=root.c;int wrong=-1;CHECK(EnemyControlBossPvpRead(foreign,&wrong)==controlled_pvp::Life::gone);CHECK(!EnemyControlBossPvpWrite(foreign,1));
    cacheOwner={};cacheStorage={};cacheFirst=cacheSecond=nullptr;int read=-1;CHECK(EnemyControlBossPvpRead(emitters[0],&read)==controlled_pvp::Life::gone);End();
}
void RetainedCachedBeams() {
    // This fixture verifies the family-to-shared callback boundary. The actual
    // shared RTTI/source/weak-pin implementation is exercised by native_test.
    Object root,beam;cacheOwner=root.Ref();cacheStorage=beam.Ref();
    cacheFirst=reinterpret_cast<int*>(beam.p+0xB40);cacheSecond=reinterpret_cast<int*>(beam.p+0x894);*cacheFirst=4;*cacheSecond=3;
    edf::Put<void*>(beam.p,0xAE8,root.p);edf::Put<void*>(beam.p,0xAF0,root.c);edf::Put<void*>(beam.p,0x880,root.p);edf::Put<void*>(beam.p,0x888,root.c);
    ObjRef members[256]{};controlled_pvp::Emitter emitters[512]{};unsigned count=0,emitted=0;
    CHECK(Collect(root,&count,&emitted,members,emitters));CHECK(count==1 && emitted==3);
    CHECK(boss_pvp_detail::Same(emitters[0].storage,beam.Ref()));
    savedCaches[0]={emitters[0],4,true};savedCaches[1]={emitters[1],3,true};
    CHECK(EnemyControlBossPvpWrite(emitters[0],2));CHECK(EnemyControlBossPvpWrite(emitters[1],2));CHECK(*cacheFirst==2 && *cacheSecond==2);
    const auto before=retainedReads;cacheOwner={}; // no longer in any current typed weapon map
    int value=-1;CHECK(EnemyControlBossPvpRead(emitters[0],&value)==controlled_pvp::Life::live && value==2);CHECK(retainedReads>before);
    CHECK(EnemyControlBossPvpBaseline(emitters[0],5,5,&value) && value==4);
    CHECK(EnemyControlBossPvpBaseline(emitters[1],5,5,&value) && value==3);
    edf::Put<LONG>(root.c,8,0);blockedObject=root.p;blockedReads=0;
    CHECK(EnemyControlBossPvpRead(emitters[0],&value)==controlled_pvp::Life::live && value==2);
    CHECK(EnemyControlBossPvpBaseline(emitters[0],-1,-1,&value) && value==4);
    CHECK(EnemyControlBossPvpWrite(emitters[0],4));CHECK(EnemyControlBossPvpWrite(emitters[1],3));
    CHECK(*cacheFirst==4 && *cacheSecond==3 && blockedReads==0 && retainedWrites>=4);
    auto foreign=emitters[0];foreign.storage.ctrl=root.c;CHECK(!EnemyControlBossPvpWrite(foreign,1));
    savedCaches[0].used=false;CHECK(EnemyControlBossPvpRead(emitters[0],&value)==controlled_pvp::Life::invalid);
    CHECK(blockedReads==0);savedCaches[0].used=true;
    blockedObject=beam.p;blockedReads=0;edf::Put<LONG>(beam.c,8,0);
    CHECK(EnemyControlBossPvpRead(emitters[0],&value)==controlled_pvp::Life::gone);CHECK(blockedReads==0);
    CHECK(!EnemyControlBossPvpWrite(emitters[0],2));
    blockedObject=nullptr;cacheStorage={};cacheFirst=cacheSecond=nullptr;savedCaches[0]={};savedCaches[1]={};End();
}
void RootAndPart(){Object root,part(4),env(7);ObjRef m[controlled_pvp::kMaxMembers]{};controlled_pvp::Emitter e[controlled_pvp::kMaxEmitters]{};unsigned n=99,k=99;
    CHECK(Collect(root,&n,&k,m,e));CHECK(n==1 && k==1);CHECK(e[0].kind==controlled_pvp::EmitterKind::liveOwnerLastTeam);
    edf::Put<int>(root.p,0x314,2);edf::Put<int>(root.p,0x318,1);int value=-1;CHECK(EnemyControlBossPvpRead(e[0],&value)==controlled_pvp::Life::live && value==1);CHECK(!EnemyControlBossPvpWrite(e[0],2));
    auto* head=root.p+0x3100;alignas(16) unsigned char node[0x50]{},entries[16]{},ctx[0x400]{},cc[32]{},maker[0x400]{},mc[32]{};
    edf::Put<void*>(head,0,node);edf::Put<void*>(head,8,node);edf::Put<void*>(node,0,head);edf::Put<void*>(node,8,head);edf::Put<const void*>(node,0x40,part.p);edf::Put<const void*>(node,0x48,part.c);edf::Put<std::size_t>(root.p,0x17F0,1);
    edf::Put<void*>(part.p,0xAE8,root.p);edf::Put<void*>(part.p,0xAF0,root.c);edf::Put<void*>(part.p,0xB60,entries);edf::Put<std::size_t>(part.p,0xB68,1);edf::Put<std::size_t>(part.p,0xB70,1);
    edf::Put<void*>(entries,0,ctx);edf::Put<void*>(entries,8,cc);edf::Put<LONG>(cc,8,1);edf::Put<LONG>(cc,12,1);edf::Put<LONG>(mc,8,1);edf::Put<LONG>(mc,12,1);edf::Put<void*>(maker,0,image+0x17E6EE0);
    edf::Put<void*>(ctx,0x200,root.p);edf::Put<void*>(ctx,0x208,root.c);edf::Put<void*>(ctx,0x210,part.p);edf::Put<void*>(ctx,0x218,part.c);edf::Put<void*>(ctx,0x30,root.p);edf::Put<void*>(ctx,0x38,root.c);edf::Put<void*>(ctx,0x40,maker);edf::Put<void*>(ctx,0x48,mc);
    CHECK(Collect(root,&n,&k,m,e));CHECK(n==2 && k==2);CHECK(m[1].obj==part.p);unsigned pn=0,pe=0;CHECK(!Collect(part,&pn,&pe));CHECK(pn==0 && pe==0);unsigned launcherIndex=0;while(launcherIndex<k && e[launcherIndex].object!=ctx+0x20)++launcherIndex;CHECK(launcherIndex<k);CHECK(e[launcherIndex].member.obj==root.p);
    edf::Put<int>(part.p,0x314,-1);edf::Put<int>(part.p,0x318,-1);
    CHECK(EnemyControlBossPvpRead(e[launcherIndex],&value)==controlled_pvp::Life::live && value==2);CHECK(EnemyControlBossPvpWrite(e[launcherIndex],2));CHECK(edf::At<LONG>(cc,12)==2 && edf::At<LONG>(mc,12)==2);
    const LONG rw=edf::At<LONG>(root.c,12),pw=edf::At<LONG>(part.c,12);const auto incarnation=e[launcherIndex].incarnation;
    for(unsigned repeat=0;repeat<16;++repeat){CHECK(Collect(root,&n,&k,m,e));CHECK(edf::At<LONG>(cc,12)==2 && edf::At<LONG>(mc,12)==2);CHECK(edf::At<LONG>(root.c,12)==rw && edf::At<LONG>(part.c,12)==pw);CHECK(edf::At<int>(root.p,0x314)==2 && edf::At<int>(root.p,0x318)==1 && edf::At<int>(part.p,0x314)==-1);CHECK(e[launcherIndex].incarnation==incarnation);}
    edf::Put<void*>(part.p,0xAE8,env.p);edf::Put<void*>(part.p,0xAF0,env.c);n=k=99;CHECK(!Collect(root,&n,&k,m,e));CHECK(n==0 && k==0);edf::Put<void*>(part.p,0xAE8,root.p);edf::Put<void*>(part.p,0xAF0,root.c);
    n=k=99;CHECK(!Collect(root,&n,&k,m,e,1));CHECK(n==0 && k==0);
    auto saved=e[launcherIndex];edf::Put<LONG>(root.c,8,0);CHECK(EnemyControlBossPvpSupports(part.Ref()));CHECK(EnemyControlBossPvpRead(saved,&value)==controlled_pvp::Life::gone);edf::Put<LONG>(root.c,8,1);
    End();CHECK(edf::At<LONG>(cc,12)==1 && edf::At<LONG>(mc,12)==1);CHECK(edf::At<LONG>(root.c,12)==1 && edf::At<LONG>(part.c,12)==1);
}
void MeteorLifecycle(){Object root,prior(7),fresh(7),environment(7);unsigned n=0,e=0;ObjRef members[controlled_pvp::kMaxMembers]{};
    const int before=published;Born(root,prior);CHECK(published==before+1);CHECK(edf::At<LONG>(prior.c,12)==2);CHECK(Collect(root,&n,&e,members));CHECK(n==2 && e==2);CHECK(members[1].obj==prior.p);
    for(unsigned i=0;i<n;++i)CHECK(members[i].obj!=environment.p);
    controlling=true;controller=root.Ref();originalTeam=0;originalLast=0;edf::Put<int>(root.p,0x314,2);edf::Put<int>(root.p,0x318,2);edf::Put<float>(root.p,0x26B0,3);
    CHECK(EnemyControlBossPvpPreflight(root.Ref()));CHECK(planned==20);edf::Put<float>(root.p,0x26B0,19);CHECK(EnemyControlBossPvpPreflight(root.Ref()));CHECK(planned==40);
    const int pc=prepared,ac=adopted;Born(root,fresh);CHECK(prepared==pc+1 && adopted==ac+1);CHECK(lastCaptured==0 && lastPublished==2);CHECK(remaining==19);std::vector<std::unique_ptr<Object>> rest;const int f0=finished;for(int i=1;i<20;++i){rest.emplace_back(std::make_unique<Object>(7));Born(root,*rest.back(),i,20,1);}CHECK(remaining==0 && finished==f0+1);CHECK(prepared==pc+1 && adopted==ac+20);int team=-1,last=-1;
    CHECK(EnemyControlBossPvpBaseline(root.Ref(),fresh.Ref(),0,0,&team,&last));CHECK(team==0 && last==0);CHECK(EnemyControlBossPvpBaseline(fresh.Ref(),fresh.Ref(),2,2,&team,&last));CHECK(team==0 && last==0);
    CHECK(!EnemyControlBossPvpReset());controlling=false;controller={};edf::Put<LONG>(prior.c,8,0);CHECK(Collect(root,&n,&e,members));CHECK(n==21);CHECK(edf::At<LONG>(prior.c,12)==1);
    End();CHECK(edf::At<LONG>(root.c,12)==1 && edf::At<LONG>(fresh.c,12)==1);for(const auto& x:rest)CHECK(edf::At<LONG>(x->c,12)==1);
}
void FailureAndIdentity(){Object root,child(7);controlling=true;controller=root.Ref();edf::Put<int>(root.p,0x314,2);edf::Put<int>(root.p,0x318,2);prepareOk=false;const int p=published,d=deleted,f=faulted;Born(root,child);CHECK(published==p && deleted==d+1 && faulted==f+1);CHECK(edf::At<LONG>(child.c,12)==1);unsigned n=1,e=1;CHECK(Collect(root,&n,&e));CHECK(n==1 && e==1);controlling=false;CHECK(!Collect(root,&n,&e));CHECK(n==0 && e==0);End();
    Object exceptional,bad(7);controlling=true;controller=exceptional.Ref();prepareOk=false;deleteFault=true;const int dp=deleted,wp=edf::At<LONG>(bad.c,12),fp=faulted;Born(exceptional,bad);CHECK(deleted==dp+1 && faulted==fp+1);CHECK(edf::At<LONG>(bad.c,12)==wp);End();
    Object other,old(7);Born(other,old);auto* replacement=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));CHECK(replacement!=nullptr);edf::Put<LONG>(replacement,8,1);edf::Put<LONG>(replacement,12,1);edf::Put<void*>(old.p,0x30,replacement);CHECK(Collect(other,&n,&e));CHECK(n==1 && e==1);CHECK(edf::At<LONG>(old.c,12)==1);End();VirtualFree(replacement,0,MEM_RELEASE);
}
void FullLedger(){Object root;std::vector<std::unique_ptr<Object>> all;all.reserve(boss_pvp_detail::kMeteorLimit+1);const int p=published,d=deleted;
    for(unsigned i=0;i<boss_pvp_detail::kMeteorLimit+1;++i){all.emplace_back(std::make_unique<Object>(7));Born(root,*all.back());}
    CHECK(published==p+static_cast<int>(boss_pvp_detail::kMeteorLimit)+1);CHECK(deleted==d);CHECK(EnemyControlBossPvpPreflight(root.Ref()));unsigned n=9,e=9;CHECK(!Collect(root,&n,&e));CHECK(n==0 && e==0);CHECK(EnemyControlBossPvpSupports(root.Ref()));End();CHECK(edf::At<LONG>(root.c,12)==1);
}
}
namespace crew {
unsigned char* image=nullptr;
void* EnemyBossTestFunction(unsigned rva) noexcept {if(rva==boss_pvp_detail::kPublish)return reinterpret_cast<void*>(Publish);if(rva==boss_pvp_detail::kDelete)return reinterpret_cast<void*>(Delete);if(rva==kSetTeam)return reinterpret_cast<void*>(SetTeam);return nullptr;}
bool EnemyControlOwnsTargetSelector(const void* root,const void* function) noexcept {return selectorOwner.obj==root && selectorOwner.Is(root) && selectorTable && edf::At<const void*>(root,0)==selectorTable && function==selectorFunction;}
bool EnemyControlNativePvpCollectHumanoidWeapons(const ObjRef& owner,controlled_pvp::Emitter* out,unsigned cap,unsigned* count) noexcept {if(!out || !count || *count>cap)return false;if(!boss_pvp_detail::Same(owner,cacheOwner))return true;if(cap-*count<2)return false;out[(*count)++]={controlled_pvp::EmitterKind::ownerLastTeamCache,owner,cacheFirst,1001,cacheStorage};out[(*count)++]={controlled_pvp::EmitterKind::ownerLastTeamCache,owner,cacheSecond,1002,cacheStorage};return true;}
bool EnemyControlNativePvpCacheRetained(const controlled_pvp::Emitter& e) noexcept {
    int original=-1;return (e.kind==controlled_pvp::EmitterKind::ownerTeamCache || e.kind==controlled_pvp::EmitterKind::ownerLastTeamCache) &&
        e.storage.obj && e.storage.ctrl && controlled_pvp::ReadEmitterBaseline(e,&original);
}
controlled_pvp::Life EnemyControlNativePvpReadRetainedCache(const controlled_pvp::Emitter& e,int* team) noexcept {
    ++retainedReads;using controlled_pvp::Life;if(!team || !EnemyControlNativePvpCacheRetained(e))return Life::invalid;
    if(!Readable(e.storage.ctrl,16))return Life::invalid;if(At<LONG>(e.storage.ctrl,8)<=0)return Life::gone;
    const auto* p=static_cast<const unsigned char*>(e.storage.obj);
    if(!Readable(p,0xB44) || At<const void*>(p,0x28)!=p || At<const void*>(p,0x30)!=e.storage.ctrl ||
       At<const void*>(p,0xAE8)!=e.member.obj || At<const void*>(p,0xAF0)!=e.member.ctrl ||
       At<const void*>(p,0x880)!=e.member.obj || At<const void*>(p,0x888)!=e.member.ctrl ||
       (e.object!=p+0xB40 && e.object!=p+0x894))return Life::invalid;
    *team=At<int>(e.object,0);return Life::live;
}
bool EnemyControlNativePvpWriteRetainedCache(const controlled_pvp::Emitter& e,int team) noexcept {
    int current=-1;if(team< -1 || team>6 || EnemyControlNativePvpReadRetainedCache(e,&current)!=controlled_pvp::Life::live)return false;
    ++retainedWrites;Put<int>(const_cast<void*>(e.object),0,team);return true;
}
bool EnemyControlNativePvpCollect(const ObjRef&,ObjRef*,unsigned,unsigned* n,controlled_pvp::Emitter*,unsigned,unsigned* e) noexcept {if(n)*n=0;if(e)*e=0;return false;}
namespace controlled_pvp {
bool OwnsMember(const ObjRef& r) noexcept {if(!controlling)return false;if(boss_pvp_detail::Same(r,controller))return true;for(unsigned i=0;i<adoptedCount;++i)if(boss_pvp_detail::Same(r,adoptedRefs[i]))return true;return false;}
bool HasTemporaryTeam(const ObjRef& r) noexcept {return OwnsMember(r);}
bool ReadEmitterBaseline(const Emitter& e,int* team) noexcept {
    if(!team)return false;for(const auto& saved:savedCaches)if(saved.used && SameBinding(saved.binding,e)){*team=saved.original;return true;}return false;
}
bool ReadBaselineForMember(const ObjRef& r,int* t,int* l) noexcept {if(!OwnsMember(r) || !t || !l)return false;*t=originalTeam;*l=originalLast;return true;}
bool CanPrepareSpawns(const ObjRef& r,const SpawnProof& p,unsigned count) noexcept {planned=count;return OwnsMember(r) && prepareOk && p.maxMembers==1 && p.maxEmitters==1 && count<=40;}
bool PrepareSpawns(const ObjRef& r,const SpawnProof& p,unsigned count,std::uint64_t* token) noexcept {if(token)*token=0;if(!token || !CanPrepareSpawns(r,p,count))return false;proof=p;remaining=count;*token=123;++prepared;return true;}
bool AdoptSpawn(std::uint64_t token,const ObjRef& parent,const ObjRef& child) noexcept {if(token!=123 || !OwnsMember(parent) || !remaining || !adoptOk)return false;ObjRef m[1]{};Emitter e[1]{};unsigned n=0,k=0;if(!proof.hooks.collect(nullptr,child,m,1,&n,e,1,&k) || n!=1 || k!=1)return false;lastCaptured=At<int>(child.obj,0x314);adoptedRefs[adoptedCount++]=child;SetTeam(const_cast<void*>(child.obj),2,true);ObjRef main[256]{};Emitter emission[512]{};unsigned count=0,emitted=0;if(!EnemyControlBossPvpCollect(parent,main,256,&count,emission,512,&emitted))return false;for(unsigned i=0;i<count;++i)if(boss_pvp_detail::Same(main[i],child))return false;--remaining;++adopted;return true;}
void FinishSpawns(std::uint64_t token) noexcept {if(token)++finished;}
bool FaultForMember(const ObjRef& r,Result) noexcept {if(!OwnsMember(r))return false;++faulted;return true;}
}
}
namespace edf {
bool Readable(const void* p,std::size_t n,bool) noexcept {if(!p)return false;
    if(blockedObject) {const auto x=reinterpret_cast<std::uintptr_t>(p),b=reinterpret_cast<std::uintptr_t>(blockedObject);
        if((x>=b && x-b<0x4000) || (x<b && b-x<n)){++blockedReads;return false;}}
    auto at=reinterpret_cast<std::uintptr_t>(p);if(n>UINTPTR_MAX-at)return false;const auto end=at+n;while(at<end){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<const void*>(at),&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;const auto next=reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize;if(next<=at)return false;at=next;}return true;}
bool RedirectCall(unsigned char* p,void*,void*,bool& changed) noexcept {p[1]^=0x55;changed=true;return true;}
}
int main(){Image();CHECK(EnemyControlBossPvpReady());CHECK(EnemyControlBossPvpInstall());CHECK(EnemyControlBossPvpReady());SelectorClone();CachedBeams();RetainedCachedBeams();RootAndPart();MeteorLifecycle();FailureAndIdentity();FullLedger();image[boss_pvp_detail::kPublishCall+1]^=1;CHECK(!EnemyControlBossPvpReady());image[boss_pvp_detail::kPublishCall+1]^=1;CHECK(EnemyControlBossPvpReady());CHECK(EnemyControlBossPvpReset());VirtualFree(image,0,MEM_RELEASE);std::printf("%d checks, %d failures\n",checks,failures);return failures ? 1 : 0;}
