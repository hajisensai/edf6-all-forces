#include "scope_pip_gpu.h"
#include "scope_pip.h"

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

namespace crew::scopepip {
namespace {

using Microsoft::WRL::ComPtr;
constexpr UINT kTargets = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;

// The lens: scope_pip.h Source() on the GPU. A pixel outside the circle is discarded (the scissor keeps the pass to
// the circle's square); inside, the frame's copy is sampled bilinearly at the centre plus the offset over the
// magnification. Alpha comes from the same sample: whatever the native target keeps there stays consistent.
constexpr char kShaders[] = R"hlsl(
cbuffer LensBlock : register(b0) {
    float2 Centre;
    float Radius;
    float InverseMagnification;
    float2 InverseSize;
    float2 Unused;
};

Texture2D<float4> Scene : register(t0);
SamplerState Linear : register(s0);

float4 Fullscreen(uint vertex : SV_VertexID) : SV_Position {
    float2 p = float2((vertex << 1) & 2, vertex & 2);
    return float4(p * 2.0 - 1.0, 0.0, 1.0);
}

float4 Lens(float4 position : SV_Position) : SV_Target0 {
    float2 offset = position.xy - Centre;
    if (dot(offset, offset) > Radius * Radius)
        discard;
    float2 source = Centre + offset * InverseMagnification;
    return Scene.SampleLevel(Linear, source * InverseSize, 0.0);
}
)hlsl";

struct LensBlock {
    float centre[2];
    float radius;
    float inverseMagnification;
    float inverseSize[2];
    float unused[2];
};
static_assert(sizeof(LensBlock) == 32, "a constant buffer is a multiple of 16 bytes");

using CompileFunction = HRESULT (WINAPI*)(LPCVOID, SIZE_T, LPCSTR,
    const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
    ID3DBlob**, ID3DBlob**);

bool SimpleTexture(const D3D11_TEXTURE2D_DESC& desc) noexcept {
    return desc.Width != 0 && desc.Height != 0 && desc.MipLevels == 1 &&
        desc.ArraySize == 1 && desc.SampleDesc.Count == 1 &&
        desc.SampleDesc.Quality == 0;
}

// Formats a world colour target can have that a Texture2D<float4> samples with a linear filter.
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
        views[0].Height == static_cast<float>(height);
}

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

struct Magnifier::Impl final {
    HMODULE compilerModule = nullptr;
    CompileFunction compile = nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11DeviceContext1> context1;
    ComPtr<ID3DDeviceContextState> isolatedState;
    ComPtr<ID3D11VertexShader> fullscreenShader;
    ComPtr<ID3D11PixelShader> lensShader;
    ComPtr<ID3D11Buffer> lensBlock;
    ComPtr<ID3D11SamplerState> linearSampler;
    ComPtr<ID3D11BlendState> overwriteBlend;
    ComPtr<ID3D11DepthStencilState> noDepthState;
    ComPtr<ID3D11RasterizerState> scissorRasterizer;
    ComPtr<ID3D11Texture2D> colorCopy;
    ComPtr<ID3D11ShaderResourceView> colorResource;
    D3D11_TEXTURE2D_DESC colorDesc{};
    DXGI_FORMAT colorViewFormat = DXGI_FORMAT_UNKNOWN;

    ~Impl() noexcept {
        if (compilerModule) FreeLibrary(compilerModule);
    }

    bool Compile(const char* entry, const char* profile, ComPtr<ID3DBlob>& blob) noexcept {
        ComPtr<ID3DBlob> errors;
        return SUCCEEDED(compile(kShaders, sizeof(kShaders) - 1, "scope_pip_gpu",
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
        ComPtr<ID3DBlob> fullscreenCode, lensCode;
        if (!Compile("Fullscreen", "vs_5_0", fullscreenCode) || !Compile("Lens", "ps_5_0", lensCode)) return false;
        if (FAILED(device->CreateVertexShader(fullscreenCode->GetBufferPointer(),
                fullscreenCode->GetBufferSize(), nullptr, &fullscreenShader)) ||
            FAILED(device->CreatePixelShader(lensCode->GetBufferPointer(),
                lensCode->GetBufferSize(), nullptr, &lensShader))) return false;

        D3D11_BUFFER_DESC blockDesc{};
        blockDesc.ByteWidth = sizeof(LensBlock);
        blockDesc.Usage = D3D11_USAGE_DEFAULT;
        blockDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_BLEND_DESC blendDesc{};
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = FALSE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;
        rasterDesc.DepthClipEnable = TRUE;
        rasterDesc.ScissorEnable = TRUE;
        return SUCCEEDED(device->CreateBuffer(&blockDesc, nullptr, &lensBlock)) &&
            SUCCEEDED(device->CreateSamplerState(&samplerDesc, &linearSampler)) &&
            SUCCEEDED(device->CreateBlendState(&blendDesc, &overwriteBlend)) &&
            SUCCEEDED(device->CreateDepthStencilState(&depthDesc, &noDepthState)) &&
            SUCCEEDED(device->CreateRasterizerState(&rasterDesc, &scissorRasterizer));
    }

    // The frame's private copy: the target's own size and format (CopyResource needs both), sampled through the RTV's
    // format (a typeless target's typed view).
    bool PrepareColor(const D3D11_TEXTURE2D_DESC& desc, DXGI_FORMAT format) noexcept {
        if (colorCopy && colorDesc.Width == desc.Width && colorDesc.Height == desc.Height &&
            colorDesc.Format == desc.Format && colorViewFormat == format) return true;
        colorCopy.Reset();
        colorResource.Reset();
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
            FAILED(device->CreateShaderResourceView(nextColor.Get(), &resourceDesc, &nextResource))) return false;
        colorCopy = std::move(nextColor);
        colorResource = std::move(nextResource);
        colorDesc = desc;
        colorViewFormat = format;
        return true;
    }
};

Magnifier::Magnifier() noexcept = default;
Magnifier::~Magnifier() noexcept = default;

bool Magnifier::Composite(ID3D11DeviceContext* context, float magnification) noexcept {
    if (!context || !(magnification >= kLeast) || !(magnification < 1000.0f)) return false;
    if (!impl_ || impl_->context.Get() != context || FAILED(impl_->device->GetDeviceRemovedReason())) {
        auto next = std::unique_ptr<Impl>(new (std::nothrow) Impl());
        if (!next || !next->Initialize(context)) {impl_.reset();return false;}
        next->context = context;
        impl_ = std::move(next);
    }
    auto& state = *impl_;
    std::array<ComPtr<ID3D11RenderTargetView>, kTargets> targets;
    std::array<ID3D11RenderTargetView*, kTargets> rawTargets{};
    ComPtr<ID3D11DepthStencilView> boundDepth;
    context->OMGetRenderTargets(kTargets, rawTargets.data(), &boundDepth);
    for (UINT i = 0; i < kTargets; ++i) targets[i].Attach(rawTargets[i]);
    if (!targets[0]) return false;
    for (UINT i = 1; i < kTargets; ++i) if (targets[i]) return false;
    D3D11_RENDER_TARGET_VIEW_DESC targetDesc{};
    targets[0]->GetDesc(&targetDesc);
    if (targetDesc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D ||
        targetDesc.Texture2D.MipSlice != 0 || !ColorFormat(targetDesc.Format)) return false;
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Texture2D> color;
    targets[0]->GetResource(&resource);
    if (FAILED(resource.As(&color))) return false;
    D3D11_TEXTURE2D_DESC desc{};
    color->GetDesc(&desc);
    if (!SimpleTexture(desc) || !FullViewport(context, desc.Width, desc.Height) ||
        !state.PrepareColor(desc, targetDesc.Format)) return false;

    const float w = static_cast<float>(desc.Width), h = static_cast<float>(desc.Height);
    const Lens lens = LensFor(w, h, magnification);
    long left = 0, top = 0, right = 0, bottom = 0;
    Scissor(lens, w, h, &left, &top, &right, &bottom);
    if (right <= left || bottom <= top) return false;
    const LensBlock block{{lens.cx, lens.cy}, lens.r, 1.0f / lens.m, {1.0f / w, 1.0f / h}, {0.0f, 0.0f}};

    // Isolate every pipeline binding (shader constants, predication, UAVs, stream output) behind the private state;
    // the native one comes back whole when `restore` goes.
    ScopedContextState restore(state.context1.Get(), state.isolatedState.Get());
    context->ClearState();
    context->CopyResource(state.colorCopy.Get(), color.Get());
    context->UpdateSubresource(state.lensBlock.Get(), 0, nullptr, &block, 0, 0);
    ID3D11RenderTargetView* target = targets[0].Get();
    context->OMSetRenderTargets(1, &target, nullptr);
    context->OMSetBlendState(state.overwriteBlend.Get(), nullptr, 0xFFFFFFFFu);
    context->OMSetDepthStencilState(state.noDepthState.Get(), 0);
    context->RSSetState(state.scissorRasterizer.Get());
    const D3D11_VIEWPORT viewport{0.0f, 0.0f, w, h, 0.0f, 1.0f};
    context->RSSetViewports(1, &viewport);
    const D3D11_RECT scissor{left, top, right, bottom};
    context->RSSetScissorRects(1, &scissor);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(state.fullscreenShader.Get(), nullptr, 0);
    context->PSSetShader(state.lensShader.Get(), nullptr, 0);
    ID3D11Buffer* blocks[] = {state.lensBlock.Get()};
    context->PSSetConstantBuffers(0, 1, blocks);
    ID3D11SamplerState* samplers[] = {state.linearSampler.Get()};
    context->PSSetSamplers(0, 1, samplers);
    ID3D11ShaderResourceView* inputs[] = {state.colorResource.Get()};
    context->PSSetShaderResources(0, 1, inputs);
    context->Draw(3, 0);
    // No native resource stays referenced by the private state.
    context->ClearState();
    return true;
}

void Magnifier::Reset() noexcept {
    impl_.reset();
}

} // namespace crew::scopepip
