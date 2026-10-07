#define _CRT_RAND_S
#include "drill_net.h"
#include "crew.h"
#include "memory.h"
#include "edf/patch.h"
#include <cstdlib>

namespace crew {
namespace {
constexpr unsigned kReceiveSlot=0x17DB0D0,kReceive=0x6325B0,kSend=0x773DA0;
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
// The most recent REGISTERED driver identifies the control epoch. A host-only NPC dummy must never get a wire id:
// its peers have no matching object. Skip it and use the last registered rider on every copy, else the -1 epoch.
bool RegisteredWeak(const unsigned char* weak) noexcept {
    const auto c=At<const unsigned char*>(weak,8);
    const auto rider=At<const unsigned char*>(weak,0);
    return rider && c && Readable(c,16) && At<long>(c,8)>0 && Readable(rider,0x12A) &&
        (At<std::uint16_t>(rider,0x128)&3)!=0;
}
bool Peek(void* reader,drill_net::State& out,bool& valid) {
    std::int64_t mark=0;
    Fn<void(__fastcall*)(void*,std::int64_t*)>(kMark)(reader,&mark);
    const bool ours=Fn<std::int64_t(__fastcall*)(void*)>(kReadValue)(reader)==drill_net::kTag;
    valid=false;
    if(ours) {
        Stream block;
        valid=Fn<bool(__fastcall*)(void*,void*)>(kReadBlock)(reader,block.bytes) &&
            drill_net::Decode(block.bytes+0x10,At<std::size_t>(block.bytes,0x5F0),out);
    }
    Fn<void(__fastcall*)(void*,std::int64_t)>(kRewind)(reader,mark);
    return ours;
}
void __fastcall Receive(unsigned char* net,void* reader) {
    drill_net::State state;
    bool valid=false;
    if(reader && Peek(reader,state,valid)) {
        if(valid && ready && Cfg().enabled && Cfg().drill && InSession())DrillNetReceived(net-kNet,state);
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
} // namespace

std::int32_t DrillNetController(unsigned char* v) noexcept {
    const unsigned char* seat=SeatAt(v,0);
    if(!seat)return -1;
    const unsigned char* weak=seat+kSeatRider;
    if(!RegisteredWeak(weak))weak=seat+0x300;
    if(!RegisteredWeak(weak))return -1;
    // 785050 takes weak_ptr BY VALUE: its epilogue (78511A..133) releases the argument's weak count. Passing the
    // seat's resident pair would consume its reference on every snapshot. Copy/addref exactly as its callers do.
    void* copy[2]={At<void*>(weak,0),At<void*>(weak,8)};
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(copy[1])+0xC));
    std::int32_t id=-1;
    Fn<std::int32_t*(__fastcall*)(std::int32_t*,const void*)>(kReference)(&id,copy);
    return id;
}
bool DrillNetSend(unsigned char* v,drill_net::State state) noexcept {
    if(!ready || !drill_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)))return false;
    state.sender=sender;
    if(++sequence==0)++sequence;
    state.sequence=sequence;state.controller=DrillNetController(v);
    if(!drill_net::Valid(state))return false;
    Stream stream;
    if(!Fn<bool(__fastcall*)(void*,std::int8_t)>(kWriteSmall)(stream.bytes,drill_net::kTag) ||
       !Fn<bool(__fastcall*)(void*,const void*,std::size_t)>(kWriteBlock)(stream.bytes,&state,sizeof(state)))return false;
    void* net=v+kNet;
    return reinterpret_cast<bool(__fastcall*)(void*,void*)>((*reinterpret_cast<void***>(net))[16])(net,stream.bytes);
}
bool InstallDrillNet() noexcept {
    for(const auto& s:signatures)if(!Matches(s.rva,s.bytes,sizeof(s.bytes))) {
        Log("DRILL NET off: EDF+%X does not match the verified transport",s.rva);return false;
    }
    unsigned int lo=0,hi=0;
    if(rand_s(&lo) || rand_s(&hi)){Log("DRILL NET off: cannot create a sender identity");return false;}
    sender=(static_cast<std::uint64_t>(hi)<<32 | lo) | 1;
    ready=edf::ChainVtableSlot(reinterpret_cast<void**>(image+kReceiveSlot),reinterpret_cast<void*>(&Receive),&nextReceive);
    Log("HOOK drill network=%d (505 NetworkObject slot 17, tag 15)",ready);
    return ready;
}
} // namespace crew
