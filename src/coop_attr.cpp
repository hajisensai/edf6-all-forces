// EDF6Coop.dll's member attribute table and mission player resolver (coop_attr.h): looked up while EDF6Coop is loaded,
// again when the module changes (unloaded, or a harness loading it after us), logged once a change.
#include "coop_attr.h"
#include "crew.h"
#include <Windows.h>
#include <atomic>

namespace crew::coopattr {
namespace {
SRWLOCK lock=SRWLOCK_INIT;
HMODULE module=nullptr;
const Table* table=nullptr;
ResolveFn resolve=nullptr;
// Under the lock: the current module's table and resolver.
void Look() noexcept {
    const HMODULE now=GetModuleHandleW(L"EDF6Coop.dll");
    if(now==module)return;
    module=now;table=nullptr;resolve=nullptr;
    if(!now){Log("COOPATTR EDF6Coop.dll not loaded: no room member attributes");return;}
    const auto get=reinterpret_cast<EDF6CoopGetMemberAttributeApiFn>(GetProcAddress(now,"EDF6CoopGetMemberAttributeApi"));
    const Table* t=nullptr;
    __try { t=get ? get(EDF6COOP_MEMBER_ATTRIBUTE_VERSION) : nullptr; } __except(EXCEPTION_EXECUTE_HANDLER){t=nullptr;}
    table=Usable(t);
    resolve=reinterpret_cast<ResolveFn>(GetProcAddress(now,"EDF6Coop_ResolveMissionPlayerPuid"));
    Log("COOPATTR EDF6Coop: member attributes %s, mission player ids %s",table ? "found" : t ? "of another version: unused" :
        "missing (older EDF6Coop)",resolve ? "found" : "missing (older EDF6Coop)");
}
}  // namespace

const Table* Api() noexcept {
    AcquireSRWLockExclusive(&lock);
    Look();
    const Table* t=table;
    ReleaseSRWLockExclusive(&lock);
    return t;
}
ResolveFn ResolvePuid() noexcept {
    AcquireSRWLockExclusive(&lock);
    Look();
    const ResolveFn r=resolve;
    ReleaseSRWLockExclusive(&lock);
    return r;
}
EDF6CoopRoomState Room() noexcept {
    EDF6CoopRoomState s{};
    s.size=sizeof(s);
    const Table* t=Api();
    bool ok=false;
    __try { ok=t && EDF6COOP_MA_HAS(t,room) && t->room && t->room(&s)!=0; } __except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
    if(!ok){s=EDF6CoopRoomState{};s.size=sizeof(s);}
    return s;
}
}  // namespace crew::coopattr
