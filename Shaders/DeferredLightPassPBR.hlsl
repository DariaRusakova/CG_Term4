// DeferredLightPassPBR.hlsl
// Полноэкранный квад для PBR освещения

#include "PBRMath.hlsl"

// ============================================================
// Входные данные
// ============================================================
struct VSInput {
    float3 position : POSITION;
    float2 texcoord : TEXCOORD;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

VSOutput VS_Main(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.texcoord = input.texcoord;
    return output;
}

// ============================================================
// GBuffer текстуры (из Combined SRV Heap)
// ============================================================
Texture2D<float4> AlbedoTex : register(t0);
Texture2D<float4> NormalTex : register(t1);
Texture2D<float2> MetallicRoughnessTex : register(t2);
Texture2D<float4> WorldPosTex : register(t3);

// IBL текстуры
TextureCube IrradianceMap : register(t4);
TextureCube PrefilteredMap : register(t5);
Texture2D<float2> BRDFLUT : register(t6);

// Сэмплеры
SamplerState DefaultSampler : register(s0);
SamplerState PrefilteredSampler : register(s1);
SamplerState BRDFSampler : register(s2);

// ============================================================
// Структуры данных
// ============================================================
struct LightData {
    float4 position_type;     // xyz = position, w = type (0=point,1=directional,2=spot)
    float4 direction;         // xyz = direction, w = unused
    float4 color_intensity;   // xyz = color, w = intensity
    float4 range_spotAngle;   // x = range, y = spotAngle (cos), z = unused, w = unused
};

cbuffer LightCB : register(b0) {
    LightData lights[16];
    uint lightCount;
    float3 padding;
}

cbuffer ShadowCB : register(b1) {
    float4x4 lightViewProj[4];
    float4 cascadeSplits;
    float4 lightDirection;
    float4 shadowBias;
    float4 textureSize;
    float4 cameraPos;
    float4 lightPos;
}

// ============================================================
// Функция расчета теней (из существующего кода)
// ============================================================
float CalculateShadow(float3 worldPos, int cascadeIndex, Texture2DArray<float> shadowMap, SamplerState shadowSampler) {
    float4 posInLightSpace = mul(float4(worldPos, 1.0), lightViewProj[cascadeIndex]);
    float3 projCoords = posInLightSpace.xyz / posInLightSpace.w;
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = projCoords.y * -0.5 + 0.5;

    if (projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 1.0;
    }

    float shadowDepth = shadowMap.Sample(shadowSampler, float3(projCoords.xy, cascadeIndex));

    float distToLight = length(worldPos - lightPos.xyz);
    float maxDist = 200.0f;
    float normalizedDist = saturate(distToLight / maxDist);

    float bias = shadowBias.x;
    return (normalizedDist - bias) <= shadowDepth ? 1.0 : 0.0;
}

// ============================================================
// Пиксельный шейдер PBR
// ============================================================
float4 PS_Main(VSOutput input) : SV_TARGET {
    // 1. Читаем G-Buffer
    int3 texPos = int3(input.position.xy, 0);
    float3 albedo = AlbedoTex.Load(texPos).rgb;
    float4 normalData = NormalTex.Load(texPos);
    float2 metallicRoughness = MetallicRoughnessTex.Load(texPos).rg;
    float4 worldPosData = WorldPosTex.Load(texPos);
    
    // Проверка на пустой пиксель
    if (length(worldPosData.xyz) < 0.001) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    
    // 2. Распаковываем данные
    float3 N = normalize(normalData.xyz * 2.0 - 1.0);
    float3 worldPos = worldPosData.xyz;
    float metallic = metallicRoughness.x;
    float roughness = metallicRoughness.y;
    
    // 3. Вектор камеры
    float3 V = normalize(cameraPos.xyz - worldPos);
    
    // 4. Расчет теней
    float depthFromCam = length(worldPos - cameraPos.xyz);
    int cascadeIndex = 0;
    if (depthFromCam > cascadeSplits.x) cascadeIndex = 1;
    if (depthFromCam > cascadeSplits.y) cascadeIndex = 2;
    if (depthFromCam > cascadeSplits.z) cascadeIndex = 3;
    
    float shadowFactor = 1.0;
    // Если есть теневой map, рассчитываем тень
    // shadowFactor = CalculateShadow(worldPos, cascadeIndex, ShadowMap, ShadowSampler);
    
    // 5. F0 (Fresnel at normal incidence)
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    
    // 6. Direct Lighting (PBR)
    float3 Lo = float3(0.0, 0.0, 0.0);
    
    for (uint i = 0; i < lightCount; i++) {
        uint type = (uint)lights[i].position_type.w;
        float3 L;
        float attenuation = 1.0;
        float spotAtten = 1.0;
        float3 lightColor = lights[i].color_intensity.rgb * lights[i].color_intensity.a;
        
        if (type == 1) { // Directional
            L = normalize(-lights[i].direction.xyz);
            float shadow = shadowFactor; // Применяем тени только для направленного света
            Lo += CookTorranceBRDF(N, V, L, F0, roughness, metallic, albedo, lightColor, attenuation) * shadow;
        }
        else if (type == 0) { // Point
            float3 lightVec = lights[i].position_type.xyz - worldPos;
            float dist = length(lightVec);
            L = normalize(lightVec);
            attenuation = saturate(1.0 - dist / lights[i].range_spotAngle.x);
            attenuation *= attenuation;
            Lo += CookTorranceBRDF(N, V, L, F0, roughness, metallic, albedo, lightColor, attenuation);
        }
        else if (type == 2) { // Spot
            float3 lightVec = lights[i].position_type.xyz - worldPos;
            float dist = length(lightVec);
            L = normalize(lightVec);
            attenuation = saturate(1.0 - dist / lights[i].range_spotAngle.x);
            attenuation *= attenuation;
            
            float spotCos = dot(-L, normalize(lights[i].direction.xyz));
            float cutoff = cos(lights[i].range_spotAngle.y);
            spotAtten = smoothstep(cutoff, cutoff + 0.2, spotCos);
            
            Lo += CookTorranceBRDF(N, V, L, F0, roughness, metallic, albedo, lightColor, attenuation * spotAtten);
        }
    }
    
    // 7. IBL (Image Based Lighting)
    // 7a. Diffuse IBL
    float3 kS_IBL = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    float3 kD_IBL = 1.0 - kS_IBL;
    kD_IBL *= (1.0 - metallic);
    
    float3 irradiance = IrradianceMap.Sample(DefaultSampler, N).rgb;
    float3 diffuseIBL = kD_IBL * albedo * irradiance * 1.2;
    
    // 7b. Specular IBL
    float3 R = reflect(-V, N);
    float mipLevel = roughness * 7.0; // 0-7 mip levels (8 total)
    float3 prefilteredColor = PrefilteredMap.SampleLevel(PrefilteredSampler, R, mipLevel).rgb;
    float2 brdf = BRDFLUT.Sample(BRDFSampler, float2(max(dot(N, V), 0.0), roughness)).rg;
    float3 specularIBL = prefilteredColor * (F0 * brdf.x + brdf.y);
    
    // 8. Ambient Occlusion (пока не используется, ставим 1.0)
    float ao = 1.0;
    
    // 9. Финальный цвет
    float3 finalColor = Lo + (diffuseIBL + specularIBL) * ao;
    
    // 10. Tone Mapping (ACES Filmic)
    // ACES Filmic Tone Mapping
    finalColor = finalColor * 0.95;
    finalColor = (finalColor * (2.51f * finalColor + 0.03f)) / 
                 (finalColor * (2.43f * finalColor + 0.59f) + 0.14f);
    
    // Gamma correction
    finalColor = pow(finalColor, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));
    
    return float4(finalColor, 1.0);
}