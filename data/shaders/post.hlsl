// Post-processing: bloom chain, luminance/exposure, tone mapping.
#include "common.hlsli"

cbuffer PostPass : register(b2)
{
    float4 gPost;     // x = bloom intensity, y = threshold, z = texel size x, w = texel size y (of the source)
    float4 gExpo;     // x = adaptation rate, y = min exposure, z = max exposure, w = key value
    float4 gGrade;    // x = vignette, y = grain, z = night blue shift, w = saturation
};

Texture2D<float4> gSrc : register(t16);
Texture2D<float4> gBloom : register(t17);
Texture2D<float> gAdapted : register(t18);
RWTexture2D<float> gLumOut : register(u0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSFull(uint id : SV_VertexID)
{
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0.0, 1.0);
    return o;
}

float3 sample13(float2 uv)
{
    float2 t = gPost.zw;
    float3 a = gSrc.SampleLevel(sLinearClamp, uv + t * float2(-2, -2), 0).rgb;
    float3 b = gSrc.SampleLevel(sLinearClamp, uv + t * float2(0, -2), 0).rgb;
    float3 c = gSrc.SampleLevel(sLinearClamp, uv + t * float2(2, -2), 0).rgb;
    float3 d = gSrc.SampleLevel(sLinearClamp, uv + t * float2(-2, 0), 0).rgb;
    float3 e = gSrc.SampleLevel(sLinearClamp, uv, 0).rgb;
    float3 f = gSrc.SampleLevel(sLinearClamp, uv + t * float2(2, 0), 0).rgb;
    float3 g = gSrc.SampleLevel(sLinearClamp, uv + t * float2(-2, 2), 0).rgb;
    float3 h = gSrc.SampleLevel(sLinearClamp, uv + t * float2(0, 2), 0).rgb;
    float3 k = gSrc.SampleLevel(sLinearClamp, uv + t * float2(2, 2), 0).rgb;
    float3 l = gSrc.SampleLevel(sLinearClamp, uv + t * float2(-1, -1), 0).rgb;
    float3 m = gSrc.SampleLevel(sLinearClamp, uv + t * float2(1, -1), 0).rgb;
    float3 n = gSrc.SampleLevel(sLinearClamp, uv + t * float2(-1, 1), 0).rgb;
    float3 o = gSrc.SampleLevel(sLinearClamp, uv + t * float2(1, 1), 0).rgb;
    return e * 0.125 + (a + c + g + k) * 0.03125 + (b + d + f + h) * 0.0625 + (l + m + n + o) * 0.125;
}

float exposureValue()
{
    float adapted = max(gAdapted.Load(int3(0, 0, 0)), 1e-5);
    return clamp(gExpo.w / adapted, gExpo.y, gExpo.z) * gTime.w;
}

float4 PSDownsampleFirst(VSOut i) : SV_Target
{
    float exposure = exposureValue();
    float3 c = sample13(i.uv) * exposure;
    float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
    float k = saturate((lum - gPost.y) / max(lum, 1e-4));
    return float4(c * k, 1.0);
}

float4 PSDownsample(VSOut i) : SV_Target { return float4(sample13(i.uv), 1.0); }

float4 PSUpsample(VSOut i) : SV_Target
{
    float2 t = gPost.zw;
    float3 s = gSrc.SampleLevel(sLinearClamp, i.uv + t * float2(-1, -1), 0).rgb + gSrc.SampleLevel(sLinearClamp, i.uv + t * float2(1, -1), 0).rgb +
               gSrc.SampleLevel(sLinearClamp, i.uv + t * float2(-1, 1), 0).rgb + gSrc.SampleLevel(sLinearClamp, i.uv + t * float2(1, 1), 0).rgb;
    s = s * 0.125 + gSrc.SampleLevel(sLinearClamp, i.uv, 0).rgb * 0.5;
    return float4(s, 1.0);
}

// Average log-luminance of a 64x64 downsampled view (one thread group), adapted over time.
groupshared float sLum[256];
[numthreads(16, 16, 1)]
void CSLuminance(uint3 tid : SV_GroupThreadID)
{
    float acc = 0;
    [unroll] for (int y = 0; y < 4; ++y)
        [unroll] for (int x = 0; x < 4; ++x)
        {
            float2 uv = (float2(tid.xy * 4 + uint2(x, y)) + 0.5) / 64.0;
            float3 c = gSrc.SampleLevel(sLinearClamp, uv, 0).rgb;
            acc += log(max(dot(c, float3(0.2126, 0.7152, 0.0722)), 1e-6));
        }
    uint idx = tid.y * 16 + tid.x;
    sLum[idx] = acc / 16.0;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint s = 128; s > 0; s >>= 1)
    {
        if (idx < s) sLum[idx] += sLum[idx + s];
        GroupMemoryBarrierWithGroupSync();
    }
    if (idx == 0)
    {
        float avg = exp(sLum[0] / 256.0);
        float prev = gAdapted.Load(int3(0, 0, 0));
        if (!(prev > 0.0) || prev > 1e6) prev = avg;
        float rate = 1.0 - exp(-gTime.y * gExpo.x);
        gLumOut[uint2(0, 0)] = prev + (avg - prev) * rate;
    }
}

float3 aces(float3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PSTonemap(VSOut i) : SV_Target
{
    float3 hdr = gSrc.SampleLevel(sPointClamp, i.uv, 0).rgb;
    float exposure = exposureValue();
    float3 c = hdr * exposure + gBloom.SampleLevel(sLinearClamp, i.uv, 0).rgb * gPost.x;
    // Scotopic vision: low light shifts colours towards blue and desaturates them.
    float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
    float scot = gGrade.z * saturate(1.0 - lum * 4.0);
    c = lerp(c, lum * float3(0.55, 0.7, 1.0), scot * 0.6);
    c = lerp(lum.xxx, c, gGrade.w);
    c = aces(c);
    float2 d = i.uv - 0.5;
    c *= 1.0 - dot(d, d) * gGrade.x;
    c += (hash12(i.pos.xy + frac(gTime.x) * 97.0) - 0.5) * gGrade.y;
    c = pow(saturate(c), 1.0 / 2.2);
    return float4(c, 1.0);
}

#if DEPTH_MS
Texture2DMS<float> gDepthMS : register(t19);
#else
Texture2D<float> gDepthMS : register(t19);
#endif

float PSResolveDepthMS(VSOut i) : SV_Target
{
#if DEPTH_MS
    return gDepthMS.Load(int2(i.pos.xy), 0);
#else
    return gDepthMS.Load(int3(i.pos.xy, 0));
#endif
}

// Fixed-exposure tone mapping for the unit-reference preview.
float4 PSTonemapPreview(VSOut i) : SV_Target
{
    float3 c = aces(gSrc.SampleLevel(sLinearClamp, i.uv, 0).rgb * gExpo.w);
    return float4(pow(saturate(c), 1.0 / 2.2), 1.0);
}
