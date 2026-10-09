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
// What a craft is asked (a horizontal velocity, a climb, a heading) is flown by its own law: the plugin's rotor craft
// fly Fly's want through jet::Hover (playerjet_board.inc HoverStep). A stock heli (2026-10-09) flies the Instructor
// below instead (War Thunder's mouse aim: the aim is the heading and pitch wanted, nose down flies forward, W / S the
// collective), through StockStick, CollectiveThrottle and PlayerYawInput, the pitch and the coordinated roll handed to its
// attitude function by heli.cpp PlayerAttitudeHook.
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
// The horizontal velocity and the way to face of a want (Fly's and Instructor's): `speed` along the level `nose`, the
// A / D `side` (-1..1) at kSideShare of `top` across it (none while `still`), the two at most `top`; the aim's heading.
inline void Course(Want& w,const float* aim,const float* nose,float speed,float side,float top,bool still) noexcept {
    float right[3];RightOf(nose,right);
    const float across=still ? 0.0f : vec::Clamp(side,-1.0f,1.0f)*kSideShare*top;
    w.vel[0]=nose[0]*speed+right[0]*across;w.vel[1]=0.0f;w.vel[2]=nose[2]*speed+right[2]*across;
    const float len=std::sqrt(w.vel[0]*w.vel[0]+w.vel[2]*w.vel[2]);
    if(len>top){w.vel[0]*=top/len;w.vel[2]*=top/len;}
    w.face[0]=aim[0];w.face[1]=0.0f;w.face[2]=aim[2];
    if(!vec::Normalize(w.face))std::memcpy(w.face,nose,12);
}

inline Want Fly(Hold& h,const float* aim,const float* nose,const float* pos,const float* vel,const Keys& k,float top,float most,
                bool grounded,bool still,float clear,float dt) noexcept {
    Want w{};
    SetSpeed(h,k.fore,top,grounded || still,dt);
    Course(w,aim,nose,h.speed,k.side,top,grounded || still);
    const float along=vel[0]*nose[0]+vel[2]*nose[2];
    w.climb=Climb(h,aim,pos,vel,along,k.vert,most,grounded,clear);
    return w;
}

// --- The instructor: a stock helicopter the player flies on the keyboard and mouse (heli.cpp AimFly) ---
// The user (2026-10-09): 「这个直升机的飞控依旧怪怪的，参考战雷做吧」. Fly above made the aim's elevation a climb and W / S a
// speed setpoint, while the nose took the aim's pitch: the nose dipped while the heli sank straight down, sat level
// while W flew it at full speed, and the stock rotor's seconds-long lag under the height hold porpoised it (heli_aim_check
// "before" rows). War Thunder's mouse-aim instructor instead takes the aim as the attitude wanted and flies the controls
// to it:
//  - heading: the aim's (the tail rotor, PlayerYawInput);
//  - pitch: the aim's elevation, at most the craft's max tilt (AimPitch); the cyclic flies it: nose down forward, the
//    farther the faster (full tilt the top speed), nose up slows and backs (at most kBackShare of the top), within
//    kElDead of level it hovers (CyclicSpeed). The stock heli's own forward stick is both its tilt and its speed (its
//    0x654E69 and 0x651E2F read the same input), so the nose and the motion agree;
//  - roll: the sidestep's, plus a coordinated turn's bank (BankInput: tan bank = speed x turn rate / g);
//  - W / S (and Space / the brake key) the collective: climb / descend at the most while held, let go it holds the
//    height it comes to (Collective, the same hold as Fly's); A / D slide.
inline float AimPitch(const float* aim,float maxTilt) noexcept {
    if(!(maxTilt>1e-3f) || !std::isfinite(aim[1]))return 0.0f;
    return vec::Clamp(std::asin(vec::Clamp(aim[1],-1.0f,1.0f)),-maxTilt,maxTilt);
}

// The forward speed the cyclic flies at a `pitch` (rad, + nose up) of at most `maxTilt`.
inline float CyclicSpeed(float pitch,float maxTilt,float top) noexcept {
    const float span=maxTilt-kElDead,down=-pitch;
    if(!(span>1e-3f) || std::fabs(down)<=kElDead)return 0.0f;
    const float past=down>0.0f ? down-kElDead : down+kElDead;
    return vec::Clamp(past/span,-kBackShare,1.0f)*top;
}

// The climb the collective asks: Fly's Climb with a level aim (no climb from the aim): the keys, else the height held.
inline float Collective(Hold& h,const float* pos,const float* vel,float vert,float most,bool grounded,float clear) noexcept {
    const float level[3]={0.0f,0.0f,1.0f};
    return Climb(h,level,pos,vel,0.0f,vert,most,grounded,clear);
}

// The instructor's want (see above) and its pitch (`pitch`, rad, + nose up). `vert` the collective keys (-1..1), `side` A / D.
// h.speed is the forward speed the cyclic flies (the HUD's speed target).
inline Want Instructor(Hold& h,const float* aim,const float* nose,const float* pos,const float* vel,float vert,float side,float top,
                       float most,float maxTilt,bool grounded,bool still,float clear,float* pitch) noexcept {
    Want w{};
    *pitch=AimPitch(aim,maxTilt);
    h.pressing=false;
    h.speed=grounded || still ? 0.0f : CyclicSpeed(*pitch,maxTilt,top);
    Course(w,aim,nose,h.speed,side,top,grounded || still);
    w.climb=Collective(h,pos,vel,vert,most,grounded,clear);
    return w;
}

// The coordinated turn's bank in the stock lateral channel (veh+0x1540, whose roll is -maxTilt x it, 0x654E84): the
// centripetal acceleration `speed` x `turn` (m/s, rad/s toward the heading's row 0) tilted into, as a sidestep the same
// way tilts it (the channel's own sign carries the handedness).
constexpr float kGravity=9.8f;
inline float BankInput(float speed,float turn,float maxTilt) noexcept {
    if(!(maxTilt>1e-3f) || !std::isfinite(speed*turn))return 0.0f;
    return vec::Clamp(std::atan(speed*turn/kGravity)/maxTilt,-1.0f,1.0f);
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

// The instructor's collective (veh+0x1544): the stock heli's vertical law inverted, on a rotor whose lag the player's
// frames shorten (heli.cpp PlayerMouseTune: its up / down rates raised to kPlayerRotorRate; the stock 0.001 / 0.0007 a
// frame, 0x656744 / 0x656770, take 17 / 24 s and porpoised the height hold).
// The vertical law (slot 57, 0x651D6B-0x651F47, read 2026-10-09): hover = M g / (60 L) (0x651D9B), t = (rotor - idle) /
// (hover - idle) within 0..1 (0x651EF4), vy = (1 + (vdamp - 1) t) vy + rotor L (0x651F35), Havok's gravity after it:
// with G = hover L, a frame vy' = (1 + (vdamp - 1) t(rotor)) vy + rotor L - G. Under the hover the damping fades with the
// rotor (at the idle none): a straight line from the hover (the old feed-forward) asked a rotor at the idle for any
// descent and the heli fell at 15 m/s for a 6 m/s one.
// ClimbRotor solves it for the rotor that brings vy kVerticalShare of the way to the climb in one frame; RotorThrottle
// then puts the rotor there in one native step (rotor += rate (throttle - rotor)), clamped to 0..1. `hover` (G / L: the
// game's gravity is not 9.8) is learned from the climb's error, kPlayerHoverLearn of it a second, only while `learn` (the
// height held, near it), the heli about still in height (under kLearnBand m/s: stopping a climb, the error is the law's
// own easing, not the hover's, and learning it wound the hover off by a fifth and crept the height for seconds; a wrong
// hover instead holds it still a little off the height, the hold's climb then the error to learn) and the throttle is
// not clamped (a saturated rotor is no measure of the hover).
constexpr float kPlayerRotorRate=0.05f,kVerticalShare=1.0f/12.0f,kPlayerHoverLearn=0.1f,kLearnBand=1.0f;
struct Rotor { float lift,vdamp,idle,up,down; };   // veh+0x1610, +0x1618, +0x1BD4 (heli_roter[3]), +0x1BCC, +0x1BD0
inline bool RotorKnown(const Rotor& r,float hover) noexcept {
    return std::isfinite(r.lift+r.vdamp+r.idle+hover) && r.lift>1e-3f && r.vdamp>0.0f && r.vdamp<=1.0f && r.idle>=0.0f &&
           hover>r.idle+1e-3f;
}
// The rotor for which the next frame's vy is `next` from `vy` (see above), within idle..1.
inline float ClimbRotor(float next,float vy,float hover,const Rotor& r) noexcept {
    const float gravity=hover*r.lift;
    const float above=(next-r.vdamp*vy+gravity)/r.lift;   // t = 1: at or over the hover
    if(above>=hover)return vec::Clamp(above,r.idle,1.0f);
    // Under the hover vy' is linear in the rotor: vy + q (rotor - idle) + rotor L - G, q = (vdamp - 1) vy / (hover - idle).
    const float q=(r.vdamp-1.0f)*vy/(hover-r.idle),slope=q+r.lift;
    const auto at=[&](float rotor){ return vy+q*(rotor-r.idle)+rotor*r.lift-gravity; };
    if(slope>1e-4f)return vec::Clamp((next-vy+q*r.idle+gravity)/slope,r.idle,hover);
    // Climbing fast the damping brakes more than the rotor's lift: the end of the range that comes nearest.
    return std::fabs(at(r.idle)-next)<std::fabs(at(hover)-next) ? r.idle : hover;
}
inline float RotorThrottle(float want,float rotor,float up,float down) noexcept {
    const float rate=want>=rotor ? up : down;
    if(!std::isfinite(rotor) || !(rate>1e-4f))return vec::Clamp(want,0.0f,1.0f);
    return vec::Clamp(rotor+(want-rotor)/rate,0.0f,1.0f);
}
inline float CollectiveThrottle(float climb,float vy,float rotor,float* hover,bool learn,float dt,const Rotor& r) noexcept {
    const float err=climb-vy;
    // Without the law's numbers (an EDF.dll that differs): the NPC's feed-forward from the hover (heli.cpp kClimbGain).
    const float want=RotorKnown(r,*hover) ? ClimbRotor(vy+err*kVerticalShare,vy,*hover,r) : vec::Clamp(*hover+err*0.08f,0.0f,1.0f);
    const float throttle=RotorThrottle(want,rotor,r.up,r.down);
    if(learn && std::fabs(vy)<kLearnBand && throttle>0.0f && throttle<1.0f)*hover=vec::Clamp(*hover+err*kPlayerHoverLearn*dt,0.1f,1.0f);
    return throttle;
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

// Native +0x1604 is a lagged heading OFFSET, not rad/s: 654E3F eases it toward maxAngle*input;
// 654ECA adds that offset to the target heading, then 6CE9C1 scales the angular error by spring*60.
// Invert that lag for the next native step. Otherwise the outer rate controller adds feedback around
// an unmodelled gain of ~9 and several seconds of lag (Brute), driving alternate full-stick turns.
inline float PlayerYawInput(float off,float rate,float rateLimit,float maxAngle,float state,float blend,float spring) noexcept {
    if(!std::isfinite(off+rate+rateLimit+maxAngle+state+blend+spring) || rateLimit<=0.0f ||
       std::fabs(maxAngle)<1e-4f || blend<=0.0f || blend>1.0f || spring<=0.0f)return 0.0f;
    const float gain=spring*60.0f;
    const float most=std::fmin(rateLimit,std::fabs(maxAngle)*gain);
    const float wanted=PlayerYaw(off,rate,most)*most/gain;
    return vec::Clamp((wanted-state*(1.0f-blend))/(maxAngle*blend),-1.0f,1.0f);
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
