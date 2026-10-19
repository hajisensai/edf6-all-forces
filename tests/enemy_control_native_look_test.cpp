// Production absolute-look conversion against synthetic native target fields.
// Native instruction/constant bytes are separately audited against EDF.dll.
#include "../src/enemy_control_native_look.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace crew { unsigned char* image=nullptr; }
namespace {
using namespace crew;
using namespace crew::enemy_native;
int checks=0,failures=0;
void Check(bool value,const char* expression,int line) {
    ++checks;if(!value){++failures;std::fprintf(stderr,"line %d: %s\n",line,expression);}
}
#define CHECK(value) Check((value),#value,__LINE__)
bool Near(double a,double b,double eps=0.00002) {return std::fabs(a-b)<eps;}
constexpr NativeProfile walking{"EDF6_Berserker_Large",0,0x343DD0,0,0,79,0,Kind::walking,7,4};
constexpr NativeProfile humanoid{"Humanoid_Basic",0,0x4A6780,0,0,86,0,Kind::humanoid,7,5};
constexpr NativeProfile other{"GiantAnt",0,0x4A8FD0,0,0,62,0,Kind::insect,7,4};
struct Fixture {
    unsigned char* object=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char* control=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Fixture(){if(!object || !control)std::exit(2);}
    ~Fixture(){VirtualFree(object,0,MEM_RELEASE);VirtualFree(control,0,MEM_RELEASE);}
    NativeLease Reset(const NativeProfile& profile) {
        std::memset(object,0,0x3000);std::memset(control,0,0x1000);
        Put<const void*>(object,0x30,control);Put<LONG>(control,8,1);
        Put<float>(object,0xF9C,0.04f);
        NativeLease lease{};lease.object=object;lease.control=control;lease.profile=&profile;return lease;
    }
};
void BuildImage() {
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)std::exit(2);
    for(const auto& p:kWalkingLookPrints)std::memcpy(image+p.rva,p.bytes,sizeof(p.bytes));
    for(const auto& p:kHumanoidLookPrints)std::memcpy(image+p.rva,p.bytes,sizeof(p.bytes));
}
Input Sentinel() {
    Input p{};p.right=0.21f;p.forward=-0.43f;p.vertical=0.67f;p.yaw=0.12f;p.pitch=-0.34f;
    p.held=0x5555;p.pressed=0xAAAA;p.seed=0x123456789ABCDEF0;return p;
}
void SameOtherInput(const Input& p) {
    CHECK(p.right==0.21f && p.forward==-0.43f && p.vertical==0.67f);
    CHECK(p.held==0x5555 && p.pressed==0xAAAA && p.seed==0x123456789ABCDEF0);
}
void ForwardAndWrap(Fixture& f,const NativeProfile& profile) {
    const auto lease=f.Reset(profile);const bool walk=profile.kind==Kind::walking;
    const auto target=walk ? 0x1450 : 0x10D0;const float step=walk ? 0.04f : 1.0f/30.0f;
    Input out=Sentinel();
    CHECK(AimToPad(lease,0.5f,0.4f,&out));CHECK(out.yaw==-1 && out.pitch==-1);SameOtherInput(out);
    Put<float>(f.object,target,-0.4f);Put<float>(f.object,target+4,0.5f);
    CHECK(AimToPad(lease,0.5f,0.4f,&out));CHECK(out.yaw==0 && out.pitch==0);
    Put<float>(f.object,target,-0.4f+step*0.25f);Put<float>(f.object,target+4,0.5f-step*0.75f);
    CHECK(AimToPad(lease,0.5f,0.4f,&out));CHECK(Near(out.pitch,-0.25));CHECK(Near(out.yaw,-0.75));
    constexpr float pi=3.14159265358979323846f;
    Put<float>(f.object,target+4,pi-0.002f);
    CHECK(AimToPad(lease,-pi+0.002f,0.4f,&out));CHECK(out.yaw<0 && out.yaw>-0.2f);
    Put<float>(f.object,target+4,-pi+0.002f);
    CHECK(AimToPad(lease,pi-0.002f,0.4f,&out));CHECK(out.yaw>0 && out.yaw<0.2f);
    Put<float>(f.object,target+4,4*pi+0.25f);
    CHECK(AimToPad(lease,0.25f,0.4f,&out));CHECK(Near(out.yaw,0,0.0001));
    // Per native frame, not per 30 Hz packet: one held absolute sample converges
    // at the native rate, and a dropped/stationary sample never restarts a turn.
    Put<float>(f.object,target,0);Put<float>(f.object,target+4,0);
    float renderedPitch=0,renderedYaw=0;
    for(unsigned frame=0;frame<120;++frame) {
        CHECK(AimToPad(lease,0.7f,0.35f,&out));
        const float nextPitch=At<float>(f.object,target)+out.pitch*step;
        const float nextYaw=At<float>(f.object,target+4)-out.yaw*step;
        Put<float>(f.object,target,nextPitch);Put<float>(f.object,target+4,nextYaw);
        renderedPitch+=(nextPitch-renderedPitch)*(walk ? 0.3f : 0.5f);
        renderedYaw+=(nextYaw-renderedYaw)*(walk ? 0.3f : 0.5f);
        if(frame>=30)CHECK(std::fabs(out.yaw)<0.0001f && std::fabs(out.pitch)<0.0001f);
    }
    CHECK(Near(At<float>(f.object,target),-0.35));CHECK(Near(At<float>(f.object,target+4),0.7));
    CHECK(Near(renderedPitch,-0.35));CHECK(Near(renderedYaw,0.7));SameOtherInput(out);
    const Input before=Sentinel();out=before;
    CHECK(!AimToPad(lease,0,std::numeric_limits<float>::quiet_NaN(),&out));CHECK(std::memcmp(&out,&before,sizeof(out))==0);
    CHECK(!AimToPad(lease,std::numeric_limits<float>::infinity(),0,&out));CHECK(std::memcmp(&out,&before,sizeof(out))==0);
    Put<float>(f.object,target,std::numeric_limits<float>::quiet_NaN());
    CHECK(!AimToPad(lease,0,0,&out));CHECK(std::memcmp(&out,&before,sizeof(out))==0);
    Put<float>(f.object,target,0);Put<LONG>(f.control,8,0);
    CHECK(!AimToPad(lease,0,0,&out));CHECK(std::memcmp(&out,&before,sizeof(out))==0);
}
void RefusalCases(Fixture& f) {
    auto lease=f.Reset(other);Input out=Sentinel(),before=out;
    CHECK(!AimToPad(lease,0,0,&out));CHECK(std::memcmp(&out,&before,sizeof(out))==0);
    lease=f.Reset(walking);Put<float>(f.object,0xF9C,0);
    CHECK(!AimToPad(lease,0,0,&out));CHECK(std::memcmp(&out,&before,sizeof(out))==0);
    Put<float>(f.object,0xF9C,std::numeric_limits<float>::infinity());CHECK(!AimToPad(lease,0,0,&out));
    Put<float>(f.object,0xF9C,-1);CHECK(!AimToPad(lease,0,0,&out));
    Put<float>(f.object,0xF9C,0.04f);CHECK(!AimToPad(lease,0,0,nullptr));
    image[kWalkingLookPrints[2].rva]^=1;
    CHECK(!AimToPad(lease,0,0,&out));CHECK(std::memcmp(&out,&before,sizeof(out))==0);
    image[kWalkingLookPrints[2].rva]^=1;
    lease=f.Reset(humanoid);image[0x176B050]^=1;
    CHECK(!AimToPad(lease,0,0,&out));CHECK(std::memcmp(&out,&before,sizeof(out))==0);
    image[0x176B050]^=1;
    // The production path only reads native state, including under read-only pages.
    DWORD previous=0;CHECK(VirtualProtect(f.object,0x3000,PAGE_READONLY,&previous)!=0);
    CHECK(AimToPad(lease,0.1f,0.2f,&out));SameOtherInput(out);
    CHECK(VirtualProtect(f.object,0x3000,previous,&previous)!=0);
    lease.object=nullptr;CHECK(!AimToPad(lease,0,0,&out));
}
}
int main() {
    BuildImage();Fixture fixture;ForwardAndWrap(fixture,walking);ForwardAndWrap(fixture,humanoid);RefusalCases(fixture);
    VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    std::printf("%d native absolute-look checks; %d failures\n",checks,failures);return failures ? 1 : 0;
}
