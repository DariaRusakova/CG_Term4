// Shaders/PBRMath.hlsl
// Математика PBR (Cook-Torrance)

#ifndef PBRMATH_HLSL
#define PBRMATH_HLSL

// === Константы ===
static const float PI = 3.14159265359f;

// === Функции распределения ===

// Distribution GGX (Trowbridge-Reitz)
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;
    
    return num / denom;
}

// === Функции затенения ===

// Geometry Smith (GGX)
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;
    
    float num = NdotV;
    float denom = NdotV * (1.0f - k) + k;
    
    return num / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// === Функция Френеля ===

// Fresnel-Schlick
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

// Fresnel-Schlick с учетом roughness (для IBL)
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0) - F0) * pow(1.0f - cosTheta, 5.0f);
}

// === Основная функция PBR ===

// Вычисляет цвет по модели Cook-Torrance
float3 CookTorranceBRDF(
    float3 N,           // Нормаль
    float3 V,           // Направление на камеру
    float3 L,           // Направление на свет
    float3 lightColor,  // Цвет света
    float3 albedo,      // Альбедо
    float metallic,     // Металличность (0-1)
    float roughness,    // Шероховатость (0-1)
    float ao,           // Ambient occlusion (0-1)
    float3 F0           // F0 (базовый цвет для диэлектриков)
)
{
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0f);
    float NdotV = max(dot(N, V), 0.0f);
    float HdotV = max(dot(H, V), 0.0f);
    
    // Вычисляем F0 с учетом металличности
    float3 F0final = lerp(F0, albedo, metallic);
    
    // Вычисляем коэффициенты
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(HdotV, F0final);
    
    // Cook-Torrance BRDF
    float3 numerator = NDF * G * F;
    float denominator = 4.0f * NdotV * NdotL + 0.0001f; // + epsilon чтобы избежать деления на 0
    float3 specular = numerator / denominator;
    
    // Диффузная часть (для металлов равна 0)
    float3 kS = F; // Отраженная часть
    float3 kD = (1.0f - kS) * (1.0f - metallic); // Диффузная часть
    
    float3 diffuse = kD * albedo / PI;
    
    // Итоговый цвет
    float3 result = (diffuse + specular) * lightColor * NdotL * ao;
    
    return result;
}

// === IBL функции ===

// Вычисляет diffuse IBL
float3 DiffuseIBL(float3 N, float3 V, float3 albedo, float3 F0, float metallic, float roughness, float ao,
    TextureCube irradianceMap, SamplerState linearSampler)
{
    float3 Nnormalized = normalize(N);
    float3 irradiance = irradianceMap.Sample(linearSampler, Nnormalized).rgb;
    
    float3 F0final = lerp(F0, albedo, metallic);
    float3 kS = FresnelSchlickRoughness(max(dot(N, V), 0.0f), F0final, roughness);
    float3 kD = (1.0f - kS) * (1.0f - metallic);
    
    float3 diffuse = irradiance * albedo * kD;
    return diffuse * ao;
}

// Вычисляет specular IBL
float3 SpecularIBL(float3 N, float3 V, float3 albedo, float3 F0, float metallic, float roughness, float ao,
    TextureCube prefilteredMap, SamplerState prefilteredSampler,
    Texture2D brdfLUT, SamplerState brdfSampler,
    float prefilteredMipCount)
{
    float3 Nnormalized = normalize(N);
    float3 R = reflect(-V, Nnormalized);
    
    // Выбор mip уровня по roughness
    float mipLevel = roughness * prefilteredMipCount;
    float3 prefilteredColor = prefilteredMap.SampleLevel(prefilteredSampler, R, mipLevel).rgb;
    
    float3 F0final = lerp(F0, albedo, metallic);
    float3 kS = F0final;
    
    // BRDF LUT
    float2 brdf = brdfLUT.Sample(brdfSampler, float2(max(dot(N, V), 0.0f), roughness)).rg;
    float3 specular = prefilteredColor * (kS * brdf.x + brdf.y);
    
    return specular * ao;
}

// === Полная PBR с IBL ===

float3 PBRWithIBL(
    float3 N,           // Нормаль
    float3 V,           // Направление на камеру
    float3 L,           // Направление на свет (для прямого света)
    float3 lightColor,  // Цвет прямого света
    float3 albedo,      // Альбедо
    float metallic,     // Металличность (0-1)
    float roughness,    // Шероховатость (0-1)
    float ao,           // Ambient occlusion (0-1)
    float3 F0,          // F0 (базовый цвет)
    // IBL текстуры
    TextureCube irradianceMap,
    SamplerState irradianceSampler,
    TextureCube prefilteredMap,
    SamplerState prefilteredSampler,
    Texture2D brdfLUT,
    SamplerState brdfSampler,
    float prefilteredMipCount,
    bool useIBL = true
)
{
    // Прямой свет
    float3 directLight = CookTorranceBRDF(N, V, L, lightColor, albedo, metallic, roughness, ao, F0);
    
    // IBL
    float3 ambientLight = float3(0, 0, 0);
    if (useIBL) {
        ambientLight = DiffuseIBL(N, V, albedo, F0, metallic, roughness, ao, irradianceMap, irradianceSampler);
        ambientLight += SpecularIBL(N, V, albedo, F0, metallic, roughness, ao, prefilteredMap, prefilteredSampler, brdfLUT, brdfSampler, prefilteredMipCount);
    }
    
    return directLight + ambientLight;
}

#endif // PBRMATH_HLSL