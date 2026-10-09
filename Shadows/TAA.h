#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>

class TAASystem {
public:
    static constexpr int kJitterCount = 8;

    struct alignas(256) TAACB {
        DirectX::XMFLOAT4X4 invViewProj;
        DirectX::XMFLOAT4X4 prevViewProj;
        DirectX::XMFLOAT4   cameraPos;
        DirectX::XMFLOAT4   prevCameraPos;
        DirectX::XMFLOAT2   resolution;
        DirectX::XMFLOAT2   invResolution;
        float               blendFactor;
        int                 historyValid;
        float               _pad[2];
    };
    static_assert(sizeof(TAACB) % 256 == 0, "TAACB must be 256-aligned");

    TAASystem() = default;
    ~TAASystem() = default;

    void Initialize(ID3D12Device* device, UINT width, UINT height);
    void Release();

    DirectX::XMFLOAT2 GetJitterOffset(UINT frameIndex);

    void SetEnabled(bool e) { m_enabled = e; }
    bool IsEnabled() const { return m_enabled; }

    void InvalidateHistory() { m_historyValid = false; }
    bool IsHistoryValid() const { return m_historyValid; }
    void MarkHistoryValid() { m_historyValid = true; }

    ID3D12Resource* GetHistoryResource(int idx) const {
        return m_historyColor[idx & 1].Get();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetHistorySRVGPU(int idx) const {
        return m_historySRVGPU[idx & 1];
    }

    int GetCurrentHistoryIndex() const { return m_currentHistory; }

    ID3D12Resource* GetConstantBuffer() const { return m_cbResource.Get(); }
    TAACB* GetCBData() { return &m_cbData; }
    void            UploadCB();

    void SavePrevFrame(const DirectX::XMFLOAT4X4& viewProjNoJitter,
        const DirectX::XMFLOAT3& cameraPos);

    void Resolve(ID3D12GraphicsCommandList* cmdList,
        ID3D12DescriptorHeap* srvHeap,
        D3D12_GPU_DESCRIPTOR_HANDLE currentColorSRV);

    void SetHistorySRVSlots(D3D12_GPU_DESCRIPTOR_HANDLE srv0,
        D3D12_GPU_DESCRIPTOR_HANDLE srv1) {
        m_historySRVGPU[0] = srv0;
        m_historySRVGPU[1] = srv1;
    }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_historyColor[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_cbResource;
    void* m_cbDataBegin = nullptr;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_historyRtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_historyRTV[2] = {};

    D3D12_GPU_DESCRIPTOR_HANDLE m_historySRVGPU[2] = {};

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_resolveRootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_resolvePSO;

    DirectX::XMFLOAT2 m_jitterSequence[kJitterCount];

    UINT m_jitterIndex = 0;
    UINT m_currentHistory = 0;
    UINT m_width = 0;
    UINT m_height = 0;

    bool m_enabled = false;
    bool m_historyValid = false;

    TAACB m_cbData = {};

    void BuildJitterSequence();
    void CreateHistoryTextures(ID3D12Device* device, UINT width, UINT height);
    void CreateHistoryRTVs(ID3D12Device* device);
    void CreateConstantBuffer(ID3D12Device* device);
    void CreateResolvePipeline(ID3D12Device* device);
};