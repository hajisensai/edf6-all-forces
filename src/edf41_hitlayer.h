// The collision layer of a ported 4.1 part's hit bodies (src/edf41mother.cpp), the pure part. No game needed
// (tests/edf41_hitlayer_test.cpp); docs/feedback-2026-10-17-edf41-mother.md 「片状炮台伤害」.
//
// Rockets and missiles (a bullet with an explosion radius) deal their damage only through ApplyAreaDamage (EDF.dll 0x542670: the
// bullet core sets 0x10 and clears the direct-hit queue, 0x23268E): an AABB query whose filter info is layer 13 (0x106070 ->
// 0xD929F0(13, 0, 0, 0)) finds the bodies, and only the objects of the bodies it finds take a damage message. EventAnimationObject's
// constructor moves every body of its ragdoll to layer 0x11 (0x5B6401 0x6E9320(ragdoll, 0x11)); the EDF6 classes whose bodies take
// explosions put theirs on the enemy body layer (UfoCarrier508 0x4F6018 and the insects 0x369A5C: 7). The 2026-10-17 M130 run
// (Edf41DevShootBlast 20, damage 200, hundreds of shots at the 125 enabled panels) took nothing; plain bullets had destroyed 122 / 125
// on 2026-10-15. In 4.1 rockets and missiles damage the panels.
//
// Collision filter info (hkpGroupFilterBase<Config<5, 5, 5, 16>>, composed by 0xD929F0): layer bits 0..4, subsystem 5..9, "don't
// collide with" 10..14, system group 15..30. The filter enables layer pairs by its lookup table (32 words, word a bit b).
#pragma once

#include <cstdint>

namespace crew::edf41layer {

constexpr int kLayers=32;
constexpr int LayerOf(std::uint32_t filterInfo) noexcept { return static_cast<int>(filterInfo&31u); }

// Whether the filter's table lets a query on layer `a` meet a body on layer `b` (the filter's isCollisionEnabled for two layers).
constexpr bool Sees(const std::uint32_t* table,int a,int b) noexcept {
    return table && a>=0 && a<kLayers && b>=0 && b<kLayers && ((table[a]>>b)&1u)!=0;
}

// The layer a part's hit bodies should be on: where the area damage's query meets them, as 4.1's do.
//  - the game's table read (non-null): the part's own layer if the query meets it; else the hull's (EDF6's own layer for the
//    carrier's damageable bodies) if the query meets that; else unchanged (nothing better known, said in the log);
//  - no table (not found in this build): the hull's layer, on the evidence of the M130 run that layer 0x11 is not met.
constexpr int HitLayer(int part,int hull,int areaQuery,const std::uint32_t* table) noexcept {
    if(part<0 || part>=kLayers)return part;
    if(hull<0 || hull>=kLayers)return part;
    if(!table)return hull;
    if(Sees(table,areaQuery,part))return part;
    if(Sees(table,areaQuery,hull))return hull;
    return part;
}

}  // namespace crew::edf41layer
