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
// The look applied to the camera `matrix` (rows left, up, ahead, eye) of `human`'s seat. `orbit` false (a first-person
// eye): the head turns where it is. `orbit` true (the third-person view, War Thunder's: the user, 2026-10-21, "鼠标中键的
// 时候应该是绕着车转，而不是车后面原地转"): the camera swings round the vehicle at its distance (OrbitPivot), the point it
// looked at staying in the middle of the view.
bool VehicleLookCamera(const unsigned char* human,float* matrix,bool orbit=false) noexcept;
// The orbit's pivot in the camera's own frame (x left, y up, z ahead): the point on the view's axis as far ahead as the
// vehicle's `centre` (world) is; false when the vehicle is not ahead of the eye (then the head turns in place).
inline bool OrbitPivot(const float* matrix,const float* centre,float* pivot) noexcept {
    const float d[3]={centre[0]-matrix[12],centre[1]-matrix[13],centre[2]-matrix[14]};
    const float ahead=d[0]*matrix[8]+d[1]*matrix[9]+d[2]*matrix[10];
    pivot[0]=pivot[1]=0.0f;pivot[2]=ahead;
    return ahead>0.0f && ahead<1.0e4f;
}
bool VehicleLookHint(bool* keys) noexcept;
}  // namespace crew
