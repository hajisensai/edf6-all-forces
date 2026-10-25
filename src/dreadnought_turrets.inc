// Included by dreadnought.cpp in its file namespace: its turrets and the view aboard (the user, 2026-10-21: 「船的炮塔是
// 可以上人的」, then 「无畏舰有自己的炮塔…那些炮塔应该都是可以上去人开的」「独立的hud显示呢？还有第一人称」).
// Eight turrets on its upper deck (tools/prep_dreadnought.py: the Dreadnought game's secondary guns on the hull's turret
// seats), each three bones of its model: dn_turret_<n> (it turns about its seat's upright), dn_turret_<n>_gun (its guns
// pitch) and dn_turret_<n>_muzzle (where its rounds leave). Seat 1 + n is turret n's gunner (tools/make_dreadnought.py:
// the 410 door gunner's pose, every class, boarded at the ship's door; seatswitch.cpp moves a rider between its seats).
//  - The view (View, Look): every local player aboard has their own, world-held, turned by their aim stick / mouse at
//    TurretCamRate (the gunship's gun seat's way, playerjet_crew.inc): round the whole ship (kBridgeBack m behind it,
//    kBridgeUp over it), on its bridge and at a gun alike (TurretsFrame: near a turret it was inside the hull); with the
//    vehicles' first person on (firstperson.h FirstPersonVehicleView: the V key) from the seat's eye instead (the model's
//    vc_eye_<seat> marker: its bridge's windows, each turret's sight). DreadnoughtCamera (turretcam.cpp LookHook) puts
//    the camera there; the screen's centre is the aim.
//  - The look ahead (Cfg dreadnoughtAheadKey / Button held, any seat): the view's pivot out past its bow, the eye on it
//    (kAheadOut, kAheadUp): round the ship the hull fills the screen's middle (the user, 2026-10-25: 「无畏舰…整个身体挡
//    面前看不到了，可以右键的时候往前看」). The aim is the screen's centre all the same.
//  - A gunner's turret swings after their aim at kYawRate / kPitchRate within its elevations (kPitchLow .. kPitchHigh);
//    their primary trigger fires its guns once the turret is on its aim (kOnAim) as the fixed emplacement made of that
//    gun fires (the user, 2026-10-25: 「无畏舰的炮塔攻击方式太抽象了，换成做好的固定炮台的那种吧」; pylib/emplacement_model.py,
//    tools/make_tracked_combat.py GUNS['emp_<kind>']): its two barrels in turn, each every kGuns[k].gapMs, the
//    emplacement's round (jet_bay.cpp DreadnoughtRound: its speed, life, blast, tracer and damage) from that barrel's
//    mouth, kGuns[k].barrel m to either side of the muzzle bone across its guns, at where the screen's centre meets the
//    map (or kAimReach out along it), scattered as its emplacement's native gun scatters (Spread: the polar angle off the
//    line even in 0 .. kGuns[k].spread, its FireAccuracy times weapon+0xE14, docs/feedback-2026-10-25-missiles.md); a map
//    point past the gun's reach (kGuns[k].reach) it does not fire at (the sight says so), the sky it does
//    (its round goes as far as it goes). It had been the gunship's cannon round from the one muzzle, every turret alike.
//  - The commander on the bridge trains every turret nobody mans on their aim, and the primary trigger fires them all
//    (each as it comes on: its guns' own arc decides).
//  - A turret nobody trains swings back to its rest (ahead, level).
// The HUD's sight (hud.cpp GunnerMarks) is on the aim; its panel (hud.cpp DreadnoughtHud) reads the ship (Publish).
// Every turret is posed every frame (its two local matrices: Pose), the engine composing the worlds (sazabi.cpp's way).
// Online (dreadnought_net.h, over seat_net.h's tag 15): each machine sends the turrets its players train (their aim kSendMs
// apart, at once when one fires); a turret a remote player trains turns to their aim here (fresh within kRemoteMs, else
// back to its rest) and its rounds are fired here too, of no damage (the damage is theirs, once).
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
// Its three kinds of secondary gun, each as its emplacement (pylib/vcobjects.py DREADNOUGHT_TURRET_GUNS, tools/selftest.py
// dreadnought_agrees): the round (jet_bay.cpp kDnRounds), a barrel's gap (the emplacement's FireInterval frames), the
// round's reach (its speed times its life), its barrels' half spacing (tools/make_dreadnought.py measures it on the
// model's guns) and its scatter (rad: the cone's half angle, the emplacement's FireAccuracy times the native weapon+0xE14
// scale, which a vehicle's Weapon_VehicleShoot has at 1: launcher.cpp's LAUNCHER log, the Katyusha's, x1.000). Turret n is kind kGunOf[n]
// (tools/prep_dreadnought.py: 0 / 1 the SecMid, 2-5 the SecShort, 6 / 7 the SecLong, the emplacements' turret_0 / _2 / _6).
struct Gun { int round; ULONGLONG gapMs; float reach,barrel,spread; };
constexpr Gun kGuns[]={{0,200,1440.0f,2.46f,0.003f},{1,1000,2100.0f,4.07f,0.004f},{2,667,3840.0f,5.7f,0.0015f}};
constexpr int kGunOf[kTurrets]={0,0,1,1,1,1,2,2};
constexpr float kAimReach=3000.0f;           // m: the aim along the view with no map nearer
constexpr float kBridgeUp=0.3f*kKeel+kKeel,kBridgeBack=2.2f*kReach;   // m: the commander's eye over the ship, behind it
constexpr float kBridgePitch=-10.0f*kDeg;    // rad: the commander's view as they take the bridge
constexpr float kEyeFloor=6.0f;              // m: a view's eye at least this over the ground under it
constexpr float kAheadOut=0.5f*kLength+20.0f,kAheadUp=10.0f;   // m: the look ahead's eye past its centre, over it
constexpr std::size_t kSeatAimStick=0x2D0;   // the rider's aim stick (the mouse's per-frame share), docs/camera-re.md §4
constexpr std::size_t kSeatFire=0x2E4;       // the seat's primary trigger (docs/heli-input-re.md §3)
constexpr float kSeatFireOn=0.8f;
constexpr ULONGLONG kSendMs=50;             // ms between a machine's aim blocks
constexpr ULONGLONG kRemoteMs=600;          // ms a remote player's aim stays fresh

struct Turret {
    unsigned char* rec[3]{};      // its bones' records (yaw, gun, muzzle)
    float bindYaw[16]{},bindGun[16]{};   // their bind locals (each a translation: the model's bones are level)
    float yaw=0.0f,pitch=0.0f;    // where it points now, in the ship's frame (rad: 0, 0 ahead and level)
    bool onAim=false;             // trained on its aim this frame
    bool inReach=false;           // ...and that aim within its gun's reach (or the sky)
    ULONGLONG shotAt=0;
    int shots=0;                  // its rounds (the even ones its left barrel's)
};
// A turret's remote trainer, as their blocks tell it (ReceivedTurrets).
struct Remote { float yaw=0.0f,pitch=0.0f; ULONGLONG at=0; bool fire=false; float aim[3]{}; };
struct Turrets {
    const void* bones=nullptr;    // the instance's bone array the records were found in
    bool ok=false,said=false;
    Turret t[kTurrets]{};
    Remote remote[kTurrets]{};
    seat_net::BasicGate<64> gate{};
    dreadnought_net::State out{};  // this machine's next block: the turrets its players train and what they fired
    ULONGLONG sentAt=0;
    std::uint8_t mine=0,others=0,commanded=0;   // this frame: trained by this machine's gunners, by remote players, by
                                                // the commander on the bridge
};
Turrets turrets[kMaxShips]{};

// The local player's view aboard (one player, one seat; the camera hook reads it on its own thread). Its points are kept
// from the ship's position (`off`, `eyeOff`): the hook adds the position it finds, the frame it draws.
struct View {
    ObjRef ref; const unsigned char* vehicle; const unsigned char* seat; int seatIndex;
    float yaw,pitch;              // world-held (tcam's: yaw 0 along +z)
    float off[3];                 // the orbit's pivot, from the ship's position
    float back,up;                // m behind the pivot along the view, over it
    bool firstPerson; float eyeOff[3];   // the seat's eye, from the ship's position
    float floorY;                 // the eye at least this high
    ULONGLONG frame;
};
View view{};
SRWLOCK viewLock=SRWLOCK_INIT;
// The local rider's aim stick this frame as it stood before the stock input (DreadnoughtInput: where the vehicles'
// observation reads it, vehicle_look.cpp, and the mouse is seen there): the view turns by it (the user, 2026-10-21:
// 「怎么只能用鼠标中键移动视角以后才能移动炮塔方位」: read after the stock input, the mouse never turned it). Game thread.
struct AimIn { const unsigned char* seat=nullptr; ULONGLONG frame=0; float x=0.0f,y=0.0f; };
AimIn aimIn{};
GunnerReadout sight{};
ULONGLONG sightFrame=0;
DreadnoughtCue cue{};
ULONGLONG cueFrame=0;

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

// Turret `t`'s next round's barrel mouth (its kind `gun`'s): its muzzle bone's point kGuns[gun].barrel m along the bone's
// left (its world matrix's first row: its guns' across, the model's bones level), the left barrel on even rounds.
void Mouth(const Turret& t,int gun,float* out) noexcept {
    const float* m=reinterpret_cast<const float*>(t.rec[2]+kBoneWorld506);
    const float side=(t.shots%2==0 ? 1.0f : -1.0f)*kGuns[gun].barrel;
    for(int i=0;i<3;++i)out[i]=m[12+i]+m[i]*side;
}

// A world direction in the ship's frame (its matrix's rows are its axes).
void ToShip(const unsigned char* v,const float* d,float* out) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    for(int r=0;r<3;++r)out[r]=d[0]*m[4*r]+d[1]*m[4*r+1]+d[2]*m[4*r+2];
}

// Where the camera of view `w` is (the hook's thread too: the ship's position read as it is now).
bool ViewPlace(const View& w,float* eye,float* look) noexcept {
    const float* p=reinterpret_cast<const float*>(w.vehicle+kPosition);
    float dir[3];
    tcam::Dir(w.yaw,w.pitch,dir);
    for(int i=0;i<3;++i) {
        eye[i]=w.firstPerson ? p[i]+w.eyeOff[i] : p[i]+w.off[i]+(i==1 ? w.up : 0.0f)-dir[i]*w.back;
        look[i]=eye[i]+dir[i]*100.0f;
    }
    if(!w.firstPerson && eye[1]<w.floorY){look[1]+=w.floorY-eye[1];eye[1]=w.floorY;}
    for(int i=0;i<3;++i)if(!std::isfinite(eye[i]+look[i]))return false;
    return true;
}

// Whether the local rider at `seat` holds the look ahead (Cfg dreadnoughtAheadKey, on a pad dreadnoughtAheadButton: it
// gives way while the thermal sight's chord it is part of is held, seat_switch_logic.h ChordOwns, as the sight's zoom does).
bool LookAhead(const unsigned char* seat) noexcept {
    const Config& c=Cfg();
    if(!Pad(seat))return KeyDown(c.dreadnoughtAheadKey);
    return Button(seat,c.dreadnoughtAheadButton) &&
           !seatsw::ChordOwns(At<std::uint16_t>(seat,kSeatButtons),static_cast<unsigned>(c.thermalButton),static_cast<unsigned>(c.dreadnoughtAheadButton));
}

// The view's pivot and its eye's distance behind and over it: round the whole ship (its centre, kBridgeBack behind it,
// kBridgeUp over), or with the look ahead held out past its bow (kAheadOut along its heading, kAheadUp over), the eye on it.
void ViewPivot(const unsigned char* v,const unsigned char* seat,float* pivot,float* back,float* up) noexcept {
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    std::memcpy(pivot,p,12);
    *back=kBridgeBack;*up=kBridgeUp;
    if(!LookAhead(seat))return;
    const float* f=reinterpret_cast<const float*>(v+kMatrix)+8;
    const float flat=std::sqrt(f[0]*f[0]+f[2]*f[2]);
    if(!(flat>1e-3f))return;
    pivot[0]+=f[0]/flat*kAheadOut;pivot[2]+=f[2]/flat*kAheadOut;pivot[1]+=kAheadUp;
    *back=0.0f;*up=0.0f;
}

// A frame of the local player's view at seat `index` of `v` (its pivot `pivot`, world): begun on the camera as it is
// drawn (the bridge's: along its heading, a little down), then turned by their stick; first person from the seat's eye.
void Look(unsigned char* v,const unsigned char* seat,int index,const float* pivot,float back,float up) noexcept {
    AcquireSRWLockShared(&viewLock);const View was=view;ReleaseSRWLockShared(&viewLock);
    float yaw=was.yaw,pitch=was.pitch;
    if(was.seat!=seat || !was.ref.Is(v)) {
        float eye[3],dir[3];
        if(index==0) {
            const float* f=reinterpret_cast<const float*>(v+kMatrix)+8;
            yaw=tcam::YawOf(f);pitch=kBridgePitch;
        } else if(CameraRay(eye,dir)){yaw=tcam::YawOf(dir);pitch=tcam::PitchOf(dir);}
        else return;
    }
    const float lateX=At<float>(seat,kSeatAimStick),lateY=At<float>(seat,kSeatAimStick+4);
    const bool early=aimIn.seat==seat && aimIn.frame==GameFrame();
    const float x=early ? aimIn.x : lateX,y=early ? aimIn.y : lateY;
    const float rate=Cfg().turretCamRate*tcam::kPi/180.0f/60.0f;
    if(std::isfinite(x) && std::isfinite(y)) {
        yaw=tcam::Wrap(yaw+x*rate);
        pitch=Clamp(pitch-y*rate,-tcam::kPitchMost,tcam::kPitchMost);
    }
    static ULONGLONG lookLogAt=0;
    if(Cfg().debug && GameMs()-lookLogAt>=kLogMs) {
        lookLogAt=GameMs();
        Log("DREADNOUGHT v=%p view: seat %d pad %d aim stick (%.3f,%.3f)%s, after the stock input (%.3f,%.3f), yaw %.1f pitch %.1f",
            v,index,Pad(seat) ? 1 : 0,x,y,early ? "" : " (none before it)",lateX,lateY,yaw/kDeg,pitch/kDeg);
    }
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    View now{ObjRef::Of(v),v,seat,index,yaw,pitch,{},back,up,false,{},-1.0e9f,GameFrame()};
    for(int i=0;i<3;++i)now.off[i]=pivot[i]-p[i];
    unsigned char* const me=PlayerHuman();
    float eye[3];RideView ride{};
    if(me && FirstPersonVehicleView(v,static_cast<unsigned>(index)) && VehicleEye(me,eye,&ride) && ride.vehicle==v &&
       ride.seat==index) {
        now.firstPerson=true;
        for(int i=0;i<3;++i)now.eyeOff[i]=eye[i]-p[i];
    } else {
        float look[3];
        if(ViewPlace(now,eye,look)) {
            const float clear=FixedClearance(eye);
            if(clear!=kNoGround)now.floorY=eye[1]-clear+kEyeFloor;
        }
    }
    AcquireSRWLockExclusive(&viewLock);view=now;ReleaseSRWLockExclusive(&viewLock);
}

// Where the screen's centre meets the map (`ground`), or kAimReach out along it, from the camera as drawn.
bool AimPoint(float* aim,float* from,bool* ground) noexcept {
    float eye[3],dir[3];
    if(!CameraRay(eye,dir))return false;
    const float end[3]={eye[0]+dir[0]*kAimReach,eye[1]+dir[1]*kAimReach,eye[2]+dir[2]*kAimReach};
    *ground=MapFixedRay(eye,end,aim)>=0.0f;   // its own hull (layer 17, moving) passed
    if(!*ground)std::memcpy(aim,end,12);
    if(from)std::memcpy(from,eye,12);
    return true;
}

// Where round `n` of a gun with the scatter `half` (rad) goes for `at`, fired from `from`, as `out`: the native fire's
// cone (0x691B02 -> 0x4E820, launcher.cpp Cone): off the line by a polar angle even in 0 .. half, round it by any
// azimuth. The plugin's rounds take no random state: the polar angle steps through kSpreadSteps even bands in a
// scrambled order, the azimuth by the golden angle (the same burst lands the same).
constexpr int kSpreadSteps=16;
void Spread(const float* from,const float* at,float half,int n,float* out) noexcept {
    std::memcpy(out,at,12);
    const float d[3]={at[0]-from[0],at[1]-from[1],at[2]-from[2]};
    const float len=vec::Len(d);
    if(!(half>0.0f) || !(len>0.0f) || !std::isfinite(len))return;
    const float dir[3]={d[0]/len,d[1]/len,d[2]/len};
    const float side[3]={std::fabs(dir[1])<0.99f ? 0.0f : 1.0f,std::fabs(dir[1])<0.99f ? 1.0f : 0.0f,0.0f};
    float u[3]={dir[1]*side[2]-dir[2]*side[1],dir[2]*side[0]-dir[0]*side[2],dir[0]*side[1]-dir[1]*side[0]};
    const float ul=vec::Len(u);
    for(float& x:u)x/=ul;
    const float w[3]={dir[1]*u[2]-dir[2]*u[1],dir[2]*u[0]-dir[0]*u[2],dir[0]*u[1]-dir[1]*u[0]};
    const unsigned k=static_cast<unsigned>(n)%static_cast<unsigned>(kSpreadSteps);
    const float band=static_cast<float>((k*7u)%static_cast<unsigned>(kSpreadSteps));   // 7 is prime to 16: every band once
    const float polar=half*(band+0.5f)/static_cast<float>(kSpreadSteps),azimuth=2.39996323f*static_cast<float>(n);
    const float r=len*std::tan(polar),cs=std::cos(azimuth)*r,sn=std::sin(azimuth)*r;
    for(int c=0;c<3;++c)out[c]=at[c]+u[c]*cs+w[c]*sn;
}

// The gap before turret `n`'s next round: its two barrels in turn, each kGuns[gun].gapMs.
ULONGLONG GapOf(int n) noexcept { return kGuns[kGunOf[n]].gapMs/2; }

// Turret `n`'s next round at `aim` from its next barrel's mouth (`real`: the damage this machine's), its barrels turned.
bool Fire(Turret& t,unsigned char* v,int n,const float* aim,bool real) noexcept {
    const int gun=kGunOf[n];
    float mouth[3],at[3];
    Mouth(t,gun,mouth);
    Spread(mouth,aim,kGuns[gun].spread,t.shots,at);
    if(!DreadnoughtRound(v,kGuns[gun].round,mouth,at,real))return false;
    ++t.shots;
    return true;
}

// Turret `n` trained on `aim` this frame (on the map: `ground`; its trainer's trigger `fire`): swung toward it, a round
// when on it, within its gun's reach and ready. True: a round left.
bool Train(Turrets& r,unsigned char* v,int n,const float* aim,bool ground,bool fire,float dt,ULONGLONG ms) noexcept {
    Turret& t=r.t[n];
    const float* muzzle=BoneAt(t.rec[2]);
    const float to[3]={aim[0]-muzzle[0],aim[1]-muzzle[1],aim[2]-muzzle[2]};
    float local[3];
    ToShip(v,to,local);
    t.onAim=Swing(t,std::atan2(local[0],local[2]),std::atan2(local[1],std::sqrt(local[0]*local[0]+local[2]*local[2])),dt);
    t.inReach=t.onAim && (!ground || vec::Len(to)<=kGuns[kGunOf[n]].reach);
    if(!(t.inReach && fire && ms-t.shotAt>=GapOf(n) && Fire(t,v,n,aim,true)))return false;
    t.shotAt=ms;
    r.out.fired|=static_cast<std::uint8_t>(1u<<n);
    std::memcpy(r.out.aim[n],aim,12);
    if(Cfg().debug && t.shots%10==1)
        Log("DREADNOUGHT v=%p turret %d (gun %d) round #%d at (%.0f,%.0f,%.0f), %.0f m (reach %.0f)",v,n,kGunOf[n],t.shots,aim[0],
            aim[1],aim[2],vec::Len(to),kGuns[kGunOf[n]].reach);
    return true;
}

// The HUD's sight on `aim`: `on` of `trained` turrets on it and within their reach, the next round in `wait` s.
void Sight(const unsigned char* seat,const float* aim,const float* eye,int on,int trained,float wait) noexcept {
    GunnerReadout& h=sight;
    h=GunnerReadout{};
    std::memcpy(h.sight,aim,12);
    const float d[3]={aim[0]-eye[0],aim[1]-eye[1],aim[2]-eye[2]};
    h.ground=true;h.inReach=on>0;h.ready=trained>0 && wait<=0.0f;h.range=vec::Len(d);
    h.wait=std::fmax(0.0f,wait);
    h.gun=GunnerGun::cannon;h.guns=1u<<static_cast<unsigned>(GunnerGun::cannon);h.zoom=1.0f;
    h.keys=!Pad(seat);
    sightFrame=GameFrame();
}

// This machine's trained turrets to the room (dreadnought_net.h): kSendMs apart, at once when one fired.
void SendTurrets(Turrets& r,unsigned char* v,ULONGLONG ms) noexcept {
    if(!r.out.manned || !(r.out.fired || ms-r.sentAt>=kSendMs))return;
    if(!drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128))){r.out=dreadnought_net::State{};return;}
    dreadnought_net::State s=r.out;
    s.head.controller=-1;
    if(netReady && seat_net::Stamp(&s,sizeof(s)) && dreadnought_net::Valid(s) && seat_net::Send(v,&s,sizeof(s)))r.sentAt=ms;
    r.out=dreadnought_net::State{};
}

// The local player's seat aboard `v` (0 the bridge, 1 + n turret n), or -1.
int LocalSeat(unsigned char* v) noexcept {
    unsigned char* const me=PlayerHuman();
    if(!me)return -1;
    const int seats=SeatCount(v);
    for(int i=0;i<seats && i<=kTurrets;++i) {
        const unsigned char* const seat=SeatAt(v,i);
        if(SeatRider(seat)==Rider::player && At<const void*>(seat,kSeatRider)==me)return i;
    }
    return -1;
}

// Every turret this frame: the local gunner's own, the commander's (every one nobody mans, when the local player is on
// the bridge), a remote player's (their blocks), the others back to their rest; all posed. The local player's view.
void TurretsFrame(Ship& s,unsigned char* v,float dt,ULONGLONG ms) noexcept {
    Turrets& r=TurretsOf(s);
    const int here=LocalSeat(v);
    if(here<0) {
        AcquireSRWLockShared(&viewLock);const bool mine=view.vehicle==v;ReleaseSRWLockShared(&viewLock);
        if(mine){AcquireSRWLockExclusive(&viewLock);view=View{};ReleaseSRWLockExclusive(&viewLock);}
    }
    const unsigned char* const seat=here>=0 ? SeatAt(v,static_cast<unsigned>(here)) : nullptr;
    // a gunner's third person is the bridge's, round the whole ship (or the look ahead): near their turret the camera was
    // inside the hull's geometry from every offset tried (its seat, its sight 2-8 m up and 5-22 m back: range 2026-10-21);
    // their turret trains on the screen's centre all the same, and the first person (V) is its sight's
    if(seat) {
        float pivot[3],back,up;
        ViewPivot(v,seat,pivot,&back,&up);
        Look(v,seat,here,pivot,back,up);
    }
    if(!Rig(r,v))return;   // no turrets: the view still
    float aim[3],eye[3];
    bool ground=false;
    const bool aimed=here>=0 && AimPoint(aim,eye,&ground);
    const float trigger=seat ? At<float>(seat,kSeatFire) : 0.0f;
    const bool fire=aimed && std::isfinite(trigger) && trigger>=kSeatFireOn;
    r.mine=r.others=r.commanded=0;
    int on=0,trained=0;
    float wait=1.0e9f;
    for(int n=0;n<kTurrets;++n) {
        Turret& t=r.t[n];
        Remote& rm=r.remote[n];
        const std::uint8_t bit=static_cast<std::uint8_t>(1u<<n);
        const unsigned char* const gunSeat=1+n<static_cast<int>(SeatCount(v)) ? SeatAt(v,static_cast<unsigned>(1+n)) : nullptr;
        const bool gunner=gunSeat && SeatRider(gunSeat)==Rider::player;   // any player's (here or remote)
        const bool own=here==1+n;
        const bool remote=!own && rm.at && ms-rm.at<kRemoteMs;
        t.onAim=t.inReach=false;
        if(own || (here==0 && !gunner && !remote)) {   // the local gunner's, or the commander's
            if(aimed) {
                Train(r,v,n,aim,ground,fire,dt,ms);
                ++trained;
                if(t.inReach)++on;
                const ULONGLONG since=ms-t.shotAt,gap=GapOf(n);
                wait=std::fmin(wait,since>=gap ? 0.0f : static_cast<float>(gap-since)/1000.0f);
            }
            (own ? r.mine : r.commanded)|=bit;
            r.out.manned|=bit;
            r.out.yaw[n]=t.yaw;r.out.pitch[n]=t.pitch;
        } else if(remote) {   // a remote player's: their aim, and their round of no damage
            Swing(t,rm.yaw,rm.pitch,dt);
            if(rm.fire){rm.fire=false;Fire(t,v,n,rm.aim,false);}
            r.others|=bit;
        } else {
            Swing(t,0.0f,0.0f,dt);
            if(gunner)r.others|=bit;
        }
        Pose(t);
    }
    SendTurrets(r,v,ms);
    if(aimed)Sight(seat,aim,eye,on,trained,trained ? wait : 0.0f);
}
