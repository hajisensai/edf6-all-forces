// Actual installed DragonSmall hook-chain contract and EDF4.1 flame lifecycle,
// exercised inside a private fake native image. No game process is touched.
#include <windows.h>
namespace crew { ULONGLONG GetTickCount64() noexcept; }   // the production file's wall clock, faked below (a long pause)
#include "../src/edf41dragon.cpp"
#include "../src/enemy_control_weapon_aim.cpp"
#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace fixture {
int checks=0,ai=0,spawned=0,constructed=0,destroyed=0,played=0,dtors=0,soundStops=0;
unsigned dtorFlags=0;ULONGLONG fakeClock=1000;void(*shutdownFn)() noexcept=nullptr;
alignas(16) unsigned char object[0x1D40]{},clipRecord[0x160]{};
crew::Config config{};
crew::enemy_native::OwnsAiFn owns=nullptr;
crew::enemy_native::InputFn controlled=nullptr;
crew::enemy_native::SkillSeedFn onSeed=nullptr;
float damage=0;std::uint32_t seed=0;float lastMatrix[16]{};
bool aimOwned=false;float actualAim[3]={5,806,807};
float actualSpeed=0,actualSize=0;int actualLife=0;
void Check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
void Jump(std::size_t at,void* destination){unsigned char code[14]={0xFF,0x25,0,0,0,0};std::memcpy(code+6,&destination,8);std::memcpy(crew::image+at,code,sizeof(code));}
std::uint64_t __fastcall Ai(void*,void*){++ai;return 99;}
const crew::Node* __fastcall Key(void*,const wchar_t*){return nullptr;}
void* __fastcall Factory(void*,const wchar_t* name){Check(std::wcscmp(name,L"FlameBullet02")==0,"original source flame factory is retained");return reinterpret_cast<void*>(0x41);}
void* __fastcall Locator(void*,float* matrix){for(int i=0;i<16;++i)matrix[i]=(i%5==0) ? 1.0f : 0;matrix[12]=5;matrix[13]=6;matrix[14]=7;return matrix;}
void* __fastcall Construct(void* param){++constructed;std::memset(param,0,0x1E0);return param;}
void __fastcall Destroy(void*){++destroyed;}
void* __fastcall Weak(void* out,const void*){return out;}
void* __fastcall Spawn(void*,const float* matrix,void*,void* param){++spawned;std::memcpy(lastMatrix,matrix,64);std::memcpy(&damage,static_cast<char*>(param)+0x9C,4);std::memcpy(&seed,static_cast<char*>(param)+0x1A0,4);std::memcpy(&actualSpeed,static_cast<char*>(param)+0x94,4);std::memcpy(&actualLife,static_cast<char*>(param)+0x98,4);std::memcpy(&actualSize,static_cast<char*>(param)+0xC0,4);return nullptr;}
bool __fastcall Play(void*,int,int,int){++played;return true;}
std::uint64_t __fastcall NativeDtor(void* self,unsigned flags){++dtors;dtorFlags=flags;return reinterpret_cast<std::uint64_t>(self);}
}
ULONGLONG crew::GetTickCount64() noexcept {return fixture::fakeClock;}
namespace edf {
void* AllocateNearCode(const void*,const unsigned char*,std::size_t) noexcept{return nullptr;}
bool PatchCode(unsigned char*,const unsigned char*,const unsigned char*,std::size_t) noexcept{return false;}
bool PatchVtableSlot(void** slot,void* expected,void* replacement) noexcept {if(*slot!=expected)return false;*slot=replacement;return true;}
bool RedirectCall(unsigned char*,void*,void*,bool& changed) noexcept{changed=true;return true;}
bool Readable(const void* value,std::size_t size,bool write) noexcept {
 if(!value || !size)return false;MEMORY_BASIC_INFORMATION info{};
 if(!VirtualQuery(value,&info,sizeof(info)) || info.State!=MEM_COMMIT || (info.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
 const auto at=reinterpret_cast<std::uintptr_t>(value),end=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
 if(at>end || size>end-at)return false;
 return !write || (info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
}
namespace crew {
unsigned char* image=nullptr;
const Config& Cfg() noexcept{return fixture::config;}
void Log(const char*,...) noexcept {}
bool EnemyControlWeaponLock(const void*,void*) noexcept{return false;}
bool EnemyControlWeaponAim(const void* object,float* out) noexcept {if(!fixture::aimOwned || object!=fixture::object)return false;std::memcpy(out,fixture::actualAim,12);return true;}
}
// The fake object carries no SGO: the sound tables (edf41_dragon_se) are absent, so ConfigureSounds logs and turns the
// sounds off; nothing past the table lookup may run.
namespace crew::edf41c {
int keyLookups=0;
const SgoNode* Key(const void*,const wchar_t*) noexcept{++keyLookups;return nullptr;}
int Count(const SgoNode* array) noexcept{return array ? 0 : -1;}
const SgoNode* Child(const SgoNode*,int) noexcept{fixture::Check(false,"no sound table to walk");return nullptr;}
bool Float(const SgoNode*,float*) noexcept{fixture::Check(false,"no sound table to read");return false;}
bool Int(const SgoNode*,std::int32_t*) noexcept{fixture::Check(false,"no sound table to read");return false;}
bool Text(const SgoNode*,wchar_t*,std::size_t) noexcept{fixture::Check(false,"no sound table to read");return false;}
int BoneIndex(void*,const wchar_t*) noexcept{fixture::Check(false,"no sound bones without a table");return -1;}
bool BoneMatrix(void*,int,float*) noexcept{fixture::Check(false,"no sound point without a table");return false;}
bool PlaySe(const void*,const wchar_t*,const float*,SoundHandle*) noexcept{fixture::Check(false,"no sound without a table");return false;}
void MoveSound(SoundHandle&,const float*) noexcept{fixture::Check(false,"no sound to move");}
void StopSound(SoundHandle& handle,int) noexcept{if(handle.instance)++fixture::soundStops;handle=SoundHandle{};}
void AtGameShutdown(void(*fn)() noexcept) noexcept{fixture::shutdownFn=fn;}
bool FillSePreset(unsigned char*,const void*,const wchar_t*) noexcept{fixture::Check(false,"no preset without a table");return false;}
}
namespace crew::enemy_native {
bool DeiroiWeaponAimReady() noexcept{return false;}
bool WalkingAcceptedAimPoint(const void*,float*) noexcept{return false;}
bool WalkingGunAimPoint(const void*,float*) noexcept{return false;}
const NativeProfile* Profiles(std::size_t* count) noexcept {static const NativeProfile p{"DragonSmall",0x17ABA40,0x4A8FD0,0x334930,1,59,0x70,Kind::insect,7,4};if(count)*count=1;return &p;}
bool RegisterControlledAiExtension(std::uint32_t vt,std::uint32_t original,OwnsAiFn owns,InputFn tick,SkillSeedFn seed) noexcept {
 fixture::Check(vt==0x17ABA40 && original==0x334930,"extension is scoped to the verified exact native class/AI");
 fixture::owns=owns;fixture::controlled=tick;fixture::onSeed=seed;return true;
}
}
int main(){
 using namespace fixture;using namespace crew;
 image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));Check(image!=nullptr,"private test native image allocated");
 Jump(kTick,reinterpret_cast<void*>(&Ai));Jump(kKeyNode,reinterpret_cast<void*>(&fixture::Key));
 Jump(kFactoryByName,reinterpret_cast<void*>(&Factory));Jump(kLocatorMatrix,reinterpret_cast<void*>(&Locator));
 Jump(kParamCtor,reinterpret_cast<void*>(&Construct));Jump(kParamDtor,reinterpret_cast<void*>(&Destroy));
 Jump(kWeakAssign,reinterpret_cast<void*>(&Weak));Jump(kSpawn,reinterpret_cast<void*>(&Spawn));Jump(kPlayState,reinterpret_cast<void*>(&Play));Jump(kDtor,reinterpret_cast<void*>(&NativeDtor));
 *reinterpret_cast<void**>(image+kBulletMgr)=reinterpret_cast<void*>(1);
 auto* table=reinterpret_cast<void**>(image+kVtable);table[kSlotTick]=image+kTick;table[kSlotDtor]=image+kDtor;config.edf41Ai=true;
 *reinterpret_cast<const void**>(image+kVtable-8)=image+0x2000;*reinterpret_cast<const void**>(object)=image+kVtable;
 crew::enemy_weapon_aim::ready=true;crew::enemy_weapon_aim::admittedImage=image;crew::enemy_weapon_aim::CacheProfileCols();
 Check(InstallEdf41Dragon() && owns && controlled && onSeed,"production install registers its exact own-chain lifecycle and seed callbacks");
 Check(table[kSlotDtor]==reinterpret_cast<void*>(&DtorHook) && shutdownFn==&ShutdownDragons,"the dragon's lifecycle ends its sounds: destructor slot wrapped, shutdown callback registered");
 Check(owns(table[kSlotTick]),"only the actual installed wrapper over the verified original is admitted");
 Check(!owns(image+kTick) && !owns(reinterpret_cast<void*>(&Ai)),"unknown hooks and bare native originals cannot impersonate the wrapper");
 const auto installed=table[kSlotTick];table[kSlotTick]=reinterpret_cast<void*>(&Ai);
 Check(!owns(installed),"later replacement of the global chain invalidates owned-hook admission");table[kSlotTick]=installed;
 Check(controlled(object,nullptr)==0 && ai==0 && spawned==0,"ordinary unmarked native DragonSmall never runs AI through the controlled extension");
 dragons[object]=Dragon{};auto& dragon=dragons[object];dragon.marked=dragon.ok=true;dragon.windowCount=1;dragon.windows[0]={99,3,20};dragon.rng=7;
 *reinterpret_cast<char**>(object+kCasAt+kPlayers)=reinterpret_cast<char*>(clipRecord);*reinterpret_cast<int*>(object+kMainPlayerAt)=0;
 *reinterpret_cast<int*>(clipRecord+kPlayerState)=99;*reinterpret_cast<float*>(clipRecord+kPlayerTime)=10;*reinterpret_cast<float*>(clipRecord+kPlayerLength)=100;
 *reinterpret_cast<float*>(object+kHpAt)=100;*reinterpret_cast<float*>(object+kLevelAt)=20;
 Check(onSeed(object,12345) && dragon.rng==12345,"approved skill seed reaches the actual EDF4.1 flame RNG without pointer/time remixing");
 Check(controlled(object,nullptr)==0 && ai==0 && spawned==1,"controlled wrapper retains the real MAB flame window but never autonomous AI");
 Check(constructed==1 && destroyed==1 && std::fabs(damage-0.075f)<1e-7f,"original bullet parameter construction/destruction and level damage remain intact");
 Check(lastMatrix[12]==5 && lastMatrix[13]==6 && lastMatrix[14]==7,"flame comes from the actual muzzle locator");
 const auto firstSeed=seed;float firstDirection[3]={lastMatrix[8],lastMatrix[9],lastMatrix[10]};
 onSeed(object,12345);controlled(object,nullptr);
 Check(seed==firstSeed && lastMatrix[8]==firstDirection[0] && lastMatrix[9]==firstDirection[1] && lastMatrix[10]==firstDirection[2],"same source skill seed produces the same actual flame direction and native bullet seed");
 *reinterpret_cast<float*>(clipRecord+kPlayerTime)=21;controlled(object,nullptr);Check(spawned==2,"outside the authored MAB window no flame is fabricated");
 *reinterpret_cast<float*>(clipRecord+kPlayerTime)=10;*reinterpret_cast<float*>(object+kHpAt)=0;controlled(object,nullptr);Check(spawned==2,"dead controlled dragon emits no flame");
 *reinterpret_cast<float*>(object+kHpAt)=100;dragon.glidePass=true;dragon.glideAttack=99;*reinterpret_cast<float*>(clipRecord+kPlayerTime)=100;
 controlled(object,nullptr);Check(!dragon.glidePass && played==1,"real glide-attack clip completion remains active under clipRecord control");
 *reinterpret_cast<float*>(clipRecord+kPlayerTime)=21;Check(TickHook(object,nullptr)==99 && ai==1,"uncontrolled wrapper still runs and forwards the original native AI exactly once");
 *reinterpret_cast<float*>(clipRecord+kPlayerTime)=10;aimOwned=true;onSeed(object,12345);const auto beforeAim=spawned;
 controlled(object,nullptr);
 const float elevation=lastMatrix[9],toward=(lastMatrix[9]+lastMatrix[10])*0.70710678118f;
 Check(spawned==beforeAim+1 && elevation>0.6f && toward>std::cos(kCone)-0.0001f,"actual FlameBullet02 cone follows a nonzero-pitch canonical aim through the production Breathe path");
 Check(lastMatrix[12]==5 && lastMatrix[13]==6 && lastMatrix[14]==7 && seed==firstSeed,"controlled flame preserves the actual muzzle and all three original RNG consumptions");
 Check(actualSpeed==dragon.speed && actualLife==dragon.life && actualSize==dragon.size && std::fabs(damage-0.075f)<1e-7f,"controlled flame preserves native source speed/lifetime/size and damage");
 aimOwned=false;onSeed(object,12345);controlled(object,nullptr);
 Check(lastMatrix[8]==firstDirection[0] && lastMatrix[9]==firstDirection[1] && lastMatrix[10]==firstDirection[2] && seed==firstSeed,"released/uncontrolled actual flame immediately returns to the unchanged native locator cone");
 // The breath's channel 0 is held: only se_stop, replacement, a state exit or the object's lifecycle ends it (edf41_sound_channels.h).
 const auto hold=[&](Dragon& d){d.channels.Start(0,[](BoneSound* sound){sound->handle.instance=reinterpret_cast<void*>(0x55);return true;},[](BoneSound& sound,int frames){edf41c::StopSound(sound.handle,frames);});};
 hold(dragon);const auto shotsBefore=spawned;fakeClock+=60000;   // a pause of a minute: no object updated, the wall clock went on
 controlled(object,nullptr);
 Check(dragons.size()==1 && &dragons.at(object)==&dragon && soundStops==0 && !dragon.channels.Empty() && spawned==shotsBefore+1,
       "a long pause keeps the same entry: its held breath is not cut and its clip's sounds are not fired again from a new timeline");
 alignas(16) static unsigned char stranger[0x1D40]{};*reinterpret_cast<const void**>(stranger)=image+0x100;
 {std::lock_guard<std::mutex> h(lock);Check(Find(stranger)==nullptr && dragons.count(stranger)==0,"an object of another vtable (whose destructor is not ours) gets no entry");}
 shutdownFn();Check(soundStops==1 && dragon.channels.Empty() && dragons.count(object)==1,"game shutdown stops every live dragon's held sounds before the sound system goes");
 hold(dragon);
 const auto dtor=reinterpret_cast<std::uint64_t(__fastcall*)(void*,unsigned)>(table[kSlotDtor]);
 Check(dtor(object,1)==reinterpret_cast<std::uint64_t>(object) && dtors==1 && dtorFlags==1,"the stock deleting destructor still runs once with its flags");
 Check(soundStops==2 && dragons.count(object)==0,"destroying the dragon (DeleteObject, despawn) stops its held breath and drops its entry");
 dtor(stranger,0);Check(dtors==2 && soundStops==2,"another object's destruction passes straight to the stock destructor");
 dragons.clear();VirtualFree(image,0,MEM_RELEASE);image=nullptr;
 std::printf("PASS %d actual dragon chain/flame checks\n",checks);
}
