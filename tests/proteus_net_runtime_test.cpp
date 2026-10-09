// The production Proteus network paths (src/proteus_net.inc) with the frame fixture: a remote driver's control replayed
// here (piles, seats, the local barrier facing the driver's way), the shield's HP counted by the registered owner and
// published (defense), taken by the other copies, the field applied on what this machine owns, expiry / epochs / death.
#define main PreviousProteusFrameChecks
#include "proteus_frame_test.cpp"
#undef main
namespace {
using namespace crew;
unsigned char* fieldAlly=nullptr;
void __fastcall WalkField(void*,std::int32_t,void* functor){if(fieldAlly)crew::FieldVisit(functor,fieldAlly);}
void __fastcall Compose(void* inst,const void* transform){
    const auto bones=At<unsigned char*>(inst,kInstBones506);
    for(int i=0;i<2;++i)exhaust::Mul(reinterpret_cast<const float*>(bones+i*kBoneStride+kBoneLocal506),
        static_cast<const float*>(transform),reinterpret_cast<float*>(bones+i*kBoneStride+kBoneWorld506));
}
proteus_net::State Control(std::uint32_t sequence) {
    proteus_net::State s;s.sender=7;s.sequence=sequence;s.controller=42;
    s.flags=proteus_net::kActive|proteus_net::kShieldOn|proteus_net::kTwoSeats;s.mode=1;s.stagger=.75f;
    s.nose[0]=1;s.nose[2]=0;s.barrier=.1f;s.barrierShare=.5f;
    return s;
}
}  // namespace

int main(){
    Setup();
    if(!image)return 2;
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto fn=reinterpret_cast<std::uintptr_t>(&Compose);std::memcpy(jump+2,&fn,8);std::memcpy(image+kSetModelWorld,jump,12);
    netSession=true;netAuthority=false;
    Put<std::uint16_t>(vehicle,0x128,2);   // registered, owned here: the host's copy, a guest drives
    alignas(16) unsigned char bones[2*kBoneStride]{},skeleton[0x70]{},binds[2*0xD0]{};
    testPoseBones=bones;Put<void*>(vehicle+kModelInst506,0,skeleton);Put<void*>(vehicle+kModelInst506,kInstBones506,bones);
    Put<void*>(skeleton,0x58,binds);Put<int>(skeleton,0x68,2);
    for(int i=0;i<2;++i){proteus::pose::Identity(reinterpret_cast<float*>(binds+i*0xD0+0x20));proteus::pose::Identity(reinterpret_cast<float*>(bones+i*kBoneStride+kBoneLocal506));}
    // The guest driver's control.
    proteus_net::State s=Control(1);
    ProteusNetReceived(vehicle,s);Unit* u=UnitOf(vehicle,false);
    Check(u && u->net.remote && u->st.mode==proteus::Mode::deploying && u->st.shieldOn,"the owner replays the remote driver's stance and shield switch");
    Check(u->st.shield==1,"the driver's control never sets the shield's HP (the owner counts it)");
    const int fires=emcFires;Tick();ProteusModelPose(vehicle);
    Check(std::fabs(At<float>(bones,kBoneLocal506+52)-kPiles[0].retract*.5f)<1e-4f,"remote half deployment moves the real pile bone");
    Check(At<float>(vehicle,kWalk)==10 && At<float>(vehicle,kJump)==8,"a replica never runs the legs");
    Check(At<int>(seats+2*kSeatStride,kSeatClassMask)==0,"the replicated two-seat layout closes the extra seats");
    Check(emcFires==fires+1,"the replica raises its own local barrier for the replayed shield");
    alignas(16) static unsigned char barrier[0x1600];
    FreshBarrier(barrier,vehicle,kBarrierSegmentCount);BarrierStep(barrier);
    const float full=.5f*7500;
    Check(u->barrier.obj==barrier && At<float>(barrier,kBarrierHp)==full,"its HP is the driver's configured share of the hull");
    Check(At<float>(barrier,kBarrierMatrix+32)==1 && At<float>(barrier,kBarrierMatrix+40)==0,"it faces the driver's replayed facing");
    // The hits settle here: the owner counts and publishes them at once.
    const int sent=defenseSends;
    Put<float>(barrier,kBarrierHp,full*.25f);BarrierStep(barrier);Tick();
    Check(std::fabs(u->st.shield-.25f)<1e-5f && defenseSends>sent && std::fabs(lastDefense.barrier-.25f)<1e-5f,"the owner publishes the barrier's drained HP");
    s=Control(2);s.barrier=1;ProteusNetReceived(vehicle,s);
    Check(std::fabs(u->st.shield-.25f)<1e-5f,"a later pilot packet cannot restore the shield's HP");
    // Switched off by the driver: the local barrier goes, the owner refills it.
    s=Control(3);s.flags&=~proteus_net::kShieldOn;s.mode=2;s.stagger=0;ProteusNetReceived(vehicle,s);Tick();BarrierStep(barrier);
    Check(!u->barrier.obj && At<float>(barrier,kBarrierHp)==0 && barrier[kBarrierNoEcho],"the driver's switch takes the local barrier down (no echo)");
    for(int i=0;i<50;++i){s=Control(4+i);s.flags&=~proteus_net::kShieldOn;s.mode=2;s.stagger=0;ProteusNetReceived(vehicle,s);Tick();}
    Check(u->st.shield>.25f,"down and unhit the owner refills the shield's HP");
    // Another guest's copy (not the owner): it takes the owner's count.
    Put<std::uint16_t>(vehicle,0x128,3);
    proteus_net::State defense=s;defense.kind=proteus_net::Kind::defense;defense.sender=11;defense.sequence=1;defense.barrier=.4f;defense.quiet=0;defense.flags=0;
    ProteusNetReceived(vehicle,defense);Check(u->st.shield==.4f && !u->st.broken,"a copy takes the owner's shield count");
    defense.barrier=.9f;ProteusNetReceived(vehicle,defense);Check(u->st.shield==.4f,"a duplicate count cannot roll it back");
    s=Control(60);ProteusNetReceived(vehicle,s);Tick();
    alignas(16) static unsigned char copyBarrier[0x1600];
    FreshBarrier(copyBarrier,vehicle,kBarrierSegmentCount);BarrierStep(copyBarrier);
    Put<float>(copyBarrier,kBarrierHp,full*.1f);BarrierStep(copyBarrier);
    Check(u->st.shield==.4f && std::fabs(At<float>(copyBarrier,kBarrierHp)-full*.4f)<0.01f,"a copy's local barrier shows the owner's count, never its own hits");
    defense.sequence=2;defense.barrier=0;defense.flags=proteus_net::kBroken;ProteusNetReceived(vehicle,defense);BarrierStep(copyBarrier);
    Check(u->st.broken && !u->barrier.obj && At<float>(copyBarrier,kBarrierHp)==0,"the owner's broken count takes a copy's barrier down");
    Put<std::uint16_t>(vehicle,0x128,2);
    // The field: a guest's deployed Proteus protects a host-owned Wing Diver through the production team walk.
    alignas(16) unsigned char ally[0x1B00]{},allyCtrl[16]{},weapon[0xF00]{},vehicle2[0x3000]{},ctrl2[16]{};
    unsigned char* weapons[]={weapon};fieldAlly=ally;
    Put<void*>(ally,0,image+kVtWingDiver);Put<void*>(ally,kSelfCtrl,allyCtrl);Put<int>(allyCtrl,8,1);
    std::memcpy(ally+kPosition,vehicle+kPosition,12);Put<float>(ally,kPosition,At<float>(vehicle,kPosition)+20);   // 20 m off the hull
    Put<std::uint16_t>(ally,0x128,2);Put<float>(ally,kTakenMul,1);Put<float>(ally,kDealtMul,1);
    Put<float>(ally,kEnergyMax,100);Put<float>(ally,kEnergy,10);Put<void*>(ally,kHumanWeapons,weapons);Put<std::uint64_t>(ally,kHumanWeaponCount,1);
    Put<float>(weapon,kCountdown,100);Put<float>(weapon,kRate,1);
    const auto walk=reinterpret_cast<std::uintptr_t>(&WalkField);std::memcpy(jump+2,&walk,8);std::memcpy(image+kTeamWalk,jump,12);
    static unsigned char teamManager[0x40]{};Put<void*>(image,kTeamManager,teamManager);
    fieldOk=true;
    s=Control(70);s.mode=2;s.stagger=0;s.fieldRadius=50;s.fieldDefense=.3f;s.fieldAttack=.2f;s.fieldFireRate=2;s.fieldEnergy=.1f;
    ProteusNetReceived(vehicle,s);FieldFrame(*u,vehicle,.1f);
    Check(std::fabs(At<float>(ally,kTakenMul)-.7f)<1e-5f && At<float>(ally,kEnergy)==11 && At<float>(weapon,kCountdown)==94,
          "a host-owned ally receives the guest pilot's field");
    std::memcpy(vehicle2,vehicle,sizeof(vehicle2));Put<void*>(vehicle2,kSelf,vehicle2);Put<void*>(vehicle2,kSelfCtrl,ctrl2);Put<int>(ctrl2,8,1);
    proteus_net::State second=s;second.sender=8;second.sequence=1;second.fieldDefense=.5f;second.fieldAttack=.5f;second.fieldFireRate=3;second.fieldEnergy=.2f;
    ProteusNetReceived(vehicle2,second);Unit* u2=UnitOf(vehicle2,false);FieldFrame(*u2,vehicle2,.1f);
    Check(At<float>(ally,kTakenMul)==.5f && At<float>(ally,kDealtMul)==1.5f && At<float>(ally,kEnergy)==12 && At<float>(weapon,kCountdown)==88,
          "overlapping fields take the strongest values and award only time-effect differences");
    FieldFrame(*u,vehicle,.1f);FieldFrame(*u2,vehicle2,.1f);
    Check(At<float>(ally,kTakenMul)==.5f && At<float>(ally,kEnergy)==12 && At<float>(weapon,kCountdown)==88,"repeated visits in one frame neither stack nor re-award");
    FieldContribution oldSource{true,.1f,0,0,ObjRef{vehicle2,ctrl}};
    Check(!FieldSourceActive(static_cast<int>(u2-units),ally,oldSource),"a reused source slot cannot inherit another ObjRef's contribution");
    second.sequence=2;second.mode=0;ProteusNetReceived(vehicle2,second);
    Check(std::fabs(At<float>(ally,kTakenMul)-.7f)<1e-5f,"stowing one field keeps only the other's protection");
    Put<std::uint16_t>(ally,0x128,1);FieldFrame(*u,vehicle,.1f);
    Check(At<float>(ally,kTakenMul)==1 && At<float>(ally,kEnergy)==12,"a remote ally copy receives no local field writes");
    Put<std::uint16_t>(ally,0x128,2);++frame;now+=100;FieldFrame(*u,vehicle,.1f);
    Check(std::fabs(At<float>(ally,kTakenMul)-.7f)<1e-5f,"a new frame restores unconsumed multipliers before one fresh contribution");
    s.sequence=71;s.mode=0;ProteusNetReceived(vehicle,s);
    Check(At<float>(ally,kTakenMul)==1 && At<float>(ally,kDealtMul)==1,"withdrawal removes all pending support multipliers");
    s.sequence=72;s.mode=2;ProteusNetReceived(vehicle,s);FieldFrame(*u,vehicle,.1f);
    now+=1600;RefreshFieldWrites();
    Check(At<float>(ally,kTakenMul)==1,"an expired network source leaves no field protection");
    fieldAlly=nullptr;
    // Expiry, epochs and death.
    s.sequence=73;s.flags|=proteus_net::kShieldOn;ProteusNetReceived(vehicle,s);u->st.shield=1;u->st.broken=false;Tick();
    alignas(16) static unsigned char late[0x1600];
    FreshBarrier(late,vehicle,kBarrierSegmentCount);BarrierStep(late);
    Check(u->barrier.obj==late,"(a replayed barrier stands)");
    now+=1600;Tick();BarrierStep(late);
    Check(!ActiveOf(vehicle) && At<float>(late,kBarrierHp)==0 && !u->barrier.obj,"expired control takes the replayed barrier down");
    netDriver=-1;s.sequence=100;ProteusNetReceived(vehicle,s);Check(u->net.control.sequence==73,"an old registered driver cannot overwrite the host epoch");
    s.controller=-1;s.sender=9;s.sequence=1;s.flags=0;s.mode=0;ProteusNetReceived(vehicle,s);Tick();
    Check(!ActiveOf(vehicle),"an empty host snapshot deactivates the old pilot's deployment");
    vehicle[kDead]=1;s.flags=proteus_net::kActive|proteus_net::kShieldOn;s.sequence=2;ProteusNetReceived(vehicle,s);
    Check(!ActiveOf(vehicle),"a destroyed vehicle cannot be revived by a packet");
    vehicle[kDead]=0;
    alignas(16) unsigned char realNpc[0x400]{},npcControl[16]{};
    Put<void*>(realNpc,0,image+kVtRanger);Put<float>(realNpc,kHp,100);Put<std::uint16_t>(realNpc,0x128,2);Put<int>(npcControl,8,1);
    Seat(1,realNpc,npcControl);
    Check(LocalGunner(vehicle),"a host-owned real NPC gunner works the cannons on the host's copy");
    Put<std::uint16_t>(realNpc,0x128,1);Check(!LocalGunner(vehicle),"a remote copy of that NPC fires nothing here");
    Put<void*>(realNpc,0,image+kDummyRiderVtable);Check(!LocalGunner(vehicle),"an unregistered Dummy is never a real gunner");
    Put<void*>(realNpc,0,image+kVtRanger);Put<std::uint16_t>(realNpc,0x128,2);
    Seat(0,realNpc,npcControl);Seat(1,human,ctrl);
    netAuthority=true;netDriver=99;Tick();
    Check(u->active && !u->net.remote && (lastControl.flags&proteus_net::kActive),
          "a real NPC host driver publishes the active state while a guest human gunner is aboard");
    Seat(1,nullptr,nullptr);Tick();
    Check(!u->active && !(lastControl.flags&proteus_net::kActive),"the last human leaving an NPC-driven Proteus clears the rework");
    ResetProteus();Check(!UnitOf(vehicle,false),"a mission reset discards object-bound replay");

    // --- A copy's local wall shows the owner's count: a local hit to 0 never drops it (no flicker, no re-raise). ---
    Seat(0,human,ctrl);Seat(1,nullptr,nullptr);netDriver=42;netAuthority=false;Put<std::uint16_t>(vehicle,0x128,3);
    s=Control(1);s.sender=21;s.mode=0;s.stagger=0;ProteusNetReceived(vehicle,s);u=UnitOf(vehicle,false);
    proteus_net::State count=s;count.kind=proteus_net::Kind::defense;count.sender=22;count.sequence=1;count.barrier=.5f;count.flags=0;
    ProteusNetReceived(vehicle,count);
    int raises=emcFires;Tick();
    alignas(16) static unsigned char local[0x1600];
    FreshBarrier(local,vehicle,kBarrierSegmentCount);BarrierStep(local);
    Put<float>(local,kBarrierHp,-20);BarrierStep(local);
    Check(u->barrier.obj==local && std::fabs(At<float>(local,kBarrierHp)-.5f*.5f*7500)<.01f && !local[kBarrierNoEcho],
          "a copy's wall shot to 0 locally is put back to the owner's count and kept");
    s.sequence=2;ProteusNetReceived(vehicle,s);Tick();
    Check(emcFires==raises+1,"no second raise for a copy's wall");
    ResetProteus();

    // --- Mixed builds: a version-1 packet for this Proteus. ---
    Put<std::uint16_t>(vehicle,0x128,2);netAuthority=true;netDriver=42;
    Put<float>(vehicle,kWalk,10);Put<float>(vehicle,kJump,8);
    for(unsigned i=0;i<4;++i)Put<int>(SeatAt(vehicle,i),kSeatClassMask,15);
    Tick();u=UnitOf(vehicle,false);
    Check(u && u->active && At<float>(vehicle,kWalk)!=10,"(the local driver reworks it)");
    const unsigned char* lent[4]{};
    ProteusNetIncompatible(vehicle,1);
    Check(u->net.incompatible && !ActiveOf(vehicle) && At<float>(vehicle,kWalk)==10 && At<int>(seats+2*kSeatStride,kSeatClassMask)==15,
          "a version-1 peer: this Proteus is given back to the stock at once");
    raises=emcFires;u->st.shieldOn=true;for(int i=0;i<5;++i)Tick();
    Check(emcFires==raises && !ActiveOf(vehicle) && ProteusBorrowedWeapons(vehicle,0,lent,4)==0 && At<float>(vehicle,kWalk)==10,
          "while a peer runs another build: no wall, no borrowed weapon, stock legs");
    ProteusReadout ro{};now+=1000;Check(!PlayerProteus(&ro),"no rework HUD for a stock Proteus");
    netSession=false;Tick();
    Check(!u->net.incompatible && ActiveOf(vehicle)==u,"out of the session the rework is back");
    netSession=true;ResetProteus();

    // --- Mixed builds: the local driver of a Proteus another machine owns hears no shield count. ---
    Put<float>(vehicle,kWalk,10);Put<float>(vehicle,kJump,8);   // (the reset above left the offline rework's legs)
    Put<std::uint16_t>(vehicle,0x128,3);netAuthority=true;
    Tick();u=UnitOf(vehicle,false);
    Check(u && u->active,"(a guest drives a host-owned Proteus)");
    for(int i=0;i<25;++i){Tick();count.sequence=static_cast<std::uint32_t>(10+i);ProteusNetReceived(vehicle,count);}
    Check(u->active && !u->net.incompatible,"the owner's count keeps coming: the rework stays");
    for(int i=0;i<25;++i)Tick();
    Check(u->active && !u->net.incompatible,"a short gap (2.5 s) is tolerated");
    for(int i=0;i<10;++i)Tick();
    Check(u->net.incompatible && !ActiveOf(vehicle) && At<float>(vehicle,kWalk)==10,
          "no count from the owner for 3 s (an older build): stock here");
    ResetProteus();Put<std::uint16_t>(vehicle,0x128,2);
    std::printf("proteus_net_runtime_test: %d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
