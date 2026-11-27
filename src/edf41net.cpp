#define _CRT_RAND_S   // object_net.cpp (rand_s) may share this file's translation unit (the native tests)
// EDF4.1 classes online: the shared network layer (src/edf41net.h has what it is for; docs/edf41-classes.md section 联机 the
// reading of EDF.dll behind it). EDF.dll TimeDateStamp 0x678CCB46, RVAs; every function checked by its first bytes before use.
//
// The events are one kind (magic E41N) of the plugin's object messages (object_net.h / object_net.cpp, the transport the
// drill's flight and the seats' picks use: the game's stream writer, the object's NetworkObject slot 16 to send, its slot 17
// to receive). object_net chains the EventAnimationObject and UfoCarrier508 NetworkObject slot 17 (its class table; both are
// GameObjectBase's 0x54D770 in the stock tables) and hands this layer's blocks to Received by their magic; every other tag or
// kind goes on to the receiver the slot held. Received checks the block and queues it for the object's next update.
// The child identity is MovingFortress's (0x4C79C0): 0x54EDB0(child) when the parent is another machine's, then
// 0x781630(manager [0x20B2AC8], weak child*, weak parent*, int index, int sub, byte remoteOwned) — both weak arguments are
// released by the callee (0x781878 / 0x781894), so each is a copy holding one weak count of its own.
#include "edf41net.h"

#include "objref.h"
#include "edf_layout.h"
#include "config.h"
#include "plugin_log.h"
#include "netprobe.h"
#include "payload.h"
#include "npcai.h"
#include "support_call.h"
#include "transport.h"
#include "edf41classes.h"
#include "memory.h"
#include "object_net_transport.h"
#include "online_authority.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace crew::edf41net {
namespace {
constexpr unsigned kRegisterChild=0x781630,kGuestChild=0x54EDB0;
constexpr std::size_t kNetManager=0x20B2AC8;
constexpr std::size_t kNetWord=0x128,kTargetSync=0x591,kSelfControl=0x30;
constexpr int kMostQueued=64,kMostLoops=4;

struct Signature { unsigned rva; unsigned char bytes[8]; };
// The stream, the send and the receive are object_net's (object_transport::signatures); these are the child identity's.
const Signature kSignatures[]={
    {kRegisterChild,{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41}},
    {kGuestChild,{0xC7,0x81,0x38,0x03,0x00,0x00,0xFF,0xFF}},
};

template<class F> F Fn(unsigned rva) noexcept { return reinterpret_cast<F>(image+rva); }
using RegisterChildFn=void*(__fastcall*)(void*,void*,void*,int,int,unsigned char);
using GuestChildFn=void(__fastcall*)(void*);

std::mutex lock;
std::atomic<int> state{0};   // 0 not tried, 1 ready, -1 off
Stats stats;

// Per object: its control block (a new object at the same address is not the old one), the sender watermarks, the queue.
struct Message { std::uint8_t kind; std::uint8_t size; unsigned char payload[kMostPayload]; };
struct Record { const void* control=nullptr; Gate gate; std::vector<Message> queue; };
std::unordered_map<const void*,Record> records;
struct Pending { WeakRef parent,child; std::uint16_t index,sub; bool sync; };
std::vector<Pending> pending;
struct Pair { WeakRef from,to; };
std::vector<Pair> loops;
struct Key { const void* control; std::uint32_t id; };
std::vector<Key> taken;      // (the parent's control block, index << 16 | sub) already given: never twice

const void* ControlOf(const void* object) noexcept {
    const char* o=static_cast<const char*>(object);
    return object && Readable(o+kSelfControl,8) ? *reinterpret_cast<const void* const*>(o+kSelfControl) : nullptr;
}
bool Same(const WeakRef& w,const void* object) noexcept { return w.obj==object && w.ctrl && w.ctrl==ControlOf(object); }

void Say(const char* what) noexcept {
    static std::mutex sayLock;
    static const char* said[24];
    static int count=0;
    std::lock_guard<std::mutex> hold(sayLock);
    for(int i=0;i<count;++i)if(said[i]==what)return;
    if(count<24)said[count++]=what;
    Log("EDF41NET %s",what);
}

void Received(unsigned char* object,const void* block,std::size_t size) noexcept;

bool InstallOnce() noexcept {
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
    if(!object_net::Install() || !object_net::Register(kMagic,&Received) ||
       !object_net::ChainClass(object_net::Class::eventAnimationObject) || !object_net::ChainClass(object_net::Class::ufoCarrier508)) {
        Log("EDF41NET off: the object message layer is not on, or the classes' NetworkObject slot 17 is not chained");
        state=-1;
        return false;
    }
    Log("EDF41NET on: kind E41N of the object messages (tag %d) on the EventAnimationObject / UfoCarrier508 NetworkObject%s",kTag,
        Cfg().edf41NetMirror ? " (development loopback Edf41NetMirror on: offline only)" : "");
    state=1;
    return true;
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
        Say("an E41N event that does not decode (version, size, reserved bytes): dropped");
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
            if(!r.gate.AdmitSequence(h.head.sender,h.head.sequence))++stats.refused;
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

// object_net's receiver of kind E41N: `object` the copy here the message came for (EventAnimationObject or UfoCarrier508).
void Received(unsigned char* object,const void* block,std::size_t size) noexcept {
    Enqueue(object,static_cast<const unsigned char*>(block),size);
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
        objref::Release(At<const void*>(weakChild,8));   // WeakOut's count for the child, not handed over
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
            if(!objref::Exists(p.parent) || !objref::Exists(p.child)) {
                p.parent.Reset();
                p.child.Reset();
                it=pending.erase(it);
                continue;
            }
            if(p.parent.obj==parent && Registered(p.parent.Obj())) {
                ready.push_back(p);
                it=pending.erase(it);
                continue;
            }
            ++it;
        }
        stats.waiting=static_cast<std::uint32_t>(pending.size());
    }
    for(Pending& p:ready) {
        RegisterNow(p.parent.Obj(),p.child.Obj(),p.index,p.sub,p.sync);
        p.parent.Reset();
        p.child.Reset();
    }
}
}  // namespace

bool Enabled() noexcept { return Cfg().edf41Net && InstallOnce(); }
bool Install() noexcept { return Enabled(); }
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

bool Send(void* object,std::uint8_t kind,const void* payload,std::size_t size) noexcept {
    if(!object || !kind || size>kMostPayload || !Enabled())return false;
    void* to[kMostLoops]{};
    int toCount=0;
    {
        std::lock_guard<std::mutex> hold(lock);
        for(const Pair& p:loops)if(toCount<kMostLoops && Same(p.from,object) && objref::Exists(p.to))to[toCount++]=p.to.Obj();
    }
    const bool online=Online() && Registered(object) && !Guest(object);
    if(!online && !toCount)return false;
    Header h;
    h.kind=kind;
    unsigned char message[kMostMessage];
    std::size_t n=0;
    if(!object_net::Stamp(&h,sizeof(h)) || !Encode(h,payload,size,message,sizeof(message),&n))return false;
    bool sent=online && object_net::Send(static_cast<unsigned char*>(object),message,n);
    for(int i=0;i<toCount;++i)sent=object_net::Loopback(static_cast<unsigned char*>(to[i]),message,n) || sent;
    std::lock_guard<std::mutex> hold(lock);
    stats.looped+=static_cast<std::uint32_t>(toCount);
    if(sent)++stats.sent;
    return sent;
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
    p.parent=ObjRef::Of(parent);
    p.child=ObjRef::Of(child);
    if(!p.parent.ctrl || !p.child.ctrl) { p.parent.Reset(); p.child.Reset(); return; }
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
        p.from=ObjRef::Of(from);
        p.to=ObjRef::Of(to);
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
