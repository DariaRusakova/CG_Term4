#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <directxmath.h>
#include "GBuffer.h"
#include "Light.h"
#include "../Model/Material.h"  // Добавляем включение Material
#include "Shaders/DeferredLightPass.h"

class RenderingSystem {
public:
    // Структура для передачи данных рендеринга
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
        const Material* materials;  // Используем правильный тип
    };

    RenderingSystem();
    ~RenderingSystem();

    void Initialize(ID3D12Device* device, UINT width, UINT height);
    void Resize(UINT width, UINT height);

    // Обновленный метод Render
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
    void CreateLightBuffers(ID3D12Device* device);
    void CreateFullscreenQuad(ID3D12Device* device);
    void CreateLightingPassPipeline(ID3D12Device* device);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_lightingRootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_lightingPSO;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_fullscreenVB;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_lightConstantBuffer;

    D3D12_VERTEX_BUFFER_VIEW m_fullscreenVBView;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_gbufferSrvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_gbufferRtvHeap;

    std::unique_ptr<GBuffer> m_gbuffer;
    std::vector<Light> m_lights;

    void* m_lightCBData = nullptr;
    UINT m_width = 0;
    UINT m_height = 0;

    struct LightBuffer {
        DirectX::XMFLOAT4 position_type[16];
        DirectX::XMFLOAT4 direction[16];
        DirectX::XMFLOAT4 color_intensity[16];
        DirectX::XMFLOAT4 range_spotAngle[16];
        UINT lightCount;
        float padding[3];
    };
};