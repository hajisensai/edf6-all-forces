// Run the production SidecarBoard against stand-in humans and bikes, without EDF.dll. The warp entry records
// where the human went. In particular, FindSeat is visited for ALL friendly objects, including those far away,
// and returning nullptr after a sidecar take does not stop that visit. No game is started.
#include "../src/sidecar.cpp"
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace crew {
unsigned char* image=nullptr;
PlayerFix player{};
namespace {
Config config{};
unsigned char human[0x1600]{},otherHuman[0x1600]{},bike[0x3000]{},second[0x3000]{},ordinary[0x3000]{},seat[kSeatStride]{};
unsigned char humanRef[16]{},otherRef[16]{},bikeRef[16]{},newRef[16]{};
constexpr std::size_t kFixtureFoot=0x780;  // separate physics pose: +0x90 really is stale at MoveIntent
unsigned char riderCtrl[0x10]{};
const void* boardingOnly=nullptr;
bool doorReadable=true;
float doorReach=2.3f;
int warps=0,failures=0;
void __fastcall WarpRec(void* ctrl,const float* matrix) {
    auto h=static_cast<unsigned char*>(ctrl)-kHumanCtrl;
    std::memcpy(h+kPosition,matrix+12,12);
    std::memcpy(h+kFixtureFoot,matrix+12,12);
    ++warps;
}
std::uintptr_t __fastcall MoveRec(void*) { return 0; }
float* __fastcall PositionRec(void* ctrl,float* out) {
    std::memcpy(out,static_cast<unsigned char*>(ctrl)-kHumanCtrl+kFixtureFoot,12);out[3]=1.0f;return out;
}
int damageCalls=0;
void __fastcall DamageRec(void*,const void*,void*) { ++damageCalls; }
// The game's 0x11B8D90: the walk controller's one-step velocity, ctrl+0x60 += v (xyz).
constexpr std::size_t kStepVel=0x60;
void __fastcall AddStepRec(void* ctrl,const float* v) {
    auto f=reinterpret_cast<float*>(static_cast<unsigned char*>(ctrl)+kStepVel);
    for(int c=0;c<3;++c)f[c]+=v[c];
}
void Jump(unsigned rva,const void* to) {
    unsigned char* p=image+rva;
    p[0]=0x48;p[1]=0xB8;std::memcpy(p+2,&to,8);p[10]=0xFF;p[11]=0xE0;
}
void Expect(bool pass,const char* what) {
    std::printf("%s: %s\n",pass ? "PASS" : "FAIL",what);
    if(!pass)++failures;
}
void HumanAt(float x,float y,float z) {
    const float p[3]={x,y,z};std::memcpy(human+kPosition,p,12);
    std::memcpy(human+kFixtureFoot,p,12);
}
void Reset() {
    ResetSidecars();warps=0;doorReadable=true;doorReach=2.3f;boardingOnly=nullptr;
    std::memset(human,0,sizeof(human));std::memset(bike,0,sizeof(bike));std::memset(second,0,sizeof(second));
    std::memset(otherHuman,0,sizeof(otherHuman));damageCalls=0;
    Put<void*>(human,kSelfCtrl,humanRef);Put<void*>(otherHuman,kSelfCtrl,otherRef);Put<void*>(bike,kSelfCtrl,bikeRef);
    for(auto ctrl:{humanRef,otherRef,bikeRef,newRef})Put<int>(ctrl,8,1);
    std::memset(seat,0,sizeof(seat));
    Put<void*>(human,kHumanPad,human);Put<unsigned char>(human,kHumanPlayer,1);
    Put<void*>(otherHuman,kHumanPad,otherHuman);Put<unsigned char>(otherHuman,kHumanPlayer,1);
    for(auto v:{bike,second}) {
        Put<unsigned char*>(v,kSeats,seat);Put<std::uint64_t>(v,kSeatCount,1);
        Put<float>(v,kMatrix,1.0f);Put<float>(v,kMatrix+20,1.0f);Put<float>(v,kMatrix+40,1.0f);
    }
    sidecars[0].ref=ObjRef::Of(bike);sidecars[0].marked=true;sidecars[0].seen=GameMs();
    sidecars[1].ref=ObjRef::Of(second);sidecars[1].marked=true;sidecars[1].seen=GameMs();
}
// The bike at heading `yaw` (x its left, y up, z forward: the frame FramePoint reads), at `at`, `speed` m/s ahead.
void Pose(unsigned char* v,float yaw,const float* at,float speed) {
    const float rows[3][3]={{std::cos(yaw),0.0f,-std::sin(yaw)},{0.0f,1.0f,0.0f},{std::sin(yaw),0.0f,std::cos(yaw)}};
    for(int r=0;r<3;++r)std::memcpy(v+kMatrix+r*16,rows[r],12);
    std::memcpy(v+kPosition,at,12);
    const float vel[3]={speed*rows[2][0],0.0f,speed*rows[2][2]};
    std::memcpy(v+kChassisVel,vel,12);
}
// A held gunner riding a moving bike for `frames` steps of 1/60 s: each frame the pre-update (MoveIntent, which
// zeroes the walk and adds the step velocity), the bike's input (Hold), then the physics step moves the character
// by its step velocity (the controller's +0x60, then cleared, as 0x11B9A92 / 0x11B9CB7) and the bike by its own.
// Returns the farthest the gunner got from the gunner's point after a step; `warpsOut` the warps it took.
float Ride(float speed,float yawRate,int frames,int& warpsOut) {
    Reset();
    float at[3]={0.0f,0.0f,0.0f},yaw=0.0f;
    Pose(bike,yaw,at,speed);
    float p[3];FramePoint(bike,kGunnerX,kGunnerY,kGunnerZ,p);HumanAt(p[0],p[1],p[2]);
    Take(sidecars[0],bike,human,false);
    warps=0;
    float worst=0.0f;
    for(int i=0;i<frames;++i) {
        MoveIntent(human);
        Hold(sidecars[0],bike);
        auto step=reinterpret_cast<float*>(human+kHumanCtrl+kStepVel);
        auto pos=reinterpret_cast<float*>(human+kFixtureFoot);
        std::memcpy(human+kPosition,pos,12); // native refresh later in pre-update, before the physics step
        for(int c=0;c<3;++c){pos[c]+=step[c]/60.0f;step[c]=0.0f;}
        const float* vel=reinterpret_cast<const float*>(bike+kChassisVel);
        for(int c=0;c<3;++c)at[c]+=vel[c]/60.0f;
        yaw+=yawRate/60.0f;
        Pose(bike,yaw,at,speed);
        FramePoint(bike,kGunnerX,kGunnerY,kGunnerZ,p);
        const float d[3]={pos[0]-p[0],pos[1]-p[1],pos[2]-p[2]};
        worst=std::fmax(worst,std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]));
    }
    warpsOut=warps;
    return worst;
}
}  // namespace
const Config& Cfg() noexcept { return config; }
const void* BoardingOnly() noexcept { return boardingOnly; }
void Log(const char*,...) noexcept {}
ULONGLONG GameMs() noexcept { return 3600000; }
ULONGLONG GameFrame() noexcept { return 1; }
bool CameraRay(float*,float*) noexcept { return false; }
bool SidecarLevelHooked() noexcept { return false; }
bool bulletHooked=true;   // the bullets' hook (jet_hooks.cpp InstallBulletPass) is in
bool SidecarBulletHooked() noexcept { return bulletHooked; }
unsigned char* BoneRecord506(const unsigned char*,const wchar_t*) noexcept { return nullptr; }
bool SeatPoint(const unsigned char* v,unsigned,float* at,float* reach) noexcept {
    if(!doorReadable)return false;
    FramePoint(v,1.0f,0.0f,kGunnerZ,at);*reach=doorReach;return true;
}
}  // namespace crew

int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    Jump(kWarp,reinterpret_cast<const void*>(&WarpRec));Jump(kMoveIntent,reinterpret_cast<const void*>(&MoveRec));
    Jump(kAddStep,reinterpret_cast<const void*>(&AddStepRec));
    Jump(kControllerPosition,reinterpret_cast<const void*>(&PositionRec));
    Jump(kDamage,reinterpret_cast<const void*>(&DamageRec));
    ok=true;
    Reset();HumanAt(-1000.0f,0.0f,kGunnerZ);
    Expect(!SidecarBoard(bike,human) && warps==0,"no boarding across the map even when the sidecar is nearer");
    Reset();HumanAt(kGunnerX,20.0f,kGunnerZ);
    Expect(!SidecarBoard(bike,human) && warps==0,"no boarding from another floor directly above the sidecar");
    Reset();HumanAt(kGunnerX-2.31f,0.0f,kGunnerZ);
    Expect(!SidecarBoard(bike,human) && warps==0,"just outside the bike's stock reach");
    Reset();HumanAt(kGunnerX-2.29f,0.0f,kGunnerZ);
    Expect(SidecarBoard(bike,human) && warps==1,"just inside the bike's stock reach boards");
    Reset();HumanAt(kGunnerX,0.0f,kGunnerZ);doorReadable=false;
    Expect(!SidecarBoard(bike,human) && warps==0,"an unreadable door does not permit an unbounded take");
    Reset();HumanAt(1.0f,0.0f,kGunnerZ);
    Expect(!SidecarBoard(bike,human) && warps==0,"the nearer free saddle keeps the original boarding path");
    Reset();HumanAt(1.0f,0.0f,kGunnerZ);  // boarding gun's temporary position at the native saddle door
    Put<std::int32_t>(riderCtrl,edf::kCtrlUses,1);
    Put<unsigned char*>(seat,kSeatRiderCtrl,riderCtrl);Put<unsigned char*>(seat,kSeatRider,ordinary);
    boardingOnly=bike;
    Expect(SeatRider(seat)==Rider::other && !SidecarBoard(bike,human) && warps==0 && !sidecars[0].gunner && !BoardHeld(human),
           "boarding gun with an occupied saddle cannot take the reachable virtual sidecar");
    boardingOnly=nullptr;
    Expect(SidecarBoard(bike,human) && warps==1,"ordinary boarding still takes the sidecar beside an occupied saddle");
    Reset();HumanAt(kGunnerX,0.0f,kGunnerZ);
    Expect(SidecarBoard(bike,human) && warps==1,"nearby sidecar takes the player");
    Expect(SidecarBoard(second,human) && warps==1 && sidecars[0].gunner.Is(human) && !sidecars[1].gunner,
           "the remaining team walk cannot transfer the player to another sidecar");
    Expect(SidecarBoard(ordinary,human) && warps==1,"the remaining team walk cannot invoke an ordinary vehicle's FindSeat");
    MoveIntent(human);  // released: the press guard clears, but occupying the sidecar still blocks boarding
    Expect(!BoardHeld(human) && SidecarBoard(ordinary,human),"sidecar occupancy still prevents a second boarding after release");
    human[kHumanBoard]=1;MoveIntent(human);
    Expect(!sidecars[0].gunner && BoardHeld(human) && SidecarBoard(second,human),"the step-off press cannot board another vehicle");
    human[kHumanBoard]=0;MoveIntent(human);
    Expect(!SidecarBoard(ordinary,human),"release after step-off restores ordinary boarding");
    // Both split-screen players can press at once; one releasing must not clear the other's guard.
    Reset();Take(sidecars[0],bike,human,true);Take(sidecars[1],second,otherHuman,true);
    human[kHumanBoard]=1;otherHuman[kHumanBoard]=1;
    MoveIntent(human);MoveIntent(otherHuman);
    Expect(sidecars[0].gunner.Is(human) && sidecars[1].gunner.Is(otherHuman),"split-screen boarding presses do not evict either passenger");
    human[kHumanBoard]=0;MoveIntent(human);
    Expect(!BoardHeld(human) && BoardHeld(otherHuman),"releasing player one preserves player two's press guard");
    // State flag, full ragdoll, death and native seating all release before suppressing input or following.
    for(int condition=0;condition<5;++condition) {
        Reset();Take(sidecars[0],bike,human,true);warps=0;
        Put<float>(human,kHumanMove,0.7f);
        if(condition==0)Put<unsigned>(human,kHumanState,0x62D); // native state 72, attach may still be zero
        if(condition==1)Put<int>(human,kHumanAttach,1);
        if(condition==2)human[kDead]=1;
        if(condition==3){Put<void*>(human,kHumanVehicleCtrl,riderCtrl);Put<int>(riderCtrl,8,1);}
        if(condition==4)bike[kDead]=1;
        MoveIntent(human);
        Expect(!sidecars[0].gunner && warps==0 && At<float>(human,kHumanMove)==0.7f &&
            At<float>(human,kHumanCtrl+kStepVel)==0.0f,"unavailable passenger releases before movement and position writes");
        Put<unsigned>(human,kHumanState,0);Put<int>(human,kHumanAttach,0);human[kDead]=0;bike[kDead]=0;
        Put<void*>(human,kHumanVehicleCtrl,nullptr);
        MoveIntent(human);
        Expect(!sidecars[0].gunner && warps==0,"recovery does not reattach a released passenger");
    }
    Reset();Put<unsigned>(human,kHumanState,0x62D);HumanAt(kGunnerX,kGunnerY,kGunnerZ);
    Expect(!SidecarBoard(bike,human),"a downed human cannot board while attach is still zero");
    Reset();Take(sidecars[0],bike,human,true);ReleaseBoard(human);Put<unsigned>(human,kHumanState,0x62D);
    Hold(sidecars[0],bike);
    Expect(!sidecars[0].gunner,"vehicle-first update order also ends the downed binding");
    Put<unsigned>(human,kHumanState,0);
    Expect(SidecarBoard(bike,human),"an explicit fresh press after recovery can board again");
    // World-space climbing with the bike is not jumping relative to its floor.
    Reset();Take(sidecars[0],bike,human,true);Put<float>(bike,kPosition+4,1.0f);
    HumanAt(kGunnerX,kGunnerY+1.0f,kGunnerZ);Hold(sidecars[0],bike);
    Expect(sidecars[0].gunner.Is(human),"a rising bike does not eject its passenger as a jump");
    HumanAt(kGunnerX,kGunnerY+1.4f,kGunnerZ);Hold(sidecars[0],bike);
    Expect(!sidecars[0].gunner,"a real jump relative to the tub releases the passenger");
    // Native controller pose is authoritative for every class, including the larger HeavyArmor/Fencer.
    for(unsigned vt:kSoldierVts) {
        Reset();Put<unsigned char*>(human,0,image+vt);Take(sidecars[0],bike,human,true);warps=0;
        Put<float>(human,kPosition,50.0f);Put<float>(human,kPosition+4,20.0f); // deliberately stale rendered pose
        MoveIntent(human);
        Expect(sidecars[0].gunner.Is(human) && warps==0 && At<float>(human,kHumanCtrl+kStepVel)==0.0f,
            "all four classes follow the current native foot transform, not stale model pose or class height hacks");
    }
    // Only a current passenger's shots skip their own bike and still-seated driver. Other allies/enemies and
    // the passenger's own blast damage retain the native path, as do stale weak references.
    Reset();Put<int>(riderCtrl,8,1);Put<void*>(seat,kSeatRiderCtrl,riderCtrl);Put<void*>(seat,kSeatRider,otherHuman);
    Take(sidecars[0],bike,human,true);
    Expect(SidecarBulletPass(human,bike,humanRef),"passenger direct rounds pass through their own bike");
    Expect(SidecarBulletPass(human,otherHuman,humanRef),"passenger rounds pass through the native driver");
    Expect(!SidecarBulletPass(human,second,humanRef) && !SidecarBulletPass(human,ordinary,humanRef) &&
        !SidecarBulletPass(otherHuman,bike,otherRef) && !SidecarBulletPass(human,human,humanRef),
        "other vehicles, strangers, other shooters and self damage are not granted immunity");
    unsigned char info[0x100]{};
    Put<void*>(info,0x10,human);Put<void*>(info,0x18,humanRef);
    const void* target[2]={bike,bikeRef};
    PassengerBlastDamage(nullptr,target,info);
    Expect(damageCalls==0,"a passenger blast skips only its own vehicle target");
    target[0]=ordinary;target[1]=newRef;Put<void*>(ordinary,kSelfCtrl,newRef);
    PassengerBlastDamage(nullptr,target,info);
    Expect(damageCalls==1,"the same blast still damages an unrelated target");
    target[0]=bike;target[1]=bikeRef;Put<void*>(info,0x18,newRef);
    PassengerBlastDamage(nullptr,target,info);
    Expect(damageCalls==2,"an old/reused attacker weak reference retains native blast damage");
    Put<void*>(seat,kSeatRider,nullptr);Put<void*>(seat,kSeatRiderCtrl,nullptr);
    Expect(!SidecarBulletPass(human,otherHuman,humanRef),"driver protection ends as soon as the driver leaves");
    Put<void*>(bike,kSelfCtrl,newRef);
    Expect(!SidecarBulletPass(human,bike,humanRef),"vehicle address reuse never inherits passenger immunity");
    Put<void*>(bike,kSelfCtrl,bikeRef);Put<unsigned>(human,kHumanState,0x62D);
    Expect(!SidecarBulletPass(human,bike,humanRef),"downed passengers lose shot protection before the next vehicle tick");
    MoveIntent(human);Put<unsigned>(human,kHumanState,0);
    Expect(!SidecarBulletPass(human,bike,humanRef),"recovery never revives the old published passenger relationship");
    Reset();Take(sidecars[0],bike,human,false);Let(sidecars[0],bike,"knocked down");
    Put<unsigned char*>(bike,0,image+kVt503);Put<int>(riderCtrl,8,1);
    Put<void*>(seat,kSeatRiderCtrl,riderCtrl);Put<void*>(seat,kSeatRider,otherHuman);
    SidecarFrame(bike);
    Expect(sidecars[0].npcReleased && !sidecars[0].gunner,"the NPC scan stays stopped after ejection for this driver's ride");
    sidecars[0].frame=0;Put<void*>(seat,kSeatRiderCtrl,nullptr);SidecarFrame(bike);
    Expect(!sidecars[0].npcReleased,"the driver stepping off permits NPC recruitment on a later ride");
    Reset();Take(sidecars[0],bike,human,true);Put<int>(humanRef,8,0);
    Expect(!SidecarBulletPass(human,bike,humanRef),"an expired projectile owner is not protected despite matching addresses");
    // Riding along (the stutter, 2026-10-06): the held gunner moves with the bike in each physics step, with no
    // warps, straight and in a turn, slow and fast; the old carried velocity (zeroed on the ground) left them behind
    // to be warped back every 0.25 m.
    for(const float speed:{3.0f,12.0f,25.0f})for(const float yawRate:{0.0f,0.9f}) {
        int took=0;
        const float worst=Ride(speed,yawRate,600,took);
        std::printf("  ride %4.1f m/s, turning %.1f rad/s: farthest %.4f m off the point, %d warps\n",speed,yawRate,worst,took);
        Expect(took==0 && worst<0.05f,"a held gunner rides with the bike without warps");
    }
    // Exercise the production install against real instruction signatures in our private executable fixture.
    // No EDF.dll is loaded: hooks are redirected only within this VirtualAlloc buffer.
    Reset();
    auto seed=[](unsigned rva,const auto& bytes){std::memcpy(image+rva,bytes,sizeof(bytes));};
    auto seedAll=[&seed]{   // the native code as shipped, the redirected calls included (each install redirects them)
        seed(kTeamWalk,kTeamWalkSig);seed(kWarp,kWarpSig);seed(kControllerPosition,kPositionSig);
        seed(0x57B17C,kExitWarpSig);seed(0x6746D3,kVelSig);seed(kAddStep,kAddStepSig);
        seed(0x11B9A92,kStepUseSig);seed(0x11B9CB7,kStepClearSig);seed(0x673AAC,kBlockSig);
        seed(0x658D6D,kBikePadSig);seed(kMoveIntentCall-3,kMoveCallSig);
        seed(0x542FC7,kBlastDamageSig);seed(0x543600,kBlastListDamageSig);seed(0x114251,kAttackerCopySig);
    };
    seedAll();
    Expect(InstallSidecar(),"all verified movement/foot/blast signatures admit the sidecar runtime");
    Expect(image+kBlastDamageCall+5+At<int>(image,kBlastDamageCall+1)!=image+kDamage &&
        image+kBlastListDamageCall+5+At<int>(image,kBlastListDamageCall+1)!=image+kDamage,
        "both native explosion per-target calls are actually redirected");
    // The own-vehicle passes are channels of their own: missing, the passengers still ride (their rounds and blasts
    // then hit the bike as stock friendly fire), not the whole sidecar off with them (the jets' heli profile off).
    bulletHooked=false;seedAll();
    Expect(InstallSidecar() && ok,"no bullets' hook: the sidecar still carries passengers");
    bulletHooked=true;
    seedAll();image[0x542FC7]=0;
    Expect(InstallSidecar() && ok && image+kBlastDamageCall+5+At<int>(image,kBlastDamageCall+1)==image+kDamage,
        "no blast filter: the sidecar still carries passengers, their blasts native");
    seedAll();image[kControllerPosition]=0;
    Expect(!InstallSidecar() && !ok,"an unsupported controller ABI fails closed");
    VirtualFree(image,0,MEM_RELEASE);
    return failures ? 1 : 0;
}
