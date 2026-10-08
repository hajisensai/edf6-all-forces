#define _CRT_RAND_S
#include "proteus_net.h"
#include "crew.h"
#include "memory.h"
#include "edf/patch.h"
#include <cstdlib>

namespace crew {
namespace proteus_transport {
constexpr unsigned kReceiveSlot=0x17DEE98,kReceive=0x6325B0,kSend=0x773DA0;
constexpr unsigned kConstruct=0x79A460,kDestroy=0x760180,kWriteSmall=0x12B5790,kWriteBlock=0x12B5200;
constexpr unsigned kReadValue=0x12B4660,kReadBlock=0x12B4900,kMark=0x12B48A0,kRewind=0x12B4FD0,kReference=0x785050;
constexpr std::size_t kNet=0x120;
using ReceiveFn=void(__fastcall*)(unsigned char*,void*);
void* nextReceive=nullptr;
bool ready=false;
std::uint64_t sender=0;
std::uint32_t sequence=0;
template<class F> F Fn(unsigned rva) noexcept { return reinterpret_cast<F>(image+rva); }
struct Stream {
    alignas(16) unsigned char bytes[0x600]{};
    Stream() { Fn<void*(__fastcall*)(void*,int)>(kConstruct)(bytes,0x40); }
    ~Stream() { Fn<void(__fastcall*)(void*)>(kDestroy)(bytes); }
};
// A live REGISTERED current driver identifies the control epoch. Empty and host-only NPC seats both use -1,
// matching online_authority's host takeover. Never retain the last driver: its late packets must fail the epoch.
bool RegisteredWeak(const unsigned char* weak) noexcept {
    const auto c=At<const unsigned char*>(weak,8);
    const auto rider=At<const unsigned char*>(weak,0);
    return rider && c && Readable(c,16) && At<long>(c,8)>0 && Readable(rider,0x12A) &&
        (At<std::uint16_t>(rider,0x128)&3)!=0;
}
bool Peek(void* reader,proteus_net::State& out,bool& valid) {
    std::int64_t mark=0;
    Fn<void(__fastcall*)(void*,std::int64_t*)>(kMark)(reader,&mark);
    const bool tag=Fn<std::int64_t(__fastcall*)(void*)>(kReadValue)(reader)==proteus_net::kTag;
    bool ours=false;
    valid=false;
    if(tag) {
        Stream block;
        if(Fn<bool(__fastcall*)(void*,void*)>(kReadBlock)(reader,block.bytes)) {
            const auto size=At<std::size_t>(block.bytes,0x5F0);
            ours=size>=4 && At<std::uint32_t>(block.bytes,0x10)==proteus_net::kMagic;
            valid=ours && proteus_net::Decode(block.bytes+0x10,size,out);
        }
    }
    Fn<void(__fastcall*)(void*,std::int64_t)>(kRewind)(reader,mark);
    return ours;
}
void __fastcall Receive(unsigned char* net,void* reader) {
    proteus_net::State state;
    bool valid=false;
    if(reader && Peek(reader,state,valid)) {
        if(valid && ready && Cfg().enabled && Cfg().proteus && InSession())ProteusNetReceived(net-kNet,state);
        return;
    }
    reinterpret_cast<ReceiveFn>(nextReceive)(net,reader);
}
struct Signature { unsigned rva;unsigned char bytes[8]; };
const Signature signatures[]={
    {kConstruct,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B}},
    {kDestroy,{0x48,0x8D,0x05,0x01,0xCE,0x08,0x01,0x48}},
    {kWriteSmall,{0x8D,0x42,0x0E,0x4C,0x8D,0x41,0x10,0x3C}},
    {kWriteBlock,{0x48,0x8B,0x81,0xF0,0x05,0x00,0x00,0x4D}},
    {kReadValue,{0x48,0x89,0x5C,0x24,0x08,0x4C,0x8B,0x51}},
    {kReadBlock,{0x48,0x89,0x5C,0x24,0x18,0x56,0x57,0x41}},
    {kMark,{0x48,0x8B,0x41,0x08,0x41,0xB8,0xFF,0xFF}},
    {kRewind,{0xB8,0xFF,0xFF,0xFF,0xFF,0x48,0x39,0x41}},
    {kReference,{0x48,0x89,0x5C,0x24,0x20,0x48,0x89,0x54}},
    {kSend,{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57}},
    {kReceive,{0x48,0x89,0x5C,0x24,0x08,0x55,0x56,0x57}},
};
} // namespace proteus_transport

std::int32_t ProteusNetController(unsigned char* v) noexcept {
    using namespace proteus_transport;
    const unsigned char* seat=SeatCount(v)>0 ? SeatAt(v,0) : nullptr;
    if(!seat)return -1;
    const unsigned char* weak=seat+kSeatRider;
    if(!RegisteredWeak(weak))return -1;
    return ProteusNetReference(At<const void*>(weak,0));
}
std::int32_t ProteusNetReference(const void* object) noexcept {
    using namespace proteus_transport;
    if(!ready || !object || !Readable(object,kSelfCtrl+8))return -1;
    const auto weak=static_cast<const unsigned char*>(object)+kSelf;
    if(!RegisteredWeak(weak))return -1;
    // 785050 takes weak_ptr BY VALUE: its epilogue (78511A..133) releases the argument's weak count. Passing the
    // seat's resident pair would consume its reference on every snapshot. Copy/addref exactly as its callers do.
    void* copy[2]={At<void*>(weak,0),At<void*>(weak,8)};
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(copy[1])+0xC));
    std::int32_t id=-1;
    Fn<std::int32_t*(__fastcall*)(std::int32_t*,const void*)>(kReference)(&id,copy);
    return id;
}
bool ProteusNetSend(unsigned char* v,proteus_net::State state) noexcept {
    using namespace proteus_transport;
    if(!ready || !drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)))return false;
    state.sender=sender;
    if(++sequence==0)++sequence;
    state.sequence=sequence;state.controller=ProteusNetController(v);
    if(!proteus_net::Valid(state))return false;
    Stream stream;
    if(!Fn<bool(__fastcall*)(void*,std::int8_t)>(kWriteSmall)(stream.bytes,proteus_net::kTag) ||
       !Fn<bool(__fastcall*)(void*,const void*,std::size_t)>(kWriteBlock)(stream.bytes,&state,sizeof(state)))return false;
    void* net=v+kNet;
    return reinterpret_cast<bool(__fastcall*)(void*,void*)>((*reinterpret_cast<void***>(net))[16])(net,stream.bytes);
}
bool InstallProteusNet() noexcept {
    using namespace proteus_transport;
    for(const auto& s:signatures)if(!Matches(s.rva,s.bytes,sizeof(s.bytes))) {
        Log("PROTEUS NET off: EDF+%X does not match the verified transport",s.rva);return false;
    }
    unsigned int lo=0,hi=0;
    if(rand_s(&lo) || rand_s(&hi)){Log("PROTEUS NET off: cannot create a sender identity");return false;}
    sender=(static_cast<std::uint64_t>(hi)<<32 | lo) | 1;
    ready=edf::ChainVtableSlot(reinterpret_cast<void**>(image+kReceiveSlot),reinterpret_cast<void*>(&Receive),&nextReceive);
    Log("HOOK proteus network=%d (BigBegaruta NetworkObject slot 17, tag 15)",ready);
    return ready;
}
} // namespace crew
