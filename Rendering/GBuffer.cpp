#include "GBuffer.h"
#include <stdexcept>

void GBuffer::Initialize(ID3D12Device* device, UINT width, UINT height) {
    m_width = width;
    m_height = height;

    DXGI_FORMAT formats[GB_COUNT] = {
        DXGI_FORMAT_R8G8B8A8_UNORM,     // Albedo
        DXGI_FORMAT_R32G32B32A32_FLOAT,  // World Position
        DXGI_FORMAT_R32G32B32A32_FLOAT   // Normal
    };

    D3D12_CLEAR_VALUE clearValues[GB_COUNT] = {
        { DXGI_FORMAT_R8G8B8A8_UNORM, { 0.0f, 0.0f, 0.0f, 1.0f } },
        { DXGI_FORMAT_R32G32B32A32_FLOAT, { 0.0f, 0.0f, 0.0f, 1.0f } },
        { DXGI_FORMAT_R32G32B32A32_FLOAT, { 0.0f, 0.0f, 0.0f, 1.0f } }
    };

    for (int i = 0; i < GB_COUNT; ++i) {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = formats[i];
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        if (FAILED(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValues[i], IID_PPV_ARGS(&m_resources[i])))) {
            throw std::runtime_error("Failed to create GBuffer resource");
        }
    }
}

void GBuffer::Release() {
    for (int i = 0; i < GB_COUNT; ++i) {
        m_resources[i].Reset();
    }
}

void GBuffer::CreateDescriptors(ID3D12Device* device,
    D3D12_CPU_DESCRIPTOR_HANDLE rtvStart,
    D3D12_CPU_DESCRIPTOR_HANDLE srvStart,
    ID3D12DescriptorHeap* srvHeap,
    UINT rtvDescriptorSize,
    UINT srvDescriptorSize) {
    DXGI_FORMAT formats[GB_COUNT] = {
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R32G32B32A32_FLOAT,
        DXGI_FORMAT_R32G32B32A32_FLOAT
    };

    for (int i = 0; i < GB_COUNT; ++i) {
        // Создаем RTV для каждой текстуры GBuffer
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
        rtvHandle.ptr = rtvStart.ptr + i * rtvDescriptorSize;
        device->CreateRenderTargetView(m_resources[i].Get(), nullptr, rtvHandle);
        m_rtvHandles[i] = rtvHandle;

        // Создаем SRV для каждой текстуры GBuffer
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
        srvHandle.ptr = srvStart.ptr + i * srvDescriptorSize;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = formats[i];
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        device->CreateShaderResourceView(m_resources[i].Get(), &srvDesc, srvHandle);
        m_srvHandles[i] = srvHandle;

        // Сохраняем GPU дескриптор, используя переданный srvHeap
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvHeap->GetGPUDescriptorHandleForHeapStart();
        m_gpuSrvHandles[i].ptr = gpuHandle.ptr + i * srvDescriptorSize;
    }
}