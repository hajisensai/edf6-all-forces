// edf::Readable (common/memory.cpp): page by page (QueryWorkingSetEx), VirtualQuery for pages not in the working set.
// What it must answer, on real memory of this process:
//   - read / write by the page's protection: read-write, read-only, execute-read, no access, a guard page (and the
//     guard is still there afterwards: Readable never touches the memory), reserved but not committed, freed;
//   - a span is readable only when every page of it is, across page boundaries and past kMaxPages (the region path);
//   - a page out of the working set (trimmed) is still committed and readable;
//   - an answer is kept only for its tick: a page made inaccessible is seen once the tick moves;
//   - null, size 0 and a span that would wrap.
// Then the cost on a 256 MB heap-like region against the old VirtualQuery-per-call way (printed, not asserted: the
// machine's speed is no verdict). Exit code 1 when a check fails.
#include <windows.h>
#include <psapi.h>

#include <chrono>
#include <cstdint>
#include <cstdio>

#include "edf/memory.h"

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

constexpr std::size_t kPage = 0x1000;

unsigned char* Alloc(std::size_t pages, DWORD protect) {
    return static_cast<unsigned char*>(VirtualAlloc(nullptr, pages * kPage, MEM_RESERVE | MEM_COMMIT, protect));
}

void WaitTick() {
    const ULONGLONG t = GetTickCount64();
    while (GetTickCount64() == t) Sleep(1);
}

// The old Readable: VirtualQuery per region, no cache (its cache hardly ever hit, see memory.cpp).
bool OldReadable(const void* ptr, std::size_t size) {
    auto at = reinterpret_cast<std::uintptr_t>(ptr);
    const auto end = at + size;
    while (at < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(at), &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;
        at = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}
}  // namespace

int main() {
    using edf::Readable;
    DWORD old0 = 0;
    unsigned char* rw = Alloc(4, PAGE_READWRITE);
    rw[0] = 1;
    Check(Readable(rw, 16) && Readable(rw, 16, true), "read-write page: readable and writable");
    Check(Readable(rw, 4 * kPage, true), "four read-write pages whole");
    {   // the page path is the one taken: a touched page is in the working set
        PSAPI_WORKING_SET_EX_INFORMATION w{};
        w.VirtualAddress = rw;
        Check(K32QueryWorkingSetEx(GetCurrentProcess(), &w, sizeof w) && w.VirtualAttributes.Valid,
              "a touched page is in the working set (QueryWorkingSetEx answers it: the page path)");
    }
    {   // two pages 32 MB apart share a cache slot (8192 entries): each answered as itself
        constexpr std::size_t kSlots = 8192;
        unsigned char* far1 = Alloc(kSlots + 1, PAGE_READWRITE);
        far1[0] = far1[kSlots * kPage] = 1;
        VirtualProtect(far1 + kSlots * kPage, kPage, PAGE_READONLY, &old0);
        WaitTick();
        bool same = true;
        for (int i = 0; i < 4; ++i)
            same = same && Readable(far1, 8, true) && !Readable(far1 + kSlots * kPage, 8, true) &&
                   Readable(far1 + kSlots * kPage, 8);
        Check(same, "two pages in one cache slot, asked in turn: each answered as itself");
    }
    {   // the image: this exe's code is readable, not writable; its read-only data too
        const auto* code = reinterpret_cast<const unsigned char*>(&main);
        Check(Readable(code, 16) && !Readable(code, 16, true), "image code: readable, not writable");
        static const char kConst[] = "read-only data";
        Check(Readable(kConst, sizeof kConst) && !Readable(kConst, sizeof kConst, true),
              "image read-only data: readable, not writable");
    }

    unsigned char* ro = Alloc(1, PAGE_READWRITE);
    ro[0] = 1;
    DWORD old = 0;
    VirtualProtect(ro, kPage, PAGE_READONLY, &old);
    WaitTick();
    Check(Readable(ro, 8) && !Readable(ro, 8, true), "read-only page: readable, not writable");

    unsigned char* rx = Alloc(1, PAGE_READWRITE);
    rx[0] = 0xC3;
    VirtualProtect(rx, kPage, PAGE_EXECUTE_READ, &old);
    WaitTick();
    Check(Readable(rx, 1) && !Readable(rx, 1, true), "execute-read page: readable, not writable");

    unsigned char* na = Alloc(1, PAGE_NOACCESS);
    Check(!Readable(na, 1) && !Readable(na, 1, true), "no-access page: neither");
    unsigned char* xo = Alloc(1, PAGE_READWRITE);
    xo[0] = 0xC3;
    VirtualProtect(xo, kPage, PAGE_EXECUTE, &old0);
    WaitTick();
    Check(!Readable(xo, 1), "execute-only page: not readable");

    unsigned char* guard = Alloc(1, PAGE_READWRITE | PAGE_GUARD);
    Check(!Readable(guard, 8), "guard page: not readable");
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(guard, &mbi, sizeof mbi);
    Check((mbi.Protect & PAGE_GUARD) != 0, "guard page: the guard is still there after the ask");

    auto* reserved = static_cast<unsigned char*>(VirtualAlloc(nullptr, kPage * 4, MEM_RESERVE, PAGE_READWRITE));
    Check(!Readable(reserved, 8) && !Readable(reserved + kPage * 2, 8), "reserved, not committed: not readable");

    // a span over a readable page and a no-access one
    unsigned char* two = Alloc(2, PAGE_READWRITE);
    two[0] = two[kPage] = 1;
    VirtualProtect(two + kPage, kPage, PAGE_NOACCESS, &old);
    WaitTick();
    Check(Readable(two + kPage - 8, 8), "the last bytes before the no-access page");
    Check(!Readable(two + kPage - 8, 16), "a span into the no-access page");

    // past kMaxPages (16): the region path, a hole in the middle seen
    unsigned char* big = Alloc(40, PAGE_READWRITE);
    for (std::size_t i = 0; i < 40; ++i) big[i * kPage] = 1;
    Check(Readable(big, 40 * kPage, true), "40 read-write pages whole");
    VirtualProtect(big + 20 * kPage, kPage, PAGE_NOACCESS, &old);
    WaitTick();
    Check(!Readable(big, 40 * kPage), "40 pages with a no-access one in the middle");
    Check(!Readable(big + 18 * kPage, 4 * kPage), "4 pages over the hole (the page path)");

    // out of the working set: still committed
    unsigned char* trimmed = Alloc(2, PAGE_READWRITE);
    trimmed[0] = trimmed[kPage] = 1;
    K32EmptyWorkingSet(GetCurrentProcess());
    PSAPI_WORKING_SET_EX_INFORMATION ws{};
    ws.VirtualAddress = trimmed;
    K32QueryWorkingSetEx(GetCurrentProcess(), &ws, sizeof ws);
    std::printf("     (the trimmed page is %sin the working set)\n", ws.VirtualAttributes.Valid ? "still " : "not ");
    Check(Readable(trimmed, 2 * kPage, true), "trimmed pages: readable and writable");
    Check(Readable(trimmed, 2 * kPage, true), "trimmed pages asked again (kept for the tick)");
    VirtualProtect(trimmed, kPage, PAGE_NOACCESS, &old0);
    WaitTick();
    Check(!Readable(trimmed, 8), "a trimmed page made no-access: seen once the tick moved");

    // kept for the tick only
    unsigned char* later = Alloc(1, PAGE_READWRITE);
    later[0] = 1;
    Check(Readable(later, 8), "a page asked once");
    VirtualProtect(later, kPage, PAGE_NOACCESS, &old);
    WaitTick();
    Check(!Readable(later, 8), "made no-access: seen once the tick moved");
    VirtualProtect(later, kPage, PAGE_READWRITE, &old);
    VirtualFree(rx, 0, MEM_RELEASE);
    WaitTick();
    Check(!Readable(rx, 1), "freed: not readable once the tick moved");

    Check(!Readable(nullptr, 1), "null");
    Check(Readable(rw, 0), "size 0 at a readable page");
    Check(!Readable(reinterpret_cast<void*>(UINTPTR_MAX - 4), 16), "a span that would wrap");

    // the cost: random small reads over a 256 MB committed, touched region (a game heap)
    constexpr std::size_t kHeapPages = 256 * 1024 * 1024 / kPage;
    unsigned char* heap = Alloc(kHeapPages, PAGE_READWRITE);
    if (heap) {
        for (std::size_t i = 0; i < kHeapPages; ++i) heap[i * kPage] = 1;
        constexpr int kAsks = 20000, kOldAsks = 200;   // the old way is ~0.3 ms an ask: a few are enough
        std::uint64_t x = 88172645463325252ull;
        auto pick = [&] {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            return heap + (x % (kHeapPages * kPage - 0x200));
        };
        int okNew = 0, okOld = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kAsks; ++i) okNew += Readable(pick(), 0x100);
        auto t1 = std::chrono::steady_clock::now();
        for (int i = 0; i < kOldAsks; ++i) okOld += OldReadable(pick(), 0x100);
        auto t2 = std::chrono::steady_clock::now();
        Check(okNew == kAsks && okOld == kOldAsks, "every ask in the heap readable, both ways");
        const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / kAsks;
        const double usOld = std::chrono::duration<double, std::micro>(t2 - t1).count() / kOldAsks;
        std::printf("     cost per ask in a 256 MB region: %.2f us now, %.2f us the old way (x%.0f)\n", us, usOld,
                    us > 0 ? usOld / us : 0.0);
        VirtualFree(heap, 0, MEM_RELEASE);
    }
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
