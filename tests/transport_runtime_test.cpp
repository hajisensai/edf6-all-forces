// Execute src/transport.cpp itself against stand-ins: vehicles and squads are memory blocks a small world model moves
// in answer to the calls transport.cpp makes (a route post drives the truck, a ferry flies the helicopter, boarding seats
// a squad near its vehicle). Checks a whole ground trip, an empty vehicle, a near order, a helicopter assault with two
// squads, WITHDRAW, a leader's succession and the paratroop drop. No game is loaded.
#include "../src/transport.cpp"
#include <cstdio>
#include <cstdlib>

namespace crew {
unsigned char* image=nullptr;
Config config{};
ULONGLONG now=1000,frameNo=1;
const Config& Cfg() noexcept {return config;}
ULONGLONG GameMs() noexcept {return now;}
std::uint64_t GameFrame() noexcept {return frameNo;}
void Log(const char* f,...) noexcept {
    if(!GetEnvironmentVariableA("TRANSPORT_LOG",nullptr,0))return;
    va_list a;va_start(a,f);std::vprintf(f,a);va_end(a);std::printf("\n");
}
bool IsOnlineAuthority(const void*) noexcept {return true;}

struct Obj { alignas(16) unsigned char m[0x2400]; unsigned char ctrl[0x20]; };
Obj truck{},heli{},plane{},soldier[16]{};
alignas(16) unsigned char truckSeats[edf::kSeatStride*5]{},heliSeats[edf::kSeatStride*13]{},planeSeats[edf::kSeatStride*6]{};
unsigned char seatCtrl[6][0x20]{};
float* PosOf(Obj& o) noexcept {return reinterpret_cast<float*>(o.m+kPosition);}
void Place(Obj& o,float x,float y,float z) noexcept {float* p=PosOf(o);p[0]=x;p[1]=y;p[2]=z;}
void MakeObj(Obj& o) noexcept {std::memset(o.m,0,sizeof(o.m));Put<void*>(o.m,kSelfCtrl,o.ctrl);Put<int>(o.ctrl,8,1);}
void MakeVehicle(Obj& o,unsigned char* seats,unsigned count) noexcept {
    MakeObj(o);Put<void*>(o.m,kSeats,seats);Put<std::uint64_t>(o.m,kSeatCount,count);
}

// --- The world model ---
struct SquadModel { Obj* top; int alive; int aboard; Obj* in; mapcmd::Command order; int boards,dismounts,orders; };
SquadModel squads[3]{};
SquadModel* SquadOf(const void* top) noexcept {for(auto& q:squads)if(q.top && q.top->m==top)return &q;return nullptr;}
bool truckDriver=true,heliDriver=true;
float truckPost[3]{},ferryAt[3]{};bool ferryOn=false,ferryLand=false,heliGround=false;
int routePosts=0,ferries=0,ferryReleases=0,heliGuards=0,withdrawals=0,canopies=0,canopyFrees=0,jetLeaves=0,moves=0;
mapcmd::Command lastHeliCommand{};
bool withdrawOk=true;

bool IsJet(const void* v) noexcept {return v==plane.m;}
bool IsHelicopter(const void* v) noexcept {return v==heli.m;}
bool NpcDrivable(const unsigned char* v) noexcept {return v==truck.m;}
bool NpcDriver(const unsigned char* v) noexcept {return v==truck.m ? truckDriver : v==heli.m ? heliDriver : v==plane.m;}
bool HeliFerry(const void* v,const float* at,bool land) noexcept {
    if(v!=heli.m)return false;
    if(!at){ferryOn=false;++ferryReleases;return true;}
    ++ferries;ferryOn=true;ferryLand=land;std::memcpy(ferryAt,at,12);return true;
}
bool HeliGrounded(const void* v) noexcept {return v==heli.m && heliGround;}
bool HeliCommand(const void* v,const Command& c,const ObjRef&) noexcept {if(v==heli.m){++heliGuards;lastHeliCommand=c;}return v==heli.m;}
bool NpcPrepareVehicleRoutePost(unsigned char* v,const float* at,float) noexcept {
    if(v!=truck.m)return false;
    ++routePosts;std::memcpy(truckPost,at,12);return true;
}
npc::navigation::Result GroundNavigate(npc::navigation::State&,const float* from,const float* to,float stop,std::uint64_t,
                                       float* waypoint,npc::navigation::Profile) noexcept {
    const float dx=to[0]-from[0],dz=to[2]-from[2],d=std::hypot(dx,dz);
    if(d<=stop){std::memcpy(waypoint,from,12);return npc::navigation::Result::arrived;}
    const float step=std::fmin(20.0f,d);
    waypoint[0]=from[0]+dx/d*step;waypoint[1]=to[1];waypoint[2]=from[2]+dz/d*step;
    return npc::navigation::Result::moving;
}
bool NpcSquadSeats(const void* top,const void* vehicle,SquadSeats* out) noexcept {
    SquadModel* q=SquadOf(top);
    if(!q || !out)return false;
    out->alive=q->alive;out->aboard=q->in && q->in->m==vehicle ? q->aboard : 0;out->onFoot=q->alive-(q->in ? q->aboard : 0);
    return true;
}
int NpcSquadBoardVehicle(const void* top,unsigned char* v) noexcept {
    SquadModel* q=SquadOf(top);
    if(!q)return 0;
    ++q->boards;
    Obj* o=v==truck.m ? &truck : v==heli.m ? &heli : nullptr;
    // They walk to their seats and sit only next to a vehicle at rest (the model: within 45 m).
    const float* vp=PosOf(*o);const float* sp=PosOf(*q->top);
    if(std::hypot(vp[0]-sp[0],vp[2]-sp[2])<=45.0f){q->in=o;q->aboard=q->alive;if(v==truck.m)truckDriver=true;}
    return q->alive;
}
int NpcSquadDismount(const void* top) noexcept {
    SquadModel* q=SquadOf(top);
    if(!q)return 0;
    ++q->dismounts;
    if(q->in){const float* vp=PosOf(*q->in);Place(*q->top,vp[0]+5.0f,vp[1],vp[2]);}
    const int off=q->aboard;q->aboard=0;q->in=nullptr;return off;
}
bool NpcSquadSetOrder(const void* top,const mapcmd::Command& c,bool) noexcept {
    SquadModel* q=SquadOf(top);
    if(!q)return false;
    q->order=c;++q->orders;if(c.order==mapcmd::Order::move)++moves;return true;
}
// --- The paratroopers ---
Obj* jumper[4]{};bool jumperFlies[4]{};bool jumperOut[4]{};
const void* NpcSquadTopOf(const void* h) noexcept {for(int i=0;i<4;++i)if(jumper[i] && jumper[i]->m==h)return squads[0].top->m;return nullptr;}
bool NpcSoldierFlies(const void* h) noexcept {for(int i=0;i<4;++i)if(jumper[i] && jumper[i]->m==h)return jumperFlies[i];return false;}
bool IsSoldierClass(const void* h) noexcept {for(const auto& o:soldier)if(o.m==h)return true;return false;}
bool HumanOnFoot(const unsigned char* h) noexcept {
    for(int i=0;i<4;++i)if(jumper[i] && jumper[i]->m==h)return jumperOut[i];
    const SquadModel* q=SquadOf(h);return !q || q->aboard==0;
}
bool NpcMoveSeat(unsigned char* v,unsigned from,int to) noexcept {
    if(v!=plane.m || to>=0)return false;
    unsigned char* seat=SeatAt(v,from);
    for(int i=0;i<4;++i)if(jumper[i] && At<const void*>(seat,kSeatRider)==jumper[i]->m) {
        jumperOut[i]=true;Put<void*>(seat,kSeatRider,nullptr);Put<void*>(seat,kSeatRiderCtrl,nullptr);
        reinterpret_cast<float*>(jumper[i]->m+0x6B0)[1]=-30.0f;   // falling out of it
        return true;
    }
    return false;
}
float ground=0.0f;
float GroundClearance(const float* p) noexcept {return p[1]-ground;}
ObjRef ChuteCanopyMake(const float*,const float*) noexcept {++canopies;return ObjRef{&canopies,&canopies};}
bool ChuteCanopyMove(const ObjRef&,const float*,const float*) noexcept {return true;}
void ChuteCanopyFree(const ObjRef& c) noexcept {if(c)++canopyFrees;}
bool JetWithdrawNow(const void* v,const char*) noexcept {if(v==plane.m)++jetLeaves;return v==plane.m;}
bool SupportWithdrawVehicle(const void*) noexcept {++withdrawals;return withdrawOk;}
}  // namespace crew

namespace {
using namespace crew;
int checks=0,failed=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++checks;
    if(!ok){++failed;std::printf("FAIL %s (%g, %g)\n",what,a,b);}
}
// A frame of the world: the truck drives to its post, the helicopter flies its ferry and lands, those aboard ride along.
void Step(int frames=1) {
    for(int f=0;f<frames;++f) {
        now+=16;++frameNo;
        TransportTick();
        float* tp=PosOf(truck);
        if(truckDriver) {
            const float dx=truckPost[0]-tp[0],dz=truckPost[2]-tp[2],d=std::hypot(dx,dz);
            const float step=std::fmin(d,0.25f);   // 15 m/s
            if(d>0.01f){tp[0]+=dx/d*step;tp[2]+=dz/d*step;}
        }
        float* hp=PosOf(heli);
        if(ferryOn && heliDriver) {
            const float goal[3]={ferryAt[0],ferryLand ? ferryAt[1] : ferryAt[1]+40.0f,ferryAt[2]};
            const float dx=goal[0]-hp[0],dy=goal[1]-hp[1],dz=goal[2]-hp[2],d=std::sqrt(dx*dx+dy*dy+dz*dz);
            const float step=std::fmin(d,0.5f);   // 30 m/s
            if(d>0.01f){hp[0]+=dx/d*step;hp[1]+=dy/d*step;hp[2]+=dz/d*step;}
            heliGround=ferryLand && std::fabs(hp[1]-ferryAt[1])<0.5f && std::hypot(hp[0]-ferryAt[0],hp[2]-ferryAt[2])<1.0f;
        }
        for(auto& q:squads)if(q.top && q.in && q.aboard>0){const float* vp=PosOf(*q.in);Place(*q.top,vp[0],vp[1],vp[2]);}
    }
}
void Reset() {
    ResetTransports();
    for(auto& q:squads)q=SquadModel{};
    truckDriver=heliDriver=true;ferryOn=ferryLand=heliGround=false;
    routePosts=ferries=ferryReleases=heliGuards=withdrawals=canopies=canopyFrees=jetLeaves=moves=0;withdrawOk=true;
}
bool Done(const void* top) noexcept {for(const auto& t:trips)for(int i=0;i<t.count;++i)if(t.riders[i].top.obj==top)return false;return true;}

void GroundTrip() {
    Reset();
    MakeVehicle(truck,truckSeats,5);Place(truck,150,0,0);MakeObj(soldier[0]);Place(soldier[0],0,0,0);
    squads[0]={&soldier[0],4,0,nullptr,{},0,0,0};
    Check(TransportPair(soldier[0].m,truck.m),"a squad paired with a truck");
    Check(TransportOf(soldier[0].m)==truck.m,"its transport");
    const mapcmd::Command nearOrder{mapcmd::Order::move,{0,0,150}};
    Check(!TransportOrder(soldier[0].m,nearOrder),"an order nearer than the auto range: they walk");
    const mapcmd::Command farOrder{mapcmd::Order::attackMove,{0,0,800}};
    Check(TransportOrder(soldier[0].m,farOrder),"a far order goes by the truck");
    Check(squads[0].order.order==mapcmd::Order::guard,"held where it is until the truck is there (no walking off)");
    TransportLink links[4];
    Check(TransportLinks(links,4)==1 && links[0].trip,"the map's line from the squad to its truck, on a trip");
    for(int i=0;i<20000 && !Done(soldier[0].m);++i)Step();
    Check(Done(soldier[0].m),"the trip ends");
    Check(squads[0].boards>0 && squads[0].dismounts>0,"they boarded and got off",squads[0].boards,squads[0].dismounts);
    const float* tp=PosOf(truck);
    Check(std::fabs(tp[2]-760.0f)<10.0f && std::fabs(tp[0])<10.0f,"the truck stopped 40 m short of the point",tp[0],tp[2]);
    Check(squads[0].order.order==mapcmd::Order::attackMove && squads[0].order.at[2]==800.0f,"off, the squad carries out its attack-move");
    Check(squads[0].aboard==0 && TransportOf(soldier[0].m)==truck.m,"on foot, still paired with the truck waiting there");
    // Aboard, a near order: they ride to it (here: get off where it stands).
    squads[0].in=&truck;squads[0].aboard=4;
    Check(TransportOrder(soldier[0].m,mapcmd::Command{mapcmd::Order::guard,{0,0,780}}),"aboard, even a near order goes by it");
    for(int i=0;i<2000 && !Done(soldier[0].m);++i)Step();
    Check(Done(soldier[0].m) && squads[0].aboard==0,"...they get off at once and guard the point");
}

void EmptyTruck() {
    Reset();
    MakeVehicle(truck,truckSeats,5);Place(truck,300,0,0);MakeObj(soldier[0]);Place(soldier[0],0,0,0);
    squads[0]={&soldier[0],4,0,nullptr,{},0,0,0};
    truckDriver=false;   // nobody aboard to drive it: the squad walks to it
    TransportPair(soldier[0].m,truck.m);
    Check(TransportOrder(soldier[0].m,mapcmd::Command{mapcmd::Order::move,{0,0,900}}),"an empty truck still takes the order");
    for(int i=0;i<10;++i)Step();
    Check(moves>0 && squads[0].order.order==mapcmd::Order::move && squads[0].order.at[0]==300.0f,"the squad sent walking to the empty truck",
          squads[0].order.at[0]);
    Place(soldier[0],290,0,0);   // there
    for(int i=0;i<20000 && !Done(soldier[0].m);++i)Step();
    Check(squads[0].dismounts>0 && std::fabs(PosOf(truck)[2]-860.0f)<10.0f,"one of them drove it there",PosOf(truck)[2]);
}

void HeliAssault() {
    Reset();
    MakeVehicle(heli,heliSeats,13);Place(heli,-1400,150,0);heliGround=false;
    MakeObj(soldier[0]);MakeObj(soldier[1]);
    squads[0]={&soldier[0],4,4,&heli,{},0,0,0};squads[1]={&soldier[1],4,4,&heli,{},0,0,0};
    const void* tops[2]={soldier[0].m,soldier[1].m};
    const float target[3]={0,0,0};
    Check(TransportDeliver(heli.m,tops,2,target),"a helicopter assault handed over (two squads aboard)");
    Check(TransportOf(soldier[1].m)==heli.m,"both paired with it");
    bool landedFirst=true;
    for(int i=0;i<40000 && !(Done(soldier[0].m) && Done(soldier[1].m));++i) {
        Step();
        if((squads[0].dismounts || squads[1].dismounts) && !heliGround && ferryOn)landedFirst=false;
    }
    Check(Done(soldier[0].m) && Done(soldier[1].m),"the assault ends");
    Check(squads[0].dismounts>0 && squads[1].dismounts>0 && landedFirst,"both squads got off, on the ground");
    const float* hp=PosOf(heli);
    Check(std::hypot(hp[0]+60.0f,hp[2])<5.0f,"it landed 60 m short of the point on its way in",hp[0],hp[2]);
    Check(squads[0].order.order==mapcmd::Order::guard && squads[1].order.order==mapcmd::Order::guard,"each guards the point");
    Check(ferryReleases>0 && heliGuards>0 && lastHeliCommand.order==mapcmd::Order::guard,"the helicopter's ferry over: it guards the drop point");
    // A far order later: it comes for them (lands beside them), they board, it flies, lands, they get off.
    Place(heli,-60,0,0);Place(soldier[0],-50,0,30);heliGround=true;
    const int boards=squads[0].boards;
    Check(TransportOrder(soldier[0].m,mapcmd::Command{mapcmd::Order::move,{0,0,1200}}),"a later far order goes by the helicopter");
    for(int i=0;i<40000 && !Done(soldier[0].m);++i)Step();
    Check(squads[0].boards>boards && std::hypot(PosOf(heli)[0],PosOf(heli)[2]-1140.0f)<5.0f,"picked up and set down 60 m short",
          PosOf(heli)[0],PosOf(heli)[2]);
    Check(squads[1].aboard==0 && squads[1].order.order==mapcmd::Order::guard,"the other squad stayed where it was");
}

void Withdraw() {
    Reset();
    MakeVehicle(heli,heliSeats,13);Place(heli,0,0,0);heliGround=true;MakeObj(soldier[0]);
    squads[0]={&soldier[0],4,4,&heli,{},0,0,0};
    TransportPair(soldier[0].m,heli.m);
    Check(TransportWithdraw(soldier[0].m)==NpcCommandReason::none,"WITHDRAW with the squad aboard a landed helicopter");
    Check(squads[0].aboard==0 && withdrawals==1 && !TransportOf(soldier[0].m),"they got off first, it left, the pair is gone");
    Reset();
    MakeVehicle(heli,heliSeats,13);Place(heli,0,100,0);heliGround=false;MakeObj(soldier[0]);
    squads[0]={&soldier[0],4,4,&heli,{},0,0,0};
    TransportPair(soldier[0].m,heli.m);
    Check(TransportWithdraw(soldier[0].m)==NpcCommandReason::noTransport && withdrawals==0 && squads[0].aboard==4,
          "aloft with the squad aboard: refused (it would fly off with them)");
    heliGround=true;withdrawOk=false;
    Check(TransportWithdraw(soldier[0].m)==NpcCommandReason::noTransport && TransportOf(soldier[0].m)==heli.m,
          "not a support vehicle: refused, the pair kept");
    Check(TransportWithdraw(soldier[1].m)==NpcCommandReason::noTransport,"no transport: refused");
}

void Succession() {
    Reset();
    MakeVehicle(truck,truckSeats,5);MakeObj(soldier[0]);MakeObj(soldier[1]);
    squads[0]={&soldier[0],4,0,nullptr,{},0,0,0};
    TransportPair(soldier[0].m,truck.m);
    TransportSucceed(soldier[0].m,soldier[1].m);
    Check(TransportOf(soldier[1].m)==truck.m && !TransportOf(soldier[0].m),"its new leader keeps the truck");
    TransportSucceed(soldier[1].m,nullptr);
    Check(!TransportOf(soldier[1].m),"joined another squad: the pair is gone");
    Check(!TransportPair(soldier[3].m,plane.m),"a plane is no squad's transport");
}

void Paradrop() {
    Reset();
    MakeVehicle(plane,planeSeats,5);Place(plane,-1500,150,0);
    MakeObj(soldier[0]);squads[0]={&soldier[0],4,4,&plane,{},0,0,0};
    for(int i=0;i<4;++i) {
        MakeObj(soldier[8+i]);jumper[i]=&soldier[8+i];jumperOut[i]=false;jumperFlies[i]=i==2;   // the third: a Wing Diver
        unsigned char* seat=SeatAt(plane.m,static_cast<unsigned>(i+1));
        Put<void*>(seat,kSeatRider,jumper[i]->m);Put<void*>(seat,kSeatRiderCtrl,seatCtrl[i]);Put<int>(seatCtrl[i],8,1);
        Place(*jumper[i],0,150,0);
    }
    const float target[3]={0,0,0};
    Check(TransportParadrop(plane.m,target),"a paratroop drop over the point");
    Step(10);
    Check(!jumperOut[0],"nobody out far from the point");
    Place(plane,-300,150,0);
    Step(1);
    Check(jumperOut[0] && !jumperOut[1],"the first jumps within 400 m, one at a time");
    for(int i=0;i<200;++i)Step();
    Check(jumperOut[1] && jumperOut[2] && jumperOut[3],"the stick goes on");
    Check(jetLeaves==1 && squads[0].order.order==mapcmd::Order::guard,"all out: the plane leaves, the squad guards the point");
    // Under the canopy: falling no faster than the sink; the Wing Diver has none.
    for(int i=0;i<4;++i)reinterpret_cast<float*>(jumper[i]->m+0x6B0)[1]=-30.0f;
    Step(1);
    Check(canopies==3,"a canopy over each but the Wing Diver",canopies);
    Check(reinterpret_cast<float*>(jumper[0]->m+0x6B0)[1]==-6.0f && reinterpret_cast<float*>(jumper[2]->m+0x6B0)[1]==-30.0f,
          "the fall held to 6 m/s, the Wing Diver's untouched");
    for(int i=0;i<4;++i)Place(*jumper[i],0,0.5f,0);
    Step(30);
    Check(canopyFrees==3,"down: the canopies gone",canopyFrees);
}
}  // namespace

int main() {
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    config.transportAutoRange=200.0f;
    GroundTrip();EmptyTruck();HeliAssault();Withdraw();Succession();Paradrop();
    std::printf("transport_runtime: %d checks, %d failed\n",checks,failed);
    return failed ? 1 : 0;
}
