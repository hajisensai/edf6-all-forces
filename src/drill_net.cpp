#define _CRT_RAND_S   // object_net.cpp (rand_s) may share this file's translation unit (the native tests)
// The drill's flight over the object message layer (object_net.cpp; its block: drill_net.h State, magic DRIL).
#include "drill_net.h"
#include "object_net_transport.h"
#include "crew.h"

namespace crew {
namespace {
bool ready=false;
void Received(unsigned char* v,const void* block,std::size_t size) noexcept {
    drill_net::State state;
    if(ready && Cfg().enabled && Cfg().drill && InSession() && drill_net::Decode(block,size,state))DrillNetReceived(v,state);
}
} // namespace

std::int32_t DrillNetController(unsigned char* v) noexcept { return object_net::SeatController(v,0); }
bool DrillNetSend(unsigned char* v,drill_net::State state) noexcept {
    if(!ready || !drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)))return false;
    state.controller=DrillNetController(v);
    if(!object_net::Stamp(&state,sizeof(state)) || !drill_net::Valid(state))return false;
    return object_net::Send(v,&state,sizeof(state));
}
bool InstallDrillNet() noexcept {
    ready=object_net::Install() && object_net::Register(drill_net::kMagic,&Received) && object_net::ChainClass(object_net::Class::drill505);
    Log("HOOK drill network=%d (505 NetworkObject slot 17, tag 15)",ready);
    return ready;
}
} // namespace crew
