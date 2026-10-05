// Included by playerjet.cpp after playerjet_board.inc, in its file namespace: the gunship's crew (2026-10-05, the user:
// "炮舰机应该是多人开的", an AC-130's crew; for now one machine, the player and an NPC). The gunship's SGO has two seats
// (tools/make_jets.py with_gunner_seat): seat 0 its pilot, kGunnerSeat its side gunner, boarded from the same door.
//  - Boarding (crew.cpp GunshipSeat, under PlayerJetBoardable's rules: on the ground or low and slow; a hailed one waiting
//    for the player): the board button takes the seat GunshipBoardSeat names (ini GunshipBoardGunner, the other one
//    while GunshipGunnerKey is held). The NPC in that seat moves to the other one when it is free (the pilot to the gun,
//    the gunner up to the stick), else it is kicked: never two pilots.
//  - The player at the stick: the player jet's path (Fly) as before; an NPC at the gun (the pilot the player bumped there)
//    shells ground targets round the gunship (CrewGunner: jet_bay.cpp CrewShell, GunshipFire's target, reach and gap; one
//    gun, the pilot's own SHELLS share its gap). Leaving the stick in the air is the pilot's rule (Left: the catch, or
//    HandBack: the stock RideAi in the empty pilot seat); the NPC gunner stays at the gun.
//  - The player at the gun (GunnerFrame): the NPC pilot flies on (jet.cpp JetFrame: its pylon turn, the target on its left,
//    round PlayerGunnerOrder's centre: the point the player last shelled for kGunnerCentreMs, else the call's own point or
//    where they boarded); the seat's primary trigger fires a shell at where the screen's centre meets the ground (CameraRay
//    as the pilot's SHELLS, jet::PlayerShell), the HUD's cross on it (hud.cpp GunnerMarks). Held for them as the pilot seat
//    is: no fuel burnt, no withdrawal, no deletion (jet.cpp GunnerHold, JetReap), no crewing (crew.cpp: a player aboard). A
//    hailed or parked gunship goes back to its NPC pilot as they take the gun (HandBack: a pilot seated if the seat is
//    empty, its call resumed: it takes off); a pilot seat found empty under them gets the stock RideAi (EnsurePilot).
//  - Leaving the gun (GunnerLeft): over kEjectFrom the parachute (EjectStart, no catch); the gunship goes on with its call
//    under its NPC pilot, on its pilot's team again.
constexpr ULONGLONG kGunnerCentreMs=30000;   // the pylon turn stays round the point last shelled this long
constexpr std::size_t kSeatFire=0x2E4;       // the seat's primary trigger (docs/heli-input-re.md §3: >= kSeatFireOn fires)
constexpr float kSeatFireOn=0.8f;
constexpr ULONGLONG kGunnerLogMs=2000;

struct Gunner {
    ObjRef ref;                  // the gunship the player sits at the gun of
    unsigned char* vehicle;
    float home[3];               // where they boarded
    float centre[3];             // the point they last shelled (centred: within kGunnerCentreMs)
    bool centred;
    ULONGLONG centreAt;
    float vel[3],clear;          // the gunship's velocity and height over the ground last frame (the parachute's)
    ULONGLONG frame,logAt;       // GameFrame of its last GunnerFrame
    bool pilotTried;             // EnsurePilot seated one (or failed to) since the pilot seat was last held
    GunnerReadout hud;
} gunner{};

bool GunshipSeats(const unsigned char* v) noexcept {
    if(BodyOf(v)!=PluginBody::jet || SeatCount(v)<=kGunnerSeat)return false;
    const pjet::Boardable* const row=BoardRowOf(v);
    return row && row->arm==pjet::Arm::shells;
}

// Where the screen's centre meets the ground within kSightFar (false: no camera, or no ground along it).
bool GroundSight(float* out) noexcept {
    float eye[3],dir[3];
    if(!CameraRay(eye,dir))return false;
    const float end[3]={eye[0]+dir[0]*kSightFar,eye[1]+dir[1]*kSightFar,eye[2]+dir[2]*kSightFar};
    return MapRay(eye,end,out)>=0.0f;
}

// The NPC at the gun under the player at the stick (see the file comment).
void CrewGunner(const PJet& j,unsigned char* v,float dt,ULONGLONG ms) noexcept {
    if(!j.board || j.board->arm!=pjet::Arm::shells || j.phase!=Phase::air || SeatCount(v)<=kGunnerSeat ||
       SeatRider(SeatAt(v,kGunnerSeat))!=Rider::dummy)return;
    jet::CrewShell(v,dt,ms);
}

// The player left the gun (`alive`: the gunship can still be read): over kEjectFrom they go out under the parachute
// (EjectStart with no jet of theirs: no catch, bail.mark 0); the gunship back on its NPC pilot's team.
void GunnerLeft(bool alive,const char* how) noexcept {
    unsigned char* const v=gunner.vehicle;
    const bool live=alive && !v[kDead];   // `v` read only when it can be
    const bool high=gunner.clear==kNoGround || gunner.clear>kEjectFrom;
    if(high) {
        PJet t{};
        std::memcpy(t.vel,gunner.vel,12);
        EjectStart(t,v,live);
        bail.mark=0.0f;
    }
    if(live && SeatRider(SeatAt(v,0))==Rider::dummy && At<std::int32_t>(v,kTeam)!=jet::kTeamFriend)
        SetObjectTeam(v,jet::kTeamFriend);
    Log("GUNNER v=%p the player left the gun: %s (%.0f m over the ground)%s",v,how,gunner.clear==kNoGround ? -1.0f : gunner.clear,
        high ? ": out under the parachute" : "");
    gunner=Gunner{};
}

void GunnerBoarded(unsigned char* v) noexcept {
    gunner=Gunner{};
    gunner.ref=ObjRef::Of(v);gunner.vehicle=v;
    std::memcpy(gunner.home,v+kPosition,12);
    // Called down for them, or left parked by them: its NPC pilot takes it back (seated if the seat is empty) and flies
    // its call on from here (jet.cpp ResumeNpc: off the ground it takes off).
    if(PJet* j=Find(v);j && j->board && (j->hail.phase!=kHailNone || j->keep || j->autopilot))HandBack(*j,v,"the player took its gun");
    Log("GUNNER v=%p the player at the gun, its pilot seat %s",v,SeatRider(SeatAt(v,0))==Rider::dummy ? "an NPC's" : "empty");
}

// The gunship never without a pilot under the player at its gun: an empty pilot seat gets the stock RideAi, once (a
// failure is logged, not retried); its flight resumes from where it is.
void EnsurePilot(unsigned char* v) noexcept {
    if(SeatRider(SeatAt(v,0))!=Rider::none){gunner.pilotTried=false;return;}
    if(gunner.pilotTried)return;
    gunner.pilotTried=true;
    reinterpret_cast<void(__fastcall* const*)(void*,bool)>(At<void* const*>(v,0))[kSlotRideAi](v,false);
    const bool seated=SeatRider(SeatAt(v,0))==Rider::dummy;
    Log("GUNNER v=%p no one at the stick: %s",v,seated ? "an NPC pilot seated" : "the stock RideAi seated no one");
    if(seated)jet::ResumeNpc(v,gunner.vel);
}

// The gun (see the file comment): the trigger fires at the ground sight, the pylon turn moves to what was shelled.
void GunnerFire(unsigned char* v,const float* pos,ULONGLONG ms) noexcept {
    const unsigned char* const seat=SeatAt(v,kGunnerSeat);
    const float trigger=At<float>(seat,kSeatFire);
    const bool pull=std::isfinite(trigger) && trigger>=kSeatFireOn;
    GunnerReadout& r=gunner.hud;
    r=GunnerReadout{};
    r.ground=GroundSight(r.sight);
    r.range=r.ground ? vec::Dist(pos,r.sight) : 0.0f;
    r.inReach=r.ground && r.range<=jet::ShellReach();
    r.wait=jet::ShellWait(v,ms);
    r.ready=jet::ShellsReady() && r.wait<=0.0f;
    if(pull && r.inReach && jet::PlayerShell(v,r.sight,ms)) {
        std::memcpy(gunner.centre,r.sight,12);gunner.centred=true;gunner.centreAt=ms;
        r.wait=jet::ShellWait(v,ms);r.ready=false;
    }
    if(gunner.centred && ms-gunner.centreAt>kGunnerCentreMs)gunner.centred=false;
    r.centred=gunner.centred;
    std::memcpy(r.centre,gunner.centre,12);
    if(!Cfg().debug || ms-gunner.logAt<kGunnerLogMs)return;
    gunner.logAt=ms;
    Log("GUNNER v=%p trigger %.2f (%s) sight %s (%.0f,%.0f,%.0f) %.0f m%s, gun %s, turn round %s (%.0f,%.0f,%.0f)",v,trigger,
        At<unsigned char>(seat,kSeatPad) ? "pad" : "keys",r.ground ? "on the ground" : "none",r.sight[0],r.sight[1],r.sight[2],r.range,
        r.inReach ? "" : " (out of reach)",r.ready ? "ready" : "waiting",gunner.centred ? "the point shelled" : "its call's point",
        gunner.centred ? gunner.centre[0] : gunner.home[0],gunner.centred ? gunner.centre[1] : gunner.home[1],
        gunner.centred ? gunner.centre[2] : gunner.home[2]);
}

// Every vehicle's frame (PlayerJetFrame, before the jet's own steps): the player's coming to, staying at and leaving the
// gun of a gunship with both seats.
void GunnerFrame(unsigned char* v) noexcept {
    if(!Cfg().playerJetAll || !GunshipSeats(v))return;
    const bool mine=gunner.ref.Is(v);
    const bool aboard=!v[kDead] && SeatRider(SeatAt(v,kGunnerSeat))==Rider::player;
    if(mine && !aboard){GunnerLeft(true,v[kDead] ? "the gunship destroyed" : "got out");return;}
    if(!aboard)return;
    if(!mine)GunnerBoarded(v);
    const ULONGLONG ms=GameMs();
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    gunner.frame=GameFrame();
    if(const jet::Jet* e=jet::FindJet(v))std::memcpy(gunner.vel,e->m.vel,12);
    gunner.clear=GroundClearance(pos);
    EnsurePilot(v);
    GunnerFire(v,pos,ms);
}

// Once a frame: a gunship gone with the player at its gun (deleted, its wreck gone, no frame of it since): they left.
void GunnerTick() noexcept {
    if(!gunner.ref || gunner.frame+2>GameFrame())return;
    bool gone=true;
    __try { gone=!gunner.ref.Is(gunner.vehicle) || gunner.vehicle[kDead]; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if(gone)GunnerLeft(false,"the gunship gone");
}
