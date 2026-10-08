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
bool mapKeys=false,mapView=false,highView=false,originalWrites=true,sazabiVehicle=false;
bool highActive=false,highReturning=false,heliVehicle=false,aircraft=false,gunship=false,fuel=false,knownRound=true,hud=true;
RoundKind roundKind=RoundKind::arc;
WeaponStyle roundStyle=WeaponStyle::projectile;
bool lobbed=false;
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
    mapKeys=false;mapView=false;highView=false;sazabiVehicle=false;originalWrites=true;originalFov=sightzoom::kBaseFov;playerHuman=human;
    highActive=highReturning=heliVehicle=aircraft=gunship=fuel=lobbed=false;knownRound=hud=true;roundKind=RoundKind::arc;roundStyle=WeaponStyle::projectile;pickedWeapon=nullptr;
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
}
unsigned char* BoneRecord506(const unsigned char*,const wchar_t* name) noexcept {
    if(extraOptic && std::wcscmp(name,L"vc_optic_01")==0)return bones+4*weaponmount::kStride;
    return opticPresent && std::wcscmp(name,L"vc_optic_00")==0 ? bones+3*weaponmount::kStride : nullptr;
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
    *r=RoundModel{};r->kind=roundKind;r->style=roundStyle;r->rtti=knownRound ? "test factory" : nullptr;r->lobbed=lobbed;r->alive=At<int>(w,edf::kWeaponAmmoAlive);
    return true;
}
}
namespace edf {
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
    Setup();Zoom();AcquireSRWLockExclusive(&cueLock);cue.at=GetTickCount64()-kCueMs-1;ReleaseSRWLockExclusive(&cueLock);
    Check(SightZoomNow(vehicle)==1,"stale cue rejected");
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
    Button(0);Button(0x80);Check(SightZoomView()==sightzoom::Kind::indirect && toggle.step==0,"overhead is its own fire-control view, never a zoomed scope");
    highActive=false;highReturning=true;Check(SightZoomNow(vehicle)==1,"returning overhead camera is not magnified");
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
    Setup();Zoom();Put<std::uint64_t>(seats,kSeatWeaponCount,2);pickedWeapon=secondWeapon;
    Check(SightZoomNow(vehicle)==1,"new selected weapon cannot inherit old weapon magnification");
    Button(0);Check(toggle.step==0,"new weapon begins at normal field of view");
    Setup();pickedWeapon=secondWeapon;Button(0);Button(0x80);Check(SightZoomNow(vehicle)==1,"stale selection outside this seat does not borrow another gun optic");
    Setup();Zoom();hud=false;Check(Fov()==originalFov,"failed sight renderer cannot leave a zoomed camera");
    Check(sightzoom::MaskOf(sightzoom::Kind::flight)==sightzoom::Mask::none && sightzoom::MaskOf(sightzoom::Kind::mech)==sightzoom::Mask::none,
          "flight and mech fire control never stack a generic scope mask");
    Setup();extraOptic=true;Button(0);Button(0x80);Check(!SightZoomMounted(vehicle) && SightZoomNow(vehicle)==1,"ambiguous same-mount optics fail closed");
    Setup();Zoom();Fov();opticPresent=false;originalWrites=false;Fov();
    Check(std::memcmp(camera+kCamMatrix,thirdPerson,sizeof(thirdPerson))==0 && At<float>(camera,kCamFov)==originalFov,"losing installed lens restores camera and FOV immediately");
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
