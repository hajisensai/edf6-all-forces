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
void CrewSetup() {
    Reset();config.scriptNpcSettleSec=0;
    Put<void*>(human,kLeader,nullptr);Put<void*>(human,kSelfCtrl,human+0x2100);
    Put<void*>(other,kSelfCtrl,other+0x2100);Put<unsigned>(other,kHumanMask,1);
    Put<int>(human+0x2100,8,1);Put<int>(other+0x2100,8,1);
    Put<unsigned>(seats,kSeatClass,1);Put<unsigned>(seats,kSeatEnable,1);
    Put<int>(vehicle,kTeam,kTeamVehicle);
    world.friends=2;world.frObject[0]=human;world.frObject[1]=other;
}
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
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("real_npc_crew: %d failures\n",failures);
    return failures ? 1 : 0;
}

