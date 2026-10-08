// Real launcher.cpp, native-layout riders/holders/muzzles, real WorldGravity and RoundImpact.
// Boundary substitutes are the current human/store, camera/map rays and launcher-pose publication only.
// Build as an executable linked to edf6common (include common); no game process or installed assets are used.
#include "../src/launcher.cpp"
#include <cstdio>
#include <cstring>
#include <vector>

namespace crew {
unsigned char* image=nullptr;
Config config{};
unsigned char* currentHuman=nullptr;
const void* selectionVehicle=nullptr;
unsigned char* selectedWeapon=nullptr;
bool highMode=false,highTransition=false;
int cameraCalls=0,loftCalls=0;
const void* loftVehicle=nullptr;
bool loftEnabled=false;
const Config& Cfg() noexcept { return config; }
ULONGLONG GameMs() noexcept { return 1000; } // cleanup assertions never rely on freshness expiry
unsigned char* PlayerHuman() noexcept { return currentHuman; }
unsigned char* PayloadSightPicked(const void* vehicle,unsigned seat) noexcept {
    return vehicle==selectionVehicle && seat==0 ? selectedWeapon : nullptr;
}
void Log(const char*,...) noexcept {}
bool HighCamOn(const void*) noexcept { return highMode; }
bool TurretCamHighTransition(const void*) noexcept { return highTransition; }
bool CameraRay(float* eye,float* dir) noexcept {
    ++cameraCalls;eye[0]=0;eye[1]=30;eye[2]=0;
    dir[0]=0;dir[1]=-0.6f;dir[2]=0.8f;return true;
}
float MapRay(const float* from,const float* to,float* hit) noexcept {
    if(from[1]<0 || to[1]>0 || to[1]==from[1])return -1;
    const float share=-from[1]/(to[1]-from[1]);
    float length2=0;
    for(int i=0;i<3;++i){hit[i]=from[i]+share*(to[i]-from[i]);const float d=hit[i]-from[i];length2+=d*d;}
    return std::sqrt(length2);
}
void SetLauncherLoft(const void* vehicle,bool aim,float) noexcept {
    ++loftCalls;loftVehicle=vehicle;loftEnabled=aim;
}
bool LauncherLoft(const void*,LoftReadout*) noexcept { return false; }

namespace {
int checks=0,failed=0;
void Check(bool ok,const char* description) {
    ++checks;if(!ok){++failed;std::printf("FAIL %s\n",description);}
}
void Identity(void* dst,float x=0,float y=0,float z=0) {
    const float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1};std::memcpy(dst,m,sizeof(m));
}
const float* __fastcall Gravity(void*) { static const float gravity[4]={0,-9.8f,0,0};return gravity; }
struct WeaponFixture {
    unsigned char weapon[0xF00]{},muzzle[edf::kMuzzleStride]{},bone[0x100]{};
    explicit WeaponFixture(float x,bool lofted=true) {
        Put<void*>(weapon,edf::kMuzzles,muzzle);Put<std::uint64_t>(weapon,edf::kMuzzleCount,1);
        Put<void*>(muzzle,0,bone);Put<int>(muzzle,edf::kMuzzleMode,1);
        Identity(weapon+edf::kWeaponMatrix);Identity(muzzle+edf::kMuzzleLocal);
        Identity(bone+edf::kBoneRows,x,8,10);
        Put<int>(weapon,edf::kWeaponMark,lofted ? edf::kMarkLofted : edf::kMarkGround);
        Put<float>(weapon,edf::kWeaponAmmoSpeed,2);Put<float>(weapon,edf::kWeaponAmmoGravity,1);
        Put<int>(weapon,edf::kWeaponAmmoAlive,1500);Put<float>(weapon,edf::kWeaponAccuracy,0.02f);
        Put<float>(weapon,edf::kWeaponAccuracyScale,1);
    }
};
struct VehicleFixture {
    unsigned char vehicle[0x800]{},seat[edf::kSeatStride]{},human[0x500]{};
    unsigned char vehicleControl[16]{},riderControl[16]{},holder[16][0x20]{},holderControl[16][16]{};
    unsigned char* holders[16]{};
    explicit VehicleFixture(bool humanRider=true) {
        Identity(vehicle+kMatrix);Put<void*>(vehicle,kSelfCtrl,vehicleControl);Put<int>(vehicleControl,8,1);
        Put<void*>(vehicle,kSeats,seat);Put<std::uint64_t>(vehicle,kSeatCount,1);
        Put<void*>(seat,kSeatRider,human);Put<void*>(seat,kSeatRiderCtrl,riderControl);Put<int>(riderControl,8,1);
        Put<void*>(human,kHumanPad,humanRider ? human : nullptr);human[kHumanPlayer]=humanRider ? 1 : 0;
        Put<void*>(seat,kSeatWeapons,holders);
        Put<float>(seat,kSeatAim+kAimAxes+kAxisStride,-1.45f);
        Put<float>(seat,kSeatAim+kAimAxes+kAxisStride+4,0.1f);
        for(int i=0;i<16;++i) {
            holders[i]=holder[i];Put<void*>(holder[i],kHolderCtrl,holderControl[i]);Put<int>(holderControl[i],8,1);
        }
    }
    void Arm(int index,WeaponFixture& weapon) { Put<void*>(holder[index],kHolderWeapon,weapon.weapon); }
    void Count(std::uint64_t count) { Put<std::uint64_t>(seat,kSeatWeaponCount,count); }
};
bool PreviewAt(float x) {
    LauncherReadout r{};
    return PlayerLauncher(&r) && r.reach && std::fabs(r.impact[0]-x)<0.001f &&
           std::fabs(r.impact[1])<0.001f && r.range>10 && r.flight>0 && r.rings>0;
}
bool PreviewAbsent() { LauncherReadout r{};return !PlayerLauncher(&r); }
void Run() {
    config.debug=false;
    std::vector<unsigned char> module(0x20B2960);
    image=module.data();unsigned char world[0x80]{},physics[0x30]{};
    void* table[]={reinterpret_cast<void*>(&Gravity)};
    Put<void*>(image,0x20B2958,world);Put<void*>(world,0x68,physics);Put<void*>(physics,0x20,table);
    VehicleFixture owner,secondPlayer,npc(false);
    WeaponFixture first(-4),second(7),normal(30,false),foreign(70);
    currentHuman=owner.human;selectionVehicle=owner.vehicle;selectedWeapon=second.weapon;
    for(int i=0;i<8;++i)owner.Arm(i,normal);
    owner.Arm(8,second);owner.Count(9);
    ResetLauncher();LauncherFrame(owner.vehicle);
    Check(PreviewAt(7),"nine live holders still publish the selected launcher's real impact and spread");

    owner.Arm(0,first);owner.Arm(1,second);owner.Arm(2,normal);owner.Count(3);
    ResetLauncher();LauncherFrame(owner.vehicle);
    Check(PreviewAt(7),"multiple lofted weapons select PayloadSightPicked, not the first marked holder");
    selectedWeapon=first.weapon;LauncherFrame(owner.vehicle);
    Check(PreviewAt(-4),"switching active lofted weapon immediately moves the preview to its real muzzle");

    secondPlayer.Arm(0,foreign);secondPlayer.Count(1);npc.Arm(0,foreign);npc.Count(1);
    // Another local pad/player and an ordinary NPC both receive native InputHook callbacks.
    const int loftBefore=loftCalls,cameraBefore=cameraCalls;
    LauncherFrame(secondPlayer.vehicle);LauncherFrame(npc.vehicle);
    Check(PreviewAt(-4),"foreign local-player and NPC callbacks neither clear nor replace the current preview");
    Check(loftCalls==loftBefore && cameraCalls==cameraBefore && loftVehicle==owner.vehicle,
          "foreign callbacks never aim their launchers through the current player's camera");

    highMode=true;const int highReads=cameraCalls;LauncherFrame(owner.vehicle);
    Check(PreviewAt(-4) && !loftEnabled && cameraCalls==highReads,
          "high mode preserves physical preview while suppressing camera-driven loft");
    highMode=false;highTransition=true;LauncherFrame(owner.vehicle);
    Check(PreviewAt(-4) && !loftEnabled && cameraCalls==highReads,
          "return transition also preserves preview without feeding the camera back into the launcher");
    highTransition=false;

    selectedWeapon=nullptr;LauncherFrame(owner.vehicle);
    Check(PreviewAbsent(),"no selected fire-control weapon clears this owner's preview immediately");
    selectedWeapon=first.weapon;LauncherFrame(owner.vehicle);Check(PreviewAt(-4),"owner preview can recover after no selection");
    selectedWeapon=normal.weapon;LauncherFrame(owner.vehicle);
    Check(PreviewAbsent(),"switching to a non-lofted weapon immediately clears the old rocket preview");
    selectedWeapon=foreign.weapon;LauncherFrame(owner.vehicle);
    Check(PreviewAbsent(),"a selected weapon outside the current seat cannot fall back to another lofted weapon");

    selectedWeapon=second.weapon;LauncherFrame(owner.vehicle);Check(PreviewAt(7),"selected second launcher is live before holder expiry");
    Put<int>(owner.holderControl[1],8,0);LauncherFrame(owner.vehicle);
    Check(PreviewAbsent(),"expired selected holder clears the preview instead of choosing the first launcher");
    Put<int>(owner.holderControl[1],8,1);selectedWeapon=first.weapon;LauncherFrame(owner.vehicle);
    Check(PreviewAt(-4),"owner preview is live before the player leaves");
    Put<void*>(owner.seat,kSeatRider,nullptr);LauncherFrame(owner.vehicle);
    Check(PreviewAbsent(),"leaving the owner seat clears its preview at the same GameMs");
    const int afterLeave=loftCalls;LauncherFrame(secondPlayer.vehicle);LauncherFrame(npc.vehicle);
    Check(PreviewAbsent() && loftCalls==afterLeave,"foreign callbacks cannot republish or drive a launcher after owner departure");

    Put<void*>(owner.seat,kSeatRider,owner.human);LauncherFrame(owner.vehicle);
    Check(PreviewAt(-4),"reboarding restores the owner's preview");
    owner.vehicle[kDead]=1;LauncherFrame(owner.vehicle);
    Check(PreviewAbsent(),"wrecking the owner vehicle clears its preview immediately");
    image=nullptr;
}
}  // namespace
}  // namespace crew
int main() {
    crew::Run();std::printf("launcher_owner: %d checks, %d failed\n",crew::checks,crew::failed);
    return crew::failed ? 1 : 0;
}
