#include "D3D12App.h"
#include "Model/ModelLoader.h"
#include "Texture/TextureLoader.h"
#include "Shaders/Shaders.h"
#include "Camera/Camera.h"
#include "libs/d3dx12.h"
#include <d3dcompiler.h>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <windows.h>
#include <Shaders/TessellationShaders.h>


using namespace DirectX;
using Microsoft::WRL::ComPtr;



D3D12App::D3D12App()
    : m_fenceEvent(nullptr)
    , m_rtvDescriptorSize(0)
    , m_srvDescriptorSize(0)
    , m_indexCount(0)
    , m_displacementTextureIndex(-1)
    , m_useTessellation(true)
    , m_useNormalMap(false)
    , m_showTessVisualization(false)
    , m_mousePressed(false)
    , m_frameIndex(0)
    , m_fenceValue(1)
    , m_rotationAngle(0.0f)
    , m_textureAnimTime(0.0f)
    , m_lastMousePos{ 0, 0 } {

    OutputDebugStringA("D3D12App constructor started\n");

    // Инициализация массивов указателей
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        m_cbvDataBegin[i] = nullptr;
    }

    // Инициализация структур
    ZeroMemory(&m_vertexBufferView, sizeof(m_vertexBufferView));
    ZeroMemory(&m_indexBufferView, sizeof(m_indexBufferView));
    ZeroMemory(&m_viewport, sizeof(m_viewport));
    ZeroMemory(&m_scissorRect, sizeof(m_scissorRect));

    OutputDebugStringA("D3D12App constructor complete\n");
}

D3D12App::~D3D12App() {
    OutputDebugStringA("Destructor called\n");
    Shutdown();
}

void D3D12App::Shutdown() {
    OutputDebugStringA("Shutdown started...\n");

    // Сначала ждём завершения GPU
    try {
        WaitForGpu();
    }
    catch (...) {
        OutputDebugStringA("WaitForGpu failed during shutdown\n");
    }

    // Закрываем событие синхронизации
    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }

    // Очистка обычных константных буферов
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        if (m_cbvDataBegin[i] && m_constantBuffer[i]) {
            try {
                m_constantBuffer[i]->Unmap(0, nullptr);
            }
            catch (...) {
                OutputDebugStringA("Failed to unmap constant buffer\n");
            }
            m_cbvDataBegin[i] = nullptr;
        }
    }

    // Очистка константных буферов тесселяции
    for (size_t i = 0; i < m_tessCBData.size(); ++i) {
        if (m_tessCBData[i] && i < m_tessellationConstantBuffers.size() && m_tessellationConstantBuffers[i]) {
            try {
                m_tessellationConstantBuffers[i]->Unmap(0, nullptr);
            }
            catch (...) {
                OutputDebugStringA("Failed to unmap tessellation constant buffer\n");
            }
            m_tessCBData[i] = nullptr;
        }
    }

    // Очищаем векторы
    m_tessellationConstantBuffers.clear();
    m_tessCBData.clear();

    // Очищаем систему рендеринга до освобождения устройств
    m_renderingSystem.reset();

    OutputDebugStringA("Shutdown complete\n");
}

// ==============================================
// Инициализация
// ==============================================

void D3D12App::EnableDebugLayer() {
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        OutputDebugStringA("Debug layer enabled\n");
    }
#endif
}

void D3D12App::CreateDevice() {
    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)))) {
            OutputDebugStringA("Device created\n");
            return;
        }
    }
    throw std::runtime_error("No suitable GPU found");
}

void D3D12App::CreateCommandObjects() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)), "Create command queue");

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        ThrowIfFailed(m_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_commandAllocators[i])), "Create allocator");
    }

    ThrowIfFailed(m_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocators[0].Get(), nullptr,
        IID_PPV_ARGS(&m_commandList)), "Create command list");

    m_commandList->Close();

    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "Create fence");
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void D3D12App::CreateSwapChain(HWND hwnd) {
    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory");

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = kFrameCount;
    swapChainDesc.Width = kWidth;
    swapChainDesc.Height = kHeight;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(), hwnd, &swapChainDesc,
        nullptr, nullptr, &swapChain), "CreateSwapChainForHwnd");

    ThrowIfFailed(swapChain.As(&m_swapChain), "QueryInterface swapchain");
}

void D3D12App::CreateDescriptorHeaps() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = kFrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)), "Create RTV heap");

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])), "Get swapchain buffer");
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_rtvDescriptorSize;
    }
}

void D3D12App::CreateDepthStencil() {
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = kWidth;
    depthDesc.Height = kHeight;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
        IID_PPV_ARGS(&m_depthStencil)), "Create depth stencil");

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)), "Create DSV heap");

    m_device->CreateDepthStencilView(m_depthStencil.Get(), nullptr,
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void D3D12App::CreateSRVHeap() {
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = (int)m_textures.size();
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)));
    m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Создаём SRV для каждой текстуры
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();

    for (size_t i = 0; i < m_textures.size(); i++) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = m_textures[i].format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        m_device->CreateShaderResourceView(m_textures[i].resource.Get(), &srvDesc, srvHandle);
        srvHandle.ptr += m_srvDescriptorSize;
    }
}

void D3D12App::CreateBuffers() {
    // Загрузка модели уже выполнена в Initialize
    // Здесь создаём буферы из загруженных данных

    ModelData model = ModelLoader::LoadOBJ("assets/sponza.obj", "assets");

    // Создаём вершинный буфер
    const UINT vertexBufferSize = sizeof(Vertex) * (UINT)model.vertices.size();

    ComPtr<ID3D12Resource> vertexUploadBuffer;
    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = vertexBufferSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&vertexUploadBuffer)), "Create vertex upload buffer");

    void* data;
    ThrowIfFailed(vertexUploadBuffer->Map(0, nullptr, &data), "Map vertex upload buffer");
    memcpy(data, model.vertices.data(), vertexBufferSize);
    vertexUploadBuffer->Unmap(0, nullptr);

    D3D12_HEAP_PROPERTIES defaultHeapProps = {};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_vertexBuffer)), "Create vertex buffer");

    // Копирование через командный лист
    ThrowIfFailed(m_commandAllocators[0]->Reset(), "Reset allocator for vertex copy");
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[0].Get(), nullptr), "Reset list for vertex copy");

    m_commandList->CopyResource(m_vertexBuffer.Get(), vertexUploadBuffer.Get());

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_vertexBuffer.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(m_commandList->Close(), "Close vertex copy list");

    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);
    WaitForGpu();

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);
    m_vertexBufferView.SizeInBytes = vertexBufferSize;

    // Создаём индексный буфер
    const UINT indexBufferSize = sizeof(uint32_t) * (UINT)model.indices.size();
    m_indexCount = (UINT)model.indices.size();

    bufferDesc.Width = indexBufferSize;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&vertexUploadBuffer)), "Create index upload buffer");

    ThrowIfFailed(vertexUploadBuffer->Map(0, nullptr, &data), "Map index upload buffer");
    memcpy(data, model.indices.data(), indexBufferSize);
    vertexUploadBuffer->Unmap(0, nullptr);

    ThrowIfFailed(m_device->CreateCommittedResource(
        &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_indexBuffer)), "Create index buffer");

    ThrowIfFailed(m_commandAllocators[0]->Reset(), "Reset allocator for index copy");
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[0].Get(), nullptr), "Reset list for index copy");

    m_commandList->CopyResource(m_indexBuffer.Get(), vertexUploadBuffer.Get());

    barrier.Transition.pResource = m_indexBuffer.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    m_commandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(m_commandList->Close(), "Close index copy list");

    lists[0] = m_commandList.Get();
    m_commandQueue->ExecuteCommandLists(1, lists);
    WaitForGpu();

    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexBufferView.SizeInBytes = indexBufferSize;

    // Сохраняем данные материалов
    m_materials = model.materials;
    m_materialStartIndex = model.materialStartIndex;
    m_materialIndexCount = model.materialIndexCount;

    OutputDebugStringA("Buffers created successfully\n");
}

void D3D12App::CreateConstantBuffers() {
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = sizeof(SceneConstantBuffer);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ThrowIfFailed(m_device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_constantBuffer[i])), "Create constant buffer");

        D3D12_RANGE readRange = { 0, 0 };
        ThrowIfFailed(m_constantBuffer[i]->Map(0, &readRange, &m_cbvDataBegin[i]), "Map constant buffer");
    }
}

bool D3D12App::Initialize(HWND hwnd) {
    try {
        EnableDebugLayer();
        CreateDevice();
        CreateCommandObjects();
        CreateSwapChain(hwnd);
        CreateDescriptorHeaps();
        CreateDepthStencil();

        CreateGeometryPassRootSignature();
        CreateGeometryPassPipelineState();

        // Инициализируем систему рендеринга
        m_renderingSystem = std::make_unique<RenderingSystem>();
        m_renderingSystem->Initialize(m_device.Get(), kWidth, kHeight);

        // Загружаем модель
        ModelData model = ModelLoader::LoadOBJ("assets/displacement/LIN99DC6515E9MMPCT1P35R39.obj", "assets/displacement");
        m_vertices = model.vertices;
        m_indices = model.indices;
        m_materials = model.materials;
        m_materialStartIndex = model.materialStartIndex;
        m_materialIndexCount = model.materialIndexCount;

        // Автоматическая настройка камеры под размер модели
        {
            XMFLOAT3 minPos(FLT_MAX, FLT_MAX, FLT_MAX);
            XMFLOAT3 maxPos(-FLT_MAX, -FLT_MAX, -FLT_MAX);

            for (const auto& v : m_vertices) {
                if (v.position.x < minPos.x) minPos.x = v.position.x;
                if (v.position.y < minPos.y) minPos.y = v.position.y;
                if (v.position.z < minPos.z) minPos.z = v.position.z;
                if (v.position.x > maxPos.x) maxPos.x = v.position.x;
                if (v.position.y > maxPos.y) maxPos.y = v.position.y;
                if (v.position.z > maxPos.z) maxPos.z = v.position.z;
            }

            float centerX = (minPos.x + maxPos.x) * 0.5f;
            float centerY = (minPos.y + maxPos.y) * 0.5f;
            float centerZ = (minPos.z + maxPos.z) * 0.5f;

            float sizeX = maxPos.x - minPos.x;
            float sizeY = maxPos.y - minPos.y;
            float sizeZ = maxPos.z - minPos.z;
            float maxSize = std::max({ sizeX, sizeY, sizeZ });

            // Сдвигаем модель в центр
            for (auto& v : m_vertices) {
                v.position.x -= centerX;
                v.position.y -= centerY;
                v.position.z -= centerZ;
            }
        }

        // Загружаем текстуры
        ThrowIfFailed(m_commandAllocators[0]->Reset());
        ThrowIfFailed(m_commandList->Reset(m_commandAllocators[0].Get(), nullptr));

        for (size_t i = 0; i < m_materials.size(); i++) {
            auto& material = m_materials[i];
            if (!material.diffuseTexturePath.empty()) {
                Texture tex = TextureLoader::LoadTexture(m_device.Get(), m_commandList.Get(), material.diffuseTexturePath);
                material.textureIndex = (int)m_textures.size();
                m_textures.push_back(tex);
            }
            else {
                material.textureIndex = -1;
            }
        }

        // Если нет текстур, создаем дефолтную
        if (m_textures.empty()) {
            Texture defaultTex = TextureLoader::CreateDefaultTexture(m_device.Get(), m_commandList.Get());
            m_textures.push_back(defaultTex);
            for (auto& mat : m_materials) mat.textureIndex = 0;
        }

        ThrowIfFailed(m_commandList->Close());
        ID3D12CommandList* lists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(1, lists);
        WaitForGpu();

        // Создаем SRV кучу для обычных текстур
        CreateSRVHeap();
        m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Загружаем displacement map (ДОБАВЛЕНО)
        LoadDisplacementMap();

        // Создаем геометрические буферы из загруженных данных
        CreateBuffersFromData();
        CreateConstantBuffers();

        // Создаем пайплайн тесселяции (ДОБАВЛЕНО)
        CreateTessellationPipeline();

        // После CreateTessellationPipeline();
        OutputDebugStringA("Checking tessellation initialization...\n");

        for (uint32_t i = 0; i < kFrameCount; ++i) {
            char buffer[256];
            bool hasResource = (i < m_tessellationConstantBuffers.size() && m_tessellationConstantBuffers[i]);
            bool hasData = (i < m_tessCBData.size() && m_tessCBData[i] != nullptr);

            sprintf_s(buffer, "Tess frame %u: resource=%s, data=%s\n",
                i, hasResource ? "OK" : "MISSING", hasData ? "OK" : "MISSING");
            OutputDebugStringA(buffer);

            if (hasData) {
                TessellationConstantBuffer* cb = (TessellationConstantBuffer*)m_tessCBData[i];
                sprintf_s(buffer, "  Data: minTessDist=%.1f maxTessDist=%.1f minTess=%.1f maxTess=%.1f\n",
                    cb->tessParams.x, cb->tessParams.y,
                    cb->tessParams.z, cb->tessParams.w);
                OutputDebugStringA(buffer);
            }
        }

        m_viewport = { 0.0f, 0.0f, (float)kWidth, (float)kHeight, 0.0f, 1.0f };
        m_scissorRect = { 0, 0, (LONG)kWidth, (LONG)kHeight };

        OutputDebugStringA("\n=== Deferred Rendering with Tessellation Init Complete ===\n");
        return true;
    }
    catch (std::exception& e) {
        char buffer[512];
        sprintf_s(buffer, "Init failed: %s\n", e.what());
        OutputDebugStringA(buffer);
        MessageBoxA(nullptr, buffer, "Error", MB_ICONERROR);
        return false;
    }
}

void D3D12App::CreateBuffersFromData() {
    // Создаем вершинный буфер
    const UINT vertexBufferSize = sizeof(Vertex) * (UINT)m_vertices.size();

    ComPtr<ID3D12Resource> vertexUploadBuffer;
    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeapProps.CreationNodeMask = 1;
    uploadHeapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = vertexBufferSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&vertexUploadBuffer)), "Create vertex upload buffer");

    // Копируем данные вершин в upload буфер
    void* data;
    D3D12_RANGE readRange = { 0, 0 };
    ThrowIfFailed(vertexUploadBuffer->Map(0, &readRange, &data), "Map vertex upload buffer");
    memcpy(data, m_vertices.data(), vertexBufferSize);
    vertexUploadBuffer->Unmap(0, nullptr);

    // Создаем дефолтный вершинный буфер
    D3D12_HEAP_PROPERTIES defaultHeapProps = {};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    defaultHeapProps.CreationNodeMask = 1;
    defaultHeapProps.VisibleNodeMask = 1;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_vertexBuffer)), "Create vertex buffer");

    // Копируем данные из upload буфера в дефолтный
    ThrowIfFailed(m_commandAllocators[0]->Reset(), "Reset allocator");
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[0].Get(), nullptr), "Reset list");

    m_commandList->CopyResource(m_vertexBuffer.Get(), vertexUploadBuffer.Get());

    // Барьер для перевода вершинного буфера в нужное состояние
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = m_vertexBuffer.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(m_commandList->Close(), "Close vertex copy list");

    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);
    WaitForGpu();

    // Настраиваем представление вершинного буфера
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);
    m_vertexBufferView.SizeInBytes = vertexBufferSize;

    // ===== Создаем индексный буфер =====
    const UINT indexBufferSize = sizeof(uint32_t) * (UINT)m_indices.size();
    m_indexCount = (UINT)m_indices.size();

    bufferDesc.Width = indexBufferSize;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&vertexUploadBuffer)), "Create index upload buffer");

    ThrowIfFailed(vertexUploadBuffer->Map(0, &readRange, &data), "Map index upload buffer");
    memcpy(data, m_indices.data(), indexBufferSize);
    vertexUploadBuffer->Unmap(0, nullptr);

    ThrowIfFailed(m_device->CreateCommittedResource(
        &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_indexBuffer)), "Create index buffer");

    ThrowIfFailed(m_commandAllocators[0]->Reset(), "Reset allocator for index");
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[0].Get(), nullptr), "Reset list for index");

    m_commandList->CopyResource(m_indexBuffer.Get(), vertexUploadBuffer.Get());

    barrier.Transition.pResource = m_indexBuffer.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    m_commandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(m_commandList->Close(), "Close index copy list");

    lists[0] = m_commandList.Get();
    m_commandQueue->ExecuteCommandLists(1, lists);
    WaitForGpu();

    // Настраиваем представление индексного буфера
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexBufferView.SizeInBytes = indexBufferSize;

    OutputDebugStringA("Buffers created from data successfully\n");
}

void D3D12App::DebugPrintMaterialMapping() {
    char buffer[512];
    OutputDebugStringA("\n=== Material-Texture Mapping ===\n");
    for (size_t i = 0; i < m_materials.size(); i++) {
        sprintf_s(buffer, "Material[%zu]: '%s' -> Texture[%d]\n",
            i, m_materials[i].name.c_str(), m_materials[i].textureIndex);
        OutputDebugStringA(buffer);
    }

    OutputDebugStringA("\n=== Render Groups ===\n");
    for (size_t i = 0; i < m_materialStartIndex.size(); i++) {
        sprintf_s(buffer, "Group[%zu]: Start=%u, Count=%u\n",
            i, m_materialStartIndex[i], m_materialIndexCount[i]);
        OutputDebugStringA(buffer);
    }
    OutputDebugStringA("==========================\n");
}

// ==============================================
// Рендеринг
// ==============================================

void D3D12App::UpdateConstantBuffer(uint32_t bufferIndex) {
    //m_rotationAngle += 0.005f;
    //if (m_rotationAngle > XM_2PI) m_rotationAngle -= XM_2PI;
    m_textureAnimTime += 0.016f;

    // Обновляем камеру
    m_camera.Update((float)kWidth / kHeight);

    XMMATRIX world = XMMatrixRotationY(m_rotationAngle);
    XMMATRIX view = m_camera.GetViewMatrix();
    XMMATRIX proj = m_camera.GetProjectionMatrix();
    XMMATRIX wvp = world * view * proj;

    XMFLOAT3 cameraPos = m_camera.GetPosition();

    SceneConstantBuffer cb = {};
    XMStoreFloat4x4(&cb.worldViewProj, XMMatrixTranspose(wvp));
    XMStoreFloat4x4(&cb.world, XMMatrixTranspose(world));

    cb.lightPos = XMFLOAT4(10.0f, 15.0f, -10.0f, 1.0f);
    cb.lightColor = XMFLOAT4(1.0f, 0.95f, 0.8f, 1.0f);
    cb.cameraPos = XMFLOAT4(cameraPos.x, cameraPos.y, cameraPos.z, 1.0f);

    cb.materialAmbient = XMFLOAT4(0.3f, 0.25f, 0.2f, 1.0f);
    cb.materialDiffuse = XMFLOAT4(0.9f, 0.8f, 0.7f, 1.0f);
    cb.materialSpecular = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
    cb.materialShininess = 16.0f;

    // Анимация текстур
    //cb.textureScale = XMFLOAT2(2.0f, 2.0f);
    //float offsetX = sinf(m_textureAnimTime * 0.5f) * 0.1f;
    //float offsetY = cosf(m_textureAnimTime * 0.3f) * 0.1f;
    //cb.textureOffset = XMFLOAT2(offsetX, offsetY);
    //или
    // Установить значения по умолчанию:
    cb.textureScale = XMFLOAT2(1.0f, 1.0f);
    cb.textureOffset = XMFLOAT2(0.0f, 0.0f);

    memcpy(m_cbvDataBegin[bufferIndex], &cb, sizeof(SceneConstantBuffer));
}

void D3D12App::CreateGeometryPassRootSignature() {
    D3D12_ROOT_PARAMETER rootParams[2];
    ZeroMemory(rootParams, sizeof(rootParams));

    // Параметр 0: CBV для константного буфера сцены (b0)
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;

    // Параметр 1: Descriptor Table для текстур (t0)
    D3D12_DESCRIPTOR_RANGE descRange = {};
    descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descRange.NumDescriptors = 1;
    descRange.BaseShaderRegister = 0;
    descRange.RegisterSpace = 0;
    descRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &descRange;

    // Статический сэмплер (s0)
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sampler.MinLOD = 0;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = _countof(rootParams);
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error
    );

    if (FAILED(hr)) {
        if (error) {
            OutputDebugStringA((char*)error->GetBufferPointer());
        }
        ThrowIfFailed(hr, "Failed to serialize geometry root signature");
    }

    hr = m_device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&m_geometryRootSig)
    );

    if (FAILED(hr)) {
        ThrowIfFailed(hr, "Failed to create geometry root signature");
    }

    OutputDebugStringA("Geometry root signature created successfully\n");
}

// Замените CreatePipelineState() на:
void D3D12App::CreateGeometryPassPipelineState() {
    ComPtr<ID3DBlob> vs, ps, error;

    UINT compileFlags = 0;
#ifdef _DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    // Компилируем геометрические шейдеры с несколькими render targets
    HRESULT hr = D3DCompile(Shaders::GeometryVS, strlen(Shaders::GeometryVS),
        "VS", nullptr, nullptr, "main", "vs_5_0", compileFlags, 0, &vs, &error);

    if (FAILED(hr)) {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr, "Compile geometry VS");
    }

    hr = D3DCompile(Shaders::GeometryPS, strlen(Shaders::GeometryPS),
        "PS", nullptr, nullptr, "main", "ps_5_0", compileFlags, 0, &ps, &error);
    if (FAILED(hr)) {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr, "Compile geometry PS");
    }

    // Input Layout
    D3D12_INPUT_ELEMENT_DESC inputDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } // Добавлено!
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputDesc, _countof(inputDesc) };
    psoDesc.pRootSignature = m_geometryRootSig.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    // Настройка для Multiple Render Targets
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[2].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 3; // Albedo, WorldPos, Normal

    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    psoDesc.RTVFormats[2] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_geometryPSO));
    ThrowIfFailed(hr, "Create geometry PSO");
}

void D3D12App::RenderFrame() {
    if (!m_device || !m_swapChain) {
        OutputDebugStringA("Device or swapchain is null, skipping frame\n");
        return;
    }
    try {
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        WaitForPreviousFrame();

        ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());
        ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr));

        UpdateConstantBuffer(m_frameIndex);
        UpdateTessellationConstantBuffer(m_frameIndex); // Обновляем CB тесселяции

        m_viewport = { 0.0f, 0.0f, (float)kWidth, (float)kHeight, 0.0f, 1.0f };
        m_scissorRect = { 0, 0, (LONG)kWidth, (LONG)kHeight };

        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissorRect);

        // Барьер для Render Target (back buffer)
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &barrier);

        // Получаем дескрипторы
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += m_frameIndex * m_rtvDescriptorSize;
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

        // Создаем структуру для передачи данных о рендеринге
        // Создаем структуру для передачи данных о рендеринге
        RenderingSystem::RenderData renderData;

        // Базовые геометрические данные
        renderData.vertexBuffer = m_vertexBuffer.Get();
        renderData.indexBuffer = m_indexBuffer.Get();
        renderData.indexCount = m_indexCount;

        // Константный буфер сцены
        renderData.cbvAddress = m_constantBuffer[m_frameIndex]->GetGPUVirtualAddress();

        // Данные материалов и текстур
        renderData.modelSrvHeap = m_srvHeap.Get();
        renderData.srvDescriptorSize = m_srvDescriptorSize;
        renderData.materialStartIndex = m_materialStartIndex.data();
        renderData.materialIndexCount = m_materialIndexCount.data();
        renderData.numMaterials = (UINT)m_materials.size();
        renderData.materials = m_materials.data();

        // Параметры тесселяции (НОВЫЕ СТРОКИ)
        renderData.useTessellation = m_useTessellation;
        renderData.tessellationPSO = m_tessellationPSO.Get();
        renderData.tessellationRootSig = m_tessellationRootSig.Get();
        if (m_frameIndex < m_tessellationConstantBuffers.size() && m_tessellationConstantBuffers[m_frameIndex]) {
            renderData.tessCBVAddress = m_tessellationConstantBuffers[m_frameIndex]->GetGPUVirtualAddress();
        }
        else {
            renderData.tessCBVAddress = 0;
        }


        //char buffer[256];
        //sprintf_s(buffer, "Render params: useTess=%d, tessPSO=%p, tessRootSig=%p, tessCBV=%llx\n",
        //    renderData.useTessellation,
        //    renderData.tessellationPSO,
        //    renderData.tessellationRootSig,
        //    renderData.tessCBVAddress);
        //OutputDebugStringA(buffer);


        // Новые параметры для тесселяции
        renderData.useTessellation = m_useTessellation;
        renderData.tessellationPSO = m_tessellationPSO.Get();
        renderData.tessellationRootSig = m_tessellationRootSig.Get();

        // Вызываем deferred rendering с поддержкой тесселяции
        m_renderingSystem->Render(
            m_commandList.Get(),
            m_depthStencil.Get(),
            dsvHandle,
            rtvHandle,
            m_geometryRootSig.Get(),
            m_geometryPSO.Get(),
            &renderData
        );

        // Барьер для Present
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        m_commandList->ResourceBarrier(1, &barrier);

        ThrowIfFailed(m_commandList->Close());

        ID3D12CommandList* commandLists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(1, commandLists);

        m_swapChain->Present(1, 0);
        SignalFrame();

    }
    catch (std::exception& e) {
        char buffer[512];
        sprintf_s(buffer, "RenderFrame exception: %s\n", e.what());
        OutputDebugStringA(buffer);
    }
}

void D3D12App::UpdateTessellationConstantBuffer(uint32_t bufferIndex) {
    if (bufferIndex >= m_tessCBData.size() || !m_tessCBData[bufferIndex]) {
        char buf[256];
        sprintf_s(buf, "UpdateTessCB: EARLY RETURN! bufferIndex=%u, size=%zu, data=%p\n",
            bufferIndex, m_tessCBData.size(),
            bufferIndex < m_tessCBData.size() ? m_tessCBData[bufferIndex] : nullptr);
        OutputDebugStringA(buf);
        return;
    }

    TessellationConstantBuffer* cbData = (TessellationConstantBuffer*)m_tessCBData[bufferIndex];
    if (!cbData) return;

    // ---- Матрицы ----
    XMMATRIX view = m_camera.GetViewMatrix();
    XMMATRIX proj = m_camera.GetProjectionMatrix();
    XMFLOAT3 cameraPos = m_camera.GetPosition();

    XMStoreFloat4x4(&cbData->viewMatrix, XMMatrixTranspose(view));
    XMStoreFloat4x4(&cbData->projMatrix, XMMatrixTranspose(proj));
    cbData->cameraPos = XMFLOAT4(cameraPos.x, cameraPos.y, cameraPos.z, 1.0f);

    // ---- Параметры LOD ----
    cbData->tessParams = XMFLOAT4(
        m_tessMinDist,
        m_tessMaxDist,
        m_tessMinLevel,
        m_tessMaxLevel
    );

    // ---- Флаги визуализации ----
    cbData->visualParams = XMFLOAT4(
        m_showTessVisualization ? 1.0f : 0.0f,                      // x
        m_useNormalMap ? 1.0f : 0.0f,                      // y
        1.0f,                                                        // z = normalStrength
        (m_showTessVisualization && m_showWireframe) ? 1.0f : 0.0f  // w
    );

    // ---- Стиль визуализации ----
    cbData->visualParams2 = XMFLOAT4(
        m_lineWidth,                                                  // x
        (float)m_colorMode,                                           // y
        (m_showTessVisualization && m_showScaleBar) ? 1.0f : 0.0f,    // z
        220.0f                                                        // w = scaleBarWidth
    );

    // ---- Масштабная линейка ----
    cbData->visualParams3 = XMFLOAT4(
        28.0f,            // x = scaleBarHeight
        20.0f,            // y = scaleBarMargin
        m_tessMaxLevel,   // z = tessLevelMaxRef
        0.0f              // w = padding
    );

    char buf[256];
    sprintf_s(buf, "UpdateTessCB: minD=%.1f maxD=%.1f minLvl=%.1f maxLvl=%.1f showVis=%.1f\n",
        cbData->tessParams.x, cbData->tessParams.y,
        cbData->tessParams.z, cbData->tessParams.w,
        cbData->visualParams.x);
    OutputDebugStringA(buf);

}


// ==============================================
// Ожидание и синхронизация
// ==============================================

void D3D12App::WaitForPreviousFrame() {
    if (m_fence->GetCompletedValue() < m_fenceValue) {
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent), "SetEventOnCompletion");
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void D3D12App::SignalFrame() {
    m_fenceValue++;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValue), "Signal fence");
}

void D3D12App::WaitForGpu() {
    m_fenceValue++;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValue), "Signal fence for GPU wait");
    ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent), "SetEventOnCompletion for GPU wait");
    WaitForSingleObject(m_fenceEvent, INFINITE);
}

// ==============================================
// Управление камерой и ввод
// ==============================================

void D3D12App::OnMouseWheel(int delta) {
    float zoomAmount = (delta > 0) ? 0.5f : -0.5f;
    m_camera.Zoom(zoomAmount);
}

void D3D12App::OnMouseDown(int x, int y) {
    m_mousePressed = true;
    m_lastMousePos.x = x;
    m_lastMousePos.y = y;
    SetCapture(GetActiveWindow());
}

void D3D12App::OnMouseUp() {
    m_mousePressed = false;
    ReleaseCapture();
}

void D3D12App::OnMouseMove(int x, int y) {
    if (m_mousePressed) {
        int dx = x - m_lastMousePos.x;
        int dy = y - m_lastMousePos.y;
        m_camera.Rotate(dx, dy);
        m_lastMousePos.x = x;
        m_lastMousePos.y = y;
    }
}

void D3D12App::OnKeyDown(WPARAM wParam) {
    switch (wParam) {
    case 'W':
        m_camera.Zoom(-0.5f);
        break;
    case 'S':
        m_camera.Zoom(0.5f);
        break;
    case VK_UP:
        m_camera.Rotate(0, -10);
        break;
    case VK_DOWN:
        m_camera.Rotate(0, 10);
        break;
    case VK_LEFT:
        m_camera.Rotate(-10, 0);
        break;
    case VK_RIGHT:
        m_camera.Rotate(10, 0);
        break;
    case VK_OEM_PLUS:  // Клавиша +
    case VK_ADD:
        m_tessMultiplier *= 2.0f;
        break;
    case VK_OEM_MINUS: // Клавиша -
    case VK_SUBTRACT:
        m_tessMultiplier *= 0.5f;
        break;
    case '1': m_tessMaxLevel = std::max(1.0f, m_tessMaxLevel * 0.5f); break;  // Уменьшить макс. тесселяцию
    case '2': m_tessMaxLevel = std::min(64.0f, m_tessMaxLevel * 2.0f); break;  // Увеличить макс. тесселяцию
    case '3': m_tessMinDist = std::max(0.1f, m_tessMinDist - 1.0f); break;    // Уменьшить мин. дистанцию
    case '4': m_tessMinDist += 1.0f; break;                               // Увеличить мин. дистанцию
    case '5': m_tessMaxDist = std::max(1.0f, m_tessMaxDist - 5.0f); break;    // Уменьшить макс. дистанцию
    case '6': m_tessMaxDist += 5.0f; break;                               // Увеличить макс. дистанцию
    case 'N':  // Клавиша N для переключения normal mapping
        m_useNormalMap = !m_useNormalMap;
        if (m_useNormalMap) {
            OutputDebugStringA("Normal Mapping: ON\n");
        }
        else {
            OutputDebugStringA("Normal Mapping: OFF\n");
        }
        break;
    case 'V':
        m_showTessVisualization = !m_showTessVisualization;
        if (m_showTessVisualization) {
            OutputDebugStringA("=== Tessellation Visualization ON ===\n");

            if (m_frameIndex < m_tessCBData.size() && m_tessCBData[m_frameIndex]) {
                TessellationConstantBuffer* cb = (TessellationConstantBuffer*)m_tessCBData[m_frameIndex];
                char buf[512];
                sprintf_s(buf,
                    "Tess Params:\n"
                    "  cameraPos: (%.1f, %.1f, %.1f)\n"
                    "  minTessDist: %.1f\n"
                    "  maxTessDist: %.1f\n"
                    "  minTessLevel: %.1f\n"
                    "  maxTessLevel: %.1f\n"
                    "  showVis: %.1f\n",
                    cb->cameraPos.x, cb->cameraPos.y, cb->cameraPos.z,
                    cb->tessParams.x, cb->tessParams.y,
                    cb->tessParams.z, cb->tessParams.w,
                    cb->visualParams.x);
                OutputDebugStringA(buf);
            }
        }
        else {
            OutputDebugStringA("=== Tessellation Visualization OFF ===\n");
        }
        break;
    case 'B':  // Wireframe on/off
        m_showWireframe = !m_showWireframe;
        {
            char b[128];
            sprintf_s(b, "Wireframe: %s\n", m_showWireframe ? "ON" : "OFF");
            OutputDebugStringA(b);
        }
        break;

    case 'M':  // Режим раскраски
        m_colorMode = (m_colorMode + 1) % 3;
        {
            const char* names[3] = { "tessLevel", "distance", "patchHash" };
            char b[128];
            sprintf_s(b, "Color mode: %s\n", names[m_colorMode]);
            OutputDebugStringA(b);
        }
        break;

    case 'L':  // Масштабная линейка
        m_showScaleBar = !m_showScaleBar;
        {
            char b[128];
            sprintf_s(b, "Scale bar: %s\n", m_showScaleBar ? "ON" : "OFF");
            OutputDebugStringA(b);
        }
        break;

    case VK_OEM_4: // '[' — тоньше
        m_lineWidth = std::max(0.5f, m_lineWidth - 0.25f);
        {
            char b[128];
            sprintf_s(b, "Line width: %.2f\n", m_lineWidth);
            OutputDebugStringA(b);
        }
        break;

    case VK_OEM_6: // ']' — толще
        m_lineWidth = std::min(6.0f, m_lineWidth + 0.25f);
        {
            char b[128];
            sprintf_s(b, "Line width: %.2f\n", m_lineWidth);
            OutputDebugStringA(b);
        }
        break;
    }
}

void D3D12App::ResetCamera() {
    m_camera.Reset();
    OutputDebugStringA("Camera reset\n");
}

void D3D12App::LoadDisplacementMap() {
    // Создаем временный command list для загрузки
    ThrowIfFailed(m_commandAllocators[0]->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[0].Get(), nullptr));

    // Загружаем displacement текстуру
    m_displacementTexture = TextureLoader::CreateDisplacementTexture(
        m_device.Get(),
        m_commandList.Get(),
        "assets/displacement/texture_N_DISP.jpg"
    );

    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);
    WaitForGpu();

    // Сохраняем индекс для displacement текстуры
    m_displacementTextureIndex = static_cast<int>(m_textures.size());

    OutputDebugStringA("Displacement map loaded\n");
}

void D3D12App::CreateTessellationPipeline() {
    OutputDebugStringA("\n=== Creating Tessellation Pipeline ===\n");

    // ===== 1. Создание Root Signature =====
    OutputDebugStringA("1. Creating root signature...\n");

    D3D12_ROOT_PARAMETER rootParams[3];
    ZeroMemory(rootParams, sizeof(rootParams));

    // Параметр 0: CBV для сцены (b0)
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;

    // Параметр 1: CBV для параметров тесселяции (b1)
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[1].Descriptor.ShaderRegister = 1;
    rootParams[1].Descriptor.RegisterSpace = 0;

    // Параметр 2: Descriptor Table с текстурами
    D3D12_DESCRIPTOR_RANGE descRanges[3];
    ZeroMemory(descRanges, sizeof(descRanges));

    descRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descRanges[0].NumDescriptors = 1;
    descRanges[0].BaseShaderRegister = 0;
    descRanges[0].RegisterSpace = 0;
    descRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    descRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descRanges[1].NumDescriptors = 1;
    descRanges[1].BaseShaderRegister = 1;
    descRanges[1].RegisterSpace = 0;
    descRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    descRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descRanges[2].NumDescriptors = 1;
    descRanges[2].BaseShaderRegister = 2;
    descRanges[2].RegisterSpace = 0;
    descRanges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 3;
    rootParams[2].DescriptorTable.pDescriptorRanges = descRanges;

    // Статический сэмплер
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 16;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sampler.MinLOD = 0;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 3;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;

    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &signature, &error);
    if (FAILED(hr)) {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr, "Failed to serialize tessellation root signature");
    }

    hr = m_device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&m_tessellationRootSig));
    ThrowIfFailed(hr, "Failed to create tessellation root signature");
    OutputDebugStringA("Root signature created OK\n");

    // ===== 2. Компиляция шейдеров =====
    OutputDebugStringA("2. Compiling shaders...\n");

    UINT compileFlags = 0;
#ifdef _DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> vs, hs, ds, ps;

    // Vertex Shader
    static const char* passThroughVS = R"(
        struct VSInput {
            float3 position : POSITION;
            float3 normal   : NORMAL;
            float2 texcoord : TEXCOORD;
            float3 tangent  : TANGENT;
        };
        struct VSOutput {
            float3 position : POSITION;
            float3 normal   : NORMAL;
            float2 texcoord : TEXCOORD;
            float3 tangent  : TANGENT;
        };
        VSOutput main(VSInput input) {
            VSOutput output;
            output.position = input.position;
            output.normal = input.normal;
            output.texcoord = input.texcoord;
            output.tangent = input.tangent;
            return output;
        }
    )";

    hr = D3DCompile(passThroughVS, strlen(passThroughVS), nullptr, nullptr, nullptr,
        "main", "vs_5_0", compileFlags, 0, &vs, &error);
    if (FAILED(hr)) {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr, "Failed to compile VS");
    }
    OutputDebugStringA("VS compiled OK\n");

    // Hull Shader (динамический LOD)
    static const char* tessHS = R"(
struct HS_INPUT {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float3 tangent  : TANGENT;
};

struct HS_CONSTANT_OUTPUT {
    float edges[3]  : SV_TessFactor;
    float inside    : SV_InsideTessFactor;
    float tessLevel : TESSLEVEL;
    float patchId   : PATCHID;
};

struct HS_OUTPUT {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float3 tangent  : TANGENT;
    float  patchId  : PATCHID;
};

cbuffer TessellationParams : register(b1) {
    float4x4 viewMatrix;
    float4x4 projMatrix;
    float4   cameraPos;
    float4   tessParams;    // x=minDist, y=maxDist, z=minLevel, w=maxLevel
    float4   visualParams;  // x=showVis, y=useNormalMap, z=normalStrength, w=showWireframe
    float4   visualParams2; // x=lineWidth, y=colorMode, z=showScaleBar, w=scaleBarWidth
    float4   visualParams3; // x=scaleBarHeight, y=scaleBarMargin, z=tessLevelMaxRef, w=padding
};

[domain("tri")]
[partitioning("fractional_even")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConstantFunc")]
HS_OUTPUT main(InputPatch<HS_INPUT, 3> patch, uint id : SV_OutputControlPointID) {
    HS_OUTPUT o;
    o.position = patch[id].position;
    o.normal   = patch[id].normal;
    o.texcoord = patch[id].texcoord;
    o.tangent  = patch[id].tangent;
    o.patchId  = 0.0; // заполним в domain через patchID? — см. ниже
    return o;
}

HS_CONSTANT_OUTPUT PatchConstantFunc(InputPatch<HS_INPUT, 3> patch, uint patchID : SV_PrimitiveID) {
    HS_CONSTANT_OUTPUT o;
    
    float minTessDist  = tessParams.x;
    float maxTessDist  = tessParams.y;
    float minTessLevel = tessParams.z;
    float maxTessLevel = tessParams.w;
    
    float3 center = (patch[0].position + patch[1].position + patch[2].position) / 3.0f;
    float dist = length(cameraPos.xyz - center);
    
    float range = max(0.0001f, maxTessDist - minTessDist);
    float t = saturate((dist - minTessDist) / range);
    float tessLevel = lerp(maxTessLevel, minTessLevel, t);
    tessLevel = max(1.0f, tessLevel);
    
    //tessLevel  = 16.0;

    o.edges[0] = tessLevel;
    o.edges[1] = tessLevel;
    o.edges[2] = tessLevel;
    o.inside   = tessLevel;
    o.tessLevel = tessLevel;
    o.patchId   = (float)patchID;
    return o;
}
)";

    hr = D3DCompile(tessHS, strlen(tessHS), nullptr, nullptr, nullptr,
        "main", "hs_5_0", compileFlags, 0, &hs, &error);
    if (FAILED(hr)) {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr, "Failed to compile HS");
    }
    OutputDebugStringA("HS compiled OK\n");

    // Domain Shader
    static const char* tessDS = R"(
struct HS_CONSTANT_OUTPUT {
    float edges[3]  : SV_TessFactor;
    float inside    : SV_InsideTessFactor;
    float tessLevel : TESSLEVEL;
    float patchId   : PATCHID;
};

struct HS_OUTPUT {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float3 tangent  : TANGENT;
    float  patchId  : PATCHID;
};

struct DS_OUTPUT {
    float4 position  : SV_POSITION;
    float3 worldPos  : WORLDPOS;
    float3 normal    : NORMAL;
    float2 texcoord  : TEXCOORD;
    float3 tangent   : TANGENT;
    float3 bitangent : BITANGENT;
    float  tessLevel : TESSLEVEL;
    float  patchId   : PATCHID;
    float3 bary      : BARYCENTRIC;
    float  camDist   : CAMDIST;
};

cbuffer SceneConstant : register(b0) {
    float4x4 worldViewProj;
    float4x4 world;
    float4 lightPos;
    float4 lightColor;
    float4 cameraPos;
    float4 materialAmbient;
    float4 materialDiffuse;
    float4 materialSpecular;
    float materialShininess;
    float2 textureScale;
    float2 textureOffset;
}

[domain("tri")]
DS_OUTPUT main(HS_CONSTANT_OUTPUT input,
               const OutputPatch<HS_OUTPUT, 3> patch,
               float3 barycentric : SV_DomainLocation) {
    DS_OUTPUT o;

    float3 position = patch[0].position * barycentric.x +
                      patch[1].position * barycentric.y +
                      patch[2].position * barycentric.z;

    float3 normal = normalize(patch[0].normal * barycentric.x +
                              patch[1].normal * barycentric.y +
                              patch[2].normal * barycentric.z);

    float2 texcoord = patch[0].texcoord * barycentric.x +
                      patch[1].texcoord * barycentric.y +
                      patch[2].texcoord * barycentric.z;

    float3 tangent = normalize(patch[0].tangent * barycentric.x +
                               patch[1].tangent * barycentric.y +
                               patch[2].tangent * barycentric.z);

    float3 worldPos = mul(float4(position, 1.0), world).xyz;

    o.position   = mul(float4(position, 1.0), worldViewProj);
    o.worldPos   = worldPos;
    o.normal     = normalize(mul(float4(normal, 0.0), world).xyz);
    o.texcoord   = texcoord;
    o.tangent    = normalize(mul(float4(tangent, 0.0), world).xyz);
    o.bitangent  = cross(o.normal, o.tangent);
    o.tessLevel  = input.tessLevel;
    o.patchId    = input.patchId;
    o.bary       = barycentric;
    o.camDist    = length(cameraPos.xyz - worldPos);
    return o;
}
)";

    hr = D3DCompile(tessDS, strlen(tessDS), nullptr, nullptr, nullptr,
        "main", "ds_5_0", compileFlags, 0, &ds, &error);
    if (FAILED(hr)) {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr, "Failed to compile DS");
    }
    OutputDebugStringA("DS compiled OK\n");

    // Pixel Shader
    static const char* geometryPS = R"(
struct PS_INPUT {
    float4 position  : SV_POSITION;
    float3 worldPos  : WORLDPOS;
    float3 normal    : NORMAL;
    float2 texcoord  : TEXCOORD;
    float3 tangent   : TANGENT;
    float3 bitangent : BITANGENT;
    float  tessLevel : TESSLEVEL;
    float  patchId   : PATCHID;
    float3 bary      : BARYCENTRIC;
    float  camDist   : CAMDIST;
};

struct PS_OUTPUT {
    float4 albedo   : SV_Target0;
    float4 worldPos : SV_Target1;
    float4 normal   : SV_Target2;
};

cbuffer TessellationParams : register(b1) {
    float4x4 viewMatrix;
    float4x4 projMatrix;
    float4   cameraPos;
    float4   tessParams;    // x=minDist, y=maxDist, z=minLevel, w=maxLevel
    float4   visualParams;  // x=showVis, y=useNormalMap, z=normalStrength, w=showWireframe
    float4   visualParams2; // x=lineWidth, y=colorMode, z=showScaleBar, w=scaleBarWidth
    float4   visualParams3; // x=scaleBarHeight, y=scaleBarMargin, z=tessLevelMaxRef, w=padding
};

Texture2D diffuseTexture   : register(t0);
Texture2D normalMap        : register(t1);
Texture2D displacementMap  : register(t2);
SamplerState textureSampler : register(s0);

// Палитра для 7 дискретных уровней (1,2,4,8,16,32,64)
static const float3 kPalette[7] = {
    float3(0.10, 0.10, 0.55), // 1  — тёмно-синий
    float3(0.00, 0.75, 1.00), // 2  — голубой
    float3(0.00, 0.90, 0.20), // 4  — зелёный
    float3(0.95, 0.95, 0.00), // 8  — жёлтый
    float3(1.00, 0.55, 0.00), // 16 — оранжевый
    float3(1.00, 0.10, 0.10), // 32 — красный
    float3(1.00, 0.00, 0.85)  // 64 — маджента
};

// Ступенчатая раскраска по log2(level): 1→0, 2→1, 4→2, ... 64→6
float3 TessLevelColor(float level, float maxRef) {
    float l = max(level, 1.0);
    float idx = log2(l);                       // 0..6 для 1..64
    float maxIdx = log2(max(maxRef, 1.0));     // обычно 6
    float t = saturate(idx / max(1.0, maxIdx));// 0..1
    float seg = t * 6.0;                       // 7 сегментов → 0..6
    float i0 = floor(seg);
    float f  = seg - i0;
    int   a  = (int)clamp(i0, 0, 6);
    int   b  = (int)clamp(i0 + 1, 0, 6);
    // Ступенчато: без плавного перехода, чтобы соседние уровни не сливались
    return kPalette[a];
}

// Изолиния на границах 2^k
float TessIsoLine(float level) {
    float l = max(level, 1.0);
    float idx = log2(l);
    float frac_ = abs(frac(idx) - 0.0);
    // Резкая линия там, где idx ≈ целое (уровень = степень двойки)
    float d = abs(idx - round(idx));
    return smoothstep(0.08, 0.0, d);
}

// Цвет по расстоянию до камеры (синий → красный)
float3 DistanceColor(float d, float dMin, float dMax) {
    float t = saturate((d - dMin) / max(0.0001, dMax - dMin));
    // 5 сегментов
    float seg = t * 4.0;
    float i0 = floor(seg);
    float f  = seg - i0;
    int   a  = (int)clamp(i0, 0, 4);
    int   b  = (int)clamp(i0 + 1, 0, 4);
    float3 cols[5] = {
        float3(0.10, 0.30, 1.00),
        float3(0.00, 0.90, 0.90),
        float3(0.10, 0.90, 0.10),
        float3(1.00, 0.90, 0.00),
        float3(1.00, 0.10, 0.10)
    };
    return lerp(cols[a], cols[b], f);
}

// Хэш цвета по patchId, чтобы соседние патчи отличались
float3 PatchHashColor(float patchId) {
    float h = frac(sin(patchId * 12.9898) * 43758.5453);
    float h2 = frac(sin(patchId * 78.233) * 12345.6789);
    float h3 = frac(sin(patchId * 39.425) * 24680.1357);
    return float3(h, h2, h3);
}

// Рисуем масштабную линейку в правом верхнем углу
// screenSize — размер вьюпорта; возвращает rgb и alpha=1 если пиксель попал
float4 DrawScaleBar(float2 pixelPos, float2 screenSize) {
    float margin = visualParams3.y;   // ← стало
    float w      = visualParams2.w;   // ← стало
    float h      = visualParams3.x;   // ← стало
    float2 topRight = float2(screenSize.x - margin, margin);

    // Область легенды
    float2 barMin = float2(topRight.x - w, topRight.y);
    float2 barMax = float2(topRight.x, topRight.y + h);

    // Обводка
    float border = 1.0;
    bool inOuter =
        pixelPos.x >= barMin.x - border && pixelPos.x <= barMax.x + border &&
        pixelPos.y >= barMin.y - border && pixelPos.y <= barMax.y + border;
    if (!inOuter) return float4(0,0,0,0);

    // Внутренняя часть
    bool inInner =
        pixelPos.x >= barMin.x && pixelPos.x <= barMax.x &&
        pixelPos.y >= barMin.y && pixelPos.y <= barMax.y;

    if (!inInner) {
        return float4(0,0,0,1); // рамка
    }

    // 7 сегментов слева направо (64 → 1), чтобы соответствовать палитре
    float u = saturate((pixelPos.x - barMin.x) / w); // 0..1
    // Инвертируем: слева 64, справа 1
    float seg = (1.0 - u) * 7.0;
    int   idx = (int)clamp(floor(seg), 0, 6);
    float3 col = kPalette[idx];

    // Тонкие разделители между сегментами
    float segFrac = frac(seg);
    if (segFrac < 0.02 || segFrac > 0.98) col = float3(0,0,0);

    return float4(col, 1);
}

PS_OUTPUT main(PS_INPUT input) {
    PS_OUTPUT o;

    int   showVisualization = (int)visualParams.x;
    int   useNormalMap      = (int)visualParams.y;
    float normalStrength    = visualParams.z;
    int   showWireframe     = (int)visualParams.w;
    float lineWidth         = visualParams2.x;
    int   colorMode         = (int)visualParams2.y;
    int   showScaleBar      = (int)visualParams2.z;
    float scaleBarWidth     = visualParams2.w;
    float scaleBarHeight    = visualParams3.x;
    float scaleBarMargin    = visualParams3.y;   // ← ЭТО ПРОПУЩЕНО
    float tessLevelMaxRef   = visualParams3.z;

    float minTessDist       = tessParams.x;
    float maxTessDist       = tessParams.y;

    // ===== Диффузная текстура =====
    float4 texColor = diffuseTexture.Sample(textureSampler, input.texcoord);

    // ===== Normal mapping =====
    float3 worldNormal;
    if (useNormalMap) {
        float3 sn = normalMap.Sample(textureSampler, input.texcoord).rgb;
        sn = sn * 2.0 - 1.0;
        sn.xy *= normalStrength;
        sn = normalize(sn);
        float3 N = normalize(input.normal);
        float3 T = normalize(input.tangent);
        float3 B = normalize(input.bitangent);
        float3x3 TBN = float3x3(T, B, N);
        worldNormal = normalize(mul(sn, TBN));
    } else {
        worldNormal = normalize(input.normal);
    }

    // ===== Цвет =====
    float3 finalColor;

    if (showVisualization) {
        // --- Заливка по выбранному режиму ---
        float3 fill;
        if (colorMode == 0) {
            fill = TessLevelColor(input.tessLevel, tessLevelMaxRef);
        } else if (colorMode == 1) {
            fill = DistanceColor(input.camDist, minTessDist, maxTessDist);
        } else {
            fill = PatchHashColor(input.patchId);
        }

        // --- Изолинии на степенях двойки (только в режиме tessLevel) ---
        if (colorMode == 0) {
            float iso = TessIsoLine(input.tessLevel);
            fill = lerp(fill, float3(1,1,1), iso * 0.6);
        }

        // --- Wireframe через barycentric ---
        if (showWireframe) {
            float3 d = fwidth(input.bary);
            float3 a = smoothstep(float3(0,0,0), d * max(lineWidth, 0.5), input.bary);
            float edge = min(min(a.x, a.y), a.z); // 0 на ребре, 1 внутри
            // Инвертируем: 1 на ребре
            edge = 1.0 - edge;
            // Цвет линии подбираем контрастным к заливке
            float lum = dot(fill, float3(0.299, 0.587, 0.114));
            float3 lineCol = (lum > 0.5) ? float3(0,0,0) : float3(1,1,1);
            finalColor = lerp(fill, lineCol, edge);
        } else {
            finalColor = fill;
        }
    } else {
        finalColor = texColor.rgb;
    }

    // ===== Масштабная линейка =====
    if (showVisualization && showScaleBar) {
        // screenSize — размер RT; можно передать через cbuffer, но здесь захардкодим через SV_Position.w? 
        // Проще: получить размер через GetDimensions у текстуры? Нет.
        // Поэтому пробрасываем через scaleBarMargin-относительные координаты:
        // используем input.position.xy как пиксельные координаты, а размер берём из
        // константы 1920x1080 (совпадает с kWidth/kHeight). Если RT другой — поправьте.
        float2 screenSize = float2(1920.0, 1080.0);
        float4 bar = DrawScaleBar(input.position.xy, screenSize);
        if (bar.a > 0.0) {
            finalColor = lerp(finalColor, bar.rgb, bar.a);
        }
    }

    o.albedo   = float4(finalColor, 1.0);
    o.worldPos = float4(input.worldPos, 1.0);
    o.normal   = float4(worldNormal * 0.5 + 0.5, 1.0);
    return o;
}
)";

    hr = D3DCompile(geometryPS, strlen(geometryPS), nullptr, nullptr, nullptr,
        "main", "ps_5_0", compileFlags, 0, &ps, &error);
    if (FAILED(hr)) {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr, "Failed to compile PS");
    }
    OutputDebugStringA("PS compiled OK\n");

    // ===== 3. Создание PSO =====
    OutputDebugStringA("3. Creating PSO...\n");

    D3D12_INPUT_ELEMENT_DESC inputDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } // Добавлено!
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputDesc, _countof(inputDesc) };
    psoDesc.pRootSignature = m_tessellationRootSig.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.HS = { hs->GetBufferPointer(), hs->GetBufferSize() };
    psoDesc.DS = { ds->GetBufferPointer(), ds->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    psoDesc.BlendState.IndependentBlendEnable = TRUE;
    for (int i = 0; i < 3; i++) {
        psoDesc.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    psoDesc.NumRenderTargets = 3;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    psoDesc.RTVFormats[2] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_tessellationPSO));
    if (FAILED(hr)) {
        ThrowIfFailed(hr, "Failed to create tessellation PSO");
    }
    OutputDebugStringA("PSO created OK\n");

    // ===== 4. СОЗДАНИЕ КОНСТАНТНЫХ БУФЕРОВ ТЕССЕЛЯЦИИ =====
    // ВОТ ЭТА СЕКЦИЯ! Она идет ПОСЛЕ создания PSO
    OutputDebugStringA("4. Creating tessellation constant buffers...\n");

    m_tessellationConstantBuffers.clear();
    m_tessCBData.clear();

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        // ВАЖНО: Размер должен быть не меньше 256 байт и выровнен
        UINT structSize = sizeof(TessellationConstantBuffer);
        UINT alignedSize = (structSize + 255) & ~255;  // Выравнивание до 256

        char buf[256];
        sprintf_s(buf, "Tess CB size: struct=%u, aligned=%u\n", structSize, alignedSize);
        OutputDebugStringA(buf);

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Alignment = 0;
        bufferDesc.Width = alignedSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.SampleDesc.Quality = 0;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ComPtr<ID3D12Resource> cb;
        HRESULT hr = m_device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&cb)
        );

        if (FAILED(hr)) {
            ThrowIfFailed(hr, "Failed to create tess CB");
        }

        m_tessellationConstantBuffers.push_back(cb);

        // Мапим буфер
        D3D12_RANGE readRange = { 0, 0 };
        void* mappedData = nullptr;
        hr = cb->Map(0, &readRange, &mappedData);

        if (FAILED(hr) || !mappedData) {
            ThrowIfFailed(hr, "Failed to map tess CB");
        }

        m_tessCBData.push_back(mappedData);

        // ВАЖНО: Обнуляем ВЕСЬ буфер, а не только структуру
        ZeroMemory(mappedData, alignedSize);
    }


    char buf[256];
    sprintf_s(buf, "sizeof(TessellationConstantBuffer) = %zu bytes\n", sizeof(TessellationConstantBuffer));
    OutputDebugStringA(buf);

    OutputDebugStringA("=== Tessellation Pipeline Complete ===\n\n");
}