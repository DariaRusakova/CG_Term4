#pragma once

namespace ParticleShaders {

    // Compute Shader: Эмиссия и обновление частиц
    static const char* EmitUpdateCS = R"(
    // Структура частицы для шейдера
    struct ShaderParticle {
        float3 position;
        float age;
        float3 velocity;
        float lifetime;
        float4 color;
        float size;
        float3 padding;
    };
    
    AppendStructuredBuffer<ShaderParticle> emitBuffer : register(u0);
    ConsumeStructuredBuffer<ShaderParticle> consumeBuffer : register(u1);
    AppendStructuredBuffer<ShaderParticle> outputBuffer : register(u2);
    
    cbuffer ParticleParams : register(b0) {
        float3 emitterPosition;
        float deltaTime;
        float3 emitterVelocity;
        float emitRate;
        float emitterRadius;
        float particleLifetime;
        float particleSpeed;
        float particleSize;
        float4 particleColor;
        float4x4 viewMatrix;
        float4x4 projMatrix;
        uint maxParticles;
        uint frameIndex;
        float2 padding2;
    }
    
    float Rand(float3 seed) {
        uint n = (uint)(seed.x * 73856093.0 + seed.y * 19349663.0 + seed.z * 83492791.0 + (float)frameIndex * 2531011.0);
        n = (n << 13) ^ n;
        return (float)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 2147483648.0;
    }
    
    [numthreads(64, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
        uint index = id.x;
        
        // 1. Обработка существующих частиц
        ShaderParticle p = consumeBuffer.Consume();
        if (p.age < 1.0f) {
            p.age += deltaTime / max(p.lifetime, 0.001f);
            p.position += p.velocity * deltaTime;
            p.velocity.y -= 2.0f * deltaTime;
            
            if (p.age < 1.0f && p.position.y > -10.0f) {
                outputBuffer.Append(p);
            }
        }
        
        // 2. Эмиссия новых частиц
        float emitCount = emitRate * deltaTime;
        if ((float)index < emitCount) {
            float3 seed = float3((float)index * 1.7f, (float)index * 3.2f, (float)index * 5.1f);
            
            ShaderParticle newParticle;
            
            float theta = Rand(seed) * 6.28318f;
            float phi = Rand(seed + 1.0f) * 3.14159f;
            float r = Rand(seed + 2.0f) * emitterRadius;
            
            newParticle.position = emitterPosition + float3(
                sin(phi) * cos(theta),
                cos(phi),
                sin(phi) * sin(theta)
            ) * r;
            
            newParticle.velocity = float3(
                (Rand(seed + 3.0f) - 0.5f) * particleSpeed,
                particleSpeed * (0.5f + Rand(seed + 4.0f) * 0.5f),
                (Rand(seed + 5.0f) - 0.5f) * particleSpeed
            );
            
            newParticle.age = 0.0f;
            newParticle.lifetime = particleLifetime * (0.5f + Rand(seed + 6.0f) * 0.5f);
            newParticle.color = particleColor;
            newParticle.size = particleSize * (0.5f + Rand(seed + 7.0f) * 1.0f);
            newParticle.padding = float3(0, 0, 0);
            
            emitBuffer.Append(newParticle);
        }
    }
)";

    // Vertex Shader для билбордов (проходит через GS)
    static const char* BillboardVS = R"(
        struct VSInput {
            float3 position : POSITION;
            float age       : AGE;
            float3 velocity : VELOCITY;
            float lifetime  : LIFETIME;
            float4 color    : COLOR;
            float size      : SIZE;
        };
        
        struct GSInput {
            float3 position : POSITION;
            float age       : AGE;
            float4 color    : COLOR;
            float size      : SIZE;
        };
        
        GSInput main(VSInput input) {
            GSInput output;
            output.position = input.position;
            output.age = input.age;
            output.color = input.color;
            output.size = input.size;
            return output;
        }
    )";

    // Geometry Shader для развёртывания точек в билборды
    static const char* BillboardGS = R"(
        struct GSInput {
            float3 position : POSITION;
            float age       : AGE;
            float4 color    : COLOR;
            float size      : SIZE;
        };
        
        struct PSInput {
            float4 position : SV_POSITION;
            float2 texcoord : TEXCOORD;
            float4 color    : COLOR;
            float age       : AGE;
        };
        
        cbuffer ParticleParams : register(b0) {
            float3 emitterPosition;
            float deltaTime;
            float3 emitterVelocity;
            float emitRate;
            float emitterRadius;
            float particleLifetime;
            float particleSpeed;
            float particleSize;
            float4 particleColor;
            float4x4 viewMatrix;
            float4x4 projMatrix;
            uint maxParticles;
            uint frameIndex;
            float2 padding;
        }
        
        [maxvertexcount(4)]
        void main(point GSInput input[1], inout TriangleStream<PSInput> triStream) {
            PSInput output;
            
            float3 pos = input[0].position;
            float halfSize = input[0].size * 0.5f;
            float alpha = 1.0f - input[0].age;
            
            output.color = float4(input[0].color.rgb, input[0].color.a * alpha);
            output.age = input[0].age;
            
            // Создаём 4 вершины билборда в world space
            float3 right = float3(1, 0, 0) * halfSize;
            float3 up = float3(0, 1, 0) * halfSize;
            
            // Вершина 0: -X, -Y
            output.position = mul(float4(pos - right - up, 1.0), mul(viewMatrix, projMatrix));
            output.texcoord = float2(0, 0);
            triStream.Append(output);
            
            // Вершина 1: +X, -Y
            output.position = mul(float4(pos + right - up, 1.0), mul(viewMatrix, projMatrix));
            output.texcoord = float2(1, 0);
            triStream.Append(output);
            
            // Вершина 2: -X, +Y
            output.position = mul(float4(pos - right + up, 1.0), mul(viewMatrix, projMatrix));
            output.texcoord = float2(0, 1);
            triStream.Append(output);
            
            // Вершина 3: +X, +Y
            output.position = mul(float4(pos + right + up, 1.0), mul(viewMatrix, projMatrix));
            output.texcoord = float2(1, 1);
            triStream.Append(output);
            
            triStream.RestartStrip();
        }
    )";

    // Pixel Shader для билбордов
    static const char* BillboardPS = R"(
        struct PSInput {
            float4 position : SV_POSITION;
            float2 texcoord : TEXCOORD;
            float4 color    : COLOR;
            float age       : AGE;
        };
        
        float4 main(PSInput input) : SV_TARGET {
            // Круглый билборд
            float2 center = input.texcoord - 0.5f;
            float dist = length(center);
            
            // Мягкий край
            float alpha = 1.0f - smoothstep(0.3f, 0.5f, dist);
            alpha *= input.color.a;
            
            // Частицы затухают с возрастом
            alpha *= (1.0f - input.age);
            
            if (alpha < 0.01f) discard;
            
            return float4(input.color.rgb, alpha);
        }
    )";

    // Shader для очистки счётчика (отдельный compute)
    static const char* ClearCounterCS = R"(
        RWStructuredBuffer<uint> counterBuffer : register(u0);
        
        [numthreads(1, 1, 1)]
        void main() {
            counterBuffer[0] = 0;
        }
    )";
}