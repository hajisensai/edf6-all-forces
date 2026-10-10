// The route guide hook on a private EDF.dll mapping: every constructor call site redirected, and a guide made
// through each patched site corrected (the constructor itself stood in for: it needs the running game). Never starts
// the game.
#include "../src/crew.h"
#include "../src/memory.h"
namespace crew {
int redirectCount=0,failAt=0;
// RedirectCall with its `failAt`th call failing (no near page): the install's partial-failure branch.
bool RouteTestRedirect(unsigned char* at,void* expected,void* replacement,bool& changed) noexcept {
    if(++redirectCount==failAt){changed=false;return false;}
    return edf::RedirectCall(at,expected,replacement,changed);
}
}
#define RedirectCall RouteTestRedirect
#include "../src/route_guide.cpp"
#undef RedirectCall
#include <cstdio>
#include <cstdlib>
#include <string>

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
using namespace routeguide;
std::uint32_t madeMode=0;
bool madeTarget=false;
int made=0;
// The stock constructor's part the hook relies on: the InitParam's mode and object copied in, the stock drops, curve
// and smoothing (0x5C6C10, docs/route-guide-re.md §1).
void* __fastcall FakeCtor(void* guide,void*) {
    ++made;
    auto g=static_cast<unsigned char*>(guide);
    edf::Put<std::uint32_t>(g,kMode,madeMode);
    edf::Put<const void*>(g,kTarget,madeTarget ? g : nullptr);
    edf::Put<float>(g,kNearDrop,15.0f);
    edf::Put<float>(g,kMergeDrop,7.0f);
    edf::Put<bool>(g,kPolyline,false);
    edf::Put<float>(g,kSmoothing,0.04f);
    return guide;
}
using Fn=void*(__fastcall*)(void*,void*);
// The patched site's call target (our near thunk).
Fn Through(unsigned site) {
    const unsigned char* p=crew::image+site;
    std::int32_t rel;std::memcpy(&rel,p+1,4);
    return reinterpret_cast<Fn>(const_cast<unsigned char*>(p+5+rel));
}
alignas(16) unsigned char guide[0x2F0];
}  // namespace

int main(int argc,char** argv) {
    using namespace crew;
    if(argc<2 || !argv[1][0] || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: pass the supported EDF.dll path (private mapping, no DllMain)");return 77;
    }
    image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    Check(image!=nullptr,"private DLL mapped without its entrypoint");
    const auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew);
    Check(nt->FileHeader.TimeDateStamp==0x678CCB46 && nt->OptionalHeader.SizeOfImage==0x22CE000,"supported game profile");
    const std::string mode=argc>2 ? argv[2] : "";
    if(mode=="foreign") {   // a site calling something else: another build, nothing installed
        bool changed=false;
        Check(edf::RedirectCall(image+kSites[1],image+kCtor,image+kCtor+0x10,changed) && changed,"site moved off the ctor");
        Check(!InstallRouteGuide(),"a site not calling the constructor: nothing installed");
        Check(image[kSites[0]]==0xE8 && reinterpret_cast<unsigned char*>(Through(kSites[0]))==image+kCtor &&
              reinterpret_cast<unsigned char*>(Through(kSites[2]))==image+kCtor,"the other sites left calling the constructor");
        std::printf("route_guide_native_test: %d checks passed (foreign site)\n",checks);
        return 0;
    }
    config.routeGuidePath=true;
    if(mode=="partial") {   // the second redirect fails: the first site redirected, but every guide passed through as made
        failAt=2;
        Check(!InstallRouteGuide() && !ready,"a failed redirect: not ready");
        Check(reinterpret_cast<unsigned char*>(Through(kSites[0]))!=image+kCtor,"the first site was redirected");
        ctor=&FakeCtor;
        std::memset(guide,0,sizeof(guide));
        madeMode=kModeNone;madeTarget=true;
        Check(Through(kSites[0])(guide,nullptr)==guide && made==1,"the redirected site still makes the guide");
        Check(At<std::uint32_t>(guide,kMode)==kModeNone && !At<bool>(guide,kPolyline) && At<float>(guide,kNearDrop)==15.0f,
              "...as the game made it, like the sites left stock");
        std::printf("route_guide_native_test: %d checks passed (partial install)\n",checks);
        return 0;
    }
    Check(InstallRouteGuide(),"installed on every constructor call");
    for(const unsigned site:kSites)
        Check(image[site]==0xE8 && reinterpret_cast<unsigned char*>(Through(site))!=image+kCtor,"call redirected");
    ctor=&FakeCtor;
    struct Case { unsigned site; std::uint32_t mode; bool target; std::uint32_t want; const char* who; };
    const Case cases[]={
        {0x1A7EDC,kModeArea,false,kModeArea,"DispRouteGuideToArea"},
        {0x1B4528,kModeObject,true,kModeObject,"DispRouteGuideToObject"},
        {0x21F681,kModeNone,true,kModeObject,"the EDF5 scripts' BVM native 1200"},
    };
    for(const Case& c:cases) {
        std::memset(guide,0,sizeof(guide));
        madeMode=c.mode;madeTarget=c.target;
        Check(Through(c.site)(guide,nullptr)==guide,"the constructor's result handed back");
        Check(At<std::uint32_t>(guide,kMode)==c.want,c.who);
        Check(At<bool>(guide,kPolyline) && At<float>(guide,kSmoothing)==1.0f &&
              At<float>(guide,kNearDrop)==kKeepCorners && At<float>(guide,kMergeDrop)==kKeepCorners,c.who);
    }
    config.routeGuidePath=false;
    std::memset(guide,0,sizeof(guide));
    madeMode=kModeNone;madeTarget=true;
    Through(0x21F681)(guide,nullptr);
    Check(At<std::uint32_t>(guide,kMode)==kModeNone && !At<bool>(guide,kPolyline) && At<float>(guide,kNearDrop)==15.0f,
          "RouteGuidePath=0: the guide as the game made it");
    Check(made==4,"the constructor ran once for each guide");
    std::printf("route_guide_native_test: %d checks passed\n",checks);
    return 0;
}
