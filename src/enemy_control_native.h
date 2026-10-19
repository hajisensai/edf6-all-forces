// Native enemy input is already present in the supported EDF.dll. This adapter
// supplies normal pad records during each class's verified input slot; native
// locomotion, slot 5 and CAS/MAB actions stay the engine's. The owner wraps slot 7 to suspend decisions while
// controlled; no AI-list remove/add or state-machine reconstruction is needed.
#pragma once
#include <cstddef>
#include <cstdint>

namespace crew::enemy_native {
constexpr std::size_t kPadBytes=0x200;
constexpr unsigned kButtonCount=16;
constexpr std::size_t kPadButtonBase=0x70,kPadButtonStride=0x18;
constexpr std::size_t kInputSlot=4,kAiSlot=7;
enum class Kind : std::uint8_t { insect,walking,humanoid,flying,monster,deiroi };
struct Input {
    float right=0.0f,forward=0.0f,yaw=0.0f,pitch=0.0f,vertical=0.0f;
    std::uint32_t held=0,pressed=0; // native pad record indices, zero based
    std::uint64_t seed=0; // exact per-object native RNG state at a skill edge
};
enum class AbilityKind : std::uint8_t { pad, namedAction, objectAction, monsterAction, mermanAction, deiroiFire, deiroiAuto, humanoidWeapon, walkingAction, walkingGun, timeShipCommand };
struct NativeAbility {
    const char* id;
    const wchar_t* name;
    std::uint16_t held,pressed; // records, not keyboard/pad device bindings
    AbilityKind kind=AbilityKind::pad;
    const wchar_t* action=nullptr; // a catalogue-owned native action, never caller text
    std::int32_t argument=0;
};
struct NativeProfile {
    const char* name;
    std::uint32_t vtableRva,inputRva,aiRva,profileHash;
    std::uint16_t slots,buttonMask;
    Kind kind;
    std::uint8_t axes; // bit 0 move X/Z, bit 1 yaw, bit 2 pitch, bit 3 vertical flight
    std::uint8_t inputSlot; // the native path reads the pad in slot 4 or slot 5
};
struct NativeLease;
using RestoreGateFn=bool(*)(NativeLease&) noexcept;
struct NativeLease {
    unsigned char* object=nullptr;
    const NativeProfile* profile=nullptr;
    const void* control=nullptr; // weak identity; owner owns its weak-reference lease
    RestoreGateFn restoreGate=nullptr; // owner-installed pending native rollback, before any state is forgotten
    alignas(16) unsigned char pad[kPadBytes]{}; // stable while borrowed; never move an active borrowed lease
    const void* savedPad=nullptr;
    std::uint32_t previousHeld=0;
    unsigned char savedPlayer=0;
    bool borrowed=false;
    bool pointerRestored=false;
    unsigned char reserved[1]{};
};
// Per-variant abilities are derived from the admitted native input path and its
// actual equipment. The family catalog supplies names; the engine retains gates.
using InputFn=std::uint64_t(__fastcall*)(void*,void*);
using OwnsAiFn=bool(*)(const void* currentSlot) noexcept;
using SkillSeedFn=bool(*)(void* object,std::uint64_t seed) noexcept;
bool RegisterControlledAiExtension(std::uint32_t vtableRva,std::uint32_t expectedOriginalAiRva,
                                   OwnsAiFn owns,InputFn controlled,SkillSeedFn onSeed=nullptr) noexcept;
bool RunControlledAiExtension(const NativeLease& lease,void* dt,std::uint64_t* result=nullptr) noexcept;
// The semantic movement is camera-relative right/up/forward, with positive W
// forward. These helpers invert the verified native control coordinate system.
bool WorldMoveToPad(const NativeLease& lease,const float movement[3],float cameraYaw,Input* out) noexcept;
bool AimToPad(const NativeLease& lease,float desiredYaw,float semanticPitch,Input* out) noexcept;

// Read-only exact native vtable/RTTI/signature admission. Abstract bases, native
// parts/buildings and unknown/modified input paths have no profile.
const NativeProfile* Profile(const void* object) noexcept;
const NativeProfile* Profiles(std::size_t* count) noexcept;
bool Capture(void* object,NativeLease* out) noexcept;
bool Neutralize(NativeLease& lease) noexcept; // release latched native movement/held input before ownership ends
bool Restore(NativeLease* lease) noexcept; // failure retains the live lease and controller baseline
bool SetSeed(const NativeLease& lease,std::uint64_t seed) noexcept;
void BuildPad(const Input& input,unsigned char (&pad)[kPadBytes]) noexcept;
// Requires the admitted object's original at profile.inputSlot (kept before cloning). Writes +0x340/+0x354 for this call only and restores them in
// a finally block, including a native exception. false means no successful call.
bool RunInput(NativeLease& lease,const Input& input,InputFn original,void* dt,
              std::uint64_t* result=nullptr) noexcept;
} // namespace crew::enemy_native
