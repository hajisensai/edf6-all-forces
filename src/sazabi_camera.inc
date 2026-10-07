// The Sazabi's own camera and its aim (sazabi.cpp includes this inside its anonymous namespace, after sazabi_arms.inc).
// Riding, the stock camera follows the seat's MAB locators: fixed on the model, it neither pitches with the aim nor puts
// the aim anywhere in particular (the user, 2026-10-07: 「视角不对」, the HUD should have its reticle in the middle). So
// the plugin places the camera itself, through turretcam.cpp's look-at hook (LookHook -> SazabiCamera): a third-person
// rig over the mech's right shoulder, turning with its heading and pitching with its aim, and the screen's centre is
// the aim: every weapon fires at what the centre's ray meets (Aim). One ray, ViewRay, for both: the hook places the eye
// on it each time the game fetches the camera, the game thread aims along it each frame. Both place it off the vehicle's
// position as they read it then (the body's current one) plus View::soles, where the soles are from that position (the
// game thread's sz_root world): the position field is no fixed point of the mech (in game 1.2 m over its soles while
// driven, 13.2 m parked: the camera that took it for the box's centre sat 3 m over the ground, 2026-10-07).
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
    float soles[3]{};            // the soles (sz_root's world) from the vehicle's position
    ULONGLONG ms=0;
};
SRWLOCK viewLock=SRWLOCK_INIT;
View view{};
volatile ULONGLONG placedMs=0;   // GameMs the hook last placed the camera at

// The camera's eye and its centre ray's unit direction for `w` with the vehicle at `pos` (its position field).
void ViewRay(const View& w,const float* pos,float* eye,float* dir) noexcept {
    const float cp=std::cos(w.pitch);
    dir[0]=std::sin(w.yaw)*cp;dir[1]=std::sin(w.pitch);dir[2]=std::cos(w.yaw)*cp;
    const float right[3]={-std::cos(w.yaw),0.0f,std::sin(w.yaw)};
    for(int k=0;k<3;++k)eye[k]=pos[k]+w.soles[k]+(k==1 ? kViewUp+kViewRise : 0.0f)-dir[k]*kViewBack+right[k]*kViewSide;
    eye[1]=std::fmax(eye[1],w.floor+kViewFloor);
}

// The view of `m` now (no vehicle or seat): its heading, its aim's pitch, the ground under it, its soles from `v`'s position.
View ViewOf(const Mech& m,const unsigned char* v) noexcept {
    View w;
    w.yaw=m.heading;w.pitch=m.aimPitch;
    if(!m.rootOk)return w;
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    for(int k=0;k<3;++k)w.soles[k]=m.root[12+k]-p[k];
    w.floor=m.root[13]-(m.fl.air && m.feetClear!=kNoGround ? m.feetClear : 0.0f);
    return w;
}

void PublishView(const Mech& m,unsigned char* v) noexcept {
    View w=ViewOf(m,v);
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

// The aim this frame: the player's along the camera's centre ray (what it meets within kAimRange, else its far end);
// an NPC's from the muzzle along its heading and pitch. The rifle arm is then turned onto it (the pose's aim yaw and
// pitch from the right shoulder, sazabi_pose.h AimRifle), the cannon's beams and the HUD's reticle go to it.
void Aim(Mech& m,const unsigned char* v) noexcept {
    Arms& a=m.arms;
    a.hasAim=false;a.aimHit=false;a.centred=false;
    if(!m.rootOk)return;
    float from[3],dir[3];
    if(!m.npc) {
        ViewRay(ViewOf(m,v),reinterpret_cast<const float*>(v+kPosition),from,dir);
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
    float muzzle[3];
    BoneAt(m,sazabi::kMuzzle,nullptr,muzzle);
    const float d[3]={a.aim[0]-muzzle[0],a.aim[1]-muzzle[1],a.aim[2]-muzzle[2]};
    a.aimRange=Len(d);
    a.hasAim=a.swing<0.0f;
    // the arm onto the aim point: its direction from the right shoulder, in sz_root's frame (x left, z ahead)
    float local[3],sh[3];
    ToRoot(m,a.aim,local);
    std::memcpy(sh,scratch.modelPos[sazabi::kShoulderR],12);
    const float r[3]={local[0]-sh[0],local[1]-sh[1],local[2]-sh[2]};
    const float flat=std::sqrt(r[0]*r[0]+r[2]*r[2]);
    if(flat>1.0f) {
        m.pose.aimYaw=std::atan2(r[0],r[2]);
        m.pose.aimPitch=std::atan2(r[1],flat);
    }
}
