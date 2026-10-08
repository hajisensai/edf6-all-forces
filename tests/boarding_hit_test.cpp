// Execute the production damage hook against synthetic game memory; no EDF.dll or running game is needed.
#include "../src/boarding.cpp"
#include <cstdio>
#include <cstdlib>

namespace crew {
unsigned char* image=nullptr;
Config config{};
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
ULONGLONG GameMs() noexcept { return 3600000; }
unsigned char* PlayerHuman() noexcept { return nullptr; }
int VehicleClassOf(const void* object) noexcept { return At<std::uintptr_t>(object,0)==1 ? 0 : -1; }
bool testJet=false,testSub=false,testFlightSupported=false;
bool IsJet(const void*) noexcept { return testJet; }
bool PlayerJetBoardingSupported(const void*) noexcept { return testFlightSupported; }
bool IsSub(const void*) noexcept { return testSub; }
bool HumanOnFoot(const unsigned char*) noexcept { return true; }
bool BoardButtonReady() noexcept { return false; }
void PressBoardButtonBumping(unsigned char*) noexcept {}
bool SeatPoint(const unsigned char*,unsigned,float*,float*) noexcept { return false; }
void SetObjectTeam(unsigned char*,std::int32_t) noexcept {}
}

namespace {
int damageCalls=0,checks=0;
void* damageSeen=nullptr;
const void* targetSeen=nullptr;
void* infoSeen=nullptr;
void __fastcall StockDamage(void* damage,const void* target,void* info) {
    ++damageCalls;damageSeen=damage;targetSeen=target;infoSeen=info;
}
void Check(bool condition,const char* description) {
    ++checks;
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",description);std::exit(1);}
}
}

int main() {
    using namespace crew;
    alignas(16) unsigned char human[0x1600]{},otherHuman[0x1600]{},core[0xC00]{},vehicle[0x700]{},other[0x700]{};
    float contact[4]={20,0,0,1};
    Put<std::uintptr_t>(vehicle,0,1);Put<std::uintptr_t>(other,0,1);
    Put<const void*>(vehicle,kSelfCtrl,vehicle);Put<const void*>(other,kSelfCtrl,other);
    Put<std::int32_t>(vehicle,kTeam,kTeamFriend);Put<std::int32_t>(other,kTeam,kTeamVehicle);
    Put<const void*>(core,kBulletOwner,human);Put<std::uint32_t>(core,kColorAlpha,kTagBits);
    Put<const float*>(core,kHitRecords,contact);
    const void* target[2]={vehicle,vehicle};
    void* const damage=core+kDamageBlock;
    void* const info=core+0x730;
    ready=true;config.debug=false;shooter.store(human);nextHitDamage=&StockDamage;

    // A candidate by itself has no path into this hook (the source integration guard checks AddBodyHook too).
    Check(ask.vehicle==nullptr,"no hit: no boarding request");
    HitDamageHook(damage,target,info);
    Check(damageCalls==0 && ask.vehicle==vehicle,"real friendly hit requests boarding and suppresses damage");
    Check(ask.dist2==400,"pending hit is ranked by actual contact");

    // The second vehicle's centre is closer, but its actual contact is farther: keep the first real contact.
    Put<float>(vehicle,kPosition,100);Put<float>(other,kPosition,1);
    target[0]=other;contact[0]=30;HitDamageHook(damage,target,info);
    Check(ask.vehicle==vehicle,"a closer vehicle centre must not replace the nearer actual contact");
    contact[0]=10;HitDamageHook(damage,target,info);
    Check(ask.vehicle==other && ask.dist2==100,"nearer real contact replaces the pending hit");

    ResetBoarding();shooter.store(human);target[0]=vehicle;
    Put<std::int32_t>(vehicle,kTeam,1);HitDamageHook(damage,target,info);
    Check(damageCalls==1 && !ask.vehicle,"enemy hit keeps stock damage");
    Check(damageSeen==damage && targetSeen==target && infoSeen==info,"all three native arguments forwarded unchanged");
    Put<std::int32_t>(vehicle,kTeam,kTeamFriend);
    Put<const void*>(core,kBulletOwner,otherHuman);HitDamageHook(damage,target,info);
    Check(damageCalls==2 && !ask.vehicle,"another player's bullet keeps stock damage");
    Put<const void*>(core,kBulletOwner,human);Put<std::uint32_t>(core,kColorAlpha,0x3F800000u);
    HitDamageHook(damage,target,info);
    Check(damageCalls==3 && !ask.vehicle,"ordinary bullet keeps stock damage");
    Put<std::uint32_t>(core,kColorAlpha,kTagBits);config.boardingGun=false;
    HitDamageHook(damage,target,info);
    Check(damageCalls==4 && !ask.vehicle,"disabled boarding keeps stock damage");
    config.boardingGun=true;ready=false;HitDamageHook(damage,target,info);
    Check(damageCalls==5 && !ask.vehicle,"failed installation keeps stock damage");
    ready=true;target[0]=human;HitDamageHook(damage,target,info);
    Check(damageCalls==6 && !ask.vehicle,"friendly non-vehicle keeps stock damage");
    target[0]=vehicle;Put<const float*>(core,kHitRecords,nullptr);HitDamageHook(damage,target,info);
    Check(damageCalls==7 && !ask.vehicle,"missing hit record keeps stock damage");
    Put<const float*>(core,kHitRecords,contact);config.enabled=false;HitDamageHook(damage,target,info);
    Check(damageCalls==8 && !ask.vehicle,"disabled plugin keeps stock damage");
    config.enabled=true;
    for(std::uint32_t tag=kTagBits;tag<=kTagBits+3u;++tag) {
        ResetBoarding();shooter.store(human);Put<std::uint32_t>(core,kColorAlpha,tag);
        HitDamageHook(damage,target,info);
        Check(ask.vehicle==vehicle,"each class's reserved tag requests boarding");
    }
    ResetBoarding();shooter.store(human);Put<std::uint32_t>(core,kColorAlpha,kTagBits+4u);
    HitDamageHook(damage,target,info);
    Check(!ask.vehicle,"the next unused color tag remains an ordinary round");
    alignas(16) unsigned char seat[kSeatStride]{};
    Put<const void*>(vehicle,kSeats,seat);Put<std::uint64_t>(vehicle,kSeatCount,1);
    Put<std::int32_t>(vehicle,kTeam,kTeamVehicle);
    testJet=true;testFlightSupported=true;
    StartBoarding(human,vehicle,GameMs());
    Check(boarding.ref.obj==vehicle,"boarding gun accepts a supported NPC aircraft after a real hit");
    ResetBoarding();testFlightSupported=false;
    StartBoarding(human,vehicle,GameMs());
    Check(!boarding.ref,"aircraft without player flight controls remains refused");
    testFlightSupported=true;testSub=true;
    StartBoarding(human,vehicle,GameMs());
    Check(!boarding.ref,"submarine carrier without player controls remains refused");
    testSub=false;Put<std::int32_t>(vehicle,kTeam,1);
    StartBoarding(human,vehicle,GameMs());
    Check(!boarding.ref,"enemy aircraft remains refused even with player flight controls");
    std::printf("boarding hit: %d checks passed\n",checks);
    return 0;
}
