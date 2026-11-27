// 战机（jet*.cpp）的任务阶段（docs/reviews/design-review-1126.md 第 8 步）：一架由插件驾驶的战机「现在在执行什么任务」，
// 和它的飞法（jet_internal.h Mode：takeoff / patrol / approach / dive … 每帧的战术）分开。
// 以前「退场」「回母舰」是 Mode 的两个值（withdraw / recover），「运送伞兵」是 ferry / ferryGone 两个 bool，
// 三者能同时为真（运送中燃料耗尽：mode=withdraw 且 ferry 仍为 true），由各处 if 的先后顺序隐式决定谁赢。
// 现在 Phase 是唯一来源，jet*.cpp 只经 Transition()（jet_flight.cpp EnterPhase）改它，非法转移被拒绝。
// 不依赖游戏，离线可测（tests/jet_phase_test.cpp）。
//
// 不在这里的（都不是任务阶段）：
//   reap      —— 待删除（JetReap 等座位上没人才删），与阶段正交：回母舰对接、退场飞出视野、运送最后一程飞出地图、
//                世界边缘、JetPilot 关闭、自爆、收进机库，都只是「删掉它」，删除前照原阶段继续飞；
//   launched / escort / thrown / crewless / liftFans / supportDrone …  —— 这架战机的属性（怎么来的、什么机体）；
//   entered   —— 呼叫来的战机第一轮攻击开始与否（入场战术，jet_internal.h Entering）；
//   held      —— 被无畏舰机库托着（物理约束，按帧新鲜度），任何阶段都可能；
//   cmdMoving —— 地图指令的「先到位再接敌」滞回（mapcmd.h AirCommandTransit，与直升机共用）。
#pragma once
#include <cstdint>

namespace crew::jetphase {

enum class Phase : std::uint8_t {
    fly,       // 绕它的锚点（打击点、玩家、地图指令的点）巡逻与作战
    ferry,     // 运送伞兵：沿穿过投放点的直线往返（ferry_line.h），不接敌；最后一程飞出地图后删除
    recover,   // 无人机回母舰（Recover / Dock）
    withdraw,  // 退场：背离玩家爬升飞走，飞出视野后删除（Away）
};

enum class Event : std::uint8_t {
    ferry,     // JetFerry：开始运送（或换一个投放点）
    recover,   // 回母舰（Leave 的回收理由、RecallDrones）
    withdraw,  // 退场（燃料 / 受损 / 弹尽 / 母舰丢失 / 地图撤退指令 / 伞兵投完）
    resume,    // 取消退场或回收：玩家坐上炮手位（GunnerHold）、机库放飞（JetHangarRelease）
};

constexpr bool Leaving(Phase p) noexcept { return p==Phase::withdraw || p==Phase::recover; }

// 一次转移：合法则改 p 并返回 true；非法返回 false，p 不变。
//   ferry：只从 fly / ferry（换点）；回母舰或退场中的拒绝（以前会把 ferry 置真但被 withdraw / recover 盖住）。
//   recover：只从 fly（只有母舰的无人机会回收，它们不运送）。
//   withdraw：fly / ferry / recover 都可（运送中退场丢下运送；母舰没了的无人机直接退场）；已在退场拒绝。
//   resume：只从 withdraw / recover 回到 fly。
constexpr bool Transition(Phase& p,Event e) noexcept {
    switch(e) {
    case Event::ferry:
        if(Leaving(p))return false;
        p=Phase::ferry;
        return true;
    case Event::recover:
        if(p!=Phase::fly)return false;
        p=Phase::recover;
        return true;
    case Event::withdraw:
        if(p==Phase::withdraw)return false;
        p=Phase::withdraw;
        return true;
    case Event::resume:
        if(!Leaving(p))return false;
        p=Phase::fly;
        return true;
    }
    return false;
}

// 旧状态推导出的阶段（切换写入路径前后的等价核对，tests/jet_phase_test.cpp）：旧 Mode 是否 withdraw / recover，与 ferry。
// 旧代码里 withdraw / recover 压过 ferry（Guide 的 switch 在 ferry 分支之前返回，Commandable / SoftEdgeHolds 同样），
// 所以 ferry 与之同在时归为退场或回收。
constexpr Phase Derive(bool withdraw,bool recover,bool ferry) noexcept {
    return withdraw ? Phase::withdraw : recover ? Phase::recover : ferry ? Phase::ferry : Phase::fly;
}

constexpr const char* Name(Phase p) noexcept {
    switch(p) {
    case Phase::fly: return "fly";
    case Phase::ferry: return "ferry";
    case Phase::recover: return "recover";
    case Phase::withdraw: return "withdraw";
    }
    return "?";
}

}  // namespace crew::jetphase
