// Actual installed DragonSmall hook-chain contract and EDF4.1 flame lifecycle,
// exercised inside a private fake native image. No game process is touched.
#include <windows.h>
namespace crew { ULONGLONG GetTickCount64() noexcept; }   // the production file's wall clock, faked below (a long pause)
#include "../src/edf41dragon.cpp"
#include "../src/enemy_control_weapon_aim.cpp"
#include "../src/edf41_persist.cpp"   // the real copy: Configure -> CopyOf -> Persist runs on built SGO trees below
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
// The object's SGO keys: none (an unmarked dragon) unless a Configure case below sets the two 4.1 keys.
const crew::Node* keyWindows=nullptr;const crew::Node* keyFlame=nullptr;int stateLookups=0;
const crew::Node* __fastcall Key(void*,const wchar_t* name){return std::wcscmp(name,L"edf41_dragon")==0 ? keyWindows : std::wcscmp(name,L"edf41_flame")==0 ? keyFlame : nullptr;}
int __fastcall StateByName(void*,int,const wchar_t*,bool){++stateLookups;return 42;}
// SGO trees laid out as the game's: 12-byte nodes, an array's children together at node + value, a string's characters at
// node + value. Each Configure case builds into a fresh region of `sgo`, so equal contents sit at different addresses.
alignas(16) unsigned char sgo[1<<20]{};std::size_t sgoUsed=0;
crew::Node* Nodes(int n){auto* p=reinterpret_cast<crew::Node*>(sgo+sgoUsed);sgoUsed+=sizeof(crew::Node)*static_cast<std::size_t>(n);Check(sgoUsed<sizeof(sgo),"SGO scratch is large enough");return p;}
std::int32_t Rel(const void* to,const void* from){return static_cast<std::int32_t>(static_cast<const unsigned char*>(to)-static_cast<const unsigned char*>(from));}
void SetInt(crew::Node* at,std::int32_t v){*at={1,0,v};}
void SetFloat(crew::Node* at,float v){at->type=2;at->count=0;std::memcpy(&at->value,&v,4);}
void SetText(crew::Node* at,const wchar_t* s){
 const auto n=std::wcslen(s);auto* t=reinterpret_cast<wchar_t*>(sgo+sgoUsed);sgoUsed=(sgoUsed+(n+1)*sizeof(wchar_t)+3)&~static_cast<std::size_t>(3);
 std::wmemcpy(t,s,n);*at={3,static_cast<std::int32_t>(n),Rel(t,at)};
}
crew::Node* SetArray(crew::Node* at,int n){crew::Node* kids=Nodes(n);*at={0,n,Rel(kids,at)};return kids;}
// Two trees with the same contents (strings compared by characters, arrays child by child).
bool Same(const crew::Node* a,const crew::Node* b){
 if(a->type!=b->type || a->count!=b->count)return false;
 if(a->type==3)return std::wmemcmp(reinterpret_cast<const wchar_t*>(reinterpret_cast<const char*>(a)+a->value),
                                   reinterpret_cast<const wchar_t*>(reinterpret_cast<const char*>(b)+b->value),static_cast<std::size_t>(a->count))==0;
 if(a->type!=0)return a->value==b->value;
 for(int i=0;i<a->count;++i)if(!Same(crew::Child(a,i),crew::Child(b,i)))return false;
 return true;
}
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
 // Configure on a marked object: the flame's custom parameter goes CopyOf -> edf41c::Persist (the real one). Persist's
 // limits replace the old dragon copy's (0..16 children, no strings): arrays up to 256 children, 8 levels below the root,
 // strings (an empty one too) copied. That is Persist as it was in edf41classes.cpp, moved, not changed: the real
 // Readable takes a zero-length range, and a negative length was refused there as well.
 Jump(kStateByName,reinterpret_cast<void*>(&StateByName));
 alignas(16) static unsigned char marked[0x1D40]{};
 const auto configure=[&](auto fill,Dragon& d){
  crew::Node* windows=Nodes(1);crew::Node* w=SetArray(windows,1);crew::Node* wk=SetArray(w,3);SetText(wk,L"attack_fire");SetFloat(wk+1,3);SetFloat(wk+2,20);
  crew::Node* flame=Nodes(1);crew::Node* fk=SetArray(flame,2);SetText(fk,L"FlameBullet02");crew::Node* body=SetArray(fk+1,3);
  SetInt(body,0);crew::Node* kv=SetArray(body+1,4);SetInt(kv,0);SetFloat(kv+1,3.5f);SetInt(kv+2,1);SetInt(kv+3,200);
  fill(body+2);
  keyWindows=windows;keyFlame=flame;const bool ok=Configure(marked,d);keyWindows=keyFlame=nullptr;
  return std::make_pair(ok,static_cast<const crew::Node*>(body+2));
 };
 const auto ints=[](int n){return [n](crew::Node* at){crew::Node* k=SetArray(at,n);for(int i=0;i<n;++i)SetInt(k+i,i*3+1);};};
 const auto chain=[](int levels){return [levels](crew::Node* at){for(int i=0;i<levels;++i)at=SetArray(at,1);SetInt(at,7);};};
 const auto inSgo=[](const void* p){return p>=static_cast<const void*>(sgo) && p<static_cast<const void*>(sgo+sizeof(sgo));};
 {
  Dragon d17{},again{},d256{},d257{},deep8{},deep9{},text{};
  const auto [ok17,src17]=configure(ints(17),d17);
  Check(ok17 && d17.custom && d17.custom!=src17 && !inSgo(d17.custom) && Same(d17.custom,src17) && d17.custom->count==17,
        "17 custom children (past the old copy's 16) reach Persist and come back as a process-lifetime copy with the same contents");
  Check(d17.speed==3.5f && d17.life==200 && d17.windowCount==1 && d17.windows[0].state==42 && stateLookups==2,
        "the same Configure read the flame's numbers and resolved its window and glide states");
  const auto [okAgain,srcAgain]=configure(ints(17),again);
  Check(okAgain && srcAgain!=src17 && again.custom==d17.custom,"the same contents at another address share one persisted copy");
  const auto [ok256,src256]=configure(ints(256),d256);
  Check(ok256 && d256.custom && d256.custom->count==256 && Same(d256.custom,src256),"256 children are the most a custom array may have");
  Check(!configure(ints(257),d257).first && d257.custom==nullptr,"257 children: no copy, the dragon is not configured");
  const auto [ok8,src8]=configure(chain(8),deep8);
  Check(ok8 && deep8.custom && Same(deep8.custom,src8),"a custom parameter nested 8 levels below its root is copied");
  Check(!configure(chain(9),deep9).first && deep9.custom==nullptr,"9 levels: no copy");
  const auto [okText,srcText]=configure([](crew::Node* at){crew::Node* k=SetArray(at,3);SetText(k,L"flame_custom");SetInt(k+1,5);SetText(k+2,L"");},text);
  Check(okText && text.custom && Same(text.custom,srcText) && !inSgo(reinterpret_cast<const char*>(crew::Child(text.custom,0))+crew::Child(text.custom,0)->value),
        "strings in the custom parameter (one empty) are copied with their characters (Persist always copied them; the dragon's old copy refused any)");
 }
 dragons.clear();VirtualFree(image,0,MEM_RELEASE);image=nullptr;
 std::printf("PASS %d actual dragon chain/flame checks\n",checks);
}
