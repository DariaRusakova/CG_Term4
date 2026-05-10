#pragma once

namespace Shaders {
    // Geometry Pass Vertex Shader (обычный, без тесселяции)
    static const char* GeometryVS = R"(
        struct VSInput {
            float3 position : POSITION;
            float3 normal   : NORMAL;
            float2 texcoord : TEXCOORD;
        };
        
        struct VSOutput {
            float4 position : SV_POSITION;
            float3 worldPos : WORLDPOS;
            float3 normal   : NORMAL;
            float2 texcoord : TEXCOORD;
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
        
        VSOutput main(VSInput input) {
            VSOutput output;
            
            float4 worldPos = mul(float4(input.position, 1.0), world);
            output.position = mul(float4(input.position, 1.0), worldViewProj);
            output.worldPos = worldPos.xyz;
            output.normal = normalize(mul(float4(input.normal, 0.0), world).xyz);
            output.texcoord = input.texcoord * textureScale + textureOffset;
            
            return output;
        }
    )";

    // Geometry Pass Pixel Shader (обычный)
    static const char* GeometryPS = R"(
        struct PSInput {
            float4 position : SV_POSITION;
            float3 worldPos : WORLDPOS;
            float3 normal   : NORMAL;
            float2 texcoord : TEXCOORD;
        };
        
        struct PSOutput {
            float4 albedo   : SV_Target0;
            float4 worldPos : SV_Target1;
            float4 normal   : SV_Target2;
        };
        
        Texture2D diffuseTexture : register(t0);
        SamplerState textureSampler : register(s0);
        
        PSOutput main(PSInput input) {
            PSOutput output;
            
            float4 albedo = diffuseTexture.Sample(textureSampler, input.texcoord);
            float3 normal = normalize(input.normal);
            
            output.albedo = float4(albedo.rgb, 1.0);
            output.worldPos = float4(input.worldPos, 1.0);
            output.normal = float4(normal * 0.5 + 0.5, 1.0); // Encode to [0,1]
            
            return output;
        }
    )";

    // Lighting Pass Vertex Shader
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

    // Lighting Pass Pixel Shader
    static const char* LightPassPS = R"(
        Texture2D<float4> AlbedoTex : register(t0);
        Texture2D<float4> WorldPosTex : register(t1);
        Texture2D<float4> NormalTex : register(t2);
        
        struct LightData {
            float4 position_type;
            float4 direction;
            float4 color_intensity;
            float4 range_spotAngle;
        };
        
        cbuffer LightCB : register(b0) {
            LightData lights[16];
            uint lightCount;
            float3 padding2;
        }
        
        SamplerState samplerState : register(s0);
        
        float3 CalculatePointLight(float3 worldPos, float3 normal, float3 viewDir, LightData light) {
            float3 lightVec = light.position_type.xyz - worldPos;
            float distance = length(lightVec);
            float3 lightDir = normalize(lightVec);
            
            float attenuation = saturate(1.0 - distance * distance / (light.range_spotAngle.x * light.range_spotAngle.x));
            attenuation *= attenuation;
            
            float NdotL = saturate(dot(normal, lightDir));
            float3 diffuse = light.color_intensity.rgb * NdotL * attenuation;
            
            float3 halfVec = normalize(lightDir + viewDir);
            float specular = pow(saturate(dot(normal, halfVec)), 64.0);
            float3 specularColor = light.color_intensity.rgb * specular * attenuation * 0.5;
            
            return (diffuse + specularColor) * light.color_intensity.a;
        }
        
        float3 CalculateDirectionalLight(float3 normal, float3 viewDir, LightData light) {
            float3 lightDir = normalize(-light.direction.xyz);
            
            float NdotL = saturate(dot(normal, lightDir));
            float3 diffuse = light.color_intensity.rgb * NdotL;
            
            float3 halfVec = normalize(lightDir + viewDir);
            float specular = pow(saturate(dot(normal, halfVec)), 64.0);
            float3 specularColor = light.color_intensity.rgb * specular * 0.3;
            
            return (diffuse + specularColor) * light.color_intensity.a;
        }
        
        float3 CalculateSpotLight(float3 worldPos, float3 normal, float3 viewDir, LightData light) {
            float3 lightVec = light.position_type.xyz - worldPos;
            float distance = length(lightVec);
            float3 lightDir = normalize(lightVec);
            
            float attenuation = saturate(1.0 - distance * distance / (light.range_spotAngle.x * light.range_spotAngle.x));
            attenuation *= attenuation;
            
            float spotFactor = dot(-lightDir, normalize(light.direction.xyz));
            float spotCutoff = cos(light.range_spotAngle.y);
            float spotAtten = smoothstep(spotCutoff, spotCutoff + 0.1, spotFactor);
            
            float NdotL = saturate(dot(normal, lightDir));
            float3 diffuse = light.color_intensity.rgb * NdotL * attenuation * spotAtten;
            
            float3 halfVec = normalize(lightDir + viewDir);
            float specular = pow(saturate(dot(normal, halfVec)), 64.0);
            float3 specularColor = light.color_intensity.rgb * specular * attenuation * spotAtten * 0.5;
            
            return (diffuse + specularColor) * light.color_intensity.a;
        }
        
        float4 main(float4 position : SV_POSITION) : SV_TARGET {
            int3 texPos = int3(position.xy, 0);
            
            float4 albedo = AlbedoTex.Load(texPos);
            float4 worldPos = WorldPosTex.Load(texPos);
            float4 normalData = NormalTex.Load(texPos);
            
            float3 N = normalize(normalData.xyz * 2.0 - 1.0);
            float3 V = normalize(float3(0.0, 5.0, -10.0) - worldPos.xyz);
            
            float3 ambient = albedo.rgb * float3(0.1, 0.09, 0.07);
            float3 finalColor = ambient;
            
            for (uint i = 0; i < lightCount; i++) {
                uint type = (uint)lights[i].position_type.w;
                
                if (type == 0) {
                    finalColor += CalculatePointLight(worldPos.xyz, N, V, lights[i]);
                } else if (type == 1) {
                    finalColor += CalculateDirectionalLight(N, V, lights[i]);
                } else if (type == 2) {
                    finalColor += CalculateSpotLight(worldPos.xyz, N, V, lights[i]);
                }
            }
            
            finalColor = finalColor * albedo.rgb;
            
            float3 fillLight = float3(0.05, 0.04, 0.03) * albedo.rgb * saturate(-N.y);
            finalColor += fillLight;
            
            finalColor = finalColor / (finalColor + float3(1.0, 1.0, 1.0));
            finalColor = pow(finalColor, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));
            
            return float4(finalColor, 1.0);
        }
    )";
}