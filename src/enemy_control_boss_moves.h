// The moves of the controlled 银之人 (E516_BIGGREYBOSS, Humanoid_BigGreyBoss): exactly the
// ones the original game gives it. The user (2026-10-25): 「银之人使用回旋激光崩溃了。并且多了
// 很多这个角色没有的攻击方式，比如举枪瞄准」.
//
// Evidence (Steam 678CCB46, read-only; docs/enemy-control-skills-re.md, E516):
// - Its embedded ai_script picks its attacks only in SelectAttack_Phase1..3: HandBeam (Native1/230
//   arg 4), HandHoming (230 arg 5), BathingKick (Native1/102 attack_bathingKick), Dive (102
//   attack_dive), Meteor (Native1/101 attack_meteorite). AiAttack_HandSpear, AiAttack_DestroyBeam
//   (attack_rollingLazer) and AiAttack_Genocide are bodies no selector or event ever calls.
// - attack_rollingLazer's only animation event (the SGO's MAB event table) is parts_action
//   {name 中心砲台, no 0}. E516's `parts` has no 中心砲台 (EDF6 ships no centre cannon on it:
//   e516_biggreyBoss_CenterMuzzle.sgo is referenced by no SGO), so the handler 443B90 ->
//   part lookup 4444B0 misses and returns the hash list's sentinel node, whose payload +0x40/+0x48
//   was never constructed: the strong-count increment there runs on garbage (the crash).
//   attack_genocide's only event is 隕石群, the meteor shower again; it is no cannon either.
// - The base Humanoid decodes pad 4 aim (+11F1), 5 trigger (+11F0), 3 cover (+11F4) and 8 crouch
//   (+11F2) for an armed soldier (470E7C..470EB4). E516 has no `weapon` (no arm equipment), and
//   its AI issues none of the soldier natives behind them (E506 soldiers: aim/fire 1/150 +
//   table-3 fire, cover 1/40 1/41, sit 1/151; E516's script calls none of these). They are not
//   its moves: 举枪瞄准 / 装备武器齐射 bound to the mouse buttons were the user's "举枪瞄准".
//   Locomotion stays: jump 0, roll 6 and pad 7, which its own slot 5 (444E90) consumes itself
//   (474560 -> its dodge state 447A90; AiDodge).
#pragma once
#include "enemy_control_native.h"
#include <cwchar>

namespace crew::enemy_native::boss_moves {
// Native ObjectAction codes (444C10, message 0x1000000D) of the hand muzzles' attacks.
inline constexpr AbilityKind kHand=AbilityKind::objectAction;
inline constexpr std::int32_t kHandBeam=4,kHandHoming=5;
// The catalogue, in key order (enemy_control_binding_order.h: attacks keep this order, so the
// left / right mouse buttons are the two hand beams the boss fires most).
inline constexpr NativeAbility kAbilities[]={
    {"boss_hand_beam",L"手部光束",0,0,kHand,nullptr,kHandBeam},
    {"boss_hand_homing",L"追踪光束",0,0,kHand,nullptr,kHandHoming},
    {"boss_bathing_kick",L"飞踢",0,0,AbilityKind::namedAction,L"attack_bathingKick",0},
    {"boss_dive",L"俯冲攻击",0,0,AbilityKind::namedAction,L"attack_dive",0},
    {"boss_meteor",L"陨石攻击",0,0,AbilityKind::namedAction,L"attack_meteorite",0},
};
// The named actions in wire-key order (enemy_control_command_actions.cpp: key = index here).
inline constexpr const wchar_t* kNamed[]={L"attack_bathingKick",L"attack_dive",L"attack_meteorite"};
inline constexpr unsigned kNamedCount=sizeof(kNamed)/sizeof(kNamed[0]);
// The native pad buttons it keeps: jump 0, roll 6, its own quick dodge 7.
inline constexpr unsigned kDodgeButton=7;
inline constexpr std::uint16_t kPadButtons=(1u<<0)|(1u<<6)|(1u<<kDodgeButton);
inline bool PadAllowed(unsigned bit) noexcept {return bit<16 && ((kPadButtons>>bit)&1u)!=0;}

inline bool HandCode(std::int64_t code) noexcept {return code==kHandBeam || code==kHandHoming;}
inline bool NamedKey(const wchar_t* state,unsigned* key) noexcept {
    if(!state)return false;
    for(unsigned i=0;i<kNamedCount;++i)if(!std::wcscmp(state,kNamed[i])) {if(key)*key=i;return true;}
    return false;
}
// Every catalogue entry is one of the wire tables, so a listed move is always sendable.
constexpr bool Consistent() noexcept {
    unsigned named=0,hand=0;
    for(const auto& a:kAbilities) {
        if(a.kind==AbilityKind::namedAction) {
            if(named>=kNamedCount)return false;
            const wchar_t* x=a.action;const wchar_t* y=kNamed[named++];
            while(*x && *x==*y){++x;++y;}
            if(*x!=*y)return false;
        } else if(a.kind==kHand) {
            if(a.argument!=kHandBeam && a.argument!=kHandHoming)return false;++hand;
        } else return false;
    }
    return named==kNamedCount && hand==2;
}
static_assert(Consistent(),"E516 catalogue and accepted-command tables list the same original moves");
}  // namespace crew::enemy_native::boss_moves
