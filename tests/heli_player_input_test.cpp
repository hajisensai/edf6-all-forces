// Production AimFly and PlayerAttitudeHook against a stand-in vehicle and recording stock attitude function.
// The NPC/rescue dependencies below are unavailable stubs: reaching any of them aborts this player-input fixture.
#include "../src/heli.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>

namespace crew {
void SupportAircraftFrame(unsigned char*) noexcept {}   // jet_spawn.cpp: no support aircraft here
bool NpcDriver(const unsigned char* v) noexcept { return v && SeatCount(v)>0 && SeatRider(SeatAt(const_cast<unsigned char*>(v),0))==Rider::dummy; }

unsigned char* image=nullptr;
// Offline (online_authority.h): no session, every heli run here, the copies' owner unchanged.
bool InSession() noexcept { return false; }
bool OnlineRunsHere(const void*) noexcept { return true; }
bool WastedFire(const void*,const void*) noexcept { return false; }   // wastedfire.cpp: no fire wasted here
online::CopyOwner SetSpawnOwner(online::CopyOwner owner) noexcept { return owner; }
Config inputConfig{};
ULONGLONG inputTime=10000;
bool testView=false;
const wchar_t* shotFile=L"EDF6VC_MEDIC_GUN.SGO";
std::size_t shotFileLength=std::wcslen(shotFile);
int fileQueries=0,obsoleteWarnings=0;
const wchar_t* WeaponFile(const unsigned char*,std::size_t* length) noexcept {
    ++fileQueries;*length=shotFileLength;return shotFile;
}
float inputVP[16]{};
const Config& Cfg() noexcept { return inputConfig; }
ULONGLONG GameMs() noexcept { return inputTime; }
bool MapHoldsKeys() noexcept { return false; }
void Log(const char* format,...) noexcept {
    if(std::strstr(format,"obsolete EDF6VC_COAX_MG.SGO"))++obsoleteWarnings;
}
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
// The plugin's jets: reached only by CheckDroneFrame's HeliFrame (testJets), unavailable elsewhere.
bool testJets=false,testFliesItself=false;
int jetFrames=0;
bool IsJet(const void*) noexcept { if(!testJets)MissingDependency();return testJets; }
bool JetFliesItself(const void*) noexcept { if(!testJets)MissingDependency();return testFliesItself; }
bool JetInLine(const float*,const float*,const void*) noexcept { MissingDependency();return false; }
void JetFrame(unsigned char*) noexcept { if(!testJets)MissingDependency();++jetFrames; }
bool SupportRescueAt(const float*,wchar_t*,std::size_t) noexcept { MissingDependency();return false; }
PluginBody BodyOf(const void*) noexcept { MissingDependency();return PluginBody{}; }
bool IsSub(const void*) noexcept { MissingDependency();return false; }
bool SubDeck(const float*,float*) noexcept { MissingDependency();return false; }
bool SupportAircraftOwned(const void*) noexcept { return false; }   // no support deployment in this test
float SubHullGap(const float*) noexcept { MissingDependency();return 0.0f; }
bool IsPlayerJet(const void*) noexcept { MissingDependency();return false; }
bool InstallNpcGunnerAim() noexcept { MissingDependency();return false; }
bool NpcGunnerAimReady() noexcept { return true; }
bool AiGunner(const unsigned char*,const unsigned char*) noexcept { MissingDependency();return false; }
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
alignas(16) unsigned char medicWeapon[0xC00]{};
int shotCalls=0,shotCounter=7;
bool shotReplay=false;
unsigned char shotPermission=0;

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

void __fastcall StockMedicShot(unsigned char* weapon,unsigned muzzle,void* overrideParam,int* counter,bool replay) {
    Check(weapon==medicWeapon && muzzle==3 && overrideParam==(replay ? inputSeat : nullptr) && counter==&shotCounter,
          "shot forwards all register arguments unchanged");
    ++shotCalls;shotReplay=replay;shotPermission=weapon[kWeaponFriendlyDamage];
    Put<int>(weapon,0xBD4,1); // Native fire's recoil marker, reached only if the hook permits the shot.
}

void CheckMedicPermission() {
    Check(!InstallMedicPermission() && !medicShotNext,"unknown shot signature leaves hook off");
    std::memcpy(image+kMedicShot,kMedicShotSig,sizeof(kMedicShotSig));
    Check(InstallMedicPermission() && InstallMedicPermission(),"matching shot installs once");
    auto* trampoline=reinterpret_cast<unsigned char*>(medicShotNext);
    Check(std::memcmp(trampoline,kMedicShotSig,sizeof(kMedicShotSig))==0,"trampoline copies complete 15-byte prologue");
    Check(At<const void*>(trampoline,sizeof(kMedicShotSig)+6)==image+kMedicShot+sizeof(kMedicShotSig),"trampoline resumes at instruction boundary");
    medicShotNext=&StockMedicShot;
    Put<const void*>(medicWeapon,0x120,inputVehicle);Put<const void*>(inputVehicle,0,image+kVt410);
    Put<float>(medicWeapon,kWeaponDamage,-150.0f);
    // Invoke the installed entry, including the fifth (stack) replay argument.
    inputConfig.enabled=false;inputConfig.medicGunnerAim=false;
    for(bool replay:{false,true}) {
        medicWeapon[kWeaponFriendlyDamage]=0; // RideAi or subsequent mission script cleared it.
        reinterpret_cast<MedicShotFn>(image+kMedicShot)(medicWeapon,3,replay ? inputSeat : nullptr,&shotCounter,replay);
        Check(shotReplay==replay,"shot replay stack argument is preserved");
        Check(shotPermission==1,"permission restored before local or replay parameter consumption");
    }
    Check(shotCalls==2,"native shot called exactly once per invocation");
    const int before=shotCalls;
    for(const wchar_t* file:{L"EDF6VC_COAX_MG.SGO",L"edf6vc_coax_mg.sgo"})for(bool replay:{false,true}) {
        shotFile=file;shotFileLength=std::wcslen(file);Put<int>(medicWeapon,0xBD4,0);
        reinterpret_cast<MedicShotFn>(image+kMedicShot)(medicWeapon,3,replay ? inputSeat : nullptr,&shotCounter,replay);
        Check(shotCalls==before && At<int>(medicWeapon,0xBD4)==0,"retired local/replay shot cannot reach native recoil marker");
    }
    Check(obsoleteWarnings==1,"old data prompts one installer-regeneration diagnostic");
    for(const wchar_t* file:{L"V_403TANK_MACHINEGUN.SGO",L"EDF6VC_COAX_MG.SGO.NEW",L"OTHER_COAX_MG.SGO"}) {
        shotFile=file;shotFileLength=std::wcslen(file);
        for(bool replay:{false,true}) {
            const int oldCalls=shotCalls;Put<int>(medicWeapon,0xBD4,0);
            reinterpret_cast<MedicShotFn>(image+kMedicShot)(medicWeapon,3,replay ? inputSeat : nullptr,&shotCounter,replay);
            Check(shotCalls==oldCalls+1 && At<int>(medicWeapon,0xBD4)==1,"stock and other custom machine guns retain local/replay fire");
        }
    }
    shotFile=L"EDF6VC_COAX_MG.SGO";shotFileLength=std::wcslen(shotFile)-1;
    Check(!RetiredLoadout(medicWeapon),"a truncated resource name is not the retired file");
    shotFile=L"EDF6VC_MEDIC_GUN.SGO";shotFileLength=std::wcslen(shotFile);
    Check(fileQueries>0,"shot guard consults the actual resource-file adapter");
    for(float damage:{150.0f,0.0f,std::numeric_limits<float>::quiet_NaN(),-std::numeric_limits<float>::infinity()}) {
        Put<float>(medicWeapon,kWeaponDamage,damage);medicWeapon[kWeaponFriendlyDamage]=0;
        MedicShotHook(medicWeapon,3,nullptr,&shotCounter,false);
        Check(shotPermission==0,"damage/zero/nonfinite gun keeps native no-friendly-fire");
    }
    Put<float>(medicWeapon,kWeaponDamage,-150.0f);Put<const void*>(inputVehicle,0,image+kVt506);
    RestoreMedicPermission(medicWeapon);
    Check(medicWeapon[kWeaponFriendlyDamage]==0,"non-410 healing weapon is unchanged");
    Put<const void*>(medicWeapon,0x120,nullptr);RestoreMedicPermission(medicWeapon);
    Check(medicWeapon[kWeaponFriendlyDamage]==0,"missing owner is unchanged");
    RestoreMedicPermission(nullptr);
    inputConfig.enabled=true;inputConfig.medicGunnerAim=true;
    VirtualFree(trampoline,0,MEM_RELEASE);medicShotNext=nullptr;
}
// Production HeliFrame with one of the plugin's jets whose seats are all empty: a launched drone (JetFliesItself) is
// flown (JetFrame), as an NPC-piloted jet is; a crewed one with no pilot is not (it waits for its real crew).
void CheckDroneFrame() {
    alignas(16) static unsigned char drone[0x2100]{},seats[0x340]{};
    Put<void*>(drone,kSeats,seats);Put<std::uint64_t>(drone,kSeatCount,1);
    Check(SeatRider(SeatAt(drone,0))==Rider::none && !NpcDriver(drone),"the drone's pilot seat is empty");
    const bool wasProfile=profileOk;
    profileOk=true;testJets=true;inputConfig.jetPilot=true;
    testFliesItself=true;jetFrames=0;HeliFrame(drone);
    Check(jetFrames==1,"a launched drone with no one aboard is flown by JetFrame");
    inputConfig.jetPilot=false;jetFrames=0;HeliFrame(drone);
    Check(jetFrames==0,"JetPilot=0 keeps the drone unflown");
    inputConfig.jetPilot=true;testFliesItself=false;jetFrames=0;HeliFrame(drone);
    Check(jetFrames==0,"a crewed jet with an empty pilot seat is not flown (no fabricated pilot)");
    profileOk=wasProfile;testJets=false;testFliesItself=false;
}
}  // namespace

int main() {
    using namespace crew;
    // The copied prologue ends on an instruction boundary before LEA RBP; a mismatch never patches a foreign body.
    constexpr std::size_t imageSize=0x1800000;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,imageSize,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"stand-in image allocated");
    CheckMedicPermission();
    Check(!InstallPlayerAttitude() && !playerAttitudeNext,"unknown attitude entry leaves the hook off");
    std::memcpy(image+kPlayerAttitude,kPlayerAttitudeSig,sizeof(kPlayerAttitudeSig));
    Check(!InstallPlayerAttitude(),"unknown native yaw contract leaves mouse flight off");
    std::memcpy(image+kPlayerYawUpdate,kPlayerYawUpdateSig,sizeof(kPlayerYawUpdateSig));
    std::memcpy(image+kPlayerYawConvert,kPlayerYawConvertSig,sizeof(kPlayerYawConvertSig));
    Check(InstallPlayerAttitude() && playerAttitudeNext,"matching prologue installs the attitude trampoline");
    Check(image[kPlayerAttitude]==0xFF && image[kPlayerAttitude+1]==0x25,"absolute detour installed");
    auto trampoline=reinterpret_cast<void*>(playerAttitudeNext);
    playerAttitudeNext=&StockAttitude;

    Put<const void*>(inputVehicle,kSelfCtrl,inputCtrl);
    Put<const void*>(inputVehicle,kSeats,inputSeat);Put<std::uint64_t>(inputVehicle,kSeatCount,1);
    Put<const void*>(inputSeat,kSeatRider,inputHuman);Put<const void*>(inputSeat,kSeatRiderCtrl,inputCtrl);
    Put<int>(inputCtrl,8,1);Put<const void*>(inputHuman,kHumanPad,inputHuman);inputHuman[kHumanPlayer]=1;
    Put<float>(inputVehicle,kPlayerMaxTilt,0.6f);Put<float>(inputVehicle,kMaxYaw,1.0f);
    Put<float>(inputVehicle,0x1620,0.15f);Put<float>(inputVehicle,kYawSmooth,0.005f);
    Put<float>(inputVehicle,kSpeedGain,20.0f);Put<float>(inputVehicle,kBlend,1.0f);Put<float>(inputVehicle,kDamp,0.99f);
    Put<float>(inputVehicle,kRotor,0.424f);
    Pilot& pilot=pilots[0];pilot=Pilot{};pilot.ref=ObjRef::Of(inputVehicle);pilot.lastMs=inputTime;
    pilot.aim[2]=1.0f;pilot.hover=0.424f;
    const float pos[3]={0.0f,0.0f,0.0f},forward[3]={0.0f,0.0f,1.0f},right[3]={-1.0f,0.0f,0.0f};
    // Actual native angular pipeline: lagged angle +1604, then angle*spring*60 with the body's
    // angular blending. The previous test treated maxYaw as rad/s and missed the Brute's oscillation.
    for(float smoothing:{0.005f,0.03f})for(float sign:{1.0f,-1.0f}) {
        const float raw=sign*50.0f*kPi/180.0f,angularBlend=smoothing<0.01f ? 0.025f : 0.125f;
        Put<float>(inputVehicle,kMaxYaw,raw);Put<float>(inputVehicle,kYawSmooth,smoothing);
        Put<float>(inputVehicle,0x1604,0.0f);
        pilot.aim[0]=std::sin(0.4f);pilot.aim[1]=0.0f;pilot.aim[2]=std::cos(0.4f);
        pilot.yawRate=0.0f;
        float heading=0.0f,bodyRate=0.0f,lastError=0.0f,lastRate=0.0f;
        for(int frame=0;frame<1200;++frame) {
            const float nose[3]={std::sin(heading),0.0f,std::cos(heading)};
            const float side[3]={-std::cos(heading),0.0f,std::sin(heading)};
            AimFly(pilot,inputVehicle,inputSeat,pos,nose,side,false,100.0f,1.0f/60.0f,inputTime);
            float state=At<float>(inputVehicle,0x1604);
            state+=(raw*At<float>(inputVehicle,kInYaw)-state)*smoothing;
            Put<float>(inputVehicle,0x1604,state);
            bodyRate+=(state*0.15f*60.0f-bodyRate)*angularBlend;
            heading+=bodyRate/60.0f;pilot.yawRate+=(bodyRate-pilot.yawRate)*0.3f;
            if(frame>=600){lastError=std::fmax(lastError,std::fabs(heading-0.4f));lastRate=std::fmax(lastRate,std::fabs(bodyRate));}
        }
        Check(lastError<0.01f && lastRate<0.01f,"released mouse settles without repeated yaw reversals under real native lag/gain");
    }
    pilot.aim[0]=pilot.aim[1]=0.0f;pilot.aim[2]=1.0f;pilot.yawRate=0.0f;
    Put<float>(inputVehicle,kMaxYaw,1.0f);Put<float>(inputVehicle,kYawSmooth,0.005f);Put<float>(inputVehicle,0x1604,0.0f);
    // Actual Root.cpk Heron YG10E request parameters (AWEAPON371) and N9 Eros. Exercise the production
    // controller through full acceleration/braking, including the original high-damping failure. The instructor's
    // cyclic: the aim pitched down the full tilt flies the top speed, level it stops.
    const float fullTilt=At<float>(inputVehicle,kPlayerMaxTilt);
    const auto pitchAim=[&](float el){pilot.aim[0]=pilot.aim[1]=0.0f;pilot.aim[2]=1.0f;Put<float>(inputSeat,kSeatLY,el/fullTilt);};
    // The stock actuators the player's frames quicken (PlayerMouseTune) and put back.
    Put<float>(inputVehicle,kRotorUp,0.001f);Put<float>(inputVehicle,kRotorDown,0.0007f);Put<float>(inputVehicle,kTiltSmooth,0.005f);
    Put<float>(inputVehicle,kLiftPerRotor,34.0f/60.0f);Put<float>(inputVehicle,kVertDamp,0.95f);Put<float>(inputVehicle,kRotorIdle,0.13f);
    for(float damp:{0.99f,0.999f}) {
        const float gain=damp<0.995f ? 90.0f : 80.0f,blend=damp<0.995f ? 0.003f : 0.0003f;
        const float top=blend*gain/(1.0f-damp*(1.0f-blend));
        Put<float>(inputVehicle,kSpeedGain,gain);Put<float>(inputVehicle,kBlend,blend);Put<float>(inputVehicle,kDamp,damp);
        PlayerAssist(inputVehicle);
        PlayerMouseTune(inputVehicle,true);
        Check(At<float>(inputVehicle,kRotorUp)==aim::kPlayerRotorRate && At<float>(inputVehicle,kRotorDown)==aim::kPlayerRotorRate &&
              At<float>(inputVehicle,kTiltSmooth)==kPlayerTiltSmooth,"mouse flight quickens the rotor and the tilt");
        Check(std::fabs(PlayerTop(inputVehicle)-top)<0.01f,"player assist preserves the request's actual top speed");
        pilot.hold={};pilot.vel[0]=pilot.vel[1]=pilot.vel[2]=0.0f;
        pitchAim(-fullTilt);
        for(int frame=0;frame<600;++frame) {
            AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,false,100.0f,1.0f/60.0f,inputTime);
            const float desired=At<float>(inputVehicle,kSpeedGain)*At<float>(inputVehicle,kInForward);
            pilot.vel[2]=damp*pilot.vel[2]+At<float>(inputVehicle,kBlend)*(desired-damp*pilot.vel[2]);
        }
        Check(std::fabs(pilot.hold.speed-top)<0.01f && std::fabs(pilot.vel[2]-top)<0.05f,
              "nose down the full tilt reaches the real helicopter top speed through the native velocity law");
        pitchAim(0.0f);
        for(int frame=0;frame<600;++frame) {
            AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,false,100.0f,1.0f/60.0f,inputTime);
            const float desired=At<float>(inputVehicle,kSpeedGain)*At<float>(inputVehicle,kInForward);
            pilot.vel[2]=damp*pilot.vel[2]+At<float>(inputVehicle,kBlend)*(desired-damp*pilot.vel[2]);
        }
        Check(pilot.hold.speed==0.0f && std::fabs(pilot.vel[2])<0.05f,"a level nose brakes to hover without reversing");
        Put<float>(inputSeat,kSeatLY,1.0f);Put<float>(inputVehicle,kInThrottle,0.75f);
        pitchAim(-fullTilt);
        AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,true,0.0f,1.0f/60.0f,inputTime);
        Check(pilot.hold.speed==0.0f && At<float>(inputVehicle,kInForward)==0.0f && At<float>(inputVehicle,kInThrottle)==0.75f,
              "grounded controller leaves the native takeoff throttle and clears forward motion");
        Put<float>(inputSeat,kSeatLY,-1.0f);
        AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,true,0.0f,1.0f/60.0f,inputTime);
        Check(At<float>(inputVehicle,kInThrottle)==0.75f && pilot.pitchIn==0.0f,"W on the ground does not change collective or pitch the parked helicopter");
        Put<float>(inputSeat,kSeatAscend,1.0f);
        AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,true,0.0f,1.0f/60.0f,inputTime);
        Check(At<float>(inputVehicle,kInThrottle)==1.0f && At<float>(inputVehicle,kInForward)==0.0f,"Space takes off vertically even while W is held");
        Put<float>(inputSeat,kSeatAscend,0.0f);
        Put<float>(inputSeat,kSeatLY,0.0f);pitchAim(0.0f);
        PlayerMouseTune(inputVehicle,false);
        Check(At<float>(inputVehicle,kRotorUp)==0.001f && At<float>(inputVehicle,kRotorDown)==0.0007f &&
              At<float>(inputVehicle,kTiltSmooth)==0.005f,"a pad puts the stock rotor and tilt back");
        PlayerMouseTune(inputVehicle,true);
        pilot.groundAt=0;AssistOff(inputVehicle);
        Check(At<float>(inputVehicle,kSpeedGain)==gain && At<float>(inputVehicle,kBlend)==blend,"leaving restores request parameters");
        Check(At<float>(inputVehicle,kRotorUp)==0.001f && At<float>(inputVehicle,kRotorDown)==0.0007f &&
              At<float>(inputVehicle,kTiltSmooth)==0.005f,"leaving restores the stock rotor and tilt");
        Heli npc{},replica{};
        inputConfig.heliSpeed=25.0f;inputConfig.heliAgility=4.0f;
        Tune(npc,inputVehicle);Tune(replica,inputVehicle);
        const float tuned=npc.params[1]*npc.params[0]/(1.0f-damp*(1.0f-npc.params[1]));
        Check(std::fabs(tuned-25.0f)<0.01f && npc.top==25.0f,"NPC speed tuning remains valid with high native damping");
        Check(std::memcmp(npc.params,replica.params,sizeof(npc.params))==0,"replica derives the same physics parameters as authority");
        inputConfig.heliSpeed=0.0f;Tune(npc,inputVehicle);
        Check(std::fabs(npc.top-top)<0.01f,"disabled NPC speed tuning reports actual stock speed rather than a fixed estimate");
    }
    inputConfig.heliSpeed=25.0f;
    pilot.hold={};pilot.vel[0]=pilot.vel[1]=pilot.vel[2]=0.0f;
    Put<float>(inputVehicle,kSpeedGain,20.0f);Put<float>(inputVehicle,kBlend,1.0f);
    // Stock slot 55 has already copied W into movement input. Production AimFly replaces the movement setpoint with the
    // keyboard cyclic; W no longer changes the collective, and the attitude hook receives keyboard pitch.
    Put<float>(inputSeat,kSeatLY,-1.0f);
    const float throttleLevel=0.424f;Put<float>(inputVehicle,kRotor,throttleLevel);
    AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,false,100.0f,1.0f/60.0f,inputTime);
    float* movement=reinterpret_cast<float*>(inputVehicle+kInLateral);
    unsigned char contact=0;
    PlayerAttitudeHook(inputVehicle+kHeadRight,inputVehicle,movement,&contact);
    Check(pilot.hold.speed>0.0f && movement[2]>0.0f && recordedInput[2]>0.0f && pilot.hold.holding,
          "W pitches nose down and flies forward while keeping height");
    Check(recordedInput[1]==movement[1] && recordedInput[3]==movement[3] && recordedInput[4]==movement[4],
          "throttle, w and yaw reach the attitude function unchanged");
    Put<float>(inputSeat,kSeatLY,1.0f);Put<float>(inputSeat,kSeatRY,1.0f);
    for(int frame=0;frame<3;++frame)AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,false,100.0f,1.0f/60.0f,inputTime);
    PlayerAttitudeHook(inputVehicle+kHeadRight,inputVehicle,movement,&contact);
    Check(pilot.aim[1]<0.0f && recordedInput[2]<0.0f && pilot.hold.speed<0.0f && movement[2]<0.0f,
          "S raises the nose and backs off even when mouse aims down");
    Put<float>(inputSeat,kSeatLY,-1.0f);Put<float>(inputSeat,kSeatRY,-1.0f);
    for(int frame=0;frame<6;++frame)AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,false,100.0f,1.0f/60.0f,inputTime);
    PlayerAttitudeHook(inputVehicle+kHeadRight,inputVehicle,movement,&contact);
    Check(pilot.aim[1]>0.0f && recordedInput[2]>0.0f && pilot.hold.speed>0.0f && movement[2]>0.0f,
          "W lowers the nose and flies forward even when mouse aims up");
    // A coordinated turn: flying forward with the nose turning toward row 0 (a slide's way), the roll the attitude gets is
    // the lateral plus the bank into the turn; the movement's lateral stays the velocity law's.
    Put<float>(inputSeat,kSeatRY,0.0f);
    pilot.vel[2]=15.0f;
    const float turned=0.2f;   // course wanted toward row 0 (right = (-1, 0, 0) here)
    pilot.aim[0]=-std::sin(turned)*std::cos(0.3f);pilot.aim[1]=-std::sin(0.3f);pilot.aim[2]=std::cos(turned)*std::cos(0.3f);
    for(int frame=0;frame<20;++frame) {
        AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,false,100.0f,1.0f/60.0f,inputTime);
    }
    PlayerAttitudeHook(inputVehicle+kHeadRight,inputVehicle,movement,&contact);
    Check(recordedInput[0]>movement[0]+0.1f,"course wanted toward row 0 banks the way a slide there rolls it");
    pitchAim(-0.3f);pilot.yawRate=1.0f;
    AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,false,100.0f,1.0f/60.0f,inputTime);
    PlayerAttitudeHook(inputVehicle+kHeadRight,inputVehicle,movement,&contact);
    Check(std::fabs(recordedInput[0]-movement[0])<1e-5f,"on course a measured yaw transient cannot feed its own bank");
    pilot.yawRate=0.0f;
    pilot.vel[2]=0.0f;
    pitchAim(0.0f);

    inputSeat[kSeatPad]=1;CheckPassThrough("pad control retains stock pitch");inputSeat[kSeatPad]=0;
    inputHuman[kHumanPlayer]=0;CheckPassThrough("NPC/non-player control retains stock pitch");inputHuman[kHumanPlayer]=1;
    inputHuman[0x128]=1;CheckPassThrough("remote player retains stock pitch");inputHuman[0x128]=0;
    pilot.flying=false;CheckPassThrough("inactive mouse flight retains stock pitch");pilot.flying=true;
    ++inputTime;CheckPassThrough("stale input from the previous frame is not applied");--inputTime;
    inputConfig.heliMouseAim=false;CheckPassThrough("disabled mouse mode retains stock pitch");inputConfig.heliMouseAim=true;
    inputConfig.enabled=false;CheckPassThrough("disabled plugin retains stock pitch");inputConfig.enabled=true;

    // A downward native Type-1 camera initializes once, then follows the world mouse direction without a screen tether.
    const float stockEye[3]={0.0f,10.0f,-10.0f},stockLook[3]={0.0f,5.0f,0.0f};
    float cameraEye[3],cameraLook[3];
    pilotCamera={};
    PublishPilotView({pilot.ref,inputSeat,inputTime,{pilot.aim[0],pilot.aim[1],pilot.aim[2]}});
    Check(PlayerHeliCamera(inputSeat,stockEye,stockLook,cameraEye,cameraLook),"local current-frame pilot owns the locator camera");
    Check(pilotCamera.dir[1]<0.0f && std::fabs(cameraEye[1]-10.0f)<1e-5f,"orbit starts from native camera elevation and distance");
    testView=true;std::memset(inputVP,0,sizeof(inputVP)); // Every aim is outside this projection.
    Put<float>(inputSeat,kSeatRX,1.0f);Put<float>(inputSeat,kSeatRY,-0.1f);
    for(int frame=0;frame<180;++frame)AimFly(pilot,inputVehicle,inputSeat,pos,forward,right,false,100.0f,1.0f/60.0f,inputTime);
    const float heldAim[3]={pilot.aim[0],pilot.aim[1],pilot.aim[2]};
    Check(std::fabs(heldAim[0])>0.1f,"mouse yaw moves freely despite an entirely off-screen aim");
    Check(At<float>(inputSeat,kSeatRX)==1.0f && At<float>(inputSeat,kSeatRY)==-0.1f,"flight preserves native seat mouse input");
    Check(PlayerHeliCamera(inputSeat,stockEye,stockLook,cameraEye,cameraLook),"camera follows the changed mouse heading and elevation");
    const float initialViewX=pilotCamera.dir[0];
    inputTime+=16;PublishPilotView({pilot.ref,inputSeat,inputTime,{heldAim[0],heldAim[1],heldAim[2]}});
    Check(PlayerHeliCamera(inputSeat,stockEye,stockLook,cameraEye,cameraLook) && pilotCamera.dir[0]!=initialViewX &&
          std::fabs(pilotCamera.dir[0]-heldAim[0])>0.001f,"camera eases toward mouse instead of jumping to it");
    const float once=pilotCamera.dir[0];
    PlayerHeliCamera(inputSeat,stockEye,stockLook,cameraEye,cameraLook);
    Check(pilotCamera.dir[0]==once,"multiple camera fetches in one game frame do not speed up easing");
    Put<float>(inputSeat,kSeatRX,0.0f);Put<float>(inputSeat,kSeatRY,0.0f);
    const float turnedNose[3]={1.0f,0.0f,0.0f},turnedRight[3]={0.0f,0.0f,1.0f};
    for(int frame=0;frame<180;++frame) {
        inputTime+=16;
        AimFly(pilot,inputVehicle,inputSeat,pos,turnedNose,turnedRight,false,100.0f,1.0f/60.0f,inputTime);
        Check(PlayerHeliCamera(inputSeat,stockEye,stockLook,cameraEye,cameraLook),"camera remains owned with released mouse");
    }
    for(int i=0;i<3;++i)Check(pilot.aim[i]==heldAim[i],"released mouse is not recentered by camera bounds or hull turning");
    for(int i=0;i<3;++i)Check(std::fabs(pilotCamera.dir[i]-heldAim[i])<1e-5f,"camera converges to released mouse direction without reversing");
    // One second of smoothing is independent of camera FPS, including a wrap across +/-pi.
    const float target[3]={-0.1f,0.0f,-std::sqrt(0.99f)};
    float lowFps[3]={0.1f,0.0f,-std::sqrt(0.99f)},highFps[3]={lowFps[0],lowFps[1],lowFps[2]};
    for(int i=0;i<30;++i)aim::EaseView(lowFps,target,1.0f/30.0f);
    for(int i=0;i<144;++i)aim::EaseView(highFps,target,1.0f/144.0f);
    Check(std::fabs(lowFps[0]-highFps[0])<1e-5f && lowFps[2]<-0.99f,"ease is frame independent and takes shortest yaw across pi");
    Put<float>(inputVehicle,kMatrix+12*4,20.0f);
    Check(PlayerHeliCamera(inputSeat,stockEye,stockLook,cameraEye,cameraLook) && cameraLook[0]==20.0f,"orbit translates with aircraft without changing view heading");
    pilotCamera.dir[0]=0.0f;pilotCamera.dir[1]=std::sin(1.0f);pilotCamera.dir[2]=std::cos(1.0f);
    Check(PlayerHeliCamera(inputSeat,stockEye,stockLook,cameraEye,cameraLook) && cameraEye[1]>=stockEye[1],
          "looking up keeps orbit eye above the aircraft at its native height");
    Check(std::fabs((cameraLook[1]-cameraEye[1])/pilotCamera.radius-pilotCamera.dir[1])<1e-5f,
          "height floor lifts both camera points without changing the viewing ray");
    inputSeat[kSeatPad]=1;Check(!PlayerHeliCamera(inputSeat,stockEye,stockLook,cameraEye,cameraLook),"pad camera remains stock");inputSeat[kSeatPad]=0;
    inputHuman[0x128]=1;Check(!PlayerHeliCamera(inputSeat,stockEye,stockLook,cameraEye,cameraLook),"remote player's camera is untouched");inputHuman[0x128]=0;
    inputConfig.heliMouseAim=false;Check(!PlayerHeliCamera(inputSeat,stockEye,stockLook,cameraEye,cameraLook),"disabled flight releases camera");inputConfig.heliMouseAim=true;
    inputTime+=251;Check(!PlayerHeliCamera(inputSeat,stockEye,stockLook,cameraEye,cameraLook),"stale flight never owns camera");inputTime-=251;
    CheckDroneFrame();
    VirtualFree(trampoline,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);
    std::printf("heli_player_input: %d production-path checks passed (%d attitude calls)\n",checks,calls);
    return 0;
}
