// Execute the actual NPC seat predicate and input producer with stand-in memory and a recording native gun call.
#define main ExistingNpcCoreChecks
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
int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    // Preserve the signature and balance its six pushes before tail-calling the recorder.
    std::memcpy(image+kSeatFire,kSeatFireSig,sizeof(kSeatFireSig));
    const unsigned char pop[]={0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5F,0x5E,0x5D};
    std::memcpy(image+kSeatFire+sizeof(kSeatFireSig),pop,sizeof(pop));
    Jump(kSeatFire+sizeof(kSeatFireSig)+sizeof(pop),reinterpret_cast<const void*>(&GunFireRec));
    SetupGunner(true);NpcGunnersInput(vehicle);
    Expect(fired==1 && lastSeat==1,"host Dummy fires its gunner seat with a remote vehicle driver");
    Expect(At<float>(SeatAt(vehicle,1),0x2E4)==1.0f,"native gunner call supplies the actual seat trigger");
    gunnerEnemy=nullptr;NpcGunnersInput(vehicle);
    Expect(fired==1 && At<float>(SeatAt(vehicle,1),0x2E4)==0.0f,"target loss releases last trigger instead of firing forever");
    SetupGunner(true);host=false;NpcGunnersInput(vehicle);
    Expect(fired==0,"another copy's Dummy cannot create a second native shot");
    SetupGunner(false);host=false;NpcGunnersInput(vehicle);
    Expect(fired==1,"a client's owned NPC soldier fires even while vehicle authority is remote");
    SetupGunner(false,true);NpcGunnersInput(vehicle);
    Expect(fired==0,"remote NPC soldier is driven by its own machine");
    SetupGunner(false);human[edf::kHumanPlayer]=1;Put<void*>(human,edf::kHumanPad,human);NpcGunnersInput(vehicle);
    Expect(fired==0,"local human gunner input is never replaced");
    Put<std::uint16_t>(human,0x128,1);NpcGunnersInput(vehicle);
    Expect(fired==0,"remote human gunner input is never replaced");
    SetupGunner(true);config.enabled=false;NpcGunnersInput(vehicle);Expect(fired==0,"global disable stays effective online");
    SetupGunner(false);config.npcBoarding=false;NpcGunnersInput(vehicle);Expect(fired==0,"NPC soldier still requires its boarding feature");
    SetupGunner(true);sessionOn=false;host=false;NpcGunnersInput(vehicle);Expect(fired==1,"offline Dummy behavior is preserved");
    SetupGunner(true);host=false;Put<std::uint16_t>(vehicle,0x128,0);vehicleCopyOwner=online::kCopyHere;NpcGunnersInput(vehicle);
    Expect(fired==1,"unregistered caller-owned copy uses its recorded owner");
    SetupGunner(true);Put<std::uint16_t>(vehicle,0x128,0);vehicleCopyOwner=online::kCopyElsewhere;NpcGunnersInput(vehicle);
    Expect(fired==0,"unregistered copy owned elsewhere does not duplicate gunner fire");
    SetupGunner(false);NpcGunnersInput(vehicle);config.npcGunners=false;NpcGunnersInput(vehicle);
    Expect(fired==1 && At<float>(SeatAt(vehicle,1),0x2E4)==0.0f,"switching gunners off reclaims owned inputs");
    SetupGunner(false);NpcGunnersInput(vehicle);Put<std::uint16_t>(human,0x128,1);NpcGunnersInput(vehicle);
    Expect(fired==1 && At<float>(SeatAt(vehicle,1),0x2E4)==0.0f,"ownership transfer releases this machine's trigger");
    SetupGunner(false);NpcGunnersInput(vehicle);human[edf::kHumanPlayer]=1;Put<void*>(human,edf::kHumanPad,human);
    config.npcGunners=false;NpcGunnersInput(vehicle);
    Expect(At<float>(SeatAt(vehicle,1),0x2E4)==1.0f,"cleanup never rewrites a human-controlled seat");
    SetupGunner(false);NpcGunnersInput(vehicle);Put<float>(SeatAt(vehicle,1),0x2D0,0.75f);config.npcGunners=false;NpcGunnersInput(vehicle);
    Expect(At<float>(SeatAt(vehicle,1),0x2D0)==0.75f,"cleanup preserves inputs subsequently changed by another owner");
    SetupGunner(false);NpcGunnersInput(vehicle);Put<void*>(vehicle,kSelfCtrl,vehicle+0x2800);config.npcGunners=false;NpcGunnersInput(vehicle);
    Expect(At<float>(SeatAt(vehicle,1),0x2E4)==1.0f,"reused vehicle identity does not inherit old input cleanup");
    SetupGunner(false);
    unsigned char alternate[0x1600]{},alternateHolder[0x80]{};
    unsigned char* both[]={holder,alternateHolder};
    Put<void*>(alternateHolder,kHolderWeapon,alternate);Put<float>(alternate,kArmReach,100.0f);
    Put<void*>(SeatAt(vehicle,1),kSeatWeapons,both);Put<std::uint64_t>(SeatAt(vehicle,1),kSeatWeaponCount,2);
    chosenGunnerWeapon=alternate;chosenGunnerFire=PayloadFire::secondary;
    NpcGunnersInput(vehicle);
    Expect(aimedWeapon==alternate,"native aim reads the selected real holder rather than the old primary ballistics");
    Expect(At<void*>(SeatAt(vehicle,1),kSeatWeapons)==both && both[0]==holder,"native aiming restores original holder list and order");
    Expect(At<float>(SeatAt(vehicle,1),0x2E4)==0.0f && At<float>(SeatAt(vehicle,1),0x2E0)==1.0f,"selected secondary uses native left trigger instead of firing primary");
    gunnerEnemy=nullptr;NpcGunnersInput(vehicle);
    Expect(At<float>(SeatAt(vehicle,1),0x2E0)==0.0f,"target loss also releases owned secondary trigger");
    ResetGunnerInputs();
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("npc_gunners_online: %d failures\n",failures);
    return failures ? 1 : 0;
}
