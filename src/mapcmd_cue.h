#pragma once
// The M map's feedback for an order (the user, 2026-11-27: 「按v上下高度一点反馈没有」; Broken Arrow answers every order):
// which cue an order given plays, and the selection's altitude as the card shows it. Pure: map_buttons_check tests it.
#include <cstdint>

namespace mapcue {

// The cue an order plays (jetaudio kClipCmd*): refused (none took it) two low notes; ALTITUDE up / down a rising /
// falling three; any other order taken a short double.
enum class Cue : std::uint8_t { none, ack, up, down, refuse };
constexpr Cue ForOrder(bool altitude,bool high,int given) noexcept {
    if(given<=0)return Cue::refuse;
    if(altitude)return high ? Cue::up : Cue::down;
    return Cue::ack;
}

// The active selection's aircraft by altitude: this machine's flying high / low; another machine's (its authority
// alone knows: heli.cpp / jet.cpp keep `high`, the map is not told) unknown.
struct Altitudes { int high=0,low=0,unknown=0; };
enum class AltState : std::uint8_t { none, high, low, mixed, unknown };
constexpr AltState StateOf(const Altitudes& a) noexcept {
    if(a.high+a.low==0)return a.unknown ? AltState::unknown : AltState::none;
    if(a.high && a.low)return AltState::mixed;
    return a.high ? AltState::high : AltState::low;
}
// What V gives now (mapcmd.cpp): up unless every known one is high already (none known: up).
constexpr bool VGoesHigh(const Altitudes& a) noexcept { return a.low>0 || a.high==0; }

// A switched aircraft's tag blinks this long after the switch, on / off every kBlinkMs.
constexpr unsigned kSwitchMs=3000,kBlinkMs=250;
constexpr bool TagLit(unsigned sinceMs) noexcept { return sinceMs>=kSwitchMs || (sinceMs/kBlinkMs)%2u==0u; }

}  // namespace mapcue
