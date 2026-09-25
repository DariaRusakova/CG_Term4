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

struct SceneObject {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    std::vector<Material> materials;
    std::vector<uint32_t> materialStartIndex;
    std::vector<uint32_t> materialIndexCount;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;

    Microsoft::WRL::ComPtr<ID3D12Resource> vbUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> ibUpload;

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW  ibv{};
    UINT indexCount = 0;

    DirectX::XMFLOAT4X4 world = { /* ... */ };
};

struct MaterialPaths {
    std::string albedo;
    std::string normal;
    std::string roughness;
    std::string metallic;
    std::string ao;
};


class D3D12App {
public:
    D3D12App();
    ~D3D12App();

    bool Initialize(HWND hwnd);
    void RenderFrame();
    void Shutdown();
    UINT GetSRVDescriptorSize() const { return m_srvDescriptorSize; }

    void OnMouseWheel(int delta);
    void OnMouseDown(int x, int y);
    void OnMouseUp();
    void OnMouseMove(int x, int y);
    void OnKeyDown(WPARAM wParam);
    void ResetCamera();
    float m_shootCooldown = 0.0f;

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
    void CreateSRVHeap();
    void CreateConstantBuffers();
    void UpdateConstantBuffer(uint32_t bufferIndex);
    //void DebugPrintMaterialMapping();

    void WaitForGpu();
    void WaitForPreviousFrame();
    void SignalFrame();

    void CreateGeometryPassRootSignature();
    void CreateGeometryPassPipelineState();

    bool LoadSceneObject(const std::string& objPath,
        const std::string& basePath,
        const DirectX::XMFLOAT4X4& world,
        SceneObject& outObject,
        const MaterialPaths& paths);

    void CreateMeshBuffers(SceneObject& obj);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_geometryRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_geometryPSO;

    std::unique_ptr<RenderingSystem> m_renderingSystem;

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
    HANDLE   m_fenceEvent = nullptr;
    uint64_t m_fenceValue = 0;
    uint32_t m_frameIndex = 0;

    uint32_t m_rtvDescriptorSize = 0;
    uint32_t m_srvDescriptorSize = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;

    static constexpr UINT kMaxObjects = 8;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer[kFrameCount][kMaxObjects];
    void* m_cbvDataBegin[kFrameCount][kMaxObjects] = {};

    std::vector<Texture> m_textures;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    UINT m_srvHeapCapacity = 0;
    UINT m_srvHeapUsed = 0;

    std::vector<SceneObject> m_objects;

    Camera m_camera;

    bool  m_mousePressed = false;
    POINT m_lastMousePos = {};

    float m_lightIntensity = 1.0f;
    float m_lightIntensityStep = 0.1f;
    float m_textureAnimTime = 0.0f;

    D3D12_VIEWPORT m_viewport;
    D3D12_RECT     m_scissorRect;
};