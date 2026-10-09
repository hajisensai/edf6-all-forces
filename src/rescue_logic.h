// The sea rescue's pure decisions (heli.cpp; tests/rescue_logic_test.cpp runs them offline): why a rescue under way is
// called off for the player it was asked for.
//
// 2026-10-10, the user: 「为什么会找不到」. A rescue heli used to look for "the player nearest the request's point within
// 300 m", and with none there it stayed on as a guard support burning its fuel. Now it is the requester's: the player of
// the machine that asked (offline / the host: this machine's; a guest's: the transaction's requester, by its stable
// network identity, support_net.h SupportTransactionRequester). When that player is no longer one to pick up, the rescue
// is called off at once and the heli leaves as support aircraft leave (StartLeave), with the reason logged.
#pragma once

namespace crew::rescue {
enum class Cancel { none, gone, dead, otherRescue, otherVehicle, ashore };
// What the requester is now. `present`: the same object is still there (false: left the room, the mission ended, or the
// requester was never known); `aboard`: in a seat of this rescue's heli; `onFoot`: in no vehicle; `inOtherRescue`: in
// another rescue's heli; `dry`: out of the sea longer than the rescue allows.
struct Requester { bool present,dead,aboard,onFoot,inOtherRescue,dry; };
// Why the pickup is off (none: go on). Once aboard nothing here calls it off (getting off ends it normally).
constexpr Cancel PickupCancel(const Requester& r) noexcept {
    if(!r.present)return Cancel::gone;
    if(r.dead)return Cancel::dead;
    if(r.aboard)return Cancel::none;
    if(!r.onFoot)return r.inOtherRescue ? Cancel::otherRescue : Cancel::otherVehicle;
    return r.dry ? Cancel::ashore : Cancel::none;
}
// The log's words (ASCII: the log is not wide).
constexpr const char* CancelText(Cancel c) noexcept {
    switch(c) {
    case Cancel::gone: return "the requester is gone (left the room, or the mission is over)";
    case Cancel::dead: return "the requester is dead";
    case Cancel::otherRescue: return "the requester was picked up by another rescue";
    case Cancel::otherVehicle: return "the requester boarded another vehicle";
    case Cancel::ashore: return "the requester is out of the sea";
    default: return "no reason";
    }
}
}  // namespace crew::rescue
