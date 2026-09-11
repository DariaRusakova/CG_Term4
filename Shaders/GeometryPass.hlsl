// GeometryPass.hlsl
// Вершинный шейдер
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
    float4 lightPos;
    float4 lightColor;
    float4 cameraPos;
    float4 materialAmbient;
    float4 materialDiffuse;
    float4 materialSpecular;
    float materialShininess;
    float2 textureScale;
    float2 textureOffset;
};

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(float4(input.position, 1.0), world);
    output.position = mul(worldPos, worldViewProj);
    output.worldPos = worldPos.xyz;
    output.normal = normalize(mul(float4(input.normal, 0.0), world).xyz);
    output.texcoord = input.texcoord * textureScale + textureOffset;
    return output;
}

// Пиксельный шейдер Geometry Pass (PBR)
Texture2D<float4> AlbedoTexture : register(t0);
SamplerState Sampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texcoord : TEXCOORD2;
};

struct PSOutput {
    float4 albedo : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float2 metallicRoughness : SV_TARGET2;
    float4 worldPos : SV_TARGET3;
};

// PBR Material properties from constant buffer
cbuffer MaterialCB : register(b1) {
    float4 albedoColor;
    float metallic;
    float roughness;
    float ao;
    float padding;
};

PSOutput main(PSInput input) {
    PSOutput output;

    // Sample albedo texture (if available)
    float4 albedoSampled = AlbedoTexture.Sample(Sampler, input.texcoord);
    float3 albedo = albedoSampled.rgb * albedoColor.rgb;

    // Output albedo (A channel unused)
    output.albedo = float4(albedo, 1.0);

    // Encode normal from -1..1 to 0..1
    float3 normal = normalize(input.normal);
    output.normal = float4(normal * 0.5 + 0.5, 1.0);

    // Metallic (R) and Roughness (G) packed into float2
    // Для Sponza используем значения из материала или заглушки
    float metallicVal = metallic;
    float roughnessVal = roughness;

    // В реальном проекте здесь можно сэмплировать texture для metallic/roughness
    // Но пока используем константы из материала
    output.metallicRoughness = float2(metallicVal, roughnessVal);

    // World position
    output.worldPos = float4(input.worldPos, 1.0);

    return output;
}