// Optional read-only EDF.dll validation of the EDF4.1 classes' network layer (src/edf41net.cpp), as tests/drill_net_native_test.cpp
// does for the drill: EDF.dll mapped WITHOUT its entry point, only its CRT imports resolved, and the production code run on
// stand-in objects with the game's own stream writer / reader, its stock receiver 0x54D770 and its vtables. The game's send
// (0x773DA0, which needs a live room) is the one stand-in on the path: it hands the bytes the game's writer wrote to the game's
// reader, into the other copy's receiver. Never attaches to the game, writes no file. Run: edf41net_native_test <EDF.dll>.
#include "../src/edf41net.cpp"
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

// A stand-in scene object: EventAnimationObject's size, its NetworkObject vtable at +0x120, the net word +0x128, its own weak
// pointer +0x28 / +0x30 on a control block {vtable, use, weak}.
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

Object* peer=nullptr;   // where the stand-in send delivers
int sends=0;
// The stand-in for 0x773DA0: the bytes the game's writer wrote, read back by the game's reader (0x12B4530), into the peer's
// NetworkObject slot 17 through its vtable (the production receiver on a copy, the stock one otherwise).
bool __fastcall StandInSend(void*,void* written) {
    using namespace crew;
    ++sends;
    if(!peer)return false;
    edf41net::RawStream reader;
    edf41net::Fn<void(__fastcall*)(void*,const void*,std::size_t)>(0x12B4530)(reader.bytes,static_cast<unsigned char*>(written)+0x10,
                                                                             At<std::size_t>(written,0x5F0));
    void* const* vt=At<void* const*>(peer->net(),0);
    reinterpret_cast<edf41net::ReceiveFn>(vt[17])(peer->net(),reader.bytes);
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

// A stream of `tag` + `size` bytes of `fill` (the game's writer), as a reader.
void Tagged(std::int8_t tag,unsigned char fill,std::size_t size,crew::edf41net::RawStream& reader) {
    using namespace crew::edf41net;
    RawStream w;
    Construct(w);
    unsigned char data[64];
    std::memset(data,fill,sizeof(data));
    Fn<bool(__fastcall*)(void*,std::int8_t)>(kWriteSmall)(w.bytes,tag);
    Fn<bool(__fastcall*)(void*,const void*,std::size_t)>(kWriteBlock)(w.bytes,data,size);
    Fn<void(__fastcall*)(void*,const void*,std::size_t)>(kReaderFrom)(reader.bytes,w.bytes+0x10,crew::At<std::size_t>(w.bytes,0x5F0));
    Destroy(w);
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
    for(unsigned vt:{0x17D4E90u,0x17C9F50u}) {
        Check(At<void*>(image,vt+16*8)==image+0x773DA0,"EventAnimationObject / UfoCarrier508 NetworkObject send (slot 16) is 773DA0");
        Check(At<void*>(image,vt+17*8)==image+0x54D770,"their receive (slot 17) is GameObjectBase's 54D770");
    }
    config.edf41Net=true;
    Check(edf41net::Enabled(),"production signatures, sender identity, vtable copies");
    for(const auto& t:edf41net::tables) {
        Check(t.block[0]==At<void*>(image,t.rva-8) && t.next==image+0x54D770 && t.block[18]==reinterpret_cast<void*>(&edf41net::Receive),
              "the copy keeps the RTTI and the stock slots, slot 17 ours, the stock one next");
        for(int i=0;i<22;++i)if(i!=17)Check(t.block[i+1]==At<void*>(image,t.rva+8*static_cast<unsigned>(i)),"the other slots are the live table's");
    }
    edf41net::tables[0].block[1+16]=reinterpret_cast<void*>(&StandInSend);   // the room's transport stands in from here

    Object host(2),guest(1);
    edf41net::Attach(host.obj());
    edf41net::Attach(guest.obj());
    Check(At<void*>(host.net(),0)==&edf41net::tables[0].block[1] && At<void*>(guest.net(),0)==&edf41net::tables[0].block[1],"Attach swaps the NetworkObject vtable");
    edf41net::Attach(host.obj());
    Check(At<void*>(host.net(),0)==&edf41net::tables[0].block[1],"a second Attach leaves it");
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
        edf41net::RawStream w;
        edf41net::Construct(w);
        edf41net::Header h;
        h.kind=3; h.sender=77; h.sequence=5;
        unsigned char msg[edf41net::kMostMessage];
        std::size_t n=0;
        Check(edf41net::Encode(h,body,8,msg,sizeof(msg),&n),"encode");
        edf41net::Fn<bool(__fastcall*)(void*,std::int8_t)>(edf41net::kWriteSmall)(w.bytes,edf41net::kTag);
        edf41net::Fn<bool(__fastcall*)(void*,const void*,std::size_t)>(edf41net::kWriteBlock)(w.bytes,msg,n);
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
        edf41net::Destroy(w);
    }

    // other tags pass to the stock receiver with their read position; the stock receiver ignores ours
    for(std::int8_t tag:{std::int8_t(13),std::int8_t(14),std::int8_t(15)}) {
        edf41net::RawStream reader;
        Tagged(tag,0x5A,24,reader);
        unsigned char saved[sizeof(guest.bytes)];
        std::memcpy(saved,guest.bytes,sizeof(saved));
        const edf41net::Stats before=edf41net::Counters();
        edf41net::Receive(guest.net(),reader.bytes);
        Check(At<std::int64_t>(reader.bytes,8)==1 && !std::memcmp(saved,guest.bytes,sizeof(saved)),
              "coop's 13 / 14 and the vehicles' 15 reach the stock receiver at their tag (it reads one value, changes nothing)");
        Check(edf41net::Counters().received==before.received && edf41net::Drain(guest.obj(),&Visit,nullptr)==0,"not queued here");
    }
    {
        edf41net::RawStream reader;
        Tagged(edf41net::kTag,0x11,40,reader);
        Object plain(1);   // a machine without this layer (or Edf41Net=0): the stock table
        unsigned char saved[sizeof(plain.bytes)];
        std::memcpy(saved,plain.bytes,sizeof(saved));
        edf41net::Fn<edf41net::ReceiveFn>(0x54D770)(plain.net(),reader.bytes);
        Check(!std::memcmp(saved,plain.bytes,sizeof(saved)) && At<std::int64_t>(reader.bytes,8)==1,
              "the stock receiver reads tag 12, changes nothing and returns");
    }
    {
        edf41net::RawStream reader;
        Tagged(edf41net::kTag,0x11,40,reader);   // a tag-12 block that is not this layer's (bad magic)
        const edf41net::Stats before=edf41net::Counters();
        edf41net::Receive(guest.net(),reader.bytes);
        Check(edf41net::Counters().refused==before.refused+1 && edf41net::Drain(guest.obj(),&Visit,nullptr)==0 && At<std::int64_t>(reader.bytes,8)==0,
              "another plugin's tag 12 is refused, nothing queued, the read position put back");
    }

    // the development loopback (offline): the game's writer and reader, into the mirror's receiver
    session=false;
    config.edf41NetMirror=true;
    Object original(0),mirror(0);
    edf41net::Attach(original.obj());
    edf41net::Attach(mirror.obj());
    Check(!edf41net::Guest(mirror.obj()),"offline an unregistered copy decides");
    edf41net::Loopback(original.obj(),mirror.obj());
    Check(edf41net::Guest(mirror.obj()) && edf41net::IsMirror(mirror.obj()) && !edf41net::Guest(original.obj()),"the mirror is a guest copy");
    const unsigned char state[64]={3,3,3};
    Check(edf41net::Send(original.obj(),3,state,sizeof(state)),"the original sends through the loopback");
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
