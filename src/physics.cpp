// Stock EDF6 physics defects the plugin corrects at load (docs/re-notes.md, "vanilla collision").
// EDF5 ran Havok hkp: vehicles were hkpVehicleInstance bodies and the terrain was welded, and a character
// pushed by a dynamic body was limited to maxForce. EDF6 moved to hknp with game-side wheels; the vehicle
// chassis lost the motion welding and the character's vertical contacts lost the force limit on the way.
#include "crew.h"
#include "memory.h"

#include <atomic>
#include <cstring>

namespace crew {
namespace {
// Overwrite `size` code bytes at image+rva with `value`, after the `expect` bytes at image+checkRva
// (covering the patched bytes) still read as the stock code. False and untouched otherwise.
bool PatchCode(std::size_t checkRva,const unsigned char* expect,std::size_t expectSize,
               std::size_t rva,const unsigned char* value,std::size_t size) noexcept {
    if(rva<checkRva || rva+size>checkRva+expectSize)return false;
    if(!Matches(checkRva,expect,expectSize))return false;
    unsigned char* at=image+rva;
    DWORD old=0;
    if(!VirtualProtect(at,size,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(at,value,size);
    VirtualProtect(at,size,old,&old);
    FlushInstructionCache(GetCurrentProcess(),at,size);
    return true;
}

// The helicopter body build (0x656E90, called from 0x64E9B6 / 0x650AA6) sets the
// hknp body quality to CHARACTER (10): `mov byte [rbp+0x86],0x0A` at 0x6571AD. NOT the car/tank chassis:
// CarBase/TankBase take [veh+0x1698] from the model's physics data (car_base_body_name, 0x663A70), so
// this patch never reaches them. In the default quality
// library (0xE13BA0) CHARACTER only asks for NEIGHBOR welding (flags 0x80), VEHICLE (9) asks for
// NEIGHBOR|MOTION (0x180); solver iterations are the same. Without motion welding a fast chassis sliding
// over the triangle seams of uneven ground meets the inner edges as ghost contacts and is thrown up.
// The fix is the quality the engine provides for exactly this body: VEHICLE.
constexpr std::size_t kChassisQualityAt=0x6571AD;
constexpr unsigned char kChassisQualityCode[]={0xC6,0x85,0x86,0x00,0x00,0x00,0x0A};
constexpr unsigned char kQualityVehicle=0x09;

bool WeldVehicleChassis() noexcept {
    const std::size_t immediate=kChassisQualityAt+sizeof(kChassisQualityCode)-1;
    return PatchCode(kChassisQualityAt,kChassisQualityCode,sizeof(kChassisQualityCode),
                     immediate,&kQualityVehicle,1);
}

// Character proxy vertical contacts (0x11DDFD0, rdi = character, rbx = contact point): the constraint
// block at [rsp+0x40] gets {0 or 1, maxImpulse, 1}, and maxImpulse is loaded at 0x11DE049 from
// HK_REAL_HIGH (0x18474B0) unconditionally. The character's maxForce ([character+0x70], copied from
// cinfo+0x98 at 0x11DD07F, default 1000) never reaches this path, so a giant standing on (or under) a
// moving dynamic body -- a mother monster, a ragdoll, debris -- takes an unlimited impulse and is launched
// or rammed into the ground. hkp limited exactly this contact to maxForce*dt. The cap is restored for
// the bodies the engine's own character push (0xD95D38) treats as pushable: flags&7 == DYNAMIC.
// Static terrain and keyframed (animated) bodies keep the unlimited support.
constexpr std::size_t kHighRealAt=0x18474B0;
constexpr std::size_t kBodyManagerVtable=0x18BC9C0;  // [character+0x28]; bodies [+0x18], count [+0x20]
constexpr std::size_t kCheckSupportSlot=0x1AE4D50;
constexpr std::size_t kCheckSupportFn=0x11DE5B0;
constexpr std::size_t kVerticalContactAt=0x11DE042;
constexpr unsigned char kVerticalContactCode[]={0x80,0x7B,0x30,0x01,0x8B,0x43,0x24,0xF3,
                                                0x0F,0x10,0x05,0x5F,0x94,0x66,0x00,0x89};
constexpr std::size_t kMaxImpulseLoadAt=0x11DE049;  // movss xmm0,[HK_REAL_HIGH], 8 bytes
constexpr std::size_t kMaxImpulseResumeAt=0x11DE051;
constexpr std::size_t kBodyStride=0xB0;
constexpr std::uint32_t kBodyFlagsMotionMask=7;  // STATIC=1 DYNAMIC=2 KEYFRAMED=4
constexpr std::uint32_t kBodyFlagDynamic=2;

std::atomic<float> stepDt{1.0f/60.0f};

// checkSupport (slot image+0x1AE4D50) runs ahead of the contact build with the solver step: dt at +8.
using CheckSupportFn=std::uintptr_t(*)(void*,const float*,void*,void*);
CheckSupportFn checkSupport;

std::uintptr_t CheckSupportHook(void* self,const float* stepInfo,void* out,void* extra) noexcept {
    if(stepInfo && stepInfo[2]>0.0f)stepDt.store(stepInfo[2],std::memory_order_relaxed);
    return checkSupport(self,stepInfo,out,extra);
}

// The body manager is read inline (getter vt+0x80 = [mgr+0x18]+idx*0xB0) instead of called: 0x11DE0E2
// calls that slot and drops the result, so another implementation may pair it with a release.
const unsigned char* DynamicBody(const unsigned char* character,std::uint32_t id) noexcept {
    const auto* mgr=At<const unsigned char*>(character,0x28);
    if(!mgr || At<const void*>(mgr,0)!=image+kBodyManagerVtable)return nullptr;
    const std::uint32_t index=id&0xFFFFFF;
    if(index>=At<std::uint32_t>(mgr,0x20))return nullptr;
    const auto* body=At<const unsigned char*>(mgr,0x18)+std::size_t{index}*kBodyStride;
    if(At<std::uint32_t>(body,0x50)!=id)return nullptr;
    return (At<std::uint32_t>(body,0x54)&kBodyFlagsMotionMask)==kBodyFlagDynamic ? body : nullptr;
}

float VerticalContactMaxImpulse(const unsigned char* character,const unsigned char* point) noexcept {
    const float unlimited=At<float>(image,kHighRealAt);
    __try {
        if(!DynamicBody(character,At<std::uint32_t>(point,0x20)))return unlimited;
        const float maxForce=At<float>(character,0x70);
        return maxForce>0.0f ? maxForce*stepDt.load(std::memory_order_relaxed) : unlimited;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return unlimited;
    }
}

// Cave for 0x11DE049: flags (cmp at 0x11DE042, read by the jne at 0x11DE078) and eax (stored at 0x11DE051)
// are live, as are all volatile registers but xmm0. rsp is 16-aligned there (3 pushes + sub 0x170), so
// pushfq + 7 pushes + 0x80 keeps the call aligned with 0x20 shadow space below the xmm1-5 saves.
void* BuildMaxImpulseCave() noexcept {
    unsigned char cave[]={
        0x9C,0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53,  // pushfq; rax rcx rdx r8-r11
        0x48,0x81,0xEC,0x80,0x00,0x00,0x00,                           // sub rsp,0x80
        0xF3,0x0F,0x7F,0x4C,0x24,0x20,0xF3,0x0F,0x7F,0x54,0x24,0x30,  // movdqu [rsp+0x20..0x60],xmm1-5
        0xF3,0x0F,0x7F,0x5C,0x24,0x40,0xF3,0x0F,0x7F,0x64,0x24,0x50,
        0xF3,0x0F,0x7F,0x6C,0x24,0x60,
        0x48,0x89,0xF9,0x48,0x89,0xDA,                                // rcx=character rdx=point
        0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,                          // mov rax,helper; call rax
        0xF3,0x0F,0x6F,0x4C,0x24,0x20,0xF3,0x0F,0x6F,0x54,0x24,0x30,  // restore xmm1-5
        0xF3,0x0F,0x6F,0x5C,0x24,0x40,0xF3,0x0F,0x6F,0x64,0x24,0x50,
        0xF3,0x0F,0x6F,0x6C,0x24,0x60,
        0x48,0x81,0xC4,0x80,0x00,0x00,0x00,                           // add rsp,0x80
        0x41,0x5B,0x41,0x5A,0x41,0x59,0x41,0x58,0x5A,0x59,0x58,0x9D,  // pop r11-r8 rdx rcx rax; popfq
        0xFF,0x25,0,0,0,0,0,0,0,0,0,0,0,0};                           // jmp [rip] -> resume
    constexpr std::size_t kHelperAt=12+7+30+6+2;  // after the pushes, sub, xmm saves, args, 48 B8
    constexpr std::size_t kResumeAt=sizeof(cave)-8;
    static_assert(sizeof(cave)==kHelperAt+8+2+30+7+12+14,"cave layout");
    const auto helper=reinterpret_cast<std::uintptr_t>(&VerticalContactMaxImpulse);
    const auto resume=reinterpret_cast<std::uintptr_t>(image+kMaxImpulseResumeAt);
    std::memcpy(cave+kHelperAt,&helper,sizeof(helper));
    std::memcpy(cave+kResumeAt,&resume,sizeof(resume));
    return AllocateNearCode(image+kMaxImpulseLoadAt,cave,sizeof(cave));
}

bool CapGiantContact() noexcept {
    auto** slot=reinterpret_cast<void**>(image+kCheckSupportSlot);
    if(!Matches(kVerticalContactAt,kVerticalContactCode,sizeof(kVerticalContactCode)))return false;
    if(*slot!=image+kCheckSupportFn)return false;
    void* cave=BuildMaxImpulseCave();
    if(!cave)return false;
    checkSupport=reinterpret_cast<CheckSupportFn>(image+kCheckSupportFn);
    if(!PatchVtableSlot(slot,image+kCheckSupportFn,reinterpret_cast<void*>(&CheckSupportHook))) {
        VirtualFree(cave,0,MEM_RELEASE);
        return false;
    }
    const auto rel=static_cast<std::int32_t>(static_cast<unsigned char*>(cave)-(image+kMaxImpulseLoadAt+5));
    unsigned char jump[8]={0xE9,0,0,0,0,0x90,0x90,0x90};
    std::memcpy(jump+1,&rel,sizeof(rel));
    if(PatchCode(kVerticalContactAt,kVerticalContactCode,sizeof(kVerticalContactCode),
                 kMaxImpulseLoadAt,jump,sizeof(jump)))return true;
    // The dt hook alone is harmless; leave it, but the unreached cave can go.
    VirtualFree(cave,0,MEM_RELEASE);
    return false;
}

// Diagnostic (debug only). The first probe showed every launch starts inside the hknp step: the game's
// final write (setLinVel at 0x6746B5) is under 3 m/s up, then the body comes back from the step at 8-16.
// This one keeps, per chassis body, the last frames of what the game wrote (linear 0x6746B5 and angular
// 0x6746C6, setAngVel 0x11B1760) next to what the step left (getLinVel 0x11B1300, getAngVel 0x11B1060),
// the body position (0x11B15B0, hknpBody+0x30) and its up axis (rotation column 1), and dumps them at the
// launch frame: whether the game-forced spin drives the hull into the ground before Havok pushes it out.
constexpr std::size_t kSetLinVel=0x11B18F0,kGetLinVel=0x11B1300;
constexpr std::size_t kSetAngVel=0x11B1760,kGetAngVel=0x11B1060,kGetPosition=0x11B15B0;
constexpr std::size_t kFinalLinSite=0x6746B5,kFinalAngSite=0x6746C6;
constexpr float kLaunchSpeed=3.0f;   // m/s up out of the step
constexpr float kLaunchJump=3.0f;    // m/s more than the game wrote before it
constexpr int kFrames=12;

using SetVecFn=std::uintptr_t(*)(void*,const float*);
using GetVecFn=const float*(*)(void*);
using GetVecOutFn=const float*(*)(void*,float*);

struct Frame { ULONGLONG t; float pos[3],up[3],physLin[3],physAng[3],outLin[3],outAng[3]; };
struct Track { void* body; int next; ULONGLONG dumped; Frame f[kFrames]; };
Track tracks[8]{};

Track& TrackOf(void* body) noexcept {
    for(Track& t:tracks)if(t.body==body)return t;
    static unsigned victim=0;
    Track& t=tracks[victim++%8];
    t=Track{};
    t.body=body;
    return t;
}

void Copy3(float* to,const float* from) noexcept { to[0]=from[0]; to[1]=from[1]; to[2]=from[2]; }

void DumpTrack(const Track& t) noexcept {
    Log("VELPROBE2 launch w=%p (oldest first)",t.body);
    for(int i=0;i<kFrames;++i) {
        const Frame& f=t.f[(t.next+i)%kFrames];
        if(!f.t)continue;
        Log("VELPROBE2 t=%llu pos=(%.2f %.2f %.2f) up=(%.2f %.2f %.2f) phys=(%.2f %.2f %.2f) ang=(%.2f %.2f %.2f)"
            " out=(%.2f %.2f %.2f) outAng=(%.2f %.2f %.2f)",static_cast<unsigned long long>(f.t),
            f.pos[0],f.pos[1],f.pos[2],f.up[0],f.up[1],f.up[2],f.physLin[0],f.physLin[1],f.physLin[2],
            f.physAng[0],f.physAng[1],f.physAng[2],f.outLin[0],f.outLin[1],f.outLin[2],
            f.outAng[0],f.outAng[1],f.outAng[2]);
    }
}

void DumpNeighbors(void* wrapper,const float* at) noexcept;

void RecordLin(void* body,const float* v) noexcept {
    __try {
        if(body && v) {
            Track& t=TrackOf(body);
            const Frame& last=t.f[(t.next+kFrames-1)%kFrames];
            Frame& f=t.f[t.next];
            f=Frame{};
            f.t=GameMs();
            const float* pos=reinterpret_cast<GetVecFn>(image+kGetPosition)(body);
            Copy3(f.pos,pos);
            Copy3(f.up,pos-0x30/4+0x10/4);   // rotation column 1 of the body transform
            Copy3(f.physLin,reinterpret_cast<GetVecFn>(image+kGetLinVel)(body));
            alignas(16) float ang[4]{};
            Copy3(f.physAng,reinterpret_cast<GetVecOutFn>(image+kGetAngVel)(body,ang));
            Copy3(f.outLin,v);
            t.next=(t.next+1)%kFrames;
            if(last.t && f.physLin[1]>kLaunchSpeed && f.physLin[1]-last.outLin[1]>kLaunchJump
               && f.t-t.dumped>2000) {
                t.dumped=f.t;
                DumpTrack(t);
                DumpNeighbors(body,f.pos);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}

std::uintptr_t FinalAngProbe(void* body,const float* w) noexcept {
    __try {
        if(body && w)for(Track& t:tracks)
            if(t.body==body)Copy3(t.f[(t.next+kFrames-1)%kFrames].outAng,w);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return reinterpret_cast<SetVecFn>(image+kSetAngVel)(body,w);
}

// Car/tank frame (0x674663): when the chassis has come to rest the game calls setBodyQuality (hknp world
// iface slot 41, 0xE51280: iface, body id, quality, extra body flags, skip-cache-rebuild) with quality 1
// at 0x6746A1 -- `mov r8d,1` at 0x674682 -- and no vehicle code ever sets it back. Library entry 1
// (0xE13C05, flags 0x25808) is the cheap debris preset: no welding, and none of the vehicle's contact
// handling. So every chassis that has once stood still drives on as debris, and the launch probe saw
// what that does on uneven ground: one step ends 0.4 m inside the terrain and the push-out throws it up.
// Two parts, both VEHICLE (9): the rest path asks for it instead of 1, and the moving path (the final
// setLinVel at 0x6746B5, right before the step) restores it on a chassis still holding another quality
// -- one spawned from model data (CarBase/TankBase, car_base_body_name) that never rested, or one a
// previous plugin version left as debris. setBodyQuality returns early when the value is unchanged.
constexpr std::size_t kRestQualityCheckAt=0x67467B;
constexpr unsigned char kRestQualityCode[]={0x48,0x8B,0x97,0x98,0x16,0x00,0x00,   // mov rdx,[rdi+0x1698]
                                            0x41,0xB8,0x01,0x00,0x00,0x00};       // mov r8d,1
constexpr std::size_t kRestQualityImmediate=0x674684;
constexpr std::size_t kSetQualitySlot=41,kSetBodyQuality=0xE51280;
constexpr std::size_t kWrapperId=0xF0,kWrapperWorld=0x100,kWorldOf=0x58,kWorldIface=0x18,kIfaceBodies=0x20;
constexpr std::size_t kBodyQuality=0x89;

using SetQualityFn=void(*)(void*,std::uint32_t,std::uint8_t,std::uint32_t,std::uint32_t);
bool keepChassisQuality;
bool probeChassisVelocity;
std::atomic<int> qualityLogs{0};

void KeepChassisQuality(unsigned char* wrapper) noexcept {
    __try {
        if(!wrapper)return;
        const auto* worldWrapper=At<unsigned char*>(wrapper,kWrapperWorld);
        if(!worldWrapper)return;
        auto* world=At<unsigned char*>(worldWrapper,kWorldOf);
        if(!world)return;
        unsigned char* const iface=world+kWorldIface;
        const auto vtable=At<void**>(iface,0);
        if(!vtable || vtable[kSetQualitySlot]!=image+kSetBodyQuality)return;
        const std::uint32_t id=At<std::uint32_t>(wrapper,kWrapperId);
        const auto* body=At<unsigned char*>(iface,kIfaceBodies)+std::size_t{id&0xFFFFFF}*kBodyStride;
        if(At<std::uint32_t>(body,0x50)!=id)return;
        if((At<std::uint32_t>(body,0x54)&kBodyFlagsMotionMask)!=kBodyFlagDynamic)return;
        const std::uint8_t quality=At<std::uint8_t>(body,kBodyQuality);
        if(quality==kQualityVehicle)return;
        if(qualityLogs.fetch_add(1,std::memory_order_relaxed)<32)
            Log("PHYSICS chassis body %08X quality %u -> %u",id,quality,kQualityVehicle);
        reinterpret_cast<SetQualityFn>(image+kSetBodyQuality)(iface,id,kQualityVehicle,0,0);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}

// At the launch frame: every body of the chassis' world within kNeighborRadius of it that is not STATIC
// (a corpse ragdoll, building debris, another vehicle), with flags, quality and inverse mass (motion
// +0x26, half). Bodies hkArray at iface+0x20/+0x28, motions at iface+0x160 (stride 0x80), as 0xE51280.
constexpr float kNeighborRadius=12.0f;

float Half(std::uint16_t h) noexcept {
    const std::uint32_t sign=(h&0x8000u)<<16,exp=(h>>10)&0x1F,man=h&0x3FF;
    if(!exp)return 0.0f;
    const std::uint32_t bits=sign|((exp+112)<<23)|(man<<13);
    float f;
    std::memcpy(&f,&bits,4);
    return f;
}

void DumpNeighbors(void* wrapper,const float* at) noexcept {
    __try {
        const auto* worldWrapper=At<unsigned char*>(wrapper,kWrapperWorld);
        const auto* world=worldWrapper ? At<unsigned char*>(worldWrapper,kWorldOf) : nullptr;
        if(!world)return;
        const unsigned char* const iface=world+kWorldIface;
        const auto* bodies=At<const unsigned char*>(iface,kIfaceBodies);
        const std::int32_t count=At<std::int32_t>(iface,kIfaceBodies+8);
        const auto* motions=At<const unsigned char*>(iface,0x160);
        const std::uint32_t self=At<std::uint32_t>(static_cast<unsigned char*>(wrapper),kWrapperId);
        int statics=0;
        for(std::int32_t i=0;i<count && i<0x40000;++i) {
            const unsigned char* b=bodies+std::size_t(i)*kBodyStride;
            const std::uint32_t id=At<std::uint32_t>(b,0x50);
            if((id&0xFFFFFF)!=std::uint32_t(i))continue;
            const float* p=reinterpret_cast<const float*>(b+0x30);
            const float dx=p[0]-at[0],dy=p[1]-at[1],dz=p[2]-at[2];
            if(dx*dx+dy*dy+dz*dz>kNeighborRadius*kNeighborRadius)continue;
            const std::uint32_t flags=At<std::uint32_t>(b,0x54);
            if(flags&1){++statics;continue;}
            const std::uint32_t motion=At<std::uint32_t>(b,0x80);
            const float invMass=motions && motion ? Half(At<std::uint16_t>(motions+std::size_t(motion)*0x80,0x26)) : 0.0f;
            Log("VELPROBE2 near %08X%s flags=%08X q=%u motion=%u invMass=%.5f d=(%.2f %.2f %.2f)",id,
                id==self ? " (self)" : "",flags,At<std::uint8_t>(b,kBodyQuality),motion,invMass,dx,dy,dz);
        }
        Log("VELPROBE2 near: %d static bodies within %.0f m",statics,kNeighborRadius);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}

std::uintptr_t ChassisSetLinVel(void* body,const float* v) noexcept {
    if(keepChassisQuality)KeepChassisQuality(static_cast<unsigned char*>(body));
    if(probeChassisVelocity)RecordLin(body,v);
    return reinterpret_cast<SetVecFn>(image+kSetLinVel)(body,v);
}

bool RedirectChassisLinVel() noexcept {
    static bool redirected=false;
    bool changed=false;
    if(!redirected)redirected=RedirectCall(image+kFinalLinSite,image+kSetLinVel,
                                           reinterpret_cast<void*>(&ChassisSetLinVel),changed);
    return redirected;
}

bool KeepVehicleQuality() noexcept {
    if(!RedirectChassisLinVel())return false;
    keepChassisQuality=true;
    PatchCode(kRestQualityCheckAt,kRestQualityCode,sizeof(kRestQualityCode),
              kRestQualityImmediate,&kQualityVehicle,1);
    return true;
}

int ProbeVehicleVelocity() noexcept {
    bool changed=false;
    int done=0;
    if(RedirectChassisLinVel()) {
        probeChassisVelocity=true;
        ++done;
    }
    if(RedirectCall(image+kFinalAngSite,image+kSetAngVel,reinterpret_cast<void*>(&FinalAngProbe),changed))++done;
    return done;
}
}  // namespace

bool InstallPhysics() noexcept {
    const bool welded=cfg.vehicleWelding && WeldVehicleChassis();
    const bool chassis=cfg.vehicleWelding && KeepVehicleQuality();
    const bool capped=cfg.giantContactCap && CapGiantContact();
    if(cfg.debug)Log("PHYSICS velocity probes=%d",ProbeVehicleVelocity());
    Log("PHYSICS vehicleWelding heli=%d chassis=%d giantContactCap=%d (config %d/%d)",welded,chassis,capped,
        cfg.vehicleWelding,cfg.giantContactCap);
    return welded || chassis || capped;
}
}  // namespace crew
