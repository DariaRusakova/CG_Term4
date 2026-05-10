#include "RenderingSystem.h"
#include "../Shaders/Shaders.h"
#include "../Shaders/TessellationShaders.h"
#include <d3dcompiler.h>
#include <stdexcept>
#include <directxmath.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

RenderingSystem::RenderingSystem()
    : m_gbuffer(std::make_unique<GBuffer>())
    , m_lightCBData(nullptr)
    , m_width(0)
    , m_height(0) {
}

RenderingSystem::~RenderingSystem() {
}

void RenderingSystem::Initialize(ID3D12Device* device, UINT width, UINT height) {
    m_width = width;
    m_height = height;

    // Создаем кучи дескрипторов для GBuffer
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = GBuffer::GB_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 1;

    HRESULT hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_gbufferRtvHeap));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create GBuffer RTV heap");
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = GBuffer::GB_COUNT + 16;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeapDesc.NodeMask = 1;

    hr = device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_gbufferSrvHeap));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create GBuffer SRV heap");
    }

    UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    UINT srvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    m_gbuffer->Initialize(device, width, height);

    m_gbuffer->CreateDescriptors(device,
        m_gbufferRtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_gbufferSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_gbufferSrvHeap.Get(),
        rtvSize, srvSize);

    CreateLightBuffers(device);
    CreateFullscreenQuad(device);
    CreateLightingPassPipeline(device);

    ClearLights();

    // Добавляем источники света по умолчанию
    Light sunLight;
    sunLight.type = LightType::Directional;
    sunLight.direction = XMFLOAT3(0.3f, -0.8f, 0.5f);
    sunLight.color = XMFLOAT4(1.0f, 0.9f, 0.7f, 1.0f);
    sunLight.intensity = 1.8f;
    AddLight(sunLight);

    Light centerLight;
    centerLight.type = LightType::Point;
    centerLight.position = XMFLOAT3(0.0f, 4.0f, 0.0f);
    centerLight.color = XMFLOAT4(1.0f, 0.85f, 0.6f, 1.0f);
    centerLight.intensity = 40.0f;
    centerLight.range = 15.0f;
    AddLight(centerLight);

    OutputDebugStringA("RenderingSystem initialized\n");
}

void RenderingSystem::AddLight(const Light& light) {
    m_lights.push_back(light);
}

void RenderingSystem::ClearLights() {
    m_lights.clear();
}

void RenderingSystem::CreateLightBuffers(ID3D12Device* device) {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Alignment = 0;
    cbDesc.Width = (sizeof(LightBuffer) + 255) & ~255;
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.Format = DXGI_FORMAT_UNKNOWN;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.SampleDesc.Quality = 0;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    cbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_lightConstantBuffer)
    );

    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create light constant buffer");
    }

    D3D12_RANGE readRange = { 0, 0 };
    hr = m_lightConstantBuffer->Map(0, &readRange, &m_lightCBData);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to map light constant buffer");
    }
}

void RenderingSystem::CreateFullscreenQuad(ID3D12Device* device) {
    struct FullscreenVertex {
        XMFLOAT3 position;
        XMFLOAT2 texcoord;
    };

    FullscreenVertex vertices[] = {
        { XMFLOAT3(-1.0f,  1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(1.0f,  1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) }
    };

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = sizeof(vertices);
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_fullscreenVB)
    );

    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create fullscreen VB");
    }

    void* data;
    D3D12_RANGE readRange = { 0, 0 };
    hr = m_fullscreenVB->Map(0, &readRange, &data);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to map fullscreen VB");
    }

    memcpy(data, vertices, sizeof(vertices));
    m_fullscreenVB->Unmap(0, nullptr);

    m_fullscreenVBView.BufferLocation = m_fullscreenVB->GetGPUVirtualAddress();
    m_fullscreenVBView.StrideInBytes = sizeof(FullscreenVertex);
    m_fullscreenVBView.SizeInBytes = sizeof(vertices);
}

void RenderingSystem::CreateLightingPassPipeline(ID3D12Device* device) {
    ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

    UINT compileFlags = 0;
#ifdef _DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    // Компилируем вершинный шейдер для освещения
    HRESULT hr = D3DCompile(
        Shaders::LightPassVS,
        strlen(Shaders::LightPassVS),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "vs_5_0",
        compileFlags,
        0,
        &vsBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        throw std::runtime_error("Failed to compile lighting VS");
    }

    // Компилируем пиксельный шейдер для освещения
    hr = D3DCompile(
        Shaders::LightPassPS,
        strlen(Shaders::LightPassPS),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        compileFlags,
        0,
        &psBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        throw std::runtime_error("Failed to compile lighting PS");
    }

    // Создаем Descriptor Range для трех текстур GBuffer
    D3D12_DESCRIPTOR_RANGE descRange = {};
    descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descRange.NumDescriptors = 3;
    descRange.BaseShaderRegister = 0;
    descRange.RegisterSpace = 0;
    descRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Создаем Root Parameters
    D3D12_ROOT_PARAMETER rootParams[2] = {};

    // Параметр 0: Descriptor Table для GBuffer текстур
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &descRange;

    // Параметр 1: CBV для буфера света
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].Descriptor.RegisterSpace = 0;

    // Статический сэмплер
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
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
    rootSigDesc.NumParameters = 2;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;

    hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        throw std::runtime_error("Failed to serialize lighting root signature");
    }

    hr = device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&m_lightingRootSig)
    );

    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create lighting root signature");
    }

    // Input Layout для полноэкранного квадрата
    D3D12_INPUT_ELEMENT_DESC inputDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // Настройка Pipeline State
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputDesc, _countof(inputDesc) };
    psoDesc.pRootSignature = m_lightingRootSig.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_lightingPSO));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create lighting PSO");
    }
}

void RenderingSystem::Render(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* depthStencil,
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
    ID3D12RootSignature* geometryRootSig,
    ID3D12PipelineState* geometryPSO,
    const RenderData* renderData) {

    if (!renderData || !renderData->vertexBuffer || !renderData->indexBuffer) {
        OutputDebugStringA("Invalid render data\n");
        return;
    }

    // Обновляем данные света
    LightBuffer* lightData = static_cast<LightBuffer*>(m_lightCBData);
    if (!lightData) {
        OutputDebugStringA("Light buffer not mapped\n");
        return;
    }

    memset(lightData, 0, sizeof(LightBuffer));
    lightData->lightCount = static_cast<UINT>(m_lights.size());

    for (size_t i = 0; i < m_lights.size() && i < 16; ++i) {
        lightData->position_type[i] = m_lights[i].GetAsFloat4();
        lightData->direction[i] = m_lights[i].GetDirectionAsFloat4();
        lightData->color_intensity[i] = XMFLOAT4(
            m_lights[i].color.x,
            m_lights[i].color.y,
            m_lights[i].color.z,
            m_lights[i].intensity
        );
        lightData->range_spotAngle[i] = XMFLOAT4(
            m_lights[i].range,
            m_lights[i].spotAngle,
            0.0f,
            0.0f
        );
    }

    // ============ GEOMETRY PASS ============
    D3D12_CPU_DESCRIPTOR_HANDLE gbufferRTVs[GBuffer::GB_COUNT];
    for (int i = 0; i < GBuffer::GB_COUNT; ++i) {
        gbufferRTVs[i] = m_gbuffer->GetRTV(static_cast<GBuffer::GBufferType>(i));
    }

    // Выбираем PSO и Root Signature
    ID3D12PipelineState* activePSO = nullptr;
    ID3D12RootSignature* activeRootSig = nullptr;

    if (renderData->useTessellation && renderData->tessellationPSO && renderData->tessellationRootSig) {
        activePSO = renderData->tessellationPSO;
        activeRootSig = renderData->tessellationRootSig;
        //OutputDebugStringA("Using tessellation pipeline\n");
    }
    else {
        activePSO = geometryPSO;
        activeRootSig = geometryRootSig;
        //OutputDebugStringA("Using standard geometry pipeline\n");
    }

    // ВАЖНО: Устанавливаем корневую подпись ДО всего остального
    cmdList->SetGraphicsRootSignature(activeRootSig);
    cmdList->SetPipelineState(activePSO);

    // Устанавливаем render targets и depth stencil
    cmdList->OMSetRenderTargets(GBuffer::GB_COUNT, gbufferRTVs, TRUE, &dsvHandle);

    // Очищаем GBuffer
    float albedoClear[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float dataClear[] = { 0.0f, 0.0f, 0.0f, 1.0f };

    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_ALBEDO], albedoClear, 0, nullptr);
    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_WORLD_POS], dataClear, 0, nullptr);
    cmdList->ClearRenderTargetView(gbufferRTVs[GBuffer::GB_NORMAL], dataClear, 0, nullptr);
    cmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // ВАЖНО: Устанавливаем descriptor heaps перед установкой descriptor tables
    ID3D12DescriptorHeap* ppHeaps[] = { renderData->modelSrvHeap };
    cmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    // Устанавливаем константный буфер сцены (b0)
    if (renderData->cbvAddress != 0) {
        cmdList->SetGraphicsRootConstantBufferView(0, renderData->cbvAddress);
    }

    // Устанавливаем буфер тесселяции (b1) если используется тесселяция
    if (renderData->useTessellation && renderData->tessCBVAddress != 0) {
        cmdList->SetGraphicsRootConstantBufferView(1, renderData->tessCBVAddress);
    }

    // Устанавливаем вершинный и индексный буферы
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = renderData->vertexBuffer->GetGPUVirtualAddress();
    vbv.StrideInBytes = sizeof(Vertex);
    vbv.SizeInBytes = sizeof(Vertex) * renderData->indexCount * 3; // Максимальный размер

    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = renderData->indexBuffer->GetGPUVirtualAddress();
    ibv.SizeInBytes = renderData->indexCount * sizeof(uint32_t);
    ibv.Format = DXGI_FORMAT_R32_UINT;

    cmdList->IASetVertexBuffers(0, 1, &vbv);
    cmdList->IASetIndexBuffer(&ibv);

    // ВАЖНО: Устанавливаем примитивную топологию
    if (renderData->useTessellation) {
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    }
    else {
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    // Устанавливаем дескриптор текстуры (только если используем материалы)
    if (renderData->materials && renderData->materialStartIndex && renderData->materialIndexCount) {
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpuStart = renderData->modelSrvHeap->GetGPUDescriptorHandleForHeapStart();

        // ВАЖНО: Устанавливаем descriptor table перед отрисовкой
        if (renderData->materials[0].textureIndex >= 0) {
            D3D12_GPU_DESCRIPTOR_HANDLE texHandle;
            texHandle.ptr = srvGpuStart.ptr +
                static_cast<UINT64>(renderData->materials[0].textureIndex) * renderData->srvDescriptorSize;
            cmdList->SetGraphicsRootDescriptorTable(2, texHandle);
        }
        else {
            cmdList->SetGraphicsRootDescriptorTable(2, srvGpuStart);
        }

        // Отрисовываем все материалы
        for (UINT i = 0; i < renderData->numMaterials; i++) {
            if (renderData->materialIndexCount[i] > 0) {
                // Обновляем текстуру для каждого материала
                if (i > 0 && renderData->materials[i].textureIndex >= 0) {
                    D3D12_GPU_DESCRIPTOR_HANDLE texHandle;
                    texHandle.ptr = srvGpuStart.ptr +
                        static_cast<UINT64>(renderData->materials[i].textureIndex) * renderData->srvDescriptorSize;
                    cmdList->SetGraphicsRootDescriptorTable(2, texHandle);
                }

                // Отрисовываем группу
                cmdList->DrawIndexedInstanced(
                    renderData->materialIndexCount[i],
                    1,
                    renderData->materialStartIndex[i],
                    0,
                    0
                );
            }
        }
    }
    else {
        // Если нет материалов, рисуем все сразу с первой текстурой
        if (renderData->modelSrvHeap) {
            D3D12_GPU_DESCRIPTOR_HANDLE srvGpuStart = renderData->modelSrvHeap->GetGPUDescriptorHandleForHeapStart();
            cmdList->SetGraphicsRootDescriptorTable(2, srvGpuStart);
        }

        cmdList->DrawIndexedInstanced(renderData->indexCount, 1, 0, 0, 0);
    }

    // ============ LIGHTING PASS ============
    // Переводим GBuffer в состояние SRV
    D3D12_RESOURCE_BARRIER barriers[GBuffer::GB_COUNT];
    for (int i = 0; i < GBuffer::GB_COUNT; ++i) {
        barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barriers[i].Transition.pResource = m_gbuffer->GetResource(static_cast<GBuffer::GBufferType>(i));
        barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    cmdList->ResourceBarrier(GBuffer::GB_COUNT, barriers);

    // Рендерим освещение на back buffer
    cmdList->SetGraphicsRootSignature(m_lightingRootSig.Get());
    cmdList->SetPipelineState(m_lightingPSO.Get());
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Устанавливаем descriptor heaps для GBuffer SRV
    ID3D12DescriptorHeap* lightHeaps[] = { m_gbufferSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(_countof(lightHeaps), lightHeaps);

    D3D12_GPU_DESCRIPTOR_HANDLE gbufferGpuHandle = m_gbufferSrvHeap->GetGPUDescriptorHandleForHeapStart();
    cmdList->SetGraphicsRootDescriptorTable(0, gbufferGpuHandle);
    cmdList->SetGraphicsRootConstantBufferView(1, m_lightConstantBuffer->GetGPUVirtualAddress());

    D3D12_VIEWPORT fullscreenViewport = {
        0.0f, 0.0f,
        static_cast<float>(m_width),
        static_cast<float>(m_height),
        0.0f, 1.0f
    };
    D3D12_RECT fullscreenScissor = {
        0, 0,
        static_cast<LONG>(m_width),
        static_cast<LONG>(m_height)
    };

    cmdList->RSSetViewports(1, &fullscreenViewport);
    cmdList->RSSetScissorRects(1, &fullscreenScissor);

    cmdList->IASetVertexBuffers(0, 1, &m_fullscreenVBView);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmdList->DrawInstanced(4, 1, 0, 0);

    // Возвращаем GBuffer в RTV для следующего кадра
    for (int i = 0; i < GBuffer::GB_COUNT; ++i) {
        barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barriers[i].Transition.pResource = m_gbuffer->GetResource(static_cast<GBuffer::GBufferType>(i));
        barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    cmdList->ResourceBarrier(GBuffer::GB_COUNT, barriers);
}