// The plugin's object message layer (object_net.h): one receive hook chained on each NetworkObject class's slot 17 (the
// class table kClasses, chained by the kinds that live on them; a stock vehicle class the first time one of its vehicles
// carries a message), one sender identity and sequence, one send through slot 16, one registry of kinds by magic.
// docs/seat-net.md; docs/drill-re.md section 8.5 has the native reads.
#define _CRT_RAND_S
#include "object_net_transport.h"
#include "edf_layout.h"
#include "plugin_log.h"
#include "netprobe.h"
#include "memory.h"
#include "edf/patch.h"
#include <atomic>
#include <cstdlib>
#include <utility>

namespace crew {
namespace object_transport {
// Chain entry i: the table it was chained on and what its slot 17 held before (each entry's hook calls on its own next,
// so a table copied from a chained one by another module still reaches the right receiver).
void* nextReceive[kMostClasses]{};
const void* chainedTable[kMostClasses]{};
std::atomic<int> chained{0};
SRWLOCK chainLock=SRWLOCK_INIT;
bool layerReady=false,layerTried=false;
std::uint64_t senderId=0;
std::atomic<std::uint32_t> sequenceCounter{0};
object_net::Kinds<object_net::kMostKinds> kinds;

const Signature signatures[12]={
    {kConstruct,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B}},
    {kDestroy,{0x48,0x8D,0x05,0x01,0xCE,0x08,0x01,0x48}},
    {kWriteSmall,{0x8D,0x42,0x0E,0x4C,0x8D,0x41,0x10,0x3C}},
    {kWriteBlock,{0x48,0x8B,0x81,0xF0,0x05,0x00,0x00,0x4D}},
    {kReadValue,{0x48,0x89,0x5C,0x24,0x08,0x4C,0x8B,0x51}},
    {kReadBlock,{0x48,0x89,0x5C,0x24,0x18,0x56,0x57,0x41}},
    {kMark,{0x48,0x8B,0x41,0x08,0x41,0xB8,0xFF,0xFF}},
    {kRewind,{0xB8,0xFF,0xFF,0xFF,0xFF,0x48,0x39,0x41}},
    {kReaderFrom,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8D}},
    {kReference,{0x48,0x89,0x5C,0x24,0x20,0x48,0x89,0x54}},
    {kSend,{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57}},
    {kVehicleReceive,{0x48,0x89,0x5C,0x24,0x08,0x55,0x56,0x57}},
    // GameObjectBase's receiver 0x54D770 is not fingerprinted: EDF6Coop hooks its first instruction (W3 / W4, found on the
    // real game 2026-10-15, where a fingerprint turned the EDF4.1 layer off). It is only called on, never read.
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
    const bool tag=Fn<std::int64_t(__fastcall*)(void*)>(kReadValue)(reader)==object_net::kTag;
    *magic=0;
    if(into)into->size=0;
    if(tag) {
        Stream block;
        if(Fn<bool(__fastcall*)(void*,void*)>(kReadBlock)(reader,block.bytes)) {
            const auto size=At<std::size_t>(block.bytes,kStreamSize);
            const std::size_t room=sizeof(block.bytes)-kStreamData;
            *magic=object_net::MagicOf(block.bytes+kStreamData,size<=room ? size : 0);
            if(into && size<=sizeof(into->bytes)){std::memcpy(into->bytes,block.bytes+kStreamData,size);into->size=size;}
        }
    }
    Fn<void(__fastcall*)(void*,std::int64_t)>(kRewind)(reader,mark);
    return tag;
}

// A tag 15 block of a kind registered here goes to its receiver (taken: the stock receivers do nothing with tag 15);
// any other message (coop's 13 / 14, the stock ones, a kind no file here registered) to the next receiver as it came.
void Dispatch(unsigned char* net,void* reader,void* next) {
    std::uint32_t magic=0;
    Block block;
    if(reader && layerReady && Peek(reader,&magic,&block)) {
        if(const object_net::Handler fn=kinds.Find(magic)){fn(net-kNet,block.bytes,block.size);return;}
    }
    reinterpret_cast<ReceiveFn>(next)(net,reader);
}

template<int I> void __fastcall ReceiveAt(unsigned char* net,void* reader) { Dispatch(net,reader,nextReceive[I]); }
template<int... I> constexpr auto MakeHooks(std::integer_sequence<int,I...>) noexcept {
    struct Hooks { ReceiveFn at[sizeof...(I)]; };
    return Hooks{{&ReceiveAt<I>...}};
}
constexpr auto hooks=MakeHooks(std::make_integer_sequence<int,kMostClasses>{});

ReceiveFn HookAt(int i) noexcept { return i>=0 && i<kMostClasses ? hooks.at[i] : nullptr; }
const void* ChainedTable(int i) noexcept { return i>=0 && i<chained.load(std::memory_order_acquire) ? chainedTable[i] : nullptr; }

bool IsChained(const void* table) noexcept {
    const int n=chained.load(std::memory_order_acquire);
    for(int i=0;i<n;++i)if(chainedTable[i]==table)return true;
    return false;
}
// Chain the receive slot of the NetworkObject table `table` (once a table).
bool Chain(void** table,const char* what) noexcept {
    AcquireSRWLockExclusive(&chainLock);
    bool ok=IsChained(table);
    const int n=chained.load(std::memory_order_relaxed);
    if(!ok && n<kMostClasses) {
        ok=edf::ChainVtableSlot(table+object_net::kReceiveSlot,reinterpret_cast<void*>(hooks.at[n]),&nextReceive[n]);
        if(ok){chainedTable[n]=table;chained.store(n+1,std::memory_order_release);}
        Log("OBJECT NET %s: NetworkObject EDF+%llX slot 17 chained=%d (entry %d)",what,
            static_cast<unsigned long long>(reinterpret_cast<unsigned char*>(table)-image),ok,n);
    } else if(!ok) {
        Log("OBJECT NET %s: NetworkObject EDF+%llX not chained: %d classes already are",what,
            static_cast<unsigned long long>(reinterpret_cast<unsigned char*>(table)-image),kMostClasses);
    }
    ReleaseSRWLockExclusive(&chainLock);
    return ok;
}
}  // namespace object_transport

namespace object_net {
using namespace object_transport;
bool Install() noexcept {
    if(layerTried)return layerReady;
    layerTried=true;
    for(const auto& s:signatures)if(!Matches(s.rva,s.bytes,sizeof(s.bytes))) {
        Log("OBJECT NET off: EDF+%X does not match the verified transport",s.rva);return false;
    }
    unsigned int lo=0,hi=0;
    if(rand_s(&lo) || rand_s(&hi)){Log("OBJECT NET off: cannot create a sender identity");return false;}
    senderId=(static_cast<std::uint64_t>(hi)<<32 | lo) | 1;
    layerReady=true;
    Log("OBJECT NET on: tag %d, sender %016llX",kTag,static_cast<unsigned long long>(senderId));
    return true;
}
bool Ready() noexcept { return layerReady; }
bool Register(std::uint32_t magic,Handler fn) noexcept {
    const bool ok=kinds.Add(magic,fn);
    if(!ok)Log("OBJECT NET kind %08X refused: %s",magic,
               !magic || !fn ? "no magic or receiver" : kinds.Find(magic) ? "its magic is another receiver's" : "the registry is full");
    return ok;
}
bool ChainClass(Class c) noexcept {
    const ClassSlot* s=ClassOf(c);
    if(!layerReady || !s)return false;
    auto table=reinterpret_cast<void**>(image+s->table);
    if(!Readable(table,(kReceiveSlot+1)*8)){Log("OBJECT NET %s: its NetworkObject vtable is not readable",s->name);return false;}
    if(IsChained(table))return true;
    if(table[kSendSlot]!=image+kStockSend)
        Log("OBJECT NET %s: slot 16 is %p, not EDF+%X: sent through as it is",s->name,table[kSendSlot],kStockSend);
    if(table[kReceiveSlot]!=image+s->stockReceive)
        Log("OBJECT NET %s: slot 17 is %p, not EDF+%X (another module's hook): chained on top of it",s->name,
            table[kReceiveSlot],s->stockReceive);
    return Chain(table,s->name);
}
bool ChainVehicle(unsigned char* v) noexcept {
    if(!layerReady || !v || !Readable(v+kNet,8))return false;
    auto table=At<void**>(v,kNet);
    if(!table || !Readable(table,(kReceiveSlot+1)*8))return false;
    if(IsChained(table))return true;
    // Only a class whose send and receive are the stock NetworkObject's (read: 0x773DA0 / 0x6325B0).
    if(table[kSendSlot]!=image+kStockSend || table[kReceiveSlot]!=image+kVehicleReceive)return false;
    return Chain(table,"vehicle class");
}
std::int32_t Reference(const void* object) noexcept {
    if(!layerReady || !object || !Readable(object,kSelfCtrl+8))return -1;
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
    if(!layerReady || !block || size<sizeof(Head))return false;
    Head head;std::memcpy(&head,block,sizeof(head));
    std::uint32_t next=sequenceCounter.fetch_add(1,std::memory_order_relaxed)+1;
    if(!next)next=sequenceCounter.fetch_add(1,std::memory_order_relaxed)+1;   // 0 is never sent
    head.sender=senderId;head.sequence=next;
    std::memcpy(block,&head,sizeof(head));
    return true;
}
namespace {
bool Write(Stream& stream,const void* block,std::size_t size) noexcept {
    return Fn<bool(__fastcall*)(void*,std::int8_t)>(kWriteSmall)(stream.bytes,kTag) &&
        Fn<bool(__fastcall*)(void*,const void*,std::size_t)>(kWriteBlock)(stream.bytes,block,size);
}
}  // namespace
bool Send(unsigned char* object,const void* block,std::size_t size) noexcept {
    if(!layerReady || !object || !block || size<sizeof(Head) || size>kMostBlock || !Replicated(InSession(),At<std::uint16_t>(object,0x128)))return false;
    Stream stream;
    if(!Write(stream,block,size))return false;
    void* net=object+kNet;
    return reinterpret_cast<SendFn>((*reinterpret_cast<void***>(net))[kSendSlot])(net,stream.bytes);
}
bool Loopback(unsigned char* to,const void* block,std::size_t size) noexcept {
    if(!layerReady || !to || !block || size<sizeof(Head) || size>kMostBlock)return false;
    Stream stream;
    if(!Write(stream,block,size))return false;
    Reader reader(stream);
    unsigned char* net=to+kNet;
    reinterpret_cast<ReceiveFn>((*reinterpret_cast<void***>(net))[kReceiveSlot])(net,reader.bytes);
    return true;
}
}  // namespace object_net
}  // namespace crew
