#include "../src/payload.cpp"
#include "seat_net_fake.inc"
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
bool mapHeld=false;
bool MapHoldsKeys() noexcept { return mapHeld; }
PluginBody BodyOf(const void*) noexcept { return PluginBody::none; }
bool AiGunner(const unsigned char*,const unsigned char* s) noexcept { return authority && SeatRider(s)==Rider::other; }
void Log(const char*,...) noexcept {}
bool IsFuelTank(const unsigned char* w) noexcept { return w[0x100]==3; }
bool IsLoadoutWeapon(const unsigned char* w) noexcept { return w[0x100]==2; }
bool IsStoreWeapon(const unsigned char* w) noexcept { return w[0x100]==2; }
const StoreSpec airSpec{L"AA", "AA", StoreRole::air,0,0},groundSpec{L"AG", "AG", StoreRole::ground,0,0},gunSpec{L"HP", "HP", StoreRole::gun,0,0},
                coaxSpec{L"MG", "MG", StoreRole::coax,0,0},
                npcPairedLeft{L"PAIR_L", "pair", StoreRole::gun,0,0,1,0},npcPairedRight{L"PAIR_R", "pair", StoreRole::gun,0,0,1,1},
                npcNativeHE{L"NATIVE_HE", "HE", StoreRole::gun,0,0,0,0,true},npcNativeAP{L"NATIVE_AP", "AP", StoreRole::gun,0,0,0,0,true},
                bmptMissile{L"BMPT_ATGM", "Ataka", StoreRole::ground,0,0,0,0,false,StoreShell::native,kSecondTriggerKey},
                bmptApLeft{L"BMPT_AP", "APDS", StoreRole::gun,0,0,3,0},bmptApRight{L"BMPT_AP_R", "APDS", StoreRole::gun,0,0,3,1},
                bmptThermobaric{L"BMPT_ATGMF", "Ataka F", StoreRole::ground,0,0,0,0,false,StoreShell::native,kSecondTriggerKey},
                // an X-Morph heavy tank's arms (pylib/xm_units.py): its machine gun on the second trigger, its anti-air
                // missiles on fire key 3 and an anti-air gun on key 4, both auto turrets
                xmMg{L"XM_MG", "MG", StoreRole::gun,0,0,0,0,false,StoreShell::native,kSecondTriggerKey},
                xmAam{L"XM_AAM", "AAM", StoreRole::air,0,0,0,0,false,StoreShell::native,3,true},
                xmAa{L"XM_AA", "AA", StoreRole::gun,0,0,0,0,false,StoreShell::native,4,true};
const StoreSpec* StoreOf(const unsigned char* w) noexcept {
    if(w[0x101]>=20 && w[0x101]<=22)return w[0x101]==20 ? &xmMg : w[0x101]==21 ? &xmAam : &xmAa;
    return w[0x101]==10 ? &bmptMissile : w[0x101]==11 ? &bmptApLeft : w[0x101]==12 ? &bmptApRight : w[0x101]==13 ? &bmptThermobaric : w[0x101]==8 ? &npcNativeHE : w[0x101]==9 ? &npcNativeAP : w[0x101]==6 ? &npcPairedLeft : w[0x101]==7 ? &npcPairedRight : w[0x101]==1 ? &airSpec : w[0x101]==2 ? &groundSpec : w[0x101]==3 ? &gunSpec : w[0x101]==5 ? &coaxSpec : nullptr;
}
unsigned TwinPairOf(const unsigned char* w) noexcept { return w[0x103]; }   // stores.cpp's, by kTwinBarrels
std::uint16_t padButtons=0;   // the XInput pad's buttons (map.cpp MapPadState): the own secondary's switch on a pad
bool MapPadState(std::uint16_t* b,std::uint8_t* lt,std::uint8_t* rt) noexcept { *b=padButtons;*lt=*rt=0;return true; }
int fileQueries=0;
const wchar_t* WeaponFile(const unsigned char* w,std::size_t* n) noexcept {
    ++fileQueries;const auto file=At<const wchar_t*>(w,0x08);*n=file ? std::wcslen(file) : 0;return file;
}
void ClearWeaponLock(unsigned char*) noexcept {}
int WeaponLock(const unsigned char*,float*,float*) noexcept { return 0; }
bool ReadRound(const unsigned char* w,RoundModel* m) noexcept {
    // 3: a catalogued gun round of the grenade class (the flak's HE proximity), 4: a stock grenade round
    *m={};m->kind=w[0x101]==1 || w[0x101]==2 || w[0x101]==10 || w[0x101]==13 || w[0x101]==21 ? RoundKind::homing : RoundKind::arc;m->lobbed=w[0x101]==3 || w[0x101]==4;m->speed=2;m->alive=300;return true;
}
namespace audio { void LockTone(int,float) noexcept {} }
}
namespace crew { bool friendInLine=false; bool FriendInLine(const float*,const float*,const void*) noexcept { return friendInLine; } }
// The game's lock (lockon.cpp), the target's height over the ground (npcai.cpp) and the enemy list (heli.cpp), as the
// auto turrets read them: one enemy at a time.
namespace crew {
const void* lockObject=nullptr;
const void* WeaponLockObject(const unsigned char*) noexcept { return lockObject; }
bool airTarget=false;
bool GunnerAirTarget(const void*) noexcept { return airTarget; }
const void* enemyObject=nullptr;
bool VisitEnemies(const unsigned char*,EnemyVisitor visit,void* ctx) noexcept {
    if(enemyObject){const float* p=reinterpret_cast<const float*>(static_cast<const unsigned char*>(enemyObject)+kPosition);visit(ctx,enemyObject,p);}
    return true;
}
}
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
    unsigned char weapons[2][3][0x1600]{},holders[2][3][kHolderStride]{};
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
    // Online (seat_net.h Pick): a seat's pick goes to the vehicle's copies; there the seat's pulls land on it too.
    {
        using seat_net::Pick;
        ResetPayload();now+=1000;cfg.stockStores=true;
        auto v=vehicles[0];auto seat=SeatAt(v,0);
        // The vehicle's holder array at the real stride (veh +0x638): a pick names its weapon by the holder's index there.
        alignas(16) static unsigned char array[3*kHolderStride]{};
        unsigned char* const* const oldList=At<unsigned char* const*>(seat,kSeatWeapons);
        static unsigned char* list[3]{};
        for(int i=0;i<3;++i){list[i]=array+i*kHolderStride;Put<void*>(list[i],kHolderWeapon,weapons[0][i]);Put<void*>(list[i],kHolderCtrl,ctrl[0]);}
        Put<void*>(v,kHolders,array);Put<void*>(seat,kSeatWeapons,list);
        unsigned char* holders0[3]={list[0],list[1],list[2]};
        Put<void*>(v,0,image+kVt403);Put<std::uint64_t>(seat,kSeatWeaponCount,3);Put<std::uint64_t>(v,kHolderCount,3);
        for(int i=0;i<3;++i){Put<int>(weapons[0][i],kWeaponAmmo,10);weapons[0][i][kWeaponTrigger]=0;weapons[0][i][0x102]=0;
                             Put<float>(weapons[0][i],0x89C,10);}
        weapons[0][1][0x101]=1;Put<const wchar_t*>(weapons[0][1],0x08,L"EDF6VC_AAM_S_2.SGO");
        pickNetOk=seat_net::Register(seat_net::kPickMagic,&PickReceived);   // InstallPayload's, past its native pull patch
        check(pickNetOk && seat_net::fakeMagic==seat_net::kPickMagic && seat_net::fakeHandler,"the pick kind has its receiver");
        // This machine's player in the gunner seat, on a pad: LB picks the next weapon; the pick goes out at once.
        riders[0][edf::kHumanPlayer]=1;Put<void*>(riders[0],edf::kHumanPad,riders[0]);riders[0][edf::kRiderNet+edf::kNetFlags]=0;
        trackedHuman=riders[0];authority=true;
        seat[kSeatPad]=1;Put<std::uint16_t>(seat,kSeatButtons,0);
        fakeSession=true;Put<std::uint16_t>(v,0x128,2);
        int from=seat_net::fakeSends;
        now+=16;PayloadFrame(v);
        Pick sent{};
        check(seat_net::LastPick(from,&sent) && sent.seat==0 && sent.holder==0 && !(sent.flags&seat_net::kNpc),
              "boarding: the seat's pick (the main gun, holder 0) goes to the copies");
        from=seat_net::fakeSends;now+=16;PayloadFrame(v);
        check(seat_net::fakeSends==from,"unchanged within the refresh: nothing sent");
        Put<std::uint16_t>(seat,kSeatButtons,0x10);now+=16;PayloadFrame(v);Put<std::uint16_t>(seat,kSeatButtons,0);
        check(seat_net::LastPick(from,&sent) && sent.holder==1,"LB: the new pick (holder 1) at once");
        from=seat_net::fakeSends;now+=seat_net::kPickRefreshMs;PayloadFrame(v);
        check(seat_net::LastPick(from,&sent) && sent.holder==1,"refreshed every kPickRefreshMs");
        fakeSession=false;from=seat_net::fakeSends;now+=seat_net::kPickRefreshMs;PayloadFrame(v);
        check(seat_net::fakeSends==from,"offline: no pick goes out");
        // Another machine's copy of that vehicle: the seat's rider is that machine's player (its network flag), this
        // machine runs no one there. Before any pick its pulls are the stock gun's.
        ResetPayload();fakeSession=true;authority=false;trackedHuman=nullptr;
        riders[0][edf::kRiderNet+edf::kNetFlags]=1;Put<void*>(riders[0],edf::kHumanPad,nullptr);
        check(SeatRider(seat)==Rider::other && IsAnyPlayer(riders[0]),"fixture: another machine's player in the seat");
        for(int i=0;i<3;++i)weapons[0][i][kWeaponTrigger]=0;
        now+=16;PayloadFrame(v);PullHook(holders0[0]);
        check(weapons[0][0][kWeaponTrigger]==1 && !weapons[0][1][kWeaponTrigger],"no pick heard: the stock pull is the gun's");
        weapons[0][0][kWeaponTrigger]=0;
        sent.head.sender=0x77;sent.head.sequence=5;sent.head.controller=-1;
        seat_net::fakeHandler(v,&sent,sizeof(sent));
        now+=16;PayloadFrame(v);PullHook(holders0[0]);
        check(weapons[0][1][kWeaponTrigger]==1 && !weapons[0][0][kWeaponTrigger],"the remote player's pick: this copy's pull lands on it too");
        check(!PayloadPicked(v),"a remote pick is no pick of this machine's player (its HUD, its sight)");
        Pick back=sent;back.holder=0;back.head.sequence=4;
        seat_net::fakeHandler(v,&back,sizeof(back));
        weapons[0][1][kWeaponTrigger]=0;now+=16;PayloadFrame(v);PullHook(holders0[0]);
        check(weapons[0][1][kWeaponTrigger]==1,"an older message from that sender cannot undo the pick");
        Pick epoch=sent;epoch.holder=0;epoch.head.sequence=9;epoch.head.controller=42;
        seat_net::fakeHandler(v,&epoch,sizeof(epoch));
        weapons[0][1][kWeaponTrigger]=0;now+=16;PayloadFrame(v);PullHook(holders0[0]);
        check(weapons[0][1][kWeaponTrigger]==1,"a message of another control epoch (a rider who left) is refused");
        Pick bad=sent;bad.reserved=1;bad.head.sequence=10;bad.holder=0;
        seat_net::fakeHandler(v,&bad,sizeof(bad));seat_net::fakeHandler(v,&bad,sizeof(bad)-4);
        weapons[0][1][kWeaponTrigger]=0;now+=16;PayloadFrame(v);PullHook(holders0[0]);
        check(weapons[0][1][kWeaponTrigger]==1,"a malformed block is ignored");
        weapons[0][1][kWeaponTrigger]=0;now+=seat_net::kPickStaleMs+1;PayloadFrame(v);PullHook(holders0[0]);
        check(weapons[0][0][kWeaponTrigger]==1 && !weapons[0][1][kWeaponTrigger],"a pick not refreshed goes stale: stock again");
        weapons[0][0][kWeaponTrigger]=0;
        // The record went with it, the sender's watermark did not (2026-10-20 review: it lived in the record): a delayed
        // older pick is still refused, a newer one taken; another room's members start the watermarks over.
        Pick late=sent;late.head.sequence=3;
        seat_net::fakeHandler(v,&late,sizeof(late));
        now+=16;PayloadFrame(v);PullHook(holders0[0]);
        check(weapons[0][0][kWeaponTrigger]==1 && !weapons[0][1][kWeaponTrigger],"a delayed older pick after the record expired: still refused");
        weapons[0][0][kWeaponTrigger]=0;
        Pick newer=sent;newer.head.sequence=6;
        seat_net::fakeHandler(v,&newer,sizeof(newer));
        now+=16;PayloadFrame(v);PullHook(holders0[0]);
        check(weapons[0][1][kWeaponTrigger]==1 && !weapons[0][0][kWeaponTrigger],"a newer pick of that sender: taken");
        weapons[0][1][kWeaponTrigger]=0;
        seat_net::fakeHandler(v,&late,sizeof(late));
        now+=16;PayloadFrame(v);PullHook(holders0[0]);
        check(weapons[0][1][kWeaponTrigger]==1,"...and the old one after it refused");
        weapons[0][1][kWeaponTrigger]=0;
        Pick gun=sent;gun.holder=0;gun.head.sequence=7;
        seat_net::fakeHandler(v,&gun,sizeof(gun));
        ++plugin_net::fakeRoomGeneration;
        seat_net::fakeHandler(v,&late,sizeof(late));   // sequence 3, the store: below 7, but the room is another now
        now+=16;PayloadFrame(v);PullHook(holders0[0]);
        check(weapons[0][1][kWeaponTrigger]==1 && !weapons[0][0][kWeaponTrigger],"another room's members: the watermarks start over");
        weapons[0][1][kWeaponTrigger]=0;
        now+=seat_net::kPickStaleMs+1;PayloadFrame(v);
        // This machine's own player takes the seat: a pick heard for it never applies (it runs the seat itself).
        sent.head.sequence=20;seat_net::fakeHandler(v,&sent,sizeof(sent));
        riders[0][edf::kRiderNet+edf::kNetFlags]=0;Put<void*>(riders[0],edf::kHumanPad,riders[0]);trackedHuman=riders[0];
        sent.head.sequence=21;seat_net::fakeHandler(v,&sent,sizeof(sent));
        Put<std::uint16_t>(seat,kSeatButtons,0);now+=16;PayloadFrame(v);
        PayloadReadout local{};
        check(PlayerPayload(&local) && local.picked==0,"the local player's own pick (the gun), not the remote one");
        // An NPC gunner: the machine that runs it (AiGunner) sends its pick, flagged; a copy lands its pulls the same way.
        ResetPayload();trackedHuman=nullptr;riders[0][edf::kHumanPlayer]=0;Put<void*>(riders[0],edf::kHumanPad,nullptr);
        riders[0][edf::kRiderNet+edf::kNetFlags]=0;authority=true;
        from=seat_net::fakeSends;
        check(NpcPayloadSelect(v,0,400,true)==weapons[0][1],"the NPC picks the AA store against an aircraft");
        check(seat_net::LastPick(from,&sent) && sent.holder==1 && (sent.flags&seat_net::kNpc),"...and sends it, flagged as an NPC's");
        ResetPayload();authority=false;   // the copy: this machine does not run that NPC
        sent.head.sender=0x99;seat_net::fakeHandler(v,&sent,sizeof(sent));
        for(int i=0;i<3;++i)weapons[0][i][kWeaponTrigger]=0;
        now+=16;PayloadFrame(v);PullHook(holders0[0]);
        check(weapons[0][1][kWeaponTrigger]==1 && !weapons[0][0][kWeaponTrigger],"the host's NPC pick: the copy's pull lands on the store");
        fakeSession=false;now+=16;PayloadFrame(v);weapons[0][1][kWeaponTrigger]=0;PullHook(holders0[0]);
        check(weapons[0][0][kWeaponTrigger]==1 && !weapons[0][1][kWeaponTrigger],"the session left: remote picks dropped");
        seat[kSeatPad]=0;Put<std::uint16_t>(v,0x128,0);authority=true;
        Put<void*>(v,kHolders,holders[0]);Put<void*>(seat,kSeatWeapons,const_cast<unsigned char**>(oldList));
        for(int i=0;i<3;++i)weapons[0][i][kWeaponTrigger]=0;
    }
    // BMPT: the authored V603 has two native cannon sources and one independent missile holder.
    // Both real seat triggers may be held together; R/LB must never turn them into alternate ammunition.
    {
        ResetPayload();now+=1000;
        auto* v=vehicles[1];auto* s=SeatAt(v,0);
        Put<void*>(v,0,image+kVt603);Put<std::uint64_t>(v,kSeatCount,1);
        Put<std::uint64_t>(s,kSeatWeaponCount,3);
        trackedHuman=riders[1];riders[1][edf::kHumanPlayer]=1;Put<void*>(riders[1],edf::kHumanPad,riders[1]);
        cfg.stockStores=true;
        for(int i=0;i<3;++i) {
            auto* w=weapons[1][i];w[0x100]=i==2 ? 2 : 0;w[0x101]=i==2 ? 10 : 0;
            Put<int>(w,kWeaponAmmo,10);Put<int>(w,kWeaponReloadTime,-1);w[kWeaponTrigger]=0;
            Put<float>(w,0x224,1800);Put<float>(w,0x6D0,1800);Put<float>(w,0x8B0,i==2 ? 6.0f : 1.2f);
        }
        Put<float>(s,0x2E4,0);Put<float>(s,0x2E0,0);PayloadFrame(v);
        PayloadReadout r{};
        check(PlayerPayload(&r) && r.count==3 && r.entry[0].fire==PayloadFire::primary && r.entry[1].fire==PayloadFire::primary &&
              r.entry[2].fire==PayloadFire::secondary,"BMPT: both cannons are primary, the missile is independent secondary");
        check(!r.choices && !r.entry[0].selectable && !r.entry[1].selectable && !r.entry[2].selectable && !PayloadPicked(v) &&
              !r.selectionToken && !RequestPayloadSelection(r.selectionToken,r.seat,2),
              "BMPT: no weapon is an R/LB alternate and no missile redirect exists");
        Put<unsigned char>(s,kSeatPad,1);Put<std::uint16_t>(s,kSeatButtons,kButtonLB);now+=16;PayloadFrame(v);
        check(PlayerPayload(&r) && !r.choices && !weapons[1][2][kWeaponTrigger],"LB cannot select or launch a BMPT missile");
        Put<float>(s,0x2E4,1);now+=16;PayloadFrame(v);PullHook(holders[1][0]);PullHook(holders[1][1]);
        check(weapons[1][0][kWeaponTrigger] && weapons[1][1][kWeaponTrigger] && !weapons[1][2][kWeaponTrigger],
              "primary pulls both cannons and never the missile");
        weapons[1][0][kWeaponTrigger]=weapons[1][1][kWeaponTrigger]=0;
        Put<float>(s,0x2E0,1);now+=16;PayloadFrame(v);PullHook(holders[1][0]);PullHook(holders[1][1]);
        check(weapons[1][0][kWeaponTrigger] && weapons[1][1][kWeaponTrigger] && weapons[1][2][kWeaponTrigger],
              "holding both actual seat triggers fires both cannons and the independent missile together");
        check(PayloadSightPicked(v,0)==weapons[1][2],"secondary trigger edge selects the missile optic without changing cannon firing");
        unsigned char* all[kMostPayload]{};
        check(PayloadTriggerWeapons(v,0,all,kMostPayload)==3 && all[0]==weapons[1][0] && all[1]==weapons[1][1] && all[2]==weapons[1][2],
              "HUD enumerates the simultaneous cannon and missile controls");
        const unsigned char* aimed=nullptr;
        check(PayloadAimWeapon(v,0,weapons[1][0],&aimed) && aimed==weapons[1][0] &&
              PayloadAimWeapon(v,0,weapons[1][2],&aimed) && aimed==weapons[1][2],"aim API keeps the native cannon and missile groups distinct");
        cfg.stockStores=false;weapons[1][2][kWeaponTrigger]=0;now+=16;PayloadFrame(v);
        check(weapons[1][2][kWeaponTrigger] && NativePayloadControl(v,0),"BMPT intrinsic missile remains available when optional stores are disabled");
        weapons[1][2][kWeaponTrigger]=0;Put<float>(s,0x2E0,0);now+=16;PayloadFrame(v);
        check(!weapons[1][2][kWeaponTrigger],"releasing secondary stops further missile pulls");
        Put<float>(s,0x2E0,1);mapHeld=true;now+=16;PayloadFrame(v);
        check(!weapons[1][2][kWeaponTrigger],"map-owned local controls never launch the missile");mapHeld=false;
        Put<int>(weapons[1][2],kWeaponAmmo,0);now+=16;PayloadFrame(v);
        weapons[1][0][kWeaponTrigger]=weapons[1][1][kWeaponTrigger]=0;PullHook(holders[1][0]);PullHook(holders[1][1]);
        check(!weapons[1][2][kWeaponTrigger] && weapons[1][0][kWeaponTrigger] && weapons[1][1][kWeaponTrigger],
              "an empty missile cannot consume or suppress either cannon");
        Put<int>(weapons[1][2],kWeaponAmmo,4);riders[1][edf::kHumanPlayer]=0;trackedHuman=nullptr;
        PayloadFire fire=PayloadFire::other;
        check(NpcPayloadSelect(v,0,400,false,&fire)==weapons[1][0] && fire==PayloadFire::primary &&
              NpcSecondarySelect(v,0,400,false)==weapons[1][2],"NPC plans main cannon and guided secondary independently");
        check(NpcSecondarySelect(v,0,1000,false)==weapons[1][2] && !NpcPayloadSelect(v,0,1000,false,&fire),
              "missile remains usable beyond cannon lifetime reach without inventing a cannon shot");
        check(PayloadAimWeapon(v,0,weapons[1][0],&aimed) && !aimed &&
              PayloadAimWeapon(v,0,weapons[1][2],&aimed) && aimed==weapons[1][2],
              "NPC out-of-range main plan is authoritative empty while the independent missile keeps its own aim group");
        check(!NpcSecondarySelect(v,0,400,true) && !NpcSecondarySelect(v,0,1801,false),"ground missile respects role and actual native lock reach");
        weapons[1][0][kWeaponTrigger]=weapons[1][1][kWeaponTrigger]=weapons[1][2][kWeaponTrigger]=0;
        NpcPayloadSelect(v,0,400,false,&fire);Put<float>(s,0x2E0,1);
        check(!FireIndependentSecondary(v,0,weapons[1][0]) && !weapons[1][2][kWeaponTrigger],
              "NPC cannot pull a missile using another weapon's native approval");
        check(FireIndependentSecondary(v,0,weapons[1][2]),"authoritative NPC secondary intent pulls only its approved intrinsic missile");
        PullHook(holders[1][0]);PullHook(holders[1][1]);
        check(weapons[1][0][kWeaponTrigger] && weapons[1][1][kWeaponTrigger] && weapons[1][2][kWeaponTrigger],
              "NPC missile pull never redirects or suppresses either main cannon");
        check(!NpcPayloadSelect(v,0,0.0f,false,&fire) && PayloadAimWeapon(v,0,weapons[1][0],&aimed) && !aimed,
              "NPC target loss stays authoritative empty for the primary with optional stores disabled");
        weapons[1][2][kWeaponTrigger]=0;authority=false;
        check(!NpcSecondarySelect(v,0,400,false) && !FireIndependentSecondary(v,0),"remote or lost NPC authority cannot plan or fire a second copy");authority=true;
        Put<int>(ctrl[1],8,0);
        check(!NpcSecondarySelect(v,0,400,false) && !FireIndependentSecondary(v,0),"expired live holders cannot plan or launch stale missiles");Put<int>(ctrl[1],8,1);
        cfg.stockStores=true;
    }
    // BMPT as built (2026-10-21, docs/feedback-2026-10-21-bmpt-fire.md): six holders, the native HE guns L/R (primary),
    // the Ataka, the APDS belt L/R (group 3) and the 9M120F. The user: 「bmpt应该是双炮管轮流开火的……没办法机炮和导弹
    // 一块开火」: the cannons on the primary, either missile on the secondary, both at once; the barrels in turn.
    {
        ResetPayload();now+=1000;cfg.stockStores=true;twinOk=true;
        auto* v=vehicles[1];auto* s=SeatAt(v,0);
        alignas(16) static unsigned char array[6*kHolderStride]{};
        static unsigned char w6[6][0x1600]{};
        static unsigned char* list[6]{};
        const wchar_t* const files[6]={L"EDF6VC_BMPT_L.SGO",L"EDF6VC_BMPT_R.SGO",L"EDF6VC_BMPT_ATGM_4.SGO",
                                       L"EDF6VC_BMPT_AP_150.SGO",L"EDF6VC_BMPT_AP_R_150.SGO",L"EDF6VC_BMPT_ATGMF_4.SGO"};
        const unsigned char kinds[6]={0,0,10,11,12,13},pairs[6]={1,1,0,2,2,0};
        for(int i=0;i<6;++i) {
            auto* w=w6[i];list[i]=array+i*kHolderStride;
            Put<void*>(list[i],kHolderWeapon,w);Put<void*>(list[i],kHolderCtrl,ctrl[1]);
            w[0x100]=i<2 ? 0 : 2;w[0x101]=kinds[i];w[0x102]=0;w[0x103]=pairs[i];Put<const wchar_t*>(w,0x08,files[i]);
            Put<int>(w,kWeaponAmmo,10);Put<int>(w,kWeaponReloadTime,-1);w[kWeaponTrigger]=0;
            Put<float>(w,0x224,1800);Put<float>(w,0x6D0,1800);Put<float>(w,0x8B0,kinds[i]==10 || kinds[i]==13 ? 6.0f : 1.2f);
            Put<float>(w,0x89C,10);Put<std::int32_t>(w,kWeaponInterval,12);Put<float>(w,kWeaponCooldown,0.0f);
            Put<float>(w,kWeaponCooldownStep,1.0f);
        }
        unsigned char** const oldList=At<unsigned char**>(s,kSeatWeapons);
        void* const oldHolders=At<void*>(v,kHolders);
        Put<void*>(v,0,image+kVt603);Put<std::uint64_t>(v,kSeatCount,1);Put<void*>(v,kHolders,array);Put<std::uint64_t>(v,kHolderCount,6);
        Put<void*>(s,kSeatWeapons,list);Put<std::uint64_t>(s,kSeatWeaponCount,6);
        trackedHuman=riders[1];riders[1][edf::kHumanPlayer]=1;Put<void*>(riders[1],edf::kHumanPad,riders[1]);
        Put<unsigned char>(s,kSeatPad,1);Put<std::uint16_t>(s,kSeatButtons,0);
        const auto latches=[&]() {unsigned m=0;for(int i=0;i<6;++i)if(w6[i][kWeaponTrigger])m|=1u<<i;return m;};
        const auto clear=[&]() {for(auto& w:w6)w[kWeaponTrigger]=0;};
        // One frame: the seat's triggers, the stock 603 input's two pulls on the primary, the frame's twin cadence.
        const auto frame=[&](bool primaryHeld,bool secondaryHeld) {
            clear();Put<float>(s,0x2E4,primaryHeld ? 1.0f : 0.0f);Put<float>(s,0x2E0,secondaryHeld ? 1.0f : 0.0f);
            now+=16;PayloadFrame(v);
            if(primaryHeld){PullHook(list[0]);PullHook(list[1]);}
            TwinBarrelFrame(v);
            return latches();
        };
        frame(false,false);
        PayloadReadout r{};
        check(PlayerPayload(&r) && r.count==6 && r.entry[0].fire==PayloadFire::primary && r.entry[1].fire==PayloadFire::primary &&
              r.entry[2].fire==PayloadFire::secondary && r.entry[5].fire==PayloadFire::secondary &&
              r.entry[3].fire==PayloadFire::store && r.entry[4].fire==PayloadFire::store,
              "BMPT x6: the guns and their APDS belt on the primary, both Ataka rounds on the secondary");
        check(r.entry[0].selectable && r.entry[3].selectable && r.entry[2].selectable && r.entry[5].selectable && r.choices==2 &&
              r.entry[0].picked && r.entry[2].picked && !r.entry[5].picked,
              "BMPT x6: two cycles (HE / APDS, Ataka / 9M120F), the native belt and the Ataka picked first");
        // The two controls at once: both guns' pulls stay theirs, the Ataka's latch rides the secondary.
        unsigned m=frame(true,true);
        check((m&1u) && (m&4u) && !(m&2u) && !(m&0x38u),
              "both triggers: the left gun and the Ataka fire together; the right barrel waits its turn (twin cadence)");
        Put<float>(w6[0],kWeaponCooldown,12.0f);Put<int>(w6[0],kWeaponAmmo,9);   // the left barrel's shot
        m=frame(true,true);
        check((m&1u) && !(m&2u) && (m&4u),"the left just fired (its pull on a cooling gun): the right is held while the left is in its first half interval");
        Put<float>(w6[0],kWeaponCooldown,7.0f);m=frame(true,true);   // 6 after this frame's countdown: half its 12
        check((m&2u) && (m&1u) && (m&4u),"half an interval on: the right barrel fires (the left's own cooldown keeps it from firing)");
        Put<float>(w6[1],kWeaponCooldown,12.0f);Put<int>(w6[1],kWeaponAmmo,9);Put<float>(w6[0],kWeaponCooldown,0.0f);
        m=frame(true,false);
        check(!(m&1u) && !(m&4u),"the right just fired: the left waits; secondary released, no missile");
        Put<float>(w6[1],kWeaponCooldown,7.0f);m=frame(true,false);
        check((m&1u),"...and fires half an interval after the right: left, right, left");
        // A partner that cannot fire is none: the left alone fires at its own rate.
        Put<float>(w6[1],kWeaponCooldown,12.0f);Put<int>(w6[1],kWeaponAmmo,0);m=frame(true,false);
        check((m&1u),"a barrel out of rounds holds nothing back");
        Put<int>(w6[1],kWeaponAmmo,9);Put<float>(w6[1],kWeaponCooldown,0.0f);Put<float>(w6[0],kWeaponCooldown,0.0f);
        Put<int>(w6[0],kWeaponAmmo,9);
        m=frame(true,false);
        check((m&1u) && !(m&2u),"both ready, the same rounds: the left first");
        Put<int>(w6[0],kWeaponAmmo,8);m=frame(true,false);
        check((m&2u) && !(m&1u),"both ready: the barrel with more rounds left first (the belts stay even)");
        twinOk=false;m=frame(true,false);
        check((m&1u) && (m&2u),"cadence off (EDF.dll not the one read): the stock pair fires together");twinOk=true;
        // R / LB with the primary in use: the APDS belt, both barrels, still in turn.
        frame(true,false);
        Put<std::uint16_t>(s,kSeatButtons,kButtonLB);frame(false,false);Put<std::uint16_t>(s,kSeatButtons,0);
        check(PlayerPayload(&r) && r.entry[3].picked && r.entry[4].picked && r.entry[2].picked && !r.entry[5].picked,
              "LB after a primary edge: the gun belt moves to APDS, the missile pick is left alone");
        for(auto& w:w6){Put<float>(w,kWeaponCooldown,0.0f);Put<int>(w,kWeaponAmmo,9);}
        m=frame(true,true);
        check((m&8u) && !(m&16u) && (m&4u) && !(m&3u),"APDS picked: the pulls land on the APDS barrels, in turn; the Ataka beside them");
        // A secondary edge, then R / LB: the missile cycle moves, the cannons keep APDS.
        // Each switch its own control, whatever was fired last (the user, 2026-10-21: 「学学战雷」): LB after a
        // secondary edge still the gun's rounds; the d-pad right (VehicleSecondarySwitchButton) the missile, after a
        // primary edge too.
        frame(false,false);frame(false,true);frame(false,false);   // a secondary edge of its own (released first)
        Put<std::uint16_t>(s,kSeatButtons,kButtonLB);frame(false,false);Put<std::uint16_t>(s,kSeatButtons,0);
        check(PlayerPayload(&r) && r.entry[0].picked && !r.entry[3].picked && r.entry[2].picked && !r.entry[5].picked,
              "LB after a secondary edge: the gun's rounds (APDS -> HE), never the missile");
        frame(false,false);Put<std::uint16_t>(s,kSeatButtons,kButtonLB);frame(false,false);Put<std::uint16_t>(s,kSeatButtons,0);
        check(PlayerPayload(&r) && r.entry[3].picked && r.entry[2].picked,"LB again: APDS, the missile still the Ataka");
        frame(true,false);frame(false,false);   // a primary edge
        padButtons=0x0008;frame(false,false);padButtons=0;
        check(PlayerPayload(&r) && r.entry[5].picked && !r.entry[2].picked && r.entry[3].picked && r.ownPicked==5 &&
              r.ownChoices==2 && r.picked==3 && r.choices==2 && r.ownSwitchButton==cfg.vehicleSecondarySwitchButton,
              "d-pad right after a primary edge: the 9M120F picked, the APDS belt kept; the readout names both cycles");
        padButtons=0x0008;frame(false,false);
        check(PlayerPayload(&r) && r.entry[5].picked,"the switch held: one press, one step");padButtons=0;
        Put<unsigned char>(s,kSeatPad,0);mapHeld=true;frame(false,false);mapHeld=false;Put<unsigned char>(s,kSeatPad,1);
        check(PlayerPayload(&r) && r.entry[5].picked,"a pad seat ignores the keyboard key; the map holding keys switches nothing");
        m=frame(true,true);
        check((m&32u) && !(m&4u) && (m&8u),"secondary pulls the 9M120F only; the primary still the APDS belt");
        unsigned char* all[kMostPayload]{};
        check(PayloadTriggerWeapons(v,0,all,kMostPayload)==3 && all[0]==w6[3] && all[1]==w6[4] && all[2]==w6[5],
              "the HUD's trigger weapons: the APDS pair, then only the missile the secondary fires");
        frame(false,false);frame(false,true);
        check(PayloadSightPicked(v,0)==w6[5],"the secondary in use: the sight follows the picked missile");
        // The 9M120F spent for good: the secondary moves to the Ataka by itself.
        Put<int>(w6[5],kWeaponAmmo,0);frame(false,false);
        check(PlayerPayload(&r) && r.entry[2].picked && !r.entry[5].selectable,"a spent own round: the next one with rounds");
        Put<int>(w6[5],kWeaponAmmo,4);
        // Optional stores off: the guns' APDS belt is a store and goes, the own missiles stay (the vehicle's own).
        cfg.stockStores=false;m=frame(true,true);
        check((m&1u) && (m&4u) && !(m&8u),"StockVehicleStores=0: HE guns and the Ataka still fire together");
        cfg.stockStores=true;
        // NPC: the cannons and a missile planned apart, the missile pick remembered for its aim.
        trackedHuman=nullptr;riders[1][edf::kHumanPlayer]=0;Put<void*>(riders[1],edf::kHumanPad,nullptr);
        PayloadFire fire=PayloadFire::other;
        unsigned char* const npcGun=NpcPayloadSelect(v,0,400,false,&fire);
        check((npcGun==w6[0] || npcGun==w6[3]) && fire==PayloadFire::primary,"NPC: a belt of the gun pair on the primary, never a missile");
        unsigned char* const npcMissile=NpcSecondarySelect(v,0,400,false);
        check(npcMissile==w6[2] || npcMissile==w6[5],"NPC: one of the own missiles on the secondary");
        clear();Put<float>(s,0x2E0,1.0f);
        check(FireIndependentSecondary(v,0,npcMissile) && npcMissile[kWeaponTrigger] && latches()==(npcMissile==w6[2] ? 4u : 32u),
              "NPC: only the approved missile is pulled");
        Put<float>(s,0x2E4,1.0f);PullHook(list[0]);PullHook(list[1]);TwinBarrelFrame(v);
        {const unsigned g=latches()&0x1Bu;check(g && !(g&(g-1)),"NPC: its guns fire in turn too (one barrel of the picked belt)");}
        const unsigned char* aimed=nullptr;
        check(PayloadAimWeapon(v,0,w6[5],&aimed) && aimed==w6[5],"an own missile aims as itself");
        // A copy of a remote player's BMPT: the copy's own pulls (its input) in turn as well; no missile pulled here.
        riders[1][edf::kRiderNet+edf::kNetFlags]=1;authority=false;
        clear();Put<float>(s,0x2E0,1.0f);PayloadFrame(v);
        check(!(latches()&36u),"a copy never pulls a remote seat's missile (its shot comes from the shooter's machine)");
        PullHook(list[0]);PullHook(list[1]);TwinBarrelFrame(v);
        check((latches()&3u)==1u || (latches()&3u)==2u,"a copy's pulls of the guns: in turn");
        // ...and that copy hearing the seat's pick (2026-10-24 review: the remote HE pick was redirected because the seat
        // has an intrinsic secondary, the left pull mapped to itself and the right one to no target: latches L=1 R=0).
        // A pick lands the copy's pulls as the shooter's own machine does (Switch): HE leaves the stock pair theirs,
        // APDS moves both onto its barrels; the cadence comes after (TwinBarrelFrame), so both pulls are seen first.
        {
            ResetPayload();fakeSession=true;Put<std::uint16_t>(v,0x128,2);Put<float>(s,0x2E0,0.0f);
            pickNetOk=seat_net::Register(seat_net::kPickMagic,&PickReceived);
            seat_net::Pick heard{};heard.head.sender=0x77;heard.head.controller=-1;
            std::uint32_t sequence=0;
            const auto hear=[&](std::int32_t holder,bool npc) {
                heard.seat=0;heard.holder=holder;heard.flags=npc ? seat_net::kNpc : 0;heard.head.sequence=++sequence;
                seat_net::fakeHandler(v,&heard,sizeof(heard));
                clear();now+=16;PayloadFrame(v);PullHook(list[0]);PullHook(list[1]);
                return latches();
            };
            for(const bool npc:{false,true}) {
                riders[1][edf::kHumanPlayer]=npc ? 0 : 1;Put<void*>(riders[1],edf::kHumanPad,nullptr);
                check(npc ? !AiGunner(v,s) : SeatRider(s)==Rider::other && IsAnyPlayer(riders[1]),
                      npc ? "fixture: another machine's NPC gunner in the BMPT" : "fixture: another machine's player in the BMPT");
                m=hear(0,npc);
                check(m==3u,npc ? "remote NPC picks HE: both stock pulls latch their own barrel (L and R)"
                                : "remote player picks HE: both stock pulls latch their own barrel (L and R)");
                m=hear(3,npc);
                check(m==24u,npc ? "remote NPC picks APDS: both pulls land on the APDS pair, none on HE"
                                 : "remote player picks APDS: both pulls land on the APDS pair, none on HE");
                m=hear(0,npc);
                check(m==3u,"back to HE: the stock pair again, both barrels");
                ResetPayload();
            }
            riders[1][edf::kHumanPlayer]=0;fakeSession=false;Put<std::uint16_t>(v,0x128,0);
        }
        riders[1][edf::kRiderNet+edf::kNetFlags]=0;authority=true;
        Put<void*>(v,kHolders,oldHolders);Put<std::uint64_t>(v,kHolderCount,3);
        Put<void*>(s,kSeatWeapons,oldList);Put<std::uint64_t>(s,kSeatWeaponCount,3);
        Put<float>(s,0x2E4,0);Put<float>(s,0x2E0,0);Put<unsigned char>(s,kSeatPad,0);twinOk=false;
    }
    // The own fire keys (2026-10-30, docs/multi-weapon-keys.md; the user: 「不是只有两个攻击按键的，很多东西可以同时发射，
    // 不一定需要切换副挂载才行。多加几个按键的事情。还有一些炮塔可以是自动无人炮塔（需要可开关）」): an X-Morph heavy tank on
    // the V603, its native gun pair on the primary, its machine gun on the second trigger, its anti-air missiles on fire key
    // 3 and an anti-air gun on key 4 (both auto turrets). Every key fires at once; nothing is cycled; the auto turrets fire
    // on their own and switch off and on.
    {
        ResetPayload();now+=1000;cfg.stockStores=true;
        auto* v=vehicles[1];auto* s=SeatAt(v,0);
        alignas(16) static unsigned char array[5*kHolderStride]{};
        static unsigned char w5[5][0x1600]{};
        static unsigned char* list[5]{};
        static unsigned char enemy[0x200]{};
        const wchar_t* const files[5]={L"EDF6VC_XM_LARGE_TANK_L.SGO",L"EDF6VC_XM_LARGE_TANK_R.SGO",L"EDF6VC_XM_MG_600.SGO",
                                       L"EDF6VC_XM_AAM_40.SGO",L"EDF6VC_XM_AA_600.SGO"};
        const unsigned char kinds[5]={0,0,20,21,22};
        for(int i=0;i<5;++i) {
            auto* w=w5[i];list[i]=array+i*kHolderStride;
            Put<void*>(list[i],kHolderWeapon,w);Put<void*>(list[i],kHolderCtrl,ctrl[1]);
            w[0x100]=i<2 ? 0 : 2;w[0x101]=kinds[i];w[0x102]=0;w[0x103]=0;Put<const wchar_t*>(w,0x08,files[i]);
            Put<int>(w,kWeaponAmmo,40);Put<int>(w,kWeaponReloadTime,-1);w[kWeaponTrigger]=0;
            Put<float>(w,0x224,1000);Put<float>(w,0x6D0,1500);Put<float>(w,0x8B0,1.0f);Put<float>(w,0x89C,10);
            Put<std::int32_t>(w,kWeaponLockon,kinds[i]==21 ? kHoming : 0);
            Put<const wchar_t*>(w,kWeaponName,i<2 ? L"gun" : kinds[i]==20 ? L"MG" : kinds[i]==21 ? L"AAM" : L"AA");
        }
        unsigned char** const oldList=At<unsigned char**>(s,kSeatWeapons);
        void* const oldHolders=At<void*>(v,kHolders);
        Put<void*>(v,0,image+kVt603);Put<std::uint64_t>(v,kSeatCount,1);Put<void*>(v,kHolders,array);Put<std::uint64_t>(v,kHolderCount,5);
        Put<void*>(s,kSeatWeapons,list);Put<std::uint64_t>(s,kSeatWeaponCount,5);
        trackedHuman=riders[1];riders[1][edf::kHumanPlayer]=1;Put<void*>(riders[1],edf::kHumanPad,riders[1]);
        Put<unsigned char>(s,kSeatPad,1);Put<std::uint16_t>(s,kSeatButtons,0);padButtons=0;
        cfg.vehicleFire3Button=0x0004;cfg.vehicleFire4Button=0x0100;cfg.vehicleAutoTurretButton=0x0200;cfg.autoTurrets=false;
        float* const at=reinterpret_cast<float*>(enemy+kPosition);
        const auto latches=[&]() {unsigned m=0;for(int i=0;i<5;++i)if(w5[i][kWeaponTrigger])m|=1u<<i;return m;};
        const auto frame=[&](bool primaryHeld,bool secondaryHeld,std::uint16_t pad) {
            for(auto& w:w5)w[kWeaponTrigger]=0;
            padButtons=pad;Put<float>(s,0x2E4,primaryHeld ? 1.0f : 0.0f);Put<float>(s,0x2E0,secondaryHeld ? 1.0f : 0.0f);
            now+=16;PayloadFrame(v);
            if(primaryHeld){PullHook(list[0]);PullHook(list[1]);}
            return latches();
        };
        frame(false,false,0);
        PayloadReadout r{};
        check(PlayerPayload(&r) && r.count==5 && r.entry[0].fire==PayloadFire::primary && r.entry[1].fire==PayloadFire::primary &&
              r.entry[2].fire==PayloadFire::secondary && r.entry[3].fire==PayloadFire::key && r.entry[4].fire==PayloadFire::key &&
              r.entry[2].key==2 && r.entry[3].key==3 && r.entry[4].key==4,
              "own keys: the gun pair on the primary, the MG on the second trigger, the missiles on key 3, the AA gun on key 4");
        check(!r.choices && r.ownChoices<=1 && r.autoTurrets==2 && !r.entry[2].autoTurret && r.entry[3].autoTurret &&
              r.entry[4].autoTurret && !r.autoOn && PayloadControlsShown(r),
              "nothing to cycle; two auto turrets (off, as the ini starts them); the HUD's control row shown");
        check(frame(false,false,0)==0u,"no key held, the turrets off: nothing fires");
        check(frame(false,false,0x0004)==8u,"fire key 3 (its pad bits): the missiles alone");
        check(frame(false,false,0x0100)==16u,"fire key 4: the AA gun alone");
        check(frame(true,true,0x0104)==31u,"both triggers and keys 3 and 4 held: the gun pair, the MG, the missiles and the AA gun at once");
        mapHeld=true;
        check(frame(false,false,0x0104)==0u,"the map holding the keys: no extra key fires");mapHeld=false;
        Put<unsigned char>(s,kSeatPad,0);
        check(frame(false,false,0x0104)==0u,"a keyboard seat reads no pad bits");Put<unsigned char>(s,kSeatPad,1);
        unsigned char* all[kMostPayload]{};
        check(PayloadTriggerWeapons(v,0,all,kMostPayload)==5,"the HUD marks every weapon on a trigger or a key");
        // The auto turrets on: each fires itself when it has a shot.
        cfg.autoTurrets=true;at[0]=0;at[1]=100;at[2]=500;
        check(frame(false,false,0)==0u && PlayerPayload(&r) && r.autoOn,"turrets on, no lock, no enemy: they hold");
        lockObject=enemy;airTarget=true;
        check(frame(false,false,0)==8u,"the missiles' lock holds an aircraft: they launch by themselves");
        airTarget=false;
        check(frame(false,false,0)==0u,"locked on something on the ground: the anti-air missiles hold");
        lockObject=nullptr;enemyObject=enemy;at[0]=0;at[1]=0;at[2]=300;
        check(frame(false,false,0)==16u,"an enemy on the AA gun's line in its reach: it fires by itself");
        at[0]=110;
        check(frame(false,false,0)==0u,"20 degrees off its line: it holds (it aims nothing: the turret turns it)");
        at[0]=0;at[2]=700;
        check(frame(false,false,0)==0u,"beyond its round's reach: it holds");
        at[2]=300;friendInLine=true;
        check(frame(false,false,0)==0u,"a friend in the line: it holds");friendInLine=false;
        // The switch: one press, one change; off, the gun is the key's alone.
        frame(false,false,0x0200);
        check(PlayerPayload(&r) && !r.autoOn && frame(false,false,0x0200)==0u && PlayerPayload(&r) && !r.autoOn,
              "the auto turret switch: off, held it stays off, the gun holds its fire");
        check(frame(false,false,0x0100)==16u,"off: its own key still fires it");
        frame(false,false,0);frame(false,false,0x0200);
        check(PlayerPayload(&r) && r.autoOn && frame(false,false,0)==16u,"pressed again: on, firing by itself");
        ResetPayload();now+=1000;cfg.autoTurrets=false;frame(false,false,0);
        check(PlayerPayload(&r) && !r.autoOn,"a new mission: the ini's state again");
        Put<int>(w5[4],kWeaponAmmo,0);cfg.autoTurrets=true;
        check(frame(false,false,0x0100)==0u,"a spent weapon is never pulled, by its key or by itself");
        Put<int>(w5[4],kWeaponAmmo,40);
        // An NPC crew: the gun pair planned as ever, never one of the keyed weapons; those fired alongside.
        trackedHuman=nullptr;riders[1][edf::kHumanPlayer]=0;Put<void*>(riders[1],edf::kHumanPad,nullptr);
        PayloadFire fire=PayloadFire::other;
        unsigned char* const npcGun=NpcPayloadSelect(v,0,300,false,&fire);
        check((npcGun==w5[0] || npcGun==w5[1]) && fire==PayloadFire::primary,"NPC: the gun pair on the primary, never a keyed weapon");
        check(NpcSecondarySelect(v,0,300,false)==w5[2],"NPC: the second trigger's MG planned apart");
        for(auto& w:w5)w[kWeaponTrigger]=0;
        check(NpcOwnKeysFire(v,0,enemy,&FriendInLine) && latches()==16u,"NPC: the AA gun laid on its target fires; the missiles, unlocked, hold");
        for(auto& w:w5)w[kWeaponTrigger]=0;lockObject=enemy;airTarget=true;at[0]=110;
        check(NpcOwnKeysFire(v,0,enemy,&FriendInLine) && latches()==8u,"NPC: the missiles locked on an aircraft launch; the gun off its line holds");
        lockObject=nullptr;airTarget=false;at[0]=0;authority=false;for(auto& w:w5)w[kWeaponTrigger]=0;
        check(!NpcOwnKeysFire(v,0,enemy,&FriendInLine) && !latches(),"another machine's NPC: nothing fired here");authority=true;
        check(!NpcOwnKeysFire(v,0,nullptr),"no target, no shot");
        // The second trigger is only the V603's to give; the extra keys are no class's own input.
        Put<void*>(v,0,image+kVt403);
        PayloadReadout c403{};unsigned char* ws[kMostPayload]{};
        c403.count=ReadSeat(v,0,ClassOf(v),ws,c403,false);
        check(c403.count==5 && c403.entry[2].fire==PayloadFire::store && !c403.entry[2].key && c403.entry[3].key==3 &&
              c403.entry[4].fire==PayloadFire::key,"another class: the second-trigger weapon a plain store, the extra keys still keys");
        Put<void*>(v,0,image+kVt603);
        enemyObject=nullptr;padButtons=0;cfg.autoTurrets=true;
        Put<void*>(v,kHolders,oldHolders);Put<std::uint64_t>(v,kHolderCount,3);
        Put<void*>(s,kSeatWeapons,oldList);Put<std::uint64_t>(s,kSeatWeaponCount,3);
        Put<float>(s,0x2E4,0);Put<float>(s,0x2E0,0);Put<unsigned char>(s,kSeatPad,0);
    }
    ResetPayload();check(!PayloadPicked(vehicles[0]),"mission reset clears choices");
    VirtualFree(image,0,MEM_RELEASE);std::printf("payload runtime: %d checks, %d failures\n",checks,fail);return fail ? 1 : 0;
}
