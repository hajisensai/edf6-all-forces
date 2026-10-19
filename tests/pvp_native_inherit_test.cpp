// Production native ownership/cache/restore path, including ordinary Decoy GameObjects.
#define main ExistingPvpNativeFixtureMain
#include "pvp_native_fixture.cpp"
#undef main

int main() {
    if(ExistingPvpNativeFixtureMain())return 1;
    crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x550000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    CHECK(crew::image!=nullptr);if(!crew::image)return 1;
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto target=reinterpret_cast<std::uintptr_t>(&NativeSetTeam);std::memcpy(jump+2,&target,8);
    std::memcpy(crew::image+crew::kSetTeam,jump,sizeof(jump));
    FlushInstructionCache(GetCurrentProcess(),crew::image+crew::kSetTeam,sizeof(jump));
    const unsigned start=checks;
    {
        FakeObject parent(false,5,true,true),decoy(false,4,true),unknown(false,4,true);
        edf::Put<void*>(decoy.bytes,0,crew::image+0x17D3F88);
        // Poison both unrelated layouts. An exact Decoy must never enumerate either one.
        unsigned char poison[16]{};
        unreadableWeapon=poison;
        edf::Put<unsigned>(decoy.bytes,edf::kSeatCount,1);edf::Put<void*>(decoy.bytes,edf::kSeats,poison);
        edf::Put<std::uint64_t>(decoy.bytes,0x1960,1);edf::Put<void*>(decoy.bytes,0x1950,poison);
        const auto parentRef=crew::ObjRef::Of(parent.bytes),decoyRef=crew::ObjRef::Of(decoy.bytes);
        CHECK(crew::PvpNativeAttachSupport("ownerA",&parentRef,1,0));
        CHECK(crew::PvpNativeAttachSupport("ownerA",&decoyRef,1,0));
        const auto unknownRef=crew::ObjRef::Of(unknown.bytes);
        CHECK(!crew::PvpNativeAttachSupport("ownerA",&unknownRef,1,0));
        char owner[65]="dirty";
        CHECK(crew::PvpNativeSupportOwner(parentRef,owner,sizeof(owner)) && !std::strcmp(owner,"ownerA"));
        CHECK(crew::PvpNativeSupportOwner(decoyRef,owner,sizeof(owner)) && !std::strcmp(owner,"ownerA"));
        CHECK(!crew::PvpNativeSupportOwner(parentRef,owner,64));
        CHECK(!crew::PvpNativeSupportOwner(parentRef,nullptr,65));
        edf::Put<unsigned char>(parent.bytes,edf::kDead,1);
        CHECK(!crew::PvpNativeSupportOwner(parentRef,owner,sizeof(owner)) && !owner[0]);
        edf::Put<unsigned char>(parent.bytes,edf::kDead,0);
        edf::Put<LONG>(parent.control,8,0);
        CHECK(!crew::PvpNativeSupportOwner(parentRef,owner,sizeof(owner)) && !owner[0]);
        edf::Put<LONG>(parent.control,8,1);
        FakeObject replacement(false,5,true,true);
        edf::Put<void*>(parent.bytes,edf::kSelfCtrl,replacement.control);
        CHECK(!crew::PvpNativeSupportOwner(parentRef,owner,sizeof(owner)));
        CHECK(!crew::PvpNativeSupportOwner(crew::ObjRef::Of(parent.bytes),owner,sizeof(owner)));
        edf::Put<void*>(parent.bytes,edf::kSelfCtrl,parent.control);
        CHECK(crew::PvpNativeSupportOwner(parentRef,owner,sizeof(owner)) && !std::strcmp(owner,"ownerA"));
        CHECK(edf::At<LONG>(parent.control,12)==2 && edf::At<LONG>(decoy.control,12)==2);
        pvp_protocol::State state{};state.enabled=true;state.count=1;
        std::memcpy(state.entries[0].puid,"ownerA",7);state.entries[0].team=1;
        crew::Collection owned{};
        CHECK(crew::AppendOwnedSupport(owned,state) && owned.objectsCount==2);
        bool ordinary=false;
        for(unsigned i=0;i<owned.objectsCount;++i)if(owned.objects[i].ref.obj==decoy.bytes)
            ordinary=!owned.objects[i].player && !owned.objects[i].vehicle;
        CHECK(ordinary && crew::ApplyCollected(owned));
        CHECK(edf::At<int>(decoy.bytes,edf::kTeam)==1);
        CHECK(edf::At<LONG>(decoy.control,12)==3);
        state.entries[0].team=2;owned={};
        CHECK(crew::AppendOwnedSupport(owned,state) && crew::ApplyCollected(owned));
        CHECK(edf::At<int>(decoy.bytes,edf::kTeam)==2);
        // The authenticated owner leaves: the object falls out of the desired set and restores team 4.
        std::memcpy(state.entries[0].puid,"ownerB",7);owned={};
        CHECK(crew::AppendOwnedSupport(owned,state) && owned.objectsCount==0 && crew::ApplyCollected(owned));
        CHECK(edf::At<int>(decoy.bytes,edf::kTeam)==4);
        CHECK(edf::At<LONG>(decoy.control,12)==2);
        CHECK(crew::PvpNativeSupportOwner(decoyRef,owner,sizeof(owner)) && !std::strcmp(owner,"ownerA"));
        crew::PvpNativeForgetSupport(decoyRef);CHECK(edf::At<LONG>(decoy.control,12)==1);
        CHECK(!crew::PvpNativeSupportOwner(decoyRef,owner,sizeof(owner)));
        crew::PvpNativeForgetSupport(parentRef);CHECK(edf::At<LONG>(parent.control,12)==1);
        unreadableWeapon=nullptr;
        crew::ResetPvpNative();crew::PvpNativeClearSupportOwners();
    }
    {
        FakeObject driver(true,2,true),passenger(true,2,true),ride(false,5,true,true);
        edf::Put<void*>(ride.seat,edf::kSeatRider,driver.bytes);
        edf::Put<void*>(ride.seat,edf::kSeatRiderCtrl,driver.control);
        for(auto* human:{driver.bytes,passenger.bytes}) {
            edf::Put<void*>(human,0x1548,ride.bytes);edf::Put<void*>(human,crew::kHumanVehicleCtrl,ride.control);
        }
        crew::Collection riding{};
        CHECK(crew::AddRide(riding,passenger.bytes,1,"passengerPuid"));
        CHECK(crew::AddRide(riding,driver.bytes,1,"driverPuid"));
        CHECK(crew::ApplyCollected(riding));
        char owner[65]{};crew::ObjRef resolved{};
        const auto rideRef=crew::ObjRef::Of(ride.bytes);
        CHECK(crew::PvpNativeSupportDriver(rideRef,&resolved) && resolved.obj==driver.bytes && resolved.ctrl==driver.control);
        CHECK(crew::PvpNativeSupportOwner(rideRef,owner,sizeof(owner)) && !std::strcmp(owner,"driverPuid"));
        // Driver-first and passenger-last traversal has the same result.
        riding={};CHECK(crew::AddRide(riding,driver.bytes,1,"driverPuid"));
        CHECK(crew::AddRide(riding,passenger.bytes,1,"passengerPuid") && crew::ApplyCollected(riding));
        CHECK(crew::PvpNativeSupportOwner(rideRef,owner,sizeof(owner)) && !std::strcmp(owner,"driverPuid"));
        edf::Put<void*>(passenger.bytes,crew::kHumanVehicleCtrl,nullptr);
        riding={};CHECK(crew::AddRide(riding,driver.bytes,1,"driverPuid"));
        CHECK(crew::AddRide(riding,passenger.bytes,1,"passengerPuid") && crew::ApplyCollected(riding));
        CHECK(crew::PvpNativeSupportOwner(rideRef,owner,sizeof(owner)) && !std::strcmp(owner,"driverPuid"));
        // A driver change invalidates the old cache before the next Collect; a passenger never wins by team.
        edf::Put<void*>(ride.seat,edf::kSeatRider,passenger.bytes);
        edf::Put<void*>(ride.seat,edf::kSeatRiderCtrl,passenger.control);
        CHECK(!crew::PvpNativeSupportOwner(rideRef,owner,sizeof(owner)));
        CHECK(crew::RestoreAll());crew::ResetPvpNative();
        CHECK(edf::At<LONG>(ride.control,12)==1 && edf::At<LONG>(driver.control,12)==1);
    }
    VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    if(failures)return 1;
    std::printf("PVP native inheritance: %u additional checks passed\n",checks-start);return 0;
}
