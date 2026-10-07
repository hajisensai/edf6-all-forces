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
    Put<std::uint16_t>(seats,kSeatButtons,static_cast<std::uint16_t>(config.proteusModeButton));Tick();
    Put<std::uint16_t>(seats,kSeatButtons,0);for(int i=0;i<35;++i)Tick();
    Check(PlayerProteus(&ro) && ro.mode==proteus::Mode::deployed,"native player frames advance deployment to completion");
    Check(At<float>(vehicle,kWalk)==0 && At<float>(vehicle,kJump)==0,"deployed leg and jump parameters change in real object");
    config.proteus=false;Tick();
    Check(At<float>(vehicle,kWalk)==10 && At<float>(vehicle,kJump)==8,"setting off restores stock legs through same native tick");
    Check(At<int>(seats+2*kSeatStride,kSeatClassMask)==15 && At<int>(seats+3*kSeatStride,kSeatClassMask)==15,"setting off restores original seat masks");
    std::printf("proteus_frame_test: %d checks, %d failed\n",checks,failures);
    VirtualFree(image,0,MEM_RELEASE);return failures?1:0;
}
