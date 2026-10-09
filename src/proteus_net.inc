// Proteus state uses the game's existing object event stream (proteus_net.h). Two senders: the control authority (the
// current registered driver's machine; empty / NPC: the host) publishes stance, shield switch, heat, facing, two-seat
// layout and the field; the registered owner publishes the shield's HP (its barrier settles the hits there). Remote
// copies run no keys, legs, stance rules or regeneration: they show the piles, raise their own local barrier, keep the
// seat layout, let a real local gunner work the borrowed mounts and apply the field on what they own.
proteus_net::State Snapshot(const Unit& u,const unsigned char* v,proteus_net::Kind kind) noexcept {
    proteus_net::State s;const auto& c=Cfg();s.kind=kind;
    s.flags=(u.active || (u.net.remote && ControlFresh(u,v,GameMs()) && (u.net.control.flags&proteus_net::kActive)) ? proteus_net::kActive : 0u)|
        (u.st.shieldOn ? proteus_net::kShieldOn : 0u)|(u.st.overheated ? proteus_net::kOverheated : 0u)|(u.st.broken ? proteus_net::kBroken : 0u)|
        ((u.net.remote ? (u.net.control.flags&proteus_net::kTwoSeats)!=0 : c.proteusTwoSeats) ? proteus_net::kTwoSeats : 0u);
    s.mode=static_cast<std::uint32_t>(u.st.mode);s.stagger=u.st.t;s.heat=u.st.heat;s.barrier=u.st.shield;s.quiet=u.st.quiet;s.lift=u.lift;
    std::memcpy(s.nose,u.shieldNose,12);
    if(!vec::Normalize(s.nose)){s.nose[0]=s.nose[1]=0;s.nose[2]=1;}
    const auto k=RulesOf(u);s.deploySec=k.deploySec;s.stowSec=k.stowSec;s.shieldBlock=1.0f;
    s.shieldArc=kBarrierArcDeg;s.regenSec=k.shieldRegenSec;s.delaySec=k.shieldDelaySec;
    s.barrierShare=BarrierShare(u);
    const auto field=FieldParameters(u);s.fieldRadius=field.radius;s.fieldDefense=field.defense;s.fieldAttack=field.attack;
    s.fieldFireRate=field.fireRate;s.fieldEnergy=field.energy;s.fieldPower=field.power;s.fieldTeam=field.team;
    return s;
}
void SendControl(Unit& u,unsigned char* v,ULONGLONG ms,bool force=false) noexcept {
    if(!u.net.networked || u.net.remote || !IsOnlineAuthority(v))return;
    if(!force && u.net.sentAt && ms-u.net.sentAt<(u.active ? 50u : 500u))return;
    if(ProteusNetSend(v,Snapshot(u,v,proteus_net::Kind::control)))u.net.sentAt=ms;
}
// The registered owner's count of the shield's HP: at once on a change (BarrierStep sets defenseDirty), else every 250 ms.
void SendDefense(Unit& u,unsigned char* v,ULONGLONG ms) noexcept {
    if(!u.net.networked || !DefenseOwner(v))return;
    if(!u.net.defenseDirty && u.net.defenseSentAt && ms-u.net.defenseSentAt<250)return;
    if(ProteusNetSend(v,Snapshot(u,v,proteus_net::Kind::defense))) {u.net.defenseSentAt=ms;u.net.defenseDirty=false;}
}
// Save the original values for restoring the replicated seat layout and an actual local gunner's cannons.
void TakeReplica(Unit& u,unsigned char* v) noexcept {
    if(u.active)return;
    u.walk=At<float>(v,kWalk);u.walkEase=At<float>(v,kWalkEase);u.turn=At<float>(v,kTurn);
    u.jump=At<float>(v,kJump);u.stepNormal=At<float>(v,kStepNormal);
    for(auto& a:u.arm)a=Arm{};
    RefreshWeapons(u,v);
    u.active=true;u.barrier.failed=false;
}
// The driver's state; the shield's HP is the owner's (a defense packet), never the driver's.
void ApplyControl(Unit& u,const proteus_net::State& s) noexcept {
    u.st.mode=static_cast<proteus::Mode>(s.mode);u.st.t=s.stagger;u.st.heat=s.heat;
    u.st.shieldOn=(s.flags&proteus_net::kShieldOn)!=0;u.st.overheated=(s.flags&proteus_net::kOverheated)!=0;
    u.lift=s.lift;std::memcpy(u.shieldNose,s.nose,12);
}
void ApplyDefense(Unit& u,const proteus_net::State& s) noexcept {
    u.st.shield=s.barrier;u.st.quiet=s.quiet;u.st.broken=(s.flags&proteus_net::kBroken)!=0;
}
bool NetworkFrame(Unit& u,unsigned char* v,bool live,int playerSeat,ULONGLONG ms) noexcept {
    RefreshFieldWrites();
    const bool networked=RegisteredProteus(v);
    if(u.net.networked!=networked) {
        GiveBack(u,v,"session changed");u.st=proteus::State{};u.net=NetState{};u.net.networked=networked;
        std::memset(u.held,0,sizeof(u.held));
    }
    if(!networked)return false;
    const auto epoch=ProteusNetController(v);
    if(u.net.epoch!=epoch) {
        GiveBack(u,v,"driver changed");
        u.net.epoch=epoch;u.net.haveControl=false;u.net.sentAt=0;u.lift=0;
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
        DropZone(v);
    } else {
        TakeReplica(u,v);RefreshWeapons(u,v);ApplyControl(u,u.net.control);
        u.playerSeat=playerSeat>=0 ? static_cast<unsigned>(playerSeat) : static_cast<unsigned>(-1);
        const bool localGunner=LocalGunner(v);
        Seats(u,v,(u.net.control.flags&proteus_net::kTwoSeats)!=0,localGunner || DefenseOwner(v));
        if(localGunner)Guns(u,Cfg());
        if(DefenseOwner(v)) {   // the shield's HP is counted here: it refills here
            const bool broken=u.st.broken;
            proteus::Refill(u.st,dt,RulesOf(u),proteus::ShieldUp(u.st));
            if(u.st.broken!=broken)u.net.defenseDirty=true;   // a gradual refill goes out with the periodic count
        }
        BarrierFrame(u,v);
        if(ObserverSeat(v)>=0){Ring(u,v,ms);Publish(u,v,Cfg());}
        FieldFrame(u,v,dt); // only target-owner support effects; no remote pilot/fire simulation
    }
    SendDefense(u,v,ms);
    return true;
}
void ReceiveNetwork(Unit& u,unsigned char* v,const proteus_net::State& s,ULONGLONG ms) noexcept {
    u.net.networked=true;u.seen=ms;
    if(s.kind==proteus_net::Kind::defense){ApplyDefense(u,s);u.net.defenseAt=ms;return;}   // never pilot input: a driver takes its owner's count
    if(!u.net.remote || u.net.epoch!=s.controller)GiveBack(u,v,"replicated driver state");
    u.net.remote=true;u.net.haveControl=true;u.net.epoch=s.controller;u.net.receivedAt=ms;u.net.control=s;
    ApplyControl(u,s);
    RefreshFieldWrites();
    if(s.flags&proteus_net::kActive)FieldFrame(u,v,0);
}
