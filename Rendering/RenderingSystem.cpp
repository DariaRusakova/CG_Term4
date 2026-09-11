#include "RenderingSystem.h"
#include "../Model/Vertex.h"
#include <stdexcept>
#include <directxmath.h>
#include <d3dcompiler.h>
#include <cstring>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

static void CheckHr(HRESULT hr, const char* msg) {
    if (FAILED(hr)) {
        throw std::runtime_error(msg);
    }
}

RenderingSystem::RenderingSystem() : m_gbuffer(std::make_unique<GBuffer>()) {}
RenderingSystem::~RenderingSystem() {}

void RenderingSystem::Initialize(ID3D12Device* device, UINT width, UINT height) {
    m_width = width;
    m_height = height;
    m_srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Создаем комбинированный heap для GBuffer + ShadowMap
    // GBuffer (4) + ShadowMap (1) = 5
    CreateCombinedSrvHeap(device);

    // Инициализация GBuffer
    m_gbuffer->Initialize(device, width, height);

    // Создаем RTV heap для GBuffer
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = GBuffer::GB_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    CheckHr(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_gbufferRtvHeap)), "Create GBuffer RTV Heap");

    // Создаем дескрипторы для GBuffer в комбинированном heap
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_combinedSrvHeap->GetCPUDescriptorHandleForHeapStart();
    m_gbuffer->CreateDescriptors(device,
        m_gbufferRtvHeap->GetCPUDescriptorHandleForHeapStart(),
        cpuHandle,
        m_combinedSrvHeap.Get(),
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV),
        m_srvDescriptorSize);

    // Буферы и PSO
    CreateLightBuffers(device);
    CreateFullscreenQuad(device);
    CreateLightingPassPipeline(device);

    // Настройка освещения
    ClearLights();
    // Основной направленный свет (солнце) - теплый оттенок
    Light sunLight;
    sunLight.type = LightType::Directional;
    sunLight.direction = XMFLOAT3(0.3f, -0.8f, 0.5f);
    sunLight.color = XMFLOAT4(1.0f, 0.9f, 0.7f, 1.0f);
    sunLight.intensity = 0.8f;
    AddLight(sunLight);

    m_originalIntensities.clear();
    for (const auto& light : m_lights) {
        m_originalIntensities.push_back(light.intensity);
    }
    m_globalIntensity = 1.1f;
}

void RenderingSystem::SetShadowResources(ID3D12Resource* shadowCB, D3D12_GPU_DESCRIPTOR_HANDLE shadowSRV) {
    m_externalShadowCB = shadowCB;
    m_externalShadowSRV = shadowSRV;
}

void RenderingSystem::CreateCombinedSrvHeap(ID3D12Device* device) {
    // GBuffer (4) + ShadowMap (1) = 5
    UINT numDescriptors = GBuffer::GB_COUNT + 1;
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = numDescriptors;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CheckHr(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_combinedSrvHeap)), "Create Combined SRV Heap");
}

void RenderingSystem::Resize(UINT width, UINT height) {
    m_width = width;
    m_height = height;
}

void RenderingSystem::AddLight(const Light& light) {
    m_lights.push_back(light);
}

void RenderingSystem::ClearLights() {
    m_lights.clear();
}

void RenderingSystem::CreateLightBuffers(ID3D12Device* device) {
    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = sizeof(LightBufferGPU);
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.Format = DXGI_FORMAT_UNKNOWN;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_lightConstantBuffer));
    CheckHr(hr, "Failed to create light constant buffer");

    D3D12_RANGE readRange = { 0, 0 };
    hr = m_lightConstantBuffer->Map(0, &readRange, &m_lightCBData);
    CheckHr(hr, "Failed to map light constant buffer");
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

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = sizeof(vertices);
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

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
    // VS код
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

    // PS код с PBR + IBL
    static const char* psCode = R"(
        // === G-Buffer текстуры ===
        Texture2D<float4> AlbedoTex : register(t0);
        Texture2D<float4> WorldPosTex : register(t1);
        Texture2D<float4> NormalTex : register(t2);
        Texture2D<float4> PBRTex : register(t3);        // metallic (R), roughness (G), ao (B)

        // === Shadow Map ===
        Texture2DArray<float> ShadowMap : register(t4);
        SamplerState ShadowSampler : register(s1);

        // === Константы ===
        static const float PI = 3.14159265359f;

        // === Константные буферы ===
        cbuffer LightCB : register(b0) {
            float4 lights_pos_type[16];
            float4 lights_dir[16];
            float4 lights_col_int[16];
            float4 lights_range_angle[16];
            uint lightCount;
            float3 padding;
        }

        cbuffer ShadowCB : register(b1) {
            float4x4 lightViewProj[4];
            float4 cascadeSplits;
            float4 lightDirection;
            float4 shadowBias;
            float4 textureSize;
            float4 cameraPos;
            float4 lightPos;
        }

        // === PBR функции ===

        // Distribution GGX (Trowbridge-Reitz)
        float DistributionGGX(float3 N, float3 H, float roughness)
        {
            float a = roughness * roughness;
            float a2 = a * a;
            float NdotH = max(dot(N, H), 0.0f);
            float NdotH2 = NdotH * NdotH;
            
            float num = a2;
            float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
            denom = PI * denom * denom;
            
            return num / denom;
        }

        // Geometry Schlick GGX
        float GeometrySchlickGGX(float NdotV, float roughness)
        {
            float r = (roughness + 1.0f);
            float k = (r * r) / 8.0f;
            
            float num = NdotV;
            float denom = NdotV * (1.0f - k) + k;
            
            return num / denom;
        }

        float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
        {
            float NdotV = max(dot(N, V), 0.0f);
            float NdotL = max(dot(N, L), 0.0f);
            float ggx2 = GeometrySchlickGGX(NdotV, roughness);
            float ggx1 = GeometrySchlickGGX(NdotL, roughness);
            
            return ggx1 * ggx2;
        }

        // Fresnel-Schlick
        float3 FresnelSchlick(float cosTheta, float3 F0)
        {
            return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
        }

        // Cook-Torrance BRDF
        float3 CookTorranceBRDF(
            float3 N, float3 V, float3 L,
            float3 lightColor, float3 albedo,
            float metallic, float roughness, float ao,
            float3 F0)
        {
            float3 H = normalize(V + L);
            float NdotL = max(dot(N, L), 0.0f);
            float NdotV = max(dot(N, V), 0.0f);
            float HdotV = max(dot(H, V), 0.0f);
            
            float3 F0final = lerp(F0, albedo, metallic);
            
            float NDF = DistributionGGX(N, H, roughness);
            float G = GeometrySmith(N, V, L, roughness);
            float3 F = FresnelSchlick(HdotV, F0final);
            
            float3 numerator = NDF * G * F;
            float denominator = 4.0f * NdotV * NdotL + 0.0001f;
            float3 specular = numerator / denominator;
            
            float3 kS = F;
            float3 kD = (1.0f - kS) * (1.0f - metallic);
            
            float3 diffuse = kD * albedo / PI;
            
            return (diffuse + specular) * lightColor * NdotL * ao;
        }

        // === Функция расчета тени ===
        float CalculateShadow(float3 worldPos, int cascadeIndex) {
            float4 posInLightSpace = mul(float4(worldPos, 1.0), lightViewProj[cascadeIndex]);
            float3 projCoords = posInLightSpace.xyz / posInLightSpace.w;
            projCoords.x = projCoords.x * 0.5 + 0.5;
            projCoords.y = projCoords.y * -0.5 + 0.5;
        
            if (projCoords.x < 0.0 || projCoords.x > 1.0 || 
                projCoords.y < 0.0 || projCoords.y > 1.0) {
                return 1.0;
            }
        
            float shadowDepth = ShadowMap.Sample(ShadowSampler, float3(projCoords.xy, cascadeIndex));
        
            float distToLight = length(worldPos - lightPos.xyz);
            float maxDist = 200.0f;
            float normalizedDist = saturate(distToLight / maxDist);
        
            float bias = shadowBias.x;
            return (normalizedDist - bias) <= shadowDepth ? 1.0 : 0.0;
        }

        // === Основной шейдер ===
        float4 main(float4 position : SV_POSITION) : SV_TARGET {
            int2 texPos = int2(position.xy);
            
            // Читаем G-Buffer
            float4 albedo = AlbedoTex.Load(int3(texPos, 0));
            float4 worldPos = WorldPosTex.Load(int3(texPos, 0));
            float4 normalData = NormalTex.Load(int3(texPos, 0));
            float4 pbrData = PBRTex.Load(int3(texPos, 0));
            
            // Пропускаем пустые пиксели
            if (length(worldPos.xyz) < 0.001) {
                return float4(0.0, 0.0, 0.0, 1.0);
            }
            
            // Декодируем данные
            float3 N = normalize(normalData.xyz * 2.0 - 1.0);
            float3 V = normalize(cameraPos.xyz - worldPos.xyz);
            float3 albedoColor = albedo.rgb;
            float metallic = pbrData.r;
            float roughness = pbrData.g;
            float ao = pbrData.b;
            
            // F0 для диэлектриков (по умолчанию 0.04)
            float3 F0 = float3(0.04, 0.04, 0.04);
            
            // Выбор каскада для теней
            float depthFromCam = length(worldPos.xyz - cameraPos.xyz);
            int cascadeIndex = 0;
            if (depthFromCam > cascadeSplits.x) cascadeIndex = 1;
            if (depthFromCam > cascadeSplits.y) cascadeIndex = 2;
            if (depthFromCam > cascadeSplits.z) cascadeIndex = 3;
            
            float shadowFactor = CalculateShadow(worldPos.xyz, cascadeIndex);
            
            // === Прямой свет ===
            float3 finalColor = float3(0, 0, 0);
            
            for (uint i = 0; i < lightCount; i++) {
                uint type = (uint)lights_pos_type[i].w;
                float3 L;
                float attenuation = 1.0;
                float spotAtten = 1.0;
                float3 lightColor = lights_col_int[i].rgb * lights_col_int[i].a;
                
                if (type == 1) { // Directional
                    L = normalize(-lights_dir[i].xyz);
                    float shadowMult = (i == 0) ? shadowFactor : 1.0f;
                    float3 brdf = CookTorranceBRDF(N, V, L, lightColor, albedoColor, metallic, roughness, ao, F0);
                    finalColor += brdf * shadowMult;
                }
                else if (type == 0) { // Point
                    float3 lightVec = lights_pos_type[i].xyz - worldPos.xyz;
                    float dist = length(lightVec);
                    L = normalize(lightVec);
                    attenuation = saturate(1.0 - dist / lights_range_angle[i].x);
                    attenuation *= attenuation;
                    float3 brdf = CookTorranceBRDF(N, V, L, lightColor, albedoColor, metallic, roughness, ao, F0);
                    finalColor += brdf * attenuation;
                }
                else if (type == 2) { // Spot
                    float3 lightVec = lights_pos_type[i].xyz - worldPos.xyz;
                    float dist = length(lightVec);
                    L = normalize(lightVec);
                    attenuation = saturate(1.0 - dist / lights_range_angle[i].x);
                    attenuation *= attenuation;
                    float spotCos = dot(-L, normalize(lights_dir[i].xyz));
                    float cutoff = cos(lights_range_angle[i].y);
                    spotAtten = smoothstep(cutoff, cutoff + 0.2, spotCos);
                    float3 brdf = CookTorranceBRDF(N, V, L, lightColor, albedoColor, metallic, roughness, ao, F0);
                    finalColor += brdf * attenuation * spotAtten;
                }
            }
            
            // === Ambient (простой, без IBL) ===
            // Пока IBL не реализован, используем простой ambient
            float3 ambient = albedoColor * 0.03f * ao;
            finalColor += ambient;
            
            // HDR тонмаппинг (Reinhard)
            finalColor = finalColor / (finalColor + float3(1.0, 1.0, 1.0));
            
            // Гамма-коррекция
            finalColor = pow(finalColor, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));
            
            return float4(finalColor, 1.0);
        }
    )";

    ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;
    HRESULT hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    CheckHr(hr, "Failed to compile lighting VS");

    hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        CheckHr(hr, "Failed to compile lighting PS");
    }

    // ============================================
    // КОРНЕВАЯ СИГНАТУРА
    // ============================================

    // Дескрипторы для текстур: Albedo, WorldPos, Normal, PBR, ShadowMap
    D3D12_DESCRIPTOR_RANGE descRange = {};
    descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descRange.NumDescriptors = 5; // 4 GBuffer + ShadowMap
    descRange.BaseShaderRegister = 0;
    descRange.RegisterSpace = 0;
    descRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[3] = {};

    // Root Parameter 0: Дескрипторная таблица (текстуры)
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &descRange;

    // Root Parameter 1: CBV с данными света
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].Descriptor.RegisterSpace = 0;

    // Root Parameter 2: CBV с данными теней
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[2].Descriptor.ShaderRegister = 1;
    rootParams[2].Descriptor.RegisterSpace = 0;

    // Сэмплеры
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};

    // s0 - обычный сэмплер для GBuffer
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].ShaderRegister = 0;
    samplers[0].RegisterSpace = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1 - сэмплер для теней (сравнение)
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[1].ShaderRegister = 1;
    samplers[1].RegisterSpace = 0;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 3;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 2;
    rootSigDesc.pStaticSamplers = samplers;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errorBlob);
    CheckHr(hr, "Failed to serialize lighting root sig");

    hr = device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_lightingRootSig));
    CheckHr(hr, "Failed to create lighting root sig");

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

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_lightingPSO));
    CheckHr(hr, "Failed to create lighting PSO");
}

void RenderingSystem::Render(ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* depthStencil,
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
    ID3D12RootSignature* geometryRootSig,
    ID3D12PipelineState* geometryPSO,
    const RenderData* renderData) {

    if (!renderData || !renderData->vertexBuffer || !renderData->indexBuffer) return;
    if (!m_lightCBData) return;

    // Обновляем данные света
    memset(m_lightCBData, 0, sizeof(LightBufferGPU));
    LightBufferGPU* lightData = reinterpret_cast<LightBufferGPU*>(m_lightCBData);
    lightData->lightCount = std::min((UINT)m_lights.size(), 16u);
    for (UINT i = 0; i < lightData->lightCount; ++i) {
        const Light& light = m_lights[i];
        lightData->lights[i].position_type = light.GetAsFloat4();
        lightData->lights[i].direction = light.GetDirectionAsFloat4();
        lightData->lights[i].color_intensity = XMFLOAT4(light.color.x, light.color.y, light.color.z, light.intensity * m_globalIntensity);
        lightData->lights[i].range_spotAngle = XMFLOAT4(light.range, light.spotAngle, 0.0f, 0.0f);
    }

    // ============================================
    // УСТАНАВЛИВАЕМ VIEWPORT ДЛЯ ВСЕГО РЕНДЕРА
    // ============================================
    UINT viewportWidth = m_backbufferWidth > 0 ? m_backbufferWidth : m_width;
    UINT viewportHeight = m_backbufferHeight > 0 ? m_backbufferHeight : m_height;

    D3D12_VIEWPORT mainViewport = { 0.0f, 0.0f, (float)viewportWidth, (float)viewportHeight, 0.0f, 1.0f };
    D3D12_RECT mainScissor = { 0, 0, (LONG)viewportWidth, (LONG)viewportHeight };

    cmdList->RSSetViewports(1, &mainViewport);
    cmdList->RSSetScissorRects(1, &mainScissor);

    // ============================================
    // GEOMETRY PASS
    // ============================================
    D3D12_CPU_DESCRIPTOR_HANDLE gbufferRTVs[GBuffer::GB_COUNT];
    for (int i = 0; i < GBuffer::GB_COUNT; ++i) {
        gbufferRTVs[i] = m_gbuffer->GetRTV((GBuffer::GBufferType)i);
    }

    cmdList->SetGraphicsRootSignature(geometryRootSig);
    cmdList->SetPipelineState(geometryPSO);
    cmdList->OMSetRenderTargets(GBuffer::GB_COUNT, gbufferRTVs, TRUE, &dsvHandle);

    float albedoClear[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float dataClear[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float pbrClear[] = { 0.0f, 0.5f, 1.0f, 0.0f }; // metallic=0, roughness=0.5, ao=1.0
    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_ALBEDO], albedoClear, 0, nullptr);
    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_WORLD_POS], dataClear, 0, nullptr);
    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_NORMAL], dataClear, 0, nullptr);
    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_PBR], pbrClear, 0, nullptr);
    cmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    cmdList->SetGraphicsRootConstantBufferView(0, renderData->cbvAddress);

    D3D12_VERTEX_BUFFER_VIEW vbv;
    vbv.BufferLocation = renderData->vertexBuffer->GetGPUVirtualAddress();
    vbv.StrideInBytes = sizeof(Vertex);
    vbv.SizeInBytes = (UINT)renderData->vertexBuffer->GetDesc().Width;

    D3D12_INDEX_BUFFER_VIEW ibv;
    ibv.BufferLocation = renderData->indexBuffer->GetGPUVirtualAddress();
    ibv.SizeInBytes = (UINT)renderData->indexBuffer->GetDesc().Width;
    ibv.Format = DXGI_FORMAT_R32_UINT;

    cmdList->IASetVertexBuffers(0, 1, &vbv);
    cmdList->IASetIndexBuffer(&ibv);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    if (renderData->materials && renderData->materialStartIndex && renderData->materialIndexCount) {
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpuStart = renderData->modelSrvHeap->GetGPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < renderData->numMaterials; i++) {
            if (renderData->materialIndexCount[i] > 0) {
                if (renderData->materials[i].textureIndex >= 0) {
                    D3D12_GPU_DESCRIPTOR_HANDLE texHandle;
                    texHandle.ptr = srvGpuStart.ptr + (UINT64)renderData->materials[i].textureIndex * renderData->srvDescriptorSize;
                    cmdList->SetGraphicsRootDescriptorTable(1, texHandle);
                }
                else {
                    cmdList->SetGraphicsRootDescriptorTable(1, srvGpuStart);
                }
                cmdList->DrawIndexedInstanced(renderData->materialIndexCount[i], 1, renderData->materialStartIndex[i], 0, 0);
            }
        }
    }
    else {
        cmdList->DrawIndexedInstanced(renderData->indexCount, 1, 0, 0, 0);
    }

    // ============================================
    // ПЕРЕХОД GBuffer ИЗ RENDER_TARGET В PIXEL_SHADER_RESOURCE
    // ============================================
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

    // ============================================
    // LIGHTING PASS
    // ============================================
    cmdList->RSSetViewports(1, &mainViewport);
    cmdList->RSSetScissorRects(1, &mainScissor);

    ID3D12DescriptorHeap* ppHeaps[] = { m_combinedSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    cmdList->SetGraphicsRootSignature(m_lightingRootSig.Get());
    cmdList->SetPipelineState(m_lightingPSO.Get());
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Устанавливаем дескрипторную таблицу с ВСЕМИ текстурами
    D3D12_GPU_DESCRIPTOR_HANDLE combinedGpuHandle = m_combinedSrvHeap->GetGPUDescriptorHandleForHeapStart();
    cmdList->SetGraphicsRootDescriptorTable(0, combinedGpuHandle);

    // Устанавливаем CBV для света и теней
    cmdList->SetGraphicsRootConstantBufferView(1, m_lightConstantBuffer->GetGPUVirtualAddress());
    if (m_externalShadowCB) {
        cmdList->SetGraphicsRootConstantBufferView(2, m_externalShadowCB->GetGPUVirtualAddress());
    }

    cmdList->IASetVertexBuffers(0, 1, &m_fullscreenVBView);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmdList->DrawInstanced(4, 1, 0, 0);

    // ============================================
    // ВОЗВРАЩАЕМ GBuffer В СОСТОЯНИЕ RENDER_TARGET
    // ============================================
    for (int i = 0; i < GBuffer::GB_COUNT; ++i) {
        barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    cmdList->ResourceBarrier(GBuffer::GB_COUNT, barriers);
}

void RenderingSystem::RenderShadowMapDebug(ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle) {
    // Создаем временный PSO для отладки
    static Microsoft::WRL::ComPtr<ID3D12PipelineState> debugPSO;
    static Microsoft::WRL::ComPtr<ID3D12RootSignature> debugRootSig;
    static bool initialized = false;

    if (!initialized) {
        // Получаем устройство через command list (упрощенно)
        ID3D12Device* device = nullptr;
        cmdList->GetDevice(IID_PPV_ARGS(&device));

        // VS для fullscreen quad
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

        // PS для визуализации ShadowMap
        static const char* psCode = R"(
        Texture2DArray<float> ShadowMap : register(t4);
        SamplerState ShadowSampler : register(s1);
        
        float4 main(float4 position : SV_POSITION) : SV_TARGET {
            float2 uv = position.xy / 1920.0;
            float shadowValue = ShadowMap.Sample(ShadowSampler, float3(uv, 0));
            return float4(shadowValue, shadowValue, shadowValue, 1.0);
        }
        )";

        ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;
        D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
        D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);

        // Корневая сигнатура
        D3D12_ROOT_PARAMETER rootParams[1] = {};
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_DESCRIPTOR_RANGE descRange = {};
        descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descRange.NumDescriptors = 1;
        descRange.BaseShaderRegister = 4; // t4 - ShadowMap
        descRange.RegisterSpace = 0;

        rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[0].DescriptorTable.pDescriptorRanges = &descRange;

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister = 1;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
        rootSigDesc.NumParameters = 1;
        rootSigDesc.pParameters = rootParams;
        rootSigDesc.NumStaticSamplers = 1;
        rootSigDesc.pStaticSamplers = &sampler;
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> signature;
        D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errorBlob);
        device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&debugRootSig));

        // PSO
        D3D12_INPUT_ELEMENT_DESC inputDesc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputDesc, _countof(inputDesc) };
        psoDesc.pRootSignature = debugRootSig.Get();
        psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;

        device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&debugPSO));
        device->Release();
        initialized = true;
    }

    // Устанавливаем RTV
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Устанавливаем дескрипторный heap
    ID3D12DescriptorHeap* ppHeaps[] = { m_combinedSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    // Устанавливаем корневую сигнатуру и PSO
    cmdList->SetGraphicsRootSignature(debugRootSig.Get());
    cmdList->SetPipelineState(debugPSO.Get());

    // Устанавливаем дескрипторную таблицу (ShadowMap на t4)
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_combinedSrvHeap->GetGPUDescriptorHandleForHeapStart();
    gpuHandle.ptr += GBuffer::GB_COUNT * m_srvDescriptorSize; // Сдвигаем к ShadowMap
    cmdList->SetGraphicsRootDescriptorTable(0, gpuHandle);

    // Рендерим fullscreen quad
    cmdList->IASetVertexBuffers(0, 1, &m_fullscreenVBView);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmdList->DrawInstanced(4, 1, 0, 0);
}

void RenderingSystem::RenderPostProcess(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV,
    UINT targetWidth,
    UINT targetHeight,
    PostProcessSystem::EffectType effect)
{
    if (effect == PostProcessSystem::EffectType::Off)
    {
        return;
    }

    if (!m_postProcessSystem)
        return;

    ID3D12Resource* albedo = m_gbuffer->GetResource(GBuffer::GB_ALBEDO);
    ID3D12Resource* worldPos = m_gbuffer->GetResource(GBuffer::GB_WORLD_POS);
    ID3D12Resource* normal = m_gbuffer->GetResource(GBuffer::GB_NORMAL);

    m_postProcessSystem->SetGBufferResources(albedo, worldPos, normal);

    m_postProcessSystem->Render(
        cmdList,
        backBufferRTV,
        targetWidth,
        targetHeight,
        effect
    );
}