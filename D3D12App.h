#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include "Model/Vertex.h"
#include "Model/Material.h"
#include "Texture/TextureLoader.h"
#include "Camera/Camera.h"
#include "Rendering/RenderingSystem.h"
#include "Shaders/DeferredGeometryPass.h"
#include "Shaders/DeferredLightPass.h"

struct alignas(256) SceneConstantBuffer {
    DirectX::XMFLOAT4X4 worldViewProj;
    DirectX::XMFLOAT4X4 world;
    DirectX::XMFLOAT4 lightPos;
    DirectX::XMFLOAT4 lightColor;
    DirectX::XMFLOAT4 cameraPos;
    DirectX::XMFLOAT4 materialAmbient;
    DirectX::XMFLOAT4 materialDiffuse;
    DirectX::XMFLOAT4 materialSpecular;
    float materialShininess;
    DirectX::XMFLOAT2 textureScale;
    DirectX::XMFLOAT2 textureOffset;
    float padding[2];
};
static_assert(sizeof(SceneConstantBuffer) == 256, "CB size mismatch");

class D3D12App {
public:
    D3D12App();
    ~D3D12App();

    bool Initialize(HWND hwnd);
    void RenderFrame();
    void Shutdown();
    UINT GetSRVDescriptorSize() const { return m_srvDescriptorSize; }

    // Input handling
    void OnMouseWheel(int delta);
    void OnMouseDown(int x, int y);
    void OnMouseUp();
    void OnMouseMove(int x, int y);
    void OnKeyDown(WPARAM wParam);
    void ResetCamera();

private:
    static constexpr uint32_t kFrameCount = 2;
    static constexpr uint32_t kWidth = 1024;
    static constexpr uint32_t kHeight = 768;

    void EnableDebugLayer();
    void CreateDevice();
    void CreateCommandObjects();
    void CreateSwapChain(HWND hwnd);
    void CreateDescriptorHeaps();
    void CreateDepthStencil();
    //void CreateRootSignature();
    //void CreatePipelineState();
    void CreateSRVHeap();
    void CreateBuffers();
    void CreateConstantBuffers();
    void UpdateConstantBuffer(uint32_t bufferIndex);
    void CreateBuffersFromData();
    void DebugPrintMaterialMapping();

    void WaitForGpu();
    void WaitForPreviousFrame();
    void SignalFrame();

    void CreateGeometryPassRootSignature();
    void CreateGeometryPassPipelineState();
    void CreateLightingPassResources();

    // Новая PSO и Root Signature для геометрического прохода
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_geometryRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_geometryPSO;

    // GBuffer дескрипторы
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_gbufferRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_gbufferSrvHeap;

    // Система рендеринга
    std::unique_ptr<RenderingSystem> m_renderingSystem;

    // D3D12 objects
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTargets[kFrameCount];
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_depthStencil;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocators[kFrameCount];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    uint64_t m_fenceValue = 0;
    uint32_t m_frameIndex = 0;

    // Descriptor sizes
    uint32_t m_rtvDescriptorSize = 0;      // Добавлено
    uint32_t m_srvDescriptorSize = 0;      // Добавлено

    // Pipeline
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;

    // Buffers
    Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer[kFrameCount];
    void* m_cbvDataBegin[kFrameCount];

    // Textures
    std::vector<Texture> m_textures;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;

    // Scene data
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<Material> m_materials;
    std::vector<uint32_t> m_materialStartIndex;
    std::vector<uint32_t> m_materialIndexCount;
    uint32_t m_indexCount = 0;

    // Camera
    Camera m_camera;

    // Input state
    bool m_mousePressed = false;
    POINT m_lastMousePos = {};

    // Animation
    float m_textureAnimTime = 0.0f;
    float m_rotationAngle = 0.0f;

    D3D12_VIEWPORT m_viewport;
    D3D12_RECT m_scissorRect;
};