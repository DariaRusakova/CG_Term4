#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <vector>
#include <stdexcept>
#include "GBuffer.h"
#include "Light.h"
#include "../Model/Material.h"
#include "../Model/Vertex.h"


struct InstanceData {
    DirectX::XMFLOAT4X4 worldMatrix;
    DirectX::XMFLOAT4 color;
};


class RenderingSystem {
public:
    struct RenderData {
        ID3D12Resource* vertexBuffer = nullptr;
        ID3D12Resource* indexBuffer = nullptr;
        UINT indexCount = 0;
        D3D12_GPU_VIRTUAL_ADDRESS cbvAddress = 0;
        ID3D12DescriptorHeap* modelSrvHeap = nullptr;
        UINT srvDescriptorSize = 0;
        const uint32_t* materialStartIndex = nullptr;
        const uint32_t* materialIndexCount = nullptr;
        UINT numMaterials = 0;
        const Material* materials = nullptr;
        bool useTessellation = false;
        ID3D12PipelineState* tessellationPSO = nullptr;
        ID3D12RootSignature* tessellationRootSig = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS tessCBVAddress = 0;
        // Инстансинг
        bool useInstancing = false;
        ID3D12Resource* instanceBuffer = nullptr;
        uint32_t instanceCount = 0;
        ID3D12PipelineState* instancedPSO = nullptr;           // PSO для инстансинга
        ID3D12RootSignature* instancedRootSig = nullptr;       // Root Signature для инстансинга
    };

    RenderingSystem();
    ~RenderingSystem();

    void Initialize(ID3D12Device* device, UINT width, UINT height);

    // Убираем Resize, так как он не реализован
    // void Resize(ID3D12Device* device, UINT width, UINT height);

    void Render(ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* depthStencil,
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
        ID3D12RootSignature* geometryRootSig,
        ID3D12PipelineState* geometryPSO,
        const RenderData* renderData);

    void AddLight(const Light& light);
    void ClearLights();

private:
    std::unique_ptr<GBuffer> m_gbuffer;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_gbufferRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_gbufferSrvHeap;
    std::vector<Light> m_lights;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_lightConstantBuffer;
    void* m_lightCBData;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_fullscreenVB;
    D3D12_VERTEX_BUFFER_VIEW m_fullscreenVBView;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_lightingPSO;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_lightingRootSig;

    UINT m_width;
    UINT m_height;

    void CreateLightBuffers(ID3D12Device* device);
    void CreateFullscreenQuad(ID3D12Device* device);
    void CreateLightingPassPipeline(ID3D12Device* device);
};