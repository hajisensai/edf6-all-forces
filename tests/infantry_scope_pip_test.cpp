// The picture-in-picture scope's production flow (src/infantryscope.cpp, InfantryScope=2; testhub #32), offline:
//  - the camera step (CamStepHook, chained over a stand-in of the stock step 0xF86A0 that writes pi/4 / cam+0x410 as
//    0xF8906 does): the scoped soldier's camera keeps pi/4 while the lens may draw (the first kProbationMs, then once it
//    drew in this opening, a stall included); its own write retracted before the next step (the no-target branch writes nothing);
//    another camera, mode 1, a foreign FOV, the world's end not hooked, a refused surface: the stock zoom stays;
//  - InfantryScopeNow's lens flag (the HUD frames the lens only while it draws) and its FOV (the reticle's angles);
//  - the world's end (InfantryScopeWorldEnd) on a real D3D11 WARP device: the scoped soldier's view gets the lens at the
//    focal ratio, another player's view nothing, a partial viewport (split screen) refuses: the camera zooms again, the
//    mission's reset tries again.
// Not the game: the real frame order, the native post stage and the look are for the game (docs/scope-pip-re.md).
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
    Check(Step(camera)==scopepip::kBaseFov,"probation: the scoped soldier's frame stays unmagnified");
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
    Open(GetTickCount64());
    Check(Step(camera)==scopepip::kBaseFov,"...and the next scope is unmagnified again");
    config.enabled=false;Check(Step(camera)==kStockFov,"the plugin off: the stock zoom");
    config.enabled=true;
}

// The world's end on WARP.
struct Gpu {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> color;ComPtr<ID3D11RenderTargetView> rtv;
    UINT w=128,h=96;
    std::vector<std::uint8_t> start;
    bool Make() {
        D3D_FEATURE_LEVEL level{};
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)) ||
           level<D3D_FEATURE_LEVEL_11_0)return false;
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET;
        start.resize(static_cast<std::size_t>(w)*h*4);
        for(UINT j=0;j<h;++j)for(UINT i=0;i<w;++i) {
            std::uint8_t* p=&start[(static_cast<std::size_t>(j)*w+i)*4];
            p[0]=static_cast<std::uint8_t>(i*255/(w-1));p[1]=static_cast<std::uint8_t>(j*255/(h-1));p[2]=64;p[3]=255;
        }
        const D3D11_SUBRESOURCE_DATA data{start.data(),w*4,0};
        return SUCCEEDED(device->CreateTexture2D(&d,&data,&color)) && SUCCEEDED(device->CreateRenderTargetView(color.Get(),nullptr,&rtv));
    }
    void Bind(bool full) {
        ID3D11RenderTargetView* t=rtv.Get();
        context->OMSetRenderTargets(1,&t,nullptr);
        const D3D11_VIEWPORT v{0,0,static_cast<float>(full ? w : w/2),static_cast<float>(h),0,1};
        context->RSSetViewports(1,&v);
    }
    void Restore() { context->UpdateSubresource(color.Get(),0,nullptr,start.data(),w*4,0); }
    std::vector<std::uint8_t> Read() {
        D3D11_TEXTURE2D_DESC d{};color->GetDesc(&d);
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&staging)))return {};
        context->CopyResource(staging.Get(),color.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m)))return {};
        std::vector<std::uint8_t> out(static_cast<std::size_t>(w)*h*4);
        for(UINT y=0;y<h;++y)std::memcpy(&out[static_cast<std::size_t>(y)*w*4],static_cast<const std::uint8_t*>(m.pData)+y*m.RowPitch,w*4);
        context->Unmap(staging.Get(),0);
        return out;
    }
};

int WorldEndChecks() {
    Gpu g;
    if(!g.Make()){std::printf("SKIP world's end: no WARP device\n");return 0;}
    // The world view's parameter block: its projection at +0x60 (row-major, [5] the vertical scale).
    alignas(16) unsigned char view[0x100]{};
    const auto project=[&](float fov){Put<float>(view,0x60+5*sizeof(float),scopepip::YScale(fov));};
    project(scopepip::kBaseFov);
    ResetInfantryScope();
    config.enabled=true;config.infantryScope=2;lensHooked.store(true);
    Open(GetTickCount64());
    Check(Step(camera)==scopepip::kBaseFov,"the camera left the frame unmagnified");

    // Another player's view (split screen's 2P): nothing drawn, no lens reported.
    viewOwner=stranger;g.Bind(true);
    End(g.context.Get(),view);
    Check(g.Read()==g.start && lensAt.load()==0,"another player's view: untouched");

    // The scoped soldier's view: the lens, at the focal ratio, round the centre; outside it nothing.
    viewOwner=soldier;g.Bind(true);
    End(g.context.Get(),view);
    const std::vector<std::uint8_t> after=g.Read();
    Check(lensAt.load()!=0 && Lens(),"the soldier's view: the lens drew, the HUD frames it");
    // A frame the stock zoom rendered (in flight from before the camera unzoomed, or zoomed again): no lens, not refused.
    {
        g.Restore();g.Bind(true);project(kStockFov);
        End(g.context.Get(),view);
        Check(g.Read()==g.start && !lensRefused.load() && !Lens(),"a stock-zoomed frame: no lens, nothing refused, none framed");
        project(scopepip::kBaseFov);g.Bind(true);
        End(g.context.Get(),view);
        g.Restore();g.Bind(true);
        End(g.context.Get(),view);
    }
    const scopepip::Lens l=scopepip::LensFor(static_cast<float>(g.w),static_cast<float>(g.h),
                                             scopepip::Magnification(scopepip::YScale(scopepip::kBaseFov),kStockFov));
    Check(l.m>kZoom && l.m<kZoom*1.1f,"the magnification is the focal ratio");
    bool outside=true,inside=true;int changed=0;
    for(UINT y=0;y<g.h;++y)for(UINT x=0;x<g.w;++x) {
        const std::size_t i=(static_cast<std::size_t>(y)*g.w+x)*4;
        const float px=x+0.5f,py=y+0.5f;
        if(!scopepip::InLens(l,px,py)){outside=outside && std::memcmp(&after[i],&g.start[i],4)==0;continue;}
        float sx,sy;scopepip::Source(l,px,py,&sx,&sy);
        // The gradient sampled bilinearly at the source (texel i holds i/(w-1), at its centre i+0.5).
        const float wantR=(sx-0.5f)/(g.w-1),wantG=(sy-0.5f)/(g.h-1);
        inside=inside && std::fabs(after[i]/255.0f-wantR)<=2.5f/255.0f && std::fabs(after[i+1]/255.0f-wantG)<=2.5f/255.0f;
        changed+=std::memcmp(&after[i],&g.start[i],4)!=0;
    }
    Check(outside,"outside the lens nothing is written (the world unmagnified)");
    Check(inside,"inside the lens the centre's neighbourhood scaled up by the magnification");
    Check(changed>0,"the lens changed the picture");

    // A stall past the lens's freshness (a pause, a hitch): the camera keeps the frame unmagnified and the next world view
    // draws the lens again (the stock-zoomed frames a stale lens used to bring back never get one).
    {
        Published p=Read();p.openedAt=GetTickCount64()-10*scopepip::kProbationMs;Publish(p);
        lensAt.store(GetTickCount64()-10*scopepip::kLensFreshMs);
        Check(Step(camera)==scopepip::kBaseFov && !Lens(),"stalled: unmagnified, no lens framed yet");
        g.Restore();g.Bind(true);
        End(g.context.Get(),view);
        Check(Lens() && g.Read()!=g.start,"...the next world view draws the lens again, the HUD frames it");
        Check(Step(camera)==scopepip::kBaseFov,"...and the camera stays unmagnified");
        g.Restore();
    }

    // A split screen's partial viewport: refused, the camera zooms again, the next mission tries again.
    g.Restore();g.Bind(false);
    End(g.context.Get(),view);
    Check(lensRefused.load() && g.Read()==g.start,"a partial viewport: refused, nothing drawn");
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
