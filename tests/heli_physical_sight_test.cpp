// Production 506 gun sight with separate left/right mode-1 native muzzle records.
// Link launcher.cpp, rounds.cpp, edf6common and user32; /Gy /OPT:REF discard unrelated frame hooks.
#include "../src/helisight.cpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace crew {
unsigned char* image=nullptr;
Config config{};
const Config& Cfg() noexcept { return config; }
ULONGLONG GameMs() noexcept { return 1000; }
void Log(const char*,...) noexcept {}
// Linked production frame hooks are outside SolveGun's contract. Any accidental use fails the fixture.
void UnexpectedSightBoundary() noexcept { static volatile bool fail=true;if(fail)std::abort(); }
PluginBody BodyOf(const void*) noexcept { UnexpectedSightBoundary();return PluginBody::none; }
int HiddenAimGuns(const unsigned char*,const unsigned char**,int) noexcept { UnexpectedSightBoundary();return 0; }
bool IsHelicopter(const void*) noexcept { UnexpectedSightBoundary();return false; }
unsigned char* PayloadPicked(const void*) noexcept { UnexpectedSightBoundary();return nullptr; }
unsigned char* PlayerHuman() noexcept { UnexpectedSightBoundary();return nullptr; }
unsigned char* PayloadSightPicked(const void*,unsigned) noexcept { UnexpectedSightBoundary();return nullptr; }
int WeaponLock(const unsigned char*,float*,float*) noexcept { UnexpectedSightBoundary();return 0; }
bool CameraRay(float*,float*) noexcept { UnexpectedSightBoundary();return false; }
bool HighCamOn(const void*) noexcept { UnexpectedSightBoundary();return false; }
bool TurretCamHighTransition(const void*) noexcept { UnexpectedSightBoundary();return false; }
void SetLauncherLoft(const void*,bool,float) noexcept { UnexpectedSightBoundary(); }
bool LauncherLoft(const void*,LoftReadout*) noexcept { UnexpectedSightBoundary();return false; }
bool terrain=true;
int leftRays=0,rightRays=0,centerRays=0;
// A floor only under the left barrel. Averaging the muzzles would query its missing center instead.
float MapRay(const float* from,const float* to,float* hit) noexcept {
    if(from[0]<-1.0f)++leftRays;
    else if(from[0]>1.0f)++rightRays;
    else ++centerRays;
    if(!terrain || from[1]<0 || to[1]>0 || from[1]==to[1])return -1.0f;
    const float t=-from[1]/(to[1]-from[1]);
    const float x=from[0]+t*(to[0]-from[0]);
    if(x>=0)return -1.0f;
    for(int i=0;i<3;++i)hit[i]=from[i]+t*(to[i]-from[i]);
    return vec::Dist(from,hit);
}
namespace {
int checks=0,failures=0;
void Check(bool value,const char* name) {
    ++checks;if(!value){++failures;std::printf("FAIL: %s\n",name);}
}
bool Near(float a,float b,float tolerance=0.001f) { return std::fabs(a-b)<tolerance; }
void Matrix(void* to,float x=0,float y=0,float z=0) {
    const float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1};std::memcpy(to,m,sizeof(m));
}
const float* __fastcall Gravity(void*) { static float g[4]={0,-9.8f,0,0};return g; }
struct Weapon {
    unsigned char data[0x1600]{},muzzle[edf::kMuzzleStride]{},bone[0x100]{};
    Weapon(float boneX,float yaw) {
        Put<const void*>(data,edf::kMuzzles,muzzle);Put<std::uint64_t>(data,edf::kMuzzleCount,1);
        Put<const void*>(muzzle,0,bone);Put<int>(muzzle,edf::kMuzzleMode,1);
        Matrix(bone+edf::kBoneRows,boneX,10,20);Matrix(muzzle+edf::kMuzzleLocal,2,1,3);
        Put<float>(muzzle,edf::kMuzzleLocal,std::cos(yaw));
        Put<float>(muzzle,edf::kMuzzleLocal+8,-std::sin(yaw));
        Put<float>(muzzle,edf::kMuzzleLocal+0x20,std::sin(yaw));
        Put<float>(muzzle,edf::kMuzzleLocal+0x28,std::cos(yaw));
        // Deliberately unrelated weapon rows: mode 1 must use local x bone, not these rows.
        Matrix(data+edf::kWeaponMatrix,900,800,700);
        Put<float>(data,edf::kWeaponMatrix+0x20,1);Put<float>(data,edf::kWeaponMatrix+0x28,0);
        Put<float>(data,edf::kWeaponAmmoSpeed,3);Put<float>(data,edf::kWeaponAmmoGravity,1);
        Put<int>(data,edf::kWeaponAmmoAlive,120);
    }
};
void Run() {
    std::vector<unsigned char> module(0x20B2960);
    image=module.data();unsigned char world[0x80]{},physics[0x30]{};
    void* gravityVtable[]={reinterpret_cast<void*>(&Gravity)};
    Put<void*>(image,0x20B2958,world);Put<void*>(world,0x68,physics);Put<void*>(physics,0x20,gravityVtable);
    Weapon left(-8,0),right(4,0.2f);
    const unsigned char* guns[]={left.data,right.data};
    HeliSightReadout r{};
    Check(SolveGun(guns,2,r) && r.gun && r.paths==2,"506 left and right mode-1 guns each yield a real physical path");
    Check(leftRays>0 && rightRays>0 && centerRays==0,"terrain is queried separately from both muzzles, never their mean");
    Check(r.path[0].hit && !r.path[1].hit,"only the left barrel intersects terrain; right retains a no-hit path");
    Check(Near(r.path[0].at[0],-6) && Near(r.path[0].at[1],0) && r.path[0].at[2]>200,"left impact starts at local x bone position");
    const float endY=11.0f-9.8f/3600.0f*(120.0f*121.0f/2.0f);
    Check(Near(r.path[1].at[0],6+360*std::sin(0.2f)) && Near(r.path[1].at[2],23+360*std::cos(0.2f)),
          "right endpoint follows its own mode-1 local direction instead of weapon rows or averaged bearing");
    Check(Near(r.path[1].at[1],endY) && Near(r.path[1].seconds,2),"no-hit endpoint uses world gravity and actual 120-frame lifetime");
    Check(r.hit && vec::Dist(r.pipper,r.path[0].at)<0.001f && Near(r.range,r.path[0].range),"legacy pipper remains first real path, not mean impact");
    Check(Near(r.bore[0],0) && Near(r.bore[2],1),"single boresight owner remains first real muzzle");
    Check(r.path[0].seconds>0 && r.path[0].seconds<2 && r.path[0].range>200,"hit path retains its own flight time and distance");

    terrain=false;r=HeliSightReadout{};
    Check(SolveGun(guns,2,r) && !r.path[0].hit && !r.path[1].hit,"missing terrain keeps both independent airborne endpoints");
    Check(Near(r.path[0].at[0],-6) && Near(r.path[0].at[1],endY) && Near(r.path[0].at[2],383),"left lifetime endpoint matches discrete ballistic step");
    const HeliSightReadout stationary=r;
    Put<float>(left.data,edf::kWeaponAmmoOwnerMove,0.5f);Put<float>(left.data,edf::kWeaponOwnerVel,24);
    Put<float>(right.data,edf::kWeaponAmmoOwnerMove,1);Put<float>(right.data,edf::kWeaponOwnerVel,-12);
    r=HeliSightReadout{};
    Check(SolveGun(guns,2,r),"inherited motion preserves valid per-barrel prediction");
    Check(Near(r.path[0].at[0]-stationary.path[0].at[0],24) && Near(r.path[1].at[0]-stationary.path[1].at[0],-24),
          "each gun inherits its own owner velocity times AmmoOwnerMove divided by 60");
    Check(Near(r.path[0].at[1],endY) && Near(r.path[1].at[1],endY) && Near(r.path[0].seconds,2),"inherited horizontal motion preserves gravity and lifetime");
    Check(!left.data[0x139] && !right.data[0x139],"sight prediction never fires either weapon");

    const unsigned char* single[]={left.data};
    Put<const void*>(left.muzzle,0,nullptr);r=HeliSightReadout{};
    Check(!SolveGun(single,1,r) && !r.gun && !r.paths,"unknown muzzle bone fails without fabricating a weapon-row shot");
    Put<const void*>(left.muzzle,0,left.bone);Put<std::uint64_t>(left.data,edf::kMuzzleCount,0);r=HeliSightReadout{};
    Check(!SolveGun(single,1,r) && !r.paths,"missing muzzle list has no invented centerline path");
    Put<std::uint64_t>(left.data,edf::kMuzzleCount,1);Put<float>(left.muzzle,edf::kMuzzleLocal+0x28,0);r=HeliSightReadout{};
    Check(!SolveGun(single,1,r) && !r.paths,"degenerate real muzzle direction fails safely");
    image=nullptr;
}
} // namespace
} // namespace crew
int main() {
    crew::Run();std::printf("heli physical sight: %d checks, %d failures\n",crew::checks,crew::failures);
    return crew::failures ? 1 : 0;
}
