// See enemy_control_native.h. The native player modes drive each class's own
// locomotion, weapons, animation events and per-variant equipment gates.
#include "enemy_control_native.h"
#include "enemy_control_identity.h"
#include "crew.h"
#include "memory.h"
#include <atomic>
#include <cmath>
#include <cstring>

namespace crew::enemy_native {
namespace {
constexpr std::size_t kPad=0x340,kPlayer=0x354,kSelfControl=0x30,kDestroyed=0x2E8;
constexpr NativeProfile kProfiles[]={
#include "enemy_control_native_profiles.inc"
};
struct AiExtension {
    std::uint32_t vtableRva=0,original=0;
    OwnsAiFn owns=nullptr;
    InputFn controlled=nullptr;
    SkillSeedFn onSeed=nullptr;
};
constexpr unsigned kAiExtensions=16;
AiExtension extensions[kAiExtensions]{};
SRWLOCK extensionLock=SRWLOCK_INIT;
unsigned extensionCount=0;
bool KnownAiExtension(const NativeProfile& profile,const void* current,AiExtension* found=nullptr) noexcept {
    AiExtension candidate{};
    AcquireSRWLockShared(&extensionLock);
    for(unsigned i=0;i<extensionCount;++i)
        if(extensions[i].vtableRva==profile.vtableRva && extensions[i].original==profile.aiRva) {
            candidate=extensions[i];break;
        }
    ReleaseSRWLockShared(&extensionLock);
    if(!candidate.owns || !candidate.controlled || !candidate.owns(current))return false;
    if(found)*found=candidate;
    return true;
}
struct Fingerprint { std::uint32_t rva;unsigned char bytes[16]; };
constexpr Fingerprint kFingerprints[]={
#include "enemy_control_native_signatures.inc"
};
bool NativeReady() noexcept {
    static std::atomic<const unsigned char*> readyImage{nullptr};
    if(!image)return false;
    if(readyImage.load(std::memory_order_acquire)==image)return true;
    for(const auto& p:kFingerprints)
        if(!Readable(image+p.rva,sizeof(p.bytes)) || std::memcmp(image+p.rva,p.bytes,sizeof(p.bytes))!=0)return false;
    readyImage.store(image,std::memory_order_release);
    return true;
}
float Axis(float value) noexcept { return !std::isfinite(value) ? 0.0f : value<-1.0f ? -1.0f : value>1.0f ? 1.0f : value; }
void FloatAt(unsigned char* pad,std::size_t at,float value) noexcept { std::memcpy(pad+at,&value,sizeof(value)); }
enum class Life { gone,live,inactive,unknown };
Life Lifetime(const NativeLease& lease) noexcept {
    if(!lease.object || !lease.profile || !lease.control)return Life::gone;
    const auto identity=enemy_identity::Read(lease.object,lease.control,kPlayer+1);
    if(identity==enemy_identity::State::unknown)return Life::unknown;
    if(identity==enemy_identity::State::gone)return Life::gone;
    if((lease.object[0x18]&4) || lease.object[kDestroyed])return Life::inactive;
    return Life::live;
}
bool Existing(const NativeLease& lease) noexcept { return Lifetime(lease)==Life::live; }
bool ReleaseController(NativeLease& lease) noexcept {
    if(!lease.borrowed)return true;
    const Life state=Lifetime(lease);
    if(state==Life::unknown)return false;
    if(state==Life::gone){lease.borrowed=false;return true;}
    const void* current=At<const void*>(lease.object,kPad);
    if(current!=lease.pad && !(lease.pointerRestored && current==lease.savedPad)) {
        lease.borrowed=false;return true; // a native controller handover
    }
    if(!Readable(lease.object+kPad,sizeof(void*),true) || !Readable(lease.object+kPlayer,1,true))return false;
    Put<const void*>(lease.object,kPad,lease.savedPad);lease.pointerRestored=true;
    if(lease.object[kPlayer]==1)lease.object[kPlayer]=lease.savedPlayer;
    const bool restored=At<const void*>(lease.object,kPad)==lease.savedPad;
    if(restored){lease.borrowed=false;lease.pointerRestored=false;}
    return restored;
}
bool Identity(const NativeLease& lease) noexcept {
    return lease.object && lease.profile && lease.control && Readable(lease.object,kPlayer+1,true) &&
        At<const void*>(lease.object,kSelfControl)==lease.control && Readable(lease.control,12) &&
        At<LONG>(lease.control,8)>0 && !(lease.object[0x18]&4) && !lease.object[kDestroyed];
}
}
bool RegisterControlledAiExtension(std::uint32_t vtableRva,std::uint32_t expectedOriginalAiRva,
                                   OwnsAiFn owns,InputFn controlled,SkillSeedFn onSeed) noexcept {
    if(!owns || !controlled)return false;
    bool known=false;
    for(const auto& p:kProfiles)
        if(p.vtableRva==vtableRva && p.aiRva==expectedOriginalAiRva){known=true;break;}
    if(!known)return false;
    AcquireSRWLockExclusive(&extensionLock);
    for(unsigned i=0;i<extensionCount;++i)if(extensions[i].vtableRva==vtableRva) {
        const bool same=extensions[i].original==expectedOriginalAiRva && extensions[i].owns==owns &&
                        extensions[i].controlled==controlled && extensions[i].onSeed==onSeed;
        ReleaseSRWLockExclusive(&extensionLock);return same;
    }
    if(extensionCount==kAiExtensions){ReleaseSRWLockExclusive(&extensionLock);return false;}
    extensions[extensionCount++]={vtableRva,expectedOriginalAiRva,owns,controlled,onSeed};
    ReleaseSRWLockExclusive(&extensionLock);return true;
}
bool RunControlledAiExtension(const NativeLease& lease,void* dt,std::uint64_t* result) noexcept {
    if(result)*result=0;
    __try {
        if(!Existing(lease) || !NativeReady())return false;
        const void* const* nativeTable=reinterpret_cast<const void* const*>(image+lease.profile->vtableRva);
        if(!Readable(nativeTable,(kAiSlot+1)*sizeof(void*)))return false;
        AiExtension extension{};
        if(!KnownAiExtension(*lease.profile,nativeTable[kAiSlot],&extension))return false;
        const auto value=extension.controlled(lease.object,dt);
        if(result)*result=value;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
const NativeProfile* Profiles(std::size_t* count) noexcept {
    if(count)*count=sizeof(kProfiles)/sizeof(kProfiles[0]);
    return kProfiles;
}
const NativeProfile* Profile(const void* object) noexcept {
    __try {
        if(!NativeReady() || !Readable(object,kPlayer+1))return nullptr;
        const void* vt=At<const void*>(object,0);
        for(const auto& p:kProfiles) {
            if(vt!=image+p.vtableRva)continue;
            // The engine's COL occupies the cell before a primary vtable. Its
            // signature, primary offset, self-RVA and descriptor name must agree.
            if(!Readable(image+p.vtableRva-8,(static_cast<std::size_t>(p.slots)+1)*8))return nullptr;
            const auto col=At<const unsigned char*>(static_cast<const unsigned char*>(vt)-8,0);
            if(!Readable(col,24) || At<std::uint32_t>(col,0)!=1 || At<std::uint32_t>(col,4)!=0 ||
                image+At<std::uint32_t>(col,20)!=col)return nullptr;
            const auto td=image+At<std::uint32_t>(col,12);
            const std::size_t length=std::strlen(p.name);
            if(!Readable(td+16,length+7) || std::memcmp(td+16,".?AV",4)!=0 ||
                std::memcmp(td+20,p.name,length)!=0 || std::memcmp(td+20+length,"@@",3)!=0)return nullptr;
            const auto entries=static_cast<const void* const*>(vt);
            if(entries[p.inputSlot]!=image+p.inputRva ||
                (entries[kAiSlot]!=image+p.aiRva && !KnownAiExtension(p,entries[kAiSlot])))return nullptr;
            return &p;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}
bool Capture(void* object,NativeLease* out) noexcept {
    if(!out)return false;
    *out={};
    const auto profile=Profile(object);
    if(!profile)return false;
    NativeLease lease{static_cast<unsigned char*>(object),profile,At<const void*>(object,kSelfControl)};
    if(!Identity(lease))return false;
    *out=lease;
    return true;
}
bool Neutralize(NativeLease& lease) noexcept {
    __try {
        if(!Identity(lease))return false;
        unsigned char* const object=lease.object;
        // The exact command destinations written by the native player branches,
        // not animation/physics state. Homogeneous vector W and existing motion
        // are preserved; the next native AI/frame consumes released commands.
        const auto zero=[object](std::size_t offset,std::size_t size) noexcept {
            if(!Readable(object+offset,size,true))return false;
            std::memset(object+offset,0,size);return true;
        };
        switch(lease.profile->inputRva) {
            case 0x30E300:
                return zero(0xCB0,12) && zero(0xCC0,5);
            case 0x4F9540:case 0x4F74D0:
                return zero(0xB40,12) && zero(0xB50,12);
            case 0x3EE300:
                return zero(0x10E0,12) && zero(0x10F0,12) && zero(0x1100,1);
            case 0x4A8FD0:case 0x42D560:
                return zero(0xFC0,12) && zero(0xFD0,4) && zero(0xFE0,12) && zero(0xFF0,4);
            case 0x343DD0:
                return zero(0x1290,12) && zero(0x12A0,8) && zero(0x12C0,4);
            case 0x31ED90:case 0x4E0010:
                return zero(0xF10,12) && zero(0xF20,6);
            case 0x368310:
                return zero(0x12E0,12) && zero(0x12F0,12) && zero(0x1300,3);
            case 0x3A4AF0:
                if(!zero(0x1BC0,12) || !zero(0x1BD0,5) || !Readable(object+0x1BD8,4,true))return false;
                Put<std::int32_t>(object,0x1BD8,-1);return true;
            case 0x3B3ED0:
                return zero(0x13C0,12) && zero(0x13D0,12) && zero(0x13E0,16);
            case 0x3C0850:
                if(!zero(0xFC0,12) || !zero(0xFD0,12) || !Readable(object+0xFE0,4,true))return false;
                Put<std::uint32_t>(object,0xFE0,At<std::uint32_t>(object,0xFE0)&~15u);return true;
            case 0x3D60F0:
                if(!zero(0x1160,12) || !zero(0x1170,12) || !Readable(object+0x1180,4,true))return false;
                Put<std::uint32_t>(object,0x1180,At<std::uint32_t>(object,0x1180)&~7u);return true;
            case 0x3E2140:
                return zero(0x1230,12) && zero(0x1240,12) && zero(0x1250,1);
            case 0x4ACD70:case 0x4BC8B0:
                if(!zero(0x880,12) || !zero(0x894,4) || !zero(0x8A0,1) || !Readable(object+0x8A4,8,true))return false;
                Put<std::uint32_t>(object,0x8A4,0xFFFFFFFFu);Put<std::uint32_t>(object,0x8A8,0x8FFFFFFFu);return true;
            case 0x444E90:case 0x461590:case 0x462E30:case 0x4A6780:
                return zero(0x11D0,12) && zero(0x11E0,8) && zero(0x11F0,6);
            case 0x52EF70:
                return zero(0x1460,12) && zero(0x1470,5);
            case 0x5371C0:
                return zero(0x600,12) && zero(0x610,5);
            case 0x4F3810:
                return true; // button 0 queues one native landing/door transition; no held input is retained
            default:return false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Restore(NativeLease* lease) noexcept {
    if(!lease)return true;
    __try {
        if(lease->restoreGate && !lease->restoreGate(*lease))return false;
        const Life state=Lifetime(*lease);
        if(state==Life::unknown)return false;
        if(state==Life::gone){*lease={};return true;}
        if(!ReleaseController(*lease))return false;
        if(state==Life::live && !Neutralize(*lease))return false;
        *lease={};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SetSeed(const NativeLease& lease,std::uint64_t seed) noexcept {
    __try {
        if(!Existing(lease) || !Readable(lease.object+0x3E8,8,true) ||
            !Readable(lease.object+0x490,8,true))return false;
        // GOB action RNG and SGO range/distribution RNG are independent native
        // LCG cursors. Seed both before the action edge, not each held frame.
        Put<std::uint64_t>(lease.object,0x3E8,seed);
        Put<std::uint64_t>(lease.object,0x490,seed);
        const auto table=reinterpret_cast<const void* const*>(image+lease.profile->vtableRva);
        AiExtension extension{};
        if(Readable(table,(kAiSlot+1)*sizeof(void*)) &&
            KnownAiExtension(*lease.profile,table[kAiSlot],&extension) && extension.onSeed)
            return extension.onSeed(lease.object,seed);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void BuildPad(const Input& input,unsigned char (&pad)[kPadBytes]) noexcept {
    std::memset(pad,0,sizeof(pad));
    FloatAt(pad,0x0C,Axis(input.right));FloatAt(pad,0x24,Axis(input.forward));
    FloatAt(pad,0x3C,Axis(input.yaw));FloatAt(pad,0x54,Axis(input.pitch));
    const float vertical=Axis(input.vertical);
    FloatAt(pad,0xCC,vertical>0.0f ? vertical : 0.0f);
    FloatAt(pad,0xE4,vertical<0.0f ? -vertical : 0.0f);
    for(unsigned i=0;i<kButtonCount;++i) {
        const std::uint32_t bit=1u<<i;
        const auto at=kPadButtonBase+static_cast<std::size_t>(i)*kPadButtonStride;
        pad[at]=(input.held&bit)!=0 ? 1u : 0u;
        pad[at+1]=(input.pressed&bit)!=0 ? 1u : 0u;
    }
}
bool RunInput(NativeLease& lease,const Input& input,InputFn original,void* dt,std::uint64_t* result) noexcept {
    if(result)*result=0;
    bool restored=false;
    __try {
        if(!Identity(lease) || !NativeReady() || !original ||
            reinterpret_cast<const void*>(original)!=image+lease.profile->inputRva || !ReleaseController(lease))return false;
        Input mapped=input;
        if(lease.profile->inputRva==0x3E2140) {
            if(mapped.vertical>0.0f)mapped.held|=1u<<6;
            if(mapped.vertical<0.0f)mapped.held|=1u<<7;
        }
        BuildPad(mapped,lease.pad);
        if((input.pressed || (input.held&~lease.previousHeld)) && !SetSeed(lease,input.seed))return false;
        unsigned char* const object=lease.object;
        lease.savedPad=At<const void*>(object,kPad);lease.savedPlayer=object[kPlayer];
        lease.borrowed=true;lease.pointerRestored=false;
        __try {
            Put<const void*>(object,kPad,lease.pad);object[kPlayer]=1;
            const std::uint64_t nativeResult=original(object,dt);
            if(result)*result=nativeResult;
            lease.previousHeld=input.held;
        } __finally {
            // Stable lease storage survives a page-protection/controller change.
            // Failure is observable and keeps the baseline for a later release.
            restored=ReleaseController(lease);
        }
        return restored;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
} // namespace crew::enemy_native
