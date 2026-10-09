// The production Proteus rework (src/proteus.cpp and its .inc files) driven through its real entry points with a
// player rider in four embedded seats: ProteusFrame (the native slot-4 tick), ProteusWeaponPost / ProteusEmptyWeapon /
// UserHook (the stock weapon phase), BarrierStep (the stock barrier's update) and the readouts. The EDF functions stay
// uncalled in this fixture (fakes for activation, the holder pull and the axis apply); tests/proteus_weapon_test.cpp
// runs the same production code against the real EDF.dll.
#include "../src/proteus.cpp"
#include <cstdio>
namespace crew {
unsigned char* image=nullptr;PlayerFix player{};
namespace {
Config config{};ULONGLONG now=3600000,frame=1;
alignas(16) unsigned char vehicle[0x3000]{},seats[4*kSeatStride]{},human[0x400]{},ctrl[16]{};
alignas(16) unsigned char gunner[0x400]{},gunnerCtrl[16]{};
alignas(16) unsigned char cannonL[0x1600]{},cannonR[0x1600]{},launcherW[0x1600]{},holders[3][0x48]{},weaponCtrl[16]{};
unsigned char* lists[3][1]={{holders[0]},{holders[1]},{holders[2]}};
unsigned char* observerHuman=human;
int failures=0,checks=0;
bool netSession=false,netAuthority=true,shieldFileReady=true,emcSucceeds=true;
const void* localNpcAuthority=nullptr;
std::int32_t netDriver=42;
int controlSends=0,defenseSends=0,emcFires=0,activated=0,deactivated=0,pulls=0,axes=0;
proteus_net::State lastControl,lastDefense;
float emcFrom[3]{},emcAt[3]{};
bool cameraOk=false;float cameraDir[3]{0,0,1};
unsigned char* testPoseBones=nullptr;
void Check(bool pass,const char* what){++checks;if(!pass){++failures;std::printf("FAIL %s\n",what);}}
void Tick(){++frame;now+=100;ProteusFrame(vehicle);}
void __fastcall FakeActive(void* w){static_cast<unsigned char*>(w)[0x13E]=1;++activated;}
void __fastcall FakeEmpty(void* w){static_cast<unsigned char*>(w)[0x13E]=0;++deactivated;}
void __fastcall FakePull(void* h){At<unsigned char*>(h,kHolderWeapon)[kPull]=1;++pulls;}
void __fastcall FakeAxis(void*,bool){++axes;}
const void* __fastcall FakeUser(void*,const void*){return nullptr;}
void Jump(unsigned rva,void* target){unsigned char b[12]={0x48,0xB8};b[10]=0xFF;b[11]=0xE0;std::memcpy(b+2,&target,8);std::memcpy(image+rva,b,sizeof(b));}
void Seat(unsigned s,unsigned char* rider,unsigned char* c){Put<void*>(SeatAt(vehicle,s),kSeatRider,rider);Put<void*>(SeatAt(vehicle,s),kSeatRiderCtrl,c);}
void InitWeapon(unsigned char* w,unsigned i) {
    Put<void*>(holders[i],kHolderCtrl,weaponCtrl);Put<void*>(holders[i],kHolderWeapon,w);
    auto seat=SeatAt(vehicle,i+1);Put<void*>(seat,kSeatWeapons,lists[i]);Put<std::uint64_t>(seat,kSeatWeaponCount,1);
    Put<float>(w,kRate,1);Put<float>(w,kSpread,1);
}
void Soldier(unsigned char* h,unsigned char* c,std::uint16_t net) {
    Put<void*>(h,0,image+kVtRanger);Put<void*>(h,kSelf,h);Put<void*>(h,kSelfCtrl,c);Put<float>(h,kHp,100);Put<int>(c,8,1);
    h[edf::kHumanPlayer]=1;Put<void*>(h,edf::kHumanPad,h);Put<std::uint16_t>(h,0x128,net);
}
// A fresh BarrierBullet01 as the IFC would make it: its segments and its owner (the attacker ShellMake names).
void FreshBarrier(unsigned char* b,const void* owner,int segments) {
    std::memset(b,0,0x1600);Put<const void*>(b,0,image+kBarrierVt);
    Put<std::int32_t>(b,kBarrierSegmentsAt,segments);Put<const void*>(b,kBarrierOwner,owner);Put<const void*>(b,kBarrierOwnerCtrl,ctrl);
    Put<float>(b,kBarrierHp,0.0f);   // ShellMake may have zeroed the round's damage: the plugin's HP is written on the claim
}
}  // namespace
const Config& Cfg() noexcept{return config;}
bool InSession() noexcept{return netSession;}
bool IsOnlineAuthority(const void* object) noexcept{
    if(!object)return false;
    if(Big(object))return netAuthority;
    return object==localNpcAuthority || (At<std::uint16_t>(object,0x128)&3)==2;
}
bool InstallProteusNet() noexcept{return true;}
std::int32_t ProteusNetController(unsigned char*) noexcept{return netDriver;}
bool ProteusNetSend(unsigned char*,proteus_net::State s) noexcept{
    if(s.kind==proteus_net::Kind::control){++controlSends;lastControl=s;}else{++defenseSends;lastDefense=s;}return true;
}
ULONGLONG GameMs() noexcept{return now;}ULONGLONG GameFrame() noexcept{return frame;}
void Log(const char*,...) noexcept{}
bool MapHoldsKeys() noexcept{return false;}
bool KnownVehicle(const void*) noexcept{return false;}
bool CameraRay(float* eye,float* dir) noexcept{if(!cameraOk)return false;eye[0]=eye[1]=eye[2]=0;std::memcpy(dir,cameraDir,12);return true;}
float MapRay(const float*,const float*,float*) noexcept{return -1.0f;}
unsigned char* PlayerHuman() noexcept{return observerHuman;}
bool EmcRoundReady(EmcRound kind) noexcept{return kind==EmcRound::proteusShield && shieldFileReady;}
RoundObj EmcFire(EmcRound kind,const unsigned char* by,const float* from,const float* at,float) noexcept{
    static unsigned char demo[0x40]{};
    if(kind!=EmcRound::proteusShield || by!=vehicle || !emcSucceeds)return RoundObj{};
    ++emcFires;std::memcpy(emcFrom,from,12);std::memcpy(emcAt,at,12);return RoundObj{demo,nullptr};
}
unsigned char* BoneRecord506(const unsigned char*,const wchar_t* name) noexcept{
    if(!testPoseBones)return nullptr;
    if(!std::wcscmp(name,L"pile_l"))return testPoseBones;
    if(!std::wcscmp(name,L"pile_r"))return testPoseBones+kBoneStride;
    return nullptr;
}
}  // namespace crew

namespace {
using namespace crew;
// `image`: a private fake (null: allocated here, its pull / axis entries jumped to fakes) or the real EDF.dll mapping.
void Setup() {
    const bool fake=!image;
    if(fake)image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    ok=userOk=barrierOk=true;config.debug=false;config.proteusFieldRadius=0;
    Put<const void*>(vehicle,0,image+kVtBig);Put<void*>(vehicle,kSelf,vehicle);Put<void*>(vehicle,kSelfCtrl,ctrl);
    Put<void*>(vehicle,kSeats,seats);Put<std::uint64_t>(vehicle,kSeatCount,4);
    Put<float>(vehicle,kWalk,10);Put<float>(vehicle,kWalkEase,0.1f);Put<float>(vehicle,kTurn,0.2f);Put<float>(vehicle,kJump,8);Put<float>(vehicle,kStepNormal,0.76f);
    Put<float>(vehicle,kHpMax,7500);Put<float>(vehicle,kHp,7500);
    proteus::pose::Identity(reinterpret_cast<float*>(vehicle+kMatrix));
    Put<float>(vehicle,kPosition,100);Put<float>(vehicle,kPosition+4,5);Put<float>(vehicle,kPosition+8,-50);
    for(int i=0;i<4;++i)Put<int>(seats+i*kSeatStride,kSeatClassMask,15);
    Soldier(human,ctrl,2);Soldier(gunner,gunnerCtrl,2);
    Seat(0,human,ctrl);seats[kSeatPad]=1;
    Put<int>(weaponCtrl,8,1);
    InitWeapon(cannonL,0);InitWeapon(cannonR,1);InitWeapon(launcherW,2);
    if(!fake)return;
    nativeActive=&FakeActive;nativeEmpty=&FakeEmpty;nextUser=&FakeUser;
    Jump(kPullFn,reinterpret_cast<void*>(&FakePull));Jump(kAxisApply,reinterpret_cast<void*>(&FakeAxis));
}

void Stance() {
    Tick();ProteusReadout ro{};
    Check(PlayerProteus(&ro) && ro.driver && ro.shieldReady,"native player tick produces the HUD readout (shield available)");
    Check(At<float>(vehicle,kWalk)==16.0f,"player entry applies the walking legs");
    Check(At<int>(seats+2*kSeatStride,kSeatClassMask)==0 && At<int>(seats+3*kSeatStride,kSeatClassMask)==0,"player entry closes the extra two seats");
    Check(ProteusVisibleSeats(vehicle,4)==2,"closed engine weapon slots are not advertised as passenger seats");
    Put<std::uint16_t>(seats,kSeatButtons,static_cast<std::uint16_t>(config.proteusModeButton));Tick();
    Put<std::uint16_t>(seats,kSeatButtons,0);for(int i=0;i<35;++i)Tick();
    Check(PlayerProteus(&ro) && ro.mode==proteus::Mode::deployed,"the pad's stance button deploys after the stagger");
    Check(At<float>(vehicle,kWalk)==0 && At<float>(vehicle,kJump)==0,"deployed: legs and jump stopped in the real object");
    Check(At<float>(cannonL,kRate)==config.proteusDeployGunRate && At<float>(cannonR,kSpread)==config.proteusDeployGunSpread &&
          At<float>(launcherW,kRate)==1,"deployed: the cannons get the stance's rate / spread, the launcher keeps its own");
}

void Weapons() {
    Unit& u=*UnitOf(vehicle,false);
    const unsigned char* lent[4]{};
    // Alone, deployed: the driver works all three stock mounts.
    Check(ProteusBorrowedWeapons(vehicle,0,lent,4)==3 && lent[0]==cannonL && lent[1]==cannonR && lent[2]==launcherW,
          "alone deployed: the driver's seat lists both cannons and the launcher");
    Check(UserHook(vehicle+kUserIface,cannonR)==human+kUserIface && UserHook(vehicle+kUserIface,launcherW)==human+kUserIface,
          "the fire step's user of a borrowed mount is the driver");
    // Aim: the borrowed mounts follow the driver's seat aim (clamped to their own stops).
    for(unsigned i=0;i<4;++i)for(int axis=0;axis<2;++axis) {
        auto a=seataim::Object(SeatAt(vehicle,i))+edf::kAimAxes+axis*edf::kAxisStride;
        Put<float>(a,edf::kAxisMin,-.2f);Put<float>(a,edf::kAxisMax,.2f);Put<float>(a,edf::kAxisAngle,i==0 ? .3f : -.1f);
    }
    nextWeaponPost=reinterpret_cast<void*>(+[](void*,const float*){});
    ProteusWeaponPost(vehicle,nullptr);
    Check(At<float>(seataim::Object(SeatAt(vehicle,1)),edf::kAimAxes+edf::kAxisAngle)==.2f &&
          At<float>(seataim::Object(SeatAt(vehicle,3)),edf::kAimAxes+edf::kAxisAngle)==.2f && axes==6,
          "before the stock pose every borrowed mount turns to the driver's aim within its stops (bones applied)");
    // Fire: the primary pulls the cannons, the second trigger (a pad's LT) the launcher; nothing without a press.
    ProteusEmptyWeapon(cannonL);ProteusEmptyWeapon(launcherW);
    Check(cannonL[0x13E] && launcherW[0x13E] && pulls==0,"a borrowed mount is activated the stock way, not pulled unpressed");
    Put<float>(seats,kSeatFire,1);ProteusEmptyWeapon(cannonL);ProteusEmptyWeapon(cannonR);ProteusEmptyWeapon(launcherW);
    Check(cannonL[kPull] && cannonR[kPull] && !launcherW[kPull] && pulls==2,"the driver's primary pulls both cannons, not the launcher");
    Put<float>(seats,kSeatFire,0);Put<float>(seats,kSeatFire2,1);cannonL[kPull]=cannonR[kPull]=0;
    ProteusEmptyWeapon(cannonL);ProteusEmptyWeapon(launcherW);
    Check(!cannonL[kPull] && launcherW[kPull],"the driver's second trigger pulls the launcher");
    Put<float>(seats,kSeatFire2,0);launcherW[kPull]=0;
    // A gunner comes: both cannons are theirs, the right one paired with their own (left) trigger latch.
    Seat(1,gunner,gunnerCtrl);u.st.mode=proteus::Mode::deployed;
    Check(ProteusBorrowedWeapons(vehicle,0,lent,4)==1 && lent[0]==launcherW,"with a gunner the driver keeps only the launcher");
    Check(ProteusBorrowedWeapons(vehicle,1,lent,4)==1 && lent[0]==cannonR,"the gunner's seat lists the paired right cannon");
    Check(UserHook(vehicle+kUserIface,cannonR)==gunner+kUserIface,"the right cannon's user is the gunner");
    Check(UserHook(vehicle+kUserIface,cannonL)==nullptr,"the gunner's own cannon goes to the stock lookup (not borrowed)");
    const int before=deactivated;ProteusEmptyWeapon(cannonL);
    Check(deactivated==before+1,"a weapon not borrowed (its seat's own) gets the stock empty callback");
    Put<float>(seats,kSeatFire,1);ProteusEmptyWeapon(cannonR);
    Check(!cannonR[kPull],"the driver's trigger no longer fires the gunner's cannons");
    Put<float>(seats,kSeatFire,0);cannonL[kHeld]=1;ProteusEmptyWeapon(cannonR);
    Check(cannonR[kPull],"the gunner's held trigger (their cannon's own latch) fires the paired right cannon");
    cannonL[kHeld]=0;cannonR[kPull]=0;
    netSession=true;Put<std::uint16_t>(gunner,0x128,1);cannonL[kPull]=1;ProteusEmptyWeapon(cannonR);
    Check(!cannonR[kPull] && cannonR[0x13E],"a remote gunner's cannon is activated here but fired only on their machine");
    netSession=false;Put<std::uint16_t>(gunner,0x128,2);cannonL[kPull]=0;
    // Walking the launcher is idle.
    u.st.mode=proteus::Mode::walk;
    Check(ProteusBorrowedWeapons(vehicle,0,lent,4)==0,"walking: the launcher is not borrowed");
    const int empties=deactivated;ProteusEmptyWeapon(launcherW);
    Check(deactivated==empties+1 && !launcherW[0x13E],"walking: the idle launcher gets the stock empty callback");
    u.st.mode=proteus::Mode::deployed;
    // A soldier in a mount's own seat keeps it.
    Seat(2,gunner,gunnerCtrl);
    Check(UserHook(vehicle+kUserIface,cannonR)==nullptr && ProteusBorrowedWeapons(vehicle,1,lent,4)==0,"a rider at the right seat keeps its own cannon");
    Seat(2,nullptr,nullptr);Seat(1,nullptr,nullptr);
    userOk=false;
    Check(!UserHook(vehicle+kUserIface,cannonR) && ProteusBorrowedWeapons(vehicle,0,lent,4)==0,"without the verified user hook nothing is borrowed");
    userOk=true;
}

void Shield() {
    Unit& u=*UnitOf(vehicle,false);
    alignas(16) static unsigned char barrier[0x1600],tochka[0x1600],second[0x1600];
    u.st.mode=proteus::Mode::walk;u.st.shield=1;u.st.broken=false;
    Put<std::uint16_t>(seats,kSeatButtons,static_cast<std::uint16_t>(config.proteusShieldButton));Tick();
    Put<std::uint16_t>(seats,kSeatButtons,0);
    Check(u.st.shieldOn && emcFires==1 && u.barrier.raisedFrame,"the shield button raises one native barrier round");
    Check(emcFrom[0]==100 && emcFrom[1]==5 && emcFrom[2]==-50 && emcAt[2]==-49,"raised at the hull's feet toward its nose");
    Tick();Check(emcFires==1,"a pending raise is not fired again");
    // An Air Raider's tochka (27 segments, its own owner) is never claimed nor touched.
    FreshBarrier(tochka,gunner,27);Put<float>(tochka,kBarrierHp,300);
    BarrierStep(tochka);
    Check(At<float>(tochka,kBarrierHp)==300 && !tochka[kBarrierNoEcho] && !u.barrier.obj,"an Air Raider's barrier is left alone");
    // Ours: claimed, given the shield's HP, kept on the hull.
    FreshBarrier(barrier,vehicle,kBarrierSegmentCount);
    BarrierStep(barrier);
    const float full=config.proteusBarrier*7500;
    Check(u.barrier.obj==barrier && At<float>(barrier,kBarrierHp)==full,"our round is claimed and its HP is the shield's");
    Check(At<float>(barrier,kBarrierMatrix+48)==100 && At<float>(barrier,kBarrierMatrix+56)==-50 &&
          At<float>(barrier,kBarrierMatrix+40)==1 && At<float>(barrier,kBarrierMatrix+20)==1 && At<float>(barrier,kBarrierMatrix)==1,
          "its world matrix is upright at the hull's feet, +Z on the nose");
    // The hull turns and moves: the next update follows.
    float* m=reinterpret_cast<float*>(vehicle+kMatrix);m[0]=0;m[2]=-1;m[8]=1;m[10]=0;Put<float>(vehicle,kPosition,130);
    Tick();BarrierStep(barrier);
    Check(At<float>(barrier,kBarrierMatrix+32)==1 && At<float>(barrier,kBarrierMatrix+40)==0 && At<float>(barrier,kBarrierMatrix+48)==130,
          "it follows the hull's facing and position every update");
    // Native hits drain its HP: the shield reads it.
    Put<float>(barrier,kBarrierHp,full*0.6f);BarrierStep(barrier);
    Check(std::fabs(u.st.shield-0.6f)<1e-5f && u.st.quiet==0 && proteus::ShieldUp(u.st),"the barrier's HP is the shield's (a hit restarts the quiet spell)");
    ProteusReadout ro{};Tick();
    Check(PlayerProteus(&ro) && ro.shieldUp && std::fabs(ro.shield-0.6f)<1e-5f && ro.shieldHp==full,"the HUD shows its HP share and full HP");
    // Deployed it faces the driver's view.
    u.st.mode=proteus::Mode::deployed;cameraOk=true;cameraDir[0]=-1;cameraDir[1]=0.5f;cameraDir[2]=0;Tick();BarrierStep(barrier);
    Check(At<float>(barrier,kBarrierMatrix+32)==-1 && At<float>(barrier,kBarrierMatrix+36)==0,"deployed: it faces the driver's horizontal view");
    cameraOk=false;u.st.mode=proteus::Mode::walk;
    // Stuck (anchored by a touch): dropped without echo, raised anew.
    barrier[kBarrierStuck]=1;BarrierStep(barrier);
    Check(At<float>(barrier,kBarrierHp)==0 && barrier[kBarrierNoEcho] && !u.barrier.obj,"a stuck barrier is dropped (no network echo)");
    BarrierStep(barrier);Check(!RaisedOf(barrier),"once dropped it is forgotten");
    Tick();Check(emcFires==2,"the shield still on raises a new barrier");
    FreshBarrier(second,vehicle,kBarrierSegmentCount);BarrierStep(second);
    Check(u.barrier.obj==second && std::fabs(At<float>(second,kBarrierHp)-full*0.6f)<0.01f,"the new one stands with the HP the shield has left");
    // Broken by hits.
    Put<float>(second,kBarrierHp,-5);BarrierStep(second);
    Check(u.st.broken && !proteus::ShieldUp(u.st) && !u.barrier.obj && u.st.shield==0,"HP gone: broken, the round goes");
    Tick();Check(emcFires==2 && PlayerProteus(&ro) && ro.broken,"broken: nothing raised, the HUD says so");
    u.st.broken=false;u.st.shield=1;Tick();FreshBarrier(second,vehicle,kBarrierSegmentCount);BarrierStep(second);
    Check(emcFires==3 && u.barrier.obj==second,"refilled: raised again");
    // Switched off: the round goes on its next update.
    Put<std::uint16_t>(seats,kSeatButtons,static_cast<std::uint16_t>(config.proteusShieldButton));Tick();
    Put<std::uint16_t>(seats,kSeatButtons,0);BarrierStep(second);
    Check(!u.st.shieldOn && At<float>(second,kBarrierHp)==0 && second[kBarrierNoEcho] && !u.barrier.obj,"switched off: the barrier goes");
    // A raise never seen ends the attempt (no endless re-raising).
    Tick();   // the button released (a press is an edge)
    Put<std::uint16_t>(seats,kSeatButtons,static_cast<std::uint16_t>(config.proteusShieldButton));Tick();Put<std::uint16_t>(seats,kSeatButtons,0);
    const int fired=emcFires;
    Check(u.st.shieldOn && u.barrier.raisedFrame,"(switched on again: a raise is pending)");
    for(ULONGLONG i=0;i<=kBarrierRaiseFrames+1;++i)Tick();
    Check(emcFires==fired && !u.st.shieldOn && u.barrier.failed,"a raise not seen in time switches the shield off, logged, not retried");
    Put<std::uint16_t>(seats,kSeatButtons,static_cast<std::uint16_t>(config.proteusShieldButton));Tick();Put<std::uint16_t>(seats,kSeatButtons,0);
    Tick();
    Check(u.st.shieldOn && emcFires==fired && PlayerProteus(&ro) && !ro.shieldReady,"after a failed raise the shield stays offline for this ride");
    u.barrier.failed=false;
    shieldFileReady=false;Tick();
    Check(PlayerProteus(&ro) && !ro.shieldReady,"without the installed SGO the HUD says the shield is offline");
    shieldFileReady=true;
}

void GiveBackAll() {
    Unit& u=*UnitOf(vehicle,false);
    alignas(16) static unsigned char standing[0x1600];
    u.st.shieldOn=true;u.st.broken=false;u.st.shield=1;Tick();FreshBarrier(standing,vehicle,kBarrierSegmentCount);BarrierStep(standing);
    Check(u.barrier.obj==standing,"(a barrier stands)");
    config.proteus=false;Tick();
    Check(At<float>(vehicle,kWalk)==10 && At<float>(vehicle,kJump)==8,"setting off restores the stock legs through the same tick");
    Check(At<int>(seats+2*kSeatStride,kSeatClassMask)==15 && At<int>(seats+3*kSeatStride,kSeatClassMask)==15,"setting off restores the seat masks");
    Check(At<float>(cannonL,kRate)==1 && At<float>(cannonR,kSpread)==1,"setting off gives the cannons their own numbers back");
    BarrierStep(standing);
    Check(At<float>(standing,kBarrierHp)==0 && !RaisedOf(standing),"given back: its barrier goes on its next update");
    Check(ProteusVisibleSeats(vehicle,4)==4,"disabled rework restores four public seats");
    config.proteus=true;
}
}  // namespace

int main() {
    using namespace crew;
    Setup();
    if(!image)return 2;
    Stance();
    Weapons();
    Shield();
    GiveBackAll();
    std::printf("proteus_frame_test: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
