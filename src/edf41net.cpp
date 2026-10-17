// EDF4.1 classes online: the shared network layer (src/edf41net.h has what it is for; docs/edf41-classes.md section 联机 the
// reading of EDF.dll behind it). EDF.dll TimeDateStamp 0x678CCB46, RVAs; every function checked by its first bytes before use.
//
// The transport is the one the plugin's drill uses (drill_net.cpp, its native test tests/drill_net_native_test.cpp): the game's
// stream writer (0x79A460 construct, 0x12B5790 small int, 0x12B5200 block), the object's NetworkObject slot 16 (+0x80,
// 0x773DA0) to send, slot 17 to receive (the reader: 0x12B4660 small int, 0x12B4900 block, 0x12B48A0 / 0x12B4FD0 mark and
// rewind). The classes' objects get a copy of their stock NetworkObject vtable (EventAnimationObject 0x17D4E90, UfoCarrier508
// 0x17C9F50, both slot 17 = GameObjectBase's 0x54D770) with slot 17 wrapped: tag 12 is read here and queued for the object, any
// other tag goes on to the slot the live table had, the read position put back first.
// The child identity is MovingFortress's (0x4C79C0): 0x54EDB0(child) when the parent is another machine's, then
// 0x781630(manager [0x20B2AC8], weak child*, weak parent*, int index, int sub, byte remoteOwned) — both weak arguments are
// released by the callee (0x781878 / 0x781894), so each is a copy holding one weak count of its own.
#define _CRT_RAND_S
#include "edf41net.h"

#include "crew.h"
#include "edf41classes.h"
#include "memory.h"
#include "online_authority.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace crew::edf41net {
namespace {
constexpr unsigned kConstruct=0x79A460,kDestroy=0x760180,kWriteSmall=0x12B5790,kWriteBlock=0x12B5200;
constexpr unsigned kReadValue=0x12B4660,kReadBlock=0x12B4900,kMark=0x12B48A0,kRewind=0x12B4FD0,kReaderFrom=0x12B4530;
constexpr unsigned kRegisterChild=0x781630,kGuestChild=0x54EDB0,kStockReceive=0x54D770,kStockSend=0x773DA0;
constexpr unsigned kEaoNet=0x17D4E90,kCarrierNet=0x17C9F50;
constexpr std::size_t kNetManager=0x20B2AC8;
constexpr std::size_t kNet=0x120,kNetWord=0x128,kTargetSync=0x591,kSelfControl=0x30;
constexpr int kNetSlots=22,kSlotSend=16,kSlotReceive=17;
constexpr std::size_t kStreamData=0x10,kStreamSize=0x5F0;
constexpr int kMostQueued=64,kMostLoops=4;

struct Signature { unsigned rva; unsigned char bytes[8]; };
const Signature kSignatures[]={
    {kConstruct,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B}},
    {kDestroy,{0x48,0x8D,0x05,0x01,0xCE,0x08,0x01,0x48}},
    {kWriteSmall,{0x8D,0x42,0x0E,0x4C,0x8D,0x41,0x10,0x3C}},
    {kWriteBlock,{0x48,0x8B,0x81,0xF0,0x05,0x00,0x00,0x4D}},
    {kReadValue,{0x48,0x89,0x5C,0x24,0x08,0x4C,0x8B,0x51}},
    {kReadBlock,{0x48,0x89,0x5C,0x24,0x18,0x56,0x57,0x41}},
    {kMark,{0x48,0x8B,0x41,0x08,0x41,0xB8,0xFF,0xFF}},
    {kRewind,{0xB8,0xFF,0xFF,0xFF,0xFF,0x48,0x39,0x41}},
    {kReaderFrom,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8D}},
    {kRegisterChild,{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41}},
    {kGuestChild,{0xC7,0x81,0x38,0x03,0x00,0x00,0xFF,0xFF}},
    {kStockSend,{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57}},
};

template<class F> F Fn(unsigned rva) noexcept { return reinterpret_cast<F>(image+rva); }
using ReceiveFn=void(__fastcall*)(unsigned char*,void*);
using SendFn=bool(__fastcall*)(void*,void*);
using RegisterChildFn=void*(__fastcall*)(void*,void*,void*,int,int,unsigned char);
using GuestChildFn=void(__fastcall*)(void*);

// The game's stream object (0x5F8 bytes: +8 the read position, +0x10 the bytes, +0x5F0 the size); its destructor only resets
// vtables (0x760180 -> 0x12B45A0), nothing is allocated. A plain buffer: the functions with __try hold no object to unwind.
struct RawStream { alignas(16) unsigned char bytes[0x600]; };
void Construct(RawStream& s) noexcept { std::memset(s.bytes,0,sizeof(s.bytes)); Fn<void*(__fastcall*)(void*,int)>(kConstruct)(s.bytes,0x40); }
void Destroy(RawStream& s) noexcept { Fn<void(__fastcall*)(void*)>(kDestroy)(s.bytes); }

// The classes' NetworkObject vtables: the 22 slots of the live table copied (RTTI first), slot 17 ours.
struct NetTable { unsigned rva; const char* name; void* block[kNetSlots+1]; void* next; };
NetTable tables[2]={{kEaoNet,"EventAnimationObject",{},nullptr},{kCarrierNet,"UfoCarrier508",{},nullptr}};

std::mutex lock;
std::atomic<int> state{0};   // 0 not tried, 1 ready, -1 off
std::uint64_t sender=0;
std::uint32_t sequence=0;
Stats stats;

// Per object: its control block (a new object at the same address is not the old one), the sender watermarks, the queue.
struct Message { std::uint8_t kind; std::uint8_t size; unsigned char payload[kMostPayload]; };
struct Record { const void* control=nullptr; Gate gate; std::vector<Message> queue; };
std::unordered_map<const void*,Record> records;
struct Pending { edf41c::WeakRef parent,child; std::uint16_t index,sub; bool sync; };
std::vector<Pending> pending;
struct Pair { edf41c::WeakRef from,to; };
std::vector<Pair> loops;
struct Key { const void* control; std::uint32_t id; };
std::vector<Key> taken;      // (the parent's control block, index << 16 | sub) already given: never twice

const void* ControlOf(const void* object) noexcept {
    const char* o=static_cast<const char*>(object);
    return object && Readable(o+kSelfControl,8) ? *reinterpret_cast<const void* const*>(o+kSelfControl) : nullptr;
}
bool Same(const edf41c::WeakRef& w,const void* object) noexcept { return w.object==object && w.control && w.control==ControlOf(object); }

void Say(const char* what) noexcept {
    static std::mutex sayLock;
    static const char* said[24];
    static int count=0;
    std::lock_guard<std::mutex> hold(sayLock);
    for(int i=0;i<count;++i)if(said[i]==what)return;
    if(count<24)said[count++]=what;
    Log("EDF41NET %s",what);
}

void __fastcall Receive(unsigned char* net,void* reader);

bool Install() noexcept {
    int s=state.load();
    if(s)return s>0;
    std::lock_guard<std::mutex> hold(lock);
    if((s=state.load())!=0)return s>0;
    for(const Signature& g:kSignatures)
        if(!Matches(g.rva,g.bytes,sizeof(g.bytes))) {
            Log("EDF41NET off: EDF+%X is not the code this was written for (docs/edf41-classes.md 联机)",g.rva);
            state=-1;
            return false;
        }
    // The stock receiver 0x54D770 is not fingerprinted: EDF6Coop hooks its first instruction (W3 / W4, `mov [rsp+0x10],rbx` ->
    // jmp; found on the real game 2026-10-15, where a fingerprint turned this layer off). It is only called on, never read.
    static const unsigned char kReceiveEntry[]={0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83};
    if(!Matches(kStockReceive,kReceiveEntry,sizeof(kReceiveEntry)))
        Log("EDF41NET GameObjectBase's NetworkObject receiver EDF+%X starts with other bytes (another module's hook, EDF6Coop's W3 / W4): called on as it is",kStockReceive);
    unsigned lo=0,hi=0;
    if(rand_s(&lo) || rand_s(&hi)) { Log("EDF41NET off: no sender identity"); state=-1; return false; }
    sender=(static_cast<std::uint64_t>(hi)<<32 | lo) | 1;
    for(NetTable& t:tables) {
        void* const* vt=reinterpret_cast<void* const*>(image+t.rva);
        if(!Readable(vt-1,sizeof(void*)*(kNetSlots+1))) { Log("EDF41NET off: the %s NetworkObject vtable is not readable",t.name); state=-1; return false; }
        for(int i=-1;i<kNetSlots;++i)t.block[i+1]=vt[i];
        if(vt[kSlotSend]!=image+kStockSend)Log("EDF41NET %s NetworkObject slot 16 is %p, not EDF+%X: kept as it is",t.name,vt[kSlotSend],kStockSend);
        if(vt[kSlotReceive]!=image+kStockReceive)Log("EDF41NET %s NetworkObject slot 17 is %p (patched): this layer calls it on for other tags",t.name,vt[kSlotReceive]);
        t.next=vt[kSlotReceive];
        t.block[kSlotReceive+1]=reinterpret_cast<void*>(&Receive);
    }
    Log("EDF41NET on: tag %d on copies of the EventAnimationObject / UfoCarrier508 NetworkObject vtables, sender %016llX%s",kTag,
        static_cast<unsigned long long>(sender),Cfg().edf41NetMirror ? " (development loopback Edf41NetMirror on: offline only)" : "");
    state=1;
    return true;
}

NetTable* TableOf(const unsigned char* net) noexcept {
    if(!Readable(net,8))return nullptr;
    void* const* vt=*reinterpret_cast<void* const* const*>(net);
    for(NetTable& t:tables)if(vt==&t.block[1])return &t;
    return nullptr;
}

bool MirrorLocked(const void* object) noexcept {
    for(const Pair& p:loops)if(Same(p.to,object))return true;
    return false;
}

// A message for `object` (its copy here): checked, then queued for the class's next update. Guests only.
void Enqueue(void* object,const unsigned char* data,std::size_t n) noexcept {
    Header h;
    const unsigned char* payload=nullptr;
    if(!Decode(data,n,&h,&payload)) {
        { std::lock_guard<std::mutex> hold(lock); ++stats.refused; }
        Say("a tag-12 event that is not this layer's (bad magic, version or size): dropped");
        return;
    }
    const bool guest=Guest(object);
    bool full=false;
    {
        std::lock_guard<std::mutex> hold(lock);
        if(!guest) { ++stats.refused; }
        else {
            const void* control=ControlOf(object);
            Record& r=records[object];
            if(r.control!=control) { r=Record{}; r.control=control; }
            if(!r.gate.Admit(h.sender,h.sequence))++stats.refused;
            else {
                if(static_cast<int>(r.queue.size())>=kMostQueued) { r.queue.erase(r.queue.begin()); full=true; }
                Message m{};
                m.kind=h.kind;
                m.size=h.size;
                std::memcpy(m.payload,payload,h.size);
                r.queue.push_back(m);
                ++stats.received;
            }
        }
    }
    if(!guest)Say("an event for an object this machine decides for: dropped");
    if(full)Say("an object's event queue overflowed (its update did not drain it): the oldest dropped");
}

// Tag 12: 1, consumed; another tag: 0; -1 the reader raised an exception. The read position is put back in each case (as the
// drill's Peek does; GameObjectBase's own receiver reads one small int and returns on a tag it does not know).
int Peek(unsigned char* net,void* reader,RawStream& block) noexcept {
    __try {
        std::int64_t mark=0;
        Fn<void(__fastcall*)(void*,std::int64_t*)>(kMark)(reader,&mark);
        const bool ours=Fn<std::int64_t(__fastcall*)(void*)>(kReadValue)(reader)==kTag;
        if(ours) {
            Construct(block);
            const bool read=Fn<bool(__fastcall*)(void*,void*)>(kReadBlock)(reader,block.bytes);
            const std::size_t n=read ? At<std::size_t>(block.bytes,kStreamSize) : 0;
            if(read && n<=kMostMessage)Enqueue(net-kNet,block.bytes+kStreamData,n);
            else Say("a tag-12 event without a readable block of a size this layer sends: dropped");
            Destroy(block);
        }
        Fn<void(__fastcall*)(void*,std::int64_t)>(kRewind)(reader,mark);
        return ours ? 1 : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void __fastcall Receive(unsigned char* net,void* reader) {
    NetTable* t=TableOf(net);
    if(reader && t) {
        RawStream block;
        const int r=Peek(net,reader,block);
        if(r>0)return;
        if(r<0)Say("reading an event raised an exception: passed on to the stock receiver");
    }
    void* next=t ? t->next : static_cast<void*>(image+kStockReceive);
    reinterpret_cast<ReceiveFn>(next)(net,reader);
}

// 0x781630 under __try: 1 done, 0 not, -1 an exception.
int CallRegister(void* manager,void* child,unsigned char* weakChild,unsigned char* weakParent,std::uint16_t index,std::uint16_t sub,
                 unsigned char remote) noexcept {
    __try {
        if(remote)Fn<GuestChildFn>(kGuestChild)(child);
        Fn<RegisterChildFn>(kRegisterChild)(manager,weakChild,weakParent,index,sub,remote);
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Called with `lock` NOT held (the game's code runs inside).
bool RegisterNow(void* parent,void* child,std::uint16_t index,std::uint16_t sub,bool sync) noexcept {
    if(Registered(child))return true;
    const void* pc=ControlOf(parent);
    const std::uint32_t id=static_cast<std::uint32_t>(index)<<16 | sub;
    bool twice=false;
    {
        std::lock_guard<std::mutex> hold(lock);
        for(const Key& k:taken)if(k.control==pc && k.id==id)twice=true;
        if(twice)++stats.childFailed;
    }
    if(twice) {
        Log("EDF41NET child identity %04X:%04X under %p asked twice: the second child %p stays without one",index,sub,parent,child);
        return false;
    }
    void* manager=Readable(image+kNetManager,8) ? *reinterpret_cast<void**>(image+kNetManager) : nullptr;
    if(!manager) {
        { std::lock_guard<std::mutex> hold(lock); ++stats.childFailed; }
        Say("no network manager ([EDF+0x20B2AC8] is null) while online: children stay without an identity");
        return false;
    }
    const unsigned char remote=static_cast<unsigned char>(At<std::uint16_t>(parent,kNetWord)&1);
    alignas(16) unsigned char weakChild[16]{},weakParent[16]{};
    if(!edf41c::WeakOut(child,weakChild))return false;
    if(!edf41c::WeakOut(parent,weakParent)) {
        edf41c::WeakRef w;
        std::memcpy(&w.object,weakChild,8);
        std::memcpy(&w.control,weakChild+8,8);
        edf41c::WeakReset(w);
        return false;
    }
    const int r=CallRegister(manager,child,weakChild,weakParent,index,sub,remote);
    if(r<0)Say("registering a child raised an exception: it stays without an identity");
    const bool ok=r>0 && Registered(child);
    if(ok && sync && Readable(static_cast<char*>(child)+kTargetSync,1))static_cast<char*>(child)[kTargetSync]=1;
    std::lock_guard<std::mutex> hold(lock);
    if(!ok) { ++stats.childFailed; return false; }
    if(taken.size()>8192)taken.erase(taken.begin(),taken.begin()+4096);
    taken.push_back(Key{pc,id});
    ++stats.children;
    return true;
}

// The children of `parent` that can be registered now (it has its own identity) are taken out of the queue (dead entries go),
// then registered with `lock` released.
void PumpFor(const void* parent) noexcept {
    std::vector<Pending> ready;
    {
        std::lock_guard<std::mutex> hold(lock);
        for(auto it=pending.begin();it!=pending.end();) {
            Pending& p=*it;
            if(!edf41c::WeakAlive(p.parent) || !edf41c::WeakAlive(p.child)) {
                edf41c::WeakReset(p.parent);
                edf41c::WeakReset(p.child);
                it=pending.erase(it);
                continue;
            }
            if(p.parent.object==parent && Registered(p.parent.object)) {
                ready.push_back(p);
                it=pending.erase(it);
                continue;
            }
            ++it;
        }
        stats.waiting=static_cast<std::uint32_t>(pending.size());
    }
    for(Pending& p:ready) {
        RegisterNow(p.parent.object,p.child.object,p.index,p.sub,p.sync);
        edf41c::WeakReset(p.parent);
        edf41c::WeakReset(p.child);
    }
}

// The stream the game writes (tag, block), sent on `object`'s NetworkObject when `online`, and read back by the game's reader
// into each of `to`'s receivers (the development loopback). 1 sent, 0 not, -1 an exception.
int Transmit(void* object,bool online,void* const* to,int toCount,const unsigned char* message,std::size_t n) noexcept {
    __try {
        RawStream stream;
        Construct(stream);
        int sent=0;
        if(Fn<bool(__fastcall*)(void*,std::int8_t)>(kWriteSmall)(stream.bytes,kTag) &&
           Fn<bool(__fastcall*)(void*,const void*,std::size_t)>(kWriteBlock)(stream.bytes,message,n)) {
            if(online) {
                unsigned char* net=static_cast<unsigned char*>(object)+kNet;
                void* const* vt=*reinterpret_cast<void* const* const*>(net);
                sent=reinterpret_cast<SendFn>(vt[kSlotSend])(net,stream.bytes) ? 1 : 0;
            }
            for(int i=0;i<toCount;++i) {
                RawStream reader;
                Fn<void(__fastcall*)(void*,const void*,std::size_t)>(kReaderFrom)(reader.bytes,stream.bytes+kStreamData,At<std::size_t>(stream.bytes,kStreamSize));
                unsigned char* net=static_cast<unsigned char*>(to[i])+kNet;
                void* const* vt=*reinterpret_cast<void* const* const*>(net);
                reinterpret_cast<ReceiveFn>(vt[kSlotReceive])(net,reader.bytes);
                Destroy(reader);
                sent=1;
            }
        }
        Destroy(stream);
        return sent;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
}  // namespace

bool Enabled() noexcept { return Cfg().edf41Net && Install(); }
bool Online() noexcept { return InSession(); }

bool Registered(const void* object) noexcept {
    const char* o=static_cast<const char*>(object);
    return object && Readable(o+kNetWord,2) && (At<std::uint16_t>(o,kNetWord)&3)!=0;
}

bool Guest(const void* object) noexcept {
    if(!object || !Enabled())return false;
    {
        std::lock_guard<std::mutex> hold(lock);
        if(MirrorLocked(object))return true;
    }
    if(!Online() || !Registered(object))return false;
    return !OnlineRunsHere(object);
}

void Attach(void* object) noexcept {
    if(!object || !Enabled())return;
    unsigned char* net=static_cast<unsigned char*>(object)+kNet;
    if(!Readable(net,8))return;
    void* const* vt=*reinterpret_cast<void* const* const*>(net);
    for(NetTable& t:tables)
        if(vt==reinterpret_cast<void* const*>(image+t.rva)) { *reinterpret_cast<void**>(net)=&t.block[1]; return; }
    for(NetTable& t:tables)if(vt==&t.block[1])return;
    Say("an object whose NetworkObject vtable is neither EventAnimationObject's nor UfoCarrier508's: left as it is");
}

bool Send(void* object,std::uint8_t kind,const void* payload,std::size_t size) noexcept {
    if(!object || !kind || size>kMostPayload || !Enabled())return false;
    void* to[kMostLoops]{};
    int toCount=0;
    Header h;
    {
        std::lock_guard<std::mutex> hold(lock);
        for(const Pair& p:loops)if(toCount<kMostLoops && Same(p.from,object) && edf41c::WeakAlive(p.to))to[toCount++]=p.to.object;
    }
    const bool online=Online() && Registered(object) && !Guest(object);
    if(!online && !toCount)return false;
    {
        std::lock_guard<std::mutex> hold(lock);
        if(++sequence==0)++sequence;
        h.sequence=sequence;
        h.sender=sender;
    }
    h.kind=kind;
    unsigned char message[kMostMessage];
    std::size_t n=0;
    if(!Encode(h,payload,size,message,sizeof(message),&n))return false;
    const int r=Transmit(object,online,to,toCount,message,n);
    if(r<0) { Say("sending an event raised an exception: not sent"); return false; }
    std::lock_guard<std::mutex> hold(lock);
    stats.looped+=static_cast<std::uint32_t>(toCount);
    if(r>0)++stats.sent;
    return r>0;
}

int Drain(const void* object,Visit visit,void* context) noexcept {
    if(!object || !visit || !Enabled())return 0;
    std::vector<Message> got;
    {
        std::lock_guard<std::mutex> hold(lock);
        const auto it=records.find(object);
        if(it==records.end())return 0;
        if(it->second.control!=ControlOf(object)) { records.erase(it); return 0; }
        got.swap(it->second.queue);
        if(records.size()>512)
            for(auto r=records.begin();r!=records.end();)r=r->second.queue.empty() && r->first!=object ? records.erase(r) : std::next(r);
    }
    for(const Message& m:got)visit(context,m.kind,m.payload,m.size);
    return static_cast<int>(got.size());
}

void Child(void* parent,void* child,std::uint16_t index,std::uint16_t sub,bool targetSync) noexcept {
    if(!parent || !child || !Enabled() || !Online())return;
    Pending p{{},{},index,sub,targetSync};
    edf41c::WeakSet(p.parent,parent);
    edf41c::WeakSet(p.child,child);
    if(!p.parent.control || !p.child.control) { edf41c::WeakReset(p.parent); edf41c::WeakReset(p.child); return; }
    {
        std::lock_guard<std::mutex> hold(lock);
        pending.push_back(p);
    }
    PumpFor(parent);
}

void Pump(void* parent) noexcept {
    if(!parent || !Enabled())return;
    {
        std::lock_guard<std::mutex> hold(lock);
        if(pending.empty())return;
    }
    PumpFor(parent);
}

bool Mirror() noexcept { return Cfg().edf41NetMirror && Enabled() && !Online(); }

void Loopback(void* from,void* to) noexcept {
    if(!from || !to || !Mirror())return;
    {
        std::lock_guard<std::mutex> hold(lock);
        Pair p;
        edf41c::WeakSet(p.from,from);
        edf41c::WeakSet(p.to,to);
        loops.push_back(p);
    }
    Log("EDF41NET development loopback: %p's events go through the game's stream writer and reader to %p, now a guest copy (ini Edf41NetMirror)",from,to);
}

bool IsMirror(const void* object) noexcept {
    if(!object)return false;
    std::lock_guard<std::mutex> hold(lock);
    return MirrorLocked(object);
}

Stats Counters() noexcept {
    std::lock_guard<std::mutex> hold(lock);
    return stats;
}
}  // namespace crew::edf41net
