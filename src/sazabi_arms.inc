// The Sazabi's arms (sazabi.cpp includes this inside its anonymous namespace; docs/gundam-plan.md 武器). The player's
// two fire buttons are taken from the 506 (its fire bytes 0x2020 / 0x2021 cleared each frame: stock, the primary fires
// holders 0 and 1 together, the beam rifle and the shield missiles) and routed here:
//  - primary: the beam rifle (holder 0 on sz_muzzle, pylib/vcobjects.py SAZABI_WEAPONS: its trigger latch held);
//  - secondary: the special picked (SazabiSwitchKey / Button cycles them): the shield missiles (holder 1, the game's
//    own lock, lockon.h), the funnels (all six launched one after another, each curving round to its own side of an
//    enemy and firing three beams there, then home: sazabi_funnels.h), or the belly's diffuse mega particle cannon (held: it charges, the
//    glow at its muzzle growing; let go: a fan of kMegaBeams beams for kMegaSec, steered with the chest);
//  - SazabiMeleeKey / Button: the beam tomahawk drawn and swung (its strike a drill charge at each enemy in reach and
//    one ahead for the buildings); pressed again mid-swing, another follows;
//  - SazabiGuardKey / Button held: the shield up, the damage taken from ahead SazabiGuardShare of itself.
// Beams are the plugin's rounds (crew.h EmcRound sz*: DemoIndirectFire objects of the Sazabi's team, jet_bay.cpp).
// Funnels fly in the world (sazabi_funnels.h, after the games' and the film's) and are drawn there by their bones
// (sazabi_pose.h Funnels).
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
// One-shots wherever it is (the player's or an NPC's); its loops are the player's alone (sazabi_sound.h: one of each).
void Sfx(const Mech& m,SzSfx which,const float* at) noexcept { if(m.driven)SazabiSfx(which,at); }

void DropArms(Mech& m) noexcept {
    Arms& a=m.arms;
    RoundDrop(a.glow);
    for(auto& r:a.mega)RoundDrop(r);
    for(auto& f:a.funnels)f=Funnel{};
    a.swing=-1.0f;a.charge=0.0f;a.megaLeft=0.0f;a.queued=false;a.funnelsOut=false;
    for(auto& slot:a.funnelSlot)slot=nullptr;
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
    if(m.driven && !m.npc)SazabiLoop(SzLoop::saberHum,blade,m.fl.vel,1.0f);
    if(!a.whooshed && a.swing>=sazabi::kWindUp){a.whooshed=true;Sfx(m,SzSfx::whoosh,blade);}
    if(!a.struck && a.swing>=0.5f*(sazabi::kWindUp+sazabi::kStrike)){a.struck=true;Strike(m,v,blade);}
    if(a.swing<1.0f)return;
    if(a.queued){a.queued=false;a.swing=0.0f;a.struck=false;a.whooshed=false;return;}
    a.swing=-1.0f;
    Sfx(m,SzSfx::saberOff,blade);
}

// The cannon's beam from `from` (its muzzle on the chest): at the aim point (sazabi_camera.inc Aim), turned `yaw` deg
// aside about the vertical (the fan); world, unit.
void CannonDir(const Mech& m,const float* from,float yaw,float* out) noexcept {
    float d[3]={m.arms.aim[0]-from[0],m.arms.aim[1]-from[1],m.arms.aim[2]-from[2]};
    float l=Len(d);
    if(l<1.0f){d[0]=std::sin(m.heading);d[1]=0.0f;d[2]=std::cos(m.heading);l=1.0f;}
    const float y=yaw*sazabi::kDeg,c=std::cos(y),s=std::sin(y);
    out[0]=(d[0]*c+d[2]*s)/l;out[1]=d[1]/l;out[2]=(-d[0]*s+d[2]*c)/l;
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
            CannonDir(m,from,a.megaYaw[b],dir);
            BeamEnd(from,dir,kMegaRange,end);
            if(a.megaLeft<=0.0f || !RoundSteer(a.mega[b],from,end))RoundDrop(a.mega[b]);
        }
        a.brace=1.0f;
        return;
    }
    if(held && a.swing<0.0f && a.cannonCool<=0.0f && EmcRoundReady(EmcRound::szMega)) {   // charging
        a.charge=std::fmin(1.0f,a.charge+dt/kChargeSec);
        float fwd[3],tip[3];
        CannonDir(m,from,0.0f,fwd);
        for(int k=0;k<3;++k)tip[k]=from[k]+fwd[k]*0.3f;
        if(!a.glow.obj && EmcRoundReady(EmcRound::szCharge))a.glow=EmcFire(EmcRound::szCharge,v,from,tip,0.0f);
        RoundSteer(a.glow,from,tip);
        RoundSize(a.glow,kGlowThin+(kGlowThick-kGlowThin)*a.charge);
        if(m.driven && !m.npc)SazabiLoop(SzLoop::cannonCharge,from,m.fl.vel,a.charge);
        a.brace=a.charge;
        return;
    }
    RoundDrop(a.glow);
    if(let && a.charge>=kChargeLeast) {   // the fan of beams
        a.megaDamage=Cfg().sazabiCannonDamage*a.charge;
        for(int b=0;b<kMegaBeams;++b) {
            float dir[3],end[3];
            a.megaYaw[b]=kMegaFan[b];
            CannonDir(m,from,a.megaYaw[b],dir);
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

// Whether every funnel is in its pack and charged (a launch sends them all), and how far the slowest is (0..1, the HUD).
bool FunnelsReady(const Arms& a) noexcept {
    for(const auto& f:a.funnels)if(f.phase!=FunnelPhase::docked || f.recharge>0.0f)return false;
    return true;
}
float FunnelReadiness(const Arms& a) noexcept {
    float most=0.0f;
    for(const auto& f:a.funnels) {
        if(f.phase!=FunnelPhase::docked)return 0.0f;
        most=std::fmax(most,f.recharge);
    }
    return 1.0f-most/sazabi::funnels::kRecharge;
}

// The funnels' flight (sazabi_funnels.h: the games' and the film's), its targets, its beams and sounds. The targets: the
// enemies within kFunnelRange of the mech, in slots an enemy keeps while it lives (an empty slot takes the nearest not in
// one; the slots kept in order, those after an empty one moving up).
void FlyFunnels(Mech& m,unsigned char* v,bool launch,float dt) noexcept {
    namespace fn=sazabi::funnels;
    Arms& a=m.arms;
    const float* r=m.root+12;
    const float body[3]={r[0],r[1]+13.0f,r[2]};
    Enemies e{};
    SeeEnemies(v,body,&e);
    bool kept[kMostTargets]{};
    int n=0;
    fn::Target ts[kFunnelSlots];
    const void* slots[kFunnelSlots]{};
    for(int s=0;s<kFunnelSlots;++s) {   // those still there, in their order
        for(int i=0;i<e.n;++i)
            if(a.funnelSlot[s] && e.obj[i]==a.funnelSlot[s] && !kept[i]){kept[i]=true;slots[n]=e.obj[i];std::memcpy(ts[n].at,e.at[i],12);++n;break;}
    }
    for(int i=0;i<e.n && n<kFunnelSlots;++i)   // the nearest others into the slots left
        if(!kept[i]){kept[i]=true;slots[n]=e.obj[i];std::memcpy(ts[n].at,e.at[i],12);++n;}
    for(int s=0;s<kFunnelSlots;++s)a.funnelSlot[s]=s<n ? slots[s] : nullptr;
    float docks[kFunnelCount][3],fwd[3];
    for(int k=0;k<kFunnelCount;++k)DockPoint(m,k,docks[k]);
    Forward(m,fwd);
    const bool go=launch && FunnelsReady(a) && EmcRoundReady(EmcRound::szFunnel);
    const fn::Events ev=fn::Step(a.funnels,docks,body,fwd,ts,n,go,dt);
    if(go)Log("SAZABI v=%p funnels launched at %d enemies",v,n);
    if(ev.launched)Sfx(m,SzSfx::funnelLaunch,docks[0]);
    for(int i=0;i<ev.shots;++i) {
        const fn::Shot& s=ev.shot[i];
        if(EmcFire(EmcRound::szFunnel,v,s.from,s.at,Cfg().sazabiFunnelDamage).obj)Sfx(m,SzSfx::funnelShot,s.from);
    }
    if(ev.docked)Sfx(m,SzSfx::funnelDock,docks[0]);
    bool out=false;
    for(const auto& f:a.funnels)out=out || f.phase!=FunnelPhase::docked;
    if(a.funnelsOut && !out)Log("SAZABI v=%p funnels home",v);
    a.funnelsOut=out;
}

// A nozzle's flame matrix: rows unit, +z `dir` (the way the flame leaves), at `at` (world).
void NozzleMatrix(const float* at,const float* dir,float* out) noexcept {
    const sazabi::M3 r=sazabi::Facing(dir);
    for(int i=0;i<3;++i){for(int k=0;k<3;++k)out[i*4+k]=r.m[i*3+k];out[i*4+3]=0.0f;}
    for(int k=0;k<3;++k)out[12+k]=at[k];
    out[15]=1.0f;
}

// The flames (the user, 2026-10-07: 「飞行时喷气明显不对」, then 「尾焰也没有根据实际喷气孔来」): a Booster flame out of
// each of the model's own nozzles (sazabi_pose.h kNozzles: the legs' big bells, the backpack's, the calves', the rear
// binders', the waist's), carried with its bone as posed, as long and wide as its bell makes it, burning as
// sazabi_flames.h has it (out on its feet, a hover's quarter in the air, a burst at a dash's start, a soft flicker,
// the waist's attitude jets in a fast turn); each flying funnel's small fire behind it.
// Every frame (0 when idle: the flames burn down and their boosters wait for the next burst).
void Flames(Mech& m,const unsigned char* v,float thrust) noexcept {
    alignas(16) float mats[sazabi::kNozzleCount][16];
    float sizes[sazabi::kNozzleCount][2],levels[sazabi::kNozzleCount];
    sazabi::flames::State fs;
    fs.air=m.fl.air;fs.boosting=m.fl.boosting || m.fl.climbing;fs.sinceBurst=m.sinceBurst;fs.yawRate=m.yawRate;fs.t=m.pose.t;
    float main[3]{};
    for(int i=0;i<sazabi::kNozzleCount;++i) {
        const sazabi::Nozzle& z=sazabi::kNozzles[i];
        float at[3],local[3],dir[3];
        BoneAt(m,z.bone,z.at,at);
        sazabi::Apply(z.dir,scratch.modelRot[z.bone],local);
        DirToWorld(m,local,dir);
        NozzleMatrix(at,dir,mats[i]);
        sizes[i][0]=z.flame[0];sizes[i][1]=z.flame[1];
        const int waist=z.bone==sazabi::kPelvis ? (z.at[0]>0.0f ? 1 : -1) : 0;   // the waist's: attitude jets
        levels[i]=sazabi::flames::Level(fs,i,z.burst,waist);
        if(!z.burst)for(int k=0;k<3;++k)main[k]+=at[k]*0.25f;   // the four main ones' middle: the roar's
    }
    NozzleFlames(v,mats,sazabi::kNozzleCount,sizes,levels,GameMs());
    // the funnels
    float at[8][3],vel[8][3];
    int n=0;
    for(const auto& f:m.arms.funnels) {
        if(f.phase==FunnelPhase::docked || f.phase==FunnelPhase::launching || n>=8)continue;
        for(int k=0;k<3;++k){at[n][k]=f.at[k]-f.dir[k]*1.0f;vel[n][k]=f.vel[k];}
        if(Len(f.vel)<1.0f)for(int k=0;k<3;++k)vel[n][k]=f.dir[k];
        ++n;
    }
    FlareFlames(v,at,vel,n,GameMs());
    if(m.driven && !m.npc && thrust>0.05f)SazabiLoop(SzLoop::thrusters,main,m.fl.vel,thrust);
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
    // the reticle's point: Aim (sazabi_camera.inc), before the pose
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
        const bool down=f.stance && !m.fl.air && scratch.gaitW>0.3f;
        if(down && !m.arms.stance[side] && m.rootOk) {
            float at[3];
            BoneAt(m,side==0 ? sazabi::kFootL : sazabi::kFootR,nullptr,at);
            Sfx(m,SzSfx::footstep,at);
        }
        m.arms.stance[side]=f.stance || m.fl.air;
    }
}

SazabiCue cue{};
ULONGLONG cueMs=0;
void PublishCue(const Mech& m) noexcept {
    const Arms& a=m.arms;
    SazabiCue c{};
    c.thruster=m.fl.gauge;
    c.rifleReady=Clamp(a.rifleSince/kRifleInterval,0.0f,1.0f);
    c.missiles=a.missiles;
    c.missileReady=Clamp(a.missileSince/kMissileInterval,0.0f,1.0f);
    c.missileLock=a.lockProgress;
    c.cannonCharge=a.charge;
    c.cannonReady=1.0f-a.cannonCool/kCannonCool;
    int out=0;
    for(const auto& f:a.funnels)out+=f.phase!=FunnelPhase::docked;
    c.funnelsOut=out;
    c.funnelReady=FunnelReadiness(a);
    c.guard=a.guard>0.5f;c.swinging=a.swing>=0.0f;c.air=m.fl.air;
    c.hasAim=a.hasAim;std::memcpy(c.aim,a.aim,12);
    c.hasLock=a.hasLock;std::memcpy(c.lock,a.lock,12);
    c.altitude=m.fl.air && m.feetClear!=kNoGround ? std::fmax(0.0f,m.feetClear) : 0.0f;
    c.speed=std::sqrt(m.fl.vel[0]*m.fl.vel[0]+m.fl.vel[2]*m.fl.vel[2]);
    c.climb=m.fl.vel[1];
    c.boosting=m.fl.boosting || m.fl.climbing;
    c.overheat=m.fl.overheat;
    c.special=static_cast<int>(a.special);
    c.aimRange=a.aimRange;c.aimHit=a.aimHit;c.centred=a.centred;
    cue=c;cueMs=GameMs();
}
