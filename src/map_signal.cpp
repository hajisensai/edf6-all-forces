// map_signal.cpp: the machine's list of the M map's signals (map_signal.h), under a lock: added on the game thread, read
// by the HUD's draw.
#include "map_signal.h"
#include <windows.h>

namespace crew {
namespace {
SRWLOCK lock=SRWLOCK_INIT;
mapsignal::List signals;
}  // namespace

bool AddMapSignal(const float* at,unsigned kind,bool mine) noexcept {
    AcquireSRWLockExclusive(&lock);
    const bool kept=signals.Add(at,kind,mine,GetTickCount64());
    ReleaseSRWLockExclusive(&lock);
    return kept;
}
int MapSignalViews(mapsignal::Signal* out,int max) noexcept {
    if(!out || max<=0)return 0;
    AcquireSRWLockExclusive(&lock);   // Live drops the ones past their time
    const int n=signals.Live(GetTickCount64(),out,max);
    ReleaseSRWLockExclusive(&lock);
    return n;
}
void ResetMapSignals() noexcept {
    AcquireSRWLockExclusive(&lock);
    signals=mapsignal::List{};
    ReleaseSRWLockExclusive(&lock);
}
}  // namespace crew
