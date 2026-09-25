#include "RenderingSystem.h"
#include "../Model/Vertex.h" 
#include <stdexcept>
#include <directxmath.h>
#include <d3dcompiler.h>
#include <cstring>
#include "libs/DDSTextureLoader12.h"
#include "libs/d3dx12.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

RenderingSystem::RenderingSystem() : m_gbuffer(std::make_unique<GBuffer>()) {}

RenderingSystem::~RenderingSystem() {}

void RenderingSystem::Initialize(ID3D12Device* device, UINT width, UINT height) {
    m_width = width;
    m_height = height;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = GBuffer::GB_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_gbufferRtvHeap));

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = GBuffer::GB_COUNT + 16 + 3;  
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_gbufferSrvHeap));

    UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    UINT srvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_srvDescriptorSize = srvSize;

    m_gbuffer->Initialize(device, width, height);

    m_gbuffer->CreateDescriptors(device,
        m_gbufferRtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_gbufferSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_gbufferSrvHeap.Get(),  
        rtvSize, srvSize);

    CreateLightBuffers(device);
    CreateFullscreenQuad(device);
    CreateLightingPassPipeline(device);
    CreateLightVisPipeline(device);

    ClearLights();

  
    Light ambient;
    ambient.type = LightType::Point;
    ambient.position = XMFLOAT3(0.0f, 6.0f, 0.0f);
    ambient.color = XMFLOAT4(0.8f, 0.85f, 1.0f, 1.0f);
    ambient.intensity = 1.0f;
    ambient.range = 30.0f;
    AddLight(ambient);

    

    m_globalIntensity = 0.1f;

}

void RenderingSystem::CreateIBLResources(ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList)
{
    const UINT iblBase = GBuffer::GB_COUNT;  
    UINT srvSize = m_srvDescriptorSize;       

    auto loadDDS = [&](const wchar_t* path,
        ComPtr<ID3D12Resource>& outTexture,
        std::unique_ptr<uint8_t[]>& outDdsData,
        std::vector<D3D12_SUBRESOURCE_DATA>& outSubresources,
        bool* outIsCube) -> bool
        {
            HRESULT hr = DirectX::LoadDDSTextureFromFile(
                device, path,
                &outTexture, outDdsData, outSubresources,
                0, nullptr, outIsCube);

            if (FAILED(hr) || !outTexture) {
                char buf[512];
                sprintf_s(buf, "[IBL] Failed to load %ls (hr=0x%08X)\n", path, hr);
                OutputDebugStringA(buf);
                return false;
            }

            char buf[512];
            sprintf_s(buf, "[IBL] Loaded %ls: format=%u mips=%u subres=%zu isCube=%d\n",
                path,
                (UINT)outTexture->GetDesc().Format,
                (UINT)outTexture->GetDesc().MipLevels,
                outSubresources.size(),
                outIsCube ? (int)*outIsCube : -1);
            OutputDebugStringA(buf);
            return true;
        };

    auto uploadAndBarrier = [&](const wchar_t* path,
        ComPtr<ID3D12Resource>& outTexture,
        bool* outIsCube) -> bool
        {
            std::unique_ptr<uint8_t[]> ddsData;
            std::vector<D3D12_SUBRESOURCE_DATA> subresources;

            if (!loadDDS(path, outTexture, ddsData, subresources, outIsCube)) {
                return false;
            }

            m_iblDdsData.push_back(std::move(ddsData));

            const UINT64 uploadSize = GetRequiredIntermediateSize(
                outTexture.Get(), 0, (UINT)subresources.size());

            D3D12_HEAP_PROPERTIES uploadHeapProps = {};
            uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC uploadDesc = {};
            uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            uploadDesc.Width = uploadSize;
            uploadDesc.Height = 1;
            uploadDesc.DepthOrArraySize = 1;
            uploadDesc.MipLevels = 1;
            uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
            uploadDesc.SampleDesc.Count = 1;
            uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            ComPtr<ID3D12Resource> uploadBuffer;
            HRESULT hr = device->CreateCommittedResource(
                &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&uploadBuffer));

            if (FAILED(hr)) {
                OutputDebugStringA("[IBL] Failed to create upload buffer\n");
                return false;
            }

            UpdateSubresources(cmdList, outTexture.Get(), uploadBuffer.Get(),
                0, 0, (UINT)subresources.size(), subresources.data());

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = outTexture.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &barrier);

            m_iblUploadBuffers.push_back(uploadBuffer);
            return true;
        };

    bool isCube = false;
    if (!uploadAndBarrier(L"assets/ibl/IrradianceMap_BC6U.dds",
        m_irradianceMap, &isCube)) {
        return;
    }
    if (!uploadAndBarrier(L"assets/ibl/PreFilteredEnvMap_BC6U.dds",
        m_prefilteredMap, &isCube)) {
        m_irradianceMap.Reset();
        return;
    }
    if (!uploadAndBarrier(L"assets/ibl/IntegrationMap.dds",
        m_brdfLUT, &isCube)) {
        m_irradianceMap.Reset();
        m_prefilteredMap.Reset();
        return;
    }

    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            m_gbufferSrvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += (UINT64)(iblBase + 0) * srvSize;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = m_irradianceMap->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = m_irradianceMap->GetDesc().MipLevels;
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

        device->CreateShaderResourceView(m_irradianceMap.Get(), &srvDesc, handle);
    }

    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            m_gbufferSrvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += (UINT64)(iblBase + 1) * srvSize;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = m_prefilteredMap->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = m_prefilteredMap->GetDesc().MipLevels;
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

        device->CreateShaderResourceView(m_prefilteredMap.Get(), &srvDesc, handle);
    }

    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            m_gbufferSrvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += (UINT64)(iblBase + 2) * srvSize;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = m_brdfLUT->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = m_brdfLUT->GetDesc().MipLevels;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        device->CreateShaderResourceView(m_brdfLUT.Get(), &srvDesc, handle);
    }

    m_iblReady = true;
    OutputDebugStringA("[IBL] All IBL resources ready\n");
}

void RenderingSystem::Resize(UINT width, UINT height) {
    m_width = width;
    m_height = height;
    m_gbuffer->Release();
}

void RenderingSystem::AddLight(const Light& light) {
    m_staticLights.push_back(light);
    m_originalIntensities.push_back(light.intensity);
}

void RenderingSystem::ClearLights() {
    m_staticLights.clear();
    m_lights.clear();
    m_originalIntensities.clear();
    for (UINT i = 0; i < kMaxProjectiles; ++i) m_projectiles[i].alive = false;
}


void RenderingSystem::CreateLightBuffers(ID3D12Device* device) {
    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Alignment = 0;

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

    HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_lightConstantBuffer));

    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create light constant buffer");
    }

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
Texture2D<float4> AlbedoTex       : register(t0);
Texture2D<float4> WorldPosTex     : register(t1);
Texture2D<float4> NormalTex       : register(t2);
Texture2D<float4> PBRTex          : register(t3);
TextureCube       IrradianceMap   : register(t4);
TextureCube       PrefilteredMap  : register(t5);
Texture2D<float4> BRDF_LUT        : register(t6);

SamplerState LinearSampler : register(s0);     
SamplerState IBLSampler    : register(s1);   

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
    float4 cameraPos;
};

static const float PI = 3.14159265359;

float D_GGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float G_SchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness) {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float3 CookTorrance(float3 N, float3 V, float3 L, float3 radiance,
                    float3 albedo, float roughness, float metallic) {
    float3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V)) + 1e-4;
    float NdotH = saturate(dot(N, H));
    float HdotV = saturate(dot(H, V));

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float  D = D_GGX(NdotH, roughness);
    float  G = G_Smith(NdotV, NdotL, roughness);
    float3 F = F_Schlick(HdotV, F0);

    float3 numerator   = D * G * F;
    float  denominator = 4.0 * NdotV * NdotL + 1e-4;
    float3 specular    = numerator / denominator;

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    return (kD * albedo / PI + specular) * radiance * NdotL;
}

float3 IBL(float3 N, float3 V, float3 albedo, float roughness, float metallic) {
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 R  = reflect(-V, N);
    float  NdotV = saturate(dot(N, V)) + 1e-4;

    float3 irradiance = IrradianceMap.Sample(IBLSampler, N).rgb;

    float maxMip = 11.0;
    float mip = roughness * maxMip;
    float3 prefilteredColor = PrefilteredMap.SampleLevel(IBLSampler, R, mip).rgb;

    float2 brdf = BRDF_LUT.Sample(IBLSampler, float2(NdotV, roughness)).rg;

    float3 specular = prefilteredColor * (F0 * brdf.x + brdf.y);

    float3 F  = F_Schlick(NdotV, F0);
    float3 kD = (1.0 - F) * (1.0 - metallic);

    return kD * albedo * irradiance + specular;
}

float4 main(float4 position : SV_POSITION) : SV_TARGET {
    int3 texPos = int3(position.xy, 0);

    float4 albedoData = AlbedoTex.Load(texPos);
    float4 worldPos   = WorldPosTex.Load(texPos);
    float4 normalData = NormalTex.Load(texPos);
    float4 pbrData    = PBRTex.Load(texPos);

    float3 albedo    = albedoData.rgb;
    float3 N         = normalize(normalData.xyz * 2.0 - 1.0);
    float  roughness = clamp(pbrData.r, 0.04, 1.0);
    float  metallic  = saturate(pbrData.g);
    float  ao        = pbrData.b;

    float3 V = normalize(cameraPos.xyz - worldPos.xyz);

    float3 Lo = float3(0.0, 0.0, 0.0);

    for (uint i = 0; i < lightCount; i++) {
        uint type = (uint)lights[i].position_type.w;
        float3 L;
        float attenuation = 1.0;

        if (type == 1) {
            L = normalize(-lights[i].direction.xyz);
        }
        else {
            float3 lightVec = lights[i].position_type.xyz - worldPos.xyz;
            float dist = length(lightVec);
            L = lightVec / dist;
            attenuation = saturate(1.0 - dist / lights[i].range_spotAngle.x);
            attenuation *= attenuation;
        }

        float3 radiance = lights[i].color_intensity.rgb *
                          lights[i].color_intensity.a * attenuation;

        Lo += CookTorrance(N, V, L, radiance, albedo, roughness, metallic);
    }

    float3 ambient = IBL(N, V, albedo, roughness, metallic) * ao;
    float3 color = ambient + Lo;

    if (dot(worldPos.xyz, worldPos.xyz) < 0.0001) {
        float2 uv = position.xy / float2(1024.0, 768.0);  
        float3 skyTop    = float3(0.15, 0.20, 0.30);
        float3 skyBottom = float3(0.03, 0.03, 0.05);
        float3 bg = lerp(skyBottom, skyTop, 1.0 - uv.y);
        color = bg;
    }

    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    float3 mapped = saturate((color * (a * color + b)) /
                             (color * (c * color + d) + e));

    float3 gamma = pow(mapped, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));

    return float4(gamma, 1.0);
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

    D3D12_DESCRIPTOR_RANGE gbufferRange = {};
    gbufferRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    gbufferRange.NumDescriptors = 4;
    gbufferRange.BaseShaderRegister = 0;
    gbufferRange.RegisterSpace = 0;
    gbufferRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE iblRange = {};
    iblRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    iblRange.NumDescriptors = 3;
    iblRange.BaseShaderRegister = 4;
    iblRange.RegisterSpace = 0;
    iblRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[3] = {};

    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &gbufferRange;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].Descriptor.RegisterSpace = 0;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &iblRange;

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


    D3D12_STATIC_SAMPLER_DESC ibSampler = {};
    ibSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    ibSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ibSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ibSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ibSampler.MipLODBias = 0;
    ibSampler.MaxAnisotropy = 1;
    ibSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    ibSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    ibSampler.MinLOD = 0;
    ibSampler.MaxLOD = D3D12_FLOAT32_MAX;
    ibSampler.ShaderRegister = 1;  
    ibSampler.RegisterSpace = 0;
    ibSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[2] = { sampler, ibSampler };

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 3;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 2;
    rootSigDesc.pStaticSamplers = samplers;
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
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; 
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
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
    const RenderData* objects,
    UINT objectCount)
{
    if (!objects || objectCount == 0) {
        OutputDebugStringA("Invalid render data\n");
        return;
    }
    if (!m_lightCBData) {
        OutputDebugStringA("Light buffer not mapped\n");
        return;
    }

    memset(m_lightCBData, 0, sizeof(LightBufferGPU));
    LightBufferGPU* lightData = reinterpret_cast<LightBufferGPU*>(m_lightCBData);
    lightData->lightCount = std::min((UINT)m_lights.size(), 16u);
    for (UINT i = 0; i < lightData->lightCount; ++i) {
        const Light& light = m_lights[i];
        lightData->lights[i].position_type = light.GetAsFloat4();
        lightData->lights[i].direction = light.GetDirectionAsFloat4();
        lightData->lights[i].color_intensity = XMFLOAT4(light.color.x, light.color.y,
            light.color.z, light.intensity);
        lightData->lights[i].range_spotAngle = XMFLOAT4(light.range, light.spotAngle, 0, 0);
    }

    lightData->cameraPos = XMFLOAT4(objects[0].cameraPos.x,
        objects[0].cameraPos.y,
        objects[0].cameraPos.z,
        1.0f);

    D3D12_CPU_DESCRIPTOR_HANDLE gbufferRTVs[GBuffer::GB_COUNT];
    for (int i = 0; i < GBuffer::GB_COUNT; ++i)
        gbufferRTVs[i] = m_gbuffer->GetRTV((GBuffer::GBufferType)i);

    {
        ID3D12DescriptorHeap* modelHeaps[] = { objects[0].modelSrvHeap };
        cmdList->SetDescriptorHeaps(_countof(modelHeaps), modelHeaps);
    }

    cmdList->SetGraphicsRootSignature(geometryRootSig);
    cmdList->SetPipelineState(geometryPSO);
    cmdList->OMSetRenderTargets(GBuffer::GB_COUNT, gbufferRTVs, FALSE, &dsvHandle);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)m_width, (LONG)m_height };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sc);

    float albedoClear[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float dataClear[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float pbrClear[] = { 1.0f, 0.0f, 1.0f, 1.0f };

    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_ALBEDO], albedoClear, 0, nullptr);
    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_WORLD_POS], dataClear, 0, nullptr);
    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_NORMAL], dataClear, 0, nullptr);
    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_PBR], pbrClear, 0, nullptr);
    cmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (UINT objIdx = 0; objIdx < objectCount; ++objIdx) {
        const RenderData& rd = objects[objIdx];
        if (!rd.vertexBuffer || !rd.indexBuffer) continue;

        cmdList->SetGraphicsRootConstantBufferView(0, rd.cbvAddress);

        D3D12_VERTEX_BUFFER_VIEW vbv;
        vbv.BufferLocation = rd.vertexBuffer->GetGPUVirtualAddress();
        vbv.StrideInBytes = sizeof(Vertex);
        vbv.SizeInBytes = (UINT)rd.vertexBuffer->GetDesc().Width;

        D3D12_INDEX_BUFFER_VIEW ibv;
        ibv.BufferLocation = rd.indexBuffer->GetGPUVirtualAddress();
        ibv.SizeInBytes = (UINT)rd.indexBuffer->GetDesc().Width;
        ibv.Format = DXGI_FORMAT_R32_UINT;

        cmdList->IASetVertexBuffers(0, 1, &vbv);
        cmdList->IASetIndexBuffer(&ibv);

        if (rd.materials && rd.materialStartIndex && rd.materialIndexCount) {
            D3D12_GPU_DESCRIPTOR_HANDLE srvGpuStart =
                rd.modelSrvHeap->GetGPUDescriptorHandleForHeapStart();

            for (UINT i = 0; i < rd.numMaterials; i++) {
                if (rd.materialIndexCount[i] == 0) continue;

                if (rd.materials[i].textureIndex >= 0) {
                    D3D12_GPU_DESCRIPTOR_HANDLE texHandle;
                    texHandle.ptr = srvGpuStart.ptr +
                        (UINT64)rd.materials[i].textureIndex * rd.srvDescriptorSize;
                    cmdList->SetGraphicsRootDescriptorTable(1, texHandle);
                }
                else {
                    cmdList->SetGraphicsRootDescriptorTable(1, srvGpuStart);
                }

                cmdList->DrawIndexedInstanced(
                    rd.materialIndexCount[i], 1, rd.materialStartIndex[i], 0, 0);
            }
        }
        else {
            cmdList->DrawIndexedInstanced(rd.indexCount, 1, 0, 0, 0);
        }
    }

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

    cmdList->SetGraphicsRootSignature(m_lightingRootSig.Get());
    cmdList->SetPipelineState(m_lightingPSO.Get());
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    ID3D12DescriptorHeap* heaps[] = { m_gbufferSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    D3D12_GPU_DESCRIPTOR_HANDLE gbufferGpuHandle =
        m_gbufferSrvHeap->GetGPUDescriptorHandleForHeapStart();
    cmdList->SetGraphicsRootDescriptorTable(0, gbufferGpuHandle);

    cmdList->SetGraphicsRootConstantBufferView(
        1, m_lightConstantBuffer->GetGPUVirtualAddress());

    if (m_iblReady) {
        D3D12_GPU_DESCRIPTOR_HANDLE iblGpuHandle;
        iblGpuHandle.ptr = gbufferGpuHandle.ptr +
            (UINT64)GBuffer::GB_COUNT * m_srvDescriptorSize;
        cmdList->SetGraphicsRootDescriptorTable(2, iblGpuHandle);
    }

    D3D12_VIEWPORT fullscreenViewport = {
        0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_RECT fullscreenScissor = { 0, 0, (LONG)m_width, (LONG)m_height };
    cmdList->RSSetViewports(1, &fullscreenViewport);
    cmdList->RSSetScissorRects(1, &fullscreenScissor);

    cmdList->IASetVertexBuffers(0, 1, &m_fullscreenVBView);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmdList->DrawInstanced(4, 1, 0, 0);

    RenderLightGizmos(cmdList, rtvHandle, objects[0].viewProj);

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

void RenderingSystem::Update(float deltaTime) {
    for (UINT i = 0; i < kMaxProjectiles; ++i) {
        Projectile& p = m_projectiles[i];
        if (!p.alive) continue;

        p.position.x += p.velocity.x * deltaTime;
        p.position.y += p.velocity.y * deltaTime;
        p.position.z += p.velocity.z * deltaTime;
        p.age += deltaTime;

        if (p.age >= p.lifetime) {
            p.alive = false;
        }
    }
    RebuildLightsFromProjectiles();
}

void RenderingSystem::CreateLightVisPipeline(ID3D12Device* device) {
    static const char* vsCode = R"(
    cbuffer LightVisCB : register(b0) {
        float4x4 viewProj;
        float4 lightPos;
        float4 lightColor;
    };
    struct VSOut {
        float4 pos   : SV_POSITION;
        float2 local : TEXCOORD0;
        float4 color : COLOR;
    };
    VSOut main(uint vid : SV_VertexID) {
        float2 corners[4] = {
            float2(-1,-1), float2(-1, 1),
            float2( 1,-1), float2( 1, 1)
        };
        float2 c = corners[vid];

        float4 clip = mul(float4(lightPos.xyz, 1.0), viewProj);

        VSOut o;
        if (clip.w <= 0.001) {
            o.pos   = float4(0, 0, -2, 1);
            o.local = float2(0, 0);
            o.color = lightColor;
            return o;
        }

        float4 ndc = clip / clip.w;
        ndc.z = 0.5;

        float screenSize = lightPos.w * 1.0 / clip.w;
        screenSize = min(screenSize, 0.5);
        float2 offset = c * screenSize;

        o.pos   = float4(ndc.xy + offset, 0.5, 1.0);
        o.local = c;
        o.color = lightColor;
        return o;
    }
)";

    static const char* psCode = R"(
    struct PSIn {
        float4 pos   : SV_POSITION;
        float2 local : TEXCOORD0;
        float4 color : COLOR;
    };
    float4 main(PSIn i) : SV_TARGET {
        float r = length(i.local);
        if (r > 1.0) discard;

        float core = smoothstep(0.5, 0.0, r);
        float glow = pow(saturate(1.0 - r), 3.0);
        float alpha = saturate(core + glow * 0.8);

        return float4(saturate(i.color.rgb * 2.0) * i.color.a * alpha, alpha);
    }
)";

    ComPtr<ID3DBlob> vs, ps, err;
    HRESULT hr = D3DCompile(vsCode, strlen(vsCode), "LightVisVS",
        nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs, &err);
    if (FAILED(hr)) {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        throw std::runtime_error("LightVis VS compile failed");
    }

    hr = D3DCompile(psCode, strlen(psCode), "LightVisPS",
        nullptr, nullptr, "main", "ps_5_0", 0, 0, &ps, &err);
    if (FAILED(hr)) {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        throw std::runtime_error("LightVis PS compile failed");
    }

    D3D12_ROOT_PARAMETER rp = {};
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp.Descriptor.ShaderRegister = 0;
    rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters = &rp;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    if (FAILED(hr)) throw std::runtime_error("LightVis RS serialize failed");
    device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&m_lightVisRootSig));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = m_lightVisRootSig.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.InputLayout = { nullptr, 0 };  // процедурные вершины
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pso.SampleDesc.Count = 1;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.SampleMask = UINT_MAX;

    auto& rt = pso.BlendState.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_ONE;
    rt.DestBlend = D3D12_BLEND_ONE;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ONE;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_lightVisPSO));
    if (FAILED(hr)) throw std::runtime_error("LightVis PSO create failed");

    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = sizeof(LightVisConstants);
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_lightVisCB));

    D3D12_RANGE rr = { 0, 0 };
    m_lightVisCB->Map(0, &rr, &m_lightVisCBData);
}

void RenderingSystem::RenderLightGizmos(ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
    const XMFLOAT4X4& viewProj) {
    bool any = false;
    for (UINT i = 0; i < kMaxProjectiles; ++i) {
        if (m_projectiles[i].alive) { any = true; break; }
    }
    if (!any) return;

    cmdList->SetGraphicsRootSignature(m_lightVisRootSig.Get());
    cmdList->SetPipelineState(m_lightVisPSO.Get());

    D3D12_VIEWPORT vp = { 0, 0, (float)m_width, (float)m_height, 0, 1 };
    D3D12_RECT sc = { 0, 0, (LONG)m_width, (LONG)m_height };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sc);
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    for (UINT i = 0; i < kMaxProjectiles; ++i) {
        const Projectile& p = m_projectiles[i];
        if (!p.alive) continue;

        float t = p.age / p.lifetime;
        float fadeOut = 1.0f - t * t;
        if (fadeOut < 0.0f) fadeOut = 0.0f;
        float fadeIn = (p.age < 0.2f) ? (p.age / 0.2f) : 1.0f;
        float fade = fadeIn * fadeOut;

        LightVisConstants c = {};
        XMStoreFloat4x4(&c.viewProj, XMMatrixTranspose(XMLoadFloat4x4(&viewProj)));
        c.lightPos = XMFLOAT4(p.position.x, p.position.y, p.position.z, p.radius);
        c.lightColor = XMFLOAT4(p.color.x, p.color.y, p.color.z,
            p.intensity * fade * 0.5f);

        memcpy(m_lightVisCBData, &c, sizeof(c));
        cmdList->SetGraphicsRootConstantBufferView(
            0, m_lightVisCB->GetGPUVirtualAddress());
        cmdList->DrawInstanced(4, 1, 0, 0);
    }
}

bool RenderingSystem::SpawnProjectile(const XMFLOAT3& origin,
    const XMFLOAT3& dir,
    const XMFLOAT4& color,
    float speed,
    float radius,
    float intensity,
    float range,
    float lifetime) {
    for (UINT i = 0; i < kMaxProjectiles; ++i) {
        Projectile& p = m_projectiles[i];
        if (p.alive) continue;

        p.position = origin;
        XMVECTOR d = XMVector3Normalize(XMLoadFloat3(&dir));
        XMStoreFloat3(&p.velocity, d * speed);
        p.color = color;
        p.radius = radius;
        p.intensity = intensity;
        p.range = range;
        p.lifetime = lifetime;
        p.age = 0.0f;
        p.alive = true;
        return true;
    }
    return false;
}

void RenderingSystem::RebuildLightsFromProjectiles() {
    m_lights.clear();
    m_originalIntensities.clear();

    for (const Light& l : m_staticLights) {
        m_lights.push_back(l);
        m_originalIntensities.push_back(l.intensity);
    }

    for (UINT i = 0; i < kMaxProjectiles; ++i) {
        const Projectile& p = m_projectiles[i];
        if (!p.alive) continue;

        float t = p.age / p.lifetime;
        float fadeOut = 1.0f - t * t;
        if (fadeOut < 0.0f) fadeOut = 0.0f;
        float fadeIn = (p.age < 0.2f) ? (p.age / 0.2f) : 1.0f;
        float fade = fadeIn * fadeOut;

        Light light;
        light.type = LightType::Point;
        light.position = p.position;
        light.color = p.color;
        light.intensity = p.intensity * fade * m_globalIntensity;
        light.range = p.range;
        m_lights.push_back(light);
        m_originalIntensities.push_back(light.intensity);
    }

    if (m_lights.size() > 16) {
        m_lights.resize(16);
        m_originalIntensities.resize(16);
    }
}