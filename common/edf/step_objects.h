// The scene's objects this period (common/step_objects.cpp): which pointers are objects the game's System holds alive
// now, so a caller can read them without asking edf::Readable (a working-set call per page each tick: stress:100 asked
// ~63,000 times a step, 4.6-8.2 ms a frame, docs/step-objects-re.md).
//
// Why an object in the System's list is safe to read (docs/step-objects-re.md, H):
//   - System (*(EDF+0x20B2958)) owns every scene object by a strong shared_ptr in one of its std::lists: +0x3B0 (added,
//     moved into +0x3D0 by 0x1195A80 / the tick loop 0x1199020), +0x3D0 (the scene's objects), +0x3C0 (Deleted, the
//     strong copy taken by 0x11975A0 before it leaves +0x3B0 / +0x3D0's turn). Nodes {next, prev, object, control block}.
//     Each list is changed under the System's CRITICAL_SECTION at +0x308.
//   - The strong references of +0x3D0 / +0x3C0 are dropped only by 0x1197680 (the release, called at 0x1198F71 once a
//     frame before the tick loop, and by the flush 0x11959E0 at 0x11959F8 and 0x1196A0D) and by the scene teardown
//     0x11956C0 (clears the lists; 25 call sites, the scene's state changes). Not by the table cleaning 0x1195F20 (it only Deletes and syncs raw tables).
//   - So from one release to the next, an object found in the list is allocated: its control block's use count > 0 and its
//     memory reads. It may be Deleted meanwhile (+0x18 bit 2, 0x118A273): Trusted still says yes (readable), Has says no.
// A period starts when the release returns (VehicleCrew hooks its three calls: src/step_objects_hook.cpp); nothing is
// trusted while a release or a teardown runs, and a teardown closes (the next frame's release reopens). The list is
// copied the first time it is asked in the period (TryEnterCriticalSection: never waits on another thread's hold; a
// held lock leaves the period without a copy, every answer false). Objects added after the copy are not in it.
//
// Every answer is false (the caller asks Readable as before) when: not installed (EDF6AutoTurret links this too and
// never installs it), not the game thread (the thread of the frame's release), no period yet, the scene ended
// (Close, crew::SceneOver), the System pointer or its scene (+0x68) differs from the copy's, or the list shrank below
// the copy's count (a teardown that bypassed the release). Game thread only.
#pragma once
#include <cstddef>
#include <cstdint>

namespace stepobj {
// The System's layout (docs/step-objects-re.md).
constexpr std::size_t kLock=0x308,kAdded=0x3B0,kObjects=0x3D0,kScene=0x68;
constexpr std::size_t kNodeObject=0x10,kNodeCtrl=0x18;
constexpr std::size_t kObjectFlags=0x18;
constexpr unsigned char kRemoved=4;
constexpr std::size_t kCtrlUses=0x8;

// Where the System pointer is (EDF+0x20B2958). Before any period. Null uninstalls (every answer false).
void Install(const void* const* systemGlobal) noexcept;
// The release (0x1197680) or the teardown (0x11956C0) starts: its objects are freed meanwhile (their destructors may run
// plugin code), nothing is trusted until Released.
void Releasing() noexcept;
// The release (0x1197680) returned: a new period. `frame`: the frame's own call (0x1198F71), whose thread becomes the
// game thread; the others (the flush) only end the period.
void Released(bool frame) noexcept;
// The scene ends (crew::SceneOver): nothing is trusted until the next frame's release.
void Close() noexcept;
// The period's number on the game thread with a period open, else 0: a key for facts that hold for a frame (the
// session, the pause), read once a period instead of once an ask.
std::uint64_t Period() noexcept;
// `object` is in this period's copy of the System's lists: allocated until the period ends (it may be Deleted).
bool Trusted(const void* object) noexcept;
// Trusted, its control block's use count > 0 and not Deleted (+0x18 bit 2): the object is in the scene.
bool Has(const void* object) noexcept;
// Game-thread counts since the last take (then zeroed): asks found in / missing from the copy, copies, objects copied.
struct Stats { unsigned long long yes,no,copies,copied; };
Stats TakeStats() noexcept;

// A fact that holds for a frame (the session, the pause, the host), read once a period: `read()` when no period is open
// (off the game thread, between scenes) or the period moved since `cache` was filled.
template<class T> struct PerPeriod { std::uint64_t period=0; T value{}; };
template<class T,class Read> T Once(PerPeriod<T>& cache,Read read) noexcept {
    const std::uint64_t p=Period();
    if(p && p==cache.period)return cache.value;
    const T now=read();
    if(p){cache.period=p;cache.value=now;}
    return now;
}
}  // namespace stepobj
