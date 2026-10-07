// Execute the production call hook and installer against a private code image; no game required.
#include "../src/npc_gunner_aim.cpp"
#include <cstdio>
#include <cstdlib>

namespace crew {
unsigned char* image=nullptr;
Config config{};
bool session=true;
const Config& Cfg() noexcept { return config; }
bool InSession() noexcept { return session; }
void Log(const char*,...) noexcept {}
}
namespace {
int checks=0,seenMode=-1,seenCalls=0;
void* seenVehicle=nullptr;
void Check(bool ok,const char* what) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
void __fastcall Record(void* v,int mode) { seenVehicle=v;seenMode=mode;++seenCalls; }
}
int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"private executable image allocated");
    Check(!InstallNpcGunnerAim() && !NpcGunnerAimReady(),"unsupported profile fails closed");
    std::memcpy(image+kHeliAimCall-8,kCallContext,sizeof(kCallContext));
    std::memcpy(image+kSeatAimMode,kAimSignature,sizeof(kAimSignature));
    Check(InstallNpcGunnerAim() && NpcGunnerAimReady(),"known call patched");
    Check(InstallNpcGunnerAim(),"installation is idempotent");
    Check(std::memcmp(image+kHeliAimCall-8,kCallContext,8)==0 &&
          std::memcmp(image+kHeliAimCall+5,kCallContext+13,sizeof(kCallContext)-13)==0,"only rel32 call changed");
    nextAim=&Record;
    unsigned char vehicle[16]{};
    Put<const void*>(vehicle,0,image+k410Vtable);
    // Enter the actual patched call through its allocated near thunk, not a copy of the policy.
    const auto displacement=At<std::int32_t>(image+kHeliAimCall,1);
    const auto hooked=reinterpret_cast<AimModeFn>(image+kHeliAimCall+5+displacement);
    hooked(vehicle,1);
    Check(seenMode==0 && seenVehicle==vehicle && seenCalls==1,"online 410 uses stock empty-seat remote policy");
    config.enabled=false;hooked(vehicle,1);
    Check(seenMode==1,"disabled plugin preserves stock mode");
    config.enabled=true;config.npcGunners=false;hooked(vehicle,1);
    Check(seenMode==1,"disabled NPC gunners preserve stock mode");
    config.npcGunners=true;session=false;hooked(vehicle,1);
    Check(seenMode==1,"singleplayer preserves stock mode");
    session=true;Put<const void*>(vehicle,0,image+0x17DB238);hooked(vehicle,1);
    Check(seenMode==1,"506 and other helicopter classes stay unchanged");
    Put<const void*>(vehicle,0,image+k410Vtable);hooked(vehicle,0);
    Check(seenMode==0,"existing mode zero remains zero");
    hooked(vehicle,2);Check(seenMode==2,"unknown modes remain unchanged");
    hooked(nullptr,1);Check(seenMode==1 && seenVehicle==nullptr,"unreadable vehicle forwarded unchanged");
    aimReady=false;hooked(vehicle,1);
    Check(seenMode==1,"partial installation remains stock while readiness is false");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("npc_gunner_aim_test: %d checks passed\n",checks);
}
