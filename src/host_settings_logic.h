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
// The word: "1;v=<version>;f=<features, hex>;r=<VanillaRecruit 0|1>;c=<NpcFreeSquadCover 0|1>".
#pragma once
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
};
inline constexpr std::uint32_t kFeatures=kFeatHostSettings|kFeatCreditReload41|kFeatBikeCarDrive|kFeatBikeGunner|
                                         kFeatOutfit41Colours|kFeatNoClimb41;
inline constexpr std::size_t kVersionMost=24;

struct Word {
    char version[kVersionMost]{};
    std::uint32_t features=0;
    bool vanillaRecruit=false,npcFreeSquadCover=false;
};
inline constexpr std::size_t kWordMost=96;

inline bool FormatWord(const Word& w,char* out,std::size_t size) noexcept {
    for(const char* p=w.version;*p;++p)if(*p==';' || *p<0x21 || *p>0x7E)return false;
    const int n=std::snprintf(out,size,"1;v=%s;f=%X;r=%d;c=%d",w.version,static_cast<unsigned>(w.features),
                              w.vanillaRecruit ? 1 : 0,w.npcFreeSquadCover ? 1 : 0);
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
}  // namespace detail
// Strict: anything else (another version's word) is no word at all.
inline bool ParseWord(std::string_view text,Word& out) noexcept {
    Word w;
    std::string_view rest=text,v;
    if(rest.substr(0,2)!="1;")return false;
    rest.remove_prefix(2);
    if(!detail::Field(rest,"v=",v) || v.empty() || v.size()>=kVersionMost)return false;
    std::memcpy(w.version,v.data(),v.size());
    if(!detail::Field(rest,"f=",v) || v.empty() || v.size()>8)return false;
    std::uint32_t f=0;
    for(const char c:v) {
        const int d=c>='0' && c<='9' ? c-'0' : c>='A' && c<='F' ? c-'A'+10 : -1;
        if(d<0)return false;
        f=f*16+static_cast<std::uint32_t>(d);
    }
    w.features=f;
    if(!detail::Field(rest,"r=",v) || !detail::Bit(v,w.vanillaRecruit))return false;
    if(!detail::Field(rest,"c=",v) || !detail::Bit(v,w.npcFreeSquadCover) || !rest.empty())return false;
    out=w;
    return true;
}

// What a machine plays by: the host's value as a guest in a room whose host published a word, else its own ini.
enum class Source : std::uint8_t { local, host, localHostSilent };
struct Effective { bool vanillaRecruit=false,npcFreeSquadCover=false; Source source=Source::local; };
constexpr Effective Decide(bool inRoom,bool isHost,bool hostWord,const Word& host,bool localRecruit,bool localCover) noexcept {
    if(!inRoom || isHost)return Effective{localRecruit,localCover,Source::local};
    if(!hostWord)return Effective{localRecruit,localCover,Source::localHostSilent};
    return Effective{host.vanillaRecruit,host.npcFreeSquadCover,Source::host};
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
    return !inRoom ? kBikeGunnerLocal : surveyed && silent==0 && EveryoneHas(common,kFeatBikeGunner);
}
// At the room / HQ frame's read (no mission decided there): out of a room, this machine's; in one, as the last sync
// decided (the next sync decides again).
constexpr bool BikeGunnerOutOfMission(bool inRoom,bool current) noexcept { return inRoom ? current : kBikeGunnerLocal; }
// At an offline mission's start: this machine's.
constexpr bool BikeGunnerOffline() noexcept { return kBikeGunnerLocal; }
}  // namespace crew::hostcfg
