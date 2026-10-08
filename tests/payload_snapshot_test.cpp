// The renderer accessor must consume only an SRW-protected value snapshot, never game objects.
#define main ExistingPayloadSightMain
#include "payload_sight_test.cpp"
#undef main
#include <atomic>
#include <thread>
int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)return 2;
    SetUp();auto& c=cars[0];Tick(c);PayloadReadout initial{};
    Check(PlayerSelectablePayload(&initial),"game-thread payload frame publishes a render snapshot");
    const auto saved=choiceContext;const auto savedHuman=localHuman;const auto savedNow=now;
    // A renderer must still copy the already-published value even after native state is inaccessible.
    // The next game-thread pump, not this accessor, owns validating/invalidating those identities.
    choiceContext.vehicle.obj=reinterpret_cast<void*>(1);choiceContext.human.obj=reinterpret_cast<void*>(1);
    localHuman=reinterpret_cast<unsigned char*>(1);cfg.enabled=false;now+=1000000;
    PayloadReadout copied{};
    Check(PlayerSelectablePayload(&copied) && copied.selectionToken==initial.selectionToken && copied.count==initial.count &&
          copied.entry[1].rounds==initial.entry[1].rounds,"draw read ignores poisoned native context, PlayerHuman, game clock and mutable config");
    choiceContext=saved;localHuman=savedHuman;cfg.enabled=true;now=savedNow;
    Check(RequestPayloadSelection(initial.selectionToken,initial.seat,2),"old snapshot click queues before player transition is sampled");
    Board(c,0,1);Tick(c);PayloadReadout replaced{};PlayerSelectablePayload(&replaced);
    Check(replaced.selectionToken!=initial.selectionToken && PayloadPicked(c.vehicle)==c.weapon[3] &&
          !RequestPayloadSelection(initial.selectionToken,initial.seat,2),"next game-thread frame rejects old human token and queued selection");
    Leave(c,0);Tick(c);
    Check(!PlayerSelectablePayload(&copied),"game-thread departure clears complete snapshot under the same lock");
    SetUp();Tick(c);
    AcquireSRWLockExclusive(&choiceLock);selectableAt=GetTickCount64()-kFreshMs-1;ReleaseSRWLockExclusive(&choiceLock);
    Check(!PlayerSelectablePayload(&copied),"draw snapshot expires by wall time without querying the game clock");
    Check(!PlayerSelectablePayload(nullptr),"null output is rejected without touching state");

    SetUp();
    auto publish=[&](int tag) {
        for(const int slot : {0,3,6}) {Put<int>(c.weapon[slot],kWeaponAmmo,tag);Put<int>(c.weapon[slot],kWeaponCapacity,tag+100);}
        Put<void*>(c.holders[6],kHolderCtrl,c.weaponCtrl[tag%2 ? 7 : 6]);
        Tick(c);
    };
    publish(1);PlayerSelectablePayload(&initial);const auto firstToken=initial.selectionToken;
    std::atomic<bool> ready=false,stop=false;std::atomic<int> reads=0,bad=0;
    std::thread renderer([&] {
        PayloadReadout r{};
        do {
            if(PlayerSelectablePayload(&r)) {
                const int tag=r.entry[0].rounds;
                bool whole=r.count==3 && r.seat==0 && r.picked==1 && tag>0 && tag<=5000 &&
                    r.selectionToken==firstToken+static_cast<std::uint64_t>(tag-1);
                for(int i=0;i<3;++i)whole=whole && r.entry[i].rounds==tag && r.entry[i].capacity==tag+100;
                whole=whole && !r.entry[0].selectable && r.entry[1].selectable && r.entry[2].selectable;
                if(!whole)++bad;
                ++reads;
            }
            ready=true;
        } while(!stop.load());
    });
    while(!ready.load())std::this_thread::yield();
    for(int tag=2;tag<=5000;++tag)publish(tag);
    stop=true;renderer.join();
    Check(reads>0 && bad==0,"actual concurrent SRW publication/read keeps token, topology and all ammunition fields from one frame");
    PlayerSelectablePayload(&copied);
    Check(copied.entry[0].rounds==5000 && copied.selectionToken==firstToken+4999,"last complete snapshot survives concurrent render reads");
    ClearChoiceContext();Check(!PlayerSelectablePayload(&copied),"clear invalidates renderer publication atomically");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("payload snapshot: %d checks, %d failures; %d concurrent reads\n",checks,failed,reads.load());return failed?1:0;
}
