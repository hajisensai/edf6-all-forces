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

#include <cstddef>
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

// ---- the Earth Eater's (401) parts: 4.1's own layers carried over by the two games' filter tables ---------------------------------------
// (docs/edf41-ufomother401.md 2.6, docs/raycast-re.md 3.1; tests/edf41_hitlayer_test.cpp; selftest edf41_mother401_parts_collision_layer)
//
// Both games' CollisionFilter constructors start from an all-on table, turn every layer 1..N-1 off against all but layer 0
// (hkpGroupFilter::disableCollisionsUsingBitfield(1 << l, 0xFFFFFFFE)) and enable a fixed list of pairs (enableCollisionsBetween).
// EDF6 EDF.dll 0x105510 (N = 30, 0xD92A90 / 0xD92E50, 141 calls) and EDF41.exe 0xEBC70 (N = 28, 0x5E7100 / 0x5E7050, 118 calls),
// the pairs in the order the code makes them (a few twice, as the code does):
inline constexpr int kEdf6FilterLayers=30,kEdf41FilterLayers=28;
inline constexpr unsigned char kEdf6Pairs[][2]={
    {1,1},{1,6},{1,7},{1,15},{1,16},{1,20},{1,17},{1,18},{1,19},{1,26},{2,15},{2,16},{2,17},{2,19},{2,26},{2,8},{3,15},{3,16},{3,17},
    {3,19},{3,3},{3,26},{4,17},{4,19},{4,4},{4,26},{10,15},{10,16},{10,20},{10,17},{10,19},{10,18},{10,6},{10,7},{10,8},{10,5},{10,14},
    {11,6},{11,7},{11,8},{11,9},{11,5},{11,14},{12,15},{12,16},{12,20},{12,17},{12,19},{12,18},{13,15},{13,16},{13,20},{13,19},{13,6},
    {13,7},{13,8},{13,5},{13,14},{13,9},{14,10},{14,1},{6,1},{6,10},{6,6},{6,15},{6,16},{6,20},{6,17},{6,19},{6,18},{6,26},{7,1},{7,3},
    {7,4},{7,10},{7,6},{7,7},{7,15},{7,16},{7,20},{7,17},{7,19},{7,18},{7,26},{8,1},{8,3},{8,10},{8,6},{8,7},{8,8},{8,15},{8,16},{8,20},
    {8,17},{8,19},{8,18},{8,26},{9,1},{9,10},{9,15},{9,16},{9,17},{9,19},{9,18},{9,26},{21,15},{21,16},{21,17},{21,19},{21,26},{21,8},
    {22,15},{22,16},{22,20},{22,17},{22,19},{22,18},{22,26},{23,17},{24,15},{24,16},{24,17},{24,24},{25,1},{25,2},{25,3},{25,6},{25,7},
    {27,15},{27,20},{27,16},{27,17},{27,18},{28,28},{28,15},{28,16},{28,20},{28,17},{28,19},{28,18},{28,26}
};
inline constexpr unsigned char kEdf41Pairs[][2]={
    {1,1},{1,6},{1,7},{1,13},{1,14},{1,17},{1,15},{1,16},{1,24},{2,13},{2,14},{2,17},{2,15},{2,24},{2,8},{3,13},{3,14},{3,17},{3,15},
    {3,3},{3,24},{4,15},{4,4},{4,24},{9,13},{9,14},{9,17},{9,15},{9,16},{9,6},{9,7},{9,8},{9,5},{9,12},{10,6},{10,7},{10,8},{10,5},
    {10,12},{11,13},{11,14},{11,17},{11,15},{11,16},{12,9},{12,1},{6,1},{6,9},{6,6},{6,13},{6,14},{6,17},{6,15},{6,16},{6,24},{7,1},
    {7,3},{7,4},{7,9},{7,6},{7,7},{7,13},{7,14},{7,17},{7,15},{7,16},{7,24},{8,1},{8,3},{8,9},{8,6},{8,7},{8,8},{8,13},{8,14},{8,17},
    {8,15},{8,16},{8,24},{18,13},{18,14},{18,17},{18,15},{18,16},{18,24},{18,8},{19,13},{19,14},{19,17},{19,15},{19,16},{19,24},{20,14},
    {20,15},{21,13},{21,14},{21,15},{21,21},{22,22},{22,14},{22,15},{23,1},{23,2},{23,3},{23,6},{23,7},{25,13},{25,17},{25,14},{25,15},
    {25,16},{26,26},{26,13},{26,14},{26,17},{26,15},{26,16},{26,24}
};

struct LayerTable { std::uint32_t row[kLayers]; };
// The filter's lookup table as the constructor leaves it (word a bit b: a meets b).
template<std::size_t N>
constexpr LayerTable BuildTable(int layers,const unsigned char (&pairs)[N][2]) noexcept {
    LayerTable t{};
    for(std::uint32_t& w:t.row)w=0xFFFFFFFFu;
    for(int l=1;l<layers;++l) {
        const std::uint32_t a=1u<<l,b=0xFFFFFFFEu;
        for(int i=0;i<kLayers;++i) {
            if((a>>i)&1u)t.row[i]&=~b;
            if((b>>i)&1u)t.row[i]&=~a;
        }
    }
    for(std::size_t i=0;i<N;++i) {
        const int x=pairs[i][0],y=pairs[i][1];
        t.row[x]|=1u<<y;
        t.row[y]|=1u<<x;
    }
    return t;
}
inline constexpr LayerTable kEdf6Table=BuildTable(kEdf6FilterLayers,kEdf6Pairs);
inline constexpr LayerTable kEdf41Table=BuildTable(kEdf41FilterLayers,kEdf41Pairs);

// The queries that matter here. EDF6: 22 is the map ray (GetGroundPosition 0x208860, CastRayCreatePosition 0x1D7490, 66 users of the
// stub 0x106100), 13 the area damage's AABB query (0x106070). 4.1: every hkpWorld::castRay caller (0x602FD0 / 0x6030D0: 11 of 13
// sites, the spawn position 0x14D0F0 among them) casts with filter info 0x13: layer 19.
inline constexpr int kEdf6MapRay=22,kEdf6AreaQuery=13,kEdf41MapRay=19;
// The character / body layers both games share by number (rows 1..8 meet each other the same way in both tables).
inline constexpr int kBodyLayerFirst=1,kBodyLayerLast=8;

// 4.1's layer for a 401 part (EDF41.exe). PanelBase's shell 0x26FAB0 (every panel class, the core too): the SGO's
// is_keyframe_ragdoll != 0 -> 0x334370(ragdoll, 7), else 0xEEC00 (`or r8d, 0xD`): 13. The batteries (GunBatteryBase 0x2691E0)
// build their ragdoll with 0x3327E0 and never move it: the ragdoll default of 0x333FB0 (`or ecx, 6`), 6.
inline constexpr int k41PanelLayer=13,k41KeyframeLayer=7,k41BatteryLayer=6;
constexpr int Part41Layer(bool battery,bool keyframe) noexcept {
    return battery ? k41BatteryLayer : keyframe ? k41KeyframeLayer : k41PanelLayer;
}

// Whether EDF6 layer `l6` stands for 4.1 layer `l41`: it meets the body layers 1..8 exactly as `l41` does in 4.1, the map ray meets it
// exactly when 4.1's map ray meets `l41`, and the area damage's query meets it (4.1's rockets damage the parts; EDF6's area damage
// reaches only what layer 13 meets).
constexpr bool Stands(const LayerTable& t6,const LayerTable& t41,int l6,int l41) noexcept {
    if(l6<1 || l6>=kLayers || l41<1 || l41>=kLayers)return false;
    for(int c=kBodyLayerFirst;c<=kBodyLayerLast;++c)
        if(Sees(t6.row,l6,c)!=Sees(t41.row,l41,c))return false;
    return Sees(t6.row,kEdf6MapRay,l6)==Sees(t41.row,kEdf41MapRay,l41) && Sees(t6.row,kEdf6AreaQuery,l6);
}
// The lowest EDF6 layer that stands for 4.1 layer `l41` (-1: none). 4.1 13 -> 15 (a map / structure layer as 13 is in 4.1:
// 4.1's map ray 19 meets it, EDF6's 22 meets 15; BuildingObject's), 7 -> 7, 6 -> 6.
constexpr int Edf6For41(int l41,const LayerTable& t6=kEdf6Table,const LayerTable& t41=kEdf41Table) noexcept {
    for(int l=1;l<kEdf6FilterLayers;++l)
        if(Stands(t6,t41,l,l41))return l;
    return -1;
}
// The live table (read from the physics world) agrees with the constructor's on the rows these choices rest on.
constexpr bool LiveAgrees(const std::uint32_t* live,int l6,const LayerTable& t6=kEdf6Table) noexcept {
    if(!live || l6<1 || l6>=kLayers)return false;
    return live[l6]==t6.row[l6] && live[kEdf6MapRay]==t6.row[kEdf6MapRay] && live[kEdf6AreaQuery]==t6.row[kEdf6AreaQuery];
}

}  // namespace crew::edf41layer
