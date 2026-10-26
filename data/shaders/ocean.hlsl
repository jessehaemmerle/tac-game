#include "common.hlsli"

struct VSOut
{
    float4 pos : SV_Position;
    float3 rel : TEXCOORD0;
    float2 world : TEXCOORD1;
    float crest : TEXCOORD2;
    float depth : TEXCOORD3;
};

float cascadeFade(float dist, int c)
{
    float far = gOcean[c] * (c == 0 ? 12.0 : 20.0);
    return saturate(1.0 - dist / far);
}

float waterDepth(float2 world)
{
    float2 uv = (world - gTerrain.xy) / gTerrain.zw;
    if (any(uv < 0.0) || any(uv > 1.0)) return 1000.0;
    uv.y = 1.0 - uv.y;  // row 0 = north edge
    return -gTerrainHeight.SampleLevel(sLinearClamp, uv, 0);
}

VSOut VSMain(float2 local : POSITION)
{
    VSOut o;
    float2 world = gCameraPos.xz + local;
    float dist = length(local);
    float depth = waterDepth(world);
    float damp = saturate(depth / 25.0);  // waves die down over the shelf
    float3 disp = 0;
    [unroll] for (int c = 0; c < 3; ++c)
        disp += gOceanDisp.SampleLevel(sLinearWrap, float3(world / gOcean[c], c), 0).xyz * cascadeFade(dist, c);
    disp *= damp;
    float3 rel = float3(local.x + disp.x, disp.y - gCameraPos.y, local.y + disp.z);
    o.rel = rel;
    o.pos = mul(gViewProj, float4(rel, 1.0));
    o.world = world;
    o.crest = disp.y;
    o.depth = depth;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    float3 V = normalize(-i.rel);
    float dist = length(i.rel);
    float2 slope = 0;
    float foam = 0;
    [unroll] for (int c = 0; c < 3; ++c)
    {
        float4 s = gOceanNormal.Sample(sAniso, float3(i.world / gOcean[c], c));
        float f = cascadeFade(dist, c);
        slope += s.xy * f;
        foam += s.z * f * (c == 0 ? 1.0 : 0.5);
    }
    float damp = saturate(i.depth / 25.0);
    slope *= damp;
    float3 N = normalize(float3(-slope.x, 1.0, -slope.y));
    // Distant water: flatten normals, raise roughness (sub-pixel waves).
    float distRough = saturate(dist / 15000.0);
    N = normalize(lerp(N, float3(0, 1, 0), distRough * 0.6));

    // Wakes and splashes from the wake map (4 km around the camera).
    float2 wd = (i.world - gWakeMap.Load(int3(0, 0, 0)).zw) / 4000.0;
    float2 wuv = float2(0.5 + wd.x, 0.5 - wd.y);
    float wake = 0;
    if (all(wuv > 0.0) && all(wuv < 1.0)) wake = gWakeMap.SampleLevel(sLinearClamp, wuv, 0).x;
    float foamNoise = fbm(i.world * 0.35 + gCameraPos.w * 0.15, 3);
    foam = saturate(foam * 0.8 + wake * (0.6 + 0.6 * foamNoise));
    // Surf on the beaches.
    float shore = saturate(1.0 - i.depth / 4.0) * (0.5 + 0.5 * sin(gCameraPos.w * 0.8 + i.depth * 3.0 + foamNoise * 6.0));
    foam = saturate(foam + shore * 0.8 * step(0.0, i.depth));

    float NdV = saturate(dot(N, V));
    float F = 0.02 + 0.98 * pow(1.0 - NdV, 5.0);
    float3 R = reflect(-V, N);
    R.y = abs(R.y);
    float3 refl = skyRadiance(R, false);
    // Moon glitter path (the disc itself is excluded from reflections to avoid star speckle).
    float3 moonD = normalize(gMoonDir.xyz);
    refl += float3(1.0, 0.97, 0.9) * gMoonDir.w * gSky.w * saturate(moonD.y * 6.0) * pow(saturate(dot(R, moonD)), 900.0) * 3.0;
    float3 L = normalize(gSunDir.xyz);
    float keyRough = 0.07 + distRough * 0.25;
    float shadow = shadowFactor(i.rel, float3(0, 1, 0));
    float3 H = normalize(V + L);
    float spec = D_GGX(saturate(dot(N, H)), keyRough * keyRough) * 0.25 * F_Schlick(0.02.xxx, saturate(dot(V, H))).x;
    refl += gSunColor.rgb * gSunDir.w * spec * saturate(dot(N, L)) * shadow * 2.0;

    // Water body: deep blue-green, lighter turquoise over shallows and reefs.
    float amb = gAmbientSky.w;
    float3 deep = float3(0.004, 0.018, 0.028);
    float3 shallow = float3(0.02, 0.11, 0.10);
    float3 body = lerp(shallow, deep, saturate(i.depth / 18.0)) * (ambientLight(float3(0, 1, 0)) * 2.5 + gSunColor.rgb * gSunDir.w * 0.08);
    // Subsurface glow through backlit wave crests.
    float sss = pow(saturate(dot(V, -L)), 4.0) * saturate(i.crest * 0.4 + 0.3);
    body += float3(0.0, 0.07, 0.06) * gSunColor.rgb * gSunDir.w * sss * 0.6;

    float3 col = lerp(body, refl, F);
    // Point lights (fires, flares, flashes, searchlights) glitter on the waves as long streaks.
    uint count = (uint)gLights.x;
    [loop] for (uint li = 0; li < count; ++li)
    {
        Light l = gLightBuf[li];
        float3 d = l.pos - i.rel;
        float d2 = dot(d, d);
        float range = l.radius * 4.0;
        if (d2 > range * range) continue;
        float ld = sqrt(d2);
        float3 Ll = d / ld;
        float3 Hl = normalize(V + Ll);
        float s2 = D_GGX(saturate(dot(N, Hl)), 0.012) * 0.02;
        float att = 1.0 / (1.0 + d2 * 0.00002) * saturate(1.0 - ld / range);
        col += l.color * att * (s2 * F * 6.0 + saturate(dot(N, Ll)) * 0.002 + foam * 0.02);
    }
    // Foam scatters light diffusely (albedo ~0.8): sky dome plus the key light even at grazing angles.
    float3 foamCol = float3(0.85, 0.87, 0.9) * (ambientLight(float3(0, 1, 0)) * 2.4 + skyRadiance(float3(0, 1, 0), false) * 0.35 +
                                                gSunColor.rgb * gSunDir.w * (0.25 + 0.75 * saturate(dot(N, L))) * 0.5 * shadow);
    col = lerp(col, foamCol, saturate(foam * 1.1));
    col = applyFog(col, i.rel, -V);
    return float4(col, 1.0);
}
