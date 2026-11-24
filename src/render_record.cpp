// The render recording's single owner (render_record.h). The call 0x1198FA4 in System::Update 0x1198DA0 calls
// System_RecordDraw 0x11978D0 once a frame, after the frame's object deletion 0x1195F20 and before the tick loop
// (n = 0: 0x1198FB3 skips the loop); the render thread is idle there (the last frame's end fence 0x11A305A waited for it).
//
// The recording first runs vtable slot 3 of every object in System's +0x428 list (the loop at 0x1197950). A soldier's
// 0x59A780 -> 0x5750D0 -> 0x6C06A0(h+0x860) copies each bone record's world over the skeleton's pose array
// (0x1100F10(h+0x900)) and places the model's Umbra box again from its own bounds, 0x11B30C0(model,model+0x110); then
// the +0x358 jobs (0x1197A0B); then each viewport's projection 0x1197B50 (-> 0x118DFF0) and its draws. So a pose or an
// Umbra box written before the recording is overwritten before it is drawn: M001 in the first person, FrameRateCap=1
// (2026-11-24): on every one of 18000 frames the first person's body and weapon boxes, placed before the recording,
// were placed again at 0x6C06F7 from 0x1197959. What the frame draws is written at the recording's first projection
// instead, after that loop and before the first draw.
//
// Only one module may redirect a call site (edf::RedirectCall refuses a site that no longer calls the stock function:
// the second module would be left out, as the first person's culling was when the frame rate was on: "FP installed:
// ... culling=0 pass=0"), and the projection's call is scope_lens.cpp's (one callback, SetBeforeProject). Both are
// taken here; the features register turns, run in Slot order:
//  1. The unlimited frame rate (frame_rate_view.cpp BeforeProject): at the first projection, the other objects sampled
//     once a tick (frame_rate_objects.cpp) and, on a frame without ticks, the player's root, then every other object,
//     moved to the blend between the last two ticks (bone poses only); at each projection its camera's blend.
//  2. The first person (firstperson.cpp): before the recording its session (RenderStart); at the first projection the
//     viewmodel's Umbra boxes around the eye (FirstProjection). The eye is the one this frame draws: on a frame without
//     ticks the frame rate's blend (crew::FrameRateDrawnEye, its camera already written in turn 1 when it is the first
//     projection's), not the step's: Umbra culls from the drawn camera, and a box left at the step's eye can miss a
//     camera a part of a tick away.
// Each runs whether the other is installed or not (a feature off leaves its slot empty). The recording is open from the
// call to its return: a recording without a projection (loading) leaves no first for a projection by another caller of
// 0x11978D0.
#include "render_record.h"
#include "crew.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include "scope_lens.h"
#include <cstdint>

namespace crew {
namespace {
constexpr unsigned kRecordCall=0x1198FA4,kRecordDraw=0x11978D0;
// mov r8b,1; movzx edx,byte ptr [r14+0CE9h]; mov rcx,r14; call 0x11978D0
const unsigned char kRecordSig[]={0x41,0xB0,0x01,0x41,0x0F,0xB6,0x96,0xE9,0x0C,0x00,0x00,0x49,0x8B,0xCE,
                                  0xE8,0x27,0xE9,0xFF,0xFF};
renderrecord::Before recordBefores[static_cast<int>(renderrecord::Slot::kCount)]{};
renderrecord::Project recordProjects[static_cast<int>(renderrecord::Slot::kCount)]{};
bool recordOpen=false;   // from the recording's call to its return, until its first projection
int recordState=0;   // 0 not tried, 1 in, -1 refused

// System_RecordDraw's call; rdx / r8 passed through whole, its result too.
using RecordDrawFn=void*(*)(void*,std::uint64_t,std::uint64_t);
void* RecordHook(void* sys,std::uint64_t a,std::uint64_t b) {
    for(const renderrecord::Before before:recordBefores)
        if(before)before();
    recordOpen=true;
    void* const r=reinterpret_cast<RecordDrawFn>(image+kRecordDraw)(sys,a,b);
    recordOpen=false;
    return r;
}

// Each projection 0x1197B50 (scope_lens.cpp's hook calls it first): the recording's first, once.
void ProjectHook(void* camera,const int* rect) {
    const bool first=recordOpen;
    recordOpen=false;
    for(const renderrecord::Project project:recordProjects)
        if(project)project(camera,rect,first);
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
    if(!scopelens::SetBeforeProject(&ProjectHook)) {
        Log("RENDER the projection call 0x1197B50 could not be shared: nothing hooked at the render recording");
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

void SetProject(Slot slot,Project project) noexcept {
    const int i=static_cast<int>(slot);
    if(recordState>0 && i>=0 && i<static_cast<int>(Slot::kCount))recordProjects[i]=project;
}
}  // namespace renderrecord
}  // namespace crew
