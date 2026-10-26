// The stock ground vehicles' War Thunder cruise gears (drive_gear.h; ini VehicleGears). The player's own driver seat on
// the keyboard only (seat+0x2B0 0; a pad's stick is left as it is): the keys' stick the human wrote this frame
// (docs/heli-input-re.md §4: HumanBase slot 4, W -> seat+0x2C4 negative) shifts the gear, and for the stock input that
// follows (crew.cpp InputHook) the stick is the gear's; after it the seat holds the keys' own again, so the next frame
// shifts on the keys and never on what the gear wrote. Which vehicles: those with CarBase's engine and a class of its
// tracks / wheels / two wheels (vehsound.cpp's BodyOfVehicle, the same names), not the plugin's bodies, not an
// emplacement (it never drives), not the mechs (their W / S walk). Each one's gears are shares of its own top: the
// native drive's top gear speed as its setup left it (docs/vehicle-top-speed-re.md); a vehicle whose gear table cannot
// be read gets no gears (one log line), never a stand-in speed.
#include "crew.h"
#include "drivegear.h"
#include "drive_gear.h"
#include "heli.h"
#include "layout.h"
#include "map.h"
#include "memory.h"
#include "vecmath.h"
#include "vhud.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr std::size_t kSeatLY=0x2C4;
constexpr ULONGLONG kStaleMs=500;     // the vehicle not driven this long: a fresh start (N) next time
constexpr float kSpeedTau=0.15f;      // s: the measured speed's smoothing (the frame's jitter out)
constexpr float kPressed=0.5f;        // the keys' stick past this: W or S held
// CarBase's forward gear table (docs/vehicle-top-speed-re.md; tests/drive_gear_native_audit.py): a vector of 16-byte rows
// {speed m/s, up-shift speed, down-shift speed, drive force}, begin at +0x1760, row count (64-bit) at +0x1770. The
// setup (slot 46: TankBase 0x67FB00 / BikeBase 0x658F10 / Car 0x65A910) scales every row by setup[1][1] (0x66E1A0), so
// the last row's speed is this vehicle's own top: the native getter 0x66CF00 reads the same row.
constexpr std::size_t kGearRows=0x1760,kGearCount=0x1770,kGearRow=0x10;

struct Drive {
    ObjRef vehicle;
    drivegear::State gear;
    float pos[3]{},speed=0.0f,top=0.0f;
    ULONGLONG ms=0,frame=~0ULL;
} drive{};
ObjRef untabled;   // the last vehicle logged for an unreadable gear table (one line per vehicle, not per frame)

bool Named(const char* name,std::initializer_list<const char*> names) noexcept {
    if(!name)return false;
    for(const char* n:names)if(!std::strcmp(name,n))return true;
    return false;
}
// A stock ground vehicle the gears are for.
bool Geared(const unsigned char* v) noexcept {
    if(BodyOf(v)!=PluginBody::none || IsHelicopter(v) || IsPlayerJet(v) || IsSub(v) || IsEmplacement(v))return false;
    return Named(VehicleClassName(v),{"403_Tank","404_Tank","505_Tank","510_Maser","601_Tank","603_Flak","402_Rocket","Car",
                                      "607_RoboTruck","60X_Truck","507_Rescuetank","503_Bike","511_Bike"});
}
// Its own governed top (m/s): its native top gear's speed, 0: no usable table (`why`: what of it).
float TopOf(const unsigned char* v,const char** why) noexcept {
    *why="unreadable";
    if(!Readable(v+kGearRows,kGearCount+8-kGearRows))return 0.0f;
    const auto* const rows=At<const unsigned char*>(v,kGearRows);
    const std::size_t count=At<std::size_t>(v,kGearCount);
    *why="has no rows or too many";
    if(count==0 || count>drivegear::kMaxGearRows)return 0.0f;
    const unsigned char* const last=rows+(count-1)*kGearRow;
    *why="unreadable";
    if(!Readable(last,sizeof(float)))return 0.0f;
    *why="ends in no forward speed";
    return drivegear::TopOfGears(count,At<float>(last,0));
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
        if(!seat || !Geared(v))return;
        const ULONGLONG ms=GameMs();
        if(!drive.vehicle.Is(v) || ms-drive.ms>kStaleMs) {   // the table is read here: written once, at its setup
            const char* why=nullptr;
            const float top=TopOf(v,&why);
            if(top<=0.0f) {
                drive=Drive{};
                if(!untabled.Is(v)){untabled=ObjRef::Of(v);Log("GEAR v=%p %s: its gear table (+0x%zX) %s, no gears for it",
                                                              v,VehicleClassName(v),kGearRows,why);}
                return;
            }
            if(!drive.vehicle.Is(v))Log("GEAR v=%p %s: its top %.1f m/s (%.0f km/h), from its gear table",v,VehicleClassName(v),top,
                                        top*3.6f);
            drive=Drive{};drive.vehicle=ObjRef::Of(v);drive.top=top;
            std::memcpy(drive.pos,v+kPosition,12);drive.ms=ms;
        }
        const float top=drive.top;
        const float keys=At<float>(seat,kSeatLY);
        if(drive.frame!=GameFrame()) {
            const ULONGLONG was=drive.ms;
            Measure(drive,v,ms);
            const float dt=was && ms>was ? static_cast<float>(ms-was)*0.001f : 0.0f;
            const int key=!KeysAllowed() || !std::isfinite(keys) ? 0 : keys<-kPressed ? 1 : keys>kPressed ? -1 : 0;
            const int before=drive.gear.gear;
            drivegear::Shift(drive.gear,key,dt);
            if(drive.gear.gear!=before)Log("GEAR v=%p %ls -> %ls at %.0f km/h (its top %.0f km/h)",v,drivegear::Name(before),
                                           drivegear::Name(drive.gear.gear),drive.speed*3.6f,top*3.6f);
            drive.frame=GameFrame();
        }
        saved_=keys;seat_=seat;vehicle_=v;ctrl_=At<const void*>(v,kSelfCtrl);
        Put<float>(seat,kSeatLY,drivegear::Drive(drive.gear,drive.speed,drive.top));
    } __except(EXCEPTION_EXECUTE_HANDLER){seat_=nullptr;drive=Drive{};}
}
DriveGearInput::~DriveGearInput() noexcept {
    __try {
        // A seat switch this frame zeroed the seat the player left: hand the keys back only to a seat he still drives.
        const auto human=PlayerHuman();
        if(seat_ && At<const void*>(vehicle_,kSelfCtrl)==ctrl_ && human && SeatRider(seat_)==Rider::player &&
           At<const void*>(seat_,kSeatRider)==human)Put<float>(seat_,kSeatLY,saved_);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}

int DriveGearOf(const void* vehicle) noexcept {
    __try {
        if(!Cfg().enabled || !Cfg().vehicleGears || !drive.vehicle.Is(vehicle) || GameMs()-drive.ms>kStaleMs)return kNoDriveGear;
        return drive.gear.gear;
    } __except(EXCEPTION_EXECUTE_HANDLER){return kNoDriveGear;}
}
}  // namespace crew
