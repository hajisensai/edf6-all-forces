// Included by dreadnought.cpp in its file namespace: its hangars' fighters and its bridge (the user, 2026-10-21: 「那艘可以
// 放飞船的无畏舰。飞船可以玩家上去的那种」, then 「无畏舰下船应该在舰桥才对。不是直接变到飞机上。飞机应该往机库停进去和飞
// 出来，不能凭空出现和消失」; 2026-10-24: 「模块能装啥，肯定是装了模块才能有对应的功能啊，比如两个机库模块，就应该两个机库，
// 而不是统一在一个位置」).
// Its hangars are the Dreadnought game's modules on its broadsides' four AB_primary sockets (dreadnought_deck.h: module m,
// its bays 2m and 2m + 1), each fitted or not as the installer built its model (ini DreadnoughtHangarLeftFore ..
// RightAft, pylib/dreadnought_model.py): a hangar is one its model has (the bone dn_hangar_<m>: RigHangar), with its two
// bays, their fighters and openings; a module not fitted has none of them (nor its mesh, nor its collision). Bay i holds
// a fighter of kind i % 4 (jet_internal.h Body::dnFighter .. dnInterceptor: crewless NPC jets, playerjet_kinds.h
// kBoardable). A fighter in its bay is the ship's own model (the bone dn_bay_<i>, its fighter's mesh, nose outboard: a
// live jet cannot be in the hull's collision); it goes out and comes in through its own bay's opening, nowhere else:
//  - out: the bay's fighter slides out to its opening's mouth (dn_bay_<i>_out, clear of the hull) over kSlideSec; there
//    the bone is hidden (scaled to nothing) and the live jet made in its very place and motion, held by the hangar
//    (jet.cpp JetHangarHold: its motion is the hangar's, whoever sits in it) until it is off: thrown on outboard and up
//    (kLaunchSec, to kLaunchSpeed over the ship's) and let go (jet.cpp JetHangarRelease);
//  - in: one on its way out (its fuel, hull or stores gone: JetLeaving) or called home is flown by the hangar to its own
//    bay's mouth (Recover: kApproachOut outboard of it first, turned nose outboard there as its bay holds it, backing in),
//    held there until the game has deleted it (JetHangarStow), and its bay's fighter shown at the mouth and slid back in.
//    A fighter lost leaves its bay empty.
//  - Escort flight: the commander's hangar key (DreadnoughtHangarKey / DreadnoughtHangarButton) on its bridge sends one of
//    each kind its fitted hangars have docked out (dreadnought_deck.h EscortKinds), one after another; pressed with its
//    escorts out, it calls them home.
//  - A player's sortie (the user's choice, 2026-10-24: stand over a hangar and press the key): a local player on foot
//    over a fitted hangar (dreadnought_deck.h OverHangar) presses the board button (crew.cpp FindSeatHook ->
//    DreadnoughtHangarBoard): that hangar's first bay with a fighter docked (BoardBay) slides it out to its mouth, the
//    player is put into it there (boarding.cpp BoardingRequest, the boarding gun's path) and thrown off outboard with it
//    (BayJob::run, as an escort is), flying it from there (playerjet.cpp PlayerJetLaunched).
//  - Home for a player: their fighter's hangar key within kReturnRange: the hangar flies it to its own bay's mouth as it
//    brings an escort in, puts them out onto its bridge there (dn_bridge, as getting out of its seats does: the user,
//    2026-10-21 「下船应在舰桥」) and takes the fighter in. Only a pilot put out at its own bay's mouth by the hangar running
//    here goes without a parachute (BayJob::alight: DreadnoughtTakesBack, dreadnought_deck.h StandsAboard); getting out
//    anywhere else (flying, on the way home, thrown off) they eject.
//  - The bridge: a local player getting out of any of its seats stands on its bridge (dn_bridge), not at the door on the
//    ground.
// Online: the plugin's aircraft are each machine's own (docs/online-re.md, "插件飞机 / 无人机的网络身份：没做"): the
// hangars run on the machine that flies the ship (their fighters are that machine's, a sortie its own player's); the
// bays' poses on the others stay docked; the bridge on every machine for its own player. Each machine draws and collides
// with the modules its own install fitted (nothing of it is sent: it is the install's, not the ship's state).
using dreadnought::kBays;
using dreadnought::kHangars;
static_assert(dreadnought::kKinds==kDnFighterKinds,"dreadnought_deck.h's kinds are crew.h's");
constexpr float kSlideSec=2.5f;              // s a fighter slides between its bay and its mouth
constexpr float kLaunchSec=2.0f,kLaunchSpeed=90.0f;   // a fighter thrown off its mouth: s, m/s over the ship's
constexpr float kLaunchClimb=12.0f;          // ...m/s up at the end of it
constexpr float kReturnRange=1500.0f;        // m from its ship a player's fighter may be called home
// The distances round its hull as its fighters' size goes (set on a 200 m ship, its fighters 7-12 m: kSize 1).
constexpr float kSize=kLength/200.0f;
constexpr float kApproachOut=60.0f*kSize,kApproachUp=8.0f*kSize;   // m outboard of its mouth and over it a fighter comes in from
constexpr float kApproachSpeed=70.0f,kFinalSpeed=12.0f;  // m/s over the ship's: closing, the last stretch
constexpr float kFarSpeed=160.0f,kFarOff=500.0f;   // ...and from farther out than kFarOff (escorts 1.5-2 km out took 30 s)
constexpr float kRecoverAccel=22.0f;         // m/s^2 a held fighter's velocity changes at most
constexpr float kTouch=1.5f;                 // m from its spot (and as slow, kFinalSpeed / 3): there
static_assert(kTouch<=dreadnought::kOnSpot && kFinalSpeed/3.0f<=dreadnought::kOnSpotSpeed,"a touch at its mouth is on its spot");
constexpr float kLined=6.0f*kSize;           // m off its mouth's line outboard it may be to come straight in
constexpr float kHoldGainJet=1.2f,kHoldMostJet=1.5f;   // a held fighter's attitude (BodyAttitude): 1/s, rad/s
constexpr float kPinGain=8.0f;               // 1/s a held fighter is pulled onto its spot
constexpr ULONGLONG kEscortGapMs=1200;       // ms between an escort flight's fighters going out
constexpr ULONGLONG kBoardWaitMs=5000;       // ms a fighter out for a player waits at its mouth for them (boarding.cpp presses
                                             // for 1.5 s a request), then goes back in
constexpr ULONGLONG kAskMs=500;              // ms between its boarding requests for its player
constexpr ULONGLONG kAlightMs=1000;          // ms a fighter that brought its player home stays at its mouth after they are put
                                             // out (their leave, playerjet.cpp Leave / Left, asks DreadnoughtTakesBack then)
constexpr ULONGLONG kPressMs=500;            // ms: board presses over a hangar closer than this are one (held, the button's
                                             // walk visits it each frame)
constexpr std::size_t kHumanVehicle=0x1548;  // the vehicle a human rides (weak_ptr object)
const char* const kKindNames[kDnFighterKinds]={"fighter","defender","bomber","interceptor"};
// dn_hangar_<m>: the sockets left01, left03, right01, right03 (tools/prep_dreadnought.py HANGAR_SOCKETS)
const char* const kHangarNames[kHangars]={"left fore","left aft","right fore","right aft"};

// What a bay's fighter is doing.
enum class BayJob { docked, out, board, launch, run, away, recover, alight, deleting, in, empty };
struct Bay {
    bool fitted=false;            // its hangar module is in the ship's model
    unsigned char* rec=nullptr;   // dn_bay_<i>: its fighter's mesh
    unsigned char* mouth=nullptr; // dn_bay_<i>_out
    float bind[16]{},mouthBind[16]{};
    BayJob job=BayJob::docked;
    float t=0.0f;                 // 0 in its bay .. 1 at its mouth
    ObjRef jet;                   // its live fighter (out of its bay)
    ULONGLONG at=0,askedAt=0;
    bool player=false;            // out for a player's sortie (out, board, run)
    bool home=false;              // its player's fighter called home to it (recover, alight)
};
struct Hangar {
    const void* bones=nullptr;
    bool ok=false,said=false,keyHeld=false,told=false;
    unsigned fitted=0;            // its hangar modules (bit m: dn_hangar_<m> in its model)
    unsigned char* module[kHangars]{};   // dn_hangar_<m>: the centre of its roof
    Bay bay[kBays]{};
    unsigned char* stand=nullptr; // dn_bridge: where a pilot put out of a fighter brought home stands
    int queue=0;                  // escort kinds still to go out (bits, one per kind)
    ULONGLONG queueAt=0;
    bool recall=false;            // its escorts called home
    bool failed[kDnFighterKinds]{};   // a kind its bays could not make this mission (not preloaded, too many jets)
    bool authority=false;         // this machine runs it
    ULONGLONG pressAt=0;          // the last board press over one of its hangars
    ULONGLONG loggedAt=0;
};
Hangar hangars[kMaxShips]{};
Hangar& HangarOf(const Ship& s) noexcept { return hangars[&s-ships]; }

// Where the local player is put (see the top): the frame after they got out of one of its seats, or out of a fighter
// brought home, on its bridge. (Off from the first range runs until 2026-10-21 evening: a soldier put on its deck fell
// through it, the ship hovering under a player's flight; standing on its deck held in the user's own play.)
bool kStandsOnHull=true;   // (a variable: the code under it kept compiled and checked)
constexpr float kPutOver=1.5f;   // m over a stand marker a soldier is put (WatchPlayer, PutOut): onto its cell, not in it
struct Bridge { bool pending=false; ObjRef ship; float at[3]{}; ULONGLONG putAt=0; int tries=0; } bridge{};

bool IsDnFighter(const void* v) noexcept {
    if(!v || BodyOf(v)!=PluginBody::jet)return false;
    const float k=BodyMark(v);
    return k>=kDnFighterMark && k<kDnFighterMark+static_cast<float>(kDnFighterKinds);
}
int KindOfBay(int i) noexcept { return dreadnought::KindOfBay(i); }
int Count(unsigned bits) noexcept { int n=0;for(;bits;bits&=bits-1u)++n;return n; }

// Its hangars as its model has them (the installer fitted them: see the top), their bays' bones, its bridge's.
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
    h.fitted=0;
    for(int m=0;m<kHangars;++m) {
        wchar_t name[32];
        swprintf_s(name,L"dn_hangar_%d",m);
        h.module[m]=BoneRecord506(inst,name);
        if(h.module[m])h.fitted|=1u<<m;
    }
    for(int i=0;i<kBays;++i) {
        Bay& b=h.bay[i];
        b.fitted=((h.fitted>>dreadnought::HangarOfBay(i))&1u)!=0;
        if(!b.fitted){b.rec=b.mouth=nullptr;continue;}
        wchar_t name[32];
        swprintf_s(name,L"dn_bay_%d",i);
        if((b.rec=need(name))==nullptr)return false;
        swprintf_s(name,L"dn_bay_%d_out",i);
        if((b.mouth=need(name))==nullptr)return false;
        std::memcpy(b.bind,b.rec+kBoneLocal506,64);
        std::memcpy(b.mouthBind,b.mouth+kBoneLocal506,64);
    }
    if((h.stand=need(L"dn_bridge"))==nullptr)return false;
    if(!h.told) {
        h.told=true;
        char names[96]="";
        for(int m=0;m<kHangars;++m) {
            if(!((h.fitted>>m)&1u))continue;
            if(*names)strcat_s(names,", ");
            strcat_s(names,kHangarNames[m]);
        }
        Log("DREADNOUGHT v=%p: %d hangars fitted (%s), %d bays",v,Count(h.fitted),*names ? names : "none",
            Count(dreadnought::FittedBays(h.fitted)));
        // The ini's fitting is the installer's (it built this model by it): one changed since means nothing until reinstalled.
        if(h.fitted!=Cfg().dreadnoughtHangars)
            Log("DREADNOUGHT v=%p: the ini fits hangars 0x%X, its installed model 0x%X: run the installer again for the ini's",v,
                Cfg().dreadnoughtHangars,h.fitted);
    }
    h.ok=true;
    return true;
}

// A bay's fighter where its job has it: from its bay toward its mouth (`t`), shown or hidden (scaled to nothing).
void PoseBay(Bay& b) noexcept {
    if(!b.fitted || !b.rec)return;
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

int Stowed(const Hangar& h) noexcept { int n=0;for(const auto& b:h.bay)n+=b.fitted && b.job==BayJob::docked ? 1 : 0;return n; }
// The bays with a fighter docked (bit i), the kinds that could not be made (bit k).
unsigned DockedBays(const Hangar& h) noexcept {
    unsigned out=0;
    for(int i=0;i<kBays;++i)if(h.bay[i].fitted && h.bay[i].job==BayJob::docked)out|=1u<<i;
    return out;
}
unsigned FailedKinds(const Hangar& h) noexcept {
    unsigned out=0;
    for(int k=0;k<kDnFighterKinds;++k)if(h.failed[k])out|=1u<<k;
    return out;
}
// An escort of its out (launched, away, coming home): not a player's sortie flown, nor one called home for its player.
bool Escort(const Bay& b) noexcept {
    if(!b.fitted || b.home || !(b.job==BayJob::away || b.job==BayJob::launch || b.job==BayJob::recover))return false;
    unsigned char* const f=JetOf(b.jet);
    return !(f && Flown(f));
}
int Out(const Hangar& h) noexcept { int n=0;for(const auto& b:h.bay)n+=Escort(b) ? 1 : 0;return n; }

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
// A level heading along `d` (else the ship's), into `out`.
void Level(const unsigned char* v,const float* d,float* out) noexcept {
    out[0]=d[0];out[1]=0.0f;out[2]=d[2];
    if(!vec::Normalize(out)){const float* n=Axis3(v,2);out[0]=n[0];out[1]=0.0f;out[2]=n[2];vec::Normalize(out);}
}
// Fighter `f` held thrown off its mouth: outboard and up, `k` (0 .. 1) of the way to its full speed.
void Throw(unsigned char* f,const unsigned char* v,const float* ship,const float* outboard,float k) noexcept {
    const float wup[3]={0.0f,1.0f,0.0f};
    float flat[3];
    Level(v,outboard,flat);
    float vel[3];
    for(int c=0;c<3;++c)vel[c]=ship[c]+flat[c]*kLaunchSpeed*k+(c==1 ? kLaunchClimb*k : 0.0f);
    HoldJet(f,vel,flat,wup);
}
// Its velocity relative to the ship's.
float Relative(unsigned char* f,const float* ship) noexcept {
    float vel[3];
    if(!JetMotion(f,vel))return 1.0e9f;
    const float d[3]={vel[0]-ship[0],vel[1]-ship[1],vel[2]-ship[2]};
    return vec::Len(d);
}

// The local player, on foot and alive, else null.
unsigned char* PlayerOnFoot() noexcept {
    unsigned char* const me=PlayerHuman();
    return me && !me[kDead] && HumanOnFoot(me) ? me : nullptr;
}

// Bay `i`'s fighter out of its bay: it slides to its mouth (BayFrame), then flies (an escort), or waits there for its
// player (`sortie`).
bool SendOut(Hangar& h,unsigned char* v,int i,bool sortie) noexcept {
    Bay& b=h.bay[i];
    if(!b.fitted || b.job!=BayJob::docked)return false;
    b.job=BayJob::out;b.at=GameMs();b.player=sortie;b.home=false;b.askedAt=0;
    Log("DREADNOUGHT v=%p hangar %s: bay %d's %s out (%s), %d docked",v,kHangarNames[dreadnought::HangarOfBay(i)],i,
        kKindNames[KindOfBay(i)],sortie ? "a player's sortie" : "an escort",Stowed(h)-1);
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
    const float* side=BoneAt(h.stand);
    for(int i=0;i<3;++i)bridge.at[i]=side[i]+Axis3(v,1)[i]*kPutOver;
    bridge.putAt=GameMs();bridge.ship=ObjRef::Of(v);bridge.pending=true;bridge.tries=0;
    Log("DREADNOUGHT v=%p hangar: its pilot put out onto its bridge",v);
}

// Back in: its fighter given to the game to delete at its mouth (then its own shown there and slid in).
void Stow(Bay& b,unsigned char* f,ULONGLONG ms) noexcept {
    JetHangarStow(f);
    b.job=BayJob::deleting;b.at=ms;b.player=false;b.home=false;
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
    const auto lost=[&]{ b.job=BayJob::empty;b.player=false;b.home=false;Log("DREADNOUGHT v=%p hangar: bay %d's %s lost",v,i,kKindNames[kind]); };
    switch(b.job) {
        case BayJob::docked:case BayJob::empty:
            break;
        case BayJob::out:   // sliding out; at its mouth the live fighter takes its place, in its motion
            b.t=std::fmin(1.0f,b.t+slide);
            if(b.t<1.0f)break;
            if(b.player && !PlayerOnFoot()) {   // its player went (into something else, or died) meanwhile
                Log("DREADNOUGHT v=%p hangar: bay %d's %s: its player is not on foot any more: back in",v,i,kKindNames[kind]);
                b.player=false;b.job=BayJob::in;break;
            }
            {
                unsigned char* const made=JetLaunchDreadnoughtFighter(kind,mouth,outboard,reinterpret_cast<const float*>(v+kPosition),
                                                                     Cfg().jetFuelSec,v,true);
                if(!made) {
                    Log("DREADNOUGHT v=%p hangar: no %s made at bay %d's mouth (not preloaded this mission, or too many jets): back in, "
                        "not sent out again this mission",v,kKindNames[kind],i);
                    h.failed[kind]=true;
                    b.player=false;b.job=BayJob::in;break;
                }
                b.jet=ObjRef::Of(made);b.job=b.player ? BayJob::board : BayJob::launch;b.at=ms;b.askedAt=0;
                const float sp=SlideLength(b)/kSlideSec;
                float vel[3];
                for(int c=0;c<3;++c)vel[c]=ship[c]+outboard[c]*sp;
                HoldJet(made,vel,outboard,up);
            }
            break;
        case BayJob::board:   // at its mouth for its player: held there until they are in it, then off with them
            if(!f){lost();break;}
            if(Flown(f)) {
                b.job=BayJob::run;b.at=ms;
                Log("DREADNOUGHT v=%p hangar: its player in bay %d's %s: off its mouth",v,i,kKindNames[kind]);
                break;
            }
            Pin(f,mouth,ship,outboard,up);
            if(ms-b.at>=kBoardWaitMs || !PlayerOnFoot()) {
                Log("DREADNOUGHT v=%p hangar: bay %d's %s: its player not in it after %.1f s: back in",v,i,kKindNames[kind],
                    static_cast<float>(ms-b.at)/1000.0f);
                Stow(b,f,ms);
                break;
            }
            if(!BoardingUnderway() && ms-b.askedAt>=kAskMs){b.askedAt=ms;BoardingRequest(f);}   // the boarding gun's way in
            break;
        case BayJob::launch:   // thrown outboard and up, then let go: an escort
        case BayJob::run: {    // ...or its player's sortie, theirs from there
            if(!f){lost();break;}
            if(b.job==BayJob::launch && Flown(f)){JetHangarRelease(f);b.job=BayJob::away;break;}   // a player took it (the boarding gun)
            const float k=Clamp(static_cast<float>(ms-b.at)/1000.0f/kLaunchSec,0.0f,1.0f);
            Throw(f,v,ship,outboard,k);
            if(k>=1.0f) {
                JetHangarRelease(f);
                const bool theirs=b.job==BayJob::run && Flown(f);
                if(theirs)PlayerJetLaunched(f);
                b.job=BayJob::away;
                Log("DREADNOUGHT v=%p hangar: bay %d's %s away%s",v,i,kKindNames[kind],theirs ? ", its player flying it" : "");
            }
            break;
        }
        case BayJob::away:   // out of its bay (flying, a player's): its live fighter's own
            if(!f)lost();
            break;
        case BayJob::recover: {   // to its mouth: outboard of it first, then backing straight in, nose outboard
            if(!f){lost();break;}
            if(!b.home && Flown(f)){JetHangarRelease(f);b.job=BayJob::away;break;}   // a player took it (the boarding gun): theirs
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
                if(b.home && Flown(f)) {   // its player brought home: put out onto the bridge here, then in
                    b.job=BayJob::alight;b.at=ms;
                    Log("DREADNOUGHT v=%p hangar: bay %d's %s at its mouth with its pilot: out onto its bridge",v,i,kKindNames[kind]);
                    PutOut(h,v,f);
                } else {
                    Stow(b,f,ms);
                    Log("DREADNOUGHT v=%p hangar: bay %d's %s at its mouth: in",v,i,kKindNames[kind]);
                }
            }
            break;
        }
        case BayJob::alight:   // its player's, brought home: held at its mouth while they are put out, then in
            if(!f){lost();break;}
            Pin(f,mouth,ship,outboard,up);
            if(ms-b.at<kAlightMs)break;   // their leave asks DreadnoughtTakesBack meanwhile
            if(Flown(f)) {   // still in it: the get-off did not take; it is theirs again, as it was
                JetHangarRelease(f);b.job=BayJob::away;b.home=false;
                Log("DREADNOUGHT v=%p hangar: bay %d's %s: its pilot still in it at its mouth: theirs again",v,i,kKindNames[kind]);
                break;
            }
            Stow(b,f,ms);
            Log("DREADNOUGHT v=%p hangar: bay %d's %s in, its pilot on its bridge",v,i,kKindNames[kind]);
            break;
        case BayJob::deleting:   // held at its mouth until the game has deleted it, then the bay's own shown there
            if(f){Pin(f,mouth,ship,outboard,up);break;}
            b.jet=ObjRef{};b.t=1.0f;b.job=BayJob::in;
            break;
        case BayJob::in:   // sliding back in
            b.t=std::fmax(0.0f,b.t-slide);
            if(b.t<=0.0f) {
                b.job=BayJob::docked;b.player=false;b.home=false;
                Log("DREADNOUGHT v=%p hangar: bay %d's %s docked (%d docked)",v,i,kKindNames[kind],Stowed(h));
            }
            break;
    }
    PoseBay(b);
}

// Bay `i`'s escort (flying) called home to its bay.
void CallHome(Hangar& h,unsigned char* v,int i) noexcept {
    Bay& b=h.bay[i];
    if(!b.fitted || b.job!=BayJob::away)return;
    b.job=BayJob::recover;b.at=GameMs();
    Log("DREADNOUGHT v=%p hangar: bay %d's %s called home",v,i,kKindNames[KindOfBay(i)]);
}

// Every frame on the machine that flies it: its bays (their escorts going out and coming home, its players' sorties).
// On the others its bays as they are (docked).
void HangarFrame(Ship& s,unsigned char* v,float dt,ULONGLONG ms) noexcept {
    Hangar& h=HangarOf(s);
    h.authority=!drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)) || IsOnlineAuthority(v);
    if(!RigHangar(h,v))return;
    if(!h.authority){for(auto& b:h.bay)PoseBay(b);return;}
    const float* const ship=s.measured;
    // the escort flight: one kind a kEscortGapMs, from the first fitted bay of it docked
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
        if(b.job!=BayJob::away || !f || !Escort(b))continue;
        if(h.recall || JetLeaving(f))CallHome(h,v,i);
    }
    if(h.recall && !Out(h))h.recall=false;
    for(int i=0;i<kBays;++i)if(h.bay[i].fitted)BayFrame(h,i,v,ship,dt,ms);
    if(Cfg().debug && ms-h.loggedAt>=1000) {
        h.loggedAt=ms;
        static const char* const kJobs[]={"docked","out","board","launch","run","away","recover","alight","deleting","in","empty"};
        static_assert(sizeof(kJobs)/sizeof(*kJobs)==static_cast<std::size_t>(BayJob::empty)+1,"a name a job");
        const float* c0=reinterpret_cast<const float*>(v+kPosition);
        for(int i=0;i<kBays;++i) {
            const Bay& b=h.bay[i];
            if(!b.fitted || b.job==BayJob::docked || b.job==BayJob::empty || b.job==BayJob::away)continue;
            unsigned char* const f=JetOf(b.jet);
            float rel[3]{},local[3]{};
            if(f){const float* p=reinterpret_cast<const float*>(f+kPosition);for(int c=0;c<3;++c)rel[c]=p[c]-c0[c];ToShip(v,rel,local);}
            Log("DREADNOUGHT v=%p bay %d %s t %.2f%s%s jet at ship (%.1f,%.1f,%.1f) %.1f m/s over the ship's",v,i,
                kJobs[static_cast<int>(b.job)],b.t,b.player ? " player" : "",b.home ? " home" : "",local[0],local[1],local[2],
                f ? Relative(f,ship) : 0.0f);
        }
    }
}

// The commander's hangar key: an escort flight out (one of each kind its fitted hangars have docked), or, its escorts
// out, home.
void HangarKey(Ship& s,unsigned char* v,const unsigned char* seat) noexcept {
    Hangar& h=HangarOf(s);
    const bool down=Pad(seat) ? Button(seat,Cfg().dreadnoughtHangarButton) : KeyDown(Cfg().dreadnoughtHangarKey);
    const bool pressed=down && !h.keyHeld;
    h.keyHeld=down;
    if(!pressed || !h.authority || !h.ok)return;
    const int escorts=Out(h);
    if(escorts>0 && !h.queue) {
        h.recall=true;
        Log("DREADNOUGHT v=%p hangar: its %d escorts called home",v,escorts);
        return;
    }
    h.recall=false;
    h.queue=static_cast<int>(dreadnought::EscortKinds(h.fitted,DockedBays(h),FailedKinds(h)));
    h.queueAt=0;
    Log("DREADNOUGHT v=%p hangar: an escort flight asked (kinds 0x%X, %d docked)",v,h.queue,Stowed(h));
}

// The hangar's part of the panel (DreadnoughtCue).
void HangarCue(const Hangar& h,DreadnoughtCue& c) noexcept {
    c.stowed=Stowed(h);
    c.out=Out(h);
    c.lifting=0;
    for(const auto& b:h.bay)
        c.lifting+=b.fitted && (b.job==BayJob::out || b.job==BayJob::in || b.job==BayJob::recover || b.job==BayJob::deleting ||
                                b.job==BayJob::board || b.job==BayJob::alight) ? 1 : 0;
    c.flightWait=0.0f;
}

// The fitted hangar local player `human` stands over (dreadnought_deck.h OverHangar), -1 none.
int HangarUnder(const unsigned char* v,const Hangar& h,const float* at) noexcept {
    for(int m=0;m<kHangars;++m) {
        if(!((h.fitted>>m)&1u) || !h.module[m])continue;
        const float* o=BoneAt(h.module[m]);
        const float d[3]={at[0]-o[0],at[1]-o[1],at[2]-o[2]};
        float local[3];
        ToShip(v,d,local);   // its left, up, ahead
        if(dreadnought::OverHangar(local[0],local[1],local[2]))return m;
    }
    return -1;
}

// A board press of the local player on foot (`human`) over one of its fitted hangars: a fighter of that hangar out to its
// mouth for them (SendOut, BayJob::board). True: the press was over a hangar of it (taken, no seat of the ship's).
bool HangarBoard(Ship& s,unsigned char* v,const unsigned char* human) noexcept {
    Hangar& h=HangarOf(s);
    if(!h.ok)return false;
    const int m=HangarUnder(v,h,reinterpret_cast<const float*>(human+kPosition));
    if(m<0)return false;
    const ULONGLONG ms=GameMs();
    const bool again=h.pressAt && ms-h.pressAt<kPressMs;
    h.pressAt=ms;
    if(again)return true;   // the same press, held
    if(!h.authority) {
        Log("DREADNOUGHT v=%p hangar %s: a board press, but another machine flies the ship (its hangars are that machine's)",v,kHangarNames[m]);
        return true;
    }
    for(int i=0;i<kBays;++i) {
        const Bay& b=h.bay[i];
        if(b.fitted && b.player && (b.job==BayJob::out || b.job==BayJob::board || b.job==BayJob::run)) {
            Log("DREADNOUGHT v=%p hangar %s: a board press, bay %d already out for its player",v,kHangarNames[m],i);
            return true;
        }
    }
    const int i=dreadnought::BoardBay(h.fitted,DockedBays(h),FailedKinds(h),m);
    if(i<0){Log("DREADNOUGHT v=%p hangar %s: a board press, no fighter docked in it",v,kHangarNames[m]);return true;}
    SendOut(h,v,i,true);
    return true;
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

// A local player's fighter `f`: its hangar key calls it home to its own bay of the ship it came out of (within
// kReturnRange).
void CallPlayerHome(unsigned char* f) noexcept {
    const float* p=reinterpret_cast<const float*>(f+kPosition);
    for(auto& s:ships) {
        if(!s.vehicle || !Live(s))continue;
        Hangar& h=HangarOf(s);
        if(!h.authority || !h.ok)continue;
        for(int i=0;i<kBays;++i) {
            Bay& b=h.bay[i];
            if(!b.fitted || !b.jet.Is(f) || b.job!=BayJob::away || b.home)continue;
            const float* q=reinterpret_cast<const float*>(s.vehicle+kPosition);
            const float d[3]={p[0]-q[0],p[1]-q[1],p[2]-q[2]};
            if(vec::Len(d)>kReturnRange) {
                Log("DREADNOUGHT v=%p hangar: bay %d's pilot calls home %.0f m out (%.0f at most)",s.vehicle,i,vec::Len(d),kReturnRange);
                return;
            }
            b.job=BayJob::recover;b.home=true;b.at=GameMs();
            Log("DREADNOUGHT v=%p hangar %s: bay %d's %s: its pilot calls home, %.0f m out: to its bay",s.vehicle,
                kHangarNames[dreadnought::HangarOfBay(i)],i,kKindNames[KindOfBay(i)],vec::Len(d));
            return;
        }
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
