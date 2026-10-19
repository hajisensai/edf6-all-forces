// Direct production-controller routing: no game process. Engine seams record
// CAS and network requests; Body/Gun decisions are the real edf41robo.cpp code.
#include "../src/edf41robo.cpp"
#include "../src/pvp_controlled.cpp"
#include <cstdlib>
#include <limits>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fixture {
struct Packet {std::uint8_t kind;std::vector<unsigned char> bytes;};
std::vector<Packet> packets;
int checks=0,played=-1,netEvents=0,bullets=0;const crew::edf41c::Bullet* lastBullet=nullptr;const void* lastOwner=nullptr;bool online=false,enabled=true,registered=true,guest=false;
crew::Config config{};
alignas(16) unsigned char object[0xD00]{},parent[0x600]{};
std::vector<unsigned char> sgo;bool useRealSgo=false;int ownerTeam=2;
void __fastcall NativeTeam(void* value,int team,bool reg) noexcept {
 edf::Put<int>(value,edf::kTeam,team);if(team>=0)edf::Put<int>(value,0x318,team);
 const auto flags=edf::At<unsigned>(value,0x380);edf::Put<unsigned>(value,0x380,reg ? flags|0x40u : flags&~0x40u);
}

void Check(bool value,const char* why) {++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
}
namespace crew {
unsigned char* image=nullptr;
const Config& Cfg() noexcept {return fixture::config;}
void Log(const char*,...) noexcept {}
int PvpSupportTeam(const char*) noexcept {return fixture::ownerTeam;}
}
namespace edf {
bool Readable(const void* value,std::size_t size,bool) noexcept {
 const auto at=reinterpret_cast<std::uintptr_t>(value);
 const auto inside=[at,size](const void* data,std::size_t length) {
  const auto base=reinterpret_cast<std::uintptr_t>(data);return data && at>=base && at<=base+length && size<=base+length-at;
 };
 return inside(fixture::object,sizeof(fixture::object)) || inside(fixture::parent,sizeof(fixture::parent)) ||
        inside(fixture::sgo.data(),fixture::sgo.size());
}
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept {return true;}

}
namespace crew::edf41c {
const std::uint32_t kEaoSlots[45]{};
bool AddFactory(const wchar_t*,CreateFn) noexcept {return false;}
bool Ready() noexcept {return true;}
bool BuildClone(Clone&,DtorFn,UpdateFn,MessageFn) noexcept {return false;}
void* CloneObject(Clone&,std::size_t,std::size_t,void*) noexcept {return nullptr;}
// Use the shipped binary SGO's real node graph for the positive Setup test. Native CAS/model/sound calls remain explicit seams.
const SgoNode* Key(const void*,const wchar_t* name) noexcept {
 if(!fixture::useRealSgo || fixture::sgo.size()<0x20)return nullptr;
 const auto* data=fixture::sgo.data();const auto count=edf::At<unsigned>(data,0x10),offset=edf::At<unsigned>(data,0x14);
 for(unsigned i=0;i<count;++i) {
  const auto* entry=data+offset+i*8;const auto* key=reinterpret_cast<const wchar_t*>(entry+edf::At<int>(entry,0));
  if(!std::wcscmp(name,key))return reinterpret_cast<const SgoNode*>(data+edf::At<unsigned>(data,0xC)+edf::At<unsigned>(entry,4)*12);
 }
 return nullptr;
}
const SgoNode* Child(const SgoNode* array,int i) noexcept {
 return array && array->type==0 && i>=0 && i<array->a ?
  reinterpret_cast<const SgoNode*>(reinterpret_cast<const unsigned char*>(array)+array->b)+i : nullptr;
}
int Count(const SgoNode* array) noexcept {return array && array->type==0 ? array->a : -1;}
bool Float(const SgoNode* n,float* out) noexcept {
 if(!n || !out)return false;if(n->type==1){*out=static_cast<float>(static_cast<std::int32_t>(n->b));return true;}
 if(n->type!=2)return false;std::memcpy(out,&n->b,4);return true;
}
bool Int(const SgoNode* n,std::int32_t* out) noexcept {
 if(!n || !out)return false;if(n->type==1){*out=static_cast<std::int32_t>(n->b);return true;}
 float value=0;if(!Float(n,&value))return false;*out=static_cast<std::int32_t>(value);return true;
}
bool Vec(const SgoNode* n,float* out,int count) noexcept {
 if(Count(n)<count)return false;for(int i=0;i<count;++i)if(!Float(Child(n,i),out+i))return false;return true;
}
bool Text(const SgoNode* n,wchar_t* out,std::size_t capacity) noexcept {
 if(!n || n->type!=3 || n->a<0 || static_cast<std::size_t>(n->a)>=capacity)return false;
 std::memcpy(out,reinterpret_cast<const unsigned char*>(n)+n->b,static_cast<std::size_t>(n->a)*2);out[n->a]=0;return true;
}
const SgoNode* Persist(const SgoNode* node) noexcept {return fixture::useRealSgo ? node : nullptr;}
void* CreateFromPath(const float*,const wchar_t*) noexcept {return nullptr;}
bool Alive(const void*) noexcept {return true;}
void Multiply(const float* a,const float*,float* out) noexcept {std::memcpy(out,a,64);}
void AimMatrix(const float* from,const float*,float* out) noexcept {for(int i=0;i<16;++i)out[i]=(i%5==0) ? 1.0f : 0.0f;std::memcpy(out+12,from,12);}
bool NearestPlayer(const float*,float*) noexcept {return false;}
void DevShoot(const void*,const float*,std::uint32_t,float) noexcept {}
void SetFlag(void*,std::uint32_t,bool) noexcept {}
bool FindLocator(void*,const wchar_t*,LocatorHandle*) noexcept {return false;}
int BoneIndex(void*,const wchar_t*) noexcept {return -1;}
bool BoneMatrix(void*,int,float*) noexcept {return false;}
bool SetTeam(void* value,int team,bool registered) noexcept {fixture::NativeTeam(value,team,registered);return true;}
float LevelS(const void*) noexcept {return 1.0f;}
void SizeDurability(void*) noexcept {}
void* Target(void*) noexcept {return nullptr;}
bool AimPoint(const void*,float*) noexcept {return false;}
void MoveSound(SoundHandle&,const float*) noexcept {}
void Explosion(const float*,const float*,float) noexcept {}
void SetSlot(Clone&,int,void*) noexcept {}
void AllowAnyTeam(Clone&) noexcept {}
int StateIndex(void*,const wchar_t*) noexcept {return fixture::useRealSgo ? 1 : -1;}
bool GroundHit(const float*,const float*,float*,void*) noexcept {return false;}
bool CameraNow(float*,float*) noexcept {return false;}
void* AddLockPoint(void*) noexcept {return nullptr;}
void SetLockPoint(void*,const float*) noexcept {}
void Smoke(const float*,float) noexcept {}
void Quake(const float*,float,float) noexcept {}
void WeakSet(WeakRef&,const void*) noexcept {}
bool FireSe(void* owner,const float*,const Bullet& bullet,const void*,const wchar_t*) noexcept {++fixture::bullets;fixture::lastBullet=&bullet;fixture::lastOwner=owner;return true;}
bool FillSePreset(unsigned char*,const void*,const wchar_t*) noexcept {return false;}
void AtGameShutdown(void(*)() noexcept) noexcept {}

bool ShuttingDown() noexcept {return false;}
bool LocatorMatrix(const LocatorHandle&,float* out) noexcept {for(int i=0;i<16;++i)out[i]=(i%5==0) ? 1.0f : 0.0f;return true;}
bool WeakAlive(const WeakRef&) noexcept {return false;}
void WeakReset(WeakRef& ref) noexcept {ref={};}
void StopSound(SoundHandle& sound,int) noexcept {sound={};}
bool PlaySe(const void*,const wchar_t*,const float*,SoundHandle*) noexcept {return false;}
bool PlayStateIndex(void*,int state) noexcept {fixture::played=state;return true;}
bool ClipRewind(void*) noexcept {return true;}
bool ClipNow(const void*,int* state,float* time,float* length) noexcept {
 if(state)*state=fixture::played;if(time)*time=0;if(length)*length=100;return true;
}
void BigExplosion(const float*,const float*,float,float) noexcept {}
void DeleteObject(void*) noexcept {}
float Random01(std::uint64_t& state) noexcept {state=state*0x5D588B656C078965ull+0x269EC3ull;return static_cast<float>(state>>40)/16777216.0f;}
}
namespace crew::edf41net {
bool Guest(const void*) noexcept {return fixture::guest;}
void Attach(void*) noexcept {}
int Drain(const void*,Visit,void*) noexcept {return 0;}
bool Mirror() noexcept {return false;}
void Loopback(void*,void*) noexcept {}
Stats Counters() noexcept {return {};}
bool Send(void*,std::uint8_t kind,const void* payload,std::size_t size) noexcept {++fixture::netEvents;const auto* first=static_cast<const unsigned char*>(payload);fixture::packets.push_back({kind,{first,first+size}});return fixture::online;}
bool Online() noexcept {return fixture::online;}
bool Enabled() noexcept {return fixture::enabled;}
bool Registered(const void*) noexcept {return fixture::registered;}
bool IsMirror(const void*) noexcept {return false;}
}

int main() {
 using namespace crew;using namespace fixture;
 {
  enemy_legacy::Control frame;Check(frame.Begin(5,0),"pure control begins at native body yaw");
  enemy_control::Input sample{};sample.absoluteAim=true;sample.aimYaw=0.4f;sample.aimPitch=0.1f;sample.sequence=1;sample.look[0]=1;sample.look[1]=1;sample.active[0]={123,2};sample.seed=70;
  Check(frame.Apply(sample,true) && frame.pending==1 && frame.pressed[0].seed==70,"edge captures its exact seed once");
  enemy_legacy::Control other;Check(other.Begin(5,0) && other.Apply(sample,true),"second cadence begins from the same accepted sample");
  for(unsigned repeat=0;repeat<19;++repeat)Check(frame.Apply(sample,false),"duplicate sample can hold native movement without integrating aim again");
  Check(frame.pending==1 && frame.yaw==other.yaw && frame.pitch==other.pitch,"different native frame cadence gives exactly the same source aim and one press");
  const float position[3]={10,20,30};float aim[3]{};frame.Aim(position,100,aim);
  Check(aim[0]>position[0] && aim[1]>position[1],"aim matches camera yaw and positive-pitch convention");
  for(unsigned i=2;i<=64;++i){sample.sequence=i;Check(frame.Apply(sample,true),"all bounded pending edges retained");}
  const auto yaw=frame.yaw;sample.sequence=65;Check(!frame.Apply(sample,true) && frame.pending==64 && frame.sequence==64 && frame.yaw==yaw,"overflow is refused atomically without changing the accepted frame");
  Check(frame.Apply({},false) && frame.pending==0 && frame.sequence==64,"neutral clears latched input but retains deduplication identity");
  sample={};sample.move[0]=1;Check(!frame.Apply(sample,false),"sequence-zero is only a truly neutral release");
  sample={};sample.absoluteAim=true;sample.sequence=65;sample.look[1]=1;sample.aimPitch=1.2f;
  for(unsigned i=0;i<100;++i){sample.sequence=65+i;Check(frame.Apply(sample,true),"camera input remains live after neutral");}
  Check(frame.pitch==1.2f,"pitch has the same bounded range as native control camera");frame.Restore();
  Check(!frame.Active() && !frame.pending && !frame.sequence,"release removes all cached commands and their identity");
 }
 Robo& r=robos[object];r.obj=object;r.ready=true;r.ai=kA3;r.yaw=0.7f;
 r.stShip=1;r.stTransform=2;r.stStand=3;r.stFall=4;r.stFallLand=5;
 for(int i=0;i<5;++i){r.stWalkF[i]=20+i;r.stWalkB[i]=30+i;}
 r.stSideL=40;r.stSideR=41;for(int i=0;i<3;++i){r.stTurnL[i]=50+i;r.stTurnR[i]=60+i;}
 const wchar_t* states[]={L"attack_chop_r",L"attack_chop_l",L"attack_stomp_r",L"attack_stomp_l",L"attack_fire360",L"attack_fire_cross",L"attack_fire_genocide",L"attack_fire_straight"};
 for(unsigned i=0;i<8;++i)r.attacks.push_back({states[i],100+static_cast<int>(i),0,1500,-3.14f,3.14f});
 const auto& adapter=Edf41RoboControlAdapter();
 Check(adapter.Match(object) && !adapter.Match(object+1),"only an actual live custom-controller record matches");
 controlled_pvp::Hooks hooks{};Check(adapter.PvpHooks && adapter.PvpHooks(object,&hooks),"actual controller supplies complete typed root-only PVP proof");
 const void* control=object+0xC98;std::memcpy(object+kSelfCtrl,&control,8);
 const void* self=object;std::memcpy(object+kSelf,&self,8);
 Put<LONG>(object,0xCA0,1);Put<LONG>(object,0xCA4,1);
 Put<const void*>(object,0,&roboClass.block[1]);
 auto* head=object+0xCC0;Put<const void*>(object,0x550,head);
 Put<const void*>(head,0,head);Put<const void*>(head,8,head);
 const ObjRef source{object,control};ObjRef collected[2]{};controlled_pvp::Emitter emitters[1]{};unsigned members=0,emitted=9;
 Check(hooks.collect(nullptr,source,collected,2,&members,emitters,1,&emitted) && members==1 && emitted==0 && collected[0].ctrl==control,"all Robo damage reads root team dynamically; no fake team mirror or hidden child is claimed");
 Check(!hooks.collect(nullptr,ObjRef{object,object+1},collected,2,&members,emitters,1,&emitted),"PVP root proof rejects a stale or spoofed weak identity");
 int savedTeam=0,savedLast=0;Check(hooks.memberBaseline(nullptr,source,source,3,1,&savedTeam,&savedLast) && savedTeam==3 && savedLast==1,"root original team and last-team baseline are exact");
 {
  controlled_pvp::SpawnProof proof{};const auto oldFeature=config.edf41Robo;config.edf41Robo=true;
  Check(!Edf41RoboPvpSpawnProof(&proof),"missing real custom factory refuses before creation");
  factoryRegistered=true;
  Check(Edf41RoboPvpSpawnProof(&proof) && proof.maxMembers==1 && proof.maxEmitters==0 && proof.prepareInstance,
        "exact custom factory exposes root-only proof and real deferred initializer");
  Check(proof.prepareInstance(proof.hooks.user,source) && r.ready && !r.broken,
        "already initialized real producer is idempotent at adoption");
  r.ready=false;
  Check(proof.hooks.collect(proof.hooks.user,source,collected,2,&members,emitters,1,&emitted) && !r.ready,
        "raw newborn typed collection does not fabricate readiness or initialize SGO/native components");
  Check(!proof.prepareInstance(proof.hooks.user,source) && r.ready && r.broken,
        "real Setup missing required SGO/CAS refuses unpublished newborn before native team snapshot");
  Check(!proof.hooks.collect(proof.hooks.user,source,collected,2,&members,emitters,1,&emitted),
        "failed real setup cannot later masquerade as a valid initialized spawn");
  r.ready=true;r.broken=false;
  Put<std::size_t>(object,0x558,1);
  Check(!proof.hooks.collect(proof.hooks.user,source,collected,2,&members,emitters,1,&emitted),
        "unexpected native child subtree requires separate typed proof instead of silent omission");
  Put<std::size_t>(object,0x558,0);
  Put<const void*>(object,0,object);
  Check(!proof.hooks.collect(proof.hooks.user,source,collected,2,&members,emitters,1,&emitted),
        "another class sharing a stale custom record is refused");
  Put<const void*>(object,0,&roboClass.block[1]);
  config.edf41Robo=false;Check(!Edf41RoboPvpSpawnProof(&proof),"disabled custom producer refuses before creation");
  config.edf41Robo=oldFeature;
 }
 const auto hash=adapter.ProfileHash(object);Check(hash!=0,"source skill profile has stable semantic fingerprint");
 Check(adapter.Abilities(object,nullptr,0)==12,"all eight authored attacks plus four real command families are enumerated");
 enemy_control::Ability skills[12]{};Check(adapter.Abilities(object,skills,12)==12,"full catalogue copy");
 for(unsigned i=0;i<12;++i)for(unsigned j=0;j<i;++j)Check(skills[i].id!=skills[j].id,"all semantic skill IDs are unique");
 Check(adapter.Begin(object,17) && adapter.Begin(object,17) && !adapter.Begin(object,18),"same lease is idempotent and cannot steal another owner");
 Check(adapter.Apply(object,{},false),"pending neutral frame does not fail the just-begun lease");
 {
  r.yaw=0;enemy_control::Input move{};move.sequence=1;move.absoluteAim=true;move.aimYaw=1.57079632679f;move.move[2]=1;
  Check(adapter.Apply(object,move,true),"camera-relative movement sample admitted");PlayerTick(r);
  Check(std::fabs(r.cmdX-r.aiSpeed)<0.001f && std::fabs(r.cmdZ)<0.001f,"forward input follows the source camera rather than stale ship body heading");
  adapter.Restore(object);r.yaw=0.7f;Check(adapter.Begin(object,17),"restore movement fixture before authored skill transitions");
 }
 enemy_control::Input input{};input.absoluteAim=true;input.sequence=1;input.seed=42;input.active[0]={skills[0].id,2};
 Check(adapter.Apply(object,input,true),"transform edge accepted");PlayerTick(r);Check(r.cmdTransform,"transform feeds the body's own command");BodyTick(r);
 Check(r.body==kS4 && played==r.stTransform,"body enters original transform state and original clip");
 Check(!adapter.Apply(object,input,true),"replayed press sequence rejected");
 Check(adapter.ProfileHash(object)==hash,"runtime form/state never changes skill identity");
 Check(adapter.Restore(object) && !r.player.Active() && !r.cmdTransform,"restore releases commands and the player lease");
 r.body=kS0;r.form=0;r.flags=0;Check(adapter.Begin(object,19),"new owner after restore");
 input={};input.absoluteAim=true;input.sequence=1;input.active[0]={skills[1].id,2};Check(adapter.Apply(object,input,true),"ship small group edge");PlayerTick(r);BodyTick(r);
 Check(r.body==kS1 && r.gun[kSmall].state==kG1,"ship small volley uses original body and gun charge machines");
 r.body=kS0;r.gun[kMiddle].state=kG0;input.sequence=2;input.active[0].id=skills[2].id;adapter.Apply(object,input,true);PlayerTick(r);BodyTick(r);
 Check(r.body==kS2 && r.gun[kMiddle].state==kG1,"ship middle volley uses original charge machine");
 r.body=kS0;input.sequence=3;input.active[0].id=skills[3].id;adapter.Apply(object,input,true);PlayerTick(r);BodyTick(r);
 Check(r.body==kS3 && r.gun[kLarge].state==kG1 && r.gun[kLarge].charge==300,"ship buster preserves the actual five-second charge contract");
 adapter.Restore(object);r.form=1;r.body=kS7;r.flags=0;r.aiFrames=121;
 Check(adapter.Begin(object,20),"robot form can be controlled");
 for(unsigned i=0;i<8;++i) {
  r.body=kS7;r.pendingState=-1;r.playerAttack=-1;r.chain=0;
  input={};input.absoluteAim=true;input.sequence=i+1;input.active[0]={skills[i+4].id,2};input.seed=500+i;
  Check(adapter.Apply(object,input,true),"authored robot skill admitted");PlayerTick(r);BodyTick(r);
  Check(r.body==kS9 && played==100+static_cast<int>(i),"each semantic skill enters the real attack body state and its authored clip");
  Check(r.aiFrames==121,"manual body/gun input never advances autonomous AI decisions");
 }
 r.body=kS7;r.playerAttack=-1;r.pendingState=-1;r.gun[kSmall].state=kG0;r.gun[kMiddle].state=kG0;
 input={};input.absoluteAim=true;input.sequence=9;input.active[0]={skills[1].id,2};input.active[1]={skills[2].id,2};
 Check(adapter.Apply(object,input,true),"independent dual gun channels in one frame");PlayerTick(r);
 Check(r.gun[kSmall].state==kG1 && r.gun[kMiddle].state==kG1,"robot knee and shoulder groups start original gun charges independently");
 r.gun[kSmall].state=kG0;r.gun[kSmall].autoFire=true;r.gun[kSmall].autoCount=1;
 GunTick(r,r.gun[kSmall],kSmall);Check(r.gun[kSmall].state==kG0 && r.gun[kSmall].autoCount==1,"controlled robot freezes autonomous gun timer without replacing the gun state machine");
 input={};input.absoluteAim=true;input.sequence=10;input.move[2]=1;input.aimYaw=r.yaw;adapter.Apply(object,input,true);r.body=kS7;PlayerTick(r);BodyTick(r);
 Check(r.body==kS8 && r.stepState==r.stWalkF[0],"forward input enters actual root-motion walk step");
 input.sequence=11;input.active[0]={0xDEADBEEF,2};Check(!adapter.Apply(object,input,true),"unknown caller skill IDs cannot name arbitrary CAS states");
 Check(adapter.Restore(object) && adapter.Restore(object),"restore is idempotent");
 GunTick(r,r.gun[kSmall],kSmall);Check(r.gun[kSmall].autoCount==0,"restoration resumes the preserved original autonomous timer");
 online=true;enabled=false;Check(!adapter.NetworkPoseReady(object),"online rejects disabled custom pose/state synchronization");
 enabled=true;registered=false;Check(!adapter.NetworkPoseReady(object),"online rejects objects without native identity");
 registered=true;guest=true;Check(adapter.NetworkPoseReady(object),"online guest participates in the same registered pose/state readiness contract");guest=false;
 r.body=kS7;r.playerAttack=-1;r.gun[kSmall].state=kG0;r.gun[kMiddle].state=kG0;packets.clear();
 Check(adapter.Begin(object,27),"controlled network scenario begins from the true current record");
 input={};input.absoluteAim=true;input.sequence=1;input.seed=123456;input.aimYaw=0.4f;input.aimPitch=0.1f;input.active[0]={skills[1].id,2};
 Check(adapter.Apply(object,input,true),"network skill carries the source's absolute aim and seed");PlayerTick(r);
 Check(packets.size()==1 && packets[0].kind==kNetPlayerGun,"accepted controlled gun uses one atomic leased event instead of an unseeded AI event");
 const auto packet=packets[0];NetPlayerGunMsg envelope{};std::memcpy(&envelope,packet.bytes.data(),sizeof(envelope));
 Check(envelope.player.lease==27 && envelope.player.rng==123456 && envelope.player.aimYaw==0.4f && envelope.gun.group==kSmall,"real gun payload keeps lease, ordinal, source aim and exact source RNG together");
 Robo copy{};int copyIdentity=0;copy.obj=&copyIdentity;copy.ready=true;copy.form=1;copy.body=kS7;copy.player.Begin(27,0);nets[copy.obj].follows=true;
 NetApply(&copy,packet.kind,packet.bytes.data(),packet.bytes.size());
 Check(copy.gun[kSmall].state==kG1 && copy.rng==envelope.player.rng && copy.player.yaw==0.4f && copy.player.pitch==0.1f,"guest enters original charge machine with exactly host's seed and aim");
 copy.rng=555;copy.gun[kSmall].frames=9;NetApply(&copy,packet.kind,packet.bytes.data(),packet.bytes.size());
 Check(copy.rng==555 && copy.gun[kSmall].frames==9,"repeated controlled transition cannot restart or reseed a live volley");
 copy.player.Restore();copy.player.Begin(28,0);copy.playerEvent=0;copy.gun[kSmall].state=kG0;
 NetApply(&copy,packet.kind,packet.bytes.data(),packet.bytes.size());
 Check(copy.gun[kSmall].state==kG0 && copy.rng==555,"late old-lease gun event cannot affect a new control owner");
 copy.player.Restore();copy.player.Begin(27,0);copy.playerEvent=0;nets[copy.obj].follows=false;
 NetApply(&copy,packet.kind,packet.bytes.data(),packet.bytes.size());Check(copy.gun[kSmall].state==kG0,"host cannot replay a guest-only control event");nets.erase(copy.obj);
 // The input never creates a fake projectile: the source MAB event starts the
 // actual hand-gun machine, which charges, fires its original bullet, then stops.
 Gun& hand=r.gun[kLarge];hand.muzzles=2;hand.muzzle[0].found=hand.muzzle[1].found=true;hand.state=kG0;
 hand.bullet.damage=47;hand.bullet.factory=L"RocketBullet01";
 const Event begin{0,kEvBeamStart,L"",2,0,0},end{0,kEvBeamEnd,L"",0,0,0};
 RaiseEvent(r,begin);Check(hand.state==kG1 && hand.charge==2 && hand.duration==-1,"authored beam-start event keeps original charge and event-owned duration");
 const auto before=bullets;GunTick(r,hand,kLarge);GunTick(r,hand,kLarge);Check(hand.state==kG2 && bullets==before,"real gun must finish charge before it may fire");
 GunTick(r,hand,kLarge);Check(bullets==before+2 && lastOwner==object && lastBullet==&hand.bullet && lastBullet->damage==47,"the real gun fires both original muzzle projectiles with unchanged owner and parameters");
 RaiseEvent(r,end);GunTick(r,hand,kLarge);Check(hand.state==kG0 && bullets==before+2,"authored beam-end stops the original gun and no extra projectile is fabricated");
 adapter.Restore(object);
 const auto oldCount=adapter.Abilities(object,nullptr,0);r.attacks.push_back({L"new_authored_action",222,0,200,-1,1});
 Check(adapter.Abilities(object,nullptr,0)==oldCount+1 && adapter.ProfileHash(object)!=hash,"future authored actions are neither fixed-eight truncated nor silently admitted under an old profile");
 r.dead=true;Check(!adapter.Match(object) && !adapter.Begin(object,30),"death cannot acquire a control lease");
 {
  const auto root=std::filesystem::path(__FILE__).parent_path().parent_path();
  std::ifstream file(root/"edf41port/objects/OBJECT/UFOROBO401.SGO",std::ios::binary);
  sgo.assign(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
  Check(sgo.size()>0x20 && !std::memcmp(sgo.data(),"SGO",3),"positive initialization reads the actual shipped UFOROBO401.SGO, not a synthetic config");
  image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x550000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
  Check(image!=nullptr,"isolated native SetTeam ABI code allocated");
  unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};const auto target=reinterpret_cast<std::uintptr_t>(&NativeTeam);
  std::memcpy(jump+2,&target,8);std::memcpy(image+kSetTeam,jump,sizeof(jump));FlushInstructionCache(GetCurrentProcess(),image+kSetTeam,sizeof(jump));
  const auto parentCollect=+[](void*,const ObjRef& ref,ObjRef* out,unsigned capacity,unsigned* count,controlled_pvp::Emitter*,unsigned,unsigned* emitted) noexcept {
   if(!count || !emitted || !out || !capacity)return false;*emitted=0;out[0]=ref;*count=1;return true;
  };
  controlled_pvp::Hooks parentHooks{};parentHooks.collect=parentCollect;
  for(const int original:{6,4}) {
   r=Robo{};r.obj=object;useRealSgo=true;config.edf41Robo=true;
   for(int i=0;i<16;++i)Put<float>(object,0x60+i*4,i%5==0 ? 1.0f : 0.0f);
   NativeTeam(object,original,true);Put<const void*>(object,0,&roboClass.block[1]);
   Put<const void*>(parent,kSelf,parent);Put<const void*>(parent,kSelfCtrl,parent+0x580);
   Put<LONG>(parent,0x588,1);Put<LONG>(parent,0x58C,1);NativeTeam(parent,5,true);
   controlled_pvp::Lease lease{};const ObjRef parentRef{parent,parent+0x580};
   Check(controlled_pvp::Acquire(parentRef,"authenticated-owner",parentHooks,&lease),"real generic lease precedes actual custom spawn");
   controlled_pvp::SpawnProof proof{};Check(Edf41RoboPvpSpawnProof(&proof),"actual custom producer supplies its proof before native creation");
   std::uint64_t token=0;
   Check(controlled_pvp::PrepareSpawns(parentRef,proof,1,&token) && !r.ready && r.clipEvents.empty(),"whole-wave reserve does not run Setup before original script team");
   Check(controlled_pvp::AdoptSpawn(token,parentRef,source) && r.ready && !r.broken && !r.clipEvents.empty() && !r.motions.empty() && !r.attacks.empty(),
         "real Setup parses authored config, initializes native action parameters, then generic Adopt applies temporary owner team");
   const int baseline=original==6 ? 1 : original;
   int saved=0,last=0;Check(At<int>(object,kTeam)==2 && controlled_pvp::ReadBaselineForMember(source,&saved,&last) && saved==baseline && last==baseline,
         "Setup neutral-six conversion and original task team are captured before temporary PVP application");
   for(const auto& gun:r.gun)Check(gun.muzzles>0 && gun.bullet.factory && gun.bullet.custom,"all four actual gun descriptions initialize without a fabricated cached team");
   controlled_pvp::FinishSpawns(token);
   r.dead=true;r.body=kS10; // real native identity/fields remain alive while death animation runs
   Check(controlled_pvp::Restore(&lease) && At<int>(object,kTeam)==baseline && At<int>(object,0x318)==baseline && At<int>(parent,kTeam)==5,
         "actual initialized dying custom newborn restores original task team and last-team without rewinding death");
   Check(r.dead && r.body==kS10,"PVP restoration preserves the current real body/death state");
   useRealSgo=false;
  }
  VirtualFree(image,0,MEM_RELEASE);image=nullptr;
 }
 robos.erase(object);Check(adapter.Restore(object),"a destroyed record restores without dereferencing a stale address");
 std::printf("PASS %d direct production Robo control checks\n",checks);
}
