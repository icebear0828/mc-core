
cbuffer Root : register(b0) {
    row_major float4x4 view_proj;
    row_major float4x4 world;
    float4 scene; // x: depth * view z constant, y: relative bias, z: absolute bias (metres), w: mode (0 off, 1 occlude, 2 debug colours)
    float4 dims;  // x, y: depth texture size, z, w: back buffer size
    float4 tint;  // rgb: colour the figure is mixed towards, a: how much (Minecraft's red hurt flash)
};
Texture2D<float2> scene_depth : register(t0);
Texture2D<float4> skin : register(t1);
SamplerState skin_sampler : register(s0); // point, clamp (static sampler)
RWByteAddressBuffer stats : register(u1); // 64 log2 bins of K = depth * view z (u0 would collide with the pixel shader output) // [0] sum of K * 1e6, [4] pixel count, [8] max, [12] min (K = depth * view z)

struct VSIn  { float3 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float3 wpos : TEXCOORD1; };

VSOut VSMain(VSIn i) {
    VSOut o;
    float4 w = mul(float4(i.pos, 1.0), world);
    o.pos = mul(w, view_proj);
    o.uv = i.uv;
    o.wpos = w.xyz;
    return o;
}

struct FullOut { float4 pos : SV_POSITION; };

FullOut VSFull(uint id : SV_VertexID) {
    FullOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

// Debug: the scene depth as grey (scene.x is the gain, sqrt spreads the small reverse-Z values). Exactly zero (cleared,
// or nothing drawn there) is dark blue.
float4 PSDepthView(FullOut i) : SV_Target {
    int2 texel = int2(i.pos.xy / dims.zw * dims.xy);
    float gd = scene_depth.Load(int3(texel, 0)).r;
    if (gd <= 0.0) return float4(0.0, 0.0, 0.25, 0.9);
    float g = sqrt(saturate(gd * scene.x));
    return float4(g, g, g, 0.9);
}

float4 PSMain(VSOut i) : SV_Target {
    if (scene.w > 0.5) {
        // Reverse-Z game depth: nearer = larger, 0 = far. SV_POSITION.w is view-space z in D3D12 pixel shader.
        int2 texel = int2(i.pos.xy / dims.zw * dims.xy);
        float gd = scene_depth.Load(int3(texel, 0)).r;
        float steve_z = i.pos.w;
        if (scene.w > 2.5) {
            // Measurement: the constant K = depth * view z over the figure's pixels (the native character is drawn
            // in the same place, so the scene depth here is its own distance).
            if (gd > 0.0) {
                float k = gd * steve_z;
                uint bin = (uint)clamp((log2(k) - log2(0.0005)) * 8.0, 0.0, 63.0);
                uint old;
                stats.InterlockedAdd(bin * 4, 1, old);
            }
            return float4(1.0, 1.0, 0.0, 1.0);
        }
        if (scene.w > 1.5) {
            // Calibration view: the scene's distance at this pixel (scene.x / depth) over the figure's own distance.
            if (gd <= 0.0) return float4(1.0, 0.0, 1.0, 1.0);                 // magenta: no depth here (far plane or bad read)
            float r = (scene.x / gd) / steve_z;
            if (r < 0.5) return float4(1.0, 0.0, 0.0, 1.0);                   // red: the scene is much nearer
            if (r < 0.92) return float4(1.0, 0.55, 0.0, 1.0);                 // orange: the scene is somewhat nearer
            if (r < 1.08) return float4(1.0, 1.0, 0.0, 1.0);                  // yellow: same distance (the constant is right)
            if (r < 2.0) return float4(0.0, 0.8, 0.2, 1.0);                   // green: the scene is farther
            return float4(0.0, 0.4, 1.0, 1.0);                                // blue: far behind
        }
        // Mode 1.25 (the placed blocks): within 2.2 m of the camera nothing in the scene can hide a block. The game's depth there holds the
        // player's own hidden body (first person looks down onto it), which would make a block under or next to the player vanish.
        const float near_skip = (scene.w > 1.1 && scene.w < 1.4) ? 2.2 : 0.0;
        if (steve_z > near_skip && gd > 0.0 && scene.x / gd < steve_z * (1.0 - scene.y) - scene.z) discard;
    }
    // Minecraft's fixed face shading (top 1.0, north/south 0.8, east/west 0.6), from the face normal.
    float3 n = normalize(cross(ddx(i.wpos), ddy(i.wpos)));
    float3 a = abs(n);
    float face = a.y > 0.7 ? 1.0 : (a.z > a.x ? 0.82 : 0.68);
    float4 texel = skin.Sample(skin_sampler, i.uv);
    if (texel.a < 0.5 && tint.a < 0.99) discard; // the hat / jacket layers are cut out of the skin (a fully opaque tint is a solid prop)
    return float4(lerp(texel.rgb * face, tint.rgb, tint.a), 1.0);
}

// The blob shadow under a figure: a flat disc, uv = (x, z) of the unit circle, tint.a = strength. Hidden by the scene like the blocks.
float4 PSShadow(VSOut i) : SV_Target {
    if (scene.w > 0.5) {
        int2 texel = int2(i.pos.xy / dims.zw * dims.xy);
        float gd = scene_depth.Load(int3(texel, 0)).r;
        float steve_z = i.pos.w;
        const float near_skip = (scene.w > 1.1 && scene.w < 1.4) ? 2.2 : 0.0;
        if (steve_z > near_skip && gd > 0.0 && scene.x / gd < steve_z * (1.0 - scene.y) - scene.z) discard;
    }
    float r2 = dot(i.uv, i.uv);
    return float4(0.0, 0.0, 0.0, tint.a * saturate(1.0 - r2));
}
