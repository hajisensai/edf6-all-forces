// Exercise the production inheritance bridge with cached support owners and
// live-player canonical identities. Reuse the original bridge fixture/stubs.
#define main ExistingPvpSupportFixtureMain
#include "pvp_support_fixture.cpp"
#undef main

namespace {
unsigned char liveParent[0x400]{},liveControl[16]{},otherControl[16]{};
crew::ObjRef ResetParent(int team=2) noexcept {
    const auto plan=Reset(true);
    std::memset(liveParent,0,sizeof(liveParent));
    std::memset(liveControl,0,sizeof(liveControl));
    std::memset(otherControl,0,sizeof(otherControl));
    crew::Put<const void*>(liveParent,crew::kSelfCtrl,liveControl);
    crew::Put<LONG>(liveControl,8,1);crew::Put<LONG>(otherControl,8,1);
    // A native team is deliberately unrelated to the resolved owner side.
    crew::Put<int>(liveParent,crew::kTeam,73);
    const crew::ObjRef parent{liveParent,liveControl};
    fixture.expectedParent=fixture.commandPlayer=fixture.markPlayer=parent;
    fixture.team=team;
    std::memcpy(fixture.parentId,plan.requester,sizeof(fixture.parentId));
    return parent;
}
}

int main() {
    using crew::PvpInheritSupport;
    using crew::PvpRefreshSupportOwner;
    crew::ObjRef children[crew::support_net::kMaxUnits]{};
    for(unsigned i=0;i<crew::support_net::kMaxUnits;++i)children[i]={actor+i,control+i};

    // Cached parents need not themselves be players. Ownership is queried from
    // the weak-identity cache, never reconstructed from the parent's team.
    auto parent=ResetParent();fixture.cachedOwner=true;fixture.soldier=fixture.actualPlayer=false;
    CHECK(PvpInheritSupport(parent,children,crew::support_net::kMaxUnits));
    CHECK(fixture.ownerCalls==1 && fixture.correctOwnerArgs && fixture.objectIdCalls==0 && fixture.strictCalls==0);
    CHECK(fixture.nativeCalls==1 && fixture.attachedTeam==2 && fixture.attachedCount==crew::support_net::kMaxUnits);
    CHECK(!std::strcmp(fixture.attachedPuid,kOwner));
    for(unsigned i=0;i<crew::support_net::kMaxUnits;++i)
        CHECK(fixture.attachedRefs[i].obj==children[i].obj && fixture.attachedRefs[i].ctrl==children[i].ctrl);
    parent=ResetParent(0);fixture.cachedOwner=true;fixture.soldier=false;
    CHECK(PvpInheritSupport(parent,children,1));
    CHECK(fixture.nativeCalls==1 && fixture.attachedTeam==0 && !std::strcmp(fixture.attachedPuid,kOwner));
    parent=ResetParent(-1);fixture.cachedOwner=true;
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.nativeCalls==0);
    parent=ResetParent();fixture.cachedOwner=true;fixture.nativeOk=false;
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.nativeCalls==1);

    // A cache miss on an actual player goes through its mission-slot requester (never its NetId: a stock mission numbers
    // those per machine), the slot's live player, Coop's mission-slot resolver, and the native PUID seal.
    parent=ResetParent(1);
    CHECK(PvpInheritSupport(parent,children,1));
    CHECK(fixture.ownerCalls==1 && fixture.soldierCalls==1 && fixture.playerCalls==1 && fixture.objectIdCalls==1);
    CHECK(fixture.strictCalls==0 && fixture.markCalls==1 && fixture.resolveCalls==1 && fixture.sealCalls==1);
    CHECK(fixture.correctObjectIdArgs && fixture.correctCommandArgs && fixture.correctMarkArgs &&
        fixture.correctLoaderArgs && fixture.correctSealArgs);
    CHECK(fixture.nativeCalls==1 && fixture.attachedTeam==1 && !std::strcmp(fixture.attachedPuid,kOwner));
    parent=ResetParent(0);
    CHECK(PvpInheritSupport(parent,children,1));CHECK(fixture.nativeCalls==1 && fixture.attachedTeam==0);
    parent=ResetParent(-1);
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.nativeCalls==0);

    // A native vehicle's seat-0 driver uses the same sealed player path, including mode-off ownership.
    unsigned char vehicle[0x400]{},vehicleCtrl[16]{};
    parent=ResetParent(0);crew::Put<const void*>(vehicle,crew::kSelfCtrl,vehicleCtrl);crew::Put<LONG>(vehicleCtrl,8,1);
    fixture.driverOk=true;fixture.driverPlayer=parent;fixture.driverVehicle={vehicle,vehicleCtrl};
    CHECK(PvpInheritSupport(fixture.driverVehicle,children,1));
    CHECK(fixture.driverCalls==1 && fixture.markCalls==1 && fixture.sealCalls==1 && fixture.attachedTeam==0);
    fixture.team=2;CHECK(PvpInheritSupport(fixture.driverVehicle,children,1));
    CHECK(fixture.attachedTeam==2 && !std::strcmp(fixture.attachedPuid,kOwner));
    fixture.driverOk=false;CHECK(!PvpInheritSupport(fixture.driverVehicle,children,1));

    // The player flags alone cannot authorize a different native object or a
    // newly allocated object at an old pointer. Reject every partial identity.
    parent=ResetParent();fixture.soldier=false;
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.objectIdCalls==0 && fixture.nativeCalls==0);
    parent=ResetParent();fixture.actualPlayer=false;
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.objectIdCalls==0 && fixture.nativeCalls==0);
    parent=ResetParent();fixture.readable=false;
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.objectIdCalls==0 && fixture.nativeCalls==0);
    parent=ResetParent();crew::Put<LONG>(liveControl,8,0);
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.objectIdCalls==0 && fixture.nativeCalls==0);
    parent=ResetParent();fixture.cachedOwner=true;crew::Put<const void*>(liveParent,crew::kSelfCtrl,otherControl);
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.objectIdCalls==0 && fixture.nativeCalls==0);
    parent=ResetParent();liveParent[crew::kDead]=1;
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.objectIdCalls==0 && fixture.nativeCalls==0);
    parent=ResetParent();liveParent[0x18]=4;
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.objectIdCalls==0 && fixture.nativeCalls==0);
    parent=ResetParent();parent.obj=reinterpret_cast<const void*>(1);
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.objectIdCalls==0 && fixture.nativeCalls==0);
    parent=ResetParent();fixture.nativeIdOk=false;
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.markCalls==0 && fixture.nativeCalls==0);
    parent=ResetParent();std::memset(fixture.parentId,0,sizeof(fixture.parentId));
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.markCalls==0 && fixture.nativeCalls==0);
    parent=ResetParent();fixture.parentId[20]=1;
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.markCalls==0 && fixture.nativeCalls==0);
    parent=ResetParent();fixture.commandPlayer=fixture.markPlayer=crew::ObjRef{actor,control};
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.resolveCalls==0 && fixture.nativeCalls==0);
    parent=ResetParent();fixture.markPlayer.ctrl=otherControl;
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.resolveCalls==0 && fixture.nativeCalls==0);
    parent=ResetParent();fixture.sealed=false;
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.sealCalls==1 && fixture.nativeCalls==0);
    parent=ResetParent();fixture.resolveOk=false;
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.resolveCalls==1 && fixture.nativeCalls==0);

    // An unresolved legacy/offline owner can retain stock behavior only after
    // the stable mode-off gate; neither active sides nor Busy may guess a team.
    parent=ResetParent(0);fixture.soldier=false;
    CHECK(PvpInheritSupport(parent,children,1));CHECK(fixture.nativeCalls==0 && fixture.teamCalls==1);
    parent=ResetParent(-1);fixture.soldier=false;
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.nativeCalls==0);
    parent=ResetParent(1);fixture.soldier=false;crew::Put<int>(liveParent,crew::kTeam,1);
    CHECK(!PvpInheritSupport(parent,children,1));CHECK(fixture.nativeCalls==0);
    parent=ResetParent();
    CHECK(!PvpInheritSupport(parent,nullptr,1));CHECK(!PvpInheritSupport(parent,children,0));
    CHECK(!PvpInheritSupport(parent,children,crew::support_net::kMaxUnits+1));
    CHECK(fixture.ownerCalls==0 && fixture.nativeCalls==0);

    // A delayed/rebuilt plan must refresh from the owner's present committed
    // side instead of trusting the old plan.team value (including invalid ones).
    auto plan=Reset(true);fixture.team=2;plan.team=99;
    CHECK(PvpRefreshSupportOwner(&plan));CHECK(plan.team==2 && fixture.markCalls==1 && fixture.sealCalls==1);
    fixture.team=1;
    CHECK(PvpRefreshSupportOwner(&plan));CHECK(plan.team==1 && fixture.markCalls==2 && fixture.sealCalls==2);
    fixture.team=0;
    CHECK(PvpRefreshSupportOwner(&plan));CHECK(plan.team==0 && fixture.markCalls==3 && fixture.sealCalls==3);
    plan=Reset(true);fixture.team=-1;plan.team=1;
    CHECK(!PvpRefreshSupportOwner(&plan));CHECK(plan.team==1);
    plan=Reset(true);fixture.markOk=false;plan.team=2;
    CHECK(!PvpRefreshSupportOwner(&plan));CHECK(plan.team==2);
    plan=Reset(true);fixture.sealed=false;plan.team=1;
    CHECK(!PvpRefreshSupportOwner(&plan));CHECK(plan.team==1);
    plan=Reset(true);plan.requester[23]=1;plan.team=2;
    CHECK(!PvpRefreshSupportOwner(&plan));CHECK(plan.team==2 && fixture.markCalls==0);
    plan=Reset();plan.team=99;
    CHECK(PvpRefreshSupportOwner(&plan));CHECK(plan.team==0 && fixture.strictCalls==0 && fixture.nativeCalls==0);
    plan=Reset();fixture.team=2;plan.team=1;
    CHECK(!PvpRefreshSupportOwner(&plan));CHECK(plan.team==1);
    plan=Reset();fixture.team=-1;plan.team=2;
    CHECK(!PvpRefreshSupportOwner(&plan));CHECK(plan.team==2);
    CHECK(!PvpRefreshSupportOwner(nullptr));

    // Dynamic carrier plans bind an existing canonical owner to the exact cached parent,
    // or build a new requester from the actual player's full native identity.
    parent=ResetParent(2);fixture.cachedOwner=true;
    crew::support_net::Plan inherited{};std::memcpy(inherited.requester,fixture.expectedId,32);inherited.team=99;
    CHECK(crew::PvpMakeInheritedSupportPlan(parent,&inherited) && inherited.team==2);
    CHECK(!std::memcmp(inherited.requester,fixture.expectedId,32));
    parent=ResetParent(2);fixture.cachedOwner=false;inherited={};std::memcpy(inherited.requester,fixture.expectedId,32);
    CHECK(!crew::PvpMakeInheritedSupportPlan(parent,&inherited));
    parent=ResetParent(2);fixture.cachedOwner=true;std::memcpy(fixture.cachedText,"differentOwner",15);
    inherited={};std::memcpy(inherited.requester,fixture.expectedId,32);
    CHECK(!crew::PvpMakeInheritedSupportPlan(parent,&inherited));
    parent=ResetParent(0);inherited={};inherited.team=99;
    CHECK(crew::PvpMakeInheritedSupportPlan(parent,&inherited) && inherited.team==0 &&
        !std::memcmp(inherited.requester,fixture.expectedId,32));
    parent=ResetParent(-1);inherited={};CHECK(!crew::PvpMakeInheritedSupportPlan(parent,&inherited));
    parent=ResetParent(0);fixture.soldier=false;fixture.actualPlayer=false;inherited={};
    CHECK(!crew::PvpMakeInheritedSupportPlan(parent,&inherited)); // online unknown owner never becomes host
    fixture.session=false;CHECK(crew::PvpMakeInheritedSupportPlan(parent,&inherited) && inherited.team==0);
    parent=ResetParent(0);inherited={};fixture.driverOk=true;fixture.driverPlayer=parent;
    fixture.driverVehicle={vehicle,vehicleCtrl};
    CHECK(crew::PvpMakeInheritedSupportPlan(fixture.driverVehicle,&inherited) &&
        !std::memcmp(inherited.requester,fixture.expectedId,32) && inherited.team==0);
    CHECK(fixture.strictCalls==0 && fixture.markCalls==1 && fixture.resolveCalls==1 && fixture.sealCalls==1);
    parent=ResetParent(2);fixture.parentId[20]=1;inherited={};
    CHECK(!crew::PvpMakeInheritedSupportPlan(parent,&inherited));
    CHECK(!crew::PvpMakeInheritedSupportPlan(parent,nullptr));

    if(failures)return 1;
    std::printf("PVP inherited support: %u checks passed\n",checks);return 0;
}
