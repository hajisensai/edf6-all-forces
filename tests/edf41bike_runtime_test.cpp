// Exercise the actual production routing hooks in a private fake image. Native
// endpoints are recording trampolines; no game process or files are touched.
#include "../src/edf41bike.cpp"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <vector>

namespace fixture {
unsigned char vehicle[0x3000]{},seats[2*edf::kSeatStride]{},gun[0x110]{},marker[0x110]{};
crew::Config config{};
edf::Rider rider=edf::Rider::player;
bool tagged=true,marked=true,living=true,npc=false,map=false,matches=true;
int chainCalls=0,failChain=0,sets=0,steps=0,pulls=0,frames=0,poses=0,registrations=0;
void* aimed=nullptr;void* pulled=nullptr;void* entrySeen=nullptr;int holderSeen=-1;
void* callHook=nullptr;
float input[2]{};
std::vector<int> order;
void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
void Jump(unsigned rva,void* to){unsigned char code[14]={0xFF,0x25,0,0,0,0};std::memcpy(code+6,&to,8);std::memcpy(crew::image+rva,code,14);}
void __fastcall Set(unsigned char*,void* aim,void* entry,int holder){++sets;aimed=aim;entrySeen=entry;holderSeen=holder;}
void __fastcall Step(unsigned char*,const float* values){++steps;input[0]=values[0];input[1]=values[1];}
void __fastcall Pose(unsigned char*,float* a,float* b){Check(a==b && a==reinterpret_cast<float*>(gun+0x70),"production passes the same real gun local to native Pose");order.push_back(2);}
void __fastcall Register(unsigned char* aim,int axis,const crew::AxisMap* value){++registrations;Check(value->bone==11,"native mapping uses the source gun index");edf::Put<std::uint64_t>(aim,0x10+static_cast<std::size_t>(axis)*0x40+0x38,1);}
void __fastcall PhysRead(void* at){Check(at==vehicle+0x1320,"physics read uses the actual ragdoll");order.push_back(1);}
void __fastcall World(void* at,const void* matrix){Check(at==vehicle+0xEE0 && matrix==vehicle+0x60,"world build uses the real model/body matrix");order.push_back(3);}
void __fastcall PhysWrite(void* at){Check(at==vehicle+0x1320,"physical gun target uses the original ragdoll");order.push_back(4);}
void __fastcall Pull(void* holder){++pulls;pulled=holder;}
int chainedSteps=0;
void __fastcall ChainedStep(unsigned char* aim,const float* values){++chainedSteps;Step(aim,values);}
void __fastcall PriorFrame(unsigned char*){++frames;}
void __fastcall PriorPose(unsigned char*){++poses;}
void __fastcall Find(const void*,void* request){static_cast<crew::ifcmodel::Find*>(request)->index=tagged ? 0 : -1;}
void Reset(){
 std::memset(vehicle,0,sizeof vehicle);std::memset(seats,0,sizeof seats);std::memset(gun,0,sizeof gun);
 config.enabled=true;tagged=marked=living=matches=true;npc=map=false;rider=edf::Rider::player;
 chainCalls=failChain=sets=steps=pulls=frames=poses=registrations=0;aimed=pulled=entrySeen=nullptr;holderSeen=-1;order.clear();
 crew::on=false;crew::nextFrame=crew::nextPose=nullptr;callHook=nullptr;
 edf::Put<void*>(vehicle,0,crew::image+0x17E01B0);edf::Put<void*>(vehicle,0x608,seats);edf::Put<std::uint64_t>(vehicle,0x618,2);
 edf::Put<std::uint16_t>(vehicle,0x110,2);edf::Put<int>(vehicle+0xEE0,0x20,13);edf::Put<int>(gun,0xC,11);
 for(unsigned i=0;i<2;++i)edf::Put<void*>(seats+i*edf::kSeatStride,0xE0,crew::image+0x17D8A68);
 auto vt=reinterpret_cast<void**>(crew::image+0x17E01B0);vt[4]=reinterpret_cast<void*>(&PriorFrame);vt[45]=reinterpret_cast<void*>(&PriorPose);
 auto aimvt=reinterpret_cast<void**>(crew::image+0x17D8A68);aimvt[2]=crew::image+0x5FBDA0;aimvt[3]=crew::image+0x5FACD0;
 auto sevt=reinterpret_cast<void**>(crew::image+0x17D8A90);sevt[2]=crew::image+0x5FCD80;sevt[3]=crew::image+0x5FACD0;
 edf::Put<void*>(crew::image,0x179EBA8+16,reinterpret_cast<void*>(&Find));
 static void* holderList[1]={gun};edf::Put<void*>(seats+edf::kSeatStride,0xC8,holderList);edf::Put<std::uint64_t>(seats+edf::kSeatStride,0xD8,1);
 edf::Put<float>(seats+edf::kSeatStride,0x2D0,0.25f);edf::Put<float>(seats+edf::kSeatStride,0x2D4,-0.5f);edf::Put<float>(seats+edf::kSeatStride,0x2E4,1.0f);
}
}
namespace edf {
bool Readable(const void* at,std::size_t,bool) noexcept{return at!=nullptr;}
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept{return fixture::matches;}
bool ChainVtableSlot(void** slot,void* hook,void** next) noexcept{++fixture::chainCalls;if(fixture::chainCalls==fixture::failChain)return false;*next=*slot;*slot=hook;return true;}
bool RedirectCall(unsigned char*,void*,void* replacement,bool& changed) noexcept{fixture::callHook=replacement;changed=true;return true;}
unsigned SeatCount(const unsigned char* v) noexcept{return static_cast<unsigned>(At<std::uint64_t>(v,kSeatCount));}
Rider SeatRider(const unsigned char*,const unsigned char*) noexcept{return fixture::rider;}
bool LivingSoldierInSeat(const unsigned char*,const unsigned char*) noexcept{return fixture::living;}
}
namespace crew {
unsigned char* image=nullptr;
const Config& Cfg() noexcept{return fixture::config;}
void Log(const char*,...) noexcept{}
bool MapHoldsKeys() noexcept{return fixture::map;}
bool AiGunner(const unsigned char*,const unsigned char*) noexcept{return fixture::npc;}
unsigned char* BoneRecord506(const unsigned char*,const wchar_t* name) noexcept{
 if(!fixture::marked)return nullptr;
 return std::wcscmp(name,L"gun")==0 ? fixture::gun : fixture::marker;
}
}
int main(){
 using namespace fixture;
 crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));Check(crew::image!=nullptr,"allocate only this test's private image");
 Jump(0x633330,reinterpret_cast<void*>(&Set));Jump(0x5FBDA0,reinterpret_cast<void*>(&Step));Jump(0x5FCD80,reinterpret_cast<void*>(&Step));
 Jump(0x5FACD0,reinterpret_cast<void*>(&Pose));Jump(0x5FB790,reinterpret_cast<void*>(&Register));Jump(0x6EDCA0,reinterpret_cast<void*>(&PhysRead));
 Jump(0x1100B90,reinterpret_cast<void*>(&World));Jump(0x6ED800,reinterpret_cast<void*>(&PhysWrite));Jump(0x62C000,reinterpret_cast<void*>(&Pull));
 Reset();Check(crew::InstallEdf41Bike(),"install the actual production wrappers");
 auto originalAim=seats+0xE0;auto operatorAim=seats+edf::kSeatStride+0xE0;int entry=42;
 reinterpret_cast<crew::SetWeaponFn>(callHook)(vehicle,originalAim,&entry,7);
 Check(sets==1 && aimed==operatorAim && entrySeen==&entry && holderSeen==7,"production constructor routes only Aim to seat1 and preserves every other argument");
 Check(registrations==2,"both native gun mappings are registered once for NetObject admission");
 crew::Frame(vehicle);Check(frames==1&&steps==1&&pulls==1&&pulled==gun,"production gunner frame chains native and pulls its one real holder");
 Check(input[0]==-0.25f&&input[1]==-0.5f&&registrations==2,"actual native update input has correct axes and does not reregister mappings each frame");
 crew::Pose(vehicle);Check(order==std::vector<int>({1,2,3,4})&&poses==0,"production pose has exact source physics/local/world/physical-target ordering");
 rider=edf::Rider::other;crew::Frame(vehicle);Check(steps==2&&pulls==1&&input[0]==0&&input[1]==0,"remote native interpolation never copies remote trigger input");
 npc=true;crew::Frame(vehicle);Check(pulls==2,"the existing NPC-authority predicate owns its one shot");
 map=true;crew::Frame(vehicle);Check(pulls==2&&input[0]==0&&input[1]==0,"map hold consumes input without a shot");
 vehicle[edf::kDead]=1;order.clear();crew::Pose(vehicle);crew::Frame(vehicle);Check(poses==1&&order.empty()&&pulls==2,"dead source vehicle returns to native wreck pose and never fires");
 Reset();Check(crew::InstallEdf41Bike(),"reinstall fixture");tagged=false;crew::Frame(vehicle);crew::Pose(vehicle);
 reinterpret_cast<crew::SetWeaponFn>(callHook)(vehicle,originalAim,&entry,3);
 Check(frames==1&&poses==1&&steps==0&&pulls==0&&aimed==originalAim,"ordinary Car passes frame pose and original constructor arguments through unchanged");
 for(int failure=1;failure<=2;++failure){Reset();failChain=failure;Check(!crew::InstallEdf41Bike(),"inject failure after the constructor redirect");reinterpret_cast<crew::SetWeaponFn>(callHook)(vehicle,originalAim,&entry,9);Check(sets==1&&aimed==originalAim&&registrations==0,"partial installation cannot redirect a source constructor");auto table=reinterpret_cast<crew::VehicleFn*>(crew::image+0x17E01B0);table[4](vehicle);table[45](vehicle);Check(frames==1&&poses==1&&steps==0&&pulls==0,"partially installed hooks forward original callbacks without owned work");}
 // Another module (turretcam.cpp / stab.cpp) chains the aim step before this installs, as plugin.cpp orders it:
 // the 408 still installs and its gunner's step runs through that chain. A replaced pose slot is refused.
 Reset();auto chained=reinterpret_cast<void**>(crew::image+0x17D8A68);chained[2]=reinterpret_cast<void*>(&ChainedStep);
 reinterpret_cast<void**>(crew::image+0x17D8A90)[2]=reinterpret_cast<void*>(&ChainedStep);chainedSteps=0;
 Check(crew::InstallEdf41Bike(),"an aim step chained by another module does not refuse the 408");
 reinterpret_cast<crew::SetWeaponFn>(callHook)(vehicle,originalAim,&entry,7);crew::Frame(vehicle);
 Check(chainedSteps==1&&steps==1&&pulls==1,"the gunner's step goes through the chained vtable slot");
 Reset();reinterpret_cast<void**>(crew::image+0x17D8A68)[3]=reinterpret_cast<void*>(&ChainedStep);
 Check(!crew::InstallEdf41Bike(),"a replaced aim pose slot still refuses the profile");
 Reset();Check(crew::InstallEdf41Bike(),"reinstall for disabled lifecycle");config.enabled=false;crew::Frame(vehicle);crew::Pose(vehicle);Check(poses==1&&steps==0&&pulls==0,"disabled feature leaves original callbacks and no owned input");
 VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
 std::puts("original 408 production routing and lifecycle checks passed");
}
