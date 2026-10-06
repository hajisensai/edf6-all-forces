// The mouse's aim and the mouse-aim flight of a helicopter the player flies on the keyboard and mouse (ini HeliMouseAim;
// the user, 2026-10-05: "直升机的俯仰也应该可以被鼠标操控", decided as War Thunder's instructor / Battlefield fly a heli):
//  - the mouse moves an aim in the world as the jets' does (Move: its heading about the world's up, its elevation within
//    kMaxEl of level; OnScreen / KeepOnScreen hold its mark on the screen), and the craft turns its nose toward the aim's
//    heading by itself;
//  - the aim's elevation is where it flies: up climbs, down descends, as steep as the aim at the speed it flies (the
//    climb that keeps the path on the aim's line), a little even at a hover (kHoverClimb), level within kElDead holds the
//    height;
//  - W / S move a forward speed setpoint that stays where they leave it (0 hovers; one press stops at 0 on the way
//    through, a new one goes on into the back speeds), A / D sidestep while held, up / down (Space, the brake key) climb
//    and descend at the craft's most; everything let go it holds its height and flies at the setpoint (0: it stops).
// What a craft is asked (Fly: a horizontal velocity, a climb, a heading) is flown by its own law: a stock heli's input
// block through the NPC pilot's stick law (StockStick, StockThrottle, StockYaw: heli.cpp Steer flies the NPC with the
// same three), the plugin's rotor craft through jet::Hover (playerjet_board.inc HoverStep). The stock heli's attitude
// takes PitchInput separately through PlayerAttitudeHook, leaving the speed channel independent of mouse pitch.
// Pure math (no EDF.dll): tools/heli_aim_check.cpp runs it against the stock heli's flight law (docs/aircraft-re.md) to
// show the signs.
#pragma once
#include "vecmath.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace aim {
// A frame's mouse (the seat's right stick on the keyboard and mouse: the frame's movement, at most 1) turns the aim
// kPerUnit rad a unit, times ini PlayerJetMouseSpeed; its elevation goes no farther than kMaxEl (75 deg) from level (an
// aim already past stays).
constexpr float kPerUnit=0.05f,kMaxEl=1.3f;

// The level right of a heading `dir` (a heading angle a has its nose at (sin a, 0, cos a) and a right turn lowers a:
// docs/player-jet-re.md §2).
inline void RightOf(const float* dir,float* right) noexcept {
    right[0]=-dir[2];right[1]=0.0f;right[2]=dir[0];
    if(!vec::Normalize(right)){right[0]=-1.0f;right[1]=0.0f;right[2]=0.0f;}
}

// The aim (a unit world direction) moved by a frame's mouse: `x` > 0 turns it right, `y` > 0 raises it; `k` rad a unit.
inline void Move(float* aim,float x,float y,float k) noexcept {
    float flat[3]={aim[0],0.0f,aim[2]};
    if(!vec::Normalize(flat)){flat[0]=0.0f;flat[2]=1.0f;}
    float right[3];RightOf(flat,right);
    const float a=x*k,co=std::cos(a),si=std::sin(a);
    for(int i=0;i<3;++i)flat[i]=flat[i]*co+right[i]*si;
    const float was=std::asin(vec::Clamp(aim[1],-1.0f,1.0f));
    const float el=vec::Clamp(was+y*k,was<-kMaxEl ? was : -kMaxEl,was>kMaxEl ? was : kMaxEl);
    aim[0]=flat[0]*std::cos(el);aim[1]=std::sin(el);aim[2]=flat[2]*std::cos(el);
}

// Whether the aim's mark (`mark` m ahead of `pos`) lies within `limit` of the screen's half size (view-projection `vp`,
// row vectors).
inline bool OnScreen(const float* vp,const float* pos,const float* aim,float mark,float limit) noexcept {
    const float p[3]={pos[0]+aim[0]*mark,pos[1]+aim[1]*mark,pos[2]+aim[2]*mark};
    float c[4];
    for(int k=0;k<4;++k)c[k]=p[0]*vp[k]+p[1]*vp[4+k]+p[2]*vp[8+k]+vp[12+k];
    return c[3]>1e-3f && std::fabs(c[0]/c[3])<=limit && std::fabs(c[1]/c[3])<=limit;
}

// The direction from the aircraft to the view centre at `mark` metres from the aircraft. Intersect the centre ray
// with that sphere: unlike the level nose, this is a valid on-screen fallback for a camera looking steeply down.
inline bool ViewCentreAim(const float* pos,const float* eye,const float* view,float mark,float* out) noexcept {
    const float d[3]={eye[0]-pos[0],eye[1]-pos[1],eye[2]-pos[2]};
    const float b=vec::Dot(d,view),q=b*b+mark*mark-vec::Dot(d,d);
    if(!(q>=0.0f) || !(mark>0.0f))return false;
    const float t=-b+std::sqrt(q);
    if(!(t>0.0f))return false;
    for(int i=0;i<3;++i)out[i]=(d[i]+view[i]*t)/mark;
    return vec::Normalize(out);
}

// The stock attitude's forward channel tilts the nose down at +maxTilt; speed has a separate use of that channel
// later in the physics step. A mouse-flown heli substitutes only the attitude copy, so W/S cannot set its pitch.
inline float PitchInput(const float* direction,float maxTilt) noexcept {
    if(!(maxTilt>1e-3f) || !std::isfinite(direction[1]))return 0.0f;
    return vec::Clamp(-std::asin(vec::Clamp(direction[1],-1.0f,1.0f))/maxTilt,-1.0f,1.0f);
}

// The aim kept on the screen (the user, 2026-10-05: the aim ran off the screen and the craft turned on after it, unseen):
// a frame's mouse that took it off is not taken (back to `was`); still off (the camera turned), it is drawn toward `dir`
// (the way the craft flies or faces) until it is on.
inline void KeepOnScreen(const float* vp,const float* pos,float* aim,const float* was,const float* dir,float mark,float limit) noexcept {
    if(OnScreen(vp,pos,aim,mark,limit))return;
    std::memcpy(aim,was,12);
    for(int step=0;step<10 && !OnScreen(vp,pos,aim,mark,limit);++step) {
        float a[3];
        for(int i=0;i<3;++i)a[i]=aim[i]*0.8f+dir[i]*0.2f;
        if(!vec::Normalize(a))break;
        std::memcpy(aim,a,12);
    }
    if(!OnScreen(vp,pos,aim,mark,limit) && OnScreen(vp,pos,dir,mark,limit))std::memcpy(aim,dir,12);
}

// The aim moved by a frame's mouse and kept on the screen one axis at a time: the turn (`x`) taken if its mark stays on
// the screen, then the elevation (`y`) likewise; what still leaves it off (the camera turned) goes to KeepOnScreen.
// (Before, a frame whose mouse took the aim off on one axis was dropped whole: pushed up and right with the aim at the
// screen's right edge it did not rise either, and the heli seemed not to follow the mouse, the user 2026-10-06.)
inline void MoveOnScreen(const float* vp,const float* pos,float* aim,float x,float y,float k,const float* dir,float mark,
                         float limit) noexcept {
    float was[3];std::memcpy(was,aim,12);
    if(x!=0.0f) {
        float a[3];std::memcpy(a,aim,12);
        Move(a,x,0.0f,k);
        if(OnScreen(vp,pos,a,mark,limit) || !OnScreen(vp,pos,aim,mark,limit))std::memcpy(aim,a,12);
    }
    if(y!=0.0f) {
        float a[3];std::memcpy(a,aim,12);
        Move(a,0.0f,y,k);
        if(OnScreen(vp,pos,a,mark,limit) || !OnScreen(vp,pos,aim,mark,limit))std::memcpy(aim,a,12);
    }
    KeepOnScreen(vp,pos,aim,was,dir,mark,limit);
}

// --- The mouse-aim flight (see the top) ---
constexpr float kSetSeconds=3.0f;   // s: W held takes the setpoint from 0 to the top speed (S as fast back)
constexpr float kBackShare=0.3f;    // the fastest back speed, of the top speed
constexpr float kSideShare=0.6f;    // A / D: the sidestep's speed, of the top speed
constexpr float kElDead=0.05f;      // rad (3 deg): an aim this near level holds the height
constexpr float kHoverClimb=2.0f;   // m/s: the climb an aim straight up asks at a hover (sin of its elevation of it)
constexpr float kHoldGain=0.5f,kHoldLead=0.5f;   // 1/s on the height held; the height taken kHoldLead s on (no overshoot)
constexpr float kSettle=2.0f,kSettleShare=0.8f;   // coming down near the ground: kSettleShare of its height a second, at least kSettle

// The keys this frame: W +1 / S -1, D +1 / A -1, up +1 / down -1.
struct Keys { float fore,side,vert; };
// What the flight keeps between frames: the forward speed W / S set (m/s; 0 hovers), the press under way and the
// setpoint it began at, the height held (holding: one is).
struct Hold { float speed,pressFrom,y; bool pressing,holding; };
// What it asks of the craft: a horizontal velocity (m/s, world), a climb (m/s, + up), the level way to face.
struct Want { float vel[3],climb,face[3]; };

// The speed setpoint after this frame's W / S (see the top). `still` (nothing horizontal: on the ground, or the lift-off's
// first moments, heli.cpp kLiftOffMs) holds it at 0: W held through the lift-off would otherwise build a setpoint the
// stick does not fly yet, and the craft lurched to it the moment it was let go.
inline void SetSpeed(Hold& h,float fore,float top,bool still,float dt) noexcept {
    if(still){h.speed=0.0f;h.pressing=false;return;}
    if(fore==0.0f){h.pressing=false;return;}
    if(!h.pressing){h.pressing=true;h.pressFrom=h.speed;}
    const float lo=h.pressFrom>0.0f ? 0.0f : -kBackShare*top,hi=h.pressFrom<0.0f ? 0.0f : top;
    h.speed=vec::Clamp(h.speed+fore*top/kSetSeconds*dt,lo,hi);
}

// The climb: the up / down keys at `most`; else the aim's elevation past kElDead (the path along the aim's line at the
// speed `along` it flies forward, plus kHoverClimb of its sine); else the height held. On the ground the aim lifts
// nothing (Space does). Coming down near the ground (`clear` m over it, < 0: unknown) it slows (kSettle).
inline float Climb(Hold& h,const float* aim,const float* pos,const float* vel,float along,float vert,float most,bool grounded,
                   float clear) noexcept {
    const float el=std::asin(vec::Clamp(aim[1],-1.0f,1.0f));
    const float past=std::fabs(el)>kElDead ? (el>0.0f ? el-kElDead : el+kElDead) : 0.0f;
    float climb=0.0f;
    if(vert!=0.0f){climb=vert*most;h.holding=false;}
    else if(grounded){h.holding=false;}
    else if(past!=0.0f) {
        climb=vec::Clamp(std::tan(past)*(along>0.0f ? along : 0.0f)+std::sin(past)*kHoverClimb,-most,most);
        h.holding=false;
    } else {
        if(!h.holding){h.holding=true;h.y=pos[1]+vel[1]*kHoldLead;}
        climb=vec::Clamp((h.y-pos[1])*kHoldGain,-most*0.5f,most*0.5f);
    }
    if(climb<0.0f && clear>=0.0f)climb=std::fmax(climb,-std::fmax(kSettle,clear*kSettleShare));
    return climb;
}

// The frame's want (see the top): `nose` the craft's level heading (unit), `vel` its velocity (m/s), `top` its top
// speed and `most` its fastest climb, `grounded` on the ground, `still` nothing horizontal yet (grounded, or lifting off:
// SetSpeed), `clear` m over the ground (< 0: unknown).
inline Want Fly(Hold& h,const float* aim,const float* nose,const float* pos,const float* vel,const Keys& k,float top,float most,
                bool grounded,bool still,float clear,float dt) noexcept {
    Want w{};
    SetSpeed(h,k.fore,top,grounded || still,dt);
    float right[3];RightOf(nose,right);
    const float side=grounded || still ? 0.0f : vec::Clamp(k.side,-1.0f,1.0f)*kSideShare*top;
    w.vel[0]=nose[0]*h.speed+right[0]*side;w.vel[2]=nose[2]*h.speed+right[2]*side;
    const float len=std::sqrt(w.vel[0]*w.vel[0]+w.vel[2]*w.vel[2]);
    if(len>top){w.vel[0]*=top/len;w.vel[2]*=top/len;}
    w.face[0]=aim[0];w.face[2]=aim[2];
    if(!vec::Normalize(w.face))std::memcpy(w.face,nose,12);
    const float along=vel[0]*nose[0]+vel[2]*nose[2];
    w.climb=Climb(h,aim,pos,vel,along,k.vert,most,grounded,clear);
    return w;
}

// --- A stock heli's input block for a want (the NPC pilot's law, heli.cpp Steer; docs/heli-input-re.md §2a) ---
// The forward (veh+0x1548) and lateral (+0x1540) stick for the horizontal velocity `want`: full stick flies `top`
// along the heading rows, plus `brake` per m/s it is off, the two at most a full stick together. The heading rows
// (`fwd` row 2, `right` row 0, level units) carry the signs: + forward along row 2, + lateral along row 0.
inline void StockStick(const float* want,const float* vel,float top,float brake,const float* fwd,const float* right,float* forward,
                       float* lateral) noexcept {
    float cv[2]={want[0]/top+(want[0]-vel[0])*brake,want[2]/top+(want[2]-vel[2])*brake};
    const float len=std::sqrt(cv[0]*cv[0]+cv[1]*cv[1]);
    if(len>1.0f){cv[0]/=len;cv[1]/=len;}
    *forward=vec::Clamp(cv[0]*fwd[0]+cv[1]*fwd[2],-1.0f,1.0f);
    *lateral=vec::Clamp(cv[0]*right[0]+cv[1]*right[2],-1.0f,1.0f);
}

// The throttle (veh+0x1544) for a climb: the rotor is the lift and trails the throttle by seconds, so the climb error
// asks for a rotor (`hover`, the one that holds height, plus `climbGain` a m/s off) and the throttle drives the rotor
// there `rotorGain` times as hard as it is off. `hover` learns (`learnGain`) only where `learn` says the climb is not
// saturated (near the height wanted: learning on a climb winds it up and it overshoots).
struct RotorGains { float climb,learn,rotor; };
inline float StockThrottle(float climb,float vy,float rotor,float* hover,bool learn,float dt,const RotorGains& g) noexcept {
    const float err=climb-vy;
    if(learn)*hover=vec::Clamp(*hover+err*g.learn*dt,0.1f,1.0f);
    const float want=vec::Clamp(*hover+err*g.climb,0.0f,1.0f);
    return std::isfinite(rotor) ? vec::Clamp(want+(want-rotor)*g.rotor,0.0f,1.0f) : want;
}

// The yaw (veh+0x1550, before the heli's yaw sign) for a heading `off` rad off the way to face (+: to the left, the
// heading angle growing), the heading turning at `rate` rad/s and the way to face at `faceRate`: 1.5 a rad, damped.
inline float StockYaw(float off,float rate,float faceRate,float damp,float feed) noexcept {
    return vec::Clamp(off*1.5f-(rate-faceRate)*damp+faceRate*feed,-1.0f,1.0f);
}

// The player's yaw (veh+0x1550 before YawSign; the mouse-aim flight, heli.cpp AimFly): a turn rate toward the aim,
// kTurnGain per rad off at most the craft's `most` rad/s (its max yaw rate), asked as that rate's share of `most` plus
// kRateGain of what the turn `rate` (rad/s) still lacks of it. The NPC's StockYaw damps the turn itself (1.2 a rad/s):
// with the aim kept on the screen (at most ~30 deg ahead of a camera that follows the nose) that damping ate half of
// the command and the heli turned 5-17 deg/s however the mouse moved (the user's log, 2026-10-06 15:01).
constexpr float kTurnGain=2.0f,kRateGain=1.5f;
inline float PlayerYaw(float off,float rate,float most) noexcept {
    if(!(most>1e-3f) || !std::isfinite(off+rate))return 0.0f;
    const float want=vec::Clamp(off*kTurnGain,-most,most);
    return vec::Clamp((want+(want-rate)*kRateGain)/most,-1.0f,1.0f);
}

// The rotor that holds a stock heli's height, for its lift per rotor `lift` (veh+0x1610, the SGO's heli_movement[0][1]
// / 60: docs/aircraft-re.md) and mass factor `mass` (+0x161C): kHoverRotor at the 506's lift (34 / 60), as the NPC
// pilot learns it holding its height (the logs: 0.42-0.43), the less the more lift. The old guess took +0x1610 as 70
// (an early note's reading): the 506 (0.567) fell to 0.5 and the 602 (70 / 60) clamped to 1.0, so the 602 the player
// flew climbed on full throttle from 2 to 141 m and its hover was never learned (the user's log, 2026-10-06 15:00).
constexpr float kHoverRotor=0.424f,kHoverLift=34.0f/60.0f;
inline float HoverRotor(float lift,float mass) noexcept {
    if(!(lift>1e-3f) || !(mass>0.0f) || !std::isfinite(lift+mass))return 0.5f;
    return vec::Clamp(kHoverRotor*kHoverLift/lift*mass,0.1f,1.0f);
}

// The heli's yaw sign: slot 57 turns the heading by the yaw input times its max yaw rate (veh+0x1634, the SGO's
// vehicle_setup[1][2]; docs/heli-input-re.md §2), so a craft whose SGO gives it a negative one turns the other way for
// the same input. + grows the heading angle with a positive max yaw rate (every stock heli's: the 506's 23.5 deg).
inline float YawSign(float maxYaw) noexcept { return maxYaw<0.0f ? -1.0f : 1.0f; }
}  // namespace aim
}  // namespace crew
