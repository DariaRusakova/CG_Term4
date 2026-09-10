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
using namespace DirectX;
using Microsoft::WRL::ComPtr;

// Вспомогательная функция
inline void ThrowIfFailed(HRESULT hr, const char* errorMsg = "") {
    if (FAILED(hr)) {
        char buffer[512];
        sprintf_s(buffer, "HRESULT 0x%08X: %s\n", hr, errorMsg);
        OutputDebugStringA(buffer);
        throw std::runtime_error(buffer);
    }
}

D3D12App::D3D12App() {
    for (auto& ptr : m_cbvDataBegin) ptr = nullptr;
    m_fenceEvent = nullptr;
}

D3D12App::~D3D12App() {
    Shutdown();
}

void D3D12App::Shutdown() {
    WaitForGpu();
    if (m_fenceEvent) CloseHandle(m_fenceEvent);
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        if (m_constantBuffer[i] && m_cbvDataBegin[i]) {
            m_constantBuffer[i]->Unmap(0, nullptr);
            m_cbvDataBegin[i] = nullptr;
        }
    }
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

void D3D12App::CreateBuffersFromData() {
    const UINT vertexBufferSize = sizeof(Vertex) * (UINT)m_vertices.size();
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
    memcpy(data, m_vertices.data(), vertexBufferSize);
    vertexUploadBuffer->Unmap(0, nullptr);
    D3D12_HEAP_PROPERTIES defaultHeapProps = {};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    ThrowIfFailed(m_device->CreateCommittedResource(
        &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_vertexBuffer)), "Create vertex buffer");
    ThrowIfFailed(m_commandAllocators[0]->Reset(), "Reset allocator");
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[0].Get(), nullptr), "Reset list");
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

    // Индексный буфер
    const UINT indexBufferSize = sizeof(uint32_t) * (UINT)m_indices.size();
    m_indexCount = (UINT)m_indices.size();
    bufferDesc.Width = indexBufferSize;
    ThrowIfFailed(m_device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&vertexUploadBuffer)), "Create index upload buffer");
    ThrowIfFailed(vertexUploadBuffer->Map(0, nullptr, &data), "Map index upload buffer");
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
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexBufferView.SizeInBytes = indexBufferSize;
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

// Создание PSO для рендеринга теней (только глубина)

void D3D12App::CreateShadowPassPipelineState() {
    static const char* shadowVS = R"(
struct VSInput {
    float3 position : POSITION;
};
struct VSOutput {
    float4 position : SV_POSITION;
};
cbuffer ShadowCB : register(b0) {
    float4x4 lightViewProj;
};
VSOutput main(VSInput input) {
    VSOutput output;
    output.position = mul(float4(input.position, 1.0), lightViewProj);
    return output;
}
)";

    ComPtr<ID3DBlob> vs, error;
    UINT compileFlags = 0;
#ifdef _DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(shadowVS, strlen(shadowVS),
        "ShadowVS", nullptr, nullptr, "main", "vs_5_0", compileFlags, 0, &vs, &error);
    if (FAILED(hr)) {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr, "Compile shadow VS");
    }

    D3D12_ROOT_PARAMETER rootParams[1];
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 1;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr)) {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr, "Serialize shadow root sig");
    }

    hr = m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&m_shadowRootSig));
    ThrowIfFailed(hr, "Create shadow root sig");

    D3D12_INPUT_ELEMENT_DESC inputDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputDesc, _countof(inputDesc) };
    psoDesc.pRootSignature = m_shadowRootSig.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { nullptr, 0 };

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = 100000;
    psoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 0;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_shadowPSO));
    ThrowIfFailed(hr, "Create shadow PSO");
}

bool D3D12App::Initialize(HWND hwnd) {
    try {
        EnableDebugLayer();
        CreateDevice();
        CreateCommandObjects();
        CreateSwapChain(hwnd);
        CreateDescriptorHeaps();
        CreateDepthStencil();

        // Инициализация системы теней (единственный экземпляр)
        m_shadowMapSystem = std::make_unique<ShadowMapSystem>();
        m_shadowMapSystem->Initialize(m_device.Get());

        CreateGeometryPassRootSignature();
        CreateGeometryPassPipelineState();
        CreateShadowPassPipelineState();

        // Инициализируем систему рендеринга
        m_renderingSystem = std::make_unique<RenderingSystem>();
        m_renderingSystem->Initialize(m_device.Get(), kWidth, kHeight);

        // Передаем ресурсы тени в RenderingSystem
        m_renderingSystem->SetShadowResources(
            m_shadowMapSystem->GetConstantBuffer(),
            m_shadowMapSystem->GetSRV()
        );

        // Копируем дескриптор SRV тени в комбинированный хип RenderingSystem
        D3D12_CPU_DESCRIPTOR_HANDLE destCpuHandle = m_renderingSystem->GetCombinedSrvHeap()->GetCPUDescriptorHandleForHeapStart();
        destCpuHandle.ptr += GBuffer::GB_COUNT * m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_CPU_DESCRIPTOR_HANDLE srcCpuHandle = m_shadowMapSystem->GetSRVHeap()->GetCPUDescriptorHandleForHeapStart();
        m_device->CopyDescriptorsSimple(1, destCpuHandle, srcCpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Загружаем модель
        ModelData model = ModelLoader::LoadOBJ("assets/sponza.obj", "assets");
        m_vertices = model.vertices;
        m_indices = model.indices;
        m_materials = model.materials;
        m_materialStartIndex = model.materialStartIndex;
        m_materialIndexCount = model.materialIndexCount;

        // Загружаем текстуры
        m_commandList->Reset(m_commandAllocators[0].Get(), nullptr);
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
        if (m_textures.empty()) {
            Texture defaultTex = TextureLoader::CreateDefaultTexture(m_device.Get(), m_commandList.Get());
            m_textures.push_back(defaultTex);
            for (auto& mat : m_materials) mat.textureIndex = 0;
        }
        ThrowIfFailed(m_commandList->Close());
        ID3D12CommandList* lists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(1, lists);
        WaitForGpu();

        CreateSRVHeap();
        m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        CreateBuffersFromData();
        CreateConstantBuffers();

        m_viewport = { 0.0f, 0.0f, (float)kWidth, (float)kHeight, 0.0f, 1.0f };
        m_scissorRect = { 0, 0, (LONG)kWidth, (LONG)kHeight };

        OutputDebugStringA("\n=== Deferred Rendering Init Complete ===\n");
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
    m_textureAnimTime += 0.016f;
    m_camera.Update((float)kWidth / kHeight);

    XMMATRIX world = XMMatrixIdentity(); // Убрал вращение для статичной сцены Sponza
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
    cb.textureScale = XMFLOAT2(1.0f, 1.0f);
    cb.textureOffset = XMFLOAT2(0.0f, 0.0f);

    memcpy(m_cbvDataBegin[bufferIndex], &cb, sizeof(SceneConstantBuffer));
}

void D3D12App::CreateGeometryPassRootSignature() {
    D3D12_ROOT_PARAMETER rootParams[2];
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;

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

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 2;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr)) {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr, "Serialize geometry root signature");
    }
    hr = m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&m_geometryRootSignature));
    ThrowIfFailed(hr, "Create geometry root signature");
}

void D3D12App::CreateGeometryPassPipelineState() {
    ComPtr<ID3DBlob> vs, ps, error;
    UINT compileFlags = 0;
#ifdef _DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
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

    D3D12_INPUT_ELEMENT_DESC inputDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputDesc, _countof(inputDesc) };
    psoDesc.pRootSignature = m_geometryRootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[2].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 3;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    psoDesc.RTVFormats[2] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_geometryPSO));
    ThrowIfFailed(hr, "Create geometry PSO");
}

// D3D12App.cpp - исправленный RenderShadowMapPass:

void D3D12App::RenderShadowMapPass(ID3D12GraphicsCommandList* cmdList) {
    if (!m_shadowMapSystem || !m_shadowPSO || !m_shadowRootSig) {
        OutputDebugStringA("WARNING: ShadowMapSystem, PSO or RootSig is null!\n");
        return;
    }

    // Проверка геометрии
    UINT totalIndices = 0;
    for (UINT j = 0; j < (UINT)m_materials.size(); j++) {
        if (m_materialIndexCount[j] > 0) {
            totalIndices += m_materialIndexCount[j];
        }
    }

    if (totalIndices == 0) {
        OutputDebugStringA("[ShadowPass] ERROR: No indices to render!\n");
        return;
    }

    if (m_vertexBufferView.BufferLocation == 0 || m_indexBufferView.BufferLocation == 0) {
        OutputDebugStringA("[ShadowPass] ERROR: Vertex or Index buffer is null!\n");
        return;
    }

    // Свет сверху
    XMMATRIX lightView = XMMatrixLookAtLH(
        XMVectorSet(0.0f, 50.0f, 0.0f, 1.0f),
        XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),
        XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)
    );

    // Ортографическая проекция
    float orthoSize = 60.0f;
    XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(
        -orthoSize, orthoSize,
        -orthoSize, orthoSize,
        1.0f, 200.0f
    );

    XMFLOAT3 camPos = m_camera.GetPosition();
    m_shadowMapSystem->UpdateCascades(lightView, lightProj, camPos, 0.1f, 100.0f);

    ID3D12Resource* shadowResource = m_shadowMapSystem->GetDepthResource();
    if (!shadowResource) {
        OutputDebugStringA("[ShadowPass] ERROR: Shadow resource is null!\n");
        return;
    }

    // Переход из PIXEL_SHADER_RESOURCE в DEPTH_WRITE
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = shadowResource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    cmdList->SetPipelineState(m_shadowPSO);
    cmdList->SetGraphicsRootSignature(m_shadowRootSig.Get());

    D3D12_VIEWPORT shadowViewport = {
        0.0f, 0.0f,
        (float)ShadowMapSystem::SHADOW_MAP_SIZE,
        (float)ShadowMapSystem::SHADOW_MAP_SIZE,
        0.0f, 1.0f
    };
    D3D12_RECT shadowScissor = {
        0, 0,
        (LONG)ShadowMapSystem::SHADOW_MAP_SIZE,
        (LONG)ShadowMapSystem::SHADOW_MAP_SIZE
    };
    cmdList->RSSetViewports(1, &shadowViewport);
    cmdList->RSSetScissorRects(1, &shadowScissor);

    cmdList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    cmdList->IASetIndexBuffer(&m_indexBufferView);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmdList->SetGraphicsRootConstantBufferView(0, m_shadowMapSystem->GetConstantBuffer()->GetGPUVirtualAddress());

    // ============================================
    // РЕНДЕРИМ ВСЕ КАСКАДЫ
    // ============================================
    for (int cascade = 0; cascade < ShadowMapSystem::CASCADE_COUNT; ++cascade) {
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_shadowMapSystem->GetDSVForCascade(cascade);
        if (dsvHandle.ptr == 0) {
            OutputDebugStringA("[ShadowPass] ERROR: Invalid DSV handle!\n");
            continue;
        }

        cmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        cmdList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);

        for (UINT j = 0; j < (UINT)m_materials.size(); j++) {
            if (m_materialIndexCount[j] > 0) {
                cmdList->DrawIndexedInstanced(m_materialIndexCount[j], 1,
                    m_materialStartIndex[j], 0, 0);
            }
        }
    }

    // Возвращаем в состояние PIXEL_SHADER_RESOURCE
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &barrier);
}


void D3D12App::RenderFrame() {
    try {
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        WaitForPreviousFrame();
        ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());
        ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr));

        UpdateConstantBuffer(m_frameIndex);

        m_viewport = { 0.0f, 0.0f, (float)kWidth, (float)kHeight, 0.0f, 1.0f };
        m_scissorRect = { 0, 0, (LONG)kWidth, (LONG)kHeight };
        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissorRect);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += m_frameIndex * m_rtvDescriptorSize;
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

        // 1. Shadow Pass
        RenderShadowMapPass(m_commandList.Get());

        // 2. Geometry + Lighting Pass (теперь с визуализацией shadowFactor)
        ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
        m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

        RenderingSystem::RenderData renderData;
        renderData.vertexBuffer = m_vertexBuffer.Get();
        renderData.indexBuffer = m_indexBuffer.Get();
        renderData.indexCount = m_indexCount;
        renderData.cbvAddress = m_constantBuffer[m_frameIndex]->GetGPUVirtualAddress();
        renderData.modelSrvHeap = m_srvHeap.Get();
        renderData.srvDescriptorSize = m_srvDescriptorSize;
        renderData.materialStartIndex = m_materialStartIndex.data();
        renderData.materialIndexCount = m_materialIndexCount.data();
        renderData.numMaterials = (UINT)m_materials.size();
        renderData.materials = m_materials.data();

        m_renderingSystem->SetGlobalIntensity(m_lightIntensity);

        m_renderingSystem->Render(
            m_commandList.Get(),
            m_depthStencil.Get(),
            dsvHandle,
            rtvHandle,
            m_geometryRootSignature.Get(),
            m_geometryPSO.Get(),
            &renderData
        );

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
        OutputDebugStringA(e.what());
    }
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
    case 'O':
        m_lightIntensity += m_lightIntensityStep;
        if (m_lightIntensity > 5.0f) m_lightIntensity = 5.0f;
        {
            char buf[64];
            sprintf_s(buf, "Light Intensity: %.1f\n", m_lightIntensity);
            OutputDebugStringA(buf);
        }
        break;
    case 'P':
        m_lightIntensity -= m_lightIntensityStep;
        if (m_lightIntensity < 0.0f) m_lightIntensity = 0.0f;
        {
            char buf[64];
            sprintf_s(buf, "Light Intensity: %.1f\n", m_lightIntensity);
            OutputDebugStringA(buf);
        }
        break;
    case '0':
        m_lightIntensity = 1.0f;
        OutputDebugStringA("Light Intensity Reset to 1.0\n");
        break;
    }
}

void D3D12App::ResetCamera() {
    m_camera.Reset();
    OutputDebugStringA("Camera reset\n");
}