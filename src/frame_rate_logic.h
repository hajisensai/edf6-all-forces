// Frame rate above the game's 59.94 Hz with the simulation left at 59.94 Hz (docs/unlimited-fps-re.md): the pure parts,
// checked offline by tools/frame_rate_check.cpp. Nothing here touches the game; the game-side wiring (the frame gate in
// the main loop, the cadence guards, the presentation interpolation) is the plan's later stages.
//
// The model (docs/unlimited-fps-re.md §1–§3, verified on EDF.dll 0x678CCB46):
//  - the main loop (0x11A2F4F) asks the tick clock 0x11DAE60 for n = whole 1/59.94 s ticks since the last boundary and
//    waits while n == 0; Application::Update then gets {dt, n} and System::Update (0x1198DA0) renders first
//    (0x1198FA4 -> 0x11978D0) and steps the scene n times after (0x1198FB0 test r12d; loop 0x1198FD0..0x119972E, the
//    viewport cameras' step 0x118DF40 inside it). A frame with n == 0 therefore renders the state as it is and steps
//    nothing: the presentation frames this file schedules.
//  - so the picture a frame renders is the state after the last step; with the pair (previous step, last step) a
//    presentation frame at phase a since the last boundary shows previous + a*(last - previous), and a tick frame
//    (render before its step) shows the last step, a = 1: the shown time runs one tick behind, without a jump.
#pragma once
#include <cmath>
#include <cstdint>

namespace crew::framerate {
// ini FrameRateCap: 0 the game's own 59.94 Hz (nothing installed), -1 no cap (the GPU and the swap chain's sync
// interval are the limit), 61..1000 at most that many frames a second. 1..60 is no more than the game already gives
// and is taken as 0; anything under -1 as 0; over 1000 as 1000.
constexpr int kOff=0,kUnlimited=-1,kMinCap=61,kMaxCap=1000;
constexpr int FixCap(int raw) noexcept {
    if(raw==kUnlimited)return kUnlimited;
    if(raw<kMinCap)return kOff;
    return raw>kMaxCap ? kMaxCap : raw;
}

// QPC counts between two presentation slots, rounded up so the rate never passes the cap. 0 for off / no cap.
constexpr std::uint64_t PresentPeriod(std::uint64_t freq,int cap) noexcept {
    if(cap<=0 || !freq)return 0;
    const auto c=static_cast<std::uint64_t>(cap);
    return (freq+c-1)/c;
}

// The presentation slots. The loop's count of ticks (0x11DAE60) moves the game's last boundary on, so it is asked only
// once a frame is going to run: first the slot, then the count, and the frame steps whatever ticks the count gives (0 on
// a presentation frame). A step may so wait for the next slot, never more than one slot period; 0x11DAE60 counts from
// boundary to boundary, so the simulation keeps the game's 59.94 Hz over time whatever the slots are. A frame that runs
// takes the slot it came for (the slots keep their phase); one that comes more than a period late (a long frame, a load)
// starts them over from now instead of letting a burst through to catch up.
struct Gate {
    std::uint64_t next=0;   // QPC of the next slot
    bool armed=false;
};
// `cap` as FixCap gives it (not kOff: then the game's own loop runs), `period` PresentPeriod(freq, cap).
constexpr bool Ready(Gate& g,std::uint64_t now,int cap,std::uint64_t period) noexcept {
    if(cap==kUnlimited)return true;
    const auto late=static_cast<std::int64_t>(now-g.next);
    if(g.armed && late<0)return false;
    g.next=(g.armed && late<static_cast<std::int64_t>(period) ? g.next : now)+period;
    g.armed=true;
    return true;
}

// How long the loop may wait before it asks again (a frame that was not ready): to the slot; 0 to ask at once.
constexpr std::uint64_t WaitCounts(const Gate& g,std::uint64_t now,int cap) noexcept {
    if(cap==kUnlimited || !g.armed)return 0;
    const auto toSlot=static_cast<std::int64_t>(g.next-now);
    return toSlot>0 ? static_cast<std::uint64_t>(toSlot) : 0;
}

// The phase a frame renders at, between the step before the last and the last (see the top): 1 on a frame that steps
// (its render comes before its steps), else the part of a tick since the last boundary, held to [0, 1].
inline float RenderAlpha(std::uint64_t ticksThisFrame,std::uint64_t now,std::uint64_t base,std::uint64_t last,
                         std::uint64_t tickPeriod) noexcept {
    if(ticksThisFrame || !tickPeriod)return 1.0f;
    const std::uint64_t since=now-base-last;
    if(since>=tickPeriod)return 1.0f;
    return static_cast<float>(static_cast<double>(since)/static_cast<double>(tickPeriod));
}

// A rigid world matrix in the game's row layout (the player camera's cam+0x220, docs/camera-re.md: rows right, up,
// forward, position; the fourth column copied through): interpolated by a shortest-arc slerp of the rotation and a
// straight line of the position. False (render the last step as it is) when either is not a rotation with a
// translation, when they are mirrored differently, or when the move between them is a cut (more than `maxMove` metres
// or `maxTurn` radians in one tick: a teleport, a camera switch), not motion.
struct Quat { double x,y,z,w; };

inline bool RotationOf(const float m[16],double sign,Quat& q) noexcept {
    double r[3][3];
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)r[i][j]=static_cast<double>(m[i*4+j])*(i==2 ? sign : 1.0);
    // Row vectors: v' = v R. The quaternion of R^T (column convention) from the larger of trace and the diagonal.
    const double t=r[0][0]+r[1][1]+r[2][2];
    if(t>0.0) {
        const double s=std::sqrt(t+1.0)*2.0;
        q={(r[1][2]-r[2][1])/s,(r[2][0]-r[0][2])/s,(r[0][1]-r[1][0])/s,0.25*s};
    } else if(r[0][0]>r[1][1] && r[0][0]>r[2][2]) {
        const double s=std::sqrt(1.0+r[0][0]-r[1][1]-r[2][2])*2.0;
        q={0.25*s,(r[1][0]+r[0][1])/s,(r[2][0]+r[0][2])/s,(r[1][2]-r[2][1])/s};
    } else if(r[1][1]>r[2][2]) {
        const double s=std::sqrt(1.0+r[1][1]-r[0][0]-r[2][2])*2.0;
        q={(r[1][0]+r[0][1])/s,0.25*s,(r[2][1]+r[1][2])/s,(r[2][0]-r[0][2])/s};
    } else {
        const double s=std::sqrt(1.0+r[2][2]-r[0][0]-r[1][1])*2.0;
        q={(r[2][0]+r[0][2])/s,(r[2][1]+r[1][2])/s,0.25*s,(r[0][1]-r[1][0])/s};
    }
    const double n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(!(n>0.0))return false;
    q={q.x/n,q.y/n,q.z/n,q.w/n};
    return true;
}

inline void RowsOf(const Quat& q,double sign,float out[16]) noexcept {
    const double xx=q.x*q.x,yy=q.y*q.y,zz=q.z*q.z,xy=q.x*q.y,xz=q.x*q.z,yz=q.y*q.z,wx=q.w*q.x,wy=q.w*q.y,wz=q.w*q.z;
    const double r[3][3]={{1-2*(yy+zz),2*(xy+wz),2*(xz-wy)},
                          {2*(xy-wz),1-2*(xx+zz),2*(yz+wx)},
                          {2*(xz+wy),2*(yz-wx),1-2*(xx+yy)}};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)out[i*4+j]=static_cast<float>(r[i][j]*(i==2 ? sign : 1.0));
}

// The determinant's sign when the upper 3x3 is orthonormal to `tolerance`, else 0.
inline double RigidSign(const float m[16],double tolerance=1e-3) noexcept {
    for(int i=0;i<16;++i)if(!std::isfinite(m[i]))return 0.0;
    for(int i=0;i<3;++i)for(int j=i;j<3;++j) {
        double d=0.0;
        for(int k=0;k<3;++k)d+=static_cast<double>(m[i*4+k])*m[j*4+k];
        if(std::fabs(d-(i==j ? 1.0 : 0.0))>tolerance)return 0.0;
    }
    const double det=m[0]*(static_cast<double>(m[5])*m[10]-static_cast<double>(m[6])*m[9])
                    -m[1]*(static_cast<double>(m[4])*m[10]-static_cast<double>(m[6])*m[8])
                    +m[2]*(static_cast<double>(m[4])*m[9]-static_cast<double>(m[5])*m[8]);
    return det>0.0 ? 1.0 : -1.0;
}

inline bool InterpolateRigid(const float previous[16],const float current[16],float alpha,float out[16],
                             float maxMove,float maxTurn) noexcept {
    if(!(alpha>=0.0f && alpha<=1.0f))return false;
    const double sp=RigidSign(previous),sc=RigidSign(current);
    if(sp==0.0 || sp!=sc)return false;
    Quat a{},b{};
    if(!RotationOf(previous,sp,a) || !RotationOf(current,sc,b))return false;
    double dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(dot<0.0){b={-b.x,-b.y,-b.z,-b.w};dot=-dot;}
    if(dot>1.0)dot=1.0;
    const double turn=2.0*std::acos(dot);
    double move2=0.0;
    for(int k=0;k<3;++k){const double d=static_cast<double>(current[12+k])-previous[12+k];move2+=d*d;}
    if(turn>maxTurn || move2>static_cast<double>(maxMove)*maxMove)return false;
    const double t=alpha;
    Quat q{};
    if(dot>0.9995) {   // nearly the same: a normalised straight line is exact enough and has no 0/0
        q={a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t};
        const double n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
        q={q.x/n,q.y/n,q.z/n,q.w/n};
    } else {
        const double th=std::acos(dot),s=std::sin(th),wa=std::sin((1.0-t)*th)/s,wb=std::sin(t*th)/s;
        q={a.x*wa+b.x*wb,a.y*wa+b.y*wb,a.z*wa+b.z*wb,a.w*wa+b.w*wb};
    }
    RowsOf(q,sc,out);
    for(int k=0;k<3;++k)out[12+k]=static_cast<float>(previous[12+k]+(static_cast<double>(current[12+k])-previous[12+k])*t);
    out[3]=current[3];out[7]=current[7];out[11]=current[11];out[15]=current[15];
    return true;
}

// The input's edges across presentation frames (docs/unlimited-fps-re.md §2.4). The game's action wrapper update
// 0x1182BC0 runs every frame (0x7058C7 -> 0x77320 -> 0x7CDC0, 154 calls): it clears the wrapper's pressed / released /
// repeat bytes (+0x11 / +0x12 / +0x13) and sets them again only on a change seen this frame. The scene step reads them
// in the next frame's System::Update, before that frame's wrapper update. With frames that do not step, an edge set by
// one frame's wrapper update is cleared by the next one before any step saw it. The shadow keeps what a frame without
// a step is about to clear, and hands it to the next step; the UI (it runs after the wrapper update every frame) still
// sees each edge once, from the native bytes.
struct Edges {
    std::uint8_t pressed=0,released=0,repeat=0;
    constexpr Edges& operator|=(const Edges& o) noexcept {pressed|=o.pressed;released|=o.released;repeat|=o.repeat;return *this;}
    constexpr bool Any() const noexcept {return pressed || released || repeat;}
};
struct Shadow { Edges held{}; };
// In the wrapper update, before the native one clears `pending` (what the last update set): kept when no step has
// read it yet, that is when this frame does not step (its System::Update came and went without a step).
constexpr void BeforeUpdate(Shadow& s,const Edges& pending,std::uint64_t ticksThisFrame) noexcept {
    if(!ticksThisFrame)s.held|=pending;
}
// On a frame that steps, before System::Update: what the step reads is the native bytes and everything kept.
constexpr Edges ForStep(Shadow& s,const Edges& native) noexcept {
    Edges e=native;
    e|=s.held;
    s.held={};
    return e;
}
}  // namespace crew::framerate
