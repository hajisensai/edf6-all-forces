// The map's support pt (the user, 2026-10-26: 「所有插件支援改成m里面消耗pt召唤」): pure, no game (tests/support_points_test.cpp
// drives it offline; support_points.cpp feeds it the game's numbers).
// Where the pt come from (docs/plan-2026-10-26-map-support-pt.md §1, H: EDF.dll 0x693E8D..0x693F1E): a stock weapon whose
// ReloadType (+0x208) is 2 (the Air Raider's calls and requests, text "$0pt") is reloaded each frame by its owner's TEAM
// score: the float at row +0x30 of the team manager's (*(0x20B2978)) row array (+0x38, 0x38 a row) for the owner's team
// (+0x314, rows 0..6). The weapon keeps the score it last saw (+0xE80; taken when it is made, 0x68FC85 / 0x69823C) and takes
// the difference off its reload (+0xE68) each frame, a frame's difference past 1500 cut down to 1500 + (it - 1500)^0.8
// (0x693EE8..0x693F0F; 0.8 at 0x176B014, powf at 0x12DA8E2). Every pt weapon of a team charges by the whole difference.
// The bar keeps one pool a machine (its local player's), charged the same way from the same score: a call spends from it.
#pragma once
#include <cmath>
#include <cstdint>

namespace crew::support_points {

constexpr float kFrameCut=1500.0f;   // a frame's score past this is cut down (0x693EE8: cmp eax,0x5DC)
constexpr float kFramePower=0.8f;    // ...to kFrameCut + (the rest)^this (0x176B014)
constexpr int kTeams=7;              // the stock reload reads rows 0..6 (0x693EBF: cmp ecx,7)

// The pt a frame's rise of the team score brings, as the stock reload takes it: whole points (cvttss2si), a rise past
// kFrameCut cut down. The stock code compares the difference unsigned (jbe) and so never meets a fall within a weapon's
// life; a fall here (a new mission's score, a team change) brings nothing.
inline std::int32_t FramePoints(float score,float seen) noexcept {
    if(!std::isfinite(score) || !std::isfinite(seen))return 0;
    const float rise=score-seen;
    if(!(rise>0.0f) || rise>2.0e9f)return 0;
    const auto whole=static_cast<std::int32_t>(rise);
    if(whole<=static_cast<std::int32_t>(kFrameCut))return whole;
    const float cut=std::pow(static_cast<float>(whole-static_cast<std::int32_t>(kFrameCut)),kFramePower);
    return static_cast<std::int32_t>(cut)+static_cast<std::int32_t>(kFrameCut);
}

// The pool's settings (EDF6VehicleCrew.ini: SupportPtStart, SupportPtMost, SupportPtScale).
struct Settings {
    std::int32_t start=6000;   // a mission begins with these (the stock calls begin charged)
    std::int32_t most=30000;   // never more than this (twice the dearest call's 15000)
    float scale=1.0f;          // every price times this (0: everything free)
};

// What an entry of `pt` costs with `scale`: rounded to whole pt, never negative.
inline std::int32_t Price(std::uint32_t pt,float scale) noexcept {
    if(!(scale>0.0f) || !std::isfinite(scale))return 0;
    const double p=static_cast<double>(pt)*static_cast<double>(scale)+0.5;
    return p>=2.0e9 ? 2000000000 : static_cast<std::int32_t>(p);
}

// One machine's pool. `seen`: the team score it last took (none at a mission's start: its first read only sets it).
struct Pool {
    std::int32_t points=0;
    bool seenSet=false;
    float seen=0.0f;
    std::int32_t team=-1;      // whose score `seen` is
    std::int64_t earned=0,spent=0,refunded=0;   // this mission (the log's)

    void Start(const Settings& s) noexcept {
        *this=Pool{};
        points=s.start<0 ? 0 : s.start>s.most ? s.most : s.start;
    }
    // A frame's read of `team`'s score (its row's +0x30). A new team (the player changed sides, the first read) only sets
    // what is seen. Returns the pt it brought.
    std::int32_t Take(std::int32_t teamNow,float score,const Settings& s) noexcept {
        if(teamNow<0 || teamNow>=kTeams || !std::isfinite(score))return 0;
        if(!seenSet || teamNow!=team){seenSet=true;team=teamNow;seen=score;return 0;}
        const std::int32_t got=FramePoints(score,seen);
        seen=score;
        if(got<=0)return 0;
        const std::int32_t room=s.most>points ? s.most-points : 0;
        const std::int32_t in=got<room ? got : room;
        points+=in;earned+=in;
        return in;
    }
    bool Afford(std::int32_t price) const noexcept { return price<=0 || points>=price; }
    // False (nothing taken) when the pool holds less.
    bool Spend(std::int32_t price) noexcept {
        if(price<=0)return true;
        if(points<price)return false;
        points-=price;spent+=price;
        return true;
    }
    // A spend taken back as if it never was: a call's price held while it was asked, the call then not asked
    // (support_points.cpp SupportPointsDropHeld). Not a refund (nothing was paid).
    void Unspend(std::int32_t price) noexcept {
        if(price<=0)return;
        points+=price;spent-=price;
    }
    // A call that was taken and then came to nothing (refused by the host, timed out, its plan refused): its pt back, up
    // to the most.
    void Refund(std::int32_t price,const Settings& s) noexcept {
        if(price<=0)return;
        const std::int64_t back=static_cast<std::int64_t>(points)+price;
        points=back>s.most ? s.most : static_cast<std::int32_t>(back);
        refunded+=price;
    }
};

// Every online call this machine paid for and may still be refunded: its request (support_net's id, known as Submit
// returns: its accepted notice is told inside it) and its price. Settled once: delivered (active) keeps the pt, refused /
// timed out / cancelled / interrupted gives them back.
struct Owed { std::uint32_t request=0; std::int32_t price=0; bool open=false; };
constexpr int kOwedMost=16;
struct Ledger {
    Owed owed[kOwedMost]{};
    // Kept for the request. False: no room (the oldest open one is then forgotten: its pt stay spent).
    bool Keep(std::uint32_t request,std::int32_t price) noexcept {
        if(price<=0 || !request)return true;
        for(Owed& o:owed)if(!o.open){o={request,price,true};return true;}
        for(int i=1;i<kOwedMost;++i)owed[i-1]=owed[i];
        owed[kOwedMost-1]={request,price,true};
        return false;
    }
    // The price owed for `request`, forgotten (0: none open).
    std::int32_t Settle(std::uint32_t request) noexcept {
        for(Owed& o:owed)if(o.open && o.request==request){const std::int32_t p=o.price;o=Owed{};return p;}
        return 0;
    }
    void Clear() noexcept { for(Owed& o:owed)o=Owed{}; }
};

}  // namespace crew::support_points
