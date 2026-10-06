// Compare the read-only rocket predictor against the production missile.cpp Guide, followed by the separately
// tested native MissileBullet01 motor/core step. No game process. Covers inherited velocity, ignition and burnout.
#include "../src/crew.h"
#include "../src/rounds.h"
#define rounds trackedRounds
#include "../src/missile.cpp"
#undef rounds
#include <cstdio>
#include <initializer_list>
namespace crew {
unsigned char* image=nullptr;
namespace {Config config{};ULONGLONG tick=1;}
const Config& Cfg() noexcept{return config;}
ULONGLONG GameFrame() noexcept{return tick;}
ULONGLONG GameMs() noexcept{return tick*16+3600000;}
void Log(const char*,...) noexcept{}
bool TvSteer(unsigned char*) noexcept{return false;}
void ResetTv() noexcept{}
}
int main(){
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)return 2;
    config.debug=false;
    int samples=0;float worst=0;
    for(int ignite:{0,12})for(float burn:{0.0f,66.0f,180.0f})for(float inherited:{0.0f,3.0f,-1.0f}) {
        unsigned char bullet[0x1500]{};
        Put<const void*>(bullet,0,image+kVtable);Put<int>(bullet,kHomingDelay,kNoStockHoming);Put<int>(bullet,kHomingFrames,kPluginMark);
        Put<int>(bullet,kIgnition,ignite);Put<float>(bullet,kGuidance,burn);Put<float>(bullet,kGuidance+4,1);Put<float>(bullet,kGuidance+8,2);
        Put<float>(bullet,kAccel,650.0f/3600.0f);
        rounds::Motor native{};native.own[2]=0.6f;native.inh[0]=inherited;native.inh[1]=-0.1f;
        native.accel=650.0f/3600.0f;native.top=740.0f/60.0f;native.ignite=ignite;native.keepInh=0.97f;native.keepOwn=1;
        native.drop[1]=-14.7f/3600.0f;
        rounds::PluginMotor predicted{native,burn};float actualPos[3]={0,80,0},predictedPos[3]={0,80,0};
        for(int age=0;age<600;++age) {
            ++tick;Put<int>(bullet,kAge,age);Put<int>(bullet,kFlown,age);
            std::memcpy(bullet+kOwn,native.own,12);std::memcpy(bullet+kInherited,native.inh,12);
            Guide(bullet); // the actual production pre-update, not another copy of its coast formula
            std::memcpy(native.own,bullet+kOwn,12);std::memcpy(native.inh,bullet+kInherited,12);
            rounds::Step(native,actualPos);rounds::Step(predicted,predictedPos);
            for(int k=0;k<3;++k) {
                const float error=std::fabs(actualPos[k]-predictedPos[k]);worst=std::fmax(worst,error);
                if(error>0.002f){std::printf("FAIL ignition %d burn %.0f inherited %.1f frame %d error %.6f\n",ignite,burn,inherited,age,error);return 1;}
            }
            ++samples;
        }
    }
    std::printf("rocket_guide_prediction_test: %d Guide/native steps matched; worst position difference %.6f m\n",samples,worst);
    VirtualFree(image,0,MEM_RELEASE);
}
