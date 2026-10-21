// The arrival's smoke (booster.cpp SmokeFrame): where the stock LineStripObject lays and draws it. Pure: no game memory;
// tools/smoke_trail_check.cpp runs it offline.
//
// Why (the user, 2026-10-21: 「飞机的入场尾烟与实际偏移了好多」; 2026-10-14: 「入场尾烟在飞机很后面」): the trail was made with
// its exhaust's world matrix as the object's matrix. The object keeps that matrix (EDF.dll 0x1189C00: InitParam+0x08
// copied to obj+0x60 once, by the base constructor) and its renderer draws the strip through it every frame (0x2F6310:
// obj+0x60 to the renderer's +0x260; 0x2F583A: 0x11544F0(.., renderer+0x260, vertices, ..), the world matrix of the
// draw), while the strip's vertices are the nodes as laid (0x2F5AC0 copies each node's +0x00 as it is), and the nodes are
// laid in the world (0x2F6110 -> 0x2F4C10: a node at the emitter's +0x50 as written, no transform). So every node was
// drawn at node x (the nozzle's matrix where the trail began): turned by the jet's heading then and moved by its position
// then (hundreds of metres to kilometres away), the smoke running along beside the jet instead of out of its nozzles.
// The stock's own strips agree: the factory's one stock caller (a debug scene, 0xA01A0) hands it an unturned frame at
// the scene's spot (rsp+0x50: the identity's rows 0xA01ED..0xA023D, its origin (0, 3, 10) at 0xA02AC / 0xA02B3, passed at
// 0xA0413 / 0xA0979 / 0xA0D82), and the strips laid inline in the world (a bullet's, 0x24B904) are drawn through the
// identity at 0x1FFF3D0 (0x24B8EC). Our nodes are world points, so the object's frame is the identity (kObjectFrame);
// where the smoke is laid is the emitter's position, the nozzle's (Emitter).
#pragma once
#include <cstring>

namespace crew::smoke {

// The LineStripObject's own matrix (the factory's InitParam+0x08): its nodes are laid in the world, drawn through this.
alignas(16) inline constexpr float kObjectFrame[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

// The emitter's position (core +0x50: the nozzle's exit, the origin of its world matrix) and the way it lays (core +0x60:
// the flame's, the matrix's +z, rearward; the speeds are 0, so only the cone's axis), the same when the trail is made
// and every frame after.
inline void Emitter(const float* nozzle,float* at,float* way) noexcept {
    const float a[4]={nozzle[12],nozzle[13],nozzle[14],1.0f},w[4]={nozzle[8],nozzle[9],nozzle[10],0.0f};
    std::memcpy(at,a,sizeof a);
    std::memcpy(way,w,sizeof w);
}

// Where the renderer draws a node laid at `node` (world), the object's matrix `frame` (row vectors, affine).
inline void Drawn(const float* frame,const float* node,float* out) noexcept {
    for(int j=0;j<3;++j)out[j]=node[0]*frame[j]+node[1]*frame[4+j]+node[2]*frame[8+j]+frame[12+j];
}

}  // namespace crew::smoke
