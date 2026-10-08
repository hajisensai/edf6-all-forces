// Production hook on a private image, or real native script recursion with the
// final SetTeam/world service replaced by a recorder. Never loads DllMain.
#include "../src/mission_crew_team.cpp"
#include <cstdio>
#include <cstdlib>
namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
}
namespace {
int checks=0,callbacks=0,crewTeam=0,order[8]{},used=0;
void* missionVehicle=nullptr;
void* denied=nullptr;
void Check(bool ok,const char* why) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}
}
void Notify(void* object,int team) noexcept {
    ++callbacks;
    Check(crew::At<int>(object,0x314)==team,"callback runs after accepted original team change");
    if(object==missionVehicle)crewTeam=team;
}
bool __fastcall SetTeam(void* object,int team,bool registered) {
    Check(registered,"native recursive script assignment registers team");
    if(object==denied)return false;
    crew::Put<int>(object,0x314,team);
    order[used++]=crew::At<int>(object,0x310);
    return true;
}
crew::RecursiveTeamFn Patched(unsigned index) {
    return reinterpret_cast<crew::RecursiveTeamFn>(crew::image+crew::kTeamCalls[index]+5+
        crew::At<int>(crew::image+crew::kTeamCalls[index],1));
}
bool __fastcall FixtureRecursive(void* object,int team) {
    auto sentinel=crew::At<unsigned char*>(object,0x550);
    auto item=crew::At<unsigned char*>(sentinel,0);
    while(item!=sentinel) {
        Patched(1)(crew::At<void*>(item,0x10),team);
        item=crew::At<unsigned char*>(item,0);
    }
    return SetTeam(object,team,true);
}
void Jump(unsigned char* at,void* target) {
    DWORD old=0;Check(VirtualProtect(at,12,PAGE_EXECUTE_READWRITE,&old)!=0,"private service patch writable");
    at[0]=0x48;at[1]=0xB8;crew::Put<void*>(at,2,target);at[10]=0xFF;at[11]=0xE0;
    FlushInstructionCache(GetCurrentProcess(),at,12);
    Check(VirtualProtect(at,12,old,&old)!=0,"private service protection restored");
}
}
int wmain(int argc,wchar_t** argv) {
    using namespace crew;
    const bool native=argc>1;
    if(native && GetFileAttributesW(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: supplied EDF.dll does not exist");return 77;
    }
    if(native)image=reinterpret_cast<unsigned char*>(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    else image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x600000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"private code image");
    if(native) {
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
        Check(pe->FileHeader.TimeDateStamp==0x678CCB46 && pe->OptionalHeader.SizeOfImage==0x22CE000,
            "supported native EDF profile");
    }
    if(!native) {
        Check(!InstallMissionCrewTeam(&Notify),"unknown profile rejected");
        std::memcpy(image+kRecursiveTeam,kRecursiveTeamSig,sizeof(kRecursiveTeamSig));
        std::memcpy(image+kTeamCalls[0]-2,kRootContext,sizeof(kRootContext));
        std::memcpy(image+kTeamCalls[1]-6,kChildContext,sizeof(kChildContext));
    }
    Check(!InstallMissionCrewTeam(nullptr),"missing callback rejected");
    Check(InstallMissionCrewTeam(&Notify),"production hooks installed");
    Check(InstallMissionCrewTeam(&Notify),"same callback is idempotent");
    if(native)Jump(image+0x54EE70,reinterpret_cast<void*>(&SetTeam));
    else nativeTeam=&FixtureRecursive;
    unsigned char vehicle[0x560]{},human[0x560]{},group[0x560]{};
    unsigned char empty[0x20]{},sentinel[0x20]{},node[0x20]{};
    Put<void*>(empty,0,empty);Put<void*>(vehicle,0x550,empty);Put<void*>(human,0x550,empty);
    Put<void*>(group,0x550,sentinel);Put<void*>(sentinel,0,node);Put<void*>(node,0,sentinel);Put<void*>(node,0x10,vehicle);
    Put<int>(vehicle,0x310,1);Put<int>(human,0x310,2);Put<int>(group,0x310,3);
    missionVehicle=vehicle;
    Check(Patched(0)(vehicle,2) && crewTeam==2,"direct mission vehicle updates real crew callback");
    used=0;Check(Patched(0)(group,1) && crewTeam==1,"nested mission vehicle receives script team");
    Check(used==2 && order[0]==1 && order[1]==3,"native child before parent ordering preserved");
    crewTeam=9;Check(Patched(0)(human,4) && crewTeam==9,"nonmission human does not alter real crew");
    const int before=callbacks;denied=vehicle;
    Check(!Patched(0)(vehicle,8) && callbacks==before,"native rejected assignment does not notify crew");
    denied=nullptr;teamHooksReady=false;
    Check(Patched(0)(vehicle,7) && callbacks==before,"partial/disabled hooks still perform native team assignment");
    std::printf("mission_crew_team_test: %d checks passed (%s)\n",checks,native?"native recursive body, fixture final SetTeam":"fixture image");
}
