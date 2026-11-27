// heli.cpp 的任务阶段（docs/reviews/design-review-1126.md 第 8 步）：一架由插件驾驶的直升机「现在在执行什么任务」，
// 和它的飞法（heli.cpp Mode：每帧按优先级选的 rescue / land / circle / run / escort / guard …）分开。
// 以前是 struct Heli 里的 guard / leaving / reap / ferry / ferryLand / ownGuard 六个 bool，合法组合没有定义；
// 现在 State 是唯一来源，heli.cpp 只经 Transition() 改它，非法转移被拒绝（返回 false，状态不变）。
// 不依赖游戏，离线可测（tests/heli_phase_test.cpp）。
//
// 不在这里的（都不是任务阶段）：
//   called / keep / medic  —— 这架直升机的属性（被呼叫来的、小队的运输机、医疗机），设一次不随任务变；
//   extend                 —— 交战中的战术（脱离再切入），每次交战自己清；
//   orbiting / orbitSet    —— 护卫盘旋的日志记忆与盘旋中心的初始化，属于飞控；
//   cmdMoving              —— 地图指令的「先到位再接敌」滞回（mapcmd.h AirCommandTransit，与战机共用）。
#pragma once
#include <cstdint>

namespace crew::heliphase {

enum class Phase : std::uint8_t {
    follow,     // 跟着玩家（护航 / 盘旋 / 在玩家旁降落）；没有玩家时原地悬停
    guard,      // 守在岗位上（护卫呼叫、地图的守卫指令）：绕岗位盘旋
    ferry,      // 运送小队：飞到 ferryAt 上方悬停，不跟人、不交战
    ferryLand,  // ……并降落在那里
    leaving,    // 退场中（StartLeave）：不交战，飞远或超时后进入 gone
    gone,       // 已退场：等别的对象的更新把它删掉（HeliReap）
};

enum class Event : std::uint8_t {
    follow,     // 跟随：跟随呼叫、地图的跟随指令
    guard,      // 守卫：护卫呼叫、地图的守卫指令
    release,    // 地图指令解除：回到第一次被指挥之前的值守（State::own）
    ferry,      // HeliFerry(at,false)
    ferryLand,  // HeliFerry(at,true)
    ferryEnd,   // HeliFerry(nullptr)
    leave,      // StartLeave：燃料 / 弹药 / 受损，或地图的撤退指令
    reap,       // 离玩家够远或退场够久：删除
};

struct State {
    Phase phase=Phase::follow;
    // 它的值守（只取 follow / guard）：运送时保留，运送结束回到它；退场后也保留（护卫盘旋关闭的日志按它判断）。
    Phase duty=Phase::follow;
    // 第一次被地图指挥前的值守（只取 follow / guard）：指令解除时放回（HeliCommand）。
    Phase own=Phase::follow;
};

constexpr bool IsDuty(Phase p) noexcept { return p==Phase::follow || p==Phase::guard; }
constexpr bool IsFerry(Phase p) noexcept { return p==Phase::ferry || p==Phase::ferryLand; }
constexpr bool IsOut(Phase p) noexcept { return p==Phase::leaving || p==Phase::gone; }

// 读法（旧 bool 的含义，一一对应）。
constexpr bool Guard(const State& s) noexcept { return s.duty==Phase::guard; }        // 旧 guard
constexpr bool Ferrying(const State& s) noexcept { return IsFerry(s.phase); }          // 旧 ferry
constexpr bool FerryLands(const State& s) noexcept { return s.phase==Phase::ferryLand; }  // 旧 ferryLand
constexpr bool Leaving(const State& s) noexcept { return IsOut(s.phase); }             // 旧 leaving
constexpr bool Gone(const State& s) noexcept { return s.phase==Phase::gone; }          // 旧 reap

// 不变量：duty / own 只取 follow / guard；值守阶段时 phase 就是 duty。
constexpr bool Valid(const State& s) noexcept {
    return IsDuty(s.duty) && IsDuty(s.own) && (!IsDuty(s.phase) || s.phase==s.duty);
}

// 一次转移：合法则改 s 并返回 true；非法（退场后再派任务、没在运送却「降落」之外的情形见下）返回 false，s 不变。
//   follow / guard / release：值守阶段直接换；运送中只换 duty（运送照旧，结束后回到新值守）；退场后拒绝。
//   ferry / ferryLand：值守或运送中都可（换点、换降落与否）；退场后拒绝。
//   ferryEnd：运送中回到 duty；本来就在值守时什么也不变、算接受（HeliFerry(nullptr) 一向返回 true）；退场后拒绝。
//   leave：退场前的任何阶段都进 leaving（运送中退场也丢下运送，见 heli.cpp StartLeave）；已在退场拒绝。
//   reap：只从 leaving 进 gone。
constexpr bool Transition(State& s,Event e) noexcept {
    const bool out=IsOut(s.phase);
    switch(e) {
    case Event::follow:
    case Event::guard:
    case Event::release: {
        if(out)return false;
        const Phase duty=e==Event::release ? s.own : e==Event::guard ? Phase::guard : Phase::follow;
        s.duty=duty;
        if(IsDuty(s.phase))s.phase=duty;
        return true;
    }
    case Event::ferry:
    case Event::ferryLand:
        if(out)return false;
        s.phase=e==Event::ferryLand ? Phase::ferryLand : Phase::ferry;
        return true;
    case Event::ferryEnd:
        if(out)return false;
        s.phase=s.duty;
        return true;
    case Event::leave:
        if(out)return false;
        s.phase=Phase::leaving;
        return true;
    case Event::reap:
        if(s.phase!=Phase::leaving)return false;
        s.phase=Phase::gone;
        return true;
    }
    return false;
}

// 第一次被地图指挥时记下现在的值守（HeliCommand：之后的指令解除放回它）。
constexpr void KeepOwn(State& s) noexcept { s.own=s.duty; }

// 旧的六个 bool 推导出的状态（切换写入路径前后的等价核对，tests/heli_phase_test.cpp）。
// 旧代码能产生的组合：ferryLand 只与 ferry 同在；reap 只在 leaving 之后；leaving 与 ferry 同在只有一种来路——
// 运送中的被呼叫直升机受损（LeaveReason → StartLeave 没清 ferry），新状态里归为 leaving（运送丢下）。
constexpr State Derive(bool guard,bool ferry,bool ferryLand,bool leaving,bool reap,bool ownGuard) noexcept {
    State s{};
    s.duty=guard ? Phase::guard : Phase::follow;
    s.own=ownGuard ? Phase::guard : Phase::follow;
    s.phase=reap ? Phase::gone : leaving ? Phase::leaving : ferry ? (ferryLand ? Phase::ferryLand : Phase::ferry) : s.duty;
    return s;
}

constexpr const char* Name(Phase p) noexcept {
    switch(p) {
    case Phase::follow: return "follow";
    case Phase::guard: return "guard";
    case Phase::ferry: return "ferry";
    case Phase::ferryLand: return "ferry (landing)";
    case Phase::leaving: return "leaving";
    case Phase::gone: return "gone";
    }
    return "?";
}

}  // namespace crew::heliphase
