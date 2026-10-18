// The mission packs' ownership hook on a private EDF.dll mapping: the native owned-content lookup (0xD92B0) run
// on a fixture of the owned set, and every patched call site. Never starts the game.
// Mode "unlock": PortedMissionsUnlocked's hook (InstallPortedMissions): its signatures match this EDF.dll, its call
// site is redirected through the stub, and a stand-in caller with the mission select's frame (the rows' vector at
// [rsp+0x48]) gets every row opened only in a ported story pack with the key on, and a pack's placeholder rows
// (EDF6VehicleCrew.packs.ini Missing) locked and never cleared, the key on or off.
#include "../src/crew.h"
#include "../src/memory.h"
#include <algorithm>
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
// The installer's EDF6VehicleCrew.packs.ini as the plugin loads it (plugin.cpp LoadMissionPacks): the runs the ini
// names (EDF5 65..67, the range 68, EDF4.1 69..71), each pack with `rows` rows, the `mask` rows placeholders.
void InstallPacks(bool edf5,bool range,bool edf41,int rows=5,const char* mask="00") {
    config.edf5CampaignContent=edf5 ? packs::kEdf5 : 0;
    config.testRangeContent=range ? packs::kRange : 0;
    config.edf41Content=edf41 ? packs::kEdf41 : 0;
    config.missionPacks=packs::Table{};
    const char* tags[packs::kCount]={"EDF5","EDF5DLC1","EDF5DLC2","EDF6TR","EDF41","EDF41MP01","EDF41MP02"};
    for(int id=packs::kBase;id<packs::kBase+packs::kCount;++id) {
        const bool on=id<packs::kRange ? edf5 : id==packs::kRange ? range : edf41;
        if(on && !packs::Make(id,tags[id-packs::kBase],"0123456789abcdef",rows,mask,config.missionPacks.packs[id-packs::kBase]))
            std::exit(2);
    }
}
int packNotices=0;
std::wstring packNotice;
void RecordPackNotice(const wchar_t* text) noexcept { ++packNotices;packNotice=text; }
std::string lastLog,logs;
void Log(const char* format,...) noexcept {
    char text[1024]{};va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);lastLog=text;
    logs+=text;logs+='\n';
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
bool clearedAnswer=false;
bool __fastcall FakeCleared(unsigned row) { ++clearedCalls;clearedRow=row;return clearedAnswer; }
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
std::vector<int> Run(CallerFn caller,std::vector<crew::ported::ListRow> rows,unsigned row=0,bool* answer=nullptr) {
    void* vec[3]={rows.data(),rows.data()+rows.size(),rows.data()+rows.size()};
    const auto target=crew::CallTarget(crew::kListScan);
    clearedRow=~0u;
    const bool cleared=caller(vec,target,row);
    if(answer)*answer=cleared;
    else Check(!cleared,"the prior's answer comes back");
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
    InstallPacks(true,true,true);
    ModeFixture e5(66),e41(71),edf6(0),range(68),dlc(1);
    Put<void*>(image,kGameStatus,e5.gs);
    config.portedMissionsUnlocked=false;
    Check(Run(caller,FreshRows(5))==stock,"key off: the stock rows");
    config.portedMissionsUnlocked=true;
    Check(Run(caller,FreshRows(5))==std::vector<int>({1,1,1,2,1}),"EDF5 DLC1: every row open, the branch row 2");
    Check(lastLog.find("content 66, 4 rows opened")!=std::string::npos,"logged once");
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
    Check(Run(caller,FreshRows(5))==stock,"EDF5 not installed: its id is nobody's pack");
    Put<void*>(image,kGameStatus,nullptr);
    config.edf5CampaignContent=packs::kEdf5;
    Check(Run(caller,FreshRows(5))==stock,"no game status: stock");
    // Placeholder rows (a mission this machine lacks, kept at its row): shown locked (3), whatever the key, so the
    // select refuses them (0x8A8629) and the scan does not stop there (0x8A2330 stops at 4 only).
    InstallPacks(true,true,true,5,"41");   // rows 2 and 4
    Put<void*>(image,kGameStatus,e5.gs);
    config.portedMissionsUnlocked=false;
    Check(Run(caller,FreshRows(5))==std::vector<int>({1,4,3,4,3}),"key off: the placeholders locked, the rest stock");
    Check(lastLog.find("PACK placeholders: content 66 (EDF5DLC1), 2 of 2 rows locked")!=std::string::npos,"logged");
    config.portedMissionsUnlocked=true;
    Check(Run(caller,FreshRows(5))==std::vector<int>({1,1,3,2,3}),"key on: every row open but the placeholders");
    Put<void*>(image,kGameStatus,range.gs);
    Check(Run(caller,FreshRows(5))==std::vector<int>({1,4,3,4,3}),"the test range's placeholders too (no unlock there)");
    // A placeholder cleared before its resources went missing is not cleared now (it would be shown selectable);
    // any other row keeps the prior's answer.
    Put<void*>(image,kGameStatus,e5.gs);
    clearedAnswer=true;
    bool answer=false;
    Run(caller,FreshRows(5),0,&answer);
    Check(answer,"row 0 (the scan's start, locking the pack): cleared as the save says");
    Run(caller,FreshRows(5),2,&answer);
    Check(!answer,"a placeholder never counts as cleared");
    Run(caller,FreshRows(5),3,&answer);
    Check(answer,"the row after it does");
    Put<void*>(image,kGameStatus,edf6.gs);
    Run(caller,FreshRows(5),0,&answer);
    Run(caller,FreshRows(5),2,&answer);
    Check(answer,"another mode's scan: its row 2 as the save says");
    clearedAnswer=false;
    Put<void*>(image,kGameStatus,nullptr);
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
    Check(native(f.Mgr(),0) && !native(f.Mgr(),1) && !native(f.Mgr(),65),"native lookup: the story owned, nothing else");
    InstallPacks(true,false,false);
    showPackProblem=&RecordPackNotice;
    const std::string mode=argc>2 ? argv[2] : "";
    if(mode=="chain") {   // a site another plugin redirected first: chained, both answer
        bool changed=false;
        Check(edf::RedirectCall(image+kSites[2],image+kOwned,reinterpret_cast<void*>(&OtherPluginHook),changed),
              "the other plugin's redirect");
        Check(InstallEdf5Campaign() && campaignReady,"installed over another plugin's redirect");
        for(unsigned site:kSites)
            Check(Patched(site)(f.Mgr(),0) && Patched(site)(f.Mgr(),65) && !Patched(site)(f.Mgr(),1),"every site answers");
        const int before=otherCalls;
        Check(Patched(kSites[2])(f.Mgr(),65) && Patched(kSites[2])(f.Mgr(),1)==false,"the chained site owns the packs");
        Check(otherCalls==before+2,"the other plugin still sees every call of its site");
        Check(lastLog.find("id=1 prior=0 mod=0 allowed=0")!=std::string::npos,"join log records the final refusal");
        Patched(kSites[0])(f.Mgr(),65);
        Check(otherCalls==before+2,"and only of its own site");
        Patched(kSites[2])(f.Mgr(),67);
        Check(lastLog.find("id=67 prior=0 mod=1 allowed=1")!=std::string::npos,"join log includes MOD override after the chained native refusal");
        Check(lastLog.find("hooks_ready=1 EDF5=65..67")!=std::string::npos && lastLog.find("packs=65,66,67")!=std::string::npos,
              "join log includes the actual installed pack ids");
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
            Check(Patched(site)(f.Mgr(),0) && !Patched(site)(f.Mgr(),65),"partial install keeps the native answer");
    } else {
        Check(InstallEdf5Campaign() && InstallEdf5Campaign(),"all calls installed, re-entry is idempotent");
        for(unsigned site:kSites) {
            const auto owned=Patched(site);
            Check(owned(f.Mgr(),0),"the story stays owned");
            Check(!owned(f.Mgr(),1) && !owned(f.Mgr(),2),"the DLCs stay as the platform says");
            Check(owned(f.Mgr(),65) && owned(f.Mgr(),66) && owned(f.Mgr(),67),"all three packs owned");
            Check(!owned(f.Mgr(),68) && !owned(f.Mgr(),69) && !owned(f.Mgr(),3),"no content past the packs, nor the old ids");
        }
        // The test range pack: its one id owned beside the EDF5 packs, nothing between them.
        InstallPacks(true,true,false);
        for(unsigned site:kSites) {
            const auto owned=Patched(site);
            Check(owned(f.Mgr(),68),"the test range owned");
            Check(!owned(f.Mgr(),64) && !owned(f.Mgr(),69),"only the test range's own id");
            Check(owned(f.Mgr(),65) && owned(f.Mgr(),67) && owned(f.Mgr(),0) && !owned(f.Mgr(),1),"the rest unchanged");
        }
        Check(!native(f.Mgr(),68),"the native lookup does not own the test range");
        InstallPacks(true,false,true);
        Check(!Patched(kSites[0])(f.Mgr(),68) && Patched(kSites[0])(f.Mgr(),65),"no test range installed: nothing extra");
        Check(Patched(kSites[0])(f.Mgr(),69) && Patched(kSites[0])(f.Mgr(),71),"the 4.1 packs: their run");
        Check(!native(f.Mgr(),65),"the native lookup itself unchanged");
        // A run the ini names but EDF6VehicleCrew.packs.ini does not list (4.1's Mission Pack 2 not built): not owned.
        config.missionPacks.packs[71-packs::kBase]=packs::Pack{};
        Check(Patched(kSites[0])(f.Mgr(),70) && !Patched(kSites[0])(f.Mgr(),71),"only the packs the installer listed");
        // A pack listed, its ini run 0 (an older installer's id taken as 0, plugin.cpp PackContent): not owned.
        InstallPacks(true,false,true);
        config.edf41Content=0;
        Check(!Patched(kSites[0])(f.Mgr(),69) && Patched(kSites[0])(f.Mgr(),65),"a run the ini does not name is not owned");
        // A pack with placeholder rows: owned only while the mission select's hook keeps them locked.
        InstallPacks(true,false,false,5,"01");
        Check(!missionsReady && !Patched(kSites[0])(f.Mgr(),65),"placeholders without the select's hook: not owned");
        missionsReady=true;
        Check(Patched(kSites[0])(f.Mgr(),65),"...owned with it");
        missionsReady=false;
        // A pack whose older saves have other rows (Legacy): owned only while the save-load hook moves them.
        InstallPacks(true,false,false);
        Check(packs::SetLegacy(config.missionPacks.packs[0],"4:0,1,3,4"),"Legacy on EDF5's story");
        Check(!Patched(kSites[0])(f.Mgr(),65) && Patched(kSites[0])(f.Mgr(),66),"Legacy rows without the save hook: not owned");
        SetMissionPackSaveHook(true);
        Check(Patched(kSites[0])(f.Mgr(),65),"...owned with it");
        // The hook's work on the game status of a loaded save (MigrateMissionPackSave): the current mode's content
        // read as 0x8A20F3 reads it, its save at GS+0x57A4 moved once, other modes' saves untouched.
        {
            std::vector<unsigned char> gs(packs::kModeSave+packs::kModeSaveSize+0x100);
            alignas(16) unsigned char modeNode[0x20]{},node[0x100]{};
            void* modes[1]={modeNode};
            Put<void*>(gs.data(),0x20,modes);Put<std::uint32_t>(gs.data(),0x30,1);Put<std::uint32_t>(gs.data(),0x38,0);
            Put<void*>(modeNode,0x10,node);Put<std::int32_t>(node,8,0x10);Put<std::int32_t>(node,0x10+0x74,65);
            unsigned char* const save=gs.data()+packs::kModeSave;
            save[packs::kClear+2]=1;save[0]=2;   // old row 2 (now row 3) cleared, current
            MigrateMissionPackSave(gs.data());
            Check(save[packs::kClear+3]==1 && save[packs::kClear+2]==0 && save[0]==3 && lastLog.find("rows-moved")!=std::string::npos,
                  "the loaded save of the pack moved and logged");
            MigrateMissionPackSave(gs.data());
            Check(save[packs::kClear+3]==1 && save[0]==3,"a second load moves nothing");
            Put<std::int32_t>(node,0x10+0x74,0);
            std::memset(save+packs::kMarker,0,4);
            const std::vector<unsigned char> before(save,save+packs::kModeSaveSize);
            MigrateMissionPackSave(gs.data());
            Check(std::equal(before.begin(),before.end(),save),"EDF6's own story's save: untouched");
        }
        SetMissionPackSaveHook(false);
        InstallPacks(true,false,false);
        Patched(kSites[2])(f.Mgr(),0);
        Check(lastLog.find("id=0 prior=1 mod=0 allowed=1")!=std::string::npos,"native story admission logged without a MOD grant");
        // The lobby's join refused for one of our ids: a dialog says which pack; stock and other ids get none.
        const int notes=packNotices;
        Check(!Patched(kSites[2])(f.Mgr(),70),"a pack not installed here is refused");
        Check(packNotices==notes+1 && packNotice.find(L"EDF4.1 任务包１")!=std::wstring::npos
              && packNotice.find(L"本机没有安装")!=std::wstring::npos,"the guest is told which pack it lacks");
        Check(lastLog.find("reason=pack-not-installed content=70")!=std::string::npos,"...and it is logged");
        Patched(kSites[2])(f.Mgr(),1);Patched(kSites[2])(f.Mgr(),7);Patched(kSites[0])(f.Mgr(),70);
        Check(packNotices==notes+1,"a stock DLC, another mod's id, the dialog's list: no dialog");
        config.edf5CampaignContent=0;
        Check(!Patched(kSites[0])(f.Mgr(),65) && Patched(kSites[0])(f.Mgr(),0),"no packs installed: native answer");
        Check(!Patched(kSites[2])(f.Mgr(),67) && logs.find("allowed=0 hooks_ready=1 EDF5=0..0 range=0 EDF41=0..0 packs=none")!=std::string::npos
              && lastLog.find("reason=pack-not-installed content=67")!=std::string::npos,
              "missing pack configuration is visible in the final join log");
        InstallPacks(false,true,false);
        Check(Patched(kSites[0])(f.Mgr(),68) && !Patched(kSites[0])(f.Mgr(),65),"the test range alone, without EDF5's packs");
        InstallPacks(false,false,false);
    }
    std::printf("edf5_campaign_native_test: %d checks passed (injected failure %d)\n",checks,failAt);
}
