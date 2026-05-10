#pragma once

namespace Shaders {
    static const char* GeometryVS = R"(
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
            float2 padding;
        }

        struct VSInput {
            float3 position : POSITION;
            float3 normal : NORMAL;
            float2 texcoord : TEXCOORD0;
        };

        struct VSOutput {
            float4 position : SV_POSITION;
            float3 worldPos : TEXCOORD0;
            float3 normal : TEXCOORD1;
            float2 texcoord : TEXCOORD2;
        };

        VSOutput main(VSInput input) {
            VSOutput output;
            float4 worldPos = mul(float4(input.position, 1.0f), world);
            output.worldPos = worldPos.xyz;
            output.position = mul(float4(input.position, 1.0f), worldViewProj);
            output.normal = mul(float4(input.normal, 0.0f), world).xyz;
            output.texcoord = input.texcoord * textureScale + textureOffset;
            return output;
        }
    )";

    static const char* GeometryPS = R"(
        struct PSInput {
            float4 position : SV_POSITION;
            float3 worldPos : TEXCOORD0;
            float3 normal : TEXCOORD1;
            float2 texcoord : TEXCOORD2;
        };

        struct PSOutput {
            float4 albedo : SV_TARGET0;
            float4 worldPos : SV_TARGET1;
            float4 normal : SV_TARGET2;
        };

        Texture2D DiffuseTex : register(t0);
        SamplerState LinearSampler : register(s0);

        PSOutput main(PSInput input) {
            PSOutput output;
            
            float4 texColor = DiffuseTex.Sample(LinearSampler, input.texcoord);
            output.albedo = float4(texColor.rgb, 1.0f);
            output.worldPos = float4(input.worldPos, 1.0f);
            
            float3 normalizedNormal = normalize(input.normal);
            output.normal = float4(normalizedNormal * 0.5f + 0.5f, 1.0f);
            
            return output;
        }
    )";
}