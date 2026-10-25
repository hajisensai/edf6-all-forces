// The stock ground vehicles' War Thunder cruise gears on the keyboard (drive_gear.h; ini VehicleGears): for one input
// traversal the player driver's left stick Y (seat+0x2C4) is the gear's, the keys' own given back after it.
#pragma once
namespace crew {
class DriveGearInput {
public:
    explicit DriveGearInput(unsigned char* vehicle) noexcept;
    ~DriveGearInput() noexcept;
    DriveGearInput(const DriveGearInput&)=delete;
    DriveGearInput& operator=(const DriveGearInput&)=delete;
private:
    unsigned char* vehicle_=nullptr;
    unsigned char* seat_=nullptr;
    const void* ctrl_=nullptr;
    float saved_=0.0f;
};
constexpr int kNoDriveGear=-99;
// The gear the player drives `vehicle` in (drive_gear.h: -1 R, 0 N, 1..4), kNoDriveGear when none (not driving it on the
// keyboard, a vehicle without gears, VehicleGears off). Game thread.
int DriveGearOf(const void* vehicle) noexcept;
}  // namespace crew
