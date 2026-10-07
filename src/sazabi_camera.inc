// The Sazabi's own camera and its aim (sazabi.cpp includes this inside its anonymous namespace, after sazabi_arms.inc).
// Riding, the stock camera follows the seat's MAB locators: fixed on the model, it neither pitches with the aim nor puts
// the aim anywhere in particular (the user, 2026-10-07: 「视角不对」, the HUD should have its reticle in the middle). So
// the plugin places the camera itself, through turretcam.cpp's look-at hook (LookHook -> SazabiCamera): a third-person
// rig over the mech's right shoulder, turning with its heading and pitching with its aim, and the screen's centre is
// the aim: every weapon fires at what the centre's ray meets (Aim). One ray, ViewRay, for both: the hook places the eye
// on it each time the game fetches the camera, the game thread aims along it each frame. Both place it off the model's
// origin (the soles), each where it is in its own context (measured 2026-10-07): on the game thread the vehicle's
// position and matrix are the collision box's centre (y 14.0 standing) and the model's origin is sz_root's world;
// when the camera is fetched the engine has turned the vehicle's matrix (and its position field) into the model's
// origin (y 1.2: the box's centre less heli_rigid_body[0], as the frames log found `mdl`). A camera
// placed off the field in the hook with an offset measured on the game thread sat 12.8 m too low.
// The rig (tools: camview/rig.png in the preview folder, 2026-10-07): its pivot kViewUp m over the soles (mid-height),
// the eye kViewBack m behind it along the aim, kViewSide m to the right and kViewRise m up: the whole 25.6 m mech in view
// left of the centre, level, 20 deg up and 20 deg down; never under kViewFloor m over the ground under the mech.

constexpr float kViewUp=14.8f,kViewBack=45.0f,kViewSide=11.0f,kViewRise=1.0f,kViewFloor=3.0f;
constexpr ULONGLONG kViewFresh=250;   // ms: a published view older than this is no longer the Sazabi's (the stock camera)
constexpr ULONGLONG kCentredMs=200;   // ms: the hook placed the camera this recently: the HUD's reticle is the centre

// What the hook needs to place the camera, published by the game thread (another thread fetches the camera).
struct View {
    const unsigned char* vehicle=nullptr;
    const unsigned char* seat=nullptr;
    float yaw=0.0f,pitch=0.0f;   // the mech's heading, the aim's pitch (rad; forward = (sin yaw, 0, cos yaw))
    float floor=-1e9f;           // the ground's height under the mech (world y)
    ULONGLONG ms=0;
};
SRWLOCK viewLock=SRWLOCK_INIT;
View view{};
volatile ULONGLONG placedMs=0;   // GameMs the hook last placed the camera at

// The model's origin as the camera hook sees it: the vehicle's matrix's position then (see the top).
const float* Where(const unsigned char* v) noexcept { return reinterpret_cast<const float*>(v+kMatrix)+12; }

// The camera's eye and its centre ray's unit direction for `w` with the model's origin (its soles) at `origin`.
void ViewRay(const View& w,const float* origin,float* eye,float* dir) noexcept {
    const float cp=std::cos(w.pitch);
    dir[0]=std::sin(w.yaw)*cp;dir[1]=std::sin(w.pitch);dir[2]=std::cos(w.yaw)*cp;
    const float right[3]={-std::cos(w.yaw),0.0f,std::sin(w.yaw)};
    for(int k=0;k<3;++k)eye[k]=origin[k]+(k==1 ? kViewUp+kViewRise : 0.0f)-dir[k]*kViewBack+right[k]*kViewSide;
    eye[1]=std::fmax(eye[1],w.floor+kViewFloor);
}

// The view of `m` now (no vehicle or seat): its heading, its aim's pitch, the ground under it.
View ViewOf(const Mech& m) noexcept {
    View w;
    w.yaw=m.heading;w.pitch=m.aimPitch;
    if(!m.rootOk)return w;
    w.floor=m.root[13]-(m.fl.air && m.feetClear!=kNoGround ? m.feetClear : 0.0f);
    return w;
}

void PublishView(const Mech& m,unsigned char* v) noexcept {
    View w=ViewOf(m);
    w.vehicle=v;w.seat=SeatAt(v,0);w.ms=GameMs();
    AcquireSRWLockExclusive(&viewLock);
    view=w;
    ReleaseSRWLockExclusive(&viewLock);
}
void DropView() noexcept {
    AcquireSRWLockExclusive(&viewLock);
    view=View{};
    ReleaseSRWLockExclusive(&viewLock);
}

// The aim assist (sazabi_assist.h; ini SazabiAimAssist ...): the player's only, after the stick has turned the view.
// The enemies' lock points within the held cone of the centre's ray are gathered (the cone the held one keeps:
// SazabiAssistCone x kAssistKeep), the one picked must be seen from the eye (no terrain or building between: MapRay;
// those hidden are passed over for the next, at most kAssistRays tried), and its point becomes the aim (Aim). On a pad
// (or the mouse with SazabiAssistMousePull) the heading and pitch are then eased toward it (Pull): the camera settles
// the enemy on the reticle and keeps it there while it moves.
constexpr float kAssistKeep=1.6f;     // the held enemy kept this many times the cone
constexpr float kAssistMostPull=1.5f; // rad/s: the pull's fastest
constexpr float kAssistBehind=3.0f;   // m: a wall hit this short of the point does not hide it (its own hull's edge)
constexpr int kAssistRays=4;
struct AssistSeen { float eye[3],dir[3],cone,range; int n; sazabi::assist::Point pts[kMostTargets]; };
void SeeAssist(void* ctx,const void* obj,const float* at) {
    AssistSeen* const s=static_cast<AssistSeen*>(ctx);
    if(s->n>=kMostTargets || !at || !std::isfinite(at[0]+at[1]+at[2]))return;
    float dist;
    if(sazabi::assist::Angle(s->dir,s->eye,at,&dist)>s->cone || dist>s->range)return;
    s->pts[s->n].obj=obj;std::memcpy(s->pts[s->n].at,at,12);++s->n;
}
bool SeenFrom(const float* eye,const float* at) noexcept {
    float hit[3];
    const float wall=MapRay(eye,at,hit);
    if(wall<0.0f)return true;
    const float d[3]={at[0]-eye[0],at[1]-eye[1],at[2]-eye[2]};
    return wall>=Len(d)-kAssistBehind;
}
void Assist(Mech& m,unsigned char* v,float dt) noexcept {
    namespace as=sazabi::assist;
    Arms& a=m.arms;
    const void* const held=a.hasAssist ? a.assistObj : nullptr;
    a.hasAssist=false;a.assistObj=nullptr;
    if(!m.rootOk || !Cfg().sazabiAimAssist || a.swing>=0.0f)return;
    const float cone=Cfg().sazabiAssistCone*sazabi::kDeg,keep=cone*kAssistKeep,range=Cfg().sazabiAssistRange;
    AssistSeen s{};
    s.cone=keep;s.range=range;
    ViewRay(ViewOf(m),m.root+12,s.eye,s.dir);
    VisitEnemies(v,SeeAssist,&s);
    int pick=-1;
    for(int tries=0;tries<kAssistRays && s.n>0;++tries) {
        const int i=as::Pick(s.eye,s.dir,s.pts,s.n,cone,keep,range,held);
        if(i<0)break;
        if(SeenFrom(s.eye,s.pts[i].at)){pick=i;break;}
        s.pts[i]=s.pts[--s.n];   // hidden: the next
    }
    if(pick<0)return;
    a.hasAssist=true;a.assistObj=s.pts[pick].obj;std::memcpy(a.assist,s.pts[pick].at,12);
    const unsigned char* const seat=SeatAt(v,0);
    const bool pad=At<unsigned char>(seat,kSeatPad)!=0;
    if(Cfg().sazabiAssistPull<=0.0f || (!pad && !Cfg().sazabiAssistMousePull))return;
    const View base=ViewOf(m);
    float wantYaw,wantPitch;
    as::Solve(a.assist,m.heading,m.aimPitch,[&](float yaw,float pitch,float* eye) noexcept {
        View w=base;w.yaw=yaw;w.pitch=pitch;
        float dir[3];
        ViewRay(w,m.root+12,eye,dir);
    },&wantYaw,&wantPitch);
    const float gain=Cfg().sazabiAssistPull;
    m.heading=as::WrapPi(m.heading+as::Pull(m.heading,wantYaw,gain,kAssistMostPull,dt));
    m.aimPitch=Clamp(m.aimPitch+as::Pull(m.aimPitch,Clamp(wantPitch,-kAimMost,kAimMost),gain,kAssistMostPull,dt),-kAimMost,kAimMost);
}

// The aim this frame: the player's along the camera's centre ray (what it meets within kAimRange, else its far end);
// an NPC's from the muzzle along its heading and pitch. The rifle arm is then turned onto it (the pose's aim yaw and
// pitch from the right shoulder, sazabi_pose.h AimRifle), the cannon's beams and the HUD's reticle go to it.
void Aim(Mech& m) noexcept {
    Arms& a=m.arms;
    a.hasAim=false;a.aimHit=false;a.centred=false;
    if(!m.rootOk)return;
    float from[3],dir[3];
    if(!m.npc) {
        ViewRay(ViewOf(m),m.root+12,from,dir);   // the model's origin here: sz_root's world
        a.centred=GameMs()-placedMs<=kCentredMs;
    } else {
        BoneAt(m,sazabi::kMuzzle,nullptr,from);
        const float cp=std::cos(m.aimPitch);
        dir[0]=std::sin(m.heading)*cp;dir[1]=std::sin(m.aimPitch);dir[2]=std::cos(m.heading)*cp;
    }
    float stop[3],hit[3];
    for(int k=0;k<3;++k)stop[k]=from[k]+dir[k]*kAimRange;
    a.aimHit=MapRay(from,stop,hit)>=0.0f;
    std::memcpy(a.aim,a.aimHit ? hit : stop,12);
    if(!m.npc && a.hasAssist){std::memcpy(a.aim,a.assist,12);a.aimHit=true;}   // the assist's enemy (Assist)
    float muzzle[3];
    BoneAt(m,sazabi::kMuzzle,nullptr,muzzle);
    const float d[3]={a.aim[0]-muzzle[0],a.aim[1]-muzzle[1],a.aim[2]-muzzle[2]};
    a.aimRange=Len(d);
    a.hasAim=a.swing<0.0f;
    // the arm onto the aim point: its direction from the right shoulder, in sz_root's frame (x left, z ahead)
    float local[3],sh[3];
    ToRoot(m,a.aim,local);
    std::memcpy(sh,m.posed.modelPos[sazabi::kShoulderR],12);
    const float r[3]={local[0]-sh[0],local[1]-sh[1],local[2]-sh[2]};
    const float flat=std::sqrt(r[0]*r[0]+r[2]*r[2]);
    if(flat>1.0f) {
        m.pose.aimYaw=std::atan2(r[0],r[2]);
        m.pose.aimPitch=std::atan2(r[1],flat);
    }
}
