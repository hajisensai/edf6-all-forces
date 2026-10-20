// The tag 15 transport's native side (seat_net.cpp; the kinds' formats: seat_net.h). Declared here so the native
// tests (tests/drill_net_native_test.cpp and the others) run the production reader / writer on a mapped EDF.dll.
#pragma once
#include "seat_net.h"
#include <windows.h>

namespace crew {
extern unsigned char* image;
namespace seat_net {
// A kind's receiver: the vehicle the message came for and its block (`size` bytes, the Head first). Game thread.
using Handler=void(*)(unsigned char* vehicle,const void* block,std::size_t size) noexcept;
// Once: the transport's native reads checked and a sender identity made. False: no plugin vehicle message goes online.
bool Install() noexcept;
bool Ready() noexcept;
// The receiver of a kind (by its magic).
bool Register(std::uint32_t magic,Handler fn) noexcept;
// Chain the receive slot 17 of a vehicle class's NetworkObject table at `slotRva` (the slot's own address), once.
bool ChainSlot(unsigned slotRva,const char* what) noexcept;
// Chain the NetworkObject class of vehicle `v` (+0x120's table) when its send / receive are the stock ones.
bool ChainVehicle(unsigned char* v) noexcept;
// The ReferenceId of a registered object (-1: none), and of the rider of `v`'s seat `seat` (the control epoch).
std::int32_t Reference(const void* object) noexcept;
std::int32_t SeatController(unsigned char* v,unsigned seat) noexcept;
// This machine's sender and its next sequence written into a block's head (false: no transport).
bool Stamp(void* block,std::size_t size) noexcept;
// Send a stamped block to `v`'s copies (false: offline, an unregistered copy, no transport).
bool Send(unsigned char* v,const void* block,std::size_t size) noexcept;
}  // namespace seat_net

namespace seat_transport {
constexpr unsigned kReceive=0x6325B0,kSend=0x773DA0;
constexpr unsigned kConstruct=0x79A460,kDestroy=0x760180,kWriteSmall=0x12B5790,kWriteBlock=0x12B5200;
constexpr unsigned kReadValue=0x12B4660,kReadBlock=0x12B4900,kMark=0x12B48A0,kRewind=0x12B4FD0,kReference=0x785050;
constexpr std::size_t kNet=0x120;
constexpr std::size_t kReceiveSlotIndex=17;   // slot 16 the send (+0x80), slot 17 the receive
constexpr int kMostClasses=48,kMostHandlers=8;
constexpr std::size_t kMostBlock=0x5E0;       // a block in the stream's 0x600 bytes after its 0x10 head
using ReceiveFn=void(__fastcall*)(unsigned char*,void*);
template<class F> F Fn(unsigned rva) noexcept { return reinterpret_cast<F>(image+rva); }
struct Stream {
    alignas(16) unsigned char bytes[0x600]{};
    Stream() { Fn<void*(__fastcall*)(void*,int)>(kConstruct)(bytes,0x40); }
    ~Stream() { Fn<void(__fastcall*)(void*)>(kDestroy)(bytes); }
    Stream(const Stream&)=delete;Stream& operator=(const Stream&)=delete;
};
struct Block { alignas(16) unsigned char bytes[kMostBlock]{}; std::size_t size=0; };
struct Signature { unsigned rva;unsigned char bytes[8]; };
extern const Signature signatures[11];
// A tag 15 message (the stream's read position restored): its kind's magic (0: none / shorter than one) and its block.
bool Peek(void* reader,std::uint32_t* magic,Block* block);
void __fastcall Receive(unsigned char* net,void* reader);
bool RegisteredWeak(const unsigned char* weak) noexcept;
}  // namespace seat_transport
}  // namespace crew
