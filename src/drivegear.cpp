// The stock ground vehicles' War Thunder cruise gears (drive_gear.h; ini VehicleGears). The player's own driver seat on
// the keyboard only (seat+0x2B0 0; a pad's stick is left as it is): the keys' stick the human wrote this frame
// (docs/heli-input-re.md §4: HumanBase slot 4, W -> seat+0x2C4 negative) shifts the gear, and for the stock input that
// follows (crew.cpp InputHook) the stick is the gear's; after it the seat holds the keys' own again, so the next frame
// shifts on the keys and never on what the gear wrote. Which vehicles: those with CarBase's engine and a class of its
// tracks / wheels / two wheels (vehsound.cpp's BodyOfVehicle, the same names), not the plugin's bodies, not an
// emplacement (it never drives), not the mechs (their W / S walk).
#include "crew.h"
#include "drivegear.h"
#include "drive_gear.h"
#include "heli.h"
#include "layout.h"
#include "map.h"
#include "memory.h"
#include "vecmath.h"
#include "vehmix.h"
#include "vhud.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr std::size_t kSeatLY=0x2C4;
constexpr ULONGLONG kStaleMs=500;     // the vehicle not driven this long: a fresh start (N) next time
constexpr float kSpeedTau=0.15f;      // s: the measured speed's smoothing (the frame's jitter out)
constexpr float kPressed=0.5f;        // the keys' stick past this: W or S held

struct Drive {
    ObjRef vehicle;
    drivegear::State gear;
    float pos[3]{},speed=0.0f,top=0.0f;
    ULONGLONG ms=0,frame=~0ULL;
} drive{};

bool Named(const char* name,std::initializer_list<const char*> names) noexcept {
    if(!name)return false;
    for(const char* n:names)if(!std::strcmp(name,n))return true;
    return false;
}
// Its class's governed top (m/s), 0: no gears for it.
float TopOf(const unsigned char* v) noexcept {
    if(BodyOf(v)!=PluginBody::none || IsHelicopter(v) || IsPlayerJet(v) || IsSub(v) || IsEmplacement(v))return 0.0f;
    const char* const name=VehicleClassName(v);
    if(Named(name,{"403_Tank","404_Tank","505_Tank","510_Maser","601_Tank","603_Flak"}))return vmix::kHeavy.topSpeed;
    if(Named(name,{"402_Rocket","Car"}))return vmix::kLight.topSpeed;
    if(Named(name,{"503_Bike","511_Bike"}))return vmix::kBike.topSpeed;
    return 0.0f;
}

bool KeysAllowed() noexcept {
    if(MapHoldsKeys() || GamePaused())return false;
    DWORD pid=0;GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    return pid==GetCurrentProcessId();
}

// The player's driver seat of `v` on the keyboard, or null.
unsigned char* DriverSeat(unsigned char* v) noexcept {
    if(!Cfg().vehicleGears || v[kDead] || SeatCount(v)==0)return nullptr;
    unsigned char* const seat=SeatAt(v,0);
    const auto human=PlayerHuman();
    if(!human || SeatRider(seat)!=Rider::player || At<const void*>(seat,kSeatRider)!=human)return nullptr;
    return At<unsigned char>(seat,edf::kSeatPad)==0 ? seat : nullptr;
}

// Its speed along its nose (m/s, negative backing), from its position over the game clock.
void Measure(Drive& d,const unsigned char* v,ULONGLONG ms) noexcept {
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    if(ms>d.ms) {
        const float dt=static_cast<float>(ms-d.ms)*0.001f;
        const float moved[3]={p[0]-d.pos[0],p[1]-d.pos[1],p[2]-d.pos[2]};
        const float now=(moved[0]*m[8]+moved[1]*m[9]+moved[2]*m[10])/dt;
        if(std::isfinite(now))d.speed+=(now-d.speed)*vec::Clamp(dt/kSpeedTau,0.0f,1.0f);
    }
    std::memcpy(d.pos,p,12);d.ms=ms;
}
}  // namespace

DriveGearInput::DriveGearInput(unsigned char* v) noexcept {
    __try {
        if(!Cfg().enabled)return;
        unsigned char* const seat=DriverSeat(v);
        const float top=seat ? TopOf(v) : 0.0f;
        if(!seat || top<=0.0f)return;
        const ULONGLONG ms=GameMs();
        if(!drive.vehicle.Is(v) || ms-drive.ms>kStaleMs) {
            drive=Drive{};drive.vehicle=ObjRef::Of(v);drive.top=top;
            std::memcpy(drive.pos,v+kPosition,12);drive.ms=ms;
        }
        const float keys=At<float>(seat,kSeatLY);
        if(drive.frame!=GameFrame()) {
            const ULONGLONG was=drive.ms;
            Measure(drive,v,ms);
            const float dt=was && ms>was ? static_cast<float>(ms-was)*0.001f : 0.0f;
            const int key=!KeysAllowed() || !std::isfinite(keys) ? 0 : keys<-kPressed ? 1 : keys>kPressed ? -1 : 0;
            const int before=drive.gear.gear;
            drivegear::Shift(drive.gear,key,dt);
            if(drive.gear.gear!=before)Log("GEAR v=%p %ls -> %ls at %.0f km/h (top %.0f km/h)",v,drivegear::Name(before),
                                           drivegear::Name(drive.gear.gear),drive.speed*3.6f,top*3.6f);
            drive.frame=GameFrame();
        }
        saved_=keys;seat_=seat;vehicle_=v;ctrl_=At<const void*>(v,kSelfCtrl);
        Put<float>(seat,kSeatLY,drivegear::Drive(drive.gear,drive.speed,drive.top));
    } __except(EXCEPTION_EXECUTE_HANDLER){seat_=nullptr;drive=Drive{};}
}
DriveGearInput::~DriveGearInput() noexcept {
    __try {
        if(seat_ && At<const void*>(vehicle_,kSelfCtrl)==ctrl_)Put<float>(seat_,kSeatLY,saved_);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}

int DriveGearOf(const void* vehicle) noexcept {
    __try {
        if(!Cfg().enabled || !Cfg().vehicleGears || !drive.vehicle.Is(vehicle) || GameMs()-drive.ms>kStaleMs)return kNoDriveGear;
        return drive.gear.gear;
    } __except(EXCEPTION_EXECUTE_HANDLER){return kNoDriveGear;}
}
}  // namespace crew
