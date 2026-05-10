#pragma once

namespace Shaders {
    const char* const VertexShader = R"(
        struct VS_INPUT {
            float3 position : POSITION;
            float3 normal   : NORMAL;
            float2 texcoord : TEXCOORD;
        };

        struct VS_OUTPUT {
            float4 position : SV_POSITION;
            float3 worldNormal : NORMAL;
            float3 worldPos : WORLDPOS;
            float2 texcoord : TEXCOORD;
        };

        cbuffer SceneConstantBuffer : register(b0) {
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

        VS_OUTPUT main(VS_INPUT input) {
            VS_OUTPUT output;
            float4 worldPosition = mul(float4(input.position, 1.0f), world);
            output.position = mul(worldPosition, worldViewProj);
            output.worldPos = worldPosition.xyz;
            output.worldNormal = normalize(mul(input.normal, (float3x3)world));
            output.texcoord = input.texcoord * textureScale + textureOffset;
            return output;
        }
    )";

    const char* const PixelShader = R"(
        struct PS_INPUT {
            float4 position : SV_POSITION;
            float3 worldNormal : NORMAL;
            float3 worldPos : WORLDPOS;
            float2 texcoord : TEXCOORD;
        };

        cbuffer SceneConstantBuffer : register(b0) {
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

        Texture2D diffuseTexture : register(t0);
        SamplerState textureSampler : register(s0);

        float4 main(PS_INPUT input) : SV_TARGET {
            float2 uv = frac(input.texcoord);
            uv = uv * textureScale + textureOffset;
            float4 texColor = diffuseTexture.Sample(textureSampler, uv);
            
            if (all(texColor.rgb > 0.99f)) {
                texColor.rgb = materialDiffuse.rgb;
            }
            
            float3 normal = normalize(input.worldNormal);
            float3 lightDir = normalize(lightPos.xyz - input.worldPos);
            float3 viewDir = normalize(cameraPos.xyz - input.worldPos);
            
            float NdotL = dot(normal, lightDir);
            float diff = max(NdotL, 0.0f) * 0.8f + max(-NdotL, 0.0f) * 0.2f;
            
            float3 reflectDir = reflect(-lightDir, normal);
            float spec = pow(max(dot(viewDir, reflectDir), 0.0f), materialShininess);
            
            float3 ambient = materialAmbient.rgb * texColor.rgb * 0.5f;
            float3 diffuse = materialDiffuse.rgb * texColor.rgb * diff * lightColor.rgb;
            float3 specular = materialSpecular.rgb * spec * lightColor.rgb;
            
            float3 finalColor = ambient + diffuse + specular;
            finalColor = pow(finalColor, 1.0f / 2.2f);
            
            return float4(finalColor, 1.0f);
        }
    )";
}