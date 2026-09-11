#pragma once

namespace Shaders {
    static const char* LightPassVS = R"(
        struct VSInput {
            float3 position : POSITION;
            float2 texcoord : TEXCOORD;
        };
        
        struct VSOutput {
            float4 position : SV_POSITION;
            float2 texcoord : TEXCOORD;
        };
        
        VSOutput main(VSInput input) {
            VSOutput output;
            output.position = float4(input.position, 1.0);
            output.texcoord = input.texcoord;
            return output;
        }
    )";

    static const char* LightPassPS = R"(
        #include "PBRMath.hlsl"

        // === G-Buffer текстуры ===
        Texture2D<float4> AlbedoTex : register(t0);
        Texture2D<float4> WorldPosTex : register(t1);
        Texture2D<float4> NormalTex : register(t2);
        Texture2D<float4> PBRTex : register(t3);    // metallic (R), roughness (G), ao (B)

        // === Shadow Map ===
        Texture2DArray<float> ShadowMap : register(t4);
        SamplerState ShadowSampler : register(s1);

        // === IBL текстуры ===
        TextureCube IrradianceMap : register(t5);
        SamplerState IrradianceSampler : register(s2);
        TextureCube PrefilteredMap : register(t6);
        SamplerState PrefilteredSampler : register(s3);
        Texture2D BRDFLUT : register(t7);
        SamplerState BRDFLUTSampler : register(s4);

        // === Константные буферы ===
        cbuffer LightCB : register(b0) {
            float4 lights_pos_type[16];
            float4 lights_dir[16];
            float4 lights_col_int[16];
            float4 lights_range_angle[16];
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

        cbuffer IBLConstants : register(b2) {
            float4 irradianceIntensity;
            float4 specularIntensity;
            float4 prefilteredMipCount;
        }

        // === Вспомогательные функции ===

        // Вычисление тени
        float CalculateShadow(float3 worldPos, int cascadeIndex) {
            float4 posInLightSpace = mul(float4(worldPos, 1.0), lightViewProj[cascadeIndex]);
            float3 projCoords = posInLightSpace.xyz / posInLightSpace.w;
            projCoords.x = projCoords.x * 0.5 + 0.5;
            projCoords.y = projCoords.y * -0.5 + 0.5;
        
            if (projCoords.x < 0.0 || projCoords.x > 1.0 || 
                projCoords.y < 0.0 || projCoords.y > 1.0) {
                return 1.0;
            }
        
            float shadowDepth = ShadowMap.Sample(ShadowSampler, float3(projCoords.xy, cascadeIndex));
        
            float distToLight = length(worldPos - lightPos.xyz);
            float maxDist = 200.0f;
            float normalizedDist = saturate(distToLight / maxDist);
        
            float bias = shadowBias.x;
            return (normalizedDist - bias) <= shadowDepth ? 1.0 : 0.0;
        }

        // === Основной шейдер ===

        float4 main(float4 position : SV_POSITION) : SV_TARGET {
            int2 texPos = int2(position.xy);
            
            // Читаем G-Buffer
            float4 albedo = AlbedoTex.Load(int3(texPos, 0));
            float4 worldPos = WorldPosTex.Load(int3(texPos, 0));
            float4 normalData = NormalTex.Load(int3(texPos, 0));
            float4 pbrData = PBRTex.Load(int3(texPos, 0));
            
            // Пропускаем пустые пиксели
            if (length(worldPos.xyz) < 0.001) {
                return float4(0.0, 0.0, 0.0, 1.0);
            }
            
            // Декодируем данные
            float3 N = normalize(normalData.xyz * 2.0 - 1.0);
            float3 V = normalize(cameraPos.xyz - worldPos.xyz);
            float3 albedoColor = albedo.rgb;
            float metallic = pbrData.r;
            float roughness = pbrData.g;
            float ao = pbrData.b;
            
            // F0 для диэлектриков (по умолчанию 0.04)
            float3 F0 = float3(0.04, 0.04, 0.04);
            
            // Выбор каскада для теней
            float depthFromCam = length(worldPos.xyz - cameraPos.xyz);
            int cascadeIndex = 0;
            if (depthFromCam > cascadeSplits.x) cascadeIndex = 1;
            if (depthFromCam > cascadeSplits.y) cascadeIndex = 2;
            if (depthFromCam > cascadeSplits.z) cascadeIndex = 3;
            
            float shadowFactor = CalculateShadow(worldPos.xyz, cascadeIndex);
            
            // === Прямой свет ===
            float3 finalColor = float3(0, 0, 0);
            
            for (uint i = 0; i < lightCount; i++) {
                uint type = (uint)lights_pos_type[i].w;
                float3 L;
                float attenuation = 1.0;
                float spotAtten = 1.0;
                float3 lightColor = lights_col_int[i].rgb * lights_col_int[i].a;
                
                if (type == 1) { // Directional
                    L = normalize(-lights_dir[i].xyz);
                    float shadowMult = (i == 0) ? shadowFactor : 1.0f; // Тени только для первого света
                    float3 brdf = CookTorranceBRDF(N, V, L, lightColor, albedoColor, metallic, roughness, ao, F0);
                    finalColor += brdf * shadowMult;
                }
                else if (type == 0) { // Point
                    float3 lightVec = lights_pos_type[i].xyz - worldPos.xyz;
                    float dist = length(lightVec);
                    L = normalize(lightVec);
                    attenuation = saturate(1.0 - dist / lights_range_angle[i].x);
                    attenuation *= attenuation;
                    float3 brdf = CookTorranceBRDF(N, V, L, lightColor, albedoColor, metallic, roughness, ao, F0);
                    finalColor += brdf * attenuation;
                }
                else if (type == 2) { // Spot
                    float3 lightVec = lights_pos_type[i].xyz - worldPos.xyz;
                    float dist = length(lightVec);
                    L = normalize(lightVec);
                    attenuation = saturate(1.0 - dist / lights_range_angle[i].x);
                    attenuation *= attenuation;
                    float spotCos = dot(-L, normalize(lights_dir[i].xyz));
                    float cutoff = cos(lights_range_angle[i].y);
                    spotAtten = smoothstep(cutoff, cutoff + 0.2, spotCos);
                    float3 brdf = CookTorranceBRDF(N, V, L, lightColor, albedoColor, metallic, roughness, ao, F0);
                    finalColor += brdf * attenuation * spotAtten;
                }
            }
            
            // === IBL ===
            // Предполагаем, что IBL текстуры загружены
            // В реальном проекте нужно передавать их через дескрипторы
            
            // Если IBL доступен, добавляем ambient
            // Для примера используем простой ambient
            float3 ambient = albedoColor * 0.03f * ao;
            finalColor += ambient;
            
            // HDR тонмаппинг (Reinhard)
            finalColor = finalColor / (finalColor + float3(1.0, 1.0, 1.0));
            
            // Гамма-коррекция
            finalColor = pow(finalColor, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));
            
            return float4(finalColor, 1.0);
        }
    )";
}