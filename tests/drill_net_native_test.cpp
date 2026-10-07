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
    std::printf("drill_net_native_test: %d checks passed\n",checks);
}
