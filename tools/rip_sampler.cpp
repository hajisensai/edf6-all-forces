// rip_sampler: an out-of-process sampling profiler for the running game (no admin rights, no ETW).
// Every tick each thread that used CPU since the last tick is suspended, its stack walked (dbghelp
// StackWalk64 over the target's .pdata), and resumed. Output: one line per sample,
//   T <tid> <cpu_ms_delta> <module>+<hex offset> <module>+<hex offset> ...
// plus M lines with the process memory counters every second.
//
//   rip_sampler.exe <pid> <seconds> <interval_ms> <out.txt>
// Modules are listed once at the start (attach after the game has loaded them): one loaded later shows as ?+addr.
// interval_ms 0: nothing suspended, no stacks; only C lines at the end: each thread's CPU ms over the run.
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

namespace {

struct Mod { std::uint64_t base, end; std::string name; };

std::vector<Mod> Modules(HANDLE p) {
    std::vector<Mod> out;
    HMODULE mods[1024]; DWORD need = 0;
    if (!EnumProcessModulesEx(p, mods, sizeof mods, &need, LIST_MODULES_64BIT)) return out;
    const DWORD count = static_cast<DWORD>(std::min<std::size_t>(need, sizeof mods) / sizeof(HMODULE));
    for (DWORD i = 0; i < count; ++i) {
        MODULEINFO mi{}; char name[MAX_PATH] = {};
        GetModuleInformation(p, mods[i], &mi, sizeof mi);
        GetModuleBaseNameA(p, mods[i], name, MAX_PATH);
        out.push_back({reinterpret_cast<std::uint64_t>(mi.lpBaseOfDll),
                       reinterpret_cast<std::uint64_t>(mi.lpBaseOfDll) + mi.SizeOfImage, name});
    }
    return out;
}

std::string Where(const std::vector<Mod>& mods, std::uint64_t a) {
    for (const Mod& m : mods)
        if (a >= m.base && a < m.end) {
            char b[300]; std::snprintf(b, sizeof b, "%s+%llx", m.name.c_str(), static_cast<unsigned long long>(a - m.base));
            return b;
        }
    char b[32]; std::snprintf(b, sizeof b, "?+%llx", static_cast<unsigned long long>(a));
    return b;
}

std::uint64_t CpuOf(HANDLE t) {
    FILETIME c, e, k, u;
    if (!GetThreadTimes(t, &c, &e, &k, &u)) return 0;
    return (static_cast<std::uint64_t>(k.dwHighDateTime) << 32 | k.dwLowDateTime) +
           (static_cast<std::uint64_t>(u.dwHighDateTime) << 32 | u.dwLowDateTime);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: rip_sampler <pid> <seconds> <interval_ms> <out>\n"); return 2; }
    const DWORD pid = std::strtoul(argv[1], nullptr, 10);
    const DWORD seconds = std::strtoul(argv[2], nullptr, 10);
    const DWORD interval = std::strtoul(argv[3], nullptr, 10);
    FILE* out = nullptr;
    if (fopen_s(&out, argv[4], "w") != 0) out = nullptr;
    if (!out) return 3;
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc) { std::fprintf(stderr, "OpenProcess %lu\n", GetLastError()); return 4; }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    if (!SymInitialize(proc, nullptr, TRUE)) { std::fprintf(stderr, "SymInitialize %lu\n", GetLastError()); return 5; }
    std::vector<Mod> mods = Modules(proc);
    for (const Mod& m : mods) std::fprintf(out, "D %s %llx %llx\n", m.name.c_str(), m.base, m.end - m.base);

    std::map<DWORD, std::pair<HANDLE, std::uint64_t>> threads;
    timeBeginPeriod(1);
    const ULONGLONG stop = GetTickCount64() + seconds * 1000ull;
    ULONGLONG nextEnum = 0, nextMem = 0;
    unsigned long samples = 0;
    while (GetTickCount64() < stop) {
        const ULONGLONG now = GetTickCount64();
        if (now >= nextEnum) {
            nextEnum = now + 1000;
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snap == INVALID_HANDLE_VALUE) break;
            THREADENTRY32 te{sizeof te};
            for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
                if (te.th32OwnerProcessID != pid || threads.count(te.th32ThreadID)) continue;
                HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                if (t) threads[te.th32ThreadID] = {t, CpuOf(t)};
            }
            CloseHandle(snap);
            DWORD code = 0;
            if (!GetExitCodeProcess(proc, &code) || code != STILL_ACTIVE) break;
        }
        if (now >= nextMem) {
            nextMem = now + 1000;
            PROCESS_MEMORY_COUNTERS_EX pm{}; pm.cb = sizeof pm;
            GetProcessMemoryInfo(proc, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm), sizeof pm);
            std::fprintf(out, "M %llu ws=%zu private=%zu pagefile=%zu\n", now, pm.WorkingSetSize, pm.PrivateUsage, pm.PagefileUsage);
        }
        if (interval == 0) { Sleep(100); continue; }  // CPU-only: no stacks, nothing suspended
        for (auto& [tid, th] : threads) {
            const std::uint64_t cpu = CpuOf(th.first);
            if (cpu == th.second) continue;  // idle since last tick: waiting, not costing anything
            const std::uint64_t delta = cpu - th.second;
            th.second = cpu;
            if (SuspendThread(th.first) == static_cast<DWORD>(-1)) continue;
            CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(th.first, &ctx)) {
                STACKFRAME64 f{};
                f.AddrPC.Offset = ctx.Rip; f.AddrPC.Mode = AddrModeFlat;
                f.AddrFrame.Offset = ctx.Rbp; f.AddrFrame.Mode = AddrModeFlat;
                f.AddrStack.Offset = ctx.Rsp; f.AddrStack.Mode = AddrModeFlat;
                std::fprintf(out, "T %lu %llu", tid, delta / 10000);
                for (int depth = 0; depth < 48; ++depth) {
                    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, th.first, &f, &ctx, nullptr,
                                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || !f.AddrPC.Offset) break;
                    std::fprintf(out, " %s", Where(mods, f.AddrPC.Offset).c_str());
                }
                std::fputc('\n', out);
                ++samples;
            }
            ResumeThread(th.first);
        }
        Sleep(interval);
    }
    timeEndPeriod(1);
    std::uint64_t total = 0;
    for (auto& [tid, th] : threads) {
        const std::uint64_t cpu = CpuOf(th.first);
        if (interval == 0) std::fprintf(out, "C %lu %llu\n", tid, (cpu - th.second) / 10000);
        total += cpu - th.second;
    }
    std::fprintf(out, "C total %llu\n", total / 10000);
    for (auto& [tid, th] : threads) CloseHandle(th.first);
    SymCleanup(proc);
    CloseHandle(proc);
    std::fprintf(stderr, "samples %lu\n", samples);
    std::fclose(out);
    return 0;
}
