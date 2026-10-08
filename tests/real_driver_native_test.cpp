// Production native adapter on a private image; optional real EDF.dll reset/profile.
#include "../src/real_driver_native.cpp"
#include <cstdio>
#include <cstdlib>
namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
}
namespace {
int checks=0,clears=0,setups=0;
unsigned char* owned=nullptr;
bool Owns(unsigned char* seat) noexcept { return seat==owned; }
void __fastcall Clear(void* input) { ++clears;std::memset(input,0,0x2A); }
void __fastcall Apply(void*,void*) { ++setups; }
void Check(bool ok,const char* why) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}
}
}
int wmain(int argc,wchar_t** argv) {
    using namespace crew;
    const bool native=argc>1;
    if(native)image=reinterpret_cast<unsigned char*>(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    else image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"private image");
    if(!native) {
        Check(!InstallRealDriverNative(&Owns),"unsupported native profile rejected");
        std::memcpy(image+kClearCall-14,kClearContext,sizeof(kClearContext));
        std::memcpy(image+kClearInput,kClearSig,sizeof(kClearSig));
        std::memcpy(image+kReadSetup,kReadSetupSig,sizeof(kReadSetupSig));
        std::memcpy(image+0x6330F0,kPathFlagSig,sizeof(kPathFlagSig));
    }
    Check(!InstallRealDriverNative(nullptr),"missing ownership policy rejected");
    Check(InstallRealDriverNative(&Owns),"checked native call patched");
    Check(InstallRealDriverNative(&Owns),"same policy installation idempotent");
    Check(std::memcmp(image+kClearCall-14,kClearContext,14)==0 &&
        std::memcmp(image+kClearCall+5,kClearContext+19,6)==0,"surrounding Human instructions unchanged");
    if(!native)nextClear=&Clear;
    auto hook=reinterpret_cast<ClearFn>(image+kClearCall+5+At<std::int32_t>(image+kClearCall,1));
    unsigned char seat[0x340]{};
    owned=seat;Put<float>(seat,0x2C4,.75f);Put<float>(seat,0x2E4,1);
    hook(seat+0x2C0);
    Check(At<float>(seat,0x2C4)==.75f && At<float>(seat,0x2E4)==1,"owned real NPC keeps drive and trigger");
    owned=nullptr;hook(seat+0x2C0);
    Check(At<float>(seat,0x2C4)==0 && At<float>(seat,0x2E4)==0,"unowned human input cleared by original");
    alignas(16) unsigned char vehicle[0xE40]{},weapon[0x900]{},holders[0x48]{},path[0x70]{};
    Put<void*>(vehicle,0x638,holders);Put<std::size_t>(vehicle,0x648,1);
    Put<void*>(holders,0x10,weapon);Put<void*>(vehicle,0xE10,path);
    Put<std::uint32_t>(path,0x68,0x100);Put<int>(vehicle,0xE30,2);weapon[0x8B6]=1;
    Check(PrepareNpcVehicle(vehicle,false),"already initialized vehicle prepared");
    Check(weapon[0x8B6]==0 && At<std::uint32_t>(path,0x68)==0x120,"native NPC weapon and path flags applied");
    Check(At<int>(vehicle,0xE30)==2,"existing snapshot mode is preserved");
    if(!native) {
        // Fixture only for external setup parser; invokes production setup/apply path.
        const unsigned char ret[]={0xC3};std::memcpy(image+kReadSetup,ret,1);
        void* vtable[47]{};vtable[46]=reinterpret_cast<void*>(&Apply);Put<void*>(vehicle,0,vtable);
        Check(PrepareNpcVehicle(vehicle,true) && setups==1,"spawned vehicle applies its mission setup exactly once");
        Check(PrepareNpcVehicle(vehicle,false) && setups==1,"existing vehicle does not reapply setup");
    }
    ready=false;Put<float>(seat,0x2C4,.75f);owned=seat;hook(seat+0x2C0);
    Check(At<float>(seat,0x2C4)==0,"failed readiness preserves normal reset");
    std::printf("real_driver_native_test: %d checks passed (%s)\n",checks,native?"real EDF profile/reset, fixture vehicle":"private fixture image");
}
