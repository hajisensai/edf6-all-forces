// Real production M163 hook with only native world-build and resource readers
// replaced. The executable image is private to this test process.
#include "../src/vulcan_rotor.cpp"
#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace fixture {
unsigned char vehicle[0x3000]{},rotor[0x110]{},holders[2*0x48]{},weapon[2][0x1600]{},control[2][0x10]{};
unsigned char skeleton[0x80]{},definitions[11*0xD0]{},wheels[4][0x110]{};
crew::Config config{};ULONGLONG now=1000,frame=1;bool marked=true,known=true,readable=true,trap=false;
int builds=0,wheelBuilds=0;bool failBuild=false;void* wheelHook=nullptr;void* hook=nullptr;const void* lastInst=nullptr;const void* lastMatrix=nullptr;
void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
void __fastcall NativeBuild(void* inst,const void* matrix){++builds;lastInst=inst;lastMatrix=matrix;}
void __fastcall NativeWheels(unsigned char* v){
 Check(v==vehicle,"native wheel writer keeps original vehicle argument");++wheelBuilds;
 for(int i=0;i<4;++i)std::memcpy(wheels[i]+0xB0,wheels[i]+0x70,64);
}
void Jump(unsigned rva,void* to){unsigned char code[14]={0xFF,0x25,0,0,0,0};std::memcpy(code+6,&to,8);std::memcpy(crew::image+rva,code,14);}
void Bind(){float value[16]={1,0,0,0,0,1,0,0,0,0,1,0,-.195785f,0,-.63693f,1};std::memcpy(rotor+0x70,value,64);std::memcpy(definitions+6*0xD0+0x20,value,64);}
void Reset(){
 crew::ResetVulcanRotors();std::memset(vehicle,0,sizeof vehicle);std::memset(rotor,0,sizeof rotor);std::memset(holders,0,sizeof holders);std::memset(weapon,0,sizeof weapon);
 config.enabled=marked=known=readable=true;trap=false;now=1000;frame=1;builds=0;wheelBuilds=0;failBuild=false;hook=wheelHook=nullptr;crew::installed=false;
 edf::Put<void*>(vehicle,0,crew::image+0x17DC620);edf::Put<void*>(vehicle,0x30,control[0]);edf::Put<void*>(vehicle,0xEE0+0x10,rotor);edf::Put<void*>(vehicle,0xEE0,skeleton);
 edf::Put<void*>(skeleton,0x58,definitions);edf::Put<int>(skeleton,0x68,10);edf::Put<int>(rotor,0x0C,6);
 edf::Put<void*>(vehicle,0x638,holders);edf::Put<std::uint64_t>(vehicle,0x648,2);
 for(int i=0;i<2;++i){edf::Put<void*>(holders+static_cast<std::size_t>(i)*0x48,0x10,weapon[i]);edf::Put<int>(weapon[i],0xBE8,2000);}
 Bind();
 for(int i=0;i<4;++i){
  std::memset(wheels[i],0,sizeof wheels[i]);edf::Put<int>(wheels[i],0x0C,7+i);
  float b[16]={1,0,0,0,0,1,0,0,0,0,1,0,i<2 ? 1.2f : -1.2f,i%2==0 ? .4f : .7f,i%2==0 ? -2.f : -3.f,1};
  std::memcpy(wheels[i]+0x70,b,64);std::memcpy(definitions+(7+i)*0xD0+0x20,b,64);
 }
}
void Call(){reinterpret_cast<crew::BuildFn>(hook)(vehicle+0xEE0,vehicle+0x60);}
void WheelCall(){reinterpret_cast<crew::WheelsFn>(wheelHook)(vehicle);}
void Tick(){now+=16;++frame;Call();}
float Angle(){return std::atan2(edf::At<float>(rotor,0x74),edf::At<float>(rotor,0x70));}
}
namespace edf {
bool Readable(const void* at,std::size_t,bool) noexcept{return fixture::readable && at!=nullptr;}
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept{return fixture::known;}
bool RedirectCall(unsigned char* at,void* prior,void* replacement,bool& changed) noexcept{
 if(at==crew::image+0x674B7F){
  fixture::Check(prior==crew::image+0x66F430,"production hooks only audited final wheel writer");fixture::wheelHook=replacement;
 }else{
  fixture::Check(at==crew::image+0x621654&&prior==crew::image+0x1100B90,"production hooks only the audited 603 final build call");
  if(fixture::failBuild)return false;
  fixture::hook=replacement;
 }
 changed=true;return true;
}
}
namespace crew {
int recoilPoses=0;const void* recoilVehicle=nullptr;
void BarrelRecoilPose(unsigned char* v) noexcept{++recoilPoses;recoilVehicle=v;}   // barrel_recoil.cpp (its own test)
int turretPoses=0;
void SeatTurretsPose(unsigned char*) noexcept{++turretPoses;}   // seat_turret.cpp (tests/seat_turret_test.cpp)
// A copy of body506.cpp's (the skeleton definition's bind): the fixture's skeleton is read as production reads it.
bool BoneBind506(const unsigned char* inst,const unsigned char* record,float* bind) noexcept{
 const auto* skeleton=At<const unsigned char*>(inst,0);
 if(!Readable(skeleton,0x6C) || !Readable(record,0x10))return false;
 const auto* definitions=At<const unsigned char*>(skeleton,0x58);
 const int index=At<std::int32_t>(record,0x0C),last=At<std::int32_t>(skeleton,0x68);
 if(index<0 || index>last || last>255 || !definitions)return false;
 const auto* local=definitions+static_cast<std::size_t>(index)*0xD0+0x20;
 if(!Readable(local,64))return false;
 std::memcpy(bind,local,64);
 for(int i=0;i<16;++i)if(!std::isfinite(bind[i]))return false;
 return true;
}
unsigned char* image=nullptr;
const Config& Cfg() noexcept{return fixture::config;}
ULONGLONG GameMs() noexcept{return fixture::now;}
ULONGLONG GameFrame() noexcept{return fixture::frame;}
void Log(const char*,...) noexcept{}
unsigned char* BoneRecord506(const unsigned char*,const wchar_t* name) noexcept{
 if(fixture::trap)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);
 if(!fixture::marked)return nullptr;
 if(std::wcscmp(name,vulcanrotor::kBone)==0)return fixture::rotor;
 const wchar_t* names[]={L"tire_moveG_l",L"tire_moveH_l",L"tire_moveG_r",L"tire_moveH_r"};
 for(int i=0;i<4;++i)if(std::wcscmp(name,names[i])==0)return fixture::wheels[i];
 return nullptr;
}
const wchar_t* WeaponFile(const unsigned char* value,std::size_t* length) noexcept{
 const wchar_t* result=value==fixture::weapon[0] ? L"EDF6VC_VULCAN_HE_2000.SGO" : value==fixture::weapon[1] ? L"EDF6VC_VULCAN_AP_2000.SGO" : nullptr;
 *length=result ? std::wcslen(result) : 0;return result;
}
}
int main(){
 using namespace fixture;
 crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));Check(crew::image!=nullptr,"private native image");
 Jump(0x1100B90,reinterpret_cast<void*>(&NativeBuild));Jump(0x66F430,reinterpret_cast<void*>(&NativeWheels));
 Reset();Check(crew::InstallVulcanRotor(),"actual production install succeeds");Call();
 Check(builds==1&&lastInst==vehicle+0xEE0&&lastMatrix==vehicle+0x60,"forward native world-build exactly once with unchanged arguments");
 Check(crew::recoilPoses==1&&crew::recoilVehicle==vehicle,"the same last pose step gives the BMPT's barrels their recoil, once a build");
 Check(crew::turretPoses==1,"the gunner seats' own turrets are posed at the same last local step, once a build");
 Check(Angle()==0&&edf::At<int>(rotor,8)==1,"idle visual bone uses bind and native dirty flag");
 edf::Put<int>(weapon[0],0xBD4,1);edf::Put<int>(weapon[0],0xBE8,1999);Tick();
 Check(std::fabs(Angle())>.001f,"actual native HE firing rotates the real production bone");
 const float same=Angle();Call();Check(Angle()==same&&builds==3,"duplicate pose in one game frame cannot double integrate");
 for(int i=0;i<10;++i){edf::Put<int>(weapon[0],0xBE8,1998-i);Tick();}
 Check(Angle()!=same,"sustained actual fire continuously advances phase");
 Check(edf::At<float>(rotor,0xA0)==-.195785f&&edf::At<float>(rotor,0xA8)==-.63693f,"rotor writes preserve original pivot translation");
 edf::Put<int>(weapon[0],0xBD4,0);for(int i=0;i<40;++i)Tick();const float coast=Angle();Tick();Check(Angle()==coast,"native trigger release coasts to a complete stop");
 edf::Put<int>(weapon[1],0xBD4,1);Tick();Check(Angle()!=coast,"alternate AP belt drives the same physical rotor");
 config.enabled=false;const float disabled=Angle();Tick();Check(Angle()==disabled,"disabled feature does not own the visual pose");config.enabled=true;
 vehicle[edf::kDead]=1;Tick();Check(Angle()==disabled,"destroyed vehicle retains native wreck pose");
 Reset();Check(crew::InstallVulcanRotor(),"reset independent test");edf::Put<int>(weapon[0],0xBD4,1);edf::Put<int>(weapon[0],0xBE8,0);for(int i=0;i<30;++i)Tick();Check(Angle()==0,"held empty native gun never spins");
 edf::Put<int>(weapon[0],0xBD4,2);edf::Put<int>(weapon[0],0xBE8,2000);for(int i=0;i<30;++i)Tick();Check(Angle()==0,"reload state never fabricates firing");
 edf::Put<int>(weapon[0],0xBD4,0);edf::Put<int>(weapon[0],0xBE8,1999);Tick();Check(std::fabs(Angle())>.001f,"offline ammunition change works with zero network firing counters");
 Reset();Check(crew::InstallVulcanRotor(),"remote counter scenario");Call();edf::Put<int>(weapon[0],0x1544,1);Tick();Check(std::fabs(Angle())>.001f,"native remote pending-shot event animates without replay flag");
 for(int i=0;i<50;++i)Tick();const float idle=Angle();Tick();Check(idle==Angle(),"unchanged pending counter cannot spin forever");
 edf::Put<void*>(vehicle,0x30,control[1]);Tick();Check(Angle()==0,"reused identity restores immutable model bind rather than compounding the last rendered phase");
 marked=false;const int before=builds;Bind();Tick();Check(builds==before+1&&Angle()==0,"stock 603 and BMPT remain exact native builds");
 marked=true;readable=false;Tick();Check(builds==before+2,"unreadable optional rotor state still forwards native build");
 Reset();Check(crew::InstallVulcanRotor(),"streaming fixture");edf::Put<void*>(skeleton,0x58,nullptr);Call();
 Check(Angle()==0&&builds==1,"incomplete immutable skeleton forwards without pose ownership");
 edf::Put<void*>(skeleton,0x58,definitions);edf::Put<int>(weapon[0],0xBD4,1);Tick();
 Check(std::fabs(Angle())>.001f,"same model retries after skeleton stream completes");
 trap=true;const int trapped=builds;Tick();Check(builds==trapped+1,"SEH inside pose still forwards exactly one native build");
 trap=false;Tick();Check(builds==trapped+2,"SEH releases state lock for the next frame");
 Reset();Check(crew::InstallVulcanRotor(),"half-idler fixture");
 float gp[16]={1,0,0,0,0,0,1,0,0,-1,0,0,1.2f,.5f,-2,1};std::memcpy(wheels[0]+0x70,gp,64);Call();
 edf::Put<int>(wheels[1],8,7);WheelCall();
 Check(std::fabs(edf::At<float>(wheels[1],0xE4)-1.5f)<1e-5f&&std::fabs(edf::At<float>(wheels[1],0xE8)+1.7f)<1e-5f,
       "production post-wheel hook updates outer H world after actual G writer and immutable pivots");
 Check(wheelBuilds==1&&edf::At<int>(wheels[1],8)==7,"half-wheel preserves native ownership flag and original writer runs exactly once");
 vehicle[edf::kDead]=1;edf::Put<float>(wheels[1],0xA4,9);WheelCall();
 Check(edf::At<float>(wheels[1],0xE4)==9,"destroyed half-idlers keep native wheel/ragdoll ownership");
 Reset();failBuild=true;Check(!crew::InstallVulcanRotor()&&wheelHook!=nullptr&&hook==nullptr,"partial second-hook failure stays disabled");
 std::memcpy(wheels[0]+0x70,gp,64);WheelCall();
 Check(wheelBuilds==1&&edf::At<float>(wheels[1],0xE4)==.7f,"partial install forwards native wheel writer without H overwrite");
 Reset();known=false;Check(!crew::InstallVulcanRotor()&&hook==nullptr,"mismatched native build signature writes no hook");
 VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
 std::puts("M163 production rotor routing, firing and lifetime checks passed");
}
