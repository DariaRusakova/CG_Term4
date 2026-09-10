#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>

struct CascadeData {
    DirectX::XMFLOAT4X4 viewProj;
    float nearPlane;
    float farPlane;
    DirectX::XMFLOAT2 padding;
};

struct ShadowMapCB {
    DirectX::XMFLOAT4X4 lightViewProj[4];
    DirectX::XMFLOAT4 cascadeSplits;
    DirectX::XMFLOAT4 lightDirection;
    DirectX::XMFLOAT4 shadowBias;
    DirectX::XMFLOAT4 textureSize;
    DirectX::XMFLOAT4 cameraPos;
    DirectX::XMFLOAT4 lightPos;  // <-- Добавить
};

class ShadowMapSystem {
public:
    static const int CASCADE_COUNT = 4;
    static const int SHADOW_MAP_SIZE = 2048;

    void Initialize(ID3D12Device* device);
    void UpdateCascades(const DirectX::XMMATRIX& lightView, const DirectX::XMMATRIX& proj,
        const DirectX::XMFLOAT3& cameraPos, float nearZ, float farZ);

    ID3D12Resource* GetDepthResource() const { return m_shadowMap.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const { return m_dsvHeap->GetCPUDescriptorHandleForHeapStart(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRV() const { return m_srvHeap->GetGPUDescriptorHandleForHeapStart(); }
    ID3D12DescriptorHeap* GetSRVHeap() const { return m_srvHeap.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVForCascade(int cascade) const { return m_cascadeDSV[cascade]; }

    const ShadowMapCB& GetConstantBufferData() const { return m_cbData; }
    ID3D12Resource* GetConstantBuffer() const { return m_cbResource.Get(); }
    void* GetConstantBufferDataPointer() const { return m_cbDataBegin; }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_shadowMap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_cbResource;
    D3D12_CPU_DESCRIPTOR_HANDLE m_cascadeDSV[CASCADE_COUNT];
    void* m_cbDataBegin = nullptr;
    ShadowMapCB m_cbData;

    void CreateDescriptorHeaps(ID3D12Device* device);
    void CreateTexture(ID3D12Device* device);
    void CreateConstantBuffer(ID3D12Device* device);
};