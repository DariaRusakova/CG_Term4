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
        Texture2D<float4> AlbedoTex : register(t0);
        Texture2D<float4> WorldPosTex : register(t1);
        Texture2D<float4> NormalTex : register(t2);

        cbuffer LightBuffer : register(b0) {
            float4 lights_pos_type[16];
            float4 lights_dir[16];
            float4 lights_col_int[16];
            float4 lights_range_angle[16];
            uint lightCount;
        }

        float4 main(float4 position : SV_POSITION) : SV_TARGET {
            int3 texPos = int3(position.xy, 0);
    
            float4 albedo = AlbedoTex.Load(texPos);
            float4 worldPos = WorldPosTex.Load(texPos);
            float4 normalData = NormalTex.Load(texPos);
    
            float3 N = normalize(normalData.xyz * 2.0 - 1.0);
            float3 finalColor = albedo.rgb * 0.1;
    
            for (uint i = 0; i < lightCount; i++) {
                float3 lightPos = lights_pos_type[i].xyz;
                float3 lightColor = lights_col_int[i].rgb;
                float intensity = lights_col_int[i].a;
                uint type = (uint)lights_pos_type[i].w;
        
                float3 L = normalize(lightPos - worldPos.xyz);
                float NdotL = saturate(dot(N, L));
        
                float globalIntensity = 0.01f;
                finalColor += albedo.rgb * lightColor * NdotL * intensity * globalIntensity;
            }
    
            return float4(finalColor, 1.0);
        }
        )";
}