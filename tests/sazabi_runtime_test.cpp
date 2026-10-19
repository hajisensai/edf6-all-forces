// Exercise the production enemy visitor and pose consumers, without loading EDF.dll.
#include "../src/sazabi.cpp"
#include <cstdio>

namespace crew {
bool NpcDriver(const unsigned char* v) noexcept { return v && SeatCount(v)>0 && SeatRider(SeatAt(const_cast<unsigned char*>(v),0))==Rider::dummy; }

unsigned char* image=nullptr;
Config config{};
bool netSession=false,netAuthority=true;
std::int32_t netDriver=42;
ULONGLONG testNow=1000;
int emcCalls=0,damageCalls=0,enemyVisits=0,soundCalls=0;
sazabi_net::State lastNetwork;
PlayerFix player{};
const void* visibleObjects[40]{};
float visiblePositions[40][3]{};
int visibleCount=0;
float testMapHit=-1.0f;
float lastRayFrom[3]{},lastRayEnd[3]{};
const void* fpVehicles[2]{};
const unsigned char* fpHumans[2]{};
fpview::M4 fpEyes[2]{};
bool fpRequests[2]{},observing=false;
bool FirstPersonVehicleView(const void* v,unsigned seat) noexcept {
    for(int i=0;i<2;++i)if(v==fpVehicles[i] && seat==0)return fpRequests[i];
    return false;
}
bool VehicleEye(unsigned char* human,float* eye,RideView* out) noexcept {
    for(int i=0;i<2;++i)if(human==fpHumans[i]) {
        *out=RideView{};out->vehicle=static_cast<unsigned char*>(const_cast<void*>(fpVehicles[i]));out->seat=0;
        std::memcpy(eye,fpEyes[i].m+12,12);std::memcpy(out->frame,fpEyes[i].m,64);return true;
    }
    return false;
}
bool VehicleLookActive(const void*) noexcept { return observing; }
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
unsigned char* BoneRecord506(const unsigned char*,const wchar_t*) noexcept { return nullptr; }
bool SeatPoint(const unsigned char*,unsigned,float*,float*) noexcept { return false; }
float GroundClearance(const float*) noexcept { return kNoGround; }
// Unused game entry points from the included runtime translation unit.
ULONGLONG GameMs() noexcept { return testNow; }
ULONGLONG GameFrame() noexcept { return 1; }
float GameStep(ULONGLONG) noexcept { return 1.0f/60.0f; }
void NozzleFlames(const unsigned char*,const float (*)[16],int,const float (*)[2],const float*,ULONGLONG) noexcept {}
void FlareFlames(const unsigned char*,const float (*)[3],const float (*)[3],int,ULONGLONG) noexcept {}
PluginBody BodyOf(const void*) noexcept { return PluginBody::sazabi; }
bool Body506Ok() noexcept { return true; }
void BoardingRequest(unsigned char*) noexcept {}
bool DrillCharge(const unsigned char*,const float*,const float*,float) noexcept { return false; }
bool EmcRoundReady(EmcRound) noexcept { return false; }
RoundObj EmcFire(EmcRound,const unsigned char*,const float*,const float*,float damage) noexcept {
    ++emcCalls;if(damage>0)++damageCalls;return {};
}
bool RoundSteer(const RoundObj&,const float*,const float*) noexcept { return false; }
bool RoundSize(const RoundObj&,float) noexcept { return false; }
void RoundDrop(RoundObj&) noexcept {}
unsigned char* PlayerHuman() noexcept { return nullptr; }
bool HumanOnFoot(const unsigned char*) noexcept { return false; }
float MapRay(const float* from,const float* end,float*) noexcept {
    std::memcpy(lastRayFrom,from,12);std::memcpy(lastRayEnd,end,12);return testMapHit;
}
bool VisitEnemies(const unsigned char*,EnemyVisitor visitor,void* context) noexcept {
    ++enemyVisits;
    for(int i=0;i<visibleCount;++i)visitor(context,visibleObjects[i],visiblePositions[i]);
    return visibleCount>0;
}
bool MapHoldsKeys() noexcept { return true; }
void BodyAttitude(const unsigned char*,const float*,const float*,float,float,float*) noexcept {}
int WeaponLock(const unsigned char*,float*,float*) noexcept { return 0; }
void SazabiSfx(SzSfx,const float*) noexcept { ++soundCalls; }
void SazabiLoop(SzLoop,const float*,const float*,float) noexcept {}
bool InSession() noexcept { return netSession; }
bool IsOnlineAuthority(const void*) noexcept { return netAuthority; }
bool InstallSazabiNet() noexcept { return true; }
std::int32_t SazabiNetController(unsigned char*) noexcept { return netDriver; }
std::int32_t SazabiNetReference(const void*) noexcept { return -1; }
bool SazabiNetSend(unsigned char*,sazabi_net::State s) noexcept { lastNetwork=s;return true; }
}

namespace {
int checks=0,failures=0;
int controlDeletes=0;
void DeleteControl(void*) { ++controlDeletes; }
using ControlFn=void(*)(void*);
ControlFn controlVtable[2]={nullptr,&DeleteControl};
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
bool Near(const float* a,const float* b) {
    for(int i=0;i<3;++i)if(std::fabs(a[i]-b[i])>1e-4f)return false;
    return true;
}
}

int main() {
    using namespace crew;
    Enemies enemies{};
    alignas(16) unsigned char objects[kMostTargets+2][0x318]{},targetControls[kMostTargets+2][0x10]{};
    for(int i=0;i<kMostTargets+2;++i) {
        Put<void*>(objects[i],kSelfCtrl,targetControls[i]);Put<LONG>(targetControls[i],edf::kCtrlUses,1);
        Put<void*>(targetControls[i],0,controlVtable);Put<LONG>(targetControls[i],0xC,1);
    }
    for(int i=0;i<kMostTargets;++i) {
        const float at[3]={static_cast<float>(100+i),0,0};
        SeeEnemy(&enemies,&objects[i],at);
    }
    Check(enemies.n==kMostTargets,"fills every target slot");
    Check(enemies.obj[kMostTargets-1]==&objects[kMostTargets-1],"last slot holds a real enemy");
    const float nearPoint[3]={1,0,0},farPoint[3]={200,0,0};
    SeeEnemy(&enemies,&objects[kMostTargets],nearPoint);
    Check(enemies.n==kMostTargets && enemies.obj[0]==&objects[kMostTargets],"a late near enemy replaces the farthest");
    Check(enemies.obj[kMostTargets-1]==&objects[kMostTargets-2],"only the farthest enemy was evicted");
    SeeEnemy(&enemies,&objects[kMostTargets+1],farPoint);
    Check(enemies.obj[0]==&objects[kMostTargets] && enemies.obj[kMostTargets-1]==&objects[kMostTargets-2],"a late farther enemy leaves the list unchanged");
    bool sorted=true,allReal=true;
    for(int i=0;i<enemies.n;++i){allReal=allReal && enemies.obj[i];if(i)sorted=sorted && enemies.d2[i-1]<=enemies.d2[i];}
    Check(sorted && allReal,"a full target list stays sorted and contains no phantom origin enemy");

    // Production Assist -> Aim, using the actual registry callback and camera rig.
    alignas(16) unsigned char assistVehicle[0x2100]{},assistSeat[kSeatStride]{};
    Put<void*>(assistVehicle,kSeats,assistSeat);
    Mech assisted{};assisted.rootOk=true;
    for(int i=0;i<16;++i)assisted.root[i]=assisted.rootInv[i]=i%5==0 ? 1.0f : 0.0f;
    config.sazabiAimAssist=true;config.sazabiAssistPull=0;
    float eye[3],dir[3];ViewRay(ViewOf(assisted),assisted.root+12,eye,dir);
    visibleCount=kMostTargets+1;
    for(int i=0;i<visibleCount;++i) {
        visibleObjects[i]=&objects[i];
        const float angle=(i==kMostTargets ? 0.1f : 7.0f)*sazabi::kDeg;
        visiblePositions[i][0]=eye[0]+300*std::sin(angle);
        visiblePositions[i][1]=eye[1];visiblePositions[i][2]=eye[2]+300*std::cos(angle);
    }
    Assist(assisted,assistVehicle,1.0f/60.0f);Aim(assisted);
    Check(assisted.arms.hasAssist && assisted.arms.assistObj==&objects[kMostTargets],
          "production assist picks a late central enemy from a crowd");
    Check(assisted.arms.hasAim && Near(assisted.arms.aim,visiblePositions[kMostTargets]),
          "production Aim passes the assisted point to weapons");
    testMapHit=299.0f;Assist(assisted,assistVehicle,1.0f/60.0f);
    Check(!assisted.arms.hasAssist,"production assist releases a held target one metre behind a wall");
    testMapHit=-1;Assist(assisted,assistVehicle,1.0f/60.0f);
    assisted.arms.swing=0;Assist(assisted,assistVehicle,1.0f/60.0f);
    Check(!assisted.arms.hasAssist,"swinging suppresses assistance");
    assisted.arms.swing=-1;Assist(assisted,assistVehicle,1.0f/60.0f);
    config.sazabiAimAssist=false;Assist(assisted,assistVehicle,1.0f/60.0f);
    Check(!assisted.arms.hasAssist,"disabling assistance releases the held target");
    config.sazabiAimAssist=true;Assist(assisted,assistVehicle,1.0f/60.0f);DropArms(assisted);
    Check(!assisted.arms.hasAssist && !assisted.arms.assistObj,"leaving clears the previous pilot's held target");
    // The hard lock must survive crowded scenes, and switching must rank on the requested side rather than keeping
    // the first registry points (or only those nearest the centre on the wrong side).
    assistSeat[kSeatPad]=1;Put<std::uint16_t>(assistSeat,kSeatButtons,0x80);
    Controls lockControls{};LockInput(assisted,assistVehicle,lockControls,1.0f/60.0f);
    Check(assisted.arms.lockOn && assisted.arms.lockOnTarget.obj==&objects[kMostTargets],
          "hard lock acquisition finds the late central enemy in a crowd");
    Check(At<LONG>(targetControls[kMostTargets],0xC)==2,"acquisition pins the target control with one weak reference");
    Assist(assisted,assistVehicle,1.0f/60.0f);
    Check(assisted.arms.lockOn && assisted.arms.hasAssist && assisted.arms.assistObj==&objects[kMostTargets],
          "hard lock retention only gathers its held enemy beyond a full registry buffer");
    assisted.heading=assisted.aimPitch=0;
    ViewRay(ViewOf(assisted),assisted.root+12,eye,dir);
    visibleCount=kMostTargets+2;
    for(int i=0;i<visibleCount;++i) {
        visibleObjects[i]=&objects[i];
        const float angle=(i==kMostTargets ? 0.0f : i==kMostTargets+1 ? 20.0f : -5.0f)*sazabi::kDeg;
        visiblePositions[i][0]=eye[0]+300*std::sin(angle);visiblePositions[i][1]=eye[1];
        visiblePositions[i][2]=eye[2]+300*std::cos(angle);
    }
    std::memcpy(assisted.arms.assist,visiblePositions[kMostTargets],12);
    assisted.arms.flickCool=0;Put<std::uint16_t>(assistSeat,kSeatButtons,0);
    lockControls.turn=20;lockControls.pitch=0.1f;
    LockInput(assisted,assistVehicle,lockControls,1.0f/60.0f);
    Check(assisted.arms.lockOnTarget.obj==&objects[kMostTargets+1],"flick finds the next enemy despite a crowd on the wrong side");
    Check(At<LONG>(targetControls[kMostTargets],0xC)==1 && At<LONG>(targetControls[kMostTargets+1],0xC)==2,
          "switching transfers exactly one weak reference to the next target");
    Check(lockControls.turn==0 && lockControls.pitch==0,"hard lock consumes view input for target switching");
    testMapHit=1;Assist(assisted,assistVehicle,2.0f);
    Check(!assisted.arms.lockOn && !assisted.arms.hasAssist,"hard lock releases after sustained map occlusion");
    testMapHit=-1;assisted.arms.lockOn=true;AssignLockTarget(assisted.arms,ObjRef::Of(objects[0]));visibleCount=0;
    Assist(assisted,assistVehicle,1.0f/60.0f);
    Check(!assisted.arms.lockOn,"hard lock releases a deleted enemy");
    assisted.arms.lockOn=true;AssignLockTarget(assisted.arms,ObjRef::Of(objects[0]));
    assisted.arms.lockKeyHeld=false;Put<std::uint16_t>(assistSeat,kSeatButtons,0x80);
    lockControls.turn=1;LockInput(assisted,assistVehicle,lockControls,1.0f/60.0f);
    Check(!assisted.arms.lockOn && lockControls.turn==1,"pressing the lock key again restores manual view input");
    DropArms(assisted);
    Check(!assisted.arms.lockKeyHeld && assisted.arms.flickCool==0,"leaving clears hard-lock input latches");
    // Destroy a locked object and place another live enemy at exactly the same address, with its new control block.
    visibleCount=1;visibleObjects[0]=objects[0];
    visiblePositions[0][0]=eye[0];visiblePositions[0][1]=eye[1];visiblePositions[0][2]=eye[2]+300;
    LockInput(assisted,assistVehicle,lockControls,1.0f/60.0f);Assist(assisted,assistVehicle,1.0f/60.0f);
    Check(assisted.arms.lockOn,"real-layout enemy is locked before address reuse");
    alignas(16) unsigned char replacementControl[0x10]{};Put<LONG>(replacementControl,edf::kCtrlUses,1);
    Put<void*>(replacementControl,0,controlVtable);Put<LONG>(replacementControl,0xC,1);
    Put<LONG>(targetControls[0],edf::kCtrlUses,0);InterlockedDecrement(reinterpret_cast<volatile LONG*>(targetControls[0]+0xC));
    Check(At<LONG>(targetControls[0],0xC)==1 && controlDeletes==0,"enemy destruction cannot recycle the pinned control");
    Put<void*>(objects[0],kSelfCtrl,replacementControl);
    Put<std::uint16_t>(assistSeat,kSeatButtons,0);
    lockControls.turn=20;assisted.arms.flickCool=0;
    LockInput(assisted,assistVehicle,lockControls,1.0f/60.0f);Assist(assisted,assistVehicle,1.0f/60.0f);
    Check(!assisted.arms.lockOn,"same-address replacement enemy cannot inherit a hard lock");
    Check(lockControls.turn==20 && controlDeletes==1 && At<LONG>(targetControls[0],0xC)==0,
          "expired identity restores input and deletes its control only after the final weak release");
    DropArms(assisted);Put<std::uint16_t>(assistSeat,kSeatButtons,0x80);
    LockInput(assisted,assistVehicle,lockControls,1.0f/60.0f);
    Check(assisted.arms.lockOn && assisted.arms.lockOnTarget.ctrl==replacementControl,"new enemy can be explicitly reacquired");
    objects[0][kDead]=1;Assist(assisted,assistVehicle,1.0f/60.0f);
    Check(!assisted.arms.lockOn && At<LONG>(replacementControl,0xC)==1,"dead enemy releases its held weak");
    objects[0][kDead]=0;DropArms(assisted);
    LockInput(assisted,assistVehicle,lockControls,1.0f/60.0f);
    objects[0][0x18]|=4;Assist(assisted,assistVehicle,1.0f/60.0f);
    Check(!assisted.arms.lockOn && At<LONG>(replacementControl,0xC)==1,"despawn flag releases the held weak");
    objects[0][0x18]=0;
    // The real table reset and stale-slot replacement must release weak references before Mech{} overwrites them.
    AssignLockTarget(mechs[0].arms,ObjRef::Of(objects[0]));ResetSazabi();
    Check(At<LONG>(replacementControl,0xC)==1,"mission reset releases hard-lock references before clearing the table");
    AssignLockTarget(mechs[0].arms,ObjRef::Of(objects[0]));Make(assistVehicle);
    Check(At<LONG>(replacementControl,0xC)==1,"reusing a mech slot releases the previous lock reference");
    ResetSazabi();
    config=Config{};visibleCount=0;

    // Production camera registry: two local pilots retain their own authoritative aim and raw physical monoeye.
    // The eye fixture supplies only VehicleEye's native marker output; all camera/assist/aim consumers are production.
    alignas(16) unsigned char cameraVehicles[2][0x2100]{},cameraSeats[2][kSeatStride]{},cameraHumans[2][0x1600]{};
    alignas(16) unsigned char cameraControls[2][2][0x10]{},newVehicleControl[0x10]{},newRiderControl[0x10]{};
    const auto identity=fpview::Identity();
    config.enabled=config.sazabi=true;config.sazabiAimAssist=false;
    for(int i=0;i<2;++i) {
        auto v=cameraVehicles[i],seat=cameraSeats[i],human=cameraHumans[i];
        Put<void*>(v,kSelfCtrl,cameraControls[i][0]);Put<LONG>(cameraControls[i][0],8,1);
        Put<void*>(human,kSelfCtrl,cameraControls[i][1]);Put<LONG>(cameraControls[i][1],8,1);
        Put<void*>(v,kSeats,seat);Put<std::uint64_t>(v,kSeatCount,1);
        Put<void*>(seat,kSeatRider,human);Put<void*>(seat,kSeatRiderCtrl,cameraControls[i][1]);
        Put<void*>(human,0x1548,v);Put<void*>(human,edf::kHumanPad,cameraControls[i][1]);human[edf::kHumanPlayer]=1;
        std::memcpy(v+kMatrix,identity.m,64);
        fpVehicles[i]=v;fpHumans[i]=human;fpRequests[i]=true;
        fpEyes[i]=fpview::Translate(102.0f+i*30.0f,25.0f,-27.0f);
        Mech* m=Make(v);m->rootOk=true;std::memcpy(m->root,identity.m,64);
        m->root[12]=100.0f+i*30.0f;m->root[13]=5.0f;m->root[14]=-30.0f;
        Check(exhaust::Inverse(m->root,m->rootInv),"translated camera root is invertible");
        m->heading=i==0 ? 0.6f : -0.7f;m->aimPitch=i==0 ? 0.2f : -0.3f;PublishView(*m,v);
    }
    Mech& c0=*Find(cameraVehicles[0]);Mech& c1=*Find(cameraVehicles[1]);
    float cameraFrame[16];std::memcpy(cameraFrame,fpEyes[0].m,64);
    Check(SazabiFirstPersonFrame(cameraVehicles[0],0,cameraFrame),"first local mech reads its own published aim after the second publishes");
    auto expected=fpview::HeadingFrame(c0.heading,c0.aimPitch,fpEyes[0].m+12);
    Check(std::memcmp(cameraFrame,expected.m,64)==0,"first-person frame preserves raw eye and uses authoritative heading/pitch");
    Check(ViewCentred(c0) && !ViewCentred(c1),"one camera placement cannot mark another pilot centred");
    observing=true;Check(!ViewCentred(c0),"free observation cannot claim the weapon follows the displayed centre");observing=false;
    // Real torso animation includes the shield-selection 40-degree left glance, guard turn and melee twist.
    for(int action=0;action<3;++action) {
        sazabi::PoseInput in{};sazabi::Anim animation{};sazabi::Pose animated{};sazabi::Reset(c0.rig,&animated);
        animation.ready=1.0f;
        if(action==0){animation.glance=0.35f;animation.special=0;}
        if(action==1)animation.guard=1.0f;
        if(action==2){animation.melee=1.0f;animation.twist=35.0f;}
        for(int step=0;step<60;++step)sazabi::Torso(in,1.0f/60.0f,animation,&animated);
        sazabi::Finish(&animated);
        auto decorated=fpEyes[0];
        for(int row=0;row<3;++row)for(int col=0;col<3;++col)decorated.m[row*4+col]=animated.modelRot[sazabi::kHead].m[row*3+col];
        const auto raw=decorated;fpEyes[0]=raw;
        if(action==0)Check(std::fabs(std::atan2(raw.m[8],raw.m[10])-40.0f*sazabi::kDeg)<0.01f,
                           "production head pose really contains the 40-degree left glance");
        Check(SazabiFirstPersonFrame(cameraVehicles[0],0,decorated.m) && std::memcmp(decorated.m,expected.m,64)==0,
              action==0 ? "40-degree animated left glance never rotates the pilot camera" : action==1 ?
              "guard chest/head animation never rotates the pilot camera" : "melee torso twist never rotates the pilot camera");
        Check(std::memcmp(decorated.m+12,raw.m+12,16)==0 && std::memcmp(fpEyes[0].m,raw.m,64)==0,
              "animation camera keeps the physical eye exactly and never writes the raw marker");
    }
    c0.heading=-1.1f;c0.aimPitch=-0.4f;PublishView(c0,cameraVehicles[0]);
    std::memcpy(cameraFrame,fpEyes[0].m,64);expected=fpview::HeadingFrame(c0.heading,c0.aimPitch,fpEyes[0].m+12);
    Check(SazabiFirstPersonFrame(cameraVehicles[0],0,cameraFrame) && std::memcmp(cameraFrame,expected.m,64)==0,
          "authoritative heading and pitch can turn back in the opposite direction");
    const View cockpit=ViewOf(c0);ViewRay(cockpit,c0.root+12,eye,dir);
    Check(cockpit.cockpit && Near(eye,fpEyes[0].m+12) && Near(dir,expected.m+8),"game-thread FP ray uses the same raw eye and authoritative direction");
    // Native camera evaluation temporarily uses model origin, while game input sees the collision centre 12.8m up.
    std::memcpy(cameraVehicles[0]+kPosition,c0.root+12,12);float placedEye[3],placedLook[3];
    Check(SazabiCamera(cameraSeats[0],nullptr,nullptr,placedEye,placedLook) && Near(placedEye,eye),
          "camera-thread model origin reconstitutes the same physical FP eye without the collision-centre offset");
    Put<float>(cameraVehicles[0],kPosition+4,c0.root[13]+12.8f);
    float centreEye[3],centreDir[3];ViewRay(ViewOf(c0),c0.root+12,centreEye,centreDir);
    Check(Near(centreEye,eye) && Near(centreDir,dir),"game-thread collision-centre shift cannot contaminate the FP ray");
    Aim(c0);const float aimedPoint[3]={eye[0]+dir[0]*kAimRange,eye[1]+dir[1]*kAimRange,eye[2]+dir[2]*kAimRange};
    Check(Near(lastRayFrom,eye) && Near(lastRayEnd,aimedPoint) && Near(c0.arms.aim,aimedPoint),"production FP Aim fires its ray from the physical monoeye");
    AssistSeen seen{};Gather(c0,cameraVehicles[0],0.4f,800.0f,&seen);
    Check(Near(seen.eye,eye) && Near(seen.dir,dir),"production FP assist gathers from the same eye and direction as Aim");
    fpRequests[0]=false;PublishView(c0,cameraVehicles[0]);const View third=ViewOf(c0);float thirdEye[3],thirdDir[3];
    ViewRay(third,c0.root+12,thirdEye,thirdDir);
    Check(!third.cockpit && !Near(thirdEye,eye) && Near(thirdDir,dir),"third person restores the original shoulder boom while keeping the same aim direction");
    Check(!SazabiFirstPersonFrame(cameraVehicles[0],0,cameraFrame),"third-person request does not rewrite a cockpit frame");
    fpRequests[0]=true;PublishView(c0,cameraVehicles[0]);
    Check(!SazabiFirstPersonFrame(cameraVehicles[0],1,cameraFrame) && !SazabiFirstPersonFrame(assistVehicle,0,cameraFrame),"wrong seat and wrong vehicle cannot borrow a published view");
    View snapshot{};Check(ReadView(cameraVehicles[0],nullptr,&snapshot),"camera view validates its live vehicle-seat-rider identity");
    Put<LONG>(newVehicleControl,8,1);Put<void*>(cameraVehicles[0],kSelfCtrl,newVehicleControl);
    Check(!ReadView(cameraVehicles[0],nullptr,&snapshot),"same-address vehicle with a new control rejects the old view");
    Put<void*>(cameraVehicles[0],kSelfCtrl,cameraControls[0][0]);
    Put<LONG>(newRiderControl,8,1);Put<void*>(cameraHumans[0],kSelfCtrl,newRiderControl);
    Check(!ReadView(cameraVehicles[0],nullptr,&snapshot),"same-address rider with a new control rejects the old view");
    Put<void*>(cameraHumans[0],kSelfCtrl,cameraControls[0][1]);
    Put<void*>(cameraSeats[0],kSeatRider,cameraHumans[1]);Check(!ReadView(cameraVehicles[0],nullptr,&snapshot),"another occupant cannot inherit the old pilot view");
    Put<void*>(cameraSeats[0],kSeatRider,cameraHumans[0]);
    Put<void*>(cameraVehicles[0],kSeats,cameraSeats[1]);Check(!ReadView(cameraVehicles[0],nullptr,&snapshot),"replacement seat array rejects the old seat identity");
    Put<void*>(cameraVehicles[0],kSeats,cameraSeats[0]);
    cameraHumans[0][kDead]=1;Check(!ReadView(cameraVehicles[0],nullptr,&snapshot),"dead rider rejects the view");cameraHumans[0][kDead]=0;
    Put<LONG>(cameraControls[0][0],8,0);Check(!ReadView(cameraVehicles[0],nullptr,&snapshot),"expired vehicle control rejects the view");Put<LONG>(cameraControls[0][0],8,1);
    testNow+=kViewFresh+1;Check(!ReadView(cameraVehicles[0],nullptr,&snapshot),"stale published view cannot steer a camera");
    PublishView(c0,cameraVehicles[0]);PublishView(c1,cameraVehicles[1]);--testNow;
    Check(!ReadView(cameraVehicles[0],nullptr,&snapshot),"future view timestamp is rejected");++testNow;
    DropView(cameraVehicles[0]);Check(!ReadView(cameraVehicles[0],nullptr,&snapshot) && ReadView(cameraVehicles[1],nullptr,&snapshot),"leaving one mech preserves the other local pilot view");
    std::memcpy(cameraFrame,fpEyes[1].m,64);Check(SazabiFirstPersonFrame(cameraVehicles[1],0,cameraFrame),"second mech keeps its own first-person camera after the first leaves");
    ResetSazabi();Check(!ReadView(cameraVehicles[1],nullptr,&snapshot) && !ViewCentred(c1),"mission reset retires every view and centred timestamp");
    fpVehicles[0]=fpVehicles[1]=nullptr;fpHumans[0]=fpHumans[1]=nullptr;fpRequests[0]=fpRequests[1]=false;
    config=Config{};

    // Real Pose writes and BoneAt/DockPoint reads, interleaved across two different rigs/poses.
    // Both bone arrays are already resolved, so no game model lookup is needed.
    alignas(16) unsigned char vehicle[kModelInst506+kInstBones506+sizeof(void*)]{};
    alignas(16) unsigned char records[2][sazabi::kBoneCount][kBoneLocal506+64]{};
    Mech a{},b{};
    for(int j=0;j<2;++j) {
        Mech& m=j==0 ? a : b;
        m.rigOk=true;
        for(int i=0;i<16;++i)m.root[i]=i%5==0 ? 1.0f : 0.0f;
        for(int i=0;i<sazabi::kBoneCount;++i) {
            m.rec[i]=records[j][i];
            m.rig.joint[i][0]=static_cast<float>(i%3)*(j==0 ? 1.0f : 4.0f);
            m.rig.joint[i][1]=static_cast<float>(i)*(j==0 ? 0.5f : 2.0f);
            m.rig.joint[i][2]=static_cast<float>(i%5);
        }
    }
    b.pose.aim=1.0f;b.pose.aimPitch=0.5f;b.pose.guard=1.0f;
    Pose(a,vehicle);
    float muzzle[3],dock[3],blade[3];
    BoneAt(a,sazabi::kMuzzle,nullptr,muzzle);
    DockPoint(a,0,dock);
    BoneAt(a,sazabi::kAxeBlade,nullptr,blade);
    Pose(b,vehicle);
    float got[3],other[3];
    BoneAt(a,sazabi::kMuzzle,nullptr,got);
    BoneAt(b,sazabi::kMuzzle,nullptr,other);
    Check(!Near(muzzle,other),"second mech has a distinguishable pose");
    Check(Near(muzzle,got),"aim and NPC muzzle remain this mech's after another mech is posed");
    DockPoint(a,0,got);
    Check(Near(dock,got),"funnel dock remains this mech's after another mech is posed");
    BoneAt(a,sazabi::kAxeBlade,nullptr,got);
    Check(Near(blade,got),"melee blade remains this mech's after another mech is posed");

    // Run the production target collection and funnel flight: A vanishes while a funnel still attacks B.
    visibleCount=4;
    for(int i=0;i<visibleCount;++i) {
        visibleObjects[i]=&objects[i];
        visiblePositions[i][0]=static_cast<float>(50+i*10);
        visiblePositions[i][1]=13.0f;
    }
    FlyFunnels(a,vehicle,false,1.0f/60.0f);
    Check(a.arms.funnelSlot[1]==&objects[1],"B initially occupies target slot 1");
    auto& onB=a.arms.funnels[0];
    onB.phase=FunnelPhase::station;onB.target=1;onB.wait=1.0f;
    std::memcpy(onB.at,visiblePositions[1],12);
    float bPosition[3];std::memcpy(bPosition,onB.at,12);
    auto& onA=a.arms.funnels[1];
    onA.phase=FunnelPhase::station;onA.target=0;onA.wait=1.0f;
    std::memcpy(onA.at,visiblePositions[0],12);
    for(int i=0;i<3;++i){visibleObjects[i]=visibleObjects[i+1];std::memcpy(visiblePositions[i],visiblePositions[i+1],12);}
    visibleCount=3;
    FlyFunnels(a,vehicle,false,1.0f/60.0f);
    Check(a.arms.funnelSlot[0]==&objects[1] && onB.target==0,"B's funnel follows B to its compacted slot");
    Check(onB.phase==FunnelPhase::station && Near(onB.at,bPosition),"B's funnel stays on station without teleporting to C");
    Check(onA.phase==FunnelPhase::transit,"the lost A target is reacquired through a flight path");
    std::printf("Sazabi runtime: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
