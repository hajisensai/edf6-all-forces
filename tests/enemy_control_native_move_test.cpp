#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include "../src/enemy_control_native_move.cpp"

namespace {
using namespace crew::enemy_native;
int checks=0,failures=0;
void Check(bool value,const char* why) { ++checks;if(!value){++failures;std::printf("FAIL %s\n",why);} }
bool Close(float a,float b) { return std::fabs(a-b)<0.0001f; }
struct Scene {
    alignas(16) unsigned char object[0x2000]{};
    alignas(16) unsigned char control[16]{};
    NativeProfile profile{"fixture",0,0x343DD0,0,1,79,0x31,Kind::walking,7,4};
    NativeLease lease{};
    Scene() {
        void* pointer=control;std::memcpy(object+0x30,&pointer,8);
        LONG count=1;std::memcpy(control+8,&count,4);
        lease.object=object;lease.control=control;lease.profile=&profile;Yaw(0.0f);
    }
    void Yaw(float angle) {
        float matrix[16]={std::cos(angle),0,-std::sin(angle),0,0,1,0,0,std::sin(angle),0,std::cos(angle),0,0,0,0,1};
        std::memcpy(object+0x60,matrix,sizeof(matrix));
        constexpr std::size_t angles[]={0x1444,0x10E4,0xF44,0x1754,0x10C4,0x1004,0xFC4,0x12D4,0x5F4,0x1504,0x1084};
        for(auto offset:angles)std::memcpy(object+offset,&angle,4);
        const float q[4]={0,std::sin(angle*0.5f),0,std::cos(angle*0.5f)};
        for(auto offset:{0xE30,0xE10,0xFB0})std::memcpy(object+offset,q,sizeof(q));
    }
};
}
int main() {
    using namespace crew::enemy_native;
    constexpr float pi=3.14159265358979323846f;
    Scene scene;
    for(const auto rva:{0x343DD0u,0x4A8FD0u,0x3EE300u,0x31ED90u,0x4E0010u,0x4F9540u,0x4A6780u}) {
        scene.profile.inputRva=rva;
        const float sign=rva==0x31ED90u || rva==0x4E0010u ? 1.0f : -1.0f;
        for(float body:{0.0f,pi*0.5f,-pi*0.5f,pi}) {
            scene.Yaw(body);
            for(float camera:{0.0f,pi*0.5f,-pi*0.5f,pi}) {
                const float move[3]={0,0,1};Input out{};out.yaw=0.2f;out.pitch=-0.3f;out.held=7;out.pressed=2;out.seed=42;
                Check(WorldMoveToPad(scene.lease,move,camera,&out),"camera-relative movement admitted");
                const float basis=rva==0x31ED90u ? 0.0f : body;
                Check(Close(out.right,sign*std::sin(camera-basis)) && Close(out.forward,sign*std::cos(camera-basis)),"native local axes invert body basis and raw signs");
                Check(Close(out.yaw,0.2f) && Close(out.pitch,-0.3f) && out.held==7 && out.pressed==2 && out.seed==42,"movement preserves aim and action identity");
            }
        }
    }
    scene.profile.inputRva=0x3B3ED0;scene.profile.axes=15;scene.Yaw(0);
    const float up[3]={0,1,0};Input out{};
    Check(WorldMoveToPad(scene.lease,up,0,&out) && Close(out.vertical,1),"FlyingDyno positive semantic up maps native CC");
    scene.profile.inputRva=0x3D60F0;
    Check(WorldMoveToPad(scene.lease,up,0,&out) && Close(out.vertical,-1),"Spinner positive up maps native E4");
    scene.profile.inputRva=0x3EE300;
    Check(WorldMoveToPad(scene.lease,up,0,&out) && Close(out.vertical,-1),"TimeShip positive up maps native E4");
    scene.profile.inputRva=0x52EF70;
    Check(WorldMoveToPad(scene.lease,up,0,&out) && Close(out.vertical,1),"UFO positive up maps native CC");
    // Rendering may lag the controller target. The native movement basis must
    // remain the exact yaw/quaternion consumed by this class, never the model.
    scene.profile.inputRva=0x4A8FD0;scene.profile.axes=7;scene.Yaw(0);
    const float lagged[16]={0,0,-1,0,0,1,0,0,1,0,0,0,0,0,0,1};
    std::memcpy(scene.object+0x60,lagged,sizeof(lagged));
    const float ahead[3]={0,0,1};
    Check(WorldMoveToPad(scene.lease,ahead,0,&out) && Close(out.forward,-1) && Close(out.right,0),"Insect movement reads E30 target instead of lagging render matrix");
    scene.profile.inputRva=0x52EF70;
    Check(WorldMoveToPad(scene.lease,ahead,0,&out) && Close(out.right,1) && Close(out.forward,0),"UFO movement uses the same actor rows its native consumer reads");
    scene.profile.inputRva=0x4A8FD0;
    const float wall[4]={-std::sin(pi*0.25f),0,0,std::cos(pi*0.25f)};
    std::memcpy(scene.object+0xE30,wall,sizeof(wall));
    Check(WorldMoveToPad(scene.lease,ahead,0,&out) && Close(out.forward,-1) && Close(out.right,0),"wall-normal camera keeps native climb tangent forward");
    scene.profile.axes=0;out.right=1;out.forward=1;out.vertical=1;
    Check(WorldMoveToPad(scene.lease,up,0,&out) && out.right==0 && out.forward==0 && out.vertical==0,"non-mobile native class does not gain fake flight");
    scene.profile.axes=7;scene.Yaw(0);
    const float bad[3]={std::numeric_limits<float>::quiet_NaN(),0,0};
    out.right=0.25f;Check(!WorldMoveToPad(scene.lease,bad,0,&out) && out.right==0.25f,"nonfinite input rejected without mutation");
    scene.object[0x2E8]=1;Check(!WorldMoveToPad(scene.lease,up,0,&out),"dead controller rejected");scene.object[0x2E8]=0;
    scene.object[0x18]=4;Check(!WorldMoveToPad(scene.lease,up,0,&out),"retiring controller rejected");scene.object[0x18]=0;
    scene.profile.inputRva=0x52EF70;std::memset(scene.object+0x60,0,64);Check(!WorldMoveToPad(scene.lease,up,0,&out),"degenerate native basis rejected");
    std::printf("%d enemy movement checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
