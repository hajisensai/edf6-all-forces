// Included by dreadnought.cpp in its file namespace: its hangars' fighters and its bridge (the user, 2026-10-21: 「那艘可以
// 放飞船的无畏舰。飞船可以玩家上去的那种」, then 「无畏舰下船应该在舰桥才对。不是直接变到飞机上。飞机应该往机库停进去和飞
// 出来，不能凭空出现和消失」).
// Its four hangars are the Dreadnought game's, where it carries them: in its broadsides (the hull's AB_primary sockets,
// tools/prep_dreadnought.py), each two bays opening outboard; its eight bays hold the game's four fighters, two of each
// (jet_internal.h Body::dnFighter .. dnInterceptor: crewless NPC jets, playerjet_kinds.h kBoardable; bay i holds kind
// i % 4). A fighter in its bay is the ship's own model (the bone dn_bay_<i>, its fighter's mesh, nose outboard: a live jet
// cannot be in the hull's collision); it goes out and comes in through its bay's opening:
//  - out: the bay's fighter slides out to its opening's mouth (dn_bay_<i>_out, clear of the hull) over kSlideSec; there
//    the bone is hidden (scaled to nothing) and the live jet made in its very place and motion, held by the hangar
//    (jet.cpp JetHangarHold: its motion is the hangar's, whoever sits in it) until it is off: an escort thrown on outboard
//    and up (kLaunchSec, to kLaunchSpeed over the ship's) and let go (jet.cpp JetHangarRelease), the ready fighter flown
//    up onto its foredeck (dn_ready) and parked there;
//  - in: one on its way out (its fuel, hull or stores gone: JetLeaving) or called home is flown by the hangar to its own
//    bay's mouth (Recover: kApproachOut outboard of it first, turned nose outboard there as its bay holds it, backing in),
//    held there until the game has deleted it (JetHangarStow), and its bay's fighter shown at the mouth and slid back in.
//    A fighter lost leaves its bay empty.
//  - Escort flight: the commander's hangar key (DreadnoughtHangarKey / DreadnoughtHangarButton) on its bridge sends one of
//    each kind it has out, one after another; pressed with its escorts out, it calls them home.
//  - The ready fighter: while it has fighters and nothing is parked on its foredeck, the next kind in turn
//    (Hangar::nextKind) comes out and parks there (dn_ready). A player who walks up and boards it (playerjet_board.inc
//    BoardableNow: a held one is boarded where it stands) is launched off the bow with it (kDeckRunSec, to kLaunchSpeed),
//    flying it from there (playerjet.cpp PlayerJetLaunched).
//  - Home for a player: their fighter's hangar key within kReturnRange: the hangar flies it onto the foredeck (the ready
//    spot, free: nothing else is parked there while a player's fighter is out of it), puts them out onto its bridge
//    (dn_bridge, as getting out of its seats does: the user, 2026-10-21 「下船应在舰桥」), and parks it there as the
//    ready fighter again. Only a pilot getting out on that spot goes without a parachute (DreadnoughtTakesBack).
//  - The bridge: a local player getting out of any of its seats stands on its bridge (dn_bridge), not at the door on the
//    ground.
// Online: the plugin's aircraft are each machine's own (docs/online-re.md, "插件飞机 / 无人机的网络身份：没做"): the
// hangars run on the machine that flies the ship (their fighters are that machine's); the bays' poses on the others stay
// docked; the bridge on every machine for its own player.
constexpr int kBays=8;
constexpr float kSlideSec=2.5f;              // s a fighter slides between its bay and its mouth
constexpr float kLaunchSec=2.0f,kLaunchSpeed=90.0f;   // an escort thrown off its mouth: s, m/s over the ship's
constexpr float kLaunchClimb=12.0f;          // ...m/s up at the end of it
constexpr float kDeckRunSec=1.6f;            // s the ready fighter's run off the bow
constexpr float kReturnRange=1500.0f;        // m from its ship a player's fighter may be called home
// The distances round its hull as its fighters' size goes (set on a 200 m ship, its fighters 7-12 m: kSize 1).
constexpr float kSize=kLength/200.0f;
constexpr float kApproachOut=60.0f*kSize,kApproachUp=8.0f*kSize;   // m outboard of its mouth and over it a fighter comes in from
constexpr float kDeckOver=25.0f*kSize;       // m over the foredeck the ready fighter is brought in from
constexpr float kSideClear=kBeam*1.15f+18.0f*kSize;   // m from its centre line a fighter is clear of its hull (half its
                                             // width 1.15 kBeam, a fighter's wing and a margin)
constexpr float kApproachSpeed=70.0f,kFinalSpeed=12.0f;  // m/s over the ship's: closing, the last stretch
constexpr float kFarSpeed=160.0f,kFarOff=500.0f;   // ...and from farther out than kFarOff (escorts 1.5-2 km out took 30 s)
constexpr float kRecoverAccel=22.0f;         // m/s^2 a held fighter's velocity changes at most
constexpr float kTouch=1.5f;                 // m from its spot (and as slow, kFinalSpeed / 3): there
constexpr float kLined=6.0f*kSize;           // m off its mouth's line outboard it may be to come straight in
constexpr float kHoldGainJet=1.2f,kHoldMostJet=1.5f;   // a held fighter's attitude (BodyAttitude): 1/s, rad/s
constexpr float kPinGain=8.0f;               // 1/s a parked fighter is pulled onto its spot
constexpr ULONGLONG kEscortGapMs=1200;       // ms between an escort flight's fighters going out
constexpr ULONGLONG kReadyGapMs=4000;        // ms after the foredeck is cleared the next ready fighter comes out
// A fighter's origin (its SGO box's centre, heli_rigid_body[0][1] at a 500 m ship) over the surface it stands on, by kind
// (tools/prep_dreadnought.py).
constexpr float kOverDeck[kDnFighterKinds]={2.93f,2.90f,5.15f,5.47f};
constexpr std::size_t kHumanVehicle=0x1548;  // the vehicle a human rides (weak_ptr object)
const char* const kKindNames[kDnFighterKinds]={"fighter","defender","bomber","interceptor"};

// What a bay's fighter is doing.
enum class BayJob { docked, out, launch, toDeck, away, recover, deleting, in, empty };
struct Bay {
    unsigned char* rec=nullptr;   // dn_bay_<i>: its fighter's mesh
    unsigned char* mouth=nullptr; // dn_bay_<i>_out
    float bind[16]{},mouthBind[16]{};
    BayJob job=BayJob::docked;
    float t=0.0f;                 // 0 in its bay .. 1 at its mouth
    ObjRef jet;                   // its live fighter (out of its bay)
    ULONGLONG at=0;
    bool ready=false;             // out to be the ready fighter
    bool low=false;               // brought onto its foredeck: over its spot, coming down
};
struct Hangar {
    const void* bones=nullptr;
    bool ok=false,said=false,keyHeld=false;
    Bay bay[kBays]{};
    unsigned char* deck=nullptr;  // dn_ready
    unsigned char* deckSide=nullptr;   // dn_bridge: where a pilot put out of the foredeck's fighter stands
    ObjRef parked;                // the fighter on its foredeck (the ready one, or a player's brought home)
    int parkedBay=-1;             // ...its bay
    const void* run=nullptr;      // ...boarded: its run off the bow began at runAt
    ULONGLONG runAt=0;
    ObjRef home;                  // a player's fighter being brought onto the foredeck
    int homeBay=-1;
    bool homeLow=false;           // ...over its spot: coming down
    ObjRef flown;                 // a player's fighter off its foredeck (none comes out to it while this flies)
    int flownBay=-1;
    ULONGLONG clearedAt=0;        // when its foredeck was last cleared
    int queue=0;                  // escort kinds still to go out (bits, one per kind)
    ULONGLONG queueAt=0;
    bool recall=false;            // its escorts called home
    int nextKind=0;               // the ready fighter's kind next
    bool failed[kDnFighterKinds]{};   // a kind its bays could not make this mission (not preloaded, too many jets)
    bool authority=false;         // this machine runs it
    ULONGLONG loggedAt=0;
};
Hangar hangars[kMaxShips]{};
Hangar& HangarOf(const Ship& s) noexcept { return hangars[&s-ships]; }

// Where the local player is put (see the top): the frame after they got out of one of its seats, on its bridge; put out
// beside its foredeck's fighter, there. (Off from the first range runs until 2026-10-21 evening: a soldier put on its
// deck fell through it, the ship hovering under a player's flight; standing on its deck held in the user's own play.)
bool kStandsOnHull=true;   // (a variable: the code under it kept compiled and checked)
constexpr float kPutOver=1.5f;   // m over a stand marker a soldier is put (WatchPlayer, PutOut): onto its cell, not in it
struct Bridge { bool pending=false; ObjRef ship; float at[3]{}; ULONGLONG putAt=0; int tries=0; } bridge{};

bool IsDnFighter(const void* v) noexcept {
    if(!v || BodyOf(v)!=PluginBody::jet)return false;
    const float k=BodyMark(v);
    return k>=kDnFighterMark && k<kDnFighterMark+static_cast<float>(kDnFighterKinds);
}
int KindOfBay(int i) noexcept { return i%kDnFighterKinds; }

bool RigHangar(Hangar& h,unsigned char* v) noexcept {
    const unsigned char* inst=v+kModelInst506;
    const void* bones=At<const void*>(inst,kInstBones506);
    if(bones==h.bones && h.ok)return true;
    h.bones=bones;h.ok=false;
    const auto need=[&](const wchar_t* name) -> unsigned char* {
        unsigned char* const rec=BoneRecord506(inst,name);
        if(!rec && !h.said){Log("DREADNOUGHT v=%p: its model lacks bone %ls: no hangar",v,name);h.said=true;}
        return rec;
    };
    for(int i=0;i<kBays;++i) {
        wchar_t name[32];
        swprintf_s(name,L"dn_bay_%d",i);
        Bay& b=h.bay[i];
        if((b.rec=need(name))==nullptr)return false;
        swprintf_s(name,L"dn_bay_%d_out",i);
        if((b.mouth=need(name))==nullptr)return false;
        std::memcpy(b.bind,b.rec+kBoneLocal506,64);
        std::memcpy(b.mouthBind,b.mouth+kBoneLocal506,64);
    }
    if((h.deck=need(L"dn_ready"))==nullptr || (h.deckSide=need(L"dn_bridge"))==nullptr)return false;
    h.ok=true;
    return true;
}

// A bay's fighter where its job has it: from its bay toward its mouth (`t`), shown or hidden (scaled to nothing).
void PoseBay(Bay& b) noexcept {
    float m[16];
    std::memcpy(m,b.bind,64);
    for(int i=12;i<15;++i)m[i]=b.bind[i]+(b.mouthBind[i]-b.bind[i])*b.t;
    const bool shown=b.job==BayJob::docked || b.job==BayJob::out || b.job==BayJob::in;
    if(!shown)for(int i=0;i<12;++i)m[i]=0.0f;
    std::memcpy(b.rec+kBoneLocal506,m,64);
}
// The way from its bay to its mouth (m).
float SlideLength(const Bay& b) noexcept {
    const float d[3]={b.mouthBind[12]-b.bind[12],b.mouthBind[13]-b.bind[13],b.mouthBind[14]-b.bind[14]};
    return vec::Len(d);
}

// The ship's axes (its matrix's rows: left, up, ahead).
const float* Axis3(const unsigned char* v,int row) noexcept { return reinterpret_cast<const float*>(v+kMatrix)+4*row; }

// The jet `r` refers to, live, else null.
unsigned char* JetOf(const ObjRef& r) noexcept {
    auto* const f=static_cast<unsigned char*>(const_cast<void*>(r.obj));
    return r && r.Is(f) && !f[kDead] ? f : nullptr;
}
bool Flown(unsigned char* f) noexcept { return f && SeatCount(f)>0 && SeatRider(SeatAt(f,0))==Rider::player; }

int Stowed(const Hangar& h) noexcept { int n=0;for(const auto& b:h.bay)n+=b.job==BayJob::docked ? 1 : 0;return n; }
// Its escorts out (launched, away, coming home): not its foredeck's fighter, nor a player's off it or brought home.
bool Escort(const Hangar& h,const Bay& b) noexcept {
    return (b.job==BayJob::away || b.job==BayJob::launch || b.job==BayJob::recover) && !b.jet.Is(h.parked.obj) &&
           !b.jet.Is(h.flown.obj) && !b.jet.Is(h.home.obj);
}
int Out(const Hangar& h) noexcept { int n=0;for(const auto& b:h.bay)n+=Escort(h,b) ? 1 : 0;return n; }

// Fighter `f` held this frame: velocity `vel`, its nose and up turned to `nose` / `up`.
void HoldJet(unsigned char* f,const float* vel,const float* nose,const float* up) noexcept {
    float omega[3];
    BodyAttitude(f,nose,up,kHoldGainJet,kHoldMostJet,omega);
    JetHangarHold(f,vel,omega);
}
// ...flown toward `to` (moving with the ship at `ship`): its velocity the ship's plus the closing (what stops it there,
// at most `most` m/s), changing at kRecoverAccel at most. Its distance to `to`.
float FlyTo(unsigned char* f,const float* to,const float* ship,const float* nose,const float* up,float most,float dt) noexcept {
    const float* p=reinterpret_cast<const float*>(f+kPosition);
    const float d[3]={to[0]-p[0],to[1]-p[1],to[2]-p[2]};
    const float dist=vec::Len(d);
    const float closing=std::fmin(most,std::sqrt(kRecoverAccel*dist));
    float vel[3],want[3];
    if(!JetMotion(f,vel))std::memcpy(vel,ship,12);
    for(int i=0;i<3;++i)want[i]=ship[i]+(dist>1e-3f ? d[i]/dist*closing : 0.0f);
    Approach(vel,want,kRecoverAccel,dt);
    HoldJet(f,vel,nose,up);
    return dist;
}
// ...held on `at` with the ship.
void Pin(unsigned char* f,const float* at,const float* ship,const float* nose,const float* up) noexcept {
    const float* p=reinterpret_cast<const float*>(f+kPosition);
    float vel[3];
    for(int i=0;i<3;++i)vel[i]=ship[i]+(at[i]-p[i])*kPinGain;
    HoldJet(f,vel,nose,up);
}
// Its velocity relative to the ship's.
float Relative(unsigned char* f,const float* ship) noexcept {
    float vel[3];
    if(!JetMotion(f,vel))return 1.0e9f;
    const float d[3]={vel[0]-ship[0],vel[1]-ship[1],vel[2]-ship[2]};
    return vec::Len(d);
}
// A level heading along `d` (else the ship's), into `out`.
void Level(const unsigned char* v,const float* d,float* out) noexcept {
    out[0]=d[0];out[1]=0.0f;out[2]=d[2];
    if(!vec::Normalize(out)){const float* n=Axis3(v,2);out[0]=n[0];out[1]=0.0f;out[2]=n[2];vec::Normalize(out);}
}

// The ready spot for kind `kind` (its origin over the foredeck), world.
void DeckSpot(const unsigned char* v,const Hangar& h,int kind,float* at) noexcept {
    const float* p=BoneAt(h.deck);
    const float* up=Axis3(v,1);
    for(int i=0;i<3;++i)at[i]=p[i]+up[i]*kOverDeck[kind];
}

// Fighter `f` brought onto its foredeck's spot `spot`: under the spot's height and beside the hull (within kSideClear of
// its centre line), out to kSideClear first; then up; then over the spot; then down onto it (`low`). Up beside the hull
// at once, a ready fighter out of its bay rose into the hull's overhang and, held, pushed the whole ship down 15 m
// (range 2026-10-21). Its distance to where it is going.
float ToDeck(unsigned char* f,const unsigned char* v,const float* spot,const float* ship,bool& low,float dt) noexcept {
    const float* up=Axis3(v,1);
    const float* nose=Axis3(v,2);
    const float wup[3]={0.0f,1.0f,0.0f};
    const float* p=reinterpret_cast<const float*>(f+kPosition);
    float over[3];
    for(int c=0;c<3;++c)over[c]=spot[c]+up[c]*kDeckOver;
    const float to[3]={over[0]-p[0],over[1]-p[1],over[2]-p[2]};
    if(vec::Len(to)<8.0f)low=true;
    float face[3];
    if(low) {
        Level(v,nose,face);
        return FlyTo(f,spot,ship,face,up,kFinalSpeed,dt);
    }
    const float rise=to[0]*up[0]+to[1]*up[1]+to[2]*up[2];   // how far under the spot's height over the deck it is
    const float* c0=reinterpret_cast<const float*>(v+kPosition);
    const float* left=Axis3(v,0);
    const float rel[3]={p[0]-c0[0],p[1]-c0[1],p[2]-c0[2]};
    const float side=rel[0]*left[0]+rel[1]*left[1]+rel[2]*left[2];   // left of its centre line
    float aim[3];
    if(rise>3.0f && std::fabs(side)<kSideClear-2.0f)   // out from the hull first, at its height
        for(int c=0;c<3;++c)aim[c]=p[c]+left[c]*((side<0.0f ? -kSideClear : kSideClear)-side);
    else if(rise>3.0f)for(int c=0;c<3;++c)aim[c]=p[c]+up[c]*rise;   // then up, where it is
    else std::memcpy(aim,over,12);
    const float flat[3]={over[0]-p[0],0.0f,over[2]-p[2]};
    Level(v,vec::Len(flat)>40.0f ? flat : nose,face);
    FlyTo(f,aim,ship,face,wup,kApproachSpeed,dt);
    return vec::Len(to);
}

// Bay `i`'s fighter out of its bay: it slides to its mouth (BayFrame), then flies.
bool SendOut(Hangar& h,unsigned char* v,int i,bool ready) noexcept {
    Bay& b=h.bay[i];
    if(b.job!=BayJob::docked)return false;
    b.job=BayJob::out;b.at=GameMs();b.ready=ready;
    Log("DREADNOUGHT v=%p hangar: bay %d's %s out (%s), %d docked",v,i,kKindNames[KindOfBay(i)],ready ? "to its foredeck" : "an escort",
        Stowed(h)-1);
    return true;
}

// The pilot of `f` (a player in its seat 0) put out: onto its bridge (this machine's own player).
void PutOut(Hangar& h,unsigned char* v,unsigned char* f) noexcept {
    if(!Flown(f))return;
    unsigned char* const me=PlayerHuman();
    const bool mine=me && At<const void*>(SeatAt(f,0),kSeatRider)==me;
    reinterpret_cast<void(__fastcall*)(void*,void*)>(image+kSeatKick)(f,SeatAt(f,0));
    if(!kStandsOnHull)return;
    if(!mine)return;
    const float* side=BoneAt(h.deckSide);
    for(int i=0;i<3;++i)bridge.at[i]=side[i]+Axis3(v,1)[i]*kPutOver;
    bridge.putAt=GameMs();bridge.ship=ObjRef::Of(v);bridge.pending=true;bridge.tries=0;
    Log("DREADNOUGHT v=%p hangar: its pilot put out onto its bridge",v);
}

// One bay's frame (its ship `v` moving at `ship`).
void BayFrame(Hangar& h,int i,unsigned char* v,const float* ship,float dt,ULONGLONG ms) noexcept {
    Bay& b=h.bay[i];
    const int kind=KindOfBay(i);
    unsigned char* const f=JetOf(b.jet);
    const float* const mouth=BoneAt(b.mouth);
    const float* const up=Axis3(v,1);
    const float* const outboard=reinterpret_cast<const float*>(b.mouth+kBoneWorld506)+8;   // the mouth's ahead: outboard
    const float wup[3]={0.0f,1.0f,0.0f};
    const float slide=dt/kSlideSec;
    switch(b.job) {
        case BayJob::docked:case BayJob::empty:
            break;
        case BayJob::out:   // sliding out; at its mouth the live fighter takes its place, in its motion
            b.t=std::fmin(1.0f,b.t+slide);
            if(b.t<1.0f)break;
            {
                unsigned char* const made=JetLaunchDreadnoughtFighter(kind,mouth,outboard,reinterpret_cast<const float*>(v+kPosition),
                                                                     Cfg().jetFuelSec,v,true);
                if(!made) {
                    Log("DREADNOUGHT v=%p hangar: no %s made at bay %d's mouth (not preloaded this mission, or too many jets): back in, "
                        "not sent out again this mission",v,kKindNames[kind],i);
                    h.failed[kind]=true;h.clearedAt=ms;
                    b.job=BayJob::in;break;
                }
                b.jet=ObjRef::Of(made);b.job=b.ready ? BayJob::toDeck : BayJob::launch;b.at=ms;
                const float sp=SlideLength(b)/kSlideSec;
                float vel[3];
                for(int c=0;c<3;++c)vel[c]=ship[c]+outboard[c]*sp;
                HoldJet(made,vel,outboard,up);
            }
            break;
        case BayJob::launch: {   // thrown outboard and up, then let go: an escort
            if(!f){b.job=BayJob::empty;break;}
            if(Flown(f)){JetHangarRelease(f);b.job=BayJob::away;break;}   // a player took it (the boarding gun): theirs
            const float k=Clamp(static_cast<float>(ms-b.at)/1000.0f/kLaunchSec,0.0f,1.0f);
            float flat[3];
            Level(v,outboard,flat);
            float vel[3];
            for(int c=0;c<3;++c)vel[c]=ship[c]+flat[c]*kLaunchSpeed*k+(c==1 ? kLaunchClimb*k : 0.0f);
            HoldJet(f,vel,flat,wup);
            if(k>=1.0f) {
                JetHangarRelease(f);
                b.job=BayJob::away;
                Log("DREADNOUGHT v=%p hangar: bay %d's %s away",v,i,kKindNames[kind]);
            }
            break;
        }
        case BayJob::toDeck: {   // up over the foredeck, then down onto its spot, parked
            if(!f){b.job=BayJob::empty;break;}
            if(Flown(f)){JetHangarRelease(f);b.job=BayJob::away;b.low=false;break;}   // a player took it on the way
            float spot[3];
            DeckSpot(v,h,kind,spot);
            const float d=ToDeck(f,v,spot,ship,b.low,dt);
            if(b.low && d<kTouch && Relative(f,ship)<kFinalSpeed/3.0f) {
                b.job=BayJob::away;b.low=false;h.parked=b.jet;h.parkedBay=i;
                Log("DREADNOUGHT v=%p hangar: bay %d's %s parked on its foredeck",v,i,kKindNames[kind]);
            }
            break;
        }
        case BayJob::away:   // out of its bay (flying, parked, a player's): its live fighter's own
            if(!f){b.job=BayJob::empty;Log("DREADNOUGHT v=%p hangar: bay %d's %s lost",v,i,kKindNames[kind]);}
            break;
        case BayJob::recover: {   // to its mouth: outboard of it first, then backing straight in, nose outboard
            if(!f){b.job=BayJob::empty;break;}
            if(Flown(f)){JetHangarRelease(f);b.job=BayJob::away;break;}   // a player took it (the boarding gun): theirs
            const float* p=reinterpret_cast<const float*>(f+kPosition);
            const float* c0=reinterpret_cast<const float*>(v+kPosition);
            const float to[3]={mouth[0]-p[0],mouth[1]-p[1],mouth[2]-p[2]};
            const float rise=to[0]*up[0]+to[1]*up[1]+to[2]*up[2];
            const float flat[3]={to[0]-up[0]*rise,to[1]-up[1]*rise,to[2]-up[2]*rise};   // across the ship's up
            const float out=-(flat[0]*outboard[0]+flat[1]*outboard[1]+flat[2]*outboard[2]);   // how far outboard of its mouth
            const float across=std::sqrt(std::fmax(0.0f,vec::Len(flat)*vec::Len(flat)-out*out));
            const bool lined=out>-1.0f && across<kLined;
            const float high=(p[0]-c0[0])*up[0]+(p[1]-c0[1])*up[1]+(p[2]-c0[2])*up[2];   // over the ship's centre
            const float clearTop=kKeel+15.0f;   // over its top
            float aim[3];
            if(out<10.0f && !lined) {   // inboard of its mouth (over the hull, or the far side): up, then over to its side
                if(high<clearTop-3.0f)for(int c=0;c<3;++c)aim[c]=p[c]+up[c]*(clearTop-high);
                else for(int c=0;c<3;++c)aim[c]=c0[c]+up[c]*clearTop+outboard[c]*(kBeam+kApproachOut);
            } else for(int c=0;c<3;++c)aim[c]=lined ? mouth[c] : mouth[c]+outboard[c]*kApproachOut+up[c]*kApproachUp;
            float face[3];
            const bool close=vec::Len(to)<kApproachOut*1.5f && out>=10.0f;
            if(close)std::memcpy(face,outboard,12);
            else{const float d[3]={aim[0]-p[0],0.0f,aim[2]-p[2]};Level(v,d,face);}
            const float d=FlyTo(f,aim,ship,face,close ? up : wup,lined ? kFinalSpeed : vec::Len(to)>kFarOff ? kFarSpeed : kApproachSpeed,dt);
            if(lined && d<kTouch && Relative(f,ship)<kFinalSpeed/3.0f) {
                JetHangarStow(f);
                b.job=BayJob::deleting;b.at=ms;
                Log("DREADNOUGHT v=%p hangar: bay %d's %s at its mouth: in",v,i,kKindNames[kind]);
            }
            break;
        }
        case BayJob::deleting:   // held at its mouth until the game has deleted it, then the bay's own shown there
            if(f){Pin(f,mouth,ship,outboard,up);break;}
            b.jet=ObjRef{};b.t=1.0f;b.job=BayJob::in;
            break;
        case BayJob::in:   // sliding back in
            b.t=std::fmax(0.0f,b.t-slide);
            if(b.t<=0.0f){b.job=BayJob::docked;Log("DREADNOUGHT v=%p hangar: bay %d's %s docked (%d docked)",v,i,kKindNames[kind],Stowed(h));}
            break;
    }
    PoseBay(b);
}

// Bay `i`'s fighter (flying) called home to its bay.
void CallHome(Hangar& h,unsigned char* v,int i) noexcept {
    Bay& b=h.bay[i];
    if(b.job!=BayJob::away)return;
    b.job=BayJob::recover;b.at=GameMs();
    Log("DREADNOUGHT v=%p hangar: bay %d's %s called home",v,i,kKindNames[KindOfBay(i)]);
}

// Its foredeck's fighter (parked, a player's brought home, a player's off it).
void DeckFrame(Hangar& h,unsigned char* v,const float* ship,float dt,ULONGLONG ms) noexcept {
    const float* nose=Axis3(v,2);
    const float* up=Axis3(v,1);
    const float wup[3]={0.0f,1.0f,0.0f};
    if(unsigned char* const f=JetOf(h.parked)) {
        float spot[3];
        DeckSpot(v,h,KindOfBay(h.parkedBay),spot);
        if(Flown(f)) {   // boarded: its run off the bow
            if(h.run!=f){h.run=f;h.runAt=ms;Log("DREADNOUGHT v=%p hangar: a player boards its foredeck's %s: off the bow",v,kKindNames[KindOfBay(h.parkedBay)]);}
            const float k=Clamp(static_cast<float>(ms-h.runAt)/1000.0f/kDeckRunSec,0.0f,1.0f);
            float flat[3];
            Level(v,nose,flat);
            float vel[3];
            for(int c=0;c<3;++c)vel[c]=ship[c]+flat[c]*kLaunchSpeed*k+(c==1 ? kLaunchClimb*k*k : 0.0f);
            HoldJet(f,vel,flat,wup);
            if(k>=1.0f) {
                JetHangarRelease(f);
                PlayerJetLaunched(f);
                h.flown=h.parked;h.flownBay=h.parkedBay;h.parked=ObjRef{};h.parkedBay=-1;h.clearedAt=ms;h.run=nullptr;
            }
        } else{h.run=nullptr;Pin(f,spot,ship,nose,up);}
    } else if(h.parked){h.parked=ObjRef{};h.parkedBay=-1;h.clearedAt=ms;h.run=nullptr;}   // lost on its deck
    if(unsigned char* const f=JetOf(h.home)) {   // a player's fighter brought onto its foredeck: over it, then down
        float spot[3];
        DeckSpot(v,h,KindOfBay(h.homeBay),spot);
        const float d=ToDeck(f,v,spot,ship,h.homeLow,dt);
        if(h.homeLow && d<kTouch && Relative(f,ship)<kFinalSpeed/3.0f) {
            PutOut(h,v,f);
            h.parked=h.home;h.parkedBay=h.homeBay;h.home=ObjRef{};h.homeBay=-1;h.homeLow=false;
            Log("DREADNOUGHT v=%p hangar: a player's fighter down on its foredeck",v);
        }
    } else if(h.home){h.home=ObjRef{};h.homeBay=-1;h.homeLow=false;h.clearedAt=ms;}
    if(h.flown && !JetOf(h.flown)){h.flown=ObjRef{};h.flownBay=-1;h.clearedAt=ms;}
}

// Every frame on the machine that flies it: its bays, its escort flight going out, its fighters coming home, its
// foredeck. On the others its bays as they are (docked).
void HangarFrame(Ship& s,unsigned char* v,float dt,ULONGLONG ms) noexcept {
    Hangar& h=HangarOf(s);
    h.authority=!drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)) || IsOnlineAuthority(v);
    if(!RigHangar(h,v))return;
    if(!h.authority){for(auto& b:h.bay)PoseBay(b);return;}
    const float* const ship=s.measured;
    // the escort flight: one kind a kEscortGapMs, from the first bay of it docked
    if(h.queue && ms-h.queueAt>=kEscortGapMs) {
        for(int k=0;k<kDnFighterKinds && h.queue;++k) {
            if(!(h.queue&(1<<k)))continue;
            h.queue&=~(1<<k);
            bool sent=false;
            for(int i=k;i<kBays && !sent;i+=kDnFighterKinds)sent=SendOut(h,v,i,false);
            if(sent){h.queueAt=ms;break;}
        }
    }
    // its escorts on their way out, or called home: to their bays
    for(int i=0;i<kBays;++i) {
        Bay& b=h.bay[i];
        unsigned char* const f=JetOf(b.jet);
        if(b.job!=BayJob::away || !f || Flown(f) || !Escort(h,b))continue;
        if(h.recall || JetLeaving(f))CallHome(h,v,i);
    }
    if(h.recall && !Out(h))h.recall=false;
    // the ready fighter: its foredeck free, no player's fighter out of it
    if(!h.parked && !h.home && !h.flown && ms-h.clearedAt>=kReadyGapMs) {
        bool busy=false;
        for(const auto& b:h.bay)busy=busy || (b.job==BayJob::out && b.ready) || b.job==BayJob::toDeck;
        for(int n=0;n<kDnFighterKinds && !busy;++n) {
            const int k=(h.nextKind+n)%kDnFighterKinds;
            if(h.failed[k])continue;
            for(int i=k;i<kBays && !busy;i+=kDnFighterKinds)
                if(SendOut(h,v,i,true)){busy=true;h.nextKind=(k+1)%kDnFighterKinds;}
        }
    }
    for(int i=0;i<kBays;++i)BayFrame(h,i,v,ship,dt,ms);
    DeckFrame(h,v,ship,dt,ms);
    if(Cfg().debug && ms-h.loggedAt>=1000) {
        h.loggedAt=ms;
        static const char* const kJobs[]={"docked","out","launch","toDeck","away","recover","deleting","in","empty"};
        const float* c0=reinterpret_cast<const float*>(v+kPosition);
        for(int i=0;i<kBays;++i) {
            const Bay& b=h.bay[i];
            if(b.job==BayJob::docked || b.job==BayJob::empty || b.job==BayJob::away)continue;
            unsigned char* const f=JetOf(b.jet);
            float rel[3]{},local[3]{},vel[3]{};
            if(f){const float* p=reinterpret_cast<const float*>(f+kPosition);for(int c=0;c<3;++c)rel[c]=p[c]-c0[c];ToShip(v,rel,local);JetMotion(f,vel);}
            Log("DREADNOUGHT v=%p bay %d %s t %.2f%s jet at ship (%.1f,%.1f,%.1f) %.1f m/s over the ship's",v,i,kJobs[static_cast<int>(b.job)],
                b.t,b.low ? " low" : "",local[0],local[1],local[2],f ? Relative(f,ship) : 0.0f);
            (void)vel;
        }
    }
}

// The commander's hangar key: an escort flight out (one of each kind it has docked), or, its escorts out, home.
void HangarKey(Ship& s,unsigned char* v,const unsigned char* seat) noexcept {
    Hangar& h=HangarOf(s);
    const bool down=Pad(seat) ? Button(seat,Cfg().dreadnoughtHangarButton) : KeyDown(Cfg().dreadnoughtHangarKey);
    const bool pressed=down && !h.keyHeld;
    h.keyHeld=down;
    if(!pressed || !h.authority)return;
    const int escorts=Out(h);
    if(escorts>0 && !h.queue) {
        h.recall=true;
        Log("DREADNOUGHT v=%p hangar: its %d escorts called home",v,escorts);
        return;
    }
    h.queue=0;h.recall=false;
    for(int i=0;i<kBays;++i)if(h.bay[i].job==BayJob::docked && !h.failed[KindOfBay(i)])h.queue|=1<<KindOfBay(i);
    h.queueAt=0;
    Log("DREADNOUGHT v=%p hangar: an escort flight asked (%d docked)",v,Stowed(h));
}

// The hangar's part of the panel (DreadnoughtCue).
void HangarCue(const Hangar& h,DreadnoughtCue& c) noexcept {
    c.stowed=Stowed(h);
    c.out=Out(h);
    c.lifting=0;
    for(const auto& b:h.bay)c.lifting+=b.job==BayJob::out || b.job==BayJob::in || b.job==BayJob::recover || b.job==BayJob::deleting ? 1 : 0;
    c.flightWait=0.0f;
}

// The ship whose hull `at` is over or in (within its box), or null.
Ship* DeckUnder(const float* at) noexcept {
    for(auto& s:ships) {
        if(!s.vehicle || !Live(s))continue;
        const float* p=reinterpret_cast<const float*>(s.vehicle+kPosition);
        const float d[3]={at[0]-p[0],at[1]-p[1],at[2]-p[2]};
        float local[3];
        ToShip(s.vehicle,d,local);
        if(std::fabs(local[0])<kBeam*1.2f && std::fabs(local[2])<kReach*1.2f && local[1]>-kKeel && local[1]<kKeel*1.5f)return &s;
    }
    return nullptr;
}

const void* riddenLast=nullptr;   // the vehicle the local player rode last frame (WatchPlayer)
const void* riddenCtrl=nullptr;
ULONGLONG watchedFrame=0;
bool homeKeyHeld=false;
ULONGLONG deckLoggedAt=0;
ULONGLONG lastPutAt=0;               // the last put onto its deck (debug: its deck logged finer for kPutLogMs after)
int standFrames=0;                   // debug: stand reports since that put (StandReport)
constexpr ULONGLONG kPutLogMs=10000;
constexpr ULONGLONG kPutMs=1500;   // ms a put is tried for (the get-off's own placing may come a frame late)
constexpr int kPutTries=3;

void ResetBridge() noexcept { bridge=Bridge{};riddenLast=riddenCtrl=nullptr;watchedFrame=0;homeKeyHeld=false;lastPutAt=0;standFrames=0; }

// Where its bridge stands (world), false: its model has no dn_bridge.
bool BridgeAt(unsigned char* v,float* at) noexcept {
    unsigned char* const rec=BoneRecord506(v+kModelInst506,L"dn_bridge");
    if(!rec)return false;
    std::memcpy(at,BoneAt(rec),12);
    return true;
}

// A local player's fighter `f`: its hangar key calls it home onto the foredeck of the ship it came off (within
// kReturnRange).
void CallPlayerHome(unsigned char* f) noexcept {
    const float* p=reinterpret_cast<const float*>(f+kPosition);
    for(auto& s:ships) {
        if(!s.vehicle || !Live(s))continue;
        Hangar& h=HangarOf(s);
        if(!h.authority || !h.flown.Is(f) || h.home)continue;
        const float* q=reinterpret_cast<const float*>(s.vehicle+kPosition);
        const float d[3]={p[0]-q[0],p[1]-q[1],p[2]-q[2]};
        if(vec::Len(d)>kReturnRange){Log("DREADNOUGHT v=%p hangar: its pilot calls home %.0f m out (%.0f at most)",s.vehicle,vec::Len(d),kReturnRange);return;}
        h.home=h.flown;h.homeBay=h.flownBay;h.flown=ObjRef{};h.flownBay=-1;h.homeLow=false;
        Log("DREADNOUGHT v=%p hangar: its pilot calls home, %.0f m out: onto its foredeck",s.vehicle,vec::Len(d));
        return;
    }
}

// Debug, for kPutLogMs after a put onto its deck: what its hull's bodies and the soldier's walking body are (does the
// soldier fall through its hull because a part's state is off, or because no contact is made: tmp report walk-re.md
// section 5). Read only. The world's body record of a wrapper as 0x11B1240 finds it: [[w+0x100]+0x58]+0x20, its vtable
// slot 0x80 with the wrapper's id (+0xF0).
constexpr unsigned kBodyInfo=0x11B1240,kBodyInWorld=0x17E720;
const unsigned char kBodyInfoSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x8B,0x91,0xF0,0x00,0x00,0x00,
                                    0x48,0x8B,0x48,0x58,0x48,0x8B,0x41,0x20,0x48,0x83,0xC1,0x20,0xFF,0x90,0x80,0x00,0x00,0x00};
const unsigned char kBodyInWorldSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x48,0x8B,0xD9};
const unsigned char* WorldBody(const unsigned char* wrapper,std::uint32_t id) noexcept {
    auto* const w=At<unsigned char*>(At<unsigned char*>(wrapper,0x100),0x58)+0x20;
    using BodyFn=const unsigned char*(__fastcall*)(void*,std::uint32_t);
    return reinterpret_cast<BodyFn>((*reinterpret_cast<void***>(w))[0x80/8])(w,id);
}
void StandReport(const unsigned char* v,const unsigned char* me,bool parts) noexcept {
    if(std::memcmp(image+kBodyInfo,kBodyInfoSig,sizeof(kBodyInfoSig))!=0 ||
       std::memcmp(image+kBodyInWorld,kBodyInWorldSig,sizeof(kBodyInWorldSig))!=0){Log("DREADNOUGHT stand report: code unexpected");return;}
    __try {
        const unsigned char* const flight=At<const unsigned char*>(v,kBody);
        const void* const hull=flight ? At<const void*>(WorldBody(flight,At<std::uint32_t>(flight,0xF0)),0x78) : nullptr;
        if(parts) {
            const auto count=At<std::size_t>(v,0x13A8);
            const auto* const base=At<const unsigned char*>(v,0x1398);
            Log("DREADNOUGHT v=%p stand report: flight body id %u, %zu parts, hull shape %p",v,flight ? At<std::uint32_t>(flight,0xF0) : 0u,count,hull);
            for(std::size_t i=0;i<count && i<16 && base;++i) {
                const unsigned char* const p=base+i*0xC0;
                const unsigned char* const b=At<const unsigned char*>(p,0x50);
                if(!b)continue;
                const std::uint32_t id=At<std::uint32_t>(b,0xF0);
                const unsigned char* const hb=WorldBody(b,id);
                const bool inWorld=reinterpret_cast<bool(__fastcall*)(const void*)>(image+kBodyInWorld)(b);
                const float* pos=reinterpret_cast<const float*>(hb+0x30);
                Log("DREADNOUGHT   part %zu id %u flags %08X word %04X info %08X world %d shape %p%s mode %d at (%.1f,%.1f,%.1f)",i,id,
                    At<std::uint32_t>(hb,0x54),At<std::uint16_t>(hb,0x58),At<std::uint32_t>(hb,0x8C),inWorld ? 1 : 0,
                    At<const void*>(hb,0x78),At<const void*>(hb,0x78)==hull ? " (the hull)" : "",At<std::int32_t>(p,0x9C),pos[0],pos[1],pos[2]);
            }
        }
        const unsigned char* const walk=me+0x680;
        const unsigned char* const ch=At<const unsigned char*>(walk,0);
        if(!ch){Log("DREADNOUGHT   the soldier: no walking body");return;}
        const std::uint32_t pid=At<std::uint32_t>(ch,0x18);
        const unsigned char* const pw=At<const unsigned char*>(walk,8);
        const unsigned char* const phb=pw ? WorldBody(pw,pid) : nullptr;
        const int n=At<std::int32_t>(ch,0x80);
        char ids[160]{};
        int at=0;
        const unsigned char* const contacts=At<const unsigned char*>(ch,0x78);
        for(int k=0;k<n && k<8 && contacts;++k) {
            const unsigned char* const c=contacts+static_cast<std::size_t>(k)*0x40;
            const float* nrm=reinterpret_cast<const float*>(c+0x10);
            const int w=sprintf_s(ids+at,sizeof(ids)-at,"%s%u(%.2f,%.2f,%.2f)",k ? " " : "",At<std::uint32_t>(c,0x20),nrm[0],nrm[1],nrm[2]);
            if(w>0)at+=w;
        }
        const float* pp=reinterpret_cast<const float*>(me+kPosition);
        Log("DREADNOUGHT   the soldier (%.1f,%.1f,%.1f): body %u flags %08X info %08X, support %u, %d contacts %s",pp[0],pp[1],pp[2],pid,
            phb ? At<std::uint32_t>(phb,0x54) : 0u,phb ? At<std::uint32_t>(phb,0x8C) : 0u,At<std::uint8_t>(walk,0x91),n,ids);
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("DREADNOUGHT stand report faulted");}
}

void WatchPlayer(ULONGLONG ms) noexcept {
    const ULONGLONG frame=GameFrame();
    if(watchedFrame==frame)return;
    watchedFrame=frame;
    unsigned char* const me=PlayerHuman();
    if(!me || me[kDead]){riddenLast=nullptr;bridge=Bridge{};return;}
    const bool onFoot=HumanOnFoot(me);
    const void* const now=onFoot ? nullptr : At<const void*>(me,kHumanVehicle);
    const void* const was=riddenLast;
    const void* const wasCtrl=riddenCtrl;
    riddenLast=now;riddenCtrl=now ? At<const void*>(static_cast<const unsigned char*>(now),kSelfCtrl) : nullptr;
    if(now && IsDnFighter(now)) {   // its pilot's hangar key: home
        auto* const f=static_cast<unsigned char*>(const_cast<void*>(now));
        const unsigned char* const seat=SeatCount(f)>0 ? SeatAt(f,0) : nullptr;
        const bool down=seat && (Pad(seat) ? Button(seat,Cfg().dreadnoughtHangarButton) : KeyDown(Cfg().dreadnoughtHangarKey));
        if(down && !homeKeyHeld)CallPlayerHome(f);
        homeKeyHeld=down;
    }
    // got out of one of its seats: onto its bridge
    if(kStandsOnHull && onFoot && was && ObjRef{was,wasCtrl}.Is(was) && IsDreadnought(was)) {
        auto* const v=static_cast<unsigned char*>(const_cast<void*>(was));
        if(!v[kDead] && BridgeAt(v,bridge.at)) {
            // kPutOver over it: the marker is its roof as drawn, its collision cell's top up to 0.5 m over that (the cells
            // take their highest point); put there the soldier stood inside the cell and slid down its seam, the cells'
            // sides pushing both ways (range 2026-10-21, 0.3 m under the top)
            for(int i=0;i<3;++i)bridge.at[i]+=Axis3(v,1)[i]*kPutOver;
            bridge.pending=true;bridge.ship=ObjRef::Of(v);bridge.putAt=ms;bridge.tries=0;
        }
    }
    // debug: four times a second for kPutLogMs after a put (does the soldier stand, fall through, bounce?), else every second
    // debug: the stand report, every frame for the first ten after a put, then with the deck's log
    const bool reporting=Cfg().debug && onFoot && lastPutAt && ms-lastPutAt<kPutLogMs;
    if(reporting && standFrames<10)
        if(Ship* const s=DeckUnder(reinterpret_cast<const float*>(me+kPosition)))StandReport(s->vehicle,me,standFrames++==0);
    if(Cfg().debug && onFoot && ms-deckLoggedAt>=(ms-lastPutAt<kPutLogMs ? 250u : 1000u)) {
        const float* p=reinterpret_cast<const float*>(me+kPosition);
        if(Ship* const s=DeckUnder(p)) {
            deckLoggedAt=ms;
            const float* c0=reinterpret_cast<const float*>(s->vehicle+kPosition);
            const float d[3]={p[0]-c0[0],p[1]-c0[1],p[2]-c0[2]};
            float local[3];
            ToShip(s->vehicle,d,local);
            Log("DREADNOUGHT v=%p the player on foot at ship (%.1f,%.1f,%.1f), its keel at %.1f",s->vehicle,local[0],local[1],local[2],-kKeel);
            if(reporting)StandReport(s->vehicle,me,false);
        }
    }
    if(!bridge.pending)return;
    if(ms-bridge.putAt>kPutMs || bridge.tries>=kPutTries){bridge=Bridge{};return;}
    if(!onFoot)return;   // put out of a fighter: the get-off still under way
    auto* const v=static_cast<unsigned char*>(const_cast<void*>(bridge.ship.obj));
    if(!bridge.ship.Is(v) || v[kDead]){bridge=Bridge{};return;}
    if(WarpHuman(me,bridge.at)) {
        ++bridge.tries;
        const float* p=reinterpret_cast<const float*>(me+kPosition);
        const float d[3]={p[0]-bridge.at[0],p[1]-bridge.at[1],p[2]-bridge.at[2]};
        Log("DREADNOUGHT v=%p: the player put on its deck at (%.0f,%.0f,%.0f), %.1f m off",v,bridge.at[0],bridge.at[1],bridge.at[2],vec::Len(d));
        lastPutAt=ms;standFrames=0;
        if(vec::Len(d)<3.0f)bridge=Bridge{};
    } else bridge=Bridge{};
}

// The local player's readout aboard `s` (hud.cpp DreadnoughtHud), each frame they are in one of its seats.
void Publish(Ship& s,unsigned char* v) noexcept {
    const int here=LocalSeat(v);
    if(here<0)return;
    const Turrets& r=TurretsOf(s);
    const Hangar& h=HangarOf(s);
    DreadnoughtCue c{};
    c.seat=here;c.turrets=kTurrets;
    const float* vel=s.measured;
    c.speed=std::sqrt(vel[0]*vel[0]+vel[1]*vel[1]+vel[2]*vel[2]);
    c.climb=vel[1];
    c.throttle=s.throttle;c.boost=s.boost;c.boosting=s.boosting;
    c.keel=s.clear==kNoGround ? -1.0f : s.clear;
    const float* f=reinterpret_cast<const float*>(v+kMatrix)+8;
    c.heading=std::atan2(-f[0],f[2])/kDeg;   // the game's compass (sight.h)
    if(c.heading<0.0f)c.heading+=360.0f;
    c.hp=At<float>(v,kHp);c.hpMax=At<float>(v,kHpMax);
    c.mine=r.mine;c.others=r.others;c.commanded=r.commanded;
    for(int n=0;n<kTurrets;++n)if(r.t[n].onAim)c.onAim|=static_cast<std::uint8_t>(1u<<n);
    HangarCue(h,c);
    const unsigned char* const seat=SeatAt(v,static_cast<unsigned>(here));
    c.keys=!Pad(seat);
    AcquireSRWLockShared(&viewLock);c.firstPerson=view.vehicle==v && view.firstPerson;ReleaseSRWLockShared(&viewLock);
    cue=c;
    cueFrame=GameFrame();
}
