// The mission packs' ownership hook on a private EDF.dll mapping: the native owned-content lookup (0xD92B0) run
// on a fixture of the owned set, and every patched call site. Never starts the game.
#include "../src/crew.h"
#include "../src/memory.h"
#include <cstdio>
#include <cstdlib>
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
    failAt=argc>2 ? std::atoi(argv[2]) : 0;
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
            Check(!owned(f.Mgr(),6),"no content past the packs");
        }
        Check(!native(f.Mgr(),3),"the native lookup itself unchanged");
        config.edf5CampaignContent=0;
        Check(!Patched(kSites[0])(f.Mgr(),3) && Patched(kSites[0])(f.Mgr(),0),"no packs installed: native answer");
    }
    std::printf("edf5_campaign_native_test: %d checks passed (injected failure %d)\n",checks,failAt);
}
