// Tessendorf FFT ocean: per-frame spectrum evolution and inverse FFT on the GPU (N = 256, 3 cascades).
#include "common.hlsli"

#define N 256
#define LOG2N 8

cbuffer OceanPass : register(b2)
{
    float4 gCascadeL;      // patch sizes (m)
    float4 gOceanTime;     // x = time, y = choppiness, z = foam decay, w = foam bias
    uint4 gMip;            // x = destination mip (downsample pass)
};

Texture2DArray<float4> gH0 : register(t16);
Texture2DArray<float4> gSrcA : register(t17);
Texture2DArray<float4> gSrcB : register(t18);
Texture2DArray<float4> gPrevNormal : register(t19);
RWTexture2DArray<float4> gDstA : register(u0);
RWTexture2DArray<float4> gDstB : register(u1);

float2 cmul(float2 a, float2 b) { return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
float2 cmulI(float2 a) { return float2(-a.y, a.x); }  // a * i

[numthreads(8, 8, 1)]
void CSSpectrum(uint3 id : SV_DispatchThreadID)
{
    float L = gCascadeL[id.z];
    float2 n = float2(int2(id.xy) - N / 2);
    float2 k = 2.0 * PI * n / L;
    float kl = max(length(k), 1e-4);
    float w = sqrt(9.81 * kl);
    float4 h0 = gH0.Load(int4(id, 0));
    float t = gOceanTime.x;
    float2 e = float2(cos(w * t), sin(w * t));
    float2 h = cmul(h0.xy, e) + cmul(h0.zw, float2(e.x, -e.y));
    float2 ih = cmulI(h);
    float2 dx = ih * (k.x / kl), dz = ih * (k.y / kl);
    float2 sx = ih * k.x, sz = ih * k.y;
    float2 dxx = -h * (k.x * k.x / kl), dzz = -h * (k.y * k.y / kl), dxz = -h * (k.x * k.y / kl);
    // Pack pairs of real fields: A + iB.
    gDstA[id] = float4(h + cmulI(dx), dz + cmulI(sx));
    gDstB[id] = float4(sz + cmulI(dxx), dzz + cmulI(dxz));
}

groupshared float4 sA[2][N];
groupshared float4 sB[2][N];

void fftStages(uint i)
{
    uint src = 0;
    [unroll] for (uint s = 1; s <= LOG2N; ++s)
    {
        uint m = 1u << s, halfM = m >> 1;
        uint start = (i / m) * m;
        uint pos = i - start;
        bool top = pos < halfM;
        uint p = top ? pos : pos - halfM;
        float ang = 2.0 * PI * float(p) / float(m);
        float2 tw = float2(cos(ang), sin(ang));
        float4 a = sA[src][start + p], b = sA[src][start + p + halfM];
        float4 c = sB[src][start + p], d = sB[src][start + p + halfM];
        float4 wb = float4(cmul(tw, b.xy), cmul(tw, b.zw));
        float4 wd = float4(cmul(tw, d.xy), cmul(tw, d.zw));
        sA[src ^ 1][i] = top ? a + wb : a - wb;
        sB[src ^ 1][i] = top ? c + wd : c - wd;
        GroupMemoryBarrierWithGroupSync();
        src ^= 1;
    }
}

[numthreads(N, 1, 1)]
void CSFFTRows(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
    uint i = tid.x, row = gid.x, c = gid.y;
    uint rev = reversebits(i) >> (32 - LOG2N);
    sA[0][rev] = gSrcA.Load(int4(i, row, c, 0));
    sB[0][rev] = gSrcB.Load(int4(i, row, c, 0));
    GroupMemoryBarrierWithGroupSync();
    fftStages(i);
    gDstA[uint3(i, row, c)] = sA[LOG2N & 1][i];
    gDstB[uint3(i, row, c)] = sB[LOG2N & 1][i];
}

[numthreads(N, 1, 1)]
void CSFFTCols(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
    uint i = tid.x, col = gid.x, c = gid.y;
    uint rev = reversebits(i) >> (32 - LOG2N);
    sA[0][rev] = gSrcA.Load(int4(col, i, c, 0));
    sB[0][rev] = gSrcB.Load(int4(col, i, c, 0));
    GroupMemoryBarrierWithGroupSync();
    fftStages(i);
    gDstA[uint3(col, i, c)] = sA[LOG2N & 1][i];
    gDstB[uint3(col, i, c)] = sB[LOG2N & 1][i];
}

// Writes displacement (dst A) and slopes + foam (dst B).
[numthreads(8, 8, 1)]
void CSFinalize(uint3 id : SV_DispatchThreadID)
{
    float sgn = ((id.x + id.y) & 1) ? -1.0 : 1.0;
    float4 a = gSrcA.Load(int4(id, 0)) * sgn;
    float4 b = gSrcB.Load(int4(id, 0)) * sgn;
    float lambda = gOceanTime.y;
    float h = a.x, dx = a.y, dz = a.z, sx = a.w;
    float sz = b.x, dxx = b.y, dzz = b.z, dxz = b.w;
    gDstA[id] = float4(-lambda * dx, h, -lambda * dz, 1.0);
    float jx = 1.0 - lambda * dxx, jz = 1.0 - lambda * dzz;
    float J = jx * jz - lambda * lambda * dxz * dxz;
    float prev = gPrevNormal.Load(int4(id, 0)).z;
    float foam = max(saturate(gOceanTime.w - J) * 1.5, prev * gOceanTime.z);
    gDstB[id] = float4(sx / max(jx, 0.2), sz / max(jz, 0.2), saturate(foam), J);
}

// Mip chain of the slope/foam array (box filter).
[numthreads(8, 8, 1)]
void CSDownsample(uint3 id : SV_DispatchThreadID)
{
    int2 p = int2(id.xy) * 2;
    float4 s = gSrcA.Load(int4(p, id.z, 0)) + gSrcA.Load(int4(p + int2(1, 0), id.z, 0)) +
               gSrcA.Load(int4(p + int2(0, 1), id.z, 0)) + gSrcA.Load(int4(p + int2(1, 1), id.z, 0));
    gDstA[id] = s * 0.25;
}
