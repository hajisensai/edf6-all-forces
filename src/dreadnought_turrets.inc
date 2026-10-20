// Included by dreadnought.cpp in its file namespace: its turrets (the user, 2026-10-21: 「船的炮塔是可以上人的」).
// Eight turrets on its upper deck (tools/prep_dreadnought.py: the Dreadnought game's secondary guns on the hull's turret
// seats), each three bones of its model: dn_turret_<n> (it turns about its seat's upright), dn_turret_<n>_gun (its guns
// pitch) and dn_turret_<n>_muzzle (where its rounds leave). Seat 1 + n is turret n's gunner (tools/make_dreadnought.py:
// the 410 door gunner's pose, every class, boarded at the ship's door; seatswitch.cpp moves a rider between seats).
//  - The local player in a gunner seat: the view is theirs (GunnerLook: a world-held heading and elevation turned by their
//    aim stick at TurretCamRate, as the gunship's gun seat, playerjet_crew.inc), the camera over and behind their turret
//    along it (DreadnoughtGunnerCamera, from turretcam.cpp LookHook); the turret swings after the view at kYawRate /
//    kPitchRate within its elevations (kPitchLow .. kPitchHigh); the primary trigger fires its guns (jet_bay.cpp
//    DreadnoughtRound: the gunship's cannon round, from its muzzle at where the screen's centre meets the map, or
//    kAimReach out along it) every kGapMs once the turret is on its aim (kOnAim); the HUD's gunner sight (hud.cpp
//    GunnerMarks) on that point.
//  - A turret nobody of this machine mans swings back to its rest (ahead, level).
// Every turret is posed every frame (its two local matrices: Pose), the engine composing the worlds (sazabi.cpp's way).
// Online (dreadnought_net.h, over seat_net.h's tag 15): each machine sends its own gunner's turret (its aim kSendMs apart,
// at once when it fires); a turret a remote gunner mans turns to their aim here (fresh within kRemoteMs, else back to its
// rest) and their rounds are fired here too, of no damage (the damage is theirs, once).
constexpr int kTurrets=8;                    // pylib/vcobjects.py DREADNOUGHT_TURRETS (tools/selftest.py dreadnought_agrees)
const wchar_t* const kTurretBones[kTurrets][3]={
    {L"dn_turret_0",L"dn_turret_0_gun",L"dn_turret_0_muzzle"},{L"dn_turret_1",L"dn_turret_1_gun",L"dn_turret_1_muzzle"},
    {L"dn_turret_2",L"dn_turret_2_gun",L"dn_turret_2_muzzle"},{L"dn_turret_3",L"dn_turret_3_gun",L"dn_turret_3_muzzle"},
    {L"dn_turret_4",L"dn_turret_4_gun",L"dn_turret_4_muzzle"},{L"dn_turret_5",L"dn_turret_5_gun",L"dn_turret_5_muzzle"},
    {L"dn_turret_6",L"dn_turret_6_gun",L"dn_turret_6_muzzle"},{L"dn_turret_7",L"dn_turret_7_gun",L"dn_turret_7_muzzle"},
};
constexpr float kYawRate=45.0f*kDeg,kPitchRate=30.0f*kDeg;   // rad/s a turret turns, its guns lift
constexpr float kPitchLow=-8.0f*kDeg,kPitchHigh=75.0f*kDeg;  // its guns' elevations
constexpr float kOnAim=2.0f*kDeg;            // its guns this near their aim: it fires
constexpr ULONGLONG kGapMs=500;              // ms between a turret's rounds
constexpr float kAimReach=3000.0f;           // m: the aim along the view with no map nearer
constexpr float kCamUp=16.0f,kCamBack=42.0f; // m: the gunner's eye over its turret's seat and back along the view
constexpr std::size_t kSeatAimStick=0x2D0;   // the rider's aim stick (the mouse's per-frame share), docs/camera-re.md §4
constexpr std::size_t kSeatFire=0x2E4;       // the seat's primary trigger (docs/heli-input-re.md §3)
constexpr float kSeatFireOn=0.8f;
constexpr ULONGLONG kSendMs=50;             // ms between a gunner's aim blocks
constexpr ULONGLONG kRemoteMs=600;          // ms a remote gunner's aim stays fresh

struct Turret {
    unsigned char* rec[3]{};      // its bones' records (yaw, gun, muzzle)
    float bindYaw[16]{},bindGun[16]{};   // their bind locals (each a translation: the model's bones are level)
    float yaw=0.0f,pitch=0.0f;    // where it points now, in the ship's frame (rad: 0, 0 ahead and level)
    ULONGLONG shotAt=0;
    int shots=0;
};
// A turret's remote gunner, as their blocks tell it (ReceivedTurrets).
struct Remote { float yaw=0.0f,pitch=0.0f; ULONGLONG at=0; bool fire=false; float aim[3]{}; };
struct Turrets {
    const void* bones=nullptr;    // the instance's bone array the records were found in
    bool ok=false,said=false;
    Turret t[kTurrets]{};
    Remote remote[kTurrets]{};
    seat_net::BasicGate<64> gate{};
    dreadnought_net::State out{};  // this machine's next block: its gunner's turret and what it fired
    ULONGLONG sentAt=0;
};
Turrets turrets[kMaxShips]{};

// The gunner's view: one local player at one gun (the camera hook reads it on its own thread).
struct GunnerView { ObjRef ref; const unsigned char* vehicle; const unsigned char* seat; int turret; float yaw,pitch;
                    float pivot[3]; ULONGLONG frame; };
GunnerView gunnerView{};
SRWLOCK gunnerViewLock=SRWLOCK_INIT;
GunnerReadout gunnerHud{};
ULONGLONG gunnerHudFrame=0;

Turrets& TurretsOf(const Ship& s) noexcept { return turrets[&s-ships]; }

// Its turrets' bone records, found again whenever the instance's bone array moves.
bool Rig(Turrets& r,unsigned char* v) noexcept {
    const unsigned char* inst=v+kModelInst506;
    const void* bones=At<const void*>(inst,kInstBones506);
    if(bones==r.bones && r.ok)return true;
    r.bones=bones;r.ok=false;
    for(int n=0;n<kTurrets;++n)
        for(int k=0;k<3;++k) {
            unsigned char* rec=BoneRecord506(inst,kTurretBones[n][k]);
            if(!rec) {
                if(!r.said)Log("DREADNOUGHT v=%p: its model lacks bone %ls: no turrets (not EDF6VC_DN_DREADNOUGHT.MRAB?)",v,kTurretBones[n][k]);
                r.said=true;
                return false;
            }
            r.t[n].rec[k]=rec;
        }
    for(auto& t:r.t) {
        std::memcpy(t.bindYaw,t.rec[0]+kBoneLocal506,64);
        std::memcpy(t.bindGun,t.rec[1]+kBoneLocal506,64);
    }
    r.ok=true;
    Log("DREADNOUGHT v=%p rig: %d turrets",v,kTurrets);
    return true;
}

// Turret `t` at its yaw and pitch: the turn about its seat's upright, its guns' lift about their own across.
void Pose(Turret& t) noexcept {
    const float cy=std::cos(t.yaw),sy=std::sin(t.yaw),cp=std::cos(t.pitch),sp=std::sin(t.pitch);
    float m[16];
    std::memcpy(m,t.bindYaw,64);
    m[0]=cy;m[1]=0.0f;m[2]=-sy;m[4]=0.0f;m[5]=1.0f;m[6]=0.0f;m[8]=sy;m[9]=0.0f;m[10]=cy;
    std::memcpy(t.rec[0]+kBoneLocal506,m,64);
    std::memcpy(m,t.bindGun,64);
    m[0]=1.0f;m[1]=0.0f;m[2]=0.0f;m[4]=0.0f;m[5]=cp;m[6]=-sp;m[8]=0.0f;m[9]=sp;m[10]=cp;
    std::memcpy(t.rec[1]+kBoneLocal506,m,64);
}

// The way `t` turns this frame toward (wantYaw, wantPitch) at its rates; true when it is on that aim.
bool Swing(Turret& t,float wantYaw,float wantPitch,float dt) noexcept {
    const float dy=tcam::Wrap(wantYaw-t.yaw),dp=Clamp(wantPitch,kPitchLow,kPitchHigh)-t.pitch;
    t.yaw=tcam::Wrap(t.yaw+Clamp(dy,-kYawRate*dt,kYawRate*dt));
    t.pitch+=Clamp(dp,-kPitchRate*dt,kPitchRate*dt);
    return std::fabs(tcam::Wrap(wantYaw-t.yaw))<kOnAim && std::fabs(Clamp(wantPitch,kPitchLow,kPitchHigh)-t.pitch)<kOnAim;
}

const float* BoneAt(const unsigned char* rec) noexcept { return reinterpret_cast<const float*>(rec+kBoneWorld506)+12; }

// A world direction in the ship's frame (its matrix's rows are its axes).
void ToShip(const unsigned char* v,const float* d,float* out) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    for(int r=0;r<3;++r)out[r]=d[0]*m[4*r]+d[1]*m[4*r+1]+d[2]*m[4*r+2];
}

// A frame of the local gunner's view in the seat of turret `n`: begun on the view as it is drawn, then turned by their stick.
void GunnerLook(unsigned char* v,const unsigned char* seat,int n,const Turret& t) noexcept {
    AcquireSRWLockShared(&gunnerViewLock);const GunnerView was=gunnerView;ReleaseSRWLockShared(&gunnerViewLock);
    float yaw=was.yaw,pitch=was.pitch;
    if(was.seat!=seat || !was.ref.Is(v)) {
        float eye[3],dir[3];
        if(!CameraRay(eye,dir))return;
        yaw=tcam::YawOf(dir);pitch=tcam::PitchOf(dir);
    }
    const float x=At<float>(seat,kSeatAimStick),y=At<float>(seat,kSeatAimStick+4);
    const float rate=Cfg().turretCamRate*tcam::kPi/180.0f/60.0f;
    if(std::isfinite(x) && std::isfinite(y)) {
        yaw=tcam::Wrap(yaw+x*rate);
        pitch=Clamp(pitch-y*rate,-tcam::kPitchMost,tcam::kPitchMost);
    }
    GunnerView now{ObjRef::Of(v),v,seat,n,yaw,pitch,{},GameFrame()};
    std::memcpy(now.pivot,BoneAt(t.rec[0]),12);
    AcquireSRWLockExclusive(&gunnerViewLock);gunnerView=now;ReleaseSRWLockExclusive(&gunnerViewLock);
}

// The local gunner at turret `n`: the turret after their view, its guns at the screen's centre on the trigger.
void Man(Turrets& r,unsigned char* v,const unsigned char* seat,int n,Turret& t,float dt,ULONGLONG ms) noexcept {
    GunnerLook(v,seat,n,t);
    float eye[3],dir[3];
    if(!CameraRay(eye,dir))return;
    const float end[3]={eye[0]+dir[0]*kAimReach,eye[1]+dir[1]*kAimReach,eye[2]+dir[2]*kAimReach};
    float aim[3];
    const bool ground=MapRay(eye,end,aim)>=0.0f;
    if(!ground)std::memcpy(aim,end,12);
    const float* muzzle=BoneAt(t.rec[2]);
    const float to[3]={aim[0]-muzzle[0],aim[1]-muzzle[1],aim[2]-muzzle[2]};
    float local[3];
    ToShip(v,to,local);
    const bool onAim=Swing(t,std::atan2(local[0],local[2]),std::atan2(local[1],std::sqrt(local[0]*local[0]+local[2]*local[2])),dt);
    const float trigger=At<float>(seat,kSeatFire);
    const bool ready=ms-t.shotAt>=kGapMs;
    if(onAim && ready && std::isfinite(trigger) && trigger>=kSeatFireOn && DreadnoughtRound(v,muzzle,aim,true)) {
        t.shotAt=ms;
        r.out.fired|=static_cast<std::uint8_t>(1u<<n);
        std::memcpy(r.out.aim[n],aim,12);
        if(Cfg().debug && t.shots++%10==0)
            Log("DREADNOUGHT v=%p turret %d round #%d at (%.0f,%.0f,%.0f), %.0f m",v,n,t.shots,aim[0],aim[1],aim[2],vec::Len(to));
    }
    GunnerReadout& h=gunnerHud;
    h=GunnerReadout{};
    std::memcpy(h.sight,aim,12);
    h.ground=true;h.inReach=onAim;h.ready=ready;h.range=vec::Len(to);
    h.wait=ready ? 0.0f : static_cast<float>(kGapMs-(ms-t.shotAt))/1000.0f;
    h.gun=GunnerGun::cannon;h.guns=1u<<static_cast<unsigned>(GunnerGun::cannon);h.zoom=1.0f;
    h.keys=At<unsigned char>(seat,kSeatPad)==0;
    gunnerHudFrame=GameFrame();
}

// This machine's gunner's turret to the room (dreadnought_net.h): kSendMs apart, at once when it fired.
void SendTurrets(Turrets& r,unsigned char* v,ULONGLONG ms) noexcept {
    if(!r.out.manned || !(r.out.fired || ms-r.sentAt>=kSendMs))return;
    if(!drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128))){r.out=dreadnought_net::State{};return;}
    dreadnought_net::State s=r.out;
    s.head.controller=-1;
    if(netReady && seat_net::Stamp(&s,sizeof(s)) && dreadnought_net::Valid(s) && seat_net::Send(v,&s,sizeof(s)))r.sentAt=ms;
    r.out=dreadnought_net::State{};
}

// Every turret this frame: the local player's own (Man), a remote gunner's (their blocks), the others back to their rest; all
// posed.
void TurretsFrame(Ship& s,unsigned char* v,float dt,ULONGLONG ms) noexcept {
    Turrets& r=TurretsOf(s);
    if(!Rig(r,v))return;
    unsigned char* const me=PlayerHuman();
    bool manned=false;
    const int seats=SeatCount(v);
    for(int n=0;n<kTurrets;++n) {
        Turret& t=r.t[n];
        const unsigned char* const seat=1+n<seats ? SeatAt(v,1+n) : nullptr;
        Remote& rm=r.remote[n];
        if(seat && me && SeatRider(seat)==Rider::player && At<const void*>(seat,kSeatRider)==me) {
            Man(r,v,seat,n,t,dt,ms);
            manned=true;
            r.out.manned|=static_cast<std::uint8_t>(1u<<n);
            r.out.yaw[n]=t.yaw;r.out.pitch[n]=t.pitch;
        } else if(rm.at && ms-rm.at<kRemoteMs) {   // a remote gunner's: their aim, and their round of no damage
            Swing(t,rm.yaw,rm.pitch,dt);
            if(rm.fire){rm.fire=false;DreadnoughtRound(v,BoneAt(t.rec[2]),rm.aim,false);}
        } else {
            Swing(t,0.0f,0.0f,dt);
        }
        Pose(t);
    }
    SendTurrets(r,v,ms);
    if(!manned) {
        AcquireSRWLockShared(&gunnerViewLock);const bool mine=gunnerView.vehicle==v;ReleaseSRWLockShared(&gunnerViewLock);
        if(mine){AcquireSRWLockExclusive(&gunnerViewLock);gunnerView=GunnerView{};ReleaseSRWLockExclusive(&gunnerViewLock);}
    }
}
