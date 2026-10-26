// Exercise the actual production routing hooks in a private fake image. Native
// endpoints are recording trampolines; no game process or files are touched.
#include "../src/edf41bike.cpp"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <vector>

namespace fixture {
unsigned char vehicle[0x3000]{},seats[2*edf::kSeatStride]{},gun[0x110]{},marker[0x110]{},gripL[0x110]{},gripR[0x110]{};
bool hasGrips=true;int updates=0;bool updateSawGrip=false;
crew::Config config{};
edf::Rider rider=edf::Rider::player;
unsigned char patches[2][21]{},stubs[2][96]{};std::size_t stubSize[2]{};bool failGate=false;
bool tagged=true,marked=true,living=true,npc=false,map=false,matches=true,gunnerAllowed=true;
int diags=0,diagSawFrames=-1,chainCalls=0,failChain=0,sets=0,steps=0,pulls=0,frames=0,poses=0,registrations=0;
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
// the native Car update: it leaves seat 0's grip matrices as they were (the seat constructor's identity)
void __fastcall PriorUpdate(unsigned char*){++updates;updateSawGrip=std::memcmp(seats+0x40,gripL+0xB0,0x40)==0;}
void __fastcall Find(const void*,void* request){static_cast<crew::ifcmodel::Find*>(request)->index=tagged ? 0 : -1;}
void Reset(){
 std::memset(vehicle,0,sizeof vehicle);std::memset(seats,0,sizeof seats);std::memset(gun,0,sizeof gun);
 config.enabled=true;tagged=marked=living=matches=gunnerAllowed=true;npc=map=false;rider=edf::Rider::player;
 chainCalls=failChain=sets=steps=pulls=frames=poses=registrations=updates=0;hasGrips=true;updateSawGrip=false;
 crew::grips=crew::gripsMissingSaid=false;crew::nextUpdate=nullptr;
 std::memset(gripL,0,sizeof gripL);std::memset(gripR,0,sizeof gripR);aimed=pulled=entrySeen=nullptr;holderSeen=-1;order.clear();
 crew::on=false;crew::nextFrame=crew::nextPose=nullptr;callHook=nullptr;failGate=false;
 edf::Put<void*>(vehicle,0,crew::image+0x17E01B0);edf::Put<void*>(vehicle,0x608,seats);edf::Put<std::uint64_t>(vehicle,0x618,2);
 edf::Put<std::uint16_t>(vehicle,0x110,2);edf::Put<int>(vehicle+0xEE0,0x20,13);edf::Put<int>(gun,0xC,11);
 for(unsigned i=0;i<2;++i)edf::Put<void*>(seats+i*edf::kSeatStride,0xE0,crew::image+0x17D8A68);
 auto vt=reinterpret_cast<void**>(crew::image+0x17E01B0);vt[4]=reinterpret_cast<void*>(&PriorFrame);vt[5]=reinterpret_cast<void*>(&PriorUpdate);vt[45]=reinterpret_cast<void*>(&PriorPose);
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
bool PatchCode(unsigned char* at,const unsigned char* expected,const unsigned char* replacement,std::size_t size) noexcept{
 const int k=at==crew::image+0x65B14B ? 0 : at==crew::image+0x65B31B ? 1 : -1;
 fixture::Check(k>=0 && size==(k ? 8u : 21u) && expected[0]==(k ? 0x85 : 0x49),"the gun gate patches the two stock tests whole");
 std::memcpy(fixture::patches[k],replacement,size);return !fixture::failGate;}
void* AllocateNearCode(const void* anchor,const unsigned char* code,std::size_t size) noexcept{
 const int k=anchor==crew::image+0x65B14B ? 0 : anchor==crew::image+0x65B31B ? 1 : -1;
 fixture::Check(k>=0 && size<=sizeof(fixture::stubs[0]),"each stub near its test");
 std::memcpy(fixture::stubs[k],code,size);fixture::stubSize[k]=size;
 // a stub address the rel32 jmp reaches: inside the private image, past everything the test writes
 return crew::image+0x1700000+static_cast<std::size_t>(k)*0x100;}
unsigned SeatCount(const unsigned char* v) noexcept{return static_cast<unsigned>(At<std::uint64_t>(v,kSeatCount));}
Rider SeatRider(const unsigned char*,const unsigned char*) noexcept{return fixture::rider;}
bool LivingSoldierInSeat(const unsigned char*,const unsigned char*) noexcept{return fixture::living;}
}
namespace crew {
unsigned char* image=nullptr;
const Config& Cfg() noexcept{return fixture::config;}
void Log(const char*,...) noexcept{}
bool MapHoldsKeys() noexcept{return fixture::map;}
bool Edf41BikeGunnerAllowed() noexcept{return fixture::gunnerAllowed;}
void Edf41BikeDiagFrame(unsigned char*) noexcept{++fixture::diags;fixture::diagSawFrames=fixture::frames;}
bool AiGunner(const unsigned char*,const unsigned char*) noexcept{return fixture::npc;}
unsigned char* BoneRecord506(const unsigned char*,const wchar_t* name) noexcept{
 if(std::wcscmp(name,L"te_ik_l")==0)return fixture::hasGrips ? fixture::gripL : nullptr;
 if(std::wcscmp(name,L"te_ik_r")==0)return fixture::hasGrips ? fixture::gripR : nullptr;
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
 // A member of the room lacks the gunner seat (host_settings.h Edf41BikeGunnerAllowed false this mission): every
 // machine builds and drives the 408 as the stock Car (original constructor arguments, no gunner frame, no pose).
 Reset();Check(crew::InstallEdf41Bike(),"reinstall for a room without the gunner seat");gunnerAllowed=false;
 reinterpret_cast<crew::SetWeaponFn>(callHook)(vehicle,originalAim,&entry,7);crew::Frame(vehicle);crew::Pose(vehicle);
 Check(sets==1&&aimed==originalAim&&registrations==0&&frames==1&&steps==0&&pulls==0&&poses==1,"a room without the gunner seat: the stock Car everywhere");
 Reset();Check(crew::InstallEdf41Bike(),"reinstall for disabled lifecycle");config.enabled=false;crew::Frame(vehicle);crew::Pose(vehicle);Check(poses==1&&steps==0&&pulls==0,"disabled feature leaves original callbacks and no owned input");
 // The hover diagnostic (edf41bike_diag.cpp): every Car frame with Debug=1, after the native frame, whatever the gunner
 // seat's state (a closed room or a disabled feature included); never with Debug=0.
 Reset();Check(crew::InstallEdf41Bike(),"reinstall for the diagnostic");diags=0;config.debug=true;gunnerAllowed=false;
 crew::Frame(vehicle);Check(diags==1&&frames==1&&diagSawFrames==1,"Debug=1: the diagnostic samples each Car frame after the native one");
 config.debug=false;gunnerAllowed=true;crew::Frame(vehicle);Check(diags==1&&frames==2,"Debug=0: no diagnostic");
 // The gun gate (2026-10-21): the stock loop skips any Car whose seat 0 owns no gun, and in it every row of another seat;
 // for the 408 the two stubs ask BuildsGun / BuildsRow.
 Reset();Check(crew::InstallEdf41Bike(),"install with the gun gate");
 for(int k=0;k<2;++k) {
  const unsigned rva=k ? 0x65B31Bu : 0x65B14Bu;std::int32_t rel=0;std::memcpy(&rel,patches[k]+1,4);
  Check(patches[k][0]==0xE9 && crew::image+rva+5+rel==crew::image+0x1700000+static_cast<std::size_t>(k)*0x100 &&
        patches[k][5]==0x90,"each test jumps to its stub");
 }
 {const unsigned char head[]={0x49,0x8B,0x87,0x08,0x06,0x00,0x00,0x48,0x83,0xB8,0xD8,0x00,0x00,0x00,0x00,0x75,0x1D};
  Check(std::memcmp(stubs[0],head,sizeof(head))==0,"a gun on seat 0 keeps the stock loop first");}
 {const unsigned char head[]={0x85,0xDB,0x74,0x1D};Check(std::memcmp(stubs[1],head,sizeof(head))==0,"a seat-0 row keeps the stock build first");}
 auto at64=[](const unsigned char* p){std::uint64_t v=0;std::memcpy(&v,p,8);return v;};
 Check(at64(stubs[0]+15+13)==reinterpret_cast<std::uint64_t>(&crew::BuildsGun)&&at64(stubs[1]+2+13)==reinterpret_cast<std::uint64_t>(&crew::BuildsRow),
       "each stub asks its own question");
 Check(at64(stubs[0]+stubSize[0]-22)==reinterpret_cast<std::uint64_t>(crew::image+0x65B160)&&at64(stubs[0]+stubSize[0]-8)==reinterpret_cast<std::uint64_t>(crew::image+0x65B3EF),
       "the count gate goes on to the stock loop or the stock skip");
 Check(at64(stubs[1]+stubSize[1]-22)==reinterpret_cast<std::uint64_t>(crew::image+0x65B323)&&at64(stubs[1]+stubSize[1]-8)==reinterpret_cast<std::uint64_t>(crew::image+0x65B3A6),
       "the row gate goes on to the stock build or the next row");
 Check(crew::BuildsGun(vehicle)&&crew::BuildsRow(vehicle,1),"the 408 with its one seat-1 holder: its gun row is built");
 Check(!crew::BuildsRow(vehicle,2),"a row of another seat: the stock skip");
 gunnerAllowed=false;Check(!crew::BuildsGun(vehicle)&&!crew::BuildsRow(vehicle,1),"a room without the seat and its gun: the stock skips");gunnerAllowed=true;
 tagged=false;Check(!crew::BuildsGun(vehicle)&&!crew::BuildsRow(vehicle,1),"another Car: the stock skips");tagged=true;
 edf::Put<std::uint64_t>(seats+edf::kSeatStride,0xD8,0);Check(!crew::BuildsGun(vehicle),"no seat-1 holder: nothing to build");
 Reset();failGate=true;Check(!crew::InstallEdf41Bike()&&!crew::on,"a gate that cannot be patched: not installed");
 // The driver's grip IK targets (2026-10-26 「人手臂伸出去好远」): Car slot 5 chained; after the native update the 408's
 // te_ik_l / te_ik_r world matrices (bone +0xB0) land in seat 0 +0x40 / +0x80, as Vehicle503_Bike's 0x617A30 does.
 Check(crew::grips,"the grips install even when the gunner seat's gate does not");
 auto seatIdentity=[](unsigned char* seat){for(int i=0;i<2;++i)for(int r=0;r<4;++r)for(int c=0;c<4;++c)edf::Put<float>(seat,0x40+i*0x40+(r*4+c)*4,r==c ? 1.0f : 0.0f);};
 auto gripWorld=[](unsigned char* rec,float base){for(int k=0;k<16;++k)edf::Put<float>(rec,0xB0+k*4,base+static_cast<float>(k));};
 auto table5=[]{return reinterpret_cast<crew::VehicleFn*>(crew::image+0x17E01B0)[5];};
 Reset();Check(crew::InstallEdf41Bike(),"install with the grips");
 Check(reinterpret_cast<void*>(table5())==reinterpret_cast<void*>(&crew::GripUpdate)&&crew::nextUpdate==reinterpret_cast<void*>(&PriorUpdate),"Car slot 5 is chained to the grip update");
 seatIdentity(seats);seatIdentity(seats+edf::kSeatStride);gripWorld(gripL,10.0f);gripWorld(gripR,40.0f);
 unsigned char gunnerSeat[0x80];std::memcpy(gunnerSeat,seats+edf::kSeatStride+0x40,0x80);
 table5()(vehicle);
 Check(updates==1&&!updateSawGrip,"the native update runs first");
 Check(std::memcmp(seats+0x40,gripL+0xB0,0x40)==0&&std::memcmp(seats+0x80,gripR+0xB0,0x40)==0,"the driver's grip targets are the 408's grip bones' world matrices");
 Check(std::memcmp(seats+edf::kSeatStride+0x40,gunnerSeat,0x80)==0,"the gunner's seat is left alone");
 gripWorld(gripL,70.0f);gunnerAllowed=false;config.enabled=false;vehicle[edf::kDead]=1;table5()(vehicle);
 Check(updates==2&&std::memcmp(seats+0x40,gripL+0xB0,0x40)==0,"every frame, whatever the gunner seat's gate, Enabled or the wreck (V503 does not ask either)");
 Reset();Check(crew::InstallEdf41Bike(),"reinstall for another Car");tagged=false;seatIdentity(seats);gripWorld(gripL,10.0f);
 unsigned char stock[0x80];std::memcpy(stock,seats+0x40,0x80);table5()(vehicle);
 Check(updates==1&&std::memcmp(seats+0x40,stock,0x80)==0,"another Car keeps its native seat");
 Reset();Check(crew::InstallEdf41Bike(),"reinstall for a model without the bones");hasGrips=false;seatIdentity(seats);
 std::memcpy(stock,seats+0x40,0x80);table5()(vehicle);table5()(vehicle);
 Check(updates==2&&std::memcmp(seats+0x40,stock,0x80)==0&&crew::gripsMissingSaid,"a 408 without its grip bones: logged once, the seat untouched");
 Reset();matches=false;Check(!crew::InstallEdf41Bike()&&!crew::grips&&table5()==reinterpret_cast<crew::VehicleFn>(&PriorUpdate),"a differing native update: no grip hook");
 VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
 std::puts("original 408 production routing and lifecycle checks passed");
}
