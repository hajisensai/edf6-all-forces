// Native mission count/progress and production hooks on a private EDF.dll mapping. Never starts the game.
#include "../src/crew.h"
#include "../src/memory.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
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
bool Near(float a,float b){return std::fabs(a-b)<0.00001f;}
// The native list accessor follows list+F0 -> index vector and parsed node -> child count.
struct Fixture {
    alignas(16) unsigned char manager[0x240]{},document[0x40]{},node[0x30]{},table[0x60]{};
    unsigned index=0;
    Fixture(int rows,unsigned current) {
        using crew::Put;
        Put<void*>(manager,0x220,document);Put<void*>(document,8,&index);
        Put<std::uint64_t>(document,0x18,1);Put<void*>(document,0x28,node);Put<void*>(node,0,table);
        Set(rows,current);
    }
    void Set(int rows,unsigned current){crew::Put<int>(table,0x58,rows);crew::Put<unsigned>(manager,0x48,current);}
    std::uintptr_t Mgr(){return reinterpret_cast<std::uintptr_t>(manager);}
    std::uintptr_t List(){return Mgr()+0x130;}
};
template<class Fn> Fn Patched(unsigned site) {
    const auto rel=crew::At<std::int32_t>(crew::image+site,1);
    return reinterpret_cast<Fn>(crew::image+site+5+rel);
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
    Fixture f(282,73);
    const auto nativeRows=reinterpret_cast<RowsFn>(image+kRows);
    const auto nativeProgress=reinterpret_cast<ProgressFn>(image+kProgress);
    Check(nativeRows(f.List())==282 && Near(nativeProgress(f.Mgr()),73.0f/281.0f),"actual native count/progress baseline");
    config.edf5CampaignRows=147;
    failAt=argc>2 ? std::atoi(argv[2]) : 0;
    if(failAt) {
        Check(!InstallEdf5Campaign() && !campaignReady,"failed call leaves campaign inactive");
        for(const auto& g:kGroups)for(std::size_t i=0;i<g.count;++i) {
            if(g.target==kRows)Check(Patched<RowsFn>(g.sites[i])(f.List())==282,"partial row hook keeps native result");
            else Check(Near(Patched<ProgressFn>(g.sites[i])(f.Mgr()),73.0f/281.0f),"partial progress hook keeps native result");
        }
    } else {
        Check(InstallEdf5Campaign() && InstallEdf5Campaign(),"all calls installed, re-entry is idempotent");
        for(unsigned site:kRowSites)Check(Patched<RowsFn>(site)(f.List())==147,"every patched row caller caps story");
        for(unsigned site:kProgressSites)Check(Near(Patched<ProgressFn>(site)(f.Mgr()),0.5f),"every progress caller keeps EDF6 scale");
        Check(nativeRows(f.List())==282,"unpatched selection/save accessor sees campaign rows");
        f.Set(282,147);Check(Near(ProgressHook(f.Mgr()),0),"first campaign row starts at zero");
        f.Set(282,281);Check(Near(ProgressHook(f.Mgr()),1),"last campaign row reaches one");
        f.Set(147,73);Check(RowsHook(f.List())==147 && Near(ProgressHook(f.Mgr()),nativeProgress(f.Mgr())),"online stock list unchanged");
        f.Set(40,20);Check(RowsHook(f.List())==40 && Near(ProgressHook(f.Mgr()),nativeProgress(f.Mgr())),"DLC mode unchanged");
        f.Set(1,0);Check(Near(ProgressHook(f.Mgr()),0),"single-row list stays native");
        f.Set(0,0);Check(Near(ProgressHook(f.Mgr()),0),"empty list stays native");
        f.Set(282,73);config.edf5CampaignRows=0;
        Check(RowsHook(f.List())==282 && Near(ProgressHook(f.Mgr()),nativeProgress(f.Mgr())),"no installed campaign leaves behavior native");
    }
    std::printf("edf5_campaign_native_test: %d checks passed (injected failure %d)\n",checks,failAt);
}
