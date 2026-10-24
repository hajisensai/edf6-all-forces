// Production controller fixtures: engine calls are bounded stand-ins; the state machines themselves are included unchanged.
#ifdef MOTHER401_FIXTURE
#include "../src/edf41mother401.cpp"
#else
#include "../src/edf41mother.cpp"
#endif
#include "../src/enemy_control_logic.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace fixture {const void* applied[256]{};unsigned appliedCount=0;int appliedTeam=0;
const void* owned[8]{};int original[8]{};unsigned ownedCount=0;
const void* newborn[8]{};unsigned newbornCount=0,newbornNext=0;
struct Adopted {crew::ObjRef ref{};int team=0,last=0;};Adopted adopted[8]{};unsigned adoptedCount=0;
std::uint64_t reservedToken=0,tokenSerial=0;crew::ObjRef reservedParent{};unsigned reservedCount=0;
bool spawnSupported=true,adoptAllowed=true,temporaryTeam=true,baselineKnown=true;int expectedBirthTeam=1,inheritedTeam=2;unsigned spawnBudget=16,preparedCalls=0,finishedCalls=0,activations=0;
int checks=0,shots=0,spawns=0,sent=0,fireTeam=-99;const void* fireOwner=nullptr;bool online=false,guest=false;const wchar_t* factory=nullptr;float damage=0;}
namespace crew {
unsigned char* image=nullptr;Config testConfig{};
const Config& Cfg() noexcept{return testConfig;}
void Log(const char*,...) noexcept{}
bool RegisterControlledPvpSpawnResolver(bool(*)(const wchar_t*,controlled_pvp::SpawnProof*) noexcept) noexcept{return true;}
}
namespace edf {
bool RedirectCall(unsigned char*,void*,void*,bool&) noexcept{return false;}
bool Readable(const void* p,std::size_t n,bool) noexcept {MEMORY_BASIC_INFORMATION mbi{};if(!p || !VirtualQuery(p,&mbi,sizeof(mbi)) || mbi.State!=MEM_COMMIT || (mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;return n<=reinterpret_cast<std::uintptr_t>(mbi.BaseAddress)+mbi.RegionSize-reinterpret_cast<std::uintptr_t>(p);}
}
namespace crew {
bool CameraRay(float*,float*) noexcept{return false;}
ULONGLONG GameFrame() noexcept{return 0;}
unsigned char* PlayerHuman() noexcept{return nullptr;}
bool VisitEnemiesOf(int,void(*)(void*,const void*,const float*),void*) noexcept{return false;}
}
namespace crew::edf41c {
bool NearestPlayer(const float*,float*) noexcept{return false;}
float Difficulty(float value) noexcept{return value;}
bool ModelMatrix(void*,const float*) noexcept{return true;}
bool FarRender(void*) noexcept{return true;}
bool WeakOut(const void*,void*) noexcept{return true;}
bool FireHit(void*,const float*,const Bullet& bullet,const wchar_t*,const SeParams&) noexcept{++fixture::shots;fixture::factory=bullet.factory;fixture::damage=bullet.damage;return true;}
int MaterialAnimsLoad(MaterialAnims&,void*,const SgoNode*,const int*,int) noexcept{return 0;}
void MaterialAnimsPlay(MaterialAnims&,int,int) noexcept{}
void MaterialAnimsUpdate(MaterialAnims&,void*) noexcept{}
void MaterialAnimsFree(MaterialAnims&) noexcept{}
void StencilLight(const float*,const float*,float,int) noexcept{}
void SmallBurst(const float*,const float*,float) noexcept{}
bool ShuttingDown() noexcept{return false;}

bool AddFactory(const wchar_t*,CreateFn) noexcept{return false;}
bool BuildClone(Clone&,DtorFn,UpdateFn,MessageFn) noexcept{return false;}
void* CloneObject(Clone&,std::size_t,std::size_t,void*) noexcept{return nullptr;}
void DeleteObject(void* o) noexcept{static_cast<unsigned char*>(o)[kFlagsAt]|=4;}
bool Alive(const void* o) noexcept{return o && !(static_cast<const unsigned char*>(o)[kFlagsAt]&4) && *reinterpret_cast<const float*>(static_cast<const unsigned char*>(o)+kDurabilityAt)>0;}
void Multiply(const float*,const float*,float*) noexcept{}
void EulerMatrix(const float*,const float*,float*) noexcept{}
void SetFlag(void*,std::uint32_t,bool) noexcept{}
float LevelS(const void*) noexcept{return 1;}
void SizeDurability(void*) noexcept{}
int HitBody(void*,const void*) noexcept{return -1;}
int RagdollBody(void*,const wchar_t*) noexcept{return -1;}
void SetSlot(Clone&,int,void*) noexcept{}
void AllowAnyTeam(Clone&) noexcept{}
int StateIndex(void*,const wchar_t*) noexcept{return -1;}
bool ClipNow(const void*,int*,float*,float*) noexcept{return false;}
bool GroundHit(const float*,const float*,float*,void*) noexcept{return false;}
void* AddLockPoint(void*) noexcept{return nullptr;}
void SetLockPoint(void*,const float*) noexcept{}
void Smoke(const float*,float) noexcept{}
void AtGameShutdown(void(*)() noexcept) noexcept{}
const std::uint32_t kEaoSlots[45]={},kCarrierSlots[49]={};
bool WeakAlive(const WeakRef& w) noexcept{return w.object && !(static_cast<const unsigned char*>(w.object)[kFlagsAt]&4);}
void WeakSet(WeakRef& w,const void* o) noexcept {w.object=const_cast<void*>(o);w.control=o ? const_cast<void*>(At<const void*>(o,kSelfCtrl)) : nullptr;}
void WeakReset(WeakRef& w) noexcept {w={};}
void StopSound(SoundHandle& h,int) noexcept {h={};}
void MoveSound(SoundHandle&,const float*) noexcept{}
bool PlaySe(const void*,const wchar_t*,const float*,SoundHandle*) noexcept{return true;}
bool PlayCue(const wchar_t*,const float*,SoundHandle*,const SeParams&) noexcept{return true;}
bool PlayState(void*,const wchar_t*) noexcept{return true;}
bool AnimDone(const void*,float* len) noexcept{if(len)*len=150;return true;}
const SgoNode* Key(const void*,const wchar_t*) noexcept{return nullptr;}
const SgoNode* Child(const SgoNode*,int) noexcept{return nullptr;}
int Count(const SgoNode*) noexcept{return -1;}
bool Text(const SgoNode*,wchar_t*,std::size_t) noexcept{return false;}
bool Float(const SgoNode*,float*) noexcept{return false;}
bool Int(const SgoNode*,std::int32_t*) noexcept{return false;}
bool Vec(const SgoNode*,float*,int) noexcept{return false;}
const SgoNode* Persist(const SgoNode*) noexcept{return nullptr;}
float Random01(std::uint64_t& rng) noexcept{rng=rng*6364136223846793005ull+1;return float((rng>>40)&0xFFFFFF)/16777216.0f;}
bool Fire(void* owner,const float*,const Bullet& bullet) noexcept{fixture::fireOwner=owner;fixture::fireTeam=At<int>(owner,kTeamAt);++fixture::shots;fixture::factory=bullet.factory;fixture::damage=bullet.damage;return true;}
void AimMatrix(const float* from,const float*,float* out) noexcept{std::memset(out,0,64);out[0]=out[5]=out[10]=out[15]=1;std::memcpy(out+12,from,12);}
int BoneIndex(void*,const wchar_t*) noexcept{return 0;}
bool BoneMatrix(void*,int,float* out) noexcept{std::memset(out,0,64);out[0]=out[5]=out[10]=out[15]=1;return true;}
void* Target(void*) noexcept{return nullptr;}
bool AimPoint(const void*,float*) noexcept{return false;}
bool FindLocator(void*,const wchar_t*,LocatorHandle*) noexcept{return false;}
bool LocatorMatrix(const LocatorHandle&,float*) noexcept{return false;}
void FireBurst(const float*,const float*,float) noexcept{}
void Blazing(const float*,const float*,float) noexcept{}
void Explosion(const float*,const float*,float) noexcept{}
void BigExplosion(const float*,const float*,float,float) noexcept{}
void* CreateFromPath(const float*,const wchar_t*) noexcept{++fixture::spawns;return fixture::newbornNext<fixture::newbornCount ? const_cast<void*>(fixture::newborn[fixture::newbornNext++]) : nullptr;}
bool SetTeam(void* owner,std::int32_t team,bool registered) noexcept{*reinterpret_cast<int*>(static_cast<unsigned char*>(owner)+kTeamAt)=team;*reinterpret_cast<int*>(static_cast<unsigned char*>(owner)+0x318)=team;auto& flags=*reinterpret_cast<std::uint32_t*>(static_cast<unsigned char*>(owner)+kFlags380);flags=registered ? flags|0x40u : flags&~0x40u;return true;}
void SetLevel(void*,float) noexcept{}
void Activate(void* object) noexcept{++fixture::activations;if(At<int>(object,kTeamAt)!=fixture::inheritedTeam)std::exit(3);}
void DevShoot(const void*,const float*,unsigned,float) noexcept{}
}
namespace crew::controlled_pvp {
bool ReadAppliedTeam(const ObjRef& member,int* team) noexcept {if(!team || !fixture::appliedTeam)return false;for(unsigned i=0;i<fixture::appliedCount;++i)if(fixture::applied[i]==member.obj){*team=fixture::appliedTeam;return true;}return false;}
bool ReadBaseline(const ObjRef&,const ObjRef&,int*,int*) noexcept{return false;}
bool HasTemporaryTeam(const ObjRef& ref) noexcept {if(!fixture::temporaryTeam)return false;for(unsigned i=0;i<fixture::ownedCount;++i)if(fixture::owned[i]==ref.obj)return true;return false;}
bool ReadBaselineForMember(const ObjRef& ref,int* team,int* last) noexcept {
    if(!team || !last || !fixture::baselineKnown)return false;
    for(unsigned i=0;i<fixture::ownedCount;++i)if(fixture::owned[i]==ref.obj){*team=*last=fixture::original[i];return true;}
    return false;
}
}
namespace crew::edf41net {
void Attach(void*) noexcept{}
void Pump(void*) noexcept{}
bool Enabled() noexcept{return true;}
bool Online() noexcept{return fixture::online;}
bool Guest(const void*) noexcept{return fixture::online && fixture::guest;}
bool Registered(const void*) noexcept{return true;}
bool Send(void*,std::uint8_t,const void*,std::size_t) noexcept{++fixture::sent;return true;}
void Child(void*,void*,std::uint16_t,std::uint16_t,bool) noexcept{}
int Drain(const void*,Visit,void*) noexcept{return 0;}
}
namespace {
using namespace crew;using namespace crew::edf41c;
std::uint64_t NativeMessageFallback(void*,std::uint32_t,void*){return 123;}
void Check(bool ok,const char* what){++fixture::checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
struct Object {alignas(16) unsigned char data[0x2000]{};Object(){const void* head=data+0x1880;*reinterpret_cast<const void**>(data+0x550)=head;*reinterpret_cast<const void**>(data+0x1880)=head;*reinterpret_cast<const void**>(data+0x1888)=head;*reinterpret_cast<const void**>(data+kSelfCtrl)=data+0x1800;*reinterpret_cast<LONG*>(data+0x1808)=1;*reinterpret_cast<int*>(data+0x318)=1;float* m=reinterpret_cast<float*>(data+kMatrixAt);m[0]=m[5]=m[10]=m[15]=1;*reinterpret_cast<float*>(data+kDurabilityAt)=1000;}};
enemy_control::Input Press(std::uint32_t id,std::uint64_t sequence){enemy_control::Input in{};in.sequence=sequence;in.absoluteAim=true;in.aimYaw=0.02f;in.aimPitch=0;in.seed=12345+sequence;in.active[0]={id,2};return in;}
bool SpawnCan(void*,const ObjRef&,const wchar_t* resource,unsigned count) noexcept {
    return fixture::spawnSupported && resource && resource[0] && count<=fixture::spawnBudget;
}
bool SpawnPrepare(void*,const ObjRef& parent,const wchar_t* path,unsigned count,std::uint64_t* token) noexcept {
    if(!token || !SpawnCan(nullptr,parent,path,count) || fixture::reservedToken)return false;
    fixture::reservedToken=++fixture::tokenSerial;fixture::reservedParent=parent;fixture::reservedCount=count;
    *token=fixture::reservedToken;++fixture::preparedCalls;return true;
}
bool SpawnAdopt(void*,std::uint64_t token,const ObjRef& parent,const ObjRef& child) noexcept {
    if(!fixture::adoptAllowed || token!=fixture::reservedToken || !fixture::reservedCount ||
       parent.obj!=fixture::reservedParent.obj || parent.ctrl!=fixture::reservedParent.ctrl || fixture::adoptedCount>=8)return false;
    auto& saved=fixture::adopted[fixture::adoptedCount++];saved={child,At<int>(child.obj,kTeamAt),At<int>(child.obj,0x318)};
    if(saved.team!=fixture::expectedBirthTeam || saved.last!=fixture::expectedBirthTeam)std::exit(4); // source original native initialization MUST precede adoption
    --fixture::reservedCount;SetTeam(const_cast<void*>(child.obj),fixture::inheritedTeam,true);return true;
}
void SpawnFinish(void*,std::uint64_t token) noexcept {
    if(token==fixture::reservedToken){fixture::reservedToken=0;fixture::reservedCount=0;++fixture::finishedCalls;}
}
void RestoreAdoptedFixture() {
    for(unsigned i=0;i<fixture::adoptedCount;++i){const auto& saved=fixture::adopted[i];SetTeam(const_cast<void*>(saved.ref.obj),saved.team,true);*reinterpret_cast<int*>(const_cast<unsigned char*>(static_cast<const unsigned char*>(saved.ref.obj))+0x318)=saved.last;}
}
#ifdef MOTHER401_FIXTURE
void Run(){
 Object hull,gun,finalGun,generator,battery,armour;Register(hull.data,Kind::hull);Register(gun.data,Kind::bigCannon);Register(finalGun.data,Kind::finalCannon);
 Register(generator.data,Kind::generator);Register(battery.data,Kind::rapid);Register(armour.data,Kind::panel01);
 for(auto* o:{gun.data,finalGun.data,generator.data,battery.data,armour.data}){*reinterpret_cast<const void**>(o)=&partClone.block[1];auto* p=Find(o);p->hull=Find(hull.data)->id;p->seen=true;p->arrived=true;p->level=10;p->team=1;SetTeam(o,1,true);SetupBullets(*p);}
 Find(battery.data)->panel=Find(armour.data)->id;
 auto& adapter=Edf41Mother401ControlAdapter();Check(!adapter.Match(hull.data)&&!adapter.Match(armour.data),"401 layout hull and inert armour are structural");
 controlled_pvp::Hooks pvp{};Check(adapter.PvpHooks&&adapter.PvpHooks(gun.data,&pvp),"401 supplies typed PVP hooks");
 ObjRef group[8]{};controlled_pvp::Emitter bindings[8]{};unsigned n=0,e=0;
 Check(pvp.collect(nullptr,ObjRef::Of(gun.data),group,8,&n,bindings,8,&e)&&n==1&&e==1&&group[0].obj==gun.data,"401 combat part is an isolated real firing tree");
 int cache=0;Check(pvp.readEmitter(nullptr,bindings[0],&cache)==controlled_pvp::Life::live&&cache==1,"401 mirror reads the actual private team field");
 const void* savedTable=*reinterpret_cast<const void**>(gun.data);*reinterpret_cast<const void**>(gun.data)=&hullClone.block[1];
 Check(pvp.readEmitter(nullptr,bindings[0],&cache)==controlled_pvp::Life::invalid,"401 emitter owner must remain the proven private native class");*reinterpret_cast<const void**>(gun.data)=savedTable;
 auto fake=bindings[0];fake.object=finalGun.data;Check(pvp.readEmitter(nullptr,fake,&cache)==controlled_pvp::Life::invalid,"401 rejects forged native-emitter pointer");
 Check(pvp.writeEmitter(nullptr,bindings[0],2),"401 mirror writes actual typed cache");SetTeam(gun.data,2,true);Find(battery.data)->team=2;SetTeam(battery.data,2,true);
 fixture::applied[0]=gun.data;fixture::applied[1]=battery.data;fixture::appliedCount=2;fixture::appliedTeam=2;
 auto& hullPart=*Find(hull.data);hullPart.team=1;SetTeam(hull.data,3,true);
 for(auto* object:{gun.data,finalGun.data,generator.data,armour.data}){Cell cell{};cell.panel=Find(object)->id;hullPart.cells.push_back(cell);}
 Part::Mount mount{};mount.battery=Find(battery.data)->id;Find(armour.data)->mounts.push_back(mount);
 HullLogic(hullPart);Check(Find(gun.data)->team==2&&*reinterpret_cast<int*>(gun.data+kTeamAt)==2&&Find(battery.data)->team==2,"401 exact PVP parts resist parent and panel team propagation");
 Check(Find(finalGun.data)->team==3,"401 siblings keep their actual hull relationship");
 fixture::appliedCount=0;fixture::appliedTeam=0;HullLogic(hullPart);
 Check(Find(gun.data)->team==3&&Find(battery.data)->team==3,"mode-off resumes a deferred parent change without needing a second hull team change");
 SetTeam(hull.data,1,true);HullLogic(hullPart);
 Check(!pvp.memberBaseline(nullptr,ObjRef::Of(gun.data),ObjRef::Of(finalGun.data),1,1,&cache,&cache),"401 refuses unknown dynamic member baseline");
 auto& p=*Find(gun.data);p.killed=true;Check(pvp.readEmitter(nullptr,bindings[0],&cache)==controlled_pvp::Life::live,"401 allocated killed part remains in the restoration write set");p.killed=false;
 auto oldBinding=bindings[0];++oldBinding.incarnation;Check(pvp.readEmitter(nullptr,oldBinding,&cache)==controlled_pvp::Life::gone,"401 old cache incarnation is never reused");
 Object outsider;auto unknown=bindings[0];unknown.member=ObjRef::Of(outsider.data);Check(pvp.readEmitter(nullptr,unknown,&cache)==controlled_pvp::Life::invalid,"unknown live owner is invalid rather than gone");
 auto unreadable=bindings[0];unreadable.member.obj=reinterpret_cast<const void*>(0x1000);Check(pvp.readEmitter(nullptr,unreadable,&cache)==controlled_pvp::Life::invalid,"401 unreadable owner cannot count as gone");
 Object nativeChild;alignas(16) unsigned char link[0x18]{};const void* head=gun.data+0x1880;
 *reinterpret_cast<const void**>(link)=head;*reinterpret_cast<const void**>(link+8)=head;*reinterpret_cast<const void**>(link+16)=nativeChild.data;
 *reinterpret_cast<const void**>(gun.data+0x1880)=link;*reinterpret_cast<const void**>(gun.data+0x1888)=link;*reinterpret_cast<std::size_t*>(gun.data+0x558)=1;
 Check(!pvp.collect(nullptr,ObjRef::Of(gun.data),group,8,&n,bindings,8,&e)&&!n&&!e,"401 unknown native child fails instead of being hidden by a typed-root claim");
 *reinterpret_cast<const void**>(gun.data+0x1880)=head;*reinterpret_cast<const void**>(gun.data+0x1888)=head;*reinterpret_cast<std::size_t*>(gun.data+0x558)=0;
 Check(!pvp.collect(nullptr,ObjRef::Of(gun.data),group,0,&n,bindings,8,&e),"401 refuses zero tree capacity");
 Check(!pvp.collect(nullptr,ObjRef::Of(gun.data),group,8,&n,bindings,0,&e),"401 refuses zero emitter capacity");
 Part::Mount unexpected{};Find(gun.data)->mounts.push_back(unexpected);Check(!pvp.collect(nullptr,ObjRef::Of(gun.data),group,8,&n,bindings,8,&e),"401 cannot hide an unexpected structural child behind its isolated-root contract");Find(gun.data)->mounts.clear();
 p.state=sBcIdle;p.attack=true;p.countdown=0;
 Check(adapter.Match(gun.data)&&adapter.Begin(gun.data,7),"combat part admitted and leased");
 Check(!adapter.Begin(gun.data,8),"second owner refused");
 TickPanel(p);Check(p.state==sBcIdle,"autonomous main cannon decision suspended");
 Check(adapter.Apply(gun.data,Press(mother_control::Id401(0,static_cast<unsigned>(p.kind)),1),true),"main cannon input accepted");
 ControlTick401(p);Check(p.state==sBcOpen,"real main cannon hatch state entered");p.frames=2;TickPanel(p);TickPanel(p);Check(p.state==sBcCharge,"real relay enters real charge");
 p.frames=301;TickPanel(p);Check(p.state==sBcFire,"native five second charge preserved");p.frames=2;TickPanel(p);Check(fixture::shots==1&&std::wcscmp(fixture::factory,L"LaserBullet02")==0,"native laser bullet factory fired");
 fixture::online=true;fixture::guest=true;
 const mother_control::Command repeat{0,static_cast<unsigned>(p.kind),998,7};
 const auto rngBefore=p.rng;PartApply(&p,19,reinterpret_cast<const unsigned char*>(&repeat),sizeof(repeat));ControlTick401(p);
 Check(p.rng==rngBefore,"busy command retry cannot reseed the preceding live attack");
 Check(p.controlInbox.size()==1&&p.state==sBcFire&&fixture::sent==0,"busy guest keeps a host accepted attack without resending");
 p.state=sBcIdle;ControlTick401(p);Check(p.state==sBcOpen&&p.controlInbox.empty()&&fixture::sent==0,"guest replays host skill on its real idle gate");
 auto old=repeat;old.lease=6;PartApply(&p,19,reinterpret_cast<const unsigned char*>(&old),sizeof(old));Check(p.controlInbox.empty(),"previous lease commands cannot enter a new control lease");
 fixture::online=false;fixture::guest=false;p.state=sBcFire;
 Check(adapter.Restore(gun.data)&&adapter.Restore(gun.data),"restore idempotent");Check(p.state==sBcFire&&p.attack,"restore keeps active attack state and baseline AI flag");
 auto& f=*Find(finalGun.data);f.state=sFcIdle;Check(adapter.Abilities(finalGun.data,nullptr,0)==2&&adapter.Begin(finalGun.data,9),"both final cannon skills catalogued");
 Check(adapter.Apply(finalGun.data,Press(mother_control::Id401(1,static_cast<unsigned>(f.kind)),1),true),"lightning input");ControlTick401(f);Check(f.state==sFcLightCharge,"lightning charge selected explicitly");
 f.frames=301;TickPanel(f);f.frames=16;TickPanel(f);Check(fixture::shots==2,"lightning real bullet fired");adapter.Restore(finalGun.data);
 auto& g=*Find(generator.data);g.state=sGenIdle;g.gen.needTrigger=true;GenCfg cfg;wcscpy_s(cfg.path,L"app:/Object/authored.sgo");cfg.type=0;cfg.count=2;cfg.delay=90;
 SetGenerator(g,cfg);Check(adapter.Begin(generator.data,10),"configured generator leased");TickPanel(g);Check(g.state==sGenIdle,"autonomous generator open suspended");
 Check(adapter.Apply(generator.data,Press(mother_control::Id401(0,static_cast<unsigned>(g.kind)),1),true),"generator input");ControlTick401(g);TickPanel(g);Check(g.state==sGenOpen,"real generator hatch opens");g.frames=2;TickPanel(g);RunGenerator(g);RunGenerator(g);RunGenerator(g);
 Check(fixture::spawns==2&&!g.gen.active&&g.gen.hasPersist,"one authored wave completes without automatic refill");adapter.Restore(generator.data);Check(g.gen.active&&g.gen.cur.type==0,"standing generator resumes without replacing weak slots");
 auto& b=*Find(battery.data);b.arrived=false;b.state=sGbIdle;Check(adapter.Begin(battery.data,11),"mounted battery leased without an independent arrival flag");
 fixture::online=true;fixture::guest=true;Check(adapter.Apply(battery.data,Press(mother_control::Id401(0,static_cast<unsigned>(b.kind)),1),true),"guest input accepted");ControlTick401(b);Check(b.state==sGbIdle&&fixture::sent==0,"guest does not decide or resend");
 fixture::guest=false;Check(adapter.Apply(battery.data,Press(mother_control::Id401(0,static_cast<unsigned>(b.kind)),2),true),"host input accepted");ControlTick401(b);Check(b.state==sGbCharge&&fixture::sent==1,"host enters real rapid charge and emits one semantic event");
 fixture::online=false;fixture::guest=false;
 adapter.Restore(generator.data);g.state=sGenWait;g.gen.waveDone=true;g.gen.active=true;g.gen.cur=g.gen.persist;g.gen.remaining=2;g.gen.delay=1;
 Check(adapter.Begin(generator.data,12)&&!g.controlledWave,"a completed generator's wait/close is not a pending wave");
 RunGenerator(g);g.state=sGenIdle;TickPanel(g);Check(g.state==sGenIdle&&fixture::spawns==2,"completed wave cannot open a fresh autonomous wave during control");
 Find(hull.data)->dying=true;adapter.Restore(generator.data);Check(!adapter.Match(generator.data),"dying hull cannot expose new combat decisions");Check(!adapter.Apply(battery.data,enemy_control::Input{},false),"parent death faults control immediately even while target part remains allocated");
 b.killed=true;Check(adapter.Restore(battery.data)&&b.killed,"restore cannot resurrect a destroyed battery");
}
#else
void Run(){
 Object hull,big,cannon,panel,hatch,cover;*reinterpret_cast<const void**>(hull.data)=&hullClass.block[1];Track(hull.data);Hull& h=hulls[hull.data];h.built=true;h.moveInit=true;h.yaw=0;*reinterpret_cast<float*>(hull.data+kMoveSpeed)=4;
 for(auto entry:{std::pair<void*,int>{big.data,kBigCannon},{cannon.data,kCannon},{panel.data,kPanel},{hatch.data,kHatch},{cover.data,kCover}}){Track(entry.first);*reinterpret_cast<const void**>(entry.first)=&partClass.block[1];Part& p=parts[entry.first];p.hull=hull.data;p.group=entry.second;p.enabled=true;p.attack=true;p.index=0;p.team=1;SetTeam(entry.first,1,true);h.parts.push_back(entry.first);}
 parts[big.data].state=kBigStandby;parts[cannon.data].state=kCanWait;parts[panel.data].state=kPanStart;parts[hatch.data].state=kHatClosed;
 Check(!parts[hatch.data].configuredOnce&&parts[hatch.data].configured.path[0]==0,"unconfigured hatch path is explicitly empty");
 hullClass.orig[kSlotMessage]=reinterpret_cast<void*>(&NativeMessageFallback);testConfig.edf41MotherParts=0;
 Check(HullMessage(hull.data,kMsgGenerator,nullptr)==1,"disabled 4.1 generator messages cannot fall through to an unproved native carrier spawner");
 alignas(16) unsigned char message[0x50]{};const wchar_t* configuredPath=L"app:/Object/authored.sgo";
 *reinterpret_cast<int*>(message+8)=1;*reinterpret_cast<int*>(message+0x10)=1;*reinterpret_cast<int*>(message+0x14)=1;
 *reinterpret_cast<float*>(message+0x18)=1;*reinterpret_cast<const wchar_t**>(message+0x20)=configuredPath;
 *reinterpret_cast<std::uint64_t*>(message+0x30)=std::wcslen(configuredPath);*reinterpret_cast<std::uint64_t*>(message+0x38)=std::wcslen(configuredPath);*reinterpret_cast<int*>(message+0x48)=63;
 Check(HullMessage(hull.data,kMsgGenerator,message)==1,"a missing configured hatch is handled by the 4.1 generator boundary rather than native fallback");
 Wave wave{};wcscpy_s(wave.path,L"app:/Object/authored.sgo");wave.count=2;wave.active=true;wave.kind=0;AssignWave(h,0,wave);
 const auto& adapter=Edf41Mother301ControlAdapter();
 controlled_pvp::SpawnProof resourceProof{};testConfig.edf41MotherParts=31;
 Check(ResolveMother301Spawn(L"APP:/OBJECT/UFOMOTHER301.SGO",&resourceProof)&&resourceProof.maxMembers==145&&resourceProof.maxEmitters==144,"shipped 301 resource proof reserves its complete real family");
 Check(!ResolveMother301Spawn(L"app:/Object/UfoMother301_unknown.Sgo",&resourceProof),"resource proof never guesses a similar factory path");testConfig.edf41MotherParts=0;
 Check(ResolveMother301Spawn(L"app:/Object/UfoMother301.Sgo",&resourceProof)&&resourceProof.maxMembers==1&&!resourceProof.maxEmitters,"constructor configuration only removes proven groups from the bound");testConfig.edf41MotherParts=31;
 controlled_pvp::Hooks pvp{};Check(adapter.PvpHooks&&adapter.PvpHooks(cannon.data,&pvp),"301 supplies typed complete-family PVP hooks");
 ObjRef group[8]{};controlled_pvp::Emitter bindings[8]{};unsigned n=0,e=0;SetTeam(hull.data,3,true);
 const auto beforeTeam=parts[cannon.data].team;
 Check(pvp.collect(nullptr,ObjRef::Of(cannon.data),group,8,&n,bindings,8,&e)&&n==6&&e==5&&group[0].obj==cannon.data,"selected 301 part stays first while hull and every real sibling are collected");
 Check(parts[cannon.data].team==beforeTeam&&*reinterpret_cast<int*>(hull.data+kTeamAt)==3,"301 collect is read-only");
 Check(!pvp.collect(nullptr,ObjRef::Of(cannon.data),group,2,&n,bindings,8,&e),"301 tree refuses insufficient capacity rather than truncating");
 Check(pvp.collect(nullptr,ObjRef::Of(cannon.data),group,8,&n,bindings,8,&e),"301 complete tree remains collectable after capacity failure");
 Object nativeChild;alignas(16) unsigned char link[0x18]{};const void* head=hull.data+0x1880;
 *reinterpret_cast<const void**>(link)=head;*reinterpret_cast<const void**>(link+8)=head;*reinterpret_cast<const void**>(link+16)=nativeChild.data;
 *reinterpret_cast<const void**>(hull.data+0x1880)=link;*reinterpret_cast<const void**>(hull.data+0x1888)=link;*reinterpret_cast<std::size_t*>(hull.data+0x558)=1;
 Check(!pvp.collect(nullptr,ObjRef::Of(cannon.data),group,8,&n,bindings,8,&e)&&!n&&!e,"301 unknown native child cannot be omitted from the real family proof");
 *reinterpret_cast<const void**>(hull.data+0x1880)=head;*reinterpret_cast<const void**>(hull.data+0x1888)=head;*reinterpret_cast<std::size_t*>(hull.data+0x558)=0;
 Check(pvp.collect(nullptr,ObjRef::Of(cannon.data),group,8,&n,bindings,8,&e),"301 tree restores after the unrelated native child is removed");
 const auto originalParts=h.parts.size();std::vector<Object> authoredLarge(251);
 for(unsigned i=0;i<250;++i){void* object=authoredLarge[i].data;Track(object);*reinterpret_cast<const void**>(object)=&partClass.block[1];Part& extra=parts[object];extra.hull=hull.data;extra.group=kPanel;extra.team=1;h.parts.push_back(object);}
 ObjRef fullGroup[controlled_pvp::kMaxMembers]{};controlled_pvp::Emitter fullBindings[controlled_pvp::kMaxEmitters]{};unsigned fullCount=0,fullEmitted=0;
 Check(pvp.collect(nullptr,ObjRef::Of(cannon.data),fullGroup,controlled_pvp::kMaxMembers,&fullCount,fullBindings,controlled_pvp::kMaxEmitters,&fullEmitted)&&fullCount==256&&fullEmitted==255,"301 exact native collector capacity retains every actual family member");
 void* overflow=authoredLarge.back().data;Track(overflow);*reinterpret_cast<const void**>(overflow)=&partClass.block[1];Part& added=parts[overflow];added.hull=hull.data;added.group=kPanel;added.team=1;h.parts.push_back(overflow);
 Check(!pvp.collect(nullptr,ObjRef::Of(cannon.data),fullGroup,controlled_pvp::kMaxMembers,&fullCount,fullBindings,controlled_pvp::kMaxEmitters,&fullEmitted),"301 one member above the bounded tree capacity fails without truncation");
 h.parts.resize(originalParts);for(auto& object:authoredLarge){parts.erase(object.data);refs.erase(object.data);}
 int cache=0;Check(pvp.readEmitter(nullptr,bindings[0],&cache)==controlled_pvp::Life::live&&cache==1,"301 reads exact private mirror field");
 const void* savedTable=*reinterpret_cast<const void**>(big.data);*reinterpret_cast<const void**>(big.data)=&hullClass.block[1];
 Check(pvp.readEmitter(nullptr,bindings[0],&cache)==controlled_pvp::Life::invalid,"301 does not treat another native class as a private part emitter");*reinterpret_cast<const void**>(big.data)=savedTable;
 auto fake=bindings[0];fake.object=hull.data;Check(pvp.readEmitter(nullptr,fake,&cache)==controlled_pvp::Life::invalid,"301 rejects fake emitter pointer");
 for(unsigned i=0;i<n;++i){fixture::applied[i]=group[i].obj;SetTeam(const_cast<void*>(group[i].obj),2,true);}fixture::appliedCount=n;fixture::appliedTeam=2;
 for(unsigned i=0;i<e;++i)Check(pvp.writeEmitter(nullptr,bindings[i],2),"301 actual cache tagged");
 SetTeam(hull.data,-1,false);Masking(cannon.data,parts[cannon.data]);Check(parts[cannon.data].team==2&&*reinterpret_cast<int*>(cannon.data+kTeamAt)==2,"disabled hull's -1 to 1 mapping cannot undo authenticated PVP part team");
 CannonRocket(cannon.data,parts[cannon.data],1);Check(fixture::fireOwner==cannon.data&&fixture::fireTeam==2,"real cannon emission uses the current native owner team");fixture::shots=0;
 fixture::appliedCount=0;fixture::appliedTeam=0;SetTeam(hull.data,3,true);Masking(cannon.data,parts[cannon.data]);Check(parts[cannon.data].team==1,"mode-off restores original neutral hull mapping");
 for(unsigned i=0;i<e;++i)Check(pvp.writeEmitter(nullptr,bindings[i],1),"301 actual cache baseline restored");SetTeam(hull.data,1,true);
 Check(!pvp.memberBaseline(nullptr,ObjRef::Of(cannon.data),ObjRef::Of(hatch.data),1,1,&cache,&cache),"301 unknown added member cannot invent inherited baseline");
 parts[big.data].dead=true;Check(pvp.readEmitter(nullptr,bindings[0],&cache)==controlled_pvp::Life::live,"301 allocated dead part cache remains restorable");parts[big.data].dead=false;
 Object outsider;auto unknown=bindings[0];unknown.member=ObjRef::Of(outsider.data);Check(pvp.readEmitter(nullptr,unknown,&cache)==controlled_pvp::Life::invalid,"301 rejects unknown owner instead of declaring destruction");
 auto unreadable=bindings[0];unreadable.member.obj=reinterpret_cast<const void*>(0x1000);Check(pvp.readEmitter(nullptr,unreadable,&cache)==controlled_pvp::Life::invalid,"301 unreadable owner is invalid rather than a deletion proof");
 Check(adapter.Match(hull.data)&&!adapter.Match(cover.data),"301 hull admitted and inert cover excluded");
 Check(adapter.Abilities(hull.data,nullptr,0)==22,"all script actions plus real weapon commands and authored hatch listed");
 enemy_control::Ability list[30]{};adapter.Abilities(hull.data,list,30);Check(list[12].state==enemy_control::AbilityState::unavailable&&list[17].state==enemy_control::AbilityState::unavailable,"unread legacy boost flags are not fake skills");
 Check(adapter.Begin(hull.data,5)&&!adapter.Begin(cannon.data,6),"hull control claims child decision gates");
 parts[hatch.data].spawnWave.fault=true;Check(adapter.Restore(cannon.data)&&parts[hatch.data].spawnWave.fault,"failed sibling acquisition cannot clear the active owner's spawn failure state");parts[hatch.data].spawnWave.fault=false;
 auto& p=parts[big.data];BigCannonTick(big.data,p);Check(p.state==kBigStandby,"autonomous main cannon fire stopped");
 auto input=Press(mother_control::Id301(7),1);input.move[2]=1;input.look[0]=0.5f;Check(adapter.Apply(hull.data,input,true),"hull controls accepted");ControlTick(hull.data);BigCannonTick(big.data,p);Check(p.state==kBigCharge,"native main cannon charge entered");
 Spin(hull.data,h);const auto chargingSeed=p.rng;Check(adapter.Apply(hull.data,Press(mother_control::Id301(0),2),true),"unrelated movement command accepted");ControlTick(hull.data);Check(p.rng==chargingSeed,"rotation input cannot reseed an ongoing weapon attack");
 p.frames=1200;BigCannonTick(big.data,p);p.frames=1;BigCannonTick(big.data,p);Check(fixture::shots==1&&fixture::damage==5000,"real native main cannon bullet contract");
 Spin(hull.data,h);Check(h.pos[2]>0&&h.vel[2]>0&&h.vel[2]<=2&&h.yaw>0,"movement uses real smoothed hull movement and manual yaw");
 Check(adapter.Apply(hull.data,Press(mother_control::Id301(19),3),true),"single rocket input");ControlTick(hull.data);Check(parts[cannon.data].state==kCanFire,"real medium cannon charge entered directly");
 Check(adapter.Apply(hull.data,Press(mother_control::Id301(mother_control::kSummon,0),4),true),"authored hatch summon input");ControlTick(hull.data);auto& hat=parts[hatch.data];Check(hat.controlledWave&&hat.current.kind==1&&hat.current.count==2,"only configured path/count reused");
 HatchTick(hatch.data,hat);hat.frames=2;HatchTick(hatch.data,hat);HatchTick(hatch.data,hat);HatchTick(hatch.data,hat);HatchTick(hatch.data,hat);Check(fixture::spawns==2&&!hat.current.active,"real generator emits exactly authored wave");
 Check(adapter.Restore(hull.data)&&adapter.Restore(hull.data),"restore idempotent");Check(parts[cannon.data].attack&&parts[cannon.data].enabled&&!parts[cannon.data].manualLease,"child AI baselines restored");Check(p.state==kBigFire,"restore leaves an active native fire lifecycle intact");
 Check(adapter.Begin(hull.data,77),"whole hull can be leased again");parts[cannon.data].dead=true;h.yawRate=h.yawTarget=0.0003f;
 const mother_control::Command staleCharge{19,mother_control::kAll,987,77},stopSpin{0,mother_control::kAll,988,77};
 CannonApply(hull.data,18,reinterpret_cast<const unsigned char*>(&staleCharge),sizeof(staleCharge));CannonApply(hull.data,18,reinterpret_cast<const unsigned char*>(&stopSpin),sizeof(stopSpin));ReplayControl301(hull.data);
 Check(h.controlInbox.empty()&&h.yawRate==0&&h.yawTarget==0,"a destroyed weapon's pending attack cannot block surviving hull commands");parts[cannon.data].dead=false;adapter.Restore(hull.data);
 float turned[16];RotY(1.57079632679f,turned);std::memcpy(cannon.data+kMatrixAt,turned,sizeof(turned));
 Check(adapter.Begin(cannon.data,8),"weapon part can be leased separately");parts[hatch.data].spawnWave.fault=true;
 Check(!adapter.Begin(hull.data,80)&&adapter.Restore(hull.data)&&parts[hatch.data].spawnWave.fault,"failed whole-hull acquisition cannot clear an active part owner's wave state");parts[hatch.data].spawnWave.fault=false;Check(parts[cannon.data].control.yaw>1.5f,"direct part aim starts along matrix forward instead of mirrored side");fixture::online=true;fixture::guest=true;
 Check(adapter.Apply(cannon.data,Press(mother_control::Id301(20),1),true),"guest input admitted");ControlTick(cannon.data);Check(!parts[cannon.data].manualBeam&&fixture::sent==0,"guest never decides or resends");
 fixture::guest=false;Check(adapter.Apply(cannon.data,Press(mother_control::Id301(20),2),true),"host input admitted");ControlTick(cannon.data);Check(parts[cannon.data].manualBeam&&fixture::sent==1,"host beam decision emitted exactly once");
 fixture::guest=true;auto& can=parts[cannon.data];can.state=kCanFire;
 const mother_control::Command repeat{19,mother_control::kAll,999,8};CannonApply(cannon.data,18,reinterpret_cast<const unsigned char*>(&repeat),sizeof(repeat));NetPartIn(cannon.data,can);
 Check(can.controlInbox.size()==1&&can.state==kCanFire&&fixture::sent==1,"busy guest retains a host accepted real rocket charge");can.state=kCanWait;NetPartIn(cannon.data,can);
 Check(can.state==kCanFire&&can.controlInbox.empty()&&fixture::sent==1,"guest replays accepted charge without sending");
 auto old=repeat;old.lease=7;CannonApply(cannon.data,18,reinterpret_cast<const unsigned char*>(&old),sizeof(old));Check(can.controlInbox.empty(),"old lease command rejected");
 fixture::online=false;fixture::guest=false;StartDeath(hull.data,h);
 Check(!can.control.Active()&&!can.manualLease&&!can.manualBeam&&!can.attack,"hull death ends child input gates and sustained fire");
 adapter.Restore(cannon.data);adapter.Restore(hull.data);Check(!can.attack&&!parts[hatch.data].current.active,"release after hull death cannot restore attacks or summons");
 Check(!adapter.Begin(hatch.data,99),"dead hull cannot admit a hatch lease");Check(!adapter.Apply(cannon.data,enemy_control::Input{},false),"hull death faults child input before native deletion");
 can.dead=true;adapter.Restore(cannon.data);Check(can.dead,"release cannot resurrect dead cannon");
}
#endif
}
void RunSpawnOwnership(){
 using namespace crew;using namespace crew::edf41c;
 fixture::online=fixture::guest=false;fixture::appliedCount=fixture::ownedCount=0;fixture::appliedTeam=0;
 fixture::spawns=0;fixture::newbornCount=fixture::newbornNext=fixture::adoptedCount=fixture::activations=0;
 Object hull,source,first,second,rejected,modeOffBirth;
#ifdef MOTHER401_FIXTURE
 byObj.clear();byId.clear();partNets.clear();hullSerials.clear();
 *reinterpret_cast<const void**>(hull.data)=&hullClone.block[1];*reinterpret_cast<const void**>(source.data)=&partClone.block[1];
 Register(hull.data,Kind::hull);Register(source.data,Kind::generator);auto& part=*Find(source.data);
 part.hull=Find(hull.data)->id;part.seen=true;part.arrived=true;part.state=sGenIdle;part.gen.needTrigger=true;part.team=2;
 GenCfg configured{};wcscpy_s(configured.path,L"app:/Object/authored.sgo");configured.type=0;configured.count=2;configured.flag=true;SetGenerator(part,configured);
 const auto& adapter=Edf41Mother401ControlAdapter();const auto summon=mother_control::Id401(0,static_cast<unsigned>(Kind::generator));
#else
 hulls.clear();parts.clear();refs.clear();partNets.clear();
 *reinterpret_cast<const void**>(hull.data)=&hullClass.block[1];*reinterpret_cast<const void**>(source.data)=&partClass.block[1];Track(hull.data);Track(source.data);
 Hull& family=hulls[hull.data];family.built=true;family.moveInit=true;family.parts.push_back(source.data);
 auto& part=parts[source.data];part.hull=hull.data;part.group=kHatch;part.index=0;part.state=kHatClosed;part.team=2;
 Wave configured{};wcscpy_s(configured.path,L"app:/Object/authored.sgo");configured.kind=0;configured.count=2;configured.active=true;AssignWave(family,0,configured);
 const auto& adapter=Edf41Mother301ControlAdapter();const auto summon=mother_control::Id301(mother_control::kSummon,0);
 fixture::owned[1]=hull.data;fixture::original[1]=3;fixture::ownedCount=2;
#endif
 fixture::owned[0]=source.data;fixture::original[0]=1;if(!fixture::ownedCount)fixture::ownedCount=1;
 fixture::applied[0]=source.data;fixture::appliedCount=1;fixture::appliedTeam=2;SetTeam(hull.data,2,true);SetTeam(source.data,2,true);
 Check(!mother_control::SpawnWave::CanPrepare(ObjRef::Of(source.data),configured.path,2),"owned spawning cannot silently succeed before the native provider is bound");
 enemy_control::Ability spawnAbility{};Check(adapter.Abilities(source.data,&spawnAbility,1)==1&&spawnAbility.state==enemy_control::AbilityState::unavailable,"live owned generator catalogue marks an unproved spawn unavailable");
 MotherSpawnOwnership provider{nullptr,&SpawnCan,&SpawnPrepare,&SpawnAdopt,&SpawnFinish};
 Check(ConfigureMotherSpawnOwnership(provider)&&ConfigureMotherSpawnOwnership(provider),"native ownership provider registers once and repeats idempotently");
 auto wrong=provider;wrong.user=&provider;Check(!ConfigureMotherSpawnOwnership(wrong),"live spawn provider cannot be exchanged under held tokens");
 Check(adapter.Begin(source.data,499),"generator can validate an unknown native resource without constructing it");fixture::spawnSupported=false;
 Check(adapter.Apply(source.data,Press(summon,1),true),"unknown-resource summon edge enters the real controller gate");
#ifdef MOTHER401_FIXTURE
 ControlTick401(part);
#else
 ControlTick(source.data);
#endif
 Check(fixture::spawns==0&&part.spawnWave.fault,"unknown resource proof is refused before creating an enemy on either team");adapter.Restore(source.data);fixture::spawnSupported=true;
 Check(adapter.Begin(source.data,500),"controlled generator leases with no active automatic wave");fixture::spawnBudget=1;
 Check(adapter.Abilities(source.data,&spawnAbility,1)==1&&spawnAbility.state==enemy_control::AbilityState::unavailable,"live skill catalogue reports whole-wave capacity refusal before a press");
 Check(adapter.Apply(source.data,Press(summon,1),true),"summon edge reaches its actual controller");
#ifdef MOTHER401_FIXTURE
 ControlTick401(part);
#else
 ControlTick(source.data);
#endif
 Check(fixture::spawns==0&&part.spawnWave.fault,"whole-wave budget rejection happens before constructing any GOB");
 Check(!adapter.Apply(source.data,enemy_control::Input{},false),"unsupported spawn faults control instead of running with the enemy team");adapter.Restore(source.data);
 fixture::spawnBudget=16;fixture::adoptAllowed=true;
 Check(adapter.Begin(source.data,501),"generator can reacquire after restoring a rejected request");
 fixture::newborn[0]=first.data;fixture::newborn[1]=second.data;fixture::newbornCount=2;fixture::newbornNext=0;
 Check(adapter.Apply(source.data,Press(summon,1),true),"supported summon edge accepted");
#ifdef MOTHER401_FIXTURE
 ControlTick401(part);const auto oneShotToken=part.spawnWave.token;GenCfg persistentUpdate=configured;persistentUpdate.type=0;persistentUpdate.count=3;SetGenerator(part,persistentUpdate);
 Check(part.spawnWave.token==oneShotToken&&fixture::reservedToken==oneShotToken&&part.gen.cur.type==1,"persistent config updates do not release a running one-shot wave reservation");
 TickPanel(part);part.frames=2;TickPanel(part);RunGenerator(part);RunGenerator(part);RunGenerator(part);
#else
 ControlTick(source.data);HatchTick(source.data,part);part.frames=2;HatchTick(source.data,part);HatchTick(source.data,part);HatchTick(source.data,part);HatchTick(source.data,part);
#endif
 Check(fixture::preparedCalls==1&&fixture::adoptedCount==2&&fixture::activations==2,"one whole-wave reservation adopts every real birth before activation");
 Check(At<int>(first.data,kTeamAt)==2&&At<int>(second.data,kTeamAt)==2&&!fixture::reservedToken,"finished wave releases unused budget while adopted actors retain the player's team");
#ifndef MOTHER401_FIXTURE
 // The lease and its old saved hull=3 remain, but all temporary writes have been restored; a task then sets hull=4 before Frame.
 RestoreAdoptedFixture();fixture::temporaryTeam=false;fixture::appliedTeam=0;SetTeam(hull.data,4,true);int currentSpawnTeam=0;
 Check(OriginalSpawnTeam301(part,&currentSpawnTeam)&&currentSpawnTeam==4,"stable mode-off birth follows the task's current hull team even while the lease retains an old saved team");
 fixture::expectedBirthTeam=fixture::inheritedTeam=4;fixture::newborn[2]=modeOffBirth.data;fixture::newbornCount=3;fixture::newbornNext=2;
 part.current=configured;part.current.kind=1;part.current.count=1;part.current.active=true;part.remaining=1;part.permit=true;part.waveDone=false;part.controlledWave=true;part.delayLeft=part.intervalLeft=0;
 HatchTick(source.data,part);
 Check(fixture::adoptedCount==3&&fixture::adopted[2].team==4&&At<int>(modeOffBirth.data,kTeamAt)==4,"real mode-off SpawnOne captures and activates the newly changed native team before a generic Frame can refresh snapshots");
 fixture::temporaryTeam=true;fixture::baselineKnown=false;
 Check(!OriginalSpawnTeam301(part,&currentSpawnTeam),"temporary writes with an unresolved baseline refuse birth instead of treating the temporary team as original");
 fixture::baselineKnown=true;fixture::temporaryTeam=true;fixture::appliedTeam=2;fixture::expectedBirthTeam=1;fixture::inheritedTeam=2;SetTeam(hull.data,2,true);
#endif
 const auto successfulActivations=fixture::activations,successfulBodies=fixture::adoptedCount;
 Check(adapter.Restore(source.data),"source control can end while its summoned GOBs remain live");RestoreAdoptedFixture();
 Check(At<int>(first.data,kTeamAt)==1&&At<int>(second.data,kTeamAt)==1&&!(first.data[kFlagsAt]&4)&&!(second.data[kFlagsAt]&4),"lease restoration restores original GOB teams without deleting successful summons");
 part.spawnWave.Reset();fixture::adoptAllowed=false;fixture::newborn[successfulBodies]=rejected.data;fixture::newbornCount=successfulBodies+1;fixture::newbornNext=successfulBodies;
#ifdef MOTHER401_FIXTURE
 part.gen.remaining=0;GenCfg once=configured;once.type=1;once.count=1;
 Check(!SpawnOne(part,once,nullptr),"runtime typed-provider mismatch refuses a newly constructed generator GOB");
#else
 part.current=configured;part.current.kind=1;part.remaining=1;SpawnOne(source.data,part);
#endif
 Check((rejected.data[kFlagsAt]&4)&&fixture::activations==successfulActivations&&fixture::adoptedCount==successfulBodies&&part.spawnWave.fault,"unadopted new actor is removed before activation and never enters the successful weak write set");
 part.spawnWave.Reset();fixture::newbornCount=fixture::newbornNext=0;fixture::adoptAllowed=true;
#ifdef MOTHER401_FIXTURE
 part.gen.remaining=0;GenCfg missing=configured;missing.type=1;missing.count=1;Check(!SpawnOne(part,missing,nullptr),"failed native construction returns an explicit failure");
#else
 part.current=configured;part.remaining=1;SpawnOne(source.data,part);
#endif
 Check(part.spawnWave.fault&&fixture::adoptedCount==successfulBodies,"native construction failure faults an owned wave instead of silently claiming a partial summon");part.spawnWave.Reset();
#ifdef MOTHER401_FIXTURE
 part.gen.active=true;part.gen.remaining=1;part.gen.waveDone=false;part.gen.cur=configured;part.state=sGenOpen;
 *reinterpret_cast<float*>(source.data+kMatrixAt+32)=std::numeric_limits<float>::quiet_NaN();
 Check(!adapter.Begin(source.data,502)&&part.spawnWave.token,"a failed body capture may still own a prepared native wave budget");
 Check(adapter.Restore(source.data)&&!part.spawnWave.token&&!fixture::reservedToken,"restore releases preparation even when control capture never became active");
#endif
 fixture::ownedCount=fixture::appliedCount=0;fixture::appliedTeam=0;
}

int main(){Run();RunSpawnOwnership();std::printf("%d mother controller checks passed\n",fixture::checks);return 0;}
