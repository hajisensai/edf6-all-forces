// netprobe.cpp: the session probe.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// netprobe.cpp: Debug=1, online only: once a second per helicopter-class vehicle, which machine runs it and how its
// pose replication stands (the NET lines, docs/online-re.md). Reads only.
void NetProbe(unsigned char* vehicle) noexcept;
// Whether this machine is in an online session (netprobe.cpp; false when the session function is not the one read).
// This is a session query, not proof that an object participates in replication: plugin call aircraft do not
// register a network identity; delivered vehicles do (docs/online-re.md sections 1 and 2).
bool InSession() noexcept;
}  // namespace crew
