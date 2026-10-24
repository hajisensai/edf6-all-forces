#pragma once
#include <cstddef>
#include <cstdint>

namespace crew::enemy_control {
constexpr unsigned kConcurrentAbilities=8;
constexpr unsigned kAbilityPage=12;
constexpr unsigned kMaxControllers=8;
enum class Phase : std::uint32_t { idle, pending, active, restoring, failed };
enum class Reason : std::uint32_t {
    none, disabled, invalidRequester, invalidTarget, sameObject, friendly, building,
    unsupportedFamily, incompleteSkills, nativeMismatch, noIdentity, busy, stale,
    nativeFailure, notOwner, networkUnavailable, dead, sceneChanged, networkCapacity, riding
};
enum AbilityFlag : std::uint32_t { held=1, edge=2, movement=4, passive=8 };
enum class AbilityState : std::uint32_t { native, active, cooldown, unavailable };
struct Ability {
    std::uint32_t id=0,flags=0,channel=0;
    AbilityState state=AbilityState::native;
    float cooldownSeconds=0;
    bool cooldownKnown=false;
    wchar_t name[96]{};
};
// Stable semantic identities; never function addresses, native pointers, CAS indices,
// or arbitrary caller-provided state strings. The complete catalogue can have any
// number of entries; this bounds only abilities concurrently held in one sample.
struct ActiveAbility { std::uint32_t id=0,phase=0; }; // phase: 1 hold, 2 press event
struct Input {
    float move[3]{},look[2]{};
    float aimYaw=0,aimPitch=0;
    bool absoluteAim=false;
    unsigned char fullFocusId[32]{};
    ActiveAbility active[kConcurrentAbilities]{};
    std::uint64_t sequence=0,seed=0;
};
struct Snapshot {
    Phase phase=Phase::idle;
    Reason reason=Reason::none;
    std::uint64_t revision=0,lease=0,request=0;
    std::uintptr_t ownerHumanKey=0; // opaque draw-view comparison only
    unsigned char owner[32]{},target[32]{};
    std::uint64_t profileHash=0;
    std::uint32_t selectedAbility=0,abilityCount=0;
    std::int32_t controlledTeam=0;
    bool sourceProtected=false;
    bool followsOwnerTeam=false;
    float health=0,maxHealth=0;
    bool healthKnown=false;
    wchar_t name[96]{};
    Ability selected{};
};
const wchar_t* ReasonText(Reason) noexcept;
bool ValidInput(const Input&) noexcept;
std::uint32_t AbilityId(const char* profile,const wchar_t* name,unsigned channel) noexcept;
}
