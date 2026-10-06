// Offline check of the boardable aircraft's flight numbers (src/playerjet_kinds.h): prints each row as the player's
// flight model gets it, next to the NPC row it comes from, and checks what a flyable row needs (exit code 1 when one
// fails), and the jet the catch makes for each. Built on request only: `cmake --build build --target pjet_kinds`, then `build\pjet_kinds.exe`.
#include "playerjet_kinds.h"
#include <cstdio>

namespace {
using namespace crew;

// What a row must hold to fly well under the player (beyond the header's own static_assert).
const char* Problem(const pjet::Boardable& b) noexcept {
    const pjet::Perf& p=b.perf;
    if(b.frame==pjet::Airframe::rotor)return p.top>=20.0f && p.thrust>=4.0f ? nullptr : "a rotor craft too slow to fly";
    const float oneG=p.corner/pjet::Root(p.maxG);                   // the speed its wing holds 1 g at
    if(p.minAir<oneG)return "idling under its 1 g speed: it sinks level";
    if(p.top-p.minAir<60.0f)return "too narrow a speed range";
    if(p.landMax<p.rotate+10.0f)return "it cannot land at its lift-off speed";
    if(p.top/p.thrust>40.0f)return "too weak an engine (over 40 s to its top speed)";
    return nullptr;
}
}  // namespace

int main() {
    int bad=0;
    std::printf("%-13s %-5s %5s | %6s %6s %6s %6s %6s %5s %6s %5s %7s %5s | NPC cruise/attack/min thrust maxG roll\n","kind","frame",
                "mark","minAir","rotate","top","thrust","brake","maxG","corner","roll","landMax","ram");
    for(const auto& b:pjet::kBoardable) {
        const pjet::Perf& p=b.perf;
        const jet::BodyRow& row=jet::Row(b.body);
        const jet::Kind& n=jet::KindOf(row.role);
        const bool bomber=b.body==jet::Body::bomber401 || b.body==jet::Body::bomber501_2;
        const jet::Kind& from=bomber ? pjet::kStockBomber : n;
        std::printf("%-13s %-5s %5d | %6.1f %6.1f %6.1f %6.1f %6.1f %5.1f %6.1f %5.2f %7.1f %5.1f | %s %.0f/%.0f/%.0f %.0f %.1f %.2f\n",p.name,
                    b.frame==pjet::Airframe::rotor ? "rotor" : "wing",p.mark,p.minAir,p.rotate,p.top,p.thrust,p.brake,p.maxG,p.corner,p.roll,
                    p.landMax,p.ram,bomber ? "stock bomber" : from.name,from.cruise,from.attack,from.minSpeed,from.thrust,from.maxG,from.roll);
        if(const char* why=Problem(b)){std::printf("  FAIL %s: %s\n",p.name,why);++bad;}
        // The jet the catch makes for a player who ejected from it (playerjet_kinds.h catchWith, checked by CatchConsistent).
        std::printf("  catch: %ls (%s): %s\n",pjet::kCatchFiles[b.catchWith].file,pjet::kCatchFiles[b.catchWith].name,b.catchWhy);
    }
    std::printf("%d rows, %d failed\n",pjet::kBoardableCount,bad);
    return bad ? 1 : 0;
}
