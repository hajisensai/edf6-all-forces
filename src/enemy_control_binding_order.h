// Which fixed key (enemy_control_bindings.h) each native ability of a controlled enemy takes.
// The user (2026-10-21): 「操控怪物绑定的键位不对吧，我怎么左键往右走，右键往后走」. The catalogue
// lists the native pad actions first, in button order (enemy_control_catalog.cpp List), and
// key i was the catalogue's entry i, so a soldier's left button was 跳跃 (pad button 0) and its
// right button 进入／退出掩体 (button 3): the left button moved it, the right one took it back to
// cover. Keys follow what the player's own keys do instead: the left mouse button / RT is the
// family's primary attack (the player's fire), the right button / LT its secondary attack (the
// soldier's 举枪瞄准, as the player's zoom), the other attacks next in catalogue order, and the
// native movement actions (jump, cover, roll, take-off, side step, run, shell, door) last.
// The rank only orders the local key list: ability ids, the catalogue hash and the wire
// protocol are computed in the native order and do not change.
#pragma once
#include "enemy_control_native.h"
#include <cstring>

namespace crew::enemy_native {
enum class BindingRank : unsigned { primary=0, secondary=1, attack=2, motion=3 };

// The rank of pad button `bit` for `family` (names as enemy_control_catalog.cpp PadName).
inline BindingRank PadBindingRank(const char* family,Kind kind,unsigned bit) noexcept {
    const auto is=[family](const char* name) noexcept { return family && std::strcmp(family,name)==0; };
    if(kind==Kind::humanoid) {   // 跳跃 0, 掩体 3, 举枪瞄准 4, 齐射 5, 翻滚 6, 特殊移动 / 快速回避 7, 蹲伏 8
        if(bit==5)return BindingRank::primary;
        if(bit==4)return BindingRank::secondary;
        return BindingRank::motion;
    }
    if(kind==Kind::monster)return BindingRank::motion;   // 侧步回避 4, 奔跑 8: the attack table carries its attacks
    if(is("GiantAnt") || is("GiantAntEX"))return bit==4 ? BindingRank::primary : BindingRank::secondary;   // 喷酸 / 抓取
    if(is("GiantBee") || is("GiantBeeEX") || is("GiantSpider") || is("GiantSpiderEX"))
        return bit==5 ? BindingRank::primary : BindingRank::motion;   // 针刺 / 吐丝; 起飞 / 跳跃
    if(is("GiantDango"))return bit==6 ? BindingRank::primary : BindingRank::motion;   // 滚动攻击; 开始翻滚
    if(is("DragonSmall"))return bit==5 ? BindingRank::primary : bit==6 ? BindingRank::secondary : BindingRank::motion;
    if(is("EDF6_GiantDangoLarge"))return bit==1 ? BindingRank::primary : BindingRank::motion;   // 针弹; 滚动
    if(is("EDF6_ShellFish"))return bit==5 ? BindingRank::primary : BindingRank::motion;   // 炮口射击; 开 / 关甲壳
    if(is("EDF6_SpinnerUfo"))return bit==1 ? BindingRank::primary : BindingRank::motion;   // 旋转炮击; 吸附 / 脱离
    if(is("DropBoat512"))return BindingRank::motion;   // 打开舱门／登陆
    if(is("UfoSmall507") || is("UfoSmall515") || is("EDF6_SquidSmall") || is("Nephila"))return BindingRank::primary;
    if(is("Deiroi"))return bit==5 ? BindingRank::primary : BindingRank::secondary;   // 主炮; 践踏
    if(is("EDF6_FlyingDyno")) {   // 火球 0, 水平火焰 1; 向左 / 向右飞步 7 / 11
        if(bit==0)return BindingRank::primary;
        if(bit==1)return BindingRank::secondary;
        return bit==7 || bit==11 ? BindingRank::motion : BindingRank::attack;
    }
    if(kind==Kind::walking) {   // 跳跃 0, 攻击／抓取保持 4 (Large: 臂炮齐射), 主攻击（依装备） 5
        if(bit==5)return BindingRank::primary;
        if(bit==4)return BindingRank::secondary;
        return BindingRank::motion;
    }
    if(bit==4)return BindingRank::primary;   // the generic 主攻击 / 副攻击 names
    if(bit==5)return BindingRank::secondary;
    return BindingRank::attack;
}
// The rank of one catalogue entry: a pad action by its button, every catalogue command
// (weapons, attack-table entries, actions, guns) is an attack.
inline BindingRank AbilityBindingRank(const NativeProfile& profile,const NativeAbility& ability) noexcept {
    if(ability.kind!=AbilityKind::pad)return BindingRank::attack;
    for(unsigned bit=0;bit<kButtonCount;++bit)
        if(ability.pressed&(1u<<bit))return PadBindingRank(profile.name,profile.kind,bit);
    return BindingRank::attack;
}
}  // namespace crew::enemy_native
