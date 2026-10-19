// Production Walking accepted codec/transaction fixture, entirely private memory.
// Optional flow-control evidence: --dump-trace <jsonl> reads committed numeric
// provenance in tests/data; --trace-assets can select newly extracted evidence.
// Only literal source cadences produce finite-burst traces; VM cadences retain
// raw evidence without selecting a difficulty or inventing a repeated cycle.
#define ENEMY_CONTROL_WALKING_NET_TEST
#include "../src/enemy_control_walking_parts.h"
#include "../src/enemy_control_walking_net.cpp"
#include <cstdio>
#include <limits>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <filesystem>
namespace crew {unsigned char* image=nullptr;}
namespace {
using namespace crew;
using namespace crew::enemy_native;
using namespace crew::enemy_native::walking_net;
unsigned nativeStops=0,nativeCacheDeletes=0;bool throwStop=false,throwCacheDelete=false,reenterStop=false,reenteredBlocked=false;
bool swapOnStop=false,swapOnDelete=false;unsigned char* callbackCache=nullptr;void* callbackTrigger=nullptr;
unsigned checks=0,failures=0,commits=0,gunApplies=0,replays=0,restores=0,originalCreates=0,beamRefreshes=0,beamSteps=0,deleted=0,freeCalls=0;
bool roles[2]{true,true},quiescing[2]{},contexts[2]{true,true},online[2]{true,true},actionReady[2]{true,true},actionDefined[2]{true,true};
bool aimAvailable=true,restoreGuns=true,replayReady=true,refuseReplay=false,journalsEmpty=true,restoreJournals=true,dispatchReady=true,refuseCreate=false,beamMode=false,partsEmpty=true,throwDelete=false,throwFree=false,createdCleanupFailure=false;
NativeProjectileSample lastReplay{};std::vector<void*> allocations;
unsigned committed[2]{4096,4096},gunCount=2;float canonical[3]{100,200,300},seenPoint[3]{};
NativeGunFactoryProvider registeredGun{};NativeUnitFactoryProvider registeredUnit{};NativeBeamRefreshProvider registeredBeam{};
NativeActionExtension registeredExtension{};NativeBeamStepProvider registeredStep{};
struct Actor {alignas(16) unsigned char object[0x2100]{},control[32]{};};
struct GunMemory {alignas(16) unsigned char gun[0xA0]{},battery[0x6D0]{},muzzles[2][0xD0]{},bullet[0xC50]{},bulletControl[32]{},head[32]{};};
Actor actors[3],results[2];GunMemory guns[2][4];bool gunEnabled[4]{true,true,true,true};NativeLease leases[2]{};
void* controlTable[2]{};void* beamTable[13]{};
constexpr NativeProfile profiles[]={
 {"EDF6_Berserker_Large",0x17ADA78,0,0,1,86,0x31,Kind::walking,7,4},
 {"EDF6_Berserker_Middle",0x17AE088,0,0,2,86,0x31,Kind::walking,7,4},
 {"EDF6_Berserker_TypeA",0x17AE968,0,0,3,86,0x31,Kind::walking,7,4},
 {"EDF6_Berserker_TypeShooter",0x17AF9F8,0,0,4,86,0x31,Kind::walking,7,4}
};
void Check(bool pass,const char* what,int line){++checks;if(!pass){++failures;std::printf("FAIL %d %s\n",line,what);}}
#define CHECK(value) Check((value),#value,__LINE__)
void __fastcall Destroy(void*){}
int Index(const void* root){for(int i=0;i<2;++i)if(root==actors[i].object)return i;return -1;}
unsigned char* Gun(unsigned actor,unsigned ordinal){return guns[actor][ordinal].gun+0x10;}
unsigned char* Battery(unsigned actor,unsigned ordinal){return guns[actor][ordinal].battery+0x10;}
void IdentityMatrix(float* m){std::memset(m,0,64);m[0]=m[5]=m[10]=m[15]=1;}
void Weak(void* pointer){Put<void*>(pointer,0,controlTable);Put<LONG>(pointer,8,1);Put<LONG>(pointer,12,1);}
void Reset(const NativeProfile* profile=profiles){
 journalsEmpty=true;restoreJournals=true;restoreGuns=true;
 throwStop=throwCacheDelete=swapOnStop=swapOnDelete=false;callbackCache=nullptr;callbackTrigger=nullptr;
 for(auto& stream:streams)if(stream.rootHeld){CHECK(TickBeams(stream,true));CHECK(Release(stream));}
 for(void* node:allocations)if(node)std::free(node);allocations.clear();
 std::memset(actors,0,sizeof(actors));std::memset(results,0,sizeof(results));std::memset(guns,0,sizeof(guns));
 controlTable[0]=reinterpret_cast<void*>(&Destroy);controlTable[1]=reinterpret_cast<void*>(&Destroy);
 for(unsigned i=0;i<3;++i){Weak(actors[i].control);Put<void*>(actors[i].object,0x28,actors[i].object);Put<void*>(actors[i].object,0x30,actors[i].control);}
 for(unsigned i=0;i<2;++i){
  roles[i]=true;quiescing[i]=false;contexts[i]=online[i]=true;actionReady[i]=true;actionDefined[i]=true;committed[i]=4096;
  leases[i]={};leases[i].object=actors[i].object;leases[i].control=actors[i].control;leases[i].profile=profile;Put<void*>(actors[i].object,0,image+profile->vtableRva);Put<void*>(image+profile->vtableRva-8,0,image+0x1000+static_cast<unsigned>(profile-profiles)*16);
  Weak(results[i].control);Put<void*>(results[i].object,0x28,results[i].object);Put<void*>(results[i].object,0x30,results[i].control);
  for(unsigned j=0;j<4;++j){auto& mem=guns[i][j];Weak(mem.gun);Weak(mem.battery);Weak(mem.bulletControl);
   Put<void*>(Gun(i,j),0,Battery(i,j));Put<void*>(Gun(i,j),8,mem.battery);Put<void*>(Gun(i,j),0x10,actors[i].object);Put<int>(Gun(i,j),0x38,-1);
   Put<void*>(Battery(i,j),0x2B0,actors[i].object);Put<void*>(Battery(i,j),0x2B8,actors[i].control);
   Put<void*>(Battery(i,j),0x278,mem.muzzles);Put<std::size_t>(Battery(i,j),0x280,2);Put<std::size_t>(Battery(i,j),0x288,2);
   Put<void*>(Battery(i,j),0x2F0,image+0x1000);Put<void*>(Battery(i,j),0x290,mem.head);Put<void*>(mem.head,0,mem.head);Put<void*>(mem.head,8,mem.head);
   for(unsigned m=0;m<2;++m){IdentityMatrix(reinterpret_cast<float*>(mem.muzzles[m]+0x30));IdentityMatrix(reinterpret_cast<float*>(mem.muzzles[m]+0x70));}
  }
  Put<void*>(actors[i].object,0x1A60,Battery(i,0));Put<void*>(actors[i].object,0x1A68,guns[i][0].battery);
 }
 for(bool& enabled:gunEnabled)enabled=true;aimAvailable=true;replayReady=true;refuseReplay=false;gunCount=2;canonical[0]=100;canonical[1]=200;canonical[2]=300;
 nativeStops=nativeCacheDeletes=0;reenterStop=reenteredBlocked=false;
 originalCreates=beamRefreshes=beamSteps=deleted=freeCalls=0;refuseCreate=beamMode=false;partsEmpty=true;throwDelete=throwFree=createdCleanupFailure=false;for(void*& slot:beamTable)slot=nullptr;beamTable[12]=image+0x22F390;for(auto& result:results)Put<void*>(result.object,0,beamTable);
 commits=gunApplies=replays=restores=0;std::memset(seenPoint,0,sizeof(seenPoint));installed=true;admittedImage=image;
}
void Prepare(bool large=true){Reset(large ? profiles : profiles+2);Input input{};CHECK(Before(leases[0],true,&input));CHECK(Before(leases[1],false,&input));}
Event Action(std::uint64_t serial=1,unsigned key=0){Event e{};e.kind=EventKind::action;e.serial=serial;e.key=key;e.digest=0xA000+key;e.command=100+serial;e.seed3=31;e.seed4=41;e.phase=0xA2;std::memcpy(e.point,canonical,12);return e;}
Event GunEvent(std::uint64_t serial=1,unsigned key=0){Event e{};e.kind=EventKind::gun;e.serial=serial;e.key=key;e.digest=0xB000+key;e.command=100+serial;e.seed3=51;e.gunRng=61;e.delay=17;std::memcpy(e.point,canonical,12);for(unsigned i=0;i<12;++i)e.aim[i]=static_cast<float>(i+1);return e;}
Event Shot(std::uint64_t serial=1){Event e{};e.kind=EventKind::shot;e.serial=serial;e.key=0;e.digest=0xB000;e.muzzle=1;e.hasMuzzle=true;e.resourceMask=3;e.value88=1;IdentityMatrix(e.matrix);IdentityMatrix(e.muzzleMatrix);e.matrix[12]=12;e.muzzleMatrix[13]=13;e.random=0x12345678;e.shot=71;e.flags=0xA010;for(unsigned i=0;i<kWords;++i)e.scalar[i]=0x100+i;for(unsigned i=0;i<kBytes;++i)e.bytes[i]=static_cast<unsigned char>(i+1);e.scalar178=0xABCD;e.target[0]=4;e.extra[0]=5;e.lock=2;return e;}
std::size_t Wire(const Event* events,unsigned count,unsigned char (&wire)[512]){std::size_t used=0;CHECK(Encode(events,count,wire,sizeof(wire),&used));return used;}
void Codec(){
 unsigned char wire[512]{};Event events[4]={Action(),GunEvent(2),Shot(3),{}};events[3].kind=EventKind::refresh;events[3].serial=4;events[3].key=1;events[3].digest=0xB001;events[3].muzzle=0;events[3].command=44;IdentityMatrix(events[3].muzzleMatrix);
 for(const auto& event:events){std::size_t used=0;CHECK(Encode(&event,1,wire,sizeof(wire),&used));CHECK(used==8+Bytes(event));Event decoded[8]{};unsigned count=9;CHECK(Decode(wire,used,decoded,&count)&&count==1);CHECK(Hash(decoded[0])==Hash(event));for(std::size_t prefix=0;prefix<used;++prefix){decoded[0].command=0xDEADBEEF;count=99;CHECK(!Decode(wire,prefix,decoded,&count)&&count==0&&decoded[0].command==0xDEADBEEF);}}
 const Event original=Action();const auto length=Wire(&original,1,wire);
 CHECK(length==64 && wire[0]=='W' && wire[1]=='L' && wire[2]=='K' && wire[3]=='1' && wire[4]==2 && wire[5]==1 && wire[6]==0 && wire[7]==0);
 CHECK(wire[8]==1 && wire[9]==1 && wire[17]==0 && wire[18]==0 && wire[19]==0 && wire[20]==0xA0 && wire[63]==0xA2);
 CHECK(wire[27]==101 && wire[35]==31 && wire[43]==41 && wire[51]==0 && wire[52]==0 && wire[53]==0xC8 && wire[54]==0x42);
 {unsigned count=0;Event decoded[8]{};unsigned char bad[512]{};std::memcpy(bad,wire,length);std::memset(bad+9,0,8);CHECK(!Decode(bad,length,decoded,&count));std::memcpy(bad,wire,length);std::memset(bad+19,0,8);CHECK(!Decode(bad,length,decoded,&count));std::memcpy(bad,wire,length);std::memset(bad+27,0,8);CHECK(!Decode(bad,length,decoded,&count));std::memcpy(bad,wire,length);bad[18]=0x10;CHECK(!Decode(bad,length,decoded,&count));}
 Event untouched[8]{};untouched[0].command=0xDEADBEEF;
 for(std::size_t size=0;size<length;++size){unsigned count=7;CHECK(!Decode(wire,size,untouched,&count)&&count==0&&untouched[0].command==0xDEADBEEF);}
 unsigned count=0;wire[length]=0;CHECK(!Decode(wire,length+1,untouched,&count));
 const unsigned headerOffsets[]={0,4,5,6,7,8};for(unsigned offset:headerOffsets){const auto old=wire[offset];wire[offset]=0xFF;CHECK(!Decode(wire,length,untouched,&count));wire[offset]=old;}
 Event pair[2]={Action(1),Action(2,1)};auto pairLength=Wire(pair,2,wire);CHECK(Decode(wire,pairLength,untouched,&count)&&count==2);
 wire[8+Bytes(pair[0])+1]=1;CHECK(!Decode(wire,pairLength,untouched,&count));pairLength=Wire(pair,2,wire);wire[8+Bytes(pair[0])]=0xFF;untouched[0].command=0xDEADBEEF;untouched[1].command=0xDEADBEEF;CHECK(!Decode(wire,pairLength,untouched,&count)&&count==0&&untouched[0].command==0xDEADBEEF&&untouched[1].command==0xDEADBEEF);
 Event invalid=Shot();invalid.resourceMask=4;std::size_t used=9;CHECK(!Encode(&invalid,1,wire,512,&used)&&used==0);
 invalid=Shot();invalid.matrix[15]=0;CHECK(!Encode(&invalid,1,wire,512,&used));invalid=Shot();invalid.matrix[1]=std::numeric_limits<float>::quiet_NaN();CHECK(!Encode(&invalid,1,wire,512,&used));
 {Event sample=Shot();const auto bytes=Wire(&sample,1,wire);wire[29]|=0x10;CHECK(!Decode(wire,bytes,untouched,&count));}
 invalid=GunEvent();invalid.phase=2;CHECK(!Encode(&invalid,1,wire,512,&used));invalid=GunEvent();invalid.delay=-1;CHECK(!Encode(&invalid,1,wire,512,&used));invalid.phase=1;CHECK(Encode(&invalid,1,wire,512,&used));
 Event unsafe=Shot();const float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(unsafe.scalar+6,&nan,4);CHECK(!Encode(&unsafe,1,wire,512,&used));unsafe=Shot();const float two=2;std::memcpy(unsafe.scalar+6,&two,4);unsafe.matrix[10]=(std::numeric_limits<float>::max)();CHECK(!Encode(&unsafe,1,wire,512,&used));unsafe=Shot();std::memcpy(unsafe.scalar+1,&nan,4);CHECK(Encode(&unsafe,1,wire,512,&used));unsafe=Shot();used=Wire(&unsafe,1,wire);std::memcpy(wire+183,&nan,4);CHECK(!Decode(wire,used,untouched,&count));
 Event many[8]{};for(unsigned i=0;i<8;++i)many[i]=Action(i+1);CHECK(Encode(many,8,wire,512,&used)&&used==456);CHECK(!Encode(many,8,wire,455,&used)&&used==0);
 CHECK(!Encode(many,0,wire,512,&used));CHECK(!Encode(many,9,wire,512,&used));CHECK(!OwnsWalkingAcceptedAction(nullptr,0));CHECK(OwnsWalkingAcceptedAction(wire,4));
}
void QueueAndApply(){
 Prepare();CHECK(At<LONG>(actors[0].control,8)==2 && At<LONG>(guns[0][0].gun,8)==2 && At<LONG>(guns[0][0].battery,8)==2);
 Input blocked{};blocked.held=blocked.pressed=0x70;partsEmpty=false;const auto beforeSeed=At<std::uint64_t>(actors[0].object,0x3E8);CHECK(!WalkingAcceptedCanQueue(leases[0]));CHECK(Before(leases[0],true,&blocked)&&blocked.pressed==0x40&&blocked.held==0x70&&At<std::uint64_t>(actors[0].object,0x3E8)==beforeSeed);blocked.held=blocked.pressed=0x70;CHECK(Before(leases[1],false,&blocked)&&blocked.pressed==0x40&&blocked.held==0x40);partsEmpty=true;
 unsigned char wire[512]{};std::size_t used=123;CHECK(CaptureWalkingAcceptedAction(leases[0],wire,512,&used)&&used==0);
 CHECK(!WalkingAcceptedCanQueue(leases[1]));quiescing[0]=true;CHECK(!WalkingAcceptedCanQueue(leases[0]));quiescing[0]=false;
 CHECK(BeginWalkingAcceptedAction(leases[0],1,12,31,41));auto* host=Find(leases[0]);CHECK(host && host->pendingAction && host->count==0);
 canonical[0]=999;CHECK(!BeginWalkingAcceptedAction(leases[0],2,13,32,42));CHECK(!CaptureWalkingAcceptedAction(leases[0],wire,512,&used));
 committed[0]=1;float point[3]{};CHECK(WalkingAcceptedAimPoint(actors[0].object,point)&&point[0]==100);
 CHECK(!QueueWalkingAcceptedAction(leases[0],2,12,31,41)&&host->pendingAction&&host->count==0);
 CHECK(QueueWalkingAcceptedAction(leases[0],1,12,31,41)&&host->count==1&&!host->pendingAction);
 CHECK(QueueWalkingAcceptedAction(leases[0],1,12,31,41)&&host->count==1);CHECK(WalkingAcceptedAimPoint(actors[0].object,point)&&point[0]==100);
 CHECK(!CaptureWalkingAcceptedAction(leases[0],wire,63,&used)&&host->count==1&&used==0);
 CHECK(CaptureWalkingAcceptedAction(leases[0],wire,512,&used)&&used==64&&host->count==0);
 CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::applied&&commits==1&&committed[1]==1&&actors[1].object[0x1940]==0);
 CHECK(seenPoint[0]==100 && At<std::uint64_t>(actors[1].object,0x3E8)==31 && At<std::uint64_t>(actors[1].object,0x490)==41);
 CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::applied&&commits==1);
 Event decoded[8]{};unsigned count=0;CHECK(Decode(wire,used,decoded,&count));decoded[0].seed3=32;used=Wire(decoded,count,wire);CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::failed&&commits==1);
 CHECK(ApplyWalkingAcceptedAction(leases[0],wire,used)==Result::failed); // authority is never a guest replay target
 CHECK(RestoreWalkingAcceptedActions(leases[0]));CHECK(RestoreWalkingAcceptedActions(leases[1]));
 CHECK(At<LONG>(actors[0].control,8)==1 && At<LONG>(guns[0][0].gun,8)==1 && At<LONG>(guns[0][0].battery,8)==1);
 Prepare();Event expired=Action(1);used=Wire(&expired,1,wire);Find(leases[1])->applied=kQueued+1;Find(leases[1])->receipts[1]={kQueued+1,Hash(Action(kQueued+1))};CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::failed&&commits==0);
 Prepare();Event e=Action();used=Wire(&e,1,wire);wire[0]^=1;const auto before=At<std::uint64_t>(actors[1].object,0x3E8);CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::failed&&commits==0&&At<std::uint64_t>(actors[1].object,0x3E8)==before&&Find(leases[1])->applied==0);
 Prepare();e=Action(2);used=Wire(&e,1,wire);CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::failed&&commits==0);
 Prepare();Event batch[2]={Action(1),Action(2,1)};used=Wire(batch,2,wire);actionReady[1]=false;CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::pending&&commits==0&&Find(leases[1])->applied==0);actionReady[1]=true;
 CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::pending&&commits==1&&Find(leases[1])->applied==1);
 actionReady[1]=true;CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::applied&&commits==2&&Find(leases[1])->applied==2);
 CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::applied&&commits==2);
 Prepare();batch[0]=Action(1);batch[1]=GunEvent(2);used=Wire(batch,2,wire);Find(leases[1])->guns[0].digest^=1;
 CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::failed&&commits==0&&gunApplies==0&&Find(leases[1])->applied==0);
 Find(leases[1])->guns[0].digest^=1;CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::applied&&commits==1&&gunApplies==1);
 CHECK(At<int>(Gun(1,0),0x38)==17&&At<std::uint64_t>(Gun(1,0),0x80)==61&&At<float>(Gun(1,0),0x50)==1);
 CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::applied&&gunApplies==1);batch[1].gunRng^=1;used=Wire(batch,2,wire);CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::failed&&gunApplies==1);
 Prepare();CHECK(BeginWalkingAcceptedAction(leases[0],0,1,1,2));CHECK(CancelWalkingAcceptedAction(leases[0])&&!Find(leases[0])->pendingAction);CHECK(WalkingAcceptedActionsEmpty(leases[0]));
 aimAvailable=false;CHECK(!BeginWalkingAcceptedAction(leases[0],0,2,1,2)&&WalkingAcceptedActionsEmpty(leases[0]));
}
void* DispatchUnit(void* factory,const float matrix[16],void* descriptor){return registeredUnit.Owns && registeredUnit.Owns(factory,descriptor) ? registeredUnit.Invoke(nullptr,matrix,factory,descriptor) : InvokeOriginalNativeUnitFactory(nullptr,matrix,factory,descriptor);}
void Emitter(){
 Prepare();alignas(16) unsigned char descriptor[0x1E0]{};float matrix[16]{};IdentityMatrix(matrix);matrix[12]=72;
 Put<void*>(descriptor,0x70,guns[0][0].muzzles[1]+0x70);Put<unsigned>(descriptor,0x54,37);
 CHECK(registeredUnit.Owns(Battery(0,0)+0x2F0,descriptor));CHECK(DispatchUnit(Battery(0,0)+0x2F0,matrix,descriptor)==results[0].object&&originalCreates==1);
 auto* host=Find(leases[0]);CHECK(host->count==1&&!host->faulted&&NativeProjectileJournalEmpty(host->replay.native));
 unsigned char wire[512]{};std::size_t used=0;CHECK(CaptureWalkingAcceptedAction(leases[0],wire,512,&used)&&used>0);
 Event decoded[8]{};unsigned count=0;CHECK(Decode(wire,used,decoded,&count)&&count==1&&decoded[0].kind==EventKind::shot&&decoded[0].muzzle==1&&decoded[0].shot==37&&decoded[0].matrix[12]==72);
 CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::applied&&replays==1);
 CHECK(lastReplay.matrix[12]==72&&lastReplay.shot==37&&At<std::uint64_t>(Battery(1,0),0x298)==1&&NativeProjectileJournalEmpty(Find(leases[1])->replay.native));
 auto* head=static_cast<unsigned char*>(At<void*>(Battery(1,0),0x290));auto* node=At<unsigned char*>(head,8);CHECK(node!=head&&At<void*>(node,0)==head&&At<void*>(node,16)==results[1].object&&At<void*>(node,24)==results[1].control&&At<void*>(head,0)==node);
 CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::applied&&replays==1&&At<std::uint64_t>(Battery(1,0),0x298)==1);
 Put<void*>(descriptor,0x70,guns[1][0].muzzles[1]+0x70);CHECK(DispatchUnit(Battery(1,0)+0x2F0,matrix,descriptor)==nullptr&&originalCreates==1&&Find(leases[1])->count==0);
 Prepare();Put<void*>(descriptor,0x70,guns[0][0].muzzles[0]+0x70);refuseCreate=true;CHECK(DispatchUnit(Battery(0,0)+0x2F0,matrix,descriptor)==nullptr&&originalCreates==1&&WalkingAcceptedActionsEmpty(leases[0])&&!Find(leases[0])->faulted);refuseCreate=false;
 CHECK(BeginWalkingAcceptedAction(leases[0],0,9,31,41));committed[0]=0;CHECK(DispatchUnit(Battery(0,0)+0x2F0,matrix,descriptor)==results[0].object&&host!=nullptr);
 host=Find(leases[0]);CHECK(host->count==2&&!host->pendingAction&&host->queue[0].kind==EventKind::action&&host->queue[1].kind==EventKind::shot);
 CHECK(QueueWalkingAcceptedAction(leases[0],0,9,31,41)&&host->count==2);
 CHECK(CaptureWalkingAcceptedAction(leases[0],wire,512,&used));CHECK(Decode(wire,used,decoded,&count)&&count==2);CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::applied&&commits==1&&replays==1);
 Prepare();contexts[0]=false;Put<void*>(descriptor,0x70,guns[0][0].muzzles[0]+0x70);CHECK(registeredUnit.Owns(Battery(0,0)+0x2F0,descriptor));CHECK(DispatchUnit(Battery(0,0)+0x2F0,matrix,descriptor)==nullptr&&originalCreates==0&&Find(leases[0])->faulted);
 Reset();CHECK(registeredUnit.Owns(Battery(0,0)+0x2F0,descriptor));CHECK(DispatchUnit(Battery(0,0)+0x2F0,matrix,descriptor)==nullptr&&originalCreates==0);contexts[0]=false;
 CHECK(!registeredUnit.Owns(Battery(0,0)+0x2F0,descriptor));CHECK(DispatchUnit(Battery(0,0)+0x2F0,matrix,descriptor)==results[0].object&&originalCreates==1);
 Prepare();Event shot=Shot();used=Wire(&shot,1,wire);refuseReplay=true;CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::failed&&replays==1&&Find(leases[1])->faulted);CHECK(!WalkingAcceptedActionsEmpty(leases[1]));restoreJournals=false;CHECK(!RestoreWalkingAcceptedActions(leases[1])&&Find(leases[1])&&Find(leases[1])->rootHeld);restoreJournals=true;refuseReplay=false;CHECK(RestoreWalkingAcceptedActions(leases[1]));
 Prepare();beamMode=true;Put<void*>(descriptor,0x70,guns[0][0].muzzles[0]+0x70);CHECK(DispatchUnit(Battery(0,0)+0x2F0,matrix,descriptor)==results[0].object);
 CHECK(CaptureWalkingAcceptedAction(leases[0],wire,512,&used)&&ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::applied);CHECK(registeredBeam.Owns(results[0].object)&&registeredBeam.Owns(results[1].object));
 auto* guest=Find(leases[1]);CHECK(guest->guns[0].beams[0].active&&At<void*>(guns[1][0].muzzles[0],0xB0)==results[1].object&&At<void*>(guns[1][0].muzzles[0],0xB8)==results[1].control);
 Put<float>(guns[1][0].muzzles[0],0x70+12*4,999);registeredStep.Invoke(results[1].object,nullptr);CHECK(beamSteps==1&&beamRefreshes==1&&At<float>(guns[1][0].muzzles[0],0x70+12*4)==0);
 Put<float>(guns[0][0].muzzles[0],0x70+12*4,333);registeredBeam.Invoke(results[0].object);CHECK(beamRefreshes==2&&Find(leases[0])->count==1);CHECK(CaptureWalkingAcceptedAction(leases[0],wire,512,&used));
 CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::applied&&beamRefreshes==3&&At<float>(guns[1][0].muzzles[0],0x70+12*4)==333);
 CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::applied&&beamRefreshes==3);
 CHECK(QuiesceWalkingAcceptedActions(leases[0])&&deleted==1&&!WalkingAcceptedActionsEmpty(leases[0]));CHECK(CaptureWalkingAcceptedAction(leases[0],wire,512,&used));CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::applied&&deleted==2&&!guest->guns[0].beams[0].held);CHECK(RestoreWalkingAcceptedActions(leases[0]));CHECK(RestoreWalkingAcceptedActions(leases[1]));
}
void BeamPair(){
 Prepare();beamMode=true;alignas(16) unsigned char descriptor[0x1E0]{};float matrix[16]{};IdentityMatrix(matrix);Put<void*>(descriptor,0x70,guns[0][0].muzzles[0]+0x70);
 CHECK(DispatchUnit(Battery(0,0)+0x2F0,matrix,descriptor)==results[0].object);unsigned char wire[512]{};std::size_t used=0;CHECK(CaptureWalkingAcceptedAction(leases[0],wire,512,&used)&&ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::applied);
}
// Faults intentionally model irreversible native uncertainty. Only this private
// fixture's teardown clears the injected condition after testing the production
// refusal; it does not claim an uncertain real native operation is retryable.
void DiscardInjectedFault(){
 throwDelete=throwFree=createdCleanupFailure=throwStop=throwCacheDelete=swapOnStop=swapOnDelete=false;callbackCache=nullptr;callbackTrigger=nullptr;restoreJournals=true;
 for(auto& stream:streams)if(stream.rootHeld){
  if(stream.replay.node && stream.replay.freeEntered){for(auto& node:allocations)if(node==stream.replay.node){std::free(node);node=nullptr;}stream.replay.node=nullptr;}
  stream.replay.native={};stream.replay.uncertain=stream.replay.freeEntered=false;
  stream.preparing=false;
  for(auto& part:stream.guns){
   part.prep.uncertain=false;if(!part.prep.complete)part.prep.entered=false;
   for(auto& beam:part.beams){beam.uncertain=false;beam.stopEntered=false;}
  }
 }
}
void ExceptionalLifetime(){
 for(unsigned cause=0;cause<2;++cause){
  BeamPair();auto* guest=Find(leases[1]);auto& latch=guest->guns[0].beams[0];const auto strong=At<LONG>(results[1].control,8);
  if(cause)results[1].object[0x18]|=4;else Put<unsigned>(results[1].object,0xC34,At<unsigned>(results[1].object,0xC34)|0x4000u);
  CHECK(!TickBeams(*guest,false)&&guest->faulted&&latch.held&&latch.active&&At<LONG>(results[1].control,8)==strong);
  CHECK(!TickBeams(*guest,false)&&latch.held&&At<LONG>(results[1].control,8)==strong);
  Input input{};CHECK(!Before(leases[1],false,&input)&&latch.held);
  CHECK(!QuiesceWalkingAcceptedActions(leases[1])&&latch.held);CHECK(AbortWalkingAcceptedActions(leases[1])&&!Find(leases[1])&&At<LONG>(results[1].control,8)==1);
 }
 for(unsigned cause=0;cause<2;++cause){
  BeamPair();auto* host=Find(leases[0]);if(cause)results[0].object[0x18]|=4;else Put<unsigned>(results[0].object,0xC34,0x4000u);
  const auto refreshBefore=beamRefreshes,stepBefore=beamSteps;registeredBeam.Invoke(results[0].object);
  CHECK(beamRefreshes==refreshBefore+1&&!host->faulted&&host->count==0&&(At<unsigned>(results[0].object,0xC34)&0x2000u)==0);
  registeredStep.Invoke(results[0].object,nullptr);CHECK(beamSteps==stepBefore+1&&!host->faulted&&host->count==0);
  CHECK(QuiesceWalkingAcceptedActions(leases[0]));
 }
 BeamPair();auto* host=Find(leases[0]);auto& latch=host->guns[0].beams[0];const auto rootStrong=At<LONG>(actors[0].control,8),beamStrong=At<LONG>(results[0].control,8);throwDelete=true;
 CHECK(!QuiesceWalkingAcceptedActions(leases[0])&&deleted==1&&latch.held&&latch.stopEntered&&latch.uncertain);
 CHECK(!WalkingAcceptedActionsEmpty(leases[0]));CHECK(!RestoreWalkingAcceptedActions(leases[0])&&Find(leases[0])==host&&At<LONG>(actors[0].control,8)==rootStrong&&At<LONG>(results[0].control,8)==beamStrong);
 CHECK(!QuiesceWalkingAcceptedActions(leases[0])&&!DeleteBeam(latch)&&deleted==1&&latch.held);DiscardInjectedFault();
 Prepare();auto* guest=Find(leases[1]);guest->replay.node=Fn<AllocFn>(0x12D85B0)(32);const auto node=guest->replay.node;throwFree=true;
 CHECK(!CleanReplay(*guest)&&freeCalls==1&&guest->replay.freeEntered&&guest->replay.uncertain&&guest->replay.node==node);
 CHECK(!CleanReplay(*guest)&&freeCalls==1&&!WalkingAcceptedActionsEmpty(leases[1]));CHECK(!RestoreWalkingAcceptedActions(leases[1])&&freeCalls==1&&Find(leases[1])==guest&&guest->rootHeld);DiscardInjectedFault();
 Prepare();guest=Find(leases[1]);Event e=Shot();unsigned char wire[512]{};const auto used=Wire(&e,1,wire);createdCleanupFailure=true;const auto beforePin=At<LONG>(results[1].control,8);
 CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::failed&&replays==1&&guest->faulted);
 CHECK(guest->replay.native.created==results[1].object&&guest->replay.native.cleanupEntered&&guest->replay.native.uncertain&&guest->replay.resultHeld&&guest->replay.result.object==results[1].object&&guest->replay.result.control==results[1].control&&At<LONG>(results[1].control,8)==beforePin+1);
 CHECK(!WalkingAcceptedActionsEmpty(leases[1])&&!RestoreWalkingAcceptedActions(leases[1])&&guest->replay.resultHeld&&At<LONG>(results[1].control,8)==beforePin+1);
 CHECK(ApplyWalkingAcceptedAction(leases[1],wire,used)==Result::failed&&replays==1&&At<LONG>(results[1].control,8)==beforePin+1);DiscardInjectedFault();
}
void LegacyBeam(unsigned owner,unsigned key,unsigned muzzle=0,bool alive=true) {
 auto& mem=guns[owner][key];Put<void*>(mem.bullet,0,beamTable);Put<void*>(mem.bullet,0x28,mem.bullet);Put<void*>(mem.bullet,0x30,mem.bulletControl);Put<unsigned>(mem.bullet,0xC30,1);
 if(!alive)Put<LONG>(mem.bulletControl,8,0);
 InterlockedIncrement(reinterpret_cast<volatile LONG*>(mem.bulletControl+12));
 Put<void*>(mem.muzzles[muzzle],0xB0,alive ? mem.bullet : reinterpret_cast<void*>(1));Put<void*>(mem.muzzles[muzzle],0xB8,mem.bulletControl);
 Put<unsigned>(Battery(owner,key),0x2C0,1);Put<unsigned>(Battery(owner,key),0x2E0,3);Put<unsigned>(Battery(owner,key),0x2E8,4);Put<int>(Gun(owner,key),0x38,12);
}
void BaselinePreparation(){
 Reset();gunCount=4;gunEnabled[1]=false;
 for(unsigned peer=0;peer<2;++peer)for(unsigned key=0;key<4;++key)LegacyBeam(peer,key);
 Input input{};CHECK(Before(leases[0],true,&input)&&Before(leases[1],false,&input));
 CHECK(nativeStops==8&&nativeCacheDeletes==0&&deleted==8&&originalCreates==0);
 for(unsigned peer=0;peer<2;++peer){auto* stream=Find(leases[peer]);CHECK(stream&&stream->guns[1].batteryHeld&&stream->guns[1].gunHeld&&stream->guns[1].prep.complete);
  for(unsigned key=0;key<4;++key){const auto& mem=guns[peer][key];CHECK(stream->guns[key].prep.complete&&!stream->guns[key].prep.held[0]&&!At<void*>(mem.muzzles[0],0xB0)&&!At<void*>(mem.muzzles[0],0xB8));CHECK((mem.bullet[0x18]&4)&&At<LONG>(mem.bulletControl,8)==1&&At<LONG>(mem.bulletControl,12)==1&&At<int>(Gun(peer,key),0x38)==-1&&At<unsigned>(Battery(peer,key),0x2E0)==0&&At<unsigned>(Battery(peer,key),0x2E8)==0);}
 }
 const auto preparedStops=nativeStops;CHECK(Before(leases[0],true,&input)&&WalkingAcceptedCanQueue(leases[0])&&nativeStops==preparedStops&&deleted==8);
 // A newly accepted beam is never mistaken for the pre-lease NPC cache.
 beamMode=true;alignas(16) unsigned char descriptor[0x1E0]{};float matrix[16]{};IdentityMatrix(matrix);Put<void*>(descriptor,0x70,guns[0][0].muzzles[0]+0x70);
 CHECK(DispatchUnit(Battery(0,0)+0x2F0,matrix,descriptor)==results[0].object);CHECK(Before(leases[0],true,&input)&&nativeStops==preparedStops&&!(results[0].object[0x18]&4));
 Reset();LegacyBeam(0,0,0,false);CHECK(Before(leases[0],true,&input)&&deleted==0&&!At<void*>(guns[0][0].muzzles[0],0xB8)&&At<LONG>(guns[0][0].bulletControl,8)==0&&At<LONG>(guns[0][0].bulletControl,12)==1);
 Reset();LegacyBeam(0,0);guns[0][0].bullet[0x18]|=4;
 CHECK(Before(leases[0],true,&input)&&deleted==0&&nativeCacheDeletes==0&&!At<void*>(guns[0][0].muzzles[0],0xB8)&&At<LONG>(guns[0][0].bulletControl,8)==1&&At<LONG>(guns[0][0].bulletControl,12)==1); // an already-deleted cache is cleared before native Delete's loop
 // All entries are preflighted before stopping the first source.
 Reset();LegacyBeam(0,0);LegacyBeam(0,1);Put<unsigned>(guns[0][1].bullet,0xC30,0);
 CHECK(!Before(leases[0],true,&input)&&nativeStops==0&&nativeCacheDeletes==0&&deleted==0&&!(guns[0][0].bullet[0x18]&4));
 CHECK(RestoreWalkingAcceptedActions(leases[0])&&At<LONG>(guns[0][0].bulletControl,8)==1&&At<LONG>(guns[0][1].bulletControl,8)==1);
 // Stop may reenter input, but preparation is admitted only once.
 Reset();LegacyBeam(0,0);reenterStop=true;CHECK(Before(leases[0],true,&input)&&reenteredBlocked&&nativeStops==2&&nativeCacheDeletes==0);
 for(unsigned failure=0;failure<2;++failure){Reset();LegacyBeam(0,0);if(failure)throwDelete=true;else throwStop=true;
  CHECK(!Before(leases[0],true,&input));auto* stream=Find(leases[0]);CHECK(stream&&stream->guns[0].prep.entered&&stream->guns[0].prep.uncertain&&stream->guns[0].prep.held[0]);
  CHECK(stream->guns[0].prep.deleteEntered[0]==(failure!=0));
  const auto stopCount=nativeStops,deleteCount=nativeCacheDeletes,steps=beamSteps,refresh=beamRefreshes;
  CHECK(registeredStep.Owns(guns[0][0].bullet)&&registeredBeam.Owns(guns[0][0].bullet));registeredStep.Invoke(guns[0][0].bullet,nullptr);registeredBeam.Invoke(guns[0][0].bullet);
  CHECK(beamSteps==steps&&beamRefreshes==refresh&&!WalkingAcceptedActionsEmpty(leases[0])&&!RestoreWalkingAcceptedActions(leases[0]));
  CHECK(!Before(leases[0],true,&input)&&nativeStops==stopCount&&nativeCacheDeletes==deleteCount);DiscardInjectedFault();
 }
 // A native Stop callback can replace the cache after preflight. The old
 // whole-battery Delete loop would follow that new pointer and delete foreign.
 Reset();LegacyBeam(0,0);Put<void*>(actors[2].object,0,beamTable);Put<unsigned>(actors[2].object,0xC30,1);
 callbackCache=guns[0][0].muzzles[0];swapOnStop=true;
 CHECK(!Before(leases[0],true,&input));auto* changed=Find(leases[0]);
 CHECK(changed&&changed->guns[0].prep.uncertain&&changed->guns[0].prep.held[0]&&nativeStops==1&&nativeCacheDeletes==0&&deleted==0);
 CHECK(!(actors[2].object[0x18]&4)&&!(guns[0][0].bullet[0x18]&4)&&At<void*>(callbackCache,0xB0)==actors[2].object&&At<LONG>(actors[2].control,8)==1);
 const auto changedStops=nativeStops;CHECK(!Before(leases[0],true,&input)&&nativeStops==changedStops&&deleted==0);DiscardInjectedFault();
 // Deleting the first snapshot must not authorize a callback's replacement
 // of a later muzzle. The already-pinned later snapshot is not deleted either.
 Reset();LegacyBeam(0,0);LegacyBeam(0,1);
 InterlockedIncrement(reinterpret_cast<volatile LONG*>(guns[0][1].bulletControl+12));Put<void*>(guns[0][0].muzzles[1],0xB0,guns[0][1].bullet);Put<void*>(guns[0][0].muzzles[1],0xB8,guns[0][1].bulletControl);
 Put<void*>(actors[2].object,0,beamTable);Put<unsigned>(actors[2].object,0xC30,1);callbackCache=guns[0][0].muzzles[1];callbackTrigger=guns[0][0].bullet;swapOnDelete=true;
 CHECK(!Before(leases[0],true,&input));changed=Find(leases[0]);
 CHECK(changed&&changed->guns[0].prep.uncertain&&deleted==1&&nativeCacheDeletes==0&&(guns[0][0].bullet[0x18]&4));
 CHECK(!(actors[2].object[0x18]&4)&&!(guns[0][1].bullet[0x18]&4)&&At<void*>(callbackCache,0xB0)==actors[2].object&&At<LONG>(actors[2].control,8)==1);
 CHECK(!Before(leases[0],true,&input)&&deleted==1);DiscardInjectedFault();
 Reset();CHECK(Before(leases[0],true,&input));auto* completed=Find(leases[0]);completed->guns[0].prep.uncertain=true;
 CHECK(completed->guns[0].prep.complete&&!Before(leases[0],true,&input)&&!WalkingAcceptedCanQueue(leases[0])&&!WalkingAcceptedActionsEmpty(leases[0]));DiscardInjectedFault();
 Reset();CHECK(Before(leases[0],true,&input));completed=Find(leases[0]);completed->guns[0].prep.uncertain=true;
 CHECK(completed->guns[0].prep.complete&&!WalkingAcceptedCanQueue(leases[0])&&!Before(leases[0],true,&input));DiscardInjectedFault();
 for(const auto* profile:{profiles+1,profiles+3}){Reset(profile);LegacyBeam(0,0);LegacyBeam(1,0);Put<int>(Gun(0,0),0x38,77);Put<int>(Gun(1,0),0x38,88);
  CHECK(Before(leases[0],true,&input)&&Before(leases[1],false,&input)&&nativeStops==2&&nativeCacheDeletes==0&&deleted==2);
  CHECK(!Find(leases[0])->guns[0].gunHeld&&At<int>(Gun(0,0),0x38)==77&&At<int>(Gun(1,0),0x38)==88);
 }
}
void DrainOrdering(){
 BeamPair();auto* host=Find(leases[0]);auto* guest=Find(leases[1]);unsigned char refreshWire[512]{},stopWire[512]{};std::size_t refreshSize=0,stopSize=0;
 Put<float>(guns[0][0].muzzles[0],0x70+12*4,444);registeredBeam.Invoke(results[0].object);CHECK(CaptureWalkingAcceptedAction(leases[0],refreshWire,512,&refreshSize)&&refreshSize);
 CHECK(QuiesceWalkingAcceptedActions(leases[0])&&CaptureWalkingAcceptedAction(leases[0],stopWire,512,&stopSize)&&stopSize);const auto sourceDeletes=deleted;
 // Protocol::StartDrain broadcasts Drain before the final buffered Action.
 CHECK(QuiesceWalkingAcceptedActions(leases[1])&&guest->quiescing&&guest->guns[0].beams[0].held&&guest->guns[0].beams[0].active&&deleted==sourceDeletes);
 CHECK(!WalkingAcceptedActionsEmpty(leases[1])&&!RestoreWalkingAcceptedActions(leases[1]));const auto keep=beamRefreshes;registeredStep.Invoke(results[1].object,nullptr);
 CHECK(beamRefreshes==keep+1&&!(results[1].object[0x18]&4));
 CHECK(ApplyWalkingAcceptedAction(leases[1],refreshWire,refreshSize)==Result::applied&&At<float>(guns[1][0].muzzles[0],0x70+12*4)==444);
 CHECK(ApplyWalkingAcceptedAction(leases[1],stopWire,stopSize)==Result::applied&&!guest->guns[0].beams[0].held&&WalkingAcceptedActionsEmpty(leases[1]));
 CHECK(RestoreWalkingAcceptedActions(leases[0])&&RestoreWalkingAcceptedActions(leases[1])&&host!=nullptr);
 BeamPair();guest=Find(leases[1]);CHECK(QuiesceWalkingAcceptedActions(leases[1])&&guest->guns[0].beams[0].active);const auto deletedBeforeAbort=deleted;
 CHECK(AbortWalkingAcceptedActions(leases[1])&&!Find(leases[1])&&deleted==deletedBeforeAbort+1);
 BeamPair();host=Find(leases[0]);host->faulted=true;
 CHECK(QuiesceWalkingAcceptedActions(leases[0])&&host->count==1&&!host->guns[0].beams[0].held);
 CHECK(QuiesceWalkingAcceptedActions(leases[0])&&host->count==1); // committed Stop is not duplicated by a previous source fault
 CHECK(CaptureWalkingAcceptedAction(leases[0],stopWire,512,&stopSize)&&stopSize);
 CHECK(ApplyWalkingAcceptedAction(leases[1],stopWire,stopSize)==Result::applied&&!Find(leases[1])->guns[0].beams[0].held);
 CHECK(RestoreWalkingAcceptedActions(leases[0])&&RestoreWalkingAcceptedActions(leases[1]));
}
struct TraceGun {unsigned key=0,offset=0,charge=0,count=0,interval=1,muzzles=1;};
bool DumpTrace(const char* path,const char* assets){
 std::ifstream input(assets);std::ofstream out(path,std::ios::binary);if(!input||!out){std::printf("trace open failed: %s / %s\n",path,assets);return false;}
 std::string line,scenario,family;std::vector<TraceGun> configs;bool eligible=false;unsigned nullEvery=0,scenarios=0;
 while(std::getline(input,line)){
  if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.size()>2&&line[0]=='M'&&line[1]=='\t'){out<<line.substr(2)<<'\n';continue;}
  if(line.size()>2&&line[0]=='S'&&line[1]=='\t'){std::istringstream row(line.substr(2));unsigned enabled=0;row>>scenario>>family>>enabled>>nullEvery;eligible=enabled!=0;configs.clear();continue;}
  if(line.size()>2&&line[0]=='G'&&line[1]=='\t'){TraceGun gun{};std::istringstream row(line.substr(2));row>>gun.key>>gun.offset>>gun.charge>>gun.count>>gun.interval>>gun.muzzles;if(!row||gun.key>=4||gun.muzzles>2||!gun.interval||!gun.count)return false;configs.push_back(gun);continue;}
  if(line!="E")continue;
  ++scenarios;if(!eligible){out<<"{\"record\":\"end\",\"scenario\":\""<<scenario<<"\",\"skippedExpressionCadence\":true,\"factoryAttempts\":0,\"actualSuccessfulUnits\":0}\n";continue;}
  const auto* profile=family=="EDF6_Berserker_Large"?profiles:family=="EDF6_Berserker_Middle"?profiles+1:profiles+3;Reset(profile);gunCount=0;for(bool& enabled:gunEnabled)enabled=false;
  unsigned last=0;for(const auto& gun:configs){gunEnabled[gun.key]=true;gunCount=(std::max)(gunCount,gun.key+1);last=(std::max)(last,gun.offset+gun.charge+(gun.count-1)*gun.interval);Put<std::size_t>(Battery(0,gun.key),0x288,gun.muzzles);}
  Input native{};if(!Before(leases[0],true,&native))return false;
  std::uint64_t attempts=0,units=0,nulls=0,packets=0;alignas(16) unsigned char descriptor[0x1E0]{};float matrix[16]{};IdentityMatrix(matrix);
  for(unsigned tick=0;tick<=last;++tick){
   for(const auto& gun:configs){const unsigned first=gun.offset+gun.charge;if(tick<first||(tick-first)%gun.interval)continue;const unsigned shot=(tick-first)/gun.interval;if(shot>=gun.count)continue;
    for(unsigned muzzle=0;muzzle<gun.muzzles;++muzzle){++attempts;refuseCreate=nullEvery&&attempts%nullEvery==0;Put<void*>(descriptor,0x70,guns[0][gun.key].muzzles[muzzle]+0x70);Put<unsigned>(descriptor,0x54,shot);matrix[12]=static_cast<float>(gun.key*100+muzzle*10);if(DispatchUnit(Battery(0,gun.key)+0x2F0,matrix,descriptor))++units;else ++nulls;}
   }
   auto* stream=Find(leases[0]);while(stream&&stream->count){unsigned char wire[512]{};std::size_t used=0;if(!CaptureWalkingAcceptedAction(leases[0],wire,512,&used)||!used)return false;Event events[8]{};unsigned count=0;if(!Decode(wire,used,events,&count))return false;unsigned accepted=0;for(unsigned i=0;i<count;++i)if(events[i].kind==EventKind::shot)++accepted;
    out<<"{\"record\":\"packet\",\"scenario\":\""<<scenario<<"\",\"nativeUpdateTick\":"<<tick<<",\"actualUnits\":"<<accepted<<",\"encodedBytes\":"<<used<<",\"hex\":\"";constexpr char digits[]="0123456789abcdef";for(std::size_t i=0;i<used;++i){out<<digits[wire[i]>>4]<<digits[wire[i]&15];}out<<"\"}\n";++packets;
   }
  }
  out<<"{\"record\":\"end\",\"scenario\":\""<<scenario<<"\",\"nativeUpdates\":"<<last+1<<",\"factoryAttempts\":"<<attempts<<",\"actualSuccessfulUnits\":"<<units<<",\"falseFactoryOutcomes\":"<<nulls<<",\"packets\":"<<packets<<"}\n";
  if(!QuiesceWalkingAcceptedActions(leases[0])||!WalkingAcceptedActionsEmpty(leases[0])||!RestoreWalkingAcceptedActions(leases[0]))return false;
 }
 std::printf("Walking production trace: %u scenarios -> %s\n",scenarios,path);return bool(out);
}
void DrainAndRestore(){
 Prepare();auto* host=Find(leases[0]);CHECK(Push(*host,Action(1)));CHECK(Push(*host,Action(2,1)));
 CHECK(!WalkingAcceptedActionsEmpty(leases[0])&&!RestoreWalkingAcceptedActions(leases[0]));
 CHECK(QuiesceWalkingAcceptedActions(leases[0])&&host->quiescing&&host->count==2&&restores==1);CHECK(!WalkingAcceptedCanQueue(leases[0]));
 unsigned char wire[512]{};std::size_t used=0;CHECK(CaptureWalkingAcceptedAction(leases[0],wire,512,&used)&&used==120&&WalkingAcceptedActionsEmpty(leases[0]));CHECK(RestoreWalkingAcceptedActions(leases[0]));
 Prepare();host=Find(leases[0]);host->faulted=true;CHECK(Push(*host,Action())==false);CHECK(QuiesceWalkingAcceptedActions(leases[0]));CHECK(RestoreWalkingAcceptedActions(leases[0]));
 Prepare();host=Find(leases[0]);CHECK(Push(*host,Action()));host->faulted=true;CHECK(!CaptureWalkingAcceptedAction(leases[0],wire,512,&used));CHECK(QuiesceWalkingAcceptedActions(leases[0]));CHECK(CaptureWalkingAcceptedAction(leases[0],wire,512,&used)&&used>0);CHECK(RestoreWalkingAcceptedActions(leases[0]));
 Prepare();host=Find(leases[0]);CHECK(Push(*host,Action()));CHECK(AbortWalkingAcceptedActions(leases[0])&&!Find(leases[0]));
 Prepare();auto* guest=Find(leases[1]);Put<void*>(actors[1].control,0,nullptr);CHECK(!RestoreWalkingAcceptedActions(leases[1])&&Find(leases[1])==guest&&guest->rootHeld&&At<LONG>(actors[1].control,8)==2);Put<void*>(actors[1].control,0,controlTable);CHECK(RestoreWalkingAcceptedActions(leases[1]));
 Prepare();host=Find(leases[0]);CHECK(BeginWalkingAcceptedAction(leases[0],0,7,8,9));CHECK(!QuiesceWalkingAcceptedActions(leases[0])&&host->pendingAction&&!WalkingAcceptedActionsEmpty(leases[0]));committed[0]=0;CHECK(QueueWalkingAcceptedAction(leases[0],0,7,8,9)&&host->count==1);CHECK(CaptureWalkingAcceptedAction(leases[0],wire,512,&used)&&used>0);CHECK(QuiesceWalkingAcceptedActions(leases[0])&&RestoreWalkingAcceptedActions(leases[0]));
 Prepare();guest=Find(leases[1]);guest->replay.uncertain=true;CHECK(!WalkingAcceptedActionsEmpty(leases[1])&&!RestoreWalkingAcceptedActions(leases[1])&&Find(leases[1])==guest&&At<LONG>(actors[1].control,8)==2);guest->replay.uncertain=false;CHECK(RestoreWalkingAcceptedActions(leases[1]));
 Prepare();host=Find(leases[0]);for(unsigned i=0;i<kQueued;++i)CHECK(Push(*host,Action(i+1)));CHECK(!Push(*host,Action(kQueued+1))&&host->count==kQueued&&host->faulted);CHECK(QuiesceWalkingAcceptedActions(leases[0]));unsigned drained=0;while(host->count){CHECK(CaptureWalkingAcceptedAction(leases[0],wire,512,&used));Event events[8]{};unsigned count=0;CHECK(Decode(wire,used,events,&count));drained+=count;}CHECK(drained==kQueued&&WalkingAcceptedActionsEmpty(leases[0]));CHECK(RestoreWalkingAcceptedActions(leases[0]));
 Reset(profiles+1);Input input{};CHECK(Before(leases[0],true,&input));CHECK(Find(leases[0])->guns[0].batteryHeld&&!Find(leases[0])->guns[0].gunHeld);CHECK(At<LONG>(guns[0][0].battery,8)==2);CHECK(RestoreWalkingAcceptedActions(leases[0]));
}
}
namespace crew {
bool EnemyControlTargetActive(const void* root) noexcept {return Index(root)>=0;}
bool EnemyControlNetworkContext(const void* root,bool* value) noexcept {const int i=Index(root);if(i<0||!value||!contexts[i])return false;*value=online[i];return true;}
bool EnemyControlWeaponLock(const void* root,void* out16) noexcept {if(Index(root)<0||!out16)return false;std::memset(out16,0,16);return true;}
bool EnemyControlWeaponAim(const void* root,float out[3]) noexcept {if(Index(root)<0||!aimAvailable)return false;std::memcpy(out,canonical,12);return true;}
}
namespace crew::enemy_native {
const NativeProfile* Profiles(std::size_t* count) noexcept {*count=sizeof(profiles)/sizeof(profiles[0]);return profiles;}
bool NativeActionRole(const void* root,bool* authority) noexcept {const int i=Index(root);if(i<0||!roles[i])return false;*authority=i==0;return true;}
bool WalkingPartsEmpty(const NativeLease&) noexcept {return partsEmpty;}
bool NativeActionQuiescing(const void* root) noexcept {const int i=Index(root);return i>=0&&quiescing[i];}
bool RegisterNativeActionExtension(const NativeActionExtension& value) noexcept {registeredExtension=value;return true;}
bool WalkingActionDefinition(const NativeLease& l,unsigned key,std::uint64_t* out) noexcept {const int i=Index(l.object);if(i<0||key>=3||!out||!actionDefined[i])return false;*out=0xA000+key;return true;}
bool WalkingActionReady(const NativeLease& l,unsigned key) noexcept {const int i=Index(l.object);return i>=0&&key<3&&actionReady[i];}
bool WalkingActionEntered(const NativeLease& l,unsigned key) noexcept {const int i=Index(l.object);return i>=0&&committed[i]==key;}
bool WalkingActionCommitted(const NativeLease& l,unsigned key) noexcept {const int i=Index(l.object);return i>=0&&committed[i]==key;}
bool CommitWalkingAction(const NativeLease& l,unsigned key,std::uint64_t,std::uint64_t a,std::uint64_t b,std::uint8_t part) noexcept {const int i=Index(l.object);if(i<0||!WalkingActionReady(l,key))return false;++commits;committed[i]=key;actionReady[i]=false;Put<std::uint64_t>(l.object,0x3E8,a);Put<std::uint64_t>(l.object,0x490,b);l.object[0x1940]=part;WalkingAcceptedAimPoint(l.object,seenPoint);return true;}
bool CommitWalkingAction(const NativeLease& l,unsigned key,std::uint64_t seq,std::uint64_t a,std::uint64_t b) noexcept {return CommitWalkingAction(l,key,seq,a,b,l.object[0x1940]);}
unsigned ListWalkingGunAbilities(const NativeLease& l,NativeAbility* out,unsigned capacity) noexcept {if(Index(l.object)<0)return 0;unsigned total=0;for(unsigned i=0;i<gunCount;++i)if(gunEnabled[i]){if(out&&total<capacity){out[total]={};out[total].argument=static_cast<int>(i);}++total;}return total;}
bool WalkingGunPreparationCount(const NativeLease& l,unsigned* count) noexcept {if(Index(l.object)<0||!count||gunCount>4)return false;*count=gunCount;return true;}
bool ResolveWalkingGunPreparationKey(const NativeLease& l,unsigned key,void** out,std::uint64_t* digest) noexcept {const int i=Index(l.object);if(i<0||key>=gunCount||key>=4||!out||!digest)return false;*out=Gun(i,key);*digest=0xB000+key;return true;}
bool ResolveWalkingGunKey(const NativeLease& l,unsigned key,void** out) noexcept {const int i=Index(l.object);if(i<0||key>=gunCount||key>=4||!gunEnabled[key]||!out)return false;*out=Gun(i,key);return true;}
bool WalkingGunDefinition(const NativeLease& l,unsigned key,std::uint64_t* out) noexcept {if(Index(l.object)<0||key>=4||!out)return false;*out=0xB000+key;return true;}
bool WalkingBatteryDefinition(const NativeLease& l,unsigned key,std::uint64_t* out) noexcept {return WalkingGunDefinition(l,key,out);}
bool WalkingGunCommitted(const NativeLease& l,unsigned key) noexcept {const int i=Index(l.object);return i>=0&&key<4&&At<int>(Gun(i,key),0x38)>=0;}
bool ApplyWalkingGunAcceptedControl(const NativeLease& l,unsigned key,unsigned phase,std::uint64_t,std::uint64_t,std::uint64_t,const float[3]) noexcept {const int i=Index(l.object);if(i<0||key>=4)return false;++gunApplies;Put<int>(Gun(i,key),0x38,phase ? -1 : 0);return true;}
bool RestoreWalkingGunControl(const NativeLease&) noexcept {++restores;return restoreGuns;}
bool CanReplayNativeProjectileSample(const NativeLease&,void*,const void*,float*,const NativeProjectileSample&) noexcept {return replayReady;}
bool NativeProjectileJournalEmpty(const NativeProjectileJournal& j) noexcept {return journalsEmpty&&!j.uncertain&&!j.busy;}
bool RestoreNativeProjectileJournal(NativeProjectileJournal& j) noexcept {if(!restoreJournals||(createdCleanupFailure&&j.created))return false;j={};return true;}
bool NativeProjectileJournalsEmpty(const NativeLease&) noexcept {return journalsEmpty;}
bool RestoreNativeProjectileJournals(const NativeLease&) noexcept {return restoreJournals;}
void* ReplayNativeProjectileSampleInPlace(NativeProjectileJournal& j,const NativeLease& l,void*,const void*,float* muzzle,const NativeProjectileSample& sample) noexcept {++replays;lastReplay=sample;if(createdCleanupFailure){const int owner=Index(l.object);if(owner<0)return nullptr;j.created=results[owner].object;j.ownerObject=l.object;j.ownerControl=l.control;j.made=true;j.cleanupEntered=true;j.uncertain=true;return nullptr;}if(refuseReplay){j.uncertain=true;return nullptr;}const int i=Index(l.object);if(i<0)return nullptr;if(sample.hasMuzzle)std::memcpy(muzzle,sample.muzzleMatrix,64);Put<unsigned>(results[i].object,0xC30,beamMode?1u:0u);return results[i].object;}
void* ReplayNativeProjectileSample(const NativeLease&,void*,const void*,float*,const NativeProjectileSample&) noexcept {++replays;return refuseReplay ? nullptr : actors[2].object;}
bool RegisterNativeGunFactoryProvider(const NativeGunFactoryProvider& p) noexcept {registeredGun=p;return true;}
bool RegisterNativeUnitFactoryProvider(const NativeUnitFactoryProvider& p) noexcept {registeredUnit=p;return true;}
bool RegisterNativeBeamRefreshProvider(const NativeBeamRefreshProvider& p) noexcept {registeredBeam=p;return true;}
bool InstallNativeGunFactoryDispatcher() noexcept {return true;}
bool InstallNativeUnitFactoryDispatcher() noexcept {return true;}
bool InstallNativeBeamRefreshDispatcher() noexcept {return true;}
bool RegisterNativeUnitFactoryCallsite(std::uint32_t) noexcept {return true;}
bool NativeGunFactoryProviderReady(const NativeGunFactoryProvider& p) noexcept {return p.Owns==registeredGun.Owns&&p.Invoke==registeredGun.Invoke;}
bool NativeUnitFactoryProviderReady(const NativeUnitFactoryProvider& p) noexcept {return dispatchReady&&p.Owns==registeredUnit.Owns&&p.Invoke==registeredUnit.Invoke;}
bool NativeBeamRefreshProviderReady(const NativeBeamRefreshProvider& p) noexcept {return dispatchReady&&p.Owns==registeredBeam.Owns&&p.Invoke==registeredBeam.Invoke;}
bool NativeUnitFactoryCallsiteReady(std::uint32_t) noexcept {return dispatchReady;}
// Mirror the shared helper's pure numeric/ID contract at this ABI boundary;
// its native copy/ownership implementation has independent focused coverage.
bool ValidNativeProjectileSample(const NativeProjectileSample& sample) noexcept {
 if(!walking_net::Matrix(sample.matrix)|| (sample.hasMuzzle&&!walking_net::Matrix(sample.muzzleMatrix))||sample.resourceMask>3)return false;
 float speed=0,inherit[3]{};std::memcpy(&speed,sample.scalar+6,4);for(unsigned i=0;i<3;++i)std::memcpy(inherit+i,sample.scalar+i+1,4);if(!std::isfinite(speed))return false;
 const float sum=(inherit[1]+inherit[0])+inherit[2];for(unsigned i=0;i<3;++i){const float value=sample.matrix[8+i]*speed;if(!std::isfinite(value)||(!std::isnan(sum)&&!std::isfinite(value+inherit[i])))return false;}
 unsigned target=0,extra=0;for(unsigned i=0;i<32;++i){target|=sample.target[i];extra|=sample.extra[i];}
 return (target ? At<unsigned>(sample.target,20)==0 : sample.lock==0) && (extra ? At<unsigned>(sample.extra,20)==0 : !sample.extraObject);
}
bool CaptureNativeProjectileSample(const NativeLease&,const void*,const float* matrix,const void* descriptor,NativeProjectileSample* out) noexcept {if(!out||!matrix||!descriptor)return false;*out={};std::memcpy(out->matrix,matrix,64);out->hasMuzzle=true;const auto point=At<const float*>(descriptor,0x70);if(!point)return false;std::memcpy(out->muzzleMatrix,point,64);out->shot=At<unsigned>(descriptor,0x54);return ValidNativeProjectileSample(*out);}
bool RegisterNativeBeamStepProvider(const NativeBeamStepProvider& p) noexcept {registeredStep=p;return true;}
bool InstallNativeBeamStepDispatcher() noexcept {return true;}
bool NativeBeamStepProviderReady(const NativeBeamStepProvider& p) noexcept {return dispatchReady&&p.Owns==registeredStep.Owns&&p.Invoke==registeredStep.Invoke;}
void InvokeOriginalNativeBeamRefresh(void* bullet){++beamRefreshes;if(At<unsigned>(bullet,0xC30)!=1 || (At<unsigned>(bullet,0xC34)&0x4000u) || (static_cast<unsigned char*>(bullet)[0x18]&4))return;Put<unsigned>(bullet,0xC34,At<unsigned>(bullet,0xC34)|0x2000u);}
void InvokeOriginalNativeBeamStep(void*,void*){++beamSteps;}
void* InvokeOriginalNativeUnitFactory(void*,const float*,void* factory,void*){++originalCreates;if(refuseCreate)return nullptr;unsigned key=0;const auto* stream=FactoryStream(factory,&key);const int index=stream?Index(stream->root.object):0;const unsigned i=index<0?0:static_cast<unsigned>(index);Put<unsigned>(results[i].object,0xC30,beamMode?1u:0u);return results[i].object;}
void* __fastcall Allocate(std::size_t bytes){auto* node=std::calloc(1,bytes);allocations.push_back(node);return node;}
void __fastcall Free(void* node,std::size_t){++freeCalls;if(throwFree)RaiseException(0xE0424301,0,0,nullptr);for(auto& p:allocations)if(p==node)p=nullptr;std::free(node);}
void* __fastcall Cast(void* scene,long,const void*,const void*,int){return scene;}
void ReplaceCallbackCache(){
 if(!callbackCache)return;auto* old=At<unsigned char*>(callbackCache,0xB8);
 InterlockedIncrement(reinterpret_cast<volatile LONG*>(actors[2].control+12));Put<void*>(callbackCache,0xB0,actors[2].object);Put<void*>(callbackCache,0xB8,actors[2].control);
 if(old)InterlockedDecrement(reinterpret_cast<volatile LONG*>(old+12));
}
void __fastcall Delete(void* scene){++deleted;if(throwDelete)RaiseException(0xE0424302,0,0,nullptr);static_cast<unsigned char*>(scene)[0x18]|=4;
 if(swapOnDelete&&scene==callbackTrigger){swapOnDelete=false;ReplaceCallbackCache();}
}
void __fastcall MoveWeak(void* target,void* source){auto* old=At<unsigned char*>(target,8);if(old)InterlockedDecrement(reinterpret_cast<volatile LONG*>(old+12));std::memcpy(target,source,16);std::memset(source,0,16);}
void __fastcall StopSource(void* battery){
 ++nativeStops;if(swapOnStop){swapOnStop=false;ReplaceCallbackCache();}if(reenterStop){reenterStop=false;const int peer=Index(At<const void*>(battery,0x2B0));Input nested{};reenteredBlocked=peer>=0&&!Before(leases[peer],peer==0,&nested);}
 if(throwStop)RaiseException(0xE0424303,0,0,nullptr);
 Put<unsigned>(battery,0x2C0,At<unsigned>(battery,0x2C0)&~1u);Put<unsigned>(battery,0x2E0,0);Put<unsigned>(battery,0x2E8,0);
}
void __fastcall DeleteSourceCache(void* battery){
 ++nativeCacheDeletes;if(throwCacheDelete)RaiseException(0xE0424304,0,0,nullptr);
 auto* rows=At<unsigned char*>(battery,0x278);const auto count=At<std::size_t>(battery,0x288);
 for(std::size_t i=0;i<count;++i){auto* row=rows+i*0xD0;const Ref ref{At<unsigned char*>(row,0xB0),At<unsigned char*>(row,0xB8)};if(!ref.control||At<LONG>(ref.control,8)<=0)continue;
  if(!Pin(ref))continue;__try {Delete(ref.object);}__finally {Unpin(ref);}
 }
}
void* WalkingNetTestFunction(unsigned rva) noexcept {switch(rva){case 0x6D3EC0:return reinterpret_cast<void*>(&StopSource);case 0x6D4040:return reinterpret_cast<void*>(&DeleteSourceCache);case 0x12D85B0:return reinterpret_cast<void*>(&Allocate);case 0x12D85EC:return reinterpret_cast<void*>(&Free);case 0x12DA7AA:return reinterpret_cast<void*>(&Cast);case 0x118A1B0:return reinterpret_cast<void*>(&Delete);case 0x58EB40:return reinterpret_cast<void*>(&MoveWeak);default:return nullptr;}}
}
int main(int argc,char** argv){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);std::setvbuf(stdout,nullptr,_IONBF,0);using namespace crew;using namespace crew::enemy_native;image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2300000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!image)return 2;installed=false;CHECK(!InstallWalkingAcceptedActions());for(const auto& print:kNativePrints)std::memcpy(image+print.at,print.bytes,16);CHECK(InstallWalkingAcceptedActions());CHECK(registeredUnit.Owns&&registeredBeam.Owns&&registeredStep.Owns&&registeredExtension.Apply);dispatchReady=false;CHECK(!InstallWalkingAcceptedActions());dispatchReady=true;CHECK(InstallWalkingAcceptedActions());Codec();QueueAndApply();DrainAndRestore();Emitter();ExceptionalLifetime();BaselinePreparation();DrainOrdering();const char* trace=nullptr;const auto defaultAssets=(std::filesystem::path(__FILE__).parent_path()/"data"/"walking-net-trace-assets.tsv").string();const char* assets=defaultAssets.c_str();for(int i=1;i<argc;++i){if(!std::strcmp(argv[i],"--dump-trace")&&i+1<argc)trace=argv[++i];else if(!std::strcmp(argv[i],"--trace-assets")&&i+1<argc)assets=argv[++i];else{std::printf("unknown or incomplete argument: %s\n",argv[i]);return 2;}}if(trace)CHECK(DumpTrace(trace,assets));Reset();VirtualFree(image,0,MEM_RELEASE);image=nullptr;std::printf("Walking accepted stream: %u checks, %u failures\n",checks,failures);return failures?1:0;}
