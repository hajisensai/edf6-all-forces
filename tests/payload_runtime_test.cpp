#include "../src/payload.cpp"
#include <cstdio>
#include <initializer_list>
namespace crew {
void PumpAircraftPayloadUi(unsigned char*) noexcept {}
unsigned char* image=nullptr;
Config cfg;
ULONGLONG now=100;
bool authority=true;
unsigned char* trackedHuman=nullptr;
unsigned char* PlayerHuman() noexcept { return trackedHuman; }
Config& MutableConfig() { return cfg; }
const Config& Cfg() noexcept { return cfg; }
ULONGLONG GameMs() noexcept { return now; }
bool MapHoldsKeys() noexcept { return false; }
PluginBody BodyOf(const void*) noexcept { return PluginBody::none; }
bool AiGunner(const unsigned char*,const unsigned char* s) noexcept { return authority && SeatRider(s)==Rider::other; }
void Log(const char*,...) noexcept {}
bool IsFuelTank(const unsigned char* w) noexcept { return w[0x100]==3; }
bool IsLoadoutWeapon(const unsigned char* w) noexcept { return w[0x100]==2; }
bool IsStoreWeapon(const unsigned char* w) noexcept { return w[0x100]==2; }
const StoreSpec airSpec{L"AA", "AA", StoreRole::air,0,0},groundSpec{L"AG", "AG", StoreRole::ground,0,0},gunSpec{L"HP", "HP", StoreRole::gun,0,0},
                coaxSpec{L"MG", "MG", StoreRole::coax,0,0},
                npcPairedLeft{L"PAIR_L", "pair", StoreRole::gun,0,0,1,0},npcPairedRight{L"PAIR_R", "pair", StoreRole::gun,0,0,1,1},
                npcNativeHE{L"NATIVE_HE", "HE", StoreRole::gun,0,0,0,0,true},npcNativeAP{L"NATIVE_AP", "AP", StoreRole::gun,0,0,0,0,true};
const StoreSpec* StoreOf(const unsigned char* w) noexcept {
    return w[0x101]==8 ? &npcNativeHE : w[0x101]==9 ? &npcNativeAP : w[0x101]==6 ? &npcPairedLeft : w[0x101]==7 ? &npcPairedRight : w[0x101]==1 ? &airSpec : w[0x101]==2 ? &groundSpec : w[0x101]==3 ? &gunSpec : w[0x101]==5 ? &coaxSpec : nullptr;
}
int fileQueries=0;
const wchar_t* WeaponFile(const unsigned char* w,std::size_t* n) noexcept {
    ++fileQueries;const auto file=At<const wchar_t*>(w,0x08);*n=file ? std::wcslen(file) : 0;return file;
}
void ClearWeaponLock(unsigned char*) noexcept {}
int WeaponLock(const unsigned char*,float*,float*) noexcept { return 0; }
bool ReadRound(const unsigned char* w,RoundModel* m) noexcept {
    // 3: a catalogued gun round of the grenade class (the flak's HE proximity), 4: a stock grenade round
    *m={};m->kind=w[0x101]==1 || w[0x101]==2 ? RoundKind::homing : RoundKind::arc;m->lobbed=w[0x101]==3 || w[0x101]==4;m->speed=2;m->alive=300;return true;
}
namespace audio { void LockTone(int,float) noexcept {} }
}
namespace crew { bool friendInLine=false; bool FriendInLine(const float*,const float*,const void*) noexcept { return friendInLine; } }
namespace edf { bool MeanMuzzle(const unsigned char* w,std::uint64_t,float* p,float* d) noexcept { p[0]=p[1]=p[2]=0;d[0]=d[1]=0;d[2]=1;return w[0x102]!=1; } }
// The coax and the main gun's firing motion (2026-10-12): stand-ins for the 505's / 601's stock slot 48 and its
// VehicleBase part, reached through 12-byte jumps from their EDF.dll addresses in the stand-in image.
namespace {
int stockFiredRuns=0,vehicleFiredRuns=0;
void __fastcall StockFired(unsigned char*,unsigned char*) { ++stockFiredRuns; }
void __fastcall VehicleFired(unsigned char*,unsigned char*) { ++vehicleFiredRuns; }
void JumpAt(unsigned rva,const void* to) {
    unsigned char code[12]={0x48,0xB8};
    std::memcpy(code+2,&to,8);
    code[10]=0xFF;code[11]=0xE0;
    std::memcpy(crew::image+rva,code,sizeof(code));
}
void CallAt(unsigned at,unsigned target) {
    crew::image[at]=0xE8;
    const std::int32_t rel=static_cast<std::int32_t>(static_cast<std::int64_t>(target)-(static_cast<std::int64_t>(at)+5));
    std::memcpy(crew::image+at+1,&rel,sizeof(rel));
}
}  // namespace
int main() {
    using namespace crew;
    int fail=0,checks=0;
    auto check=[&](bool ok,const char* msg){++checks;if(!ok){++fail;std::printf("FAIL: %s\n",msg);}};
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    unsigned char vehicles[2][0x2400]{},seats[2][edf::kSeatStride*2]{},ctrl[2][16]{},riders[2][0x2000]{};
    unsigned char weapons[2][3][0x1600]{},holders[2][3][0x80]{};
    unsigned char* seatHolders[2][3]{};
    cfg.enabled=cfg.stockStores=cfg.npcGunners=true;pullOk=true;
    for(int j=0;j<2;++j) {
        auto v=vehicles[j];Put<void*>(v,0,image+kVt403);Put<void*>(v,kSelfCtrl,ctrl[j]);
        Put<void*>(v,kSeats,seats[j]);Put<std::uint64_t>(v,kSeatCount,2);Put<void*>(v,kHolders,holders[j]);Put<std::uint64_t>(v,kHolderCount,3);
        Put<int>(ctrl[j],8,1);
        auto seat=SeatAt(v,0);Put<void*>(seat,kSeatRider,riders[j]);Put<void*>(seat,kSeatRiderCtrl,ctrl[j]);
        for(int i=0;i<3;++i) {
            auto w=weapons[j][i];seatHolders[j][i]=holders[j][i];Put<void*>(holders[j][i],kHolderWeapon,w);Put<void*>(holders[j][i],kHolderCtrl,ctrl[j]);
            w[0x100]=i ? 2 : 0;w[0x101]=static_cast<unsigned char>(i);
            Put<int>(w,kWeaponAmmo,10);Put<int>(w,kWeaponCapacity,10);Put<int>(w,kWeaponReloadTime,-1);
            Put<float>(w,0x224,1000);Put<float>(w,0x89C,10);Put<float>(w,0x6D0,1000);Put<const wchar_t*>(w,kWeaponName,i ? L"missile" : L"cannon");
            Put<const wchar_t*>(w,0x08,i ? L"EDF6VC_AAM_S_2.SGO" : L"V_403TANK_CANNON01.SGO");
        }
        Put<void*>(seat,kSeatWeapons,seatHolders[j]);Put<std::uint64_t>(seat,kSeatWeaponCount,3);
    }
    check(NpcPayloadSelect(vehicles[0],0,75,false)==weapons[0][0],"driver uses loaded main cannon at close ground range");
    check(NpcPayloadSelect(vehicles[0],0,800,false)==weapons[0][2],"driver uses ground missile beyond cannon lifetime range");
    Put<int>(weapons[0][2],kWeaponAmmo,0);
    check(!NpcPayloadSelect(vehicles[0],0,800,false),"exhausted ground missile cannot be replaced by out-of-range cannon or AA missile");
    Put<int>(weapons[0][2],kWeaponAmmo,10);
    check(NpcPayloadSelect(vehicles[0],0,400,true)==weapons[0][1],"air target selects actual AA store");
    Put<const wchar_t*>(weapons[0][1],0x08,L"EDF6VC_COAX_MG.SGO");
    check(NpcPayloadSelect(vehicles[0],0,400,true)==weapons[0][0],"NPC excludes exact retired coax resource");
    Put<const wchar_t*>(weapons[0][1],0x08,L"EDF6VC_AAM_S_2.SGO");
    NpcPayloadSelect(vehicles[0],0,400,true);
    PullHook(holders[0][0]);check(weapons[0][1][kWeaponTrigger]==1 && !weapons[0][0][kWeaponTrigger],"native pull redirected once to selected weapon");
    check(NpcPayloadSelect(vehicles[1],0,400,false)==weapons[1][2],"second vehicle independently selects ground missile");
    PullHook(holders[0][0]);PullHook(holders[1][0]);check(weapons[0][1][kWeaponTrigger] && weapons[1][2][kWeaponTrigger],"multiple vehicle selections coexist");
    Put<int>(weapons[0][1],kWeaponAmmo,0);
    check(NpcPayloadSelect(vehicles[0],0,400,true)==weapons[0][0],"empty AA falls back to reachable installed cannon");
    Put<int>(weapons[0][0],kWeaponAmmo,0);
    check(!NpcPayloadSelect(vehicles[0],0,400,true),"ground missile cannot replace exhausted AA against aircraft");
    check(!NpcPayloadSelect(vehicles[1],0,1500,false),"all out of range returns no usable weapon");
    Put<int>(weapons[0][1],kWeaponAmmo,10);weapons[0][1][0x102]=1;
    check(!NpcPayloadSelect(vehicles[0],0,400,true),"missing muzzle cannot create a usable mount");
    weapons[0][1][0x102]=0;Put<float>(weapons[0][1],0x89C,-150);
    check(!NpcPayloadSelect(vehicles[0],0,400,true),"healer never selected for enemy");
    Put<float>(weapons[0][1],0x89C,10);NpcPayloadSelect(vehicles[0],0,400,true);
    weapons[0][1][kWeaponTrigger]=0;authority=false;PullHook(holders[0][0]);
    check(!weapons[0][1][kWeaponTrigger],"authority loss invalidates redirect before another shot");
    check(!NpcPayloadSelect(vehicles[0],0,400,true),"remote seat cannot select locally");authority=true;
    NpcPayloadSelect(vehicles[0],0,400,true);now+=201;PullHook(holders[0][0]);
    check(!weapons[0][1][kWeaponTrigger],"expired selection is not reused");
    // A removed or expired holder must not leave a stale redirect to an old weapon.
    now+=1;NpcPayloadSelect(vehicles[0],0,400,true);
    Put<std::uint64_t>(SeatAt(vehicles[0],0),kSeatWeaponCount,1);PullHook(holders[0][0]);
    check(!weapons[0][1][kWeaponTrigger],"removed store cannot receive a stale redirect");
    Put<std::uint64_t>(SeatAt(vehicles[0],0),kSeatWeaponCount,3);
    Put<int>(ctrl[0],8,0);
    check(!NpcPayloadSelect(vehicles[0],0,400,true),"expired holder is not an installed weapon");
    Put<int>(ctrl[0],8,1);
    // Real human control must never be selected by the NPC path.
    riders[0][edf::kHumanPlayer]=1;Put<void*>(riders[0],edf::kHumanPad,riders[0]);
    trackedHuman=riders[0];
    check(!NpcPayloadSelect(vehicles[0],0,400,true),"local human seat remains player controlled");
    PayloadFrame(vehicles[0]);PayloadReadout readout{};
    check(PlayerPayload(&readout) && readout.count==3 && readout.choices==3,"player HUD lists three real mounted weapons");
    check(readout.entry[0].rounds==0 && !wcscmp(readout.entry[0].name,L"cannon"),"HUD reports weapon identity and actual remaining rounds");
    Put<const wchar_t*>(weapons[0][1],0x08,L"edf6vc_coax_mg.sgo");PayloadFrame(vehicles[0]);
    check(PlayerPayload(&readout) && readout.count==2 && readout.choices==2,"player HUD and switching exclude retired coax data");
    for(const wchar_t* file:{L"V_403TANK_MACHINEGUN.SGO",L"EDF6VC_COAX_MG.SGO.OTHER"}) {
        Put<const wchar_t*>(weapons[0][1],0x08,file);PayloadFrame(vehicles[0]);
        check(PlayerPayload(&readout) && readout.count==3,"stock machine gun and other custom filenames remain mounted");
    }
    check(fileQueries>0,"selection queries real resource filenames");
    Put<std::uint64_t>(SeatAt(vehicles[0],0),kSeatWeaponCount,1);PayloadFrame(vehicles[0]);
    check(PlayerPayload(&readout) && readout.count==1 && readout.choices==0,"vehicle without additional mounts has no invented switch choices");
    // The flak's HE proximity round (StoreRole::gun) flies the grenade class: aimed at aircraft all the same;
    // a lobbed round that is no catalogued gun round is not.
    Put<int>(weapons[1][0],kWeaponAmmo,0);weapons[1][1][0x101]=3;
    check(NpcPayloadSelect(vehicles[1],0,400,true)==weapons[1][1],"gun-role proximity HE is used against aircraft");
    weapons[1][1][0x101]=4;
    check(!NpcPayloadSelect(vehicles[1],0,400,true),"a lobbed round that is no gun store stays off aircraft");
    // 2026-10-11 「应该有同轴机枪的坦克增加同轴机枪」 / 「用副武器的时候瞄具不会强制切回去」: a Blacker-class seat (its stock
    // input reads the primary alone) with its main gun, a store on the gun and the coaxial machine gun. The coax rides
    // the secondary trigger by itself: never one of R / LB's choices, its latch pulled only while that trigger is held,
    // the main gun's pick untouched; the fire-control view follows the secondary edge to it and back.
    {
        ResetPayload();now+=1000;
        auto v=vehicles[1];Put<void*>(v,0,image+0x1000);   // Class::other (not one of the classes read in §2)
        auto seat=SeatAt(v,0);
        for(int i=0;i<3;++i)weapons[1][i][kWeaponTrigger]=0;
        weapons[1][0][0x100]=0;weapons[1][0][0x101]=0;Put<int>(weapons[1][0],kWeaponAmmo,10);
        weapons[1][1][0x100]=2;weapons[1][1][0x101]=2;Put<int>(weapons[1][1],kWeaponAmmo,4);
        weapons[1][2][0x100]=2;weapons[1][2][0x101]=5;Put<int>(weapons[1][2],kWeaponAmmo,3000);
        Put<int>(weapons[1][2],kWeaponReloadTime,600);
        Put<const wchar_t*>(weapons[1][1],0x08,L"EDF6VC_GLM_4.SGO");Put<const wchar_t*>(weapons[1][2],0x08,L"EDF6VC_MG505_3000.SGO");
        coaxBeltOk=true;
        const auto colour=[&](const unsigned char* w){return reinterpret_cast<const float*>(w+kWeaponAmmoColor);};
        const auto is=[&](const unsigned char* w,const float* c){return std::memcmp(colour(w),c,sizeof(kCoaxTracer))==0;};
        Put<std::uint64_t>(seat,kSeatWeaponCount,3);
        riders[1][edf::kHumanPlayer]=1;Put<void*>(riders[1],edf::kHumanPad,riders[1]);trackedHuman=riders[1];
        Put<float>(seat,0x2E4,0.0f);Put<float>(seat,0x2E0,0.0f);
        PayloadFrame(v);PayloadReadout coax{};
        check(PlayerPayload(&coax) && coax.count==3 && coax.entry[2].coax && coax.entry[2].fire==PayloadFire::secondary,
              "the coaxial gun is the seat's secondary-trigger weapon");
        check(coax.choices==2 && !coax.entry[2].selectable && coax.entry[1].selectable,"R / LB cycles the main gun and its stores, never the coax");
        check(!weapons[1][2][kWeaponTrigger],"no trigger, no coax burst");
        check(PayloadSightPicked(v,0)==weapons[1][0],"the main gun owns the sight by default");
        now+=16;Put<float>(seat,0x2E0,1.0f);PayloadFrame(v);
        check(weapons[1][2][kWeaponTrigger]==1 && !weapons[1][0][kWeaponTrigger] && !weapons[1][1][kWeaponTrigger],
              "the secondary trigger pulls the coax alone");
        check(PayloadSightPicked(v,0)==weapons[1][2],"the secondary edge hands the fire-control view to the coax");
        check(is(weapons[1][2],kCoaxTracer),"3000 left: the round it fires is a tracer (one in five)");
        check(!is(weapons[1][0],kCoaxTracer) && !is(weapons[1][0],kCoaxBall),"the main gun's colour is never touched");
        // The belt's rhythm over the next rounds: 2999..2996 ball, 2995 tracer.
        bool rhythm=true;
        for(int left=2999;left>=2995;--left) {
            Put<int>(weapons[1][2],kWeaponAmmo,left);weapons[1][2][kWeaponTrigger]=0;now+=16;PayloadFrame(v);
            rhythm=rhythm && weapons[1][2][kWeaponTrigger]==1 && is(weapons[1][2],left%5==0 ? kCoaxTracer : kCoaxBall);
        }
        check(rhythm,"held, it keeps firing (its own interval paces it), four ball to one tracer");
        coaxBeltOk=false;Put<int>(weapons[1][2],kWeaponAmmo,2994);std::memcpy(weapons[1][2]+kWeaponAmmoColor,kCoaxTracer,16);
        weapons[1][2][kWeaponTrigger]=0;now+=16;PayloadFrame(v);
        check(weapons[1][2][kWeaponTrigger]==1 && is(weapons[1][2],kCoaxTracer),"an unread EDF.dll: the file's tracer on every round, still firing");
        coaxBeltOk=true;Put<int>(weapons[1][2],kWeaponAmmo,3000);
        Put<float>(seat,0x2E0,0.0f);weapons[1][2][kWeaponTrigger]=0;now+=16;PayloadFrame(v);
        check(!weapons[1][2][kWeaponTrigger] && PayloadSightPicked(v,0)==weapons[1][2],"let go: it stops, the view stays on it");
        Put<float>(seat,0x2E4,1.0f);now+=16;PayloadFrame(v);
        check(PayloadSightPicked(v,0)==weapons[1][0],"the primary edge takes the view back to the main gun");
        Put<float>(seat,0x2E4,0.0f);Put<int>(weapons[1][2],kWeaponAmmo,0);Put<int>(weapons[1][2],kWeaponReloadTime,-1);
        Put<float>(seat,0x2E0,1.0f);now+=16;PayloadFrame(v);
        check(!weapons[1][2][kWeaponTrigger],"a coax spent for good is not pulled");
        Put<float>(seat,0x2E0,0.0f);Put<int>(weapons[1][2],kWeaponAmmo,3000);Put<int>(weapons[1][2],kWeaponReloadTime,600);
        // An NPC crew (2026-10-11: 「ap弹和机枪不冲突吧，两个都用不就好了」): the coax is never the one weapon chosen; it
        // fires alongside the main gun whenever the gun is laid on the target close enough, a friend not in the line.
        riders[1][edf::kHumanPlayer]=0;trackedHuman=nullptr;
        PayloadFire fire=PayloadFire::other;
        check(NpcPayloadSelect(v,0,80,false,&fire)==weapons[1][0],"an NPC crew keeps its main gun on a close target");
        alignas(16) unsigned char enemy[0x400]{};
        float* where=reinterpret_cast<float*>(enemy+kPosition);where[0]=0;where[1]=-0.5f;where[2]=80;
        weapons[1][0][kWeaponTrigger]=weapons[1][2][kWeaponTrigger]=0;
        Put<int>(weapons[1][2],kWeaponAmmo,2999);
        check(NpcCoaxFire(v,0,enemy,&FriendInLine) && weapons[1][2][kWeaponTrigger]==1,"the coax fires alongside it, laid on the target");
        check(is(weapons[1][2],kCoaxBall),"an NPC's coax belt too: 2999 left, a ball round");
        Put<int>(weapons[1][2],kWeaponAmmo,3000);
        PullHook(holders[1][0]);
        check(weapons[1][0][kWeaponTrigger]==1,"and the main gun's own pull still lands on the main gun");
        weapons[1][2][kWeaponTrigger]=0;where[0]=30;
        check(!NpcCoaxFire(v,0,enemy,&FriendInLine) && !weapons[1][2][kWeaponTrigger],"not laid on it (20 degrees off): no coax burst");
        where[0]=0;where[2]=850;weapons[1][2][kWeaponTrigger]=0;
        check(NpcCoaxFire(v,0,enemy,&FriendInLine) && weapons[1][2][kWeaponTrigger]==1,"850 m: inside a 7.62 mm coax's reach, it fires");
        where[2]=950;check(!NpcCoaxFire(v,0,enemy,&FriendInLine),"beyond 900 m (its tracer burns out): the coax holds");
        where[2]=80;friendInLine=true;check(!NpcCoaxFire(v,0,enemy,&FriendInLine),"a friend in the line: the coax holds");friendInLine=false;
        check(!NpcCoaxFire(v,0,nullptr),"no target, no burst");
        Put<void*>(v,0,image+kVt403);
    }
    // 2026-10-12 「同轴机枪开火的时候主炮会伸缩」: the 505's and the 601's slot 48 play the main gun's firing motion for
    // whichever holder fired. Chained, a coax holder runs only the VehicleBase part; every other holder the class's own.
    {
        DWORD old=0;
        check(VirtualProtect(image,0x2200000,PAGE_EXECUTE_READWRITE,&old)!=0,"stand-in image executable");
        JumpAt(kVehicleFired,reinterpret_cast<const void*>(&VehicleFired));
        for(const auto& c:kMotionClasses) {
            reinterpret_cast<void**>(image+c.vtable)[kSlotFired]=image+c.fired;
            JumpAt(c.fired,reinterpret_cast<const void*>(&StockFired));
            CallAt(c.fired+kFiredVehicleCallAt,kVehicleFired);
            CallAt(c.motionCall,kPlayMotion);
        }
        CallAt(kMotionClasses[1].motionCall,kPlayMotion+0x10);   // the 601's motion call not the one read
        check(InstallCoaxMotion()==1 && reinterpret_cast<void**>(image+kMotionClasses[1].vtable)[kSlotFired]==image+kMotionClasses[1].fired,
              "an unread class keeps its stock slot 48");
        alignas(16) static unsigned char gun[0x1600]{},coax[0x1600]{},mainHolder[0x80]{},coaxHolder[0x80]{},emptyHolder[0x80]{},veh[0x100]{};
        coax[0x101]=5;                      // StoreRole::coax
        Put<void*>(mainHolder,kHolderWeapon,gun);Put<void*>(coaxHolder,kHolderWeapon,coax);
        const auto fire=[&](unsigned char* holder){
            reinterpret_cast<FiredFn>(reinterpret_cast<void**>(image+kMotionClasses[0].vtable)[kSlotFired])(veh,holder);
        };
        check(reinterpret_cast<void**>(image+kMotionClasses[0].vtable)[kSlotFired]!=image+kMotionClasses[0].fired,"the 505's slot 48 chained");
        stockFiredRuns=vehicleFiredRuns=0;fire(coaxHolder);
        check(stockFiredRuns==0 && vehicleFiredRuns==1,"a coax round: the VehicleBase part only, no main-gun motion");
        stockFiredRuns=vehicleFiredRuns=0;fire(mainHolder);
        check(stockFiredRuns==1 && vehicleFiredRuns==0,"a main-gun round: the class's own slot 48 (motion and hull push) as before");
        stockFiredRuns=vehicleFiredRuns=0;fire(emptyHolder);
        check(stockFiredRuns==1 && vehicleFiredRuns==0,"a holder with no weapon read: the class's own slot 48");
        reinterpret_cast<void**>(image+kMotionClasses[0].vtable)[kSlotFired]=image+kMotionClasses[0].fired;
        CallAt(kMotionClasses[1].motionCall,kPlayMotion);
        check(InstallCoaxMotion()==2,"both classes chain on the EDF.dll read");
        stockFiredRuns=vehicleFiredRuns=0;
        reinterpret_cast<FiredFn>(reinterpret_cast<void**>(image+kMotionClasses[1].vtable)[kSlotFired])(veh,coaxHolder);
        check(stockFiredRuns==0 && vehicleFiredRuns==1,"the 601's coax too");
        VirtualProtect(image,0x2200000,old,&old);
    }
    // NPC driver uses the same paired-barrel redirect as the player, not two pulls on the selected leader.
    {
        ResetPayload();now+=1000;trackedHuman=nullptr;
        static unsigned char v[0x2400]{},s[edf::kSeatStride]{},vc[16]{},rider[0x2000]{},rc[16]{},
                             w[4][0x1600]{},h[4][kHolderStride]{},hc[4][16]{};
        unsigned char* list[4]{};
        Put<void*>(v,0,image+kVt603);Put<void*>(v,kSelfCtrl,vc);Put<int>(vc,8,1);
        Put<void*>(v,kSeats,s);Put<std::uint64_t>(v,kSeatCount,1);Put<void*>(v,kHolders,h);Put<std::uint64_t>(v,kHolderCount,4);
        Put<void*>(s,kSeatRider,rider);Put<void*>(s,kSeatRiderCtrl,rc);Put<int>(rc,8,1);
        for(int i=0;i<4;++i) {
            list[i]=h[i];Put<void*>(h[i],kHolderWeapon,w[i]);Put<void*>(h[i],kHolderCtrl,hc[i]);Put<int>(hc[i],8,1);
            Put<int>(w[i],kWeaponAmmo,i<2 ? 0 : 10);Put<int>(w[i],kWeaponCapacity,10);Put<int>(w[i],kWeaponReloadTime,-1);
            Put<float>(w[i],0x224,1000);Put<float>(w[i],0x89C,10);Put<float>(w[i],0x8B0,i<2 ? 0.0f : 4.0f);
            Put<const wchar_t*>(w[i],kWeaponName,L"flak");Put<const wchar_t*>(w[i],8,L"V603_FLAK_GUNH01_DLC_L.SGO");
            if(i>=2){w[i][0x100]=2;w[i][0x101]=static_cast<unsigned char>(i+4);}
        }
        Put<void*>(s,kSeatWeapons,list);Put<std::uint64_t>(s,kSeatWeaponCount,4);
        check(NpcPayloadSelect(v,0,400,true)==w[2],"NPC chooses the real linked flak loadout for an aircraft");
        PullHook(h[0]);check(w[2][kWeaponTrigger] && !w[3][kWeaponTrigger],"NPC left source fires linked left only");
        w[2][kWeaponTrigger]=0;PullHook(h[1]);check(!w[2][kWeaponTrigger] && w[3][kWeaponTrigger],"NPC right source fires linked right only");
        w[3][kWeaponTrigger]=0;Put<int>(w[2],kWeaponAmmo,0);
        check(NpcPayloadSelect(v,0,400,true)==w[3],"NPC can choose the remaining right barrel when the linked leader is spent");
        PullHook(h[0]);PullHook(h[1]);check(!w[2][kWeaponTrigger] && w[3][kWeaponTrigger],"NPC cannot redirect both native sources into the surviving single barrel");
        w[3][kWeaponTrigger]=0;Put<std::uint64_t>(s,kSeatWeaponCount,2);
        w[0][0x100]=w[1][0x100]=2;w[0][0x101]=8;w[1][0x101]=9;Put<int>(w[1],kWeaponAmmo,10);
        check(NpcPayloadSelect(v,0,400,true)==w[1],"NPC selects AP native belt when HE is empty");
        PullHook(h[0]);w[1][kWeaponTrigger]=0;PullHook(h[1]);
        check(!w[0][kWeaponTrigger] && !w[1][kWeaponTrigger],"NPC second native source cannot repeat the selected AP shot");
        Put<int>(w[0],kWeaponAmmo,10);Put<float>(w[0],0x8B0,4);
        check(NpcPayloadSelect(v,0,400,false)==w[0],"NPC switches from AP back to HE against a ground target");
        PullHook(h[1]);check(!w[0][kWeaponTrigger] && !w[1][kWeaponTrigger],"NPC unselected native AP stays silent after changing back");
        PullHook(h[0]);check(w[0][kWeaponTrigger] && !w[1][kWeaponTrigger],"NPC native HE receives exactly its selected source");
        w[0][kWeaponTrigger]=0;cfg.stockStores=false;Put<int>(w[0],kWeaponAmmo,0);
        check(NpcPayloadSelect(v,0,400,true)==w[1],"NPC keeps native alternate ammunition selection when optional stores are off");
        PullHook(h[1]);check(!w[1][kWeaponTrigger],"optional stores off still suppresses NPC's redundant second native pull");
        PullHook(h[0]);check(w[1][kWeaponTrigger] && !w[0][kWeaponTrigger],"optional stores off preserves NPC single-belt firing");cfg.stockStores=true;
    }
    ResetPayload();check(!PayloadPicked(vehicles[0]),"mission reset clears choices");
    VirtualFree(image,0,MEM_RELEASE);std::printf("payload runtime: %d checks, %d failures\n",checks,fail);return fail ? 1 : 0;
}
