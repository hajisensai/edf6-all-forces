// The squads' transports (transport.h; the rules and the user's words: transport_logic.h).
//  - Pairs: a squad (its top NPC) and its vehicle. Made by the map's BOARD (npcai.cpp), a crewed APC / truck's delivery
//    and a helicopter assault (support_dispatch.cpp TransportDeliver). Let go when either is gone, on WITHDRAW, when the
//    squad joins another (npcai.cpp Succeed), at a new mission.
//  - Trips: one a vehicle at a time, carrying up to kRiders of its paired squads (a 12-soldier platoon is three), each with
//    the order it carries out once off. The vehicle comes for them (pickup) or they walk to an empty one, they board, it
//    goes (a ground vehicle along a ground route: GroundNavigate, as a support vehicle comes in; a helicopter's ferry:
//    heli.cpp HeliFerry), stops / lands short of the order's point, they get off, it waits there.
//  - Drops: a transport plane's passengers jump over its point one after another (a stick); a canopy over each but a Wing
//    Diver (playerjet.cpp ChuteCanopyMake), the fall held as the player's parachute holds it.
// No soldier or vehicle is made or teleported here: the soldiers board and get off by the stock ride (npcai.cpp), the
// vehicles are driven / flown by their NPC drivers through the plugin's own controllers.
#include "crew.h"
#include "transport.h"
#include "transport_logic.h"
#include "npcai.h"
#include "mapcmd.h"
#include "body506.h"
#include "ground_navigation.h"
#include "online_authority.h"
#include "layout.h"
#include "memory.h"
#include <cmath>
#include <cstring>

namespace crew {
bool SupportWithdrawVehicle(const void* vehicle) noexcept;   // support_dispatch.cpp: a support vehicle sent off the field
namespace {
using transport::Act;
using transport::Carrier;
using transport::Phase;
// kDrops: one a deployment (support_dispatch.cpp kDeployments): a plane with no drop would fly its call with its soldiers
// aboard and never let them out.
constexpr int kPairs=32,kTrips=8,kRiders=4,kDrops=16,kJumpers=32,kDropTops=16;
constexpr ULONGLONG kBoardEveryMs=2000,kUnloadEveryMs=1500,kWalkEveryMs=3000,kLogMs=2000;
constexpr ULONGLONG kJumpMostMs=180000;              // a jump not down after this long is let go
constexpr float kChuteSink=6.0f,kChuteBleed=0.6f;    // as the player's parachute (playerjet.cpp EjectTick)
constexpr float kChuteLand=1.5f;
constexpr float kHeliBeside=25.0f;                   // m from the squad a helicopter lands to pick it up
constexpr float kVehicleHold=1.0f;                   // m: a held vehicle's route post radius (npcpost.cpp)
// The soldier's walk controller (playerjet.cpp: kHumanVel, kHumanSupport).
constexpr std::size_t kHumanVel=0x6B0,kHumanSupport=0x711;

struct Pair { ObjRef top,vehicle; };
struct TripRider { ObjRef top; mapcmd::Command after; };
struct Trip {
    ObjRef vehicle;
    transport::Trip t;
    TripRider riders[kRiders];
    int count;
    float sample[3];ULONGLONG sampleAt;float speed;
    ULONGLONG actAt,loggedAt;
    Act last;
    npc::navigation::State nav;
};
// `wayLogged`: its way out of the plane logged (the drift StickLead assumes, transport_logic.h kJumpInherit).
struct Jumper { ObjRef human,canopy; ULONGLONG at; bool flies,wayLogged; };
// tops: the squads that jumped, by identity (a squad's top killed on the way down may be freed and its memory reused
// before the stick ends: a bare pointer would hand the guard order to whatever lives there then).
// `seen` / `seenAt`: the plane's place last frame (its way: StickStarts).
struct Drop { ObjRef plane; float target[3]; int jumped; ULONGLONG lastJump; ObjRef tops[kDropTops]; int topCount;
              float seen[3]; ULONGLONG seenAt; };
Pair pairs[kPairs]{};
Trip trips[kTrips]{};
Drop drops[kDrops]{};
Jumper jumpers[kJumpers]{};
transport::Tuning tuning{};

bool Live(const ObjRef& r) noexcept {
    return r && Readable(r.obj,kDead+1) && r.Is(r.obj) && !static_cast<const unsigned char*>(r.obj)[kDead];
}
const float* Pos(const void* o) noexcept { return reinterpret_cast<const float*>(static_cast<const unsigned char*>(o)+kPosition); }
unsigned char* Vehicle(const ObjRef& r) noexcept { return static_cast<unsigned char*>(const_cast<void*>(r.obj)); }
bool Transportable(const void* v) noexcept {
    return v && Readable(v,kSeatCount+8) && !static_cast<const unsigned char*>(v)[kDead] && !IsJet(v) &&
           (IsHelicopter(v) || NpcDrivable(static_cast<const unsigned char*>(v))) && SeatCount(static_cast<const unsigned char*>(v))>1;
}
Carrier CarrierOf(const void* v) noexcept { return IsHelicopter(v) ? Carrier::heli : Carrier::ground; }
float Standoff(Carrier c) noexcept { return c==Carrier::heli ? tuning.heliStandoff : tuning.groundStandoff; }
npc::navigation::Profile VehicleProfile() noexcept {
    npc::navigation::Profile p;
    p.radius=3.5f;p.height=3.0f;p.cell=4.0f;p.waypointRadius=1.5f;
    p.horizon=120.0f;   // rolling legs: a trip may cross the map (a whole route at once would not fit the search)
    return p;
}

Pair* PairOf(const void* top) noexcept {
    for(auto& p:pairs)if(p.top && p.top.Is(top))return &p;
    return nullptr;
}
Trip* TripOf(const void* v) noexcept {
    for(auto& t:trips)if(t.count>0 && t.vehicle.Is(v))return &t;
    return nullptr;
}
TripRider* RiderOf(Trip& t,const void* top) noexcept {
    for(int i=0;i<t.count;++i)if(t.riders[i].top.Is(top))return &t.riders[i];
    return nullptr;
}
void Hold(unsigned char* v) noexcept { NpcPrepareVehicleRoutePost(v,Pos(v),kVehicleHold); }

// The trip's end: each rider takes its order on foot (`off`: the riding ones get off first, when the vehicle is at rest),
// the vehicle waits where it is (a helicopter's ferry ends: it guards the drop point).
void EndTrip(Trip& t,bool off,const char* why) noexcept {
    unsigned char* const v=Live(t.vehicle) ? Vehicle(t.vehicle) : nullptr;
    for(int i=0;i<t.count;++i) {
        const TripRider& r=t.riders[i];
        if(!Live(r.top))continue;
        if(off)NpcSquadDismount(r.top.obj);
        NpcSquadSetOrder(r.top.obj,r.after,false);
    }
    if(v) {
        if(t.t.carrier==Carrier::heli) {
            HeliFerry(v,nullptr,false);
            const float* at=t.t.phase==Phase::pickup || t.t.phase==Phase::boarding ? Pos(v) : t.t.drop;
            HeliCommand(v,mapcmd::Command{mapcmd::Order::guard,{at[0],at[1],at[2]}});
        } else Hold(v);
    }
    Log("TRANSPORT v=%p trip over (%s): %d squad(s) on with their orders",t.vehicle.obj,why,t.count);
    t.count=0;t.t=transport::Trip{};
}

transport::View ViewOf(Trip& t,ULONGLONG ms) noexcept {
    transport::View view{};
    view.ms=ms;
    view.vehicleLive=Live(t.vehicle);
    if(!view.vehicleLive){view.squadLive=false;return view;}
    unsigned char* const v=Vehicle(t.vehicle);
    const float* vp=Pos(v);
    int alive=0,aboard=0;float farthest=0.0f;
    for(int i=0;i<t.count;++i) {
        SquadSeats s{};
        if(!Live(t.riders[i].top) || !NpcSquadSeats(t.riders[i].top.obj,v,&s))continue;
        alive+=s.alive;aboard+=s.aboard;
        if(s.onFoot>0){const float d=transport::Level(Pos(t.riders[i].top.obj),vp);if(d>farthest)farthest=d;}
    }
    view.squadLive=alive>0;
    view.aboardAny=aboard>0;view.aboardAll=alive>0 && aboard==alive;
    view.squadToVehicle=farthest;
    view.vehicleToDrop=transport::Level(vp,t.t.drop);
    if(t.sampleAt && ms>t.sampleAt) {
        const float dt=static_cast<float>(ms-t.sampleAt)*0.001f;
        const float d[3]={vp[0]-t.sample[0],vp[1]-t.sample[1],vp[2]-t.sample[2]};
        const float now=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2])/dt;
        t.speed+=(now-t.speed)*0.3f;
    }
    std::memcpy(t.sample,vp,12);t.sampleAt=ms;
    view.speed=t.speed;
    view.grounded=t.t.carrier==Carrier::heli ? HeliGrounded(v) : true;
    view.driven=NpcDriver(v);
    return view;
}

// The ground vehicle on its route to `goal` (stopping within `arrive` m): the route's next corner its post, else held.
void Drive(Trip& t,unsigned char* v,const float* goal,float arrive,ULONGLONG ms) noexcept {
    float waypoint[3];
    const auto path=GroundNavigate(t.nav,Pos(v),goal,arrive,ms,waypoint,VehicleProfile());
    if(path==npc::navigation::Result::moving)NpcPrepareVehicleRoutePost(v,waypoint,kVehicleHold);
    else Hold(v);   // arrived, still searching or no route: it waits (never a straight line through a wall)
}

void Act1(Trip& t,Act a,const transport::View& view,ULONGLONG ms) noexcept {
    unsigned char* const v=Vehicle(t.vehicle);
    const bool heli=t.t.carrier==Carrier::heli;
    const bool fresh=a!=t.last;
    t.last=a;
    switch(a) {
    case Act::comeTo: {
        const TripRider* farthest=nullptr;float d=-1.0f;
        for(int i=0;i<t.count;++i)if(Live(t.riders[i].top)) {
            const float e=transport::Level(Pos(t.riders[i].top.obj),Pos(v));
            if(e>d){d=e;farthest=&t.riders[i];}
        }
        if(!farthest)return;
        const float* sp=Pos(farthest->top.obj);
        if(view.driven) {
            if(heli) {   // down beside the squad, on its side of it
                float dir[3]={Pos(v)[0]-sp[0],0.0f,Pos(v)[2]-sp[2]};
                const float l=std::hypot(dir[0],dir[2]);
                if(l>1.0f){dir[0]/=l;dir[2]/=l;}else{dir[0]=1.0f;dir[2]=0.0f;}
                const float spot[3]={sp[0]+dir[0]*kHeliBeside,sp[1],sp[2]+dir[2]*kHeliBeside};
                HeliFerry(v,spot,true);
            } else Drive(t,v,sp,tuning.groundReach*0.5f,ms);
        } else if(fresh || ms-t.actAt>kWalkEveryMs) {   // nobody to drive it: they walk to it
            t.actAt=ms;
            const float* vp=Pos(v);
            for(int i=0;i<t.count;++i)if(Live(t.riders[i].top))
                NpcSquadSetOrder(t.riders[i].top.obj,mapcmd::Command{mapcmd::Order::move,{vp[0],vp[1],vp[2]}},true);
        }
        return;
    }
    case Act::board:
        if(heli)HeliFerry(v,Pos(v),true);else Hold(v);
        if(fresh || ms-t.actAt>kBoardEveryMs) {
            t.actAt=ms;
            for(int i=0;i<t.count;++i)if(Live(t.riders[i].top)) {
                const float* sp=Pos(t.riders[i].top.obj);
                NpcSquadSetOrder(t.riders[i].top.obj,mapcmd::Command{mapcmd::Order::guard,{sp[0],sp[1],sp[2]}},true);   // no walking off
                NpcSquadBoardVehicle(t.riders[i].top.obj,v);
            }
        }
        return;
    case Act::goTo:
        if(heli)HeliFerry(v,t.t.drop,false);else Drive(t,v,t.t.drop,tuning.groundArrive,ms);
        return;
    case Act::land:
        HeliFerry(v,t.t.drop,true);
        return;
    case Act::unload:
        if(heli)HeliFerry(v,Pos(v),true);else Hold(v);
        if(fresh || ms-t.actAt>kUnloadEveryMs) {
            t.actAt=ms;
            for(int i=0;i<t.count;++i)if(Live(t.riders[i].top))NpcSquadDismount(t.riders[i].top.obj);
        }
        return;
    case Act::release: EndTrip(t,false,"done");return;
    case Act::abort: EndTrip(t,view.aboardAny && (heli ? view.grounded : view.speed<=tuning.stopped),"cannot go on");return;
    default: return;
    }
}

void TripFrame(Trip& t,ULONGLONG ms) noexcept {
    if(Live(t.vehicle) && !IsOnlineAuthority(t.vehicle.obj))return;   // driven on another machine: its trip is that one's
    const transport::View view=ViewOf(t,ms);
    const Phase was=t.t.phase;
    const transport::Step step=transport::Advance(t.t,view,tuning);
    if(step.next!=was && step.act!=Act::release && step.act!=Act::abort) {
        t.t.phase=step.next;t.t.since=ms;
        Log("TRANSPORT v=%p %s -> %s (%.0f m to the drop, %d squad(s), aboard %s)",t.vehicle.obj,transport::kPhaseNames[static_cast<int>(was)],
            transport::kPhaseNames[static_cast<int>(step.next)],view.vehicleToDrop,t.count,view.aboardAll ? "all" : view.aboardAny ? "some" : "none");
    }
    if(!view.vehicleLive){EndTrip(t,false,"the vehicle is gone");return;}
    Act1(t,step.act,view,ms);
    if(t.count>0 && Cfg().debug && ms-t.loggedAt>kLogMs) {
        t.loggedAt=ms;
        Log("TRANSPORT v=%p %s act=%s drop %.0f m, squad %.0f m, %.1f m/s%s%s",t.vehicle.obj,transport::kPhaseNames[static_cast<int>(t.t.phase)],
            transport::kActNames[static_cast<int>(step.act)],view.vehicleToDrop,view.squadToVehicle,view.speed,
            view.grounded ? " grounded" : "",view.driven ? "" : " (no driver)");
    }
}

// A trip for `v` with rider `top` (its order `after`): a new one, or `top` joining the vehicle's trip under way while it
// still gathers (or riding it already). False: the vehicle is busy elsewhere.
// The trip's drop point for `after` (the vehicle where it is now) and the phase it goes on in from there.
void Aim(Trip& t,unsigned char* v,const mapcmd::Command& after,ULONGLONG ms) noexcept {
    transport::DropPoint(Pos(v),after.at,Standoff(t.t.carrier),t.t.drop);
    const transport::View view=ViewOf(t,ms);
    t.t.phase=transport::Begin(t.t.carrier,view,tuning);t.t.since=ms;t.last=Act::none;
}

bool StartOrJoin(unsigned char* v,const void* top,const mapcmd::Command& after,bool aboard,ULONGLONG ms) noexcept {
    if(Trip* t=TripOf(v)) {
        if(TripRider* r=RiderOf(*t,top)) {   // a new order for a squad on it: the trip goes there instead
            r->after=after;Aim(*t,v,after,ms);
            Log("TRANSPORT v=%p squad %p re-ordered: drop (%.0f,%.0f), %s",v,top,t->t.drop[0],t->t.drop[2],
                transport::kPhaseNames[static_cast<int>(t->t.phase)]);
            return true;
        }
        const bool gathering=t->t.phase==Phase::pickup || t->t.phase==Phase::boarding;
        if(t->count>=kRiders || !(gathering || aboard))return false;
        t->riders[t->count++]=TripRider{ObjRef::Of(top),after};
        Log("TRANSPORT v=%p squad %p joins its trip (%s)",v,top,transport::kPhaseNames[static_cast<int>(t->t.phase)]);
        return true;
    }
    Trip* t=nullptr;
    for(auto& row:trips)if(row.count==0){t=&row;break;}
    if(!t){Log("TRANSPORT trips full (%d): squad %p walks",kTrips,top);return false;}
    t->vehicle=ObjRef::Of(v);t->count=1;t->riders[0]=TripRider{ObjRef::Of(top),after};
    t->sampleAt=0;t->speed=0.0f;t->actAt=0;t->loggedAt=0;t->last=Act::none;t->nav.initialized=false;
    t->t=transport::Trip{};t->t.carrier=CarrierOf(v);
    Aim(*t,v,after,ms);
    Log("TRANSPORT v=%p (%s) trip for squad %p to (%.0f,%.0f,%.0f): drop (%.0f,%.0f), %s",v,t->t.carrier==Carrier::heli ? "heli" : "ground",
        top,after.at[0],after.at[1],after.at[2],t->t.drop[0],t->t.drop[2],transport::kPhaseNames[static_cast<int>(t->t.phase)]);
    return true;
}

void LeaveTrips(const void* top) noexcept {
    for(auto& t:trips)for(int i=0;i<t.count;++i)if(t.riders[i].top.Is(top)) {
        t.riders[i]=t.riders[--t.count];
        if(t.count==0)EndTrip(t,false,"its squads took other orders");   // the vehicle waits where it is
        break;
    }
}

// --- The drop ---
void JumpFrame(Jumper& j,ULONGLONG ms) noexcept {
    if(!Live(j.human) || !HumanOnFoot(static_cast<const unsigned char*>(j.human.obj)) || ms-j.at>kJumpMostMs) {
        ChuteCanopyFree(j.canopy);j=Jumper{};return;
    }
    auto* h=static_cast<unsigned char*>(const_cast<void*>(j.human.obj));
    const float* p=Pos(h);
    const float clear=GroundClearance(p);
    if(ms-j.at>300 && transport::Landed(h[kHumanSupport]!=0,clear==kNoGround ? -1.0f : clear,kChuteLand)) {
        ChuteCanopyFree(j.canopy);
        Log("TRANSPORT paratrooper %p down at (%.0f,%.0f,%.0f) after %.0f s",h,p[0],p[1],p[2],static_cast<float>(ms-j.at)*0.001f);
        j=Jumper{};return;
    }
    if(j.flies)return;   // a Wing Diver flies down on her own
    float* const vel=reinterpret_cast<float*>(h+kHumanVel);
    if(!j.wayLogged){j.wayLogged=true;Log("TRANSPORT paratrooper %p out at %.0f m/s level, %.0f m/s down",h,std::sqrt(vel[0]*vel[0]+vel[2]*vel[2]),-vel[1]);}
    transport::ChuteStep(vel,kChuteSink,kChuteBleed);
    if(!j.canopy && vel[1]<=0.0f)j.canopy=ChuteCanopyMake(p,vel);
    else if(j.canopy)ChuteCanopyMove(j.canopy,p,vel);
}

// A free jumper's place: a soldier is let out only with one (with none it would fall without its canopy).
Jumper* FreeJumper() noexcept {
    for(auto& j:jumpers)if(!j.human)return &j;
    return nullptr;
}

// The plane's passengers (soldiers in its seats past the pilot's): the first one's seat, or -1.
int NextJumper(unsigned char* plane) noexcept {
    for(unsigned seat=1;seat<SeatCount(plane);++seat)
        if(SeatRider(SeatAt(plane,seat))==Rider::other && IsSoldierClass(At<const void*>(SeatAt(plane,seat),kSeatRider)))return static_cast<int>(seat);
    return -1;
}

void DropFrame(Drop& d,ULONGLONG ms) noexcept {
    if(!Live(d.plane)){Log("TRANSPORT drop: its plane is gone (%d out)",d.jumped);d=Drop{};return;}
    unsigned char* const plane=Vehicle(d.plane);
    if(!IsOnlineAuthority(plane))return;
    const int seat=NextJumper(plane);
    if(seat<0) {
        if(d.jumped>0 || ms-d.lastJump>60000) {   // all out (or nobody aboard after a minute): off it goes, they guard the point
            // On along its line to its end off the map, deleted there (jet.cpp JetFerryDone); a plane flying no line withdraws.
            if(!JetFerryDone(plane))JetWithdrawNow(plane,"paratroopers out");
            for(int i=0;i<d.topCount;++i)
                if(Live(d.tops[i]))NpcSquadSetOrder(d.tops[i].obj,mapcmd::Command{mapcmd::Order::guard,{d.target[0],d.target[1],d.target[2]}},false);
            Log("TRANSPORT drop over (%d out): the plane leaves",d.jumped);
            d=Drop{};
        }
        return;
    }
    const float* pp=Pos(plane);
    const float dist=transport::Level(pp,d.target);
    if(d.jumped==0) {
        // Its way from last frame's place; the point along and across its track (StickStarts).
        const float dt=d.seenAt && ms>d.seenAt ? static_cast<float>(ms-d.seenAt)*0.001f : 0.0f;
        const float way[2]={dt>0.0f ? (pp[0]-d.seen[0])/dt : 0.0f,dt>0.0f ? (pp[2]-d.seen[2])/dt : 0.0f};
        std::memcpy(d.seen,pp,12);d.seenAt=ms;
        const float speed=std::sqrt(way[0]*way[0]+way[1]*way[1]);
        if(!(speed>1.0f))return;
        const float to[2]={d.target[0]-pp[0],d.target[2]-pp[2]};
        const float along=(to[0]*way[0]+to[1]*way[1])/speed,across=(to[0]*way[1]-to[1]*way[0])/speed;
        int aboard=0;
        for(unsigned s=1;s<SeatCount(plane);++s)
            aboard+=SeatRider(SeatAt(plane,s))==Rider::other && IsSoldierClass(At<const void*>(SeatAt(plane,s),kSeatRider));
        if(!transport::StickStarts(along,across,speed,aboard,kChuteBleed))return;
        Log("TRANSPORT stick of %d starts %.0f m short of the point along the track, %.0f m off it, at %.0f m/s (lead %.0f m)",aboard,along,
            across,speed,transport::StickLead(speed,aboard,kChuteBleed));
    } else if(!transport::JumpNow(d.jumped,d.jumped+1,ms,d.lastJump))return;
    Jumper* const slot=FreeJumper();
    if(!slot)return;   // every place taken by those still coming down: the next waits for one
    auto* h=At<unsigned char*>(SeatAt(plane,static_cast<unsigned>(seat)),kSeatRider);
    const void* top=NpcSquadTopOf(h);
    if(!NpcMoveSeat(plane,static_cast<unsigned>(seat),-1)){d.lastJump=ms;return;}
    ++d.jumped;d.lastJump=ms;
    *slot=Jumper{ObjRef::Of(h),{},ms,NpcSoldierFlies(h),false};
    bool known=false;for(int i=0;i<d.topCount;++i)known=known || d.tops[i].Is(top);
    if(top && !known && d.topCount<kDropTops)d.tops[d.topCount++]=ObjRef::Of(top);
    Log("TRANSPORT jump %d from plane %p (seat %d) %.0f m from the point%s",d.jumped,plane,seat,dist,NpcSoldierFlies(h) ? ", a Wing Diver: no canopy" : "");
}
}  // namespace

bool TransportPair(const void* top,const void* vehicle) noexcept {
    __try {
        if(!top || !Transportable(vehicle))return false;
        TransportCancel(top);
        Pair* p=PairOf(top);
        if(!p)for(auto& row:pairs)if(!row.top || !Live(row.top) || !Live(row.vehicle)){p=&row;break;}
        if(!p){Log("TRANSPORT pairs full (%d): squad %p not paired",kPairs,top);return false;}
        p->top=ObjRef::Of(top);p->vehicle=ObjRef::Of(vehicle);
        if(IsHelicopter(vehicle))HeliKeep(vehicle);   // theirs until WITHDRAW (no leaving for fuel or ammo)
        Log("TRANSPORT squad %p paired with v=%p (%s)",top,vehicle,IsHelicopter(vehicle) ? "heli" : "ground");
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

const void* TransportOf(const void* top) noexcept {
    __try {
        const Pair* p=PairOf(top);
        return p && Live(p->vehicle) ? p->vehicle.obj : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}

const void* TransportRiderOf(const void* vehicle) noexcept {
    __try {
        for(const auto& p:pairs) {
            if(!p.vehicle.Is(vehicle) || !Live(p.top))continue;
            SquadSeats s{};
            if(NpcSquadSeats(p.top.obj,vehicle,&s) && s.aboard>0)return p.top.obj;
        }
        return nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}

bool TransportOrder(const void* top,const mapcmd::Command& c) noexcept {
    __try {
        const Pair* p=PairOf(top);
        if(!p || !Live(p->vehicle) || !mapcmd::PointOrder(c.order))return false;
        unsigned char* const v=Vehicle(p->vehicle);
        if(!IsOnlineAuthority(v))return false;
        SquadSeats s{};
        if(!NpcSquadSeats(top,v,&s) || s.alive<=0)return false;
        const float distance=transport::Level(Pos(top),c.at);
        if(!transport::Rides(true,s.aboard>0,distance,Cfg().transportAutoRange))return false;
        const ULONGLONG ms=GameMs();
        if(!StartOrJoin(v,top,c,s.aboard>0,ms))return false;
        // Held where it is until the trip lets it go (a move / attack-move would walk it off over its boarding).
        if(s.onFoot>0) {
            const float* tp=Pos(top);
            NpcSquadSetOrder(top,mapcmd::Command{mapcmd::Order::guard,{tp[0],tp[1],tp[2]}},true);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void TransportCancel(const void* top) noexcept {
    __try {LeaveTrips(top);} __except(EXCEPTION_EXECUTE_HANDLER){}
}

NpcCommandReason TransportWithdraw(const void* top) noexcept {
    __try {
        Pair* p=PairOf(top);
        if(!p || !Live(p->vehicle))return NpcCommandReason::noTransport;
        unsigned char* const v=Vehicle(p->vehicle);
        if(!IsOnlineAuthority(v))return NpcCommandReason::notAuthority;
        // Its riders off first, at rest only (a vehicle under way would leave with them: refused).
        Trip* t=TripOf(v);
        const bool heli=IsHelicopter(v);
        const bool still=heli ? HeliGrounded(v) : !t || t->speed<=tuning.stopped;
        for(const auto& q:pairs) {
            if(!q.vehicle.Is(v) || !Live(q.top))continue;
            SquadSeats s{};
            if(NpcSquadSeats(q.top.obj,v,&s) && s.aboard>0 && !still)return NpcCommandReason::noTransport;
        }
        if(!SupportWithdrawVehicle(v))return NpcCommandReason::noTransport;   // only a support vehicle leaves the field
        if(t)EndTrip(*t,true,"withdrawn");
        for(auto& q:pairs)if(q.vehicle.Is(v)) {
            if(Live(q.top))NpcSquadDismount(q.top.obj);
            q=Pair{};
        }
        Log("TRANSPORT v=%p withdrawn by order (squad %p)",v,top);
        return NpcCommandReason::none;
    } __except(EXCEPTION_EXECUTE_HANDLER){return NpcCommandReason::failed;}
}

void TransportSucceed(const void* dead,const void* lead) noexcept {
    __try {
        for(auto& p:pairs)if(p.top.obj==dead) {
            if(lead)p.top=ObjRef::Of(lead);
            else p=Pair{};
        }
        if(lead) {
            for(auto& t:trips)for(int i=0;i<t.count;++i)if(t.riders[i].top.obj==dead)t.riders[i].top=ObjRef::Of(lead);
        } else LeaveTrips(dead);   // joined another squad: its place in the trip goes as any other order's (EndTrip if the last)
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}

bool TransportDeliver(const void* vehicle,const void* const* tops,int count,const float* target) noexcept {
    __try {
        if(!Transportable(vehicle) || !tops || count<=0 || !target)return false;
        auto* const v=static_cast<unsigned char*>(const_cast<void*>(vehicle));
        const ULONGLONG ms=GameMs();
        int carried=0;
        for(int i=0;i<count;++i) {
            if(!tops[i] || !TransportPair(tops[i],v))continue;
            SquadSeats s{};NpcSquadSeats(tops[i],v,&s);
            if(StartOrJoin(v,tops[i],mapcmd::Command{mapcmd::Order::guard,{target[0],target[1],target[2]}},s.aboard>0,ms))++carried;
        }
        Log("TRANSPORT v=%p delivers %d squad(s) to (%.0f,%.0f,%.0f)",v,carried,target[0],target[1],target[2]);
        return carried>0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool TransportParadrop(const void* plane,const float* target) noexcept {
    if(!plane || !target)return false;
    for(auto& d:drops)if(!d.plane || !Live(d.plane)) {
        d=Drop{};d.plane=ObjRef::Of(plane);std::memcpy(d.target,target,12);d.lastJump=GameMs();
        Log("TRANSPORT plane %p: paratroop drop over (%.0f,%.0f,%.0f)",plane,target[0],target[1],target[2]);
        return true;
    }
    return false;
}

void TransportTick() noexcept {
    static ULONGLONG frame=~0ull;
    if(frame==GameFrame())return;
    frame=GameFrame();
    __try {
        tuning.autoRange=Cfg().transportAutoRange;
        const ULONGLONG ms=GameMs();
        for(auto& p:pairs)if(p.top && (!Live(p.top) || !Live(p.vehicle))) {
            if(Live(p.top) && !Live(p.vehicle))Log("TRANSPORT squad %p: its transport is gone",p.top.obj);
            p=Pair{};
        }
        for(auto& t:trips)if(t.count>0)TripFrame(t,ms);
        for(auto& d:drops)if(d.plane)DropFrame(d,ms);
        for(auto& j:jumpers)if(j.human)JumpFrame(j,ms);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}

void ResetTransports() noexcept {
    for(auto& p:pairs)p=Pair{};
    for(auto& t:trips){t.count=0;t.t=transport::Trip{};t.nav.initialized=false;}
    for(auto& d:drops)d=Drop{};
    for(auto& j:jumpers)j=Jumper{};   // the canopies went with the last mission's objects
}

int TransportLinks(TransportLink* out,int most) noexcept {
    int n=0;
    __try {
        for(const auto& p:pairs) {
            if(n>=most || !Live(p.top) || !Live(p.vehicle))continue;
            if(!HumanOnFoot(static_cast<const unsigned char*>(p.top.obj)))continue;   // aboard: its vehicle's mark stands for it
            std::memcpy(out[n].squad,Pos(p.top.obj),12);std::memcpy(out[n].vehicle,Pos(p.vehicle.obj),12);
            out[n].trip=TripOf(p.vehicle.obj)!=nullptr;
            ++n;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return n;
}
}  // namespace crew
