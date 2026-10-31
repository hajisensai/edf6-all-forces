// Execute production assignment/boarding with native-layout objects, no game process.
#define main ExistingNpcCoreChecks
#include "../tools/npc_core_check.cpp"
#undef main
namespace crew { namespace {
void __fastcall RideMoveRec(void* h,SharedRef* ref,int index) {
    auto* old=At<unsigned char*>(h,kHumanSeat);
    if(old){Put<void*>(old,kSeatRider,nullptr);Put<void*>(old,kSeatRiderCtrl,nullptr);}
    auto* dest=SeatAt(static_cast<unsigned char*>(ref->obj),static_cast<unsigned>(index));
    Put<void*>(dest,kSeatRider,h);Put<void*>(dest,kSeatRiderCtrl,At<void*>(h,kSelfCtrl));
    Put<void*>(h,kHumanSeat,dest);Put<void*>(h,kHumanRiding,ref->obj);
    --*reinterpret_cast<int*>(static_cast<unsigned char*>(ref->ctrl)+8);++rides;
}
int originalThinks=0;
void __fastcall ThinkRecord(void*,const float*) {++originalThinks;}
void __fastcall MovingThinkRecord(void* h,const float*) {
    ++originalThinks;Put<float>(h,kMoveX,1.0f);Put<float>(h,kMoveZ,0.5f);
}
void CrewSetup() {
    Reset();config.scriptNpcSettleSec=0;
    Put<void*>(human,kLeader,nullptr);Put<void*>(human,kSelfCtrl,human+0x2100);
    Put<void*>(other,kSelfCtrl,other+0x2100);Put<unsigned>(other,kHumanMask,1);
    Put<int>(human+0x2100,8,1);Put<int>(other+0x2100,8,1);
    Put<unsigned>(seats,kSeatClass,1);Put<unsigned>(seats,kSeatEnable,1);
    Put<int>(vehicle,kTeam,kTeamVehicle);
    world.friends=2;world.frObject[0]=human;world.frObject[1]=other;
}
// --- Several NPC-driven vehicles at once (the user 2026-10-31 #3: only one vehicle's NPC crew fights) ---
// Two stand-in CarBase tanks whose AI pass (slot 7, kCarAiPass's real prologue then a recorder), AI action (slot 72) and
// the object activation 0x548D50 (its real prologue then a recorder) count per vehicle what each driver's Think ran.
constexpr int kCars=2;
alignas(16) unsigned char cars[kCars][0x3000]{};
alignas(16) unsigned char carSeats[kCars][edf::kSeatStride*3]{};
unsigned char carCtrl[kCars][0x10]{};
void* carVtable[84]{};
int passes[kCars]{},actions[kCars]{},activations[kCars]{};
bool refuseActivation=false;
int CarIndex(const void* v) { for(int i=0;i<kCars;++i)if(v==cars[i])return i; return -1; }
void __fastcall PassRec(void* v,const float*) { const int i=CarIndex(v);if(i>=0)++passes[i]; }
void __fastcall ActionRec(void* v,int edx,const float*,void*) {
    const int i=CarIndex(v);
    if(i>=0 && edx==1 && static_cast<unsigned char*>(v)[kCarInAction]==1)++actions[i];
}
void __fastcall ActivateRec(void* v) {
    const int i=CarIndex(v);if(i>=0)++activations[i];
    if(!refuseActivation)static_cast<unsigned char*>(v)[kActiveFlag]=1;
}
// The real prologues kept (the plugin checks them) and balanced before the recorder.
void InstallCarNatives() {
    std::memcpy(image+kCarAiPass,kCarAiPassSig,sizeof(kCarAiPassSig));   // ...call 0x54A000: the recorder
    const unsigned char passTail[]={0x48,0x83,0xC4,0x20,0x5F,0x48,0x8B,0x5C,0x24,0x08,0x48,0x8B,0x74,0x24,0x10,0xC3};
    std::memcpy(image+kCarAiPass+sizeof(kCarAiPassSig),passTail,sizeof(passTail));
    Jump(0x54A000,reinterpret_cast<const void*>(&PassRec));
    Jump(kCarAi,reinterpret_cast<const void*>(&ActionRec));
    std::memcpy(image+kActivate,kActivateSig,sizeof(kActivateSig));
    const unsigned char activateTail[]={0x48,0x83,0xC4,0x20,0x5B};   // add rsp,20h; pop rbx
    std::memcpy(image+kActivate+sizeof(kActivateSig),activateTail,sizeof(activateTail));
    Jump(kActivate+sizeof(kActivateSig)+sizeof(activateTail),reinterpret_cast<const void*>(&ActivateRec));
    carVtable[kSlotSix]=image+kCarSix;carVtable[kSlotAiPass]=image+kCarAiPass;carVtable[kSlotCarAi]=image+kCarAi;
    carVtable[kSlotSeatFire]=image+kSeatFire;
}
void MakeCars() {
    for(int i=0;i<kCars;++i) {
        std::memset(cars[i],0,sizeof(cars[i]));std::memset(carSeats[i],0,sizeof(carSeats[i]));std::memset(carCtrl[i],0,sizeof(carCtrl[i]));
        Put<void*>(cars[i],0,carVtable);Put<void*>(cars[i],kSeats,carSeats[i]);Put<std::uint64_t>(cars[i],kSeatCount,3);
        Put<void*>(cars[i],kSelfCtrl,carCtrl[i]);Put<int>(carCtrl[i],8,1);Put<int>(cars[i],kTeam,kTeamVehicle);
        cars[i][kListFlags]=0x17;   // not on the AI list (bit 3), as measured for a boarded call-in
        for(unsigned k=0;k<3;++k){Put<unsigned>(carSeats[i]+k*edf::kSeatStride,kSeatClass,1);Put<unsigned>(carSeats[i]+k*edf::kSeatStride,kSeatEnable,1);}
        passes[i]=actions[i]=activations[i]=0;
    }
    refuseActivation=false;
}
void Sit(unsigned char* h,int car,unsigned seat) {
    unsigned char* s=SeatAt(cars[car],seat);
    Put<void*>(s,kSeatRider,h);Put<void*>(s,kSeatRiderCtrl,At<void*>(h,kSelfCtrl));
    Put<void*>(h,kHumanSeat,s);Put<void*>(h,kHumanRiding,cars[car]);Put<void*>(h,kHumanVehicleCtrl,carCtrl[car]);
}
void Leave(unsigned char* h) {
    if(auto* s=At<unsigned char*>(h,kHumanSeat)){Put<void*>(s,kSeatRider,nullptr);Put<void*>(s,kSeatRiderCtrl,nullptr);}
    Put<void*>(h,kHumanSeat,nullptr);Put<void*>(h,kHumanRiding,nullptr);Put<void*>(h,kHumanVehicleCtrl,nullptr);
}
const float frameStep=1.0f/60.0f;
void ThinkOf(unsigned char* h) { ThinkHook<0>(h,&frameStep); }
DriveStep LastStep(int car) { const NpcVehicleAi* r=VehicleAiOf(cars[car],false);return r ? r->last : DriveStep::count; }
// NPC gunner seats (NpcGunnersInput): the native seat aim 0x65F6F0's prologue, then a recorder that pulls the trigger.
unsigned char gunWeapon[0x1600]{},gunHolder[0x80]{};
unsigned char* gunHolders[1]={gunHolder};
int seatFires[3]{};
void __fastcall SeatFireRec(void* v,int seat,const void*) {
    if(seat>=0 && seat<3)++seatFires[seat];
    Put<float>(SeatAt(static_cast<unsigned char*>(v),static_cast<unsigned>(seat)),0x2E4,1.0f);
}
void InstallSeatFire() {
    std::memcpy(image+kSeatFire,kSeatFireSig,sizeof(kSeatFireSig));
    const unsigned char pop[]={0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5F,0x5E,0x5D};
    std::memcpy(image+kSeatFire+sizeof(kSeatFireSig),pop,sizeof(pop));
    Jump(kSeatFire+sizeof(kSeatFireSig)+sizeof(pop),reinterpret_cast<const void*>(&SeatFireRec));
    Put<void*>(gunHolder,kHolderWeapon,gunWeapon);Put<float>(gunWeapon,kArmReach,100.0f);
}
void Arm(int car,unsigned seat) {
    unsigned char* s=SeatAt(cars[car],seat);
    Put<void*>(s,kSeatWeapons,gunHolders);Put<std::uint64_t>(s,kSeatWeaponCount,1);
}
unsigned char playerSoldier[0x2200]{};
}}
int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    Jump(kRideVehicle,reinterpret_cast<const void*>(&RideRec));
    CrewSetup();world.friends=0;
    Expect(!NpcRequestCrew(vehicle) && rides==0,"empty roster never fabricates or immediately seats a rider");
    CrewSetup();
    Expect(NpcRequestCrew(vehicle) && rides==0,"request assigns real soldiers without teleporting into seats");
    Soldier* a=Entry(human,now);Soldier* b=Entry(other,now);
    Expect(a->boardSeat==0 && b->boardSeat==1,"driver assigned before other seats");
    Expect(NpcRequestCrew(vehicle) && a->boardSeat==0 && b->boardSeat==1,"pending request preserves assignments");
    const float distant[3]={100,0,0};Board(*a,human,distant,now);
    Expect(rides==0 && a->boardV.Is(vehicle),"real soldier must reach native entrance before boarding");
    const float nearby[3]={0,0,0};Board(*a,human,nearby,now);
    Expect(rides==1 && At<int>(ctrl,8)==1,"driver uses real Human RideVehicle shared ownership contract");
    CrewSetup();Put<unsigned>(human,kObjectFlags,kFixPosition);world.friends=1;
    Expect(!NpcRequestCrew(vehicle),"mission-script soldier is not stolen for crew");
    CrewSetup();human[edf::kHumanPlayer]=1;Put<void*>(human,edf::kHumanPad,human);world.friends=1;
    Expect(!NpcRequestCrew(vehicle),"human player never recruited as automatic crew");
    CrewSetup();sessionOn=true;host=false;
    Expect(!NpcRequestCrew(vehicle),"non-host cannot assign crew to registered vehicle");
    CrewSetup();AssignBoard(vehicle,human,now);a=Entry(human,now);sessionOn=true;host=false;
    Board(*a,human,nearby,now);
    Expect(rides==0 && !a->boardV,"lost authority cancels walking assignment before native boarding");
    CrewSetup();world.friends=1;Put<float>(human,kPosition,151);
    Expect(!NpcRequestCrew(vehicle),"automatic recruitment does not pull soldiers from arbitrary map distance");
    CrewSetup();unsigned char* explicitCrew[]={human,other};
    Expect(NpcBoardCrew(vehicle,explicitCrew,2)==2,"support supplied real crew uses same priority and walking path");
    CrewSetup();Put<void*>(seats,kSeatRider,human);Put<void*>(seats,kSeatRiderCtrl,human+0x2100);
    Expect(NpcDriver(vehicle),"real soldier driver activates vehicle pilot routines");
    human[edf::kHumanPlayer]=1;Put<void*>(human,edf::kHumanPad,human);
    Expect(!NpcDriver(vehicle),"human driver never mistaken for AI pilot");
    CrewSetup();
    unsigned char fourSeats[edf::kSeatStride*4]{};
    Put<void*>(vehicle,kSeats,fourSeats);Put<std::uint64_t>(vehicle,kSeatCount,4);
    for(unsigned k=0;k<4;++k){Put<unsigned>(fourSeats+k*edf::kSeatStride,kSeatClass,1);Put<unsigned>(fourSeats+k*edf::kSeatStride,kSeatEnable,1);}
    Put<std::uint64_t>(fourSeats+2*edf::kSeatStride,kSeatWeaponCount,1);
    NpcBoardCrew(vehicle,explicitCrew,2);
    Expect(Entry(human,now)->boardSeat==0 && Entry(other,now)->boardSeat==2,
           "actual armed seat outranks lower numbered passenger seat");
    CrewSetup();Put<void*>(seats,kSeatRider,human);Put<void*>(seats,kSeatRiderCtrl,human+0x2100);
    Put<void*>(human,kHumanSeat,seats);Put<void*>(human,kHumanRiding,vehicle);
    Expect(OwnsNpcSeatInput(seats),"only real local driver owns preserved seat input");
    config.enabled=false;Expect(!OwnsNpcSeatInput(seats),"plugin disable restores original Human reset");config.enabled=true;
    Jump(kRideVehicle,reinterpret_cast<const void*>(&RideMoveRec));
    Expect(NpcMoveSeat(vehicle,0,1) && At<void*>(human,kHumanSeat)==SeatAt(vehicle,1) && At<int>(ctrl,8)==1,
           "yielding driver uses Human seat transition and balances ownership");
    Expect(!OwnsNpcSeatInput(SeatAt(vehicle,1)),"gunner seat does not retain driver input policy");
    human[edf::kHumanPlayer]=1;Put<void*>(human,edf::kHumanPad,human);
    Expect(!NpcMoveSeat(vehicle,1,0),"human cannot be moved by NPC yield path");
    CrewSetup();heldSupportActor=human;world.friends=1;
    Expect(!NpcRequestCrew(vehicle),"held deployment actor cannot be recruited by a different vehicle");
    CrewSetup();nextThink[0]=&ThinkRecord;heldSupportActor=human;originalThinks=0;
    Put<float>(human,kMoveX,1);human[kTrigger]=1;ThinkHook<0>(human,nullptr);
    Expect(originalThinks==0 && At<float>(human,kMoveX)==0 && human[kTrigger]==0,
           "held support actor cannot run native AI, move or shoot before all peers exist");
    heldSupportActor=nullptr;ThinkHook<0>(human,nullptr);
    Expect(originalThinks==1,"activated support actor resumes native AI");
    for(bool wrongFloor:{false,true}) {
        CrewSetup();world.friends=1;originalThinks=0;nextThink[0]=&MovingThinkRecord;
        human[kListFlags]|=kInAiList;doorAt[0]=10.0f;rayOn=wrongFloor;
        Expect(NpcRequestCrew(vehicle),"assign a real driver before validating the walking entrance");
        // MapFloorRay either rejects the door altogether or returns the fixture's (0,0,30), outside this door's reach.
        ThinkHook<0>(human,nullptr);
        Expect(originalThinks==1 && rides==0 && Entry(human,now)->boardV.Is(vehicle),
               "native Think ran, rejected entrance keeps a pending boarding request without teleporting");
        Expect(At<float>(human,kMoveX)==0.0f && At<float>(human,kMoveZ)==0.0f,
               "rejected boarding floor must not leak the native Think's old movement around the navigation gate");
    }
    CrewSetup();Put<void*>(human,kHumanSeat,seats);Put<void*>(human,kHumanRiding,vehicle);
    Jump(kRideVehicle,reinterpret_cast<const void*>(&RideMoveRec));
    Expect(NpcRestoreMissionSeat(vehicle,human) && At<void*>(seats,kSeatRider)==human,
           "matched snapshot seat is restored through native Human ride without new actor");
    CrewSetup();Put<void*>(human,kHumanSeat,seats+1);Put<void*>(human,kHumanRiding,vehicle);
    Expect(!NpcRestoreMissionSeat(vehicle,human) && rides==0,
           "unknown stale seat pointer is never passed into native exit or restore");
    // --- Several NPC-driven vehicles (feedback 2026-10-31 #3) ---
    InstallCarNatives();InstallSeatFire();
    CrewSetup();MakeCars();nextThink[0]=&ThinkRecord;
    Sit(human,0,0);Sit(other,1,0);
    ThinkOf(human);ThinkOf(other);
    Expect(passes[0]==1 && passes[1]==1 && actions[0]==1 && actions[1]==1,
           "two NPC drivers in the same frame: each vehicle runs its own AI pass and action");
    Expect(activations[0]==1 && activations[1]==1 && cars[0][kActiveFlag]==1 && cars[1][kActiveFlag]==1,
           "each inactive boarded vehicle is activated (+0x4A0) before its first pass, not only the first one");
    Expect(LastStep(0)==DriveStep::ran && LastStep(1)==DriveStep::ran,"both vehicles' records say their pass ran");
    ++frame;now+=16;ThinkOf(other);ThinkOf(human);
    Expect(passes[0]==2 && passes[1]==2 && activations[0]==1 && activations[1]==1,
           "the other order next frame: both run again, an active vehicle is not activated twice");
    ThinkOf(human);
    Expect(passes[0]==2 && LastStep(0)==DriveStep::sameFrame,"a second Think in one frame does not run that vehicle's pass twice");
    ThinkOf(other);
    Expect(passes[1]==2,"...nor the other vehicle's: the once-a-frame rule is kept per vehicle");
    ++frame;now+=16;Leave(human);ThinkOf(human);ThinkOf(other);
    Expect(passes[0]==2 && passes[1]==3,"a driver who got out runs nothing; the other vehicle goes on");
    ++frame;now+=16;Sit(human,0,0);ThinkOf(human);
    Expect(passes[0]==3 && actions[0]==3,"the same soldier back at the wheel: its vehicle's pass resumes");
    ++frame;now+=16;cars[1][kListFlags]|=kInAiList;ThinkOf(other);
    Expect(passes[1]==3 && LastStep(1)==DriveStep::onAiList,"negative: a vehicle on the AI list keeps its own pass");
    cars[1][kListFlags]&=static_cast<unsigned char>(~kInAiList);
    ++frame;now+=16;Leave(other);Sit(other,0,1);ThinkOf(other);
    Expect(passes[0]==3,"negative: an NPC gunner's Think never runs seat 0's AI");
    Leave(other);
    CrewSetup();MakeCars();nextThink[0]=&ThinkRecord;refuseActivation=true;
    Sit(other,1,0);++frame;now+=16;ThinkOf(other);
    Expect(activations[1]==1 && passes[1]==1 && actions[1]==1 && LastStep(1)==DriveStep::notActive,
           "a vehicle the stock refuses to activate still runs its pass and action (its moves), and says why");
    {
        const NpcVehicleAi* rr=VehicleAiOf(cars[1],false);
        Expect(rr && rr->refusalLogged && !cars[1][kActiveFlag],"...the refusal is logged and the vehicle left inactive");
    }
    ++frame;now+=16;ThinkOf(other);
    Expect(passes[1]==2 && actions[1]==2 && LastStep(1)==DriveStep::notActive,"...and next frame too, once a frame");
    Leave(other);
    // A player drives, two NPC gunners: both seats aim and pull their triggers in the same frame.
    CrewSetup();MakeCars();std::memset(playerSoldier,0,sizeof(playerSoldier));
    Put<void*>(playerSoldier,0,image+kSoldiers[0].vtable);Put<int>(playerSoldier,kTeam,kTeamFriend);
    Put<void*>(playerSoldier,kSelfCtrl,playerSoldier+0x2100);Put<int>(playerSoldier+0x2100,8,1);
    playerSoldier[edf::kHumanPlayer]=1;Put<void*>(playerSoldier,edf::kHumanPad,playerSoldier);
    Sit(playerSoldier,0,0);Sit(human,0,1);Sit(other,0,2);Arm(0,1);Arm(0,2);
    nextThink[0]=&ThinkRecord;ThinkOf(playerSoldier);ThinkOf(human);ThinkOf(other);
    Expect(passes[0]==0,"negative: a player's vehicle never runs the NPC driver's AI pass");
    seatFires[1]=seatFires[2]=0;gunnerEnemy=other;
    NpcGunnersInput(cars[0]);
    const NpcVehicleAi* gr=VehicleAiOf(cars[0],false);
    Expect(seatFires[1]==1 && seatFires[2]==1 && At<float>(SeatAt(cars[0],1),0x2E4)==1.0f && At<float>(SeatAt(cars[0],2),0x2E4)==1.0f,
           "player driving + two NPC gunners: both gunner seats aim and pull their triggers");
    Expect(gr && gr->gunner[1]==GunnerStep::trigger && gr->gunner[2]==GunnerStep::trigger,"both gunner seats recorded as firing");
    gunnerEnemy=nullptr;NpcGunnersInput(cars[0]);gr=VehicleAiOf(cars[0],false);
    Expect(seatFires[1]==1 && seatFires[2]==1 && At<float>(SeatAt(cars[0],1),0x2E4)==0.0f && gr && gr->gunner[1]==GunnerStep::noTarget,
           "negative: with no enemy neither gunner fires and the record says no target");
    Leave(playerSoldier);Leave(human);Leave(other);ResetGunnerInputs();
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("real_npc_crew: %d failures\n",failures);
    return failures ? 1 : 0;
}

