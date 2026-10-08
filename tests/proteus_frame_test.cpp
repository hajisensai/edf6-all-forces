// Exercise the production ProteusFrame reached by the native slot-4 shared vehicle tick, with an actual player
// rider and four embedded seats. No AI input task is simulated. The EDF functions stay uncalled in this fixture.
#include "../src/proteus.cpp"
#include <cstdio>
namespace crew {
unsigned char* image=nullptr;PlayerFix player{};
namespace {
Config config{};ULONGLONG now=3600000,frame=1;
unsigned char vehicle[0x3000]{},seats[4*kSeatStride]{},human[0x400]{},ctrl[16]{};
int failures=0,checks=0;
void Check(bool pass,const char* what){++checks;if(!pass){++failures;std::printf("FAIL %s\n",what);}}
void Tick(){++frame;now+=100;ProteusFrame(vehicle);}
}
const Config& Cfg() noexcept{return config;}
ULONGLONG GameMs() noexcept{return now;}ULONGLONG GameFrame() noexcept{return frame;}
void Log(const char*,...) noexcept{}
bool MapHoldsKeys() noexcept{return false;}
bool KnownVehicle(const void*) noexcept{return false;}
bool CameraRay(float*,float*) noexcept{return false;}
bool VisitEnemies(const unsigned char*,EnemyVisitor,void*) noexcept{return true;}
float MapRay(const float*,const float*,float*) noexcept{return -1.0f;}
unsigned char* PlayerHuman() noexcept{return human;}
void ProteusRoundsReady(bool* gun,bool* salvo) noexcept{if(gun)*gun=false;if(salvo)*salvo=false;}
bool ProteusGunRound(const unsigned char*,const float*,const float*,float) noexcept{return false;}
bool ProteusSalvoRound(const unsigned char*,const float*,const float*,float) noexcept{return false;}
unsigned char* BoneRecord506(const unsigned char*,const wchar_t*) noexcept{return nullptr;}
}
int main(){
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)return 2;
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
    std::printf("proteus_frame_test: %d checks, %d failed\n",checks,failures);
    VirtualFree(image,0,MEM_RELEASE);return failures?1:0;
}
