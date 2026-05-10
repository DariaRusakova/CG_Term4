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

// Структуры для BVH (AABB-tree)

// Выровненный по осям ограничивающий параллелепипед
struct AABB {
    DirectX::XMFLOAT3 min;
    DirectX::XMFLOAT3 max;

    AABB() : min(FLT_MAX, FLT_MAX, FLT_MAX), max(-FLT_MAX, -FLT_MAX, -FLT_MAX) {}

    // Расширить AABB точкой
    void Extend(const DirectX::XMFLOAT3& point) {
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        min.z = std::min(min.z, point.z);
        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
        max.z = std::max(max.z, point.z);
    }

    // Расширить другим AABB
    void Extend(const AABB& other) {
        min.x = std::min(min.x, other.min.x);
        min.y = std::min(min.y, other.min.y);
        min.z = std::min(min.z, other.min.z);
        max.x = std::max(max.x, other.max.x);
        max.y = std::max(max.y, other.max.y);
        max.z = std::max(max.z, other.max.z);
    }

    // Центр AABB
    DirectX::XMFLOAT3 Center() const {
        return DirectX::XMFLOAT3(
            (min.x + max.x) * 0.5f,
            (min.y + max.y) * 0.5f,
            (min.z + max.z) * 0.5f
        );
    }

    // Полуразмер (extents)
    DirectX::XMFLOAT3 HalfSize() const {
        return DirectX::XMFLOAT3(
            (max.x - min.x) * 0.5f,
            (max.y - min.y) * 0.5f,
            (max.z - min.z) * 0.5f
        );
    }
};

// Узел BVH
struct BVHNode {
    AABB bounds;               // Ограничивающий объём узла
    BVHNode* left = nullptr;   // Левый потомок
    BVHNode* right = nullptr;  // Правый потомок
    bool isLeaf = false;       // Лист?

    // Данные листа
    std::vector<uint32_t> instanceIndices;  // Индексы инстансов в листе
    uint32_t depth = 0;                     // Глубина узла (для отладки)
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

    // Инстансинг
    std::vector<InstanceData> m_instances;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_instanceBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_instanceBufferView = {};
    uint32_t m_instanceCount = 0;
    bool m_useInstancing = false;

    // Frustum culling
    bool m_useFrustumCulling = true;
    float m_cullDistance = 250.0f;  // Дистанция отсечения

    // Плоскости фрустума (6 плоскостей: left, right, top, bottom, near, far)
    DirectX::XMFLOAT4 m_frustumPlanes[6];

    // Кэш видимых инстансов
    std::vector<InstanceData> m_visibleInstances;
    std::vector<bool> m_instanceVisible;
    bool IsInFrustum(const InstanceData& instance);
    // Отдельный PSO для инстансинга с тесселяцией
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_instancedTessPSO;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_instancedTessRootSig;


    // BVH дерево
    BVHNode* m_bvhRoot = nullptr;
    bool m_useBVH = true;

    // Методы BVH
    void BuildBVH();
    BVHNode* BuildBVHRecursive(std::vector<uint32_t>& indices, uint32_t depth, uint32_t maxDepth);
    AABB ComputeInstanceAABB(const InstanceData& instance);
    void QueryBVH(BVHNode* node, std::vector<uint32_t>& visibleIndices);
    bool IsAABBInFrustum(const AABB& aabb);
    void DestroyBVH();



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

    void GenerateInstances();
    void CreateInstanceBuffer();
    void CreateInstancedTessellationPipeline();

    void ComputeFrustumPlanes();                                    // Вычисление плоскостей фрустума
    bool IsInstanceVisible(const InstanceData& instance);          // Проверка видимости
    void UpdateVisibleInstances();                                  // Обновление списка видимых

    // ===== Методы синхронизации =====
    void WaitForPreviousFrame();
    void SignalFrame();
    void WaitForGpu();

    // ===== Вспомогательные методы =====
    static UINT AlignSizeForCB(UINT size) {
        return (size + 255) & ~255;
    }
};