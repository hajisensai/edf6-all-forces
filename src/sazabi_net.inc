// Included inside sazabi.cpp after the pose/arms functions. Remote replay never enters their decision/fire steps.
constexpr ULONGLONG kSazabiNetFresh=1500,kSazabiEventFresh=500;
std::uint32_t EventAge(ULONGLONG now,ULONGLONG at) noexcept {
    return static_cast<std::uint32_t>(now>=at && now-at<60000 ? now-at : 60000);
}
bool RemoteFresh(const Mech& m,unsigned char* v,ULONGLONG now) noexcept {
    return m.net.remote && m.net.source && now>=m.net.receivedAt && now-m.net.receivedAt<=kSazabiNetFresh &&
        drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)) && !IsOnlineAuthority(v) &&
        m.net.state.controller==SazabiNetController(v);
}
void PackPose(const sazabi::PoseInput& p,sazabi_net::Pose& q) noexcept {
    q.t=p.t;q.gait=p.gait;q.stride=p.stride;q.air=p.air;q.lean=p.lean;q.bank=p.bank;q.crouch=p.crouch;
    q.aimPitch=p.aimPitch;q.aimYaw=p.aimYaw;q.aim=p.aim;q.guard=p.guard;q.swing=p.swing;q.combo=p.combo;
    q.boost=p.boost;q.recoil=p.recoil;q.cannon=p.cannon;
}
void UnpackPose(const sazabi_net::Pose& p,sazabi::PoseInput& q) noexcept {
    q.t=p.t;q.gait=p.gait;q.stride=p.stride;q.air=p.air;q.lean=p.lean;q.bank=p.bank;q.crouch=p.crouch;
    q.aimPitch=p.aimPitch;q.aimYaw=p.aimYaw;q.aim=p.aim;q.guard=p.guard;q.swing=p.swing;q.combo=p.combo;
    q.boost=p.boost;q.recoil=p.recoil;q.cannon=p.cannon;
}
void SendNetwork(Mech& m,unsigned char* v,ULONGLONG ms) noexcept {
    if(!m.net.networked || m.net.remote)return;
    auto& s=m.net.state;
    const std::uint32_t flags=(m.driven ? sazabi_net::kDriven : 0u)|(m.npc ? sazabi_net::kNpc : 0u)|
        (m.fl.air ? sazabi_net::kAir : 0u)|(m.fl.boosting ? sazabi_net::kBoost : 0u)|
        (m.fl.climbing ? sazabi_net::kClimb : 0u)|(m.fl.overheat ? sazabi_net::kOverheat : 0u);
    const bool changed=flags!=s.flags || (m.arms.guard>=0.5f)!=(s.pose.guard>=0.5f) ||
        (m.arms.swing>=0)!=(s.pose.swing>=0) || (m.arms.charge>0)!=(s.arms.charge>0) ||
        (m.arms.megaLeft>0)!=(s.arms.megaLeft>0) || s.special!=static_cast<std::uint32_t>(m.arms.special);
    const ULONGLONG period=m.driven ? 50 : 500;
    if(m.net.sentAt && !changed && !m.net.dirty && ms-m.net.sentAt<period)return;
    s.flags=flags;s.special=static_cast<std::uint32_t>(m.arms.special);PackPose(m.pose,s.pose);
    // The shield's damage state is current even if the cannon changed the visual pose after Pose this frame.
    s.pose.guard=m.arms.guard;
    s.motion.heading=m.heading;s.motion.aimPitch=m.aimPitch;s.motion.yawRate=m.yawRate;s.motion.sinceBurst=m.sinceBurst;
    s.motion.gauge=m.fl.gauge;std::memcpy(s.motion.vel,m.fl.vel,12);
    s.arms.charge=m.arms.charge;s.arms.cannonCool=m.arms.cannonCool;s.arms.megaLeft=std::fmax(0.0f,m.arms.megaLeft);
    s.arms.guardShare=Cfg().sazabiGuardShare;std::memcpy(s.arms.aim,m.arms.aim,12);
    for(int i=0;i<kFunnelCount;++i) {
        const auto& f=m.arms.funnels[i];auto& out=s.funnels[i];
        out.phase=static_cast<std::uint32_t>(f.phase);out.order=f.order;out.shots=f.shots;
        out.recharge=f.recharge;out.sortie=f.sortie;
        std::memcpy(out.at,f.at,12);std::memcpy(out.dir,f.dir,12);std::memcpy(out.vel,f.vel,12);
        out.target=f.target>=0 && f.target<kFunnelSlots ? SazabiNetReference(m.arms.funnelSlot[f.target]) : -1;
        s.shots[i].age=EventAge(ms,m.net.shotAt[i]);
    }
    for(int i=0;i<4;++i)s.sounds[i].age=EventAge(ms,m.net.soundAt[i]);
    if(SazabiNetSend(v,s)){m.net.sentAt=ms;m.net.dirty=false;}
}
void DropRemoteEffects(Mech& m) noexcept {
    RoundDrop(m.arms.glow);for(auto& beam:m.arms.mega)RoundDrop(beam);
    if(m.rootOk)Flames(m,m.vehicle,0.0f,true); // NozzleFlames requires real arrays even to write zero levels
    FlareFlames(m.vehicle,nullptr,nullptr,0,GameMs());
}
void AcceptNetwork(Mech& m,unsigned char* v,const sazabi_net::State& s,ULONGLONG ms) noexcept {
    if(!m.net.remote) {
        DropLocalAim(m.arms); // Local target pointers and key latches never survive a driver authority change.
        DropRemoteEffects(m);m.active=false;
        if(m.insetSaved)Put<float>(v,kAreaInset,m.savedInset);
        m.insetSaved=false;
        if(view.vehicle==v)DropView();
    }
    if(m.net.source!=s.sender) {
        std::memset(m.net.shotSeen,0,sizeof(m.net.shotSeen));std::memset(m.net.soundSeen,0,sizeof(m.net.soundSeen));
        m.net.source=s.sender;
    }
    m.net.networked=m.net.remote=true;m.net.receivedAt=ms;m.net.state=s;
    m.driven=(s.flags&sazabi_net::kDriven)!=0;m.npc=(s.flags&sazabi_net::kNpc)!=0;
    m.active=false;m.heading=s.motion.heading;m.aimPitch=s.motion.aimPitch;m.yawRate=s.motion.yawRate;
    m.sinceBurst=s.motion.sinceBurst;m.fl.gauge=s.motion.gauge;std::memcpy(m.fl.vel,s.motion.vel,12);
    m.fl.air=(s.flags&sazabi_net::kAir)!=0;m.fl.boosting=(s.flags&sazabi_net::kBoost)!=0;
    m.fl.climbing=(s.flags&sazabi_net::kClimb)!=0;m.fl.overheat=(s.flags&sazabi_net::kOverheat)!=0;
    UnpackPose(s.pose,m.pose);
    auto& a=m.arms;
    a.guard=s.pose.guard;a.swing=s.pose.swing;a.combo=s.pose.combo;a.brace=s.pose.cannon;
    a.special=static_cast<Special>(s.special);a.charge=s.arms.charge;a.cannonCool=s.arms.cannonCool;a.megaLeft=s.arms.megaLeft;
    std::memcpy(a.aim,s.arms.aim,12);a.hasAim=true;
    for(int i=0;i<kFunnelCount;++i) {
        auto& f=a.funnels[i];const auto& in=s.funnels[i];
        f.phase=static_cast<FunnelPhase>(in.phase);f.order=in.order;f.shots=in.shots;f.recharge=in.recharge;f.sortie=in.sortie;
        std::memcpy(f.at,in.at,12);std::memcpy(f.dir,in.dir,12);std::memcpy(f.vel,in.vel,12);
        // A wire identity is NOT an index into this machine's independently ordered enemy list. Replay is entirely
        // world-space; a later local authority will acquire its own targets from these positions.
        f.target=-1;
    }
    for(auto& slot:a.funnelSlot)slot=nullptr;
}
void ReplayNetwork(Mech& m,unsigned char* v,ULONGLONG ms) noexcept {
    if(!RemoteFresh(m,v,ms)) {
        DropRemoteEffects(m);m.driven=false;m.arms.guard=0;m.arms.swing=-1;m.arms.charge=m.arms.megaLeft=0;
        m.pose=sazabi::PoseInput{};m.pose.aim=0;Pose(m,v);return;
    }
    Rig(m,v);RootFrame(m,v);
    const auto& s=m.net.state;
    for(int i=0;i<kFunnelCount;++i) {
        const auto& f=m.arms.funnels[i];
        m.pose.funnelOut[i]=m.rootOk && f.phase!=FunnelPhase::docked && f.phase!=FunnelPhase::launching;
        if(m.pose.funnelOut[i]){ToRoot(m,f.at,m.pose.funnelAt[i]);DirToRoot(m,f.dir,m.pose.funnelDir[i]);}
    }
    Pose(m,v);
    if(m.rootOk)Flames(m,v,0.0f); // no remote loop voice takes the local pilot's single loop channel
    const ULONGLONG elapsed=ms-m.net.receivedAt;
    for(int i=0;i<6;++i) {
        const auto& shot=s.shots[i];
        if(!shot.sequence || shot.sequence==m.net.shotSeen[i])continue;
        m.net.shotSeen[i]=shot.sequence;
        if(shot.age+elapsed<=kSazabiEventFresh)EmcFire(EmcRound::szFunnel,v,shot.from,shot.at,0.0f);
    }
    for(int i=0;i<4;++i) {
        const auto& sound=s.sounds[i];
        if(!sound.sequence || sound.sequence==m.net.soundSeen[i])continue;
        m.net.soundSeen[i]=sound.sequence;
        if(sound.age+elapsed<=kSazabiEventFresh)SazabiSfx(static_cast<SzSfx>(sound.kind),sound.at);
    }
    if(s.arms.charge>0) {
        if(!RoundSteer(m.arms.glow,s.arms.megaFrom,s.arms.megaEnd[0]))
            m.arms.glow=EmcFire(EmcRound::szCharge,v,s.arms.megaFrom,s.arms.megaEnd[0],0.0f);
        RoundSize(m.arms.glow,kGlowThin+(kGlowThick-kGlowThin)*s.arms.charge);
    } else RoundDrop(m.arms.glow);
    for(int i=0;i<kMegaBeams;++i) {
        if(s.arms.megaLeft>0 && elapsed<static_cast<ULONGLONG>(s.arms.megaLeft*1000)) {
            if(!RoundSteer(m.arms.mega[i],s.arms.megaFrom,s.arms.megaEnd[i]))
                m.arms.mega[i]=EmcFire(EmcRound::szMega,v,s.arms.megaFrom,s.arms.megaEnd[i],0.0f);
            RoundSize(m.arms.mega[i],s.arms.megaSize);
        } else RoundDrop(m.arms.mega[i]);
    }
}
bool NetworkFrame(Mech& m,unsigned char* v,ULONGLONG ms) noexcept {
    const bool networked=drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128));
    if(m.net.networked!=networked) {
        DropRemoteEffects(m);m.net=NetState{};m.net.networked=networked;m.driven=m.active=false;
        DropArms(m);
    }
    if(networked && !IsOnlineAuthority(v)) {
        if(!m.net.remote) {
            DropLocalAim(m.arms);
            DropRemoteEffects(m);
            if(m.insetSaved)Put<float>(v,kAreaInset,m.savedInset);
            m.insetSaved=false;
            if(view.vehicle==v)DropView();
        }
        m.net.remote=true;m.active=false;ReplayNetwork(m,v,ms);return true;
    }
    if(m.net.remote) {
        DropRemoteEffects(m);m.net.remote=false;m.net.sentAt=0;m.net.dirty=true;
        // Local control starts from the received pose/velocity. Each undocked funnel re-plans a path from its
        // current world position, never from a foreign candidate-list index or a missing Bezier control point.
        Rig(m,v);RootFrame(m,v);
        for(int i=0;i<6;++i) {
            auto& f=m.arms.funnels[i];f.target=-1;
            if(f.phase==FunnelPhase::docked)continue;
            float dock[3];DockPoint(m,i,dock);f.phase=FunnelPhase::home;
            sazabi::funnels::Course(f,dock,sazabi::funnels::kCruise,0.0f,1.0f,sazabi::funnels::kMinTransit);
        }
        m.arms.charge=m.arms.megaLeft=0;m.arms.swing=-1;m.arms.guard=0;
        m.driven=false; // Board initializes this pilot's input latches and physics ownership
        for(auto& x:m.net.state.shots)x=sazabi_net::Shot{};
        for(auto& x:m.net.state.sounds)x=sazabi_net::Sound{};
    }
    return false;
}
