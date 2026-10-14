#pragma once
namespace crew::support {
enum class Environment { unknown, normal, noExternalSupport, forceEnable };
struct Policy { Environment environment=Environment::unknown;bool underground=false; };
enum class Capability { infantry, civilianGround, militaryGround, air };
// Underground (the native scene flag) only soldiers walk in (the user, 2026-10-14: 「地底也能召唤飞机支援……应该只能召唤
// 地底的钻地战车这种」; asked, 「只留步兵+钻地车」): no aircraft, and no hull of the support catalog either (the tank,
// the APC and the light truck are none of them made to go underground). The drill tank is the Air Raider's own vehicle
// request, which the game allows underground by its weapon's use_underground (tools/calls.py underground). FORCE_ENABLE
// does not open a cave roof.
inline bool Allowed(Policy policy,Capability capability) noexcept {
    if(capability==Capability::infantry)return true;
    if(policy.underground)return false;
    if(capability==Capability::civilianGround)return true;
    return policy.environment==Environment::normal || policy.environment==Environment::forceEnable;
}
}
namespace crew { support::Policy SupportMissionPolicy() noexcept; }
