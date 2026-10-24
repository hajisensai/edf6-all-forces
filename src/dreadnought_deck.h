// The rules a Dreadnought's hangars go by (src/dreadnought_hangar.inc, src/dreadnought.cpp), header-only and pure so
// tests/dreadnought_deck_test.cpp checks them offline.
//
// Its hangar modules (the user, 2026-10-24: 「模块能装啥，肯定是装了模块才能有对应的功能啊，比如两个机库模块，就应该两个机库，
// 而不是统一在一个位置」): the hull's four AB_primary sockets, module m 0 left fore (left01), 1 left aft (left03), 2 right
// fore (right01), 3 right aft (right03), each fitted or not (ini DreadnoughtHangarLeftFore .. RightAft, read by the
// installer: a module not fitted is not in the model, nor in its collision: pylib/dreadnought_model.py). Module m holds
// bays 2m and 2m + 1; bay i holds a fighter of kind i % 4. Which are fitted the plugin reads off the ship's model (its
// bones dn_hangar_<m>): what the installer built is what there is.
//
// A player boards from a hangar by standing over it (OverHangar: on its roof or the deck over it) and pressing the board
// button: one of its own bays' fighters (BoardBay) goes out through that bay's opening with them, and comes home to it.
//
// No parachute and no catch for a fighter's pilot getting out (StandsAboard) only when they are certain to be put aboard:
// the hangar running on this machine has its own bay taking that fighter in, held at the bay's opening, still, its pilot
// being put out onto the bridge. Near or over the hull is not enough (the hull's box takes in air off its bow, its beams
// and over its deck: a fighter flying through it would drop its pilot off the ship with no parachute, review
// 2026-10-24); a fighter on its way home, launched, or out of a hangar running on another machine: they eject.
#pragma once

namespace dreadnought {

constexpr int kHangars=4;            // its AB_primary sockets (tools/prep_dreadnought.py HANGAR_SOCKETS)
constexpr int kBaysPerHangar=2;
constexpr int kBays=kHangars*kBaysPerHangar;
constexpr int kKinds=4;              // crew.h kDnFighterKinds
constexpr unsigned kAllHangars=(1u<<kHangars)-1u;

constexpr int HangarOfBay(int bay) noexcept { return bay/kBaysPerHangar; }
constexpr int KindOfBay(int bay) noexcept { return bay%kKinds; }
// The bays a fitting has (bit i: bay i).
constexpr unsigned FittedBays(unsigned hangars) noexcept {
    unsigned out=0;
    for(int i=0;i<kBays;++i)if((hangars>>HangarOfBay(i))&1u)out|=1u<<i;
    return out;
}

// The kinds an escort flight sends (bit k), one of each kind docked in a fitted bay whose kind could be made this
// mission (`failedKinds`, bit k).
constexpr unsigned EscortKinds(unsigned hangars,unsigned docked,unsigned failedKinds) noexcept {
    unsigned out=0;
    const unsigned bays=FittedBays(hangars)&docked;
    for(int i=0;i<kBays;++i)if((bays>>i)&1u && !((failedKinds>>KindOfBay(i))&1u))out|=1u<<KindOfBay(i);
    return out;
}

// The bay a board press over hangar `m` takes the player out of: its first bay with a fighter docked (of a kind that can
// be made this mission), -1 none (or hangar `m` not fitted).
constexpr int BoardBay(unsigned hangars,unsigned docked,unsigned failedKinds,int m) noexcept {
    if(m<0 || m>=kHangars || !((hangars>>m)&1u))return -1;
    for(int k=0;k<kBaysPerHangar;++k) {
        const int i=m*kBaysPerHangar+k;
        if((docked>>i)&1u && !((failedKinds>>KindOfBay(i))&1u))return i;
    }
    return -1;
}

// Where a player on foot stands to board from a hangar: in the ship's frame from its module's origin (dn_hangar_<m>, the
// centre of its roof), `across` (its left), `up` and `along` (ahead), within its footprint and on or over its roof. The
// numbers cover each module's mesh with a margin (pylib/dreadnought_model.py HANGAR_REACH, which tools/make_dreadnought.py
// holds every fitted module to; tools/selftest.py dreadnought_agrees holds these to it).
constexpr float kHangarAcross=28.0f;   // m either side of its origin across the ship (the module 51.7 m wide)
constexpr float kHangarAlong=48.0f;    // m ahead or astern of it (the module 95.6 m long, the next one's origin 96.5 m on)
constexpr float kHangarBelow=3.0f;     // m under its origin (its roof 0.8 m over it)
constexpr float kHangarAbove=15.0f;    // m over it (the hull's deck beside it, 11 m higher, counts)
constexpr float Abs(float x) noexcept { return x<0.0f ? -x : x; }
constexpr bool OverHangar(float across,float up,float along) noexcept {
    return Abs(across)<=kHangarAcross && Abs(along)<=kHangarAlong && up>=-kHangarBelow && up<=kHangarAbove;   // NaN: no
}

constexpr float kOnSpot=3.0f;        // m from its bay's opening (the hangar touches it there within 1.5 m, its pin holds it)
constexpr float kOnSpotSpeed=4.0f;   // m/s over the ship's (the touch's own limit, kFinalSpeed / 3)

// `authority`: the hangar holding it runs on this machine; `alighting`: its own bay has it at its opening, taking it
// in, its pilot being put out (BayJob::alight); `dist`: metres from that opening; `relSpeed`: m/s over the ship's. A NaN
// or a negative (not measured) is no.
constexpr bool StandsAboard(bool authority,bool alighting,float dist,float relSpeed) noexcept {
    return authority && alighting && dist>=0.0f && dist<=kOnSpot && relSpeed>=0.0f && relSpeed<=kOnSpotSpeed;
}

}  // namespace dreadnought
