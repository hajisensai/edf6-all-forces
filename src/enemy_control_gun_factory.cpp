// A single process-local factory relay. Exact family adapters retain ownership
// of native state/counters and accepted wire events; this layer never coalesces
// shots, guesses a family from a team, or sends function addresses.
#include "enemy_control_gun_factory.h"
#include "crew.h"
#include "memory.h"
#include <cstring>
namespace crew::enemy_native {
#ifdef ENEMY_CONTROL_GUN_FACTORY_TEST
void* GunFactoryTestFunction(unsigned) noexcept;
#endif
namespace native_factory {
constexpr unsigned kProviders=16,kSites=64,kGunSite=0x6CFE2A,kGun=0x6D05C0,kUnit=0x1194280,kBeamSite=0x6D31BA,kBeamContinue=0x6D36F6,kBeamStepSite=0x233C92,kBeamStep=0x234420;
struct Sig {unsigned rva;unsigned char bytes[16];};
constexpr Sig signatures[]={
    {0x234420,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x48,0x8D,0x6C,0x24,0xA9}},
    {0x6D05C0,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x55,0x57,0x41,0x54,0x41}},
    {0x1194280,{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x40,0x49,0x8B,0xF1,0x48,0x8B}},
};
struct Site {unsigned rva=0;bool installed=false;unsigned char relay[5]{};void* function=nullptr;};
NativeGunFactoryProvider guns[kProviders]{};unsigned gunCount=0;
NativeBeamStepProvider beamSteps[kProviders]{};unsigned beamStepCount=0;Site beamStepSite{kBeamStepSite};
NativeBeamRefreshProvider beams[kProviders]{};unsigned beamCount=0;bool beamInstalled=false;unsigned char beamBytes[11]{};
NativeUnitFactoryProvider units[kProviders]{};unsigned unitCount=0;Site sites[kSites]{};unsigned siteCount=0;
Site gunSite{kGunSite};unsigned char* installedImage=nullptr;
using Gun=void(__fastcall*)(void*,const float*,std::uint64_t*);
using Unit=void*(__fastcall*)(void*,const float*,void*,void*);
using Beam=void(__fastcall*)(void*);
using BeamStep=void(__fastcall*)(void*,void*);
template<class T>T Fn(unsigned rva) noexcept {
#ifdef ENEMY_CONTROL_GUN_FACTORY_TEST
    return reinterpret_cast<T>(GunFactoryTestFunction(rva));
#else
    return reinterpret_cast<T>(image+rva);
#endif
}
bool Codes() noexcept {
    if(!image || (installedImage && installedImage!=image))return false;
    for(const auto& s:signatures)if(!Readable(image+s.rva,16) || std::memcmp(image+s.rva,s.bytes,16))return false;
    return true;
}
bool Same(const NativeGunFactoryProvider& a,const NativeGunFactoryProvider& b) noexcept {return a.tag==b.tag && a.Owns==b.Owns && a.Invoke==b.Invoke && a.Fault==b.Fault;}
bool Same(const NativeUnitFactoryProvider& a,const NativeUnitFactoryProvider& b) noexcept {return a.tag==b.tag && a.Owns==b.Owns && a.Invoke==b.Invoke && a.Fault==b.Fault;}
bool Same(const NativeBeamRefreshProvider& a,const NativeBeamRefreshProvider& b) noexcept {return a.tag==b.tag && a.Owns==b.Owns && a.Invoke==b.Invoke && a.Fault==b.Fault;}
bool Same(const NativeBeamStepProvider& a,const NativeBeamStepProvider& b) noexcept {return a.tag==b.tag && a.Owns==b.Owns && a.Invoke==b.Invoke && a.Fault==b.Fault;}
bool Relay(const Site& s) noexcept {
    if(!s.installed || image!=installedImage || !Readable(image+s.rva,5) || std::memcmp(image+s.rva,s.relay,5))return false;
    std::int32_t relative=0;std::memcpy(&relative,image+s.rva+1,4);const auto* thunk=image+s.rva+5+relative;
    if(!Readable(thunk,12) || thunk[0]!=0x48 || thunk[1]!=0xB8 || thunk[10]!=0xFF || thunk[11]!=0xE0)return false;
    void* function=nullptr;std::memcpy(&function,thunk+2,sizeof(function));return function==s.function;
}
bool Call(unsigned site,unsigned target) noexcept {
    if(!image || site<0x1000 || site>=0x22CE000-5 || !Readable(image+site,5))return false;
    const auto op=image[site];if(op!=0xE8 && op!=0xE9)return false;std::int32_t displacement=0;std::memcpy(&displacement,image+site+1,4);
    return static_cast<std::int64_t>(site)+5+displacement==target;
}
void FaultGun(const void* holder) noexcept {
    for(unsigned i=0;i<gunCount;++i)if(guns[i].Fault)guns[i].Fault(holder);
}
void FaultUnit(const void* factory,const void* descriptor) noexcept {
    for(unsigned i=0;i<unitCount;++i)if(units[i].Fault)units[i].Fault(factory,descriptor);
}
void __fastcall GunRelay(void* holder,const float* matrix,std::uint64_t* seed) {
    const NativeGunFactoryProvider* selected=nullptr;unsigned matches=0;
    __try {for(unsigned i=0;i<gunCount;++i)if(guns[i].Owns(holder)){selected=guns+i;++matches;}}
    __except(EXCEPTION_EXECUTE_HANDLER){FaultGun(holder);return;}
    if(matches>1){FaultGun(holder);return;}
    if(!selected){Fn<Gun>(kGun)(holder,matrix,seed);return;} // preserve unowned native exceptions too
    __try {selected->Invoke(holder,matrix,seed);}
    __except(EXCEPTION_EXECUTE_HANDLER){FaultGun(holder);}
}
void* __fastcall UnitRelay(void* manager,const float* matrix,void* factory,void* descriptor) {
    const NativeUnitFactoryProvider* selected=nullptr;unsigned matches=0;
    __try {for(unsigned i=0;i<unitCount;++i)if(units[i].Owns(factory,descriptor)){selected=units+i;++matches;}}
    __except(EXCEPTION_EXECUTE_HANDLER){FaultUnit(factory,descriptor);return nullptr;}
    if(matches>1){FaultUnit(factory,descriptor);return nullptr;}
    if(!selected)return Fn<Unit>(kUnit)(manager,matrix,factory,descriptor);
    __try {return selected->Invoke(manager,matrix,factory,descriptor);}
    __except(EXCEPTION_EXECUTE_HANDLER){FaultUnit(factory,descriptor);return nullptr;}
}
void FaultBeam(const void* bullet) noexcept {for(unsigned i=0;i<beamCount;++i)beams[i].Fault(bullet);}
void __fastcall BeamRelay(void* bullet) {
    const NativeBeamRefreshProvider* selected=nullptr;unsigned matches=0;
    __try {for(unsigned i=0;i<beamCount;++i)if(beams[i].Owns(bullet)){selected=beams+i;++matches;}}
    __except(EXCEPTION_EXECUTE_HANDLER){FaultBeam(bullet);return;}
    if(matches>1){FaultBeam(bullet);return;}
    if(!selected){InvokeOriginalNativeBeamRefresh(bullet);return;}
    __try {selected->Invoke(bullet);}
    __except(EXCEPTION_EXECUTE_HANDLER){FaultBeam(bullet);}
}
void FaultBeamStep(const void* bullet) noexcept {for(unsigned i=0;i<beamStepCount;++i)beamSteps[i].Fault(bullet);}
void __fastcall BeamStepRelay(void* state,void* dt) {
    auto* bullet=static_cast<unsigned char*>(state)-0x140;const NativeBeamStepProvider* selected=nullptr;unsigned matches=0;
    __try {for(unsigned i=0;i<beamStepCount;++i)if(beamSteps[i].Owns(bullet)){selected=beamSteps+i;++matches;}}
    __except(EXCEPTION_EXECUTE_HANDLER){FaultBeamStep(bullet);return;}
    if(matches>1){FaultBeamStep(bullet);return;}
    if(!selected){Fn<BeamStep>(kBeamStep)(state,dt);return;}
    __try {selected->Invoke(bullet,dt);}
    __except(EXCEPTION_EXECUTE_HANDLER){FaultBeamStep(bullet);}
}
bool BeamReady() noexcept {
    if(!beamInstalled || image!=installedImage || !Readable(image+kBeamSite,11) || std::memcmp(image+kBeamSite,beamBytes,11))return false;
    std::int32_t relative=0;std::memcpy(&relative,image+kBeamSite+1,4);const auto* thunk=image+kBeamSite+5+relative;
    if(!Readable(thunk,12) || thunk[0]!=0x48 || thunk[1]!=0xB8 || thunk[10]!=0xFF || thunk[11]!=0xE0)return false;
    void* function=nullptr;std::memcpy(&function,thunk+2,8);return function==reinterpret_cast<void*>(&BeamRelay);
}
bool Install(Site& site,unsigned target,void* relay) noexcept {
    if(site.installed)return Relay(site);
    if(!Call(site.rva,target))return false;
#ifdef ENEMY_CONTROL_GUN_FACTORY_TEST
    auto* thunk=image+0x100000+(site.rva%0x10000)*16;const unsigned char code[]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    std::memcpy(thunk,code,12);std::memcpy(thunk+2,&relay,sizeof(relay));const auto displacement=static_cast<std::int32_t>(thunk-(image+site.rva+5));std::memcpy(image+site.rva+1,&displacement,4);
#else
    bool changed=false;if(!RedirectCall(image+site.rva,image+target,relay,changed) || !changed)return false;
#endif
    std::memcpy(site.relay,image+site.rva,5);site.function=relay;site.installed=true;installedImage=image;return true;
}
} // namespace native_factory
bool RegisterNativeGunFactoryProvider(const NativeGunFactoryProvider& p) noexcept {
    using namespace native_factory;if(!p.tag || !p.Owns || !p.Invoke || !p.Fault)return false;
    for(unsigned i=0;i<gunCount;++i)if(guns[i].tag==p.tag)return Same(guns[i],p);
    if(gunCount==kProviders)return false;guns[gunCount++]=p;return true;
}
bool RegisterNativeUnitFactoryProvider(const NativeUnitFactoryProvider& p) noexcept {
    using namespace native_factory;if(!p.tag || !p.Owns || !p.Invoke || !p.Fault)return false;
    for(unsigned i=0;i<unitCount;++i)if(units[i].tag==p.tag)return Same(units[i],p);
    if(unitCount==kProviders)return false;units[unitCount++]=p;return true;
}
bool RegisterNativeUnitFactoryCallsite(std::uint32_t rva) noexcept {
    using namespace native_factory;for(unsigned i=0;i<siteCount;++i)if(sites[i].rva==rva)return sites[i].installed ? Relay(sites[i]) : Call(rva,kUnit);
    if(siteCount==kSites || !Call(rva,kUnit))return false;sites[siteCount++].rva=rva;return true;
}
bool InstallNativeGunFactoryDispatcher() noexcept {
    using namespace native_factory;__try {return gunCount && Codes() && Install(gunSite,kGun,reinterpret_cast<void*>(&GunRelay));}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool InstallNativeUnitFactoryDispatcher() noexcept {
    using namespace native_factory;__try {
        if(!unitCount || !siteCount || !Codes())return false;
        for(unsigned i=0;i<siteCount;++i)if(!Install(sites[i],kUnit,reinterpret_cast<void*>(&UnitRelay)))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool NativeGunFactoryProviderReady(const NativeGunFactoryProvider& p) noexcept {
    using namespace native_factory;if(!Codes() || !Relay(gunSite))return false;
    for(unsigned i=0;i<gunCount;++i)if(Same(guns[i],p))return true;return false;
}
bool NativeUnitFactoryProviderReady(const NativeUnitFactoryProvider& p) noexcept {
    using namespace native_factory;if(!Codes() || !siteCount)return false;
    for(unsigned i=0;i<siteCount;++i)if(!Relay(sites[i]))return false;
    for(unsigned i=0;i<unitCount;++i)if(Same(units[i],p))return true;return false;
}
bool NativeUnitFactoryCallsiteReady(std::uint32_t rva) noexcept {
    using namespace native_factory;if(!Codes())return false;
    for(unsigned i=0;i<siteCount;++i)if(sites[i].rva==rva)return Relay(sites[i]);return false;
}
bool RegisterNativeBeamRefreshProvider(const NativeBeamRefreshProvider& p) noexcept {
    using namespace native_factory;if(!p.tag || !p.Owns || !p.Invoke || !p.Fault)return false;
    for(unsigned i=0;i<beamCount;++i)if(beams[i].tag==p.tag)return Same(beams[i],p);
    if(beamCount==kProviders)return false;beams[beamCount++]=p;return true;
}
bool InstallNativeBeamRefreshDispatcher() noexcept {
    using namespace native_factory;
    __try {
        if(!beamCount || !Codes())return false;if(beamInstalled)return BeamReady();
        constexpr unsigned char expected[]={0x48,0x8B,0x01,0xFF,0x50,0x60,0xE9,0x31,0x05,0,0};
        if(!Readable(image+kBeamSite,11) || std::memcmp(image+kBeamSite,expected,11))return false;
#ifdef ENEMY_CONTROL_GUN_FACTORY_TEST
        auto* thunk=image+0x200000;const unsigned char code[]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};std::memcpy(thunk,code,12);
        void* function=reinterpret_cast<void*>(&BeamRelay);std::memcpy(thunk+2,&function,8);
#else
        auto* thunk=static_cast<unsigned char*>(AllocateNearThunk(image+kBeamSite,reinterpret_cast<void*>(&BeamRelay)));if(!thunk)return false;
#endif
        const auto distance=thunk-(image+kBeamSite+5);if(distance>0x7FFFFFF0 || distance<-0x7FFFFFF0)return false;
        unsigned char replacement[11]={0xE8,0,0,0,0,0xE9,0,0,0,0,0x90};const auto relay=static_cast<std::int32_t>(distance);
        const auto next=static_cast<std::int32_t>(kBeamContinue-(kBeamSite+10));std::memcpy(replacement+1,&relay,4);std::memcpy(replacement+6,&next,4);
#ifdef ENEMY_CONTROL_GUN_FACTORY_TEST
        std::memcpy(image+kBeamSite,replacement,11);
#else
        if(!edf::PatchCode(image+kBeamSite,expected,replacement,11))return false;
#endif
        std::memcpy(beamBytes,replacement,11);beamInstalled=true;installedImage=image;return BeamReady();
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool NativeBeamRefreshProviderReady(const NativeBeamRefreshProvider& p) noexcept {
    using namespace native_factory;if(!Codes() || !BeamReady())return false;
    for(unsigned i=0;i<beamCount;++i)if(Same(beams[i],p))return true;return false;
}
void InvokeOriginalNativeBeamRefresh(void* bullet) {
    const auto* table=At<void* const*>(bullet,0);reinterpret_cast<native_factory::Beam>(table[12])(bullet);
}
bool RegisterNativeBeamStepProvider(const NativeBeamStepProvider& p) noexcept {
    using namespace native_factory;if(!p.tag || !p.Owns || !p.Invoke || !p.Fault)return false;
    for(unsigned i=0;i<beamStepCount;++i)if(beamSteps[i].tag==p.tag)return Same(beamSteps[i],p);
    if(beamStepCount==kProviders)return false;beamSteps[beamStepCount++]=p;return true;
}
bool InstallNativeBeamStepDispatcher() noexcept {
    using namespace native_factory;__try {return beamStepCount && Codes() && Install(beamStepSite,kBeamStep,reinterpret_cast<void*>(&BeamStepRelay));}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool NativeBeamStepProviderReady(const NativeBeamStepProvider& p) noexcept {
    using namespace native_factory;if(!Codes() || !Relay(beamStepSite))return false;
    for(unsigned i=0;i<beamStepCount;++i)if(Same(beamSteps[i],p))return true;return false;
}
void InvokeOriginalNativeBeamStep(void* bullet,void* dt) {native_factory::Fn<native_factory::BeamStep>(native_factory::kBeamStep)(static_cast<unsigned char*>(bullet)+0x140,dt);}
void InvokeOriginalNativeGunFactory(void* holder,const float* matrix,std::uint64_t* seed) {native_factory::Fn<native_factory::Gun>(native_factory::kGun)(holder,matrix,seed);}
void* InvokeOriginalNativeUnitFactory(void* manager,const float* matrix,void* factory,void* descriptor) {return native_factory::Fn<native_factory::Unit>(native_factory::kUnit)(manager,matrix,factory,descriptor);}
void* DispatchNativeUnitFactory(void* manager,const float* matrix,void* factory,void* descriptor) {return native_factory::UnitRelay(manager,matrix,factory,descriptor);}
}
