#define main PreviousProteusFrameChecks
#include "proteus_frame_test.cpp"
#undef main
namespace {
unsigned char* fieldAlly=nullptr;
void __fastcall WalkField(void*,std::int32_t,void* functor){if(fieldAlly)crew::FieldVisit(functor,fieldAlly);}
void __fastcall Compose(void* inst,const void* transform){
    using namespace crew;const auto bones=At<unsigned char*>(inst,kInstBones506);
    for(int i=0;i<37;++i)exhaust::Mul(reinterpret_cast<const float*>(bones+i*kBoneStride+kBoneLocal506),
        static_cast<const float*>(transform),reinterpret_cast<float*>(bones+i*kBoneStride+kBoneWorld506));
}
}
int main(){
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto fn=reinterpret_cast<std::uintptr_t>(&Compose);std::memcpy(jump+2,&fn,8);std::memcpy(image+kSetModelWorld,jump,12);
    ok=damageOk=true;config.debug=false;config.proteusFieldRadius=0;netSession=true;netAuthority=false;
    Put<const void*>(vehicle,0,image+kVtBig);Put<void*>(vehicle,kSelfCtrl,ctrl);Put<void*>(vehicle,kSeats,seats);Put<std::uint64_t>(vehicle,kSeatCount,4);
    Put<std::uint16_t>(vehicle,0x128,2);Put<float>(vehicle,kHpMax,7500);Put<float>(vehicle,kHp,7500);
    Put<float>(vehicle,kWalk,10);Put<float>(vehicle,kJump,8);Put<float>(vehicle,kStepNormal,.76f);
    proteus::pose::Identity(reinterpret_cast<float*>(vehicle+kMatrix));
    for(int i=0;i<4;++i)Put<int>(seats+i*kSeatStride,kSeatClassMask,15);
    Put<void*>(seats,kSeatRider,human);Put<void*>(seats,kSeatRiderCtrl,ctrl);Put<int>(ctrl,8,1);
    human[edf::kHumanPlayer]=1;Put<void*>(human,edf::kHumanPad,human);Put<std::uint16_t>(human,0x128,1);seats[kSeatPad]=1;
    alignas(16) unsigned char bones[37*kBoneStride]{},skeleton[0x70]{},binds[37*0xD0]{};
    testPoseBones=bones;Put<void*>(vehicle+kModelInst506,0,skeleton);Put<void*>(vehicle+kModelInst506,kInstBones506,bones);
    Put<void*>(skeleton,0x58,binds);Put<int>(skeleton,0x68,37);
    for(int i=0;i<37;++i){proteus::pose::Identity(reinterpret_cast<float*>(binds+i*0xD0+0x20));proteus::pose::Identity(reinterpret_cast<float*>(bones+i*kBoneStride+kBoneLocal506));}
    proteus_net::State s;s.sender=7;s.sequence=1;s.controller=42;
    s.flags=proteus_net::kActive|proteus_net::kShieldOn|proteus_net::kTwoSeats;s.mode=1;s.stagger=.75f;
    s.nose[0]=1;s.nose[2]=0;s.shieldArc=60;s.barrier=.1f;s.barrierShare=.5f;
    ProteusNetReceived(vehicle,s);Unit* u=UnitOf(vehicle,false);
    Check(u && u->net.remote && u->st.mode==proteus::Mode::deploying,"host target owner consumes remote driver deployment");
    Check(u->st.barrier==1,"driver control cannot replace the damage owner's barrier pool");
    Tick();ProteusModelPose(vehicle);
    Check(std::fabs(At<float>(bones+36*kBoneStride,kBoneLocal506+52)-kPiles[0].retract*.5f)<1e-4f,"remote half deployment moves the real pile bone");
    Check(std::fabs(std::atan2(At<float>(bones,kBoneLocal506+32),At<float>(bones,kBoneLocal506+40))-65*kPi/180)<1e-4f,
        "received shield direction and arc pose the real panel matrix");
    Check(At<float>(vehicle,kWalk)==10 && At<float>(vehicle,kJump)==8 && gunShots==0 && salvoShots==0,
        "remote frame never runs Legs, driver fire or salvo");
    Check(At<int>(seats+2*kSeatStride,kSeatClassMask)==0,"replicated seat layout closes slots without firing them");
    s.sequence=2;s.mode=2;s.stagger=0;ProteusNetReceived(vehicle,s);
    alignas(16) unsigned char hit[0x100]{},team[0x100]{},teamRows[0x38]{};int relation[1]={2};
    Put<void*>(image,kTeamManager,team);Put<void*>(team,0x38,teamRows);Put<void*>(teamRows,0x18,relation);
    Put<float>(hit,kDmgAt,-20);Put<float>(hit,kDmgAmount,1000);
    float was=0;const int before=defenseSends;
    relation[0]=1;
    Check(!Shield(vehicle,hit,&was) && u->st.barrier==1 && defenseSends==before,"rejected friendly hit cannot consume or publish barrier");
    relation[0]=2;Put<std::uint32_t>(vehicle,0x380,1);
    Check(!Shield(vehicle,hit,&was) && u->st.barrier==1 && defenseSends==before,"native invulnerability cannot consume or publish barrier");
    Put<std::uint32_t>(vehicle,0x380,0);
    Check(Shield(vehicle,hit,&was)!=nullptr && At<float>(hit,kDmgAmount)==0 && was==1000,"legal rear hit is fully caught by the owner barrier");
    const float consumed=u->st.barrier;
    Check(consumed<1 && defenseSends==before+1 && lastDefense.barrier==consumed,"full absorption with zero HP loss still broadcasts defense consumption");
    s.sequence=3;s.barrier=1;ProteusNetReceived(vehicle,s);
    Check(u->st.barrier==consumed,"later pilot pose cannot restore consumed barrier");
    Put<std::uint16_t>(vehicle,0x128,1);Put<std::uint16_t>(hit,0x60,0x40);Put<float>(hit,kDmgAmount,500);
    Check(!Shield(vehicle,hit,&was) && u->st.barrier==consumed && At<float>(hit,kDmgAmount)==500,"remote native replay never absorbs or consumes defense again");
    netAuthority=true;
    proteus_net::State defense=s;defense.kind=proteus_net::Kind::defense;defense.sender=11;defense.sequence=1;defense.barrier=.4f;defense.quiet=0;
    ProteusNetReceived(vehicle,defense);Check(u->st.barrier==.4f,"driver accepts target-owner barrier accounting");
    defense.barrier=.9f;ProteusNetReceived(vehicle,defense);Check(u->st.barrier==.4f,"duplicate defense cannot roll barrier back");
    // A guest's deployed Proteus protects a host-owned Wing Diver through the production team walker/FieldVisit.
    // Two overlapping fields choose the strongest multipliers and only the extra difference in time-based rewards.
    Put<std::uint16_t>(vehicle,0x128,2);netAuthority=false;
    alignas(16) unsigned char ally[0x1B00]{},allyCtrl[16]{},weapon[0xF00]{},vehicle2[0x3000]{},ctrl2[16]{};
    unsigned char* weapons[]={weapon};fieldAlly=ally;
    Put<void*>(ally,0,image+kVtWingDiver);Put<void*>(ally,kSelfCtrl,allyCtrl);Put<int>(allyCtrl,8,1);
    Put<std::uint16_t>(ally,0x128,2);Put<float>(ally,kTakenMul,1);Put<float>(ally,kDealtMul,1);
    Put<float>(ally,kEnergyMax,100);Put<float>(ally,kEnergy,10);Put<void*>(ally,kHumanWeapons,weapons);Put<std::uint64_t>(ally,kHumanWeaponCount,1);
    Put<float>(weapon,kCountdown,100);Put<float>(weapon,kRate,1);
    const auto walk=reinterpret_cast<std::uintptr_t>(&WalkField);std::memcpy(jump+2,&walk,8);std::memcpy(image+kTeamWalk,jump,12);
    fieldOk=true;
    s.sequence=4;s.fieldRadius=50;s.fieldDefense=.3f;s.fieldAttack=.2f;s.fieldFireRate=2;s.fieldEnergy=.1f;
    ProteusNetReceived(vehicle,s);FieldFrame(*u,vehicle,.1f,config);
    Check(std::fabs(At<float>(ally,kTakenMul)-.7f)<1e-5f && At<float>(ally,kEnergy)==11 && At<float>(weapon,kCountdown)==94,
          "host-owned ally receives the guest pilot's actual field parameters");
    std::memcpy(vehicle2,vehicle,sizeof(vehicle2));Put<void*>(vehicle2,kSelfCtrl,ctrl2);Put<int>(ctrl2,8,1);
    proteus_net::State second=s;second.sender=8;second.sequence=1;second.fieldDefense=.5f;second.fieldAttack=.5f;second.fieldFireRate=3;second.fieldEnergy=.2f;
    ProteusNetReceived(vehicle2,second);Unit* u2=UnitOf(vehicle2,false);FieldFrame(*u2,vehicle2,.1f,config);
    Check(At<float>(ally,kTakenMul)==.5f && At<float>(ally,kDealtMul)==1.5f && At<float>(ally,kEnergy)==12 && At<float>(weapon,kCountdown)==88,
          "overlapping fields take strongest values and award only time-effect differences");
    FieldFrame(*u,vehicle,.1f,config);FieldFrame(*u2,vehicle2,.1f,config);
    Check(At<float>(ally,kTakenMul)==.5f && At<float>(ally,kEnergy)==12 && At<float>(weapon,kCountdown)==88,
          "repeated visits in one frame neither stack nor re-award support");
    FieldContribution oldSource{true,.1f,0,0,ObjRef{vehicle2,ctrl}};
    Check(!FieldSourceActive(static_cast<int>(u2-units),ally,oldSource),"reused source slot cannot inherit another ObjRef's field contribution");
    second.sequence=2;second.mode=0;ProteusNetReceived(vehicle2,second);
    Check(std::fabs(At<float>(ally,kTakenMul)-.7f)<1e-5f,"stowing one field immediately retains only the other field's protection");
    Put<std::uint16_t>(ally,0x128,1);FieldFrame(*u,vehicle,.1f,config);
    Check(At<float>(ally,kTakenMul)==1 && At<float>(ally,kEnergy)==12,"remote ally copy receives no local field writes");
    Put<std::uint16_t>(ally,0x128,2);++frame;now+=100;FieldFrame(*u,vehicle,.1f,config);
    Check(std::fabs(At<float>(ally,kTakenMul)-.7f)<1e-5f,"new frame restores unconsumed multipliers before applying one fresh contribution");
    s.sequence=5;s.mode=0;ProteusNetReceived(vehicle,s);
    Check(At<float>(ally,kTakenMul)==1 && At<float>(ally,kDealtMul)==1,"withdrawal immediately removes all pending support multipliers");
    s.sequence=6;s.mode=2;ProteusNetReceived(vehicle,s);FieldFrame(*u,vehicle,.1f,config);
    now+=1600;RefreshFieldWrites();
    Check(At<float>(ally,kTakenMul)==1,"expired network source cannot leave accumulated field protection");
    fieldAlly=nullptr;
    // Expiration, driver handoff and destruction stop both the visible shield and effective protection.
    Put<std::uint16_t>(vehicle,0x128,2);netAuthority=false;s.sequence=7;ProteusNetReceived(vehicle,s);
    now+=1600;Put<float>(hit,kDmgAmount,500);
    Check(!Shield(vehicle,hit,&was),"expired control cannot keep protection alive");
    ProteusModelPose(vehicle);Check(At<float>(bones,kBoneLocal506)==0 && At<float>(bones,kBoneLocal506+20)==0,"expired control hides actual shield geometry");
    netDriver=-1;s.sequence=100;ProteusNetReceived(vehicle,s);Check(u->net.control.sequence==7,"old registered driver cannot overwrite empty/NPC host epoch");
    s.controller=-1;s.sender=9;s.sequence=1;s.flags=0;s.mode=0;ProteusNetReceived(vehicle,s);Tick();
    Check(!ActiveOf(vehicle),"empty host snapshot deactivates old pilot deployment");
    vehicle[kDead]=1;s.flags=proteus_net::kActive|proteus_net::kShieldOn;s.sequence=2;ProteusNetReceived(vehicle,s);
    Check(!ActiveOf(vehicle) && !Shield(vehicle,hit,&was),"destroyed vehicle cannot resurrect protection from a packet");
    vehicle[kDead]=0;
    alignas(16) unsigned char realNpc[0x400]{},npcControl[16]{};
    Put<void*>(realNpc,0,image+kVtRanger);Put<std::uint16_t>(realNpc,0x128,2);Put<int>(npcControl,8,1);
    Put<void*>(seats+kSeatStride,kSeatRider,realNpc);Put<void*>(seats+kSeatStride,kSeatRiderCtrl,npcControl);
    Check(LocalGunner(vehicle),"host-owned real NPC gunner may operate the paired native cannon on the host replica");
    Put<std::uint16_t>(realNpc,0x128,1);Check(!LocalGunner(vehicle),"remote copy of that NPC cannot trigger another paired shot");
    Put<void*>(realNpc,0,image+kDummyRiderVtable);Check(!LocalGunner(vehicle),"unregistered Dummy is never promoted to real gunner ownership");
    Put<void*>(realNpc,0,image+kVtRanger);Put<std::uint16_t>(realNpc,0x128,2);
    Put<void*>(seats,kSeatRider,realNpc);Put<void*>(seats,kSeatRiderCtrl,npcControl);
    Put<void*>(seats+kSeatStride,kSeatRider,human);Put<void*>(seats+kSeatStride,kSeatRiderCtrl,ctrl);
    netAuthority=true;netDriver=99;Tick();
    Check(u->active && !u->net.remote && (lastControl.flags&proteus_net::kActive),
          "real NPC host driver publishes active state while a guest human gunner remains aboard");
    Put<void*>(seats+kSeatStride,kSeatRider,nullptr);Put<void*>(seats+kSeatStride,kSeatRiderCtrl,nullptr);Tick();
    Check(!u->active && !(lastControl.flags&proteus_net::kActive),"last human leaving an NPC-driven vehicle clears special deployment state");
    ResetProteus();Check(!UnitOf(vehicle,false),"mission reset discards object-bound replay");
    std::printf("proteus_net_runtime_test: %d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
