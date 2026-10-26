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
    float3 modelNrm : TEXCOORD5;
};

// Tiling material layers (tools/textures/make_ship_textures.py): RGB albedo detail (0.5 = keep the
// paint), A metal mask; RG normal, B roughness factor (0.5 = x1), A cavity.
Texture2DArray<float4> gShipAlbedo : register(t16);
Texture2DArray<float4> gShipSurface : register(t17);

struct MaterialLayer
{
    int layer;    // -1: plain paint
    float tile;   // metres per texture tile
    float bump;   // normal map strength
};

MaterialLayer materialLayer(uint mat)
{
    MaterialLayer m;
    m.layer = -1; m.tile = 4.0; m.bump = 1.0;
    switch (mat)
    {
        case 0:  m.layer = 0; m.tile = 8.0; m.bump = 3.0; break;            // hull plating
        case 1:  case 2: m.layer = 7; m.tile = 4.0; m.bump = 2.0; break;    // bottom, boot topping
        case 3:  case 14: m.layer = 2; m.tile = 4.0; m.bump = 2.0; break;   // teak deck, wood
        case 4:  m.layer = 1; m.tile = 4.0; m.bump = 3.0; break;            // superstructure plating
        case 5:  m.layer = 6; m.tile = 2.0; m.bump = 1.0; break;            // gun metal
        case 7:  m.layer = 5; m.tile = 1.5; m.bump = 2.0; break;            // canvas
        case 9:  m.layer = 1; m.tile = 2.0; m.bump = 1.0; break;            // aircraft skin
        case 11: m.layer = 3; m.tile = 4.0; m.bump = 2.0; break;            // IJN linoleum
        case 12: m.layer = 4; m.tile = 4.0; m.bump = 2.0; break;            // steel deck, non-skid
        case 13: m.layer = 8; m.tile = 1.0; m.bump = 1.0; break;            // grating
    }
    return m;
}

struct Surface
{
    float3 albedo;   // multiplier (1 = paint)
    float metalMask;
    float3 n;        // perturbed model-space normal
    float rough;     // multiplier
    float cavity;
};

// Triplanar projection in model space. Side projections keep v pointing down the ship's side so rust
// runs downwards; the mirrored sides flip u.
Surface triplanar(float3 m, float3 nm, MaterialLayer ml)
{
    float3 w = pow(abs(nm), 6.0);
    w /= max(w.x + w.y + w.z, 1e-4);
    float3 sg = float3(nm.x < 0 ? -1.0 : 1.0, nm.y < 0 ? -1.0 : 1.0, nm.z < 0 ? -1.0 : 1.0);
    float2 uvX = float2(m.y * sg.x, -m.z) / ml.tile;
    float2 uvY = float2(-m.x * sg.y, -m.z) / ml.tile;
    float2 uvZ = float2(m.x, m.y * sg.z) / ml.tile;
    float4 aX = float4(0.5, 0.5, 0.5, 0), aY = aX, aZ = aX;
    float4 sX = float4(0.5, 0.5, 0.5, 1), sY = sX, sZ = sX;
    if (w.x > 0.02) { aX = gShipAlbedo.Sample(sAniso, float3(uvX, ml.layer)); sX = gShipSurface.Sample(sAniso, float3(uvX, ml.layer)); }
    if (w.y > 0.02) { aY = gShipAlbedo.Sample(sAniso, float3(uvY, ml.layer)); sY = gShipSurface.Sample(sAniso, float3(uvY, ml.layer)); }
    if (w.z > 0.02) { aZ = gShipAlbedo.Sample(sAniso, float3(uvZ, ml.layer)); sZ = gShipSurface.Sample(sAniso, float3(uvZ, ml.layer)); }
    float4 a = aX * w.x + aY * w.y + aZ * w.z;
    float4 sf = sX * w.x + sY * w.y + sZ * w.z;
    Surface o;
    o.albedo = a.rgb * 2.0;
    o.metalMask = a.a;
    o.rough = sf.b * 2.0;
    o.cavity = sf.a;
    // Tangent frames (T = dP/du, B = dP/dv) of the three projections; tangent normal = (-dh/du, +dh/dv).
    float2 tX = (sX.xy * 2.0 - 1.0) * ml.bump, tY = (sY.xy * 2.0 - 1.0) * ml.bump, tZ = (sZ.xy * 2.0 - 1.0) * ml.bump;
    float3 dX = float3(0, sg.x, 0) * tX.x - float3(0, 0, -1) * tX.y;
    float3 dY = float3(-sg.y, 0, 0) * tY.x - float3(0, 0, -1) * tY.y;
    float3 dZ = float3(1, 0, 0) * tZ.x - float3(0, sg.z, 0) * tZ.y;
    o.n = normalize(nm + dX * w.x + dY * w.y + dZ * w.z);
    return o;
}

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
    o.modelNrm = v.nrm;
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
    float3 nm = normalize(i.modelNrm);
    // Thin parts (shields, platforms, lofted plating) are drawn double-sided: light the visible side.
    if (dot(N, V) < 0.0) { N = -N; nm = -nm; }
    float3 albedo = i.color.rgb;
    float rough = i.params.x, metal = i.params.y;
    uint mat = (uint)round(i.params.z * 255.0);
    float emissive = i.params.w;
    float3 m = i.model;
    float seed = gObjParams.x;
    float cavity = 1.0;

    // Material textures.
    MaterialLayer ml = materialLayer(mat);
    if (ml.layer >= 0)
    {
        Surface sf = triplanar(m, nm, ml);
        albedo = lerp(albedo * sf.albedo, saturate(sf.albedo * 0.5), sf.metalMask);
        metal = max(metal, sf.metalMask);
        rough = saturate(rough * sf.rough);
        cavity = sf.cavity;
        N = normalize(mul((float3x3)gWorld, sf.n));
    }

    // Weathering in model space (stable as the ship moves).
    float grime = fbm(m.xz * 0.35 + seed, 3);
    if (mat == 0 || mat == 4)
    {
        // Rust and grime running down from the deck edge and the scuppers.
        float streak = saturate(pow(valueNoise(float2(m.x * 2.7 + seed, 0.0)), 6.0) * 3.0) * saturate(1.0 - abs(m.z - gObjParams.w) * 0.25);
        albedo = lerp(albedo, albedo * float3(0.62, 0.42, 0.30), streak * 0.5);
        // Waterline: salt bloom above, oily grime at the boot topping.
        float salt = saturate(1.0 - abs(m.z - 1.6) * 0.8) * fbm(m.xz * float2(0.9, 2.5) + seed * 2.0, 3);
        albedo = lerp(albedo, float3(0.55, 0.56, 0.55), salt * 0.18);
        albedo *= lerp(1.0, 0.78, saturate((1.2 - m.z) * 0.8) * grime);
        // Portholes along the hull below the deck edge.
        if (mat == 0 && m.z > gObjParams.w - 2.8 && m.z < gObjParams.w - 1.6)
        {
            float px = frac(m.x / 2.4) - 0.5, pz = (m.z - (gObjParams.w - 2.2)) / 0.35;
            float d = px * px * 40.0 + pz * pz;
            if (d < 1.0 && abs(m.x) < gObjParams.z * 0.38) { albedo = d < 0.62 ? float3(0.01, 0.012, 0.015) : albedo * 0.45; rough = d < 0.62 ? 0.1 : 0.4; metal = d < 0.62 ? 0.0 : 0.6; }
        }
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
    float3 col = brdf(albedo, rough, metal, N, V, L, gSunColor.rgb * gSunDir.w * shadow) * lerp(1.0, cavity, 0.5);
    float ao = lerp(0.55, 1.0, saturate((m.z + 2.0) / 12.0)) * cavity;
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
