// The module registry's rules (src/module_logic.h) on tables made here: the reset rule (a stateful module with no
// reset, a stateless one with a reset or with an empty reason, a module reset twice), the phases' order, the ids'
// range, a module in no pass. The real tables are held to the same rules at compile time (src/module_table.h
// static_asserts); tests/module_registry_guard.py reads them again from the source. And the fault policy (FaultStreak).
// No EDF.dll, no game.
#include "../src/module_logic.h"
#include <cstdio>

namespace {
using namespace crew::modules;
enum class Id : unsigned short { kA, kB, kC, kCount };
struct Row { Id id; const char* name; const char* stateless; };
struct Entry { ResetPhase phase; Id id; void (*run)() noexcept; };
void Run() noexcept {}

int cases=0,failed=0;
void Check(bool ok,const char* message) noexcept {
    ++cases;
    if(!ok){++failed;std::printf("FAIL %s\n",message);}
}

constexpr Row kRows[]={{Id::kA,"a",nullptr},{Id::kB,"b","keeps nothing"},{Id::kC,"c",nullptr}};
constexpr std::size_t kN=static_cast<std::size_t>(Id::kCount);
constexpr Entry kGood[]={{ResetPhase::kReleaseHolds,Id::kC,&Run},{ResetPhase::kMissionState,Id::kA,&Run}};
// Held at compile time too: the rules are constexpr (the plugin's static_asserts need that).
static_assert(FirstResetBreak(kRows,kGood)==kN,"a stateful module each reset once, the stateless one not");
static_assert(RowsInIdOrder(kRows) && PhasesInOrder(kGood) && IdsInRange(kGood,kN),"the rules are constexpr");

void ResetRule() noexcept {
    Check(FirstResetBreak(kRows,kGood)==kN,"every stateful module reset once: no break");
    constexpr Entry noC[]={{ResetPhase::kMissionState,Id::kA,&Run}};
    Check(FirstResetBreak(kRows,noC)==2,"a stateful module with no reset: the rule names it");
    constexpr Entry twice[]={{ResetPhase::kReleaseHolds,Id::kC,&Run},{ResetPhase::kMissionState,Id::kA,&Run},
                             {ResetPhase::kMissionState,Id::kA,&Run}};
    Check(FirstResetBreak(kRows,twice)==0,"a module reset twice: the rule names it");
    constexpr Entry statelessReset[]={{ResetPhase::kReleaseHolds,Id::kC,&Run},{ResetPhase::kMissionState,Id::kA,&Run},
                                      {ResetPhase::kMissionState,Id::kB,&Run}};
    Check(FirstResetBreak(kRows,statelessReset)==1,"a stateless module with a reset: its reason is wrong, the rule names it");
    constexpr Row emptyWhy[]={{Id::kA,"a",nullptr},{Id::kB,"b",""},{Id::kC,"c",nullptr}};
    Check(FirstResetBreak(emptyWhy,kGood)==1,"a stateless reason left empty: not a reason");
}

void Order() noexcept {
    constexpr Entry backwards[]={{ResetPhase::kMissionState,Id::kA,&Run},{ResetPhase::kReleaseHolds,Id::kC,&Run}};
    Check(!PhasesInOrder(backwards),"an entry of an earlier phase after a later one: refused");
    Check(PhasesInOrder(kGood),"phases in order: kept");
    constexpr Entry outOfRange[]={{ResetPhase::kMissionState,static_cast<Id>(3),&Run}};
    Check(!IdsInRange(outOfRange,kN),"an entry naming no row: refused");
    constexpr Entry noRun[]={{ResetPhase::kMissionState,Id::kA,nullptr}};
    Check(!IdsInRange(noRun,kN),"an entry with nothing to run: refused");
    constexpr Row unordered[]={{Id::kB,"b",nullptr},{Id::kA,"a",nullptr},{Id::kC,"c",nullptr}};
    Check(!RowsInIdOrder(unordered),"rows out of their ids' order: refused (the id is the row, the fault id)");
    constexpr Row unnamed[]={{Id::kA,"",nullptr},{Id::kB,"b",nullptr},{Id::kC,"c",nullptr}};
    Check(!RowsInIdOrder(unnamed),"a row with no name: refused (the fault log's and the notice's name)");
}

void Unused() noexcept {
    constexpr Entry onlyA[]={{ResetPhase::kMissionState,Id::kA,&Run}};
    constexpr Entry onlyB[]={{ResetPhase::kMissionState,Id::kB,&Run}};
    Check(FirstUnused(kRows,onlyA,onlyB,onlyA,onlyB)==2,"a module in no pass: named");
    constexpr Entry onlyC[]={{ResetPhase::kMissionState,Id::kC,&Run}};
    Check(FirstUnused(kRows,onlyA,onlyB,onlyC,onlyA)==kN,"every module in some pass: none named");
    Check(CountOf(kGood,0)==1 && CountOf(kGood,1)==0 && Has(kGood,2) && !Has(kGood,1),"the counts the rules are made of");
}

// The fault policy (module_logic.h FaultStreak): kFaultLimit faulted frames in a row take the module off, once.
void FaultPolicy() noexcept {
    {
        FaultStreak f;
        Check(!f.Run(10,true) && !f.Run(11,true) && f.Run(12,true),"three frames faulted in a row: off at the third");
        Check(!f.Run(13,true) && !f.Run(14,true),"off once: no second report");
    }
    {
        FaultStreak f;
        Check(!f.Run(10,true) && !f.Run(10,true) && !f.Run(10,true) && !f.Run(10,true),"many faults in one frame: one frame faulted");
        Check(!f.Run(11,true) && f.Run(12,true),"...then two more frames: off");
    }
    {
        FaultStreak f;
        Check(!f.Run(10,true) && !f.Run(11,true) && !f.Run(12,false),"a clean frame...");
        Check(!f.Run(13,true) && !f.Run(14,true) && f.Run(15,true),"...breaks the streak: three more needed");
    }
    {
        FaultStreak f;   // a vehicle step: one bad vehicle among good ones, each frame
        bool offAt=false;
        for(std::uint64_t frame=20;frame<23;++frame)offAt=f.Run(frame,false) | f.Run(frame,true) | f.Run(frame,false);
        Check(offAt,"one vehicle faulting each frame among clean ones: off at the third frame");
    }
    {
        FaultStreak f;
        Check(!f.Run(10,true) && !f.Run(11,true) && f.Run(40,true),"frames it did not run in neither count nor break");
    }
    {
        FaultStreak f;
        for(std::uint64_t frame=1;frame<100;++frame)Check(!f.Run(frame,frame%2==0),"every other frame faulted: never off");
    }
    static_assert(kFaultLimit>=2,"one fault alone does not take a module off (a step faulting once is skipped once)");
}
}  // namespace

int main() {
    ResetRule();
    Order();
    Unused();
    FaultPolicy();
    std::printf("module_registry: %d cases, %d failed\n",cases,failed);
    return failed ? 1 : 0;
}
