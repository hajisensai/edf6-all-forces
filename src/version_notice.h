// Whether the room's All Forces builds differ, and what that costs (2026-10-10, the user: 「进入房间发现版本不同步给一下
// 说明吧」). Pure: support_net.cpp feeds it what the existing exchanges already carry, tests/rescue_logic_test.cpp runs it.
//  - support_protocol.h features (Features: the capability bits and the extension bits in one word): a guest's hello
//    carries its own (the host compares every guest's with its own); the host's welcome carries the host's and whether
//    a guest of the room is behind it (an older host sends none: 0).
//  - the map command protocol (command_protocol.h kVersion): a message whose magic is the protocol's but whose version is
//    not this build's comes from a different build; it is dropped (map orders are not synchronised with that player).
// No new message: only fields the protocols already had.
#pragma once
#include <cstdint>

namespace crew::versionnote {
struct State {
    bool online=false,host=false;
    std::uint32_t mine=0;                       // this build's features (support_protocol.h kFeatures)
    // A guest's view of its host: whether the welcome came, what it announced, whether it said a guest is behind it.
    bool hostKnown=false,roomBehind=false;
    std::uint32_t hostCaps=0;
    // What a host that announces nothing (hostCaps 0: a build before the welcome carried them) is taken to have: the
    // newest build without the field (support_net.cpp passes every capability and kExtVariants). Older ones lack more.
    std::uint32_t silentHost=0;
    // A host's view of its guests: how many announced fewer capabilities, and what they lack between them.
    std::uint32_t peersBehind=0,peersMissing=0,peersAhead=0;   // ahead: announced capabilities this build does not know
    // Players whose map command messages carry another protocol version (seen by whoever receives them).
    std::uint32_t commandPeers=0;
};
enum class Kind : std::uint8_t { none, hostOlder, selfOlder, peersOlder, peersNewer, roomOlder, commandOnly };
struct Notice {
    Kind kind=Kind::none;
    std::uint32_t missing=0;   // capabilities that do not work in this room because of it (0: not named, e.g. newer than this build)
    std::uint32_t count=0;     // peersOlder: how many guests
    bool command=false;        // and the map commands are not synchronised with someone
    bool operator==(const Notice& o) const noexcept {return kind==o.kind && missing==o.missing && count==o.count && command==o.command;}
    bool operator!=(const Notice& o) const noexcept {return !(*this==o);}
};
constexpr Notice Compare(const State& s) noexcept {
    Notice n{};
    if(!s.online)return n;
    n.command=s.commandPeers>0;
    if(s.host) {
        if(s.peersBehind){n.kind=Kind::peersOlder;n.missing=s.peersMissing&s.mine;n.count=s.peersBehind;}
        else if(s.peersAhead){n.kind=Kind::peersNewer;n.count=s.peersAhead;}
    } else if(s.hostKnown) {
        const std::uint32_t caps=s.hostCaps ? s.hostCaps : s.silentHost;
        const std::uint32_t lacking=s.mine&~caps,extra=caps&~s.mine;
        if(lacking){n.kind=Kind::hostOlder;n.missing=lacking;}
        else if(extra)n.kind=Kind::selfOlder;   // the host's newer features: this build cannot name them
        else if(s.roomBehind)n.kind=Kind::roomOlder;
    }
    if(n.kind==Kind::none && n.command)n.kind=Kind::commandOnly;
    return n;
}
}  // namespace crew::versionnote
