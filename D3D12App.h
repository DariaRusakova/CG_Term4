#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <windows.h>


#include "Camera/Camera.h"
#include "Model/Material.h"
#include "Model/Vertex.h"
#include "Texture/TextureLoader.h"
#include "Rendering/RenderingSystem.h"

// Структура для сцены (константный буфер b0)
struct SceneConstantBuffer {
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
};

// Вспомогательная функция
inline void ThrowIfFailed(HRESULT hr, const char* errorMsg = "") {
    if (FAILED(hr)) {
        char buffer[512];
        sprintf_s(buffer, "HRESULT 0x%08X: %s\n", hr, errorMsg);
        OutputDebugStringA(buffer);
        throw std::runtime_error(buffer);
    }
}

class D3D12App {
public:
    D3D12App();
    ~D3D12App();

    bool Initialize(HWND hwnd);
    void RenderFrame();
    void Shutdown();

    // Управление камерой и ввод
    void OnMouseWheel(int delta);
    void OnMouseDown(int x, int y);
    void OnMouseUp();
    void OnMouseMove(int x, int y);
    void OnKeyDown(WPARAM wParam);
    void ResetCamera();

private:
    static const uint32_t kWidth = 1920;
    static const uint32_t kHeight = 1080;
    static const uint32_t kFrameCount = 2;

    // DirectX 12 объекты
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocators[kFrameCount];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;

    // Render Targets и Depth Stencil
    Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTargets[kFrameCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_depthStencil;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;

    UINT m_rtvDescriptorSize = 0;
    UINT m_srvDescriptorSize = 0;

    // Геометрические буферы
    Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView = {};
    UINT m_indexCount = 0;

    // Константные буферы
    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer[kFrameCount];
    void* m_cbvDataBegin[kFrameCount];

    // Тесселяция
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_tessellationConstantBuffers;
    std::vector<void*> m_tessCBData;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_tessellationPSO;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_tessellationRootSig;
    Texture m_displacementTexture;
    int m_displacementTextureIndex = -1;
    bool m_useTessellation = true;
    bool m_useNormalMap = true;  // Использовать карту нормалей

    // Структура для константного буфера тесселяции
    struct TessellationConstantBuffer {
        DirectX::XMFLOAT4X4 viewMatrix;      // 64 байта (4x4 float32)
        DirectX::XMFLOAT4X4 projMatrix;      // 64 байта
        DirectX::XMFLOAT4 cameraPos;         // 16 байт
        float minTessDist;                   // 4 байта
        float maxTessDist;                   // 4 байта
        float minTessLevel;                  // 4 байта
        float maxTessLevel;                  // 4 байта
        int showVisualization;               // 4 байта
        float padding;                       // 4 байта (для выравнивания 16 байт)
        int useNormalMap;                    // 4 байта
        float normalStrength;                // 4 байта
        float padding2[2];                  // 8 байт (дополнительное выравнивание)
        // ОБЩИЙ РАЗМЕР: должен быть кратен 256 байтам
    };

    // PSO и Root Signature для геометрии
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_geometryPSO;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_geometryRootSig;

    // Данные модели
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<Material> m_materials;
    std::vector<uint32_t> m_materialStartIndex;
    std::vector<uint32_t> m_materialIndexCount;

    // Текстуры
    std::vector<Texture> m_textures;


    float m_tessMultiplier = 1.0f;
    bool m_showTessVisualization = false;  // Визуализация уровней тесселяции

    float m_tessMinDist = 2.0f;
    float m_tessMaxDist = 30.0f;
    float m_tessMinLevel = 1.0f;
    float m_tessMaxLevel = 64.0f;

    // Камера
    Camera m_camera;
    bool m_mousePressed = false;
    POINT m_lastMousePos = { 0, 0 };

    // Вьюпорт и прямоугольник отсечения
    D3D12_VIEWPORT m_viewport = {};
    D3D12_RECT m_scissorRect = {};

    // Система рендеринга
    std::unique_ptr<RenderingSystem> m_renderingSystem;

    // Индекс текущего кадра и синхронизация
    uint32_t m_frameIndex = 0;
    uint64_t m_fenceValue = 1;

    // Анимация
    float m_rotationAngle = 0.0f;
    float m_textureAnimTime = 0.0f;

    // ===== Методы инициализации =====
    void EnableDebugLayer();
    void CreateDevice();
    void CreateCommandObjects();
    void CreateSwapChain(HWND hwnd);
    void CreateDescriptorHeaps();
    void CreateDepthStencil();
    void CreateSRVHeap();
    void CreateBuffers();                          // Старый метод (устаревший)
    void CreateBuffersFromData();                  // Новый метод (используется в Initialize)
    void CreateConstantBuffers();
    void CreateTessellationPipeline();             // Новый метод для тесселяции
    void LoadDisplacementMap();                    // Новый метод для загрузки displacement

    // ===== Методы рендеринга =====
    void UpdateConstantBuffer(uint32_t bufferIndex);
    void UpdateTessellationConstantBuffer(uint32_t bufferIndex);  // Новый метод
    void CreateGeometryPassRootSignature();
    void CreateGeometryPassPipelineState();
    void DebugPrintMaterialMapping();

    // ===== Методы синхронизации =====
    void WaitForPreviousFrame();
    void SignalFrame();
    void WaitForGpu();

    // ===== Вспомогательные методы =====
    static UINT AlignSizeForCB(UINT size) {
        return (size + 255) & ~255;
    }
};