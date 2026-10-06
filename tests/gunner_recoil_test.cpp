// Execute the production gunner-recoil hook (src/gunnerrecoil.cpp) against a stand-in EDF.dll: its vtables, the stock
// weapon message (slot 52) that updates the shot count the way 0x690420 does, a class slot 48 that records the recoil
// strength it reads, and the vehicle NetworkObject's operator query. No game, no network.
#include "../src/gunnerrecoil.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace crew {
unsigned char* image=nullptr;
Config config{};
bool testOnline=true,testAuthority=true;
const Config& Cfg() noexcept { return config; }
bool InSession() noexcept { return testOnline; }
bool VehicleAuthority(unsigned char*) noexcept { return testAuthority; }
void Log(const char*,...) noexcept {}
}

namespace {
using namespace crew;
using namespace crew::gunner_recoil;
int checks=0;
void Check(bool condition,const char* description) {
    ++checks;
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",description);std::exit(1);}
}

constexpr std::size_t kImageSize=0x1800000;
constexpr int kTitan=2;   // kRecoilClasses[2]: 404_Tank
unsigned char* gVehicle=nullptr;
unsigned char* gWeapon=nullptr;
std::int32_t messageShots=0;   // what the next stock message sets the count to
int fires=0;
float recoilSeen[64]{};
unsigned char operatorNet[16]{};
bool operatorKnown=true;
int stockCalls=0;

void __fastcall StockMessage(unsigned char*,void*) { ++stockCalls; Put<std::int32_t>(gWeapon,kWeaponShots,messageShots); }
void __fastcall ClassFired(unsigned char* v,unsigned char* holder) {
    Check(v==gVehicle && At<unsigned char*>(holder,kHolderWeapon)==gWeapon,"slot 48 gets the gVehicle and the gun's holder");
    if(fires<64)recoilSeen[fires]=At<float>(gWeapon,kWeaponRecoil);
    ++fires;
}
const unsigned char* __fastcall Operator(void*,const void* w) { Check(w==gWeapon,"operator asked for the gun"); return operatorKnown ? operatorNet : nullptr; }

// A 12-byte absolute jump at image+rva to `to` (the hook calls slot 48 by its stock address).
void Jump(unsigned rva,const void* to) {
    unsigned char code[12]={0x48,0xB8};
    std::memcpy(code+2,&to,8);
    code[10]=0xFF;code[11]=0xE0;
    std::memcpy(image+rva,code,sizeof(code));
}

void Message(std::int32_t to) {
    messageShots=to;
    auto vt=*reinterpret_cast<void***>(gVehicle);
    reinterpret_cast<void(__fastcall*)(unsigned char*,void*)>(vt[52])(gVehicle,nullptr);
}

void Reset(std::int32_t shots) { Put<std::int32_t>(gWeapon,kWeaponShots,shots); fires=0; stockCalls=0; }
}  // namespace

int main() {
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,kImageSize,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"stand-in image");
    std::memcpy(image+kWeaponMessage,kWeaponMessageSig,sizeof(kWeaponMessageSig));
    std::memcpy(image+kOperator,kOperatorSig,sizeof(kOperatorSig));
    std::memcpy(image+0x690540,kStoreShotsSig,sizeof(kStoreShotsSig));
    std::memcpy(image+0x69056D,kOperatorTestSig,sizeof(kOperatorTestSig));
    // Every class: slot 48 its stock address (a jump to the recorder), slot 52 the stock message (after the signature,
    // which stays readable: the jump to the stand-in lives at +0x40).
    Jump(kWeaponMessage+0x40,reinterpret_cast<const void*>(&StockMessage));
    for(const auto& c:kRecoilClasses) {
        auto vt=reinterpret_cast<void**>(image+c.vtable);
        vt[kSlotFired]=image+c.fired;
        vt[kSlotWeaponMessage]=image+kWeaponMessage+0x40;
        Jump(c.fired,reinterpret_cast<const void*>(&ClassFired));
    }
    // The stock message is not at its own address here; the hook chains whatever the slot holds.
    Check(InstallGunnerRecoil(),"installs on the stand-in");
    for(const auto& c:kRecoilClasses)Check(reinterpret_cast<void**>(image+c.vtable)[kSlotWeaponMessage]!=image+kWeaponMessage+0x40,"every class chained");

    alignas(16) static unsigned char veh[0x1800]{},holders[0x48*3]{},gun[0x1600]{},other[0x1600]{};
    static void* netVtable[16]{};
    gVehicle=veh;gWeapon=gun;
    Put<void*>(veh,0,image+kRecoilClasses[kTitan].vtable);
    netVtable[kNetOperatorSlot]=reinterpret_cast<void*>(&Operator);
    Put<void*>(veh,kVehicleNet,netVtable);
    Put<unsigned char*>(veh,kHolders,holders);
    Put<std::uint64_t>(veh,kHolderCount,3);
    Put<unsigned char*>(holders+1*kHolderStride,kHolderWeapon,gun);        // the side cannon: holder 1
    Put<std::int32_t>(holders+1*kHolderStride,kHolderRecoilType,kBodyRecoil);
    Put<unsigned char*>(holders+2*kHolderStride,kHolderWeapon,other);      // another gun, never messaged
    Put<float>(gun,kWeaponFireRecoil,0.6f);
    Put<float>(gun,kWeaponRecoil,0.125f);   // the copy's own decaying value, which must come back as it was
    operatorNet[kNetFlags]=1;               // a remote player in the gunner seat

    Reset(5);Message(6);
    Check(stockCalls==1,"the stock message always runs");
    Check(fires==1 && recoilSeen[0]==0.6f,"authority: one shot of the remote gunner, at the gun's FireRecoil");
    Check(At<float>(gun,kWeaponRecoil)==0.125f,"the copy's own recoil value is put back");

    Reset(5);Message(8);
    Check(fires==3,"one push per shot the message carried");

    Reset(5);Message(5000);
    Check(fires==kMostShots,"a broken count is capped");

    Reset(5);Message(5);
    Check(fires==0,"no new shot, no push");

    Reset(5);Message(4);
    Check(fires==0,"an older count (0x690420 ignores it) adds nothing");

    testAuthority=false;Reset(5);Message(6);testAuthority=true;
    Check(fires==0,"not the authority: its pose follows the authority's, which has the push");

    operatorNet[kNetFlags]=0;Reset(5);Message(6);operatorNet[kNetFlags]=1;
    Check(fires==0,"local operator: the stock receive fires the copy (its own recoil), nothing added");

    operatorKnown=false;Reset(5);Message(6);operatorKnown=true;
    Check(fires==0,"no operator (an NPC gun: the host fires it): nothing added");

    Put<std::int32_t>(holders+1*kHolderStride,kHolderRecoilType,1);Reset(5);Message(6);
    Put<std::int32_t>(holders+1*kHolderStride,kHolderRecoilType,kBodyRecoil);
    Check(fires==0,"AimRecoil mount (a machine gun's aim jitter): left alone");

    testOnline=false;Reset(5);Message(6);testOnline=true;
    Check(fires==0 && stockCalls==1,"offline: the stock message only");

    Put<std::uint64_t>(veh,kHolderCount,kMostHolders+1);Reset(5);Message(6);Put<std::uint64_t>(veh,kHolderCount,3);
    Check(fires==0 && stockCalls==1,"an implausible holder count: the stock message only");

    Check(Decide(1,2,true,true,0).count==1 && Decide(1,2,false,true,0).count==0 && Decide(1,2,true,false,0).count==0,
          "Decide: remote operator and authority both needed");
    std::printf("gunner_recoil: %d checks passed\n",checks);
    return 0;
}
