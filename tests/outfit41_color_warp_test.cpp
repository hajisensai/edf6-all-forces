// Real EDF4.1 / EDF6 pixel bytecode and original compressed outfit textures on D3D11 WARP.
// The Python companion extracts read-only fixtures from installed games and shipped archives.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
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
using Bytes = std::vector<unsigned char>;
using Pixel = std::array<float, 4>;
constexpr UINT kSide = 64;
static void Check(HRESULT hr, const char* action) {
    if (FAILED(hr)) throw std::runtime_error(std::string(action) + " HRESULT=" + std::to_string(hr));
}
static UINT U32(const Bytes& bytes, size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) throw std::runtime_error("truncated input");
    UINT result; std::memcpy(&result, bytes.data() + at, 4); return result;
}
struct Reader {
    Bytes bytes; size_t at = 0;
    explicit Reader(const char* path) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("cannot open fixture");
        bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }
    UINT Number() { const UINT result = U32(bytes, at); at += 4; return result; }
    Bytes Blob() {
        const size_t count = Number();
        if (at > bytes.size() || count > bytes.size() - at) throw std::runtime_error("truncated blob");
        Bytes result(bytes.begin() + static_cast<ptrdiff_t>(at), bytes.begin() + static_cast<ptrdiff_t>(at + count));
        at += count; return result;
    }
};
struct Param { std::string name; Pixel value; };
static std::vector<Param> Params(Reader& input) {
    std::vector<Param> result;
    const UINT count = input.Number();
    if (count > 128) throw std::runtime_error("invalid material parameter count");
    for (UINT i = 0; i < count; ++i) {
        const Bytes name = input.Blob(), value = input.Blob();
        if (value.size() != sizeof(Pixel)) throw std::runtime_error("invalid parameter value");
        Param param; param.name.assign(name.begin(), name.end()); std::memcpy(param.value.data(), value.data(), value.size());
        result.push_back(param);
    }
    return result;
}
static ComPtr<ID3D11ShaderResourceView> Texture(ID3D11Device* device, const Bytes& dds) {
    if (dds.size() < 128 || U32(dds, 0) != 0x20534444) throw std::runtime_error("expected original DDS");
    const UINT fourcc = U32(dds, 84), width = U32(dds, 16), height = U32(dds, 12);
    DXGI_FORMAT format; UINT block;
    if (fourcc == 0x31545844) { format = DXGI_FORMAT_BC1_UNORM; block = 8; }
    else if (fourcc == 0x33545844) { format = DXGI_FORMAT_BC2_UNORM; block = 16; }
    else if (fourcc == 0x35545844) { format = DXGI_FORMAT_BC3_UNORM; block = 16; }
    else throw std::runtime_error("unreviewed outfit DDS format");
    const UINT pitch = std::max(1u, (width + 3) / 4) * block;
    const size_t count = static_cast<size_t>(pitch) * std::max(1u, (height + 3) / 4);
    if (!width || !height || count > dds.size() - 128) throw std::runtime_error("invalid DDS mip0");
    D3D11_TEXTURE2D_DESC desc{}; desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{}; data.pSysMem = dds.data() + 128; data.SysMemPitch = pitch;
    ComPtr<ID3D11Texture2D> texture; Check(device->CreateTexture2D(&desc, &data, &texture), "CreateTexture2D DDS");
    ComPtr<ID3D11ShaderResourceView> view; Check(device->CreateShaderResourceView(texture.Get(), nullptr, &view), "CreateSRV DDS");
    return view;
}
struct Shader {
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11ShaderReflection> reflection;
    Shader(ID3D11Device* device, const Bytes& bytecode) {
        Check(device->CreatePixelShader(bytecode.data(), bytecode.size(), nullptr, &pixel), "CreatePixelShader actual DXBC");
        Check(D3DReflect(bytecode.data(), bytecode.size(), IID_PPV_ARGS(&reflection)), "D3DReflect actual DXBC");
        D3D11_SHADER_DESC desc{}; Check(reflection->GetDesc(&desc), "GetDesc actual DXBC");
        std::string source = "struct O { float4 position:SV_POSITION;";
        std::string body = "O main(uint id:SV_VertexID){O o;float2 p=float2((id<<1)&2,id&2);o.position=float4(p*float2(2,-2)+float2(-1,1),0,1);";
        for (UINT i = 0; i < desc.InputParameters; ++i) {
            D3D11_SIGNATURE_PARAMETER_DESC param{}; Check(reflection->GetInputParameterDesc(i, &param), "GetInputParameterDesc");
            if (param.SystemValueType == D3D_NAME_POSITION) continue;
            const std::string name = param.SemanticName, field = "v" + std::to_string(i);
            const UINT components = param.Mask == 3 ? 2u : 3u;
            source += "float" + std::to_string(components) + " " + field + ":" + name + std::to_string(param.SemanticIndex) + ";";
            if (name == "TEXCOORD") body += "o." + field + "=p;";
            else if (name == "EYE_VECTOR") body += "o." + field + "=float3(0,0,-1);";
            else if (name == "NORMAL") body += "o." + field + "=float3(0,0,1);";
            else if (name == "TANGENT") body += "o." + field + "=float3(1,0,0);";
            else if (name == "BINORMAL") body += "o." + field + "=float3(0,1,0);";
            else body += "o." + field + "=float3(0,0,1);";
        }
        source += "};" + body + "return o;}";
        ComPtr<ID3DBlob> code, errors;
        const HRESULT hr = D3DCompile(source.data(), source.size(), "outfit_colour_vertex_fixture", nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
        if (FAILED(hr) && errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
        Check(hr, "D3DCompile vertex fixture");
        Check(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vertex), "CreateVertexShader fixture");
    }
    ComPtr<ID3D11Buffer> Constants(ID3D11Device* device, const std::vector<Param>& params, UINT palette) {
        auto* cb = reflection->GetConstantBufferByName("xgl_user_param");
        D3D11_SHADER_BUFFER_DESC desc{}; Check(cb->GetDesc(&desc), "GetDesc material buffer");
        Bytes bytes((desc.Size + 15) & ~15u, 0);
        for (UINT i = 0; i < desc.Variables; ++i) {
            D3D11_SHADER_VARIABLE_DESC var{}; Check(cb->GetVariableByIndex(i)->GetDesc(&var), "GetDesc shader default");
            if (var.DefaultValue) std::memcpy(bytes.data() + var.StartOffset, var.DefaultValue, var.Size);
        }
        for (const auto& param : params) {
            D3D11_SHADER_VARIABLE_DESC var{};
            if (FAILED(cb->GetVariableByName(param.name.c_str())->GetDesc(&var))) continue;
            if (var.Size > sizeof(Pixel) || var.StartOffset + var.Size > bytes.size()) throw std::runtime_error("unexpected material variable size");
            std::memcpy(bytes.data() + var.StartOffset, param.value.data(), var.Size);
        }
        const std::array<Pixel, 4> primary{{{0,0,0,0}, {.85f,.12f,.28f,1}, {.11f,.68f,.90f,1}, {.85f,.12f,.28f,1}}};
        const std::array<Pixel, 4> secondary{{{0,0,0,0}, {.18f,.78f,.42f,1}, {.18f,.78f,.42f,1}, {.72f,.19f,.83f,1}}};
        for (UINT slot = 0; palette && slot < 2; ++slot) {
            D3D11_SHADER_VARIABLE_DESC var{}; D3D11_SHADER_TYPE_DESC type{};
            auto* variable = cb->GetVariableByName(slot ? "change_color1" : "change_color0");
            Check(variable->GetDesc(&var), "GetDesc colour binding"); Check(variable->GetType()->GetDesc(&type), "GetDesc colour type");
            if (var.StartOffset != 16 + slot * 16 || var.Size != 12 || type.Type != D3D_SVT_FLOAT || type.Columns != 3)
                throw std::runtime_error("colour ABI differs from actual float3 offsets 16/32");
            std::memcpy(bytes.data() + var.StartOffset, (slot ? secondary[palette] : primary[palette]).data(), var.Size);
        }
        D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth = static_cast<UINT>(bytes.size()); buffer.Usage = D3D11_USAGE_IMMUTABLE; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = bytes.data(); ComPtr<ID3D11Buffer> result;
        Check(device->CreateBuffer(&buffer, &initial, &result), "CreateBuffer material constants"); return result;
    }
};
static std::vector<Pixel> Render(ID3D11Device* device, ID3D11DeviceContext* context, Shader& shader,
                                  const std::vector<Param>& params, UINT palette, ID3D11ShaderResourceView* const* textures) {
    auto constants = shader.Constants(device, params, palette);
    D3D11_TEXTURE2D_DESC desc{}; desc.Width = kSide; desc.Height = kSide; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    std::array<ComPtr<ID3D11Texture2D>, 5> output; std::array<ComPtr<ID3D11RenderTargetView>, 5> targets;
    std::array<ID3D11RenderTargetView*, 5> raw{};
    for (UINT i = 0; i < raw.size(); ++i) {
        Check(device->CreateTexture2D(&desc, nullptr, &output[i]), "CreateTexture2D target");
        Check(device->CreateRenderTargetView(output[i].Get(), nullptr, &targets[i]), "CreateRTV"); raw[i] = targets[i].Get();
    }
    D3D11_BUFFER_DESC cbDesc{}; cbDesc.ByteWidth = 512; cbDesc.Usage = D3D11_USAGE_IMMUTABLE; cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    const std::array<float, 128> zero{}; D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = zero.data();
    ComPtr<ID3D11Buffer> system; Check(device->CreateBuffer(&cbDesc, &initial, &system), "CreateBuffer system");
    ID3D11Buffer* cb[] = {system.Get(), system.Get(), constants.Get()};
    context->PSSetConstantBuffers(0, 3, cb); context->PSSetShaderResources(0, 4, textures);
    context->OMSetRenderTargets(static_cast<UINT>(raw.size()), raw.data(), nullptr);
    const D3D11_VIEWPORT viewport{0,0,static_cast<float>(kSide),static_cast<float>(kSide),0,1}; context->RSSetViewports(1, &viewport);
    context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(shader.vertex.Get(), nullptr, 0); context->PSSetShader(shader.pixel.Get(), nullptr, 0); context->Draw(3, 0);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging; Check(device->CreateTexture2D(&desc, nullptr, &staging), "CreateTexture2D staging");
    context->CopyResource(staging.Get(), output[0].Get()); D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map actual pixel output");
    std::vector<Pixel> result(kSide * kSide);
    for (UINT y = 0; y < kSide; ++y) std::memcpy(result.data() + y * kSide, static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch, kSide * sizeof(Pixel));
    context->Unmap(staging.Get(), 0); return result;
}
static float Error(const std::vector<Pixel>& a, const std::vector<Pixel>& b, UINT first, UINT last) {
    float result = 0;
    for (size_t i = 0; i < a.size(); ++i) for (UINT c = first; c < last; ++c) {
        if (!std::isfinite(a[i][c]) || !std::isfinite(b[i][c])) throw std::runtime_error("non-finite actual pixel output");
        result = std::max(result, std::fabs(a[i][c] - b[i][c]));
    }
    return result;
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("expected extracted fixture path");
        Reader input(argv[1]); if (input.Number() != 0x31434c4f) throw std::runtime_error("fixture version mismatch");
        std::array<Bytes, 3> code; for (auto& bytes : code) bytes = input.Blob();
        const auto originalParams = Params(input), convertedParams = Params(input);
        std::array<Bytes, 4> data; for (auto& bytes : data) bytes = input.Blob();
        if (input.at != input.bytes.size()) throw std::runtime_error("unexpected fixture tail");
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; D3D_FEATURE_LEVEL level{};
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context), "D3D11CreateDevice WARP");
        if (level < D3D_FEATURE_LEVEL_11_0) throw std::runtime_error("WARP shader model 5 unavailable");
        D3D11_SAMPLER_DESC samplerDesc{}; samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        ComPtr<ID3D11SamplerState> sampler; Check(device->CreateSamplerState(&samplerDesc, &sampler), "CreateSampler");
        std::array<ID3D11SamplerState*, 4> samplers; samplers.fill(sampler.Get()); context->PSSetSamplers(0, 4, samplers.data());
        D3D11_RASTERIZER_DESC rasterDesc{}; rasterDesc.FillMode = D3D11_FILL_SOLID; rasterDesc.CullMode = D3D11_CULL_NONE; rasterDesc.DepthClipEnable = TRUE;
        ComPtr<ID3D11RasterizerState> raster; Check(device->CreateRasterizerState(&rasterDesc, &raster), "CreateRasterizer"); context->RSSetState(raster.Get());
        std::array<ComPtr<ID3D11ShaderResourceView>, 4> textures; std::array<ID3D11ShaderResourceView*, 4> raw{};
        for (UINT i = 0; i < raw.size(); ++i) { textures[i] = Texture(device.Get(), data[i]); raw[i] = textures[i].Get(); }
        std::array<std::array<std::vector<Pixel>, 4>, 3> pixels;
        for (UINT game = 0; game < code.size(); ++game) {
            Shader shader(device.Get(), code[game]);
            for (UINT palette = 0; palette < 4; ++palette)
                pixels[game][palette] = Render(device.Get(), context.Get(), shader, game ? convertedParams : originalParams, palette, raw.data());
        }
        float parity = 0, alpha = 0;
        for (UINT palette = 0; palette < 4; ++palette) parity = std::max(parity, Error(pixels[0][palette], pixels[2][palette], 0, 3));
        for (UINT game = 0; game < 3; ++game) for (UINT palette = 2; palette < 4; ++palette)
            alpha = std::max(alpha, Error(pixels[game][1], pixels[game][palette], 3, 4));
        const float primary = Error(pixels[2][1], pixels[2][2], 0, 3), secondary = Error(pixels[2][1], pixels[2][3], 0, 3);
        if (parity > 3e-6f || alpha > 3e-6f) throw std::runtime_error("actual shader RGB parity or palette-independent alpha failed");
        std::cout << "{\"pixels\":" << kSide * kSide << ",\"palettes\":4,\"shaders\":3,\"parity\":" << parity
                  << ",\"alpha\":" << alpha << ",\"primary\":" << primary << ",\"secondary\":" << secondary << "}\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
