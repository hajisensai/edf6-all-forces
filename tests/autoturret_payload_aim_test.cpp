// Exercise the real AutoTurret readers and V4 publication with mounted weapon memory.
// Only the optional cross-DLL PayloadAim boundary and unused tank hook are fixtures.
#include "../autoturret/src/plugin.cpp"
#include "../autoturret/src/designate.cpp"
#include <array>

namespace autoturret {
int HookGunners() noexcept { return 0; }
}

namespace fixture {
using namespace autoturret;
namespace link=edf::aimlink;
enum class Answer { selected,unhandled,empty,badAbi,invalid };
Answer answer=Answer::selected;
const unsigned char* selected=nullptr;
const void* askedVehicle=nullptr;
const void* askedSource=nullptr;
unsigned askedSeat=99;
unsigned queryCount=0;

bool __cdecl Payload(const void* vehicle,unsigned seat,const void* source,link::PayloadAimV1* out) {
    ++queryCount;askedVehicle=vehicle;askedSeat=seat;askedSource=source;
    if(answer==Answer::unhandled)return false;
    if(answer==Answer::badAbi){out->size=sizeof(*out)+8;return true;}
    out->weapon=answer==Answer::empty ? nullptr : answer==Answer::invalid ? reinterpret_cast<const void*>(1) : selected;
    return true;
}

void Identity(void* memory) noexcept {
    float m[16]{};m[0]=m[5]=m[10]=m[15]=1.0f;std::memcpy(memory,m,sizeof(m));
}

struct Weapon {
    std::array<unsigned char,0x1000> bytes{};
    std::array<unsigned char,edf::kMuzzleStride> muzzle{};
    std::array<unsigned char,edf::kBoneRows+0x40> bone{};
    Weapon(float speed,int life,float gravity,std::int32_t mark,float x,float y,float z,float dx,float dy,float dz) noexcept {
        Put<float>(bytes.data(),kAmmoSpeed,speed);Put<std::int32_t>(bytes.data(),kAmmoAlive,life);
        Put<float>(bytes.data(),kAmmoGravity,gravity);Put<std::int32_t>(bytes.data(),kLockonTargetType,mark);
        Identity(bytes.data()+edf::kWeaponMatrix);Identity(muzzle.data()+edf::kMuzzleLocal);Identity(bone.data()+edf::kBoneRows);
        Put<float>(bytes.data(),edf::kWeaponMatrix+0x20,dx);Put<float>(bytes.data(),edf::kWeaponMatrix+0x24,dy);
        Put<float>(bytes.data(),edf::kWeaponMatrix+0x28,dz);
        Put<float>(bone.data(),edf::kBoneRows+0x30,x);Put<float>(bone.data(),edf::kBoneRows+0x34,y);
        Put<float>(bone.data(),edf::kBoneRows+0x38,z);
        Put<void*>(muzzle.data(),0,bone.data());Put<std::int32_t>(muzzle.data(),edf::kMuzzleMode,edf::kModeWeaponRows);
        Put<void*>(bytes.data(),edf::kMuzzles,muzzle.data());Put<std::uint64_t>(bytes.data(),edf::kMuzzleCount,1);
    }
};

const float* __fastcall Gravity(void*) { static const float g[3]={0.0f,-9.8f,0.0f};return g; }
bool Close(float got,float want,float tolerance=0.0001f) noexcept { return std::fabs(got-want)<=tolerance; }
bool Point(const float* got,float x,float y,float z) noexcept { return Close(got[0],x)&&Close(got[1],y)&&Close(got[2],z); }

// Independent constant-velocity intercept: |target + (1,0,0)t - muzzle| = speed*t.
float Intercept(float speed,float x,float y,float z) noexcept {
    const double dx=-static_cast<double>(x),dy=5.0-static_cast<double>(y),dz=207.0-static_cast<double>(z);
    const double a=1.0-static_cast<double>(speed)*speed,b=2.0*dx,c=dx*dx+dy*dy+dz*dz;
    return static_cast<float>((-b-std::sqrt(b*b-4.0*a*c))/(2.0*a));
}
}

int main() {
    using namespace autoturret;
    using namespace fixture;
    int checks=0,failed=0;
    auto check=[&](bool pass,const char* description){++checks;if(!pass){++failed;std::printf("FAIL %s\n",description);}};
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!image)return 2;
    unsigned char gravityWorld[0x80]{},physics[0x40]{};
    void* gravityVtable[]={reinterpret_cast<void*>(&Gravity)};
    Put<void*>(image,0x20B2958,gravityWorld);Put<void*>(gravityWorld,0x68,physics);Put<void*>(physics,0x20,gravityVtable);
    unsigned char vehicle[0x3000]{},seat[edf::kSeatStride]{},vehicleCtrl[16]{},rider[0x400]{},riderCtrl[16]{};
    unsigned char holder[0x20]{},enemy[0x400]{},enemyCtrl[16]{};
    unsigned char* holders[]={holder};
    Identity(vehicle+kMatrix);Put<void*>(vehicle,kSelfCtrl,vehicleCtrl);Put<int>(vehicleCtrl,edf::kCtrlUses,1);
    Put<void*>(vehicle,edf::kSeats,seat);Put<std::uint64_t>(vehicle,edf::kSeatCount,1);
    Put<void*>(seat,kSeatWeapons,holders);Put<std::uint64_t>(seat,kSeatWeaponCount,1);
    Put<void*>(seat,edf::kSeatRider,rider);Put<void*>(seat,edf::kSeatRiderCtrl,riderCtrl);
    Put<int>(riderCtrl,edf::kCtrlUses,1);rider[edf::kHumanPlayer]=1;Put<void*>(rider,edf::kHumanPad,rider);
    Put<void*>(enemy,kSelfCtrl,enemyCtrl);Put<int>(enemyCtrl,edf::kCtrlUses,1);
    Weapon native(8.0f,60,0.25f,kMarkAir,1,2,3,0,0,1);
    Weapon ap(16.0f,90,0.5f,0,2,4,6,0.6f,0,0.8f);
    Weapon he(5.0f,120,1.25f,0,-3,5,7,0,0.6f,0.8f);
    Put<void*>(holder,kHolderWeapon,native.bytes.data());
    payloadAim=&Payload;selected=ap.bytes.data();
    cfg.enabled=true;cfg.autoAim=1;cfg.lockKey=cfg.lockButton=0;   // auto-aim on (off by default since 2026-10-25)
    Shot shot{};float range=0.0f;
    check(GunMark(ap.bytes.data())==Mark::none && GunMark(he.bytes.data())==Mark::none,"AP/HE fixtures carry no AutoTurret marker");
    check(ReadShot(vehicle,seat,shot,range) && Close(shot.speed,16) && Close(range,1440),"marked native gun admits unmarked selected AP speed and lifetime");
    check(Close(shot.drop,0.5f*9.8f/kFramesPerSecondSq) && !shot.ground && !shot.lofted,"selected AP supplies real gravity while native air role remains the eligibility profile");
    check(askedVehicle==vehicle && askedSeat==0 && askedSource==native.bytes.data(),"ReadShot asks Crew for this vehicle and native trigger source");
    selected=he.bytes.data();
    check(ReadShot(vehicle,seat,shot,range) && Close(shot.speed,5) && Close(range,600),"unmarked selected HE replaces AP speed and lifetime");
    check(Close(shot.drop,1.25f*9.8f/kFramesPerSecondSq),"selected HE supplies its own gravity factor");
    answer=Answer::unhandled;
    check(ReadShot(vehicle,seat,shot,range) && Close(shot.speed,8) && Close(range,480) && Close(shot.drop,0.25f*9.8f/kFramesPerSecondSq),"unhandled payload query retains complete native ballistics");
    answer=Answer::badAbi;
    check(ReadShot(vehicle,seat,shot,range) && Close(shot.speed,8) && Close(range,480),"incompatible optional ABI preserves native behavior");
    answer=Answer::empty;
    check(!ReadShot(vehicle,seat,shot,range),"handled empty trigger group never restores a native round");
    answer=Answer::invalid;
    check(!ReadShot(vehicle,seat,shot,range),"handled unreadable selected weapon never restores a native round");
    payloadAim=nullptr;payloadTried=GetTickCount64();
    check(ReadShot(vehicle,seat,shot,range) && Close(shot.speed,8) && Close(range,480),"missing peer export retains standalone native ballistics");
    payloadAim=&Payload;answer=Answer::selected;selected=ap.bytes.data();
    const unsigned beforeUnmarked=queryCount;Put<std::int32_t>(native.bytes.data(),kLockonTargetType,0);
    check(ReadShot(vehicle,seat,shot,range) && Close(shot.speed,16) && Close(range,1440) && queryCount==beforeUnmarked+1,"verified Crew-managed controller admits mark-0 native and selected weapons");
    answer=Answer::unhandled;
    check(!ReadShot(vehicle,seat,shot,range),"unhandled query leaves an unmarked standalone stock turret unarmed");
    answer=Answer::selected;Put<std::int32_t>(native.bytes.data(),kLockonTargetType,kMarkAir);
    const Barrel apBarrel=SeatBarrel(vehicle,seat);
    check(apBarrel.ok && apBarrel.weapon==ap.bytes.data() && Close(apBarrel.life,90),"SeatBarrel identifies selected AP and its actual lifetime");
    check(Point(apBarrel.pos,2,4,6) && Point(apBarrel.dir,0.6f,0,0.8f),"SeatBarrel reconstructs selected AP muzzle and bore from its own bone");
    selected=he.bytes.data();const Barrel heBarrel=SeatBarrel(vehicle,seat);
    check(heBarrel.ok && heBarrel.weapon==he.bytes.data() && Close(heBarrel.life,120) && Point(heBarrel.pos,-3,5,7) && Point(heBarrel.dir,0,0.6f,0.8f),"SeatBarrel switches to selected HE muzzle, bore and lifetime");
    answer=Answer::empty;const Barrel emptyBarrel=SeatBarrel(vehicle,seat);
    check(!emptyBarrel.ok && !emptyBarrel.weapon,"handled empty group has no native barrel fallback");
    answer=Answer::unhandled;const Barrel nativeBarrel=SeatBarrel(vehicle,seat);
    check(nativeBarrel.ok && nativeBarrel.weapon==native.bytes.data() && Point(nativeBarrel.pos,1,2,3) && Close(nativeBarrel.life,60),"unhandled barrel query retains the native holder muzzle");

    // The input hook has already seen this vehicle: every V4 request below really is the same frame.
    frame=100;frameSeenCount=0;exportSeen=nullptr;SeeVehicle(vehicle);
    pilot=Pilot{};pilot.vehicle=vehicle;pilot.ctrl=vehicleCtrl;pilot.seat=0;pilot.seenFrame=Frame();pilot.range=2000;pilot.keys=true;
    pilot.target=enemy;pilot.targetCtrl=enemyCtrl;pilot.lockedAt=GetTickCount64();pilot.lockVel[0]=1;
    worldFrame=Frame();worldCount=1;worldEnemies[0]=Enemy{};worldEnemies[0].object=enemy;
    worldEnemies[0].pos[1]=5;worldEnemies[0].pos[2]=207;
    Put<float>(ap.bytes.data(),kAmmoGravity,0);Put<float>(he.bytes.data(),kAmmoGravity,0);
    Put<std::int32_t>(he.bytes.data(),kAmmoAlive,5); // valid muzzle, but the HE round expires before this target
    const auto nativeBefore=native.bytes,apBefore=ap.bytes,heBefore=he.bytes;
    const ULONGLONG sameFrame=Frame();
    const float target[3]={0,5,207};
    PublishAim(vehicle,true,enemy,target,native.bytes.data());
    answer=Answer::selected;selected=ap.bytes.data();
    edf::aimlink::PlayerAimV4 out{};edf::aimlink::TurretReadoutV1 apReadout{},heReadout{},backReadout{};
    check(EDF6AutoTurret_PlayerAimV4(vehicle,0,native.bytes.data(),&out) && out.steer,"V4 resolves selected AP even when caller supplies its native source");
    const float apFrames=Intercept(16,2,4,6);
    check(Close(out.point[0],apFrames,0.002f) && Close(out.point[1],5) && Close(out.point[2],207),"V4 AP steering uses an independently computed 16 m/frame intercept");
    check(EDF6AutoTurret_TurretReadoutV1(&apReadout) && apReadout.target && apReadout.ownGun,"V4 selected AP replaces earlier native HUD publication in the same frame");
    check(publishedGun==ap.bytes.data() && Frame()==sameFrame,"AP override changes published gun without inventing another frame");
    selected=he.bytes.data();out={};
    check(EDF6AutoTurret_PlayerAimV4(vehicle,0,native.bytes.data(),&out) && !out.steer,"same-frame HE selection honors its real short lifetime");
    const float heFrames=Intercept(5,-3,5,7);
    check(Close(out.point[0],heFrames,0.002f),"same-frame HE solve uses 5 m/frame instead of the prior AP or native speed");
    check(EDF6AutoTurret_TurretReadoutV1(&heReadout) && heReadout.target && publishedGun==he.bytes.data(),"same-frame gun identity change republishes for HE");
    check(heFrames>apFrames*2 && Frame()==sameFrame,"HE solve is distinct from AP while the game frame remains unchanged");
    selected=ap.bytes.data();out={};
    check(EDF6AutoTurret_PlayerAimV4(vehicle,0,native.bytes.data(),&out) && out.steer && Close(out.point[0],apFrames,0.002f),"same-frame return from HE to AP restores the AP steering solution");
    check(EDF6AutoTurret_TurretReadoutV1(&backReadout) && backReadout.target && publishedGun==ap.bytes.data(),"returning to a previous gun is not blocked by a once-per-frame publication gate");
    answer=Answer::empty;out={};
    check(EDF6AutoTurret_PlayerAimV4(vehicle,0,native.bytes.data(),&out) && !out.steer && !publishedGun,"same-frame empty selection clears the prior gun instead of steering native");
    check(EDF6AutoTurret_TurretReadoutV1(&backReadout) && !backReadout.lead && backReadout.mode==edf::aimlink::Mode::autoAim,"handled empty selection publishes no lead (the one auto-aim's readout)");
    answer=Answer::unhandled;out={};
    check(EDF6AutoTurret_PlayerAimV4(vehicle,0,native.bytes.data(),&out) && out.steer && publishedGun==native.bytes.data(),"unhandled V4 query preserves native standalone steering");
    check(native.bytes==nativeBefore && ap.bytes==apBefore && he.bytes==heBefore,"aim readers never rewrite native or selected weapon state");
    check(Frame()==sameFrame,"all V4 switch and return checks share one actual input-hook frame");
    std::printf("autoturret payload aim: %d checks, %d failures\n",checks,failed);
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;return failed?1:0;
}
