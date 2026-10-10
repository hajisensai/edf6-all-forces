// The stock route guide strip (class RouteGuide) along the navmesh route's own corners, not through the walls beside
// them (src/route_guide.h says what the stock builder does to the route). The user, 2026-10-10: the EDF5 missions'
// guide line, a stock fault EDF6 has too, "感觉指到墙里面了".
// RouteGuide's constructor 0x5C6C10(this, InitParam*) is called from three places, every guide a mission makes:
// the AngelScript DispRouteGuideToArea[EX] factory 0x1A7EDC (the snapshot registration 0x5C5FE0 too), DispRouteGuideToObject[EX]
// 0x1B4528, and the EDF5 scripts' BVM native 1200 0x21F681. Each call is redirected to a wrapper that runs the
// constructor and then sets the guide's fields (routeguide::Corrected); the frame's update reads them every frame.
// Docs: docs/route-guide-re.md. All addresses are RVAs into EDF.dll 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include "route_guide.h"
#include <cstring>
#include <iterator>

namespace crew {
namespace {
constexpr unsigned kCtor=0x5C6C10;
constexpr unsigned kSites[]={0x1A7EDC,0x1B4528,0x21F681};
constexpr unsigned char kCtorSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x4C,0x24,0x08,0x55,0x56,0x57,0x48,0x83,0xEC};
// The fields the wrapper writes, where the constructor and the strip's builder use them.
struct Signature { std::size_t rva; unsigned char bytes[12]; std::size_t size; };
const Signature kSignatures[]={
    {0x5C6CEE,{0xC7,0x87,0xD8,0x02,0x00,0x00,0x0A,0xD7,0x23,0x3D},10},     // mov [rdi+0x2D8], 0.04f (smoothing)
    {0x5C804F,{0x80,0xB9,0xA0,0x01,0x00,0x00,0x00,0x0F,0x85},9},          // cmp byte [rcx+0x1A0], 0: curve or polyline
    {0x5C7BF6,{0xF3,0x41,0x0F,0x10,0x8E,0x94,0x01,0x00,0x00},9},          // movss xmm1, [r14+0x194]: near drop
    {0x5C7CF6,{0xF3,0x41,0x0F,0x10,0x8D,0x98,0x01,0x00,0x00},9},          // movss xmm1, [r13+0x198]: merge drop
    {0x5C9D32,{0x8B,0x89,0x50,0x01,0x00,0x00},6},                         // mov ecx, [rcx+0x150]: the target's mode
    {0x5C9DFA,{0x48,0x8B,0x87,0x68,0x01,0x00,0x00},7},                    // mov rax, [rdi+0x168]: mode 1's object
    {0x21F44C,{0x44,0x89,0x75,0xB7},4},                                   // BVM 1200: InitParam mode = r14d (0)
};

using CtorFn=void*(__fastcall*)(void*,void*);
CtorFn ctor=nullptr;
bool ready=false;

// Whether the rel32 call at `site` still calls the constructor.
bool CallsCtor(unsigned site) noexcept {
    const unsigned char* p=image+site;
    if(!Readable(p,5) || p[0]!=0xE8)return false;
    std::int32_t rel;
    std::memcpy(&rel,p+1,4);
    return p+5+rel==image+kCtor;
}

void Correct(unsigned char* guide) noexcept {
    using namespace routeguide;
    if(!Readable(guide,kSmoothing+4,true))return;
    Fields f{At<std::uint32_t>(guide,kMode),At<const void*>(guide,kTarget)!=nullptr,At<float>(guide,kNearDrop),
             At<float>(guide,kMergeDrop),At<bool>(guide,kPolyline),At<float>(guide,kSmoothing)};
    const Fields was=f;
    f=Corrected(f);
    Put<std::uint32_t>(guide,kMode,f.mode);
    Put<float>(guide,kNearDrop,f.nearDrop);
    Put<float>(guide,kMergeDrop,f.mergeDrop);
    Put<bool>(guide,kPolyline,f.polyline);
    Put<float>(guide,kSmoothing,f.smoothing);
    if(Cfg().debug)
        Log("ROUTE guide %p: mode %u->%u, drop %.1f/%.1f->%.1f/%.1f, polyline %d, smoothing %.2f->1",guide,was.mode,
            f.mode,was.nearDrop,was.mergeDrop,f.nearDrop,f.mergeDrop,was.polyline,was.smoothing);
}

void* __fastcall CtorHook(void* guide,void* param) {
    void* const made=ctor(guide,param);
    if(ready && Cfg().routeGuidePath && made)Correct(static_cast<unsigned char*>(made));
    return made;
}
}  // namespace

// All or nothing: a guide corrected where one mission makes it and stock where another does is harder to report.
bool InstallRouteGuide() noexcept {
    static bool tried=false;
    if(tried)return ready;
    tried=true;
    bool profile=false;
    __try {
        profile=Matches(kCtor,kCtorSig,sizeof(kCtorSig));
        for(const auto& s:kSignatures)profile=profile && Matches(s.rva,s.bytes,s.size);
        for(const unsigned site:kSites)profile=profile && CallsCtor(site);
    } __except(EXCEPTION_EXECUTE_HANDLER){profile=false;}
    int done=0;
    if(profile) {
        ctor=reinterpret_cast<CtorFn>(image+kCtor);
        for(const unsigned site:kSites) {
            bool changed=false;
            if(RedirectCall(image+site,image+kCtor,reinterpret_cast<void*>(&CtorHook),changed))++done;
            else Log("ROUTE guide call %#x %s",site,changed ? "half patched" : "not patched");
        }
    }
    // Every site checked first; a redirect that still fails (no near page) leaves the hooks passing the constructor's
    // guide through as made (`ready` false), so no mission's guide is corrected while another's is not.
    ready=done==static_cast<int>(std::size(kSites));
    Log("HOOK route guide=%d (profile=%d, %d/%d calls, config=%d): the guide strip along the navmesh corners",ready,
        profile,done,static_cast<int>(std::size(kSites)),Cfg().routeGuidePath);
    return ready;
}
}  // namespace crew
