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

    // Создаем комбинированный heap для GBuffer + место под тень
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
    sunLight.direction = XMFLOAT3(0.3f, -0.8f, 0.5f);  // Направление солнца
    sunLight.color = XMFLOAT4(1.0f, 0.9f, 0.7f, 1.0f);  // Теплый солнечный цвет
    sunLight.intensity = 0.8f;
    AddLight(sunLight);

    //// Точечный свет в центре атриума (теплый)
    //Light centerLight;
    //centerLight.type = LightType::Point;
    //centerLight.position = XMFLOAT3(0.0f, 4.0f, 0.0f);
    //centerLight.color = XMFLOAT4(1.0f, 0.85f, 0.6f, 1.0f);
    //centerLight.intensity = 15.0f;
    //centerLight.range = 15.0f;
    //AddLight(centerLight);

    ////// Боковые точечные источники для подсветки колонн
    //Light leftColumn;
    //leftColumn.type = LightType::Point;
    //leftColumn.position = XMFLOAT3(-6.0f, 3.0f, -3.0f);
    //leftColumn.color = XMFLOAT4(0.9f, 0.8f, 0.7f, 1.0f);
    //leftColumn.intensity = 10.0f;
    //leftColumn.range = 10.0f;
    //AddLight(leftColumn);

    //Light rightColumn;
    //rightColumn.type = LightType::Point;
    //rightColumn.position = XMFLOAT3(6.0f, 3.0f, 3.0f);
    //rightColumn.color = XMFLOAT4(0.9f, 0.8f, 0.7f, 1.0f);
    //rightColumn.intensity = 10.0f;
    //rightColumn.range = 10.0f;
    //AddLight(rightColumn);

    //////// Свет сзади для подсветки задней стены
    //Light backWall;
    //backWall.type = LightType::Point;
    //backWall.position = XMFLOAT3(0.0f, 5.0f, -8.0f);
    //backWall.color = XMFLOAT4(1.0f, 0.85f, 0.7f, 1.0f);
    //backWall.intensity = 8.0f;
    //backWall.range = 12.0f;
    //AddLight(backWall);

    //////// Передний свет для подсветки входа
    //Light frontLight;
    //frontLight.type = LightType::Point;
    //frontLight.position = XMFLOAT3(0.0f, 3.0f, 8.0f);
    //frontLight.color = XMFLOAT4(0.8f, 0.9f, 1.0f, 1.0f);  // Немного холоднее для контраста
    //frontLight.intensity = 12.0f;
    //frontLight.range = 14.0f;
    //AddLight(frontLight);

    //////// Spot свет сверху - как свет через окно
    //Light skylight;
    //skylight.type = LightType::Spot;
    //skylight.position = XMFLOAT3(0.0f, 10.0f, 0.0f);
    //skylight.direction = XMFLOAT3(0.0f, -1.0f, 0.1f);
    //skylight.color = XMFLOAT4(1.0f, 0.95f, 0.85f, 1.0f);
    //skylight.intensity = 25.0f;
    //skylight.range = 25.0f;
    //skylight.spotAngle = 40.0f * XM_PI / 180.0f;
    //AddLight(skylight);

    m_originalIntensities.clear();
    for (const auto& light : m_lights) {
        m_originalIntensities.push_back(light.intensity);
    }
    m_globalIntensity = 1.1f;
}

void RenderingSystem::SetShadowResources(ID3D12Resource* shadowCB, D3D12_GPU_DESCRIPTOR_HANDLE shadowSRV) {
    m_externalShadowCB = shadowCB;
    m_externalShadowSRV = shadowSRV;

    // Копируем дескриптор SRV тени в наш комбинированный хип (после GBuffer)
    // Предполагаем, что устройство доступно через контекст или передаем его, 
    // но так как у нас нет доступа к device здесь, мы должны сделать это при инициализации 
    // или передать device. Для простоты, предположим, что мы копируем CPU handle, 
    // но в DX12 лучше копировать дескрипторы через устройство.
    // В данном случае, так как SRV не меняется, мы можем просто сохранить GPU handle 
    // и использовать его напрямую, если шейдер позволяет, или обновить хип.

    // Примечание: В текущей архитектуре Lighting PS использует Descriptor Table (root param 0).
    // Нам нужно убедиться, что SRV тени находится в этом table.
    // Мы уже зарезервировали место в CreateCombinedSrvHeap.
    // Здесь мы должны скопировать дескриптор. Так как device нет в аргументах, 
    // давайте предположим, что мы будем делать это в D3D12App или передадим device.
    // Для сейчас просто сохраним handles.
}

void RenderingSystem::CreateCombinedSrvHeap(ID3D12Device* device) {
    // GBuffer (3) + ShadowMap (1) + RoofTextures (3)
    UINT numDescriptors = GBuffer::GB_COUNT + 1 + ROOF_TEXTURE_COUNT; // = 7
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
        Texture2D<float4> AlbedoTex    : register(t0);
        Texture2D<float4> WorldPosTex  : register(t1);
        Texture2D<float4> NormalTex    : register(t2);
        Texture2DArray<float> ShadowMap : register(t3);
        Texture2D<float4> RoofTex0     : register(t4);   // sponza_fabric_diff
        Texture2D<float4> RoofTex1     : register(t5);   // sponza_fabric_blue_diff
        Texture2D<float4> RoofTex2     : register(t6);   // sponza_fabric_green_diff

        SamplerState ShadowSampler : register(s0);   // CLAMP — для shadow map
        SamplerState RoofSampler   : register(s1);   // WRAP  — для roof-текстур

        struct LightData {
            float4 position_type;
            float4 direction;
            float4 color_intensity;
            float4 range_spotAngle;
        };

        cbuffer LightCB : register(b0) {
            LightData lights[16];
            uint lightCount;
            uint debugMode;
            float2 padding;
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

        static const float3 kCascadeColors[4] = {
            float3(1.0, 0.2, 0.2),
            float3(0.2, 1.0, 0.2),
            float3(0.2, 0.4, 1.0),
            float3(1.0, 1.0, 0.2)
        };

        // Возвращает цвет roof-текстуры для текущего каскада.
        // Каскад 0 и 3 — базовая ткань, 1 — синяя, 2 — зелёная.
        float3 SampleRoofTexture(int cascadeIndex, float2 uv) {
            if (cascadeIndex == 1) {
                return RoofTex1.Sample(RoofSampler, uv).rgb;
            }
            else if (cascadeIndex == 2) {
                return RoofTex2.Sample(RoofSampler, uv).rgb;
            }
            else {
                return RoofTex0.Sample(RoofSampler, uv).rgb;
            }
        }

        float CalculateShadow(float3 worldPos, int cascadeIndex, out float2 outShadowUV) {
            float4 posInLightSpace = mul(float4(worldPos, 1.0), lightViewProj[cascadeIndex]);
            float3 projCoords = posInLightSpace.xyz / posInLightSpace.w;
            projCoords.x = projCoords.x * 0.5 + 0.5;
            projCoords.y = projCoords.y * -0.5 + 0.5;

            outShadowUV = projCoords.xy;

            if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
                projCoords.y < 0.0 || projCoords.y > 1.0) {
                return 1.0;
            }

            float distToLight = length(worldPos - lightPos.xyz);
            float maxDist = 200.0f;
            float normalizedDist = saturate(distToLight / maxDist);

            float bias = shadowBias.x;

            float radius = float(cascadeIndex) * 1.5;
            float2 texelSize = float2(textureSize.z, textureSize.w);

            float shadow = 0.0;
            const int PCF_RADIUS = 2;
            int samples = 0;

            for (int x = -PCF_RADIUS; x <= PCF_RADIUS; ++x) {
                for (int y = -PCF_RADIUS; y <= PCF_RADIUS; ++y) {
                    float2 offset = float2(x, y) * texelSize * radius;
                    float shadowDepth = ShadowMap.Sample(
                        ShadowSampler,
                        float3(projCoords.xy + offset, cascadeIndex));
                    shadow += (normalizedDist - bias) <= shadowDepth ? 1.0 : 0.0;
                    samples++;
                }
            }

            return shadow / float(samples);
        }

        int SelectCascade(float depthFromCam) {
            int idx = 0;
            if (depthFromCam > cascadeSplits.x) idx = 1;
            if (depthFromCam > cascadeSplits.y) idx = 2;
            if (depthFromCam > cascadeSplits.z) idx = 3;
            return idx;
        }

        float4 main(float4 position : SV_POSITION) : SV_TARGET {
            int3 texPos = int3(position.xy, 0);
            float4 albedo     = AlbedoTex.Load(texPos);
            float4 worldPos   = WorldPosTex.Load(texPos);
            float4 normalData = NormalTex.Load(texPos);

            if (length(worldPos.xyz) < 0.001) {
                return float4(0.0, 0.0, 0.0, 1.0);
            }

            float3 N = normalize(normalData.xyz * 2.0 - 1.0);
            float3 V = normalize(cameraPos.xyz - worldPos.xyz);

            float depthFromCam = length(worldPos.xyz - cameraPos.xyz);
            int cascadeIndex = SelectCascade(depthFromCam);

            float2 shadowUV;
            float shadowFactor = CalculateShadow(worldPos.xyz, cascadeIndex, shadowUV);

            // ============================================================
            // РЕЖИМ 1: цветовая визуализация каскадов
            // ============================================================
            if (debugMode == 1) {
                float3 cascadeColor = kCascadeColors[cascadeIndex];
                float3 vis = lerp(albedo.rgb * 0.3, cascadeColor, 0.8);
                vis *= lerp(0.4, 1.0, shadowFactor);
                return float4(vis, 1.0);
            }

            // ============================================================
            // РЕЖИМ 2: подсветка границ каскадов
            // ============================================================
            if (debugMode == 2) {
                float blendWidth = 0.3;
                float3 baseColor = albedo.rgb * (0.3 + 0.7 * shadowFactor);

                float d0 = abs(depthFromCam - cascadeSplits.x);
                float d1 = abs(depthFromCam - cascadeSplits.y);
                float d2 = abs(depthFromCam - cascadeSplits.z);

                float edge = 0.0;
                if (d0 < blendWidth) edge = max(edge, 1.0 - d0 / blendWidth);
                if (d1 < blendWidth) edge = max(edge, 1.0 - d1 / blendWidth);
                if (d2 < blendWidth) edge = max(edge, 1.0 - d2 / blendWidth);

                float3 edgeColor = float3(1.0, 0.0, 0.0);
                float3 vis = lerp(baseColor, edgeColor, edge);
                return float4(vis, 1.0);
            }

            // ============================================================
            // ОБЫЧНЫЙ РЕЖИМ ОСВЕЩЕНИЯ
            // ============================================================
            float3 finalColor = albedo.rgb * 0.15;

            // Сэмплим roof-текстуру, соответствующую каскаду.
            // Тайлинг 6x6 — паттерн ткани повторяется.
            float2 roofUV = shadowUV * 6.0;
            float3 roofColor = SampleRoofTexture(cascadeIndex, roofUV);

            for (uint i = 0; i < lightCount; i++) {
                uint type = (uint)lights[i].position_type.w;
                float3 L;
                float attenuation = 1.0;
                float spotAtten = 1.0;
                float3 lightColor = lights[i].color_intensity.rgb *
                                    lights[i].color_intensity.a;

                if (type == 1) { // Directional — с тенями и roof-подмешиванием
                    L = normalize(-lights[i].direction.xyz);
                    float diffuse = saturate(dot(N, L));

                    float3 litColor = lightColor * diffuse;

                    // Затенённая часть: roof-цвет, модулированный albedo
                    // В разных каскадах — разная ткань (синяя/зелёная/базовая)
                    float3 shadowColor = roofColor * albedo.rgb * 0.35;

                    finalColor += lerp(shadowColor, litColor, shadowFactor);
                }
                else if (type == 0) { // Point
                    float3 lightVec = lights[i].position_type.xyz - worldPos.xyz;
                    float dist = length(lightVec);
                    L = normalize(lightVec);
                    attenuation = saturate(1.0 - dist / lights[i].range_spotAngle.x);
                    attenuation *= attenuation;
                    float diffuse = saturate(dot(N, L));
                    finalColor += lightColor * diffuse * attenuation * spotAtten;
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
                    float diffuse = saturate(dot(N, L));
                    finalColor += lightColor * diffuse * attenuation * spotAtten;
                }
            }

            finalColor *= albedo.rgb;
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
    // КОРНЕВАЯ СИГНАТУРА: 7 SRV
    // ============================================
    D3D12_DESCRIPTOR_RANGE descRange = {};
    descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descRange.NumDescriptors = 7;   // t0..t6: GBuffer(3) + ShadowMap(1) + RoofTex(3)
    descRange.BaseShaderRegister = 0;
    descRange.RegisterSpace = 0;
    descRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[3] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &descRange;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].Descriptor.RegisterSpace = 0;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[2].Descriptor.ShaderRegister = 1;
    rootParams[2].Descriptor.RegisterSpace = 0;

    D3D12_STATIC_SAMPLER_DESC shadowSamplerDesc = {};
    shadowSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    shadowSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    shadowSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    shadowSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    shadowSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    shadowSamplerDesc.ShaderRegister = 0;
    shadowSamplerDesc.RegisterSpace = 0;
    shadowSamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC roofSamplerDesc = {};
    roofSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    roofSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    roofSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    roofSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    roofSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    roofSamplerDesc.ShaderRegister = 1;
    roofSamplerDesc.RegisterSpace = 0;
    roofSamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[] = { shadowSamplerDesc, roofSamplerDesc };

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

    lightData->debugMode = m_debugMode;

    for (UINT i = 0; i < lightData->lightCount; ++i) {
        const Light& light = m_lights[i];
        lightData->lights[i].position_type = light.GetAsFloat4();
        lightData->lights[i].direction = light.GetDirectionAsFloat4();
        lightData->lights[i].color_intensity = XMFLOAT4(light.color.x, light.color.y, light.color.z, light.intensity * m_globalIntensity);
        lightData->lights[i].range_spotAngle = XMFLOAT4(light.range, light.spotAngle, 0.0f, 0.0f);
    }

    D3D12_VIEWPORT mainViewport = { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_RECT mainScissor = { 0, 0, (LONG)m_width, (LONG)m_height };
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
    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_ALBEDO], albedoClear, 0, nullptr);
    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_WORLD_POS], dataClear, 0, nullptr);
    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_NORMAL], dataClear, 0, nullptr);
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
        Texture2DArray<float> ShadowMap : register(t3);
        SamplerState ShadowSampler : register(s0);
        
        float4 main(float4 position : SV_POSITION) : SV_TARGET {
            float2 uv = position.xy / 1920.0; // Используем разрешение экрана
            
            // Читаем из теневой карты
            float shadowValue = ShadowMap.Sample(ShadowSampler, float3(uv, 0));
            
            // Если значение 0 - черный, если 1 - белый
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
        descRange.BaseShaderRegister = 3; // t3 - ShadowMap
        descRange.RegisterSpace = 0;

        rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[0].DescriptorTable.pDescriptorRanges = &descRange;

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
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

    // Устанавливаем дескрипторную таблицу (ShadowMap на t3)
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_combinedSrvHeap->GetGPUDescriptorHandleForHeapStart();
    gpuHandle.ptr += GBuffer::GB_COUNT * m_srvDescriptorSize; // Сдвигаем к ShadowMap
    cmdList->SetGraphicsRootDescriptorTable(0, gpuHandle);

    // Рендерим fullscreen quad
    cmdList->IASetVertexBuffers(0, 1, &m_fullscreenVBView);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmdList->DrawInstanced(4, 1, 0, 0);
}

void RenderingSystem::LoadRoofTextures(ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const std::string paths[ROOF_TEXTURE_COUNT]) {
    // Индексы 4, 5, 6 — после GBuffer[0..2] и ShadowMap[3]
    D3D12_CPU_DESCRIPTOR_HANDLE baseHandle =
        m_combinedSrvHeap->GetCPUDescriptorHandleForHeapStart();
    baseHandle.ptr += (GBuffer::GB_COUNT + 1) * m_srvDescriptorSize;

    for (UINT i = 0; i < ROOF_TEXTURE_COUNT; ++i) {
        m_roofTextures[i] = TextureLoader::LoadTexture(device, cmdList, paths[i]);

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = baseHandle;
        cpuHandle.ptr += i * m_srvDescriptorSize;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = m_roofTextures[i].format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;

        device->CreateShaderResourceView(m_roofTextures[i].resource.Get(), &srvDesc, cpuHandle);

        char buf[256];
        sprintf_s(buf, "[RenderingSystem] Roof texture %u loaded: %s\n", i, paths[i].c_str());
        OutputDebugStringA(buf);
    }

    m_roofTexturesLoaded = true;
}