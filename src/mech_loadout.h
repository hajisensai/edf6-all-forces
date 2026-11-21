// Custom mech loadouts (docs/feature-2026-11-21-mech-custom.md; the player, 2026-11-21: "机甲弄成自定义化的 四个武器槽位+
// 一个机体槽位+加一个核心"): the parts (src/mech_parts.inc, generated from pylib/mech_parts.py) and the rules. Pure, no game,
// no Windows. support_loadout.h parses SupportVehicle_OWN_MECH into a MechLoadout and packs it into the player vehicle's
// 64-bit variant (MechVariantBits); player_vehicle.cpp swaps a call of the base request for the generated mech.
//   SupportVehicle_OWN_MECH=<request>,<frame>,<right arm>,<left arm>,<right shoulder>,<left shoulder>,<core>
//   (- an empty weapon slot; the frame / core: the request's own)
// A weapon or core is never above the request's level (the lowest level of the stock requests carrying it).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cwchar>

namespace crew {
enum class MechMount : std::uint8_t { hand, arm, shoulder };
struct MechFrame { const wchar_t* token; const wchar_t* file; const wchar_t* label[4]; };
struct MechWeapon { const wchar_t* token; const wchar_t* label[4]; MechMount mount; bool right,left; int level; };
struct MechCore { const wchar_t* token; const wchar_t* label[4]; int level; };
// `level`: the float its container is configured with (WEAPONTABLE column 4); `cents` the same in hundredths. `frame`,
// `core`, `weapon`: indexes (-1 none).
struct MechRequest { const wchar_t* token; const wchar_t* label[4]; const wchar_t* vehicle; float level; int cents; int frame;
                     int core; int weapon[4]; };
#include "mech_parts.inc"
inline constexpr int kMechFrameCount=static_cast<int>(sizeof(kMechFrames)/sizeof(kMechFrames[0]));
inline constexpr int kMechWeaponCount=static_cast<int>(sizeof(kMechWeapons)/sizeof(kMechWeapons[0]));
inline constexpr int kMechCoreCount=static_cast<int>(sizeof(kMechCores)/sizeof(kMechCores[0]));
inline constexpr int kMechRequestCount=static_cast<int>(sizeof(kMechRequests)/sizeof(kMechRequests[0]));
static_assert(kMechRequestCount<64 && kMechFrameCount<16 && kMechCoreCount<16 && kMechWeaponCount<64,
              "the variant's fields (MechVariantBits): 6, 4, 4 and 6 bits, 0 = none");

// The four weapon slots: the right / left arm (a hand-held weapon on row 0 / 1, a forearm one on row 2 / 3), the right /
// left shoulder (row 4 / 5).
inline constexpr int kMechSlots=4;
inline constexpr int kMechFields=3+kMechSlots;   // request, frame, four weapons, core

struct MechLoadout {
    int request=-1;                       // kMechRequests index; -1 none (no loadout)
    int frame=-1;                         // kMechFrames index; -1 the request's own
    int core=-1;                          // kMechCores index; -1 the request's own
    int weapon[kMechSlots]{-1,-1,-1,-1};  // kMechWeapons index; -1 empty
    constexpr bool On() const noexcept { return request>=0; }
    constexpr bool operator==(const MechLoadout& o) const noexcept {
        if(request!=o.request || frame!=o.frame || core!=o.core)return false;
        for(int i=0;i<kMechSlots;++i)if(weapon[i]!=o.weapon[i])return false;
        return true;
    }
    constexpr bool operator!=(const MechLoadout& o) const noexcept { return !(*this==o); }
};

// `w` goes into slot `slot` (its mount and that side's file).
constexpr bool MechWeaponFits(int w,int slot) noexcept {
    if(w<0 || w>=kMechWeaponCount || slot<0 || slot>=kMechSlots)return false;
    const MechWeapon& x=kMechWeapons[w];
    const bool side=slot%2==0 ? x.right : x.left;
    return side && (slot<2 ? x.mount!=MechMount::shoulder : x.mount==MechMount::shoulder);
}
constexpr bool MechWeaponAllowed(int w,int slot,int request) noexcept {
    return request>=0 && request<kMechRequestCount && MechWeaponFits(w,slot) && kMechWeapons[w].level<=kMechRequests[request].cents;
}
constexpr bool MechCoreAllowed(int c,int request) noexcept {
    return request>=0 && request<kMechRequestCount && c>=0 && c<kMechCoreCount && kMechCores[c].level<=kMechRequests[request].cents;
}
// Every rule the parser applies; 0: valid, else what is wrong (MechFault).
enum class MechFault : std::uint8_t { none, request, frame, weaponFit, weaponLevel, noWeapon, core, coreLevel };
constexpr MechFault MechCheck(const MechLoadout& m,int* slotOut=nullptr) noexcept {
    if(m.request<0 || m.request>=kMechRequestCount)return MechFault::request;
    if(m.frame<-1 || m.frame>=kMechFrameCount)return MechFault::frame;
    bool any=false;
    for(int i=0;i<kMechSlots;++i) {
        const int w=m.weapon[i];
        if(w==-1)continue;
        if(slotOut)*slotOut=i;
        if(!MechWeaponFits(w,i))return MechFault::weaponFit;
        if(!MechWeaponAllowed(w,i,m.request))return MechFault::weaponLevel;
        any=true;
    }
    if(!any)return MechFault::noWeapon;
    if(m.core<-1 || m.core>=kMechCoreCount)return MechFault::core;
    if(m.core>=0 && !MechCoreAllowed(m.core,m.request))return MechFault::coreLevel;
    return MechFault::none;
}
constexpr bool MechValid(const MechLoadout& m) noexcept { return MechCheck(m)==MechFault::none; }
// The frame / core it is made with (its own, or the request's).
constexpr int MechFrameOf(const MechLoadout& m) noexcept {
    return m.frame>=0 ? m.frame : m.request>=0 && m.request<kMechRequestCount ? kMechRequests[m.request].frame : -1;
}
constexpr int MechCoreOf(const MechLoadout& m) noexcept {
    return m.core>=0 ? m.core : m.request>=0 && m.request<kMechRequestCount ? kMechRequests[m.request].core : -1;
}
// The request as it comes (its frame and core its own, its stock weapons).
constexpr MechLoadout MechStock(int request) noexcept {
    MechLoadout m;
    if(request<0 || request>=kMechRequestCount)return m;
    m.request=request;
    for(int i=0;i<kMechSlots;++i)m.weapon[i]=kMechRequests[request].weapon[i];
    return m;
}
// After the request changed: every weapon / core above its level (or no longer fitting) cleared, and its stock weapons
// when nothing is left.
constexpr MechLoadout MechWithRequest(const MechLoadout& from,int request) noexcept {
    MechLoadout m=from;
    m.request=request;
    bool any=false;
    for(int i=0;i<kMechSlots;++i) {
        if(m.weapon[i]>=0 && !MechWeaponAllowed(m.weapon[i],i,request))m.weapon[i]=-1;
        any=any || m.weapon[i]>=0;
    }
    if(m.core>=0 && !MechCoreAllowed(m.core,request))m.core=-1;
    if(!any)for(int i=0;i<kMechSlots;++i)m.weapon[i]=kMechRequests[request].weapon[i];
    return m;
}

// The variant's mech fields (support_loadout.h LoadoutVariant adds the vehicle and mech bits): bits 0-5 the request, 6-9
// the frame, 10-13 the core, 14-37 the four weapons, 6 bits each; 1-based indexes, 0 = none / the request's own.
inline constexpr int kMechVariantBits=38;
constexpr std::uint64_t MechVariantBits(const MechLoadout& m) noexcept {
    if(!m.On())return 0;
    std::uint64_t v=static_cast<std::uint64_t>(m.request+1)|static_cast<std::uint64_t>(m.frame+1)<<6|static_cast<std::uint64_t>(m.core+1)<<10;
    for(int i=0;i<kMechSlots;++i)v|=static_cast<std::uint64_t>(m.weapon[i]+1)<<(14+6*i);
    return v;
}
// Back (whatever a peer sent: every rule checked). False: not a mech loadout.
constexpr bool MechOfVariantBits(std::uint64_t v,MechLoadout* out) noexcept {
    if(v>>kMechVariantBits)return false;
    MechLoadout m;
    m.request=static_cast<int>(v&63u)-1;m.frame=static_cast<int>((v>>6)&15u)-1;m.core=static_cast<int>((v>>10)&15u)-1;
    for(int i=0;i<kMechSlots;++i)m.weapon[i]=static_cast<int>((v>>(14+6*i))&63u)-1;
    if(!MechValid(m))return false;
    if(out)*out=m;
    return true;
}

// --- The summon (player_vehicle.cpp) ---
namespace mech_detail {
inline const wchar_t* FileName(const wchar_t* path) noexcept {
    const wchar_t* name=path;
    for(const wchar_t* c=path;c && *c;++c)if(*c==L'/' || *c==L'\\')name=c+1;
    return name;
}
}  // namespace mech_detail
// A container configured for one of the mech frames (its vehicle path's file name, any case): a mech request.
inline bool IsMechFrameFile(const wchar_t* path) noexcept {
    if(!path)return false;
    const wchar_t* name=mech_detail::FileName(path);
    for(const auto& f:kMechFrames)if(!_wcsicmp(name,f.file))return true;
    return false;
}
// Which stock request a container configure is (its vehicle path and the level it is configured with: the pair is
// unique among the stock requests); -1 none.
inline int MechRequestOf(const wchar_t* path,float level) noexcept {
    if(!path)return -1;
    const wchar_t* name=mech_detail::FileName(path);
    for(int i=0;i<kMechRequestCount;++i)
        if(kMechRequests[i].level==level && !_wcsicmp(name,kMechRequests[i].vehicle))return i;
    return -1;
}
// A table token (any case) back to its index; -1 none.
template<class Row,std::size_t N> int MechIndexOf(const Row (&table)[N],const wchar_t* a,const wchar_t* b) noexcept {
    for(std::size_t i=0;i<N;++i) {
        const wchar_t* t=table[i].token;
        const wchar_t* p=a;
        for(;p<b && *t;++p,++t) {
            const wchar_t c=*p>=L'a' && *p<=L'z' ? static_cast<wchar_t>(*p-L'a'+L'A') : *p;
            if(c!=*t)break;
        }
        if(p==b && !*t)return static_cast<int>(i);
    }
    return -1;
}
}  // namespace crew
