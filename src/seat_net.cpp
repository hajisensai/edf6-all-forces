// The vehicles' tag 15 transport (seat_net.h): one receive hook on each vehicle class's NetworkObject slot 17, one
// sender identity and sequence, one send through slot 16. The drill (505), the Proteus (BigBegaruta) and the Sazabi
// (506) chain their classes' slot at install, as before; a stock vehicle's class is chained the first time one of its
// vehicles carries a seat pick (ChainVehicle). docs/drill-re.md §8.5 has the native reads.
#define _CRT_RAND_S
#include "seat_net_transport.h"
#include "crew.h"
#include "memory.h"
#include "edf/patch.h"
#include <atomic>
#include <cstdlib>

namespace crew {
namespace seat_transport {
void* nextReceive[kMostClasses]{};
const void* chainedTable[kMostClasses]{};
std::atomic<int> chained{0};
SRWLOCK chainLock=SRWLOCK_INIT;
bool transportReady=false,transportTried=false;
std::uint64_t senderId=0;
std::uint32_t sequenceCounter=0;
struct Handler { std::uint32_t magic; seat_net::Handler fn; };
Handler handlers[kMostHandlers]{};
int handlerCount=0;

const Signature signatures[11]={
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

// A live REGISTERED current rider identifies the control epoch. Empty and host-only NPC seats both use -1, matching
// online_authority's host takeover. Never retain the last rider: its late messages must fail the epoch.
bool RegisteredWeak(const unsigned char* weak) noexcept {
    const auto c=At<const unsigned char*>(weak,8);
    const auto rider=At<const unsigned char*>(weak,0);
    return rider && c && Readable(c,16) && At<long>(c,8)>0 && Readable(rider,0x12A) &&
        (At<std::uint16_t>(rider,0x128)&3)!=0;
}

bool Peek(void* reader,std::uint32_t* magic,Block* into) {
    std::int64_t mark=0;
    Fn<void(__fastcall*)(void*,std::int64_t*)>(kMark)(reader,&mark);
    const bool tag=Fn<std::int64_t(__fastcall*)(void*)>(kReadValue)(reader)==seat_net::kTag;
    *magic=0;
    if(into)into->size=0;
    if(tag) {
        Stream block;
        if(Fn<bool(__fastcall*)(void*,void*)>(kReadBlock)(reader,block.bytes)) {
            const auto size=At<std::size_t>(block.bytes,0x5F0);
            const std::size_t room=sizeof(block.bytes)-0x10;
            *magic=seat_net::MagicOf(block.bytes+0x10,size<=room ? size : 0);
            if(into && size<=sizeof(into->bytes)){std::memcpy(into->bytes,block.bytes+0x10,size);into->size=size;}
        }
    }
    Fn<void(__fastcall*)(void*,std::int64_t)>(kRewind)(reader,mark);
    return tag;
}

seat_net::Handler HandlerOf(std::uint32_t magic) noexcept {
    for(int i=0;i<handlerCount;++i)if(handlers[i].magic==magic)return handlers[i].fn;
    return nullptr;
}
void* NextOf(const void* table) noexcept {
    const int n=chained.load(std::memory_order_acquire);
    for(int i=0;i<n;++i)if(chainedTable[i]==table)return nextReceive[i];
    return image+kReceive;
}
// A tag 15 block of a kind registered here goes to its handler (taken: the stock receiver does nothing with tag 15);
// any other message (coop's 13 / 14, the stock ones, a kind of another build) to the next receiver as it came.
void __fastcall Receive(unsigned char* net,void* reader) {
    std::uint32_t magic=0;
    Block block;
    if(reader && Peek(reader,&magic,&block)) {
        if(const seat_net::Handler fn=transportReady ? HandlerOf(magic) : nullptr){fn(net-kNet,block.bytes,block.size);return;}
    }
    reinterpret_cast<ReceiveFn>(NextOf(At<const void*>(net,0)))(net,reader);
}

// Chain the receive slot `slot` of the NetworkObject table `table` (once a table).
bool Chain(void** table,const char* what) noexcept {
    AcquireSRWLockExclusive(&chainLock);
    bool ok=false;
    const int n=chained.load(std::memory_order_relaxed);
    for(int i=0;i<n;++i)if(chainedTable[i]==table)ok=true;
    if(!ok && n<kMostClasses) {
        void* next=nullptr;
        ok=edf::ChainVtableSlot(table+kReceiveSlotIndex,reinterpret_cast<void*>(&Receive),&next);
        if(ok){nextReceive[n]=next;chainedTable[n]=table;chained.store(n+1,std::memory_order_release);}
        Log("SEAT NET %s: NetworkObject EDF+%llX slot 17 chained=%d",what,
            static_cast<unsigned long long>(reinterpret_cast<unsigned char*>(table)-image),ok);
    }
    ReleaseSRWLockExclusive(&chainLock);
    return ok;
}
}  // namespace seat_transport

namespace seat_net {
using namespace seat_transport;
bool Install() noexcept {
    if(transportTried)return transportReady;
    transportTried=true;
    for(const auto& s:signatures)if(!Matches(s.rva,s.bytes,sizeof(s.bytes))) {
        Log("SEAT NET off: EDF+%X does not match the verified transport",s.rva);return false;
    }
    unsigned int lo=0,hi=0;
    if(rand_s(&lo) || rand_s(&hi)){Log("SEAT NET off: cannot create a sender identity");return false;}
    senderId=(static_cast<std::uint64_t>(hi)<<32 | lo) | 1;
    transportReady=true;
    Log("SEAT NET on: tag %d, sender %016llX",kTag,static_cast<unsigned long long>(senderId));
    return true;
}
bool Ready() noexcept { return transportReady; }
bool Register(std::uint32_t magic,Handler fn) noexcept {
    if(!magic || !fn)return false;
    for(int i=0;i<handlerCount;++i)if(handlers[i].magic==magic){handlers[i].fn=fn;return true;}
    if(handlerCount>=kMostHandlers)return false;
    handlers[handlerCount++]=seat_transport::Handler{magic,fn};
    return true;
}
bool ChainSlot(unsigned slotRva,const char* what) noexcept {
    if(!transportReady)return false;
    __try {
        auto table=reinterpret_cast<void**>(image+slotRva)-kReceiveSlotIndex;
        if(table[kReceiveSlotIndex]!=image+kReceive && table[kReceiveSlotIndex]!=reinterpret_cast<void*>(&Receive)) {
            Log("SEAT NET %s: slot 17 is not the stock receiver",what);return false;
        }
        return Chain(table,what);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ChainVehicle(unsigned char* v) noexcept {
    if(!transportReady || !v)return false;
    __try {
        auto table=At<void**>(v,kNet);
        if(!table || !Readable(table,(kReceiveSlotIndex+1)*8))return false;
        const int n=chained.load(std::memory_order_acquire);
        for(int i=0;i<n;++i)if(chainedTable[i]==table)return true;
        // Only a class whose send and receive are the stock NetworkObject's (read: 0x773DA0 / 0x6325B0).
        if(table[kReceiveSlotIndex-1]!=image+kSend || table[kReceiveSlotIndex]!=image+kReceive)return false;
        return Chain(table,"vehicle class");
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::int32_t Reference(const void* object) noexcept {
    if(!transportReady || !object || !Readable(object,kSelfCtrl+8))return -1;
    const auto weak=static_cast<const unsigned char*>(object)+kSelf;
    if(!RegisteredWeak(weak))return -1;
    // 785050 takes weak_ptr BY VALUE: its epilogue (78511A..133) releases the argument's weak count. Passing the
    // object's resident pair would consume its reference on every call. Copy/addref exactly as its callers do.
    void* copy[2]={At<void*>(weak,0),At<void*>(weak,8)};
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(copy[1])+0xC));
    std::int32_t id=-1;
    Fn<std::int32_t*(__fastcall*)(std::int32_t*,const void*)>(kReference)(&id,copy);
    return id;
}
std::int32_t SeatController(unsigned char* v,unsigned seat) noexcept {
    if(!v || seat>=SeatCount(v))return -1;
    const unsigned char* weak=SeatAt(v,seat)+kSeatRider;
    if(!RegisteredWeak(weak))return -1;
    return Reference(At<const void*>(weak,0));
}
bool Stamp(void* block,std::size_t size) noexcept {
    if(!transportReady || !block || size<sizeof(Head))return false;
    Head head;std::memcpy(&head,block,sizeof(head));
    head.sender=senderId;head.sequence=NextSequence(sequenceCounter);
    std::memcpy(block,&head,sizeof(head));
    return true;
}
bool Send(unsigned char* v,const void* block,std::size_t size) noexcept {
    if(!transportReady || !v || !block || size<sizeof(Head) || size>kMostBlock || !Replicated(InSession(),At<std::uint16_t>(v,0x128)))return false;
    Stream stream;
    if(!Fn<bool(__fastcall*)(void*,std::int8_t)>(kWriteSmall)(stream.bytes,kTag) ||
       !Fn<bool(__fastcall*)(void*,const void*,std::size_t)>(kWriteBlock)(stream.bytes,block,size))return false;
    void* net=v+kNet;
    return reinterpret_cast<bool(__fastcall*)(void*,void*)>((*reinterpret_cast<void***>(net))[kReceiveSlotIndex-1])(net,stream.bytes);
}
}  // namespace seat_net
}  // namespace crew
