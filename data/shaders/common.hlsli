// Iron Tide — shared shader definitions (Shader Model 5.1).
// Render space: x = east, y = up, z = north. All positions sent to shaders are camera-relative.
#ifndef IRONTIDE_COMMON_HLSLI
#define IRONTIDE_COMMON_HLSLI

#define PI 3.14159265

cbuffer Frame : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4x4 gView;
    float4x4 gShadowViewProj;
    float4 gCameraPos;      // world position of the camera, w = time (s)
    float4 gSunDir;         // direction TO the key light (sun or moon), w = key light intensity
    float4 gSunColor;       // rgb, w = 1 if the key light is the moon
    float4 gMoonDir;        // direction to the moon, w = illuminated fraction
    float4 gTrueSunDir;     // direction to the sun, w = sun altitude (deg)
    float4 gAmbientSky;     // rgb, w = ambient light level 0..1 (simulation scale)
    float4 gAmbientGround;
    float4 gFogColor;       // rgb, w = density (1/m)
    float4 gFogParams;      // x = visibility (m), y = height falloff, z = rain, w = cloud cover
    float4 gScreen;         // width, height, 1/width, 1/height
    float4 gTime;           // time, dt, frame, exposure
    float4 gWind;           // dir x, dir z, speed (m/s), sea state
    float4 gOcean;          // cascade sizes L0, L1, L2, choppiness
    float4 gTerrain;        // world min x, min z, size x, size z
    float4 gLights;         // x = light count
    float4 gSky;            // x = star intensity, y = cloud brightness, z = flash glow, w = moon intensity
};

struct Light
{
    float3 pos;      // camera-relative
    float radius;
    float3 color;    // linear, premultiplied by intensity
    float type;      // 0 point, 1 spot
    float3 dir;      // spot direction
    float cosAngle;  // spot cone
    float4 extra;    // padding to 64 bytes
};

StructuredBuffer<Light> gLightBuf : register(t0);
Texture2D<float> gShadowMap : register(t1);
Texture2DArray<float4> gOceanDisp : register(t2);    // xyz displacement per cascade
Texture2DArray<float4> gOceanNormal : register(t3);  // xy slope, z foam (jacobian), w unused
Texture2D<float> gTerrainHeight : register(t4);
Texture2D<float4> gWakeMap : register(t5);           // x foam, y turbulence, zw centre of the map (world xz)
Texture2D<float> gSceneDepth : register(t6);

SamplerState sLinearWrap : register(s0);
SamplerState sLinearClamp : register(s1);
SamplerState sPointClamp : register(s2);
SamplerComparisonState sShadow : register(s3);
SamplerState sAniso : register(s4);

// ------------------------------------------------------------------------------------------ noise
float hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float hash13(float3 p3)
{
    p3 = frac(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return frac((p3.x + p3.y) * p3.z);
}

float valueNoise(float2 p)
{
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash12(i), hash12(i + float2(1, 0)), u.x), lerp(hash12(i + float2(0, 1)), hash12(i + float2(1, 1)), u.x), u.y);
}

float fbm(float2 p, int octaves)
{
    float s = 0.0, a = 0.5;
    [loop] for (int i = 0; i < octaves; ++i) { s += a * valueNoise(p); p = p * 2.03 + 17.1; a *= 0.5; }
    return s;
}

// ------------------------------------------------------------------------------------------- sky
// Atmosphere: compact single-scattering approximation (Rayleigh + Mie) plus a night sky with
// moonlight scattering, stars, Milky Way, moon disc and a cloud deck lit by moon and flashes.
float3 skyRadiance(float3 dir, bool withDiscs)
{
    float3 sunD = normalize(gTrueSunDir.xyz);
    float sunAlt = gTrueSunDir.w;
    float up = max(dir.y, 0.0);
    float horizon = pow(1.0 - up, 4.0);
    // Daylight sky (fades out below the horizon)
    float day = saturate((sunAlt + 6.0) / 12.0);
    float cosS = dot(dir, sunD);
    float rayleigh = 0.75 * (1.0 + cosS * cosS);
    float g = 0.76;
    float mie = (1.0 - g * g) / (4.0 * PI * pow(1.0 + g * g - 2.0 * g * cosS, 1.5));
    float3 zenith = float3(0.12, 0.25, 0.62);
    float3 hor = float3(0.62, 0.70, 0.78);
    float sunsetness = saturate(1.0 - abs(sunAlt) / 12.0);
    float3 dusk = lerp(float3(0.9, 0.45, 0.2), float3(0.6, 0.3, 0.35), saturate(-cosS));
    float3 daySky = lerp(zenith, hor, horizon) * rayleigh * 0.9 + float3(1.0, 0.85, 0.6) * mie * 0.08;
    daySky = lerp(daySky, dusk * (0.35 + 0.65 * horizon), sunsetness * saturate(cosS * 0.5 + 0.6) * 0.8);
    float3 col = daySky * day * 1.6;
    // Night sky
    float night = 1.0 - day;
    float3 moonD = normalize(gMoonDir.xyz);
    float moonUp = saturate(moonD.y * 4.0) * gSky.w;
    float cosM = dot(dir, moonD);
    float3 nightBase = lerp(float3(0.004, 0.006, 0.012), float3(0.010, 0.012, 0.018), horizon);
    float3 moonGlow = float3(0.06, 0.07, 0.09) * moonUp * (0.25 * (1.0 + cosM * cosM) + 2.0 * pow(saturate(cosM), 24.0));
    col += night * (nightBase + moonGlow);
    // Stars and Milky Way: sub-pixel points (distance to a jittered position inside fine sky cells).
    if (withDiscs && dir.y > -0.05)
    {
        float3 sd = dir;
        float2 sc = float2(atan2(sd.z, sd.x), asin(saturate(sd.y))) * 900.0;
        float2 cell = floor(sc);
        float h = hash12(cell);
        float2 jitter = float2(hash12(cell + 3.1), hash12(cell + 5.7));
        float d = length(frac(sc) - jitter);
        float star = step(0.985, h) * pow(hash12(cell + 7.3), 3.0) * saturate(1.0 - d * 6.0) * 6.0;
        float twinkle = 0.7 + 0.3 * sin(gCameraPos.w * (2.0 + h * 5.0) + h * 40.0);
        float band = exp(-pow(dot(sd, normalize(float3(0.3, 0.5, -0.8))) * 3.0, 2.0));
        float milky = band * (0.6 + 0.4 * fbm(sd.xz * 6.0 + sd.y * 3.0, 4)) * 0.012;
        col += night * (star * twinkle * float3(0.9, 0.95, 1.0) * 0.05 + milky * float3(0.8, 0.85, 1.0)) * gSky.x * saturate(dir.y * 8.0);
        // Moon disc with phase terminator.
        float disc = smoothstep(0.99985, 0.99992, cosM);
        float3 moonCol = float3(1.0, 0.97, 0.9) * 1.6 * gMoonDir.w;
        col += disc * moonCol * saturate(moonD.y * 10.0 + 0.2);
        // Sun disc
        col += smoothstep(0.99990, 0.99995, cosS) * float3(40.0, 36.0, 30.0) * day;
    }
    // Cloud deck (procedural), lit by sky light, the moon and battle flashes.
    float cloud = gFogParams.w;
    if (dir.y > 0.0 && cloud > 0.01)
    {
        float2 uv = dir.xz / (dir.y + 0.08) * 0.45 + gWind.xy * gCameraPos.w * 0.0015 + gCameraPos.xz * 0.000012;
        float n = fbm(uv * 2.2, 5);
        float cov = smoothstep(1.0 - cloud * 0.95, 1.05 - cloud * 0.6, n);
        float3 cloudLit = (daySky * 0.6 + float3(1, 1, 1) * day * 0.5) * day + night * (float3(0.008, 0.009, 0.012) + moonGlow * 1.5);
        cloudLit += float3(1.0, 0.8, 0.6) * gSky.z * 0.012;  // battle flashes reflected under the deck
        col = lerp(col, cloudLit * gSky.y, cov * saturate(dir.y * 6.0));
    }
    return col;
}

// ------------------------------------------------------------------------------------------- fog
float3 applyFog(float3 color, float3 relPos, float3 viewDir)
{
    float dist = length(relPos);
    float worldY = relPos.y + gCameraPos.y;
    float density = gFogColor.w;
    float heightFactor = exp(-max(worldY, 0.0) * gFogParams.y);
    float fog = 1.0 - exp(-dist * density * (0.35 + 0.65 * heightFactor));
    float3 fogCol = skyRadiance(normalize(float3(viewDir.x, max(viewDir.y, 0.02), viewDir.z)), false) * 0.8 + gFogColor.rgb;
    return lerp(color, fogCol, saturate(fog));
}

// -------------------------------------------------------------------------------------- lighting
float D_GGX(float NdH, float a)
{
    float a2 = a * a;
    float d = NdH * NdH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 1e-6);
}

float V_SmithGGX(float NdV, float NdL, float a)
{
    float k = a * 0.5;
    return 1.0 / ((NdV * (1.0 - k) + k) * (NdL * (1.0 - k) + k) + 1e-5);
}

float3 F_Schlick(float3 f0, float VdH)
{
    return f0 + (1.0 - f0) * pow(1.0 - VdH, 5.0);
}

float3 brdf(float3 albedo, float rough, float metal, float3 N, float3 V, float3 L, float3 radiance)
{
    float3 H = normalize(V + L);
    float NdL = saturate(dot(N, L)), NdV = saturate(dot(N, V)) + 1e-4, NdH = saturate(dot(N, H)), VdH = saturate(dot(V, H));
    float a = max(rough * rough, 0.02);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metal);
    float3 F = F_Schlick(f0, VdH);
    float3 spec = D_GGX(NdH, a) * V_SmithGGX(NdV, NdL, a) * F * 0.25;
    float3 diff = albedo * (1.0 - metal) * (1.0 - F) / PI;
    return (diff + spec) * radiance * NdL;
}

float shadowFactor(float3 relPos, float3 N)
{
    float4 sp = mul(gShadowViewProj, float4(relPos + N * 0.15, 1.0));
    float3 uvz = sp.xyz / sp.w;
    float2 uv = uvz.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0)) return 1.0;
    float s = 0.0;
    [unroll] for (int i = 0; i < 4; ++i)
    {
        float2 o = float2((i & 1) ? 0.5 : -0.5, (i & 2) ? 0.5 : -0.5) / 2048.0;
        s += gShadowMap.SampleCmpLevelZero(sShadow, uv + o, uvz.z - 0.0008);
    }
    return s * 0.25;
}

float3 pointLights(float3 albedo, float rough, float metal, float3 N, float3 V, float3 P)
{
    float3 sum = 0;
    uint count = (uint)gLights.x;
    [loop] for (uint i = 0; i < count; ++i)
    {
        Light l = gLightBuf[i];
        float3 d = l.pos - P;
        float dist2 = dot(d, d);
        if (dist2 > l.radius * l.radius) continue;
        float dist = sqrt(dist2);
        float3 L = d / dist;
        float att = 1.0 / (1.0 + dist2 * 0.0004);
        float window = saturate(1.0 - pow(dist / l.radius, 4.0));
        att *= window * window;
        if (l.type > 0.5)
        {
            float c = dot(-L, l.dir);
            att *= smoothstep(l.cosAngle, l.cosAngle + (1.0 - l.cosAngle) * 0.6, c);
        }
        sum += brdf(albedo, rough, metal, N, V, L, l.color * att);
    }
    return sum;
}

float3 ambientLight(float3 N)
{
    return lerp(gAmbientGround.rgb, gAmbientSky.rgb, N.y * 0.5 + 0.5);
}

#endif
