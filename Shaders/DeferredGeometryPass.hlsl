// Shaders/DeferredGeometryPass.hlsl
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texcoord : TEXCOORD2;
};

cbuffer SceneCB : register(b0) {
    float4x4 worldViewProj;
    float4x4 world;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = mul(float4(input.position, 1.0), worldViewProj);
    output.worldPos = mul(float4(input.position, 1.0), world).xyz;
    output.normal = normalize(mul(input.normal, (float3x3)world));
    output.texcoord = input.texcoord;
    return output;
}

// ============================================
// PIXEL SHADER
// ============================================
Texture2D diffuseTex : register(t0);
SamplerState samplerState : register(s0);

struct PSOutput {
    float4 albedoMetallic : SV_TARGET0;
    float4 normalRoughness : SV_TARGET1;
    float4 worldPos : SV_TARGET2;
};

cbuffer MaterialCB : register(b1) {
    float4 materialAmbient;
    float4 materialDiffuse;
    float4 materialSpecular;
    float materialShininess;
    float materialMetallic;
    float materialRoughness;
    float2 padding;
};

PSOutput main(VSOutput input) {
    PSOutput output;
    
    // Sample texture
    float4 albedo = diffuseTex.Sample(samplerState, input.texcoord);
    
    // If texture not available, use material color
    if (albedo.r + albedo.g + albedo.b < 0.001) {
        albedo = materialDiffuse;
    }
    
    // Pack: Albedo (RGB) + Metallic (A)
    output.albedoMetallic = float4(albedo.rgb, materialMetallic);
    
    // Pack: Normal (XYZ) + Roughness (W)
    // Normal is already in world space from vertex shader
    float3 normal = normalize(input.normal);
    output.normalRoughness = float4(normal * 0.5 + 0.5, materialRoughness);
    
    // World Position
    output.worldPos = float4(input.worldPos, 1.0);
    
    return output;
}