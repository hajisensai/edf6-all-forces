// The Katyusha's 16 rockets on their rails as its launcher's state shows them (the user, 2026-10-07: 「补齐应有动画和模型。
// 例如喀秋莎射火箭弹」): each rocket leaves its rail as it is fired and the rack is loaded again before the next salvo.
// Pure: no game memory; src/katyusha.cpp poses each rocket's bone from it, tools/katyusha_rack_check.cpp runs it offline.
//  - The model: one bone per rocket (pylib/katyusha_model.py ROCKET_BONES, under the launcher), rocket k fired from
//    muzzle k (MUZZLES, at its tail). The weapon fires muzzle n % count for its n-th round since its full load (fire-start
//    0x690C84 and the burst step 0x6940EA: AmmoCount - rounds left -> slot 16 0x6B3640: % the muzzle count): a salvo of
//    16 (tools/make_katyusha.py ROCKETS) takes rocket 0, 1, .. 15 off in that order.
//  - The salvo: the first round sets the burst's rounds still to come, FireBurstCount - 1 (0x690C75, weapon +0xE18), each
//    further one takes one off (0x6940DF): the rockets fired so far are FireBurstCount - that. (Each round also takes one
//    off the rounds left, 0x69820E, while weapon +0xBE4 is set: the weapon base's constructor sets it, 0x68E8A9, only
//    another weapon class's clears it, 0x6AA657; so an NPC crew's launcher counts its rounds too and its salvos take the
//    rockets in the same order.)
//  - The loading: after a salvo's last round the weapon waits FireInterval (+0x36C) frames (+0xE0C counts it down,
//    0x6981E0 / 0x693A58) before the next: the rack is loaded over the first kLoadedBy of that wait, rocket 0 first, each
//    one put on at its rail's back end and pushed kLoad forward onto its stop (every rocket on its stop before the next
//    salvo may go). A weapon with no rounds left (AmmoCount spent; the Katyusha's
//    ReloadTime is -1: for good) has its rack empty, or, with a reload, loaded over the reload (+0xE68 frames left of
//    ReloadTime +0x20C, the stock gauge's own reckoning 0x692100).
#pragma once

namespace crew::rack {
constexpr int kRockets=16;          // pylib/katyusha_model.py ROCKET_COUNT
constexpr float kLoad=0.8f;         // m a rocket slides being loaded: pylib/katyusha_model.py LOAD (ROCKET_TAIL - RAIL_BACK)
constexpr float kLoadedBy=0.9f;     // the share of the wait (or the reload) the rack is loaded over

// What the weapon's state says (weapon offsets in src/katyusha.cpp).
struct Launcher {
    int rounds;         // +0xBE8 rounds left of AmmoCount
    int burst;          // +0x370 FireBurstCount
    int burstLeft;      // +0xE18 rounds of this burst still to come
    float wait;         // +0xE0C frames until it may fire again
    int interval;       // +0x36C FireInterval (frames)
    int reloadTime;     // +0x20C ReloadTime (frames; < 0 none)
    int reloadLeft;     // +0xE68 frames of the reload left
};

inline float Clamp01(float v) noexcept { return v<0.0f ? 0.0f : v>1.0f ? 1.0f : v; }

// Where each rocket is: on[k] 0 off the rack (fired, not loaded again), 1 on its stop, between: being loaded, (1 - on[k])
// x kLoad short of its stop. A state it cannot read as one of the above (a negative count, ...) shows the rack full.
inline void Rockets(const Launcher& w,float* on) noexcept {
    float loaded=static_cast<float>(kRockets);   // how much of the rack is loaded, rocket 0 first
    int fired=0;                                  // the first `fired` rockets are off it this salvo
    if(w.rounds<=0) {
        const bool reload=w.reloadTime>0 && w.reloadLeft>=0 && w.reloadLeft<=w.reloadTime;
        loaded=reload ? (1.0f-static_cast<float>(w.reloadLeft)/static_cast<float>(w.reloadTime))/kLoadedBy*kRockets : 0.0f;
    } else if(w.burstLeft>0 && w.burst>w.burstLeft) {
        fired=w.burst-w.burstLeft;
    } else if(w.burstLeft==0 && w.interval>0 && w.wait>0.0f) {
        loaded=(1.0f-w.wait/static_cast<float>(w.interval))/kLoadedBy*kRockets;
    }
    if(w.rounds>0 && w.rounds<kRockets && kRockets-w.rounds>fired)fired=kRockets-w.rounds;   // fewer rounds than rockets
    for(int k=0;k<kRockets;++k)on[k]=k<fired ? 0.0f : Clamp01(loaded-static_cast<float>(k));
}
}  // namespace crew::rack
