#include "common.hlsli"

struct VSIn { float3 pos : POSITION; float3 nrm : NORMAL; };
struct VSOut { float4 pos : SV_Position; float3 rel : TEXCOORD0; float3 nrm : TEXCOORD1; float3 world : TEXCOORD2; };

VSOut VSMain(VSIn v)
{
    VSOut o;
    o.world = v.pos;
    o.rel = v.pos - gCameraPos.xyz;
    o.pos = mul(gViewProj, float4(o.rel, 1.0));
    o.nrm = v.nrm;
    return o;
}

float4 VSShadow(VSIn v) : SV_Position
{
    return mul(gShadowViewProj, float4(v.pos - gCameraPos.xyz, 1.0));
}

float4 PSMain(VSOut i) : SV_Target
{
    float3 N = normalize(i.nrm);
    float h = i.world.y;
    float slope = 1.0 - N.y;
    float n = fbm(i.world.xz * 0.004, 5);
    float n2 = fbm(i.world.xz * 0.05, 3);
    // Tropical island palette: beach, coastal palms, dense jungle, bare rock on steep slopes.
    float3 sand = float3(0.42, 0.37, 0.27);
    float3 jungle = lerp(float3(0.035, 0.07, 0.025), float3(0.07, 0.11, 0.035), n);
    float3 grass = float3(0.11, 0.13, 0.05);  // kunai grass fields of Guadalcanal
    float3 rock = float3(0.16, 0.15, 0.13) * (0.8 + 0.4 * n2);
    float3 albedo = jungle;
    albedo = lerp(albedo, grass, smoothstep(0.55, 0.75, n) * smoothstep(400.0, 80.0, h));
    albedo = lerp(albedo, rock, smoothstep(0.35, 0.6, slope + n2 * 0.15));
    albedo = lerp(albedo, sand, smoothstep(6.0, 1.0, h + n2 * 3.0));
    if (h < 0.0) albedo = lerp(sand, float3(0.05, 0.08, 0.07), saturate(-h / 30.0));
    float3 V = normalize(-i.rel);
    float3 L = normalize(gSunDir.xyz);
    float shadow = shadowFactor(i.rel, N);
    float3 col = brdf(albedo, 0.9, 0.0, N, V, L, gSunColor.rgb * gSunDir.w * shadow);
    col += albedo * ambientLight(N) * (0.7 + 0.3 * n2);
    col += pointLights(albedo, 0.9, 0.0, N, V, i.rel);
    col = applyFog(col, i.rel, -V);
    return float4(col, 1.0);
}
