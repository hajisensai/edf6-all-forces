// Production return-to-post adapter, synthetic object/seat memory, real seat classification. No game calls.
#include "../src/npcpost.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace crew {
unsigned char* image=nullptr;
Config config{};
ULONGLONG now=1000;
bool sessionOn=false,host=true;
const Config& Cfg() noexcept { return config; }
ULONGLONG GameMs() noexcept { return now; }
bool InSession() noexcept { return sessionOn; }
bool OnlineHostOnly() noexcept { return !sessionOn || host; }
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
    Check(At<float>(seat,0x2C4)>0 && listed()==0,"host returns tank but cannot use offline map commands");
    sessionOn=false;config.customNpcAi=false;Check(listed()==0 && !TankCommand(v,guard),"disabled AI cannot be commanded");
    ResetNpcPosts();Check(!posts[0].ref,"mission reset drops identity and orders");
    // A mech of the Begaruta family (slot 4 0x644350, no slot 72): no CarBase drive bit, and frames with no AI pass
    // clear nothing, so the plugin's own last stick must not read as the stock AI driving (2026-10-07).
    config.customNpcAi=true;
    alignas(16) unsigned char m[0x2700]{},mseat[kSeatStride]{},mrider[0x400]{},mctrl[0x20]{};
    void* mvt[5]{};mvt[4]=image+0x644350;
    Put<void**>(m,0,mvt);Put<void*>(m,kSelfCtrl,mctrl);
    Put<void*>(m,kSeats,mseat);Put<std::uint64_t>(m,kSeatCount,1);
    Put<void*>(mseat,kSeatRider,mrider);Put<void*>(mseat,kSeatRiderCtrl,mctrl);Put<int>(mctrl,8,1);
    Put<void*>(mrider,0,image+edf::kDummyRiderVtable);
    Put<float>(m,kMatrix,1);Put<float>(m,kMatrix+40,1);
    Check(NpcDrivable(m),"a Begaruta-family mech is NPC-drivable");
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
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("npc_post_test: %d checks passed\n",checks);
}
