// Exercise the production ProteusFrame reached by the native slot-4 shared vehicle tick, with an actual player
// rider and four embedded seats. No AI input task is simulated. The EDF functions stay uncalled in this fixture.
#include "../src/proteus.cpp"
#include <cstdio>
namespace crew {
unsigned char* image=nullptr;PlayerFix player{};
namespace {
Config config{};ULONGLONG now=3600000,frame=1;
unsigned char vehicle[0x3000]{},seats[4*kSeatStride]{},human[0x400]{},ctrl[16]{};
int failures=0,checks=0,gunShots=0,salvoShots=0;
bool netSession=false,netAuthority=true;
std::int32_t netDriver=42;
int controlSends=0,defenseSends=0;
proteus_net::State lastControl,lastDefense;
float lastFrom[3]{},lastAt[3]{};bool roundSucceeds=true;
unsigned char* testPoseBones=nullptr;
void Check(bool pass,const char* what){++checks;if(!pass){++failures;std::printf("FAIL %s\n",what);}}
void Tick(){++frame;now+=100;ProteusFrame(vehicle);}
}
const Config& Cfg() noexcept{return config;}
bool InSession() noexcept{return netSession;}
bool IsOnlineAuthority(const void*) noexcept{return netAuthority;}
bool InstallProteusNet() noexcept{return true;}
std::int32_t ProteusNetController(unsigned char*) noexcept{return netDriver;}
bool ProteusNetSend(unsigned char*,proteus_net::State s) noexcept{
    if(s.kind==proteus_net::Kind::control){++controlSends;lastControl=s;}else{++defenseSends;lastDefense=s;}return true;
}
ULONGLONG GameMs() noexcept{return now;}ULONGLONG GameFrame() noexcept{return frame;}
void Log(const char*,...) noexcept{}
bool MapHoldsKeys() noexcept{return false;}
bool KnownVehicle(const void*) noexcept{return false;}
bool CameraRay(float*,float*) noexcept{return false;}
bool VisitEnemies(const unsigned char*,EnemyVisitor,void*) noexcept{return true;}
float MapRay(const float*,const float*,float*) noexcept{return -1.0f;}
unsigned char* PlayerHuman() noexcept{return human;}
void ProteusRoundsReady(bool* gun,bool* salvo) noexcept{if(gun)*gun=false;if(salvo)*salvo=false;}
unsigned char* BoneRecord506(const unsigned char*,const wchar_t* name) noexcept{
    if(!testPoseBones)return nullptr;
    if(!std::wcscmp(name,L"pile_l"))return testPoseBones+36*kBoneStride;
    if(std::wcsncmp(name,L"vc_ps_",6))return nullptr;
    const int i=_wtoi(name+6);return i>=0 && i<36 ? testPoseBones+static_cast<std::size_t>(i)*kBoneStride : nullptr;
}
bool ProteusGunRound(const unsigned char*,const float* from,const float* at,float) noexcept{
    ++gunShots;std::memcpy(lastFrom,from,12);std::memcpy(lastAt,at,12);return roundSucceeds;
}
bool ProteusSalvoRound(const unsigned char*,const float* from,const float* at,float) noexcept{
    ++salvoShots;std::memcpy(lastFrom,from,12);std::memcpy(lastAt,at,12);return roundSucceeds;
}
}
int main(){
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)return 2;
    const unsigned char setWorldCode[]={0xC6,0x81,0xB0,0,0,0,1,0x0F,0x28,0x02};
    std::memcpy(image+kSetModelWorld,setWorldCode,sizeof(setWorldCode));
    Put<const void*>(image,kProteusPoseSlot,image+kProteusPoseFn);
    Check(InstallProteusPose() && nextProteusPose==image+kProteusPoseFn &&
          At<const void*>(image,kProteusPoseSlot)==reinterpret_cast<const void*>(&ProteusPoseHook),
          "production install chains only the actual BigBegaruta pose slot");
    Check(!InstallProteusPose(),"unexpected already-hooked pose slot is not blindly overwritten");
    ok=true;config.debug=false;config.proteusDriverGun=false;config.proteusFieldRadius=0;
    Put<const void*>(vehicle,0,image+kVtBig);Put<void*>(vehicle,kSelfCtrl,ctrl);Put<void*>(vehicle,kSeats,seats);Put<std::uint64_t>(vehicle,kSeatCount,4);
    Put<float>(vehicle,kWalk,10);Put<float>(vehicle,kWalkEase,0.1f);Put<float>(vehicle,kTurn,0.2f);Put<float>(vehicle,kJump,8);Put<float>(vehicle,kStepNormal,0.76f);
    Put<float>(vehicle,kHpMax,7500);Put<float>(vehicle,kMatrix,1);Put<float>(vehicle,kMatrix+20,1);Put<float>(vehicle,kMatrix+40,1);
    for(int i=0;i<4;++i)Put<int>(seats+i*kSeatStride,kSeatClassMask,15);
    Put<void*>(seats,kSeatRider,human);Put<void*>(seats,kSeatRiderCtrl,ctrl);Put<int>(ctrl,edf::kCtrlUses,1);
    human[edf::kHumanPlayer]=1;Put<void*>(human,edf::kHumanPad,human);seats[kSeatPad]=1;
    Tick();ProteusReadout ro{};
    Check(PlayerProteus(&ro) && ro.driver,"native player tick produces Proteus HUD readout without AI task");
    Check(At<float>(vehicle,kWalk)==16.0f,"player entry applies movement rework");
    Check(At<int>(seats+2*kSeatStride,kSeatClassMask)==0 && At<int>(seats+3*kSeatStride,kSeatClassMask)==0,"player entry really closes extra two seats");
    Check(ProteusVisibleSeats(vehicle,4)==2,"closed engine weapon slots are not advertised as passenger seats");
    Put<void*>(seats+2*kSeatStride,kSeatRider,human);
    Check(ProteusVisibleSeats(vehicle,4)==4,"an existing rider in a closed slot stays visible");
    Put<void*>(seats+2*kSeatStride,kSeatRider,nullptr);
    Put<std::uint16_t>(seats,kSeatButtons,static_cast<std::uint16_t>(config.proteusModeButton));Tick();
    Put<std::uint16_t>(seats,kSeatButtons,0);for(int i=0;i<35;++i)Tick();
    Check(PlayerProteus(&ro) && ro.mode==proteus::Mode::deployed,"native player frames advance deployment to completion");
    Check(At<float>(vehicle,kWalk)==0 && At<float>(vehicle,kJump)==0,"deployed leg and jump parameters change in real object");
    config.proteus=false;Tick();
    Check(At<float>(vehicle,kWalk)==10 && At<float>(vehicle,kJump)==8,"setting off restores stock legs through same native tick");
    Check(At<int>(seats+2*kSeatStride,kSeatClassMask)==15 && At<int>(seats+3*kSeatStride,kSeatClassMask)==15,"setting off restores original seat masks");
    Check(ProteusVisibleSeats(vehicle,4)==4,"disabled rework restores four public seats");
    float transform[16];
    proteus::pose::Panel(0,120,0,true,transform);
    Check(std::fabs(std::atan2(transform[8],transform[10])+55.0f*kPi/180.0f)<.0001f,"shield first panel faces -55 degrees, covering -60 to -50");
    proteus::pose::Panel(12,120,0,true,transform);
    Check(transform[0]==0 && transform[5]==0 && transform[10]==0,"panels outside the configured arc are hidden");
    proteus::pose::Panel(35,360,0,true,transform);
    Check(transform[5]==1,"full-circle shield displays all 36 panels");
    proteus::pose::Panel(0,120,0,false,transform);
    Check(transform[0]==0 && transform[5]==0,"shield off writes a hidden complete matrix");
    float local[16],world[16];proteus::pose::Identity(local);proteus::pose::Identity(world);
    world[0]=0;world[1]=1;world[4]=-1;world[5]=0;
    const float delta[3]={0,-2,0};
    Check(proteus::pose::MoveWorld(local,world,delta,transform) && std::fabs(transform[12]+2)<.0001f && transform[13]==0,
          "rotated pile parent converts vertical ground movement to its actual local axis");
    proteus::State poseState{};poseState.mode=proteus::Mode::deploying;poseState.t=.75f;
    Check(std::fabs(proteus::pose::Deployed(poseState,proteus::Tunables{})-.5f)<.0001f,"deployment model progresses halfway through real stagger");
    poseState.mode=proteus::Mode::stowing;
    Check(std::fabs(proteus::pose::Deployed(poseState,proteus::Tunables{})-.5f)<.0001f,"stowing reverses deployment model");
    // Production firing paths, with real MuzzleFrame reading separate physical tubes and native bone matrices.
    unsigned char weapon[0xF00]{},muzzles[2*edf::kMuzzleStride]{},bone[0x100]{};
    Put<void*>(weapon,edf::kMuzzles,muzzles);Put<std::uint64_t>(weapon,edf::kMuzzleCount,2);
    for(int k=0;k<4;++k)Put<float>(bone,edf::kBoneRows+k*20,1);
    for(int i=0;i<2;++i){
        auto m=muzzles+i*edf::kMuzzleStride;Put<void*>(m,0,bone);Put<int>(m,edf::kMuzzleMode,1);
        for(int k=0;k<4;++k)Put<float>(m,edf::kMuzzleLocal+k*20,1);
        Put<float>(m,edf::kMuzzleLocal+48,i==0 ? -3.0f : 3.0f);
        Put<float>(m,edf::kMuzzleLocal+52,10.0f);Put<float>(m,edf::kMuzzleLocal+56,9.0f);
    }
    Unit fire{};fire.weapon[kRightSeat]=weapon;fire.weapon[kLauncherSeat]=weapon;
    fire.st.mode=proteus::Mode::deployed;fire.closed=true;config.proteusDriverGun=true;
    Put<float>(seats,kSeatFire,1);float from[3],dir[3];
    Check(RoundFrom(fire,kLauncherSeat,0,from,dir) && from[0]==-3 && from[1]==10,"salvo selects first real tube without arbitrary height offset");
    Check(RoundFrom(fire,kLauncherSeat,1,from,dir) && from[0]==3,"salvo selects next real tube rather than midpoint in hull");
    DriverGun(fire,vehicle,seats,now,config);
    Check(gunShots==1 && lastFrom[0]==-3 && lastAt[2]>lastFrom[2],"driver cannon follows real barrel even when camera ray unavailable");
    Put<void*>(seats+kSeatStride,kSeatRider,human);Put<void*>(seats+kSeatStride,kSeatRiderCtrl,ctrl);
    DriverGun(fire,vehicle,seats,now+2000,config);Check(gunShots==1,"occupied gunner has exclusive cannon ownership");
    Put<void*>(seats+kSeatStride,kSeatRider,nullptr);Put<void*>(seats+kSeatStride,kSeatRiderCtrl,nullptr);
    fire.mark=human;fire.markAt[2]=200;fire.salvoLeft=2;Salvo(fire,vehicle,now,config);
    Check(salvoShots==1 && fire.salvoLeft==1 && lastFrom[0]==-3,"salvo creates a round from actual tube and consumes one on success");
    fire.markAt[2]=-200;Salvo(fire,vehicle,now+2000,config);
    Check(salvoShots==1 && fire.salvoLeft==0,"target behind launcher cannot fire through its hull");
    fire.markAt[2]=200;fire.salvoLeft=2;roundSucceeds=false;Salvo(fire,vehicle,now+4000,config);
    Check(fire.salvoLeft==0,"failed round creation stops the salvo");
    const auto old=fire.gunAt;DriverGun(fire,vehicle,seats,now+6000,config);
    Check(fire.gunAt==old,"failed cannon creation does not consume cooldown");
    Put<std::uint64_t>(weapon,edf::kMuzzleCount,0);fire.salvoLeft=2;Salvo(fire,vehicle,now+8000,config);
    Check(fire.salvoLeft==0 && !RoundFrom(fire,kLauncherSeat,0,from,dir),"missing muzzle never spawns from imaginary hull position");
    std::printf("proteus_frame_test: %d checks, %d failed\n",checks,failures);
    VirtualFree(image,0,MEM_RELEASE);return failures?1:0;
}
