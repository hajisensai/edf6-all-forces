// EDF4.1's 406 Begaruta (BM03 Vegalta, pylib/edf41mech.py) crashed the game on its first jump (the user, 2026-10-25:
// 「维加泰跳跃会炸游戏」; live the same day: the test range's debug spawn VEHICLE406_BEGARUTA, boarded, one jump:
// C0000005 at EDF+0x64833E writing 0x3EC, stack 0x64833E <- 0x638D22 <- 0x6443CA).
//
// The 406 keeps EDF6's retained VehicleBegaruta class and 4.1's `begaruta_boost` key, which the class's setup hands to
// its old boost parser (0x636DCE -> 0x640340; `begaruta_boost6` goes to 0x63DEC0, `begaruta_boost5` to 0x63CEC0). The
// vehicle's jet group (vehicle+0x1BB0) has four slots (+0x20 + i*0xA0, the emitter at +0x50). EDF6's own parsers fill
// one slot per bone (the 504's and the Nix's four); the old parser's loop builds two, one per group (0x640CB3
// cmp rsi,2), and leaves slots 2 and 3 with a null emitter. Two loops of the update write through every slot's emitter
// unchecked: the group's state step 0x6482A0 (called at 0x638CE7 / 0x638D1D when a boost starts) and the boost stop at
// 0x638EF0. The 406's first jump writes through null. No EDF6 object uses the old key (every 504 / 608 / 612 SGO has
// boost5 or boost6), so the game's own data never ran that path; the dump of the crash shows slots 0 / 1 built, 2 / 3
// null.
//
// The two loops are given the old parser's contract: a slot whose emitter was never built is no thruster, and its
// emitter write is skipped. Each loop body jumps to a stub that runs the same instructions with the emitter's write
// behind a null test, then returns past the body (src/edf41mech_boost.h). A built slot (every slot of every EDF6
// object) runs exactly the stock instructions; the 406 keeps 4.1's two burner groups as the old parser builds them.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46 (tests/edf41mech_boost_native_audit.py).
#include "crew.h"
#include "memory.h"
#include "edf41mech_boost.h"
#include "edf/patch.h"

#include <cstdint>
#include <cstring>

namespace crew {
namespace {
using namespace mechboost;

// The stub near the body (a rel32 jump's reach), then the body replaced by the jump to it; all or nothing per site.
bool Divert(std::size_t rva,const unsigned char* stock,std::size_t size,const unsigned char* stub,std::size_t stubSize) noexcept {
    void* const code=AllocateNearCode(image+rva,stub,stubSize);
    if(!code)return false;
    unsigned char patch[32];
    if(size>sizeof(patch) || !Jump(reinterpret_cast<std::uint64_t>(image+rva),reinterpret_cast<std::uint64_t>(code),size,patch) ||
       !edf::PatchCode(image+rva,stock,patch,size)) {
        VirtualFree(code,0,MEM_RELEASE);
        return false;
    }
    return true;
}
}  // namespace

bool InstallEdf41MechBoost() noexcept {
    __try {
        if(!Matches(kStepWrites,kStepWritesCode,sizeof(kStepWritesCode)) || !Matches(kStopWrites,kStopWritesCode,sizeof(kStopWritesCode)) ||
           !Matches(kStepLoop,kStepLoopCode,sizeof(kStepLoopCode)) || !Matches(kStopLoop,kStopLoopCode,sizeof(kStopLoopCode)) ||
           !Matches(kOldLoop,kOldLoopCode,sizeof(kOldLoopCode))) {
            Log("EDF41MECH boost: EDF+%zX / +%zX are not the jet group's loops as read: not patched",kStepWrites,kStopWrites);
            return false;
        }
        unsigned char step[kStepStubSize],stop[kStopStubSize];
        StepStub(reinterpret_cast<std::uint64_t>(image+kStepNext),step);
        StopStub(reinterpret_cast<std::uint64_t>(image+kStopNext),stop);
        const bool stepOk=Divert(kStepWrites,kStepWritesCode,sizeof(kStepWritesCode),step,sizeof(step));
        const bool stopOk=stepOk && Divert(kStopWrites,kStopWritesCode,sizeof(kStopWritesCode),stop,sizeof(stop));
        if(stepOk && !stopOk) {   // one loop guarded and not the other is no fix: the step goes back to the game's code
            unsigned char now[sizeof(kStepWritesCode)];
            std::memcpy(now,image+kStepWrites,sizeof(now));
            edf::PatchCode(image+kStepWrites,now,kStepWritesCode,sizeof(now));
        }
        Log("EDF41MECH boost: the jet group's state step and boost stop skip the slots the old begaruta_boost parser leaves "
            "unbuilt: %s",stepOk && stopOk ? "on" : "NOT patched");
        return stepOk && stopOk;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("EDF41MECH boost: fault while patching: not patched");
        return false;
    }
}
}  // namespace crew
