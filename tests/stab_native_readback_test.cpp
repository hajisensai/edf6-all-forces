// Production stabilizer + exact native joint-readback/aim-step/bone-map chain. Optional EDF.dll, no game.
#include "../src/stab.cpp"
#include "edf/host.h"
#include <cstdio>
#include <cstdlib>

namespace crew {
unsigned char* image=nullptr;
Config config{};
ULONGLONG testFrame=1;
const Config& Cfg() noexcept { return config; }
ULONGLONG GameFrame() noexcept { return testFrame; }
PluginBody BodyOf(const void*) noexcept { return PluginBody::none; }
bool IsPlayerJet(const void*) noexcept { return false; }
bool IsSub(const void*) noexcept { return false; }
bool IsDrillTank(const void*) noexcept { return false; }
bool IndirectFireSeat(const unsigned char*) noexcept { return false; }
int AutoTurretSteers(const void*,unsigned) noexcept { return 0; }
int AutoTurretStabAware() noexcept { return 1; }
void Log(const char*,...) noexcept {}
}
namespace edf {
// No real scene/skeleton in the fixture: mount probing is intentionally left at its known seat-0 hull basis.
bool MeanMuzzle(const unsigned char*,std::uint64_t,float*,float*) noexcept { return false; }
}
namespace {
using namespace crew;
int checks=0;
void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
constexpr float deg=stab::kPi/180.0f;
alignas(16) unsigned char vehicle[0x3000]{},seat[0x340]{},gun[0x1800]{},holder[0x48]{},human[0x2100]{},ctrl[16]{},bones[2][0x38]{};
void* holders[]={holder};
unsigned char* aim=seat+kSeatAim;
void Hull(float yaw) {
    const stab::Frame id{{1,0,0,0,1,0,0,0,1}};
    const stab::Frame frame=stab::Turned(id,yaw);
    for(int r=0;r<3;++r)std::memcpy(vehicle+kMatrix+r*16,frame.r+r*3,12);
}
Entry* Setup() {
    std::memset(vehicle,0,sizeof vehicle);std::memset(seat,0,sizeof seat);std::memset(bones,0,sizeof bones);
    Put<void*>(vehicle,0,image+0x17D8FA0);Put<void*>(vehicle,kSelfCtrl,ctrl);
    Put<void*>(vehicle,kSeats,seat);Put<std::uint64_t>(vehicle,kSeatCount,1);
    Put<void*>(seat,kSeatRider,human);Put<void*>(seat,kSeatRiderCtrl,ctrl);Put<LONG>(ctrl,8,1);
    Put<unsigned char>(human,edf::kHumanPlayer,1);Put<void*>(human,edf::kHumanPad,human);
    Put<void*>(seat,kSeatWeapons,holders);Put<std::uint64_t>(seat,kSeatWeaponCount,1);Put<void*>(holder,kHolderWeapon,gun);
    Put<std::uint64_t>(gun,edf::kMuzzleCount,1);
    for(int i=0;i<2;++i) {
        auto* axis=reinterpret_cast<unsigned char*>(AxisOf(aim,i));
        Put<float>(axis,0,-stab::kPi);Put<float>(axis,4,stab::kPi);
        Put<void*>(axis,0x28,bones[i]);Put<std::uint64_t>(axis,0x38,1);
        Put<float>(bones[i],4,-stab::kPi);Put<float>(bones[i],8,stab::kPi);
    }
    Put<float>(aim,kAimParams,0.1f);Put<float>(aim,kAimParams+4,0.1f);Put<float>(aim,kAimParams+8,0.5f*deg);
    Hull(0);ResetStabilizer();testFrame=1;StabFrame(vehicle);
    alignas(16) const float none[4]{};
    StabStep(aim,none,reinterpret_cast<AimStepFn>(image+kPlainAimStep));
    Entry* e=Find(aim);Check(e && e->active && e->hold.live,"production seat-0 stabilizer active");return e;
}
void Step() {
    StabFrame(vehicle);alignas(16) const float none[4]{};
    StabStep(aim,none,reinterpret_cast<AimStepFn>(image+kPlainAimStep));
}
}
int wmain(int argc,wchar_t** argv) {
    using namespace crew;
    if(argc!=2 || GetFileAttributesW(argv[1])==INVALID_FILE_ATTRIBUTES){std::puts("SKIP: pass supported EDF.dll");return 77;}
    const auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    image=edf::IdentifyImage(module);
    Check(image!=nullptr,"supported private EDF.dll mapping (no DllMain)");config.debug=false;
    DWORD old=0;Check(VirtualProtect(image+0x5FC230,1,PAGE_EXECUTE_READWRITE,&old)!=0,"private signature negative control writable");
    const unsigned char originalWrite=image[0x5FC230];image[0x5FC230]^=1;
    Check(!InstallStabilizer() && !hooked,"changed native readback write disables every stabilizer path");
    image[0x5FC230]=originalWrite;DWORD restored=0;
    Check(VirtualProtect(image+0x5FC230,1,old,&restored)!=0,"private native instruction restored before execution");
    FlushInstructionCache(GetCurrentProcess(),image+0x5FC230,1);
    Check(InstallStabilizer(),"production stabilizer signatures and native readback call patched");
    const auto patchedReadback=reinterpret_cast<ReadbackFn>(image+kReadbackCall+5+At<std::int32_t>(image+kReadbackCall,1));
    Entry* e=Setup();testFrame=2;
    // Negative control: exact native physical joint is half a degree behind the previous target.
    // The old path keeps the old absolute reference and pumps almost the whole native frame's travel back into it.
    nextReadback(aim,0,0,-0.5f*deg);Step();
    const float legacy=e->hold.shift[0];
    Check(legacy>0.45f*deg,"legacy native readback provokes stationary-hull corrective motor command");
    e=Setup();testFrame=2;patchedReadback(aim,0,0,-0.5f*deg);Step();
    Check(std::fabs(e->hold.shift[0])<0.0001f*deg,"native readback reconciliation removes stationary corrective feedback");
    Check(std::fabs(AxisOf(aim,0)[3])<1e-8f,"reconciliation never changes native motor rate");
    // Same frame: physical tracking lag + genuine parent yaw + new player input. The prior parent basis
    // must remain untouched; only the actuator's local displacement is reconciled.
    e=Setup();testFrame=2;
    const auto previous=e->hold.seen;
    const float tracking=-0.3f*deg,hullTurn=0.2f*deg;
    Hull(hullTurn);patchedReadback(aim,0,0,tracking);
    Check(std::memcmp(previous.r,e->hold.seen.r,sizeof previous.r)==0,"readback does not swallow new parent rotation");
    float held[2],delta[2],basis[9];Check(StabHeld(aim,held,delta,basis),"camera sees reconciled reference before choosing input");
    Check(std::fabs(stab::Wrap(held[0]-(tracking-hullTurn)))<0.0001f*deg,"reference retains hull yaw in current basis");
    StabFrame(vehicle);alignas(16) const float input[4]={0.4f,0.2f,0,0};
    StabStep(aim,input,reinterpret_cast<AimStepFn>(image+kPlainAimStep));
    const float own=0.4f*0.1f*0.5f*deg;
    const float correction=-hullTurn*stab::Gain(kClasses[0].main);
    Check(std::fabs(e->hold.shift[0]-correction)<0.0001f*deg,"combined case compensates hull motion only, not native tracking lag");
    Check(std::fabs(AxisOf(aim,0)[2]-(tracking+own+correction))<0.0001f*deg,"combined case preserves player command plus hull stabilization");
    Check(std::fabs(AxisOf(aim,0)[3]-own)<1e-7f,"combined case preserves original native command rate");
    const float combined=AxisOf(aim,0)[2];
    e=Setup();testFrame=2;patchedReadback(aim,0,0,tracking);Hull(hullTurn);StabFrame(vehicle);
    StabStep(aim,input,reinterpret_cast<AimStepFn>(image+kPlainAimStep));
    Check(std::fabs(AxisOf(aim,0)[2]-combined)<1e-7f,"readback on either side of hull matrix refresh uses the same previous basis");
    // A repeated readback oscillation may come from the native physical motor. It must not be amplified
    // into another stabilizer correction, including when the native readback arrives more than once.
    e=Setup();float worst=0;
    for(int f=2;f<182;++f) {
        testFrame=static_cast<ULONGLONG>(f);
        const float physical=0.5f*deg*std::sin(float(f)*0.7f);
        patchedReadback(aim,0,0,physical);patchedReadback(aim,0,0,physical);Step();
        worst=std::fmax(worst,std::fabs(e->hold.shift[0]));
    }
    Check(worst<0.001f*deg,"180 real native frames do not feed physical readback jitter into a second servo");
    std::printf("stab_native_readback_test: %d checks passed; old stationary shift %.3f deg, new worst %.6f deg\n",checks,legacy/deg,worst/deg);
    std::puts("No live Havok world/game; native angle readback, axis step and bone mapping execute unchanged.");
}
