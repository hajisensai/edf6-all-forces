// The picture-in-picture scope's production flow (src/infantryscope.cpp, InfantryScope=2; testhub #32), offline:
//  - the camera step (CamStepHook, chained over a stand-in of the stock step 0xF86A0 that writes pi/4 / cam+0x410 as
//    0xF8906 does): the scoped soldier's camera keeps pi/4 while the lens may draw (the first kProbationMs, then once it
//    drew in this opening, a stall included); its own write retracted before the next step (the no-target branch writes nothing);
//    another camera, mode 1, a foreign FOV, the world's end not hooked, a refused surface: the stock zoom stays;
//  - InfantryScopeNow's lens flag (the HUD frames the lens only while it draws) and its FOV (the reticle's angles);
//  - the camera step asks scope_lens.cpp for the lens view at the stock zoom's FOV; a refused lens view zooms again;
//  - the world's ends (InfantryScopeWorldEnd) and the lens command (InfantryScopeLensCommand) on a real D3D11 WARP
//    device, in the frame's order (docs/scope-pip-re.md §5.3): the main view's world colour held, the lens view's
//    (viewport 3, S x S) held, the command draws the lens view's picture into the lens's disc (scope_pip.h LensSample),
//    nothing outside it; the reticle's FOV from the lens view's projection; another player's view, a stock-zoomed frame,
//    a mis-sized lens view, a lens view with no main view before it: nothing; a partial viewport refuses until the
//    mission's reset.
// Not the game: the real frame order, the native post stage and the look are for the game (docs/scope-pip-re.md §7).
#include "../src/infantryscope.cpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace crew {
unsigned char* image=nullptr;
namespace {
Config config{};
int checks=0,failures=0;
bool worldEndHooked=true;
const void* viewOwner=nullptr;
bool stockWrites=true;   // the stock step's target branch (false: its no-target branch, no FOV written)
alignas(16) unsigned char soldier[0x100]{},stranger[0x100]{};
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
void __fastcall StockStep(void* cam,void*) {
    auto c=static_cast<unsigned char*>(cam);
    if(stockWrites)Put<float>(c,kCamFov,scopepip::kBaseFov/At<float>(c,kCamZoom));
}
}
const Config& Cfg() noexcept{return config;}
void Log(const char*,...) noexcept{}
unsigned char* PlayerHuman() noexcept{return soldier;}
bool HumanOnFoot(const unsigned char*) noexcept{return true;}
bool ReadRound(const unsigned char*,RoundModel* r) noexcept{*r=RoundModel{};return false;}
bool HudScopeHoldsCrosshair(const void*) noexcept{return false;}
bool EnemyControlReplacesPlayerHud(const void*) noexcept{return false;}
const void* LocalViewPlayer(const void*) noexcept{return viewOwner;}
namespace thermal { Status GetStatus(const void*) noexcept{Status s{};s.installed=worldEndHooked;return s;} }
// The lens view (scope_lens.cpp) as infantryscope.cpp sees it; tests/scope_lens_test.cpp covers the module itself.
namespace {
bool lensViewInstalled=true,lensViewRefused=false;
int lensViewSide=0;
const void* wantedCamera=nullptr;
float wantedFov=0.0f;
}
namespace scopelens {
bool Install() noexcept{return lensViewInstalled;}
bool Installed() noexcept{return lensViewInstalled;}
void Want(const void* camera,float scopeFov) noexcept{wantedCamera=camera;wantedFov=scopeFov;}
bool Refused() noexcept{return lensViewRefused;}
int Side() noexcept{return lensViewSide;}
void Reset() noexcept{lensViewRefused=false;}
}
}
bool Matches(unsigned,const unsigned char*,std::size_t) noexcept{return false;}

namespace {
using namespace crew;
constexpr float kZoom=4.0f;
const float kStockFov=scopepip::kBaseFov/kZoom;
alignas(16) unsigned char camera[0x500]{},otherCamera[0x500]{};

void Open(ULONGLONG openedAt) {
    Published p{};
    p.human=ObjRef::Of(soldier);p.cue=InfantryScopeCue{soldier,kZoom,scope::Look::marksman};p.at=GetTickCount64();p.openedAt=openedAt;
    Publish(p);
}
// The cue republished as the soldier's frame does (WARP's first shader compile can outlast its freshness).
void Refresh() { Published p=Read();p.at=GetTickCount64();Publish(p); }
void End(ID3D11DeviceContext* context,const void* view) { Refresh();InfantryScopeWorldEnd(context,view);Refresh(); }
float Step(unsigned char* cam) { CamStepHook(cam,nullptr);return At<float>(cam,kCamFov); }
bool Lens(float* fov=nullptr) {
    InfantryScopeCue c{};
    if(!InfantryScopeNow(&c))return false;
    if(fov)*fov=c.lensFov;
    return c.lens;
}

void CameraChecks() {
    nextCamStep=&StockStep;
    for(unsigned char* c:{camera,otherCamera}){Put<float>(c,kCamZoom,kZoom);Put<float>(c,kCamFov,scopepip::kBaseFov);}
    Put<const void*>(camera,kCamTarget,soldier);Put<const void*>(otherCamera,kCamTarget,stranger);
    config.enabled=true;config.infantryScope=2;lensHooked.store(true);
    const ULONGLONG now=GetTickCount64();

    Open(now);
    wantedCamera=nullptr;wantedFov=0.0f;
    Check(Step(camera)==scopepip::kBaseFov,"probation: the scoped soldier's frame stays unmagnified");
    Check(wantedCamera==camera && wantedFov==kStockFov,"...and the lens view is asked for at the stock zoom's FOV");
    Check(Step(otherCamera)==kStockFov,"another soldier's camera keeps its stock zoom");
    {
        const Published p=Read();
        Check(p.frameFov==scopepip::kBaseFov && p.scopeFov==kStockFov,"the camera publishes the frame's FOV and the scope's");
    }
    Check(!Lens(),"no lens drew yet: the HUD does not frame one");

    // The lens drew: the HUD frames it with the scope's FOV, the camera keeps the frame unmagnified past the probation.
    lensAt.store(GetTickCount64());lensShown.store(true);lensShownFov.store(kStockFov);
    float fov=0.0f;
    Check(Lens(&fov) && fov==kStockFov,"the lens drew: the HUD frames it, the reticle at the scope's FOV");
    lensShown.store(false);
    Check(!Lens(),"the soldier's last world view had no lens: the HUD frames none");
    lensShown.store(true);
    Open(now-10*scopepip::kProbationMs);
    Check(Step(camera)==scopepip::kBaseFov,"past the probation the lens drawing keeps the frame unmagnified");

    // The lens stopped drawing for a while (a pause: the cue goes stale, the camera step zooms; a hitch) after it drew
    // in this opening: the frame stays unmagnified so the lens can draw again; the HUD frames none until it does.
    lensAt.store(now-5*scopepip::kProbationMs);
    Check(Step(camera)==scopepip::kBaseFov,"the lens drew in this opening, then stalled: still unmagnified (it can draw again)");
    Check(!Lens(),"...and the HUD frames no lens until it draws again");
    // No lens ever drew in this opening past its probation (one drawn before it proves nothing): the stock zoom.
    Open(now-2*scopepip::kProbationMs);lensAt.store(now-3*scopepip::kProbationMs);
    Check(Step(camera)==kStockFov,"no lens in this opening past the probation: the stock zoom is back");
    Check(!Lens(),"...and the HUD frames no lens (the whole-screen eyepiece)");
    {
        const Published p=Read();
        Check(p.frameFov==0.0f,"...the frame published as the stock zoom's (the world's end draws nothing)");
    }

    // The no-target branch writes nothing: this hook's own write is retracted before it.
    Open(now);lensAt.store(GetTickCount64());
    Check(Step(camera)==scopepip::kBaseFov,"unmagnified again");
    Publish(Published{});stockWrites=false;
    Check(Step(camera)==kStockFov,"scope gone, no-target step: the hook's own pi/4 retracted");
    stockWrites=true;

    // A FOV some other logic wrote (the close-up's easing): left alone, and not retracted over.
    Open(now);
    stockWrites=false;Put<float>(camera,kCamFov,0.3f);
    Check(Step(camera)==0.3f,"a foreign FOV is not unzoomed");
    stockWrites=true;

    // Mode 1, mode 0, the world's end not hooked, a refused surface: the stock zoom.
    config.infantryScope=1;Check(Step(camera)==kStockFov,"InfantryScope=1: the whole screen zooms");
    config.infantryScope=0;Check(Step(camera)==kStockFov,"InfantryScope=0: the stock zoom");
    config.infantryScope=2;
    lensHooked.store(false);Check(Step(camera)==kStockFov,"the world's end not hooked: the stock zoom");
    lensHooked.store(true);
    lensRefused.store(true);Check(Step(camera)==kStockFov && !Lens(),"a refused surface: the stock zoom, no lens framed");
    ResetInfantryScope();
    Check(!lensRefused.load() && lensAt.load()==0,"the mission's reset tries the lens again");
    Open(GetTickCount64());lensAt.store(GetTickCount64());lensShown.store(true);lensShownFov.store(kStockFov);
    lensViewRefused=true;
    Check(Step(camera)==kStockFov && !Lens(),"a refused lens view (split screen, viewport 3 in use): the stock zoom, no lens framed");
    ResetInfantryScope();
    Check(!lensViewRefused,"the mission's reset tries the lens view again");
    Open(GetTickCount64());
    Check(Step(camera)==scopepip::kBaseFov,"...and the next scope is unmagnified again");
    config.enabled=false;Check(Step(camera)==kStockFov,"the plugin off: the stock zoom");
    config.enabled=true;
}

// A colour target on WARP with a gradient (R across, G down), its RTV and its first contents.
struct Target {
    ComPtr<ID3D11Texture2D> color;ComPtr<ID3D11RenderTargetView> rtv;
    UINT w=0,h=0;
    std::vector<std::uint8_t> start;
    bool Make(ID3D11Device* device,UINT width,UINT height,std::uint8_t blue) {
        w=width;h=height;
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        start.resize(static_cast<std::size_t>(w)*h*4);
        for(UINT j=0;j<h;++j)for(UINT i=0;i<w;++i) {
            std::uint8_t* p=&start[(static_cast<std::size_t>(j)*w+i)*4];
            p[0]=static_cast<std::uint8_t>(i*255/(w-1));p[1]=static_cast<std::uint8_t>(j*255/(h-1));p[2]=blue;p[3]=255;
        }
        const D3D11_SUBRESOURCE_DATA data{start.data(),w*4,0};
        return SUCCEEDED(device->CreateTexture2D(&d,&data,&color)) && SUCCEEDED(device->CreateRenderTargetView(color.Get(),nullptr,&rtv));
    }
};
struct Gpu {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    Target main,lens,odd;
    bool Make(UINT side) {
        D3D_FEATURE_LEVEL level{};
        return SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)) &&
               level>=D3D_FEATURE_LEVEL_11_0 && main.Make(device.Get(),128,96,64) && lens.Make(device.Get(),side,side,200) &&
               odd.Make(device.Get(),side+6,side,200);
    }
    void Bind(const Target& t,bool full=true) {
        ID3D11RenderTargetView* r=t.rtv.Get();
        context->OMSetRenderTargets(1,&r,nullptr);
        const D3D11_VIEWPORT v{0,0,static_cast<float>(full ? t.w : t.w/2),static_cast<float>(t.h),0,1};
        context->RSSetViewports(1,&v);
    }
    void Restore(const Target& t) { context->UpdateSubresource(t.color.Get(),0,nullptr,t.start.data(),t.w*4,0); }
    std::vector<std::uint8_t> Read(const Target& t) {
        D3D11_TEXTURE2D_DESC d{};t.color->GetDesc(&d);
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&staging)))return {};
        context->CopyResource(staging.Get(),t.color.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m)))return {};
        std::vector<std::uint8_t> out(static_cast<std::size_t>(t.w)*t.h*4);
        for(UINT y=0;y<t.h;++y)std::memcpy(&out[static_cast<std::size_t>(y)*t.w*4],static_cast<const std::uint8_t*>(m.pData)+y*m.RowPitch,t.w*4);
        context->Unmap(staging.Get(),0);
        return out;
    }
};

int WorldEndChecks() {
    const float mw=128.0f,mh=96.0f;
    const float r=scopepip::LensRadius(mw,mh);
    const int side=scopepip::LensViewSide(r);
    Gpu g;
    if(!g.Make(static_cast<UINT>(side))){std::printf("SKIP world's end: no WARP device\n");return 0;}
    lensViewSide=side;
    // The world views' parameter blocks: +0 the viewport, +4 the query (0 the main one), +0x60 the projection
    // (row-major, [5] the vertical scale). The lens view's field: scope_pip.h LensViewFov.
    alignas(16) unsigned char mainView[0x100]{},lensView[0x100]{},shadowView[0x100]{};
    Put<unsigned>(lensView,0,scopepip::kLensViewport);
    Put<unsigned>(shadowView,4,2);
    const auto project=[](unsigned char* view,float fov){Put<float>(view,0x60+5*sizeof(float),scopepip::YScale(fov));};
    const float lensFov=scopepip::LensViewFov(mh,r,kStockFov);
    project(mainView,scopepip::kBaseFov);project(lensView,lensFov);project(shadowView,scopepip::kBaseFov);
    ResetInfantryScope();
    config.enabled=true;config.infantryScope=2;lensHooked.store(true);
    Open(GetTickCount64());
    Check(Step(camera)==scopepip::kBaseFov,"the camera left the frame unmagnified");
    // One frame in the recording's order: the main view's world end, the lens view's, the lens command.
    const auto frame=[&](unsigned char* view,const Target& lensTarget) {
        g.Bind(g.main);End(g.context.Get(),view);
        g.Bind(lensTarget);End(g.context.Get(),lensView);
        Refresh();InfantryScopeLensCommand(g.context.Get());Refresh();
    };

    // Another player's view (split screen's 2P): nothing held, nothing drawn, no lens reported.
    viewOwner=stranger;
    frame(mainView,g.lens);
    Check(g.Read(g.main)==g.main.start && lensAt.load()==0,"another player's view: untouched");

    // A shadow cascade's world end (query 2) holds nothing.
    viewOwner=soldier;
    g.Bind(g.main);End(g.context.Get(),shadowView);
    g.Bind(g.lens);End(g.context.Get(),lensView);
    InfantryScopeLensCommand(g.context.Get());
    Check(g.Read(g.main)==g.main.start && lensAt.load()==0,"a shadow cascade's world end: nothing held, nothing drawn");

    // The scoped soldier's frame: the lens view's picture in the disc, nothing outside it.
    frame(mainView,g.lens);
    const std::vector<std::uint8_t> after=g.Read(g.main);
    Check(lensAt.load()!=0 && Lens(),"the soldier's frame: the lens drew, the HUD frames it");
    float shown=0.0f;Lens(&shown);
    Check(std::fabs(shown-kStockFov)<1e-4f,"the reticle's FOV from the lens view's projection is the stock zoom's");
    Check(g.Read(g.lens)==g.lens.start,"the lens view's own target is not written");
    const scopepip::Lens l=scopepip::LensFor(mw,mh);
    bool outside=true,inside=true;int changed=0;
    for(UINT y=0;y<g.main.h;++y)for(UINT x=0;x<g.main.w;++x) {
        const std::size_t i=(static_cast<std::size_t>(y)*g.main.w+x)*4;
        const float px=x+0.5f,py=y+0.5f;
        if(!scopepip::InLens(l,px,py)){outside=outside && std::memcmp(&after[i],&g.main.start[i],4)==0;continue;}
        float u,v;scopepip::LensSample(l,px,py,&u,&v);
        // The lens view's gradient sampled bilinearly (texel i holds i/(S-1) at its centre i+0.5, clamped at the edge).
        const auto expect=[&](float t){return std::fmin(1.0f,std::fmax(0.0f,(t*side-0.5f)/(side-1)));};
        inside=inside && std::fabs(after[i]/255.0f-expect(u))<=2.5f/255.0f && std::fabs(after[i+1]/255.0f-expect(v))<=2.5f/255.0f &&
               after[i+2]==200;
        changed+=std::memcmp(&after[i],&g.main.start[i],4)!=0;
    }
    Check(outside,"outside the lens nothing is written (the world unmagnified)");
    Check(inside,"inside the lens the lens view's picture spans the disc (LensSample)");
    Check(changed>0,"the lens changed the picture");

    // The lens command with nothing held (a frame with no lens view, or a second command): nothing.
    g.Restore(g.main);
    InfantryScopeLensCommand(g.context.Get());
    Check(g.Read(g.main)==g.main.start && !lensShown.load(),"no views held: the command draws nothing");
    // The lens view's world end with no main view held before it: nothing.
    g.Bind(g.lens);End(g.context.Get(),lensView);
    InfantryScopeLensCommand(g.context.Get());
    Check(g.Read(g.main)==g.main.start,"a lens view with no main view before it: nothing");

    // A frame the stock zoom rendered (in flight from before the camera unzoomed): no lens, not refused.
    project(mainView,kStockFov);
    frame(mainView,g.lens);
    Check(g.Read(g.main)==g.main.start && !lensRefused.load() && !Lens(),"a stock-zoomed frame: no lens, nothing refused, none framed");
    project(mainView,scopepip::kBaseFov);

    // A lens view whose target is not the S x S one (a resize in flight): no lens that frame, not refused.
    frame(mainView,g.odd);
    Check(g.Read(g.main)==g.main.start && !lensRefused.load(),"a mis-sized lens view: no lens, nothing refused");
    frame(mainView,g.lens);
    Check(Lens() && g.Read(g.main)!=g.main.start,"...the next frame draws it again");
    g.Restore(g.main);

    // A stall past the lens's freshness (a pause, a hitch): the camera keeps the frame unmagnified and the next frame
    // draws the lens again.
    {
        Published p=Read();p.openedAt=GetTickCount64()-10*scopepip::kProbationMs;Publish(p);
        lensAt.store(GetTickCount64()-10*scopepip::kLensFreshMs);
        Check(Step(camera)==scopepip::kBaseFov && !Lens(),"stalled: unmagnified, no lens framed yet");
        frame(mainView,g.lens);
        Check(Lens() && g.Read(g.main)!=g.main.start,"...the next frame draws the lens again, the HUD frames it");
        Check(Step(camera)==scopepip::kBaseFov,"...and the camera stays unmagnified");
        g.Restore(g.main);
    }

    // A split screen's partial viewport: refused, the camera zooms again, the next mission tries again.
    {
        ID3D11RenderTargetView* t=g.main.rtv.Get();
        g.context->OMSetRenderTargets(1,&t,nullptr);
        const D3D11_VIEWPORT v{0,0,static_cast<float>(g.main.w/2),static_cast<float>(g.main.h),0,1};
        g.context->RSSetViewports(1,&v);
        End(g.context.Get(),mainView);
        g.Bind(g.lens);End(g.context.Get(),lensView);
        InfantryScopeLensCommand(g.context.Get());
    }
    Check(lensRefused.load() && g.Read(g.main)==g.main.start,"a partial viewport: refused, nothing drawn");
    Open(GetTickCount64());
    Check(Step(camera)==kStockFov && !Lens(),"refused: the stock zoom, the whole-screen eyepiece");
    ResetInfantryScope();
    Check(!lensRefused.load(),"the mission's reset clears the refusal");
    viewOwner=nullptr;
    return 0;
}
}  // namespace

int wmain() {
    CameraChecks();
    WorldEndChecks();
    std::printf("infantry_scope_pip: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
