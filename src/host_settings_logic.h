// The host's settings and every member's features in an online room, pure (no Windows, no game, no EDF6Coop:
// tests/host_settings_test.cpp). src/host_settings.cpp publishes and reads them (coop_attr.h).
//
// Why (2026-10-20 review of #222 / #219): some ini switches decide what happens to objects every machine shares, yet
// each machine read its own ini. VanillaRecruit (the stock walk-up join, run where the soldier's Think runs) and
// NpcFreeSquadCover (the free squad's cover: seen at a third of the range, damage x0.01) differed between a host and a
// guest with no word said; +0x540, the flag the stock reads for both, is not replicated (docs/npc-ai-design.md §2.3).
// Damage is settled on the object's authority only (ApplyDamage 0x547C30: in a session 0x7748F0 it asks slot 35 /
// slot 34 whether this machine owns the object, 0x547E1D..0x547ED9; a non-owner only tells the attacker it hit,
// 0x10000007, and returns at 0x548625, so the HP change from 0x548134 on runs on one machine). So these switches are the
// host's: every member publishes its own word, a guest uses the host's values (its own ini when the host published
// none: an older All Forces), and the menu says so.
// The same word carries the build's features (behaviours every machine must agree on, kFeat*): the room's notice names
// the members whose build differs and what may then differ, and a feature every member must have (the 408's gunner
// seat) is only opened when every member said it has it.
//
// CommanderMode (docs/feature-2026-11-21-commander-mode.md) is the host's too: every member's player becomes a commander
// and its NPC a shared object; it is decided once at the mission's sync (CommanderAtSync) and kept for that mission.
// The scoped weapons' spread (ScopeSpread*, scope_spread_logic.h, 2026-11-21) is the host's too: a round's spread is
// drawn on the shooter's machine and its hits on enemies are settled there (docs/scope-accuracy-re.md §4), so each machine
// reading its own ini would let one member play the stock spread while the others are penalised unzoomed.
//
// The support rules (2026-11-24, docs/armory-re.md §6「房主规则」): whether a support may be called (SupportDisabled) is
// the host's rule (support_dispatch.cpp Plan refuses by the host's SupportCfg), and the pt a call costs is its pt times
// the host's SupportPtScale (support_points.cpp: the host keeps the pool). So a guest's 整备 support page shows the host's:
// the word carries the host's SupportDisabled mask (d=, SupportConfig::disabled, 1..16 upper-case hex digits) and its
// SupportPtScale (p=, the float's 32 bits in 8 upper-case hex digits: exactly the value the host prices by).
//
// Word v3 carried the commander flag and the scope settings; v4 adds the support rules. Scope values are integers in
// units of 1/10000.
#pragma once
#include "scope_spread_logic.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace crew::hostcfg {
inline constexpr char kKey[]="EDF6X_AF_ROOM";   // EDF6X_ + [A-Z0-9_], at most 32
// A build's features (what may differ between two members when one lacks a bit).
enum : std::uint32_t {
    kFeatHostSettings=1u,      // takes the host's VanillaRecruit / NpcFreeSquadCover (this word)
    kFeatCreditReload41=2u,    // EDF4.1 calls reload by the score (EDF6 ReloadType 2), not by a timer (#219)
    kFeatBikeCarDrive=4u,      // the 408's request carries the Car's seven-number drive block (#219)
    kFeatBikeGunner=8u,        // the 408's sidecar gunner seat (edf41bike.cpp, #219)
    kFeatOutfit41Colours=16u,  // the EDF4.1 outfit keeps its own colours, in the member record too (#226)
    kFeatNoClimb41=32u,        // EDF4.1 friends do not enter EDF6's climb (#225)
    kFeatBikeGrounded=64u,     // the 408's gun is an animated part, not a keyframed body pinning the chassis in the air;
                               // its fall-out seats are its own (the object SGO's data, docs/edf41-bike-re.md 「悬空」)
    kFeatBikeGunMount=128u,    // the 408's seat-1 gun is built at all (edf41bike.cpp's gate over 0x65B15A): without it a
                               // build's gunner seat had no weapon and an unturnable Aim; the seat opens only with both
    kFeatBikeRide=256u,        // the 408 as its 4.1 rows say: rear-wheel drive with a free sidecar wheel, the driver on
                               // EDF6's bike pose, each SDL1 model's request with its own grip / top speed / torque
                               // (edf41bike.car_drive); and the shipped SGO is the generator's, so the 64 bit's data is in it
                               // (builds 64 / 128 shipped the keyframed gun still: docs/edf41-bike-re.md 2026-10-25)
    kFeatCommander=512u,       // the commander mode (commander.cpp): the commander's NPC request (support catalog
                               // kCommanderCatalog), the players' NoTarget / NoHpLoss and the NPC-wipe failure
    kFeatScopeSpread=1024u,
    kFeatHostRules=2048u,      // the host's SupportDisabled and SupportPtScale in this word (word v4): a guest's 整备
                               // support page shows the host's rules
};
inline constexpr std::uint32_t kFeatures=kFeatHostSettings|kFeatCreditReload41|kFeatBikeCarDrive|kFeatBikeGunner|
                                         kFeatOutfit41Colours|kFeatNoClimb41|kFeatBikeGrounded|kFeatBikeGunMount|
                                         kFeatBikeRide|kFeatCommander|kFeatScopeSpread|kFeatHostRules;
inline constexpr std::size_t kVersionMost=24;

struct Word {
    char version[kVersionMost]{};
    std::uint32_t features=0;
    bool vanillaRecruit=false,npcFreeSquadCover=false,commander=false;
    scopespread::Params scope{};
    std::uint64_t supportDisabled=0;   // SupportConfig::disabled (bit = catalog index)
    float ptScale=1.0f;                // SupportPtScale, 0..kMostPtScale
};
inline constexpr float kMostPtScale=100.0f;   // plugin.cpp's bound of SupportPtScale (support_points_net.h kMostScale)
inline constexpr std::size_t kWordMost=128;
// The longest word FormatWord writes: "4;v=" + version + ";f=" + 8 + ";r=1;c=1;m=1" + ";s=1," + 4 numbers of 6 digits
// with 3 commas + ";d=" + 16 + ";p=" + 8, and its NUL. room_sync_logic.h reserves kWordMost of Coop's 4096 bytes for it.
inline constexpr std::size_t kWordLongest=4+(kVersionMost-1)+3+8+12+5+(4*6+3)+3+16+3+8+1;
static_assert(kWordLongest<=kWordMost,"the longest word fits its buffer and its share of Coop's budget");
// A SupportPtScale in the word: the float's bits (exact), and back (false: not a scale plugin.cpp would have taken).
inline std::uint32_t ScaleBits(float v) noexcept { std::uint32_t b=0;std::memcpy(&b,&v,sizeof(b));return b; }
inline bool ScaleOfBits(std::uint32_t b,float& out) noexcept {
    float v=0;std::memcpy(&v,&b,sizeof(v));
    if(!(v>=0.0f) || !(v<=kMostPtScale))return false;   // NaN fails both
    out=v;
    return true;
}
// A ScopeSpread number in the word: 1/10000 units, rounded (every value is in [0, 20]: scope_spread_logic.h ranges).
inline constexpr std::int32_t kScopeUnit=10000,kScopeMost=999999;
constexpr std::int32_t ScopeToWord(float v) noexcept {
    return !(v>0.0f) ? 0 : v*kScopeUnit+0.5f>=kScopeMost ? kScopeMost : static_cast<std::int32_t>(v*kScopeUnit+0.5f);
}
constexpr float ScopeFromWord(std::int32_t v) noexcept { return static_cast<float>(v)/kScopeUnit; }

inline bool FormatWord(const Word& w,char* out,std::size_t size) noexcept {
    for(const char* p=w.version;*p;++p)if(*p==';' || *p<0x21 || *p>0x7E)return false;
    float scale=0;
    if(!ScaleOfBits(ScaleBits(w.ptScale),scale))return false;
    const int n=std::snprintf(out,size,"4;v=%s;f=%X;r=%d;c=%d;m=%d;s=%d,%d,%d,%d,%d;d=%llX;p=%08X",w.version,static_cast<unsigned>(w.features),
                              w.vanillaRecruit ? 1 : 0,w.npcFreeSquadCover ? 1 : 0,w.commander ? 1 : 0,w.scope.on ? 1 : 0,
                              static_cast<int>(ScopeToWord(w.scope.sniperHip)),static_cast<int>(ScopeToWord(w.scope.sniperHipMin)),
                              static_cast<int>(ScopeToWord(w.scope.hip)),static_cast<int>(ScopeToWord(w.scope.aim)),
                              static_cast<unsigned long long>(w.supportDisabled),static_cast<unsigned>(ScaleBits(w.ptScale)));
    return n>0 && static_cast<std::size_t>(n)<size;
}
namespace detail {
inline bool Field(std::string_view& rest,std::string_view name,std::string_view& value) noexcept {
    if(rest.substr(0,name.size())!=name)return false;
    rest.remove_prefix(name.size());
    const std::size_t end=rest.find(';');
    value=rest.substr(0,end);
    rest=end==std::string_view::npos ? std::string_view{} : rest.substr(end+1);
    return true;
}
inline bool Bit(std::string_view v,bool& out) noexcept {
    if(v!="0" && v!="1")return false;
    out=v=="1";
    return true;
}
// One whole number of the s field (decimal digits only, at most kScopeMost), the rest after its comma.
inline bool Number(std::string_view& rest,bool last,std::int32_t& out) noexcept {
    const std::size_t end=last ? rest.size() : rest.find(',');
    if(end==std::string_view::npos || end==0 || end>6)return false;
    std::int32_t v=0;
    for(std::size_t i=0;i<end;++i) {
        if(rest[i]<'0' || rest[i]>'9')return false;
        v=v*10+(rest[i]-'0');
    }
    out=v;
    rest.remove_prefix(last ? end : end+1);
    return true;
}
inline bool Scope(std::string_view v,scopespread::Params& out) noexcept {
    if(v.size()<2 || v[1]!=',' || !Bit(v.substr(0,1),out.on))return false;
    v.remove_prefix(2);
    std::int32_t n[4]{};
    for(int i=0;i<4;++i)if(!Number(v,i==3,n[i]))return false;
    if(!v.empty())return false;
    out.sniperHip=ScopeFromWord(n[0]);out.sniperHipMin=ScopeFromWord(n[1]);
    out.hip=ScopeFromWord(n[2]);out.aim=ScopeFromWord(n[3]);
    out=scopespread::Clamped(out);   // a host's value outside the ranges is played at the range's end, as the ini's is
    return true;
}
// Upper-case hex of 1..`most` digits.
inline bool Hex(std::string_view v,std::size_t most,std::uint64_t& out) noexcept {
    if(v.empty() || v.size()>most)return false;
    std::uint64_t r=0;
    for(const char c:v) {
        const int d=c>='0' && c<='9' ? c-'0' : c>='A' && c<='F' ? c-'A'+10 : -1;
        if(d<0)return false;
        r=r*16+static_cast<std::uint64_t>(d);
    }
    out=r;
    return true;
}
}  // namespace detail
// Strict: anything else (another version's word) is no word at all.
inline bool ParseWord(std::string_view text,Word& out) noexcept {
    Word w;
    std::string_view rest=text,v;
    if(rest.substr(0,2)!="4;")return false;
    rest.remove_prefix(2);
    if(!detail::Field(rest,"v=",v) || v.empty() || v.size()>=kVersionMost)return false;
    std::memcpy(w.version,v.data(),v.size());
    std::uint64_t f=0;
    if(!detail::Field(rest,"f=",v) || !detail::Hex(v,8,f))return false;
    w.features=static_cast<std::uint32_t>(f);
    if(!detail::Field(rest,"r=",v) || !detail::Bit(v,w.vanillaRecruit))return false;
    if(!detail::Field(rest,"c=",v) || !detail::Bit(v,w.npcFreeSquadCover))return false;
    if(!detail::Field(rest,"m=",v) || !detail::Bit(v,w.commander))return false;
    if(!detail::Field(rest,"s=",v) || !detail::Scope(v,w.scope))return false;
    if(!detail::Field(rest,"d=",v) || !detail::Hex(v,16,w.supportDisabled))return false;
    std::uint64_t bits=0;
    if(!detail::Field(rest,"p=",v) || v.size()!=8 || !detail::Hex(v,8,bits) ||
       !ScaleOfBits(static_cast<std::uint32_t>(bits),w.ptScale) || !rest.empty())return false;
    out=w;
    return true;
}

// What a machine plays by: the host's value as a guest in a room whose host published a word, else its own ini.
enum class Source : std::uint8_t { local, host, localHostSilent };
struct Effective { bool vanillaRecruit=false,npcFreeSquadCover=false; Source source=Source::local; scopespread::Params scope{}; };
constexpr Effective Decide(bool inRoom,bool isHost,bool hostWord,const Word& host,bool localRecruit,bool localCover,
                           const scopespread::Params& localScope=scopespread::Params{}) noexcept {
    if(!inRoom || isHost)return Effective{localRecruit,localCover,Source::local,localScope};
    if(!hostWord)return Effective{localRecruit,localCover,Source::localHostSilent,localScope};
    return Effective{host.vanillaRecruit,host.npcFreeSquadCover,Source::host,host.scope};
}
// The support rules a machine's 整备 page shows (and a guest's calls are refused / priced by on the host): the host's.
// The host and an offline machine: its own ini. A guest: the host's word's; none yet (`known` false: the panel says it
// waits for them, never this machine's ini in their place).
struct SupportRules { bool known=true; Source source=Source::local; std::uint64_t disabled=0; float ptScale=1.0f; };
constexpr SupportRules DecideSupportRules(bool inRoom,bool isHost,bool hostWord,const Word& host,std::uint64_t localDisabled,
                                          float localScale) noexcept {
    if(!inRoom || isHost)return SupportRules{true,Source::local,localDisabled,localScale};
    if(!hostWord)return SupportRules{false,Source::localHostSilent,0,0.0f};
    return SupportRules{true,Source::host,host.supportDisabled,host.ptScale};
}
constexpr bool SupportCallable(const SupportRules& r,int catalog) noexcept {
    return catalog<0 || catalog>=64 || !((r.disabled>>catalog)&1u);
}
constexpr bool SameSupportRules(const SupportRules& a,const SupportRules& b) noexcept {
    return a.known==b.known && a.source==b.source && a.disabled==b.disabled && a.ptScale==b.ptScale;
}

// Whether two ScopeSpread settings play the same (the word's units: what a guest would see of this machine's).
constexpr bool SameScope(const scopespread::Params& a,const scopespread::Params& b) noexcept {
    return a.on==b.on && (!a.on || (ScopeToWord(a.sniperHip)==ScopeToWord(b.sniperHip) &&
        ScopeToWord(a.sniperHipMin)==ScopeToWord(b.sniperHipMin) && ScopeToWord(a.hip)==ScopeToWord(b.hip) &&
        ScopeToWord(a.aim)==ScopeToWord(b.aim)));
}

// The room's builds compared, for the menu and the log: members whose word is missing (an older All Forces) or whose
// features differ from this build's, and the features one of them lacks (what may then differ in this room).
struct Builds {
    std::uint32_t members=0,silent=0,differing=0,missing=0;
    bool versionDiffers=false;
};
inline void Count(Builds& b,bool self,bool has,const Word* w,const char* myVersion) noexcept {
    ++b.members;
    if(self)return;
    if(!has || !w){++b.silent;b.missing|=kFeatures;return;}
    if((w->features&kFeatures)!=kFeatures){++b.differing;b.missing|=kFeatures&~w->features;}
    if(myVersion && std::strcmp(w->version,myVersion)!=0)b.versionDiffers=true;
}
// A feature every member must have (the 408's gunner seat): every member's word has it.
constexpr bool EveryoneHas(std::uint32_t features,std::uint32_t want) noexcept { return (features&want)==want; }

// The 408's sidecar gunner seat (kFeatBikeGunner). This machine's own value: open (as built). Decided for an online
// mission at its MissionSync (every member said it has it), kept for that mission. Before 2026-10-20 it was decided
// there alone, so a closed seat of the last online mission stayed closed offline (2026-10-20 review): it goes back to
// this machine's value wherever a mission without a room may begin, out of the room (the room / HQ frame seeing no
// room) and at an offline mission's start (mission.cpp PreloadHook: the offline preload, never reached online).
inline constexpr bool kBikeGunnerLocal=true;
// At an online mission's sync: the room's say (offline: this machine's).
constexpr bool BikeGunnerAtSync(bool inRoom,bool surveyed,std::uint32_t silent,std::uint32_t common) noexcept {
    // Both bits (2026-10-21): a member with the seat but not the built gun would play a weaponless seat while the others
    // fire, so such a room keeps the stock Car everywhere, as a room with an older build.
    return !inRoom ? kBikeGunnerLocal : surveyed && silent==0 && EveryoneHas(common,kFeatBikeGunner|kFeatBikeGunMount);
}
// At the room / HQ frame's read (no mission decided there): out of a room, this machine's; in one, as the last sync
// decided (the next sync decides again).
constexpr bool BikeGunnerOutOfMission(bool inRoom,bool current) noexcept { return inRoom ? current : kBikeGunnerLocal; }
// At an offline mission's start: this machine's.
constexpr bool BikeGunnerOffline() noexcept { return kBikeGunnerLocal; }

// The commander mode for a mission (decided at its sync, kept for it; offline at its start: this machine's ini). Online
// the host's word decides (the host: its own ini), and only when every member's build has it (kFeatCommander) and the room
// was read: a member that would play an ordinary soldier in a commanders' room is no such mission, so it is off for all.
// A guest whose host published no word (an older All Forces): off.
constexpr bool CommanderAtSync(bool inRoom,bool isHost,bool local,bool hostWord,bool hostCommander,bool surveyed,
                               std::uint32_t silent,std::uint32_t common) noexcept {
    if(!inRoom)return local;
    const bool wanted=isHost ? local : hostWord && hostCommander;
    return wanted && surveyed && silent==0 && EveryoneHas(common,kFeatCommander);
}
}  // namespace crew::hostcfg
