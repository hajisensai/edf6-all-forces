// The plugin's online message channels (the registry, its rules and the roster: plugin_net_logic.h). Game thread only.
// A feature that shares state between the room's machines:
//  1. picks a magic (and a version) and, if an older build in the room must be left out, a support_protocol.h kExt* bit
//     its build announces in kExtBuilt (and names in support_net.cpp Features for the version notice);
//  2. registers a plugin_net::Channel (support_net.cpp RegisterPluginChannels lists them, in that order of precedence);
//  3. writes its messages with wire.h and sends them with Broadcast / SendHost / Send under its feature bit: a peer
//     that did not announce the bit is skipped (never sent bytes it cannot read);
//  4. in its receive, checks a message that names a player against its sender with SenderIsPlayer (command_identity.h),
//     tells a new run of its sender's by an origin (NewOrigin), and in `joined` sends a newly joined peer its whole state.
// Offline (no session) every call is nothing and every send false.
#pragma once
#include "plugin_net_logic.h"

namespace crew::plugin_net {
bool RegisterChannel(const Channel& channel) noexcept;

// The transport's state as support_net.cpp sees it, each frame (Update) and for each message (Receive).
struct Room {
    CommandNetworkContext context{};
    // The peer's authenticated PUID (1..context.peerCount; 0 this machine's), from the transport's snapshot.
    const char* (*puid)(std::uint32_t peer) noexcept=nullptr;
    // Host: the extension word of the peer's current hello (support_net::Session::PeerExtension; 0 not heard).
    std::uint32_t (*heard)(std::uint32_t peer) noexcept=nullptr;
    std::uint32_t ownExt=0;   // this machine's (support_protocol.h kExtBuilt and the variants hook's)
};
// support_net.cpp's: each frame and before each message (the channels' update, the roster, the joins).
void Update(const Room& room,std::uint64_t now) noexcept;
// True: a channel's message (delivered or dropped, never the support parser's); false: none of theirs.
bool Receive(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t now) noexcept;
// Every channel's reset (the room gone, a new mission's transport).
void Reset() noexcept;

// For the channels.
bool Ready() noexcept;                    // the room is up and its peers' indexes are the current ones
bool Host() noexcept;
std::uint32_t HostPeer() noexcept;        // a guest's host (0: this machine hosts, or offline)
std::uint32_t PeerCount() noexcept;
// Whether `peer` announced every bit of `featureBit` (0: true for every peer of the room).
bool PeerHas(std::uint32_t peer,std::uint32_t featureBit) noexcept;
bool Send(std::uint32_t peer,std::uint32_t featureBit,const void* bytes,std::size_t size) noexcept;
// To every peer that has the feature; the number sent.
std::uint32_t Broadcast(std::uint32_t featureBit,const void* bytes,std::size_t size) noexcept;
// A guest to its host (false on the host itself, or a host without the feature).
bool SendHost(std::uint32_t featureBit,const void* bytes,std::size_t size) noexcept;
// The room's changes so far (RoomWatch: another epoch, roster, host, local PUID; and the whole Reset): state keyed by
// the room's members outside a channel (payload.cpp's pick gates) is dropped when it moves. Game thread.
std::uint32_t RoomGeneration() noexcept;
// A new run's origin (rand_s; never zero).
std::uint32_t NewOrigin() noexcept;
}  // namespace crew::plugin_net
