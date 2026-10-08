// Run the actual sightzoom.cpp hook and seat updates with native-layout fixtures.
// The original camera stand-in can leave FOV unwritten, as EDF.dll does on an expired target.
#include "../src/sightzoom.cpp"
#include <cstdio>
namespace crew {
unsigned char* image=nullptr;
namespace {
Config config{};
unsigned char vehicle[0x800]{},otherVehicle[0x800]{},seats[2*kSeatStride]{},human[0x1600]{},otherHuman[0x1600]{};
unsigned char camera[0x500]{},otherCamera[0x500]{},vehicleCtrl[16]{},humanCtrl[16]{},otherCtrl[16]{};
unsigned char* playerHuman=human;
bool mapKeys=false,mapView=false,highView=false,originalWrites=true,sazabiVehicle=false;
bool highActive=false,highReturning=false,heliVehicle=false,aircraft=false,gunship=false,fuel=false,knownRound=true,hud=true;
RoundKind roundKind=RoundKind::arc;
bool lobbed=false;
unsigned char weapon[0x1000]{},secondWeapon[0x1000]{},holder[0x100]{},secondHolder[0x100]{};
const unsigned char* holders[2]={holder,secondHolder};
const unsigned char* pickedWeapon=nullptr;
float originalFov=sightzoom::kBaseFov;
int checks=0,failures=0;
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
void __fastcall Original(void* cam,void*) {
    if(originalWrites)*reinterpret_cast<float*>(static_cast<unsigned char*>(cam)+kCamFov)=originalFov;
}
void Setup() {
    ResetSightZoom();applied=Applied{};config=Config{};installed=true;nextCamStep=&Original;
    mapKeys=false;mapView=false;highView=false;sazabiVehicle=false;originalWrites=true;originalFov=sightzoom::kBaseFov;playerHuman=human;
    highActive=highReturning=heliVehicle=aircraft=gunship=fuel=lobbed=false;knownRound=hud=true;roundKind=RoundKind::arc;pickedWeapon=nullptr;
    std::memset(weapon,0,sizeof(weapon));std::memset(secondWeapon,0,sizeof(secondWeapon));
    Put<void*>(holder,kHolderWeapon,weapon);Put<void*>(secondHolder,kHolderWeapon,secondWeapon);
    Put<int>(weapon,edf::kWeaponAmmoAlive,120);Put<int>(secondWeapon,edf::kWeaponAmmoAlive,120);
    std::memset(vehicle,0,sizeof(vehicle));std::memset(otherVehicle,0,sizeof(otherVehicle));std::memset(seats,0,sizeof(seats));
    std::memset(human,0,sizeof(human));std::memset(otherHuman,0,sizeof(otherHuman));std::memset(camera,0,sizeof(camera));
    Put<int>(vehicleCtrl,8,1);Put<int>(humanCtrl,8,1);Put<int>(otherCtrl,8,1);
    Put<void*>(vehicle,kSelfCtrl,vehicleCtrl);Put<void*>(human,kSelfCtrl,humanCtrl);Put<void*>(otherHuman,kSelfCtrl,otherCtrl);
    Put<void*>(vehicle,kSeats,seats);Put<std::uint64_t>(vehicle,kSeatCount,2);
    Put<void*>(human,kHumanVehicleCtrl,vehicleCtrl);Put<void*>(human,kHumanVehicleCtrl-8,vehicle);
    human[kHumanPlayer]=1;Put<void*>(human,kHumanPad,human);
    otherHuman[kHumanPlayer]=1;Put<void*>(otherHuman,kHumanPad,otherHuman);
    Put<void*>(seats,kSeatRider,human);Put<void*>(seats,kSeatRiderCtrl,humanCtrl);seats[kSeatPad]=1;
    Put<const void*>(seats,kSeatWeapons,holders);Put<std::uint64_t>(seats,kSeatWeaponCount,1);
    Put<void*>(camera,kCamTarget,human);Put<void*>(camera,kCamTargetRef,human);
    Put<float>(camera,kCamFov,originalFov);
}
void Button(unsigned short mask){Put<unsigned short>(seats,kSeatButtons,mask);SightZoomStock(vehicle);}
void Zoom(){Button(0);Button(0x80);Check(SightZoomNow(vehicle)==3.0f,"pad release/press selects 3x");}
float Fov(){CamStepHook(camera,nullptr);return At<float>(camera,kCamFov);}
}
const Config& Cfg() noexcept{return config;}
void Log(const char*,...) noexcept{}
unsigned char* PlayerHuman() noexcept{return playerHuman;}
bool MapHoldsKeys() noexcept{return mapKeys;}
bool MapOwnsView() noexcept{return mapView;}
bool HighCamOffered(unsigned char*) noexcept{return highView;}
bool IsSazabi(const void*) noexcept{return sazabiVehicle;}
bool HudReady() noexcept{return hud;}
bool HighCamOn(const void*) noexcept{return highActive;}
bool TurretCamHighTransition(const void*) noexcept{return highReturning;}
bool IsHelicopter(const void*) noexcept{return heliVehicle;}
bool PlayerJetOwnSight(const void*) noexcept{return aircraft;}
bool GunshipCrewSeats(const void*) noexcept{return gunship;}
PluginBody BodyOf(const void*) noexcept{return aircraft ? PluginBody::jet : sazabiVehicle ? PluginBody::sazabi : PluginBody::none;}
bool IsFuelTank(const unsigned char*) noexcept{return fuel;}
unsigned char* PayloadPicked(const void*) noexcept{return nullptr;} // store selection is not the fire-control selection
unsigned char* PayloadSightPicked(const void*,unsigned) noexcept{return const_cast<unsigned char*>(pickedWeapon ? pickedWeapon : weapon);}
bool ReadRound(const unsigned char* w,RoundModel* r) noexcept {
    *r=RoundModel{};r->kind=roundKind;r->rtti=knownRound ? "test factory" : nullptr;r->lobbed=lobbed;r->alive=At<int>(w,edf::kWeaponAmmoAlive);
    return true;
}
}
namespace edf {
bool MeanMuzzle(const unsigned char*,std::uint64_t,float* p,float* d) noexcept {p[0]=p[1]=p[2]=d[0]=d[1]=0;d[2]=1;return true;}
}
int main(){
    using namespace crew;
    Setup();Button(0x80);Check(SightZoomNow(vehicle)==1,"held while boarding is not a press");Zoom();
    Check(std::fabs(Fov()-originalFov/3)<1e-6f,"camera applies selected zoom");
    originalWrites=false;
    for(int i=0;i<100;++i)Check(std::fabs(Fov()-originalFov/3)<1e-6f,"no-write original does not compound zoom");
    Put<void*>(camera,kCamTarget,nullptr);Put<void*>(camera,kCamTargetRef,nullptr);
    Check(Fov()==originalFov,"expired camera target restores FOV even when original leaves it untouched");
    Setup();Zoom();Fov();originalWrites=false;ResetSightZoom();Check(Fov()==originalFov,"mission reset restores prior write");
    Setup();Zoom();Fov();originalWrites=false;config.enabled=false;
    Check(Fov()==originalFov && SightZoomNow(vehicle)==1,"disabled plugin immediately drops cue");SightZoomStock(vehicle);
    config.enabled=true;Button(0);Check(SightZoomNow(vehicle)==1,"re-enable starts at 1x");
    Setup();Zoom();config.sightZoom=false;Check(SightZoomNow(vehicle)==1,"disabled feature immediately drops cue");SightZoomStock(vehicle);
    config.sightZoom=true;Button(0);Check(SightZoomNow(vehicle)==1,"feature re-enable starts at 1x");
    Setup();Zoom();Fov();originalWrites=false;mapView=true;Check(Fov()==originalFov,"map owns an unzoomed view");
    mapKeys=true;Button(0);Button(0x80);Check(toggle.step==1,"map input does not toggle pad zoom");
    mapKeys=false;Button(0x80);Check(toggle.step==1,"held map button is not replayed on close");
    Button(0);Button(0x80);Check(toggle.step==2,"new press after map close changes zoom");
    Setup();Zoom();Put<int>(vehicleCtrl,8,0);Check(SightZoomNow(vehicle)==1,"expired vehicle weak reference rejected");
    Setup();Zoom();Put<void*>(human,kHumanVehicleCtrl-8,otherVehicle);Check(SightZoomNow(nullptr)==1,"different vehicle cannot inherit cue");
    Setup();Zoom();vehicle[kDead]=1;Check(SightZoomNow(vehicle)==1,"wrecked vehicle drops cue immediately");
    Setup();Zoom();human[kDead]=1;Check(SightZoomNow(vehicle)==1,"dead player drops cue immediately");
    Setup();Zoom();Put<void*>(vehicle,kSelfCtrl,otherCtrl);Check(SightZoomNow(vehicle)==1,"reused vehicle address cannot inherit cue");
    Setup();Zoom();Put<void*>(human,kSelfCtrl,otherCtrl);Check(SightZoomNow(vehicle)==1,"reused human address cannot inherit cue");
    Setup();Zoom();Put<void*>(seats,kSeatRider,nullptr);Put<void*>(seats,kSeatRiderCtrl,nullptr);
    Check(SightZoomNow(vehicle)==1,"old seat cue expires immediately on leaving");SightZoomStock(vehicle);
    Put<void*>(seats,kSeatRider,human);Put<void*>(seats,kSeatRiderCtrl,humanCtrl);Button(0);Check(toggle.step==0,"quick reboarding starts at 1x");
    Setup();Zoom();Put<void*>(seats,kSeatRider,otherHuman);Check(SightZoomNow(vehicle)==1,"another local player's seat is not this cue's");
    SightZoomStock(vehicle);Check(toggle.step==0,"another local player cannot publish a key toggle");
    Setup();Zoom();human[edf::kRiderNet+edf::kNetFlags]=1;Check(SightZoomNow(vehicle)==1,"remote replica cannot own local zoom");
    Setup();Zoom();std::memcpy(otherCamera,camera,sizeof(camera));Put<void*>(otherCamera,kCamTarget,otherHuman);Put<void*>(otherCamera,kCamTargetRef,otherHuman);
    CamStepHook(otherCamera,nullptr);Check(At<float>(otherCamera,kCamFov)==originalFov,"second local camera is unchanged");
    Setup();Zoom();Fov();originalWrites=false;Put<float>(camera,kCamFov,0.6f);mapView=true;
    Check(Fov()==0.6f,"a later camera owner's write is preserved");
    Setup();Zoom();AcquireSRWLockExclusive(&cueLock);cue.at=GetTickCount64()-kCueMs-1;ReleaseSRWLockExclusive(&cueLock);
    Check(SightZoomNow(vehicle)==1,"stale cue rejected");
    Setup();highView=true;Button(0);Button(0x80);Check(SightZoomNow(vehicle)==1,"high view keeps shared R3");
    Setup();highView=true;config.highCamButton=0x40;Zoom();Check(toggle.step==1,"separately bound high view leaves zoom button available");
    Setup();sazabiVehicle=true;Button(0);Button(0x80);Check(toggle.step==0,"Sazabi hard lock keeps shared R3 without also zooming");
    Setup();sazabiVehicle=true;config.sightZoomButton=0x40;Button(0);Button(0x40);
    Check(toggle.step==1,"a separate Sazabi zoom binding remains usable");
    Setup();sazabiVehicle=true;config.sazabiLockButton=0;Zoom();Check(toggle.step==1,"disabled hard-lock binding releases R3 to zoom");
    Setup();sazabiVehicle=true;config.sazabi=false;Button(0);Button(0x80);
    Check(SightZoomNow(vehicle)==1,"disabled Sazabi fire control has no virtual weapon sight");
    Setup();Zoom();playerHuman=otherHuman;Put<void*>(otherHuman,kHumanVehicleCtrl,vehicleCtrl);
    Put<void*>(otherHuman,kHumanVehicleCtrl-8,vehicle);Put<void*>(seats,kSeatRider,otherHuman);Put<void*>(seats,kSeatRiderCtrl,otherCtrl);
    Button(0);Check(toggle.step==0,"another tracked human in the same seat starts at 1x");
    Setup();Put<std::uint64_t>(seats,kSeatWeaponCount,0);Button(0);Button(0x80);
    Check(SightZoomNow(vehicle)==1 && SightZoomView()==sightzoom::Kind::none,"unarmed passenger cannot enable a scope");
    Setup();fuel=true;Button(0);Button(0x80);Check(SightZoomNow(vehicle)==1,"fuel holder is not a gun sight");
    Setup();knownRound=false;Button(0);Button(0x80);Check(SightZoomNow(vehicle)==1,"unknown ammo has no fabricated optic");
    Setup();Zoom();highActive=true;Check(Fov()==originalFov && SightZoomNow(vehicle)==1,"entering overhead restores FOV before another input frame");
    Button(0);Button(0x80);Check(SightZoomView()==sightzoom::Kind::indirect && toggle.step==0,"overhead is its own fire-control view, never a zoomed scope");
    highActive=false;highReturning=true;Check(SightZoomNow(vehicle)==1,"returning overhead camera is not magnified");
    Setup();Put<int>(weapon,edf::kWeaponMark,edf::kMarkLofted);Button(0);Button(0x80);
    Check(SightZoomNow(vehicle)==1 && SightZoomView()==sightzoom::Kind::indirect,"Katyusha uses its impact/spread solution, not an optical scope");
    Setup();Put<int>(weapon,edf::kWeaponMark,edf::kMarkGround);Put<int>(weapon,edf::kWeaponAmmoAlive,1200);Button(0);Button(0x80);
    Check(SightZoomView()==sightzoom::Kind::indirect,"long-life howitzer remains indirect fire");
    Setup();Zoom();Check(SightZoomView()==sightzoom::Kind::optical,"tank cannon owns an optical sight");
    Setup();roundKind=RoundKind::homing;Zoom();Check(SightZoomView()==sightzoom::Kind::missile,"guided missiles use lock fire control");
    Setup();roundKind=RoundKind::rocket;Zoom();Check(SightZoomView()==sightzoom::Kind::rocket,"unguided rockets use ballistic fire control");
    Setup();lobbed=true;Zoom();Check(SightZoomView()==sightzoom::Kind::rocket,"short-range grenade uses a ballistic sensor instead of a rifle optic");
    Setup();heliVehicle=true;Zoom();Check(SightZoomView()==sightzoom::Kind::flight,"armed helicopter pilot keeps a flight HUD without optical vignette");
    Setup();Zoom();Put<std::uint64_t>(seats,kSeatWeaponCount,2);pickedWeapon=secondWeapon;
    Check(SightZoomNow(vehicle)==1,"new selected weapon cannot inherit old weapon magnification");
    Button(0);Check(toggle.step==0,"new weapon begins at normal field of view");
    Setup();Zoom();hud=false;Check(Fov()==originalFov,"failed sight renderer cannot leave a zoomed camera");
    Check(sightzoom::MaskOf(sightzoom::Kind::flight)==sightzoom::Mask::none && sightzoom::MaskOf(sightzoom::Kind::mech)==sightzoom::Mask::none,
          "flight and mech fire control never stack a generic scope mask");
    std::printf("sightzoom: %d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
