#pragma once

namespace Shaders {
    const char* const VertexShader = R"(
        struct VS_INPUT {
            float3 position : POSITION;
            float3 normal   : NORMAL;
            float2 texcoord : TEXCOORD0;
        };

        struct VS_OUTPUT {
            float4 position    : SV_POSITION;
            float3 worldNormal : NORMAL;
            float3 worldPos    : TEXCOORD1;
            float2 texcoord    : TEXCOORD0;
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
            output.texcoord = input.texcoord;
            return output;
        }
    )";

    const char* const PixelShader = R"(
        struct PS_INPUT {
            float4 position    : SV_POSITION;
            float3 worldNormal : NORMAL;
            float3 worldPos    : TEXCOORD1;
            float2 texcoord    : TEXCOORD0;
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

        // b2: шахматное вращение тайлов
        // x = time (сек), y = gridSize, z = speed (рад/сек), w = enable
        cbuffer TileParams : register(b2) {
            float4 tileRot;
        };

        Texture2D    diffuseTexture : register(t0);
        SamplerState textureSampler : register(s0);

        float2 Rotate2D(float2 p, float a) {
            float s, c;
            sincos(a, s, c);
            return float2(p.x * c - p.y * s,
                          p.x * s + p.y * c);
        }

        float4 main(PS_INPUT input) : SV_TARGET {
            float2 uv = input.texcoord * textureScale + textureOffset;

            // --- Шахматное вращение тайлов ---
            if (tileRot.w > 0.5) {
                float time  = tileRot.x;
                float speed = tileRot.z;

                float2 tileCoord = floor(uv);
                float2 localUV   = uv - tileCoord;

                float parity = fmod(tileCoord.x + tileCoord.y, 2.0);
                float sgn    = (parity < 0.5) ? 1.0 : -1.0;

                float angle = sgn * time * speed;

                float2 centered = localUV - 0.5;
                float2 rotated  = Rotate2D(centered, angle) + 0.5;

                uv = rotated + tileCoord;
            }

            float4 texColor = diffuseTexture.Sample(textureSampler, uv);

            if (all(texColor.rgb > 0.99f)) {
                texColor.rgb = materialDiffuse.rgb;
            }

            float3 normal   = normalize(input.worldNormal);
            float3 lightDir = normalize(lightPos.xyz - input.worldPos);
            float3 viewDir  = normalize(cameraPos.xyz - input.worldPos);

            float NdotL = dot(normal, lightDir);
            float diff  = max(NdotL, 0.0f) * 0.8f + max(-NdotL, 0.0f) * 0.2f;

            float3 reflectDir = reflect(-lightDir, normal);
            float  spec = pow(max(dot(viewDir, reflectDir), 0.0f), materialShininess);

            float3 ambient  = materialAmbient.rgb  * texColor.rgb * 0.5f;
            float3 diffuse  = materialDiffuse.rgb  * texColor.rgb * diff * lightColor.rgb;
            float3 specular = materialSpecular.rgb * spec * lightColor.rgb;

            float3 finalColor = ambient + diffuse + specular;
            finalColor = pow(finalColor, 1.0f / 2.2f);

            return float4(finalColor, 1.0f);
        }
    )";
}