// Read-only EDF.dll: its real serializer, native object envelope and final event packet writer. No game entry point,
// no running-game attachment, no EOS call. 750130's queue/peer lookup is replaced with a recorder only in this process.
#include "../src/sazabi_net.cpp"
#include "edf/host.h"
#include <cstdio>
#include <climits>
namespace crew {
unsigned char* image=nullptr;
Config config{};
int sazabiReceived=0,drillReceived=0;
sazabi_net::State lastState;
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
bool InSession() noexcept { return true; }
void SazabiNetReceived(unsigned char*,const sazabi_net::State& s) noexcept { ++sazabiReceived;lastState=s; }
void DrillNetReceived(unsigned char*,const drill_net::State&) noexcept { ++drillReceived; }
}
namespace {
using namespace crew;
namespace transport=crew::sazabi_transport;
int checks=0;
std::size_t messageBytes=0,objectBytes=0,packetBytes=0;
void Check(bool ok,const char* why) { ++checks;if(!ok){std::printf("FAIL: %s\n",why);std::exit(1);} }
#include "sazabi_native_imports.inc"
void Reader(const void* written,void* reader) {
    transport::Fn<void(__fastcall*)(void*,const void*,std::size_t)>(0x12B4530)(reader,
        static_cast<const unsigned char*>(written)+0x10,At<std::size_t>(written,0x5F0));
}
bool __fastcall Loopback(void* net,void* written) {
    messageBytes=At<std::size_t>(written,0x5F0);
    alignas(16) unsigned char reader[0x600]{};Reader(written,reader);
    reinterpret_cast<transport::ReceiveFn>((*reinterpret_cast<void***>(net))[17])(static_cast<unsigned char*>(net),reader);
    return true;
}
void* __fastcall NoPeer(void*,void* out) { std::memset(out,0,16);return out; }
void* __fastcall RecordEvent(void*,void* out,std::uint32_t tag,const void* data,std::size_t size,const void*) {
    Check(tag==7,"native NetworkObject sends event 7");objectBytes=size;
    // Execute the actual final event packet writer with maximum-width event/sequence ids, and no transport peer.
    // Its flush threshold keeps the assembled bytes in the native stream for measurement, without touching EOS.
    alignas(16) unsigned char writer[0x640]{},context[0x100]{},event[0x40]{};
    void* table[6]{};table[5]=reinterpret_cast<void*>(&NoPeer);void* provider=table;
    transport::Fn<void*(__fastcall*)(void*,int)>(transport::kConstruct)(writer+0x30,0x40);
    Put<std::size_t>(writer,0x18,static_cast<std::size_t>(-1));
    Put<void*>(context,0xC0,writer);Put<void*>(context,0xA8,&provider);
    Put<std::uint8_t>(event,0xC,static_cast<std::uint8_t>(tag));Put<std::uint16_t>(event,0x10,0xFFFF);
    Put<std::int32_t>(event,0x14,INT_MIN);
    Check(transport::Fn<bool(__fastcall*)(void*,void*,const void*,std::size_t)>(0x750380)(context,event,data,size),
          "actual 750380 builds the complete application packet");
    packetBytes=At<std::size_t>(writer+0x30,0x5F0);
    transport::Fn<void(__fastcall*)(void*)>(transport::kDestroy)(writer+0x30);
    std::memset(out,0,16);return out;
}
void PatchRecorder(unsigned char* original) {
    std::memcpy(original,image+0x750130,12);
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto target=reinterpret_cast<std::uintptr_t>(&RecordEvent);std::memcpy(jump+2,&target,8);
    Check(edf::PatchCode(image+0x750130,original,jump,12),"redirect only private event queue to recorder");
}
}
int main(int argc,char** argv) {
    using namespace crew;
    if(argc<2 || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES){std::puts("SKIP: EDF.dll path required");return 77;}
    HMODULE mapped=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Check(mapped!=nullptr,"map EDF.dll without DllMain");image=edf::IdentifyImage(mapped);
    Check(image && RuntimeImports(mapped),"supported image and private CRT imports");
    Check(At<void*>(image,0x17DB4C8)==image+0x6325B0 && At<void*>(image,0x17DB4C0)==image+0x773DA0,"506 native send/receive slots");
    Check(InstallDrillNet() && InstallSazabiNet(),"505 drill and 506 Sazabi install in the same process");
    alignas(16) unsigned char sazabiVehicle[0x800]{},drillVehicle[0x800]{};
    void* sazabiTable[18]{},*drillTable[18]{};
    sazabiTable[16]=drillTable[16]=reinterpret_cast<void*>(&Loopback);
    sazabiTable[17]=At<void*>(image,0x17DB4C8);drillTable[17]=At<void*>(image,0x17DB0D0);
    Put<void*>(sazabiVehicle,0x120,sazabiTable);Put<std::uint16_t>(sazabiVehicle,0x128,2);
    Put<void*>(drillVehicle,0x120,drillTable);Put<std::uint16_t>(drillVehicle,0x128,2);
    sazabi_net::State s;s.flags=sazabi_net::kDriven;s.pose.guard=1;s.funnels[0].at[0]=123;s.funnels[0].target=77;
    Check(SazabiNetSend(sazabiVehicle,s) && sazabiReceived==1 && drillReceived==0 && lastState.pose.guard==1 &&
        lastState.funnels[0].target==77,"native Sazabi codec roundtrip routes guard and target identity");
    Check(messageBytes==sizeof(s)+3,"native tag/block encoding has three bytes overhead");
    drill_net::State drill;
    Check(DrillNetSend(drillVehicle,drill) && drillReceived==1 && sazabiReceived==1,"drill remains independently usable");
    for(std::int8_t tag=13;tag<=15;++tag) {
        transport::Stream written;transport::Fn<bool(__fastcall*)(void*,std::int8_t)>(transport::kWriteSmall)(written.bytes,tag);
        s.sender=1;s.sequence=1;
        transport::Fn<bool(__fastcall*)(void*,const void*,std::size_t)>(transport::kWriteBlock)(written.bytes,&s,sizeof(s));
        alignas(16) unsigned char reader[0x600]{};Reader(written.bytes,reader);
        sazabi_net::State decoded;bool valid=false;
        Check(transport::Peek(reader,decoded,valid)==(tag==15) && At<std::int64_t>(reader,8)==0,"peek preserves unrelated coop message position");
        unsigned char before[sizeof(sazabiVehicle)];std::memcpy(before,sazabiVehicle,sizeof(before));
        transport::Fn<transport::ReceiveFn>(transport::kReceive)(sazabiVehicle+0x120,reader);
        Check(!std::memcmp(before,sazabiVehicle,sizeof(before)) && At<std::int64_t>(reader,8)==1,"stock receiver ignores custom tags safely");
    }
    // Another feature's type-15 block is not swallowed just because this receiver also uses type 15.
    transport::Stream wrong;transport::Fn<bool(__fastcall*)(void*,std::int8_t)>(transport::kWriteSmall)(wrong.bytes,15);
    transport::Fn<bool(__fastcall*)(void*,const void*,std::size_t)>(transport::kWriteBlock)(wrong.bytes,&drill,sizeof(drill));
    alignas(16) unsigned char reader[0x600]{};Reader(wrong.bytes,reader);
    sazabi_net::State decoded;bool valid=false;
    Check(!transport::Peek(reader,decoded,valid) && At<std::int64_t>(reader,8)==0,"different magic passes through unchanged");
    // The full native object wrapper -> broadcast wrapper -> final event packet writer, with a max-width descriptor.
    alignas(16) unsigned char world[0x1600]{},manager[0x100]{},session[0x100]{},descriptor[0x40]{},reference[16]{};
    Put<void*>(image,0x20B2AC0,world+0x98);Put<void*>(image,0x20B2AC8,world);
    Put<void*>(world,0xD0,manager);Put<void*>(manager,0xE0,session);
    Put<std::int32_t>(descriptor,0x28,INT_MAX);Put<LONG>(reference,0,2);Put<LONG>(reference,4,2);
    Put<void*>(reference,8,descriptor);Put<void*>(sazabiVehicle,0x130,reference);
    sazabiTable[16]=image+0x773DA0;
    unsigned char original[12];PatchRecorder(original);
    Check(SazabiNetSend(sazabiVehicle,s),"production state passes the real 773DA0 object-envelope sender");
    Check(objectBytes>sizeof(s) && packetBytes>objectBytes && packetBytes<=1170,"complete native event/object envelope fits stock EOS budget");
    Check(At<LONG>(reference,0)==2 && At<LONG>(reference,4)==2,"native descriptor reference counts remain balanced");
    std::printf("sazabi_net_native_test: %d checks passed; state %zu, object message %zu, full packet %zu / 1170 bytes\n",
                checks,sizeof(s),objectBytes,packetBytes);
}
