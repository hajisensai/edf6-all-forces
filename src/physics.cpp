// Stock EDF6 physics defects the plugin corrects at load (docs/re-notes.md, "vanilla collision").
// EDF5 ran Havok hkp: vehicles were hkpVehicleInstance bodies and the terrain was welded, and a character
// pushed by a dynamic body was limited to maxForce. EDF6 moved to hknp with game-side wheels; the vehicle
// chassis lost the motion welding and the character's vertical contacts lost the force limit on the way.
#include "crew.h"
#include "memory.h"

#include <atomic>

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

// Diagnostic (debug only): every game write of a vehicle chassis velocity goes through setLinVel
// (0x11B18F0). Three call sites in the CarBase/TankBase frame: gravity pre-step (0x673C73), the
// ground-normal projection (0x678137) and the final write after the slots (0x6746B5). Logging what
// the physics step left in the body (getLinVel 0x11B1300) next to what the game writes tells whether
// an upward launch is produced by the hknp step (contacts) or by game code.
constexpr std::size_t kSetLinVel=0x11B18F0;
constexpr std::size_t kGetLinVel=0x11B1300;
constexpr std::size_t kVelocitySites[]={0x673C73,0x678137,0x6746B5};
constexpr float kLaunchSpeed=3.0f;

using SetLinVelFn=std::uintptr_t(*)(void*,const float*);
using GetLinVelFn=const float*(*)(void*);

template<int Site>
std::uintptr_t SetLinVelProbe(void* body,const float* v) noexcept {
    const auto get=reinterpret_cast<GetLinVelFn>(image+kGetLinVel);
    const float* phys=body ? get(body) : nullptr;
    if(phys && v && (phys[1]>kLaunchSpeed || v[1]>kLaunchSpeed)) {
        Log("VELPROBE site=%#zx t=%llu w=%p phys=(%.2f %.2f %.2f) out=(%.2f %.2f %.2f)",
            kVelocitySites[Site],static_cast<unsigned long long>(GameMs()),body,
            phys[0],phys[1],phys[2],v[0],v[1],v[2]);
    }
    return reinterpret_cast<SetLinVelFn>(image+kSetLinVel)(body,v);
}

int ProbeVehicleVelocity() noexcept {
    void* const probes[]={reinterpret_cast<void*>(&SetLinVelProbe<0>),
                          reinterpret_cast<void*>(&SetLinVelProbe<1>),
                          reinterpret_cast<void*>(&SetLinVelProbe<2>)};
    int done=0;
    for(std::size_t i=0;i<3;++i) {
        bool changed=false;
        if(RedirectCall(image+kVelocitySites[i],image+kSetLinVel,probes[i],changed))++done;
    }
    return done;
}
}  // namespace

bool InstallPhysics() noexcept {
    const bool welded=cfg.vehicleWelding && WeldVehicleChassis();
    const bool capped=cfg.giantContactCap && CapGiantContact();
    if(cfg.debug)Log("PHYSICS velocity probes=%d",ProbeVehicleVelocity());
    Log("PHYSICS vehicleWelding=%d giantContactCap=%d (config %d/%d)",welded,capped,
        cfg.vehicleWelding,cfg.giantContactCap);
    return welded || capped;
}
}  // namespace crew
