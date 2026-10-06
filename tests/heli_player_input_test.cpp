// Production AimFly and PlayerAttitudeHook against a stand-in vehicle and recording stock attitude function.
// The NPC/rescue dependencies below are unavailable stubs: reaching any of them aborts this player-input fixture.
#include "../src/heli.cpp"
#include <cstdio>
#include <cstdlib>

namespace crew {
unsigned char* image=nullptr;
Config inputConfig{};
ULONGLONG inputTime=10000;
bool testView=false;
float inputVP[16]{};
const Config& Cfg() noexcept { return inputConfig; }
ULONGLONG GameMs() noexcept { return inputTime; }
bool MapHoldsKeys() noexcept { return false; }
void Log(const char*,...) noexcept {}
bool LastViewProj(float* out) noexcept {
    if(testView)std::memcpy(out,inputVP,sizeof(inputVP));
    return testView;
}
bool CameraRay(float* eye,float* dir) noexcept {
    eye[0]=eye[1]=eye[2]=0.0f;
    dir[0]=0.0f;dir[1]=-0.5f;dir[2]=std::sqrt(0.75f);
    return testView;
}
PlayerFix player{};
ULONGLONG GameFrame() noexcept { return inputTime/16; }
void MissingDependency() noexcept { void(*volatile stop)()=std::abort;stop(); }
void SuppressBump(bool) noexcept { MissingDependency(); }
void SetObjectTeam(unsigned char*,std::int32_t) noexcept { MissingDependency(); }
bool IsJet(const void*) noexcept { MissingDependency();return false; }
bool JetInLine(const float*,const float*,const void*) noexcept { MissingDependency();return false; }
void JetFrame(unsigned char*) noexcept { MissingDependency(); }
unsigned char* HeliLaunch(HeliBody,const float*,const float*) noexcept { MissingDependency();return nullptr; }
PluginBody BodyOf(const void*) noexcept { MissingDependency();return PluginBody{}; }
bool IsSub(const void*) noexcept { MissingDependency();return false; }
bool SubDeck(const float*,float*) noexcept { MissingDependency();return false; }
float SubHullGap(const float*) noexcept { MissingDependency();return 0.0f; }
bool IsPlayerJet(const void*) noexcept { MissingDependency();return false; }
bool IsSazabi(const void*) noexcept { return false; }
namespace jet { int LockersOf(const void*,float (*)[3],int) noexcept { MissingDependency();return 0; } }
int MissilesHomingAt(const float*,float,float (*)[3],int) noexcept { MissingDependency();return 0; }
unsigned char* PlayerHuman() noexcept { MissingDependency();return nullptr; }
bool FuelGauge(const void*,FuelReading*) noexcept { MissingDependency();return false; }
PlayArea MapPlayArea() noexcept { MissingDependency();return PlayArea{}; }
bool MoveAreaBox(float*,float*) noexcept { MissingDependency();return false; }
bool CommandVehicleLive(const ObjRef&) noexcept { MissingDependency();return false; }
bool ReadCommandUnit(const ObjRef&,const char*,const mapcmd::Command&,bool,CommandUnit*) noexcept { MissingDependency();return false; }
float GroundClearance(const float*) noexcept { MissingDependency();return 0.0f; }
float CeilingY() noexcept { MissingDependency();return 0.0f; }
float GameStep(ULONGLONG) noexcept { MissingDependency();return 1.0f/60.0f; }
float ClosureIn(const float*,const float*,float,float,float,float,bool*) noexcept { MissingDependency();return 0.0f; }
Gpws GpwsOf(float,bool) noexcept { MissingDependency();return Gpws::none; }
}

namespace {
using namespace crew;
alignas(16) unsigned char inputVehicle[0x2100]{},inputSeat[0x340]{},inputHuman[0x400]{},inputCtrl[0x10]{};
float recordedInput[5]{};
const float* recordedPointer=nullptr;
const unsigned char* recordedContact=nullptr;
void* recordedBody=nullptr;
int checks=0,calls=0;

void Check(bool condition,const char* what) {
    ++checks;
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
void __fastcall StockAttitude(unsigned char* attitude,void* body,const float* input,const unsigned char* contact) {
    Check(attitude==inputVehicle+kHeadRight,"stock attitude receives the same attitude object");
    recordedBody=body;recordedContact=contact;recordedPointer=input;
    std::memcpy(recordedInput,input,sizeof(recordedInput));++calls;
}

void CheckPassThrough(const char* what) {
    float input[5]={0.2f,0.4f,0.7f,1.0f,-0.3f};
    const unsigned char contact=2;
    PlayerAttitudeHook(inputVehicle+kHeadRight,inputVehicle,input,&contact);
    Check(recordedPointer==input && std::memcmp(recordedInput,input,sizeof(input))==0,what);
    Check(recordedBody==inputVehicle && recordedContact==&contact,"stock body/contact arguments are preserved");
}
}  // namespace

int main() {
    using namespace crew;
    // The copied prologue ends on an instruction boundary before LEA RBP; a mismatch never patches a foreign body.
    constexpr std::size_t imageSize=0x660000;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,imageSize,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"stand-in image allocated");
    Check(!InstallPlayerAttitude() && !playerAttitudeNext,"unknown attitude entry leaves the hook off");
    std::memcpy(image+kPlayerAttitude,kPlayerAttitudeSig,sizeof(kPlayerAttitudeSig));
    Check(InstallPlayerAttitude() && playerAttitudeNext,"matching prologue installs the attitude trampoline");
    Check(image[kPlayerAttitude]==0xFF && image[kPlayerAttitude+1]==0x25,"absolute detour installed");
    auto trampoline=reinterpret_cast<void*>(playerAttitudeNext);
    playerAttitudeNext=&StockAttitude;

    Put<const void*>(inputVehicle,kSelfCtrl,inputCtrl);
    Put<const void*>(inputVehicle,kSeats,inputSeat);Put<std::uint64_t>(inputVehicle,kSeatCount,1);
    Put<const void*>(inputSeat,kSeatRider,inputHuman);Put<const void*>(inputSeat,kSeatRiderCtrl,inputCtrl);
    Put<int>(inputCtrl,8,1);Put<const void*>(inputHuman,kHumanPad,inputHuman);inputHuman[kHumanPlayer]=1;
    Put<float>(inputVehicle,kPlayerMaxTilt,0.6f);Put<float>(inputVehicle,kMaxYaw,1.0f);
    Put<float>(inputVehicle,kSpeedGain,20.0f);Put<float>(inputVehicle,kBlend,1.0f);Put<float>(inputVehicle,kDamp,0.99f);
    Put<float>(inputVehicle,kRotor,0.424f);
    Pilot& pilot=pilots[0];pilot=Pilot{};pilot.ref=ObjRef::Of(inputVehicle);pilot.lastMs=inputTime;
    pilot.aim[2]=1.0f;pilot.hover=0.424f;
    const float pos[3]={0.0f,0.0f,0.0f},forward[3]={0.0f,0.0f,1.0f},right[3]={-1.0f,0.0f,0.0f};
    // Stock slot 55 has already copied W into movement input. Production AimFly replaces the movement setpoint,
    // then the attitude hook supplies level pitch while keeping the original movement block for translation.
    Put<float>(inputSeat,kSeatLY,-1.0f);
    AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,false,100.0f,1.0f/60.0f,inputTime);
    float* movement=reinterpret_cast<float*>(inputVehicle+kInLateral);
    const float before=movement[2];
    unsigned char contact=0;
    PlayerAttitudeHook(inputVehicle+kHeadRight,inputVehicle,movement,&contact);
    Check(before>0.0f && movement[2]==before && recordedInput[2]==0.0f,"W increases speed without pitching the nose");
    Check(recordedInput[0]==movement[0] && recordedInput[1]==movement[1] && recordedInput[3]==movement[3] &&
          recordedInput[4]==movement[4],"only attitude pitch changes, other input channels are preserved");
    Put<float>(inputSeat,kSeatLY,0.0f);Put<float>(inputSeat,kSeatRY,-1.0f);
    const float speedSet=pilot.hold.speed;
    AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,false,100.0f,1.0f/60.0f,inputTime);
    PlayerAttitudeHook(inputVehicle+kHeadRight,inputVehicle,movement,&contact);
    Check(pilot.aim[1]>0.0f && recordedInput[2]<0.0f && pilot.hold.speed==speedSet,"mouse up raises the nose without changing forward speed");
    const float mousePitch=recordedInput[2];
    Put<float>(inputSeat,kSeatLY,1.0f);Put<float>(inputSeat,kSeatRY,0.0f);
    AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,false,100.0f,1.0f/60.0f,inputTime);
    PlayerAttitudeHook(inputVehicle+kHeadRight,inputVehicle,movement,&contact);
    Check(pilot.hold.speed<speedSet && recordedInput[2]==mousePitch,"S lowers the speed setpoint without changing mouse pitch");
    Put<float>(inputSeat,kSeatLY,0.0f);Put<float>(inputSeat,kSeatRY,1.0f);
    for(int frame=0;frame<2;++frame)AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,false,100.0f,1.0f/60.0f,inputTime);
    PlayerAttitudeHook(inputVehicle+kHeadRight,inputVehicle,movement,&contact);
    Check(pilot.aim[1]<0.0f && recordedInput[2]>0.0f,"mouse down lowers the nose through the stock pitch sign");

    inputSeat[kSeatPad]=1;CheckPassThrough("pad control retains stock pitch");inputSeat[kSeatPad]=0;
    inputHuman[kHumanPlayer]=0;CheckPassThrough("NPC/non-player control retains stock pitch");inputHuman[kHumanPlayer]=1;
    inputHuman[0x128]=1;CheckPassThrough("remote player retains stock pitch");inputHuman[0x128]=0;
    pilot.flying=false;CheckPassThrough("inactive mouse flight retains stock pitch");pilot.flying=true;
    ++inputTime;CheckPassThrough("stale input from the previous frame is not applied");--inputTime;
    inputConfig.heliMouseAim=false;CheckPassThrough("disabled mouse mode retains stock pitch");inputConfig.heliMouseAim=true;
    inputConfig.enabled=false;CheckPassThrough("disabled plugin retains stock pitch");inputConfig.enabled=true;

    // A 30-degree downward camera: the level nose is above its vertical FOV. The old fallback was that same
    // off-screen nose, so every attempted mouse movement was undone and the square never reappeared.
    const float c=std::sqrt(0.75f),s=0.5f;
    const float downward[16]={1,0,0,0, 0,2.5f*c,-s,-s, 0,2.5f*s,c,c, 0,0,-0.1f,0};
    std::memcpy(inputVP,downward,sizeof(downward));testView=true;
    pilot.aim[0]=pilot.aim[1]=0.0f;pilot.aim[2]=1.0f;
    Put<float>(inputSeat,kSeatRX,0.0f);Put<float>(inputSeat,kSeatRY,0.0f);
    Check(!aim::OnScreen(inputVP,pos,pilot.aim,kPlayerMark,kAimOnScreen),"regression camera starts with the level nose off-screen");
    AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,false,100.0f,1.0f/60.0f,inputTime);
    Check(pilot.aim[1]==0.0f,"a camera looking down alone does not command a descent");
    Put<float>(inputSeat,kSeatRX,0.5f);Put<float>(inputSeat,kSeatRY,0.2f);
    AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,false,100.0f,1.0f/60.0f,inputTime);
    Check(aim::OnScreen(inputVP,pos,pilot.aim,kPlayerMark,kAimOnScreen),"mouse movement recovers an aim inside the downward camera view");
    const float oldX=pilot.aim[0];
    AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,false,100.0f,1.0f/60.0f,inputTime);
    Check(pilot.aim[0]!=oldX && aim::OnScreen(inputVP,pos,pilot.aim,kPlayerMark,kAimOnScreen),"subsequent mouse movement remains effective and visible");
    VirtualFree(trampoline,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);
    std::printf("heli_player_input: %d production-path checks passed (%d attitude calls)\n",checks,calls);
    return 0;
}
