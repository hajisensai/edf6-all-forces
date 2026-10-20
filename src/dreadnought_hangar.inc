// Included by dreadnought.cpp in its file namespace: its hangar's fighters (the user, 2026-10-21: 「那艘可以放飞船的无畏舰。
// 飞船可以玩家上去的那种」). The hangar (dn_hangar, set into its foredeck: tools/prep_dreadnought.py) launches the Dreadnought
// game's four fighters (jet_internal.h Body::dnFighter .. dnInterceptor: crewless NPC jets, playerjet_kinds.h kBoardable):
//  - Escort: its pilot's hangar key (DreadnoughtHangarKey / DreadnoughtHangarButton) launches a flight, one of each kind,
//    off its lifts (Launch: over dn_hangar_lift_f / _a, along its bow, at its speed), flying as escorts round the player
//    (jet.cpp: the escort's guard) until their fuel runs out; kEscortGapMs between flights, at most kEscortMost of its own
//    up at once.
//  - Sortie: a local player who leaves any of its seats while it flies (its keel over kSortieClear) is not dropped: the
//    hangar launches the next kind in turn (Ship::nextKind) and the boarding gun's path puts them into it
//    (boarding.cpp BoardingRequest: any reach, any speed), the moment both are there (kSortieWaitMs at most).
//  - Return: a local player leaving one of these fighters within kReturnRange of one of its dreadnoughts is put back aboard
//    the nearest (its seats in order: the pilot's first, then the turrets'); the fighter flies on, crewless, as an escort.
// Online, see docs/dreadnought-plan.md: the plugin's aircraft are each machine's own (docs/online-re.md, "插件飞机 / 无人机
// 的网络身份：没做"): a fighter launched here is this machine's.
constexpr ULONGLONG kEscortGapMs=8000;       // ms between its escort flights
constexpr int kEscortMost=8;                 // its escorts up at once at most
constexpr float kSortieClear=25.0f;          // m its keel over the ground: leaving it then is a sortie
constexpr float kLaunchUp=12.0f;             // m over a lift a fighter starts
constexpr ULONGLONG kSortieWaitMs=2500;      // ms a sortie waits for its fighter and the player on foot
constexpr float kReturnRange=800.0f;         // m from its dreadnought a fighter's pilot is put back aboard
constexpr std::uint16_t kButtonY=0x08;       // the seat's button bits (docs/stores-re.md §4)
constexpr std::size_t kHumanVehicle=0x1548;  // the vehicle a human rides (weak_ptr object)
const wchar_t* const kLifts[2]={L"dn_hangar_lift_f",L"dn_hangar_lift_a"};
const char* const kKindNames[kDnFighterKinds]={"fighter","defender","bomber","interceptor"};

struct Hangar {
    unsigned char* lift[2]{};     // its lifts' bone records
    const void* bones=nullptr;
    bool ok=false,said=false,keyHeld=false;
    ULONGLONG escortAt=0;
    ObjRef escorts[kEscortMost]{};
    int nextKind=0;               // the kind its next sortie flies
};
Hangar hangars[kMaxShips]{};
Hangar& HangarOf(const Ship& s) noexcept { return hangars[&s-ships]; }

// The local player's trip between a dreadnought and its fighters (one player, one trip at a time).
enum class Trip { none, sortie, back };
struct Transfer {
    Trip trip=Trip::none;
    ObjRef from,to;               // the vehicle left, the one they are put into
    ULONGLONG at=0;
    bool asked=false;
} transfer{};
const void* riddenLast=nullptr;   // the vehicle the local player rode last frame (WatchPlayer)
const void* riddenCtrl=nullptr;
ULONGLONG watchedFrame=0;

bool RigHangar(Hangar& h,unsigned char* v) noexcept {
    const unsigned char* inst=v+kModelInst506;
    const void* bones=At<const void*>(inst,kInstBones506);
    if(bones==h.bones && h.ok)return true;
    h.bones=bones;h.ok=false;
    for(int i=0;i<2;++i) {
        h.lift[i]=BoneRecord506(inst,kLifts[i]);
        if(!h.lift[i]) {
            if(!h.said)Log("DREADNOUGHT v=%p: its model lacks bone %ls: no hangar",v,kLifts[i]);
            h.said=true;
            return false;
        }
    }
    h.ok=true;
    return true;
}

int EscortsUp(Hangar& h) noexcept {
    int n=0;
    for(auto& e:h.escorts) {
        if(e && !JetFlying(e.obj,e.ctrl))e=ObjRef{};
        if(e)++n;
    }
    return n;
}

// One fighter of kind `kind` off lift `lift`: over it, along the ship's bow, working round where the ship is.
unsigned char* LaunchFighter(Hangar& h,unsigned char* v,int kind,int lift,bool escort) noexcept {
    if(!RigHangar(h,v))return nullptr;
    const float* at=BoneAt(h.lift[lift]);
    const float from[3]={at[0],at[1]+kLaunchUp,at[2]};
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    unsigned char* const f=JetLaunchDreadnoughtFighter(kind,from,m+8,pos,Cfg().jetFuelSec,v,escort);
    if(!f)Log("DREADNOUGHT v=%p hangar: no %s launched (its body not preloaded this mission, JetPilot off, or too many jets)",v,
              kKindNames[kind]);
    return f;
}

// The pilot's hangar key: an escort flight, one of each kind.
void HangarKey(Ship& s,unsigned char* v,const unsigned char* seat,ULONGLONG ms) noexcept {
    Hangar& h=HangarOf(s);
    const bool keys=At<unsigned char>(seat,kSeatPad)==0;
    const bool down=keys ? KeyDown(Cfg().dreadnoughtHangarKey) :
                           (At<std::uint16_t>(seat,kSeatButtons)&Cfg().dreadnoughtHangarButton)!=0;
    const bool pressed=down && !h.keyHeld;
    h.keyHeld=down;
    if(!pressed)return;
    const int up=EscortsUp(h);
    if(ms-h.escortAt<kEscortGapMs || up>=kEscortMost) {
        Log("DREADNOUGHT v=%p hangar: no flight now (%d escorts up, %.1f s since the last)",v,up,
            static_cast<float>(ms-h.escortAt)/1000.0f);
        return;
    }
    h.escortAt=ms;
    int launched=0;
    for(int k=0;k<kDnFighterKinds && EscortsUp(h)<kEscortMost;++k) {
        unsigned char* const f=LaunchFighter(h,v,k,k&1,true);
        if(!f)continue;
        for(auto& e:h.escorts)if(!e){e=ObjRef::Of(f);break;}
        ++launched;
    }
    Log("DREADNOUGHT v=%p hangar: an escort flight of %d launched",v,launched);
}

bool IsDnFighter(const void* v) noexcept {
    if(!v || BodyOf(v)!=PluginBody::jet)return false;
    const float k=BodyMark(v);
    return k>=kDnFighterMark && k<kDnFighterMark+static_cast<float>(kDnFighterKinds);
}

// The nearest of its live dreadnoughts within kReturnRange of `at`.
Ship* NearestShip(const float* at) noexcept {
    Ship* best=nullptr;
    float most=kReturnRange*kReturnRange;
    for(auto& s:ships) {
        if(!s.vehicle || !Live(s))continue;
        const float* p=reinterpret_cast<const float*>(s.vehicle+kPosition);
        const float d=(p[0]-at[0])*(p[0]-at[0])+(p[1]-at[1])*(p[1]-at[1])+(p[2]-at[2])*(p[2]-at[2]);
        if(d<most){most=d;best=&s;}
    }
    return best;
}

// Once a frame: what the local player rode last frame and rides now. Leaving a flying dreadnought starts a sortie,
// leaving one of its fighters near one starts the way back; a trip asks the boarding gun's path to seat them.
void WatchPlayer(ULONGLONG ms) noexcept {
    const ULONGLONG frame=GameFrame();
    if(watchedFrame==frame)return;
    watchedFrame=frame;
    unsigned char* const me=PlayerHuman();
    if(!me || me[kDead]){riddenLast=nullptr;transfer=Transfer{};return;}
    const bool onFoot=HumanOnFoot(me);
    const void* const now=onFoot ? nullptr : At<const void*>(me,kHumanVehicle);
    const void* const was=riddenLast;
    const void* const wasCtrl=riddenCtrl;
    riddenLast=now;riddenCtrl=now ? At<const void*>(static_cast<const unsigned char*>(now),kSelfCtrl) : nullptr;
    if(transfer.trip!=Trip::none) {
        auto* const to=static_cast<unsigned char*>(const_cast<void*>(transfer.to.obj));
        if(now && now==to) {
            Log("DREADNOUGHT %s: the player is in %p",transfer.trip==Trip::sortie ? "sortie" : "back aboard",now);
            transfer=Transfer{};
        } else if(ms-transfer.at>kSortieWaitMs) {
            Log("DREADNOUGHT %s: gave up (the player %s)",transfer.trip==Trip::sortie ? "sortie" : "way back",
                onFoot ? "on foot" : "in another vehicle");
            transfer=Transfer{};
        } else if(onFoot && !transfer.asked && transfer.to.Is(to) && !to[kDead]) {
            BoardingRequest(to);
            transfer.asked=true;
        }
        return;
    }
    if(!onFoot || !was || !ObjRef{was,wasCtrl}.Is(was))return;
    // Left a flying dreadnought: a sortie in its hangar's next fighter.
    for(auto& s:ships) {
        if(s.vehicle!=was || !Live(s) || s.clear==kNoGround || s.clear<kSortieClear)continue;
        Hangar& h=HangarOf(s);
        unsigned char* const f=LaunchFighter(h,s.vehicle,h.nextKind,0,true);
        if(!f)return;
        Log("DREADNOUGHT v=%p sortie: the player leaves it %.0f m up, into a %s",s.vehicle,s.clear,kKindNames[h.nextKind]);
        h.nextKind=(h.nextKind+1)%kDnFighterKinds;
        transfer=Transfer{Trip::sortie,ObjRef::Of(s.vehicle),ObjRef::Of(f),ms,false};
        return;
    }
    // Left one of its fighters near a dreadnought: back aboard it.
    if(IsDnFighter(was)) {
        const float* p=reinterpret_cast<const float*>(me+kPosition);
        Ship* const s=NearestShip(p);
        if(!s)return;
        Log("DREADNOUGHT v=%p: the player leaves a fighter %p within %.0f m: back aboard",s->vehicle,was,kReturnRange);
        transfer=Transfer{Trip::back,ObjRef{was,wasCtrl},ObjRef::Of(s->vehicle),ms,false};
    }
}
