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
#include "Terrain/TerrainLoader.h"

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
    m_fenceEvent = nullptr;
}

D3D12App::~D3D12App() {
    OutputDebugStringA("[App] destroying\n");
    Shutdown();
}

void D3D12App::Shutdown() {
    OutputDebugStringA("[Shutdown] entering\n");
    WaitForGpu();
    if (m_fenceEvent) CloseHandle(m_fenceEvent);

    for (uint32_t f = 0; f < kFrameCount; ++f) {
        for (UINT o = 0; o < kMaxObjects; ++o) {
            if (m_constantBuffer[f][o] && m_cbvDataBegin[f][o]) {
                m_constantBuffer[f][o]->Unmap(0, nullptr);
                m_cbvDataBegin[f][o] = nullptr;
            }
        }
    }
}

static int RegisterTexture(ID3D12Device* device,
    ID3D12DescriptorHeap* heap,
    UINT& used,
    UINT capacity,
    UINT descriptorSize,
    const Texture& tex)
{
    if (used >= capacity) return -1;

    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += (UINT64)used * descriptorSize;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = tex.format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    device->CreateShaderResourceView(tex.resource.Get(), &srvDesc, handle);
    return (int)used++;
}

static SceneObject CreateUVSphere(float radius, int segments, int rings) {
    SceneObject sphere;

    for (int y = 0; y <= rings; ++y) {
        float v = (float)y / rings;
        float phi = v * XM_PI;   // 0..PI

        for (int x = 0; x <= segments; ++x) {
            float u = (float)x / segments;
            float theta = u * XM_2PI;   // 0..2PI

            float px = radius * sinf(phi) * cosf(theta);
            float py = radius * cosf(phi);
            float pz = radius * sinf(phi) * sinf(theta);

            Vertex vtx;
            vtx.position = XMFLOAT3(px, py, pz);
            vtx.normal = XMFLOAT3(px / radius, py / radius, pz / radius);
            vtx.texcoord = XMFLOAT2(u, v);
            sphere.vertices.push_back(vtx);
        }
    }

    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            uint32_t a = y * (segments + 1) + x;
            uint32_t b = a + segments + 1;

            sphere.indices.push_back(a);
            sphere.indices.push_back(b);
            sphere.indices.push_back(a + 1);

            sphere.indices.push_back(a + 1);
            sphere.indices.push_back(b);
            sphere.indices.push_back(b + 1);
        }
    }

    Material mat;
    mat.name = "sphere";
    sphere.materials.push_back(mat);
    sphere.materialStartIndex.push_back(0);
    sphere.materialIndexCount.push_back((uint32_t)sphere.indices.size());
    sphere.indexCount = (uint32_t)sphere.indices.size();
    sphere.hasTextures = false;

    return sphere;
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
    // Резервируем с запасом: 2 модели * ~6 карт + fallback
    m_srvHeapCapacity = 64;

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = m_srvHeapCapacity;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)),
        "Create SRV heap");

    m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    m_srvHeapUsed = 0;
}

void D3D12App::CreateConstantBuffers() {
    for (uint32_t f = 0; f < kFrameCount; ++f) {
        for (UINT o = 0; o < kMaxObjects; ++o) {
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
                IID_PPV_ARGS(&m_constantBuffer[f][o])), "Create CB");

            D3D12_RANGE rr = { 0, 0 };
            ThrowIfFailed(m_constantBuffer[f][o]->Map(0, &rr, &m_cbvDataBegin[f][o]),
                "Map CB");
        }
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

        m_renderingSystem = std::make_unique<RenderingSystem>();
        m_renderingSystem->Initialize(m_device.Get(), kWidth, kHeight);

        // SRV heap заранее — до загрузки текстур
        CreateSRVHeap();

        ThrowIfFailed(m_commandAllocators[0]->Reset());
        ThrowIfFailed(m_commandList->Reset(m_commandAllocators[0].Get(), nullptr));

        {
            float globalMin = 1e9f;
            float globalMax = -1e9f;

            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    char path[256];
                    sprintf_s(path, "assets/volcano/Erosion2/Erosion2_Out_y%d_x%d.png", y, x);

                    int w = 0, h = 0;
                    auto height = TerrainLoader::LoadHeightmap(path, w, h);
                    if (height.empty()) continue;

                    for (float v : height) {
                        if (v < globalMin) globalMin = v;
                        if (v > globalMax) globalMax = v;
                    }
                }
            }

            m_terrainHeightMin = globalMin;
            m_terrainHeightMax = globalMax;

            char buf[256];
            sprintf_s(buf, "[Terrain] GLOBAL: min=%.4f max=%.4f\n", globalMin, globalMax);
            OutputDebugStringA(buf);
        }

        {
            const int tilesX = 4;
            const int tilesZ = 4;
            const float tileSize = m_terrainWorldSize;  

            for (int y = 0; y < tilesZ; ++y) {
                for (int x = 0; x < tilesX; ++x) {

                    char path[256];
                    sprintf_s(path,
                        "assets/volcano/Erosion2/Erosion2_Out_y%d_x%d.png", y, x);

                    int w = 0, h = 0;
                    auto height = TerrainLoader::LoadHeightmap(path, w, h);
                    if (height.empty()) {
                        char buf[256];
                        sprintf_s(buf, "[Terrain] Skipping tile y%d x%d (empty)\n", y, x);
                        OutputDebugStringA(buf);
                        continue;
                    }

                    SceneObject tile = CreateTerrainTileFromHeightmap(
                        height, w, h,
                        m_terrainGridRes,
                        m_terrainWorldSize,
                        m_terrainHeightMin, m_terrainHeightMax,
                        m_terrainHeightScale);

                    XMFLOAT4X4 world;
                    XMStoreFloat4x4(&world, XMMatrixTranslation(
                        (float)x * tileSize, 0.0f, (float)y * tileSize));
                    tile.world = world;

                    CreateMeshBuffers(tile);
                    m_objects.push_back(std::move(tile));

                    char buf[256];
                    sprintf_s(buf,
                        "[Terrain] Tile y%d x%d: %zu verts, %zu idx at (%.0f, 0, %.0f)\n",
                        y, x,
                        m_objects.back().vertices.size(),
                        m_objects.back().indices.size(),
                        (float)x * tileSize, (float)y * tileSize);
                    OutputDebugStringA(buf);
                }
            }
        }


        {
            Texture defaultTex = TextureLoader::CreateDefaultTexture(
                m_device.Get(), m_commandList.Get());
            int idx = RegisterTexture(m_device.Get(), m_srvHeap.Get(),
                m_srvHeapUsed, m_srvHeapCapacity,
                m_srvDescriptorSize, defaultTex);
            m_textures.push_back(std::move(defaultTex));
            char buf[64];
            sprintf_s(buf, "[SRV] Default white -> srv %d\n", idx);
            OutputDebugStringA(buf);
        }

        // Cerberus — слева
        {
            MaterialPaths paths;
            paths.albedo = "assets/Cerberus_by_Andrew_Maximov/Textures/Cerberus_A.jpg";
            paths.normal = "assets/Cerberus_by_Andrew_Maximov/Textures/Cerberus_N.jpg";
            paths.roughness = "assets/Cerberus_by_Andrew_Maximov/Textures/Cerberus_R.jpg";
            paths.metallic = "assets/Cerberus_by_Andrew_Maximov/Textures/Cerberus_M.jpg";
            paths.ao = "";  // AO-карты у Cerberus нет

            SceneObject cerberus;
            XMFLOAT4X4 world;
            XMStoreFloat4x4(&world, XMMatrixTranslation(-1.0f, 0.0f, 0.0f));

            if (LoadSceneObject(
                "assets/Cerberus_by_Andrew_Maximov/Cerberus_LP.obj",
                "assets",
                world, cerberus, paths))
            {
                m_objects.push_back(std::move(cerberus));
            }
        }

        // Wood root — справа
        {
            MaterialPaths paths;
            paths.albedo = "assets/wood_root/Aset_wood_root_M_rkswd_2K_Albedo.jpg";
            paths.normal = "assets/wood_root/Aset_wood_root_M_rkswd_2K_Normal_LOD0.jpg";
            paths.roughness = "assets/wood_root/Aset_wood_root_M_rkswd_2K_Roughness.jpg";
            paths.metallic = "";  // metallic-карты у wood_root нет 
            paths.ao = "";  // AO-карты у wood_root нет

            SceneObject woodRoot;
            XMFLOAT4X4 world;
            XMStoreFloat4x4(&world, XMMatrixTranslation(1.0f, 0.0f, 0.0f));

            if (LoadSceneObject(
                "assets/wood_root/Aset_wood_root_M_rkswd_LOD0.obj",
                "assets",
                world, woodRoot, paths))
            {
                m_objects.push_back(std::move(woodRoot));
            }
        }

        if (m_objects.empty()) {
            OutputDebugStringA("[Scene] No models loaded\n");
            return false;
        }

        m_renderingSystem->CreateIBLResources(m_device.Get(), m_commandList.Get());

        // Закрываем список и ждём завершения загрузки
        ThrowIfFailed(m_commandList->Close());
        ID3D12CommandList* lists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(1, lists);
        WaitForGpu();

        for (auto& obj : m_objects) {
            obj.vbUpload.Reset();
            obj.ibUpload.Reset();
        }

        CreateConstantBuffers();

        m_viewport = { 0.0f, 0.0f, (float)kWidth, (float)kHeight, 0.0f, 1.0f };
        m_scissorRect = { 0, 0, (LONG)kWidth, (LONG)kHeight };

        OutputDebugStringA("\n=== Scene Init Complete ===\n");
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


// ==============================================
// Рендеринг
// ==============================================

void D3D12App::UpdateConstantBuffer(uint32_t frameIndex) {
    m_textureAnimTime += 0.016f;
    m_camera.Update((float)kWidth / kHeight);

    XMMATRIX view = m_camera.GetViewMatrix();
    XMMATRIX proj = m_camera.GetProjectionMatrix();
    XMFLOAT3 cameraPos = m_camera.GetPosition();

    for (UINT o = 0; o < (UINT)m_objects.size() && o < kMaxObjects; ++o) {
        XMMATRIX world = XMLoadFloat4x4(&m_objects[o].world);
        XMMATRIX wvp = world * view * proj;

        auto& obj = m_objects[o];
        SceneConstantBuffer cb = {};
        XMStoreFloat4x4(&cb.worldViewProj, XMMatrixTranspose(wvp));
        XMStoreFloat4x4(&cb.world, XMMatrixTranspose(world));

        cb.lightPos = XMFLOAT4(10.0f, 15.0f, -10.0f, 1.0f);
        cb.lightColor = XMFLOAT4(1.0f, 0.95f, 0.8f, 1.0f);
        cb.cameraPos = XMFLOAT4(cameraPos.x, cameraPos.y, cameraPos.z, 1.0f);



        cb.textureScale = XMFLOAT2(1.0f, 1.0f);
        cb.textureOffset = XMFLOAT2(0.0f, 0.0f);

        const Material& mat = obj.materials.empty() ? Material{} : obj.materials[0];
        cb.materialFactor = XMFLOAT4(
            mat.roughnessFactor,
            mat.metallicFactor,
            mat.aoFactor,
            obj.hasTextures ? 1.0f : 0.0f    // ← .w = useMaterialTextures
        );
   

        memcpy(m_cbvDataBegin[frameIndex][o], &cb, sizeof(SceneConstantBuffer));
    }
}

void D3D12App::CreateGeometryPassRootSignature() {
    D3D12_ROOT_PARAMETER rootParams[2];

    // CBV для константного буфера сцены
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;

    // Descriptor Table для текстур
    D3D12_DESCRIPTOR_RANGE descRange = {};
    descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descRange.NumDescriptors = 5;
    descRange.BaseShaderRegister = 0;
    descRange.RegisterSpace = 0;
    descRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &descRange;

    // Статический сэмплер
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
    psoDesc.BlendState.RenderTarget[3].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 4; // Albedo, WorldPos, Normal, PBR

    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.RTVFormats[2] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.RTVFormats[3] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_geometryPSO));
    ThrowIfFailed(hr, "Create geometry PSO");
}

void D3D12App::RenderFrame() {
    try {
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        XMFLOAT3 cameraPos = m_camera.GetPosition();
        WaitForPreviousFrame();

        ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());
        ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr));

        UpdateConstantBuffer(m_frameIndex);

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

        // Устанавливаем текстуры для геометрического прохода
        ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
        m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

        XMMATRIX view = m_camera.GetViewMatrix();
        XMMATRIX proj = m_camera.GetProjectionMatrix();
        XMMATRIX viewProj = view * proj;
        XMFLOAT4X4 viewProjF;
        XMStoreFloat4x4(&viewProjF, viewProj);

        std::vector<RenderingSystem::RenderData> renderData;
        renderData.reserve(m_objects.size());

        for (UINT o = 0; o < (UINT)m_objects.size() && o < kMaxObjects; ++o) {
            auto& obj = m_objects[o];

            RenderingSystem::RenderData rd = {};
            rd.vertexBuffer = obj.vertexBuffer.Get();
            rd.indexBuffer = obj.indexBuffer.Get();
            rd.indexCount = obj.indexCount;
            rd.cbvAddress = m_constantBuffer[m_frameIndex][o]->GetGPUVirtualAddress();
            rd.modelSrvHeap = m_srvHeap.Get();
            rd.srvDescriptorSize = m_srvDescriptorSize;
            rd.materialStartIndex = obj.materialStartIndex.data();
            rd.materialIndexCount = obj.materialIndexCount.data();
            rd.numMaterials = (UINT)obj.materials.size();
            rd.materials = obj.materials.data();
            rd.viewProj = viewProjF;
            rd.cameraPos = cameraPos;
            renderData.push_back(rd);
        }

        static LARGE_INTEGER freq = {};
        static LARGE_INTEGER last = {};
        if (freq.QuadPart == 0) {
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&last);
        }
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float deltaTime = float(now.QuadPart - last.QuadPart) / float(freq.QuadPart);
        last = now;
        // Ограничиваем dt, чтобы при паузах/отладке свет не "прыгал"
        if (deltaTime > 0.1f) deltaTime = 0.1f;

        m_shootCooldown = std::max(0.0f, m_shootCooldown - deltaTime);

        m_renderingSystem->Update(deltaTime);
        m_renderingSystem->SetGlobalIntensity(m_lightIntensity);


        // Вызываем deferred rendering
        m_renderingSystem->Render(
            m_commandList.Get(),
            m_depthStencil.Get(),
            dsvHandle,
            rtvHandle,
            m_geometryRootSignature.Get(),
            m_geometryPSO.Get(),
            renderData.data(),
            (UINT)renderData.size()
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
        m_camera.ZoomScaled(0.9f);
        break;
    case 'S': 
        m_camera.ZoomScaled(1.1f); 
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
    case 'A': m_camera.MoveTarget(-50.0f, 0.0f, 0.0f); break; 
    case 'D': m_camera.MoveTarget(50.0f, 0.0f, 0.0f); break;  
    case 'Q': m_camera.MoveTarget(0.0f, 0.0f, -50.0f); break; 
    case 'E': m_camera.MoveTarget(0.0f, 0.0f, 50.0f); break;  

    case 'O':  // Увеличить интенсивность
        m_lightIntensity += m_lightIntensityStep;
        if (m_lightIntensity > 5.0f) m_lightIntensity = 5.0f;
        {
            char buf[64];
            sprintf_s(buf, "Light Intensity: %.1f\n", m_lightIntensity);
            OutputDebugStringA(buf);
        }
        break;

    case 'P':  // Уменьшить интенсивность
        m_lightIntensity -= m_lightIntensityStep;
        if (m_lightIntensity < 0.0f) m_lightIntensity = 0.0f;
        {
            char buf[64];
            sprintf_s(buf, "Light Intensity: %.1f\n", m_lightIntensity);
            OutputDebugStringA(buf);
        }
        break;

    case '0':  // Сброс до 1.0
        m_lightIntensity = 1.0f;
        OutputDebugStringA("Light Intensity Reset to 1.0\n");
        break;
    case VK_SPACE: {
        if (m_shootCooldown > 0.0f) break;

        XMFLOAT3 camPos = m_camera.GetPosition();

        // Точка, куда летит снаряд (можно независимо от target камеры)
        const float aimHeight = 2.0f;     // выше центра модели
        XMFLOAT3 aimPoint = { 0.0f, aimHeight, 0.0f };

        XMFLOAT3 toAim = {
            aimPoint.x - camPos.x,
            aimPoint.y - camPos.y,
            aimPoint.z - camPos.z
        };
        XMVECTOR dirV = XMVector3Normalize(XMLoadFloat3(&toAim));
        XMFLOAT3 dir;
        XMStoreFloat3(&dir, dirV);

        const float spawnForward = 1.0f;    // вперёд от камеры
        const float spawnUp = 0.3f;    // ← дополнительный подъём точки старта

        XMFLOAT3 spawn = {
            camPos.x + dir.x * spawnForward,
            camPos.y + dir.y * spawnForward + spawnUp,
            camPos.z + dir.z * spawnForward
        };

        if (!m_renderingSystem->SpawnProjectile(
            spawn, dir,
            XMFLOAT4(1.0f, 0.6f, 0.2f, 1.0f),
            20.0f, 0.5f, 25.0f, 10.0f, 3.0f)) {
            OutputDebugStringA("[Spawn] pool full\n");
        }
        m_shootCooldown = 0.15f;
        break;
    }
    }
}

void D3D12App::ResetCamera() {
    m_camera.Reset();
    OutputDebugStringA("Camera reset\n");
}


bool D3D12App::LoadSceneObject(const std::string& objPath,
    const std::string& basePath,
    const DirectX::XMFLOAT4X4& world,
    SceneObject& outObject,
    const MaterialPaths& paths)
{
    try {
        ModelData data = ModelLoader::LoadOBJ(objPath, basePath);

        outObject.vertices = std::move(data.vertices);
        outObject.indices = std::move(data.indices);
        outObject.materials = std::move(data.materials);
        outObject.materialStartIndex = std::move(data.materialStartIndex);
        outObject.materialIndexCount = std::move(data.materialIndexCount);
        outObject.indexCount = (UINT)outObject.indices.size();
        outObject.world = world;

        // Проверка существования файла
        auto fileExists = [](const std::string& path) -> bool {
            if (path.empty()) return false;
            DWORD attrs = GetFileAttributesA(path.c_str());
            return attrs != INVALID_FILE_ATTRIBUTES;
            };


        auto loadMaterialBlock = [&](const MaterialPaths& paths,
            int& outAlbedo, int& outRoughness,
            int& outMetallic, int& outAO, int& outNormal) -> int
            {
                if (m_srvHeapUsed + 5 > m_srvHeapCapacity) return -1;

                int base = (int)m_srvHeapUsed;

                // albedo, roughness, metallic, ao, normal
                const std::string* paths5[5] = {
                    &paths.albedo, &paths.roughness, &paths.metallic, &paths.ao, &paths.normal
                };

                int albedoResIdx = -1;  

                if (fileExists(paths.albedo)) {
                    Texture t = TextureLoader::LoadTexture(m_device.Get(), m_commandList.Get(), paths.albedo);
                    albedoResIdx = (int)m_textures.size();
                    m_textures.push_back(std::move(t));
                }

                if (albedoResIdx < 0) {
                    OutputDebugStringA("[Mat] albedo missing\n");
                    return -1;
                }

                D3D12_CPU_DESCRIPTOR_HANDLE heapStart = m_srvHeap->GetCPUDescriptorHandleForHeapStart();

                for (int i = 0; i < 5; ++i) {
                    int texIdx = albedoResIdx; 

                    if (i > 0 && fileExists(*paths5[i])) {
                        Texture t = TextureLoader::LoadTexture(m_device.Get(), m_commandList.Get(), *paths5[i]);
                        texIdx = (int)m_textures.size();
                        m_textures.push_back(std::move(t));
                    }

                    D3D12_CPU_DESCRIPTOR_HANDLE h = heapStart;
                    h.ptr += (UINT64)(m_srvHeapUsed + i) * m_srvDescriptorSize;

                    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                    srvDesc.Format = m_textures[texIdx].format;
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srvDesc.Texture2D.MipLevels = 1;

                    m_device->CreateShaderResourceView(m_textures[texIdx].resource.Get(), &srvDesc, h);

                    char buf[256];
                    sprintf_s(buf, "  [Mat] slot %d = srv %d (tex %d)\n",
                        i, m_srvHeapUsed + i, texIdx);
                    OutputDebugStringA(buf);
                }

                outAlbedo = base + 0;
                outRoughness = base + 1;
                outMetallic = base + 2;
                outAO = base + 3;
                outNormal = base + 4;

                m_srvHeapUsed += 5;
                return base;
            };

        for (auto& mat : outObject.materials) {
            mat.albedoTexturePath = paths.albedo;
            mat.normalTexturePath = paths.normal;
            mat.roughnessTexturePath = paths.roughness;
            mat.metallicTexturePath = paths.metallic;
            mat.aoTexturePath = paths.ao;

            int a, r, m, o, n;
            loadMaterialBlock(paths, a, r, m, o, n);

            mat.albedoSrv = a;
            mat.roughnessSrv = r;
            mat.metallicSrv = m;
            mat.aoSrv = o;
            mat.normalSrv = n;

            mat.textureIndex = a;   // legacy — geometry pass использует как базовый индекс

            char buf[256];
            sprintf_s(buf, "[Material] '%s': base=%d (A=%d R=%d M=%d AO=%d N=%d)\n",
                mat.name.c_str(), a, a, r, m, o, n);
            OutputDebugStringA(buf);
        }

        outObject.hasTextures = !outObject.materials.empty() &&
            outObject.materials[0].albedoSrv >= 0;

        CreateMeshBuffers(outObject);

        char buf[512];
        sprintf_s(buf, "[Scene] Loaded '%s': %zu verts, %zu idx, %zu materials\n",
            objPath.c_str(), outObject.vertices.size(),
            outObject.indices.size(), outObject.materials.size());
        OutputDebugStringA(buf);
        return true;
    }
    catch (std::exception& e) {
        char buf[512];
        sprintf_s(buf, "[Scene] Failed to load '%s': %s\n", objPath.c_str(), e.what());
        OutputDebugStringA(buf);
        return false;
    }
}

void D3D12App::CreateMeshBuffers(SceneObject& obj) {
    if (obj.vertices.empty() || obj.indices.empty()) return;

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_HEAP_PROPERTIES defaultHeapProps = {};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // ---- Vertex buffer ----
    const UINT vbSize = (UINT)(sizeof(Vertex) * obj.vertices.size());
    bufferDesc.Width = vbSize;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&obj.vbUpload)), "Create VB upload");

    void* data = nullptr;
    ThrowIfFailed(obj.vbUpload->Map(0, nullptr, &data), "Map VB upload");
    memcpy(data, obj.vertices.data(), vbSize);
    obj.vbUpload->Unmap(0, nullptr);

    ThrowIfFailed(m_device->CreateCommittedResource(
        &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&obj.vertexBuffer)), "Create VB");

    // ---- Index buffer ----
    const UINT ibSize = (UINT)(sizeof(uint32_t) * obj.indices.size());
    bufferDesc.Width = ibSize;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&obj.ibUpload)), "Create IB upload");

    ThrowIfFailed(obj.ibUpload->Map(0, nullptr, &data), "Map IB upload");
    memcpy(data, obj.indices.data(), ibSize);
    obj.ibUpload->Unmap(0, nullptr);

    ThrowIfFailed(m_device->CreateCommittedResource(
        &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&obj.indexBuffer)), "Create IB");

    // ---- Копирование ----
    m_commandList->CopyResource(obj.vertexBuffer.Get(), obj.vbUpload.Get());
    m_commandList->CopyResource(obj.indexBuffer.Get(), obj.ibUpload.Get());

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = obj.vertexBuffer.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = obj.indexBuffer.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    m_commandList->ResourceBarrier(2, barriers);

    obj.vbv.BufferLocation = obj.vertexBuffer->GetGPUVirtualAddress();
    obj.vbv.StrideInBytes = sizeof(Vertex);
    obj.vbv.SizeInBytes = vbSize;

    obj.ibv.BufferLocation = obj.indexBuffer->GetGPUVirtualAddress();
    obj.ibv.Format = DXGI_FORMAT_R32_UINT;
    obj.ibv.SizeInBytes = ibSize;
}

SceneObject D3D12App::CreateTerrainTileFromHeightmap(
    const std::vector<float>& heightmap,
    int heightmapWidth, int heightmapHeight,
    int gridRes,
    float worldSize,
    float heightMin, float heightMax,
    float heightScale)
{
    SceneObject tile;

    const float invRange = 1.0f / std::max(1e-6f, heightMax - heightMin);

    auto sampleHeight = [&](float u, float v) -> float {
        float px = u * (heightmapWidth - 1);
        float py = v * (heightmapHeight - 1);
        int x0 = (int)px;
        int y0 = (int)py;
        int x1 = std::min(x0 + 1, heightmapWidth - 1);
        int y1 = std::min(y0 + 1, heightmapHeight - 1);
        float fx = px - x0;
        float fy = py - y0;

        auto at = [&](int x, int y) {
            return heightmap[y * heightmapWidth + x];
            };

        float h00 = at(x0, y0);
        float h10 = at(x1, y0);
        float h01 = at(x0, y1);
        float h11 = at(x1, y1);

        float h0 = h00 * (1 - fx) + h10 * fx;
        float h1 = h01 * (1 - fx) + h11 * fx;
        return h0 * (1 - fy) + h1 * fy;
        };

    const float step = worldSize / (gridRes - 1);

    for (int z = 0; z < gridRes; ++z) {
        for (int x = 0; x < gridRes; ++x) {
            float u = (float)x / (gridRes - 1);
            float v = (float)z / (gridRes - 1);

            float rawH = sampleHeight(u, v);
            float h = (rawH - heightMin) * invRange; 
            h = std::max(0.0f, std::min(1.0f, h));    

            Vertex vtx;
            vtx.position.x = u * worldSize - worldSize * 0.5f;
            vtx.position.y = h * heightScale;
            vtx.position.z = v * worldSize - worldSize * 0.5f;
            vtx.normal = XMFLOAT3(0, 1, 0);  
            vtx.texcoord = XMFLOAT2(u, v);
            tile.vertices.push_back(vtx);
        }
    }


    for (int z = 1; z < gridRes - 1; ++z) {
        for (int x = 1; x < gridRes - 1; ++x) {
            int idx = z * gridRes + x;
            int left = z * gridRes + (x - 1);
            int right = z * gridRes + (x + 1);
            int up = (z - 1) * gridRes + x;
            int down = (z + 1) * gridRes + x;

            XMVECTOR pL = XMLoadFloat3(&tile.vertices[left].position);
            XMVECTOR pR = XMLoadFloat3(&tile.vertices[right].position);
            XMVECTOR pU = XMLoadFloat3(&tile.vertices[up].position);
            XMVECTOR pD = XMLoadFloat3(&tile.vertices[down].position);

            XMVECTOR dx = pR - pL;
            XMVECTOR dz = pD - pU;
            XMVECTOR n = XMVector3Normalize(XMVector3Cross(dz, dx));

            XMStoreFloat3(&tile.vertices[idx].normal, n);
        }
    }


    for (int z = 0; z < gridRes - 1; ++z) {
        for (int x = 0; x < gridRes - 1; ++x) {
            uint32_t a = z * gridRes + x;
            uint32_t b = z * gridRes + (x + 1);
            uint32_t c = (z + 1) * gridRes + x;
            uint32_t d = (z + 1) * gridRes + (x + 1);


            tile.indices.push_back(a);
            tile.indices.push_back(c);
            tile.indices.push_back(b);

            tile.indices.push_back(b);
            tile.indices.push_back(c);
            tile.indices.push_back(d);
        }
    }

    Material mat;
    mat.name = "terrain";
    mat.roughnessFactor = 0.8f;
    mat.metallicFactor = 0.0f;
    mat.aoFactor = 1.0f;
    mat.albedoFactor = XMFLOAT3(0.5f, 0.5f, 0.5f);

    tile.materials.push_back(mat);
    tile.materialStartIndex.push_back(0);
    tile.materialIndexCount.push_back((uint32_t)tile.indices.size());
    tile.indexCount = (uint32_t)tile.indices.size();
    tile.hasTextures = false;   

    return tile;
}