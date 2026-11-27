#pragma once
// A small fork-join pool for work the game thread hands out and waits for in the same call (the soldiers' parallel
// Decide, src/npcai.cpp): Run(fn,ctx,n) calls fn(ctx,i) for every i in [0,n) on the pool's threads and the calling
// thread, and returns only once every call is done. Nothing is left running past Run, so a level's end, a reset or the
// plugin's teardown has nothing to join.
//
// - Threads: min(hardware threads - 1, 8) workers plus the caller, made on the first Run that needs them. SetLimit
//   caps the count (1: the caller alone, the fixtures' single-thread reference).
// - Order: the calls run in any order on any thread; a task writes only its own index's result, so the outcome is the
//   same for every thread count.
// - Floating point: every worker takes the caller's MXCSR (rounding, denormals) for the Run, so the same task gives
//   the same bits on any thread.
// - Faults: every call runs inside __try. A fault ends that call only (the others go on); its code, address, index and
//   thread come back in Fault, and the first fault of the process writes a minidump (SetDumpWriter) to
//   <%LOCALAPPDATA%\CrashDumps>\<exe>.<pid>.npcmt.dmp. The caller decides what a faulted index means.
#include <Windows.h>
#include <DbgHelp.h>
#include <cstdint>

namespace crew::workers {
using TaskFn=void(*)(void* ctx,int index) noexcept;

struct Fault {
    int count=0;                 // calls that faulted this Run
    DWORD code=0;                // the first one's exception code, address, index and thread
    const void* address=nullptr;
    int index=-1;
    DWORD thread=0;
    bool dumped=false;           // the first fault of the process, dumped by this Run
};

using DumpWriter=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,
                               PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
// The minidump writer for a task's fault (crew::MiniDumpWriter(), serialized; null: no dump) and its dump type.
void SetDumpWriter(DumpWriter writer,MINIDUMP_TYPE type) noexcept;

// The most threads a Run uses, the caller included: 0 = min(hardware - 1, 8) + 1, 1 = the caller alone.
void SetLimit(int threads) noexcept;
// The threads the next Run of a big enough job would use, the caller included.
int Threads() noexcept;
// fn(ctx,i) for i in [0,count), on the pool and this thread; returns when all are done. Not reentrant: one Run at a
// time, from one thread (the game thread). Returns the faulted calls' count (0: all ran to their end).
int Run(TaskFn fn,void* ctx,int count,Fault* fault=nullptr) noexcept;
// The workers ended and joined (tests; the plugin keeps them parked on a semaphore for the process).
void Shutdown() noexcept;
}  // namespace crew::workers
