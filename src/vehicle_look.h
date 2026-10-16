// Hold-to-look in any local vehicle seat. Only the seat's look axes are borrowed,
// for one input traversal; throttle, steering, triggers and the source input are preserved.
#pragma once
namespace crew {
class VehicleLookInput {
public:
    explicit VehicleLookInput(unsigned char* vehicle) noexcept;
    ~VehicleLookInput() noexcept;
    VehicleLookInput(const VehicleLookInput&)=delete;
    VehicleLookInput& operator=(const VehicleLookInput&)=delete;
private:
    unsigned char* vehicle_=nullptr;
    unsigned char* seat_=nullptr;
    const void* ctrl_=nullptr;
    float saved_[2]{};
};
void VehicleLookSetReady(bool ready) noexcept; // camera hook availability; never consume input without it
bool VehicleLookActive(const void* vehicle) noexcept;
bool VehicleLookCamera(const unsigned char* human,float* matrix) noexcept;
bool VehicleLookHint(bool* keys) noexcept;
}  // namespace crew
