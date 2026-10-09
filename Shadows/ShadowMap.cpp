#include "ShadowMap.h"
#include "libs/d3dx12.h"
#include <stdexcept>
#include <cmath>
#include <cstdio>
#include <windows.h>

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
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&m_dsvHeap)),
        "Create DSV Heap");

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.NumDescriptors = 1;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)),
        "Create SRV Heap");
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

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
    dsvDesc.Texture2DArray.MipSlice = 0;
    dsvDesc.Texture2DArray.ArraySize = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    UINT dsvSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

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

    device->CreateShaderResourceView(
        m_shadowMap.Get(), &srvDesc,
        m_srvHeap->GetCPUDescriptorHandleForHeapStart());
}

void ShadowMapSystem::CreateConstantBuffer(ID3D12Device* device) {
    for (int i = 0; i < CASCADE_COUNT; ++i) {
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
            IID_PPV_ARGS(&m_cbResource[i])), "Create Shadow CB");

        D3D12_RANGE readRange = { 0, 0 };
        ThrowIfFailed(m_cbResource[i]->Map(0, &readRange, &m_cbDataBegin[i]),
            "Map Shadow CB");
    }
}

void ShadowMapSystem::UpdateCascades(const XMFLOAT3& lightDir,
    const XMFLOAT3& cameraPos,
    const XMFLOAT3& cameraForward,
    float fovY,
    float aspect,
    float nearZ,
    float farZ)
{
    float splits[CASCADE_COUNT + 1] = { nearZ, 600.0f, 2000.0f, 5000.0f, 15000.0f };
    m_cbData.cascadeSplits = XMFLOAT4(splits[1], splits[2], splits[3], splits[4]);

    XMVECTOR L = XMVector3Normalize(XMLoadFloat3(&lightDir));
    m_cbData.lightDirection = XMFLOAT4(XMVectorGetX(L), XMVectorGetY(L), XMVectorGetZ(L), 0.0f);
    m_cbData.textureSize = XMFLOAT4((float)SHADOW_MAP_SIZE, (float)SHADOW_MAP_SIZE,
        1.0f / (float)SHADOW_MAP_SIZE, 1.0f / (float)SHADOW_MAP_SIZE);
    m_cbData.cameraPos = XMFLOAT4(cameraPos.x, cameraPos.y, cameraPos.z, 1.0f);

    XMVECTOR camPosV = XMLoadFloat3(&cameraPos);
    XMVECTOR camFwdV = XMVector3Normalize(XMLoadFloat3(&cameraForward));

    XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    if (fabsf(XMVectorGetY(L)) > 0.99f) {
        lightUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }

    float tanHalfV = tanf(fovY * 0.5f);
    float tanHalfH = tanHalfV * aspect;

    for (int i = 0; i < CASCADE_COUNT; ++i) {
        float n = splits[i];
        float f = splits[i + 1];

        XMVECTOR center = camPosV + camFwdV * ((n + f) * 0.5f);

        float cornerX = f * tanHalfH;
        float cornerY = f * tanHalfV;
        float halfDepth = (f - n) * 0.5f;
        float radius = sqrtf(cornerX * cornerX + cornerY * cornerY + halfDepth * halfDepth);

        radius = ceilf(radius / 32.0f) * 32.0f;

        XMMATRIX lightView = XMMatrixLookAtLH(center - L * 20000.0f, center, lightUp);

        float worldUnitsPerTexel = (radius * 2.0f) / (float)SHADOW_MAP_SIZE;
        XMVECTOR centerLightSpace = XMVector3TransformCoord(center, lightView);

        float cx = XMVectorGetX(centerLightSpace);
        float cy = XMVectorGetY(centerLightSpace);

        float snappedX = floorf(cx / worldUnitsPerTexel) * worldUnitsPerTexel;
        float snappedY = floorf(cy / worldUnitsPerTexel) * worldUnitsPerTexel;

        XMVECTOR offset = XMVectorSet(snappedX - cx, snappedY - cy, 0.0f, 0.0f);
        XMMATRIX invLightView = XMMatrixInverse(nullptr, lightView);
        XMVECTOR snappedCenter = center + XMVector3TransformNormal(offset, invLightView);

        lightView = XMMatrixLookAtLH(snappedCenter - L * 20000.0f, snappedCenter, lightUp);

        const float distanceToCenter = 20000.0f;
        const float nearPlane = distanceToCenter - radius - 100.0f;
        const float farPlane = distanceToCenter + radius + 100.0f;

        XMMATRIX proj = XMMatrixOrthographicOffCenterLH(
            -radius, radius,
            -radius, radius,
            nearPlane, farPlane);

        XMMATRIX vp = lightView * proj;

        XMStoreFloat4x4(&m_cbData.lightViewProj[i], XMMatrixTranspose(vp));
    }

    for (int i = 0; i < CASCADE_COUNT; ++i) {
        m_cbData.cascadeIndex = i;
        memcpy(m_cbDataBegin[i], &m_cbData, sizeof(ShadowMapCB));
    }
}

void ShadowMapSystem::SetCurrentCascadeIndex(int index) {
    m_cbData.cascadeIndex = index;
    if (m_cbDataBegin) {
        memcpy(m_cbDataBegin, &m_cbData, sizeof(ShadowMapCB));
    }
}