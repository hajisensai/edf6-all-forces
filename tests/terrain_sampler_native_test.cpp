// Read-only D3D11 WARP terrain-array sampling contract probe. No game process.
// cl /std:c++17 /W4 /WX /EHsc tests\terrain_sampler_native_test.cpp /link d3d11.lib d3dcompiler.lib
// Optional argv[1]: original DX10 BC1 array DDS, argv[2]: source slice (default 8),
// argv[3]: repaired DDS from the production Python converter (otherwise append an independent white fixture).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using Pixel = std::array<float, 4>;
constexpr UINT kSamples = 16;

static void Check(HRESULT result, const char* action) {
    if (FAILED(result)) throw std::runtime_error(std::string(action) + " HRESULT=" + std::to_string(result));
}

static UINT U32(const std::vector<unsigned char>& bytes, size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) throw std::runtime_error("truncated DDS header");
    UINT value = 0;
    std::memcpy(&value, bytes.data() + at, sizeof(value));
    return value;
}

struct Array {
    UINT width = 4, height = 4, mips = 1, slices = 11;
    DXGI_FORMAT format = DXGI_FORMAT_BC1_UNORM;
    std::vector<std::vector<unsigned char>> data;
};

static std::vector<unsigned char> White(UINT width, UINT height) {
    const size_t count = static_cast<size_t>(std::max(1u, (width + 3) / 4)) * std::max(1u, (height + 3) / 4);
    std::vector<unsigned char> result(count * 8, 0);
    for (size_t i = 0; i < count; ++i) {
        result[i * 8] = result[i * 8 + 1] = result[i * 8 + 2] = result[i * 8 + 3] = 255;
    }
    return result;
}

static Array ReadArray(const char* path) {
    Array result;
    if (!path) {
        // Constant BC1 sand, sand and soil, with the actual affected slice indices.
        result.data.resize(result.slices, std::vector<unsigned char>(8, 0));
        const UINT16 sand = 0xdf18, soil = 0x61e5;
        for (UINT i = 0; i < result.slices; ++i) {
            const UINT16 color = i == 10 ? soil : sand;
            std::memcpy(result.data[i].data(), &color, 2);
            std::memcpy(result.data[i].data() + 2, &color, 2);
        }
        return result;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open DDS");
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (bytes.size() < 148 || U32(bytes, 0) != 0x20534444 || U32(bytes, 84) != 0x30315844 || U32(bytes, 132) != 3)
        throw std::runtime_error("expected DX10 Texture2D array DDS");
    result.height = U32(bytes, 12); result.width = U32(bytes, 16); result.mips = std::max(1u, U32(bytes, 28));
    result.slices = U32(bytes, 140); result.format = static_cast<DXGI_FORMAT>(U32(bytes, 128));
    if (!result.width || !result.height || !result.slices || result.mips > 32 ||
        (result.format != DXGI_FORMAT_BC1_UNORM && result.format != DXGI_FORMAT_BC1_UNORM_SRGB))
        throw std::runtime_error("expected nonempty BC1_UNORM[_SRGB] array DDS");
    size_t at = 148;
    for (UINT slice = 0; slice < result.slices; ++slice) {
        UINT width = result.width, height = result.height;
        for (UINT mip = 0; mip < result.mips; ++mip) {
            const size_t size = static_cast<size_t>(std::max(1u, (width + 3) / 4)) * std::max(1u, (height + 3) / 4) * 8;
            if (at > bytes.size() || size > bytes.size() - at) throw std::runtime_error("truncated BC1 array");
            result.data.emplace_back(bytes.begin() + static_cast<ptrdiff_t>(at), bytes.begin() + static_cast<ptrdiff_t>(at + size));
            at += size; width = std::max(1u, width / 2); height = std::max(1u, height / 2);
        }
    }
    if (at != bytes.size()) throw std::runtime_error("unexpected trailing DDS data");
    return result;
}

static ComPtr<ID3D11ShaderResourceView> Upload(ID3D11Device* device, const Array& array) {
    std::vector<D3D11_SUBRESOURCE_DATA> initial;
    for (UINT slice = 0; slice < array.slices; ++slice) {
        UINT width = array.width, height = array.height;
        for (UINT mip = 0; mip < array.mips; ++mip) {
            D3D11_SUBRESOURCE_DATA sub{};
            sub.pSysMem = array.data[static_cast<size_t>(slice) * array.mips + mip].data();
            sub.SysMemPitch = std::max(1u, (width + 3) / 4) * 8;
            sub.SysMemSlicePitch = sub.SysMemPitch * std::max(1u, (height + 3) / 4);
            initial.push_back(sub); width = std::max(1u, width / 2); height = std::max(1u, height / 2);
        }
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = array.width; desc.Height = array.height; desc.MipLevels = array.mips; desc.ArraySize = array.slices;
    desc.Format = array.format; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    Check(device->CreateTexture2D(&desc, initial.data(), &texture), "CreateTexture2D");
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = array.format; view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    view.Texture2DArray.MipLevels = array.mips; view.Texture2DArray.ArraySize = array.slices;
    ComPtr<ID3D11ShaderResourceView> result;
    Check(device->CreateShaderResourceView(texture.Get(), &view, &result), "CreateShaderResourceView");
    return result;
}

static std::vector<Pixel> Sample(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ComputeShader* shader,
                                  ID3D11ShaderResourceView* view, UINT color, UINT multip, UINT mip = 0) {
    const std::array<UINT, 4> params{color, multip, mip, 0};
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(params); desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = params.data();
    ComPtr<ID3D11Buffer> constants;
    Check(device->CreateBuffer(&desc, &initial, &constants), "CreateBuffer constants");
    desc = {}; desc.ByteWidth = kSamples * 3 * sizeof(Pixel); desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS; desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; desc.StructureByteStride = sizeof(Pixel);
    ComPtr<ID3D11Buffer> output;
    Check(device->CreateBuffer(&desc, nullptr, &output), "CreateBuffer output");
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN; uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; uavDesc.Buffer.NumElements = kSamples * 3;
    ComPtr<ID3D11UnorderedAccessView> uav;
    Check(device->CreateUnorderedAccessView(output.Get(), &uavDesc, &uav), "CreateUAV");
    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;
    Check(device->CreateSamplerState(&samplerDesc, &sampler), "CreateSampler");
    ID3D11Buffer* cb = constants.Get(); ID3D11UnorderedAccessView* dst = uav.Get(); ID3D11SamplerState* samp = sampler.Get();
    context->CSSetShader(shader, nullptr, 0); context->CSSetConstantBuffers(0, 1, &cb);
    context->CSSetShaderResources(0, 1, &view); context->CSSetUnorderedAccessViews(0, 1, &dst, nullptr); context->CSSetSamplers(0, 1, &samp);
    context->Dispatch(1, 1, 1);
    ID3D11UnorderedAccessView* none = nullptr; context->CSSetUnorderedAccessViews(0, 1, &none, nullptr);
    desc.BindFlags = 0; desc.MiscFlags = 0; desc.StructureByteStride = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    Check(device->CreateBuffer(&desc, nullptr, &staging), "CreateBuffer staging");
    context->CopyResource(staging.Get(), output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map readback");
    std::vector<Pixel> result(kSamples * 3);
    std::memcpy(result.data(), mapped.pData, result.size() * sizeof(Pixel)); context->Unmap(staging.Get(), 0);
    return result;
}

int main(int argc, char** argv) {
    try {
        Array original = ReadArray(argc > 1 ? argv[1] : nullptr);
        const UINT color = argc > 2 ? static_cast<UINT>(std::stoul(argv[2])) : 8u;
        if (color >= original.slices) throw std::runtime_error("source slice outside array");
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level{};
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context), "D3D11CreateDevice WARP");
        if (level < D3D_FEATURE_LEVEL_11_0) throw std::runtime_error("WARP lacks CS5");
        static const char source[] = R"(
Texture2DArray<float4> colors : register(t0);
SamplerState colorSampler : register(s0);
cbuffer Params : register(b0) { uint colorIndex; uint multipIndex; uint mipIndex; uint pad; };
RWStructuredBuffer<float4> result : register(u0);
[numthreads(16,1,1)] void main(uint3 id : SV_DispatchThreadID) {
    float2 uv = (float2(id.x % 4, id.x / 4) + 0.371) / 4.0;
    float3 base = colors.SampleLevel(colorSampler, float3(uv, (float)colorIndex), (float)mipIndex).rgb;
    float3 multip = colors.SampleLevel(colorSampler, float3(uv, (float)multipIndex), (float)mipIndex).rgb;
    result[id.x * 3] = float4(base,1);
    result[id.x * 3 + 1] = float4(multip,1);
    result[id.x * 3 + 2] = float4(base * multip,1);
})";
        ComPtr<ID3DBlob> code, errors;
        const HRESULT compiled = D3DCompile(source, sizeof(source) - 1, "terrain_array_probe", nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
        if (FAILED(compiled) && errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
        Check(compiled, "D3DCompile");
        ComPtr<ID3DBlob> assembly;
        Check(D3DDisassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, &assembly), "D3DDisassemble");
        const std::string instructions(static_cast<const char*>(assembly->GetBufferPointer()), assembly->GetBufferSize());
        if (instructions.find("utof ") == std::string::npos || instructions.find("sample_l_indexable") == std::string::npos)
            throw std::runtime_error("probe did not compile uint-to-float array sampling");
        ComPtr<ID3D11ComputeShader> shader;
        Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader), "CreateComputeShader");
        auto oldView = Upload(device.Get(), original);
        const auto bad = Sample(device.Get(), context.Get(), shader.Get(), oldView.Get(), color, 0xffffffffu);
        const auto last = Sample(device.Get(), context.Get(), shader.Get(), oldView.Get(), color, original.slices - 1);
        UINT closestSlice = 0;
        float closestError = D3D11_FLOAT32_MAX;
        for (UINT slice = 0; slice < original.slices; ++slice) {
            const auto candidate = Sample(device.Get(), context.Get(), shader.Get(), oldView.Get(), color, slice);
            float error = 0;
            for (UINT i = 0; i < kSamples; ++i) for (UINT channel = 0; channel < 3; ++channel)
                error = std::max(error, std::fabs(bad[i * 3 + 1][channel] - candidate[i * 3 + 1][channel]));
            if (error < closestError) { closestError = error; closestSlice = slice; }
        }
        Array fixed = original;
        if (argc > 3) {
            fixed = ReadArray(argv[3]);
            if (fixed.width != original.width || fixed.height != original.height || fixed.mips != original.mips ||
                fixed.format != original.format || fixed.slices != original.slices + 1 ||
                !std::equal(original.data.begin(), original.data.end(), fixed.data.begin()))
                throw std::runtime_error("production repair changed an authored subresource or DDS layout");
        } else {
            UINT width = fixed.width, height = fixed.height;
            for (UINT mip = 0; mip < fixed.mips; ++mip) {
                fixed.data.push_back(White(width, height)); width = std::max(1u, width / 2); height = std::max(1u, height / 2);
            }
            ++fixed.slices;
        }
        auto fixedView = Upload(device.Get(), fixed);
        const auto good = Sample(device.Get(), context.Get(), shader.Get(), fixedView.Get(), color, original.slices);
        float maxClampError = 0, maxIdentityError = 0, maxOldChange = 0, maxSourceChange = 0;
        for (UINT i = 0; i < kSamples; ++i) for (UINT channel = 0; channel < 3; ++channel) {
            maxClampError = std::max(maxClampError, std::fabs(bad[i * 3 + 1][channel] - last[i * 3 + 1][channel]));
            maxIdentityError = std::max(maxIdentityError, std::fabs(good[i * 3 + 2][channel] - good[i * 3][channel]));
            maxOldChange = std::max(maxOldChange, std::fabs(bad[i * 3 + 2][channel] - bad[i * 3][channel]));
            maxSourceChange = std::max(maxSourceChange, std::fabs(bad[i * 3][channel] - good[i * 3][channel]));
        }
        for (UINT mip = 1; mip < original.mips; ++mip) {
            const auto oldMip = Sample(device.Get(), context.Get(), shader.Get(), oldView.Get(), color, 0xffffffffu, mip);
            const auto fixedMip = Sample(device.Get(), context.Get(), shader.Get(), fixedView.Get(), color, original.slices, mip);
            for (UINT i = 0; i < kSamples; ++i) for (UINT channel = 0; channel < 3; ++channel) {
                maxIdentityError = std::max(maxIdentityError, std::fabs(fixedMip[i * 3 + 2][channel] - fixedMip[i * 3][channel]));
                maxSourceChange = std::max(maxSourceChange, std::fabs(oldMip[i * 3][channel] - fixedMip[i * 3][channel]));
            }
        }
        std::cout << "WARP BC1 array=" << original.width << 'x' << original.height << " slices=" << original.slices
                  << " mips=" << original.mips << " sourceSlice=" << color << " samplesPerMip=" << kSamples << '\n';
        std::cout << "uint(-1)-vs-lastSlice maxError=" << maxClampError << " closestSlice=" << closestSlice << " closestSliceError=" << closestError
                  << " oldProduct-vs-source maxChange=" << maxOldChange
                  << " identityProduct-vs-source maxError=" << maxIdentityError << " appendSourceChange=" << maxSourceChange << '\n';
        std::cout << "firstSourceRGB=" << bad[0][0] << ',' << bad[0][1] << ',' << bad[0][2]
                  << " invalidMultiplierRGB=" << bad[1][0] << ',' << bad[1][1] << ',' << bad[1][2] << '\n';
        if (maxIdentityError > 1e-6f || maxSourceChange > 1e-6f || maxOldChange < 0.01f)
            throw std::runtime_error("terrain sampler contract failed");
        std::cout << "PASS invalid uint(-1) multiplier changes source; explicit white slice restores source albedo exactly\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
