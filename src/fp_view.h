// The first person's arithmetic (firstperson.cpp; RE docs/fp-render-re.md §6, docs/fp-soldier-re.md §2.1), no game
// state: tools/fp_view_check.cpp runs it offline.
// Matrices are the game's: 16 floats, rows right, up, forward, position (a point p maps to p·M, row vectors).
//  - The view transform T (the viewmodel): a bone's draw pose P (world) shown in front of the camera is P·T with
//    T = R⁻¹ · Tr(hold − eye) · S(s) · C. R is the soldier's aim frame (+0x60: its pitch and yaw, the same frame the
//    stock third-person camera turns in), eye the camera's eye in R, C the camera's world matrix. Arms and weapon share
//    T, so the hand stays on the grip; hold moves the pair in the aim frame; s shrinks it about the eye (the picture is
//    the same at any s, only nearer: walls in front do not cut it).
//  - The firearm-anchored transform: T = W⁻¹ · Tr(grip) · S(s) · C with W the held
//    weapon's root (its grip, the hand's mount) made orthonormal: the weapon sits at `grip` in the camera's frame,
//    pointing straight ahead, whatever the third-person pose holds it at; the arms follow it, the hands' own animation
//    (a reload's) on it kept.
//  - Around a shot that anchor is the root's rest before the shot (ShotAnchor): the firing animation is shown.
//  - Melee weapons and shields instead use NativeGripTransform: position the grip, preserving the authored rotation
//    relative to the aim frame. Cancelling W's rotation every frame cancels their animated swing too.
//  - The collapse: a matrix that sends a rigid instrument glyph's vertices to one point (rows 0..2 zero). The
//    first-person body no longer uses it: mixed skinning faces must be excluded by topology (fp_skin.h).
//  - The eye (Call of Duty's: a view height over the body's own axis): on the yaw-only body frame's up axis, `ahead` in
//    front of it, at the head bone's height eased (the user, 2026-10-14: the Fencer's view shook: its head's sway in the
//    walk and the dash, followed by a quarter a frame, went into the camera).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crew {
namespace fpview {
struct M4 { float m[16]; };

inline M4 Identity() noexcept {
    M4 r{};
    r.m[0]=r.m[5]=r.m[10]=r.m[15]=1.0f;
    return r;
}

// A controller-owned camera frame. Heading/pitch are input state, not a decorative model bone or last
// rendered view; +X is the native camera's left, +Y up, +Z ahead. Eye placement is an independent physical anchor.
inline M4 HeadingFrame(float yaw,float pitch,const float* eye) noexcept {
    const float sy=std::sin(yaw),cy=std::cos(yaw),sp=std::sin(pitch),cp=std::cos(pitch);
    M4 out{};
    out.m[0]=cy;out.m[2]=-sy;
    out.m[4]=-sy*sp;out.m[5]=cp;out.m[6]=-cy*sp;
    out.m[8]=sy*cp;out.m[9]=sp;out.m[10]=cy*cp;
    std::memcpy(out.m+12,eye,12);out.m[15]=1.0f;
    return out;
}

// Camera edits own rotation and eye separately. A later owner may move the eye without replacing our rotation,
// or replace the rotation while leaving the eye. Requiring all 64 bytes to match leaks an old rotation into the
// next native step; restoring all 64 bytes overwrites unrelated newer writes. Neither is a valid transaction.
struct CameraWrite { M4 before{},written{};bool axes=false,eye=false; };
inline CameraWrite CameraWriteOf(const M4& before,const M4& written) noexcept {
    CameraWrite out;out.before=before;out.written=written;
    out.axes=std::memcmp(before.m,written.m,48)!=0;
    out.eye=std::memcmp(before.m+12,written.m+12,16)!=0;
    return out;
}
inline void ReturnCameraWrite(M4* current,const CameraWrite& edit) noexcept {
    if(edit.axes && std::memcmp(current->m,edit.written.m,48)==0)std::memcpy(current->m,edit.before.m,48);
    if(edit.eye && std::memcmp(current->m+12,edit.written.m+12,16)==0)std::memcpy(current->m+12,edit.before.m+12,16);
}

// a·b (row vectors: p·(a·b) = (p·a)·b).
inline M4 Mul(const M4& a,const M4& b) noexcept {
    M4 r{};
    for(int i=0;i<4;++i)
        for(int j=0;j<4;++j)
            r.m[i*4+j]=a.m[i*4]*b.m[j]+a.m[i*4+1]*b.m[4+j]+a.m[i*4+2]*b.m[8+j]+a.m[i*4+3]*b.m[12+j];
    return r;
}

inline M4 Translate(float x,float y,float z) noexcept {
    M4 r=Identity();
    r.m[12]=x;r.m[13]=y;r.m[14]=z;
    return r;
}

inline M4 Scale(float s) noexcept {
    M4 r=Identity();
    r.m[0]=r.m[5]=r.m[10]=s;
    return r;
}

// Move a signed lateral grip inward without crossing the centre line.
inline float CenteredGrip(float lateral,float inward) noexcept {
    return std::copysign(std::fmax(0.0f,std::fabs(lateral)-inward),lateral);
}

// The inverse of an affine matrix (the 3x3 part inverted in full: a scaled frame inverts too). False when singular.
inline bool InverseAffine(const M4& a,M4* out) noexcept {
    const float* m=a.m;
    const float c00=m[5]*m[10]-m[6]*m[9],c01=m[6]*m[8]-m[4]*m[10],c02=m[4]*m[9]-m[5]*m[8];
    const float det=m[0]*c00+m[1]*c01+m[2]*c02;
    if(!(std::fabs(det)>1e-12f) || !std::isfinite(det))return false;
    const float k=1.0f/det;
    M4 r{};
    r.m[0]=c00*k;                  r.m[1]=(m[2]*m[9]-m[1]*m[10])*k;  r.m[2]=(m[1]*m[6]-m[2]*m[5])*k;
    r.m[4]=c01*k;                  r.m[5]=(m[0]*m[10]-m[2]*m[8])*k;  r.m[6]=(m[2]*m[4]-m[0]*m[6])*k;
    r.m[8]=c02*k;                  r.m[9]=(m[1]*m[8]-m[0]*m[9])*k;   r.m[10]=(m[0]*m[5]-m[1]*m[4])*k;
    for(int j=0;j<3;++j)r.m[12+j]=-(m[12]*r.m[j]+m[13]*r.m[4+j]+m[14]*r.m[8+j]);
    r.m[15]=1.0f;
    *out=r;
    return true;
}

inline void Point(const M4& a,const float* p,float* out) noexcept {
    for(int j=0;j<3;++j)out[j]=p[0]*a.m[j]+p[1]*a.m[4+j]+p[2]*a.m[8+j]+a.m[12+j];
}

inline bool Finite(const M4& a) noexcept {
    for(float v:a.m)if(!std::isfinite(v))return false;
    return true;
}

inline bool LookFrame(const float* eye,const float* look,M4* out) noexcept {
    float d[3];
    for(int i=0;i<3;++i){if(!std::isfinite(eye[i]) || !std::isfinite(look[i]))return false;d[i]=look[i]-eye[i];}
    const float flat=std::sqrt(d[0]*d[0]+d[2]*d[2]);
    if(!(flat*flat+d[1]*d[1]>1e-8f))return false;
    *out=HeadingFrame(std::atan2(d[0],d[2]),std::atan2(d[1],flat),eye);
    return Finite(*out);
}

// A gun camera's first person (the user, 2026-10-25: a weapon station's view lost the gun once it was raised, "往上抬炮管
// 就看不见了"; the gunship gunner's first person "无法动视角"). A remote_display seat (pylib/cockpit_kits.py: a weapon station,
// the gunship's side guns, the 410's pod guns, a mech's gun mounts) sees a camera's picture, and its eye marker rides a
// bone that does not carry the aim: the turret's yaw (cannon_main), the hull (bomber401, gunCam_l), the body. Its
// orientation from that bone held the view level while the gun climbed, or fixed to the airframe while the rider's aim
// (which the gunship's guns are laid by: no bone at all) turned. The camera is on the gun: it looks along `way`, level,
// from the marker's eye. `way` (firstperson.cpp Ride) is the gun's own bore where the gun turns in yaw and pitch
// (vhud.cpp SeatGunLine): the view stops where the gun stops (its elevation limits), and its centre is where the gun
// points, near or far. A gun that does not turn (the gunship's guns, the pod guns: the rider's aim is virtual) gives the
// stock seat camera's forward instead, which the game turns with that aim.
inline bool GunCameraFrame(const float* way,const float* eye,M4* out) noexcept {
    const float look[3]={eye[0]+way[0],eye[1]+way[1],eye[2]+way[2]};
    return LookFrame(eye,look,out);
}
// Whether seat `seat`'s first person is laid with its gun (above): its kit says the seat's sight line is the gun's
// (pylib/cockpit_kits.py Kit.gaze): the station's monitor (a sensor screen marker: remote_display) or the gaze marker
// vc_gaze_<seat> (an open seat: open_rider, the M163's and the quad Vulcan's operator standing at his gun, the user
// 2026-10-31: 「第一人称四管防空车视角不随炮管往上抬」), and it is not the pilot's seat of a craft the player flies (`flies`:
// a drone's, an aircraft's, a helicopter's, the Sazabi's: their monitor shows the nose, which the flight turns, not an
// aim). A closed cockpit (a tank's gunner behind his sight: tank_gunner) has neither: its view is its turret's frame,
// the sight's own optic (vc_optic_) the one that looks along the gun.
constexpr bool GunCameraSeat(bool sensorScreen,bool gaze,bool flies,int seat) noexcept {
    return (sensorScreen || gaze) && !(flies && seat==0);
}
// Where a gun camera seat (GunCameraSeat) looks: along the gun's bore where its gun turns in yaw and pitch (`bore`:
// vhud.cpp SeatGunLine found it); else a station's monitor (`sensorScreen`) along the stock seat camera's way (its
// rider's virtual aim: the gunship's side guns, the 410's pods), and an open seat along its own bone's frame: a rider
// with no gun of his own that turns (the kei truck's bed, the cargo truck's driver) looks as he always did.
enum class GunWay : std::uint8_t { cockpit, bore, stock };
constexpr GunWay GunCameraWay(bool sensorScreen,bool gaze,bool flies,int seat,bool bore) noexcept {
    return !GunCameraSeat(sensorScreen,gaze,flies,seat) ? GunWay::cockpit
         : bore ? GunWay::bore : sensorScreen ? GunWay::stock : GunWay::cockpit;
}

// The procedural motion (fp_motion.h) as a matrix in the camera's frame (x right, y up, z ahead): turned about `pivot`
// by pitch (up), yaw (right) and roll (right), then moved by the offset.
inline M4 MotionMatrix(float x,float y,float z,float pitch,float yaw,float roll,const float* pivot) noexcept {
    const float cp=std::cos(pitch),sp=std::sin(pitch),cy=std::cos(yaw),sy=std::sin(yaw),cr=std::cos(roll),sr=std::sin(roll);
    M4 rx=Identity(),ry=Identity(),rz=Identity();
    rx.m[5]=cp;rx.m[6]=-sp;rx.m[9]=sp;rx.m[10]=cp;     // ahead tips up
    ry.m[0]=cy;ry.m[2]=-sy;ry.m[8]=sy;ry.m[10]=cy;     // ahead turns right
    rz.m[0]=cr;rz.m[1]=-sr;rz.m[4]=sr;rz.m[5]=cr;      // up leans right
    const M4 turn=Mul(Mul(rz,rx),ry);
    return Mul(Mul(Translate(-pivot[0],-pivot[1],-pivot[2]),turn),Translate(pivot[0]+x,pivot[1]+y,pivot[2]+z));
}

// T (above), the motion applied in the camera's frame before the shrink. False when R is singular or the result is not
// finite (then nothing is moved).
inline bool ViewTransform(const M4& aim,const M4& camera,const float* hold,float scale,M4* out,
                          const M4& motion=Identity()) noexcept {
    M4 inv;
    if(!InverseAffine(aim,&inv))return false;
    float eye[3];
    Point(inv,camera.m+12,eye);
    const M4 t=Mul(Mul(Mul(Mul(inv,Translate(hold[0]-eye[0],hold[1]-eye[1],hold[2]-eye[2])),motion),Scale(scale)),camera);
    if(!Finite(t))return false;
    *out=t;
    return true;
}

// `a` with its three axes made unit length and square to each other (forward kept, up next, right from them).
inline bool Orthonormal(const M4& a,M4* out) noexcept {
    float f[3]={a.m[8],a.m[9],a.m[10]},u[3]={a.m[4],a.m[5],a.m[6]};
    auto unit=[](float* v){const float l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);if(!(l>1e-6f))return false;for(int i=0;i<3;++i)v[i]/=l;return true;};
    if(!unit(f))return false;
    const float d=u[0]*f[0]+u[1]*f[1]+u[2]*f[2];
    for(int i=0;i<3;++i)u[i]-=f[i]*d;
    if(!unit(u))return false;
    const float r[3]={u[1]*f[2]-u[2]*f[1],u[2]*f[0]-u[0]*f[2],u[0]*f[1]-u[1]*f[0]};
    M4 o=a;
    for(int i=0;i<3;++i){o.m[i]=r[i];o.m[4+i]=u[i];o.m[8+i]=f[i];}
    o.m[3]=o.m[7]=o.m[11]=0.0f;o.m[15]=1.0f;
    *out=o;
    return true;
}

// T anchored on the weapon (above), the motion applied about the grip. False when W is degenerate.
inline bool AnchoredTransform(const M4& weapon,const M4& camera,const float* grip,float scale,M4* out,
                              const M4& motion=Identity()) noexcept {
    M4 w,inv;
    if(!Orthonormal(weapon,&w) || !InverseAffine(w,&inv))return false;
    const M4 t=Mul(Mul(Mul(Mul(inv,Translate(grip[0],grip[1],grip[2])),motion),Scale(scale)),camera);
    if(!Finite(t))return false;
    *out=t;
    return true;
}

// --- A shot's kick on the viewmodel (docs/fp-render-re.md §5.1) ---
// The firearm anchor above puts the weapon root at the grip every frame. The third person's firing motion is the arms'
// animation, which 0x68FD00 copies onto the weapon root each frame (§4.1), so that anchor cancelled it whole (the user,
// 2026-10-31: the rifle's muzzle never rose, the rocket launcher did not move). Around a shot the anchor is instead the
// root as it rested just before it: its place in the aim frame (R0), carried by the aim as the view turns. The
// animation's departure from R0 is then shown, arms and weapon through the same T (the hand stays on the grip). The
// hold lasts kShotHoldFrames after the latest shot and, past that, while the weapon is still in its after-shot phase
// (`firing`: the fire state +0xBD4 2, which 0x6934F0 keeps after the shot's own 1 until it goes 0 or a reload's 3),
// at most kShotHoldMost steps; then it eases back to the per-frame anchor over kShotBlendFrames. Live 2026-10-31
// (Debug probe, the root from R0): the M1 Raven 5-7 cm back and 1-2 deg, its phase over 3 steps after the round, the
// motion settling in ~12 (the window governs); the NC101 hand cannon 0.2 m back and 10 deg up, its phase ~31 steps;
// Goliath D1 0.5 m back and 70 deg up, a reload's 3 from the 20th step. A reload does not cut the hold short (the
// firing animation still runs when it starts); after the hold the reload is anchored per frame again. A weapon switch
// drops it (firstperson.cpp).
struct ShotHold {
    M4 last{},rest{};    // the root in the aim frame: the last camera step's, and R0 the hold shows the shot from
    bool lastOk=false;
    bool holding=false;
    int frames=0;        // camera steps of the hold left after the latest shot (then held on while `firing`)
    int held=0;          // camera steps since the latest shot
    int blend=0;         // camera steps of the hand-back left
};
// kShotHoldMost bounds a phase that never ends (a weapon held in state 2 without rounds): ~1.5 s at 60 steps a second.
constexpr int kShotHoldFrames=24,kShotHoldMost=90,kShotBlendFrames=8;

// `a`'s rows blended toward `b`'s by t (0: a, 1: b), made orthonormal again (the two are near: a kick's departure).
inline M4 BlendFrames(const M4& a,const M4& b,float t) noexcept {
    M4 r=a;
    for(int i=0;i<16;++i)r.m[i]=a.m[i]+(b.m[i]-a.m[i])*t;
    M4 o;
    return Orthonormal(r,&o) ? o : a;
}

// One camera step of hand `s`: `weapon` its root as it will be drawn, `aim` the soldier's aim frame, `shot` a round of
// that weapon left since the last step (0x696FD0, every weapon class's one shot entry), `firing` the weapon still in
// its after-shot phase. `anchor` is what AnchoredTransform anchors on: `weapon` itself whenever no shot is being shown.
// True while the anchor is not `weapon` (holding or easing back). A degenerate frame drops the hold.
inline bool ShotAnchor(ShotHold& s,const M4& weapon,const M4& aim,bool shot,bool firing,M4* anchor) noexcept {
    *anchor=weapon;
    M4 a,w,inv,local;
    if(!Orthonormal(aim,&a) || !Orthonormal(weapon,&w) || !InverseAffine(a,&inv)){s=ShotHold{};return false;}
    local=Mul(w,inv);
    if(shot) {
        // A shot from rest takes R0 from the step before (this step's pose may already be the kick's first frame); one
        // during the hold or its easing back keeps the R0 it has.
        if(!s.holding && s.blend==0)s.rest=s.lastOk ? s.last : local;
        s.holding=true;s.frames=kShotHoldFrames;s.held=0;s.blend=0;
    } else if(s.holding) {
        if(s.frames>0)--s.frames;
        if((s.frames==0 && !firing) || ++s.held>=kShotHoldMost){s.holding=false;s.frames=0;s.held=0;s.blend=kShotBlendFrames;}
    }
    bool moved=false;
    if(s.holding){*anchor=Mul(s.rest,a);moved=true;}
    else if(s.blend>0) {
        const float t=static_cast<float>(s.blend)/static_cast<float>(kShotBlendFrames+1);
        *anchor=Mul(BlendFrames(local,s.rest,t),a);
        --s.blend;moved=true;
    }
    s.last=local;s.lastOk=true;
    if(moved && !Finite(*anchor)){*anchor=weapon;s=ShotHold{};return false;}
    return moved;
}

// A melee weapon / shield has an authored holding angle and animated swing, not a gun bore to straighten. Anchor
// the idle grip's position but keep authored translation from `rest` and rotation relative to the soldier's aim frame. The same T goes through
// its arm, palm and fingers, so the contact is retained while the blade swings.
inline bool NativeGripTransform(const M4& weapon,const M4& aim,const M4& camera,const float* grip,float scale,M4* out,
                                const M4& motion=Identity(),const float* rest=nullptr) noexcept {
    M4 inv;
    if(!InverseAffine(aim,&inv))return false;
    float local[3];Point(inv,weapon.m+12,local);
    if(rest)std::memcpy(local,rest,12);
    const M4 t=Mul(Mul(Mul(Mul(inv,Translate(grip[0]-local[0],grip[1]-local[1],grip[2]-local[2])),motion),Scale(scale)),camera);
    if(!Finite(t))return false;
    *out=t;
    return true;
}

// EDF's render view flips X/Z after world-to-camera (native 0x678CCB46; measured main-view parameters).
inline bool NativeView(const M4& camera,M4* out) noexcept {
    M4 inverse;if(!InverseAffine(camera,&inverse))return false;
    for(int row=0;row<4;++row){inverse.m[row*4]*=-1;inverse.m[row*4+2]*=-1;}
    *out=inverse;return true;
}

// --- A weapon's size, in its own frame (docs/fp-render-re.md §17) ---
// A skeleton instance keeps each bone's bounds in its bone record: a flag (+0x09), half sizes along the bone's own
// axes (+0xF0, at least 0.001) and a centre in the bone's frame (+0x100). The model's Umbra box (instance +0x70 =
// model+0x110) made of them by 0x11005E0 is, for a model of several meshes or a skinned one (every Ranger rifle and
// shotgun), the WORLD-axis box round them: its "right" half size is the weapon's width facing one way and its length
// facing 90 degrees off, and every frame of the walk's sway changes it. The size a hold is fitted to must be taken
// along the weapon's own axes instead: the same bone boxes, measured in the weapon root's frame.
struct BoneBox { M4 world; float half[3],centre[3]; };

// The box round `boxes` along the axes of `root` (rows: half axes along root's right, up, ahead, scaled by root's own
// axes; row 3 the centre in root's frame, w 1). False: no box, or root singular.
inline bool FrameBounds(const M4& root,const BoneBox* boxes,int n,M4* out) noexcept {
    M4 inv;
    if(n<=0 || !InverseAffine(root,&inv))return false;
    float lo[3]={1e30f,1e30f,1e30f},hi[3]={-1e30f,-1e30f,-1e30f};
    for(int b=0;b<n;++b) {
        const M4 local=Mul(boxes[b].world,inv);   // the bone in the root's frame
        float c[3];Point(local,boxes[b].centre,c);
        for(int j=0;j<3;++j) {
            float e=0.0f;
            for(int k=0;k<3;++k)e+=std::fabs(local.m[k*4+j])*std::fmax(boxes[b].half[k],0.001f);
            lo[j]=std::fmin(lo[j],c[j]-e);hi[j]=std::fmax(hi[j],c[j]+e);
        }
    }
    M4 r{};
    for(int j=0;j<3;++j){r.m[j*4+j]=0.5f*(hi[j]-lo[j]);r.m[12+j]=0.5f*(hi[j]+lo[j]);}
    r.m[15]=1.0f;
    if(!Finite(r))return false;
    *out=r;
    return true;
}

// The game's own box of the same bones (0x11005E0's path for several meshes, H): along the world's axes. Kept for the
// offline check that shows what it did to the hold.
inline M4 WorldBounds(const BoneBox* boxes,int n) noexcept {
    M4 r=Identity();
    FrameBounds(Identity(),boxes,n,&r);
    return r;
}

// The half sizes of a box (rows 0..2).
inline void HalfSizes(const M4& box,float* half) noexcept {
    for(int r=0;r<3;++r)half[r]=std::sqrt(box.m[r*4]*box.m[r*4]+box.m[r*4+1]*box.m[r*4+1]+box.m[r*4+2]*box.m[r*4+2]);
}

// A bulky or long weapon held further off, a shield out to the side (the hold's offset, camera frame, `out` the camera
// x of "away from the middle"): from the weapon's half sizes along its own right, up, ahead. False: not a sane size
// (nothing moved).
inline bool FitGrip(const float* half,bool dual,bool flip,float out,float* grip) noexcept {
    if(!std::isfinite(half[0]+half[1]+half[2]) || half[0]>3.0f || half[1]>3.0f || half[2]>3.0f)return false;
    float x=0.0f,y=0.0f,z=0.0f;
    if(!dual) {
        const float girth=std::fmax(half[0],half[1]);
        z=0.8f*std::fmax(0.0f,girth-0.12f)+0.3f*std::fmax(0.0f,half[2]-0.5f);
        x=0.6f*std::fmax(0.0f,half[0]-0.08f);
        y=-0.4f*std::fmax(0.0f,half[1]-0.12f);
    }
    if(flip){x+=0.22f;z+=0.15f;}
    grip[0]+=out*x;grip[1]+=y;grip[2]+=z;
    return true;
}

// Every vertex weighed by it lands on `at`.
inline M4 Collapse(const float* at) noexcept {
    M4 r{};
    r.m[12]=at[0];r.m[13]=at[1];r.m[14]=at[2];r.m[15]=1.0f;
    return r;
}

// The eye's place in the body frame `body` (rows right, up, forward, position; yaw only): on its up axis `ahead` metres
// in front, at the head bone's height (world position `head`) pushed `up`, the height eased toward it by `ease` (0..1 a
// frame) from `local` (in, out). The head's sway to the sides and back and forth is not followed. The first frame
// (`fresh`) takes the height as it is.
inline void EaseEye(const M4& body,const float* head,float up,float ahead,float ease,bool fresh,float* local) noexcept {
    const float rel[3]={head[0]-body.m[12],head[1]-body.m[13],head[2]-body.m[14]};
    const float height=rel[0]*body.m[4]+rel[1]*body.m[5]+rel[2]*body.m[6]+up;
    local[0]=0.0f;local[2]=ahead;
    local[1]=fresh ? height : local[1]+(height-local[1])*ease;
}

inline void EyeWorld(const M4& body,const float* local,float* out) noexcept {
    for(int j=0;j<3;++j)out[j]=body.m[12+j]+local[0]*body.m[j]+local[1]*body.m[4+j]+local[2]*body.m[8+j];
}

// --- The arms posed for the first person ---
// ArmChainTo preserves limb lengths while solving one connected elbow for a specified wrist; ArmChain keeps the
// animated wrist unchanged. A displaced support hand and its forearm always move together; reloads keep the native wrist.

inline void Sub(const float* a,const float* b,float* o) noexcept { for(int i=0;i<3;++i)o[i]=a[i]-b[i]; }
inline float Dot3(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline float Length(const float* a) noexcept { return std::sqrt(Dot3(a,a)); }
inline bool Unit(float* a) noexcept {
    const float l=Length(a);
    if(!(l>1e-6f) || !std::isfinite(l))return false;
    for(int i=0;i<3;++i)a[i]/=l;
    return true;
}
inline void Cross3(const float* a,const float* b,float* o) noexcept {
    o[0]=a[1]*b[2]-a[2]*b[1];o[1]=a[2]*b[0]-a[0]*b[2];o[2]=a[0]*b[1]-a[1]*b[0];
}

// The shoulder an upper arm `upper` long reaches from the elbow `elbow` toward `toward`. False when degenerate.
inline bool ReachShoulder(const float* elbow,const float* toward,float upper,float* shoulder) noexcept {
    float d[3];
    Sub(toward,elbow,d);
    if(!(upper>1e-4f) || !Unit(d))return false;
    for(int i=0;i<3;++i)shoulder[i]=elbow[i]+d[i]*upper;
    return true;
}

// The turn about the unit axis `k` by the angle whose cosine is `c` and sine `sn` (Rodrigues, as rows: p' = p R for
// row vectors, R the transpose of the column form).
inline M4 Rotation(const float* k,float c,float sn) noexcept {
    const float t=1.0f-c;
    const float m[3][3]={{c+k[0]*k[0]*t,k[0]*k[1]*t-k[2]*sn,k[0]*k[2]*t+k[1]*sn},
                         {k[1]*k[0]*t+k[2]*sn,c+k[1]*k[1]*t,k[1]*k[2]*t-k[0]*sn},
                         {k[2]*k[0]*t-k[1]*sn,k[2]*k[1]*t+k[0]*sn,c+k[2]*k[2]*t}};
    M4 r=Identity();
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)r.m[i*4+j]=m[j][i];
    return r;
}

// The turn by `angle` (rad, right-handed) about the line through `at` along the unit `axis`.
inline M4 TurnAbout(const float* at,const float* axis,float angle) noexcept {
    return Mul(Mul(Translate(-at[0],-at[1],-at[2]),Rotation(axis,std::cos(angle),std::sin(angle))),
               Translate(at[0],at[1],at[2]));
}

// The angle (rad, right-handed about the unit `axis`) that turns `from` onto `toward`, both seen square to the axis;
// 0 when either lies along it.
inline float RollAngle(const float* from,const float* toward,const float* axis) noexcept {
    float a[3],b[3],k[3];
    const float fa=Dot3(from,axis),ta=Dot3(toward,axis);
    for(int i=0;i<3;++i){a[i]=from[i]-axis[i]*fa;b[i]=toward[i]-axis[i]*ta;}
    if(!Unit(a) || !Unit(b))return 0.0f;
    Cross3(a,b,k);
    return std::atan2(Dot3(k,axis),Dot3(a,b));
}

// The rigid move (no scale) that takes the point `a0` to `a1` and turns the direction a0->b0 onto a1->b1 the shortest
// way: a bone posed as P goes to P * SegmentMap. False when a direction is degenerate.
inline bool SegmentMap(const float* a0,const float* b0,const float* a1,const float* b1,M4* out) noexcept {
    float u[3],v[3];
    Sub(b0,a0,u);Sub(b1,a1,v);
    if(!Unit(u) || !Unit(v))return false;
    float k[3];
    Cross3(u,v,k);
    const float c=Dot3(u,v),sn=Length(k);
    M4 r=Identity();
    if(sn>1e-6f) {
        for(int i=0;i<3;++i)k[i]/=sn;
        r=Rotation(k,c,sn);
    } else if(c<0.0f) {   // opposite: half a turn about any square axis
        float any[3]={std::fabs(u[0])<0.9f ? 1.0f : 0.0f,std::fabs(u[0])<0.9f ? 0.0f : 1.0f,0.0f},ax[3];
        Cross3(u,any,ax);
        if(!Unit(ax))return false;
        for(int i=0;i<3;++i)for(int j=0;j<3;++j)r.m[i*4+j]=2.0f*ax[i]*ax[j]-(i==j ? 1.0f : 0.0f);
    }
    *out=Mul(Mul(Translate(-a0[0],-a0[1],-a0[2]),r),Translate(a1[0],a1[1],a1[2]));
    return Finite(*out);
}

inline float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : (v>hi ? hi : v); }

// A connected two-bone arm with its animated wrist held fixed on the gun. Move the shoulder only as far as the
// two measured bone lengths permit, then solve the elbow on their intersection circle. The pole is projected
// perpendicular to the reach, so no angle wrapping or independently rolled wrist can flip the support hand.
inline bool ArmChainTo(const float* s0,const float* e0,const float* w0,const float* wrist,const float* shoulderWant,
                       const float* pole,M4* upperMove,M4* foreMove) noexcept {
    float v[3];Sub(e0,s0,v);const float a=Length(v);
    Sub(w0,e0,v);const float b=Length(v);
    if(!(a>1e-5f) || !(b>1e-5f))return false;
    float axis[3];Sub(shoulderWant,wrist,axis);
    const float wanted=Length(axis);
    if(!Unit(axis))return false;
    const float reach=Clamp(wanted,std::fabs(a-b)+1e-5f,a+b-1e-5f);
    float s1[3];for(int i=0;i<3;++i)s1[i]=wrist[i]+axis[i]*reach;
    // Distance from the wrist along wrist->shoulder to the elbow circle's centre.
    const float along=(b*b+reach*reach-a*a)/(2.0f*reach);
    const float radius=std::sqrt(std::fmax(0.0f,b*b-along*along));
    float bend[3];
    const float axial=Dot3(pole,axis);
    for(int i=0;i<3;++i)bend[i]=pole[i]-axis[i]*axial;
    if(!Unit(bend)) { // A straight-on pole: use the animated elbow, never an arbitrary sign-changing cross product.
        Sub(e0,wrist,bend);const float d=Dot3(bend,axis);
        for(int i=0;i<3;++i)bend[i]-=axis[i]*d;
        if(!Unit(bend))return false;
    }
    float e1[3];for(int i=0;i<3;++i)e1[i]=wrist[i]+axis[i]*along+bend[i]*radius;
    return SegmentMap(s0,e0,s1,e1,upperMove) && SegmentMap(e0,w0,e1,wrist,foreMove);
}

inline bool ArmChain(const float* s0,const float* e0,const float* wrist,const float* shoulderWant,
                     const float* pole,M4* upperMove,M4* foreMove) noexcept {
    return ArmChainTo(s0,e0,wrist,wrist,shoulderWant,pole,upperMove,foreMove);
}

// --- The free (support) hand's arm (docs/fp-render-re.md §17.4) ---
// The view's frustum: the camera's axes in the world (right on screen, up, ahead), its eye, the projection's x and y
// scales (1/tan of the half angles) and its near clip.
struct Frustum { float right[3],up[3],ahead[3],eye[3],xs,ys,nearClip; };

// Whether a ball of radius `r` at `p` reaches into the view (a side plane's signed distance under r, ahead of the near
// clip).
inline bool Seen(const Frustum& f,const float* p,float r) noexcept {
    float d[3];Sub(p,f.eye,d);
    const float x=Dot3(d,f.right),y=Dot3(d,f.up),z=Dot3(d,f.ahead);
    if(z+r<f.nearClip)return false;
    const float kx=std::sqrt(f.xs*f.xs+1.0f),ky=std::sqrt(f.ys*f.ys+1.0f);
    if((x*f.xs-z)/kx>r || (-x*f.xs-z)/kx>r)return false;
    if((y*f.ys-z)/ky>r || (-y*f.ys-z)/ky>r)return false;
    return true;
}

// The support hand's arm: the hand moved to `wrist` (its own turn on the gun kept) takes its forearm with it, turned
// about the wrist toward the shoulder by at most `wristMost` (the wrist bends at most that much more than the
// animation bends it); the upper arm turns about the elbow toward the shoulder
// `shoulderWant` (the soldier's own, seen from the eye), both bones their measured length. Its open shoulder end must
// stay out of the view (a ball of `sleeve` radius): when the shoulder's way would show it, the upper arm takes the
// hidden way nearest it (a sleeve coming in from the edge, never a cut sleeve on screen). The upper arm's move and the
// forearm's (a translation: the hand's) out. False: degenerate bones, or the elbow so deep in the view that no way
// hides the end (the caller then uses the two-bone chain).
inline bool SupportArm(const float* s0,const float* e0,const float* h0,const float* wrist,const float* shoulderWant,
                       const Frustum& view,float sleeve,float wristMost,M4* upperMove,M4* foreMove) noexcept {
    float v[3];Sub(e0,s0,v);
    const float a=Length(v);
    if(!(a>1e-5f))return false;
    // The forearm: the animation's, turned about the wrist toward the shoulder by at most `wristMost` (rad), so the arm
    // reaches back toward its body as far as a wrist bends without folding.
    float f[3],g[3];Sub(e0,h0,f);
    const float b=Length(f);
    if(!Unit(f))return false;
    Sub(shoulderWant,wrist,g);
    float e1[3];
    if(Unit(g) && wristMost>0.0f) {
        float k[3];Cross3(f,g,k);
        const float between=std::atan2(Length(k),Dot3(f,g));
        if(Unit(k)) {
            const float turn=std::fmin(between,wristMost);
            const M4 r=Rotation(k,std::cos(turn),std::sin(turn));
            float t[3];Point(r,f,t);std::memcpy(f,t,12);
        }
    }
    for(int i=0;i<3;++i)e1[i]=wrist[i]+f[i]*b;
    float u[3];Sub(shoulderWant,e1,u);
    if(!Unit(u))return false;
    // The shoulder's own way when its end is hidden; else the hidden way nearest it (5-degree steps over the sphere
    // of ways, the nearest first).
    float s1[3];
    for(int i=0;i<3;++i)s1[i]=e1[i]+u[i]*a;
    bool found=!Seen(view,s1,sleeve);
    if(!found) {
        float best=-2.0f,bestWay[3]={0,0,0};
        for(int el=-17;el<=17;++el) {
            const float phi=el*0.0872664626f,cp=std::cos(phi),sp=std::sin(phi);
            const int around=static_cast<int>(72.0f*cp)+1;
            for(int az=0;az<around;++az) {
                const float th=az*6.2831853f/around;
                float w[3];
                for(int i=0;i<3;++i)w[i]=view.up[i]*sp+cp*(view.right[i]*std::cos(th)+view.ahead[i]*std::sin(th));
                const float closeness=Dot3(w,u);
                if(closeness<=best)continue;
                float p[3];for(int i=0;i<3;++i)p[i]=e1[i]+w[i]*a;
                if(Seen(view,p,sleeve))continue;
                best=closeness;std::memcpy(bestWay,w,12);
            }
        }
        if(best>-2.0f){for(int i=0;i<3;++i)s1[i]=e1[i]+bestWay[i]*a;found=true;}
    }
    if(!found)return false;
    return SegmentMap(e0,h0,e1,wrist,foreMove) && SegmentMap(s0,e0,s1,e1,upperMove);
}

// The move that takes a hand whose wrist is at `wrist` onto the weapon whose bounds are `box` (rows: half axes right,
// up, ahead; row 3 the centre, all through T): when the wrist lies further from the box's axis than the box's
// elliptic section there plus `palm`, it is pulled in square to the axis to that distance (and along it into the
// box's length); nearer, it stays (a box counts a magazine and a sight: never pushed out). False: a degenerate box.
inline bool SnapToBox(const M4& box,const float* wrist,float palm,float* move) noexcept {
    float r[3],u[3],f[3];
    for(int i=0;i<3;++i){r[i]=box.m[i];u[i]=box.m[4+i];f[i]=box.m[8+i];move[i]=0.0f;}
    const float hr=Length(r),hu=Length(u),hf=Length(f);
    if(!Unit(r) || !Unit(u) || !Unit(f) || !(hr>1e-4f) || !(hu>1e-4f))return false;
    float d[3];
    Sub(wrist,box.m+12,d);
    const float along=Dot3(d,f),x=Dot3(d,r),y=Dot3(d,u);
    const float radial=std::sqrt(x*x+y*y);
    const float clampedAlong=Clamp(along,-hf,hf);
    if(!(radial>1e-5f)) {
        for(int i=0;i<3;++i)move[i]=f[i]*(clampedAlong-along);
        return true;
    }
    const float q=std::sqrt((x/hr)*(x/hr)+(y/hu)*(y/hu));   // 1 on the section's ellipse
    const float reach=radial/q+palm;                         // the surface that way, and the palm's thickness
    const float k=radial>reach ? reach/radial : 1.0f;
    for(int i=0;i<3;++i)move[i]=f[i]*(clampedAlong-along)+(r[i]*x+u[i]*y)*(k-1.0f);
    return true;
}

// The roll (rad, right-handed about the unit `axis` through `centre`) a hand at `wrist` takes of `want`: its place
// round the axis kept within `arc` of `down` (a place already outside is brought to the nearer edge).
inline float ClampedRoll(const float* centre,const float* axis,const float* wrist,const float* down,float want,
                         float arc) noexcept {
    float w[3];
    Sub(wrist,centre,w);
    const float at=RollAngle(down,w,axis);
    return Clamp(at+want,-arc,arc)-at;
}
}  // namespace fpview
}  // namespace crew
