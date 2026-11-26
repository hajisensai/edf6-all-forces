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
    float4 Fade;       // the stock particle distance fade: x mul, y add (alpha *= sat(1 - (distance * x + y)));
                       // z: the depth target's UNORM steps (2^24 - 1 for D24; 0: a float depth)
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
    float4 EfsFetch[4];  // the efs kinds: the stock quad's fetch (corner xy, uv zw) in strip order
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
// The stock kinds' further textures of one draw (Batch::more): an instance's slot 1..7 samples TexMore[slot - 1].
Texture2D<float4> TexMore[7] : register(t8);
Texture2D<float> Depth : register(t2);
SamplerState Linear : register(s0);
SamplerState Wrap : register(s1);     // the stock sampler (linear, wrap: the PS s0 of the first stock efs and particle draws)

struct V {
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float4 uv : TEXCOORD0;
    float3 misc : TEXCOORD1;    // frame blend, distance from the camera plane, soft range
    float3 basis : TEXCOORD2;   // cos and sin of the rotation, emissive
    float4 vpos : TEXCOORD3;    // the corner's view-space position; w: the centre's distance from the camera
    float3 nrm : TEXCOORD4;     // the lit kinds: the stock VS's world normal
    float3 vlight : TEXCOORD5;  // the set 0xC08: its light, per vertex like its VS (0x19848A0)
    nointerpolation uint slot : TEXCOORD6;   // the stock kinds: which of the draw's textures (0 Tex, 1..7 TexMore)
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
            // The stock's order: the position and range first, the rest only for a light in range (a light out of
            // range costs one 16 byte read, not the whole 64).
            const uint at = k * 32u + b;
            const float4 pr = Points[at].posRange;
            float3 d = w - pr.xyz;
            float dist = length(d);
            [branch] if (dist < pr.w) {
                const float cz = Points[at].cone.z;
                float t = cz - dot(d / dist, n) * (1.0 - cz);
                [branch] if (t > 0.0) {
                    const float4 cf = Points[at].colorFalloff;
                    l += pow(max(1.0 - dist * cf.w, 0.0), Points[at].cone.w) * saturate(t) * cf.rgb;
                }
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

// The efs renderer's Texture<quad> (its VS 0x2130, docs/gpufx-re.md "efs"): a quad is a pair of instances
// (gpufx_efs.h EfsQuad), its corner from the stock fetch, moved along the view ray by zbias (no nearer than `near`),
// the stock fog, alpha and glow terms from its distance; in the stock system constants' own view and projection.
V EfsVs(uint vid, uint iid) {
    Particle a = Particles[Base + 2u * iid];
    Particle b = Particles[Base + 2u * iid + 1u];
    float4 f = EfsFetch[vid & 3u];
    float3 w = a.pos + f.x * float3(a.rot, a.frame, a.soft) + f.y * b.pos;
    float4 w4 = float4(w, 1.0);
    float3 v = float3(dot(w4, Sys[0]), dot(w4, Sys[1]), dot(w4, Sys[2]));
    float d = length(v);
    float nd = max(d + a.size, a.emissive);
    float4 pv = float4(v * (rsqrt(dot(v, v)) * nd), 1.0);
    V o = (V)0;
    o.pos = float4(dot(pv, Sys[8]), dot(pv, Sys[9]), dot(pv, Sys[10]), dot(pv, Sys[11]));
    float3 wc = w - float3(Sys[4].w, Sys[5].w, Sys[6].w);
    float dist = length(wc + normalize(wc) * (d - nd));
    o.misc = float3(dist * Sys[34].x + Sys[34].y, 1.0 - (dist * Sys[35].z + Sys[35].w), dist * Sys[34].w + Sys[35].x);
    o.color = a.color;
    o.uv = float4(f.zw * b.color.zw + b.color.xy, 0.0, 0.0);
    o.vpos = float4(pv.xyz, b.size);
    return o;
}

V Vs(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    if (Kind >= 5u && Kind <= 10u) return EfsVs(vid, iid);
    Particle p = Particles[Base + iid];
    float2 c = float2((vid & 1) ? 1.0 : -1.0, (vid & 2) ? -1.0 : 1.0);
    float s, co;
    sincos(p.rot, s, co);
    float2 r = float2(c.x * co - c.y * s, c.x * s + c.y * co) * (p.size * CornerScale);
    float4 vp = mul(float4(p.pos, 1.0), View);
    float centre = length(vp.xyz);
    if (Kind == 11u && centre > 1e-3) vp.xyz *= (centre - min(0.5 * p.size, 0.5 * centre)) / centre;
    vp.xy += r;
    V o;
    o.pos = mul(vp, Proj);
    o.color = p.color;
    // Ours: a puff fades only once the camera is inside it (its centre nearer than half its size, to nothing at the
    // centre): the camera inside a dust cloud sees through it, not a wall, while a puff a size away stays whole (a
    // fade from one size out thinned every near blast). The stock kinds keep their own distance fade.
    if (Kind == 0u) o.color.a *= saturate(centre / max(0.5 * p.size, 0.5));
    float2 uv = c * float2(0.5, -0.5) + 0.5;
    float last = (float)(max(Frames, 1u) - 1u);
    float fr = clamp(p.frame, 0.0, last);
    uint f0 = (uint)fr;
    uint f1 = min(f0 + 1u, (uint)last);
    o.uv = float4(FrameUV(f0, uv), FrameUV(f1, uv));
    // The stock kinds carry their texture slot in `soft` (gpufx.cpp AppendBatch); their soft range is half the size.
    const bool stock = Kind >= 1u && Kind <= 4u;
    o.misc = float3(fr - (float)f0, abs(vp.z), stock ? clamp(p.size * 0.5, 0.05, 3.0) : p.soft);
    o.slot = stock ? (uint)p.soft : 0u;
    o.basis = float3(co, s, p.emissive);
    o.vpos = float4(vp.xyz, centre);
    o.nrm = float3(0.0, 1.0, 0.0);
    o.vlight = 0.0;
    if (Kind >= 3u && Kind <= 4u) {
        // The stock lit VS (0x196F5C0): normalize(normal * w.x + the corner's world direction * w.y + w.z times the
        // direction from the view-space corner to the camera's world position: the stock mixes the two spaces).
        float3 n = LitSet == 1u ? UnpackNormal(asuint(p.emissive)) : float3(0.0, 1.0, 0.0);
        float3 side = float3(dot(r, View._11_12), dot(r, View._21_22), dot(r, View._31_32));
        float3 cam = float3(Sys[4].w, Sys[5].w, Sys[6].w);
        float3 sum = n * LitWeights.x + side * LitWeights.y + normalize(cam - vp.xyz) * LitWeights.z;
        o.nrm = dot(sum, sum) > 1e-12 ? normalize(sum) : float3(0.0, 1.0, 0.0);
        // The set 0xC08 lights in its VS: shadow, lights and clustered point lights at the corner, interpolated
        // across the card (a per-pixel shadow would cut sharp edges into the card that the stock never shows).
        if (LitSet == 2u && o.pos.w > 1e-4) {
            float2 ndc = o.pos.xy / o.pos.w;
            float2 screen = float2(asuint(Sys[20].x), asuint(Sys[20].y));
            float2 px = clamp((ndc * float2(0.5, -0.5) + 0.5) * screen, 0.0, screen - 1.0);
            o.vlight = StockLight(vp.xyz, o.nrm, px);
        }
    }
    return o;
}

// The distance from the camera plane of the scene at this pixel (SV_Position is in target pixels; the depth copy is the
// whole target, so a split-screen viewport reads its own rectangle), from the hardware depth under this view's own
// projection (d = (z*P22 + P32) / (z*P23 + P33) solved for z; standard and reversed depth alike).
float SceneDistance(float2 px) {
    float d = Depth.Load(int3(int2(px), 0));
    float den = d * Proj._34 - Proj._33;
    return abs((Proj._43 - d * Proj._44) / den);
}

// The efs PS 0x1D0: texel times colour, its glow floor (Sys[37]) and fog (Sys[36]) from the VS's terms; drawn
// premultiplied (the stock SRC_ALPHA blends). Its depth test (LESS / LESS_EQUAL against the scene depth, no write)
// against the depth copy.
float4 EfsPs(V i) {
    float4 t = Tex.Sample(Wrap, i.uv.xy) * i.color;
    if (i.vpos.w > 0.5) {
        float scene = Depth.Load(int3(int2(i.pos.xy), 0));
        // The hardware test compares the fragment's depth converted to the target's format (round to nearest).
        float z = Fade.z > 0.0 ? round(i.pos.z * Fade.z) / Fade.z : i.pos.z;
        if (i.vpos.w > 1.5 ? !(z <= scene) : !(z < scene)) discard;
    }
    float3 fz = saturate(i.misc);
    float fog = exp2(log2(fz.x) * Sys[34].z) * Sys[36].w;
    float3 rgb = max(exp2(log2(fz.z) * Sys[35].y) * Sys[37].w * Sys[37].rgb, t.rgb);
    rgb += fog * (Sys[36].rgb - rgb);
    float a = fz.y * saturate(t.a);
    return float4(rgb * a, a);
}

)hlsl"
// (split: one string literal is limited to 16 KB by MSVC; adjacent literals join)
R"hlsl(
// One of the draw's textures. The slot is one instance's (flat), so a quad's pixels agree but for its edges: the
// gradients are taken outside the branch.
float4 SlotClamp(uint slot, float2 uv, float2 dx, float2 dy) {
    float4 t;
    [branch] switch (slot) {
    case 1u: t = TexMore[0].SampleGrad(Linear, uv, dx, dy); break;
    case 2u: t = TexMore[1].SampleGrad(Linear, uv, dx, dy); break;
    case 3u: t = TexMore[2].SampleGrad(Linear, uv, dx, dy); break;
    case 4u: t = TexMore[3].SampleGrad(Linear, uv, dx, dy); break;
    case 5u: t = TexMore[4].SampleGrad(Linear, uv, dx, dy); break;
    case 6u: t = TexMore[5].SampleGrad(Linear, uv, dx, dy); break;
    case 7u: t = TexMore[6].SampleGrad(Linear, uv, dx, dy); break;
    default: t = Tex.SampleGrad(Linear, uv, dx, dy); break;
    }
    return t;
}
float4 SlotWrap(uint slot, float2 uv, float2 dx, float2 dy) {
    float4 t;
    [branch] switch (slot) {
    case 1u: t = TexMore[0].SampleGrad(Wrap, uv, dx, dy); break;
    case 2u: t = TexMore[1].SampleGrad(Wrap, uv, dx, dy); break;
    case 3u: t = TexMore[2].SampleGrad(Wrap, uv, dx, dy); break;
    case 4u: t = TexMore[3].SampleGrad(Wrap, uv, dx, dy); break;
    case 5u: t = TexMore[4].SampleGrad(Wrap, uv, dx, dy); break;
    case 6u: t = TexMore[5].SampleGrad(Wrap, uv, dx, dy); break;
    case 7u: t = TexMore[6].SampleGrad(Wrap, uv, dx, dy); break;
    default: t = Tex.SampleGrad(Wrap, uv, dx, dy); break;
    }
    return t;
}
// Ours (kind 0) clamps its own flipbook; the stock kinds sample as the stock does (linear, wrap).
float4 SampleSlot(uint slot, float2 uv, bool clampUv) {
    float2 dx = ddx(uv), dy = ddy(uv);
    return clampUv ? SlotClamp(slot, uv, dx, dy) : SlotWrap(slot, uv, dx, dy);
}

// One pixel shader per group of kinds (the group a literal: each entry point compiles only its own path, so the
// stock cards do not pay the registers of our lit smoke or of the stock lit path): 0 ours, 1 the stock unlit kinds,
// 2 the stock lit set 0xC08, 3 the efs kinds, 4 the stock lit set 0xA40 (its own entry: the set 0xC08's clip is a
// discard, and a pixel shader with a discard in it is a late depth test on the hardware, so every card pixel behind
// the scene would run the 0xA40's shadow and point light loop: the stock 0xA40 PS 0x196BA60 has none).
float4 PsGroup(V i, const uint group) {
    if (group == 3u) return EfsPs(i);
    // A flipbook blends two frames; a one-frame texture (the stock cards, almost all) samples once (Frames is uniform).
    float4 t = SampleSlot(i.slot, i.uv.xy, group == 0u);
    [branch] if (Frames > 1u) t = lerp(t, SampleSlot(i.slot, i.uv.zw, group == 0u), i.misc.x);
    float fade = saturate((SceneDistance(i.pos.xy) - i.misc.y) / max(i.misc.z, 0.001));
    if (group == 0u) {
        fade *= saturate((i.misc.y - 0.3) / max(Ambient.w, 0.001));   // ours only: no card cuts the near plane
        float a = t.a * i.color.a * fade;
        // Nothing to add (a = 0: this blend keeps the target as it is, alpha not written): skip the lighting.
        if (!(a > 0.0)) discard;
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
    if (group == 2u || group == 4u) {
        // The set 0xC08 lit per vertex; the set 0xA40 per pixel.
        float3 light = i.vlight;
        if (group == 2u) {
            if (t.a * i.color.a < 0.001) discard;   // the set 0xC08's clip
        } else {
            // Lit only where the pixel adds anything (alpha: a > 0; additive: a non-zero texel colour, not faded out):
            // elsewhere the output is zero whatever the light, so skipping the shadow and point lights changes nothing.
            const bool adds = Kind == 3u ? a > 0.0 : fade > 0.0 && any(t.rgb * i.color.rgb != 0.0);
            light = 0.0;
            [branch] if (adds) light = StockLight(i.vpos.xyz, normalize(i.nrm), i.pos.xy);
        }
        float3 rgb = t.rgb * i.color.rgb * light;
        return Kind == 3u ? float4(rgb * a, a) : float4(rgb * fade, a);
    }
    if (Kind == 11u) {
        // The halo: its own falloff, no texel; only colour is added (alpha 0 under ONE/ONE: the target's kept).
        float2 q = i.uv.xy * 2.0 - 1.0;
        float g = saturate(1.0 - dot(q, q));
        return float4(i.color.rgb * (g * g * saturate(i.color.a) * fade), 0.0);
    }
    if (Kind == 1u) return float4(t.rgb * i.color.rgb * a, a);
    return float4(t.rgb * i.color.rgb * fade, a);
}
float4 PsOurs(V i) : SV_Target0 { return PsGroup(i, 0u); }
float4 PsStock(V i) : SV_Target0 { return PsGroup(i, 1u); }
float4 PsStockLit(V i) : SV_Target0 { return PsGroup(i, 2u); }
float4 PsEfs(V i) : SV_Target0 { return PsGroup(i, 3u); }
float4 PsStockLitA(V i) : SV_Target0 { return PsGroup(i, 4u); }
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
    float efsFetch[16];
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
    ComPtr<ID3D11PixelShader> ps[5];   // PsOurs, PsStock, PsStockLit (0xC08), PsEfs, PsStockLitA (0xA40)
    ComPtr<ID3D11BlendState> blend,blendStockAlpha,blendStockAdd,blendEfsAdd,blendEfsSubtract;
    // The trail kinds (PrepareTrails): the stock trail shaders, their vertex layout, the quads' vertex buffer and the
    // fixed triangle-list indices, xgl_transform (the identity: the quads are in world space) and xgl_user_param, the
    // efs blends as the stock applies them (SRC_ALPHA: the stock PS's output is not premultiplied) and the compares.
    ComPtr<ID3D11VertexShader> trailVs;
    ComPtr<ID3D11PixelShader> trailPs;
    ComPtr<ID3D11InputLayout> trailLayout;
    ComPtr<ID3D11Buffer> trailVertices,trailIndices,trailTransform,trailColor;
    ComPtr<ID3D11BlendState> trailBlend[3];
    ComPtr<ID3D11DepthStencilState> trailLess,trailLessEqual;
    bool trailFailed=false;
    ComPtr<ID3D11DepthStencilState> noDepth,testDepth,testDepthReversed;
    // The scene depth bound for the hardware test (read-only, no write) of the particle kinds: a view of the depth
    // target itself (the depth copy is what the shaders read).
    ComPtr<ID3D11Texture2D> testTarget;
    ComPtr<ID3D11DepthStencilView> testView;
    DXGI_FORMAT testFormat=DXGI_FORMAT_UNKNOWN;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11SamplerState> sampler,samplerWrap;
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
    struct Timing {
        ComPtr<ID3D11Query> disjoint,begin,end,pipeline;
        // ViewParams::profile: a timestamp before each batch and one after the last; marks[k]..marks[k+1] is bucket[k]'s.
        std::vector<ComPtr<ID3D11Query>> marks;
        std::vector<ComPtr<ID3D11Query>> pipes;   // and a pipeline statistics query around each batch
        std::vector<unsigned char> bucket;
        int used=0;
        bool pending=false;
    };
    std::array<Timing,kQueries> timing;
    int timingNext=0;
    float gpuMs=-1.0f;
    double pixelsShaded=-1.0;   // the PS invocations of the same timed Draw
    std::array<float,Stats::kBuckets> bucketMs{-1.0f,-1.0f,-1.0f,-1.0f,-1.0f,-1.0f};
    std::array<double,Stats::kBuckets> bucketPixels{-1.0,-1.0,-1.0,-1.0,-1.0,-1.0};
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
        ComPtr<ID3DBlob> vsCode;
        if(!Compile("Vs","vs_5_0",vsCode))return false;
        static constexpr const char* kPs[5]={"PsOurs","PsStock","PsStockLit","PsEfs","PsStockLitA"};
        for(int k=0;k<5;++k) {
            ComPtr<ID3DBlob> psCode;
            if(!Compile(kPs[k],"ps_5_0",psCode) ||
               FAILED(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps[k])))return false;
        }
        if(FAILED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs)))return false;
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
        // The efs blends (the ctx table 0x10FAEE0, docs/gpufx-re.md): 1 SRC_ALPHA/ONE ADD, 2 SRC_ALPHA/ONE REV_SUBTRACT,
        // both with alpha ONE/ZERO; drawn premultiplied. (Its blend 0 is blendStockAlpha.)
        D3D11_BLEND_DESC be=ba;auto& re=be.RenderTarget[0];
        re.DestBlend=D3D11_BLEND_ONE;
        D3D11_BLEND_DESC bs=be;bs.RenderTarget[0].BlendOp=D3D11_BLEND_OP_REV_SUBTRACT;
        D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=FALSE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc=D3D11_COMPARISON_ALWAYS;
        // The stock particles' own depth state (GPUFX native depth log: test on, no write, LESS_EQUAL); GREATER_EQUAL
        // under a reversed projection.
        D3D11_DEPTH_STENCIL_DESC dt=ds;dt.DepthEnable=TRUE;dt.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        D3D11_DEPTH_STENCIL_DESC dr=dt;dr.DepthFunc=D3D11_COMPARISON_GREATER_EQUAL;
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
        s.ComparisonFunc=D3D11_COMPARISON_NEVER;
        D3D11_SAMPLER_DESC sw=s;sw.AddressU=sw.AddressV=sw.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        D3D11_BUFFER_DESC pb{};pb.ByteWidth=static_cast<UINT>(sizeof(Instance))*kMaxInstances;pb.Usage=D3D11_USAGE_DYNAMIC;
        pb.BindFlags=D3D11_BIND_SHADER_RESOURCE;pb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        pb.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;pb.StructureByteStride=sizeof(Instance);
        D3D11_BUFFER_DESC fb{};fb.ByteWidth=sizeof(FrameConstants);fb.Usage=D3D11_USAGE_DYNAMIC;
        fb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;fb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        D3D11_BUFFER_DESC bb=fb;bb.ByteWidth=sizeof(BatchConstants);
        if(FAILED(device->CreateBlendState(&b,&blend)) || FAILED(device->CreateBlendState(&ba,&blendStockAlpha)) ||
           FAILED(device->CreateBlendState(&bd,&blendStockAdd)) || FAILED(device->CreateBlendState(&be,&blendEfsAdd)) ||
           FAILED(device->CreateBlendState(&bs,&blendEfsSubtract)) || FAILED(device->CreateDepthStencilState(&ds,&noDepth)) ||
           FAILED(device->CreateDepthStencilState(&dt,&testDepth)) || FAILED(device->CreateDepthStencilState(&dr,&testDepthReversed)) ||
           FAILED(device->CreateRasterizerState(&r,&raster)) ||
           FAILED(device->CreateSamplerState(&s,&sampler)) || FAILED(device->CreateSamplerState(&sw,&samplerWrap)) ||
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
            qd.Query=D3D11_QUERY_PIPELINE_STATISTICS;
            if(FAILED(device->CreateQuery(&qd,&t.pipeline)))return false;
        }
        order.resize(kMaxInstances);keys.resize(kMaxInstances);
        context=value;
        return true;
    }

    // The stock trail pipeline from its bytecode (the input layout checked against the VS's own signature by D3D).
    bool MakeTrails(const void* vsCode,std::size_t vsBytes,const void* psCode,std::size_t psBytes,const float color[4]) noexcept {
        const D3D11_INPUT_ELEMENT_DESC layout[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0x00,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0x0C,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"UV_SIZE",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0x1C,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"FRONT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0x28,D3D11_INPUT_PER_VERTEX_DATA,0}};
        static_assert(offsetof(TrailVertex,color)==0x0C && offsetof(TrailVertex,uvSize)==0x1C && offsetof(TrailVertex,front)==0x28,
                      "the stock vertex layout");
        if(FAILED(device->CreateVertexShader(vsCode,vsBytes,nullptr,&trailVs)) ||
           FAILED(device->CreatePixelShader(psCode,psBytes,nullptr,&trailPs)) ||
           FAILED(device->CreateInputLayout(layout,4,vsCode,vsBytes,&trailLayout)))return false;
        D3D11_BUFFER_DESC vb{};vb.ByteWidth=static_cast<UINT>(sizeof(TrailQuad))*kMaxTrailQuads;vb.Usage=D3D11_USAGE_DYNAMIC;
        vb.BindFlags=D3D11_BIND_VERTEX_BUFFER;vb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        std::vector<std::uint16_t> index;
        try { index.resize(static_cast<std::size_t>(kMaxTrailQuads)*kTrailIndices); } catch(...) { return false; }
        for(std::size_t i=0;i<index.size();++i)index[i]=TrailIndex(static_cast<int>(i));
        D3D11_BUFFER_DESC ib{};ib.ByteWidth=static_cast<UINT>(index.size()*sizeof(std::uint16_t));ib.Usage=D3D11_USAGE_IMMUTABLE;
        ib.BindFlags=D3D11_BIND_INDEX_BUFFER;
        const D3D11_SUBRESOURCE_DATA indexData{index.data(),0,0};
        const float identity[16]{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        D3D11_BUFFER_DESC tb{};tb.ByteWidth=sizeof(identity);tb.Usage=D3D11_USAGE_IMMUTABLE;tb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        const D3D11_SUBRESOURCE_DATA identityData{identity,0,0};
        D3D11_BUFFER_DESC cb=tb;cb.ByteWidth=16;
        const D3D11_SUBRESOURCE_DATA colorData{color,0,0};
        if(FAILED(device->CreateBuffer(&vb,nullptr,&trailVertices)) || FAILED(device->CreateBuffer(&ib,&indexData,&trailIndices)) ||
           FAILED(device->CreateBuffer(&tb,&identityData,&trailTransform)) || FAILED(device->CreateBuffer(&cb,&colorData,&trailColor)))return false;
        // The ctx blend table 0x10FAEE0 as the stock applies it (docs/gpufx-re.md "efs"): alpha SRC_ALPHA/INV_SRC_ALPHA,
        // add SRC_ALPHA/ONE, subtract SRC_ALPHA/ONE REV_SUBTRACT; alpha ONE/ZERO ADD; all four channels written.
        for(int k=0;k<3;++k) {
            D3D11_BLEND_DESC b{};auto& rt=b.RenderTarget[0];
            rt.BlendEnable=TRUE;rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;rt.DestBlend=k==0 ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_ONE;
            rt.BlendOp=k==2 ? D3D11_BLEND_OP_REV_SUBTRACT : D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlendAlpha=D3D11_BLEND_ZERO;rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
            rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
            if(FAILED(device->CreateBlendState(&b,&trailBlend[k])))return false;
        }
        D3D11_DEPTH_STENCIL_DESC d{};d.DepthEnable=TRUE;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;d.DepthFunc=D3D11_COMPARISON_LESS;
        if(FAILED(device->CreateDepthStencilState(&d,&trailLess)))return false;
        d.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        return SUCCEEDED(device->CreateDepthStencilState(&d,&trailLessEqual));
    }
    void DropTrails() noexcept {
        trailVs.Reset();trailPs.Reset();trailLayout.Reset();trailVertices.Reset();trailIndices.Reset();trailTransform.Reset();
        trailColor.Reset();for(auto& b:trailBlend)b.Reset();trailLess.Reset();trailLessEqual.Reset();
    }

    bool PrepareTestView(ID3D11Texture2D* target,DXGI_FORMAT viewFormat) noexcept {
        if(testView && testTarget.Get()==target && testFormat==viewFormat)return true;
        testView.Reset();testTarget.Reset();
        D3D11_DEPTH_STENCIL_VIEW_DESC v{};v.Format=viewFormat;v.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        v.Flags=D3D11_DSV_READ_ONLY_DEPTH;
        if(viewFormat==DXGI_FORMAT_D24_UNORM_S8_UINT || viewFormat==DXGI_FORMAT_D32_FLOAT_S8X24_UINT)v.Flags|=D3D11_DSV_READ_ONLY_STENCIL;
        if(FAILED(device->CreateDepthStencilView(target,&v,&testView))){testView.Reset();return false;}
        testTarget=target;testFormat=viewFormat;
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
            D3D11_QUERY_DATA_PIPELINE_STATISTICS pipe{};
            if(ctx->GetData(t.pipeline.Get(),&pipe,sizeof(pipe),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK)continue;
            std::array<double,Stats::kBuckets> sum{},px{};
            bool marksReady=true;
            UINT64 prev=0;
            for(int k=0;k<t.used && marksReady;++k) {
                UINT64 m=0;
                if(ctx->GetData(t.marks[static_cast<std::size_t>(k)].Get(),&m,sizeof(m),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK){marksReady=false;break;}
                if(k>0 && m>=prev && dj.Frequency)sum[t.bucket[static_cast<std::size_t>(k-1)]]+=static_cast<double>(m-prev)*1000.0/static_cast<double>(dj.Frequency);
                prev=m;
                if(k+1<t.used) {
                    D3D11_QUERY_DATA_PIPELINE_STATISTICS one{};
                    if(ctx->GetData(t.pipes[static_cast<std::size_t>(k)].Get(),&one,sizeof(one),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK){marksReady=false;break;}
                    px[t.bucket[static_cast<std::size_t>(k)]]+=static_cast<double>(one.PSInvocations);
                }
            }
            if(!marksReady)continue;
            t.pending=false;
            if(t.used>1 && !dj.Disjoint)for(int k=0;k<Stats::kBuckets;++k)bucketMs[static_cast<std::size_t>(k)]=static_cast<float>(sum[static_cast<std::size_t>(k)]);
            if(t.used>1)bucketPixels=px;
            if(!dj.Disjoint && dj.Frequency && b>=a)gpuMs=static_cast<float>(static_cast<double>(b-a)*1000.0/static_cast<double>(dj.Frequency));
            pixelsShaded=static_cast<double>(pipe.PSInvocations);
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

bool Renderer::PrepareTrails(ID3D11DeviceContext* context,const void* vs,std::size_t vsBytes,const void* ps,std::size_t psBytes,
                             const float color[4]) noexcept {
    if(!context || !vs || !ps || !color)return false;
    if(!impl_)impl_.reset(new(std::nothrow) Impl);
    if(!impl_)return false;
    Impl& s=*impl_;
    if(s.failed)return false;
    if(!s.device) {
        if(!s.Initialize(context)){s.failed=true;return false;}
    } else if(s.context.Get()!=context) {
        return false;
    }
    if(s.trailVs)return true;
    if(s.trailFailed)return false;
    if(s.MakeTrails(vs,vsBytes,ps,psBytes,color))return true;
    s.DropTrails();s.trailFailed=true;
    return false;
}

bool Renderer::TrailsReady() const noexcept { return impl_ && impl_->trailVs; }

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
    if(!s.PrepareTestView(depth,static_cast<DXGI_FORMAT>(depthViewFormat)))return refuse("depth test view failed");
    UINT viewports=1;
    D3D11_VIEWPORT vp{};
    context->RSGetViewports(&viewports,&vp);
    if(viewports==0 || !(vp.Width>0.0f) || !(vp.Height>0.0f))
        vp=D3D11_VIEWPORT{0.0f,0.0f,static_cast<float>(cd.Width),static_cast<float>(cd.Height),0.0f,1.0f};
    st.viewport[0]=vp.TopLeftX;st.viewport[1]=vp.TopLeftY;st.viewport[2]=vp.Width;st.viewport[3]=vp.Height;
    s.ResolveTiming(context);
    st.gpuMs=s.gpuMs;st.pixelsShaded=s.pixelsShaded;
    for(int k=0;k<Stats::kBuckets;++k){st.bucketMs[k]=s.bucketMs[static_cast<std::size_t>(k)];st.bucketPixels[k]=s.bucketPixels[static_cast<std::size_t>(k)];}
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
    bool anyTrail=false;
    for(int b=0;b<count;++b) {
        const Batch& batch=batches[b];
        if(IsTrail(batch.kind)){ranges[b]={0,0};anyTrail=anyTrail || (batch.trail && batch.count>0);continue;}
        int n=batch.instances ? batch.count : 0;
        if(n>kMaxInstances-total)n=kMaxInstances-total;
        if(n<0)n=0;
        const bool efs=IsEfs(batch.kind);
        if(efs)n&=~1;   // whole quads (instance pairs), in the stock order
        ranges[b]={total,n};
        if(n==0)continue;
        if(batch.sorted && !efs) {
            SortBackToFront(batch.instances,n,view.view,st.lookNegZ,s.order.data(),s.keys.data());
            for(int i=0;i<n;++i)out[total+i]=batch.instances[s.order[i]];
        } else {
            std::memcpy(out+total,batch.instances,sizeof(Instance)*static_cast<std::size_t>(n));
        }
        total+=n;
    }
    context->Unmap(s.particles.Get(),0);
    // The trail quads, in the batches' order (a batch's range in quads).
    if(anyTrail && s.trailVs && view.trailSystem) {
        if(FAILED(context->Map(s.trailVertices.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map)))return refuse("trail buffer map failed");
        auto* quads=static_cast<TrailQuad*>(map.pData);
        int used=0;
        for(int b=0;b<count;++b) {
            const Batch& batch=batches[b];
            if(!IsTrail(batch.kind) || !batch.trail)continue;
            int n=batch.count;
            if(n>kMaxTrailQuads-used)n=kMaxTrailQuads-used;
            if(n<=0)continue;
            ranges[b]={used,n};
            std::memcpy(quads+used,batch.trail,sizeof(TrailQuad)*static_cast<std::size_t>(n));
            used+=n;
        }
        context->Unmap(s.trailVertices.Get(),0);
    }

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
    f.fade[2]=depthViewFormat==DXGI_FORMAT_D24_UNORM_S8_UINT ? 16777215.0f : 0.0f;
    f.screen[0]=static_cast<float>(cd.Width);f.screen[1]=static_cast<float>(cd.Height);
    f.screen[2]=1.0f/f.screen[0];f.screen[3]=1.0f/f.screen[1];
    if(FAILED(context->Map(s.frameCb.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map)))return refuse("constant map failed");
    std::memcpy(map.pData,&f,sizeof(f));
    context->Unmap(s.frameCb.Get(),0);

    Impl::Timing& timing=s.timing[static_cast<std::size_t>(s.timingNext)];
    const bool timed=!timing.pending;
    if(timed)timing.used=0;
    const bool profiled=timed && view.profile && [&]() noexcept {
        if(timing.marks.size()==static_cast<std::size_t>(kMaxBatches+1))return true;
        try { timing.marks.resize(kMaxBatches+1);timing.pipes.resize(kMaxBatches+1);timing.bucket.resize(kMaxBatches+1); } catch(...) { timing.marks.clear();return false; }
        D3D11_QUERY_DESC md{};md.Query=D3D11_QUERY_TIMESTAMP;
        for(auto& m:timing.marks)if(FAILED(s.device->CreateQuery(&md,&m))){timing.marks.clear();return false;}
        md.Query=D3D11_QUERY_PIPELINE_STATISTICS;
        for(auto& p:timing.pipes)if(FAILED(s.device->CreateQuery(&md,&p))){timing.marks.clear();return false;}
        return true;
    }();
    if(timed) {
        context->Begin(timing.disjoint.Get());
        context->End(timing.begin.Get());
        context->Begin(timing.pipeline.Get());
    }
    {
        ScopedContextState restore(s.context1.Get(),s.isolated.Get());
        context->ClearState();
        s.PrepareFlipbook(context);
        context->CopyResource(s.depthCopy.Get(),depth);
        ID3D11RenderTargetView* target=targets[0].Get();
        context->OMSetRenderTargets(1,&target,s.testView.Get());
        context->OMSetBlendState(s.blend.Get(),nullptr,0xFFFFFFFFu);
        // The particle kinds: the hardware test against the scene depth (a camera-facing card behind the scene has a
        // soft fade of 0 there anyway; the test only lets the GPU drop those pixels before shading them). The efs
        // kinds test per instance in their PS (their compare is the stock draw's own).
        ID3D11DepthStencilState* const tested=st.reversed ? s.testDepthReversed.Get() : s.testDepth.Get();
        ID3D11DepthStencilState* depthBound=nullptr;
        context->RSSetState(s.raster.Get());
        context->RSSetViewports(1,&vp);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->VSSetShader(s.vs.Get(),nullptr,0);
        ID3D11PixelShader* psBound=nullptr;
        ID3D11Buffer* cbs[]={s.frameCb.Get(),s.batchCb.Get()};
        context->VSSetConstantBuffers(0,2,cbs);
        context->PSSetConstantBuffers(0,2,cbs);
        ID3D11ShaderResourceView* pv=s.particleView.Get();
        context->VSSetShaderResources(0,1,&pv);
        ID3D11ShaderResourceView* dv=s.depthView.Get();
        context->PSSetShaderResources(2,1,&dv);
        ID3D11SamplerState* sm[]={s.sampler.Get(),s.samplerWrap.Get()};
        context->PSSetSamplers(0,2,sm);
        // The stock lighting, for the lit batches: borrowed buffers and views, bound read-only.
        const StockLights& L=view.lights;
        const bool litBound=L.Complete();
        if(litBound) {
            ID3D11Buffer* lcb[]={L.system,L.shadow};
            context->VSSetConstantBuffers(2,2,lcb);
            context->PSSetConstantBuffers(2,2,lcb);
            ID3D11ShaderResourceView* lsrv[]={L.shadowMap,L.points,L.masks[0],L.masks[1],L.masks[2]};
            context->PSSetShaderResources(3,5,lsrv);
            context->VSSetShaderResources(3,5,lsrv);   // the set 0xC08 lights per vertex
        }
        // b2: the lit batches' system constants, or the efs batches' (the same xgl_system, taken from an efs draw).
        ID3D11Buffer* systemBound=litBound ? L.system : nullptr;
        // A trail batch swaps in the stock trail pipeline; the next particle batch swaps the particles' back.
        bool trailBound=false;
        auto timedDraw=[&](int bucket,auto draw) noexcept {
            ID3D11Query* const pipe=profiled ? timing.pipes[static_cast<std::size_t>(timing.used)].Get() : nullptr;
            if(profiled) {
                timing.bucket[static_cast<std::size_t>(timing.used)]=static_cast<unsigned char>(bucket);
                context->End(timing.marks[static_cast<std::size_t>(timing.used++)].Get());
                context->Begin(pipe);
            }
            draw();
            if(pipe)context->End(pipe);
        };
        for(int b=0;b<count;++b) {
            const Batch& batch=batches[b];
            if(ranges[b].count==0)continue;
            if(IsTrail(batch.kind)) {
                if(!batch.texture)continue;
                if(!trailBound) {
                    ID3D11Buffer* vb=s.trailVertices.Get();const UINT stride=sizeof(TrailVertex),offset=0;
                    context->IASetInputLayout(s.trailLayout.Get());
                    context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
                    context->IASetIndexBuffer(s.trailIndices.Get(),DXGI_FORMAT_R16_UINT,0);
                    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    ID3D11Buffer* vcb[]={view.trailSystem,s.trailTransform.Get()};
                    context->VSSetShader(s.trailVs.Get(),nullptr,0);
                    context->VSSetConstantBuffers(0,2,vcb);
                    ID3D11Buffer* pcb=s.trailColor.Get();
                    context->PSSetShader(s.trailPs.Get(),nullptr,0);
                    context->PSSetConstantBuffers(2,1,&pcb);
                    ID3D11SamplerState* ts=view.trailSampler;
                    context->PSSetSamplers(0,1,&ts);
                    context->RSSetState(view.trailRaster);
                    trailBound=true;psBound=nullptr;depthBound=nullptr;
                }
                ID3D11ShaderResourceView* tex=batch.texture;
                context->PSSetShaderResources(0,1,&tex);
                const int blendIndex=batch.kind==Kind::trailAlpha ? 0 : batch.kind==Kind::trailAdditive ? 1 : 2;
                context->OMSetBlendState(s.trailBlend[blendIndex].Get(),nullptr,0xFFFFFFFFu);
                ID3D11DepthStencilState* const dss=batch.depthFunc==1 ? s.trailLess.Get() : batch.depthFunc==2 ? s.trailLessEqual.Get() : s.noDepth.Get();
                if(dss!=depthBound){context->OMSetDepthStencilState(dss,0);depthBound=dss;}
                const int quads=ranges[b].count,base=ranges[b].base;
                st.bucketDrawn[5]+=quads;
                timedDraw(5,[&]() noexcept { context->DrawIndexed(static_cast<UINT>(quads*kTrailIndices),0,base*4); });
                st.drawn+=quads;++st.batches;
                continue;
            }
            if(trailBound) {
                context->IASetInputLayout(nullptr);
                context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                context->VSSetShader(s.vs.Get(),nullptr,0);
                context->VSSetConstantBuffers(0,2,cbs);
                context->PSSetConstantBuffers(2,1,&systemBound);
                context->PSSetSamplers(0,2,sm);
                context->RSSetState(s.raster.Get());
                trailBound=false;psBound=nullptr;depthBound=nullptr;
            }
            const bool efs=IsEfs(batch.kind);
            if(efs && (!view.efsSystem || !batch.texture))continue;
            ID3D11ShaderResourceView* tex=batch.texture ? batch.texture : s.flipView.Get();
            if(!tex)continue;
            ID3D11Buffer* system=efs ? view.efsSystem : litBound ? L.system : systemBound;
            if(system!=systemBound) {
                context->VSSetConstantBuffers(2,1,&system);
                context->PSSetConstantBuffers(2,1,&system);
                systemBound=system;
            }
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
            std::memcpy(bc.efsFetch,batch.efsFetch,sizeof(bc.efsFetch));
            if(FAILED(context->Map(s.batchCb.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map)))continue;
            std::memcpy(map.pData,&bc,sizeof(bc));
            context->Unmap(s.batchCb.Get(),0);
            context->PSSetShaderResources(1,1,&tex);
            if(batch.moreCount>0)context->PSSetShaderResources(8,static_cast<UINT>(batch.moreCount),batch.more);
            ID3D11BlendState* bs=batch.kind==Kind::nativeAlpha || batch.kind==Kind::nativeLitAlpha ? s.blendStockAlpha.Get() :
                batch.kind==Kind::nativeAdditive || batch.kind==Kind::nativeLitAdditive ? s.blendStockAdd.Get() :
                batch.kind==Kind::efsAlpha ? s.blendStockAlpha.Get() : batch.kind==Kind::efsAdditive ? s.blendEfsAdd.Get() :
                batch.kind==Kind::efsSubtract ? s.blendEfsSubtract.Get() : batch.kind==Kind::glow ? s.blendStockAdd.Get() : s.blend.Get();
            context->OMSetBlendState(bs,nullptr,0xFFFFFFFFu);
            const int group=efs ? 3 : lit ? (batch.litSet==2 ? 2 : 4) : batch.kind==Kind::smoke ? 0 : 1;
            ID3D11PixelShader* const pss=s.ps[group].Get();
            if(pss!=psBound){context->PSSetShader(pss,nullptr,0);psBound=pss;}
            ID3D11DepthStencilState* const dss=efs ? s.noDepth.Get() : tested;
            if(dss!=depthBound){context->OMSetDepthStencilState(dss,0);depthBound=dss;}
            const int drawn=efs ? ranges[b].count/2 : ranges[b].count;
            const int bucket=group==3 ? 4 : group==4 ? 3 : group;
            st.bucketDrawn[bucket]+=drawn;
            timedDraw(bucket,[&]() noexcept { context->DrawInstanced(4,static_cast<UINT>(drawn),0,0); });
            st.drawn+=drawn;++st.batches;
        }
        // Nothing of the native frame stays referenced by the private state.
        context->ClearState();
    }
    if(profiled && timing.used>0)context->End(timing.marks[static_cast<std::size_t>(timing.used++)].Get());
    if(timed) {
        context->End(timing.pipeline.Get());
        context->End(timing.end.Get());
        context->End(timing.disjoint.Get());
        timing.pending=true;
        s.timingNext=(s.timingNext+1)%Impl::kQueries;
    }
    return true;
}

}  // namespace crew::gpufx
