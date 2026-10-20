// The plugin's online message channels, the pure part (plugin_net.h the game's; tests/plugin_net_test.cpp runs this).
// Every plugin-to-plugin message rides the one EDF6Coop extension poll support_net.cpp owns. Before 2026-10-20 each
// feature kept its own magic check, codec helpers, broadcast loop, sender check and roster reset, and support_net.cpp
// called them in a chain; an unknown message ended in the support parser, which logged it malformed every time.
// Now a channel is a row of this registry:
//  - its magic (the message's first little-endian word) or, for a channel that owns several, its own `owns` test;
//  - its version (the second word; 0: the channel reads its own versions, as the older ones do). A message of another
//    version is the channel's and dropped here, said by nobody (an older or newer build: the version notice says that);
//  - its feature bit (support_protocol.h kExt*, 0: none, every build that has the channel reads it): a message is sent only
//    to a peer that announced the bit, and one from a peer that did not announce it is dropped here (it cannot be this
//    build's: an older build never sends it). The bit is what lets an older build in the room be left out instead of being
//    sent bytes it would log as malformed;
//  - its size bounds (outside: dropped);
//  - its receive, update (each frame, the transport's context), reset (the room changed: the peer indexes mean other
//    machines now) and joined (a peer newly has the channel's feature, a new member or a newly heard one: the channel sends
//    it its whole state; until then it sent that peer nothing).
// Which peers have which feature: the host hears every guest's hello (support_protocol.h, the extension word); a guest hears
// only its host. So the host tells every guest that announced kExtPeerRoster the room's words by PUID (the roster, this
// file's codec), and a guest knows the others' from it.
#pragma once
#include "command_net.h"
#include "wire.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::plugin_net {
constexpr std::uint32_t kMaxPeers=1024;
constexpr std::size_t kMaxChannels=24;
constexpr std::size_t kMaxPayload=1024;   // EDF6COOP_EXTENSION_MAX_PAYLOAD

struct Channel {
    const char* name="";
    std::uint32_t magic=0;                                    // first word; 0: `owns` decides
    bool (*owns)(const void* bytes,std::size_t size) noexcept=nullptr;
    std::uint32_t version=0;                                  // second word; 0: the channel checks its own
    std::uint32_t featureBit=0;                               // support_protocol.h kExt*; 0: every peer
    std::size_t minSize=4,maxSize=kMaxPayload;
    bool (*receive)(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t now) noexcept=nullptr;
    void (*update)(const CommandNetworkContext& context,std::uint64_t now) noexcept=nullptr;
    void (*reset)() noexcept=nullptr;
    void (*joined)(std::uint32_t peer) noexcept=nullptr;
};

enum class Route : std::uint8_t {
    none,        // no channel's: the caller tries its own parser (support_net's v2 messages)
    delivered,   // given to the channel
    dropped,     // the channel's, but not taken: another version, a size out of bounds, a peer without the feature
};

class Registry {
public:
    // False: full, no receive, no magic and no owns, a magic taken already, bounds that make no sense.
    bool Register(const Channel& c) noexcept {
        if(count_>=kMaxChannels || !c.receive || (!c.magic && !c.owns) || c.minSize<4 || c.minSize>c.maxSize ||
           c.maxSize>kMaxPayload)return false;
        if(c.magic)for(std::size_t i=0;i<count_;++i)if(rows_[i].magic==c.magic)return false;
        rows_[count_++]=c;
        return true;
    }
    std::size_t Count() const noexcept { return count_; }
    const Channel& At(std::size_t i) const noexcept { return rows_[i<count_ ? i : 0]; }
    // The channel a message is for: its magic first, then the channels that test their own.
    const Channel* Find(const void* bytes,std::size_t size,std::size_t* index=nullptr) const noexcept {
        if(!bytes || size<4)return nullptr;
        const std::uint32_t magic=wire::HeadWord(bytes,size,0);
        for(std::size_t i=0;i<count_;++i)if(rows_[i].magic && rows_[i].magic==magic){if(index)*index=i;return &rows_[i];}
        for(std::size_t i=0;i<count_;++i)if(!rows_[i].magic && rows_[i].owns && rows_[i].owns(bytes,size)){if(index)*index=i;return &rows_[i];}
        return nullptr;
    }
    // `has(peer,bit)`: whether the peer announced the feature bit (plugin_net::Peers::Has).
    template<class Has>
    Route Dispatch(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t now,Has has) const noexcept {
        const Channel* c=Find(bytes,size);
        if(!c)return Route::none;
        if(size<c->minSize || size>c->maxSize)return Route::dropped;
        if(c->version && wire::HeadWord(bytes,size,1)!=c->version)return Route::dropped;
        if(c->featureBit && !has(peer,c->featureBit))return Route::dropped;
        // A channel's receive returning false would hand its bytes to another parser: it never may (they are its).
        c->receive(peer,puid,bytes,size,now);
        return Route::delivered;
    }
private:
    Channel rows_[kMaxChannels]{};
    std::size_t count_=0;
};

// Each peer's extension word as this machine knows it (0: not heard), and which channels were told it joined.
class Peers {
public:
    void Reset() noexcept { std::memset(ext_,0,sizeof(ext_));std::memset(told_,0,sizeof(told_));std::memset(known_,0,sizeof(known_)); }
    void Set(std::uint32_t peer,std::uint32_t ext) noexcept {
        if(!peer || peer>kMaxPeers)return;
        ext_[peer]=ext;known_[peer]=1;
    }
    // A peer's word gone (the host lost its hello: a new challenge): told again once heard.
    void Forget(std::uint32_t peer) noexcept { if(peer && peer<=kMaxPeers){ext_[peer]=0;told_[peer]=0;known_[peer]=0;} }
    std::uint32_t Ext(std::uint32_t peer) const noexcept { return peer && peer<=kMaxPeers ? ext_[peer] : 0u; }
    bool Known(std::uint32_t peer) const noexcept { return peer && peer<=kMaxPeers && known_[peer]; }
    // bit 0: every known-or-not peer (the older channels, read by every build that has them).
    bool Has(std::uint32_t peer,std::uint32_t bit) const noexcept {
        if(!peer || peer>kMaxPeers)return false;
        return !bit || (known_[peer] && (ext_[peer]&bit)==bit);
    }
    // For each channel with a joined callback and each peer 1..peers that has its feature now and was not told yet:
    // `joined(channel index, peer)`, once until Reset / Forget. A channel without a feature bit is told once the peer is
    // known (heard), so a newly heard peer gets its state.
    template<class Joined>
    void Joins(const Registry& reg,std::uint32_t peers,Joined joined) noexcept {
        if(peers>kMaxPeers)peers=kMaxPeers;
        for(std::uint32_t p=1;p<=peers;++p) {
            if(!known_[p])continue;
            for(std::size_t i=0;i<reg.Count() && i<32;++i) {
                const Channel& c=reg.At(i);
                const std::uint32_t mask=1u<<i;
                if(!c.joined || (told_[p]&mask) || !Has(p,c.featureBit))continue;
                told_[p]|=mask;
                joined(i,p);
            }
        }
    }
private:
    std::uint32_t ext_[kMaxPeers+1]{};
    std::uint32_t told_[kMaxPeers+1]{};
    unsigned char known_[kMaxPeers+1]{};
};
static_assert(kMaxChannels<=32,"Peers::told_ keeps one bit a channel");

// The roster (host -> every guest that announced kExtPeerRoster): the room's members by PUID and the extension word each
// announced (the host's own among them). Up to kRosterPerMessage a message: a bigger room goes in several, each merged.
// Wire: magic, version, count (u32); then count entries {ext u32, puid 64 bytes NUL-padded} (a PUID is at most 64 chars).
constexpr std::uint32_t kRosterMagic=0x52534E50u;   // "PNSR"
constexpr std::uint32_t kRosterVersion=1;
constexpr std::size_t kPuidChars=64,kRosterEntry=4+kPuidChars,kRosterHead=12;
constexpr std::size_t kRosterPerMessage=(kMaxPayload-kRosterHead)/kRosterEntry;
static_assert(kRosterPerMessage>=8,"a room of eight fits one roster message");
struct RosterEntry {
    char puid[kPuidChars+1]{};
    std::uint32_t ext=0;
};
inline bool ValidPuid(const char* p) noexcept {
    if(!p || !p[0])return false;
    std::size_t n=0;
    while(n<=kPuidChars && p[n])++n;
    if(n>kPuidChars)return false;
    for(std::size_t i=0;i<n;++i)if(static_cast<unsigned char>(p[i])<0x21 || static_cast<unsigned char>(p[i])>0x7E)return false;
    return true;
}
// The bytes used (0: nothing encoded: no room, a bad PUID, more than kRosterPerMessage).
inline std::size_t EncodeRoster(const RosterEntry* e,std::size_t count,void* bytes,std::size_t size) noexcept {
    if(!e || !count || count>kRosterPerMessage)return 0;
    const std::size_t used=kRosterHead+count*kRosterEntry;
    if(size<used)return 0;
    wire::Writer w(bytes,used);
    w.U32(kRosterMagic).U32(kRosterVersion).U32(static_cast<std::uint32_t>(count));
    for(std::size_t i=0;i<count;++i) {
        if(!ValidPuid(e[i].puid))return 0;
        char padded[kPuidChars]{};
        std::memcpy(padded,e[i].puid,std::strlen(e[i].puid));
        w.U32(e[i].ext).Bytes(padded,kPuidChars);
    }
    return w.Ok() && w.Used()==used ? used : 0;
}
// The entries (at most kRosterPerMessage); false: not a roster of this version, a count that is not the size's, a PUID
// that is not one.
inline bool DecodeRoster(const void* bytes,std::size_t size,RosterEntry* out,std::size_t* count) noexcept {
    if(!out || !count)return false;
    *count=0;
    wire::Reader r(bytes,size);
    if(r.U32()!=kRosterMagic || r.U32()!=kRosterVersion)return false;
    const std::uint32_t n=r.U32();
    if(!r.Ok() || !n || n>kRosterPerMessage || size!=kRosterHead+n*kRosterEntry)return false;
    for(std::uint32_t i=0;i<n;++i) {
        RosterEntry e{};
        e.ext=r.U32();
        char raw[kPuidChars];r.Bytes(raw,kPuidChars);
        std::size_t len=0;while(len<kPuidChars && raw[len])++len;
        for(std::size_t k=len;k<kPuidChars;++k)if(raw[k])return false;   // padding is zeros
        std::memcpy(e.puid,raw,len);e.puid[len]=0;
        if(!ValidPuid(e.puid))return false;
        out[i]=e;
    }
    if(!r.Ok())return false;
    *count=n;
    return true;
}

// A sender's run (a channel's `origin`: a new one tells receivers its state starts over): random, never zero.
constexpr std::uint32_t Origin(std::uint32_t random,std::uint64_t fallback) noexcept {
    return (random ? random : static_cast<std::uint32_t>(fallback ^ (fallback>>32)))|1u;
}
}  // namespace crew::plugin_net
