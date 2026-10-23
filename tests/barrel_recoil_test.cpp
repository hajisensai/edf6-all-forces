// src/barrel_recoil.cpp as built (the production source), with the game's readers replaced: each BMPT barrel kicks
// its own slide on its own shot, in turn, from the bone its muzzle hangs on; a copy's received shots too; no kick that
// adds up over frames the CAS leaves alone; the dead, the disabled and vehicles without twin barrels untouched.
#include "../src/barrel_recoil.cpp"
#include <cstdio>
#include <cstdlib>

namespace fixture {
crew::Config config{};ULONGLONG now=1000,frame=1;
unsigned char vehicle[0x3000]{},holders[4*0x48]{},weapon[4][0x1600]{},muzzles[4][0xF0]{},bones[2][0x110]{};
unsigned pairs[4]={1,1,2,2};
int fails=0,checks=0;
void Check(bool ok,const char* what){++checks;if(!ok){++fails;std::printf("FAIL: %s\n",what);}}
// The slides' locals as the CAS leaves them each frame: the bind (barrel along +z, at the slide's pivot).
const float kBind[2][16]={{1,0,0,0,0,1,0,0,0,0,1,0,.195785f,0,1.444801f,1},{1,0,0,0,0,1,0,0,0,0,1,0,-.195785f,0,1.444801f,1}};
void Cas(){for(int s=0;s<2;++s)std::memcpy(bones[s]+0x70,kBind[s],64);}
float Z(int side){return edf::At<float>(bones[side],0x70+14*4);}
void Reset(){
    crew::ResetBarrelRecoil();std::memset(vehicle,0,sizeof vehicle);config.enabled=true;now=1000;frame=1;
    edf::Put<void*>(vehicle,crew::kHolders,holders);edf::Put<std::uint64_t>(vehicle,crew::kHolderCount,4);
    edf::Put<void*>(vehicle,crew::kSelfCtrl,vehicle+0x2F00);edf::Put<int>(vehicle+0x2F00,8,1);
    for(int i=0;i<4;++i) {
        edf::Put<void*>(holders+i*0x48,crew::kHolderWeapon,weapon[i]);
        std::memset(weapon[i],0,sizeof weapon[i]);edf::Put<int>(weapon[i],crew::kWeaponAmmo,425);
        // HE L / HE R / APDS L / APDS R: each its side's slide (vehicle_weapon_setting), one muzzle.
        edf::Put<void*>(weapon[i],edf::kMuzzles,muzzles[i]);edf::Put<std::uint64_t>(weapon[i],edf::kMuzzleCount,1);
        edf::Put<void*>(muzzles[i],0,bones[i%2]);
    }
    for(int s=0;s<2;++s){std::memset(bones[s],0,sizeof bones[s]);}
    Cas();
}
void Frame(){++frame;now+=16;Cas();crew::BarrelRecoilPose(vehicle);}
}  // namespace fixture
namespace crew {
const Config& Cfg() noexcept{return fixture::config;}
ULONGLONG GameMs() noexcept{return fixture::now;}
ULONGLONG GameFrame() noexcept{return fixture::frame;}
unsigned TwinPairOf(const unsigned char* w) noexcept{
    for(int i=0;i<4;++i)if(w==fixture::weapon[i])return fixture::pairs[i];
    return 0;
}
}  // namespace crew

int main(){
    using namespace fixture;
    Reset();Frame();
    Check(Z(0)==kBind[0][14] && Z(1)==kBind[1][14],"no shot: the slides as the CAS left them");
    edf::Put<int>(weapon[0],crew::kWeaponAmmo,424);Frame();
    Check(std::fabs(Z(0)-(kBind[0][14]-crew::recoil::kTravel))<1e-6f && Z(1)==kBind[1][14],
          "the left barrel's shot kicks the left slide back its full travel, the right one not");
    Check(edf::At<float>(bones[0],0x70+12*4)==kBind[0][12] && edf::At<int>(bones[0],8)==1,
          "back along the barrel only, the local owning the bone this frame");
    const float first=Z(0);crew::BarrelRecoilPose(vehicle);
    Check(Z(0)==first,"a second pose in one frame kicks nothing again");
    Frame();
    Check(Z(0)>first && Z(0)<kBind[0][14],"the next frame: on its way out again");
    // A frame the CAS does not rewrite the local: the kick is taken from the base, never added to itself.
    ++frame;now+=16;crew::BarrelRecoilPose(vehicle);
    const float held=Z(0);++frame;now+=16;crew::BarrelRecoilPose(vehicle);
    Check(Z(0)>held && Z(0)<=kBind[0][14],"no rewrite by the CAS: the kick still runs out, it does not add up");
    for(int i=0;i<crew::recoil::kReturnFrames;++i)Frame();
    Check(Z(0)==kBind[0][14],"back in battery after kReturnFrames");
    ++frame;now+=16;edf::Put<int>(weapon[1],crew::kWeaponAmmo,424);crew::BarrelRecoilPose(vehicle);
    ++frame;now+=16;crew::BarrelRecoilPose(vehicle);for(int i=0;i<crew::recoil::kReturnFrames+1;++i){++frame;now+=16;crew::BarrelRecoilPose(vehicle);}
    Check(Z(1)==kBind[1][14],"no CAS rewrite at all: the slide is given its base back at rest");
    // Turn about: right, then left, each its own slide.
    Frame();edf::Put<int>(weapon[1],crew::kWeaponAmmo,423);Frame();
    Check(Z(1)<kBind[1][14] && Z(0)==kBind[0][14],"the right barrel's shot: the right slide");
    // The APDS belt rides the same slides (its muzzle's bone): its shot kicks that side.
    for(int i=0;i<8;++i)Frame();
    edf::Put<int>(weapon[2],crew::kWeaponAmmo,424);Frame();
    Check(Z(0)<kBind[0][14] && Z(1)==kBind[1][14],"an APDS round from the left: the left slide, found by its muzzle's bone");
    // A copy: the shooter's machine fired it; the shots heard (+0x1544) above those played (+0xBD0).
    for(int i=0;i<8;++i)Frame();
    edf::Put<int>(weapon[1],0x1544,1);Frame();
    Check(Z(1)<kBind[1][14],"a copy: a shot received from the operator's machine kicks too");
    for(int i=0;i<8;++i)Frame();
    edf::Put<int>(weapon[1],crew::kWeaponAmmo,425);Frame();
    Check(Z(1)==kBind[1][14],"a reload (rounds up) is no shot");
    // The dead, the disabled, a vehicle without twin barrels: the CAS's locals untouched.
    edf::Put<int>(weapon[0],crew::kWeaponAmmo,100);config.enabled=false;Frame();
    Check(Z(0)==kBind[0][14],"the plugin off: no kick");config.enabled=true;
    vehicle[edf::kDead]=1;edf::Put<int>(weapon[0],crew::kWeaponAmmo,99);Frame();
    Check(Z(0)==kBind[0][14],"a wreck: no kick");vehicle[edf::kDead]=0;
    for(auto& p:pairs)p=0;edf::Put<int>(weapon[0],crew::kWeaponAmmo,98);Frame();
    Check(Z(0)==kBind[0][14],"no twin barrels (a Kepler, the M163): never touched");
    // The offsets themselves.
    Check(crew::recoil::Offset(0)==crew::recoil::kTravel && crew::recoil::Offset(crew::recoil::kReturnFrames)==0.0f &&
          crew::recoil::Offset(2)<crew::recoil::Offset(1),"the kick: full at the shot, out again by kReturnFrames");
    std::printf("barrel recoil: %d checks, %d failures\n",checks,fails);
    return fails ? 1 : 0;
}
