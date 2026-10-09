// The mission packs' ownership hook on a private EDF.dll mapping: the native owned-content lookup (0xD92B0) run
// on a fixture of the owned set, and every patched call site. Never starts the game.
#include "../src/crew.h"
#include "../src/memory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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
void Log(const char*,...) noexcept {}
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
        Patched(kSites[0])(f.Mgr(),3);
        Check(otherCalls==before+2,"and only of its own site");
        std::printf("edf5_campaign_native_test: %d checks passed (chained)\n",checks);
        return 0;
    }
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
        config.edf5CampaignContent=0;
        Check(!Patched(kSites[0])(f.Mgr(),3) && Patched(kSites[0])(f.Mgr(),0),"no packs installed: native answer");
        config.testRangeContent=4;
        Check(Patched(kSites[0])(f.Mgr(),4) && !Patched(kSites[0])(f.Mgr(),3),"the test range alone, without EDF5's packs");
        config.testRangeContent=0;
    }
    std::printf("edf5_campaign_native_test: %d checks passed (injected failure %d)\n",checks,failAt);
}
