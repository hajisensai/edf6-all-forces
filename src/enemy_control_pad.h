// The controlled enemy's movement and look, read the way the player's own soldier reads its
// controller (EDF.dll TimeDateStamp 0x678CCB46, static disassembly 2026-10-21):
//  - 0x56D920 (left stick) and 0x56DBC0 (right stick) take the pad path when the controller
//    object's +0x2D68 is non-zero or 0x1183D70 (movzx eax, byte [0x1FEEE1C]) is true: the sticks
//    are the object's own +0x0C / +0x24 (move: right positive, forward negative) and
//    +0x3C / +0x54 (look: right / down positive), with no record index (0x56DA0C, 0x56DE6C);
//  - otherwise the keyboard path: the record [human+0xD40] (stride 0xA80) holds the key bytes
//    +0x610 forward, +0x628 left, +0x640 back, +0x658 right (0x56D99D..0x56D9E4: x = -[628]+[658],
//    y = -[610]+[640]) and the mouse's per-frame motion +0x66C / +0x684, which the game divides
//    by 24 (0x1766014, 0x56DD40) -- the same record the map's mouse reads (map.cpp MouseDelta).
// The user (2026-10-21): 「我怎么左键往右走，右键往后走，wasd没反应」. The sampler read the pad
// floats of the record whatever the path: on the keyboard path those are not the sticks (the
// soldier never reads them there), and the keys the soldier walks with were not read at all.
//
// Which record, and whose keys (static disassembly 2026-10-24):
//  - [human+0xD40] is the soldier CLASS's key-configuration record, not a player number. Each
//    class constructor writes its own: HumanBase 0x56AAF6 and AssultSoldier (Ranger) 0,
//    PaleWing (Wing Diver) 1 at 0x57CD80, Engineer (Air Raider) 2 at 0x563706, HeavyArmor
//    (Fencer) 3 at 0x5652C8. The controller object holds the four classes' records.
//  - The local pad bound to this player is [human+0x338]: 0x54ED40 (human, pad) writes it and
//    takes the controller object 0x77030(manager, pad) = manager+8+pad*0x2D70 into +0x340.
//    0x22AB90 (the mission's player creation) calls it with its 5th argument, the pad, and skips
//    it for a negative one (0x22ACFB: a remote online user, -1); 0x54EDB0 / 0x54E830 reset it
//    to -1 for a soldier with no pad. So the first local player of this machine has pad 0,
//    offline (the creation loop's pads 0..3) and online (this machine's own count) alike.
// The sampler once gated the plugin's own attack keys / XInput on [human+0xD40]==0: only a
// Ranger had them (and, before reading the record's keys, WASD at all); a Wing Diver, Air
// Raider or Fencer had nothing but the native fire action.
#pragma once
#include <cstddef>
#include <cstring>

namespace crew::enemy_control {
constexpr std::size_t kHumanClassRecord=0xD40,kHumanLocalPad=0x338;
constexpr std::size_t kPadRecordStride=0xA80,kPadRecords=4,kPadPathField=0x2D68;
// This machine's keyboard and the plugin's own XInput read belong to its first local player (pad 0).
inline bool FirstLocalPad(int localPad) noexcept { return localPad==0; }
constexpr std::size_t kPadStickRight=0x0C,kPadStickForward=0x24,kPadLookRight=0x3C,kPadLookDown=0x54;
constexpr std::size_t kKeyForward=0x610,kKeyLeft=0x628,kKeyBack=0x640,kKeyRight=0x658;
constexpr std::size_t kMouseRight=0x66C,kMouseDown=0x684,kRecordBytes=0x688;
constexpr float kMouseStep=24.0f;   // 0x1766014: the soldier's own divisor of the mouse motion
constexpr unsigned kPadFlagCode=0x1183D70;   // movzx eax, byte ptr [rip+rel32]; ret

// Semantic sticks (enemy_control_camera.h): move right / forward positive, look right / down positive.
struct PadSticks { float right=0,forward=0,lookRight=0,lookDown=0; };

inline float PadFloat(const unsigned char* at,std::size_t offset) noexcept {
    float value=0;std::memcpy(&value,at+offset,sizeof(value));return value;
}
// `pad` the controller object (human+0x340), `record` its [human+0xD40] record, `padPath` the
// game's own choice (+0x2D68 or the 0x1183D70 flag). Both must be readable for kRecordBytes / the sticks.
inline PadSticks ReadPadSticks(const unsigned char* pad,const unsigned char* record,bool padPath) noexcept {
    PadSticks s{};
    if(padPath) {
        s.right=PadFloat(pad,kPadStickRight);s.forward=-PadFloat(pad,kPadStickForward);
        s.lookRight=PadFloat(pad,kPadLookRight);s.lookDown=PadFloat(pad,kPadLookDown);
        return s;
    }
    s.right=(record[kKeyRight] ? 1.0f : 0.0f)-(record[kKeyLeft] ? 1.0f : 0.0f);
    s.forward=(record[kKeyForward] ? 1.0f : 0.0f)-(record[kKeyBack] ? 1.0f : 0.0f);
    s.lookRight=PadFloat(record,kMouseRight)/kMouseStep;s.lookDown=PadFloat(record,kMouseDown)/kMouseStep;
    return s;
}
// The address of the byte 0x1183D70 returns, from its own code (nullptr when the code is not that).
inline const unsigned char* PadFlagAddress(const unsigned char* code) noexcept {
    if(!code || code[0]!=0x0F || code[1]!=0xB6 || code[2]!=0x05 || code[7]!=0xC3)return nullptr;
    int rel=0;std::memcpy(&rel,code+3,sizeof(rel));
    return code+7+rel;
}
}  // namespace crew::enemy_control
