#include "common.hlsli"

struct VSOut { float4 pos : SV_Position; float3 dir : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID)
{
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0.0, 1.0);  // reversed Z: depth 0 = infinitely far
    float4 w = mul(gInvViewProj, float4(o.pos.xy, 1e-6, 1.0));
    o.dir = w.xyz / w.w;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    float3 d = normalize(i.dir);
    float3 c = skyRadiance(d, true);
    // Below the horizon (rarely visible behind the ocean): dark sea haze.
    if (d.y < 0.0) c = lerp(c, gFogColor.rgb * 0.5, saturate(-d.y * 20.0));
    return float4(c, 1.0);
}
