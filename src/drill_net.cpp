#define _CRT_RAND_S   // seat_net.cpp (rand_s) may share this file's translation unit (the native tests)
// The drill's flight over the vehicles' tag 15 transport (seat_net.cpp; its block: drill_net.h State, magic DRIL).
#include "drill_net.h"
#include "seat_net_transport.h"
#include "crew.h"

namespace crew {
namespace {
constexpr unsigned kReceiveSlot=0x17DB0D0;   // the 505's NetworkObject slot 17 (stock 0x6325B0)
bool ready=false;
void Received(unsigned char* v,const void* block,std::size_t size) noexcept {
    drill_net::State state;
    if(ready && Cfg().enabled && Cfg().drill && InSession() && drill_net::Decode(block,size,state))DrillNetReceived(v,state);
}
} // namespace

std::int32_t DrillNetController(unsigned char* v) noexcept { return seat_net::SeatController(v,0); }
bool DrillNetSend(unsigned char* v,drill_net::State state) noexcept {
    if(!ready || !drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)))return false;
    state.controller=DrillNetController(v);
    if(!seat_net::Stamp(&state,sizeof(state)) || !drill_net::Valid(state))return false;
    return seat_net::Send(v,&state,sizeof(state));
}
bool InstallDrillNet() noexcept {
    ready=seat_net::Install() && seat_net::Register(drill_net::kMagic,&Received) && seat_net::ChainSlot(kReceiveSlot,"drill (505)");
    Log("HOOK drill network=%d (505 NetworkObject slot 17, tag 15)",ready);
    return ready;
}
} // namespace crew
