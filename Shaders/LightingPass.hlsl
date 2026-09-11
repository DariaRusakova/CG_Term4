// ============================================
// LightingPass.hlsl - PBR with IBL
// ============================================

#include "PBRMath.hlsl"

// ============================================
// Textures
// ============================================
Texture2D<float4> AlbedoTex : register(t0);
Texture2D<float3> NormalTex : register(t1);
Texture2D<float2> MetallicRoughnessTex : register(t2);
Texture2D<float3> WorldPosTex : register(t3);

// IBL Textures
TextureCube IrradianceMap : register(t5);
TextureCube PrefilteredMap : register(t6);
Texture2D<float2> BRDFLUT : register(t7);

// Shadow Map (optional)
Texture2DArray<float> ShadowMap : register(t4);

// ============================================
// Samplers
// ============================================
SamplerState linearClampSampler : register(s0);
SamplerState envSampler : register(s1);

// ============================================
// Constant Buffers
// ============================================
struct LightDataGPU {
    float4 position_type;
    float4 direction;
    float4 color_intensity;
    float4 range_spotAngle;
};

cbuffer LightCB : register(b0) {
    LightDataGPU lights[16];
    uint lightCount;
    float padding[3];
};

cbuffer ShadowCB : register(b1) {
    float4x4 lightViewProj[4];
    float4 cascadeSplits;
    float4 lightDirection;
    float4 shadowBias;
    float4 textureSize;
    float4 cameraPos;
    float4 lightPos;
};

// ============================================
// Constants
// ============================================
static const float MAX_REFLECTION_LOD = 6.0f;
static const float TONE_MAPPING_ACES_A = 2.51f;
static const float TONE_MAPPING_ACES_B = 0.03f;
static const float TONE_MAPPING_ACES_C = 2.43f;
static const float TONE_MAPPING_ACES_D = 0.59f;
static const float TONE_MAPPING_ACES_E = 0.14f;

// ============================================
// Shadow Calculation (Cascaded Shadow Maps)
// ============================================
float CalculateShadow(float3 worldPos, int cascadeIndex) {
    float4 posInLightSpace = mul(float4(worldPos, 1.0f), lightViewProj[cascadeIndex]);
    float3 projCoords = posInLightSpace.xyz / posInLightSpace.w;
    projCoords.x = projCoords.x * 0.5f + 0.5f;
    projCoords.y = projCoords.y * -0.5f + 0.5f;
    
    if (projCoords.x < 0.0f || projCoords.x > 1.0f || 
        projCoords.y < 0.0f || projCoords.y > 1.0f) {
        return 1.0f;
    }
    
    float shadowDepth = ShadowMap.Sample(linearClampSampler, float3(projCoords.xy, cascadeIndex));
    float bias = shadowBias.x;
    return (projCoords.z - bias) <= shadowDepth ? 1.0f : 0.0f;
}

// ============================================
// Main Pixel Shader
// ============================================
float4 main(float4 position : SV_POSITION) : SV_TARGET {
    int3 texPos = int3(position.xy, 0);
    
    // ============================================
    // Read GBuffer
    // ============================================
    float4 albedoAO = AlbedoTex.Load(texPos);
    float3 normalData = NormalTex.Load(texPos);
    float2 metallicRoughness = MetallicRoughnessTex.Load(texPos);
    float3 worldPos = WorldPosTex.Load(texPos);
    
    // Skip invalid pixels
    if (length(worldPos) < 0.001f) {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    
    // ============================================
    // Extract PBR Parameters
    // ============================================
    float3 albedo = albedoAO.rgb;
    float ao = albedoAO.a;
    float3 N = normalize(normalData * 2.0f - 1.0f);
    float metallic = metallicRoughness.r;
    float roughness = max(metallicRoughness.g, 0.05f); // Clamp to avoid NaN
    float3 V = normalize(cameraPos.xyz - worldPos);
    float3 F0 = lerp(F0_NON_METAL, albedo, metallic);
    
    // ============================================
    // IBL (Ambient Lighting)
    // ============================================
    float3 ambient = CalculateIBL(
        N, V, albedo, metallic, roughness, ao,
        IrradianceMap, PrefilteredMap, BRDFLUT,
        envSampler, linearClampSampler,
        MAX_REFLECTION_LOD
    );
    
    // ============================================
    // Direct Lighting (Cook-Torrance)
    // ============================================
    float3 directLighting = float3(0.0f, 0.0f, 0.0f);
    
    for (uint i = 0; i < lightCount; i++) {
        uint type = (uint)lights[i].position_type.w;
        float3 L;
        float attenuation = 1.0f;
        float spotAtten = 1.0f;
        
        // Light color and intensity
        float3 lightColor = lights[i].color_intensity.rgb * lights[i].color_intensity.a;
        
        // ============================================
        // Light Type Calculations
        // ============================================
        if (type == 1) { // Directional Light
            L = normalize(-lights[i].direction.xyz);
            
            // Cascaded Shadow Map
            float depthFromCam = length(worldPos - cameraPos.xyz);
            int cascadeIndex = 3;
            if (depthFromCam <= cascadeSplits.x) cascadeIndex = 0;
            else if (depthFromCam <= cascadeSplits.y) cascadeIndex = 1;
            else if (depthFromCam <= cascadeSplits.z) cascadeIndex = 2;
            
            float shadowFactor = CalculateShadow(worldPos, cascadeIndex);
            lightColor *= shadowFactor;
            
        } else if (type == 0) { // Point Light
            float3 lightVec = lights[i].position_type.xyz - worldPos;
            float dist = length(lightVec);
            L = normalize(lightVec);
            
            // Inverse square falloff with smooth cutoff
            float range = lights[i].range_spotAngle.x;
            attenuation = saturate(1.0f - (dist * dist) / (range * range));
            attenuation *= attenuation;
            
        } else if (type == 2) { // Spot Light
            float3 lightVec = lights[i].position_type.xyz - worldPos;
            float dist = length(lightVec);
            L = normalize(lightVec);
            
            // Distance attenuation
            float range = lights[i].range_spotAngle.x;
            attenuation = saturate(1.0f - (dist * dist) / (range * range));
            attenuation *= attenuation;
            
            // Spot angle attenuation
            float spotCos = dot(-L, normalize(lights[i].direction.xyz));
            float cutoff = cos(lights[i].range_spotAngle.y);
            spotAtten = smoothstep(cutoff, cutoff + 0.2f, spotCos);
        }
        
        // ============================================
        // Cook-Torrance BRDF
        // ============================================
        float3 brdf = CookTorranceBRDF(N, V, L, albedo, metallic, roughness, F0);
        directLighting += brdf * lightColor * attenuation * spotAtten;
    }
    
    // ============================================
    // Final Color
    // ============================================
    float3 finalColor = ambient + directLighting;
    
    // ============================================
    // ACES Filmic Tone Mapping
    // ============================================
    float3 acesInput = finalColor;
    float3 acesOutput = saturate(
        (acesInput * (TONE_MAPPING_ACES_A * acesInput + TONE_MAPPING_ACES_B)) /
        (acesInput * (TONE_MAPPING_ACES_C * acesInput + TONE_MAPPING_ACES_D) + TONE_MAPPING_ACES_E)
    );
    
    // Gamma Correction
    finalColor = pow(acesOutput, float3(1.0f / 2.2f, 1.0f / 2.2f, 1.0f / 2.2f));
    
    return float4(finalColor, 1.0f);
}