// The object message layer's native side (object_net.cpp; the head, the gates, the kinds' registry and the class table:
// object_net.h). Declared here so the native tests (tests/drill_net_native_test.cpp, tests/edf41net_native_test.cpp and
// the others) run the production reader / writer on a mapped EDF.dll.
#pragma once
#include "object_net.h"
#include <windows.h>

namespace crew {
extern unsigned char* image;
namespace object_net {
// Once: the layer's native reads checked and a sender identity made. False: no plugin object message goes online.
bool Install() noexcept;
bool Ready() noexcept;
// The receiver of a kind (by its magic; object_net.h Kinds: a magic is one file's).
bool Register(std::uint32_t magic,Handler fn) noexcept;
// Chain the receive slot 17 of a class of kClasses, once (on top of whatever the slot holds: another module's hook stays
// in the chain, logged). False: no layer, or the slot is not readable.
bool ChainClass(Class c) noexcept;
// Chain the NetworkObject class of vehicle `v` (+0x120's table) when its send / receive are the stock ones.
bool ChainVehicle(unsigned char* v) noexcept;
// The ReferenceId of a registered object (-1: none), and of the rider of `v`'s seat `seat` (the control epoch).
std::int32_t Reference(const void* object) noexcept;
std::int32_t SeatController(unsigned char* v,unsigned seat) noexcept;
// This machine's sender and its next sequence written into a block's head (false: no layer).
bool Stamp(void* block,std::size_t size) noexcept;
// Send a stamped block to `object`'s copies (false: offline, an unregistered copy, no layer).
bool Send(unsigned char* object,const void* block,std::size_t size) noexcept;
// Development (edf41net.cpp's Edf41NetMirror, offline): a stamped block written by the game's stream writer, read back by
// its reader into `to`'s own NetworkObject slot 17, as a copy on another machine would get it (false: no layer).
bool Loopback(unsigned char* to,const void* block,std::size_t size) noexcept;
}  // namespace object_net

namespace object_transport {
constexpr unsigned kSend=object_net::kStockSend,kVehicleReceive=object_net::kVehicleReceive;
constexpr unsigned kConstruct=0x79A460,kDestroy=0x760180,kWriteSmall=0x12B5790,kWriteBlock=0x12B5200;
constexpr unsigned kReadValue=0x12B4660,kReadBlock=0x12B4900,kMark=0x12B48A0,kRewind=0x12B4FD0,kReference=0x785050;
constexpr unsigned kReaderFrom=0x12B4530;
constexpr std::size_t kNet=0x120;
constexpr int kMostClasses=48;
constexpr std::size_t kMostBlock=0x5E0;       // a block in the stream's 0x600 bytes after its 0x10 head
constexpr std::size_t kStreamData=0x10,kStreamSize=0x5F0;
using ReceiveFn=void(__fastcall*)(unsigned char*,void*);
using SendFn=bool(__fastcall*)(void*,void*);
template<class F> F Fn(unsigned rva) noexcept { return reinterpret_cast<F>(image+rva); }
// The game's stream object (+8 the read position, +0x10 the bytes, +0x5F0 the size); its destructor only resets vtables
// (0x760180), nothing is allocated.
struct Stream {
    alignas(16) unsigned char bytes[0x600]{};
    Stream() { Fn<void*(__fastcall*)(void*,int)>(kConstruct)(bytes,0x40); }
    ~Stream() { Fn<void(__fastcall*)(void*)>(kDestroy)(bytes); }
    Stream(const Stream&)=delete;Stream& operator=(const Stream&)=delete;
};
// A reader over the bytes a writer wrote (0x12B4530 makes it).
struct Reader {
    alignas(16) unsigned char bytes[0x600]{};
    explicit Reader(const Stream& written) {
        Fn<void(__fastcall*)(void*,const void*,std::size_t)>(kReaderFrom)(bytes,written.bytes+kStreamData,
            *reinterpret_cast<const std::size_t*>(written.bytes+kStreamSize));
    }
    ~Reader() { Fn<void(__fastcall*)(void*)>(kDestroy)(bytes); }
    Reader(const Reader&)=delete;Reader& operator=(const Reader&)=delete;
};
struct Block { alignas(16) unsigned char bytes[kMostBlock]{}; std::size_t size=0; };
struct Signature { unsigned rva;unsigned char bytes[8]; };
extern const Signature signatures[12];
// A tag 15 message (the stream's read position restored): its kind's magic (0: none / shorter than one) and its block.
bool Peek(void* reader,std::uint32_t* magic,Block* block);
// What the chained slot 17 does: a tag 15 block of a registered kind to its receiver, anything else to `next` as it came.
void Dispatch(unsigned char* net,void* reader,void* next);
// The receive hook of chain entry `i` (0..kMostClasses-1; null past it), and the table it was chained on (null: free).
ReceiveFn HookAt(int i) noexcept;
const void* ChainedTable(int i) noexcept;
bool RegisteredWeak(const unsigned char* weak) noexcept;
}  // namespace object_transport
}  // namespace crew
