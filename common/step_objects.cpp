// The scene's objects this period (edf/step_objects.h has why an object in the System's lists reads).
#include "edf/step_objects.h"
#include "edf/hash_index.h"
#include "edf/layout.h"
#include <Windows.h>
#include <atomic>
#include <new>
#include <vector>

namespace stepobj {
namespace {
std::atomic<const void* const*> global{nullptr};
std::atomic<std::uint64_t> period{0};      // 0: no period yet
std::atomic<bool> open{false};             // a frame's release seen and no Close since
std::atomic<DWORD> gameThread{0};
std::atomic<int> releasing{0};           // inside a release or a teardown: its objects are going

// The copy (game thread only): made for `copiedFor`, of the System `system` with its scene `scene`, `count` objects.
edf::PtrIndex index;
std::vector<const void*> ctrls;
std::uint64_t copiedFor=0;
bool copyOk=false;
const unsigned char* copySystem=nullptr;
const void* copyScene=nullptr;
std::uint64_t copyCount=0;
Stats stats{};

constexpr std::uint64_t kMostObjects=1u<<20;

struct ListHead { const unsigned char* head; std::uint64_t size; };
ListHead ListAt(const unsigned char* system,std::size_t at) noexcept {
    return {edf::At<const unsigned char*>(system,at),edf::At<std::uint64_t>(system,at+8)};
}
// The nodes of one list into the copy (the room reserved for them already: no growth, no throw).
bool CopyList(const ListHead& l) noexcept {
    if(!l.head || l.size>kMostObjects)return false;
    const unsigned char* node=edf::At<const unsigned char*>(l.head,0);
    for(std::uint64_t i=0;i<=l.size;++i) {
        if(!node)return false;
        if(node==l.head)return true;
        const void* o=edf::At<const void*>(node,kNodeObject);
        const void* c=edf::At<const void*>(node,kNodeCtrl);
        if(o && c && index.Find(o)<0) {
            index.Set(o,static_cast<int>(ctrls.size()));
            ctrls.push_back(c);
        }
        node=edf::At<const unsigned char*>(node,0);
    }
    return false;   // longer than its size: changed under us
}
bool CopyUnderLock(const ListHead& objects,const ListHead& added) noexcept {
    __try {
        return CopyList(objects) && CopyList(added);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Reserve(std::size_t n) noexcept {
    try {
        index.Reserve(n);
        ctrls.reserve(n);
        return true;
    } catch(const std::bad_alloc&) { return false; }
}
// This period's copy, made now.
void Copy(const unsigned char* system,const void* scene) noexcept {
    index.Clear();ctrls.clear();
    copyOk=false;copySystem=system;copyScene=scene;copyCount=0;
    auto* lock=reinterpret_cast<CRITICAL_SECTION*>(const_cast<unsigned char*>(system)+kLock);
    if(!TryEnterCriticalSection(lock))return;   // another thread holds the lists: no copy this period
    ListHead objects{},added{};
    bool ok=false;
    __try {
        objects=ListAt(system,kObjects);added=ListAt(system,kAdded);
        ok=objects.size<=kMostObjects && added.size<=kMostObjects;
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok=false; }
    ok=ok && Reserve(static_cast<std::size_t>(objects.size+added.size+64));
    ok=ok && CopyUnderLock(objects,added);
    LeaveCriticalSection(lock);
    copyOk=ok;
    copyCount=objects.size;
    ++stats.copies;
    if(ok)stats.copied+=ctrls.size();
    else{index.Clear();ctrls.clear();}
}
// The copy's control block of `object` when the period's copy is good and still the System's, else null.
const void* Lookup(const void* object) noexcept {
    if(!object)return nullptr;
    const std::uint64_t p=Period();
    if(!p)return nullptr;
    const auto* const* where=global.load(std::memory_order_acquire);
    const auto* system=reinterpret_cast<const unsigned char*>(*where);
    if(!system)return nullptr;
    const void* scene=nullptr;
    std::uint64_t count=0;
    __try {
        scene=edf::At<const void*>(system,kScene);
        count=edf::At<std::uint64_t>(system,kObjects+8);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    if(copiedFor!=p){copiedFor=p;Copy(system,scene);}
    if(!copyOk || system!=copySystem || scene!=copyScene || count<copyCount)return nullptr;
    const int at=index.Find(object);
    if(at<0){++stats.no;return nullptr;}
    ++stats.yes;
    return ctrls[static_cast<std::size_t>(at)];
}
}  // namespace

void Install(const void* const* systemGlobal) noexcept {
    open.store(false);
    releasing.store(0);
    global.store(systemGlobal,std::memory_order_release);
    period.fetch_add(1);
}
void Releasing() noexcept {
    releasing.fetch_add(1);
    period.fetch_add(1);
}
void Released(bool frame) noexcept {
    if(releasing.load()>0)releasing.fetch_sub(1);
    if(frame) {
        gameThread.store(GetCurrentThreadId(),std::memory_order_relaxed);
        open.store(true);
    }
    period.fetch_add(1);
}
void Close() noexcept {
    open.store(false);
    period.fetch_add(1);
}
std::uint64_t Period() noexcept {
    if(!global.load(std::memory_order_acquire) || !open.load(std::memory_order_acquire) || releasing.load()>0 ||
       GetCurrentThreadId()!=gameThread.load(std::memory_order_relaxed))return 0;
    return period.load(std::memory_order_acquire);
}
bool Trusted(const void* object) noexcept { return Lookup(object)!=nullptr; }
bool Has(const void* object) noexcept {
    const void* ctrl=Lookup(object);
    bool yes=false;
    if(ctrl) {
        __try {
            yes=edf::At<std::int32_t>(ctrl,kCtrlUses)>0 &&
                !(edf::At<unsigned char>(object,kObjectFlags)&kRemoved);
        } __except(EXCEPTION_EXECUTE_HANDLER) { yes=false; }
    }
    return yes;
}
Stats TakeStats() noexcept { const Stats s=stats; stats={}; return s; }
}  // namespace stepobj
