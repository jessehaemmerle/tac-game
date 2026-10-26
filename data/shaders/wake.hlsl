// Renders ship wakes, bow waves and splash rings into the wake map (top-down, 4 km around the camera).
cbuffer WakePass : register(b2)
{
    float4 gWakeCenter;  // xy = world xz centre, z = half size (m)
};

struct VSIn { float2 pos : POSITION; float foam : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float foam : TEXCOORD0; };

VSOut VSMain(VSIn v)
{
    VSOut o;
    float2 p = (v.pos - gWakeCenter.xy) / gWakeCenter.z;
    o.pos = float4(p.x, p.y, 0.5, 1.0);
    o.foam = v.foam;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    return float4(i.foam, 0.0, 0.0, 0.0);
}

// Clears the map and stores its centre in texel (0,0).zw for the ocean shader.
float4 PSClear(float4 pos : SV_Position) : SV_Target
{
    return (pos.x < 1.0 && pos.y < 1.0) ? float4(0, 0, gWakeCenter.xy) : float4(0, 0, gWakeCenter.xy);
}

float4 VSFull(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0.5, 1.0);
}
