// Production return-to-post adapter, synthetic object/seat memory, real seat classification. No game calls.
#include "../src/npcpost.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace crew {
bool NpcDriver(const unsigned char* v) noexcept { return v && SeatCount(v)>0 && SeatRider(SeatAt(const_cast<unsigned char*>(v),0))==Rider::dummy; }
bool JetFliesItself(const void*) noexcept { return false; }   // no plugin jet in this world (command_unit.cpp)

unsigned char* image=nullptr;
Config config{};
ULONGLONG now=1000;
bool sessionOn=false,host=true;
const Config& Cfg() noexcept { return config; }
ULONGLONG GameMs() noexcept { return now; }
bool InSession() noexcept { return sessionOn; }
bool OnlineHostOnly() noexcept { return !sessionOn || host; }
bool IsOnlineAuthority(const void*) noexcept { return !sessionOn || host; }
void Log(const char*,...) noexcept {}
}
namespace {
int checks=0;
void Check(bool b,const char* why) { ++checks;if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);} }
}
int main() {
    using namespace crew;
    // A real allocated image span makes every image+RVA pointer valid C++ pointer arithmetic; no code is executed.
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(image!=nullptr,"fixture image allocated");
    alignas(16) unsigned char v[0x2700]{},seat[kSeatStride]{},rider[0x400]{},ctrl[0x20]{},otherCtrl[0x20]{};
    void* vt[73]{};vt[72]=image+0x661440;
    Put<void**>(v,0,vt);Put<void*>(v,kSelfCtrl,ctrl);
    Put<void*>(v,kSeats,seat);Put<std::uint64_t>(v,kSeatCount,1);
    Put<void*>(seat,kSeatRider,rider);Put<void*>(seat,kSeatRiderCtrl,ctrl);Put<int>(ctrl,8,1);
    Put<void*>(rider,0,image+edf::kDummyRiderVtable);
    Put<float>(v,kMatrix,1);Put<float>(v,kMatrix+40,1);Put<unsigned>(v,0x1AD0,1);
    auto neutral=[&](){Put<float>(seat,0x2C0,0);Put<float>(seat,0x2C4,0);++now;};
    auto listed=[&](){CommandUnit u[1]{};return TankCommandUnits(u,1);};
    NpcPostInput(v);Check(listed()==1,"NPC tank listed");
    Put<float>(v,kPosition+8,15);neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)>0,"recoil behind post causes reverse drive");
    neutral();Put<float>(seat,0x2C0,0.7f);NpcPostInput(v);
    Check(At<float>(seat,0x2C0)==0.7f && At<float>(seat,0x2C4)==0,"stock turret-alignment turn owns this frame");
    neutral();Put<float>(seat,0x2C4,-0.5f);NpcPostInput(v);
    Check(At<float>(seat,0x2C4)==-0.5f,"stock throttle owns this frame");
    // An enemy far beyond a gun's reach, but inside the turret arc, must not prevent returning.
    unsigned char target[0x100]{},holder[0x20]{},weapon[0x230]{};void* holders[]={holder};
    Put<float>(target,kPosition+8,1000);Put<void*>(v,0x518,target);Put<void*>(v,0x520,ctrl);
    Put<void*>(seat,0xC8,holders);Put<std::uint64_t>(seat,0xD8,1);Put<void*>(holder,0x10,weapon);Put<float>(weapon,0x224,50);
    neutral();NpcPostInput(v);Check(At<float>(seat,0x2C4)>0,"far enemy does not suppress neutral-frame return");
    Command guard{Order::guard,{100,0,100}};
    Check(TankCommand(v,guard),"live tank accepts guard");
    Put<void*>(v,0x4A8,target);neutral();NpcPostInput(v);
    Check(listed()==0 && !TankCommand(v,guard),"script-controlled tank immediately loses command availability");
    Put<void*>(v,0x4A8,nullptr);neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)==0 && posts[0].at[2]==15 && posts[0].cmd.order==Order::none,
          "short route clears old order and adopts current position, not nearby old spawn");
    Put<void*>(seat,kSeatRiderCtrl,nullptr);neutral();NpcPostInput(v);
    Check(listed()==0 && !TankCommand(v,guard),"empty/player-owned driver cannot be commanded");
    Put<void*>(seat,kSeatRiderCtrl,ctrl);Put<float>(v,kPosition+8,25);neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)==0 && posts[0].at[2]==25,"new NPC driver starts at current position");
    v[kDead]=1;Check(listed()==0 && !TankCommand(v,guard),"dead tank rejected before next input");v[kDead]=0;
    Put<void*>(v,kSelfCtrl,otherCtrl);Check(listed()==0 && !TankCommand(v,guard),"reused object address rejected");Put<void*>(v,kSelfCtrl,ctrl);
    now+=501;Check(listed()==0 && !TankCommand(v,guard),"stale tank rejected");
    neutral();NpcPostInput(v);sessionOn=true;host=false;neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)==0 && !TankCommand(v,guard),"client cannot drive or command");
    host=true;Put<float>(v,kPosition+8,45);neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)>0 && listed()==1,"AI authority can return and command its tank online");
    sessionOn=false;config.customNpcAi=false;Check(listed()==0 && !TankCommand(v,guard),"disabled AI cannot be commanded");
    ResetNpcPosts();Check(!posts[0].ref,"mission reset drops identity and orders");
    // A mech of the Begaruta family (slot 4 0x644350, no slot 72): no CarBase drive bit, and frames with no AI pass
    // clear nothing, so the plugin's own last stick must not read as the stock AI driving (2026-10-07).
    config.customNpcAi=true;
    alignas(16) unsigned char m[0x2700]{},mseat[kSeatStride]{},mrider[0x400]{},mctrl[0x20]{};
    Put<void*>(m,kSelfCtrl,mctrl);
    Put<void*>(m,kSeats,mseat);Put<std::uint64_t>(m,kSeatCount,1);
    Put<void*>(mseat,kSeatRider,mrider);Put<void*>(mseat,kSeatRiderCtrl,mctrl);Put<int>(mctrl,8,1);
    Put<void*>(mrider,0,image+edf::kDummyRiderVtable);
    Put<float>(m,kMatrix,1);Put<float>(m,kMatrix+40,1);
    // InstallInputs replaces slot 4 before any NpcPostInput/SeatSwitchFrame runs. Use each real family
    // vtable and replace that slot just as the installed hook does; its stock function is no longer there.
    const unsigned mechVtables[]={0x17DA960,0x17DD440,0x17DE0A8,0x17DEC40};
    for(const unsigned rva:mechVtables) {
        auto mvt=reinterpret_cast<void**>(image+rva);
        Put<void**>(m,0,mvt);
        mvt[4]=image+0x644350;
        Check(NpcDrivable(m),"each stock Begaruta-family mech is NPC-drivable");
        mvt[4]=image+0x100;   // stand-in InputHook, never called
        Check(NpcDrivable(m),"a hooked Begaruta-family mech remains NPC-drivable");
        ResetNpcPosts();++now;NpcPostInput(m);
        CommandUnit units[1]{};
        Check(TankCommandUnits(units,1)==1 && std::strcmp(units[0].name,"MECH")==0,
              "each hooked mech is still listed as MECH");
        Check(TankCommand(m,guard),"each hooked mech accepts map orders");
    }
    ResetNpcPosts();
    ++now;NpcPostInput(m);
    {CommandUnit u[1]{};Check(TankCommandUnits(u,1)==1 && std::strcmp(u[0].name,"MECH")==0,"the mech listed on the map as MECH");}
    Put<float>(m,kPosition+8,-40);++now;NpcPostInput(m);
    const float steer=At<float>(mseat,0x2C0),throttle=At<float>(mseat,0x2C4);
    Check(throttle<0,"a mech behind its post drives forward (no drive bit needed)");
    ++now;NpcPostInput(m);   // no AI pass: nothing cleared the stick
    Check(At<float>(mseat,0x2C0)==steer && At<float>(mseat,0x2C4)==throttle && posts[0].active,
          "its own last stick is not the stock AI driving: it keeps driving");
    Put<float>(m,kPosition+8,0);++now;NpcPostInput(m);
    Check(At<float>(mseat,0x2C0)==0 && At<float>(mseat,0x2C4)==0 && !posts[0].active,"back on its post: the stick taken back");
    Put<float>(m,kPosition+8,-40);Put<float>(mseat,0x2C0,0.7f);++now;NpcPostInput(m);
    Check(At<float>(mseat,0x2C0)==0.7f && At<float>(mseat,0x2C4)==0,"the stock AI's own turn owns the frame");
    ResetNpcPosts();Put<void*>(mseat,kSeatRiderCtrl,nullptr);
    const float arrival[3]={0,0,120};
    Check(NpcPrepareVehiclePost(m,arrival),"support arrival accepted before its real driver boards");
    ++now;NpcPostInput(m);
    Check(pendingPosts[0].ref.Is(m),"empty vehicle keeps its pending arrival without moving");
    Put<void*>(mseat,kSeatRiderCtrl,mctrl);Put<float>(mseat,0x2C0,0);Put<float>(mseat,0x2C4,0);
    ++now;NpcPostInput(m);
    Check(!pendingPosts[0].ref && posts[0].at[2]==120 && posts[0].commanded,
          "arrival transfers to vehicle controller only once crew is seated");
    ResetNpcPosts();Check(!pendingPosts[0].ref,"new mission drops pending support destinations");
    // Production reproduction: a 4 m grid waypoint falls inside the ordinary 6 m guard hold.
    ResetNpcPosts();config.tankPostHold=6.0f;sessionOn=false;host=true;
    Put<float>(v,kPosition,0);Put<float>(v,kPosition+8,0);Put<void*>(v,0x4A8,nullptr);
    const float gridPoint[3]={0,0,4};
    Check(NpcPrepareVehiclePost(v,gridPoint),"ordinary post request accepted");neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)==0,"ordinary six metre guard radius is preserved");
    Check(NpcPrepareVehicleRoutePost(v,gridPoint,1.0f),"route supplies its own one metre arrival radius");neutral();NpcPostInput(v);
    const float routeThrottle=At<float>(seat,0x2C4);
    Check(routeThrottle<0 && routeThrottle>=-0.5f,"four metre route edge produces slow forward throttle");
    Put<float>(v,kPosition+8,2.6f);neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)<0,"driver continues inside planner 1.5m advancement radius until next waypoint arrives");
    Put<float>(v,kPosition+8,3.1f);++now;NpcPostInput(v);
    Check(At<float>(seat,0x2C4)==0 && !posts[0].active,"route stops and releases its throttle within explicit radius");
    Put<float>(v,kPosition+8,0);Check(NpcPrepareVehicleRoutePost(v,gridPoint,0.25f),"narrow route arrival is also explicit");neutral();NpcPostInput(v);
    Check(At<float>(seat,0x2C4)<0,"legacy 0.45m planner can use smaller 0.25m driver radius");
    const float stationary[3]={0,0,0};Check(NpcPrepareVehicleRoutePost(v,stationary,1.0f),"route waiting request uses current point");
    ++now;NpcPostInput(v);Check(At<float>(seat,0x2C4)==0,"route wait removes prior throttle before any new path is accepted");
    Check(!NpcPrepareVehicleRoutePost(v,gridPoint,0) && !NpcPrepareVehicleRoutePost(v,gridPoint,-1) &&
          !NpcPrepareVehicleRoutePost(v,gridPoint,std::numeric_limits<float>::quiet_NaN()),"invalid route radius rejected");
    Check(NpcPrepareVehicleRoutePost(v,gridPoint,1.0f),"pending route queued before user guard");
    Check(NpcPostCommand(v,gridPoint),"explicit user guard overrides route mode");neutral();NpcPostInput(v);
    Check(posts[0].routeArrival==0 && At<float>(seat,0x2C4)==0 && !pendingPosts[0].ref,
          "user guard restores six metre hold and removes a queued old route");
    Check(NpcPrepareVehicleRoutePost(v,gridPoint,1.0f),"route queued before driver changes");
    Put<void*>(rider,0,nullptr);rider[edf::kHumanPlayer]=1;Put<std::uint16_t>(rider,0x128,1);
    Put<float>(seat,0x2C4,0.73f);++now;NpcPostInput(v);
    Check(At<float>(seat,0x2C4)==0.73f && !pendingPosts[0].ref,"player takeover preserves human input and cancels pending route");
    Check(!NpcPrepareVehicleRoutePost(v,gridPoint,1.0f),"route producer cannot enqueue more waypoints under player control");
    rider[edf::kHumanPlayer]=0;Put<std::uint16_t>(rider,0x128,0);Put<void*>(rider,0,image+edf::kDummyRiderVtable);
    neutral();NpcPostInput(v);
    Check(posts[0].routeArrival==0 && At<float>(seat,0x2C4)==0,"new NPC after takeover does not resume stale route");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("npc_post_test: %d checks passed\n",checks);
}
