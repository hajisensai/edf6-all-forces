#include "thermal_gpu.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cstring>
#include <new>
#include <utility>

namespace crew::thermal {
namespace {

using Microsoft::WRL::ComPtr;
constexpr UINT kTargets = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
constexpr UINT kInterfaces = D3D11_SHADER_MAX_INTERFACES;

constexpr char kShaders[] = R"hlsl(
// This PS consumes only the native rasterizer's SV_Position. Native transforms
// and clipping therefore produce the same pixels as the original opaque draw.
float2 Capture(float4 position : SV_Position) : SV_Target0 {
    return float2(position.z, position.z);
}

float4 Fullscreen(uint vertex : SV_VertexID) : SV_Position {
    float2 p = float2((vertex << 1) & 2, vertex & 2);
    return float4(p * 2.0 - 1.0, 0.0, 1.0);
}

Texture2D<float4> Scene : register(t0);
Texture2D<float2> Heat : register(t1);
Texture2D<float> Depth : register(t2);

float4 Shade(float4 position, bool blackHot, bool depth16, bool depth24) {
    int3 pixel = int3(int2(position.xy), 0);
    float4 scene = Scene.Load(pixel);
    float2 heat = Heat.Load(pixel);
    float depth = Depth.Load(pixel);
    // Compare exact UNORM codes, avoiding a second normalization whose float
    // rounding can differ from the SRV's integer-to-normalized conversion.
    if (depth16) {
        heat.y = round(saturate(heat.y) * 65535.0);
        depth = round(saturate(depth) * 65535.0);
    } else if (depth24) {
        heat.y = round(saturate(heat.y) * 16777215.0);
        depth = round(saturate(depth) * 16777215.0);
    }
    float value = 0.1 + 0.35 * saturate(dot(scene.rgb, float3(0.2126, 0.7152, 0.0722)));
    // Compare the native stored representation, including reversed depth. A
    // perspective-depth epsilon would admit distant, nearer occluders. Prefer
    // a conservative missed hot pixel over admitting a different depth value.
    if (heat.x >= 0.0 && heat.x <= 1.0 && heat.y == depth)
        value = 0.95;
    if (blackHot)
        value = 1.0 - value;
    return float4(value, value, value, scene.a);
}

float4 WhiteHot(float4 position : SV_Position) : SV_Target0 {
    return Shade(position, false, false, false);
}

float4 BlackHot(float4 position : SV_Position) : SV_Target0 {
    return Shade(position, true, false, false);
}

float4 WhiteHot16(float4 position : SV_Position) : SV_Target0 {
    return Shade(position, false, true, false);
}

float4 BlackHot16(float4 position : SV_Position) : SV_Target0 {
    return Shade(position, true, true, false);
}

float4 WhiteHot24(float4 position : SV_Position) : SV_Target0 {
    return Shade(position, false, false, true);
}

float4 BlackHot24(float4 position : SV_Position) : SV_Target0 {
    return Shade(position, true, false, true);
}
)hlsl";

using CompileFunction = HRESULT (WINAPI*)(LPCVOID, SIZE_T, LPCSTR,
    const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
    ID3DBlob**, ID3DBlob**);

bool SimpleTexture(const D3D11_TEXTURE2D_DESC& desc) noexcept {
    return desc.Width != 0 && desc.Height != 0 && desc.MipLevels == 1 &&
        desc.ArraySize == 1 && desc.SampleDesc.Count == 1 &&
        desc.SampleDesc.Quality == 0;
}

bool DepthFormats(DXGI_FORMAT view, DXGI_FORMAT& resource,
                  DXGI_FORMAT& shader) noexcept {
    switch (view) {
    case DXGI_FORMAT_D16_UNORM:
        resource = DXGI_FORMAT_R16_TYPELESS;
        shader = DXGI_FORMAT_R16_UNORM;
        return true;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        resource = DXGI_FORMAT_R24G8_TYPELESS;
        shader = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        return true;
    case DXGI_FORMAT_D32_FLOAT:
        resource = DXGI_FORMAT_R32_TYPELESS;
        shader = DXGI_FORMAT_R32_FLOAT;
        return true;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        resource = DXGI_FORMAT_R32G8X24_TYPELESS;
        shader = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        return true;
    default:
        return false;
    }
}

bool ColorFormat(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return true;
    default:
        return false;
    }
}

bool FullViewport(ID3D11DeviceContext* context, UINT width, UINT height) noexcept {
    std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> views{};
    UINT count = static_cast<UINT>(views.size());
    context->RSGetViewports(&count, views.data());
    return count == 1 && views[0].TopLeftX == 0.0f && views[0].TopLeftY == 0.0f &&
        views[0].Width == static_cast<float>(width) &&
        views[0].Height == static_cast<float>(height) &&
        views[0].MinDepth == 0.0f && views[0].MaxDepth == 1.0f;
}

void ClearMask(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
               FLOAT value) noexcept {
    // Predication gates Clear as well as Draw. A skipped private clear would
    // expose pixels left by an earlier view even if this view has no enemies.
    ComPtr<ID3D11Predicate> predicate;
    BOOL predicateValue = FALSE;
    context->GetPredication(&predicate, &predicateValue);
    context->SetPredication(nullptr, FALSE);
    const FLOAT clear[4]{value, value, value, value};
    context->ClearRenderTargetView(target, clear);
    context->SetPredication(predicate.Get(), predicateValue);
}

// Capture changes only OM RTVs/depth/blend and the PS object. UAV bindings are
// deliberately kept: RTV0 must already be bound, so replacing it cannot evict a
// native UAV in slot 0; the private mask cannot alias any native resource.
struct CaptureState final {
    ID3D11DeviceContext* context;
    std::array<ID3D11RenderTargetView*, kTargets> targets{};
    ID3D11DepthStencilView* depth = nullptr;
    ID3D11DepthStencilState* depthState = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11PixelShader* shader = nullptr;
    std::array<ID3D11ClassInstance*, kInterfaces> instances{};
    UINT instanceCount = kInterfaces;
    UINT stencil = 0;
    UINT sampleMask = 0;
    FLOAT factors[4]{};
    UINT targetCount = 0;
    bool changed = false;

    explicit CaptureState(ID3D11DeviceContext* value) noexcept : context(value) {
        context->OMGetRenderTargets(kTargets, targets.data(), &depth);
        context->OMGetDepthStencilState(&depthState, &stencil);
        context->OMGetBlendState(&blend, factors, &sampleMask);
        context->PSGetShader(&shader, instances.data(), &instanceCount);
        for (UINT i = 0; i < kTargets; ++i)
            if (targets[i]) targetCount = i + 1;
    }

    ~CaptureState() noexcept {
        if (changed) {
            context->OMSetRenderTargetsAndUnorderedAccessViews(targetCount,
                targets.data(), depth, 0, D3D11_KEEP_UNORDERED_ACCESS_VIEWS, nullptr, nullptr);
            context->OMSetDepthStencilState(depthState, stencil);
            context->OMSetBlendState(blend, factors, sampleMask);
            context->PSSetShader(shader, instances.data(), instanceCount);
        }
        for (auto* target : targets) if (target) target->Release();
        for (auto* instance : instances) if (instance) instance->Release();
        if (depth) depth->Release();
        if (depthState) depthState->Release();
        if (blend) blend->Release();
        if (shader) shader->Release();
    }

    CaptureState(const CaptureState&) = delete;
    CaptureState& operator=(const CaptureState&) = delete;
};

struct ScopedContextState final {
    ID3D11DeviceContext1* context;
    ComPtr<ID3DDeviceContextState> previous;
    ScopedContextState(ID3D11DeviceContext1* value, ID3DDeviceContextState* state) noexcept
        : context(value) {
        context->SwapDeviceContextState(state, &previous);
    }
    ~ScopedContextState() noexcept {
        context->SwapDeviceContextState(previous.Get(), nullptr);
    }
    ScopedContextState(const ScopedContextState&) = delete;
    ScopedContextState& operator=(const ScopedContextState&) = delete;
};

} // namespace

struct Renderer::Impl final {
    enum class DepthConvention { unset, less, greater };
    HMODULE compilerModule = nullptr;
    CompileFunction compile = nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11DeviceContext1> context1;
    ComPtr<ID3DDeviceContextState> isolatedState;
    ComPtr<ID3D11PixelShader> captureShader;
    ComPtr<ID3D11VertexShader> fullscreenShader;
    ComPtr<ID3D11PixelShader> whiteShader;
    ComPtr<ID3D11PixelShader> blackShader;
    ComPtr<ID3D11PixelShader> whiteShader16;
    ComPtr<ID3D11PixelShader> blackShader16;
    ComPtr<ID3D11PixelShader> whiteShader24;
    ComPtr<ID3D11PixelShader> blackShader24;
    ComPtr<ID3D11BlendState> overwriteBlend;
    ComPtr<ID3D11BlendState> nearestMinBlend;
    ComPtr<ID3D11BlendState> nearestMaxBlend;
    ComPtr<ID3D11DepthStencilState> captureDepthState;
    D3D11_DEPTH_STENCIL_DESC captureDepthDesc{};
    ComPtr<ID3D11DepthStencilState> noDepthState;
    ComPtr<ID3D11RasterizerState> fullscreenRasterizer;

    ComPtr<ID3D11Texture2D> mask;
    ComPtr<ID3D11RenderTargetView> maskTarget;
    ComPtr<ID3D11ShaderResourceView> maskResource;
    ComPtr<ID3D11Texture2D> depthCopy;
    ComPtr<ID3D11ShaderResourceView> depthResource;
    ComPtr<ID3D11Texture2D> sceneDepth;
    ComPtr<ID3D11Texture2D> colorCopy;
    ComPtr<ID3D11ShaderResourceView> colorResource;
    D3D11_TEXTURE2D_DESC depthDesc{};
    DXGI_FORMAT depthViewFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT colorViewFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT colorTextureFormat = DXGI_FORMAT_UNKNOWN;
    std::uint64_t frameToken = 0;
    bool active = false;
    bool observed = false;
    bool provisionalObservation = false;
    DepthConvention depthConvention = DepthConvention::unset;

    ~Impl() noexcept {
        if (compilerModule) FreeLibrary(compilerModule);
    }

    void Invalidate() noexcept {
        active = false;
        observed = false;
        provisionalObservation = false;
        depthConvention = DepthConvention::unset;
        sceneDepth.Reset();
    }

    bool Compile(const char* entry, const char* profile, ComPtr<ID3DBlob>& blob) noexcept {
        ComPtr<ID3DBlob> errors;
        return SUCCEEDED(compile(kShaders, sizeof(kShaders) - 1, "thermal_gpu",
            nullptr, nullptr, entry, profile,
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS |
                D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0, &blob, &errors));
    }

    bool Initialize(ID3D11DeviceContext* value) noexcept {
        if (!value || value->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
        value->GetDevice(&device);
        if (!device || device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0 ||
            FAILED(device->GetDeviceRemovedReason()) ||
            FAILED(value->QueryInterface(IID_PPV_ARGS(&context1)))) return false;
        UINT maskSupport = 0;
        constexpr UINT requiredMaskSupport = D3D11_FORMAT_SUPPORT_TEXTURE2D |
            D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_BLENDABLE |
            D3D11_FORMAT_SUPPORT_SHADER_LOAD;
        if (FAILED(device->CheckFormatSupport(DXGI_FORMAT_R32G32_FLOAT, &maskSupport)) ||
            (maskSupport & requiredMaskSupport) != requiredMaskSupport) return false;
        ComPtr<ID3D11Device1> device1;
        if (FAILED(device.As(&device1))) return false;
        const D3D_FEATURE_LEVEL level = device->GetFeatureLevel();
        const UINT flags = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
            ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
        if (FAILED(device1->CreateDeviceContextState(flags, &level, 1,
            D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr, &isolatedState))) return false;

        compilerModule = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!compilerModule) return false;
        const FARPROC address = GetProcAddress(compilerModule, "D3DCompile");
        static_assert(sizeof(address) == sizeof(compile), "Windows function pointer size");
        std::memcpy(&compile, &address, sizeof(compile));
        if (!compile) return false;
        ComPtr<ID3DBlob> captureCode, fullscreenCode, whiteCode, blackCode;
        ComPtr<ID3DBlob> whiteCode16, blackCode16, whiteCode24, blackCode24;
        if (!Compile("Capture", "ps_5_0", captureCode) ||
            !Compile("Fullscreen", "vs_5_0", fullscreenCode) ||
            !Compile("WhiteHot", "ps_5_0", whiteCode) ||
            !Compile("BlackHot", "ps_5_0", blackCode) ||
            !Compile("WhiteHot16", "ps_5_0", whiteCode16) ||
            !Compile("BlackHot16", "ps_5_0", blackCode16) ||
            !Compile("WhiteHot24", "ps_5_0", whiteCode24) ||
            !Compile("BlackHot24", "ps_5_0", blackCode24)) return false;
        if (FAILED(device->CreatePixelShader(captureCode->GetBufferPointer(),
                captureCode->GetBufferSize(), nullptr, &captureShader)) ||
            FAILED(device->CreateVertexShader(fullscreenCode->GetBufferPointer(),
                fullscreenCode->GetBufferSize(), nullptr, &fullscreenShader)) ||
            FAILED(device->CreatePixelShader(whiteCode->GetBufferPointer(),
                whiteCode->GetBufferSize(), nullptr, &whiteShader)) ||
            FAILED(device->CreatePixelShader(blackCode->GetBufferPointer(),
                blackCode->GetBufferSize(), nullptr, &blackShader)) ||
            FAILED(device->CreatePixelShader(whiteCode16->GetBufferPointer(),
                whiteCode16->GetBufferSize(), nullptr, &whiteShader16)) ||
            FAILED(device->CreatePixelShader(blackCode16->GetBufferPointer(),
                blackCode16->GetBufferSize(), nullptr, &blackShader16)) ||
            FAILED(device->CreatePixelShader(whiteCode24->GetBufferPointer(),
                whiteCode24->GetBufferSize(), nullptr, &whiteShader24)) ||
            FAILED(device->CreatePixelShader(blackCode24->GetBufferPointer(),
                blackCode24->GetBufferSize(), nullptr, &blackShader24))) return false;
        D3D11_BLEND_DESC blendDesc{};
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device->CreateBlendState(&blendDesc, &overwriteBlend))) return false;
        auto& blendTarget = blendDesc.RenderTarget[0];
        blendTarget.BlendEnable = TRUE;
        blendTarget.SrcBlend = D3D11_BLEND_ONE;
        blendTarget.DestBlend = D3D11_BLEND_ONE;
        blendTarget.BlendOp = D3D11_BLEND_OP_MIN;
        blendTarget.SrcBlendAlpha = D3D11_BLEND_ONE;
        blendTarget.DestBlendAlpha = D3D11_BLEND_ONE;
        blendTarget.BlendOpAlpha = D3D11_BLEND_OP_MIN;
        if (FAILED(device->CreateBlendState(&blendDesc, &nearestMinBlend))) return false;
        blendTarget.BlendOp = D3D11_BLEND_OP_MAX;
        blendTarget.BlendOpAlpha = D3D11_BLEND_OP_MAX;
        if (FAILED(device->CreateBlendState(&blendDesc, &nearestMaxBlend))) return false;
        D3D11_DEPTH_STENCIL_DESC disabled{};
        disabled.DepthFunc = D3D11_COMPARISON_ALWAYS;
        if (FAILED(device->CreateDepthStencilState(&disabled, &noDepthState))) return false;
        D3D11_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode = D3D11_FILL_SOLID;
        rasterizer.CullMode = D3D11_CULL_NONE;
        rasterizer.DepthClipEnable = TRUE;
        if (FAILED(device->CreateRasterizerState(&rasterizer, &fullscreenRasterizer))) return false;
        context = value;
        return true;
    }

    bool Match(ID3D11DeviceContext* value) const noexcept {
        return active && value && value == context.Get() &&
            SUCCEEDED(device->GetDeviceRemovedReason());
    }

    bool PrepareDepth(const D3D11_TEXTURE2D_DESC& desc, DXGI_FORMAT format) noexcept {
        if (mask && depthDesc.Width == desc.Width && depthDesc.Height == desc.Height &&
            depthViewFormat == format) return true;
        DXGI_FORMAT resourceFormat = DXGI_FORMAT_UNKNOWN, shaderFormat = DXGI_FORMAT_UNKNOWN;
        if (!DepthFormats(format, resourceFormat, shaderFormat)) return false;
        D3D11_TEXTURE2D_DESC copyDesc = desc;
        copyDesc.Format = resourceFormat;
        copyDesc.Usage = D3D11_USAGE_DEFAULT;
        copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        copyDesc.CPUAccessFlags = 0;
        copyDesc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> nextDepth, nextMask;
        ComPtr<ID3D11ShaderResourceView> nextDepthResource, nextMaskResource;
        ComPtr<ID3D11RenderTargetView> nextMaskTarget;
        D3D11_SHADER_RESOURCE_VIEW_DESC resourceDesc{};
        resourceDesc.Format = shaderFormat;
        resourceDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        resourceDesc.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateTexture2D(&copyDesc, nullptr, &nextDepth)) ||
            FAILED(device->CreateShaderResourceView(nextDepth.Get(), &resourceDesc,
                &nextDepthResource))) return false;
        copyDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
        copyDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&copyDesc, nullptr, &nextMask)) ||
            FAILED(device->CreateRenderTargetView(nextMask.Get(), nullptr, &nextMaskTarget)) ||
            FAILED(device->CreateShaderResourceView(nextMask.Get(), nullptr,
                &nextMaskResource))) return false;
        depthCopy = std::move(nextDepth);
        depthResource = std::move(nextDepthResource);
        mask = std::move(nextMask);
        maskTarget = std::move(nextMaskTarget);
        maskResource = std::move(nextMaskResource);
        depthDesc = desc;
        depthViewFormat = format;
        colorCopy.Reset();
        colorResource.Reset();
        return true;
    }

    bool PrepareCaptureState(ID3D11DepthStencilState* nativeState,
                             DepthConvention& convention) noexcept {
        D3D11_DEPTH_STENCIL_DESC desc{};
        if (nativeState) nativeState->GetDesc(&desc);
        else {
            desc.DepthEnable = TRUE;
            desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            desc.DepthFunc = D3D11_COMPARISON_LESS;
            desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
            desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
            desc.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP,
                D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS};
            desc.BackFace = desc.FrontFace;
        }
        if (!desc.DepthEnable) return false;
        const auto conditionalStencil = [](D3D11_COMPARISON_FUNC function) noexcept {
            return function != D3D11_COMPARISON_ALWAYS && function != D3D11_COMPARISON_NEVER;
        };
        const auto changesStencil = [](const D3D11_DEPTH_STENCILOP_DESC& face) noexcept {
            return face.StencilFailOp != D3D11_STENCIL_OP_KEEP ||
                face.StencilDepthFailOp != D3D11_STENCIL_OP_KEEP ||
                face.StencilPassOp != D3D11_STENCIL_OP_KEEP;
        };
        // Even before the original draw, mutating conditional stencil can have
        // dependencies between overlapping primitives inside that one draw.
        // Unconditional marking passes (ALWAYS/NEVER) remain safe to replay.
        if (desc.StencilEnable && desc.StencilWriteMask != 0 &&
            (conditionalStencil(desc.FrontFace.StencilFunc) ||
                conditionalStencil(desc.BackFace.StencilFunc)) &&
            (changesStencil(desc.FrontFace) || changesStencil(desc.BackFace))) return false;
        switch (desc.DepthFunc) {
        case D3D11_COMPARISON_LESS:
        case D3D11_COMPARISON_LESS_EQUAL:
            convention = DepthConvention::less;
            break;
        case D3D11_COMPARISON_GREATER:
        case D3D11_COMPARISON_GREATER_EQUAL:
            convention = DepthConvention::greater;
            break;
        case D3D11_COMPARISON_EQUAL:
        case D3D11_COMPARISON_ALWAYS:
        case D3D11_COMPARISON_NEVER:
            convention = depthConvention == DepthConvention::unset
                ? DepthConvention::less : depthConvention;
            break;
        default:
            return false;
        }
        if (depthConvention != DepthConvention::unset && depthConvention != convention)
            return false;
        desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        desc.StencilWriteMask = 0;
        desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        desc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
        desc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
        desc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        if (captureDepthState && std::memcmp(&desc, &captureDepthDesc, sizeof(desc)) == 0)
            return true;
        ComPtr<ID3D11DepthStencilState> next;
        if (FAILED(device->CreateDepthStencilState(&desc, &next))) return false;
        captureDepthState = std::move(next);
        captureDepthDesc = desc;
        return true;
    }

    bool PrepareColor(const D3D11_TEXTURE2D_DESC& desc, DXGI_FORMAT format) noexcept {
        if (colorCopy && colorTextureFormat == desc.Format && colorViewFormat == format)
            return true;
        D3D11_TEXTURE2D_DESC copyDesc = desc;
        copyDesc.Usage = D3D11_USAGE_DEFAULT;
        copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        copyDesc.CPUAccessFlags = 0;
        copyDesc.MiscFlags = 0;
        D3D11_SHADER_RESOURCE_VIEW_DESC resourceDesc{};
        resourceDesc.Format = format;
        resourceDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        resourceDesc.Texture2D.MipLevels = 1;
        ComPtr<ID3D11Texture2D> nextColor;
        ComPtr<ID3D11ShaderResourceView> nextResource;
        if (FAILED(device->CreateTexture2D(&copyDesc, nullptr, &nextColor)) ||
            FAILED(device->CreateShaderResourceView(nextColor.Get(), &resourceDesc,
                &nextResource))) return false;
        colorCopy = std::move(nextColor);
        colorResource = std::move(nextResource);
        colorTextureFormat = desc.Format;
        colorViewFormat = format;
        return true;
    }
};

Renderer::Renderer() noexcept = default;
Renderer::~Renderer() noexcept = default;

bool Renderer::BeginView(ID3D11DeviceContext* context, std::uint64_t frameToken) noexcept {
    if (impl_) impl_->Invalidate();
    if (!context) return false;
    if (!impl_ || impl_->context.Get() != context ||
        FAILED(impl_->device->GetDeviceRemovedReason())) {
        auto next = std::unique_ptr<Impl>(new (std::nothrow) Impl());
        if (!next || !next->Initialize(context)) return false;
        impl_ = std::move(next);
    }
    impl_->frameToken = frameToken;
    impl_->active = true;
    // A view can begin before the renderer binds its G-buffer/depth surface.
    // Observation is opportunistic; the first subsequent opaque mesh retries it.
    if (ObserveSurface(context)) impl_->provisionalObservation = true;
    return true;
}

bool Renderer::ObserveSurface(ID3D11DeviceContext* context) noexcept {
    if (!impl_ || !impl_->Match(context)) {
        if (impl_) impl_->Invalidate();
        return false;
    }
    auto& state = *impl_;
    auto fail = [&state]() noexcept {
        if (state.observed) state.Invalidate();
        return false;
    };
    ComPtr<ID3D11DepthStencilView> depth;
    context->OMGetRenderTargets(0, nullptr, &depth);
    if (!depth) return fail();
    D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
    depth->GetDesc(&viewDesc);
    DXGI_FORMAT resourceFormat = DXGI_FORMAT_UNKNOWN, shaderFormat = DXGI_FORMAT_UNKNOWN;
    if (viewDesc.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D ||
        viewDesc.Texture2D.MipSlice != 0 ||
        !DepthFormats(viewDesc.Format, resourceFormat, shaderFormat)) return fail();
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Texture2D> texture;
    depth->GetResource(&resource);
    if (FAILED(resource.As(&texture))) return fail();
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (!SimpleTexture(desc)) return fail();
    if (state.observed) {
        if (state.sceneDepth.Get() == texture.Get() && state.depthViewFormat == viewDesc.Format) {
            state.provisionalObservation = false;
            return true;
        }
        // Begin can run before this camera binds its scene DSV. Its opportunistic
        // observation must not lock out the first authoritative mesh callback.
        if (!state.provisionalObservation) {
            state.Invalidate();
            return false;
        }
    }
    if (!state.PrepareDepth(desc, viewDesc.Format)) return fail();
    ClearMask(context, state.maskTarget.Get(), 2.0f);
    state.sceneDepth = texture;
    state.observed = true;
    state.provisionalObservation = false;
    return true;
}

bool Renderer::CaptureIndexed(ID3D11DeviceContext* context, unsigned count,
                             unsigned first, int base) noexcept {
    if (!impl_ || !impl_->Match(context)) {
        if (impl_) impl_->Invalidate();
        return false;
    }
    auto& state = *impl_;
    if (count == 0) return true;
    CaptureState native(context);
    auto fail = [&state]() noexcept { state.Invalidate(); return false; };
    if (!native.depth || !native.targets[0]) return fail();
    D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
    native.depth->GetDesc(&viewDesc);
    if (viewDesc.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D ||
        viewDesc.Texture2D.MipSlice != 0) return fail();
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Texture2D> texture;
    native.depth->GetResource(&resource);
    if (FAILED(resource.As(&texture))) return fail();
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (!SimpleTexture(desc) || !FullViewport(context, desc.Width, desc.Height)) return fail();
    if (state.observed && !state.provisionalObservation && (state.sceneDepth.Get() != texture.Get() ||
        state.depthViewFormat != viewDesc.Format)) return fail();
    std::array<ID3D11Buffer*, D3D11_SO_BUFFER_SLOT_COUNT> streamOutput{};
    context->SOGetTargets(static_cast<UINT>(streamOutput.size()), streamOutput.data());
    bool hasStreamOutput = false;
    for (auto* target : streamOutput) {
        if (target) { hasStreamOutput = true; target->Release(); }
    }
    Impl::DepthConvention convention = Impl::DepthConvention::unset;
    if (hasStreamOutput || !state.PrepareCaptureState(native.depthState, convention) ||
        !ObserveSurface(context)) return fail();

    // Every allocation/validation above completed before the first GPU write.
    if (state.depthConvention == Impl::DepthConvention::unset) {
        if (convention == Impl::DepthConvention::greater) {
            ClearMask(context, state.maskTarget.Get(), -1.0f);
        }
        state.depthConvention = convention;
    }
    native.changed = true;
    ID3D11RenderTargetView* target = state.maskTarget.Get();
    context->OMSetRenderTargetsAndUnorderedAccessViews(1, &target, native.depth,
        0, D3D11_KEEP_UNORDERED_ACCESS_VIEWS, nullptr, nullptr);
    context->OMSetBlendState(convention == Impl::DepthConvention::greater ?
        state.nearestMaxBlend.Get() : state.nearestMinBlend.Get(), nullptr, native.sampleMask);
    context->OMSetDepthStencilState(state.captureDepthState.Get(), native.stencil);
    context->PSSetShader(state.captureShader.Get(), nullptr, 0);
    context->DrawIndexed(count, first, base);
    return true;
}

bool Renderer::Composite(ID3D11DeviceContext* context, Polarity polarity) noexcept {
    if (!impl_ || !impl_->Match(context) || !impl_->observed) {
        if (impl_) impl_->Invalidate();
        return false;
    }
    auto& state = *impl_;
    auto fail = [&state]() noexcept { state.Invalidate(); return false; };
    if (polarity != Polarity::whiteHot && polarity != Polarity::blackHot) return fail();
    std::array<ComPtr<ID3D11RenderTargetView>, kTargets> targets;
    std::array<ID3D11RenderTargetView*, kTargets> rawTargets{};
    ComPtr<ID3D11DepthStencilView> boundDepth;
    context->OMGetRenderTargets(kTargets, rawTargets.data(), &boundDepth);
    for (UINT i = 0; i < kTargets; ++i) targets[i].Attach(rawTargets[i]);
    if (!targets[0]) return fail();
    for (UINT i = 1; i < kTargets; ++i) if (targets[i]) return fail();
    if (boundDepth) {
        ComPtr<ID3D11Resource> depth;
        boundDepth->GetResource(&depth);
        if (depth.Get() != static_cast<ID3D11Resource*>(state.sceneDepth.Get())) return fail();
    }
    D3D11_RENDER_TARGET_VIEW_DESC targetDesc{};
    targets[0]->GetDesc(&targetDesc);
    if (targetDesc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D ||
        targetDesc.Texture2D.MipSlice != 0 || !ColorFormat(targetDesc.Format)) return fail();
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Texture2D> color;
    targets[0]->GetResource(&resource);
    if (FAILED(resource.As(&color))) return fail();
    D3D11_TEXTURE2D_DESC desc{};
    color->GetDesc(&desc);
    if (!SimpleTexture(desc) || desc.Width != state.depthDesc.Width ||
        desc.Height != state.depthDesc.Height ||
        !FullViewport(context, desc.Width, desc.Height) ||
        !state.PrepareColor(desc, targetDesc.Format)) return fail();

    // Isolate pipeline bindings, including shader CB ranges, predication, UAVs
    // and stream output offsets. Native UAV counters are never touched. Active
    // queries are not isolated: the header makes that a call-site constraint.
    ScopedContextState restore(state.context1.Get(), state.isolatedState.Get());
    context->ClearState();
    context->CopyResource(state.colorCopy.Get(), color.Get());
    context->CopyResource(state.depthCopy.Get(), state.sceneDepth.Get());
    ID3D11RenderTargetView* target = targets[0].Get();
    context->OMSetRenderTargets(1, &target, nullptr);
    context->OMSetBlendState(state.overwriteBlend.Get(), nullptr, 0xFFFFFFFFu);
    context->OMSetDepthStencilState(state.noDepthState.Get(), 0);
    context->RSSetState(state.fullscreenRasterizer.Get());
    const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(desc.Width),
        static_cast<float>(desc.Height), 0.0f, 1.0f};
    context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(state.fullscreenShader.Get(), nullptr, 0);
    ID3D11PixelShader* compositeShader = nullptr;
    if (state.depthViewFormat == DXGI_FORMAT_D16_UNORM)
        compositeShader = polarity == Polarity::whiteHot ? state.whiteShader16.Get() : state.blackShader16.Get();
    else if (state.depthViewFormat == DXGI_FORMAT_D24_UNORM_S8_UINT)
        compositeShader = polarity == Polarity::whiteHot ? state.whiteShader24.Get() : state.blackShader24.Get();
    else
        compositeShader = polarity == Polarity::whiteHot ? state.whiteShader.Get() : state.blackShader.Get();
    context->PSSetShader(compositeShader, nullptr, 0);
    ID3D11ShaderResourceView* inputs[] = {state.colorResource.Get(),
        state.maskResource.Get(), state.depthResource.Get()};
    context->PSSetShaderResources(0, static_cast<UINT>(std::size(inputs)), inputs);
    context->Draw(3, 0);
    // Drop isolated bindings before returning to native state. In particular no
    // old scene target/depth resource stays retained in the private context state.
    context->ClearState();
    state.Invalidate();
    return true;
}

void Renderer::Reset() noexcept {
    impl_.reset();
}

} // namespace crew::thermal
