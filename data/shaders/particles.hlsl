// Billboard particles: smoke, flames, splash columns, flashes, flares, sparks, searchlight beams, tracers,
// rain and snow, and the rain curtains under distant squalls.
#include "common.hlsli"

struct Particle
{
    float3 pos;      // camera-relative
    float size;      // radius (m); beams: width
    float4 color;    // linear rgb, a = opacity
    float3 axis;     // beams: end point (camera relative); columns: unused
    float type;      // 0 smoke, 1 fire, 2 splash column, 3 glow, 4 spark, 5 beam, 6 tracer, 7 rain, 8 snow, 9 rain curtain
    float age;       // 0..1
    float seed;
    float emissive;
    float stretch;   // vertical stretch for columns
};

StructuredBuffer<Particle> gParticles : register(t16);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation float4 color : TEXCOORD1;
    nointerpolation float4 info : TEXCOORD2;  // type, age, seed, emissive
    float3 rel : TEXCOORD3;
    float depth : TEXCOORD4;
};

VSOut VSMain(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    Particle p = gParticles[iid];
    float2 corner = float2((vid & 1) ? 1.0 : -1.0, (vid & 2) ? 1.0 : -1.0);
    float3 camRight = float3(gView[0][0], gView[0][1], gView[0][2]);
    float3 camUp = float3(gView[1][0], gView[1][1], gView[1][2]);
    float3 pos;
    int type = (int)p.type;
    if (type == 5)
    {
        // Beam: quad along the axis from pos to end, facing the camera.
        float3 a = p.pos, b = p.axis;
        float3 ax = normalize(b - a);
        float3 side = normalize(cross(ax, normalize(-(a + b) * 0.5)));
        float t = corner.y * 0.5 + 0.5;
        float width = lerp(p.size * 0.3, p.size * 6.0, t);
        pos = lerp(a, b, t) + side * corner.x * width;
    }
    else if (type == 6 || type == 7)
    {
        // Tracer / raindrop: a thin streak from pos back along the flight path to axis, facing the camera.
        float3 a = p.pos, b = p.axis;
        float3 ax = normalize(a - b + float3(1e-4, 0, 0));
        float3 side = normalize(cross(ax, normalize(-(a + b) * 0.5)));
        float t = corner.y * 0.5 + 0.5;
        float width = p.size * (0.6 + 0.004 * length((a + b) * 0.5));  // stays visible far away
        pos = lerp(b, a, t) + side * corner.x * width;
    }
    else if (type == 2 || type == 9)
    {
        // Splash column / rain curtain: vertical billboard, rising from the water.
        float3 right = normalize(float3(camRight.x, 0, camRight.z));
        pos = p.pos + right * corner.x * p.size + float3(0, 1, 0) * (corner.y * 0.5 + 0.5) * p.size * p.stretch;
    }
    else
    {
        float rot = p.seed * 6.2831 + p.age * (p.seed - 0.5) * 2.0;
        float2 c = float2(corner.x * cos(rot) - corner.y * sin(rot), corner.x * sin(rot) + corner.y * cos(rot));
        pos = p.pos + (camRight * c.x + camUp * c.y) * p.size;
    }
    VSOut o;
    o.pos = mul(gViewProj, float4(pos, 1.0));
    o.uv = corner * 0.5 + 0.5;
    o.color = p.color;
    o.info = float4(p.type, p.age, p.seed, p.emissive);
    o.rel = pos;
    o.depth = o.pos.w;
    return o;
}

float linearDepth(float z)
{
    // Reversed infinite projection: z = near / w  =>  w = near / z  (near = 0.5 m)
    return 0.5 / max(z, 1e-7);
}

float4 PSMain(VSOut i) : SV_Target
{
    int type = (int)i.info.x;
    float age = i.info.y, seed = i.info.z;
    float2 c = i.uv * 2.0 - 1.0;
    float r = length(c);
    float alpha = 0, emissive = i.info.w;
    float3 col = i.color.rgb;
    // Soft particles: fade where the billboard cuts into the scene (the depth test removes hidden parts).
    float2 suv = i.pos.xy * gScreen.zw;
    float sceneW = linearDepth(gSceneDepth.SampleLevel(sPointClamp, suv, 0));
    float soft = saturate((sceneW - i.depth) / max(2.0, 0.05 * i.depth));
    if (type == 0)
    {
        float n = fbm(c * 1.8 + seed * 13.0 + age * 0.6, 4);
        alpha = saturate((1.0 - r) * 1.6) * smoothstep(0.25, 0.65, n + (1.0 - r) * 0.35);
        // Smoke is lit by the key light, the sky and nearby fires (approximated by emissive).
        float3 lit = ambientLight(float3(0, 1, 0)) * 1.2 + gSunColor.rgb * gSunDir.w * (0.35 + 0.4 * n);
        col = col * lit + col * emissive * 2.0 * (1.0 - n);
    }
    else if (type == 1)
    {
        float n = fbm(c * 2.5 + float2(0, -age * 3.0) + seed * 7.0, 4);
        float core = saturate(1.0 - r * 1.2) * n;
        alpha = saturate(core * 2.0) * 0.6;
        col = lerp(float3(1.0, 0.25, 0.04), float3(1.0, 0.85, 0.45), core) * emissive;
        return float4(col * alpha * soft, 0.0);  // additive
    }
    else if (type == 2)
    {
        // Column: bright at the base, ragged top, falling spray later in life.
        float2 uv = i.uv;
        float x = abs(uv.x * 2.0 - 1.0);
        float n = fbm(float2(uv.x * 4.0 + seed * 9.0, uv.y * 2.0 - age * 1.5), 4);
        float shape = saturate(1.0 - x * (1.2 + uv.y)) * smoothstep(1.0, 0.55 + 0.3 * n, uv.y);
        alpha = saturate(shape * (0.6 + n)) * (1.0 - smoothstep(0.55, 1.0, age));
        col = col * (ambientLight(float3(0, 1, 0)) * 1.6 + gSunColor.rgb * gSunDir.w * 0.6) + col * emissive;
    }
    else if (type == 3 || type == 4)
    {
        float g = exp(-r * r * (type == 4 ? 18.0 : 5.0));
        return float4(col * emissive * g * lerp(soft, 1.0, type == 3 ? 0.7 : 0.0), 0.0);  // additive glow
    }
    else if (type == 6)
    {
        float x = abs(i.uv.x * 2.0 - 1.0);
        float along = i.uv.y;  // 1 at the head
        float core = exp(-x * x * 8.0) * (0.25 + 0.75 * along) * (1.0 - smoothstep(0.85, 1.0, age));
        return float4(col * emissive * core, 0.0);  // additive
    }
    else if (type == 7)
    {
        // Raindrop streak, lit by the sky.
        float x = abs(i.uv.x * 2.0 - 1.0);
        alpha = exp(-x * x * 6.0) * (0.35 + 0.65 * i.uv.y);
        col = col * (ambientLight(float3(0, 1, 0)) * 1.4 + gSunColor.rgb * gSunDir.w * 0.25);
    }
    else if (type == 8)
    {
        // Snowflake: a soft dot, a little brighter than the snow-laden air around it.
        alpha = saturate(1.6 * exp(-r * r * 2.2) - 0.1);
        col = col * (ambientLight(float3(0, 1, 0)) * 2.0 + gSunColor.rgb * gSunDir.w * 0.6);
    }
    else if (type == 9)
    {
        // Rain curtain: streaky grey veil with ragged sides, fading up into the cloud base.
        float2 uv = i.uv;
        float x = abs(uv.x * 2.0 - 1.0);
        float wobble = fbm(float2(uv.y * 2.5 + seed * 5.0, seed * 3.0 + (uv.x > 0.5 ? 7.0 : 0.0)), 3);
        float edgeW = 0.4 + 0.4 * wobble;
        float streak = fbm(float2(uv.x * 9.0 + seed * 11.0, uv.y * 0.7 + gCameraPos.w * 0.03), 4);
        float body = (1.0 - smoothstep(edgeW - 0.3, edgeW + 0.25, x)) * smoothstep(0.0, 0.04, uv.y) * (1.0 - smoothstep(0.5, 1.0, uv.y));
        alpha = body * (0.45 + 0.55 * streak) * soft * i.color.a;
        col = col * (ambientLight(float3(0, 1, 0)) * 1.2 + gSunColor.rgb * gSunDir.w * 0.15);
        // Squalls stand out darker than the haze around them: only half the fog of their distance.
        col = lerp(col, applyFog(col, i.rel, normalize(i.rel)), 0.5);
        return float4(col * alpha, alpha);
    }
    else if (type == 5)
    {
        float x = abs(i.uv.x * 2.0 - 1.0);
        float along = i.uv.y;
        float beam = exp(-x * x * 6.0) * (1.0 - along * 0.7) * (0.75 + 0.25 * valueNoise(float2(along * 10.0 - gCameraPos.w * 0.4, seed * 10.0)));
        float haze = 0.35 + gFogParams.z * 2.0 + gFogColor.w * 3000.0;
        return float4(col * beam * emissive * haze * 0.03, 0.0);
    }
    alpha *= soft * i.color.a;
    col = applyFog(col, i.rel, normalize(i.rel));
    return float4(col * alpha, alpha);
}
