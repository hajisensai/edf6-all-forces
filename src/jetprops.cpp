// Fixed-wing bodies get their own Havok motion properties (docs/jet-model-re.md, "Own motion properties").
// Every vehicle body shares the preset whose maxLinearSpeed (hknpMotionProperties+0x10) is 200 m/s: Havok
// clamps the body there whatever velocity the plugin sets, so the jets flew like fast helicopters. A jet's
// body gets a copy of its preset with the speed cap raised, added to the world's motion-properties library
// (never the shared entry edited in place: every heli and truck uses it).
#include "crew.h"
#include "memory.h"
#include <cstring>

namespace crew {
namespace {
constexpr unsigned kPropsAdd=0xE15190;        // (library, u16* outId, const props*): 0xFFFF in outId when full
constexpr unsigned kSetMotionProps=0xE50720;  // world interface slot 32: (iface, u32 bodyId, u16 propsId)
constexpr std::size_t kSetPropsSlot=32;
// Body wrapper: +0x60 its props copy, +0xF0 body id, +0xF6 props id, +0x100 world wrapper (+0x58 hknpWorld).
constexpr std::size_t kBodyProps=0x60,kBodyId=0xF0,kBodyPropsId=0xF6,kBodyWorld=0x100,kWorldOf=0x58;
// hknpWorld: +0x18 the write interface, +0x928 the motion-properties library (+0x40 entries, +0x48 count).
constexpr std::size_t kWorldIface=0x18,kWorldLibrary=0x928,kLibEntries=0x40,kLibCount=0x48,kPropsSize=0x70;
// hknpMotionProperties: +0 must be 0 for add to reuse an equal entry, +0x10 max linear speed, +0x54 and
// +0x58 the two terms Havok keeps as 5 / max and 0.005 / max.
constexpr std::size_t kMaxLinear=0x10,kInvMax=0x54,kInvMaxSmall=0x58;
constexpr float kJetMaxLinear=600.0f;   // m/s: past anything a jet is commanded (jet.cpp kBodyTop)

const unsigned char kPropsAddSig[]={0x40,0x53,0x55,0x41,0x54,0x41,0x55,0x48,0x83,0xEC,0x28,0x48,0x8B,0xD9,0x49,0x8B};
const unsigned char kSetPropsSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};

using AddFn=std::uint16_t*(__fastcall*)(void*,std::uint16_t*,const void*);
using SetPropsFn=void(__fastcall*)(void*,std::uint32_t,std::uint16_t);

bool ok=false;
bool failLogged=false;
// What a preset became, per library (a new mission makes a new world): the copy is added once.
struct Made { const void* library; std::uint16_t from,to; };
Made made[8]{};
unsigned madeNext=0;

bool IsJetCopy(const unsigned char* entries,std::uint16_t id) noexcept {
    return At<float>(entries+static_cast<std::size_t>(id)*kPropsSize,kMaxLinear)==kJetMaxLinear;
}

// The id the jet's copy of preset `from` has in `library`, adding it when there is none yet; 0xFFFF on failure.
std::uint16_t JetCopyOf(unsigned char* library,const unsigned char* entries,int count,std::uint16_t from) noexcept {
    for(const Made& m:made)
        if(m.library==library && m.from==from && m.to<count && IsJetCopy(entries,m.to))return m.to;
    alignas(16) unsigned char copy[kPropsSize];
    std::memcpy(copy,entries+static_cast<std::size_t>(from)*kPropsSize,kPropsSize);
    Put<std::uint32_t>(copy,0,0);
    Put<float>(copy,kMaxLinear,kJetMaxLinear);
    Put<float>(copy,kInvMax,5.0f/kJetMaxLinear);
    Put<float>(copy,kInvMaxSmall,0.005f/kJetMaxLinear);
    std::uint16_t to=0xFFFF;
    reinterpret_cast<AddFn>(image+kPropsAdd)(library,&to,copy);
    if(to==0xFFFF)return to;
    made[madeNext++%8]=Made{library,from,to};
    Log("JET motion props: preset %u -> %u (max %.0f m/s, was %.0f)",from,to,kJetMaxLinear,
        At<float>(entries+static_cast<std::size_t>(from)*kPropsSize,kMaxLinear));
    return to;
}
}  // namespace

bool InstallJetProps() noexcept {
    ok=Matches(kPropsAdd,kPropsAddSig,sizeof(kPropsAddSig)) && Matches(kSetMotionProps,kSetPropsSig,sizeof(kSetPropsSig));
    Log("HOOK jet motion props=%d%s",ok,ok ? "" : " (unexpected EDF.dll code: jets stay under 200 m/s)");
    return ok;
}

bool JetMotionProps(void* bodyPtr) noexcept {
    if(!ok || !bodyPtr)return false;
    __try {
        auto body=static_cast<unsigned char*>(bodyPtr);
        if(!Readable(body,kBodyWorld+8))return false;
        const auto wrapper=At<unsigned char*>(body,kBodyWorld);
        if(!wrapper || !Readable(wrapper+kWorldOf,8))return false;
        const auto world=At<unsigned char*>(wrapper,kWorldOf);
        if(!world || !Readable(world,kWorldLibrary+8))return false;
        unsigned char* const iface=world+kWorldIface;
        const auto vtable=At<void**>(iface,0);
        if(!vtable || !Readable(vtable+kSetPropsSlot,8) || vtable[kSetPropsSlot]!=image+kSetMotionProps)return false;
        const auto library=At<unsigned char*>(world,kWorldLibrary);
        if(!library || !Readable(library,kLibCount+4))return false;
        const int count=At<int>(library,kLibCount);
        const auto entries=At<unsigned char*>(library,kLibEntries);
        const auto id=At<std::uint16_t>(body,kBodyPropsId);
        if(!entries || id>=count || !Readable(entries,static_cast<std::size_t>(count)*kPropsSize))return false;
        if(IsJetCopy(entries,id))return true;
        const std::uint16_t to=JetCopyOf(library,entries,count,id);
        if(to==0xFFFF) {
            if(!failLogged)Log("JET motion props: the library is full, jets stay under %.0f m/s",At<float>(entries+id*kPropsSize,kMaxLinear));
            failLogged=true;
            return false;
        }
        reinterpret_cast<SetPropsFn>(image+kSetMotionProps)(iface,At<std::uint32_t>(body,kBodyId),to);
        // The wrapper's own copy too: should EDF ever commit it, the body keeps the raised cap.
        const auto now=At<unsigned char*>(library,kLibEntries);
        if(now && At<std::uint16_t>(body,kBodyPropsId)==id) {
            Put<std::uint16_t>(body,kBodyPropsId,to);
            std::memcpy(body+kBodyProps,now+static_cast<std::size_t>(to)*kPropsSize,kPropsSize);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
