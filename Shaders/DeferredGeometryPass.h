#pragma once

namespace Shaders {
    static const char* GeometryVS = R"(
        cbuffer SceneConstantBuffer : register(b0) {
            float4x4 worldViewProj;         // 64
            float4x4 world;                 // 64
            float4x4 prevWorldViewProj;     // 64
            float2   textureScale;          // 8
            float2   textureOffset;         // 8
            float4   materialFactor;        // 16
            float4   terrainAnim;           // 16
            float2   invResolution;         // 8
            float2   padding;               // 8
        }

        struct VSInput {
            float3 position : POSITION;
            float3 normal   : NORMAL;
            float2 texcoord : TEXCOORD0;
        };

        struct VSOutput {
            float4 position     : SV_POSITION;
            float4 prevPosition : TEXCOORD3;   // clip-space (не NDC)
            float3 worldPos     : TEXCOORD0;
            float3 normal       : TEXCOORD1;
            float2 texcoord     : TEXCOORD2;
        };

        VSOutput main(VSInput input) {
            VSOutput output;
            float4 localPos = float4(input.position, 1.0f);
            if (terrainAnim.w > 0.5) {
                localPos.y = localPos.y * terrainAnim.x + terrainAnim.y;
            }

            float4 worldPos = mul(localPos, world);
            output.worldPos = worldPos.xyz;
            output.position = mul(localPos, worldViewProj);
            output.prevPosition = mul(localPos, prevWorldViewProj);   // НОВОЕ
            output.normal = mul(float4(input.normal, 0.0f), world).xyz;
            output.texcoord = input.texcoord * textureScale + textureOffset;
            return output;
        }
    )";

    static const char* GeometryPS = R"(

        cbuffer SceneConstantBuffer : register(b0) {
            float4x4 worldViewProj;         // 64
            float4x4 world;                 // 64
            float4x4 prevWorldViewProj;     // 64
            float2   textureScale;          // 8
            float2   textureOffset;         // 8
            float4   materialFactor;        // 16
            float4   terrainAnim;           // 16
            float2   invResolution;         // 8
            float2   padding;               // 8
        }

        struct PSInput {
            float4 position     : SV_POSITION;
            float4 prevPosition : TEXCOORD3;   
            float3 worldPos     : TEXCOORD0;
            float3 normal       : TEXCOORD1;
            float2 texcoord     : TEXCOORD2;
        };

        struct PSOutput {
            float4 albedo   : SV_TARGET0;
            float4 worldPos : SV_TARGET1;
            float4 normal   : SV_TARGET2;
            float4 pbr      : SV_TARGET3;
            float2 velocity : SV_TARGET4;     
        };

        Texture2D AlbedoTexture    : register(t0);
        Texture2D RoughnessTexture : register(t1);
        Texture2D MetallicTexture  : register(t2);
        Texture2D AOTexture        : register(t3);
        Texture2D NormalTexture    : register(t4);

        SamplerState LinearSampler : register(s0);

        PSOutput main(PSInput input) {
            PSOutput output;

            float4 texColor = AlbedoTexture.Sample(LinearSampler, input.texcoord);

            // ==== Анимация цвета террейна ====
            float3 tint = float3(1.0, 1.0, 1.0);
            if (terrainAnim.w > 0.5) {
                float t = terrainAnim.z;
                float phase = 0.4;
                tint.r = 0.5 + 0.5 * sin(t * phase);
                tint.g = 0.5 + 0.5 * sin(t * phase + 2.094);
                tint.b = 0.5 + 0.5 * sin(t * phase + 4.189);
                tint = lerp(float3(1.0, 1.0, 1.0), tint, 0.4);
            }

            output.albedo = float4(texColor.rgb * tint, 1.0f);
            output.worldPos = float4(input.worldPos, 1.0f);

            float3 N = normalize(input.normal);
            output.normal = float4(N * 0.5f + 0.5f, 1.0f);

            float roughness = RoughnessTexture.Sample(LinearSampler, input.texcoord).r;
            float metallic  = MetallicTexture.Sample(LinearSampler, input.texcoord).r;
            float ao        = AOTexture.Sample(LinearSampler, input.texcoord).r;

            if (materialFactor.w < 0.5) {
                roughness = materialFactor.x;
                metallic  = materialFactor.y;
                ao        = materialFactor.z;
            }

            output.pbr = float4(roughness, metallic, ao, 1.0f);

            // ==== Motion Vectors ====
            float2 currentUV = input.position.xy * invResolution;

            if (input.prevPosition.w > 1e-6) {
                float2 prevNdc = input.prevPosition.xy / input.prevPosition.w;
                // D3D12 NDC: Y вниз, UV: Y вверх → flip
                float2 prevUV = float2(prevNdc.x * 0.5 + 0.5,
                                      -prevNdc.y * 0.5 + 0.5);
                output.velocity = currentUV - prevUV;
            } else {
                output.velocity = float2(0.0, 0.0);
            }

            return output;
        }
    )";
}