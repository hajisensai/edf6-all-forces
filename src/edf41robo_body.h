// UfoRobo's body machine (src/edf41robo.cpp), the pure part: which state changes are allowed. Death is final: once the robot is
// dead (Die ran: the ragdoll, the explosion, the drive stopped) the only way is S10 (dying) and from S10 only S11 (the wreck), whatever
// asks for another state (the script's ObjectAction, a guest copy's host events, the machine's own steps). Without this a guest
// copy that died first was pulled back to S7 / S8 / S9 by the host's body events, and a robot that never reached S10 again never
// came to S11 and its deletion (a ghost). No game needed (tests/edf41robo_body_test.cpp).
#pragma once

namespace crew::edf41robo {

constexpr int kDying=10;   // S10: the ragdoll has it, 8 + 7 rand s
constexpr int kWreck=11;   // S11: Explosion_Big, deleted 10 s later

constexpr bool BodyStepAllowed(int from,bool dead,int to) noexcept {
    if(to<0 || to>kWreck)return false;
    if(from==kWreck)return false;           // the wreck stays the wreck
    if(from==kDying)return to==kWreck;      // dying: only on to the wreck
    if(dead)return to==kDying;              // dead, not yet dying (Die's own step): only into dying
    return to<kDying;                       // alive: any living state; dying only through Die
}

}  // namespace crew::edf41robo
