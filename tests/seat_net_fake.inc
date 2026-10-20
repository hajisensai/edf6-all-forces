// A stand-in for the vehicles' tag 15 transport (src/seat_net.cpp) for tests that run payload.cpp: what a send would
// put on the wire is kept, and a received block goes to the registered handler as seat_net.cpp's Receive hands it.
#pragma once
#include "../src/seat_net_transport.h"
namespace crew {
bool fakeSession=false;
bool InSession() noexcept { return fakeSession; }
namespace seat_net {
struct FakeSent { unsigned char* vehicle; unsigned char bytes[64]; std::size_t size; };
FakeSent fakeSent[64]{};
int fakeSends=0;
std::uint32_t fakeSequence=0;
std::int32_t fakeController=-1;
std::uint64_t fakeSender=0x51;
Handler fakeHandler=nullptr;
std::uint32_t fakeMagic=0;
bool Install() noexcept { return true; }
bool Ready() noexcept { return true; }
bool Register(std::uint32_t magic,Handler fn) noexcept { fakeMagic=magic;fakeHandler=fn;return true; }
bool ChainSlot(unsigned,const char*) noexcept { return true; }
bool ChainVehicle(unsigned char*) noexcept { return true; }
std::int32_t Reference(const void*) noexcept { return -1; }
std::int32_t SeatController(unsigned char*,unsigned) noexcept { return fakeController; }
bool Stamp(void* block,std::size_t size) noexcept {
    if(!block || size<sizeof(Head))return false;
    Head h;std::memcpy(&h,block,sizeof(h));h.sender=fakeSender;h.sequence=NextSequence(fakeSequence);std::memcpy(block,&h,sizeof(h));
    return true;
}
bool Send(unsigned char* v,const void* block,std::size_t size) noexcept {
    if(!v || !block || size>sizeof(FakeSent::bytes) || !Replicated(fakeSession,At<std::uint16_t>(v,0x128)))return false;
    FakeSent& s=fakeSent[fakeSends++%64];s.vehicle=v;std::memcpy(s.bytes,block,size);s.size=size;
    return true;
}
// The last pick sent (false: none since `from`).
inline bool LastPick(int from,Pick* into) noexcept {
    if(fakeSends<=from)return false;
    const FakeSent& s=fakeSent[(fakeSends-1)%64];
    return s.size==sizeof(Pick) && Decode(s.bytes,s.size,*into);
}
}  // namespace seat_net
namespace plugin_net {
std::uint32_t fakeRoomGeneration=0;
std::uint32_t RoomGeneration() noexcept { return fakeRoomGeneration; }
}  // namespace plugin_net
}  // namespace crew
