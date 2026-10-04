// The memory primitives live in common/ (edf6common, shared with EDF6AutoTurret); the plugin's code calls
// them unqualified from namespace crew.
#pragma once
#include "edf/memory.h"
namespace crew {
using edf::Readable;
using edf::AllocateNearThunk;
using edf::RedirectCall;
}  // namespace crew
