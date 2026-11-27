#define _CRT_RAND_S   // object_net.cpp (rand_s) may share this file's translation unit (the native tests)
// The Sazabi's pose over the object message layer (object_net.cpp; its block: sazabi_net.h State, magic SZBI).
// docs/sazabi-online.md.
#include "sazabi_net.h"
#include "object_net_transport.h"
#include "crew.h"

namespace crew {
namespace {
bool ready=false;
void Received(unsigned char* v,const void* block,std::size_t size) noexcept {
    sazabi_net::State state;
    if(ready && Cfg().enabled && Cfg().sazabi && InSession() && sazabi_net::Decode(block,size,state))SazabiNetReceived(v,state);
}
} // namespace

std::int32_t SazabiNetController(unsigned char* v) noexcept { return ready ? object_net::SeatController(v,0) : -1; }
std::int32_t SazabiNetReference(const void* object) noexcept { return ready ? object_net::Reference(object) : -1; }
bool SazabiNetSend(unsigned char* v,sazabi_net::State state) noexcept {
    if(!ready || !drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)))return false;
    state.controller=SazabiNetController(v);
    if(!object_net::Stamp(&state,sizeof(state)) || !sazabi_net::Valid(state))return false;
    return object_net::Send(v,&state,sizeof(state));
}
bool InstallSazabiNet() noexcept {
    ready=object_net::Install() && object_net::Register(sazabi_net::kMagic,&Received) && object_net::ChainClass(object_net::Class::v506);
    Log("HOOK sazabi network=%d (506 NetworkObject slot 17, tag 15)",ready);
    return ready;
}
} // namespace crew
