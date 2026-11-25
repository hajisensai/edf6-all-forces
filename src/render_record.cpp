// The render recording's single owner (render_record.h). The call 0x1198FA4 in System::Update 0x1198DA0 calls
// System_RecordDraw 0x11978D0 once a frame, after the frame's object deletion 0x1195F20 and before the tick loop
// (n = 0: 0x1198FB3 skips the loop); the render thread is idle there (the last frame's end fence 0x11A305A waited for it).
//
// Several features act there, and only one module may redirect a call site (edf::RedirectCall refuses a site that no longer
// calls the stock function: the second module would be left out, as the first person's culling was when the frame rate
// was on: "FP installed: ... culling=0 pass=0"). They run in this order:
//  1. The unlimited frame rate (frame_rate_view.cpp RecordStart): it only opens the recording. Its first projection
//     (0x1197B50, after the recording copied the skeletons' poses at 0x1197950) samples the other objects once a tick
//     (frame_rate_objects.cpp) and, on a frame without ticks, moves the player's root, then every other object, to the
//     blend between the last two ticks. Bone poses only: the cameras and the models' Umbra boxes
//     (model+0x110) are not touched.
//  2. View distance reapplies the active clip planes before the cameras build their render frusta.
//  3. The first person (firstperson.cpp RenderStart): the viewmodel's Umbra boxes around the eye. The eye is the one
//     this frame's projection draws (inside the render recording, 0x1197B50: on a frame without ticks the frame rate's
//     blend, crew::FrameRateDrawnEye), not the step's: Umbra culls from the drawn camera, and a box left at the step's eye
//     can miss a camera a part of a tick away. Last, so it sees the frame as it will be drawn.
// Each runs whether the other is installed or not (a feature off leaves its slot empty).
#include "render_record.h"
#include "crew.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <cstdint>

namespace crew {
namespace {
constexpr unsigned kRecordCall=0x1198FA4,kRecordDraw=0x11978D0;
// mov r8b,1; movzx edx,byte ptr [r14+0CE9h]; mov rcx,r14; call 0x11978D0
const unsigned char kRecordSig[]={0x41,0xB0,0x01,0x41,0x0F,0xB6,0x96,0xE9,0x0C,0x00,0x00,0x49,0x8B,0xCE,
                                  0xE8,0x27,0xE9,0xFF,0xFF};
renderrecord::Before recordBefores[static_cast<int>(renderrecord::Slot::kCount)]{};
int recordState=0;   // 0 not tried, 1 in, -1 refused

// System_RecordDraw's call; rdx / r8 passed through whole, its result too.
using RecordDrawFn=void*(*)(void*,std::uint64_t,std::uint64_t);
void* RecordHook(void* sys,std::uint64_t a,std::uint64_t b) {
    for(const renderrecord::Before before:recordBefores)
        if(before)before();
    return reinterpret_cast<RecordDrawFn>(image+kRecordDraw)(sys,a,b);
}
}  // namespace

namespace renderrecord {
bool Install() noexcept {
    if(recordState)return recordState>0;
    recordState=-1;
    if(!edf::Matches(image,kRecordCall-14,kRecordSig,sizeof(kRecordSig))) {
        Log("RENDER the render recording's call (0x%X) is not where docs/unlimited-fps-re.md has it: nothing hooked there",
            kRecordCall);
        return false;
    }
    bool changed=false;
    if(!edf::RedirectCall(image+kRecordCall,image+kRecordDraw,reinterpret_cast<void*>(&RecordHook),changed)) {
        Log("RENDER the render recording's call (0x%X) could not be redirected: nothing hooked there",kRecordCall);
        return false;
    }
    recordState=1;
    return true;
}

void Set(Slot slot,Before before) noexcept {
    const int i=static_cast<int>(slot);
    if(recordState>0 && i>=0 && i<static_cast<int>(Slot::kCount))recordBefores[i]=before;
}
}  // namespace renderrecord
}  // namespace crew
