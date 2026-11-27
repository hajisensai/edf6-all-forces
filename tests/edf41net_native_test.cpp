// Optional read-only EDF.dll validation of the EDF4.1 classes' network layer (src/edf41net.cpp, one kind of the object messages
// of src/object_net.cpp), as tests/drill_net_native_test.cpp does for the drill: EDF.dll mapped WITHOUT its entry point, only its
// CRT imports resolved, and the production code run on stand-in objects with the game's own stream writer / reader, its stock
// receiver 0x54D770 and its vtables (the object message layer chains the mapped image's EventAnimationObject / UfoCarrier508
// slot 17). The game's send (0x773DA0, which needs a live room) is the one stand-in on the path: it hands the bytes the game's
// writer wrote to the game's reader, into the other copy's receiver. Never attaches to the game, writes no file. Run:
// edf41net_native_test <EDF.dll>.
#include "../src/edf41net.cpp"
#include "../src/object_net.cpp"
#include "../src/seat_net.h"
#include "edf/host.h"

#include <cstdarg>
#include <cstdio>
#include <initializer_list>

namespace crew {
unsigned char* image=nullptr;
Config config{};
bool session=true;
const Config& Cfg() noexcept { return config; }
void Log(const char* f,...) noexcept {
    va_list a;
    va_start(a,f);
    std::vprintf(f,a);
    std::puts("");
    va_end(a);
}
bool InSession() noexcept { return session; }
// online_authority's answer for a registered object that is not a vehicle: another machine's when bit 0 is set (online::RunsHere)
bool OnlineRunsHere(const void* object) noexcept { return !session || (At<std::uint16_t>(object,0x128)&1)==0; }
namespace edf41c {
void WeakReset(WeakRef& w) noexcept {
    if(w.control)reinterpret_cast<std::atomic<std::int32_t>*>(static_cast<char*>(w.control)+0xC)->fetch_sub(1);
    w=WeakRef{};
}
void WeakSet(WeakRef& w,const void* object) noexcept {
    WeakReset(w);
    if(!object)return;
    w.object=At<void*>(object,0x28);
    w.control=At<void*>(object,0x30);
    if(w.control)reinterpret_cast<std::atomic<std::int32_t>*>(static_cast<char*>(w.control)+0xC)->fetch_add(1);
}
bool WeakAlive(const WeakRef& w) noexcept { return w.control && At<std::int32_t>(w.control,8)>0; }
bool WeakOut(const void* object,void* out) noexcept {
    WeakRef w;
    WeakSet(w,object);
    if(!w.control)return false;
    std::memcpy(out,&w.object,8);
    std::memcpy(static_cast<char*>(out)+8,&w.control,8);
    return true;
}
}  // namespace edf41c
}  // namespace crew

namespace {
namespace transport=crew::object_transport;
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",what); std::exit(1); }
}
bool RuntimeImports(HMODULE module) {
    auto* base=reinterpret_cast<unsigned char*>(module);
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    const auto& dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for(auto d=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+dir.VirtualAddress);d->Name;++d) {
        const char* dll=reinterpret_cast<const char*>(base+d->Name);
        if(_strnicmp(dll,"VCRUNTIME",9) && _strnicmp(dll,"api-ms-win-crt-",15))continue;
        HMODULE runtime=LoadLibraryA(dll);
        if(!runtime)return false;
        auto names=reinterpret_cast<const IMAGE_THUNK_DATA64*>(base+d->OriginalFirstThunk);
        auto slots=reinterpret_cast<std::uint64_t*>(base+d->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots) {
            if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;
            auto name=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
            FARPROC proc=GetProcAddress(runtime,reinterpret_cast<const char*>(name->Name));
            if(!proc)continue;
            DWORD old=0;
            if(!VirtualProtect(slots,8,PAGE_READWRITE,&old))return false;
            *slots=reinterpret_cast<std::uint64_t>(proc);
            VirtualProtect(slots,8,old,&old);
        }
    }
    return true;
}
// A vtable slot of the mapped image (.rdata) rewritten in this process's private copy.
bool PatchSlot(unsigned rva,void* to) {
    void** slot=reinterpret_cast<void**>(crew::image+rva);
    DWORD old=0;
    if(!VirtualProtect(slot,8,PAGE_READWRITE,&old))return false;
    *slot=to;
    VirtualProtect(slot,8,old,&old);
    return true;
}

// A stand-in scene object: EventAnimationObject's size, its class's NetworkObject vtable at +0x120 (the mapped image's, chained),
// the net word +0x128, its own weak pointer +0x28 / +0x30 on a control block {vtable, use, weak}.
struct Object {
    alignas(16) unsigned char bytes[0xC90]{};
    alignas(16) unsigned char control[16]{};
    explicit Object(std::uint16_t word) {
        using namespace crew;
        Put<void*>(bytes,0x120,image+0x17D4E90);
        Put<std::uint16_t>(bytes,0x128,word);
        Put<void*>(bytes,0x28,bytes);
        Put<void*>(bytes,0x30,control);
        Put<std::int32_t>(control,8,1);
        Put<std::int32_t>(control,0xC,1);
    }
    void* obj() { return bytes; }
    unsigned char* net() { return bytes+0x120; }
};

// What the game calls on a received message: the object's NetworkObject slot 17 (its class's, chained by object_net).
void Receive(unsigned char* net,void* reader) {
    reinterpret_cast<transport::ReceiveFn>(crew::At<void* const*>(net,0)[17])(net,reader);
}
void ReaderOf(const void* written,void* reader) {
    transport::Fn<void(__fastcall*)(void*,const void*,std::size_t)>(transport::kReaderFrom)(reader,
        static_cast<const unsigned char*>(written)+transport::kStreamData,crew::At<std::size_t>(written,transport::kStreamSize));
}

Object* peer=nullptr;   // where the stand-in send delivers
int sends=0;
// The stand-in for 0x773DA0: the bytes the game's writer wrote, read back by the game's reader (0x12B4530), into the peer's
// NetworkObject slot 17 through its vtable.
bool __fastcall StandInSend(void*,void* written) {
    ++sends;
    if(!peer)return false;
    alignas(16) unsigned char reader[0x600]{};
    ReaderOf(written,reader);
    Receive(peer->net(),reader);
    return true;
}

int drained=0;
std::uint8_t lastKind=0;
unsigned char lastPayload[crew::edf41net::kMostPayload]{};
std::size_t lastSize=0;
void Visit(void*,std::uint8_t kind,const unsigned char* data,std::size_t size) noexcept {
    ++drained;
    lastKind=kind;
    lastSize=size;
    std::memcpy(lastPayload,data,size);
}

// `tag` + a block (the game's writer), as a reader.
void Tagged(std::int8_t tag,const void* data,std::size_t size,unsigned char* reader) {
    transport::Stream w;
    transport::Fn<bool(__fastcall*)(void*,std::int8_t)>(transport::kWriteSmall)(w.bytes,tag);
    transport::Fn<bool(__fastcall*)(void*,const void*,std::size_t)>(transport::kWriteBlock)(w.bytes,data,size);
    ReaderOf(w.bytes,reader);
}
void Tagged(std::int8_t tag,unsigned char fill,std::size_t size,unsigned char* reader) {
    unsigned char data[64];
    std::memset(data,fill,sizeof(data));
    Tagged(tag,data,size,reader);
}

// A seat pick kind on the same objects (payload.cpp's on a vehicle; here beside E41N on one EventAnimationObject).
int picks=0;
crew::seat_net::Pick heardPick{};
void PickReceived(unsigned char*,const void* block,std::size_t size) noexcept {
    if(crew::seat_net::Decode(block,size,heardPick))++picks;
}
}  // namespace

int main(int argc,char** argv) {
    using namespace crew;
    if(argc<2 || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: pass the supported EDF.dll path for the native checks");
        return 77;
    }
    HMODULE mapped=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Check(mapped!=nullptr,"map EDF.dll without executing DllMain");
    image=edf::IdentifyImage(mapped);
    Check(image!=nullptr,"supported image timestamp and size");
    Check(RuntimeImports(mapped),"resolve the codec's CRT imports in private memory");
    for(object_net::Class c:{object_net::Class::eventAnimationObject,object_net::Class::ufoCarrier508}) {
        const object_net::ClassSlot& s=*object_net::ClassOf(c);
        Check(At<void*>(image,object_net::SlotRva(s,object_net::kSendSlot))==image+object_net::kStockSend,
              "EventAnimationObject / UfoCarrier508 NetworkObject send (slot 16) is 773DA0");
        Check(At<void*>(image,object_net::SlotRva(s,object_net::kReceiveSlot))==image+s.stockReceive && s.stockReceive==0x54D770,
              "their receive (slot 17) is GameObjectBase's 54D770 (the class table's row)");
    }
    config.edf41Net=true;
    Check(edf41net::Enabled(),"production signatures, the object message layer, the classes chained");
    int chainedClasses=0;
    for(object_net::Class c:{object_net::Class::eventAnimationObject,object_net::Class::ufoCarrier508}) {
        const object_net::ClassSlot& s=*object_net::ClassOf(c);
        int entry=-1;
        for(int i=0;i<transport::kMostClasses;++i)if(transport::ChainedTable(i)==image+s.table)entry=i;
        Check(entry>=0 && At<void*>(image,object_net::SlotRva(s,object_net::kReceiveSlot))==reinterpret_cast<void*>(transport::HookAt(entry)) &&
              transport::nextReceive[entry]==image+0x54D770,"the class's own slot 17 chained, GameObjectBase's receiver next");
        ++chainedClasses;
    }
    const int entries=transport::chained.load();
    Check(chainedClasses==2 && edf41net::Install() && transport::chained.load()==entries,"a second install chains nothing again");
    Check(PatchSlot(object_net::SlotRva(*object_net::ClassOf(object_net::Class::eventAnimationObject),object_net::kSendSlot),
                    reinterpret_cast<void*>(&StandInSend)),"the room's transport stands in from here");

    Object host(2),guest(1);
    Check(!edf41net::Guest(host.obj()) && edf41net::Guest(guest.obj()) && edf41net::Decides(host.obj()),"the host decides, the guest copy follows");

    // the host's event, through the game's writer and reader, into the guest copy's queue
    peer=&guest;
    const unsigned char body[56]={7,0,0,0,42};
    Check(edf41net::Send(host.obj(),1,body,sizeof(body)) && sends==1,"the host sends through NetworkObject slot 16");
    Check(edf41net::Drain(guest.obj(),&Visit,nullptr)==1 && lastKind==1 && lastSize==sizeof(body) && !std::memcmp(lastPayload,body,sizeof(body)),
          "the guest copy's receiver queued the payload unchanged, its update drains it");
    Check(edf41net::Drain(guest.obj(),&Visit,nullptr)==0,"drained once");
    Check(!edf41net::Send(guest.obj(),1,body,sizeof(body)) && sends==1,"a guest copy sends nothing");
    session=false;
    Check(!edf41net::Send(host.obj(),1,body,sizeof(body)) && sends==1,"offline nothing is sent");
    session=true;
    Put<std::uint16_t>(host.bytes,0x128,0);
    Check(!edf41net::Send(host.obj(),1,body,sizeof(body)) && sends==1,"an unregistered copy has nothing to send on");
    Put<std::uint16_t>(host.bytes,0x128,2);

    // a repeated packet (the same sender and sequence) is refused by the watermark
    {
        transport::Stream w;
        edf41net::Header h;
        h.kind=3; h.head.sender=77; h.head.sequence=5;
        unsigned char msg[edf41net::kMostMessage];
        std::size_t n=0;
        Check(edf41net::Encode(h,body,8,msg,sizeof(msg),&n),"encode");
        transport::Fn<bool(__fastcall*)(void*,std::int8_t)>(transport::kWriteSmall)(w.bytes,edf41net::kTag);
        transport::Fn<bool(__fastcall*)(void*,const void*,std::size_t)>(transport::kWriteBlock)(w.bytes,msg,n);
        const edf41net::Stats before=edf41net::Counters();
        StandInSend(nullptr,w.bytes);
        StandInSend(nullptr,w.bytes);
        const edf41net::Stats after=edf41net::Counters();
        Check(after.received==before.received+1 && after.refused==before.refused+1,"a repeat is refused, the first taken");
        Check(edf41net::Drain(guest.obj(),&Visit,nullptr)==1 && lastKind==3,"one queued");
        peer=&host;
        StandInSend(nullptr,w.bytes);
        Check(edf41net::Counters().refused==after.refused+1 && edf41net::Drain(host.obj(),&Visit,nullptr)==0,"the deciding copy takes no event");
        peer=&guest;
    }

    // other tags, and tag 15 blocks of a kind nobody registered, pass to the stock receiver with their read position
    for(std::int8_t tag:{std::int8_t(13),std::int8_t(14),std::int8_t(15)}) {
        alignas(16) unsigned char reader[0x600]{};
        Tagged(tag,0x5A,24,reader);
        unsigned char saved[sizeof(guest.bytes)];
        std::memcpy(saved,guest.bytes,sizeof(saved));
        const edf41net::Stats before=edf41net::Counters();
        Receive(guest.net(),reader);
        Check(At<std::int64_t>(reader,8)==1 && !std::memcmp(saved,guest.bytes,sizeof(saved)),
              "coop's 13 / 14 and an unregistered kind's 15 reach the stock receiver at their tag (it reads one value, changes nothing)");
        Check(edf41net::Counters().received==before.received && edf41net::Drain(guest.obj(),&Visit,nullptr)==0,"not queued here");
    }
    {
        alignas(16) unsigned char reader[0x600]{};
        Tagged(edf41net::kTag,0x11,40,reader);
        Object plain(1);   // a machine without this layer (or Edf41Net=0): the stock receiver
        unsigned char saved[sizeof(plain.bytes)];
        std::memcpy(saved,plain.bytes,sizeof(saved));
        transport::Fn<transport::ReceiveFn>(object_net::kObjectReceive)(plain.net(),reader);
        Check(!std::memcmp(saved,plain.bytes,sizeof(saved)) && At<std::int64_t>(reader,8)==1,
              "the stock receiver reads tag 15, changes nothing and returns");
    }
    {
        // E41N's magic on a block that is not a version-2 event (version 1's layout): taken by the kind, refused, nothing queued
        edf41net::Header h;
        h.kind=1; h.head.sender=5; h.head.sequence=1; h.head.version=1;
        alignas(16) unsigned char reader[0x600]{};
        Tagged(edf41net::kTag,&h,sizeof(h),reader);
        const edf41net::Stats before=edf41net::Counters();
        Receive(guest.net(),reader);
        Check(edf41net::Counters().refused==before.refused+1 && edf41net::Drain(guest.obj(),&Visit,nullptr)==0 && At<std::int64_t>(reader,8)==0,
              "an E41N block of another version is refused, nothing queued, the read position put back");
    }

    // two kinds on one object: a seat pick and the E41N events, one sender and one sequence counter, through the same slot
    {
        Check(object_net::Register(seat_net::kPickMagic,&PickReceived),"the pick kind registers beside E41N");
        Check(!object_net::Register(edf41net::kMagic,&PickReceived),"E41N's magic is edf41net's: a second receiver refused");
        seat_net::Pick p;
        p.seat=1;p.holder=4;
        Check(object_net::Stamp(&p,sizeof(p)) && object_net::Send(static_cast<unsigned char*>(host.obj()),&p,sizeof(p)) && sends==5,
              "a pick goes through the host's slot 16");
        Check(picks==1 && heardPick.holder==4 && heardPick.seat==1 && edf41net::Drain(guest.obj(),&Visit,nullptr)==0,
              "the pick reaches the pick kind only, nothing on the E41N queue");
        const edf41net::Stats before=edf41net::Counters();
        Check(edf41net::Send(host.obj(),2,body,4) && edf41net::Counters().received==before.received+1 && picks==1,
              "an event after it: the E41N queue only, the pick kind untouched");
        Check(edf41net::Drain(guest.obj(),&Visit,nullptr)==1 && lastKind==2 && lastSize==4,"the event drained");
        // The pick's sequence is newer than an E41N event stamped before it: each kind's gate orders its own messages alone.
        edf41net::Header early;
        early.kind=4;
        Check(object_net::Stamp(&early,sizeof(early)),"stamp an event");
        seat_net::Pick later;
        later.seat=1;later.holder=5;
        Check(object_net::Stamp(&later,sizeof(later)) && later.head.sequence>early.head.sequence &&
              object_net::Send(static_cast<unsigned char*>(host.obj()),&later,sizeof(later)) && picks==2 && heardPick.holder==5,
              "the later pick arrives first");
        unsigned char msg[edf41net::kMostMessage];
        std::size_t n=0;
        Check(edf41net::Encode(early,nullptr,0,msg,sizeof(msg),&n) && object_net::Send(static_cast<unsigned char*>(host.obj()),msg,n),"then the event");
        Check(edf41net::Drain(guest.obj(),&Visit,nullptr)==1 && lastKind==4 && picks==2,
              "the earlier event is still taken (the pick's newer sequence does not hold it back), the pick kind untouched");
    }

    // the development loopback (offline): the game's writer and reader, into the mirror's receiver
    session=false;
    config.edf41NetMirror=true;
    Object original(0),mirror(0);
    Check(!edf41net::Guest(mirror.obj()),"offline an unregistered copy decides");
    edf41net::Loopback(original.obj(),mirror.obj());
    Check(edf41net::Guest(mirror.obj()) && edf41net::IsMirror(mirror.obj()) && !edf41net::Guest(original.obj()),"the mirror is a guest copy");
    const unsigned char state[64]={3,3,3};
    const int sentBefore=sends;
    Check(edf41net::Send(original.obj(),3,state,sizeof(state)) && sends==sentBefore,"the original sends through the loopback, not the room");
    Check(edf41net::Drain(mirror.obj(),&Visit,nullptr)==1 && lastKind==3 && lastSize==sizeof(state) && lastPayload[2]==3,"the mirror gets it");
    Check(edf41net::Counters().looped>=1,"counted as looped");
    config.edf41NetMirror=false;

    // children: offline nothing; online without the game's network manager nothing either (no call, no weak count taken)
    Object parent(2),child(0);
    edf41net::Child(parent.obj(),child.obj(),edf41net::kChildPart,0,true);
    Check(At<std::uint16_t>(child.bytes,0x128)==0 && edf41net::Counters().waiting==0,"offline no identity is asked");
    session=true;
    Check(At<void*>(image,0x20B2AC8)==nullptr,"the mapped image has no network manager");
    const edf41net::Stats before=edf41net::Counters();
    edf41net::Child(parent.obj(),child.obj(),edf41net::kChildPart,0,true);
    Check(edf41net::Counters().childFailed==before.childFailed+1 && At<std::int32_t>(child.control,0xC)==1 && At<std::int32_t>(parent.control,0xC)==1,
          "no manager: refused before the call, the weak counts as they were");
    Check(At<unsigned char>(child.bytes,0x591)==0,"no target sync without an identity");
    Object orphanParent(0),waiting(0);
    edf41net::Child(orphanParent.obj(),waiting.obj(),edf41net::kChildPart,1,false);
    Check(edf41net::Counters().waiting==1 && At<std::int32_t>(waiting.control,0xC)==2,"a parent without its identity yet: the child waits (one weak count held)");
    Put<std::int32_t>(waiting.control,8,0);   // the child dies while waiting
    edf41net::Pump(orphanParent.obj());
    Check(edf41net::Counters().waiting==0 && At<std::int32_t>(waiting.control,0xC)==1,"a dead waiting child is dropped, its weak count released");
    std::printf("edf41net_native_test: %d checks passed\n",checks);
    return 0;
}
