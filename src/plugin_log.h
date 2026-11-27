// The plugin's log (EDF6VehicleCrew.log; common/log.h is the writer both plugins link).
#pragma once
#include <cstddef>

namespace crew {
// The plugin's log (EDF6VehicleCrew.log; over kLogMax it is renamed to .log.1 and a new one begun).
void Log(const char* format,...) noexcept;
// Preformatted crash diagnostics: direct append, without the ordinary logger's locks or CRT formatting.
void CrashLog(const char* text,std::size_t size) noexcept;
void FlushLog() noexcept;   // what is buffered written now (plugin.cpp; best-effort, not the crash path)
}  // namespace crew
