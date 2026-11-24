#include "live_set.h"
#include "crew.h"
#include "edf/hash_index.h"

namespace liveset {
namespace {
edf::PtrIndex objects;
ULONGLONG step=~0ull;
}  // namespace

void Add(const void* object) noexcept {
    if(!object)return;
    const ULONGLONG now=crew::GameFrame();
    if(now!=step){objects.Clear();step=now;}
    if(objects.Capacity()==0)objects.Reserve(4096);   // a battle's ~2,000 objects without growing mid-walk
    objects.Set(object,1);
}
bool Has(const void* object) noexcept {
    return object && step==crew::GameFrame() && objects.Find(object)>=0;
}
}  // namespace liveset
