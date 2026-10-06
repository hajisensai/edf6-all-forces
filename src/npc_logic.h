// The plugin's NPC AI, its pure part (docs/npc-ai-design.md): the decisions a soldier, a squad and a ground vehicle
// make each frame, with no game and no Windows, so tools/npc_ai_check.cpp can run them offline. The game side
// (src/npcai.cpp) reads the world into these structs, calls these functions and writes the result into the stock
// AI's own outputs.
//  - Fire lanes (B1): the player's lane is the line from the player's eye along their aim, out to what it meets; an NPC
//    stays out of it (LaneEscape) and fires only when no friend stands on its own line or in its round's blast (ShotClear).
//  - Arms (B2, B3): every weapon the soldier carries scored for the target's range and kind; its true reach (the round's
//    speed x life, as the game's own 0x68DB1B keeps it) is the range it fights at (EngageRange).
//  - Crowding (B4, B5): enemies pressing in turn into a way out (Evade): back off, side-step or roll, never stand.
//  - The combat spot (B1, B7): a point on the ring round the target at the engage range, off the player's lane, on the
//    side nearer the NPC.
//  - Squads (B8, C): the next leader when the leader falls, or the squad joined to another; the dismiss cooldown.
//  - The mark (C): a marked enemy comes first when it is within the weapon's reach plus the unit's move radius.
//  - Ground vehicles (D): back to the post after a push (recoil, a ram): reverse when it is close behind, else turn and
//    drive.
#pragma once
#include <cmath>
#include <cstdint>

namespace npc {
constexpr float kPi=3.14159265f;

// --- Vectors (x, y, z; y up) ---
inline float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline float Len(const float* a) noexcept { return std::sqrt(Dot(a,a)); }
inline float Dist(const float* a,const float* b) noexcept {
    const float d[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]};
    return Len(d);
}
inline float Horiz(const float* a,const float* b) noexcept {
    const float dx=b[0]-a[0],dz=b[2]-a[2];
    return std::sqrt(dx*dx+dz*dz);
}
inline float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
inline float Wrap(float a) noexcept {
    while(a>kPi)a-=2.0f*kPi;
    while(a<-kPi)a+=2.0f*kPi;
    return a;
}
// The unit horizontal direction from a to b; false when they stand on one spot.
inline bool HorizDir(const float* a,const float* b,float* out) noexcept {
    const float dx=b[0]-a[0],dz=b[2]-a[2],l=std::sqrt(dx*dx+dz*dz);
    if(!(l>1e-4f))return false;
    out[0]=dx/l;out[1]=0.0f;out[2]=dz/l;
    return true;
}
// How far `p` is from the segment a-b, and where along it (0..1, clamped) the nearest point lies.
inline float SegmentDist(const float* a,const float* b,const float* p,float* along) noexcept {
    const float d[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]},q[3]={p[0]-a[0],p[1]-a[1],p[2]-a[2]};
    const float dd=Dot(d,d);
    const float t=dd>1e-6f ? Clamp(Dot(q,d)/dd,0.0f,1.0f) : 0.0f;
    if(along)*along=t;
    const float c[3]={q[0]-d[0]*t,q[1]-d[1]*t,q[2]-d[2]*t};
    return Len(c);
}

// --- A soldier's intent block (docs/npc-ai-design.md §3.1) ---
// The look a soldier is given towards world direction `dir` as the stock AI gives it (0x5A2B50 at 0x5A2FFB): 0x4E100
// turns the direction into (pitch = -atan2(dy, horizontal), yaw = atan2(dx, dz)); d60 = wrap(pitch - cur pitch) x gain,
// d64 = wrap(yaw - cur yaw) x gain, each clamped to `most` rad a frame. False for a zero direction.
inline bool AimDelta(float curPitch,float curYaw,const float* dir,float gain,float most,float* out) noexcept {
    const float h=std::sqrt(dir[0]*dir[0]+dir[2]*dir[2]);
    if(!(h>1e-6f) && !(std::fabs(dir[1])>1e-6f))return false;
    const float pitch=-std::atan2(dir[1],h),yaw=h>1e-6f ? std::atan2(dir[0],dir[2]) : curYaw;
    out[0]=Clamp(Wrap(pitch-curPitch)*gain,-most,most);
    out[1]=Clamp(Wrap(yaw-curYaw)*gain,-most,most);
    return true;
}
// The aim's error (rad) between the soldier's look (pitch, yaw) and world direction `dir`.
inline float AimError(float curPitch,float curYaw,const float* dir) noexcept {
    const float h=std::sqrt(dir[0]*dir[0]+dir[2]*dir[2]);
    const float pitch=-std::atan2(dir[1],h),yaw=std::atan2(dir[0],dir[2]);
    const float dp=Wrap(pitch-curPitch),dy=Wrap(yaw-curYaw)*std::cos(pitch);
    return std::sqrt(dp*dp+dy*dy);
}
// The local move stick (d50.x, d50.z) for world horizontal direction `dir` at `magnitude` (0..1), the soldier's yaw
// `yaw`: the inverse of 0x56D350's world = x (cos, 0, -sin) + z (sin, 0, cos).
inline void LocalMove(float yaw,const float* dir,float magnitude,float* x,float* z) noexcept {
    const float s=std::sin(yaw),c=std::cos(yaw);
    float lx=dir[0]*c-dir[2]*s,lz=dir[0]*s+dir[2]*c;
    const float l=std::sqrt(lx*lx+lz*lz);
    if(l>1e-6f){lx/=l;lz/=l;}
    *x=lx*magnitude;*z=lz*magnitude;
}
// The world direction of local stick (x, z) at yaw `yaw` (what 0x56D350 does with it).
inline void WorldMove(float yaw,float x,float z,float* out) noexcept {
    const float s=std::sin(yaw),c=std::cos(yaw);
    out[0]=x*c+z*s;out[1]=0.0f;out[2]=-x*s+z*c;
}
// A roll's stick: the stock roll needs |d50.x| over 0.35 (0x1790138); a way out straight back or ahead gets a side
// component of at least `side` towards the side it leans to (right when none).
inline void RollStick(float x,float z,float side,float* ox,float* oz) noexcept {
    const float s=x<0.0f ? -1.0f : 1.0f;
    float ax=std::fabs(x);
    if(ax<side)ax=side;
    const float rz=std::sqrt(1.0f-ax*ax>0.0f ? 1.0f-ax*ax : 0.0f)*(z<0.0f ? -1.0f : 1.0f);
    *ox=s*ax;*oz=std::fabs(z)>1e-6f ? rz : 0.0f;
}

// --- Script control (A, docs/npc-ai-design.md §4.2) ---
// What the stock fields say of a unit: its route (+0x4A8), its root leader (up +0x548) the local player or an NPC with
// a route, its fixed position (+0x380 bit 15), a direction order's frames (+0x4E0), the explorer a Navigation one
// (an escort), it has an NPC leader at all.
struct ScriptFacts { bool route,rootPlayer,rootRouted,fixed,escort,npcLeader; int directionFrames; };
enum class Control : std::uint8_t { free, squad, recruited, script, hold, escort };
// Script control first (the unit's moves left to the stock AI while it lasts), then recruited, in an NPC squad, free.
inline Control Classify(const ScriptFacts& f) noexcept {
    if(f.route || f.rootRouted)return f.escort ? Control::escort : Control::script;
    if(f.fixed || f.directionFrames>0)return Control::hold;
    if(f.rootPlayer)return Control::recruited;
    return f.npcLeader ? Control::squad : Control::free;
}
inline bool Scripted(Control c) noexcept { return c==Control::script || c==Control::hold || c==Control::escort; }

// --- Fire lanes (B1) ---
// The player's lane: from their eye along their aim to `end` (the first wall or enemy, else their weapon's reach),
// `radius` m wide (the spread their rounds and an NPC's body take up).
struct Lane { float from[3],to[3],radius; };
// `p` stands in the lane (between its ends, within its radius).
inline bool InLane(const Lane& lane,const float* p) noexcept {
    float t;
    const float d=SegmentDist(lane.from,lane.to,p,&t);
    return t>0.0f && t<1.0f && d<lane.radius;
}
// The horizontal way out of the lane for `p` (unit, at a right angle to it, the side `p` is already on); false when `p`
// is not in it. Standing right on the line, the lane's right.
inline bool LaneEscape(const Lane& lane,const float* p,float* out) noexcept {
    if(!InLane(lane,p))return false;
    const float d[3]={lane.to[0]-lane.from[0],0.0f,lane.to[2]-lane.from[2]};
    const float l=std::sqrt(d[0]*d[0]+d[2]*d[2]);
    if(!(l>1e-3f))return false;
    const float right[3]={d[2]/l,0.0f,-d[0]/l};
    const float q[3]={p[0]-lane.from[0],0.0f,p[2]-lane.from[2]};
    const float side=Dot(q,right)>=0.0f ? 1.0f : -1.0f;
    for(int i=0;i<3;++i)out[i]=right[i]*side;
    return true;
}
// A friend (or the player) the shot must not touch: where they are and how wide they stand.
struct Friend { float pos[3],radius; };
// A shot from `from` to `to` with a round of blast radius `blast` is clear: no friend within its line (their radius plus
// `spread`) and none within the blast round the point it lands on (plus a margin, the round may fall short).
inline bool ShotClear(const float* from,const float* to,float spread,float blast,const Friend* friends,int n) noexcept {
    for(int i=0;i<n;++i) {
        float t;
        if(SegmentDist(from,to,friends[i].pos,&t)<friends[i].radius+spread && t>0.0f)return false;
        if(blast>0.0f && Dist(to,friends[i].pos)<blast*1.25f+friends[i].radius)return false;
    }
    return true;
}

// --- Arms (B2, B3) ---
enum class TargetKind : std::uint8_t { small, large, air };
// One weapon the soldier carries: its true reach (m: speed x life), its blast radius (0: none), homing, its shots ready
// (ammo in the magazine and not reloading), its damage a second (any consistent measure), whether it is fit to hit
// flyers, and its own minimum range (a blast weapon: the blast must not reach the shooter).
struct Arm { float reach,blast,dps; bool homing,ready,antiAir; };
inline float MinRange(const Arm& a) noexcept { return a.blast>0.0f ? a.blast*1.5f+2.0f : 0.0f; }
// How good arm `a` is against a target `dist` m off of `kind`; below 0: unusable now. A friend near the target rules out
// a blast weapon.
inline float ArmScore(const Arm& a,float dist,TargetKind kind,bool friendNearTarget) noexcept {
    if(!a.ready || !(a.reach>0.0f) || dist>a.reach || dist<MinRange(a))return -1.0f;
    if(friendNearTarget && a.blast>0.0f)return -1.0f;
    float s=a.dps>0.0f ? a.dps : 1.0f;
    if(kind==TargetKind::air)s*=a.homing || a.antiAir ? 3.0f : 0.5f;
    if(kind==TargetKind::large && a.blast>0.0f)s*=1.5f;
    if(kind==TargetKind::small && a.blast>0.0f)s*=1.2f;   // a crowd of small ones: the blast takes several
    // The far end of a reach is where rounds miss most: a little credit for the target well inside it.
    return s*(1.2f-0.4f*dist/a.reach);
}
// The arm to fire at a target `dist` m off (index), -1 when none can. `current` is kept unless another scores a
// quarter better (no switching back and forth every frame).
inline int PickArm(const Arm* arms,int n,int current,float dist,TargetKind kind,bool friendNearTarget) noexcept {
    int best=-1;float bestScore=0.0f;
    for(int i=0;i<n;++i) {
        const float s=ArmScore(arms[i],dist,kind,friendNearTarget);
        if(s>bestScore){best=i;bestScore=s;}
    }
    if(current>=0 && current<n && best!=current) {
        const float keep=ArmScore(arms[current],dist,kind,friendNearTarget);
        if(keep>0.0f && keep*1.25f>=bestScore)return current;
    }
    return best;
}
// The range it fights at: `share` of its longest ready reach (ready or reloading: it closes for the gun it will
// have), no nearer than its blast weapons allow; 0 with no arm.
inline float EngageRange(const Arm* arms,int n,float share) noexcept {
    float reach=0.0f,floor=0.0f;
    for(int i=0;i<n;++i) {
        if(arms[i].reach>reach)reach=arms[i].reach;
        if(MinRange(arms[i])>floor && arms[i].reach>MinRange(arms[i]))floor=MinRange(arms[i]);
    }
    const float r=reach*share;
    return r>floor ? r : (reach>floor ? floor : reach);
}

// --- Crowding (B4, B5) ---
struct Threat { float pos[3],radius; };   // radius: how big it is (its reach to bite or grab)
enum class Move : std::uint8_t { hold, back, sidestep, roll };
struct Evade { Move move; float dir[3]; float pressure; int near; };
// What to do with `threats` round `pos`: each within `danger` m (past its own radius) presses with (1 - gap/danger);
// the way out is away from their weighted centre. A pressure of `crowd` or more, or one within `grab` m: back off;
// one within `grab` with the roll ready: roll (out, along the way out); else side-step across the nearest's line
// when it comes straight on.
inline Evade CrowdResponse(const float* pos,const Threat* threats,int n,float danger,float crowd,float grab,
                           bool rollReady) noexcept {
    Evade e{Move::hold,{0.0f,0.0f,0.0f},0.0f,0};
    float push[3]={0.0f,0.0f,0.0f},nearest=1e30f;int ni=-1;
    for(int i=0;i<n;++i) {
        const float gap=Horiz(pos,threats[i].pos)-threats[i].radius;
        if(gap>=danger)continue;
        const float w=1.0f-Clamp(gap,0.0f,danger)/danger;
        float d[3];
        if(!HorizDir(threats[i].pos,pos,d)){d[0]=1.0f;d[2]=0.0f;}
        for(int k=0;k<3;k+=2)push[k]+=d[k]*w;
        e.pressure+=w;++e.near;
        if(gap<nearest){nearest=gap;ni=i;}
    }
    if(ni<0)return e;
    const float l=std::sqrt(push[0]*push[0]+push[2]*push[2]);
    if(l>1e-4f){e.dir[0]=push[0]/l;e.dir[2]=push[2]/l;}
    else HorizDir(threats[ni].pos,pos,e.dir);   // surrounded evenly: straight away from the nearest
    if(nearest<grab && rollReady){e.move=Move::roll;return e;}
    if(nearest<grab || e.pressure>=crowd){e.move=Move::back;return e;}
    if(e.near==1) {   // one coming on: step across its line
        const float right[3]={e.dir[2],0.0f,-e.dir[0]};
        for(int k=0;k<3;++k)e.dir[k]=right[k];
        e.move=Move::sidestep;
    }
    return e;
}

// --- The combat spot (B1, B7) ---
// Where to fight `target` from: on the ring `range` m round it, at the bearing nearer `pos` of the two `flank` rad either
// side of the player's own bearing from the target (beside the player, never between them and it); with no player,
// straight back from the target towards `pos`.
inline void CombatSpot(const float* pos,const float* target,const float* playerAt,float range,float flank,float* out) noexcept {
    float base[3];
    const float* from=playerAt ? playerAt : pos;
    if(!HorizDir(target,from,base)){base[0]=1.0f;base[2]=0.0f;}
    float a=std::atan2(base[0],base[2]);
    if(playerAt) {
        float mine[3];
        const float me=HorizDir(target,pos,mine) ? std::atan2(mine[0],mine[2]) : a;
        const float l=a-flank,r=a+flank;
        a=std::fabs(Wrap(me-l))<=std::fabs(Wrap(me-r)) ? l : r;
    }
    out[0]=target[0]+std::sin(a)*range;out[1]=pos[1];out[2]=target[2]+std::cos(a)*range;
}

// --- Squads (B8, C) ---
struct Member { std::uint32_t id; bool alive,player; float hp; int rank; float pos[3]; };
// The next leader: the live non-player member of the highest rank, then the most HP, then the lowest id; -1: none left.
inline int PickLeader(const Member* m,int n) noexcept {
    int best=-1;
    for(int i=0;i<n;++i) {
        if(!m[i].alive || m[i].player)continue;
        if(best<0)best=i;
        else if(m[i].rank!=m[best].rank){if(m[i].rank>m[best].rank)best=i;}
        else if(m[i].hp!=m[best].hp){if(m[i].hp>m[best].hp)best=i;}
        else if(m[i].id<m[best].id)best=i;
    }
    return best;
}
// A squad down to `alive` members: join another (the nearest within `joinRange`, of `others` squads' leader points)
// when fewer than `minSize` are left; the index of that squad, -1 to keep going alone.
inline int JoinSquad(int alive,int minSize,const float* at,const float (*others)[3],const int* sizes,int n,int maxSize,
                     float joinRange) noexcept {
    if(alive<=0 || alive>=minSize)return -1;
    int best=-1;float bestD=joinRange;
    for(int i=0;i<n;++i) {
        if(sizes[i]+alive>maxSize)continue;
        const float d=Horiz(at,others[i]);
        if(d<=bestD){bestD=d;best=i;}
    }
    return best;
}
// Dismissed squads wait `cooldownMs` before they may be recruited again (C).
struct Cooldown { std::uint32_t squad; std::uint64_t until; };
template<int N> struct Cooldowns {
    Cooldown list[N]{};
    void Start(std::uint32_t squad,std::uint64_t now,std::uint64_t ms) noexcept {
        Cooldown* slot=&list[0];
        for(auto& c:list) {
            if(c.squad==squad){slot=&c;break;}
            if(c.until<slot->until)slot=&c;   // a free (0) or the soonest done
        }
        slot->squad=squad;slot->until=now+ms;
    }
    bool Ready(std::uint32_t squad,std::uint64_t now) const noexcept {
        for(const auto& c:list)if(c.squad==squad && c.until>now)return false;
        return true;
    }
    std::uint64_t Left(std::uint32_t squad,std::uint64_t now) const noexcept {
        for(const auto& c:list)if(c.squad==squad && c.until>now)return c.until-now;
        return 0;
    }
};

// --- The mark (C) ---
// A marked enemy is taken first by a unit at `pos` when it is within the unit's weapon reach plus how far the unit may
// move from where it stands under its order (`moveRadius`).
inline bool MarkInReach(const float* pos,const float* mark,float reach,float moveRadius) noexcept {
    return Dist(pos,mark)<=reach+moveRadius;
}

// --- Ground vehicles (D) ---
// The way back to `post` for a vehicle at `pos` facing `forward` (its matrix row, horizontal part used): steer (-1..1,
// positive turning towards +heading, i.e. atan2(x, z) growing) and throttle (-1..1, negative: reverse). Within `hold`
// m: none (`active` false). Behind it (more than `backAngle` off the nose) and nearer than `reverseMax` m: it reverses
// onto it (steering with the tail); else it turns towards it, on the spot when more than `turnOnSpot` off.
struct Steer { bool active,reverse; float steer,throttle,dist; };
inline Steer ReturnToPost(const float* pos,const float* forward,const float* post,float hold,float reverseMax,
                          float backAngle,float turnOnSpot) noexcept {
    Steer s{false,false,0.0f,0.0f,Horiz(pos,post)};
    if(s.dist<=hold)return s;
    float to[3];
    if(!HorizDir(pos,post,to))return s;
    const float fl=std::sqrt(forward[0]*forward[0]+forward[2]*forward[2]);
    if(!(fl>0.3f))return s;   // on its side or a wall: no heading to steer by
    const float heading=std::atan2(forward[0],forward[2]);
    float err=Wrap(std::atan2(to[0],to[2])-heading);
    s.active=true;
    const float ramp=Clamp((s.dist-hold)/10.0f,0.25f,1.0f);
    if(std::fabs(err)>backAngle && s.dist<reverseMax) {
        s.reverse=true;
        err=Wrap(err+kPi);                 // the tail onto the post
        s.steer=Clamp(err*2.0f,-1.0f,1.0f);
        s.throttle=-ramp;
        return s;
    }
    s.steer=Clamp(err*2.0f,-1.0f,1.0f);
    s.throttle=std::fabs(err)>turnOnSpot ? 0.0f : ramp;
    return s;
}
}  // namespace npc
