// Driver hook consumes the original AI target once; it never becomes a second seat-0 fire loop.
#define main ExistingGunnerOnlineMain
#include "../tools/npc_core_check.cpp"
#undef main
namespace crew {
namespace {
int fired=0,lastSeat=-1;
const void* aimedWeapon=nullptr;
unsigned char weapon[0x1600]{},holder[0x80]{};
unsigned char* holders[1]={holder};
void __fastcall GunFireRec(void* v,int seat,const void*) {
    ++fired;lastSeat=seat;
    auto* s=SeatAt(static_cast<unsigned char*>(v),static_cast<unsigned>(seat));
    const auto current=At<unsigned char**>(s,kSeatWeapons);
    aimedWeapon=current ? At<void*>(current[0],kHolderWeapon) : nullptr;
    Put<float>(s,0x2D0,0.25f);Put<float>(s,0x2D4,-0.5f);Put<float>(s,0x2E4,1.0f);
}
void SetupGunner(bool dummy,bool remote=false) {
    Reset();fired=0;lastSeat=-1;chosenGunnerWeapon=nullptr;chosenGunnerFire=PayloadFire::primary;
    sessionOn=true;host=true;
    Put<void*>(vehicle,0,image+0x1000);
    Put<void*>(image+0x1000,kSlotSeatFire*8,image+kSeatFire);
    Put<std::uint16_t>(vehicle,0x128,1); // vehicle itself is another machine's, which must NOT suppress this gunner
    Put<void*>(human,kSelfCtrl,ctrl);
    if(dummy)Put<void*>(human,0,image+edf::kDummyRiderVtable);
    Put<std::uint16_t>(human,0x128,remote ? 1 : dummy ? 0 : 2);
    unsigned char* seat=SeatAt(vehicle,1);
    Put<void*>(seat,kSeatRider,human);Put<void*>(seat,kSeatRiderCtrl,ctrl);
    Put<void*>(seat,kSeatWeapons,holders);Put<std::uint64_t>(seat,kSeatWeaponCount,1);
    Put<void*>(holder,kHolderWeapon,weapon);Put<float>(weapon,kArmReach,100.0f);
    gunnerEnemy=other;
}
}
}

namespace crew {
const void* driverTarget=nullptr;
int driverCalls=0;
void __fastcall DriverRec(void* v,int seat,const void* target) {
    ++driverCalls;driverTarget=target;GunFireRec(v,seat,target);
}
void SetupDriver() {
    SetupGunner(false);ResetDriverPayload();driverCalls=0;driverTarget=nullptr;suppressGunnerChoice=false;
    auto from=SeatAt(vehicle,1),to=SeatAt(vehicle,0);
    std::memcpy(to,from,edf::kSeatStride);std::memset(from,0,edf::kSeatStride);
    Put<float>(human,0x2F8,100);Put<float>(other,kPosition+8,20);
    Put<float>(to,0x2C0,0.4f);Put<float>(to,0x2C4,-0.8f);
    driverPayloadReady=true;
}
}
int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    Expect(!InstallDriverPayload(),"unknown original callsites refuse patching");
    std::memcpy(image+kSeatFire,kSeatFireSig,sizeof(kSeatFireSig));
    // SeatFire signature includes pushes rbp/rsi/rdi/r12/r13/r14. Match all six.
    const unsigned char unwind[]={0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5F,0x5E,0x5D};
    std::memcpy(image+kSeatFire+sizeof(kSeatFireSig),unwind,sizeof(unwind));
    Jump(kSeatFire+sizeof(kSeatFireSig)+sizeof(unwind),reinterpret_cast<const void*>(&DriverRec));
    for(int i=0;i<2;++i)std::memcpy(image+kDriverAimCalls[i]-11,kDriverAimContext[i],sizeof(kDriverAimContext[i]));
    Expect(InstallDriverPayload() && InstallDriverPayload(),"both native driver aim callsites install idempotently");
    for(const auto rva:kDriverAimCalls) {
        Expect(image[rva]==0xE8 && image[rva+5]==0x90,"six-byte indirect call becomes one near call plus NOP");
    }
    const auto thunk=image+kDriverAimCalls[0]+5+At<std::int32_t>(image+kDriverAimCalls[0],1);
    const auto hooked=reinterpret_cast<SeatFireFn>(thunk);
    SetupDriver();unsigned char* original=At<unsigned char*>(SeatAt(vehicle,0),kSeatWeapons);
    hooked(vehicle,0,other);
    Expect(driverCalls==1 && driverTarget==other && lastSeat==0,"same authoritative stock target reaches exactly one native driver aim");
    Expect(At<float>(SeatAt(vehicle,0),0x2C0)==0.4f && At<float>(SeatAt(vehicle,0),0x2C4)==-0.8f,"driver steering and throttle remain unchanged");
    Expect(At<void*>(SeatAt(vehicle,0),kSeatWeapons)==original,"temporary holder list restored after driver aim");
    NpcGunnersInput(vehicle);
    Expect(driverCalls==1 && At<float>(SeatAt(vehicle,0),0x2E4)==1.0f,"input pass preserves fresh driver trigger without invoking fire again");
    SetupDriver();chosenGunnerFire=PayloadFire::secondary;hooked(vehicle,0,other);
    Expect(driverCalls==1 && At<float>(SeatAt(vehicle,0),0x2E4)==0 && At<float>(SeatAt(vehicle,0),0x2E0)==1,"selected secondary moves the single native fire intent to secondary");
    suppressGunnerChoice=true;hooked(vehicle,0,other);
    Expect(driverCalls==2 && At<float>(SeatAt(vehicle,0),0x2E4)==0 && At<float>(SeatAt(vehicle,0),0x2E0)==0,"no usable loaded weapon suppresses both triggers while native tracking runs once");
    suppressGunnerChoice=false;hooked(vehicle,0,nullptr);
    Expect(driverCalls==3 && !driverTarget && At<float>(SeatAt(vehicle,0),0x2E0)==0,"stock target loss does not invent another target or keep firing");
    SetupDriver();hooked(vehicle,0,other);human[kDead]=1;NpcGunnersInput(vehicle);
    Expect(At<float>(SeatAt(vehicle,0),0x2E4)==0,"driver death releases last owned fire intent");
    SetupDriver();hooked(vehicle,0,other);config.stockStores=false;NpcGunnersInput(vehicle);
    Expect(At<float>(SeatAt(vehicle,0),0x2E4)==0,"turning stores off releases last owned intent");config.stockStores=true;
    SetupDriver();human[edf::kHumanPlayer]=1;Put<void*>(human,edf::kHumanPad,human);
    chosenGunnerFire=PayloadFire::secondary;hooked(vehicle,0,other);
    Expect(driverCalls==1 && At<float>(SeatAt(vehicle,0),0x2E4)==1 && At<float>(SeatAt(vehicle,0),0x2E0)==0,"player takeover follows original dispatch, never NPC-selected secondary");
    SetupDriver();Put<std::uint16_t>(human,0x128,1);chosenGunnerFire=PayloadFire::secondary;hooked(vehicle,0,other);
    Expect(driverCalls==1 && At<float>(SeatAt(vehicle,0),0x2E0)==0,"remote driver copy cannot choose or redirect locally");
    SetupDriver();Put<void*>(SeatAt(vehicle,0),kSeatRiderCtrl,nullptr);hooked(vehicle,0,other);
    Expect(driverCalls==1 && driverWrites.empty(),"ordinary empty vehicle remains original dispatch with no fabricated crew state");
    ResetDriverPayload();VirtualFree(image,0,MEM_RELEASE);
    std::printf("npc driver payload: %d failures\n",failures);return failures?1:0;
}
