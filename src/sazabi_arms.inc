// The Sazabi's arms (sazabi.cpp includes this inside its anonymous namespace; docs/gundam-plan.md 武器). The player's
// two fire buttons are taken from the 506 (its fire bytes 0x2020 / 0x2021 cleared each frame: stock, the primary fires
// holders 0 and 1 together, the beam rifle and the shield missiles) and routed here:
//  - primary: the beam rifle (holder 0 on sz_muzzle, pylib/vcobjects.py SAZABI_WEAPONS: its trigger latch held);
//  - secondary: the special picked (SazabiSwitchKey / Button cycles them): the shield missiles (holder 1, the game's
//    own lock, lockon.h), the funnels (all six launched, each flying at an enemy round the mech and firing its short
//    beam, back to their packs after kFunnelSec), or the chest's diffuse mega particle cannon (held: it charges, the
//    glow at its muzzle growing; let go: a fan of kMegaBeams beams for kMegaSec, steered with the chest);
//  - SazabiMeleeKey / Button: the beam tomahawk drawn and swung (its strike a drill charge at each enemy in reach and
//    one ahead for the buildings); pressed again mid-swing, another follows;
//  - SazabiGuardKey / Button held: the shield up, the damage taken from ahead SazabiGuardShare of itself.
// Beams are the plugin's rounds (crew.h EmcRound sz*: DemoIndirectFire objects of the Sazabi's team, jet_bay.cpp).
// Funnels fly in the world (a spring to their station) and are drawn there by their bones (sazabi_pose.h Funnels).
// The thrusters' flames and the funnels' are Booster flames (booster.cpp FlareFlames). Every effect has its sound
// (sazabi_sound.h); the HUD reads SazabiCue.

// Its constants and the state it keeps: sazabi_arms.h.

// The buttons this frame, besides the sticks (Controls): the two fire bytes and the ini's keys / pad buttons.
struct ArmsInput { bool primary,secondary,cycle,melee,guard; };
bool Pressed(const unsigned char* seat,int key,int button) noexcept {
    if(At<unsigned char>(seat,kSeatPad)==0)return KeyDown(key);
    return button && (At<std::uint16_t>(seat,kSeatButtons)&static_cast<std::uint16_t>(button))!=0;
}
ArmsInput TakeButtons(unsigned char* v,const unsigned char* seat) noexcept {
    ArmsInput a{};
    a.primary=v[kFireGun]!=0;a.secondary=v[kFireMissile]!=0;
    v[kFireGun]=0;v[kFireMissile]=0;   // the 506 fires nothing of its own
    a.cycle=Pressed(seat,Cfg().sazabiSwitchKey,Cfg().sazabiSwitchButton);
    a.melee=Pressed(seat,Cfg().sazabiMeleeKey,Cfg().sazabiMeleeButton);
    a.guard=Pressed(seat,Cfg().sazabiGuardKey,Cfg().sazabiGuardButton);
    return a;
}

// Seat 0's holder `i`'s weapon, or nullptr.
unsigned char* HolderWeapon(unsigned char* v,std::uint64_t i) noexcept {
    if(SeatCount(v)==0)return nullptr;
    const auto seat=SeatAt(v,0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(i>=count || count>8 || !Readable(holders,count*8) || !Readable(holders[i],kHolderWeapon+8))return nullptr;
    unsigned char* const w=At<unsigned char*>(holders[i],kHolderWeapon);
    return Readable(w,kWeaponAmmo+4) ? w : nullptr;
}
void Trigger(unsigned char* w) noexcept {
    if(w && At<std::int32_t>(w,kWeaponAmmo)>0 && Readable(w+kWeaponTrigger,1,true))w[kWeaponTrigger]=1;
}
// Whether the weapon fired since the last frame (its rounds counted down against `last`); `since` the seconds since.
bool Watch(const unsigned char* w,std::int32_t* last,float* since,float dt) noexcept {
    const std::int32_t n=w ? At<std::int32_t>(w,kWeaponAmmo) : -1;
    *since+=dt;
    const bool fired=*last>=0 && n>=0 && n<*last;
    if(fired)*since=0.0f;
    *last=n;
    return fired;
}

// ------------------------------------------------------------------------------------------ frames
// sz_root's world this frame (its record's world is the frame before's: carried to the body's pose now, exhaust_pose.h).
void RootFrame(Mech& m,const unsigned char* v) noexcept {
    m.rootOk=false;
    if(!m.rigOk)return;
    exhaust::Observe(m.track,GameFrame(),reinterpret_cast<const float*>(v+kMatrix));
    exhaust::Carry(reinterpret_cast<const float*>(m.rec[sazabi::kRoot]+kBoneWorld506),m.track,m.root);
    m.rootOk=exhaust::Inverse(m.root,m.rootInv) && std::isfinite(m.root[12]+m.root[13]+m.root[14]);
}
void ToWorld(const Mech& m,const float* p,float* out) noexcept {
    for(int k=0;k<3;++k)out[k]=p[0]*m.root[k]+p[1]*m.root[4+k]+p[2]*m.root[8+k]+m.root[12+k];
}
void DirToWorld(const Mech& m,const float* d,float* out) noexcept {
    for(int k=0;k<3;++k)out[k]=d[0]*m.root[k]+d[1]*m.root[4+k]+d[2]*m.root[8+k];
    const float l=Len(out);
    if(l>1e-5f)for(int k=0;k<3;++k)out[k]/=l;
}
void ToRoot(const Mech& m,const float* p,float* out) noexcept {
    for(int k=0;k<3;++k)out[k]=p[0]*m.rootInv[k]+p[1]*m.rootInv[4+k]+p[2]*m.rootInv[8+k]+m.rootInv[12+k];
}
void DirToRoot(const Mech& m,const float* d,float* out) noexcept {
    for(int k=0;k<3;++k)out[k]=d[0]*m.rootInv[k]+d[1]*m.rootInv[4+k]+d[2]*m.rootInv[8+k];
    const float l=Len(out);
    if(l>1e-5f)for(int k=0;k<3;++k)out[k]/=l;
}
// Bone `b`'s joint in the world as last posed (scratch), and a point `off` in its frame.
void BoneAt(const Mech& m,int b,const float* off,float* out) noexcept {
    float local[3]={0.0f,0.0f,0.0f};
    if(off)sazabi::Apply(off,scratch.modelRot[b],local);
    const float p[3]={scratch.modelPos[b][0]+local[0],scratch.modelPos[b][1]+local[1],scratch.modelPos[b][2]+local[2]};
    ToWorld(m,p,out);
}
// Where funnel `k` sits in its pack (its bind offset from the pack, as the pack is posed), in the world.
void DockPoint(const Mech& m,int k,float* out) noexcept {
    const int b=sazabi::kFunnels[k],pack=sazabi::kBones[b].parent;
    const float off[3]={m.rig.joint[b][0]-m.rig.joint[pack][0],m.rig.joint[b][1]-m.rig.joint[pack][1],m.rig.joint[b][2]-m.rig.joint[pack][2]};
    BoneAt(m,pack,off,out);
}
void Forward(const Mech& m,float* out) noexcept { out[0]=std::sin(m.heading);out[1]=0.0f;out[2]=std::cos(m.heading); }

// ------------------------------------------------------------------------------------------ the enemies
struct Enemies { const unsigned char* self; float from[3]; int n; const void* obj[kMostTargets]; float at[kMostTargets][3]; float d2[kMostTargets]; };
void SeeEnemy(void* ctx,const void* object,const float* aim) {
    Enemies* const e=static_cast<Enemies*>(ctx);
    if(!aim || !std::isfinite(aim[0]+aim[1]+aim[2]))return;
    const float d[3]={aim[0]-e->from[0],aim[1]-e->from[1],aim[2]-e->from[2]};
    const float d2=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
    if(d2>kFunnelRange*kFunnelRange)return;
    int at=e->n<kMostTargets ? e->n++ : kMostTargets-1;   // kept nearest first
    if(at==kMostTargets-1 && e->n==kMostTargets && d2>=e->d2[at])return;
    while(at>0 && e->d2[at-1]>d2){e->obj[at]=e->obj[at-1];e->d2[at]=e->d2[at-1];std::memcpy(e->at[at],e->at[at-1],12);--at;}
    e->obj[at]=object;e->d2[at]=d2;std::memcpy(e->at[at],aim,12);
}
void SeeEnemies(const unsigned char* v,const float* from,Enemies* e) noexcept {
    e->self=v;e->n=0;std::memcpy(e->from,from,12);
    VisitEnemies(v,SeeEnemy,e);
}

// ------------------------------------------------------------------------------------------ the arms each frame
void Sfx(const Mech& m,SzSfx which,const float* at) noexcept { if(m.driven)SazabiSfx(which,at); }

void DropArms(Mech& m) noexcept {
    Arms& a=m.arms;
    RoundDrop(a.glow);
    for(auto& r:a.mega)RoundDrop(r);
    for(auto& f:a.funnels)f=Funnel{};
    a.swing=-1.0f;a.charge=0.0f;a.megaLeft=0.0f;a.queued=false;a.funnelLeft=0.0f;
}

// The tomahawk: drawn and swung; its strike half way through.
void Strike(Mech& m,unsigned char* v,const float* blade) noexcept {
    float fwd[3],ahead[3];
    Forward(m,fwd);
    const float* r=m.root+12;
    for(int k=0;k<3;++k)ahead[k]=r[k]+fwd[k]*kAxeAhead;
    ahead[1]+=kAxeHigh*0.6f;
    Enemies e{};
    SeeEnemies(v,ahead,&e);
    int hits=0;
    for(int i=0;i<e.n && hits<kAxeMostHits;++i) {
        const float* at=e.at[i];
        const float dx=at[0]-ahead[0],dz=at[2]-ahead[2],dy=at[1]-r[1];
        if(dx*dx+dz*dz>kAxeReach*kAxeReach || dy<-2.0f || dy>kAxeHigh+10.0f)continue;
        if((at[0]-r[0])*fwd[0]+(at[2]-r[2])*fwd[2]<0.0f)continue;   // behind it
        if(DrillCharge(v,blade,at,Cfg().sazabiAxeDamage)){++hits;Sfx(m,SzSfx::saberHit,at);}
    }
    float low[3]={r[0]+fwd[0]*kAxeAhead,r[1]+2.0f,r[2]+fwd[2]*kAxeAhead};   // what stands ahead of it: buildings, rocks
    DrillCharge(v,blade,low,Cfg().sazabiAxeDamage);
    if(Cfg().debug)Log("SAZABI v=%p tomahawk strike: %d enemies hit",v,hits);
}

void Tomahawk(Mech& m,unsigned char* v,const ArmsInput& in,float dt) noexcept {
    Arms& a=m.arms;
    const bool press=in.melee && !a.meleeHeld;
    a.meleeHeld=in.melee;
    float hand[3];
    if(a.swing<0.0f) {
        if(!press || a.charge>0.0f || a.megaLeft>0.0f)return;
        a.swing=0.0f;a.struck=false;a.whooshed=false;
        BoneAt(m,sazabi::kHandR,nullptr,hand);
        Sfx(m,SzSfx::saberOn,hand);
        return;
    }
    if(press)a.queued=true;
    a.swing+=dt/kSwingSec;
    float blade[3];
    BoneAt(m,sazabi::kAxeBlade,nullptr,blade);
    if(m.driven)SazabiLoop(SzLoop::saberHum,blade,m.vel,1.0f);
    if(!a.whooshed && a.swing>=sazabi::kWindUp){a.whooshed=true;Sfx(m,SzSfx::whoosh,blade);}
    if(!a.struck && a.swing>=0.5f*(sazabi::kWindUp+sazabi::kStrike)){a.struck=true;Strike(m,v,blade);}
    if(a.swing<1.0f)return;
    if(a.queued){a.queued=false;a.swing=0.0f;a.struck=false;a.whooshed=false;return;}
    a.swing=-1.0f;
    Sfx(m,SzSfx::saberOff,blade);
}

// The cannon's aim from the chest: the mech's heading, the aim's pitch, `yaw` deg aside; world.
void CannonDir(const Mech& m,float yaw,float* out) noexcept {
    const float y=yaw*sazabi::kDeg,p=m.aimPitch;
    const float local[3]={std::sin(y)*std::cos(p),std::sin(p),std::cos(y)*std::cos(p)};
    DirToWorld(m,local,out);
}
void BeamEnd(const float* from,const float* dir,float range,float* end) noexcept {
    float stop[3],hit[3];
    for(int k=0;k<3;++k)stop[k]=from[k]+dir[k]*range;
    if(MapRay(from,stop,hit)>=0.0f)std::memcpy(end,hit,12); else std::memcpy(end,stop,12);
}

void Cannon(Mech& m,unsigned char* v,bool held,bool let,float dt) noexcept {
    Arms& a=m.arms;
    a.cannonCool=std::fmax(0.0f,a.cannonCool-dt);
    float from[3];
    BoneAt(m,sazabi::kCannon,nullptr,from);
    if(a.megaLeft>0.0f) {   // the fan, steered with the chest
        a.megaLeft-=dt;
        for(int b=0;b<kMegaBeams;++b) {
            float dir[3],end[3];
            CannonDir(m,a.megaYaw[b],dir);
            BeamEnd(from,dir,kMegaRange,end);
            if(a.megaLeft<=0.0f || !RoundSteer(a.mega[b],from,end))RoundDrop(a.mega[b]);
        }
        a.brace=1.0f;
        return;
    }
    if(held && a.swing<0.0f && a.cannonCool<=0.0f && EmcRoundReady(EmcRound::szMega)) {   // charging
        a.charge=std::fmin(1.0f,a.charge+dt/kChargeSec);
        float fwd[3],tip[3];
        CannonDir(m,0.0f,fwd);
        for(int k=0;k<3;++k)tip[k]=from[k]+fwd[k]*0.3f;
        if(!a.glow.obj && EmcRoundReady(EmcRound::szCharge))a.glow=EmcFire(EmcRound::szCharge,v,from,tip,0.0f);
        RoundSteer(a.glow,from,tip);
        RoundSize(a.glow,kGlowThin+(kGlowThick-kGlowThin)*a.charge);
        if(m.driven)SazabiLoop(SzLoop::cannonCharge,from,m.vel,a.charge);
        a.brace=a.charge;
        return;
    }
    RoundDrop(a.glow);
    if(let && a.charge>=kChargeLeast) {   // the fan of beams
        a.megaDamage=Cfg().sazabiCannonDamage*a.charge;
        for(int b=0;b<kMegaBeams;++b) {
            float dir[3],end[3];
            a.megaYaw[b]=kMegaFan[b];
            CannonDir(m,a.megaYaw[b],dir);
            BeamEnd(from,dir,kMegaRange,end);
            a.mega[b]=EmcFire(EmcRound::szMega,v,from,end,a.megaDamage);
            RoundSize(a.mega[b],kMegaSize*(0.5f+0.5f*a.charge));
        }
        a.megaLeft=kMegaSec;a.cannonCool=kCannonCool;
        Sfx(m,SzSfx::cannonShot,from);
        Log("SAZABI v=%p mega particle cannon: charge %.0f%%, %.0f a round, %d beams",v,a.charge*100.0f,a.megaDamage,kMegaBeams);
    }
    a.charge=0.0f;
    a.brace=std::fmax(0.0f,a.brace-3.0f*dt);
}

// The funnels' flight: to their station by a spring (round their enemy, or over the mech with none), their nose on
// their enemy; a beam when near their station and loaded; home to their packs when their time is up.
void FlyFunnels(Mech& m,unsigned char* v,bool launch,float dt) noexcept {
    Arms& a=m.arms;
    int out=0;
    for(const auto& f:a.funnels)out+=f.phase!=FunnelPhase::docked;
    if(!out)a.funnelCool=std::fmax(0.0f,a.funnelCool-dt);
    if(launch && !out && a.funnelCool<=0.0f && EmcRoundReady(EmcRound::szFunnel)) {
        for(int k=0;k<kFunnelCount;++k) {
            Funnel& f=a.funnels[k];
            f=Funnel{};
            f.phase=FunnelPhase::launching;f.wait=kFunnelLaunchGap*static_cast<float>(k);
            DockPoint(m,k,f.at);
            f.shotIn=kFunnelShotSec*0.5f+0.17f*static_cast<float>(k);
        }
        a.funnelLeft=kFunnelSec;
        Log("SAZABI v=%p funnels launched",v);
        return;
    }
    if(!out)return;
    a.funnelLeft-=dt;
    const float* r=m.root+12;
    Enemies e{};
    SeeEnemies(v,r,&e);
    const float spin=m.pose.t*25.0f*sazabi::kDeg;
    int docked=0;
    for(int k=0;k<kFunnelCount;++k) {
        Funnel& f=a.funnels[k];
        float dock[3];
        DockPoint(m,k,dock);
        if(f.phase==FunnelPhase::docked){++docked;continue;}
        if(f.phase==FunnelPhase::launching) {
            std::memcpy(f.at,dock,12);
            if((f.wait-=dt)>0.0f)continue;
            f.phase=FunnelPhase::out;
            float up[3]={0.0f,1.0f,0.0f},side[3];
            const float sx=k<3 ? 1.0f : -1.0f;
            const float local[3]={sx,0.6f,-0.4f};
            DirToWorld(m,local,side);
            for(int i=0;i<3;++i)f.vel[i]=side[i]*30.0f+up[i]*15.0f;
            Sfx(m,SzSfx::funnelLaunch,f.at);
        }
        if(f.phase==FunnelPhase::out && a.funnelLeft<=0.0f){f.phase=FunnelPhase::back;f.backFor=0.0f;f.target=nullptr;}
        float station[3];
        bool aiming=false;
        if(f.phase==FunnelPhase::back) {
            std::memcpy(station,dock,12);
            f.backFor+=dt;
            const float d[3]={dock[0]-f.at[0],dock[1]-f.at[1],dock[2]-f.at[2]};
            if(Len(d)<kFunnelDock || f.backFor>kFunnelBackMost) {
                f=Funnel{};
                Sfx(m,SzSfx::funnelDock,dock);
                ++docked;
                continue;
            }
        } else {
            int pick=-1;
            for(int i=0;i<e.n;++i)if(e.obj[i]==f.target){pick=i;break;}
            if(pick<0 && e.n>0){pick=k%e.n;f.target=e.obj[pick];}
            const float ang=spin+static_cast<float>(k)*sazabi::kPi/3.0f;
            if(pick>=0) {
                std::memcpy(f.aim,e.at[pick],12);
                station[0]=f.aim[0]+kFunnelOrbit*std::cos(ang);station[1]=f.aim[1]+kFunnelOver;
                station[2]=f.aim[2]+kFunnelOrbit*std::sin(ang);
                aiming=true;
            } else {
                f.target=nullptr;
                station[0]=r[0]+kFunnelHome*std::cos(ang);station[1]=r[1]+kFunnelHomeHigh;station[2]=r[2]+kFunnelHome*std::sin(ang);
            }
        }
        const float spring=f.phase==FunnelPhase::back ? 2.0f*kFunnelSpring : kFunnelSpring;
        for(int i=0;i<3;++i)f.vel[i]+=(spring*(station[i]-f.at[i])-kFunnelDamp*f.vel[i])*dt;
        const float sp=Len(f.vel);
        if(sp>kFunnelSpeed)for(float& c:f.vel)c*=kFunnelSpeed/sp;
        for(int i=0;i<3;++i)f.at[i]+=f.vel[i]*dt;
        float look[3];
        if(aiming)for(int i=0;i<3;++i)look[i]=f.aim[i]-f.at[i];
        else std::memcpy(look,f.vel,12);
        const float l=Len(look);
        if(l>1e-3f)for(int i=0;i<3;++i)f.dir[i]=look[i]/l;
        f.shotIn-=dt;
        const float off[3]={station[0]-f.at[0],station[1]-f.at[1],station[2]-f.at[2]};
        if(aiming && f.shotIn<=0.0f && Len(off)<kFunnelNear) {
            f.shotIn=kFunnelShotSec+0.17f*static_cast<float>(k);
            if(EmcFire(EmcRound::szFunnel,v,f.at,f.aim,Cfg().sazabiFunnelDamage).obj)Sfx(m,SzSfx::funnelShot,f.at);
        }
    }
    if(docked==kFunnelCount){a.funnelCool=kFunnelCool;Log("SAZABI v=%p funnels home",v);}
}

// The flames: the thrusters' while they push (`thrust` 0..1), each flying funnel's behind it.
void Flames(Mech& m,const unsigned char* v,float thrust) noexcept {
    float at[8][3],vel[8][3];
    int n=0;
    if(thrust>0.05f)
        for(int i=0;i<2;++i) {
            float dir[3],local[3];
            BoneAt(m,sazabi::kBackpack,kNozzleAt[i],at[n]);
            sazabi::Apply(kNozzleDir[i],scratch.modelRot[sazabi::kBackpack],local);
            DirToWorld(m,local,dir);
            for(int k=0;k<3;++k)vel[n][k]=-dir[k];   // the flame trails against `vel`: out of the throat
            ++n;
        }
    for(const auto& f:m.arms.funnels) {
        if(f.phase==FunnelPhase::docked || f.phase==FunnelPhase::launching || n>=8)continue;
        for(int k=0;k<3;++k){at[n][k]=f.at[k]-f.dir[k]*1.0f;vel[n][k]=f.vel[k];}
        if(Len(f.vel)<1.0f)for(int k=0;k<3;++k)vel[n][k]=f.dir[k];
        ++n;
    }
    if(n)FlareFlames(v,at,vel,n,GameMs());
    if(m.driven && thrust>0.05f) {
        float mid[3];
        for(int k=0;k<3;++k)mid[k]=0.5f*(at[0][k]+at[1][k]);
        SazabiLoop(SzLoop::thrusters,mid,m.vel,thrust);
    }
}

// The pose's share: the swing, the guard, the brace, the funnels where they fly (in sz_root's frame).
void ArmsPose(Mech& m) noexcept {
    sazabi::PoseInput& p=m.pose;
    const Arms& a=m.arms;
    p.swing=a.swing;p.guard=a.guard;p.cannon=a.brace;
    p.crouch=std::fmax(p.crouch,0.3f*a.brace);
    for(int k=0;k<kFunnelCount;++k) {
        const Funnel& f=a.funnels[k];
        p.funnelOut[k]=m.rootOk && f.phase!=FunnelPhase::docked && f.phase!=FunnelPhase::launching;
        if(!p.funnelOut[k])continue;
        ToRoot(m,f.at,p.funnelAt[k]);
        DirToRoot(m,f.dir,p.funnelDir[k]);
    }
}

// Before the pose: the buttons, the timers, the swing, the guard, the funnels' flight.
void ArmsStep(Mech& m,unsigned char* v,const ArmsInput& in,float dt) noexcept {
    Arms& a=m.arms;
    if(in.cycle && !a.switchHeld) {
        a.special=static_cast<Special>((static_cast<int>(a.special)+1)%static_cast<int>(Special::count));
        static const char* const kNames[]={"shield missiles","funnels","mega particle cannon"};
        Log("SAZABI v=%p special: %s",v,kNames[static_cast<int>(a.special)]);
    }
    a.switchHeld=in.cycle;
    a.guard+=((in.guard ? 1.0f : 0.0f)-a.guard)*std::fmin(1.0f,(in.guard ? 6.0f : 4.0f)*dt);
    if(m.rootOk)Tomahawk(m,v,in,dt);
    const bool launch=in.secondary && !a.secondaryHeld && a.special==Special::funnels;
    if(m.rootOk)FlyFunnels(m,v,launch,dt);
    ArmsPose(m);
}

// After the pose (its bones where they are this frame): the guns, the cannon, the flames, the HUD's cue.
void ArmsFire(Mech& m,unsigned char* v,const ArmsInput& in,float thrust,float dt) noexcept {
    Arms& a=m.arms;
    unsigned char* const rifle=HolderWeapon(v,kHolderRifle);
    unsigned char* const missile=HolderWeapon(v,kHolderMissile);
    // each round's sound as it leaves (the trigger is only a request: the weapon keeps its own interval)
    if(Watch(rifle,&a.rifleAmmo,&a.rifleSince,dt) && m.rootOk) {
        float at[3];
        BoneAt(m,sazabi::kMuzzle,nullptr,at);
        Sfx(m,SzSfx::beamShot,at);
    }
    if(Watch(missile,&a.missileAmmo,&a.missileSince,dt) && m.rootOk) {
        float at[3];
        BoneAt(m,sazabi::kMissile,nullptr,at);
        Sfx(m,SzSfx::missileLaunch,at);
    }
    if(in.primary && a.swing<0.0f)Trigger(rifle);
    if(in.secondary && a.special==Special::missiles && a.swing<0.0f)Trigger(missile);
    if(m.rootOk) {
        const bool cannon=a.special==Special::cannon;
        Cannon(m,v,cannon && in.secondary,cannon && a.secondaryHeld && !in.secondary,dt);
        Flames(m,v,thrust);
    }
    a.secondaryHeld=in.secondary;
    // the reticle: along the rifle from its muzzle to what it meets
    a.hasAim=false;
    if(m.rootOk) {
        float from[3],dir[3],end[3];
        BoneAt(m,sazabi::kMuzzle,nullptr,from);
        DirToWorld(m,&scratch.modelRot[sazabi::kRifle].m[6],dir);
        BeamEnd(from,dir,kAimRange,end);
        std::memcpy(a.aim,end,12);
        a.hasAim=a.swing<0.0f;
    }
    float point[3],progress=0.0f;
    const int lock=missile ? WeaponLock(missile,point,&progress) : 0;
    a.hasLock=lock==2;
    a.lockProgress=lock==2 ? 1.0f : lock==1 ? progress : 0.0f;
    if(lock)std::memcpy(a.lock,point,12);
    a.missiles=missile ? At<std::int32_t>(missile,kWeaponAmmo) : 0;
}

// A footstep each time a foot comes down on its feet (its plan's stance begins), and the cue.
void Footsteps(Mech& m) noexcept {
    for(int side=0;side<2;++side) {
        const sazabi::FootPlan f=sazabi::Plan(m.pose.gait,m.pose.stride,side);
        const bool down=f.stance && !m.air && scratch.gaitW>0.3f;
        if(down && !m.arms.stance[side] && m.rootOk) {
            float at[3];
            BoneAt(m,side==0 ? sazabi::kFootL : sazabi::kFootR,nullptr,at);
            Sfx(m,SzSfx::footstep,at);
        }
        m.arms.stance[side]=f.stance || m.air;
    }
}

SazabiCue cue{};
ULONGLONG cueMs=0;
void PublishCue(const Mech& m) noexcept {
    const Arms& a=m.arms;
    SazabiCue c{};
    c.thruster=m.thruster;
    c.rifleReady=Clamp(a.rifleSince/kRifleInterval,0.0f,1.0f);
    c.missiles=a.missiles;
    c.missileReady=Clamp(a.missileSince/kMissileInterval,0.0f,1.0f);
    c.missileLock=a.lockProgress;
    c.cannonCharge=a.charge;
    c.cannonReady=1.0f-a.cannonCool/kCannonCool;
    int out=0;
    for(const auto& f:a.funnels)out+=f.phase!=FunnelPhase::docked;
    c.funnelsOut=out;
    c.funnelReady=out ? 0.0f : 1.0f-a.funnelCool/kFunnelCool;
    c.guard=a.guard>0.5f;c.swinging=a.swing>=0.0f;c.air=m.air;
    c.hasAim=a.hasAim;std::memcpy(c.aim,a.aim,12);
    c.hasLock=a.hasLock;std::memcpy(c.lock,a.lock,12);
    cue=c;cueMs=GameMs();
}
