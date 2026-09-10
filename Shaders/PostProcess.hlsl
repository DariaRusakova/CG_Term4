// ================================================
// POST-PROCESSING SHADERS
// ================================================

// ================================================
// VS_PostProcess - Fullscreen Quad without Vertex Buffer
// ================================================
struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput VS_PostProcess(uint vertexID : SV_VertexID)
{
    VSOutput output;
    
    // Generate fullscreen quad using 3 vertices (triangle strip)
    // Vertex IDs: 0, 1, 2 -> (0,0), (2,0), (0,2) in NDC
    // This creates a triangle that covers the entire screen
    
    // Map vertex ID to NDC coordinates
    // UV coordinates: (0,0) top-left, (1,1) bottom-right
    float2 uv = float2(
        (vertexID == 1) ? 2.0f : 0.0f,  // x: 0, 2, 0 for vertices 0, 1, 2
        (vertexID == 2) ? 2.0f : 0.0f   // y: 0, 0, 2 for vertices 0, 1, 2
    );
    
    output.uv = uv;
    
    // Convert UV to NDC (-1 to 1)
    // UV -> NDC: x: 0->-1, 1->1; y: 0->1, 1->-1 (flipped for DX)
    output.position = float4(
        uv.x - 1.0f,    // -1 to 1
        1.0f - uv.y,    // 1 to -1 (DX clip space)
        0.0f,
        1.0f
    );
    
    return output;
}

// ================================================
// PS_ReadGBuffer - Debug shader to verify GBuffer reads
// ================================================
Texture2D<float4> g_AlbedoTexture : register(t0);
Texture2D<float4> g_WorldPosTexture : register(t1);
Texture2D<float4> g_NormalTexture : register(t2);
SamplerState g_Sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 PS_ReadGBuffer(PSInput input) : SV_TARGET
{
    // Sample GBuffer textures
    float4 albedo = g_AlbedoTexture.Sample(g_Sampler, input.uv);
    float4 worldPos = g_WorldPosTexture.Sample(g_Sampler, input.uv);
    float4 normalData = g_NormalTexture.Sample(g_Sampler, input.uv);
    
    // Display albedo with overlay of world position intensity
    float3 worldPosNormalized = normalize(abs(worldPos.xyz) + 0.001f);
    
    // Combine: Albedo with world position as a subtle overlay
    float3 result = albedo.rgb * 0.7f + worldPosNormalized * 0.3f;
    
    return float4(result, 1.0f);
}

// ================================================
// PS_Sepia - Sepia tone effect
// ================================================
float4 PS_Sepia(PSInput input) : SV_TARGET
{
    // Sample GBuffer
    float4 albedo = g_AlbedoTexture.Sample(g_Sampler, input.uv);
    float4 worldPos = g_WorldPosTexture.Sample(g_Sampler, input.uv);
    float4 normalData = g_NormalTexture.Sample(g_Sampler, input.uv);
    
    // Check if pixel is background (no geometry)
    if (length(worldPos.xyz) < 0.001f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    
    // Basic diffuse lighting (simplified ambient)
    float3 N = normalize(normalData.xyz * 2.0f - 1.0f);
    float3 lightDir = normalize(float3(0.5f, 0.8f, 0.3f));
    float diffuse = saturate(dot(N, lightDir));
    
    // Simple ambient + diffuse
    float3 litColor = albedo.rgb * (0.15f + 0.85f * diffuse);
    
    // Apply Sepia tone
    // Sepia matrix: weighted average of RGB channels
    float3 sepiaColor = float3(
        dot(litColor, float3(0.393f, 0.769f, 0.189f)),
        dot(litColor, float3(0.349f, 0.686f, 0.168f)),
        dot(litColor, float3(0.272f, 0.534f, 0.131f))
    );
    
    // Vignette effect for more dramatic look
    float2 uvCentered = input.uv - 0.5f;
    float vignette = 1.0f - dot(uvCentered, uvCentered) * 0.8f;
    sepiaColor *= vignette;
    
    return float4(sepiaColor, 1.0f);
}

// ================================================
// PS_EdgeDetection - Sobel operator
// ================================================
float4 PS_EdgeDetection(PSInput input) : SV_TARGET
{
    // Sobel kernel for edge detection on world position
    const float2 texelSize = float2(1.0f / 1024.0f, 1.0f / 768.0f); // Should be passed via CB
    
    // Sobel kernels
    const float3x3 sobelX = {
        1.0f, 0.0f, -1.0f,
        2.0f, 0.0f, -2.0f,
        1.0f, 0.0f, -1.0f
    };
    const float3x3 sobelY = {
        1.0f, 2.0f, 1.0f,
        0.0f, 0.0f, 0.0f,
        -1.0f, -2.0f, -1.0f
    };
    
    // Sample world positions around the pixel
    float3 samples[3][3];
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float2 sampleUV = input.uv + float2(x, y) * texelSize;
            // Use Load for more precise sampling (integer coordinates)
            int3 texPos = int3(sampleUV * float2(1024.0f, 768.0f), 0);
            samples[y + 1][x + 1] = g_WorldPosTexture.Load(texPos).xyz;
        }
    }
    
    // Check if center pixel is background
    if (length(samples[1][1]) < 0.001f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    
    // Apply Sobel operator
    float3 edgeX = float3(0, 0, 0);
    float3 edgeY = float3(0, 0, 0);
    
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            float weightX = sobelX[i][j];
            float weightY = sobelY[i][j];
            edgeX += samples[i][j] * weightX;
            edgeY += samples[i][j] * weightY;
        }
    }
    
    // Calculate edge magnitude (using only world position, not color)
    float magnitude = length(edgeX) + length(edgeY);
    
    // Normalize magnitude to 0-1 range
    float edgeStrength = saturate(magnitude * 2.0f);
    
    // Sample albedo for the background
    float4 albedo = g_AlbedoTexture.Sample(g_Sampler, input.uv);
    
    // Edge color: white edge on top of original
    float3 result = albedo.rgb;
    float3 edgeColor = float3(1.0f, 1.0f, 1.0f);
    
    // Blend: white edges where edgeStrength > threshold
    float threshold = 0.15f;
    float edge = smoothstep(threshold, threshold + 0.2f, edgeStrength);
    result = lerp(result, edgeColor, edge);
    
    return float4(result, 1.0f);
}