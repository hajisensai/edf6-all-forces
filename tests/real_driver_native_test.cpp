// Production native adapter on a private image; optional real EDF.dll reset/profile.
#include "../src/real_driver_native.cpp"
#include <cstdio>
#include <cstdlib>
namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
}
namespace {
int checks=0,clears=0,setups=0,boards=0,exits=0;
unsigned char* owned=nullptr;
bool Owns(unsigned char* seat) noexcept { return seat==owned; }
void __fastcall Clear(void* input) { ++clears;std::memset(input,0,0x2A); }
void __fastcall Apply(void*,void*) { ++setups; }
void __fastcall Board(void*) { ++boards; }
void __fastcall Exit(void*) { ++exits; }
void Jump(unsigned char* at,void* fn) {
    at[0]=0x48;at[1]=0xB8;crew::Put<void*>(at,2,fn);at[10]=0xFF;at[11]=0xE0;
}
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
        std::memcpy(image+kAnnounceBoard,kBoardSig,sizeof(kBoardSig));
        std::memcpy(image+kAnnounceExit,kExitSig,sizeof(kExitSig));
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
        Jump(image+kAnnounceBoard,reinterpret_cast<void*>(&Board));
        Jump(image+kAnnounceExit,reinterpret_cast<void*>(&Exit));
        unsigned char human[0x1830]{},ctrl[16]{};
        Put<LONG>(ctrl,8,1);Put<void*>(human,0x1540,seat);Put<void*>(human,0x1550,ctrl);
        Check(AnnounceNpcBoarding(human) && boards==1,"local NPC announces successful ride");
        Check(!AnnounceNpcDismount(human) && exits==0,"still seated NPC cannot announce exit");
        human[0x128]=1;
        Check(!AnnounceNpcBoarding(human) && boards==1,"remote NPC cannot echo board");
        Put<void*>(human,0x1540,nullptr);Put<void*>(human,0x1550,nullptr);
        Check(!AnnounceNpcDismount(human) && exits==0,"remote NPC cannot echo exit");
        human[0x128]=0;
        Check(AnnounceNpcDismount(human) && exits==1,"local completed exit announces native message");
        human[kHumanPlayer]=1;
        Check(!AnnounceNpcDismount(human) && exits==1,"player remains owned by native input path");
        Check(!AnnounceNpcBoarding(nullptr) && !AnnounceNpcDismount(nullptr),"null human rejected");
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
