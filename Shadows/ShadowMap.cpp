#include "ShadowMap.h"
#include "libs/d3dx12.h"
#include <stdexcept>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

inline void ThrowIfFailed(HRESULT hr, const char* msg) {
    if (FAILED(hr)) throw std::runtime_error(msg);
}

void ShadowMapSystem::Initialize(ID3D12Device* device) {
    CreateDescriptorHeaps(device);
    CreateTexture(device);
    CreateConstantBuffer(device);
}

void ShadowMapSystem::CreateDescriptorHeaps(ID3D12Device* device) {
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
    dsvDesc.NumDescriptors = CASCADE_COUNT;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&m_dsvHeap)), "Create DSV Heap");

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.NumDescriptors = 1;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)), "Create SRV Heap");
}

void ShadowMapSystem::CreateTexture(ID3D12Device* device) {
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = SHADOW_MAP_SIZE;
    texDesc.Height = SHADOW_MAP_SIZE;
    texDesc.DepthOrArraySize = CASCADE_COUNT;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearOptimized = {};
    clearOptimized.Format = DXGI_FORMAT_D32_FLOAT;
    clearOptimized.DepthStencil.Depth = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearOptimized,
        IID_PPV_ARGS(&m_shadowMap)), "Create Shadow Map Texture");

    for (int i = 0; i < CASCADE_COUNT; ++i) {
        m_cascadeDSV[i].ptr = 0;
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
    dsvDesc.Texture2DArray.MipSlice = 0;
    dsvDesc.Texture2DArray.ArraySize = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    UINT dsvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    for (int i = 0; i < CASCADE_COUNT; ++i) {
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        device->CreateDepthStencilView(m_shadowMap.Get(), &dsvDesc, dsvHandle);
        m_cascadeDSV[i] = dsvHandle;
        dsvHandle.ptr += dsvSize;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = CASCADE_COUNT;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    device->CreateShaderResourceView(m_shadowMap.Get(), &srvDesc, m_srvHeap->GetCPUDescriptorHandleForHeapStart());
}

void ShadowMapSystem::CreateConstantBuffer(ID3D12Device* device) {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = sizeof(ShadowMapCB);
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_cbResource)), "Create Shadow CB");

    D3D12_RANGE readRange = { 0, 0 };
    ThrowIfFailed(m_cbResource->Map(0, &readRange, &m_cbDataBegin), "Map Shadow CB");
}

void ShadowMapSystem::UpdateCascades(const XMMATRIX& lightView, const XMMATRIX& proj,
    const XMFLOAT3& cameraPos, float nearZ, float farZ) {

    float splits[CASCADE_COUNT + 1];
    splits[0] = nearZ;
    for (int i = 1; i < CASCADE_COUNT; ++i) {
        float p = (float)i / CASCADE_COUNT;
        float logSplit = nearZ * pow(farZ / nearZ, p);
        float uniformSplit = nearZ + (farZ - nearZ) * p;
        splits[i] = 0.7f * logSplit + 0.3f * uniformSplit;
    }
    splits[CASCADE_COUNT] = farZ;

    m_cbData.cascadeSplits = XMFLOAT4(splits[1], splits[2], splits[3], 0.0f);
    m_cbData.lightDirection = XMFLOAT4(0.0f, -1.0f, 0.0f, 0.0f); // Ñâåò ñâåðõó âíèç
    m_cbData.shadowBias = XMFLOAT4(0.001f, 0.001f, 0.001f, 0.001f);
    m_cbData.textureSize = XMFLOAT4((float)SHADOW_MAP_SIZE, (float)SHADOW_MAP_SIZE,
        1.0f / (float)SHADOW_MAP_SIZE, 1.0f / (float)SHADOW_MAP_SIZE);
    m_cbData.cameraPos = XMFLOAT4(cameraPos.x, cameraPos.y, cameraPos.z, 1.0f);

    // ÈÇÂËÅÊÀÅÌ ÏÎÇÈÖÈÞ ÑÂÅÒÀ ÈÇ lightView
    XMVECTOR lightPos = lightView.r[3];
    XMFLOAT3 lightPosFloat;
    XMStoreFloat3(&lightPosFloat, lightPos);
    m_cbData.lightPos = XMFLOAT4(lightPosFloat.x, lightPosFloat.y, lightPosFloat.z, 1.0f);

    float orthoSize[4] = { 30.f, 50.f, 80.f, 120.f };
    XMMATRIX lightProj;
    for (int i = 0; i < CASCADE_COUNT; ++i) {
        lightProj = XMMatrixOrthographicOffCenterLH(
            -orthoSize[i], orthoSize[i],
            -orthoSize[i], orthoSize[i],
            1.0f, 200.0f);

        XMMATRIX viewProj = lightView * lightProj;
        XMStoreFloat4x4(&m_cbData.lightViewProj[i], XMMatrixTranspose(viewProj));
    }

    memcpy(m_cbDataBegin, &m_cbData, sizeof(ShadowMapCB));
}