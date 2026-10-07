// Optional read-only EDF.dll validation. Map it WITHOUT its entry point, resolve only CRT memcpy imports, and run
// the real stream codec and unknown-tag receiver on this process's stand-ins. Never attach to the game or write files.
#include "../src/drill_net.cpp"
#include "edf/host.h"
#include <cstdio>
#include <initializer_list>

namespace crew {
unsigned char* image=nullptr;
Config config{};
bool session=true;
int received=0;
unsigned char* receivedVehicle=nullptr;
drill_net::State receivedState;
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
bool InSession() noexcept { return session; }
void DrillNetReceived(unsigned char* v,const drill_net::State& s) noexcept { ++received;receivedVehicle=v;receivedState=s; }
}
namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
bool RuntimeImports(HMODULE module) {
    auto* base=reinterpret_cast<unsigned char*>(module);
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    const auto& dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for(auto d=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+dir.VirtualAddress);d->Name;++d) {
        const char* dll=reinterpret_cast<const char*>(base+d->Name);
        if(_strnicmp(dll,"VCRUNTIME",9) && _strnicmp(dll,"api-ms-win-crt-",15))continue;
        HMODULE runtime=LoadLibraryA(dll);if(!runtime)return false;
        auto names=reinterpret_cast<const IMAGE_THUNK_DATA64*>(base+d->OriginalFirstThunk);
        auto slots=reinterpret_cast<std::uint64_t*>(base+d->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots) {
            if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;
            auto name=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
            FARPROC proc=GetProcAddress(runtime,reinterpret_cast<const char*>(name->Name));
            if(!proc)continue;
            DWORD old=0;if(!VirtualProtect(slots,8,PAGE_READWRITE,&old))return false;
            *slots=reinterpret_cast<std::uint64_t>(proc);VirtualProtect(slots,8,old,&old);
        }
    }
    return true;
}
void Reader(const void* written,void* reader) {
    crew::Fn<void(__fastcall*)(void*,const void*,std::size_t)>(0x12B4530)(reader,
        static_cast<const unsigned char*>(written)+0x10,crew::At<std::size_t>(written,0x5F0));
}
bool __fastcall Send(void* net,void* written) {
    alignas(16) unsigned char reader[0x600]{};
    Reader(written,reader);
    crew::Receive(static_cast<unsigned char*>(net),reader);
    return true;
}
}
int main(int argc,char** argv) {
    using namespace crew;
    if(argc<2 || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: pass the supported EDF.dll path for native transport checks");return 77;
    }
    HMODULE mapped=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Check(mapped!=nullptr,"map EDF.dll without executing DllMain");
    image=edf::IdentifyImage(mapped);Check(image!=nullptr,"supported image timestamp and size");
    Check(RuntimeImports(mapped),"resolve codec CRT imports in private process memory");
    Check(At<void*>(image,kReceiveSlot)==image+kReceive,"505 network receive is 6325B0");
    Check(At<void*>(image,kReceiveSlot-8)==image+kSend,"505 network send is 773DA0");
    Check(InstallDrillNet(),"production transport signatures and receive slot chain");
    alignas(16) unsigned char vehicle[0x800]{};
    void* netTable[18]{};netTable[16]=reinterpret_cast<void*>(&Send);
    Put<void*>(vehicle,0x120,netTable);Put<std::uint16_t>(vehicle,0x128,2);
    drill_net::State s;s.phase=drill_net::Phase::out;s.dir[2]=s.axis[2]=1;s.pos[2]=123;s.speed=90;
    Check(DrillNetSend(vehicle,s),"production send through NetworkObject +0x80");
    Check(received==1 && receivedVehicle==vehicle && receivedState.pos[2]==123 && receivedState.sequence==1,
          "native serializer/reader roundtrip preserves routed tank and flight");
    session=false;
    Check(!DrillNetSend(vehicle,s),"offline send is not routed");
    session=true;Put<std::uint16_t>(vehicle,0x128,0);
    Check(!DrillNetSend(vehicle,s),"unregistered copy cannot invent a descriptor");
    Put<std::uint16_t>(vehicle,0x128,2);
    for(std::int8_t tag:{std::int8_t(13),std::int8_t(14),std::int8_t(15)}) {
        Stream written;Fn<bool(__fastcall*)(void*,std::int8_t)>(kWriteSmall)(written.bytes,tag);
        s.sender=1;s.sequence=1;
        Fn<bool(__fastcall*)(void*,const void*,std::size_t)>(kWriteBlock)(written.bytes,&s,sizeof(s));
        alignas(16) unsigned char reader[0x600]{};Reader(written.bytes,reader);
        drill_net::State out;bool valid=false;
        Check(Peek(reader,out,valid)==(tag==15) && At<std::int64_t>(reader,8)==0,"peek restores stream, reserves only tag 15");
        const int before=received;
        if(tag!=15) {
            Receive(vehicle+0x120,reader);
            Check(received==before && At<std::int64_t>(reader,8)==1,"coop tags pass to stock with their original read position");
        } else {
            unsigned char saved[sizeof(vehicle)];std::memcpy(saved,vehicle,sizeof(vehicle));
            Fn<ReceiveFn>(kReceive)(vehicle+0x120,reader);
            Check(!std::memcmp(saved,vehicle,sizeof(vehicle)) && At<std::int64_t>(reader,8)==1,
                  "stock 505 receiver safely ignores tag 15 without changing object state");
        }
    }
    Stream bad;Fn<bool(__fastcall*)(void*,std::int8_t)>(kWriteSmall)(bad.bytes,drill_net::kTag);
    Fn<bool(__fastcall*)(void*,const void*,std::size_t)>(kWriteBlock)(bad.bytes,&s,4);
    alignas(16) unsigned char badReader[0x600]{};Reader(bad.bytes,badReader);
    const int before=received;Receive(vehicle+0x120,badReader);
    Check(received==before,"truncated drill payload is consumed without replay");
    // Run the REAL reference function with a world having no registry: its early path still destroys the incoming
    // by-value weak pointer. No game entry point, mission, peer or allocator is needed for that path.
    const unsigned char releaseWeak[]={0x49,0x8B,0x4E,0x08,0x48,0x85,0xC9,0x74,0x10,0xF0,0x0F,0xC1,0x79,0x0C,
        0x83,0xFF,0x01,0x75,0x06,0x48,0x8B,0x01,0xFF,0x50,0x08};
    Check(Matches(0x78511A,releaseWeak,sizeof(releaseWeak)),"native ReferenceId consumes the incoming weak reference");
    alignas(16) unsigned char world[0xE0]{};
    void* originalWorld=At<void*>(image,0x20B2AC0);Put<void*>(image,0x20B2AC0,world+0x98);
    alignas(16) unsigned char seat[0x340]{},control[16]{};
    Put<void*>(vehicle,kSeats,seat);Put<std::uint64_t>(vehicle,kSeatCount,1);
    Put<void*>(seat,kSeatRider,vehicle);Put<void*>(seat,kSeatRider+8,control);
    Put<LONG>(control,8,1);Put<LONG>(control,0xC,2);
    std::int32_t noIdentity=0;
    Fn<std::int32_t*(__fastcall*)(std::int32_t*,const void*)>(kReference)(&noIdentity,seat+kSeatRider);
    Check(noIdentity==-1 && At<LONG>(control,0xC)==1,"negative control: native callee consumes an unowned resident weak");
    Put<LONG>(control,0xC,2);
    Check(DrillNetController(vehicle)==-1 && At<LONG>(control,0xC)==2,
          "native current driver lookup consumes only an incremented temporary weak reference");
    Put<void*>(seat,kSeatRider,nullptr);Put<void*>(seat,0x300,vehicle);Put<void*>(seat,0x308,control);
    Check(DrillNetController(vehicle)==-1 && At<LONG>(control,0xC)==2,
          "native last driver lookup preserves the seat's resident weak reference");
    alignas(16) unsigned char dummy[0x130]{},dummyControl[16]{};
    Put<LONG>(dummyControl,8,1);Put<LONG>(dummyControl,0xC,2);
    Put<void*>(seat,kSeatRider,dummy);Put<void*>(seat,kSeatRider+8,dummyControl);
    Check(!RegisteredWeak(seat+kSeatRider) && RegisteredWeak(seat+0x300),"unregistered host NPC does not become a wire driver identity");
    Check(DrillNetController(vehicle)==-1 && At<LONG>(dummyControl,0xC)==2 && At<LONG>(control,0xC)==2,
          "NPC takeover looks up only the previous registered driver and preserves both resident weak references");
    Put<LONG>(control,8,0);
    Check(DrillNetController(vehicle)==-1 && At<LONG>(control,0xC)==2,"expired last driver uses host fallback without a reference call");
    Put<void*>(image,0x20B2AC0,originalWorld);
    std::printf("drill_net_native_test: %d checks passed\n",checks);
}
