// Live-layout fixture: the embedded object's vtable must never become the aim
// buffer, and a paired cannon must map the current frame's angles before fire.
#include "../src/seat_aim.h"
#include <cstdio>
#include <limits>

int main() {
    alignas(16) unsigned char seats[2][edf::kSeatStride]{};
    const std::uintptr_t table[3]={0x11111111,0x22222222,0x33333333};
    for(auto& seat:seats)edf::Put<const void*>(seat,0xE0,table);
    auto left=crew::seataim::Object(seats[0]);
    auto right=crew::seataim::Object(seats[1]);
    if(left!=seats[0]+0xE0 || right!=seats[1]+0xE0 || edf::At<const void*>(left,0)!=table)return 1;
    // Two stock steps have already run; the left moved this frame, while the
    // now-empty right seat drifted under its old angular velocity.
    edf::Put<float>(seats[0],0xF8,0.7f);edf::Put<float>(seats[0],0x138,-0.9f);
    edf::Put<float>(seats[1],0xF0,-1.0f);edf::Put<float>(seats[1],0xF4,1.0f);
    edf::Put<float>(seats[1],0x130,-0.5f);edf::Put<float>(seats[1],0x134,0.5f);
    edf::Put<float>(seats[1],0xF8,-0.4f);edf::Put<float>(seats[1],0xFC,0.2f);
    float mapped[2]={};int calls=0;
    crew::seataim::Follow(left,right,[&](unsigned char* axis) noexcept {
        mapped[calls++]=edf::At<float>(axis,8);
    });
    if(calls!=2 || mapped[0]!=0.7f || mapped[1]!=-0.5f || edf::At<float>(seats[1],0xFC)!=0.0f)return 2;
    if(edf::At<const void*>(right,0)!=table || table[0]!=0x11111111)return 3;
    // Invalid source data must leave both the destination angle and bone alone.
    edf::Put<float>(seats[0],0xF8,std::numeric_limits<float>::quiet_NaN());
    calls=0;
    crew::seataim::Follow(left,right,[&](unsigned char*) noexcept {++calls;});
    if(calls!=1 || edf::At<float>(seats[1],0xF8)!=0.7f)return 4;
    std::puts("seat_aim_check: embedded objects, clamped same-frame bone mapping, vtable preservation and invalid input passed");
    return 0;
}
