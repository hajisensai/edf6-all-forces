#pragma once
namespace crew::support {
enum class Environment { unknown, normal, noExternalSupport, forceEnable };
struct Policy { Environment environment=Environment::unknown;bool underground=false; };
enum class Capability { infantry, civilianGround, militaryGround, air };
inline bool Allowed(Policy policy,Capability capability) noexcept {
    if(capability==Capability::infantry || capability==Capability::civilianGround)return true;
    if(capability==Capability::air && policy.underground)return false;
    return policy.environment==Environment::normal || policy.environment==Environment::forceEnable;
}
}
namespace crew { support::Policy SupportMissionPolicy() noexcept; }
