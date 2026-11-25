#include "gpufx_gpu.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

namespace crew::gpufx {
namespace {

using Microsoft::WRL::ComPtr;
constexpr UINT kTargets=D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;

constexpr char kShaders[]=R"hlsl(
// Row-vector matrices exactly as the engine's Commander view block holds them (view+0x20 view, view+0x60 projection).
cbuffer Frame : register(b0) {
    row_major float4x4 View;
    row_major float4x4 Proj;
    float4 SunView;    // xyz: towards the sun, view space
    float4 SunColor;   // rgb; w: exposure (the scene's measured light scale)
    float4 Ambient;    // rgb; w: near-camera fade distance
    float4 Screen;     // width, height, 1/width, 1/height
    float4 Fade;       // the stock particle distance fade: x mul, y add (alpha *= sat(1 - (distance * x + y)))
};
cbuffer BatchConstants : register(b1) {
    uint Base;
    uint GridX;
    uint GridY;
    uint Frames;
    uint Kind;
    float CornerScale;
    float UInset;
    uint LitSet;
    float4 LitWeights;   // xyz: the normal's, the corner direction's and the camera direction's weights
};
// The stock lighting (bound only with lit batches): xgl_system (cb0 of the stock particle shaders) and
// xgl_additional_light_setting (their cb3), the shadow map array and the clustered point lights.
cbuffer StockSystem : register(b2) { float4 Sys[38]; };
cbuffer StockShadow : register(b3) { float4 Sh[25]; };
struct PointLight { float4 posRange; float4 colorFalloff; float4 unused; float4 cone; };   // +56 cone.z, +60 cone.w
struct ClusterMask { uint w[65]; };
Texture2DArray<float4> ShadowMap : register(t3);
StructuredBuffer<PointLight> Points : register(t4);
StructuredBuffer<ClusterMask> MaskX : register(t5);
StructuredBuffer<ClusterMask> MaskY : register(t6);
StructuredBuffer<ClusterMask> MaskZ : register(t7);
struct Particle {
    float3 pos;
    float size;
    float4 color;
    float rot;
    float frame;
    float soft;
    float emissive;
};
StructuredBuffer<Particle> Particles : register(t0);
Texture2D<float4> Tex : register(t1);
Texture2D<float> Depth : register(t2);
SamplerState Linear : register(s0);

struct V {
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float4 uv : TEXCOORD0;
    float3 misc : TEXCOORD1;    // frame blend, distance from the camera plane, soft range
    float3 basis : TEXCOORD2;   // cos and sin of the rotation, emissive
    float4 vpos : TEXCOORD3;    // the corner's view-space position; w: the centre's distance from the camera
    float3 nrm : TEXCOORD4;     // the lit kinds: the stock VS's world normal
};

float3 UnpackNormal(uint u) {
    return float3(u & 1023u, (u >> 10) & 1023u, (u >> 20) & 1023u) / 1023.0 * 2.0 - 1.0;
}

// A tile's uv: row-major from the top left, u trimmed by UInset on both sides (the stock VS's frc(u) + 0.0005).
float2 FrameUV(uint f, float2 uv) {
    float2 cell = float2(f % GridX, f / GridX);
    float2 lo = cell / float2(GridX, GridY) + float2(UInset, 0.0);
    float2 hi = (cell + 1.0) / float2(GridX, GridY) - float2(UInset, 0.0);
    return lerp(lo, hi, uv);
}

V Vs(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    Particle p = Particles[Base + iid];
    float2 c = float2((vid & 1) ? 1.0 : -1.0, (vid & 2) ? -1.0 : 1.0);
    float s, co;
    sincos(p.rot, s, co);
    float2 r = float2(c.x * co - c.y * s, c.x * s + c.y * co) * (p.size * CornerScale);
    float4 vp = mul(float4(p.pos, 1.0), View);
    float centre = length(vp.xyz);
    vp.xy += r;
    V o;
    o.pos = mul(vp, Proj);
    o.color = p.color;
    float2 uv = c * float2(0.5, -0.5) + 0.5;
    float last = (float)(max(Frames, 1u) - 1u);
    float fr = clamp(p.frame, 0.0, last);
    uint f0 = (uint)fr;
    uint f1 = min(f0 + 1u, (uint)last);
    o.uv = float4(FrameUV(f0, uv), FrameUV(f1, uv));
    o.misc = float3(fr - (float)f0, abs(vp.z), p.soft);
    o.basis = float3(co, s, p.emissive);
    o.vpos = float4(vp.xyz, centre);
    o.nrm = float3(0.0, 1.0, 0.0);
    if (Kind >= 3u) {
        // The stock lit VS (0x196F5C0): normalize(normal * w.x + the corner's world direction * w.y + w.z times the
        // direction from the view-space corner to the camera's world position: the stock mixes the two spaces).
        float3 n = LitSet == 1u ? UnpackNormal(asuint(p.emissive)) : float3(0.0, 1.0, 0.0);
        float3 side = float3(dot(r, View._11_12), dot(r, View._21_22), dot(r, View._31_32));
        float3 cam = float3(Sys[4].w, Sys[5].w, Sys[6].w);
        float3 sum = n * LitWeights.x + side * LitWeights.y + normalize(cam - vp.xyz) * LitWeights.z;
        o.nrm = dot(sum, sum) > 1e-12 ? normalize(sum) : float3(0.0, 1.0, 0.0);
    }
    return o;
}

// The stock lit PS's light (0x196BA60): `v` the view-space position, `n` the world normal, `px` the target pixel.
float ShadowAt(float3 v) {
    float z = abs(v.z);
    if (!(Sh[17].x >= z)) return 1.0;
    uint k = z < Sh[8].x ? 0u : 9u;
    float slice = z < Sh[8].x ? 0.0 : 1.0;
    float4 p = float4(v, 1.0);
    float4 q = float4(dot(p, Sh[k + 0]), dot(p, Sh[k + 1]), 0.0, 1.0);
    float d = dot(p, Sh[k + 2]);
    q.z = d + Sh[k + 8].y;
    float2 s = float2(dot(q, Sh[k + 4]), dot(q, Sh[k + 5]));
    float ref = dot(q, Sh[k + 6]);
    float range = dot(float4(q.xy, d - Sh[k + 8].z, 1.0), Sh[k + 6]) - ref;
    uint w, h, layers;
    ShadowMap.GetDimensions(w, h, layers);
    float2 t = (s * float2(0.5, -0.5) + 0.5) * float2(w, h);
    float2 f = frac(t);
    int2 i = int2(t);
    float4 m = float4(ShadowMap.Load(int4(i, slice, 0)).x, ShadowMap.Load(int4(i + int2(1, 0), slice, 0)).x,
                      ShadowMap.Load(int4(i + int2(0, 1), slice, 0)).x, ShadowMap.Load(int4(i + int2(1, 1), slice, 0)).x);
    m = saturate((m - ref) / range + 1.0);
    float2 row = lerp(m.xz, m.yw, f.x);
    float lit = lerp(row.x, row.y, f.y);
    float away = saturate((z - Sh[18].x) * Sh[18].y);
    return lit + away * (1.0 - lit);
}
float3 PointLights(float3 v, float3 n, float2 px) {
    float3 l = 0.0;
    float4 v1 = float4(v, 1.0);
    float3 w = float3(dot(v1, Sys[4]), dot(v1, Sys[5]), dot(v1, Sys[6]));
    float4 w1 = float4(w, 1.0);
    float2 screen = float2(asuint(Sys[20].x), asuint(Sys[20].y));
    uint2 cxy = (uint2)(px / screen * 128.0);
    uint cz = (uint)(dot(w1, Sh[23]) / dot(w1, Sh[24]) * 128.0);
    if ((MaskX[cxy.x].w[64] & MaskY[cxy.y].w[64] & MaskZ[cz].w[64]) == 0u) return l;
    uint words = (asuint(Sh[19].y) + 31u) >> 5;
    [loop] for (uint k = 0u; k < words; ++k) {
        uint bits = MaskX[cxy.x].w[k] & MaskY[cxy.y].w[k] & MaskZ[cz].w[k];
        [loop] while (bits != 0u) {
            uint b = firstbitlow(bits);
            bits &= ~(1u << b);
            PointLight pl = Points[k * 32u + b];
            float3 d = w - pl.posRange.xyz;
            float dist = length(d);
            if (dist < pl.posRange.w) {
                float t = pl.cone.z - dot(d / dist, n) * (1.0 - pl.cone.z);
                if (t > 0.0) l += pow(max(1.0 - dist * pl.colorFalloff.w, 0.0), pl.cone.w) * saturate(t) * pl.colorFalloff.rgb;
            }
        }
    }
    return l;
}
float3 StockLight(float3 v, float3 n, float2 px) {
    float3 l = ShadowAt(v) * saturate(-dot(n, Sys[21].xyz)) * Sys[22].rgb;
    l += saturate(-dot(n, Sys[24].xyz)) * Sys[25].rgb;
    l += saturate(-dot(n, Sys[27].xyz)) * Sys[28].rgb;
    l += saturate(-dot(n, Sys[30].xyz)) * Sys[31].rgb;
    l += Sys[33].rgb;
    return l + PointLights(v, n, px);
}

// The distance from the camera plane of the scene at this pixel (SV_Position is in target pixels; the depth copy is the
// whole target, so a split-screen viewport reads its own rectangle), from the hardware depth under this view's own
// projection (d = (z*P22 + P32) / (z*P23 + P33) solved for z; standard and reversed depth alike).
float SceneDistance(float2 px) {
    float d = Depth.Load(int3(int2(px), 0));
    float den = d * Proj._34 - Proj._33;
    return abs((Proj._43 - d * Proj._44) / den);
}

float4 Ps(V i) : SV_Target0 {
    float4 t = lerp(Tex.Sample(Linear, i.uv.xy), Tex.Sample(Linear, i.uv.zw), i.misc.x);
    float fade = saturate((SceneDistance(i.pos.xy) - i.misc.y) / max(i.misc.z, 0.001));
    if (Kind == 0u) {
        fade *= saturate((i.misc.y - 0.3) / max(Ambient.w, 0.001));   // ours only: no card cuts the near plane
        float a = t.a * i.color.a * fade;
        float thick = t.b;
        float2 n2 = t.rg * 2.0 - 1.0;
        float2 nr = float2(n2.x * i.basis.x - n2.y * i.basis.y, n2.x * i.basis.y + n2.y * i.basis.x);
        float3 n = float3(nr, sqrt(saturate(1.0 - dot(nr, nr))));
        // The sun: the view's own light block, or (LitSet 1: this view's stock lighting captured) the stock lit
        // particles' sun, ambient, cascade shadow and point lights, in the scene's own units (no exposure guess).
        float3 sunTo = SunView.xyz, sunRgb = SunColor.rgb * SunColor.w, amb = Ambient.rgb * SunColor.w, extra = 0.0;
        float sunShadow = 1.0;
        if (LitSet == 1u) {
            // view -> world rotation (Sys[4..6].xyz), the stock light's travel direction Sys[21] back into view space
            float3x3 vw = float3x3(Sys[4].xyz, Sys[5].xyz, Sys[6].xyz);
            sunTo = -mul(Sys[21].xyz, vw);
            sunRgb = Sys[22].rgb;
            amb = Sys[33].rgb;
            sunShadow = ShadowAt(i.vpos.xyz);
            extra = PointLights(i.vpos.xyz, mul(vw, n), i.pos.xy);
        }
        float wrap = saturate((dot(n, sunTo) + 0.6) / 1.6);
        // Absorption: sunlight reaching a texel crosses the puff's own thickness on the side turned away from the sun
        // (a dark core and underside: Beer-Lambert over the thickness, the core darker than the rim), the ambient
        // sky less so (it comes from everywhere: the core sees less of it).
        float shadow = exp(-thick * (1.0 - wrap) * 3.2) * (1.0 - 0.35 * thick);
        // Edge transmission: thin edges seen towards the sun glow (Henyey-Greenstein forward scattering, g = 0.55).
        float cosT = dot(normalize(i.vpos.xyz), sunTo);
        float hg = 0.6975 / pow(max(1.3025 - 1.1 * cosT, 1e-3), 1.5);
        float trans = hg * (1.0 - thick) * 0.22;
        float3 light = amb * (1.0 - 0.55 * thick) + sunRgb * sunShadow * (wrap * shadow + trans) + extra * (1.0 - 0.5 * thick);
        float3 rgb = i.color.rgb * light;
        return float4(rgb * a, a * (1.0 - i.basis.z));
    }
    // The stock forward PS (0x19670A0): rgb = texel * colour (no saturate: HDR), a = sat(texel.a * colour.a) times
    // sat(the VS's distance fade); ours adds the soft depth fade.
    float a = saturate(t.a * i.color.a) * saturate(1.0 - (i.vpos.w * Fade.x + Fade.y)) * fade;
    if (Kind >= 3u) {
        if (LitSet == 2u && t.a * i.color.a < 0.001) discard;   // the set 0xC08's clip
        float3 rgb = t.rgb * i.color.rgb * StockLight(i.vpos.xyz, normalize(i.nrm), i.pos.xy);
        return Kind == 3u ? float4(rgb * a, a) : float4(rgb * fade, a);
    }
    if (Kind == 1u) return float4(t.rgb * i.color.rgb * a, a);
    return float4(t.rgb * i.color.rgb * fade, a);
}
)hlsl";

using CompileFunction=HRESULT(WINAPI*)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,
                                       ID3DBlob**,ID3DBlob**);

bool DepthFormats(DXGI_FORMAT view,DXGI_FORMAT& resource,DXGI_FORMAT& shader) noexcept {
    switch(view) {
    case DXGI_FORMAT_D16_UNORM: resource=DXGI_FORMAT_R16_TYPELESS;shader=DXGI_FORMAT_R16_UNORM;return true;
    case DXGI_FORMAT_D24_UNORM_S8_UINT: resource=DXGI_FORMAT_R24G8_TYPELESS;shader=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;return true;
    case DXGI_FORMAT_D32_FLOAT: resource=DXGI_FORMAT_R32_TYPELESS;shader=DXGI_FORMAT_R32_FLOAT;return true;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        resource=DXGI_FORMAT_R32G8X24_TYPELESS;shader=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;return true;
    default: return false;
    }
}

struct FrameConstants {
    float view[16],proj[16];
    float sun[4],sunColor[4],ambient[4],screen[4],fade[4];
};
struct BatchConstants {
    std::uint32_t base,gridX,gridY,frames,kind;
    float cornerScale,uInset;
    std::uint32_t litSet;
    float litWeights[4];
};
static_assert(sizeof(FrameConstants)%16==0 && sizeof(BatchConstants)%16==0,"constant buffer sizes");

struct ScopedContextState final {
    ID3D11DeviceContext1* context;
    ComPtr<ID3DDeviceContextState> previous;
    ScopedContextState(ID3D11DeviceContext1* value,ID3DDeviceContextState* state) noexcept : context(value) {
        context->SwapDeviceContextState(state,&previous);
    }
    ~ScopedContextState() noexcept { context->SwapDeviceContextState(previous.Get(),nullptr); }
    ScopedContextState(const ScopedContextState&)=delete;
    ScopedContextState& operator=(const ScopedContextState&)=delete;
};

}  // namespace

struct Renderer::Impl final {
    HMODULE compilerModule=nullptr;
    CompileFunction compile=nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11DeviceContext1> context1;
    ComPtr<ID3DDeviceContextState> isolated;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11BlendState> blend,blendStockAlpha,blendStockAdd;
    ComPtr<ID3D11DepthStencilState> noDepth;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Buffer> particles,frameCb,batchCb;
    ComPtr<ID3D11ShaderResourceView> particleView;
    ComPtr<ID3D11Texture2D> flip;
    ComPtr<ID3D11ShaderResourceView> flipView;
    std::vector<std::uint8_t> flipPixels;
    ComPtr<ID3D11Texture2D> depthCopy;
    ComPtr<ID3D11ShaderResourceView> depthView;
    D3D11_TEXTURE2D_DESC depthDesc{};
    DXGI_FORMAT depthViewFormat=DXGI_FORMAT_UNKNOWN;
    // GPU time: a ring of timestamp sets, read back without stalling a few frames later.
    static constexpr int kQueries=4;
    struct Timing { ComPtr<ID3D11Query> disjoint,begin,end; bool pending=false; };
    std::array<Timing,kQueries> timing;
    int timingNext=0;
    float gpuMs=-1.0f;
    // The scene's luminance: five 16x16 blocks of the colour target copied before our draw into a ring of staging
    // textures, mapped without waiting a few frames later.
    static constexpr int kLumaRing=3,kLumaBlock=16,kLumaBlocks=5;
    struct LumaRead { ComPtr<ID3D11Texture2D> tex; bool pending=false; };
    std::array<LumaRead,kLumaRing> luma;
    int lumaNext=0;
    DXGI_FORMAT lumaFormat=DXGI_FORMAT_UNKNOWN;
    float sceneLuma=-1.0f,sceneLumaMax=-1.0f;
    std::vector<std::uint32_t> order;
    std::vector<float> keys;
    bool failed=false;

    ~Impl() noexcept { if(compilerModule)FreeLibrary(compilerModule); }

    bool Compile(const char* entry,const char* profile,ComPtr<ID3DBlob>& blob) noexcept {
        ComPtr<ID3DBlob> errors;
        return SUCCEEDED(compile(kShaders,sizeof(kShaders)-1,"gpufx",nullptr,nullptr,entry,profile,
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors));
    }

    bool Initialize(ID3D11DeviceContext* value) noexcept {
        if(!value || value->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
        value->GetDevice(&device);
        if(!device || device->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0 || FAILED(value->QueryInterface(IID_PPV_ARGS(&context1))))
            return false;
        ComPtr<ID3D11Device1> device1;
        if(FAILED(device.As(&device1)))return false;
        const D3D_FEATURE_LEVEL level=device->GetFeatureLevel();
        const UINT flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED) ?
            D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
        if(FAILED(device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device1),nullptr,&isolated)))
            return false;
        compilerModule=LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!compilerModule)return false;
        const FARPROC address=GetProcAddress(compilerModule,"D3DCompile");
        static_assert(sizeof(address)==sizeof(compile),"Windows function pointer size");
        std::memcpy(&compile,&address,sizeof(compile));
        if(!compile)return false;
        ComPtr<ID3DBlob> vsCode,psCode;
        if(!Compile("Vs","vs_5_0",vsCode) || !Compile("Ps","ps_5_0",psCode))return false;
        if(FAILED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs)) ||
           FAILED(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps)))return false;
        D3D11_BLEND_DESC b{};
        auto& rt=b.RenderTarget[0];
        rt.BlendEnable=TRUE;rt.SrcBlend=D3D11_BLEND_ONE;rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOp=D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha=D3D11_BLEND_ZERO;rt.DestBlendAlpha=D3D11_BLEND_ONE;rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
        // The stock blends as their states were met (GPUFX native blend log): 1 = SRC_ALPHA/INV_SRC_ALPHA, alpha ONE/ZERO
        // (our premultiplied output gives the same colour); 2 = ONE/ONE on colour and alpha. All four channels written.
        D3D11_BLEND_DESC ba=b;auto& ra=ba.RenderTarget[0];
        ra.SrcBlendAlpha=D3D11_BLEND_ONE;ra.DestBlendAlpha=D3D11_BLEND_ZERO;ra.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_BLEND_DESC bd=ba;auto& rd=bd.RenderTarget[0];
        rd.SrcBlend=rd.DestBlend=rd.SrcBlendAlpha=rd.DestBlendAlpha=D3D11_BLEND_ONE;
        D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=FALSE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc=D3D11_COMPARISON_ALWAYS;
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
        s.ComparisonFunc=D3D11_COMPARISON_NEVER;
        D3D11_BUFFER_DESC pb{};pb.ByteWidth=static_cast<UINT>(sizeof(Instance))*kMaxInstances;pb.Usage=D3D11_USAGE_DYNAMIC;
        pb.BindFlags=D3D11_BIND_SHADER_RESOURCE;pb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        pb.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;pb.StructureByteStride=sizeof(Instance);
        D3D11_BUFFER_DESC fb{};fb.ByteWidth=sizeof(FrameConstants);fb.Usage=D3D11_USAGE_DYNAMIC;
        fb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;fb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        D3D11_BUFFER_DESC bb=fb;bb.ByteWidth=sizeof(BatchConstants);
        if(FAILED(device->CreateBlendState(&b,&blend)) || FAILED(device->CreateBlendState(&ba,&blendStockAlpha)) ||
           FAILED(device->CreateBlendState(&bd,&blendStockAdd)) || FAILED(device->CreateDepthStencilState(&ds,&noDepth)) ||
           FAILED(device->CreateRasterizerState(&r,&raster)) || FAILED(device->CreateSamplerState(&s,&sampler)) ||
           FAILED(device->CreateBuffer(&pb,nullptr,&particles)) || FAILED(device->CreateBuffer(&fb,nullptr,&frameCb)) ||
           FAILED(device->CreateBuffer(&bb,nullptr,&batchCb)))return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC pv{};pv.Format=DXGI_FORMAT_UNKNOWN;pv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;
        pv.Buffer.FirstElement=0;pv.Buffer.NumElements=kMaxInstances;
        if(FAILED(device->CreateShaderResourceView(particles.Get(),&pv,&particleView)))return false;
        D3D11_QUERY_DESC qd{};
        for(auto& t:timing) {
            qd.Query=D3D11_QUERY_TIMESTAMP_DISJOINT;
            if(FAILED(device->CreateQuery(&qd,&t.disjoint)))return false;
            qd.Query=D3D11_QUERY_TIMESTAMP;
            if(FAILED(device->CreateQuery(&qd,&t.begin)) || FAILED(device->CreateQuery(&qd,&t.end)))return false;
        }
        order.resize(kMaxInstances);keys.resize(kMaxInstances);
        context=value;
        return true;
    }

    bool PrepareDepth(const D3D11_TEXTURE2D_DESC& desc,DXGI_FORMAT viewFormat) noexcept {
        if(depthCopy && depthDesc.Width==desc.Width && depthDesc.Height==desc.Height && depthViewFormat==viewFormat)return true;
        depthCopy.Reset();depthView.Reset();
        DXGI_FORMAT resource=DXGI_FORMAT_UNKNOWN,shader=DXGI_FORMAT_UNKNOWN;
        if(!DepthFormats(viewFormat,resource,shader))return false;
        D3D11_TEXTURE2D_DESC d{};d.Width=desc.Width;d.Height=desc.Height;d.MipLevels=d.ArraySize=1;d.Format=resource;
        d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};v.Format=shader;v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&depthCopy)) ||
           FAILED(device->CreateShaderResourceView(depthCopy.Get(),&v,&depthView))) {depthCopy.Reset();depthView.Reset();return false;}
        depthDesc=desc;depthViewFormat=viewFormat;
        return true;
    }

    void ResolveTiming(ID3D11DeviceContext* ctx) noexcept {
        for(auto& t:timing) {
            if(!t.pending)continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
            UINT64 a=0,b=0;
            if(ctx->GetData(t.disjoint.Get(),&dj,sizeof(dj),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK ||
               ctx->GetData(t.begin.Get(),&a,sizeof(a),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK ||
               ctx->GetData(t.end.Get(),&b,sizeof(b),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK)continue;
            t.pending=false;
            if(!dj.Disjoint && dj.Frequency && b>=a)gpuMs=static_cast<float>(static_cast<double>(b-a)*1000.0/static_cast<double>(dj.Frequency));
        }
    }

    static float Half(std::uint16_t h) noexcept {
        const std::uint32_t sign=(h&0x8000u)<<16,exp=(h>>10)&0x1Fu,man=h&0x3FFu;
        std::uint32_t bits;
        if(exp==0)return (sign ? -1.0f : 1.0f)*static_cast<float>(man)*(1.0f/16777216.0f);
        if(exp==31)bits=sign|0x7F800000u|(man<<13);
        else bits=sign|((exp+112u)<<23)|(man<<13);
        float f;std::memcpy(&f,&bits,sizeof(f));return f;
    }
    // Reads back finished luminance copies (never waits), then copies this frame's blocks if a ring slot is free.
    void SampleLuma(ID3D11DeviceContext* ctx,ID3D11Texture2D* color,const D3D11_TEXTURE2D_DESC& cd,const D3D11_VIEWPORT& vp) noexcept {
        if(cd.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT && cd.Format!=DXGI_FORMAT_R16G16B16A16_TYPELESS)return;
        if(lumaFormat!=cd.Format){for(auto& l:luma){l.tex.Reset();l.pending=false;}lumaFormat=cd.Format;}
        for(auto& l:luma) {
            if(!l.pending)continue;
            D3D11_MAPPED_SUBRESOURCE m{};
            if(ctx->Map(l.tex.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&m)!=S_OK)continue;
            float block[kLumaBlocks];
            for(int k=0;k<kLumaBlocks;++k) {
                double sum=0.0;
                for(int y=0;y<kLumaBlock;++y) {
                    const auto* row=reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(m.pData)+static_cast<std::size_t>(y)*m.RowPitch)+
                        static_cast<std::size_t>(k)*kLumaBlock*4;
                    for(int x=0;x<kLumaBlock;++x) {
                        const float r=Half(row[x*4]),g=Half(row[x*4+1]),b=Half(row[x*4+2]);
                        const float lum=0.2126f*r+0.7152f*g+0.0722f*b;
                        sum+=std::isfinite(lum) && lum>0.0f ? lum : 0.0;
                    }
                }
                block[k]=static_cast<float>(sum/(kLumaBlock*kLumaBlock));
            }
            ctx->Unmap(l.tex.Get(),0);
            l.pending=false;
            std::sort(block,block+kLumaBlocks);
            sceneLuma=block[kLumaBlocks/2];sceneLumaMax=block[kLumaBlocks-1];
        }
        LumaRead& l=luma[static_cast<std::size_t>(lumaNext)];
        if(l.pending)return;
        if(!l.tex) {
            D3D11_TEXTURE2D_DESC d{};d.Width=kLumaBlock*kLumaBlocks;d.Height=kLumaBlock;d.MipLevels=d.ArraySize=1;d.Format=cd.Format;
            d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            if(FAILED(device->CreateTexture2D(&d,nullptr,&l.tex)))return;
        }
        // Centre, left, right, above and below the centre of the view's rectangle (sky and ground alike: the median).
        const float fx[kLumaBlocks]={0.5f,0.25f,0.75f,0.5f,0.5f},fy[kLumaBlocks]={0.5f,0.55f,0.55f,0.3f,0.75f};
        for(int k=0;k<kLumaBlocks;++k) {
            const float cx=vp.TopLeftX+vp.Width*fx[k],cy=vp.TopLeftY+vp.Height*fy[k];
            const UINT x0=static_cast<UINT>((std::max)(0.0f,(std::min)(cx-kLumaBlock*0.5f,static_cast<float>(cd.Width-kLumaBlock))));
            const UINT y0=static_cast<UINT>((std::max)(0.0f,(std::min)(cy-kLumaBlock*0.5f,static_cast<float>(cd.Height-kLumaBlock))));
            const D3D11_BOX box{x0,y0,0,x0+kLumaBlock,y0+kLumaBlock,1};
            ctx->CopySubresourceRegion(l.tex.Get(),0,static_cast<UINT>(k*kLumaBlock),0,0,color,0,&box);
        }
        l.pending=true;
        lumaNext=(lumaNext+1)%kLumaRing;
    }

    // The flipbook texture with its mips (the tiles keep 2 px of clear border: mips 0..4 never bleed between frames).
    void PrepareFlipbook(ID3D11DeviceContext* ctx) noexcept {
        if(flipView || flipPixels.empty())return;
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=kFlipSide;d.MipLevels=5;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        d.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&flip)) || FAILED(device->CreateShaderResourceView(flip.Get(),nullptr,&flipView))) {
            flip.Reset();flipView.Reset();flipPixels.clear();return;
        }
        ctx->UpdateSubresource(flip.Get(),0,nullptr,flipPixels.data(),kFlipSide*4,0);
        ctx->GenerateMips(flipView.Get());
        flipPixels.clear();flipPixels.shrink_to_fit();
    }
};

Renderer::Renderer() noexcept=default;
Renderer::~Renderer() noexcept=default;

void Renderer::SetFlipbook(const std::uint8_t* rgba) noexcept {
    if(!rgba)return;
    if(!impl_)impl_.reset(new(std::nothrow) Impl);
    if(!impl_ || impl_->flipView)return;
    try { impl_->flipPixels.assign(rgba,rgba+static_cast<std::size_t>(kFlipSide)*kFlipSide*4); }
    catch(...) { impl_->flipPixels.clear(); }
}

void Renderer::Reset() noexcept {
    // The flipbook's pixels survive (they are the plugin's, not the device's): kept for the next device.
    std::vector<std::uint8_t> keep;
    if(impl_ && impl_->flipView==nullptr) keep.swap(impl_->flipPixels);
    impl_.reset();
    if(!keep.empty()) {
        impl_.reset(new(std::nothrow) Impl);
        if(impl_)impl_->flipPixels.swap(keep);
    }
}

bool Renderer::Draw(ID3D11DeviceContext* context,ID3D11Texture2D* depth,unsigned depthViewFormat,const ViewParams& view,
                    const Batch* batches,int count,Stats* stats) noexcept {
    Stats local{};
    Stats& st=stats ? *stats : local;
    st=Stats{};
    auto refuse=[&st](const char* why){st.refused=why;return false;};
    if(!context || !depth)return refuse("no context or depth");
    if(!impl_)impl_.reset(new(std::nothrow) Impl);
    if(!impl_)return refuse("out of memory");
    Impl& s=*impl_;
    if(s.failed)return refuse("initialisation failed earlier");
    if(!s.device) {
        if(!s.Initialize(context)){s.failed=true;return refuse("initialisation failed");}
    } else if(s.context.Get()!=context) {
        return refuse("another device context");
    }
    // The bound colour target: exactly one 2D single-sample RTV.
    std::array<ID3D11RenderTargetView*,kTargets> raw{};
    ComPtr<ID3D11DepthStencilView> boundDepth;
    context->OMGetRenderTargets(kTargets,raw.data(),&boundDepth);
    std::array<ComPtr<ID3D11RenderTargetView>,kTargets> targets;
    for(UINT i=0;i<kTargets;++i)targets[i].Attach(raw[i]);
    if(!targets[0])return refuse("no colour target bound");
    for(UINT i=1;i<kTargets;++i)if(targets[i])return refuse("several colour targets bound");
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    targets[0]->GetDesc(&rtvDesc);
    ComPtr<ID3D11Resource> colorResource;
    ComPtr<ID3D11Texture2D> color;
    targets[0]->GetResource(&colorResource);
    if(FAILED(colorResource.As(&color)))return refuse("colour target is not a 2D texture");
    D3D11_TEXTURE2D_DESC cd{};color->GetDesc(&cd);
    D3D11_TEXTURE2D_DESC dd{};depth->GetDesc(&dd);
    st.width=cd.Width;st.height=cd.Height;st.colorFormat=static_cast<unsigned>(rtvDesc.Format);
    st.depthFormat=depthViewFormat;st.samples=cd.SampleDesc.Count;
    if(rtvDesc.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || cd.SampleDesc.Count!=1)return refuse("colour target multisampled or not 2D");
    if(dd.SampleDesc.Count!=1 || dd.MipLevels!=1 || dd.ArraySize!=1)return refuse("depth multisampled or layered");
    if(dd.Width!=cd.Width || dd.Height!=cd.Height)return refuse("depth and colour differ in size");
    if(!s.PrepareDepth(dd,static_cast<DXGI_FORMAT>(depthViewFormat)))return refuse("depth format unsupported");
    UINT viewports=1;
    D3D11_VIEWPORT vp{};
    context->RSGetViewports(&viewports,&vp);
    if(viewports==0 || !(vp.Width>0.0f) || !(vp.Height>0.0f))
        vp=D3D11_VIEWPORT{0.0f,0.0f,static_cast<float>(cd.Width),static_cast<float>(cd.Height),0.0f,1.0f};
    st.viewport[0]=vp.TopLeftX;st.viewport[1]=vp.TopLeftY;st.viewport[2]=vp.Width;st.viewport[3]=vp.Height;
    s.ResolveTiming(context);
    st.gpuMs=s.gpuMs;
    if(cd.Width>=Impl::kLumaBlock && cd.Height>=Impl::kLumaBlock)s.SampleLuma(context,color.Get(),cd,vp);
    st.sceneLuma=s.sceneLuma;st.sceneLumaMax=s.sceneLumaMax;

    // Upload every batch, each sorted back to front under this view.
    st.lookNegZ=view.proj[11]<0.0f;
    st.reversed=ReversedDepth(view.proj,st.lookNegZ);
    struct Range { int base,count; };
    std::array<Range,kMaxBatches> ranges{};
    if(count>static_cast<int>(ranges.size()))count=static_cast<int>(ranges.size());
    D3D11_MAPPED_SUBRESOURCE map{};
    if(FAILED(context->Map(s.particles.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map)))return refuse("particle buffer map failed");
    auto* out=static_cast<Instance*>(map.pData);
    int total=0;
    for(int b=0;b<count;++b) {
        const Batch& batch=batches[b];
        int n=batch.instances ? batch.count : 0;
        if(n>kMaxInstances-total)n=kMaxInstances-total;
        if(n<0)n=0;
        ranges[b]={total,n};
        if(n==0)continue;
        if(batch.sorted) {
            SortBackToFront(batch.instances,n,view.view,st.lookNegZ,s.order.data(),s.keys.data());
            for(int i=0;i<n;++i)out[total+i]=batch.instances[s.order[i]];
        } else {
            std::memcpy(out+total,batch.instances,sizeof(Instance)*static_cast<std::size_t>(n));
        }
        total+=n;
    }
    context->Unmap(s.particles.Get(),0);

    FrameConstants f{};
    std::memcpy(f.view,view.view,sizeof(f.view));std::memcpy(f.proj,view.proj,sizeof(f.proj));
    float sx=view.sunWorld[0],sy=view.sunWorld[1],sz=view.sunWorld[2];
    // Direction into view space (w = 0: rotation only), normalised.
    float vx=sx*view.view[0]+sy*view.view[4]+sz*view.view[8];
    float vy=sx*view.view[1]+sy*view.view[5]+sz*view.view[9];
    float vz=sx*view.view[2]+sy*view.view[6]+sz*view.view[10];
    const float len=std::sqrt(vx*vx+vy*vy+vz*vz);
    if(len>1e-6f){vx/=len;vy/=len;vz/=len;}
    f.sun[0]=vx;f.sun[1]=vy;f.sun[2]=vz;
    std::memcpy(f.sunColor,view.sunColor,12);f.sunColor[3]=view.exposure;
    std::memcpy(f.ambient,view.ambient,12);f.ambient[3]=1.5f;
    f.fade[0]=view.fadeMul;f.fade[1]=view.fadeAdd;
    f.screen[0]=static_cast<float>(cd.Width);f.screen[1]=static_cast<float>(cd.Height);
    f.screen[2]=1.0f/f.screen[0];f.screen[3]=1.0f/f.screen[1];
    if(FAILED(context->Map(s.frameCb.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map)))return refuse("constant map failed");
    std::memcpy(map.pData,&f,sizeof(f));
    context->Unmap(s.frameCb.Get(),0);

    Impl::Timing& timing=s.timing[static_cast<std::size_t>(s.timingNext)];
    const bool timed=!timing.pending;
    if(timed) {
        context->Begin(timing.disjoint.Get());
        context->End(timing.begin.Get());
    }
    {
        ScopedContextState restore(s.context1.Get(),s.isolated.Get());
        context->ClearState();
        s.PrepareFlipbook(context);
        context->CopyResource(s.depthCopy.Get(),depth);
        ID3D11RenderTargetView* target=targets[0].Get();
        context->OMSetRenderTargets(1,&target,nullptr);
        context->OMSetBlendState(s.blend.Get(),nullptr,0xFFFFFFFFu);
        context->OMSetDepthStencilState(s.noDepth.Get(),0);
        context->RSSetState(s.raster.Get());
        context->RSSetViewports(1,&vp);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->VSSetShader(s.vs.Get(),nullptr,0);
        context->PSSetShader(s.ps.Get(),nullptr,0);
        ID3D11Buffer* cbs[]={s.frameCb.Get(),s.batchCb.Get()};
        context->VSSetConstantBuffers(0,2,cbs);
        context->PSSetConstantBuffers(0,2,cbs);
        ID3D11ShaderResourceView* pv=s.particleView.Get();
        context->VSSetShaderResources(0,1,&pv);
        ID3D11ShaderResourceView* dv=s.depthView.Get();
        context->PSSetShaderResources(2,1,&dv);
        ID3D11SamplerState* sm=s.sampler.Get();
        context->PSSetSamplers(0,1,&sm);
        // The stock lighting, for the lit batches: borrowed buffers and views, bound read-only.
        const StockLights& L=view.lights;
        const bool litBound=L.Complete();
        if(litBound) {
            ID3D11Buffer* lcb[]={L.system,L.shadow};
            context->VSSetConstantBuffers(2,2,lcb);
            context->PSSetConstantBuffers(2,2,lcb);
            ID3D11ShaderResourceView* lsrv[]={L.shadowMap,L.points,L.masks[0],L.masks[1],L.masks[2]};
            context->PSSetShaderResources(3,5,lsrv);
        }
        for(int b=0;b<count;++b) {
            const Batch& batch=batches[b];
            if(ranges[b].count==0)continue;
            ID3D11ShaderResourceView* tex=batch.texture ? batch.texture : s.flipView.Get();
            if(!tex)continue;
            BatchConstants bc{};
            bc.base=static_cast<std::uint32_t>(ranges[b].base);
            bc.gridX=static_cast<std::uint32_t>(batch.gridX>0 ? batch.gridX : 1);
            bc.gridY=static_cast<std::uint32_t>(batch.gridY>0 ? batch.gridY : 1);
            bc.frames=static_cast<std::uint32_t>(batch.frames>0 ? batch.frames : 1);
            bc.kind=static_cast<std::uint32_t>(batch.kind);bc.cornerScale=batch.cornerScale;bc.uInset=batch.uInset;
            const bool lit=batch.kind==Kind::nativeLitAlpha || batch.kind==Kind::nativeLitAdditive;
            if(lit && !litBound)continue;
            bc.litSet=static_cast<std::uint32_t>(batch.litSet);std::memcpy(bc.litWeights,batch.litWeights,sizeof(batch.litWeights));
            if(batch.kind==Kind::smoke)bc.litSet=litBound ? 1u : 0u;   // ours lit by the stock lighting when captured
            if(FAILED(context->Map(s.batchCb.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map)))continue;
            std::memcpy(map.pData,&bc,sizeof(bc));
            context->Unmap(s.batchCb.Get(),0);
            context->PSSetShaderResources(1,1,&tex);
            ID3D11BlendState* bs=batch.kind==Kind::nativeAlpha || batch.kind==Kind::nativeLitAlpha ? s.blendStockAlpha.Get() :
                batch.kind==Kind::nativeAdditive || batch.kind==Kind::nativeLitAdditive ? s.blendStockAdd.Get() : s.blend.Get();
            context->OMSetBlendState(bs,nullptr,0xFFFFFFFFu);
            context->DrawInstanced(4,static_cast<UINT>(ranges[b].count),0,0);
            st.drawn+=ranges[b].count;++st.batches;
        }
        // Nothing of the native frame stays referenced by the private state.
        context->ClearState();
    }
    if(timed) {
        context->End(timing.end.Get());
        context->End(timing.disjoint.Get());
        timing.pending=true;
        s.timingNext=(s.timingNext+1)%Impl::kQueries;
    }
    return true;
}

}  // namespace crew::gpufx
