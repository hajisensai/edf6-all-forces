#include "live_set.h"
#include "scene.h"
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
void Catch(std::uint64_t nowMs) noexcept {
    const std::uint64_t now=clock.Epoch(crew::GameFrame(),nowMs);
    if(now!=epoch){objects.Clear();epoch=now;}
}
}  // namespace

void AddAt(const void* object,std::uint64_t nowMs) noexcept {
    if(!object)return;
    Catch(nowMs);
    if(objects.Capacity()==0)objects.Reserve(4096);   // a battle's ~2,000 objects without growing mid-walk
    objects.Set(object,1);
}
bool HasAt(const void* object,std::uint64_t nowMs) noexcept {
    if(!object)return false;
    Catch(nowMs);
    return objects.Find(object)>=0;
}
void Add(const void* object) noexcept { AddAt(object,GetTickCount64()); }
bool Has(const void* object) noexcept { return HasAt(object,GetTickCount64()); }
}  // namespace liveset
