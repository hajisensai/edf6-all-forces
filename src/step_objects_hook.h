// The System's releases and teardowns hooked for stepobj (common/edf/step_objects.h): its periods start and end there.
#pragma once

namespace crew {
bool InstallStepObjects() noexcept;   // at load (module_table.h): every site or stepobj stays off
}  // namespace crew
