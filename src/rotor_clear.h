// Rotor clearance for the NPC helis (the user, 2026-10-11: "直升机在城区里面可能在大楼之间会蹭到桨叶"). Pure logic, no
// game state: heli.cpp Avoid calls it with its map ray; tools/rotor_clear_check.cpp tests it offline.
//
// The stock heli's Havok body is the SGO's heli_rigid_body box (docs/aircraft-collision.md): the 506's is 2.8 m half
// wide, its rotor 4.76 m in radius (docs/player-jet-re.md), the 410's rotor 20 m across. Only the box meets the map; the
// rotor is a contact query that only deals damage (roter_contact_damage_scale, docs/jet-model-re.md). And Avoid's rays
// all leave the body centre with no width, the side ones 30 deg off the way it flies: nothing looks beside it. So a
// heli slid along a street, or hovered by a tower, with its blades through the wall and its box clear of it.
//
// Clear: kRays horizontal rays all round from the rotor's height (kLift over the body centre), each its rotor's radius
// plus kMargin plus what it takes to stop from its speed long. A hit whose normal is no more than kWallNormal up or down
// is a wall (a floor, roof or slope is not one: the height rays see those); its gap is the distance to its plane along
// the normal (three rays on one flat wall give one gap). Per wall:
//  - the wanted velocity into it is cut to what still stops kGapMin off the disc's edge at `decel`;
//  - inside the disc plus kMargin it is pushed out at kPushGain m/s per metre short (one push per wall, the pushes of
//    walls either side adding up: between two it goes to the middle; all of them at most kPushMax);
//  - inside the disc plus kOverlap (a street narrower than the disc, or pressed against one) the wanted height goes
//    kOverRoof over the wall's top (`roofAt` a metre past the hit), unless `climb` is off (landing: it has to come down;
//    the sea rescue low over the water).
#pragma once
#include <cmath>

namespace crew {
namespace rotorclear {
constexpr int kRays=8;
constexpr float kLift=2.0f;          // m: the rotor over the body centre (the 506's box centre 1.45 m over its origin)
constexpr float kMargin=3.0f;        // m past the disc's edge a wall starts pushing
constexpr float kGapMin=1.0f;        // m past the disc's edge the speed into a wall plans to stop
constexpr float kOverlap=1.0f;       // m past the disc's edge a wall makes it climb over
constexpr float kStopLook=30.0f;     // m at most the rays reach further for its stopping distance
constexpr float kPushGain=1.2f;      // m/s out per metre short of its clearance
constexpr float kPushMax=8.0f;       // m/s
constexpr float kWallNormal=0.7f;    // |normal.y| at most: a wall
constexpr float kSameWall=0.95f;     // two hits' normals this close (cosine): one wall
constexpr float kOverRoof=6.0f;      // m over a wall's top it climbs to (Avoid's kGroundClear)
constexpr float kPi=3.14159265f;

// nearest: metres from the rotor's centre to the nearest wall (-1: none within reach), dir the way to it (x, z);
// walls: how many walls; roof: the highest wall top it climbs over (-1e9: none).
struct Result { float nearest,dir[2]; int walls; float roof; };

// `ray(from, to, hit, normal)`: metres along from->to to the nearest map hit, or < 0 with none. `roofAt(point)`: the
// map's top under that point (y), or below -1e8 with none. `vel`: its velocity, `decel`: the braking it plans with
// (m/s2); `want`: the wanted velocity (x, z used); `height`: the wanted height.
template<class Ray,class Roof>
Result Clear(const float* pos,const float* vel,float radius,float decel,float* want,float* height,bool climb,
             Ray&& ray,Roof&& roofAt) noexcept {
    Result r{-1.0f,{0.0f,0.0f},0,-1e9f};
    if(!(radius>0.0f))return r;
    if(!(decel>0.5f))decel=0.5f;
    const float keep=radius+kMargin;
    const float speed2=vel[0]*vel[0]+vel[2]*vel[2];
    float stop=speed2/(2.0f*decel);
    if(!(stop<kStopLook))stop=kStopLook;
    const float reach=keep+stop;
    const float from[3]={pos[0],pos[1]+kLift,pos[2]};
    struct Wall { float n[2],gap,out; } walls[kRays];   // n: the way into it
    int count=0;
    for(int i=0;i<kRays;++i) {
        const float a=2.0f*kPi*static_cast<float>(i)/static_cast<float>(kRays);
        const float d[2]={std::sin(a),std::cos(a)};
        const float to[3]={from[0]+d[0]*reach,from[1],from[2]+d[1]*reach};
        float hit[3]={0.0f,0.0f,0.0f},normal[3]={0.0f,0.0f,0.0f};
        const float dist=ray(from,to,hit,normal);
        if(!(dist>=0.0f) || dist>reach)continue;
        if(std::fabs(normal[1])>kWallNormal)continue;
        // The way into the wall: its horizontal normal turned to face along the ray (a facing normal may point either
        // way, map_floor.h); with none readable, the ray itself.
        float n[2]={normal[0],normal[2]};
        float nl=std::sqrt(n[0]*n[0]+n[1]*n[1]);
        if(nl<0.1f){n[0]=d[0];n[1]=d[1];nl=1.0f;}
        n[0]/=nl;n[1]/=nl;
        float along=n[0]*d[0]+n[1]*d[1];
        if(along<0.0f){n[0]=-n[0];n[1]=-n[1];along=-along;}
        if(along<0.2f)continue;   // grazing along a wall: the rays square to it see it
        const float gap=dist*along;
        if(climb && gap<radius+kOverlap) {
            const float past[3]={hit[0]+n[0],hit[1],hit[2]+n[1]};
            const float top=roofAt(past);
            if(top>-1e8f && top>r.roof)r.roof=top;
        }
        const float out=gap<keep ? (keep-gap)*kPushGain : 0.0f;
        int k=0;
        while(k<count && walls[k].n[0]*n[0]+walls[k].n[1]*n[1]<kSameWall)++k;
        if(k==count){walls[count++]=Wall{{n[0],n[1]},gap,out};}
        else if(gap<walls[k].gap){walls[k].gap=gap;walls[k].out=out;}
    }
    float push[2]={0.0f,0.0f};
    for(int k=0;k<count;++k) {
        const Wall& w=walls[k];
        if(r.nearest<0.0f || w.gap<r.nearest){r.nearest=w.gap;r.dir[0]=w.n[0];r.dir[1]=w.n[1];}
        const float room=w.gap-radius-kGapMin;
        const float allowed=room>0.0f ? std::sqrt(2.0f*decel*room) : 0.0f;
        const float into=want[0]*w.n[0]+want[2]*w.n[1];
        if(into>allowed){want[0]-=w.n[0]*(into-allowed);want[2]-=w.n[1]*(into-allowed);}
        push[0]-=w.n[0]*w.out;push[1]-=w.n[1]*w.out;
    }
    r.walls=count;
    const float len=std::sqrt(push[0]*push[0]+push[1]*push[1]);
    if(len>kPushMax){push[0]*=kPushMax/len;push[1]*=kPushMax/len;}
    want[0]+=push[0];want[2]+=push[1];
    if(r.roof>-1e8f && *height<r.roof+kOverRoof)*height=r.roof+kOverRoof;
    return r;
}
}  // namespace rotorclear
}  // namespace crew
