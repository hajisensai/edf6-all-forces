// The Katyusha's launcher pose (README 喀秋莎; tools/make_katyusha.py, pylib/katyusha_model.py; autoturret/docs/
// re-notes.md "The Katyusha's camera and pose").
//  - The camera. A Vehicle402_Rocket rider aims the launcher with the seat's aim (VehicleWeaponAim, seat+0xE0: the
//    stick turns its yaw / pitch axes, 0x5FD948 -> +0x2AA0 -> slot 2 0x5FBDA0), and the camera follows those axes: when
//    EDF6AutoTurret steered the axes onto the rockets' high arc (75..79 deg, the user's log 2026-10-05 18:36) the
//    player stared at the sky. So the axes stay the player's (EDF6AutoTurret leaves a lofted launcher the player rides
//    alone: PlayerLofted), the player looks at a ground point as in any vehicle, and only the launcher's bone is
//    lifted onto the arc that lands there (LauncherFrame works out the elevation from where the camera looks:
//    launcher.cpp LoftWant). The weapon fires along that bone (vehicle_weapon_setting Rocketcannon_main: the muzzles'
//    rows are the bone's, 0x633DD0), so the rockets and the impact cross follow it.
//  - Where. The class's slot 45 (0x5FD980) poses the launcher (0x5FDDA0: the turntable's yaw and the launcher's pitch
//    from the axes, 0x661C00 / 0x661810; the prop turned by the launcher's elevation) and then builds the model's world
//    matrices from the bones' locals (0x1100B90 -> 0x1100010: world = local x parent's world, bones in order). The
//    call at 0x5FD99C is detoured here: the stock pose runs, then the launcher's local (held elevation) and the ram's
//    two locals are written, and the world matrices are built from them as usual.
//  - The ram. The Naegling's prop is one bone turned by the launcher's elevation about the cylinder's pivot: its eye
//    leaves the launcher past ~52 deg and hangs 1.3 m off it at 80 (pylib/katyusha_model.py ram_report). The model
//    gives the rod a bone of its own (kRod, at the eye); both are turned by the angle the line from the cylinder's
//    pivot to the eye (fixed on the launcher) has turned since bind: the cylinder stays on the turntable, the eye on
//    the launcher, the rod slides in the cylinder (pylib/katyusha_model.py ram_pose / check_ram are this, offline).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "body506.h"
#include "memory.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr unsigned kPoseCall=0x5FD99C,kPose=0x5FDDA0;
const unsigned char kPoseCallCode[]={0xE8,0xFF,0x03,0x00,0x00};   // call 0x5FDDA0 (rel32 0x3FF from 0x5FD9A1)
// The model's skeleton (*(veh+0xEE0)): bone records at +0x58 (stride 0xD0), the last index at +0x68, a record's bind
// local matrix at +0x20 (0x661865..0x66189F reads the launcher's there).
constexpr std::size_t kSkeleton=0x0,kSkeletonBones=0x58,kSkeletonLast=0x68,kSkeletonStride=0xD0,kSkeletonLocal=0x20;
// pylib/katyusha_model.py RAM_ROD; the stock bones the pose touches.
const wchar_t kRod[]=L"edf6vc_ram_rod";
const wchar_t kMain[]=L"Rocketcannon_main";
const wchar_t kProp[]=L"Rocketcannon_prop";
// How fast the launcher is lifted to the elevation LauncherFrame asks for, rad/s: the stock turret's own pitch rate
// (about 1.1 rad/s, autoturret/docs/re-notes.md). A request older than kWantMs is gone (the player got out).
constexpr float kLoftRate=1.1f;
constexpr ULONGLONG kWantMs=200;
constexpr ULONGLONG kStaleMs=2000;   // a launcher not posed this long is gone: its slot is free
constexpr int kMaxRacks=16;

using PoseFn=void(__fastcall*)(void*);
PoseFn stockPose=nullptr;
bool poseOk=false;

struct Rack {
    ObjRef ref;
    ULONGLONG seen;
    const void* bones;             // the bone array the records are in (looked up again when it changes)
    unsigned char *main,*prop,*rod;   // nullptr rod: its bones unreadable: left stock
    float mainBind[16],propBind[16],rodBind[16];
    bool aim;                      // LauncherFrame's request: hold the launcher at `want`
    float want;
    ULONGLONG wantAt;
    bool held;                     // the launcher is held off its stock pose, at `elev`
    float elev,stock,ramTurn,ramLength;
};
Rack racks[kMaxRacks]{};
SRWLOCK rackLock=SRWLOCK_INIT;

float Clamp1(float v) noexcept { return v<-1.0f ? -1.0f : v>1.0f ? 1.0f : v; }

// Rack `v` is in: a new one in a free slot (or a gone vehicle's at the same address, or a stale one's). Under rackLock.
Rack* RackOf(const void* v,ULONGLONG ms,bool make) noexcept {
    Rack* slot=nullptr;
    for(auto& r:racks) {
        if(r.ref.Is(v))return &r;
        if(!slot && (!r.ref || r.ref.obj==v || ms-r.seen>kStaleMs))slot=&r;
    }
    if(!make || !slot)return nullptr;
    *slot=Rack{};slot->ref=ObjRef::Of(v);slot->seen=ms;
    return slot;
}

// Bone record `rec`'s bind local matrix from the model's skeleton (the stock writes the launcher's and the prop's
// locals every frame, so the instance's own are posed ones).
bool SkeletonBind(const unsigned char* inst,const unsigned char* bones,const unsigned char* rec,float* out) noexcept {
    const auto skeleton=At<const unsigned char*>(inst,kSkeleton);
    if(!Readable(skeleton,kSkeletonLast+4))return false;
    const auto index=static_cast<std::int32_t>((rec-bones)/static_cast<std::ptrdiff_t>(kBoneStride));
    const auto records=At<const unsigned char*>(skeleton,kSkeletonBones);
    if(index<0 || index>At<std::int32_t>(skeleton,kSkeletonLast))return false;
    const unsigned char* at=records+static_cast<std::size_t>(index)*kSkeletonStride+kSkeletonLocal;
    if(!Readable(at,64))return false;
    std::memcpy(out,at,64);
    for(int i=0;i<16;++i)if(!std::isfinite(out[i]))return false;
    return true;
}

// The records and binds of v's launcher, prop and rod, looked up when its bone array is new. False: not ours.
bool Resolve(Rack& r,const unsigned char* v) noexcept {
    const unsigned char* inst=v+kModelInst506;
    const auto bones=At<const unsigned char*>(inst,kInstBones506);
    if(!bones)return false;
    if(bones==r.bones)return r.rod!=nullptr;
    r.bones=bones;r.held=false;
    r.rod=BoneRecord506(inst,kRod);
    r.main=BoneRecord506(inst,kMain);
    r.prop=BoneRecord506(inst,kProp);
    if(!r.rod || !r.main || !r.prop || !SkeletonBind(inst,bones,r.main,r.mainBind) || !SkeletonBind(inst,bones,r.prop,r.propBind)
       || !SkeletonBind(inst,bones,r.rod,r.rodBind)) {
        if(r.rod)Log("KATYUSHA v=%p: its bones or their binds unreadable (main=%p prop=%p): the stock pose",v,r.main,r.prop);
        r.rod=nullptr;
        return false;
    }
    Log("KATYUSHA v=%p: launcher, cylinder and rod bones found",v);
    return true;
}

// `rows` (a 4x4's three rotation rows, row vectors) times the turn `d` rad about the parent's X, the far (backward)
// end rising: x -> x, y -> (0, cos, sin), z -> (0, -sin, cos) (pylib/katyusha_model.py ram_turn). The launcher's nose
// rising by e is RamTurn(-e).
void RamTurn(const float* rows,float d,float* out) noexcept {
    const float c=std::cos(d),s=std::sin(d);
    for(int i=0;i<3;++i) {
        const float* r=rows+i*4;
        out[i*4+0]=r[0];out[i*4+1]=r[1]*c-r[2]*s;out[i*4+2]=r[1]*s+r[2]*c;out[i*4+3]=r[3];
    }
}

// The rod's eye where the launcher's local `main` puts it (in the turntable's frame): its bind offset from the
// launcher's pivot taken into the launcher's bind frame, then out through `main`.
void EyeNow(const Rack& r,const float* main,float* eye) noexcept {
    const float* b=r.mainBind;
    const float d[3]={r.rodBind[12]-b[12],r.rodBind[13]-b[13],r.rodBind[14]-b[14]};
    float q[3];
    for(int i=0;i<3;++i)q[i]=d[0]*b[i*4]+d[1]*b[i*4+1]+d[2]*b[i*4+2];   // x bind rotation's transpose
    for(int c=0;c<3;++c)eye[c]=q[0]*main[c]+q[1]*main[4+c]+q[2]*main[8+c]+main[12+c];
}

// The ram's angle from the cylinder's pivot to `eye`, rad up from the turntable's backward (ram_angle).
float RamAngle(const float* pivot,const float* eye) noexcept { return std::atan2(eye[1]-pivot[1],-(eye[2]-pivot[2])); }

// The launcher held at r.elev, eased toward what LauncherFrame wants (or back to the stock pose when nothing is wanted;
// once there it is left to the stock pose again). Its local is written when held.
void Launcher(Rack& r,ULONGLONG ms) noexcept {
    float* local=reinterpret_cast<float*>(r.main+kBoneLocal506);
    r.stock=std::asin(Clamp1(local[9]));   // the stock pose's forward (row 2), its rise on the turntable
    const bool aim=r.aim && ms-r.wantAt<=kWantMs;
    if(!aim && !r.held)return;
    if(!r.held){r.held=true;r.elev=r.stock;}
    const float step=kLoftRate/60.0f,target=aim ? r.want : r.stock;
    r.elev+=target-r.elev>step ? step : target-r.elev<-step ? -step : target-r.elev;
    if(!aim && std::fabs(r.elev-r.stock)<1e-4f){r.held=false;return;}
    float set[16];
    RamTurn(r.mainBind,-r.elev,set);
    std::memcpy(set+12,local+12,16);   // the stock's translation: the hinge's pivot
    std::memcpy(local,set,sizeof(set));
}

// The ram's two bones aimed at each other's pivot from the launcher's local as it is now.
void Ram(Rack& r) noexcept {
    const float* main=reinterpret_cast<const float*>(r.main+kBoneLocal506);
    const float* pivot=r.propBind+12;
    float eye[3];
    EyeNow(r,main,eye);
    const float turn=RamAngle(pivot,eye)-RamAngle(pivot,r.rodBind+12);
    if(!std::isfinite(turn))return;
    float prop[16],rod[16];
    RamTurn(r.propBind,turn,prop);std::memcpy(prop+12,r.propBind+12,16);
    RamTurn(r.rodBind,turn,rod);std::memcpy(rod+12,eye,12);rod[15]=1.0f;
    std::memcpy(r.prop+kBoneLocal506,prop,sizeof(prop));
    std::memcpy(r.rod+kBoneLocal506,rod,sizeof(rod));
    r.ramTurn=turn;
    r.ramLength=std::sqrt((eye[1]-pivot[1])*(eye[1]-pivot[1])+(eye[2]-pivot[2])*(eye[2]-pivot[2]));
}

void Pose(unsigned char* v) noexcept {
    const ULONGLONG ms=GameMs();
    AcquireSRWLockExclusive(&rackLock);
    Rack* r=RackOf(v,ms,false);
    // A slot only for the Katyusha's model (the rod bone): a stock Naegling is probed each frame and left stock.
    if(!r && BoneRecord506(v+kModelInst506,kRod))r=RackOf(v,ms,true);
    if(r) {
        r->seen=ms;
        if(Resolve(*r,v)){Launcher(*r,ms);Ram(*r);}
    }
    ReleaseSRWLockExclusive(&rackLock);
}

// The stock pose skips a dead vehicle (0x5FDDAB): so does this (the wreck's bones are the physics').
void __fastcall PoseHook(void* v) {
    stockPose(v);
    if(!Cfg().enabled)return;
    __try { if(!static_cast<unsigned char*>(v)[kDead])Pose(static_cast<unsigned char*>(v)); } __except(EXCEPTION_EXECUTE_HANDLER){}
}
}  // namespace

bool InstallKatyusha() noexcept {
    if(!Matches(kPoseCall,kPoseCallCode,sizeof(kPoseCallCode))) {
        Log("KATYUSHA pose call at %#x changed (another plugin?): the launcher keeps its stock pose (no loft, stock ram)",kPoseCall);
        return false;
    }
    unsigned char stub[14]={0xFF,0x25,0,0,0,0};   // jmp [rip] -> PoseHook
    const auto hook=reinterpret_cast<std::uintptr_t>(&PoseHook);
    std::memcpy(stub+6,&hook,8);
    unsigned char* const at=image+kPoseCall;
    void* const page=AllocateNearCode(at,stub,sizeof(stub));
    if(!page){Log("KATYUSHA no code page near EDF.dll: the stock pose");return false;}
    stockPose=reinterpret_cast<PoseFn>(image+kPose);
    const auto rel=static_cast<std::int32_t>(static_cast<unsigned char*>(page)-(at+5));
    unsigned char call[5]={0xE8,0,0,0,0};
    std::memcpy(call+1,&rel,4);
    poseOk=edf::PatchCode(at,kPoseCallCode,call,sizeof(call));
    if(!poseOk)VirtualFree(page,0,MEM_RELEASE);
    Log("KATYUSHA launcher pose hook=%d",poseOk);
    return poseOk;
}

void SetLauncherLoft(const void* vehicle,bool aim,float elevation) noexcept {
    const ULONGLONG ms=GameMs();
    AcquireSRWLockExclusive(&rackLock);
    if(Rack* r=RackOf(vehicle,ms,false)) {
        r->aim=aim && std::isfinite(elevation);
        r->want=elevation;r->wantAt=ms;
    }
    ReleaseSRWLockExclusive(&rackLock);
}

bool LauncherLoft(const void* vehicle,LoftReadout* out) noexcept {
    bool ok=false;
    AcquireSRWLockShared(&rackLock);
    for(const auto& r:racks) {
        if(!r.ref.Is(vehicle) || !r.rod)continue;
        *out=LoftReadout{r.held,r.held ? r.elev : r.stock,r.stock,r.ramTurn,r.ramLength};
        ok=true;
        break;
    }
    ReleaseSRWLockShared(&rackLock);
    return ok;
}

void ResetKatyushas() noexcept {
    AcquireSRWLockExclusive(&rackLock);
    for(auto& r:racks)r=Rack{};
    ReleaseSRWLockExclusive(&rackLock);
}
}  // namespace crew
