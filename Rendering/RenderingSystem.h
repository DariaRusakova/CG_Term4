#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <directxmath.h>
#include "GBuffer.h"
#include "Light.h"
#include "../Model/Material.h"
#include "../Texture/TextureLoader.h"
#include "Shaders/DeferredLightPass.h"

class RenderingSystem {
public:
    struct RenderData {
        ID3D12Resource* vertexBuffer;
        ID3D12Resource* indexBuffer;
        UINT indexCount;
        D3D12_GPU_VIRTUAL_ADDRESS cbvAddress;
        ID3D12DescriptorHeap* modelSrvHeap;
        UINT srvDescriptorSize;
        const uint32_t* materialStartIndex;
        const uint32_t* materialIndexCount;
        UINT numMaterials;
        const Material* materials;
    };

    static constexpr UINT ROOF_TEXTURE_COUNT = 3;

    RenderingSystem();
    ~RenderingSystem();

    void Initialize(ID3D12Device* device, UINT width, UINT height);
    void Resize(UINT width, UINT height);

    // Загрузка 3 roof-текстур (заменителей тени) — по одной на каскадную зону
    void LoadRoofTextures(ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const std::string paths[ROOF_TEXTURE_COUNT]);

    void SetShadowResources(ID3D12Resource* shadowCB, D3D12_GPU_DESCRIPTOR_HANDLE shadowSRV);

    void Render(ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* depthStencil,
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
        ID3D12RootSignature* geometryRootSig,
        ID3D12PipelineState* geometryPSO,
        const RenderData* renderData);

    void AddLight(const Light& light);
    void ClearLights();
    ID3D12DescriptorHeap* GetCombinedSrvHeap() const { return m_combinedSrvHeap.Get(); }

    void SetGlobalIntensity(float intensity) {
        if (!m_lights.empty()) {
            m_globalIntensity = intensity;
            for (auto& light : m_lights) {
                light.intensity = m_originalIntensities[&light - m_lights.data()] * intensity;
            }
        }
    }

    void SetDebugMode(UINT mode) { m_debugMode = mode; }
    UINT GetDebugMode() const { return m_debugMode; }

    void RenderShadowMapDebug(ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle);

private:
    void CreateLightBuffers(ID3D12Device* device);
    void CreateFullscreenQuad(ID3D12Device* device);
    void CreateLightingPassPipeline(ID3D12Device* device);
    void CreateCombinedSrvHeap(ID3D12Device* device);

    void* m_lightCBData = nullptr;
    float m_globalIntensity = 1.0f;
    std::vector<float> m_originalIntensities;
    UINT m_debugMode = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_lightingRootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_lightingPSO;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_fullscreenVB;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_lightConstantBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_fullscreenVBView;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_combinedSrvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_gbufferRtvHeap;
    std::unique_ptr<GBuffer> m_gbuffer;
    std::vector<Light> m_lights;

    // 3 roof-текстуры (заменители тени)
    Texture m_roofTextures[ROOF_TEXTURE_COUNT];
    bool m_roofTexturesLoaded = false;

    ID3D12Resource* m_externalShadowCB = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE m_externalShadowSRV;

    UINT m_width = 0;
    UINT m_height = 0;
    UINT m_srvDescriptorSize = 0;

    struct LightDataGPU {
        DirectX::XMFLOAT4 position_type;
        DirectX::XMFLOAT4 direction;
        DirectX::XMFLOAT4 color_intensity;
        DirectX::XMFLOAT4 range_spotAngle;
    };

    struct LightBufferGPU {
        LightDataGPU lights[16];
        UINT lightCount;
        UINT debugMode;
        float padding[2];
    };
};