#define _CRT_RAND_S   // seat_net.cpp (rand_s) may share this file's translation unit (the native tests)
// The Proteus's control and shield over the vehicles' tag 15 transport (seat_net.cpp; its block: proteus_net.h State,
// magic PRNT). docs/proteus-online.md.
#include "proteus_net.h"
#include "seat_net_transport.h"
#include "crew.h"

namespace crew {
namespace {
constexpr unsigned kReceiveSlot=0x17DEE98;   // BigBegaruta's NetworkObject slot 17 (stock 0x6325B0)
bool ready=false;
// Ours (the magic): a decodable state of this version, or another build's block (its version).
void Received(unsigned char* v,const void* block,std::size_t size) noexcept {
    if(!ready || !Cfg().enabled || !Cfg().proteus || !InSession())return;
    proteus_net::State state;
    if(proteus_net::Decode(block,size,state))ProteusNetReceived(v,state);
    else if(const std::uint32_t foreign=proteus_net::ForeignVersion(block,size))ProteusNetIncompatible(v,foreign);
}
} // namespace

std::int32_t ProteusNetController(unsigned char* v) noexcept { return ready ? seat_net::SeatController(v,0) : -1; }
std::int32_t ProteusNetReference(const void* object) noexcept { return ready ? seat_net::Reference(object) : -1; }
bool ProteusNetSend(unsigned char* v,proteus_net::State state) noexcept {
    if(!ready || !drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)))return false;
    state.controller=ProteusNetController(v);
    if(!seat_net::Stamp(&state,sizeof(state)) || !proteus_net::Valid(state))return false;
    return seat_net::Send(v,&state,sizeof(state));
}
bool InstallProteusNet() noexcept {
    ready=seat_net::Install() && seat_net::Register(proteus_net::kMagic,&Received) &&
        seat_net::ChainSlot(kReceiveSlot,"Proteus (BigBegaruta)");
    Log("HOOK proteus network=%d (BigBegaruta NetworkObject slot 17, tag 15)",ready);
    return ready;
}
} // namespace crew
