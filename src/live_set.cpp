#include "live_set.h"
#include "crew.h"
#include "edf/hash_index.h"
#include "memo_epoch.h"

namespace liveset {
namespace {
edf::PtrIndex objects;
// Keyed as WastedFire's memos are (memoepoch::Clock): a new set each step, and again each kStallMs the step does not move
// (paused, loading, the frame clock not found), so a stalled step never keeps an object live for good.
memoepoch::Clock clock;
std::uint64_t epoch=~0ull;
// The set of now's epoch, emptied when the epoch moved.
void Catch() noexcept {
    const std::uint64_t now=clock.Epoch(crew::GameFrame(),GetTickCount64());
    if(now!=epoch){objects.Clear();epoch=now;}
}
}  // namespace

void Add(const void* object) noexcept {
    if(!object)return;
    Catch();
    if(objects.Capacity()==0)objects.Reserve(4096);   // a battle's ~2,000 objects without growing mid-walk
    objects.Set(object,1);
}
bool Has(const void* object) noexcept {
    if(!object)return false;
    Catch();
    return objects.Find(object)>=0;
}
}  // namespace liveset
