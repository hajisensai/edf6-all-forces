// Included by playerjet.cpp before Fly, in its file namespace: any other aircraft of the plugin under the player
// (2026-10-05, the user: "every plane we have: reuse what can be reused, write what cannot; each one really good to
// fly"). Which they are and their numbers: playerjet_kinds.h. They go through the player jet's own record (PJet) and
// steps: Fly, Air / Ground, the stores, the cockpit readout, the ejection and the catch; what is theirs alone is here.
//  - Boarding (crew.cpp FindSeatHook / PromptHook, PlayerJetBoardable): the stock board button, the NPC pilot kicked
//    (a jet has one seat), on one of ours that is on the ground or hovers within kBoardClear of it at kBoardSpeed or
//    less. Up there they fly at 60-250 m/s: the hail key (HailTick, ini PlayerJetHailKey) calls the nearest one down
//    to the player: a rotor craft hovers in beside them and sets down (RotorHail), a wing finds a strip near them,
//    flies the approach, lands and rolls out to a stop by them (PlanStrip, Approach, Rollout); then it waits
//    kHailWaitMs for them, its NPC pilot still aboard, and goes back to its call if they do not come.
//  - Flight: a wing flies the player jet's model with its kind's numbers (Air, Ground); a rotor craft flies the NPC's
//    own rotor flight (jet_flight.cpp Hover: its lean, its thrusters) at the velocity the stick asks for (HoverStep).
//  - Weapons: its guns (primary) and stores (secondary, as the player jets'), and its own weapon as one more store in
//    the cockpit's cycle (SpecialStore): a bomber's bay (BOMB BAY: the carpet from the CCIP point on), the gunship's
//    shells and its long-range side cannon (SHELLS / CANNON at where the camera looks; the target key held flies the
//    pylon turn round that point), the carrier's drones (DRONES launched at where the camera looks; the target key
//    calls them back), the blast / doll drone's charge (CHARGE: it goes off and the drone with it, the player thrown
//    clear).
//  - Leaving (Left): on the ground it stays where it is and waits for them (keep); in the air the jet itself is the
//    catch (a wing, Cfg().playerJetCatch: it flies back to the parachuting player), else, and when the catch gives up,
//    its NPC pilot takes it back and it goes on with its call (HandBack, jet.cpp ResumeNpc: its fuel clock stood).
//  - While the plugin holds it for the player (Held) jet.cpp does not fly it, crew.cpp does not crew it, its fuel and
//    withdrawal stand: no deletion, no autopilot of its call under the player.

constexpr float kBoardClear=12.0f,kBoardSpeed=8.0f;   // m over the ground, m/s: the most it may be boarded at
constexpr int kHailNone=0,kHailPlan=1,kHailFerry=2,kHailFinal=3,kHailRoll=4,kHailRotor=5,kHailWait=6;
const char* const kHailNames[]={"none","plan","ferry","final","roll","rotor","wait"};
constexpr ULONGLONG kHailWaitMs=90000,kHailMostMs=240000;
constexpr int kHailTries=3;
constexpr float kHailRange=6000.0f;   // m: the farthest one it calls

// Whether the plugin holds `v` for the player (see the file comment).
bool Held(const unsigned char* v) noexcept {
    if(SeatCount(v)>0 && SeatRider(SeatAt(const_cast<unsigned char*>(v),0))==Rider::player)return true;
    // A newly spawned catch twin has no PJet entry until PlayerJetFrame passes this gate and calls Make.
    // Check its control block too: an address reused after the catch disappeared is not the catch.
    if(catchFlight.v==v && bail.caught.Is(v))return true;
    const PJet* j=Find(v);
    return j && (j->driven || j->autopilot || j->hail.phase!=kHailNone || j->keep);
}
// Its entry, once it is not held: let go of.
void Forget(const unsigned char* v) noexcept {
    if(PJet* j=Find(v))*j=PJet{};
}

// The jet.cpp entry kept in step with the player's flight: its velocity (a carrier's drones dock on it, the engine
// sound follows it).
void MirrorEntry(const PJet& j,const unsigned char* v) noexcept {
    if(!j.board)return;
    if(jet::Jet* e=jet::FindJet(v))std::memcpy(e->m.vel,j.vel,12);
}

// An actual pilot resumes its call; an empty seat only requests existing soldiers to walk aboard.
void HandBack(PJet& j,unsigned char* v,const char* why) noexcept {
    j.keep=false;j.hail=PJet::Hail{};j.autopilot=false;j.active=false;j.steered=false;j.orbiting=false;
    if(catchFlight.v==v)catchFlight=CatchFlight{};
    if(SeatCount(v)>0 && SeatRider(SeatAt(v,0))==Rider::none && !v[kDead])SeatNpcRider(v,false);
    if(CrewedPilot(v)){jet::ResumeNpc(v,j.vel);Log("PJET v=%p back to its real NPC pilot: %s",v,why);}
    else Log("PJET v=%p has no local real pilot after hand-back: no autonomous flight",v);
}

void Boarded(PJet& j,unsigned char* v,const float* pos,float clear) noexcept {
    (void)pos;
    if(!j.board)return;
    j.hail=PJet::Hail{};j.keep=false;j.blastAt=0;j.orbiting=false;j.steered=false;
    // Its jet.cpp entry, made now for one a mission placed empty (jet::Adopt): the hover, the drones, the hand-back read it.
    jet::Jet* const e=jet::Adopt(v);
    if(e)e->carrier.ordered=false;
    if(j.board->frame==pjet::Airframe::rotor) {
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        j.yaw=std::atan2(m[8],m[10]);
        const bool down=clear!=kNoGround && clear<kTouch && Len(j.measured)<kBoardSpeed;
        j.phase=down ? Phase::parked : Phase::air;
        j.throttle=0.0f;j.hover=aim::Hold{};
        if(e){std::memcpy(e->m.vel,j.measured,12);std::memset(e->m.acc,0,12);}
    }
    Log("PJET v=%p one of ours boarded: %s (%s), its NPC pilot out",v,j.kind->name,j.board->frame==pjet::Airframe::rotor ? "rotor" : "wing");
}

void Left(PJet& j,unsigned char* v,float clear,bool alive,bool eject) noexcept {
    (void)clear;
    if(!j.board)return;
    j.steered=false;j.orbiting=false;
    // A live real pilot can bring this aircraft back; otherwise Catch may select another already
    // crewed aircraft. A destroyed or abandoned aircraft never creates an empty rescue twin.
    if(!alive || v[kDead]) {
        if(eject)Log("PJET v=%p lost under the player: the catch searches for a crewed aircraft",v);
        j=PJet{};
        return;
    }
    if(j.phase!=Phase::air){j.keep=true;Log("PJET v=%p left on the ground: it waits there for the player",v);return;}
    if(eject && j.board->frame==pjet::Airframe::wing && Cfg().playerJetCatch && CrewedPilot(v)) {
        // The jet itself comes back for them (Catch: it steers in under the parachute, presses the board button).
        const float* pos=reinterpret_cast<const float*>(v+kPosition);
        const float speed=std::fmax(Len(j.vel),j.kind->rotate+kCatchOver);
        // Its heading level along its flight (the catch's formation attitude and the speed the player takes it over
        // at: AutoFly, Board); the drift Catch refreshes every frame.
        float f[3]={j.vel[0],0.0f,j.vel[2]};
        if(!Normalize(f)){f[0]=0.0f;f[2]=1.0f;}
        catchFlight=CatchFlight{v,{pos[0]+j.vel[0]*5.0f,pos[1]+j.vel[1]*5.0f,pos[2]+j.vel[2]*5.0f},speed,{0.0f,0.0f,0.0f},
                                {f[0],0.0f,f[2]}};
        bail.caught=ObjRef::Of(v);bail.caughtAt=GameMs();bail.self=true;
        Log("PJET v=%p left in the air: it comes back to catch the player",v);
        return;
    }
    // Resume only with an actual pilot; otherwise leave the aircraft without autonomous control.
    HandBack(j,v,"the player left it in the air");
}

// --- The camera's sight: where the centre of the screen looks (the shells' and the drones' aim) ---
constexpr float kSightFar=3000.0f,kSightNone=1500.0f;

// Where the camera's centre meets the ground (kSightNone along it, meeting none within kSightFar); false: no camera.
bool Sight(float* out) noexcept {
    float eye[3],dir[3];
    if(!CameraRay(eye,dir))return false;
    const float end[3]={eye[0]+dir[0]*kSightFar,eye[1]+dir[1]*kSightFar,eye[2]+dir[2]*kSightFar};
    if(MapRay(eye,end,out)>=0.0f)return true;
    for(int i=0;i<3;++i)out[i]=eye[i]+dir[i]*kSightNone;
    return true;
}

// --- Its own weapon, as one more store (see the file comment) ---
// No weapon behind them (Store::weapon null): stores.cpp's lock, trigger and cycle calls pass them by; FireSpecial
// fires them. Only the bay is a bomb; guns, drones and detonation retain distinct roles.
// SpecialFrame supplies their real aim/order points independently of a falling bomb's CCIP. The gunship has up to three (SHELLS, then CANNON and GATLING each when its round is installed: jet_bay.cpp
// kSideGuns; an install from before has the guns it had, the preload logs the one missing), every other aircraft one at
// most (SpecialRoom).
constexpr StoreSpec kSpecials[]={
    {L"","BOMB BAY",StoreRole::bomb,0.0f,0.0f},
    {L"","SHELLS",StoreRole::gun,0.0f,0.0f},
    {L"","DRONES",StoreRole::drone,0.0f,0.0f},
    {L"","CHARGE",StoreRole::charge,0.0f,0.0f},
    {L"","CANNON",StoreRole::gun,0.0f,0.0f},
    {L"","GATLING",StoreRole::gun,0.0f,0.0f},
};
constexpr int kBay=0,kShells=1,kDrones=2,kCharge=3,kCannon=4,kGatling=5;
// The gunship's side guns as stores, in jet::SideGun's order, each listed while its round is there (jet::SideGunReady).
struct SideGunStore { int special; jet::SideGun gun; };
constexpr SideGunStore kSideGunStores[]={{kCannon,jet::SideGun::cannon},{kGatling,jet::SideGun::gatling}};
constexpr int kSpecialCount=static_cast<int>(sizeof(kSpecials)/sizeof(kSpecials[0]));
// The gunship's guns never run out (as its NPC's): shown as 99. Its cannon and gatling are no different: a magazine with a
// reload would only be a pause the NPC crew's guns do not have, and their rates (2 and 10 a second, jet_bay.cpp
// kCannonGapMs / kGatlingGapMs) are their limits.
constexpr int kShellRounds=99;
constexpr ULONGLONG kChargeFireMs=100,kChargeMs=300;   // jet_carrier.cpp kBlastFireMs / kBlastMs
constexpr float kOrderUp=40.0f;   // m: the drones' point over the sight (they work round it, jet.cpp)

// The room its own weapons take in the cockpit's cycle (kMostStores in all): the gunship's shells and its side guns whose
// rounds are installed (SpecialStore: a missing one's slot is a pylon's), else one.
int SpecialRoom(const PJet& j) noexcept {
    if(!j.board || j.board->arm!=pjet::Arm::shells)return 1;
    int n=1;
    for(const auto& g:kSideGunStores)n+=jet::SideGunReady(g.gun) ? 1 : 0;
    return n;
}

// Its own weapons as stores, at most SpecialRoom of them: their count.
int SpecialStore(PJet& j,const unsigned char* v,Store* out) noexcept {
    if(!j.board)return 0;
    int which=-1,rounds=0;
    if(const int bombs=jet::BayLeft(v)){which=kBay;rounds=bombs;}
    else if(j.board->arm==pjet::Arm::shells){which=kShells;rounds=jet::ShellsReady() ? kShellRounds : 0;}
    else if(j.board->arm==pjet::Arm::drones){which=kDrones;rounds=jet::DronesLeft(v);}
    else if(j.board->arm==pjet::Arm::charge){which=kCharge;rounds=j.blastAt ? 0 : 1;}
    if(which<0)return 0;
    out[0]=Store{nullptr,&kSpecials[which],rounds,0,0.0f};
    if(which!=kShells)return 1;
    int n=1;
    for(const auto& g:kSideGunStores)
        if(jet::SideGunReady(g.gun))out[n++]=Store{nullptr,&kSpecials[g.special],kShellRounds,0,0.0f};
    return n;
}

int SpecialOf(const StoreSpec* spec) noexcept {
    for(int i=0;i<kSpecialCount;++i)if(spec==&kSpecials[i])return i;
    return -1;
}

// What falls as a bomb when let go (its CCIP traced, Stores): a bomb store's weapon, or the bay's.
bool FallsAsBomb(const Store& st) noexcept { return st.weapon!=nullptr || SpecialOf(st.spec)==kBay; }

// The point the shells, the side guns and the drones go for: the pylon turn's centre while it flies one, else the
// camera's sight.
const float* AimPoint(const PJet& j) noexcept { return j.orbiting ? j.orbitAt : j.hasSight ? j.sight : nullptr; }

void FireSpecial(PJet& j,unsigned char* v,const Store& st,const float* pos) noexcept {
    (void)pos;
    const ULONGLONG ms=GameMs();
    const float* at=AimPoint(j);
    switch(SpecialOf(st.spec)) {
    case kBay:
        if(j.hasImpact && jet::PlayerOpenBay(v,j.impact,j.vel))Log("PJET v=%p bombs away",v);
        break;
    case kShells:
        if(at)jet::PlayerShell(v,at,ms);
        break;
    case kCannon:   // held, a round every kCannonGapMs (the fire byte is set again each frame it is held)
        if(at)jet::PlayerSideGun(v,jet::SideGun::cannon,at,ms);
        break;
    case kGatling:  // held, a round every kGatlingGapMs, scattered round the aim
        if(at)jet::PlayerSideGun(v,jet::SideGun::gatling,at,ms);
        break;
    case kDrones:
        if(at) {
            const float order[3]={at[0],at[1]+kOrderUp,at[2]};
            if(jet::PlayerLaunchDrone(v,order,ms))Log("PJET v=%p drone sent to (%.0f,%.0f,%.0f)",v,at[0],at[1],at[2]);
        }
        break;
    case kCharge:
        if(!j.blastAt){j.blastAt=ms;Log("PJET v=%p charge: off it goes",v);}
        break;
    default:
        break;
    }
}

// The gunship's pylon turn (the target key held with one of its guns picked): round the point the camera
// looked at when it was pressed, kOrbitMin to kOrbitMax out (where it was), that point on its left (the AC-130's guns'
// side), at the height it had; the plane steered by the aim's law (Air: steered), the player's mouse left to the camera.
constexpr float kOrbitMin=400.0f,kOrbitMax=900.0f;
void Orbit(PJet& j,const unsigned char* v,const float* pos,bool hold) noexcept {
    if(!hold){if(j.orbiting)Log("PJET v=%p pylon turn: off",v);j.orbiting=false;return;}
    if(!j.orbiting) {
        if(!j.hasSight || j.phase!=Phase::air)return;
        std::memcpy(j.orbitAt,j.sight,12);
        j.orbitR=Clamp(vec::Flat(pos,j.sight),kOrbitMin,kOrbitMax);
        j.orbiting=true;j.orbitAlt=pos[1];
        Log("PJET v=%p pylon turn round (%.0f,%.0f,%.0f), %.0f m out",v,j.orbitAt[0],j.orbitAt[1],j.orbitAt[2],j.orbitR);
    }
    float out[3]={pos[0]-j.orbitAt[0],0.0f,pos[2]-j.orbitAt[2]};
    const float dist=Len(out);
    if(!Normalize(out)){out[0]=1.0f;out[2]=0.0f;}
    const float pull=Clamp((j.orbitR-dist)/j.orbitR,-1.5f,1.5f);
    float aim[3]={out[2]+out[0]*pull,Clamp((j.orbitAlt-pos[1])/150.0f,-0.45f,0.6f),-out[0]+out[2]*pull};
    if(!Normalize(aim))return;
    std::memcpy(j.aim,aim,12);j.hasAim=true;j.steered=true;
}

void SpecialFrame(PJet& j,unsigned char* v,Stick& s,const float* pos,ULONGLONG ms) noexcept {
    j.steered=false;
    if(!j.board)return;
    const bool press=s.nextTarget && !j.specialHeld;
    j.specialHeld=s.nextTarget;
    j.hasSight=Sight(j.sight);
    int special=-1;
    for(int i=0;i<kSpecialCount;++i)if(j.stores>0 && j.store<j.stores && j.storeName[j.store]==kSpecials[i].name)special=i;
    const bool gun=special==kShells || special==kCannon || special==kGatling;
    Orbit(j,v,pos,gun && s.nextTarget);
    if(j.steered){s.aimX=0.0f;s.aimY=0.0f;}   // the pylon turn steers; the mouse only turns the camera
    if(gun || special==kDrones) {   // the cross on where they go, not a bomb's fall
        const float* at=AimPoint(j);
        j.hasImpact=at!=nullptr;
        if(at)std::memcpy(j.impact,at,12);
    }
    if(special==kDrones && press)jet::RecallDrones(v,ms);
    jet::PlayerBayFrame(v,pos);
    if(j.blastAt) {
        if(ms-j.blastAt<kChargeFireMs)v[kFireStore]=1;   // holder 2, the charge (jet_carrier.cpp Blast)
        else if(ms-j.blastAt>=kChargeMs && !v[kDead])Kill(j,v,"its charge went off");
    }
}

// --- An airframe's clearance (playerjet_kinds.h RestHeight / BottomClear) ---
// The grounded mesh origin is below the body's position on wings too; use the same fuselage bones as Body506.
float RestOver(const unsigned char* v,const float* pos) noexcept {
    const wchar_t* const names[]={L"bomber501",L"bomber401",L"body"};
    for(const auto name:names) {
        const unsigned char* const rec=BoneRecord506(v+kModelInst506,name);
        if(rec)return pjet::RestHeight(pos[1],At<float>(rec,kBoneWorld506+0x34));
    }
    return 0.0f;
}
// `clear` (from its position) as its bottom's. A large wing's centre can remain above kTouch while its wheels touch.
float FloorClear(const PJet& j,const unsigned char* v,const float* pos,float clear) noexcept {
    (void)j;
    return pjet::BottomClear(clear,RestOver(v,pos),kNoGround);
}

// --- A rotor craft under the player (the carriers, the blast and doll drones) ---
// On the keyboard and mouse with ini HeliMouseAim, the mouse-aim flight (heliaim.h, as a stock heli's: the mouse moves
// the aim, the craft faces it and flies along its elevation, W / S set the forward speed and leave it, A / D sidestep,
// Space / the brake key climb and descend at its most, let go it holds its height at the speed set). Else the stick
// asks for a velocity: forward / back (left stick, W / S) and sideways (A / D) up to its top speed (kHoverCruise of it
// on the keys, all of it with the boost key held), up with the ascend trigger (Space), down with L3 (the brake key), its
// height held with neither; its heading turns with the right stick (kHoverYaw rad/s at full, its kind's roll rate within
// those bounds) or the mouse (as the jets' aim: kAimPerUnit a unit). Either way its heading goes no more than kHoverLead
// ahead of its nose. jet_flight.cpp's Hover flies it there (its thrust, its response, its lean: the NPC's
// own rotor flight), HoldOffGround keeps it off the floor, the carrier's nacelles follow its thrust. Touching down
// faster than its landMax is a crash; set down still with nothing asked, it is parked (held still on its contacts:
// PlayerJetBodyStep's GroundContact, not the stock step that pulled it level against them).
constexpr float kHoverCruise=0.6f,kHoverYawMin=0.6f,kHoverYawMax=2.0f,kHoverLead=0.8f,kHoverClimbShare=0.4f;
constexpr float kSettle=2.0f,kSettleShare=0.8f;   // m/s, 1/s
constexpr std::uint16_t kButtonL3=0x40;

float HoverClimb(const jet::Kind& k) noexcept { return std::fmax(jet::kHoverClimb,k.cruise*kHoverClimbShare); }

// Velocity `want` (horizontal) and `vz` asked of Hover: its goal and speed for that.
void HoverTo(jet::Jet& e,const jet::Kind& k,const unsigned char* v,const float* pos,const float* want,float vz,const float* face,
             float dt) noexcept {
    float dir[3]={want[0],0.0f,want[2]};
    const float speed=Len(dir);
    float goal[3]={pos[0],pos[1]+vz*2.0f,pos[2]};
    if(Normalize(dir)) {
        const float reach=speed*speed/(2.0f*k.brake)+5.0f;   // far enough that Hover's braking curve asks the whole speed
        goal[0]+=dir[0]*reach;goal[2]+=dir[2]*reach;
    }
    const JetMass* const mass=JetMassOf(BodyMark(v));   // the player's own lift (hover_lift.h; the NPCs' Hover passes none)
    // Player flight already uses the map walls; a hail must reach its landing point outside the NPC patrol band too.
    jet::Hover(e,k,v,pos,goal,face,speed,HoverClimb(k),dt,mass ? hover::PlayerLift(mass->mass,Cfg().playerRotorLift) : 0.0f,false);
}

// The rotor craft's flight done: the player's record from its entry's, the nacelles, the doll; parked when it sits
// still on the ground with nothing asked (`still`).
void HoverDone(PJet& j,jet::Jet& e,const jet::Kind& k,unsigned char* v,const float* pos,float clear,bool still,float dt,ULONGLONG ms) noexcept {
    jet::HoldOffGround(e,pos,clear,dt,ms,RestOver(v,pos));   // `clear` is its bottom's (FloorClear)
    if(k.pose==jet::Pose::thrusters)jet::Thrusters(e,k,v,dt,ms,clear==kNoGround ? 1e9f : clear);
    jet::DollFrame(jet::IndexOf(e),v,clear);
    const bool down=clear!=kNoGround && clear<kTouch;
    if(down && still && Len(e.m.vel)<1.5f) {
        std::memset(e.m.vel,0,12);std::memset(e.m.acc,0,12);std::memset(e.m.omega,0,12);
        if(j.phase!=Phase::parked)Log("PJET v=%p set down",v);
        j.phase=Phase::parked;
    } else j.phase=Phase::air;
    std::memcpy(j.vel,e.m.vel,12);std::memcpy(j.omega,e.m.omega,12);
    j.load=1.0f;j.stall=false;j.clear=clear;j.climb=j.vel[1];
    j.active=!v[kDead] && handling::Drives(j.phase==Phase::parked,Body506ReadsSolver());
    std::memcpy(j.sent,j.vel,12);
}

// The stick's velocity (see above): `want` horizontal, `vz` up, along the heading j.yaw (turned by the caller).
void HoverStick(const PJet& j,const jet::Kind& k,const Stick& s,bool down,float* want,float* vz) noexcept {
    const float fwd[3]={std::sin(j.yaw),0.0f,std::cos(j.yaw)};
    float right[3];RightOf(fwd,right);
    const float fore=s.keys ? (s.ly<-0.3f ? 1.0f : s.ly>0.3f ? -1.0f : 0.0f) : -s.ly,side=s.lx;
    const float top=k.cruise*(s.keys && s.throttle<=0.0f ? kHoverCruise : 1.0f);
    want[0]=(fwd[0]*fore+right[0]*side)*top;want[1]=0.0f;want[2]=(fwd[2]*fore+right[2]*side)*top;
    const float asked=Len(want);
    if(asked>top)for(int i=0;i<3;i+=2)want[i]*=top/asked;
    *vz=(s.ascend-(down ? 1.0f : 0.0f))*HoverClimb(k);
}

// The mouse-aim flight's frame (heliaim.h): the aim moved and kept on the screen, the heading onto it, the velocity and
// the climb it asks (aim::Fly, its kind's cruise the top speed, HoverClimb the most climb).
void HoverAim(PJet& j,unsigned char* v,const Stick& s,const float* pos,float nose,float clear,bool down,float dt,float* want,
              float* vz) noexcept {
    const jet::Jet* const e=jet::FindJet(v);
    if(!e)return;
    const jet::Kind& k=jet::KindOf(e->role);
    const float level[3]={std::sin(nose),0.0f,std::cos(nose)};
    if(!j.hasAim){std::memcpy(j.aim,level,12);j.hasAim=true;j.hover=aim::Hold{};}
    float was[3];std::memcpy(was,j.aim,12);
    MoveAim(j,s);
    KeepAimOnScreen(j,v,level,was);
    const bool grounded=clear!=kNoGround && clear<kTouch;
    const aim::Keys keys{s.ly<-0.3f ? 1.0f : s.ly>0.3f ? -1.0f : 0.0f,s.lx,s.ascend-(down ? 1.0f : 0.0f)};
    // Its own velocity (Hover's), not the measured one: touching the ground the solver's push out is in that, and the
    // height the hold took on letting go (h.y = y + vy kHoldLead) rose with every bounce: the carrier let down on the
    // ground was thrown back up to a height it then held (the user, 2026-10-09: 「我都撞地上了，还会把空母往天上弹」).
    const aim::Want w=aim::Fly(j.hover,j.aim,level,pos,e->m.vel,keys,k.cruise,HoverClimb(k),grounded,grounded,
                               clear==kNoGround ? -1.0f : clear,dt);
    std::memcpy(want,w.vel,12);*vz=w.climb;
    j.yaw=std::atan2(w.face[0],w.face[2]);
}

void HoverStep(PJet& j,unsigned char* v,const Stick& s,const float* pos,float clear,bool wet,float dt,ULONGLONG ms) noexcept {
    jet::Jet* const e=jet::FindJet(v);
    if(!e){j.active=false;return;}
    const jet::Kind& k=jet::KindOf(e->role);
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float nose=std::atan2(m[8],m[10]);
    const bool down=s.keys ? s.throttle<0.0f : (At<std::uint16_t>(SeatAt(v,0),kSeatButtons)&kButtonL3)!=0;
    float want[3]={0.0f,0.0f,0.0f},vz=0.0f;
    const bool aimFlight=s.keys && Cfg().heliMouseAim;
    if(aimFlight)HoverAim(j,v,s,pos,nose,clear,down,dt,want,&vz);
    else {
        const float rate=Clamp(k.roll,kHoverYawMin,kHoverYawMax);
        j.yaw-=s.keys ? s.aimX*kAimPerUnit*Cfg().playerJetMouseSpeed : s.yaw*rate*dt;
        j.hover=aim::Hold{};j.hasAim=false;
    }
    j.hoverTop=k.cruise;
    j.yaw=nose+Clamp(std::remainder(j.yaw-nose,2.0f*kWatchPi),-kHoverLead,kHoverLead);
    if(!aimFlight)HoverStick(j,k,s,down,want,&vz);
    j.area=WallTurnVelocity(pos,want);   // the walls where the map's ground ends (playarea.h), as a wing's
    const float fwd[3]={std::sin(j.yaw),0.0f,std::cos(j.yaw)};
    const float asked=Len(want);
    // Coming down near the ground it slows (kSettleShare of its height a second, at least kSettle): set down, not dropped.
    if(vz<0.0f && clear!=kNoGround)vz=std::fmax(vz,-std::fmax(kSettle,clear*kSettleShare));
    const float face[3]={pos[0]+fwd[0]*100.0f,pos[1],pos[2]+fwd[2]*100.0f};
    const float sink=-j.measured[1];
    HoverTo(*e,k,v,pos,want,vz,face,dt);
    if(clear!=kNoGround && clear<kTouch && j.fresh && sink>j.kind->landMax)Crash(j,v,sink,0.0f,false,ms,nullptr);
    if(wet)Crash(j,v,0.0f,0.0f,false,ms,nullptr);   // afloat: it breaks up, one crash a kCrashMs
    if(v[kDead]){j.active=false;return;}
    HoverDone(j,*e,k,v,pos,clear,asked<0.1f && vz<=0.0f,dt,ms);
}

// --- The hail: one of ours called down to the player (see the file comment) ---
// A wing's strip (PlanStrip): its stop point on one of kRings rings round the player (nearest ring first), kAngles
// round it, its heading one of kHeads, kPlanPerFrame candidates a frame. A strip: from its touchdown point (its
// rollout before the stop: the approach speed's stop at kRollDecel, and kRollMargin) to kStripPast past the stop, the
// ground every kStripStep within kStripRise of the stop's and kStripStepRise of the last sample's, nothing standing in
// it kStripHigh and kStripHigher over it, the final approach (kFinal out on a kGlide slope) clear, the player
// kStripClear off it, the stop no more than kStopRise from the player's feet. Cheapest: nearest, then turning least
// from the jet's heading (kTurnCost a unit of 1 - cos).
constexpr float kRings[]={60.0f,120.0f,200.0f,320.0f,500.0f,800.0f};
constexpr int kRingCount=static_cast<int>(sizeof(kRings)/sizeof(kRings[0])),kAngles=12,kHeads=12,kPlanPerFrame=12;
constexpr float kRollDecel=8.0f,kRollMargin=40.0f,kStripPast=30.0f,kStripStep=25.0f,kStripRise=4.0f,kStripStepRise=1.5f;
constexpr float kStripHigh=4.0f,kStripHigher=12.0f,kFinal=1500.0f,kGlide=0.07f,kStripClear=25.0f,kStopRise=6.0f,kTurnCost=300.0f;
constexpr float kProbe=400.0f;
constexpr float kApproachOver=10.0f;   // m/s over its rotate speed: the final's speed
constexpr float kFinalLead=450.0f;     // m: the final's aim, ahead along the glide line
constexpr float kFinalOn=400.0f;       // m from the final's entry, flying along it within kFinalAlign: on the final
constexpr float kFinalAlign=0.85f;
constexpr float kMissPast=100.0f,kMissSide=150.0f,kMissHigh=80.0f,kMissLow=40.0f;   // a missed approach: go round

// The ground at (x, z) between y-kProbe and y+kProbe: its height, or false.
bool GroundAt(float x,float z,float y,float* out) noexcept {
    const float top[3]={x,y+kProbe,z},bottom[3]={x,y-kProbe,z};
    float hit[3];
    if(MapRay(top,bottom,hit)<0.0f)return false;
    float surface=0.0f;
    if(SeaAt(x,z,&surface)==Sea::water && surface>=hit[1]-0.5f)return false;   // the sea is no runway
    *out=hit[1];
    return true;
}

// Whether the segment a-b, kStripHigh / kStripHigher over the strip at `y`, meets anything.
bool StripBlocked(const float* a,const float* b,float y,float over) noexcept {
    const float from[3]={a[0],y+over,a[2]},to[3]={b[0],y+over,b[2]};
    float hit[3];
    return MapRay(from,to,hit)>=0.0f;
}

// Horizontal metres from `p` to the segment a-b.
float FlatToSegment(const float* p,const float* a,const float* b) noexcept {
    const float ab[2]={b[0]-a[0],b[2]-a[2]},ap[2]={p[0]-a[0],p[2]-a[2]};
    const float l=ab[0]*ab[0]+ab[1]*ab[1],t=l>1e-6f ? Clamp((ap[0]*ab[0]+ap[1]*ab[1])/l,0.0f,1.0f) : 0.0f;
    const float d[2]={ap[0]-ab[0]*t,ap[1]-ab[1]*t};
    return std::sqrt(d[0]*d[0]+d[1]*d[1]);
}

// The strip stopping at `stop` (y: the player's) along `dir` for kind `k` (see kRings): its cost, or -1.
float StripCost(const Kind& k,float* stop,const float* dir,const float* who,const float* heading,float ring) noexcept {
    float y=0.0f;
    if(!GroundAt(stop[0],stop[2],who[1],&y) || std::fabs(y-who[1])>kStopRise)return -1.0f;
    stop[1]=y;
    const float speed=k.rotate+kApproachOver,roll=speed*speed/(2.0f*kRollDecel)+kRollMargin;
    float last=y;
    for(float d=kStripStep;d<=roll+kStripStep*0.5f;d+=kStripStep) {
        float g=0.0f;
        if(!GroundAt(stop[0]-dir[0]*d,stop[2]-dir[2]*d,y,&g) || std::fabs(g-y)>kStripRise || std::fabs(g-last)>kStripStepRise)return -1.0f;
        last=g;
    }
    const float from[3]={stop[0]-dir[0]*roll,y,stop[2]-dir[2]*roll},to[3]={stop[0]+dir[0]*kStripPast,y,stop[2]+dir[2]*kStripPast};
    if(FlatToSegment(who,from,to)<kStripClear || StripBlocked(from,to,y,kStripHigh) || StripBlocked(from,to,y,kStripHigher))return -1.0f;
    const float entry[3]={from[0]-dir[0]*kFinal,y+kStripHigh+kFinal*kGlide,from[2]-dir[2]*kFinal},touch[3]={from[0],y+kStripHigh,from[2]};
    float hit[3];
    if(MapRay(entry,touch,hit)>=0.0f)return -1.0f;
    return ring+(1.0f-Dot(dir,heading))*kTurnCost;
}

// The rollout's start: where it touches down.
void TouchPoint(const Kind& k,PJet::Hail& h) noexcept {
    const float speed=k.rotate+kApproachOver,roll=speed*speed/(2.0f*kRollDecel)+kRollMargin;
    for(int i=0;i<3;++i)h.touch[i]=h.stop[i]-h.dir[i]*roll;
    h.touch[1]=h.stop[1];
}

void PlanStrip(PJet& j,unsigned char* v,ULONGLONG ms) noexcept {
    PJet::Hail& h=j.hail;
    float heading[3]={j.vel[0],0.0f,j.vel[2]};
    if(!Normalize(heading)){heading[0]=0.0f;heading[2]=1.0f;}
    const int perRing=kAngles*kHeads,all=perRing*kRingCount;
    for(int n=0;n<kPlanPerFrame && h.cand<all;++n,++h.cand) {
        const int ring=h.cand/perRing,angle=(h.cand/kHeads)%kAngles,head=h.cand%kHeads;
        const float a=2.0f*kWatchPi*static_cast<float>(angle)/kAngles,b=2.0f*kWatchPi*static_cast<float>(head)/kHeads;
        float stop[3]={player.pos[0]+std::cos(a)*kRings[ring],player.pos[1],player.pos[2]+std::sin(a)*kRings[ring]};
        const float dir[3]={std::sin(b),0.0f,std::cos(b)};
        const float cost=StripCost(*j.kind,stop,dir,player.pos,heading,kRings[ring]);
        if(cost<0.0f || (h.best>0.0f && cost>=h.best))continue;
        h.best=cost;std::memcpy(h.bestStop,stop,12);std::memcpy(h.bestDir,dir,12);
    }
    const bool ringDone=h.cand%perRing==0 || h.cand>=all;
    if(!ringDone || (h.best<=0.0f && h.cand<all))return;
    if(h.best<=0.0f){HandBack(j,v,"no place to land near the player");return;}
    std::memcpy(h.stop,h.bestStop,12);std::memcpy(h.dir,h.bestDir,12);
    TouchPoint(*j.kind,h);
    h.phase=kHailFerry;h.at=ms;
    Log("PJET hail v=%p: a strip %.0f m from the player, heading %.0f deg, touchdown at (%.0f,%.0f,%.0f), stop at (%.0f,%.0f,%.0f)",v,
        vec::Flat(h.stop,player.pos),std::atan2(h.dir[0],h.dir[2])*180.0f/kWatchPi,h.touch[0],h.touch[1],h.touch[2],h.stop[0],h.stop[1],
        h.stop[2]);
}

// A wing flown at `goal` at `want` m/s by the aim's law (Air on the autopilot: the catch's way), never down under
// kCatchFloor over the ground but on the final; above `want` + kAirbrakeOver the airbrake.
constexpr float kAirbrakeOver=15.0f,kSpeedGain=0.03f;
void SteerAt(PJet& j,unsigned char* v,const float* pos,const float* goal,float want,bool final,float clear,bool water,float dt,
             ULONGLONG ms) noexcept {
    const Kind& k=*j.kind;
    float to[3]={goal[0]-pos[0],goal[1]-pos[1],goal[2]-pos[2]};
    if(!final && clear!=kNoGround && clear<kCatchFloor && to[1]<0.0f)to[1]=0.0f;
    if(!Normalize(to))std::memcpy(to,j.aim,12);
    std::memcpy(j.aim,to,12);j.hasAim=true;
    const float speed=Len(j.vel);
    j.throttle=Clamp((want-k.minAir)/(k.top-k.minAir)+(want-speed)*kSpeedGain,0.0f,1.0f);
    Stick s{};
    s.keys=true;s.throttle=speed>want+kAirbrakeOver ? -1.0f : 0.0f;
    j.climb=j.vel[1];
    Air(j,v,s,pos,clear,water,dt,ms);
    j.active=!v[kDead];
    std::memcpy(j.sent,j.vel,12);
    MirrorEntry(j,v);
    Elevons(j,v,0.0f,0.0f,dt);
    // Its gear (gear.cpp): no one aboard steps it here (PilotGear is the player's, NpcGear the NPC flight's, and a held
    // jet flies neither), so down on the final (well over the ~3 s it takes before the touch), up before; a landing with
    // it up would be Touch's belly landing.
    GearStep(v,!final,dt,false);
    JetFlames(v,j.throttle,false,ms);
}

// The approach (kHailFerry: to the final's entry, turned onto the strip's line on the way; kHailFinal: down the glide
// slope to the touchdown point at its rotate speed + kApproachOver). Air lands it (Touch); a missed approach goes round.
void Approach(PJet& j,unsigned char* v,const float* pos,float clear,bool water,float dt,ULONGLONG ms) noexcept {
    PJet::Hail& h=j.hail;
    const Kind& k=*j.kind;
    if(j.phase!=Phase::air){h.phase=kHailRoll;h.at=ms;return;}   // Air's Touch landed it
    const float* d=h.dir;
    const float entry[3]={h.touch[0]-d[0]*kFinal,h.touch[1]+kStripHigh+kFinal*kGlide,h.touch[2]-d[2]*kFinal};
    const float rel[3]={pos[0]-h.touch[0],0.0f,pos[2]-h.touch[2]};
    const float along=rel[0]*d[0]+rel[2]*d[2],side=std::fabs(rel[0]*d[2]-rel[2]*d[0]);
    if(h.phase==kHailFerry) {
        const float out=vec::Flat(pos,entry),lead=Clamp(out*0.5f,0.0f,2000.0f);
        const float goal[3]={entry[0]-d[0]*lead,entry[1],entry[2]-d[2]*lead};
        float track[3]={j.vel[0],0.0f,j.vel[2]};
        Normalize(track);
        SteerAt(j,v,pos,goal,k.rotate+kApproachOver*4.0f,false,clear,water,dt,ms);
        if(out<kFinalOn && Dot(track,d)>kFinalAlign){h.phase=kHailFinal;h.at=ms;Log("PJET hail v=%p: on the final",v);}
        return;
    }
    const float glide=h.touch[1]+kStripHigh-along*kGlide;   // the slope's height here (along < 0 before the touchdown)
    if(along>kMissPast || (along<-kFinalLead && side>kMissSide) || pos[1]>glide+kMissHigh || pos[1]<glide-kMissLow) {
        if(++h.tries>=kHailTries){HandBack(j,v,"it could not land");return;}
        h.phase=kHailFerry;h.at=ms;
        Log("PJET hail v=%p: missed approach (%.0f m along, %.0f m off the line, %.0f m over the slope): round again",v,along,side,pos[1]-glide);
        return;
    }
    const float ahead=along+kFinalLead;
    const float goal[3]={h.touch[0]+d[0]*ahead,h.touch[1]+(ahead<0.0f ? kStripHigh-ahead*kGlide : -2.0f),h.touch[2]+d[2]*ahead};
    SteerAt(j,v,pos,goal,k.rotate+kApproachOver,true,clear,water,dt,ms);
}

// The rollout (kHailRoll): along its nose to the stop, braking so it stops there (at most kRollDecel), steered
// at it; parked: it waits.
void Rollout(PJet& j,unsigned char* v,const float* pos,float clear,float dt,ULONGLONG ms) noexcept {
    PJet::Hail& h=j.hail;
    if(j.phase==Phase::air){HandBack(j,v,"it left the ground rolling out");return;}
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float nose[3]={m[8],0.0f,m[10]};
    if(!Normalize(nose)){nose[0]=0.0f;nose[2]=1.0f;}
    float to[3]={h.stop[0]-pos[0],0.0f,h.stop[2]-pos[2]};
    const float ahead=Dot(to,nose);
    Normalize(to);
    float right[3];RightOf(nose,right);
    Stick s{};
    s.keys=true;
    s.turn=ahead>15.0f ? Clamp(Dot(to,right)*3.0f,-1.0f,1.0f) : 0.0f;
    const float want=ahead>2.0f ? std::sqrt(2.0f*kRollDecel*ahead) : 0.0f;
    j.throttle=want>1.0f ? Clamp(want/j.kind->top,0.0f,1.0f) : 0.0f;
    Ground(j,v,s,clear,dt);
    j.active=j.phase!=Phase::parked && !v[kDead];
    std::memcpy(j.sent,j.vel,12);
    MirrorEntry(j,v);
    JetFlames(v,j.throttle,false,ms);
    if(j.phase!=Phase::parked)return;
    h.phase=kHailWait;h.at=ms;j.autopilot=false;
    Log("PJET hail v=%p: down %.0f m from the player, waiting %.0f s for them",v,vec::Flat(pos,player.pos),
        static_cast<float>(kHailWaitMs)*0.001f);
}

// A rotor craft's spot by the player: kSpotGap past its own size from them, on the side it comes from, else round
// them (kAngles) where the ground under its size is within kSpotRise of level and of their feet.
constexpr float kSpotGap=8.0f,kSpotRise=3.0f,kSpotSize=1.2f,kSpotDescend=120.0f,kSpotOver=80.0f;
void PickSpot(PJet& j,const float* pos) noexcept {
    float from[3]={pos[0]-player.pos[0],0.0f,pos[2]-player.pos[2]};
    if(!Normalize(from)){from[0]=1.0f;from[2]=0.0f;}
    const float out=j.kind->ram*kSpotSize+kSpotGap,half=j.kind->ram*0.5f;
    const float base=std::atan2(from[2],from[0]);
    for(int i=0;i<kAngles;++i) {
        const float a=base+2.0f*kWatchPi*static_cast<float>((i+1)/2*(i%2 ? 1 : -1))/kAngles;
        const float x=player.pos[0]+std::cos(a)*out,z=player.pos[2]+std::sin(a)*out;
        float y=0.0f;
        if(!GroundAt(x,z,player.pos[1],&y) || std::fabs(y-player.pos[1])>kStopRise)continue;
        bool flat=true;
        for(int c=0;c<4 && flat;++c) {
            float g=0.0f;
            flat=GroundAt(x+(c&1 ? half : -half),z+(c&2 ? half : -half),y,&g) && std::fabs(g-y)<kSpotRise;
        }
        if(!flat)continue;
        j.hail.stop[0]=x;j.hail.stop[1]=y;j.hail.stop[2]=z;
        return;
    }
    j.hail.stop[0]=player.pos[0]+from[0]*out;j.hail.stop[1]=player.pos[1];j.hail.stop[2]=player.pos[2]+from[2]*out;
}

// kHailRotor: over to its spot at its height, then straight down onto it, its nose on the player; set down: it waits.
void RotorHail(PJet& j,unsigned char* v,const float* pos,float clear,float dt,ULONGLONG ms) noexcept {
    jet::Jet* const e=jet::FindJet(v);
    if(!e){HandBack(j,v,"its flight is gone");return;}
    const jet::Kind& k=jet::KindOf(e->role);
    if(!j.hail.spot || vec::Flat(j.hail.stop,player.pos)>j.kind->ram*kSpotSize+kSpotGap*4.0f){PickSpot(j,pos);j.hail.spot=true;}
    const float* spot=j.hail.stop;
    const float off=vec::Flat(pos,spot);
    float to[3]={spot[0]-pos[0],0.0f,spot[2]-pos[2]};
    const float speed=Normalize(to) ? std::fmin(k.cruise,std::sqrt(2.0f*k.brake*off)) : 0.0f;
    const float want[3]={to[0]*speed,0.0f,to[2]*speed};
    const float rest=RestOver(v,pos);   // its position over its bottom: down is the spot plus that
    const float height=off>kSpotDescend ? std::fmax(pos[1],spot[1]+rest+kSpotOver) : spot[1]+rest;
    const float vz=Clamp((height-pos[1])*0.5f,-HoverClimb(k),HoverClimb(k));
    HoverTo(*e,k,v,pos,want,vz,player.pos,dt);
    HoverDone(j,*e,k,v,pos,clear,off<4.0f,dt,ms);
    if(j.phase!=Phase::parked)return;
    j.hail.phase=kHailWait;j.hail.at=ms;j.autopilot=false;
    Log("PJET hail v=%p: set down %.0f m from the player, waiting %.0f s for them",v,vec::Flat(pos,player.pos),
        static_cast<float>(kHailWaitMs)*0.001f);
}

void HailFly(PJet& j,unsigned char* v,const float* pos,float dt,ULONGLONG ms) noexcept {
    Put<float>(v,kInLateral,0.0f);Put<float>(v,kInForward,0.0f);Put<float>(v,kInYaw,0.0f);
    Put<float>(v,kInThrottle,0.0f);Put<float>(v,kInW,1.0f);
    Put<float>(v,kAreaInset,kNoInset);
    bool water=false;
    const float clear=FloorClear(j,v,pos,Clear(pos,&water));
    j.clear=clear;
    PJet::Hail& h=j.hail;
    if(h.phase==kHailWait) {
        j.active=false;
        if(ms-h.at>kHailWaitMs)HandBack(j,v,"the player did not come");
        return;
    }
    if(ms-h.at>kHailMostMs){HandBack(j,v,"it could not come down in time");return;}
    switch(h.phase) {
    case kHailPlan: {
        PlanStrip(j,v,ms);
        if(j.hail.phase!=kHailPlan && j.hail.phase!=kHailFerry)return;   // handed back
        const float over[3]={player.pos[0],player.pos[1]+300.0f,player.pos[2]};   // toward the player meanwhile
        SteerAt(j,v,pos,over,j.kind->rotate+kApproachOver*4.0f,false,clear,water,dt,ms);
        return;
    }
    case kHailFerry:
    case kHailFinal:
        Approach(j,v,pos,clear,water,dt,ms);
        return;
    case kHailRoll:
        Rollout(j,v,pos,clear,dt,ms);
        return;
    case kHailRotor:
        RotorHail(j,v,pos,clear,dt,ms);
        return;
    default:
        HandBack(j,v,"lost its way");
        return;
    }
}

// The NPC aircraft `e` called down for the player.
void Hail(jet::Jet& e,ULONGLONG ms) noexcept {
    unsigned char* const v=e.Vehicle();
    const Kind* const kind=KindOf(v);
    if(!kind)return;
    PJet* j=Find(v);
    if(!j && (j=Make(v,kind))!=nullptr)j->board=BoardRowOf(v);
    if(!j || !j->board)return;
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    j->hail=PJet::Hail{};j->hail.at=ms;
    std::memcpy(j->vel,e.m.vel,12);
    j->havePrev=false;j->hasUp=false;j->hasAim=false;j->keep=false;
    const float clear=GroundClearance(pos);
    const bool down=clear!=kNoGround && clear<kOffGround;
    if(j->board->frame==pjet::Airframe::rotor){j->hail.phase=kHailRotor;j->phase=Phase::air;}
    else if(!down){j->hail.phase=kHailPlan;j->phase=Phase::air;j->throttle=kCruiseThrottle;}
    else {   // a wing on the ground already: it stops where it is
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        float nose[3]={m[8],0.0f,m[10]};
        if(!Normalize(nose)){nose[0]=0.0f;nose[2]=1.0f;}
        const float speed=Len(j->vel);
        for(int i=0;i<3;++i)j->hail.stop[i]=pos[i]+nose[i]*speed*speed/(2.0f*kRollDecel);
        std::memcpy(j->hail.dir,nose,12);
        j->hail.phase=kHailRoll;j->phase=speed>kParkSpeed ? Phase::rolling : Phase::parked;
    }
    j->autopilot=true;   // flown by the plugin until it waits (PlayerJetBodyStep)
    Log("PJET hail: %s v=%p, %.0f m from the player, comes down for them (%s)",kind->name,v,vec::Dist(pos,player.pos),kHailNames[j->hail.phase]);
}

// Once a frame: the hail key (on its press, the player on foot) calls the nearest aircraft of ours (alive, flown by its
// NPC pilot, not leaving, of our side) within kHailRange down to the player; a hail going is let go first (its jet
// back to its call), so the next press calls the next nearest. (A catch the parachute ended without is let go in
// EndEject, for every kind of jet.)
// The aircraft the hail key calls for a player at `p` (HailTick; the HUD's hint, PlayerJetHailHint, shows the same one):
// the nearest within kHailRange of ours, alive, flown by its NPC pilot, not leaving, `skip` aside; `*dist` its distance.
jet::Jet* HailChoice(const float* p,const void* skip,float* dist) noexcept {
    jet::Jet* best=nullptr;
    float bestD=kHailRange;
    for(auto& e:jet::jets) {
        if(!e.ref || e.reap || e.mode==jet::Mode::withdraw || e.drone.blastAt || !jet::Alive(e.ref))continue;
        unsigned char* const v=e.Vehicle();
        if(v==skip || v[kDead] || At<std::int32_t>(v,kTeam)!=jet::kTeamFriend || !CrewedPilot(v) ||
           !BoardRowOf(v) || Held(v))continue;
        const float d=vec::Dist(reinterpret_cast<const float*>(v+kPosition),p);
        if(d<bestD){bestD=d;best=&e;}
    }
    if(dist)*dist=bestD;
    return best;
}

jet::Jet* CatchChoice(const float* p) noexcept {
    jet::Jet* best=nullptr;float bestD=kHailRange;
    for(auto& e:jet::jets) {
        if(!e.ref || e.reap || e.mode==jet::Mode::withdraw || e.drone.blastAt || !jet::Alive(e.ref))continue;
        auto* v=e.Vehicle();
        const auto* row=BoardRowOf(v);
        if(v[kDead] || !CrewedPilot(v) || At<std::int32_t>(v,kTeam)!=jet::kTeamFriend ||
           !row || row->frame!=pjet::Airframe::wing || Held(v))continue;
        bool human=false;for(unsigned i=0;i<SeatCount(v);++i)human=human || AnyPlayerIn(SeatAt(v,i));
        if(human)continue;
        const float d=vec::Dist(reinterpret_cast<const float*>(v+kPosition),p);
        if(d<bestD){bestD=d;best=&e;}
    }
    return best;
}

void HailTick() noexcept {
    static bool held=false;
    const bool down=KeyDown(Cfg().playerJetHailKey);
    const bool press=down && !held;
    held=down;
    if(!press || !Cfg().playerJetAll)return;
    unsigned char* const human=PlayerHuman();
    if(!human || human[kDead] || (At<std::uint32_t>(human,kHumanFlags)&kRiding))return;
    const ULONGLONG ms=GameMs();
    const void* skip=nullptr;
    for(auto& j:jets)
        if(j.vehicle && j.hail.phase!=kHailNone && Live(j)){skip=j.vehicle;HandBack(j,j.vehicle,"the player called another");}
    jet::Jet* const best=HailChoice(reinterpret_cast<const float*>(human+kPosition),skip,nullptr);
    if(!best){Log("PJET hail: no aircraft of ours to call");return;}
    Hail(*best,ms);
}

// The supported, live friendly aircraft, before walking reach/speed checks.
bool BoardingSupported(const unsigned char* v) noexcept {
    if(!flyOk || !Cfg().enabled || !Cfg().playerJet || v[kDead] || !BoardRowOf(v))return false;
    const std::int32_t team=At<std::int32_t>(v,kTeam);
    if(team!=jet::kTeamFriend && team!=kTeamVehicle && team!=player.team)return false;
    const jet::Jet* const e=jet::FindJet(v);
    if(e && (e->reap || e->drone.blastAt))return false;
    return true;
}

bool BoardableNow(const unsigned char* v) noexcept {
    if(!BoardingSupported(v))return false;
    // The gun has already made an actual collision with this exact aircraft. The
    // transient stock-button call supplies its seat point; walking reach is irrelevant.
    if(BoardingOnly()==v)return true;
    const jet::Jet* const e=jet::FindJet(v);
    // The catch's (Left: it came back for the parachuting player) is boarded in the air at its speed, as the player
    // jet's catch is: the low-and-slow gate below is for one standing by, and turned the catch's every board press
    // down (no seat for them: "not aboard yet" under the parachute for 45 s, then handed back to its NPC).
    if(bail.state==Eject::chute && bail.caught.Is(v) && catchFlight.v==v)return true;
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float clear=GroundClearance(pos);
    if(clear==kNoGround || clear>kBoardClear)return false;
    const PJet* j=Find(v);
    const float speed=j && j->fresh ? Len(j->measured) : e ? e->m.real : 0.0f;
    return speed<=kBoardSpeed;
}
