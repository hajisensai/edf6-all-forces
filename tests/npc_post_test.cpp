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
const void* focusEnemy=nullptr;
bool VisitEnemies(const unsigned char*,EnemyVisitor visit,void* context) noexcept {if(focusEnemy)visit(context,focusEnemy,nullptr);return true;}
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
    {   // 0x548D50's stand-in (the activation PrepareNpcVehicleAi runs): its real prologue, then +0x4A0 = 1 and return.
        static const unsigned char stub[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x80,0xB9,0xA0,0x04,0x00,0x00,0x00,0x48,0x8B,0xD9,
                                           0xC6,0x83,0xA0,0x04,0x00,0x00,0x01,0x48,0x83,0xC4,0x20,0x5B,0xC3};
        std::memcpy(image+kActivate,stub,sizeof(stub));
        DWORD was=0;
        Check(VirtualProtect(image+kActivate,sizeof(stub),PAGE_EXECUTE_READWRITE,&was)!=0,"stand-in activation executable");
    }
    std::memcpy(image+kAttackRead,kAttackReadSig,sizeof(kAttackReadSig));
    std::memcpy(image+kAttackRelease,kAttackReleaseSig,sizeof(kAttackReleaseSig));
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
    Check(listed()==1,"client can select the remote NPC vehicle without writing its input");
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
    // Native firing must never trust a non-null origin outside the current model's bone array.
    auto* mv=reinterpret_cast<void**>(image+0x17DA960);Put<void**>(m,0,mv);
    std::memcpy(image+kAttackRead,kAttackReadSig,sizeof(kAttackReadSig));
    std::memcpy(image+kAttackRelease,kAttackReleaseSig,sizeof(kAttackReleaseSig));
    alignas(16) unsigned char bones[2*kBoneStride]{},replacement[2*kBoneStride]{};
    const wchar_t spine[16]=L"spine",body[16]=L"body";
    Put<const wchar_t*>(bones,0,spine);Put<const wchar_t*>(bones,kBoneStride,body);
    Put<const wchar_t*>(replacement,0,spine);Put<const wchar_t*>(replacement,kBoneStride,body);
    Put<void*>(m+kModelInst506,kInstBones506,bones);Put<int>(m+kModelInst506,kInstBoneCount,2);
    Put<std::uint64_t>(m,kAttackSize,1);Put<std::uintptr_t>(m,kAimOrigin,0x70000481DULL);
    Check(PrepareNpcVehicleAi(m) && At<void*>(m,kAimOrigin)==bones,"non-null corrupted firing origin restored from actual spine");
    Put<void*>(m+kModelInst506,kInstBones506,replacement);
    Check(PrepareNpcVehicleAi(m) && At<void*>(m,kAimOrigin)==replacement,"model replacement refreshes stale firing bone");
    Put<void*>(m,kAimOrigin,replacement+1);
    Check(PrepareNpcVehicleAi(m) && At<void*>(m,kAimOrigin)==replacement,"misaligned pointer is not a bone record");
    Put<void*>(m,kAimOrigin,replacement+kBoneStride);
    Check(PrepareNpcVehicleAi(m) && At<void*>(m,kAimOrigin)==replacement+kBoneStride,"native valid custom origin is preserved");
    const wchar_t neck[16]=L"neck",arm[16]=L"arm";
    Put<void*>(m,kAimOrigin,nullptr);Put<const wchar_t*>(replacement,0,arm);Put<const wchar_t*>(replacement,kBoneStride,arm);
    Check(!PrepareNpcVehicleAi(m) && !At<void*>(m,kAimOrigin),"missing origin prevents native fire instead of dereferencing garbage");
    // V614_PROTEUS_MK2 has no spine: its seat-0 aim bone of begaruta_pilot_class (neck) is the origin (report #12).
    Put<const wchar_t*>(replacement,kBoneStride,neck);
    Check(PrepareNpcVehicleAi(m) && At<void*>(m,kAimOrigin)==replacement+kBoneStride,"a model with no spine fires from its neck");
    Put<void*>(m,kAimOrigin,nullptr);Put<const wchar_t*>(replacement,0,spine);Put<const wchar_t*>(replacement,kBoneStride,body);
    Check(PrepareNpcVehicleAi(m) && At<void*>(m,kAimOrigin)==replacement,"valid model becomes fire-ready again (spine first)");
    // The Proteus's weapons are on seats 1-3 and none on seat 0 (vehicle_weapon_setting): its NPC driver's table holds a
    // row per gunner seat's weapon (its AI think runs the fire entry for seats 1..n-1), not an empty seat-0 table.
    {
        alignas(16) static unsigned char pseats[4*kSeatStride]{},pholder[3][0x20]{},pweapon[3][0x230]{};
        static void* pholders[3][1]{};
        alignas(16) static unsigned char rowsBuf[4*16]{};
        std::memcpy(pseats,mseat,kSeatStride);   // seat 0: the same NPC driver, no weapon
        for(int k=1;k<=3;++k) {
            unsigned char* const s=pseats+k*kSeatStride;
            pholders[k-1][0]=pholder[k-1];Put<void*>(pholder[k-1],kHolderWeapon,pweapon[k-1]);Put<float>(pweapon[k-1],kWeaponReach,500.0f);
            Put<void*>(s,kSeatWeapons,pholders[k-1]);Put<std::uint64_t>(s,kSeatWeaponCount,1);
        }
        Put<void*>(m,kSeats,pseats);Put<std::uint64_t>(m,kSeatCount,4);
        Put<std::uint64_t>(m,kAttackSize,0);Put<void*>(m,kAttackData,rowsBuf);Put<std::uint64_t>(m,kAttackCap,4);
        Check(PrepareNpcVehicleAi(m) && At<std::uint64_t>(m,kAttackSize)==3,"a Proteus-like mech gets one row per gunner seat weapon");
        const auto* rows=reinterpret_cast<const AttackRow*>(rowsBuf);
        Check(rows[0].seat==1 && rows[1].seat==2 && rows[2].seat==3 && rows[0].weapon==0 && rows[2].maxRange==500.0f,
              "the rows name their own seats");
        Put<void*>(m,kSeats,mseat);Put<std::uint64_t>(m,kSeatCount,1);Put<std::uint64_t>(m,kAttackSize,1);
    }
    // An NPC driver's vehicle is activated (+0x4A0, the stock fire gates' "engaged") through the stock 0x548D50, as a
    // CreateFriend AI vehicle is at its spawn; once. The stand-in: the real prologue, then +0x4A0 = 1 and return.
    {
        m[kEngaged]=0;
        Check(PrepareNpcVehicleAi(m) && m[kEngaged]==1,"an NPC-driven vehicle not yet engaged is activated before its AI pass");
        m[kEngaged]=2;   // already engaged: 0x548D50 is not run again
        Check(PrepareNpcVehicleAi(m) && m[kEngaged]==2,"an engaged vehicle is left as it is");
        m[kEngaged]=0;
    }
    // Turning off automatic return must not remove a crewed vehicle from the command map.
    config.tankReturnToPost=false;ResetNpcPosts();neutral();NpcPostInput(v);
    Check(listed()==1 && TankCommand(v,guard),"explicit map orders remain available with automatic return off");
    Put<void*>(target,kSelfCtrl,otherCtrl);Put<int>(otherCtrl,8,1);Put<int>(otherCtrl,12,1);focusEnemy=target;
    const ObjRef focused=ObjRef::Of(target);Command focus{Order::focus,{0,0,0}};
    Check(TankCommand(v,focus,focused),"NPC vehicle accepts a live enemy focus");
    Check(At<int>(otherCtrl,12)==2,"map focus owns one weak reference");
    Check(PrepareNpcVehicleAi(v) && At<void*>(v,kTarget)==target,"map focus reaches the native AI target");
    focusEnemy=nullptr;
    Check(!TankCommand(v,focus,focused),"friendly or absent target cannot be focused");
    Check(TankCommand(v,Command{}),"release clears focus");
    Check(At<int>(otherCtrl,12)==2,"clearing map focus releases its reference while native AI retains its own");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("npc_post_test: %d checks passed\n",checks);
}
