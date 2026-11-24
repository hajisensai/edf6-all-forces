// The synchronous loads on the game thread (debug only; docs/sync-load-re.md, hitch-2 of
// docs/reviews/2026-11-24-optimization-audit.md).
//
// The resource cache's get-or-load 0x84E80(cache 0x20B28D0, out, key) looks the key up (0x9C950 at 0x84ECF). A miss
// inserts a pending entry (0x89360 at 0x84FDA), then reads the file in the CALLER's thread: the file system's reader
// (factory [0x2005540]) is opened (vtable +8, queues the read) and waited for (vtable +0x10 with 0 at 0x85052: a
// WaitForSingleObject INFINITE, 0x3E690). A hit on a pending entry (another thread loading it) blocks in the lookup on the
// entry's lock (0x9C9F4) until that load ends.
//
// With Debug=1 and SyncLoadProbe=1 the probe chains four call sites inside 0x84E80 (the lookup, the miss, the not-found
// branch, the exit) and, on the game thread only (perf_probe.h GameThread), logs after the outermost 0x84E80 returns:
//   SYNC LOAD <path> <ms> ms            a miss read on the game thread (the whole 0x84E80 call)
//   SYNC LOAD <path> <ms> ms not found  the same, the file missing (0x84E80 gives an empty handle)
//   SYNC WAIT <path> <ms> ms            a lookup that waited >= 0.5 ms (another thread's load of it, or the cache lock)
//   SYNC LOAD STATS ...                 cumulative counts and the hook's own time a call, at most every 10 s
// Other threads (the loader thread's preloads) only pass through.
#pragma once

namespace crew::syncload {
// After perfprobe::Install (it needs the frame anchor's game thread). Logs one HOOK line.
void Install() noexcept;
}  // namespace crew::syncload
