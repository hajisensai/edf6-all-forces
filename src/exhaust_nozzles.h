// A vehicle's exhaust nozzles, read off its model (booster.cpp: the jets' and the carriers' flames and arrival smoke).
// Pure: no game memory beyond the bone records it is handed; tools/exhaust_nozzles_check.cpp runs it offline.
//
// Why (the user, 2026-10-10: 「尾烟和实际不对。尾烟应该跟着模型生成，而不是用两段可能不同步的代码维护」): the flames and the
// smoke were placed from hand-copied tables (the jets' exits by SGO mark, the carrier's V508 locators x 1.6), kept equal to
// the models only by a self-test's regex. Now the model says where its exhausts are:
//  - the plugin's own models (pylib/jet_models.py with_nozzles) carry bones nozzle_0, nozzle_1, ... (kNozzlePrefix), each
//    transform only, on the bone its exhaust moves with (a jet's body, the carrier's four pods): its local matrix the
//    exit (origin: the exit's centre, +z: the way the flame leaves), its half extents the flame's (length, width) (the
//    engine copies the model's +0xA0 into the record's +0xF0, EDF.dll 0x1111042);
//  - the stock models flown as they are (the bombers a strike jet takes over, the gunship's) cannot carry bones of ours:
//    tools/gen_nozzles.py measures them the same way into nozzles_gen.h (kStockNozzles), a model known by a bone of its own
//    name and its bone count.
// The game's bone records (docs/mdb-format.md, the instance's 0x110-byte records at inst+0x10; EDF.dll 0x1110FC0 fills
// them): +0x00 the name, +0x10 the parent's index, +0x70 the local matrix, +0xB0 the world matrix (local x the parent's
// world, made by the game's update after the input step: a frame old there, exhaust_pose.h), +0xF0 the half extents.
// A nozzle's world this frame is its local on its parent's pose this frame: the parent's record carried by the body's
// motion since it was posed (exhaust_pose.h Carry), with the parent's local as it is now in place of the one it was posed
// with (the carrier's pods are tilted in the input step: their record still holds the last frame's tilt; NozzleWorld).
#pragma once
#include "exhaust_pose.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace crew::exhaust {
constexpr int kMaxNozzles=4;   // a model's nozzles at most (pylib/jet_models.py MAX_NOZZLES)
constexpr std::size_t kRecStride=0x110,kRecName=0x00,kRecParent=0x10,kRecLocal=0x70,kRecWorld=0xB0,kRecHalf=0xF0;
inline constexpr wchar_t kNozzlePrefix[]=L"nozzle_";   // pylib/jet_models.py NOZZLE_BONE

// A stock model's nozzles (nozzles_gen.h): `model` a bone of its own and `bones` its bone count (to know it), `parent` the
// bone they hang on, each one's local matrix on it and flame (length, width).
struct StockNozzles {
    const wchar_t* model;
    int bones;
    const wchar_t* parent;
    int count;
    float local[kMaxNozzles][16];
    float size[kMaxNozzles][2];
};
}  // namespace crew::exhaust

#include "nozzles_gen.h"

namespace crew::exhaust {

// A model's nozzles: each one's parent bone (its record's index), local matrix on it and flame (length, width).
struct NozzleSet {
    int count=0;
    bool stock=false;   // from kStockNozzles (else the model's own bones)
    int parent[kMaxNozzles]{};
    float local[kMaxNozzles][16]{};
    float size[kMaxNozzles][2]{};
};

// The index of the bone named `name` in `count` records at `bones` (the first 16 characters compared, as the game's own
// lookup does: body506.cpp BoneRecord506), -1 with none. `ok(name)`: the name pointer is readable.
template<class Ok>
int BoneIndex(const unsigned char* bones,int count,const wchar_t* name,Ok ok) noexcept {
    for(int i=0;i<count;++i) {
        const wchar_t* n;
        std::memcpy(&n,bones+static_cast<std::size_t>(i)*kRecStride+kRecName,sizeof n);
        if(ok(n) && std::wcsncmp(n,name,16)==0)return i;
    }
    return -1;
}

// The nozzles of the model whose `count` bone records are at `bones`: its own bones nozzle_0, nozzle_1, ... (in order, up
// to the first missing), else the stock table's entry for it, else none. A nozzle whose parent or flame does not make
// sense ends the set there.
template<class Ok>
NozzleSet FindNozzles(const unsigned char* bones,int count,Ok ok) noexcept {
    NozzleSet s;
    for(int k=0;k<kMaxNozzles;++k) {
        wchar_t name[16];
        std::wmemcpy(name,kNozzlePrefix,7);
        name[7]=static_cast<wchar_t>(L'0'+k);name[8]=0;
        const int i=BoneIndex(bones,count,name,ok);
        if(i<0)break;
        const unsigned char* r=bones+static_cast<std::size_t>(i)*kRecStride;
        std::int32_t parent;
        float size[2];
        std::memcpy(&parent,r+kRecParent,4);
        std::memcpy(size,r+kRecHalf,8);
        if(parent<0 || parent>=count || parent==i || !(size[0]>0.0f && size[0]<1e4f && size[1]>0.0f && size[1]<1e4f))break;
        s.parent[k]=parent;
        std::memcpy(s.local[k],r+kRecLocal,64);
        s.size[k][0]=size[0];s.size[k][1]=size[1];
        s.count=k+1;
    }
    if(s.count)return s;
    for(const auto& m:kStockNozzles) {
        if(m.bones!=count || BoneIndex(bones,count,m.model,ok)<0)continue;
        const int p=BoneIndex(bones,count,m.parent,ok);
        if(p<0)continue;
        s.stock=true;s.count=m.count;
        for(int k=0;k<m.count;++k) {
            s.parent[k]=p;
            std::memcpy(s.local[k],m.local[k],64);
            s.size[k][0]=m.size[k][0];s.size[k][1]=m.size[k][1];
        }
        return s;
    }
    return s;
}

// Nozzle `local`'s world this frame on its parent: `parentWorld` the parent's record (posed the frame before, with its
// local `parentPosed` then; nullptr: not seen then, taken as `parentNow`), `parentNow` the parent's local now, `t` the
// body's track (Carry). local x parentNow x parentPosed^-1 x Carry(parentWorld), its rows made unit length (+z: the flame's
// way; the scale of a scaled model is in the flame's size).
inline void NozzleWorld(const float* local,const float* parentNow,const float* parentPosed,const float* parentWorld,
                        const BodyTrack& t,float* out) noexcept {
    float base[16];
    Carry(parentWorld,t,base);
    float inv[16];
    if(parentPosed && std::memcmp(parentPosed,parentNow,64)!=0 && Inverse(parentPosed,inv)) {
        float m[16];
        Mul(inv,base,m);
        Mul(parentNow,m,base);
    }
    Mul(local,base,out);
    for(int r=0;r<3;++r) {
        float* const row=out+r*4;
        const float l=std::sqrt(row[0]*row[0]+row[1]*row[1]+row[2]*row[2]);
        if(l>1e-4f)for(int c=0;c<3;++c)row[c]/=l;
    }
}

}  // namespace crew::exhaust
