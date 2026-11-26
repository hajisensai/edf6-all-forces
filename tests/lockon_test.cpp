// Execute production NextLockTarget against stand-in game entry points and an observable remote weapon copy.
// The type-6 recorder applies the fields cleared by EDF+0x692482; the remote tick models EDF+0x691E50..0x691E68.
// No game is loaded, and no network message leaves this process.
#include "../src/lockon.cpp"
#include <cstdio>
#include <cstdlib>

namespace crew {
unsigned char* image=nullptr;
Config config{};
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
ULONGLONG GameMs() noexcept { return 1000; }
bool CameraRay(float*,float*) noexcept { return false; }
// Score's per-weapon facts (FactsOf): the frame and the store answer are the test's, the lookups counted.
ULONGLONG frameNow=1;
ULONGLONG GameFrame() noexcept { return frameNow; }
bool storeAnswer=false,salvoAnswer=false;
int storeQueries=0;
const StoreSpec* StoreOf(const unsigned char*) noexcept {
    ++storeQueries;
    static const StoreSpec generic{L"EDF6VC_AAM_", "AAM", StoreRole::air,0.0f,0.0f};
    static const StoreSpec salvo{L"EDF6VC_SALVO_", "Salvo", StoreRole::air,0.0f,0.0f};
    return salvoAnswer ? &salvo : storeAnswer ? &generic : nullptr;
}
}

namespace {
using namespace crew;
constexpr std::size_t kImageSize=0x700000,kCancelRva=0x695150;
constexpr std::size_t kRemoteEntry=0xC88,kRemoteCtrl=0xC90,kRemoteProgress=0xC98,kSoundTimer=0xCB8;
const unsigned char cancelHead[]={0x40,0x53,0x48,0x81,0xEC,0x40,0x06,0x00,0x00,0x48,0x8B,0x05,0xF8,0x9E,0x95,0x01};
alignas(16) unsigned char weapon[0xD00]{},remote[0xD00]{},listHead[0x28]{},lockNode[0x28]{},target[0x60]{},locked[0x60]{},ctrl[0x10]{};
int checks=0,cancelCalls=0,dropCalls=0,clearCalls=0;

struct ShotSeen { unsigned index; std::int32_t cursor,burstLeft; };
ShotSeen shots[300];
int shotCount=0;
bool refuseShots=false;
// Weapon_VehicleShoot's slot 16 standing in: records the round, takes it off the magazine (0x69820E's decrement).
void __fastcall RecordShot(unsigned char* w,unsigned index) {
    if(refuseShots)return;
    if(shotCount<300)shots[shotCount++]={index,At<std::int32_t>(w,kLockCursor),At<std::int32_t>(w,kBurstLeft)};
    Put<std::int32_t>(w,kRoundsLeft,At<std::int32_t>(w,kRoundsLeft)-1);
}

void Check(bool condition,const char* what) {
    ++checks;
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}

void Jump(std::size_t rva,const void* to) {
    unsigned char code[12]={0x48,0xB8};
    std::memcpy(code+2,&to,8);code[10]=0xFF;code[11]=0xE0;
    std::memcpy(image+rva,code,sizeof(code));
    FlushInstructionCache(GetCurrentProcess(),image+rva,sizeof(code));
}

void __fastcall Cancel(void* w) {
    Check(w==weapon,"cancel receives the weapon, not the pending-entry subobject");
    Check(dropCalls==0 && At<const void*>(weapon,kLocking)==target,"notify before dropping the local lock");
    Check(At<std::uint64_t>(weapon,kLockCount)==1,"notification keeps the completed lock list");
    ++cancelCalls;
    // Type 6: the receiver drops its weak entry, clears its control block and progress (0x692482..0x6924C0).
    Put<const void*>(remote,kRemoteEntry,nullptr);Put<const void*>(remote,kRemoteCtrl,nullptr);
    Put<std::uint32_t>(remote,kRemoteProgress,0);
}

void __fastcall Drop(void* entry) {
    Check(entry==weapon+kLocking,"drop receives weapon+C70");
    Check(cancelCalls==1 && !At<const void*>(remote,kRemoteEntry),"remote cancel precedes the local drop");
    ++dropCalls;
    // The local part (0x68FEE0): the pending weak reference and lock progress, not the completed list.
    Put<const void*>(entry,0,nullptr);Put<const void*>(entry,8,nullptr);Put<std::uint32_t>(entry,0x10,0);
}

void __fastcall Clear(void* w) {
    Check(w==weapon,"full clear receives the weapon");
    ++clearCalls;
    Put<std::uint64_t>(weapon,kLockCount,0);
    Put<const void*>(weapon,kLocking,nullptr);Put<const void*>(weapon,kLockingCtrl,nullptr);
}

void RemoteTick() {
    // Without type 6, the real remote tick keeps counting this old target as being locked, even without a new pick.
    auto entry=At<unsigned char*>(remote,kRemoteEntry);
    if(entry && At<const void*>(remote,kRemoteCtrl))
        Put<std::uint32_t>(entry,0x34,At<std::uint32_t>(entry,0x34)+1);
}
}  // namespace

int main() {
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,kImageSize,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"allocate a stand-in image");
    std::memcpy(image+kClearLock,kClearLockSig,sizeof(kClearLockSig));
    std::memcpy(image+kDropLocking,kDropLockingSig,sizeof(kDropLockingSig));
    std::memcpy(image+kPick,kPickSig,sizeof(kPickSig));
    // A missing cancellation signature must disable partial drops, even if the local drop function matches.
    InstallLockon();
    Check(clearOk && !dropOk,"unverified cancel sender cannot enable partial cancellation");
    std::memcpy(image+kCancelRva,cancelHead,sizeof(cancelHead));
    InstallLockon();
    Check(clearOk && dropOk,"matching cancel and drop signatures enable partial cancellation");
    Jump(kCancelRva,reinterpret_cast<const void*>(&Cancel));
    Jump(kDropLocking,reinterpret_cast<const void*>(&Drop));
    Jump(kClearLock,reinterpret_cast<const void*>(&Clear));
    Put<const void*>(weapon,kLockList,listHead);Put<std::uint64_t>(weapon,kLockCount,1);
    Put<const void*>(listHead,0,lockNode);Put<const void*>(lockNode,0,listHead);
    Put<const void*>(lockNode,kNodeEntry,locked);Put<const void*>(lockNode,kNodeCtrl,ctrl);
    Put<std::int32_t>(ctrl,8,1);
    Put<const void*>(weapon,kLocking,target);Put<const void*>(weapon,kLockingCtrl,ctrl);
    Put<float>(weapon,kLockProgress,12.0f);Put<std::uint32_t>(weapon,kLockFailed,7);
    Put<float>(weapon,kSoundTimer,19.0f);
    Put<const void*>(remote,kRemoteEntry,target);Put<const void*>(remote,kRemoteCtrl,ctrl);
    Put<float>(remote,kRemoteProgress,12.0f);
    RemoteTick();
    Check(At<std::uint32_t>(target,0x34)==1,"the stand-in remote initially reports the pending target");

    NextLockTarget(weapon);
    Check(cancelCalls==1 && dropCalls==1 && clearCalls==0,"partial cycle cancels then drops, without full clear");
    Check(!At<const void*>(weapon,kLocking) && !At<const void*>(weapon,kLockingCtrl) &&
          At<float>(weapon,kLockProgress)==0.0f,"the local pending lock is cleared");
    Check(At<std::uint64_t>(weapon,kLockCount)==1 && At<const void*>(listHead,0)==lockNode &&
          At<const void*>(lockNode,kNodeEntry)==locked,"completed lock and list links survive the target cycle");
    Check(At<std::uint32_t>(weapon,kLockFailed)==0 && At<float>(weapon,kSoundTimer)==0.0f,
          "failure and lock-sound timers reset as in stock cancellation");
    Check(skip.weapon==weapon && skip.count==1 && skip.entries[0]==target,"only the dropped target is cycled away");
    // No new candidate and therefore no type-5 replacement packet arrives.
    for(int frame=0;frame<120;++frame)RemoteTick();
    Check(At<std::uint32_t>(target,0x34)==1 && !At<const void*>(remote,kRemoteCtrl) &&
          At<float>(remote,kRemoteProgress)==0.0f,"no replacement candidate leaves no ghost lock on the remote copy");
    NextLockTarget(weapon);
    Check(clearCalls==1 && cancelCalls==1 && dropCalls==1,"with no pending lock, the existing full-clear path is used");
    Check(skip.count==1 && skip.entries[0]==locked,"full cycle records the completed lock as skipped");

    // Score reads the weapon's store / held facts once a frame, not once a candidate, and gives what a fresh read would.
    {
        alignas(16) static unsigned char picker[0xD00]{},nodeA[0x40]{},nodeB[0x40]{},result[0x40]{},crewless[0x1000]{};
        Put<const void*>(picker,kWeaponNode,nodeA);
        Put<float>(result,0,0.5f);Put<float>(result,4,0.25f);
        void* ref[1]={target};
        float score=-1.0f;
        storeAnswer=false;storeQueries=0;
        Check(!Score(ref,result,picker,&score) && !Score(ref,result,picker,&score) && storeQueries==1,
              "no store, not held: the stock order, the weapon read once in the frame");
        storeAnswer=true;
        Check(!Score(ref,result,picker,&score) && storeQueries==1,"the same frame keeps the frame's answer");
        ++frameNow;
        Check(Score(ref,result,picker,&score) && storeQueries==2 && score==0.5f*0.5f+0.25f*0.25f,
              "a new frame reads afresh: a store scores its stock yaw and pitch");
        storeAnswer=false;
        Put<const void*>(picker,kWeaponNode,nodeB);
        Check(!Score(ref,result,picker,&score) && storeQueries==3,"another weapon at the same address reads afresh");
        storeAnswer=true;
        Put<const void*>(picker,kWeaponOwner,crewless);   // a vehicle with no seats: still not held
        Check(Score(ref,result,picker,&score) && storeQueries==4,"the weapon changing hands reads afresh");
    }
    {
        alignas(16) static unsigned char launcher[0xD00]{}, head[0x28]{}, entries[4][0x30]{}, nodes[kSalvoLockLimit][0x28]{};
        static unsigned char enemies[3]{};
        salvoAnswer=true;
        Put<const void*>(launcher,kLockList,head);
        Put<const void*>(head,0,head);
        // Enemy zero has two lock points. They share its allocation.
        for(int i=0;i<4;++i)Put<const void*>(entries[i],kEntryObject,&enemies[i==3 ? 0 : i]);
        unsigned perEnemy[3]{};
        const unsigned char* tail=head;
        for(unsigned n=0;n<kSalvoLockLimit;++n) {
            ++frameNow;
            float best=static_cast<float>(kSalvoLockLimit);
            int chosen=-1;
            for(int i=0;i<4;++i) {
                alignas(16) unsigned char result[0x40]{};
                Put<float>(result,0,i==3 ? 0.01f : static_cast<float>(i+1)*0.1f);
                void* ref[]={entries[i]};
                float score=0.0f;
                Check(Score(ref,result,launcher,&score),"salvo entrant scored");
                if(score<best){best=score;chosen=i;}
            }
            const int enemy=chosen==3 ? 0 : chosen;
            Check(enemy==static_cast<int>(n%3),"all enemies get a round before any repeat, even with multiple lock points");
            ++perEnemy[enemy];
            Put<const void*>(const_cast<unsigned char*>(tail),0,nodes[n]);
            Put<const void*>(nodes[n],0,head);
            Put<const void*>(nodes[n],kNodeEntry,entries[chosen]);
            Put<const void*>(nodes[n],kNodeCtrl,ctrl);
            Put<std::uint64_t>(launcher,kLockCount,static_cast<unsigned>(n+1));
            tail=nodes[n];
        }
        Check(launcher[0x78E]==0 && perEnemy[0]==86 && perEnemy[1]==85 && perEnemy[2]==85,
              "a full list is balanced 86, 85, 85 across three enemies, the weapon's repeat byte untouched");
    }
    {
        // The volleys (ShotHook): kVolley rounds a call from the fire start and the burst step alike, round n's cursor
        // at its enemy's node, the burst run on to the magazine's end; every other weapon one shot as before.
        alignas(16) static unsigned char gun[0x1000]{},gunHead[0x28]{},gunNodes[3][0x28]{},gunEntries[3][0x30]{},dead[0x10]{};
        static unsigned char foes[2]{};
        static void* gunVt[kShotSlot+1]{};
        gunVt[kShotSlot]=reinterpret_cast<void*>(&RecordShot);
        Put<void* const*>(gun,0,gunVt);
        Put<std::int32_t>(gun,kAmmoCount,salvo::kRounds);Put<std::int32_t>(gun,kRoundsLeft,salvo::kRounds);
        Put<std::int32_t>(gun,kLockShare,0);gun[kLockKept]=1;
        // Nodes: foe 0, foe 1, foe 0's second lock point.
        Put<const void*>(gun,kLockList,gunHead);
        const unsigned char* tail=gunHead;
        for(int i=0;i<3;++i) {
            Put<const void*>(gunEntries[i],kEntryObject,&foes[i==2 ? 0 : i]);
            Put<const void*>(const_cast<unsigned char*>(tail),0,gunNodes[i]);
            Put<const void*>(gunNodes[i],0,gunHead);
            Put<const void*>(gunNodes[i],kNodeEntry,gunEntries[i]);
            Put<const void*>(gunNodes[i],kNodeCtrl,ctrl);
            tail=gunNodes[i];
        }
        Put<std::uint64_t>(gun,kLockCount,3);
        salvoAnswer=true;shotCount=0;
        Put<std::int32_t>(gun,kBurstLeft,2);   // the stock's start: k - 1 for its 3 locks
        ShotHook(gun,0);
        Check(shotCount==salvo::kVolley && At<std::int32_t>(gun,kRoundsLeft)==salvo::kRounds-salvo::kVolley &&
              At<std::int32_t>(gun,kBurstLeft)==salvo::kRounds-salvo::kVolley,
              "the fire start fires a volley and the burst runs on past its 3 locks");
        const int nodeOf[]={0,1,2,1,0,1,2,1};   // foe r % 2; foe 0's two points in turn
        bool order=true;
        for(int r=0;r<salvo::kVolley;++r)
            order=order && shots[r].index==static_cast<unsigned>(r) && shots[r].cursor==nodeOf[r] &&
                  shots[r].burstLeft==salvo::kRounds-r-1;
        Check(order,"each round its own index (its rail), its enemy's node, the burst left after it");
        int calls=1;
        while(At<std::int32_t>(gun,kRoundsLeft)>0 && calls<100) {
            Put<std::int32_t>(gun,kBurstLeft,At<std::int32_t>(gun,kBurstLeft)-1);   // the burst step's own decrement
            ShotHook(gun,static_cast<unsigned>(salvo::kRounds-At<std::int32_t>(gun,kRoundsLeft)));
            ++calls;
        }
        Check(calls==salvo::kRounds/salvo::kVolley && shotCount==salvo::kRounds && shots[salvo::kRounds-1].burstLeft==0 &&
              At<std::int32_t>(gun,kBurstLeft)==0,"256 rounds in 32 calls, the last timed by FireInterval");
        int per[2]={};
        for(int r=0;r<salvo::kRounds;++r)++per[shots[r].cursor==1 ? 1 : 0];
        Check(per[0]==128 && per[1]==128,"two enemies share the salvo evenly");
        // No live lock: the cursor past the last node (unguided).
        for(int i=0;i<3;++i)Put<const void*>(gunNodes[i],kNodeCtrl,dead);
        Put<std::int32_t>(gun,kRoundsLeft,salvo::kRounds);shotCount=0;
        ShotHook(gun,0);
        Check(shotCount==salvo::kVolley && shots[0].cursor==3 && shots[7].cursor==3,"no live lock: unguided rounds");
        // A shot that takes no round ends the volley.
        refuseShots=true;shotCount=0;
        const std::int32_t before=At<std::int32_t>(gun,kRoundsLeft);
        ShotHook(gun,0);
        Check(At<std::int32_t>(gun,kRoundsLeft)==before && At<std::int32_t>(gun,kBurstLeft)==before,"a refused shot stops the volley");
        refuseShots=false;
        // Not the salvo: one shot, nothing of the weapon's touched.
        salvoAnswer=false;shotCount=0;
        Put<std::int32_t>(gun,kBurstLeft,5);Put<std::int32_t>(gun,kLockCursor,7);
        ShotHook(gun,9);
        Check(shotCount==1 && shots[0].index==9 && shots[0].cursor==7 && At<std::int32_t>(gun,kBurstLeft)==5,
              "every other weapon: the stock's one shot");
    }
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("lockon: %d production cancellation checks passed\n",checks);
    return 0;
}
