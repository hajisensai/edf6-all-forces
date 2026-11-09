// Exercise the real EDF4.1 Hector callsite wrappers. Fake engine endpoints only;
// no copied AI rule, no game process or installed game files.
#include <windows.h>
namespace crew { ULONGLONG GetTickCount64() noexcept; }   // the production file's wall clock, faked below (a long pause)
#include "../src/edf41ai.cpp"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
namespace fixture {
int checks=0,moves=0,faces=0,starts=0,bases=0,created=0,deleted=0;bool active=false,present=true,marked=true,shieldKey=false;
ULONGLONG fakeClock=1000;const void* lastDeleted=nullptr;
alignas(16) unsigned char shieldObject[0x2000]{},bones[crew::kBoneSize]{},ghost[0x100]{},recent[0x100]{},ghostShield[0x100]{};
alignas(16) unsigned char object[0x2000]{},other[0x2000]{},enemy[0x400]{},gun[2][0x30]{};
int weak=0;crew::Config config{};
float lastGoal[4]{},lastFace[4]{},lastSpeed=0;
void Check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
void Jump(std::size_t at,void* destination){unsigned char code[14]={0xFF,0x25,0,0,0,0};std::memcpy(code+6,&destination,8);std::memcpy(crew::image+at,code,sizeof(code));}
crew::Handle* __fastcall Child(const crew::Handle* parent,crew::Handle* out,const wchar_t* name){
 *out={};unsigned index=0;
 if(parent->tree==object+crew::kTreeAt && marked && std::wcscmp(name,L"edf41_hector")==0)index=1;
 else if(parent->tree==object+crew::kTreeAt && shieldKey && std::wcscmp(name,L"edf41_hector_shield")==0)index=5;   // {arms: [1], sgo}
 else if(parent->index==5){if(std::wcscmp(name,L"arms")==0)index=6;else if(std::wcscmp(name,L"sgo")==0)index=7;}
 else if(parent->index==1){if(std::wcscmp(name,L"ranges")==0)index=2;else if(std::wcscmp(name,L"same_weapon")==0)index=3;else if(std::wcscmp(name,L"ai")==0)index=4;}
 if(index)*out={reinterpret_cast<void*>(1),index,0};return out;
}
int __fastcall Count(const crew::Handle* handle){return handle->index==2 ? 2 : handle->index==4 ? 7 : handle->index==6 ? 1 : 0;}
std::uint32_t __fastcall Index(const crew::Handle* handle,std::uint32_t index){return handle->index==2 ? 20+index : handle->index==6 ? 60+index : 40+index;}
const wchar_t* __fastcall Text(const crew::Handle* handle){return handle->index==7 ? L"app:/Object/HECTOR401_SHIELD_COLLISION.SGO" : nullptr;}
float __fastcall Number(const crew::Handle* handle){
 if(handle->index==60)return 1;   // the shield is on arms_r (gun_control entry 1)
 if(handle->index==20)return 100;if(handle->index==21)return 200;if(handle->index==3)return 0;
 const float ai[]={720,0,540,0,150,20,80};return handle->index>=40 && handle->index<47 ? ai[handle->index-40] : 0;
}
void* __fastcall Target(void*){return enemy;}
std::uint64_t __fastcall Base(void*,void*){++bases;return 73;}
bool __fastcall Move(void*,const float* goal,float,float,float speed,float){++moves;std::memcpy(lastGoal,goal,16);lastSpeed=speed;return true;}
void __fastcall Face(void*,const float* goal,float){++faces;std::memcpy(lastFace,goal,16);}
void __fastcall Start(void*){++starts;}
}
ULONGLONG crew::GetTickCount64() noexcept {return fixture::fakeClock;}
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
bool Particle(const float*,int,float,const float*,const wchar_t*) noexcept {return true;}
void StencilLight(const float*,const float*,float,int) noexcept {}
bool AimPoint(const void*,float* out) noexcept {out[0]=10;out[1]=100;out[2]=20;return true;}
// the shield (edf41ai Shields): made only once the fixture's object carries edf41_hector_shield; CreateFromPath stands for
// the factory (CreateShield registers the object), DeleteObject records what goes
const std::uint32_t kEaoSlots[45]{};
bool BuildClone(Clone&,DtorFn,UpdateFn,MessageFn) noexcept {return false;}
void SetSlot(Clone&,int,void*) noexcept {}
void AllowAnyTeam(Clone&) noexcept {}
bool AddFactory(const wchar_t*,CreateFn) noexcept {return false;}
void* CloneObject(Clone&,std::size_t,std::size_t,void*) noexcept {fixture::Check(false,"no shield object is made");return nullptr;}
void* CreateFromPath(const float*,const wchar_t* path) noexcept {
 fixture::Check(fixture::shieldKey && std::wcscmp(path,L"app:/Object/HECTOR401_SHIELD_COLLISION.SGO")==0,"a shield is asked for only by its edf41_hector_shield path");
 ++fixture::created;crew::shields[fixture::shieldObject]=crew::ShieldRec{};return fixture::shieldObject;
}
void DeleteObject(void* object) noexcept {++fixture::deleted;fixture::lastDeleted=object;}
void SetFlag(void*,std::uint32_t,bool) noexcept {}
bool SetTeam(void*,std::int32_t,bool) noexcept {return false;}
int RagdollLayer(void*) noexcept {return -1;}
bool SetRagdollLayer(void*,int) noexcept {return false;}
void WeakSet(WeakRef& w,const void* object) noexcept {w=WeakRef{const_cast<void*>(object),const_cast<void*>(object)};}
void WeakReset(WeakRef& w) noexcept {w=WeakRef{};}
bool WeakAlive(const WeakRef& w) noexcept {return w.object!=nullptr;}
}
int main(){
 using namespace fixture;using namespace crew;
 image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));Check(image!=nullptr,"private native image allocated");
 Jump(kChildByName,reinterpret_cast<void*>(&fixture::Child));Jump(kChildCount,reinterpret_cast<void*>(&Count));Jump(kChildIndex,reinterpret_cast<void*>(&Index));Jump(kNodeFloat,reinterpret_cast<void*>(&Number));
 Jump(kGetTarget,reinterpret_cast<void*>(&fixture::Target));Jump(kBaseUpdate,reinterpret_cast<void*>(&Base));Jump(kMoveTo,reinterpret_cast<void*>(&Move));Jump(kFaceTarget,reinterpret_cast<void*>(&Face));Jump(kGunStart,reinterpret_cast<void*>(&Start));Jump(kNodeText,reinterpret_cast<void*>(&fixture::Text));
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
 // The shield arm (edf41_hector_shield {arms: [1], sgo}): its gun never starts, whoever drives; its shield is made once,
 // kept through a pause and goes with the entry.
 *reinterpret_cast<void**>(gun[0]+kGunOwnerAt)=object;shieldKey=true;
 *reinterpret_cast<std::int32_t*>(bones+kBoneParentAt)=-1;for(int i=0;i<16;i+=5)reinterpret_cast<float*>(bones+kBoneWorldAt)[i]=1;
 reinterpret_cast<float*>(bones+kBoneWorldAt)[12]=31;
 *reinterpret_cast<unsigned char**>(object+kSkeletonAt+0x10)=bones;*reinterpret_cast<std::int64_t*>(object+kSkeletonAt+0x20)=1;
 entry->shieldRead=false;entry->bonesTried=true;entry->arm[1]=0;
 animState=1;active=false;entry->pickTick=entry->tick;entry->pickMask=hector::kArmLeft|hector::kArmRight;const int fired=starts;
 GunStartHook(gun[1]);animState=2;   // the SGO readers checked once (the fake image has no prints); head / lights stay off below
 Check(entry->shieldMask==2u && starts==fired,"AI-driven: the shield arm's gun never starts, even when the AI's pick includes it");
 active=true;GunStartHook(gun[1]);Check(starts==fired,"player-driven: the shield arm's gun still never starts");
 GunStartHook(gun[0]);Check(starts==fired+1,"player-driven: the other arm still fires");
 BaseUpdateHook(object,nullptr);BaseUpdateHook(object,nullptr);
 Check(created==1 && entry->shield[1].object==shieldObject && entry->shield[0].object==nullptr,"the shield is asked for once, on its arm only, not every frame");
 Check(reinterpret_cast<const float*>(shieldObject+edf41c::kMatrixAt)[12]==31,"the shield follows its arm bone's world matrix");
 fakeClock+=60000;BaseUpdateHook(object,nullptr);   // a pause of a minute: nothing ticked, the wall clock went on
 Check(Find(object)==entry && deleted==0 && created==1 && entry->shield[1].object==shieldObject,"a long pause keeps the entry and its shield (not deleted and made again)");
 table[ghost].seenAt=0;edf41c::WeakSet(table[ghost].shield[0],ghostShield);table[recent].seenAt=0x7000;
 ticks=0x7FFF;Tick(object);   // the sweep: every kStaleTicks Hector ticks
 Check(table.count(ghost)==0 && deleted==1 && lastDeleted==ghostShield,"an entry not ticked for kStaleTicks Hector ticks is swept with its shield");
 Check(table.count(recent)==1 && Find(object)==entry && entry->shield[1].object==shieldObject,"entries ticked within kStaleTicks stay, the live one's shield too");
 table.erase(recent);marked=false;Tick(object);
 Check(table.count(object)==0 && deleted==2 && lastDeleted==shieldObject,"a Hector that loses its marker drops its entry and deletes its shield");
 shields.clear();
 table.clear();VirtualFree(image,0,MEM_RELEASE);image=nullptr;std::printf("PASS %d production Hector control-isolation checks\n",checks);
}
