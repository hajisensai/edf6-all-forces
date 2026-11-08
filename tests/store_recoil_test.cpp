// Execute the production 403 slot 48 hook (src/storerecoil.cpp) against a stand-in EDF.dll: the 403's vtable, its stock
// slot 48 and Vehicle404_Tank's (recorders at their addresses), the instruction bytes the install reads. A holder past
// the vehicle's seats (the AGM-114 store, holder 3 of a three-seat 403) must not reach the stock code, which takes it for
// seat 3's machine gun (testhub #12: EDF+5F9F2B reading 0xC); every stock holder must. No game.
#include "../src/storerecoil.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
}

namespace {
using namespace crew;
using store_recoil::Path;
int checks=0;
void Check(bool condition,const char* description) {
    ++checks;
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",description);std::exit(1);}
}

constexpr std::size_t kImageSize=0x1800000;
unsigned char* gVehicle=nullptr;
const unsigned char* stockHolder=nullptr;
const unsigned char* hullHolder=nullptr;
int stockCalls=0,hullCalls=0;

void __fastcall StockFired(unsigned char* v,unsigned char* holder) { Check(v==gVehicle,"stock: the vehicle");++stockCalls;stockHolder=holder; }
void __fastcall HullFired(unsigned char* v,unsigned char* holder) { Check(v==gVehicle,"hull: the vehicle");++hullCalls;hullHolder=holder; }

void Jump(unsigned rva,const void* to) {
    unsigned char code[12]={0x48,0xB8};
    std::memcpy(code+2,&to,8);
    code[10]=0xFF;code[11]=0xE0;
    std::memcpy(image+rva,code,sizeof(code));
}

void Fire(unsigned char* holder) {
    stockCalls=hullCalls=0;stockHolder=hullHolder=nullptr;
    auto vt=*reinterpret_cast<void***>(gVehicle);
    reinterpret_cast<FiredFn>(vt[kSlotFired])(gVehicle,holder);
}
}  // namespace

int main() {
    static_assert(store_recoil::Route(0,4,3)==Path::stock,"holder 0: the rail gun, the 403's own BodyRecoil branch");
    static_assert(store_recoil::Route(1,4,3)==Path::stock && store_recoil::Route(2,4,3)==Path::stock,"the gunners' guns");
    static_assert(store_recoil::Route(3,4,3)==Path::hull,"the store past the seats");
    static_assert(store_recoil::Route(3,4,4)==Path::stock,"a holder with a seat of its index: the stock code");
    static_assert(store_recoil::Route(4,4,3)==Path::stock,"not one of the holders: the stock code");
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,kImageSize,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"stand-in image");
    auto vt=reinterpret_cast<void**>(image+kVtable403);
    vt[kSlotFired]=image+kFired403;
    Jump(kFired403,reinterpret_cast<const void*>(&StockFired));
    std::memcpy(image+kIndexCall,kIndexCallCode,sizeof(kIndexCallCode));
    std::memcpy(image+kAimBranch,kAimBranchCode,sizeof(kAimBranchCode));
    std::memcpy(image+kHullRecord,kHullRecordCode,sizeof(kHullRecordCode));
    std::memcpy(image+kHullPush,kHullPushCode,sizeof(kHullPushCode));
    Check(!InstallStoreRecoil() && vt[kSlotFired]==image+kFired403,"the hull path's bytes not as read: not installed");
    std::memcpy(image+kHullHead,kHullHeadCode,sizeof(kHullHeadCode));
    image[kAimBranch]^=1;
    Check(!InstallStoreRecoil() && vt[kSlotFired]==image+kFired403,"the seat-indexing branch not as read: not installed");
    image[kAimBranch]^=1;
    Check(InstallStoreRecoil() && vt[kSlotFired]!=image+kFired403,"installs on the stand-in");
    Jump(kHullFired,reinterpret_cast<const void*>(&HullFired));   // after the install read its bytes

    alignas(16) static unsigned char veh[0x2B00]{},holders[0x48*4]{};
    gVehicle=veh;
    Put<void*>(veh,0,vt);
    Put<unsigned char*>(veh,kHolders,holders);
    Put<std::uint64_t>(veh,kHolderCount,4);   // the rail gun, the two gunners' machine guns, the AGM-114s
    Put<std::uint64_t>(veh,kSeatCount,3);
    for(int i=0;i<3;++i) {
        Fire(holders+i*kHolderStride);
        Check(stockCalls==1 && hullCalls==0 && stockHolder==holders+i*kHolderStride,"a stock holder: the 403's own code");
    }
    Fire(holders+3*kHolderStride);
    Check(stockCalls==0 && hullCalls==1 && hullHolder==holders+3*kHolderStride,
          "the store past the seats: the hull push, never the stock seat-3 jitter");
    Put<std::uint64_t>(veh,kSeatCount,4);
    Fire(holders+3*kHolderStride);
    Check(stockCalls==1 && hullCalls==0,"a fourth seat exists: the stock code");
    std::printf("store_recoil: %d checks passed\n",checks);
    return 0;
}
