// Run the actual sightzoom.cpp hook and seat updates with native-layout fixtures.
// The original camera stand-in can leave FOV unwritten, as EDF.dll does on an expired target.
#include "../src/sightzoom.cpp"
#include <cstdio>
#include <array>
namespace crew {
unsigned char* image=nullptr;
namespace {
Config config{};
unsigned char vehicle[0x1200]{},otherVehicle[0x1200]{},seats[2*kSeatStride]{},human[0x1600]{},otherHuman[0x1600]{};
unsigned char camera[0x500]{},otherCamera[0x500]{},vehicleCtrl[16]{},humanCtrl[16]{},otherCtrl[16]{};
unsigned char* playerHuman=human;
bool mapKeys=false,mapView=false,highView=false,originalWrites=true,sazabiVehicle=false,paused=false;
bool highActive=false,highReturning=false,heliVehicle=false,aircraft=false,gunship=false,fuel=false,knownRound=true,hud=true;
RoundKind roundKind=RoundKind::arc;
WeaponStyle roundStyle=WeaponStyle::projectile;
bool lobbed=false;
float roundSpeed=0.0f,roundFactor=0.0f;
int wheelNotches=0;bool wheelHeld=false;
alignas(16) unsigned char bones[5*weaponmount::kStride]{};
bool opticPresent=true,extraOptic=false;
float muzzleDirection[3]={0,0,1};
LookToFn testedLookTo=nullptr;
unsigned char weapon[0x1000]{},secondWeapon[0x1000]{},holder[0x100]{},secondHolder[0x100]{};
const unsigned char* holders[2]={holder,secondHolder};
const unsigned char* pickedWeapon=nullptr;
float originalFov=sightzoom::kBaseFov;
const float thirdPerson[16]={1,0,0,0,0,1,0,0,0,0,1,0,25,12,-20,1};
int checks=0,failures=0;
int autoSteers=0,remaps=0;bool remapOk=true;   // EDF6AutoTurret's Steers answer; stab.cpp RemapAimAxis's bone map
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
void __fastcall Original(void* cam,void*) {
    if(originalWrites) {
        *reinterpret_cast<float*>(static_cast<unsigned char*>(cam)+kCamFov)=originalFov;
        std::memcpy(static_cast<unsigned char*>(cam)+kCamMatrix,thirdPerson,sizeof(thirdPerson));
    }
}
float* __fastcall LookFixture(float* m,const float* d) {
    std::memset(m,0,16*sizeof(float));m[0]=1;m[5]=1;m[10]=1;m[15]=1;
    m[8]=d[0];m[9]=d[1];m[10]=d[2];return m;
}
void Setup() {
    ResetSightZoom();applied=Applied{};config=Config{};installed=true;nextCamStep=&Original;opticLookTo=testedLookTo ? testedLookTo : &LookFixture;opticPresent=true;extraOptic=false;
    muzzleDirection[0]=muzzleDirection[1]=0;muzzleDirection[2]=1;
    mapKeys=false;mapView=false;highView=false;sazabiVehicle=false;paused=false;originalWrites=true;originalFov=sightzoom::kBaseFov;playerHuman=human;
    highActive=highReturning=heliVehicle=aircraft=gunship=fuel=lobbed=false;roundSpeed=roundFactor=0.0f;wheelNotches=0;knownRound=hud=true;roundKind=RoundKind::arc;roundStyle=WeaponStyle::projectile;pickedWeapon=nullptr;
    autoSteers=0;remaps=0;remapOk=true;
    std::memset(weapon,0,sizeof(weapon));std::memset(secondWeapon,0,sizeof(secondWeapon));
    Put<void*>(holder,kHolderWeapon,weapon);Put<void*>(secondHolder,kHolderWeapon,secondWeapon);
    Put<int>(weapon,edf::kWeaponAmmoAlive,120);Put<int>(secondWeapon,edf::kWeaponAmmoAlive,120);
    std::memset(vehicle,0,sizeof(vehicle));std::memset(otherVehicle,0,sizeof(otherVehicle));std::memset(seats,0,sizeof(seats));
    std::memset(human,0,sizeof(human));std::memset(otherHuman,0,sizeof(otherHuman));std::memset(camera,0,sizeof(camera));
    std::memset(bones,0,sizeof(bones));
    Put<void*>(vehicle+weaponmount::kVehicleModel,weaponmount::kRecords,bones);
    Put<int>(vehicle+weaponmount::kVehicleModel,weaponmount::kCount,5);
    for(int i=0;i<5;++i) {
        auto bone=bones+i*weaponmount::kStride;
        Put<int>(bone,weaponmount::kOwnIndex,i);Put<int>(bone,weaponmount::kParent,i==0 ? -1 : i==1 ? 0 : 1);
        Put<float>(bone,kBoneWorld506,1);Put<float>(bone,kBoneWorld506+20,1);Put<float>(bone,kBoneWorld506+40,1);
        Put<float>(bone,kBoneWorld506+60,1);
    }
    Put<float>(bones+3*weaponmount::kStride,kBoneWorld506+48,4);
    Put<float>(bones+3*weaponmount::kStride,kBoneWorld506+52,5);
    Put<float>(bones+3*weaponmount::kStride,kBoneWorld506+56,6);
    Put<void*>(holder,weaponmount::kHolderBone,bones+2*weaponmount::kStride);
    Put<void*>(secondHolder,weaponmount::kHolderBone,bones+2*weaponmount::kStride);
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
    std::memcpy(camera+kCamMatrix,thirdPerson,sizeof(thirdPerson));
}
void Button(unsigned short mask){Put<unsigned short>(seats,kSeatButtons,mask);SightZoomStock(vehicle);}
void Zoom(){Button(0);Button(0x80);Check(SightZoomNow(vehicle)==3.0f,"pad release/press selects 3x");}
float Fov(){CamStepHook(camera,nullptr);return At<float>(camera,kCamFov);}
// Seat 0's aim (seat+0xE0): its pitch axis {min, max, angle, rate} and the params' top rate, as docs/nix-re.md §3 has them.
unsigned char* Aim(){return seats+kSeatAim;}
float* Pitch(){return reinterpret_cast<float*>(Aim()+kAimAxes+kAxisStride);}
void Axis(float lo,float hi,float top){Pitch()[0]=lo;Pitch()[1]=hi;Pitch()[2]=0.0f;Put<float>(Aim(),kAimParams+8,top);}
// One aim step of the seat (stab.cpp StepHook: the stock step left the axis as it is), then the drawn barrel posed from
// the axis (the pitch negative up) so the muzzle's direction is the gun's.
void LayFrame() {
    SightLayStep(Aim(),Pitch()[2]);
    const float e=-Pitch()[2];
    muzzleDirection[0]=0.0f;muzzleDirection[1]=std::sin(e);muzzleDirection[2]=std::cos(e);
}
// The camera's line of sight's elevation, rad (LookFixture puts the view's direction in the third row).
float Sight(){Fov();return std::asin(At<float>(camera,kCamMatrix+36));}
}
unsigned char* BoneRecord506(const unsigned char*,const wchar_t* name) noexcept {
    if(extraOptic && std::wcscmp(name,L"vc_optic_01")==0)return bones+4*weaponmount::kStride;
    return opticPresent && std::wcscmp(name,L"vc_optic_00")==0 ? bones+3*weaponmount::kStride : nullptr;
}
const Config& Cfg() noexcept{return config;}
void Log(const char*,...) noexcept{}
unsigned char* PlayerHuman() noexcept{return playerHuman;}
int TakeScopeWheel(bool hold) noexcept{wheelHeld=hold;const int n=hold ? wheelNotches : 0;wheelNotches=0;return n;}
bool MapPadButtons(std::uint16_t*) noexcept{return false;}
bool GamePaused() noexcept{return paused;}
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
int AutoTurretSteers(const void*,unsigned) noexcept{return autoSteers;}
bool AimAxisAdjustable() noexcept{return remapOk;}
bool RemapAimAxis(unsigned char* aim,int axis,float,float corrected) noexcept {
    if(!remapOk || axis!=1)return false;
    ++remaps;reinterpret_cast<float*>(aim+kAimAxes+kAxisStride)[2]=corrected;return true;
}
bool ReadRound(const unsigned char* w,RoundModel* r) noexcept {
    *r=RoundModel{};r->kind=roundKind;r->style=roundStyle;r->rtti=knownRound ? "test factory" : nullptr;r->lobbed=lobbed;r->alive=At<int>(w,edf::kWeaponAmmoAlive);r->speed=roundSpeed;r->factor=roundFactor;
    return true;
}
}
namespace edf {
bool WorldGravity(const unsigned char*,float* g) noexcept {g[0]=0;g[1]=-9.8f;g[2]=0;return true;}
bool MeanMuzzle(const unsigned char*,std::uint64_t,float* p,float* d) noexcept {p[0]=p[1]=p[2]=0;std::memcpy(d,crew::muzzleDirection,12);return true;}
}
float NativeSin(float x){return std::sin(x);}
float NativeCos(float x){return std::cos(x);}
float NativeSqrt(float x){return std::sqrt(x);}
float NativeAtan2(float y,float x){return std::atan2(y,x);}
void BindMath(unsigned char* base,std::size_t slot,std::uintptr_t fn) {
    DWORD old=0,ignored=0;
    if(!VirtualProtect(base+slot,8,PAGE_READWRITE,&old))std::abort();
    std::memcpy(base+slot,&fn,8);
    if(!VirtualProtect(base+slot,8,old,&ignored))std::abort();
}
int wmain(int argc,wchar_t** argv){
    using namespace crew;
    HMODULE native=nullptr;
    if(argc>1) {
        if(!argv[1][0] || GetFileAttributesW(argv[1])==INVALID_FILE_ATTRIBUTES){std::puts("SKIP: supported EDF.dll not supplied");return 77;}
        native=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
        Check(native!=nullptr,"private EDF module maps without running DllMain");
        if(!native)return 1;
        const auto base=reinterpret_cast<unsigned char*>(native);
        const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        Check(nt->FileHeader.TimeDateStamp==0x678CCB46 && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64,"supported native image");
        Check(std::memcmp(base+kLookTo,kLookToCode,sizeof(kLookToCode))==0,"native LookTo fingerprint");
        if(failures){FreeLibrary(native);return 1;}
        // DONT_RESOLVE intentionally leaves imports untouched. Bind only the four CRT
        // math imports used by this native camera function, in this private image.
        BindMath(base,0x1756408,reinterpret_cast<std::uintptr_t>(&NativeSin));
        BindMath(base,0x1756400,reinterpret_cast<std::uintptr_t>(&NativeCos));
        BindMath(base,0x1756438,reinterpret_cast<std::uintptr_t>(&NativeSqrt));
        BindMath(base,0x1756450,reinterpret_cast<std::uintptr_t>(&NativeAtan2));
        testedLookTo=reinterpret_cast<LookToFn>(base+kLookTo);
    }
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
    Setup();Button(0);
    Check(SightZoomCanMount(vehicle,0) && !SightZoomMounted(vehicle) && SightZoomNow(vehicle)==1,"binding reservation sees actual installed optic before its first zoom press");
    Check(!SightZoomCanMount(vehicle,1) && !SightZoomCanMount(otherVehicle,0),"real optic availability is bound to exact current vehicle and seat");
    opticPresent=false;Check(!SightZoomCanMount(vehicle,0),"lost physical lens immediately releases binding despite old optical-kind cue");
    Setup();Zoom();paused=true;Sleep(230);Fov();
    Check(SightZoomNow(vehicle)==3 && SightZoomMounted(vehicle),"pause past the old wall TTL retains the same physical optic owner");
    Button(0x80);paused=false;Button(0x80);Check(toggle.step==1,"menu-held zoom button is not replayed on resume");
    Button(0);Button(0x80);Check(toggle.step==2,"new post-pause press still changes zoom");
    Setup();Zoom();Sleep(230);Button(0);
    Check(toggle.step==1 && SightZoomNow(vehicle)==3,"slow input frames do not reset the owner's scope choice");
    Put<void*>(seats,kSeatRiderCtrl,nullptr);
    Check(SightZoomNow(vehicle)==1,"identity loss still revokes scope immediately without a timeout");
    Setup();highView=true;Button(0);Button(0x80);Check(SightZoomNow(vehicle)==1,"high view keeps shared R3");
    Setup();highView=true;config.highCamButton=0x40;Zoom();Check(toggle.step==1,"separately bound high view leaves zoom button available");
    Setup();sazabiVehicle=true;Button(0);Button(0x80);Check(toggle.step==0,"Sazabi hard lock keeps shared R3 without also zooming");
    Setup();sazabiVehicle=true;config.sightZoomButton=0x40;Button(0);Button(0x40);
    Check(toggle.step==0,"Sazabi without physical optic does not fake zoom");
    Setup();sazabiVehicle=true;config.sazabiLockButton=0;Button(0);Button(0x80);Check(toggle.step==0,"removing lock binding cannot create a missing optic");
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
    Button(0);Button(0x80);Check(SightZoomView()==sightzoom::Kind::indirect && SightZoomNow(vehicle)==1 && toggle.step==1,
                                 "overhead is its own fire-control view, never a zoomed scope; its presses do not step the scope");
    highActive=false;highReturning=true;Check(SightZoomNow(vehicle)==1,"returning overhead camera is not magnified");
    highReturning=false;Button(0);Check(SightZoomNow(vehicle)==3,"back on the gunner's sight the seat's own magnification returns");
    Setup();Put<int>(weapon,edf::kWeaponMark,edf::kMarkLofted);Button(0);Button(0x80);
    Check(SightZoomNow(vehicle)==1 && SightZoomView()==sightzoom::Kind::indirect,"Katyusha uses its impact/spread solution, not an optical scope");
    Setup();Put<int>(weapon,edf::kWeaponMark,edf::kMarkGround);Put<int>(weapon,edf::kWeaponAmmoAlive,1200);Button(0);Button(0x80);
    Check(SightZoomView()==sightzoom::Kind::indirect,"long-life howitzer remains indirect fire");
    Setup();Zoom();Check(SightZoomView()==sightzoom::Kind::optical,"tank cannon owns an optical sight");
    Fov();Check(At<float>(camera,kCamMatrix+48)==4 && At<float>(camera,kCamMatrix+52)==5 && At<float>(camera,kCamMatrix+56)==6,
                "scope eye uses physical optic bone, not third-person camera or muzzle");
    Check(SightZoomMounted(vehicle),"active physical view asks turret input to stay native");
    originalWrites=false;ResetSightZoom();Fov();Check(std::memcmp(camera+kCamMatrix,thirdPerson,sizeof(thirdPerson))==0,"leaving restores untouched original world matrix");
    Setup();opticPresent=false;Button(0);Button(0x80);
    Check(SightZoomNow(vehicle)==1 && !SightZoomMounted(vehicle),"an armed vehicle without an installed optic cannot merely magnify third-person view");
    Setup();Put<int>(bones+3*weaponmount::kStride,weaponmount::kParent,0);Button(0);Button(0x80);
    Check(SightZoomNow(vehicle)==1,"a scene-root optic does not belong to every weapon");
    Setup();Zoom();Fov();originalWrites=false;Put<float>(camera,kCamMatrix+48,99);mapView=true;Fov();
    Check(At<float>(camera,kCamMatrix+48)==99,"another camera owner's world matrix is never overwritten during restore");
    Setup();roundStyle=WeaponStyle::maser;Zoom();Check(SightZoomView()==sightzoom::Kind::sensor,"a physical energy optic is a sensor, not a cannon scope");
    Setup();roundKind=RoundKind::homing;Zoom();Check(SightZoomView()==sightzoom::Kind::missile,"guided missiles use lock fire control");
    Setup();roundKind=RoundKind::rocket;Zoom();Check(SightZoomView()==sightzoom::Kind::rocket,"unguided rockets use ballistic fire control");
    Setup();lobbed=true;Zoom();Check(SightZoomView()==sightzoom::Kind::rocket,"short-range grenade uses a ballistic sensor instead of a rifle optic");
    Setup();heliVehicle=true;Zoom();Check(SightZoomView()==sightzoom::Kind::flight,"armed helicopter pilot keeps a flight HUD without optical vignette");
    // 2026-10-11 ("鐢ㄥ壇姝﹀櫒鐨勬椂鍊欑瀯鍏蜂笉浼氬己鍒跺垏鍥炲幓锛岃€屾槸涓よ竟鍏辩敤"): a second weapon on the same mount (the coaxial gun on
    // the secondary trigger, a store the switch picks) looks through the same optic: the scope, its magnification and
    // its eye stay, through the pick and back, with no frame of the third-person view in between.
    Setup();Put<std::uint64_t>(seats,kSeatWeaponCount,2);Put<void*>(holder,kHolderCtrl,vehicleCtrl);Put<void*>(secondHolder,kHolderCtrl,vehicleCtrl);
    Put<void*>(secondHolder,weaponmount::kHolderBone,At<void*>(holder,weaponmount::kHolderBone));Zoom();
    pickedWeapon=secondWeapon;
    Check(SightZoomNow(vehicle)==3 && SightZoomMounted(vehicle),"a second weapon on the same optic keeps the scope before its first input frame");
    Fov();Check(Fov()<originalFov,"the camera stays magnified across the trigger change");
    Button(0);Check(toggle.step==1 && SightZoomNow(vehicle)==3,"the secondary weapon shares the seat's magnification");
    Button(0x80);Check(SightZoomNow(vehicle)==6,"the shared scope still steps on the secondary weapon");
    pickedWeapon=weapon;Button(0);Check(SightZoomNow(vehicle)==6,"back on the primary the shared scope is unchanged");
    Put<void*>(secondHolder,kHolderCtrl,nullptr);
    Setup();pickedWeapon=secondWeapon;Button(0);Button(0x80);Check(SightZoomNow(vehicle)==1,"stale selection outside this seat does not borrow another gun optic");
    // 2026-10-09 (fb aim #2): the Titan's right trigger picks its hull gatling (holder on the hull root, no optic of its
    // own). The seat's turret optic stays the scope's, through a pick and back, and so does the key reserved for it.
    Setup();Put<std::uint64_t>(seats,kSeatWeaponCount,2);Put<void*>(holder,kHolderCtrl,vehicleCtrl);Put<void*>(secondHolder,kHolderCtrl,vehicleCtrl);
    Put<void*>(secondHolder,weaponmount::kHolderBone,bones);
    pickedWeapon=secondWeapon;Zoom();
    Check(SightZoomMounted(vehicle) && SightZoomCanMount(vehicle,0),"hull gun pick keeps the seat's turret optic and its key");
    pickedWeapon=weapon;Button(0);Check(SightZoomNow(vehicle)==3,"switching back to the cannon keeps the same optic and zoom");
    pickedWeapon=secondWeapon;Button(0);Check(SightZoomNow(vehicle)==3 && SightZoomCanMount(vehicle,0),"switching to the gatling again is symmetric, no lost scope");
    Setup();Put<std::uint64_t>(seats,kSeatWeaponCount,2);Put<void*>(holder,kHolderCtrl,nullptr);Put<void*>(secondHolder,kHolderCtrl,vehicleCtrl);
    Put<void*>(secondHolder,weaponmount::kHolderBone,bones);pickedWeapon=secondWeapon;
    Button(0);Button(0x80);Check(SightZoomNow(vehicle)==1,"a dead holder's optic is never borrowed");
    Put<void*>(secondHolder,kHolderCtrl,nullptr);   // Setup leaves the holders' controls as they are
    // 2026-10-11 ("类似战雷那样，鼠标滚轮可以调到100m、200m"): the range setting. The wheel is the sight's only while it is
    // magnified; each notch a step of the graduated drum (25 m to 300, 50 m to 1000, 100 m beyond) up to the round's reach;
    // the camera's line of sight is laid under the bore by
    // the round's superelevation for that range, the setting kept across the seat's weapons and dropped with the seat.
    Setup();Button(0);Check(!wheelHeld,"at 1x the wheel stays the game's");
    roundSpeed=10.0f;roundFactor=1.0f;Zoom();Check(wheelHeld,"magnified, the sight takes the wheel");
    wheelNotches=3;Button(0);float angle=0.0f;
    Check(SightZoomRange(vehicle,&angle)==75,"three notches up: 75 m (the fine end of the drum)");
    const float want=sightzoom::ZeroAngle(10.0f,9.8f/3600.0f,75.0f);
    Check(want>0.0f && std::fabs(angle-want)<1e-6f,"the superelevation of this round at that range");
    Fov();{const auto m=reinterpret_cast<const float*>(camera+kCamMatrix);
           Check(std::fabs(m[9]+std::sin(want))<1e-4f && std::fabs(m[10]-std::cos(want))<1e-4f,"the line of sight laid under the bore by it");}
    wheelNotches=-1;Button(0);Check(SightZoomRange(vehicle)==50,"a notch down: 50 m");
    wheelNotches=1000;Button(0);Check(SightZoomRange(vehicle)==1200,"held to the round's reach (10 m a frame for 120 frames)");
    wheelNotches=-1000;Button(0);Check(SightZoomRange(vehicle)==0,"down to the bore line");
    Fov();{const auto m=reinterpret_cast<const float*>(camera+kCamMatrix);Check(m[9]==0.0f && m[10]==1.0f,"at 0 the bore line itself");}
    wheelNotches=14;Button(0);Button(0x80);Check(SightZoomRange(vehicle)==400 && SightZoomNow(vehicle)==6,"the setting stays through a magnification step");
    Button(0);Button(0x80);Check(SightZoomRange(vehicle)==0 && !wheelHeld,"back at 1x: no setting shown, the wheel the game's again");
    Button(0);Button(0x80);Check(SightZoomRange(vehicle)==400,"magnified again: the seat's setting");
    config.sightRangeStep=200;wheelNotches=2;Button(0);Check(SightZoomRange(vehicle)==800,"a fixed step (ini SightRangeStep) when set");
    ResetSightZoom();Check(SightZoomRange(vehicle)==0,"a new seat or mission starts at the bore line");    Setup();Zoom();hud=false;Check(Fov()==originalFov,"failed sight renderer cannot leave a zoomed camera");
    Check(sightzoom::MaskOf(sightzoom::Kind::flight)==sightzoom::Mask::none && sightzoom::MaskOf(sightzoom::Kind::mech)==sightzoom::Mask::none,
          "flight and mech fire control never stack a generic scope mask");
    Setup();extraOptic=true;Button(0);Button(0x80);Check(!SightZoomMounted(vehicle) && SightZoomNow(vehicle)==1,"ambiguous same-mount optics fail closed");
    Setup();Zoom();Fov();opticPresent=false;originalWrites=false;Fov();
    Check(std::memcmp(camera+kCamMatrix,thirdPerson,sizeof(thirdPerson))==0 && At<float>(camera,kCamFov)==originalFov,"losing installed lens restores camera and FOV immediately");
    // 2026-10-11 ("调标尺的时候准镜和炮管应该会动啊"): the drum is laid on the gun. Turning it leaves the line of sight on
    // the target and the barrel climbs over it by the superelevation at a gun drive's pace through the seat aim's own
    // pitch axis; the camera looks along the climbing bore lowered by what the gun carries so far.
    {
        Setup();roundSpeed=10.0f;roundFactor=1.0f;Axis(-0.6f,0.2f,0.01f);Zoom();
        LayFrame();Check(lay.engaged && lay.laid==0.0f && remaps==0 && Pitch()[2]==0.0f,"the sight comes up on the bore line: the lay engages, the gun stays");
        Check(std::fabs(Sight())<1e-6f,"the line of sight on the target");
        wheelNotches=1000;Button(0);
        const float elev=sightzoom::ZeroAngle(10.0f,9.8f/3600.0f,1200.0f);
        Check(SightZoomRange(vehicle)==1200 && elev>0.01f,"the drum at 1200 m: a superelevation worth laying");
        Check(std::fabs(Sight())<1e-6f,"turning the drum does not throw the view down (the old range drum lowered the camera)");
        int frames=0;float worst=0.0f,last=Pitch()[2];bool monotone=true;
        while(lay.laid<elev && frames<1000) {
            LayFrame();++frames;
            worst=std::fmax(worst,std::fabs(Sight()));
            monotone=monotone && Pitch()[2]<last;last=Pitch()[2];
        }
        Check(std::fabs(lay.laid-elev)<1e-6f && std::fabs(-Pitch()[2]-elev)<1e-6f,"the barrel stands the superelevation over the line of sight");
        Check(frames==static_cast<int>(std::ceil(elev/sightzoom::kLayRate)) && monotone,"at the gun drive's pace, climbing every frame, not a jump");
        Check(worst<1e-5f,"the reticle stays on the target while the barrel climbs");
        float shown=0.0f;Check(SightZoomRange(vehicle,&shown)==1200 && std::fabs(shown-elev)<1e-6f,"the readout's superelevation is the gun's");
        const int moved=remaps;LayFrame();Check(remaps==moved && Pitch()[2]==last,"laid: no further turn");
        wheelNotches=-1000;Button(0);
        for(int i=0;i<1000 && lay.laid>0.0f;++i){LayFrame();worst=std::fmax(worst,std::fabs(Sight()));}
        Check(lay.laid==0.0f && std::fabs(Pitch()[2])<1e-6f && worst<1e-5f,"back to the bore line the barrel comes down, the view still");
        // Out of the sight the gun is left where it is (the turret camera has it); the sight comes up on it laid at once.
        wheelNotches=1000;Button(0);for(int i=0;i<1000 && lay.laid<elev;++i)LayFrame();
        Button(0);Button(0x80);Button(0);Button(0x80);Check(SightZoomNow(vehicle)==1,"back at 1x");
        const float raised=Pitch()[2];LayFrame();Check(!lay.engaged && Pitch()[2]==raised,"at 1x the lay lets go and does not touch the gun");
        Button(0);Button(0x80);const int before=remaps;LayFrame();
        Check(lay.engaged && std::fabs(lay.laid-elev)<1e-6f && remaps==before && Pitch()[2]==raised,"magnified again: the setting carried at once, no second climb");
        // EDF6AutoTurret laying the gun on a lock solves the arc itself: the lay holds still.
        autoSteers=1;wheelNotches=-4;Button(0);const float held=Pitch()[2];LayFrame();
        Check(Pitch()[2]==held && std::fabs(lay.laid-elev)<1e-6f,"auto-aim's lock: no lay on top of its own arc");
        // Another seat's aim (an NPC's, another vehicle's) is never laid.
        autoSteers=0;SightLayStep(seats+kSeatStride+kSeatAim,0.0f);Check(Pitch()[2]==held,"only the player's sighted seat's aim");
        Aim()[kAimNetwork]=1;LayFrame();Check(Pitch()[2]==held,"an aim the network steps is never laid");Aim()[kAimNetwork]=0;
        LayFrame();Check(Pitch()[2]>held,"its own again: the gun comes down toward the new setting");
    }
    {   // the elevation stop: the gun carries what it can, and the view shows where the round really goes
        Setup();roundSpeed=10.0f;roundFactor=1.0f;Axis(-0.005f,0.2f,0.01f);Zoom();LayFrame();
        wheelNotches=1000;Button(0);for(int i=0;i<1000;++i)LayFrame();
        Check(Pitch()[2]==-0.005f && std::fabs(lay.laid-0.005f)<1e-6f,"held at its stop the gun carries only what it can");
        Check(std::fabs(Sight())<1e-5f,"and the line of sight did not move for the part it could not lay");
    }
    {   // the aim step's hooks not in (no bone map): the old way, the line of sight laid under the still bore
        Setup();roundSpeed=10.0f;roundFactor=1.0f;Axis(-0.6f,0.2f,0.01f);remapOk=false;Zoom();LayFrame();
        wheelNotches=1000;Button(0);LayFrame();
        const float elev=sightzoom::ZeroAngle(10.0f,9.8f/3600.0f,1200.0f);
        Check(!lay.engaged && Pitch()[2]==0.0f && std::fabs(Sight()+elev)<1e-5f,"no lay: the camera falls back to the set range's shown");
    }
    {   // review of #137: the setting is held to the reach only when turned; a shorter-reaching weapon picked after it
        // must not see ZeroAngle's 45 degrees
        Setup();roundSpeed=10.0f;roundFactor=1.0f;Axis(-0.6f,0.2f,0.01f);
        Put<std::uint64_t>(seats,kSeatWeaponCount,2);Put<void*>(holder,kHolderCtrl,vehicleCtrl);Put<void*>(secondHolder,kHolderCtrl,vehicleCtrl);
        Put<void*>(secondHolder,weaponmount::kHolderBone,At<void*>(holder,weaponmount::kHolderBone));Put<int>(secondWeapon,edf::kWeaponAmmoAlive,30);
        Zoom();LayFrame();wheelNotches=1000;Button(0);for(int i=0;i<1000;++i)LayFrame();
        const float cannon=sightzoom::ZeroAngle(10.0f,9.8f/3600.0f,1200.0f),coax=sightzoom::ZeroAngle(10.0f,9.8f/3600.0f,300.0f);
        pickedWeapon=secondWeapon;Button(0);float shown=0.0f;
        Check(SightZoomRange(vehicle,&shown)==300 && toggle.zero==1200,"the coaxial gun reads its own reach (300 m), the drum keeps the cannon's 1200");
        Check(std::fabs(ZeroOf(secondWeapon,1200)-coax)<1e-6f && ZeroOf(secondWeapon,1200)<0.1f,"past its reach its superelevation is its reach's, never 45 degrees");
        for(int i=0;i<1000;++i){LayFrame();Check(std::fabs(Sight())<1e-5f,"the re-lay for the coaxial round keeps the view");if(lay.laid==coax)break;}
        Check(std::fabs(lay.laid-coax)<1e-6f && std::fabs(-Pitch()[2]-coax)<1e-6f && coax<cannon,"the gun comes down to the coaxial round's lay");
        pickedWeapon=weapon;Button(0);Check(SightZoomRange(vehicle)==1200,"back on the cannon its setting is whole");
        Put<void*>(holder,kHolderCtrl,nullptr);Put<void*>(secondHolder,kHolderCtrl,nullptr);
        // A slow round lands short of its flight's reach: the drum stops where it can still land (speed^2 / drop).
        Setup();roundSpeed=1.0f;roundFactor=1.0f;Put<int>(weapon,edf::kWeaponAmmoAlive,1200);Zoom();wheelNotches=1000;Button(0);
        const int most=static_cast<int>(sightzoom::BallisticReach(1.0f,9.8f/3600.0f));
        Check(SightZoomRange(vehicle,&shown)==most && most<1200 && shown<0.785398163f && std::isfinite(shown),"held to where the round still lands, below 45 degrees");
    }
    Check(sightzoom::LayStep(0.01f,0.0f,0.0f,0.0f,-1.0f,1.0f).angle==-0.01f && sightzoom::LayStep(0.01f,0.0f,0.0f,0.0f,-1.0f,1.0f).laid==0.01f,
          "no rate: laid at once");
    Check(sightzoom::LayStep(-0.01f,0.0f,0.002f,0.0f,-1.0f,1.0f).angle==0.002f,"a negative lay lowers the gun at the rate");
    Check(sightzoom::LayStep(0.5f,0.0f,0.0f,0.0f,-7.0f,7.0f).angle==-0.5f,"a whole-circle axis has no stops");
    Check(sightzoom::LayStep(std::nanf(""),0.2f,0.01f,0.1f,-1.0f,1.0f).laid==0.2f,"not a number: nothing moves");
    if(native) {
        for(const auto& d: {std::array<float,3>{0,0,1},std::array<float,3>{1,0,0},std::array<float,3>{0.3f,0.4f,0.8660254f}}) {
            Setup();std::memcpy(muzzleDirection,d.data(),12);Zoom();Fov();
            const auto m=reinterpret_cast<const float*>(camera+kCamMatrix);
            Check(std::fabs(m[8]*d[0]+m[9]*d[1]+m[10]*d[2]-1.0f)<1e-5f,"real native camera basis follows selected barrel direction");
            Check(m[12]==4 && m[13]==5 && m[14]==6,"native camera eye is the tagged physical optic");
            originalWrites=false;ResetSightZoom();Fov();Check(std::memcmp(camera+kCamMatrix,thirdPerson,sizeof(thirdPerson))==0,"native view restores on exit");
        }
        FreeLibrary(native);
    }
    std::printf("sightzoom: %d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
