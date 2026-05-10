#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

class GBuffer {
public:
    enum GBufferType {
        GB_ALBEDO = 0,
        GB_WORLD_POS,
        GB_NORMAL,
        GB_COUNT
    };

    void Initialize(ID3D12Device* device, UINT width, UINT height);
    void Release();
    void CreateDescriptors(ID3D12Device* device,
        D3D12_CPU_DESCRIPTOR_HANDLE rtvStart,
        D3D12_CPU_DESCRIPTOR_HANDLE srvStart,
        ID3D12DescriptorHeap* srvHeap,
        UINT rtvDescriptorSize,
        UINT srvDescriptorSize);

    ID3D12Resource* GetResource(GBufferType type) const { return m_resources[type].Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(GBufferType type) const { return m_rtvHandles[type]; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetSRV(GBufferType type) const { return m_srvHandles[type]; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUSRV(GBufferType type) const { return m_gpuSrvHandles[type]; }
    UINT GetWidth() const { return m_width; }
    UINT GetHeight() const { return m_height; }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_resources[GB_COUNT];
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandles[GB_COUNT];
    D3D12_CPU_DESCRIPTOR_HANDLE m_srvHandles[GB_COUNT];
    D3D12_GPU_DESCRIPTOR_HANDLE m_gpuSrvHandles[GB_COUNT];
    UINT m_width = 0;
    UINT m_height = 0;
};