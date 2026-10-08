// Proteus state uses the game's existing object event stream. No remote input/AI/salvo or leg controller runs here.
proteus_net::State Snapshot(const Unit& u,const unsigned char* v,proteus_net::Kind kind) noexcept {
    proteus_net::State s;const auto& c=Cfg();s.kind=kind;
    s.flags=(u.active || (u.net.remote && ControlFresh(u,v,GameMs()) && (u.net.control.flags&proteus_net::kActive)) ? proteus_net::kActive : 0u)|
        (u.st.shieldOn ? proteus_net::kShieldOn : 0u)|(u.st.overheated ? proteus_net::kOverheated : 0u)|
        ((u.net.remote ? (u.net.control.flags&proteus_net::kTwoSeats)!=0 : c.proteusTwoSeats) ? proteus_net::kTwoSeats : 0u);
    s.mode=static_cast<std::uint32_t>(u.st.mode);s.stagger=u.st.t;s.heat=u.st.heat;s.barrier=u.st.barrier;s.quiet=u.st.quiet;s.lift=u.lift;
    std::memcpy(s.nose,u.shieldNose,12);
    if(!vec::Normalize(s.nose)){s.nose[0]=s.nose[1]=0;s.nose[2]=1;}
    const auto k=RulesOf(u);s.deploySec=k.deploySec;s.stowSec=k.stowSec;s.shieldBlock=k.shieldBlock;
    s.shieldArc=k.shieldHalfArc*360.0f/kPi;s.regenSec=k.barrierRegenSec;s.delaySec=k.barrierDelaySec;
    s.barrierShare=u.net.remote && u.net.haveControl ? u.net.control.barrierShare : c.proteusBarrier;
    const auto field=FieldParameters(u);s.fieldRadius=field.radius;s.fieldDefense=field.defense;s.fieldAttack=field.attack;
    s.fieldFireRate=field.fireRate;s.fieldEnergy=field.energy;s.fieldPower=field.power;s.fieldTeam=field.team;
    return s;
}
void SendControl(Unit& u,unsigned char* v,ULONGLONG ms,bool force=false) noexcept {
    if(!u.net.networked || u.net.remote || !IsOnlineAuthority(v))return;
    if(!force && u.net.sentAt && ms-u.net.sentAt<(u.active ? 50u : 500u))return;
    if(ProteusNetSend(v,Snapshot(u,v,proteus_net::Kind::control)))u.net.sentAt=ms;
}
void SendDefense(Unit& u,unsigned char* v,ULONGLONG ms) noexcept {
    if(!u.net.networked || !DefenseOwner(v))return;
    if(!u.net.defenseDirty && u.net.defenseSentAt && ms-u.net.defenseSentAt<250)return;
    if(ProteusNetSend(v,Snapshot(u,v,proteus_net::Kind::defense))) {u.net.defenseSentAt=ms;u.net.defenseDirty=false;}
}
void DefenseTick(Unit& u,unsigned char* v,ULONGLONG ms) noexcept {
    if(!u.net.networked)return;
    const float dt=u.net.defenseLastMs && ms>=u.net.defenseLastMs ? vec::Clamp(static_cast<float>(ms-u.net.defenseLastMs)*.001f,0.0f,.1f) : 0;
    u.net.defenseLastMs=ms;
    if(!DefenseOwner(v))return;
    // The driver publishes mode/heat, never this mutable HP pool. Native HP deltas cannot report a hit entirely
    // caught by a barrier; its registered owner publishes that consumption explicitly, including zero hull damage.
    if(ActiveOf(v)) {
        const auto k=RulesOf(u);const float before=u.st.barrier;
        u.st.quiet=std::fmin(1.0e9f,u.st.quiet+dt);
        if(u.st.mode==proteus::Mode::deployed && u.st.quiet>=k.barrierDelaySec && k.barrierRegenSec>0)
            u.st.barrier=proteus::Clamp01(u.st.barrier+dt/k.barrierRegenSec);
        if(u.st.barrier!=before)u.net.defenseDirty=true;
    }
    SendDefense(u,v,ms);
}
// Save the original values for restoring the replicated seat layout and an actual local gunner's paired cannons.
// Replica frames never run Legs, the native input hook, target selection or custom driver fire.
void TakeReplica(Unit& u,unsigned char* v) noexcept {
    if(u.active)return;
    u.walk=At<float>(v,kWalk);u.walkEase=At<float>(v,kWalkEase);u.turn=At<float>(v,kTurn);
    u.jump=At<float>(v,kJump);u.stepNormal=At<float>(v,kStepNormal);
    for(unsigned i=1;i<kProteusSeats;++i) {
        u.weapon[i]=SeatWeapon(v,i,&u.holder[i]);
        u.rate[i]=u.weapon[i] ? At<float>(u.weapon[i],kRate) : 1;
        u.spread[i]=u.weapon[i] ? At<float>(u.weapon[i],kSpread) : 1;
    }
    u.active=true;
}
void ApplyControl(Unit& u,const proteus_net::State& s) noexcept {
    u.st.mode=static_cast<proteus::Mode>(s.mode);u.st.t=s.stagger;u.st.heat=s.heat;
    u.st.shieldOn=(s.flags&proteus_net::kShieldOn)!=0;u.st.overheated=(s.flags&proteus_net::kOverheated)!=0;
    u.lift=s.lift;std::memcpy(u.shieldNose,s.nose,12);u.salvoLeft=0;u.mark=nullptr;
}
bool NetworkFrame(Unit& u,unsigned char* v,bool live,int playerSeat,ULONGLONG ms) noexcept {
    RefreshFieldWrites();
    const bool networked=RegisteredProteus(v);
    if(u.net.networked!=networked) {
        GiveBack(u,v,"session changed");u.st=proteus::State{};u.net=NetState{};u.net.networked=networked;
        std::memset(u.held,0,sizeof(u.held));u.salvoLeft=0;u.mark=nullptr;
    }
    if(!networked)return false;
    const auto epoch=ProteusNetController(v);
    if(u.net.epoch!=epoch) {
        GiveBack(u,v,"driver changed");const float barrier=u.st.barrier,quiet=u.st.quiet;
        u.st=proteus::State{};u.st.barrier=barrier;u.st.quiet=quiet;
        u.net.epoch=epoch;u.net.haveControl=false;u.net.sentAt=0;u.lift=0;u.salvoLeft=0;u.mark=nullptr;
        std::memset(u.held,0,sizeof(u.held));
    }
    if(IsOnlineAuthority(v)) {
        if(u.net.remote) {GiveBack(u,v,"local control");u.net.remote=false;u.net.sentAt=0;u.st.mode=proteus::Mode::walk;u.st.shieldOn=false;}
        return false;
    }
    // A replica may have been the old local driver. Restore what that driver changed before consuming another pilot.
    if(!u.net.remote) {GiveBack(u,v,"remote control");u.net.remote=true;}
    if(u.frame==GameFrame())return true;
    const float dt=u.lastMs && ms>=u.lastMs ? vec::Clamp(static_cast<float>(ms-u.lastMs)*.001f,0.0f,.1f) : 0;
    u.frame=GameFrame();u.seen=u.lastMs=ms;
    const bool active=live && ControlFresh(u,v,ms) && (u.net.control.flags&proteus_net::kActive);
    if(!active) {
        GiveBack(u,v,"network control expired");u.st.mode=proteus::Mode::walk;u.st.shieldOn=false;u.lift=0;
        if(zone.z.vehicle==v){AcquireSRWLockExclusive(&zoneLock);zone.on=false;ReleaseSRWLockExclusive(&zoneLock);}
    } else {
        TakeReplica(u,v);ApplyControl(u,u.net.control);
        u.playerSeat=playerSeat>=0 ? static_cast<unsigned>(playerSeat) : static_cast<unsigned>(-1);
        const bool localGunner=LocalGunner(v);
        if(u.net.control.flags&proteus_net::kTwoSeats)TwoSeats(u,v,localGunner);
        else if(u.closed) {
            for(unsigned s=kRightSeat;s<kProteusSeats && s<SeatCount(v);++s)Put<std::int32_t>(SeatAt(v,s),kSeatClassMask,u.seatMask[s]);
            u.closed=false;
        }
        if(localGunner)Guns(u,false,Cfg());
        if(playerSeat==static_cast<int>(kGunnerSeat)){Ring(u,v,ms,Cfg());Publish(u,v,false,SeatAt(v,0),Cfg());}
        FieldFrame(u,v,dt,Cfg()); // only target-owner support effects; no remote pilot/fire simulation
    }
    DefenseTick(u,v,ms);
    return true;
}
void ReceiveNetwork(Unit& u,unsigned char* v,const proteus_net::State& s,ULONGLONG ms) noexcept {
    u.net.networked=true;u.seen=ms;
    if(s.kind==proteus_net::Kind::defense) {
        // This is never treated as pilot input, so the driver can receive its host's barrier accounting.
        u.st.barrier=s.barrier;u.st.quiet=s.quiet;return;
    }
    if(!u.net.remote || u.net.epoch!=s.controller)GiveBack(u,v,"replicated driver state");
    u.net.remote=true;u.net.haveControl=true;u.net.epoch=s.controller;u.net.receivedAt=ms;u.net.control=s;
    ApplyControl(u,s);
    RefreshFieldWrites();
    if(s.flags&proteus_net::kActive)FieldFrame(u,v,0,Cfg());
    // Incoming pilot packets intentionally do NOT overwrite barrier/quiet: only a defense packet or its owner does.
    DefenseTick(u,v,ms);
}
