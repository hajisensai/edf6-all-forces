// The mission packs' ownership hook on a private EDF.dll mapping: the native owned-content lookup (0xD92B0) run
// on a fixture of the owned set, and every patched call site. Never starts the game.
// Mode "unlock": PortedMissionsUnlocked's hook (InstallPortedMissions): its signatures match this EDF.dll, its call
// site is redirected through the stub, and a stand-in caller with the mission select's frame (the rows' vector at
// [rsp+0x48]) gets every row opened only in a ported story pack with the key on.
#include "../src/crew.h"
#include "../src/memory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cstdarg>
#include <vector>
namespace crew {
int redirectCount=0,failAt=0;
bool CampaignTestRedirect(unsigned char* at,void* expected,void* replacement,bool& changed) noexcept {
    if(++redirectCount==failAt){changed=false;return false;}
    return edf::RedirectCall(at,expected,replacement,changed);
}
}
#define RedirectCall CampaignTestRedirect
#include "../src/edf5campaign.cpp"
#undef RedirectCall
namespace crew {
unsigned char* image=nullptr;
Config config{};
const Config& Cfg() noexcept { return config; }
std::string lastLog;
void Log(const char* format,...) noexcept {
    char text[1024]{};va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);lastLog=text;
}
}
namespace {
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
// mgr+0xE0: the owned contents, an MSVC std::set<int>: head {+0 left, +8 root, +0x10 right, +0x19 isnil}, nodes
// {+0 left, +8 parent, +0x10 right, +0x19 isnil 0, +0x1C key}. What 0xDBB50 leaves without add-ons: {0}.
struct Fixture {
    alignas(16) unsigned char manager[0x100]{},head[0x20]{},zero[0x20]{};
    Fixture() {
        using crew::Put;
        Put<void*>(manager,0xE0,head);
        Put<void*>(head,0,zero);Put<void*>(head,8,zero);Put<void*>(head,0x10,zero);head[0x19]=1;
        Put<void*>(zero,0,head);Put<void*>(zero,8,head);Put<void*>(zero,0x10,head);zero[0x19]=0;Put<int>(zero,0x1C,0);
    }
    std::uintptr_t Mgr(){return reinterpret_cast<std::uintptr_t>(manager);}
};
crew::OwnedFn Patched(unsigned site) {
    const auto rel=crew::At<std::int32_t>(crew::image+site,1);
    return reinterpret_cast<crew::OwnedFn>(crew::image+site+5+rel);
}
// Another plugin's redirect of a site, made before ours (EDF6Coop's lobby join log of 0x8EC659): counts its calls and
// answers natively.
int otherCalls=0;
bool __fastcall OtherPluginHook(std::uintptr_t mgr,int id) {
    ++otherCalls;
    return reinterpret_cast<crew::OwnedFn>(crew::image+crew::kOwned)(mgr,id);
}
// A site's rel32 pointed at `target` by hand (a different call inside EDF.dll: another build).
void PointAt(unsigned site,const unsigned char* target) {
    unsigned char* at=crew::image+site;
    const auto rel=static_cast<std::int32_t>(target-(at+5));
    DWORD old=0;
    VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&old);
    std::memcpy(at+1,&rel,4);
    VirtualProtect(at,5,old,&old);
}

// ---- mode "unlock" ----
// The prior of the mission select's clear check (a redirect made before ours): records the row, answers "not cleared".
int clearedCalls=0;
unsigned clearedRow=~0u;
bool __fastcall FakeCleared(unsigned row) { ++clearedCalls;clearedRow=row;return false; }
int __fastcall FakeField5(void*,unsigned row) { return row==3 ? 1 : 0; }
// A stand-in for the mission select at its call: rcx = {begin, end, capacity} copied to [rsp+0x48], rdx = the call's
// target, r8d = the row in ecx. sub rsp,0x68; ...; mov ecx,r8d; call rdx; add rsp,0x68; ret
using CallerFn=bool(__fastcall*)(void* vec,void* target,unsigned row);
CallerFn MakeCaller() {
    static const unsigned char code[]={0x48,0x83,0xEC,0x68, 0x48,0x8B,0x01, 0x48,0x89,0x44,0x24,0x48, 0x48,0x8B,0x41,0x08,
        0x48,0x89,0x44,0x24,0x50, 0x48,0x8B,0x41,0x10, 0x48,0x89,0x44,0x24,0x58, 0x44,0x89,0xC1, 0xFF,0xD2,
        0x48,0x83,0xC4,0x68, 0xC3};
    void* const at=VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    if(at)std::memcpy(at,code,sizeof(code));
    return reinterpret_cast<CallerFn>(at);
}
// The game status the hook reads the current mode from: modes at +0x20, count +0x30, current +0x38; a mode's node at
// +0x10, the node's values at node + [node+8], the content id at +0x74.
struct ModeFixture {
    alignas(16) unsigned char gs[0x200]{},mode[0x20]{},node[0x100]{};
    void* modes[1]{};
    explicit ModeFixture(int content) {
        using crew::Put;
        modes[0]=mode;
        Put<void*>(gs,0x20,modes);Put<std::uint32_t>(gs,0x30,1);Put<std::uint32_t>(gs,0x38,0);
        Put<void*>(mode,0x10,node);Put<std::int32_t>(node,8,0x10);Put<std::int32_t>(node,0x10+0x74,content);
    }
};
std::vector<crew::ported::ListRow> FreshRows(std::size_t n) {
    std::vector<crew::ported::ListRow> rows(n);
    for(std::size_t i=0;i<n;++i){rows[i]=crew::ported::ListRow{};rows[i].row=static_cast<std::int32_t>(i);rows[i].state=4;}
    rows[0].state=1;
    return rows;
}
// The rows after one call of the site's target through the stand-in caller (row 0 unless given).
std::vector<int> Run(CallerFn caller,std::vector<crew::ported::ListRow> rows,unsigned row=0) {
    void* vec[3]={rows.data(),rows.data()+rows.size(),rows.data()+rows.size()};
    const auto target=crew::CallTarget(crew::kListScan);
    clearedRow=~0u;
    Check(!caller(vec,target,row),"the prior's answer comes back");
    Check(clearedRow==row,"the prior asked about the same row");
    std::vector<int> states;
    for(const auto& r:rows)states.push_back(r.state);
    return states;
}
int UnlockMode() {
    using namespace crew;
    unsigned char* const gsSlot=image+kGameStatus;
    DWORD old=0;
    Check(VirtualProtect(gsSlot,8,PAGE_READWRITE,&old)!=0,"the GS pointer writable in the private mapping");
    Check(CallTarget(kListScan)==image+kCleared,"0x8A21C3 is a call of 0x8A4C00");
    bool changed=false;
    Check(edf::RedirectCall(image+kListScan,image+kCleared,reinterpret_cast<void*>(&FakeCleared),changed),"stand-in prior");
    Check(InstallPortedMissions() && missionsReady,"the mission select's signatures match and its call is redirected");
    Check(InstallPortedMissions(),"re-entry is idempotent");
    Check(CallTarget(kListScan)!=reinterpret_cast<unsigned char*>(&FakeCleared),"the site goes through our stub now");
    Check(lastLog.find("chained")!=std::string::npos || lastLog.find("HOOK ported missions=1")!=std::string::npos,"installed");
    field5=&FakeField5;
    const CallerFn caller=MakeCaller();
    Check(caller!=nullptr,"stand-in caller");
    const std::vector<int> stock={1,4,4,4,4};
    config.edf5CampaignContent=3;config.testRangeContent=6;config.edf41Content=7;
    ModeFixture e5(4),e41(9),edf6(0),range(6),dlc(1);
    Put<void*>(image,kGameStatus,e5.gs);
    config.portedMissionsUnlocked=false;
    Check(Run(caller,FreshRows(5))==stock,"key off: the stock rows");
    config.portedMissionsUnlocked=true;
    Check(Run(caller,FreshRows(5))==std::vector<int>({1,1,1,2,1}),"EDF5 DLC1: every row open, the branch row 2");
    Check(lastLog.find("content 4, 4 rows opened")!=std::string::npos,"logged once");
    Check(Run(caller,FreshRows(5),1)==stock,"only at the scan's first row");
    Put<void*>(image,kGameStatus,e41.gs);
    Check(Run(caller,FreshRows(3))==std::vector<int>({1,1,1}),"EDF4.1 Mission Pack 2 opened");
    for(ModeFixture* other:{&edf6,&range,&dlc}) {
        Put<void*>(image,kGameStatus,other->gs);
        Check(Run(caller,FreshRows(5))==stock,"EDF6's story, the test range, EDF6's DLC: stock");
    }
    Put<void*>(image,kGameStatus,e5.gs);
    auto odd=FreshRows(4);
    odd[2].state=1;
    Check(Run(caller,odd)==std::vector<int>({1,4,1,4}),"a list not as the first loop leaves it: untouched");
    Check(lastLog.find("left stock")!=std::string::npos,"...and logged");
    config.edf5CampaignContent=0;
    Check(Run(caller,FreshRows(5))==stock,"EDF5 not installed: its old id is nobody's pack");
    Put<void*>(image,kGameStatus,nullptr);
    config.edf5CampaignContent=3;
    Check(Run(caller,FreshRows(5))==stock,"no game status: stock");
    std::printf("edf5_campaign_native_test: %d checks passed (ported missions unlock)\n",checks);
    return 0;
}
}
int main(int argc,char** argv) {
    using namespace crew;
    if(argc<2 || !argv[1][0] || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: pass the supported EDF.dll path (private mapping, no DllMain)");return 77;
    }
    image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    Check(image!=nullptr,"private DLL mapped without its entrypoint");
    const auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew);
    Check(nt->FileHeader.TimeDateStamp==0x678CCB46 && nt->OptionalHeader.SizeOfImage==0x22CE000,"supported game profile");
    Fixture f;
    const auto native=reinterpret_cast<OwnedFn>(image+kOwned);
    Check(native(f.Mgr(),0) && !native(f.Mgr(),1) && !native(f.Mgr(),3),"native lookup: the story owned, nothing else");
    config.edf5CampaignContent=3;
    const std::string mode=argc>2 ? argv[2] : "";
    if(mode=="chain") {   // a site another plugin redirected first: chained, both answer
        bool changed=false;
        Check(edf::RedirectCall(image+kSites[2],image+kOwned,reinterpret_cast<void*>(&OtherPluginHook),changed),
              "the other plugin's redirect");
        Check(InstallEdf5Campaign() && campaignReady,"installed over another plugin's redirect");
        for(unsigned site:kSites)
            Check(Patched(site)(f.Mgr(),0) && Patched(site)(f.Mgr(),3) && !Patched(site)(f.Mgr(),1),"every site answers");
        const int before=otherCalls;
        Check(Patched(kSites[2])(f.Mgr(),3) && Patched(kSites[2])(f.Mgr(),1)==false,"the chained site owns the packs");
        Check(otherCalls==before+2,"the other plugin still sees every call of its site");
        Check(lastLog.find("id=1 prior=0 mod=0 allowed=0")!=std::string::npos,"join log records the final refusal");
        Patched(kSites[0])(f.Mgr(),3);
        Check(otherCalls==before+2,"and only of its own site");
        Patched(kSites[2])(f.Mgr(),5);
        Check(lastLog.find("id=5 prior=0 mod=1 allowed=1")!=std::string::npos,"join log includes MOD override after the chained native refusal");
        Check(lastLog.find("hooks_ready=1 EDF5=3..5")!=std::string::npos,"join log includes the actual installed pack ids");
        std::printf("edf5_campaign_native_test: %d checks passed (chained)\n",checks);
        return 0;
    }
    if(mode=="unlock")return UnlockMode();
    if(mode=="foreign") {   // a site calling something else inside EDF.dll: another build, nothing installed
        PointAt(kSites[2],image+kOwned+0x10);
        Check(!InstallEdf5Campaign() && !campaignReady,"a different call inside EDF.dll is refused");
        Check(Patched(kSites[0])==reinterpret_cast<OwnedFn>(image+kOwned),"no site touched");
        std::printf("edf5_campaign_native_test: %d checks passed (foreign site)\n",checks);
        return 0;
    }
    failAt=std::atoi(mode.c_str());
    if(failAt) {
        Check(!InstallEdf5Campaign() && !campaignReady,"failed call leaves the campaign inactive");
        for(unsigned site:kSites)
            Check(Patched(site)(f.Mgr(),0) && !Patched(site)(f.Mgr(),3),"partial install keeps the native answer");
    } else {
        Check(InstallEdf5Campaign() && InstallEdf5Campaign(),"all calls installed, re-entry is idempotent");
        for(unsigned site:kSites) {
            const auto owned=Patched(site);
            Check(owned(f.Mgr(),0),"the story stays owned");
            Check(!owned(f.Mgr(),1) && !owned(f.Mgr(),2),"the DLCs stay as the platform says");
            Check(owned(f.Mgr(),3) && owned(f.Mgr(),4) && owned(f.Mgr(),5),"all three packs owned");
            Check(!owned(f.Mgr(),6) && !owned(f.Mgr(),7),"no content past the packs");
        }
        // The test range pack: its one id owned beside the EDF5 packs, nothing between them.
        config.testRangeContent=7;
        for(unsigned site:kSites) {
            const auto owned=Patched(site);
            Check(owned(f.Mgr(),7),"the test range owned");
            Check(!owned(f.Mgr(),6) && !owned(f.Mgr(),8),"only the test range's own id");
            Check(owned(f.Mgr(),3) && owned(f.Mgr(),5) && owned(f.Mgr(),0) && !owned(f.Mgr(),1),"the rest unchanged");
        }
        Check(!native(f.Mgr(),7),"the native lookup does not own the test range");
        config.testRangeContent=0;
        Check(!Patched(kSites[0])(f.Mgr(),7) && Patched(kSites[0])(f.Mgr(),3),"no test range installed: nothing extra");
        Check(!native(f.Mgr(),3),"the native lookup itself unchanged");
        Patched(kSites[2])(f.Mgr(),0);
        Check(lastLog.find("id=0 prior=1 mod=0 allowed=1")!=std::string::npos,"native story admission logged without a MOD grant");
        config.edf5CampaignContent=0;
        Check(!Patched(kSites[0])(f.Mgr(),3) && Patched(kSites[0])(f.Mgr(),0),"no packs installed: native answer");
        Check(!Patched(kSites[2])(f.Mgr(),5) && lastLog.find("id=5 prior=0 mod=0 allowed=0")!=std::string::npos,
              "missing pack configuration is visible in the final join log");
        config.testRangeContent=4;
        Check(Patched(kSites[0])(f.Mgr(),4) && !Patched(kSites[0])(f.Mgr(),3),"the test range alone, without EDF5's packs");
        config.testRangeContent=0;
    }
    std::printf("edf5_campaign_native_test: %d checks passed (injected failure %d)\n",checks,failAt);
}
