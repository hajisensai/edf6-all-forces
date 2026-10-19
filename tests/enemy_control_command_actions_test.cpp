#define ENEMY_CONTROL_COMMAND_ACTIONS_TEST
#include "../src/enemy_control_command_actions.cpp"
#include <cstdio>
#include <cstdlib>
namespace crew {unsigned char* image=nullptr;}
namespace {
using namespace crew;using namespace crew::enemy_native;using namespace crew::enemy_native::command_actions;
constexpr NativeProfile profiles[]={
 {"Monster501",0x17C1000,0,0,1,64,0,Kind::monster,3,4},
 {"EDF6_Merman",0x17B5370,0,0,2,64,0,Kind::insect,3,4},
 {"Humanoid_BigGreyBoss",0x17BEC68,0,0,3,64,0,Kind::humanoid,3,4},
 {"UfoCarrier508",0x17C2000,0,0,4,64,0,Kind::flying,3,4},
};
unsigned checks=0,failures=0,hostEntries=0,replayEntries=0,mermanEntries=0,bossNamedEntries=0,bossHandEntries=0,carrierEntries=0,stockReceives=0,stockSends=0,stockNamed=0;unsigned observedMode=0;bool bossBusy=false,handsBusy=false;bool reject=false,online=true,sessionFault=false,registered=false,emittersReady=true;unsigned emitterInstalls=0;
NativeActionExtension extension{};
void Check(bool value,const char* text,int line){++checks;if(!value){++failures;std::fprintf(stderr,"line %d: %s\n",line,text);}}
#define CHECK(x) Check((x),#x,__LINE__)
const wchar_t* names[]={L"attack_breath",L"attack_sniperSpark_vs_big"};
const NativeLease* releaseLease=nullptr;const NativeLease* entryLease=nullptr;
bool releaseFault=false,releaseReentry=false,entryReentry=false;unsigned releases=0;
void __fastcall Release(void*) {
 ++releases;
 if(releaseReentry && releaseLease){CHECK(!Restore(*releaseLease));CHECK(!Abort(*releaseLease));CHECK(!Empty(*releaseLease));}
 if(releaseFault)RaiseException(0xE1234567u,0,0,nullptr);
}
void* controlVtable[]={reinterpret_cast<void*>(&Release),reinterpret_cast<void*>(&Release)};
struct Fixture {
 unsigned char* actor=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 unsigned char* weak=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 unsigned char* rows=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 NativeLease l{};unsigned family;bool authority,bound=true;
 Fixture(unsigned f,bool host):family(f),authority(host) {
  if(!actor || !weak || !rows)std::exit(2);Put<const void*>(actor,0,image+profiles[f].vtableRva);Put<const void*>(actor,0x30,weak);Put<void*>(weak,0,controlVtable);Put<LONG>(weak,8,1);Put<LONG>(weak,12,1);
  l.object=actor;l.control=weak;l.profile=profiles+f;Put<float>(actor,0x698,1.25f);Put<unsigned>(actor,0x8B0,4);Put<unsigned>(actor,0x128,host ? 2u : 1u);
  const unsigned basicMachine=f==0 ? 0x1020u : 0x1B90u;Put<const void*>(actor+basicMachine,0,image+(f==0 ? 0x17C5F90u : 0x17B5358u));Put<const void*>(actor+basicMachine,8,actor+(f==0 ? 0xF80u : 0x1AF0u));Put<const void*>(actor+basicMachine,0x10,actor);
  Put<const void*>(actor+(f==0 ? 0xF80u : 0x1AF0u),8,actor+basicMachine);
  const auto stride=f==0 ? 0x38u : 0x88u;
  for(unsigned i=0;i<2;++i){auto* row=rows+i*stride;Put<const wchar_t*>(row,0,names[i]);if(!f)Put<float>(row,8,1.5f);else {Put<unsigned>(row,8,i);Put<unsigned>(row,0xC,i);row[0x18]=static_cast<unsigned char>(i);Put<int>(row,0x1C,10+static_cast<int>(i));}}
  Put<const void*>(actor,f==0 ? 0x14A8u : 0x16F8u,rows);Put<std::size_t>(actor,f==0 ? 0x14B8u : 0x1708u,2);
  if(f==2) {
   Put<const void*>(actor,0xA70,actor+0x3000);Put<int>(actor,0xEB0,1);actor[0x1090]=1;Put<float>(actor,0x2F8,100);
   for(unsigned which=0;which<2;++which) {
    const unsigned machineOffset=which ? 0x17B0u : 0xE50u,contextOffset=which ? 0x16F0u : 0xD90u;
    auto* machine=actor+machineOffset;auto* context=actor+contextOffset;auto* table=rows+(which ? 0x600u : 0u);const unsigned n=which ? 3u : 1u;
    Put<const void*>(machine,0,image+(which ? 0x17BEC50u : 0x17C29A8u));Put<const void*>(machine,8,context);Put<const void*>(machine,0x28,actor);
    Put<const void*>(context,0x10,table);Put<std::size_t>(context,0x20,n);
    for(unsigned i=0;i<n;++i) {auto* node=rows+(which ? 0x700u : 0x200u)+i*0x40;Put<const void*>(table+i*16,0,node);Put<const void*>(table+i*16,8,weak);
     Put<const void*>(node,8,image+(which ? HandCallback(4+i) : 0x466E7Cu));Put<int>(node,0x10,0);Put<const void*>(node,0x18,actor);}
   }
  }
 }
 ~Fixture(){CHECK(Abort(l));CHECK(At<LONG>(weak,8)==1);VirtualFree(actor,0,MEM_RELEASE);VirtualFree(weak,0,MEM_RELEASE);VirtualFree(rows,0,MEM_RELEASE);}
 NativeAbility Ability(unsigned key=0) const {return {"fixture",L"Fixture",0,0,family==0 ? AbilityKind::monsterAction : AbilityKind::mermanAction,names[key],static_cast<int>(key)};}
 void Bind(){CHECK(Before(l,authority,nullptr));CHECK(At<LONG>(weak,8)==2);}
 void ReadyFor(unsigned key){if(!family)Put<unsigned>(actor,0x8B0,4);else {Put<unsigned>(actor,0x17F4,0);Put<unsigned>(actor,0x17F0,key);}}
};
Fixture* roleFixtures[8]{};
void SetRole(unsigned index,Fixture* f){roleFixtures[index]=f;}
void NativeEntry(unsigned char* a,unsigned key,bool merman) {
 if(entryReentry && entryLease){CHECK(!Restore(*entryLease));CHECK(!Abort(*entryLease));CHECK(!Empty(*entryLease));}
 if(reject)return;CHECK(At<std::uint64_t>(a,0x490)==(At<std::uint64_t>(a,0x3E8)^0xABCDu));
 const auto machine=merman ? 0x1B90u : 0x1020u;Put<const void*>(a+machine,0x18,image+(merman ? 0x3A6ED0u : 0x4BEC00u));Put<int>(a+machine,0x20,0);
 const auto context=At<unsigned char*>(a+machine,8);Put<const void*>(context,8,a+machine);Put<unsigned>(context,0,0);
 if(merman){const auto row=At<unsigned char*>(a,0x16F8)+key*0x88;bool isAuthority=false;CHECK(NativeActionRole(a,&isAuthority));CHECK(At<unsigned>(a,0x128)==(isAuthority ? 2u : 1u));Put<const void*>(a,0x1710,row);a[0x1718]=1;Put<unsigned>(a,0x17F0,At<unsigned>(row,8));Put<unsigned>(a,0x17F4,row[0x18] ? 3u : 7u);++mermanEntries;}
 else {Put<int>(a,0x14D0,static_cast<int>(key));Put<unsigned>(a,0x8B0,0);}
}
void __fastcall MonsterHost(void* object,int key){++hostEntries;auto* a=static_cast<unsigned char*>(object);if(At<unsigned>(a,0x8B0)&4)NativeEntry(a,static_cast<unsigned>(key),false);}
void __fastcall MonsterReplay(void* machine,const Descriptor* descriptor,const void* data){++replayEntries;CHECK(descriptor->callback==image+0x4BEC00 && !descriptor->adjustor);NativeEntry(static_cast<unsigned char*>(machine)-0x1020,static_cast<unsigned>(*static_cast<const int*>(data)),false);}
void __fastcall MermanNative(void* actor,int key,bool selected){CHECK(selected);NativeEntry(static_cast<unsigned char*>(actor),static_cast<unsigned>(key),true);}
bool __fastcall NativeBusy(void*){return bossBusy;}
bool __fastcall HandsBusy(void*){return handsBusy;}
int __fastcall FindCas(const void*,int,const wchar_t*,bool){return 7;}
bool __fastcall PoolGetter(void* value,const Descriptor* desc) {
 auto* m=static_cast<unsigned char*>(value);const auto* node=At<const unsigned char*>(m,0x10);
 return node && At<const void*>(node,8)==desc->callback && At<int>(node,0x10)==desc->adjustor;
}
void PoolEnter(void* value,const Descriptor* desc,const NamedAction* action) {
 if(reject)return;auto* m=static_cast<unsigned char*>(value);auto* a=At<unsigned char*>(m,0x28);auto* context=At<unsigned char*>(m,8);
 const auto* table=At<const unsigned char*>(context,0x10);const auto n=At<std::size_t>(context,0x20);const void* node=nullptr;
 for(std::size_t i=0;i<n;++i){const auto* candidate=At<const unsigned char*>(table+i*16,0);if(At<const void*>(candidate,8)==desc->callback)node=candidate;}
 CHECK(node!=nullptr);if(!node)return;const bool repeat=At<const void*>(m,0x10)==node;Put<unsigned>(m,0x20,repeat ? At<unsigned>(m,0x20)+1 : 0);
 Put<const void*>(m,0x10,node);Put<const void*>(context,0x28,m);
 if(action){CHECK(desc->callback==image+0x466E7C);CHECK(!action->turnTarget);Put<NativeWide>(a,0x1190,action->name);Put<int>(a,0x11B0,action->priority);Put<float>(a,0x11B4,action->blend);Put<float>(a,0x11B8,action->speed);a[0x11BC]=static_cast<unsigned char>(action->turnTarget);++bossNamedEntries;}
 else {observedMode=At<unsigned>(a,0x26C8);++bossHandEntries;}
}
void __fastcall NamedNative(void* value,const NamedAction* action) {
 auto* a=static_cast<unsigned char*>(value);const Descriptor desc{image+0x466E7C};
 if(bossBusy || (PoolGetter(a+0xE50,&desc) && (At<int>(a,0x11B0)>1 || (At<int>(a,0x11B0)==1 && At<float>(a,0x11B4)>action->blend))))return;
 PoolEnter(a+0xE50,&desc,action);
}
void __fastcall NamedReplay(void* m,const Descriptor* d,const NamedAction* a){PoolEnter(m,d,a);}
void __fastcall HandReplay(void* m,const Descriptor* d,const void*){PoolEnter(m,d,nullptr);}
bool __fastcall BossMessage(void* value,unsigned message,void* argument) {
 auto* a=static_cast<unsigned char*>(value);const auto key=static_cast<unsigned>(*static_cast<int*>(argument));CHECK(message==0x1000000D && key>=4 && key<=6);
 if(!bossBusy && !handsBusy && At<float>(a,0x2F8)>0 && !(a[0xEAC]&0x20)){const Descriptor d{image+HandCallback(key)};PoolEnter(a+0x17B0,&d,nullptr);}return false; // original base AL is unrelated to acceptance
}
bool __fastcall CarrierMessage(void* value,unsigned message,void* argument) {
 CHECK(message==0x1000000D && *static_cast<int*>(argument)==64);if(reject)return false;Put<int>(value,0xD88,1);++carrierEntries;return true;
}
void __fastcall Bookmark(void* packet,std::uint64_t* out){*out=At<std::uint64_t>(packet,8);}
unsigned char __fastcall ReadTag(void* packet){const auto cursor=At<std::uint64_t>(packet,8);Put<std::uint64_t>(packet,8,cursor+1);return static_cast<unsigned char*>(packet)[0x10+cursor];}
void __fastcall RestoreBookmark(void* packet,std::uint64_t cursor){Put<std::uint64_t>(packet,8,cursor);}
void __fastcall StockReceiver(void*,void*){++stockReceives;}
void __fastcall StockSender(void*,int,bool){++stockSends;}
void __fastcall StockEntry(void*,signed char,void*){++stockNamed;}
bool __fastcall NativeSession(void*){if(sessionFault)RaiseException(0xE1234567u,0,0,nullptr);return online;}
double __fastcall Rate(const void*){return 1.0;}
bool Run(Fixture& host,unsigned key,std::uint64_t input,std::uint64_t seed=123){host.ReadyFor(key);bool handled=false;const bool result=ExecuteAcceptedCommand(host.l,host.Ability(key),input,seed,&handled);CHECK(handled);return result;}
void TestAccepted() {
 Fixture host(0,true),guest(0,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();bool handled=false;
 const auto before=hostEntries+replayEntries;CHECK(ExecuteAcceptedCommand(guest.l,guest.Ability(),1,42,&handled));CHECK(handled && hostEntries+replayEntries==before);
 CHECK(Run(host,0,10));CHECK(Run(host,1,11,456));CHECK(hostEntries==before+2);CHECK(!Empty(host.l));CHECK(!Restore(host.l));
 unsigned char packet[512]{};std::size_t used=0;CHECK(!Capture(host.l,packet,64,&used));CHECK(!used && Find(host.l)->count==2);
 CHECK(Capture(host.l,packet,sizeof(packet),&used));CHECK(used==kHeader+2*kEventBytes);CHECK(Empty(host.l));
 Put<unsigned>(guest.actor,0x8B0,0);Put<int>(guest.actor,0x14D0,1);CHECK(Apply(guest.l,packet,used)==Result::applied);CHECK(replayEntries==2);
 CHECK(At<int>(guest.actor,0x14D0)==1);CHECK(At<float>(guest.actor,0x698)==1.25f);CHECK(Apply(guest.l,packet,used)==Result::applied && replayEntries==2);
 packet[20]^=1;CHECK(Apply(guest.l,packet,used)==Result::failed && replayEntries==2);packet[20]^=1;
 CHECK(Restore(host.l));CHECK(Restore(guest.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
void TestMerman() {
 Fixture host(1,true),guest(1,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();const auto entered=mermanEntries;
 CHECK(Run(host,0,21));CHECK(At<unsigned>(host.actor,0x128)==2);CHECK(Run(host,1,22));
 unsigned char packet[512]{};std::size_t used=0;CHECK(Capture(host.l,packet,sizeof(packet),&used));Put<unsigned>(guest.actor,0x17F0,4);Put<unsigned>(guest.actor,0x17F4,7);
 CHECK(Apply(guest.l,packet,used)==Result::applied);CHECK(At<unsigned>(guest.actor,0x17F0)==1 && At<unsigned>(guest.actor,0x128)==1);CHECK(mermanEntries==entered+4);
 CHECK(Apply(guest.l,packet,used)==Result::applied && mermanEntries==entered+4);CHECK(Restore(host.l));CHECK(Restore(guest.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
void TestRejectAndClose() {
 Fixture host(0,true),guest(0,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();bool handled=false;Put<unsigned>(host.actor,0x8B0,0);
 Put<int>(host.actor,0x14D0,0);const auto entries=hostEntries;CHECK(!ExecuteAcceptedCommand(host.l,host.Ability(),31,123,&handled));CHECK(handled && Empty(host.l) && hostEntries==entries);
 host.ReadyFor(0);CHECK(!ExecuteAcceptedCommand(host.l,host.Ability(),31,123,&handled));CHECK(Empty(host.l));
 for(unsigned i=0;i<kQueued;++i)CHECK(Run(host,i%2,100+i));const auto fullEntries=hostEntries;CHECK(!Run(host,0,300));CHECK(hostEntries==fullEntries);
 CHECK(!Restore(host.l));CHECK(Quiesce(host.l));CHECK(!Run(host,0,301));CHECK(!Empty(host.l));
 unsigned char packet[512]{};std::size_t used=0;while(!Empty(host.l)){CHECK(Capture(host.l,packet,sizeof(packet),&used));CHECK(Apply(guest.l,packet,used)==Result::applied);}
 CHECK(Restore(host.l));CHECK(Restore(guest.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
void TestPreflightAndFailure() {
 Fixture host(0,true),guest(0,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();CHECK(Run(host,0,400));CHECK(Run(host,1,401));
 unsigned char packet[512]{};std::size_t used=0;CHECK(Capture(host.l,packet,sizeof(packet),&used));const auto old=replayEntries;
 packet[kHeader+kEventBytes+21]^=1;CHECK(Apply(guest.l,packet,used)==Result::failed && replayEntries==old && !Find(guest.l)->applied);packet[kHeader+kEventBytes+21]^=1;
 Event events[kBatch]{};unsigned count=0;CHECK(Decode(packet,used,events,&count));events[0].serial=3;events[1].serial=4;CHECK(Encode(events,count,packet,sizeof(packet),&used));
 CHECK(Apply(guest.l,packet,used)==Result::pending && replayEntries==old);events[0].serial=1;events[1].serial=2;CHECK(Encode(events,count,packet,sizeof(packet),&used));
 reject=true;CHECK(Apply(guest.l,packet,used)==Result::failed);reject=false;CHECK(Find(guest.l)->faulted && !Find(guest.l)->applied);
 CHECK(Quiesce(host.l));CHECK(Restore(host.l));CHECK(Abort(guest.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
void TestCodecAndThroughput() {
 unsigned char bytes[512]{};Event in[kBatch]{},out[kBatch]{};std::size_t used=0;unsigned count=0;
 for(unsigned i=0;i<1000;++i){const unsigned n=1+i%kBatch;for(unsigned j=0;j<n;++j){auto& e=in[j];e={};e.operation=(i%2) ? Operation::monster : Operation::merman;e.key=(i+j)%4096;e.serial=1+j;e.input=5+i+j;e.definition=12345+i;e.seed3=i*45ull;e.seed4=i^0x5555u;e.scale=1.0f+static_cast<float>(i%10);}
  CHECK(Encode(in,n,bytes,sizeof(bytes),&used));CHECK(used<=512);CHECK(Decode(bytes,used,out,&count) && count==n);for(unsigned j=0;j<n;++j)CHECK(Hash(in[j])==Hash(out[j]));
  CHECK(!Decode(bytes,used-1,out,&count));bytes[6]=1;CHECK(!Decode(bytes,used,out,&count));bytes[6]=0;
 }
 Fixture host(0,true),guest(0,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();const auto begin=replayEntries;
 for(unsigned frame=1;frame<=600;++frame){CHECK(Run(host,frame%2,1000+frame,frame));if(frame%15==0)while(!Empty(host.l)){CHECK(Capture(host.l,bytes,sizeof(bytes),&used));CHECK(Apply(guest.l,bytes,used)==Result::applied);}}
 CHECK(replayEntries==begin+600);CHECK(Empty(host.l));CHECK(Restore(host.l));CHECK(Restore(guest.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
void TestSameInputAbilities() {
 Fixture host(1,true),guest(1,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();bool handled=false;
 host.ReadyFor(1);CHECK(!ExecuteAcceptedCommand(host.l,host.Ability(0),9000,123,&handled));CHECK(handled);
 CHECK(ExecuteAcceptedCommand(host.l,host.Ability(1),9000,123,&handled));CHECK(Find(host.l)->count==1);const auto entered=mermanEntries;
 CHECK(ExecuteAcceptedCommand(host.l,host.Ability(1),9000,123,&handled) && mermanEntries==entered);CHECK(!ExecuteAcceptedCommand(host.l,host.Ability(1),9000,124,&handled));
 unsigned char bytes[512]{};std::size_t used=0;CHECK(Capture(host.l,bytes,sizeof(bytes),&used));CHECK(Apply(guest.l,bytes,used)==Result::applied);
 CHECK(Restore(host.l));CHECK(Restore(guest.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
NativeAbility BossAbility(unsigned key,bool hand=false){return {"boss_fixture",L"Boss",0,0,hand ? AbilityKind::objectAction : AbilityKind::namedAction,hand ? nullptr : kBossNames[key],hand ? static_cast<int>(key) : 0};}
void TestBossAndCarrier() {
 Fixture host(2,true),guest(2,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();bool handled=false;
 const auto started=bossNamedEntries;for(unsigned i=0;i<5;++i)CHECK(ExecuteAcceptedCommand(host.l,BossAbility(i),10000+i,100+i,&handled) && handled);
 unsigned char bytes[512]{};std::size_t used=0;CHECK(Capture(host.l,bytes,sizeof(bytes),&used));
 bossBusy=true;Put<unsigned>(guest.actor,0x10F0,5);guest.actor[0x11F2]=1;CHECK(Apply(guest.l,bytes,used)==Result::applied);CHECK(bossNamedEntries==started+10);
 CHECK(!guest.actor[0x11BC] && NamedString(guest.l,kBossNames[4]));const auto committed=bossNamedEntries;CHECK(Apply(guest.l,bytes,used)==Result::applied && bossNamedEntries==committed);
 bossBusy=false;Put<int>(host.actor,0x11B0,2);CHECK(!ExecuteAcceptedCommand(host.l,BossAbility(4),10005,105,&handled));CHECK(Empty(host.l) && bossNamedEntries==committed);
 Put<int>(host.actor,0x11B0,1);Put<float>(host.actor,0x11B4,0.1f);CHECK(ExecuteAcceptedCommand(host.l,BossAbility(4),10006,106,&handled));CHECK(bossNamedEntries==committed+1);
 CHECK(Capture(host.l,bytes,sizeof(bytes),&used));CHECK(Apply(guest.l,bytes,used)==Result::applied);CHECK(bossNamedEntries==committed+2);
 for(unsigned i=4;i<=6;++i){Put<unsigned>(host.actor,0x26C8,0x70000000u+i);CHECK(ExecuteAcceptedCommand(host.l,BossAbility(i,true),10100+i,800+i,&handled));CHECK(observedMode==0x70000000u+i);}
 CHECK(Capture(host.l,bytes,sizeof(bytes),&used));
 {Event malformed[kBatch]{};unsigned n=0;CHECK(Decode(bytes,used,malformed,&n));malformed[0].mode=0x80000000u;std::size_t ignored=0;CHECK(!Encode(malformed,n,bytes,sizeof(bytes),&ignored));
  const unsigned offset=kHeader+49;unsigned char saved[4]{};std::memcpy(saved,bytes+offset,4);const std::uint32_t illegal=0x80000000u;std::memcpy(bytes+offset,&illegal,4);
  const auto nativeBefore=bossHandEntries;CHECK(!Decode(bytes,used,malformed,&n));CHECK(Apply(guest.l,bytes,used)==Result::failed && bossHandEntries==nativeBefore);std::memcpy(bytes+offset,saved,4);}
 Put<unsigned>(guest.actor,0x26C8,77);handsBusy=true;bossBusy=true;
 CHECK(Apply(guest.l,bytes,used)==Result::applied);CHECK(observedMode==0x70000006u && At<unsigned>(guest.actor,0x26C8)==77);CHECK(bossHandEntries>=6);
 handsBusy=bossBusy=false;CHECK(Restore(host.l));CHECK(Restore(guest.l));SetRole(0,nullptr);SetRole(1,nullptr);
 {Fixture carrier(3,true),remote(3,false);SetRole(0,&carrier);SetRole(1,&remote);carrier.Bind();remote.Bind();NativeAbility a{"carrier_fixture",L"Carrier",0,0,AbilityKind::objectAction,nullptr,64};
  const auto first=carrierEntries;CHECK(ExecuteAcceptedCommand(carrier.l,a,11000,1,&handled));CHECK(ExecuteAcceptedCommand(carrier.l,a,11001,2,&handled));CHECK(At<int>(carrier.actor,0xD88)==1);
  CHECK(Capture(carrier.l,bytes,sizeof(bytes),&used));CHECK(Apply(remote.l,bytes,used)==Result::applied);CHECK(At<int>(remote.actor,0xD88)==1 && carrierEntries==first+4);
  CHECK(Apply(remote.l,bytes,used)==Result::applied && carrierEntries==first+4);CHECK(Restore(carrier.l));CHECK(Restore(remote.l));SetRole(0,nullptr);SetRole(1,nullptr);
 }
}
void TestStockIsolation() {
 {Fixture merman(1,false);SetRole(0,&merman);merman.Bind();alignas(16) unsigned char packet[0x80]{};Put<std::uint64_t>(packet,8,3);packet[0x13]=6;
  const auto received=stockReceives,sent=stockSends;MermanReceiver(merman.actor+0x120,packet);MermanSender(merman.actor,0,true);CHECK(stockReceives==received && stockSends==sent && At<std::uint64_t>(packet,8)==3);
  packet[0x13]=4;MermanReceiver(merman.actor+0x120,packet);CHECK(stockReceives==received+1 && At<std::uint64_t>(packet,8)==3);
  CHECK(Quiesce(merman.l));CHECK(Restore(merman.l));CHECK(!Find(merman.l));packet[0x13]=6;MermanReceiver(merman.actor+0x120,packet);MermanSender(merman.actor,0,true);
  CHECK(stockReceives==received+1 && stockSends==sent);merman.bound=false;MermanReceiver(merman.actor+0x120,packet);MermanSender(merman.actor,0,true);CHECK(stockReceives==received+2 && stockSends==sent+1);SetRole(0,nullptr);
 }
 {Fixture boss(2,false);SetRole(0,&boss);boss.Bind();const auto entered=stockNamed;StockNamedEntry(boss.actor+0xE50,0,nullptr);CHECK(stockNamed==entered);
  StockNamedEntry(boss.actor+0xE50,-1,nullptr);CHECK(stockNamed==entered+1);CHECK(Restore(boss.l));StockNamedEntry(boss.actor+0xE50,0,nullptr);CHECK(stockNamed==entered+1);
  void* cloned[2]={const_cast<void*>(At<const void*>(image+0x17BEC68-sizeof(void*),0)),nullptr};Put<const void*>(boss.actor,0,cloned+1);StockNamedEntry(boss.actor+0xE50,0,nullptr);CHECK(stockNamed==entered+1);
  boss.bound=false;StockNamedEntry(boss.actor+0xE50,0,nullptr);CHECK(stockNamed==entered+2);SetRole(0,nullptr);
 }
}
void TestRelayOwnership() {
 const unsigned sites[]={0x476530,0x3A3A29,0x3A6235,0x3A6587,0x3A67F0};
 for(const auto site:sites) {
  std::int32_t delta=0;std::memcpy(&delta,image+site+1,4);auto* thunk=image+site+5+delta;
  thunk[2]^=1;CHECK(!Ready());CHECK(!InstallCommandAcceptedActions());thunk[2]^=1;CHECK(Ready());
  thunk[10]^=1;CHECK(!Ready());thunk[10]^=1;CHECK(Ready());
  image[site+4]^=1;CHECK(!Ready());image[site+4]^=1;CHECK(Ready());
 }
 auto** receiver=reinterpret_cast<void**>(image+0x17B55A8);auto* original=*receiver;*receiver=image+0x3A6410;
 CHECK(!Ready());*receiver=original;CHECK(Ready());
}
void TestRootReleaseAndReentry() {
 for(unsigned scenario=0;scenario<4;++scenario) {
  Fixture host(0,true);SetRole(0,&host);host.Bind();const auto before=releases;
  if(scenario==0) {
   DWORD old=0;CHECK(VirtualProtect(host.weak,0x1000,PAGE_READONLY,&old)!=0);Sleep(20);
   CHECK(!Restore(host.l));CHECK(Find(host.l) && Find(host.l)->rootHeld && !Find(host.l)->cleanupUncertain);
   DWORD ignored=0;CHECK(VirtualProtect(host.weak,0x1000,old,&ignored)!=0);Sleep(20);
   CHECK(Restore(host.l));CHECK(releases==before && At<LONG>(host.weak,8)==1);
  } else if(scenario<3) {
   // Simulate the game releasing its original strong owner. Our retained root
   // becomes the last reference, so Restore really enters both native dtors.
   CHECK(InterlockedDecrement(reinterpret_cast<volatile LONG*>(host.weak+8))==1);
   releaseLease=&host.l;releaseReentry=scenario==1;releaseFault=scenario==2;
   if(scenario==1) {
    CHECK(Restore(host.l));CHECK(releases==before+2 && At<LONG>(host.weak,8)==0);CHECK(!Find(host.l));
   } else {
    CHECK(!Restore(host.l));CHECK(releases==before+1 && At<LONG>(host.weak,8)==0);
    auto* stream=Find(host.l);CHECK(stream && !stream->rootHeld && stream->cleanupUncertain && !stream->cleanupEntered);
    CHECK(!Restore(host.l));CHECK(!Abort(host.l));CHECK(!Empty(host.l));CHECK(releases==before+1);
    // Test-only disposal of the fake terminal native exception. Production
    // retains the identity/journal and never retries the consumed decrement.
    if(stream)*stream={};
   }
   releaseFault=releaseReentry=false;releaseLease=nullptr;Put<LONG>(host.weak,8,1);Put<LONG>(host.weak,12,1);
  } else {
   // Restore/Abort called by a native state-entry callback cannot release its
   // owner or erase an accepted event while Execute is still publishing it.
   entryLease=&host.l;entryReentry=true;CHECK(Run(host,0,1001));entryReentry=false;entryLease=nullptr;
   CHECK(Find(host.l) && Find(host.l)->quiescing && Find(host.l)->count==1 && !Find(host.l)->nativeCalls);
   CHECK(!Run(host,0,1002));unsigned char bytes[512]{};std::size_t used=0;
   CHECK(Capture(host.l,bytes,sizeof(bytes),&used) && used>0);CHECK(Restore(host.l));CHECK(releases==before);
  }
  SetRole(0,nullptr);
 }
 Fixture host(0,true),guest(0,false);SetRole(0,&host);SetRole(1,&guest);host.Bind();guest.Bind();CHECK(Run(host,0,2001));
 unsigned char bytes[512]{};std::size_t used=0;CHECK(Capture(host.l,bytes,sizeof(bytes),&used));
 entryLease=&guest.l;entryReentry=true;CHECK(Apply(guest.l,bytes,used)==Result::applied);entryReentry=false;entryLease=nullptr;
 CHECK(Find(guest.l) && Find(guest.l)->quiescing && !Find(guest.l)->nativeCalls);
 CHECK(Apply(guest.l,bytes,used)==Result::applied);CHECK(Restore(host.l));CHECK(Restore(guest.l));SetRole(0,nullptr);SetRole(1,nullptr);
}
void TestOfflineAndIdentity() {
 Fixture host(0,true);SetRole(0,&host);host.bound=false;bool handled=true;online=false;CHECK(ExecuteAcceptedCommand(host.l,host.Ability(),1,2,&handled) && !handled);
 online=true;CHECK(!ExecuteAcceptedCommand(host.l,host.Ability(),1,2,&handled) && handled);sessionFault=true;CHECK(!ExecuteAcceptedCommand(host.l,host.Ability(),1,2,&handled) && handled);sessionFault=false;
 host.bound=true;host.Bind();CHECK(Run(host,0,44));Put<const void*>(host.actor,0x30,host.rows);unsigned char bytes[512]{};std::size_t used=0;CHECK(!Capture(host.l,bytes,sizeof(bytes),&used));Put<const void*>(host.actor,0x30,host.weak);
 CHECK(Quiesce(host.l));host.actor[0x18]|=4;CHECK(Capture(host.l,bytes,sizeof(bytes),&used));CHECK(used>0);CHECK(Restore(host.l));SetRole(0,nullptr);
}
}
namespace crew {bool EnemyControlNetworkContext(const void*,bool* result) noexcept {if(!result || sessionFault)return false;*result=online;return true;}}
namespace crew::enemy_native {
bool InstallMonsterAcceptedProjectiles() noexcept{++emitterInstalls;return emittersReady;}
bool InstallMermanAcceptedProjectiles() noexcept{++emitterInstalls;return emittersReady;}
bool InstallBossGunAcceptedProjectiles() noexcept{++emitterInstalls;return emittersReady;}
const NativeProfile* Profiles(std::size_t* count) noexcept{*count=sizeof(profiles)/sizeof(profiles[0]);return profiles;}
bool RegisterNativeActionExtension(const NativeActionExtension& e) noexcept{extension=e;registered=true;return true;}
bool NativeActionRole(const void* root,bool* authority) noexcept {for(auto* f:roleFixtures)if(f && f->bound && f->actor==root && At<const void*>(f->actor,0x30)==f->weak){*authority=f->authority;return true;}return false;}
bool SetSeed(const NativeLease& l,std::uint64_t seed) noexcept{Put<std::uint64_t>(l.object,0x3E8,seed);Put<std::uint64_t>(l.object,0x490,seed^0xABCDu);return true;}
void* CommandActionsTestFunction(unsigned rva) noexcept {
 switch(rva){case 0x7748F0:return reinterpret_cast<void*>(&NativeSession);case 0x4B7AC0:return reinterpret_cast<void*>(&MonsterHost);case 0x4BE500:return reinterpret_cast<void*>(&MonsterReplay);case 0x3A6710:return reinterpret_cast<void*>(&MermanNative);case 0x52F50:return reinterpret_cast<void*>(&Rate);case 0x47D940:return reinterpret_cast<void*>(&NativeBusy);case 0x4448E0:return reinterpret_cast<void*>(&HandsBusy);case 0x11664F0:return reinterpret_cast<void*>(&FindCas);
 case 0x46D5E0:case 0x444770:return reinterpret_cast<void*>(&PoolGetter);case 0x475BE0:return reinterpret_cast<void*>(&NamedNative);case 0x446D90:return reinterpret_cast<void*>(&NamedReplay);case 0x4471A0:return reinterpret_cast<void*>(&HandReplay);
 case 0x444C10:return reinterpret_cast<void*>(&BossMessage);case 0x4F6FB0:return reinterpret_cast<void*>(&CarrierMessage);case 0x12B48A0:return reinterpret_cast<void*>(&Bookmark);case 0x12B4BB0:return reinterpret_cast<void*>(&ReadTag);case 0x12B4FD0:return reinterpret_cast<void*>(&RestoreBookmark);
 case 0x3A6410:return reinterpret_cast<void*>(&StockReceiver);case 0x3A5DA0:return reinterpret_cast<void*>(&StockSender);case 0x4775E0:return reinterpret_cast<void*>(&StockEntry);default:std::abort();}
}
}
int main(){image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!image)return 2;
 CHECK(!InstallCommandAcceptedActions());for(const auto& sig:kSigs)std::memcpy(image+sig.rva,sig.bytes,16);Put<const void*>(image,0x17B55A8,image+0x3A6410);const unsigned char stockCall[]={0xE8,0xAB,0x10,0,0};std::memcpy(image+0x476530,stockCall,5);const unsigned sendSites[]={0x3A3A29,0x3A6235,0x3A6587,0x3A67F0};for(const auto site:sendSites){image[site]=0xE8;const auto disp=static_cast<std::int32_t>(0x3A5DA0-site-5);std::memcpy(image+site+1,&disp,4);}for(const auto& profile:profiles)Put<const void*>(image+profile.vtableRva-sizeof(void*),0,image+profile.vtableRva-0x100);emittersReady=false;CHECK(!InstallCommandAcceptedActions() && !registered);emittersReady=true;CHECK(InstallCommandAcceptedActions() && registered && extension.tag==kTag && emitterInstalls>=4);
 TestRelayOwnership();TestAccepted();TestMerman();TestRejectAndClose();TestPreflightAndFailure();TestCodecAndThroughput();TestSameInputAbilities();TestBossAndCarrier();TestStockIsolation();TestRootReleaseAndReentry();TestOfflineAndIdentity();
 VirtualFree(image,0,MEM_RELEASE);std::printf("%u checks, %u failures\n",checks,failures);return failures ? 1 : 0;
}
