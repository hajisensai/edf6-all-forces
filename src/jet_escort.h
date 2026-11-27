// A jet flying escort on a player (the map's follow order, a call's follow variant; jet.cpp Guide), pure: no game, the
// offline jet_escort_check runs it. The player 2026-11-26: 「飞机我觉得也可以让他跟随」 (a follower mod's design borrowed:
// the follower matches its leader's motion, it does not chase where the leader stood).
//  - Observe: the player's velocity, sampled every kSampleMs and blended; a jump past kJumpSpeed (a warp, a respawn)
//    or a gap of over a second starts it over at rest; its level heading kept from the last time it moved.
//  - a player faster than the jet's own least speed (flying a jet: kSlotIn of minSpeed, out under kSlotOut, both over
//    it so the jet can fly that slow): its slot, kSlotBack m behind and kSlotSide m out to its side (wings alternate,
//    each pair farther); the jet flies the player's velocity plus kSlotGain of its gap to the slot a second, so it holds
//    station instead of circling round it (never under its least speed, never past its top);
//  - a slower player (on foot, driving, a hovering heli): a racetrack round them, not a ring round a fixed point: an
//    ellipse kLong times as long as it is wide, along the player's heading, its centre kTrackLead s ahead of them, its
//    half width kTrackWide turns of the jet at its loiter speed (at least kTrackLeast m), each wing a ring step out;
//    flown at the loiter speed plus the player's own velocity (the track moves with them), never under the least
//    speed's kLoiter share, never past its cruise.
// Its height is the caller's (kind.alt over the player); its turn radius at the loiter speed the caller's too
// (jet_flight.cpp TurnRadiusOf).
#pragma once
#include <cmath>
#include <cstdint>

namespace jetescort {
constexpr std::uint64_t kSampleMs=100,kSampleGap=1000;
constexpr float kJumpSpeed=500.0f,kVelBlend=0.25f,kMoving=3.0f;
constexpr float kSlotIn=1.25f,kSlotOut=1.05f;
constexpr float kSlotBack=180.0f,kSlotSide=90.0f,kSlotPair=70.0f,kSlotGain=0.35f;
constexpr float kTrackLead=4.0f,kLong=1.8f,kTrackWide=1.4f,kTrackLeast=450.0f;
constexpr float kLoiter=1.15f;
constexpr int kRings=6;

struct Track {
    float last[3]; float vel[2]; float fwd[2];
    std::uint64_t at; bool set; bool slot;
};
// What the caller's kind gives: m/s its least, cruise and top speeds; m its turn at the loiter speed, its ring step.
struct Params { float minSpeed,cruise,top,turn,step; };
// The level way to fly (unit x, z; y 0) and the speed.
struct Steer { float dir[3]; float speed; bool slot; };

inline float Speed2(const float* v) noexcept { return std::sqrt(v[0]*v[0]+v[1]*v[1]); }
inline float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
inline float LoiterSpeed(const Params& p) noexcept { return Clamp(p.minSpeed*kLoiter,p.minSpeed,p.cruise); }

inline void Observe(Track& t,const float* at,std::uint64_t ms) noexcept {
    if(!t.set || ms<t.at) {
        const bool keep=t.set && (t.fwd[0]!=0.0f || t.fwd[1]!=0.0f);
        const float f0=keep ? t.fwd[0] : 0.0f,f1=keep ? t.fwd[1] : 1.0f;
        t=Track{{at[0],at[1],at[2]},{0.0f,0.0f},{f0,f1},ms,true,false};
        return;
    }
    const std::uint64_t dt=ms-t.at;
    if(dt<kSampleMs)return;
    const float s=static_cast<float>(dt)*0.001f;
    const float vx=(at[0]-t.last[0])/s,vz=(at[2]-t.last[2])/s;
    if(dt>kSampleGap || !std::isfinite(vx) || !std::isfinite(vz) || vx*vx+vz*vz>kJumpSpeed*kJumpSpeed){t.vel[0]=t.vel[1]=0.0f;}
    else{t.vel[0]+=(vx-t.vel[0])*kVelBlend;t.vel[1]+=(vz-t.vel[1])*kVelBlend;}
    const float v=Speed2(t.vel);
    if(v>kMoving){t.fwd[0]=t.vel[0]/v;t.fwd[1]=t.vel[1]/v;}
    t.last[0]=at[0];t.last[1]=at[1];t.last[2]=at[2];t.at=ms;
}

// Its slot behind player `anchor` (heading fwd: x, z), wing `wing`: even wings on the left, odd on the right, each pair
// kSlotPair farther back and out.
inline void SlotOf(const float* anchor,const float* fwd,int wing,float* out) noexcept {
    const int w=wing<0 ? 0 : wing%kRings;
    const float pair=static_cast<float>(w/2);
    const float back=kSlotBack+kSlotPair*pair,side=(w%2 ? 1.0f : -1.0f)*(kSlotSide+kSlotPair*pair);
    const float right[2]={-fwd[1],fwd[0]};
    out[0]=anchor[0]-fwd[0]*back+right[0]*side;
    out[1]=anchor[1];
    out[2]=anchor[2]-fwd[1]*back+right[1]*side;
}

// The racetrack's half length and half width for wing `wing`.
inline void TrackSize(const Params& p,int wing,float* along,float* across) noexcept {
    const int w=wing<0 ? 0 : wing%kRings;
    float b=p.turn*kTrackWide;
    if(!(b>=kTrackLeast))b=kTrackLeast;
    b+=p.step*static_cast<float>(w);
    *across=b;*along=b*kLong;
}

inline Steer Fly(Track& t,const Params& p,const float* pos,const float* anchor,int wing) noexcept {
    Steer s{{0.0f,0.0f,1.0f},LoiterSpeed(p),false};
    const float v=Speed2(t.vel);
    t.slot=t.slot ? v>=p.minSpeed*kSlotOut : v>=p.minSpeed*kSlotIn;
    const float fwd[2]={t.fwd[0],t.fwd[1]};
    float want[2];
    if(t.slot) {
        float slot[3];
        SlotOf(anchor,fwd,wing,slot);
        want[0]=t.vel[0]+(slot[0]-pos[0])*kSlotGain;want[1]=t.vel[1]+(slot[2]-pos[2])*kSlotGain;
        s.speed=Clamp(Speed2(want),p.minSpeed<p.top ? p.minSpeed : p.top,p.top);
        s.slot=true;
    } else {
        float along,across;
        TrackSize(p,wing,&along,&across);
        const float centre[2]={anchor[0]+t.vel[0]*kTrackLead,anchor[2]+t.vel[1]*kTrackLead};
        const float right[2]={-fwd[1],fwd[0]};
        const float dx=pos[0]-centre[0],dz=pos[2]-centre[1];
        const float u=dx*fwd[0]+dz*fwd[1],w=dx*right[0]+dz*right[1];   // along the heading, to its right
        const float nu=u/along,nw=w/across;
        float e=std::sqrt(nu*nu+nw*nw);
        // The way round (the patrol ring's sense): along the ellipse through here, pulled in or out to it.
        float tu=-nw*along,tw=nu*across;
        float nl=std::sqrt(tu*tu+tw*tw);
        if(!(e>1e-3f) || !(nl>1e-3f)){tu=1.0f;tw=0.0f;nl=1.0f;e=0.0f;}
        tu/=nl;tw/=nl;
        float ou=nu/along,ow=nw/across;
        const float ol=std::sqrt(ou*ou+ow*ow);
        if(ol>1e-9f){ou/=ol;ow/=ol;}else{ou=0.0f;ow=0.0f;}
        const float pull=Clamp(1.0f-e,-1.5f,1.5f);
        const float du=tu+ou*pull,dw=tw+ow*pull;
        float dir[2]={fwd[0]*du+right[0]*dw,fwd[1]*du+right[1]*dw};
        const float dl=Speed2(dir);
        if(dl>1e-6f){dir[0]/=dl;dir[1]/=dl;}else{dir[0]=fwd[0];dir[1]=fwd[1];}
        const float loiter=LoiterSpeed(p);
        want[0]=dir[0]*loiter+t.vel[0];want[1]=dir[1]*loiter+t.vel[1];
        s.speed=Clamp(Speed2(want),loiter,p.cruise>loiter ? p.cruise : loiter);
    }
    const float l=Speed2(want);
    if(l>1e-4f){s.dir[0]=want[0]/l;s.dir[2]=want[1]/l;}
    else{s.dir[0]=fwd[0];s.dir[2]=fwd[1];}
    s.dir[1]=0.0f;
    return s;
}
}  // namespace jetescort
