// A stand-in EDF6AutoTurret.dll for tests/crash_dump_test.cpp: a log buffer of its own (the real plugin's is its own
// copy of common/log.cpp) with no writer thread, so a line is in the file only when the crash path flushed it. Its
// crash export has the real one's name and contract (common/log.h kCrashFlush: try-only, never waits).
#include "../common/log.h"
#include <cstring>
#include <cwchar>

namespace {
edf::logfile::Buffer<4096> buffer;
wchar_t path[MAX_PATH]{};
void Write(const char* bytes,std::size_t size,unsigned) noexcept { edf::logfile::AppendCrashLine(path,bytes,size); }
}  // namespace

extern "C" __declspec(dllexport) void __cdecl PeerStart(const wchar_t* log) { wcscpy_s(path,log); }
extern "C" __declspec(dllexport) void __cdecl PeerLine(const char* text) {
    buffer.Append(text,std::strlen(text));
    buffer.Append("\r\n",2);
}
// The crashing thread owns the peer's buffer lock: the flush must give up, not wait.
extern "C" __declspec(dllexport) void __cdecl PeerLock() { AcquireSRWLockExclusive(&buffer.bufferLock); }
extern "C" __declspec(dllexport) void __cdecl EDF6AutoTurret_CrashFlushLogV1() { buffer.Drain(&Write,true); }
