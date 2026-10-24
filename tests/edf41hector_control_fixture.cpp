// Exercise the real EDF4.1 Hector callsite wrappers. Fake engine endpoints only;
// no copied AI rule, no game process or installed game files.
#include "../src/edf41ai.cpp"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
namespace fixture {
int checks=0,moves=0,faces=0,starts=0,bases=0;bool active=false,present=true;
alignas(16) unsigned char object[0x2000]{},other[0x2000]{},enemy[0x400]{},gun[2][0x30]{};
int weak=0;crew::Config config{};
float lastGoal[4]{},lastFace[4]{},lastSpeed=0;
void Check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
void Jump(std::size_t at,void* destination){unsigned char code[14]={0xFF,0x25,0,0,0,0};std::memcpy(code+6,&destination,8);std::memcpy(crew::image+at,code,sizeof(code));}
crew::Handle* __fastcall Child(const crew::Handle* parent,crew::Handle* out,const wchar_t* name){
 *out={};unsigned index=0;
 if(parent->tree==object+crew::kTreeAt && std::wcscmp(name,L"edf41_hector")==0)index=1;
 else if(parent->index==1){if(std::wcscmp(name,L"ranges")==0)index=2;else if(std::wcscmp(name,L"same_weapon")==0)index=3;else if(std::wcscmp(name,L"ai")==0)index=4;}
 if(index)*out={reinterpret_cast<void*>(1),index,0};return out;
}
int __fastcall Count(const crew::Handle* handle){return handle->index==2 ? 2 : handle->index==4 ? 7 : 0;}
std::uint32_t __fastcall Index(const crew::Handle* handle,std::uint32_t index){return handle->index==2 ? 20+index : 40+index;}
float __fastcall Number(const crew::Handle* handle){
 if(handle->index==20)return 100;if(handle->index==21)return 200;if(handle->index==3)return 0;
 const float ai[]={720,0,540,0,150,20,80};return handle->index>=40 && handle->index<47 ? ai[handle->index-40] : 0;
}
void* __fastcall Target(void*){return enemy;}
std::uint64_t __fastcall Base(void*,void*){++bases;return 73;}
bool __fastcall Move(void*,const float* goal,float,float,float speed,float){++moves;std::memcpy(lastGoal,goal,16);lastSpeed=speed;return true;}
void __fastcall Face(void*,const float* goal,float){++faces;std::memcpy(lastFace,goal,16);}
void __fastcall Start(void*){++starts;}
}
namespace edf {
bool Readable(const void* value,std::size_t,bool) noexcept {return value!=nullptr;}
bool RedirectCall(unsigned char*,void*,void*,bool& changed) noexcept {changed=true;return true;}
}
namespace crew {
unsigned char* image=nullptr;const Config& Cfg() noexcept{return fixture::config;}void Log(const char*,...) noexcept {}
bool EnemyControlTargetActive(const void* object) noexcept {return fixture::active && object==fixture::object;}
bool EnemyControlWeaponAim(const void* object,float* out) noexcept {
 if(!EnemyControlTargetActive(object) || !fixture::present || !out)return false;
 out[0]=777;out[1]=333;out[2]=555;return true; // canonical core/scenery result, not recomputed inside a gun hook
}
}
namespace crew::edf41c {
int BoneIndex(void*,const wchar_t*) noexcept {return -1;}
bool AimPoint(const void*,float* out) noexcept {out[0]=10;out[1]=100;out[2]=20;return true;}
}
int main(){
 using namespace fixture;using namespace crew;
 image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));Check(image!=nullptr,"private native image allocated");
 Jump(kChildByName,reinterpret_cast<void*>(&fixture::Child));Jump(kChildCount,reinterpret_cast<void*>(&Count));Jump(kChildIndex,reinterpret_cast<void*>(&Index));Jump(kNodeFloat,reinterpret_cast<void*>(&Number));
 Jump(kGetTarget,reinterpret_cast<void*>(&fixture::Target));Jump(kBaseUpdate,reinterpret_cast<void*>(&Base));Jump(kMoveTo,reinterpret_cast<void*>(&Move));Jump(kFaceTarget,reinterpret_cast<void*>(&Face));Jump(kGunStart,reinterpret_cast<void*>(&Start));
 const void* identity=&weak;std::memcpy(object+kSelfCtrl,&identity,8);object[kHasTargetAt]=1;
 *reinterpret_cast<float*>(enemy+kPosAt)=50;*reinterpret_cast<float*>(object+kAxisXAt)=1;*reinterpret_cast<float*>(object+kAxisZAt+8)=1;
 void* guns[]={gun[0],nullptr,gun[1],nullptr};
 *reinterpret_cast<void***>(object+kGunsAt)=guns;*reinterpret_cast<std::uint64_t*>(object+kGunCountAt)=2;
 *reinterpret_cast<void**>(gun[0]+kGunOwnerAt)=object;*reinterpret_cast<void**>(gun[1]+kGunOwnerAt)=object;
 Tick(object);auto* entry=Find(object);Check(entry!=nullptr && entry->haveTarget && entry->target[0]==50,"real source config and original AI target are read");
 entry->cycle.phase=hector::Phase::Retreat;entry->cycle.retreatLeft=7;entry->rng.s=123;const auto counter=entry->cycle.counter;
 active=true;
 Tick(object);Check(entry->cycle.retreatLeft==7 && entry->cycle.counter==counter && entry->rng.s==123,"controlled tick cannot advance backoff or pick fresh AI randomness");
 Check(entry->target[0]==777 && entry->target[1]==333 && entry->target[2]==555,"actual aimed arms use the same canonical core/scenery aim point without a second divergent ray");
 const float goal[4]={8,9,10,1};Check(!MoveToHook(object,goal,0,0,0.7f,0) && moves==0,"stale AI-only walk call cannot overwrite native player movement with retreat");
 FaceHook(object,goal,0.1f);Check(faces==0,"stale AI-only face call cannot replace player yaw with enemy target");
 entry->pickTick=entry->tick;entry->pickMask=0;GunStartHook(gun[0]);GunStartHook(gun[1]);
 Check(starts==2 && entry->pickMask==0,"both player-selected real arms fire despite an AI mask that would suppress both");
 const auto result=BaseUpdateHook(object,nullptr);Check(result==73 && bases==1 && entry->cycle.retreatLeft==7,"original base animation/weapon lifecycle still runs while AI cycle stays paused");
 present=false;Tick(object);Check(!entry->haveTarget && entry->cycle.retreatLeft==7,"pending/neutral control never falls back to autonomous target selection");
 present=true;active=false;Tick(object);Check(entry->cycle.retreatLeft==6 && entry->haveTarget && entry->target[0]==50,"exit immediately resumes original AI target and preserved backoff phase");
 Check(MoveToHook(object,goal,0,0,0.7f,0) && moves==1 && lastGoal[2]==-100 && lastSpeed==1,"exit restores the original true retreat steering");
 FaceHook(object,goal,0.1f);Check(faces==1 && lastFace[0]==50 && lastFace[1]==0,"exit restores target-facing AI");
 entry->pickTick=entry->tick;entry->pickMask=0;GunStartHook(gun[0]);Check(starts==2,"exit restores real AI arm selection instead of leaving a global bypass");
 *reinterpret_cast<void**>(gun[0]+kGunOwnerAt)=other;active=true;GunStartHook(gun[0]);
 Check(starts==3,"ordinary non-EDF4.1 gun ownership passes through unchanged");
 table.clear();VirtualFree(image,0,MEM_RELEASE);image=nullptr;std::printf("PASS %d production Hector control-isolation checks\n",checks);
}
