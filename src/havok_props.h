#pragma once

namespace crew {
// Plugin initialization only, before the application's init (so before any hknpWorld and its event dispatcher):
// every dispatcher made after it links its signal nodes by uint32 indexes instead of uint16 (havok_props_logic.h),
// so more than 0xFFFF live (body, event type) subscriptions no longer close the lists into a cycle. All sites or none;
// refused (stock, one log line) when any byte differs or the application's init has run already.
bool InstallHavokProps() noexcept;
}  // namespace crew
