#include "common.hlsli"

cbuffer Object : register(b1)
{
    float4x4 gWorld;      // model -> camera-relative world
    float4 gTint;         // x = sinking, y = selection highlight, z = burn damage, w = searchlight lens emissive
    float4 gObjParams;    // x = seed, y = world y of the waterline (camera relative), z = length, w = freeboard
};

struct VSIn
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float4 color : COLOR;
    float4 params : TEXCOORD0;
};

struct VSOut
{
    float4 pos : SV_Position;
    float3 rel : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float4 color : TEXCOORD2;
    float4 params : TEXCOORD3;
    float3 model : TEXCOORD4;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    float4 w = mul(gWorld, float4(v.pos, 1.0));
    o.rel = w.xyz;
    o.pos = mul(gViewProj, float4(w.xyz, 1.0));
    o.nrm = normalize(mul((float3x3)gWorld, v.nrm));
    o.color = v.color;
    o.params = v.params;
    o.model = v.pos;
    return o;
}

float4 VSShadow(VSIn v) : SV_Position
{
    float4 w = mul(gWorld, float4(v.pos, 1.0));
    return mul(gShadowViewProj, float4(w.xyz, 1.0));
}

float4 PSMain(VSOut i, bool front : SV_IsFrontFace) : SV_Target
{
    float3 N = normalize(i.nrm);
    float3 V = normalize(-i.rel);
    // Thin parts (shields, platforms, lofted plating) are drawn double-sided: light the visible side.
    if (dot(N, V) < 0.0) N = -N;
    float3 albedo = i.color.rgb;
    float rough = i.params.x, metal = i.params.y;
    uint mat = (uint)round(i.params.z * 255.0);
    float emissive = i.params.w;
    float3 m = i.model;
    float seed = gObjParams.x;

    // Paint variation and weathering in model space (stable as the ship moves).
    float grime = fbm(m.xz * 0.35 + seed, 3);
    float panel = valueNoise(float2(floor(m.x / 1.8), floor(m.z / 1.2)) + seed);
    if (mat == 0 || mat == 4)
    {
        albedo *= 0.88 + 0.16 * panel;
        // Rust streaks and grime running down from the deck edge and scuppers.
        float streak = saturate(pow(valueNoise(float2(m.x * 2.7 + seed, 0.0)), 6.0) * 3.0) * saturate(1.0 - abs(m.z - gObjParams.w) * 0.25);
        albedo = lerp(albedo, albedo * float3(0.62, 0.42, 0.30), streak * 0.5);
        albedo *= lerp(1.0, 0.75, saturate((1.5 - m.z) * 0.6) * grime);
        // Portholes along the hull below the deck edge.
        if (mat == 0 && m.z > gObjParams.w - 2.8 && m.z < gObjParams.w - 1.6)
        {
            float px = frac(m.x / 2.4) - 0.5, pz = (m.z - (gObjParams.w - 2.2)) / 0.35;
            if (px * px * 40.0 + pz * pz < 1.0 && abs(m.x) < gObjParams.z * 0.38) { albedo = float3(0.01, 0.012, 0.015); rough = 0.15; }
        }
    }
    else if (mat == 3)
    {
        // Deck planking (teak / linoleum strips).
        float plank = frac(m.y / 0.14);
        float joint = smoothstep(0.0, 0.08, plank) * smoothstep(1.0, 0.92, plank);
        albedo *= lerp(0.72, 1.0, joint) * (0.9 + 0.2 * valueNoise(float2(m.x * 0.4, floor(m.y / 0.14)) + seed));
    }
    else if (mat == 1)
    {
        albedo *= 0.8 + 0.3 * grime;
    }
    // Fire damage: soot and scorched paint.
    float burn = gTint.z * smoothstep(0.35, 0.75, fbm(m.xz * 0.15 + seed * 3.0, 4));
    albedo = lerp(albedo, float3(0.02, 0.018, 0.016), burn);
    rough = lerp(rough, 0.95, burn);

    float3 L = normalize(gSunDir.xyz);
    float shadow = shadowFactor(i.rel, N);
    float3 col = brdf(albedo, rough, metal, N, V, L, gSunColor.rgb * gSunDir.w * shadow);
    float ao = lerp(0.55, 1.0, saturate((m.z + 2.0) / 12.0));
    col += albedo * ambientLight(N) * ao;
    col += pointLights(albedo, rough, metal, N, V, i.rel);
    if (mat == 8) col += float3(1.0, 0.95, 0.8) * gTint.w * 40.0;  // searchlight lens
    col += albedo * emissive * 4.0;
    // Hull below the (moving) water surface darkens towards the sea colour.
    float under = saturate((gObjParams.y - i.rel.y) * 0.4);
    col = lerp(col, float3(0.002, 0.012, 0.015), under * 0.85);
    col = applyFog(col, i.rel, -V);
    return float4(col, 1.0);
}
