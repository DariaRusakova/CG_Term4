#pragma once

namespace Shaders {
    // Hull Shader (Tessellation Control Shader)
    static const char* TessHS = R"(
struct HS_INPUT {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float3 tangent  : TANGENT;
};

struct HS_CONSTANT_OUTPUT {
    float edges[3]  : SV_TessFactor;
    float inside    : SV_InsideTessFactor;
};

struct HS_OUTPUT {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float3 tangent  : TANGENT;
};

cbuffer TessellationParams : register(b1) {
    float4x4 viewMatrix;
    float4x4 projMatrix;
    float4 cameraPos;
    float minTessDist;     // 20.0
    float maxTessDist;     // 800.0
    float minTessLevel;    // 4.0
    float maxTessLevel;    // 64.0
}

//float ComputeTessLevel(float3 worldPos) {
//    float4 eyePos = mul(float4(worldPos, 1.0), viewMatrix);
//    float distance = abs(eyePos.z);
//    float t = saturate((distance - minTessDist) / (maxTessDist - minTessDist));
//    return lerp(maxTessLevel, minTessLevel, t);
//}

// Более продвинутая версия с учетом размера треугольника на экране
float ComputeTessLevel(float3 worldPos0, float3 worldPos1) {
    // Преобразуем обе точки в пространство вида
    float4 eyePos0 = mul(float4(worldPos0, 1.0), viewMatrix);
    float4 eyePos1 = mul(float4(worldPos1, 1.0), viewMatrix);
    
    // Вычисляем расстояние между точками в пространстве вида
    float edgeLength = length(eyePos1.xyz - eyePos0.xyz);
    
    // Вычисляем расстояние до камеры (среднее)
    float distance = (abs(eyePos0.z) + abs(eyePos1.z)) * 0.5;
    
    // Чем ближе и больше ребро, тем выше тесселяция
    float tessFactor = maxTessLevel * saturate(1.0 - (distance - minTessDist) / (maxTessDist - minTessDist));
    
    // Учитываем размер ребра (большие рёбра требуют больше разбиений)
    float edgeFactor = edgeLength / 5.0;  // 5.0 - эмпирический коэффициент
    
    return clamp(tessFactor * edgeFactor, minTessLevel, maxTessLevel);
}

HS_CONSTANT_OUTPUT PatchConstantFunc(InputPatch<HS_INPUT, 3> patch, uint patchID : SV_PrimitiveID) {
    HS_CONSTANT_OUTPUT output;
    
    float3 worldPos0 = patch[0].position;
    float3 worldPos1 = patch[1].position;
    float3 worldPos2 = patch[2].position;
    
    float tess0 = ComputeTessLevel(worldPos0);
    float tess1 = ComputeTessLevel(worldPos1);
    float tess2 = ComputeTessLevel(worldPos2);
    
    // Avoid T-junctions by using max of edge vertices
    output.edges[0] = max(tess1, tess2);
    output.edges[1] = max(tess2, tess0);
    output.edges[2] = max(tess0, tess1);
    output.inside = (tess0 + tess1 + tess2) / 3.0;
    
    return output;
}

[domain("tri")]
[partitioning("fractional_even")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConstantFunc")]
HS_OUTPUT main(InputPatch<HS_INPUT, 3> patch, uint id : SV_OutputControlPointID) {
    HS_OUTPUT output;
    output.position = patch[id].position;
    output.normal = patch[id].normal;
    output.texcoord = patch[id].texcoord;
    output.tangent = patch[id].tangent;
    return output;
}
)";

    // Domain Shader (Tessellation Evaluation Shader)
    static const char* TessDS = R"(
struct HS_CONSTANT_OUTPUT {
    float edges[3]  : SV_TessFactor;
    float inside    : SV_InsideTessFactor;
};

struct HS_OUTPUT {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float3 tangent  : TANGENT;
};

struct DS_OUTPUT {
    float4 position : SV_POSITION;
    float3 worldPos : WORLDPOS;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float3 tangent  : TANGENT;
    float3 bitangent : BITANGENT;
};

cbuffer SceneConstant : register(b0) {
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
}

cbuffer TessellationParams : register(b1) {
    float4x4 viewMatrix;
    float4x4 projMatrix;
    float4 tessCameraPos;
    float minTessDist;
    float maxTessDist;
    float minTessLevel;
    float maxTessLevel;
}

Texture2D displacementMap : register(t2);
SamplerState displacementSampler : register(s0);

float displacementScale = 0.5f;

[domain("tri")]
DS_OUTPUT main(HS_CONSTANT_OUTPUT input, 
               const OutputPatch<HS_OUTPUT, 3> patch,
               float3 barycentric : SV_DomainLocation) {
    DS_OUTPUT output;
    
    // Interpolate vertex data
    float3 position = patch[0].position * barycentric.x + 
                      patch[1].position * barycentric.y + 
                      patch[2].position * barycentric.z;
    
    float3 normal = normalize(patch[0].normal * barycentric.x + 
                              patch[1].normal * barycentric.y + 
                              patch[2].normal * barycentric.z);
    
    float2 texcoord = patch[0].texcoord * barycentric.x + 
                      patch[1].texcoord * barycentric.y + 
                      patch[2].texcoord * barycentric.z;
    
    float3 tangent = normalize(patch[0].tangent * barycentric.x + 
                               patch[1].tangent * barycentric.y + 
                               patch[2].tangent * barycentric.z);
    
    // Apply displacement
    texcoord = texcoord * textureScale + textureOffset;
    float displacement = displacementMap.SampleLevel(displacementSampler, texcoord, 0).r;
    position += normal * displacement * displacementScale;
    
    // Transform to clip space
    output.position = mul(float4(position, 1.0), worldViewProj);
    output.worldPos = mul(float4(position, 1.0), world).xyz;
    output.normal = normalize(mul(float4(normal, 0.0), world).xyz);
    output.texcoord = texcoord;
    output.tangent = normalize(mul(float4(tangent, 0.0), world).xyz);
    output.bitangent = cross(output.normal, output.tangent);
    
    return output;
}
)";

    // Geometry Pixel Shader with normal mapping
    static const char* GeometryPS_NM = R"(
struct PS_INPUT {
    float4 position : SV_POSITION;
    float3 worldPos : WORLDPOS;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float3 tangent  : TANGENT;
    float3 bitangent : BITANGENT;
};

struct PS_OUTPUT {
    float4 albedo   : SV_Target0;
    float4 worldPos : SV_Target1;
    float4 normal   : SV_Target2;
};

Texture2D diffuseTexture : register(t0);
Texture2D normalMap : register(t1);
SamplerState textureSampler : register(s0);

PS_OUTPUT main(PS_INPUT input) {
    PS_OUTPUT output;
    
    // Sample textures
    float4 albedo = diffuseTexture.Sample(textureSampler, input.texcoord);
    
    // Normal mapping
    float3 texNormal = normalMap.Sample(textureSampler, input.texcoord).rgb;
    texNormal = normalize(texNormal * 2.0 - 1.0);
    
    // Build TBN matrix
    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent);
    float3 B = normalize(input.bitangent);
    
    // Transform normal to world space
    float3x3 TBN = float3x3(T, B, N);
    float3 worldNormal = normalize(mul(texNormal, TBN));
    
    output.albedo = float4(albedo.rgb, 1.0);
    output.worldPos = float4(input.worldPos, 1.0);
    output.normal = float4(worldNormal * 0.5 + 0.5, 1.0); // Encode to [0,1]
    
    return output;
}
)";
}