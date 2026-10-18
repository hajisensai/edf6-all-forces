#pragma once
// The plugin's own crash dump (2026-10-18: testers' reports #19 / #20 of a crash came with no dump at all; Windows
// writes one only where HKLM\...\Windows Error Reporting\LocalDumps exists, which a tester's machine does not have,
// and nothing in the plugin wrote one). docs/feedback-2026-10-18-crash-dump.md.
//
// What it catches, from any thread of the game:
//  - an exception nobody handles (the top-level filter): an access violation, a C++ exception no catch took, a stack
//    overflow (written by a thread of its own: the crashed thread has a page of stack left). One-chance exceptions a
//    __try / catch handles never reach it, so the game's and the CRT's handled ones (_com_error, ...) write nothing;
//  - abort() and std::terminate() of the game's CRT (ucrtbase.dll: its SIGABRT handler) and of this plugin's own
//    static CRT;
//  - an invalid CRT parameter (ucrtbase's and ours), which would fast-fail right after.
// What it cannot: __fastfail (the kernel ends the process without running any code in it: /GS failures, CFG,
// heap corruption the heap detects), a process killed from outside, a hang. Those are left to Windows' LocalDumps.
//
// The filter stays first: kernelbase!SetUnhandledExceptionFilter is redirected so a filter set later (the Steam
// client's, an overlay's) goes behind ours and is called after the dump; where that redirect cannot go in, the mission
// start puts the filter back on top (CheckCrashFilter).
#include <Windows.h>
#include <DbgHelp.h>

namespace crew {
using MiniDumpWriteDumpFn=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,
                                        PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
// A serialized MiniDumpWriteDump service (null if dbghelp or its worker is unavailable), never the raw dbghelp
// pointer. Hang and crash dumps execute on the same worker: dbghelp forbids concurrent calls. Resolve this before
// anything is stuck or crashed: a thread that died holding the loader lock would stop LoadLibrary.
MiniDumpWriteDumpFn MiniDumpWriter() noexcept;
// The dump's contents (mission_watch's hang dump too): every thread's stack and registers, the memory they point at,
// the modules (unloaded ones too), the threads' TEBs and the PEB. A few MB, not the whole heap.
constexpr MINIDUMP_TYPE kDumpType=static_cast<MINIDUMP_TYPE>(MiniDumpNormal|MiniDumpWithThreadInfo|MiniDumpWithUnloadedModules|
                                                              MiniDumpWithIndirectlyReferencedMemory|MiniDumpWithProcessThreadData);
// The crash dump installed: written to <folder>\<exe name>.<pid>.plugin.dmp, folder null: %LOCALAPPDATA%\CrashDumps,
// where Windows' own LocalDumps go and the installer's 「回传日志」 looks (tools/testhub.py collect, 'EDF6*.dmp').
// Once per process; call it only once the plugin stays loaded (the filter and handlers point into it).
bool InstallCrashDump(const wchar_t* folder=nullptr) noexcept;
// The mission start: when the redirect of SetUnhandledExceptionFilter did not go in, the filter is put back on top
// (and who had replaced it is logged); with the redirect, nothing to do.
void CheckCrashFilter() noexcept;
}  // namespace crew
