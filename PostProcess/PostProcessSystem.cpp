#include "PostProcessSystem.h"
#include <stdexcept>
#include <d3dcompiler.h>
#include <vector>

using Microsoft::WRL::ComPtr;

static inline void CheckHr(HRESULT hr, const char* msg)
{
    if (FAILED(hr))
    {
        throw std::runtime_error(msg);
    }
}

PostProcessSystem::PostProcessSystem()
{
    for (auto& ptr : m_pipelineStates)
        ptr = nullptr;
}

PostProcessSystem::~PostProcessSystem()
{
    Release();
}

void PostProcessSystem::Release()
{
    for (auto& ptr : m_pipelineStates)
        ptr.Reset();
    m_rootSignature.Reset();
    m_gbufferSrvHeap.Reset();
    m_initialized = false;
}

void PostProcessSystem::Initialize(ID3D12Device* device, UINT width, UINT height)
{
    if (m_initialized)
        Release();

    m_width = width;
    m_height = height;
    m_rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CreateRootSignature(device);
    CreatePipelineStates(device);
    CreateDescriptorHeaps(device);

    m_initialized = true;
}

void PostProcessSystem::Resize(UINT width, UINT height)
{
    m_width = width;
    m_height = height;
}

void PostProcessSystem::CreateRootSignature(ID3D12Device* device)
{
    D3D12_DESCRIPTOR_RANGE combinedRange = {};
    combinedRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    combinedRange.NumDescriptors = 3;
    combinedRange.BaseShaderRegister = 0;
    combinedRange.RegisterSpace = 0;
    combinedRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[1] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &combinedRange;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 1;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr))
    {
        if (error)
            OutputDebugStringA((char*)error->GetBufferPointer());
        CheckHr(hr, "Failed to serialize post-process root signature");
    }

    hr = device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature)
    );
    CheckHr(hr, "Failed to create post-process root signature");
}

void PostProcessSystem::CreatePipelineStates(ID3D12Device* device)
{
    static const char* shaderSource = R"(
struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput VS_PostProcess(uint vertexID : SV_VertexID)
{
    VSOutput output;
    
  
    // Vertex 0: (-1,  1) -> uv (0, 0) - left-top
    // Vertex 1: ( 1,  1) -> uv (1, 0) - right-top
    // Vertex 2: (-1, -1) -> uv (0, 1) - left-bottom
    // Vertex 3: ( 1, -1) -> uv (1, 1) - right-bottom
    
    float x = (vertexID == 1 || vertexID == 3) ? 1.0f : -1.0f;
    float y = (vertexID == 0 || vertexID == 1) ? 1.0f : -1.0f;
    
    float u = (x + 1.0f) * 0.5f;
    float v = 1.0f - (y + 1.0f) * 0.5f;
    
    output.uv = float2(u, v);
    output.position = float4(x, y, 0.0f, 1.0f);
    
    return output;
}

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
    float4 albedo = g_AlbedoTexture.Sample(g_Sampler, input.uv);
    float4 worldPos = g_WorldPosTexture.Sample(g_Sampler, input.uv);
    float4 normalData = g_NormalTexture.Sample(g_Sampler, input.uv);
    
    float3 worldPosNormalized = normalize(abs(worldPos.xyz) + 0.001f);
    float3 result = albedo.rgb * 0.7f + worldPosNormalized * 0.3f;
    
    return float4(result, 1.0f);
}

float4 PS_Sepia(PSInput input) : SV_TARGET
{
    float4 albedo = g_AlbedoTexture.Sample(g_Sampler, input.uv);
    float4 worldPos = g_WorldPosTexture.Sample(g_Sampler, input.uv);
    float4 normalData = g_NormalTexture.Sample(g_Sampler, input.uv);
    
    if (length(worldPos.xyz) < 0.001f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    
    float3 N = normalize(normalData.xyz * 2.0f - 1.0f);
    float3 lightDir = normalize(float3(0.5f, 0.8f, 0.3f));
    float diffuse = saturate(dot(N, lightDir));
    
    float3 litColor = albedo.rgb * (0.15f + 0.85f * diffuse);
    
    float3 sepiaColor = float3(
        dot(litColor, float3(0.393f, 0.769f, 0.189f)),
        dot(litColor, float3(0.349f, 0.686f, 0.168f)),
        dot(litColor, float3(0.272f, 0.534f, 0.131f))
    );
    
    float2 uvCentered = input.uv - 0.5f;
    float vignette = 1.0f - dot(uvCentered, uvCentered) * 0.8f;
    sepiaColor *= vignette;
    
    return float4(sepiaColor, 1.0f);
}

float4 PS_EdgeDetection(PSInput input) : SV_TARGET
{
    float2 texelSize = float2(1.0f / 1024.0f, 1.0f / 768.0f);
    
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
    
    float3 samples[3][3];
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float2 sampleUV = input.uv + float2(x, y) * texelSize;
            samples[y + 1][x + 1] = g_WorldPosTexture.Sample(g_Sampler, sampleUV).xyz;
        }
    }
    
    if (length(samples[1][1]) < 0.001f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    
    float3 edgeX = float3(0, 0, 0);
    float3 edgeY = float3(0, 0, 0);
    
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            edgeX += samples[i][j] * sobelX[i][j];
            edgeY += samples[i][j] * sobelY[i][j];
        }
    }
    
    float magnitude = length(edgeX) + length(edgeY);
    float edgeStrength = saturate(magnitude * 2.0f);
    
    float4 albedo = g_AlbedoTexture.Sample(g_Sampler, input.uv);
    float3 result = albedo.rgb;
    float3 edgeColor = float3(1.0f, 1.0f, 1.0f);
    
    float threshold = 0.15f;
    float edge = smoothstep(threshold, threshold + 0.2f, edgeStrength);
    result = lerp(result, edgeColor, edge);
    
    return float4(result, 1.0f);
}
float4 PS_Test(PSInput input) : SV_TARGET
{
    return float4(input.uv.x, input.uv.y, 0.0f, 1.0f);
}
)";

    UINT compileFlags = 0;
#ifdef _DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    // Vertex Shader
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> vsError;

    HRESULT hr = D3DCompile(
        shaderSource,
        strlen(shaderSource),
        nullptr,
        nullptr,
        nullptr,
        "VS_PostProcess",
        "vs_5_0",
        compileFlags,
        0,
        &vsBlob,
        &vsError
    );
    if (FAILED(hr))
    {
        if (vsError)
            OutputDebugStringA((char*)vsError->GetBufferPointer());
        CheckHr(hr, "Failed to compile post-process VS");
    }

    const char* psEntryPoints[] = {
        "PS_ReadGBuffer",
        "PS_Sepia",
        "PS_EdgeDetection",
        "PS_Test"  
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    for (int i = 0; i < static_cast<int>(EffectType::Count); ++i)
    {
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> psError;

        hr = D3DCompile(
            shaderSource,
            strlen(shaderSource),
            nullptr,
            nullptr,
            nullptr,
            psEntryPoints[i],
            "ps_5_0",
            compileFlags,
            0,
            &psBlob,
            &psError
        );
        if (FAILED(hr))
        {
            if (psError)
                OutputDebugStringA((char*)psError->GetBufferPointer());
            char errorMsg[256];
            sprintf_s(errorMsg, "Failed to compile post-process PS for effect %d", i);
            CheckHr(hr, errorMsg);
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC currentDesc = psoDesc;
        currentDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

        hr = device->CreateGraphicsPipelineState(&currentDesc, IID_PPV_ARGS(&m_pipelineStates[i]));
        CheckHr(hr, "Failed to create post-process PSO");
    }
}

void PostProcessSystem::CreateDescriptorHeaps(ID3D12Device* device)
{
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 3;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_gbufferSrvHeap));
    CheckHr(hr, "Failed to create GBuffer SRV heap for post-process");

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_gbufferSrvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_gbufferSrvHeap->GetGPUDescriptorHandleForHeapStart();

    for (int i = 0; i < 3; ++i)
    {
        m_gbufferSRVs[i] = cpuHandle;
        m_gbufferSRVGPU[i] = gpuHandle;
        cpuHandle.ptr += m_srvDescriptorSize;
        gpuHandle.ptr += m_srvDescriptorSize;
    }
}

void PostProcessSystem::SetGBufferResources(
    ID3D12Resource* albedoTexture,
    ID3D12Resource* worldPosTexture,
    ID3D12Resource* normalTexture)
{
    m_albedoTexture = albedoTexture;
    m_worldPosTexture = worldPosTexture;
    m_normalTexture = normalTexture;

    if (m_initialized && m_gbufferSrvHeap)
    {
        ID3D12Device* device = nullptr;
        m_gbufferSrvHeap->GetDevice(IID_PPV_ARGS(&device));
        if (device)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;

            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            device->CreateShaderResourceView(m_albedoTexture, &srvDesc, m_gbufferSRVs[0]);

            srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            device->CreateShaderResourceView(m_worldPosTexture, &srvDesc, m_gbufferSRVs[1]);
            device->CreateShaderResourceView(m_normalTexture, &srvDesc, m_gbufferSRVs[2]);

            device->Release();
        }
    }
}

void PostProcessSystem::Render(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE targetRTV,
    UINT targetWidth,
    UINT targetHeight,
    EffectType effectType)
{
    if (!m_initialized || !m_albedoTexture || !m_worldPosTexture || !m_normalTexture)
    {
        return;
    }

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(targetWidth);
    viewport.Height = static_cast<float>(targetHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    cmdList->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect = {};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(targetWidth);
    scissorRect.bottom = static_cast<LONG>(targetHeight);
    cmdList->RSSetScissorRects(1, &scissorRect);

    cmdList->OMSetRenderTargets(1, &targetRTV, FALSE, nullptr);

    ID3D12DescriptorHeap* ppHeaps[] = { m_gbufferSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, ppHeaps);

    cmdList->SetGraphicsRootSignature(m_rootSignature.Get());

    int effectIndex = static_cast<int>(effectType);
    if (effectIndex >= static_cast<int>(EffectType::Count))
        effectIndex = 0;

    cmdList->SetPipelineState(m_pipelineStates[effectIndex].Get());

    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_gbufferSrvHeap->GetGPUDescriptorHandleForHeapStart();
    cmdList->SetGraphicsRootDescriptorTable(0, gpuHandle);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmdList->DrawInstanced(4, 1, 0, 0);
}