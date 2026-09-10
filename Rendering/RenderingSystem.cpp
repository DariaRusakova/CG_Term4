#include "RenderingSystem.h"
#include "../Model/Vertex.h" 
#include <stdexcept>
#include <directxmath.h>
#include <d3dcompiler.h>
#include <cstring>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

RenderingSystem::RenderingSystem() : m_gbuffer(std::make_unique<GBuffer>()) {}

RenderingSystem::~RenderingSystem() {}

void RenderingSystem::Initialize(ID3D12Device* device, UINT width, UINT height) {
    m_width = width;
    m_height = height;

    // Создаем кучи дескрипторов для GBuffer
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = GBuffer::GB_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 1;
    device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_gbufferRtvHeap));

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = GBuffer::GB_COUNT + 16; // +16 для резерва
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeapDesc.NodeMask = 1;
    device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_gbufferSrvHeap));

    UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    UINT srvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    m_gbuffer->Initialize(device, width, height);

    // Теперь передаем m_gbufferSrvHeap как параметр
    m_gbuffer->CreateDescriptors(device,
        m_gbufferRtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_gbufferSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_gbufferSrvHeap.Get(),  // <-- Вот как передается srvHeap
        rtvSize, srvSize);

    CreateLightBuffers(device);
    CreateFullscreenQuad(device);
    CreateLightingPassPipeline(device);

    ClearLights();

    // Основной направленный свет (солнце) - теплый оттенок
    Light sunLight;
    sunLight.type = LightType::Directional;
    sunLight.direction = XMFLOAT3(0.3f, -0.8f, 0.5f);  // Направление солнца
    sunLight.color = XMFLOAT4(1.0f, 0.9f, 0.7f, 1.0f);  // Теплый солнечный цвет
    sunLight.intensity = 0.8f;
    AddLight(sunLight);

    // Точечный свет в центре атриума (теплый)
    Light centerLight;
    centerLight.type = LightType::Point;
    centerLight.position = XMFLOAT3(0.0f, 4.0f, 0.0f);
    centerLight.color = XMFLOAT4(1.0f, 0.85f, 0.6f, 1.0f);
    centerLight.intensity = 15.0f;
    centerLight.range = 15.0f;
    AddLight(centerLight);

    // Боковые точечные источники для подсветки колонн
    Light leftColumn;
    leftColumn.type = LightType::Point;
    leftColumn.position = XMFLOAT3(-6.0f, 3.0f, -3.0f);
    leftColumn.color = XMFLOAT4(0.9f, 0.8f, 0.7f, 1.0f);
    leftColumn.intensity = 10.0f;
    leftColumn.range = 10.0f;
    AddLight(leftColumn);

    Light rightColumn;
    rightColumn.type = LightType::Point;
    rightColumn.position = XMFLOAT3(6.0f, 3.0f, 3.0f);
    rightColumn.color = XMFLOAT4(0.9f, 0.8f, 0.7f, 1.0f);
    rightColumn.intensity = 10.0f;
    rightColumn.range = 10.0f;
    AddLight(rightColumn);

    //// Свет сзади для подсветки задней стены
    Light backWall;
    backWall.type = LightType::Point;
    backWall.position = XMFLOAT3(0.0f, 5.0f, -8.0f);
    backWall.color = XMFLOAT4(1.0f, 0.85f, 0.7f, 1.0f);
    backWall.intensity = 8.0f;
    backWall.range = 12.0f;
    AddLight(backWall);

    //// Передний свет для подсветки входа
    Light frontLight;
    frontLight.type = LightType::Point;
    frontLight.position = XMFLOAT3(0.0f, 3.0f, 8.0f);
    frontLight.color = XMFLOAT4(0.8f, 0.9f, 1.0f, 1.0f);  // Немного холоднее для контраста
    frontLight.intensity = 12.0f;
    frontLight.range = 14.0f;
    AddLight(frontLight);

    //// Spot свет сверху - как свет через окно
    Light skylight;
    skylight.type = LightType::Spot;
    skylight.position = XMFLOAT3(0.0f, 10.0f, 0.0f);
    skylight.direction = XMFLOAT3(0.0f, -1.0f, 0.1f);
    skylight.color = XMFLOAT4(1.0f, 0.95f, 0.85f, 1.0f);
    skylight.intensity = 25.0f;
    skylight.range = 25.0f;
    skylight.spotAngle = 40.0f * XM_PI / 180.0f;
    AddLight(skylight);


    m_originalIntensities.clear();
    for (const auto& light : m_lights) {
        m_originalIntensities.push_back(light.intensity);
    }
    m_globalIntensity = 0.1f;
}

void RenderingSystem::Resize(UINT width, UINT height) {
    m_width = width;
    m_height = height;
    m_gbuffer->Release();
    // Нужно сохранить device для Resize или передавать его параметром
}

void RenderingSystem::AddLight(const Light& light) {
    m_lights.push_back(light);
}

void RenderingSystem::ClearLights() {
    m_lights.clear();
}

// RenderingSystem.cpp

void RenderingSystem::CreateLightBuffers(ID3D12Device* device) {
    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Alignment = 0;

    // ИСПРАВЛЕНИЕ: Используем размер структуры, которая соответствует шейдеру
    // Убедитесь, что LightBufferGPU определена и её размер совпадает с cbuffer в шейдере
    cbDesc.Width = sizeof(LightBufferGPU);

    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.Format = DXGI_FORMAT_UNKNOWN;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.SampleDesc.Quality = 0;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    cbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    // Создаем ресурс
    HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_lightConstantBuffer));

    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create light constant buffer");
    }

    // Маппим память
    D3D12_RANGE readRange = { 0, 0 };
    hr = m_lightConstantBuffer->Map(0, &readRange, &m_lightCBData);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to map light constant buffer");
    }
}

void RenderingSystem::CreateFullscreenQuad(ID3D12Device* device) {
    struct FullscreenVertex {
        XMFLOAT3 position;
        XMFLOAT2 texcoord;
    };

    FullscreenVertex vertices[] = {
        { XMFLOAT3(-1.0f,  1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(1.0f,  1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) }
    };

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = sizeof(vertices);
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_fullscreenVB));

    void* data;
    D3D12_RANGE readRange = { 0, 0 };
    m_fullscreenVB->Map(0, &readRange, &data);
    memcpy(data, vertices, sizeof(vertices));
    m_fullscreenVB->Unmap(0, nullptr);

    m_fullscreenVBView.BufferLocation = m_fullscreenVB->GetGPUVirtualAddress();
    m_fullscreenVBView.StrideInBytes = sizeof(FullscreenVertex);
    m_fullscreenVBView.SizeInBytes = sizeof(vertices);
}

void RenderingSystem::CreateLightingPassPipeline(ID3D12Device* device) {
    // Шейдеры остаются без изменений...
    static const char* vsCode = R"(
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

    static const char* psCode = R"(
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
    float3 padding;
}

float4 main(float4 position : SV_POSITION) : SV_TARGET {
    int3 texPos = int3(position.xy, 0);
    
    float4 albedo = AlbedoTex.Load(texPos);
    float4 worldPos = WorldPosTex.Load(texPos);
    float4 normalData = NormalTex.Load(texPos);
    
    // Декодируем нормаль
    float3 N = normalize(normalData.xyz * 2.0 - 1.0);
    float3 V = normalize(float3(0.0, 5.0, -10.0) - worldPos.xyz);
    
    // Ambient
    float3 finalColor = albedo.rgb * 0.15;
    
    for (uint i = 0; i < lightCount; i++) {
        uint type = (uint)lights[i].position_type.w;
        float3 L;
        float attenuation = 1.0;
        float spotAtten = 1.0;
        
        if (type == 1) { // Directional
            L = normalize(-lights[i].direction.xyz);
        } 
        else if (type == 0) { // Point
            float3 lightVec = lights[i].position_type.xyz - worldPos.xyz;
            float dist = length(lightVec);
            L = normalize(lightVec);
            attenuation = saturate(1.0 - dist / lights[i].range_spotAngle.x);
            attenuation *= attenuation;
        }
        else if (type == 2) { // Spot
            float3 lightVec = lights[i].position_type.xyz - worldPos.xyz;
            float dist = length(lightVec);
            L = normalize(lightVec);
            
            attenuation = saturate(1.0 - dist / lights[i].range_spotAngle.x);
            attenuation *= attenuation;
            
            float spotCos = dot(-L, normalize(lights[i].direction.xyz));
            float cutoff = cos(lights[i].range_spotAngle.y);
            spotAtten = smoothstep(cutoff, cutoff + 0.2, spotCos);
        }
        
        float NdotL = saturate(dot(N, L));
        float3 diffuse = lights[i].color_intensity.rgb * NdotL * lights[i].color_intensity.a * attenuation * spotAtten;
        
        finalColor += diffuse;
    }
    
    finalColor *= albedo.rgb;
    finalColor = pow(finalColor, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));
    
    return float4(finalColor, 1.0);
}
)";

    ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

    HRESULT hr = D3DCompile(vsCode, strlen(vsCode),
        nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        throw std::runtime_error("Failed to compile lighting VS");
    }

    hr = D3DCompile(psCode, strlen(psCode),
        nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);

    if (FAILED(hr)) {
        if (errorBlob) OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        throw std::runtime_error("Failed to compile lighting PS");
    }

    // Исправленная структура D3D12_DESCRIPTOR_RANGE
    D3D12_DESCRIPTOR_RANGE descRange = {};
    descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descRange.NumDescriptors = 3;
    descRange.BaseShaderRegister = 0;
    descRange.RegisterSpace = 0;
    descRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Исправленный rootParams[1] без Flags
    D3D12_ROOT_PARAMETER rootParams[2] = {};

    // Param 0: Descriptor table для GBuffer
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &descRange;

    // Param 1: Constant buffer для света
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].Descriptor.RegisterSpace = 0;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sampler.MinLOD = 0;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 2;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        throw std::runtime_error("Failed to serialize root signature");
    }

    hr = device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_lightingRootSig));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create root signature");
    }

    D3D12_INPUT_ELEMENT_DESC inputDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputDesc, _countof(inputDesc) };
    psoDesc.pRootSignature = m_lightingRootSig.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount = 0;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;  // Исправлено с TRIANGLESTRIP на TRIANGLE
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.NodeMask = 1;
    psoDesc.CachedPSO.CachedBlobSizeInBytes = 0;
    psoDesc.CachedPSO.pCachedBlob = nullptr;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_lightingPSO));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create lighting PSO");
    }
}

void RenderingSystem::Render(ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* depthStencil,
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
    ID3D12RootSignature* geometryRootSig,
    ID3D12PipelineState* geometryPSO,
    const RenderData* renderData) {

    if (!renderData || !renderData->vertexBuffer || !renderData->indexBuffer) {
        OutputDebugStringA("Invalid render data\n");
        return;
    }

    if (!m_lightCBData) {
        OutputDebugStringA("Light buffer not mapped\n");
        return;
    }

    // Очищаем и заполняем буфер
    memset(m_lightCBData, 0, sizeof(LightBufferGPU));
    LightBufferGPU* lightData = reinterpret_cast<LightBufferGPU*>(m_lightCBData);

    lightData->lightCount = std::min((UINT)m_lights.size(), 16u);

    for (UINT i = 0; i < lightData->lightCount; ++i) {
        const Light& light = m_lights[i];
        lightData->lights[i].position_type = light.GetAsFloat4();
        lightData->lights[i].direction = light.GetDirectionAsFloat4();
        lightData->lights[i].color_intensity = XMFLOAT4(light.color.x, light.color.y, light.color.z, light.intensity);
        lightData->lights[i].range_spotAngle = XMFLOAT4(light.range, light.spotAngle, 0.0f, 0.0f);
    }

    // ============ GEOMETRY PASS ============
    D3D12_CPU_DESCRIPTOR_HANDLE gbufferRTVs[GBuffer::GB_COUNT];
    for (int i = 0; i < GBuffer::GB_COUNT; ++i) {
        gbufferRTVs[i] = m_gbuffer->GetRTV((GBuffer::GBufferType)i);
    }

    // Устанавливаем геометрический PSO и root signature
    cmdList->SetGraphicsRootSignature(geometryRootSig);
    cmdList->SetPipelineState(geometryPSO);
    cmdList->OMSetRenderTargets(GBuffer::GB_COUNT, gbufferRTVs, TRUE, &dsvHandle);

    // Очищаем GBuffer
    float albedoClear[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float dataClear[] = { 0.0f, 0.0f, 0.0f, 1.0f };

    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_ALBEDO], albedoClear, 0, nullptr);
    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_WORLD_POS], dataClear, 0, nullptr);
    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_NORMAL], dataClear, 0, nullptr);
    cmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Устанавливаем константный буфер
    cmdList->SetGraphicsRootConstantBufferView(0, renderData->cbvAddress);

    // Устанавливаем буферы вершин и индексов
    D3D12_VERTEX_BUFFER_VIEW vbv;
    vbv.BufferLocation = renderData->vertexBuffer->GetGPUVirtualAddress();
    vbv.StrideInBytes = sizeof(Vertex);
    vbv.SizeInBytes = (UINT)renderData->vertexBuffer->GetDesc().Width;  // Реальный размер

    D3D12_INDEX_BUFFER_VIEW ibv;
    ibv.BufferLocation = renderData->indexBuffer->GetGPUVirtualAddress();
    ibv.SizeInBytes = (UINT)renderData->indexBuffer->GetDesc().Width;  // Реальный размер
    ibv.Format = DXGI_FORMAT_R32_UINT;

    cmdList->IASetVertexBuffers(0, 1, &vbv);
    cmdList->IASetIndexBuffer(&ibv);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Отрисовка по материалам
    if (renderData->materials && renderData->materialStartIndex && renderData->materialIndexCount) {
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpuStart = renderData->modelSrvHeap->GetGPUDescriptorHandleForHeapStart();

        for (UINT i = 0; i < renderData->numMaterials; i++) {
            if (renderData->materialIndexCount[i] > 0) {
                // Устанавливаем текстуру для материала
                if (renderData->materials[i].textureIndex >= 0) {
                    D3D12_GPU_DESCRIPTOR_HANDLE texHandle;
                    texHandle.ptr = srvGpuStart.ptr + (UINT64)renderData->materials[i].textureIndex * renderData->srvDescriptorSize;
                    cmdList->SetGraphicsRootDescriptorTable(1, texHandle);
                }
                else {
                    // Используем первую текстуру как fallback
                    cmdList->SetGraphicsRootDescriptorTable(1, srvGpuStart);
                }

                // Отрисовываем подгруппу
                cmdList->DrawIndexedInstanced(renderData->materialIndexCount[i], 1, renderData->materialStartIndex[i], 0, 0);
            }
        }
    }
    else {
        // Если нет материалов, рисуем все сразу
        cmdList->DrawIndexedInstanced(renderData->indexCount, 1, 0, 0, 0);
    }

    // ============ LIGHTING PASS ============
    // Переводим GBuffer в состояние SRV
    D3D12_RESOURCE_BARRIER barriers[GBuffer::GB_COUNT];
    for (int i = 0; i < GBuffer::GB_COUNT; ++i) {
        barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barriers[i].Transition.pResource = m_gbuffer->GetResource((GBuffer::GBufferType)i);
        barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    cmdList->ResourceBarrier(GBuffer::GB_COUNT, barriers);

    // Рендерим освещение на back buffer
    cmdList->SetGraphicsRootSignature(m_lightingRootSig.Get());
    cmdList->SetPipelineState(m_lightingPSO.Get());
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    ID3D12DescriptorHeap* ppHeaps[] = { m_gbufferSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    D3D12_GPU_DESCRIPTOR_HANDLE gbufferGpuHandle = m_gbufferSrvHeap->GetGPUDescriptorHandleForHeapStart();
    cmdList->SetGraphicsRootDescriptorTable(0, gbufferGpuHandle);
    cmdList->SetGraphicsRootConstantBufferView(1, m_lightConstantBuffer->GetGPUVirtualAddress());

    D3D12_VIEWPORT fullscreenViewport = { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_RECT fullscreenScissor = { 0, 0, (LONG)m_width, (LONG)m_height };
    cmdList->RSSetViewports(1, &fullscreenViewport);
    cmdList->RSSetScissorRects(1, &fullscreenScissor);

    cmdList->IASetVertexBuffers(0, 1, &m_fullscreenVBView);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmdList->DrawInstanced(4, 1, 0, 0);

    // Возвращаем GBuffer в RTV для следующего кадра
    for (int i = 0; i < GBuffer::GB_COUNT; ++i) {
        barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barriers[i].Transition.pResource = m_gbuffer->GetResource((GBuffer::GBufferType)i);
        barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    cmdList->ResourceBarrier(GBuffer::GB_COUNT, barriers);
}