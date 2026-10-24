// One fixed key per attack of the controlled enemy (the user, 2026-10-19: no switching,
// each attack its own key, usable at any time). The catalogue's ability i is bound to
// kBindings[i]: a keyboard/mouse key for the first local player, an XInput button for the
// pad. Movement (WASD, Space/Ctrl), the map key and the plugin's on-foot hotkeys
// (Q mark, T formation, Y pickup, C high camera, Z zoom, V first person, N thermal,
// middle button free look, F1-F9, [ ]) are left out so an attack never doubles as them.
#pragma once
#include <cstdint>

namespace crew::enemy_control {
constexpr std::uint32_t kPadRightTrigger=0x10000,kPadLeftTrigger=0x20000;   // beyond XInput's 16 button bits
struct KeyBinding { int vk;std::uint32_t pad; };
inline constexpr KeyBinding kBindings[]={
    {0x01,kPadRightTrigger},  // left mouse button / RT
    {0x02,kPadLeftTrigger},   // right mouse button / LT
    {'E',0x0200},             // RB
    {'R',0x0100},             // LB
    {'F',0x1000},             // A
    {'G',0x2000},             // B
    {'X',0x4000},             // X
    {'B',0x8000},             // Y
    {0x05,0x0001},            // mouse 4 / D-pad up
    {0x06,0x0008},            // mouse 5 / D-pad right
    {'1',0x0002},             // D-pad down
    {'2',0x0004},             // D-pad left
    {'3',0},{'4',0},{'5',0},{'6',0},{'7',0},{'8',0},{'9',0},{'0',0},
};
constexpr unsigned kBindingCount=sizeof(kBindings)/sizeof(kBindings[0]);
constexpr std::uint8_t kTriggerDown=64;   // of 255: a trigger past a quarter is a press
inline bool PadDown(std::uint32_t binding,std::uint16_t buttons,std::uint8_t leftTrigger,std::uint8_t rightTrigger) noexcept {
    if(binding==kPadRightTrigger)return rightTrigger>=kTriggerDown;
    if(binding==kPadLeftTrigger)return leftTrigger>=kTriggerDown;
    return binding && (buttons&binding)==binding;
}
// The key of ability `index` of the catalogue, null past the last key (the M card still uses it).
inline const KeyBinding* BindingOf(unsigned index) noexcept { return index<kBindingCount ? &kBindings[index] : nullptr; }
}
